#include "pch.h"
#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "HookUtils.h"
#include "log.h"
#include "tpp/ui/menu/impl/MbDvcCassetteTapeCallbackImpl/MbDvcCassetteTapeCallbackImpl_SetCurrentAlbum.h"
#include "tpp/sd/SoundMusicPlayer/CustomTapeOwnership.h"

namespace
{
    using SetCurrentAlbum_t = void(__fastcall*)(void* thisPtr, std::uint64_t albumId);

    using GetCurrentAlbumInfo_t = void* (__fastcall*)(void* soundPlayer);
    using GetTrackInfoByAlbumIndex_t = void* (__fastcall*)(void* soundPlayer, std::uint64_t albumId, std::uint32_t trackIndex);

    static constexpr std::uintptr_t ABS_SetCurrentAlbum = 0x140EF7A50ull;

    static SetCurrentAlbum_t g_OrigSetCurrentAlbum = nullptr;
}

// Returns true if one vanilla save index is unlocked in the game's raw bitfield.
// Params: thisPtr, saveIndex
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

// Returns true if one track should be shown in the cassette menu.
// Params: thisPtr, albumType, trackInfo
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

// Rebuilds the callback's accepted direct-play track-id table using vanilla ownership for vanilla save indices and custom ownership for custom save indices.
// Params: thisPtr, albumId
static void RebuildAcceptedTrackIdsForCurrentAlbum(void* thisPtr, std::uint64_t albumId)
{
    if (!thisPtr)
        return;

    __try
    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(thisPtr);

        void* menuRoot = *reinterpret_cast<void**>(base + 0x30ull);
        if (!menuRoot)
            return;

        void* soundPlayer = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(menuRoot) + 0xD8ull);
        if (!soundPlayer)
            return;

        void** vtbl = *reinterpret_cast<void***>(soundPlayer);
        if (!vtbl)
            return;

        GetCurrentAlbumInfo_t getCurrentAlbumInfo =
            reinterpret_cast<GetCurrentAlbumInfo_t>(vtbl[0x170 / 8]);
        GetTrackInfoByAlbumIndex_t getTrackInfoByAlbumIndex =
            reinterpret_cast<GetTrackInfoByAlbumIndex_t>(vtbl[0x188 / 8]);

        if (!getCurrentAlbumInfo || !getTrackInfoByAlbumIndex)
            return;

        void* albumInfo = getCurrentAlbumInfo(soundPlayer);
        if (!albumInfo)
        {
            *reinterpret_cast<std::uint32_t*>(base + 0xF80ull) = 0;
            return;
        }

        const std::uint16_t trackCount =
            *reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::uintptr_t>(albumInfo) + 0x10ull);

        const std::uint16_t albumType =
            *reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::uintptr_t>(albumInfo) + 0x12ull);

        std::uint32_t* directPlayTrackIdTable =
            reinterpret_cast<std::uint32_t*>(base + 0xD80ull);

        std::memset(directPlayTrackIdTable, 0, 0x80u * sizeof(std::uint32_t));

        std::uint32_t acceptedCount = 0;

        for (std::uint32_t trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        {
            void* trackInfo = getTrackInfoByAlbumIndex(soundPlayer, albumId, trackIndex);
            if (!trackInfo)
                continue;

            if (!IsTrackAcceptedForMenu(thisPtr, albumType, trackInfo))
                continue;

            if (acceptedCount >= 0x80u)
                break;

            const std::uint32_t directPlayTrackId =
                *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(trackInfo) + 0x14ull);

            directPlayTrackIdTable[acceptedCount] = directPlayTrackId;
            ++acceptedCount;
        }

        *reinterpret_cast<std::uint32_t*>(base + 0xF80ull) = acceptedCount;

        Log(
            "[CassetteMenu] SetCurrentAlbum rebuild: albumId=%016llX acceptedCount=%u\n",
            static_cast<unsigned long long>(albumId),
            acceptedCount);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CassetteMenu] SetCurrentAlbum rebuild crashed\n");
    }
}

// Hooked SetCurrentAlbum.
// Params: thisPtr, albumId
static void __fastcall hkSetCurrentAlbum(void* thisPtr, std::uint64_t albumId)
{
    if (!g_OrigSetCurrentAlbum)
        return;

    g_OrigSetCurrentAlbum(thisPtr, albumId);
    RebuildAcceptedTrackIdsForCurrentAlbum(thisPtr, albumId);
}

// Installs the SetCurrentAlbum hook.
// Params: none
bool Install_CassetteTapeSetCurrentAlbum_Hook()
{
    void* target = ResolveGameAddress(ABS_SetCurrentAlbum);
    if (!target)
    {
        Log("[CassetteMenu] SetCurrentAlbum resolve failed\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkSetCurrentAlbum),
        reinterpret_cast<void**>(&g_OrigSetCurrentAlbum));

    Log("[CassetteMenu] SetCurrentAlbum: %s\n", ok ? "OK" : "FAIL");
    return ok;
}

// Removes the SetCurrentAlbum hook.
// Params: none
bool Uninstall_CassetteTapeSetCurrentAlbum_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(ABS_SetCurrentAlbum));
    g_OrigSetCurrentAlbum = nullptr;
    return true;
}