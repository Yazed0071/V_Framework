#include "pch.h"
#include "AnnounceLogHook.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using GetAnnounceLogSE_t = std::uint32_t(__fastcall*)(void* manager, int announceType);

    static GetAnnounceLogSE_t g_Orig      = nullptr;
    static bool               g_Installed = false;

    static std::unordered_map<std::uint32_t, std::uint32_t> g_Overrides;

    static constexpr std::uint32_t kModSeIdBase = 0xA0u;
    static constexpr std::uint32_t kModSeIdMax  = 0xFFu;

    static constexpr std::size_t kTLA_SoundControl = 0x90;
    static constexpr std::size_t kTLA_SeId         = 0xA8;

    static constexpr std::size_t kCtrl_Object   = 0x08;
    static constexpr std::size_t kCtrl_DlgEvent = 0x10;
    static constexpr std::size_t kCtrl_Chara    = 0x14;

    using TypingLogActUpdate_t = void(__fastcall*)(void* self);
    using SoundControlStart_t  = void(__fastcall*)(void* soundControl, std::uint64_t eventId, std::uint8_t flag);
    using GetInstance_t        = void*(__fastcall*)();
    using GetGraphState_t      = void*(__fastcall*)(void* cdm);
    using MbDvcVoicePlay_t     = char(__fastcall*)(void* voiceParam, void* obj, std::uint32_t dlgEvent, std::uint32_t chara);

    static TypingLogActUpdate_t g_OrigUpdate        = nullptr;
    static SoundControlStart_t  g_SoundControlStart = nullptr;
    static GetInstance_t        g_GetInstance       = nullptr;
    static GetGraphState_t      g_GetGraphState     = nullptr;
    static MbDvcVoicePlay_t     g_VoicePlay         = nullptr;
    static bool                 g_UpdateInstalled   = false;

    struct ModDialogue { std::uint32_t condition; std::uint32_t chara; std::uint32_t dlgEvent; };

    static ModDialogue   g_ModDialogue[0x100] = {};
    static std::string   g_ModVoices[0x100];
    static std::uint32_t g_ModEvents[0x100] = {};
    static std::unordered_map<std::uint32_t, std::uint32_t> g_SeIdByType;
    static std::uint32_t g_NextModSeId = kModSeIdBase;

    static std::uint32_t g_ModSfx[0x100] = {};
    static std::unordered_set<std::uint32_t> g_GameplaySfx;

    using SoundDaemonEnqueue_t = void*(__fastcall*)(void* daemon, void* out, std::uint32_t eventId,
                                                    void* paramBlock, std::uint32_t muteType, std::uint8_t flag);
    static void**               g_SdInstancePtr  = nullptr;
    static SoundDaemonEnqueue_t g_SdEnqueue      = nullptr;
    static std::uint8_t         g_kW_Select[16]  = {};
    static bool                 g_kW_SelectReady = false;

    static std::uint32_t LabelToAnnounceType(const char* label)
    {
        return static_cast<std::uint32_t>(FoxHashes::StrCode64(label) & 0xFFFFFFFFull);
    }

    static std::uint32_t __fastcall hkGetAnnounceLogSE(void* manager, int announceType)
    {
        const auto it = g_Overrides.find(static_cast<std::uint32_t>(announceType));
        if (it != g_Overrides.end())
            return it->second;
        return g_Orig ? g_Orig(manager, announceType) : 0u;
    }

    static void PlayDialogue(const ModDialogue& d)
    {
        if (!g_GetInstance || !g_GetGraphState || !g_VoicePlay)
            return;
        void* cdm  = g_GetInstance();
        void* ctrl = cdm ? g_GetGraphState(cdm) : nullptr;
        if (!ctrl)
            return;
        void* obj = *reinterpret_cast<void**>(reinterpret_cast<char*>(ctrl) + kCtrl_Object);
        if (!obj)
            return;
        const std::uint32_t ev = d.dlgEvent ? d.dlgEvent
            : *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(ctrl) + kCtrl_DlgEvent);
        const std::uint32_t ch = d.chara ? d.chara
            : *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(ctrl) + kCtrl_Chara);
        std::uint32_t voiceParam[8] = {};
        voiceParam[1] = d.condition;
        g_VoicePlay(voiceParam, obj, ev, ch);
    }

    static void PlayVoiceByName(const char* name)
    {
        if (!g_GetInstance || !g_GetGraphState || !name || !*name)
            return;
        void* cdm  = g_GetInstance();
        void* ctrl = cdm ? g_GetGraphState(cdm) : nullptr;
        if (!ctrl)
            return;
        void** vt = *reinterpret_cast<void***>(ctrl);
        using GetVoiceType_t = char(__fastcall*)(void* self, const char* name);
        using RequestVoice_t = void(__fastcall*)(void* self, std::uint8_t slot);
        const char slot = reinterpret_cast<GetVoiceType_t>(vt[0x50 / 8])(ctrl, name);
        if (static_cast<std::uint8_t>(slot) != 0x76u)
            reinterpret_cast<RequestVoice_t>(vt[0x18 / 8])(ctrl, static_cast<std::uint8_t>(slot));
    }

    static void PostDaemonSfx(std::uint32_t eventId)
    {
        if (!g_SdInstancePtr || !g_SdEnqueue || !g_kW_SelectReady || eventId == 0)
            return;
        void* daemon = *g_SdInstancePtr;
        if (!daemon)
            return;
        alignas(16) std::uint8_t block[32];
        std::memcpy(block, g_kW_Select, sizeof(g_kW_Select));
        std::memset(block + 16, 0, 16);
        std::uint64_t out = 0;
        g_SdEnqueue(daemon, &out, eventId, block, 0u, 0u);
    }

    static void __fastcall hkTypingLogActUpdate(void* self)
    {
        if (self)
        {
            auto* seIdPtr = reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(self) + kTLA_SeId);
            const std::uint32_t seId = *seIdPtr;
            if (seId >= kModSeIdBase && seId <= kModSeIdMax)
            {
                if (!MissionCodeGuard::ShouldBypassHooks())
                {
                    if (g_ModSfx[seId] != 0)
                        PostDaemonSfx(g_ModSfx[seId]);
                    else if (g_ModDialogue[seId].condition != 0)
                        PlayDialogue(g_ModDialogue[seId]);
                    else if (!g_ModVoices[seId].empty())
                        PlayVoiceByName(g_ModVoices[seId].c_str());
                    else if (g_ModEvents[seId] != 0 && g_SoundControlStart)
                    {
                        void* soundControl = *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + kTLA_SoundControl);
                        if (soundControl)
                            g_SoundControlStart(soundControl, g_ModEvents[seId], 0);
                    }
                }
                *seIdPtr = 0;
            }
        }
        g_OrigUpdate(self);
    }
}

std::uint32_t Set_AnnounceLogSE(const char* announceLabel, std::uint32_t seId)
{
    const std::uint32_t announceType = LabelToAnnounceType(announceLabel);
    if (announceType != 0)
        g_Overrides[announceType] = seId;
    return announceType;
}

static std::uint32_t AllocAnnounceSlot(std::uint32_t announceType)
{
    if (announceType == 0)
        return 0;
    std::uint32_t seId;
    const auto it = g_SeIdByType.find(announceType);
    if (it != g_SeIdByType.end())
    {
        seId = it->second;
    }
    else
    {
        if (g_NextModSeId > kModSeIdMax)
        {
            Log("[AnnounceLog] custom-sound slots exhausted (max %u)\n", kModSeIdMax - kModSeIdBase + 1u);
            return 0;
        }
        seId = g_NextModSeId++;
        g_SeIdByType[announceType] = seId;
    }
    g_Overrides[announceType] = seId;
    return seId;
}

std::uint32_t Set_AnnounceLogEvent(const char* announceLabel, const char* eventName)
{
    if (!eventName || !*eventName)
        return 0;
    const std::uint32_t announceType = LabelToAnnounceType(announceLabel);
    const std::uint32_t eventId      = FoxHashes::FNVHash32(eventName);
    if (eventId == 0)
        return 0;
    const std::uint32_t seId = AllocAnnounceSlot(announceType);
    if (seId == 0)
        return 0;
    g_ModEvents[seId]   = eventId;
    g_ModVoices[seId].clear();
    g_ModDialogue[seId] = {};
    g_ModSfx[seId]      = 0;
    Log("[AnnounceLog] '%s' -> event 0x%08X via seId 0x%02X\n", announceLabel, eventId, seId);
    return announceType;
}

std::uint32_t Set_AnnounceLogVoice(const char* announceLabel, const char* voiceName)
{
    if (!voiceName || !*voiceName)
        return 0;
    const std::uint32_t announceType = LabelToAnnounceType(announceLabel);
    const std::uint32_t seId         = AllocAnnounceSlot(announceType);
    if (seId == 0)
        return 0;
    g_ModVoices[seId]   = voiceName;
    g_ModEvents[seId]   = 0;
    g_ModDialogue[seId] = {};
    g_ModSfx[seId]      = 0;
    Log("[AnnounceLog] '%s' -> voice '%s' via seId 0x%02X\n", announceLabel, voiceName, seId);
    return announceType;
}

std::uint32_t Set_AnnounceLogDialogue(const char* announceLabel, std::uint32_t condition,
                                      std::uint32_t chara, std::uint32_t dialogueEvent)
{
    if (condition == 0)
        return 0;
    const std::uint32_t announceType = LabelToAnnounceType(announceLabel);
    const std::uint32_t seId         = AllocAnnounceSlot(announceType);
    if (seId == 0)
        return 0;
    g_ModDialogue[seId] = ModDialogue{ condition, chara, dialogueEvent };
    g_ModEvents[seId]   = 0;
    g_ModVoices[seId].clear();
    g_ModSfx[seId]      = 0;
    Log("[AnnounceLog] '%s' -> dialogue cond 0x%08X chara 0x%08X evt 0x%08X via seId 0x%02X\n",
        announceLabel, condition, chara, dialogueEvent, seId);
    return announceType;
}

std::uint32_t Set_AnnounceLogSfx(const char* announceLabel, const char* eventName)
{
    if (!eventName || !*eventName)
        return 0;
    const std::uint32_t announceType = LabelToAnnounceType(announceLabel);
    const std::uint32_t eventId      = FoxHashes::FNVHash32(eventName);
    if (eventId == 0)
        return 0;
    const std::uint32_t seId = AllocAnnounceSlot(announceType);
    if (seId == 0)
        return 0;
    g_ModSfx[seId]      = eventId;
    g_ModEvents[seId]   = 0;
    g_ModVoices[seId].clear();
    g_ModDialogue[seId] = {};
    Log("[AnnounceLog] '%s' -> gameplay sfx 0x%08X via seId 0x%02X (daemon)\n", announceLabel, eventId, seId);
    return announceType;
}

bool Register_AnnounceLogSfx(const char* eventName)
{
    if (!eventName || !*eventName)
        return false;
    const std::uint32_t id = FoxHashes::FNVHash32(eventName);
    if (id == 0)
        return false;
    g_GameplaySfx.insert(id);
    return true;
}

bool IsAnnounceLogSfxRegistered(const char* eventName)
{
    if (!eventName || !*eventName)
        return false;
    return g_GameplaySfx.find(FoxHashes::FNVHash32(eventName)) != g_GameplaySfx.end();
}

bool Install_AnnounceLogHook()
{
    if (g_Installed)
        return true;

    if (!gAddr.Hud_GetAnnounceLogSE)
    {
        Log("[AnnounceLog] address not set for this build\n");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.Hud_GetAnnounceLogSE);
    if (!target)
    {
        Log("[AnnounceLog] resolve failed\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkGetAnnounceLogSE),
        reinterpret_cast<void**>(&g_Orig));

    if (ok)
        g_Installed = true;

    Log("[AnnounceLog] hook: %s (target=%p)\n", ok ? "OK" : "FAIL", target);

    if (ok && gAddr.Hud_TypingLogActUpdate)
    {
        void* upd = ResolveGameAddress(gAddr.Hud_TypingLogActUpdate);
        if (upd && CreateAndEnableHook(
                       upd,
                       reinterpret_cast<void*>(&hkTypingLogActUpdate),
                       reinterpret_cast<void**>(&g_OrigUpdate)))
        {
            g_UpdateInstalled = true;
            if (gAddr.Ui_SoundControlStart)
                g_SoundControlStart = reinterpret_cast<SoundControlStart_t>(
                    ResolveGameAddress(gAddr.Ui_SoundControlStart));
            if (gAddr.Ui_UiCommonDataManagerGetInstance)
                g_GetInstance = reinterpret_cast<GetInstance_t>(
                    ResolveGameAddress(gAddr.Ui_UiCommonDataManagerGetInstance));
            if (gAddr.Ui_EventNodeBodyGetGraphState)
                g_GetGraphState = reinterpret_cast<GetGraphState_t>(
                    ResolveGameAddress(gAddr.Ui_EventNodeBodyGetGraphState));
            if (gAddr.VoiceParam_PlayDialogue)
                g_VoicePlay = reinterpret_cast<MbDvcVoicePlay_t>(
                    ResolveGameAddress(gAddr.VoiceParam_PlayDialogue));
            if (gAddr.SoundDaemon_Instance)
                g_SdInstancePtr = reinterpret_cast<void**>(ResolveGameAddress(gAddr.SoundDaemon_Instance));
            if (gAddr.SoundDaemon_PostEventQueue)
                g_SdEnqueue = reinterpret_cast<SoundDaemonEnqueue_t>(
                    ResolveGameAddress(gAddr.SoundDaemon_PostEventQueue));
            if (gAddr.Sd_kW_Select)
            {
                void* kw = ResolveGameAddress(gAddr.Sd_kW_Select);
                if (kw)
                {
                    std::memcpy(g_kW_Select, kw, sizeof(g_kW_Select));
                    g_kW_SelectReady = true;
                }
            }
        }

        Log("[AnnounceLog] TypingLogAct::Update hook: %s | event=%s voice/dialogue=%s\n",
            g_UpdateInstalled ? "OK" : "FAIL",
            g_SoundControlStart ? "ready" : "off",
            (g_GetInstance && g_GetGraphState && g_VoicePlay) ? "ready" : "off");
    }
    else if (ok)
    {
        Log("[AnnounceLog] custom-sound path disabled (TypingLogAct::Update address not set)\n");
    }

    return ok;
}

bool Uninstall_AnnounceLogHook()
{
    if (!g_Installed)
        return true;

    if (gAddr.Hud_GetAnnounceLogSE)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.Hud_GetAnnounceLogSE));

    if (g_UpdateInstalled && gAddr.Hud_TypingLogActUpdate)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.Hud_TypingLogActUpdate));

    g_Orig              = nullptr;
    g_Installed         = false;
    g_OrigUpdate        = nullptr;
    g_SoundControlStart = nullptr;
    g_GetInstance       = nullptr;
    g_GetGraphState     = nullptr;
    g_VoicePlay         = nullptr;
    g_UpdateInstalled   = false;
    g_Overrides.clear();
    g_SeIdByType.clear();
    g_NextModSeId       = kModSeIdBase;
    for (auto& e : g_ModEvents)
        e = 0;
    for (auto& v : g_ModVoices)
        v.clear();
    for (auto& d : g_ModDialogue)
        d = ModDialogue{};
    for (auto& s : g_ModSfx)
        s = 0;
    g_GameplaySfx.clear();
    g_SdInstancePtr  = nullptr;
    g_SdEnqueue      = nullptr;
    g_kW_SelectReady = false;
    return true;
}
