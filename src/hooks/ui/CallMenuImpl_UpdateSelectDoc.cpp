#include "pch.h"
#include "CallMenuImpl_UpdateSelectDoc.h"

#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "../player/CallMenuCommandSet_UpdateState.h"

namespace
{
    using UpdateSelectDoc_t = void(__fastcall*)(std::uint8_t* callMenu);
    using IsVisible_t       = bool(__fastcall*)(void* guide);
    using SetVisible_t      = void(__fastcall*)(void* guide, bool visible);
    using SetTextureName_t  = std::uint8_t(__fastcall*)(void* mesh, std::uint64_t texture, std::uint64_t slot, int unk);
    using PrefetchTexture_t = void(__fastcall*)(void* ui, std::uint64_t texture, int unk);
    using SetBaseUv_t       = void(__fastcall*)(void* ui, void* mesh, float u, float v);
    using SetNodeVisible_t  = void(__fastcall*)(void* ui, void* node, bool visible);

    constexpr std::size_t kSelectGuide         = 0x90;    // the "Select" guide; the game shows it only while an option is highlighted
    constexpr std::size_t kHighlightedRow      = 0x100;
    constexpr std::size_t kIsVisibleVtblIndex  = 0xD0 / 8;
    constexpr std::size_t kSetVisibleVtblIndex = 0xA8 / 8;

    constexpr std::size_t kCallMenuUi            = 0x68;
    constexpr std::size_t kColumnLogos           = 0x130;
    constexpr std::size_t kColumnLogoSize        = 0x50;
    constexpr std::size_t kLogoShownIcon         = 0x01;
    constexpr std::size_t kLogoMesh              = 0x18;
    constexpr int         kColumnCount           = 4;
    constexpr std::size_t kPrefetchTextureVtblIndex = 0x548 / 8;
    constexpr std::size_t kSetBaseUvShiftVtblIndex  = 0x5A0 / 8;   // the shift CallMenuImpl::SetCategoryIconText moves to pick a logo
    constexpr std::size_t kSetBaseUvRepeatVtblIndex = 0x5B8 / 8;
    constexpr std::size_t kSetNodeVisibleVtblIndex  = 0x2A8 / 8;
    constexpr std::uint32_t kBaseUvRepeatU = 0x96B29F61;
    constexpr std::uint32_t kBaseUvRepeatV = 0x540F4165;
    constexpr std::uint64_t kLogoTextureSlot = 0x3BBF9889;   // where CallMenuImpl puts the logo sheet
    constexpr const char* kLogoSheet = "/Assets/tpp/ui/texture/CategoryIcon/hud_calm_icon_cat_a_alp.ftex";   // 2x2: knock, DD, sonar, interrogation
    constexpr int         kSheetColumns = 2;
    constexpr float       kSheetCellSize = 1.0f / kSheetColumns;
    constexpr std::uint8_t kSheetCellOfIcon[] = { 0, 0, 3, 1, 2 };   // HUD logo id -> cell, as CallMenuImpl::SetCategoryIconText picks it

    constexpr std::size_t   kMeshMaterial         = 0xA8;
    constexpr std::size_t   kMaterialHandle       = 0x10;
    constexpr std::size_t   kMaterialMode         = 0x16;
    constexpr std::uint8_t  kMaterialFromResource = 2;
    constexpr std::uint32_t kHandleTypeMask       = 0x7F;
    constexpr std::uint32_t kMaterialHandleType   = 7;
    constexpr std::uint32_t kTechniqueHandleType  = 4;
    constexpr int           kHandleIndexShift     = 17;
    constexpr std::size_t   kManagerRecords       = 0x08;
    constexpr std::size_t   kMaterialRecordSize   = 0xD0;
    constexpr std::size_t   kTechniqueRecordSize  = 0x50;
    constexpr std::size_t   kResourceTechnique    = 0x20;
    constexpr std::size_t   kResourceVectorCount  = 0x24;
    constexpr std::size_t   kResourceVectors      = 0x30;
    constexpr std::size_t   kTechniqueParameters  = 0x28;
    constexpr std::size_t   kTechniquePacking     = 0x30;

    constexpr std::size_t   kMeshTextureCount     = 0xD8;
    constexpr std::size_t   kMeshTextureSlots     = 0xE0;
    constexpr std::size_t   kMeshTextureSlotSize  = 0x20;
    constexpr std::size_t   kMeshTextureObject    = 0x08;
    constexpr std::size_t   kLogoState            = 0x00;
    constexpr std::uint8_t  kLogoFadedOut         = 1;      // set while the HUD has the column's logo faded out

    struct DrawnLogo
    {
        void*         mesh            = nullptr;
        std::uint64_t texture         = 0;         // what we keep bound: a mod picture, the game's sheet, or 0 = never touched
        void*         boundObject     = nullptr;   // the texture object that bind put in the main slot
        bool          gameRepeatKnown = false;
        float         gameRepeat[2]   = {};
        bool          repeatChanged   = false;
        float         shownRepeat[2]  = {};
        bool          rebind          = false;   // the HUD hid the node; shown again it draws a blank square until a picture is bound
        bool          fadedOut        = false;
    };

    DrawnLogo        g_DrawnLogos[kColumnCount];
    SetTextureName_t g_SetTextureName = nullptr;
    std::uint64_t    g_LogoSheet      = 0;
    std::uint64_t    g_WarnedTexture  = 0;
    bool             g_WarnedRepeat   = false;
    bool             g_LoggedRebind   = false;

    UpdateSelectDoc_t g_Orig        = nullptr;
    void*             g_HookTarget  = nullptr;
    bool              g_GuideForced = false;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    template <typename Fn>
    Fn VirtualAt(void* object, std::size_t index)
    {
        return (*reinterpret_cast<Fn**>(object))[index];
    }

    void KeepPageGuideShown(std::uint8_t* callMenu)
    {
        void* guide = At<void*>(callMenu, kSelectGuide);
        if (!guide)
            return;

        const bool shown  = VirtualAt<IsVisible_t>(guide, kIsVisibleVtblIndex)(guide);
        const bool wanted = CallMenuItems::PageCount() > 1;
        if (wanted)
        {
            if (!shown)
            {
                VirtualAt<SetVisible_t>(guide, kSetVisibleVtblIndex)(guide, true);
                g_GuideForced = true;
            }
            return;
        }

        if (g_GuideForced && shown && At<void*>(callMenu, kHighlightedRow) == nullptr)
            VirtualAt<SetVisible_t>(guide, kSetVisibleVtblIndex)(guide, false);   // back to the game's rule once there is one page
        g_GuideForced = false;
    }

    bool ResolveLogoSwap()
    {
        if (!g_SetTextureName && gAddr.SetTextureName)
            g_SetTextureName = reinterpret_cast<SetTextureName_t>(ResolveGameAddress(gAddr.SetTextureName));
        if (!g_LogoSheet)
            g_LogoSheet = FoxHashes::PathCode64Ext(kLogoSheet);
        return g_SetTextureName && g_LogoSheet;
    }

    void SetBaseUv(void* ui, std::size_t vtblIndex, void* mesh, float u, float v)
    {
        VirtualAt<SetBaseUv_t>(ui, vtblIndex)(ui, mesh, u, v);
    }

    std::uint8_t* RecordOf(uintptr_t managerAddress, std::uint32_t handle, std::uint32_t type, std::size_t recordSize)
    {
        if (!managerAddress || (handle & kHandleTypeMask) != type)
            return nullptr;
        std::uint8_t* manager = *static_cast<std::uint8_t**>(ResolveGameAddress(managerAddress));
        std::uint8_t* records = manager ? At<std::uint8_t*>(manager, kManagerRecords) : nullptr;
        return records ? records + static_cast<std::size_t>(handle >> kHandleIndexShift) * recordSize : nullptr;
    }

    bool ReadMaterialPair(void* mesh, std::uint32_t parameterU, std::uint32_t parameterV, float (&out)[2])   // laid out as MaterialResource::BindParameter writes it
    {
        std::uint8_t* material = At<std::uint8_t*>(static_cast<std::uint8_t*>(mesh), kMeshMaterial);
        if (!material || At<std::uint8_t>(material, kMaterialMode) != kMaterialFromResource)
            return false;
        std::uint8_t* resource = RecordOf(gAddr.GrMaterialManagerInstance, At<std::uint32_t>(material, kMaterialHandle),
                                          kMaterialHandleType, kMaterialRecordSize);
        if (!resource)
            return false;
        std::uint8_t* technique = RecordOf(gAddr.GrShaderTechniqueManagerInstance, At<std::uint32_t>(resource, kResourceTechnique),
                                           kTechniqueHandleType, kTechniqueRecordSize);
        if (!technique)
            return false;

        std::uint8_t* parameters   = At<std::uint8_t*>(technique, kTechniqueParameters);
        std::uint8_t* packingTable = At<std::uint8_t*>(technique, kTechniquePacking);
        if (!parameters || !packingTable)
            return false;
        const std::uint32_t parameterCount = At<std::uint32_t>(parameters, 0);
        const std::uint32_t* names   = At<const std::uint32_t*>(parameters, 8);
        const std::uint8_t*  packing = At<const std::uint8_t*>(packingTable, 8);
        const std::uint32_t  laneCount = At<std::uint8_t>(resource, kResourceVectorCount) * 4u;

        const std::uint32_t wanted[2] = { parameterU, parameterV };
        for (int axis = 0; axis < 2; ++axis)
        {
            std::uint32_t binding = parameterCount;
            for (std::uint32_t i = 0; i < parameterCount; ++i)
                if (names[i] == wanted[axis])
                    binding = i;
            if (binding == parameterCount)
                return false;

            std::uint32_t lane = 0;
            while (lane < laneCount && packing[lane] != static_cast<std::uint8_t>(binding << 2))   // the parameter's first component
                ++lane;
            if (lane == laneCount)
                return false;
            out[axis] = At<float>(resource, kResourceVectors + lane * sizeof(float));
        }
        return true;
    }

    bool ReadBaseUvRepeat(void* mesh, float (&out)[2])
    {
        __try
        {
            return ReadMaterialPair(mesh, kBaseUvRepeatU, kBaseUvRepeatV, out) && out[0] > 0.0f && out[1] > 0.0f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ShowPictureRepeat(void* ui, void* mesh, const float (&repeat)[2], DrawnLogo& drawn)
    {
        if (!drawn.gameRepeatKnown)
        {
            drawn.gameRepeatKnown = ReadBaseUvRepeat(mesh, drawn.gameRepeat);
            if (drawn.gameRepeatKnown)
                LogInfo("CallMenu: logo UV repeat read as (%.3f, %.3f)", drawn.gameRepeat[0], drawn.gameRepeat[1]);
            else
            {
                if (!g_WarnedRepeat)
                {
                    g_WarnedRepeat = true;
                    LogWarn("CallMenu: couldn't read the logo's UV repeat, iconUvRepeat is ignored and icons show a quarter of their texture");
                }
                return;
            }
        }

        const float u = repeat[0] / kSheetCellSize * drawn.gameRepeat[0];   // the game shows a quarter of its sheet at its own repeat
        const float v = repeat[1] / kSheetCellSize * drawn.gameRepeat[1];
        const bool  isGames = u == drawn.gameRepeat[0] && v == drawn.gameRepeat[1];
        if (drawn.repeatChanged ? (u == drawn.shownRepeat[0] && v == drawn.shownRepeat[1]) : isGames)
            return;
        SetBaseUv(ui, kSetBaseUvRepeatVtblIndex, mesh, u, v);
        drawn.repeatChanged  = !isGames;
        drawn.shownRepeat[0] = u;
        drawn.shownRepeat[1] = v;
    }

    void* BoundTexture(void* mesh)   // what the logo's main slot holds right now
    {
        std::uint8_t* node = static_cast<std::uint8_t*>(mesh);
        const int count = At<int>(node, kMeshTextureCount);
        std::uint8_t* slots = At<std::uint8_t*>(node, kMeshTextureSlots);
        for (int i = 0; slots && i < count; ++i)
        {
            std::uint8_t* slot = slots + i * kMeshTextureSlotSize;
            if (At<std::uint32_t>(slot, 0) == kLogoTextureSlot)
                return At<void*>(slot, kMeshTextureObject);
        }
        return nullptr;
    }

    bool KeepBound(void* ui, void* mesh, std::uint64_t texture, DrawnLogo& drawn)
    {
        const bool stillBound = drawn.texture == texture && BoundTexture(mesh) == drawn.boundObject;
        if (stillBound && !drawn.rebind)
            return true;
        if (drawn.texture == texture && !stillBound && !g_LoggedRebind)
        {
            g_LoggedRebind = true;
            LogInfo("CallMenu: a logo came back from being hidden with another texture, its picture was bound again");
        }

        VirtualAt<PrefetchTexture_t>(ui, kPrefetchTextureVtblIndex)(ui, texture, 2);
        if (g_SetTextureName(mesh, texture, kLogoTextureSlot, 2) == 0)
        {
            if (g_WarnedTexture != texture)
            {
                g_WarnedTexture = texture;
                LogWarn("CallMenu: icon texture 0x%llX not found, is the .ftex in a pack loaded for this mission? Showing the column's own logo",
                        static_cast<unsigned long long>(texture));
            }
            return false;
        }
        drawn.texture     = texture;
        drawn.boundObject = BoundTexture(mesh);
        drawn.rebind      = false;
        return true;
    }

    bool DrawModLogo(void* ui, void* mesh, const CallMenuItems::IconPicture& picture, DrawnLogo& drawn)
    {
        if (!KeepBound(ui, mesh, picture.texture, drawn))
            return false;
        ShowPictureRepeat(ui, mesh, picture.repeat, drawn);
        const float shownU = drawn.gameRepeatKnown ? picture.repeat[0] : kSheetCellSize;
        const float shownV = drawn.gameRepeatKnown ? picture.repeat[1] : kSheetCellSize;
        SetBaseUv(ui, kSetBaseUvShiftVtblIndex, mesh,   // every frame: the HUD moves it back to its own logo whenever the logo id changes
                  picture.shift[0] - shownU + kSheetCellSize,   // measured in game: a window grown past a quarter also starts that much further on
                  picture.shift[1] - shownV + kSheetCellSize);
        return true;
    }

    void DrawGameLogo(void* ui, void* mesh, std::uint8_t shownIcon, DrawnLogo& drawn)
    {
        KeepBound(ui, mesh, g_LogoSheet, drawn);
        if (drawn.repeatChanged)
            SetBaseUv(ui, kSetBaseUvRepeatVtblIndex, mesh, drawn.gameRepeat[0], drawn.gameRepeat[1]);
        drawn.repeatChanged = false;
        if (shownIcon < sizeof(kSheetCellOfIcon))
        {
            const std::uint8_t cell = kSheetCellOfIcon[shownIcon];
            SetBaseUv(ui, kSetBaseUvShiftVtblIndex, mesh, (cell % kSheetColumns) * kSheetCellSize, (cell / kSheetColumns) * kSheetCellSize);
        }
    }

    void DrawColumnLogos(std::uint8_t* callMenu, bool gameLogosOnly)
    {
        if (!ResolveLogoSwap())
            return;

        void* ui = At<void*>(callMenu, kCallMenuUi);
        for (int column = 0; column < kColumnCount; ++column)
        {
            std::uint8_t* logo = callMenu + kColumnLogos + column * kColumnLogoSize;
            void* mesh = At<void*>(logo, kLogoMesh);
            DrawnLogo& drawn = g_DrawnLogos[column];
            if (drawn.mesh != mesh)
            {
                drawn = DrawnLogo{};   // a new HUD starts with the game's sheet
                drawn.mesh = mesh;
            }
            if (!mesh)
                continue;

            const std::uint8_t shownIcon = At<std::uint8_t>(logo, kLogoShownIcon);
            if (shownIcon == CallMenuItems::kHiddenIcon)
            {
                drawn.rebind = true;
                continue;
            }
            const CallMenuItems::IconPicture picture = gameLogosOnly ? CallMenuItems::IconPicture{} : CallMenuItems::ColumnIconPicture(column);
            if (At<std::uint8_t>(logo, kLogoState) & kLogoFadedOut)
            {
                if (!drawn.fadedOut && drawn.texture != 0)
                    VirtualAt<SetNodeVisible_t>(ui, kSetNodeVisibleVtblIndex)(ui, mesh, false);   // the HUD's fade-out doesn't reach a logo we changed either
                drawn.fadedOut = true;
                continue;
            }
            if (drawn.fadedOut)
            {
                drawn.fadedOut = false;
                if (picture.texture != 0 || drawn.texture != 0)
                {
                    At<std::uint8_t>(logo, kLogoShownIcon) = 0;   // next frame the HUD sets the logo up as a new one (shown, full alpha); a faded-in logo we changed stays empty otherwise
                    drawn.rebind = true;
                    continue;
                }
            }

            if (picture.texture != 0 && DrawModLogo(ui, mesh, picture, drawn))
                continue;
            if (drawn.texture != 0 || drawn.rebind)
                DrawGameLogo(ui, mesh, shownIcon, drawn);
        }
    }

    void __fastcall hk_UpdateSelectDoc(std::uint8_t* callMenu)
    {
        g_Orig(callMenu);
        if (!callMenu)
            return;

        const bool bypass = MissionCodeGuard::ShouldBypassHooks();
        DrawColumnLogos(callMenu, bypass);
        if (!bypass)
            KeepPageGuideShown(callMenu);   // last step of the HUD update, after the cursor code has had its say
    }
}

bool Install_CallMenuImpl_UpdateSelectDoc_Hook()
{
    if (g_HookTarget)
        return true;

    if (!gAddr.CallMenuImpl_UpdateSelectDoc)
    {
        LogError("CallMenu: CallMenuImpl::UpdateSelectDoc address missing for %s, page guide only shows over a highlighted option",
                 GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(gAddr.CallMenuImpl_UpdateSelectDoc);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_UpdateSelectDoc),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CallMenu: CallMenuImpl::UpdateSelectDoc hook failed at %p, page guide only shows over a highlighted option", target);
        return false;
    }

    g_HookTarget = target;
    return true;
}

void Uninstall_CallMenuImpl_UpdateSelectDoc_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
    g_GuideForced = false;
    for (DrawnLogo& drawn : g_DrawnLogos)
        drawn = DrawnLogo{};
}
