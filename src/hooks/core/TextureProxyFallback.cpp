#include <pch.h>
#include "TextureProxyFallback.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "NativeCallPatch.h"
#include "log.h"

namespace
{
    constexpr std::size_t kResultSite = 0x65;   // right after TextureResource_Load asks the streamer for a proxy
    constexpr std::size_t kPatchSize  = 6;

    const std::uint8_t kVanilla[] = {
        0x45, 0x33, 0xF6,                     // xor r14d, r14d
        0x83, 0xF8, 0xFF,                     // cmp eax, -1
        0x41, 0x0F, 0x95, 0xC6,               // setne r14b
        0x41, 0x8D, 0x46, 0xFF,               // lea eax, [r14 - 1]
        0xE9, 0x57, 0x04, 0x00,               // jmp to the end; the rel32's last byte stays as it is
    };
    const std::uint8_t kFallback[] = {
        0x83, 0xF8, 0xFF,                     // cmp eax, -1
        0x74, 0x0E,                           // je to the load the game uses with streaming off
        0x33, 0xC0,                           // xor eax, eax
        0xE9, 0x5E, 0x04, 0x00, 0x00,         // jmp to the end
        0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00,   // nop; its last byte is the 00 left in place
    };
    static_assert(sizeof(kVanilla) == sizeof(kFallback) && sizeof(kVanilla) % kPatchSize == 0, "same-length patch in 6-byte parts");
    constexpr std::size_t kParts = sizeof(kVanilla) / kPatchSize;

    NativeCodePatch g_Patches[kParts]{};
    std::uint8_t*   g_Site = nullptr;
}

bool Install_TextureProxyFallback()
{
    auto* fn = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.TextureResource_Load));
    if (!fn)
    {
        LogError("TextureProxyFallback: no TextureResource_Load address on %s, streamed textures past the 4880 proxies stay grey",
                 GetGameBuildName(gGameBuild));
        return false;
    }

    std::uint8_t* site = fn + kResultSite;
    for (std::size_t i = 0; i < kParts; ++i)
    {
        g_Patches[i] = NativeCodePatch{};
        g_Patches[i].target = site + i * kPatchSize;
        g_Patches[i].size = static_cast<unsigned char>(kPatchSize);
        std::memcpy(g_Patches[i].expected, kVanilla + i * kPatchSize, kPatchSize);
        std::memcpy(g_Patches[i].replacement, kFallback + i * kPatchSize, kPatchSize);
    }

    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (InstallNativeCodePatches(g_Patches, kParts, nullptr, site, site + sizeof(kVanilla) + 1))
        {
            g_Site = site;
            LogInfo("TextureProxyFallback: a streamed texture that gets no streamer proxy now loads whole instead of staying grey");
            return true;
        }
        if (!NativePatchErrorIsTransient(GetLastError()))
            break;
        Sleep(4);
    }
    LogError("TextureProxyFallback: TextureResource_Load patch refused (%s), streamed textures past the 4880 proxies stay grey",
             NativePatchErrorName(GetLastError()));
    return false;
}

void Uninstall_TextureProxyFallback()
{
    if (!g_Site)
        return;
    RemoveNativeCodePatches(g_Patches, kParts, nullptr, g_Site, g_Site + sizeof(kVanilla) + 1);
    g_Site = nullptr;
}
