#include "pch.h"
#include "ThrowingImpl_AddReboundNoise.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "ThrowingSystemLayout.h"
#include "log.h"

namespace
{
    using AddReboundNoise_t = void(__fastcall*)(void* impl, std::uint32_t localIndex, const float* position);

    AddReboundNoise_t g_Orig   = nullptr;
    void*             g_Target = nullptr;

    struct Bounce
    {
        std::uint32_t handle;
        std::uint16_t equipId;
        std::uint16_t owner;
    };

    bool ReadBounceSEH(std::uint8_t* impl, std::uint32_t localIndex, Bounce& out)
    {
        __try
        {
            using namespace ThrowingSystemLayout;
            if ((Read<std::uint32_t>(ImplRecord(impl, localIndex), kRecordFlags) & kFlagsStopped) != 0)
                return false;
            out.handle  = HandleOf(impl, localIndex);
            out.equipId = ImplRealEquipId(impl, localIndex);
            out.owner   = Read<std::uint16_t>(ImplRecord(impl, localIndex), kRecordOwner);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkAddReboundNoise(void* impl, std::uint32_t localIndex, const float* position)
    {
        Bounce bounce{};
        if (impl && !MissionCodeGuard::ShouldBypassHooks()
            && ReadBounceSEH(static_cast<std::uint8_t*>(impl), localIndex, bounce))
            V_FrameWork::QueueMessage("Throwing", "OnBounceThrowing", bounce.handle, bounce.equipId, bounce.owner);
        g_Orig(impl, localIndex, position);
    }
}

void ThrowingImpl_AddReboundNoiseQuiet(void* impl, std::uint32_t localIndex, const float* position)
{
    auto noise = g_Orig ? g_Orig
                        : reinterpret_cast<AddReboundNoise_t>(ResolveGameAddress(gAddr.ThrowingImpl_AddReboundNoise));
    if (noise)
        noise(impl, localIndex, position);
}

bool Install_ThrowingImpl_AddReboundNoise_Hook()
{
    g_Target = ResolveGameAddress(gAddr.ThrowingImpl_AddReboundNoise);
    if (!g_Target)
    {
        LogError("ThrowingSystem: AddReboundNoise address missing for this build, OnBounceThrowing is off");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkAddReboundNoise),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ThrowingSystem: AddReboundNoise hook failed, OnBounceThrowing is off");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_ThrowingImpl_AddReboundNoise_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
