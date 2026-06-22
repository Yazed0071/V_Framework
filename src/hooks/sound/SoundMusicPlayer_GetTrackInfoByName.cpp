#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "HookUtils.h"
#include "log.h"
#include "AddressSet.h"

namespace
{


    using GetTrackInfoByName_t = void* (__fastcall*)(void* thisPtr, std::int32_t trackNameStrCode);

    static GetTrackInfoByName_t g_OrigGetTrackInfoByName = nullptr;
}


static void LogHexRow(const char* prefix, std::size_t baseOffset, const std::uint8_t* rowBytes)
{
}


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
}


static void* __fastcall hkGetTrackInfoByName(void* thisPtr, std::int32_t trackNameStrCode)
{
    void* result = nullptr;

    if (g_OrigGetTrackInfoByName)
    {
        result = g_OrigGetTrackInfoByName(thisPtr, trackNameStrCode);
    }

    if (result)
    {
        DumpTrackInfoRecord(result);
    }

    return result;
}


bool Install_SoundMusicPlayer_GetTrackInfoByName_Hook()
{
    void* target = ResolveGameAddress(gAddr.GetTrackInfoByName);
    if (!target)
    {
        Log("[TrackInfo] ERROR: GetTrackInfoByName address unavailable for this build — cassette track lookups by name will not work.\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkGetTrackInfoByName),
        reinterpret_cast<void**>(&g_OrigGetTrackInfoByName));

    if (!ok)
        Log("[TrackInfo] ERROR: failed to hook GetTrackInfoByName — cassette track lookups by name will not work.\n");
    return ok;
}


bool Uninstall_SoundMusicPlayer_GetTrackInfoByName_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.GetTrackInfoByName));
    g_OrigGetTrackInfoByName = nullptr;
    return true;
}