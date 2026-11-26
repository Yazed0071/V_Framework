#include <pch.h>
#include "ModelNodeMesh_Destruct.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using Destruct_t            = void* (__fastcall*)(void* mesh);
    using GetQuarkSystemTable_t = std::uint8_t* (__fastcall*)();
    using GetButtonMeshInfo_t   = std::uint8_t* (__fastcall*)(void* holder);

    constexpr std::uint8_t kDestructEntry[] =
    {
        0x48, 0x89, 0x4C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE,
        0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x5C, 0x24, 0x50, 0x48, 0x8B, 0xD9, 0x48, 0x8D, 0x05, 0x00, 0x00,
        0x00, 0x00, 0x48, 0x89, 0x01, 0x48, 0x8B, 0xB9, 0x90, 0x00, 0x00, 0x00,
    };
    constexpr std::uint8_t kDestructMask[] =
    {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
        0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };

    constexpr std::size_t kButtonMeshInfoPath[] = { 0x98, 0x40, 0x18 };   // quark system table -> ... -> the object that hands out ButtonMeshInfo
    constexpr std::size_t kGetButtonMeshInfoSlot = 0x5F8;

    constexpr std::size_t   kInfoCount  = 0x00;   // ButtonMeshInfo: how many slots are taken, then the slots
    constexpr std::size_t   kInfoSlots  = 0x08;
    constexpr std::size_t   kSlotSize   = 0x18;
    constexpr std::size_t   kSlotMesh   = 0x00;
    constexpr std::size_t   kSlotIcon   = 0x08;
    constexpr std::uint32_t kSlotCount  = 50;

    Destruct_t            g_Orig                = nullptr;
    GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    void*                 g_HookTarget          = nullptr;
    std::uintptr_t        g_GameImageBegin      = 0;
    std::uintptr_t        g_GameImageEnd        = 0;
    std::atomic<bool>     g_Told{ false };

    bool InGameImage(const void* address)
    {
        const auto value = reinterpret_cast<std::uintptr_t>(address);
        return value >= g_GameImageBegin && value < g_GameImageEnd;
    }

    bool ForgetMeshSEH(void* mesh, std::uint32_t& icon)
    {
        __try
        {
            std::uint8_t* link = g_GetQuarkSystemTable();
            for (const std::size_t offset : kButtonMeshInfoPath)
            {
                if (!link)
                    return false;
                link = *reinterpret_cast<std::uint8_t**>(link + offset);
            }
            if (!link)
                return false;
            void** vtable = *reinterpret_cast<void***>(link);
            if (!InGameImage(vtable) || !InGameImage(vtable[kGetButtonMeshInfoSlot / sizeof(void*)]))
                return false;   // the UI is not up yet, or already torn down
            std::uint8_t* info = reinterpret_cast<GetButtonMeshInfo_t>(vtable[kGetButtonMeshInfoSlot / sizeof(void*)])(link);
            if (!info)
                return false;

            std::uint32_t& count = *reinterpret_cast<std::uint32_t*>(info + kInfoCount);
            if (count == 0 || count > kSlotCount)
                return false;
            bool found = false;
            for (std::uint32_t i = 0; i < kSlotCount; ++i)
            {
                std::uint8_t* entry = info + kInfoSlots + i * kSlotSize;
                if (*reinterpret_cast<void**>(entry + kSlotMesh) != mesh)
                    continue;
                icon = *reinterpret_cast<std::uint32_t*>(entry + kSlotIcon);
                std::memset(entry, 0, kSlotSize);   // the game's own erase: empty the slot, one less taken
                if (count)
                    --count;
                found = true;
            }
            return found;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return false;
    }

    void* __fastcall hk_Destruct(void* mesh)
    {
        std::uint32_t icon = 0;
        if (mesh && ForgetMeshSEH(mesh, icon) && !g_Told.exchange(true))
            LogInfo("ButtonMeshNodeGuard: the game destroyed a prompt icon it still listed for the keyboard/pad switch "
                    "(icon 0x%08X); such icons now leave the list first", icon);
        return g_Orig(mesh);
    }
}

bool Install_ModelNodeMesh_Destruct_Hook()
{
    if (g_HookTarget)
        return true;

    void* target = ResolveGameAddress(gAddr.ModelNodeMesh_Destruct);
    g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.Fox_GetQuarkSystemTable));
    auto* image = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    if (!target || !g_GetQuarkSystemTable || !image)
    {
        LogError("ButtonMeshNodeGuard: no address on %s, switching keyboard/pad after a prompt icon was destroyed can crash",
                 GetGameBuildName(gGameBuild));
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    g_GameImageBegin = reinterpret_cast<std::uintptr_t>(image);
    g_GameImageEnd   = g_GameImageBegin + nt->OptionalHeader.SizeOfImage;

    const HookEntry entry = CheckHookEntryMasked(target, kDestructEntry, kDestructMask, sizeof(kDestructEntry));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("ButtonMeshNodeGuard: the mesh destructor is not the known code on %s (%s), switching keyboard/pad after "
                 "a prompt icon was destroyed can crash", GetGameBuildName(gGameBuild), found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("ButtonMeshNodeGuard: mesh destructor", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_Destruct), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ButtonMeshNodeGuard: mesh destructor hook failed at %p, switching keyboard/pad after a prompt icon was "
                 "destroyed can crash", target);
        return false;
    }
    g_HookTarget = target;
    return true;
}

void Uninstall_ModelNodeMesh_Destruct_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
