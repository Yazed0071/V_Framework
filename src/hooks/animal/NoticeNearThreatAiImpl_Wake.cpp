#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "AddressSet.h"
#include "GetGameObjectIdWithIndex.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using AnimalWakeFn_t = void(__fastcall*)(void* self, std::uint32_t animalIndex, void* param);

    constexpr std::size_t kWakeParam_FreshFlagOffset = 0x8;
    constexpr std::uint8_t kWakeParam_FreshBit = 0x1;

    constexpr std::uintptr_t kNoticeAi_ImplOffset = 0x28;
    constexpr std::uintptr_t kAnimalImpl_TypeIndexOffset = 0x40;
    constexpr std::uint16_t kAnimalTypeIndexMax = 0x7F;
    constexpr std::uint32_t kAnimalIndexMask = 0x1FFu;

    constexpr std::uint64_t kPerAnimalCooldownMs = 2000;
    constexpr std::uint64_t kBurstWindowMs = 250;
    constexpr int kBurstMax = 8;
    constexpr std::size_t kCooldownMapSoftCap = 512;

    constexpr const char* kAnimalNoticeMessage = "AnimalNotice";

    constexpr std::uint32_t kKindNearThreat = 0;
    constexpr std::uint32_t kKindNoiseAlert = 1;
    constexpr std::uint32_t kKindNearGameObject = 2;
    constexpr std::uint32_t kKindNoise = 3;

    AnimalWakeFn_t g_OrigNearThreatWake = nullptr;
    AnimalWakeFn_t g_OrigNoiseAlertWake = nullptr;
    AnimalWakeFn_t g_OrigWolfNearGameObjectWake = nullptr;
    AnimalWakeFn_t g_OrigWolfNoiseSneakWake = nullptr;
    AnimalWakeFn_t g_OrigBearNearGameObjectWake = nullptr;
    AnimalWakeFn_t g_OrigBearNoiseWake = nullptr;

    std::unordered_map<std::uint32_t, std::uint64_t> g_Cooldown;
    std::uint64_t g_BurstWindowStart = 0;
    int g_BurstCount = 0;
    std::mutex g_Mutex;

    thread_local bool g_InEmit = false;

    struct ReentryGuard
    {
        bool entered;
        ReentryGuard() : entered(!g_InEmit) { if (entered) g_InEmit = true; }
        ~ReentryGuard() { if (entered) g_InEmit = false; }
    };

    __forceinline bool IsWakeFresh(void* param)
    {
        if (!param)
            return false;
        __try
        {
            const auto* flags = reinterpret_cast<const std::uint8_t*>(
                reinterpret_cast<const char*>(param) + kWakeParam_FreshFlagOffset);
            return (*flags & kWakeParam_FreshBit) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    __forceinline bool ResolveAnimalObjId(void* self, std::uint32_t animalIndex,
                                          std::uint32_t& objIdOut)
    {
        if (!self)
            return false;
        __try
        {
            void* impl = *reinterpret_cast<void**>(
                reinterpret_cast<char*>(self) + kNoticeAi_ImplOffset);
            if (!impl)
                return false;

            const std::uint16_t typeIndex = *reinterpret_cast<const std::uint16_t*>(
                reinterpret_cast<const char*>(impl) + kAnimalImpl_TypeIndexOffset);
            if (typeIndex == 0 || typeIndex > kAnimalTypeIndexMax)
                return false;

            objIdOut = (static_cast<std::uint32_t>(typeIndex) << 9)
                     | (animalIndex & kAnimalIndexMask);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool AllowNotice(std::uint32_t animalIndex)
    {
        const std::uint64_t now = GetTickCount64();

        std::lock_guard<std::mutex> lock(g_Mutex);

        if (g_BurstWindowStart == 0 || (now - g_BurstWindowStart) >= kBurstWindowMs)
        {
            g_BurstWindowStart = now;
            g_BurstCount = 0;
        }
        if (g_BurstCount >= kBurstMax)
            return false;

        const auto it = g_Cooldown.find(animalIndex);
        if (it != g_Cooldown.end() && (now - it->second) < kPerAnimalCooldownMs)
            return false;

        if (g_Cooldown.size() > kCooldownMapSoftCap)
        {
            for (auto i = g_Cooldown.begin(); i != g_Cooldown.end(); )
                i = ((now - i->second) > kPerAnimalCooldownMs * 4) ? g_Cooldown.erase(i) : ++i;
        }

        g_Cooldown[animalIndex] = now;
        ++g_BurstCount;
        return true;
    }

    void EmitAnimalNotice(void* self, std::uint32_t animalIndex, std::uint32_t kind)
    {
        ReentryGuard guard;
        if (!guard.entered)
            return;

        const unsigned long luaTid = V_FrameWork_LuaOwnerThreadId();
        if (luaTid == 0 || GetCurrentThreadId() != luaTid)
            return;

        std::uint32_t objId = 0;
        if (!ResolveAnimalObjId(self, animalIndex, objId))
            return;

        if (!AllowNotice(animalIndex))
            return;

#ifdef _DEBUG
        {
            static int s_probe = 0;
            if (s_probe < 32)
            {
                ++s_probe;
                LogDebug("[AnimalNotice] probe: kind=%u animalIndex=%u -> objId=0x%08X "
                         "(typeIndex=0x%02X)\n",
                         kind, animalIndex, objId, (objId >> 9) & 0x7F);
            }
        }
#endif

        V_FrameWork::EmitMessage("GameObject", kAnimalNoticeMessage, objId, kind);
    }

    __forceinline void RunWake(AnimalWakeFn_t orig, void* self, std::uint32_t animalIndex,
                               void* param, std::uint32_t kind)
    {
        if (!orig)
            return;
        if (MissionCodeGuard::ShouldBypassHooks())
        {
            orig(self, animalIndex, param);
            return;
        }

        const bool fresh = IsWakeFresh(param);
        orig(self, animalIndex, param);

        if (fresh)
            EmitAnimalNotice(self, animalIndex, kind);
    }

    void __fastcall hk_NearThreatWake(void* self, std::uint32_t animalIndex, void* param)
    {
        RunWake(g_OrigNearThreatWake, self, animalIndex, param, kKindNearThreat);
    }

    void __fastcall hk_NoiseAlertWake(void* self, std::uint32_t animalIndex, void* param)
    {
        RunWake(g_OrigNoiseAlertWake, self, animalIndex, param, kKindNoiseAlert);
    }

    void __fastcall hk_WolfNearGameObjectWake(void* self, std::uint32_t animalIndex, void* param)
    {
        RunWake(g_OrigWolfNearGameObjectWake, self, animalIndex, param, kKindNearGameObject);
    }

    void __fastcall hk_WolfNoiseSneakWake(void* self, std::uint32_t animalIndex, void* param)
    {
        RunWake(g_OrigWolfNoiseSneakWake, self, animalIndex, param, kKindNoise);
    }

    void __fastcall hk_BearNearGameObjectWake(void* self, std::uint32_t animalIndex, void* param)
    {
        RunWake(g_OrigBearNearGameObjectWake, self, animalIndex, param, kKindNearGameObject);
    }

    void __fastcall hk_BearNoiseWake(void* self, std::uint32_t animalIndex, void* param)
    {
        RunWake(g_OrigBearNoiseWake, self, animalIndex, param, kKindNoise);
    }

    bool HookOne(std::uintptr_t addr, void* detour, void** orig)
    {
        if (!addr)
            return true;
        void* target = ResolveGameAddress(addr);
        return target && CreateAndEnableHook(target, detour, orig);
    }
}

bool Install_AnimalNotice_Hooks()
{
    Install_GetGameObjectIdWithIndex();

    bool ok = true;
    ok &= HookOne(gAddr.NoticeNearThreatAiImpl_Wake,
                  reinterpret_cast<void*>(&hk_NearThreatWake),
                  reinterpret_cast<void**>(&g_OrigNearThreatWake));
    ok &= HookOne(gAddr.NoticeNoiseAlertAiImpl_Wake,
                  reinterpret_cast<void*>(&hk_NoiseAlertWake),
                  reinterpret_cast<void**>(&g_OrigNoiseAlertWake));
    ok &= HookOne(gAddr.NoticeNearGameObjectAiImpl_Wolf_Wake,
                  reinterpret_cast<void*>(&hk_WolfNearGameObjectWake),
                  reinterpret_cast<void**>(&g_OrigWolfNearGameObjectWake));
    ok &= HookOne(gAddr.NoticeNoiseSneakAiImpl_Wolf_Wake,
                  reinterpret_cast<void*>(&hk_WolfNoiseSneakWake),
                  reinterpret_cast<void**>(&g_OrigWolfNoiseSneakWake));
    ok &= HookOne(gAddr.NoticeNearGameObjectAiImpl_Bear_Wake,
                  reinterpret_cast<void*>(&hk_BearNearGameObjectWake),
                  reinterpret_cast<void**>(&g_OrigBearNearGameObjectWake));
    ok &= HookOne(gAddr.NoticeNoiseAiImpl_Bear_Wake,
                  reinterpret_cast<void*>(&hk_BearNoiseWake),
                  reinterpret_cast<void**>(&g_OrigBearNoiseWake));

    if (!ok)
        Log("[AnimalNotice] ERROR: hook install failed - GameObject.AnimalNotice will not be "
            "emitted for some or all species.\n");

    return ok;
}

bool Uninstall_AnimalNotice_Hooks()
{
    if (gAddr.NoticeNearThreatAiImpl_Wake)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeNearThreatAiImpl_Wake));
    if (gAddr.NoticeNoiseAlertAiImpl_Wake)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeNoiseAlertAiImpl_Wake));
    if (gAddr.NoticeNearGameObjectAiImpl_Wolf_Wake)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeNearGameObjectAiImpl_Wolf_Wake));
    if (gAddr.NoticeNoiseSneakAiImpl_Wolf_Wake)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeNoiseSneakAiImpl_Wolf_Wake));
    if (gAddr.NoticeNearGameObjectAiImpl_Bear_Wake)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeNearGameObjectAiImpl_Bear_Wake));
    if (gAddr.NoticeNoiseAiImpl_Bear_Wake)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeNoiseAiImpl_Bear_Wake));

    g_OrigNearThreatWake = nullptr;
    g_OrigNoiseAlertWake = nullptr;
    g_OrigWolfNearGameObjectWake = nullptr;
    g_OrigWolfNoiseSneakWake = nullptr;
    g_OrigBearNearGameObjectWake = nullptr;
    g_OrigBearNoiseWake = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Cooldown.clear();
        g_BurstWindowStart = 0;
        g_BurstCount = 0;
    }
    return true;
}
