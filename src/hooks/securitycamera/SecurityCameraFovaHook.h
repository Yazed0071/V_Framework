#pragma once

#include <cstdint>


bool Install_SecurityCameraFova_Hook();


bool Uninstall_SecurityCameraFova_Hook();


void Set_SecurityCameraFovaHash(std::int32_t variantIndex, std::uint64_t hash);


void Set_SecurityCameraFovaPath(std::int32_t variantIndex, const char* path);


void Clear_SecurityCameraFova(std::int32_t variantIndex);


void Clear_AllSecurityCameraFovas();


std::uint64_t Get_SecurityCameraFovaHash(std::int32_t variantIndex);


std::int32_t ResolveSecurityCameraVariantName(const char* name);


bool Set_SecurityCameraFovaFromArg(std::int32_t variantIndex, const char* fova);
