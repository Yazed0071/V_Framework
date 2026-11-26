#pragma once

#include <cstdint>

bool Set_LocationIconTexture(int locationId, const char* texturePath, float u, float v);
bool Set_LocationIconVanilla(int locationId, int iconLocationId);
bool Get_LocationIcon(std::uint16_t locationId, std::uint64_t& texture, float& u, float& v);

void Apply_LocationIcon(void* ui, void* mesh, std::uint16_t locationId);
