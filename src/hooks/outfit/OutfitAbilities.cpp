#include "pch.h"

#include "OutfitAbilities.h"
#include "OutfitRegistry.h"
#include "BlockControllerImpl_LoadPartsNew.h"
#include "SkillAndItemParameterSystemImpl_GetSuitParam.h"
#include "CustomHeadRegistry.h"
#include "ShadowState.h"
#include "AdditionalMotionTable_GetMtarPathId.h"
#include "MissionCodeGuard.h"

#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "LuaBroadcaster.h"

namespace
{
    constexpr std::uint8_t  kConditionBit_InfiniteAmmo = 0x04;
    constexpr std::uint32_t kSuitTableRows             = 149;
    constexpr std::uint32_t kSuitTableRowStride        = 12;
    constexpr std::uint32_t kCamo_SneakingSuitTpp      = 15;
    constexpr std::uint32_t kCamo_BattleDress          = 16;
    constexpr std::uint8_t  kMaxAbilityLevel           = 9;

    constexpr std::size_t kHolderOff_CalcDamage   = 0x8;
    constexpr std::size_t kHolderOff_LifeRecovery = 0x28;
    constexpr std::size_t kHolder_WornSuitArr     = 0x70;
    constexpr std::size_t kHolder_SlotCount       = 0x208;
    constexpr std::size_t kSeParam_PartsTypeOff   = 0x13;
    constexpr std::size_t kDmgCtl_Owner           = 0x28;
    constexpr std::size_t kDmgOwner_PlayerMgr     = 0x138;
    constexpr std::size_t kPlayerMgr_Parts        = 0x60;
    constexpr std::size_t kPlayerMgr_LocalSlot    = 0x214;
    constexpr std::size_t kParts_CamoArr          = 0x50;
    constexpr std::uint8_t kCamoByte_BattleDress  = 0x10;
    constexpr std::uint8_t kCamoByte_Plain        = 0x00;
    constexpr std::uint16_t kWornSuitNone         = 0xFFFF;
    constexpr std::size_t kMaxSlots = outfit::shadow::kMaxSlots;

    static std::atomic<bool> g_DonorSwapFaulted{ false };

    constexpr std::uint8_t kPrologue_CalcDamage[] =
        { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20, 0x89, 0x50, 0x10, 0x55 };
    constexpr std::uint8_t kPrologue_UpdateLife[] =
        { 0x40, 0x53, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
          0x48, 0x81, 0xEC, 0xF8, 0x00, 0x00, 0x00 };
    constexpr std::uint8_t kPrologue_RattleBody[] =
        { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
          0x48, 0x89, 0x7C, 0x24, 0x18 };
    constexpr std::uint8_t kPrologue_RattleThunkEn153[] =
        { 0xE9, 0x4B, 0x54, 0x80, 0x05 };
    constexpr std::uint8_t kPrologue_RattleThunkJp153[] =
        { 0xE9, 0xDB, 0x55, 0x4A, 0x07 };
    constexpr std::uint8_t kPrologue_FootStepBody[] =
        { 0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41,
          0x57, 0x48, 0x8D, 0xAC, 0x24, 0xC0, 0xFE, 0xFF, 0xFF,
          0x48, 0x81, 0xEC, 0x40, 0x02, 0x00, 0x00 };
    constexpr std::uint8_t kPrologue_DamageEffect[] =
        { 0x40, 0x55, 0x53, 0x57, 0x41, 0x54, 0x41, 0x55,
          0x48, 0x8D, 0xAC, 0x24, 0x00, 0xFD, 0xFF, 0xFF,
          0x48, 0x81, 0xEC, 0x00, 0x04, 0x00, 0x00 };

    struct HookCandidate
    {
        std::uintptr_t     addr;
        const std::uint8_t* expect;
        std::size_t        expectLen;
    };

    struct CandidateList
    {
        const HookCandidate* items;
        std::size_t          count;
    };

    template <std::size_t N>
    constexpr CandidateList ListOf(const HookCandidate (&items)[N])
    {
        return { items, N };
    }

    constexpr HookCandidate kCalcDamage_En154[] =
    {
        { 0x1411f5350ull, kPrologue_CalcDamage, sizeof(kPrologue_CalcDamage) },
    };
    constexpr HookCandidate kCalcDamage_Jp154[] =
    {
        { 0x1411f5380ull, kPrologue_CalcDamage, sizeof(kPrologue_CalcDamage) },
    };
    constexpr HookCandidate kCalcDamage_En153[] =
    {
        { 0x1411f5dc0ull, kPrologue_CalcDamage, sizeof(kPrologue_CalcDamage) },
    };
    constexpr HookCandidate kCalcDamage_Jp153[] =
    {
        { 0x1411f5ed0ull, kPrologue_CalcDamage, sizeof(kPrologue_CalcDamage) },
    };

    constexpr HookCandidate kUpdateLife_En154[] =
    {
        { 0x1411f92d0ull, kPrologue_UpdateLife, sizeof(kPrologue_UpdateLife) },
    };
    constexpr HookCandidate kUpdateLife_Jp154[] =
    {
        { 0x1411f9300ull, kPrologue_UpdateLife, sizeof(kPrologue_UpdateLife) },
    };
    constexpr HookCandidate kUpdateLife_En153[] =
    {
        { 0x1411f9d40ull, kPrologue_UpdateLife, sizeof(kPrologue_UpdateLife) },
    };
    constexpr HookCandidate kUpdateLife_Jp153[] =
    {
        { 0x1411f9e50ull, kPrologue_UpdateLife, sizeof(kPrologue_UpdateLife) },
    };

    constexpr HookCandidate kRattle_En154[] =
    {
        { 0x140984c30ull, kPrologue_RattleBody, sizeof(kPrologue_RattleBody) },
    };
    constexpr HookCandidate kRattle_Jp154[] =
    {
        { 0x140984b70ull, kPrologue_RattleBody, sizeof(kPrologue_RattleBody) },
    };
    constexpr HookCandidate kRattle_En153[] =
    {
        { 0x1461891f0ull, kPrologue_RattleBody, sizeof(kPrologue_RattleBody) },
        { 0x140983da0ull, kPrologue_RattleThunkEn153,
          sizeof(kPrologue_RattleThunkEn153) },
    };
    constexpr HookCandidate kRattle_Jp153[] =
    {
        { 0x147e28e70ull, kPrologue_RattleBody, sizeof(kPrologue_RattleBody) },
        { 0x140983890ull, kPrologue_RattleThunkJp153,
          sizeof(kPrologue_RattleThunkJp153) },
    };

    constexpr HookCandidate kFootStep_En154[] =
    {
        { 0x140984010ull, kPrologue_FootStepBody,
          sizeof(kPrologue_FootStepBody) },
    };
    constexpr HookCandidate kFootStep_Jp154[] =
    {
        { 0x140983f50ull, kPrologue_FootStepBody,
          sizeof(kPrologue_FootStepBody) },
    };
    constexpr HookCandidate kFootStep_En153[] =
    {
        { 0x140983180ull, kPrologue_FootStepBody,
          sizeof(kPrologue_FootStepBody) },
    };
    constexpr HookCandidate kFootStep_Jp153[] =
    {
        { 0x140982c70ull, kPrologue_FootStepBody,
          sizeof(kPrologue_FootStepBody) },
    };

    constexpr HookCandidate kDamageEffect_En154[] =
    {
        { 0x141192840ull, kPrologue_DamageEffect,
          sizeof(kPrologue_DamageEffect) },
    };
    constexpr HookCandidate kDamageEffect_Jp154[] =
    {
        { 0x1411928c0ull, kPrologue_DamageEffect,
          sizeof(kPrologue_DamageEffect) },
    };
    constexpr HookCandidate kDamageEffect_En153[] =
    {
        { 0x141193070ull, kPrologue_DamageEffect,
          sizeof(kPrologue_DamageEffect) },
    };
    constexpr HookCandidate kDamageEffect_Jp153[] =
    {
        { 0x141193120ull, kPrologue_DamageEffect,
          sizeof(kPrologue_DamageEffect) },
    };

    struct AbilitySites
    {
        CandidateList  calcDamage;
        CandidateList  updateLife;
        CandidateList  rattle;
        CandidateList  footStep;
        CandidateList  damageEffect;
        std::uintptr_t suitParamTable;
        std::uintptr_t scriptConditionBits;
    };

    constexpr AbilitySites kSites_En154 =
    {
        ListOf(kCalcDamage_En154), ListOf(kUpdateLife_En154),
        ListOf(kRattle_En154), ListOf(kFootStep_En154),
        ListOf(kDamageEffect_En154), 0x1423997e0ull, 0x142c1c2aaull
    };
    constexpr AbilitySites kSites_Jp154 =
    {
        ListOf(kCalcDamage_Jp154), ListOf(kUpdateLife_Jp154),
        ListOf(kRattle_Jp154), ListOf(kFootStep_Jp154),
        ListOf(kDamageEffect_Jp154), 0x142399af0ull, 0x142c1c2aaull
    };
    constexpr AbilitySites kSites_En153 =
    {
        ListOf(kCalcDamage_En153), ListOf(kUpdateLife_En153),
        ListOf(kRattle_En153), ListOf(kFootStep_En153),
        ListOf(kDamageEffect_En153), 0x142399940ull, 0x142c1c1baull
    };
    constexpr AbilitySites kSites_Jp153 =
    {
        ListOf(kCalcDamage_Jp153), ListOf(kUpdateLife_Jp153),
        ListOf(kRattle_Jp153), ListOf(kFootStep_Jp153),
        ListOf(kDamageEffect_Jp153), 0x142399970ull, 0x142c1c1baull
    };

    static const AbilitySites* SitesForBuild()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return &kSites_En154;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:  return &kSites_Jp154;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:  return &kSites_En153;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  return &kSites_Jp153;
        default:                                           return nullptr;
        }
    }

    static std::uintptr_t SuitParamTableAddr()
    {
        const AbilitySites* sites = SitesForBuild();
        if (!sites)
            return 0;
        return reinterpret_cast<std::uintptr_t>(
            ResolveGameAddress(sites->suitParamTable));
    }

    static std::uintptr_t ScriptConditionBitsAddr()
    {
        const AbilitySites* sites = SitesForBuild();
        if (!sites)
            return 0;
        return reinterpret_cast<std::uintptr_t>(
            ResolveGameAddress(sites->scriptConditionBits));
    }

    using CalcDamage_t = float (__fastcall*)(
        void*, std::uint32_t, void*, void*, void*, void*, void*, void*, void*);
    using UpdateLife_t = void (__fastcall*)(void*);
    using ConvertRattle_t = void* (__fastcall*)(
        void*, void*, std::uint64_t, void*, void*, void*, std::uint64_t, void*,
        void*);
    using ConvertFootStep_t = void (__fastcall*)(
        void*, void*, std::uint64_t, void*, void**, void**, std::uint32_t,
        void*, void*, void*);

    static CalcDamage_t    g_OrigCalcDamage    = nullptr;
    static UpdateLife_t    g_OrigUpdateLife    = nullptr;
    static ConvertRattle_t   g_OrigConvertRattle   = nullptr;
    static ConvertFootStep_t g_OrigConvertFootStep = nullptr;
    static bool g_InstalledCalcDamage = false;
    static bool g_InstalledUpdateLife = false;
    static bool g_InstalledRattle     = false;
    static bool g_InstalledFootStep   = false;

    using CallDamageEffect_t = bool (__fastcall*)(
        void*, std::uint32_t, void*, std::uint8_t, bool, bool);
    static CallDamageEffect_t g_OrigCallDamageEffect = nullptr;
    static bool g_InstalledDamageEffect = false;

    static std::atomic<bool> g_AmmoBitOwned{ false };

    struct DonorCacheEntry
    {
        std::uint16_t equipId = 0;
        std::uint8_t  level   = 0;
        std::uint8_t  state   = 0;
    };
    static DonorCacheEntry g_BdDonor[kMaxAbilityLevel + 1];
    static DonorCacheEntry g_SnkDonor[kMaxAbilityLevel + 1];
    static std::atomic<DWORD> g_DonorRetryTick{ 0 };

    static bool ReadSuitRow_SEH(std::uint32_t idx, std::uint16_t* outId,
                                std::uint32_t* outCamo, std::uint8_t* outLevel)
    {
        __try
        {
            const std::uint8_t* row = reinterpret_cast<const std::uint8_t*>(
                SuitParamTableAddr()) + idx * kSuitTableRowStride;
            *outId    = *reinterpret_cast<const std::uint16_t*>(row);
            *outCamo  = *reinterpret_cast<const std::uint32_t*>(row + 4);
            *outLevel = row[8];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static std::uint16_t ScanSuitTableForDonor(std::uint32_t camo,
                                               std::uint8_t wantLevel,
                                               std::uint8_t* outLevel)
    {
        std::uint16_t minId = 0xFFFF;
        for (std::uint32_t i = 0; i < kSuitTableRows; ++i)
        {
            std::uint16_t id  = 0;
            std::uint32_t c   = 0;
            std::uint8_t  lvl = 0;
            if (!ReadSuitRow_SEH(i, &id, &c, &lvl))
                return 0;
            if (id == 0 || id == kWornSuitNone || c > 130 || lvl > 10)
                continue;
            if (id < minId)
                minId = id;
        }
        if (minId == 0xFFFF)
        {
            static std::atomic<bool> s_warnedEmpty{ false };
            if (!s_warnedEmpty.exchange(true))
                LogWarn("OutfitAbilities: suit table @ 0x%llX has no sane rows, "
                    "defense/lifeRecovery donors off",
                    static_cast<unsigned long long>(SuitParamTableAddr()));
            return 0;
        }

        std::uint16_t bestLoId = 0;
        std::uint8_t  bestLo   = 0;
        std::uint16_t bestHiId = 0;
        std::uint8_t  bestHi   = 0xFF;
        std::uint32_t valid    = 0;
        for (std::uint32_t i = 0; i < kSuitTableRows; ++i)
        {
            std::uint16_t id  = 0;
            std::uint32_t c   = 0;
            std::uint8_t  lvl = 0;
            if (!ReadSuitRow_SEH(i, &id, &c, &lvl))
                return 0;
            if (id < minId || static_cast<std::uint32_t>(id - minId) >= 2048
                || c > 130 || lvl > 10)
                continue;
            ++valid;
            if (c != camo)
                continue;
            if (lvl <= wantLevel)
            {
                if (bestLoId == 0 || lvl > bestLo)
                {
                    bestLo   = lvl;
                    bestLoId = id;
                }
            }
            else if (lvl < bestHi)
            {
                bestHi   = lvl;
                bestHiId = id;
            }
        }
        if (valid < 100)
        {
            static std::atomic<bool> s_warned{ false };
            if (!s_warned.exchange(true))
                LogWarn("OutfitAbilities: suit table @ 0x%llX rejected (%u of %u "
                    "rows sane), defense/lifeRecovery donors off",
                    static_cast<unsigned long long>(SuitParamTableAddr()),
                    valid, kSuitTableRows);
            return 0;
        }
        if (bestLoId != 0)
        {
            if (outLevel) *outLevel = bestLo;
            return bestLoId;
        }
        if (outLevel) *outLevel = bestHi;
        return bestHiId;
    }

    constexpr std::size_t  kConvertInfoOff         = 0x08;
    constexpr std::size_t  kClothRowTableOff       = 0x7A0;

    static bool ReadClothRowTable_SEH(void* self, std::uint32_t* out4)
    {
        __try
        {
            if (!self) return false;
            void* ci = *reinterpret_cast<void**>(
                reinterpret_cast<char*>(self) + kConvertInfoOff);
            if (!ci) return false;
            const std::uint32_t* t = reinterpret_cast<const std::uint32_t*>(
                reinterpret_cast<char*>(ci) + kClothRowTableOff);
            for (int i = 0; i < 4; ++i) out4[i] = t[i];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool ReadOutParamPP_SEH(void* pp, std::uint32_t* out)
    {
        __try
        {
            if (!pp) return false;
            std::uint32_t* inner = *reinterpret_cast<std::uint32_t**>(pp);
            if (!inner) return false;
            *out = inner[0];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool WriteOutParamPP_SEH(void* pp, std::uint32_t value)
    {
        __try
        {
            if (!pp) return false;
            std::uint32_t* inner = *reinterpret_cast<std::uint32_t**>(pp);
            if (!inner) return false;
            inner[0] = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool VextActiveVariantAbilities(std::uint8_t parts,
                                           std::uint8_t playerType,
                                           std::uint8_t selector,
                                           outfit::DeclaredAbilities* out)
    {
        std::uint8_t vextParts = 0xFF;
        std::uint8_t variantIdx = 0;
        if (!outfit::TryGetVanillaExtByVariantSelector(selector, &vextParts,
                                                       &variantIdx))
            return false;
        if (vextParts != parts)
            return false;
        return outfit::VanillaExtGetVariantAbilities(parts, playerType,
                                                     variantIdx, out);
    }

    static std::uint32_t ResolveClothSwitch(std::uint8_t seParts,
                                            std::uint8_t* outParts)
    {
        std::uint8_t parts = seParts;
        if (parts < outfit::kCustomPartsTypeStart
            || parts > outfit::kCustomPartsTypeEnd)
            parts = outfit::ReadLivePartsType();
        if (outParts) *outParts = parts;

        const std::uint8_t livePT = outfit::ReadLivePlayerType();
        std::uint32_t want = outfit::kSoundSwitchUnset;

        if (parts >= outfit::kCustomPartsTypeStart
            && parts <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* e = nullptr;
            if (outfit::TryGetOutfitByPartsType(parts, &e) && e)
            {
                const outfit::OutfitPlayerTypeData* d = e->GetPTData(livePT);
                if (d) want = d->abilityRattleSuit;
            }
            outfit::DeclaredAbilities rva{};
            if (outfit::TryGetVariantAbilities(
                    parts, livePT,
                    outfit::GetActiveVariantLockFree(parts), &rva)
                && rva.rattleSuit != outfit::kSoundSwitchUnset)
                want = rva.rattleSuit;
        }
        else if (outfit::VanillaExtHasAnyAbilities())
        {
            const std::uint8_t sel = outfit::ReadLiveSelectorCode();
            outfit::VanillaSuitAbilities va{};
            if (outfit::VanillaExtGetSuitAbilities(parts, livePT, sel, &va))
                want = va.rattleSuit;
            outfit::DeclaredAbilities vva{};
            if (VextActiveVariantAbilities(parts, livePT, sel, &vva)
                && vva.rattleSuit != outfit::kSoundSwitchUnset)
                want = vva.rattleSuit;
        }
        return want;
    }

    static bool ReadQwordAt_SEH(void* p, std::uint64_t* out)
    {
        __try
        {
            if (!p) return false;
            *out = *reinterpret_cast<std::uint64_t*>(p);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static std::uint16_t GetDonorEquipId(bool battleDress, std::uint8_t level)
    {
        if (level == 0) return 0;

        if (SuitParamTableAddr() == 0)
        {
            static std::atomic<bool> s_unportedLogged{ false };
            if (!s_unportedLogged.exchange(true))
                LogError("OutfitAbilities: suit-donor table address missing for "
                    "this build, defense/lifeRecovery donors off");
            return 0;
        }

        if (level > kMaxAbilityLevel) level = kMaxAbilityLevel;
        DonorCacheEntry& e = battleDress ? g_BdDonor[level] : g_SnkDonor[level];
        if (e.state == 1) return e.equipId;
        if (e.state == 2)
        {
            const DWORD last = g_DonorRetryTick.load(std::memory_order_relaxed);
            if (last != 0 && (GetTickCount() - last) < 2000)
                return 0;
        }
        std::uint8_t served = 0;
        const std::uint16_t id = ScanSuitTableForDonor(
            battleDress ? kCamo_BattleDress : kCamo_SneakingSuitTpp, level,
            &served);
        if (id == 0)
        {
            e.state = 2;
            g_DonorRetryTick.store(GetTickCount(), std::memory_order_relaxed);
            return 0;
        }
        e.equipId = id;
        e.level   = served;
        e.state   = 1;
        return id;
    }

    struct SlotAbilities
    {
        bool         ok      = false;
        bool         silent  = false;
        std::uint32_t sprint = outfit::kSprintNone;
        std::uint64_t spark  = 0;
        std::uint8_t defense = 0;
        std::uint8_t regen   = 0;
    };

    static void ReportAbilityLookupMiss(std::size_t slot, std::uint8_t parts,
                                        std::uint8_t pt, const char* why)
    {
        static std::atomic<int> s_logged{ 0 };
        if (s_logged.fetch_add(1, std::memory_order_relaxed) >= 8) return;
        LogWarn("OutfitAbilities: slot %zu partsType 0x%02X playerType %u: %s, "
            "abilities off",
            slot, static_cast<unsigned>(parts), static_cast<unsigned>(pt), why);
    }

    static SlotAbilities GetSlotAbilities(std::size_t slot)
    {
        SlotAbilities r{};
        outfit::shadow::Slot s;
        const bool haveShadow = outfit::shadow::Get(slot, &s);

        if (haveShadow
            && s.realPartsType >= outfit::kCustomPartsTypeStart
            && s.realPartsType <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* e = nullptr;
            if (!outfit::TryGetOutfitByPartsType(s.realPartsType, &e) || !e)
            {
                ReportAbilityLookupMiss(slot, s.realPartsType, s.realPlayerType,
                                        "no registered outfit");
                return r;
            }
            const outfit::OutfitPlayerTypeData* d = e->GetPTData(s.realPlayerType);
            if (!d)
            {
                ReportAbilityLookupMiss(slot, s.realPartsType, s.realPlayerType,
                                        "outfit has no branch for it");
                return r;
            }
            r.ok      = true;
            r.silent  = d->abilitySilentSteps;
            r.sprint  = d->abilitySprint;
            r.spark   = d->abilitySprintSpark;
            r.defense = d->abilityDefense;
            r.regen   = d->abilityLifeRecovery;

            outfit::DeclaredAbilities va{};
            if (outfit::TryGetVariantAbilities(s.realPartsType, s.realPlayerType,
                                               s.variantIdx, &va))
            {
                r.silent  = va.silentSteps;
                r.sprint  = va.sprint;
                r.spark   = va.sprintSpark;
                r.defense = va.defense;
                r.regen   = va.lifeRecovery;
            }
            return r;
        }

        if (!outfit::VanillaExtHasAnyAbilities())
            return r;

        const std::uint8_t parts = haveShadow ? s.realPartsType
                                              : outfit::ReadLivePartsType();
        const std::uint8_t pt    = haveShadow ? s.realPlayerType
                                              : outfit::ReadLivePlayerType();
        const std::uint8_t camo  = haveShadow ? s.realCamoType
                                              : outfit::ReadLiveSelectorCode();

        outfit::VanillaSuitAbilities va{};
        if (outfit::VanillaExtGetSuitAbilities(parts, pt, camo, &va))
        {
            r.ok      = true;
            r.silent  = va.silentSteps;
            r.sprint  = va.sprint;
            r.spark   = va.sprintSpark;
            r.defense = va.defense;
            r.regen   = va.lifeRecovery;
        }

        outfit::DeclaredAbilities vva{};
        if (VextActiveVariantAbilities(parts, pt, camo, &vva))
        {
            r.ok      = true;
            r.silent  = vva.silentSteps;
            r.sprint  = vva.sprint;
            r.spark   = vva.sprintSpark;
            r.defense = vva.defense;
            r.regen   = vva.lifeRecovery;
        }
        return r;
    }

    static std::uint16_t* GetWornArr_SEH(void* self, std::size_t holderOff,
                                         std::uint32_t* outCount)
    {
        __try
        {
            auto* holder = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(self) + holderOff);
            if (!holder)
                return nullptr;
            auto* arr = *reinterpret_cast<std::uint16_t**>(
                holder + kHolder_WornSuitArr);
            if (outCount)
                *outCount = *reinterpret_cast<std::uint32_t*>(
                    holder + kHolder_SlotCount);
            return arr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static bool ReadWorn_SEH(std::uint16_t* arr, std::size_t slot,
                             std::uint16_t* out)
    {
        __try
        {
            *out = arr[slot];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool WriteWorn_SEH(std::uint16_t* arr, std::size_t slot,
                              std::uint16_t value)
    {
        __try
        {
            arr[slot] = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static std::uint8_t ReadSeByte_SEH(void* p, std::size_t off,
                                       bool* okOut)
    {
        __try
        {
            const std::uint8_t v =
                reinterpret_cast<const std::uint8_t*>(p)[off];
            *okOut = true;
            return v;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *okOut = false;
            return 0;
        }
    }

    static bool WriteSeByte_SEH(void* p, std::size_t off, std::uint8_t v)
    {
        __try
        {
            reinterpret_cast<std::uint8_t*>(p)[off] = v;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool RmwAmmoBit_SEH(bool set)
    {
        const std::uintptr_t bitsAddr = ScriptConditionBitsAddr();
        if (bitsAddr == 0)
        {
            static std::atomic<bool> s_warned{ false };
            if (!s_warned.exchange(true, std::memory_order_relaxed))
                LogError("OutfitAbilities: script-condition byte address missing "
                    "for this build, infiniteAmmo off");
            return false;
        }

        __try
        {
            auto* p = reinterpret_cast<std::uint8_t*>(bitsAddr);
            const std::uint8_t cur = *p;
            if (set)
            {
                if ((cur & kConditionBit_InfiniteAmmo) == 0)
                    *p = cur | kConditionBit_InfiniteAmmo;
            }
            else
            {
                if ((cur & kConditionBit_InfiniteAmmo) != 0)
                    *p = cur & static_cast<std::uint8_t>(~kConditionBit_InfiniteAmmo);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void SyncInfiniteAmmoBit(bool forceOff)
    {
        bool desired = false;
        if (!forceOff)
        {
            const std::uint16_t worn = outfit::ReadLiveWornHeadCategory();
            const std::uint8_t slot = (worn != 0xFFFFu)
                ? static_cast<std::uint8_t>(worn & 0xFFu) : std::uint8_t{0};
            if (slot >= outfit::kCustomHeadSlotBase
                && outfit::GetRealizedLocalHeadSlot() == slot)
            {
                if (const outfit::CustomHeadEntry* h =
                        outfit::TryGetCustomHeadBySlot(slot);
                    h && outfit::GetCustomHeadInfiniteAmmo(h->name))
                    desired = outfit::IsHeadOptionOfferedByLiveSuit(h->equipId);
            }
        }
        if (desired)
        {
            if (RmwAmmoBit_SEH(true))
                g_AmmoBitOwned.store(true, std::memory_order_relaxed);
        }
        else if (g_AmmoBitOwned.exchange(false, std::memory_order_relaxed))
        {
            RmwAmmoBit_SEH(false);
        }
    }

    using GetLocalPartsTypeFn  = std::uint16_t (__fastcall*)();

    constexpr unsigned      kEnginePartsNone  = 0x00FFu;
    constexpr std::uint32_t kIdentityNone     = 0xFFFFFFFFu;

    static std::atomic<std::uint32_t> g_LastIdentity{ kIdentityNone };
    static std::atomic<bool>          g_AwaitingSettle{ false };

    static unsigned ReadEnginePartsType()
    {
        auto fn = reinterpret_cast<GetLocalPartsTypeFn>(
            ResolveGameAddress(gAddr.PlayerInfo_GetLocalPartsType));
        if (!fn) return kEnginePartsNone;
        __try { return fn(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return kEnginePartsNone; }
    }

    static bool EngineIdentitySettled(std::uint8_t parts)
    {
        auto getEngineParts = reinterpret_cast<GetLocalPartsTypeFn>(
            ResolveGameAddress(gAddr.PlayerInfo_GetLocalPartsType));
        if (!getEngineParts)
            return true;

        unsigned engineParts = kEnginePartsNone;
        __try
        {
            engineParts = getEngineParts();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
        return engineParts == parts;
    }

    static void EmitPartsTypeChange()
    {
        const std::uint8_t parts = outfit::ReadLivePartsType();
        const std::uint8_t sel   = outfit::ReadLiveSelectorCode();
        const std::uint8_t pt    = outfit::ReadLivePlayerType();
        if (parts == 0xFF)
            return;

        const std::uint32_t key =
            static_cast<std::uint32_t>(parts)
            | (static_cast<std::uint32_t>(sel) << 8)
            | (static_cast<std::uint32_t>(pt) << 16);

        const bool changed =
            g_LastIdentity.load(std::memory_order_relaxed) != key;

        bool settleRetake = false;
        if (!changed && g_AwaitingSettle.load(std::memory_order_relaxed))
            settleRetake = ReadEnginePartsType() == parts;

        if (!changed && !settleRetake)
            return;

        g_LastIdentity.store(key, std::memory_order_relaxed);

        g_DonorSwapFaulted.store(false);

        const bool identitySettled = EngineIdentitySettled(parts);
        g_AwaitingSettle.store(!identitySettled, std::memory_order_relaxed);

        outfit::NoteLiveOutfitIdentity(parts, identitySettled);

        if (!changed)
            return;

        V_FrameWork::EmitMessage("Player", "partsTypeChange",
            pt, parts, sel);
    }

    static int DonorSehAvOnly(unsigned int code)
    {
        return code == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
    }

    static bool CallOrigUpdateLife_SEH(void* self)
    {
        __try
        {
            g_OrigUpdateLife(self);
            return true;
        }
        __except (DonorSehAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    static void SyncQuietStepPatch();

    static void __fastcall hkUpdateLifeRecoverySpeed(void* self)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
        {
            SyncInfiniteAmmoBit(true);
            SyncQuietStepPatch();
            g_OrigUpdateLife(self);
            return;
        }

        SyncInfiniteAmmoBit(false);
        EmitPartsTypeChange();
        SyncQuietStepPatch();

        std::uint32_t count = 0;
        std::uint16_t* arr = GetWornArr_SEH(self, kHolderOff_LifeRecovery, &count);
        if (!arr)
        {
            g_OrigUpdateLife(self);
            return;
        }
        if (count > kMaxSlots) count = kMaxSlots;

        const bool allowDonor =
            !g_DonorSwapFaulted.load(std::memory_order_relaxed)
            && !outfit::NativeSuitParamsActive();

        std::uint16_t saved[kMaxSlots] = {};
        bool swapped[kMaxSlots] = {};
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const SlotAbilities ab = GetSlotAbilities(i);
            if (!allowDonor || !ab.ok || ab.regen == 0)
                continue;
            const std::uint16_t donor = GetDonorEquipId(false, ab.regen);
            if (donor == 0)
                continue;
            std::uint16_t cur = 0;
            if (!ReadWorn_SEH(arr, i, &cur))
                continue;
            if (cur != kWornSuitNone && cur != 0)
                continue;
            if (WriteWorn_SEH(arr, i, donor))
            {
                saved[i]   = cur;
                swapped[i] = true;
            }
        }

        const bool origOk = CallOrigUpdateLife_SEH(self);

        for (std::uint32_t i = 0; i < count; ++i)
            if (swapped[i])
                WriteWorn_SEH(arr, i, saved[i]);

        if (!origOk && !g_DonorSwapFaulted.exchange(true))
            LogWarn("OutfitAbilities: UpdateLifeRecoverySpeed faulted with a "
                "donor swapped in, donors parked until the next outfit change");
    }

    static float __fastcall hkCalcDamageValueAtIndex(
        void* self, std::uint32_t slotIndex, void* a3, void* a4, void* a5,
        void* a6, void* a7, void* a8, void* a9)
    {
        std::uint16_t* arr = nullptr;
        std::uint16_t saved = 0;
        bool swapped = false;

        if (!MissionCodeGuard::ShouldBypassHooks() && slotIndex < kMaxSlots
            && !g_DonorSwapFaulted.load(std::memory_order_relaxed)
            && !outfit::NativeSuitParamsActive())
        {
            const SlotAbilities ab = GetSlotAbilities(slotIndex);
            if (ab.ok && ab.defense != 0)
            {
                const std::uint16_t donor = GetDonorEquipId(true, ab.defense);
                if (donor != 0)
                {
                    arr = GetWornArr_SEH(self, kHolderOff_CalcDamage, nullptr);
                    std::uint16_t cur = 0;
                    if (arr && ReadWorn_SEH(arr, slotIndex, &cur)
                        && cur != donor
                        && (cur == kWornSuitNone || cur == 0)
                        && WriteWorn_SEH(arr, slotIndex, donor))
                    {
                        saved   = cur;
                        swapped = true;
                    }
                }
            }
        }

        const float r = g_OrigCalcDamage(
            self, slotIndex, a3, a4, a5, a6, a7, a8, a9);

        if (swapped)
            WriteWorn_SEH(arr, slotIndex, saved);
        return r;
    }

    static std::uint32_t ResolveDamageSeClass()
    {
        const std::uint8_t parts  = outfit::ReadLivePartsType();
        const std::uint8_t livePT = outfit::ReadLivePlayerType();
        std::uint32_t want = outfit::kSoundSwitchUnset;

        if (parts >= outfit::kCustomPartsTypeStart
            && parts <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* e = nullptr;
            if (outfit::TryGetOutfitByPartsType(parts, &e) && e)
            {
                const outfit::OutfitPlayerTypeData* d = e->GetPTData(livePT);
                if (d) want = d->abilityDamageSe;
            }
            outfit::DeclaredAbilities rva{};
            if (outfit::TryGetVariantAbilities(
                    parts, livePT,
                    outfit::GetActiveVariantLockFree(parts), &rva)
                && rva.damageSe != outfit::kSoundSwitchUnset)
                want = rva.damageSe;
        }
        else if (outfit::VanillaExtHasAnyAbilities())
        {
            const std::uint8_t sel = outfit::ReadLiveSelectorCode();
            outfit::VanillaSuitAbilities va{};
            if (outfit::VanillaExtGetSuitAbilities(parts, livePT, sel, &va))
                want = va.damageSe;
            outfit::DeclaredAbilities vva{};
            if (VextActiveVariantAbilities(parts, livePT, sel, &vva)
                && vva.damageSe != outfit::kSoundSwitchUnset)
                want = vva.damageSe;
        }
        return want;
    }

    static bool DamageSeTargetCamo(std::uint32_t cls, std::uint8_t* out)
    {
        if (cls == outfit::kDamageSeBattleDress)
        {
            *out = kCamoByte_BattleDress;
            return true;
        }
        if (cls == outfit::kDamageSeDefault || cls == outfit::kDamageSeNormal)
        {
            *out = kCamoByte_Plain;
            return true;
        }
        static std::atomic<bool> s_warned{ false };
        if (!s_warned.exchange(true, std::memory_order_relaxed))
            LogWarn("OutfitAbilities: damageSe 0x%08X is neither battledress "
                "nor default, vanilla damage sound kept", cls);
        return false;
    }

    static void* LocalPlayerCamoArr_SEH(void* self, std::uint32_t slotIndex)
    {
        __try
        {
            std::uint8_t* ctl = reinterpret_cast<std::uint8_t*>(self);
            if (!ctl) return nullptr;
            std::uint8_t* owner =
                *reinterpret_cast<std::uint8_t**>(ctl + kDmgCtl_Owner);
            if (!owner) return nullptr;
            std::uint8_t* mgr =
                *reinterpret_cast<std::uint8_t**>(owner + kDmgOwner_PlayerMgr);
            if (!mgr) return nullptr;
            if (*reinterpret_cast<std::uint32_t*>(mgr + kPlayerMgr_LocalSlot)
                != slotIndex)
                return nullptr;
            std::uint8_t* parts =
                *reinterpret_cast<std::uint8_t**>(mgr + kPlayerMgr_Parts);
            if (!parts) return nullptr;
            return *reinterpret_cast<std::uint8_t**>(parts + kParts_CamoArr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    static bool __fastcall hkCallDamageEffectWithReaction(
        void* self, std::uint32_t slotIndex, void* damage,
        std::uint8_t param3, bool param4, bool param5)
    {
        void* camoArr = nullptr;
        std::uint8_t saved = 0;
        bool swapped = false;

        if (!MissionCodeGuard::ShouldBypassHooks())
        {
            const std::uint32_t cls = ResolveDamageSeClass();
            std::uint8_t target = 0;
            if (cls != outfit::kSoundSwitchUnset
                && DamageSeTargetCamo(cls, &target))
            {
                camoArr = LocalPlayerCamoArr_SEH(self, slotIndex);
                bool ok = false;
                const std::uint8_t cur =
                    camoArr ? ReadSeByte_SEH(camoArr, slotIndex, &ok)
                            : std::uint8_t{ 0 };
                const bool needed = (target == kCamoByte_BattleDress)
                    ? (cur != kCamoByte_BattleDress)
                    : (cur == kCamoByte_BattleDress);
                if (ok && needed
                    && WriteSeByte_SEH(camoArr, slotIndex, target))
                {
                    saved   = cur;
                    swapped = true;
                }
            }
        }

        const bool r = g_OrigCallDamageEffect(self, slotIndex, damage, param3,
                                              param4, param5);

        if (swapped)
            WriteSeByte_SEH(camoArr, slotIndex, saved);
        return r;
    }

    static void* __fastcall hkConvertRattleSuitPlayer(
        void* self, void* retStorage, std::uint64_t stringId, void* p2,
        void* p3, void* p4, std::uint64_t p5, void* p6, void* seParam)
    {
        void* r = g_OrigConvertRattle(self, retStorage, stringId, p2, p3, p4,
                                      p5, p6, seParam);

        if (MissionCodeGuard::ShouldBypassHooks() || !seParam) return r;

        bool readOk = false;
        const std::uint8_t seParts =
            ReadSeByte_SEH(seParam, kSeParam_PartsTypeOff, &readOk);
        if (!readOk) return r;

        const std::uint32_t want = ResolveClothSwitch(seParts, nullptr);
        if (want == outfit::kSoundSwitchUnset) return r;

        std::uint32_t vanilla = 0;
        const bool okVanilla = ReadOutParamPP_SEH(p4, &vanilla);
        if (okVanilla && vanilla == want) return r;
        if (!WriteOutParamPP_SEH(p4, want)) return r;

        static std::atomic<bool> s_checked{ false };
        std::uint32_t tbl[4] = {};
        if (!s_checked.exchange(true) && ReadClothRowTable_SEH(self, tbl)
            && tbl[0] != want && tbl[1] != want && tbl[2] != want
            && tbl[3] != want)
            LogWarn("OutfitAbilities: rattleSuit 0x%08X not registered by "
                "SetupPlayerType, default cloth foley kept", want);
        return r;
    }

    static void __fastcall hkConvertFootStepPlayer(
        void* self, void* out, std::uint64_t stringId, void* actionOut,
        void** groups, void** values, std::uint32_t flags, void* p8,
        void* seParam, void* p10)
    {
        g_OrigConvertFootStep(self, out, stringId, actionOut, groups, values,
                              flags, p8, seParam, p10);

        if (MissionCodeGuard::ShouldBypassHooks() || !seParam || !values)
            return;

        bool readOk = false;
        const std::uint8_t seParts =
            ReadSeByte_SEH(seParam, kSeParam_PartsTypeOff, &readOk);
        if (!readOk) return;

        const std::uint32_t want = ResolveClothSwitch(seParts, nullptr);
        if (want == outfit::kSoundSwitchUnset) return;

        std::uint32_t vanilla = 0;
        const bool okVanilla = ReadOutParamPP_SEH(values, &vanilla);
        if (okVanilla && vanilla == want) return;
        WriteOutParamPP_SEH(values, want);
    }

    static bool VerifyPrologue(const char* what, std::uintptr_t addr,
                               const std::uint8_t* expect,
                               std::size_t expectLen, bool report)
    {
        std::uint8_t bytes[32] = {};
        if (expectLen > sizeof(bytes))
        {
            if (report)
                LogWarn("OutfitAbilities: %s prologue is %u bytes, over %u, "
                    "candidate skipped",
                    what, static_cast<unsigned>(expectLen),
                    static_cast<unsigned>(sizeof(bytes)));
            return false;
        }
        bool readOk = false;
        __try
        {
            std::memcpy(bytes, reinterpret_cast<const void*>(addr), expectLen);
            readOk = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readOk = false;
        }
        if (!readOk)
        {
            if (report)
                LogWarn("OutfitAbilities: %s @ 0x%llX unreadable, candidate skipped",
                    what, static_cast<unsigned long long>(addr));
            return false;
        }
        if (expect && std::memcmp(bytes, expect, expectLen) != 0)
        {
            if (report)
                LogWarn("OutfitAbilities: %s @ 0x%llX prologue mismatch, "
                    "candidate skipped",
                    what, static_cast<unsigned long long>(addr));
            return false;
        }
        return true;
    }

    static bool ReadBytes_SEH(std::uintptr_t addr, void* out,
                              std::size_t n);

    constexpr std::uint16_t kStepNoiseQuietJzOrig = 0x0274;
    constexpr std::uint16_t kStepNoiseQuietJzNop  = 0x9090;

    constexpr std::uint8_t kStepNoiseQuietPrefix[] =
        { 0x44, 0x38, 0x7C, 0x24, 0x28 };
    constexpr std::uint8_t kStepNoiseQuietSuffix[] = { 0xFF, 0xCE };
    constexpr std::size_t  kStepNoiseQuietWindow   = 9;

    constexpr std::uintptr_t kQuietScanBase  = 0x140900000ull;
    constexpr std::size_t    kQuietScanSize  = 0x100000;
    constexpr std::size_t    kQuietScanChunk = 0x10000;

    static std::atomic<std::uintptr_t> g_QuietCmpAddr{ 0 };

    static bool QuietWindowMatches(std::uintptr_t cmpAddr,
                                   std::uint16_t expectMid)
    {
        std::uint8_t w[kStepNoiseQuietWindow] = {};
        if (!ReadBytes_SEH(cmpAddr, w, sizeof(w)))
            return false;
        if (std::memcmp(w, kStepNoiseQuietPrefix,
                        sizeof(kStepNoiseQuietPrefix)) != 0)
            return false;
        if (std::memcmp(w + 7, kStepNoiseQuietSuffix,
                        sizeof(kStepNoiseQuietSuffix)) != 0)
            return false;
        std::uint16_t mid = 0;
        std::memcpy(&mid, w + 5, sizeof(mid));
        return mid == expectMid;
    }

    static std::uint8_t g_QuietScanBuf[kQuietScanChunk];

    static std::uintptr_t ScanForQuietStepSite()
    {
        std::uintptr_t hit   = 0;
        std::uint32_t  hits  = 0;

        for (std::size_t off = 0; off < kQuietScanSize;
             off += kQuietScanChunk - kStepNoiseQuietWindow)
        {
            const std::size_t want =
                (kQuietScanSize - off < kQuietScanChunk)
                    ? (kQuietScanSize - off) : kQuietScanChunk;
            const std::uintptr_t base = kQuietScanBase + off;
            if (!ReadBytes_SEH(base, g_QuietScanBuf, want))
                continue;
            if (want < kStepNoiseQuietWindow)
                continue;

            for (std::size_t i = 0; i + kStepNoiseQuietWindow <= want; ++i)
            {
                if (g_QuietScanBuf[i]     != 0x44 ||
                    g_QuietScanBuf[i + 5] != 0x74 ||
                    g_QuietScanBuf[i + 6] != 0x02)
                    continue;
                if (std::memcmp(g_QuietScanBuf + i, kStepNoiseQuietPrefix,
                                sizeof(kStepNoiseQuietPrefix)) != 0)
                    continue;
                if (std::memcmp(g_QuietScanBuf + i + 7, kStepNoiseQuietSuffix,
                                sizeof(kStepNoiseQuietSuffix)) != 0)
                    continue;

                const std::uintptr_t found = base + i;
                if (hits == 0)
                {
                    hit  = found;
                    hits = 1;
                }
                else if (found != hit)
                {
                    return 0;
                }
            }
        }
        return hit;
    }

    static std::uintptr_t ResolveQuietStepSite()
    {
        const std::uintptr_t cached =
            g_QuietCmpAddr.load(std::memory_order_relaxed);
        if (cached)
            return cached;

        const std::uintptr_t mapped =
            gAddr.SoundPlayerAnimEvent_StepNoiseQuietCmp;
        if (mapped && QuietWindowMatches(mapped, kStepNoiseQuietJzOrig))
        {
            g_QuietCmpAddr.store(mapped, std::memory_order_relaxed);
            return mapped;
        }

        const std::uintptr_t found = ScanForQuietStepSite();
        if (found)
        {
            g_QuietCmpAddr.store(found, std::memory_order_relaxed);
            LogWarn("OutfitAbilities: StepNoiseQuietCmp %s in address set, "
                "scan found 0x%llX",
                mapped ? "stale" : "missing",
                static_cast<unsigned long long>(found));
        }
        return found;
    }

    static std::atomic<bool> g_QuietStepPatched{ false };
    static std::atomic<bool> g_QuietStepRefused{ false };

    static bool WriteQuietStepWord(std::uintptr_t jzAddr, std::uint16_t want)
    {
        auto* p = reinterpret_cast<std::uint16_t*>(jzAddr);
        DWORD prot = 0;
        if (!VirtualProtect(p, sizeof(*p), PAGE_EXECUTE_READWRITE, &prot))
            return false;
        bool ok = false;
        __try
        {
            *p = want;
            ok = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ok = false;
        }
        DWORD back = 0;
        VirtualProtect(p, sizeof(*p), prot, &back);
        FlushInstructionCache(GetCurrentProcess(), p, sizeof(*p));
        return ok;
    }

    static void SetQuietStepPatch(bool on)
    {
        if (g_QuietStepRefused.load(std::memory_order_relaxed))
            return;
        if (g_QuietStepPatched.load(std::memory_order_relaxed) == on)
            return;

        const std::uintptr_t cmpAddr = ResolveQuietStepSite();
        if (!cmpAddr)
        {
            if (!g_QuietStepRefused.exchange(true, std::memory_order_relaxed))
                LogError("OutfitAbilities: StepNoiseQuietCmp signature not found, "
                    "silentFootsteps off");
            return;
        }

        const std::uintptr_t jzAddr = cmpAddr + 5;
        const std::uint16_t  expect = on ? kStepNoiseQuietJzOrig
                                         : kStepNoiseQuietJzNop;
        if (!QuietWindowMatches(cmpAddr, expect))
        {
            if (!g_QuietStepRefused.exchange(true, std::memory_order_relaxed))
                LogError("OutfitAbilities: StepNoiseQuietCmp @ 0x%llX no longer "
                    "matches 0x%04X, silentFootsteps off",
                    static_cast<unsigned long long>(cmpAddr),
                    static_cast<unsigned>(expect));
            return;
        }

        if (!WriteQuietStepWord(jzAddr, on ? kStepNoiseQuietJzNop
                                           : kStepNoiseQuietJzOrig))
            return;

        g_QuietStepPatched.store(on, std::memory_order_relaxed);
    }

    static std::atomic<std::uint32_t> g_SprintLive{ outfit::kSprintNone };
    static std::atomic<std::uint64_t> g_SprintSparkLive{ 0 };

    static void SyncQuietStepPatch()
    {
        bool          want   = false;
        std::uint32_t sprint = outfit::kSprintNone;
        std::uint64_t spark  = 0;
        if (!MissionCodeGuard::ShouldBypassHooks())
        {
            const SlotAbilities ab =
                GetSlotAbilities(outfit::GetLocalPartsSlot());
            want = ab.ok && ab.silent;
            if (ab.ok)
            {
                sprint = ab.sprint;
                spark  = ab.spark;
            }
        }
        SetQuietStepPatch(want);
        g_SprintLive.store(sprint, std::memory_order_relaxed);
        g_SprintSparkLive.store(spark, std::memory_order_relaxed);
    }

    static bool ReadBytes_SEH(std::uintptr_t addr, void* out, std::size_t n)
    {
        __try
        {
            std::memcpy(out, reinterpret_cast<const void*>(addr), n);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static std::uintptr_t g_ChosenCalcDamage = 0;
    static std::uintptr_t g_ChosenUpdateLife = 0;
    static std::uintptr_t g_ChosenRattle     = 0;
    static std::uintptr_t g_ChosenFootStep   = 0;
    static std::uintptr_t g_ChosenDamageEffect = 0;

    static std::uintptr_t InstallFromCandidates(
        const char* what, CandidateList candidates, void* detour,
        void** original)
    {
        if (candidates.count == 0)
        {
            LogError("OutfitAbilities: %s address missing for this build, hook "
                "skipped", what);
            return 0;
        }
        for (std::size_t i = 0; i < candidates.count; ++i)
        {
            const HookCandidate& c = candidates.items[i];
            const bool last = i + 1 == candidates.count;
            const std::uintptr_t addr =
                reinterpret_cast<std::uintptr_t>(ResolveGameAddress(c.addr));
            if (!addr
                || !VerifyPrologue(what, addr, c.expect, c.expectLen, last))
                continue;
            if (CreateAndEnableHook(reinterpret_cast<void*>(addr), detour,
                                    original))
                return addr;
            if (last)
                LogWarn("OutfitAbilities: %s @ 0x%llX MinHook refused, "
                    "candidate skipped",
                    what, static_cast<unsigned long long>(addr));
        }
        return 0;
    }
}

namespace outfit
{
    void Reset_PartsTypeChangeTracking()
    {
        g_LastIdentity.store(kIdentityNone, std::memory_order_relaxed);
        g_AwaitingSettle.store(false, std::memory_order_relaxed);
    }

    std::uint32_t SprintLive()
    {
        return g_SprintLive.load(std::memory_order_relaxed);
    }

    std::uint64_t SprintSparkLive()
    {
        return g_SprintSparkLive.load(std::memory_order_relaxed);
    }

    bool Install_OutfitAbilities_Hooks()
    {
        const AbilitySites* sites = SitesForBuild();
        if (!sites)
        {
            LogError("OutfitAbilities: no addresses for build %s, outfit "
                "abilities off", GetGameBuildName(gGameBuild));
            return false;
        }

        g_ChosenDamageEffect = InstallFromCandidates(
            "CallDamageEffectWithReaction", sites->damageEffect,
            reinterpret_cast<void*>(&hkCallDamageEffectWithReaction),
            reinterpret_cast<void**>(&g_OrigCallDamageEffect));
        g_InstalledDamageEffect = g_ChosenDamageEffect != 0;
        if (!g_InstalledDamageEffect)
            LogError("OutfitAbilities: CallDamageEffectWithReaction hook failed, "
                "damageSe off");

        g_ChosenCalcDamage = InstallFromCandidates(
            "CalculateDamageValueAtIndex", sites->calcDamage,
            reinterpret_cast<void*>(&hkCalcDamageValueAtIndex),
            reinterpret_cast<void**>(&g_OrigCalcDamage));
        g_InstalledCalcDamage = g_ChosenCalcDamage != 0;
        if (!g_InstalledCalcDamage)
            LogError("OutfitAbilities: CalculateDamageValueAtIndex hook failed, "
                "defense donor off");

        g_ChosenUpdateLife = InstallFromCandidates(
            "UpdateLifeRecoverySpeed", sites->updateLife,
            reinterpret_cast<void*>(&hkUpdateLifeRecoverySpeed),
            reinterpret_cast<void**>(&g_OrigUpdateLife));
        g_InstalledUpdateLife = g_ChosenUpdateLife != 0;
        if (!g_InstalledUpdateLife)
            LogError("OutfitAbilities: UpdateLifeRecoverySpeed hook failed, "
                "silentFootsteps, infiniteAmmo, partsTypeChange and "
                "lifeRecovery off");

        g_ChosenRattle = InstallFromCandidates(
            "ConvertRattleSuit(player)", sites->rattle,
            reinterpret_cast<void*>(&hkConvertRattleSuitPlayer),
            reinterpret_cast<void**>(&g_OrigConvertRattle));
        g_InstalledRattle = g_ChosenRattle != 0;
        if (!g_InstalledRattle)
            LogError("OutfitAbilities: ConvertRattleSuit hook failed, "
                "rattleSuit off");

        g_ChosenFootStep = InstallFromCandidates(
            "ConvertFootStep(player)", sites->footStep,
            reinterpret_cast<void*>(&hkConvertFootStepPlayer),
            reinterpret_cast<void**>(&g_OrigConvertFootStep));
        g_InstalledFootStep = g_ChosenFootStep != 0;
        if (!g_InstalledFootStep)
            LogError("OutfitAbilities: ConvertFootStep hook failed, footsteps "
                "keep vanilla cloth foley");

        return g_InstalledCalcDamage || g_InstalledUpdateLife
            || g_InstalledRattle || g_InstalledFootStep
            || g_InstalledDamageEffect;
    }

    void Uninstall_OutfitAbilities_Hooks()
    {
        if (g_AmmoBitOwned.exchange(false, std::memory_order_relaxed))
            RmwAmmoBit_SEH(false);

        SetQuietStepPatch(false);
        g_SprintLive.store(outfit::kSprintNone, std::memory_order_relaxed);
        g_SprintSparkLive.store(0, std::memory_order_relaxed);

        if (g_InstalledFootStep)
        {
            DisableAndRemoveHook(reinterpret_cast<void*>(g_ChosenFootStep));
            g_OrigConvertFootStep = nullptr;
            g_InstalledFootStep   = false;
            g_ChosenFootStep      = 0;
        }

        if (g_InstalledRattle)
        {
            DisableAndRemoveHook(reinterpret_cast<void*>(g_ChosenRattle));
            g_OrigConvertRattle = nullptr;
            g_InstalledRattle   = false;
            g_ChosenRattle      = 0;
        }
        if (g_InstalledUpdateLife)
        {
            DisableAndRemoveHook(reinterpret_cast<void*>(g_ChosenUpdateLife));
            g_OrigUpdateLife      = nullptr;
            g_InstalledUpdateLife = false;
            g_ChosenUpdateLife    = 0;
        }
        if (g_InstalledDamageEffect)
        {
            DisableAndRemoveHook(
                reinterpret_cast<void*>(g_ChosenDamageEffect));
            g_OrigCallDamageEffect  = nullptr;
            g_InstalledDamageEffect = false;
            g_ChosenDamageEffect    = 0;
        }
        if (g_InstalledCalcDamage)
        {
            DisableAndRemoveHook(reinterpret_cast<void*>(g_ChosenCalcDamage));
            g_OrigCalcDamage      = nullptr;
            g_InstalledCalcDamage = false;
            g_ChosenCalcDamage    = 0;
        }
    }
}
