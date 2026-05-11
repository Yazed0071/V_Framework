#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "AddressSet.h"
#include "HookUtils.h"
#include "SoldierObjectRtpc.h"
#include "SoldierVoiceTypeQuery.h"


namespace
{


    // Soldier2SoundControllerImpl::Activate(this, uint slot, int soundIndex).
    using Activate_t = void(__fastcall*)(void* self,
                                         std::uint32_t slot,
                                         std::int32_t soundIndex);


    static Activate_t g_OrigActivate       = nullptr;
    static void*      g_ActivateHookTarget = nullptr;

    // soldierIndex (slot) -> Soldier2SoundControllerImpl* (impl base).
    static std::unordered_map<std::uint16_t, void*> g_SoundCtrlBySoldierIndex;
    // soundSlot (sound-controller's 0..N index) -> soldierIndex.
    // Captured at Activate(soldierIndex, soundSlot) time.
    static std::unordered_map<std::uint16_t, std::uint16_t> g_SoldierIndexBySoundSlot;
    static std::mutex g_MapMutex;


    static std::uint16_t NormalizeSoldierIndexFromGameObjectId(std::uint32_t gameObjectId)
    {
        const std::uint16_t raw = static_cast<std::uint16_t>(gameObjectId);

        if (raw == 0xFFFFu)
            return 0xFFFFu;

        // Soldier-class gameObjectIds have the form 0x...._04xx.
        if ((raw & 0xFE00u) != 0x0400u)
            return 0xFFFFu;

        return static_cast<std::uint16_t>(raw & 0x01FFu);
    }


    // Activate fires once per soldier-audio-source at mission load — even for
    // soldiers who never speak. Populates both maps eagerly so SetSoldierVoicePitch
    // resolves without waiting for the soldier to make a noise.
    static void __fastcall hk_Activate(void* self, std::uint32_t slot, std::int32_t soundIndex)
    {
        if (g_OrigActivate)
            g_OrigActivate(self, slot, soundIndex);

        if (slot != 0xFFFFFFFFu && self)
        {
            const std::uint16_t key = static_cast<std::uint16_t>(slot & 0x01FFu);
            std::lock_guard<std::mutex> lock(g_MapMutex);
            g_SoundCtrlBySoldierIndex[key] = self;
            if (soundIndex >= 0)
            {
                const std::uint16_t soundKey =
                    static_cast<std::uint16_t>(soundIndex & 0xFFFF);
                g_SoldierIndexBySoundSlot[soundKey] = key;
            }
        }

        try { TryApplyAllPendingSoldierPitches(); } catch (...) {}
    }
}


void* GetSoldierSoundControllerImpl(std::uint32_t gameObjectId)
{
    const std::uint16_t soldierIndex = NormalizeSoldierIndexFromGameObjectId(gameObjectId);
    if (soldierIndex == 0xFFFFu)
        return nullptr;

    std::lock_guard<std::mutex> lock(g_MapMutex);
    const auto it = g_SoundCtrlBySoldierIndex.find(soldierIndex);
    if (it == g_SoundCtrlBySoldierIndex.end())
        return nullptr;
    return it->second;
}


std::uint32_t GetSoldierSlotFromGameObjectId(std::uint32_t gameObjectId)
{
    const std::uint16_t soldierIndex = NormalizeSoldierIndexFromGameObjectId(gameObjectId);
    if (soldierIndex == 0xFFFFu)
        return 0xFFFFu;
    return static_cast<std::uint32_t>(soldierIndex);
}


std::uint32_t GetSoldierIndexFromSoundSlot(std::uint32_t soundSlot)
{
    if (soundSlot > 0xFFFFu) return 0xFFFFu;
    const std::uint16_t key = static_cast<std::uint16_t>(soundSlot & 0xFFFF);
    std::lock_guard<std::mutex> lock(g_MapMutex);
    const auto it = g_SoldierIndexBySoundSlot.find(key);
    if (it == g_SoldierIndexBySoundSlot.end()) return 0xFFFFu;
    return static_cast<std::uint32_t>(it->second);
}


bool Install_SoldierVoiceTypeQuery_Hook()
{
    if (!gAddr.Soldier2SoundController_Activate)
        return false;

    void* actTarget = ResolveGameAddress(gAddr.Soldier2SoundController_Activate);
    if (!actTarget)
        return false;

    const bool ok = CreateAndEnableHook(
        actTarget,
        reinterpret_cast<void*>(&hk_Activate),
        reinterpret_cast<void**>(&g_OrigActivate));
    if (ok)
        g_ActivateHookTarget = actTarget;
    return ok;
}


bool Uninstall_SoldierVoiceTypeQuery_Hook()
{
    if (g_ActivateHookTarget)
    {
        DisableAndRemoveHook(g_ActivateHookTarget);
        g_ActivateHookTarget = nullptr;
        g_OrigActivate = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(g_MapMutex);
        g_SoundCtrlBySoldierIndex.clear();
        g_SoldierIndexBySoundSlot.clear();
    }
    return true;
}
