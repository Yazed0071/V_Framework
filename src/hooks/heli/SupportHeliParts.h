#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct lua_State;

namespace SupportHeliParts
{
    enum class Kind : std::uint8_t { Outfit, Gatling, SideGun };

    constexpr std::size_t kKindCount      = 3;
    constexpr std::size_t kMostParts      = 255;
    constexpr std::size_t kMostModelFiles = 64;

    // one file property of TppHeli2Parameter each (partsFiles ... fovaFiles are lists by part name, mtarFile is one file);
    // EveryList: a part name the heli loads nothing under
    enum class ModelFileKind : std::uint8_t { Parts, Fmdl, Fcnp, Gskl, Vfx, Fova, Mtar, EveryList };

    constexpr std::size_t kFileListCount = 6;

    struct ModelFile
    {
        std::uint64_t name = 0;   // the list's key: Main, GunL, GunR, RocketLauncherLL, ...
        std::uint64_t path = 0;
        ModelFileKind kind = ModelFileKind::Fmdl;
    };

    struct Part
    {
        Kind          kind      = Kind::Outfit;
        std::uint8_t  id        = 0;    // 1-based, kept per key in the state file
        std::uint32_t developId = 0;
        std::uint64_t packPath  = 0;    // the pack that holds the part's files
        std::int32_t  attackId  = -1;   // gatling / side gun: -1 keeps the vanilla attack
        std::int32_t  bulletId  = -1;   // gatling / side gun: -1 keeps BL_HeliChainGun
        bool          wholeHeli = false;   // outfit: it names every file of its heli, the Pequod's other parts are left out
        ModelFile     models[kMostModelFiles];   // files the heli loads instead of its own
        std::uint8_t  modelCount = 0;
    };

    int __cdecl Lua_RegisterSupportHeliOutfit(lua_State* L);
    int __cdecl Lua_RegisterSupportHeliGatling(lua_State* L);
    int __cdecl Lua_RegisterSupportHeliSideGun(lua_State* L);
    int __cdecl Lua_IsSupportHeliRebuildNeeded(lua_State* L);
    int __cdecl Lua_KeepSupportHeliPicks(lua_State* L);
    int __cdecl Lua_RestoreSupportHeliPicks(lua_State* L);

    bool         HasAny(Kind kind);
    std::size_t  List(Kind kind, Part* out, std::size_t capacity);   // registered parts that have an R&D row
    bool         FindByDevelopId(std::uint32_t developId, Part& out);
    std::size_t  PackFiles(bool everyPart, std::string* out, std::size_t capacity);   // the picked parts' packs, or every part's
    bool         RebuildNeeded();                      // the picks differ from the files the heli was built with

    std::uint8_t PickedId(Kind kind);                  // the part picked in the customize menu
    void         SetPicked(Kind kind, std::uint8_t id);
    bool         PickedPart(Kind kind, Part& out);     // the picked part, only while its R&D row is developed

    void         SetLoadedOutfit(std::uint8_t id);     // the outfit the heli of this mission is built with
    bool         HasLoadedOutfit();
    void         NoteHeliBuilt();                      // the picks whose model files the heli was just built with
    bool         LoadedOutfitPaint(std::uint64_t vanillaFova, std::uint64_t& pack, std::uint64_t& fova);
}
