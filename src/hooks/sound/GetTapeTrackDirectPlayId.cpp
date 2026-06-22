#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "HookUtils.h"
#include "log.h"
#include "FoxHashes.h"
#include "GetTapeTrackDirectPlayId.h"
#include "AddressSet.h"

namespace
{
    using GetTrackInfoByName_t = void* (__fastcall*)(void* thisPtr, std::int32_t trackNameStrCode);
}


std::int32_t ResolveTapeTrackDirectPlayId(const char* trackName)
{
    if (trackName == nullptr || trackName[0] == '\0')
        return -1;

    const std::uint32_t trackNameStrCode = FoxHashes::StrCode32(trackName);
    if (trackNameStrCode == 0)
    {
        Log("[TapeDirectPlayId] WARN: track name '%s' hashed to 0 — cannot resolve its play id.\n", trackName);
        return -1;
    }

    void* fnAddr = ResolveGameAddress(gAddr.GetTrackInfoByName);
    if (fnAddr == nullptr)
    {
        Log("[TapeDirectPlayId] ERROR: GetTrackInfoByName address unavailable for this build — cannot resolve tape play ids.\n");
        return -1;
    }

    void* musicManagerGlobalAddr = ResolveGameAddress(gAddr.MusicManager_s_instance);
    if (musicManagerGlobalAddr == nullptr)
    {
        Log("[TapeDirectPlayId] ERROR: MusicManager::s_instance address unavailable for this build — cannot resolve tape play ids.\n");
        return -1;
    }

    GetTrackInfoByName_t GetTrackInfoByName =
        reinterpret_cast<GetTrackInfoByName_t>(fnAddr);

    __try
    {
        void* musicManagerInstance = *reinterpret_cast<void**>(musicManagerGlobalAddr);
        if (musicManagerInstance == nullptr)
        {
            Log("[TapeDirectPlayId] WARN: MusicManager not initialized yet — cannot resolve tape play id for '%s'.\n", trackName);
            return -1;
        }

        void* soundMusicPlayer =
            *reinterpret_cast<void**>(
                reinterpret_cast<std::uintptr_t>(musicManagerInstance) + 0xA8ull);

        if (soundMusicPlayer == nullptr)
        {
            Log("[TapeDirectPlayId] WARN: SoundMusicPlayer not initialized yet — cannot resolve tape play id for '%s'.\n", trackName);
            return -1;
        }

        void* trackInfo = GetTrackInfoByName(
            soundMusicPlayer,
            static_cast<std::int32_t>(trackNameStrCode));

        if (trackInfo == nullptr)
        {
            Log(
                "[TapeDirectPlayId] WARN: no tape track named '%s' (strCode=%08X) — check the track is registered/loaded.\n",
                trackName,
                static_cast<unsigned int>(trackNameStrCode));
            return -1;
        }

        const std::uint32_t directPlayTrackId =
            *reinterpret_cast<const std::uint32_t*>(
                reinterpret_cast<const std::uintptr_t>(trackInfo) + 0x14ull);

        return static_cast<std::int32_t>(directPlayTrackId);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log(
            "[TapeDirectPlayId] ERROR: exception while resolving tape play id for '%s' (strCode=%08X) — could not read track info.\n",
            trackName,
            static_cast<unsigned int>(trackNameStrCode));
        return -1;
    }
}