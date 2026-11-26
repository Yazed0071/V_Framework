#include <pch.h>

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "LocationIconRegistry.h"

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    constexpr std::uint64_t kVanillaIconTexture = 0x156B77F99BE3DC3Dull;   // texture path hash of the Missions list tab icon sheet (real name unknown)
    constexpr std::uint64_t kSlotMainTexture = 0x3BBF9889ull;
    constexpr std::size_t kSlotSetMeshVisible = 0x2A8 / sizeof(void*);
    constexpr std::size_t kSlotSetMeshUvShift = 0x5A0 / sizeof(void*);
    constexpr std::size_t kSlotPrefetchTexture = 0x548 / sizeof(void*);

    using SetTextureName_t = std::uint64_t(__fastcall*)(void* mesh, std::uint64_t textureHash, std::uint64_t slotHash, int type);
    using SetMeshVisible_t = void(__fastcall*)(void* ui, void* mesh, bool visible);
    using SetMeshUvShift_t = void(__fastcall*)(void* ui, void* mesh, float u, float v);
    using PrefetchTexture_t = void(__fastcall*)(void* uixUtility, std::uint64_t textureHash, int type);
    using GetUixUtility_t = void* (__fastcall*)();

    struct LocationIcon
    {
        std::uint64_t texture = 0;
        float u = 0.0f;
        float v = 0.0f;
    };

    std::mutex g_Mutex;
    std::unordered_map<std::uint16_t, LocationIcon> g_Icons;
    std::unordered_map<std::uint64_t, void*> g_CustomTextureObjects;
    std::unordered_set<std::uint64_t> g_WarnedTextures;

    bool IsVanillaIconLocation(int locationId)
    {
        return locationId == 10 || locationId == 20 || locationId == 30 || locationId == 50;
    }

    LocationIcon VanillaIconCell(int locationId)
    {
        LocationIcon icon;
        if (locationId == 10)
        {
            icon.u = 0.5f;
        }
        else if (locationId == 20)
        {
            icon.v = 0.25f;
        }
        else if (locationId == 30)
        {
            icon.v = 0.5f;
        }
        else if (locationId == 50)
        {
            icon.u = 0.5f;
            icon.v = 0.25f;
        }
        return icon;
    }

    void PrefetchTextureSEH(std::uint64_t textureHash)
    {
        __try
        {
            const auto getUixUtility = reinterpret_cast<GetUixUtility_t>(ResolveGameAddress(gAddr.GetUixUtilityToFeedQuarkEnvironment));
            void* uixUtility = getUixUtility ? getUixUtility() : nullptr;
            if (!uixUtility)
                return;
            const auto prefetch = reinterpret_cast<PrefetchTexture_t>((*static_cast<void***>(uixUtility))[kSlotPrefetchTexture]);
            prefetch(uixUtility, textureHash, 2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool SetTextureNameSEH(void* mesh, std::uint64_t textureHash)
    {
        __try
        {
            const auto setTextureName = reinterpret_cast<SetTextureName_t>(ResolveGameAddress(gAddr.SetTextureName));
            return setTextureName && (setTextureName(mesh, textureHash, kSlotMainTexture, 2) & 0xFF) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void* MainSlotTextureSEH(void* mesh)
    {
        __try
        {
            const auto* node = static_cast<const std::uint8_t*>(mesh);
            const std::int32_t slotCount = *reinterpret_cast<const std::int32_t*>(node + 0xD8);
            const auto* slots = *reinterpret_cast<const std::uint8_t* const*>(node + 0xE0);   // 0x20 per slot: slot hash +0, texture +8
            for (std::int32_t i = 0; slots && i < slotCount && i < 64; ++i)
            {
                const std::uint8_t* slot = slots + i * 0x20;
                if (*reinterpret_cast<const std::uint32_t*>(slot) == static_cast<std::uint32_t>(kSlotMainTexture))
                    return *reinterpret_cast<void* const*>(slot + 0x08);
            }
            return nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    void ShowIconCellSEH(void* ui, void* mesh, float u, float v)
    {
        __try
        {
            void** vtable = *static_cast<void***>(ui);
            reinterpret_cast<SetMeshVisible_t>(vtable[kSlotSetMeshVisible])(ui, mesh, true);
            reinterpret_cast<SetMeshUvShift_t>(vtable[kSlotSetMeshUvShift])(ui, mesh, u, v);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void BindTexture(void* mesh, std::uint64_t textureHash)
    {
        void* current = MainSlotTextureSEH(mesh);
        const auto known = g_CustomTextureObjects.find(textureHash);
        if (current && known != g_CustomTextureObjects.end() && known->second == current)
            return;

        PrefetchTextureSEH(textureHash);
        if (SetTextureNameSEH(mesh, textureHash))
        {
            g_CustomTextureObjects[textureHash] = MainSlotTextureSEH(mesh);
            return;
        }
        if (g_WarnedTextures.insert(textureHash).second)
            LogWarn("V_TppUiCommand.SetLocationIcon: texture 0x%llX did not load, is the .ftex installed? the game's icon sheet shows instead",
                static_cast<unsigned long long>(textureHash));
    }

    void RestoreVanillaTexture(void* mesh)
    {
        void* current = MainSlotTextureSEH(mesh);
        if (!current)
            return;
        for (const auto& custom : g_CustomTextureObjects)
        {
            if (custom.second == current)
            {
                SetTextureNameSEH(mesh, kVanillaIconTexture);
                return;
            }
        }
    }

    std::string FtexPath(const char* rawPath)
    {
        std::string path(rawPath);
        const std::size_t n = path.size();
        if (n <= 5 || _stricmp(path.c_str() + n - 5, ".ftex") != 0)
            path += ".ftex";
        return path;
    }

    bool CheckCustomLocationId(int locationId)
    {
        if (locationId <= 0 || locationId > 0xFFFF)
        {
            LogWarn("V_TppUiCommand.SetLocationIcon: locationId %d is not a location id, icon not set", locationId);
            return false;
        }
        if (IsVanillaIconLocation(locationId))
        {
            LogWarn("V_TppUiCommand.SetLocationIcon: location %d already has the game's icon, icon not set", locationId);
            return false;
        }
        return true;
    }
}

bool Set_LocationIconTexture(int locationId, const char* texturePath, float u, float v)
{
    if (!CheckCustomLocationId(locationId))
        return false;
    if (!texturePath || !texturePath[0])
    {
        LogWarn("V_TppUiCommand.SetLocationIcon: location %d has no texturePath, icon not set", locationId);
        return false;
    }

    LocationIcon icon;
    icon.texture = FoxHashes::PathCode64Ext(FtexPath(texturePath));
    icon.u = u;
    icon.v = v;
    if (icon.texture == 0)
    {
        LogWarn("V_TppUiCommand.SetLocationIcon: texturePath '%s' hashed to 0, icon not set", texturePath);
        return false;
    }

    std::lock_guard<std::mutex> lock(g_Mutex);
    g_Icons[static_cast<std::uint16_t>(locationId)] = icon;
    return true;
}

bool Set_LocationIconVanilla(int locationId, int iconLocationId)
{
    if (!CheckCustomLocationId(locationId))
        return false;
    if (!IsVanillaIconLocation(iconLocationId))
    {
        LogWarn("V_TppUiCommand.SetLocationIcon: iconLocationId %d must be 10, 20, 30 or 50 (or give a texturePath), icon not set", iconLocationId);
        return false;
    }

    std::lock_guard<std::mutex> lock(g_Mutex);
    g_Icons[static_cast<std::uint16_t>(locationId)] = VanillaIconCell(iconLocationId);
    return true;
}

bool Get_LocationIcon(std::uint16_t locationId, std::uint64_t& texture, float& u, float& v)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    const auto icon = g_Icons.find(locationId);
    if (icon == g_Icons.end())
        return false;
    texture = icon->second.texture != 0 ? icon->second.texture : kVanillaIconTexture;
    u = icon->second.u;
    v = icon->second.v;
    return true;
}

void Apply_LocationIcon(void* ui, void* mesh, std::uint16_t locationId)
{
    if (!ui || !mesh)
        return;

    std::lock_guard<std::mutex> lock(g_Mutex);
    const auto icon = g_Icons.find(locationId);
    if (icon == g_Icons.end())
    {
        RestoreVanillaTexture(mesh);
        return;
    }

    if (icon->second.texture != 0)
        BindTexture(mesh, icon->second.texture);
    else
        RestoreVanillaTexture(mesh);
    ShowIconCellSEH(ui, mesh, icon->second.u, icon->second.v);
}
