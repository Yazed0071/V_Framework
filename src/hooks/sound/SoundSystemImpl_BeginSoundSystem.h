#pragma once

#include <cstdint>


bool Install_SoundSystem_BeginSoundSystem_Hook();


bool Uninstall_SoundSystem_BeginSoundSystem_Hook();


void* GetCachedSoundSystem();


void* GetGlobalCassetteMusicPlayerFromSoundSystem();


bool RefreshGlobalCassetteMusicPlayerFromSoundSystem();


std::uint32_t GetCassettePlayingTime();


std::uint32_t GetCassettePlayingTrackId();


std::int32_t PauseCassette(std::uint32_t fadeMs);


std::int32_t ResumeCassette(std::uint32_t fadeMs);


std::int32_t StopCassette(std::uint32_t fadeMs, bool stopByUser);