#pragma once

#include <cstdint>


bool Install_SetEyeLampColor_Hook();
bool Uninstall_SetEyeLampColor_Hook();


// Set the eye-lamp color for a specific AI mode. `mode` is the engine's
// AI state value (typically 0..5, seen in [EyeLamp] PushEyeColor log
// lines, valid range 0..15). `pulseSpeed` is the pulse rate in Hz
// (cycles per second); 0 = steady, no pulsing.
void Set_EyeLampColor(int mode, float r, float g, float b, float pulseSpeed);


// Clear every per-mode override. Engine's natural colors resume.
void Clear_EyeLampColor();


// Disco mode — every eye lamp cycles smoothly through the full hue
// rainbow, overriding any per-mode settings until disabled.
//   speed: hue cycles per second (e.g. 2.0 = one rainbow lap every 0.5s)
void Set_EyeLampDisco(bool enabled, float speed);


// Heart light (Sahelanthropus' chest glow). Single color, no per-mode —
// the engine's natural color is driven by the boss's HP ratio. Overriding
// forces this color regardless of HP.
//   pulseSpeed: 1 = steady, 0 = normal pulse (1 Hz); lerp in between.
void Set_HeartLightColor(float r, float g, float b, float pulseSpeed);
void Clear_HeartLightColor();


// Toggle hot-path logging of every SetEyeLampColor call.
void Set_EyeLampColorLogging(bool enabled);
bool Is_EyeLampColorLogging();
