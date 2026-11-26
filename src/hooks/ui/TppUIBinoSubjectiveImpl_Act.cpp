#include <pch.h>
#include "TppUIBinoSubjectiveImpl_Act.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using Act_t                       = void(__fastcall*)(void* bino);
    using GetButtonNumber_t           = std::uint32_t(__fastcall*)(void* uix, std::uint64_t action, bool convert);
    using GetAssignedButtonNumber_t   = std::uint32_t(__fastcall*)(void* utility, std::uint64_t action);
    using GetPadButtonUvShift_t       = bool(__fastcall*)(void* utility, float* u, float* v, std::uint8_t kind, std::uint32_t padButton);
    using SetPadKeyboardIconTexture_t = void(__fastcall*)(void* setter, void* mesh, std::uint32_t icon, float u, float v);

    constexpr std::uint8_t kActEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55, 0x48, 0x8B, 0xEC, 0x48, 0x83,
        0xEC, 0x50, 0x0F, 0xB6, 0x81, 0x33, 0x09, 0x00, 0x00, 0x48, 0x8B, 0xF9,
    };

    constexpr std::size_t kBinoOwner     = 0x28;    // the act sets every prompt icon through owner + 0x48
    constexpr std::size_t kBinoUtility   = 0x38;
    constexpr std::size_t kBinoUix       = 0x50;
    constexpr std::size_t kBinoRadioMesh = 0x240;   // the Intel radio line's icon
    constexpr std::size_t kOwnerSetter   = 0x48;

    constexpr std::size_t kGetButtonNumberSlot     = 0x18;
    constexpr std::size_t kGetPadButtonUvShiftSlot = 0x4D0;
    constexpr std::size_t kSetPadKeyboardIconSlot  = 0x8C8;

    constexpr std::uint64_t kBinoRadioAction   = 0x1E72A38342E5ull;   // what the act asks for the Intel radio line: a name with a key and no pad button
    constexpr std::uint32_t kBinoRadioIcon     = 0x1264D109u;         // UI_BINO_RADIO
    constexpr std::uint64_t kInterrogateAction = 0xADA7E3DF52D4ull;   // the two names the game answers from the pad itself (PL_CQC_INTERROGATE, PL_CALL);
    constexpr std::uint64_t kCallAction        = 0xF86AE002CF22ull;   // the Intel radio shares its key with both, so it is the same button
    constexpr std::uint32_t kNoPicture         = 0x10;                // GetPadButtonUvShift only has cells for buttons 0x0 - 0xF
    constexpr float kRadioIconShiftU = -0.375f;   // the act's own offsets: the cell the bino icon meshes are authored at
    constexpr float kRadioIconShiftV = -0.5f;

    Act_t                     g_Orig          = nullptr;
    GetAssignedButtonNumber_t g_GetAssigned   = nullptr;
    void*                     g_HookTarget    = nullptr;

    template <typename T> T At(void* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset);
    }

    void* Virtual(void* object, std::size_t slot)
    {
        return *reinterpret_cast<void**>(*reinterpret_cast<std::uint8_t**>(object) + slot);
    }

    void SetRadioIconSEH(void* bino)
    {
        __try
        {
            void* uix     = At<void*>(bino, kBinoUix);
            void* utility = At<void*>(bino, kBinoUtility);
            void* owner   = At<void*>(bino, kBinoOwner);
            void* mesh    = At<void*>(bino, kBinoRadioMesh);
            if (!uix || !utility || !owner || !mesh)
                return;
            void* setter = At<void*>(owner, kOwnerSetter);
            if (!setter)
                return;

            if (reinterpret_cast<GetButtonNumber_t>(Virtual(uix, kGetButtonNumberSlot))(uix, kBinoRadioAction, true) < kNoPicture)
                return;

            std::uint32_t button = g_GetAssigned(utility, kInterrogateAction);
            if (button >= kNoPicture)
                button = g_GetAssigned(utility, kCallAction);
            if (button >= kNoPicture)
            {
                LogWarnOnce("BinoRadioIcon: the pad has no button for the Intel radio, its prompt keeps the game's icon");
                return;
            }

            float u = 0.0f;
            float v = 0.0f;
            if (!reinterpret_cast<GetPadButtonUvShift_t>(Virtual(utility, kGetPadButtonUvShiftSlot))(utility, &u, &v, 0, button))
                return;
            reinterpret_cast<SetPadKeyboardIconTexture_t>(Virtual(setter, kSetPadKeyboardIconSlot))
                (setter, mesh, kBinoRadioIcon, u + kRadioIconShiftU, v + kRadioIconShiftV);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hk_Act(void* bino)
    {
        g_Orig(bino);
        if (bino)
            SetRadioIconSEH(bino);
    }
}

bool Install_TppUIBinoSubjectiveImpl_Act_Hook()
{
    if (g_HookTarget)
        return true;

    void* target = ResolveGameAddress(gAddr.TppUIBinoSubjectiveImpl_Act);
    g_GetAssigned = reinterpret_cast<GetAssignedButtonNumber_t>(ResolveGameAddress(gAddr.UiUtilityImpl_GetAssignedButtonNumber));
    if (!target || !g_GetAssigned)
    {
        LogError("BinoRadioIcon: no address on %s, the Intel radio prompt keeps the game's icon", GetGameBuildName(gGameBuild));
        return false;
    }

    const HookEntry entry = CheckHookEntry(target, kActEntry, sizeof(kActEntry));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("BinoRadioIcon: the binocular act is not the known code on %s (%s), the Intel radio prompt keeps the game's icon",
                 GetGameBuildName(gGameBuild), found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("BinoRadioIcon: binocular act", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_Act), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BinoRadioIcon: binocular act hook failed at %p, the Intel radio prompt keeps the game's icon", target);
        return false;
    }
    g_HookTarget = target;
    return true;
}

void Uninstall_TppUIBinoSubjectiveImpl_Act_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
