#include "pch.h"

#include "HeadMotion.h"

#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <map>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "FoxHashes.h"
#include "log.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "OutfitRegistry.h"
#include "CustomHeadRegistry.h"
#include "PlayerAppearance_LoadPlayerFv2s.h"

namespace
{
    struct BranchMotion
    {
        std::uint64_t                           mtar = 0;
        std::vector<outfit::headmotion::Clip>   clips;
        std::vector<outfit::headmotion::Effect> effects;
        std::string                             rest;
    };

    std::mutex g_Mutex;
    std::map<std::string, std::array<BranchMotion, outfit::kPlayerTypeMax>> g_Heads;

    constexpr std::uint64_t kGaniExtCode      = 0x1F8Aull << 51;
    constexpr std::size_t   kFile_Path        = 0x30;
    constexpr std::size_t   kMtarFile_Header  = 0x98;
    constexpr std::size_t   kMtarHeader_Flags = 0x10;
    constexpr std::uint32_t kMtarOwnBones     = 0x40000000;

    constexpr std::size_t   kWork_SlotCount     = 11;
    constexpr std::size_t   kWork_ContainerBase = 11;

    constexpr std::size_t   kRes_ActiveMask = 0x00;
    constexpr std::size_t   kRes_Count      = 0x20;
    constexpr std::size_t   kRes_AnimCtls   = 0x34;
    constexpr std::size_t   kRes_Extras     = 0x40;
    constexpr std::size_t   kRes_Bindings   = 0x44;
    constexpr std::uint32_t kRes_MaxModels  = 64;

    constexpr std::size_t   kBinding_Model   = 0x20;
    constexpr std::size_t   kBinding_BoneMap = 0x30;

    constexpr std::size_t   kModel_BoneWorld = 0xE0;
    constexpr std::size_t   kModel_BoneInfo  = 0xF0;
    constexpr std::size_t   kModel_BoneCount = 0xF8;
    constexpr std::size_t   kBoneInfo_Stride = 0x20;
    constexpr std::size_t   kBoneInfo_Local  = 0x10;
    constexpr int           kMaxBones        = 256;

    constexpr std::size_t   kControl_Inner      = 0x0C;
    constexpr std::size_t   kInner_Flags        = 0x04;
    constexpr std::uint32_t kInnerPlayEnded     = 0x200;
    constexpr std::size_t   kInner_RemapCount   = 0x0C;
    constexpr std::size_t   kInner_DeltaTime    = 0x20;
    constexpr std::size_t   kInner_TrackControl = 0x24;
    constexpr std::size_t   kInner_RemapOffset  = 0x30;
    constexpr std::size_t   kInner_TrackData    = 0x58;
    constexpr std::size_t   kInner_AnimFile     = 0x78;
    constexpr std::uint32_t kMaxTracks          = 64;

    constexpr std::uint32_t kControlPoseBuffer = 2;
    constexpr std::uint32_t kControlBoneByName = 4;
    constexpr std::uint64_t kMaxControlSize    = 0x100000;

    constexpr std::size_t   kInner_Events     = 0x70;
    constexpr std::size_t   kModel_BoneHashes = 0xE8;
    constexpr std::size_t   kModel_Skeleton   = 0xA0;

    constexpr std::size_t   kEvp_CategoryCount   = 0x04;
    constexpr std::size_t   kEvp_Categories      = 0x08;
    constexpr int           kEvp_MaxCategories   = 64;
    constexpr std::size_t   kCategory_EventCount = 0x04;
    constexpr std::size_t   kCategory_Flag       = 0x06;
    constexpr std::size_t   kCategory_Events     = 0x08;
    constexpr std::uint32_t kEffectCategory      = 0x9924F978;
    constexpr std::size_t   kEvent_KeyInfo       = 0x04;
    constexpr std::size_t   kEvent_IntCount      = 0x05;
    constexpr std::size_t   kEvent_FloatCount    = 0x06;
    constexpr std::size_t   kEvent_StringCount   = 0x07;
    constexpr std::size_t   kEvent_Keys          = 0x08;
    constexpr std::uint8_t  kEventKeyCountMask   = 0x3F;
    constexpr std::uint8_t  kEventKeysWide       = 0x40;
    constexpr std::uint32_t kOpenEnded           = 0xFFFFFFFF;
    constexpr float         kTimeUnitsPerFrame   = 5.0f;

    constexpr std::size_t   kBlock_Packages      = 0x90;
    constexpr std::size_t   kBlock_PackageCount  = 0x150;
    constexpr std::size_t   kBlock_SubBlockCount = 0xD8;
    constexpr std::size_t   kBlock_SubBlocks     = 0xE0;
    constexpr int           kBlock_MaxDepth      = 4;
    constexpr std::size_t   kPackage_GetFileSlot = 7;
    constexpr std::size_t   kPackage_FileMap     = 0x28;
    constexpr std::size_t   kMapNode_Left        = 0x00;
    constexpr std::size_t   kMapNode_Parent      = 0x08;
    constexpr std::size_t   kMapNode_Right       = 0x10;
    constexpr std::size_t   kMapNode_IsNil       = 0x19;
    constexpr std::size_t   kMapNode_Key         = 0x20;
    constexpr std::size_t   kMapNode_File        = 0x28;
    constexpr int           kMaxVisitedFiles     = 4096;
    constexpr int           kMaxTreeDepth        = 64;

    constexpr std::size_t   kFile_State       = 0x40;
    constexpr std::size_t   kFile_Data        = 0x60;
    constexpr std::size_t   kFile_Size        = 0x68;
    constexpr std::uint8_t  kFileStateLoaded  = 2;
    constexpr std::size_t   kVfxFile_Resource = 0x80;
    constexpr std::size_t   kVfx_Id           = 0x46;
    constexpr std::uint64_t kVfxIdMask        = 0xFFFFFFFFFFFFull;
    constexpr unsigned      kPathExtShift     = 51;

    constexpr std::size_t   kFcnp_FirstNode     = 0x04;
    constexpr std::size_t   kNode_Name          = 0x04;
    constexpr std::size_t   kNode_Transform     = 0x0C;
    constexpr std::size_t   kNode_Next          = 0x20;
    constexpr std::size_t   kNode_Attributes    = 0x24;
    constexpr std::size_t   kAttr_Next          = 0x02;
    constexpr std::size_t   kAttr_Name          = 0x04;
    constexpr std::size_t   kAttr_Value         = 0x0C;
    constexpr std::size_t   kTransform_Rotation = 0x10;
    constexpr int           kFcnp_MaxNodes      = 256;
    constexpr int           kFcnp_MaxAttributes = 16;

    constexpr std::size_t   kEffect_Flags    = 0x54;
    constexpr std::uint32_t kEffectAlive     = 4;
    constexpr std::uint64_t kStringIdMask    = 0x3FFFFFFFFFFFFull;
    constexpr std::uint64_t kEmptyStringId   = 0xB8A0BF169F98ull;
    constexpr std::uint32_t kEmptyStrCode32  = 0xBF169F98;
    constexpr std::uint8_t  kEffectDescFlags = 0x0B;
    constexpr std::size_t   kMaxClipEffects  = 16;
    constexpr std::size_t   kMaxLiveEffects  = 8;

    struct EngineAddresses
    {
        std::uintptr_t workActMotion;
        std::uintptr_t controlSize;
        std::uintptr_t controlCtor;
        std::uintptr_t setSpeedRate;
        std::uintptr_t setMotionData;
        std::uintptr_t advanceMotion;
        std::uintptr_t decodeTracks;
        std::uintptr_t createEffect;
        std::uintptr_t effectSetWorldMatrix;
        std::uintptr_t effectSendDestroy;
        std::uintptr_t effectSetVisible;
        std::uintptr_t effectSetKillNow;
        std::uintptr_t sharedAssign;
        std::uintptr_t blockPackageGetFile;
        std::uintptr_t boneWorldMatrix;
        std::uintptr_t effectUpdate;
        std::uintptr_t effectSync;
        std::uintptr_t effectContextAcquire;
        std::uintptr_t effectContextRelease;
        std::uintptr_t akRegisterGameObj;
        std::uintptr_t akUnregisterGameObj;
        std::uintptr_t akSetPosition;
        std::uintptr_t akPostEvent;
    };

    const EngineAddresses* AddressesForBuild()
    {
        static constexpr EngineAddresses kEn154 = {
            0x140AEF070ull, 0x141AB7940ull, 0x141AB7360ull, 0x141AB82B0ull,
            0x141AB8260ull, 0x141E19020ull, 0x141A95AB0ull,
            0x141B19340ull, 0x140407110ull, 0x141B19460ull, 0x140418280ull, 0x140418100ull,
            0x140016090ull, 0x1400DDA00ull, 0x140216050ull,
            0x141B8EA30ull, 0x141B26A00ull, 0x141B40FA0ull, 0x141B40F20ull,
            0x14033C870ull, 0x14033DB20ull, 0x14033D030ull, 0x14033C050ull };
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
            return &kEn154;
        default:
            return nullptr;
        }
    }

    struct ControlDesc
    {
        std::uint32_t flags;
        std::uint32_t motionPoints;
        void*         mtar;
        void*         model;
        void*         helpBone;
    };

    struct MotionDesc
    {
        std::uint64_t path;
        float         startTime;
        std::uint32_t reserved[3];
    };

    using ControlSize_t   = std::uint64_t (__fastcall*)(const ControlDesc*);
    using ControlCtor_t   = void* (__fastcall*)(void*, const ControlDesc*, std::uint32_t);
    using SetSpeedRate_t  = void (__fastcall*)(void*, float);
    using SetMotionData_t = bool (__fastcall*)(void*, void*, const MotionDesc*);
    using AdvanceMotion_t = void (__fastcall*)(void*);
    using DecodeTracks_t  = std::uint64_t (__fastcall*)(void*, float*, float*, std::uint32_t,
                                                        const std::uint32_t*, const float*);
    using CreateEffect_t  = void** (__fastcall*)(void**, const void*);
    using EffectMatrix_t  = std::uint32_t* (__fastcall*)(void*, std::uint32_t*, const float*);
    using EffectDestroy_t = std::uint32_t* (__fastcall*)(void*, std::uint32_t*);
    using EffectVisible_t = std::uint32_t* (__fastcall*)(void*, std::uint32_t*, bool);
    using SharedAssign_t  = void (__fastcall*)(void**, void*);
    using BoneWorld_t     = float* (__fastcall*)(const void*, float*, std::uint32_t);
    using EffectSync_t    = void (__fastcall*)(void*);
    using ContextFree_t   = void (__fastcall*)(void*, void*);
    using AkRegister_t    = std::int32_t (__fastcall*)(std::uint64_t, const char*, std::uint32_t);
    using AkUnregister_t  = std::int32_t (__fastcall*)(std::uint64_t);
    using AkSetPosition_t = std::int32_t (__fastcall*)(std::uint64_t, const float*);
    using AkPostEvent_t   = std::uint32_t (__fastcall*)(std::uint32_t, std::uint64_t, std::uint32_t, void*, void*,
                                                        std::uint32_t, void*, std::uint32_t);

    struct Engine
    {
        ControlSize_t   controlSize         = nullptr;
        ControlCtor_t   controlCtor         = nullptr;
        SetSpeedRate_t  setSpeedRate        = nullptr;
        SetMotionData_t setMotionData       = nullptr;
        AdvanceMotion_t advanceMotion       = nullptr;
        DecodeTracks_t  decodeTracks        = nullptr;
        CreateEffect_t  createEffect        = nullptr;
        EffectMatrix_t  setWorldMatrix      = nullptr;
        EffectDestroy_t sendDestroy         = nullptr;
        EffectVisible_t setVisible          = nullptr;
        EffectVisible_t setKillNow          = nullptr;
        SharedAssign_t  sharedAssign        = nullptr;
        std::uintptr_t  blockPackageGetFile = 0;
        BoneWorld_t     boneWorldMatrix     = nullptr;
        EffectSync_t    effectSync          = nullptr;
        ContextFree_t   contextRelease      = nullptr;
        AkRegister_t    akRegister          = nullptr;
        AkUnregister_t  akUnregister        = nullptr;
        AkSetPosition_t akSetPosition       = nullptr;
        AkPostEvent_t   akPostEvent         = nullptr;
    };

    const Engine* GetEngine()
    {
        static Engine         s_engine{};
        static bool           s_ok = false;
        static std::once_flag s_once;
        std::call_once(s_once, []
        {
            const EngineAddresses* a = AddressesForBuild();
            if (!a)
                return;
            s_engine.controlSize     = reinterpret_cast<ControlSize_t>(ResolveGameAddress(a->controlSize));
            s_engine.controlCtor     = reinterpret_cast<ControlCtor_t>(ResolveGameAddress(a->controlCtor));
            s_engine.setSpeedRate    = reinterpret_cast<SetSpeedRate_t>(ResolveGameAddress(a->setSpeedRate));
            s_engine.setMotionData   = reinterpret_cast<SetMotionData_t>(ResolveGameAddress(a->setMotionData));
            s_engine.advanceMotion   = reinterpret_cast<AdvanceMotion_t>(ResolveGameAddress(a->advanceMotion));
            s_engine.decodeTracks    = reinterpret_cast<DecodeTracks_t>(ResolveGameAddress(a->decodeTracks));
            s_engine.createEffect    = reinterpret_cast<CreateEffect_t>(ResolveGameAddress(a->createEffect));
            s_engine.setWorldMatrix  = reinterpret_cast<EffectMatrix_t>(ResolveGameAddress(a->effectSetWorldMatrix));
            s_engine.sendDestroy     = reinterpret_cast<EffectDestroy_t>(ResolveGameAddress(a->effectSendDestroy));
            s_engine.setVisible      = reinterpret_cast<EffectVisible_t>(ResolveGameAddress(a->effectSetVisible));
            s_engine.setKillNow      = reinterpret_cast<EffectVisible_t>(ResolveGameAddress(a->effectSetKillNow));
            s_engine.sharedAssign    = reinterpret_cast<SharedAssign_t>(ResolveGameAddress(a->sharedAssign));
            s_engine.blockPackageGetFile =
                reinterpret_cast<std::uintptr_t>(ResolveGameAddress(a->blockPackageGetFile));
            s_engine.boneWorldMatrix = reinterpret_cast<BoneWorld_t>(ResolveGameAddress(a->boneWorldMatrix));
            s_engine.effectSync      = reinterpret_cast<EffectSync_t>(ResolveGameAddress(a->effectSync));
            s_engine.contextRelease  = reinterpret_cast<ContextFree_t>(ResolveGameAddress(a->effectContextRelease));
            s_engine.akRegister      = reinterpret_cast<AkRegister_t>(ResolveGameAddress(a->akRegisterGameObj));
            s_engine.akUnregister    = reinterpret_cast<AkUnregister_t>(ResolveGameAddress(a->akUnregisterGameObj));
            s_engine.akSetPosition   = reinterpret_cast<AkSetPosition_t>(ResolveGameAddress(a->akSetPosition));
            s_engine.akPostEvent     = reinterpret_cast<AkPostEvent_t>(ResolveGameAddress(a->akPostEvent));
            s_ok = s_engine.controlSize && s_engine.controlCtor && s_engine.setSpeedRate
                && s_engine.setMotionData && s_engine.advanceMotion && s_engine.decodeTracks
                && s_engine.createEffect && s_engine.setWorldMatrix && s_engine.sendDestroy
                && s_engine.setVisible && s_engine.setKillNow && s_engine.sharedAssign && s_engine.blockPackageGetFile
                && s_engine.boneWorldMatrix;
        });
        return s_ok ? &s_engine : nullptr;
    }

    struct Quat
    {
        float x, y, z, w;
    };

    struct Vec3
    {
        float x, y, z;
    };

    struct Request
    {
        std::uint64_t fv2  = 0;
        std::uint64_t mtar = 0;
        std::uint64_t clip = 0;
        std::uint8_t  playerType   = 0;
        char          clipName[64] = {};
        char          headName[64] = {};
    };

    std::mutex                 g_RequestMutex;
    Request                    g_Request;
    std::uint32_t              g_LastGeneration = 0;
    std::atomic<std::uint32_t> g_Generation{ 0 };

    struct ModelDriver
    {
        void*         model   = nullptr;
        void*         control = nullptr;
        std::uint32_t index   = 0;
    };

    struct ContainerDriver
    {
        void*                      container  = nullptr;
        bool                       built      = false;
        std::array<ModelDriver, 8> models{};
        std::size_t                modelCount = 0;
    };

    struct ClipEffect
    {
        std::uint32_t start = 0;
        std::uint32_t end   = kOpenEnded;
        std::uint64_t point = 0;
        std::uint64_t vfx   = 0;
        bool          fired = false;
    };

    struct Attach
    {
        void*       model = nullptr;
        int         bone  = -1;
        Quat        rot{ 0.0f, 0.0f, 0.0f, 1.0f };
        Vec3        pos{ 0.0f, 0.0f, 0.0f };
    };

    struct LiveEffect
    {
        void*         effect = nullptr;
        std::uint32_t end    = kOpenEnded;
        Attach        at{};
    };

    struct Driver
    {
        std::uint32_t                                 generation = 0;
        Request                                       request{};
        bool                                          warned     = false;
        std::array<ContainerDriver, 4>                slots{};
        std::size_t                                   nextSlot   = 0;
        void*                                         effectOwner = nullptr;
        float                                         elapsed     = 0.0f;
        std::array<ClipEffect, kMaxClipEffects>       events{};
        std::size_t                                   eventCount  = 0;
        std::array<LiveEffect, kMaxLiveEffects>       live{};
        bool                                          clipEnded   = false;
    };

    struct EffectRequest
    {
        bool          play     = false;
        std::uint64_t fv2      = 0;
        std::uint64_t vfx      = 0;
        std::uint64_t point    = 0;
        int           attempts = 0;
        char          name[64]     = {};
        char          headName[64] = {};
    };

    struct NamedEffect
    {
        void*         container = nullptr;
        std::uint64_t fv2       = 0;
        LiveEffect    live{};
        bool          automatic = false;
        int           idle      = 0;
        char          name[64]     = {};
        char          headName[64] = {};
    };

    constexpr int         kMaxRequestAttempts = 120;
    constexpr int         kMaxIdleCalls       = 8;
    constexpr std::size_t kMaxPendingRequests = 32;
    constexpr std::size_t kMaxEffectModels    = 8;

    std::mutex                               g_EffectRequestMutex;
    std::vector<EffectRequest>               g_EffectRequests;
    std::array<NamedEffect, kMaxLiveEffects> g_Named{};
    std::atomic<bool>                        g_EffectsBusy{ false };
    std::atomic<bool>                        g_EffectsReset{ false };
    std::atomic<bool>                        g_RestPending{ false };
    std::atomic<bool>                        g_RestWanted{ false };

    std::mutex        g_DriverMutex;
    Driver            g_Driver;
    std::atomic<bool> g_DriverHolds{ false };

    void ReleaseSlot(ContainerDriver& slot)
    {
        for (std::size_t i = 0; i < slot.modelCount; ++i)
        {
            if (slot.models[i].control)
                _aligned_free(slot.models[i].control);
            slot.models[i] = ModelDriver{};
        }
        slot.modelCount = 0;
        slot.built      = false;
    }

    struct DeferredUpdate
    {
        std::atomic<void*> effect{ nullptr };
        void*              instance = nullptr;
        float              delta    = 0.0f;
        std::atomic<int>   state{ 0 };
    };

    constexpr std::size_t kMaxDeferred     = kMaxLiveEffects * 3;
    constexpr std::size_t kInstance_Effect = 0x10;
    constexpr int         kDeferIdle       = 0;
    constexpr int         kDeferStoring    = 1;
    constexpr int         kDeferPending    = 2;
    constexpr int         kDeferRunning    = 3;

    std::array<DeferredUpdate, kMaxDeferred>           g_Deferred;
    std::atomic<int>                                   g_DeferredCount{ 0 };
    std::atomic<void*>                                 g_EffectContextPool{ nullptr };
    std::atomic<outfit::headmotion::EffectUpdate_t>         g_EffectUpdate{ nullptr };
    std::atomic<outfit::headmotion::EffectContextAcquire_t> g_EffectContextAcquire{ nullptr };

    DeferredUpdate* FindDeferred(void* effect)
    {
        for (DeferredUpdate& d : g_Deferred)
            if (d.effect.load(std::memory_order_acquire) == effect)
                return &d;
        return nullptr;
    }

    void TrackDeferred(void* effect)
    {
        if (!effect || !g_EffectUpdate.load(std::memory_order_acquire))
            return;
        for (DeferredUpdate& d : g_Deferred)
        {
            void* expected = nullptr;
            if (d.effect.compare_exchange_strong(expected, effect, std::memory_order_acq_rel))
            {
                d.state.store(kDeferIdle, std::memory_order_release);
                g_DeferredCount.fetch_add(1, std::memory_order_acq_rel);
                return;
            }
        }
    }

    void ForgetDeferred(void* effect)
    {
        if (!effect)
            return;
        if (DeferredUpdate* d = FindDeferred(effect))
        {
            d->state.store(kDeferIdle, std::memory_order_release);
            d->effect.store(nullptr, std::memory_order_release);
            g_DeferredCount.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    void ForgetAllDeferred()
    {
        for (DeferredUpdate& d : g_Deferred)
        {
            d.state.store(kDeferIdle, std::memory_order_release);
            d.effect.store(nullptr, std::memory_order_release);
        }
        g_DeferredCount.store(0, std::memory_order_release);
    }

    void* AcquireContextSEH(outfit::headmotion::EffectContextAcquire_t acquire, void* pool)
    {
        __try
        {
            return acquire(pool);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void ReleaseContextSEH(const Engine& e, void* pool, void* context)
    {
        __try
        {
            e.contextRelease(pool, context);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void SyncAndUpdateSEH(const Engine& e, outfit::headmotion::EffectUpdate_t update,
                          void* effect, void* instance, float delta, void* context)
    {
        __try
        {
            if (*reinterpret_cast<void* const*>(static_cast<const std::uint8_t*>(instance) + kInstance_Effect) != effect)
                return;
            e.effectSync(instance);
            update(instance, delta, context);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void RunDeferred(const Engine& e, void* effect)
    {
        DeferredUpdate* d = FindDeferred(effect);
        if (!d)
            return;
        int state = kDeferPending;
        if (!d->state.compare_exchange_strong(state, kDeferRunning, std::memory_order_acq_rel))
            return;
        const auto update  = g_EffectUpdate.load(std::memory_order_acquire);
        const auto acquire = g_EffectContextAcquire.load(std::memory_order_acquire);
        void* const pool   = g_EffectContextPool.load(std::memory_order_acquire);
        if (update && acquire && pool && e.effectSync && e.contextRelease)
        {
            if (void* context = AcquireContextSEH(acquire, pool))
            {
                SyncAndUpdateSEH(e, update, effect, d->instance, d->delta, context);
                ReleaseContextSEH(e, pool, context);
            }
        }
        d->state.store(kDeferIdle, std::memory_order_release);
    }

    void DestroyEffectSEH(const Engine& e, void*& effect)
    {
        ForgetDeferred(effect);
        __try
        {
            std::uint32_t err = 0;
            e.setKillNow(effect, &err, true);
            e.sendDestroy(effect, &err);
            e.sharedAssign(&effect, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            effect = nullptr;
        }
    }

    void ReleaseEffectSEH(const Engine& e, void*& effect)
    {
        ForgetDeferred(effect);
        __try
        {
            e.sharedAssign(&effect, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            effect = nullptr;
        }
    }

    void DestroyEffects(Driver& d)
    {
        const Engine* e = GetEngine();
        for (LiveEffect& live : d.live)
        {
            if (live.effect && e)
                DestroyEffectSEH(*e, live.effect);
            live = LiveEffect{};
        }
    }

    void ReleaseModels(Driver& d)
    {
        DestroyEffects(d);
        for (ContainerDriver& slot : d.slots)
        {
            ReleaseSlot(slot);
            slot.container = nullptr;
        }
        d.nextSlot    = 0;
        d.effectOwner = nullptr;
        d.eventCount  = 0;
        d.elapsed     = 0.0f;
        d.clipEnded   = false;
    }

    ContainerDriver& SlotFor(Driver& d, void* container)
    {
        for (ContainerDriver& slot : d.slots)
            if (slot.container == container)
                return slot;
        for (ContainerDriver& slot : d.slots)
            if (!slot.container)
            {
                slot.container = container;
                return slot;
            }
        ContainerDriver& slot = d.slots[d.nextSlot];
        d.nextSlot = (d.nextSlot + 1) % d.slots.size();
        if (slot.container == d.effectOwner)
        {
            DestroyEffects(d);
            d.effectOwner = nullptr;
        }
        ReleaseSlot(slot);
        slot.container = container;
        return slot;
    }

    void WarnOnce(Driver& d, const char* what)
    {
        if (d.warned)
            return;
        d.warned = true;
        LogWarn("PlayHeadMotion '%s': head '%s' %s",
            d.request.clipName, d.request.headName, what);
    }

    bool IsOwnBonesArchiveSEH(void* file)
    {
        __try
        {
            const auto* header = *reinterpret_cast<const std::uint8_t* const*>(
                static_cast<const std::uint8_t*>(file) + kMtarFile_Header);
            if (!header)
                return false;
            const std::uint32_t flags =
                *reinterpret_cast<const std::uint32_t*>(header + kMtarHeader_Flags);
            return (flags & kMtarOwnBones) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void* FindContainerSEH(void* work, std::uint64_t fv2Code)
    {
        __try
        {
            auto* const* slots = static_cast<void* const*>(work);
            for (std::size_t s = 0; s < kWork_SlotCount; ++s)
            {
                const auto* file = static_cast<const std::uint8_t*>(slots[s]);
                if (file && *reinterpret_cast<const std::uint64_t*>(file + kFile_Path) == fv2Code)
                    return slots[kWork_ContainerBase + s];
            }
            return nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint32_t ModelCountSEH(void* container)
    {
        __try
        {
            const std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(
                static_cast<const std::uint8_t*>(container) + kRes_Count);
            return count < kRes_MaxModels ? count : kRes_MaxModels;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void* const* ResArray(const std::uint8_t* container, std::size_t field)
    {
        const std::uint32_t rel = *reinterpret_cast<const std::uint32_t*>(container + field);
        return rel ? reinterpret_cast<void* const*>(container + rel) : nullptr;
    }

    void* PoseCopiedBindingSEH(void* container, std::uint32_t index)
    {
        __try
        {
            const auto* c = static_cast<const std::uint8_t*>(container);
            const std::uint64_t mask = *reinterpret_cast<const std::uint64_t*>(c + kRes_ActiveMask);
            if (index >= kRes_MaxModels || ((mask >> index) & 1) == 0)
                return nullptr;
            void* const* anims    = ResArray(c, kRes_AnimCtls);
            void* const* extras   = ResArray(c, kRes_Extras);
            void* const* bindings = ResArray(c, kRes_Bindings);
            if (!bindings || (anims && anims[index]) || (extras && extras[index]))
                return nullptr;
            return bindings[index];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void* ModelOfBindingSEH(void* binding)
    {
        __try
        {
            return *reinterpret_cast<void* const*>(
                static_cast<const std::uint8_t*>(binding) + kBinding_Model);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool ControlSizeSEH(const Engine& e, const ControlDesc* desc, std::uint64_t* out)
    {
        __try
        {
            *out = e.controlSize(desc);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool StartControlSEH(const Engine& e, void* buffer, const ControlDesc* desc,
                         std::uint32_t size, void* model, std::uint64_t clip)
    {
        __try
        {
            e.controlCtor(buffer, desc, size);
            e.setSpeedRate(buffer, 1.0f);
            const MotionDesc motion{ clip, 0.0f, { 0, 0, 0 } };
            return e.setMotionData(buffer, model, &motion);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    int MatchedClipBonesSEH(void* control)
    {
        __try
        {
            const auto* outer = static_cast<const std::uint8_t*>(control);
            const auto* inner = outer + *reinterpret_cast<const std::uint32_t*>(outer + kControl_Inner);
            const std::uint32_t remapOff = *reinterpret_cast<const std::uint32_t*>(inner + kInner_RemapOffset);
            const int count = *reinterpret_cast<const std::int16_t*>(inner + kInner_RemapCount);
            if (!remapOff || count <= 0)
                return 0;
            const auto* remap = reinterpret_cast<const std::int16_t*>(inner + remapOff);
            int matched = 0;
            for (int i = 0; i < count; ++i)
                if (remap[i] >= 0)
                    ++matched;
            return matched;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void* CreateControl(const Engine& e, void* mtar, void* model, std::uint64_t clip)
    {
        const ControlDesc desc{ kControlPoseBuffer | kControlBoneByName, 0, mtar, model, nullptr };
        std::uint64_t size = 0;
        if (!ControlSizeSEH(e, &desc, &size) || size == 0 || size > kMaxControlSize)
            return nullptr;
        void* buffer = _aligned_malloc(static_cast<std::size_t>(size), 16);
        if (!buffer)
            return nullptr;
        std::memset(buffer, 0, static_cast<std::size_t>(size));
        if (!StartControlSEH(e, buffer, &desc, static_cast<std::uint32_t>(size), model, clip)
            || MatchedClipBonesSEH(buffer) == 0)
        {
            _aligned_free(buffer);
            return nullptr;
        }
        return buffer;
    }

    Quat Conj(const Quat& q)
    {
        return { -q.x, -q.y, -q.z, q.w };
    }

    Quat Mul(const Quat& a, const Quat& b)
    {
        return {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
    }

    Vec3 Rotate(const Quat& q, const Vec3& v)
    {
        const Vec3 t{
            2.0f * (q.y * v.z - q.z * v.y),
            2.0f * (q.z * v.x - q.x * v.z),
            2.0f * (q.x * v.y - q.y * v.x) };
        return {
            v.x + q.w * t.x + (q.y * t.z - q.z * t.y),
            v.y + q.w * t.y + (q.z * t.x - q.x * t.z),
            v.z + q.w * t.z + (q.x * t.y - q.y * t.x) };
    }

    struct TrackPose
    {
        float rot[kMaxBones][4];
        float pos[kMaxBones][3];
        bool  tracked[kMaxBones];
    };

    TrackPose g_Pose{};

    void ComposeLocal(const Quat& q, const Vec3& offset, const float* p, float* out)
    {
        const float x = q.x, y = q.y, z = q.z, w = q.w;
        const float r[3][3] = {
            { 1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + z * w),        2.0f * (x * z - y * w) },
            { 2.0f * (x * y - z * w),        1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + x * w) },
            { 2.0f * (x * z + y * w),        2.0f * (y * z - x * w),        1.0f - 2.0f * (x * x + y * y) } };
        for (int k = 0; k < 3; ++k)
            for (int c = 0; c < 3; ++c)
                out[k * 4 + c] = r[k][0] * p[c] + r[k][1] * p[4 + c] + r[k][2] * p[8 + c];
        for (int c = 0; c < 3; ++c)
            out[12 + c] = offset.x * p[c] + offset.y * p[4 + c] + offset.z * p[8 + c] + p[12 + c];
    }

    void ApplyPose(void* binding, void* model, const TrackPose& pose)
    {
        auto* m = static_cast<std::uint8_t*>(model);
        auto* world = *reinterpret_cast<float**>(m + kModel_BoneWorld);
        const auto* info = *reinterpret_cast<const std::uint8_t* const*>(m + kModel_BoneInfo);
        const int count = *reinterpret_cast<const std::int16_t*>(m + kModel_BoneCount);
        const auto* map = *reinterpret_cast<const std::int32_t* const*>(
            static_cast<const std::uint8_t*>(binding) + kBinding_BoneMap);
        if (!world || !info || !map || count <= 0 || count > kMaxBones)
            return;

        bool moved[kMaxBones] = {};
        for (int i = 0; i < count; ++i)
        {
            if (map[i] >= 0)
                continue;
            const std::uint8_t* bone = info + static_cast<std::size_t>(i) * kBoneInfo_Stride;
            const int parent = *reinterpret_cast<const std::int32_t*>(bone);
            if (parent < 0 || parent >= i)
                continue;
            if (!pose.tracked[i] && !moved[parent])
                continue;

            const auto* bind = reinterpret_cast<const float*>(bone + kBoneInfo_Local);
            Quat local{ 0.0f, 0.0f, 0.0f, 1.0f };
            Vec3 offset{ bind[0], bind[1], bind[2] };
            if (pose.tracked[i])
            {
                local  = { pose.rot[i][0], pose.rot[i][1], pose.rot[i][2], pose.rot[i][3] };
                offset = { pose.pos[i][0], pose.pos[i][1], pose.pos[i][2] };
            }

            ComposeLocal(local, offset, world + parent * 16, world + i * 16);
            moved[i] = true;
        }
    }

    bool ReadTrackPose(const Engine& e, void* control, void* model, const float* timeBase,
                       TrackPose& pose)
    {
        auto* outer = static_cast<std::uint8_t*>(control);
        auto* inner = outer + *reinterpret_cast<const std::uint32_t*>(outer + kControl_Inner);
        const float delta = *timeBase;
        *reinterpret_cast<float*>(inner + kInner_DeltaTime) = delta > 0.0f ? delta : 0.0f;
        e.advanceMotion(inner);

        if (!*reinterpret_cast<void* const*>(inner + kInner_AnimFile))
            return false;
        const std::uint32_t trackOff = *reinterpret_cast<const std::uint32_t*>(inner + kInner_TrackControl);
        const std::uint32_t remapOff = *reinterpret_cast<const std::uint32_t*>(inner + kInner_RemapOffset);
        const auto* data = *reinterpret_cast<const std::uint32_t* const*>(inner + kInner_TrackData);
        const int remapCount = *reinterpret_cast<const std::int16_t*>(inner + kInner_RemapCount);
        if (!trackOff || !remapOff || !data || remapCount <= 0)
            return false;

        std::uint32_t tracks = *data;
        if (tracks > kMaxTracks)
            tracks = kMaxTracks;
        alignas(16) float pos[kMaxTracks][4] = {};
        alignas(16) float rot[kMaxTracks][4] = {};
        alignas(16) const float zero[4] = {};
        e.decodeTracks(inner + trackOff, &pos[0][0], &rot[0][0], tracks, data, zero);

        auto* m = static_cast<const std::uint8_t*>(model);
        const auto* info = *reinterpret_cast<const std::uint8_t* const*>(m + kModel_BoneInfo);
        const int boneCount = *reinterpret_cast<const std::int16_t*>(m + kModel_BoneCount);
        const auto* remap = reinterpret_cast<const std::int16_t*>(inner + remapOff);
        if (!info || boneCount <= 0 || boneCount > kMaxBones)
            return false;

        for (std::uint32_t t = 1; t < tracks && static_cast<int>(t - 1) < remapCount; ++t)
        {
            const int bone = remap[t - 1];
            if (bone < 0 || bone >= boneCount)
                continue;
            const auto* bind = reinterpret_cast<const float*>(
                info + static_cast<std::size_t>(bone) * kBoneInfo_Stride + kBoneInfo_Local);
            for (int k = 0; k < 4; ++k)
                pose.rot[bone][k] = rot[t][k];
            for (int k = 0; k < 3; ++k)
                pose.pos[bone][k] = bind[k] + pos[t][k];
            pose.tracked[bone] = true;
        }
        return true;
    }

    bool DriveModelSEH(const Engine& e, void* binding, void* model, void* control,
                       const float* timeBase, TrackPose& pose)
    {
        __try
        {
            std::memset(pose.tracked, 0, sizeof(pose.tracked));
            if (ReadTrackPose(e, control, model, timeBase, pose))
                ApplyPose(binding, model, pose);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ReadEffectEvent(const std::uint8_t* ev, ClipEffect* out, std::size_t& count, std::size_t max)
    {
        const std::uint8_t  keyInfo = ev[kEvent_KeyInfo];
        const std::uint32_t keys    = keyInfo & kEventKeyCountMask;
        const bool          wide    = (keyInfo & kEventKeysWide) != 0;
        if (keys == 0 || ev[kEvent_StringCount] < 2)
            return;
        const std::size_t    values   = static_cast<std::size_t>(ev[kEvent_IntCount]) + ev[kEvent_FloatCount];
        const std::uint8_t*  keyData  = ev + kEvent_Keys;
        const std::size_t    keyBytes = (static_cast<std::size_t>(keys) * (wide ? 4 : 2) + 3) & ~static_cast<std::size_t>(3);
        const std::uint8_t*  strings  = keyData + keyBytes + values * 4;
        std::uint64_t point = 0;
        std::uint64_t vfx   = 0;
        std::memcpy(&point, strings, sizeof(point));
        std::memcpy(&vfx, strings + sizeof(point), sizeof(vfx));
        for (std::uint32_t k = 0; k < keys && count < max; ++k)
        {
            ClipEffect& c = out[count++];
            if (wide)
            {
                std::uint16_t start = 0;
                std::uint16_t end   = 0;
                std::memcpy(&start, keyData + k * 4, sizeof(start));
                std::memcpy(&end, keyData + k * 4 + 2, sizeof(end));
                c.start = start;
                c.end   = end == 0xFFFF ? kOpenEnded : end;
            }
            else
            {
                c.start = keyData[k * 2];
                c.end   = keyData[k * 2 + 1] == 0xFF ? kOpenEnded : keyData[k * 2 + 1];
            }
            c.point = point & kStringIdMask;
            c.vfx   = vfx & kStringIdMask;
            c.fired = false;
        }
    }

    std::size_t ReadClipEffectsSEH(void* control, ClipEffect* out, std::size_t max)
    {
        __try
        {
            const auto* outer = static_cast<const std::uint8_t*>(control);
            const auto* inner = outer + *reinterpret_cast<const std::uint32_t*>(outer + kControl_Inner);
            const auto* evp   = *reinterpret_cast<const std::uint8_t* const*>(inner + kInner_Events);
            if (!evp)
                return 0;
            const int   categories = *reinterpret_cast<const std::int16_t*>(evp + kEvp_CategoryCount);
            std::size_t count      = 0;
            for (int c = 0; c < categories && c < kEvp_MaxCategories; ++c)
            {
                const std::uint32_t catOff = *reinterpret_cast<const std::uint32_t*>(
                    evp + kEvp_Categories + static_cast<std::size_t>(c) * 4);
                if (!catOff)
                    continue;
                const std::uint8_t* cat = evp + catOff;
                if (*reinterpret_cast<const std::uint32_t*>(cat) != kEffectCategory
                    || *reinterpret_cast<const std::uint16_t*>(cat + kCategory_Flag) != 0)
                    continue;
                const std::uint16_t events = *reinterpret_cast<const std::uint16_t*>(cat + kCategory_EventCount);
                for (std::uint16_t i = 0; i < events && count < max; ++i)
                {
                    const std::uint32_t evOff = *reinterpret_cast<const std::uint32_t*>(
                        cat + kCategory_Events + static_cast<std::size_t>(i) * 4);
                    if (evOff)
                        ReadEffectEvent(cat + evOff, out, count, max);
                }
            }
            return count;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    using FileVisitor = bool (*)(std::uint64_t pathId, const std::uint8_t* file, void* ctx);

    bool VisitFileMap(const std::uint8_t* head, FileVisitor visit, void* ctx, int& budget)
    {
        const std::uint8_t* stack[kMaxTreeDepth];
        int top = 0;
        const auto* root = *reinterpret_cast<const std::uint8_t* const*>(head + kMapNode_Parent);
        if (root && root != head && !root[kMapNode_IsNil])
            stack[top++] = root;
        while (top > 0 && budget-- > 0)
        {
            const std::uint8_t* node = stack[--top];
            const std::uint64_t key  = *reinterpret_cast<const std::uint64_t*>(node + kMapNode_Key);
            const auto*         file = *reinterpret_cast<const std::uint8_t* const*>(node + kMapNode_File);
            if (file && visit(key, file, ctx))
                return true;
            const auto* left  = *reinterpret_cast<const std::uint8_t* const*>(node + kMapNode_Left);
            const auto* right = *reinterpret_cast<const std::uint8_t* const*>(node + kMapNode_Right);
            if (left && !left[kMapNode_IsNil] && top < kMaxTreeDepth)
                stack[top++] = left;
            if (right && !right[kMapNode_IsNil] && top < kMaxTreeDepth)
                stack[top++] = right;
        }
        return false;
    }

    bool VisitBlock(const std::uint8_t* block, std::uintptr_t packageGetFile, FileVisitor visit,
                    void* ctx, int depth, int& budget)
    {
        if (!block || depth > kBlock_MaxDepth)
            return false;
        const auto* const* packages =
            *reinterpret_cast<const std::uint8_t* const* const*>(block + kBlock_Packages);
        const std::uint32_t packageCount = *reinterpret_cast<const std::uint32_t*>(block + kBlock_PackageCount);
        for (std::uint32_t i = 0; packages && i < packageCount && budget > 0; ++i)
        {
            const std::uint8_t* package = packages[i];
            if (!package)
                continue;
            const auto* vtbl = *reinterpret_cast<const std::uintptr_t* const*>(package);
            if (!vtbl || vtbl[kPackage_GetFileSlot] != packageGetFile)
                continue;
            const auto* head = *reinterpret_cast<const std::uint8_t* const*>(package + kPackage_FileMap);
            if (head && VisitFileMap(head, visit, ctx, budget))
                return true;
        }
        const auto* const* subBlocks =
            *reinterpret_cast<const std::uint8_t* const* const*>(block + kBlock_SubBlocks);
        const std::uint32_t subCount = *reinterpret_cast<const std::uint32_t*>(block + kBlock_SubBlockCount);
        for (std::uint32_t i = 0; subBlocks && i < subCount && budget > 0; ++i)
            if (VisitBlock(subBlocks[i], packageGetFile, visit, ctx, depth + 1, budget))
                return true;
        return false;
    }

    bool VisitBlockSEH(void* block, std::uintptr_t packageGetFile, FileVisitor visit, void* ctx)
    {
        __try
        {
            int budget = kMaxVisitedFiles;
            return VisitBlock(static_cast<const std::uint8_t*>(block), packageGetFile, visit, ctx, 0, budget);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint64_t ExtCode(const char* samplePath)
    {
        return FoxHashes::PathCode64Ext(samplePath) >> kPathExtShift;
    }

    bool LoadedRawData(const std::uint8_t* file, const std::uint8_t*& data, std::uint64_t& size)
    {
        if (file[kFile_State] != kFileStateLoaded)
            return false;
        data = *reinterpret_cast<const std::uint8_t* const*>(file + kFile_Data);
        size = *reinterpret_cast<const std::uint64_t*>(file + kFile_Size);
        return data && size > 0;
    }

    struct VfxQuery
    {
        std::uint64_t ext   = 0;
        std::uint64_t id    = 0;
        void*         file  = nullptr;
        bool          found = false;
    };

    bool MatchVfx(std::uint64_t pathId, const std::uint8_t* file, void* ctx)
    {
        auto* q = static_cast<VfxQuery*>(ctx);
        const std::uint8_t* data = nullptr;
        std::uint64_t       size = 0;
        if ((pathId >> kPathExtShift) != q->ext || !LoadedRawData(file, data, size)
            || size < kVfx_Id + 6)
            return false;
        std::uint64_t id = 0;
        std::memcpy(&id, data + kVfx_Id, 6);
        if (id != q->id)
            return false;
        q->found = true;
        if (!*reinterpret_cast<void* const*>(file + kVfxFile_Resource))
            return false;
        q->file = const_cast<std::uint8_t*>(file);
        return true;
    }

    struct PointQuery
    {
        std::uint64_t ext      = 0;
        std::uint64_t point    = 0;
        bool          found    = false;
        std::uint32_t boneHash = 0;
        Quat          rot{ 0.0f, 0.0f, 0.0f, 1.0f };
        Vec3          pos{ 0.0f, 0.0f, 0.0f };
    };

    const char* FcnpString(const std::uint8_t* base, std::uint32_t offset,
                           const std::uint8_t* data, std::uint64_t size)
    {
        if (!offset)
            return nullptr;
        const std::uint8_t* s = base + offset;
        return (s >= data && s < data + size) ? reinterpret_cast<const char*>(s) : nullptr;
    }

    const char* FcnpParent(const std::uint8_t* node, const std::uint8_t* data, std::uint64_t size)
    {
        const std::uint32_t attrOff = *reinterpret_cast<const std::uint32_t*>(node + kNode_Attributes);
        if (!attrOff)
            return nullptr;
        const std::uint8_t* attr = node + attrOff;
        for (int i = 0; i < kFcnp_MaxAttributes && attr >= data && attr + kAttr_Value + 8 <= data + size; ++i)
        {
            const char* name = FcnpString(attr + kAttr_Name,
                *reinterpret_cast<const std::uint32_t*>(attr + kAttr_Name + 4), data, size);
            if (name && std::strcmp(name, "Parent") == 0)
                return FcnpString(attr + kAttr_Value,
                    *reinterpret_cast<const std::uint32_t*>(attr + kAttr_Value + 4), data, size);
            const std::uint16_t next = *reinterpret_cast<const std::uint16_t*>(attr + kAttr_Next);
            if (!next)
                break;
            attr += next;
        }
        return nullptr;
    }

    bool MatchPoint(std::uint64_t pathId, const std::uint8_t* file, void* ctx)
    {
        auto* q = static_cast<PointQuery*>(ctx);
        const std::uint8_t* data = nullptr;
        std::uint64_t       size = 0;
        if ((pathId >> kPathExtShift) != q->ext || !LoadedRawData(file, data, size)
            || size < kFcnp_FirstNode + 4)
            return false;
        std::uint32_t       offset = *reinterpret_cast<const std::uint32_t*>(data + kFcnp_FirstNode);
        const std::uint8_t* node   = data + offset;
        for (int i = 0; i < kFcnp_MaxNodes && offset && node + kNode_Attributes + 4 <= data + size; ++i)
        {
            const char* name = FcnpString(node, *reinterpret_cast<const std::uint32_t*>(node + kNode_Name), data, size);
            if (name && (FoxHashes::StrCode64(name) & kStringIdMask) == q->point)
            {
                const char*         parent = FcnpParent(node, data, size);
                const std::uint32_t xfOff  = *reinterpret_cast<const std::uint32_t*>(node + kNode_Transform);
                if (!parent || !xfOff || node + xfOff + kTransform_Rotation + 16 > data + size)
                    return false;
                const auto* xf = reinterpret_cast<const float*>(node + xfOff);
                q->pos      = { xf[0], xf[1], xf[2] };
                q->rot      = { xf[4], xf[5], xf[6], xf[7] };
                q->boneHash = static_cast<std::uint32_t>(FoxHashes::StrCode64(parent));
                q->found    = true;
                return true;
            }
            offset = *reinterpret_cast<const std::uint32_t*>(node + kNode_Next);
            node += offset;
        }
        return false;
    }

    bool FindBoneSEH(void* const* models, std::size_t count, std::uint32_t hash, Attach& at)
    {
        __try
        {
            for (std::size_t m = 0; m < count; ++m)
            {
                const auto* model  = static_cast<const std::uint8_t*>(models[m]);
                const auto* hashes = *reinterpret_cast<const std::uint32_t* const*>(model + kModel_BoneHashes);
                const int   bones  = *reinterpret_cast<const std::int16_t*>(model + kModel_BoneCount);
                for (int b = 0; hashes && b < bones && b < kMaxBones; ++b)
                {
                    if (hashes[b] != hash)
                        continue;
                    at.model = models[m];
                    at.bone  = b;
                    return true;
                }
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool AttachWorldSEH(const Engine& e, const Attach& at, float* out)
    {
        __try
        {
            const auto* m     = static_cast<const std::uint8_t*>(at.model);
            if (!m)
                return false;
            const auto* world = *reinterpret_cast<const float* const*>(m + kModel_BoneWorld);
            const int   count = *reinterpret_cast<const std::int16_t*>(m + kModel_BoneCount);
            if (!world || at.bone < 0 || at.bone >= count)
                return false;
            alignas(16) float bone[16];
            e.boneWorldMatrix(m + kModel_Skeleton, bone, static_cast<std::uint32_t>(at.bone));
            ComposeLocal(at.rot, at.pos, bone, out);
            out[3]  = 0.0f;
            out[7]  = 0.0f;
            out[11] = 0.0f;
            out[15] = 1.0f;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct alignas(16) EffectDesc
    {
        float         matrix[16];
        void*         file;
        std::uint32_t reserved0;
        std::uint32_t pad0;
        std::uint64_t name;
        std::uint64_t reserved1;
        std::uint32_t tag;
        std::uint32_t pad1;
        void*         owner;
        std::uint64_t reserved2;
        std::uint32_t fixedId;
        std::uint32_t id;
        std::uint8_t  flags;
        std::uint8_t  pad2[15];
    };
    static_assert(offsetof(EffectDesc, file) == 0x40, "EffectDesc file");
    static_assert(offsetof(EffectDesc, name) == 0x50, "EffectDesc name");
    static_assert(offsetof(EffectDesc, tag) == 0x60, "EffectDesc tag");
    static_assert(offsetof(EffectDesc, owner) == 0x68, "EffectDesc owner");
    static_assert(offsetof(EffectDesc, fixedId) == 0x78, "EffectDesc fixedId");
    static_assert(offsetof(EffectDesc, flags) == 0x80, "EffectDesc flags");

    void* CreateEffectSEH(const Engine& e, const EffectDesc& desc)
    {
        __try
        {
            void* effect = nullptr;
            e.createEffect(&effect, &desc);
            if (effect)
            {
                std::uint32_t err = 0;
                e.setVisible(effect, &err, true);
            }
            return effect;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool MoveEffectSEH(const Engine& e, void* effect, const float* matrix)
    {
        __try
        {
            const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(
                static_cast<const std::uint8_t*>(effect) + kEffect_Flags);
            if ((flags & kEffectAlive) == 0)
                return false;
            std::uint32_t err = 0;
            e.setWorldMatrix(effect, &err, matrix);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool VfxLoadedSEH(void* file)
    {
        __try
        {
            return *reinterpret_cast<void* const*>(static_cast<const std::uint8_t*>(file) + kVfxFile_Resource)
                != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ResolveAttach(const Engine& e, void* block, void* const* models, std::size_t count,
                       std::uint64_t point, Attach& at, const char* api, const char* name, const char* headName)
    {
        PointQuery q{};
        q.ext   = ExtCode("/Assets/a.fcnp");
        q.point = point;
        if (block && point)
            VisitBlockSEH(block, e.blockPackageGetFile, &MatchPoint, &q);
        if (q.found && FindBoneSEH(models, count, q.boneHash, at))
        {
            at.rot = q.rot;
            at.pos = q.pos;
            return true;
        }
        if (point && FindBoneSEH(models, count, static_cast<std::uint32_t>(point), at))
            return true;
        if (!FindBoneSEH(models, count, static_cast<std::uint32_t>(FoxHashes::StrCode64("SKL_004_HEAD")), at))
            return false;
        static std::atomic<bool> s_fallbackLogged{ false };
        if (point && !s_fallbackLogged.exchange(true))
            LogWarn("%s '%s': head '%s' effect attach point not in the head's .fcnp, using the head bone",
                api, name, headName);
        return true;
    }

    void* SpawnEffect(const Engine& e, void* file, const Attach& at)
    {
        EffectDesc desc{};
        if (!AttachWorldSEH(e, at, desc.matrix))
            return nullptr;
        desc.file  = file;
        desc.name  = kEmptyStringId;
        desc.tag   = kEmptyStrCode32;
        desc.flags = kEffectDescFlags;
        void* effect = CreateEffectSEH(e, desc);
        TrackDeferred(effect);
        return effect;
    }

    void TickLive(const Engine& e, LiveEffect& live, bool keep)
    {
        alignas(16) float matrix[16];
        if (!keep || !AttachWorldSEH(e, live.at, matrix))
        {
            DestroyEffectSEH(e, live.effect);
            live = LiveEffect{};
            return;
        }
        if (!MoveEffectSEH(e, live.effect, matrix))
        {
            ReleaseEffectSEH(e, live.effect);
            live = LiveEffect{};
            return;
        }
        RunDeferred(e, live.effect);
    }

    std::size_t SlotModels(const ContainerDriver& slot, void** out)
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < slot.modelCount && n < kMaxEffectModels; ++i)
            out[n++] = slot.models[i].model;
        return n;
    }

    bool HasModel(void* const* models, std::size_t count, void* model)
    {
        for (std::size_t i = 0; i < count; ++i)
            if (models[i] == model)
                return true;
        return false;
    }

    void FireEffect(Driver& d, const ContainerDriver& slot, const Engine& e, const ClipEffect& ev)
    {
        LiveEffect* live = nullptr;
        for (LiveEffect& l : d.live)
        {
            if (!l.effect)
            {
                live = &l;
                break;
            }
        }
        if (!live)
            return;

        void* block = outfit::ddheadlayers::FindLocalHeadBlock();
        VfxQuery vfx{};
        vfx.ext = ExtCode("/Assets/a.vfx");
        vfx.id  = ev.vfx & kVfxIdMask;
        if (block)
            VisitBlockSEH(block, e.blockPackageGetFile, &MatchVfx, &vfx);
        if (!vfx.file)
        {
            WarnOnce(d, vfx.found ? "clip effect .vfx did not load in this game"
                                  : "clip effect .vfx is not in the head's pack");
            return;
        }

        void* models[kMaxEffectModels];
        const std::size_t count = SlotModels(slot, models);
        Attach at{};
        if (!ResolveAttach(e, block, models, count, ev.point, at,
                "PlayHeadMotion", d.request.clipName, d.request.headName))
        {
            WarnOnce(d, "clip effect has no attach point on the head");
            return;
        }

        void* effect = SpawnEffect(e, vfx.file, at);
        if (!effect)
        {
            WarnOnce(d, "clip effect could not be created");
            return;
        }
        live->effect = effect;
        live->end    = ev.end;
        live->at     = at;
    }

    void UpdateEffects(Driver& d, const ContainerDriver& slot, const Engine& e, float delta)
    {
        if (d.effectOwner != slot.container)
            return;
        if (delta > 0.0f)
            d.elapsed += delta;
        const float frame = d.elapsed / kTimeUnitsPerFrame;

        for (std::size_t i = 0; i < d.eventCount; ++i)
        {
            ClipEffect& ev = d.events[i];
            if (ev.fired || frame < static_cast<float>(ev.start))
                continue;
            ev.fired = true;
            FireEffect(d, slot, e, ev);
        }

        void* models[kMaxEffectModels];
        const std::size_t count = SlotModels(slot, models);
        for (LiveEffect& live : d.live)
        {
            if (!live.effect)
                continue;
            const bool expired = live.end != kOpenEnded && frame >= static_cast<float>(live.end);
            TickLive(e, live, !expired && HasModel(models, count, live.at.model));
        }
    }

    std::size_t ContainerModels(void* container, void** out)
    {
        std::size_t n = 0;
        const std::uint32_t count = ModelCountSEH(container);
        for (std::uint32_t i = 0; i < count && n < kMaxEffectModels; ++i)
        {
            void* binding = PoseCopiedBindingSEH(container, i);
            void* model = binding ? ModelOfBindingSEH(binding) : nullptr;
            if (model)
                out[n++] = model;
        }
        return n;
    }

    void DestroyNamed(const Engine* e, NamedEffect& n)
    {
        if (n.live.effect && e)
            DestroyEffectSEH(*e, n.live.effect);
        n = NamedEffect{};
    }

    void DestroyAllNamed()
    {
        const Engine* e = GetEngine();
        for (NamedEffect& n : g_Named)
            if (n.live.effect || n.container)
                DestroyNamed(e, n);
    }

    void StopNamed(const Engine& e, const char* name)
    {
        for (NamedEffect& n : g_Named)
            if (n.live.effect && std::strcmp(n.name, name) == 0)
                DestroyNamed(&e, n);
    }

    NamedEffect* StartNamed(const Engine& e, const EffectRequest& r, void* container)
    {
        StopNamed(e, r.name);
        NamedEffect* slot = nullptr;
        for (NamedEffect& n : g_Named)
        {
            if (!n.live.effect)
            {
                slot = &n;
                break;
            }
        }
        if (!slot)
        {
            LogWarn("PlayHeadEffect '%s': too many head effects playing", r.name);
            return nullptr;
        }

        void* file = outfit::ddheadlayers::FindLocalHeadFile(r.vfx);
        if (!file || !VfxLoadedSEH(file))
        {
            LogWarn("PlayHeadEffect '%s': head '%s' .vfx not loaded, is it in the head's pack?", r.name, r.headName);
            return nullptr;
        }

        void* models[kMaxEffectModels];
        const std::size_t count = ContainerModels(container, models);
        Attach at{};
        if (count == 0 || !ResolveAttach(e, outfit::ddheadlayers::FindLocalHeadBlock(), models, count,
                r.point, at, "PlayHeadEffect", r.name, r.headName))
        {
            LogWarn("PlayHeadEffect '%s': head '%s' has no attach point", r.name, r.headName);
            return nullptr;
        }

        void* effect = SpawnEffect(e, file, at);
        if (!effect)
        {
            LogWarn("PlayHeadEffect '%s': head '%s' effect could not be created", r.name, r.headName);
            return nullptr;
        }
        slot->container   = container;
        slot->fv2         = r.fv2;
        slot->live.effect = effect;
        slot->live.at     = at;
        strncpy_s(slot->name, r.name, _TRUNCATE);
        strncpy_s(slot->headName, r.headName, _TRUNCATE);
        return slot;
    }

    bool ControlEndedSEH(void* control)
    {
        __try
        {
            const auto* outer = static_cast<const std::uint8_t*>(control);
            const auto* inner = outer + *reinterpret_cast<const std::uint32_t*>(outer + kControl_Inner);
            return (*reinterpret_cast<const std::uint32_t*>(inner + kInner_Flags) & kInnerPlayEnded) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void StopAutomatic(const Engine& e)
    {
        for (NamedEffect& n : g_Named)
            if (n.automatic && n.live.effect)
                DestroyNamed(&e, n);
    }

    void StartBoundEffects(const Driver& d, const Engine& e, void* container)
    {
        std::vector<outfit::headmotion::Effect> bound;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            const auto it = g_Heads.find(d.request.headName);
            if (it == g_Heads.end() || d.request.playerType >= outfit::kPlayerTypeMax)
                return;
            for (const outfit::headmotion::Effect& fx : it->second[d.request.playerType].effects)
                if (!fx.clip.empty() && fx.clip == d.request.clipName)
                    bound.push_back(fx);
        }
        bool started = false;
        for (const outfit::headmotion::Effect& fx : bound)
        {
            EffectRequest r;
            r.play  = true;
            r.fv2   = d.request.fv2;
            r.vfx   = fx.vfx;
            r.point = fx.point;
            strncpy_s(r.name, fx.name.c_str(), _TRUNCATE);
            strncpy_s(r.headName, d.request.headName, _TRUNCATE);
            if (NamedEffect* n = StartNamed(e, r, container))
            {
                n->automatic = true;
                started = true;
            }
        }
        if (started)
        {
            std::lock_guard<std::mutex> lock(g_EffectRequestMutex);
            g_EffectsBusy.store(true, std::memory_order_release);
        }
    }

    struct RestState
    {
        bool                                    resolved  = false;
        std::uint64_t                           fv2       = 0;
        void*                                   container = nullptr;
        char                                    headName[64] = {};
        std::vector<outfit::headmotion::Effect> effects;
    };

    RestState g_Rest;

    bool ResolveRest(RestState& rest)
    {
        const std::uint16_t worn = outfit::ReadLiveWornHeadCategory();
        const auto slot = static_cast<std::uint8_t>(worn & 0xFF);
        const outfit::CustomHeadEntry* head =
            (worn != 0xFFFF && slot >= outfit::kCustomHeadSlotBase)
                ? outfit::TryGetCustomHeadBySlot(slot) : nullptr;
        const std::uint8_t pt = outfit::ReadLivePlayerType();
        if (!head || pt >= outfit::kPlayerTypeMax || !head->faceFv2Code[pt])
            return false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            const auto it = g_Heads.find(head->name);
            if (it == g_Heads.end())
                return false;
            const BranchMotion& b = it->second[pt];
            if (b.rest.empty())
                return false;
            for (const outfit::headmotion::Effect& fx : b.effects)
                if (fx.clip == b.rest)
                    rest.effects.push_back(fx);
        }
        if (rest.effects.empty())
            return false;
        rest.fv2 = head->faceFv2Code[pt];
        strncpy_s(rest.headName, head->name, _TRUNCATE);
        rest.resolved = true;
        return true;
    }

    void UpdateRest(const Engine& e, void* work)
    {
        if (g_RestPending.exchange(false, std::memory_order_acq_rel))
        {
            g_Rest = RestState{};
            g_RestWanted.store(true, std::memory_order_release);
        }
        if (!g_RestWanted.load(std::memory_order_acquire))
            return;
        if (!g_Rest.resolved && !ResolveRest(g_Rest))
        {
            g_RestWanted.store(false, std::memory_order_release);
            return;
        }

        void* container = FindContainerSEH(work, g_Rest.fv2);
        if (!container)
            return;
        if (g_Rest.container)
            for (const NamedEffect& n : g_Named)
                if (n.automatic && n.live.effect)
                    return;
        void* models[kMaxEffectModels];
        if (ContainerModels(container, models) == 0)
            return;

        StopAutomatic(e);
        bool started = false;
        for (const outfit::headmotion::Effect& fx : g_Rest.effects)
        {
            EffectRequest r;
            r.play  = true;
            r.fv2   = g_Rest.fv2;
            r.vfx   = fx.vfx;
            r.point = fx.point;
            strncpy_s(r.name, fx.name.c_str(), _TRUNCATE);
            strncpy_s(r.headName, g_Rest.headName, _TRUNCATE);
            if (NamedEffect* n = StartNamed(e, r, container))
            {
                n->automatic = true;
                started = true;
            }
        }
        g_Rest.container = container;
        if (started)
        {
            std::lock_guard<std::mutex> lock(g_EffectRequestMutex);
            g_EffectsBusy.store(true, std::memory_order_release);
        }
    }

    void UpdateNamed(const Engine& e, void* work)
    {
        if (g_EffectsReset.exchange(false, std::memory_order_acq_rel))
            DestroyAllNamed();

        std::vector<EffectRequest> pending;
        {
            std::lock_guard<std::mutex> lock(g_EffectRequestMutex);
            pending.swap(g_EffectRequests);
        }
        std::vector<EffectRequest> retry;
        for (EffectRequest& r : pending)
        {
            if (!r.play)
            {
                StopNamed(e, r.name);
                continue;
            }
            if (void* container = FindContainerSEH(work, r.fv2))
            {
                StartNamed(e, r, container);
                continue;
            }
            if (++r.attempts < kMaxRequestAttempts)
                retry.push_back(r);
            else
                LogWarn("PlayHeadEffect '%s': head '%s' is not loaded", r.name, r.headName);
        }

        bool live = false;
        for (NamedEffect& n : g_Named)
        {
            if (!n.live.effect)
                continue;
            if (FindContainerSEH(work, n.fv2) != n.container)
            {
                if (++n.idle > kMaxIdleCalls)
                {
                    DestroyNamed(&e, n);
                    continue;
                }
                live = true;
                continue;
            }
            n.idle = 0;
            void* models[kMaxEffectModels];
            const std::size_t count = ContainerModels(n.container, models);
            TickLive(e, n.live, HasModel(models, count, n.live.at.model));
            if (n.live.effect)
                live = true;
            else
                n = NamedEffect{};
        }

        std::lock_guard<std::mutex> lock(g_EffectRequestMutex);
        if (!retry.empty())
            g_EffectRequests.insert(g_EffectRequests.begin(), retry.begin(), retry.end());
        g_EffectsBusy.store(live || !g_EffectRequests.empty(), std::memory_order_release);
    }

    struct SoundRequest
    {
        std::uint32_t eventId  = 0;
        std::uint64_t fv2      = 0;
        std::uint64_t point    = 0;
        int           attempts = 0;
        char          name[64]     = {};
        char          headName[64] = {};
    };

    struct HeadSound
    {
        bool   registered = false;
        bool   active     = false;
        int    idle       = 0;
        void*  container  = nullptr;
        std::uint64_t fv2 = 0;
        Attach at{};
    };

    constexpr std::uint64_t kHeadSoundObject = 0x9F5A0001ull;

    std::mutex                g_SoundRequestMutex;
    std::vector<SoundRequest> g_SoundRequests;
    std::atomic<bool>         g_SoundsBusy{ false };
    HeadSound                 g_HeadSound;

    std::uint32_t WwiseId(const char* name)
    {
        std::uint32_t h = 2166136261u;
        for (; *name; ++name)
        {
            h *= 16777619u;
            h ^= static_cast<std::uint32_t>(std::tolower(static_cast<unsigned char>(*name)));
        }
        return h;
    }

    bool SoundReady(const Engine& e)
    {
        return e.akRegister && e.akSetPosition && e.akPostEvent;
    }

    bool PlaceSoundSEH(const Engine& e, const float* position)
    {
        __try
        {
            if (!g_HeadSound.registered)
            {
                e.akRegister(kHeadSoundObject, nullptr, 1);
                g_HeadSound.registered = true;
            }
            return e.akSetPosition(kHeadSoundObject, position) == 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool PlaceSound(const Engine& e)
    {
        alignas(16) float m[16];
        if (!AttachWorldSEH(e, g_HeadSound.at, m))
            return false;
        float fx = m[8], fy = m[9], fz = m[10];
        const float len = std::sqrt(fx * fx + fy * fy + fz * fz);
        if (len > 1e-6f)
        {
            fx /= len;
            fy /= len;
            fz /= len;
        }
        else
        {
            fx = 0.0f;
            fy = 0.0f;
            fz = 1.0f;
        }
        const float position[6] = { m[12], m[13], -m[14], fx, fy, -fz };
        return PlaceSoundSEH(e, position);
    }

    std::uint32_t PostSoundSEH(const Engine& e, std::uint32_t eventId)
    {
        __try
        {
            e.akRegister(kHeadSoundObject, nullptr, 1);
            return e.akPostEvent(eventId, kHeadSoundObject, 0, nullptr, nullptr, 0, nullptr, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void UpdateSounds(const Engine& e, void* work)
    {
        std::vector<SoundRequest> pending;
        {
            std::lock_guard<std::mutex> lock(g_SoundRequestMutex);
            pending.swap(g_SoundRequests);
        }
        std::vector<SoundRequest> retry;
        for (SoundRequest& r : pending)
        {
            void* container = FindContainerSEH(work, r.fv2);
            if (!container)
            {
                if (++r.attempts < kMaxRequestAttempts)
                    retry.push_back(r);
                else
                    LogWarn("PlayHeadSound '%s': head '%s' is not loaded", r.name, r.headName);
                continue;
            }
            void* models[kMaxEffectModels];
            const std::size_t count = ContainerModels(container, models);
            Attach at{};
            if (count == 0 || !ResolveAttach(e, outfit::ddheadlayers::FindLocalHeadBlock(), models, count,
                    r.point, at, "PlayHeadSound", r.name, r.headName))
            {
                LogWarn("PlayHeadSound '%s': head '%s' has no attach point", r.name, r.headName);
                continue;
            }
            g_HeadSound.container = container;
            g_HeadSound.fv2       = r.fv2;
            g_HeadSound.at        = at;
            g_HeadSound.active    = true;
            g_HeadSound.idle      = 0;
            if (!PlaceSound(e) || PostSoundSEH(e, r.eventId) == 0)
                LogWarn("PlayHeadSound '%s': event did not play, is its sound bank loaded?", r.name);
        }

        if (g_HeadSound.active)
        {
            if (FindContainerSEH(work, g_HeadSound.fv2) == g_HeadSound.container)
            {
                void* models[kMaxEffectModels];
                const std::size_t count = ContainerModels(g_HeadSound.container, models);
                if (HasModel(models, count, g_HeadSound.at.model) && PlaceSound(e))
                    g_HeadSound.idle = 0;
                else
                    g_HeadSound.active = false;
            }
            else if (++g_HeadSound.idle > kMaxIdleCalls)
            {
                g_HeadSound.active = false;
            }
        }

        std::lock_guard<std::mutex> lock(g_SoundRequestMutex);
        if (!retry.empty())
            g_SoundRequests.insert(g_SoundRequests.begin(), retry.begin(), retry.end());
        g_SoundsBusy.store(g_HeadSound.active || !g_SoundRequests.empty(), std::memory_order_release);
    }

    void ReleaseHeadSoundSEH(const Engine& e)
    {
        __try
        {
            if (g_HeadSound.registered && e.akUnregister)
                e.akUnregister(kHeadSoundObject);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        g_HeadSound = HeadSound{};
    }

    void Build(Driver& d, ContainerDriver& slot, const Engine& e)
    {
        ReleaseSlot(slot);
        slot.built = true;
        void* container = slot.container;

        void* mtar = outfit::ddheadlayers::FindLocalHeadFile(d.request.mtar);
        if (!mtar || !IsOwnBonesArchiveSEH(mtar))
        {
            WarnOnce(d, "motion mtar not loaded, is it in the head's fpk?");
            return;
        }

        const std::uint32_t count = ModelCountSEH(container);
        for (std::uint32_t i = 0; i < count && slot.modelCount < slot.models.size(); ++i)
        {
            void* binding = PoseCopiedBindingSEH(container, i);
            void* model = binding ? ModelOfBindingSEH(binding) : nullptr;
            if (!model)
                continue;
            if (void* control = CreateControl(e, mtar, model, d.request.clip))
                slot.models[slot.modelCount++] = ModelDriver{ model, control, i };
        }
        if (slot.modelCount == 0)
        {
            WarnOnce(d, "model has none of the clip's bones");
            return;
        }
        if (!d.effectOwner)
            d.effectOwner = container;
        if (d.effectOwner == container)
        {
            DestroyEffects(d);
            d.elapsed    = 0.0f;
            d.clipEnded  = false;
            d.eventCount = ReadClipEffectsSEH(slot.models[0].control, d.events.data(), d.events.size());
        }
    }

    bool IsHexId(const char* s, std::size_t& digits, std::size_t& start)
    {
        start = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 2 : 0;
        digits = std::strlen(s + start);
        if (digits != 16 && (digits < 8 || digits > 13))
            return false;
        for (std::size_t i = start; s[i]; ++i)
            if (!std::isxdigit(static_cast<unsigned char>(s[i])))
                return false;
        return true;
    }
}

namespace outfit::headmotion
{
    std::uint64_t ParseClipId(const char* text)
    {
        if (!text || !text[0])
            return 0;
        std::size_t digits = 0;
        std::size_t start  = 0;
        if (IsHexId(text, digits, start))
        {
            const std::uint64_t value = std::strtoull(text + start, nullptr, 16);
            return digits == 16 ? value : (kGaniExtCode | value);
        }
        return FoxHashes::PathCode64Ext(text);
    }

    void SetBranchMotion(const char* headKey, std::uint8_t playerType,
                         std::uint64_t mtarCode, std::vector<Clip> clips)
    {
        if (!headKey || !headKey[0] || playerType >= outfit::kPlayerTypeMax)
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        BranchMotion& b = g_Heads[headKey][playerType];
        b.mtar  = mtarCode;
        b.clips = std::move(clips);
    }

    std::uint64_t ParseEffectPoint(const char* name)
    {
        return (name && name[0]) ? (FoxHashes::StrCode64(name) & kStringIdMask) : 0;
    }

    void SetBranchEffects(const char* headKey, std::uint8_t playerType, std::vector<Effect> effects)
    {
        if (!headKey || !headKey[0] || playerType >= outfit::kPlayerTypeMax)
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Heads[headKey][playerType].effects = std::move(effects);
    }

    void SetBranchRest(const char* headKey, std::uint8_t playerType, const char* restClip)
    {
        if (!headKey || !headKey[0] || playerType >= outfit::kPlayerTypeMax)
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Heads[headKey][playerType].rest = restClip ? restClip : "";
    }

    bool Play(const char* clipName)
    {
        if (!clipName || !clipName[0])
        {
            LogWarn("PlayHeadMotion: missing clip name");
            return false;
        }
        if (MissionCodeGuard::ShouldBypassHooks())
            return false;

        if (!GetEngine())
        {
            static std::atomic<bool> s_logged{ false };
            if (!s_logged.exchange(true))
                LogError("PlayHeadMotion: not supported on game %s yet",
                    GetGameBuildName(gGameBuild));
            return false;
        }

        const std::uint16_t worn = outfit::ReadLiveWornHeadCategory();
        const auto slot = static_cast<std::uint8_t>(worn & 0xFF);
        const outfit::CustomHeadEntry* head =
            (worn != 0xFFFF && slot >= outfit::kCustomHeadSlotBase)
                ? outfit::TryGetCustomHeadBySlot(slot) : nullptr;
        if (!head)
        {
            LogWarn("PlayHeadMotion '%s': no custom head worn", clipName);
            return false;
        }

        const std::uint8_t pt = outfit::ReadLivePlayerType();
        std::uint64_t mtar = 0;
        std::uint64_t clip = 0;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            const auto it = g_Heads.find(head->name);
            if (it != g_Heads.end() && pt < outfit::kPlayerTypeMax)
            {
                const BranchMotion& b = it->second[pt];
                mtar = b.mtar;
                for (const Clip& c : b.clips)
                    if (c.name == clipName)
                        clip = c.code;
            }
        }
        const std::uint64_t fv2 = pt < outfit::kPlayerTypeMax ? head->faceFv2Code[pt] : 0;
        if (!mtar || !clip || !fv2)
        {
            LogWarn("PlayHeadMotion '%s': head '%s' has no such clip for this character",
                clipName, head->name);
            return false;
        }

        void* file = outfit::ddheadlayers::FindLocalHeadFile(mtar);
        if (!file)
        {
            LogWarn("PlayHeadMotion '%s': head '%s' motion mtar not loaded, is it in the head's fpk?",
                clipName, head->name);
            return false;
        }
        if (!IsOwnBonesArchiveSEH(file))
        {
            LogWarn("PlayHeadMotion '%s': head '%s' motion mtar animates the player, not the head",
                clipName, head->name);
            return false;
        }

        std::lock_guard<std::mutex> lock(g_RequestMutex);
        g_Request = Request{};
        g_Request.fv2  = fv2;
        g_Request.mtar = mtar;
        g_Request.clip = clip;
        strncpy_s(g_Request.clipName, clipName, _TRUNCATE);
        strncpy_s(g_Request.headName, head->name, _TRUNCATE);
        g_Request.playerType = pt;
        if (++g_LastGeneration == 0)
            ++g_LastGeneration;
        g_Generation.store(g_LastGeneration, std::memory_order_release);
        return true;
    }

    bool PlayEffect(const char* name)
    {
        if (!name || !name[0])
        {
            LogWarn("PlayHeadEffect: missing effect name");
            return false;
        }
        if (MissionCodeGuard::ShouldBypassHooks())
            return false;

        if (!GetEngine())
        {
            static std::atomic<bool> s_logged{ false };
            if (!s_logged.exchange(true))
                LogError("PlayHeadEffect: not supported on game %s yet",
                    GetGameBuildName(gGameBuild));
            return false;
        }

        const std::uint16_t worn = outfit::ReadLiveWornHeadCategory();
        const auto slot = static_cast<std::uint8_t>(worn & 0xFF);
        const outfit::CustomHeadEntry* head =
            (worn != 0xFFFF && slot >= outfit::kCustomHeadSlotBase)
                ? outfit::TryGetCustomHeadBySlot(slot) : nullptr;
        if (!head)
        {
            LogWarn("PlayHeadEffect '%s': no custom head worn", name);
            return false;
        }

        const std::uint8_t pt = outfit::ReadLivePlayerType();
        EffectRequest r;
        r.play = true;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            const auto it = g_Heads.find(head->name);
            if (it != g_Heads.end() && pt < outfit::kPlayerTypeMax)
            {
                for (const Effect& fx : it->second[pt].effects)
                {
                    if (fx.name != name)
                        continue;
                    r.vfx   = fx.vfx;
                    r.point = fx.point;
                }
            }
        }
        r.fv2 = pt < outfit::kPlayerTypeMax ? head->faceFv2Code[pt] : 0;
        if (!r.vfx || !r.fv2)
        {
            LogWarn("PlayHeadEffect '%s': head '%s' has no such effect for this character", name, head->name);
            return false;
        }
        strncpy_s(r.name, name, _TRUNCATE);
        strncpy_s(r.headName, head->name, _TRUNCATE);

        std::lock_guard<std::mutex> lock(g_EffectRequestMutex);
        if (g_EffectRequests.size() >= kMaxPendingRequests)
        {
            LogWarn("PlayHeadEffect '%s': too many requests pending", name);
            return false;
        }
        g_EffectRequests.push_back(r);
        g_EffectsBusy.store(true, std::memory_order_release);
        return true;
    }

    bool PlaySound(const char* eventName, const char* pointName)
    {
        if (!eventName || !eventName[0])
        {
            LogWarn("PlayHeadSound: missing event name");
            return false;
        }
        const char* label = eventName;
        if (MissionCodeGuard::ShouldBypassHooks())
            return false;

        const Engine* e = GetEngine();
        if (!e || !SoundReady(*e))
        {
            static std::atomic<bool> s_logged{ false };
            if (!s_logged.exchange(true))
                LogError("PlayHeadSound: not supported on game %s yet", GetGameBuildName(gGameBuild));
            return false;
        }

        const std::uint16_t worn = outfit::ReadLiveWornHeadCategory();
        const auto slot = static_cast<std::uint8_t>(worn & 0xFF);
        const outfit::CustomHeadEntry* head =
            (worn != 0xFFFF && slot >= outfit::kCustomHeadSlotBase)
                ? outfit::TryGetCustomHeadBySlot(slot) : nullptr;
        if (!head)
        {
            LogWarn("PlayHeadSound '%s': no custom head worn", label);
            return false;
        }
        const std::uint8_t pt = outfit::ReadLivePlayerType();
        SoundRequest r;
        r.eventId = WwiseId(eventName);
        r.fv2     = pt < outfit::kPlayerTypeMax ? head->faceFv2Code[pt] : 0;
        r.point   = ParseEffectPoint(pointName);
        if (!r.fv2)
        {
            LogWarn("PlayHeadSound '%s': head '%s' has no model for this character", label, head->name);
            return false;
        }
        strncpy_s(r.name, label, _TRUNCATE);
        strncpy_s(r.headName, head->name, _TRUNCATE);

        std::lock_guard<std::mutex> lock(g_SoundRequestMutex);
        if (g_SoundRequests.size() >= kMaxPendingRequests)
        {
            LogWarn("PlayHeadSound '%s': too many requests pending", label);
            return false;
        }
        g_SoundRequests.push_back(r);
        g_SoundsBusy.store(true, std::memory_order_release);
        return true;
    }

    bool StopEffect(const char* name)
    {
        if (!name || !name[0])
        {
            LogWarn("StopHeadEffect: missing effect name");
            return false;
        }
        EffectRequest r;
        strncpy_s(r.name, name, _TRUNCATE);
        std::lock_guard<std::mutex> lock(g_EffectRequestMutex);
        if (g_EffectRequests.size() >= kMaxPendingRequests)
            return false;
        g_EffectRequests.push_back(r);
        g_EffectsBusy.store(true, std::memory_order_release);
        return true;
    }

    std::uintptr_t WorkActMotionAddress()
    {
        const EngineAddresses* a = AddressesForBuild();
        return a ? a->workActMotion : 0;
    }

    std::uintptr_t EffectUpdateAddress()
    {
        const EngineAddresses* a = AddressesForBuild();
        return a ? a->effectUpdate : 0;
    }

    std::uintptr_t EffectContextAcquireAddress()
    {
        const EngineAddresses* a = AddressesForBuild();
        return a ? a->effectContextAcquire : 0;
    }

    void SetEffectUpdateOriginals(EffectUpdate_t update, EffectContextAcquire_t acquire)
    {
        g_EffectContextAcquire.store(acquire, std::memory_order_release);
        g_EffectUpdate.store(update, std::memory_order_release);
        if (!update)
            ForgetAllDeferred();
    }

    void NoteEffectContextPool(void* pool)
    {
        if (pool)
            g_EffectContextPool.store(pool, std::memory_order_release);
    }

    bool DeferEffectUpdate(void* instance, float delta)
    {
        if (!instance || g_DeferredCount.load(std::memory_order_acquire) <= 0
            || !g_EffectContextPool.load(std::memory_order_acquire))
            return false;
        void* const effect = *reinterpret_cast<void* const*>(static_cast<const std::uint8_t*>(instance) + kInstance_Effect);
        if (!effect)
            return false;
        DeferredUpdate* d = FindDeferred(effect);
        if (!d)
            return false;
        int state = kDeferPending;
        if (d->state.compare_exchange_strong(state, kDeferIdle, std::memory_order_acq_rel))
            return false;
        state = kDeferIdle;
        if (!d->state.compare_exchange_strong(state, kDeferStoring, std::memory_order_acq_rel))
            return false;
        d->instance = instance;
        d->delta    = delta;
        d->state.store(kDeferPending, std::memory_order_release);
        return true;
    }

    static void DriveClip(void* work, const float* timeBase, std::uint32_t gen)
    {
        Driver& d = g_Driver;
        if (gen == 0)
        {
            if (g_DriverHolds.load(std::memory_order_acquire))
            {
                ReleaseModels(d);
                d.generation = 0;
                g_DriverHolds.store(false, std::memory_order_release);
            }
            return;
        }
        if (!work || !timeBase || MissionCodeGuard::ShouldBypassHooks())
            return;
        const Engine* e = GetEngine();
        if (!e)
            return;

        if (d.generation != gen)
        {
            StopAutomatic(*e);
            ReleaseModels(d);
            {
                std::lock_guard<std::mutex> requestLock(g_RequestMutex);
                d.request = g_Request;
            }
            d.generation = gen;
            d.warned     = false;
            g_DriverHolds.store(true, std::memory_order_release);
        }

        void* container = FindContainerSEH(work, d.request.fv2);
        if (!container)
            return;
        ContainerDriver& slot = SlotFor(d, container);
        if (!slot.built)
            Build(d, slot, *e);

        for (std::size_t i = 0; i < slot.modelCount; ++i)
        {
            const ModelDriver& m = slot.models[i];
            void* binding = PoseCopiedBindingSEH(container, m.index);
            if (!binding || ModelOfBindingSEH(binding) != m.model)
            {
                ReleaseSlot(slot);
                return;
            }
            if (!DriveModelSEH(*e, binding, m.model, m.control, timeBase, g_Pose))
            {
                if (d.effectOwner == slot.container)
                    DestroyEffects(d);
                ReleaseSlot(slot);
                slot.built = true;
                WarnOnce(d, "clip playback faulted, stopped");
                return;
            }
        }
        UpdateEffects(d, slot, *e, *timeBase);
        if (d.effectOwner == slot.container && !d.clipEnded && slot.modelCount > 0
            && ControlEndedSEH(slot.models[0].control))
        {
            d.clipEnded = true;
            StartBoundEffects(d, *e, slot.container);
            V_FrameWork::EmitMessage("Player", "FinishHeadMotion", 0, d.request.clipName);
        }
    }

    void OnWorkActMotion(void* work, const float* timeBase)
    {
        const std::uint32_t gen = g_Generation.load(std::memory_order_acquire);
        const bool effects = g_EffectsBusy.load(std::memory_order_acquire);
        const bool rest    = g_RestPending.load(std::memory_order_acquire)
                          || g_RestWanted.load(std::memory_order_acquire);
        const bool sounds  = g_SoundsBusy.load(std::memory_order_acquire);
        if (gen == 0 && !g_DriverHolds.load(std::memory_order_acquire) && !effects && !rest && !sounds)
            return;

        std::unique_lock<std::mutex> lock(g_DriverMutex, std::try_to_lock);
        if (!lock.owns_lock())
            return;

        DriveClip(work, timeBase, gen);

        if (!work || MissionCodeGuard::ShouldBypassHooks())
            return;
        const bool busy = g_EffectsBusy.load(std::memory_order_acquire);
        const bool audio = g_SoundsBusy.load(std::memory_order_acquire);
        if (!busy && !rest && !audio)
            return;
        if (const Engine* fx = GetEngine())
        {
            if (busy)
                UpdateNamed(*fx, work);
            if (audio)
                UpdateSounds(*fx, work);
            if (gen == 0)
                UpdateRest(*fx, work);
            else if (rest)
            {
                g_RestPending.store(false, std::memory_order_release);
                g_RestWanted.store(false, std::memory_order_release);
            }
        }
    }

    void OnPlayerPartsReloaded()
    {
        g_Generation.store(0, std::memory_order_release);
        std::lock_guard<std::mutex> lock(g_EffectRequestMutex);
        g_EffectRequests.clear();
        g_EffectsReset.store(true, std::memory_order_release);
        g_EffectsBusy.store(true, std::memory_order_release);
        g_RestPending.store(true, std::memory_order_release);
    }

    void Shutdown()
    {
        g_Generation.store(0, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(g_DriverMutex);
            ReleaseModels(g_Driver);
            DestroyAllNamed();
            ForgetAllDeferred();
            if (const Engine* e = GetEngine())
                ReleaseHeadSoundSEH(*e);
            g_Rest = RestState{};
            g_Driver.generation = 0;
            g_DriverHolds.store(false, std::memory_order_release);
        }
        std::lock_guard<std::mutex> lock(g_EffectRequestMutex);
        g_EffectRequests.clear();
        g_EffectsReset.store(false, std::memory_order_release);
        g_EffectsBusy.store(false, std::memory_order_release);
        g_RestPending.store(false, std::memory_order_release);
        g_RestWanted.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> soundLock(g_SoundRequestMutex);
        g_SoundRequests.clear();
        g_SoundsBusy.store(false, std::memory_order_release);
    }
}
