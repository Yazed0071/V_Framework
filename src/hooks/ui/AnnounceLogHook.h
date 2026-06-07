#pragma once

#include <cstdint>

bool Install_AnnounceLogHook();
bool Uninstall_AnnounceLogHook();

std::uint32_t Set_AnnounceLogSE(const char* announceLabel, std::uint32_t seId);
std::uint32_t Set_AnnounceLogEvent(const char* announceLabel, const char* eventName);
std::uint32_t Set_AnnounceLogVoice(const char* announceLabel, const char* voiceName);
std::uint32_t Set_AnnounceLogDialogue(const char* announceLabel, std::uint32_t condition,
                                      std::uint32_t chara, std::uint32_t dialogueEvent);
std::uint32_t Set_AnnounceLogSfx(const char* announceLabel, const char* eventName);
bool Register_AnnounceLogSfx(const char* eventName);
bool IsAnnounceLogSfxRegistered(const char* eventName);
