#include <pch.h>
#include "EquipControllerImpl_UpdateBattery.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "EquipAliasLease.h"
#include "ItemCount.h"
#include "LuaBroadcaster.h"
#include "../ui/EquipCrossEvCall_SpecialItemAct.h"
#include "../player/Player2Impl_CallVoice.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using UpdateBattery_t       = void(__fastcall*)(void* equipController, std::uint32_t playerIndex, std::uint8_t powerCut);
    using SetCurrentItemIndex_t = void(__fastcall*)(void* equipController, std::uint32_t playerIndex, std::uint32_t itemIndex, std::uint32_t silent);
    using GetHudInfo_t          = std::uint8_t*(__fastcall*)(void* uiController);
    using PlayerEvent_t         = void(__fastcall*)(void* player, std::uint32_t objectIndex, std::uint32_t eventHash);
    using RideValue_t           = float(__fastcall*)(void* ride, std::uint32_t playerIndex);
    using StanceLocked_t        = bool(__fastcall*)(void* stance);
    using Stance_t              = int(__fastcall*)(void* stance);
    using IsFobMode_t           = bool(__fastcall*)();
    using HasBatteryEquip_t     = std::uint32_t(__fastcall*)(void* equipController);

    constexpr float         kFullCharge         = 54000.0f;   // the engine's battery scale
    constexpr std::int32_t  kFullChargeUnits    = 54000;
    constexpr std::uint16_t kEquipIdMask        = 0x7FF;
    constexpr int           kItemSlots          = 8;
    constexpr std::uint32_t kBatteryEmptyEvent  = 0xC7EC1F66;

    constexpr std::size_t kControllerContext    = 0x18;
    constexpr std::size_t kControllerClock      = 0x30;
    constexpr std::size_t kControllerStance     = 0x38;
    constexpr std::size_t kClockDeltaSeconds    = 0x10;
    constexpr std::size_t kContextPlayers       = 0x50;
    constexpr std::size_t kContextRecords       = 0x60;
    constexpr std::size_t kContextObjectIndex   = 0x70;
    constexpr std::size_t kContextPlayerSlot    = 0x78;
    constexpr std::size_t kContextUi            = 0xD8;
    constexpr std::size_t kContextRide          = 0x138;
    constexpr std::size_t kRecordsBase          = 0xC0;
    constexpr std::size_t kRecordStride         = 0x3A;
    constexpr std::size_t kRecordItems          = 0x1C;
    constexpr std::size_t kRecordCurrentItem    = 0x37;
    constexpr std::size_t kPlayerMotion         = 0x78;
    constexpr std::size_t kMotionFirstIndex     = 0x8;
    constexpr std::size_t kMotionVelocities     = 0x268;
    constexpr std::size_t kMotionStates         = 0x2D8;

    constexpr std::size_t kSetCurrentItemIndexSlot = 0x68;
    constexpr std::size_t kUiGetHudInfoSlot        = 0x190;
    constexpr std::size_t kPlayerEventSlot         = 0x20;
    constexpr std::size_t kRideFirstSlot           = 0x18;
    constexpr std::size_t kRideSecondSlot          = 0x10;
    constexpr std::size_t kStanceLockedSlot        = 0x68;
    constexpr std::size_t kStanceSlot              = 0x0;
    constexpr std::size_t kHasBatteryEquipSlot     = 0xF8;

    const std::uint8_t kHasBatteryEquipCode[] = { 0x33, 0xC0, 0x39, 0x41, 0x54, 0x0F, 0x95, 0xC0, 0xC3 }; // xor eax,eax / cmp [rcx+0x54],eax / setne al / ret: +0x54 holds the vanilla battery items the player carries, a UAV EMP only hits a player who has one

    constexpr std::size_t  kInfoCounts        = 0x78;
    constexpr std::size_t  kInfoStateIds      = 0x98;
    constexpr std::size_t  kInfoStateFlags    = 0xB8;
    constexpr std::size_t  kInfoGaugeNow      = 0x10C;
    constexpr std::size_t  kInfoGaugeMax      = 0x12C;
    constexpr std::size_t  kInfoGaugeIds      = 0x274;
    constexpr int          kInfoSlots         = 8;
    constexpr int          kInfoGaugeIdCount  = 20;
    constexpr std::uint8_t kNoUseBit          = 0x01;
    constexpr std::uint8_t kUseRequestedBit   = 0x04;

    struct Battery
    {
        float seconds        = 0.0f;
        float rechargeStill  = ItemBattery::kRechargeStillSeconds;
        float rechargeMoving = ItemBattery::kRechargeMovingSeconds;
        float charge         = kFullCharge;
        std::uint8_t gaugeLength = 0;   // 0 = from seconds
    };

    struct PlayerItems
    {
        std::uint16_t ids[kItemSlots] = {};   // real ids, what mods register
        std::uint16_t hud[kItemSlots] = {};   // the ids the game holds (a wide id's alias), what the HUD compares
        int           current         = -1;
        float         deltaSeconds    = 0.0f;
    };

    struct GaugeRow
    {
        std::uint16_t equipId  = 0;
        int           itemSlot = 0;
        std::int32_t  units    = 0;
    };

    std::mutex                       g_Lock;
    std::map<std::uint16_t, Battery> g_Batteries;
    std::map<std::uint16_t, bool>    g_SwitchedOn;   // set by Lua: on drains while carried, off never drains; unset drains while selected

    UpdateBattery_t g_Orig      = nullptr;
    void*           g_Target    = nullptr;
    IsFobMode_t     g_IsFobMode = nullptr;

    HasBatteryEquip_t  g_OrigHasBattery  = nullptr;
    void**             g_HasBatterySlot  = nullptr;
    bool               g_HasBatteryTried = false;
    std::atomic<void*> g_CarryController{ nullptr };
    std::atomic<bool>  g_CarriesBattery{ false };

    template <typename Fn>
    Fn Virtual(void* self, std::size_t slotOffset)
    {
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(self))[slotOffset / sizeof(void*)]);
    }

    float Clamp01(float value)
    {
        if (!(value >= 0.0f))
            return 0.0f;
        return value > 1.0f ? 1.0f : value;
    }

    std::uint8_t* Context(std::uint8_t* controller)
    {
        return *reinterpret_cast<std::uint8_t**>(controller + kControllerContext);
    }

    bool InFobSEH()
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

    void ToRealIds(PlayerItems& items)
    {
        for (int j = 0; j < kItemSlots; ++j)
        {
            items.hud[j] = items.ids[j];
            if (items.ids[j] != 0)
                items.ids[j] = static_cast<std::uint16_t>(equip::FromEngine(items.ids[j]));
        }
    }

    bool ReadPlayerItemsSEH(std::uint8_t* controller, std::uint32_t playerIndex, PlayerItems& out)
    {
        __try
        {
            auto* records = *reinterpret_cast<std::uint8_t**>(Context(controller) + kContextRecords);
            auto* record  = *reinterpret_cast<std::uint8_t**>(records + kRecordsBase) + playerIndex * kRecordStride;
            for (int j = 0; j < kItemSlots; ++j)
                out.ids[j] = *reinterpret_cast<std::uint16_t*>(record + kRecordItems + j * 2) & kEquipIdMask;
            out.current = record[kRecordCurrentItem] >> 4;
            auto* clock = *reinterpret_cast<std::uint8_t**>(controller + kControllerClock);
            out.deltaSeconds = static_cast<float>(*reinterpret_cast<double*>(clock + kClockDeltaSeconds));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool PlayerObjectSEH(std::uint8_t* controller, std::uint32_t playerIndex, std::uint8_t*& player, std::uint32_t& objectIndex)
    {
        __try
        {
            auto* context = Context(controller);
            objectIndex = (*reinterpret_cast<std::uint16_t**>(context + kContextObjectIndex))[playerIndex];
            const std::uint8_t slot = (*reinterpret_cast<std::uint8_t**>(context + kContextPlayerSlot))[playerIndex];
            if (objectIndex == 0xFFFF || slot == 0xFF)
                return false;
            player = (*reinterpret_cast<std::uint8_t***>(context + kContextPlayers))[slot];
            return player != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // vanilla kinetic charging, copied from UpdateBattery: 0 standing still .. 1 running, which moves the recharge time from rechargeSeconds to rechargeSecondsMoving
    float KineticFactorSEH(std::uint8_t* controller, std::uint32_t playerIndex)
    {
        std::uint8_t* player = nullptr;
        std::uint32_t objectIndex = 0;
        if (!PlayerObjectSEH(controller, playerIndex, player, objectIndex))
            return 0.0f;
        __try
        {
            auto* motion = *reinterpret_cast<std::uint8_t**>(player + kPlayerMotion);
            const std::uint32_t index = objectIndex - *reinterpret_cast<std::int32_t*>(motion + kMotionFirstIndex);
            const std::uint8_t state = (*reinterpret_cast<std::uint8_t**>(motion + kMotionStates))[index];
            switch (state)
            {
            case 0x02: case 0x04: case 0x05: case 0x07: case 0x0E: case 0x14: case 0x15: case 0x16:
                return 0.0f;
            }
            const float* velocity = reinterpret_cast<const float*>(*reinterpret_cast<std::uint8_t**>(motion + kMotionVelocities) + index * 0x10);
            const float speed = Clamp01(std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]) * 0.06f);

            void* ride   = *reinterpret_cast<void**>(Context(controller) + kContextRide);
            void* stance = *reinterpret_cast<void**>(controller + kControllerStance);
            bool stanceCounts = !(Virtual<RideValue_t>(ride, kRideFirstSlot)(ride, playerIndex) >= 0.0f);
            if (!stanceCounts)
                stanceCounts = !(Virtual<RideValue_t>(ride, kRideSecondSlot)(ride, playerIndex) > 0.0f)
                               && !Virtual<StanceLocked_t>(stance, kStanceLockedSlot)(stance);
            float stanceTerm = 0.0f;
            if (stanceCounts)
            {
                switch (Virtual<Stance_t>(stance, kStanceSlot)(stance))
                {
                case 0:  stanceTerm = 1.0f; break;
                case 1:  stanceTerm = 0.5f; break;
                case 2:
                case 3:
                case 4:  stanceTerm = 0.1f; break;
                default: break;
                }
            }
            if (state == 0x39)
                stanceTerm *= 0.3f;
            return Clamp01((stanceTerm * 0.28f + speed) * 0.78125f);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0.0f;
        }
    }

    std::uint8_t* HudInfoSEH(std::uint8_t* controller)
    {
        __try
        {
            void* ui = *reinterpret_cast<void**>(Context(controller) + kContextUi);
            return Virtual<GetHudInfo_t>(ui, kUiGetHudInfoSlot)(ui);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void WriteGaugeSEH(std::uint8_t* info, int itemSlot, std::int32_t equipId, std::int32_t units)
    {
        __try
        {
            auto* gaugeIds = reinterpret_cast<std::int32_t*>(info + kInfoGaugeIds);
            int g = 0;
            while (g < kInfoGaugeIdCount && gaugeIds[g] != equipId && gaugeIds[g] != 0)
                ++g;
            if (g < kInfoGaugeIdCount && gaugeIds[g] == 0)
                gaugeIds[g] = equipId; // the HUD draws a gauge instead of a count only for ids in this list

            reinterpret_cast<std::int32_t*>(info + kInfoGaugeNow)[itemSlot] = units; // same row the engine's own battery write uses: the player's item slot
            reinterpret_cast<std::int32_t*>(info + kInfoGaugeMax)[itemSlot] = kFullChargeUnits;

            auto* stateIds = reinterpret_cast<std::int32_t*>(info + kInfoStateIds);
            int s = 0;
            while (s < kInfoSlots && stateIds[s] != equipId)
                ++s;
            if (s == kInfoSlots)
            {
                s = 0;
                while (s < kInfoSlots && stateIds[s] != -1)
                    ++s;
                if (s == kInfoSlots)
                    return;
                stateIds[s] = equipId;
            }
            if (units == 0)
                info[kInfoStateFlags + s] |= kNoUseBit;
            else
                info[kInfoStateFlags + s] &= static_cast<std::uint8_t>(~kNoUseBit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void WriteCountSEH(std::uint8_t* info, int itemSlot, std::int32_t equipId, std::int32_t count, bool hasBattery)
    {
        __try
        {
            reinterpret_cast<std::int32_t*>(info + kInfoCounts)[itemSlot] = count; // the number the item wheel and the HUD print for this item slot; the engine leaves -1 (no number) for ids outside 0x1C2-0x1F1

            auto* stateIds = reinterpret_cast<std::int32_t*>(info + kInfoStateIds);
            int s = 0;
            while (s < kInfoSlots && stateIds[s] != equipId)
                ++s;
            if (s == kInfoSlots)
            {
                if (count != 0)
                    return;
                s = 0;
                while (s < kInfoSlots && stateIds[s] != -1)
                    ++s;
                if (s == kInfoSlots)
                    return;
                stateIds[s] = equipId;
            }
            info[kInfoStateFlags + s] &= static_cast<std::uint8_t>(~kUseRequestedBit); // the item wheel sets it on a Use press and skips every later press while it is set; the game clears it only for its own consumables
            if (count == 0)
                info[kInfoStateFlags + s] |= kNoUseBit;
            else if (!hasBattery) // a battery item's gauge row set or cleared the bit this frame
                info[kInfoStateFlags + s] &= static_cast<std::uint8_t>(~kNoUseBit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void SwitchItemOffSEH(std::uint8_t* controller, std::uint32_t playerIndex, bool playEmptyEvent, bool unequip)
    {
        std::uint8_t* player = nullptr;
        std::uint32_t objectIndex = 0;
        const bool havePlayer = playEmptyEvent && PlayerObjectSEH(controller, playerIndex, player, objectIndex);
        __try
        {
            if (havePlayer)
                Virtual<PlayerEvent_t>(player, kPlayerEventSlot)(player, objectIndex, kBatteryEmptyEvent);
            if (unequip)
                Virtual<SetCurrentItemIndex_t>(controller, kSetCurrentItemIndexSlot)(controller, playerIndex, 0, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint32_t __fastcall hkHasBatteryEquip(void* equipController)
    {
        if ((g_OrigHasBattery(equipController) & 0xFF) != 0)
            return 1;
        return equipController == g_CarryController.load() && g_CarriesBattery.load()
               && ItemBattery::IsActiveHere() ? 1 : 0;
    }

    void SwapHasBatteryEquip(std::uint8_t* controller)
    {
        g_HasBatteryTried = true;
        void** slot   = *reinterpret_cast<void***>(controller) + kHasBatteryEquipSlot / sizeof(void*);
        void*  target = nullptr;
        if (!ReadEntryBytesSEH(slot, &target, sizeof(target)))
            target = nullptr;

        const HookEntry entry = CheckHookEntry(target, kHasBatteryEquipCode, sizeof(kHasBatteryEquipCode));
        if (entry == HookEntry::Unknown)
        {
            LogError("ItemBattery: battery-item check does not match %s, UAV EMP ignores custom battery items",
                GetGameBuildName(gGameBuild));
            return;
        }
        if (entry == HookEntry::ForeignHook)
            LogHookedOnTop("ItemBattery: battery-item check", target);

        if (!SwapVtableSlot(slot, reinterpret_cast<void*>(&hkHasBatteryEquip), reinterpret_cast<void**>(&g_OrigHasBattery)))
        {
            LogError("ItemBattery: battery-item check swap failed, UAV EMP ignores custom battery items");
            return;
        }
        g_HasBatterySlot = slot;
    }

    bool Tick(std::uint8_t* controller, std::uint32_t playerIndex, std::uint8_t powerCut)
    {
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            if (g_Batteries.empty())
                return false;
        }
        if (!controller || !ItemBattery::IsActiveHere())
            return false;

        PlayerItems items;
        if (!ReadPlayerItemsSEH(controller, playerIndex, items))
            return false;
        ToRealIds(items);
        const float deltaSeconds = items.deltaSeconds > 0.0f ? items.deltaSeconds : 0.0f;

        GaugeRow rows[kItemSlots];
        int rowCount = 0;
        std::uint16_t emptiedId = 0;
        bool emptiedSelected = false;
        float kinetic = -1.0f;
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            for (int j = 0; j < kItemSlots; ++j)
            {
                auto it = g_Batteries.find(items.ids[j]);
                if (it == g_Batteries.end())
                    continue;
                Battery& battery = it->second;
                const bool selected = j == items.current;
                const auto switched = g_SwitchedOn.find(items.ids[j]);
                const bool draining = switched == g_SwitchedOn.end() ? selected : switched->second;
                if (powerCut != 0)
                {
                    battery.charge = 0.0f;
                }
                else if (draining)
                {
                    battery.charge -= kFullCharge / battery.seconds * deltaSeconds;
                    if (battery.charge < 0.0f)
                        battery.charge = 0.0f;
                }
                else
                {
                    if (kinetic < 0.0f)
                        kinetic = KineticFactorSEH(controller, playerIndex);
                    battery.charge += kFullCharge / ((1.0f - kinetic) * battery.rechargeStill + kinetic * battery.rechargeMoving) * deltaSeconds;
                    if (battery.charge > kFullCharge)
                        battery.charge = kFullCharge;
                }
                rows[rowCount].equipId  = items.hud[j];
                rows[rowCount].itemSlot = j;
                rows[rowCount].units    = static_cast<std::int32_t>(std::ceil(battery.charge));
                ++rowCount;
                if (draining && battery.charge <= 0.0f)
                {
                    emptiedId = items.ids[j];
                    emptiedSelected = selected;
                    if (switched != g_SwitchedOn.end())
                        switched->second = false;
                }
            }
        }

        if (std::uint8_t* info = HudInfoSEH(controller))
            for (int r = 0; r < rowCount; ++r)
                WriteGaugeSEH(info, rows[r].itemSlot, rows[r].equipId, rows[r].units);

        if (emptiedId != 0)
        {
            SwitchItemOffSEH(controller, playerIndex, powerCut == 0, emptiedSelected);
            V_FrameWork::EmitMessage("Player", "ItemBatteryEmpty", static_cast<int>(emptiedId));
        }
        return rowCount > 0;
    }

    void TickCounts(std::uint8_t* controller, const PlayerItems& items)
    {
        if (!ItemCount::Any() || !ItemBattery::IsActiveHere())
            return;
        ItemCount::SetCarried(items.ids, kItemSlots);
        std::uint8_t* info = nullptr;
        for (int j = 0; j < kItemSlots; ++j)
        {
            int count = 0;
            int max = 0;
            if (items.ids[j] == 0 || !ItemCount::Get(items.ids[j], count, max))
                continue;
            if (!info)
                info = HudInfoSEH(controller);
            if (!info)
                return;
            bool hasBattery = false;
            {
                std::lock_guard<std::mutex> lock(g_Lock);
                hasBattery = g_Batteries.find(items.ids[j]) != g_Batteries.end();
            }
            WriteCountSEH(info, j, items.hud[j], count, hasBattery);
        }
    }

    void __fastcall hkUpdateBattery(void* equipController, std::uint32_t playerIndex, std::uint8_t powerCut)
    {
        g_Orig(equipController, playerIndex, powerCut);
        auto* controller = static_cast<std::uint8_t*>(equipController);
        PlayerItems selected;
        const bool read = controller && ReadPlayerItemsSEH(controller, playerIndex, selected);
        if (read)
            ToRealIds(selected);
        ItemUse::OnPlayerFrame(read && selected.current >= 0 && selected.current < kItemSlots ? selected.ids[selected.current] : 0);
        SayCatchBreathAfterSprint();
        const bool carries = Tick(controller, playerIndex, powerCut);
        if (read)
            TickCounts(controller, selected);
        if (carries)
        {
            if (!g_HasBatteryTried)
                SwapHasBatteryEquip(controller);
            g_CarryController.store(equipController);
        }
        g_CarriesBattery.store(carries);
    }
}

void ItemBattery::Set(std::uint16_t equipId, float seconds, float rechargeStillSeconds, float rechargeMovingSeconds, std::uint8_t gaugeLength)
{
    std::lock_guard<std::mutex> lock(g_Lock);
    Battery& battery = g_Batteries[equipId];
    battery.seconds        = seconds;
    battery.rechargeStill  = rechargeStillSeconds;
    battery.rechargeMoving = rechargeMovingSeconds;
    battery.gaugeLength    = gaugeLength;
}

bool ItemBattery::GetCharge(std::uint16_t equipId, float& fraction)
{
    std::lock_guard<std::mutex> lock(g_Lock);
    auto it = g_Batteries.find(equipId);
    if (it == g_Batteries.end())
        return false;
    fraction = it->second.charge / kFullCharge;
    return true;
}

bool ItemBattery::SetCharge(std::uint16_t equipId, float fraction)
{
    std::lock_guard<std::mutex> lock(g_Lock);
    auto it = g_Batteries.find(equipId);
    if (it == g_Batteries.end())
        return false;
    it->second.charge = Clamp01(fraction) * kFullCharge;
    return true;
}

void ItemBattery::SetOn(std::uint16_t equipId, int state)
{
    std::lock_guard<std::mutex> lock(g_Lock);
    if (state < 0)
        g_SwitchedOn.erase(equipId);
    else
        g_SwitchedOn[equipId] = state != 0;
}

int ItemBattery::IsOn(std::uint16_t equipId)
{
    std::lock_guard<std::mutex> lock(g_Lock);
    const auto it = g_SwitchedOn.find(equipId);
    return it == g_SwitchedOn.end() ? -1 : (it->second ? 1 : 0);
}

void ItemBattery::RefillAll()
{
    std::lock_guard<std::mutex> lock(g_Lock);
    for (auto& entry : g_Batteries)
        entry.second.charge = kFullCharge;
}

bool ItemBattery::IsActiveHere()
{
    return g_IsFobMode && !MissionCodeGuard::ShouldBypassHooks() && !InFobSEH();
}

bool ItemBattery::GaugeLevel(int equipId, std::uint8_t& level)
{
    if (equipId <= 0 || equipId > kEquipIdMask)
        return false;
    float seconds = 0.0f;
    std::uint8_t gaugeLength = 0;
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        auto it = g_Batteries.find(static_cast<std::uint16_t>(equipId));
        if (it == g_Batteries.end())
            return false;
        seconds = it->second.seconds;
        gaugeLength = it->second.gaugeLength;
    }
    if (!IsActiveHere())
        return false;
    if (gaugeLength != 0)
        level = gaugeLength;
    else
        level = seconds <= 180.0f ? 1 : seconds <= 360.0f ? 2 : 3; // NVG's bar lengths: 3, 6 and 9 minutes
    return true;
}

bool Install_EquipControllerImpl_UpdateBattery_Hook()
{
    g_IsFobMode = reinterpret_cast<IsFobMode_t>(ResolveGameAddress(gAddr.Barrier_IsFobMode));
    g_Target    = ResolveGameAddress(gAddr.EquipControllerImpl_UpdateBattery);
    if (!g_IsFobMode || !g_Target)
    {
        LogError("ItemBattery: UpdateBattery or IsFobMode address missing for this build, custom items have no battery");
        g_Target = nullptr;
        return false;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkUpdateBattery),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ItemBattery: UpdateBattery hook failed, custom items have no battery");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_EquipControllerImpl_UpdateBattery_Hook()
{
    g_CarriesBattery.store(false);
    if (g_HasBatterySlot)
        RestoreVtableSlot(g_HasBatterySlot, reinterpret_cast<void*>(g_OrigHasBattery), reinterpret_cast<void*>(&hkHasBatteryEquip));
    g_HasBatterySlot  = nullptr;
    g_HasBatteryTried = false;
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
