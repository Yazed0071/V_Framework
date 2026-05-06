#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>

#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "VIPRadioHook.h"
#include "StepRadioDiscovery.h"
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
        // StrCode32 hash. 0 = no override, fall back to officer/VIP defaults.
        std::uint32_t customDeadBodyLabel = 0;
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
    static CallWithRadioType_t g_OrigCallWithRadioType = nullptr;
    static CallImpl_t g_CallImpl = nullptr;

    static std::mutex g_StateMutex;


    static std::unordered_map<std::uint32_t, ImportantTargetInfo> g_ImportantByGameObjectId;
    static std::unordered_map<std::uint16_t, ImportantTargetInfo> g_ImportantBySoldierIndex;


    static std::deque<ImportantTargetInfo> g_RecentImportantCorpsesFromRequest;
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


// At radioType=0x0E (BodyFound), the StateRadio entry's `arg5` (entry+0x06)
// carries the body's GameObjectId — written by Update from the AddNoticeInfo
// blob bytes [4..5] that CheckSightNoticeSoldier emits for body sightings.
// We look the body up directly instead of guessing from a Lua-populated FIFO
// queue (which mismatched soldier↔body when multiple bodies were active).
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

    // Hostage-discovery (FOUND alive) override: speaker-soldier-keyed lookup
    // populated by NoticeHostageAi tracking in StepRadioDiscovery. The AI emit
    // site for these radioTypes (0x0B/0x0C/0x12/0x25) doesn't pass arg5, so we
    // can't read the target from the entry — we rely on the per-soldier pending
    // map keyed by ownerIndex (the speaker).
    {
        std::uint32_t hostageOverrideLabel = 0u;
        if (LostHostageDiscovery_TryOverrideForCallWithRadioType(
                ownerIndex, radioType, hostageOverrideLabel)
            && hostageOverrideLabel != 0u)
        {
            Log(
                "[Radio] Hostage-found override: owner=%u radioType=0x%02X arg5=0x%04X overrideLabel=0x%08X\n",
                static_cast<unsigned int>(ownerIndex),
                static_cast<unsigned int>(radioType),
                static_cast<unsigned int>(arg5),
                static_cast<unsigned int>(hostageOverrideLabel));

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

    if (radioType == kRadioTypeBodyFound || radioType == kRadioTypeVipBodyFound)
    {
        ImportantTargetInfo info{};
        bool found = false;
        const char* matchSource = "miss";

        {
            std::lock_guard<std::mutex> lock(g_StateMutex);

            // Primary: arg5 lookup. Works when the radio carries the body's
            // registered gameObjectId directly (e.g. soldier-discovers-soldier
            // in a down-but-alive state).
            if (FindImportantTarget(static_cast<std::uint32_t>(arg5), info))
            {
                found = true;
                matchSource = "arg5";
            }
            // Fallback: for KILLED targets, the body-found radio uses the
            // corpse's gameObjectId (a separate entity assigned by the game),
            // not the original soldier's id we registered. hkRequestCorpse
            // populates g_RecentImportantCorpsesFromRequest whenever an
            // important target's corpse is created. Pop the front so a
            // subsequent body-found radio doesn't double-fire on the same
            // corpse.
            else if (!g_RecentImportantCorpsesFromRequest.empty())
            {
                info = g_RecentImportantCorpsesFromRequest.front();
                g_RecentImportantCorpsesFromRequest.pop_front();
                found = true;
                matchSource = "recent-corpse";
            }
        }

        if (found)
        {
            Log(
                "[Radio] BodyFound DIAG: owner=%u radioType=0x%02X arg5=0x%04X lookup=MATCH(%s) gameObjectId=0x%08X soldierIndex=%u officer=%s\n",
                static_cast<unsigned int>(ownerIndex),
                static_cast<unsigned int>(radioType),
                static_cast<unsigned int>(arg5),
                matchSource,
                static_cast<unsigned int>(info.gameObjectId),
                static_cast<unsigned int>(info.soldierIndex),
                YesNo(info.isOfficer));
        }
        else
        {
            Log(
                "[Radio] BodyFound DIAG: owner=%u radioType=0x%02X arg5=0x%04X lookup=MISS\n",
                static_cast<unsigned int>(ownerIndex),
                static_cast<unsigned int>(radioType),
                static_cast<unsigned int>(arg5));
        }

        if (radioType == kRadioTypeBodyFound && found)
        {
            // Caller-supplied StrCode32 label takes priority over both the
            // officer-default and the non-officer radioType swap.
            if (info.customDeadBodyLabel != 0 && g_CallImpl)
            {
                Log(
                    "[Radio] Custom body-found override: owner=%u arg5=0x%04X gameObjectId=0x%08X soldierIndex=%u customLabel=0x%08X\n",
                    static_cast<unsigned int>(ownerIndex),
                    static_cast<unsigned int>(arg5),
                    static_cast<unsigned int>(info.gameObjectId),
                    static_cast<unsigned int>(info.soldierIndex),
                    static_cast<unsigned int>(info.customDeadBodyLabel));

                return g_CallImpl(
                    reinterpret_cast<long long>(self) - 0x20ll,
                    outHandle,
                    static_cast<int>(ownerIndex),
                    info.customDeadBodyLabel,
                    arg5);
            }

            if (info.isOfficer)
            {
                Log(
                    "[Radio] Officer body-found override: owner=%u arg5=0x%04X gameObjectId=0x%08X soldierIndex=%u speechLabel=0x%08X\n",
                    static_cast<unsigned int>(ownerIndex),
                    static_cast<unsigned int>(arg5),
                    static_cast<unsigned int>(info.gameObjectId),
                    static_cast<unsigned int>(info.soldierIndex),
                    static_cast<unsigned int>(kOfficerBodyFoundSpeechLabel));

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
                    static_cast<unsigned int>(info.gameObjectId),
                    static_cast<unsigned int>(info.soldierIndex),
                    static_cast<unsigned int>(kRadioTypeVipBodyFound));

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
    }

    if (g_OrigCallWithRadioType)
        return g_OrigCallWithRadioType(self, outHandle, ownerIndex, radioType, arg5);

    return outHandle;
}


void Add_VIPRadioImportantTarget(std::uint32_t gameObjectId, std::uint16_t soldierIndex, bool isOfficer, std::uint32_t customDeadBodyLabel)
{
    if (MissionCodeGuard::ShouldBypassHooks())
        return;

    const ImportantTargetInfo info{
        gameObjectId,
        NormalizeSoldierIndex(gameObjectId, soldierIndex),
        isOfficer,
        customDeadBodyLabel
    };

    std::lock_guard<std::mutex> lock(g_StateMutex);
    g_ImportantByGameObjectId[info.gameObjectId] = info;
    g_ImportantBySoldierIndex[info.soldierIndex] = info;

    Log(
        "[Radio] Added important target: gameObjectId=0x%08X soldierIndex=0x%04X officer=%s customDeadBodyLabel=0x%08X\n",
        static_cast<unsigned int>(info.gameObjectId),
        static_cast<unsigned int>(info.soldierIndex),
        YesNo(info.isOfficer),
        static_cast<unsigned int>(info.customDeadBodyLabel));
}


void Add_VIPRadioImportantGameObjectId(std::uint32_t gameObjectId, bool isOfficer, std::uint32_t customDeadBodyLabel)
{
    Add_VIPRadioImportantTarget(
        gameObjectId,
        GameObjectIdToSoldierIndex(gameObjectId),
        isOfficer,
        customDeadBodyLabel);
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
    g_RecentImportantCorpsesFromRequest.clear();

    Log("[Radio] Cleared important targets and recent corpse log\n");
}


bool Install_VIPRadio_Hook()
{
    g_CallImpl = reinterpret_cast<CallImpl_t>(ResolveGameAddress(gAddr.CallImpl));

    void* requestCorpseTarget = ResolveGameAddress(gAddr.RequestCorpse);
    void* callWithRadioTypeTarget = ResolveGameAddress(gAddr.CallWithRadioType);

    if (!g_CallImpl || !requestCorpseTarget || !callWithRadioTypeTarget)
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
        callWithRadioTypeTarget,
        reinterpret_cast<void*>(&hkCallWithRadioType),
        reinterpret_cast<void**>(&g_OrigCallWithRadioType));

    Log("[Hook] VIPRadio: %s\n", ok ? "OK" : "FAIL");
    return ok;
}


bool Uninstall_VIPRadio_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.RequestCorpse));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.CallWithRadioType));

    g_OrigRequestCorpse = nullptr;
    g_OrigCallWithRadioType = nullptr;
    g_CallImpl = nullptr;

    Clear_VIPRadioImportantGameObjectIds();
    return true;
}
