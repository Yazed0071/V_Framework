#include <pch.h>
#include "PlayerSkill.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "log.h"
#include "../outfit/CustomHeadRegistry.h"

namespace
{
    using StartSkill_t     = bool(__fastcall*)(void* skills, std::uint32_t playerIndex, std::uint8_t skill, std::uint32_t level);
    using StopSkill_t      = bool(__fastcall*)(void* skills, std::uint32_t playerIndex, std::uint8_t skill);
    using IsSkillActive_t  = bool(__fastcall*)(void* skills, std::uint32_t playerIndex, std::uint8_t skill);
    using GetCommonValue_t = float(__fastcall*)(void* skills, std::uint32_t playerIndex, std::uint8_t skill);
    using SetCommonValue_t = bool(__fastcall*)(void* skills, std::uint32_t playerIndex, std::uint8_t skill, float value);
    using GetValueOnStart_t = float(__fastcall*)(void* skills, std::uint32_t playerIndex, std::uint8_t skill);

    constexpr std::size_t kUiPlayerContext    = 0x28;
    constexpr std::size_t kContextSkills      = 0xF8;
    constexpr std::size_t kContextPlayerIndex = 0x214;
    constexpr std::size_t kSkillsPlayerCount  = 0x28;
    constexpr std::size_t kSkillsSlots        = 0x30;   // per player 0x20 slots of 0x10 bytes: u8 skill, +4 value, +0xC value at the start
    constexpr std::size_t kSlotsPerPlayer     = 0x20;
    constexpr std::size_t kSlotSize           = 0x10;
    constexpr std::size_t kSlotValueOnStart   = 0x0C;

    constexpr std::size_t kStartSkillSlot     = 0x00 / sizeof(void*);
    constexpr std::size_t kStopSkillSlot      = 0x08 / sizeof(void*);
    constexpr std::size_t kIsSkillActiveSlot  = 0x10 / sizeof(void*);
    constexpr std::size_t kGetCommonValueSlot = 0x18 / sizeof(void*);   // seconds left of a timed skill
    constexpr std::size_t kSetCommonValueSlot = 0x28 / sizeof(void*);
    constexpr std::size_t kGetValueOnStartSlot = 0x40 / sizeof(void*);

    constexpr std::uint64_t kRequestLifeMs = 2000;   // a request made while the player is not updating (loading, title) is dropped

    const int kUsable[] = { 4, 8, 9, 10, 11, 12, 13 };   // StealthCamo, Pentazemin, Noctocyanin, Acceleramin, ParasiteMist, ParasiteCamo, ParasiteHard
    constexpr int          kFirstParasiteSkill = 11;
    constexpr std::uint8_t kParasiteSuitParts  = 10;   // PlayerPartsType PARASITE: the only suit whose material these skills drive

    enum class Ask : std::uint8_t { None, Start, Stop };

    struct Request
    {
        Ask           ask     = Ask::None;
        std::uint32_t level   = 1;
        float         seconds = 0.0f;
        int           usedItem = 0;
        std::uint64_t madeAt  = 0;
    };

    std::mutex        g_Lock;
    Request           g_Requests[PlayerSkill::kSkillCount];
    std::atomic<bool> g_AnyRequest{ false };

    std::atomic<std::uint32_t> g_ActiveMask{ 0 };
    std::atomic<float>         g_Seconds[PlayerSkill::kSkillCount];
    std::atomic<float>         g_StartSeconds[PlayerSkill::kSkillCount];
    std::atomic<int>           g_UsedItem[PlayerSkill::kSkillCount];   // the item whose Use started the skill, 0 = none

    void* g_SeenSkills  = nullptr;   // HUD update only
    bool  g_MismatchLog = false;

    bool ResolveSEH(void* uiController, void* expectedStartSkill, void*& skills, void**& vtable, std::uint32_t& playerIndex, bool& mismatch)
    {
        mismatch = false;
        __try
        {
            auto* context = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(uiController) + kUiPlayerContext);
            if (!context)
                return false;
            skills = *reinterpret_cast<void**>(context + kContextSkills);
            if (!skills)
                return false;
            vtable = *reinterpret_cast<void***>(skills);
            if (!vtable || vtable[kStartSkillSlot] != expectedStartSkill)
            {
                mismatch = true;
                return false;
            }
            playerIndex = *reinterpret_cast<std::uint32_t*>(context + kContextPlayerIndex);
            return playerIndex < *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(skills) + kSkillsPlayerCount);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool StartSEH(void** vtable, void* skills, std::uint32_t playerIndex, std::uint8_t skill, std::uint32_t level, float seconds)
    {
        __try
        {
            if (!reinterpret_cast<StartSkill_t>(vtable[kStartSkillSlot])(skills, playerIndex, skill, level))
                return false;
            if (seconds <= 0.0f)
                seconds = reinterpret_cast<GetCommonValue_t>(vtable[kGetCommonValueSlot])(skills, playerIndex, skill);   // a second start adds the grade's length; the bar then runs over the new total
            else
                reinterpret_cast<SetCommonValue_t>(vtable[kSetCommonValueSlot])(skills, playerIndex, skill, seconds);
            if (seconds > 0.0f)
            {
                auto* slot = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(skills) + kSkillsSlots)
                             + static_cast<std::size_t>(playerIndex) * kSlotsPerPlayer * kSlotSize;
                for (std::size_t i = 0; i < kSlotsPerPlayer; ++i, slot += kSlotSize)
                {
                    if (*slot != skill)
                        continue;
                    *reinterpret_cast<float*>(slot + kSlotValueOnStart) = seconds; // the HUD bars draw time left over this value; left at the grade's own length, a longer time shows as a full bar that does not move
                    break;
                }
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void StopSEH(void** vtable, void* skills, std::uint32_t playerIndex, std::uint8_t skill)
    {
        __try
        {
            reinterpret_cast<StopSkill_t>(vtable[kStopSkillSlot])(skills, playerIndex, skill);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool SnapshotSEH(void** vtable, void* skills, std::uint32_t playerIndex, std::uint32_t& mask, float* seconds, float* startSeconds)
    {
        mask = 0;
        __try
        {
            for (const int skill : kUsable)
            {
                if (!reinterpret_cast<IsSkillActive_t>(vtable[kIsSkillActiveSlot])(skills, playerIndex, static_cast<std::uint8_t>(skill)))
                    continue;
                mask |= 1u << skill;
                seconds[skill] = reinterpret_cast<GetCommonValue_t>(vtable[kGetCommonValueSlot])(skills, playerIndex, static_cast<std::uint8_t>(skill));
                startSeconds[skill] = reinterpret_cast<GetValueOnStart_t>(vtable[kGetValueOnStartSlot])(skills, playerIndex, static_cast<std::uint8_t>(skill));
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool Queue(int skill, Ask ask, int level, float seconds, int usedItem)
    {
        if (!PlayerSkill::IsUsable(skill))
            return false;
        std::lock_guard<std::mutex> lock(g_Lock);
        Request& request = g_Requests[skill];
        request.ask     = ask;
        request.level   = static_cast<std::uint32_t>(level < 1 ? 1 : level);
        request.seconds = seconds;
        request.usedItem = usedItem;
        request.madeAt  = GetTickCount64();
        g_AnyRequest.store(true, std::memory_order_release);
        return true;
    }
}

bool PlayerSkill::IsUsable(int skill)
{
    for (const int usable : kUsable)
        if (usable == skill)
            return true;
    return false;
}

bool PlayerSkill::Start(int skill, int level, float seconds, int usedItem)
{
    if (skill >= kFirstParasiteSkill)
    {
        std::uint8_t partsType = 0;
        if (!outfit::GetCurrentEquippedSuitBytes(&partsType, nullptr) || partsType != kParasiteSuitParts)
        {
            static std::atomic<bool> s_told{ false };
            if (!s_told.exchange(true))
                LogWarn("V_Player.StartSkill: the Parasite Suit skills start only while the Parasite Suit is worn");
            return false;
        }
    }
    return Queue(skill, Ask::Start, level, seconds, usedItem);
}

bool PlayerSkill::Stop(int skill)
{
    return Queue(skill, Ask::Stop, 1, 0.0f, 0);
}

bool PlayerSkill::IsActive(int skill)
{
    return IsUsable(skill) && (g_ActiveMask.load(std::memory_order_acquire) >> skill & 1u) != 0;
}

bool PlayerSkill::GetTime(int skill, float& seconds)
{
    if (!IsActive(skill))
        return false;
    seconds = g_Seconds[skill].load(std::memory_order_acquire);
    return true;
}

bool PlayerSkill::ItemGauge(int equipId, int& current, int& max)
{
    if (equipId <= 0)
        return false;
    const std::uint32_t mask = g_ActiveMask.load(std::memory_order_acquire);
    float left = 0.0f;
    float start = 0.0f;
    for (const int skill : kUsable)
    {
        if ((mask >> skill & 1u) == 0 || g_UsedItem[skill].load(std::memory_order_acquire) != equipId)
            continue;
        const float seconds = g_Seconds[skill].load(std::memory_order_acquire);
        if (seconds <= left)
            continue;
        left  = seconds;
        start = g_StartSeconds[skill].load(std::memory_order_acquire);
    }
    current = static_cast<int>(left);
    max     = static_cast<int>(start);
    if (current > 0 && max == 0)   // the two rules the game applies to its own effect bars
        max = current;
    if (current > max)
        current = max;
    return current >= 1;
}

void PlayerSkill::DropRequests()
{
    if (!g_AnyRequest.exchange(false, std::memory_order_acq_rel))
        return;
    std::lock_guard<std::mutex> lock(g_Lock);
    for (Request& request : g_Requests)
        request = Request();
}

void PlayerSkill::OnUpdateEquipHud(void* uiController)
{
    void*  expected = gAddr.SkillControllerImpl_StartSkill ? ResolveGameAddress(gAddr.SkillControllerImpl_StartSkill) : nullptr;
    void*  skills = nullptr;
    void** vtable = nullptr;
    std::uint32_t playerIndex = 0;
    bool mismatch = false;
    if (!expected || !ResolveSEH(uiController, expected, skills, vtable, playerIndex, mismatch))
    {
        if (mismatch && !g_MismatchLog)
        {
            g_MismatchLog = true;
            LogError("PlayerSkill: the skill controller of the player is not the one of %s, V_Player skill calls are off", GetGameBuildName(gGameBuild));
        }
        DropRequests();
        return;
    }

    if (g_AnyRequest.exchange(false, std::memory_order_acq_rel))
    {
        Request requests[kSkillCount];
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            for (int skill = 0; skill < kSkillCount; ++skill)
            {
                requests[skill] = g_Requests[skill];
                g_Requests[skill] = Request();
            }
        }
        const std::uint64_t now = GetTickCount64();
        for (const int skill : kUsable)
        {
            const Request& request = requests[skill];
            if (request.ask == Ask::None || now - request.madeAt > kRequestLifeMs)
                continue;
            if (request.ask == Ask::Stop)
                StopSEH(vtable, skills, playerIndex, static_cast<std::uint8_t>(skill));
            else if (StartSEH(vtable, skills, playerIndex, static_cast<std::uint8_t>(skill), request.level,
                              skill == kAcceleramin ? 0.0f : request.seconds))
                g_UsedItem[skill].store(request.usedItem, std::memory_order_release);
        }
    }

    std::uint32_t mask = 0;
    float seconds[kSkillCount] = {};
    float startSeconds[kSkillCount] = {};
    if (!SnapshotSEH(vtable, skills, playerIndex, mask, seconds, startSeconds))
        return;
    const std::uint32_t before = skills == g_SeenSkills ? g_ActiveMask.load(std::memory_order_acquire) : mask;   // a new player object starts silent
    g_SeenSkills = skills;
    for (const int skill : kUsable)
    {
        g_Seconds[skill].store(seconds[skill], std::memory_order_release);
        g_StartSeconds[skill].store(startSeconds[skill], std::memory_order_release);
        if ((mask >> skill & 1u) == 0)
            g_UsedItem[skill].store(0, std::memory_order_release);
    }
    g_ActiveMask.store(mask, std::memory_order_release);
    for (const int skill : kUsable)
    {
        const std::uint32_t bit = 1u << skill;
        if ((mask & bit) != 0 && (before & bit) == 0)
            V_FrameWork::EmitMessage("Player", "SkillStart", skill);
        else if ((mask & bit) == 0 && (before & bit) != 0)
            V_FrameWork::EmitMessage("Player", "SkillEnd", skill);
    }
}
