#include <pch.h>
#include "BaseControllerImpl_CompletePlatformDeck.h"

#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using CompletePlatformDeck_t = void(__fastcall*)(std::uint8_t* self, std::uint8_t platform, bool baseGradeUp);

    constexpr std::size_t kSelf_PlatformStates = 0x6638;   // one u16 per platform (base * 7 + type), deck in the top 4 bits
    constexpr unsigned    kDeckShift           = 12;
    constexpr unsigned    kFirstDeck           = 1;

    const std::uint8_t kEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
                                    0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x81, 0x38, 0x66, 0x00, 0x00, 0x0F,
                                    0xB6, 0xEA, 0x44, 0x0F, 0xB6, 0xCA, 0x46, 0x0F, 0xB7, 0x34, 0x48 };

    CompletePlatformDeck_t g_Orig   = nullptr;
    void*                  g_Target = nullptr;
    thread_local bool      t_FirstDeck = false;

    void __fastcall hkCompletePlatformDeck(std::uint8_t* self, std::uint8_t platform, bool baseGradeUp)
    {
        const auto* states = *reinterpret_cast<const std::uint16_t* const*>(self + kSelf_PlatformStates);
        const unsigned deck = (states[platform] >> kDeckShift) & 0xF;
        LogDebug("IdroidLines: platform %u completed deck %u", platform, deck);
        t_FirstDeck = deck == kFirstDeck;
        g_Orig(self, platform, baseGradeUp);
        t_FirstDeck = false;
    }
}

bool Install_BaseControllerImpl_CompletePlatformDeck_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.BaseControllerImpl_CompletePlatformDeck)
    {
        LogError("IdroidLines: no platform address for this build, every platform deck keeps the vanilla line");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.BaseControllerImpl_CompletePlatformDeck);
    const HookEntry state = CheckHookEntry(target, kEntry, sizeof(kEntry));
    if (state == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("IdroidLines: BaseControllerImpl::CompletePlatformDeck is not the code we know (%s), every platform deck keeps the vanilla line", found);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("IdroidLines: BaseControllerImpl::CompletePlatformDeck", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkCompletePlatformDeck), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("IdroidLines: BaseControllerImpl::CompletePlatformDeck hook failed at %p, every platform deck keeps the vanilla line", target);
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_BaseControllerImpl_CompletePlatformDeck_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}

bool IsCompletingFirstPlatformDeck()
{
    return t_FirstDeck;
}
