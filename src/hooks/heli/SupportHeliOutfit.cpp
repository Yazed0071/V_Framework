#include <pch.h>
#include "SupportHeliOutfit.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "FoxPath.h"
#include "MissionCodeGuard.h"
#include "SupportHeliHookInstall.h"
#include "SupportHeliParts.h"
#include "TppHeli2_Allocate.h"
#include "../../lua/LuaApi.h"

namespace
{
    using EvaluateDetail_t = std::uint64_t*(__fastcall*)(void* coloring, std::uint64_t* detail, std::int32_t userType, std::int32_t coloringType);
    using LuaFunction_t    = int(__cdecl*)(lua_State* L);
    using ApplyColor_t     = void(__fastcall*)(std::uint8_t* plugin, std::uint32_t instance, std::uint8_t* variation);
    using MeshMaterial_t   = void*(__fastcall*)(void* model, std::uint32_t meshName);
    using SearchParam_t    = void*(__fastcall*)(void* material, std::uint32_t name);
    using GetParam_t       = const float*(__fastcall*)(void* material, std::uint32_t name);
    using SetParam_t       = void(__fastcall*)(void* material, std::uint32_t name, const float* value);

    constexpr std::int32_t kHeliUser          = 8;       // tpp::gk::ColoringUserType of the support heli
    constexpr std::size_t  kDetailPack        = 0;
    constexpr std::size_t  kDetailFova        = 1;
    constexpr std::int32_t kFirstMissionCode  = 6;       // below: init and title
    constexpr std::int32_t kFobMission        = 50050;
    constexpr std::int32_t kFobMissionEvent   = 50055;
    constexpr std::size_t  kMostPackFiles     = 64;
    constexpr std::int32_t kHeliSpaces[]      = { 40010, 40020, 40050 };

    constexpr std::uint32_t kPaintSwitch       = 0xE38487FF;   // material parameter whose x picks plain or painted
    constexpr std::uint32_t kPlainBits[]       = { 0x3E808081, 0x3E828283 };   // 64/255, 65/255 (exe 0x142115C24 / 0x1422FB590)
    constexpr std::uint32_t kPaintedBits[]     = { 0x3F24A4A5, 0x3F25A5A6 };   // 164/255, 165/255 (exe 0x1422FB5A0 / 0x1422FB5B0)
    constexpr float         kSwitchTolerance   = 0.001f;
    constexpr std::size_t   kPluginWorks       = 0x40;
    constexpr std::size_t   kPluginOwner       = 0x38;
    constexpr std::size_t   kPluginDefault     = 0x60;
    constexpr std::size_t   kPluginFirst       = 0x6C;
    constexpr std::size_t   kWorkSize          = 0x38;
    constexpr std::size_t   kWorkModels        = 0x08;
    constexpr std::size_t   kSecondModel       = 0x30;
    constexpr std::size_t   kOwnerFlags        = 0x133;
    constexpr std::size_t   kVariationState    = 0x40;
    constexpr std::uint8_t  kVariationLoaded   = 2;
    constexpr std::size_t   kVariationData     = 0x60;
    constexpr std::size_t   kDataMeshCount     = 0x20;
    constexpr std::size_t   kDataMeshNames     = 0x28;

    constexpr std::uint8_t kEvaluateDetailEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    };
    constexpr std::uint8_t kPackPathsEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
    };
    constexpr std::uint8_t kApplyColorEntry[] =
    {
        0x4D, 0x85, 0xC0, 0x0F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xDC, 0x55, 0x41, 0x54, 0x41, 0x55,
    };
    constexpr std::uint8_t kApplyColorMask[] =
    {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    constexpr std::uint8_t kMeshMaterialEntry[] = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30 };
    constexpr std::uint8_t kSearchParamEntry[]  = { 0x4C, 0x8B, 0x49, 0x28, 0x44, 0x8B, 0x41, 0x20, 0x44, 0x8B, 0xD2 };
    constexpr std::uint8_t kGetParamEntry[]     = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x4C, 0x8B, 0x49, 0x28, 0x44, 0x8B };
    constexpr std::uint8_t kSetParamEntry[]     = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48 };

    EvaluateDetail_t  g_OrigEvaluateDetail = nullptr;
    LuaFunction_t     g_OrigPackPaths      = nullptr;
    void*             g_EvaluateDetailTarget = nullptr;
    void*             g_PackPathsTarget      = nullptr;
    ApplyColor_t      g_OrigApplyColor       = nullptr;
    void*             g_ApplyColorTarget     = nullptr;
    MeshMaterial_t    g_MeshMaterial         = nullptr;
    SearchParam_t     g_SearchParam          = nullptr;
    GetParam_t        g_GetParam             = nullptr;
    SetParam_t        g_SetParam             = nullptr;
    std::atomic<bool> g_MissingPackWarned{ false };

    // the outfit a heli was built with may carry its own file for a paint of the game
    std::uint64_t* __fastcall hkEvaluateDetail(void* coloring, std::uint64_t* detail, std::int32_t userType, std::int32_t coloringType)
    {
        std::uint64_t* result = g_OrigEvaluateDetail(coloring, detail, userType, coloringType);
        if (userType == kHeliUser && detail && !MissionCodeGuard::ShouldBypassHooks())
        {
#ifdef _DEBUG
            const std::uint64_t gameFova = detail[kDetailFova];
            const bool remapped = SupportHeliParts::LoadedOutfitPaint(detail[kDetailFova], detail[kDetailPack], detail[kDetailFova]);
            LogDebug("PequodPaint: colour %d, fv2 0x%llX -> 0x%llX (%s), file %p, result %s", coloringType,
                     static_cast<unsigned long long>(gameFova), static_cast<unsigned long long>(detail[kDetailFova]),
                     remapped ? "outfit" : "game", TppHeli2_ProbePequodFile(detail[kDetailFova]),
                     result == detail ? "same" : "other");   // TEMP probe
#else
            SupportHeliParts::LoadedOutfitPaint(detail[kDetailFova], detail[kDetailPack], detail[kDetailFova]);
#endif
        }
        return result;
    }

    float FloatOf(std::uint32_t bits)
    {
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    // the game's own switch, run on the meshes it does not know (it lists only the vanilla heli's five)
    void SwitchToPainted(void* model, const std::uint32_t* meshNames, std::uint8_t count)
    {
        for (std::uint8_t i = 0; i < count; ++i)
        {
            void* material = g_MeshMaterial(model, meshNames[i]);
#ifdef _DEBUG
            const bool hasSwitch = material && g_SearchParam(material, kPaintSwitch);
            LogDebug("PequodPaintSwitch: mesh 0x%08X material %p switch %s x %.4f", meshNames[i], material, hasSwitch ? "yes" : "no",
                     hasSwitch ? g_GetParam(material, kPaintSwitch)[0] : -1.0f);   // TEMP probe
#endif
            if (!material || !g_SearchParam(material, kPaintSwitch))
                continue;
            const float* current = g_GetParam(material, kPaintSwitch);
            alignas(16) float value[4] = { current[0], current[1], current[2], current[3] };
            bool plain = false;
            for (std::size_t mode = 0; mode < std::size(kPlainBits); ++mode)
            {
                if (std::fabs(current[0] - FloatOf(kPlainBits[mode])) < kSwitchTolerance)
                {
                    value[0] = FloatOf(kPaintedBits[mode]);
                    plain = true;
                    break;
                }
            }
            if (plain)
                g_SetParam(material, kPaintSwitch, value);
        }
    }

    void SwitchOutfitMaterialsSEH(std::uint8_t* plugin, std::uint32_t instance, std::uint8_t* variation)
    {
        __try
        {
            if (*(variation + kVariationState) != kVariationLoaded)
                return;
            const std::uint8_t* data = *reinterpret_cast<std::uint8_t**>(variation + kVariationData);
            if (!data)
                return;
            const std::uint8_t count = data[kDataMeshCount];
            const auto* meshNames = reinterpret_cast<const std::uint32_t*>(data + kDataMeshNames);
            const std::uint32_t first = *reinterpret_cast<std::uint32_t*>(plugin + kPluginFirst);
            std::uint8_t* work = *reinterpret_cast<std::uint8_t**>(plugin + kPluginWorks) + (instance - first) * kWorkSize;
            std::uint8_t* models = *reinterpret_cast<std::uint8_t**>(work + kWorkModels);
            if (!models)
                return;
            if (void* model = *reinterpret_cast<void**>(models))
                SwitchToPainted(model, meshNames, count);
            const std::uint8_t* owner = *reinterpret_cast<std::uint8_t**>(plugin + kPluginOwner);
            void* second = *reinterpret_cast<void**>(models + kSecondModel);
            if (owner && (owner[kOwnerFlags] & 1) == 0 && second && variation != *reinterpret_cast<std::uint8_t**>(plugin + kPluginDefault))
                SwitchToPainted(second, meshNames, count);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkApplyColor(std::uint8_t* plugin, std::uint32_t instance, std::uint8_t* variation)
    {
        g_OrigApplyColor(plugin, instance, variation);
        if (plugin && variation && !MissionCodeGuard::ShouldBypassHooks() && SupportHeliParts::HasLoadedOutfit())
            SwitchOutfitMaterialsSEH(plugin, instance, variation);
    }

    bool MaterialHelpersReady()
    {
        void* meshMaterial = gAddr.Fox_Model_GetMeshMaterialModifier ? ResolveGameAddress(gAddr.Fox_Model_GetMeshMaterialModifier) : nullptr;
        void* searchParam  = gAddr.MaterialModifier2_SearchParameterInfo ? ResolveGameAddress(gAddr.MaterialModifier2_SearchParameterInfo) : nullptr;
        void* getParam     = gAddr.MaterialModifier2_GetParameter ? ResolveGameAddress(gAddr.MaterialModifier2_GetParameter) : nullptr;
        void* setParam     = gAddr.MaterialModifier2_SetParameter ? ResolveGameAddress(gAddr.MaterialModifier2_SetParameter) : nullptr;
        if (!meshMaterial || !searchParam || !getParam || !setParam
            || CheckHookEntry(meshMaterial, kMeshMaterialEntry, sizeof(kMeshMaterialEntry)) == HookEntry::Unknown
            || CheckHookEntry(searchParam, kSearchParamEntry, sizeof(kSearchParamEntry)) == HookEntry::Unknown
            || CheckHookEntry(getParam, kGetParamEntry, sizeof(kGetParamEntry)) == HookEntry::Unknown
            || CheckHookEntry(setParam, kSetParamEntry, sizeof(kSetParamEntry)) == HookEntry::Unknown)
            return false;
        g_MeshMaterial = reinterpret_cast<MeshMaterial_t>(meshMaterial);
        g_SearchParam  = reinterpret_cast<SearchParam_t>(searchParam);
        g_GetParam     = reinterpret_cast<GetParam_t>(getParam);
        g_SetParam     = reinterpret_cast<SetParam_t>(setParam);
        return true;
    }

    bool IsHeliSpace(std::int32_t missionCode)
    {
        for (const std::int32_t heliSpace : kHeliSpaces)
        {
            if (missionCode == heliSpace)
                return true;
        }
        return false;
    }

    bool CarriesCustomParts(std::int32_t missionCode)
    {
        return missionCode >= kFirstMissionCode && missionCode != kFobMission && missionCode != kFobMissionEvent;
    }

    std::int32_t MissionCodeArgument(lua_State* L)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 1, "missionCode");
        const std::int32_t missionCode = LuaIsNumber(L, -1) ? GetLuaInt(L, -1) : 0;
        g_lua_settop(L, top);
        return missionCode;
    }

    void AppendPartPacks(lua_State* L)
    {
        const int packs = GetLuaTop(L);
        static std::string files[kMostPackFiles];
        const std::size_t count = SupportHeliParts::PackFiles(IsHeliSpace(MissionCodeArgument(L)), files, kMostPackFiles);   // the ACC rebuilds the heli on each pick
        int listed = static_cast<int>(LuaObjLen(L, packs));
        for (std::size_t i = 0; i < count; ++i)
        {
            if (!fox::PathExists(files[i].c_str()))
            {
                if (!g_MissingPackWarned.exchange(true))
                    LogWarn("SupportHeliWeapon: pack '%s' is not installed, its Pequod part keeps the default look", files[i].c_str());
                continue;
            }
            g_lua_pushnumber(L, static_cast<lua_Number>(++listed));
            PushLuaString(L, files[i].c_str());
            g_lua_settable(L, packs);
        }
    }

    int __cdecl hkPackPaths(lua_State* L)
    {
        const bool readable = L && ResolveLuaApi() && LuaType(L, 1) == LUA_TTABLE;
        const bool customParts = readable && CarriesCustomParts(MissionCodeArgument(L));
        SupportHeliParts::Part outfit{};
        const bool hasOutfit = customParts && SupportHeliParts::PickedPart(SupportHeliParts::Kind::Outfit, outfit);
        SupportHeliParts::SetLoadedOutfit(hasOutfit ? outfit.id : 0);   // the paints listed below are the ones of the heli this mission builds
        const int results = g_OrigPackPaths(L);
        if (customParts && results == 1 && LuaType(L, -1) == LUA_TTABLE)
            AppendPartPacks(L);
        return results;
    }
}

bool Install_SupportHeliOutfit_Hooks()
{
    if (!g_EvaluateDetailTarget)
    {
        g_EvaluateDetailTarget = InstallSupportHeliHook(
            "Pequod paint lookup", gAddr.ColoringSystemImpl_EvaluateDetail,
            kEvaluateDetailEntry, nullptr, sizeof(kEvaluateDetailEntry),
            reinterpret_cast<void*>(&hkEvaluateDetail), reinterpret_cast<void**>(&g_OrigEvaluateDetail),
            "custom Pequod outfits keep the game's own paint files");
    }
    if (g_EvaluateDetailTarget && !g_PackPathsTarget)
    {
        g_PackPathsTarget = InstallSupportHeliHook(
            "Pequod paint pack list", gAddr.TppColoringSystem_GetAdditionalColoringPackFilePaths,
            kPackPathsEntry, nullptr, sizeof(kPackPathsEntry),
            reinterpret_cast<void*>(&hkPackPaths), reinterpret_cast<void**>(&g_OrigPackPaths),
            "the packs of custom Pequod parts are not loaded, the heli keeps its own files");
    }
    if (g_PackPathsTarget && !g_ApplyColorTarget)
    {
        constexpr const char* kNoPaint = "custom Pequod outfits show their paints only on meshes named like the vanilla heli's";
        if (MaterialHelpersReady())
        {
            g_ApplyColorTarget = InstallSupportHeliHook(
                "Pequod paint switch", gAddr.Heli2ModelPlugin_ApplyColorVariation,
                kApplyColorEntry, kApplyColorMask, sizeof(kApplyColorEntry),
                reinterpret_cast<void*>(&hkApplyColor), reinterpret_cast<void**>(&g_OrigApplyColor), kNoPaint);
        }
        else
            LogError("SupportHeliWeapon: material code missing or changed on this build, %s", kNoPaint);
    }
    return g_EvaluateDetailTarget != nullptr && g_PackPathsTarget != nullptr;
}

void Uninstall_SupportHeliOutfit_Hooks()
{
    UninstallSupportHeliHook(g_ApplyColorTarget);
    UninstallSupportHeliHook(g_PackPathsTarget);
    UninstallSupportHeliHook(g_EvaluateDetailTarget);
}
