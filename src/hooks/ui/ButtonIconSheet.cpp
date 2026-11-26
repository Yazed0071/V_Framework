#include <pch.h>
#include "ButtonIconSheet.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "V_FrameWorkState.h"
#include "log.h"

namespace
{
    using PrefetchControl_t    = void* (__fastcall*)();
    using CreatePrefetch_t     = bool(__fastcall*)(void* prefetch, std::uint64_t path, int level);
    using GetPrefetch_t        = void** (__fastcall*)(void* prefetch, void** texture, std::uint64_t path);
    using SetFontIconTexture_t = void(__fastcall*)(void* fontManager, std::uint32_t texture, void** loaded,
                                                   float cellWidth, float cellHeight, float drawScale,
                                                   float advanceScale);
    using PadGetInstance_t     = void* (__fastcall*)();
    using GetPhysicalPadNum_t  = int(__fastcall*)(int logicalPad);
    using DevPadGetInstance_t  = void** (__fastcall*)(unsigned int index);
    using RequestTextureByPath_t = std::uint32_t(__fastcall*)(const std::uint64_t* path);
    using GetShaderResourceView_t = void* (__fastcall*)(std::uint8_t* texture);

    constexpr std::uint8_t kSetFontIconTextureEntry[] =
    {
        0x4C, 0x8B, 0xDC, 0x4D, 0x89, 0x43, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC,
        0x70,
    };
    constexpr std::uint8_t kRequestTextureEntry[] =
    {
        0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48,
        0xC7, 0x40, 0xB8, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x20, 0x48,
        0x8B, 0xD9, 0x48, 0x8B, 0x35, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8E, 0x50, 0x06, 0x00, 0x00,
    };
    constexpr std::uint8_t kRequestTextureMask[] =
    {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    constexpr std::uint8_t kShaderResourceViewEntry[] =
    {
        0x48, 0x8B, 0x41, 0x10, 0x48, 0x85, 0xC0, 0x75, 0x02, 0xF3, 0xC3, 0x48, 0x8B, 0x40, 0x10, 0xC3,
    };

    constexpr std::size_t   kTexturePathCode = 0x08;   // a texture manager entry keeps its file's path code here, extension bits dropped
    constexpr std::size_t   kTextureBlock    = 0x10;
    constexpr std::size_t   kBlockView       = 0x10;   // null until the file's picture is in memory, and for good when the file does not exist
    constexpr std::size_t   kTextureStride   = 0x50;
    constexpr std::uint64_t kPathStemMask    = 0x3FFFFFFFFFFFFull;
    constexpr int           kHandleIndexShift = 17;

    constexpr std::size_t  kRequestLoadsManager = 0x22;   // mov rsi,[rip+rel32] in RequestTextureByPath
    constexpr std::uint8_t kLoadManagerOpcode[] = { 0x48, 0x8B, 0x35 };
    constexpr std::size_t  kManagerEntries      = 0x08;

    constexpr std::uint32_t kButtonIcon = 0x01798CBF;   // ButtonIcon, the name every button icon is cut from
    constexpr int kPrefetchLevel = 2;

    enum Style { kStyleXbox360, kStyleXboxOne, kStyleDualShock4, kStyleDualShock3, kStyleCount };
    constexpr int kStyleGame = -1;   // nothing picked and no pad we know: every picture stays as the game draws it

    enum Family { kFamilySheet, kFamilyPadPicture, kFamilyPadLinesA, kFamilyPadLinesB, kFamilyCount };

    constexpr std::uint64_t kPadLinesACode = 0x156BE848144627F8ull;   // the Controls screen's two pointer-line pictures,
    constexpr std::uint64_t kPadLinesBCode = 0x156BFBD98D4DA65Dull;   // known by path code only

    struct Picture
    {
        Family        family;
        Style         style;
        const char*   path;   // null: a game picture known by its path code alone
        std::uint64_t code;
    };

    Picture g_Pictures[] =
    {
        { kFamilySheet,      kStyleXbox360,    "/Assets/tpp/ui/texture/ButtonIcon/cmn_btn_icon_a_x36_alp.ftex", 0 },
        { kFamilySheet,      kStyleXboxOne,    "/Assets/tpp/ui/texture/ButtonIcon/cmn_btn_icon_a_alp.ftex", 0 },
        { kFamilySheet,      kStyleDualShock4, "/Assets/tpp/ui/texture/ButtonIcon/cmn_btn_icon_a_ps4_alp.ftex", 0 },
        { kFamilySheet,      kStyleDualShock3, "/Assets/tpp/ui/texture/ButtonIcon/cmn_btn_icon_a_ps3_alp.ftex", 0 },
        { kFamilyPadPicture, kStyleXbox360,    "/Assets/tpp/ui/ModelAsset/sys_pause/Pictures/sys_pause_pad_x36.ftex", 0 },
        { kFamilyPadPicture, kStyleDualShock4, "/Assets/tpp/ui/ModelAsset/sys_pause/Pictures/sys_pause_pad_ds4.ftex", 0 },
        { kFamilyPadPicture, kStyleDualShock3, "/Assets/tpp/ui/ModelAsset/sys_pause/Pictures/sys_pause_pad_ds3.ftex", 0 },
        { kFamilyPadLinesA,  kStyleXbox360,    nullptr, kPadLinesACode },
        { kFamilyPadLinesA,  kStyleDualShock4, "/Assets/tpp/ui/ModelAsset/sys_pause/Pictures/sys_pause_pad_ds4_line_a.ftex", 0 },
        { kFamilyPadLinesA,  kStyleDualShock3, "/Assets/tpp/ui/ModelAsset/sys_pause/Pictures/sys_pause_pad_ds3_line_a.ftex", 0 },
        { kFamilyPadLinesB,  kStyleXbox360,    nullptr, kPadLinesBCode },
        { kFamilyPadLinesB,  kStyleDualShock4, "/Assets/tpp/ui/ModelAsset/sys_pause/Pictures/sys_pause_pad_ds4_line_b.ftex", 0 },
        { kFamilyPadLinesB,  kStyleDualShock3, "/Assets/tpp/ui/ModelAsset/sys_pause/Pictures/sys_pause_pad_ds3_line_b.ftex", 0 },
    };
    constexpr int kPictureCount = static_cast<int>(sizeof(g_Pictures) / sizeof(g_Pictures[0]));

    std::uint64_t              g_PictureStem[kPictureCount] = {};
    std::atomic<std::uint32_t> g_PictureIndex[kPictureCount] = {};   // texture manager entry index + 1, 0 = not requested yet
    int                        g_Shown[kFamilyCount][kStyleCount] = {};   // the picture a family shows in a style; the game's own where the style has none

    constexpr std::size_t kIconTextureMap   = 0x30;   // the font manager's std::map of texture name -> picture
    constexpr std::size_t kNodeLeft         = 0x00;
    constexpr std::size_t kNodeParent       = 0x08;
    constexpr std::size_t kNodeRight        = 0x10;
    constexpr std::size_t kNodeIsNil        = 0x19;
    constexpr std::size_t kNodeKey          = 0x20;
    constexpr std::size_t kNodeTexture      = 0x30;
    constexpr std::size_t kNodeWidth        = 0x40;
    constexpr std::size_t kNodeHeight       = 0x44;
    constexpr std::size_t kNodeDrawScale    = 0x48;
    constexpr std::size_t kNodeAdvanceScale = 0x4C;

    constexpr std::size_t kPadListeners   = 0x30;
    constexpr std::size_t kListenerDevPad = 0x0A;
    constexpr std::size_t kRecordVendor   = 0x10;
    constexpr std::size_t kRecordProduct  = 0x12;
    constexpr std::size_t kRecordDetail   = 0x18;
    constexpr int kPadCount = 4;

    constexpr std::uint16_t kVendorMicrosoft    = 0x045E;
    constexpr std::uint16_t kVendorSony         = 0x054C;
    constexpr std::uint16_t kProductDualShock3  = 0x0268;
    constexpr std::uint16_t kXbox360Products[]  = { 0x028E, 0x028F, 0x0291, 0x02A1, 0x0719 };   // wired pad, wireless pad, the three wireless receivers

    SetFontIconTexture_t g_Orig       = nullptr;
    void*                g_HookTarget = nullptr;
    void*                g_OurTexture = nullptr;
    int                  g_OurStyle   = kStyleGame;   // the style whose sheet we registered for text icons

    RequestTextureByPath_t   g_OrigRequest    = nullptr;
    void*                    g_RequestTarget  = nullptr;
    GetShaderResourceView_t  g_OrigView       = nullptr;
    void*                    g_ViewTarget     = nullptr;
    std::atomic<int>         g_Style{ kStyleGame };   // set by the pad tick and the menu, read by the render thread
    std::atomic<bool>        g_PicturesPinned{ false };
    std::uint8_t**           g_TextureManagerSlot = nullptr;   // TextureManager::s_Instance, read out of the request function's own code

    constexpr ULONGLONG kSheetLoadWaitMs = 15000;
    int       g_AwaitedStyle = kStyleGame;
    ULONGLONG g_AwaitedSince = 0;
    bool      g_WarnedMissing[kStyleCount] = {};

    constexpr const char* kStateSpace = "ButtonIcons";
    constexpr const char* kStateName  = "Sheet";
    constexpr int kStoredChoice[buttoniconsheet::kChoiceCount] = { 1, 2, 5, 3, 4 };   // per choice; 1 - 4 are what builds without DualShock 3 saved for Auto, PlayStation, Xbox 360, Xbox One (the state file drops zeros)
    std::atomic<int>      g_Choice{ buttoniconsheet::kChoiceAuto };

    const char* SheetPath(int style)
    {
        return g_Pictures[g_Shown[kFamilySheet][style]].path;
    }

    std::uint8_t* PictureEntry(int picture)   // reads game memory: call inside a __try only
    {
        std::uint8_t* manager = g_TextureManagerSlot ? *g_TextureManagerSlot : nullptr;
        std::uint8_t* entries = manager ? *reinterpret_cast<std::uint8_t**>(manager + kManagerEntries) : nullptr;
        const std::uint32_t index = g_PictureIndex[picture].load(std::memory_order_relaxed);
        if (!entries || !index)
            return nullptr;
        std::uint8_t* entry = entries + static_cast<std::size_t>(index - 1) * kTextureStride;
        return *reinterpret_cast<std::uint64_t*>(entry + kTexturePathCode) == g_PictureStem[picture] ? entry : nullptr;
    }

    bool HasView(std::uint8_t* entry)   // reads game memory: call inside a __try only
    {
        auto* block = entry ? *reinterpret_cast<std::uint8_t**>(entry + kTextureBlock) : nullptr;
        return block && *reinterpret_cast<void**>(block + kBlockView);
    }

    bool PictureLoadedSEH(int picture)
    {
        __try
        {
            return HasView(PictureEntry(picture));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return false;
    }

    bool CanJudgePictures()
    {
        return g_TextureManagerSlot && g_RequestTarget;   // without both there is no telling loaded from missing
    }

    bool SheetUsable(int style)   // called from the pad tick only
    {
        if (!CanJudgePictures() || PictureLoadedSEH(g_Shown[kFamilySheet][style]))
        {
            g_AwaitedStyle = kStyleGame;
            return true;
        }
        const ULONGLONG now = GetTickCount64();
        if (g_AwaitedStyle != style)
        {
            g_AwaitedStyle = style;
            g_AwaitedSince = now;
        }
        else if (!g_WarnedMissing[style] && now - g_AwaitedSince > kSheetLoadWaitMs)
        {
            g_WarnedMissing[style] = true;
            LogWarn("ButtonIconSheet: %s is not in the game data (it comes with V Framework's own package); "
                    "the game's own button icons stay", SheetPath(style));
        }
        return false;
    }

    int StyleForPad()
    {
        switch (g_Choice.load(std::memory_order_relaxed))
        {
        case buttoniconsheet::kChoiceDualShock4: return kStyleDualShock4;
        case buttoniconsheet::kChoiceDualShock3: return kStyleDualShock3;
        case buttoniconsheet::kChoiceXbox:       return kStyleXbox360;
        case buttoniconsheet::kChoiceXboxOne:    return kStyleXboxOne;
        default:                                 break;
        }

        auto getPad = reinterpret_cast<PadGetInstance_t>(ResolveGameAddress(gAddr.Pad_GetInstance));
        auto physicalPadNum = reinterpret_cast<GetPhysicalPadNum_t>(ResolveGameAddress(gAddr.PadMapping_GetPhysicalPadNum));
        auto getDevPad = reinterpret_cast<DevPadGetInstance_t>(ResolveGameAddress(gAddr.DevPad_GetInstance));
        if (!getPad || !physicalPadNum || !getDevPad)
            return kStyleGame;
        auto* instance = static_cast<std::uint8_t*>(getPad());
        const int slot = physicalPadNum(0);
        if (!instance || slot < 0 || slot >= kPadCount)
            return kStyleGame;
        auto* listener = *reinterpret_cast<std::uint8_t**>(instance + kPadListeners + slot * sizeof(void*));
        if (!listener)
            return kStyleGame;
        void** entry = getDevPad(*reinterpret_cast<std::uint16_t*>(listener + kListenerDevPad));
        auto* record = entry ? static_cast<std::uint8_t*>(*entry) : nullptr;
        if (!record || !*reinterpret_cast<void**>(record + kRecordDetail))
            return kStyleGame;   // no device behind pad 1 (parked, unplugged or XInput only): the engine would call that X360
        const std::uint16_t product = *reinterpret_cast<std::uint16_t*>(record + kRecordProduct);
        switch (*reinterpret_cast<std::uint16_t*>(record + kRecordVendor))
        {
        case kVendorSony:      return product == kProductDualShock3 ? kStyleDualShock3 : kStyleDualShock4;
        case kVendorMicrosoft: break;
        default:               return kStyleGame;   // a pad the game does not know
        }
        for (const std::uint16_t xbox360 : kXbox360Products)
            if (product == xbox360)
                return kStyleXbox360;
        return kStyleXboxOne;   // every later Microsoft pad carries the Xbox One buttons
    }

    void* LoadPicture(const char* path)
    {
        auto prefetchControl = reinterpret_cast<PrefetchControl_t>(ResolveGameAddress(gAddr.Daemon_GetDaemonPrefetchController));
        auto createPrefetch = reinterpret_cast<CreatePrefetch_t>(ResolveGameAddress(gAddr.PrefetchTextureController_CreatePrefetchTexture));
        auto getPrefetch = reinterpret_cast<GetPrefetch_t>(ResolveGameAddress(gAddr.PrefetchTextureController_GetPrefetchTexture));
        if (!prefetchControl || !createPrefetch || !getPrefetch)
            return nullptr;
        void* prefetch = prefetchControl();
        if (!prefetch)
            return nullptr;
        const std::uint64_t code = FoxHashes::PathCode64Ext(path);
        void* loaded = nullptr;
        if (createPrefetch(prefetch, code, kPrefetchLevel))
            getPrefetch(prefetch, &loaded, code);
        return loaded;
    }

    std::uint8_t* FindIconTexture(void* fontManager, std::uint32_t name)
    {
        auto* head = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(fontManager) + kIconTextureMap);
        if (!head)
            return nullptr;
        auto* node = *reinterpret_cast<std::uint8_t**>(head + kNodeParent);
        std::uint8_t* found = nullptr;
        while (node && !*(node + kNodeIsNil))
        {
            if (*reinterpret_cast<std::uint32_t*>(node + kNodeKey) < name)
            {
                node = *reinterpret_cast<std::uint8_t**>(node + kNodeRight);
            }
            else
            {
                found = node;
                node = *reinterpret_cast<std::uint8_t**>(node + kNodeLeft);
            }
        }
        return (found && *reinterpret_cast<std::uint32_t*>(found + kNodeKey) == name) ? found : nullptr;
    }

    void RememberOurs(void* fontManager, int style)
    {
        auto* entry = FindIconTexture(fontManager, kButtonIcon);
        g_OurStyle = style;
        g_OurTexture = entry ? *reinterpret_cast<void**>(entry + kNodeTexture) : nullptr;
        LogInfo("ButtonIconSheet: button icons now come from %s", SheetPath(style));
    }

    void ReplaceSheetSEH()
    {
        __try
        {
            int wanted = StyleForPad();
            g_Style.store(wanted, std::memory_order_relaxed);
            if (wanted == kStyleGame)
                g_AwaitedStyle = kStyleGame;
            else if (!SheetUsable(wanted))
                wanted = kStyleGame;
            if (wanted == kStyleGame)
            {
                if (g_OurStyle == kStyleGame)
                    return;
                wanted = kStyleXbox360;   // we replaced the sheet earlier and now have none to show: the game's own sheet again
            }
            auto** fontManagerSlot = reinterpret_cast<void**>(ResolveGameAddress(gAddr.RawDaemon_s_fontManager));
            void* fontManager = fontManagerSlot ? *fontManagerSlot : nullptr;
            if (!fontManager || !g_Orig)
                return;

            auto* entry = FindIconTexture(fontManager, kButtonIcon);
            if (!entry)
                return;
            if (wanted == g_OurStyle && *reinterpret_cast<void**>(entry + kNodeTexture) == g_OurTexture)
                return;

            void* loaded = LoadPicture(SheetPath(wanted));
            if (!loaded)
                return;
            g_Orig(fontManager, kButtonIcon, &loaded,
                   *reinterpret_cast<float*>(entry + kNodeWidth),
                   *reinterpret_cast<float*>(entry + kNodeHeight),
                   *reinterpret_cast<float*>(entry + kNodeDrawScale),
                   *reinterpret_cast<float*>(entry + kNodeAdvanceScale));
            RememberOurs(fontManager, wanted);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void PinPictures()
    {
        if (g_PicturesPinned.exchange(true))
            return;
        for (const Picture& picture : g_Pictures)   // the prefetch keeps them loaded for the session, so the render path can hand
            if (picture.path && (picture.family == kFamilySheet || picture.style != kStyleXbox360))   // any of them out the moment the pick changes
                LoadPicture(picture.path);
    }

    void __fastcall hk_SetFontIconTexture(void* fontManager, std::uint32_t texture, void** loaded,
                                          float cellWidth, float cellHeight, float drawScale,
                                          float advanceScale)
    {
        if (texture == kButtonIcon && fontManager)
        {
            PinPictures();
            const int wanted = StyleForPad();
            g_Style.store(wanted, std::memory_order_relaxed);
            const bool usable = wanted != kStyleGame
                             && (!CanJudgePictures() || PictureLoadedSEH(g_Shown[kFamilySheet][wanted]));   // not loaded yet: the pad tick swaps it in later
            void* ours = usable ? LoadPicture(SheetPath(wanted)) : nullptr;
            if (ours)
            {
                g_Orig(fontManager, texture, &ours, cellWidth, cellHeight, drawScale, advanceScale);
                RememberOurs(fontManager, wanted);
                return;
            }
        }
        g_Orig(fontManager, texture, loaded, cellWidth, cellHeight, drawScale, advanceScale);
    }

    std::uint32_t __fastcall hk_RequestTextureByPath(const std::uint64_t* path)
    {
        const std::uint64_t code = path ? *path : 0;
        const std::uint32_t handle = g_OrigRequest(path);
        if (handle)
            for (int picture = 0; picture < kPictureCount; ++picture)
                if (code == g_Pictures[picture].code)
                    g_PictureIndex[picture].store((handle >> kHandleIndexShift) + 1, std::memory_order_relaxed);
        return handle;
    }

    std::uint8_t* SwappedPictureSEH(std::uint8_t* texture, int style)
    {
        __try
        {
            const std::uint64_t stem = *reinterpret_cast<std::uint64_t*>(texture + kTexturePathCode);
            for (int drawn = 0; drawn < kPictureCount; ++drawn)
            {
                if (stem != g_PictureStem[drawn])
                    continue;
                const int wanted = g_Shown[g_Pictures[drawn].family][style];
                if (wanted == drawn)
                    return texture;
                std::uint8_t* other = PictureEntry(wanted);
                return HasView(other) ? other : texture;   // no picture (yet): keep the drawn one
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return texture;
    }

    void* __fastcall hk_GetShaderResourceView(std::uint8_t* texture)
    {
        const int style = g_Style.load(std::memory_order_relaxed);
        if (texture && style != kStyleGame)
            texture = SwappedPictureSEH(texture, style);
        return g_OrigView(texture);
    }

    bool HookChecked(std::uintptr_t address, const std::uint8_t* bytes, const std::uint8_t* mask, std::size_t size,
                     const char* what, void* detour, void** original, void*& target)
    {
        void* at = ResolveGameAddress(address);
        if (!at)
        {
            LogError("ButtonIconSheet: no %s address on %s, layout button icons keep the game's art", what,
                     GetGameBuildName(gGameBuild));
            return false;
        }
        const HookEntry entry = mask ? CheckHookEntryMasked(at, bytes, mask, size) : CheckHookEntry(at, bytes, size);
        if (entry == HookEntry::Unknown)
        {
            char found[160] = {};
            DescribeFunctionEntry(at, found, sizeof(found));
            LogError("ButtonIconSheet: the %s is not the known code on %s (%s), layout button icons keep the game's art",
                     what, GetGameBuildName(gGameBuild), found);
            return false;
        }
        if (entry == HookEntry::ForeignHook)
            LogHookedOnTop(what, at);
        if (!CreateAndEnableHook(at, detour, original))
        {
            LogError("ButtonIconSheet: %s hook failed at %p, layout button icons keep the game's art", what, at);
            return false;
        }
        target = at;
        return true;
    }

    void ListPictures()
    {
        for (int picture = 0; picture < kPictureCount; ++picture)
        {
            if (g_Pictures[picture].path)
                g_Pictures[picture].code = FoxHashes::PathCode64Ext(g_Pictures[picture].path);
            g_PictureStem[picture] = g_Pictures[picture].code & kPathStemMask;
        }
        for (int family = 0; family < kFamilyCount; ++family)
            for (int style = 0; style < kStyleCount; ++style)
            {
                int shown = -1;
                for (int picture = 0; picture < kPictureCount; ++picture)
                {
                    if (g_Pictures[picture].family != family)
                        continue;
                    if (g_Pictures[picture].style == style)
                        shown = picture;
                    else if (shown < 0 && g_Pictures[picture].style == kStyleXbox360)
                        shown = picture;
                }
                g_Shown[family][style] = shown;
            }
    }

    void InstallLayoutPictureHooks()
    {
        if (auto* request = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.TextureManager_RequestTextureByPath)))
        {
            std::uint8_t* load = request + kRequestLoadsManager;
            if (std::memcmp(load, kLoadManagerOpcode, sizeof(kLoadManagerOpcode)) == 0)
            {
                std::int32_t displacement = 0;
                std::memcpy(&displacement, load + sizeof(kLoadManagerOpcode), sizeof(displacement));
                g_TextureManagerSlot = reinterpret_cast<std::uint8_t**>(load + sizeof(kLoadManagerOpcode) + sizeof(displacement) + displacement);
            }
        }
        if (!g_TextureManagerSlot)
            LogWarn("ButtonIconSheet: the texture request code differs on %s, layout button icons keep the game's art",
                    GetGameBuildName(gGameBuild));
        if (!HookChecked(gAddr.TextureManager_RequestTextureByPath, kRequestTextureEntry, kRequestTextureMask,
                         sizeof(kRequestTextureEntry), "texture request",
                         reinterpret_cast<void*>(&hk_RequestTextureByPath), reinterpret_cast<void**>(&g_OrigRequest),
                         g_RequestTarget))
            return;
        HookChecked(gAddr.TextureResourceDx11_GetShaderResourceView, kShaderResourceViewEntry, nullptr,
                    sizeof(kShaderResourceViewEntry), "texture view lookup",
                    reinterpret_cast<void*>(&hk_GetShaderResourceView), reinterpret_cast<void**>(&g_OrigView),
                    g_ViewTarget);
    }
}

namespace buttoniconsheet
{
    void FollowPad()
    {
        if (g_HookTarget)
            ReplaceSheetSEH();
    }

    int GetChoice()
    {
        return g_Choice.load(std::memory_order_relaxed);
    }

    void SetChoice(int choice)
    {
        if (choice < kChoiceAuto || choice >= kChoiceCount)
            choice = kChoiceAuto;
        if (g_Choice.exchange(choice, std::memory_order_relaxed) == choice)
            return;
        V_FrameWorkState::SetPersistedConstant(kStateSpace, kStateName, kStoredChoice[choice]);
        if (g_HookTarget)
            ReplaceSheetSEH();
    }
}

bool Install_ButtonIconSheet_Hook()
{
    if (g_HookTarget)
        return true;

    const std::int32_t stored = V_FrameWorkState::GetPersistedConstant(kStateSpace, kStateName);
    for (int choice = 0; choice < buttoniconsheet::kChoiceCount; ++choice)
        if (kStoredChoice[choice] == stored)
            g_Choice.store(choice, std::memory_order_relaxed);

    void* target = ResolveGameAddress(gAddr.Ui_SetFontIconTexture);
    if (!target)
    {
        LogError("ButtonIconSheet: no address on %s, button icons keep the game's own sheet",
                 GetGameBuildName(gGameBuild));
        return false;
    }

    const HookEntry entry = CheckHookEntry(target, kSetFontIconTextureEntry, sizeof(kSetFontIconTextureEntry));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("ButtonIconSheet: the icon texture registration is not the known code on %s (%s), button icons "
                 "keep the game's own sheet", GetGameBuildName(gGameBuild), found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("ButtonIconSheet: icon texture registration", target);
    ListPictures();
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_SetFontIconTexture), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ButtonIconSheet: icon texture hook failed at %p, button icons keep the game's own sheet", target);
        return false;
    }
    g_HookTarget = target;
    InstallLayoutPictureHooks();
    return true;
}

void Uninstall_ButtonIconSheet_Hook()
{
    if (g_ViewTarget)
    {
        DisableAndRemoveHook(g_ViewTarget);
        g_ViewTarget = nullptr;
        g_OrigView   = nullptr;
    }
    if (g_RequestTarget)
    {
        DisableAndRemoveHook(g_RequestTarget);
        g_RequestTarget = nullptr;
        g_OrigRequest   = nullptr;
    }
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
