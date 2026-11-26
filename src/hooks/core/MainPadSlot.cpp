#include <pch.h>
#include "MainPadSlot.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../ui/ButtonIconSheet.h"

namespace
{
    using UpdateSystem_t      = void(__fastcall*)();
    using PadGetInstance_t    = void* (__fastcall*)();
    using GetPhysicalPadNum_t = int(__fastcall*)(int logicalPad);
    using DevPadGetInstance_t = void** (__fastcall*)(unsigned int index);
    using XInputGetState_t    = DWORD(WINAPI*)(DWORD user, void* state);

    constexpr std::uint8_t kUpdateSystemEntry[] =
    {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x33, 0xDB, 0x85, 0xD2,
        0x74, 0x58, 0x48, 0x89, 0x7C, 0x24, 0x40,
    };
    constexpr std::uint8_t kUpdateSystemMask[] =
    {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };

    constexpr std::size_t kPadListeners  = 0x30;   // the pad holds one listener pointer per physical slot
    constexpr std::size_t kListenerDevPad = 0x0A;  // which hid::DevPad the listener reads
    constexpr std::size_t kRecordVendor   = 0x10;
    constexpr std::size_t kRecordProduct  = 0x12;
    constexpr std::size_t kRecordDetail   = 0x18;
    constexpr int kPadCount = 4;
    constexpr unsigned int kMaxDevPads = 16;

    constexpr std::uint16_t kVendorMicrosoft = 0x045E;
    constexpr std::uint16_t kVendorSony      = 0x054C;

    constexpr unsigned int kXInputUsers     = 4;
    constexpr ULONGLONG    kXInputRecheckMs = 500;   // asking about an absent XInput user is slow, so not every tick

    struct XInputUser
    {
        ULONGLONG checkedAt = 0;
        bool      connected = false;
    };

    UpdateSystem_t   g_Orig           = nullptr;
    void*            g_HookTarget     = nullptr;
    XInputGetState_t g_XInputGetState = nullptr;
    XInputUser       g_XInputUsers[kXInputUsers];

    std::uint8_t* RecordOf(DevPadGetInstance_t getDevPad, unsigned int index)
    {
        void** entry = getDevPad(index);
        return entry ? static_cast<std::uint8_t*>(*entry) : nullptr;
    }

    bool XInputFeeds(unsigned int index)   // the engine reads XInput user N into DevPad N, whatever else that DevPad holds
    {
        if (!g_XInputGetState || index >= kXInputUsers)
            return false;
        XInputUser& user = g_XInputUsers[index];
        const ULONGLONG now = GetTickCount64();
        if (user.checkedAt && now - user.checkedAt < kXInputRecheckMs)
            return user.connected;
        std::uint8_t state[16] = {};
        user.connected = g_XInputGetState(index, state) == ERROR_SUCCESS;
        user.checkedAt = now;
        return user.connected;
    }

    bool IsGamepad(DevPadGetInstance_t getDevPad, unsigned int index)
    {
        if (XInputFeeds(index))
            return true;
        auto* record = RecordOf(getDevPad, index);
        if (!record || !*reinterpret_cast<void**>(record + kRecordDetail))
            return false;
        const std::uint16_t vendor = *reinterpret_cast<std::uint16_t*>(record + kRecordVendor);
        return vendor == kVendorMicrosoft || vendor == kVendorSony;
    }

    bool HoldsNoDevice(DevPadGetInstance_t getDevPad, unsigned int index)
    {
        auto* record = RecordOf(getDevPad, index);
        return record && !*reinterpret_cast<void**>(record + kRecordDetail);
    }

    void PointMainPadAtAGamepadSEH()
    {
        __try
        {
            auto getPad = reinterpret_cast<PadGetInstance_t>(ResolveGameAddress(gAddr.Pad_GetInstance));
            auto physicalPadNum = reinterpret_cast<GetPhysicalPadNum_t>(ResolveGameAddress(gAddr.PadMapping_GetPhysicalPadNum));
            auto getDevPad = reinterpret_cast<DevPadGetInstance_t>(ResolveGameAddress(gAddr.DevPad_GetInstance));
            if (!getPad || !physicalPadNum || !getDevPad)
                return;

            auto* instance = static_cast<std::uint8_t*>(getPad());
            const int slot = physicalPadNum(0);
            if (!instance || slot < 0 || slot >= kPadCount)
                return;
            auto* listener = *reinterpret_cast<std::uint8_t**>(instance + kPadListeners + slot * sizeof(void*));
            if (!listener)
                return;

            auto& devPad = *reinterpret_cast<std::uint16_t*>(listener + kListenerDevPad);
            if (IsGamepad(getDevPad, devPad))
                return;

            unsigned int count = 0;
            while (count < kMaxDevPads && getDevPad(count))
                ++count;

            for (unsigned int index = 0; index < count; ++index)
            {
                if (!IsGamepad(getDevPad, index) || index == devPad)
                    continue;
                auto* record = RecordOf(getDevPad, index);
                if (XInputFeeds(index))
                    LogInfo("MainPadSlot: pad 1 was reading device %u, which is not a controller; moved it to device %u "
                            "(XInput controller %u)", devPad, index, index + 1);
                else
                    LogInfo("MainPadSlot: pad 1 was reading device %u, which is not a controller; moved it to device %u "
                            "(vendor 0x%04X product 0x%04X)", devPad, index,
                            *reinterpret_cast<std::uint16_t*>(record + kRecordVendor),
                            *reinterpret_cast<std::uint16_t*>(record + kRecordProduct));
                devPad = static_cast<std::uint16_t>(index);
                return;
            }

            if (HoldsNoDevice(getDevPad, devPad))
                return;

            for (unsigned int index = 0; index < count; ++index)
            {
                if (!HoldsNoDevice(getDevPad, index))
                    continue;
                LogInfo("MainPadSlot: no controller among the %u input devices; parked pad 1 on the empty device %u "
                        "so device %u cannot drive it", count, index, devPad);
                devPad = static_cast<std::uint16_t>(index);
                return;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hk_UpdateSystem()
    {
        g_Orig();
        PointMainPadAtAGamepadSEH();
        buttoniconsheet::FollowPad();
    }
}

bool Install_MainPadSlot_Hook()
{
    if (g_HookTarget)
        return true;

    void* target = ResolveGameAddress(gAddr.DevPad_UpdateSystem);
    if (!target)
    {
        LogError("MainPadSlot: no address on %s, pad 1 keeps whatever device the game gives it",
                 GetGameBuildName(gGameBuild));
        return false;
    }

    const HookEntry entry = CheckHookEntryMasked(target, kUpdateSystemEntry, kUpdateSystemMask,
                                                 sizeof(kUpdateSystemEntry));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("MainPadSlot: the pad update is not the known code on %s (%s), pad 1 keeps whatever device "
                 "the game gives it", GetGameBuildName(gGameBuild), found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("MainPadSlot: pad update", target);
    if (const HMODULE xinput = GetModuleHandleW(L"XINPUT1_3.dll"))   // the game's own import, so Steam Input's pad shows up here too
    {
        g_XInputGetState = reinterpret_cast<XInputGetState_t>(GetProcAddress(xinput, "XInputGetState"));
        if (!g_XInputGetState)
            g_XInputGetState = reinterpret_cast<XInputGetState_t>(GetProcAddress(xinput, MAKEINTRESOURCEA(2)));   // the ordinal the game imports
    }
    if (!g_XInputGetState)
        LogWarn("MainPadSlot: XInput is not loaded, a controller that only reaches the game through XInput is not recognised");
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_UpdateSystem), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("MainPadSlot: pad update hook failed at %p, pad 1 keeps whatever device the game gives it", target);
        return false;
    }
    g_HookTarget = target;
    return true;
}

void Uninstall_MainPadSlot_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
