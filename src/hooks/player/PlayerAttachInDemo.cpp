#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <mutex>
#include <string>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "GetGameObjectIdWithIndex.h"
#include "MissionCodeGuard.h"
#include "PlayerAttachInDemo.h"
#include "log.h"

namespace
{
    using OnPlayingAfterUpdateStream_t = void(__fastcall*)(void* playback);
    using DemoState_t  = std::uint32_t(__fastcall*)(void* playback);
    using QuarkTable_t = std::uint8_t* (__fastcall*)();
    using GetCommunication_t = void* (__fastcall*)(void* self, std::int32_t* err, void** out,
                                                   std::uint16_t instanceIndex);
    using GetVehicleCnpWorld_t = void* (__fastcall*)(void* info, std::int32_t* err,
                                                     float* outMatrix, std::uint32_t cnpHash);
    using CheckStatusBit_t = bool(__fastcall*)(std::uint32_t statusId);
    using DemoActionExecute_t = void(__fastcall*)(void* self, std::uint32_t phase,
                                                 std::uint32_t gameObjectIndex);
    using DemoActionOnSignal_t = void(__fastcall*)(void* self, std::uint32_t gameObjectIndex,
                                                  void* err, std::uint64_t* signal);
    using UpdateStreamMotion_t = void(__fastcall*)(void* animController, std::uint32_t index,
                                                   const void* transformRT);
    using IsPlaying_t = bool(__fastcall*)(void* demoAnimController, std::uint32_t index);
    using Warp_t = void(__fastcall*)(void* self, std::uint32_t instanceIndex,
                                     const float* pos, const float* rot, bool waitForBlock);
    using GetSharedInstance_t = void* (__fastcall*)();
    using GetTypeObject_t = void* (__fastcall*)(void* manager, std::uint16_t gameObjectId);
    using TypeCnpWorld_t = bool(__fastcall*)(void* controller, std::uint32_t instanceIndex,
                                            std::uint32_t cnpHash, float* outMatrix);

    enum class OwnerKind : std::uint8_t
    {
        Vehicle,
        Horse,
        Heli,
        WalkerGear,
        Other
    };

    constexpr std::uint32_t kInvalidId         = 0xFFFFu;
    constexpr std::uint32_t kInstanceMask      = 0x1FFu;
    constexpr std::size_t   kQuarkAppSystem    = 0x98;
    constexpr std::size_t   kAppVehicleSystem  = 0x2A8;
    constexpr std::size_t   kGetCommunicationSlot = 0x18;
    bool IsLocalPlayerActor(void* self, std::uint32_t gameObjectIndex);

    constexpr std::size_t   kPlayerSubObject   = 0x20;
    constexpr std::size_t   kPlayerWarpSlot    = 0x1B0;
    constexpr std::uint32_t kStatusUnconscious = 0x67;
    constexpr std::uint32_t kTypeIdShift        = 9;
    constexpr std::size_t   kManagerGetTypeSlot = 0x20;
    constexpr std::size_t   kTypeWrapperOffset  = 0x08;
    constexpr std::size_t   kTypeIdOffset       = 0x1C;
    constexpr std::size_t   kWrapperTypeOffset  = 0x20;
    constexpr std::size_t   kMaxRealizedChunks  = 64;
    constexpr std::size_t   kSharedConnectPointSlot = 0x370;
    constexpr std::size_t   kSharedControllerBase   = 0x0C;
    constexpr std::size_t   kSharedControllerCount  = 0x08;
    constexpr std::size_t   kHeliConnectPointSlot   = 0x130;
    constexpr std::size_t   kHeliControllerBase     = 0x6C;
    constexpr std::size_t   kHeliControllerCount    = 0x68;

    constexpr std::uint32_t kDemoPhaseAnimation      = 2;
    constexpr std::ptrdiff_t kExecOwner              = -0x28;
    constexpr std::size_t   kExecDemoAnimController  = 0x18;
    constexpr std::size_t   kOwnerAnimHolder         = 0x78;
    constexpr std::size_t   kAnimHolderController    = 0x238;
    constexpr std::size_t   kOwnerPlayerSub          = 0x138;
    constexpr std::size_t   kPlayerSubLocalIndex     = 0x218;
    constexpr std::size_t   kDemoAnimIsPlayingSlot   = 0x18;
    constexpr std::size_t   kUpdateStreamMotionSlot  = 0x330;

    constexpr std::uint64_t kSignalIdMask         = 0xFFFFFFFFFFFFull;
    constexpr std::uint64_t kSignalDemoFinish     = 0x650a0778cc01ull;
    constexpr std::uint64_t kLastStateRideHorse   = 0x78f0576b4ae7ull;
    constexpr std::uint64_t kLastStateRideHeli    = 0x9c2c350c1ad2ull;
    constexpr std::uint64_t kLastStateRideWalker  = 0x212c94e8edb6ull;
    constexpr std::size_t   kSignalLastStateSlot  = 8;
    constexpr std::size_t   kSignalOpponentOffset = 0x50;
    constexpr std::uint64_t kHandoffWindowMs      = 3000;

    OnPlayingAfterUpdateStream_t g_OrigOnPlayingAfterUpdateStream = nullptr;
    DemoState_t                  g_OrigDoFinish    = nullptr;
    DemoState_t                  g_OrigDoInterrupt = nullptr;
    void*                        g_HookTarget      = nullptr;
    void*                        g_HookFinish      = nullptr;
    void*                        g_HookInterrupt   = nullptr;
    DemoActionExecute_t          g_OrigDemoActionExecute = nullptr;
    void*                        g_HookDemoExecute = nullptr;
    DemoActionOnSignal_t         g_OrigDemoActionOnSignal = nullptr;
    void*                        g_HookDemoOnSignal = nullptr;

    std::mutex        g_Mutex;
    std::string       g_OwnerName;
    std::uint32_t     g_OwnerObjectId   = kInvalidId;
    std::uint32_t     g_ConnectPointId  = 0;
    bool              g_UnattachOnSleep = false;
    OwnerKind         g_OwnerKind       = OwnerKind::Vehicle;
    std::atomic<bool> g_Active{ false };
    std::atomic<std::uint64_t> g_HandoffDeadlineMs{ 0 };
    std::atomic<std::uint64_t> g_StreamPlacedAtMs{ 0 };
    std::atomic<std::int64_t>  g_PerFrameHookQpc{ 0 };

    constexpr std::uint64_t kStreamLiveWindowMs = 40;

    std::atomic<std::int64_t> g_DemoEndQpc{ 0 };
    std::atomic<bool>         g_InOwnWarp{ false };
    void**                    g_WarpSlot = nullptr;
    Warp_t                    g_OrigWarp = nullptr;
    constexpr double          kDemoEndWatchMs   = 3000.0;

    std::int64_t QpcNow();
    double       QpcToMs(std::int64_t ticks);

    alignas(16) float         g_MissionWarpPos[4] = {};
    alignas(16) float         g_MissionWarpRot[4] = {};
    std::atomic<bool>         g_MissionWarpPending{ false };

    float Distance3(const float* a, const float* b);

    bool  g_TuneCamFix = true;

    void ReadAttachTuning()
    {
        g_TuneCamFix = true;
        char path[MAX_PATH]{};
        if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
            return;
        char* lastSlash = std::strrchr(path, '\\');
        if (!lastSlash)
            return;
        *(lastSlash + 1) = '\0';
        if (strcat_s(path, "mod\\V_FrameWork\\attach_tuning.txt") != 0)
            return;
        FILE* f = nullptr;
        if (fopen_s(&f, path, "r") != 0 || !f)
            return;
        char line[128];
        while (std::fgets(line, sizeof(line), f))
        {
            char* eq = std::strchr(line, '=');
            if (!eq)
                continue;
            *eq = '\0';
            const char* key = line;
            const float value = static_cast<float>(std::atof(eq + 1));
            if (std::strncmp(key, "camfix", 6) == 0)
                g_TuneCamFix = value != 0.0f;
        }
        std::fclose(f);
    }

    std::uintptr_t MainViewportGlobalAddress()
    {
        using GB = AddressSetRuntime::GameBuild;
        switch (gGameBuild)
        {
        case GB::En_1_0_15_4:
        case GB::En_1_0_15_4a: return 0x142BE1280ull;
        case GB::Jp_1_0_15_4:
        case GB::Jp_1_0_15_4a: return 0x142BE1280ull;
        case GB::En_1_0_15_3:  return 0x142BE1260ull;
        case GB::Jp_1_0_15_3:  return 0x142BE1260ull;
        default:               return 0;
        }
    }

    constexpr std::size_t    kViewportCamera    = 0x570;
    constexpr std::size_t    kCameraWorldMatrix = 0x30;

    constexpr std::size_t    kCameraDaemonCount   = 0x88;
    constexpr std::size_t    kCameraDaemonCameras = 0x90;
    constexpr std::size_t    kGkCameraEnabled     = 0xE0;
    constexpr std::size_t    kGkCameraQuat        = 0x120;
    constexpr std::size_t    kGkCameraPos         = 0x130;
    constexpr std::size_t    kGrCameraViewMatrix  = 0x70;
    constexpr std::size_t    kPlaybackAnchorScale = 0x190;
    constexpr std::size_t    kPlaybackAnchorQuat  = 0x1A0;
    constexpr std::size_t    kPlaybackAnchorPos   = 0x1B0;
    constexpr float          kAnchorMatchMeters   = 0.02f;
    constexpr float          kAnchorMatchQuatDot  = 0.999f;
    constexpr unsigned char  kCameraCommitPrologue[] = { 0x48, 0x8B, 0xC4, 0x55, 0x41, 0x54,
                                                         0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
    constexpr unsigned char  kCameraCommitPrologue153[] = { 0x48, 0x89, 0xE0, 0x55, 0x41, 0x54,
                                                            0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
    static_assert(sizeof(kCameraCommitPrologue153) == sizeof(kCameraCommitPrologue));

    using CameraCommit_t = void(__fastcall*)(void* daemon, void* a2, void* a3, void* a4);
    CameraCommit_t     g_OrigCameraCommit = nullptr;
    void*              g_HookCameraCommit = nullptr;
    std::atomic<void*> g_Playback{ nullptr };

    void MatrixToQuaternion(const float* m16, float* outXYZW);

    enum class AnchorMode : std::uint8_t
    {
        Unknown,
        Full,
        YawOnly,
        TranslationOnly
    };

    struct CamFix
    {
        bool         valid = false;
        float        dq[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        float        dt[3] = {};
        std::int64_t writeQpc = 0;
    };

    std::mutex   g_CamFixMutex;
    CamFix       g_CamFix;
    AnchorMode   g_AnchorMode = AnchorMode::Unknown;
    float        g_PrevWriteQuat[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float        g_PrevWritePos[3] = {};
    float        g_PrevPrevWriteQuat[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float        g_PrevPrevWritePos[3] = {};
    unsigned     g_CamFixWriteCount = 0;
    std::atomic<std::int64_t> g_LastCommitQpc{ 0 };
    float        g_LastPatchedQuat[4] = {};
    float        g_LastPatchedPos[3] = {};
    bool         g_PatchedOnce = false;
    bool         g_ReportedGrPatchFailed = false;

    void ResetCamFixState()
    {
        {
            std::lock_guard<std::mutex> lock(g_CamFixMutex);
            g_CamFix = CamFix{};
        }
        g_AnchorMode = AnchorMode::Unknown;
        g_CamFixWriteCount = 0;
        g_PatchedOnce = false;
        g_ReportedGrPatchFailed = false;
    }

    void QuatMul(const float* a, const float* b, float* out)
    {
        out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    }

    void QuatNormalize(float* q)
    {
        const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (len < 1e-6f)
        {
            q[0] = 0.0f;
            q[1] = 0.0f;
            q[2] = 0.0f;
            q[3] = 1.0f;
            return;
        }
        for (int i = 0; i < 4; ++i)
            q[i] /= len;
    }

    void QuatRotate(const float* q, const float* v, float* out)
    {
        const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
        const float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
        const float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
        out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
        out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
        out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
    }

    void QuatToMatrix3(const float* q, float* r9)
    {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        r9[0] = 1.0f - 2.0f * (y * y + z * z);
        r9[1] = 2.0f * (x * y + z * w);
        r9[2] = 2.0f * (x * z - y * w);
        r9[3] = 2.0f * (x * y - z * w);
        r9[4] = 1.0f - 2.0f * (x * x + z * z);
        r9[5] = 2.0f * (y * z + x * w);
        r9[6] = 2.0f * (x * z + y * w);
        r9[7] = 2.0f * (y * z - x * w);
        r9[8] = 1.0f - 2.0f * (x * x + y * y);
    }

    void YawOnlyQuat(const float* q, float* out)
    {
        const float fx = 2.0f * (q[0] * q[2] + q[1] * q[3]);
        const float fz = 1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]);
        const float yaw = std::atan2(fx, fz);
        out[0] = 0.0f;
        out[1] = std::sin(yaw * 0.5f);
        out[2] = 0.0f;
        out[3] = std::cos(yaw * 0.5f);
    }

    float QuatDotAbs(const float* a, const float* b)
    {
        return std::fabs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
    }

    bool ReadPlaybackAnchor(void* playback, float* outQuat, float* outPos, float* outScale)
    {
        __try
        {
            auto* base = reinterpret_cast<const std::uint8_t*>(playback);
            const float* scale = reinterpret_cast<const float*>(base + kPlaybackAnchorScale);
            const float* quat  = reinterpret_cast<const float*>(base + kPlaybackAnchorQuat);
            const float* pos   = reinterpret_cast<const float*>(base + kPlaybackAnchorPos);
            for (int i = 0; i < 4; ++i)
            {
                if (!std::isfinite(scale[i]) || !std::isfinite(quat[i]) || !std::isfinite(pos[i]))
                    return false;
            }
            const float qLen = quat[0] * quat[0] + quat[1] * quat[1] + quat[2] * quat[2]
                               + quat[3] * quat[3];
            if (qLen < 0.9f || qLen > 1.1f)
                return false;
            for (int i = 0; i < 3; ++i)
            {
                if (scale[i] < 0.5f || scale[i] > 2.0f)
                    return false;
                outScale[i] = scale[i];
                outPos[i] = pos[i];
            }
            for (int i = 0; i < 4; ++i)
                outQuat[i] = quat[i];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ApplyFixToGrCamera(const float* dq, const float* dt)
    {
        auto** mainViewport = reinterpret_cast<std::uint8_t**>(
            ResolveGameAddress(MainViewportGlobalAddress()));
        if (!mainViewport)
            return false;
        __try
        {
            auto* viewport = *mainViewport;
            if (!viewport)
                return false;
            auto* camera = *reinterpret_cast<std::uint8_t**>(viewport + kViewportCamera);
            if (!camera)
                return false;
            float* world = reinterpret_cast<float*>(camera + kCameraWorldMatrix);
            float* view  = reinterpret_cast<float*>(camera + kGrCameraViewMatrix);
            for (int i = 0; i < 16; ++i)
            {
                if (!std::isfinite(world[i]))
                    return false;
            }
            float r[9];
            QuatToMatrix3(dq, r);
            float fresh[16];
            for (int c = 0; c < 4; ++c)
            {
                const float vx = world[c * 4 + 0];
                const float vy = world[c * 4 + 1];
                const float vz = world[c * 4 + 2];
                fresh[c * 4 + 0] = r[0] * vx + r[3] * vy + r[6] * vz;
                fresh[c * 4 + 1] = r[1] * vx + r[4] * vy + r[7] * vz;
                fresh[c * 4 + 2] = r[2] * vx + r[5] * vy + r[8] * vz;
                fresh[c * 4 + 3] = world[c * 4 + 3];
            }
            fresh[12] += dt[0];
            fresh[13] += dt[1];
            fresh[14] += dt[2];
            float inv[16];
            for (int c = 0; c < 3; ++c)
            {
                for (int rr = 0; rr < 3; ++rr)
                    inv[c * 4 + rr] = fresh[rr * 4 + c];
                inv[c * 4 + 3] = 0.0f;
            }
            for (int rr = 0; rr < 3; ++rr)
            {
                inv[12 + rr] = -(fresh[rr * 4 + 0] * fresh[12] + fresh[rr * 4 + 1] * fresh[13]
                                 + fresh[rr * 4 + 2] * fresh[14]);
            }
            inv[15] = 1.0f;
            for (int i = 0; i < 16; ++i)
            {
                world[i] = fresh[i];
                view[i] = inv[i];
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void NoteCamFixAtWrite(const float* matrix16)
    {
        float q[4];
        MatrixToQuaternion(matrix16, q);
        QuatNormalize(q);
        const float p[3] = { matrix16[12], matrix16[13], matrix16[14] };

        float prevQ[4], prevPrevQ[4];
        float prevP[3], prevPrevP[3];
        for (int i = 0; i < 4; ++i)
        {
            prevQ[i] = g_PrevWriteQuat[i];
            prevPrevQ[i] = g_PrevPrevWriteQuat[i];
            g_PrevPrevWriteQuat[i] = g_PrevWriteQuat[i];
            g_PrevWriteQuat[i] = q[i];
        }
        for (int i = 0; i < 3; ++i)
        {
            prevP[i] = g_PrevWritePos[i];
            prevPrevP[i] = g_PrevPrevWritePos[i];
            g_PrevPrevWritePos[i] = g_PrevWritePos[i];
            g_PrevWritePos[i] = p[i];
        }
        const unsigned written = g_CamFixWriteCount;
        if (g_CamFixWriteCount < 1000000u)
            ++g_CamFixWriteCount;

        if (!g_TuneCamFix || written < 2)
            return;

        void* playback = g_Playback.load(std::memory_order_relaxed);
        float aq[4], ap[3], ascale[3];
        if (!playback || !ReadPlaybackAnchor(playback, aq, ap, ascale))
        {
            std::lock_guard<std::mutex> lock(g_CamFixMutex);
            g_CamFix.valid = false;
            return;
        }

        const float* matchQ = nullptr;
        if (Distance3(ap, prevP) < kAnchorMatchMeters)
            matchQ = prevQ;
        else if (Distance3(ap, prevPrevP) < kAnchorMatchMeters)
            matchQ = prevPrevQ;
        else
        {
            std::lock_guard<std::mutex> lock(g_CamFixMutex);
            g_CamFix.valid = false;
            return;
        }

        if (g_AnchorMode == AnchorMode::Unknown)
        {
            float yawQ[4];
            YawOnlyQuat(matchQ, yawQ);
            if (QuatDotAbs(aq, matchQ) > kAnchorMatchQuatDot)
                g_AnchorMode = AnchorMode::Full;
            else if (QuatDotAbs(aq, yawQ) > kAnchorMatchQuatDot)
                g_AnchorMode = AnchorMode::YawOnly;
            else
                g_AnchorMode = AnchorMode::TranslationOnly;
        }

        float tq[4] = { q[0], q[1], q[2], q[3] };
        if (g_AnchorMode == AnchorMode::YawOnly)
            YawOnlyQuat(q, tq);
        else if (g_AnchorMode == AnchorMode::TranslationOnly)
        {
            for (int i = 0; i < 4; ++i)
                tq[i] = aq[i];
        }
        const float aqConj[4] = { -aq[0], -aq[1], -aq[2], aq[3] };
        float dq[4];
        QuatMul(tq, aqConj, dq);
        QuatNormalize(dq);
        float rotatedAnchor[3];
        QuatRotate(dq, ap, rotatedAnchor);
        float dt[3];
        for (int i = 0; i < 3; ++i)
            dt[i] = p[i] - rotatedAnchor[i];

        const std::int64_t now = QpcNow();
        {
            std::lock_guard<std::mutex> lock(g_CamFixMutex);
            g_CamFix.valid = true;
            for (int i = 0; i < 4; ++i)
                g_CamFix.dq[i] = dq[i];
            for (int i = 0; i < 3; ++i)
                g_CamFix.dt[i] = dt[i];
            g_CamFix.writeQpc = now;
        }

        const std::int64_t tickQpc = g_PerFrameHookQpc.load(std::memory_order_relaxed);
        const std::int64_t commitQpc = g_LastCommitQpc.load(std::memory_order_relaxed);
        if (!g_HookCameraCommit || commitQpc > tickQpc)
        {
            if (ApplyFixToGrCamera(dq, dt))
            {
                std::lock_guard<std::mutex> lock(g_CamFixMutex);
                g_CamFix.valid = false;
            }
            else if (!g_ReportedGrPatchFailed)
            {
                g_ReportedGrPatchFailed = true;
                LogWarn("AttachInDemo: viewport camera patch failed, camera fix skipped on early-commit frames");
            }
        }
    }

    int PatchActiveGkCamera(void* daemon, const CamFix* fix)
    {
        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(daemon);
            const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(base + kCameraDaemonCount);
            auto** cameras = *reinterpret_cast<std::uint8_t***>(base + kCameraDaemonCameras);
            std::uint8_t* active = nullptr;
            for (std::uint32_t i = 0; cameras && i < count && i < 64; ++i)
            {
                if (cameras[i] && cameras[i][kGkCameraEnabled] != 0)
                {
                    active = cameras[i];
                    break;
                }
            }
            if (!active)
                return -1;
            float* quat = reinterpret_cast<float*>(active + kGkCameraQuat);
            float* pos  = reinterpret_cast<float*>(active + kGkCameraPos);
            bool finite = true;
            for (int i = 0; i < 4; ++i)
                finite = finite && std::isfinite(quat[i]) && std::isfinite(pos[i]);
            const float qLen = quat[0] * quat[0] + quat[1] * quat[1] + quat[2] * quat[2]
                               + quat[3] * quat[3];
            if (!finite || qLen < 0.9f || qLen > 1.1f)
                return -1;
            bool untouched = g_PatchedOnce;
            for (int i = 0; i < 4 && untouched; ++i)
                untouched = quat[i] == g_LastPatchedQuat[i];
            for (int i = 0; i < 3 && untouched; ++i)
                untouched = pos[i] == g_LastPatchedPos[i];
            if (untouched)
                return 0;
            float nq[4];
            QuatMul(fix->dq, quat, nq);
            QuatNormalize(nq);
            float np[3];
            QuatRotate(fix->dq, pos, np);
            for (int i = 0; i < 4; ++i)
            {
                quat[i] = nq[i];
                g_LastPatchedQuat[i] = nq[i];
            }
            for (int i = 0; i < 3; ++i)
            {
                pos[i] = np[i] + fix->dt[i];
                g_LastPatchedPos[i] = pos[i];
            }
            g_PatchedOnce = true;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    constexpr double         kDemoTickRecentMs     = 200.0;
    constexpr float          kGeneralFixMinMeters  = 0.002f;
    constexpr float          kRootIdMatchMeters    = 1.0f;
    constexpr unsigned       kControlRootRing      = 4;
    constexpr unsigned char  kGetControlRootPrologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57,
                                                           0x48, 0x83, 0xEC, 0x30 };

    struct DemoCameraSites
    {
        std::uintptr_t       cameraCommit;
        const unsigned char* cameraCommitPrologue;
        std::uintptr_t       getControlRoot;
        std::uintptr_t       anchorCaptureBegin;
        std::uintptr_t       anchorCaptureEnd;
    };

    DemoCameraSites GetDemoCameraSites()
    {
        using GB = AddressSetRuntime::GameBuild;
        switch (gGameBuild)
        {
        case GB::En_1_0_15_4:
        case GB::En_1_0_15_4a:
            return { 0x140437B60ull, kCameraCommitPrologue, 0x140421740ull,
                     0x1404334B0ull, 0x140433720ull };
        case GB::Jp_1_0_15_4:
        case GB::Jp_1_0_15_4a:
            return { 0x1404380E0ull, kCameraCommitPrologue, 0x140421D30ull,
                     0x140433A90ull, 0x140433D00ull };
        case GB::En_1_0_15_3:
            return { 0x1444A6AD0ull, kCameraCommitPrologue153, 0x1443C9170ull,
                     0x140433030ull, 0x1404332A0ull };
        case GB::Jp_1_0_15_3:
            return { 0x144465CB0ull, kCameraCommitPrologue153, 0x144416AE0ull,
                     0x140432BA0ull, 0x140432E10ull };
        default:
            return { 0, kCameraCommitPrologue, 0, 0, 0 };
        }
    }

    using GetControlRoot_t = int* (__fastcall*)(int* err, float* outTransform, std::uint64_t id);
    GetControlRoot_t          g_OrigGetControlRoot = nullptr;
    void*                     g_HookGetControlRoot = nullptr;
    std::atomic<std::int64_t> g_DemoTickQpc{ 0 };

    struct ControlRootSample
    {
        std::uint64_t id = 0;
        float         pos[3] = {};
        bool          ok = false;
    };

    std::mutex        g_ControlRootMutex;
    ControlRootSample g_ControlRootRing[kControlRootRing];
    unsigned          g_ControlRootRingCount = 0;
    float             g_GenPrevFresh[2][3] = {};
    bool              g_GenPrevValid[2] = { false, false };

    void ResetGeneralFixState()
    {
        g_GenPrevValid[0] = false;
        g_GenPrevValid[1] = false;
    }

    int* __fastcall hkGetCharacterControlRoot(int* err, float* outTransform, std::uint64_t id)
    {
        int* result = g_OrigGetControlRoot ? g_OrigGetControlRoot(err, outTransform, id) : err;
        const std::uintptr_t caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const std::uintptr_t base = GetExeBase();
        const DemoCameraSites sites = GetDemoCameraSites();
        if (base && sites.anchorCaptureBegin && sites.anchorCaptureEnd > sites.anchorCaptureBegin
            && caller >= base + ToRva(sites.anchorCaptureBegin)
            && caller < base + ToRva(sites.anchorCaptureEnd) && outTransform)
        {
            ControlRootSample sample;
            sample.id = id;
            sample.ok = result && *result >= 0;
            sample.pos[0] = outTransform[8];
            sample.pos[1] = outTransform[9];
            sample.pos[2] = outTransform[10];
            std::lock_guard<std::mutex> lock(g_ControlRootMutex);
            g_ControlRootRing[g_ControlRootRingCount % kControlRootRing] = sample;
            ++g_ControlRootRingCount;
        }
        return result;
    }

    bool ReadFreshControlRoot(std::uint64_t id, float* outQuat, float* outPos)
    {
        if (!g_OrigGetControlRoot)
            return false;
        alignas(16) float transform[12] = {};
        int err = -1;
        int* result = g_OrigGetControlRoot(&err, transform, id);
        if (!result || *result < 0)
            return false;
        for (int i = 0; i < 12; ++i)
        {
            if (!std::isfinite(transform[i]))
                return false;
        }
        for (int i = 0; i < 4; ++i)
            outQuat[i] = transform[4 + i];
        for (int i = 0; i < 3; ++i)
            outPos[i] = transform[8 + i];
        return true;
    }

    std::uint64_t PickAnchoredRootId(const float* anchorPos)
    {
        std::lock_guard<std::mutex> lock(g_ControlRootMutex);
        const unsigned n = g_ControlRootRingCount < kControlRootRing ? g_ControlRootRingCount
                                                                     : kControlRootRing;
        for (unsigned back = 1; back <= n; ++back)
        {
            const ControlRootSample& s =
                g_ControlRootRing[(g_ControlRootRingCount - back) % kControlRootRing];
            if (s.ok && Distance3(s.pos, anchorPos) < kRootIdMatchMeters)
                return s.id;
        }
        return 0;
    }

    void ApplyGeneralCameraFix(void* daemon, std::int64_t now)
    {
        void* playback = g_Playback.load(std::memory_order_relaxed);
        const std::int64_t tick = g_DemoTickQpc.load(std::memory_order_relaxed);
        if (!playback || !tick || QpcToMs(now - tick) > kDemoTickRecentMs)
            return;
        float aq[4], ap[3], ascale[3];
        if (!ReadPlaybackAnchor(playback, aq, ap, ascale))
            return;
        const std::uint64_t id = PickAnchoredRootId(ap);
        if (!id)
            return;
        float fq[4], fp[3];
        if (!ReadFreshControlRoot(id, fq, fp))
            return;
        const bool anchored =
            (g_GenPrevValid[0] && Distance3(ap, g_GenPrevFresh[0]) < kAnchorMatchMeters)
            || (g_GenPrevValid[1] && Distance3(ap, g_GenPrevFresh[1]) < kAnchorMatchMeters);
        for (int i = 0; i < 3; ++i)
        {
            g_GenPrevFresh[1][i] = g_GenPrevFresh[0][i];
            g_GenPrevFresh[0][i] = fp[i];
        }
        g_GenPrevValid[1] = g_GenPrevValid[0];
        g_GenPrevValid[0] = true;
        if (!anchored)
            return;
        const float len = Distance3(fp, ap);
        const float dot = QuatDotAbs(fq, aq);
        if (len < kGeneralFixMinMeters && dot > 0.99999f)
            return;
        const float aqConj[4] = { -aq[0], -aq[1], -aq[2], aq[3] };
        CamFix fix;
        QuatMul(fq, aqConj, fix.dq);
        QuatNormalize(fix.dq);
        float rotatedAnchor[3];
        QuatRotate(fix.dq, ap, rotatedAnchor);
        for (int i = 0; i < 3; ++i)
            fix.dt[i] = fp[i] - rotatedAnchor[i];
        fix.valid = true;
        fix.writeQpc = now;
        PatchActiveGkCamera(daemon, &fix);
    }

    void __fastcall hkCameraCommit(void* daemon, void* a2, void* a3, void* a4)
    {
        if (!MissionCodeGuard::ShouldBypassHooks())
        {
            const std::int64_t now = QpcNow();
            bool patched = false;
            if (g_Active.load(std::memory_order_relaxed) && g_TuneCamFix)
            {
                const std::int64_t tickQpc = g_PerFrameHookQpc.load(std::memory_order_relaxed);
                CamFix fix;
                {
                    std::lock_guard<std::mutex> lock(g_CamFixMutex);
                    fix = g_CamFix;
                    if (fix.valid && fix.writeQpc > tickQpc)
                        g_CamFix.valid = false;
                }
                if (fix.valid && fix.writeQpc > tickQpc
                    && PatchActiveGkCamera(daemon, &fix) == 1)
                    patched = true;
                g_LastCommitQpc.store(now, std::memory_order_relaxed);
            }
            if (g_TuneCamFix && !patched)
                ApplyGeneralCameraFix(daemon, now);
        }
        if (g_OrigCameraCommit)
            g_OrigCameraCommit(daemon, a2, a3, a4);
    }

    bool PrologueMatchesSeh(const void* at, const unsigned char* expect, std::size_t len)
    {
        __try
        {
            return std::memcmp(at, expect, len) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool InstallGetControlRootHook()
    {
        if (g_HookGetControlRoot)
            return true;
        const DemoCameraSites sites = GetDemoCameraSites();
        void* target = ResolveGameAddress(sites.getControlRoot);
        if (!target)
            return false;
        if (!PrologueMatchesSeh(target, kGetControlRootPrologue, sizeof(kGetControlRootPrologue)))
        {
            LogError("AttachInDemo: control-root prologue mismatch, demo camera lag fix needs an attach");
            return false;
        }
        if (!CreateAndEnableHook(target, &hkGetCharacterControlRoot,
                                 reinterpret_cast<void**>(&g_OrigGetControlRoot)))
        {
            LogError("AttachInDemo: control-root hook failed, demo camera lag fix needs an attach");
            return false;
        }
        g_HookGetControlRoot = target;
        return true;
    }

    bool InstallCameraCommitHook()
    {
        if (g_HookCameraCommit)
            return true;
        const DemoCameraSites sites = GetDemoCameraSites();
        void* target = ResolveGameAddress(sites.cameraCommit);
        if (!target)
            return false;
        if (!PrologueMatchesSeh(target, sites.cameraCommitPrologue, sizeof(kCameraCommitPrologue)))
        {
            LogError("AttachInDemo: camera commit prologue mismatch, demo camera fix off");
            return false;
        }
        if (!CreateAndEnableHook(target, &hkCameraCommit,
                                 reinterpret_cast<void**>(&g_OrigCameraCommit)))
        {
            LogError("AttachInDemo: camera commit hook failed, demo camera fix limited to stream writes");
            return false;
        }
        g_HookCameraCommit = target;
        return true;
    }

    float Distance3(const float* a, const float* b)
    {
        const float dx = a[0] - b[0];
        const float dy = a[1] - b[1];
        const float dz = a[2] - b[2];
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    std::int64_t QpcNow()
    {
        LARGE_INTEGER li;
        QueryPerformanceCounter(&li);
        return li.QuadPart;
    }

    double QpcToMs(std::int64_t ticks)
    {
        static const double perMs = []
        {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return static_cast<double>(f.QuadPart) / 1000.0;
        }();
        return static_cast<double>(ticks) / perMs;
    }

    std::atomic<void*> g_PlayerImpl{ nullptr };

    bool g_ReportedNoMatrix = false;
    bool g_ReportedNoPlacementKind = false;

    const char* KindName(OwnerKind kind)
    {
        switch (kind)
        {
            case OwnerKind::Vehicle:    return "vehicle";
            case OwnerKind::Horse:      return "horse";
            case OwnerKind::Heli:       return "heli";
            case OwnerKind::WalkerGear: return "walkergear";
            default:                    return "other";
        }
    }

    struct OwnerProbe
    {
        const char* typeName;
        OwnerKind   kind;
    };

    constexpr OwnerProbe kOwnerProbes[] =
    {
        { "TppVehicle2",    OwnerKind::Vehicle },
        { "TppHorse2",      OwnerKind::Horse },
        { "TppHeli2",       OwnerKind::Heli },
        { "TppWalkerGear2", OwnerKind::WalkerGear },
    };

    bool ResolveOwnerByName(const char* ownerName, std::uint32_t& idOut, OwnerKind& kindOut)
    {
        for (const OwnerProbe& probe : kOwnerProbes)
        {
            std::uint32_t id = kInvalidId;
            if (GetGameObjectIdByName(probe.typeName, ownerName, id) && id != kInvalidId)
            {
                idOut   = id;
                kindOut = probe.kind;
                return true;
            }
        }
        return false;
    }

    bool ResolveOwnerKindById(std::uint32_t ownerObjectId, OwnerKind& kindOut)
    {
        for (const OwnerProbe& probe : kOwnerProbes)
        {
            std::uint32_t id = kInvalidId;
            if (GetGameObjectIdWithIndex(probe.typeName, ownerObjectId & kInstanceMask, id)
                && id == ownerObjectId)
            {
                kindOut = probe.kind;
                return true;
            }
        }
        return false;
    }

    std::uint64_t HandoffStateForKind(OwnerKind kind)
    {
        switch (kind)
        {
            case OwnerKind::Horse:      return kLastStateRideHorse;
            case OwnerKind::Heli:       return kLastStateRideHeli;
            case OwnerKind::WalkerGear: return kLastStateRideWalker;
            default:                    return 0;
        }
    }

    void NormalizeAxis(float* v)
    {
        const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (len > 1e-6f)
        {
            v[0] /= len;
            v[1] /= len;
            v[2] /= len;
        }
        else
        {
            v[0] = 0.0f;
            v[1] = 0.0f;
            v[2] = 0.0f;
        }
    }

    void MatrixToQuaternion(const float* m16, float* outXYZW)
    {
        float x[3] = { m16[0], m16[1], m16[2] };
        float y[3] = { m16[4], m16[5], m16[6] };
        float z[3] = { m16[8], m16[9], m16[10] };
        NormalizeAxis(x);
        NormalizeAxis(y);
        NormalizeAxis(z);

        const float r00 = x[0], r10 = x[1], r20 = x[2];
        const float r01 = y[0], r11 = y[1], r21 = y[2];
        const float r02 = z[0], r12 = z[1], r22 = z[2];

        float qx, qy, qz, qw;
        const float trace = r00 + r11 + r22;
        if (trace > 0.0f)
        {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            qw = 0.25f * s;
            qx = (r21 - r12) / s;
            qy = (r02 - r20) / s;
            qz = (r10 - r01) / s;
        }
        else if (r00 > r11 && r00 > r22)
        {
            const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
            qw = (r21 - r12) / s;
            qx = 0.25f * s;
            qy = (r01 + r10) / s;
            qz = (r02 + r20) / s;
        }
        else if (r11 > r22)
        {
            const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
            qw = (r02 - r20) / s;
            qx = (r01 + r10) / s;
            qy = 0.25f * s;
            qz = (r12 + r21) / s;
        }
        else
        {
            const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
            qw = (r10 - r01) / s;
            qx = (r02 + r20) / s;
            qy = (r12 + r21) / s;
            qz = 0.25f * s;
        }

        const float len = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
        if (len > 1e-6f)
        {
            qx /= len;
            qy /= len;
            qz /= len;
            qw /= len;
        }
        else
        {
            qx = 0.0f;
            qy = 0.0f;
            qz = 0.0f;
            qw = 1.0f;
        }

        outXYZW[0] = qx;
        outXYZW[1] = qy;
        outXYZW[2] = qz;
        outXYZW[3] = qw;
    }

    bool PlayerIsUnconscious()
    {
        if (!gAddr.PlayerInfo_CheckStatusBit)
            return false;

        auto fn = reinterpret_cast<CheckStatusBit_t>(
            ResolveGameAddress(gAddr.PlayerInfo_CheckStatusBit));
        if (!fn)
            return false;

        __try
        {
            return fn(kStatusUnconscious);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ReleaseAttach()
    {
        g_Active.store(false, std::memory_order_relaxed);
    }

    void* GetVehicleSystem()
    {
        if (!gAddr.Fox_GetQuarkSystemTable)
            return nullptr;

        auto qst = reinterpret_cast<QuarkTable_t>(
            ResolveGameAddress(gAddr.Fox_GetQuarkSystemTable));
        if (!qst)
            return nullptr;

        __try
        {
            std::uint8_t* table = qst();
            if (!table)
                return nullptr;

            std::uint8_t* app = *reinterpret_cast<std::uint8_t**>(table + kQuarkAppSystem);
            if (!app)
                return nullptr;

            return *reinterpret_cast<void**>(app + kAppVehicleSystem);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool ReadVehicleConnectPointMatrix(std::uint32_t ownerObjectId, std::uint32_t cnpHash,
                                       float* outMatrix16)
    {
        void* sys = GetVehicleSystem();
        if (!sys)
            return false;

        auto cnpWorld = reinterpret_cast<GetVehicleCnpWorld_t>(
            ResolveGameAddress(gAddr.Vehicle_GetConnectPointWorldMatrix));
        if (!cnpWorld)
            return false;

        __try
        {
            void** sysVtbl = *reinterpret_cast<void***>(sys);
            if (!sysVtbl)
                return false;

            auto getCommunication = reinterpret_cast<GetCommunication_t>(
                sysVtbl[kGetCommunicationSlot / sizeof(void*)]);
            if (!getCommunication)
                return false;

            std::int32_t err  = -1;
            void*        info = nullptr;
            getCommunication(sys, &err, &info,
                             static_cast<std::uint16_t>(ownerObjectId & kInstanceMask));
            if (err < 0 || !info)
                return false;

            std::int32_t cnpErr = -1;
            cnpWorld(info, &cnpErr, outMatrix16, cnpHash);
            return cnpErr >= 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadFloat3Safe(const float* src, float* out)
    {
        __try
        {
            out[0] = src[0];
            out[1] = src[1];
            out[2] = src[2];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadFloat4Safe(const float* src, float* out)
    {
        __try
        {
            out[0] = src[0];
            out[1] = src[1];
            out[2] = src[2];
            out[3] = src[3];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkPlayerWarp(void* self, std::uint32_t instanceIndex, const float* pos,
                                 const float* rot, bool waitForBlock)
    {
        const bool own = g_InOwnWarp.exchange(false, std::memory_order_relaxed);
        if (!own)
        {
            float p[3] = {};
            const bool havePos = pos && ReadFloat3Safe(pos, p);
            const std::int64_t endQpc = g_DemoEndQpc.load(std::memory_order_relaxed);
            const double sinceEnd = endQpc ? QpcToMs(QpcNow() - endQpc) : -1.0;
            const bool inWindow = sinceEnd >= 0.0 && sinceEnd <= kDemoEndWatchMs;

            if (inWindow && !waitForBlock && havePos)
            {
                g_MissionWarpPos[0] = p[0];
                g_MissionWarpPos[1] = p[1];
                g_MissionWarpPos[2] = p[2];
                g_MissionWarpPos[3] = 1.0f;
                float r[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                if (rot)
                    ReadFloat4Safe(rot, r);
                g_MissionWarpRot[0] = r[0];
                g_MissionWarpRot[1] = r[1];
                g_MissionWarpRot[2] = r[2];
                g_MissionWarpRot[3] = r[3];
                g_MissionWarpPending.store(true, std::memory_order_relaxed);
            }
            else if (inWindow && waitForBlock
                     && g_MissionWarpPending.exchange(false, std::memory_order_relaxed))
            {
                if (g_OrigWarp)
                    g_OrigWarp(self, instanceIndex, g_MissionWarpPos, g_MissionWarpRot, waitForBlock);
                return;
            }
            else if (!inWindow)
            {
                g_MissionWarpPending.store(false, std::memory_order_relaxed);
            }
        }
        if (g_OrigWarp)
            g_OrigWarp(self, instanceIndex, pos, rot, waitForBlock);
    }

    void EnsurePlayerWarpHook()
    {
        if (g_WarpSlot || !g_HookTarget)
            return;
        void* impl = g_PlayerImpl.load(std::memory_order_relaxed);
        if (!impl)
            return;
        __try
        {
            auto* sub = reinterpret_cast<std::uint8_t*>(impl) + kPlayerSubObject;
            auto** vtbl = *reinterpret_cast<void***>(sub);
            if (!vtbl)
                return;
            void** slot = vtbl + kPlayerWarpSlot / sizeof(void*);
            DWORD old = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old))
                return;
            g_OrigWarp = reinterpret_cast<Warp_t>(*slot);
            *slot = reinterpret_cast<void*>(&hkPlayerWarp);
            VirtualProtect(slot, sizeof(void*), old, &old);
            g_WarpSlot = slot;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void RestorePlayerWarpHook()
    {
        if (!g_WarpSlot)
            return;
        __try
        {
            DWORD old = 0;
            if (VirtualProtect(g_WarpSlot, sizeof(void*), PAGE_READWRITE, &old))
            {
                *g_WarpSlot = reinterpret_cast<void*>(g_OrigWarp);
                VirtualProtect(g_WarpSlot, sizeof(void*), old, &old);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        g_WarpSlot = nullptr;
        g_OrigWarp = nullptr;
    }

    void NoteDemoEnd()
    {
        g_DemoEndQpc.store(QpcNow(), std::memory_order_relaxed);
        g_MissionWarpPending.store(false, std::memory_order_relaxed);
    }

    bool WarpPlayerTo(const float* matrix16)
    {
        void* impl = g_PlayerImpl.load(std::memory_order_relaxed);
        if (!impl)
            return false;

        __try
        {
            auto* sub = reinterpret_cast<std::uint8_t*>(impl) + kPlayerSubObject;
            auto** vtbl = *reinterpret_cast<void***>(sub);
            if (!vtbl)
                return false;

            auto warp = reinterpret_cast<Warp_t>(vtbl[kPlayerWarpSlot / sizeof(void*)]);
            if (!warp)
                return false;

            alignas(16) float pos[4] = { matrix16[12], matrix16[13], matrix16[14], 1.0f };
            alignas(16) float rot[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            MatrixToQuaternion(matrix16, rot);

            g_InOwnWarp.store(true, std::memory_order_relaxed);
            warp(sub, 0, pos, rot, false);
            g_InOwnWarp.store(false, std::memory_order_relaxed);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct TypeLayout
    {
        OwnerKind   kind;
        uintptr_t   typeVtable;
        std::size_t instanceCount;
        std::size_t realizedBegin;
        std::size_t realizedEnd;
        std::size_t controller;
        uintptr_t   controllerVtable;
        std::size_t connectPointSlot;
        std::size_t controllerBase;
        std::size_t controllerCount;
    };

    std::size_t BuildTypeLayouts(TypeLayout* out, std::size_t capacity)
    {
        const TypeLayout all[] =
        {
            { OwnerKind::Horse, gAddr.GameObjectType_Horse2_Vtable,
              0x40, 0x58, 0x60, 0x10, gAddr.AnimationControllerImpl_Vtable,
              kSharedConnectPointSlot, kSharedControllerBase, kSharedControllerCount },
            { OwnerKind::Heli, gAddr.GameObjectType_Heli2_Vtable,
              0x60, 0x48, 0x50, 0x08, gAddr.HeliAnimationControllerImpl_Vtable,
              kHeliConnectPointSlot, kHeliControllerBase, kHeliControllerCount },
            { OwnerKind::Heli, gAddr.GameObjectType_HeliAlt_Vtable,
              0x68, 0x80, 0x88, 0x10, gAddr.AnimationControllerImpl_Vtable,
              kSharedConnectPointSlot, kSharedControllerBase, kSharedControllerCount },
            { OwnerKind::WalkerGear, gAddr.GameObjectType_WalkerGear2_Vtable,
              0x70, 0x88, 0x90, 0x10, gAddr.AnimationControllerImpl_Vtable,
              kSharedConnectPointSlot, kSharedControllerBase, kSharedControllerCount },
        };

        std::size_t n = 0;
        for (const TypeLayout& layout : all)
        {
            if (n >= capacity)
                break;
            if (layout.typeVtable != 0 && layout.controllerVtable != 0)
                out[n++] = layout;
        }
        return n;
    }

    bool TypePlacementAvailable(OwnerKind kind)
    {
        if (!gAddr.Fox_GameObjectManager_GetSharedInstance)
            return false;

        TypeLayout layouts[4];
        const std::size_t n = BuildTypeLayouts(layouts, 4);
        for (std::size_t i = 0; i < n; ++i)
        {
            if (layouts[i].kind == kind)
                return true;
        }
        return false;
    }

    bool PlacementSupported(OwnerKind kind)
    {
        if (kind == OwnerKind::Vehicle)
            return gAddr.Vehicle_GetConnectPointWorldMatrix != 0;
        return TypePlacementAvailable(kind);
    }

    bool ReadTypeConnectPointMatrix(OwnerKind kind, std::uint32_t ownerObjectId,
                                    std::uint32_t cnpHash, float* outMatrix16,
                                    const char** reason)
    {
        auto getShared = reinterpret_cast<GetSharedInstance_t>(
            ResolveGameAddress(gAddr.Fox_GameObjectManager_GetSharedInstance));
        if (!getShared)
        {
            *reason = "the game object manager accessor is not ported for this build";
            return false;
        }

        TypeLayout layouts[4];
        const std::size_t layoutCount = BuildTypeLayouts(layouts, 4);

        __try
        {
            std::uint8_t* manager = reinterpret_cast<std::uint8_t*>(getShared());
            if (!manager)
            {
                *reason = "the game object manager is not up";
                return false;
            }

            void** managerVtbl = *reinterpret_cast<void***>(manager);
            auto getType = reinterpret_cast<GetTypeObject_t>(
                managerVtbl[kManagerGetTypeSlot / sizeof(void*)]);
            if (!getType)
            {
                *reason = "the game object manager has no type lookup";
                return false;
            }

            std::uint8_t* type = reinterpret_cast<std::uint8_t*>(
                getType(manager, static_cast<std::uint16_t>(ownerObjectId)));
            if (!type)
            {
                *reason = "no game object type is registered for the owner id";
                return false;
            }

            void* typeVtbl = *reinterpret_cast<void**>(type);
            const TypeLayout* layout = nullptr;
            for (std::size_t i = 0; i < layoutCount; ++i)
            {
                if (ResolveGameAddress(layouts[i].typeVtable) == typeVtbl)
                {
                    layout = &layouts[i];
                    break;
                }
            }
            if (!layout)
            {
                *reason = "the owner's type object has a layout this build does not know";
                return false;
            }
            if (layout->kind != kind)
            {
                *reason = "the owner's type object does not match the owner kind";
                return false;
            }

            const std::uint16_t typeId = *reinterpret_cast<std::uint16_t*>(type + kTypeIdOffset);
            if (typeId != static_cast<std::uint16_t>(ownerObjectId >> kTypeIdShift))
            {
                *reason = "the type object's id does not match the owner id";
                return false;
            }

            std::uint8_t* wrapper = *reinterpret_cast<std::uint8_t**>(type + kTypeWrapperOffset);
            if (!wrapper || *reinterpret_cast<std::uint8_t**>(wrapper + kWrapperTypeOffset) != type)
            {
                *reason = "the type object's back-link is inconsistent";
                return false;
            }

            const std::uint32_t index = ownerObjectId & kInstanceMask;
            const std::uint32_t instanceCount =
                *reinterpret_cast<std::uint32_t*>(type + layout->instanceCount);
            if (index >= instanceCount)
            {
                *reason = "the owner index is past the type's instance count";
                return false;
            }

            std::uint8_t** begin = *reinterpret_cast<std::uint8_t***>(type + layout->realizedBegin);
            std::uint8_t** end   = *reinterpret_cast<std::uint8_t***>(type + layout->realizedEnd);
            if (!begin || !end || end < begin
                || static_cast<std::size_t>(end - begin) > kMaxRealizedChunks)
            {
                *reason = "the owner's type has no realized instances";
                return false;
            }

            void* controllerVtbl = ResolveGameAddress(layout->controllerVtable);
            for (std::uint8_t** it = begin; it != end; ++it)
            {
                std::uint8_t* realized = *it;
                if (!realized)
                    continue;

                std::uint8_t* controller =
                    *reinterpret_cast<std::uint8_t**>(realized + layout->controller);
                if (!controller || *reinterpret_cast<void**>(controller) != controllerVtbl)
                    continue;

                const std::int32_t base =
                    *reinterpret_cast<std::int32_t*>(controller + layout->controllerBase);
                const std::uint32_t count =
                    *reinterpret_cast<std::uint32_t*>(controller + layout->controllerCount);
                if (base < 0 || index < static_cast<std::uint32_t>(base)
                    || index >= static_cast<std::uint32_t>(base) + count)
                    continue;

                auto query = reinterpret_cast<TypeCnpWorld_t>(
                    (*reinterpret_cast<void***>(controller))[layout->connectPointSlot / sizeof(void*)]);
                if (!query)
                {
                    *reason = "the owner's animation controller has no connect point query";
                    return false;
                }

                if (!query(controller, index, cnpHash, outMatrix16))
                {
                    *reason = "the connect point name is not on the owner's model";
                    return false;
                }

                return true;
            }

            *reason = "the owner instance is not realized right now";
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *reason = "a read faulted while walking the owner's type object";
            return false;
        }
    }

    bool ReadConnectPointMatrix(OwnerKind kind, std::uint32_t ownerObjectId, std::uint32_t cnpHash,
                                float* outMatrix16, const char** reason)
    {
        if (kind == OwnerKind::Vehicle)
        {
            if (ReadVehicleConnectPointMatrix(ownerObjectId, cnpHash, outMatrix16))
                return true;
            *reason = "the vehicle may be unrealized, or the connect-point name may not exist on "
                      "its model";
            return false;
        }
        return ReadTypeConnectPointMatrix(kind, ownerObjectId, cnpHash, outMatrix16, reason);
    }

    void __fastcall hkOnPlayingAfterUpdateStream(void* playback)
    {
        if (g_OrigOnPlayingAfterUpdateStream)
            g_OrigOnPlayingAfterUpdateStream(playback);
        if (g_Playback.exchange(playback, std::memory_order_relaxed) != playback)
            ResetGeneralFixState();
        g_DemoTickQpc.store(QpcNow(), std::memory_order_relaxed);

        if (!g_Active.load(std::memory_order_relaxed))
            return;

        if (MissionCodeGuard::ShouldBypassHooks())
            return;

        std::uint32_t ownerObjectId;
        std::uint32_t cnpHash;
        bool          unattachOnSleep;
        OwnerKind     kind;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            ownerObjectId   = g_OwnerObjectId;
            cnpHash         = g_ConnectPointId;
            unattachOnSleep = g_UnattachOnSleep;
            kind            = g_OwnerKind;
        }

        if (ownerObjectId == kInvalidId)
            return;

        if (unattachOnSleep && PlayerIsUnconscious())
        {
            g_Active.store(false, std::memory_order_relaxed);
            g_HandoffDeadlineMs.store(0, std::memory_order_relaxed);
            return;
        }

        if (!PlacementSupported(kind))
        {
            if (!g_ReportedNoPlacementKind)
            {
                g_ReportedNoPlacementKind = true;
                LogWarn("AttachInDemo '%s': no connect point path for %s owners on this build, boarding handoff only",
                        g_OwnerName.c_str(), KindName(kind));
            }
            return;
        }

        alignas(16) float matrix[16] = {};
        const char* reason = "";
        if (!ReadConnectPointMatrix(kind, ownerObjectId, cnpHash, matrix, &reason))
        {
            if (!g_ReportedNoMatrix)
            {
                g_ReportedNoMatrix = true;
                LogWarn("AttachInDemo '%s': connect point unreachable (%s), player not attached",
                        g_OwnerName.c_str(), reason);
            }
            return;
        }

        g_PerFrameHookQpc.store(QpcNow(), std::memory_order_relaxed);

        const std::uint64_t placedAt = g_StreamPlacedAtMs.load(std::memory_order_relaxed);
        const bool streamLive = placedAt && GetTickCount64() - placedAt < kStreamLiveWindowMs;
        if (streamLive)
            return;

        WarpPlayerTo(matrix);
    }

    bool PlaceDemoActor(void* self, std::uint32_t index, const float* matrix16)
    {
        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(self);

            void* owner = *reinterpret_cast<void**>(base + kExecOwner);
            void* demoAnimCtl = *reinterpret_cast<void**>(base + kExecDemoAnimController);
            if (!owner || !demoAnimCtl)
                return false;

            auto* ownerBytes = reinterpret_cast<std::uint8_t*>(owner);

            void* animHolder = *reinterpret_cast<void**>(ownerBytes + kOwnerAnimHolder);
            void* playerSub  = *reinterpret_cast<void**>(ownerBytes + kOwnerPlayerSub);
            if (!animHolder || !playerSub)
                return false;

            void* animCtl = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(animHolder) + kAnimHolderController);
            if (!animCtl)
                return false;

            const std::uint32_t localIndex = *reinterpret_cast<std::uint32_t*>(
                reinterpret_cast<std::uint8_t*>(playerSub) + kPlayerSubLocalIndex);
            if (index != localIndex)
                return false;

            auto** demoVtbl = *reinterpret_cast<void***>(demoAnimCtl);
            if (!demoVtbl)
                return false;

            auto isPlaying = reinterpret_cast<IsPlaying_t>(
                demoVtbl[kDemoAnimIsPlayingSlot / sizeof(void*)]);
            if (!isPlaying || !isPlaying(demoAnimCtl, index))
                return false;

            auto** animVtbl = *reinterpret_cast<void***>(animCtl);
            if (!animVtbl)
                return false;

            auto updateStreamMotion = reinterpret_cast<UpdateStreamMotion_t>(
                animVtbl[kUpdateStreamMotionSlot / sizeof(void*)]);
            if (!updateStreamMotion)
                return false;

            alignas(16) float transformRT[12] = {};
            transformRT[0]  = 1.0f;
            transformRT[1]  = 1.0f;
            transformRT[2]  = 1.0f;
            transformRT[3]  = 1.0f;
            MatrixToQuaternion(matrix16, transformRT + 4);
            transformRT[8]  = matrix16[12];
            transformRT[9]  = matrix16[13];
            transformRT[10] = matrix16[14];
            transformRT[11] = 1.0f;

            updateStreamMotion(animCtl, index, transformRT);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkSequentialDemoActionExecute(void* self, std::uint32_t phase,
                                                  std::uint32_t gameObjectIndex)
    {
        if (g_OrigDemoActionExecute)
            g_OrigDemoActionExecute(self, phase, gameObjectIndex);

        if (phase != kDemoPhaseAnimation || !g_Active.load(std::memory_order_relaxed))
            return;

        if (MissionCodeGuard::ShouldBypassHooks())
            return;

        std::uint32_t ownerObjectId;
        std::uint32_t cnpHash;
        OwnerKind     kind;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            ownerObjectId = g_OwnerObjectId;
            cnpHash       = g_ConnectPointId;
            kind          = g_OwnerKind;
        }

        if (ownerObjectId == kInvalidId || !PlacementSupported(kind))
            return;

        alignas(16) float matrix[16] = {};
        const char* reason = "";
        if (!ReadConnectPointMatrix(kind, ownerObjectId, cnpHash, matrix, &reason))
            return;

        if (!PlaceDemoActor(self, gameObjectIndex, matrix))
            return;

        NoteCamFixAtWrite(matrix);

        g_StreamPlacedAtMs.store(GetTickCount64(), std::memory_order_relaxed);
    }

    bool IsLocalPlayerActor(void* self, std::uint32_t gameObjectIndex)
    {
        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(self);
            auto* owner = *reinterpret_cast<std::uint8_t**>(base + kExecOwner);
            if (!owner)
                return false;

            auto* playerSub = *reinterpret_cast<std::uint8_t**>(owner + kOwnerPlayerSub);
            if (!playerSub)
                return false;

            return *reinterpret_cast<std::uint32_t*>(playerSub + kPlayerSubLocalIndex)
                   == gameObjectIndex;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint64_t ReadSignalId(std::uint64_t* signal)
    {
        __try
        {
            return signal[0] & kSignalIdMask;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool PatchFinishPayload(std::uint64_t* signal, std::uint64_t lastState,
                            std::uint32_t ownerObjectId)
    {
        __try
        {
            signal[kSignalLastStateSlot] = lastState;
            *reinterpret_cast<std::uint16_t*>(
                reinterpret_cast<std::uint8_t*>(signal) + kSignalOpponentOffset)
                = static_cast<std::uint16_t>(ownerObjectId);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void TryPatchDemoFinish(void* self, std::uint32_t gameObjectIndex, std::uint64_t* signal)
    {
        if (!signal)
            return;

        const bool armed = g_Active.load(std::memory_order_relaxed)
                        || GetTickCount64() < g_HandoffDeadlineMs.load(std::memory_order_relaxed);
        if (!armed)
            return;

        if (MissionCodeGuard::ShouldBypassHooks())
            return;

        if (ReadSignalId(signal) != kSignalDemoFinish)
            return;

        std::uint32_t ownerObjectId;
        OwnerKind     kind;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            ownerObjectId = g_OwnerObjectId;
            kind          = g_OwnerKind;
        }

        const std::uint64_t lastState = HandoffStateForKind(kind);
        if (ownerObjectId == kInvalidId || lastState == 0)
            return;

        if (!IsLocalPlayerActor(self, gameObjectIndex))
            return;

        if (!PatchFinishPayload(signal, lastState, ownerObjectId))
            return;

        g_HandoffDeadlineMs.store(0, std::memory_order_relaxed);
    }

    void __fastcall hkSequentialDemoActionOnSignal(void* self, std::uint32_t gameObjectIndex,
                                                   void* err, std::uint64_t* signal)
    {
        TryPatchDemoFinish(self, gameObjectIndex, signal);

        if (g_OrigDemoActionOnSignal)
            g_OrigDemoActionOnSignal(self, gameObjectIndex, err, signal);
    }

    void OpenHandoffWindow()
    {
        if (!g_Active.load(std::memory_order_relaxed))
            return;

        OwnerKind kind;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            kind = g_OwnerKind;
        }

        if (HandoffStateForKind(kind) != 0)
            g_HandoffDeadlineMs.store(GetTickCount64() + kHandoffWindowMs,
                                      std::memory_order_relaxed);
    }

    std::uint32_t __fastcall hkDoFinish(void* playback)
    {
        NoteDemoEnd();
        OpenHandoffWindow();
        const std::uint32_t result = g_OrigDoFinish ? g_OrigDoFinish(playback) : 0;
        ReleaseAttach();
        g_Playback.store(nullptr, std::memory_order_relaxed);
        ResetGeneralFixState();
        return result;
    }

    std::uint32_t __fastcall hkDoInterrupt(void* playback)
    {
        NoteDemoEnd();
        OpenHandoffWindow();
        const std::uint32_t result = g_OrigDoInterrupt ? g_OrigDoInterrupt(playback) : 0;
        ReleaseAttach();
        g_Playback.store(nullptr, std::memory_order_relaxed);
        ResetGeneralFixState();
        return result;
    }

    bool ArmAttach(const char* label, std::uint32_t ownerObjectId, const char* connectPointName,
                   bool unattachOnSleep, OwnerKind kind)
    {
        if (ownerObjectId == kInvalidId || !connectPointName || !*connectPointName)
            return false;

        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            g_OwnerName       = label;
            g_OwnerObjectId   = ownerObjectId;
            g_ConnectPointId  = FoxHashes::StrCode32(connectPointName);
            g_UnattachOnSleep = unattachOnSleep;
            g_OwnerKind       = kind;
        }

        g_ReportedNoMatrix        = false;
        g_ReportedNoPlacementKind = false;
        g_StreamPlacedAtMs.store(0, std::memory_order_relaxed);
        ReadAttachTuning();
        ResetCamFixState();
        g_PerFrameHookQpc.store(0, std::memory_order_relaxed);
        g_HandoffDeadlineMs.store(0, std::memory_order_relaxed);
        g_Active.store(true, std::memory_order_relaxed);
        EnsurePlayerWarpHook();
        return true;
    }
}

void Note_PlayerGameObjectImpl(void* impl)
{
    if (impl)
        g_PlayerImpl.store(impl, std::memory_order_relaxed);
}

bool RequestToAttachInDemoById(std::uint32_t ownerObjectId, const char* connectPointName,
                               bool unattachOnSleep)
{
    char label[32] = {};
    std::snprintf(label, sizeof(label), "0x%04X", ownerObjectId);

    OwnerKind kind = OwnerKind::Other;
    if (ownerObjectId == kInvalidId || !ResolveOwnerKindById(ownerObjectId, kind))
    {
        LogWarn("RequestToAttachInDemo %s: not a live vehicle, horse, heli or walkergear, not attached", label);
        return false;
    }

    return ArmAttach(label, ownerObjectId, connectPointName, unattachOnSleep, kind);
}

bool RequestToAttachInDemo(const char* ownerName, const char* connectPointName,
                           bool unattachOnSleep)
{
    if (!ownerName || !*ownerName || !connectPointName || !*connectPointName)
        return false;

    std::uint32_t ownerObjectId = kInvalidId;
    OwnerKind     kind          = OwnerKind::Other;
    if (!ResolveOwnerByName(ownerName, ownerObjectId, kind))
    {
        LogWarn("RequestToAttachInDemo '%s': no vehicle, horse, heli or walkergear by that name, not attached",
                ownerName);
        return false;
    }

    return ArmAttach(ownerName, ownerObjectId, connectPointName, unattachOnSleep, kind);
}

void ClearAttachInDemo()
{
    ReleaseAttach();
    g_HandoffDeadlineMs.store(0, std::memory_order_relaxed);
}

bool Install_PlayerAttachInDemo_Hook()
{
    if (g_HookTarget)
        return true;

    if (gGameBuild == ::AddressSetRuntime::GameBuild::En_1_0_15_3
        || gGameBuild == ::AddressSetRuntime::GameBuild::Jp_1_0_15_3)
    {
        LogError("AttachInDemo: not supported on 15.3, RequestToAttachInDemo has no effect");
        return true;
    }

    if (!gAddr.DemoPlayback_OnPlayingAfterUpdateStream
        || !gAddr.Vehicle_GetConnectPointWorldMatrix)
    {
        LogError("AttachInDemo: demo/vehicle addresses missing for this build, RequestToAttachInDemo has no effect");
        return true;
    }

    void* target = ResolveGameAddress(gAddr.DemoPlayback_OnPlayingAfterUpdateStream);
    if (!target)
        return false;

    if (!CreateAndEnableHook(target, &hkOnPlayingAfterUpdateStream,
                             reinterpret_cast<void**>(&g_OrigOnPlayingAfterUpdateStream)))
    {
        LogError("AttachInDemo: demo per-frame hook failed, RequestToAttachInDemo has no effect");
        return false;
    }

    g_HookTarget = target;

    if (gAddr.DemoPlayback_DoFinish)
    {
        void* finish = ResolveGameAddress(gAddr.DemoPlayback_DoFinish);
        if (finish && CreateAndEnableHook(finish, &hkDoFinish,
                                          reinterpret_cast<void**>(&g_OrigDoFinish)))
            g_HookFinish = finish;
    }

    if (gAddr.DemoPlayback_DoInterrupt)
    {
        void* interrupt = ResolveGameAddress(gAddr.DemoPlayback_DoInterrupt);
        if (interrupt && CreateAndEnableHook(interrupt, &hkDoInterrupt,
                                             reinterpret_cast<void**>(&g_OrigDoInterrupt)))
            g_HookInterrupt = interrupt;
    }

    if (gAddr.Player_SequentialDemoActionExecute)
    {
        void* exec = ResolveGameAddress(gAddr.Player_SequentialDemoActionExecute);
        if (exec && CreateAndEnableHook(exec, &hkSequentialDemoActionExecute,
                                        reinterpret_cast<void**>(&g_OrigDemoActionExecute)))
            g_HookDemoExecute = exec;
    }

    if (gAddr.Player_SequentialDemoActionOnSignal)
    {
        void* onSignal = ResolveGameAddress(gAddr.Player_SequentialDemoActionOnSignal);
        if (onSignal && CreateAndEnableHook(onSignal, &hkSequentialDemoActionOnSignal,
                                            reinterpret_cast<void**>(&g_OrigDemoActionOnSignal)))
            g_HookDemoOnSignal = onSignal;
    }

    InstallCameraCommitHook();
    InstallGetControlRootHook();

    if (!g_HookDemoExecute)
        LogError("AttachInDemo: demo placement hook failed, player reaches the connect point only at demo end");

    if (!g_HookDemoOnSignal)
        LogError("AttachInDemo: demo finish-signal hook failed, no boarding handoff at demo end");

    if (!g_HookFinish || !g_HookInterrupt)
        LogError("AttachInDemo: demo end hooks failed, attach stays armed; call ClearAttachInDemo at demo end");

    return true;
}

bool Uninstall_PlayerAttachInDemo_Hook()
{
    g_Active.store(false, std::memory_order_relaxed);
    g_HandoffDeadlineMs.store(0, std::memory_order_relaxed);
    RestorePlayerWarpHook();

    if (g_HookTarget)
        DisableAndRemoveHook(g_HookTarget);
    if (g_HookFinish)
        DisableAndRemoveHook(g_HookFinish);
    if (g_HookInterrupt)
        DisableAndRemoveHook(g_HookInterrupt);

    if (g_HookDemoExecute)
        DisableAndRemoveHook(g_HookDemoExecute);
    if (g_HookDemoOnSignal)
        DisableAndRemoveHook(g_HookDemoOnSignal);
    if (g_HookCameraCommit)
        DisableAndRemoveHook(g_HookCameraCommit);
    if (g_HookGetControlRoot)
        DisableAndRemoveHook(g_HookGetControlRoot);

    g_HookTarget       = nullptr;
    g_HookFinish       = nullptr;
    g_HookInterrupt    = nullptr;
    g_HookDemoExecute  = nullptr;
    g_HookDemoOnSignal = nullptr;
    g_HookCameraCommit = nullptr;
    g_OrigCameraCommit = nullptr;
    g_HookGetControlRoot = nullptr;
    g_OrigGetControlRoot = nullptr;
    g_Playback.store(nullptr, std::memory_order_relaxed);
    g_OrigDemoActionExecute  = nullptr;
    g_OrigDemoActionOnSignal = nullptr;
    g_OrigOnPlayingAfterUpdateStream = nullptr;
    g_OrigDoFinish    = nullptr;
    g_OrigDoInterrupt = nullptr;
    g_PlayerImpl.store(nullptr, std::memory_order_relaxed);
    return true;
}
