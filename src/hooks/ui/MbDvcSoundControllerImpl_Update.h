#pragma once

#include <cstdint>

bool Install_MbDvcSoundControllerImpl_Update_Hooks();
void Uninstall_MbDvcSoundControllerImpl_Update_Hooks();

bool IdroidVoice_IsReady();
bool IdroidVoice_Request(std::uint32_t condition, std::uint32_t chara = 0, std::uint32_t dialogueEvent = 0, std::uint32_t delayMs = 0);   // chara/event 0 = the iDroid's own
void IdroidVoice_OfferTake(std::uint8_t voice, std::uint32_t condition);   // call right after the game asked for voice: 50/50 says condition in its place, this once
