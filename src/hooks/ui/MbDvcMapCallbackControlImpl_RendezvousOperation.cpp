#include <pch.h>
#include "MbDvcMapCallbackControlImpl_RendezvousOperation.h"

#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "FeatureSwitch.h"
#include "HookUtils.h"
#include "MbDvcSoundControllerImpl_Update.h"
#include "log.h"

namespace
{
    using RendezvousOperation_t = void(__fastcall*)(std::uint8_t* self);

    constexpr std::size_t  kStep        = 0x120;
    constexpr std::uint8_t kStepOpening = 0;   // sends MbDvcActOpenHeliCall, asks for voice 3, goes to 1
    constexpr std::uint8_t kStepPicking = 1;   // a confirmed RV point calls the heli, asks for voice 4, goes to 3
    constexpr std::uint8_t kStepCalled  = 3;

    constexpr std::uint8_t  kVoiceOnOpening      = 3;   // the change-location map asks for it too
    constexpr std::uint8_t  kVoiceOnCall         = 4;
    constexpr std::uint32_t kLineSelectRvPoint   = 1184166798;   // "Please select an RV point."
    constexpr std::uint32_t kLineRvPointConfirmed = 1184166796;  // "RV point confirmed."

    const std::uint8_t kEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x0F, 0xB6, 0x89,
                                    0x20, 0x01, 0x00, 0x00, 0x85, 0xC9, 0x0F, 0x84, 0x04, 0x02, 0x00, 0x00, 0xFF, 0xC9, 0x74, 0x67,
                                    0x83, 0xF9, 0x02 };   // movzx ecx, byte [rcx+0x120], then steps 0, 1 and 3

    RendezvousOperation_t g_Orig   = nullptr;
    void*                 g_Target = nullptr;

    // the heli pickup map of the iDroid, every frame it is open
    void __fastcall hkRendezvousOperation(std::uint8_t* self)
    {
        const std::uint8_t before = self[kStep];
        g_Orig(self);
        const std::uint8_t after = self[kStep];
        if (before == after || !FeatureSwitch::IsOn(FeatureSwitch::Feature::RestoredIdroidLines))
            return;
        if (before == kStepOpening && after == kStepPicking)
            IdroidVoice_OfferTake(kVoiceOnOpening, kLineSelectRvPoint);
        else if (before == kStepPicking && after == kStepCalled)
            IdroidVoice_OfferTake(kVoiceOnCall, kLineRvPointConfirmed);
    }
}

bool Install_MbDvcMapCallbackControlImpl_RendezvousOperation_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.MbDvcMapCallbackControlImpl_RendezvousOperation)
    {
        LogError("IdroidLines: no heli pickup map address for this build, the two RV point lines stay silent");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.MbDvcMapCallbackControlImpl_RendezvousOperation);
    const HookEntry state = CheckHookEntry(target, kEntry, sizeof(kEntry));
    if (state == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("IdroidLines: MbDvcMapCallbackControlImpl::RendezvousOperation is not the code we know (%s), the two RV point lines stay silent", found);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("IdroidLines: MbDvcMapCallbackControlImpl::RendezvousOperation", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkRendezvousOperation), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("IdroidLines: MbDvcMapCallbackControlImpl::RendezvousOperation hook failed at %p, the two RV point lines stay silent", target);
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_MbDvcMapCallbackControlImpl_RendezvousOperation_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
