#include <pch.h>

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "VIPRadioHook.h"
#include "VIPImportant.h"
#include "CommandPost2Impl_RequestInvestigation.h"
#include "CpRadioVariants.h"
#include "SoldierBackupCall.h"
#include "StepRadioDiscovery.h"
#include "AddressSet.h"

namespace
{


    static constexpr std::uint8_t kRadioTypeBodyFound = 0x0E;


    static constexpr std::uint8_t kRadioTypeVipBodyFound = 0x0F;


    static constexpr std::uint8_t kRadioTypeNoLine = 0x5F;   // ConvertRadioTypeToSpeechLabel's default: label 0


    static constexpr std::uint32_t kOfficerBodyFoundSpeechLabel = 0xDD6EA61Bu;   // V_CPR0042

    constexpr std::size_t   kSoldierSlots          = 0x200;
    constexpr std::uint16_t kNoSoldier             = 0xFFFF;
    constexpr std::uint16_t kNoMember              = 0xFFFF;
    constexpr std::uint16_t kCorpseType            = 0x26;     // GameObjectId >> 9
    constexpr std::uint16_t kGameObjectIndexMask   = 0x1FF;
    constexpr std::size_t   kCorpseRecords         = 0x38;     // one 0x48 record per corpse, the soldier it was at +0x44
    constexpr std::size_t   kCorpseRecordSize      = 0x48;
    constexpr std::size_t   kCorpseSoldier         = 0x44;
    constexpr std::size_t   kFootFoundComrade      = 0x26;     // the downed comrade, or his corpse
    constexpr std::size_t   kVehicleFoundComrade   = 0x14;
    constexpr std::size_t   kNodeSoldier           = 0x70;
    constexpr std::size_t   kSoldierCommandPost    = 0xD0;
    constexpr std::size_t   kCommandPostController = 0x08;
    constexpr std::size_t   kCommandPostClient     = 0x30;
    constexpr std::size_t   kControllerClients     = 0x1F0;    // the CP numbers his calls firstMember (+0x40 of his 0x48 client) + his index
    constexpr std::size_t   kClientSize            = 0x48;
    constexpr std::size_t   kClientFirstMember     = 0x40;
    constexpr std::int32_t  kStepLeave             = 1;

    constexpr std::uint8_t kFootStepRadioEntry[] = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xB8,
        0x48, 0x81, 0xEC, 0x48, 0x01, 0x00, 0x00 };

    constexpr std::uint8_t kVehicleStepRadioEntry[] = {
        0x40, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00,
        0x48, 0x8B, 0x05 };


    using RequestCorpse_t = void(__fastcall*)(
        void* self,
        void* animControl,
        void* ragdollPlugin,
        void* facialPlugin,
        std::uint32_t* facialParam,
        const void* location,
        std::uint16_t originalGameObjectId,
        const void* inheritanceInfo,
        bool fromScript);


    using CallWithRadioType_t = short* (__fastcall*)(
        void* self,
        short* outHandle,
        std::uint32_t ownerIndex,
        std::uint8_t radioType,
        std::uint16_t arg5);


    using CallImpl_t = short* (__fastcall*)(
        long long selfMinus20,
        short* outHandle,
        int ownerIndex,
        std::uint32_t speechLabel,
        std::uint16_t arg5);

    using StepRadio_t = void(__fastcall*)(void* node, std::uint32_t soldierIndex, std::int32_t proc, std::uint8_t* knowledge);

    static RequestCorpse_t g_OrigRequestCorpse = nullptr;
    static CallWithRadioType_t g_OrigCallWithRadioType = nullptr;
    static CallImpl_t g_CallImpl = nullptr;
    static StepRadio_t g_OrigFootStepRadio = nullptr;
    static StepRadio_t g_OrigVehicleStepRadio = nullptr;
    static void* g_FootStepRadioTarget = nullptr;
    static void* g_VehicleStepRadioTarget = nullptr;

    std::atomic<std::uint8_t*> g_CorpseManager{ nullptr };

    constexpr std::uint64_t kFoundVanillaVip        = 1ull << 32;   // below it: his CP member number << 16 | the body's soldier GameObjectId
    constexpr std::uint64_t kFoundVanillaVipSpecial = 1ull << 33;

    std::array<std::atomic<std::uint64_t>, kSoldierSlots> g_FoundBodyByReporter{};


    std::uint16_t ReadFoundComradeSEH(const std::uint8_t* knowledge, std::size_t foundComrade)
    {
        __try
        {
            return *reinterpret_cast<const std::uint16_t*>(knowledge + foundComrade);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoSoldier;
        }
    }


    std::uint16_t CorpseSoldierSEH(const std::uint8_t* corpseManager, std::uint16_t corpseIndex)
    {
        __try
        {
            const auto* records = *reinterpret_cast<const std::uint8_t* const*>(corpseManager + kCorpseRecords);
            if (!records)
                return kNoSoldier;
            return *reinterpret_cast<const std::uint16_t*>(records + corpseIndex * kCorpseRecordSize + kCorpseSoldier);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoSoldier;
        }
    }


    const std::uint8_t* NodeSoldiersSEH(void* node)
    {
        __try
        {
            return *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(node) + kNodeSoldier);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }


    std::uint16_t ReporterMemberSEH(const std::uint8_t* soldiers, std::uint32_t soldierIndex)
    {
        if (!soldiers)
            return kNoMember;
        __try
        {
            const auto* commandPost = *reinterpret_cast<const std::uint8_t* const*>(soldiers + kSoldierCommandPost);
            const auto* controller = *reinterpret_cast<const std::uint8_t* const*>(commandPost + kCommandPostController);
            const std::uint32_t client = *reinterpret_cast<const std::uint32_t*>(commandPost + kCommandPostClient);
            const auto* clients = *reinterpret_cast<const std::uint8_t* const*>(controller + kControllerClients);
            const std::uint16_t firstMember = *reinterpret_cast<const std::uint16_t*>(clients + client * kClientSize + kClientFirstMember);
            return static_cast<std::uint16_t>(firstMember + soldierIndex);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoMember;
        }
    }


    std::uint16_t SoldierOfBody(std::uint16_t bodyId)
    {
        if (SoldierIndexOfGameObjectId(bodyId) != kNoSoldier)
            return bodyId;
        const std::uint8_t* corpseManager = g_CorpseManager.load(std::memory_order_acquire);
        if ((bodyId >> 9) != kCorpseType || !corpseManager)
            return kNoSoldier;
        return CorpseSoldierSEH(corpseManager, bodyId & kGameObjectIndexMask);
    }


    std::uint64_t FoundBodyOfMember(std::uint16_t member)
    {
        if (member == kNoMember)
            return 0;
        for (const auto& slot : g_FoundBodyByReporter)
        {
            const std::uint64_t found = slot.load(std::memory_order_acquire);
            if (found != 0 && static_cast<std::uint16_t>(found >> 16) == member)
                return found;
        }
        return 0;
    }


    void ClearFoundBodies()
    {
        for (auto& slot : g_FoundBodyByReporter)
            slot.store(0, std::memory_order_release);
    }


    void NoteFoundBody(void* node, std::uint32_t soldierIndex, std::int32_t proc, const std::uint8_t* knowledge, std::size_t foundComrade)
    {
        if (soldierIndex >= kSoldierSlots || proc == kStepLeave || !knowledge)
            return;
        const std::uint8_t* soldiers = NodeSoldiersSEH(node);
        const std::uint16_t member = ReporterMemberSEH(soldiers, soldierIndex);
        const std::uint16_t body = SoldierOfBody(ReadFoundComradeSEH(knowledge, foundComrade));
        std::uint64_t found = 0;
        if (member != kNoMember && body != kNoSoldier)
        {
            found = (static_cast<std::uint64_t>(member) << 16) | body;
            const std::uint16_t bodyIndex = SoldierIndexOfGameObjectId(body);
            if (IsVanillaVIPSoldier(soldiers, bodyIndex))
                found |= kFoundVanillaVip;
            if (IsVanillaVIPSpecialSoldier(soldiers, bodyIndex))
                found |= kFoundVanillaVipSpecial;
        }
        g_FoundBodyByReporter[soldierIndex].store(found, std::memory_order_release);
    }


    void ForgetFoundBody(std::uint32_t soldierIndex, std::int32_t proc)
    {
        if (soldierIndex < kSoldierSlots && proc == kStepLeave)
            g_FoundBodyByReporter[soldierIndex].store(0, std::memory_order_release);
    }
}


static void __fastcall hkRequestCorpse(
    void* self,
    void* animControl,
    void* ragdollPlugin,
    void* facialPlugin,
    std::uint32_t* facialParam,
    const void* location,
    std::uint16_t originalGameObjectId,
    const void* inheritanceInfo,
    bool fromScript)
{
    g_CorpseManager.store(static_cast<std::uint8_t*>(self), std::memory_order_release);

    if (g_OrigRequestCorpse)
    {
        g_OrigRequestCorpse(
            self,
            animControl,
            ragdollPlugin,
            facialPlugin,
            facialParam,
            location,
            originalGameObjectId,
            inheritanceInfo,
            fromScript);
    }
}


static void __fastcall hkFootStepRadio(void* node, std::uint32_t soldierIndex, std::int32_t proc, std::uint8_t* knowledge)
{
    MISSION_GUARD_ORIGINAL_VOID(g_OrigFootStepRadio, node, soldierIndex, proc, knowledge);

    NoteFoundBody(node, soldierIndex, proc, knowledge, kFootFoundComrade);
    g_OrigFootStepRadio(node, soldierIndex, proc, knowledge);
    ForgetFoundBody(soldierIndex, proc);
}


static void __fastcall hkVehicleStepRadio(void* node, std::uint32_t soldierIndex, std::int32_t proc, std::uint8_t* knowledge)
{
    MISSION_GUARD_ORIGINAL_VOID(g_OrigVehicleStepRadio, node, soldierIndex, proc, knowledge);

    NoteFoundBody(node, soldierIndex, proc, knowledge, kVehicleFoundComrade);
    g_OrigVehicleStepRadio(node, soldierIndex, proc, knowledge);
    ForgetFoundBody(soldierIndex, proc);
}


static short* __fastcall hkCallWithRadioType(
    void* self,
    short* outHandle,
    std::uint32_t ownerIndex,
    std::uint8_t radioType,
    std::uint16_t arg5)
{
    if (MissionCodeGuard::ShouldBypassHooks())
    {
        if (g_OrigCallWithRadioType)
            return g_OrigCallWithRadioType(self, outHandle, ownerIndex, radioType, arg5);

        return outHandle;
    }

    std::uint32_t investigationLabel = 0;
    if (CpInvestigation_EndCall(ownerIndex, radioType, arg5, investigationLabel))
    {
        if (investigationLabel == 0 && g_OrigCallWithRadioType)   // silent: a type with no line takes vanilla's own no-voice branch (RadioEnd, handle 0xFFFF)
            return g_OrigCallWithRadioType(self, outHandle, ownerIndex, kRadioTypeNoLine, arg5);
        if (investigationLabel != 0 && g_CallImpl)
            return g_CallImpl(reinterpret_cast<long long>(self) - 0x20ll, outHandle, static_cast<int>(ownerIndex), investigationLabel, arg5);
    }

    if (const std::uint32_t backupLabel = SoldierBackupCall::LabelForCall(radioType, arg5))
    {
        if (g_CallImpl)
            return g_CallImpl(reinterpret_cast<long long>(self) - 0x20ll, outHandle, static_cast<int>(ownerIndex), backupLabel, arg5);
    }

    if (const std::uint32_t contactLabel = CpRadioVariants_LabelForCall(ownerIndex, radioType, arg5))
    {
        if (g_CallImpl)
            return g_CallImpl(reinterpret_cast<long long>(self) - 0x20ll, outHandle, static_cast<int>(ownerIndex), contactLabel, arg5);
    }

    {
        std::uint32_t hostageOverrideLabel = 0u;
        if (LostHostageDiscovery_TryOverrideForCallWithRadioType(
                ownerIndex, radioType, arg5, hostageOverrideLabel)
            && hostageOverrideLabel != 0u)
        {
            if (g_CallImpl)
            {
                return g_CallImpl(
                    reinterpret_cast<long long>(self) - 0x20ll,
                    outHandle,
                    static_cast<int>(ownerIndex),
                    hostageOverrideLabel,
                    arg5);
            }
        }
    }

    if (const std::uint32_t surveyLabel = CpRadioVariants_SurveyLabel(ownerIndex, radioType, arg5))
    {
        if (g_CallImpl)
            return g_CallImpl(reinterpret_cast<long long>(self) - 0x20ll, outHandle, static_cast<int>(ownerIndex), surveyLabel, arg5);
    }

    const std::uint64_t found = radioType == kRadioTypeBodyFound ? FoundBodyOfMember(arg5) : 0;
    const std::uint32_t ownLabel = found != 0 ? DeadBodyLabel_Get(static_cast<std::uint16_t>(found)) : 0;
    if (ownLabel != 0 && g_CallImpl)
        return g_CallImpl(reinterpret_cast<long long>(self) - 0x20ll, outHandle, static_cast<int>(ownerIndex), ownLabel, arg5);

    VIPImportantInfo vip{};
    const bool listed = found != 0 && VIPImportant_Find(static_cast<std::uint16_t>(found), &vip);
    if (!listed)
        vip.isOfficer = (found & kFoundVanillaVipSpecial) != 0;
    if (listed || (found & kFoundVanillaVip) != 0)
    {
        if (vip.deadBodyLabel != 0 && g_CallImpl)
        {
            return g_CallImpl(
                reinterpret_cast<long long>(self) - 0x20ll,
                outHandle,
                static_cast<int>(ownerIndex),
                vip.deadBodyLabel,
                arg5);
        }

        if (vip.isOfficer)
        {
            if (g_CallImpl)
            {
                return g_CallImpl(
                    reinterpret_cast<long long>(self) - 0x20ll,
                    outHandle,
                    static_cast<int>(ownerIndex),
                    kOfficerBodyFoundSpeechLabel,
                    arg5);
            }
        }
        else
        {
            if (g_OrigCallWithRadioType)
            {
                return g_OrigCallWithRadioType(
                    self,
                    outHandle,
                    ownerIndex,
                    kRadioTypeVipBodyFound,
                    arg5);
            }
        }
    }

    if (g_OrigCallWithRadioType)
        return g_OrigCallWithRadioType(self, outHandle, ownerIndex, radioType, arg5);

    return outHandle;
}


void VIPRadio_ClearForReload()
{
    ClearFoundBodies();
}


static void* Install_StepRadio_Hook(std::uintptr_t address, const char* name, const std::uint8_t* expected, std::size_t size,
                                    StepRadio_t detour, StepRadio_t* original)
{
    void* target = ResolveGameAddress(address);
    if (!target)
    {
        LogError("VIPRadio: no %s address for this build, its VIP body-found lines are off", name);
        return nullptr;
    }

    const HookEntry entry = CheckHookEntry(target, expected, size);
    if (entry == HookEntry::Unknown)
    {
        char bytes[128] = {};
        DescribeFunctionEntry(target, bytes, sizeof(bytes));
        LogError("VIPRadio: %s entry differs (%s), its VIP body-found lines are off", name, bytes);
        return nullptr;
    }
    if (entry == HookEntry::ForeignHook)
    {
        char what[96] = {};
        sprintf_s(what, "VIPRadio: %s", name);
        LogHookedOnTop(what, target);
    }

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original)))
    {
        LogError("VIPRadio: %s hook failed, its VIP body-found lines are off", name);
        return nullptr;
    }
    return target;
}


bool Install_VIPRadio_Hook()
{
    g_CallImpl = reinterpret_cast<CallImpl_t>(ResolveGameAddress(gAddr.CallImpl));

    void* requestCorpseTarget = ResolveGameAddress(gAddr.RequestCorpse);
    void* callWithRadioTypeTarget = ResolveGameAddress(gAddr.CallWithRadioType);

    if (!g_CallImpl || !requestCorpseTarget || !callWithRadioTypeTarget)
    {
        LogError("VIPRadio: addresses missing for this build, VIP body-found radio off");
        return false;
    }

    bool ok = true;

    ok = ok && CreateAndEnableHook(
        requestCorpseTarget,
        reinterpret_cast<void*>(&hkRequestCorpse),
        reinterpret_cast<void**>(&g_OrigRequestCorpse));

    ok = ok && CreateAndEnableHook(
        callWithRadioTypeTarget,
        reinterpret_cast<void*>(&hkCallWithRadioType),
        reinterpret_cast<void**>(&g_OrigCallWithRadioType));

    if (!ok)
    {
        LogError("VIPRadio: hooks failed, VIP body-found radio off");
        return false;
    }

    g_FootStepRadioTarget = Install_StepRadio_Hook(gAddr.NoticeDownComradeAiImpl_StepRadio, "NoticeDownComradeAiImpl::StepRadio",
        kFootStepRadioEntry, sizeof(kFootStepRadioEntry), &hkFootStepRadio, &g_OrigFootStepRadio);
    g_VehicleStepRadioTarget = Install_StepRadio_Hook(gAddr.VehicleNoticeDownComradeAiImpl_StepRadio, "VehicleNoticeDownComradeAiImpl::StepRadio",
        kVehicleStepRadioEntry, sizeof(kVehicleStepRadioEntry), &hkVehicleStepRadio, &g_OrigVehicleStepRadio);
    return true;
}


bool Uninstall_VIPRadio_Hook()
{
    if (g_FootStepRadioTarget)
    {
        DisableAndRemoveHook(g_FootStepRadioTarget);
        g_FootStepRadioTarget = nullptr;
    }
    if (g_VehicleStepRadioTarget)
    {
        DisableAndRemoveHook(g_VehicleStepRadioTarget);
        g_VehicleStepRadioTarget = nullptr;
    }
    DisableAndRemoveHook(ResolveGameAddress(gAddr.RequestCorpse));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.CallWithRadioType));

    g_OrigFootStepRadio = nullptr;
    g_OrigVehicleStepRadio = nullptr;
    g_OrigRequestCorpse = nullptr;
    g_OrigCallWithRadioType = nullptr;
    g_CallImpl = nullptr;

    ClearFoundBodies();
    return true;
}
