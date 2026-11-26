#pragma once

#include <cstddef>
#include <cstdint>

struct lua_State;

namespace CustomVehicles
{
    constexpr std::uint8_t kFirstType = 9;   // the vehicle system has 16 body slots, the game's own vehicles use 1 to 8
    constexpr std::uint8_t kLastType  = 15;

    struct SpawnEdit
    {
        bool   typeChanged      = false;
        double requestedType    = 0.0;
        bool   hadSubType       = false;
        double requestedSubType = 0.0;
        bool   lifeAdded[5]     = {};
        bool   defenseAdded     = false;
    };

    struct SortieVehicle
    {
        std::uint8_t  type;
        std::uint8_t  reliefType;   // the game vehicle it is deployed as
        std::uint8_t  resourceId;   // that game vehicle's Mother Base resource id
        std::uint64_t reliefPack;
        std::uint64_t nameLangId;   // 0 on these two = that game vehicle's own
        std::uint64_t iconFile;
        char          name[49];     // its registered name: Mother Base's count is kept under it
    };

    void         SetHooksInstalled(bool installed);
    std::uint8_t RegisteredCount();
    void         ForgetSpawnPicks();

    std::size_t  SortieVehicles(SortieVehicle* vehicles, std::size_t capacity);
    bool         SortieVehicleOf(std::uint8_t type, SortieVehicle& sortieVehicle);
    std::uint8_t ReliefTypeOfResource(std::uint8_t resourceId);
    bool         ShowReliefPack(std::uint8_t reliefType, std::uint64_t pack);   // pack 0 = the game's own (or SetReliefPack's); true when the relief type's pack changed

    std::uint8_t TypeOfPartsFile(std::uint64_t partsFile);
    void         SetProxyType(std::uint8_t type, std::uint8_t proxyType);
    std::uint8_t TypeOfAttachmentFile(std::uint64_t attachmentFile);
    bool         ProxyTypeCode(std::uint8_t typeCode, std::uint8_t& proxyCode);
    void         ApplyBodySpecs(std::uint8_t type, std::uint8_t* body);

    bool      IsSpawnCommand(std::uint32_t command);
    SpawnEdit BeginSpawnCommand(lua_State* L);
    void      EndSpawnCommand(lua_State* L, const SpawnEdit& edit);

    int __cdecl Lua_RegisterVehicle(lua_State* L);
    int __cdecl Lua_SetSpawnChance(lua_State* L);
    int __cdecl Lua_SetReliefPack(lua_State* L);
    int __cdecl Lua_IsLoaded(lua_State* L);
}
