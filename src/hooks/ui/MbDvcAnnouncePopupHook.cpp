#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

#include "HookUtils.h"
#include "log.h"
#include "AddressSet.h"
#include "FoxHashes.h"
#include "MbDvcAnnouncePopupHook.h"


using GetLangText_t = const char*(__fastcall*)(std::uint64_t hash);

using QueueSubmit_t = void(__fastcall*)(void* queue, const void* reservation);


static constexpr const char* kDefaultLangKey = "mbtutorial_exit_1_b";


static GetLangText_t            g_OrigGetLangText = nullptr;
static std::atomic<std::uint64_t> g_ActiveTargetHash{ 0 };
static std::atomic<bool>        g_OverrideArmed{ false };
static std::mutex               g_OverrideMutex;
static std::string              g_OverrideText;


struct AnnounceReservation
{
    std::int32_t commonValue1;
    std::int32_t commonValue2;
    std::int32_t commonValue3;
    std::int32_t commonValue4;
    std::uint8_t reserveId;
    std::uint8_t pad[7];
};


static const char* __fastcall hkGetLangText(std::uint64_t hash)
{
    if (g_OverrideArmed.load())
    {
        const std::uint64_t target = g_ActiveTargetHash.load();
        if (target != 0 && hash == target)
        {
            std::lock_guard<std::mutex> lock(g_OverrideMutex);
            if (g_OverrideArmed.exchange(false))
            {
                Log("[MbDvcAnnouncePopup] hkGetLangText: hash=0x%llX matched active target — returning override text (%zu bytes)\n",
                    static_cast<unsigned long long>(hash),
                    g_OverrideText.size());
                g_ActiveTargetHash.store(0);
                return g_OverrideText.c_str();
            }
        }
    }

    if (!g_OrigGetLangText)
        return "";

    return g_OrigGetLangText(hash);
}


bool Install_MbDvcAnnouncePopupHook()
{
    void* addrGetLang = ResolveGameAddress(gAddr.TppUi_GetLangText);
    if (!addrGetLang)
    {
        Log("[MbDvcAnnouncePopup] Install skipped: TppUi_GetLangText address not resolved\n");
        return false;
    }

    if (!FoxHashes::Resolve())
    {
        Log("[MbDvcAnnouncePopup] Install skipped: FoxHashes not resolved\n");
        return false;
    }

    const std::uint64_t defaultHash = FoxHashes::StrCode64(kDefaultLangKey);
    Log("[MbDvcAnnouncePopup] default lang anchor '%s' -> hash 0x%llX\n",
        kDefaultLangKey,
        static_cast<unsigned long long>(defaultHash));

    const bool ok = CreateAndEnableHook(
        addrGetLang,
        reinterpret_cast<void*>(&hkGetLangText),
        reinterpret_cast<void**>(&g_OrigGetLangText));

    Log("[MbDvcAnnouncePopup] Hook GetLangText: %s target=%p orig=%p\n",
        ok ? "OK" : "FAIL",
        addrGetLang,
        reinterpret_cast<void*>(g_OrigGetLangText));

    return ok;
}


bool Uninstall_MbDvcAnnouncePopupHook()
{
    void* addr = ResolveGameAddress(gAddr.TppUi_GetLangText);
    if (addr)
    {
        DisableAndRemoveHook(addr);
    }

    g_OrigGetLangText = nullptr;
    g_OverrideArmed.store(false);
    g_ActiveTargetHash.store(0);

    std::lock_guard<std::mutex> lock(g_OverrideMutex);
    g_OverrideText.clear();
    return true;
}


static bool TrySubmitToQueue(const AnnounceReservation* reservation)
{
    void* uiMgrPtr = ResolveGameAddress(gAddr.UiCommonDataManager_Instance);
    if (!uiMgrPtr)
    {
        Log("[MbDvcAnnouncePopup] submit: UiCommonDataManager_Instance address not resolved\n");
        return false;
    }

    void* uiMgr = *reinterpret_cast<void**>(uiMgrPtr);
    if (!uiMgr)
    {
        Log("[MbDvcAnnouncePopup] submit: UiCommonDataManager not initialized yet\n");
        return false;
    }

    void** queue = *reinterpret_cast<void***>(
        reinterpret_cast<std::uint8_t*>(uiMgr) + 0xA0);
    if (!queue)
    {
        Log("[MbDvcAnnouncePopup] submit: announce queue at uiMgr+0xA0 is null\n");
        return false;
    }

    void** queueVtable = *reinterpret_cast<void***>(queue);
    if (!queueVtable)
    {
        Log("[MbDvcAnnouncePopup] submit: queue vtable is null\n");
        return false;
    }

    auto submitFn = reinterpret_cast<QueueSubmit_t>(queueVtable[1]);
    if (!submitFn)
    {
        Log("[MbDvcAnnouncePopup] submit: queue vtable[1] (submit) is null\n");
        return false;
    }

    __try
    {
        submitFn(queue, reservation);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[MbDvcAnnouncePopup] submit: queue->submit raised access violation\n");
        return false;
    }
}


namespace MbDvcAnnouncePopup
{
    std::uint64_t GetActiveTargetHash()
    {
        return g_ActiveTargetHash.load();
    }

    bool ShowPopup(const char* text, std::uint8_t reserveId, const char* langKey)
    {
        if (!text || !*text)
        {
            Log("[MbDvcAnnouncePopup] ShowPopup: empty or missing text\n");
            return false;
        }

        if (reserveId == 0x0E)
        {
            Log("[MbDvcAnnouncePopup] ShowPopup: reserveId 0x0E is the unset sentinel; pick another id\n");
            return false;
        }

        const char* effectiveLangKey = (langKey && *langKey) ? langKey : kDefaultLangKey;

        if (!FoxHashes::Resolve())
        {
            Log("[MbDvcAnnouncePopup] ShowPopup: FoxHashes not resolved — hook not installed?\n");
            return false;
        }

        const std::uint64_t targetHash = FoxHashes::StrCode64(effectiveLangKey);
        if (targetHash == 0)
        {
            Log("[MbDvcAnnouncePopup] ShowPopup: FoxStrCode64('%s') returned 0\n", effectiveLangKey);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(g_OverrideMutex);
            g_OverrideText.assign(text);
        }
        g_ActiveTargetHash.store(targetHash);
        g_OverrideArmed.store(true);

        AnnounceReservation reservation{};
        reservation.reserveId = reserveId;

        if (!TrySubmitToQueue(&reservation))
        {
            g_OverrideArmed.store(false);
            g_ActiveTargetHash.store(0);
            std::lock_guard<std::mutex> lock(g_OverrideMutex);
            g_OverrideText.clear();
            return false;
        }

        Log("[MbDvcAnnouncePopup] ShowPopup: queued reserveId=0x%02X langKey='%s' (hash=0x%llX, %zu text bytes)\n",
            reserveId,
            effectiveLangKey,
            static_cast<unsigned long long>(targetHash),
            std::strlen(text));

        return true;
    }
}
