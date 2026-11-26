#include <pch.h>
#include "RideHeliActionImpl_State_IdleAction.h"

#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "CcRootAdjustAction_Exec.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"
#include "RideHeliActionImpl_State_GetInAction.h"

namespace
{
    using StateAction_t   = void(__fastcall*)(void* rideHeli, std::uint32_t realizedSlot, int proc, void* motionEvent);
    using RootAdjust_t    = void(__fastcall*)(void* rideHeli, std::uint32_t realizedSlot);
    using GetRootAdjust_t = void*(__fastcall*)(void* realized, std::uint32_t realizedSlot);
    using AttachMatrix_t  = std::uint64_t(__fastcall*)(void* passengers, float* matrix, std::uint32_t heliIndex, std::int32_t connectPoint);
    using SetAlpha_t      = void(__fastcall*)(void* realized, std::uint32_t soldierIndex, std::uint32_t realizedSlot, std::uint64_t model, std::uint64_t mesh, float alpha);
    using RequestMotion_t = void(__fastcall*)(void* motion, std::uint32_t realizedSlot, std::uint32_t layer, std::uint64_t state, float blend, std::int32_t flags);
    using RealizedHeli_t  = void*(__fastcall*)(void* helis, std::uint16_t compactIndex);
    using MotionEnded_t   = std::uint64_t(__fastcall*)(void* motion, std::uint32_t realizedSlot, std::uint32_t layer);
    using WorldMatrix_t   = void(__fastcall*)(void* passengers, float* matrix, std::uint32_t heliIndex);
    using ResetEquipMotion_t  = void(__fastcall*)(std::uint32_t realizedSlot, void* soldiers);
    using StartReloadMotion_t = void(__fastcall*)(std::uint32_t realizedSlot, std::uint32_t soldierIndex, std::uint32_t stance, void* soldiers);
    using QuarkTable_t        = std::uint8_t*(__fastcall*)();
    using EquipType_t         = std::int32_t(__fastcall*)(void* equipTypes, std::uint16_t equipId);
    using EventInfo_t         = std::int32_t(__fastcall*)(void* info);
    using LoopRunning_t       = std::uint64_t(__fastcall*)(void* motion, std::uint32_t realizedSlot, std::uint32_t layer);

    struct MotionListener
    {
        void*         callback;   // (realized slot, event info)
        void*         owner;
        std::uint64_t id;
        std::uint32_t args[2];
    };
    using Listen_t   = void(__fastcall*)(void* motion, std::uint32_t realizedSlot, std::uint64_t key, const MotionListener* listener);
    using Unlisten_t = void(__fastcall*)(void* motion, std::uint32_t realizedSlot, std::uint64_t key, const void* owner);

    constexpr int kProcEnter = 1;
    constexpr int kProcExit  = 2;

    constexpr std::size_t  kRideSoldiers   = 0x70;
    constexpr std::size_t  kRideRealized   = 0x78;
    constexpr std::size_t  kGetRootAdjust  = 0x98;   // RealizedSoldier2Impl::GetRootAdjustModule(slot)
    constexpr std::size_t  kRideWork       = 0x88;   // 0x28 per riding soldier
    constexpr std::size_t  kRideWorkBase   = 0x90;   // realized slot of entry 0
    constexpr std::size_t  kWorkSize       = 0x28;
    constexpr std::size_t  kWorkMotion     = 0x08;
    constexpr std::size_t  kWorkSoldier    = 0x10;
    constexpr std::size_t  kWorkState      = 0x24;
    constexpr std::size_t  kWorkFlags      = 0x26;
    constexpr std::uint8_t kRideGetIn      = 1;
    constexpr std::uint8_t kRideIdle       = 2;
    constexpr std::uint8_t kLeftDoorBit    = 0x40;
    constexpr std::size_t  kPartFlagInfo   = 0x110;
    constexpr std::size_t  kFlagRecordSize = 0x28;
    constexpr std::size_t  kRequestMotion  = 0x170;

    constexpr std::uint64_t kHideBit   = 1ull << 50;   // the realizer drops him and he rides on unseen
    constexpr std::uint64_t kRidingBit = 1ull;         // the equip plugin hides his gun while it is set

    constexpr std::int32_t  kLeftDoorPoint     = 0;   // CNP_ppos_a
    constexpr std::int32_t  kCentrePoint       = 1;
    constexpr std::int32_t  kRightDoorPoint    = 2;   // CNP_ppos_c
    constexpr std::uint64_t kStateSquatAimIdle   = 0x4A61F68CFB8Full;   // stateSquatAimIdle
    constexpr std::uint64_t kStateSupineDownIdle = 0x3D61D8913013ull;   // stateSupineDownIdle
    // read from the exe's soldier SID tables, the states the vanilla damage, down and wake-up actions request
    constexpr std::uint64_t kStateSquatArmHit[2] = { 0x0FB0AE6C8F89ull, 0x80AAA7E72879ull };   // 0x142AF1900/08, squat bullet hit to an arm
    constexpr std::uint64_t kStateSquatToSleep   = 0x391507122009ull;   // 0x142AF17A8, tranq: squat into the sleep fall
    constexpr std::uint64_t kStateSleepToSupine  = 0x494CFF86B938ull;   // 0x142AF17B0, then down onto his back
    constexpr std::uint64_t kStateSquatToDown    = 0x4D443C44BEF2ull;   // 0x142AF1920, squat bullet hit that drops him
    constexpr std::uint64_t kStateSleepWakeUp    = 0xB5A349AFD8B1ull;   // 0x142AE9138, gets up from sleep, on his back
    constexpr std::uint64_t kStateFaintRise      = 0x93D46C517DBAull;   // 0x142AF9E20, gets up from a faint, on his back
    constexpr std::uint64_t kStateSquatReload             = 0x0F0FBE02449Cull;   // stateSquatReload
    constexpr std::uint64_t kStateSquatShotgunReloadStart = 0x8A62A117E70Full;   // stateSquatShotgunReloadStart
    constexpr std::uint64_t kStateSquatShotgunReload      = 0x29809523EA16ull;   // stateSquatShotgunReload
    constexpr std::uint64_t kStateSquatShotgunReloadEnd   = 0x702698F398F0ull;   // stateSquatShotgunReloadEnd
    constexpr std::size_t   kMotionEnded         = 0x228;    // motion controller: (slot, layer) the clip on that layer has finished
    constexpr std::size_t   kLifePoints          = 0x1A;     // u16, only lethal rounds take it
    constexpr std::size_t   kLifeDownKind        = 0x12;     // low 2 bits: 1 sleep, 2 faint, 3 dying
    constexpr std::uint8_t  kLifeDown            = 1;
    constexpr std::uint8_t  kDownSleep           = 1;
    constexpr std::uint8_t  kDownFaint           = 2;
    constexpr std::uint8_t  kDownDying           = 3;
    constexpr std::uint64_t kFlinchCooldownMs    = 300;      // GZ 0.3 s
    constexpr std::uint64_t kClipMinMs           = 200;      // the motion graph needs a moment before "finished" means the new clip
    constexpr std::uint64_t kClipMaxMs           = 4000;     // a clip that never reports finished doesn't trap him
    constexpr std::uint64_t kReloadClipMaxMs     = 8000;
    constexpr std::uint64_t kShellLoopMaxMs      = 20000;
    constexpr std::uint64_t kFirstShellMs        = 3000;     // no shell by then, his clip sends no events and he is loaded at once
    constexpr std::size_t   kListen              = 0x248;    // motion controller: (slot, key, listener) its clips' events go to the listener
    constexpr std::size_t   kUnlisten            = 0x250;    // (slot, key, owner)
    constexpr std::size_t   kLoopRunning         = 0x2B8;    // (slot, layer) what vanilla's shotgun reload waits for before it ends the loop
    constexpr std::uint64_t kReloadEventKey      = 0xBEBB0B1F891Eull;   // the key vanilla's reload listens under (real name unknown)
    constexpr std::uint64_t kReloadListenerId    = 0x62BCC8F4FCB6ull;   // the id vanilla's reload hands with its listener (real name unknown)
    constexpr std::uint32_t kEventMagazineIn     = 0x94FD9355;   // reload clip event: the magazine is in (real name unknown)
    constexpr std::uint32_t kEventShellIn        = 0x786F2910;   // shotgun reload clip event: one shell is in (real name unknown)
    constexpr std::size_t   kEventInfoId         = 0x00;     // event info vtable: the event id
    constexpr std::size_t   kEventInfoKind       = 0x50;     // 0 for a clip event

    enum class SeatPose : std::uint8_t
    {
        Aim,
        Flinch,
        FallToSleep,   // first half of the tranq fall
        Falling,
        Down,          // lying at the door until his life timer wakes him
        GettingUp,
        Reload,
        ShotgunReloadStart,
        ShotgunReload,
        ShotgunReloadEnd,
    };

    constexpr std::size_t   kPartEquip         = 0xA0;    // EquipController: +8 eight bytes per soldier (loaded u16, magazine u16, shots u16, slot u8), +0x10 three equip ids per soldier
    constexpr std::size_t   kPartEquipMotion   = 0x98;    // +8 one 0x70 record per soldier, +0x10 count
    constexpr std::size_t   kEquipMotionSize   = 0x70;
    constexpr std::size_t   kEquipMotionStance = 0x34;    // low bits: 1 standing, 2 squatting
    constexpr std::uint32_t kStanceSquat       = 2;
    constexpr std::size_t   kQuarkEquip        = 0x98;    // QuarkSystemTable -> +0x1E8 the equip system, whose +8 knows each equip id's type
    constexpr std::size_t   kEquipSystem       = 0x1E8;
    constexpr std::int32_t  kEquipTypeShotgun  = 5;
    constexpr float         kMotionBlend         = 8.0f;

    constexpr std::size_t   kRideController   = 0x80;    // ActionController, +8 one 0xD0 task per soldier
    constexpr std::size_t   kTaskSize         = 0xD0;
    constexpr std::size_t   kTaskAimBlock     = 0x40;    // where he aims, 0x28 bytes
    constexpr std::size_t   kTaskSubAction    = 0xC7;    // 1 = the attack action aims and fires alongside the ride
    constexpr std::size_t   kTaskFlags        = 0xC2;    // u16, ProcessFireControl holds his fire while 0x800 is set
    constexpr std::uint16_t kHoldFire         = 0x800;
    constexpr std::size_t   kAimFire          = 0x1E;    // u16, 0x0103 = burst type 3 mode 1, what RouteAiImpl::SetAimingActionParam writes for a shot
    constexpr std::uint16_t kAimFireBytes     = 0x0103;
    constexpr std::uint64_t kAiFireHoldMs     = 5000;    // vanilla's combat shoot step lasts 5 s
    constexpr std::size_t   kAimBlockSize     = 0x28;
    constexpr std::size_t   kAimKind          = 0x0C;    // low 3 bits: 1 position, 2 game object, 4 the CP's target
    constexpr std::uint8_t  kAimAtGameObject  = 2;
    constexpr std::uint16_t kPlayerId         = 0;
    constexpr std::size_t   kPartLife         = 0x38;    // +8 one 0x40 entry per soldier
    constexpr std::size_t   kLifeEntrySize    = 0x40;
    constexpr std::size_t   kLifeFlags        = 0x14;
    constexpr std::size_t   kLifeStatus       = 0x3E;    // 1 unconscious, 2 dead
    constexpr std::size_t   kRecordNotice     = 0x20;    // dword
    constexpr std::uint32_t kIgnoresNotices   = 0x8000;  // the ride's Wake sets it: everything he sees or hears is thrown away
    constexpr std::int32_t  kPhaseAlert       = 3;
    constexpr std::size_t   kSeatPassengers   = 0x1A8;   // on the heli seat record at work+0
    constexpr std::size_t   kSeatHeliIndex    = 0x1B0;
    constexpr std::size_t   kHelisData        = 0x28;    // passengers+0x38 = heli system, its data: ids at +0x28, count at +0x44
    constexpr std::size_t   kRealizedHeli     = 0x88;
    constexpr std::size_t   kWorldMatrix      = 0x98;
    constexpr float         kStaleBonesM      = 1.0f;    // the heli pose lags its world matrix by more than this

    StateAction_t  g_OrigIdleAction   = nullptr;
    RootAdjust_t   g_OrigRootAdjust   = nullptr;
    AttachMatrix_t g_OrigAttachMatrix = nullptr;
    SetAlpha_t     g_OrigSetAlpha     = nullptr;
    void*          g_IdleTarget       = nullptr;
    void*          g_RootTarget       = nullptr;
    void*          g_AttachTarget     = nullptr;
    void*          g_AlphaTarget      = nullptr;
    bool           g_Installed        = false;

    QuarkTable_t        g_QuarkTable        = nullptr;
    ResetEquipMotion_t  g_ResetEquipMotion  = nullptr;
    StartReloadMotion_t g_StartReloadMotion = nullptr;
    std::uint8_t                g_ListenerOwner = 0;
    std::atomic<std::uint32_t>  g_ReloadSlotSoldier[0x200];   // realized slot -> soldier index + 1 while his reload listens
    std::atomic<std::uint8_t*>  g_ReloadSlotPart[0x200];
    std::atomic<std::uint8_t>   g_ReloadEventSeen[0x200];     // by soldier

    thread_local std::int32_t  t_DoorPoint   = -1;
    thread_local std::uint8_t* t_DoorFlags   = nullptr;
    thread_local std::uint32_t t_DoorSoldier = 0;
    std::uint8_t               g_Pose[0x200];        // SeatPose, the soldier's own job is the only writer
    std::uint8_t               g_DownKind[0x200];     // what put him down, picks the get-up clip
    std::uint16_t              g_LastLife[0x200];
    std::uint64_t              g_PoseSinceMs[0x200];
    std::uint64_t              g_NextFlinchMs[0x200];
    std::atomic<std::uint8_t>  g_Engaged[0x200];
    std::atomic<std::uint8_t>  g_HeliAway[0x200];
    std::uint64_t              g_EngagedUntil[0x200];   // the soldier's own job, one writer per soldier
    std::uint8_t               g_EngagedAim[0x200][0x28];
    constexpr std::uint64_t    kEngageHoldMs = 2000;    // a stale CP read or a late AI order doesn't lower his gun
    std::uint32_t              g_AiShotsSeen[0x200];    // the soldier's own job, one writer per soldier
    std::uint64_t              g_AiFireUntil[0x200];
    std::atomic<std::uint8_t>  g_FireWanted[0x200];    // his attack action keeps its burst loop going while set

    void ClearRecordBits(std::uint64_t* record, std::uint64_t bits)
    {
        if (*record & bits)
            _InterlockedAnd64(reinterpret_cast<volatile long long*>(record), static_cast<long long>(~bits));   // the life controller writes this word too
    }

    // row-vector rigid matrices, translation in m[12..14]
    void Multiply(const float* a, const float* b, float* out)
    {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                out[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];
    }

    void InvertRigid(const float* m, float* out)
    {
        for (int r = 0; r < 3; ++r)
        {
            for (int c = 0; c < 3; ++c)
                out[r * 4 + c] = m[c * 4 + r];
            out[r * 4 + 3] = 0.0f;
        }
        for (int c = 0; c < 3; ++c)
            out[12 + c] = -(m[12] * m[c * 4] + m[13] * m[c * 4 + 1] + m[14] * m[c * 4 + 2]);
        out[15] = 1.0f;
    }

    bool HeliRealizedSeh(const std::uint8_t* seat, bool whenUnsure)
    {
        __try
        {
            auto* passengers = *reinterpret_cast<std::uint8_t* const*>(seat + kSeatPassengers);
            const std::uint32_t heliIndex = *reinterpret_cast<const std::uint32_t*>(seat + kSeatHeliIndex);
            auto* helis = *reinterpret_cast<std::uint8_t**>(passengers + 0x38);
            auto* data = *reinterpret_cast<std::uint8_t**>(helis + kHelisData);
            const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(data + 0x44);
            const auto* ids = *reinterpret_cast<std::uint16_t**>(data + 0x28);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                if (ids[i] != heliIndex)
                    continue;
                void** vtbl = *reinterpret_cast<void***>(helis);
                return reinterpret_cast<RealizedHeli_t>(vtbl[kRealizedHeli / sizeof(void*)])(helis, static_cast<std::uint16_t>(i)) != nullptr;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return whenUnsure;
        }
    }

    // The heli's pose can lag its world matrix; rebuild the door from the pose's own root and the current world matrix.
    const float* HeliRootSeh(void* passengers, std::uint32_t heliIndex)   // bone 0 of the heli's pose, null when the heli has no pose
    {
        __try
        {
            auto* helis = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(passengers) + 0x38);
            auto* data = *reinterpret_cast<std::uint8_t**>(helis + kHelisData);
            const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(data + 0x44);
            const auto* ids = *reinterpret_cast<std::uint16_t**>(data + 0x28);
            std::uint32_t compact = count;
            for (std::uint32_t i = 0; i < count; ++i)
                if (ids[i] == heliIndex)
                    compact = i;
            if (compact == count)
                return nullptr;
            void** vtbl = *reinterpret_cast<void***>(helis);
            auto* realized = static_cast<std::uint8_t*>(reinterpret_cast<RealizedHeli_t>(vtbl[kRealizedHeli / sizeof(void*)])(helis, static_cast<std::uint16_t>(compact)));
            if (!realized)
                return nullptr;
            auto* anim = *reinterpret_cast<std::uint8_t**>(realized + 8);
            const std::int32_t first = *reinterpret_cast<std::int32_t*>(anim + 0x6C);
            auto* models = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(anim + 0x40) + static_cast<std::size_t>(compact - first) * 0x38 + 8);
            auto* model = models ? *reinterpret_cast<std::uint8_t**>(models) : nullptr;   // model type 0, the one the door points use
            if (!model || *reinterpret_cast<std::int16_t*>(model + 0xF8) <= 0)
                return nullptr;
            return *reinterpret_cast<float**>(model + 0xE0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void FollowHeliSeh(void* passengers, float* door, std::uint32_t heliIndex)
    {
        __try
        {
            const float* root = HeliRootSeh(passengers, heliIndex);   // same snapshot as the door point
            if (!root)
                return;
            alignas(16) float world[16];
            void** pvtbl = *reinterpret_cast<void***>(passengers);
            reinterpret_cast<WorldMatrix_t>(pvtbl[kWorldMatrix / sizeof(void*)])(passengers, world, heliIndex);
            const float dx = root[12] - world[12], dy = root[13] - world[13], dz = root[14] - world[14];
            const float lag = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (lag < kStaleBonesM)
                return;
            alignas(16) float invRoot[16], local[16];
            InvertRigid(root, invRoot);
            Multiply(door, invRoot, local);
            Multiply(local, world, door);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ApplySide(std::uint8_t* work, std::uint32_t index)
    {
        const HeliSeatSide side = SoldierRideHeli_Side(index);
        if (side == HeliSeatSide::Left)
            work[kWorkFlags] = static_cast<std::uint8_t>(work[kWorkFlags] | kLeftDoorBit);
        else if (side == HeliSeatSide::Right)
            work[kWorkFlags] = static_cast<std::uint8_t>(work[kWorkFlags] & ~kLeftDoorBit);
    }

    struct RiderSeat
    {
        std::uint32_t index;
        std::uint8_t  state;
        std::uint8_t* doorFlags;
        void*         rootAdjust;
        std::uint8_t* heliSeat;
    };

    std::uint8_t* WorkOf(std::uint8_t* rideHeli, std::uint32_t realizedSlot)
    {
        const std::uint32_t entry = realizedSlot - static_cast<std::uint32_t>(*reinterpret_cast<std::int32_t*>(rideHeli + kRideWorkBase));
        return *reinterpret_cast<std::uint8_t**>(rideHeli + kRideWork) + static_cast<std::size_t>(entry) * kWorkSize;
    }

    std::uint64_t* FlagRecordOf(std::uint8_t* rideHeli, std::uint32_t index)
    {
        auto* flagInfo = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(rideHeli + kRideSoldiers) + kPartFlagInfo);
        if (index >= *reinterpret_cast<std::uint32_t*>(flagInfo + 0x10))
            return nullptr;
        return reinterpret_cast<std::uint64_t*>(*reinterpret_cast<std::uint8_t**>(flagInfo + 8) + static_cast<std::size_t>(index) * kFlagRecordSize);
    }

    std::uint8_t* LifeEntryOf(std::uint8_t* rideHeli, std::uint32_t index)
    {
        auto* life = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(rideHeli + kRideSoldiers) + kPartLife);
        return *reinterpret_cast<std::uint8_t**>(life + 8) + static_cast<std::size_t>(index) * kLifeEntrySize;
    }

    void RequestPose(std::uint8_t* work, std::uint32_t realizedSlot, std::uint64_t state)
    {
        void* motion = *reinterpret_cast<void**>(work + kWorkMotion);
        void** vtbl = *reinterpret_cast<void***>(motion);
        reinterpret_cast<RequestMotion_t>(vtbl[kRequestMotion / sizeof(void*)])(motion, realizedSlot, 0, state, kMotionBlend, 0);
    }

    void LetHimNotice(std::uint64_t* record)
    {
        auto* notice = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(record) + kRecordNotice);
        if (*notice & kIgnoresNotices)
            _InterlockedAnd(reinterpret_cast<volatile long*>(notice), static_cast<long>(~kIgnoresNotices));
    }

    void SeatAtDoorSeh(std::uint8_t* rideHeli, std::uint32_t realizedSlot)
    {
        __try
        {
            auto* work = WorkOf(rideHeli, realizedSlot);
            const std::uint32_t index = *reinterpret_cast<std::uint32_t*>(work + kWorkSoldier);
            if (!SoldierRideHeli_IsDoorRider(index))
                return;
            std::uint64_t* record = FlagRecordOf(rideHeli, index);
            if (!record)
                return;
            ClearRecordBits(record, kHideBit | kRidingBit);
            LetHimNotice(record);
            ApplySide(work, index);   // unrealizing wipes the door bit and coming back never sets it
            std::uint8_t* life = LifeEntryOf(rideHeli, index);
            SoldierRideHeli_KeepGun(index, life + kLifeFlags);
            const bool down = life[kLifeStatus] != 0;
            g_Pose[index]        = static_cast<std::uint8_t>(down ? SeatPose::Down : SeatPose::Aim);   // coming back realized: no fall replay
            g_DownKind[index]    = life[kLifeDownKind] & 3;
            g_LastLife[index]    = *reinterpret_cast<std::uint16_t*>(life + kLifePoints);
            g_AiShotsSeen[index] = SoldierRideHeli_AiShots(index);   // shots his AI asked for on the way in are gone
            g_AiFireUntil[index] = 0;
            g_PoseSinceMs[index] = GetTickCount64();
            RequestPose(work, realizedSlot, down ? kStateSupineDownIdle : kStateSquatAimIdle);
            SoldierRideHeli_NoteStep(index, 2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool MotionEnded(std::uint8_t* work, std::uint32_t realizedSlot)
    {
        void* motion = *reinterpret_cast<void**>(work + kWorkMotion);
        void** vtbl = *reinterpret_cast<void***>(motion);
        return static_cast<std::uint8_t>(reinterpret_cast<MotionEnded_t>(vtbl[kMotionEnded / sizeof(void*)])(motion, realizedSlot, 0)) != 0;
    }

    void SetPose(std::uint32_t index, std::uint8_t* work, std::uint32_t realizedSlot, SeatPose pose, std::uint64_t state, std::uint64_t now)
    {
        RequestPose(work, realizedSlot, state);
        g_Pose[index]        = static_cast<std::uint8_t>(pose);
        g_PoseSinceMs[index] = now;
    }

    bool IsReloadPose(SeatPose pose)
    {
        return pose == SeatPose::Reload || pose == SeatPose::ShotgunReloadStart || pose == SeatPose::ShotgunReload || pose == SeatPose::ShotgunReloadEnd;
    }

    struct Magazine
    {
        std::uint16_t* rounds  = nullptr;   // loaded, magazine size, shots fired
        std::uint16_t  equipId = 0;
    };

    bool MagazineOf(std::uint8_t* rideHeli, std::uint32_t index, Magazine& magazine)
    {
        auto* equip = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(rideHeli + kRideSoldiers) + kPartEquip);
        auto* slot = *reinterpret_cast<std::uint8_t**>(equip + 8) + static_cast<std::size_t>(index) * 8;
        magazine.rounds  = reinterpret_cast<std::uint16_t*>(slot);
        magazine.equipId = *reinterpret_cast<std::uint16_t*>(*reinterpret_cast<std::uint8_t**>(equip + 0x10) + (static_cast<std::size_t>(index) * 3 + slot[6]) * 2);
        return magazine.rounds[1] != 0;
    }

    bool NeedsReload(const Magazine& magazine)   // vanilla's combat AI: under a quarter of the magazine left
    {
        return static_cast<float>(magazine.rounds[0]) < static_cast<float>(magazine.rounds[1]) * 0.25f;
    }

    bool LoadsShellByShell(std::uint16_t equipId)
    {
        auto* equipSystem = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(g_QuarkTable() + kQuarkEquip) + kEquipSystem);
        void* types = *reinterpret_cast<void**>(equipSystem + 8);
        const std::int32_t type = reinterpret_cast<EquipType_t>((*reinterpret_cast<void***>(types))[0])(types, equipId);
        return type == kEquipTypeShotgun && equipId - 0x18u >= 2 && equipId - 0x40Cu >= 2 && equipId - 0x41Cu >= 2;   // these shotguns reload like a rifle
    }

    void SquatEquipMotion(std::uint8_t* rideHeli, std::uint32_t index)
    {
        auto* table = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(rideHeli + kRideSoldiers) + kPartEquipMotion);
        auto* record = *reinterpret_cast<std::uint8_t**>(table + 8);
        if (index < *reinterpret_cast<std::uint32_t*>(table + 0x10))
            record += static_cast<std::size_t>(index) * kEquipMotionSize;
        auto* stance = reinterpret_cast<std::uint32_t*>(record + kEquipMotionStance);
        *stance = (*stance & 0xFFFFFFF2u) | kStanceSquat;
    }

    void __fastcall OnReloadEvent(std::uint32_t realizedSlot, void* info)
    {
        __try
        {
            if (realizedSlot >= 0x200)
                return;
            const std::uint32_t owner = g_ReloadSlotSoldier[realizedSlot].load(std::memory_order_acquire);
            std::uint8_t* part = g_ReloadSlotPart[realizedSlot].load(std::memory_order_acquire);
            if (owner == 0 || !part)
                return;
            void** vtbl = *reinterpret_cast<void***>(info);
            if (reinterpret_cast<EventInfo_t>(vtbl[kEventInfoKind / sizeof(void*)])(info) != 0)
                return;
            const auto event = static_cast<std::uint32_t>(reinterpret_cast<EventInfo_t>(vtbl[kEventInfoId / sizeof(void*)])(info));
            const std::uint32_t index = owner - 1;
            auto* equip = *reinterpret_cast<std::uint8_t**>(part + kPartEquip);
            auto* rounds = reinterpret_cast<std::uint16_t*>(*reinterpret_cast<std::uint8_t**>(equip + 8) + static_cast<std::size_t>(index) * 8);
            if (event == kEventMagazineIn)
            {
                rounds[0] = rounds[1];
                rounds[2] = 0;
            }
            else if (event == kEventShellIn)
            {
                if (rounds[0] < rounds[1])
                    ++rounds[0];
            }
            else
                return;
            g_ReloadEventSeen[index].store(1, std::memory_order_release);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ListenForReload(std::uint8_t* rideHeli, std::uint8_t* work, std::uint32_t realizedSlot, std::uint32_t index)
    {
        if (realizedSlot >= 0x200)
            return;
        g_ReloadEventSeen[index].store(0, std::memory_order_relaxed);
        g_ReloadSlotPart[realizedSlot].store(*reinterpret_cast<std::uint8_t**>(rideHeli + kRideSoldiers), std::memory_order_relaxed);
        g_ReloadSlotSoldier[realizedSlot].store(index + 1, std::memory_order_release);
        const MotionListener listener{ reinterpret_cast<void*>(&OnReloadEvent), &g_ListenerOwner, kReloadListenerId, { 1, 1 } };
        void* motion = *reinterpret_cast<void**>(work + kWorkMotion);
        reinterpret_cast<Listen_t>((*reinterpret_cast<void***>(motion))[kListen / sizeof(void*)])(motion, realizedSlot, kReloadEventKey, &listener);
    }

    bool LoopRunning(std::uint8_t* work, std::uint32_t realizedSlot)
    {
        void* motion = *reinterpret_cast<void**>(work + kWorkMotion);
        return static_cast<std::uint8_t>(reinterpret_cast<LoopRunning_t>((*reinterpret_cast<void***>(motion))[kLoopRunning / sizeof(void*)])(motion, realizedSlot, 0)) != 0;
    }

    bool StartReload(std::uint8_t* rideHeli, std::uint8_t* work, std::uint32_t realizedSlot, std::uint32_t index, std::uint64_t now)
    {
        Magazine magazine;
        if (!g_StartReloadMotion || !MagazineOf(rideHeli, index, magazine) || !NeedsReload(magazine))
            return false;
        const bool shells = LoadsShellByShell(magazine.equipId);
        SquatEquipMotion(rideHeli, index);
        if (shells)
            SetPose(index, work, realizedSlot, SeatPose::ShotgunReloadStart, kStateSquatShotgunReloadStart, now);
        else
            SetPose(index, work, realizedSlot, SeatPose::Reload, kStateSquatReload, now);
        g_StartReloadMotion(realizedSlot, index, kStanceSquat, *reinterpret_cast<void**>(rideHeli + kRideSoldiers));
        ListenForReload(rideHeli, work, realizedSlot, index);
        return true;
    }

    void EndReload(std::uint8_t* rideHeli, std::uint8_t* work, std::uint32_t realizedSlot)
    {
        if (g_ResetEquipMotion)
            g_ResetEquipMotion(realizedSlot, *reinterpret_cast<void**>(rideHeli + kRideSoldiers));
        void* motion = *reinterpret_cast<void**>(work + kWorkMotion);
        reinterpret_cast<Unlisten_t>((*reinterpret_cast<void***>(motion))[kUnlisten / sizeof(void*)])(motion, realizedSlot, kReloadEventKey, &g_ListenerOwner);
        if (realizedSlot < 0x200)
            g_ReloadSlotSoldier[realizedSlot].store(0, std::memory_order_release);
    }

    void FillMagazine(const Magazine& magazine)
    {
        magazine.rounds[0] = magazine.rounds[1];
        magazine.rounds[2] = 0;
    }

    void CancelReloadSeh(std::uint8_t* rideHeli, std::uint32_t realizedSlot, std::uint32_t index)
    {
        __try
        {
            if (index >= 0x200 || !IsReloadPose(static_cast<SeatPose>(g_Pose[index])))
                return;
            EndReload(rideHeli, WorkOf(rideHeli, realizedSlot), realizedSlot);
            g_Pose[index] = static_cast<std::uint8_t>(SeatPose::Aim);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    // Every frame he sits, as GZ's rider: a lethal hit -> a seated flinch, non-lethal rounds -> nothing; asleep or dead -> falls and lies at the door,
    // the life timers wake a sleeper and he gets back into his crouch; fainted or dying -> he lets go and drops out of the door; his heli destroyed ->
    // limp at the door. He keeps noticing things and his gun. True while he is up and may aim.
    bool TendRiderSeh(std::uint8_t* rideHeli, std::uint32_t realizedSlot, std::uint32_t index)
    {
        __try
        {
            auto* work = WorkOf(rideHeli, realizedSlot);
            if (std::uint64_t* record = FlagRecordOf(rideHeli, index))
            {
                ClearRecordBits(record, kHideBit | kRidingBit);
                LetHimNotice(record);
            }
            std::uint8_t* life = LifeEntryOf(rideHeli, index);
            SoldierRideHeli_KeepGun(index, life + kLifeFlags);
            const std::uint8_t status = life[kLifeStatus];
            const std::uint8_t kind = life[kLifeDownKind] & 3;
            const std::uint16_t points = *reinterpret_cast<std::uint16_t*>(life + kLifePoints);
            const bool hurt = points < g_LastLife[index];
            g_LastLife[index] = points;
            const bool heliDead = SoldierRideHeli_HeliDead(index);
            if (status == kLifeDown && (kind == kDownFaint || kind == kDownDying))
            {
                if (IsReloadPose(static_cast<SeatPose>(g_Pose[index])))
                {
                    EndReload(rideHeli, work, realizedSlot);
                    g_Pose[index] = static_cast<std::uint8_t>(SeatPose::Aim);
                }
                SoldierRideHeli_RequestFall(index, kind == kDownFaint ? "fainted on his seat" : "is dying on his seat");
                return false;
            }
            const std::uint64_t now = GetTickCount64();
            const auto pose = static_cast<SeatPose>(g_Pose[index]);
            const bool reloading = IsReloadPose(pose);
            const std::uint64_t inPose = now - g_PoseSinceMs[index];
            const bool clipDone = inPose >= (reloading ? kReloadClipMaxMs : kClipMaxMs) || (inPose >= kClipMinMs && MotionEnded(work, realizedSlot));
            const bool upright = pose == SeatPose::Aim || pose == SeatPose::Flinch || reloading;
            if (status != 0 && !upright && kind != 0)
                g_DownKind[index] = kind;   // a second dart while he lies there changes how he gets up
            if (upright)
            {
                if (status != 0 || heliDead)
                {
                    if (reloading)
                        EndReload(rideHeli, work, realizedSlot);
                    g_DownKind[index] = kind;
                    if (status == kLifeDown && kind == kDownSleep)
                        SetPose(index, work, realizedSlot, SeatPose::FallToSleep, kStateSquatToSleep, now);
                    else
                        SetPose(index, work, realizedSlot, SeatPose::Falling, kStateSquatToDown, now);
                    return false;
                }
                if (hurt && !SoldierRideHeli_FlinchDisabled(index) && now >= g_NextFlinchMs[index])
                {
                    if (reloading)
                        EndReload(rideHeli, work, realizedSlot);   // a hit cuts the reload short
                    g_NextFlinchMs[index] = now + kFlinchCooldownMs;
                    SetPose(index, work, realizedSlot, SeatPose::Flinch, kStateSquatArmHit[now & 1], now);   // GZ picks the arm from the hit side, else at random
                    return false;
                }
            }
            Magazine magazine;
            switch (pose)
            {
            case SeatPose::Aim:
                return !StartReload(rideHeli, work, realizedSlot, index, now);
            case SeatPose::Flinch:
                if (!clipDone)
                    return false;
                SetPose(index, work, realizedSlot, SeatPose::Aim, kStateSquatAimIdle, now);
                return true;
            case SeatPose::Reload:
                if (!clipDone)
                    return false;
                if (MagazineOf(rideHeli, index, magazine) && !g_ReloadEventSeen[index].load(std::memory_order_acquire))
                    FillMagazine(magazine);   // his clip sent no magazine event
                EndReload(rideHeli, work, realizedSlot);
                SetPose(index, work, realizedSlot, SeatPose::Aim, kStateSquatAimIdle, now);
                return true;
            case SeatPose::ShotgunReloadStart:
                if (!clipDone)
                    return false;
                if (MagazineOf(rideHeli, index, magazine) && magazine.rounds[0] < magazine.rounds[1])
                    SetPose(index, work, realizedSlot, SeatPose::ShotgunReload, kStateSquatShotgunReload, now);
                else
                    SetPose(index, work, realizedSlot, SeatPose::ShotgunReloadEnd, kStateSquatShotgunReloadEnd, now);
                return false;
            case SeatPose::ShotgunReload:
            {
                const bool read = MagazineOf(rideHeli, index, magazine);
                const bool full = !read || magazine.rounds[0] >= magazine.rounds[1];
                const bool noEvents = !g_ReloadEventSeen[index].load(std::memory_order_acquire) && inPose >= kFirstShellMs;
                if (read && noEvents)
                    FillMagazine(magazine);
                if ((full && LoopRunning(work, realizedSlot)) || noEvents || inPose >= kShellLoopMaxMs)
                    SetPose(index, work, realizedSlot, SeatPose::ShotgunReloadEnd, kStateSquatShotgunReloadEnd, now);
                return false;
            }
            case SeatPose::ShotgunReloadEnd:
                if (!clipDone)
                    return false;
                EndReload(rideHeli, work, realizedSlot);
                SetPose(index, work, realizedSlot, SeatPose::Aim, kStateSquatAimIdle, now);
                return true;
            case SeatPose::FallToSleep:
                if (clipDone)
                    SetPose(index, work, realizedSlot, SeatPose::Falling, kStateSleepToSupine, now);
                return false;
            case SeatPose::Falling:
                if (clipDone)
                    SetPose(index, work, realizedSlot, SeatPose::Down, kStateSupineDownIdle, now);
                return false;
            case SeatPose::Down:
                if (status == 0 && !heliDead)
                {
                    SetPose(index, work, realizedSlot, SeatPose::GettingUp, g_DownKind[index] == kDownSleep ? kStateSleepWakeUp : kStateFaintRise, now);
                }
                return false;
            case SeatPose::GettingUp:
                if (status != 0 || heliDead)
                    SetPose(index, work, realizedSlot, SeatPose::Down, kStateSupineDownIdle, now);
                else if (clipDone)
                    SetPose(index, work, realizedSlot, SeatPose::Aim, kStateSquatAimIdle, now);
                return false;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void GoHiddenSeh(std::uint8_t* rideHeli, std::uint32_t index)
    {
        __try
        {
            if (std::uint64_t* record = FlagRecordOf(rideHeli, index))
                _InterlockedOr64(reinterpret_cast<volatile long long*>(record), static_cast<long long>(kHideBit));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void SetAimSeh(std::uint8_t* rideHeli, std::uint32_t index, const std::uint8_t* aim, bool fire)
    {
        __try
        {
            auto* controller = *reinterpret_cast<std::uint8_t**>(rideHeli + kRideController);
            auto* task = *reinterpret_cast<std::uint8_t**>(controller + 8) + static_cast<std::size_t>(index) * kTaskSize;
            auto* flags = reinterpret_cast<std::uint16_t*>(task + kTaskFlags);
            const std::uint16_t held = fire ? static_cast<std::uint16_t>(*flags & ~kHoldFire) : static_cast<std::uint16_t>(*flags | kHoldFire);
            if (held != *flags)
                *flags = held;   // held, the fire control skips him: dropped shot bytes can't leave it firing every frame
            if (aim)
            {
                std::memcpy(task + kTaskAimBlock, aim, kAimBlockSize);
                task[kTaskSubAction] = 1;
            }
            else
                task[kTaskSubAction] = 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void AimAt(std::uint8_t* aim, std::uint16_t gameObjectId)
    {
        std::memset(aim, 0, kAimBlockSize);
        std::memcpy(aim, &gameObjectId, sizeof(gameObjectId));
        aim[kAimKind] = kAimAtGameObject;
        const std::uint32_t timing = 0x013A001E;   // what the route AI puts in every aim order
        const std::uint16_t timing2 = 0x013A;
        std::memcpy(aim + 0x14, &timing, sizeof(timing));
        std::memcpy(aim + 0x18, &timing2, sizeof(timing2));
    }

    // GZ: he keeps his gun on the target he boarded with, else on his AI's aim or, on alert, the player. He shoots while his AI keeps
    // ordering shots or his CP is on alert; his attack action then runs vanilla's bursts and pauses (RideHeliDoorSeat_FireWanted).
    void AimIfEngaged(std::uint8_t* rideHeli, std::uint32_t index, bool awake)
    {
        alignas(16) std::uint8_t aim[kAimBlockSize];
        bool engage = false;
        bool alert = false;
        const std::uint64_t now = GetTickCount64();
        const bool reloading = IsReloadPose(static_cast<SeatPose>(g_Pose[index]));
        awake = awake || reloading;
        if (awake)
        {
            std::uint32_t cp = 0;
            std::int32_t phase = 0;
            std::uint16_t target = 0;
            alert = CpRadioResponse::CpPhaseOf(static_cast<std::uint16_t>(index), cp, phase) && phase == kPhaseAlert;
            if (SoldierRideHeli_AimTarget(index, target))
            {
                AimAt(aim, target);
                engage = true;
            }
            else if (SoldierRideHeli_AiAim(index, aim))
            {
                engage = true;
            }
            else if (alert)
            {
                AimAt(aim, kPlayerId);
                engage = true;
            }
        }
        if (engage)
        {
            std::memcpy(g_EngagedAim[index], aim, kAimBlockSize);
            g_EngagedUntil[index] = now + kEngageHoldMs;
        }
        else if (awake && now < g_EngagedUntil[index])
        {
            std::memcpy(aim, g_EngagedAim[index], kAimBlockSize);
            engage = true;
        }
        else
            g_EngagedUntil[index] = 0;

        const std::uint32_t aiShots = SoldierRideHeli_AiShots(index);
        if (aiShots != g_AiShotsSeen[index])
            g_AiFireUntil[index] = now + kAiFireHoldMs;
        g_AiShotsSeen[index] = aiShots;
        const bool aiFire = now < g_AiFireUntil[index];
        const bool fire = engage && !reloading && (aiFire || alert);
        const std::uint16_t fireBytes = fire ? kAimFireBytes : 0;
        std::memcpy(aim + kAimFire, &fireBytes, sizeof(fireBytes));

        if (engage && !reloading)
        {
            SetAimSeh(rideHeli, index, aim, fire);
            g_Engaged[index].store(1, std::memory_order_relaxed);
        }
        else if (engage)
            SetAimSeh(rideHeli, index, nullptr, false);   // gun down for the reload, still engaged
        else if (g_Engaged[index].exchange(0, std::memory_order_relaxed))
        {
            SetAimSeh(rideHeli, index, nullptr, false);
        }
        g_FireWanted[index].store(fire ? 1 : 0, std::memory_order_release);   // after the shot bytes are in his task
    }

    void LeaveDoorSeh(std::uint8_t* rideHeli, std::uint32_t realizedSlot)
    {
        __try
        {
            auto* work = WorkOf(rideHeli, realizedSlot);
            const std::uint32_t index = *reinterpret_cast<std::uint32_t*>(work + kWorkSoldier);
            RootAdjustPin_Release(index);
            if (index >= 0x200)
                return;
            g_FireWanted[index].store(0, std::memory_order_release);
            if (IsReloadPose(static_cast<SeatPose>(g_Pose[index])))
            {
                EndReload(rideHeli, work, realizedSlot);
                g_Pose[index] = static_cast<std::uint8_t>(SeatPose::Aim);
            }
            if (SoldierRideHeli_IsDoorRider(index))
                SoldierRideHeli_NoteStep(index, 3);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool RiderSeatSeh(std::uint8_t* rideHeli, std::uint32_t realizedSlot, RiderSeat& seat)
    {
        __try
        {
            auto* work = WorkOf(rideHeli, realizedSlot);
            seat.index = *reinterpret_cast<std::uint32_t*>(work + kWorkSoldier);
            if (!SoldierRideHeli_IsDoorRider(seat.index))
                return false;
            seat.state     = work[kWorkState];
            seat.doorFlags = work + kWorkFlags;
            seat.heliSeat  = *reinterpret_cast<std::uint8_t**>(work);
            void* realized = *reinterpret_cast<void**>(rideHeli + kRideRealized);
            void** vtbl = *reinterpret_cast<void***>(realized);
            seat.rootAdjust = reinterpret_cast<GetRootAdjust_t>(vtbl[kGetRootAdjust / sizeof(void*)])(realized, realizedSlot);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkIdleAction(void* rideHeli, std::uint32_t realizedSlot, int proc, void* motionEvent)
    {
        g_OrigIdleAction(rideHeli, realizedSlot, proc, motionEvent);
        if ((proc != kProcEnter && proc != kProcExit) || MissionCodeGuard::ShouldBypassHooks())
            return;
        if (proc == kProcEnter)
            SeatAtDoorSeh(static_cast<std::uint8_t*>(rideHeli), realizedSlot);
        else
            LeaveDoorSeh(static_cast<std::uint8_t*>(rideHeli), realizedSlot);
    }

    void __fastcall hkSetRootAdjustMatrix(void* rideHeli, std::uint32_t realizedSlot)
    {
        RiderSeat seat{};
        if (MissionCodeGuard::ShouldBypassHooks() || !RiderSeatSeh(static_cast<std::uint8_t*>(rideHeli), realizedSlot, seat))
        {
            g_OrigRootAdjust(rideHeli, realizedSlot);
            return;
        }
        if (seat.state == kRideGetIn)
            RootAdjustPin_Update(seat.index, seat.rootAdjust, RootAdjustPinPhase::Learn);
        else if (seat.state == kRideIdle)
            RootAdjustPin_Update(seat.index, seat.rootAdjust, RootAdjustPinPhase::Hold);
        else
            RootAdjustPin_Release(seat.index);
        if (seat.state != kRideIdle)
        {
            g_OrigRootAdjust(rideHeli, realizedSlot);
            return;
        }
        auto* self = static_cast<std::uint8_t*>(rideHeli);
        if (!HeliRealizedSeh(seat.heliSeat, true))
        {
            CancelReloadSeh(self, realizedSlot, seat.index);
            const bool heliDead = SoldierRideHeli_HeliDead(seat.index);
            if (heliDead)
                SoldierRideHeli_RequestFall(seat.index, "lost his seat, the wreck of his heli is gone");   // GZ: a heli that is gone takes nobody along
            else
                GoHiddenSeh(self, seat.index);   // an unrealized heli only knows its placement, he would hang there in the air
            RootAdjustPin_Release(seat.index);
            AimIfEngaged(self, seat.index, false);
            if (!heliDead)
                g_HeliAway[seat.index].store(1, std::memory_order_relaxed);
            g_OrigRootAdjust(rideHeli, realizedSlot);
            return;
        }
        g_HeliAway[seat.index].store(0, std::memory_order_relaxed);
        AimIfEngaged(self, seat.index, TendRiderSeh(self, realizedSlot, seat.index));
        std::uint8_t* doorFlags = seat.doorFlags;
        const HeliSeatSide side = SoldierRideHeli_Side(seat.index);
        const bool left = side == HeliSeatSide::Left || (side == HeliSeatSide::Nearest && (*doorFlags & kLeftDoorBit) != 0);
        t_DoorPoint   = left ? kLeftDoorPoint : kRightDoorPoint;
        t_DoorFlags   = doorFlags;
        t_DoorSoldier = seat.index;
        *doorFlags = static_cast<std::uint8_t>(*doorFlags | kLeftDoorBit);   // the door points already face out, cleared it turns him round for the centre seat
        g_OrigRootAdjust(rideHeli, realizedSlot);
        if (!left)
            *doorFlags = static_cast<std::uint8_t>(*doorFlags & ~kLeftDoorBit);
        t_DoorPoint = -1;
        t_DoorFlags = nullptr;
    }

    std::uint64_t __fastcall hkGetAttachMatrix(void* passengers, float* matrix, std::uint32_t heliIndex, std::int32_t connectPoint)
    {
        if (t_DoorPoint < 0 || connectPoint != kCentrePoint)
            return g_OrigAttachMatrix(passengers, matrix, heliIndex, connectPoint);
        const std::uint64_t found = g_OrigAttachMatrix(passengers, matrix, heliIndex, t_DoorPoint);
        if (static_cast<std::uint8_t>(found) != 0)
        {
            FollowHeliSeh(passengers, matrix, heliIndex);
            const float x[3] = { matrix[0], matrix[1], matrix[2] };
            const float z[3] = { matrix[8], matrix[9], matrix[10] };
            const float turn = t_DoorPoint == kLeftDoorPoint ? 1.0f : -1.0f;   // the door points face along the heli, turn him 90 deg out of his door
            for (int i = 0; i < 3; ++i)
            {
                matrix[i]     = -turn * z[i];
                matrix[8 + i] =  turn * x[i];
            }
            SoldierRideHeli_SetDoorOut(t_DoorSoldier, matrix[8], matrix[10]);   // he faces out of his door
            return found;
        }
        if (t_DoorPoint == kRightDoorPoint)
            *t_DoorFlags = static_cast<std::uint8_t>(*t_DoorFlags & ~kLeftDoorBit);   // this heli has no door point, sit him at the centre facing right as vanilla does
        return g_OrigAttachMatrix(passengers, matrix, heliIndex, connectPoint);
    }

    void __fastcall hkSetModelAlpha(void* realized, std::uint32_t soldierIndex, std::uint32_t realizedSlot, std::uint64_t model, std::uint64_t mesh, float alpha)
    {
        if (SoldierRideHeli_IsDoorRider(soldierIndex) && !MissionCodeGuard::ShouldBypassHooks())
            alpha = 1.0f;
        g_OrigSetAlpha(realized, soldierIndex, realizedSlot, model, mesh, alpha);
    }

    bool HookOne(std::uintptr_t address, const char* name, void* detour, void** original, void*& target)
    {
        target = address ? ResolveGameAddress(address) : nullptr;
        if (!target)
        {
            LogError("RideHeliDoorSeat: %s address missing for this build, soldiers boarded by script ride hidden", name);
            return false;
        }
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("RideHeliDoorSeat: %s hook failed, soldiers boarded by script ride hidden", name);
            target = nullptr;
            return false;
        }
        return true;
    }

    void UnhookAll()
    {
        void** targets[] = { &g_IdleTarget, &g_RootTarget, &g_AttachTarget, &g_AlphaTarget };
        for (void** target : targets)
        {
            if (*target)
                DisableAndRemoveHook(*target);
            *target = nullptr;
        }
        g_OrigIdleAction   = nullptr;
        g_OrigRootAdjust   = nullptr;
        g_OrigAttachMatrix = nullptr;
        g_OrigSetAlpha     = nullptr;
    }
}

bool RideHeliDoorSeat_HeliRealized(const std::uint8_t* heliSeat)
{
    return HeliRealizedSeh(heliSeat, false);
}

bool RideHeliDoorSeat_Up(std::uint32_t soldierIndex)
{
    if (soldierIndex >= 0x200)
        return false;
    const auto pose = static_cast<SeatPose>(g_Pose[soldierIndex]);
    return (pose == SeatPose::Aim || pose == SeatPose::Flinch)
        && g_HeliAway[soldierIndex].load(std::memory_order_relaxed) == 0
        && !SoldierRideHeli_HeliDead(soldierIndex);
}

bool RideHeliDoorSeat_FireWanted(std::uint32_t soldierIndex)
{
    return soldierIndex < 0x200 && g_FireWanted[soldierIndex].load(std::memory_order_acquire) != 0;
}

void RideHeliDoorSeat_Clear()
{
    for (std::uint32_t index = 0; index < 0x200; ++index)
    {
        g_FireWanted[index].store(0, std::memory_order_relaxed);
        g_ReloadSlotSoldier[index].store(0, std::memory_order_relaxed);
        g_ReloadSlotPart[index].store(nullptr, std::memory_order_relaxed);
        g_ReloadEventSeen[index].store(0, std::memory_order_relaxed);
    }
}

bool Install_RideHeliActionImpl_State_IdleAction_Hook()
{
    if (g_Installed)
        return true;

    const bool hooked =
        HookOne(gAddr.RideHeliActionImpl_State_IdleAction, "RideHeliActionImpl_State_IdleAction",
            reinterpret_cast<void*>(&hkIdleAction), reinterpret_cast<void**>(&g_OrigIdleAction), g_IdleTarget)
        && HookOne(gAddr.RideHeliActionImpl_SetRootAdjustMatrix, "RideHeliActionImpl_SetRootAdjustMatrix",
            reinterpret_cast<void*>(&hkSetRootAdjustMatrix), reinterpret_cast<void**>(&g_OrigRootAdjust), g_RootTarget)
        && HookOne(gAddr.HeliPassengerControllerImpl_GetAttachMatrix, "HeliPassengerControllerImpl_GetAttachMatrix",
            reinterpret_cast<void*>(&hkGetAttachMatrix), reinterpret_cast<void**>(&g_OrigAttachMatrix), g_AttachTarget)
        && HookOne(gAddr.RealizedSoldier2Impl_SetModelAlphaAndMeshVisibilityImmediately, "RealizedSoldier2Impl_SetModelAlphaAndMeshVisibilityImmediately",
            reinterpret_cast<void*>(&hkSetModelAlpha), reinterpret_cast<void**>(&g_OrigSetAlpha), g_AlphaTarget);
    if (!hooked)
    {
        UnhookAll();
        return false;
    }
    g_QuarkTable        = gAddr.GetQuarkSystemTable ? reinterpret_cast<QuarkTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable)) : nullptr;
    g_ResetEquipMotion  = gAddr.Soldier2ActionUtil_ResetEquipMotion ? reinterpret_cast<ResetEquipMotion_t>(ResolveGameAddress(gAddr.Soldier2ActionUtil_ResetEquipMotion)) : nullptr;
    g_StartReloadMotion = gAddr.Soldier2ActionUtil_StartEquipReloadMotion ? reinterpret_cast<StartReloadMotion_t>(ResolveGameAddress(gAddr.Soldier2ActionUtil_StartEquipReloadMotion)) : nullptr;
    if (!g_QuarkTable || !g_ResetEquipMotion || !g_StartReloadMotion)
    {
        g_StartReloadMotion = nullptr;
        LogWarn("RideHeliDoorSeat: reload addresses missing for this build, heli riders don't reload on their seat");
    }
    g_Installed = true;
    return true;
}

bool Uninstall_RideHeliActionImpl_State_IdleAction_Hook()
{
    if (!g_Installed)
        return true;
    UnhookAll();
    g_Installed = false;
    return true;
}
