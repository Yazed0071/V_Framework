#pragma once

// tpp::ui::menu::MotherBaseMissionCommonData layout (Joey's structs, from GitmoHook).
struct ChangeLocationMenuParameter
{
    unsigned short LocationId;
    unsigned short MissionId;
    unsigned char paddingA[0x4];
    unsigned long long FreeMissionName;
    unsigned char Flags;
    unsigned char MbStageBaseId;
    unsigned char paddingB[0x6];
};

struct MotherBaseMissionCommonData
{
    unsigned char paddingA[0x111];
    unsigned char ChangeLocationMenuParamCount;
    unsigned char paddingB[0x6];
    ChangeLocationMenuParameter ChangeLocationMenuParams[12];
    unsigned char paddingC[0x88];
};

bool Install_ChangeLocationMenu_Hook();
bool Uninstall_ChangeLocationMenu_Hook();

void AddLocationIdToChangeLocationMenu(unsigned short locationId);
