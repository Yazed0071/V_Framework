#pragma once

#include <cstdint>

namespace outfit
{
    bool InstallFaceFovaWiden();
    bool FaceFovaWidenReady();
    bool ResetWideFaceFova(void* rows);
    bool StoreWideFaceFova(void* row, const std::uint16_t (&indices)[4]);
    void ClearWideFaceFova(void* row);
    bool ReadWideFaceFova(const void* row, std::uint16_t (&indices)[4]);
}
