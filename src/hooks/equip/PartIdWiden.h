#pragma once

#include <cstdint>

class PartIdWidenReadGuard
{
public:
    PartIdWidenReadGuard() noexcept;
    ~PartIdWidenReadGuard() noexcept;
    PartIdWidenReadGuard(const PartIdWidenReadGuard&) = delete;
    PartIdWidenReadGuard& operator=(const PartIdWidenReadGuard&) = delete;
    explicit operator bool() const noexcept;

private:
    bool usable;
};

bool PartIdWiden_Plan();
bool PartIdWiden_IsPlanned();
bool PartIdWiden_Install();
bool PartIdWiden_Uninstall();
bool PartIdWiden_IsActive();

int  PartIdWiden_MaxPartId();
bool PartIdWiden_BuildWideDesc(const unsigned char* narrowDesc,
                               const int* wideIds12,
                               std::uint8_t* out24);
