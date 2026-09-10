#pragma once

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
    std::uint64_t stepReload       = 0;
};

constexpr int kReceiverMotionGrip             = 100;
constexpr int kReceiverMotionMagazineGrip     = 101;
constexpr int kReceiverMotionMagazineGripDual = 102;
constexpr int kReceiverMotionUnderBarrelGrip  = 103;
constexpr int kReceiverMotionStepReload       = 104;

constexpr int kReceiverVariantTwoHand    = 0;
constexpr int kReceiverVariantOneHand    = 1;
constexpr int kReceiverVariantOneHandCqc = 2;
constexpr int kReceiverVariantHorse      = 3;

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

struct ReceiverMotionDesc
{
    int                 receiverId  = 0;
    std::uint64_t       playerMtar  = 0;
    std::uint64_t       playerPack  = 0;
    ReceiverMotionClips playerClips;
    ReceiverMotionClips oneHandClips;
    ReceiverMotionClips oneHandCqcClips;
    ReceiverMotionClips horseClips;
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
bool          ReceiverMotion_GetPartRowIndices(int receiverId,
                                               ReceiverPartRowIndices& out);
bool          ReceiverMotion_EnsurePartRowsPublished(int receiverId);
bool          ReceiverMotion_ReceiverTypeOverride(unsigned int setupEquipId, unsigned int* outType);
std::uint64_t ReceiverMotion_WeaponArchiveForEquip(unsigned int equipId);
std::uint64_t ReceiverMotion_PlayerClip(unsigned int receiverType, int gunUseMotionType, bool roundsLeft);
std::uint64_t ReceiverMotion_PlayerClipVariant(unsigned int receiverType, int variant, int gunUseMotionType, bool roundsLeft);
std::uint64_t ReceiverMotion_WeaponClip(unsigned int receiverType, int gunUseMotionType, bool roundsLeft);

bool Install_MotionLoaderImpl_GetMtarIds_Hook();
void Uninstall_MotionLoaderImpl_GetMtarIds_Hook();
