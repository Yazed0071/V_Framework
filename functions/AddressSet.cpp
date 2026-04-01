#include "pch.h"
#include "AddressSet.h"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "log.h"

GameBuild gGameBuild = GameBuild::Unknown;
AddressSet gAddr{};

namespace
{
    static AddressSet gEmptyAddr{};

    static const AddressSet kSteamMstEnDay1820MgoPatch0212_1307 =
    {
        0x14147F240ull,
        0x1414DCB60ull,
        0x140989340ull,
        0x1473CFCD0ull,
        0x1473CFF10ull,
        0x142285780ull,
        0x149310440ull,
        0x1414E1090ull,
        0x140D685C0ull,
        0x140D6EAA0ull,
        0x140A19030ull,
        0x14006B6D0ull,
        0x1400855B0ull,
        0x142ECE7F0ull,
        0x14C1BD310ull,
        0x145CB8890ull,
        0x145E5EE70ull,
        0x146C98180ull,
        0x14614A4E0ull,
        0x14614AA30ull,
        0x14614C0C0ull,
        0x140DA3170ull,
        0x146867240ull,
        0x145CD0630ull,
        0x142BFFAC8ull,
        0x14C1BD5D0ull,
        0x140972C70ull,
        0x140EF6BD0ull,
        0x140A69070ull,
        0x1409739E0ull,
        0x145F236F0ull,
        0x1408D78A0ull,
        0x141DC78F0ull,
        0x140989120ull,
        0x140D69140ull,
        0x14A2ACC00ull,
        0x1414B8D20ull,
        0x14A140940ull,
        0x14A140C00ull,
        0x14A141500ull,
        0x1414BCEF0ull,
        0x14A141910ull,
        0x1414BCA10ull,
        0x14150F2C0ull,
        0x146150970ull,
        0x149F65330ull,
        0x142C009F0ull,
        0x14C1D7320ull,
        0x14C1D7D40ull,
        0x14C1D8C90ull,
        0x14C1D9250ull,
        0x14C1DA960ull,
        0x14C1DB230ull,
        0x141A11BC0ull,
        0x14C1E9320ull,
        0x14C1EBBE0ull,
        0x141A12330ull,
        0x141A12390ull,
        0x141A123C0ull,
        0x141A12460ull,
        0x14C1ED760ull,
    };

    static std::wstring GetExeDirectory(HMODULE hGame)
    {
        wchar_t modulePath[MAX_PATH] = {};
        if (!GetModuleFileNameW(hGame ? hGame : GetModuleHandleW(nullptr), modulePath, MAX_PATH))
            return L"";

        std::wstring path(modulePath);
        const std::size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return L"";

        path.resize(slash + 1);
        return path;
    }

    static std::wstring ReadWholeFileW(const std::wstring& path)
    {
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp)
            return L"";

        std::wstring result;
        std::fseek(fp, 0, SEEK_END);
        const long size = std::ftell(fp);
        std::rewind(fp);

        if (size > 0)
        {
            std::string bytes;
            bytes.resize(static_cast<std::size_t>(size));
            if (std::fread(bytes.data(), 1, bytes.size(), fp) == bytes.size())
            {
                result.assign(bytes.begin(), bytes.end());
            }
        }

        std::fclose(fp);
        return result;
    }

    static std::string ToLowerAscii(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char ch) -> char
            {
                return static_cast<char>(std::tolower(ch));
            });
        return text;
    }

    static std::string ReadVersionInfoText(HMODULE hGame)
    {
        const std::wstring exeDir = GetExeDirectory(hGame);
        if (!exeDir.empty())
        {
            const std::wstring exePath = exeDir + L"version_info.txt";
            const std::wstring content = ReadWholeFileW(exePath);
            if (!content.empty())
                return ToLowerAscii(std::string(content.begin(), content.end()));
        }

        const std::wstring localPath = L"version_info.txt";
        const std::wstring localContent = ReadWholeFileW(localPath);
        if (!localContent.empty())
            return ToLowerAscii(std::string(localContent.begin(), localContent.end()));

        return std::string();
    }

    static GameBuild DetectGameBuildFromVersionInfo(HMODULE hGame)
    {
        const std::string versionInfo = ReadVersionInfoText(hGame);
        if (versionInfo.empty())
        {
            Log("[AddressSet] version_info.txt not found, defaulting to English address set.\n");
            return GameBuild::Steam_MST_EN_DAY1820MGO_PATCH_0212_1307;
        }

        Log("[AddressSet] version_info.txt = %s\n", versionInfo.c_str());

        if (versionInfo.find("tpp_steam_mst_en_day1820mgo_patch_0212_1307") != std::string::npos)
            return GameBuild::Steam_MST_EN_DAY1820MGO_PATCH_0212_1307;

        if (versionInfo.find("mst_en") != std::string::npos)
        {
            Log("[AddressSet] Unrecognized English build string, using known English address set.\n");
            return GameBuild::Steam_MST_EN_DAY1820MGO_PATCH_0212_1307;
        }

        Log("[AddressSet] Unsupported build string, defaulting to English address set.\n");
        return GameBuild::Steam_MST_EN_DAY1820MGO_PATCH_0212_1307;
    }

    static const AddressSet* GetAddressSetForBuild(GameBuild build)
    {
        switch (build)
        {
        case GameBuild::Steam_MST_EN_DAY1820MGO_PATCH_0212_1307:
            return &kSteamMstEnDay1820MgoPatch0212_1307;
        default:
            return &gEmptyAddr;
        }
    }

    static bool HasRequiredCoreAddresses(const AddressSet& addr)
    {
        return addr.SetLuaFunctions != 0 &&
            addr.GetCurrentMissionCode != 0;
    }
}

const char* GetGameBuildName(GameBuild build)
{
    switch (build)
    {
    case GameBuild::Steam_MST_EN_DAY1820MGO_PATCH_0212_1307:
        return "tpp_steam_mst_en_day1820mgo_patch_0212_1307";
    default:
        return "unknown";
    }
}

bool ResolveAddressSet(HMODULE hGame)
{
    gGameBuild = DetectGameBuildFromVersionInfo(hGame);

    const AddressSet* selected = GetAddressSetForBuild(gGameBuild);
    if (!selected || !HasRequiredCoreAddresses(*selected))
    {
        gAddr = gEmptyAddr;
        Log("[AddressSet] Failed to select a valid address set.\n");
        return false;
    }

    gAddr = *selected;
    Log("[AddressSet] Selected build: %s\n", GetGameBuildName(gGameBuild));
    return true;
}
