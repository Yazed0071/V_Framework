#include <pch.h>
#include "SpeechSystemImpl_FindSpeechSequenceIndex.h"

extern "C" {
    #include "lua.h"
}

#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>

#include "AddressSet.h"
#include "FeatureSwitch.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "../../lua/LuaApi.h"

namespace
{
    using FindSpeechSequenceIndex_t = std::uint16_t(__fastcall*)(void* speechSystem, std::uint32_t label);
    using ConvertSpeechLabelToConversationType_t = std::uint8_t(__fastcall*)(std::uint32_t label);
    using LockSection_t = void(__fastcall*)(void* section);

    static FindSpeechSequenceIndex_t g_OrigFindSpeechSequenceIndex = nullptr;

    struct SpeechFileTableEntry
    {
        std::uint8_t* sequence;
        std::uint32_t label;
        std::uint32_t unused;
    };
    static_assert(sizeof(SpeechFileTableEntry) == 0x10, "speech file table entries are 0x10 bytes");

    constexpr std::uint16_t kSpeechFileTableSize = 0x400;
    constexpr std::uint16_t kNoSequence = 0xFFFF;

    constexpr std::size_t kSequenceHeaderSize = 0xC; // label, voice event, part count
    constexpr std::size_t kPartSize = 0x14;          // speaker, voice param, action, facial, interval to the next part
    constexpr std::uint32_t kMaxConversationParts = 64;

    constexpr std::uint8_t kFirstSovietConversation = 0x14;
    constexpr std::uint8_t kLastSovietConversation = 0x49;
    constexpr std::uint8_t kFirstPfConversation = 0x4A;
    constexpr std::uint8_t kLastPfConversation = 0x7A;

    constexpr int kWeatherRainy = 2;
    constexpr int kWeatherSandstorm = 3;

    constexpr std::uint32_t kSpeakerVoiceA = 0xDAF48A2C; // ene_a
    constexpr std::uint32_t kSpeakerVoiceB = 0x77C2A183; // ene_b
    constexpr std::uint32_t kSpeakerVoiceC = 0x43D9BA88; // ene_c
    constexpr std::uint32_t kSpeakerVoiceD = 0x61AC66D8; // ene_d

    constexpr std::uint32_t kActionNone = 0;
    constexpr std::uint32_t kActionGreeting = 0xED64CC14;      // Greeting
    constexpr std::uint32_t kActionAskQuestion = 0x3CA24194;   // AskQuestion
    constexpr std::uint32_t kActionConversation1 = 0xB48CCA3F; // Conversation1
    constexpr std::uint32_t kActionConversation2 = 0x8C89E3FE; // Conversation2
    constexpr std::uint32_t kActionScratchHead = 0xD8862169;   // ScratchHead

    constexpr std::uint32_t kSorryImLate[4]        = { 0x6B0D8733, 0x6B0D8730, 0x6B0D8731, 0x6B0D8736 }; // DD_vox_ene line keys for voices A, B, C, D, here and below (real names unknown)
    constexpr std::uint32_t kWhatTookYouSoLong[4]  = { 0x83E71BA4, 0x83E71BA7, 0x83E71BA6, 0x83E71BA1 };
    constexpr std::uint32_t kBeenWaitingLong[4]    = { 0x9D358C15, 0x9D358C16, 0x9D358C17, 0x9D358C10 };
    constexpr std::uint32_t kItsAllRight[4]        = { 0x18D6083E, 0x18D6083D, 0x18D6083C, 0x18D6083B };
    constexpr std::uint32_t kAnythingUnusual[4]    = { 0x5F7AA28F, 0x5F7AA28C, 0x5F7AA28D, 0x5F7AA28A };
    constexpr std::uint32_t kTotallyQuietPf[4]     = { 0xCAFED680, 0xCAFED683, 0xCAFED683, 0xCAFED685 }; // the PF voice C take sits under voice B's key
    constexpr std::uint32_t kTotallyQuietSoviet[4] = { 0xCAFED680, 0xCAFED683, 0xCAFED682, 0xCAFED685 };
    constexpr std::uint32_t kAnyAction[4]          = { 0x458B12D1, 0x458B12D2, 0x458B12D3, 0x458B12D4 };
    constexpr std::uint32_t kNothingToSpeakOf[4]   = { 0x46A10E1A, 0x46A10E19, 0x46A10E18, 0x46A10E1F };
    constexpr std::uint32_t kWeatherComplaint[4]   = { 0x27D05BEB, 0x27D05BE8, 0x27D05BE9, 0x27D05BEE }; // PF voices say the rain line, Soviet voices the sandstorm line
    constexpr std::uint32_t kWeatherAgreement[4]   = { 0x1C25D301, 0x1C25D302, 0x1C25D303, 0x1C25D304 };
    constexpr std::uint32_t kCancelForRain[4]      = { 0xE7ECC470, 0xE7ECC473, 0xE7ECC472, 0xE7ECC475 };
    constexpr std::uint32_t kYeahRight[4]          = { 0x15B60EFF, 0x15B60EFC, 0x15B60EFD, 0x15B60EFA };
    constexpr std::uint32_t kTheseSqualls[4]       = { 0x7C1722EE, 0x7C1722ED, 0x7C1722EC, 0x7C1722EB };
    constexpr std::uint32_t kComingDownSoHard[4]   = { 0x0D1DCAC5, 0x0D1DCAC6, 0x0D1DCAC7, 0x0D1DCAC0 };

    enum class Faction { None, Pf, Soviet };

    struct OpenerLine
    {
        const std::uint32_t* keyByVoice;
        bool saidByFirstSpeaker;
        std::uint32_t action;
    };

    struct Opener
    {
        const OpenerLine* lines;
        std::uint32_t lineCount;
    };

    template <std::size_t Count>
    constexpr Opener MakeOpener(const OpenerLine (&lines)[Count])
    {
        return { lines, static_cast<std::uint32_t>(Count) };
    }

    constexpr OpenerLine kLateGreeting[] = {
        { kSorryImLate, true, kActionGreeting },
        { kWhatTookYouSoLong, false, kActionNone },
    };
    constexpr OpenerLine kWaitingGreeting[] = {
        { kBeenWaitingLong, true, kActionGreeting },
        { kItsAllRight, false, kActionNone },
    };
    constexpr OpenerLine kUnusualGreetingPf[] = {
        { kAnythingUnusual, true, kActionAskQuestion },
        { kTotallyQuietPf, false, kActionNone },
    };
    constexpr OpenerLine kUnusualGreetingSoviet[] = {
        { kAnythingUnusual, true, kActionAskQuestion },
        { kTotallyQuietSoviet, false, kActionNone },
    };
    constexpr OpenerLine kActionGreetingLines[] = {
        { kAnyAction, true, kActionAskQuestion },
        { kNothingToSpeakOf, false, kActionNone },
    };
    constexpr OpenerLine kRainTalk[] = {
        { kWeatherComplaint, true, kActionConversation1 },
        { kWeatherAgreement, false, kActionNone },
        { kCancelForRain, true, kActionConversation2 },
        { kYeahRight, false, kActionNone },
    };
    constexpr OpenerLine kSquallTalk[] = {
        { kTheseSqualls, true, kActionConversation1 },
        { kComingDownSoHard, false, kActionNone },
    };
    constexpr OpenerLine kSandstormTalk[] = {
        { kWeatherComplaint, true, kActionConversation1 },
        { kWeatherAgreement, false, kActionNone },
    };

    constexpr Opener kPfGreetings[] = {
        MakeOpener(kLateGreeting), MakeOpener(kWaitingGreeting), MakeOpener(kUnusualGreetingPf), MakeOpener(kActionGreetingLines),
    };
    constexpr Opener kSovietGreetings[] = {
        MakeOpener(kLateGreeting), MakeOpener(kWaitingGreeting), MakeOpener(kUnusualGreetingSoviet), MakeOpener(kActionGreetingLines),
    };
    constexpr Opener kRainOpeners[] = { MakeOpener(kRainTalk), MakeOpener(kSquallTalk) };
    constexpr Opener kSandstormOpener = MakeOpener(kSandstormTalk);
    constexpr Opener kNoOpener = { nullptr, 0 };   // conversations switched off: the part fixes still apply

    constexpr std::uint32_t kMbStory08 = 0x503B66D6; // MB_story_08
    constexpr std::uint32_t kMbStory08PartCount = 9;
    constexpr std::uint32_t kMbStory08CutLinesAt = 8;
    constexpr std::uint32_t kMassacredThemHuh = 0xF2257290; // DD_vox_ene line key of "So they moved on the Caribbean base and massacred them. Huh..." (real name unknown)
    constexpr std::uint32_t kIHearYou = 0x9DD9B255;         // DD_vox_ene line key of "I hear you..." (real name unknown)

    struct RestoredLine
    {
        std::uint32_t speaker;
        std::uint32_t voiceParam;
        std::uint32_t action;
    };

    constexpr RestoredLine kMbStory08CutLines[] = {
        { kSpeakerVoiceC, 0xF2257291, kActionConversation1 }, // DD_vox_ene line key of "See thing is... Commander Miller says this is about revenge, ..." (real name unknown)
        { kSpeakerVoiceA, 0xF325740B, kActionAskQuestion },   // "Meaning, how many of us here actually lost someone from then?"
        { kSpeakerVoiceA, 0xF325740A, kActionConversation2 }, // "I say, let the commander and the boss get their revenge."
        { kSpeakerVoiceC, 0xF3257409, kActionNone },          // "We have our own job to do."
    };
    constexpr std::uint32_t kMbStory08CutLineCount = sizeof(kMbStory08CutLines) / sizeof(kMbStory08CutLines[0]);

    constexpr std::uint32_t kUssrStory18   = 0x3A81B82C; // USSR_story_18
    constexpr std::uint32_t kUssrRevenge06 = 0x5B30FA92; // USSR_revenge_06
    constexpr std::uint32_t kPfRevenge03   = 0x40020B29; // PF_revenge_03
    constexpr std::uint32_t kPfCounter09   = 0xF6028098; // PF_counter_09
    constexpr std::uint32_t kUssrCounter03 = 0x35EEA85E; // USSR_counter_03
    constexpr std::uint32_t kPfRevenge09   = 0x80A69749; // PF_revenge_09

    struct PartFix
    {
        std::uint32_t conversation;
        std::uint32_t voiceParam;
        std::uint32_t speaker;       // 0 keeps the speaker
        std::uint32_t newVoiceParam; // 0 keeps the line
    };

    constexpr PartFix kPartFixes[] = {
        { kUssrStory18,   0x072C6CDA, 0, 0x072C6CD5 },       // the last line names a key with no audio, "Yeah, right." is the recorded take
        { kPfRevenge03,   0x5D22A092, kSpeakerVoiceC, 0 },   // "All right." is only recorded by voice C
        { kPfCounter09,   0x9B74991D, kSpeakerVoiceD, 0 },   // "OK." is only recorded by voice D for PF soldiers
        { kUssrRevenge06, 0x4E657FF2, kSpeakerVoiceA, 0 },   // the Soviet takes are voices A and B, the sequence asks for C and D
        { kUssrRevenge06, 0x4E657FF1, kSpeakerVoiceA, 0 },
        { kUssrRevenge06, 0x4E657FF0, kSpeakerVoiceB, 0 },
        { kUssrRevenge06, 0x4E657FF7, kSpeakerVoiceA, 0 },
        { kUssrRevenge06, 0x4E657FF6, kSpeakerVoiceA, 0 },
        { kUssrRevenge06, 0x4E657FF5, kSpeakerVoiceA, 0 },
        { kUssrRevenge06, 0x4E657FF4, kSpeakerVoiceB, 0 },
        { kUssrRevenge06, 0x4E657FFB, kSpeakerVoiceA, 0 },
        { kUssrRevenge06, 0x4E657FFA, kSpeakerVoiceB, 0 },
    };

    struct MissingPart
    {
        std::uint32_t conversation;
        std::uint32_t afterVoiceParam;
        RestoredLine line;
    };

    constexpr MissingPart kMissingParts[] = {
        { kUssrCounter03, 0x29267CD0, { kSpeakerVoiceD, 0x29267CD1, kActionScratchHead } }, // "I guess that's to be expected." is in the PF copy only, the Soviet take is voice D's
        { kPfRevenge09,   0x205539EB, { kSpeakerVoiceD, 0x205539EA, kActionNone } },        // the closing "Yeah." is in the Soviet copy only, the PF take is voice D's
    };
    constexpr std::uint32_t kMissingPartCount = sizeof(kMissingParts) / sizeof(kMissingParts[0]);

    constexpr std::int32_t kSpecialSoldierLevel     = 5; // STEALTH_5 and COMBAT_5 bring the SPECIAL soldier rank
    constexpr std::int32_t kMoreReinforcementsLevel = 4; // COMBAT_4 raises REINFORCE_COUNT to 2

    constexpr std::uint32_t kMaxOpenerLines = 4;
    constexpr std::size_t kConversationSequenceSize = kSequenceHeaderSize + (kMaxOpenerLines + kMaxConversationParts + kMissingPartCount) * kPartSize;

    constexpr std::size_t kConversationSlotCount = 16;
    constexpr ULONGLONG kConversationSlotReuseMs = 5 * 60 * 1000;

    struct ConversationSlot
    {
        std::uint16_t tableIndex = kNoSequence;
        std::uint32_t tableLabel = 0;
        ULONGLONG lastUsedMs = 0;
        bool everUsed = false;
        alignas(8) std::uint8_t sequence[kConversationSequenceSize] = {};
    };

    static std::mutex   g_SlotMutex;
    static ConversationSlot g_Slots[kConversationSlotCount];
    static std::mt19937 g_Random{ static_cast<std::uint32_t>(GetTickCount64()) };
    static bool         g_WarnedSlotsBusy = false;
    static bool         g_WarnedTableFull = false;

    template <typename T>
    T ReadAt(const std::uint8_t* base, std::size_t offset)
    {
        T value;
        std::memcpy(&value, base + offset, sizeof(T));
        return value;
    }

    template <typename T>
    void WriteAt(std::uint8_t* base, std::size_t offset, T value)
    {
        std::memcpy(base + offset, &value, sizeof(T));
    }

    Faction FactionOfConversation(std::uint32_t label)
    {
        const auto convert = reinterpret_cast<ConvertSpeechLabelToConversationType_t>(
            ResolveGameAddress(gAddr.SoldierConversationService_ConvertSpeechLabelToConversationType));
        if (!convert)
            return Faction::None;

        const std::uint8_t type = convert(label);
        if (type >= kFirstPfConversation && type <= kLastPfConversation)
            return Faction::Pf;
        if (type >= kFirstSovietConversation && type <= kLastSovietConversation)
            return Faction::Soviet;
        return Faction::None;
    }

    int CurrentWeather()
    {
        __try
        {
            const auto weatherManager = *reinterpret_cast<const std::uint8_t* const*>(ResolveGameAddress(gAddr.TppWeatherManager_s_Instance));
            if (!weatherManager)
                return -1;
            const auto weatherDesc = *reinterpret_cast<const std::uint8_t* const*>(weatherManager + 0x80); // the weather in effect right now
            if (!weatherDesc)
                return -1;
            return *reinterpret_cast<const int*>(weatherDesc + 0x10); // its weather type
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    int VoiceOfSpeaker(std::uint32_t speaker)
    {
        if (speaker == kSpeakerVoiceA) return 0;
        if (speaker == kSpeakerVoiceB) return 1;
        if (speaker == kSpeakerVoiceC) return 2;
        if (speaker == kSpeakerVoiceD) return 3;
        return -1;
    }

    const Opener& PickOpener(Faction faction)
    {
        const int weather = CurrentWeather();
        if (faction == Faction::Pf && weather == kWeatherRainy)
            return kRainOpeners[std::uniform_int_distribution<int>(0, 1)(g_Random)];
        if (faction == Faction::Soviet && weather == kWeatherSandstorm)
            return kSandstormOpener;

        const int greeting = std::uniform_int_distribution<int>(0, 3)(g_Random);
        return faction == Faction::Pf ? kPfGreetings[greeting] : kSovietGreetings[greeting];
    }

    ConversationSlot* FindFreeSlot(ULONGLONG now)
    {
        ConversationSlot* oldest = nullptr;
        for (ConversationSlot& slot : g_Slots)
        {
            if (!slot.everUsed)
                return &slot;
            if (now - slot.lastUsedMs >= kConversationSlotReuseMs && (!oldest || slot.lastUsedMs < oldest->lastUsedMs))
                oldest = &slot;
        }
        return oldest;
    }

    void FixBrokenParts(std::uint32_t label, std::uint8_t* parts, std::uint32_t partCount)
    {
        for (std::uint32_t i = 0; i < partCount; ++i)
        {
            std::uint8_t* part = parts + i * kPartSize;
            const std::uint32_t voiceParam = ReadAt<std::uint32_t>(part, 0x4);
            for (const PartFix& fix : kPartFixes)
            {
                if (fix.conversation != label || fix.voiceParam != voiceParam)
                    continue;
                if (fix.speaker)
                    WriteAt<std::uint32_t>(part, 0x0, fix.speaker);
                if (fix.newVoiceParam)
                    WriteAt<std::uint32_t>(part, 0x4, fix.newVoiceParam);
            }
        }
    }

    std::uint32_t AddMissingParts(std::uint32_t label, std::uint8_t* parts, std::uint32_t partCount)
    {
        for (const MissingPart& missing : kMissingParts)
        {
            if (missing.conversation != label)
                continue;
            for (std::uint32_t i = 0; i < partCount; ++i)
            {
                std::uint8_t* part = parts + i * kPartSize;
                if (ReadAt<std::uint32_t>(part, 0x4) != missing.afterVoiceParam)
                    continue;
                std::uint8_t* added = part + kPartSize;
                if (i + 1 < partCount && ReadAt<std::uint32_t>(added, 0x4) == missing.line.voiceParam)
                    break; // a speech file mod already has it
                std::memmove(added + kPartSize, added, (partCount - i - 1) * kPartSize);
                std::memset(added, 0, kPartSize);
                WriteAt<std::uint32_t>(added, 0x0, missing.line.speaker);
                WriteAt<std::uint32_t>(added, 0x4, missing.line.voiceParam);
                WriteAt<std::uint32_t>(added, 0x8, missing.line.action);
                ++partCount;
                break;
            }
        }
        return partCount;
    }

    bool BuildGreetingSequence(ConversationSlot& slot, std::uint32_t label, const std::uint8_t* conversation, const Opener& opener)
    {
        const std::uint32_t vanillaPartCount = ReadAt<std::uint32_t>(conversation, 0x8);
        if (vanillaPartCount == 0 || vanillaPartCount > kMaxConversationParts)
            return false;

        std::uint8_t* out = slot.sequence;
        std::uint8_t* parts = out + kSequenceHeaderSize + opener.lineCount * kPartSize;
        std::memcpy(parts, conversation + kSequenceHeaderSize, vanillaPartCount * kPartSize);
        FixBrokenParts(label, parts, vanillaPartCount);
        const std::uint32_t partCount = AddMissingParts(label, parts, vanillaPartCount);

        const std::uint32_t firstSpeaker = ReadAt<std::uint32_t>(parts, 0x0);
        std::uint32_t secondSpeaker = 0;
        for (std::uint32_t i = 1; i < partCount && secondSpeaker == 0; ++i)
        {
            const std::uint32_t speaker = ReadAt<std::uint32_t>(parts, i * kPartSize);
            if (speaker != firstSpeaker)
                secondSpeaker = speaker;
        }

        const int firstVoice = VoiceOfSpeaker(firstSpeaker);
        const int secondVoice = VoiceOfSpeaker(secondSpeaker);
        if (firstVoice < 0 || secondVoice < 0)
            return false;

        std::memcpy(out, conversation, 0x8);
        WriteAt<std::uint32_t>(out, 0x8, opener.lineCount + partCount);

        std::uint8_t* part = out + kSequenceHeaderSize;
        for (std::uint32_t i = 0; i < opener.lineCount; ++i)
        {
            const OpenerLine& line = opener.lines[i];
            const std::uint32_t speaker = line.saidByFirstSpeaker ? firstSpeaker : secondSpeaker;
            const int voice = line.saidByFirstSpeaker ? firstVoice : secondVoice;
            std::memset(part, 0, kPartSize);
            WriteAt<std::uint32_t>(part, 0x0, speaker);
            WriteAt<std::uint32_t>(part, 0x4, line.keyByVoice[voice]);
            WriteAt<std::uint32_t>(part, 0x8, line.action);
            part += kPartSize;
        }
        return true;
    }

    bool BuildGreetingSequenceGuarded(ConversationSlot& slot, std::uint32_t label, const std::uint8_t* conversation, const Opener& opener)
    {
        __try
        {
            return BuildGreetingSequence(slot, label, conversation, opener);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool BuildRestoredMbStory08(ConversationSlot& slot, const std::uint8_t* conversation)
    {
        const std::uint32_t partCount = ReadAt<std::uint32_t>(conversation, 0x8);
        const std::uint8_t* parts = conversation + kSequenceHeaderSize;
        if (partCount != kMbStory08PartCount
            || ReadAt<std::uint32_t>(parts, (kMbStory08CutLinesAt - 1) * kPartSize + 0x4) != kMassacredThemHuh
            || ReadAt<std::uint32_t>(parts, kMbStory08CutLinesAt * kPartSize + 0x4) != kIHearYou)
            return false;

        std::uint8_t* out = slot.sequence;
        std::memcpy(out, conversation, 0x8);
        WriteAt<std::uint32_t>(out, 0x8, partCount + kMbStory08CutLineCount);

        std::uint8_t* part = out + kSequenceHeaderSize;
        std::memcpy(part, parts, kMbStory08CutLinesAt * kPartSize);
        part += kMbStory08CutLinesAt * kPartSize;
        for (const RestoredLine& line : kMbStory08CutLines)
        {
            std::memset(part, 0, kPartSize);
            WriteAt<std::uint32_t>(part, 0x0, line.speaker);
            WriteAt<std::uint32_t>(part, 0x4, line.voiceParam);
            WriteAt<std::uint32_t>(part, 0x8, line.action);
            part += kPartSize;
        }
        std::memcpy(part, parts + kMbStory08CutLinesAt * kPartSize, (partCount - kMbStory08CutLinesAt) * kPartSize);
        return true;
    }

    bool BuildRestoredMbStory08Guarded(ConversationSlot& slot, const std::uint8_t* conversation)
    {
        __try
        {
            return BuildRestoredMbStory08(slot, conversation);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool PlaceSlotInTable(ConversationSlot& slot, SpeechFileTableEntry* table)
    {
        if (slot.tableIndex < kSpeechFileTableSize)
        {
            const SpeechFileTableEntry& entry = table[slot.tableIndex];
            if (entry.sequence == slot.sequence && entry.label == slot.tableLabel)
                return true;
        }

        for (std::uint16_t i = 0; i < kSpeechFileTableSize; ++i)
        {
            if (table[i].sequence != nullptr)
                continue;
            table[i].sequence = slot.sequence;
            table[i].label = slot.tableLabel;
            slot.tableIndex = i;
            return true;
        }
        slot.tableIndex = kNoSequence;
        return false;
    }

    template <typename BuildSequence>
    std::uint16_t PlayRebuiltConversation(std::uint16_t conversationIndex, BuildSequence buildSequence)
    {
        auto* table = reinterpret_cast<SpeechFileTableEntry*>(ResolveGameAddress(gAddr.SpeechFileTable));
        void* tableLock = ResolveGameAddress(gAddr.SpeechFileTableLock);
        const auto enterLock = reinterpret_cast<LockSection_t>(ResolveGameAddress(gAddr.LwLockSection_Enter));
        const auto leaveLock = reinterpret_cast<LockSection_t>(ResolveGameAddress(gAddr.LwLockSection_Leave));
        if (!table || !tableLock || !enterLock || !leaveLock)
            return kNoSequence;

        const std::uint8_t* conversation = table[conversationIndex].sequence;
        if (!conversation)
            return kNoSequence;

        std::lock_guard<std::mutex> lock(g_SlotMutex);

        const ULONGLONG now = GetTickCount64();
        ConversationSlot* slot = FindFreeSlot(now);
        if (!slot)
        {
            if (!g_WarnedSlotsBusy)
                LogWarn("SoldierConversations: all %u restored-conversation slots are still playing, this conversation plays as vanilla", static_cast<unsigned>(kConversationSlotCount));
            g_WarnedSlotsBusy = true;
            return kNoSequence;
        }

        if (!buildSequence(*slot, conversation))
            return kNoSequence;

        if (slot->tableLabel == 0)
        {
            char name[32];
            std::snprintf(name, sizeof(name), "V_SoldierConversation_%02u", static_cast<unsigned>(slot - g_Slots));
            slot->tableLabel = FoxHashes::StrCode32(name);
        }

        enterLock(tableLock);
        const bool placed = PlaceSlotInTable(*slot, table);
        leaveLock(tableLock);

        if (!placed)
        {
            if (!g_WarnedTableFull)
                LogWarn("SoldierConversations: the speech table is full, conversations play as vanilla");
            g_WarnedTableFull = true;
            return kNoSequence;
        }

        slot->everUsed = true;
        slot->lastUsedMs = now;
        return slot->tableIndex;
    }

    static std::uint16_t __fastcall hk_FindSpeechSequenceIndex(void* speechSystem, std::uint32_t label)
    {
        const std::uint16_t conversationIndex = g_OrigFindSpeechSequenceIndex(speechSystem, label);
        if (conversationIndex >= kSpeechFileTableSize)
            return conversationIndex;

        const bool isMbStory08 = label == kMbStory08;
        const Faction faction = isMbStory08 ? Faction::None : FactionOfConversation(label);
        if (!isMbStory08 && faction == Faction::None)
            return conversationIndex;
        if (MissionCodeGuard::ShouldBypassHooks())
            return conversationIndex;
        if (isMbStory08 && !FeatureSwitch::IsOn(FeatureSwitch::Feature::StaffTalk))
            return conversationIndex;

        std::uint16_t rebuiltIndex = kNoSequence;
        if (isMbStory08)
            rebuiltIndex = PlayRebuiltConversation(conversationIndex,
                [](ConversationSlot& slot, const std::uint8_t* conversation) { return BuildRestoredMbStory08Guarded(slot, conversation); });
        else
        {
            const bool greetings = FeatureSwitch::IsOn(FeatureSwitch::Feature::SoldierConversations);
            rebuiltIndex = PlayRebuiltConversation(conversationIndex,
                [faction, label, greetings](ConversationSlot& slot, const std::uint8_t* conversation) { return BuildGreetingSequenceGuarded(slot, label, conversation, greetings ? PickOpener(faction) : kNoOpener); });
        }
        return rebuiltIndex < kSpeechFileTableSize ? rebuiltIndex : conversationIndex;
    }

    std::int32_t ReadLuaNumber(lua_State* L, const char* global, const char* group, const char* field)
    {
        const int top = g_lua_gettop(L);
        std::int32_t result = -1;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>(global));
        bool found = g_lua_type(L, -1) == LUA_TTABLE || g_lua_type(L, -1) == LUA_TUSERDATA;
        if (found && group)
        {
            g_lua_getfield(L, -1, const_cast<char*>(group));
            found = g_lua_type(L, -1) == LUA_TTABLE;
        }
        if (found)
        {
            g_lua_getfield(L, -1, const_cast<char*>(field));
            if (g_lua_type(L, -1) == LUA_TNUMBER)
                result = static_cast<std::int32_t>(g_lua_tonumber(L, -1));
        }
        g_lua_settop(L, top);
        return result;
    }

    std::int32_t RevengeLevel(lua_State* L, const char* revengeType)
    {
        const std::int32_t type = ReadLuaNumber(L, "TppRevenge", "REVENGE_TYPE", revengeType);
        if (type < 0)
            return -1;

        const int top = g_lua_gettop(L);
        std::int32_t level = -1;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppRevenge"));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_getfield(L, -1, const_cast<char*>("GetRevengeLv"));
            if (g_lua_type(L, -1) == LUA_TFUNCTION)
            {
                g_lua_pushnumber(L, type);
                if (g_lua_pcall(L, 1, 1, 0) == 0 && g_lua_type(L, -1) == LUA_TNUMBER)
                    level = static_cast<std::int32_t>(g_lua_tonumber(L, -1));
            }
        }
        g_lua_settop(L, top);
        return level;
    }

    bool IsFirstFobBuilt(lua_State* L)
    {
        const int top = g_lua_gettop(L);
        bool built = false;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppMotherBaseManagement"));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_getfield(L, -1, const_cast<char*>("IsBuiltFirstFob"));
            if (g_lua_type(L, -1) == LUA_TFUNCTION && g_lua_pcall(L, 0, 1, 0) == 0)
                built = g_lua_toboolean(L, -1) != 0;
        }
        g_lua_settop(L, top);
        return built;
    }

    bool AppendConversation(lua_State* L, int list, const char* label)
    {
        const int count = static_cast<int>(g_lua_objlen(L, list));
        for (int i = 1; i <= count; ++i)
        {
            g_lua_rawgeti(L, list, i);
            const bool listed = g_lua_type(L, -1) == LUA_TSTRING && std::strcmp(GetLuaString(L, -1), label) == 0;
            g_lua_settop(L, -2);
            if (listed)
                return false;
        }
        g_lua_pushnumber(L, static_cast<lua_Number>(count + 1));
        g_lua_pushstring(L, const_cast<char*>(label));
        g_lua_rawset(L, list);
        return true;
    }
}

namespace SoldierConversations
{
    void AddRestoredConversations(lua_State* L, int commandIndex)
    {
        if (!g_OrigFindSpeechSequenceIndex || !L || !ResolveLuaApi() || MissionCodeGuard::ShouldBypassHooks())
            return;

        const int top = g_lua_gettop(L);
        g_lua_getfield(L, commandIndex, const_cast<char*>("list"));
        if (g_lua_type(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            return;
        }
        const int list = g_lua_gettop(L);

        const std::int32_t combat = RevengeLevel(L, "COMBAT");
        const std::int32_t stealth = RevengeLevel(L, "STEALTH");
        const std::int32_t walkerGearsShowUp = ReadLuaNumber(L, "TppDefine", "STORY_SEQUENCE", "CLEARD_DESTROY_THE_FLOW_STATION"); // enemy Walker Gears first appear in Pitch Dark
        const bool conversations = FeatureSwitch::IsOn(FeatureSwitch::Feature::SoldierConversations);
        const bool specialUnit = conversations && (combat >= kSpecialSoldierLevel || stealth >= kSpecialSoldierLevel);
        const bool ostriches = conversations && walkerGearsShowUp >= 0 && ReadLuaNumber(L, "gvars", nullptr, "str_storySequence") >= walkerGearsShowUp;
        const bool moreGuys = conversations && combat >= kMoreReinforcementsLevel;
        const bool firstFob = FeatureSwitch::IsOn(FeatureSwitch::Feature::StaffTalk) && IsFirstFobBuilt(L);

        int added = 0;
        if (specialUnit)
        {
            added += AppendConversation(L, list, "PF_counter_02");
            added += AppendConversation(L, list, "USSR_counter_02");
        }
        if (ostriches)
        {
            added += AppendConversation(L, list, "PF_counter_09");
            added += AppendConversation(L, list, "USSR_counter_09");
        }
        if (moreGuys)
        {
            added += AppendConversation(L, list, "PF_counter_21");
            added += AppendConversation(L, list, "USSR_counter_21");
        }
        if (firstFob)
            added += AppendConversation(L, list, "MB_story_06");
        g_lua_settop(L, top);

    }
}

bool Install_SoldierConversations_Hook()
{
    if (!gAddr.SpeechSystemImpl_FindSpeechSequenceIndex || !gAddr.SpeechFileTable || !gAddr.SpeechFileTableLock
        || !gAddr.TppWeatherManager_s_Instance || !gAddr.SoldierConversationService_ConvertSpeechLabelToConversationType
        || !gAddr.LwLockSection_Enter || !gAddr.LwLockSection_Leave)
    {
        LogError("SoldierConversations: speech addresses missing for this build, restored conversation lines off");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.SpeechSystemImpl_FindSpeechSequenceIndex);
    if (!target)
    {
        LogError("SoldierConversations: FindSpeechSequenceIndex unresolved, restored conversation lines off");
        return false;
    }

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_FindSpeechSequenceIndex),
                             reinterpret_cast<void**>(&g_OrigFindSpeechSequenceIndex)))
    {
        LogError("SoldierConversations: FindSpeechSequenceIndex hook failed, restored conversation lines off");
        return false;
    }
    return true;
}

bool Uninstall_SoldierConversations_Hook()
{
    if (g_OrigFindSpeechSequenceIndex)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.SpeechSystemImpl_FindSpeechSequenceIndex));
    g_OrigFindSpeechSequenceIndex = nullptr;
    return true;
}
