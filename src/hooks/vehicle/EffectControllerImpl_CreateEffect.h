#pragma once

bool Install_EffectControllerImpl_CreateEffect_Hook();
bool Uninstall_EffectControllerImpl_CreateEffect_Hook();

// Between the two calls, on this thread, a request for a slot the vehicle's effect table lacks is handed over instead of
// skipped with a warning: its world matrix (16 floats) is copied to `matrix`. EndCapture is true when one was handed over.
void EffectGuard_BeginCapture(float* matrix);
bool EffectGuard_EndCapture();
