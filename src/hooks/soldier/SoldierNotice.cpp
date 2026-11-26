#include <pch.h>

#include <Windows.h>
#include <cstdint>
#include <unordered_map>
#include <mutex>

#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "AddressSet.h"
#include "GetGameObjectIdWithIndex.h"
#include "LuaBroadcaster.h"
#include "SoldierFaction.h"
#include "SoldierRealizedSlot.h"
#include "../fox/gm/GameObjectPosition.h"


using StepFn_t = void(__fastcall*)(void* self, std::uint32_t soldierIndex, int stepProc, void* knowledge);
using SpreadFn_t = void(__fastcall*)(void* self, std::uint32_t comradeIndex, std::uint32_t sourceIndex);
using CheckNoticeFn_t = void(__fastcall*)(void* self, std::uint32_t param1, void* param2, void* param3);
using SpeakVfunc20_t = bool(__fastcall*)(void* self, std::uint32_t id32, std::uint32_t a3, int a4, std::uint32_t lineId, float a6);

static constexpr int STEP_ENTER = 0;
static constexpr std::uint32_t kEvn220LineId = 0x8073EA46u;
static constexpr std::uint64_t kSpeakCooldownMs = 8000;
static constexpr std::uint64_t kGlobalSpeakCooldownMs = 3000;

static StepFn_t g_OrigNoiseAware = nullptr;
static StepFn_t g_OrigIndisAware = nullptr;
static SpreadFn_t g_OrigSpread = nullptr;
static CheckNoticeFn_t g_OrigCheckSight = nullptr;
static thread_local bool g_spreadFromSight = false;

static std::unordered_map<std::uint64_t, std::uint64_t> g_SpeakCooldown;
static std::uint64_t g_lastSpeakTick = 0;
static std::mutex g_Mutex;

static constexpr std::uint32_t kNoticeSoldiers   = 0x200;
static constexpr int           kRecentNotices    = 4;
static constexpr std::uint8_t  kBlobSighted      = 1;      // AddNoticeInfo blob: the noticed object's id at +2
static constexpr std::uint8_t  kBlobNoise        = 2;      // AddNoticeInfo blob: a fox::gs::Noise at +8
static constexpr std::uint16_t kNoObject         = 0xFFFF;
static constexpr std::uint64_t kRecentMs         = 3000;
static constexpr std::uint64_t kUnplacedSightMs  = 1000;
static constexpr float         kNoiseMatchSq     = 4.0f;   // 2 m
static constexpr float         kSightMatchSq     = 36.0f;  // 6 m

struct SightedObject
{
    std::uint16_t objectId = kNoObject;
    std::uint64_t tickMs   = 0;
};

struct HeardNoise
{
    float         position[3] = {};
    std::uint8_t  type        = 0;
    std::uint16_t maker       = kNoObject;
    std::uint64_t tickMs      = 0;
};

static SightedObject g_Sighted[kNoticeSoldiers][kRecentNotices];
static HeardNoise    g_Heard[kNoticeSoldiers][kRecentNotices];
static std::uint8_t  g_SightedNext[kNoticeSoldiers] = {};
static std::uint8_t  g_HeardNext[kNoticeSoldiers]   = {};
static std::mutex    g_RecentMutex;


static __forceinline std::uintptr_t ReadQwordNoThrow(std::uintptr_t addr)
{
    __try { return *reinterpret_cast<std::uintptr_t*>(addr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static void MakeSoldierSpeakViaOwner(std::uintptr_t owner, std::uint32_t soldierIndex, std::uint32_t lineId)
{
    if (!owner) return;
    const std::uint32_t slot = SoldierRealizedSlot::Of(reinterpret_cast<const std::uint8_t*>(owner), soldierIndex);
    if (slot == SoldierRealizedSlot::kNotRealized) return;
    const std::uintptr_t obj = ReadQwordNoThrow(owner + 0xA8);
    if (!obj) return;
    const std::uintptr_t vtbl = ReadQwordNoThrow(obj);
    if (!vtbl) return;
    const std::uintptr_t fnp = ReadQwordNoThrow(vtbl + 0x20);
    if (!fnp) return;

    auto fn = reinterpret_cast<SpeakVfunc20_t>(fnp);
    __try { fn(reinterpret_cast<void*>(obj), slot, 0x95EA16B0u, 4, lineId, 0.0f); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static bool ReadNoticeBlobSeh(const void* blob, std::uint8_t& payload, std::uint16_t& objectId, const std::uint8_t*& data)
{
    __try
    {
        const auto* bytes = static_cast<const std::uint8_t*>(blob);
        payload  = bytes[1];
        objectId = *reinterpret_cast<const std::uint16_t*>(bytes + 2);
        data     = *reinterpret_cast<const std::uint8_t* const*>(bytes + 8);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool ReadNoiseSeh(const std::uint8_t* noise, HeardNoise& out)
{
    __try
    {
        out.position[0] = *reinterpret_cast<const float*>(noise + 0x00);
        out.position[1] = *reinterpret_cast<const float*>(noise + 0x04);
        out.position[2] = *reinterpret_cast<const float*>(noise + 0x08);
        out.type        = noise[0x10];
        out.maker       = *reinterpret_cast<const std::uint16_t*>(noise + 0x12);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool ReadKnowledgeSpotSeh(const void* knowledge, float spot[3])   // both notice AIs keep the noticed spot at +0
{
    __try
    {
        const auto* floats = static_cast<const float*>(knowledge);
        spot[0] = floats[0];
        spot[1] = floats[1];
        spot[2] = floats[2];
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static float DistanceSq(const float a[3], const float b[3])
{
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

static std::uint16_t SightedAt(std::uint32_t soldierIndex, const float spot[3])
{
    SightedObject recent[kRecentNotices];
    {
        std::lock_guard<std::mutex> lock(g_RecentMutex);
        for (int i = 0; i < kRecentNotices; ++i)
            recent[i] = g_Sighted[soldierIndex][i];
    }
    const std::uint64_t now = GetTickCount64();
    std::uint16_t closest = kNoObject;
    float closestSq = kSightMatchSq;
    std::uint16_t newest = kNoObject;
    std::uint64_t newestMs = 0;
    bool anyPlaced = false;
    for (const SightedObject& seen : recent)
    {
        if (seen.tickMs == 0 || now - seen.tickMs > kRecentMs)
            continue;
        if (seen.tickMs > newestMs)
        {
            newest = seen.objectId;
            newestMs = seen.tickMs;
        }
        float position[3] = {};
        if (!GameObjectPosition::Read(seen.objectId, position))
            continue;
        anyPlaced = true;
        const float distanceSq = DistanceSq(position, spot);
        if (distanceSq <= closestSq)
        {
            closest = seen.objectId;
            closestSq = distanceSq;
        }
    }
    if (closest != kNoObject || anyPlaced)
        return closest;
    return now - newestMs <= kUnplacedSightMs ? newest : kNoObject;   // nothing had a readable position: the latest one, if fresh
}

static bool HeardAt(std::uint32_t soldierIndex, const float spot[3], HeardNoise& out)
{
    HeardNoise recent[kRecentNotices];
    {
        std::lock_guard<std::mutex> lock(g_RecentMutex);
        for (int i = 0; i < kRecentNotices; ++i)
            recent[i] = g_Heard[soldierIndex][i];
    }
    const std::uint64_t now = GetTickCount64();
    float closestSq = kNoiseMatchSq;
    bool found = false;
    for (const HeardNoise& noise : recent)
    {
        if (noise.tickMs == 0 || now - noise.tickMs > kRecentMs)
            continue;
        const float distanceSq = DistanceSq(noise.position, spot);
        if (distanceSq <= closestSq)
        {
            out = noise;
            closestSq = distanceSq;
            found = true;
        }
    }
    return found;
}

static V_FrameWork::LuaBroadcastArg IdOrNil(std::uint16_t objectId)
{
    return objectId != kNoObject ? V_FrameWork::LuaBroadcastArg(static_cast<std::uint32_t>(objectId)) : V_FrameWork::LuaBroadcastArg(nullptr);
}

static V_FrameWork::LuaBroadcastArg SpotOrNil(bool known, const float spot[3])
{
    return known ? V_FrameWork::LuaBroadcastArg(V_FrameWork::Vector3Arg{ spot[0], spot[1], spot[2] }) : V_FrameWork::LuaBroadcastArg(nullptr);
}

static void EmitNoticeIndis(std::uint32_t soldierIndex, const void* knowledge)
{
    std::uint32_t soldierId = 0;
    if (soldierIndex >= kNoticeSoldiers || !GetSoldierGameObjectIdWithIndex(soldierIndex, soldierId))
        return;
    float spot[3] = {};
    const bool known = ReadKnowledgeSpotSeh(knowledge, spot);
    const std::uint16_t targetId = known ? SightedAt(soldierIndex, spot) : kNoObject;
    V_FrameWork::EmitMessage("GameObject", "NoticeIndis", soldierId, IdOrNil(targetId), SpotOrNil(known, spot));
}

static void EmitNoticeNoise(std::uint32_t soldierIndex, const void* knowledge)
{
    std::uint32_t soldierId = 0;
    if (soldierIndex >= kNoticeSoldiers || !GetSoldierGameObjectIdWithIndex(soldierIndex, soldierId))
        return;
    float spot[3] = {};
    const bool known = ReadKnowledgeSpotSeh(knowledge, spot);
    HeardNoise noise{};
    const bool heard = known && HeardAt(soldierIndex, spot, noise);
    V_FrameWork::EmitMessage("GameObject", "NoticeNoise", soldierId,
        heard ? V_FrameWork::LuaBroadcastArg(static_cast<std::uint32_t>(noise.type)) : V_FrameWork::LuaBroadcastArg(nullptr),
        IdOrNil(heard ? noise.maker : kNoObject), SpotOrNil(known, spot));
}

void SoldierNotice_NoteAccepted(std::uint32_t soldierIndex, const void* noticeBlob)   // from the shared AddNoticeInfo hook
{
    if (soldierIndex >= kNoticeSoldiers || !noticeBlob)
        return;
    std::uint8_t payload = 0;
    std::uint16_t objectId = kNoObject;
    const std::uint8_t* data = nullptr;
    if (!ReadNoticeBlobSeh(noticeBlob, payload, objectId, data))
        return;
    const std::uint64_t now = GetTickCount64();
    if (payload == kBlobSighted && objectId != kNoObject)
    {
        std::lock_guard<std::mutex> lock(g_RecentMutex);
        g_Sighted[soldierIndex][g_SightedNext[soldierIndex]++ % kRecentNotices] = { objectId, now };
        return;
    }
    if (payload != kBlobNoise || !data)
        return;
    HeardNoise noise{};
    if (!ReadNoiseSeh(data, noise))
        return;
    noise.tickMs = now;
    std::lock_guard<std::mutex> lock(g_RecentMutex);
    g_Heard[soldierIndex][g_HeardNext[soldierIndex]++ % kRecentNotices] = noise;
}


static void __fastcall hk_CheckSightNoticeSoldier(void* self, std::uint32_t param1, void* param2, void* param3)
{
    if (!g_OrigCheckSight) return;
    if (MissionCodeGuard::ShouldBypassHooks()) { g_OrigCheckSight(self, param1, param2, param3); return; }

    SoldierFactionHiddenSight hidden;
    SoldierFaction_HideDownedRivals(self, param1, param3, &hidden);

    g_spreadFromSight = true;
    g_OrigCheckSight(self, param1, param2, param3);
    g_spreadFromSight = false;

    SoldierFaction_RestoreDownedRivals(&hidden);
    SoldierFaction_OnSightCheck(self, param1);
}

static void __fastcall hk_SpreadNotice(void* self, std::uint32_t comradeIndex, std::uint32_t sourceIndex)
{
    if (!g_OrigSpread) return;
    if (MissionCodeGuard::ShouldBypassHooks()) { g_OrigSpread(self, comradeIndex, sourceIndex); return; }

    const std::uint32_t ignoredBefore = SoldierFaction_IgnoredNoticeCount();
    g_OrigSpread(self, comradeIndex, sourceIndex);

    if (!g_spreadFromSight)
        return;
    if (SoldierFaction_IgnoredNoticeCount() != ignoredBefore)
        return;
    if (SoldierFaction_AreHostileSoldiers(static_cast<std::uint16_t>(0x400u | comradeIndex),
                                          static_cast<std::uint16_t>(0x400u | sourceIndex)))
        return;

    const std::uint64_t now = GetTickCount64();
    const std::uint32_t pairLo = (sourceIndex < comradeIndex) ? sourceIndex : comradeIndex;
    const std::uint32_t pairHi = (sourceIndex < comradeIndex) ? comradeIndex : sourceIndex;
    const std::uint64_t pairKey = (static_cast<std::uint64_t>(pairLo) << 32) | pairHi;

    bool canSpeak = false;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const bool globalCooled = (g_lastSpeakTick != 0) && (now - g_lastSpeakTick) < kGlobalSpeakCooldownMs;
        auto it = g_SpeakCooldown.find(pairKey);
        const bool pairCooled = (it != g_SpeakCooldown.end()) && (now - it->second) < kSpeakCooldownMs;
        if (!globalCooled && !pairCooled)
        {
            canSpeak = true;
            g_SpeakCooldown[pairKey] = now;
            g_lastSpeakTick = now;
        }
    }

    if (canSpeak)
    {
        const std::uintptr_t owner = ReadQwordNoThrow(reinterpret_cast<std::uintptr_t>(self) + 0x58);
        MakeSoldierSpeakViaOwner(owner, comradeIndex, kEvn220LineId);
    }
}

static void __fastcall hk_NoiseAware(void* self, std::uint32_t soldierIndex, int stepProc, void* knowledge)
{
    if (!g_OrigNoiseAware) return;
    if (MissionCodeGuard::ShouldBypassHooks()) { g_OrigNoiseAware(self, soldierIndex, stepProc, knowledge); return; }

    g_OrigNoiseAware(self, soldierIndex, stepProc, knowledge);
    if (stepProc == STEP_ENTER)
        EmitNoticeNoise(soldierIndex, knowledge);
}

static void __fastcall hk_IndisAware(void* self, std::uint32_t soldierIndex, int stepProc, void* knowledge)
{
    if (!g_OrigIndisAware) return;
    if (MissionCodeGuard::ShouldBypassHooks()) { g_OrigIndisAware(self, soldierIndex, stepProc, knowledge); return; }

    g_OrigIndisAware(self, soldierIndex, stepProc, knowledge);
    if (stepProc == STEP_ENTER)
        EmitNoticeIndis(soldierIndex, knowledge);
}


static bool HookOne(uintptr_t addr, void* detour, void** orig)
{
    if (!addr) return true;
    void* target = ResolveGameAddress(addr);
    return target && CreateAndEnableHook(target, detour, orig);
}

bool Install_SoldierNotice_Hooks()
{
    Install_GetGameObjectIdWithIndex();

    bool ok = true;
    ok &= HookOne(gAddr.NoticeControllerImpl_CheckSightNoticeSoldier, reinterpret_cast<void*>(&hk_CheckSightNoticeSoldier), reinterpret_cast<void**>(&g_OrigCheckSight));
    ok &= HookOne(gAddr.NoticeControllerImpl_DoCheckSpreadNotice, reinterpret_cast<void*>(&hk_SpreadNotice), reinterpret_cast<void**>(&g_OrigSpread));
    ok &= HookOne(gAddr.NoticeNoiseAiImpl_StepAware, reinterpret_cast<void*>(&hk_NoiseAware), reinterpret_cast<void**>(&g_OrigNoiseAware));
    ok &= HookOne(gAddr.NoticeIndisAiImpl_StepAware, reinterpret_cast<void*>(&hk_IndisAware), reinterpret_cast<void**>(&g_OrigIndisAware));
    return ok;
}

bool Uninstall_SoldierNotice_Hooks()
{
    if (gAddr.NoticeControllerImpl_CheckSightNoticeSoldier) DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeControllerImpl_CheckSightNoticeSoldier));
    if (gAddr.NoticeControllerImpl_DoCheckSpreadNotice) DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeControllerImpl_DoCheckSpreadNotice));
    if (gAddr.NoticeNoiseAiImpl_StepAware) DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeNoiseAiImpl_StepAware));
    if (gAddr.NoticeIndisAiImpl_StepAware) DisableAndRemoveHook(ResolveGameAddress(gAddr.NoticeIndisAiImpl_StepAware));

    g_OrigCheckSight = nullptr;
    g_OrigSpread = nullptr;
    g_OrigNoiseAware = nullptr;
    g_OrigIndisAware = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_SpeakCooldown.clear();
        g_lastSpeakTick = 0;
    }
    return true;
}
