#include <pch.h>
#include "FoxGrModelMesh.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using ModelMesh_t          = void(__fastcall*)(void* model, const std::uint64_t* meshId);
    using IsModelMeshVisible_t = bool(__fastcall*)(void* model, std::uint32_t meshId);

    const std::uint8_t kInvisibleMeshCode[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x8B, 0x02, 0x48, 0x8B, 0xD9, 0x0F, 0xB7, 0x89, 0xE8, 0x01, 0x00, 0x00
    };
    const std::uint8_t kVisibleMeshCode[] = {
        0x48, 0x83, 0xEC, 0x28, 0x4C, 0x8B, 0x0A, 0x33, 0xD2, 0x4C, 0x8B, 0xD1, 0x0F, 0xB7, 0x89, 0xE8, 0x01, 0x00, 0x00
    };
    const std::uint8_t kIsMeshVisibleCode[] = {
        0x48, 0x8B, 0x81, 0x58, 0x01, 0x00, 0x00, 0x44, 0x8B, 0x81, 0x50, 0x01, 0x00, 0x00
    };

    struct ModelFns
    {
        ModelMesh_t          invisibleMesh = nullptr;
        ModelMesh_t          visibleMesh   = nullptr;
        IsModelMeshVisible_t isMeshVisible = nullptr;
    };

    bool IsKnownCode(std::uintptr_t address, const std::uint8_t* code, std::size_t size)
    {
        return address && CheckHookEntry(ResolveGameAddress(address), code, size) != HookEntry::Unknown;
    }

    const ModelFns* Resolve()
    {
        static ModelFns s_fns;
        static std::atomic<int> s_state{ 0 };   // 0 not tried, 1 ready, -1 off
        const int state = s_state.load(std::memory_order_acquire);
        if (state != 0)
            return state > 0 ? &s_fns : nullptr;

        const bool ok = IsKnownCode(gAddr.FoxGrModel_InvisibleMesh, kInvisibleMeshCode, sizeof(kInvisibleMeshCode))
            && IsKnownCode(gAddr.FoxGrModel_VisibleMesh, kVisibleMeshCode, sizeof(kVisibleMeshCode))
            && IsKnownCode(gAddr.FoxGrModel_IsMeshVisible, kIsMeshVisibleCode, sizeof(kIsMeshVisibleCode));
        if (ok)
        {
            s_fns.invisibleMesh = reinterpret_cast<ModelMesh_t>(ResolveGameAddress(gAddr.FoxGrModel_InvisibleMesh));
            s_fns.visibleMesh   = reinterpret_cast<ModelMesh_t>(ResolveGameAddress(gAddr.FoxGrModel_VisibleMesh));
            s_fns.isMeshVisible = reinterpret_cast<IsModelMeshVisible_t>(ResolveGameAddress(gAddr.FoxGrModel_IsMeshVisible));
        }
        else
        {
            LogError("ModelMesh: the model mesh functions are not ported or not the known code on %s, mesh visibility is off",
                GetGameBuildName(gGameBuild));
        }
        s_state.store(ok ? 1 : -1, std::memory_order_release);
        return ok ? &s_fns : nullptr;
    }

    bool SetSEH(const ModelFns& fns, void* model, std::uint32_t meshId, bool visible)
    {
        const std::uint64_t id = meshId;
        __try
        {
            if (visible)
                fns.visibleMesh(model, &id);
            else
                fns.invisibleMesh(model, &id);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsVisibleSEH(const ModelFns& fns, void* model, std::uint32_t meshId, bool& visible)
    {
        __try
        {
            visible = fns.isMeshVisible(model, meshId);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace FoxGrModelMesh
{
    bool IsReady()
    {
        return Resolve() != nullptr;
    }

    bool SetVisible(void* model, std::uint32_t meshId, bool visible)
    {
        const ModelFns* fns = Resolve();
        return fns && model && SetSEH(*fns, model, meshId, visible);
    }

    bool IsVisible(void* model, std::uint32_t meshId, bool& visible)
    {
        const ModelFns* fns = Resolve();
        return fns && model && IsVisibleSEH(*fns, model, meshId, visible);
    }
}
