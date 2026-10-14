#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct ReceiverMotionClips
{
    std::uint64_t hold        = 0;
    std::uint64_t fire        = 0;
    std::uint64_t reload      = 0;
    std::uint64_t reloadEmpty = 0;
    std::uint64_t cock         = 0;
    std::uint64_t dualReload   = 0;
    std::uint64_t grip         = 0;
    std::uint64_t magazineGrip = 0;
    std::uint64_t magazineGripDual = 0;
    std::uint64_t underBarrelGrip  = 0;
    std::array<std::uint64_t, 4> stepReload      = {};
    std::array<std::uint64_t, 4> stepReloadEmpty = {};
};

constexpr int kReceiverMotionGrip             = 100;
constexpr int kReceiverMotionMagazineGrip     = 101;
constexpr int kReceiverMotionMagazineGripDual = 102;
constexpr int kReceiverMotionUnderBarrelGrip  = 103;

constexpr int kReceiverVariantTwoHand    = 0;
constexpr int kReceiverVariantOneHand    = 1;
constexpr int kReceiverVariantOneHandCqc = 2;
constexpr int kReceiverVariantHorse      = 3;
constexpr int kReceiverVariantCrouch     = 4;
constexpr int kReceiverVariantOneHandCrouch = 5;

struct ReceiverPartMotionRow
{
    std::string bone;
    int    axis     = 0;
    int    moveType = 0;
    double start    = 0;
    double end      = 0;
    double value    = 0;
    bool   set      = false;
};

struct ReceiverPartMotion
{
    bool present = false;
    ReceiverPartMotionRow shoot[2];
    ReceiverPartMotionRow shootLast[2];
    ReceiverPartMotionRow poseLoaded[2];
    ReceiverPartMotionRow poseEmpty[2];
    bool casingShoot = true;
    bool casingLast  = true;
};

struct ReceiverMotionArchive
{
    std::uint64_t mtar = 0;
    std::uint64_t pack = 0;
};

struct ReceiverMotionDesc
{
    int                 receiverId  = 0;
    std::uint64_t       playerMtar  = 0;
    std::uint64_t       playerPack  = 0;
    std::vector<ReceiverMotionArchive> extraArchives;
    ReceiverMotionClips playerClips;
    ReceiverMotionClips oneHandClips;
    ReceiverMotionClips oneHandCqcClips;
    ReceiverMotionClips oneHandCrouchClips;
    ReceiverMotionClips horseClips;
    ReceiverMotionClips crouchClips;
    std::uint64_t       weaponMtar  = 0;
    ReceiverMotionClips weaponClips;
    ReceiverPartMotion  partMotion;
};

struct ReceiverPartMotionSnapshot
{
    int                receiverId = 0;
    ReceiverPartMotion motion;
};

struct ReceiverPartRowIndices
{
    bool          present   = false;
    std::uint16_t motion[4] = {};
    std::uint16_t pose[4]   = {};
    std::uint8_t  flags[2]  = {};
    bool          fromLua   = false;
};

bool          ReceiverMotion_Register(const ReceiverMotionDesc& desc);
bool          ReceiverMotion_HasFamily(int receiverId);
int           ReceiverMotion_RowByteFor(int receiverId);
void          ReceiverMotion_CollectPartMotions(std::vector<ReceiverPartMotionSnapshot>& out);
void          ReceiverMotion_SetPartRowIndices(int receiverId,
                                               const ReceiverPartRowIndices& idx);
bool          ReceiverMotion_CylinderShotDegrees(unsigned int equipId, double& outDegrees);
bool          ReceiverMotion_GetPartRowIndices(int receiverId,
                                               ReceiverPartRowIndices& out);
bool          ReceiverMotion_EnsurePartRowsPublished(int receiverId);
std::uint64_t ReceiverMotion_WeaponArchiveForEquip(unsigned int equipId);
void          ReceiverMotion_NoteGunBasicRow(unsigned int gunBasicRow, int receiverId);
std::uint64_t ReceiverMotion_PlayerClip(const void* weaponInfo, int variant, int gunUseMotionType, std::uint8_t descFlags);
std::uint64_t ReceiverMotion_WeaponClip(const void* weaponInfo, int gunUseMotionType, std::uint8_t descFlags);

bool Install_MotionLoaderImpl_GetMtarIds_Hook();
void Uninstall_MotionLoaderImpl_GetMtarIds_Hook();
