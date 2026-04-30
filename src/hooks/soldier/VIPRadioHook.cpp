#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "VIPRadioHook.h"
#include "AddressSet.h"

namespace
{


    static constexpr std::uint8_t kRadioTypeBodyFound = 0x0E;


    static constexpr std::uint8_t kRadioTypeVipBodyFound = 0x0F;


    static constexpr std::uint32_t kOfficerBodyFoundSpeechLabel = 0xDD6EA61Bu;

    struct ImportantTargetInfo
    {
        std::uint32_t gameObjectId = 0;
        std::uint16_t soldierIndex = 0;
        bool isOfficer = false;
    };

    struct PendingBodyFoundOverride
    {
        bool active = false;
        ImportantTargetInfo target{};
    };


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


    using StateRadio_t = void(__fastcall*)(void* self, std::uint32_t slot, int proc);


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

    static RequestCorpse_t g_OrigRequestCorpse = nullptr;
    static StateRadio_t g_OrigStateRadio = nullptr;
    static CallWithRadioType_t g_OrigCallWithRadioType = nullptr;
    static CallImpl_t g_CallImpl = nullptr;

    static std::mutex g_StateMutex;


    static std::unordered_map<std::uint32_t, ImportantTargetInfo> g_ImportantByGameObjectId;
    static std::unordered_map<std::uint16_t, ImportantTargetInfo> g_ImportantBySoldierIndex;


    static std::deque<ImportantTargetInfo> g_DiscoveredImportantBodyQueue;


    static std::unordered_set<std::uint64_t> g_SeenDiscoveredImportantBodies;


    static std::deque<ImportantTargetInfo> g_RecentImportantCorpsesFromRequest;


    static PendingBodyFoundOverride g_PendingBodyFoundOverride;
}


static std::uint16_t GameObjectIdToSoldierIndex(std::uint32_t gameObjectId)
{
    const std::uint16_t low8 = static_cast<std::uint16_t>(gameObjectId & 0x00FFu);
    if (low8 != 0)
        return low8;

    return static_cast<std::uint16_t>(gameObjectId & 0xFFFFu);
}


static const char* YesNo(bool value)
{
    return value ? "YES" : "NO";
}


static std::uint16_t NormalizeSoldierIndex(std::uint32_t gameObjectId, std::uint16_t soldierIndex)
{
    if (soldierIndex != 0 && soldierIndex <= 0x00FFu)
        return soldierIndex;

    return GameObjectIdToSoldierIndex(gameObjectId);
}


static std::uint64_t MakeBodyKey(std::uint32_t gameObjectId, std::uint16_t soldierIndex)
{
    return (static_cast<std::uint64_t>(gameObjectId) << 32) | static_cast<std::uint64_t>(soldierIndex);
}


static bool FindImportantTarget(
    std::uint32_t gameObjectId,
    std::uint16_t soldierIndex,
    ImportantTargetInfo& outInfo)
{
    if (gameObjectId != 0)
    {
        const auto byGameObject = g_ImportantByGameObjectId.find(gameObjectId);
        if (byGameObject != g_ImportantByGameObjectId.end())
        {
            outInfo = byGameObject->second;
            return true;
        }
    }

    const std::uint16_t normalizedSoldierIndex = NormalizeSoldierIndex(gameObjectId, soldierIndex);
    if (normalizedSoldierIndex != 0)
    {
        const auto bySoldierIndex = g_ImportantBySoldierIndex.find(normalizedSoldierIndex);
        if (bySoldierIndex != g_ImportantBySoldierIndex.end())
        {
            outInfo = bySoldierIndex->second;
            return true;
        }
    }

    return false;
}


static bool FindImportantTarget(std::uint32_t gameObjectId, ImportantTargetInfo& outInfo)
{
    return FindImportantTarget(
        gameObjectId,
        static_cast<std::uint16_t>(gameObjectId & 0xFFFFu),
        outInfo);
}


static std::uint8_t ReadRadioType(void* self, std::uint32_t slot)
{
    const auto selfBytes = reinterpret_cast<std::uint8_t*>(self);
    const auto entry88Base = *reinterpret_cast<std::uint8_t**>(selfBytes + 0x88);
    if (!entry88Base)
        return 0;

    return *(entry88Base + 8 + static_cast<std::size_t>(slot) * 0x0C);
}


static void RemoveRecentImportantCorpse_NoLock(const ImportantTargetInfo& info)
{
    for (auto it = g_RecentImportantCorpsesFromRequest.begin();
        it != g_RecentImportantCorpsesFromRequest.end();
        ++it)
    {
        if (it->gameObjectId == info.gameObjectId &&
            it->soldierIndex == info.soldierIndex)
        {
            g_RecentImportantCorpsesFromRequest.erase(it);
            return;
        }
    }
}


static void ClearRuntimeRadioState_NoLock()
{
    g_DiscoveredImportantBodyQueue.clear();
    g_SeenDiscoveredImportantBodies.clear();
    g_RecentImportantCorpsesFromRequest.clear();
    g_PendingBodyFoundOverride = {};
}


static bool QueueDiscoveredImportantBody(std::uint32_t foundGameObjectId, std::uint16_t foundSoldierIndex)
{
    ImportantTargetInfo info{};
    if (!FindImportantTarget(foundGameObjectId, foundSoldierIndex, info))
    {
        Log(
            "[Radio] The found body wasn't the VIP's: gameObjectId=0x%08X soldierIndex=0x%04X\n",
            static_cast<unsigned int>(foundGameObjectId),
            static_cast<unsigned int>(foundSoldierIndex));
        return false;
    }

    const std::uint64_t bodyKey = MakeBodyKey(info.gameObjectId, info.soldierIndex);
    if (g_SeenDiscoveredImportantBodies.find(bodyKey) != g_SeenDiscoveredImportantBodies.end())
    {
        Log(
            "[Radio] Duplicate important body discovery ignored: gameObjectId=0x%08X soldierIndex=0x%04X officer=%s\n",
            static_cast<unsigned int>(info.gameObjectId),
            static_cast<unsigned int>(info.soldierIndex),
            YesNo(info.isOfficer));
        return false;
    }

    g_SeenDiscoveredImportantBodies.insert(bodyKey);
    g_DiscoveredImportantBodyQueue.push_back(info);
    RemoveRecentImportantCorpse_NoLock(info);

    Log(
        "[Radio] Queued discovered important body: gameObjectId=0x%08X soldierIndex=0x%04X officer=%s queueSize=%zu\n",
        static_cast<unsigned int>(info.gameObjectId),
        static_cast<unsigned int>(info.soldierIndex),
        YesNo(info.isOfficer),
        g_DiscoveredImportantBodyQueue.size());

    return true;
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
    if (MissionCodeGuard::ShouldBypassHooks())
    {
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
        return;
    }

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

    ImportantTargetInfo info{};
    {
        std::lock_guard<std::mutex> lock(g_StateMutex);
        if (FindImportantTarget(static_cast<std::uint32_t>(originalGameObjectId), info))
        {
            bool alreadyRecent = false;
            for (const auto& recent : g_RecentImportantCorpsesFromRequest)
            {
                if (recent.gameObjectId == info.gameObjectId &&
                    recent.soldierIndex == info.soldierIndex)
                {
                    alreadyRecent = true;
                    break;
                }
            }

            if (!alreadyRecent)
            {
                g_RecentImportantCorpsesFromRequest.push_back(info);

                while (g_RecentImportantCorpsesFromRequest.size() > 4)
                    g_RecentImportantCorpsesFromRequest.pop_front();
            }

            Log(
                "[Radio] RequestCorpse originalGameObjectId=0x%04X soldierIndex=%u important=YES officer=%s fromScript=%s\n",
                static_cast<unsigned int>(originalGameObjectId),
                static_cast<unsigned int>(info.soldierIndex),
                YesNo(info.isOfficer),
                YesNo(fromScript));
            return;
        }
    }

    Log(
        "[Radio] RequestCorpse originalGameObjectId=0x%04X soldierIndex=%u important=NO officer=NO fromScript=%s\n",
        static_cast<unsigned int>(originalGameObjectId),
        static_cast<unsigned int>(GameObjectIdToSoldierIndex(static_cast<std::uint32_t>(originalGameObjectId))),
        YesNo(fromScript));
}


static void __fastcall hkStateRadio(void* self, std::uint32_t slot, int proc)
{
    if (MissionCodeGuard::ShouldBypassHooks())
    {
        if (g_OrigStateRadio)
            g_OrigStateRadio(self, slot, proc);
        return;
    }

    if (proc == 0)
    {
        const std::uint8_t radioType = ReadRadioType(self, slot);

        std::lock_guard<std::mutex> lock(g_StateMutex);

        if (radioType == kRadioTypeBodyFound)
        {
            if (!g_PendingBodyFoundOverride.active && !g_DiscoveredImportantBodyQueue.empty())
            {
                g_PendingBodyFoundOverride.active = true;
                g_PendingBodyFoundOverride.target = g_DiscoveredImportantBodyQueue.front();

                Log(
                    "[Radio] Armed body-found override: slot=%u gameObjectId=0x%08X soldierIndex=%u officer=%s queueSize=%zu\n",
                    static_cast<unsigned int>(slot),
                    static_cast<unsigned int>(g_PendingBodyFoundOverride.target.gameObjectId),
                    static_cast<unsigned int>(g_PendingBodyFoundOverride.target.soldierIndex),
                    YesNo(g_PendingBodyFoundOverride.target.isOfficer),
                    g_DiscoveredImportantBodyQueue.size());
            }
        }
        else
        {
            g_PendingBodyFoundOverride = {};
        }
    }

    if (g_OrigStateRadio)
        g_OrigStateRadio(self, slot, proc);
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

    PendingBodyFoundOverride pending{};
    bool shouldConsumeQueueFront = false;

    {
        std::lock_guard<std::mutex> lock(g_StateMutex);
        if (radioType == kRadioTypeBodyFound && g_PendingBodyFoundOverride.active)
        {
            pending = g_PendingBodyFoundOverride;
            g_PendingBodyFoundOverride = {};
            shouldConsumeQueueFront = !g_DiscoveredImportantBodyQueue.empty();
        }
    }

    if (radioType == kRadioTypeBodyFound && pending.active)
    {
        if (pending.target.isOfficer)
        {
            Log(
                "[Radio] Officer body-found override: owner=%u arg5=0x%04X gameObjectId=0x%08X soldierIndex=%u speechLabel=0x%08X\n",
                static_cast<unsigned int>(ownerIndex),
                static_cast<unsigned int>(arg5),
                static_cast<unsigned int>(pending.target.gameObjectId),
                static_cast<unsigned int>(pending.target.soldierIndex),
                static_cast<unsigned int>(kOfficerBodyFoundSpeechLabel));

            if (shouldConsumeQueueFront)
            {
                std::lock_guard<std::mutex> lock(g_StateMutex);
                if (!g_DiscoveredImportantBodyQueue.empty())
                    g_DiscoveredImportantBodyQueue.pop_front();
            }

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
            Log(
                "[Radio] VIP body-found override: owner=%u arg5=0x%04X gameObjectId=0x%08X soldierIndex=%u radioType=0x%02X\n",
                static_cast<unsigned int>(ownerIndex),
                static_cast<unsigned int>(arg5),
                static_cast<unsigned int>(pending.target.gameObjectId),
                static_cast<unsigned int>(pending.target.soldierIndex),
                static_cast<unsigned int>(kRadioTypeVipBodyFound));

            if (shouldConsumeQueueFront)
            {
                std::lock_guard<std::mutex> lock(g_StateMutex);
                if (!g_DiscoveredImportantBodyQueue.empty())
                    g_DiscoveredImportantBodyQueue.pop_front();
            }

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


void Add_VIPRadioImportantTarget(std::uint32_t gameObjectId, std::uint16_t soldierIndex, bool isOfficer)
{
    if (MissionCodeGuard::ShouldBypassHooks())
        return;

    const ImportantTargetInfo info{
        gameObjectId,
        NormalizeSoldierIndex(gameObjectId, soldierIndex),
        isOfficer
    };

    std::lock_guard<std::mutex> lock(g_StateMutex);
    g_ImportantByGameObjectId[info.gameObjectId] = info;
    g_ImportantBySoldierIndex[info.soldierIndex] = info;

    Log(
        "[Radio] Added important target: gameObjectId=0x%08X soldierIndex=0x%04X officer=%s\n",
        static_cast<unsigned int>(info.gameObjectId),
        static_cast<unsigned int>(info.soldierIndex),
        YesNo(info.isOfficer));
}


void Add_VIPRadioImportantGameObjectId(std::uint32_t gameObjectId, bool isOfficer)
{
    Add_VIPRadioImportantTarget(
        gameObjectId,
        GameObjectIdToSoldierIndex(gameObjectId),
        isOfficer);
}


void Remove_VIPRadioImportantGameObjectId(std::uint32_t gameObjectId)
{
    if (MissionCodeGuard::ShouldBypassHooks())
        return;

    std::lock_guard<std::mutex> lock(g_StateMutex);

    const std::uint16_t soldierIndex = GameObjectIdToSoldierIndex(gameObjectId);
    g_ImportantByGameObjectId.erase(gameObjectId);
    g_ImportantBySoldierIndex.erase(soldierIndex);

    Log(
        "[Radio] Removed important target: gameObjectId=0x%08X soldierIndex=%u\n",
        static_cast<unsigned int>(gameObjectId),
        static_cast<unsigned int>(soldierIndex));
}


bool Notify_VIPRadioBodyDiscovered(std::uint32_t foundGameObjectId)
{
    if (MissionCodeGuard::ShouldBypassHooks())
        return false;

    std::lock_guard<std::mutex> lock(g_StateMutex);
    return QueueDiscoveredImportantBody(
        foundGameObjectId,
        GameObjectIdToSoldierIndex(foundGameObjectId));
}


bool Notify_VIPRadioBodyDiscoveredTarget(std::uint32_t foundGameObjectId, std::uint16_t foundSoldierIndex)
{
    if (MissionCodeGuard::ShouldBypassHooks())
        return false;

    std::lock_guard<std::mutex> lock(g_StateMutex);
    return QueueDiscoveredImportantBody(foundGameObjectId, foundSoldierIndex);
}


bool Try_GetSingleRecentImportantCorpseIndex(std::uint16_t& outSoldierIndex, bool& outIsOfficer)
{
    outSoldierIndex = 0xFFFFu;
    outIsOfficer = false;

    std::lock_guard<std::mutex> lock(g_StateMutex);

    if (g_RecentImportantCorpsesFromRequest.size() != 1)
        return false;

    outSoldierIndex = g_RecentImportantCorpsesFromRequest.front().soldierIndex;
    outIsOfficer = g_RecentImportantCorpsesFromRequest.front().isOfficer;
    return true;
}


void Clear_VIPRadioImportantGameObjectIds()
{
    std::lock_guard<std::mutex> lock(g_StateMutex);
    g_ImportantByGameObjectId.clear();
    g_ImportantBySoldierIndex.clear();
    ClearRuntimeRadioState_NoLock();

    Log("[Radio] Cleared important targets, discovered-body queue, duplicate cache, and pending radio state\n");
}


bool Install_VIPRadio_Hook()
{
    g_CallImpl = reinterpret_cast<CallImpl_t>(ResolveGameAddress(gAddr.CallImpl));

    void* requestCorpseTarget = ResolveGameAddress(gAddr.RequestCorpse);
    void* stateRadioTarget = ResolveGameAddress(gAddr.StateRadio);
    void* callWithRadioTypeTarget = ResolveGameAddress(gAddr.CallWithRadioType);

    if (!g_CallImpl || !requestCorpseTarget || !stateRadioTarget || !callWithRadioTypeTarget)
    {
        Log("[Hook] VIPRadio: address resolve failed\n");
        return false;
    }

    bool ok = true;

    ok = ok && CreateAndEnableHook(
        requestCorpseTarget,
        reinterpret_cast<void*>(&hkRequestCorpse),
        reinterpret_cast<void**>(&g_OrigRequestCorpse));

    ok = ok && CreateAndEnableHook(
        stateRadioTarget,
        reinterpret_cast<void*>(&hkStateRadio),
        reinterpret_cast<void**>(&g_OrigStateRadio));

    ok = ok && CreateAndEnableHook(
        callWithRadioTypeTarget,
        reinterpret_cast<void*>(&hkCallWithRadioType),
        reinterpret_cast<void**>(&g_OrigCallWithRadioType));

    Log("[Hook] VIPRadio: %s\n", ok ? "OK" : "FAIL");
    return ok;
}


bool Uninstall_VIPRadio_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.RequestCorpse));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.StateRadio));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.CallWithRadioType));

    g_OrigRequestCorpse = nullptr;
    g_OrigStateRadio = nullptr;
    g_OrigCallWithRadioType = nullptr;
    g_CallImpl = nullptr;

    Clear_VIPRadioImportantGameObjectIds();
    return true;
}