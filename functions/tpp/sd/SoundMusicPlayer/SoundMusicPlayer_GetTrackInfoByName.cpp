#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "HookUtils.h"
#include "log.h"

namespace
{
    // SoundMusicPlayer::GetTrackInfoByName
    // Params: this, trackNameStrCode
    using GetTrackInfoByName_t = void* (__fastcall*)(void* thisPtr, std::int32_t trackNameStrCode);

    static constexpr std::uintptr_t ABS_GetTrackInfoByName = 0x14614C0C0ull;

    static GetTrackInfoByName_t g_OrigGetTrackInfoByName = nullptr;
}

// Logs one 16-byte row in hex.
// Params: prefix, baseOffset, rowBytes
static void LogHexRow(const char* prefix, std::size_t baseOffset, const std::uint8_t* rowBytes)
{
    Log(
        "%s +0x%02zX : "
        "%02X %02X %02X %02X %02X %02X %02X %02X "
        "%02X %02X %02X %02X %02X %02X %02X %02X\n",
        prefix,
        baseOffset,
        static_cast<unsigned int>(rowBytes[0]),
        static_cast<unsigned int>(rowBytes[1]),
        static_cast<unsigned int>(rowBytes[2]),
        static_cast<unsigned int>(rowBytes[3]),
        static_cast<unsigned int>(rowBytes[4]),
        static_cast<unsigned int>(rowBytes[5]),
        static_cast<unsigned int>(rowBytes[6]),
        static_cast<unsigned int>(rowBytes[7]),
        static_cast<unsigned int>(rowBytes[8]),
        static_cast<unsigned int>(rowBytes[9]),
        static_cast<unsigned int>(rowBytes[10]),
        static_cast<unsigned int>(rowBytes[11]),
        static_cast<unsigned int>(rowBytes[12]),
        static_cast<unsigned int>(rowBytes[13]),
        static_cast<unsigned int>(rowBytes[14]),
        static_cast<unsigned int>(rowBytes[15]));
}

// Dumps one 0x38-byte trackInfo record.
// Params: trackInfo
static void DumpTrackInfoRecord(void* trackInfo)
{
    if (!trackInfo)
        return;

    std::uint8_t bytes[0x38] = {};

    __try
    {
        std::memcpy(bytes, trackInfo, sizeof(bytes));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[TrackInfo] exception while copying record\n");
        return;
    }

    LogHexRow("[TrackInfo]", 0x00, &bytes[0x00]);
    LogHexRow("[TrackInfo]", 0x10, &bytes[0x10]);
    LogHexRow("[TrackInfo]", 0x20, &bytes[0x20]);

    const std::uint32_t d00 = *reinterpret_cast<std::uint32_t*>(&bytes[0x00]);
    const std::uint32_t d04 = *reinterpret_cast<std::uint32_t*>(&bytes[0x04]);
    const std::uint32_t d08 = *reinterpret_cast<std::uint32_t*>(&bytes[0x08]);
    const std::uint32_t d0C = *reinterpret_cast<std::uint32_t*>(&bytes[0x0C]);
    const std::uint32_t d10 = *reinterpret_cast<std::uint32_t*>(&bytes[0x10]);
    const std::uint32_t d14 = *reinterpret_cast<std::uint32_t*>(&bytes[0x14]);
    const std::uint32_t d18 = *reinterpret_cast<std::uint32_t*>(&bytes[0x18]);
    const std::uint32_t d1C = *reinterpret_cast<std::uint32_t*>(&bytes[0x1C]);
    const std::uint32_t d20 = *reinterpret_cast<std::uint32_t*>(&bytes[0x20]);
    const std::uint32_t d24 = *reinterpret_cast<std::uint32_t*>(&bytes[0x24]);
    const std::uint32_t d28 = *reinterpret_cast<std::uint32_t*>(&bytes[0x28]);
    const std::uint32_t d2C = *reinterpret_cast<std::uint32_t*>(&bytes[0x2C]);
    const std::uint32_t d30 = *reinterpret_cast<std::uint32_t*>(&bytes[0x30]);
    const std::uint32_t d34 = *reinterpret_cast<std::uint32_t*>(&bytes[0x34]);

    Log(
        "[TrackInfo] dwords"
        " +00=%08X"
        " +04=%08X"
        " +08=%08X"
        " +0C=%08X"
        " +10=%08X"
        " +14=%08X"
        " +18=%08X"
        " +1C=%08X"
        " +20=%08X"
        " +24=%08X"
        " +28=%08X"
        " +2C=%08X"
        " +30=%08X"
        " +34=%08X\n",
        static_cast<unsigned int>(d00),
        static_cast<unsigned int>(d04),
        static_cast<unsigned int>(d08),
        static_cast<unsigned int>(d0C),
        static_cast<unsigned int>(d10),
        static_cast<unsigned int>(d14),
        static_cast<unsigned int>(d18),
        static_cast<unsigned int>(d1C),
        static_cast<unsigned int>(d20),
        static_cast<unsigned int>(d24),
        static_cast<unsigned int>(d28),
        static_cast<unsigned int>(d2C),
        static_cast<unsigned int>(d30),
        static_cast<unsigned int>(d34));
}

// Hook for SoundMusicPlayer::GetTrackInfoByName
// Params: thisPtr, trackNameStrCode
static void* __fastcall hkGetTrackInfoByName(void* thisPtr, std::int32_t trackNameStrCode)
{
    void* result = nullptr;

    if (g_OrigGetTrackInfoByName)
    {
        result = g_OrigGetTrackInfoByName(thisPtr, trackNameStrCode);
    }

    Log(
        "[TrackInfo] GetTrackInfoByName this=%p strCode=%08X result=%p\n",
        thisPtr,
        static_cast<unsigned int>(trackNameStrCode),
        result);

    if (result)
    {
        DumpTrackInfoRecord(result);
    }

    return result;
}

// Installs the hook.
// Params: none
bool Install_SoundMusicPlayer_GetTrackInfoByName_Hook()
{
    void* target = ResolveGameAddress(ABS_GetTrackInfoByName);
    if (!target)
    {
        Log("[Hook] SoundMusicPlayer::GetTrackInfoByName: address resolve failed\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkGetTrackInfoByName),
        reinterpret_cast<void**>(&g_OrigGetTrackInfoByName));

    Log("[Hook] SoundMusicPlayer::GetTrackInfoByName: %s\n", ok ? "OK" : "FAIL");
    return ok;
}

// Removes the hook.
// Params: none
bool Uninstall_SoundMusicPlayer_GetTrackInfoByName_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(ABS_GetTrackInfoByName));
    g_OrigGetTrackInfoByName = nullptr;
    return true;
}