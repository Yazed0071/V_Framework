#pragma once

#include <cstdint>

bool Install_DecoySystemImpl_Realize_Hook();
void Uninstall_DecoySystemImpl_Realize_Hook();
void DecoySystemImpl_NoteRecordItem(std::uint16_t record, const std::uint8_t* placed, std::uint32_t placedIndex,
                                    std::uint16_t equipId, const std::uint16_t* correlation);   // PlacedSystemImpl::CreateCorrelation made it
