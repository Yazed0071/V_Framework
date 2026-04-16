#pragma once

#include <cstdint>

// Sets one pickable countRaw override by locator index.
// Params: locatorIndex, countRaw
bool Set_TppPickableCountRawByIndex(std::uint32_t locatorIndex, std::uint32_t countRaw);

// Gets one pickable countRaw override by locator index.
// Params: locatorIndex, outCountRaw
bool Get_TppPickableCountRawByIndex(std::uint32_t locatorIndex, std::uint16_t& outCountRaw);

// Clears all pickable countRaw overrides.
// Params: none
void Clear_TppPickableCountRawOverrides();

// Installs the pickable runtime hooks.
// Params: none
bool Install_TppPickableHooks();

// Removes the pickable runtime hooks.
// Params: none
bool Uninstall_TppPickableHooks();