#pragma once

namespace equip
{
    void MultiShot_RegisterBullet(int bulletId, int ammoPerShot,
                                  int lockAmmoPerShot);
    bool Install_BulletMultiShot_Hooks();
    void Uninstall_BulletMultiShot_Hooks();
}
