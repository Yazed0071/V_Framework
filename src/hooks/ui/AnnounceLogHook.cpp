#include <pch.h>
#include "AnnounceLogHook.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>

#include "AddressSet.h"
#include "BaseControllerImpl_CompletePlatformDeck.h"
#include "FeatureSwitch.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "MbDvcSoundControllerImpl_Update.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using GetAnnounceLogSE_t = std::uint32_t(__fastcall*)(void* manager, int announceType);

    static GetAnnounceLogSE_t g_Orig      = nullptr;
    static bool               g_Installed = false;

    static constexpr std::uint32_t kVoiceSeBase    = 0x87u;   // sound bytes from here up are iDroid voice (byte - 0x87)
    static constexpr std::uint32_t kVoiceCount     = 0x75u;
    static constexpr std::uint8_t  kNoVoice        = 0x76u;
    static constexpr std::uint32_t kModMarkerFirst = kVoiceSeBase + kVoiceCount;   // 0xFC-0xFF: past the last voice, never vanilla
    static constexpr std::uint32_t kModMarkerCount = 4u;

    static constexpr std::size_t kTLA_Cdm          = 0x88;
    static constexpr std::size_t kTLA_SoundControl = 0x90;
    static constexpr std::size_t kTLA_LogOffset    = 0x99;
    static constexpr std::size_t kTLA_CharInterval = 0x9C;   // seconds per typed character, 0.01 unless the act's param says otherwise
    static constexpr std::size_t kTLA_HoldTime     = 0xA0;   // seconds a typed line stays, 1.5 minus a little per row
    static constexpr std::size_t kTLA_TypeTimer    = 0xA4;
    static constexpr std::size_t kTLA_SeId         = 0xA8;
    static constexpr std::size_t kTLA_TypedChars   = 0xAC;
    static constexpr std::size_t kTLA_TypingDone   = 0xB0;
    static constexpr float       kVanillaHoldTime  = 1.5f;

    static constexpr std::size_t kCtrl_Object   = 0x08;
    static constexpr std::size_t kCtrl_DlgEvent = 0x10;
    static constexpr std::size_t kCtrl_Chara    = 0x14;

    static constexpr std::size_t kCdm_AnnounceLog       = 0x4A0;
    static constexpr std::size_t kCdm_AnnounceLogCursor = 0x1134;
    static constexpr std::size_t kLog_Head              = 0x988;
    static constexpr std::size_t kLog_Entries           = 0x28;
    static constexpr std::size_t kLog_EntrySize         = 0x18;
    static constexpr std::size_t kEntry_SeId            = 0x03;
    static constexpr unsigned    kAnnounceLogSlots      = 100;
    static constexpr std::size_t kAnnounceQueueMax      = 256;

    // TypingLogAct::Update up to its sound check, proves the act offsets above; call and jump targets are masked
    static constexpr std::uint8_t kUpdateEntry[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0x91, 0x98, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD9,
        0x48, 0x8B, 0x89, 0x88, 0x00, 0x00, 0x00, 0x48, 0x89, 0x6C, 0x24, 0x30, 0xE8, 0x00, 0x00, 0x00,
        0x00, 0x48, 0x8B, 0x93, 0x88, 0x00, 0x00, 0x00, 0x44, 0x0F, 0xB6, 0x82, 0x34, 0x11, 0x00, 0x00,
        0x0F, 0xB6, 0xE8, 0x44, 0x02, 0x83, 0x99, 0x00, 0x00, 0x00, 0x83, 0xBB, 0xAC, 0x00, 0x00, 0x00,
        0x00, 0x44, 0x88, 0x83, 0x9A, 0x00, 0x00, 0x00, 0x0F, 0x85, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x93,
        0xA8, 0x00, 0x00, 0x00, 0x85, 0xD2,
    };
    static constexpr std::size_t kUpdateCallRel = 0x1D;
    static constexpr std::size_t kUpdateJumpRel = 0x4A;

    // Update's voice branch: GetQuarkSystemTable()+0x98 -> +0x40 -> +0x18, vt+0x548 -> +0x18 = the last voice played
    static constexpr std::size_t  kUpdateLastVoiceAt = 0x92;
    static constexpr std::uint8_t kUpdateLastVoice[] = {
        0x48, 0x89, 0x7C, 0x24, 0x40, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x88, 0x98, 0x00, 0x00,
        0x00, 0x48, 0x8B, 0x51, 0x40, 0x48, 0x8B, 0x7A, 0x18, 0x48, 0x8B, 0xCF, 0x48, 0x8B, 0x17, 0xFF,
        0x92, 0x48, 0x05, 0x00, 0x00, 0x8B, 0x48, 0x18, 0x3B, 0x8B, 0xA8, 0x00, 0x00, 0x00,
    };
    static constexpr std::size_t   kLastVoiceCallRel = 0x06;
    static constexpr std::uint32_t kModSoundKeyBit   = 0x80000000u;   // never a voice number (0-0x74) or vanilla's cleared 0

    using TypingLogActUpdate_t = void(__fastcall*)(void* self);
    using SoundControlStart_t  = void(__fastcall*)(void* soundControl, std::uint64_t eventId, std::uint8_t flag);
    using GetInstance_t        = void*(__fastcall*)();
    using GetGraphState_t      = void*(__fastcall*)(void* cdm);
    using MbDvcVoicePlay_t     = char(__fastcall*)(void* voiceParam, void* obj, std::uint32_t dlgEvent, std::uint32_t chara);
    using GetVoiceTypeByName_t = std::uint8_t(__fastcall*)(void* soundController, const char* name);
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using AnnounceVoiceState_t  = std::uint8_t*(__fastcall*)(void* self);
    using RequestVoice_t        = void(__fastcall*)(void* soundController, std::uint8_t voice);
    using StartUiSound_t        = void(__fastcall*)(void* soundControl, std::uint8_t uiSe);

    static TypingLogActUpdate_t g_OrigUpdate        = nullptr;
    static SoundControlStart_t  g_SoundControlStart = nullptr;
    static StartUiSound_t       g_StartUiSound      = nullptr;
    static GetInstance_t        g_GetInstance       = nullptr;
    static GetGraphState_t      g_GetGraphState     = nullptr;
    static MbDvcVoicePlay_t     g_VoicePlay         = nullptr;
    static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    static bool                 g_UpdateInstalled   = false;

    struct ModDialogue { std::uint32_t condition; std::uint32_t chara; std::uint32_t dlgEvent; };

    struct ModAnnouncePayload
    {
        std::string   name;
        std::uint32_t se       = 0;
        ModDialogue   dialogue = {};
        bool          hasExtra = false;
        std::uint32_t extraSe  = 0;
        std::string   extraName;
    };

    static std::mutex g_Mutex;

    static std::unordered_map<std::uint32_t, std::uint32_t>      g_Overrides;
    static std::unordered_map<std::uint32_t, ModAnnouncePayload> g_Payloads;
    static std::unordered_set<std::uint32_t>                     g_GameplaySfx;
    static std::unordered_set<std::string>                       g_Warned;

    static std::atomic<float> g_TypingSpeed{ 1.0f };
    static std::atomic<float> g_HoldTime{ -1.0f };   // below 0 = the game's own

    struct TypingActState
    {
        void* act             = nullptr;
        float baseInterval    = 0.0f;
        float writtenInterval = 0.0f;
        float baseHold        = 0.0f;
        float writtenHold     = 0.0f;
        float extraChars      = 0.0f;
    };
    static constexpr unsigned kTypingActSlots = 16;
    static TypingActState     g_TypingActs[kTypingActSlots] = {};   // only TypingLogAct::Update touches it

    struct BuiltInLine
    {
        const char*   label;
        std::uint32_t condition         = 0;          // restored iDroid line
        std::uint8_t  voice             = kNoVoice;   // or a vanilla voice the game never plays for this announce
        std::uint32_t extraSe           = 0;          // 0 = the game's own UI sound
        bool          firstPlatformDeck = false;
        std::uint32_t type              = 0;
    };

    static constexpr std::uint32_t kLineLevelUp    = 1167242151;   // "Unit level increased."
    static constexpr std::uint32_t kLineLevelDown  = 1167242144;   // "Unit level decreased."
    static constexpr std::uint32_t kLinePlatform   = 1167242157;   // "Platform construction complete."
    static constexpr std::uint32_t kPlatformJingle = 0x59;         // vanilla platform voice's partner sound

    static BuiltInLine g_BuiltInLines[] = {
        { "announce_unit_lvup_combat", kLineLevelUp },
        { "announce_unit_lvup_RD", kLineLevelUp },
        { "announce_unit_lvup_base_dev", kLineLevelUp },
        { "announce_unit_lvup_support", kLineLevelUp },
        { "announce_unit_lvup_intel", kLineLevelUp },
        { "announce_unit_lvup_medical", kLineLevelUp },
        { "announce_unit_lvup_security", kLineLevelUp },
        { "announce_unit_lvdown_combat", kLineLevelDown },
        { "announce_unit_lvdown_RD", kLineLevelDown },
        { "announce_unit_lvdown_base_dev", kLineLevelDown },
        { "announce_unit_lvdown_support", kLineLevelDown },
        { "announce_unit_lvdown_intel", kLineLevelDown },
        { "announce_unit_lvdown_medical", kLineLevelDown },
        { "announce_unit_lvdown_security", kLineLevelDown },
        { "announce_target_extract", 1150464567 },       // "Extraction successful."
        { "announce_target_extract_a", 1150464567 },
        { "announce_target_extract_num", 1150464567 },
        { "announce_func_resume", 1200944483 },          // "Functionality has been restored."
        { "announce_func_suspend", 1200944480 },         // "Function unavailable."
        { "announce_quest_add", 1217722064 },            // "New side op available."
        { "announce_quest_delete", 1217722065 },         // "Side op canceled."
        { "announce_mission_add_emerg", 1234499662 },    // "Emergency op available."
        { "announce_mission_del_emerg", 1217722068 },    // "Emergency op canceled."
        { "announce_ops_success_mission", 0, 99 },       // vanilla voices the native dispatch result never asks for
        { "announce_ops_failed_mission", 0, 100 },
        { "announce_platform_add_command", kLinePlatform, kNoVoice, kPlatformJingle, true },
        { "announce_platform_add_combat", kLinePlatform, kNoVoice, kPlatformJingle, true },
        { "announce_platform_add_RD", kLinePlatform, kNoVoice, kPlatformJingle, true },
        { "announce_platform_add_base_dev", kLinePlatform, kNoVoice, kPlatformJingle, true },
        { "announce_platform_add_support", kLinePlatform, kNoVoice, kPlatformJingle, true },
        { "announce_platform_add_intel", kLinePlatform, kNoVoice, kPlatformJingle, true },
        { "announce_platform_add_medical", kLinePlatform, kNoVoice, kPlatformJingle, true },
    };

    struct EntrySound
    {
        std::uint32_t      type    = 0;
        std::uint32_t      gameSe  = 0;   // what the game would have played
        const BuiltInLine* builtIn = nullptr;
    };

    static EntrySound        g_MarkerSound[kModMarkerCount]  = {};
    static std::uint32_t     g_NextMarker                    = 0;
    static EntrySound        g_EntrySound[kAnnounceLogSlots] = {};
    static std::atomic<bool> g_LogLayoutBad{ false };

    struct AnsweredSound
    {
        bool          set   = false;
        std::uint32_t se    = 0;
        EntrySound    sound = {};
        LONGLONG      until = 0;
    };
    static thread_local AnsweredSound t_LastAnswer;   // the unit level-up announce asks for its sound, then logs a fixed 0x59
    static LONGLONG                   g_AnswerWindow = 0;

    static LONGLONG Now()
    {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return t.QuadPart;
    }

    using SoundDaemonEnqueue_t = void*(__fastcall*)(void* daemon, void* out, std::uint32_t eventId,
                                                    void* paramBlock, std::uint32_t muteType, std::uint8_t flag);
    static void**               g_SdInstancePtr  = nullptr;
    static SoundDaemonEnqueue_t g_SdEnqueue      = nullptr;
    static std::uint8_t         g_kW_Select[16]  = {};
    static bool                 g_kW_SelectReady = false;

    static std::uint32_t LabelToAnnounceType(const char* label)
    {
        if (!label || !*label)
            return 0;
        return static_cast<std::uint32_t>(FoxHashes::StrCode64(label) & 0xFFFFFFFFull);
    }

    static bool IsModMarker(std::uint32_t se)
    {
        return se >= kModMarkerFirst && se < kModMarkerFirst + kModMarkerCount;
    }

    static bool FirstWarning(const std::string& key)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_Warned.insert(key).second;
    }

    using AnnounceLogView_t = std::uint64_t (__fastcall*)(void* cdm, const char* text,
                                                          std::uint8_t type, std::uint8_t se,
                                                          bool important);
    static AnnounceLogView_t g_OrigAnnounceLogViewDiag = nullptr;
    static void* g_ViewTarget = nullptr;

    static constexpr DWORD kAnnounceReportMs = 1000;

    static long  g_AnnounceOverflowed  = 0;
    static DWORD g_LastOverflowReport  = 0;

    struct DeferredAnnounce
    {
        std::string   text;
        std::uint8_t  type      = 0;
        std::uint8_t  se        = 0;
        bool          important = false;
        EntrySound    sound     = {};
    };

    static std::deque<DeferredAnnounce> g_AnnounceQueue;
    static void*                        g_AnnounceCdm = nullptr;

    static std::uint8_t* AnnounceLog(void* cdm)
    {
        return static_cast<std::uint8_t*>(cdm) + kCdm_AnnounceLog;
    }

    static unsigned AnnounceLogCursor(void* cdm)
    {
        if (!cdm)
            return kAnnounceLogSlots;
        return *(reinterpret_cast<const std::uint8_t*>(cdm) + kCdm_AnnounceLogCursor);
    }

    static bool EngineHasAnnounceRoom(void* cdm)
    {
        if (!cdm)
            return false;
        return AnnounceLogCursor(cdm) + 1u < kAnnounceLogSlots;
    }

    static unsigned SlotBefore(unsigned slot, unsigned back)
    {
        return (slot + kAnnounceLogSlots - back) % kAnnounceLogSlots;
    }

    // the fields AnnounceLog_BubbleNewEntryByPriority carries when it swaps two lines
    static bool SameLogLine(const std::uint8_t* a, const std::uint8_t* b)
    {
        return std::memcmp(a, b, 5) == 0 && std::memcmp(a + 8, b + 8, 4) == 0;
    }

    // AnnounceLogView lets a priority line swap back past older ones; returns how many slots it moved, -1 if not found
    static int NewLineMovedBy(const std::uint8_t* entries, const std::uint8_t* before, unsigned head, std::uint8_t se)
    {
        for (unsigned moved = 0; moved < kAnnounceLogSlots; ++moved)
        {
            const unsigned at = SlotBefore(head, moved);
            if (entries[at * kLog_EntrySize + kEntry_SeId] != se)
                continue;
            bool olderMovedUp = true;
            for (unsigned j = 1; j <= moved && olderMovedUp; ++j)
            {
                const unsigned to = (at + j) % kAnnounceLogSlots;
                olderMovedUp = SameLogLine(entries + to * kLog_EntrySize, before + SlotBefore(to, 1) * kLog_EntrySize);
            }
            if (olderMovedUp)
                return static_cast<int>(moved);
        }
        return -1;
    }

    static std::uint64_t ViewAndBind(void* cdm, const char* text, std::uint8_t type, std::uint8_t se,
                                     bool important, const EntrySound& sound)
    {
        std::uint8_t before[kAnnounceLogSlots * kLog_EntrySize];
        if (cdm)
            std::memcpy(before, AnnounceLog(cdm) + kLog_Entries, sizeof(before));

        const std::uint64_t shown = g_OrigAnnounceLogViewDiag(cdm, text, type, se, important);
        if (!shown || !cdm)
            return shown;

        const std::uint8_t* log  = AnnounceLog(cdm);
        const std::uint8_t  head = log[kLog_Head];
        const int moved = head < kAnnounceLogSlots ? NewLineMovedBy(log + kLog_Entries, before, head, se) : -1;
        if (moved >= 0)
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            for (unsigned j = 0; j < static_cast<unsigned>(moved); ++j)
                g_EntrySound[SlotBefore(head, j)] = g_EntrySound[SlotBefore(head, j + 1)];
            g_EntrySound[SlotBefore(head, static_cast<unsigned>(moved))] = sound;
        }
        else if (IsModMarker(se) && !g_LogLayoutBad.exchange(true))
        {
            LogError("AnnounceLog: announce log is not laid out as expected, custom announce sounds off");
        }
        return shown;
    }

    static void DeferAnnounce(const char* text, std::uint8_t type, std::uint8_t se,
                              bool important, const EntrySound& sound, DWORD now)
    {
        long lost = 0;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (g_AnnounceQueue.size() < kAnnounceQueueMax)
            {
                DeferredAnnounce entry;
                entry.text      = text ? text : "";
                entry.type      = type;
                entry.se        = se;
                entry.important = important;
                entry.sound     = sound;
                g_AnnounceQueue.push_back(std::move(entry));
                return;
            }
            lost = ++g_AnnounceOverflowed;
            if (now - g_LastOverflowReport < kAnnounceReportMs)
                return;
            g_LastOverflowReport = now;
        }
        LogWarn("AnnounceLog: deferred queue full at %zu, %ld announces lost", kAnnounceQueueMax, lost);
    }

    static void DrainDeferredAnnounces()
    {
        if (!g_OrigAnnounceLogViewDiag)
            return;
        for (;;)
        {
            void* cdm = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_Mutex);
                if (g_AnnounceQueue.empty())
                    return;
                cdm = g_AnnounceCdm;
            }
            if (!EngineHasAnnounceRoom(cdm))
                return;

            DeferredAnnounce entry;
            {
                std::lock_guard<std::mutex> lock(g_Mutex);
                if (g_AnnounceQueue.empty())
                    return;
                entry = std::move(g_AnnounceQueue.front());
                g_AnnounceQueue.pop_front();
            }
            ViewAndBind(cdm, entry.text.c_str(), entry.type, entry.se, entry.important, entry.sound);
        }
    }

    static std::uint64_t __fastcall hkAnnounceLogViewDiag(void* cdm, const char* text,
                                                          std::uint8_t type, std::uint8_t se,
                                                          bool important)
    {
        const AnsweredSound answer = t_LastAnswer;
        t_LastAnswer = {};
        const bool answered = answer.set && Now() < answer.until;

        EntrySound sound;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (cdm)
                g_AnnounceCdm = cdm;
            if (answered && IsModMarker(answer.se))
            {
                sound = answer.sound;
                if (!IsModMarker(se))
                    sound.gameSe = se;   // native code that logs its own fixed sound
            }
            else if (!answered && IsModMarker(se))
            {
                sound = g_MarkerSound[se - kModMarkerFirst];
            }
        }
        if (answered)
            se = static_cast<std::uint8_t>(answer.se);

        if (g_UpdateInstalled)
        {
            DrainDeferredAnnounces();
            bool queued = false;
            {
                std::lock_guard<std::mutex> lock(g_Mutex);
                queued = !g_AnnounceQueue.empty();
            }
            if (queued || !EngineHasAnnounceRoom(cdm))
            {
                DeferAnnounce(text, type, se, important, sound, GetTickCount());
                return 1;
            }
        }

        return g_OrigAnnounceLogViewDiag
             ? ViewAndBind(cdm, text, type, se, important, sound)
             : 0;
    }

    static const BuiltInLine* BuiltInLineFor(std::uint32_t announceType)
    {
        if (!FeatureSwitch::IsOn(FeatureSwitch::Feature::RestoredIdroidLines))
            return nullptr;
        for (const BuiltInLine& line : g_BuiltInLines)
        {
            if (line.type == announceType && (!line.firstPlatformDeck || IsCompletingFirstPlatformDeck()))
                return &line;
        }
        return nullptr;
    }

    // a mod's own SetAnnounceLogSE wins outside FOB; the restored lines play everywhere
    static AnsweredSound AnswerFor(std::uint32_t announceType, std::uint32_t gameSe)
    {
        const LONGLONG until        = Now() + g_AnswerWindow;
        const bool     customSounds = g_UpdateInstalled && !g_LogLayoutBad;
        EntrySound     sound{ announceType, gameSe, nullptr };

        std::lock_guard<std::mutex> lock(g_Mutex);
        const bool modSoundsHere = !MissionCodeGuard::ShouldBypassHooks();
        const auto plain = modSoundsHere ? g_Overrides.find(announceType) : g_Overrides.end();
        if (plain != g_Overrides.end())
            return AnsweredSound{ true, plain->second, sound, until };
        const auto payload = modSoundsHere ? g_Payloads.find(announceType) : g_Payloads.end();
        if (payload != g_Payloads.end() && !customSounds)
        {
            if (payload->second.name.empty() && payload->second.dialogue.condition == 0)
                return AnsweredSound{ true, payload->second.se, sound, until };   // no custom playback: the main sound plays, without its extra
            return {};
        }
        if (payload == g_Payloads.end())
        {
            sound.builtIn = customSounds ? BuiltInLineFor(announceType) : nullptr;
            if (!sound.builtIn)
                return {};
        }

        const std::uint32_t marker = g_NextMarker;
        g_NextMarker = (g_NextMarker + 1u) % kModMarkerCount;
        g_MarkerSound[marker] = sound;
        return AnsweredSound{ true, kModMarkerFirst + marker, sound, until };
    }

    static std::uint32_t __fastcall hkGetAnnounceLogSE(void* manager, int announceType)
    {
        t_LastAnswer = {};
        const std::uint32_t gameSe = g_Orig ? g_Orig(manager, announceType) : 0u;
        const AnsweredSound answer = AnswerFor(static_cast<std::uint32_t>(announceType), gameSe);
        if (!answer.set)
            return gameSe;
        t_LastAnswer = answer;
        return answer.se;
    }

    using GetAnnounceLogType_t = std::uint64_t(__fastcall*)(void* cdm, std::uint64_t langId);
    static GetAnnounceLogType_t g_OrigGetAnnounceLogType = nullptr;
    static void*                g_TypeTarget             = nullptr;

    static constexpr std::uint8_t kGetAnnounceLogTypeEntry[] = { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0xCA, 0x33, 0xD2, 0xE8, 0x00, 0x00, 0x00, 0x00,
                                                                 0x48, 0x83, 0xC4, 0x28, 0xC3 };   // GetLangAnnounceType(langId, 0)
    static constexpr std::uint8_t kGetAnnounceLogTypeMask[]  = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1 };

    // native announces such as the dispatch results never ask GetAnnounceLogSE, but every view asks for the log type
    static std::uint64_t __fastcall hkGetAnnounceLogType(void* cdm, std::uint64_t langId)
    {
        const std::uint64_t logType = g_OrigGetAnnounceLogType(cdm, langId);
        const auto announceType = static_cast<std::uint32_t>(langId);
        if (t_LastAnswer.set && t_LastAnswer.sound.type == announceType)
            t_LastAnswer.until = Now() + g_AnswerWindow;   // GetAnnounceLogSE answered it and knows the game's own sound
        else
            t_LastAnswer = AnswerFor(announceType, 0u);    // an answer another announce left behind must not reach this one
        return logType;
    }

    using GetLangAnnounceType_t = std::uint64_t(__fastcall*)(std::uint64_t langId, const char* label);
    static GetLangAnnounceType_t g_OrigGetLangAnnounceType = nullptr;
    static void*                 g_LangTypeTarget          = nullptr;

    static constexpr std::uint8_t kGetLangAnnounceTypeEntry[] = { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30,
                                                                  0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF,
                                                                  0x48, 0x89, 0x74, 0x24, 0x58, 0x48, 0x8B, 0xC2, 0x48, 0x85, 0xD2 };

    static constexpr std::uint8_t kTypeJumpsAhead  = 0x20;   // a new line with it moves ahead of waiting lines without it
    static constexpr std::uint8_t kTypeWaitsBehind = 0x40;   // every new line moves ahead of a waiting line with it

    static std::unordered_map<std::uint32_t, std::uint8_t> g_Priorities;

    // every announce gets its type here, and the log reorders by nothing else
    static std::uint64_t __fastcall hkGetLangAnnounceType(std::uint64_t langId, const char* label)
    {
        const std::uint64_t type = g_OrigGetLangAnnounceType(langId, label);
        std::uint8_t priority = 0;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            const auto it = g_Priorities.find(static_cast<std::uint32_t>(langId));
            if (it == g_Priorities.end() || MissionCodeGuard::ShouldBypassHooks())
                return type;
            priority = it->second;
        }
        return (type & ~static_cast<std::uint64_t>(kTypeJumpsAhead | kTypeWaitsBehind)) | priority;
    }

    static void* SoundController()
    {
        if (!g_GetInstance || !g_GetGraphState)
            return nullptr;
        void* cdm = g_GetInstance();
        return cdm ? g_GetGraphState(cdm) : nullptr;
    }

    static bool PlayDialogue(const ModDialogue& d)
    {
        if (IdroidVoice_IsReady())
            return IdroidVoice_Request(d.condition, d.chara, d.dlgEvent);   // waits for the game's own iDroid voices, and they wait for it

        void* ctrl = g_VoicePlay ? SoundController() : nullptr;
        if (!ctrl)
            return false;
        void* obj = *reinterpret_cast<void**>(reinterpret_cast<char*>(ctrl) + kCtrl_Object);
        if (!obj)
            return false;
        const std::uint32_t ev = d.dlgEvent ? d.dlgEvent
            : *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(ctrl) + kCtrl_DlgEvent);
        const std::uint32_t ch = d.chara ? d.chara
            : *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(ctrl) + kCtrl_Chara);
        std::uint32_t voiceParam[8] = {};
        voiceParam[1] = d.condition;
        return g_VoicePlay(voiceParam, obj, ev, ch) != 0;
    }

    static void RequestVoice(std::uint8_t voice)
    {
        void* ctrl = SoundController();
        if (!ctrl)
            return;
        void** vt = *reinterpret_cast<void***>(ctrl);
        reinterpret_cast<RequestVoice_t>(vt[0x18 / 8])(ctrl, voice);   // MbDvcSoundControllerImpl, the call Update makes for a voice line
    }

    static std::uint8_t VoiceByName(const char* name)
    {
        void* ctrl = SoundController();
        if (!ctrl)
            return kNoVoice;
        void** vt = *reinterpret_cast<void***>(ctrl);
        return reinterpret_cast<GetVoiceTypeByName_t>(vt[0x50 / 8])(ctrl, name);
    }

    static std::uint32_t* LastAnnounceVoice()
    {
        if (!g_GetQuarkSystemTable)
            return nullptr;
        const std::uint8_t* table = g_GetQuarkSystemTable();
        const std::uint8_t* ui    = table ? *reinterpret_cast<std::uint8_t* const*>(table + 0x98) : nullptr;
        const std::uint8_t* hud   = ui ? *reinterpret_cast<std::uint8_t* const*>(ui + 0x40) : nullptr;
        void* owner = hud ? *reinterpret_cast<void* const*>(hud + 0x18) : nullptr;
        if (!owner)
            return nullptr;
        void** vt = *reinterpret_cast<void***>(owner);
        std::uint8_t* state = reinterpret_cast<AnnounceVoiceState_t>(vt[0x548 / 8])(owner);
        return state ? reinterpret_cast<std::uint32_t*>(state + 0x18) : nullptr;
    }

    // vanilla's rule for voices: the same sound twice in one burst plays once; the game clears it when no announce waits
    static bool PlayedThisBurst(std::uint32_t soundKey)
    {
        std::uint32_t* last = LastAnnounceVoice();
        if (!last)
            return false;
        if (*last == soundKey)
            return true;
        *last = soundKey;
        return false;
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

    struct AnnounceSound
    {
        std::uint8_t  voice   = kNoVoice;
        std::uint32_t uiSe    = 0;
        std::uint32_t eventId = 0;
    };

    static AnnounceSound ResolveSound(std::uint32_t se, const std::string& name)
    {
        AnnounceSound sound;
        if (name.empty())
        {
            if (se >= kVoiceSeBase)
                sound.voice = static_cast<std::uint8_t>(se - kVoiceSeBase);
            else
                sound.uiSe = se;
            return sound;
        }
        const std::uint8_t voice = VoiceByName(name.c_str());
        if (voice < kVoiceCount)
        {
            sound.voice = voice;
            return sound;
        }
        if (name.compare(0, 6, "VOICE_") == 0 && FirstWarning(name))
            LogWarn("SetAnnounceLogSE: no iDroid voice named %s, played as a Wwise event", name.c_str());
        sound.eventId = FoxHashes::FNVHash32(name);
        return sound;
    }

    static void PlaySound(std::uint8_t* act, const AnnounceSound& sound)
    {
        auto* seId         = reinterpret_cast<std::uint32_t*>(act + kTLA_SeId);
        void* soundControl = *reinterpret_cast<void**>(act + kTLA_SoundControl);
        if (sound.voice < kVoiceCount)
        {
            RequestVoice(sound.voice);
        }
        else if (sound.uiSe != 0)
        {
            if (*seId == 0)
                *seId = sound.uiSe;   // Update plays it right after us, as its own UI sound
            else if (g_StartUiSound && soundControl)
                g_StartUiSound(soundControl, static_cast<std::uint8_t>(sound.uiSe));
        }
        else if (sound.eventId != 0)
        {
            bool gameplaySfx = false;
            {
                std::lock_guard<std::mutex> lock(g_Mutex);
                gameplaySfx = g_GameplaySfx.find(sound.eventId) != g_GameplaySfx.end();
            }
            if (gameplaySfx)
                PostDaemonSfx(sound.eventId);
            else if (g_SoundControlStart && soundControl)
                g_SoundControlStart(soundControl, sound.eventId, 0);
        }
    }

    static void PlayBuiltInLine(std::uint8_t* act, const EntrySound& sound)
    {
        const BuiltInLine&  line        = *sound.builtIn;
        const bool          gameUiSound = sound.gameSe != 0 && sound.gameSe < kVoiceSeBase;
        const std::uint32_t soundKey    = line.condition ? (line.condition | kModSoundKeyBit) : line.voice;
        const bool          repeated    = PlayedThisBurst(soundKey);
        if (!repeated)
        {
            LogDebug("IdroidLines: announce %s -> line %u voice %u, extra sound %u (game's own %u)", line.label, line.condition,
                     static_cast<unsigned>(line.voice), line.extraSe, sound.gameSe);
            if (line.condition)
                PlayDialogue(ModDialogue{ line.condition, 0u, 0u });
            else
                RequestVoice(line.voice);
        }
        if (gameUiSound)
            PlaySound(act, AnnounceSound{ kNoVoice, sound.gameSe, 0u });   // the game plays a UI sound on every line, a voice and its partner once a burst
        else if (!repeated && line.extraSe)
            PlaySound(act, AnnounceSound{ kNoVoice, line.extraSe, 0u });
    }

    static void PlayModAnnounceSound(std::uint8_t* act)
    {
        auto* seId = reinterpret_cast<std::uint32_t*>(act + kTLA_SeId);
        if (*reinterpret_cast<const std::uint32_t*>(act + kTLA_TypedChars) != 0 || !IsModMarker(*seId))
            return;
        *seId = 0;

        const std::uint8_t* cdm = *reinterpret_cast<std::uint8_t**>(act + kTLA_Cdm);
        if (!cdm)
            return;

        const std::uint8_t offset = static_cast<std::uint8_t>(cdm[kCdm_AnnounceLogCursor] + act[kTLA_LogOffset]);
        int entry = static_cast<int>(cdm[kCdm_AnnounceLog + kLog_Head]) - offset;   // same entry AnnounceLog::GetSeId reads
        if (entry < 0)
            entry += kAnnounceLogSlots;
        if (static_cast<unsigned>(entry) >= kAnnounceLogSlots)
            entry = 0;

        EntrySound         sound;
        ModAnnouncePayload p;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            sound = g_EntrySound[entry];
            if (!sound.builtIn)
            {
                const auto it = sound.type ? g_Payloads.find(sound.type) : g_Payloads.end();
                if (it == g_Payloads.end())
                    return;
                p = it->second;
            }
        }
        if (sound.builtIn)
        {
            PlayBuiltInLine(act, sound);
            return;
        }
        if (MissionCodeGuard::ShouldBypassHooks())
            return;
        const std::uint32_t type = sound.type;

        const bool          dialogue = p.dialogue.condition != 0;
        const AnnounceSound main     = dialogue ? AnnounceSound{} : ResolveSound(p.se, p.name);
        if (!p.hasExtra && main.voice < kVoiceCount)
        {
            *seId = kVoiceSeBase + main.voice;   // vanilla plays it: the voice, its partner sound, no repeat
            return;
        }

        AnnounceSound extra = p.hasExtra ? ResolveSound(p.extraSe, p.extraName) : AnnounceSound{};
        if (main.voice < kVoiceCount && extra.voice < kVoiceCount)
        {
            if (FirstWarning("two voices " + std::to_string(type)))
                LogWarn("SetAnnounceLogSE: only one of the two sounds can be an iDroid voice, the extra one is not played");
            extra = AnnounceSound{};
        }

        // a voice keys the line the way vanilla does, so the pair repeats with that voice; UI sounds alone play every time
        const std::uint8_t  voice    = main.voice < kVoiceCount ? main.voice : extra.voice;
        const std::uint32_t mainKey  = dialogue ? (p.dialogue.condition ^ p.dialogue.chara ^ p.dialogue.dlgEvent)
                                                : (main.eventId ? main.eventId : main.uiSe);
        const std::uint32_t extraKey = extra.eventId ? extra.eventId : extra.uiSe;
        const bool burstRule = voice < kVoiceCount || dialogue || main.eventId != 0 || extra.eventId != 0;
        const std::uint32_t soundKey = voice < kVoiceCount ? voice : ((mainKey + 31u * extraKey) | kModSoundKeyBit);
        if (burstRule && PlayedThisBurst(soundKey))
            return;

        if (dialogue)
        {
            if (!PlayDialogue(p.dialogue)
                && FirstWarning("dialogue " + std::to_string(p.dialogue.condition)))
                LogWarn("SetAnnounceLogSE: dialogue 0x%08X (chara 0x%08X, event 0x%08X) did not play",
                    p.dialogue.condition, p.dialogue.chara, p.dialogue.dlgEvent);
        }
        else
        {
            PlaySound(act, main);
        }
        PlaySound(act, extra);
    }

    static TypingActState& TypingStateFor(std::uint8_t* act)
    {
        TypingActState* empty = nullptr;
        for (TypingActState& state : g_TypingActs)
        {
            if (state.act == act)
                return state;
            if (!state.act && !empty)
                empty = &state;
        }
        TypingActState& state = empty ? *empty
            : g_TypingActs[(reinterpret_cast<std::uintptr_t>(act) >> 4) % kTypingActSlots];
        const float interval = *reinterpret_cast<const float*>(act + kTLA_CharInterval);
        const float hold     = *reinterpret_cast<const float*>(act + kTLA_HoldTime);
        state = TypingActState{ act, interval, interval, hold, hold, 0.0f };
        return state;
    }

    static void ApplyTypingOverrides(std::uint8_t* act)
    {
        const float speed = g_TypingSpeed.load(std::memory_order_relaxed);
        const float hold  = g_HoldTime.load(std::memory_order_relaxed);
        auto* interval = reinterpret_cast<float*>(act + kTLA_CharInterval);
        auto* holdTime = reinterpret_cast<float*>(act + kTLA_HoldTime);

        TypingActState& state = TypingStateFor(act);
        if (*interval != state.writtenInterval)   // the act started a new line and set the game's values again
            state.baseInterval = *interval;
        if (*holdTime != state.writtenHold)
            state.baseHold = *holdTime;
        state.writtenInterval = speed < 1.0f ? state.baseInterval / speed : state.baseInterval;
        state.writtenHold     = hold < 0.0f ? state.baseHold : state.baseHold + hold - kVanillaHoldTime;
        if (state.writtenHold < 0.0f)
            state.writtenHold = 0.0f;
        *interval = state.writtenInterval;
        *holdTime = state.writtenHold;

        // ActTyping types at most one character a frame, and vanilla already does that every frame: faster means extra ones
        const bool typing   = act[kTLA_TypingDone] == 0;
        const bool stepping = *interval < *reinterpret_cast<const float*>(act + kTLA_TypeTimer);
        if (speed <= 1.0f || !typing || !stepping)
            return;
        state.extraChars += speed - 1.0f;
        const int extra = static_cast<int>(state.extraChars);
        state.extraChars -= static_cast<float>(extra);
        *reinterpret_cast<int*>(act + kTLA_TypedChars) += extra;
    }

    static void __fastcall hkTypingLogActUpdate(void* self)
    {
        DrainDeferredAnnounces();
        if (self)
        {
            PlayModAnnounceSound(static_cast<std::uint8_t*>(self));
            ApplyTypingOverrides(static_cast<std::uint8_t*>(self));
        }
        g_OrigUpdate(self);
    }

    static HookEntry UpdateEntry(const void* fn)
    {
        std::uint8_t mask[sizeof(kUpdateEntry)];
        std::memset(mask, 1, sizeof(mask));
        std::memset(mask + kUpdateCallRel, 0, 4);
        std::memset(mask + kUpdateJumpRel, 0, 4);
        return CheckHookEntryMasked(fn, kUpdateEntry, mask, sizeof(kUpdateEntry));
    }

    static GetQuarkSystemTable_t FindGetQuarkSystemTable(const std::uint8_t* update)
    {
        constexpr std::size_t afterCall = kLastVoiceCallRel + 4;
        std::uint8_t live[sizeof(kUpdateLastVoice)] = {};
        if (!ReadEntryBytesSEH(update + kUpdateLastVoiceAt, live, sizeof(live))
            || std::memcmp(live, kUpdateLastVoice, kLastVoiceCallRel) != 0
            || std::memcmp(live + afterCall, kUpdateLastVoice + afterCall, sizeof(live) - afterCall) != 0)
            return nullptr;
        std::int32_t rel = 0;
        std::memcpy(&rel, live + kLastVoiceCallRel, sizeof(rel));
        return reinterpret_cast<GetQuarkSystemTable_t>(update + kUpdateLastVoiceAt + afterCall + rel);
    }

    static void ScrubModMarkers()
    {
        void* cdm = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            cdm = g_AnnounceCdm;
        }
        if (!cdm || g_LogLayoutBad)
            return;
        std::uint8_t* log = AnnounceLog(cdm);
        for (unsigned i = 0; i < kAnnounceLogSlots; ++i)
        {
            std::uint8_t& se = log[kLog_Entries + i * kLog_EntrySize + kEntry_SeId];
            if (IsModMarker(se))
                se = 0;
        }
    }
}

std::uint32_t Set_AnnounceLogSE(const char* announceLabel, const AnnounceLogSound& sound)
{
    const std::uint32_t announceType = LabelToAnnounceType(announceLabel);
    if (announceType == 0)
        return 0;

    const bool byName   = sound.condition == 0 && sound.name;
    const bool byNumber = sound.condition == 0 && !sound.name;
    if (byName && !*sound.name)
    {
        LogWarn("SetAnnounceLogSE: %s: empty sound name", announceLabel);
        return 0;
    }
    if (byNumber && sound.se >= kModMarkerFirst)
    {
        LogWarn("SetAnnounceLogSE: %s: %u is not a sound (1-134 UI sound, 135-251 iDroid voice)",
            announceLabel, sound.se);
        return 0;
    }
    if (sound.hasExtra && sound.extraName && !*sound.extraName)
    {
        LogWarn("SetAnnounceLogSE: %s: empty extra sound name", announceLabel);
        return 0;
    }
    if (sound.hasExtra && !sound.extraName && sound.extraSe >= kModMarkerFirst)
    {
        LogWarn("SetAnnounceLogSE: %s: extra %u is not a sound (0 none, 1-134 UI sound, 135-251 iDroid voice)",
            announceLabel, sound.extraSe);
        return 0;
    }
    if (sound.hasExtra && byNumber && !sound.extraName && sound.se >= kVoiceSeBase && sound.extraSe >= kVoiceSeBase)
    {
        LogWarn("SetAnnounceLogSE: %s: only one of the two sounds can be an iDroid voice", announceLabel);
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_Mutex);
    if (byNumber && !sound.hasExtra)
    {
        g_Overrides[announceType] = sound.se;
        g_Payloads.erase(announceType);
        return announceType;
    }
    g_Overrides.erase(announceType);
    ModAnnouncePayload& p = g_Payloads[announceType];
    p = ModAnnouncePayload{};
    if (sound.condition != 0)
        p.dialogue = ModDialogue{ sound.condition, sound.chara, sound.dialogueEvent };
    else if (byName)
        p.name = sound.name;
    else
        p.se = sound.se;
    p.hasExtra = sound.hasExtra;
    p.extraSe  = sound.extraSe;
    if (sound.extraName)
        p.extraName = sound.extraName;
    return announceType;
}

bool Register_AnnounceLogSfx(const char* eventName)
{
    if (!eventName || !*eventName)
    {
        LogWarn("RegisterAnnounceLogSfx: needs a Wwise event name");
        return false;
    }
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_GameplaySfx.insert(FoxHashes::FNVHash32(eventName));
    return true;
}

bool Unset_AnnounceLogSE(const char* announceLabel)
{
    const std::uint32_t announceType = LabelToAnnounceType(announceLabel);
    if (announceType == 0)
        return false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    const bool hadOverride = g_Overrides.erase(announceType) != 0;
    const bool hadPayload  = g_Payloads.erase(announceType) != 0;
    return hadOverride || hadPayload;
}

bool Unregister_AnnounceLogSfx(const char* eventName)
{
    if (!eventName || !*eventName)
        return false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    return g_GameplaySfx.erase(FoxHashes::FNVHash32(eventName)) != 0;
}

bool Set_AnnounceLogTypingSpeed(float speed)
{
    if (!(speed > 0.0f && speed <= 1000.0f))
    {
        LogWarn("SetAnnounceLogTypingSpeed: %.3f is not a speed (above 0, at most 1000; 1 = the game's own)", speed);
        return false;
    }
    g_TypingSpeed.store(speed, std::memory_order_relaxed);
    return g_UpdateInstalled;
}

bool Set_AnnounceLogHoldTime(float seconds)
{
    if (!(seconds >= 0.0f && seconds <= 3600.0f))
    {
        LogWarn("SetAnnounceLogHoldTime: %.3f is not a time in seconds (0 to 3600; the game's own is 1.5)", seconds);
        return false;
    }
    g_HoldTime.store(seconds, std::memory_order_relaxed);
    return g_UpdateInstalled;
}

void Reset_AnnounceLogTyping()
{
    g_TypingSpeed.store(1.0f, std::memory_order_relaxed);
    g_HoldTime.store(-1.0f, std::memory_order_relaxed);
}

bool Set_AnnounceLogPriority(const char* announceLabel, AnnounceLogPriority priority)
{
    const std::uint32_t announceType = LabelToAnnounceType(announceLabel);
    if (announceType == 0)
        return false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    if (priority == AnnounceLogPriority::Default)
        g_Priorities.erase(announceType);
    else if (priority == AnnounceLogPriority::High)
        g_Priorities[announceType] = kTypeJumpsAhead;
    else if (priority == AnnounceLogPriority::Low)
        g_Priorities[announceType] = kTypeWaitsBehind;
    else
        g_Priorities[announceType] = 0;
    return g_LangTypeTarget != nullptr;
}

bool Install_AnnounceLogHook()
{
    if (g_Installed)
        return true;

    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    g_AnswerWindow = frequency.QuadPart / 200;   // 5 ms

    if (!gAddr.Hud_GetAnnounceLogSE)
    {
        LogError("AnnounceLog: no address for this build, custom announce SE off");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.Hud_GetAnnounceLogSE);
    if (!target)
    {
        LogError("AnnounceLog: address did not resolve, custom announce SE off");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkGetAnnounceLogSE),
        reinterpret_cast<void**>(&g_Orig));

    if (ok)
        g_Installed = true;
    else
        LogError("AnnounceLog: GetAnnounceLogSE hook failed @ %p", target);

    if (gAddr.HudCommonDataManager_AnnounceLogView)
    {
        void* viewTarget = ResolveGameAddress(gAddr.HudCommonDataManager_AnnounceLogView);
        const bool viewOk = viewTarget && CreateAndEnableHook(
            viewTarget,
            reinterpret_cast<void*>(&hkAnnounceLogViewDiag),
            reinterpret_cast<void**>(&g_OrigAnnounceLogViewDiag));
        if (viewOk)
            g_ViewTarget = viewTarget;
        else
            LogError("AnnounceLog: AnnounceLogView hook failed @ %p, overflow not deferred",
                viewTarget);
    }
    else
    {
        LogError("AnnounceLog: no AnnounceLogView address for this build, overflow not deferred");
    }

    if (ok && g_ViewTarget && gAddr.Hud_TypingLogActUpdate)
    {
        void* upd = ResolveGameAddress(gAddr.Hud_TypingLogActUpdate);
        const HookEntry entry = upd ? UpdateEntry(upd) : HookEntry::Unknown;
        if (entry == HookEntry::Unknown)
        {
            LogError("AnnounceLog: TypingLogAct::Update is not the code we know, custom announce sounds off");
        }
        else
        {
            if (entry == HookEntry::ForeignHook)
                LogHookedOnTop("AnnounceLog: TypingLogAct::Update", upd);
            if (CreateAndEnableHook(
                    upd,
                    reinterpret_cast<void*>(&hkTypingLogActUpdate),
                    reinterpret_cast<void**>(&g_OrigUpdate)))
            {
                if (gAddr.Ui_SoundControlStart)
                    g_SoundControlStart = reinterpret_cast<SoundControlStart_t>(
                        ResolveGameAddress(gAddr.Ui_SoundControlStart));
                if (gAddr.Uix_UiUtility_StartSound)
                    g_StartUiSound = reinterpret_cast<StartUiSound_t>(
                        ResolveGameAddress(gAddr.Uix_UiUtility_StartSound));
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
                g_GetQuarkSystemTable = FindGetQuarkSystemTable(static_cast<const std::uint8_t*>(upd));
                if (!g_GetQuarkSystemTable)
                    LogError("AnnounceLog: TypingLogAct::Update voice check is not the code we know, custom announce sounds repeat on every line");
                g_UpdateInstalled = true;
            }
            else
            {
                LogError("AnnounceLog: TypingLogAct::Update hook failed, custom announce sounds off");
            }
        }
    }
    else if (ok)
    {
        LogError("AnnounceLog: no TypingLogAct::Update or AnnounceLogView hook for this build, custom announce sounds off");
    }

    for (BuiltInLine& line : g_BuiltInLines)
        line.type = LabelToAnnounceType(line.label);

    if (g_UpdateInstalled && gAddr.Hud_GetAnnounceLogType)
    {
        void* typeTarget = ResolveGameAddress(gAddr.Hud_GetAnnounceLogType);
        const HookEntry entry = CheckHookEntryMasked(typeTarget, kGetAnnounceLogTypeEntry, kGetAnnounceLogTypeMask,
                                                     sizeof(kGetAnnounceLogTypeEntry));
        if (entry == HookEntry::Unknown)
        {
            LogError("AnnounceLog: the announce type getter is not the code we know, announces that never ask for a sound stay as they are");
        }
        else
        {
            if (entry == HookEntry::ForeignHook)
                LogHookedOnTop("AnnounceLog: announce type getter", typeTarget);
            if (CreateAndEnableHook(typeTarget, reinterpret_cast<void*>(&hkGetAnnounceLogType),
                                    reinterpret_cast<void**>(&g_OrigGetAnnounceLogType)))
                g_TypeTarget = typeTarget;
            else
                LogError("AnnounceLog: announce type getter hook failed, announces that never ask for a sound stay as they are");
        }
    }

    void* langTypeTarget = gAddr.Hud_GetLangAnnounceType ? ResolveGameAddress(gAddr.Hud_GetLangAnnounceType) : nullptr;
    const HookEntry langTypeEntry = CheckHookEntry(langTypeTarget, kGetLangAnnounceTypeEntry, sizeof(kGetLangAnnounceTypeEntry));
    if (langTypeEntry == HookEntry::Unknown)
    {
        LogError("AnnounceLog: GetLangAnnounceType is not the code we know, SetAnnounceLogPriority off");
    }
    else
    {
        if (langTypeEntry == HookEntry::ForeignHook)
            LogHookedOnTop("AnnounceLog: GetLangAnnounceType", langTypeTarget);
        if (CreateAndEnableHook(langTypeTarget, reinterpret_cast<void*>(&hkGetLangAnnounceType),
                                reinterpret_cast<void**>(&g_OrigGetLangAnnounceType)))
            g_LangTypeTarget = langTypeTarget;
        else
            LogError("AnnounceLog: GetLangAnnounceType hook failed, SetAnnounceLogPriority off");
    }

    return ok;
}

bool Uninstall_AnnounceLogHook()
{
    g_UpdateInstalled = false;
    ScrubModMarkers();
    Reset_AnnounceLogTyping();

    if (g_LangTypeTarget)
    {
        DisableAndRemoveHook(g_LangTypeTarget);
        g_LangTypeTarget = nullptr;
        g_OrigGetLangAnnounceType = nullptr;
    }

    if (g_TypeTarget)
    {
        DisableAndRemoveHook(g_TypeTarget);
        g_TypeTarget = nullptr;
        g_OrigGetAnnounceLogType = nullptr;
    }

    if (g_ViewTarget)
    {
        DisableAndRemoveHook(g_ViewTarget);
        g_ViewTarget = nullptr;
        g_OrigAnnounceLogViewDiag = nullptr;
    }

    if (g_Installed)
    {
        if (gAddr.Hud_GetAnnounceLogSE)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.Hud_GetAnnounceLogSE));
        if (g_OrigUpdate && gAddr.Hud_TypingLogActUpdate)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.Hud_TypingLogActUpdate));
    }

    g_Orig              = nullptr;
    g_Installed         = false;
    g_OrigUpdate        = nullptr;
    g_SoundControlStart = nullptr;
    g_StartUiSound      = nullptr;
    g_GetInstance       = nullptr;
    g_GetGraphState     = nullptr;
    g_VoicePlay         = nullptr;
    g_GetQuarkSystemTable = nullptr;
    g_SdInstancePtr     = nullptr;
    g_SdEnqueue         = nullptr;
    g_kW_SelectReady    = false;
    g_LogLayoutBad      = false;

    std::lock_guard<std::mutex> lock(g_Mutex);
    g_AnnounceQueue.clear();
    g_AnnounceCdm = nullptr;
    g_Overrides.clear();
    g_Payloads.clear();
    g_GameplaySfx.clear();
    g_Priorities.clear();
    g_Warned.clear();
    g_NextMarker = 0;
    for (EntrySound& sound : g_MarkerSound)
        sound = {};
    for (EntrySound& sound : g_EntrySound)
        sound = {};
    return true;
}
