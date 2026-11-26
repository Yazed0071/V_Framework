#pragma once

#include <cstdint>

bool PartIdWiden_BeginRead();   // false when poisoned; not RAII so SetUpGunInfo can release it in __finally
void PartIdWiden_EndRead();

bool PartIdWiden_Plan();
bool PartIdWiden_IsPlanned();
bool PartIdWiden_Install();
bool PartIdWiden_Uninstall();
bool PartIdWiden_IsActive();

int  PartIdWiden_MaxPartId();
bool PartIdWiden_BuildWideDesc(const unsigned char* narrowDesc,
                               const int* wideIds12,
                               std::uint8_t* out24);
