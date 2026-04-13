#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using ChangePlayerInfoToForceAvatarFromOriginal_t = void(__fastcall*)(
        void* self,
        void* loadPlayerParam3,
        void* info,
        std::uint32_t playerPartsType,
        std::uint32_t playerType,
        std::uint32_t playerCamoType,
        std::uint32_t playerFaceId,
        std::uint32_t param7
        );

    static ChangePlayerInfoToForceAvatarFromOriginal_t
        g_OrigChangePlayerInfoToForceAvatarFromOriginal = nullptr;

    static bool g_Installed = false;
}

static void __fastcall hkChangePlayerInfoToForceAvatarFromOriginal(
    void* self,
    void* loadPlayerParam3,
    void* info,
    std::uint32_t playerPartsType,
    std::uint32_t playerType,
    std::uint32_t playerCamoType,
    std::uint32_t playerFaceId,
    std::uint32_t param7
)
{
    Log(
        "[AvatarStage] in  partsType=0x%02X playerType=0x%02X camoType=0x%02X faceId=0x%04X param7=0x%X\n",
        static_cast<unsigned>(playerPartsType),
        static_cast<unsigned>(playerType),
        static_cast<unsigned>(playerCamoType),
        static_cast<unsigned>(playerFaceId),
        static_cast<unsigned>(param7)
    );

    g_OrigChangePlayerInfoToForceAvatarFromOriginal(
        self,
        loadPlayerParam3,
        info,
        playerPartsType,
        playerType,
        playerCamoType,
        playerFaceId,
        param7
    );

    auto* p = reinterpret_cast<std::uint8_t*>(self);

    const std::uint32_t stagedPlayerType = *reinterpret_cast<std::uint32_t*>(p + 0x23C);
    const std::uint32_t stagedCamoType = *reinterpret_cast<std::uint32_t*>(p + 0x240);
    const std::uint32_t stagedPartsType = *reinterpret_cast<std::uint32_t*>(p + 0x244);
    const std::uint16_t stagedFaceId = *reinterpret_cast<std::uint16_t*>(p + 0x248);
    const std::uint8_t  stagedState = *(p + 0x27C);

    Log(
        "[AvatarStage] out stagedType=0x%02X stagedCamo=0x%02X stagedParts=0x%02X stagedFace=0x%04X state=0x%02X\n",
        static_cast<unsigned>(stagedPlayerType),
        static_cast<unsigned>(stagedCamoType),
        static_cast<unsigned>(stagedPartsType),
        static_cast<unsigned>(stagedFaceId),
        static_cast<unsigned>(stagedState)
    );
}

bool Install_ChangePlayerInfoToForceAvatarFromOriginal_Hook()
{
    if (g_Installed)
        return true;

    void* target = ResolveGameAddress(0x14625A170ull);
    if (!target)
    {
        Log("[Hook] AvatarStage: failed to resolve target\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkChangePlayerInfoToForceAvatarFromOriginal),
        reinterpret_cast<void**>(&g_OrigChangePlayerInfoToForceAvatarFromOriginal)
    );

    Log("[Hook] AvatarStage: %s\n", ok ? "OK" : "FAIL");

    g_Installed = ok;
    return ok;
}

bool Uninstall_ChangePlayerInfoToForceAvatarFromOriginal_Hook()
{
    if (!g_Installed)
        return true;

    if (void* target = ResolveGameAddress(0x14625A170ull))
        DisableAndRemoveHook(target);

    g_OrigChangePlayerInfoToForceAvatarFromOriginal = nullptr;
    g_Installed = false;

    Log("[Hook] AvatarStage: removed\n");
    return true;
}