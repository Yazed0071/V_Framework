#include "pch.h"
#include "Player2UtilityImpl_GetPickableInfosOfAmmoBox.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SupportStockLease.h"
#include "SupportWeaponRegistry.h"
#include "V_FrameWorkState.h"
#include "log.h"

namespace
{
    using GetPickableInfosOfAmmoBox_t = void(__fastcall*)(void* utility, const std::uint8_t* stocks, std::uint16_t* infos);

    constexpr int           kSupportLaneFirst   = 6;          // stocks[6..13] = support slot stock ids
    constexpr int           kSupportSlotCount   = 8;
    constexpr int           kInfoWords          = 8;          // PickableInfo = 16 bytes
    constexpr int           kSupportInfoIndex   = 6;          // record 6 = EQP_AB_Support box
    constexpr std::uint16_t kAmmoBoxSupport     = 0x1B5;
    constexpr std::uint16_t kNoEquipId          = 0x7FF;
    constexpr std::uint16_t kInfoFlags          = 0x50;       // set on every record the original fills
    constexpr std::int32_t  kSupportStockOffset = 0xEC;       // stock id = equipId - 0xEC below 0x600

    GetPickableInfosOfAmmoBox_t g_Orig   = nullptr;
    void*                       g_Target = nullptr;

    bool IsCustomSupportStock(std::uint8_t stock)
    {
        if (SupportStockLease::Live())
            return SupportStockLease::EquipOfLentByte(stock) != 0;
        const std::int32_t equipId = stock + kSupportStockOffset;
        return V_FrameWorkState::IsClaimedEquipId(equipId)
            && !SupportWeapon::IsVanillaSupportWeaponEquipId(equipId);
    }

    void AddCustomSupportStocks(const std::uint8_t* stocks, std::uint16_t* infos)
    {
        std::uint16_t* info = infos + kSupportInfoIndex * kInfoWords;
        auto* lane = reinterpret_cast<std::uint8_t*>(info + 2);
        bool added = false;
        for (int i = 0; i < kSupportSlotCount; ++i)
        {
            const std::uint8_t stock = stocks[kSupportLaneFirst + i];
            if (stock == 0 || lane[i] == stock || !IsCustomSupportStock(stock))
                continue;
            lane[i] = stock;
            added = true;
        }
        if (!added)
            return;
        if ((info[0] & kNoEquipId) != kAmmoBoxSupport)
        {
            info[0] = kAmmoBoxSupport;
            info[6] = 0;
        }
        info[7] |= kInfoFlags;
    }

    void __fastcall hkGetPickableInfosOfAmmoBox(void* utility, const std::uint8_t* stocks, std::uint16_t* infos)
    {
        g_Orig(utility, stocks, infos);
        if (!stocks || !infos || MissionCodeGuard::ShouldBypassHooks())
            return;
        AddCustomSupportStocks(stocks, infos);
    }
}

bool Install_Player2UtilityImpl_GetPickableInfosOfAmmoBox_Hook()
{
    g_Target = ResolveGameAddress(gAddr.Player2UtilityImpl_GetPickableInfosOfAmmoBox);
    if (!g_Target)
    {
        LogError("SupportWeapon: ammo box address missing for this build, supply drops do not refill custom support weapons");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkGetPickableInfosOfAmmoBox),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SupportWeapon: ammo box hook failed, supply drops do not refill custom support weapons");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_Player2UtilityImpl_GetPickableInfosOfAmmoBox_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
