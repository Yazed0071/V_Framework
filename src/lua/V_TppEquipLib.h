#pragma once

struct lua_State;

bool Register_V_TppEquipLibrary(lua_State* L);

#include "equip/RegisterConstantEquipId.h"
#include "equip/DeclareEQPTypes.h"
#include "equip/DeclareSWPTypes.h"
#include "equip/DeclareEQPBlocks.h"
#include "equip/DeclareSWPs.h"
#include "equip/DeclareBLs.h"
#include "equip/DeclareBLAs.h"
#include "equip/DeclareCasings.h"
#include "equip/DeclareMZs.h"
#include "equip/DeclareLTLS.h"
#include "equip/DeclareWPs.h"
#include "equip/DeclareMOs.h"
#include "equip/DeclareUBs.h"
#include "equip/DeclareAMs.h"
#include "equip/DeclareSTs.h"
#include "equip/DeclareRCs.h"
#include "equip/DeclareBAs.h"
#include "equip/DeclareSKs.h"
#include "equip/DeclareReticleUIs.h"
#include "equip/DeclareScopeUIs.h"
#include "equip/DeclareBarrelLengths.h"
#include "equip/DeclareRicochetSizes.h"
#include "equip/DeclareBulletTypes.h"
#include "equip/DeclarePenetrateLevels.h"
#include "equip/DeclareTriggers.h"
#include "equip/DeclareWeaponPaints.h"
