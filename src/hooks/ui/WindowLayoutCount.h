#pragma once

#include <cstdint>

bool Install_WindowLayoutCount();
void Uninstall_WindowLayoutCount();
bool WindowLayoutCount_Live();

// around one CreateWindow call: the window built from this WindowInfo creates `count` layouts instead of its byte count
void WindowLayoutCount_Begin(const void* windowInfo, std::uint32_t count);
void WindowLayoutCount_End();
