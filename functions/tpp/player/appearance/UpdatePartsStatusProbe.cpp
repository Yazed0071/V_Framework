#include "pch.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "tpp/player/appearance/CustomSuitRegistry.h"

namespace
{
    using UpdatePartsStatus_t = void(__fastcall*)(void* self);
    using GetQuarkSystemTable_t = void* (__fastcall*)();

    static UpdatePartsStatus_t g_OrigUpdatePartsStatus = nullptr;
    static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    static bool g_Installed = false;

    // Set once the custom face-equip has successfully loaded.
    static bool g_CustomFaceEquipLoadCompleted = false;

    // After load completion, allow exactly one repair during each idle period.
    static bool g_IdleFaceEquipRepairIssued = false;

    struct LiveSuitState
    {
        std::uint8_t  f8 = 0xFF;       // playerPartsType
        std::uint8_t  f9 = 0xFF;       // selector / camo-like code
        std::uint8_t  fb = 0xFF;       // playerType
        std::uint16_t fc = 0xFFFF;     // faceId
        std::uint16_t fe = 0xFFFF;     // head option
    };

    struct SlotSourceRefs
    {
        bool valid = false;
        std::uint32_t selectedSlot = 0xFFFFFFFF;

        std::uint8_t* base40_type = nullptr;
        std::uint8_t* base48_parts = nullptr;
        std::uint8_t* base50_camo = nullptr;
        std::uint16_t* base60_face = nullptr;
        std::uint8_t* base68_byte = nullptr;
    };

    struct SlotSourceState
    {
        bool valid = false;
        std::uint32_t selectedSlot = 0xFFFFFFFF;
        std::uint8_t  src40_type = 0xFF;
        std::uint8_t  src48_parts = 0xFF;
        std::uint8_t  src50_camo = 0xFF;
        std::uint16_t src60_face = 0xFFFF;
        std::uint8_t  src68_byte = 0xFF;
    };

    struct StagedAvatarState
    {
        std::uint32_t type = 0xFFFFFFFF;
        std::uint32_t camo = 0xFFFFFFFF;
        std::uint32_t parts = 0xFFFFFFFF;
        std::uint16_t face = 0xFFFF;
        std::uint8_t  state = 0xFF;
    };

    struct QuarkStoredAppearance
    {
        bool valid = false;
        std::uint8_t armType = 0;
        std::uint8_t faceEquipId = 0;
        std::uint8_t faceEquipUnk = 0;
        std::uint16_t headOption = 0;
    };

    static bool ResolveQuarkApi()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable =
                reinterpret_cast<GetQuarkSystemTable_t>(
                    ResolveGameAddress(gAddr.GetQuarkSystemTable)
                    );
        }

        return g_GetQuarkSystemTable != nullptr;
    }

    static std::uint8_t* GetPlayerQuarkState()
    {
        if (!ResolveQuarkApi())
            return nullptr;

        auto* quarkSystemTable =
            reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!quarkSystemTable)
            return nullptr;

        auto* q98 = *reinterpret_cast<std::uint8_t**>(quarkSystemTable + 0x98);
        if (!q98)
            return nullptr;

        return *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
    }

    static bool TryReadQuarkStoredAppearance(
        std::uint8_t playerType,
        QuarkStoredAppearance& out)
    {
        out = {};

        auto* state = GetPlayerQuarkState();
        if (!state)
            return false;

        if (playerType == 0 || playerType == 3)
        {
            out.armType = state[0x1994];
            out.faceEquipId = state[0x1995];
            out.faceEquipUnk = state[0x1998];
            out.headOption = *reinterpret_cast<std::uint16_t*>(state + 0x1996);
        }
        else if (playerType == 1 || playerType == 2)
        {
            out.armType = state[0x1999];
            out.faceEquipId = state[0x199A];
            out.faceEquipUnk = state[0x199E];
            out.headOption = *reinterpret_cast<std::uint16_t*>(state + 0x199C);
        }
        else
        {
            return false;
        }

        out.valid =
            (out.armType != 0) ||
            (out.faceEquipId != 0) ||
            (out.faceEquipUnk != 0) ||
            (out.headOption != 0 && out.headOption != 0xFFFF);

        return out.valid;
    }

    static bool TryResolveCustomFaceEquipId(
        const CustomSuitEntry* entry,
        std::uint8_t& outFaceEquipId)
    {
        outFaceEquipId = 0;
        if (!entry)
            return false;

        QuarkStoredAppearance quark{};
        if (TryReadQuarkStoredAppearance(entry->playerType, quark) &&
            quark.valid &&
            quark.faceEquipId != 0)
        {
            outFaceEquipId = quark.faceEquipId;
            return true;
        }

        PreservedAppearanceState preserved{};
        if (TryGetPreservedAppearance(entry->playerType, preserved) &&
            preserved.faceEquipId != 0)
        {
            outFaceEquipId = preserved.faceEquipId;
            return true;
        }

        return false;
    }

    static bool TryReadLiveSuitState(LiveSuitState& out)
    {
        auto* state = GetPlayerQuarkState();
        if (!state)
            return false;

        out.f8 = state[0xF8];
        out.f9 = state[0xF9];
        out.fb = state[0xFB];
        out.fc = *reinterpret_cast<std::uint16_t*>(state + 0xFC);
        out.fe = *reinterpret_cast<std::uint16_t*>(state + 0xFE);
        return true;
    }

    static bool TryReadStagedAvatarState(void* self, StagedAvatarState& out)
    {
        if (!self)
            return false;

        auto* p = reinterpret_cast<std::uint8_t*>(self);

        out.type = *reinterpret_cast<std::uint32_t*>(p + 0x23C);
        out.camo = *reinterpret_cast<std::uint32_t*>(p + 0x240);
        out.parts = *reinterpret_cast<std::uint32_t*>(p + 0x244);
        out.face = *reinterpret_cast<std::uint16_t*>(p + 0x248);
        out.state = *(p + 0x27C);
        return true;
    }

    static bool TryReadSlotSourceRefs(void* self, SlotSourceRefs& out)
    {
        if (!self)
            return false;

        auto* p = reinterpret_cast<std::uint8_t*>(self);
        auto* info = *reinterpret_cast<std::uint8_t**>(p + 0x80);
        if (!info)
            return false;

        const std::uint32_t slot = *reinterpret_cast<std::uint32_t*>(p + 0x234);

        auto* base40 = *reinterpret_cast<std::uint8_t**>(info + 0x40);
        auto* base48 = *reinterpret_cast<std::uint8_t**>(info + 0x48);
        auto* base50 = *reinterpret_cast<std::uint8_t**>(info + 0x50);
        auto* base60 = *reinterpret_cast<std::uint16_t**>(info + 0x60);
        auto* base68 = *reinterpret_cast<std::uint8_t**>(info + 0x68);

        if (!base40 || !base48 || !base50 || !base60 || !base68)
            return false;

        out.valid = true;
        out.selectedSlot = slot;
        out.base40_type = base40;
        out.base48_parts = base48;
        out.base50_camo = base50;
        out.base60_face = base60;
        out.base68_byte = base68;
        return true;
    }

    static bool MakeSlotSourceState(const SlotSourceRefs& refs, SlotSourceState& out)
    {
        if (!refs.valid)
            return false;

        const std::uint32_t slot = refs.selectedSlot;

        out.valid = true;
        out.selectedSlot = slot;
        out.src40_type = refs.base40_type[slot];
        out.src48_parts = refs.base48_parts[slot];
        out.src50_camo = refs.base50_camo[slot];
        out.src60_face = refs.base60_face[slot];
        out.src68_byte = refs.base68_byte[slot];
        return true;
    }

    static bool TryReadSlotSourceState(void* self, SlotSourceState& out)
    {
        SlotSourceRefs refs{};
        if (!TryReadSlotSourceRefs(self, refs))
            return false;

        return MakeSlotSourceState(refs, out);
    }

    static bool IsCustomLiveState(const LiveSuitState& s)
    {
        return
            (s.f8 >= 0x40 && s.f8 <= 0x7F) ||
            (s.f9 >= 0x80 && s.f9 <= 0xFE);
    }

    static bool Changed(
        const LiveSuitState& a,
        const LiveSuitState& b,
        const SlotSourceState& sa,
        const SlotSourceState& sb,
        const StagedAvatarState& ta,
        const StagedAvatarState& tb)
    {
        return std::memcmp(&a, &b, sizeof(LiveSuitState)) != 0 ||
            std::memcmp(&sa, &sb, sizeof(SlotSourceState)) != 0 ||
            std::memcmp(&ta, &tb, sizeof(StagedAvatarState)) != 0;
    }

    static bool LooksLikeBadCustomReset(
        const SlotSourceState& slot,
        const LiveSuitState& live,
        const ActiveCustomSuitState& active)
    {
        if (!slot.valid || !active.valid)
            return false;

        return
            slot.src48_parts == 0x00 &&
            slot.src50_camo == 0xFF &&
            live.fb == active.playerType &&
            live.f8 == active.partsType &&
            live.f9 == active.selectorCode &&
            live.fc == active.faceId;
    }

    static void PatchSourceToCustom(
        const SlotSourceRefs& refs,
        std::uint8_t playerType,
        std::uint8_t partsType,
        std::uint8_t selectorCode,
        std::uint16_t faceId,
        std::uint8_t faceEquipId)
    {
        const std::uint32_t slot = refs.selectedSlot;

        refs.base40_type[slot] = playerType;
        refs.base48_parts[slot] = partsType;
        refs.base50_camo[slot] = selectorCode;
        refs.base60_face[slot] = faceId;
        refs.base68_byte[slot] = faceEquipId;
    }

    static bool TryArmForcedCustomSuit(
        void* self,
        const LiveSuitState& live,
        const StagedAvatarState& staged)
    {
        if (!self)
            return false;

        if (staged.state != 0x18)
            return false;

        const std::uint16_t pendingDevelopId = GetPendingCustomSuitDevelopId();
        if (pendingDevelopId == 0 || pendingDevelopId == 0xFFFF)
            return false;

        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByDevelopIdForPlayerType(pendingDevelopId, live.fb, &entry) || !entry)
            return false;

        auto* p = reinterpret_cast<std::uint8_t*>(self);

        *reinterpret_cast<std::uint32_t*>(p + 0x23C) = entry->playerType;
        *reinterpret_cast<std::uint32_t*>(p + 0x240) = entry->customSelectorCode;
        *reinterpret_cast<std::uint32_t*>(p + 0x244) = entry->customPartsType;
        *reinterpret_cast<std::uint16_t*>(p + 0x248) = live.fc;

        p[0x27C] = static_cast<std::uint8_t>((p[0x27C] & 0xFC) | 0x01);

        Log(
            "[UpdatePartsStatusProbe] arm forced custom developId=%u type=0x%02X selector=0x%02X parts=0x%02X face=0x%04X flags=0x%02X\n",
            static_cast<unsigned>(pendingDevelopId),
            static_cast<unsigned>(entry->playerType),
            static_cast<unsigned>(entry->customSelectorCode),
            static_cast<unsigned>(entry->customPartsType),
            static_cast<unsigned>(live.fc),
            static_cast<unsigned>(p[0x27C])
        );

        return true;
    }

    static void PatchBadCustomReset(
        const SlotSourceRefs& refs,
        const ActiveCustomSuitState& active)
    {
        const CustomSuitEntry* entry = nullptr;
        std::uint8_t faceEquipId = 0;

        if (TryGetCustomSuitByPartsType(active.partsType, &entry) && entry)
            TryResolveCustomFaceEquipId(entry, faceEquipId);

        PatchSourceToCustom(
            refs,
            active.playerType,
            active.partsType,
            active.selectorCode,
            active.faceId,
            faceEquipId
        );
    }

    static bool TryRepairLiveCustomHeadOption(const LiveSuitState& live)
    {
        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByPartsType(live.f8, &entry) || !entry)
            return false;

        if (!entry->enableHead)
            return false;

        if (live.fe != 0 && live.fe != 0xFFFF)
            return false;

        std::uint16_t resolvedHead = 0;

        ActiveCustomSuitState active{};
        if (TryGetActiveCustomSuit(active) &&
            active.valid &&
            active.partsType == entry->customPartsType &&
            active.playerType == entry->playerType &&
            active.headOption != 0 &&
            active.headOption != 0xFFFF)
        {
            resolvedHead = active.headOption;
        }

        if (resolvedHead == 0 || resolvedHead == 0xFFFF)
        {
            QuarkStoredAppearance quark{};
            if (TryReadQuarkStoredAppearance(entry->playerType, quark) &&
                quark.valid &&
                quark.headOption != 0 &&
                quark.headOption != 0xFFFF)
            {
                resolvedHead = quark.headOption;
            }
        }

        if (resolvedHead == 0 || resolvedHead == 0xFFFF)
            return false;

        auto* state = GetPlayerQuarkState();
        if (!state)
            return false;

        *reinterpret_cast<std::uint16_t*>(state + 0xFE) = resolvedHead;

        Log(
            "[UpdatePartsStatusProbe] repaired live head parts=0x%02X fe=0x%04X\n",
            static_cast<unsigned>(entry->customPartsType),
            static_cast<unsigned>(resolvedHead)
        );

        return true;
    }

    static bool TryRepairCustomFaceEquipIdAnyPhase(
        void* self,
        const LiveSuitState& live,
        const SlotSourceState& slot,
        const StagedAvatarState& staged)
    {
        if (!self || !slot.valid)
            return false;

        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByPartsType(live.f8, &entry) || !entry)
            return false;

        if (!entry->enableHead)
            return false;

        std::uint8_t resolvedFaceEquipId = 0;
        if (!TryResolveCustomFaceEquipId(entry, resolvedFaceEquipId) || resolvedFaceEquipId == 0)
            return false;

        if (slot.src68_byte == resolvedFaceEquipId)
            return false;

        SlotSourceRefs refs{};
        if (!TryReadSlotSourceRefs(self, refs) || !refs.valid)
            return false;

        refs.base68_byte[refs.selectedSlot] = resolvedFaceEquipId;

        Log(
            "[UpdatePartsStatusProbe] repaired custom faceEquip slot=0x%08X parts=0x%02X state=0x%02X src68=0x%02X old=0x%02X\n",
            static_cast<unsigned>(refs.selectedSlot),
            static_cast<unsigned>(entry->customPartsType),
            static_cast<unsigned>(staged.state),
            static_cast<unsigned>(resolvedFaceEquipId),
            static_cast<unsigned>(slot.src68_byte)
        );

        return true;
    }

    static bool IsCustomFaceEquipReady(
        const LiveSuitState& live,
        const SlotSourceState& slot,
        const StagedAvatarState& staged)
    {
        if (!slot.valid)
            return false;

        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByPartsType(live.f8, &entry) || !entry)
            return false;

        if (!entry->enableHead)
            return false;

        std::uint8_t resolvedFaceEquipId = 0;
        if (!TryResolveCustomFaceEquipId(entry, resolvedFaceEquipId) || resolvedFaceEquipId == 0)
            return false;

        return staged.state == 0x02 && slot.src68_byte == resolvedFaceEquipId;
    }
}

static void __fastcall hkUpdatePartsStatus(void* self)
{
    if (GetPendingCustomSuitDevelopId() != 0)
    {
        g_CustomFaceEquipLoadCompleted = false;
        g_IdleFaceEquipRepairIssued = false;
    }

    LiveSuitState beforeLive{};
    const bool haveBeforeLive = TryReadLiveSuitState(beforeLive);

    SlotSourceRefs beforeRefs{};
    const bool haveBeforeRefs = TryReadSlotSourceRefs(self, beforeRefs);

    SlotSourceState beforeSlot{};
    const bool haveBeforeSlot =
        haveBeforeRefs && MakeSlotSourceState(beforeRefs, beforeSlot);

    StagedAvatarState beforeStaged{};
    const bool haveBeforeStaged = TryReadStagedAvatarState(self, beforeStaged);

    if (haveBeforeLive && haveBeforeStaged)
    {
        TryArmForcedCustomSuit(self, beforeLive, beforeStaged);
    }

    if (haveBeforeLive && haveBeforeRefs && haveBeforeSlot)
    {
        ActiveCustomSuitState active{};
        if (TryGetActiveCustomSuit(active) &&
            LooksLikeBadCustomReset(beforeSlot, beforeLive, active))
        {
            PatchBadCustomReset(beforeRefs, active);

            SlotSourceState patchedSlot{};
            MakeSlotSourceState(beforeRefs, patchedSlot);

            Log(
                "[UpdatePartsStatusProbe] anti-reset developId=%u "
                "slot=0x%08X src{40=0x%02X 48=0x%02X 50=0x%02X 60=0x%04X 68=0x%02X}\n",
                static_cast<unsigned>(active.developId),
                static_cast<unsigned>(patchedSlot.selectedSlot),
                static_cast<unsigned>(patchedSlot.src40_type),
                static_cast<unsigned>(patchedSlot.src48_parts),
                static_cast<unsigned>(patchedSlot.src50_camo),
                static_cast<unsigned>(patchedSlot.src60_face),
                static_cast<unsigned>(patchedSlot.src68_byte)
            );
        }
    }

    g_OrigUpdatePartsStatus(self);

    LiveSuitState live{};
    SlotSourceState slot{};
    StagedAvatarState staged{};

    const bool haveLive = TryReadLiveSuitState(live);
    const bool haveSlot = TryReadSlotSourceState(self, slot);
    const bool haveStaged = TryReadStagedAvatarState(self, staged);

    if (haveLive)
    {
        TryRepairLiveCustomHeadOption(live);
        TryReadLiveSuitState(live);
    }

    if (haveLive && haveSlot && haveStaged)
    {
        if (IsCustomFaceEquipReady(live, slot, staged))
        {
            g_CustomFaceEquipLoadCompleted = true;
        }

        if (staged.state != 0x00)
        {
            g_IdleFaceEquipRepairIssued = false;
        }

        bool allowRepair = false;

        if (!g_CustomFaceEquipLoadCompleted || staged.state != 0x00)
        {
            allowRepair = true;
        }
        else
        {
            const CustomSuitEntry* entry = nullptr;
            std::uint8_t resolvedFaceEquipId = 0;

            if (TryGetCustomSuitByPartsType(live.f8, &entry) &&
                entry &&
                entry->enableHead &&
                TryResolveCustomFaceEquipId(entry, resolvedFaceEquipId) &&
                resolvedFaceEquipId != 0 &&
                slot.src68_byte != resolvedFaceEquipId &&
                !g_IdleFaceEquipRepairIssued)
            {
                allowRepair = true;
            }
        }

        if (allowRepair &&
            TryRepairCustomFaceEquipIdAnyPhase(self, live, slot, staged))
        {
            if (staged.state == 0x00)
            {
                g_IdleFaceEquipRepairIssued = true;
            }

            TryReadSlotSourceState(self, slot);
            TryReadLiveSuitState(live);

            if (IsCustomFaceEquipReady(live, slot, staged))
            {
                g_CustomFaceEquipLoadCompleted = true;
            }
        }
    }

    if (haveLive)
    {
        const std::uint16_t pendingDevelopId = GetPendingCustomSuitDevelopId();
        if (pendingDevelopId != 0 && pendingDevelopId != 0xFFFF)
        {
            const CustomSuitEntry* entry = nullptr;
            if (TryGetCustomSuitByDevelopIdForPlayerType(pendingDevelopId, live.fb, &entry) &&
                entry &&
                live.f8 == entry->customPartsType)
            {
                std::uint16_t resolvedHead = live.fe;

                if (resolvedHead == 0 || resolvedHead == 0xFFFF)
                {
                    QuarkStoredAppearance quark{};
                    if (TryReadQuarkStoredAppearance(entry->playerType, quark) &&
                        quark.valid &&
                        quark.headOption != 0 &&
                        quark.headOption != 0xFFFF)
                    {
                        resolvedHead = quark.headOption;
                    }
                }

                SetActiveCustomSuit(
                    pendingDevelopId,
                    entry->playerType,
                    entry->customPartsType,
                    entry->customSelectorCode,
                    live.fc,
                    resolvedHead
                );

                ClearPendingCustomSuitDevelopId();

                Log(
                    "[UpdatePartsStatusProbe] forced custom applied developId=%u live{f8=0x%02X f9=0x%02X fb=0x%02X fc=0x%04X fe=0x%04X}\n",
                    static_cast<unsigned>(pendingDevelopId),
                    static_cast<unsigned>(live.f8),
                    static_cast<unsigned>(live.f9),
                    static_cast<unsigned>(live.fb),
                    static_cast<unsigned>(live.fc),
                    static_cast<unsigned>(resolvedHead)
                );
            }
        }
    }

    static LiveSuitState lastLive{};
    static SlotSourceState lastSlot{};
    static StagedAvatarState lastStaged{};
    static bool haveLast = false;

    if (!haveLive && !haveSlot && !haveStaged)
        return;

    if (!haveLast || Changed(live, lastLive, slot, lastSlot, staged, lastStaged))
    {
        Log(
            "[UpdatePartsStatusProbe] "
            "slot=0x%08X "
            "src{40=0x%02X 48=0x%02X 50=0x%02X 60=0x%04X 68=0x%02X} "
            "live{f8=0x%02X f9=0x%02X fb=0x%02X fc=0x%04X fe=0x%04X} "
            "staged{type=0x%08X camo=0x%08X parts=0x%08X face=0x%04X state=0x%02X}\n",
            haveSlot ? static_cast<unsigned>(slot.selectedSlot) : 0xFFFFFFFFu,
            haveSlot ? static_cast<unsigned>(slot.src40_type) : 0xFFu,
            haveSlot ? static_cast<unsigned>(slot.src48_parts) : 0xFFu,
            haveSlot ? static_cast<unsigned>(slot.src50_camo) : 0xFFu,
            haveSlot ? static_cast<unsigned>(slot.src60_face) : 0xFFFFu,
            haveSlot ? static_cast<unsigned>(slot.src68_byte) : 0xFFu,
            haveLive ? static_cast<unsigned>(live.f8) : 0xFFu,
            haveLive ? static_cast<unsigned>(live.f9) : 0xFFu,
            haveLive ? static_cast<unsigned>(live.fb) : 0xFFu,
            haveLive ? static_cast<unsigned>(live.fc) : 0xFFFFu,
            haveLive ? static_cast<unsigned>(live.fe) : 0xFFFFu,
            haveStaged ? static_cast<unsigned>(staged.type) : 0xFFFFFFFFu,
            haveStaged ? static_cast<unsigned>(staged.camo) : 0xFFFFFFFFu,
            haveStaged ? static_cast<unsigned>(staged.parts) : 0xFFFFFFFFu,
            haveStaged ? static_cast<unsigned>(staged.face) : 0xFFFFu,
            haveStaged ? static_cast<unsigned>(staged.state) : 0xFFu
        );

        lastLive = live;
        lastSlot = slot;
        lastStaged = staged;
        haveLast = true;
    }

    if (haveLive &&
        !IsCustomLiveState(live) &&
        GetPendingCustomSuitDevelopId() == 0)
    {
        g_CustomFaceEquipLoadCompleted = false;
        g_IdleFaceEquipRepairIssued = false;
        ClearActiveCustomSuit();
    }
}

bool Install_UpdatePartsStatusProbe_Hook()
{
    if (g_Installed)
    {
        Log("[Hook] UpdatePartsStatusProbe: already installed\n");
        return true;
    }

    if (!ResolveQuarkApi())
    {
        Log("[Hook] UpdatePartsStatusProbe: failed to resolve GetQuarkSystemTable\n");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.UpdatePartsStatus);
    if (!target)
    {
        Log("[Hook] UpdatePartsStatusProbe: failed to resolve UpdatePartsStatus\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkUpdatePartsStatus),
        reinterpret_cast<void**>(&g_OrigUpdatePartsStatus)
    );

    Log("[Hook] UpdatePartsStatusProbe: %s\n", ok ? "OK" : "FAIL");
    g_Installed = ok;
    return ok;
}

bool Uninstall_UpdatePartsStatusProbe_Hook()
{
    if (!g_Installed)
        return true;

    if (void* target = ResolveGameAddress(gAddr.UpdatePartsStatus))
        DisableAndRemoveHook(target);

    g_OrigUpdatePartsStatus = nullptr;
    g_GetQuarkSystemTable = nullptr;
    g_Installed = false;
    g_CustomFaceEquipLoadCompleted = false;
    g_IdleFaceEquipRepairIssued = false;

    Log("[Hook] UpdatePartsStatusProbe: removed\n");
    return true;
}