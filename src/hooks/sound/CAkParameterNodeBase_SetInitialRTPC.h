#pragma once

#include <cstdint>

enum AudioVolumeType
{
    VOLUME_MASTER       = 0,
    VOLUME_MUSIC        = 1,
    VOLUME_VOICE        = 2,
    VOLUME_SOUND_EFFECT = 3,
    VOLUME_AMBIENCE     = 4,
    VOLUME_UI           = 5,
    VOLUME_TAPE         = 6,
    VOLUME_TYPE_COUNT   = 7,
};

constexpr int kAudioVolumeMin     = 1;
constexpr int kAudioVolumeMax     = 100;
constexpr int kAudioVolumeDefault = 50;

int  AudioVolume_Get(int type);
void AudioVolume_Commit(int type, int value);
void AudioVolume_Preview(int type, int value);
void AudioVolume_ApplySaved();
void AudioVolume_Pump();

bool Install_CAkParameterNodeBase_SetInitialRTPC_Hook();
bool Uninstall_CAkParameterNodeBase_SetInitialRTPC_Hook();
