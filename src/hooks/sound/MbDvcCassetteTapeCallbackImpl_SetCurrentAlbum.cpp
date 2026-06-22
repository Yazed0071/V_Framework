#include "pch.h"
#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "HookUtils.h"
#include "log.h"
#include "MbDvcCassetteTapeCallbackImpl_SetCurrentAlbum.h"
#include "CustomTapeOwnership.h"
#include "AddressSet.h"

namespace
{
    using SetCurrentAlbum_t = void(__fastcall*)(void* thisPtr, std::uint64_t albumId);

    using GetCurrentAlbumInfo_t = void* (__fastcall*)(void* soundPlayer);
    using GetTrackInfoByAlbumIndex_t = void* (__fastcall*)(void* soundPlayer, std::uint64_t albumId, std::uint32_t trackIndex);

    static SetCurrentAlbum_t g_OrigSetCurrentAlbum = nullptr;
}


static bool IsVanillaTapeOwnedBySaveIndex(void* thisPtr, std::int16_t saveIndex)
{
    if (!thisPtr || saveIndex < 0)
        return false;

    const std::uint16_t bitIndex = static_cast<std::uint16_t>(saveIndex + 0x00B7u);
    if (bitIndex == 0xFFFFu)
        return false;

    __try
    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(thisPtr);

        void* menuRoot = *reinterpret_cast<void**>(base + 0x30ull);
        if (!menuRoot)
            return false;

        void* quarkOwner = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(menuRoot) + 0xB0ull);
        if (!quarkOwner)
            return false;

        void* ae8Obj = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(quarkOwner) + 0xAE8ull);
        if (!ae8Obj)
            return false;

        std::uint8_t* ownedTable = *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uintptr_t>(ae8Obj) + 0x740ull);
        if (!ownedTable)
            return false;

        return ((ownedTable[bitIndex] >> 1) & 1u) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


static bool IsTrackAcceptedForMenu(void* thisPtr, std::uint16_t albumType, void* trackInfo)
{
    if (!trackInfo)
        return false;

    if (albumType == 10)
        return true;

    const std::int16_t saveIndex =
        *reinterpret_cast<std::int16_t*>(reinterpret_cast<std::uintptr_t>(trackInfo) + 0x1Cull);

    if (saveIndex < 0)
        return false;

    if (IsCustomTapeSaveIndex(saveIndex))
    {
        const bool owned = IsCustomTapeOwnedSaveIndex(saveIndex);
        return owned;
    }

    return IsVanillaTapeOwnedBySaveIndex(thisPtr, saveIndex);
}

using GetAlbumArray_t = std::uintptr_t(__fastcall*)(void* thisPtr);
using GetAlbumCount_t = std::uint32_t(__fastcall*)(void* thisPtr);


static void* FindLiveAlbumRecordByAlbumId(void* soundPlayer, std::uint64_t albumId)
{
    if (!soundPlayer || albumId == 0)
        return nullptr;

    __try
    {
        std::uintptr_t* vtbl = *reinterpret_cast<std::uintptr_t**>(soundPlayer);
        if (!vtbl)
        {
            Log("[CassetteMenu] WARN: SoundMusicPlayer vtable is null (soundPlayer=%p) — cannot list albums; custom tapes may not show in the menu.\n", soundPlayer);
            return nullptr;
        }

        GetAlbumArray_t getAlbumArray =
            reinterpret_cast<GetAlbumArray_t>(vtbl[0x160ull / 8ull]);

        GetAlbumCount_t getAlbumCount =
            reinterpret_cast<GetAlbumCount_t>(vtbl[0x150ull / 8ull]);

        if (!getAlbumArray || !getAlbumCount)
        {
            Log(
                "[CassetteMenu] WARN: album accessor vtable slots are null (soundPlayer=%p) — wrong build offsets; custom tapes may not show in the menu.\n",
                soundPlayer);
            return nullptr;
        }

        const std::uintptr_t albumArrayBase = getAlbumArray(soundPlayer);
        const std::uint32_t albumCount = getAlbumCount(soundPlayer);

        if (albumArrayBase == 0 || albumCount == 0)
        {
            return nullptr;
        }

        const std::uint8_t* albumCursor =
            reinterpret_cast<const std::uint8_t*>(albumArrayBase + 0x10ull);

        for (std::uint32_t i = 0; i < albumCount; ++i)
        {
            const std::uint8_t* recordBase = albumCursor - 0x10ull;
            const std::uint64_t currentAlbumId =
                *reinterpret_cast<const std::uint64_t*>(recordBase + 0x00ull);

            if (currentAlbumId == albumId)
            {
                return const_cast<std::uint8_t*>(recordBase);
            }

            albumCursor += 0x18ull;
        }

        return nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log(
            "[CassetteMenu] WARN: exception while scanning the album list (soundPlayer=%p albumId=%016llX) — cassette menu may be incomplete.\n",
            soundPlayer,
            static_cast<unsigned long long>(albumId));
        return nullptr;
    }
}


static void RebuildAcceptedTrackIdsForCurrentAlbum(void* thisPtr, std::uint64_t albumId)
{
    if (!thisPtr)
    {
        Log("[CassetteMenu] WARN: cassette callback is null — cannot rebuild the album's track list; custom tapes may not show.\n");
        return;
    }

    __try
    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(thisPtr);

        void* menuRoot = *reinterpret_cast<void**>(base + 0x30ull);
        if (!menuRoot)
        {
            Log(
                "[CassetteMenu] WARN: cassette menu root is null (albumId=%016llX) — cannot rebuild the album's track list; custom tapes may not show.\n",
                static_cast<unsigned long long>(albumId));
            return;
        }

        void* soundPlayer = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(menuRoot) + 0xD8ull);
        if (!soundPlayer)
        {
            Log(
                "[CassetteMenu] WARN: SoundMusicPlayer is null (albumId=%016llX) — cannot rebuild the album's track list; custom tapes may not show.\n",
                static_cast<unsigned long long>(albumId));
            return;
        }

        void** vtbl = *reinterpret_cast<void***>(soundPlayer);
        if (!vtbl)
        {
            Log("[CassetteMenu] WARN: SoundMusicPlayer vtable is null (soundPlayer=%p) — cannot rebuild the album's track list; custom tapes may not show.\n", soundPlayer);
            return;
        }

        GetTrackInfoByAlbumIndex_t getTrackInfoByAlbumIndex =
            reinterpret_cast<GetTrackInfoByAlbumIndex_t>(vtbl[0x188 / 8]);

        if (!getTrackInfoByAlbumIndex)
        {
            Log("[CassetteMenu] WARN: track accessor vtable slot is null (soundPlayer=%p) — wrong build offsets; custom tapes may not show in the menu.\n", soundPlayer);
            return;
        }

        void* albumRecord = FindLiveAlbumRecordByAlbumId(soundPlayer, albumId);
        if (!albumRecord)
        {
            *reinterpret_cast<std::uint32_t*>(base + 0xF80ull) = 0;

            return;
        }

        const std::uint16_t trackCount =
            *reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::uintptr_t>(albumRecord) + 0x10ull);

        const std::uint16_t albumType =
            *reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::uintptr_t>(albumRecord) + 0x12ull);

        std::uint32_t* directPlayTrackIdTable =
            reinterpret_cast<std::uint32_t*>(base + 0xD80ull);

        std::memset(directPlayTrackIdTable, 0, 0x80u * sizeof(std::uint32_t));

        std::uint32_t acceptedCount = 0;

        for (std::uint32_t trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        {
            void* trackInfo = getTrackInfoByAlbumIndex(soundPlayer, albumId, trackIndex);
            if (!trackInfo)
            {
                continue;
            }

            if (!IsTrackAcceptedForMenu(thisPtr, albumType, trackInfo))
            {
                continue;
            }

            if (acceptedCount >= 0x80u)
            {
                Log("[CassetteMenu] WARN: album has more than 128 owned tracks — extra tracks are dropped from the cassette menu.\n");
                break;
            }

            const std::uint32_t directPlayTrackId =
                *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(trackInfo) + 0x14ull);

            directPlayTrackIdTable[acceptedCount] = directPlayTrackId;

            ++acceptedCount;
        }

        *reinterpret_cast<std::uint32_t*>(base + 0xF80ull) = acceptedCount;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassetteMenu] ERROR: exception while rebuilding the cassette menu track list — the album's track list may be wrong or empty.\n");
    }
}

static void __fastcall hkSetCurrentAlbum(void* thisPtr, std::uint64_t albumId)
{
    if (!g_OrigSetCurrentAlbum)
    {
        Log("[CassetteMenu] ERROR: SetCurrentAlbum trampoline is null — the hook failed to install; custom tapes will not show in the menu.\n");
        return;
    }

    g_OrigSetCurrentAlbum(thisPtr, albumId);

    Sync_CustomTapeStateToLiveTable();
    RebuildAcceptedTrackIdsForCurrentAlbum(thisPtr, albumId);
}


bool Install_CassetteTapeSetCurrentAlbum_Hook()
{
    void* target = ResolveGameAddress(gAddr.SetCurrentAlbum);
    if (!target)
    {
        Log("[CassetteMenu] ERROR: SetCurrentAlbum address unavailable for this build — custom tapes will not show in the cassette menu.\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkSetCurrentAlbum),
        reinterpret_cast<void**>(&g_OrigSetCurrentAlbum));

    if (!ok)
        Log("[CassetteMenu] ERROR: failed to hook SetCurrentAlbum — custom tapes will not show in the cassette menu.\n");
    return ok;
}


bool Uninstall_CassetteTapeSetCurrentAlbum_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.SetCurrentAlbum));
    g_OrigSetCurrentAlbum = nullptr;
    return true;
}