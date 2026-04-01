#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "HookUtils.h"
#include "AddressSet.h"
#include "log.h"
#include "FoxHashes.h"
#include "GetTapeTrackDirectPlayId.h"

namespace
{
    // tpp::sd::SoundMusicPlayer::GetTrackInfoByName
    // Params: thisPtr, trackNameStrCode
    using GetTrackInfoByName_t = void* (__fastcall*)(void* thisPtr, std::int32_t trackNameStrCode);


    // Replace this with your real absolute address for:
    // _?s_instance@MusicManager@sd@tpp@@0VGlobalEntityPtr@fox@@A
}

// Resolves a direct-play tape track id from a C string.
// Params: trackName
// Returns: direct-play track id, or -1 on failure.
std::int32_t ResolveTapeTrackDirectPlayId(const char* trackName)
{
    if (trackName == nullptr || trackName[0] == '\0')
        return -1;

    const std::uint32_t trackNameStrCode = FoxHashes::StrCode32(trackName);
    if (trackNameStrCode == 0)
    {
        Log("[TapeDirectPlayId] StrCode32 failed for %s\n", trackName);
        return -1;
    }

    void* fnAddr = ResolveGameAddress(gAddr.GetTrackInfoByName);
    if (fnAddr == nullptr)
    {
        Log("[TapeDirectPlayId] GetTrackInfoByName address resolve failed\n");
        return -1;
    }

    void* musicManagerGlobalAddr = ResolveGameAddress(gAddr.MusicManager_s_instance);
    if (musicManagerGlobalAddr == nullptr)
    {
        Log("[TapeDirectPlayId] MusicManager::s_instance address resolve failed\n");
        return -1;
    }

    GetTrackInfoByName_t GetTrackInfoByName =
        reinterpret_cast<GetTrackInfoByName_t>(fnAddr);

    __try
    {
        void* musicManagerInstance = *reinterpret_cast<void**>(musicManagerGlobalAddr);
        if (musicManagerInstance == nullptr)
        {
            Log("[TapeDirectPlayId] MusicManager::s_instance is null\n");
            return -1;
        }

        void* soundMusicPlayer =
            *reinterpret_cast<void**>(
                reinterpret_cast<std::uintptr_t>(musicManagerInstance) + 0xA8ull);

        if (soundMusicPlayer == nullptr)
        {
            Log("[TapeDirectPlayId] SoundMusicPlayer is null\n");
            return -1;
        }

        void* trackInfo = GetTrackInfoByName(
            soundMusicPlayer,
            static_cast<std::int32_t>(trackNameStrCode));

        if (trackInfo == nullptr)
        {
            Log(
                "[TapeDirectPlayId] GetTrackInfoByName failed"
                " trackName=%s"
                " strCode=%08X\n",
                trackName,
                static_cast<unsigned int>(trackNameStrCode));
            return -1;
        }

        const std::uint32_t directPlayTrackId =
            *reinterpret_cast<const std::uint32_t*>(
                reinterpret_cast<const std::uintptr_t>(trackInfo) + 0x14ull);

        Log(
            "[TapeDirectPlayId] Resolved"
            " trackName=%s"
            " strCode=%08X"
            " musicManager=%p"
            " soundMusicPlayer=%p"
            " trackInfo=%p"
            " directPlayTrackId=%u (0x%X)\n",
            trackName,
            static_cast<unsigned int>(trackNameStrCode),
            musicManagerInstance,
            soundMusicPlayer,
            trackInfo,
            static_cast<unsigned int>(directPlayTrackId),
            static_cast<unsigned int>(directPlayTrackId));

        return static_cast<std::int32_t>(directPlayTrackId);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log(
            "[TapeDirectPlayId] Exception while resolving"
            " trackName=%s"
            " strCode=%08X"
            " global=%p\n",
            trackName,
            static_cast<unsigned int>(trackNameStrCode),
            musicManagerGlobalAddr);
        return -1;
    }
}