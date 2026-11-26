#pragma once

#include <cstdint>

void PreApplyTextureStoragePoolPatch();
bool Install_TextureStoragePool();
void Uninstall_TextureStoragePool();
std::uint32_t GetTextureStoragePool0Size();
