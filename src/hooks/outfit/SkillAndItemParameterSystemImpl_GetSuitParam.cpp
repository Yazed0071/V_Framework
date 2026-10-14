#include "pch.h"

#include "SkillAndItemParameterSystemImpl_GetSuitParam.h"
#include "OutfitRegistry.h"

#include <cstddef>
#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"

namespace
{
    using GetSuitParam_t = bool (__fastcall*)(void* self, void** out,
                                              std::uint32_t suitKind,
                                              std::uint32_t level);

    static GetSuitParam_t g_OrigGetSuitParam = nullptr;
    static bool           g_Installed        = false;

    constexpr std::uint32_t kOcelotSuitKind = 0x73;
    constexpr std::uint32_t kQuietSuitKind  = 0x74;

    constexpr std::uint32_t kCamo_SneakingSuitTpp = 15;
    constexpr std::uint32_t kCamo_BattleDress     = 16;
    constexpr std::uint32_t kParamId_DamageRate   = 0x12;
    constexpr std::uint32_t kParamId_LifeRecovery = 0x10;
    constexpr std::uint32_t kParamId_Sprint[]     = { 2, 3 };
    constexpr std::size_t   kMaxParamPairs        = 64;

    struct ParamPair
    {
        std::uint32_t index;
        float         value;
    };

    struct ParamArray
    {
        std::uint8_t  flags;
        std::uint8_t  pad0;
        std::uint16_t count;
        std::uint32_t pad1;
        ParamPair*    pairs;
    };
    static_assert(sizeof(ParamArray) == 0x10, "ParamArray must match the engine");
    static_assert(sizeof(ParamPair) == 8, "ParamPair must match the engine");

    static thread_local ParamPair  t_Pairs[kMaxParamPairs];
    static thread_local ParamArray t_Array;

    static bool CopyPairs_SEH(const ParamArray* src, std::size_t* outCount)
    {
        __try
        {
            if (!src || (src->flags & 1u) == 0 || !src->pairs) return false;
            std::size_t n = src->count;
            if (n > kMaxParamPairs) n = kMaxParamPairs;
            for (std::size_t i = 0; i < n; ++i) t_Pairs[i] = src->pairs[i];
            *outCount = n;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool ReadVanillaParam(void* self, std::uint32_t camo,
                                 std::uint8_t level, std::uint32_t paramId,
                                 float* outValue)
    {
        if (level == 0 || !g_OrigGetSuitParam) return false;
        ParamArray* got = nullptr;
        if (!g_OrigGetSuitParam(self, reinterpret_cast<void**>(&got), camo, level)
            || !got)
            return false;
        __try
        {
            if ((got->flags & 1u) == 0 || !got->pairs) return false;
            for (std::uint16_t i = 0; i < got->count; ++i)
                if (got->pairs[i].index == paramId)
                {
                    *outValue = got->pairs[i].value;
                    return true;
                }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        return false;
    }

    static void SetPair(std::size_t* count, std::uint32_t id, float v)
    {
        for (std::size_t i = 0; i < *count; ++i)
            if (t_Pairs[i].index == id) { t_Pairs[i].value = v; return; }
        if (*count >= kMaxParamPairs) return;
        t_Pairs[*count].index = id;
        t_Pairs[*count].value = v;
        ++*count;
    }

    static bool __fastcall hkGetSuitParam(void* self, void** out,
                                          std::uint32_t suitKind,
                                          std::uint32_t level)
    {
        std::uint32_t useKind  = suitKind;
        std::uint32_t useLevel = level;

        if (!MissionCodeGuard::ShouldBypassHooks()
            && suitKind >= outfit::kCustomSelectorStart
            && suitKind <= outfit::kCustomSelectorEnd)
        {
            const std::uint8_t pt       = outfit::ReadLivePlayerType();
            const std::uint8_t declared =
                outfit::GetSuitParamDonorLockFree(
                    static_cast<std::uint8_t>(suitKind), pt);

            if (declared != 0)
                useKind = declared;
            else if (pt == outfit::kPlayerType_Ocelot)
                useKind = kOcelotSuitKind;
            else if (pt == outfit::kPlayerType_Quiet)
                useKind = kQuietSuitKind;

            if (useKind != suitKind && useLevel == 0
                && (useKind == kOcelotSuitKind || useKind == kQuietSuitKind))
                useLevel = 1;
        }

        if (!g_OrigGetSuitParam) return false;

        const bool served = g_OrigGetSuitParam(self, out, useKind, useLevel);

        const bool custom = suitKind >= outfit::kCustomSelectorStart
                         && suitKind <= outfit::kCustomSelectorEnd;
        if (MissionCodeGuard::ShouldBypassHooks()
            || (!custom && suitKind > outfit::kVanillaCamoTypeMax))
            return served;

        std::uint8_t defense = 0;
        std::uint8_t regen   = 0;
        const std::uint8_t livePT = outfit::ReadLivePlayerType();
        const std::uint8_t liveVar =
            outfit::GetActiveVariantLockFree(outfit::ReadLivePartsType());
        outfit::GetAbilityLevelsForVariantLockFree(
            static_cast<std::uint8_t>(suitKind),
            livePT, liveVar, &defense, &regen);
        const std::uint32_t speed = outfit::GetSprintForVariantLockFree(
            static_cast<std::uint8_t>(suitKind), livePT, liveVar) & outfit::kSprintSpeedMask;
        if (defense == 0 && regen == 0 && speed == 0)
            return served;

        std::size_t count = 0;
        if (served)
            CopyPairs_SEH(*reinterpret_cast<ParamArray**>(out), &count);

        float v = 0.0f;
        if (defense != 0
            && ReadVanillaParam(self, kCamo_BattleDress, defense,
                                kParamId_DamageRate, &v))
            SetPair(&count, kParamId_DamageRate, v);
        if (regen != 0
            && ReadVanillaParam(self, kCamo_SneakingSuitTpp, regen,
                                kParamId_LifeRecovery, &v))
            SetPair(&count, kParamId_LifeRecovery, v);
        if (speed != 0)
            for (const std::uint32_t id : kParamId_Sprint)
                SetPair(&count, id, static_cast<float>(speed) / 100.0f);

        t_Array.flags = 1;
        t_Array.pad0  = 0;
        t_Array.count = static_cast<std::uint16_t>(count);
        t_Array.pad1  = 0;
        t_Array.pairs = t_Pairs;
        *out = &t_Array;

        if (count == 0) return served;

        return true;
    }
}

namespace outfit
{
    bool Install_SuitParamUniqueCharacter_Hook()
    {
        if (g_Installed) return true;

        void* target =
            ResolveGameAddress(gAddr.SkillAndItemParameterSystem_GetSuitParam);
        if (!target)
        {
            LogError("SuitParam: GetSuitParam address missing for this build, custom "
                "outfits give no suit skills");
            return false;
        }

        g_Installed = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkGetSuitParam),
            reinterpret_cast<void**>(&g_OrigGetSuitParam));

        if (!g_Installed)
        {
            LogError("SuitParam: GetSuitParam hook failed, custom outfits give no "
                "suit skills");
            return false;
        }

        return g_Installed;
    }

    bool NativeSuitParamsActive()
    {
        return g_Installed;
    }

    void Uninstall_SuitParamUniqueCharacter_Hook()
    {
        if (!g_Installed) return;
        if (void* t =
                ResolveGameAddress(gAddr.SkillAndItemParameterSystem_GetSuitParam))
            DisableAndRemoveHook(t);
        g_OrigGetSuitParam = nullptr;
        g_Installed        = false;
    }
}
