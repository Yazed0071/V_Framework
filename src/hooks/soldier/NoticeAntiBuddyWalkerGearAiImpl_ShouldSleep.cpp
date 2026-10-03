#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierFaction.h"
#include "log.h"

namespace
{
    using ShouldSleep_t   = bool(__fastcall*)(void* self, std::uint32_t soldierIndex, void* const* context);
    using GetNoticeInfo_t = const std::uint8_t*(__fastcall*)(void* noticeInterface, std::uint32_t soldierIndex);

    constexpr std::size_t  kNodeSoldierCore        = 0x180;
    constexpr std::size_t  kSoldierCoreNotice      = 0x68;
    constexpr std::size_t  kGetNoticeInfoSlot      = 2;
    constexpr std::size_t  kSoldierCoreCommandPost = 0xD0;
    constexpr std::size_t  kCommandPostMembers     = 0x10;
    constexpr std::size_t  kMemberStride           = 0x20;
    constexpr std::size_t  kMemberState            = 0x1D;
    constexpr std::uint8_t kMemberStateOffDuty     = 6u;
    constexpr std::size_t  kKnowledgeLatchedNotice = 0x9F;
    constexpr std::size_t  kKnowledgeStep          = 0x54;
    constexpr std::uint8_t kFinalStep              = 0x14;

    ShouldSleep_t g_OrigShouldSleep = nullptr;

    bool ReadCurrentNoticeType(void* soldierCore, std::uint32_t soldierIndex, std::uint8_t* noticeType)
    {
        __try
        {
            void* noticeInterface = *reinterpret_cast<void* const*>(static_cast<std::uint8_t*>(soldierCore) + kSoldierCoreNotice);
            if (!noticeInterface)
                return false;
            GetNoticeInfo_t getNoticeInfo = (*reinterpret_cast<GetNoticeInfo_t* const*>(noticeInterface))[kGetNoticeInfoSlot];
            const std::uint8_t* notice = getNoticeInfo(noticeInterface, soldierIndex);
            if (!notice)
                return false;
            *noticeType = notice[0];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t ReadMemberState(void* soldierCore, std::uint32_t soldierIndex)
    {
        __try
        {
            const std::uint8_t* commandPost = *reinterpret_cast<const std::uint8_t* const*>(
                static_cast<std::uint8_t*>(soldierCore) + kSoldierCoreCommandPost);
            const std::uint8_t* members = *reinterpret_cast<const std::uint8_t* const*>(commandPost + kCommandPostMembers);
            return members[soldierIndex * kMemberStride + kMemberState];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kMemberStateOffDuty;
        }
    }

    bool RivalFightShouldEnd(void* self, std::uint32_t soldierIndex, const std::uint8_t* knowledge,
                             char* reason, std::size_t reasonSize)
    {
        void* soldierCore = nullptr;
        std::memcpy(&soldierCore, static_cast<const std::uint8_t*>(self) + kNodeSoldierCore, sizeof(soldierCore));
        if (!soldierCore)
        {
            std::snprintf(reason, reasonSize, "soldier core missing");
            return true;
        }

        std::uint8_t noticeType = 0;
        if (ReadCurrentNoticeType(soldierCore, soldierIndex, &noticeType) && noticeType != knowledge[kKnowledgeLatchedNotice])
        {
            std::snprintf(reason, reasonSize, "interrupted by notice 0x%02X", noticeType);
            return true;
        }
        if (ReadMemberState(soldierCore, soldierIndex) == kMemberStateOffDuty)
        {
            std::snprintf(reason, reasonSize, "soldier left combat duty");
            return true;
        }
        if (const char* lost = SoldierFaction_WhyRivalLost(soldierCore, soldierIndex))
        {
            std::snprintf(reason, reasonSize, "%s", lost);
            return true;
        }
        if (knowledge[kKnowledgeStep] == kFinalStep)
        {
            std::snprintf(reason, reasonSize, "combat routine finished");
            return true;
        }
        return false;
    }

    bool __fastcall hk_ShouldSleep(void* self, std::uint32_t soldierIndex, void* const* context)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigShouldSleep, self, soldierIndex, context);

        if (SoldierFaction_GetRivalTarget(soldierIndex) == 0 || !context || !context[0])
            return g_OrigShouldSleep(self, soldierIndex, context);

        char reason[64] = {};
        if (!RivalFightShouldEnd(self, soldierIndex, static_cast<const std::uint8_t*>(context[0]), reason, sizeof(reason)))
            return false;

        SoldierFaction_EndRivalFight(soldierIndex, reason);
        return true;
    }
}

bool Install_NoticeAntiBuddyWalkerGearAiImpl_ShouldSleep_Hook()
{
    void* target = ResolveGameAddress(gAddr.NoticeAntiBuddyWalkerGearAiImpl_ShouldSleep);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_ShouldSleep),
                               reinterpret_cast<void**>(&g_OrigShouldSleep));
}

bool Uninstall_NoticeAntiBuddyWalkerGearAiImpl_ShouldSleep_Hook()
{
    void* target = ResolveGameAddress(gAddr.NoticeAntiBuddyWalkerGearAiImpl_ShouldSleep);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigShouldSleep = nullptr;
    return true;
}
