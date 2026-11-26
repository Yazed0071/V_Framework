#pragma once

bool Install_TppHeli2_Allocate_Hook();   // the Pequod loads the model files of the picked custom outfit, gatling and side gun
void Uninstall_TppHeli2_Allocate_Hook();
bool TppHeli2_RebuildPequodModels();      // ACC only: rebuilds the realized Pequod with the picked files, false when it could not
#ifdef _DEBUG
void* TppHeli2_ProbePequodFile(unsigned long long pathCode);   // TEMP probe
#endif
