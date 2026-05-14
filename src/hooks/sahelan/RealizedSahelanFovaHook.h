#pragma once

#include <cstdint>

bool Install_RealizedSahelanFova_Hook();

bool Uninstall_RealizedSahelanFova_Hook();

void Set_SahelanFovaHash(std::uint64_t hash);

void Set_SahelanFovaPath(const char* path);

void Clear_SahelanFovaOverride();

std::uint64_t Get_SahelanFovaHash();
