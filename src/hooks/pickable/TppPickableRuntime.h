#pragma once

#include <cstdint>


bool Set_TppPickableCountRawByIndex(std::uint32_t locatorIndex, std::uint32_t countRaw);


bool Get_TppPickableCountRawByIndex(std::uint32_t locatorIndex, std::uint16_t& outCountRaw);


void Clear_TppPickableCountRawOverrides();


bool Install_TppPickableHooks();


bool Uninstall_TppPickableHooks();