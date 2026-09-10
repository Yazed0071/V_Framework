#include "pch.h"

#include <Windows.h>

#include "HookArena.h"

extern "C"
{
    VOID InitializeBuffer(VOID)
    {
    }

    VOID UninitializeBuffer(VOID)
    {
    }

    LPVOID AllocateBuffer(LPVOID pOrigin)
    {
        return HookArena::AllocateSlot(pOrigin, 0x7E000000);
    }

    VOID FreeBuffer(LPVOID pBuffer)
    {
        HookArena::FreeSlot(pBuffer);
    }

    BOOL IsExecutableAddress(LPVOID pAddress)
    {
        MEMORY_BASIC_INFORMATION mi{};
        if (VirtualQuery(pAddress, &mi, sizeof(mi)) != sizeof(mi))
            return FALSE;
        const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
                         | PAGE_EXECUTE_WRITECOPY;
        return (mi.State == MEM_COMMIT && (mi.Protect & exec)) ? TRUE : FALSE;
    }
}
