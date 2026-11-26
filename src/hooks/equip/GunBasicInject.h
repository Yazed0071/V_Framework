#pragma once

inline constexpr int kGunBasicParameters2StockSlots = 514;

struct lua_State;

int __cdecl l_SetGunBasic(lua_State* L);

int GunBasic_AllocateWeaponIdForName(const char* name);

bool GunBasic_ReadRowBytes(int weaponId, unsigned char* out12);
unsigned char* GunBasic_RowForFob(int weaponId);   // a vanilla weapon's live gunBasic row, for the FOB swap; no lock taken
bool GunBasic_WeaponHasNoRow(int equipId);

int GunBasic_MaxWeaponId();
int GunBasic_PackedSubIdMax();

int GunBasic_RebindWidePartsForWeapon(int weaponId);
int GunBasic_ReplayPendingForMagazine(int ammoId);
int GunBasic_PendingWideAmmoForWeapon(int weaponId);
bool GunBasic_WeaponNeedsLaneBind(int weaponId);
int GunBasic_ClearLaneFromRows(int space, int lane);
int GunBasic_ReNarrowReceiverRows(int wideRc);
int GunBasic_RefreshBorrowedParts(int ammoId, int receiverId, int weaponId);   // 0 matches any
int GunBasic_GetWideSlotLanes(int weaponId, int space, unsigned char outLanes[12]);
int GunBasic_GetLogicalPart(int weaponId, int space);
int GunBasic_LogicalMagazineForRow(const unsigned char* row);
int GunBasic_CustomMuzzleOptionForRow(const unsigned char* row);
int GunBasic_LogicalMagazineForWeapon(int weaponId, int narrowMagazine);
bool GunBasic_GetLogicalParts(int weaponId, int out11[11], const unsigned char* nativeDesc = nullptr);

bool Install_TppEquip_ReloadEquipParameterTables2_Hook();
bool Uninstall_TppEquip_ReloadEquipParameterTables2_Hook();
