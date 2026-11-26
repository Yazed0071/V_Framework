#pragma once

#include <cstdint>

bool Install_IdroidVoiceLines_Hooks();
void Uninstall_IdroidVoiceLines_Hooks();

struct IdroidVoiceResourceStep
{
    bool         known      = false;
    std::uint8_t developing = 0;
};
IdroidVoiceResourceStep IdroidVoice_BeforeResourceStep();                                            // from the shared Resources menu step hook
void IdroidVoice_AfterResourceStep(void* menu, const IdroidVoiceResourceStep& before);
bool SayRevengeLine();   // going into a FOB through a retaliation wormhole
