#include <pch.h>

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter.h"
#include "LocationIconRegistry.h"

#include "AddressSet.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NativeCallPatch.h"
#include "log.h"
#include "../buddy/BuddyCodeCave.h"

namespace
{
    using TablistSetup_t = std::uintptr_t(__fastcall*)(void* self);
    using ActivateTablist_t = std::uintptr_t(__fastcall*)(void* self, bool activate);
    using MakeLabel_t = std::uint64_t(__fastcall*)(void* ui, std::uint64_t langKey);
    using LangIdToKey_t = void* (__fastcall*)(std::uint64_t* out, const char* langId);

    constexpr int kMaxCustomTabs = 15;
    constexpr int kMaxTabs = 20;   // the tab bar graph /Assets/fox/ui/prefab/GraphAsset/tablist/tablist.uigb has 20 tab slots
    constexpr int kMaxVanillaTabs = 5;
    constexpr std::uint8_t kFirstCustomTabType = 5;
    constexpr std::uint16_t kNoLocation = 0xFFFF;
    constexpr std::uint64_t kVanillaTabTexture = 0x156B77F99BE3DC3Dull;   // /Assets/tpp/ui/texture/CategoryIcon/mb_msm_icon_cat_a.ftex
    constexpr std::size_t kSlotMakeLabel = 0x758 / sizeof(void*);

    constexpr std::uintptr_t kTabTypes = 0x160;
    constexpr std::uintptr_t kTabCount = 0x165;
    constexpr std::uintptr_t kTabSelectedCount = 0x194;
    constexpr std::uintptr_t kTabSelectedIndex = 0x198;
    constexpr std::uintptr_t kTabTexture = 0x1A0;
    constexpr std::uintptr_t kTabTextureList = 0x1A8;
    constexpr std::uintptr_t kTabUvList = 0x1B0;
    constexpr std::uintptr_t kTabLabelList = 0x1C8;
    constexpr std::uintptr_t kTabUvs = 0x1F0;
    constexpr std::uintptr_t kTabLabels = 0x240;
    constexpr std::uintptr_t kCurrentTabType = 0x7C0;

    struct TabUv
    {
        float u;
        float v;
        float unused0;
        float unused1;
    };

    struct LocationTab
    {
        std::uint16_t locationId;
        std::uint64_t nameLangKey;
    };

    struct alignas(8) TabState
    {
        std::uint32_t count;
        std::uint32_t unused;
        void* owner;
        std::uint8_t types[kMaxTabs];
    };

    std::mutex g_Mutex;
    std::vector<LocationTab> g_Tabs;
    std::unordered_map<std::uint16_t, bool> g_TabVisible;

    TabState g_TabState{};
    std::uint16_t g_TabLocationByType[kMaxCustomTabs] = {
        kNoLocation, kNoLocation, kNoLocation, kNoLocation, kNoLocation, kNoLocation, kNoLocation, kNoLocation,
        kNoLocation, kNoLocation, kNoLocation, kNoLocation, kNoLocation, kNoLocation, kNoLocation
    };
    struct TabLists
    {
        TabUv uvs[kMaxTabs];
        std::uint64_t labels[kMaxTabs];
        std::uint64_t textures[kMaxTabs];
    };

    TabLists* g_TabLists = nullptr;   // never freed: the game's tab widget keeps pointing here

    TablistSetup_t g_OrigTablistSetup = nullptr;
    ActivateTablist_t g_OrigActivateTablist = nullptr;
    NativeCodePatch g_Patches[2]{};

    const std::uint8_t kTablistSetupPrologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0xE2, 0x32, 0x00, 0x00,
        0x48, 0x8B, 0x43, 0x38, 0x48, 0x8B, 0x48, 0x48, 0x33, 0xD2
    };
    const std::uint8_t kTablistSetupMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1
    };
    const std::uint8_t kActivatePrologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x84, 0xD2, 0x74, 0x3B,
        0xE8, 0x4E, 0x49, 0x00, 0x00, 0x0F, 0xB6, 0x8B, 0x65, 0x01, 0x00, 0x00, 0x33, 0xD2
    };
    const std::uint8_t kActivateMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1
    };
    const std::uint8_t kTabTypeReadBytes[] = {
        0x0F, 0xB6, 0x8C, 0x18, 0x40, 0x01, 0x00, 0x00   // movzx ecx,byte [rax+rbx+0x140]: the tab type of the picked tab
    };
    const std::uint8_t kTabFilterBytes[] = {
        0x75, 0x24, 0x3C, 0x04, 0x41, 0x0F, 0x94, 0xC0, 0xEB, 0x1C   // jne; cmp al,4; sete r8b; jmp: tab type 3 keeps category 4 missions
    };
    constexpr std::uintptr_t kTabFilterResume = 0x26;

    std::uint64_t LangKeySEH(const char* langId)
    {
        __try
        {
            const auto toKey = reinterpret_cast<LangIdToKey_t>(ResolveGameAddress(gAddr.Ui_LangIdToKey));
            std::uint64_t key = 0;
            if (toKey)
                toKey(&key, langId);
            return key;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    std::uint64_t MakeLabelSEH(void* ui, std::uint64_t langKey)
    {
        __try
        {
            return reinterpret_cast<MakeLabel_t>((*static_cast<void***>(ui))[kSlotMakeLabel])(ui, langKey);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    void* MenuUiSEH(std::uint8_t* self)
    {
        __try
        {
            const auto* menu = *reinterpret_cast<const std::uint8_t* const*>(self + 0x38);
            return menu ? *reinterpret_cast<void* const*>(menu + 0x20) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    void RestoreVanillaTabListsSEH(std::uint8_t* self)
    {
        __try
        {
            *reinterpret_cast<std::uint64_t*>(self + kTabTexture) = kVanillaTabTexture;
            *reinterpret_cast<std::uint64_t**>(self + kTabTextureList) = nullptr;
            *reinterpret_cast<void**>(self + kTabUvList) = self + kTabUvs;
            *reinterpret_cast<void**>(self + kTabLabelList) = self + kTabLabels;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    std::uint32_t CopyVanillaTabsSEH(std::uint8_t* self)
    {
        __try
        {
            std::uint32_t count = self[kTabCount];
            if (count > kMaxVanillaTabs)
                count = kMaxVanillaTabs;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                g_TabState.types[i] = self[kTabTypes + i];
                std::memcpy(&g_TabLists->uvs[i], self + kTabUvs + i * sizeof(TabUv), sizeof(TabUv));
                g_TabLists->labels[i] = *reinterpret_cast<const std::uint64_t*>(self + kTabLabels + i * sizeof(std::uint64_t));
                g_TabLists->textures[i] = kVanillaTabTexture;
            }
            return count;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    void PointWidgetAtOurTabsSEH(std::uint8_t* self, std::uint32_t count, bool activating)
    {
        __try
        {
            *reinterpret_cast<std::uint64_t*>(self + kTabTexture) = 0;
            *reinterpret_cast<std::uint64_t**>(self + kTabTextureList) = g_TabLists->textures;
            *reinterpret_cast<TabUv**>(self + kTabUvList) = g_TabLists->uvs;
            *reinterpret_cast<std::uint64_t**>(self + kTabLabelList) = g_TabLists->labels;
            if (!activating)
                return;
            std::uint32_t selected = 0;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                if (g_TabState.types[i] == self[kCurrentTabType])
                    selected = i;
            }
            *reinterpret_cast<std::uint32_t*>(self + kTabSelectedCount) = count;
            *reinterpret_cast<std::uint32_t*>(self + kTabSelectedIndex) = selected;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void RebuildTabs(std::uint8_t* self, bool activating)
    {
        void* ui = MenuUiSEH(self);
        if (!ui)
            return;

        std::uint32_t count = CopyVanillaTabsSEH(self);
        const std::uint32_t vanillaCount = count;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            for (std::size_t i = 0; i < g_Tabs.size() && count < kMaxTabs; ++i)
            {
                const LocationTab& tab = g_Tabs[i];
                const auto visible = g_TabVisible.find(tab.locationId);
                if (visible == g_TabVisible.end() || !visible->second)
                    continue;

                std::uint64_t texture = kVanillaTabTexture;
                float u = 0.0f;
                float v = 0.0f;
                Get_LocationIcon(tab.locationId, texture, u, v);
                g_TabState.types[count] = static_cast<std::uint8_t>(kFirstCustomTabType + i);
                g_TabLists->uvs[count] = TabUv{ u, v, 0.0f, 0.0f };
                g_TabLists->textures[count] = texture;
                g_TabLists->labels[count] = MakeLabelSEH(ui, tab.nameLangKey);
                ++count;
            }
        }

        if (count == vanillaCount)
        {
            g_TabState.count = 0;
            RestoreVanillaTabListsSEH(self);
            return;
        }
        g_TabState.owner = self + 0x20;   // Update reads the tab type through its this+0x20 view
        g_TabState.count = count;
        PointWidgetAtOurTabsSEH(self, count, activating);
    }

    std::uintptr_t __fastcall hk_TablistSetup(void* self)
    {
        const std::uintptr_t result = g_OrigTablistSetup(self);
        if (!MissionCodeGuard::ShouldBypassHooks())
            RebuildTabs(static_cast<std::uint8_t*>(self), false);
        return result;
    }

    std::uintptr_t __fastcall hk_ActivateTablist(void* self, bool activate)
    {
        const std::uintptr_t result = g_OrigActivateTablist(self, activate);
        if (activate && !MissionCodeGuard::ShouldBypassHooks())
            RebuildTabs(static_cast<std::uint8_t*>(self), true);
        return result;
    }

    CaveCode BuildTabTypeReadCave(std::uintptr_t resume)
    {
        CaveCode code;
        code.Emit({ 0x48, 0xB9 });                                     // mov rcx,&g_TabState
        code.Imm64(reinterpret_cast<std::uintptr_t>(&g_TabState));
        code.Emit({ 0x48, 0x3B, 0x59, 0x08 });                         // cmp rbx,[rcx+8]: our tabs belong to this list
        const std::size_t otherList = code.Jcc8(0x75);
        code.Emit({ 0x3B, 0x01 });                                     // cmp eax,[rcx]: picked tab inside our count
        const std::size_t outside = code.Jcc8(0x73);
        code.Emit({ 0x0F, 0xB6, 0x4C, 0x01, 0x10 });                   // movzx ecx,byte [rcx+rax+0x10]: g_TabState.types[picked]
        code.JmpAbs(resume);
        code.Bind8(otherList);
        code.Bind8(outside);
        code.Emit({ 0x0F, 0xB6, 0x8C, 0x18, 0x40, 0x01, 0x00, 0x00 }); // movzx ecx,byte [rax+rbx+0x140]
        code.JmpAbs(resume);
        return code;
    }

    CaveCode BuildTabFilterCave(std::uintptr_t resume)
    {
        CaveCode code;
        code.Emit({ 0x85, 0xD2 });                                     // test edx,edx: edx = tab type - 3
        const std::size_t notType3 = code.Jcc8(0x75);
        code.Emit({ 0x3C, 0x04 });                                     // cmp al,4
        code.Emit({ 0x41, 0x0F, 0x94, 0xC0 });                         // sete r8b
        code.JmpAbs(resume);
        code.Bind8(notType3);
        code.Emit({ 0x83, 0xFA, 0x02 });                               // cmp edx,2: tab types 5+ are ours
        const std::size_t belowOurs = code.Jcc8(0x72);
        code.Emit({ 0x83, 0xFA, static_cast<std::uint8_t>(kMaxCustomTabs + 1) });   // cmp edx,16
        const std::size_t aboveOurs = code.Jcc8(0x77);
        code.Emit({ 0x50 });                                           // push rax
        code.Emit({ 0x48, 0xB8 });                                     // mov rax,&g_TabLocationByType
        code.Imm64(reinterpret_cast<std::uintptr_t>(&g_TabLocationByType[0]));
        code.Emit({ 0x66, 0x3B, 0x74, 0x50, 0xFC });                   // cmp si,[rax+rdx*2-4]: si = the mission's location
        code.Emit({ 0x41, 0x0F, 0x94, 0xC0 });                         // sete r8b
        code.Emit({ 0x58 });                                           // pop rax
        code.Bind8(belowOurs);
        code.Bind8(aboveOurs);
        code.JmpAbs(resume);
        return code;
    }

    bool BytesAtSEH(const std::uint8_t* at, const std::uint8_t* bytes, std::size_t size)
    {
        __try
        {
            return std::memcmp(at, bytes, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool PrepareCavePatch(NativeCodePatch& patch, std::uintptr_t siteAbs, const std::uint8_t* expected, std::size_t expectedSize,
                          const CaveCode& code, const char* what)
    {
        auto* site = static_cast<std::uint8_t*>(ResolveGameAddress(siteAbs));
        if (!site || !BytesAtSEH(site, expected, expectedSize))
        {
            LogError("LocationTabs: %s does not match on %s, custom location tabs off", what, GetGameBuildName(gGameBuild));
            return false;
        }

        auto* thunk = static_cast<std::uint8_t*>(HookArena::AllocateNear(reinterpret_cast<std::uintptr_t>(site), code.bytes.size()));
        const std::int64_t rel = thunk
            ? static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(thunk)) - static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(site) + 5)
            : 0;
        if (!thunk || rel < INT32_MIN || rel > INT32_MAX)
        {
            LogError("LocationTabs: no code page near %s, custom location tabs off", what);
            return false;
        }
        std::memcpy(thunk, code.bytes.data(), code.bytes.size());
        FlushInstructionCache(GetCurrentProcess(), thunk, code.bytes.size());

        patch = NativeCodePatch{};
        patch.target = site;
        patch.size = 5;
        std::memcpy(patch.expected, site, 5);
        patch.replacement[0] = 0xE9;
        const std::int32_t rel32 = static_cast<std::int32_t>(rel);
        std::memcpy(patch.replacement + 1, &rel32, sizeof(rel32));
        return true;
    }

    bool HookFunction(std::uintptr_t address, const std::uint8_t* prologue, const std::uint8_t* mask, std::size_t size,
                      void* detour, void** original, const char* what)
    {
        void* target = ResolveGameAddress(address);
        if (!target)
        {
            LogError("%s: no address for %s, custom location tabs off", what, GetGameBuildName(gGameBuild));
            return false;
        }

        const HookEntry entry = CheckHookEntryMasked(target, prologue, mask, size);
        if (entry == HookEntry::Unknown)
        {
            char found[96] = {};
            DescribeFunctionEntry(target, found, sizeof(found));
            LogError("%s: does not match (%s), custom location tabs off", what, found);
            return false;
        }
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("%s: hook failed, custom location tabs off", what);
            return false;
        }
        if (entry == HookEntry::ForeignHook)
            LogHookedOnTop(what, target);
        return true;
    }
}

bool Add_MissionListLocationTab(int locationId, const char* nameLangId)
{
    if (locationId <= 0 || locationId > 0xFFFF || locationId == 10 || locationId == 20)
    {
        LogWarn("V_TppUiCommand.AddMissionListLocationTab: locationId %d is not a location id or already has the game's tab (10, 20), tab not added", locationId);
        return false;
    }
    if (!nameLangId || !nameLangId[0])
    {
        LogWarn("V_TppUiCommand.AddMissionListLocationTab: location %d has no nameLangId, tab not added", locationId);
        return false;
    }
    const std::uint64_t nameLangKey = LangKeySEH(nameLangId);
    if (nameLangKey == 0)
    {
        LogWarn("V_TppUiCommand.AddMissionListLocationTab: nameLangId '%s' resolved to 0, tab not added", nameLangId);
        return false;
    }

    std::lock_guard<std::mutex> lock(g_Mutex);
    for (LocationTab& tab : g_Tabs)
    {
        if (tab.locationId == locationId)
        {
            tab.nameLangKey = nameLangKey;
            return true;
        }
    }
    if (g_Tabs.size() >= kMaxCustomTabs)
    {
        LogWarn("V_TppUiCommand.AddMissionListLocationTab: all %d custom tabs are taken, location %d gets no tab", kMaxCustomTabs, locationId);
        return false;
    }
    g_TabLocationByType[g_Tabs.size()] = static_cast<std::uint16_t>(locationId);
    g_Tabs.push_back(LocationTab{ static_cast<std::uint16_t>(locationId), nameLangKey });
    return true;
}

void Set_MissionListLocationTabVisible(int locationId, bool visible)
{
    if (locationId <= 0 || locationId > 0xFFFF)
        return;
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_TabVisible[static_cast<std::uint16_t>(locationId)] = visible;
}

bool Install_MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter_Hook()
{
    const auto tabTypeRead = reinterpret_cast<std::uintptr_t>(ResolveGameAddress(gAddr.MbDvcMissionListCallbackImpl_UpdateTabTypeRead));
    const auto tabFilter = reinterpret_cast<std::uintptr_t>(ResolveGameAddress(gAddr.MbDvcMissionListCallbackImpl_UpdateMissionListParametersTabFilter));
    if (!tabTypeRead || !tabFilter)
    {
        LogError("LocationTabs: no Missions list tab sites for %s, custom location tabs off", GetGameBuildName(gGameBuild));
        return false;
    }
    if (!g_TabLists)
        g_TabLists = static_cast<TabLists*>(VirtualAlloc(nullptr, sizeof(TabLists), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!g_TabLists)
    {
        LogError("LocationTabs: no memory for the tab lists, custom location tabs off");
        return false;
    }

    if (!PrepareCavePatch(g_Patches[0], gAddr.MbDvcMissionListCallbackImpl_UpdateTabTypeRead, kTabTypeReadBytes, sizeof(kTabTypeReadBytes),
                          BuildTabTypeReadCave(tabTypeRead + sizeof(kTabTypeReadBytes)),
                          "the tab type read in MbDvcMissionListCallbackImpl::Update"))
        return false;
    if (!PrepareCavePatch(g_Patches[1], gAddr.MbDvcMissionListCallbackImpl_UpdateMissionListParametersTabFilter, kTabFilterBytes, sizeof(kTabFilterBytes),
                          BuildTabFilterCave(tabFilter + kTabFilterResume),
                          "the tab filter in MbDvcMissionListCallbackImpl::UpdateMissionListParameters"))
        return false;

    bool patched = false;
    for (int attempt = 0; attempt < 50 && !patched; ++attempt)
    {
        patched = InstallNativeCodePatches(g_Patches, 2);
        if (!patched && !NativePatchErrorIsTransient(GetLastError()))
            break;
        if (!patched)
            Sleep(4);
    }
    if (!patched)
    {
        LogError("LocationTabs: tab patches refused (%s), custom location tabs off", NativePatchErrorName(GetLastError()));
        return false;
    }

    if (!HookFunction(gAddr.MbDvcMissionListCallbackImpl_TablistSetup, kTablistSetupPrologue, kTablistSetupMask, sizeof(kTablistSetupPrologue),
                      reinterpret_cast<void*>(&hk_TablistSetup), reinterpret_cast<void**>(&g_OrigTablistSetup),
                      "LocationTabs: Missions list tab setup"))
    {
        RemoveNativeCodePatches(g_Patches, 2);
        return false;
    }
    if (!HookFunction(gAddr.MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter, kActivatePrologue, kActivateMask, sizeof(kActivatePrologue),
                      reinterpret_cast<void*>(&hk_ActivateTablist), reinterpret_cast<void**>(&g_OrigActivateTablist),
                      "LocationTabs: MbDvcMissionListCallbackImpl::ActivatePrefabTablistParameter"))
    {
        DisableAndRemoveHook(ResolveGameAddress(gAddr.MbDvcMissionListCallbackImpl_TablistSetup));
        RemoveNativeCodePatches(g_Patches, 2);
        return false;
    }
    return true;
}

bool Uninstall_MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter_Hook()
{
    if (g_Patches[0].installed || g_Patches[1].installed)
        RemoveNativeCodePatches(g_Patches, 2);
    if (gAddr.MbDvcMissionListCallbackImpl_TablistSetup)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.MbDvcMissionListCallbackImpl_TablistSetup));
    if (gAddr.MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter));
    g_OrigTablistSetup = nullptr;
    g_OrigActivateTablist = nullptr;
    g_TabState.count = 0;
    return true;
}
