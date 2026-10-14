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
bool TppEquip_BindAliasRow(int aliasId, int wideId);

void TppEquip_ReserveInfoListMirrorEarly();
bool TppEquip_EnsureInfoListMirror();

bool Install_TppEquip_ReloadEquipIdTable_Hook();
bool Uninstall_TppEquip_ReloadEquipIdTable_Hook();
