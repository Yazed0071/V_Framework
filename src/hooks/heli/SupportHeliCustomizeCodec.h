#pragma once

bool Install_SupportHeliCustomizeCodec_Hook();
void Uninstall_SupportHeliCustomizeCodec_Hook();
void SupportHeliCustomizeCodec_SwapWhenReady();   // a boot loader can start us before the game builds the codec; the Lua pump finishes the swap
