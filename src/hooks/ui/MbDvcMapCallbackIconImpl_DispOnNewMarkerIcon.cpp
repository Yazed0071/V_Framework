#include "pch.h"
#include "MbDvcMapCallbackIconImpl_DispOnNewMarkerIcon.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <iterator>

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
    constexpr std::size_t   kUtility_SetColourGroup2 = 0x320;

    constexpr std::uint32_t kGroupSentinel     = 0x56460000u;
    constexpr std::uint32_t kGroupSentinelMask = 0xFFFF0000u;
    constexpr std::size_t   kComponent_GetColour = 0xC8;

    constexpr std::uint32_t kMapIconCategoryMask = 0xFFFF0000u;
    constexpr std::uint32_t kMapIconCategory     = 0x00010000u;
    constexpr std::uint32_t kMapIconSlotMask     = 0x000000FFu;

    constexpr std::uint16_t kInvalidMarkerId     = 0xFFFFu;

    constexpr std::uint8_t  kJumpRel32            = 0xE9;

    const std::size_t kMarkerNodes[] = { 0x00 };
    const std::size_t kGoalNodes[]   = { 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30 };
    const std::size_t kGeneralNodes[]      = { 0x00, 0x08, 0x10, 0x18 };
    const std::size_t kTargetMarkerNodes[] = { 0x00, 0x08 };
    const std::size_t kAreaNodes[]         = { 0x00, 0x08, 0x10 };

    const std::uint8_t kPrologue[] = {
        0x44, 0x89, 0x44, 0x24, 0x18,
        0x53,
        0x55,
        0x56,
        0x57,
        0x41, 0x56,
        0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,
        0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x33, 0xC4,
        0x48, 0x89, 0x44, 0x24, 0x50,
        0x48, 0x8B, 0xF1,
        0x48, 0x8B, 0x49, 0x30,
        0x41, 0x8B, 0xC0,
        0x48, 0x8B, 0x79, 0x20,
        0x25, 0x00, 0x00, 0xFF, 0xFF,
    };

    const std::uint8_t kPrologueMask[] = {
        1, 1, 1, 1, 1,
        1,
        1,
        1,
        1,
        1, 1,
        1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 0, 0, 0, 0,
        1, 1, 1,
        1, 1, 1, 1, 1,
        1, 1, 1,
        1, 1, 1, 1,
        1, 1, 1,
        1, 1, 1, 1,
        1, 1, 1, 1, 1,
    };

    const std::uint8_t kImportantPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x20,
        0x44, 0x89, 0x44, 0x24, 0x18,
        0x55,
        0x56,
        0x57,
        0x48, 0x83, 0xEC, 0x40,
        0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x33, 0xC4,
        0x48, 0x89, 0x44, 0x24, 0x30,
        0x48, 0x8B, 0xE9,
        0x48, 0x8B, 0x49, 0x30,
        0x41, 0x8B, 0xC0,
        0x48, 0x8B, 0x79, 0x20,
        0x25, 0x00, 0x00, 0xFF, 0xFF,
    };

    const std::uint8_t kImportantMask[] = {
        1, 1, 1, 1, 1,
        1, 1, 1, 1, 1,
        1,
        1,
        1,
        1, 1, 1, 1,
        1, 1, 1, 0, 0, 0, 0,
        1, 1, 1,
        1, 1, 1, 1, 1,
        1, 1, 1,
        1, 1, 1, 1,
        1, 1, 1,
        1, 1, 1, 1,
        1, 1, 1, 1, 1,
    };

    const std::uint8_t kGeneralPrologue[] = {
        0x44, 0x89, 0x44, 0x24, 0x18,
        0x55, 0x56, 0x57, 0x41, 0x56,
        0x48, 0x8D, 0x6C, 0x24, 0xC1,
        0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00,
    };

    const std::uint8_t kGeneralMask[] = {
        1, 1, 1, 1, 1,
        1, 1, 1, 1, 1,
        1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1,
    };

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

    const std::uint8_t kAreaPrologue[] = {
        0x48, 0x8B, 0xC4,
        0x44, 0x89, 0x40, 0x18,
        0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00,
    };

    const std::uint8_t kAreaMask[] = {
        1, 1, 1,
        1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1,
    };

    const std::uint8_t kTargetMarkerPrologue[] = {
        0x44, 0x89, 0x44, 0x24, 0x18,
        0x53, 0x55, 0x56, 0x57, 0x41, 0x56,
        0x48, 0x83, 0xEC, 0x70,
    };

    const std::uint8_t kTargetMarkerMask[] = {
        1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1,
        1, 1, 1, 1,
    };

    const std::uint8_t kGoalPrologue[] = {
        0x4C, 0x8B, 0xDC,
        0x45, 0x89, 0x43, 0x18,
        0x55,
        0x53,
        0x56,
        0x41, 0x56,
        0x41, 0x57,
        0x49, 0x8D, 0x6B, 0xC8,
        0x48, 0x81, 0xEC, 0x10, 0x01, 0x00, 0x00,
        0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x33, 0xC4,
        0x48, 0x89, 0x44, 0x24, 0x70,
        0x48, 0x8B, 0x41, 0x30,
        0x44, 0x89, 0x4C, 0x24, 0x3C,
        0x41, 0x8B, 0xD8,
        0x48, 0x8B, 0x70, 0x20,
        0x41, 0x8B, 0xC0,
        0x4C, 0x8B, 0xF2,
        0x25, 0x00, 0x00, 0xFF, 0xFF,
        0x4C, 0x8B, 0xF9,
        0x3D, 0x00, 0x00, 0x01, 0x00,
    };

    const std::uint8_t kGoalMask[] = {
        1, 1, 1,
        1, 1, 1, 1,
        1,
        1,
        1,
        1, 1,
        1, 1,
        1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 0, 0, 0, 0,
        1, 1, 1,
        1, 1, 1, 1, 1,
        1, 1, 1, 1,
        1, 1, 1, 1, 1,
        1, 1, 1,
        1, 1, 1, 1,
        1, 1, 1,
        1, 1, 1,
        1, 1, 1, 1, 1,
        1, 1, 1,
        1, 1, 1, 1, 1,
    };

    using DispOnGoalIcon_t = void(__fastcall*)(void* self, void* icon, std::uint32_t mapIconId,
                                               std::uint32_t a4, std::uint32_t a5,
                                               std::uint32_t a6, std::uint32_t a7);

        using DispOnImportantIcon_t = void(__fastcall*)(void* self, void* icon, std::uint32_t mapIconId);

    using DispOnNewMarkerIcon_t = void(__fastcall*)(void* self, void* icon, std::uint32_t mapIconId,
                                                   std::uint8_t a, std::uint8_t b, std::uint8_t c);
    using ComponentGetColour_t  = void(__fastcall*)(void* component, float* outColour);
    using UtilitySetRgb_t       = void(__fastcall*)(void* utility, void* component,
                                                    float r, float g, float b);
    using UtilitySetGroup_t     = void(__fastcall*)(void* utility, void* component,
                                                    std::uint32_t group);
    using DispOnListIcon_t      = void(__fastcall*)(void* self, void* icon,
                                                    std::uint32_t mapIconId, float a4);
    using GetIconDisplayParam_t = bool(__fastcall*)(void* self, std::uint32_t mapIconId,
                                                    std::uint32_t* outColourGroup,
                                                    std::uint64_t* outIconPath,
                                                    float* outScaleX, float* outScaleY,
                                                    float* outAlpha, float* outParam);
    using IsFobMode_t           = bool(__fastcall*)();

    DispOnNewMarkerIcon_t  g_Orig          = nullptr;
    DispOnImportantIcon_t  g_OrigImportant = nullptr;
    DispOnGoalIcon_t       g_OrigGoal      = nullptr;
    DispOnListIcon_t       g_OrigGeneral      = nullptr;
    DispOnListIcon_t       g_OrigTargetMarker = nullptr;
    DispOnListIcon_t       g_OrigArea         = nullptr;
    GetIconDisplayParam_t  g_OrigDisplayParam = nullptr;
    UtilitySetGroup_t      g_OrigSetGroup     = nullptr;
    UtilitySetGroup_t      g_OrigSetGroup2    = nullptr;
    void**                 g_SetGroupSlot     = nullptr;
    void**                 g_SetGroupSlot2    = nullptr;
    bool                   g_SetGroupTried    = false;

    struct SlotRgb
    {
        float        rgb[3];
        std::uint8_t gen;
        bool         live;
    };

    SlotRgb g_SlotRgb[kMarkerSlotCount] = {};
    IsFobMode_t           g_IsFobMode = nullptr;
    bool                  g_TintFailureLogged = false;
    constexpr bool        kInstallGoalIcon    = true;

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

    struct TintDiag
    {
        void* component;
        void* utility;
        void* getFn;
        void* rgbFn;
    };

    bool ReadIconTargetSeh(void* self, void* icon, std::size_t offset, TintDiag& d)
    {
        __try
        {
            d.component = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(icon) + offset);
            if (!d.component)
                return false;

            auto base = *reinterpret_cast<std::uint8_t**>(
                static_cast<std::uint8_t*>(self) + kImpl_Base);
            if (!base)
                return false;

            d.utility = *reinterpret_cast<void**>(base + kBase_Utility);
            if (!d.utility)
                return false;

            auto uv = *reinterpret_cast<void***>(d.utility);
            if (!uv)
                return false;
            d.rgbFn = uv[kUtility_SetColourRgb / sizeof(void*)];

            auto cv = *reinterpret_cast<void***>(d.component);
            d.getFn = cv ? cv[kComponent_GetColour / sizeof(void*)] : nullptr;

            return d.rgbFn != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TintViaUtilitySeh(const TintDiag& d, const float rgb[3], float* before, float* after)
    {
        __try
        {
            auto setRgb = reinterpret_cast<UtilitySetRgb_t>(d.rgbFn);
            auto get    = reinterpret_cast<ComponentGetColour_t>(d.getFn);

            if (get)
                get(d.component, before);

            setRgb(d.utility, d.component, rgb[0], rgb[1], rgb[2]);

            if (get)
                get(d.component, after);

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void TintForIcon(void* self, void* icon, std::uint32_t mapIconId,
                     const std::size_t* nodes, unsigned nodeCount)
    {
        if (!self || !icon)
            return;

        if (IsOnlineSession())
            return;

        const std::uint32_t slot = mapIconId & kMapIconSlotMask;

        if ((mapIconId & kMapIconCategoryMask) != kMapIconCategory)
            return;

        std::uint16_t systemId = kInvalidMarkerId;
        if (!ResolveSystemIdSeh(self, slot, systemId))
            return;

        float rgb[3] = {};
        int   reason = 0;
        if (!GetHeadMarkColourForSlot(systemId, slot, rgb, &reason))
            return;

        unsigned applied = 0;

        for (unsigned i = 0; i < nodeCount; ++i)
        {
            TintDiag          node{};
            alignas(16) float nodeBefore[4] = {};
            alignas(16) float nodeAfter[4]  = {};

            if (!ReadIconTargetSeh(self, icon, nodes[i], node))
                continue;
            if (!TintViaUtilitySeh(node, rgb, nodeBefore, nodeAfter))
                continue;

            ++applied;
        }

        if (applied == 0 && !g_TintFailureLogged)
        {
            g_TintFailureLogged = true;
            LogWarn("HeadMarkDying: iDroid map icon refused colour, map markers stay vanilla");
        }
    }

    void __fastcall hkDispOnNewMarkerIcon(void* self, void* icon, std::uint32_t mapIconId,
                                          std::uint8_t a, std::uint8_t b, std::uint8_t c)
    {
        if (g_Orig)
            g_Orig(self, icon, mapIconId, a, b, c);

        TintForIcon(self, icon, mapIconId, kMarkerNodes,
                    static_cast<unsigned>(std::size(kMarkerNodes)));
    }

    void __fastcall hkDispOnImportantIcon(void* self, void* icon, std::uint32_t mapIconId)
    {
        if (g_OrigImportant)
            g_OrigImportant(self, icon, mapIconId);

        TintForIcon(self, icon, mapIconId, kMarkerNodes,
                    static_cast<unsigned>(std::size(kMarkerNodes)));
    }

    void __fastcall hkDispOnGoalIcon(void* self, void* icon, std::uint32_t mapIconId,
                                     std::uint32_t a4, std::uint32_t a5,
                                     std::uint32_t a6, std::uint32_t a7)
    {
        if (g_OrigGoal)
            g_OrigGoal(self, icon, mapIconId, a4, a5, a6, a7);

        TintForIcon(self, icon, mapIconId, kGoalNodes,
                    static_cast<unsigned>(std::size(kGoalNodes)));
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

    void __fastcall hkSetColourGroup2(void* utility, void* component, std::uint32_t group)
    {
        if (ConvertSentinelGroup(utility, component, group))
            return;

        if (g_OrigSetGroup2)
            g_OrigSetGroup2(utility, component, group);
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

        void** slot  = UtilitySlotSeh(self, kUtility_SetColourGroup);
        void** slot2 = UtilitySlotSeh(self, kUtility_SetColourGroup2);
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

        if (slot2 && slot2 != slot)
        {
            if (SwapVtableSlot(slot2, reinterpret_cast<void*>(&hkSetColourGroup2), reinterpret_cast<void**>(&g_OrigSetGroup2)))
                g_SetGroupSlot2 = slot2;
            else
                LogError("HeadMarkDying: important-target palette setter hook failed @ %p", static_cast<void*>(slot2));
        }
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

                const bool have = !IsOnlineSession()
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

    void __fastcall hkDispOnAreaIcon(void* self, void* icon, std::uint32_t mapIconId,
                                     float a4)
    {
        if (g_OrigArea)
            g_OrigArea(self, icon, mapIconId, a4);

        TintForIcon(self, icon, mapIconId, kAreaNodes,
                    static_cast<unsigned>(std::size(kAreaNodes)));
    }

    void __fastcall hkDispOnGeneralIcon(void* self, void* icon, std::uint32_t mapIconId,
                                        float a4)
    {
        if (g_OrigGeneral)
            g_OrigGeneral(self, icon, mapIconId, a4);

        TintForIcon(self, icon, mapIconId, kGeneralNodes,
                    static_cast<unsigned>(std::size(kGeneralNodes)));
    }

    void __fastcall hkDispOnTargetMarkerIcon(void* self, void* icon, std::uint32_t mapIconId,
                                             float a4)
    {
        if (g_OrigTargetMarker)
            g_OrigTargetMarker(self, icon, mapIconId, a4);

        TintForIcon(self, icon, mapIconId, kTargetMarkerNodes,
                    static_cast<unsigned>(std::size(kTargetMarkerNodes)));
    }

    std::uint8_t* FollowThunkSeh(std::uint8_t* at)
    {
        if (!at)
            return nullptr;

        __try
        {
            for (int hop = 0; hop < 4; ++hop)
            {
                if (at[0] != kJumpRel32)
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

    bool PrologueMatchesSeh(const std::uint8_t* at, const std::uint8_t* expect,
                            const std::uint8_t* mask, std::size_t len)
    {
        __try
        {
            for (std::size_t i = 0; i < len; ++i)
            {
                if (mask[i] && at[i] != expect[i])
                    return false;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

bool Install_MbDvcMapCallbackIconImpl_DispOnNewMarkerIcon_Patch()
{
    auto anchor = reinterpret_cast<std::uint8_t*>(ResolveGameAddress(
        gAddr.MbDvcMapCallbackIconImpl_DispOnNewMarkerIcon));
    auto entry  = FollowThunkSeh(anchor);
    if (!entry)
    {
        LogError("HeadMarkDying: no DispOnNewMarkerIcon address for this build, map markers stay vanilla");
        return false;
    }

    if (!PrologueMatchesSeh(entry, kPrologue, kPrologueMask, sizeof(kPrologue)))
    {
        LogError("HeadMarkDying: %p is not DispOnNewMarkerIcon, map markers stay vanilla", static_cast<void*>(entry));
        return false;
    }

    g_IsFobMode = reinterpret_cast<IsFobMode_t>(
        ResolveGameAddress(gAddr.Barrier_IsFobMode));

    if (!CreateAndEnableHook(entry, &hkDispOnNewMarkerIcon, reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("HeadMarkDying: DispOnNewMarkerIcon hook failed @ %p, map markers stay vanilla", static_cast<void*>(entry));
        return false;
    }

    auto important = FollowThunkSeh(reinterpret_cast<std::uint8_t*>(
        ResolveGameAddress(gAddr.MbDvcMapCallbackIconImpl_DispOnImportantIcon)));
    if (!important)
    {
        LogError("HeadMarkDying: no DispOnImportantIcon address for this build, marked-enemy icons "
                 "stay vanilla");
    }
    else if (!PrologueMatchesSeh(important, kImportantPrologue, kImportantMask,
                                 sizeof(kImportantPrologue)))
    {
        LogError("HeadMarkDying: %p is not DispOnImportantIcon, marked-enemy icons stay vanilla",
                 static_cast<void*>(important));
    }
    else if (!CreateAndEnableHook(important, &hkDispOnImportantIcon,
                                  reinterpret_cast<void**>(&g_OrigImportant)))
    {
        LogError("HeadMarkDying: DispOnImportantIcon hook failed @ %p, marked-enemy icons stay vanilla",
                 static_cast<void*>(important));
    }

    if constexpr (kInstallGoalIcon)
    {
        auto goal = FollowThunkSeh(reinterpret_cast<std::uint8_t*>(
            ResolveGameAddress(gAddr.MbDvcMapCallbackIconImpl_DispOnGoalIcon)));
        if (!goal)
        {
            LogError("HeadMarkDying: no DispOnGoalIcon address for this build, objective icons "
                     "stay vanilla");
        }
        else if (!PrologueMatchesSeh(goal, kGoalPrologue, kGoalMask, sizeof(kGoalPrologue)))
        {
            LogError("HeadMarkDying: %p is not DispOnGoalIcon, objective icons stay vanilla", static_cast<void*>(goal));
        }
        else if (!CreateAndEnableHook(goal, &hkDispOnGoalIcon,
                                      reinterpret_cast<void**>(&g_OrigGoal)))
        {
            LogError("HeadMarkDying: DispOnGoalIcon hook failed @ %p, objective icons stay vanilla",
                     static_cast<void*>(goal));
        }
    }
    else
    {
        (void)&hkDispOnGoalIcon;
        (void)kGoalPrologue;
        (void)kGoalMask;
    }

    auto general = FollowThunkSeh(reinterpret_cast<std::uint8_t*>(
        ResolveGameAddress(gAddr.MbDvcMapCallbackIconImpl_DispOnGeneralIcon)));
    if (!general)
    {
        LogError("HeadMarkDying: no DispOnGeneralIcon address for this build, general icons "
                 "stay vanilla");
    }
    else if (!PrologueMatchesSeh(general, kGeneralPrologue, kGeneralMask,
                                 sizeof(kGeneralPrologue)))
    {
        LogError("HeadMarkDying: %p is not DispOnGeneralIcon, general icons stay vanilla", static_cast<void*>(general));
    }
    else if (!CreateAndEnableHook(general, &hkDispOnGeneralIcon,
                                  reinterpret_cast<void**>(&g_OrigGeneral)))
    {
        LogError("HeadMarkDying: DispOnGeneralIcon hook failed @ %p, general icons stay vanilla",
                 static_cast<void*>(general));
    }

    auto targetMarker = FollowThunkSeh(reinterpret_cast<std::uint8_t*>(
        ResolveGameAddress(gAddr.MbDvcMapCallbackIconImpl_DispOnTargetMarkerIcon)));
    if (!targetMarker)
    {
        LogError("HeadMarkDying: no DispOnTargetMarkerIcon address for this build, type-3 markers "
                 "stay vanilla");
    }
    else if (!PrologueMatchesSeh(targetMarker, kTargetMarkerPrologue, kTargetMarkerMask,
                                 sizeof(kTargetMarkerPrologue)))
    {
        LogError("HeadMarkDying: %p is not DispOnTargetMarkerIcon, type-3 markers stay vanilla",
                 static_cast<void*>(targetMarker));
    }
    else if (!CreateAndEnableHook(targetMarker, &hkDispOnTargetMarkerIcon,
                                  reinterpret_cast<void**>(&g_OrigTargetMarker)))
    {
        LogError("HeadMarkDying: DispOnTargetMarkerIcon hook failed @ %p, type-3 markers stay vanilla",
                 static_cast<void*>(targetMarker));
    }

    auto displayParam = FollowThunkSeh(reinterpret_cast<std::uint8_t*>(
        ResolveGameAddress(gAddr.MbDvcMapCallbackIconImpl_GetIconDisplayParam)));
    if (!displayParam)
    {
        LogError("HeadMarkDying: no GetIconDisplayParam address for this build, icon palette "
                 "not overridden");
    }
    else if (!PrologueMatchesSeh(displayParam, kDisplayParamPrologue, kDisplayParamMask,
                                 sizeof(kDisplayParamPrologue)))
    {
        LogError("HeadMarkDying: %p is not GetIconDisplayParam, icon palette not overridden",
                 static_cast<void*>(displayParam));
    }
    else if (!CreateAndEnableHook(displayParam, &hkGetIconDisplayParam,
                                  reinterpret_cast<void**>(&g_OrigDisplayParam)))
    {
        LogError("HeadMarkDying: GetIconDisplayParam hook failed @ %p, icon palette not overridden",
                 static_cast<void*>(displayParam));
    }

    auto area = FollowThunkSeh(reinterpret_cast<std::uint8_t*>(
        ResolveGameAddress(gAddr.MbDvcMapCallbackIconImpl_DispOnAreaIcon)));
    if (!area)
    {
        LogError("HeadMarkDying: no DispOnAreaIcon address for this build, area icons stay vanilla");
    }
    else if (!PrologueMatchesSeh(area, kAreaPrologue, kAreaMask, sizeof(kAreaPrologue)))
    {
        LogError("HeadMarkDying: %p is not DispOnAreaIcon, area icons stay vanilla", static_cast<void*>(area));
    }
    else if (!CreateAndEnableHook(area, &hkDispOnAreaIcon,
                                  reinterpret_cast<void**>(&g_OrigArea)))
    {
        LogError("HeadMarkDying: DispOnAreaIcon hook failed @ %p, area icons stay vanilla", static_cast<void*>(area));
    }

    return true;
}

void Uninstall_MbDvcMapCallbackIconImpl_DispOnNewMarkerIcon_Patch()
{
    RestoreVtableSlot(g_SetGroupSlot2, reinterpret_cast<void*>(g_OrigSetGroup2));
    RestoreVtableSlot(g_SetGroupSlot, reinterpret_cast<void*>(g_OrigSetGroup));
    g_SetGroupSlot  = nullptr;
    g_SetGroupSlot2 = nullptr;
    g_Orig          = nullptr;
    g_OrigImportant = nullptr;
    g_OrigGoal      = nullptr;
    g_OrigGeneral      = nullptr;
    g_OrigTargetMarker = nullptr;
    g_OrigArea         = nullptr;
    g_OrigDisplayParam = nullptr;
    g_OrigSetGroup     = nullptr;
    g_OrigSetGroup2    = nullptr;
    g_SetGroupTried    = false;
    g_IsFobMode     = nullptr;
}
