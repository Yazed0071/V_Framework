#pragma once

#include <cstdint>
#include <vector>

struct lua_State;

int __cdecl l_AddToEquipIdTable(lua_State* L);

int TppEquip_GetSubIdForEquipId(int equipId);
int TppEquip_GetSupportWeaponIdForEquipId(int equipId);
std::uint32_t TppEquip_EquipTableEpoch();
void TppEquip_GetCustomWeaponEquipIds(std::vector<int>& out);
int TppEquip_ReleaseEquipRow(int equipId);
int TppEquip_FirstVanillaRowOverride();   // first vanilla equipId a mod re-rowed whose own row was not kept, 0 if none
void TppEquip_EnforceFobVanillaRows();    // FOB: re-rowed vanilla equipIds show the game's own rows; outside FOB the mod rows
void TppEquip_NoteAmmoRootParam(int eqpAmmoEquipId, int ammoId, int nativeAmmoId = 0);
bool TppEquip_WideAmmoRootReady();

struct V_ExtendedEquipRow
{
    int equipType;
    int subId;
    int block;
    std::uint64_t partsHash;
    std::uint64_t packHash;
};

bool TppEquip_GetExtendedEquipRow(int equipId, V_ExtendedEquipRow* out);
bool TppEquip_GetCustomEquipPack(int equipId, std::uint64_t* outPack);
int TppEquip_FindShieldPaintDonor(int equipId);   // vanilla shield with the custom shield's model and pack, 0 if none
bool TppEquip_GetOwnPack(int equipId, std::uint64_t* outPack);   // throwable/placed/box/item rows with EQP_BLOCK_MISSION and a pack

struct V_DecoyModels
{
    std::uint64_t pack;
    std::uint64_t canister;   // the row's .fmdl, 0 = the game's own
    std::uint64_t figure;     // decoyParts, 0 = the game's own
};

bool TppEquip_GetDecoyModels(int equipId, V_DecoyModels* out);   // own-pack rows only
bool TppEquip_BindAliasRow(int aliasId, int wideId);

void TppEquip_ReserveInfoListMirrorEarly();
bool TppEquip_EnsureInfoListMirror();

bool Install_TppEquip_ReloadEquipIdTable_Hook();
bool Uninstall_TppEquip_ReloadEquipIdTable_Hook();
