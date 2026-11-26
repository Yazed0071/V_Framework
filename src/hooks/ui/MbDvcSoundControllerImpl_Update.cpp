#include <pch.h>
#include "MbDvcSoundControllerImpl_Update.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <random>
#include <unordered_set>

#include "AddressSet.h"
#include "FeatureSwitch.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using Update_t            = void(__fastcall*)(std::uint8_t* ctrl);
    using IsAnyVoicePlaying_t = bool(__fastcall*)(std::uint8_t* ctrl);
    using PostVoice_t         = bool(__fastcall*)(std::uint32_t* voice, std::uint8_t* object, std::uint32_t dialogueEvent, std::uint32_t chara);
    using IsSoundPlaying_t    = bool(__fastcall*)(void* soundManager, std::uint32_t playingId, std::uint32_t unused);

    constexpr std::size_t kCtrl_Object      = 0x08;
    constexpr std::size_t kCtrl_DlgEvent    = 0x10;
    constexpr std::size_t kCtrl_Chara       = 0x14;
    constexpr std::size_t kCtrl_Voices      = 0x18;
    constexpr std::size_t kCtrl_Suppressed  = 0xCE6;
    constexpr std::size_t kCtrl_VoiceOff    = 0xCE7;   // the game's own iDroid voice option
    constexpr std::size_t kObj_SoundManager = 0x10;
    constexpr std::size_t kVoiceSize        = 0x1C;
    constexpr std::size_t kVoiceCount       = 0x75;
    constexpr std::size_t kVoice_Condition  = 1;
    constexpr std::size_t kVoice_PlayingId  = 2;
    constexpr std::size_t kVoice_Flags      = 3;
    constexpr std::uint32_t kVoiceRequested = 0x200;
    constexpr std::size_t kQueueMax         = 8;
    constexpr DWORD       kLineWaitMs       = 10000;   // a line that cannot start by then is dropped

    const std::uint8_t kUpdateEntry[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83, 0x79, 0x08, 0x00, 0x48, 0x8B, 0xD9, 0x74, 0x27 };
    const std::uint8_t kIsAnyVoicePlayingEntry[] = { 0x33, 0xC0, 0x48, 0x83, 0xC1, 0x24, 0xF6, 0x41, 0x01, 0x01, 0x75, 0x0E, 0xFF, 0xC0,
                                                     0x48, 0x83, 0xC1, 0x1C, 0x83, 0xF8, 0x75, 0x72, 0xEF };   // 0x75 voices of 0x1C bytes, flag 0x100 = playing
    const std::uint8_t kPostVoiceEntry[] = { 0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x18, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x50,
                                             0x48, 0x8B, 0x42, 0x10, 0x48, 0x8D, 0x72, 0x10 };   // the sound manager at object+0x10

    struct AlternateTake
    {
        std::uint32_t voice;
        std::uint32_t condition;
    };

    const AlternateTake kAlternateTakes[] = {
        { 15, 1150464565 },   // "Sandstorm approaching."
        { 16, 1150464560 },   // "Weather will clear shortly."
        { 18, 1184166793 },   // "Support now en route." beside the buddy "request received" voice
        { 24, 1234499663 },   // "New mission available."
        { 81, 1150464568 },   // "Extraction complete."
        { 93, 1268054952 },   // "Nuclear weapon disarmament complete."
        { 98, 1167389213 },   // "Staff dispatch complete." beside "Unit dispatched", the voice for a sent Combat Deployment team
    };

    struct QueuedLine
    {
        std::uint32_t condition     = 0;
        std::uint32_t chara         = 0;
        std::uint32_t dialogueEvent = 0;
        DWORD         notBefore     = 0;
        DWORD         deadline      = 0;
    };

    struct OfferedTake
    {
        std::uint32_t condition = 0;
        DWORD         deadline  = 0;
    };

    Update_t            g_OrigUpdate        = nullptr;
    IsAnyVoicePlaying_t g_IsAnyVoicePlaying = nullptr;   // the game's own voices only
    PostVoice_t         g_PostVoice         = nullptr;   // never swaps in an alternate take
    void*               g_UpdateTarget      = nullptr;
    void*               g_IsAnyTarget       = nullptr;
    void*               g_PostVoiceTarget   = nullptr;

    std::mutex                        g_Mutex;
    std::deque<QueuedLine>            g_Queue;
    OfferedTake                       g_OfferedTakes[kVoiceCount] = {};   // by voice: a voice the game also asks for elsewhere keeps its own line there
    std::unordered_set<std::uint32_t> g_Warned;
    std::atomic<std::uint8_t*>        g_Ctrl{ nullptr };
    std::atomic<std::uint32_t>        g_PlayingCondition{ 0 };
    std::uint32_t                     g_PlayingId      = 0;
    bool                              g_LetGameGoFirst = false;
    std::minstd_rand                  g_Coin{ GetTickCount() };

    bool StillPlaying(std::uint8_t* object, std::uint32_t playingId)
    {
        void*  soundManager = object + kObj_SoundManager;
        void** vt = *static_cast<void***>(soundManager);
        return reinterpret_cast<IsSoundPlaying_t>(vt[1])(soundManager, playingId, 0);
    }

    bool TakeNextLine(QueuedLine& line)
    {
        const DWORD now = GetTickCount();
        std::lock_guard<std::mutex> lock(g_Mutex);
        while (!g_Queue.empty() && static_cast<LONG>(now - g_Queue.front().deadline) >= 0)
            g_Queue.pop_front();
        if (g_Queue.empty() || static_cast<LONG>(now - g_Queue.front().notBefore) < 0)
            return false;
        line = g_Queue.front();
        g_Queue.pop_front();
        return true;
    }

    void PostNextLine(std::uint8_t* ctrl, std::uint8_t* object)
    {
        if (g_PlayingId)
        {
            if (StillPlaying(object, g_PlayingId))
                return;
            g_PlayingId = 0;
            g_PlayingCondition.store(0, std::memory_order_relaxed);
            g_LetGameGoFirst = true;   // the game's waiting voices get one update to start before our next line
            return;
        }
        if (g_LetGameGoFirst)
        {
            g_LetGameGoFirst = false;
            return;
        }
        if (ctrl[kCtrl_Suppressed] || ctrl[kCtrl_VoiceOff] || g_IsAnyVoicePlaying(ctrl))
            return;

        QueuedLine line;
        if (!TakeNextLine(line))
            return;

        std::uint32_t voice[kVoiceSize / sizeof(std::uint32_t)] = {};
        voice[kVoice_Condition] = line.condition;
        const std::uint32_t dialogueEvent = line.dialogueEvent ? line.dialogueEvent : *reinterpret_cast<std::uint32_t*>(ctrl + kCtrl_DlgEvent);
        const std::uint32_t chara         = line.chara ? line.chara : *reinterpret_cast<std::uint32_t*>(ctrl + kCtrl_Chara);
        if (g_PostVoice(voice, object, dialogueEvent, chara) && voice[kVoice_PlayingId])
        {
            g_PlayingId = voice[kVoice_PlayingId];
            g_PlayingCondition.store(line.condition, std::memory_order_relaxed);
            LogDebug("IdroidLines: line %u playing as %u", line.condition, g_PlayingId);
            return;
        }

        bool firstTime = false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            firstTime = g_Warned.insert(line.condition ^ chara ^ dialogueEvent).second;
        }
        if (firstTime)
            LogWarn("IdroidLines: iDroid line %u (chara 0x%08X, event 0x%08X) did not play", line.condition, chara, dialogueEvent);
    }

    void __fastcall hkUpdate(std::uint8_t* ctrl)
    {
        g_Ctrl.store(ctrl, std::memory_order_relaxed);
        g_OrigUpdate(ctrl);
        auto* object = ctrl ? *reinterpret_cast<std::uint8_t**>(ctrl + kCtrl_Object) : nullptr;
        if (object)
            PostNextLine(ctrl, object);
    }

    bool __fastcall hkIsAnyVoicePlaying(std::uint8_t* ctrl)
    {
        return g_IsAnyVoicePlaying(ctrl) || g_PlayingCondition.load(std::memory_order_relaxed) != 0;
    }

    std::uint32_t OfferedTakeFor(std::uint32_t slot)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const OfferedTake offer = g_OfferedTakes[slot];
        g_OfferedTakes[slot] = {};
        return static_cast<LONG>(GetTickCount() - offer.deadline) < 0 ? offer.condition : 0;
    }

    std::uint32_t AlternateTakeFor(const std::uint32_t* voice)
    {
        const std::uint8_t* ctrl = g_Ctrl.load(std::memory_order_relaxed);
        if (!ctrl || !FeatureSwitch::IsOn(FeatureSwitch::Feature::RestoredIdroidLines))
            return 0;
        const std::ptrdiff_t offset = reinterpret_cast<const std::uint8_t*>(voice) - (ctrl + kCtrl_Voices);
        if (offset < 0 || offset >= static_cast<std::ptrdiff_t>(kVoiceSize * kVoiceCount) || offset % kVoiceSize != 0)
            return 0;
        const auto slot = static_cast<std::uint32_t>(offset / kVoiceSize);
        std::uint32_t take = OfferedTakeFor(slot);
        for (const AlternateTake& always : kAlternateTakes)
        {
            if (!take && always.voice == slot)
                take = always.condition;
        }
        return take && std::bernoulli_distribution(0.5)(g_Coin) ? take : 0;
    }

    bool __fastcall hkPostVoice(std::uint32_t* voice, std::uint8_t* object, std::uint32_t dialogueEvent, std::uint32_t chara)
    {
        const std::uint32_t take = AlternateTakeFor(voice);
        if (take)
        {
            const std::uint32_t vanilla = voice[kVoice_Condition];
            voice[kVoice_Condition] = take;
            const bool posted = g_PostVoice(voice, object, dialogueEvent, chara);
            voice[kVoice_Condition] = vanilla;
            LogDebug("IdroidLines: voice line %u swapped for its alternate take %u, posted %d", vanilla, take, posted);
            if (posted)
                return true;
        }
        return g_PostVoice(voice, object, dialogueEvent, chara);
    }

    bool Hook(void* target, const std::uint8_t* entry, std::size_t size, void* detour, void** original, const char* what)
    {
        const HookEntry state = CheckHookEntry(target, entry, size);
        if (state == HookEntry::Unknown)
        {
            char found[160] = {};
            DescribeFunctionEntry(target, found, sizeof(found));
            LogError("IdroidLines: %s is not the code we know (%s), restored iDroid lines off", what, found);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(what, target);
        if (CreateAndEnableHook(target, detour, original))
            return true;
        LogError("IdroidLines: %s hook failed at %p, restored iDroid lines off", what, target);
        return false;
    }
}

bool Install_MbDvcSoundControllerImpl_Update_Hooks()
{
    if (g_UpdateTarget)
        return true;
    if (!gAddr.MbDvcSoundControllerImpl_Update || !gAddr.MbDvcSoundControllerImpl_IsAnyVoicePlaying || !gAddr.VoiceParam_PlayDialogue)
    {
        LogError("IdroidLines: no iDroid voice addresses for this build, restored iDroid lines off");
        return false;
    }

    void* isAny  = ResolveGameAddress(gAddr.MbDvcSoundControllerImpl_IsAnyVoicePlaying);
    void* post   = ResolveGameAddress(gAddr.VoiceParam_PlayDialogue);
    void* update = ResolveGameAddress(gAddr.MbDvcSoundControllerImpl_Update);
    if (Hook(isAny, kIsAnyVoicePlayingEntry, sizeof(kIsAnyVoicePlayingEntry), reinterpret_cast<void*>(&hkIsAnyVoicePlaying),
             reinterpret_cast<void**>(&g_IsAnyVoicePlaying), "MbDvcSoundControllerImpl::IsAnyVoicePlaying"))
        g_IsAnyTarget = isAny;
    if (g_IsAnyTarget && Hook(post, kPostVoiceEntry, sizeof(kPostVoiceEntry), reinterpret_cast<void*>(&hkPostVoice),
                              reinterpret_cast<void**>(&g_PostVoice), "MbDvcSoundControllerImpl::PostVoice"))
        g_PostVoiceTarget = post;
    if (g_PostVoiceTarget && Hook(update, kUpdateEntry, sizeof(kUpdateEntry), reinterpret_cast<void*>(&hkUpdate),
                                  reinterpret_cast<void**>(&g_OrigUpdate), "MbDvcSoundControllerImpl::Update"))
        g_UpdateTarget = update;

    if (g_UpdateTarget)
        return true;
    Uninstall_MbDvcSoundControllerImpl_Update_Hooks();
    return false;
}

void Uninstall_MbDvcSoundControllerImpl_Update_Hooks()
{
    if (g_UpdateTarget)
        DisableAndRemoveHook(g_UpdateTarget);
    if (g_PostVoiceTarget)
        DisableAndRemoveHook(g_PostVoiceTarget);
    if (g_IsAnyTarget)
        DisableAndRemoveHook(g_IsAnyTarget);
    g_UpdateTarget    = nullptr;
    g_PostVoiceTarget = nullptr;
    g_IsAnyTarget     = nullptr;
    g_Ctrl.store(nullptr, std::memory_order_relaxed);
    g_PlayingCondition.store(0, std::memory_order_relaxed);
    g_PlayingId      = 0;
    g_LetGameGoFirst = false;

    std::lock_guard<std::mutex> lock(g_Mutex);
    g_Queue.clear();
    g_Warned.clear();
    for (OfferedTake& offer : g_OfferedTakes)
        offer = {};
}

bool IdroidVoice_IsReady()
{
    return g_UpdateTarget != nullptr;
}

bool IdroidVoice_Request(std::uint32_t condition, std::uint32_t chara, std::uint32_t dialogueEvent, std::uint32_t delayMs)
{
    if (!condition || !g_UpdateTarget)
        return false;
    const DWORD now = GetTickCount();
    std::lock_guard<std::mutex> lock(g_Mutex);
    for (const QueuedLine& line : g_Queue)
    {
        if (line.condition == condition && line.chara == chara && line.dialogueEvent == dialogueEvent)
            return true;
    }
    if (g_PlayingCondition.load(std::memory_order_relaxed) == condition)
        return true;
    if (g_Queue.size() >= kQueueMax)
        return false;
    g_Queue.push_back(QueuedLine{ condition, chara, dialogueEvent, now + delayMs, now + delayMs + kLineWaitMs });
    return true;
}

void IdroidVoice_OfferTake(std::uint8_t voice, std::uint32_t condition)
{
    const std::uint8_t* ctrl = g_Ctrl.load(std::memory_order_relaxed);
    if (!condition || !ctrl || !g_UpdateTarget || voice >= kVoiceCount)
        return;
    const auto* record = reinterpret_cast<const std::uint32_t*>(ctrl + kCtrl_Voices + voice * kVoiceSize);
    if (!(record[kVoice_Flags] & kVoiceRequested))
    {
        LogDebug("IdroidLines: the game did not ask for voice %u, line %u is said on its own", static_cast<unsigned>(voice), condition);
        IdroidVoice_Request(condition);
        return;
    }
    LogDebug("IdroidLines: voice %u may be said as line %u", static_cast<unsigned>(voice), condition);
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_OfferedTakes[voice] = OfferedTake{ condition, GetTickCount() + kLineWaitMs };
}
