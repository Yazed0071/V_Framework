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
        0x14147F240ull, // AddNoise
        0x1414DCB60ull, // AddNoticeInfo
        0x140989340ull, // BeginSoundSystem
        0x1473CFCD0ull, // CallImpl
        0x1473CFF10ull, // CallWithRadioType
        0x142285780ull, // CassettePlayerVtable
        0x149310440ull, // CassetteStart
        0x1414E1090ull, // CheckSightNoticeHostage
        0x140D685C0ull, // ConvertRadioTypeToLabel
        0x140D6EAA0ull, // DecrementPhaseCounter
        0x140A19030ull, // ExecCallback
        0x14006B6D0ull, // FoxLuaRegisterLibrary
        0x1400855B0ull, // FoxPath_Path
        0x142ECE7F0ull, // FoxStrHash32
        0x14C1BD310ull, // FoxStrHash64
        0x145CB8890ull, // GameOverSetVisible
        0x145E5EE70ull, // GetCurrentMissionCode
        0x146C98180ull, // GetNameIdWithGameObjectId
        0x14614A4E0ull, // GetPlayingTime
        0x14614AA30ull, // GetPlayingTrackId
        0x14614C0C0ull, // GetTrackInfoByName
        0x140DA3170ull, // GetVoiceParamWithCallSign
        0x146867240ull, // LoadPlayerVoiceFpk
        0x145CD0630ull, // LoadingScreenOrGameOverSplash2
        0x142BFFAC8ull, // MusicManager_s_instance
        0x14C1BD5D0ull, // PathHashCode
        0x140972C70ull, // PauseMusicPlayer
        0x140EF6BD0ull, // PlayOrPauseSelectedTrack
        0x140A69070ull, // RequestCorpse
        0x1409739E0ull, // ResumeMusicPlayer
        0x145F236F0ull, // SetEquipBackgroundTexture
        0x1408D78A0ull, // SetLuaFunctions
        0x141DC78F0ull, // SetTextureName
        0x140989120ull, // SoundSystemCtor
        0x140D69140ull, // StateRadio
        0x14A2ACC00ull, // StateRadioRequest
        0x1414B8D20ull, // State_ComradeAction
        0x14A140940ull, // State_EnterDownHoldup
        0x14A140C00ull, // State_EnterStandHoldup1
        0x14A141500ull, // State_EnterStandHoldupUnarmed
        0x1414BCEF0ull, // State_RecoveryTouch
        0x14A141910ull, // State_StandHoldupCancelLookToPlayer
        0x1414BCA10ull, // State_StandRecoveryHoldup
        0x14150F2C0ull, // StepRadioDiscovery
        0x146150970ull, // StopMusicPlayer
        0x149F65330ull, // UpdateOptCamo
        0x142C009F0ull, // g_SoundSystem
        0x14C1D7320ull, // lua_getfield
        0x14C1D7D40ull, // lua_gettop
        0x14C1D8C90ull, // lua_isnumber
        0x14C1D9250ull, // lua_isstring
        0x14C1DA960ull, // lua_objlen
        0x14C1DB230ull, // lua_pushboolean
        0x141A11BC0ull, // lua_pushnumber
        0x14C1E9320ull, // lua_rawgeti
        0x14C1EBBE0ull, // lua_settop
        0x141A12330ull, // lua_toboolean
        0x141A12390ull, // lua_tointeger
        0x141A123C0ull, // lua_tolstring
        0x141A12460ull, // lua_tonumber
        0x14C1ED760ull  // lua_type
    };

    static const AddressSet kSteamMstJpnDay1820MgoPatch0212_1307 =
    {
        0x0ull, // AddNoise
        0x0ull, // AddNoticeInfo
        0x0ull, // BeginSoundSystem
        0x0ull, // CallImpl
        0x0ull, // CallWithRadioType
        0x0ull, // CassettePlayerVtable
        0x0ull, // CassetteStart
        0x0ull, // CheckSightNoticeHostage
        0x0ull, // ConvertRadioTypeToLabel
        0x0ull, // DecrementPhaseCounter
        0x0ull, // ExecCallback
        0x0ull, // FoxLuaRegisterLibrary
        0x0ull, // FoxPath_Path
        0x0ull, // FoxStrHash32
        0x0ull, // FoxStrHash64
        0x0ull, // GameOverSetVisible
        0x0ull, // GetCurrentMissionCode
        0x0ull, // GetNameIdWithGameObjectId
        0x0ull, // GetPlayingTime
        0x0ull, // GetPlayingTrackId
        0x0ull, // GetTrackInfoByName
        0x0ull, // GetVoiceParamWithCallSign
        0x0ull, // LoadPlayerVoiceFpk
        0x0ull, // LoadingScreenOrGameOverSplash2
        0x0ull, // MusicManager_s_instance
        0x0ull, // PathHashCode
        0x0ull, // PauseMusicPlayer
        0x0ull, // PlayOrPauseSelectedTrack
        0x0ull, // RequestCorpse
        0x0ull, // ResumeMusicPlayer
        0x0ull, // SetEquipBackgroundTexture
        0x0ull, // SetLuaFunctions
        0x0ull, // SetTextureName
        0x0ull, // SoundSystemCtor
        0x0ull, // StateRadio
        0x0ull, // StateRadioRequest
        0x0ull, // State_ComradeAction
        0x0ull, // State_EnterDownHoldup
        0x0ull, // State_EnterStandHoldup1
        0x0ull, // State_EnterStandHoldupUnarmed
        0x0ull, // State_RecoveryTouch
        0x0ull, // State_StandHoldupCancelLookToPlayer
        0x0ull, // State_StandRecoveryHoldup
        0x0ull, // StepRadioDiscovery
        0x0ull, // StopMusicPlayer
        0x0ull, // UpdateOptCamo
        0x0ull, // g_SoundSystem
        0x0ull, // lua_getfield
        0x0ull, // lua_gettop
        0x0ull, // lua_isnumber
        0x0ull, // lua_isstring
        0x0ull, // lua_objlen
        0x0ull, // lua_pushboolean
        0x0ull, // lua_pushnumber
        0x0ull, // lua_rawgeti
        0x0ull, // lua_settop
        0x0ull, // lua_toboolean
        0x0ull, // lua_tointeger
        0x0ull, // lua_tolstring
        0x0ull, // lua_tonumber
        0x0ull  // lua_type
    };

    static std::wstring GetExeDirectory(HMODULE hGame)
    {
        wchar_t modulePath[MAX_PATH] = {};
        HMODULE module = hGame ? hGame : GetModuleHandleW(nullptr);

        if (!GetModuleFileNameW(module, modulePath, MAX_PATH))
            return L"";

        std::wstring path(modulePath);
        const std::size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return L"";

        path.resize(slash + 1);
        return path;
    }

    static std::string ReadWholeFileA(const std::wstring& path)
    {
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp)
            return "";

        std::string result;

        std::fseek(fp, 0, SEEK_END);
        const long size = std::ftell(fp);
        std::rewind(fp);

        if (size > 0)
        {
            result.resize(static_cast<std::size_t>(size));
            if (!result.empty())
            {
                const std::size_t readCount = std::fread(&result[0], 1, result.size(), fp);
                result.resize(readCount);
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
            const std::wstring versionPath = exeDir + L"version_info.txt";
            const std::string content = ReadWholeFileA(versionPath);
            if (!content.empty())
                return ToLowerAscii(content);
        }

        const std::string localContent = ReadWholeFileA(L"version_info.txt");
        if (!localContent.empty())
            return ToLowerAscii(localContent);

        return "";
    }

    static std::string GetLangFromVersionInfo(HMODULE hGame)
    {
        const std::string versionInfo = ReadVersionInfoText(hGame);
        if (versionInfo.empty())
            return "";

        const std::string prefix = "tpp_steam_mst_";
        const std::size_t found = versionInfo.find(prefix);
        if (found == std::string::npos)
            return "";

        const std::size_t langPos = found + prefix.length();
        if (langPos + 2 > versionInfo.length())
            return "";

        return versionInfo.substr(langPos, 2);
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

        const std::string lang = GetLangFromVersionInfo(hGame);
        if (lang == "jp")
        {
            Log("[AddressSet] Detected JP language build from version_info.txt.\n");
            return GameBuild::Steam_MST_JPN_DAY1820MGO_PATCH_0212_1307;
        }

        if (lang == "en")
        {
            Log("[AddressSet] Detected EN language build from version_info.txt.\n");
            return GameBuild::Steam_MST_EN_DAY1820MGO_PATCH_0212_1307;
        }

        Log("[AddressSet] Could not parse language from version_info.txt, defaulting to English address set.\n");
        return GameBuild::Steam_MST_EN_DAY1820MGO_PATCH_0212_1307;
    }

    static const AddressSet* GetAddressSetForBuild(GameBuild build)
    {
        switch (build)
        {
        case GameBuild::Steam_MST_EN_DAY1820MGO_PATCH_0212_1307:
            return &kSteamMstEnDay1820MgoPatch0212_1307;

        case GameBuild::Steam_MST_JPN_DAY1820MGO_PATCH_0212_1307:
            return &kSteamMstJpnDay1820MgoPatch0212_1307;

        default:
            return &gEmptyAddr;
        }
    }

    static bool HasRequiredCoreAddresses(const AddressSet& addr)
    {
        return
            addr.SetLuaFunctions != 0 &&
            addr.GetCurrentMissionCode != 0;
    }
}

const char* GetGameBuildName(GameBuild build)
{
    switch (build)
    {
    case GameBuild::Steam_MST_EN_DAY1820MGO_PATCH_0212_1307:
        return "tpp_steam_mst_en_day1820mgo_patch_0212_1307";

    case GameBuild::Steam_MST_JPN_DAY1820MGO_PATCH_0212_1307:
        return "tpp_steam_mst_jp_day1820mgo_patch_0212_1307";

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
        Log("[AddressSet] Failed to select a valid address set for build: %s\n", GetGameBuildName(gGameBuild));
        return false;
    }

    gAddr = *selected;
    Log("[AddressSet] Selected build: %s\n", GetGameBuildName(gGameBuild));
    return true;
}