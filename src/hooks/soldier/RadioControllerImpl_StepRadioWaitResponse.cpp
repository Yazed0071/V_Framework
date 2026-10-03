#include "pch.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"

#include <Windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using MemberRadio = CpRadioResponse::MemberRadio;

    using StepFunc_t             = void(__fastcall*)(std::uint8_t* radioController, std::uint32_t cp, std::int32_t proc);
    using RequestRadioByMember_t = std::uint16_t(__fastcall*)(std::uint8_t* cpController, std::uint32_t client,
                                                              std::uint32_t member, std::uint32_t radioType,
                                                              std::uint32_t label);
    using SetRadioMemberReady_t  = void(__fastcall*)(std::uint8_t* cpController, std::uint32_t client,
                                                     std::uint32_t member, std::uint16_t handle, bool ready);
    using ReleaseRadioByMember_t = void(__fastcall*)(std::uint8_t* cpController, std::uint32_t client,
                                                     std::uint32_t member, std::uint16_t handle);
    using UpdateMemberRadio_t    = void(__fastcall*)(std::uint8_t* cpController, std::uint32_t client, std::uint32_t member);
    using FinishRadio_t          = void(__fastcall*)(std::uint8_t* radioService, std::uint32_t cp, std::uint16_t handle);
    using RequestRadioByCp_t     = std::uint16_t(__fastcall*)(std::uint8_t* radioService, std::uint32_t cp,
                                                              std::uint32_t radioType, std::uint32_t member,
                                                              std::uint32_t label);
    using ConvertSpeechLabel_t   = std::uint8_t(__fastcall*)(std::uint32_t label);
    using SpeechStop_t           = void(__fastcall*)(std::uint8_t* speechController, std::uint16_t handle);

    constexpr std::int32_t kProcEnter   = 0;
    constexpr std::int32_t kProcExit    = 1;
    constexpr std::int32_t kProcUpdate  = 3;
    constexpr std::int32_t kProcReenter = 4;

    constexpr std::size_t  kControllerService = 0x58;
    constexpr std::size_t  kControllerSteps   = 0x60;   // one per CP
    constexpr std::size_t  kStepSize          = 0x10;
    constexpr std::size_t  kStepHandle        = 0x00;
    constexpr std::size_t  kStepGameObject    = 0x02;   // the member on the radio, none when the CP calls out itself
    constexpr std::size_t  kStepRadioType     = 0x0A;
    constexpr std::size_t  kStepWaitTime      = 0x04;
    constexpr std::size_t  kStepMember        = 0x08;
    constexpr std::size_t  kStepNext          = 0x0C;
    constexpr std::uint8_t kStepIdle          = 0;
    constexpr std::uint8_t kStepRadio         = 1;
    constexpr std::uint8_t kStepNoResponse    = 3;
    constexpr std::uint8_t kStepWaitResponse  = 4;

    constexpr std::size_t  kServiceAction        = 0x10;
    constexpr std::size_t  kServiceRequests      = 0x38;
    constexpr std::size_t  kServiceFrameTime     = 0x158;
    constexpr std::size_t  kServiceCpCount       = 0x160;
    constexpr std::size_t  kRequestSize          = 0x18;
    constexpr std::size_t  kRequestActiveHandle  = 0x0C;
    constexpr std::size_t  kActionStates         = 0x08;   // two bytes per CP, the first is the state
    constexpr std::size_t  kActionRadios         = 0x70;   // eight bytes per CP, the speech playing at +6
    constexpr std::size_t  kActionRadioSize      = 0x08;
    constexpr std::size_t  kActionRadioSpeech    = 0x06;
    constexpr std::uint8_t kActionWaitForMember  = 1;
    constexpr std::uint8_t kActionEndFlags       = 0x07;   // the second status byte: StepRadio ends quietly on any of these
    constexpr std::size_t  kActionSpeechHandler  = 0x60;   // the CP's RadioSpeechHandler
    constexpr std::size_t  kSpeechController     = 0x08;
    constexpr std::size_t  kSpeechStop           = 4;      // SpeechController vtable slot, Stop(handle) only flags the call to wind down
    constexpr std::uint8_t kActionRadio          = 2;

    constexpr std::ptrdiff_t kServiceToCpController = -0x18;   // two interfaces of the same CommandPost2Impl
    constexpr std::size_t  kCpControllerMemberTotal = 0x174;
    constexpr std::size_t  kCpControllerMembers     = 0x58;
    constexpr std::size_t  kMemberSize              = 0x18;
    constexpr std::size_t  kMemberGameObjectId      = 0x10;
    constexpr std::size_t  kMemberCp                = 0x12;
    constexpr std::size_t  kCpControllerMemberRadio = 0x60;
    constexpr std::size_t  kMemberRadioSize         = 0x0C;
    constexpr std::size_t  kMemberRadioBound        = 0x04;
    constexpr std::size_t  kMemberRadioOwned        = 0x06;
    constexpr std::size_t  kMemberRadioState        = 0x09;
    constexpr std::uint8_t kMemberRadioAccepted     = 2;
    constexpr std::size_t  kCpControllerClients     = 0x1F0;   // soldiers, helis... each with its own run of members
    constexpr std::size_t  kCpControllerClientCount = 0x324;
    constexpr std::size_t  kClientSize              = 0x48;
    constexpr std::size_t  kClientFirstMember       = 0x40;
    constexpr std::size_t  kClientMemberCount       = 0x42;

    constexpr std::uint16_t kNoHandle          = 0xFFFF;
    constexpr std::uint32_t kNoMember          = 0xFFFF;
    constexpr std::uint32_t kAnyLabelRadio     = 0x5E;         // says the label as given, without the CP's distance check
    constexpr std::uint32_t kUnknownLabelRadio = 0x5F;         // the game's answer for a label that means nothing to the CP
    constexpr std::uint32_t kCoercedAnswer     = 0x4FE17F4Du;   // CPR0320ENE
    constexpr std::uint32_t kStandDown         = 0xCB4981B8u;   // CPR0321CP
    constexpr std::uint32_t kSitrepRadio       = 0x4A;          // the evasion CP's "SITREP? ... Do you copy?" call (CPR0311); once it ends the CP drops to caution
    constexpr std::uint16_t kSoldierObjectType = 2;
    constexpr std::uint16_t kSoldierIndexMask  = 0x1FF;
    constexpr std::size_t   kMostRequests      = 64;

    constexpr float kLongestOrder   = 3.0f;   // answers even if Snake's line never reports finished
    constexpr float kAbandonedOrder = 8.0f;
    constexpr float kStuckAnswer    = 1.0f;
    constexpr float kExpiredAnswer  = 6.0f;
    constexpr float kExpiredRequest = 10.0f;

    struct Window
    {
        std::uint32_t cp           = 0;
        std::uint16_t member       = 0;
        std::uint16_t gameObjectId = 0;
        bool          sitrep       = false;
    };

    struct Order
    {
        bool          active  = false;
        bool          ready   = false;
        bool          sitrep      = false;
        bool          callStopped = false;
        bool          callDropped = false;   // the call ended the normal way, so its CP has already dropped to caution
        float         winding     = 0.0f;
        std::uint32_t serial = 0;
        std::uint32_t cp     = 0;
        std::uint16_t member = 0;
        float         waited = 0.0f;
    };

    struct Request
    {
        std::uint16_t gameObjectId = 0;
        MemberRadio   radio;
        bool          resolved     = false;
        std::uint32_t cp           = 0;
        std::uint16_t member       = 0;
        float         waited       = 0.0f;
    };

    struct Answer
    {
        std::uint32_t cp      = 0;
        std::uint32_t client  = 0;
        std::uint32_t local   = 0;
        std::uint16_t member  = 0;
        std::uint16_t handle  = 0;
        std::uint32_t reply   = 0;
        std::uint32_t replyType = 0;   // 0: what the reply label means
        std::uint16_t soldier = 0;
        bool          sitrep  = false;
        bool          spoken  = false;
        bool          down    = false;
        bool          cut     = false;
        float         stuck   = 0.0f;
        float         pending = 0.0f;
    };

    std::mutex           g_Mutex;
    std::vector<Window>  g_Windows;
    std::vector<Request> g_Requests;
    std::vector<Answer>  g_Answers;   // at most one per CP
    Order                g_Order;
    std::uint32_t        g_LastSerial = 0;
    std::vector<std::uint32_t> g_SitrepReplays;   // CPs whose cut SITREP call has to run again, or they'd wait in evasion forever

    StepFunc_t             g_OrigStepIdle         = nullptr;
    StepFunc_t             g_OrigStepRadio        = nullptr;
    StepFunc_t             g_OrigNoResponse       = nullptr;
    StepFunc_t             g_OrigWaitResponse     = nullptr;
    RequestRadioByMember_t g_RequestRadioByMember = nullptr;
    SetRadioMemberReady_t  g_SetRadioMemberReady  = nullptr;
    ReleaseRadioByMember_t g_ReleaseRadioByMember = nullptr;
    UpdateMemberRadio_t    g_UpdateMemberRadio    = nullptr;
    FinishRadio_t          g_FinishRadio          = nullptr;
    RequestRadioByCp_t     g_RequestRadioByCp     = nullptr;
    ConvertSpeechLabel_t   g_ConvertSpeechLabel   = nullptr;
    void*                  g_StepIdleTarget       = nullptr;
    void*                  g_StepRadioTarget      = nullptr;
    void*                  g_NoResponseTarget     = nullptr;
    void*                  g_WaitResponseTarget   = nullptr;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    std::uint8_t* StepOf(std::uint8_t* controller, std::uint32_t cp)
    {
        return At<std::uint8_t*>(controller, kControllerSteps) + cp * kStepSize;
    }

    std::uint8_t* ServiceOf(std::uint8_t* controller)
    {
        return At<std::uint8_t*>(controller, kControllerService);
    }

    std::uint8_t* CpControllerOf(std::uint8_t* controller)
    {
        return ServiceOf(controller) + kServiceToCpController;
    }

    float FrameTimeOf(std::uint8_t* controller)
    {
        return At<float>(ServiceOf(controller), kServiceFrameTime);
    }

    std::uint8_t* ActionOf(std::uint8_t* controller)
    {
        return At<std::uint8_t*>(ServiceOf(controller), kServiceAction);
    }

    std::uint8_t ActionStateOf(std::uint8_t* controller, std::uint32_t cp)
    {
        return At<std::uint8_t*>(ActionOf(controller), kActionStates)[cp * 2];
    }

    std::uint16_t ActionSpeechOf(std::uint8_t* controller, std::uint32_t cp)
    {
        return At<std::uint16_t>(At<std::uint8_t*>(ActionOf(controller), kActionRadios), cp * kActionRadioSize + kActionRadioSpeech);
    }

    std::uint16_t ActiveHandleOf(std::uint8_t* controller, std::uint32_t cp)
    {
        return At<std::uint16_t>(At<std::uint8_t*>(ServiceOf(controller), kServiceRequests),
                                 cp * kRequestSize + kRequestActiveHandle);
    }

    bool IsMember(std::uint8_t* cpController, std::uint16_t member)
    {
        return member < At<std::uint32_t>(cpController, kCpControllerMemberTotal);
    }

    std::uint8_t* MemberOf(std::uint8_t* cpController, std::uint16_t member)
    {
        return At<std::uint8_t*>(cpController, kCpControllerMembers) + member * kMemberSize;
    }

    std::uint8_t* MemberRadioOf(std::uint8_t* cpController, std::uint16_t member)
    {
        return At<std::uint8_t*>(cpController, kCpControllerMemberRadio) + member * kMemberRadioSize;
    }

    bool FindClientOf(std::uint8_t* cpController, std::uint16_t member, std::uint32_t& client, std::uint32_t& local)
    {
        std::uint8_t* clients = At<std::uint8_t*>(cpController, kCpControllerClients);
        const std::uint32_t count = At<std::uint32_t>(cpController, kCpControllerClientCount);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            std::uint8_t* record = clients + index * kClientSize;
            const std::uint16_t first = At<std::uint16_t>(record, kClientFirstMember);
            if (member >= first && member < first + At<std::uint16_t>(record, kClientMemberCount))
            {
                client = index;
                local  = member - first;
                return true;
            }
        }
        return false;
    }

    bool FindMemberOf(std::uint8_t* controller, std::uint16_t gameObjectId, std::uint16_t& member, std::uint32_t& cp)
    {
        std::uint8_t* cpController = CpControllerOf(controller);
        const std::uint32_t total = At<std::uint32_t>(cpController, kCpControllerMemberTotal);
        for (std::uint32_t index = 0; index < total; ++index)
        {
            std::uint8_t* entry = MemberOf(cpController, static_cast<std::uint16_t>(index));
            if (At<std::uint16_t>(entry, kMemberGameObjectId) == gameObjectId)
            {
                member = static_cast<std::uint16_t>(index);
                cp     = At<std::uint8_t>(entry, kMemberCp);
                return cp < At<std::uint32_t>(ServiceOf(controller), kServiceCpCount);
            }
        }
        return false;
    }

    Answer* FindAnswerOf(std::uint32_t cp)
    {
        for (Answer& answer : g_Answers)
            if (answer.cp == cp)
                return &answer;
        return nullptr;
    }

    void ForgetAnswerOf(std::uint32_t cp)
    {
        for (auto it = g_Answers.begin(); it != g_Answers.end(); ++it)
        {
            if (it->cp == cp)
            {
                g_Answers.erase(it);
                return;
            }
        }
    }

    bool HasRequestFor(std::uint16_t gameObjectId)
    {
        for (const Request& request : g_Requests)
            if (request.gameObjectId == gameObjectId)
                return true;
        return false;
    }

    const Window* FindWindowOf(std::uint16_t gameObjectId)
    {
        for (const Window& window : g_Windows)
            if (window.gameObjectId == gameObjectId)
                return &window;
        return nullptr;
    }

    void EraseWindowsOf(std::uint32_t cp, bool sitrepOnly)
    {
        g_Windows.erase(std::remove_if(g_Windows.begin(), g_Windows.end(),
            [&](const Window& window) { return window.cp == cp && (!sitrepOnly || window.sitrep); }), g_Windows.end());
    }

    void CloseWindow(std::uint32_t cp)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        EraseWindowsOf(cp, false);
        if (g_Order.active && g_Order.cp == cp)
            g_Order = Order{};
    }

    void CloseSitrepWindows(std::uint32_t cp)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        EraseWindowsOf(cp, true);
    }

    bool StopCpSpeech(std::uint8_t* controller, std::uint32_t cp)   // as the CP drops a call of its own; false until the call is on air
    {
        const std::uint16_t speech = ActionSpeechOf(controller, cp);
        std::uint8_t* handler = At<std::uint8_t*>(ActionOf(controller), kActionSpeechHandler);
        std::uint8_t* speechController = handler ? At<std::uint8_t*>(handler, kSpeechController) : nullptr;
        if (speech == kNoHandle || !speechController)
            return false;
        const auto stop = reinterpret_cast<SpeechStop_t>((*reinterpret_cast<void***>(speechController))[kSpeechStop]);
        stop(speechController, speech);
        return true;
    }

    bool IsSitrepCall(std::uint8_t* controller, std::uint32_t cp)
    {
        std::uint8_t* step = StepOf(controller, cp);
        return At<std::uint8_t>(step, kStepRadioType) == kSitrepRadio && At<std::uint16_t>(step, kStepGameObject) == kNoHandle;
    }

    void OpenSitrepWindows(std::uint8_t* controller, std::uint32_t cp)   // the call is to nobody in particular, so any of his soldiers can answer
    {
        std::uint8_t* cpController = CpControllerOf(controller);
        const std::uint32_t total = At<std::uint32_t>(cpController, kCpControllerMemberTotal);

        std::lock_guard<std::mutex> lock(g_Mutex);
        for (std::uint32_t index = 0; index < total; ++index)
        {
            std::uint8_t* entry = MemberOf(cpController, static_cast<std::uint16_t>(index));
            const std::uint16_t gameObjectId = At<std::uint16_t>(entry, kMemberGameObjectId);
            if (At<std::uint8_t>(entry, kMemberCp) == cp && gameObjectId != kNoHandle && !FindWindowOf(gameObjectId))
                g_Windows.push_back({ cp, static_cast<std::uint16_t>(index), gameObjectId, true });
        }
    }

    void QueueSitrepReplay(std::uint32_t cp)
    {
        if (std::find(g_SitrepReplays.begin(), g_SitrepReplays.end(), cp) == g_SitrepReplays.end())
            g_SitrepReplays.push_back(cp);
    }

    void RunSitrepReplay(std::uint8_t* controller, std::uint32_t cp)
    {
        const auto it = std::find(g_SitrepReplays.begin(), g_SitrepReplays.end(), cp);
        if (it == g_SitrepReplays.end())
            return;
        g_SitrepReplays.erase(it);
        g_RequestRadioByCp(ServiceOf(controller), cp, kSitrepRadio, kNoMember, 0);   // asked the way the evasion CP asks it
    }

    void OpenWindow(std::uint8_t* controller, std::uint32_t cp, std::uint16_t member)
    {
        std::uint8_t* cpController = CpControllerOf(controller);
        if (!IsMember(cpController, member))
        {
            CloseWindow(cp);
            return;
        }
        const std::uint16_t gameObjectId = At<std::uint16_t>(MemberOf(cpController, member), kMemberGameObjectId);

        std::lock_guard<std::mutex> lock(g_Mutex);
        if (g_Order.active && g_Order.cp == cp && g_Order.member != member)
            g_Order = Order{};

        EraseWindowsOf(cp, false);
        g_Windows.push_back({ cp, member, gameObjectId, false });
    }

    void DropAnswer(std::uint8_t* controller, const Answer& answer, const char* why)
    {
        std::uint8_t* cpController = CpControllerOf(controller);
        if (ActiveHandleOf(controller, answer.cp) == answer.handle)
            g_FinishRadio(ServiceOf(controller), answer.cp, answer.handle);
        if (At<std::uint16_t>(MemberRadioOf(cpController, answer.member), kMemberRadioBound) == answer.handle)
            g_SetRadioMemberReady(cpController, answer.client, answer.local, answer.handle, false);
        g_ReleaseRadioByMember(cpController, answer.client, answer.local, answer.handle);
        g_UpdateMemberRadio(cpController, answer.client, answer.local);   // unbinds him even if he's no longer updated

        if (why)
            LogWarn("CpRadioResponse: cp %u %s, member %u's radio dropped", answer.cp, why, answer.member);
        if (answer.sitrep)
            QueueSitrepReplay(answer.cp);
        ForgetAnswerOf(answer.cp);
    }

    // What a soldier's own radio does when he's hit mid-call: his CP hears the line die and calls him ("what happened, come in").
    void CutAnswer(std::uint8_t* controller, Answer& answer)
    {
        std::uint8_t* cpController = CpControllerOf(controller);
        g_ReleaseRadioByMember(cpController, answer.client, answer.local, answer.handle);
        g_UpdateMemberRadio(cpController, answer.client, answer.local);
        answer.cut = true;
    }

    std::uint32_t RadioTypeOf(std::uint32_t label)   // as CallRadio does, so the CP acts on what the label means (CPR0080 stands it down)
    {
        const std::uint32_t type = g_ConvertSpeechLabel ? g_ConvertSpeechLabel(label) : kUnknownLabelRadio;
        return type == kUnknownLabelRadio ? kAnyLabelRadio : type;
    }

    bool PostMemberRadio(std::uint8_t* controller, std::uint32_t cp, std::uint16_t member, const MemberRadio& radio,
                         std::uint32_t replyType = 0, bool sitrep = false)
    {
        std::uint8_t* cpController = CpControllerOf(controller);
        std::uint32_t client = 0;
        std::uint32_t local  = 0;
        if (!IsMember(cpController, member) || At<std::uint8_t>(MemberOf(cpController, member), kMemberCp) != cp
            || !FindClientOf(cpController, member, client, local))
        {
            LogWarn("CpRadioResponse: member %u isn't on cp %u's radio, he can't talk to it", member, cp);
            return false;
        }

        g_RequestRadioByMember(cpController, client, local, RadioTypeOf(radio.label), radio.label);

        std::uint8_t* entry = MemberRadioOf(cpController, member);
        const std::uint16_t handle = At<std::uint16_t>(entry, kMemberRadioOwned);
        if (At<std::uint8_t>(entry, kMemberRadioState) != kMemberRadioAccepted || At<std::uint16_t>(entry, kMemberRadioBound) != handle)
        {
            LogWarn("CpRadioResponse: cp %u turned down member %u's radio", cp, member);
            g_ReleaseRadioByMember(cpController, client, local, handle);
            g_UpdateMemberRadio(cpController, client, local);
            return false;
        }

        g_SetRadioMemberReady(cpController, client, local, handle, true);   // a held soldier never gets to his radio pose, so say he's in it

        Answer answer;
        answer.cp     = cp;
        answer.client = client;
        answer.local  = local;
        answer.member = member;
        answer.handle = handle;
        answer.reply  = radio.reply;
        answer.replyType = replyType;
        answer.sitrep = sitrep;
        answer.soldier = At<std::uint16_t>(MemberOf(cpController, member), kMemberGameObjectId);
        g_Answers.push_back(answer);
        return true;
    }

    void TickAnswer(std::uint8_t* controller, std::uint32_t cp, std::uint8_t step, float frameTime)
    {
        Answer* answer = FindAnswerOf(cp);
        if (!answer || answer->cut)
            return;

        const bool onAir = step == kStepRadio && At<std::uint16_t>(StepOf(controller, cp), kStepHandle) == answer->handle;
        if (answer->down)
        {
            if (onAir && !answer->sitrep)
                CutAnswer(controller, *answer);
            else
                DropAnswer(controller, *answer, nullptr);   // before his CP heard him; a SITREP answer lost this way lets the CP ask again
            return;
        }

        if (onAir)
        {
            const std::uint8_t state = ActionStateOf(controller, cp);
            if (state == kActionRadio && ActionSpeechOf(controller, cp) != kNoHandle)
                answer->spoken = true;

            if (state != kActionWaitForMember)
            {
                answer->stuck = 0.0f;
                return;
            }
            answer->stuck += frameTime;
            if (answer->stuck > kStuckAnswer)
                DropAnswer(controller, *answer, "never put the radio on air");
            return;
        }

        if (step == kStepWaitResponse)
            At<float>(StepOf(controller, cp), kStepWaitTime) = 0.0f;   // a member is about to answer, the CP keeps waiting

        std::uint8_t* entry = MemberRadioOf(CpControllerOf(controller), answer->member);
        if (At<std::uint16_t>(entry, kMemberRadioBound) != answer->handle || At<std::uint8_t>(entry, kMemberRadioState) != kMemberRadioAccepted)
        {
            DropAnswer(controller, *answer, "turned the radio down");
            return;
        }
        answer->pending += frameTime;
        if (answer->pending > kExpiredAnswer)
            DropAnswer(controller, *answer, "kept its radio busy");
    }

    void TickOrder(std::uint8_t* controller, std::uint32_t cp, std::uint8_t step, float frameTime)
    {
        if (!g_Order.active || g_Order.cp != cp)
            return;

        if (g_Order.sitrep)
        {
            if (!g_Order.callStopped)
            {
                if (step != kStepRadio || !IsSitrepCall(controller, cp))
                {
                    g_Order = Order{};   // the call ended before the lie got to it, and the CP went on as usual
                    return;
                }
                g_Order.callStopped = StopCpSpeech(controller, cp);   // fades out after "Gimme a SITREP"
            }
            if (step == kStepRadio)
            {
                g_Order.winding += frameTime;
                if (g_Order.winding > kAbandonedOrder)
                {
                    LogWarn("CpRadioResponse: cp %u's call didn't stop, member %u's answer is dropped", cp, g_Order.member);
                    g_Order = Order{};   // its call ends as usual
                }
                return;
            }
        }

        const bool replayCall = g_Order.sitrep && !g_Order.callDropped;   // its CP still has to drop to caution
        g_Order.waited += frameTime;
        if (g_Order.waited > kAbandonedOrder)
        {
            LogWarn("CpRadioResponse: cp %u never heard member %u's answer, it gives up on him as usual", cp, g_Order.member);
            if (replayCall)
                QueueSitrepReplay(cp);
            g_Order = Order{};
            return;
        }

        if (step == kStepWaitResponse)
            At<float>(StepOf(controller, cp), kStepWaitTime) = 0.0f;   // the CP keeps waiting while Snake gets him to talk
        if (FindAnswerOf(cp) || (!g_Order.ready && g_Order.waited < kLongestOrder) || (g_Order.sitrep && step != kStepIdle))
            return;

        MemberRadio lie;
        lie.label = kCoercedAnswer;
        lie.reply = kStandDown;
        if (!PostMemberRadio(controller, cp, g_Order.member, lie, replayCall ? kSitrepRadio : 0, replayCall) && replayCall)
            QueueSitrepReplay(cp);   // with kSitrepRadio his CP's reply closes the call it started, so it still drops to caution
        g_Order = Order{};   // one answer per order; his answer now keeps the CP waiting
    }

    void TickRequests(std::uint8_t* controller, std::uint32_t cp, std::uint8_t step, float frameTime)
    {
        for (auto it = g_Requests.begin(); it != g_Requests.end();)
        {
            Request& request = *it;
            if (!request.resolved)
            {
                if (!FindMemberOf(controller, request.gameObjectId, request.member, request.cp))
                {
                    LogWarn("LieToRadio: 0x%04X isn't on any CP's radio, ignored", request.gameObjectId);
                    it = g_Requests.erase(it);
                    continue;
                }
                request.resolved = true;
            }

            if (request.cp != cp)
            {
                ++it;
                continue;
            }

            const bool ordered = g_Order.active && g_Order.cp == cp;   // the player's lie goes first
            if ((step == kStepIdle || step == kStepWaitResponse) && !FindAnswerOf(cp) && !ordered)
            {
                PostMemberRadio(controller, cp, request.member, request.radio);
                it = g_Requests.erase(it);
                continue;
            }

            request.waited += frameTime;
            if (request.waited > kExpiredRequest)
            {
                LogWarn("LieToRadio: cp %u's radio stayed busy, 0x%04X didn't get on it", cp, request.gameObjectId);
                it = g_Requests.erase(it);
                continue;
            }
            ++it;
        }
    }

    void Tick(std::uint8_t* controller, std::uint32_t cp, std::uint8_t step)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (g_Answers.empty() && g_Requests.empty() && !g_Order.active && g_SitrepReplays.empty())
            return;

        const float frameTime = FrameTimeOf(controller);
        TickAnswer(controller, cp, step, frameTime);
        if (step == kStepWaitResponse || g_Order.sitrep)
            TickOrder(controller, cp, step, frameTime);
        TickRequests(controller, cp, step, frameTime);
        RunSitrepReplay(controller, cp);
    }

    void EndAnswer(std::uint8_t* controller, std::uint32_t cp)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        Answer* answer = FindAnswerOf(cp);
        std::uint8_t* step = StepOf(controller, cp);
        if (!answer || At<std::uint16_t>(step, kStepHandle) != answer->handle)
            return;

        if (!answer->cut)
        {
            g_ReleaseRadioByMember(CpControllerOf(controller), answer->client, answer->local, answer->handle);
            if (!answer->spoken || At<std::uint8_t>(step, kStepNext) != kStepIdle)
            {
                LogWarn("CpRadioResponse: member %u's radio to cp %u ended without his voice, no reply", answer->member, cp);
                if (answer->sitrep)
                    QueueSitrepReplay(cp);
            }
            else if (answer->reply)
                g_RequestRadioByCp(ServiceOf(controller), cp, answer->replyType ? answer->replyType : RadioTypeOf(answer->reply),
                                   kNoMember, answer->reply);
        }
        ForgetAnswerOf(cp);
    }

    void __fastcall hk_StepIdle(std::uint8_t* controller, std::uint32_t cp, std::int32_t proc)
    {
        if (proc == kProcUpdate)
            Tick(controller, cp, kStepIdle);
        g_OrigStepIdle(controller, cp, proc);
    }

    void NoteStoppedCallEnd(std::uint8_t* controller, std::uint32_t cp)   // read before StepRadio acts on the same bytes
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (!g_Order.active || !g_Order.sitrep || g_Order.cp != cp || !g_Order.callStopped || g_Order.callDropped)
            return;
        const std::uint8_t* status = At<std::uint8_t*>(ActionOf(controller), kActionStates) + cp * 2;
        if (status[0] == 0 && (status[1] & kActionEndFlags) == 0)
            g_Order.callDropped = true;   // StepRadio ends it the normal way and sends its type, so the CP drops to caution now
    }

    void __fastcall hk_StepRadio(std::uint8_t* controller, std::uint32_t cp, std::int32_t proc)
    {
        if (proc == kProcUpdate)
        {
            Tick(controller, cp, kStepRadio);
            NoteStoppedCallEnd(controller, cp);
        }

        g_OrigStepRadio(controller, cp, proc);

        if ((proc == kProcEnter || proc == kProcReenter) && IsSitrepCall(controller, cp))
            OpenSitrepWindows(controller, cp);
        else if (proc == kProcExit)
        {
            EndAnswer(controller, cp);
            CloseSitrepWindows(cp);
        }
    }

    void __fastcall hk_StepRadioNoResponse(std::uint8_t* controller, std::uint32_t cp, std::int32_t proc)
    {
        if (proc == kProcUpdate)
            Tick(controller, cp, kStepNoResponse);

        const std::uint16_t cutMember = At<std::uint16_t>(StepOf(controller, cp), kStepMember);   // enter rewrites it from the CP's queue
        g_OrigNoResponse(controller, cp, proc);

        if (proc == kProcEnter || proc == kProcReenter)
            OpenWindow(controller, cp, cutMember);
        else if (proc == kProcExit && At<std::uint8_t>(StepOf(controller, cp), kStepNext) != kStepWaitResponse)
            CloseWindow(cp);
    }

    void __fastcall hk_StepRadioWaitResponse(std::uint8_t* controller, std::uint32_t cp, std::int32_t proc)
    {
        if (proc == kProcUpdate)
            Tick(controller, cp, kStepWaitResponse);

        g_OrigWaitResponse(controller, cp, proc);

        if (proc == kProcExit)
            CloseWindow(cp);
    }

    bool Hook(std::uintptr_t address, void* detour, void** original, void*& target, const char* name)
    {
        void* resolved = ResolveGameAddress(address);
        if (!CreateAndEnableHook(resolved, detour, original))
        {
            LogError("CpRadioResponse: %s hook failed at %p, no member radio (Lie to them, LieToRadio)", name, resolved);
            return false;
        }
        target = resolved;
        return true;
    }

    void RemoveHook(void*& target)
    {
        if (target)
            DisableAndRemoveHook(target);
        target = nullptr;
    }

    template <typename Fn>
    Fn Resolve(std::uintptr_t address)
    {
        return reinterpret_cast<Fn>(ResolveGameAddress(address));
    }
}

bool CpRadioResponse::IsAvailable()
{
    return g_WaitResponseTarget != nullptr;
}

bool CpRadioResponse::IsWaitingOn(std::uint16_t soldierId, bool& sitrep)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    const Window* window = FindWindowOf(soldierId);
    sitrep = window && window->sitrep;
    return window && !g_Order.active && !FindAnswerOf(window->cp) && !HasRequestFor(soldierId);
}

std::uint32_t CpRadioResponse::OrderFalseResponse(std::uint16_t soldierId)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    const Window* window = FindWindowOf(soldierId);
    if (!window || !g_WaitResponseTarget || g_Order.active || FindAnswerOf(window->cp))
        return 0;

    g_Order        = Order{};
    g_Order.active = true;
    if (++g_LastSerial == 0)
        ++g_LastSerial;
    g_Order.serial = g_LastSerial;
    g_Order.cp     = window->cp;
    g_Order.member = window->member;
    g_Order.sitrep = window->sitrep;
    return g_Order.serial;
}

void CpRadioResponse::LetSoldierAnswer(std::uint32_t order)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    if (g_Order.active && g_Order.serial == order)
        g_Order.ready = true;
}

bool CpRadioResponse::RequestMemberRadio(std::uint16_t soldierId, const MemberRadio& radio)
{
    if (!g_WaitResponseTarget || (soldierId >> 9) != kSoldierObjectType || radio.label == 0)
        return false;

    std::lock_guard<std::mutex> lock(g_Mutex);
    for (Request& request : g_Requests)
    {
        if (request.gameObjectId == soldierId)
        {
            request = Request{};
            request.gameObjectId = soldierId;
            request.radio        = radio;
            return true;
        }
    }
    if (g_Requests.size() >= kMostRequests)
        return false;

    Request request;
    request.gameObjectId = soldierId;
    request.radio        = radio;
    g_Requests.push_back(request);
    return true;
}

void CpRadioResponse::OnSoldierDown(std::uint16_t soldierIndex)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    for (Answer& answer : g_Answers)
        if ((answer.soldier & kSoldierIndexMask) == soldierIndex)
            answer.down = true;

    for (auto it = g_Requests.begin(); it != g_Requests.end();)
        it = (it->gameObjectId & kSoldierIndexMask) == soldierIndex ? g_Requests.erase(it) : it + 1;

    for (const Window& window : g_Windows)
    {
        if ((window.gameObjectId & kSoldierIndexMask) != soldierIndex || !g_Order.active
            || g_Order.cp != window.cp || g_Order.member != window.member)
            continue;
        if (g_Order.sitrep && g_Order.callStopped && !g_Order.callDropped)
            QueueSitrepReplay(window.cp);
        g_Order = Order{};   // he can't give the answer Snake ordered; his CP's wait runs out as usual
        break;
    }
}

void CpRadioResponse::Clear()
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_Windows.clear();
    g_Requests.clear();
    g_Answers.clear();
    g_SitrepReplays.clear();
    g_Order = Order{};
}

bool Install_RadioControllerImpl_StepRadioWaitResponse_Hooks()
{
    if (g_WaitResponseTarget)
        return true;

    if (!gAddr.RadioControllerImpl_StepIdle || !gAddr.RadioControllerImpl_StepRadio
        || !gAddr.RadioControllerImpl_StepRadioNoResponse || !gAddr.RadioControllerImpl_StepRadioWaitResponse
        || !gAddr.CommandPost2Impl_Controller_RequestRadioByMember || !gAddr.CommandPost2Impl_Controller_SetRadioMemberReady
        || !gAddr.CommandPost2Impl_Controller_ReleaseRadioByMember || !gAddr.CommandPost2Impl_Controller_UpdateRadio
        || !gAddr.CommandPost2Impl_FinishRadio || !gAddr.CommandPost2Impl_RequestRadioByCp)
    {
        LogError("CpRadioResponse: radio addresses missing for %s, no member radio (Lie to them, LieToRadio)",
                 GetGameBuildName(gGameBuild));
        return true;
    }

    g_RequestRadioByMember = Resolve<RequestRadioByMember_t>(gAddr.CommandPost2Impl_Controller_RequestRadioByMember);
    g_SetRadioMemberReady  = Resolve<SetRadioMemberReady_t>(gAddr.CommandPost2Impl_Controller_SetRadioMemberReady);
    g_ReleaseRadioByMember = Resolve<ReleaseRadioByMember_t>(gAddr.CommandPost2Impl_Controller_ReleaseRadioByMember);
    g_UpdateMemberRadio    = Resolve<UpdateMemberRadio_t>(gAddr.CommandPost2Impl_Controller_UpdateRadio);
    g_FinishRadio          = Resolve<FinishRadio_t>(gAddr.CommandPost2Impl_FinishRadio);
    g_RequestRadioByCp     = Resolve<RequestRadioByCp_t>(gAddr.CommandPost2Impl_RequestRadioByCp);
    g_ConvertSpeechLabel   = Resolve<ConvertSpeechLabel_t>(gAddr.CpRadioService_ConvertSpeechLabelToRadioType);

    const bool hooked =
        Hook(gAddr.RadioControllerImpl_StepIdle, reinterpret_cast<void*>(&hk_StepIdle),
             reinterpret_cast<void**>(&g_OrigStepIdle), g_StepIdleTarget, "StepIdle")
        && Hook(gAddr.RadioControllerImpl_StepRadio, reinterpret_cast<void*>(&hk_StepRadio),
                reinterpret_cast<void**>(&g_OrigStepRadio), g_StepRadioTarget, "StepRadio")
        && Hook(gAddr.RadioControllerImpl_StepRadioNoResponse, reinterpret_cast<void*>(&hk_StepRadioNoResponse),
                reinterpret_cast<void**>(&g_OrigNoResponse), g_NoResponseTarget, "StepRadioNoResponse")
        && Hook(gAddr.RadioControllerImpl_StepRadioWaitResponse, reinterpret_cast<void*>(&hk_StepRadioWaitResponse),
                reinterpret_cast<void**>(&g_OrigWaitResponse), g_WaitResponseTarget, "StepRadioWaitResponse");
    if (!hooked)
    {
        RemoveHook(g_WaitResponseTarget);
        RemoveHook(g_NoResponseTarget);
        RemoveHook(g_StepRadioTarget);
        RemoveHook(g_StepIdleTarget);
        return false;
    }
    return true;
}

void Uninstall_RadioControllerImpl_StepRadioWaitResponse_Hooks()
{
    RemoveHook(g_WaitResponseTarget);
    RemoveHook(g_NoResponseTarget);
    RemoveHook(g_StepRadioTarget);
    RemoveHook(g_StepIdleTarget);
    CpRadioResponse::Clear();
}
