#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

struct CaveCode
{
    std::vector<std::uint8_t> bytes;

    std::size_t Pos() const { return bytes.size(); }
    void Emit(std::initializer_list<std::uint8_t> b) { bytes.insert(bytes.end(), b); }
    void Imm64(std::uint64_t v);
    std::size_t Jcc8(std::uint8_t opcode);
    void Bind8(std::size_t jccAt);
    void JmpAbs(std::uintptr_t target);
};

struct BuddyCodeCave
{
    const char*    name = "";
    std::uint8_t*  site = nullptr;
    std::size_t    size = 0;
    std::uint8_t   original[16] = {};
    std::uint8_t*  thunk = nullptr;
    bool           installed = false;
};

bool InstallBuddyCodeCave(BuddyCodeCave& cave, std::uintptr_t siteAbs,
                          const std::uint8_t* expected, std::size_t size, const CaveCode& code);
void UninstallBuddyCodeCave(BuddyCodeCave& cave);
