#include "pch.h"

#include "RemoteMissile.h"

#include <Windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "AddressSet.h"
#include "EquipPartParams.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "log.h"
#include "../../lua/LuaApi.h"

namespace
{
    constexpr std::size_t kQuarkApplicationSystem = 0x98;
    constexpr std::size_t kAppShellSystem154 = 0x210;
    constexpr std::size_t kAppShellSystem153 = 0x208;

    constexpr std::size_t kShellVtblFire = 0x00;
    constexpr std::size_t kShellVtblFireRocketPunch = 0x58;
    constexpr std::size_t kShellVtblCancelRocketPunch = 0x60;
    constexpr std::size_t kShellVtblIsExistRocketPunch = 0x68;
    constexpr std::size_t kShellVtblOperateRocketPunch = 0x70;
    constexpr std::size_t kShellVtblGetCameraMatrix = 0x78;

    constexpr std::size_t kShellActiveIndex = 0xFC;
    constexpr std::uint32_t kShellNoIndex = 0x3F;
    constexpr std::size_t kShellSpeedState = 0x160;
    constexpr std::size_t kShellOutOfRangeTimer = 0x16C;

    constexpr std::size_t kShellImplHolder = 0x80;
    constexpr std::size_t kShellImplWorkArray = 0xE8;
    constexpr std::size_t kShellImplOriginArray = 0x70;
    constexpr std::size_t kShellImplVelocityArray = 0x78;
    constexpr std::size_t kShellImplTimeBase = 0xC8;
    constexpr std::size_t kShellImplNetFlag = 0xE0;
    constexpr std::size_t kShellExecVtblCreate = 0x08;
    constexpr std::size_t kShellSysPositions = 0x60;
    constexpr std::size_t kShellSysTotalCap = 0x48;
    constexpr std::size_t kShellSysExecCount = 0x4C;
    constexpr std::size_t kShellSysTotalShells = 0x50;
    constexpr std::size_t kShellSysPerExec = 0x54;
    constexpr std::size_t kShellSysCountBytes = 0x88;
    constexpr std::uint32_t kShellSysMaxExec = 32;
    constexpr std::size_t kShellImplPoolSize = 0x30;
    constexpr std::size_t kShellImplUsedMask = 0x98;
    constexpr std::size_t kShellWorkStride = 0x2C;
    constexpr std::size_t kShellWorkAge = 0x00;
    constexpr std::size_t kShellWorkLifetime = 0x04;
    constexpr std::size_t kShellWorkShellId = 0x14;
    constexpr std::size_t kShellWorkFlags = 0x24;
    constexpr std::size_t kShellWorkState = 0x28;
    constexpr std::uint32_t kShellWorkTerminating = 0x00040000;
    constexpr std::uint32_t kShellWorkRetire = 0x00004000;
    constexpr std::uint32_t kShellStateImpacted = 0x40000000;
    constexpr std::size_t kShellPunchFlags = 0x2B0;
    constexpr std::uint32_t kShellPunchSystemPaused = 0x1;
    constexpr std::uint32_t kShellPunchAreaArm = 0x10;
    constexpr std::uint32_t kShellPunchEffectLatch = 0x2000;
    constexpr std::uint32_t kShellWorkTrailSpawned = 0x04000000;
    constexpr std::size_t kShellCallbackObject = 0xA8;
    constexpr std::size_t kCallbackVtblEnableNoiseFilter = 0x18;
    constexpr float kShellEffectSpeedThreshold = 12.0f;
    constexpr float kTimeBaseTicksPerSecond = 299.7f;
    constexpr std::uint16_t kDescLifetimeMax = 0xFFFE;
    constexpr float kLifetimeHeadroomSeconds = 5.0f;
    constexpr float kShellAgeHold = 0.6f;
    constexpr float kShellSpeedFloor = 3.0f;
    constexpr float kShellSpeedCeiling = 16.0f;
    constexpr float kShellSpeedVanillaSpan = 13.0f;
    constexpr float kShellSpeedBoostRate = 10.0f;
    constexpr float kShellSpeedBrakeRate = 20.0f;
    constexpr float kShellSpeedDragRate = 1.0f;
    constexpr float kShellBankInvertSpeed = 29.0f;
    constexpr float kEngineTriggerEpsilon = 1.0e-4f;
    constexpr std::uint32_t kShellWorkFlightModeSteer = 0x3000;
    constexpr std::uint32_t kShellWorkLocalAuthority = 0x200000;
    constexpr std::size_t kShellExecuteSubObject = 0x08;
    constexpr std::size_t kExecuteVtblSerially = 0x00;

    constexpr std::size_t kDescDirection = 0x00;
    constexpr std::size_t kDescSpeed = 0x0C;
    constexpr std::size_t kDescPosition = 0x20;
    constexpr std::size_t kDescOwner = 0x2C;
    constexpr std::size_t kDescShellId = 0x2E;
    constexpr std::size_t kDescLifetime = 0x30;
    constexpr std::size_t kDescParent = 0x3A;
    constexpr std::size_t kDescAttack = 0x3C;
    constexpr std::size_t kDescExpiryLatch = 0x3E;
    constexpr std::size_t kDescBytes = 0x40;
    constexpr std::uint16_t kDescAttackIdMask = 0x03FF;
    constexpr std::uint16_t kDescVelocityModeMask = 0x1C00;
    constexpr std::uint16_t kDescVelocityModeDirect = 0x0400;
    constexpr std::uint16_t kDescExpiryLatchBit = 0x4000;
    constexpr float kHandoffMaxLifeSeconds = 10.0f;
    constexpr float kHandoffMinSpeed = 0.5f;
    constexpr float kHandoffMaxSpeed = 400.0f;
    constexpr std::uint8_t kDescNoParent = 0x3F;
    constexpr std::uint16_t kDescDefaultLifetime = 0xFFFF;

    constexpr std::size_t kPlayerSubsystems = 0x138;
    constexpr std::size_t kSubsystemPad = 0xB0;
    constexpr std::size_t kSubsystemCamera = 0xB8;
    constexpr std::size_t kSubsystemLocalCharaIndex = 0x218;
    constexpr std::size_t kPlayerSlotBase = 0x0C;
    constexpr std::size_t kPlayerVtblCallVoice = 0x50;
    constexpr std::uint32_t kPlayerVoicePriority = 3;
    constexpr std::int32_t kPlayerVoiceSlotLimit = 16;

    constexpr std::size_t kPadVtblTrigger = 0x78;
    constexpr std::size_t kPadVtblStickXIgnoreMask = 0x80;
    constexpr std::size_t kPadVtblStickYIgnoreMask = 0x88;
    constexpr std::size_t kPadVtblSetMaskButtons = 0xB0;
    constexpr std::size_t kPadVtblResetMaskButtons = 0xB8;
    constexpr std::size_t kPadVtblSetMaskSticks = 0xC0;
    constexpr std::size_t kPadVtblResetMaskSticks = 0xC8;
    constexpr std::size_t kPadVtblIsActionOn = 0x108;
    constexpr std::size_t kPadStickMaskResult = 0x2C;
    constexpr std::size_t kPadButtonLeaseTable = 0x2C0;
    constexpr unsigned long long kPadActionBrake = 0x5E39C68D6333ull;
    constexpr unsigned long long kPadActionBoost = 0x83089FD930D1ull;
    constexpr float kTriggerDeadzone = 0.01f;

    constexpr std::size_t kCameraVtblSetBaseRotation = 0x18;
    constexpr std::size_t kCameraBaseRotation = 0xC0;
    constexpr std::size_t kCameraManualPosition = 0x170;
    constexpr std::size_t kCameraAttachMode = 0x20C;
    constexpr std::size_t kCameraManualPitch = 0x270;
    constexpr std::size_t kCameraManualYaw = 0x274;
    constexpr std::size_t kCameraLookOffset = 0x2A8;
    constexpr std::uint32_t kCameraAttachFollowPlayer = 0;
    constexpr std::uint32_t kCameraAttachManual = 4;
    constexpr std::size_t kCameraFlags = 0x204;
    constexpr std::size_t kCameraManualSpace = 0x32C;
    constexpr std::size_t kCameraFocalLength = 0x2DC;
    constexpr std::size_t kCameraPlayerIndex = 0x380;
    constexpr std::uint32_t kCameraFlagNoManualRotation = 0x40;

    constexpr std::uint32_t kInputOwner = 0x56464D53;
    constexpr std::uint32_t kButtonMaskBlockAll = 0xFFFFFFFF;
    constexpr std::uint32_t kStickMaskMoveStick = 0x1;

    constexpr int kUndeclaredReceiverId = 1;

    struct MissileSpec
    {
        int attackId = 0;
        bool hasAttackId = false;
        float maxFlightSeconds = 20.0f;
        float maxRange = 0.0f;
        float cameraDistance = 4.0f;
        float cameraHeight = 0.0f;
        float minSpeed = 0.0f;
        float maxSpeed = 0.0f;
        std::uint32_t fireVoiceId = 0;
    };

    std::recursive_mutex g_Mutex;
    std::unordered_map<int, MissileSpec> g_SpecByEquipId;
    std::unordered_map<int, MissileSpec> g_SpecByReceiverId;

    MissileSpec g_ActiveSpec{};
    std::atomic<bool> g_Flying{ false };
    long long g_LastTickCounter = 0;
    unsigned long long g_LaunchTickMs = 0;
    bool g_CameraTaken = false;
    bool g_InputMasked = false;
    std::uint32_t g_SavedAttachMode = 0;
    std::uint32_t g_SavedManualSpace = 0;
    alignas(16) float g_SavedBaseRotation[4] = {};
    std::uint64_t g_SavedLookOffset = 0;
    std::uint32_t g_AttachModeReadback = 0;
    float g_ManualOffsetAtTakeOver[2] = {};
    bool g_ManualOffsetStompLogged = false;
    bool g_ManualOffsetCaptured = false;
    bool g_BrakeArmed = false;
    bool g_SpeedBanded = false;
    float g_SpeedLo = kShellSpeedFloor;
    float g_SpeedHi = kShellSpeedCeiling;
    float g_SpeedSpan = 0.0f;
    float g_Speed = 0.0f;
    float g_LaunchPos[3] = {};
    bool g_LaunchPosValid = false;
    float g_LastShellPos[3] = {};
    float g_PrevShellPos[3] = {};
    float g_LastStepSeconds = 0.0f;
    unsigned g_ShellPosSamples = 0;

    alignas(16) std::uint8_t g_HandoffDesc[kDescBytes] = {};
    bool g_HandoffDescValid = false;
    std::uint32_t g_HandoffExecCursor = 0;
    bool g_HandoffLogged = false;

    std::uint8_t* g_PlayerContext = nullptr;
    bool g_ResolveLogged = false;
    std::uint8_t* g_ShapeLoggedFor = nullptr;
    unsigned g_IdleProbeTicks = 0;
    constexpr unsigned kIdleProbeInterval = 300;
    unsigned g_TickCount = 0;
    bool g_FirstTickLogged = false;

    thread_local bool tls_ShotArmed = false;
    MissileSpec g_ArmedSpec{};
    int g_ArmedPlayerIndex = 0;
    int g_ArmedEquipId = 0;

    using ShellFire_t = void(__fastcall*)(void*, void*);
    ShellFire_t g_OrigShellFire = nullptr;
    void* g_ShellFireTarget = nullptr;
    bool g_FireHookAttempted = false;
    bool g_ClaimLogged = false;
    std::uint32_t g_ClaimedWorkIndex = kShellNoIndex;
    std::uint16_t g_ClaimedShellId = 0;

    using ShellExecute_t = void(__fastcall*)(void*);
    ShellExecute_t g_OrigExecuteSerially = nullptr;
    void* g_ExecuteSerialTarget = nullptr;
    bool g_ExecuteHookAttempted = false;

    int SehKeepAvOnly(unsigned long code)
    {
        return (code == EXCEPTION_ACCESS_VIOLATION
                || code == EXCEPTION_IN_PAGE_ERROR)
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
    }

    using GetQuarkSystemTable_t = std::uint8_t* (__fastcall*)();
    GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;

    std::uint8_t* ResolveShellSystemSEH()
    {
        __try
        {
            if (!g_GetQuarkSystemTable)
                return nullptr;
            std::uint8_t* qst = g_GetQuarkSystemTable();
            if (!qst)
                return nullptr;
            std::uint8_t* app =
                *reinterpret_cast<std::uint8_t**>(qst + kQuarkApplicationSystem);
            if (!app)
                return nullptr;

            const std::size_t offsets[2] = { kAppShellSystem154, kAppShellSystem153 };
            for (std::size_t i = 0; i < 2; ++i)
            {
                std::uint8_t* candidate =
                    *reinterpret_cast<std::uint8_t**>(app + offsets[i]);
                if (!candidate)
                    continue;
                std::uint8_t* vtable = *reinterpret_cast<std::uint8_t**>(candidate);
                if (!vtable)
                    continue;
                if (!*reinterpret_cast<void**>(vtable + kShellVtblOperateRocketPunch))
                    continue;

                const std::uint32_t activeIndex =
                    *reinterpret_cast<std::uint32_t*>(candidate + kShellActiveIndex);
                if (activeIndex > kShellNoIndex)
                    continue;

                if (candidate != g_ShapeLoggedFor)
                {
                    g_ShapeLoggedFor = candidate;

                    const std::uint32_t cap =
                        *reinterpret_cast<std::uint32_t*>(candidate + kShellSysTotalCap);
                    const std::uint32_t execCount =
                        *reinterpret_cast<std::uint32_t*>(candidate + kShellSysExecCount);
                    const std::uint32_t granted =
                        *reinterpret_cast<std::uint32_t*>(candidate + kShellSysTotalShells);
                    const std::uint32_t perExec =
                        *reinterpret_cast<std::uint32_t*>(candidate + kShellSysPerExec);
                    const auto* counts = *reinterpret_cast<const std::uint8_t**>(
                        candidate + kShellSysCountBytes);

                    char slices[160];
                    slices[0] = '\0';
                    if (counts && execCount && execCount <= kShellSysMaxExec)
                    {
                        std::size_t used = 0;
                        for (std::uint32_t e = 0; e < execCount; ++e)
                        {
                            const int written = _snprintf_s(
                                slices + used, sizeof(slices) - used, _TRUNCATE,
                                e ? " %u" : "%u",
                                static_cast<unsigned>(counts[e]));
                            if (written <= 0)
                                break;
                            used += static_cast<std::size_t>(written);
                        }
                    }

                    Log("[RemoteMissile] shell system resolved at ApplicationSystem+0x%zX "
                        "(object=%p vtable=%p activeShell=0x%X) - shell pool cap=%u "
                        "executors=%u granted=%u perExecutor=%u slices=[%s] - "
                        "FireRocketPunch always allocates in executor 0, so while its slice "
                        "is 1 every steerable shot evicts the one before it\n",
                        offsets[i], candidate, vtable, activeIndex,
                        cap, execCount, granted, perExec,
                        slices[0] ? slices : "unreadable");
                }
                g_ResolveLogged = true;
                return candidate;
            }

            if (!g_ResolveLogged)
            {
                g_ResolveLogged = true;
                Log("[RemoteMissile] the shell system was not found at "
                    "ApplicationSystem+0x210 or +0x208 - no steerable shell can be fired "
                    "on this build until that offset is corrected\n");
            }
            return nullptr;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return nullptr;
        }
    }

    template <typename Fn>
    Fn ShellCall(std::uint8_t* shellSystem, std::size_t slot)
    {
        std::uint8_t* vtable = *reinterpret_cast<std::uint8_t**>(shellSystem);
        return *reinterpret_cast<Fn*>(vtable + slot);
    }

    bool IsExistSEH(std::uint8_t* shellSystem)
    {
        __try
        {
            using IsExist_t = char(__fastcall*)(void*);
            return ShellCall<IsExist_t>(shellSystem, kShellVtblIsExistRocketPunch)
                       (shellSystem) != 0;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    bool FireRocketPunchSEH(std::uint8_t* shellSystem, std::uint8_t* desc)
    {
        __try
        {
            using Fire_t = void(__fastcall*)(void*, void*);
            ShellCall<Fire_t>(shellSystem, kShellVtblFireRocketPunch)(shellSystem, desc);
            return *reinterpret_cast<std::uint32_t*>(shellSystem + kShellActiveIndex)
                       != kShellNoIndex;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    void CancelSEH(std::uint8_t* shellSystem)
    {
        __try
        {
            using Cancel_t = void(__fastcall*)(void*);
            ShellCall<Cancel_t>(shellSystem, kShellVtblCancelRocketPunch)(shellSystem);
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
        }
    }

    std::uint8_t* PlayerSubsystemSEH(std::size_t offset)
    {
        __try
        {
            if (!g_PlayerContext)
                return nullptr;
            std::uint8_t* table =
                *reinterpret_cast<std::uint8_t**>(g_PlayerContext + kPlayerSubsystems);
            if (!table)
                return nullptr;
            return *reinterpret_cast<std::uint8_t**>(table + offset);
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return nullptr;
        }
    }

    bool ReadLocalCharaIndexSEH(std::int32_t* outIndex)
    {
        __try
        {
            if (!g_PlayerContext)
                return false;
            std::uint8_t* table =
                *reinterpret_cast<std::uint8_t**>(g_PlayerContext + kPlayerSubsystems);
            if (!table)
                return false;
            const std::int32_t index = *reinterpret_cast<std::int32_t*>(
                table + kSubsystemLocalCharaIndex);
            const std::int32_t base = *reinterpret_cast<std::int32_t*>(
                g_PlayerContext + kPlayerSlotBase);
            if (index < base || index - base >= kPlayerVoiceSlotLimit)
                return false;
            *outIndex = index;
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    bool CallPlayerVoiceSEH(std::int32_t charaIndex, std::uint32_t voiceId)
    {
        __try
        {
            if (!g_PlayerContext)
                return false;
            using CallVoice_t = void(__fastcall*)(void*, std::uint32_t,
                                                  std::uint32_t, std::uint32_t);
            CallVoice_t fn =
                ShellCall<CallVoice_t>(g_PlayerContext, kPlayerVtblCallVoice);
            if (!fn)
                return false;
            fn(g_PlayerContext, static_cast<std::uint32_t>(charaIndex), voiceId,
               kPlayerVoicePriority);
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    void PlayFireVoice(std::uint32_t voiceId)
    {
        if (voiceId == 0)
            return;
        std::int32_t charaIndex = 0;
        if (ReadLocalCharaIndexSEH(&charaIndex)
            && CallPlayerVoiceSEH(charaIndex, voiceId))
            return;
        static std::atomic<bool> s_toldVoice{ false };
        if (!s_toldVoice.exchange(true))
            Log("[RemoteMissile] fireVoiceId 0x%08X could not be played - the "
                "player object or its chara slot could not be read when the "
                "weapon fired, so this weapon stays silent for the rest of the "
                "session while every other part of the missile still works\n",
                voiceId);
    }

    struct StickInput
    {
        float leftX = 0.0f;
        float leftY = 0.0f;
        float brake = 0.0f;
        float boost = 0.0f;
    };

    bool ReadStickSEH(std::uint8_t* pad, StickInput* out)
    {
        __try
        {
            std::uint8_t* vtable = *reinterpret_cast<std::uint8_t**>(pad);
            using Axis_t = float(__fastcall*)(void*, std::uint32_t);
            Axis_t axisX =
                *reinterpret_cast<Axis_t*>(vtable + kPadVtblStickXIgnoreMask);
            Axis_t axisY =
                *reinterpret_cast<Axis_t*>(vtable + kPadVtblStickYIgnoreMask);
            Axis_t trigger = *reinterpret_cast<Axis_t*>(vtable + kPadVtblTrigger);
            using Action_t = bool(__fastcall*)(void*, unsigned long long);
            Action_t isActionOn =
                *reinterpret_cast<Action_t*>(vtable + kPadVtblIsActionOn);

            out->leftX = axisX(pad, 0);
            out->leftY = axisY(pad, 0);

            float brake = trigger(pad, 0);
            float boost = trigger(pad, 1);
            if (brake < kTriggerDeadzone)
                brake = isActionOn(pad, kPadActionBrake) ? 1.0f : 0.0f;
            if (boost < kTriggerDeadzone)
                boost = isActionOn(pad, kPadActionBoost) ? 1.0f : 0.0f;

            if (!g_BrakeArmed && brake < kTriggerDeadzone)
                g_BrakeArmed = true;

            out->brake = g_BrakeArmed ? brake : 0.0f;
            out->boost = boost;
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    void OperateSEH(std::uint8_t* shellSystem, const StickInput& in, float dt)
    {
        __try
        {
            using Operate_t = void(__fastcall*)(void*, float, float, float, float,
                                                float, float, float);
            ShellCall<Operate_t>(shellSystem, kShellVtblOperateRocketPunch)
                (shellSystem, in.leftX, in.leftY, 0.0f, 0.0f, in.brake, in.boost, dt);
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
        }
    }

    bool GetCameraMatrixSEH(std::uint8_t* shellSystem, float outMatrix[16])
    {
        __try
        {
            std::memset(outMatrix, 0, sizeof(float) * 16);
            outMatrix[15] = 1.0f;
            using GetMatrix_t = void(__fastcall*)(void*, void*);
            ShellCall<GetMatrix_t>(shellSystem, kShellVtblGetCameraMatrix)
                (shellSystem, outMatrix);
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    void OffsetChaseCamera(float m[16], float back, float rise)
    {
        float fx = m[8], fy = m[9], fz = m[10];

        const float forwardLength = std::sqrt(fx * fx + fy * fy + fz * fz);
        if (!(forwardLength > 0.0001f))
            return;
        fx /= forwardLength;
        fy /= forwardLength;
        fz /= forwardLength;

        m[12] -= fx * back;
        m[13] -= fy * back;
        m[14] -= fz * back;
        m[13] += rise;
    }

    void MatrixToQuaternion(const float m[16], float outQuat[4])
    {
        const float m00 = m[0], m01 = m[4], m02 = m[8];
        const float m10 = m[1], m11 = m[5], m12 = m[9];
        const float m20 = m[2], m21 = m[6], m22 = m[10];
        const float trace = m00 + m11 + m22;

        if (trace > 0.0f)
        {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            outQuat[0] = (m21 - m12) / s;
            outQuat[1] = (m02 - m20) / s;
            outQuat[2] = (m10 - m01) / s;
            outQuat[3] = 0.25f * s;
        }
        else if (m00 > m11 && m00 > m22)
        {
            const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            outQuat[0] = 0.25f * s;
            outQuat[1] = (m01 + m10) / s;
            outQuat[2] = (m02 + m20) / s;
            outQuat[3] = (m21 - m12) / s;
        }
        else if (m11 > m22)
        {
            const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            outQuat[0] = (m01 + m10) / s;
            outQuat[1] = 0.25f * s;
            outQuat[2] = (m12 + m21) / s;
            outQuat[3] = (m02 - m20) / s;
        }
        else
        {
            const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            outQuat[0] = (m02 + m20) / s;
            outQuat[1] = (m12 + m21) / s;
            outQuat[2] = 0.25f * s;
            outQuat[3] = (m10 - m01) / s;
        }
    }

    bool InstallCameraSEH(std::uint8_t* camera, const float matrix[16])
    {
        __try
        {
            if (!g_CameraTaken)
            {
                g_SavedAttachMode =
                    *reinterpret_cast<std::uint32_t*>(camera + kCameraAttachMode);
                g_SavedManualSpace =
                    *reinterpret_cast<std::uint32_t*>(camera + kCameraManualSpace);
                std::memcpy(g_SavedBaseRotation, camera + kCameraBaseRotation,
                            sizeof(g_SavedBaseRotation));
                g_SavedLookOffset =
                    *reinterpret_cast<std::uint64_t*>(camera + kCameraLookOffset);
                g_CameraTaken = true;
            }

            auto* attachMode =
                reinterpret_cast<std::uint32_t*>(camera + kCameraAttachMode);
            *attachMode = kCameraAttachManual;

            auto* flags = reinterpret_cast<std::uint32_t*>(camera + kCameraFlags);
            *flags |= kCameraFlagNoManualRotation;

            auto* manualPitch = reinterpret_cast<float*>(camera + kCameraManualPitch);
            auto* manualYaw = reinterpret_cast<float*>(camera + kCameraManualYaw);
            if (!g_ManualOffsetCaptured)
            {
                g_ManualOffsetCaptured = true;
                g_ManualOffsetAtTakeOver[0] = *manualPitch;
                g_ManualOffsetAtTakeOver[1] = *manualYaw;
            }
            else if (!g_ManualOffsetStompLogged
                     && (*manualPitch != 0.0f || *manualYaw != 0.0f))
            {
                g_ManualOffsetStompLogged = true;
                Log("[RemoteMissile] the camera's manual look offset came back as "
                    "(%.3f %.3f) after a steer tick zeroed it, so something re-applies "
                    "it later in the frame and the view trails the shell\n",
                    *manualPitch, *manualYaw);
            }
            *manualPitch = 0.0f;
            *manualYaw = 0.0f;

            *reinterpret_cast<std::uint64_t*>(camera + kCameraLookOffset) = 0;

            auto* space = reinterpret_cast<std::uint32_t*>(camera + kCameraManualSpace);
            *space &= ~1u;

            auto* position = reinterpret_cast<float*>(camera + kCameraManualPosition);
            position[0] = matrix[12];
            position[1] = matrix[13];
            position[2] = matrix[14];
            position[3] = 1.0f;

            alignas(16) float quat[4];
            MatrixToQuaternion(matrix, quat);

            const std::uint32_t playerIndex =
                *reinterpret_cast<std::uint32_t*>(camera + kCameraPlayerIndex);

            std::uint8_t* vtable = *reinterpret_cast<std::uint8_t**>(camera);
            using SetBaseRotation_t =
                void(__fastcall*)(void*, std::uint32_t, const float*, bool);
            SetBaseRotation_t setRotation = *reinterpret_cast<SetBaseRotation_t*>(
                vtable + kCameraVtblSetBaseRotation);
            setRotation(camera, playerIndex, quat, false);
            g_AttachModeReadback = *attachMode;
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    bool ReleaseCameraSEH(std::uint8_t* camera)
    {
        __try
        {
            *reinterpret_cast<std::uint32_t*>(camera + kCameraAttachMode) =
                g_SavedAttachMode;

            auto* flags = reinterpret_cast<std::uint32_t*>(camera + kCameraFlags);
            *flags &= ~kCameraFlagNoManualRotation;

            *reinterpret_cast<std::uint64_t*>(camera + kCameraLookOffset) =
                g_SavedLookOffset;

            *reinterpret_cast<float*>(camera + kCameraManualPitch) =
                g_ManualOffsetAtTakeOver[0];
            *reinterpret_cast<float*>(camera + kCameraManualYaw) =
                g_ManualOffsetAtTakeOver[1];

            auto* space = reinterpret_cast<std::uint32_t*>(camera + kCameraManualSpace);
            *space = (*space & ~1u) | (g_SavedManualSpace & 1u);

            std::uint8_t* vtable = *reinterpret_cast<std::uint8_t**>(camera);
            using SetBaseRotation_t =
                void(__fastcall*)(void*, std::uint32_t, const float*, bool);
            SetBaseRotation_t setRotation = *reinterpret_cast<SetBaseRotation_t*>(
                vtable + kCameraVtblSetBaseRotation);
            setRotation(camera,
                        *reinterpret_cast<std::uint32_t*>(camera + kCameraPlayerIndex),
                        g_SavedBaseRotation, false);
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    bool SetInputMaskSEH(std::uint8_t* pad, bool masked)
    {
        __try
        {
            std::uint8_t* vtable = *reinterpret_cast<std::uint8_t**>(pad);
            if (masked)
            {
                using SetMask_t =
                    void(__fastcall*)(void*, std::uint32_t, std::uint32_t);
                SetMask_t setButtons = *reinterpret_cast<SetMask_t*>(
                    vtable + kPadVtblSetMaskButtons);
                setButtons(pad, kInputOwner, kButtonMaskBlockAll);

                SetMask_t setSticks = *reinterpret_cast<SetMask_t*>(
                    vtable + kPadVtblSetMaskSticks);
                setSticks(pad, kInputOwner, kStickMaskMoveStick);
            }
            else
            {
                using ResetMask_t = void(__fastcall*)(void*, std::uint32_t);
                ResetMask_t resetButtons = *reinterpret_cast<ResetMask_t*>(
                    vtable + kPadVtblResetMaskButtons);
                resetButtons(pad, kInputOwner);

                ResetMask_t resetSticks = *reinterpret_cast<ResetMask_t*>(
                    vtable + kPadVtblResetMaskSticks);
                resetSticks(pad, kInputOwner);
            }
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    bool EngineSkippedFrameSEH(std::uint8_t* shellSystem)
    {
        __try
        {
            return (*reinterpret_cast<std::uint32_t*>(
                        shellSystem + kShellPunchFlags)
                    & kShellPunchSystemPaused) != 0;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    void WriteShellSpeedSEH(std::uint8_t* shellSystem, float value)
    {
        __try
        {
            *reinterpret_cast<float*>(shellSystem + kShellSpeedState) = value;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
        }
    }

    void ResolveSpeedBand(const MissileSpec& spec, float* outLo, float* outHi,
                          bool* outBanded)
    {
        *outBanded = (spec.minSpeed > 0.0f || spec.maxSpeed > 0.0f);
        float lo = spec.minSpeed > 0.0f ? spec.minSpeed : kShellSpeedFloor;
        float hi = spec.maxSpeed > 0.0f ? spec.maxSpeed : kShellSpeedCeiling;
        if (hi < lo)
            hi = lo + kShellSpeedVanillaSpan;
        *outLo = lo;
        *outHi = hi;
    }

    void ArmSpeedBand(const MissileSpec& spec)
    {
        ResolveSpeedBand(spec, &g_SpeedLo, &g_SpeedHi, &g_SpeedBanded);
        g_SpeedSpan = g_SpeedHi - g_SpeedLo;
        g_Speed = g_SpeedLo;
    }

    float SpeedBandScale()
    {
        return g_SpeedSpan > 0.0f ? g_SpeedSpan / kShellSpeedVanillaSpan : 1.0f;
    }

    float ShellSpeedShadow()
    {
        if (!(g_SpeedSpan > 0.0f))
            return kShellSpeedFloor;
        return kShellSpeedFloor
            + (g_Speed - g_SpeedLo) / g_SpeedSpan * kShellSpeedVanillaSpan;
    }

    void AdvanceSpeedBand(const StickInput& in, float dt)
    {
        const float scale = SpeedBandScale();
        if (in.brake > kEngineTriggerEpsilon)
            g_Speed -= dt * kShellSpeedBrakeRate * scale;
        else if (in.boost > kEngineTriggerEpsilon)
            g_Speed += dt * kShellSpeedBoostRate * scale;
        else
            g_Speed -= dt * kShellSpeedDragRate * scale;

        if (g_Speed < g_SpeedLo)
            g_Speed = g_SpeedLo;
        if (g_Speed > g_SpeedHi)
            g_Speed = g_SpeedHi;
    }

    float ReadShellSpeedSEH(std::uint8_t* shellSystem)
    {
        __try
        {
            return *reinterpret_cast<float*>(shellSystem + kShellSpeedState);
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return 0.0f;
        }
    }

    bool ButtonLeaseHeldSEH(std::uint8_t* pad)
    {
        __try
        {
            auto* slot =
                reinterpret_cast<std::uint32_t*>(pad + kPadButtonLeaseTable);
            for (int i = 0; i < 8; ++i, slot += 2)
            {
                if (slot[0] == kInputOwner)
                    return true;
            }
            return false;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    std::uint32_t ReadStickMaskSEH(std::uint8_t* pad)
    {
        __try
        {
            return *reinterpret_cast<std::uint32_t*>(pad + kPadStickMaskResult);
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return 0;
        }
    }

    void SuppressLeashSEH(std::uint8_t* shellSystem)
    {
        __try
        {
            *reinterpret_cast<float*>(shellSystem + kShellOutOfRangeTimer) = 0.0f;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
        }
    }

    std::uint16_t DescLifetimeTicks(float seconds)
    {
        float ticks = seconds * kTimeBaseTicksPerSecond;
        if (!(ticks > 1.0f))
            ticks = 1.0f;
        if (ticks > static_cast<float>(kDescLifetimeMax))
            ticks = static_cast<float>(kDescLifetimeMax);
        return static_cast<std::uint16_t>(ticks + 0.5f);
    }

    bool SwapDescLifetimeSEH(std::uint8_t* desc, std::uint16_t value,
                             std::uint16_t* outOld)
    {
        __try
        {
            auto* field = reinterpret_cast<std::uint16_t*>(desc + kDescLifetime);
            *outOld = *field;
            *field = value;
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    void RestoreDescLifetimeSEH(std::uint8_t* desc, std::uint16_t value)
    {
        __try
        {
            *reinterpret_cast<std::uint16_t*>(desc + kDescLifetime) = value;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
        }
    }

    bool SwapDescAttackSEH(std::uint8_t* desc, std::uint16_t value,
                           std::uint16_t* outOld)
    {
        __try
        {
            auto* field = reinterpret_cast<std::uint16_t*>(desc + kDescAttack);
            *outOld = *field;
            *field = static_cast<std::uint16_t>(
                (*field & ~kDescAttackIdMask) | (value & kDescAttackIdMask));
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    void RestoreDescAttackSEH(std::uint8_t* desc, std::uint16_t value)
    {
        __try
        {
            *reinterpret_cast<std::uint16_t*>(desc + kDescAttack) = value;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
        }
    }

    void DisarmAreaLatchSEH(std::uint8_t* shellSystem)
    {
        __try
        {
            *reinterpret_cast<std::uint32_t*>(shellSystem + kShellPunchFlags) &=
                ~(kShellPunchAreaArm | kShellPunchEffectLatch);
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
        }
    }

    void SilenceNoiseFilterSEH(std::uint8_t* shellSystem)
    {
        __try
        {
            std::uint8_t* callback =
                *reinterpret_cast<std::uint8_t**>(shellSystem + kShellCallbackObject);
            if (!callback)
                return;
            std::uint8_t* vtable = *reinterpret_cast<std::uint8_t**>(callback);
            if (!vtable)
                return;
            using Enable_t = void(__fastcall*)(void*, bool, float);
            const Enable_t enable = *reinterpret_cast<Enable_t*>(
                vtable + kCallbackVtblEnableNoiseFilter);
            if (enable)
                enable(callback, false, 0.0f);
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
        }
    }

    void ReassertLifetimeSEH(std::uint8_t* shellSystem, float remainingSeconds)
    {
        __try
        {
            const std::uint32_t idx =
                *reinterpret_cast<std::uint32_t*>(shellSystem + kShellActiveIndex);
            if (idx == kShellNoIndex)
                return;
            std::uint8_t* holder =
                *reinterpret_cast<std::uint8_t**>(shellSystem + kShellImplHolder);
            if (!holder)
                return;
            std::uint8_t* shellImpl = *reinterpret_cast<std::uint8_t**>(holder);
            if (!shellImpl)
                return;
            std::uint8_t* workBase =
                *reinterpret_cast<std::uint8_t**>(shellImpl + kShellImplWorkArray);
            if (!workBase)
                return;

            std::uint8_t* work =
                workBase + static_cast<std::size_t>(idx) * kShellWorkStride;
            const std::uint32_t flags =
                *reinterpret_cast<std::uint32_t*>(work + kShellWorkFlags);
            const std::uint32_t state =
                *reinterpret_cast<std::uint32_t*>(work + kShellWorkState);
            if ((flags & kShellWorkTerminating) || (state & kShellStateImpacted))
                return;

            auto* life = reinterpret_cast<float*>(work + kShellWorkLifetime);
            if (remainingSeconds > *life)
                *life = remainingSeconds;
            if (flags & kShellWorkTrailSpawned)
                *reinterpret_cast<float*>(work + kShellWorkAge) = kShellAgeHold;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
        }
    }

    bool SwapDescParentSEH(std::uint8_t* desc, std::uint8_t value, std::uint8_t* outOld)
    {
        __try
        {
            *outOld = desc[kDescParent];
            desc[kDescParent] = value;
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    void RestoreDescParentSEH(std::uint8_t* desc, std::uint8_t value)
    {
        __try
        {
            desc[kDescParent] = value;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
        }
    }

    bool ReadShellPoolSEH(std::uint8_t* shellSystem, std::uint32_t* outSize,
                          std::uint32_t* outUsed)
    {
        __try
        {
            std::uint8_t* holder =
                *reinterpret_cast<std::uint8_t**>(shellSystem + kShellImplHolder);
            if (!holder)
                return false;
            std::uint8_t* shellImpl = *reinterpret_cast<std::uint8_t**>(holder);
            if (!shellImpl)
                return false;

            const std::uint32_t size =
                *reinterpret_cast<std::uint32_t*>(shellImpl + kShellImplPoolSize);
            const auto* mask =
                *reinterpret_cast<std::uint8_t**>(shellImpl + kShellImplUsedMask);
            if (!mask || size == 0 || size > kShellNoIndex)
                return false;

            std::uint32_t used = 0;
            for (std::uint32_t i = 0; i < size; ++i)
                if (mask[i >> 3] & (1u << (i & 7)))
                    ++used;

            *outSize = size;
            *outUsed = used;
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    struct HandoffSnapshot
    {
        float position[3];
        float velocity[3];
        float speed;
        float lifeSeconds;
    };

    bool CaptureShotDescriptorSEH(const std::uint8_t* desc)
    {
        __try
        {
            std::memcpy(g_HandoffDesc, desc, kDescBytes);
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    std::uint32_t ReadExecutorCountSEH(std::uint8_t* shellSystem)
    {
        __try
        {
            return *reinterpret_cast<std::uint32_t*>(
                shellSystem + kShellSysExecCount);
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return 0;
        }
    }

    bool ReadHandoffSnapshotSEH(std::uint8_t* shellSystem, std::uint32_t idx,
                                HandoffSnapshot* out)
    {
        __try
        {
            auto* holder =
                *reinterpret_cast<std::uint8_t***>(shellSystem + kShellImplHolder);
            if (!holder)
                return false;
            std::uint8_t* exec0 = holder[0];
            if (!exec0)
                return false;

            const auto* positions =
                *reinterpret_cast<const float**>(shellSystem + kShellSysPositions);
            const auto* eulers =
                *reinterpret_cast<const float**>(exec0 + kShellImplVelocityArray);
            const auto* speeds =
                *reinterpret_cast<const float**>(exec0 + kShellImplOriginArray);
            const auto* workBase =
                *reinterpret_cast<const std::uint8_t**>(exec0 + kShellImplWorkArray);
            if (!positions || !eulers || !speeds || !workBase)
                return false;

            const std::size_t lane = static_cast<std::size_t>(idx) * 4;
            out->position[0] = positions[lane + 0];
            out->position[1] = positions[lane + 1];
            out->position[2] = positions[lane + 2];

            const float pitch = eulers[lane + 0];
            const float yaw = eulers[lane + 1];
            const float speed = speeds[lane + 0];
            const float horizontal = std::cos(pitch);
            out->speed = speed;
            out->velocity[0] = horizontal * std::sin(yaw) * speed;
            out->velocity[1] = -std::sin(pitch) * speed;
            out->velocity[2] = horizontal * std::cos(yaw) * speed;

            out->lifeSeconds = *reinterpret_cast<const float*>(
                workBase + static_cast<std::size_t>(idx) * kShellWorkStride
                + kShellWorkLifetime);
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    void BuildHandoffDescriptor(const float* position, const float* velocity,
                                float lifeSeconds)
    {
        std::memcpy(g_HandoffDesc + kDescDirection, velocity, sizeof(float) * 3);
        *reinterpret_cast<float*>(g_HandoffDesc + kDescSpeed) = 0.0f;
        std::memcpy(g_HandoffDesc + kDescPosition, position, sizeof(float) * 3);
        *reinterpret_cast<std::uint16_t*>(g_HandoffDesc + kDescLifetime) =
            DescLifetimeTicks(lifeSeconds);
        g_HandoffDesc[kDescParent] = kDescNoParent;

        auto* mode = reinterpret_cast<std::uint16_t*>(g_HandoffDesc + kDescAttack);
        *mode = static_cast<std::uint16_t>((*mode & ~kDescVelocityModeMask)
                                           | kDescVelocityModeDirect);
        auto* latch =
            reinterpret_cast<std::uint16_t*>(g_HandoffDesc + kDescExpiryLatch);
        *latch = static_cast<std::uint16_t>(*latch & ~kDescExpiryLatchBit);
    }

    std::uint32_t SpawnHandoffShellSEH(std::uint8_t* shellSystem,
                                       std::uint32_t exec,
                                       const float* position)
    {
        __try
        {
            auto* holder =
                *reinterpret_cast<std::uint8_t***>(shellSystem + kShellImplHolder);
            if (!holder)
                return kShellNoIndex;
            std::uint8_t* elem = holder[exec];
            if (!elem)
                return kShellNoIndex;
            if (*reinterpret_cast<const std::uint8_t*>(elem + kShellImplNetFlag) != 0)
                return kShellNoIndex;

            std::uint8_t* vtable = *reinterpret_cast<std::uint8_t**>(elem);
            if (!vtable)
                return kShellNoIndex;
            using Create_t = std::uint32_t(__fastcall*)(void*, void*);
            const Create_t create =
                *reinterpret_cast<Create_t*>(vtable + kShellExecVtblCreate);
            if (!create)
                return kShellNoIndex;

            const std::uint32_t poolSize =
                *reinterpret_cast<std::uint32_t*>(elem + kShellImplPoolSize);
            const std::uint32_t idx = create(elem, g_HandoffDesc);
            if (idx == kShellNoIndex || poolSize == 0 || idx >= poolSize)
                return kShellNoIndex;

            std::uint8_t* workBase =
                *reinterpret_cast<std::uint8_t**>(elem + kShellImplWorkArray);
            auto* origins = *reinterpret_cast<float**>(elem + kShellImplOriginArray);
            auto* timeBase = *reinterpret_cast<float**>(elem + kShellImplTimeBase);
            if (!workBase || !origins || !timeBase)
                return kShellNoIndex;

            std::uint8_t* work =
                workBase + static_cast<std::size_t>(idx) * kShellWorkStride;
            *reinterpret_cast<std::uint32_t*>(work + kShellWorkFlags) &=
                ~kShellWorkLocalAuthority;

            const std::size_t lane = static_cast<std::size_t>(idx) * 4;
            origins[lane + 0] = position[0];
            origins[lane + 1] = position[1];
            origins[lane + 2] = position[2];
            origins[lane + 3] = 0.0f;

            *reinterpret_cast<float*>(work + kShellWorkAge) = kShellAgeHold;
            timeBase[idx] = kShellAgeHold;
            return idx;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return kShellNoIndex;
        }
    }

    bool RetirePunchShellSEH(std::uint8_t* shellSystem, std::uint32_t idx)
    {
        __try
        {
            auto* holder =
                *reinterpret_cast<std::uint8_t***>(shellSystem + kShellImplHolder);
            if (!holder)
                return false;
            std::uint8_t* exec0 = holder[0];
            if (!exec0)
                return false;
            std::uint8_t* workBase =
                *reinterpret_cast<std::uint8_t**>(exec0 + kShellImplWorkArray);
            if (!workBase)
                return false;
            *reinterpret_cast<std::uint32_t*>(
                workBase + static_cast<std::size_t>(idx) * kShellWorkStride
                + kShellWorkFlags) |= kShellWorkRetire;
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    void HandOffReleasedShell(std::uint8_t* shellSystem)
    {
        const std::uint32_t idxA = g_ClaimedWorkIndex;
        if (!g_HandoffDescValid || idxA == kShellNoIndex)
            return;
        if (MissionCodeGuard::ShouldBypassHooks())
            return;

        const std::uint32_t execCount = ReadExecutorCountSEH(shellSystem);
        if (execCount < 2)
        {
            Log("[RemoteMissile] the released missile could not be handed off: this "
                "machine gives the shell system only %u executor(s), so there is no "
                "pool outside the rocket-punch slot to move it into - it stays where "
                "it is and the next steerable shot still evicts it\n", execCount);
            return;
        }

        HandoffSnapshot snap{};
        if (!ReadHandoffSnapshotSEH(shellSystem, idxA, &snap))
        {
            Log("[RemoteMissile] the released missile could not be handed off: its "
                "position and heading could not be read out of the shell pool, so "
                "there is nothing to rebuild it from - it stays in the rocket-punch "
                "slot\n");
            return;
        }

        float position[3] = { snap.position[0], snap.position[1], snap.position[2] };
        const char* posSource = "shell pool";
        if (g_ShellPosSamples > 0)
        {
            position[0] = g_LastShellPos[0];
            position[1] = g_LastShellPos[1];
            position[2] = g_LastShellPos[2];
            posSource = "last steer tick";
        }

        float velocity[3] = { snap.velocity[0], snap.velocity[1], snap.velocity[2] };
        const char* velSource = "engine heading";
        float measured[3] = {};
        float measuredSpeed = 0.0f;
        if (g_ShellPosSamples >= 2 && g_LastStepSeconds > 0.0001f)
        {
            const float inv = 1.0f / g_LastStepSeconds;
            measured[0] = (g_LastShellPos[0] - g_PrevShellPos[0]) * inv;
            measured[1] = (g_LastShellPos[1] - g_PrevShellPos[1]) * inv;
            measured[2] = (g_LastShellPos[2] - g_PrevShellPos[2]) * inv;
            measuredSpeed = std::sqrt(measured[0] * measured[0]
                                      + measured[1] * measured[1]
                                      + measured[2] * measured[2]);
            if (measuredSpeed > kHandoffMinSpeed && measuredSpeed < kHandoffMaxSpeed)
            {
                velocity[0] = measured[0];
                velocity[1] = measured[1];
                velocity[2] = measured[2];
                velSource = "measured across the last two frames";
            }
        }

        float life = snap.lifeSeconds;
        if (!(life > 0.0f) || life > kHandoffMaxLifeSeconds)
            life = kHandoffMaxLifeSeconds;

        BuildHandoffDescriptor(position, velocity, life);

        const std::uint32_t exec = 1 + (g_HandoffExecCursor++ % (execCount - 1));
        const std::uint32_t idxB = SpawnHandoffShellSEH(shellSystem, exec, position);
        if (idxB == kShellNoIndex)
        {
            Log("[RemoteMissile] the released missile could not be handed off: shell "
                "executor %u refused the replacement (its slot is taken, or this is a "
                "network session where a second shell would be announced to the other "
                "player) - the missile stays in the rocket-punch slot and the next "
                "steerable shot still evicts it\n", exec);
            return;
        }

        if (!RetirePunchShellSEH(shellSystem, idxA))
            Log("[RemoteMissile] the replacement shell was created in executor %u but "
                "the original could not be marked for release, so both are in the air "
                "and the rocket-punch slot is still occupied\n", exec);

        if (!g_HandoffLogged)
        {
            g_HandoffLogged = true;
            Log("[RemoteMissile] released missile handed off: rebuilt as an ordinary "
                "shell in executor %u slot %u and the rocket-punch slot was freed, so "
                "the next steerable shot evicts nothing - position=(%.1f %.1f %.1f) "
                "from the %s, velocity=(%.2f %.2f %.2f) %s, engine heading gives "
                "(%.2f %.2f %.2f) at speed %.2f, measured gives %.2f, life=%.1fs\n",
                exec, idxB, position[0], position[1], position[2], posSource,
                velocity[0], velocity[1], velocity[2], velSource,
                snap.velocity[0], snap.velocity[1], snap.velocity[2], snap.speed,
                measuredSpeed, life);
        }
    }

    bool ClaimActivatedShellSEH(std::uint8_t* shellSystem, std::uint32_t* outFlags)
    {
        g_ClaimedWorkIndex = kShellNoIndex;
        __try
        {
            const std::uint32_t idx =
                *reinterpret_cast<std::uint32_t*>(shellSystem + kShellActiveIndex);
            if (idx == kShellNoIndex)
                return false;

            std::uint8_t* holder =
                *reinterpret_cast<std::uint8_t**>(shellSystem + kShellImplHolder);
            if (!holder)
                return false;
            std::uint8_t* shellImpl = *reinterpret_cast<std::uint8_t**>(holder);
            if (!shellImpl)
                return false;
            std::uint8_t* workBase =
                *reinterpret_cast<std::uint8_t**>(shellImpl + kShellImplWorkArray);
            if (!workBase)
                return false;

            auto* flags = reinterpret_cast<std::uint32_t*>(
                workBase + static_cast<std::size_t>(idx) * kShellWorkStride
                + kShellWorkFlags);
            *outFlags = *flags;
            g_ClaimedShellId = *reinterpret_cast<std::uint16_t*>(
                workBase + static_cast<std::size_t>(idx) * kShellWorkStride
                + kShellWorkShellId);
            g_ClaimedWorkIndex = idx;
            *flags |= kShellWorkFlightModeSteer | kShellWorkLocalAuthority;
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    void ReleaseControl(const char* reason)
    {
        if (!g_Flying.exchange(false))
            return;

        g_ClaimedWorkIndex = kShellNoIndex;

        Log("[RemoteMissile] flight ended (%s) after %u steer tick(s) - 0 ticks "
            "means the tick source never ran while the shell was alive, so the "
            "stick and the camera were never applied\n",
            reason, g_TickCount);
        if (g_CameraTaken)
        {
            std::uint8_t* camera = PlayerSubsystemSEH(kSubsystemCamera);
            if (!camera || ReleaseCameraSEH(camera))
                g_CameraTaken = false;
            else
                Log("[RemoteMissile] the camera object is still there but refused the "
                    "hand-back, so the pre-flight snapshot is being kept for the next "
                    "attempt rather than overwritten with the hijacked pose\n");
        }
        if (g_InputMasked)
        {
            std::uint8_t* pad = PlayerSubsystemSEH(kSubsystemPad);
            if (!pad || SetInputMaskSEH(pad, false))
                g_InputMasked = false;
            else
                Log("[RemoteMissile] the pad object is still there but refused the "
                    "lease release, so the player may still be locked out until the "
                    "next flight ends\n");
        }
    }

    void __fastcall hkShellFire(void* self, void* desc)
    {
        const bool mine = tls_ShotArmed;
        tls_ShotArmed = false;

        if (!mine || !self || !desc)
        {
            if (g_OrigShellFire)
                g_OrigShellFire(self, desc);
            return;
        }

        auto* shellSystem = static_cast<std::uint8_t*>(self);
        auto* descBytes = static_cast<std::uint8_t*>(desc);

        std::uint8_t parentWas = kDescNoParent;
        const bool parentSwapped =
            SwapDescParentSEH(descBytes, kDescNoParent, &parentWas);

        std::uint32_t poolSize = 0;
        std::uint32_t poolUsed = 0;
        const bool poolRead = ReadShellPoolSEH(shellSystem, &poolSize, &poolUsed);

        std::uint16_t lifeWas = kDescDefaultLifetime;
        const bool lifeSwapped = SwapDescLifetimeSEH(
            descBytes,
            DescLifetimeTicks(g_ArmedSpec.maxFlightSeconds + kLifetimeHeadroomSeconds),
            &lifeWas);

        std::uint16_t attackWas = 0;
        const bool attackSwapped =
            g_ArmedSpec.hasAttackId
            && SwapDescAttackSEH(descBytes,
                                 static_cast<std::uint16_t>(g_ArmedSpec.attackId),
                                 &attackWas);

        g_HandoffDescValid = CaptureShotDescriptorSEH(descBytes);

        const bool fired = FireRocketPunchSEH(shellSystem, descBytes);

        if (attackSwapped)
            RestoreDescAttackSEH(descBytes, attackWas);
        if (lifeSwapped)
            RestoreDescLifetimeSEH(descBytes, lifeWas);
        if (parentSwapped)
            RestoreDescParentSEH(descBytes, parentWas);

        if (!fired)
        {
            Log("[RemoteMissile] equipId=%d: FireRocketPunch refused the weapon's own "
                "shell descriptor, so this shot stays an ordinary projectile\n",
                g_ArmedEquipId);
            if (g_OrigShellFire)
                g_OrigShellFire(self, desc);
            return;
        }

        DisarmAreaLatchSEH(shellSystem);
        SilenceNoiseFilterSEH(shellSystem);

        std::uint32_t workFlags = 0;
        if (!ClaimActivatedShellSEH(shellSystem, &workFlags))
        {
            CancelSEH(shellSystem);
            Log("[RemoteMissile] equipId=%d: the shell activated but its work entry "
                "could not be reached, so steerable flight was never switched on - the "
                "orphaned shell was cancelled so it cannot block later shots\n",
                g_ArmedEquipId);
            return;
        }

        if (poolRead && poolUsed >= poolSize)
            Log("[RemoteMissile] equipId=%d: all %u shell slots the launcher can use "
                "were already taken when this shot went out, so the engine "
                "deactivated the longest-lived one to make room - a missile that was "
                "still in the air was removed\n",
                g_ArmedEquipId, poolSize);

        PlayFireVoice(g_ArmedSpec.fireVoiceId);
        g_ActiveSpec = g_ArmedSpec;
        g_LaunchTickMs = GetTickCount64();
        g_LastTickCounter = 0;
        g_TickCount = 0;
        g_FirstTickLogged = false;
        g_ManualOffsetStompLogged = false;
        g_ManualOffsetCaptured = false;
        g_BrakeArmed = false;
        ArmSpeedBand(g_ArmedSpec);
        g_LaunchPosValid = false;
        g_ShellPosSamples = 0;
        g_LastStepSeconds = 0.0f;
        g_Flying = true;

        std::uint8_t* pad = PlayerSubsystemSEH(kSubsystemPad);
        if (pad)
        {
            SetInputMaskSEH(pad, true);
            g_InputMasked = true;

            const std::uint32_t stickMask = ReadStickMaskSEH(pad);
            if ((stickMask & kStickMaskMoveStick) == 0)
                Log("[RemoteMissile] the move-stick lease was refused (pad stick mask "
                    "reads 0x%X), so the player keeps walking around while the missile "
                    "flies\n", stickMask);

            if (!ButtonLeaseHeldSEH(pad))
                Log("[RemoteMissile] the button lease was refused (all 8 pad slots are "
                    "held by other owners), so the player can still aim and fire while "
                    "the missile flies\n");
        }

        if (!g_ClaimLogged)
        {
            g_ClaimLogged = true;
            Log("[RemoteMissile] equipId=%d took control of its own shell - "
                "engine work flags 0x%X (mode %u, authority %s) then forced to "
                "0x%X - slot %u of %u, %u in use, the shell the magazine launched "
                "is equipAmmoId 0x%X - "
                "pad=%s playerContext=%s parentWas=0x%X\n",
                g_ArmedEquipId, workFlags, (workFlags >> 12) & 3,
                (workFlags & kShellWorkLocalAuthority) ? "set" : "clear",
                workFlags | kShellWorkFlightModeSteer | kShellWorkLocalAuthority,
                g_ClaimedWorkIndex, poolSize, poolUsed, g_ClaimedShellId,
                pad ? "bound" : "MISSING (no stick input, no camera)",
                g_PlayerContext ? "captured" : "NEVER CAPTURED",
                parentWas);
        }
    }

    float NextDeltaSeconds()
    {
        LARGE_INTEGER freq{};
        LARGE_INTEGER now{};
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&now);

        float dt = 1.0f / 60.0f;
        if (g_LastTickCounter != 0 && freq.QuadPart > 0)
        {
            const double elapsed =
                static_cast<double>(now.QuadPart - g_LastTickCounter)
                / static_cast<double>(freq.QuadPart);
            if (elapsed > 0.0 && elapsed < 0.25)
                dt = static_cast<float>(elapsed);
        }
        g_LastTickCounter = now.QuadPart;
        return dt;
    }

    void __fastcall hkExecuteSerially(void* self)
    {
        if (g_OrigExecuteSerially)
            g_OrigExecuteSerially(self);
        if (g_Flying)
        {
            shell::RemoteMissile_Tick(NextDeltaSeconds());
            return;
        }
        if (++g_IdleProbeTicks >= kIdleProbeInterval)
        {
            g_IdleProbeTicks = 0;
            ResolveShellSystemSEH();
        }
    }

    bool EnsureExecuteHook(std::uint8_t* shellSystem)
    {
        if (g_OrigExecuteSerially)
            return true;
        if (g_ExecuteHookAttempted || !shellSystem)
            return false;
        g_ExecuteHookAttempted = true;

        void* target = nullptr;
        __try
        {
            std::uint8_t* vtable = *reinterpret_cast<std::uint8_t**>(
                shellSystem + kShellExecuteSubObject);
            if (vtable)
                target = *reinterpret_cast<void**>(vtable + kExecuteVtblSerially);
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            target = nullptr;
        }

        if (!target)
        {
            Log("[RemoteMissile] the shell system's serial execute slot could not be "
                "read, so a launched missile would get no per-frame steering update\n");
            return false;
        }

        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkExecuteSerially),
                                 reinterpret_cast<void**>(&g_OrigExecuteSerially)))
        {
            g_OrigExecuteSerially = nullptr;
            return false;
        }

        g_ExecuteSerialTarget = target;
        Log("[RemoteMissile] shell serial execute hooked at %p - steering now runs once "
            "per frame on the shell system's own thread instead of once per bullet "
            "pool on worker threads\n", target);
        return true;
    }

    bool EnsureFireHook(std::uint8_t* shellSystem)
    {
        if (g_OrigShellFire)
            return true;
        if (g_FireHookAttempted || !shellSystem)
            return false;
        g_FireHookAttempted = true;

        void* target = nullptr;
        __try
        {
            std::uint8_t* vtable = *reinterpret_cast<std::uint8_t**>(shellSystem);
            if (vtable)
                target = *reinterpret_cast<void**>(vtable + kShellVtblFire);
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            target = nullptr;
        }

        if (!target)
        {
            Log("[RemoteMissile] the shell system's Fire slot could not be read, so a "
                "registered weapon keeps firing its ordinary projectile\n");
            return false;
        }

        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkShellFire),
                                 reinterpret_cast<void**>(&g_OrigShellFire)))
        {
            g_OrigShellFire = nullptr;
            return false;
        }

        g_ShellFireTarget = target;
        Log("[RemoteMissile] shell Fire hooked at %p (vtable slot 0) - a registered "
            "weapon's own shot is now redirected into steerable flight instead of "
            "spawning a second projectile\n", target);
        return true;
    }
    void AuditReceiverKeysOnce()
    {
        static std::atomic<bool> s_audited{ false };
        if (s_audited.exchange(true))
            return;

        std::vector<int> wanted;
        {
            std::lock_guard<std::recursive_mutex> lock(g_Mutex);
            wanted.reserve(g_SpecByReceiverId.size());
            for (const auto& entry : g_SpecByReceiverId)
                wanted.push_back(entry.first);
        }
        if (wanted.empty())
            return;

        std::vector<int> equips;
        TppEquip_GetCustomWeaponEquipIds(equips);
        for (int wantRc : wanted)
        {
            bool matched = false;
            for (int eq : equips)
            {
                if (EquipParam_GetReceiverForEquipId(eq) == wantRc)
                {
                    matched = true;
                    break;
                }
            }
            if (!matched)
                Log("[RemoteMissile] receiverId=%d matched no weapon: nothing "
                    "registered through SetGunBasic declares that receiver, and "
                    "a receiver key never reaches a stock weapon, so no shot "
                    "will become a steerable missile\n", wantRc);
        }
    }
}

namespace shell
{
    void RemoteMissile_CaptureContext(void* playerContext)
    {
        g_PlayerContext = static_cast<std::uint8_t*>(playerContext);
    }

    void RemoteMissile_Register(int equipId, int receiverId, int attackId,
                               bool hasAttackId,
                               double maxFlightSeconds, double maxRange,
                               double cameraDistance, double cameraHeight,
                               double minSpeed, double maxSpeed,
                               unsigned int fireVoiceId)
    {
        if ((equipId > 0) == (receiverId > 0))
        {
            LogDebug("[RemoteMissile] equipId=%d receiverId=%d rejected: a remote "
                     "missile is bound to one weapon equip or to one receiver, "
                     "never to both and never to neither - nothing was "
                     "registered\n", equipId, receiverId);
            return;
        }
        const bool byReceiver = receiverId > 0;
        const char* const keyName = byReceiver ? "receiverId" : "equipId";
        const int keyValue = byReceiver ? receiverId : equipId;
        if (byReceiver && receiverId == kUndeclaredReceiverId)
            LogDebug("[RemoteMissile] receiverId=%d is the id SetGunBasic "
                     "substitutes for a weapon that declares no receiver of its "
                     "own, so this registration also covers every under-declared "
                     "custom weapon in every installed pack\n", receiverId);
        MissileSpec spec{};
        if (hasAttackId)
        {
            if (attackId <= 0 || attackId > kDescAttackIdMask)
                LogDebug("[RemoteMissile] %s=%d attackId=%d rejected: a "
                         "shell descriptor carries only 10 bits of attack id, "
                         "so a value outside 1..1023 would land on a different "
                         "damage row - the receiver's own attack id is used "
                         "instead\n",
                         keyName, keyValue, attackId);
            else
            {
                spec.attackId = attackId;
                spec.hasAttackId = true;
            }
        }
        spec.fireVoiceId = fireVoiceId;
        double seconds = maxFlightSeconds;
        if (seconds < 1.0) seconds = 1.0;
        if (seconds > 90.0) seconds = 90.0;
        spec.maxFlightSeconds = static_cast<float>(seconds);

        double range = maxRange;
        if (range < 0.0) range = 0.0;
        if (range > 100000.0) range = 100000.0;
        spec.maxRange = static_cast<float>(range);

        double chase = cameraDistance;
        if (chase < 0.0) chase = 0.0;
        if (chase > 50.0) chase = 50.0;
        spec.cameraDistance = static_cast<float>(chase);

        double rise = cameraHeight;
        if (rise < -50.0) rise = -50.0;
        if (rise > 50.0) rise = 50.0;
        spec.cameraHeight = static_cast<float>(rise);

        double low = minSpeed;
        double high = maxSpeed;
        if (low < 0.0) low = 0.0;
        if (high < 0.0) high = 0.0;
        if (low > 100.0) low = 100.0;
        if (high > 100.0) high = 100.0;
        if (low > 0.0 && high > 0.0 && low > high)
        {
            LogDebug("[RemoteMissile] %s=%d minSpeed=%.1f is above maxSpeed=%.1f, "
                     "so the two would fight each frame - minSpeed was lowered to "
                     "match\n", keyName, keyValue, low, high);
            low = high;
        }
        spec.minSpeed = static_cast<float>(low);
        spec.maxSpeed = static_cast<float>(high);
        if (spec.maxSpeed > kShellBankInvertSpeed)
            LogDebug("[RemoteMissile] %s=%d maxSpeed=%.1f is above %.0f, where "
                     "the engine's own model-bank scale turns negative - the missile "
                     "leans the wrong way and several times too far while you steer. "
                     "Flight and steering themselves are unaffected\n",
                     keyName, keyValue, spec.maxSpeed, kShellBankInvertSpeed);
        if (spec.maxSpeed > 0.0f && spec.maxSpeed <= kShellEffectSpeedThreshold)
            LogDebug("[RemoteMissile] %s=%d maxSpeed=%.1f holds the shell at or "
                     "under the engine's own %.0f effect threshold, so the shell "
                     "system never starts the exhaust effect - raise maxSpeed above "
                     "%.0f if you want it\n",
                     keyName, keyValue, spec.maxSpeed, kShellEffectSpeedThreshold,
                     kShellEffectSpeedThreshold);
        float bandLo = kShellSpeedFloor;
        float bandHi = kShellSpeedCeiling;
        bool banded = false;
        ResolveSpeedBand(spec, &bandLo, &bandHi, &banded);
        if (banded && !(bandHi > bandLo))
            LogDebug("[RemoteMissile] %s=%d minSpeed and maxSpeed are both %.1f, "
                     "so the shell flies at one fixed speed and the brake and boost "
                     "triggers have nothing to move it within\n",
                     keyName, keyValue, bandLo);

        {
            std::lock_guard<std::recursive_mutex> lock(g_Mutex);
            if (byReceiver)
                g_SpecByReceiverId[receiverId] = spec;
            else
                g_SpecByEquipId[equipId] = spec;
        }

        Log("[RemoteMissile] %s=%d registered: "
            "maxFlight=%.0fs range=%.0fm (0 = unlimited) camera=%.1fm behind %.1fm up "
            "speed=%.1f..%.1f -> flies %.1f..%.1f "
            "(0 = leave the engine's own %.0f..%.0f band) - firing this weapon now "
            "launches a steerable shell instead of letting the vanilla one fly on its "
            "own\n",
            keyName, keyValue, spec.maxFlightSeconds,
            spec.maxRange, spec.cameraDistance, spec.cameraHeight,
            spec.minSpeed, spec.maxSpeed,
            banded ? bandLo : kShellSpeedFloor,
            banded ? bandHi : kShellSpeedCeiling,
            kShellSpeedFloor, kShellSpeedCeiling);

        if (spec.hasAttackId)
            Log("[RemoteMissile] %s=%d attack id 0x%X armed - the blast "
                "and the direct hit are charged to that damage row instead of "
                "the one the receiver carries\n",
                keyName, keyValue, spec.attackId);

        if (spec.fireVoiceId != 0)
            Log("[RemoteMissile] %s=%d fire voice 0x%08X armed - the player "
                "shouts this clip when the weapon fires, the way the Rocket Arm "
                "shouts its own. A clip the active player voice type does not "
                "carry is queued and dropped in silence\n",
                keyName, keyValue, spec.fireVoiceId);
    }

    bool RemoteMissile_IsFlying()
    {
        return g_Flying;
    }

    void RemoteMissile_AbortWithoutHandoff()
    {
        std::uint8_t* shellSystem = ResolveShellSystemSEH();
        if (shellSystem && IsExistSEH(shellSystem))
            CancelSEH(shellSystem);
        ReleaseControl("cancelled");
    }

    void RemoteMissile_Abort()
    {
        std::uint8_t* shellSystem = ResolveShellSystemSEH();
        if (shellSystem && IsExistSEH(shellSystem))
        {
            HandOffReleasedShell(shellSystem);
            CancelSEH(shellSystem);
        }
        ReleaseControl("cancelled");
    }

    void RemoteMissile_ArmShot(int playerIndex, int ownerEquipId)
    {
        tls_ShotArmed = false;
        if (MissionCodeGuard::ShouldBypassHooks())
            return;

        MissileSpec spec{};
        bool haveSpec = false;
        bool anyReceiverKeys = false;
        {
            std::lock_guard<std::recursive_mutex> lock(g_Mutex);
            auto it = g_SpecByEquipId.find(ownerEquipId);
            if (it != g_SpecByEquipId.end())
            {
                spec = it->second;
                haveSpec = true;
            }
            anyReceiverKeys = !g_SpecByReceiverId.empty();
        }

        if (!haveSpec)
        {
            if (!anyReceiverKeys)
                return;
            AuditReceiverKeysOnce();
            const int rc = EquipParam_GetReceiverForEquipId(ownerEquipId);
            if (rc <= 0)
                return;
            std::lock_guard<std::recursive_mutex> lock(g_Mutex);
            auto rit = g_SpecByReceiverId.find(rc);
            if (rit == g_SpecByReceiverId.end())
                return;
            spec = rit->second;
        }

        std::uint8_t* shellSystem = ResolveShellSystemSEH();
        if (!shellSystem)
            return;
        if (!EnsureFireHook(shellSystem))
            return;
        EnsureExecuteHook(shellSystem);
        if (IsExistSEH(shellSystem))
        {
            LogDebug("[RemoteMissile] shot left vanilla: a steerable shell is already "
                     "in flight and the engine tracks only one\n");
            return;
        }

        g_ArmedSpec = spec;
        g_ArmedPlayerIndex = playerIndex;
        g_ArmedEquipId = ownerEquipId;
        tls_ShotArmed = true;
    }

    void RemoteMissile_DisarmShot()
    {
        tls_ShotArmed = false;
    }

    void RemoteMissile_Tick(float deltaSeconds)
    {
        if (!g_Flying)
            return;

        if (MissionCodeGuard::ShouldBypassHooks())
        {
            RemoteMissile_AbortWithoutHandoff();
            return;
        }

        std::uint8_t* shellSystem = ResolveShellSystemSEH();
        if (shellSystem && EngineSkippedFrameSEH(shellSystem))
            return;
        if (!shellSystem || !IsExistSEH(shellSystem))
        {
            ReleaseControl("the engine reports no rocket-punch shell in flight");
            return;
        }

        const unsigned long long elapsedMs = GetTickCount64() - g_LaunchTickMs;
        if (elapsedMs > static_cast<unsigned long long>(
                g_ActiveSpec.maxFlightSeconds * 1000.0f))
        {
            Log("[RemoteMissile] flight cancelled: the watchdog reached %.1f seconds, "
                "so control is being returned to the player\n",
                g_ActiveSpec.maxFlightSeconds);
            RemoteMissile_Abort();
            return;
        }

        SuppressLeashSEH(shellSystem);

        const float remainingSeconds = g_ActiveSpec.maxFlightSeconds
            - static_cast<float>(elapsedMs) * 0.001f + kLifetimeHeadroomSeconds;
        ReassertLifetimeSEH(shellSystem, remainingSeconds);

        std::uint8_t* pad = PlayerSubsystemSEH(kSubsystemPad);
        StickInput input{};
        if (pad)
        {
            ReadStickSEH(pad, &input);
            if (g_InputMasked)
                SetInputMaskSEH(pad, true);
        }

        float dt = deltaSeconds;
        if (!(dt > 0.0f) || dt > 0.25f)
            dt = 1.0f / 60.0f;
        if (g_SpeedBanded)
        {
            AdvanceSpeedBand(input, dt);
            WriteShellSpeedSEH(shellSystem, ShellSpeedShadow());
        }

        OperateSEH(shellSystem, input, dt);

        if (g_SpeedBanded)
            WriteShellSpeedSEH(shellSystem, g_Speed);

        alignas(16) float matrix[16] = {};
        const char* cameraState = "no matrix";
        if (GetCameraMatrixSEH(shellSystem, matrix))
        {
            if (g_TickCount == 0)
            {
                cameraState = "held for one frame (the shell has not written its "
                              "own camera pose yet)";
            }
            else
            {
                if (g_ShellPosSamples > 0)
                {
                    g_PrevShellPos[0] = g_LastShellPos[0];
                    g_PrevShellPos[1] = g_LastShellPos[1];
                    g_PrevShellPos[2] = g_LastShellPos[2];
                }
                g_LastShellPos[0] = matrix[12];
                g_LastShellPos[1] = matrix[13];
                g_LastShellPos[2] = matrix[14];
                g_LastStepSeconds = dt;
                ++g_ShellPosSamples;

                if (!g_LaunchPosValid)
                {
                    g_LaunchPos[0] = matrix[12];
                    g_LaunchPos[1] = matrix[13];
                    g_LaunchPos[2] = matrix[14];
                    g_LaunchPosValid = true;
                }
                else if (g_ActiveSpec.maxRange > 0.0f)
                {
                    const float dx = matrix[12] - g_LaunchPos[0];
                    const float dy = matrix[13] - g_LaunchPos[1];
                    const float dz = matrix[14] - g_LaunchPos[2];
                    const float travelled =
                        std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (travelled > g_ActiveSpec.maxRange)
                    {
                        Log("[RemoteMissile] flight cancelled: the shell reached "
                            "%.0f m from the launch point and maxRange is "
                            "%.0f m\n",
                            travelled, g_ActiveSpec.maxRange);
                        RemoteMissile_Abort();
                        return;
                    }
                }

                if (g_ActiveSpec.cameraDistance > 0.0f
                    || g_ActiveSpec.cameraHeight != 0.0f)
                    OffsetChaseCamera(matrix, g_ActiveSpec.cameraDistance,
                                      g_ActiveSpec.cameraHeight);

                std::uint8_t* camera = PlayerSubsystemSEH(kSubsystemCamera);
                if (!camera)
                    cameraState = "NO OBJECT at player subsystem +0xB8";
                else if (InstallCameraSEH(camera, matrix))
                    cameraState = "installed";
                else
                    cameraState = "INSTALL FAULTED";
            }
        }

        ++g_TickCount;
        if (!g_FirstTickLogged)
        {
            g_FirstTickLogged = true;
            Log("[RemoteMissile] first steer tick: dt=%.4f stick=(%.2f %.2f) "
                "brake=%.2f boost=%.2f pad=%s camera=%s attachMode=%u "
                "lookOffsetWas=(%.3f %.3f) chase=%.1fm rise=%.1fm speed=%.1f "
                "brakeArmed=%s shellCamera=(%.1f %.1f %.1f) band=%.1f..%.1f "
                "agilityShadow=%.1f rates=boost %+.1f brake %+.1f drag %+.1f per "
                "second - attachMode must read 4, and a non-zero lookOffsetWas is the "
                "player's own aim angle that was tilting the view off the shell's "
                "axis\n",
                dt, input.leftX, input.leftY, input.brake, input.boost,
                pad ? "bound" : "MISSING",
                cameraState, g_AttachModeReadback,
                g_ManualOffsetAtTakeOver[0], g_ManualOffsetAtTakeOver[1],
                g_ActiveSpec.cameraDistance, g_ActiveSpec.cameraHeight,
                ReadShellSpeedSEH(shellSystem), g_BrakeArmed ? "yes" : "no",
                matrix[12], matrix[13], matrix[14],
                g_SpeedBanded ? g_SpeedLo : kShellSpeedFloor,
                g_SpeedBanded ? g_SpeedHi : kShellSpeedCeiling,
                g_SpeedBanded ? ShellSpeedShadow() : ReadShellSpeedSEH(shellSystem),
                kShellSpeedBoostRate * SpeedBandScale(),
                -kShellSpeedBrakeRate * SpeedBandScale(),
                -kShellSpeedDragRate * SpeedBandScale());
        }
    }

    bool Install_RemoteMissile_Hooks()
    {
        g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
            ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!g_GetQuarkSystemTable)
        {
            Log("[RemoteMissile] not installed: GetQuarkSystemTable is unresolved on "
                "this build, so the shell system cannot be reached\n");
            return true;
        }
        return true;
    }

    void Uninstall_RemoteMissile_Hooks()
    {
        RemoteMissile_AbortWithoutHandoff();
        if (g_ShellFireTarget)
        {
            DisableAndRemoveHook(g_ShellFireTarget);
            g_ShellFireTarget = nullptr;
            g_OrigShellFire = nullptr;
        }
        g_FireHookAttempted = false;
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        g_SpecByEquipId.clear();
        g_SpecByReceiverId.clear();
        g_PlayerContext = nullptr;
        g_GetQuarkSystemTable = nullptr;
    }
}

int __cdecl l_SetRemoteMissile(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    int equipId = 0;
    LuaGetField(L, 1, "equipId");
    if (LuaIsNumber(L, -1)) equipId = GetLuaInt(L, -1);
    g_lua_settop(L, -2);

    int receiverId = 0;
    LuaGetField(L, 1, "receiverId");
    if (LuaIsNumber(L, -1)) receiverId = GetLuaInt(L, -1);
    g_lua_settop(L, -2);

    int attackId = 0;
    bool hasAttackId = false;
    LuaGetField(L, 1, "attackId");
    if (LuaIsNumber(L, -1))
    {
        attackId = GetLuaInt(L, -1);
        hasAttackId = true;
    }
    g_lua_settop(L, -2);

    double maxFlightSeconds = 20.0;
    LuaGetField(L, 1, "maxFlightSeconds");
    if (LuaIsNumber(L, -1)) maxFlightSeconds = GetLuaNumber(L, -1);
    g_lua_settop(L, -2);

    double maxRange = 0.0;
    LuaGetField(L, 1, "maxRange");
    if (LuaIsNumber(L, -1)) maxRange = GetLuaNumber(L, -1);
    g_lua_settop(L, -2);

    double cameraDistance = 4.0;
    LuaGetField(L, 1, "cameraDistance");
    if (LuaIsNumber(L, -1)) cameraDistance = GetLuaNumber(L, -1);
    g_lua_settop(L, -2);

    double cameraHeight = 0.0;
    LuaGetField(L, 1, "cameraHeight");
    if (LuaIsNumber(L, -1)) cameraHeight = GetLuaNumber(L, -1);
    g_lua_settop(L, -2);

    double minSpeed = 0.0;
    LuaGetField(L, 1, "minSpeed");
    if (LuaIsNumber(L, -1)) minSpeed = GetLuaNumber(L, -1);
    g_lua_settop(L, -2);

    double maxSpeed = 0.0;
    LuaGetField(L, 1, "maxSpeed");
    if (LuaIsNumber(L, -1)) maxSpeed = GetLuaNumber(L, -1);
    g_lua_settop(L, -2);

    std::uint32_t fireVoiceId = 0;
    LuaGetField(L, 1, "fireVoiceId");
    if (LuaIsNumber(L, -1) || LuaIsString(L, -1))
        fireVoiceId = GetLuaFnvHash32Arg(L, -1);
    g_lua_settop(L, -2);

    shell::RemoteMissile_Register(equipId, receiverId, attackId, hasAttackId,
                                  maxFlightSeconds, maxRange, cameraDistance,
                                  cameraHeight, minSpeed, maxSpeed,
                                  fireVoiceId);
    return 0;
}

int __cdecl l_AbortRemoteMissile(lua_State* L)
{
    (void)L;
    shell::RemoteMissile_Abort();
    return 0;
}

int __cdecl l_IsRemoteMissileFlying(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;
    g_lua_pushboolean(L, shell::RemoteMissile_IsFlying() ? 1 : 0);
    return 1;
}
