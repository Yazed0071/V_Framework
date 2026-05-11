#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "SoldierAkObjIdMap.h"
#include "SoldierObjectRtpc.h"
#include "SoldierVoiceTypeQuery.h"
#include "../sound/VoicePitchOverride.h"


namespace
{


    using ConvertParameterID_t  = std::uint32_t(__cdecl*)(const char* name);
    using DaemonGetObject_t     = void* (__fastcall*)(void* self, std::uint32_t childId);


    constexpr std::uintptr_t kImpl_SourceMgrOffset    = 0x68;
    constexpr std::uintptr_t kImpl_SlotVectorOffset   = 0x98;
    constexpr std::uintptr_t kSlotEntryStride         = 0x18;
    constexpr std::uintptr_t kSlotEntry_AkObjIdOffset = 0x00;
    constexpr std::size_t    kVtbl_GetSourceForSlot   = 0xF8 / sizeof(void*);
    constexpr std::size_t    kVtbl_GetObjectForSlot   = 0x90 / sizeof(void*);
    constexpr std::size_t    kVtbl_SetRTPC            = 0x28 / sizeof(void*);
    constexpr std::intptr_t  kControl_SubObjectBack   = -0x10;
    constexpr std::uintptr_t kControl_ChildCountOff   = 0x18;
    constexpr std::uintptr_t kControl_ChildArrayOff   = 0x20;
    constexpr std::uint32_t  kCurveLinear             = 4;


    static ConvertParameterID_t g_ConvertParameterID = nullptr;
    static DaemonGetObject_t    g_DaemonGetObject    = nullptr;


    static ConvertParameterID_t ResolveConvertParameterID()
    {
        if (g_ConvertParameterID) return g_ConvertParameterID;
        const auto a = gAddr.Fox_Sd_ConvertParameterID;
        if (!a) return nullptr;
        g_ConvertParameterID = reinterpret_cast<ConvertParameterID_t>(ResolveGameAddress(a));
        return g_ConvertParameterID;
    }


    static DaemonGetObject_t ResolveDaemonGetObject()
    {
        if (g_DaemonGetObject) return g_DaemonGetObject;
        const auto a = gAddr.Fox_Sd_Daemon_GetObject;
        if (!a) return nullptr;
        g_DaemonGetObject = reinterpret_cast<DaemonGetObject_t>(ResolveGameAddress(a));
        return g_DaemonGetObject;
    }


    static void* ResolveDaemonInstance()
    {
        const auto a = gAddr.Fox_Sd_Daemon_Singleton;
        if (!a) return nullptr;
        void* slot = ResolveGameAddress(a);
        if (!slot) return nullptr;
        __try
        {
            return *reinterpret_cast<void**>(slot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }


    static void* SafeReadPtr(void* base, std::uintptr_t off)
    {
        if (!base) return nullptr;
        __try
        {
            return *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(base) + off);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }


    static std::uint32_t SafeReadU32(void* base, std::uintptr_t off)
    {
        if (!base) return 0;
        __try
        {
            return *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(base) + off);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }


    static void* SafeReadVtableEntry(void* obj, std::size_t index)
    {
        if (!obj) return nullptr;
        __try
        {
            void** vtbl = *reinterpret_cast<void***>(obj);
            if (!vtbl) return nullptr;
            return vtbl[index];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }


    using GetByIndex_t = void* (__fastcall*)(void* self, std::uint32_t index);


    static void* SafeCallGetByIndex(void* self, GetByIndex_t fn, std::uint32_t index)
    {
        if (!self || !fn) return nullptr;
        __try
        {
            return fn(self, index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }


    static void* SafeCallDaemonGetObject(void* daemon, DaemonGetObject_t fn,
                                         std::uint32_t childId)
    {
        if (!daemon || !fn || !childId) return nullptr;
        __try
        {
            return fn(daemon, childId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }


    using SetRtpcOnObject_t = void* (__fastcall*)(
        void*           self,
        void*           errCodeOut,
        std::uint32_t   rtpcId,
        float           value,
        std::uint32_t   changeTime,
        std::uint32_t   curve);


    static bool SafeCallSetRtpc(void*             obj,
                                SetRtpcOnObject_t fn,
                                std::uint32_t     rtpcId,
                                float             value,
                                std::uint32_t     timeMs,
                                std::uint32_t     curve)
    {
        if (!obj || !fn) return false;
        std::uint64_t errCode = 0;
        __try
        {
            fn(obj, &errCode, rtpcId, value, timeMs, curve);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }




    static void* WalkSoldierObject(std::uint32_t gameObjectId)
    {
        const std::uint32_t slot = GetSoldierSlotFromGameObjectId(gameObjectId);
        if (slot == 0xFFFFu) return nullptr;

        void* impl = GetSoldierSoundControllerImpl(gameObjectId);
        if (!impl) return nullptr;

        void* sourceMgr = SafeReadPtr(impl, kImpl_SourceMgrOffset);
        if (!sourceMgr) return nullptr;

        void* getSourceFn = SafeReadVtableEntry(sourceMgr, kVtbl_GetSourceForSlot);
        if (!getSourceFn) return nullptr;
        void* source = SafeCallGetByIndex(sourceMgr,
            reinterpret_cast<GetByIndex_t>(getSourceFn), slot);
        if (!source) return nullptr;

        void* getObjectFn = SafeReadVtableEntry(source, kVtbl_GetObjectForSlot);
        if (!getObjectFn) return nullptr;
        return SafeCallGetByIndex(source,
            reinterpret_cast<GetByIndex_t>(getObjectFn), slot);
    }




    static std::unordered_map<std::uint32_t, float> g_PendingPitchByGameObjectId;
    static std::mutex g_PendingMutex;


    static bool TryApplyImmediate(std::uint32_t gameObjectId, float cents,
                                  const char** outReason)
    {
        if (outReason) *outReason = nullptr;
        SoldierAkObjIdMap::SetDesiredPitchForGoId(gameObjectId, cents);
        return true;
    }


}


bool Set_SoldierObjectRtpc(std::uint32_t gameObjectId,
                           std::uint32_t rtpcId,
                           float         value,
                           long          timeMs)
{
    void* object = WalkSoldierObject(gameObjectId);
    if (!object) return false;

    void* setRtpcFn = SafeReadVtableEntry(object, kVtbl_SetRTPC);
    if (!setRtpcFn) return false;

    return SafeCallSetRtpc(object,
        reinterpret_cast<SetRtpcOnObject_t>(setRtpcFn),
        rtpcId, value, static_cast<std::uint32_t>(timeMs), kCurveLinear);
}


bool Set_SoldierObjectRtpcByName(std::uint32_t gameObjectId,
                                 const char*   rtpcName,
                                 float         value,
                                 long          timeMs)
{
    if (!rtpcName || !*rtpcName) return false;

    auto convert = ResolveConvertParameterID();
    if (!convert) return false;

    std::uint32_t rtpcId = 0;
    __try
    {
        rtpcId = convert(rtpcName);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    return Set_SoldierObjectRtpc(gameObjectId, rtpcId, value, timeMs);
}


std::uint32_t Get_SoldierAkObjId(std::uint32_t gameObjectId)
{
    const auto ids = SoldierAkObjIdMap::GetAkObjIdsForGoId(gameObjectId);
    return ids.empty() ? 0u : ids.front();
}


bool Set_SoldierVoicePitch(std::uint32_t gameObjectId, float cents)
{
    if (cents == 0.0f)
    {
        SoldierAkObjIdMap::ClearDesiredPitchForGoId(gameObjectId);
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        g_PendingPitchByGameObjectId.erase(gameObjectId);
        return true;
    }

    const char* reason = nullptr;
    const bool applied = TryApplyImmediate(gameObjectId, cents, &reason);

    if (!applied)
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        g_PendingPitchByGameObjectId[gameObjectId] = cents;
    }
    return applied;
}


void TryApplyAllPendingSoldierPitches()
{
    std::vector<std::pair<std::uint32_t, float>> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        if (g_PendingPitchByGameObjectId.empty()) return;
        snapshot.reserve(g_PendingPitchByGameObjectId.size());
        for (const auto& kv : g_PendingPitchByGameObjectId)
            snapshot.emplace_back(kv.first, kv.second);
    }

    for (const auto& kv : snapshot)
    {
        const char* reason = nullptr;
        if (TryApplyImmediate(kv.first, kv.second, &reason))
        {
            std::lock_guard<std::mutex> lock(g_PendingMutex);
            g_PendingPitchByGameObjectId.erase(kv.first);
        }
    }
}
