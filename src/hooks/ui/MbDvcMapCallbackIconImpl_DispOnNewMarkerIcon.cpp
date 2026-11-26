#include <pch.h>
#include "MbDvcMapCallbackIconImpl_DispOnNewMarkerIcon.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "HeadMarkMarkerEvCall_SetIconSubType.h"

namespace
{
    constexpr std::size_t   kImpl_Base           = 0x30;
    constexpr std::size_t   kBase_CommonData     = 0xC8;
    constexpr std::size_t   kRec_Stride          = 0x30;
    constexpr std::size_t   kRec_Bias            = 2;
    constexpr std::size_t   kRec_HandleId        = 0x18;
    constexpr std::uint32_t kMarkerSlotCount     = 0x80;

    constexpr std::size_t   kBase_Utility        = 0x20;
    constexpr std::size_t   kUtility_SetColourRgb = 0x308;
    constexpr std::size_t   kUtility_SetColourGroup  = 0x318;

    constexpr std::uint32_t kGroupSentinel     = 0x56460000u;
    constexpr std::uint32_t kGroupSentinelMask = 0xFFFF0000u;

    constexpr std::uint32_t kMapIconCategoryMask = 0xFFFF0000u;
    constexpr std::uint32_t kMapIconCategory     = 0x00010000u;
    constexpr std::uint32_t kMapIconSlotMask     = 0x000000FFu;

    constexpr std::uint16_t kInvalidMarkerId     = 0xFFFFu;

    constexpr std::uint8_t  kJumpRel32            = 0xE9;

    const std::uint8_t kDisplayParamPrologue[] = {
        0x89, 0x54, 0x24, 0x10,
        0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x83, 0xEC, 0x68,
    };

    const std::uint8_t kDisplayParamMask[] = {
        1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1,
    };

    using UtilitySetRgb_t       = void(__fastcall*)(void* utility, void* component,
                                                    float r, float g, float b);
    using UtilitySetGroup_t     = void(__fastcall*)(void* utility, void* component,
                                                    std::uint32_t group);
    using GetIconDisplayParam_t = bool(__fastcall*)(void* self, std::uint32_t mapIconId,
                                                    std::uint32_t* outColourGroup,
                                                    std::uint64_t* outIconPath,
                                                    float* outScaleX, float* outScaleY,
                                                    float* outAlpha, float* outParam);
    using IsFobMode_t           = bool(__fastcall*)();

    GetIconDisplayParam_t  g_OrigDisplayParam = nullptr;
    UtilitySetGroup_t      g_OrigSetGroup     = nullptr;
    void**                 g_SetGroupSlot     = nullptr;
    bool                   g_SetGroupTried    = false;

    struct SlotRgb
    {
        float        rgb[3];
        std::uint8_t gen;
        bool         live;
    };

    SlotRgb g_SlotRgb[kMarkerSlotCount] = {};
    IsFobMode_t           g_IsFobMode = nullptr;

    bool CallIsFobModeSeh()
    {
        __try
        {
            return g_IsFobMode();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
    }

    bool IsOnlineSession()
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return true;
        if (!g_IsFobMode)
            return false;
        return CallIsFobModeSeh();
    }

    bool ResolveSystemIdSeh(void* self, std::uint32_t slot, std::uint16_t& outId)
    {
        if (slot >= kMarkerSlotCount)
            return false;

        __try
        {
            auto base = *reinterpret_cast<std::uint8_t**>(
                static_cast<std::uint8_t*>(self) + kImpl_Base);
            if (!base)
                return false;

            auto data = *reinterpret_cast<std::uint8_t**>(base + kBase_CommonData);
            if (!data)
                return false;

            outId = *reinterpret_cast<const std::uint16_t*>(
                data + (static_cast<std::size_t>(slot) + kRec_Bias) * kRec_Stride + kRec_HandleId);
            return outId != kInvalidMarkerId;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ApplyRgbDirectSeh(void* utility, void* component, const float rgb[3])
    {
        __try
        {
            if (!utility || !component)
                return false;

            auto uv = *reinterpret_cast<void***>(utility);
            if (!uv)
                return false;

            auto setRgb = reinterpret_cast<UtilitySetRgb_t>(
                uv[kUtility_SetColourRgb / sizeof(void*)]);
            if (!setRgb)
                return false;

            setRgb(utility, component, rgb[0], rgb[1], rgb[2]);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ConvertSentinelGroup(void* utility, void* component, std::uint32_t group)
    {
        if ((group & kGroupSentinelMask) != kGroupSentinel)
            return false;

        const std::uint32_t slot = group & 0xFFu;
        if (slot < kMarkerSlotCount && g_SlotRgb[slot].live)
            ApplyRgbDirectSeh(utility, component, g_SlotRgb[slot].rgb);
        return true;
    }

    void __fastcall hkSetColourGroup(void* utility, void* component, std::uint32_t group)
    {
        if (ConvertSentinelGroup(utility, component, group))
            return;

        if (g_OrigSetGroup)
            g_OrigSetGroup(utility, component, group);
    }

    void** UtilitySlotSeh(void* self, std::size_t slot)
    {
        __try
        {
            auto base = *reinterpret_cast<std::uint8_t**>(
                static_cast<std::uint8_t*>(self) + kImpl_Base);
            if (!base)
                return nullptr;

            auto utility = *reinterpret_cast<void**>(base + kBase_Utility);
            if (!utility)
                return nullptr;

            auto uv = *reinterpret_cast<void***>(utility);
            if (!uv || !uv[slot / sizeof(void*)])
                return nullptr;

            return uv + slot / sizeof(void*);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void EnsureSetColourGroupHook(void* self)
    {
        if (g_SetGroupTried || !self)
            return;

        void** slot = UtilitySlotSeh(self, kUtility_SetColourGroup);
        if (!slot)
            return;

        g_SetGroupTried = true;

        if (!SwapVtableSlot(slot, reinterpret_cast<void*>(&hkSetColourGroup), reinterpret_cast<void**>(&g_OrigSetGroup)))
        {
            LogError("HeadMarkDying: map icon palette setter hook failed @ %p, icons keep "
                     "engine palette", static_cast<void*>(slot));
            return;
        }
        g_SetGroupSlot = slot;
    }

    std::uint8_t* FollowThunkSeh(std::uint8_t* at);

    bool WriteU32Seh(std::uint32_t* p, std::uint32_t value)
    {
        __try
        {
            if (!p)
                return false;
            *p = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool __fastcall hkGetIconDisplayParam(void* self, std::uint32_t mapIconId,
                                          std::uint32_t* outColourGroup,
                                          std::uint64_t* outIconPath,
                                          float* outScaleX, float* outScaleY,
                                          float* outAlpha, float* outParam)
    {
        bool used = false;
        if (g_OrigDisplayParam)
            used = g_OrigDisplayParam(self, mapIconId, outColourGroup, outIconPath,
                                      outScaleX, outScaleY, outAlpha, outParam);

        if ((mapIconId & kMapIconCategoryMask) == kMapIconCategory)
        {
            EnsureSetColourGroupHook(self);

            const std::uint32_t slot = mapIconId & kMapIconSlotMask;
            if (slot < kMarkerSlotCount)
            {
                float         rgb[3]   = {};
                int           reason   = 0;
                std::uint16_t systemId = kInvalidMarkerId;

                const bool have = g_SetGroupSlot && !IsOnlineSession()
                                  && ResolveSystemIdSeh(self, slot, systemId)
                                  && GetHeadMarkColourForSlot(systemId, slot, rgb, &reason);

                if (have)
                {
                    SlotRgb& e = g_SlotRgb[slot];

                    const bool changed = !e.live
                                         || e.rgb[0] != rgb[0]
                                         || e.rgb[1] != rgb[1]
                                         || e.rgb[2] != rgb[2];
                    if (changed)
                        ++e.gen;

                    e.rgb[0] = rgb[0];
                    e.rgb[1] = rgb[1];
                    e.rgb[2] = rgb[2];
                    e.live   = true;

                    WriteU32Seh(
                        outColourGroup,
                        kGroupSentinel | (static_cast<std::uint32_t>(e.gen) << 8) | slot);
                }
                else
                {
                    g_SlotRgb[slot].live = false;
                }
            }
        }

        return used;
    }

    std::uint8_t* FollowThunkSeh(std::uint8_t* at)
    {
        if (!at)
            return nullptr;

        __try
        {
            for (int hop = 0; hop < 4; ++hop)
            {
                if (EntryHookedByOtherModule(at) || at[0] != kJumpRel32)
                    return at;

                const std::int32_t rel = *reinterpret_cast<const std::int32_t*>(at + 1);
                at = at + 5 + rel;
            }
            return at;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }
}

bool Install_MbDvcMapCallbackIconImpl_DispOnNewMarkerIcon_Patch()
{
    g_IsFobMode = reinterpret_cast<IsFobMode_t>(
        ResolveGameAddress(gAddr.Barrier_IsFobMode));

    auto displayParam = FollowThunkSeh(reinterpret_cast<std::uint8_t*>(
        ResolveGameAddress(gAddr.MbDvcMapCallbackIconImpl_GetIconDisplayParam)));
    const HookEntry displayParamCheck = CheckHookEntryMasked(displayParam, kDisplayParamPrologue, kDisplayParamMask,
                                                             sizeof(kDisplayParamPrologue));
    if (!displayParam)
    {
        LogError("HeadMarkDying: no GetIconDisplayParam address for this build, map markers stay vanilla");
        return false;
    }
    if (displayParamCheck == HookEntry::Unknown)
    {
        LogError("HeadMarkDying: %p is not GetIconDisplayParam, map markers stay vanilla",
                 static_cast<void*>(displayParam));
        return false;
    }
    if (!CreateAndEnableHook(displayParam, &hkGetIconDisplayParam,
                             reinterpret_cast<void**>(&g_OrigDisplayParam)))
    {
        LogError("HeadMarkDying: GetIconDisplayParam hook failed @ %p, map markers stay vanilla",
                 static_cast<void*>(displayParam));
        return false;
    }
    if (displayParamCheck == HookEntry::ForeignHook)
        LogHookedOnTop("HeadMarkDying: GetIconDisplayParam", displayParam);

    return true;
}

void Uninstall_MbDvcMapCallbackIconImpl_DispOnNewMarkerIcon_Patch()
{
    RestoreVtableSlot(g_SetGroupSlot, reinterpret_cast<void*>(g_OrigSetGroup), reinterpret_cast<void*>(&hkSetColourGroup));
    g_SetGroupSlot     = nullptr;
    g_OrigDisplayParam = nullptr;
    g_OrigSetGroup     = nullptr;
    g_SetGroupTried    = false;
    g_IsFobMode        = nullptr;
}
