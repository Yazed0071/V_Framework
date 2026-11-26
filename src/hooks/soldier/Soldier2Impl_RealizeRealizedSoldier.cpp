#include <pch.h>
#include "Soldier2Impl_RealizeRealizedSoldier.h"

#include <Windows.h>
#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_map>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierRealizedSlot.h"
#include "log.h"
#include "../fox/gm/GameObjectInstance.h"

namespace
{
    using RealizeRealizedSoldier_t = void(__fastcall*)(std::uint8_t* soldiers, std::uint32_t soldierIndex, std::uint32_t realizedSlot);
    using GetRealizedSoldier_t     = std::uint8_t*(__fastcall*)(std::uint8_t* soldierCore, std::uint32_t realizedSlot);
    using SetMainModelMesh_t       = void(__fastcall*)(std::uint8_t* realized, std::uint32_t realizedSlot, std::uint64_t meshId, bool visible);
    using IsMainModelMeshVisible_t = bool(__fastcall*)(std::uint8_t* realized, std::uint32_t realizedSlot, std::uint64_t meshId);

    using MeshStates = std::map<std::uint32_t, bool>;

    constexpr std::size_t   kSoldierCore              = 0x20;   // Soldier2Impl view the realized soldiers hang off
    constexpr std::size_t   kGetRealizedSoldierSlot   = 0xF8;   // core vtable: RealizedSoldier2Impl holding a realized slot
    constexpr std::size_t   kSetMainModelMeshSlot     = 0xF0;   // RealizedSoldier2Impl::SetMainModelMeshVisiblityAtName
    constexpr std::size_t   kIsMainModelMeshSlot      = 0xF8;   // RealizedSoldier2Impl::IsMainModelMeshVisiblityAtName
    constexpr std::uint32_t kSoldierType              = 2;
    constexpr std::uint32_t kSoldierIndexMask         = 0x1FF;

    const std::uint8_t kPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
        0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x83, 0xC1, 0x20, 0x8B,
        0xFA, 0x41, 0x8B, 0xD0, 0x41, 0x8B, 0xE8, 0xFF, 0x90, 0xF8, 0x00, 0x00, 0x00
    };

    RealizeRealizedSoldier_t g_Orig = nullptr;
    void*                    g_Addr = nullptr;

    std::mutex                                     g_Mutex;
    std::unordered_map<std::uint32_t, MeshStates>  g_MeshesBySoldier;

    template <typename Fn>
    Fn VirtualAt(std::uint8_t* object, std::size_t slot)
    {
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(object))[slot / sizeof(void*)]);
    }

    std::uint8_t* RealizedSoldierSEH(std::uint8_t* soldierCore, std::uint32_t realizedSlot)
    {
        __try
        {
            return VirtualAt<GetRealizedSoldier_t>(soldierCore, kGetRealizedSoldierSlot)(soldierCore, realizedSlot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool SetMainModelMeshSEH(std::uint8_t* realized, std::uint32_t realizedSlot, std::uint32_t meshId, bool visible)
    {
        __try
        {
            VirtualAt<SetMainModelMesh_t>(realized, kSetMainModelMeshSlot)(realized, realizedSlot, meshId, visible);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsMainModelMeshVisibleSEH(std::uint8_t* realized, std::uint32_t realizedSlot, std::uint32_t meshId, bool& visible)
    {
        __try
        {
            visible = VirtualAt<IsMainModelMeshVisible_t>(realized, kIsMainModelMeshSlot)(realized, realizedSlot, meshId);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct RealizedSoldier
    {
        std::uint8_t* realized = nullptr;
        std::uint32_t slot     = SoldierRealizedSlot::kNotRealized;
    };

    RealizedSoldier FindRealized(std::uint32_t soldierId)
    {
        RealizedSoldier found;
        std::uint8_t* soldiers = GameObjectInstance::FindSoldier(soldierId).object;
        if (!soldiers)
            return found;
        std::uint8_t* soldierCore = soldiers + kSoldierCore;
        found.slot = SoldierRealizedSlot::Of(soldierCore, soldierId & kSoldierIndexMask);
        if (found.slot != SoldierRealizedSlot::kNotRealized)
            found.realized = RealizedSoldierSEH(soldierCore, found.slot);
        return found;
    }

    MeshStates StoredMeshes(std::uint32_t soldierIndex)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto it = g_MeshesBySoldier.find(soldierIndex);
        return it != g_MeshesBySoldier.end() ? it->second : MeshStates{};
    }

    void __fastcall hkRealizeRealizedSoldier(std::uint8_t* soldiers, std::uint32_t soldierIndex, std::uint32_t realizedSlot)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_Orig, soldiers, soldierIndex, realizedSlot);
        g_Orig(soldiers, soldierIndex, realizedSlot);

        const MeshStates meshes = StoredMeshes(soldierIndex);
        if (meshes.empty())
            return;
        std::uint8_t* realized = RealizedSoldierSEH(soldiers + kSoldierCore, realizedSlot);
        if (!realized)
            return;
        for (const auto& [meshId, visible] : meshes)
            SetMainModelMeshSEH(realized, realizedSlot, meshId, visible);
    }
}

namespace SoldierMesh
{
    bool SetVisible(std::uint32_t soldierId, std::uint32_t meshId, bool visible)
    {
        if ((soldierId >> 9) != kSoldierType || meshId == 0)
            return false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            g_MeshesBySoldier[soldierId & kSoldierIndexMask][meshId] = visible;
        }
        const RealizedSoldier soldier = FindRealized(soldierId);
        if (soldier.realized)
            SetMainModelMeshSEH(soldier.realized, soldier.slot, meshId, visible);
        return true;
    }

    bool IsVisible(std::uint32_t soldierId, std::uint32_t meshId, bool& visible)
    {
        if ((soldierId >> 9) != kSoldierType || meshId == 0)
            return false;
        const RealizedSoldier soldier = FindRealized(soldierId);
        if (soldier.realized)
            return IsMainModelMeshVisibleSEH(soldier.realized, soldier.slot, meshId, visible);

        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto it = g_MeshesBySoldier.find(soldierId & kSoldierIndexMask);
        if (it == g_MeshesBySoldier.end())
            return false;
        const auto mesh = it->second.find(meshId);
        if (mesh == it->second.end())
            return false;
        visible = mesh->second;
        return true;
    }

    void ClearForNewMission()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_MeshesBySoldier.clear();
    }
}

bool Install_Soldier2Impl_RealizeRealizedSoldier_Hook()
{
    if (!gAddr.Soldier2Impl_RealizeRealizedSoldier)
    {
        LogError("SoldierMesh: no address for RealizeRealizedSoldier on %s, soldier mesh visibility is lost when he reloads",
            GetGameBuildName(gGameBuild));
        return false;
    }

    void* fn = ResolveGameAddress(gAddr.Soldier2Impl_RealizeRealizedSoldier);
    const HookEntry entry = CheckHookEntry(fn, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        char bytes[160] = {};
        DescribeFunctionEntry(fn, bytes, sizeof(bytes));
        LogError("SoldierMesh: RealizeRealizedSoldier is not the known code on %s (%s), soldier mesh visibility is lost when he reloads",
            GetGameBuildName(gGameBuild), bytes);
        return false;
    }
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkRealizeRealizedSoldier),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SoldierMesh: RealizeRealizedSoldier hook failed, soldier mesh visibility is lost when he reloads");
        return false;
    }
    g_Addr = fn;
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("SoldierMesh: RealizeRealizedSoldier", fn);
    return true;
}

void Uninstall_Soldier2Impl_RealizeRealizedSoldier_Hook()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
