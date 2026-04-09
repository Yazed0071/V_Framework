#pragma once

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

#include "log.h"

namespace AddressSetRuntime
{
    enum class GameBuild
    {
        Unknown,
        English,
        Japanese
    };

    struct AddressSet
    {
        uintptr_t AddCassetteTapeTrack = 0;
        uintptr_t AddNoise = 0;
        uintptr_t AddNoticeInfo = 0;
        uintptr_t ArrayBaseFree = 0;
        uintptr_t BeginSoundSystem = 0;
        uintptr_t CallImpl = 0;
        uintptr_t CallWithRadioType = 0;
        uintptr_t CassettePlayerVtable = 0;
        uintptr_t CassetteStart = 0;
        uintptr_t CheckSightNoticeHostage = 0;
        uintptr_t CollectGotTapes = 0;
        uintptr_t ConvertRadioTypeToLabel = 0;
        uintptr_t CopyAndAdjustInfo = 0;
        uintptr_t DecrementPhaseCounter = 0;
        uintptr_t ExecCallback = 0;
        uintptr_t FoxLuaRegisterLibrary = 0;
        uintptr_t FoxPath_Path = 0;
        uintptr_t FoxStrHash32 = 0;
        uintptr_t FoxStrHash64 = 0;
        uintptr_t GameOverSetVisible = 0;
        uintptr_t GetCurrentMissionCode = 0;
        uintptr_t GetNameIdWithGameObjectId = 0;
        uintptr_t GetPlayingTime = 0;
        uintptr_t GetPlayingTrackId = 0;
        uintptr_t GetQuarkSystemTable = 0;
        uintptr_t GetTrackInfoByName = 0;
        uintptr_t GetVoiceLanguage = 0;
        uintptr_t GetVoiceParamWithCallSign = 0;
        uintptr_t IsGotCassetteTapeTrack = 0;
        uintptr_t KernelAllocAligned = 0;
        uintptr_t LoadPlayerVoiceFpk = 0;
        uintptr_t LoadingScreenOrGameOverSplash2 = 0;
        uintptr_t MusicManager_s_instance = 0;
        uintptr_t PathHashCode = 0;
        uintptr_t PauseMusicPlayer = 0;
        uintptr_t PlayOrPauseSelectedTrack = 0;
        uintptr_t RequestCorpse = 0;
        uintptr_t ResumeMusicPlayer = 0;
        uintptr_t SetCassetteTapeTrackNewFlag = 0;
        uintptr_t SetCurrentAlbum = 0;
        uintptr_t SetEquipBackgroundTexture = 0;
        uintptr_t SetLuaFunctions = 0;
        uintptr_t SetTextureName = 0;
        uintptr_t SetupMusicInfos = 0;
        uintptr_t SoundSystemCtor = 0;
        uintptr_t StateRadio = 0;
        uintptr_t StateRadioRequest = 0;
        uintptr_t State_ComradeAction = 0;
        uintptr_t State_EnterDownHoldup = 0;
        uintptr_t State_EnterStandHoldup1 = 0;
        uintptr_t State_EnterStandHoldupUnarmed = 0;
        uintptr_t State_RecoveryTouch = 0;
        uintptr_t State_StandHoldupCancelLookToPlayer = 0;
        uintptr_t State_StandRecoveryHoldup = 0;
        uintptr_t StepRadioDiscovery = 0;
        uintptr_t StopMusicPlayer = 0;
        uintptr_t SubtitleManager_Get = 0;
        uintptr_t UpdateOptCamo = 0;
        uintptr_t g_SoundSystem = 0;
        uintptr_t lua_getfield = 0;
        uintptr_t lua_gettop = 0;
        uintptr_t lua_isnumber = 0;
        uintptr_t lua_isstring = 0;
        uintptr_t lua_objlen = 0;
        uintptr_t lua_pushboolean = 0;
        uintptr_t lua_pushnumber = 0;
        uintptr_t lua_rawgeti = 0;
        uintptr_t lua_settop = 0;
        uintptr_t lua_toboolean = 0;
        uintptr_t lua_tointeger = 0;
        uintptr_t lua_tolstring = 0;
        uintptr_t lua_tonumber = 0;
        uintptr_t lua_type = 0;
        uintptr_t lua_pushstring = 0;
        uintptr_t lua_createtable = 0;
        uintptr_t lua_rawset = 0;
        uintptr_t lua_settable = 0;
        uintptr_t lua_pushnil = 0;
        uintptr_t lua_next = 0;
        uintptr_t lua_gettable = 0;
        uintptr_t lua_pushvalue = 0;

        uintptr_t RegisterConstantEquipIdHashTable = 0;
        uintptr_t EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2 = 0;
        uintptr_t EquipIdTableImpl_ReloadEquipIdTable = 0;
        uintptr_t TppMotherBaseManagement_RegCstDev = 0;
        uintptr_t TppMotherBaseManagement_RegFlwDev = 0;
        uintptr_t EquipIdTableImpl_GetSupportWeaponTypeId = 0;

    };

    inline GameBuild& GetGameBuild()
    {
        static GameBuild value = GameBuild::Unknown;
        return value;
    }

    inline AddressSet& GetAddressSet()
    {
        static AddressSet value{};
        return value;
    }

    inline const AddressSet& GetEnglishAddressSet()
    {
        static const AddressSet value =
        {
            0x1466A5770ull, // AddCassetteTapeTrack
            0x14147F240ull, // AddNoise
            0x1414DCB60ull, // AddNoticeInfo
            0x140015EF0ull, // ArrayBaseFree
            0x140989340ull, // BeginSoundSystem
            0x1473CFCD0ull, // CallImpl
            0x1473CFF10ull, // CallWithRadioType
            0x142285780ull, // CassettePlayerVtable
            0x149310440ull, // CassetteStart
            0x1414E1090ull, // CheckSightNoticeHostage
            0x149309EA0ull, // CollectGotTapes
            0x140D685C0ull, // ConvertRadioTypeToLabel
            0x140FB9000ull, // CopyAndAdjustInfo
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
            0x140BFF3F0ull, // GetQuarkSystemTable
            0x14614C0C0ull, // GetTrackInfoByName
            0x1404D2AD0ull, // GetVoiceLanguage
            0x140DA3170ull, // GetVoiceParamWithCallSign
            0x1466EC350ull, // IsGotCassetteTapeTrack
            0x140015F20ull, // KernelAllocAligned
            0x146867240ull, // LoadPlayerVoiceFpk
            0x145CD0630ull, // LoadingScreenOrGameOverSplash2
            0x142BFFAC8ull, // MusicManager_s_instance
            0x14C1BD5D0ull, // PathHashCode
            0x140972C70ull, // PauseMusicPlayer
            0x140EF6BD0ull, // PlayOrPauseSelectedTrack
            0x140A69070ull, // RequestCorpse
            0x1409739E0ull, // ResumeMusicPlayer
            0x140AAC670ull, // SetCassetteTapeTrackNewFlag
            0x140EF7A50ull, // SetCurrentAlbum
            0x145F236F0ull, // SetEquipBackgroundTexture
            0x1408D78A0ull, // SetLuaFunctions
            0x141DC78F0ull, // SetTextureName
            0x140974880ull, // SetupMusicInfos
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
            0x1404D2770ull, // SubtitleManager_Get
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
            0x14C1ED760ull, // lua_type
            0x14C1E7EE0ull, // lua_pushstring
            0x14C1D6320ull, // lua_createtable
            0x14C1E9CF0ull, // lua_rawset
            0x14C1EB2B0ull, // lua_settable
            0x14C1E7CC0ull, // lua_pushnil
            0x14C1DA770ull, // lua_next
            0x14C1D7C10ull, // lua_gettable
            0x14C1E87E0ull, // lua_pushvalue

            0x142C24C90ull, // RegisterConstantEquipIdHashTable
            0x140A41410ull, // EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2
            0x1464B6740ull, // EquipIdTableImpl_ReloadEquipIdTable
            0x1466F3B10ull, // TppMotherBaseManagement_RegCstDev
            0x1466F4600ull, // TppMotherBaseManagement_RegFlwDev
            0x140A29FE0ull, // EquipIdTableImpl_GetSupportWeaponTypeId

        };

        return value;
    }

    inline const AddressSet& GetJapaneseAddressSet()
    {
        static const AddressSet value =
        {
            0x0ull, // AddCassetteTapeTrack
            0x0ull, // AddNoise
            0x0ull, // AddNoticeInfo
            0x0ull, // ArrayBaseFree
            0x0ull, // BeginSoundSystem
            0x0ull, // CallImpl
            0x0ull, // CallWithRadioType
            0x0ull, // CassettePlayerVtable
            0x0ull, // CassetteStart
            0x0ull, // CheckSightNoticeHostage
            0x0ull, // CollectGotTapes
            0x0ull, // ConvertRadioTypeToLabel
            0x0ull, // CopyAndAdjustInfo
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
            0x0ull, // GetQuarkSystemTable
            0x0ull, // GetTrackInfoByName
            0x0ull, // GetVoiceLanguage
            0x0ull, // GetVoiceParamWithCallSign
            0x0ull, // IsGotCassetteTapeTrack
            0x0ull, // KernelAllocAligned
            0x0ull, // LoadPlayerVoiceFpk
            0x0ull, // LoadingScreenOrGameOverSplash2
            0x0ull, // MusicManager_s_instance
            0x0ull, // PathHashCode
            0x0ull, // PauseMusicPlayer
            0x0ull, // PlayOrPauseSelectedTrack
            0x0ull, // RequestCorpse
            0x0ull, // ResumeMusicPlayer
            0x0ull, // SetCassetteTapeTrackNewFlag
            0x0ull, // SetCurrentAlbum
            0x0ull, // SetEquipBackgroundTexture
            0x0ull, // SetLuaFunctions
            0x0ull, // SetTextureName
            0x0ull, // SetupMusicInfos
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
            0x0ull, // SubtitleManager_Get
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
            0x0ull, // lua_type
            0x0ull, // lua_pushstring
            0x0ull, // lua_createtable
            0x0ull, // lua_rawset
            0x0ull, // lua_settable
            0x0ull, // lua_pushnil
            0x0ull, // lua_next
            0x0ull, // lua_gettable
            0x0ull, // lua_pushvalue


			0x0ull, // RegisterConstantEquipIdHashTable
            0x0ull, // EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2
			0x0ull, // EquipIdTableImpl_ReloadEquipIdTable
			0x0ull, // TppMotherBaseManagement_RegCstDev
			0x0ull, // TppMotherBaseManagement_RegFlwDev
			0x0ull, // EquipIdTableImpl_GetSupportWeaponTypeId
        };

        return value;
    }

    inline std::wstring GetModuleDirectory(HMODULE hModule)
    {
        wchar_t path[MAX_PATH] = {};
        if (!GetModuleFileNameW(hModule, path, MAX_PATH))
            return L"";

        std::wstring fullPath(path);
        const size_t slash = fullPath.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return L"";

        return fullPath.substr(0, slash);
    }

    inline std::string ReadWholeFileUtf8OrAnsi(const std::wstring& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return {};

        return std::string(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
    }

    inline std::string ToLowerAscii(std::string text)
    {
        std::transform(
            text.begin(),
            text.end(),
            text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        return text;
    }

    inline GameBuild DetectGameBuildFromVersionInfo(HMODULE hGame)
    {
        const std::wstring dir = GetModuleDirectory(hGame ? hGame : GetModuleHandleW(nullptr));
        if (dir.empty())
            return GameBuild::Unknown;

        const std::wstring versionInfoPath = dir + L"\\version_info.txt";
        std::string text = ReadWholeFileUtf8OrAnsi(versionInfoPath);
        if (text.empty())
        {
            Log("[AddressSet] Failed to read version_info.txt, defaulting to English.\n");
            return GameBuild::English;
        }

        text = ToLowerAscii(text);
        Log("[AddressSet] version_info.txt = %s\n", text.c_str());

        if (text.find("mst_en") != std::string::npos)
            return GameBuild::English;

        if (text.find("mst_jp") != std::string::npos)
            return GameBuild::Japanese;

        return GameBuild::English;
    }

    inline const char* GetGameBuildName(GameBuild build)
    {
        switch (build)
        {
        case GameBuild::English:
            return "English";
        case GameBuild::Japanese:
            return "Japanese";
        default:
            return "Unknown";
        }
    }

    inline bool ResolveAddressSet(HMODULE hGame)
    {
        if (!hGame)
            return false;

        GetGameBuild() = DetectGameBuildFromVersionInfo(hGame);

        switch (GetGameBuild())
        {
        case GameBuild::English:
            GetAddressSet() = GetEnglishAddressSet();
            Log("[AddressSet] Selected English address set.\n");
            return true;

        case GameBuild::Japanese:
            GetAddressSet() = GetJapaneseAddressSet();
            Log("[AddressSet] Selected Japanese address set.\n");
            return true;

        default:
            GetAddressSet() = GetEnglishAddressSet();
            Log("[AddressSet] Unknown build, defaulting to English address set.\n");
            return true;
        }
    }
}

#define gGameBuild (::AddressSetRuntime::GetGameBuild())
#define gAddr (::AddressSetRuntime::GetAddressSet())
#define ResolveAddressSet (::AddressSetRuntime::ResolveAddressSet)
#define GetGameBuildName (::AddressSetRuntime::GetGameBuildName)
