#pragma once

bool Install_LifeControllerImpl_UpdateHost();
void Uninstall_LifeControllerImpl_UpdateHost();

bool LifeRecoveryCooldown_Set(float seconds);
bool LifeRecoveryCooldown_Get(float& seconds);
void LifeRecoveryCooldown_Reset();
