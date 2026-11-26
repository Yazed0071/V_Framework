#pragma once

#include <cstdint>

void HeliInterest_SetLookAt_Watch(void* heli, std::uint16_t heliType);
void HeliInterest_SetLookAt_Forget();   // a mission change frees the interest objects
void Uninstall_HeliInterest_SetLookAt_Hook();
