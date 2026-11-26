#include <pch.h>
#include "SightManagerImpl_UpdateEspionageRadio.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "FeatureSwitch.h"
#include "Player2Impl_CallVoice.h"
#include "log.h"

namespace
{
    using UpdateEspionageRadio_t  = void(__fastcall*)(void* sightManager, void* owner);
    using GetGroupName_t          = std::uint32_t(__fastcall*)(void* radioSystem, std::uint16_t gameObjectId);
    using LightControllerUpdate_t = void(__fastcall*)(void* sunData, void* skyState, std::uint8_t flags);
    using GetQuarkSystemTable_t   = std::uint8_t*(__fastcall*)();
    using GetTransform_t          = const std::uint8_t*(__fastcall*)(void* eye);
    using ConstructGeoControl_t   = void(__fastcall*)(void* control, std::uint32_t scene);
    using GeoLineCheck_t          = void(__fastcall*)(void* control, std::uint32_t flags, const float* start, const float* end, float radius,
                                                      std::uint64_t scene);
    using GetHitPosition_t        = float*(__fastcall*)(const void* hit, float* position);
    using GetSharedInstance_t     = void*(__fastcall*)();
    using GetTypeObject_t         = std::uint8_t*(__fastcall*)(void* manager, std::uint16_t gameObjectId);
    using GetInterface_t          = std::uint8_t*(__fastcall*)(void* quark, std::uint64_t interfaceId);
    using CollectionCount_t       = std::uint32_t(__fastcall*)(void* collections);
    using CollectionInfo_t        = std::uint8_t(__fastcall*)(void* collections, std::uint32_t index, std::uint8_t* out, std::uint8_t checkPicked);
    using GetCandidateId_t        = std::uint16_t(__fastcall*)(void* radio);
    using SetCandidateId_t        = void(__fastcall*)(void* radio, std::uint16_t gameObjectId);
    using SetCandidatePosition_t  = void(__fastcall*)(void* radio, const float* position);
    using SetCandidateType_t      = void(__fastcall*)(void* radio, std::uint32_t type);
    using IsInCentreCircle_t      = bool(__fastcall*)(void* sightManager, const float* point, const float* projection, const float* view,
                                                      float ratio, float radius);
    using UpdateAutoMarking_t     = void(__fastcall*)(void* sightManager);
    using EquipTypeOf_t           = std::int32_t(__fastcall*)(void* equipTypes, std::uint16_t equipId);
    using FindRadioGroup_t        = const void*(__fastcall*)(void* radioManager, std::uint32_t group, std::uint8_t* file,
                                                             std::uint16_t* index);

    const std::uint8_t kUpdateEspionageRadioEntry[]  = { 0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x49, 0x8D,
                                                         0xAB, 0x08, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xD0, 0x01, 0x00, 0x00 };
    const std::uint8_t kGetGroupNameEntry[]          = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48,
                                                         0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0xB9, 0x00, 0xFE, 0x00, 0x00 };
    const std::uint8_t kLightControllerUpdateEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83,
                                                         0xEC, 0x20, 0x41, 0x0F, 0xB6, 0xD8, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xF1 };
    const std::uint8_t kIsInCentreCircleEntry[]      = { 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x0F, 0x29, 0x74, 0x24, 0x70, 0x0F,
                                                         0x29, 0x7C, 0x24, 0x60 };
    const std::uint8_t kUpdateAutoMarkingEntry[]     = { 0x40, 0x55, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xF8, 0xFD, 0xFF, 0xFF, 0x48, 0x81,
                                                         0xEC, 0x08, 0x03, 0x00, 0x00, 0x0F, 0x29, 0xB4, 0x24, 0xE0, 0x02, 0x00, 0x00 };
    const std::uint8_t kConstructGeoControlEntry[]   = { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x48, 0x83, 0xEC, 0x18, 0x48, 0xC7, 0x04, 0x24, 0xFE,
                                                         0xFF, 0xFF, 0xFF };
    const std::uint8_t kGeoLineCheckEntry[]          = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
                                                         0x18, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xE9, 0x48, 0x8B, 0x4C, 0x24, 0x68 };
    const std::uint8_t kGetHitPositionEntry[]        = { 0x48, 0x83, 0xEC, 0x48, 0xF3, 0x0F, 0x10, 0x41, 0x08, 0xF3, 0x0F, 0x10, 0x51, 0x04,
                                                         0x48, 0x8B, 0x41, 0x58 };

    struct alignas(16) GeoCheckControl   // fox::geo::GeoCheckControlN<1>, its constructor fills two 0x60-byte hit slots
    {
        std::uint8_t bytes[0x130];
    };

    struct BuildSites
    {
        std::uintptr_t updateEspionageRadio;     // SightManagerImpl::UpdateEspionageRadio, runs while the binoculars are up
        std::uintptr_t getGroupName;             // EspionageRadioSystemImpl::GetGroupNameFromGameObjectId
        std::uintptr_t lightControllerUpdate;    // atmosphere DirectionalLightController::Update
        std::uintptr_t isInCentreCircle;         // the vanilla picker's centre test
        std::uintptr_t starSystem;
        std::uintptr_t starSystemFirstVirtual;   // slot 0 of TppStarSystem's vtable, no other vtable points at it
        std::uintptr_t decoySystemVtable;
    };


    const BuildSites* g_Sites = nullptr;

    constexpr std::size_t kQuarkTableApp    = 0x98;
    constexpr std::size_t kAppVars          = 0x10;
    constexpr std::size_t kAppRadio         = 0x90;
    constexpr std::size_t kRadioEspionage   = 0x20;
    constexpr std::size_t kRadioManager     = 0x08;
    constexpr std::size_t kFindRadioGroupSlot = 0x2D8 / sizeof(void*);   // looks a group up in every loaded radio file
    constexpr std::size_t kAppCollections   = 0x1F8;
    constexpr std::size_t kAppPlaced        = 0x208;   // mines, C4 and the like out in the world
    constexpr std::size_t kVarsLocationCode = 0x10;
    constexpr std::uint16_t kLocationMafr     = 20;
    constexpr std::uint16_t kLocationSandMafr = 92;

    constexpr std::size_t kSightCamera       = 0x30;
    constexpr std::size_t kSightModes        = 0x2E;   // SightManagerImpl::Update leaves early while none is on
    constexpr std::size_t kCameraOwner       = 0xB8;   // what Update hands its sub-updates
    constexpr std::size_t kCameraView        = 0x170;
    constexpr std::size_t kCameraProjection  = 0x1B0;
    constexpr std::size_t kOwnerEye          = 0x360;
    constexpr std::size_t kTransformPosition = 0x10;
    constexpr float       kCentreRadius      = 0.3f;   // the vanilla picker's screen-centre circle

    constexpr std::size_t kGetCandidateIdSlot       = 0x40 / sizeof(void*);
    constexpr std::size_t kSetCandidateIdSlot       = 0x48 / sizeof(void*);
    constexpr std::size_t kSetCandidatePositionSlot = 0x38 / sizeof(void*);
    constexpr std::size_t kSetCandidateTypeSlot     = 0x58 / sizeof(void*);
    constexpr std::uint16_t kNoCandidate            = 0xFFFF;

    constexpr std::size_t kSkySunLight  = 0x80;    // light direction, pointing down from the sun
    constexpr std::size_t kSkyMoonLight = 0xA0;
    constexpr std::size_t kSkySunOn     = 0x110;   // above 0 the sky draws the sun, otherwise the moon
    constexpr std::size_t kSkyMoonOn    = 0x114;
    constexpr std::uint64_t kSkyStaleMs = 1000;

    constexpr std::size_t kStarState    = 0x2D0;
    constexpr int         kStarActive   = 2;
    constexpr std::size_t kStarModel    = 0x300;
    constexpr std::size_t kModelHidden  = 0x1A4;   // 0 while the dome is drawn
    constexpr std::size_t kStarRotation = 0x320;   // quaternion, turned with the clock

    constexpr std::uint16_t kDecoySystemId       = 68 << 9;
    constexpr std::uint16_t kDecoySystemType     = 68;
    constexpr std::size_t   kManagerGetTypeSlot  = 0x20 / sizeof(void*);
    constexpr std::size_t   kTypeObjectType      = 0x1C;
    constexpr std::size_t   kTypeObjectQuark     = 0x28;
    constexpr std::size_t   kQuarkHandleObject   = 0x10;
    constexpr std::size_t   kGetInterfaceSlot    = 0x18 / sizeof(void*);
    constexpr std::uint64_t kDecoyExecution      = 0xADC8AD2187BBull;
    constexpr std::size_t   kDecoyExecutionAt    = 0x48;
    constexpr std::size_t   kDecoyRecords        = 0x70;
    constexpr std::size_t   kDecoyRecordSize     = 0x40;
    constexpr std::size_t   kDecoyOwner          = 0x20;
    constexpr std::size_t   kDecoyFlags          = 0x2E;
    constexpr std::uint8_t  kDecoyStanding       = 0x4;
    constexpr std::size_t   kDecoyRealizedCount  = 0xA4;
    constexpr std::size_t   kDecoyRealizedRecord = 0xB0;    // record index per realized decoy, 0xFFFF when free
    constexpr std::size_t   kDecoyRealizedTurn   = 0x130;   // quaternion per realized decoy
    constexpr std::uint32_t kNoDecoyRecord       = 0xFFFF;
    constexpr int           kMaxDecoys           = 32;
    const float             kDecoyAimOffset[3]   = { 0.0f, 1.0f, 0.0f };   // the engine aims 1 m up the decoy's own axis

    constexpr std::size_t   kCollectionCountSlot = 0x10 / sizeof(void*);
    constexpr std::size_t   kCollectionInfoSlot  = 0x18 / sizeof(void*);
    constexpr std::size_t   kInfoType            = 0x16;
    constexpr std::size_t   kInfoFlags           = 0x17;
    constexpr std::uint8_t  kInfoPicked          = 0x4;
    constexpr std::uint8_t  kSmallDiamond        = 1;   // TppCollection.TYPE_DIAMOND_SMALL
    constexpr std::uint8_t  kLargeDiamond        = 2;   // TppCollection.TYPE_DIAMOND_LARGE
    constexpr std::uint8_t  kFirstPinUp          = 0x32;   // TppCollection.TYPE_POSTER_GRAVURE_V, then GRAVURE_H, MOE_V, MOE_H
    constexpr std::uint8_t  kLastPinUp           = 0x35;
    constexpr std::uint8_t  kGoldenCrescent      = 0x64;   // TppCollection.TYPE_HERB_G_CRESCENT
    constexpr std::uint8_t  kAfricanPeach        = 0x65;   // TppCollection.TYPE_HERB_A_PEACH
    constexpr std::uint8_t  kDigitalisPurpurea   = 0x66;   // TppCollection.TYPE_HERB_DIGITALIS_P
    constexpr std::uint8_t  kDigitalisLutea      = 0x67;   // TppCollection.TYPE_HERB_DIGITALIS_R
    constexpr std::uint8_t  kBlackCarrot         = 0x68;   // TppCollection.TYPE_HERB_B_CARROT
    constexpr std::uint8_t  kWormwood            = 0x69;   // TppCollection.TYPE_HERB_WORM_WOOD
    constexpr std::uint8_t  kTarragon            = 0x6A;   // TppCollection.TYPE_HERB_TARRAGON
    constexpr std::uint8_t  kHaoma               = 0x6B;   // TppCollection.TYPE_HERB_HAOMA
    constexpr int           kMaxPlants           = 24;
    constexpr float         kPlantHeight         = 0.3f;

    constexpr std::size_t   kPlacedPositions     = 0x08;    // 0x10 bytes per placed thing
    constexpr std::size_t   kPlacedEquips        = 0x18;    // 4 bytes per placed thing, the equip id in the low half
    constexpr std::size_t   kPlacedCount         = 0x60;
    constexpr std::size_t   kPlacedLocatorCount  = 0x68;    // the first ones come from the mission's locators: the enemy's
    constexpr std::size_t   kPlacedStatus        = 0x90;    // 3 bytes per placed thing
    constexpr std::size_t   kPlacedEquipTypes    = 0x170;
    constexpr std::size_t   kEquipTypesTable     = 0x08;
    constexpr std::size_t   kEquipTypeSlot       = 0x48 / sizeof(void*);
    constexpr std::uint8_t  kPlacedPresent       = 0x01;    // in the first status byte
    constexpr std::uint8_t  kPlacedGone          = 0x80;    // in the second status byte
    constexpr std::uint32_t kFirstMineType       = 10;
    constexpr std::uint32_t kMineTypes           = 5;       // types 10 to 14 are the mines
    constexpr std::uint32_t kMaxPlaced           = 0x400;
    constexpr int           kMaxPlacedNear       = 64;
    constexpr int           kMaxMines            = 16;
    constexpr float         kMineRange           = 100.0f;
    constexpr float         kMineHeight          = 0.05f;
    constexpr std::uint64_t kMineRefreshMs       = 2000;

    constexpr std::uint32_t kCommsGroup          = 0x765E14B9u;   // f1000_esrg1110
    constexpr std::uint32_t kCommsRestoredGroup  = 0x2EE3C66Fu;   // f1000_esrg2190
    constexpr std::uint32_t kMaxCollections      = 0x4000;
    constexpr int           kMaxDiamonds         = 16;
    constexpr int           kMaxPosters          = 16;
    constexpr float         kDiamondRange        = 100.0f;
    constexpr float         kDiamondHeight       = 0.2f;
    constexpr std::uint64_t kDiamondRefreshMs    = 2000;
    constexpr std::uint64_t kPinUpLineMs         = 1000;
    constexpr float         kPinUpPullBack       = 0.3f;   // the sight line stops short of the wall the poster hangs on
    constexpr float         kSamePoster          = 0.5f;
    constexpr std::uint32_t kPinUpHuh            = 0x8DDF4EDDu;   // "Huh...?"
    constexpr std::uint32_t kPinUpHnng           = 0x8DDF4EDEu;   // "Hnnnnng..."
    constexpr std::uint32_t kPinUpMmm            = 0x8DDF4EDFu;   // "Mmm..."

    constexpr std::uint32_t kGeoLineFlags     = 0x4020700;   // the Walker's stun-gun shot line: walls stop it
    constexpr std::uint64_t kMainScene        = 0xD8FD1368C292ull;   // MainScene
    constexpr std::uint64_t kGeoControlFilter = 0x70;
    constexpr std::uint64_t kGeoControlMode   = 0x80000004;
    constexpr std::size_t   kGeoControlModeAt = 0x18;
    constexpr std::size_t   kGeoHitCount      = 0x60;
    constexpr std::size_t   kGeoNearestHit    = 0x64;
    constexpr std::size_t   kGeoHits          = 0x70;
    constexpr std::size_t   kGeoHitSize       = 0x60;
    constexpr int           kGeoHitSlots      = 2;
    constexpr float         kSightSkip        = 0.5f;   // the line starts this far out, clear of Snake's own head
    constexpr float         kSightSlack       = 1.0f;   // a hit this close to the point is the target itself, a decoy's body

    constexpr float         kSkyDistance  = 100.0f;   // the sight line toward the sky stops here; at 500 m it ran into distant ground
    constexpr float         kMinElevation = 0.02f;    // about 1 degree above the horizon
    constexpr std::uint64_t kDwellMs      = 900;      // the vanilla picker's 0.9 s before a target counts
    constexpr std::uint64_t kGraceMs      = 150;

    enum Target : int
    {
        Decoy, Diamond, Sun, Moon, BigDipper, SouthernCross, Mine,
        GoldenCrescent, Haoma, AfricanPeach, Digitalis, Wormwood, Tarragon, BlackCarrot,
        TargetCount
    };

    constexpr std::uint16_t kFirstTargetId = 0x91F0;   // espionage radio point ids past the engine's 50
    constexpr std::uint32_t kTargetType    = kFirstTargetId >> 9;

    const float kBigDipper[][3] = {   // star dome model space: x = RA 0h, y = RA 6h, z = north celestial pole
        { -0.459108f,  0.115042f,  0.880900f },   // Dubhe
        { -0.535916f,  0.138997f,  0.832751f },   // Merak
        { -0.591873f,  0.015941f,  0.805874f },   // Phecda
        { -0.542930f, -0.036595f,  0.838980f },   // Megrez
        { -0.544292f, -0.130741f,  0.828645f },   // Alioth
        { -0.536543f, -0.205755f,  0.818405f },   // Mizar
        { -0.581457f, -0.294806f,  0.758286f },   // Alkaid
    };
    const float kSouthernCross[][3] = {
        { -0.449406f, -0.052394f, -0.891790f },   // Acrux
        { -0.493796f, -0.104334f, -0.863296f },   // Mimosa
        { -0.537978f, -0.073611f, -0.839739f },   // Gacrux
        { -0.517657f, -0.034260f, -0.854902f },   // Imai
    };

    UpdateEspionageRadio_t  g_OrigUpdateEspionageRadio  = nullptr;
    GetGroupName_t          g_OrigGetGroupName          = nullptr;
    LightControllerUpdate_t g_OrigLightControllerUpdate = nullptr;
    IsInCentreCircle_t      g_OrigIsInCentreCircle      = nullptr;
    UpdateAutoMarking_t     g_OrigUpdateAutoMarking     = nullptr;
    void* g_UpdateEspionageRadioTarget  = nullptr;
    void* g_GetGroupNameTarget          = nullptr;
    void* g_LightControllerUpdateTarget = nullptr;
    void* g_IsInCentreCircleTarget      = nullptr;
    void* g_UpdateAutoMarkingTarget     = nullptr;

    const char* const kNoComments = "no sky, decoy or diamond comments";

    std::atomic<std::uint8_t*>  g_SkyState{ nullptr };
    std::atomic<std::uint64_t>  g_SkyStateMs{ 0 };

    int           g_Tracked       = TargetCount;
    std::uint64_t g_TrackedSince  = 0;
    std::uint64_t g_TrackedSeen   = 0;
    int           g_Aimed         = TargetCount;
    std::uint16_t g_PlayedMission = 0;
    std::uint32_t g_PlayedTargets = 0;

    float         g_Diamonds[kMaxDiamonds][4];
    int           g_DiamondCount   = 0;
    float         g_Posters[kMaxPosters][4];
    int           g_PosterCount    = 0;
    std::uint64_t g_DiamondsMs     = 0;
    int           g_PostersLogged  = -1;
    float         g_Plants[kMaxPlants][4];
    int           g_PlantTargets[kMaxPlants];
    int           g_PlantCount     = 0;
    float         g_Mines[kMaxMines][4];
    int           g_MineCount      = 0;
    std::uint64_t g_MinesMs        = 0;
    int           g_MinesLogged    = -1;
    int           g_PlantsLogged   = -1;
    std::uint16_t g_CommsRestoredMission = 0;   // the story mission that already heard the restored comms comment
    std::atomic<bool> g_WarnedOldCommonPack{ false };
    std::uint64_t g_PinUpSeen       = 0;
    std::uint64_t g_PinUpNextMs     = 0;
    int           g_PinUpStep       = 0;   // 0 "Huh...?", 1 "Hnnnnng...", 2 "Mmm...", then either of the last two
    float         g_PinUpPoster[3]  = {};
    float         g_PinUpGreeted[3] = {};  // the last poster that got "Huh...?"
    bool          g_PinUpHasGreeted = false;

    GetQuarkSystemTable_t g_GetQuarkSystemTable  = nullptr;
    GetSharedInstance_t   g_GetGameObjectManager = nullptr;
    ConstructGeoControl_t g_ConstructGeoControl  = nullptr;
    GeoLineCheck_t        g_GeoLineCheck         = nullptr;
    GetHitPosition_t      g_GetHitPosition       = nullptr;

    struct View
    {
        alignas(16) float eye[4];
        float view[16];
        float projection[16];
        float aspect;
    };

    const BuildSites* SitesFor()
    {
        static const BuildSites sites = { gAddr.SightManagerImpl_UpdateEspionageRadio, gAddr.EspionageRadioSystemImpl_GetGroupNameFromGameObjectId, gAddr.DirectionalLightController_Update, gAddr.SightManagerImpl_IsInCentreCircle,
                                     gAddr.TppStarSystem_Instance, gAddr.TppStarSystem_FirstVirtual, gAddr.DecoySystem_Vtable };
        return &sites;
    }

    std::uint8_t* App()
    {
        if (!g_GetQuarkSystemTable)
            return nullptr;
        __try
        {
            std::uint8_t* quarkTable = g_GetQuarkSystemTable();
            return quarkTable ? *reinterpret_cast<std::uint8_t**>(quarkTable + kQuarkTableApp) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    template <typename T>
    T* Member(std::uint8_t* base, std::size_t offset)
    {
        __try
        {
            return base ? *reinterpret_cast<T**>(base + offset) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool InAfrica(std::uint8_t* app)
    {
        __try
        {
            const std::uint8_t* vars = *reinterpret_cast<const std::uint8_t* const*>(app + kAppVars);
            const std::uint16_t location = vars ? *reinterpret_cast<const std::uint16_t*>(vars + kVarsLocationCode) : 0;
            return location == kLocationMafr || location == kLocationSandMafr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadView(void* sightManager, void* owner, View& out)
    {
        __try
        {
            const std::uint8_t* camera = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(sightManager) + kSightCamera);
            void* eye = *reinterpret_cast<void* const*>(static_cast<const std::uint8_t*>(owner) + kOwnerEye);
            if (!camera || !eye)
                return false;
            const std::uint8_t* transform = (*reinterpret_cast<GetTransform_t* const*>(eye))[0](eye);
            if (!transform)
                return false;
            const float* position = reinterpret_cast<const float*>(transform + kTransformPosition);
            const float* view = reinterpret_cast<const float*>(camera + kCameraView);
            const float* projection = reinterpret_cast<const float*>(camera + kCameraProjection);
            for (int i = 0; i < 4; ++i)
                out.eye[i] = position[i];
            for (int i = 0; i < 16; ++i)
            {
                out.view[i] = view[i];
                out.projection[i] = projection[i];
            }
            out.aspect = projection[5] != 0.0f ? projection[0] / projection[5] : 1.0f;   // height over width; vanilla's whole-number division gives 0, so up/down never counted
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // the vanilla centre circle (0x14127EEB0), kept round on screen and false for points behind the camera
    bool OffCentre(const View& v, const float point[3], float& offCentre)
    {
        float viewed[4];
        for (int k = 0; k < 4; ++k)
            viewed[k] = v.view[k] * point[0] + v.view[4 + k] * point[1] + v.view[8 + k] * point[2] + v.view[12 + k];
        float clip[4];
        for (int k = 0; k < 4; ++k)
            clip[k] = v.projection[k] * viewed[0] + v.projection[4 + k] * viewed[1] + v.projection[8 + k] * viewed[2]
                    + v.projection[12 + k] * viewed[3];
        if (clip[3] <= 0.0f)
            return false;
        const float x = clip[0] / clip[3];
        const float y = clip[1] / clip[3] * v.aspect;
        offCentre = x * x + y * y;
        return offCentre <= kCentreRadius * kCentreRadius;
    }

    // the shot-impact line check: anything hit short of the point blocks the view
    bool SightLineClear(const View& v, const float point[3])
    {
        if (!g_ConstructGeoControl || !g_GeoLineCheck || !g_GetHitPosition)
            return false;
        const float dx = point[0] - v.eye[0], dy = point[1] - v.eye[1], dz = point[2] - v.eye[2];
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length <= kSightSkip + kSightSlack)
            return true;
        const float skip = kSightSkip / length;
        alignas(16) float start[4] = { v.eye[0] + dx * skip, v.eye[1] + dy * skip, v.eye[2] + dz * skip, 0.0f };
        alignas(16) float end[4]   = { point[0], point[1], point[2], 0.0f };
        alignas(16) float hit[4]   = {};
        GeoCheckControl control;
        __try
        {
            g_ConstructGeoControl(&control, 0);
            *reinterpret_cast<std::uint64_t*>(control.bytes) = kGeoControlFilter;
            *reinterpret_cast<std::uint64_t*>(control.bytes + kGeoControlModeAt) = kGeoControlMode;
            g_GeoLineCheck(&control, kGeoLineFlags, start, end, 0.0f, kMainScene);
            const int hits = *reinterpret_cast<const std::int32_t*>(control.bytes + kGeoHitCount);
            const int nearest = *reinterpret_cast<const std::int32_t*>(control.bytes + kGeoNearestHit);
            if (hits <= 0 || nearest < 0 || nearest >= kGeoHitSlots)
                return true;
            g_GetHitPosition(control.bytes + kGeoHits + nearest * kGeoHitSize, hit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        const float hx = hit[0] - point[0], hy = hit[1] - point[1], hz = hit[2] - point[2];
        return hx * hx + hy * hy + hz * hz <= kSightSlack * kSightSlack;
    }

    bool AlreadyPlayed(int target)   // story missions play each one once, like vanilla's intel radio
    {
        const std::uint16_t mission = MissionCodeGuard::GetCurrentMissionCode();
        if (mission / 10000 == 3)
            return false;
        return mission == g_PlayedMission && ((g_PlayedTargets >> target) & 1u) != 0;
    }

    void MarkPlayed(int target)
    {
        const std::uint16_t mission = MissionCodeGuard::GetCurrentMissionCode();
        if (mission != g_PlayedMission)
        {
            g_PlayedMission = mission;
            g_PlayedTargets = 0;
        }
        g_PlayedTargets |= 1u << target;
    }

    struct Pick
    {
        alignas(16) float point[4] = {};
        int   target    = TargetCount;
        float offCentre = 0.0f;
    };

    // whatever sits closest to the screen centre wins, so the thing aimed at is the one commented on
    void Consider(const View& v, int target, const float point[3], Pick& pick)
    {
        float offCentre = 0.0f;
        if (AlreadyPlayed(target) || !OffCentre(v, point, offCentre)
            || (pick.target != TargetCount && offCentre >= pick.offCentre) || !SightLineClear(v, point))
            return;
        pick.target = target;
        pick.offCentre = offCentre;
        for (int k = 0; k < 3; ++k)
            pick.point[k] = point[k];
    }

    void ConsiderSky(const View& v, int target, const float direction[3], Pick& pick)
    {
        if (direction[1] < kMinElevation)
            return;
        float point[3];
        for (int k = 0; k < 3; ++k)
            point[k] = v.eye[k] + direction[k] * kSkyDistance;
        Consider(v, target, point, pick);
    }

    void RotateByQuaternion(const float q[4], const float v[3], float out[3])
    {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        out[0] = (1 - 2 * y * y - 2 * z * z) * v[0] + 2 * (x * y - z * w) * v[1] + 2 * (x * z + y * w) * v[2];
        out[1] = 2 * (x * y + z * w) * v[0] + (1 - 2 * z * z - 2 * x * x) * v[1] + 2 * (y * z - x * w) * v[2];
        out[2] = 2 * (x * z - y * w) * v[0] + 2 * (y * z + x * w) * v[1] + (1 - 2 * x * x - 2 * y * y) * v[2];
    }

    void ConsiderStars(const View& v, int target, const float (*stars)[3], int count, const float rotation[4], Pick& pick)
    {
        for (int i = 0; i < count; ++i)
        {
            float direction[3];
            RotateByQuaternion(rotation, stars[i], direction);
            ConsiderSky(v, target, direction, pick);
        }
    }

    bool ReadStarRotation(float rotation[4])
    {
        __try
        {
            const std::uint8_t* stars = *reinterpret_cast<const std::uint8_t* const*>(ResolveGameAddress(g_Sites->starSystem));
            if (!stars || **reinterpret_cast<void* const* const*>(stars) != ResolveGameAddress(g_Sites->starSystemFirstVirtual)
                || *reinterpret_cast<const int*>(stars + kStarState) != kStarActive)
                return false;
            const std::uint8_t* model = *reinterpret_cast<const std::uint8_t* const*>(stars + kStarModel);
            if (!model || *reinterpret_cast<const int*>(model + kModelHidden) != 0)
                return false;
            for (int i = 0; i < 4; ++i)
                rotation[i] = reinterpret_cast<const float*>(stars + kStarRotation)[i];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadSky(float sun[3], bool& sunUp, float moon[3], bool& moonUp)
    {
        std::uint8_t* sky = g_SkyState.load(std::memory_order_relaxed);
        if (!sky || GetTickCount64() - g_SkyStateMs.load(std::memory_order_relaxed) > kSkyStaleMs)
            return false;
        __try
        {
            const float* sunLight = reinterpret_cast<const float*>(sky + kSkySunLight);
            const float* moonLight = reinterpret_cast<const float*>(sky + kSkyMoonLight);
            const float sunLength = std::sqrt(sunLight[0] * sunLight[0] + sunLight[1] * sunLight[1] + sunLight[2] * sunLight[2]);
            const float moonLength = std::sqrt(moonLight[0] * moonLight[0] + moonLight[1] * moonLight[1] + moonLight[2] * moonLight[2]);
            if (sunLength <= 0.0f || moonLength <= 0.0f)
                return false;
            for (int k = 0; k < 3; ++k)
            {
                sun[k] = -sunLight[k] / sunLength;
                moon[k] = -moonLight[k] / moonLength;
            }
            sunUp = *reinterpret_cast<const float*>(sky + kSkySunOn) > 0.0f;
            moonUp = !sunUp && *reinterpret_cast<const float*>(sky + kSkyMoonOn) > 0.0f;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t* DecoySystem()
    {
        if (!g_GetGameObjectManager)
            return nullptr;
        __try
        {
            void* manager = g_GetGameObjectManager();
            if (!manager)
                return nullptr;
            std::uint8_t* typeObject = (*reinterpret_cast<GetTypeObject_t* const*>(manager))[kManagerGetTypeSlot](manager, kDecoySystemId);
            if (!typeObject || *reinterpret_cast<void* const*>(typeObject) != ResolveGameAddress(g_Sites->decoySystemVtable)
                || *reinterpret_cast<const std::uint16_t*>(typeObject + kTypeObjectType) != kDecoySystemType)
                return nullptr;
            const std::uint8_t* handle = *reinterpret_cast<const std::uint8_t* const*>(typeObject + kTypeObjectQuark);
            std::uint8_t* decoys = handle ? *reinterpret_cast<std::uint8_t* const*>(handle + kQuarkHandleObject) : nullptr;
            if (!decoys)
                return nullptr;
            const auto getInterface = (*reinterpret_cast<GetInterface_t* const*>(decoys))[kGetInterfaceSlot];
            return getInterface(decoys, kDecoyExecution) == decoys + kDecoyExecutionAt ? decoys : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    // the decoys that have a model out in the world, at the point the engine's own GetRealizedDecoyRotatePos gives
    int ReadDecoys(float (*out)[4])
    {
        std::uint8_t* decoys = DecoySystem();
        if (!decoys)
            return 0;
        int n = 0;
        __try
        {
            const std::uint8_t* records = *reinterpret_cast<const std::uint8_t* const*>(decoys + kDecoyRecords);
            const std::uint32_t* realized = *reinterpret_cast<const std::uint32_t* const*>(decoys + kDecoyRealizedRecord);
            const float* turns = *reinterpret_cast<const float* const*>(decoys + kDecoyRealizedTurn);
            const std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(decoys + kDecoyRealizedCount);
            for (std::uint32_t i = 0; records && realized && turns && i < count && n < kMaxDecoys; ++i)
            {
                if (realized[i] == kNoDecoyRecord)
                    continue;
                const std::uint8_t* record = records + realized[i] * kDecoyRecordSize;
                if (*reinterpret_cast<const std::int16_t*>(record + kDecoyOwner) == -1 || !(record[kDecoyFlags] & kDecoyStanding))
                    continue;
                float offset[3];
                RotateByQuaternion(turns + i * 4, kDecoyAimOffset, offset);
                const float* position = reinterpret_cast<const float*>(record);
                for (int k = 0; k < 3; ++k)
                    out[n][k] = position[k] + offset[k];
                out[n][3] = 0.0f;
                ++n;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return n;
    }

    // keeps the nearest `max` points of one kind
    int KeepNearest(float (*list)[4], float* distances, int& count, int max, const float* position, float distance, float height)
    {
        int slot = count < max ? count++ : -1;
        if (slot < 0)
        {
            int farthest = 0;
            for (int k = 1; k < max; ++k)
                if (distances[k] > distances[farthest])
                    farthest = k;
            if (distances[farthest] <= distance)
                return -1;
            slot = farthest;
        }
        distances[slot] = distance;
        list[slot][0] = position[0];
        list[slot][1] = position[1] + height;
        list[slot][2] = position[2];
        list[slot][3] = 0.0f;
        return slot;
    }

    int PlantTarget(std::uint8_t collectionType)   // the game's own pairing, TppTutorial.PlantRadioMatchTable
    {
        switch (collectionType)
        {
        case kGoldenCrescent:    return GoldenCrescent;
        case kAfricanPeach:      return AfricanPeach;
        case kDigitalisPurpurea:
        case kDigitalisLutea:    return Digitalis;
        case kBlackCarrot:       return BlackCarrot;
        case kWormwood:          return Wormwood;
        case kTarragon:          return Tarragon;
        case kHaoma:             return Haoma;
        default:                 return TargetCount;
        }
    }

    // the engine calls stay outside SEH: the collection info hook takes a lock
    void RefreshCollections(std::uint8_t* app, const float eye[3])
    {
        const std::uint64_t now = GetTickCount64();
        if (now - g_DiamondsMs < kDiamondRefreshMs)
            return;
        g_DiamondsMs = now;
        g_DiamondCount = 0;
        g_PosterCount = 0;
        g_PlantCount = 0;
        void* collections = Member<void>(app, kAppCollections);
        void** vtable = collections ? Member<void*>(static_cast<std::uint8_t*>(collections), 0) : nullptr;
        if (!vtable)
            return;
        const auto count = reinterpret_cast<CollectionCount_t>(vtable[kCollectionCountSlot]);
        const auto getInfo = reinterpret_cast<CollectionInfo_t>(vtable[kCollectionInfoSlot]);
        std::uint32_t total = count(collections) & 0xFFFF;
        if (total > kMaxCollections)
            total = kMaxCollections;
        float diamondDistances[kMaxDiamonds];
        float posterDistances[kMaxPosters];
        float plantDistances[kMaxPlants];
        alignas(16) std::uint8_t info[0x20];   // the position is stored with movaps
        for (std::uint32_t i = 0; i < total; ++i)
        {
            if (!getInfo(collections, i, info, 0) || (info[kInfoFlags] & kInfoPicked))
                continue;
            const std::uint8_t type = info[kInfoType];
            const bool diamond = type == kSmallDiamond || type == kLargeDiamond;
            const bool pinUp = type >= kFirstPinUp && type <= kLastPinUp;
            const int plant = PlantTarget(type);
            if (!diamond && !pinUp && plant == TargetCount)
                continue;
            const float* position = reinterpret_cast<const float*>(info);
            const float dx = position[0] - eye[0], dy = position[1] - eye[1], dz = position[2] - eye[2];
            const float distance = dx * dx + dy * dy + dz * dz;
            if (distance > kDiamondRange * kDiamondRange)
                continue;
            if (diamond)
                KeepNearest(g_Diamonds, diamondDistances, g_DiamondCount, kMaxDiamonds, position, distance, kDiamondHeight);
            else if (pinUp)
                KeepNearest(g_Posters, posterDistances, g_PosterCount, kMaxPosters, position, distance, 0.0f);
            else
            {
                const int slot = KeepNearest(g_Plants, plantDistances, g_PlantCount, kMaxPlants, position, distance, kPlantHeight);
                if (slot >= 0)
                    g_PlantTargets[slot] = plant;
            }
        }
        if (g_PosterCount != g_PostersLogged)
        {
            g_PostersLogged = g_PosterCount;
        }
    }

    struct PlacedThing
    {
        float         position[3];
        std::uint16_t equipId;
    };

    // what the mission's own locators put down that is still out there and near, whatever it is
    int ReadLocatorPlaced_SEH(const std::uint8_t* placed, const float eye[3], PlacedThing* out, int max, void*& equipTypes)
    {
        int n = 0;
        __try
        {
            const std::uint8_t* typeHolder = *reinterpret_cast<const std::uint8_t* const*>(placed + kPlacedEquipTypes);
            equipTypes = typeHolder ? *reinterpret_cast<void* const*>(typeHolder + kEquipTypesTable) : nullptr;
            const float* positions = *reinterpret_cast<const float* const*>(placed + kPlacedPositions);
            const std::uint16_t* equips = *reinterpret_cast<const std::uint16_t* const*>(placed + kPlacedEquips);
            const std::uint8_t* status = *reinterpret_cast<const std::uint8_t* const*>(placed + kPlacedStatus);
            std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(placed + kPlacedCount);
            const std::uint32_t locators = *reinterpret_cast<const std::uint32_t*>(placed + kPlacedLocatorCount);
            if (count > locators)
                count = locators;
            if (count > kMaxPlaced)
                count = kMaxPlaced;
            for (std::uint32_t i = 0; equipTypes && positions && equips && status && i < count && n < max; ++i)
            {
                if (!(status[i * 3] & kPlacedPresent) || (status[i * 3 + 1] & kPlacedGone))
                    continue;
                const float* position = positions + i * 4;
                const float dx = position[0] - eye[0], dy = position[1] - eye[1], dz = position[2] - eye[2];
                if (dx * dx + dy * dy + dz * dz > kMineRange * kMineRange)
                    continue;
                for (int k = 0; k < 3; ++k)
                    out[n].position[k] = position[k];
                out[n].equipId = equips[i * 2];
                ++n;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            n = 0;
        }
        return n;
    }

    // the equip type call stays outside SEH like the collection one
    void RefreshMines(std::uint8_t* app, const float eye[3])
    {
        const std::uint64_t now = GetTickCount64();
        if (now - g_MinesMs < kMineRefreshMs)
            return;
        g_MinesMs = now;
        g_MineCount = 0;
        const std::uint8_t* placed = Member<std::uint8_t>(app, kAppPlaced);
        if (!placed)
            return;
        PlacedThing things[kMaxPlacedNear];
        void* equipTypes = nullptr;
        const int count = ReadLocatorPlaced_SEH(placed, eye, things, kMaxPlacedNear, equipTypes);
        void** vtable = count > 0 ? Member<void*>(static_cast<std::uint8_t*>(equipTypes), 0) : nullptr;
        if (!vtable)
            return;
        const auto typeOf = reinterpret_cast<EquipTypeOf_t>(vtable[kEquipTypeSlot]);
        for (int i = 0; i < count && g_MineCount < kMaxMines; ++i)
        {
            if (static_cast<std::uint32_t>(typeOf(equipTypes, things[i].equipId)) - kFirstMineType >= kMineTypes)
                continue;
            g_Mines[g_MineCount][0] = things[i].position[0];
            g_Mines[g_MineCount][1] = things[i].position[1] + kMineHeight;
            g_Mines[g_MineCount][2] = things[i].position[2];
            g_Mines[g_MineCount][3] = 0.0f;
            ++g_MineCount;
        }
    }

    void PickTarget(std::uint8_t* app, const View& v, Pick& pick)
    {
        float decoys[kMaxDecoys][4];
        const int decoyCount = ReadDecoys(decoys);
        for (int i = 0; i < decoyCount; ++i)
            Consider(v, Decoy, decoys[i], pick);
        RefreshMines(app, v.eye);
        for (int i = 0; i < g_MineCount; ++i)
            Consider(v, Mine, g_Mines[i], pick);
        RefreshCollections(app, v.eye);
        for (int i = 0; i < g_DiamondCount; ++i)
            Consider(v, Diamond, g_Diamonds[i], pick);
        for (int i = 0; i < g_PlantCount; ++i)
            Consider(v, g_PlantTargets[i], g_Plants[i], pick);
        if (g_MineCount != g_MinesLogged || g_PlantCount != g_PlantsLogged)
        {
            g_MinesLogged = g_MineCount;
            g_PlantsLogged = g_PlantCount;
            LogDebug("BinocularRadio: %d enemy mines and %d plants within 100 m", g_MineCount, g_PlantCount);
        }
        float sun[3], moon[3];
        bool sunUp = false, moonUp = false;
        if (ReadSky(sun, sunUp, moon, moonUp))
        {
            if (sunUp)
                ConsiderSky(v, Sun, sun, pick);
            if (moonUp)
                ConsiderSky(v, Moon, moon, pick);
        }
        float rotation[4];
        if (ReadStarRotation(rotation))
        {
            if (InAfrica(app))
                ConsiderStars(v, SouthernCross, kSouthernCross, 4, rotation, pick);
            ConsiderStars(v, BigDipper, kBigDipper, 7, rotation, pick);
        }
    }

    std::uint32_t GroupOf(int target)
    {
        switch (target)
        {
        case Decoy:         return 0x0FA948BCu;   // f1000_esrg2120
        case Diamond:       return 0x12DD7E27u;   // f1000_esrg2290
        case Sun:           return 0x3474D39Bu;   // f2000_esrg0050
        case Moon:          return (GetTickCount64() & 1) ? 0x910BC84Cu : 0xDC5DA842u;   // f2000_esrg0020 full moon, f2000_esrg0010 moon
        case BigDipper:     return 0x3319B116u;   // f2000_esrg0040
        case SouthernCross: return 0x8412B597u;   // f2000_esrg0030
        case Mine:          return 0xB0E47F33u;   // f1000_esrg2130
        case GoldenCrescent: return 0x9C460C04u;  // V_f1000_esrg5010
        case Haoma:         return 0x5FC0E2D0u;   // V_f1000_esrg5011
        case AfricanPeach:  return 0x78294C11u;   // V_f1000_esrg5012
        case Digitalis:     return 0x6ED6ABF5u;   // V_f1000_esrg5013
        case Wormwood:      return 0xB293EA63u;   // V_f1000_esrg5014
        case Tarragon:      return 0xAC390307u;   // V_f1000_esrg5015
        case BlackCarrot:   return 0x09E3A55Eu;   // V_f1000_esrg5016
        default:            return 0;
        }
    }

    std::uint32_t RealTimePlantGroup(int target)   // the game's own plant lines: real-time radio, silent from Evasion up
    {
        switch (target)
        {
        case GoldenCrescent: return 0xAD72B0D1u;  // f1000_rtrg5010
        case Haoma:         return 0xB9E11A21u;   // f1000_rtrg5011
        case AfricanPeach:  return 0x5A2F7FE9u;   // f1000_rtrg5012
        case Digitalis:     return 0x067EA129u;   // f1000_rtrg5013
        case Wormwood:      return 0xA6185557u;   // f1000_rtrg5014
        case Tarragon:      return 0x5DB99A12u;   // f1000_rtrg5015
        case BlackCarrot:   return 0xBB26A900u;   // f1000_rtrg5016
        default:            return 0;
        }
    }

    bool RadioGroupLoaded(std::uint32_t group)
    {
        std::uint8_t* manager = Member<std::uint8_t>(Member<std::uint8_t>(App(), kAppRadio), kRadioManager);
        void** vtable = manager ? Member<void*>(manager, 0) : nullptr;
        if (!vtable)
            return false;
        std::uint8_t file = 0;
        std::uint16_t index = 0;
        return reinterpret_cast<FindRadioGroup_t>(vtable[kFindRadioGroupSlot])(manager, group, &file, &index) != nullptr;
    }

    void __fastcall hkUpdateEspionageRadio(void* sightManager, void* owner)
    {
        g_OrigUpdateEspionageRadio(sightManager, owner);
        if (MissionCodeGuard::ShouldBypassHooks() || !FeatureSwitch::IsOn(FeatureSwitch::Feature::OcelotBinocularComments))
            return;
        std::uint8_t* app = App();
        std::uint8_t* radio = Member<std::uint8_t>(Member<std::uint8_t>(app, kAppRadio), kRadioEspionage);
        if (!radio)
            return;
        void** vtable = *reinterpret_cast<void***>(radio);
        if (reinterpret_cast<GetCandidateId_t>(vtable[kGetCandidateIdSlot])(radio) != kNoCandidate)
            return;   // the vanilla picker found something

        View view;
        if (!ReadView(sightManager, owner, view))
            return;
        Pick pick;
        PickTarget(app, view, pick);
        const std::uint64_t now = GetTickCount64();
        if (pick.target == TargetCount)
            return;
        if (pick.target != g_Tracked || now - g_TrackedSeen > kGraceMs)
        {
            g_Tracked = pick.target;
            g_TrackedSince = now;
            g_Aimed = TargetCount;
        }
        g_TrackedSeen = now;
        if (now - g_TrackedSince < kDwellMs)
            return;
        if (g_Aimed != pick.target)
        {
            g_Aimed = pick.target;
            const float dx = pick.point[0] - view.eye[0], dy = pick.point[1] - view.eye[1], dz = pick.point[2] - view.eye[2];
        }

        reinterpret_cast<SetCandidateId_t>(vtable[kSetCandidateIdSlot])(radio, static_cast<std::uint16_t>(kFirstTargetId + pick.target));
        reinterpret_cast<SetCandidatePosition_t>(vtable[kSetCandidatePositionSlot])(radio, pick.point);
        reinterpret_cast<SetCandidateType_t>(vtable[kSetCandidateTypeSlot])(radio, kTargetType);
    }

    // a story mission drops the game's comms comment after it plays: the restored one may only go first there, once
    std::uint32_t NextCommsGroup()
    {
        const std::uint16_t mission = MissionCodeGuard::GetCurrentMissionCode();
        const bool freeRoam = mission / 10000 == 3;
        if ((!freeRoam && g_CommsRestoredMission == mission) || (GetTickCount64() & 1))
            return kCommsGroup;
        g_CommsRestoredMission = freeRoam ? 0 : mission;
        return kCommsRestoredGroup;
    }

    std::uint32_t __fastcall hkGetGroupName(void* radioSystem, std::uint16_t gameObjectId)
    {
        const int target = static_cast<int>(gameObjectId) - kFirstTargetId;
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigGetGroupName(radioSystem, gameObjectId);
        if (target < 0 || target >= TargetCount)
        {
            std::uint32_t group = g_OrigGetGroupName(radioSystem, gameObjectId);
            if (group == kCommsGroup && FeatureSwitch::IsOn(FeatureSwitch::Feature::OcelotBinocularComments))
            {
                group = NextCommsGroup();
                LogDebug("BinocularRadio: comms equipment plays 0x%08X", group);
            }
            return group;
        }
        MarkPlayed(target);
        std::uint32_t group = GroupOf(target);
        if (target >= GoldenCrescent && target <= BlackCarrot && !RadioGroupLoaded(group))
        {
            if (!g_WarnedOldCommonPack.exchange(true, std::memory_order_relaxed))
                LogWarn("BinocularRadio: older V_FrameWork_Common.fpk without the plant intel lines, the game's own plant lines play");
            group = RealTimePlantGroup(target);
        }
        LogDebug("BinocularRadio: target %d plays 0x%08X", target, group);
        return group;
    }

    void __fastcall hkLightControllerUpdate(void* sunData, void* skyState, std::uint8_t flags)
    {
        g_SkyState.store(static_cast<std::uint8_t*>(skyState), std::memory_order_relaxed);
        g_SkyStateMs.store(GetTickCount64(), std::memory_order_relaxed);
        g_OrigLightControllerUpdate(sunData, skyState, flags);
    }

    bool __fastcall hkIsInCentreCircle(void* sightManager, const float* point, const float* projection, const float* view, float ratio,
                                       float radius)
    {
        if (!MissionCodeGuard::ShouldBypassHooks() && projection[5] != 0.0f)
            ratio = projection[0] / projection[5];   // vanilla passes a whole-number height/width, 0 on landscape screens
        return g_OrigIsInCentreCircle(sightManager, point, projection, view, ratio, radius);
    }

    bool ReadSightOwner_SEH(std::uint8_t* sightManager, void*& owner)
    {
        __try
        {
            if (*reinterpret_cast<const std::uint16_t*>(sightManager + kSightModes) == 0)
                return false;
            const std::uint8_t* camera = *reinterpret_cast<const std::uint8_t* const*>(sightManager + kSightCamera);
            owner = camera ? *reinterpret_cast<void* const*>(camera + kCameraOwner) : nullptr;
            return owner != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SamePoster(const float a[3], const float b[3])
    {
        const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        return dx * dx + dy * dy + dz * dz <= kSamePoster * kSamePoster;
    }

    // Snake's own reaction in any sight mode: a line every second while he looks, "Huh...?" only for a poster that hasn't had it
    void UpdatePinUpLook(std::uint8_t* sightManager)
    {
        void* owner = nullptr;
        View view;
        std::uint8_t* app = App();
        if (!app || !ReadSightOwner_SEH(sightManager, owner) || !ReadView(sightManager, owner, view))
            return;
        RefreshCollections(app, view.eye);
        int seen = -1;
        for (int i = 0; i < g_PosterCount && seen < 0; ++i)
        {
            float offCentre = 0.0f;
            if (!OffCentre(view, g_Posters[i], offCentre))
                continue;
            const float dx = view.eye[0] - g_Posters[i][0], dy = view.eye[1] - g_Posters[i][1], dz = view.eye[2] - g_Posters[i][2];
            const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pull = length > kPinUpPullBack ? kPinUpPullBack / length : 0.0f;
            const float nearPoster[3] = { g_Posters[i][0] + dx * pull, g_Posters[i][1] + dy * pull, g_Posters[i][2] + dz * pull };
            if (SightLineClear(view, nearPoster))
                seen = i;
        }
        if (seen < 0)
            return;
        const std::uint64_t now = GetTickCount64();
        if (now - g_PinUpSeen > kGraceMs || !SamePoster(g_Posters[seen], g_PinUpPoster))
        {
            for (int k = 0; k < 3; ++k)
                g_PinUpPoster[k] = g_Posters[seen][k];
            g_PinUpStep = g_PinUpHasGreeted && SamePoster(g_PinUpPoster, g_PinUpGreeted) ? 1 : 0;
            g_PinUpNextMs = now + kPinUpLineMs;
        }
        g_PinUpSeen = now;
        if (now < g_PinUpNextMs)
            return;
        const std::uint32_t line = g_PinUpStep == 0 ? kPinUpHuh
                                 : g_PinUpStep == 1 ? kPinUpHnng
                                 : g_PinUpStep == 2 ? kPinUpMmm
                                 : ((now >> 4) & 1) ? kPinUpHnng : kPinUpMmm;
        if (!SayMinorPlayerLine(line))
            return;   // he is still talking, the line waits for him
        if (g_PinUpStep == 0)
        {
            for (int k = 0; k < 3; ++k)
                g_PinUpGreeted[k] = g_PinUpPoster[k];
            g_PinUpHasGreeted = true;
        }
        if (g_PinUpStep < 3)
            ++g_PinUpStep;
        g_PinUpNextMs = now + kPinUpLineMs;
    }

    void __fastcall hkUpdateAutoMarking(void* sightManager)
    {
        g_OrigUpdateAutoMarking(sightManager);
        if (sightManager && !MissionCodeGuard::ShouldBypassHooks() && FeatureSwitch::IsOn(FeatureSwitch::Feature::PinUpPosterReactions))
            UpdatePinUpLook(static_cast<std::uint8_t*>(sightManager));
    }

    void* InstallOne(const char* what, const char* lost, std::uintptr_t address, const std::uint8_t* entry, std::size_t entrySize,
                     void* detour, void** original)
    {
        void* target = ResolveGameAddress(address);
        const HookEntry state = CheckHookEntry(target, entry, entrySize);
        if (state == HookEntry::Unknown)
        {
            LogError("BinocularRadio: %s entry mismatch on %s, %s", what, GetGameBuildName(gGameBuild), lost);
            return nullptr;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(what, target);
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("BinocularRadio: %s hook failed at %p, %s", what, target, lost);
            return nullptr;
        }
        return target;
    }

    void RemoveOne(void*& target)
    {
        if (target)
            DisableAndRemoveHook(target);
        target = nullptr;
    }
}

bool Install_SightManagerImpl_UpdateEspionageRadio_Hooks()
{
    g_Sites = SitesFor();
    if (!g_Sites)
    {
        LogWarn("BinocularRadio: addresses unknown for %s, no sky, decoy or diamond comments", GetGameBuildName(gGameBuild));
        return true;
    }

    g_GetQuarkSystemTable = gAddr.GetQuarkSystemTable
        ? reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable))
        : nullptr;
    g_GetGameObjectManager = gAddr.Fox_GameObjectManager_GetSharedInstance
        ? reinterpret_cast<GetSharedInstance_t>(ResolveGameAddress(gAddr.Fox_GameObjectManager_GetSharedInstance))
        : nullptr;

    void* construct   = gAddr.GeoCheckControlN1_Construct ? ResolveGameAddress(gAddr.GeoCheckControlN1_Construct) : nullptr;
    void* lineCheck   = gAddr.FoxGeo_CollisionLineCheck ? ResolveGameAddress(gAddr.FoxGeo_CollisionLineCheck) : nullptr;
    void* hitPosition = gAddr.FoxGeoResultI_GetPosition ? ResolveGameAddress(gAddr.FoxGeoResultI_GetPosition) : nullptr;
    if (construct && CheckHookEntry(construct, kConstructGeoControlEntry, sizeof(kConstructGeoControlEntry)) != HookEntry::Unknown
        && lineCheck && CheckHookEntry(lineCheck, kGeoLineCheckEntry, sizeof(kGeoLineCheckEntry)) != HookEntry::Unknown
        && hitPosition && CheckHookEntry(hitPosition, kGetHitPositionEntry, sizeof(kGetHitPositionEntry)) != HookEntry::Unknown)
    {
        g_ConstructGeoControl = reinterpret_cast<ConstructGeoControl_t>(construct);
        g_GeoLineCheck        = reinterpret_cast<GeoLineCheck_t>(lineCheck);
        g_GetHitPosition      = reinterpret_cast<GetHitPosition_t>(hitPosition);
    }
    else
        LogError("BinocularRadio: the collision line check isn't there on %s, no sky, decoy, diamond or pin-up comments", GetGameBuildName(gGameBuild));

    if (!g_LightControllerUpdateTarget)
        g_LightControllerUpdateTarget = InstallOne("DirectionalLightController::Update", kNoComments, g_Sites->lightControllerUpdate,
                                                   kLightControllerUpdateEntry, sizeof(kLightControllerUpdateEntry),
                                                   reinterpret_cast<void*>(&hkLightControllerUpdate),
                                                   reinterpret_cast<void**>(&g_OrigLightControllerUpdate));
    if (!g_GetGroupNameTarget)
        g_GetGroupNameTarget = InstallOne("EspionageRadioSystemImpl::GetGroupNameFromGameObjectId", kNoComments, g_Sites->getGroupName,
                                          kGetGroupNameEntry, sizeof(kGetGroupNameEntry), reinterpret_cast<void*>(&hkGetGroupName),
                                          reinterpret_cast<void**>(&g_OrigGetGroupName));
    if (!g_UpdateEspionageRadioTarget && g_GetGroupNameTarget)
        g_UpdateEspionageRadioTarget = InstallOne("SightManagerImpl::UpdateEspionageRadio", kNoComments, g_Sites->updateEspionageRadio,
                                                  kUpdateEspionageRadioEntry, sizeof(kUpdateEspionageRadioEntry),
                                                  reinterpret_cast<void*>(&hkUpdateEspionageRadio),
                                                  reinterpret_cast<void**>(&g_OrigUpdateEspionageRadio));
    if (!g_IsInCentreCircleTarget)
        g_IsInCentreCircleTarget = InstallOne("binocular centre test", "binocular comments still count anything above or below the crosshair",
                                              g_Sites->isInCentreCircle, kIsInCentreCircleEntry, sizeof(kIsInCentreCircleEntry),
                                              reinterpret_cast<void*>(&hkIsInCentreCircle),
                                              reinterpret_cast<void**>(&g_OrigIsInCentreCircle));
    if (!g_UpdateAutoMarkingTarget && gAddr.SightManagerImpl_UpdateAutoMarkingToMecha)
        g_UpdateAutoMarkingTarget = InstallOne("SightManagerImpl::UpdateAutoMarkingToMecha", "no pin-up poster lines",
                                               gAddr.SightManagerImpl_UpdateAutoMarkingToMecha, kUpdateAutoMarkingEntry,
                                               sizeof(kUpdateAutoMarkingEntry), reinterpret_cast<void*>(&hkUpdateAutoMarking),
                                               reinterpret_cast<void**>(&g_OrigUpdateAutoMarking));
    return true;
}

void Uninstall_SightManagerImpl_UpdateEspionageRadio_Hooks()
{
    RemoveOne(g_UpdateAutoMarkingTarget);
    RemoveOne(g_IsInCentreCircleTarget);
    RemoveOne(g_UpdateEspionageRadioTarget);
    RemoveOne(g_GetGroupNameTarget);
    RemoveOne(g_LightControllerUpdateTarget);
    g_SkyState.store(nullptr, std::memory_order_relaxed);
}
