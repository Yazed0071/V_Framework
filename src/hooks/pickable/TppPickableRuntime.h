#pragma once

#include <cstdint>


inline constexpr std::uint32_t kTppPickableFieldEquipId = 0;
inline constexpr std::uint32_t kTppPickableFieldCountRaw = 1;
inline constexpr std::uint32_t kTppPickableFieldSecondCountRaw = 2;
inline constexpr std::uint32_t kTppPickableFieldCountMax = 3;
inline constexpr std::uint32_t kTppPickableFieldSecondCountMax = 4;
inline constexpr std::uint32_t kTppPickableFieldInfoType = 5;
inline constexpr std::uint32_t kTppPickableFieldFlags = 6;
inline constexpr std::uint32_t kTppPickableFieldCount = 7;


bool Set_TppPickableCountRawByIndex(std::uint32_t locatorIndex, std::uint32_t countRaw);


bool Get_TppPickableCountRawByIndex(std::uint32_t locatorIndex, std::uint16_t& outCountRaw);


bool Set_TppPickableFieldByIndex(std::uint32_t locatorIndex, std::uint32_t fieldId, std::uint32_t value);


bool Get_TppPickableInfoWordsByIndex(std::uint32_t locatorIndex, std::uint16_t* outWords8);


void Clear_TppPickableOverrides();


bool Install_TppPickableHooks();


bool Uninstall_TppPickableHooks();
