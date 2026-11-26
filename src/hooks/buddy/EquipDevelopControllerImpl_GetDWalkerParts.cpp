#include <pch.h>

#include "EquipDevelopControllerImpl_GetDWalkerParts.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AddressSet.h"
#include "BattleGearCustomizePage.h"
#include "BuddyOutfitRegistry.h"
#include "EquipPartParams.h"
#include "HookUtils.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "log.h"

namespace
{
    using DevelopInfoIndex_t = std::uint64_t(__fastcall*)(void* base20, std::uint8_t slot, std::uint8_t type,
                                                          std::uint8_t level);
    using CurrentLevel_t     = std::uint64_t(__fastcall*)(void* base20, std::uint8_t slot, std::uint8_t type);
    using ByFlowIndex_t      = std::uint64_t(__fastcall*)(void* base20, std::uint16_t flowIndex);
    using VtblU16_t          = std::uint64_t(__fastcall*)(void* base20, std::uint16_t arg);

    constexpr std::size_t   kVtblIndexByDevelopId = 0xE0;
    constexpr std::size_t   kVtblDevelopIdByIndex = 0x128;
    constexpr std::size_t   kVtblIsDeveloped      = 0x1C8;
    constexpr std::uint16_t kNoIndex              = 0x400;
    constexpr std::uint32_t kEquipTypeHandgun     = 2;
    constexpr std::uint32_t kEquipTypeSmg         = 3;

    constexpr std::uint8_t kInfoIndexEntry[] =
    {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x45, 0x32, 0xDB, 0x48, 0x8D, 0x1D, 0x40, 0x41, 0x43, 0x01,
        0x41, 0x0F, 0xB6, 0xC3, 0x4C, 0x8D, 0x14, 0x40,
    };
    constexpr int kInfoIndexTableRel = 12;   // rel32 of lea rbx, D-Walker parts table: differs between EN and JP
    constexpr std::uint8_t kCurrentLevelEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20,
    };
    constexpr std::uint8_t kRowFieldEntry[] =   // Id, Level and Type share this entry; the row byte they read differs
    {
        0x48, 0x83, 0xEC, 0x28, 0xB8, 0x00, 0x04, 0x00, 0x00, 0x66, 0x3B, 0xD0, 0x74, 0x36, 0x48, 0x8B,
        0x01, 0xFF, 0x90, 0x28, 0x01, 0x00, 0x00, 0x44,
    };
    constexpr std::size_t  kRowFieldByteAt = 0x57;   // disp8 of movzx eax, byte [r9+rax*4+N]
    constexpr std::uint8_t kRowSlot  = 2;
    constexpr std::uint8_t kRowType  = 3;
    constexpr std::uint8_t kRowLevel = 4;

    DevelopInfoIndex_t g_OrigInfoIndex    = nullptr;
    CurrentLevel_t     g_OrigCurrentLevel = nullptr;
    ByFlowIndex_t      g_OrigId           = nullptr;
    ByFlowIndex_t      g_OrigLevel        = nullptr;
    ByFlowIndex_t      g_OrigType         = nullptr;
    void* g_TargetInfoIndex    = nullptr;
    void* g_TargetCurrentLevel = nullptr;
    void* g_TargetId           = nullptr;
    void* g_TargetLevel        = nullptr;
    void* g_TargetType         = nullptr;

    struct Verdict
    {
        std::uint32_t generation;
        std::uint16_t equipId;
        bool          usable;
    };

    std::mutex                              g_WarnMutex;
    std::unordered_set<std::string>         g_Warned;
    std::unordered_map<std::string, Verdict> g_Verdicts;

    VtblU16_t VtblFn(void* obj, std::size_t offset)
    {
        return reinterpret_cast<VtblU16_t>((*reinterpret_cast<void***>(obj))[offset / sizeof(void*)]);
    }

    bool IsVanillaWalkerSidearm(std::uint16_t equipId)   // the NPC guns in buddy_wg2_common.fpk
    {
        switch (equipId)
        {
        case 0x1: case 0x2: case 0x4: case 0x7: case 0x8: case 0x9: case 0xA: case 0xB: case 0xD:
            return true;
        default:
            return false;
        }
    }

    const char* SidearmProblem(const buddyoutfit::Entry& e)
    {
        const std::uint32_t type = EquipParam_GetEquipType(e.equipId);
        if (type != kEquipTypeHandgun && type != kEquipTypeSmg)
            return "is not a handgun or SMG, the row is not a D-Walker sidearm";
        if (IsVanillaWalkerSidearm(e.equipId))
            return nullptr;
        std::uint64_t pack = 0;
        if (!TppEquip_GetCustomEquipPack(e.equipId, &pack))
            return "has no pack in the EquipIdTable";
        for (const std::int32_t other : buddyoutfit::NonBuddyRowEquipIds())
            if (other == e.equipId)
                return "is one of Snake's weapons, the D-Walker row needs an equipId of its own";
        return nullptr;
    }

    bool SidearmByValue(std::uint8_t slot, std::uint8_t type, buddyoutfit::Entry* e)
    {
        return slot == buddyoutfit::kWalkerSlotSidearm
            && type >= buddyoutfit::FirstCustomValue(buddyoutfit::Kind::WalkerSidearm)
            && buddyoutfit::TryGetByValue(buddyoutfit::Kind::WalkerSidearm, type, e)
            && IsUsableWalkerSidearm(*e);
    }

    bool SidearmAtFlowIndex(void* base20, std::uint16_t flowIndex, buddyoutfit::Entry* e)
    {
        if (!base20 || flowIndex == kNoIndex || !buddyoutfit::HasEntries())
            return false;
        const auto developId = static_cast<std::uint16_t>(VtblFn(base20, kVtblDevelopIdByIndex)(base20, flowIndex));
        return developId != 0 && buddyoutfit::TryGetByDevelopId(developId, e)
            && e->kind == buddyoutfit::Kind::WalkerSidearm && IsUsableWalkerSidearm(*e);
    }

#ifdef _DEBUG   // Battle Gear is unfinished: Debug test builds only
    struct BattleGearPart
    {
        std::uint8_t slot = 0;
        std::uint8_t type = 0;
        std::uint8_t level = 0;
    };

    bool BattleGearGunAtFlowIndex(void* base20, std::uint16_t flowIndex, BattleGearPart* part)
    {
        if (!base20 || flowIndex == kNoIndex || !battleGearCustomizePage::Active())
            return false;
        const auto developId = static_cast<std::uint16_t>(VtblFn(base20, kVtblDevelopIdByIndex)(base20, flowIndex));
        return developId != 0 && battleGearCustomizePage::RegisteredPartRow(developId, &part->slot, &part->type, &part->level);
    }
#endif

    std::uint64_t __fastcall hkGetDWalkerPartsDevelopInfoIndex(void* base20, std::uint8_t slot, std::uint8_t type,
                                                               std::uint8_t level)
    {
#ifdef _DEBUG
        if (base20)
            if (const std::uint16_t developId = battleGearCustomizePage::PickedRegisteredRow(slot, type))
                return VtblFn(base20, kVtblIndexByDevelopId)(base20, developId);
#endif
        buddyoutfit::Entry e;
        const std::uint64_t r = base20 && SidearmByValue(slot, type, &e)
            ? VtblFn(base20, kVtblIndexByDevelopId)(base20, e.developId)
            : g_OrigInfoIndex(base20, slot, type, level);
        return r;
    }

    std::uint64_t __fastcall hkGetDWalkerPartsDevelopedCurrentLevel(void* base20, std::uint8_t slot, std::uint8_t type)
    {
        buddyoutfit::Entry e;
        std::uint64_t r;
        if (base20 && SidearmByValue(slot, type, &e))
        {
            const auto index = static_cast<std::uint16_t>(VtblFn(base20, kVtblIndexByDevelopId)(base20, e.developId));
            r = static_cast<std::uint8_t>(VtblFn(base20, kVtblIsDeveloped)(base20, index)) != 0 ? 1 : 0;
        }
        else
            r = g_OrigCurrentLevel(base20, slot, type);
        return r;
    }

    std::uint64_t __fastcall hkGetDWalkerPartsId(void* base20, std::uint16_t flowIndex)
    {
        buddyoutfit::Entry e;
        if (SidearmAtFlowIndex(base20, flowIndex, &e))
            return e.value;
#ifdef _DEBUG
        BattleGearPart part;
        if (BattleGearGunAtFlowIndex(base20, flowIndex, &part))
            return part.type;
#endif
        return g_OrigId(base20, flowIndex);
    }

    std::uint64_t __fastcall hkGetDWalkerPartsLevel(void* base20, std::uint16_t flowIndex)
    {
        buddyoutfit::Entry e;
        if (SidearmAtFlowIndex(base20, flowIndex, &e))
            return 1;
#ifdef _DEBUG
        BattleGearPart part;
        if (BattleGearGunAtFlowIndex(base20, flowIndex, &part))
            return part.level;
#endif
        return g_OrigLevel(base20, flowIndex);
    }

    std::uint64_t __fastcall hkGetDWalkerPartsType(void* base20, std::uint16_t flowIndex)
    {
        buddyoutfit::Entry e;
        if (SidearmAtFlowIndex(base20, flowIndex, &e))
            return buddyoutfit::kWalkerSlotSidearm;
#ifdef _DEBUG
        BattleGearPart part;
        if (BattleGearGunAtFlowIndex(base20, flowIndex, &part))
            return part.slot;
#endif
        return g_OrigType(base20, flowIndex);
    }

    bool HookOne(std::uintptr_t addr, const std::uint8_t* entry, std::size_t entrySize, int tableRel, int rowField,
                 void* detour, void** orig, void*& target, const char* what)
    {
        void* t = ResolveGameAddress(addr);
        if (!t)
        {
            LogError("BuddyOutfit: %s address missing on this build, custom D-Walker sidearms off", what);
            return false;
        }
        std::vector<std::uint8_t> mask(entrySize, 1);
        if (tableRel >= 0)
            std::memset(mask.data() + tableRel, 0, 4);
        const HookEntry state = CheckHookEntryMasked(t, entry, mask.data(), entrySize);
        std::uint8_t field = 0;
        if (state == HookEntry::Unknown
            || (rowField >= 0 && (!ReadEntryBytesSEH(static_cast<const std::uint8_t*>(t) + kRowFieldByteAt, &field, 1)
                                  || field != rowField)))
        {
            char desc[160] = {};
            DescribeFunctionEntry(t, desc, sizeof(desc));
            LogError("BuddyOutfit: %s code differs on this build (%s), custom D-Walker sidearms off", what, desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
        {
            char label[96] = {};
            snprintf(label, sizeof(label), "BuddyOutfit: %s", what);
            LogHookedOnTop(label, t);
        }
        if (!CreateAndEnableHook(t, detour, orig))
        {
            LogError("BuddyOutfit: %s hook failed, custom D-Walker sidearms off", what);
            return false;
        }
        target = t;
        return true;
    }
}

bool IsUsableWalkerSidearm(const buddyoutfit::Entry& e)
{
    const std::uint32_t generation = buddyoutfit::DevelopRowsGeneration();
    {
        std::lock_guard<std::mutex> lock(g_WarnMutex);
        const auto it = g_Verdicts.find(e.key);
        if (it != g_Verdicts.end() && it->second.generation == generation && it->second.equipId == e.equipId)
            return it->second.usable;
    }
    const char* problem = SidearmProblem(e);
    std::lock_guard<std::mutex> lock(g_WarnMutex);
    g_Verdicts[e.key] = { generation, e.equipId, problem == nullptr };
    if (problem && g_Warned.insert(e.key).second)
        LogWarn("D-Walker sidearm '%s': equipID %u %s", e.key.c_str(), static_cast<unsigned>(e.equipId), problem);
    return problem == nullptr;
}

bool Install_EquipDevelopControllerImpl_GetDWalkerParts_Hooks()
{
    bool ok = true;
    if (!g_TargetInfoIndex)
        ok &= HookOne(gAddr.EquipDevelopControllerImpl_GetDWalkerPartsDevelopInfoIndex,
                      kInfoIndexEntry, sizeof(kInfoIndexEntry), kInfoIndexTableRel, -1,
                      reinterpret_cast<void*>(&hkGetDWalkerPartsDevelopInfoIndex),
                      reinterpret_cast<void**>(&g_OrigInfoIndex), g_TargetInfoIndex,
                      "GetDWalkerPartsDevelopInfoIndex");
    if (!g_TargetCurrentLevel)
        ok &= HookOne(gAddr.EquipDevelopControllerImpl_GetDWalkerPartsDevelopedCurrentLevel,
                      kCurrentLevelEntry, sizeof(kCurrentLevelEntry), -1, -1,
                      reinterpret_cast<void*>(&hkGetDWalkerPartsDevelopedCurrentLevel),
                      reinterpret_cast<void**>(&g_OrigCurrentLevel), g_TargetCurrentLevel,
                      "GetDWalkerPartsDevelopedCurrentLevel");
    if (!g_TargetId)
        ok &= HookOne(gAddr.EquipDevelopControllerImpl_GetDWalkerPartsId,
                      kRowFieldEntry, sizeof(kRowFieldEntry), -1, kRowType,
                      reinterpret_cast<void*>(&hkGetDWalkerPartsId),
                      reinterpret_cast<void**>(&g_OrigId), g_TargetId,
                      "GetDWalkerPartsId");
    if (!g_TargetLevel)
        ok &= HookOne(gAddr.EquipDevelopControllerImpl_GetDWalkerPartsLevel,
                      kRowFieldEntry, sizeof(kRowFieldEntry), -1, kRowLevel,
                      reinterpret_cast<void*>(&hkGetDWalkerPartsLevel),
                      reinterpret_cast<void**>(&g_OrigLevel), g_TargetLevel,
                      "GetDWalkerPartsLevel");
    if (!g_TargetType)
        ok &= HookOne(gAddr.EquipDevelopControllerImpl_GetDWalkerPartsType,
                      kRowFieldEntry, sizeof(kRowFieldEntry), -1, kRowSlot,
                      reinterpret_cast<void*>(&hkGetDWalkerPartsType),
                      reinterpret_cast<void**>(&g_OrigType), g_TargetType,
                      "GetDWalkerPartsType");
    return ok;
}

bool Uninstall_EquipDevelopControllerImpl_GetDWalkerParts_Hooks()
{
    for (void** t : { &g_TargetType, &g_TargetLevel, &g_TargetId, &g_TargetCurrentLevel, &g_TargetInfoIndex })
    {
        if (*t)
        {
            DisableAndRemoveHook(*t);
            *t = nullptr;
        }
    }
    g_OrigInfoIndex = nullptr;
    g_OrigCurrentLevel = nullptr;
    g_OrigId = nullptr;
    g_OrigLevel = nullptr;
    g_OrigType = nullptr;
    return true;
}
