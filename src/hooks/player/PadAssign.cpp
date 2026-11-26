#include <pch.h>
#include "PadAssign.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "V_FrameWorkState.h"
#include "log.h"
#include "../equip/LaserSightSwitch.h"
#include "../ui/TextIcons.h"

namespace
{
    using Update_t              = void (__fastcall*)(std::uint8_t* pad);
    using SetPadMappings_t      = void (__fastcall*)(std::uint8_t* pad, std::uint32_t controlType);
    using GetQuarkSystemTable_t = std::uint8_t* (__fastcall*)();
    using SetAssignedButton_t   = void (__fastcall*)(void* buttonMapper, std::uint32_t id, std::uint32_t button);
    using SetPad3Button_t       = void (__fastcall*)(void* pad3, std::uint32_t row, std::uint32_t button);
    using GetPad3Button_t       = std::uint32_t (__fastcall*)(void* pad3, std::uint32_t row);

    constexpr std::uint8_t kUpdateEntry[] =
    {
        0x48, 0x8B, 0xC4, 0x56, 0x41, 0x54, 0x48, 0x81, 0xEC, 0xB8, 0x00, 0x00, 0x00, 0x0F, 0x29, 0x70,
        0xC8, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68,
    };
    constexpr std::uint8_t kSetPadMappingsEntry[] =
    {
        0x40, 0x55, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x28, 0x48,
        0x8B, 0x81, 0x80, 0x02, 0x00, 0x00, 0x48, 0x8B,
    };

    constexpr int kTableRows = 26;   // one row per PlayerPad bit, the game's own count

    constexpr std::size_t   kPadPad3        = 0x008;   // the fox::gk::impl::Pad3 that holds one button per row
    constexpr std::size_t   kPadFlags       = 0x4B0;
    constexpr std::uint8_t  kFlagVehicle    = 0x02;    // riding a vehicle: the game puts the marker row on L3
    constexpr std::uint8_t  kFlagArmoured   = 0x04;    // an armoured vehicle or a tank: it also puts the radio row on X, or on Y
    constexpr std::uint8_t  kFlagLoadTable  = 0x10;    // Update loads the table again and clears this
    constexpr std::size_t   kPad3SetButton  = 0x40 / sizeof(void*);
    constexpr std::size_t   kPad3GetButton  = 0x48 / sizeof(void*);
    constexpr std::uint32_t kRowCall        = 9;
    constexpr std::uint32_t kRowPlaceMarker = 0x16;
    constexpr int           kButtonX        = 0;
    constexpr int           kButtonY        = 3;
    constexpr int           kButtonSelect   = 9;
    constexpr int           kButtonL3       = 10;
    constexpr int           kButtonDpadUp   = 12;      // 12 to 15 are the D-pad

    constexpr std::size_t kConfigPath[]      = { 0x98, 0x10 };   // quark system table -> ... -> the game config's data
    constexpr std::size_t kConfigControlType = 0x58;

    constexpr const char* kSaveSpace       = "PadAssign";
    constexpr int         kSaveParts       = 3;                   // a Control Type's 16 buttons, 4 bits each, in three numbers
    constexpr char        kSavePart[]      = { 'A', 'B', 'C' };
    constexpr int         kSavePartFirst[] = { 0, 6, 12 };
    constexpr int         kSavePartCount[] = { 6, 6, 4 };
    constexpr ULONGLONG   kNameRetryMs     = 250;
    constexpr int         kMaxUiPairs      = 64;

    struct NameState
    {
        const char*   saveName;
        std::uint32_t action;
        int           gameButton = padassign::kNoButton;   // what its feature registered
        int           wanted     = padassign::kNoButton;
    };

    struct SeenPad
    {
        std::atomic<std::uint8_t*> pad{ nullptr };
        std::atomic<std::uint32_t> change{ 0 };
    };

    struct VehicleMove
    {
        void*           pad3 = nullptr;
        SetPad3Button_t set = nullptr;
        GetPad3Button_t get = nullptr;
        void*           buttonMapper = nullptr;
        bool            marker = false;
        bool            call = false;
        std::uint8_t    markerButton = 0;
        std::uint8_t    callButton = 0;
    };

    struct SavedType
    {
        bool         changed = false;
        std::int32_t packed[kSaveParts] = {};
    };

    Update_t              g_OrigUpdate           = nullptr;
    SetPadMappings_t      g_OrigSetPadMappings   = nullptr;
    void*                 g_UpdateTarget         = nullptr;
    void*                 g_SetPadMappingsTarget = nullptr;
    GetQuarkSystemTable_t g_GetQuarkSystemTable  = nullptr;
    SetAssignedButton_t   g_SetAssignedButton    = nullptr;
    void**                g_ButtonMapperSlot     = nullptr;
    const std::uint32_t*  g_UiPairs              = nullptr;   // (id, row) pairs the game tells the UI after loading a table
    std::uintptr_t        g_GameImageBegin       = 0;
    std::uintptr_t        g_GameImageEnd         = 0;

    std::mutex                 g_Lock;   // every change of the layout
    std::uint8_t               g_Button[padassign::kControlTypeCount][padassign::kButtonCount];
    const std::int32_t**       g_TableSlots = nullptr;                                   // the game's four table pointers
    const std::int32_t*        g_GameTable[padassign::kControlTypeCount] = {};
    alignas(16) std::int32_t   g_OurTable[padassign::kControlTypeCount][kTableRows] = {};
    std::atomic<bool>          g_Active{ false };                                        // the game reads our tables
    std::atomic<std::uint32_t> g_Change{ 1 };
    SeenPad                    g_SeenPads[16];
    std::atomic<std::uint32_t> g_NextSeenPad{ 0 };

    NameState g_Names[padassign::kNameCount] =
    {
        { "UI_LASER_SWITCH", LaserSightSwitch::kAimAction },
        { "UI_EQUIP_OP_3",   LaserSightSwitch::kMenuAction },
    };
    std::atomic<bool>      g_NamesSettled{ false };
    std::atomic<ULONGLONG> g_NameLastTry{ 0 };

    bool InGameImage(const void* address)
    {
        const auto value = reinterpret_cast<std::uintptr_t>(address);
        return value >= g_GameImageBegin && value < g_GameImageEnd;
    }

    bool IsControlType(int controlType)
    {
        return g_Active.load(std::memory_order_acquire) && controlType >= 0 && controlType < padassign::kControlTypeCount;
    }

    void RebuildTable(int controlType)
    {
        if (!g_GameTable[controlType])
            return;
        for (int row = 0; row < kTableRows; ++row)
        {
            const std::int32_t game = g_GameTable[controlType][row];
            g_OurTable[controlType][row] = game >= 0 && game < padassign::kButtonCount ? g_Button[controlType][game] : game;
        }
    }

    bool IsFullSet(const std::uint8_t* buttons)
    {
        bool used[padassign::kButtonCount] = {};
        for (int game = 0; game < padassign::kButtonCount; ++game)
        {
            if (buttons[game] >= padassign::kButtonCount || used[buttons[game]])
                return false;
            used[buttons[game]] = true;
        }
        return true;
    }

    void SaveName(char* out, std::size_t size, int controlType, int part)
    {
        std::snprintf(out, size, "Type%d%c", controlType, kSavePart[part]);
    }

    void PackType(int controlType, SavedType& saved)
    {
        const std::uint8_t* buttons = g_Button[controlType];
        bool gameLayout = true;
        for (int game = 0; game < padassign::kButtonCount; ++game)
            gameLayout = gameLayout && buttons[game] == game;
        saved.changed = true;
        for (int part = 0; part < kSaveParts; ++part)
        {
            saved.packed[part] = 0;   // 0 in all three: the game's layout, nothing kept in the file
            if (!gameLayout)
                for (int i = 0; i < kSavePartCount[part]; ++i)
                    saved.packed[part] |= static_cast<std::int32_t>(buttons[kSavePartFirst[part] + i]) << (4 * i);
        }
    }

    void SaveType(int controlType, const SavedType& saved)   // file writes: not under g_Lock
    {
        for (int part = 0; saved.changed && part < kSaveParts; ++part)
        {
            char name[16];
            SaveName(name, sizeof(name), controlType, part);
            V_FrameWorkState::SetPersistedConstant(kSaveSpace, name, saved.packed[part]);
        }
    }

    void LoadType(int controlType)
    {
        std::int32_t packed[kSaveParts] = {};
        bool saved = false;
        for (int part = 0; part < kSaveParts; ++part)
        {
            char name[16];
            SaveName(name, sizeof(name), controlType, part);
            packed[part] = V_FrameWorkState::GetPersistedConstant(kSaveSpace, name);
            saved = saved || packed[part] != 0;
        }
        if (!saved)
            return;
        std::uint8_t buttons[padassign::kButtonCount];
        for (int part = 0; part < kSaveParts; ++part)
            for (int i = 0; i < kSavePartCount[part]; ++i)
                buttons[kSavePartFirst[part] + i] = static_cast<std::uint8_t>((packed[part] >> (4 * i)) & 0xF);
        if (!IsFullSet(buttons))
        {
            LogWarn("PadAssign: the saved buttons of Control Type %d are not a full set, the game's layout is used", controlType);
            return;
        }
        std::memcpy(g_Button[controlType], buttons, sizeof(buttons));
    }

    void LoadSaved()   // also what keeps the state file from dropping our lines as unused
    {
        for (int type = 0; type < padassign::kControlTypeCount; ++type)
        {
            for (int game = 0; game < padassign::kButtonCount; ++game)
                g_Button[type][game] = static_cast<std::uint8_t>(game);
            LoadType(type);
        }
        for (NameState& name : g_Names)
        {
            const std::int32_t saved = V_FrameWorkState::GetPersistedConstant(kSaveSpace, name.saveName);
            name.wanted = saved >= 1 && saved <= padassign::kButtonCount ? saved - 1 : padassign::kNoButton;
        }
    }

    void SaveNameButton(const NameState& name, int wanted)   // a file write: not under g_Lock
    {
        V_FrameWorkState::SetPersistedConstant(kSaveSpace, name.saveName, wanted == padassign::kNoButton ? 0 : wanted + 1);
    }

    int ButtonOfNameLocked(const NameState& name)
    {
        const int button = name.wanted != padassign::kNoButton ? name.wanted : name.gameButton;
        return name.gameButton != padassign::kNoButton && button >= 0 && button < padassign::kButtonCount ? button : padassign::kNoButton;
    }

    bool GiveNameButtonLocked(NameState& name, int button)
    {
        if (name.gameButton == padassign::kNoButton || !TextIcons::SetActionPadButton(name.action, button))
            return false;
        name.wanted = button == name.gameButton ? padassign::kNoButton : button;
        return true;
    }

    int ControlTypeSEH()
    {
        __try
        {
            std::uint8_t* link = g_GetQuarkSystemTable ? g_GetQuarkSystemTable() : nullptr;
            for (const std::size_t offset : kConfigPath)
            {
                if (!link)
                    return -1;
                link = *reinterpret_cast<std::uint8_t**>(link + offset);
            }
            return link ? *reinterpret_cast<const std::int32_t*>(link + kConfigControlType) : -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    bool ReadGameTablesSEH()
    {
        __try
        {
            for (int type = 0; type < padassign::kControlTypeCount; ++type)
            {
                const std::int32_t* table = g_TableSlots[type];
                if (!InGameImage(table) || !InGameImage(table + kTableRows))
                    return false;
                for (int row = 0; row < kTableRows; ++row)
                    if (table[row] != -1 && (table[row] < 0 || table[row] >= padassign::kButtonCount))
                        return false;
                g_GameTable[type] = table;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool UiPairsLookRightSEH(const std::uint32_t* pairs)
    {
        __try
        {
            for (int i = 0; i < kMaxUiPairs; ++i)
            {
                if (pairs[2 * i] == 0)
                    return i > 0;
                if (pairs[2 * i + 1] >= static_cast<std::uint32_t>(kTableRows))
                    return false;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SetTableSlotsSEH(bool ours)
    {
        __try
        {
            for (int type = 0; type < padassign::kControlTypeCount; ++type)
            {
                if (ours)
                    g_TableSlots[type] = g_OurTable[type];
                else if (g_TableSlots[type] == g_OurTable[type] && g_GameTable[type])   // only what is still ours goes back
                    g_TableSlots[type] = g_GameTable[type];
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool LoadedCurrentTable(std::uint8_t* pad)
    {
        const std::uint32_t change = g_Change.load(std::memory_order_acquire);
        for (SeenPad& seen : g_SeenPads)
            if (seen.pad.load(std::memory_order_relaxed) == pad)
                return seen.change.exchange(change, std::memory_order_relaxed) == change;
        SeenPad& seen = g_SeenPads[g_NextSeenPad.fetch_add(1, std::memory_order_relaxed) % (sizeof(g_SeenPads) / sizeof(g_SeenPads[0]))];
        seen.pad.store(pad, std::memory_order_relaxed);
        seen.change.store(change, std::memory_order_relaxed);
        return false;   // a pad not met before loads once more: it may have read a table from before a change
    }

    void AskForTableSEH(std::uint8_t* pad)
    {
        __try
        {
            pad[kPadFlags] |= kFlagLoadTable;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool FindVehicleMoveSEH(const std::uint8_t* pad, std::uint32_t controlType, VehicleMove& move)
    {
        __try
        {
            const std::uint8_t flags = pad[kPadFlags];
            if (!(flags & kFlagVehicle))
                return false;
            const std::uint8_t* ours = g_Button[controlType];
            const int gameCall = (controlType & ~2u) == 0 ? kButtonX : kButtonY;   // the game's own pick, by Control Type
            move.marker = ours[kButtonL3] != kButtonL3;
            move.call   = (flags & kFlagArmoured) && ours[gameCall] != gameCall;
            if (!move.marker && !move.call)
                return false;
            move.markerButton = ours[kButtonL3];
            move.callButton   = ours[gameCall];
            move.pad3 = *reinterpret_cast<void* const*>(pad + kPadPad3);
            void** vtable = move.pad3 ? *reinterpret_cast<void***>(move.pad3) : nullptr;
            if (!InGameImage(vtable) || !InGameImage(vtable[kPad3SetButton]) || !InGameImage(vtable[kPad3GetButton]))
                return false;
            move.set = reinterpret_cast<SetPad3Button_t>(vtable[kPad3SetButton]);
            move.get = reinterpret_cast<GetPad3Button_t>(vtable[kPad3GetButton]);
            move.buttonMapper = g_ButtonMapperSlot ? *g_ButtonMapperSlot : nullptr;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hk_Update(std::uint8_t* pad)
    {
        if (pad && !LoadedCurrentTable(pad))
            AskForTableSEH(pad);
        g_OrigUpdate(pad);
    }

    void __fastcall hk_SetPadMappings(std::uint8_t* pad, std::uint32_t controlType)
    {
        g_OrigSetPadMappings(pad, controlType);   // loads our table, then puts the two vehicle rows on the game's own buttons
        VehicleMove move;
        if (!pad || !g_Active.load(std::memory_order_acquire) || controlType >= padassign::kControlTypeCount
            || !FindVehicleMoveSEH(pad, controlType, move))
            return;
        if (move.marker)
            move.set(move.pad3, kRowPlaceMarker, move.markerButton);
        if (move.call)
            move.set(move.pad3, kRowCall, move.callButton);
        if (!move.buttonMapper || !g_SetAssignedButton || !g_UiPairs)
            return;
        for (const std::uint32_t* pair = g_UiPairs; pair[0]; pair += 2)   // the UI's copy of the two rows, for prompt icons
            if ((move.marker && pair[1] == kRowPlaceMarker) || (move.call && pair[1] == kRowCall))
                g_SetAssignedButton(move.buttonMapper, pair[0], move.get(move.pad3, pair[1]));
    }

    bool InstallHook(std::uintptr_t address, const std::uint8_t* entry, std::size_t entrySize, void* hook, void** original,
                     void*& target, const char* function)
    {
        void* fn = address ? ResolveGameAddress(address) : nullptr;
        if (!fn)
        {
            LogError("PadAssign: no %s address on %s, controller assignments off", function, GetGameBuildName(gGameBuild));
            return false;
        }
        const HookEntry state = CheckHookEntry(fn, entry, entrySize);
        if (state == HookEntry::Unknown)
        {
            char found[160] = {};
            DescribeFunctionEntry(fn, found, sizeof(found));
            LogError("PadAssign: %s is not the known code on %s (%s), controller assignments off",
                     function, GetGameBuildName(gGameBuild), found);
            return false;
        }
        if (state == HookEntry::ForeignHook)
        {
            char what[96] = {};
            std::snprintf(what, sizeof(what), "PadAssign: %s", function);
            LogHookedOnTop(what, fn);
        }
        if (!CreateAndEnableHook(fn, hook, original))
        {
            LogError("PadAssign: %s hook failed at %p, controller assignments off", function, fn);
            return false;
        }
        target = fn;
        return true;
    }

    void RemoveHooks()
    {
        if (g_UpdateTarget)
            DisableAndRemoveHook(g_UpdateTarget);
        if (g_SetPadMappingsTarget)
            DisableAndRemoveHook(g_SetPadMappingsTarget);
        g_UpdateTarget = g_SetPadMappingsTarget = nullptr;
        g_OrigUpdate = nullptr;
        g_OrigSetPadMappings = nullptr;
    }

    void ForgetGameTables()
    {
        for (const std::int32_t*& table : g_GameTable)
            table = nullptr;
    }
}

namespace padassign
{
    bool IsActive()
    {
        return g_Active.load(std::memory_order_acquire);
    }

    int ControlType()
    {
        const int controlType = ControlTypeSEH();
        return IsControlType(controlType) ? controlType : 0;
    }

    bool IsOfferedButton(int button)
    {
        return button >= 0 && button < kButtonDpadUp && button != kButtonSelect;
    }

    int ButtonOfAction(int controlType, int action)
    {
        if (!IsControlType(controlType) || action < 0 || action >= kTableRows)
            return kNoButton;
        const std::int32_t button = g_OurTable[controlType][action];
        return button >= 0 && button < kButtonCount ? button : kNoButton;
    }

    bool GiveActionButton(int controlType, int action, int button)
    {
        if (!IsOfferedButton(button))
            return false;
        SavedType saved;
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            const int now = ButtonOfAction(controlType, action);
            if (!IsOfferedButton(now))
                return false;
            if (now == button)
                return true;
            for (std::uint8_t& does : g_Button[controlType])
            {
                if (does == now)
                    does = static_cast<std::uint8_t>(button);
                else if (does == button)
                    does = static_cast<std::uint8_t>(now);
            }
            RebuildTable(controlType);
            PackType(controlType, saved);
            g_Change.fetch_add(1, std::memory_order_release);
        }
        SaveType(controlType, saved);
        return true;
    }

    int ButtonOfName(int name)
    {
        if (name < 0 || name >= kNameCount)
            return kNoButton;
        std::lock_guard<std::mutex> lock(g_Lock);
        return ButtonOfNameLocked(g_Names[name]);
    }

    bool GiveNameButton(int name, int button)
    {
        if (name < 0 || name >= kNameCount || button < 0 || button >= kButtonCount)
            return false;
        int wanted = kNoButton;
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            if (!GiveNameButtonLocked(g_Names[name], button))
                return false;
            wanted = g_Names[name].wanted;
        }
        SaveNameButton(g_Names[name], wanted);
        return true;
    }

    void UseGameLayout(int controlType)
    {
        SavedType saved;
        bool nameChanged[kNameCount] = {};
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            if (IsControlType(controlType))
            {
                for (int game = 0; game < kButtonCount; ++game)
                    g_Button[controlType][game] = static_cast<std::uint8_t>(game);
                RebuildTable(controlType);
                PackType(controlType, saved);
                g_Change.fetch_add(1, std::memory_order_release);
            }
            for (int name = 0; name < kNameCount; ++name)
                nameChanged[name] = g_Names[name].wanted != kNoButton && GiveNameButtonLocked(g_Names[name], g_Names[name].gameButton);
        }
        SaveType(controlType, saved);
        for (int name = 0; name < kNameCount; ++name)
            if (nameChanged[name])
                SaveNameButton(g_Names[name], kNoButton);
    }

    void Read(Layout& layout)
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        std::memcpy(layout.button, g_Button, sizeof(layout.button));
        for (int name = 0; name < kNameCount; ++name)
            layout.nameButton[name] = static_cast<std::int8_t>(ButtonOfNameLocked(g_Names[name]));
    }

    void Write(const Layout& layout)
    {
        SavedType saved[kControlTypeCount];
        bool nameChanged[kNameCount] = {};
        int nameWanted[kNameCount] = {};
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            bool changed = false;
            for (int type = 0; type < kControlTypeCount; ++type)
            {
                if (!IsControlType(type) || !IsFullSet(layout.button[type])
                    || std::memcmp(g_Button[type], layout.button[type], sizeof(g_Button[type])) == 0)
                    continue;
                std::memcpy(g_Button[type], layout.button[type], sizeof(g_Button[type]));
                RebuildTable(type);
                PackType(type, saved[type]);
                changed = true;
            }
            if (changed)
                g_Change.fetch_add(1, std::memory_order_release);
            for (int name = 0; name < kNameCount; ++name)
            {
                const int button = layout.nameButton[name];
                if (button < 0 || button >= kButtonCount || button == ButtonOfNameLocked(g_Names[name]))
                    continue;
                nameChanged[name] = GiveNameButtonLocked(g_Names[name], button);
                nameWanted[name] = g_Names[name].wanted;
            }
        }
        for (int type = 0; type < kControlTypeCount; ++type)
            SaveType(type, saved[type]);
        for (int name = 0; name < kNameCount; ++name)
            if (nameChanged[name])
                SaveNameButton(g_Names[name], nameWanted[name]);
    }

    bool Differs(const Layout& layout)
    {
        Layout now;
        Read(now);
        return std::memcmp(now.button, layout.button, sizeof(now.button)) != 0
            || std::memcmp(now.nameButton, layout.nameButton, sizeof(now.nameButton)) != 0;
    }

    void ApplySavedNameButtons()
    {
        if (g_NamesSettled.load(std::memory_order_acquire))
            return;
        const ULONGLONG now = GetTickCount64();
        if (now - g_NameLastTry.load(std::memory_order_relaxed) < kNameRetryMs)
            return;
        g_NameLastTry.store(now, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(g_Lock);
        bool settled = true;
        for (NameState& name : g_Names)
        {
            if (name.gameButton != kNoButton)
                continue;
            int padButton = kNoButton, keyCode = 0;
            if (!TextIcons::FindButton(name.action, padButton, keyCode))
            {
                settled = false;   // its feature has not put it in the button map yet
                continue;
            }
            name.gameButton = padButton;
            if (name.wanted == padButton)
                name.wanted = kNoButton;
            else if (name.wanted != kNoButton)
                TextIcons::SetActionPadButton(name.action, name.wanted);
        }
        g_NamesSettled.store(settled, std::memory_order_release);
    }
}

bool Install_PadAssign_Hooks()
{
    if (g_UpdateTarget)
        return true;

    LoadSaved();

    auto* image = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    g_TableSlots          = reinterpret_cast<const std::int32_t**>(ResolveGameAddress(gAddr.PlayerPad_ButtonTables));
    g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.Fox_GetQuarkSystemTable));
    g_SetAssignedButton   = reinterpret_cast<SetAssignedButton_t>(ResolveGameAddress(gAddr.ButtonMapper_SetAssignedButton));
    g_ButtonMapperSlot    = reinterpret_cast<void**>(ResolveGameAddress(gAddr.ButtonMapper_Map));
    const auto* uiPairs   = reinterpret_cast<const std::uint32_t*>(ResolveGameAddress(gAddr.PlayerPad_UiPairList));
    if (!image || !g_TableSlots || !g_GetQuarkSystemTable)
    {
        LogError("PadAssign: no address on %s, controller assignments off", GetGameBuildName(gGameBuild));
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    g_GameImageBegin = reinterpret_cast<std::uintptr_t>(image);
    g_GameImageEnd   = g_GameImageBegin + nt->OptionalHeader.SizeOfImage;

    if (!ReadGameTablesSEH())
    {
        ForgetGameTables();
        LogError("PadAssign: the game's pad tables are not the known ones on %s, controller assignments off",
                 GetGameBuildName(gGameBuild));
        return false;
    }
    g_UiPairs = uiPairs && UiPairsLookRightSEH(uiPairs) ? uiPairs : nullptr;
    if (!g_UiPairs || !g_SetAssignedButton || !g_ButtonMapperSlot)
        LogWarn("PadAssign: the UI's list of player buttons is not the known one on %s, prompt icons may show the game's "
                "button while riding a vehicle", GetGameBuildName(gGameBuild));
    for (int type = 0; type < padassign::kControlTypeCount; ++type)
        RebuildTable(type);

    if (!InstallHook(gAddr.PadImpl_Update, kUpdateEntry, sizeof(kUpdateEntry), reinterpret_cast<void*>(&hk_Update),
                     reinterpret_cast<void**>(&g_OrigUpdate), g_UpdateTarget, "PadImpl::Update")
        || !InstallHook(gAddr.PadImpl_SetPadMappings, kSetPadMappingsEntry, sizeof(kSetPadMappingsEntry),
                        reinterpret_cast<void*>(&hk_SetPadMappings), reinterpret_cast<void**>(&g_OrigSetPadMappings),
                        g_SetPadMappingsTarget, "PadImpl::SetPadMappings"))
    {
        RemoveHooks();
        ForgetGameTables();
        return false;
    }
    if (!SetTableSlotsSEH(true))
    {
        SetTableSlotsSEH(false);
        RemoveHooks();
        ForgetGameTables();
        LogError("PadAssign: the game's pad table pointers cannot be written on %s, controller assignments off",
                 GetGameBuildName(gGameBuild));
        return false;
    }
    g_Change.fetch_add(1, std::memory_order_release);   // a pad that updated before the swap loads once more, from our table
    g_Active.store(true, std::memory_order_release);
    return true;
}

void Uninstall_PadAssign_Hooks()
{
    {
        std::lock_guard<std::mutex> lock(g_Lock);   // a change under way finishes before the tables go
        if (g_Active.exchange(false, std::memory_order_acq_rel))
        {
            SetTableSlotsSEH(false);
            g_Change.fetch_add(1, std::memory_order_release);   // live pads load the game's table again
        }
        ForgetGameTables();
    }
    RemoveHooks();
}
