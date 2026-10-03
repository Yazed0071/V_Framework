#pragma once

#include <cstddef>
#include <atomic>

struct NativeCallPatch
{
    void* target;
    void* destination;
    unsigned char expected[6];
    unsigned char size;
    unsigned char original[6];
    bool installed;
};

bool InstallNativeCallPatches(NativeCallPatch* patches, std::size_t count);
bool RemoveNativeCallPatches(NativeCallPatch* patches, std::size_t count);

struct NativeCodePatch
{
    void* target;
    unsigned char expected[6];
    unsigned char replacement[6];
    unsigned char size;
    unsigned char original[6];
    bool installed;
};

bool InstallNativeCodePatches(NativeCodePatch* patches, std::size_t count,
    std::atomic<bool>* readiness = nullptr, const void* quietBegin = nullptr,
    const void* quietEnd = nullptr);
bool RemoveNativeCodePatches(NativeCodePatch* patches, std::size_t count,
    std::atomic<bool>* readiness = nullptr, const void* quietBegin = nullptr,
    const void* quietEnd = nullptr);

bool NativePatchErrorIsTransient(unsigned long error);
const char* NativePatchErrorName(unsigned long error);
