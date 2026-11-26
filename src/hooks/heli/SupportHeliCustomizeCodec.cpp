#include <pch.h>
#include "SupportHeliCustomizeCodec.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "EquipDevelopControllerImpl_GetHeliPartsId.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SupportHeliWeapon.h"
#include "log.h"

namespace
{
    using Encode_t      = std::uint32_t(__fastcall*)(void* codec, const std::int32_t* fields);
    using Decode_t      = std::int32_t*(__fastcall*)(void* codec, std::int32_t* fields, std::uint32_t packed);
    using PanelNumber_t = std::int32_t(__fastcall*)(void* codec, std::int32_t subWeaponLevel);

    constexpr std::size_t kCodecSlots      = 8;
    constexpr std::size_t kSlotEncode      = 0;
    constexpr std::size_t kSlotDecode      = 1;
    constexpr std::size_t kSlotPanelNumber = 6;   // the ammo number the menus print for a sub weapon level
    constexpr std::size_t kFieldSubWeapon  = 1;

    alignas(16) void* g_Table[kCodecSlots + 1] = {};   // [0] is the class info slot that sits before every vtable

    void**        g_CodecVptr       = nullptr;
    void**        g_VanillaTable    = nullptr;
    Encode_t      g_OrigEncode      = nullptr;
    Decode_t      g_OrigDecode      = nullptr;
    PanelNumber_t g_OrigPanelNumber = nullptr;
    bool          g_Installed       = false;
    std::atomic<bool> g_SwapPending{ false };

    std::uint32_t __fastcall Encode(void* codec, const std::int32_t* fields)
    {
        const std::uint32_t packed = g_OrigEncode(codec, fields);
        if (MissionCodeGuard::ShouldBypassHooks())
            return packed;
        const std::uint8_t menuLevel = static_cast<std::uint8_t>(fields[kFieldSubWeapon]);
        const std::uint32_t customId = SupportHeliWeapon::MenuChoiceForSortie(menuLevel);
        return (packed & SupportHeliWeapon::kVanillaFieldMask) | (customId << SupportHeliWeapon::kCustomIdShift);
    }

    std::int32_t* __fastcall Decode(void* codec, std::int32_t* fields, std::uint32_t packed)
    {
        std::int32_t* result = g_OrigDecode(codec, fields, packed);
        std::uint8_t customId = 0;
        if (!MissionCodeGuard::ShouldBypassHooks())
            customId = static_cast<std::uint8_t>((packed >> SupportHeliWeapon::kCustomIdShift) & SupportHeliWeapon::kCustomIdMask);

        SupportHeliWeapon::Weapon weapon{};
        if (SupportHeliWeapon::FindByCustomId(customId, weapon) && weapon.developId != 0)
        {
            fields[kFieldSubWeapon] = weapon.fireLevel;
            SupportHeliWeapon::SetSortieWeapon(customId);
        }
        else
        {
            SupportHeliWeapon::SetSortieWeapon(0);
        }
        return result;
    }

    std::int32_t __fastcall PanelNumber(void* codec, std::int32_t subWeaponLevel)
    {
        const std::int32_t vanilla = g_OrigPanelNumber(codec, subWeaponLevel);
        std::int32_t custom = 0;
        if (MissionCodeGuard::ShouldBypassHooks() || !HeliParts_TakePanelNumber(subWeaponLevel, custom))
            return vanilla;
        return custom;
    }

    void SwapCodec()
    {
        void* before = InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(g_CodecVptr), &g_Table[1], g_VanillaTable);
        if (before == g_VanillaTable || before == &g_Table[1])
        {
            g_Installed = true;
            return;
        }
        LogError("SupportHeliWeapon: Pequod customize codec was replaced by another module, custom Pequod weapons are not taken into missions");
    }
}

bool Install_SupportHeliCustomizeCodec_Hook()
{
    if (g_Installed || g_SwapPending)
        return true;
    void** codec = static_cast<void**>(gAddr.SupportHeli_CustomizeCodec ? ResolveGameAddress(gAddr.SupportHeli_CustomizeCodec) : nullptr);
    void** vanilla = static_cast<void**>(gAddr.SupportHeli_CustomizeCodecVtable ? ResolveGameAddress(gAddr.SupportHeli_CustomizeCodecVtable) : nullptr);
    if (!codec || !vanilla)
    {
        LogError("SupportHeliWeapon: Pequod customize codec address missing on this build, custom Pequod weapons are not taken into missions");
        return false;
    }

    std::memcpy(g_Table, vanilla - 1, sizeof(g_Table));
    g_OrigEncode      = reinterpret_cast<Encode_t>(vanilla[kSlotEncode]);
    g_OrigDecode      = reinterpret_cast<Decode_t>(vanilla[kSlotDecode]);
    g_OrigPanelNumber = reinterpret_cast<PanelNumber_t>(vanilla[kSlotPanelNumber]);
    g_Table[1 + kSlotEncode]      = reinterpret_cast<void*>(&Encode);
    g_Table[1 + kSlotDecode]      = reinterpret_cast<void*>(&Decode);
    g_Table[1 + kSlotPanelNumber] = reinterpret_cast<void*>(&PanelNumber);
    g_CodecVptr    = codec;
    g_VanillaTable = vanilla;

    if (*codec != vanilla)
    {
        g_SwapPending = true;
        return true;
    }
    SwapCodec();
    return true;
}

void SupportHeliCustomizeCodec_SwapWhenReady()
{
    if (!g_SwapPending.exchange(false))
        return;
    SwapCodec();
}

void Uninstall_SupportHeliCustomizeCodec_Hook()
{
    g_SwapPending = false;
    if (!g_Installed)
        return;
    InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(g_CodecVptr), g_VanillaTable, &g_Table[1]);
    g_Installed = false;
}
