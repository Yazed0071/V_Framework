#pragma once

namespace equip
{
    struct LockOnCategories
    {
        int soldiers = -1;
        int vehicles = -1;
    };

    void LockOn_RegisterBullet(int bulletId, int lockCount, double lockTimeSec,
                               double turnRateDeg, double minRangeMeters,
                               double maxRangeMeters,
                               double lockedSpeed, double baseSpeed,
                               double homingStartMeters,
                               const LockOnCategories* categories);
    void LockOn_RegisterBulletType(int bulletId, int lockedType, int normalType);
    bool LockOn_IsLockedNow();
    bool Install_BulletLockOn_Hooks();
    void Uninstall_BulletLockOn_Hooks();
}
