#pragma once

#include <cstddef>
#include <cstdint>

bool Install_OccasionalChatList_Hook();
bool Uninstall_OccasionalChatList_Hook();

void SetOccasionalChatList(const std::uint32_t* labels, std::size_t count);
void InsertToOccasionalChatList(const std::uint32_t* labels, std::size_t count);
void RemoveFromOccasionalChatList(const std::uint32_t* labels, std::size_t count);
void ClearOccasionalChatListOverride();
