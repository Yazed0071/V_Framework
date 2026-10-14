#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "V_FrameWorkState.h"
#include "AudioVolumeNodes.h"
#include "CAkParameterNodeBase_SetInitialRTPC.h"

namespace
{
    struct WwiseAddresses
    {
        std::uintptr_t setInitialRtpc;
        std::uintptr_t setNodeBaseParams;
        std::uintptr_t setRtpc;
        std::uintptr_t getNodePtrAndAddRef;
        std::uintptr_t setRtpcValue;
        std::uintptr_t index;
        std::uintptr_t rtpcMgr;
        std::uintptr_t audioMgr;
        std::uintptr_t mainLock;
    };

    const WwiseAddresses* AddressesForBuild()
    {
        static constexpr WwiseAddresses kEn154 = {
            0x1403917A0ull, 0x140391940ull, 0x140391FB0ull, 0x14034D6B0ull, 0x14033D090ull,
            0x142B9EAF0ull, 0x142B9EB70ull, 0x142B9EB78ull, 0x142B9EB98ull };
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
            return &kEn154;
        default:
            return nullptr;
        }
    }

    struct ExpectedBytes
    {
        std::uintptr_t WwiseAddresses::* field;
        std::uint8_t   bytes[5];
    };

    constexpr ExpectedBytes kExpected[] = {
        { &WwiseAddresses::setInitialRtpc,      { 0x48, 0x89, 0x5C, 0x24, 0x18 } },
        { &WwiseAddresses::setNodeBaseParams,   { 0x48, 0x89, 0x5C, 0x24, 0x08 } },
        { &WwiseAddresses::setRtpc,             { 0x48, 0x89, 0x5C, 0x24, 0x08 } },
        { &WwiseAddresses::getNodePtrAndAddRef, { 0x45, 0x85, 0xC0, 0x74, 0x07 } },
        { &WwiseAddresses::setRtpcValue,        { 0x48, 0x83, 0xEC, 0x68, 0xF3 } },
    };

    using SetInitialRtpc_t      = std::uint32_t(__fastcall*)(void* node, std::uint8_t** cursor, std::uint32_t* remaining);
    using SetNodeBaseParams_t   = std::uint32_t(__fastcall*)(void* node, std::uint8_t** cursor, std::uint32_t* remaining,
                                                             bool partialOnly);
    using SetRtpc_t             = void(__fastcall*)(void* node, std::uint32_t rtpcId, std::uint32_t paramId,
                                                    std::uint32_t curveId, std::uint32_t scaling,
                                                    const void* points, std::uint32_t pointCount);
    using GetNodePtrAndAddRef_t = void*(__fastcall*)(void* index, std::uint32_t nodeId, std::uint32_t isBus);
    using SetRtpcValue_t        = std::int32_t(__fastcall*)(std::uint32_t rtpcId, float value, std::uint64_t gameObject,
                                                            std::int32_t transitionMs, std::uint32_t interpolation);
    using Release_t             = void(__fastcall*)(void* node);

    struct CurvePoint
    {
        float         x;
        float         y;
        std::uint32_t interpolation;
    };

    constexpr std::uint32_t kParamVolume      = 0;
    constexpr std::uint32_t kParamBusVolume   = 4;
    constexpr std::uint32_t kScalingNone      = 0;
    constexpr std::uint32_t kInterpLinear     = 4;
    constexpr std::uint64_t kGlobalGameObject = ~0ull;
    constexpr std::int32_t  kSlideMs          = 100;

    constexpr std::size_t   kNodeId      = 0x10;
    constexpr std::size_t   kNodeFlags   = 0x53;
    constexpr std::uint8_t  kNodeFlagBus = 0x02;
    constexpr std::size_t   kReleaseSlot = 2;
    constexpr std::size_t   kFxEntrySize = 7;   // fx slot, fx id, share set flag, rendered flag

    constexpr std::uint32_t kRtpcIds[VOLUME_TYPE_COUNT] = {
        0xAA08104Fu,   // vfw_master_vol
        0x17BFA8CAu,   // vfw_music_vol
        0xC75BFCEDu,   // vfw_voice_vol
        0xB96A72E2u,   // vfw_sfx_vol
        0x98851F09u,   // vfw_ambience_vol
        0xDF6740B7u,   // vfw_ui_vol
        0xF66D8751u,   // vfw_tape_vol
    };
    constexpr std::uint32_t kCurveIds[VOLUME_TYPE_COUNT] = {   // our own curve ids, no bank uses them
        0x56465701u, 0x56465702u, 0x56465703u, 0x56465704u, 0x56465705u, 0x56465706u, 0x56465707u };

    struct BusBinding
    {
        std::uint32_t   busId;
        std::uint32_t   paramId;
        AudioVolumeType type;
    };

    constexpr BusBinding kBuses[] = {
        { 0xE2B7BC37u, kParamBusVolume, VOLUME_MASTER },     // Master Audio Bus
        { 0x75DEB539u, kParamBusVolume, VOLUME_MUSIC },      // music_main_bus
        { 0xC7E06479u, kParamBusVolume, VOLUME_MUSIC },      // music_system_bus
        { 0xDC8F735Fu, kParamVolume,    VOLUME_MUSIC },      // env radio
        { 0x030D6FCCu, kParamVolume,    VOLUME_VOICE },      // env dialogue
        { 0x9E1D8BF6u, kParamVolume,    VOLUME_VOICE },      // hash of the mission radio voice bus (real name unknown)
        { 0x56405243u, kParamVolume,    VOLUME_VOICE },      // hash of the result / free roam voice bus (real name unknown)
        { 0x985A1E10u, kParamVolume,    VOLUME_VOICE },      // hash of a mission voice bus (real name unknown)
        { 0x61AE6A7Du, kParamVolume,    VOLUME_VOICE },      // hash of the enemy / iDroid voice bus (real name unknown)
        { 0x6D7427ABu, kParamVolume,    VOLUME_AMBIENCE },   // hash of the ambience bus (real name unknown)
        { 0xF49DDE30u, kParamVolume,    VOLUME_UI },         // hash of the ui / system sound bus (real name unknown)
    };

    constexpr std::uint32_t kTapeNodes[] = {   // cassette tapes, headphones and the three speaker modes (not the custom soundtrack)
        0x0B3F61CEu, 0x05519F7Eu, 0x38DDEC4Du, 0x3AFB4BD7u, 0x09919A09u };

    constexpr std::uint32_t kCassetteUiNodes[] = { 0x0081B8C0u, 0x2C4C7534u };   // cassette player menu sounds on the tape bus

    constexpr std::uint32_t kSoundEffectBuses[] = {
        0xFA0554B3u,   // env sfx
        0xD6876675u,   // hash of the vehicle sfx bus (real name unknown)
        0xAEDD7D39u,   // hash of an env sfx bus (real name unknown)
    };

    struct Target
    {
        std::uint32_t   paramId;
        AudioVolumeType type;
    };

    constexpr const char* kStateSpace    = "Volume";
    constexpr const char* kOldStateSpace = "AudioVolume";   // the old 0-10 sliders
    constexpr const char* kStateNames[VOLUME_TYPE_COUNT] = {
        "Master", "Music", "Voice", "SoundEffect", "Ambience", "UI", "Tape" };

    const WwiseAddresses*  g_Addr                    = nullptr;
    void*                  g_HookTargets[2]          = {};
    SetInitialRtpc_t       g_OrigSetInitialRtpc      = nullptr;
    SetNodeBaseParams_t    g_OrigSetNodeBaseParams   = nullptr;
    SetRtpc_t              g_SetRtpc                 = nullptr;
    GetNodePtrAndAddRef_t  g_GetNodePtrAndAddRef     = nullptr;
    SetRtpcValue_t         g_SetRtpcValue            = nullptr;
    void**                 g_IndexSlot               = nullptr;
    void**                 g_RtpcMgrSlot             = nullptr;
    void**                 g_AudioMgrSlot            = nullptr;
    CRITICAL_SECTION*      g_MainLock                = nullptr;

    constexpr int kCurvePointCount = kAudioVolumeMax - kAudioVolumeMin + 1;

    CurvePoint                                 g_Curve[kCurvePointCount];
    std::unordered_map<std::uint64_t, Target>  g_Targets;
    std::unordered_set<std::uint32_t>          g_VanillaSoundEffectTops;
    std::unordered_set<void*>                  g_BoundNodes;

    std::atomic<int>  g_Saved[VOLUME_TYPE_COUNT];
    std::atomic<int>  g_Live[VOLUME_TYPE_COUNT];
    std::atomic<bool> g_Installed{ false };
    std::atomic<bool> g_Pushed{ false };
    std::atomic<bool> g_PushedInFob{ false };
    std::atomic<bool> g_LateBound{ false };

    std::uint64_t TargetKey(std::uint32_t nodeId, bool isBus)
    {
        return static_cast<std::uint64_t>(nodeId) | (isBus ? (1ull << 32) : 0ull);
    }

    int Clamp(int value)
    {
        return value < kAudioVolumeMin ? kAudioVolumeMin : (value > kAudioVolumeMax ? kAudioVolumeMax : value);
    }

    bool ValidType(int type)
    {
        return type >= 0 && type < VOLUME_TYPE_COUNT;
    }

    bool EngineUp()
    {
        return g_RtpcMgrSlot && *g_RtpcMgrSlot && g_AudioMgrSlot && *g_AudioMgrSlot && g_IndexSlot && *g_IndexSlot;
    }

    bool IsSoundEffectBus(std::uint32_t busId)
    {
        for (std::uint32_t id : kSoundEffectBuses)
            if (id == busId)
                return true;
        return false;
    }

    void BuildCurve()
    {
        for (int value = kAudioVolumeMin; value <= kAudioVolumeMax; ++value)
        {
            CurvePoint& p = g_Curve[value - kAudioVolumeMin];
            p.x = static_cast<float>(value - kAudioVolumeDefault);   // we send slider - 50, so a parameter nobody set yet plays at vanilla volume
            p.y = static_cast<float>(20.0 * std::log10(static_cast<double>(value) / kAudioVolumeDefault));
            p.interpolation = kInterpLinear;
        }
    }

    void BuildTargets()
    {
        g_Targets.clear();
        for (const BusBinding& b : kBuses)
            g_Targets[TargetKey(b.busId, true)] = { b.paramId, b.type };
        for (std::uint32_t id : kMusicNodes)
            g_Targets[TargetKey(id, false)] = { kParamVolume, VOLUME_MUSIC };
        for (std::uint32_t id : kVoiceNodes)
            g_Targets[TargetKey(id, false)] = { kParamVolume, VOLUME_VOICE };
        for (std::uint32_t id : kSoundEffectNodes)
            g_Targets[TargetKey(id, false)] = { kParamVolume, VOLUME_SOUND_EFFECT };
        for (std::uint32_t id : kAmbienceNodes)
            g_Targets[TargetKey(id, false)] = { kParamVolume, VOLUME_AMBIENCE };
        for (std::uint32_t id : kTapeNodes)
            g_Targets[TargetKey(id, false)] = { kParamVolume, VOLUME_TAPE };
        for (std::uint32_t id : kCassetteUiNodes)
            g_Targets[TargetKey(id, false)] = { kParamVolume, VOLUME_UI };
        g_VanillaSoundEffectTops.clear();
        for (std::uint32_t id : kVanillaSoundEffectTops)
            g_VanillaSoundEffectTops.insert(id);
    }

    void Bind(void* node, const Target& target)
    {
        g_SetRtpc(node, kRtpcIds[target.type], target.paramId, kCurveIds[target.type], kScalingNone,
                  g_Curve, static_cast<std::uint32_t>(kCurvePointCount));
        g_BoundNodes.insert(node);
    }

    std::uint32_t NodeId(const void* node)
    {
        return *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(node) + kNodeId);
    }

    std::uint32_t __fastcall hk_SetInitialRtpc(void* node, std::uint8_t** cursor, std::uint32_t* remaining)
    {
        const std::uint32_t result = g_OrigSetInitialRtpc(node, cursor, remaining);
        if (node && g_Installed.load(std::memory_order_acquire))
        {
            const bool isBus = (static_cast<const std::uint8_t*>(node)[kNodeFlags] & kNodeFlagBus) != 0;
            const auto it = g_Targets.find(TargetKey(NodeId(node), isBus));
            if (it != g_Targets.end())
                Bind(node, it->second);
        }
        return result;
    }

    bool ReadBusAndParent(const std::uint8_t* data, std::uint32_t remaining, std::uint32_t& busId, std::uint32_t& parentId)
    {
        if (!data || remaining < 2)
            return false;
        const std::uint32_t fxCount = data[1];
        const std::uint32_t offset  = 2 + (fxCount ? 1 + fxCount * static_cast<std::uint32_t>(kFxEntrySize) : 0);   // override flag, fx count, bypass bits, fx slots
        if (offset + 8 > remaining)
            return false;
        std::memcpy(&busId, data + offset, 4);
        std::memcpy(&parentId, data + offset + 4, 4);
        return true;
    }

    std::uint32_t __fastcall hk_SetNodeBaseParams(void* node, std::uint8_t** cursor, std::uint32_t* remaining, bool partialOnly)
    {
        std::uint32_t busId = 0, parentId = 0;
        const bool parsed = !partialOnly && cursor && remaining && ReadBusAndParent(*cursor, *remaining, busId, parentId);
        const std::uint32_t result = g_OrigSetNodeBaseParams(node, cursor, remaining, partialOnly);
        if (result != 1 || !parsed || parentId != 0 || !IsSoundEffectBus(busId) || !node
            || !g_Installed.load(std::memory_order_acquire))
            return result;
        const std::uint32_t nodeId = NodeId(node);
        if (g_Targets.count(TargetKey(nodeId, false)) || g_VanillaSoundEffectTops.count(nodeId))
            return result;
        Bind(node, { kParamVolume, VOLUME_SOUND_EFFECT });   // a mod's own sound group on a sound effect bus
        return result;
    }

    void BindAlreadyLoaded()
    {
        EnterCriticalSection(g_MainLock);
        void* index = *g_IndexSlot;
        for (const auto& kv : g_Targets)
        {
            const std::uint32_t nodeId = static_cast<std::uint32_t>(kv.first);
            const std::uint32_t isBus  = static_cast<std::uint32_t>(kv.first >> 32);
            void* node = g_GetNodePtrAndAddRef(index, nodeId, isBus);
            if (!node)
                continue;
            if (!g_BoundNodes.count(node))
                Bind(node, kv.second);
            (*reinterpret_cast<Release_t**>(node))[kReleaseSlot](node);
        }
        LeaveCriticalSection(g_MainLock);
    }

    void Push(int type, int value, bool inFob, std::int32_t transitionMs)
    {
        const int heard = inFob ? kAudioVolumeDefault : value;
        g_SetRtpcValue(kRtpcIds[type], static_cast<float>(heard - kAudioVolumeDefault), kGlobalGameObject,
                       transitionMs, kInterpLinear);
    }

    void PushAll(bool inFob, std::int32_t transitionMs)
    {
        for (int type = 0; type < VOLUME_TYPE_COUNT; ++type)
            Push(type, g_Live[type].load(std::memory_order_relaxed), inFob, transitionMs);
    }

    void PushOne(int type, std::int32_t transitionMs)
    {
        if (!g_Installed.load(std::memory_order_acquire) || !EngineUp())
            return;
        Push(type, g_Live[type].load(std::memory_order_relaxed), MissionCodeGuard::ShouldBypassHooks(), transitionMs);
    }

    void LoadSaved()
    {
        for (int type = 0; type < VOLUME_TYPE_COUNT; ++type)
        {
            const std::int32_t stored = V_FrameWorkState::GetPersistedConstant(kStateSpace, kStateNames[type]);   // stored as value + 1, the state file drops zeros
            int value = kAudioVolumeDefault;
            if (stored > 0)
                value = Clamp(stored - 1);
            else if (const std::int32_t old = V_FrameWorkState::GetPersistedConstant(kOldStateSpace, kStateNames[type]); old > 0)
            {
                value = Clamp((old - 1) * 10);
                V_FrameWorkState::SetPersistedConstant(kStateSpace, kStateNames[type], value + 1);
            }
            g_Saved[type].store(value, std::memory_order_relaxed);
            g_Live[type].store(value, std::memory_order_relaxed);
        }
    }

    bool BytesMatch(std::uintptr_t absAddr, const std::uint8_t* expected, std::size_t size)
    {
        const void* at = ResolveGameAddress(absAddr);
        if (!at)
            return false;
        __try
        {
            return std::memcmp(at, expected, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void RemoveHooks()
    {
        for (void*& target : g_HookTargets)
        {
            if (target)
                DisableAndRemoveHook(target);
            target = nullptr;
        }
        g_OrigSetInitialRtpc    = nullptr;
        g_OrigSetNodeBaseParams = nullptr;
    }
}

int AudioVolume_Get(int type)
{
    return ValidType(type) ? g_Saved[type].load(std::memory_order_relaxed) : kAudioVolumeDefault;
}

void AudioVolume_Commit(int type, int value)
{
    if (!ValidType(type))
        return;
    value = Clamp(value);
    g_Saved[type].store(value, std::memory_order_relaxed);
    g_Live[type].store(value, std::memory_order_relaxed);
    V_FrameWorkState::SetPersistedConstant(kStateSpace, kStateNames[type], value + 1);
    PushOne(type, kSlideMs);
}

void AudioVolume_Preview(int type, int value)
{
    if (!ValidType(type))
        return;
    g_Live[type].store(Clamp(value), std::memory_order_relaxed);
    PushOne(type, kSlideMs);
}

void AudioVolume_ApplySaved()
{
    for (int type = 0; type < VOLUME_TYPE_COUNT; ++type)
    {
        const int saved = g_Saved[type].load(std::memory_order_relaxed);
        if (g_Live[type].exchange(saved, std::memory_order_relaxed) != saved)
            PushOne(type, kSlideMs);
    }
}

void AudioVolume_Pump()
{
    if (!g_Installed.load(std::memory_order_acquire) || !EngineUp())
        return;
    const bool inFob = MissionCodeGuard::ShouldBypassHooks();
    if (g_Pushed.load(std::memory_order_relaxed) && g_PushedInFob.load(std::memory_order_relaxed) == inFob)
        return;
    if (!g_LateBound.exchange(true))
        BindAlreadyLoaded();
    PushAll(inFob, 0);
    g_PushedInFob.store(inFob, std::memory_order_relaxed);
    g_Pushed.store(true, std::memory_order_relaxed);
}

bool Install_CAkParameterNodeBase_SetInitialRTPC_Hook()
{
    if (g_Installed.load(std::memory_order_acquire))
        return true;

    g_Addr = AddressesForBuild();
    if (!g_Addr)
    {
        LogError("AudioVolume: this game build is not supported, volume sliders off");
        return false;
    }
    for (const ExpectedBytes& e : kExpected)
    {
        if (!BytesMatch(g_Addr->*(e.field), e.bytes, sizeof(e.bytes)))
        {
            LogError("AudioVolume: sound engine code differs (another mod patched it?), volume sliders off");
            return false;
        }
    }

    g_SetRtpc             = reinterpret_cast<SetRtpc_t>(ResolveGameAddress(g_Addr->setRtpc));
    g_GetNodePtrAndAddRef = reinterpret_cast<GetNodePtrAndAddRef_t>(ResolveGameAddress(g_Addr->getNodePtrAndAddRef));
    g_SetRtpcValue        = reinterpret_cast<SetRtpcValue_t>(ResolveGameAddress(g_Addr->setRtpcValue));
    g_IndexSlot           = static_cast<void**>(ResolveGameAddress(g_Addr->index));
    g_RtpcMgrSlot         = static_cast<void**>(ResolveGameAddress(g_Addr->rtpcMgr));
    g_AudioMgrSlot        = static_cast<void**>(ResolveGameAddress(g_Addr->audioMgr));
    g_MainLock            = static_cast<CRITICAL_SECTION*>(ResolveGameAddress(g_Addr->mainLock));
    if (!g_SetRtpc || !g_GetNodePtrAndAddRef || !g_SetRtpcValue || !g_IndexSlot || !g_RtpcMgrSlot
        || !g_AudioMgrSlot || !g_MainLock)
    {
        LogError("AudioVolume: sound engine addresses missing, volume sliders off");
        return false;
    }

    BuildCurve();
    BuildTargets();
    LoadSaved();

    void* rtpcTarget = ResolveGameAddress(g_Addr->setInitialRtpc);
    void* nodeTarget = ResolveGameAddress(g_Addr->setNodeBaseParams);
    if (!CreateAndEnableHook(rtpcTarget, reinterpret_cast<void*>(&hk_SetInitialRtpc),
                             reinterpret_cast<void**>(&g_OrigSetInitialRtpc)))
    {
        LogError("AudioVolume: bank hook failed, volume sliders off");
        return false;
    }
    g_HookTargets[0] = rtpcTarget;
    if (!CreateAndEnableHook(nodeTarget, reinterpret_cast<void*>(&hk_SetNodeBaseParams),
                             reinterpret_cast<void**>(&g_OrigSetNodeBaseParams)))
    {
        RemoveHooks();
        LogError("AudioVolume: bank hook failed, volume sliders off");
        return false;
    }
    g_HookTargets[1] = nodeTarget;

    g_Pushed.store(false, std::memory_order_relaxed);
    g_LateBound.store(false, std::memory_order_relaxed);
    g_Installed.store(true, std::memory_order_release);
    return true;
}

bool Uninstall_CAkParameterNodeBase_SetInitialRTPC_Hook()
{
    if (!g_Installed.exchange(false))
        return true;
    RemoveHooks();
    if (EngineUp())
    {
        for (int type = 0; type < VOLUME_TYPE_COUNT; ++type)
            g_SetRtpcValue(kRtpcIds[type], 0.0f, kGlobalGameObject, 0, kInterpLinear);
    }
    g_Pushed.store(false, std::memory_order_relaxed);
    return true;
}
