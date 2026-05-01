#include "pch.h"
#include "SetSupportWeaponTypeId.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

// Custom support-weapon categories.
//
// EquipIdTableImpl::GetSupportWeaponTypeId(equipId) is a hardcoded switch on
// equipId ranges, returning a vanilla SWP_TYPE_* enum value (0..0x16) or
// 0x17 fallback. Every native consumer that switches on the result —
// ThrowingImpl::UpdateAction (the per-frame throwable dispatcher; picks
// which UpdateAction* runs), GetBlastParamByEquipId (which BlastParameter
// row applies), ActivateThrowingAtEmptyWork (blast velocity precompute,
// decoy/special-action bit flags), Lua scripts' IsDecoy / IsMine / IsC4
// checks — keys off the same 0..0x16 enum. Anything outside that range
// falls through to default everywhere, which means the throwable does
// nothing.
//
// A custom category therefore consists of three pieces:
//
//   1. A FRESH IDENTITY — `name` and a fresh swpType id (>= 0x80) exposed to
//      Lua as TppEquip.SWP_TYPE_<name>. Scripts can branch on this directly
//      via V_FrameWork.GetSupportWeaponCategory(equipId), which sees the
//      true id (not the spoof below).
//
//   2. A NATIVE BEHAVIOR TEMPLATE — `behavior` picks one of the 5
//      throwable-action templates the engine implements. The framework
//      spoofs the matching vanilla swpType to native callers of
//      GetSupportWeaponTypeId so the engine's hardcoded switches dispatch
//      through the template's per-frame action update, animations, sound,
//      and detonation trigger. There is no sixth template — the engine
//      doesn't have other throwable code paths.
//
//   3. CUSTOM BLAST PARAMETERS (optional) — `blast = { flag, maxRange,
//      optRange }` overrides the BlastParameter struct returned by
//      GetBlastParamByEquipId. The custom struct is heap-stable and lives
//      in the registry for the process lifetime.
//
// The result: a category that has its own name, its own blast curve, and
// its own Lua-side identity, while still being a fully working throwable
// because it dispatches through one of the engine's existing behavior
// templates.

namespace
{
    using GetSupportWeaponTypeId_t  = std::uint64_t(__fastcall*)(void* self, int equipId);
    // ThrowingImpl::GetBlastParamByEquipId at retail 0x1415D6D20 — fastcall
    // (this, ushort equipId) returning a pointer to a 6-byte BlastParameter
    // struct in the EquipParameterTables block, or 0 for unsupported types.
    using GetBlastParamByEquipId_t  = void*(__fastcall*)(void* self, std::uint64_t equipId);
    // EquipParameterTablesImpl::GetSupportWeaponParameterBlock at retail
    // 0x140A3C980 — fastcall (this, outErrorCode*, equipId, outBlock*)
    // returns the outErrorCode pointer. Populates *outBlock with derived
    // SupportWeaponParameter values (ammo capacity, ranges, grade) for
    // the given equipId, OR sets *outErrorCode = 0xFFFFFFFF on failure.
    using GetSupportWeaponParameterBlock_t = std::uint32_t*(__fastcall*)(
        void* self,
        std::uint32_t* outErrorCode,
        std::uint32_t equipId,
        float* outBlock);
    // EquipParameterTablesImpl::GetAttackIdByEquipId at retail
    // 0x140A3B5E0 — fastcall (this, equipId) returning the AttackId
    // (ushort) for a given equipId. Used by
    // GetDamageParameterByEquipIdWithLevel to resolve which
    // DamageParameter row applies on hit.
    using GetAttackIdByEquipId_t = std::uint64_t(__fastcall*)(void* self, std::uint32_t equipId);
    // DamageParameterTable::GetDamageParameter (vtable[0] of the
    // DamageParameterTable singleton, reachable via
    // QuarkSystem[0x98][0xa0][0]) — fastcall (this, attackId)
    // returning `(uint8_t*)this + 8 + attackId * 0x1A`. The function is
    // a trivial 3-instruction inline.
    using GetDamageParameter_t = std::uint8_t*(__fastcall*)(void* self, std::uint32_t attackId);

    SupportWeaponType::Deps g_Deps{};

    GetSupportWeaponTypeId_t g_OrigGetSupportWeaponTypeId = nullptr;
    bool g_GetSupportWeaponTypeIdHookInstalled = false;

    GetBlastParamByEquipId_t g_OrigGetBlastParamByEquipId = nullptr;
    bool g_GetBlastParamByEquipIdHookInstalled = false;

    GetSupportWeaponParameterBlock_t g_OrigGetSupportWeaponParameterBlock = nullptr;
    bool g_GetSupportWeaponParameterBlockHookInstalled = false;

    GetAttackIdByEquipId_t g_OrigGetAttackIdByEquipId = nullptr;
    bool g_GetAttackIdByEquipIdHookInstalled = false;

    GetDamageParameter_t g_OrigGetDamageParameter = nullptr;
    bool g_GetDamageParameterHookInstalled = false;
    void* g_DamageParameterTableSingleton = nullptr;
    void* g_DamageParameterTableHookTarget = nullptr;

    // Captured on first invocation of hkGetSupportWeaponTypeId so that Lua
    // entry-points (which don't have a `self` to pass through) can still
    // call the orig.
    std::atomic<void*> g_CapturedThis{ nullptr };
    // Captured on first invocation of hkGetSupportWeaponParameterBlock so
    // we can call the orig from contexts where we have only the equipId.
    std::atomic<void*> g_CapturedEquipParamTablesThis{ nullptr };

    std::mutex g_SupportWeaponTypeMutex;
    std::unordered_map<std::int32_t, std::int32_t> g_CustomSupportWeaponTypes;

    constexpr std::int32_t kVanillaSwpTypeFallback = 0x17;
    constexpr std::int32_t kVanillaSwpTypeMax      = 0x16;
    constexpr std::int32_t kCategoryIdBase         = 0x80;
    constexpr std::int32_t kCategoryIdMax          = 0xFFFF;

    // The 5 native throwable-action templates the engine can dispatch to.
    // ThrowingImpl::UpdateAction at mgsvtpp.exe.c:2847070 switches on the
    // swpType returned by GetSupportWeaponTypeId and calls one of these per
    // frame for each thrown item. There is no sixth — the engine does not
    // implement other throwable behaviors. A custom category therefore picks
    // ONE of these as its "physical-throw template": the framework spoofs
    // the matching vanilla swpType to native callers so the engine routes
    // the throwable through that template's update logic (animations,
    // particle effects, sound, blast trigger). Lua-level identity is
    // unaffected — `GetSupportWeaponCategory` still returns the new id.
    //
    //   "grenade"   -> swpType  1 -> UpdateActionGrenade            (frag-style)
    //   "smoke"     -> swpType  2 -> UpdateActionSmoke              (smoke / stun / flares)
    //   "molotov"   -> swpType  5 -> UpdateActionMolotovCocktail    (fire)
    //   "decoy"     -> swpType  7 -> UpdateActionDecoy              (sound decoy)
    //   "kibidango" -> swpType 0x11 -> UpdateActionKibidango        (animal lure)
    //
    // Default when the user omits `behavior` entirely: "grenade" — the most
    // generic explode-on-impact template.
    constexpr std::int32_t kBehaviorSwpTypeGrenade      = 1;
    constexpr std::int32_t kBehaviorSwpTypeSmoke        = 2;
    constexpr std::int32_t kBehaviorSwpTypeStunGrenade  = 3;
    constexpr std::int32_t kBehaviorSwpTypeSleepingGus  = 4;
    constexpr std::int32_t kBehaviorSwpTypeMolotov      = 5;
    constexpr std::int32_t kBehaviorSwpTypeDecoy        = 7;
    constexpr std::int32_t kBehaviorSwpTypeKibidango    = 0x11;
    constexpr std::int32_t kDefaultBehaviorSwpType      = kBehaviorSwpTypeGrenade;

    // Native BlastParameter struct (6 bytes) — layout reverse-engineered from
    // tpp::gm::impl::equip::`anonymous_namespace'::ReadBlastParameter at
    // mgsvtpp.exe.c:1370435 (Lua `BlastParameter` table loader) and confirmed
    // by ThrowingImpl::ActivateThrowingAtEmptyWork's reads at
    // mgsvtpp.exe.c:2843589 ((blastPtr + 0)/2 ushorts × 0.1 = velocity floats).
    //
    //   +0x00 int16  maxRange   = lua_field[3] * 10
    //   +0x02 int16  optRange   = lua_field[4] * 10
    //   +0x04 uint8  internal flag (translated from lua_field[2] 0..4):
    //                   0 -> 0x20, 1 -> 0x1C, 2 -> 0x4B, 3 -> 0x38, 4 -> 0x4D
    //                   anything else -> 0
    //   +0x05 uint8  pad
    struct BlastParameter
    {
        std::int16_t maxRangeX10  = 0;
        std::int16_t optRangeX10  = 0;
        std::uint8_t internalFlag = 0;
        std::uint8_t pad          = 0;
    };
    static_assert(sizeof(BlastParameter) == 6, "BlastParameter must be 6 bytes — matches native struct layout");

    static std::uint8_t TranslateBlastFlagFromLua(std::int32_t luaFlag)
    {
        switch (luaFlag & 0xFF)
        {
            case 0:  return 0x20;
            case 1:  return 0x1C;
            case 2:  return 0x4B;
            case 3:  return 0x38;
            case 4:  return 0x4D;
            default: return 0;
        }
    }

    // How the category resolves its BlastParameter at hook time:
    //   None   - no override; orig blast resolution (driven by behavior
    //            template's swpType) applies.
    //   Inline - user supplied { flag, maxRange, optRange }; the framework
    //            owns a synthetic 6-byte struct stored on the category.
    //   Linked - user supplied a BLA_* id (number); the framework returns
    //            a pointer into the live BlastParameter table at that
    //            row, so the values come straight from EquipParameterTables
    //            (e.g. BLA_StunGrenade, BLA_C4) and any subsequent edits
    //            via SetEquipParameters propagate automatically.
    enum class BlastMode : std::uint8_t { None, Inline, Linked };

    // Native DamageParameter struct (26 bytes, 0x1A) — layout reverse-
    // engineered from tpp::gm::impl::damage::`anonymous_namespace'::
    // ReadDamageParameter at mgsvtpp.exe.c:864226. Field names follow
    // IH/muffins' canonical DamageParameterTables.lua row layout:
    //
    //   col 1  = AttackId (key, not stored in the struct)
    //   col 2  → lethalDamageUI    (×0.1) — Displays Lethal Damage UI
    //   col 3  → unk3              (×0.1)
    //   col 4  → unk4              (×0.1)
    //   col 5  → unk5              (raw int16)
    //   col 6  → unk6              (raw int16)
    //   col 7  → unk7              (raw int16)
    //   col 8  → unk8              (raw int16)
    //   col 9  → injureType        (4-bit; e.g. TppDamage.INJ_TYPE_BULLET)
    //   col 10 → injurePart        (4-bit; e.g. TppDamage.INJ_PART_ALL)
    //   col 11 → unk11             (int8)
    //   col 12 → unk12             (×2 int8)
    //   col 13 → hitNPC            (bit  0)  Projectile-hits-NPCs flag
    //   col 14 → unk14             (bit  1)
    //   col 15 → unk15             (bit  2)  Vortex-Ring flag?
    //   col 16 → isTranq           (bit  3)  Tranquilizer (needs non-lethal value)
    //   col 17 → isStun            (bit  4)  Stun damage   (needs non-lethal value)
    //   col 18 → unk18             (bit  5)
    //   col 19 → unk19             (bit  6)
    //   col 20 → unk20             (bit  7)
    //   col 21 → isFire            (bit  8)
    //   col 22 → unk22             (bit 11)
    //   col 23 → isGas             (bit 12)
    //   col 24 → unk24             (bit 13)
    //   col 25 → unk25             (bit 14)
    //   col 26 → isElectric        (bit 10)
    //   col 27 → unk27             (bit  9)
    //   col 28 → unk28             (bit 15)
    //   col 29 → damageSource      (int8 ; e.g. TppDamage.DAM_SOURCE_Handgun)
    //   col 30 → lethalDamage      (int16) Lethal damage value
    //   col 31 → staminaDamage     (int16) Non-lethal / stamina damage value
    //   col 32 → impactForce       (int16) Impact force
    //
    // The bit layout at +0x14 is non-sequential (cols 22..25 occupy bits
    // 11..14, then col 26 → bit 10, col 27 → bit 9, col 28 → bit 15) —
    // matches the engine's exact bit-set order in ReadDamageParameter.
    struct DamageParameter
    {
        std::int16_t  lethalDamage    = 0;   // +0   (col 30)
        std::int16_t  staminaDamage   = 0;   // +2   (col 31)
        std::int16_t  impactForce     = 0;   // +4   (col 32)
        std::int16_t  lethalDamageUI  = 0;   // +6   (col 2  ×0.1)
        std::int16_t  unk3            = 0;   // +8   (col 3  ×0.1)
        std::int16_t  unk4            = 0;   // +10  (col 4  ×0.1)
        std::int16_t  unk5            = 0;   // +12  (col 5)
        std::int16_t  unk6            = 0;   // +14  (col 6)
        std::int16_t  unk7            = 0;   // +16  (col 7)
        std::int16_t  unk8            = 0;   // +18  (col 8)
        std::uint16_t bitFlags        = 0;   // +20  (cols 13..28 packed)
        std::int8_t   damageSource    = 0;   // +22  (col 29)
        std::int8_t   unk11           = 0;   // +24  (col 11)
        std::uint8_t  injureTypeAndPart = 0; // +0x17  low4=col9 injureType, high4=col10 injurePart
        std::uint8_t  unk12_pct       = 0;   // +0x19  col 12 ×2 (clamped to byte)
    };
    static_assert(sizeof(DamageParameter) == 26, "DamageParameter must be 26 bytes — matches native 0x1A struct");

    // Bit positions inside DamageParameter::bitFlags (uint16 at +0x14).
    // Order matches the cols-13..28 → bit-index mapping in
    // ReadDamageParameter at mgsvtpp.exe.c:864320..864410.
    constexpr std::uint16_t kBitFlag_hitNPC      = 1u <<  0;  // col 13
    constexpr std::uint16_t kBitFlag_unk14       = 1u <<  1;  // col 14
    constexpr std::uint16_t kBitFlag_unk15       = 1u <<  2;  // col 15
    constexpr std::uint16_t kBitFlag_isTranq     = 1u <<  3;  // col 16
    constexpr std::uint16_t kBitFlag_isStun      = 1u <<  4;  // col 17
    constexpr std::uint16_t kBitFlag_unk18       = 1u <<  5;  // col 18
    constexpr std::uint16_t kBitFlag_unk19       = 1u <<  6;  // col 19
    constexpr std::uint16_t kBitFlag_unk20       = 1u <<  7;  // col 20
    constexpr std::uint16_t kBitFlag_isFire      = 1u <<  8;  // col 21
    constexpr std::uint16_t kBitFlag_unk27       = 1u <<  9;  // col 27 (out-of-order)
    constexpr std::uint16_t kBitFlag_isElectric  = 1u << 10;  // col 26 (out-of-order)
    constexpr std::uint16_t kBitFlag_unk22       = 1u << 11;  // col 22
    constexpr std::uint16_t kBitFlag_isGas       = 1u << 12;  // col 23
    constexpr std::uint16_t kBitFlag_unk24       = 1u << 13;  // col 24
    constexpr std::uint16_t kBitFlag_unk25       = 1u << 14;  // col 25
    constexpr std::uint16_t kBitFlag_unk28       = 1u << 15;  // col 28

    enum class DamageMode : std::uint8_t { None, Inline, Linked };

    // Native SupportWeaponParameter struct (8 bytes) — layout reverse-
    // engineered from tpp::gm::impl::equip::`anonymous_namespace'::
    // ReadSupportWeaponParameter at mgsvtpp.exe.c:1371729.
    //
    // Lua row column → struct field:
    //   col 1 = swpId (key)
    //   col 2 → +4 ushort (clamped: <1 → 0x3FFF; else value)   ammo
    //   col 3 → +0 int16 (×10)                                 p1_x10
    //   col 4 → +2 int16 (×10)                                 p2_x10
    //   col 5 → +6 int8                                        grade
    //   +7   = pad
    struct SupportWeaponRow
    {
        std::int16_t  p1_x10 = 0;  // +0
        std::int16_t  p2_x10 = 0;  // +2
        std::uint16_t ammo   = 0;  // +4
        std::int8_t   grade  = 0;  // +6
        std::uint8_t  pad    = 0;  // +7
    };
    static_assert(sizeof(SupportWeaponRow) == 8, "SupportWeaponRow must be 8 bytes — matches native struct");

    enum class SwpRowMode : std::uint8_t { None, Inline /* link mode unused — orig already does it */ };

    // Synthetic AttackIds for Inline damage mode live above the vanilla
    // table's range. The vanilla DamageParameter table is sized for ids
    // < 0x1000 (well below 0x8000), so synthetic ids in 0x8000+ won't
    // collide with anything the engine itself looks up.
    constexpr std::uint32_t kSyntheticAttackIdBase = 0x8000;
    constexpr std::uint32_t kSyntheticAttackIdMax  = 0xFFFE;
    std::uint32_t g_NextSyntheticAttackId = kSyntheticAttackIdBase;

    struct RegisteredCategory
    {
        std::string     name;
        // Which native UpdateAction* template fires when an equipId bound
        // to this category is thrown. Held as a vanilla swpType (0..0x16)
        // because the framework spoofs this value to the native dispatcher.
        std::int32_t    behaviorSwpType = kDefaultBehaviorSwpType;

        // Blast: orig | inline-custom | link-to-existing-row.
        BlastMode       blastMode       = BlastMode::None;
        BlastParameter  inlineBlast{};       // valid when blastMode == Inline
        std::int32_t    linkedBlastId   = 0; // valid when blastMode == Linked

        // Damage: orig | inline-custom (synthetic AttackId + custom row) |
        // link to existing AttackId.
        DamageMode      damageMode      = DamageMode::None;
        DamageParameter inlineDamage{};                  // valid when damageMode == Inline
        std::uint32_t   syntheticAttackId  = 0;          // valid when damageMode == Inline (allocated lazily)
        std::uint32_t   linkedAttackId     = 0;          // valid when damageMode == Linked

        // SupportWeaponParameter row (ammo / p1 / p2 / grade): orig | inline.
        SwpRowMode       swpRowMode = SwpRowMode::None;
        SupportWeaponRow inlineSwpRow{};

        // Optional chaff radar/sensor disruption that fires at projectile
        // detonation. When `hasChaffEffect` is true, the framework's
        // UpdateAction* hooks call RangeAttackSystemImpl::RequestToChaff at
        // the projectile's landing position with these values. Independent
        // of `behavior` — the throwable still uses whichever native
        // template (smoke / grenade / etc) for its physical state machine.
        bool   hasChaffEffect = false;
        float  chaffRadius    = 15.0f;
        float  chaffDuration  = 20.0f;
    };

    std::mutex g_CategoryMutex;
    std::unordered_map<std::int32_t, RegisteredCategory> g_RegisteredCategories;
    std::unordered_map<std::string, std::int32_t> g_CategoryByName;
    std::int32_t g_NextCategoryId = kCategoryIdBase;

    // Reverse map: synthetic AttackId → category id. Lets
    // hkGetDamageParameter find the inline DamageParameter struct in O(1).
    std::unordered_map<std::uint32_t, std::int32_t> g_SyntheticAttackIdToCategory;

    constexpr int LUA_GLOBALSINDEX_51 = -10002;
    constexpr int LUA_TTABLE_51       = 5;
    constexpr int LUA_TNUMBER_51      = 3;
    constexpr int LUA_TSTRING_51      = 4;
}

namespace
{
    static bool ValidateBaseDeps()
    {
        return
            g_Deps.ResolveLuaApi &&
            g_Deps.LuaType &&
            g_Deps.GetLuaInt;
    }

    static bool ValidateCategoryDeps()
    {
        return
            ValidateBaseDeps() &&
            g_Deps.GetLuaTop &&
            g_Deps.LuaGetField &&
            g_Deps.LuaPop &&
            g_Deps.GetLuaString &&
            g_Deps.PushLuaNumber &&
            g_Deps.LuaPushString &&
            g_Deps.LuaCreateTable &&
            g_Deps.LuaRawSet &&
            g_Deps.LuaSetTable;
    }

    static bool EnsureLuaReady()
    {
        return ValidateBaseDeps() && g_Deps.ResolveLuaApi();
    }

    static bool EnsureLuaReadyForCategories()
    {
        return ValidateCategoryDeps() && g_Deps.ResolveLuaApi();
    }

    static bool IsLuaNumber(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TNUMBER_51;
    }

    static bool IsLuaString(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TSTRING_51;
    }

    static void SetSupportWeaponTypeInternal(std::int32_t equipId, std::int32_t swpType)
    {
        std::lock_guard<std::mutex> lock(g_SupportWeaponTypeMutex);
        g_CustomSupportWeaponTypes[equipId] = swpType;

        Log(
            "[SupportWeaponType] Set custom mapping equipId=0x%X (%d) -> swpType=0x%X (%d)\n",
            equipId,
            equipId,
            swpType,
            swpType
        );
    }

    static bool RemoveSupportWeaponTypeInternal(std::int32_t equipId)
    {
        std::lock_guard<std::mutex> lock(g_SupportWeaponTypeMutex);

        const auto it = g_CustomSupportWeaponTypes.find(equipId);
        if (it == g_CustomSupportWeaponTypes.end())
            return false;

        g_CustomSupportWeaponTypes.erase(it);

        Log(
            "[SupportWeaponType] Removed custom mapping equipId=0x%X (%d)\n",
            equipId,
            equipId
        );

        return true;
    }

    static void ClearSupportWeaponTypesInternal()
    {
        std::lock_guard<std::mutex> lock(g_SupportWeaponTypeMutex);
        g_CustomSupportWeaponTypes.clear();
        Log("[SupportWeaponType] Cleared all custom mappings\n");
    }

    static bool TryGetCustomSupportWeaponType(std::int32_t equipId, std::int32_t& outSwpType)
    {
        std::lock_guard<std::mutex> lock(g_SupportWeaponTypeMutex);

        const auto it = g_CustomSupportWeaponTypes.find(equipId);
        if (it == g_CustomSupportWeaponTypes.end())
            return false;

        outSwpType = it->second;
        return true;
    }

    // For a given resolved swpType, return the vanilla behavior swpType the
    // category was registered with, so native callers (the throw dispatcher,
    // blast offset switch, decoy/mine helpers) get a value they can dispatch
    // on. Returns false when `swpType` isn't a registered category id.
    static bool TryGetRegisteredCategoryBehaviorSwpType(std::int32_t swpType, std::int32_t& outBehavior)
    {
        std::lock_guard<std::mutex> lock(g_CategoryMutex);

        const auto it = g_RegisteredCategories.find(swpType);
        if (it == g_RegisteredCategories.end())
            return false;

        outBehavior = it->second.behaviorSwpType;
        return true;
    }

    // Look up the resolved blast info for an equipId.
    //   Inline -> returns a stable pointer to the registry-owned struct.
    //   Linked -> writes the BLA id into outLinkedId and returns nullptr;
    //             the caller computes the live table row from `self`.
    //   None   -> returns nullptr and leaves outLinkedId = -1; caller
    //             defers to orig.
    //
    // Stable-pointer note (Inline): entries are never erased mid-process
    // and re-registration only updates fields in place, so the inline
    // struct address is valid for the process lifetime once registered.
    // A torn read across an in-place re-registration is theoretically
    // possible but bounded — writers run during mod load, readers only
    // when the player throws. Worst case is one frame of stale/torn blast
    // values.
    static const BlastParameter* TryGetRegisteredCategoryBlastForEquipId(
        std::int32_t equipId,
        std::int32_t& outLinkedBlastId)
    {
        outLinkedBlastId = -1;

        std::int32_t customSwpType = 0;
        if (!TryGetCustomSupportWeaponType(equipId, customSwpType))
            return nullptr;

        std::lock_guard<std::mutex> lock(g_CategoryMutex);
        const auto it = g_RegisteredCategories.find(customSwpType);
        if (it == g_RegisteredCategories.end())
            return nullptr;

        switch (it->second.blastMode)
        {
            case BlastMode::Inline:
                return &it->second.inlineBlast;
            case BlastMode::Linked:
                outLinkedBlastId = it->second.linkedBlastId;
                return nullptr;
            case BlastMode::None:
            default:
                return nullptr;
        }
    }

    static void* __fastcall hkGetBlastParamByEquipId(void* self, std::uint64_t equipId)
    {
        const std::int32_t equipId32 = static_cast<std::int32_t>(equipId & 0xFFFFFFFFu);

        std::int32_t linkedBlastId = -1;
        if (const BlastParameter* inlineBlast =
                TryGetRegisteredCategoryBlastForEquipId(equipId32, linkedBlastId))
        {
            // Inline mode — return the framework-owned synthetic struct.
            // The orig returns a non-const pointer (callers only read), so
            // strip const.
            return const_cast<BlastParameter*>(inlineBlast);
        }

        if (linkedBlastId >= 0 && self)
        {
            // Linked mode — compute the live BlastParameter table row from
            // the throwing impl's parameter-tables sub-object. The orig
            // function does the same at retail 0x1415D6D26..0x1415D6D70:
            //
            //   tableBase = *(*(self + 0x178)) + 0x98)   ; ParameterTables.blastTable
            //   row       = tableBase + offset           ; offset = bla_id * 6
            //
            // We replicate that addressing exactly — the offsets the orig
            // hardcodes (0x48 for StunGrenade, 0x54 for the next, etc.) are
            // all multiples of 6, matching the 6-byte stride the table
            // loader uses (mgsvtpp.exe.c:1370490).
            constexpr std::ptrdiff_t kOffsetSubObj   = 0x178;
            constexpr std::ptrdiff_t kOffsetBlastTbl = 0x98;
            constexpr std::ptrdiff_t kBlastEntryStride = 6;

            std::uint8_t* subObj =
                *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(self) + kOffsetSubObj);
            if (subObj)
            {
                std::uint8_t* tableBase =
                    *reinterpret_cast<std::uint8_t**>(subObj + kOffsetBlastTbl);
                if (tableBase)
                {
                    return tableBase + static_cast<std::ptrdiff_t>(linkedBlastId) * kBlastEntryStride;
                }
            }
            // Subobject or table base missing — fall through to orig.
        }

        if (g_OrigGetBlastParamByEquipId)
            return g_OrigGetBlastParamByEquipId(self, equipId);

        return nullptr;
    }

    // Forward declaration so hkGetSupportWeaponParameterBlock can invoke our
    // own swpType resolver (which sees registered categories and returns
    // their spoofed behavior swpType).
    static std::uint64_t __fastcall hkGetSupportWeaponTypeId(void* self, int equipId);

    // ---- Damage / SupportWeaponParameter / AttackId lookup helpers ----

    // Look up the registered category's damage info for an equipId.
    //   Returns mode + payload, or DamageMode::None if the equipId isn't
    //   bound to a registered category or its category has no damage
    //   override. Locks g_CategoryMutex internally.
    struct DamageLookupResult
    {
        DamageMode    mode    = DamageMode::None;
        std::uint32_t synthAtkId = 0; // valid when mode == Inline
        std::uint32_t linkedAtkId  = 0; // valid when mode == Linked
    };

    static DamageLookupResult LookupDamageForEquipId(std::int32_t equipId)
    {
        DamageLookupResult result;

        std::int32_t customSwpType = 0;
        if (!TryGetCustomSupportWeaponType(equipId, customSwpType))
            return result;

        std::lock_guard<std::mutex> lock(g_CategoryMutex);
        const auto it = g_RegisteredCategories.find(customSwpType);
        if (it == g_RegisteredCategories.end())
            return result;

        result.mode = it->second.damageMode;
        switch (it->second.damageMode)
        {
            case DamageMode::Inline:
                result.synthAtkId = it->second.syntheticAttackId;
                break;
            case DamageMode::Linked:
                result.linkedAtkId = it->second.linkedAttackId;
                break;
            case DamageMode::None:
            default:
                break;
        }
        return result;
    }

    // Look up the inline DamageParameter struct keyed by the synthetic
    // AttackId we stamped on a category. The pointer is owned by the
    // category map, stable for the process lifetime.
    static const DamageParameter* TryGetInlineDamageBySyntheticAttackId(std::uint32_t synthAtkId)
    {
        std::lock_guard<std::mutex> lock(g_CategoryMutex);
        const auto idIt = g_SyntheticAttackIdToCategory.find(synthAtkId);
        if (idIt == g_SyntheticAttackIdToCategory.end())
            return nullptr;

        const auto catIt = g_RegisteredCategories.find(idIt->second);
        if (catIt == g_RegisteredCategories.end())
            return nullptr;
        if (catIt->second.damageMode != DamageMode::Inline)
            return nullptr;

        return &catIt->second.inlineDamage;
    }

    // Look up the inline SupportWeaponRow for an equipId. Returns nullptr
    // when the equipId isn't bound to a registered category, or its
    // category uses orig SWP-row resolution.
    static const SupportWeaponRow* TryGetInlineSwpRowForEquipId(std::int32_t equipId)
    {
        std::int32_t customSwpType = 0;
        if (!TryGetCustomSupportWeaponType(equipId, customSwpType))
            return nullptr;

        std::lock_guard<std::mutex> lock(g_CategoryMutex);
        const auto it = g_RegisteredCategories.find(customSwpType);
        if (it == g_RegisteredCategories.end())
            return nullptr;
        if (it->second.swpRowMode != SwpRowMode::Inline)
            return nullptr;

        return &it->second.inlineSwpRow;
    }

    // ---- New hooks ----

    // EquipParameterTablesImpl::GetAttackIdByEquipId at retail 0x140A3B5E0.
    // Vanilla impl is a giant equipId-range switch returning the AttackId
    // for a given equipId. Used by the damage-resolution pipeline
    // (GetDamageParameterByEquipIdWithLevel) to pick which row of the
    // DamageParameter table applies to a hit.
    //
    // For registered equipIds:
    //   damageMode == Linked -> return user's chosen AttackId (existing row)
    //   damageMode == Inline -> return our synthetic AttackId; the
    //                           hkGetDamageParameter hook below intercepts
    //                           the row lookup for that id.
    //   damageMode == None   -> defer to orig.
    static std::uint64_t __fastcall hkGetAttackIdByEquipId(void* self, std::uint32_t equipId)
    {
        if (self && !g_CapturedEquipParamTablesThis.load(std::memory_order_relaxed))
            g_CapturedEquipParamTablesThis.store(self, std::memory_order_relaxed);

        const DamageLookupResult d = LookupDamageForEquipId(static_cast<std::int32_t>(equipId));
        switch (d.mode)
        {
            case DamageMode::Linked:
                return static_cast<std::uint64_t>(d.linkedAtkId);
            case DamageMode::Inline:
                return static_cast<std::uint64_t>(d.synthAtkId);
            case DamageMode::None:
            default:
                break;
        }

        if (g_OrigGetAttackIdByEquipId)
            return g_OrigGetAttackIdByEquipId(self, equipId);
        return 0;
    }

    // DamageParameterTable::GetDamageParameter (vtable[0]) — the trivial
    // 3-instruction `return this + 8 + atkId * 0x1A` lookup. We hook it
    // ONLY when at least one inline-damage category exists, so vanilla
    // damage resolution (which goes through this function for every hit
    // event) is unaffected when no mod is using inline damage.
    //
    // For synthetic AttackIds in the 0x8000+ range, we return the
    // framework-owned DamageParameter struct from the category that
    // allocated that id.
    static std::uint8_t* __fastcall hkGetDamageParameter(void* self, std::uint32_t attackId)
    {
        if (attackId >= kSyntheticAttackIdBase)
        {
            if (const DamageParameter* dp = TryGetInlineDamageBySyntheticAttackId(attackId))
                return reinterpret_cast<std::uint8_t*>(const_cast<DamageParameter*>(dp));
            // Synthetic id but not registered (shouldn't happen) — fall
            // through to orig and let it return whatever it would for the
            // raw arithmetic.
        }

        if (g_OrigGetDamageParameter)
            return g_OrigGetDamageParameter(self, attackId);

        // Last-ditch fallback: replicate the trivial inline so callers
        // don't get a null pointer if the orig was never captured.
        return reinterpret_cast<std::uint8_t*>(self) + 8 +
               static_cast<std::ptrdiff_t>(attackId) * 0x1A;
    }

    // EquipParameterTablesImpl::GetSupportWeaponParameterBlock at retail
    // 0x140A3C980. Native impl looks up two SupportWeaponRow entries (the
    // current equipId and a "grade-up" equipId via vtable[0x30]) and
    // populates a polymorphic output block whose layout depends on the
    // resolved swpType. For our registered categories with inline SWP
    // rows, we replicate the same write pattern so dependent UI / HUD /
    // ammo-tracking code sees the values we promised.
    //
    // The output `outBlock` layout (driven by the spoofed behavior swpType):
    //
    //   case 2, 4, 0xd, 0x11 (smoke / stun / flares / kibidango):
    //     outBlock+0x00 float = (untouched in this case)
    //     outBlock+0x04 ushort = ammo
    //     outBlock+0x08 float  = p1 / 10.0
    //     outBlock+0x0C float  = upgrade_p1 / 10.0  (we use the same row)
    //     outBlock+0x12 ushort = upgrade_ammo      (= ammo for us)
    //
    //   case 7, 8, 9 (decoys):
    //     outBlock+0x00 float  = p1 / 10.0
    //     outBlock+0x04 float  = upgrade_p1 / 10.0  (collides with ammo)
    //     outBlock+0x08, +0x0C: untouched in this case
    //     outBlock+0x12 ushort = upgrade_ammo
    //
    //   default (grenade / molotov / etc.): only the unconditional
    //     ammo/upgrade_ammo writes happen, no float fields are touched.
    //
    // For categories without an inline SWP row, we defer to the orig.
    static std::uint32_t* __fastcall hkGetSupportWeaponParameterBlock(
        void* self,
        std::uint32_t* outErrorCode,
        std::uint32_t equipId,
        float* outBlock)
    {
        if (self && !g_CapturedEquipParamTablesThis.load(std::memory_order_relaxed))
            g_CapturedEquipParamTablesThis.store(self, std::memory_order_relaxed);

        const SupportWeaponRow* row =
            TryGetInlineSwpRowForEquipId(static_cast<std::int32_t>(equipId));

        {
            static std::atomic<bool> s_logged{false};
            if (!s_logged.exchange(true))
            {
                Log("[SupportWeaponType] hkGetSupportWeaponParameterBlock first-fire equipId=0x%X row=%p\n",
                    static_cast<unsigned int>(equipId),
                    static_cast<const void*>(row));
            }
        }

        if (!row)
        {
            if (g_OrigGetSupportWeaponParameterBlock)
                return g_OrigGetSupportWeaponParameterBlock(self, outErrorCode, equipId, outBlock);
            if (outErrorCode) *outErrorCode = 0xFFFFFFFFu;
            return outErrorCode;
        }

        Log("[SupportWeaponType] hkGetSupportWeaponParameterBlock equipId=0x%X -> ammo=%u p1=%d p2=%d\n",
            static_cast<unsigned int>(equipId),
            static_cast<unsigned int>(row->ammo),
            static_cast<int>(row->p1_x10) / 10,
            static_cast<int>(row->p2_x10) / 10);

        // ---- Write our row into the LIVE SupportWeaponParameter table ----
        // Many engine paths (e.g. PickUpActionPluginImpl::DoPickUpWithPickableInfo
        // at mgsvtpp.exe.c:2325266) read the table directly via
        //   *(EquipParameterTablesImpl + 0xa0) + GetRootParameterId2(equipId) * 8
        // bypassing this function. Our equipId's swpId (registered via
        // AddToEquipIdTable column 3 / DeclareSWPs) is past the vanilla
        // table's populated range, so the direct read returns zero/garbage.
        // Mirror our values into the live table so downstream readers find
        // them. swpId comes from the engine's per-equipId packed table at
        // (EquipIdTableImpl + 8 + equipId*2), upper 10 bits — exactly what
        // GetRootParameterId2 returns.
        if (self)
        {
            void* eqIdTableSingleton = g_CapturedThis.load(std::memory_order_relaxed);
            if (eqIdTableSingleton)
            {
                const std::uint16_t* packedTable =
                    reinterpret_cast<const std::uint16_t*>(
                        reinterpret_cast<std::uint8_t*>(eqIdTableSingleton) + 8);
                const std::uint16_t packed = packedTable[equipId];
                const std::uint32_t swpId  = static_cast<std::uint32_t>(packed) >> 6;

                std::uint8_t* tableBase =
                    *reinterpret_cast<std::uint8_t**>(
                        reinterpret_cast<std::uint8_t*>(self) + 0xa0);
                if (tableBase && swpId > 0 && swpId < 0x100)
                {
                    std::uint8_t* rowAddr = tableBase + static_cast<std::ptrdiff_t>(swpId) * 8;
                    *reinterpret_cast<std::int16_t*>(rowAddr + 0) = row->p1_x10;
                    *reinterpret_cast<std::int16_t*>(rowAddr + 2) = row->p2_x10;
                    *reinterpret_cast<std::uint16_t*>(rowAddr + 4) = row->ammo;
                    *reinterpret_cast<std::int8_t*>(rowAddr + 6)  = row->grade;
                    *reinterpret_cast<std::uint8_t*>(rowAddr + 7) = 0;

                    static std::atomic<bool> s_writeLogged{false};
                    if (!s_writeLogged.exchange(true))
                    {
                        Log("[SupportWeaponType]   wrote inline swpRow into live table at swpId=0x%X (base=%p)\n",
                            swpId,
                            static_cast<void*>(tableBase));
                    }
                }
            }
        }

        if (!outBlock || !outErrorCode)
        {
            // Defensive: unexpected null buffer. Mark error and bail.
            if (outErrorCode) *outErrorCode = 0xFFFFFFFFu;
            return outErrorCode;
        }

        // Always populate ammo / upgrade-ammo (vanilla writes these
        // unconditionally before the swpType switch).
        std::uint8_t* ammoOut    = reinterpret_cast<std::uint8_t*>(outBlock) + 0x04;
        std::uint8_t* upgAmmoOut = reinterpret_cast<std::uint8_t*>(outBlock) + 0x12;
        *reinterpret_cast<std::uint16_t*>(ammoOut)    = row->ammo;
        *reinterpret_cast<std::uint16_t*>(upgAmmoOut) = row->ammo; // single-row: upgrade equals current

        // Determine which native behavior swpType applies (so the float
        // writes match what callers expect). We look up the spoofed
        // behavior via the same path the orig would (vtable[0x48] = our
        // GetSupportWeaponTypeId hook). Since we're already inside the
        // EquipParameterTablesImpl, we can call our own hook indirectly
        // — but the simplest is to call the captured EquipIdTableImpl
        // singleton via g_CapturedThis.
        std::int32_t behaviorSwpType = kDefaultBehaviorSwpType;
        {
            void* eqIdTable = g_CapturedThis.load(std::memory_order_relaxed);
            if (eqIdTable && g_OrigGetSupportWeaponTypeId)
            {
                // Run our own hook (not orig) — for registered categories
                // this returns the spoofed behavior swpType, exactly what
                // the orig would have computed via the same vtable call.
                behaviorSwpType = static_cast<std::int32_t>(
                    hkGetSupportWeaponTypeId(eqIdTable, static_cast<int>(equipId)));
            }
        }

        const float p1f = static_cast<float>(row->p1_x10) * 0.1f;

        switch (behaviorSwpType)
        {
            case 2: case 4: case 0xd: case 0x11:
                outBlock[2] = p1f; // +0x08
                outBlock[3] = p1f; // +0x0C  (upgrade — same)
                break;
            case 7: case 8: case 9:
                outBlock[0] = p1f; // +0x00
                outBlock[1] = p1f; // +0x04 (collides with ammo write — vanilla
                                   //        does the same; decoys don't have ammo
                                   //        in the conventional sense here)
                break;
            default:
                // No additional float writes — ammo/upgrade-ammo above
                // are the only fields the orig would touch for grenade /
                // molotov / etc.
                break;
        }

        *outErrorCode = 0;
        return outErrorCode;
    }

    static std::uint64_t __fastcall hkGetSupportWeaponTypeId(void* self, int equipId)
    {
        if (self && !g_CapturedThis.load(std::memory_order_relaxed))
            g_CapturedThis.store(self, std::memory_order_relaxed);

        std::int32_t customSwpType = 0;
        if (TryGetCustomSupportWeaponType(static_cast<std::int32_t>(equipId), customSwpType))
        {
            // Registered category? Native consumers (the throw dispatcher at
            // ThrowingImpl::UpdateAction, the blast offset switch, decoy /
            // mine / C4 helpers) all switch on a hardcoded 0..0x16 enum —
            // anything else falls through to default, which means the
            // throwable does nothing. So we spoof the category's native
            // behavior swpType (one of the 5 throwable templates the engine
            // implements: Grenade, Smoke, Molotov, Decoy, Kibidango). The
            // category's true id is exposed to Lua via
            // GetSupportWeaponCategory, which sees through the spoof.
            std::int32_t behaviorSwpType = 0;
            if (TryGetRegisteredCategoryBehaviorSwpType(customSwpType, behaviorSwpType))
                return static_cast<std::uint64_t>(static_cast<std::uint32_t>(behaviorSwpType));

            return static_cast<std::uint64_t>(static_cast<std::uint32_t>(customSwpType));
        }

        if (g_OrigGetSupportWeaponTypeId)
            return g_OrigGetSupportWeaponTypeId(self, equipId);

        return kVanillaSwpTypeFallback;
    }

    static std::int32_t AllocateCategoryIdLocked(const std::string& name)
    {
        const auto existing = g_CategoryByName.find(name);
        if (existing != g_CategoryByName.end())
            return existing->second;

        if (g_NextCategoryId > kCategoryIdMax)
            return -1;

        return g_NextCategoryId++;
    }

    // EnsureTppEquipTable — returns a stack with TppEquip on top (or false if
    // it had to give up). Mirrors the helper used in DeclareSWPs.cpp /
    // RegisterConstantEquipId.cpp so behavior across the framework matches.
    static bool EnsureTppEquipTable(lua_State* L)
    {
        g_Deps.LuaGetField(L, LUA_GLOBALSINDEX_51, "TppEquip");
        if (g_Deps.LuaType(L, -1) == LUA_TTABLE_51)
            return true;

        g_Deps.LuaPop(L, 1);

        g_Deps.LuaPushString(L, "TppEquip");
        g_Deps.LuaCreateTable(L, 0, 0);
        g_Deps.LuaRawSet(L, LUA_GLOBALSINDEX_51);

        g_Deps.LuaGetField(L, LUA_GLOBALSINDEX_51, "TppEquip");
        return g_Deps.LuaType(L, -1) == LUA_TTABLE_51;
    }

    static void WriteSwpTypeConstantToLua(lua_State* L, const std::string& constName, std::int32_t value)
    {
        if (!EnsureTppEquipTable(L))
            return;

        g_Deps.LuaPushString(L, constName.c_str());
        g_Deps.PushLuaNumber(L, static_cast<float>(value));
        g_Deps.LuaSetTable(L, -3);

        g_Deps.LuaPop(L, 1);
    }

    // Reads a numeric subfield from the table at the top of the Lua stack and
    // pops the value the LuaGetField pushed. Returns true when a number was
    // found; on miss / wrong type, leaves the stack balanced and returns false.
    static bool TryReadIntField(lua_State* L, int tableIdx, const char* fieldName, std::int32_t& out)
    {
        g_Deps.LuaGetField(L, tableIdx, fieldName);
        const bool ok = (g_Deps.LuaType(L, -1) == LUA_TNUMBER_51);
        if (ok)
            out = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, -1));
        g_Deps.LuaPop(L, 1);
        return ok;
    }

    // Read result for the optional `blast` spec field. The user can pass:
    //   - nil / absent          -> mode=None, no override.
    //   - a TABLE  { flag, maxRange, optRange }
    //                           -> mode=Inline, populates outInline.
    //   - a NUMBER (BLA_* id)   -> mode=Linked, populates outLinkedId.
    //                              The framework returns the live table
    //                              row at hook time, so values track
    //                              EquipParameterTables.BlastParameter
    //                              edits made via SetEquipParameters.
    enum class BlastSpecMode : std::uint8_t { None, Inline, Linked };
    struct BlastSpecResult
    {
        BlastSpecMode  mode         = BlastSpecMode::None;
        BlastParameter inlineBlast{};
        std::int32_t   linkedBlastId = 0;
    };

    // Reads optional `blast` field from the spec table at `specIdx`.
    // Returns the parsed result. Stack is balanced on exit.
    //
    // INLINE TABLE FORM:
    //   { flag = N, maxRange = N, optRange = N }
    //
    //   `flag`   uses the same 0..4 convention as vanilla
    //            EquipParameterTables.lua BlastParameter row[2]:
    //              0 = ground burst
    //              1 = smoke
    //              2 = stun-style
    //              3 = fire / molotov
    //              4 = flash-bang
    //            Translated to the native byte via TranslateBlastFlagFromLua.
    //   `maxRange` and `optRange` are floats (meters) — stored as
    //            × 10 shorts to match the loader at mgsvtpp.exe.c:1370490.
    //   Aliases: `blastFlag` for `flag`, `max` for `maxRange`, `opt` for
    //            `optRange`.
    //
    // LINKED FORM:
    //   blast = TppEquip.BLA_StunGrenade  (any BLA_* number)
    //
    //   The framework returns a pointer to the live table row at hook
    //   time. Values reflect whatever EquipParameterTables.lua set during
    //   the most recent SetEquipParameters reload, including any
    //   modifications made via that API.
    static BlastSpecResult ReadBlastField(lua_State* L, int specIdx)
    {
        BlastSpecResult result;

        g_Deps.LuaGetField(L, specIdx, "blast");
        const int t = g_Deps.LuaType(L, -1);

        if (t == LUA_TNUMBER_51)
        {
            result.mode          = BlastSpecMode::Linked;
            result.linkedBlastId = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, -1));
            g_Deps.LuaPop(L, 1);
            return result;
        }

        if (t != LUA_TTABLE_51)
        {
            g_Deps.LuaPop(L, 1);
            return result; // mode = None
        }

        // Inline form — read sub-fields from the blast subtable on top.
        const int blastTableIdx = -1;
        std::int32_t flag = 0, maxRange = 0, optRange = 0;

        if (!TryReadIntField(L, blastTableIdx, "flag", flag))
            (void)TryReadIntField(L, blastTableIdx, "blastFlag", flag);

        if (!TryReadIntField(L, blastTableIdx, "maxRange", maxRange))
            (void)TryReadIntField(L, blastTableIdx, "max", maxRange);

        if (!TryReadIntField(L, blastTableIdx, "optRange", optRange))
            (void)TryReadIntField(L, blastTableIdx, "opt", optRange);

        result.mode = BlastSpecMode::Inline;
        result.inlineBlast.maxRangeX10  = static_cast<std::int16_t>(maxRange * 10);
        result.inlineBlast.optRangeX10  = static_cast<std::int16_t>(optRange * 10);
        result.inlineBlast.internalFlag = TranslateBlastFlagFromLua(flag);
        result.inlineBlast.pad          = 0;

        g_Deps.LuaPop(L, 1);
        return result;
    }

    // ---- Damage spec parser ----

    // Damage field accepts:
    //   nil    -> mode=None  (defer to orig damage resolution)
    //   number -> mode=Linked, AttackId = the number; the framework hooks
    //             GetAttackIdByEquipId so registered equipIds resolve to
    //             this AttackId, and the orig DamageParameterTable lookup
    //             picks up the right vanilla row.
    //   table  -> mode=Inline, parse subfields into a DamageParameter struct.
    //             The framework allocates a synthetic AttackId in 0x8000+
    //             and stamps it on the category; the
    //             DamageParameterTable::GetDamageParameter hook returns
    //             this struct for that synthetic id.
    //
    // INLINE forms (table). All fields are optional, missing → 0.
    //
    //   NAMED fields — match IH/muffins canonical DamageParameterTables.lua:
    //     lethalDamage      number     col 30 — primary lethal damage
    //     staminaDamage     number     col 31 — non-lethal / stamina damage
    //     impactForce       number     col 32 — impact force
    //     lethalDamageUI    number     col 2 (×0.1)  — UI-displayed damage
    //     unk3, unk4        number     cols 3, 4 (×0.1)
    //     unk5..unk8        number     cols 5..8 (raw int16)
    //     unk11             number     col 11 (raw int8)
    //     unk12             number     col 12 (×2 internally, capped at 255)
    //     injureType        number     col 9  (4-bit; e.g. TppDamage.INJ_TYPE_BULLET)
    //     injurePart        number     col 10 (4-bit; e.g. TppDamage.INJ_PART_ALL)
    //     damageSource      number     col 29 (e.g. TppDamage.DAM_SOURCE_Handgun)
    //     hitNPC            bool/number col 13 (bit  0)
    //     isTranq           bool/number col 16 (bit  3)
    //     isStun            bool/number col 17 (bit  4)
    //     isFire            bool/number col 21 (bit  8)
    //     isGas             bool/number col 23 (bit 12)
    //     isElectric        bool/number col 26 (bit 10)
    //     unk14, unk15, unk18, unk19, unk20, unk22, unk24, unk25, unk27, unk28
    //                       bool/number — corresponding bits per
    //                       ReadDamageParameter mapping
    //
    //   POSITIONAL row[] form — paste a vanilla DamageParameterTables.lua
    //     row literally (minus column 1 = AttackId, which is auto-allocated).
    //     31 entries covering cols 2..32:
    //       row = {
    //         lethalDamageUI, unk3, unk4, unk5, unk6, unk7, unk8,
    //         injureType, injurePart, unk11, unk12,
    //         hitNPC, unk14, unk15, isTranq, isStun, unk18, unk19, unk20, isFire,
    //         unk22, isGas, unk24, unk25, isElectric, unk27, unk28,
    //         damageSource, lethalDamage, staminaDamage, impactForce,
    //       }
    //     Named fields, if also present, override the row[] values.
    enum class DamageSpecMode : std::uint8_t { None, Inline, Linked };
    struct DamageSpecResult
    {
        DamageSpecMode  mode           = DamageSpecMode::None;
        DamageParameter inlineDamage{};
        std::uint32_t   linkedAttackId = 0;
    };

    // Read row[i] from a Lua table-on-stack as an int, returning false on
    // miss / non-number / missing deps. `i` is 1-based (Lua convention).
    static bool TryReadRowInt(lua_State* L, int tableIdx, int i, std::int32_t& out)
    {
        if (!g_Deps.LuaRawGetI || !g_Deps.LuaType || !g_Deps.GetLuaInt || !g_Deps.LuaPop)
            return false;

        g_Deps.LuaRawGetI(L, tableIdx, i);
        const bool ok = (g_Deps.LuaType(L, -1) == LUA_TNUMBER_51);
        if (ok)
            out = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, -1));
        g_Deps.LuaPop(L, 1);
        return ok;
    }

    // Read a named-or-bool field as an int (0/1). Booleans aren't a Lua-51
    // type we test for separately — treat any non-nil non-zero as 1.
    static bool TryReadBitField(lua_State* L, int tableIdx, const char* name, std::int32_t& outBit)
    {
        std::int32_t v = 0;
        if (!TryReadIntField(L, tableIdx, name, v))
            return false;
        outBit = (v != 0) ? 1 : 0;
        return true;
    }

    static DamageSpecResult ReadDamageField(lua_State* L, int specIdx)
    {
        DamageSpecResult result;

        g_Deps.LuaGetField(L, specIdx, "damage");
        const int t = g_Deps.LuaType(L, -1);

        if (t == LUA_TNUMBER_51)
        {
            result.mode           = DamageSpecMode::Linked;
            result.linkedAttackId = static_cast<std::uint32_t>(g_Deps.GetLuaInt(L, -1));
            g_Deps.LuaPop(L, 1);
            return result;
        }

        if (t != LUA_TTABLE_51)
        {
            g_Deps.LuaPop(L, 1);
            return result;
        }

        const int dmgIdx = -1;

        // Per-column locals (cols 2..32). Initialize all zero so missing
        // keys cleanly produce 0 fields.
        std::int32_t lethalDamageUI = 0;
        std::int32_t unk3 = 0, unk4 = 0;
        std::int32_t unk5 = 0, unk6 = 0, unk7 = 0, unk8 = 0;
        std::int32_t injureType = 0, injurePart = 0;
        std::int32_t unk11 = 0, unk12 = 0;
        std::int32_t bit_hitNPC = 0, bit_unk14 = 0, bit_unk15 = 0;
        std::int32_t bit_isTranq = 0, bit_isStun = 0;
        std::int32_t bit_unk18 = 0, bit_unk19 = 0, bit_unk20 = 0;
        std::int32_t bit_isFire = 0;
        std::int32_t bit_unk22 = 0, bit_isGas = 0, bit_unk24 = 0, bit_unk25 = 0;
        std::int32_t bit_isElectric = 0, bit_unk27 = 0, bit_unk28 = 0;
        std::int32_t damageSource = 0;
        std::int32_t lethalDamage = 0, staminaDamage = 0, impactForce = 0;

        // ---- Positional row[] form ----
        // Fill from the row[] array first, then named keys can override.
        g_Deps.LuaGetField(L, dmgIdx, "row");
        const bool hasRow = (g_Deps.LuaType(L, -1) == LUA_TTABLE_51);
        if (hasRow)
        {
            const int rowIdx = -1;
            // Map cols 2..32 to row[1]..row[31] (the user passes 31 values
            // in the order shown in the doc comment above).
            (void)TryReadRowInt(L, rowIdx,  1, lethalDamageUI);
            (void)TryReadRowInt(L, rowIdx,  2, unk3);
            (void)TryReadRowInt(L, rowIdx,  3, unk4);
            (void)TryReadRowInt(L, rowIdx,  4, unk5);
            (void)TryReadRowInt(L, rowIdx,  5, unk6);
            (void)TryReadRowInt(L, rowIdx,  6, unk7);
            (void)TryReadRowInt(L, rowIdx,  7, unk8);
            (void)TryReadRowInt(L, rowIdx,  8, injureType);
            (void)TryReadRowInt(L, rowIdx,  9, injurePart);
            (void)TryReadRowInt(L, rowIdx, 10, unk11);
            (void)TryReadRowInt(L, rowIdx, 11, unk12);
            (void)TryReadRowInt(L, rowIdx, 12, bit_hitNPC);
            (void)TryReadRowInt(L, rowIdx, 13, bit_unk14);
            (void)TryReadRowInt(L, rowIdx, 14, bit_unk15);
            (void)TryReadRowInt(L, rowIdx, 15, bit_isTranq);
            (void)TryReadRowInt(L, rowIdx, 16, bit_isStun);
            (void)TryReadRowInt(L, rowIdx, 17, bit_unk18);
            (void)TryReadRowInt(L, rowIdx, 18, bit_unk19);
            (void)TryReadRowInt(L, rowIdx, 19, bit_unk20);
            (void)TryReadRowInt(L, rowIdx, 20, bit_isFire);
            (void)TryReadRowInt(L, rowIdx, 21, bit_unk22);
            (void)TryReadRowInt(L, rowIdx, 22, bit_isGas);
            (void)TryReadRowInt(L, rowIdx, 23, bit_unk24);
            (void)TryReadRowInt(L, rowIdx, 24, bit_unk25);
            (void)TryReadRowInt(L, rowIdx, 25, bit_isElectric);
            (void)TryReadRowInt(L, rowIdx, 26, bit_unk27);
            (void)TryReadRowInt(L, rowIdx, 27, bit_unk28);
            (void)TryReadRowInt(L, rowIdx, 28, damageSource);
            (void)TryReadRowInt(L, rowIdx, 29, lethalDamage);
            (void)TryReadRowInt(L, rowIdx, 30, staminaDamage);
            (void)TryReadRowInt(L, rowIdx, 31, impactForce);
        }
        g_Deps.LuaPop(L, 1); // pop row table (or nil/non-table)

        // ---- Named fields (override any row[] values) ----
        TryReadIntField(L, dmgIdx, "lethalDamageUI", lethalDamageUI);
        TryReadIntField(L, dmgIdx, "unk3",  unk3);
        TryReadIntField(L, dmgIdx, "unk4",  unk4);
        TryReadIntField(L, dmgIdx, "unk5",  unk5);
        TryReadIntField(L, dmgIdx, "unk6",  unk6);
        TryReadIntField(L, dmgIdx, "unk7",  unk7);
        TryReadIntField(L, dmgIdx, "unk8",  unk8);
        TryReadIntField(L, dmgIdx, "injureType", injureType);
        TryReadIntField(L, dmgIdx, "injurePart", injurePart);
        TryReadIntField(L, dmgIdx, "unk11", unk11);
        TryReadIntField(L, dmgIdx, "unk12", unk12);
        TryReadBitField(L, dmgIdx, "hitNPC",     bit_hitNPC);
        TryReadBitField(L, dmgIdx, "unk14",      bit_unk14);
        TryReadBitField(L, dmgIdx, "unk15",      bit_unk15);
        TryReadBitField(L, dmgIdx, "isTranq",    bit_isTranq);
        TryReadBitField(L, dmgIdx, "isStun",     bit_isStun);
        TryReadBitField(L, dmgIdx, "unk18",      bit_unk18);
        TryReadBitField(L, dmgIdx, "unk19",      bit_unk19);
        TryReadBitField(L, dmgIdx, "unk20",      bit_unk20);
        TryReadBitField(L, dmgIdx, "isFire",     bit_isFire);
        TryReadBitField(L, dmgIdx, "unk22",      bit_unk22);
        TryReadBitField(L, dmgIdx, "isGas",      bit_isGas);
        TryReadBitField(L, dmgIdx, "unk24",      bit_unk24);
        TryReadBitField(L, dmgIdx, "unk25",      bit_unk25);
        TryReadBitField(L, dmgIdx, "isElectric", bit_isElectric);
        TryReadBitField(L, dmgIdx, "unk27",      bit_unk27);
        TryReadBitField(L, dmgIdx, "unk28",      bit_unk28);
        TryReadIntField(L, dmgIdx, "damageSource",  damageSource);
        TryReadIntField(L, dmgIdx, "lethalDamage",  lethalDamage);
        TryReadIntField(L, dmgIdx, "staminaDamage", staminaDamage);
        TryReadIntField(L, dmgIdx, "impactForce",   impactForce);

        // ---- Pack into the native struct ----
        result.mode = DamageSpecMode::Inline;
        DamageParameter& d = result.inlineDamage;

        d.lethalDamage   = static_cast<std::int16_t>(lethalDamage);
        d.staminaDamage  = static_cast<std::int16_t>(staminaDamage);
        d.impactForce    = static_cast<std::int16_t>(impactForce);
        // Cols 2..4 are stored × 0.1 (i.e. value/10) per the vanilla
        // ReadDamageParameter loader at mgsvtpp.exe.c:864250..864262. Pass
        // the same raw number you'd write in EquipParameterTables.lua and
        // we'll apply the same scaling.
        d.lethalDamageUI = static_cast<std::int16_t>(static_cast<float>(lethalDamageUI) * 0.1f);
        d.unk3           = static_cast<std::int16_t>(static_cast<float>(unk3) * 0.1f);
        d.unk4           = static_cast<std::int16_t>(static_cast<float>(unk4) * 0.1f);
        d.unk5           = static_cast<std::int16_t>(unk5);
        d.unk6           = static_cast<std::int16_t>(unk6);
        d.unk7           = static_cast<std::int16_t>(unk7);
        d.unk8           = static_cast<std::int16_t>(unk8);
        d.damageSource   = static_cast<std::int8_t>(damageSource);
        d.unk11          = static_cast<std::int8_t>(unk11);
        d.injureTypeAndPart = static_cast<std::uint8_t>(
            ((injurePart & 0xF) << 4) | (injureType & 0xF));
        // unk12 is stored as ×2 byte (per ReadDamageParameter:
        // *(byte+0x19) = lvar3 * 0.01 * 200 = lvar3 * 2). Clamp to byte.
        const std::int32_t pctClamped = (std::min)(255, (std::max)(0, unk12 * 2));
        d.unk12_pct      = static_cast<std::uint8_t>(pctClamped);

        // Pack bits per the cols-13..28 → bit-index mapping.
        std::uint16_t bits = 0;
        if (bit_hitNPC)     bits |= kBitFlag_hitNPC;
        if (bit_unk14)      bits |= kBitFlag_unk14;
        if (bit_unk15)      bits |= kBitFlag_unk15;
        if (bit_isTranq)    bits |= kBitFlag_isTranq;
        if (bit_isStun)     bits |= kBitFlag_isStun;
        if (bit_unk18)      bits |= kBitFlag_unk18;
        if (bit_unk19)      bits |= kBitFlag_unk19;
        if (bit_unk20)      bits |= kBitFlag_unk20;
        if (bit_isFire)     bits |= kBitFlag_isFire;
        if (bit_unk22)      bits |= kBitFlag_unk22;
        if (bit_isGas)      bits |= kBitFlag_isGas;
        if (bit_unk24)      bits |= kBitFlag_unk24;
        if (bit_unk25)      bits |= kBitFlag_unk25;
        if (bit_isElectric) bits |= kBitFlag_isElectric;
        if (bit_unk27)      bits |= kBitFlag_unk27;
        if (bit_unk28)      bits |= kBitFlag_unk28;
        d.bitFlags = bits;

        g_Deps.LuaPop(L, 1); // pop damage table
        return result;
    }

    // ---- SupportWeaponParameter row spec parser ----
    //
    // swpRow accepts:
    //   nil   -> mode=None (defer to orig — but the orig table likely
    //            doesn't have an entry for our custom swpId, so the user
    //            should usually provide this)
    //   table -> mode=Inline, parse subfields:
    //     ammo  number  capacity (clamped: <1 -> 0x3FFF)
    //     p1    number  parameter 1 (×10)
    //     p2    number  parameter 2 (×10)
    //     grade number  grade level (int8)
    enum class SwpRowSpecMode : std::uint8_t { None, Inline };
    struct SwpRowSpecResult
    {
        SwpRowSpecMode   mode = SwpRowSpecMode::None;
        SupportWeaponRow row{};
    };

    static SwpRowSpecResult ReadSwpRowField(lua_State* L, int specIdx)
    {
        SwpRowSpecResult result;

        g_Deps.LuaGetField(L, specIdx, "swpRow");
        if (g_Deps.LuaType(L, -1) != LUA_TTABLE_51)
        {
            g_Deps.LuaPop(L, 1);
            return result;
        }

        const int rowIdx = -1;
        std::int32_t ammo = 0, p1 = 0, p2 = 0, grade = 0;
        TryReadIntField(L, rowIdx, "ammo",  ammo);
        TryReadIntField(L, rowIdx, "p1",    p1);
        TryReadIntField(L, rowIdx, "p2",    p2);
        TryReadIntField(L, rowIdx, "grade", grade);

        result.mode        = SwpRowSpecMode::Inline;
        result.row.ammo    = (ammo < 1) ? 0x3FFF : static_cast<std::uint16_t>(ammo);
        result.row.p1_x10  = static_cast<std::int16_t>(p1 * 10);
        result.row.p2_x10  = static_cast<std::int16_t>(p2 * 10);
        result.row.grade   = static_cast<std::int8_t>(grade);
        result.row.pad     = 0;

        g_Deps.LuaPop(L, 1);
        return result;
    }

    // ---- chaffEffect spec parser ----
    //
    // chaffEffect accepts:
    //   nil      -> hasChaffEffect=false (default)
    //   table    -> { radius=15, duration=20 } — both fields optional with
    //               sensible vanilla-CHAFF-like defaults.
    struct ChaffEffectSpecResult
    {
        bool  has      = false;
        float radius   = 15.0f;
        float duration = 20.0f;
    };

    static bool TryReadFloatField(lua_State* L, int tableIdx, const char* name, float& out)
    {
        g_Deps.LuaGetField(L, tableIdx, name);
        const bool isNum = (g_Deps.LuaType(L, -1) == LUA_TNUMBER_51);
        if (isNum)
            out = static_cast<float>(g_Deps.GetLuaInt(L, -1));
        g_Deps.LuaPop(L, 1);
        return isNum;
    }

    static ChaffEffectSpecResult ReadChaffEffectField(lua_State* L, int specIdx)
    {
        ChaffEffectSpecResult result;

        g_Deps.LuaGetField(L, specIdx, "chaffEffect");
        if (g_Deps.LuaType(L, -1) != LUA_TTABLE_51)
        {
            g_Deps.LuaPop(L, 1);
            return result;
        }

        const int tIdx = -1;
        result.has = true;
        (void)TryReadFloatField(L, tIdx, "radius",   result.radius);
        (void)TryReadFloatField(L, tIdx, "duration", result.duration);

        g_Deps.LuaPop(L, 1);
        return result;
    }
}

namespace SupportWeaponType
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
    }

    int __cdecl Lua_SetSupportWeaponType(lua_State* L)
    {
        if (!L || !EnsureLuaReady())
            return 0;

        if (!IsLuaNumber(L, 1) || !IsLuaNumber(L, 2))
        {
            Log("[SupportWeaponType] Lua_SetSupportWeaponType: expected (number equipId, number swpType)\n");
            return 0;
        }

        const std::int32_t equipId = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, 1));
        const std::int32_t swpType = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, 2));

        SetSupportWeaponTypeInternal(equipId, swpType);
        return 0;
    }

    int __cdecl Lua_RemoveSupportWeaponType(lua_State* L)
    {
        if (!L || !EnsureLuaReady())
            return 0;

        if (!IsLuaNumber(L, 1))
        {
            Log("[SupportWeaponType] Lua_RemoveSupportWeaponType: expected (number equipId)\n");
            return 0;
        }

        const std::int32_t equipId = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, 1));
        RemoveSupportWeaponTypeInternal(equipId);
        return 0;
    }

    int __cdecl Lua_ClearSupportWeaponTypes(lua_State* L)
    {
        UNREFERENCED_PARAMETER(L);
        ClearSupportWeaponTypesInternal();
        return 0;
    }

    // V_FrameWork.RegisterSupportWeaponCategory{
    //     name     = "MyChaff",
    //     blast    = { flag = 2, maxRange = 15, optRange = 10 },   -- optional
    //     behavior = "smoke",                                       -- optional
    // }
    //
    //   Mints a brand-new SWP_TYPE id (>= 0x80) for a custom support-weapon
    //   category. Writes TppEquip.SWP_TYPE_<name> = newId so Lua scripts can
    //   reference it directly. Returns the new id on success, -1 on failure.
    //
    //   Required:
    //     name     string  short identifier; appended to "SWP_TYPE_" for the
    //                      TppEquip global key.
    //
    //   Optional:
    //     blast    table   custom BlastParameter values — see TryReadBlastSubtable.
    //                      When provided, equipIds bound to this category get
    //                      this blast struct from GetBlastParamByEquipId
    //                      regardless of which native behavior template runs.
    //     behavior string  one of "grenade", "smoke", "molotov", "decoy",
    //              | int    "kibidango" — picks which native UpdateAction*
    //                      template fires the throwable's per-frame physics
    //                      and detonation. May also pass a vanilla SWP_TYPE_*
    //                      number (0..0x16) for direct selection. Defaults to
    //                      "grenade" when omitted.
    //
    //   Without `behavior`, the new category still works for Lua-side
    //   branching but the native throwable would have no UpdateAction running
    //   per-frame (since the engine's switch only knows 0..0x16). The default
    //   ensures the throwable actually does something when thrown.
    static bool TryReadBehaviorField(lua_State* L, int specIdx, std::int32_t& outSwpType)
    {
        g_Deps.LuaGetField(L, specIdx, "behavior");
        const int t = g_Deps.LuaType(L, -1);

        if (t == LUA_TNUMBER_51)
        {
            outSwpType = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, -1));
            g_Deps.LuaPop(L, 1);
            return true;
        }

        if (t == LUA_TSTRING_51)
        {
            const char* raw = g_Deps.GetLuaString(L, -1);
            std::string s = raw ? raw : "";
            // Lowercase for tolerant matching.
            for (auto& c : s)
                c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
            g_Deps.LuaPop(L, 1);

            if (s == "grenade")        { outSwpType = kBehaviorSwpTypeGrenade;      return true; }
            if (s == "smoke")          { outSwpType = kBehaviorSwpTypeSmoke;        return true; }
            if (s == "stun")           { outSwpType = kBehaviorSwpTypeStunGrenade;  return true; }
            if (s == "stungrenade")    { outSwpType = kBehaviorSwpTypeStunGrenade;  return true; }
            if (s == "flashbang")      { outSwpType = kBehaviorSwpTypeStunGrenade;  return true; }
            if (s == "sleepinggus")    { outSwpType = kBehaviorSwpTypeSleepingGus;  return true; }
            if (s == "sleepgas")       { outSwpType = kBehaviorSwpTypeSleepingGus;  return true; }
            if (s == "sleep")          { outSwpType = kBehaviorSwpTypeSleepingGus;  return true; }
            if (s == "molotov")        { outSwpType = kBehaviorSwpTypeMolotov;      return true; }
            if (s == "fire")           { outSwpType = kBehaviorSwpTypeMolotov;      return true; }
            if (s == "decoy")          { outSwpType = kBehaviorSwpTypeDecoy;        return true; }
            if (s == "kibidango")      { outSwpType = kBehaviorSwpTypeKibidango;    return true; }
            if (s == "lure")           { outSwpType = kBehaviorSwpTypeKibidango;    return true; }

            // Unknown string — fall through and let the caller decide (error
            // vs. default). We return false and leave outSwpType untouched.
            return false;
        }

        // Field absent or wrong type — caller will apply default.
        g_Deps.LuaPop(L, 1);
        return false;
    }

    int __cdecl Lua_RegisterSupportWeaponCategory(lua_State* L)
    {
        if (!L || !EnsureLuaReadyForCategories())
        {
            if (g_Deps.PushLuaNumber)
                g_Deps.PushLuaNumber(L, -1.0f);
            return 1;
        }

        if (g_Deps.LuaType(L, 1) != LUA_TTABLE_51)
        {
            Log("[SupportWeaponType] RegisterSupportWeaponCategory: expected a table argument\n");
            g_Deps.PushLuaNumber(L, -1.0f);
            return 1;
        }

        // Read 'name' (required).
        g_Deps.LuaGetField(L, 1, "name");
        if (!IsLuaString(L, -1))
        {
            g_Deps.LuaPop(L, 1);
            Log("[SupportWeaponType] RegisterSupportWeaponCategory: missing or non-string 'name'\n");
            g_Deps.PushLuaNumber(L, -1.0f);
            return 1;
        }
        std::string name = g_Deps.GetLuaString(L, -1);
        g_Deps.LuaPop(L, 1);

        if (name.empty())
        {
            Log("[SupportWeaponType] RegisterSupportWeaponCategory: empty 'name'\n");
            g_Deps.PushLuaNumber(L, -1.0f);
            return 1;
        }

        // Read 'behavior' (optional). Accepts string aliases or vanilla
        // SWP_TYPE_* number; default "grenade" if absent or unrecognized.
        std::int32_t behaviorSwpType = kDefaultBehaviorSwpType;
        if (TryReadBehaviorField(L, 1, behaviorSwpType))
        {
            if (behaviorSwpType < 0 || behaviorSwpType > kVanillaSwpTypeMax)
            {
                Log("[SupportWeaponType] RegisterSupportWeaponCategory '%s': behavior=%d out of range (must be 0..0x16); using default\n",
                    name.c_str(),
                    behaviorSwpType);
                behaviorSwpType = kDefaultBehaviorSwpType;
            }
        }

        // Optional 'blast' field — table form (inline values) or number
        // form (link to existing BLA_* row). See ReadBlastField for the
        // accepted shapes.
        const BlastSpecResult blastSpec = ReadBlastField(L, 1);

        // Translate parsed BlastSpec → registry BlastMode. We pre-compute the
        // pair so both the new-registration and re-registration paths apply
        // the same value.
        BlastMode      newBlastMode    = BlastMode::None;
        BlastParameter newInlineBlast{};
        std::int32_t   newLinkedBlast  = 0;
        switch (blastSpec.mode)
        {
            case BlastSpecMode::Inline:
                newBlastMode    = BlastMode::Inline;
                newInlineBlast  = blastSpec.inlineBlast;
                break;
            case BlastSpecMode::Linked:
                newBlastMode    = BlastMode::Linked;
                newLinkedBlast  = blastSpec.linkedBlastId;
                break;
            case BlastSpecMode::None:
            default:
                break;
        }

        // Optional 'damage' field (table form = inline custom DamageParameter,
        // or number = AttackId link to existing row).
        const DamageSpecResult damageSpec = ReadDamageField(L, 1);

        DamageMode      newDamageMode  = DamageMode::None;
        DamageParameter newInlineDamage{};
        std::uint32_t   newLinkedAtkId = 0;
        switch (damageSpec.mode)
        {
            case DamageSpecMode::Inline:
                newDamageMode    = DamageMode::Inline;
                newInlineDamage  = damageSpec.inlineDamage;
                break;
            case DamageSpecMode::Linked:
                newDamageMode    = DamageMode::Linked;
                newLinkedAtkId   = damageSpec.linkedAttackId;
                break;
            case DamageSpecMode::None:
            default:
                break;
        }

        // Optional 'swpRow' field (table form = inline ammo / p1 / p2 / grade).
        const SwpRowSpecResult swpRowSpec = ReadSwpRowField(L, 1);

        SwpRowMode       newSwpRowMode = SwpRowMode::None;
        SupportWeaponRow newInlineSwpRow{};
        if (swpRowSpec.mode == SwpRowSpecMode::Inline)
        {
            newSwpRowMode    = SwpRowMode::Inline;
            newInlineSwpRow  = swpRowSpec.row;
        }

        // Optional 'chaffEffect' field — when present, registered equipIds
        // bound to this category fire RangeAttackSystemImpl::RequestToChaff
        // at projectile detonation.
        const ChaffEffectSpecResult chaffSpec = ReadChaffEffectField(L, 1);

        std::int32_t allocatedId = -1;
        bool firstRegistration = false;
        bool needLazyDamageHook = false;
        {
            std::lock_guard<std::mutex> lock(g_CategoryMutex);

            const auto existingByName = g_CategoryByName.find(name);
            if (existingByName != g_CategoryByName.end())
            {
                allocatedId = existingByName->second;
                // Update entry in place so re-registering with new
                // semantics works. The id stays stable across mod reloads
                // for the lifetime of the process — important so any
                // already-bound equipIds keep working.
                auto entryIt = g_RegisteredCategories.find(allocatedId);
                if (entryIt != g_RegisteredCategories.end())
                {
                    entryIt->second.behaviorSwpType = behaviorSwpType;
                    // Only overwrite blast/damage/swpRow when the new spec
                    // provides one; omitting any of these on re-register
                    // keeps the previous override (clearing would break
                    // equipIds already bound to it).
                    if (blastSpec.mode != BlastSpecMode::None)
                    {
                        entryIt->second.blastMode     = newBlastMode;
                        entryIt->second.inlineBlast   = newInlineBlast;
                        entryIt->second.linkedBlastId = newLinkedBlast;
                    }
                    if (damageSpec.mode != DamageSpecMode::None)
                    {
                        entryIt->second.damageMode    = newDamageMode;
                        entryIt->second.inlineDamage  = newInlineDamage;
                        entryIt->second.linkedAttackId = newLinkedAtkId;
                        if (newDamageMode == DamageMode::Inline)
                        {
                            // Re-registration with new inline damage: keep
                            // the same synthetic AttackId so any equipIds
                            // already bound continue to resolve correctly.
                            // entryIt->second.syntheticAttackId stays as
                            // it was. If the entry didn't have one yet
                            // (previously was Linked or None), allocate.
                            if (entryIt->second.syntheticAttackId == 0)
                            {
                                if (g_NextSyntheticAttackId <= kSyntheticAttackIdMax)
                                {
                                    entryIt->second.syntheticAttackId = g_NextSyntheticAttackId++;
                                    g_SyntheticAttackIdToCategory.emplace(
                                        entryIt->second.syntheticAttackId,
                                        allocatedId);
                                }
                            }
                            needLazyDamageHook = true;
                        }
                    }
                    if (swpRowSpec.mode != SwpRowSpecMode::None)
                    {
                        entryIt->second.swpRowMode    = newSwpRowMode;
                        entryIt->second.inlineSwpRow  = newInlineSwpRow;
                    }
                    if (chaffSpec.has)
                    {
                        entryIt->second.hasChaffEffect = true;
                        entryIt->second.chaffRadius    = chaffSpec.radius;
                        entryIt->second.chaffDuration  = chaffSpec.duration;
                    }
                }
            }
            else
            {
                allocatedId = AllocateCategoryIdLocked(name);
                if (allocatedId < 0)
                {
                    Log("[SupportWeaponType] RegisterSupportWeaponCategory '%s': allocator exhausted (max=0x%X)\n",
                        name.c_str(),
                        kCategoryIdMax);
                    g_Deps.PushLuaNumber(L, -1.0f);
                    return 1;
                }

                RegisteredCategory entry{};
                entry.name            = name;
                entry.behaviorSwpType = behaviorSwpType;
                entry.blastMode       = newBlastMode;
                entry.inlineBlast     = newInlineBlast;
                entry.linkedBlastId   = newLinkedBlast;
                entry.damageMode      = newDamageMode;
                entry.inlineDamage    = newInlineDamage;
                entry.linkedAttackId  = newLinkedAtkId;
                entry.swpRowMode      = newSwpRowMode;
                entry.inlineSwpRow    = newInlineSwpRow;
                entry.hasChaffEffect  = chaffSpec.has;
                entry.chaffRadius     = chaffSpec.radius;
                entry.chaffDuration   = chaffSpec.duration;

                if (newDamageMode == DamageMode::Inline)
                {
                    if (g_NextSyntheticAttackId <= kSyntheticAttackIdMax)
                    {
                        entry.syntheticAttackId = g_NextSyntheticAttackId++;
                        g_SyntheticAttackIdToCategory.emplace(
                            entry.syntheticAttackId,
                            allocatedId);
                        needLazyDamageHook = true;
                    }
                    else
                    {
                        Log("[SupportWeaponType] RegisterSupportWeaponCategory '%s': synthetic AttackId allocator exhausted (max=0x%X) — inline damage falls back to None\n",
                            name.c_str(),
                            kSyntheticAttackIdMax);
                        entry.damageMode = DamageMode::None;
                    }
                }

                g_RegisteredCategories.emplace(allocatedId, std::move(entry));
                g_CategoryByName.emplace(name, allocatedId);
                firstRegistration = true;
            }
        }

        // Inline damage requires the DamageParameterTable::GetDamageParameter
        // hook so that synthetic AttackIds resolve to our framework-owned
        // struct. Install it lazily — the function pointer is fetched from
        // the singleton's vtable, and the singleton may not exist before
        // engine init. Outside the registry lock to avoid deadlock paths
        // through the hook's MinHook calls.
        if (needLazyDamageHook)
            (void)LazyInstall_DamageParameterTable_GetDamageParameter_Hook();

        // Expose to Lua as TppEquip.SWP_TYPE_<name>.
        const std::string constName = "SWP_TYPE_" + name;
        WriteSwpTypeConstantToLua(L, constName, allocatedId);

        const char* blastDesc  = "none";
        switch (blastSpec.mode)
        {
            case BlastSpecMode::Inline: blastDesc = "inline"; break;
            case BlastSpecMode::Linked: blastDesc = "linked"; break;
            case BlastSpecMode::None:
            default:                    blastDesc = "none";   break;
        }
        const char* damageDesc = "none";
        switch (damageSpec.mode)
        {
            case DamageSpecMode::Inline: damageDesc = "inline"; break;
            case DamageSpecMode::Linked: damageDesc = "linked"; break;
            case DamageSpecMode::None:
            default:                     damageDesc = "none";  break;
        }
        const char* swpRowDesc =
            (swpRowSpec.mode == SwpRowSpecMode::Inline) ? "inline" : "none";

        Log("[SupportWeaponType] %s category '%s' => SWP_TYPE id 0x%X (%d), behaviorSwpType=0x%X, blast=%s, damage=%s, swpRow=%s\n",
            firstRegistration ? "Registered" : "Re-registered",
            name.c_str(),
            allocatedId,
            allocatedId,
            behaviorSwpType,
            blastDesc,
            damageDesc,
            swpRowDesc);

        if (firstRegistration && blastSpec.mode == BlastSpecMode::Inline)
        {
            Log("[SupportWeaponType]   inline blast: maxRangeX10=%d optRangeX10=%d internalFlag=0x%02X\n",
                newInlineBlast.maxRangeX10,
                newInlineBlast.optRangeX10,
                newInlineBlast.internalFlag);
        }
        else if (firstRegistration && blastSpec.mode == BlastSpecMode::Linked)
        {
            Log("[SupportWeaponType]   linked blast: BLA id=0x%X (%d) — values come from EquipParameterTables.BlastParameter row\n",
                newLinkedBlast,
                newLinkedBlast);
        }
        if (firstRegistration && damageSpec.mode == DamageSpecMode::Inline)
        {
            Log("[SupportWeaponType]   inline damage allocated synthetic AttackId=0x%X\n",
                static_cast<unsigned int>(g_RegisteredCategories.at(allocatedId).syntheticAttackId));
        }
        else if (firstRegistration && damageSpec.mode == DamageSpecMode::Linked)
        {
            Log("[SupportWeaponType]   linked damage: ATK id=0x%X (%d) — values come from EquipParameterTables.DamageParameter row\n",
                newLinkedAtkId,
                newLinkedAtkId);
        }
        if (firstRegistration && swpRowSpec.mode == SwpRowSpecMode::Inline)
        {
            Log("[SupportWeaponType]   inline swpRow: ammo=%u p1=%d p2=%d grade=%d\n",
                static_cast<unsigned int>(newInlineSwpRow.ammo),
                static_cast<int>(newInlineSwpRow.p1_x10) / 10,
                static_cast<int>(newInlineSwpRow.p2_x10) / 10,
                static_cast<int>(newInlineSwpRow.grade));
        }
        if (chaffSpec.has)
        {
            Log("[SupportWeaponType]   chaffEffect: radius=%.1f duration=%.1f — fires RequestToChaff at detonation\n",
                chaffSpec.radius,
                chaffSpec.duration);
        }

        g_Deps.PushLuaNumber(L, static_cast<float>(allocatedId));
        return 1;
    }

    // V_FrameWork.GetSupportWeaponCategory(equipId) -> number
    //
    //   Returns the unmasked custom swpType for an equipId — including
    //   registered categories above the vanilla 0..0x16 range. If the
    //   equipId has no custom mapping, falls back to the vanilla
    //   GetSupportWeaponTypeId result (0..0x17).
    //
    //   This is the function Lua scripts should use when they want to
    //   branch on the *true* category id, since the vanilla
    //   TppEquip.GetSupportWeaponTypeId is hooked to return the inherited
    //   value for native compatibility.
    int __cdecl Lua_GetSupportWeaponCategory(lua_State* L)
    {
        if (!L || !EnsureLuaReady() || !g_Deps.PushLuaNumber)
            return 0;

        if (!IsLuaNumber(L, 1))
        {
            g_Deps.PushLuaNumber(L, static_cast<float>(kVanillaSwpTypeFallback));
            return 1;
        }

        const std::int32_t equipId = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, 1));

        std::int32_t customSwpType = 0;
        if (TryGetCustomSupportWeaponType(equipId, customSwpType))
        {
            g_Deps.PushLuaNumber(L, static_cast<float>(customSwpType));
            return 1;
        }

        // No custom mapping — fall back to the orig vtable behavior so the
        // function is a drop-in equivalent of TppEquip.GetSupportWeaponTypeId
        // for vanilla equipIds. The orig needs the EquipIdTableImpl singleton
        // as `self`; we captured it during the first natural hook invocation.
        void* capturedSelf = g_CapturedThis.load(std::memory_order_relaxed);
        if (g_OrigGetSupportWeaponTypeId && capturedSelf)
        {
            const std::uint64_t origSwp = g_OrigGetSupportWeaponTypeId(capturedSelf, equipId);
            g_Deps.PushLuaNumber(L, static_cast<float>(static_cast<std::int32_t>(origSwp)));
            return 1;
        }

        g_Deps.PushLuaNumber(L, static_cast<float>(kVanillaSwpTypeFallback));
        return 1;
    }

    bool Install_EquipIdTableImpl_GetSupportWeaponTypeId_Hook()
    {
        if (g_GetSupportWeaponTypeIdHookInstalled)
        {
            Log("[SupportWeaponType] GetSupportWeaponTypeId hook already installed\n");
            return true;
        }

        void* target = ResolveGameAddress(gAddr.EquipIdTableImpl_GetSupportWeaponTypeId);
        if (!target)
        {
            Log("[SupportWeaponType] Failed to resolve GetSupportWeaponTypeId target\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkGetSupportWeaponTypeId),
            reinterpret_cast<void**>(&g_OrigGetSupportWeaponTypeId)
        );

        if (!ok)
        {
            Log("[SupportWeaponType] Failed to install GetSupportWeaponTypeId hook\n");
            return false;
        }

        g_GetSupportWeaponTypeIdHookInstalled = true;
        Log("[SupportWeaponType] GetSupportWeaponTypeId hook installed\n");
        return true;
    }

    bool Uninstall_EquipIdTableImpl_GetSupportWeaponTypeId_Hook()
    {
        if (!g_GetSupportWeaponTypeIdHookInstalled)
            return true;

        if (void* target = ResolveGameAddress(gAddr.EquipIdTableImpl_GetSupportWeaponTypeId))
        {
            DisableAndRemoveHook(target);
        }

        g_OrigGetSupportWeaponTypeId = nullptr;
        g_GetSupportWeaponTypeIdHookInstalled = false;
        return true;
    }

    bool Install_ThrowingImpl_GetBlastParamByEquipId_Hook()
    {
        if (g_GetBlastParamByEquipIdHookInstalled)
        {
            Log("[SupportWeaponType] GetBlastParamByEquipId hook already installed\n");
            return true;
        }

        void* target = ResolveGameAddress(gAddr.ThrowingImpl_GetBlastParamByEquipId);
        if (!target)
        {
            Log("[SupportWeaponType] Failed to resolve GetBlastParamByEquipId target\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkGetBlastParamByEquipId),
            reinterpret_cast<void**>(&g_OrigGetBlastParamByEquipId)
        );

        if (!ok)
        {
            Log("[SupportWeaponType] Failed to install GetBlastParamByEquipId hook\n");
            return false;
        }

        g_GetBlastParamByEquipIdHookInstalled = true;
        Log("[SupportWeaponType] GetBlastParamByEquipId hook installed\n");
        return true;
    }

    bool Uninstall_ThrowingImpl_GetBlastParamByEquipId_Hook()
    {
        if (!g_GetBlastParamByEquipIdHookInstalled)
            return true;

        if (void* target = ResolveGameAddress(gAddr.ThrowingImpl_GetBlastParamByEquipId))
        {
            DisableAndRemoveHook(target);
        }

        g_OrigGetBlastParamByEquipId = nullptr;
        g_GetBlastParamByEquipIdHookInstalled = false;
        return true;
    }

    bool Install_EquipParameterTablesImpl_GetSupportWeaponParameterBlock_Hook()
    {
        if (g_GetSupportWeaponParameterBlockHookInstalled)
        {
            Log("[SupportWeaponType] GetSupportWeaponParameterBlock hook already installed\n");
            return true;
        }

        void* target = ResolveGameAddress(gAddr.EquipParameterTablesImpl_GetSupportWeaponParameterBlock);
        if (!target)
        {
            Log("[SupportWeaponType] Failed to resolve GetSupportWeaponParameterBlock target\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkGetSupportWeaponParameterBlock),
            reinterpret_cast<void**>(&g_OrigGetSupportWeaponParameterBlock)
        );

        if (!ok)
        {
            Log("[SupportWeaponType] Failed to install GetSupportWeaponParameterBlock hook\n");
            return false;
        }

        g_GetSupportWeaponParameterBlockHookInstalled = true;
        Log("[SupportWeaponType] GetSupportWeaponParameterBlock hook installed\n");
        return true;
    }

    bool Uninstall_EquipParameterTablesImpl_GetSupportWeaponParameterBlock_Hook()
    {
        if (!g_GetSupportWeaponParameterBlockHookInstalled)
            return true;

        if (void* target = ResolveGameAddress(gAddr.EquipParameterTablesImpl_GetSupportWeaponParameterBlock))
        {
            DisableAndRemoveHook(target);
        }

        g_OrigGetSupportWeaponParameterBlock = nullptr;
        g_GetSupportWeaponParameterBlockHookInstalled = false;
        return true;
    }

    bool Install_EquipParameterTablesImpl_GetAttackIdByEquipId_Hook()
    {
        if (g_GetAttackIdByEquipIdHookInstalled)
        {
            Log("[SupportWeaponType] GetAttackIdByEquipId hook already installed\n");
            return true;
        }

        void* target = ResolveGameAddress(gAddr.EquipParameterTablesImpl_GetAttackIdByEquipId);
        if (!target)
        {
            Log("[SupportWeaponType] Failed to resolve GetAttackIdByEquipId target\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkGetAttackIdByEquipId),
            reinterpret_cast<void**>(&g_OrigGetAttackIdByEquipId)
        );

        if (!ok)
        {
            Log("[SupportWeaponType] Failed to install GetAttackIdByEquipId hook\n");
            return false;
        }

        g_GetAttackIdByEquipIdHookInstalled = true;
        Log("[SupportWeaponType] GetAttackIdByEquipId hook installed\n");
        return true;
    }

    bool Uninstall_EquipParameterTablesImpl_GetAttackIdByEquipId_Hook()
    {
        if (!g_GetAttackIdByEquipIdHookInstalled)
            return true;

        if (void* target = ResolveGameAddress(gAddr.EquipParameterTablesImpl_GetAttackIdByEquipId))
        {
            DisableAndRemoveHook(target);
        }

        g_OrigGetAttackIdByEquipId = nullptr;
        g_GetAttackIdByEquipIdHookInstalled = false;
        return true;
    }

    // The damage-row hook is keyed off a function pointer fetched from the
    // DamageParameterTable singleton's vtable[0] at runtime — there's no
    // static address for the function body. We resolve it via
    // fox::GetQuarkSystemTable on first use (when an inline-damage category
    // is registered, since the engine is fully initialized by then).
    bool LazyInstall_DamageParameterTable_GetDamageParameter_Hook()
    {
        if (g_GetDamageParameterHookInstalled)
            return true;

        using GetQuarkSystemTable_t = void* (__cdecl*)();
        auto getQuark = reinterpret_cast<GetQuarkSystemTable_t>(
            ResolveGameAddress(gAddr.fox_GetQuarkSystemTable));
        if (!getQuark)
        {
            Log("[SupportWeaponType] DamageParameter hook: fox_GetQuarkSystemTable address not configured (build is JP / address missing) — inline damage disabled\n");
            return false;
        }

        void* qs = getQuark();
        if (!qs)
        {
            Log("[SupportWeaponType] DamageParameter hook: GetQuarkSystemTable returned null — engine not yet initialized?\n");
            return false;
        }

        // Walk the chain:  intermediate = *(qs + 0x98); singleton = *(intermediate + 0xa0);
        // vtable = *singleton; getDamageParam = vtable[0].
        std::uint8_t** intermediatePtr =
            reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(qs) + 0x98);
        std::uint8_t* intermediate = *intermediatePtr;
        if (!intermediate)
        {
            Log("[SupportWeaponType] DamageParameter hook: QuarkSystem[0x98] is null\n");
            return false;
        }

        std::uint8_t** singletonPtr =
            reinterpret_cast<std::uint8_t**>(intermediate + 0xa0);
        std::uint8_t* singleton = *singletonPtr;
        if (!singleton)
        {
            Log("[SupportWeaponType] DamageParameter hook: QuarkSystem[0x98][0xa0] is null\n");
            return false;
        }

        void** vtable = *reinterpret_cast<void***>(singleton);
        if (!vtable)
        {
            Log("[SupportWeaponType] DamageParameter hook: singleton vtable is null\n");
            return false;
        }

        void* funcPtr = vtable[0];
        if (!funcPtr)
        {
            Log("[SupportWeaponType] DamageParameter hook: vtable[0] is null\n");
            return false;
        }

        g_DamageParameterTableSingleton = singleton;
        g_DamageParameterTableHookTarget = funcPtr;

        const bool ok = CreateAndEnableHook(
            funcPtr,
            reinterpret_cast<void*>(&hkGetDamageParameter),
            reinterpret_cast<void**>(&g_OrigGetDamageParameter)
        );

        if (!ok)
        {
            Log("[SupportWeaponType] Failed to install DamageParameter hook\n");
            g_DamageParameterTableSingleton = nullptr;
            g_DamageParameterTableHookTarget = nullptr;
            return false;
        }

        g_GetDamageParameterHookInstalled = true;
        Log("[SupportWeaponType] DamageParameter hook installed (target=0x%p, singleton=0x%p)\n",
            funcPtr,
            singleton);
        return true;
    }

    bool Uninstall_DamageParameterTable_GetDamageParameter_Hook()
    {
        if (!g_GetDamageParameterHookInstalled)
            return true;

        if (g_DamageParameterTableHookTarget)
            DisableAndRemoveHook(g_DamageParameterTableHookTarget);

        g_OrigGetDamageParameter = nullptr;
        g_DamageParameterTableSingleton = nullptr;
        g_DamageParameterTableHookTarget = nullptr;
        g_GetDamageParameterHookInstalled = false;
        return true;
    }

    bool TryGetChaffEffectForEquipId(int equipId, float& outRadius, float& outDuration)
    {
        std::int32_t customSwpType = 0;
        if (!TryGetCustomSupportWeaponType(static_cast<std::int32_t>(equipId), customSwpType))
            return false;

        std::lock_guard<std::mutex> lock(g_CategoryMutex);
        const auto it = g_RegisteredCategories.find(customSwpType);
        if (it == g_RegisteredCategories.end() || !it->second.hasChaffEffect)
            return false;

        outRadius   = it->second.chaffRadius;
        outDuration = it->second.chaffDuration;
        return true;
    }
}
