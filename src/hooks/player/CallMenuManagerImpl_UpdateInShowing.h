#pragma once

#include <cstdint>

namespace CallMenuPaging
{
    int CurrentPage();
    void OpenOnFirstPage(std::uint8_t* callMenuManager);
}

bool Install_CallMenuManagerImpl_UpdateInShowing_Hook();
void Uninstall_CallMenuManagerImpl_UpdateInShowing_Hook();
