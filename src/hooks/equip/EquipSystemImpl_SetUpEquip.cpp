#include <pch.h>
#include "EquipSystemImpl_SetUpEquip.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "EquipIdCompression.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using SetUpEquipFn = std::uint32_t*(__fastcall*)(void* self, std::uint32_t* outHandle,
                                                     std::uint32_t equipId, std::uint32_t option);

    const std::uint8_t kPrologue[] = {
        0x44, 0x89, 0x4C, 0x24, 0x20, 0x53, 0x41, 0x54, 0x41, 0x55,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20
    };

    using EquipIdTableCall_t = std::uint32_t(__fastcall*)(void* equipIdTable, std::uint32_t equipId);

    constexpr std::size_t   kEquipSystemIdTable = 0x08;   // EquipIdTableImpl: slot 0 equip type, slot 2 root parameter id
    constexpr std::size_t   kTableEquipType     = 0;
    constexpr std::size_t   kTableRootParameter = 2;
    constexpr std::uint32_t kFirstGunType       = 1;
    constexpr std::uint32_t kLastGunType        = 8;

    SetUpEquipFn g_Orig = nullptr;
    void*        g_Addr = nullptr;
    bool         g_WarnedNoGunRow[EquipIdCompression::kNoEquipId + 1] = {};

    // a gun type goes to the gun setup, which reads its parameter row unchecked: root parameter id 0 reads address 0
    bool IsGunWithoutRowSEH(void* self, std::uint32_t engineId)
    {
        if (engineId < static_cast<std::uint32_t>(EquipIdCompression::kExtendedAllocFirst)
            || engineId >= static_cast<std::uint32_t>(EquipIdCompression::kNoEquipId))
            return false;
        __try
        {
            void* table = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(self) + kEquipSystemIdTable);
            if (!table)
                return false;
            void** calls = *static_cast<void***>(table);
            const std::uint32_t type = reinterpret_cast<EquipIdTableCall_t>(calls[kTableEquipType])(table, engineId);
            if (type < kFirstGunType || type > kLastGunType)
                return false;
            return reinterpret_cast<EquipIdTableCall_t>(calls[kTableRootParameter])(table, engineId) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uintptr_t SetUpEquipAddr()
    {
        return gAddr.EquipSystemImpl_SetUpEquip;
    }

    std::uint32_t* __fastcall hkSetUpEquip(void* self, std::uint32_t* outHandle,
                                           std::uint32_t equipId, std::uint32_t option)
    {
        equip::NoteEquipSystem(self);
        const auto realId   = equip::ResolveReal(static_cast<std::int32_t>(equipId));
        const auto engineId = equip::ToEngine(realId);
        if (self && IsGunWithoutRowSEH(self, static_cast<std::uint32_t>(engineId)))
        {
            if (!g_WarnedNoGunRow[engineId])
            {
                g_WarnedNoGunRow[engineId] = true;
                LogWarn("EquipAlias: equipId 0x%X (real 0x%X, leased %d) is a gun without a gun parameter row, set up as an empty slot",
                    static_cast<unsigned>(engineId), static_cast<unsigned>(realId), equip::IsLeasedAlias(engineId) ? 1 : 0);
            }
            return g_Orig(self, outHandle, static_cast<std::uint32_t>(EquipIdCompression::kNoEquipId), option);
        }
        return g_Orig(self, outHandle, static_cast<std::uint32_t>(engineId), option);
    }
}

bool Install_EquipSystemImpl_SetUpEquip()
{
    const std::uintptr_t addr = SetUpEquipAddr();
    if (addr == 0)
        return true;

    void* fn = ResolveGameAddress(addr);
    const HookEntry entry = CheckHookEntry(fn, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("EquipAlias: SetUpEquip prologue mismatch on %s, equipIds above 0x7FF stay off",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkSetUpEquip),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("EquipAlias: SetUpEquip hook failed on %s, equipIds above 0x7FF stay off",
            GetGameBuildName(gGameBuild));
        return false;
    }
    g_Addr = fn;
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("EquipAlias: SetUpEquip", fn);
    equip::NoteGateInstalled(equip::AliasGate::SetUpEquip, true);
    return true;
}

void Uninstall_EquipSystemImpl_SetUpEquip()
{
    equip::NoteGateInstalled(equip::AliasGate::SetUpEquip, false);
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
