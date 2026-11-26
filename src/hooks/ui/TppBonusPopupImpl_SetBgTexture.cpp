#include <pch.h>
#include "TppBonusPopupImpl_SetBgTexture.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "MissionTextureTable.h"
#include "UiTexturePrefetch.h"

namespace
{
    using SetBgTexture_t = void(__fastcall*)(void* bonusPopup, bool showLogo, uint64_t photoTexture);

    constexpr std::uint8_t kSetBgTextureEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0x81, 0x86,
        0x03, 0x00, 0x00, 0x49, 0x8B, 0xF8, 0x48, 0x8B, 0xD9, 0x84, 0xD2, 0x74, 0x4E,
    };

    SetBgTexture_t g_OrigSetBgTexture = nullptr;

    MissionTextureTable g_LogoTextures;
    MissionTextureTable g_PhotoTextures;


    void __fastcall hkSetBgTexture(void* bonusPopup, bool showLogo, uint64_t photoTexture)
    {
        const uint32_t mission = MissionCodeGuard::GetCurrentMissionCode();
        const uint64_t customTexture = showLogo ? g_LogoTextures.Resolve(mission) : g_PhotoTextures.Resolve(mission);
        if (customTexture == 0)
        {
            g_OrigSetBgTexture(bonusPopup, showLogo, photoTexture);
            return;
        }

        UiTexturePrefetch::Prefetch(customTexture);
        g_OrigSetBgTexture(bonusPopup, false, customTexture);
    }
}


void BonusPopup_SetLogoTexture(uint64_t textureHash, uint32_t missionCode)
{
    UiTexturePrefetch::Prefetch(textureHash);
    g_LogoTextures.Set(missionCode, textureHash);
}

void BonusPopup_SetPhotoTexture(uint64_t textureHash, uint32_t missionCode)
{
    UiTexturePrefetch::Prefetch(textureHash);
    g_PhotoTextures.Set(missionCode, textureHash);
}

void BonusPopup_ClearTextures(uint32_t missionCode)
{
    g_LogoTextures.Unset(missionCode);
    g_PhotoTextures.Unset(missionCode);
}


bool Install_TppBonusPopupImpl_SetBgTexture_Hook()
{
    void* target = ResolveGameAddress(gAddr.TppBonusPopupImpl_SetBgTexture);
    if (!target)
        return false;

    const HookEntry entry = CheckHookEntry(target, kSetBgTextureEntry, sizeof(kSetBgTextureEntry));
    if (entry == HookEntry::Unknown)
    {
        LogError("BonusPopupBgTexture: SetBgTexture entry bytes differ at %p, custom bonus popup backgrounds off", target);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("BonusPopupBgTexture: SetBgTexture", target);

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkSetBgTexture),
        reinterpret_cast<void**>(&g_OrigSetBgTexture));

    if (!ok)
        LogError("BonusPopupBgTexture: hook failed, custom bonus popup backgrounds off");
    return ok;
}


bool Uninstall_TppBonusPopupImpl_SetBgTexture_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.TppBonusPopupImpl_SetBgTexture));
    g_OrigSetBgTexture = nullptr;
    return true;
}
