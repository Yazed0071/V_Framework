#include "pch.h"

#include "CombatAiImpl_SendFireSignal.h"

#include <cstdint>

#include "AddressSet.h"
#include "EquipPartParams.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using SendFireSignal_t = void(__fastcall*)(void* self, std::uint32_t buddyIndex, void* gameObjectSystem,
                                               std::uint16_t targetId, std::uint16_t bulletId);

    SendFireSignal_t g_Orig = nullptr;
    void*            g_Target = nullptr;

    constexpr int kBulletTypeShell = 3;

    void __fastcall hkSendFireSignal(void* self, std::uint32_t buddyIndex, void* gameObjectSystem,
                                     std::uint16_t targetId, std::uint16_t bulletId)
    {
        if (!MissionCodeGuard::ShouldBypassHooks() && EquipParam_GetBulletType(bulletId) == kBulletTypeShell)
            return; // her shots normally hit soldiers through this signal, a shell's own blast does it instead
        g_Orig(self, buddyIndex, gameObjectSystem, targetId, bulletId);
    }
}

bool Install_CombatAiImpl_SendFireSignal_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.BuddyQuietCombatAiImpl_SendFireSignal);
    if (!target || !CreateAndEnableHook(target, reinterpret_cast<void*>(&hkSendFireSignal),
                                        reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BuddyOutfit: Quiet fire signal hook failed or address missing, her launchers also land a bullet hit");
        return false;
    }
    g_Target = target;
    return true;
}

bool Uninstall_CombatAiImpl_SendFireSignal_Hook()
{
    if (!g_Target)
        return true;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    return true;
}
