#include <pch.h>
#include "VehicleSortieRows.h"
#include "../ui/ResourceManagementCallbackImpl_UpdateState.h"
#include "../deploy/DeployMissionRegistry.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "AddressSet.h"
#include "CustomVehicles.h"
#include "FoxHashes.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "NativeCallPatch.h"
#include "V_FrameWorkState.h"
#include "log.h"
#include "../../lua/LuaApi.h"

namespace
{
    using CustomVehicles::SortieVehicle;
    using AddressSetRuntime::AddressSet;

    using ExhibitSelectingRelief_t = void(__fastcall*)(std::uint8_t* itemSelector, std::uint32_t row);
    using VehicleDisplay_t         = void(__fastcall*)(std::uint8_t* missionPrep, std::uint8_t resourceId, std::int32_t coloring);
    using ReserveSelectionRelief_t = void(__fastcall*)(std::uint8_t* prepCallback);
    using PayResourceCost_t        = std::int32_t(__fastcall*)(void* resources, std::uint8_t resourceId, std::int32_t count);
    using SetTopMenuVehicleName_t  = void(__fastcall*)(std::uint8_t* prepCallback);
    using ExhibitRelief_t          = std::int32_t*(__fastcall*)(std::uint8_t* vehicleSystem, std::int32_t* result, std::uint32_t reliefType, std::uint32_t coloring, const float* transform);
    using SpawnRelief_t            = std::int32_t*(__fastcall*)(std::uint8_t* vehicleSystem, std::int32_t* result, std::uint32_t reliefType, const float* position, float rotY, std::uint32_t options);
    using DeliverRelief_t          = std::int32_t*(__fastcall*)(std::uint8_t* vehicleSystem, std::int32_t* result, std::uint32_t reliefType, const float* position, float rotY);
    using InstanceCommand_t        = void(__fastcall*)(std::uint8_t* vehicles, std::int32_t* result, lua_State* L, std::uint32_t index, std::uint32_t command);
    using SetupVehicleDetails_t    = void(__fastcall*)(std::uint8_t* details);
    using ConvertIntoResourceId_t  = std::uint8_t(__fastcall*)(std::uint32_t reliefType);
    using ReflectLifeState_t       = void(__fastcall*)(void* instances, std::uint16_t index, std::uint8_t* commInfo);
    using FultonRecovery_t         = void(__fastcall*)(void* maneuverController, std::uint8_t index, std::uint8_t* commInfo);
    using CanPayResourceCost_t     = bool(__fastcall*)(void* resources, std::uint8_t resourceId, std::int32_t count);
    using RequestDropVehicle_t     = bool(__fastcall*)(void* supportRequest, std::uint32_t reliefType, const float* position);
    using GetVehicleType_t         = std::uint8_t(__fastcall*)(const std::uint8_t* commInfo);
    using Seize_t                  = void(__fastcall*)(std::uint8_t* vehicles, std::uint16_t index, std::uint32_t recoveryType, std::uint64_t options);
    using AddResource_t            = void(__fastcall*)(void* resources, std::uint8_t resourceId, std::uint64_t count, std::uint64_t visual,
                                                       std::uint64_t a, std::uint64_t b, std::uint64_t c);
    using AddTempResource_t        = void(__fastcall*)(void* motherBase, std::uint8_t resourceId, std::int32_t count, std::uint64_t visual,
                                                       std::uint64_t owner, std::uint64_t extra);
    using AddResourcesFromTemp_t   = void(__fastcall*)(void* resources);
    using GetUniqueIconLangId_t    = std::uint64_t*(__fastcall*)(void* mapData, std::uint64_t* langId, std::uint16_t gameObjectId);
    using RefreshResourceList_t    = void(__fastcall*)(std::uint8_t* resourcePage, std::uint8_t category);
    using ResourceRecordStart_t    = void(__fastcall*)(std::uint8_t* record, void* window);
    using ResourceInfoText_t       = void(__fastcall*)(std::uint8_t* resourcePage);
    using ResourceCount_t          = std::int32_t(__fastcall*)(void* resources, std::uint8_t resourceId);
    using SaleResource_t           = void(__fastcall*)(std::uint8_t* resources, std::uint8_t resourceId, std::int32_t count);
    using AddPoolRewards_t         = void(__fastcall*)(std::uint8_t* rewards, const std::int16_t* rewardsInfo, std::uint8_t forFob,
                                                       std::uint16_t seed, std::uint8_t rank, std::uint32_t source);
    using AddPoolRewardCore_t      = void(__fastcall*)(std::uint8_t* rewards, std::uint8_t forFob, std::uint8_t type, std::uint16_t row,
                                                       std::uint16_t seed, std::uint8_t rank, std::uint32_t source, std::uint32_t amount,
                                                       std::uint8_t mbCoin, std::uint8_t a, std::uint8_t online, std::uint8_t vehicleIndex,
                                                       std::uint8_t b, std::uint8_t c, std::uint8_t d, std::uint8_t e, std::uint8_t f);
    using AcceptPoolRewardOne_t    = void(__fastcall*)(std::uint8_t* rewards, std::uint64_t entry);
    using RewardListRow_t          = void(__fastcall*)(std::uint8_t* record, std::uint64_t row);
    using MissionRewardsPanel_t    = void(__fastcall*)(std::uint8_t* deployPage);
    using SetNodeShaderParam_t     = void(__fastcall*)(void* ui, void* node, std::uint64_t param, const float* value);
    using RefreshImageRecord_t     =void(__fastcall*)(std::uint8_t* resourcePage, std::uint8_t* imageRecord, std::uint16_t category,
                                                       std::uint64_t row, std::uint16_t slot);
    using SalePopupRefresh_t       = void(__fastcall*)(std::uint8_t* popup);
    using DrawGmpRow_t             = void(__fastcall*)(std::uint8_t* deployPage, std::uint32_t row, std::uint32_t amount, std::uint8_t fob);
    using DrawMaterialRow_t        = void(__fastcall*)(std::uint8_t* deployPage, std::uint32_t row, std::int8_t materialIndex, const std::uint8_t* rewardParam);
    using DrawStaffRow_t           = void(__fastcall*)(std::uint8_t* deployPage, std::uint32_t row, const std::uint8_t* staffDraw, const std::uint8_t* rewardParam);
    using RewardIfaceByte_t        = std::uint8_t(__fastcall*)(void* rewardIface, std::uint8_t poolType);
    using SetNodeVisible_t         = void(__fastcall*)(void* ui, void* node, bool visible);
    using AddGmp_t                 = void(__fastcall*)(void* wallet, std::uint32_t gmp);
    using ResourceCategory_t       = std::uint8_t(__fastcall*)(void* resources, std::uint8_t resourceId);
    using WriteInfoText_t          = void(__fastcall*)(void* ui, void* node, void* style, std::int32_t font, const void* text, std::uint8_t a, std::uint8_t b);
    using ConvertIntoReliefType_t  = std::uint32_t(__fastcall*)(std::uint8_t typeCode);

    using IsVehicleEnabled_t = bool(__fastcall*)(void* missionPrep);
    using GetTextByLangId_t  = void*(__fastcall*)(void* ui, std::uint64_t langId);
    using WriteText_t        = void(__fastcall*)(void* ui, void* node, void* style, const void* text, std::uint8_t flag);
    using SetNodeTexture_t   = void(__fastcall*)(void* ui, void* node, std::uint64_t textureFile, std::uint64_t slotName);
    using PrefetchTexture_t  = void(__fastcall*)(void* ui, std::uint64_t textureFile, std::int32_t priority);
    using GetUixUtility_t    = void**(__fastcall*)();

    constexpr std::uint32_t kModeSortieVehicles = 0x800;
    constexpr std::size_t   kCellsPerRow        = 15;
    constexpr std::uint32_t kMostRows           = 0x400;
    constexpr std::size_t   kMostSortieVehicles = CustomVehicles::kLastType - CustomVehicles::kFirstType + 1;
    constexpr std::uint8_t  kNoVehicle          = 0xFF;
    constexpr std::uint32_t kLastReliefType     = 10;
    constexpr std::uint64_t kIconSlot           = 0xCAFB3BBF9889ull;
    constexpr std::uint64_t kSelectorSignature  = 0xB8A0BF169F98ull;   // a built item selector keeps this at +0x461B8
    constexpr const char*   kStateSpace         = "VehicleSortie";
    constexpr const char*   kStockSpace         = "VehicleStock";   // Mother Base's count of each mod vehicle, by name: never in the game's resource table
    constexpr std::uint32_t kStockIdBase        = 0xF0;   // resource ids the game never uses (its own end at 0x3A, 0xFF = none): one per mod vehicle type

    constexpr std::size_t kSelectorUi          = 0x30;
    constexpr std::size_t kSelectorPrep        = 0x70;
    constexpr std::size_t kSelectorCountStyle  = 0xB8;
    constexpr std::size_t kSelectorShownRows   = 0x104;
    constexpr std::size_t kSelectorCursor      = 0x10C;     // the cursor's row = top row + cursor
    constexpr std::size_t kSelectorTopRow      = 0x110;
    constexpr std::size_t kSelectorRowEnabled  = 0x548;     // 15 cells a row
    constexpr std::size_t kSelectorSelectedRow = 0x4198;
    constexpr std::size_t kSelectorRowIds      = 0x421A4;   // a vehicle row = its Mother Base resource id, 0xFF = the "none" row
    constexpr std::size_t kSelectorRowMarks    = 0x425A4;   // 15 cells a row, 1 = the row in use
    constexpr std::size_t kSelectorRowCount    = 0x442C;
    constexpr std::size_t kSelectorMode        = 0x4434;
    constexpr std::size_t kSelectorRowCells    = 0xBC40;    // how many cells each row has
    constexpr std::size_t kSelectorRowCell     = 0xC040;    // the cell each row shows
    constexpr std::size_t kSelectorNoNoneRow   = 0x461B0;   // 1 = the iDroid's supply drop list
    constexpr std::size_t kSelectorSignatureAt = 0x461B8;
    constexpr std::size_t kSelectorCountNode   = 0x46228;
    constexpr std::size_t kSelectorCountText   = 0x46230;   // char[8]

    constexpr std::size_t kRecordRow        = 0x8;
    constexpr std::size_t kRecordUi         = 0x38;
    constexpr std::size_t kRecordStyle      = 0x80;
    constexpr std::size_t kRecordNameNode   = 0x88;
    constexpr std::size_t kRecordCountNode  = 0x90;
    constexpr std::size_t kRecordIconNode   = 0xA8;
    constexpr std::size_t kRecordIconNode2  = 0xB0;
    constexpr std::size_t kRecordRowIds     = 0x218;
    constexpr std::size_t kRecordBuddyList  = 0x230;
    constexpr std::size_t kRecordReliefList = 0x231;

    constexpr std::size_t kCallbackPrep     = 0x48;
    constexpr std::size_t kCallbackUi       = 0x1C0;
    constexpr std::size_t kCallbackNameNode = 0x200;
    constexpr std::size_t kCallbackStyle    = 0x2C08;
    constexpr std::size_t kPrepSelectedId   = 0x1A;
    constexpr std::size_t kPrepVtblIsVehicleEnabled = 0x4C8 / sizeof(void*);

    constexpr std::size_t kDetailsUi         = 0x80;
    constexpr std::size_t kDetailsStyle      = 0x98;
    constexpr std::size_t kDetailsIconNode   = 0x128;
    constexpr std::size_t kDetailsIconNode2  = 0x130;
    constexpr std::size_t kDetailsNameNode   = 0x138;
    constexpr std::size_t kDetailsResourceId = 0xE88;

    constexpr std::size_t kSystemReliefRequest = 0x58;    // bit 0 = load the relief pack again
    constexpr std::size_t kVehiclesCommInfos   = 0x30;
    constexpr std::size_t kSeizeCommInfos      = 0x10;
    constexpr std::size_t kSystemVehicles      = 0xB0;    // the vehicles' instance block (the one Seize runs on)
    constexpr std::size_t kVehiclesAliveBits   = 0x28;    // in that block: one bit per vehicle in use
    constexpr std::uint16_t kGameObjectTypeMask  = 0xFE00;
    constexpr std::uint16_t kVehicleGameObjects  = 0x6400;   // TppVehicle2 game object ids: 0x6400 | instance index
    constexpr std::uint16_t kGameObjectIndexMask = 0x01FF;
    constexpr std::uint64_t kLangIdMask          = 0xFFFFFFFFFFFF;    // Seize runs on the vehicles' instance block at +0x20, which keeps the same array at its +0x10
    constexpr std::size_t kVehiclesCount       = 0x1E0;
    constexpr std::size_t kCommInfoSize        = 0x80;
    constexpr std::size_t kCommInfoTypeCode    = 0x6C;    // type 0 in the high 4 bits = the player's relief vehicle

    constexpr std::size_t kUiVtblSetNodeTexture = 0x518 / sizeof(void*);
    constexpr std::size_t kUiVtblPrefetch       = 0x548 / sizeof(void*);
    constexpr std::size_t kUiVtblWriteText      = 0x708 / sizeof(void*);
    constexpr std::size_t kUiVtblLangText       = 0x750 / sizeof(void*);

    const std::uint8_t kExhibitSelectingEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x81, 0xB9, 0x34, 0x44, 0x00, 0x00,
        0x00, 0x00, 0x20, 0x00, 0x8B, 0xC2, 0x48, 0x8B, 0xD9, 0x0F, 0xB6, 0xBC, 0x08, 0xA4, 0x21, 0x04,
    };
    const std::uint8_t kVehicleDisplayEntry[] = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0xE0,
        0x00, 0x00, 0x00, 0x0F, 0x29, 0xB4, 0x24, 0xD0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05,
    };
    const std::uint8_t kReserveEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
        0x48, 0x83, 0xEC, 0x70, 0x48, 0x8B, 0x05, 0x65, 0xCE, 0x48, 0x01, 0x48, 0x33, 0xC4, 0x48, 0x89,
    };
    const std::uint8_t kReserveEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1,
    };
    const std::uint8_t kPayEntry[] = {
        0x80, 0xFA, 0xFF, 0x74, 0x07, 0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x58, 0xF3, 0xC3,
    };
    const std::uint8_t kTopMenuNameEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
        0x8B, 0xD9, 0x48, 0x8B, 0x49, 0x48, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0xC8, 0x04, 0x00, 0x00, 0x84,
    };
    const std::uint8_t kExhibitEntry[] = {
        0x40, 0x55, 0x48, 0x8D, 0x6C, 0x24, 0xB1, 0x48, 0x81, 0xEC, 0xF0, 0x00, 0x00, 0x00, 0x48, 0x8B,
        0x05, 0x2B, 0x30, 0x12, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45, 0x9F, 0x48, 0x83, 0xB9, 0xB0,
    };
    const std::uint8_t kExhibitEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    const std::uint8_t kSpawnEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0x05, 0xFD,
        0xFF, 0x11, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x30, 0x48, 0x8B, 0xFA, 0x0F, 0xB6,
    };
    const std::uint8_t kDeliverEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0x05, 0xDD,
        0x31, 0x12, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x30, 0x48, 0x8B, 0xFA, 0x0F, 0xB6,
    };
    const std::uint8_t kInstanceCommandEntry[] = {
        0x40, 0x56, 0x57, 0x41, 0x55, 0x48, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x0D,
        0xDC, 0x01, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00, 0x0F, 0xB7,
    };
    const std::uint8_t kCookieEntryMask[] = {   // these three entries load the stack cookie at byte 12
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
        0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    const std::uint8_t kDetailsEntry[] = {
        0x40, 0x56, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0xF1, 0xE8, 0xC2, 0x41, 0xFF, 0xFF, 0x48, 0x8B,
        0xCE, 0xE8, 0x5A, 0x44, 0xFF, 0xFF, 0x80, 0xBE, 0x88, 0x0E, 0x00, 0x00, 0xFF, 0x0F, 0x84, 0xBF,
    };
    const std::uint8_t kDetailsEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1,
        1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    const std::uint8_t kConvertResourceEntry[] = {
        0xFF, 0xC9, 0x83, 0xF9, 0x09, 0x77, 0x34, 0x48, 0x8D, 0x15, 0x22, 0x9F, 0x5D, 0xFF,
    };
    const std::uint8_t kConvertResourceEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
    };
    const std::uint8_t kReflectLifeEntry[] = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xD1, 0x48, 0x81,
        0xEC, 0x88, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x54, 0xED, 0x44, 0x01, 0x48, 0x33, 0xC4, 0x48,
    };
    const std::uint8_t kReflectLifeEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
    };
    const std::uint8_t kFultonRecoveryEntry[] = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xF0,
        0xFD, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x10, 0x03, 0x00, 0x00, 0x48, 0x8B, 0x05,
    };
    const std::uint8_t kCanPayEntry[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xD8, 0x80, 0xFA, 0xFF, 0x75, 0x08, 0xB0, 0x01,
        0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3,
    };
    const std::uint8_t kRequestDropEntry[] = {
        0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xE0, 0x00,
        0x00, 0x00, 0x48, 0x8B, 0x05, 0xD7, 0x95, 0x07, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24,
    };
    const std::uint8_t kRequestDropEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    };
    const std::uint8_t kSeizeEntry[] = {
        0x40, 0x53, 0x56, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00,
        0x48, 0x8B, 0x05, 0x99, 0x45, 0x01, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0x80, 0x00,
    };
    const std::uint8_t kSeizeEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    const std::uint8_t kAddResourceEntry[] = {
        0x80, 0xFA, 0xFF, 0x0F, 0x84, 0x0F, 0x01, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0x48,
        0x83, 0xEC, 0x20, 0x48, 0x8B, 0x81, 0xF8, 0x11, 0x00, 0x00,
    };
    const std::uint8_t kAddTempResourceEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x99, 0xE0, 0x0A, 0x00,
        0x00, 0x41, 0x8B, 0xF8, 0x48, 0x63, 0x83, 0x74, 0x11, 0x00, 0x00, 0x48, 0x81, 0xC3, 0x58, 0x09,
    };
    const std::uint8_t kAddFromTempEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55,
        0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x10, 0xFE, 0xFF, 0xFF,
    };
    const std::uint8_t kUniqueIconLangEntry[] = {   // byte 0x37 tells it from the goal label twin right before it (+0x08 vs +0x10)
        0x44, 0x0F, 0xB6, 0x91, 0x98, 0x07, 0x00, 0x00, 0x45, 0x33, 0xC9, 0x48, 0x8D, 0x81, 0xA0, 0x07,
        0x00, 0x00, 0x45, 0x85, 0xD2, 0x74, 0x12, 0x66, 0x44, 0x39, 0x00, 0x74, 0x17, 0x41, 0xFF, 0xC1,
        0x48, 0x83, 0xC0, 0x18, 0x45, 0x3B, 0xCA, 0x72, 0xEE, 0x48, 0xC7, 0x02, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x8B, 0xC2, 0xC3, 0x48, 0x8B, 0x48, 0x08,
    };
    const std::uint8_t kResourceListEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
        0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x45, 0x33, 0xF6, 0x48, 0xC7, 0x81,
    };
    const std::uint8_t kResourceRecordEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
        0x83, 0x79, 0x38, 0x00, 0x48, 0x8B, 0xD9, 0x75, 0x09, 0x48, 0x89, 0x51, 0x38,
    };
    const std::uint8_t kResourceInfoEntry[] = {
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0x91, 0xE8, 0x08, 0x00, 0x00, 0x83, 0xB9, 0x2C,
        0x01, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0x49, 0x28, 0x48, 0x8B, 0x01, 0x76, 0x05,
    };
    const std::uint8_t kUsableCountEntry[] = {
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x81, 0xF0, 0x11, 0x00, 0x00, 0x44, 0x0F, 0xB6, 0xCA, 0x42,
        0x8B, 0x0C, 0x88, 0x44, 0x8B, 0xC1, 0x41, 0xD1, 0xE8, 0x41, 0x81, 0xE0, 0xFF, 0xFF, 0x0F, 0x00,
    };
    const std::uint8_t kServerCountEntry[] = {
        0x48, 0x83, 0xEC, 0x28, 0x44, 0x0F, 0xB6, 0xC2, 0x48, 0x8B, 0xD1, 0x41, 0x80, 0xF8, 0x1C, 0x76,
        0x11, 0x41, 0x8D, 0x40, 0xDE, 0x3C, 0x04, 0x76, 0x09, 0x33, 0xC9, 0x8B, 0xC1, 0x48, 0x83, 0xC4,
    };
    const std::uint8_t kSaleEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
        0x8B, 0x01, 0x0F, 0xB6, 0xDA, 0x41, 0x8B, 0xF8, 0x0F, 0xB6, 0xD3, 0x48, 0x8B, 0xF1, 0xFF, 0x50,
    };
    const std::uint8_t kAddPoolRewardsEntry[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48,
        0x89, 0x78, 0x20, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00,
    };
    const std::uint8_t kAcceptOneEntry[] = {
        0x40, 0x55, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xB8, 0x48, 0x81, 0xEC, 0x48, 0x01, 0x00, 0x00,
        0x48, 0x8B, 0x05, 0x99, 0x4E, 0xBD, 0x01, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45, 0x10, 0x4C, 0x8B,
    };
    const std::uint8_t kAcceptOneEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    const std::uint8_t kImageRecordEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
        0xEC, 0x20, 0x41, 0x0F, 0xB7, 0xC1, 0x4C, 0x8B, 0xF2, 0x48, 0x8B, 0xE9,
    };
    const std::uint8_t kSalePopupEntry[] = {
        0x40, 0x53, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0xE8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x0D,
        0xA1, 0x47, 0x01, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0xD0, 0x00, 0x00, 0x00,
    };
    const std::uint8_t kSalePopupEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,   // the stack cookie's rel32 differs on each build
        0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    static_assert(sizeof(kSalePopupEntry) == sizeof(kSalePopupEntryMask), "one mask byte per entry byte");
    const std::uint8_t kMissionRewardsEntry[] = {
        0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00, 0x48, 0x8B,
        0x05, 0xEB, 0x28, 0x50, 0x01, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0xB0, 0x00, 0x00, 0x00,
    };
    const std::uint8_t kMissionRewardsEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    const std::uint8_t kRewardListRowEntry[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
        0x41, 0x57, 0x48, 0x8D, 0x68, 0x88, 0x48, 0x81, 0xEC, 0x40, 0x01, 0x00, 0x00, 0x0F, 0x29, 0x70,
    };
    static_assert(sizeof(kAcceptOneEntry) == sizeof(kAcceptOneEntryMask), "one mask byte per entry byte");
    static_assert(sizeof(kSeizeEntry) == sizeof(kSeizeEntryMask), "one mask byte per entry byte");
    static_assert(sizeof(kConvertResourceEntry) == sizeof(kConvertResourceEntryMask), "one mask byte per entry byte");
    static_assert(sizeof(kReflectLifeEntry) == sizeof(kReflectLifeEntryMask), "one mask byte per entry byte");
    static_assert(sizeof(kRequestDropEntry) == sizeof(kRequestDropEntryMask), "one mask byte per entry byte");
    static_assert(sizeof(kReserveEntry) == sizeof(kReserveEntryMask), "one mask byte per entry byte");
    static_assert(sizeof(kExhibitEntry) == sizeof(kExhibitEntryMask), "one mask byte per entry byte");
    static_assert(sizeof(kSpawnEntry) == sizeof(kCookieEntryMask) && sizeof(kDeliverEntry) == sizeof(kCookieEntryMask)
                  && sizeof(kInstanceCommandEntry) == sizeof(kCookieEntryMask), "one mask byte per entry byte");
    static_assert(sizeof(kDetailsEntry) == sizeof(kDetailsEntryMask), "one mask byte per entry byte");

    ExhibitSelectingRelief_t g_OrigExhibitSelectingRelief = nullptr;
    VehicleDisplay_t         g_OrigStartVehicleDisplay    = nullptr;
    VehicleDisplay_t         g_OrigChangeVehicleDisplay   = nullptr;
    ReserveSelectionRelief_t g_OrigReserveSelectionRelief = nullptr;
    PayResourceCost_t        g_OrigPayResourceCost        = nullptr;
    SetTopMenuVehicleName_t  g_OrigSetTopMenuVehicleName  = nullptr;
    ExhibitRelief_t          g_OrigExhibitRelief          = nullptr;
    SpawnRelief_t            g_OrigSpawnRelief            = nullptr;
    DeliverRelief_t          g_OrigDeliverRelief          = nullptr;
    InstanceCommand_t        g_OrigInstanceCommand        = nullptr;
    SetupVehicleDetails_t    g_OrigSetupVehicleDetails    = nullptr;
    ConvertIntoResourceId_t  g_OrigConvertIntoResourceId  = nullptr;
    ReflectLifeState_t       g_OrigReflectLifeState       = nullptr;
    FultonRecovery_t         g_OrigFultonRecovery         = nullptr;
    CanPayResourceCost_t     g_OrigCanPayResourceCost     = nullptr;
    RequestDropVehicle_t     g_OrigRequestDropVehicle     = nullptr;
    Seize_t                  g_OrigSeize                  = nullptr;
    AddResource_t            g_OrigAddResource            = nullptr;
    AddTempResource_t        g_OrigAddTempResource        = nullptr;
    AddResourcesFromTemp_t   g_OrigAddResourcesFromTemp   = nullptr;
    GetUniqueIconLangId_t    g_OrigGetUniqueIconLangId    = nullptr;
    RefreshResourceList_t    g_OrigRefreshResourceList    = nullptr;
    ResourceRecordStart_t    g_OrigResourceRecordStart    = nullptr;
    ResourceInfoText_t       g_OrigResourceInfoText       = nullptr;
    bool                     g_HoldsResourceStep          = false;
    ResourceCount_t          g_OrigResourceUsableCount    = nullptr;
    ResourceCount_t          g_OrigResourceServerCount    = nullptr;
    SaleResource_t           g_OrigSaleResource           = nullptr;
    AddPoolRewards_t         g_OrigAddPoolRewards         = nullptr;
    AcceptPoolRewardOne_t    g_OrigAcceptPoolRewardOne    = nullptr;
    RewardListRow_t          g_OrigRewardListRow          = nullptr;
    MissionRewardsPanel_t    g_OrigMissionRewardsPanel    = nullptr;
    RefreshImageRecord_t     g_OrigRefreshImageRecord     = nullptr;
    SalePopupRefresh_t       g_OrigSalePopupRefresh       = nullptr;

    constexpr std::size_t  kDeploySelected      = 0x5F38;   // the deployment info under the cursor
    constexpr std::size_t  kDeployInfoSeed      = 0x54;
    constexpr std::size_t  kDeployInfoRewardRow = 0xA2;
    constexpr std::size_t  kDeployUi            = 0x40;
    constexpr std::size_t  kDeployStyle         = 0x28;
    constexpr std::size_t  kDeployRowParts      = 0x4D5;    // three alternative parts per row at (row + this) * 0x18
    constexpr std::size_t  kDeployRowName       = 0x7480;   // 0x18 per row
    constexpr std::size_t  kDeployRowIcon       = 0x7588;   // 8 per row
    constexpr std::size_t  kDeployRowAmountNode = 0x7538;
    constexpr std::size_t  kDeployRowRateNode   = 0x7560;
    constexpr std::size_t  kDeployRowAmountText = 0x7858;   // 0x80 per row, " " = an empty row
    constexpr std::size_t  kDeployRowRateText   = 0x7AD8;
    constexpr std::size_t  kDeployRewardParams  = 0xD0;     // the reward controller: pool reward params at +8, 0x30 per reward row
    constexpr std::size_t  kRewardParamFirst    = 0x8;
    constexpr std::size_t  kRewardParamSize     = 0x30;
    constexpr std::size_t  kRewardParamMainType = 0x2;
    constexpr std::size_t  kRewardParamGmp      = 0x4;
    constexpr std::uint8_t kMainRewardGmp       = 1;
    constexpr std::uint8_t kPoolRewardGmp           = 1;      // POOL_REWARD_TYPE_GMP
    constexpr std::uint8_t kPoolRewardFirstStaff    = 2;      // STAFF_SPP .. STAFF_G
    constexpr std::uint8_t kPoolRewardLastStaff     = 11;
    constexpr std::uint8_t kPoolRewardFirstMaterial = 12;     // COMMON_METAL .. HAOMA
    constexpr std::uint8_t kPoolRewardLastMaterial  = 24;
    constexpr std::size_t  kRewardIfaceVtblRank     = 0xC0 / sizeof(void*);   // pool type -> staff rank
    constexpr std::size_t  kRewardIfaceVtblMaterial = 0xC8 / sizeof(void*);   // pool type -> material index
    constexpr std::size_t  kStaffDrawSize       = 0x48;    // the deploy screen's staff draw: 64 rank bytes from +3, +0x43 = 1
    constexpr std::size_t  kStaffDrawRanks      = 0x3;
    constexpr std::size_t  kStaffDrawRankCount  = 0x40;
    constexpr std::size_t  kStaffDrawKind       = 0x43;
    constexpr std::size_t  kFakeParamRate       = 0x8;     // the row drawers read rate/count from a reward param: ours are overwritten after
    constexpr std::size_t  kFakeParamCount      = 0x9;
    constexpr std::uint32_t kDeployRewardRows   = 5;
    constexpr std::size_t  kUiVtblSetVisible    = 0x2A8 / sizeof(void*);

    constexpr std::uint8_t kPoolRewardVehicle   = 0x1B;     // the game's own vehicle reward type (online rewards), vehicle index in bits 22-25
    constexpr std::size_t  kRewardEntries       = 0x3030;   // u32 per pending reward: bits 0-8 reward row, 9-13 type, 19 MB coin, 21 online
    constexpr std::size_t  kListEntryIndex      = 0x1A8;    // in the Rewards page: entry index of each list row
    constexpr std::size_t  kListPage            = 0x30;
    constexpr std::size_t  kListRewards         = 0x40;     // rewards + 0x20
    constexpr std::size_t  kListPageUi          = 0x18;
    constexpr std::size_t  kListPageStyle       = 0x8;
    constexpr std::size_t  kListIconNode        = 0x368;
    constexpr std::size_t  kListNameNode        = 0x370;
    constexpr std::size_t  kListCountNode       = 0x398;
    constexpr std::size_t  kListCountText       = 0x378;
    constexpr std::size_t  kListCountTextSize   = 0x20;
    constexpr std::size_t  kMostRewardRows      = 0x200;

    using RewardLine = VehicleSortieRows::DispatchRewardLine;
    struct DispatchRewards
    {
        std::string             missionKey;
        std::vector<RewardLine> lines;
    };
    DispatchRewards g_DispatchRewards[kMostRewardRows] = {};   // by dispatch reward row, filled by mod Lua each launch

    constexpr std::size_t kPageStateAt        = 0x20;    // UpdateState and the rows work on the page from +0x20
    constexpr std::size_t kPageStep           = 0x274;   // UpdateState's switch byte
    constexpr std::uint8_t kStepSalePopup     = 0x13;
    constexpr std::size_t kPopupQuantity      = 0x200;   // the quantity list: +0xC = how many it offers
    constexpr std::size_t kPopupOwned         = 0x20C;
    constexpr std::size_t kPopupResourceId    = 0x210;
    constexpr std::size_t kPopupLocalMost     = 0x214;
    constexpr std::size_t kPopupServerMost    = 0x218;   // anything above the local count is sold on the server
    constexpr std::size_t kPopupState         = 0x8;     // the sale popup: the page + 0x20
    constexpr std::size_t kPopupPicture       = 0x20;
    constexpr std::size_t kPopupPicture2      = 0x28;
    constexpr std::size_t kPopupItemName      = 0xB0;
    constexpr std::size_t kStateResourceId    = 0x1F0;
    constexpr std::size_t kStateUi            = 0x8;
    constexpr std::size_t kStateStyle         = 0x40;
    constexpr std::size_t kImageRecordNode    = 0x28;    // the big picture: two nodes, each in two slots
    constexpr std::size_t kImageRecordNode2   = 0x30;
    constexpr std::uint64_t kImageSlot2       = 0xEC3F8D982B8Eull;
    constexpr std::size_t kUiVtblShaderParam  = 0x590 / sizeof(void*);
    constexpr std::uint64_t kIconShaderShift  = 0x95173E2EF6C2ull;
    constexpr std::uint64_t kIconShaderTone   = 0xB32EA1B43647ull;
    constexpr std::size_t kQuantityCount      = 0xC;
    constexpr std::size_t kQuantityVtblRefresh = 0x30 / sizeof(void*);
    constexpr std::size_t kResRecordCountText = 0xE8;    // 0x10 each: the row reads these after the call
    constexpr std::size_t kResRecordLocalText = 0x100;
    constexpr std::size_t kResRecordLocalNode = 0x110;
    constexpr std::size_t kResRecordServerText = 0x118;
    constexpr std::size_t kResRecordServerNode = 0x128;
    constexpr std::size_t kResRecordTextSize  = 0x10;
    constexpr std::size_t kPageUi             = 0x28;
    constexpr std::size_t kPageResources      = 0x50;
    constexpr std::size_t kPageInfoStyle      = 0x70;
    constexpr std::size_t kPageRowIds         = 0xA0;    // 20 resource ids
    constexpr std::size_t kPageRowCount       = 0x12C;
    constexpr std::size_t kPageCursor         = 0x134;   // the cursor's row = (top + cursor) % rows
    constexpr std::size_t kPageTopRow         = 0x138;
    constexpr std::size_t kPageInfoNode       = 0x8F0;
    constexpr std::uint32_t kPageMostRows     = 20;
    constexpr std::size_t kResourcesVtblCategory = 0xF8 / sizeof(void*);
    constexpr std::size_t kUiVtblWriteName       = 0x710 / sizeof(void*);
    constexpr std::size_t kUiVtblWriteInfo       = 0x738 / sizeof(void*);
    constexpr std::size_t kRecordPage         = 0x30;    // the page + 0x20
    constexpr std::size_t kPageUiFromState    = 0x8;     // from the page + 0x20
    constexpr std::size_t kPageStyleFromState = 0x40;
    constexpr std::size_t kResRecordIconNode  = 0x50;
    constexpr std::size_t kResRecordIconNode2 = 0x58;
    constexpr std::size_t kResRecordNameNode  = 0xE0;
    constexpr std::size_t kResRecordCountNode = 0xF8;

    std::uint8_t* g_ResourcePage = nullptr;
    std::uint32_t g_ResourceFirstRow = 0;
    std::uint32_t g_ResourceCount = 0;
    std::uint8_t  g_ResourceTypes[kMostSortieVehicles] = {};
    thread_local std::int32_t t_SaleRowId = -1;   // the borrowed game id of the mod vehicle row under the Resources cursor
    thread_local std::uint8_t t_SaleRowType = 0;
    constexpr std::size_t   kResourcesSalePrices = 0x8;      // u32 per resource id
    constexpr std::size_t   kResourcesWallet     = 0x3268;
    constexpr std::size_t   kWalletVtblAddGmp    = 0x30 / sizeof(void*);
    constexpr std::uint32_t kMostSaleGmp         = 30000000;

    constexpr std::size_t kCallSize          = 5;
    constexpr std::size_t kThunkSize         = 16;
    constexpr std::size_t kCarryReliefCallAt = 0x12;   // SaveCarry converts the carried vehicle's type into its relief type right after
    GetVehicleType_t g_GetVehicleType = nullptr;
    NativeCallPatch  g_CarryCall{};
    bool             g_CarryCallPatched = false;

    std::uint8_t* g_ListSelector = nullptr;
    std::uint32_t g_ListFirstRow = 0;
    std::uint32_t g_ListCount = 0;
    std::uint8_t  g_ListTypes[kMostSortieVehicles] = {};
    bool          g_Installed = false;

    std::atomic<std::uint8_t> g_SelectedType{ 0 };   // the mod vehicle picked in Sortie Prep, 0 = a game vehicle or none
    std::atomic<std::uint8_t> g_ReservedType{ 0 };   // the mod vehicle the player deployed with, while it is the relief vehicle
    std::atomic<bool>         g_StateLoaded{ false };
    std::atomic<std::uint8_t> g_DropType{ 0 };       // the mod vehicle picked in the iDroid's supply drop list, until it is ordered

    std::atomic<std::int32_t> g_PendingStock[kMostSortieVehicles] = {};   // recovered this mission: Mother Base gets them when the game adds its temp resources

    thread_local std::uint8_t t_ResourceType = 0;   // the mod vehicle whose Mother Base resource the game is working out
    thread_local std::uint8_t t_PayStockType = 0;   // deploying or dropping this mod vehicle: the game's payment takes it from its stock
    thread_local std::uint8_t t_DropType = 0;
    thread_local bool         t_CursorExhibit = false;
    thread_local std::uint8_t t_CursorType = 0;
    thread_local bool         t_SortieDisplay = false;
    thread_local std::uint8_t t_SortieDisplayType = 0;

    std::uint8_t SavedType(const char* name)
    {
        const std::int32_t type = V_FrameWorkState::GetPersistedConstant(kStateSpace, name);
        return type >= CustomVehicles::kFirstType && type <= CustomVehicles::kLastType ? static_cast<std::uint8_t>(type) : 0;
    }

    void LoadState()
    {
        if (g_StateLoaded.exchange(true, std::memory_order_relaxed))
            return;
        g_SelectedType.store(SavedType("selected"), std::memory_order_relaxed);
        g_ReservedType.store(SavedType("reserved"), std::memory_order_relaxed);
    }

    void SetSelected(std::uint8_t type)
    {
        LoadState();
        if (g_SelectedType.exchange(type, std::memory_order_relaxed) != type)
            V_FrameWorkState::SetPersistedConstant(kStateSpace, "selected", type);
    }

    void SetReserved(std::uint8_t type)
    {
        LoadState();
        if (g_ReservedType.exchange(type, std::memory_order_relaxed) != type)
            V_FrameWorkState::SetPersistedConstant(kStateSpace, "reserved", type);
    }

    std::uint8_t SelectedTypeFor(std::uint8_t resourceId)
    {
        LoadState();
        const std::uint8_t type = g_SelectedType.load(std::memory_order_relaxed);
        SortieVehicle vehicle{};
        return type != 0 && CustomVehicles::SortieVehicleOf(type, vehicle) && vehicle.resourceId == resourceId ? type : 0;
    }

    std::uint8_t ReservedTypeFor(std::uint32_t reliefType)
    {
        LoadState();
        const std::uint8_t type = g_ReservedType.load(std::memory_order_relaxed);
        SortieVehicle vehicle{};
        return type != 0 && CustomVehicles::SortieVehicleOf(type, vehicle) && vehicle.reliefType == reliefType ? type : 0;
    }

    std::int32_t StockOf(std::uint8_t type)
    {
        SortieVehicle vehicle{};
        if (!CustomVehicles::SortieVehicleOf(type, vehicle))
            return 0;
        const std::int32_t stock = V_FrameWorkState::GetPersistedConstant(kStockSpace, vehicle.name);
        return stock > 0 ? stock : 0;
    }

    void AddStock(std::uint8_t type, std::int32_t count)
    {
        SortieVehicle vehicle{};
        if (count == 0 || !CustomVehicles::SortieVehicleOf(type, vehicle))
            return;
        const std::int32_t stock = StockOf(type) + count;
        V_FrameWorkState::SetPersistedConstant(kStockSpace, vehicle.name, stock > 0 ? stock : 0);
    }

    std::uint8_t StockIdOf(std::uint8_t type)
    {
        return static_cast<std::uint8_t>(kStockIdBase + (type - CustomVehicles::kFirstType));
    }

    std::uint8_t TypeOfStockId(std::uint32_t resourceId)
    {
        resourceId &= 0xFF;
        if (resourceId < kStockIdBase || resourceId >= kStockIdBase + kMostSortieVehicles)
            return 0;
        const std::uint8_t type = static_cast<std::uint8_t>(CustomVehicles::kFirstType + (resourceId - kStockIdBase));
        SortieVehicle vehicle{};
        return CustomVehicles::SortieVehicleOf(type, vehicle) ? type : 0;
    }

    std::uint8_t ReservedSortieType()
    {
        LoadState();
        const std::uint8_t type = g_ReservedType.load(std::memory_order_relaxed);
        SortieVehicle vehicle{};
        return CustomVehicles::SortieVehicleOf(type, vehicle) ? type : 0;
    }

    std::uint8_t TypeAtRow(const std::uint8_t* itemSelector, std::uint32_t row)
    {
        if (!itemSelector || itemSelector != g_ListSelector || row < g_ListFirstRow || row - g_ListFirstRow >= g_ListCount)
            return 0;
        return g_ListTypes[row - g_ListFirstRow];
    }

    void ReloadReliefPackSEH(std::uint8_t* vehicleSystem)
    {
        __try
        {
            *reinterpret_cast<std::uint16_t*>(vehicleSystem + kSystemReliefRequest) |= 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ShowInReliefSlot(std::uint8_t* vehicleSystem, std::uint32_t reliefType, std::uint8_t type)
    {
        if (!vehicleSystem || reliefType < 1 || reliefType > kLastReliefType)
            return;
        SortieVehicle vehicle{};
        const bool shown = type != 0 && CustomVehicles::SortieVehicleOf(type, vehicle) && vehicle.reliefType == reliefType;
        if (CustomVehicles::ShowReliefPack(static_cast<std::uint8_t>(reliefType), shown ? vehicle.reliefPack : 0))
            ReloadReliefPackSEH(vehicleSystem);   // the game only loads again when the relief type changes, a mod vehicle shares its game vehicle's
    }

    bool Succeeded(const std::int32_t* result)
    {
        std::int32_t code = -1;
        return result && ReadEntryBytesSEH(result, &code, sizeof(code)) && code == 0;
    }

    enum class VehicleList { None, SortiePrep, SupplyDrop };

    VehicleList OpenVehicleListSEH(const std::uint8_t* itemSelector)
    {
        __try
        {
            if (*reinterpret_cast<const std::uint64_t*>(itemSelector + kSelectorSignatureAt) != kSelectorSignature
                || *reinterpret_cast<const std::uint32_t*>(itemSelector + kSelectorMode) != kModeSortieVehicles)
                return VehicleList::None;
            return *reinterpret_cast<const std::int32_t*>(itemSelector + kSelectorNoNoneRow) == 1 ? VehicleList::SupplyDrop : VehicleList::SortiePrep;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return VehicleList::None;
        }
    }

    bool IsReliefCommInfoSEH(const std::uint8_t* commInfo)
    {
        __try
        {
            return commInfo && (commInfo[kCommInfoTypeCode] >> 4) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TypeCodeSEH(const std::uint8_t* commInfo, std::uint8_t& typeCode)
    {
        __try
        {
            typeCode = commInfo[kCommInfoTypeCode];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool CursorRowSEH(const std::uint8_t* itemSelector, std::uint32_t& row)
    {
        __try
        {
            const std::int32_t cursor = *reinterpret_cast<const std::int32_t*>(itemSelector + kSelectorCursor)
                                      + *reinterpret_cast<const std::int32_t*>(itemSelector + kSelectorTopRow);
            if (cursor < 0)
                return false;
            const std::uint32_t shownRows = *reinterpret_cast<const std::uint32_t*>(itemSelector + kSelectorShownRows);
            row = shownRows != 0 ? static_cast<std::uint32_t>(cursor) % shownRows : static_cast<std::uint32_t>(cursor);   // the drop list wraps round
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t SelectedResourceIdSEH(const std::uint8_t* missionPrep)
    {
        __try
        {
            return missionPrep ? missionPrep[kPrepSelectedId] : kNoVehicle;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoVehicle;
        }
    }

    std::uint8_t* PointerAtSEH(const std::uint8_t* owner, std::size_t field)
    {
        __try
        {
            return *reinterpret_cast<std::uint8_t* const*>(owner + field);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool AppendRowsSEH(std::uint8_t* itemSelector, const SortieVehicle* vehicles, std::size_t vehicleCount, std::uint8_t selectedType,
                       bool supplyDrop, std::uint32_t& firstRow)
    {
        __try
        {
            firstRow = *reinterpret_cast<std::uint32_t*>(itemSelector + kSelectorRowCount);
            if ((firstRow == 0 && !supplyDrop) || firstRow + vehicleCount > kMostRows)   // the Sortie Prep list always starts with its "none" row
                return false;
            std::uint32_t row = firstRow;
            for (std::size_t i = 0; i < vehicleCount; ++i, ++row)
            {
                const SortieVehicle& vehicle = vehicles[i];
                itemSelector[kSelectorRowIds + row] = vehicle.resourceId;
                itemSelector[kSelectorRowCells + row] = 1;
                itemSelector[kSelectorRowCell + row] = 0;
                itemSelector[kSelectorRowEnabled + row * kCellsPerRow] = 1;
                itemSelector[kSelectorRowMarks + row * kCellsPerRow] = 0;
                if (vehicle.type != selectedType)
                    continue;
                for (std::uint32_t gameRow = 0; gameRow < firstRow; ++gameRow)
                {
                    if (itemSelector[kSelectorRowIds + gameRow] == vehicle.resourceId)
                        itemSelector[kSelectorRowMarks + gameRow * kCellsPerRow] = 0;   // the game marked its own row of that resource id as the one in use
                }
                itemSelector[kSelectorRowMarks + row * kCellsPerRow] = 1;
                itemSelector[kSelectorSelectedRow] = static_cast<std::uint8_t>(row);
            }
            *reinterpret_cast<std::uint32_t*>(itemSelector + kSelectorRowCount) = row;
            *reinterpret_cast<std::uint32_t*>(itemSelector + kSelectorShownRows) = row;

            char* countText = reinterpret_cast<char*>(itemSelector + kSelectorCountText);
            std::snprintf(countText, 8, "%02u", row);
            void* ui = *reinterpret_cast<void**>(itemSelector + kSelectorUi);
            void* countNode = *reinterpret_cast<void**>(itemSelector + kSelectorCountNode);
            if (ui && countNode)
            {
                const auto write = reinterpret_cast<WriteText_t>((*reinterpret_cast<void***>(ui))[kUiVtblWriteText]);
                write(ui, countNode, *reinterpret_cast<void**>(itemSelector + kSelectorCountStyle), countText, 1);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void PrefetchIconSEH(std::uint64_t iconFile)
    {
        __try
        {
            const auto getUi = reinterpret_cast<GetUixUtility_t>(ResolveGameAddress(gAddr.GetUixUtilityToFeedQuarkEnvironment));
            void** ui = getUi ? getUi() : nullptr;
            if (ui && *ui)
                reinterpret_cast<PrefetchTexture_t>((*reinterpret_cast<void***>(ui))[kUiVtblPrefetch])(ui, iconFile, 2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool WriteNameAndIconSEH(void* ui, void* nameNode, void* style, void* iconNode, void* iconNode2, const SortieVehicle& vehicle)
    {
        __try
        {
            if (!ui)
                return false;
            void** vtbl = *reinterpret_cast<void***>(ui);
            if (vehicle.iconFile != 0)
            {
                const auto setTexture = reinterpret_cast<SetNodeTexture_t>(vtbl[kUiVtblSetNodeTexture]);
                if (iconNode)
                    setTexture(ui, iconNode, vehicle.iconFile, kIconSlot);
                if (iconNode2)
                    setTexture(ui, iconNode2, vehicle.iconFile, kIconSlot);
            }
            if (vehicle.nameLangId != 0 && nameNode)
            {
                void* text = reinterpret_cast<GetTextByLangId_t>(vtbl[kUiVtblLangText])(ui, vehicle.nameLangId);
                if (text)
                    reinterpret_cast<WriteText_t>(vtbl[kUiVtblWriteText])(ui, nameNode, style, text, 1);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct RecordText
    {
        const void* record = nullptr;
        char        text[32] = {};
    };
    RecordText g_RecordTexts[64] = {};   // UI text nodes read the string after the call returns: one lasting buffer per list record
    std::size_t g_NextRecordText = 0;

    char* RecordCountText(const void* record, std::int32_t count)
    {
        RecordText* slot = nullptr;
        for (RecordText& kept : g_RecordTexts)
        {
            if (kept.record == record)
            {
                slot = &kept;
                break;
            }
        }
        if (!slot)
        {
            slot = &g_RecordTexts[g_NextRecordText];
            g_NextRecordText = (g_NextRecordText + 1) % std::size(g_RecordTexts);
            slot->record = record;
        }
        std::snprintf(slot->text, sizeof(slot->text), "%d", count);
        return slot->text;
    }

    bool DrawRowSEH(std::uint8_t* record, const SortieVehicle& vehicle, const char* countText)
    {
        __try
        {
            void* ui = *reinterpret_cast<void**>(record + kRecordUi);
            void* style = *reinterpret_cast<void**>(record + kRecordStyle);
            if (!WriteNameAndIconSEH(ui, *reinterpret_cast<void**>(record + kRecordNameNode), style,
                                     *reinterpret_cast<void**>(record + kRecordIconNode), *reinterpret_cast<void**>(record + kRecordIconNode2), vehicle))
                return false;
            void* countNode = *reinterpret_cast<void**>(record + kRecordCountNode);
            if (countNode)
                reinterpret_cast<WriteText_t>((*reinterpret_cast<void***>(ui))[kUiVtblWriteText])(ui, countNode, style, countText, 1);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool RowOfRecordSEH(const std::uint8_t* record, const std::uint8_t*& rowIds, std::uint32_t& row)
    {
        __try
        {
            if (record[kRecordBuddyList] != 0 || record[kRecordReliefList] == 0)
                return false;
            rowIds = *reinterpret_cast<const std::uint8_t* const*>(record + kRecordRowIds);
            row = *reinterpret_cast<const std::uint32_t*>(record + kRecordRow);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool VehicleAtListCursor(SortieVehicle& vehicle)
    {
        std::uint32_t row = 0;
        const std::uint8_t* itemSelector = g_ListSelector;
        return itemSelector && OpenVehicleListSEH(itemSelector) != VehicleList::None && CursorRowSEH(itemSelector, row)
            && CustomVehicles::SortieVehicleOf(TypeAtRow(itemSelector, row), vehicle);
    }

    bool DrawDetailsSEH(std::uint8_t* details, const SortieVehicle& vehicle)
    {
        __try
        {
            if (details[kDetailsResourceId] != vehicle.resourceId)
                return true;
            return WriteNameAndIconSEH(*reinterpret_cast<void**>(details + kDetailsUi), *reinterpret_cast<void**>(details + kDetailsNameNode),
                                       *reinterpret_cast<void**>(details + kDetailsStyle), *reinterpret_cast<void**>(details + kDetailsIconNode),
                                       *reinterpret_cast<void**>(details + kDetailsIconNode2), vehicle);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool DrawTopMenuNameSEH(std::uint8_t* prepCallback, std::uint8_t* missionPrep, const SortieVehicle& vehicle)
    {
        __try
        {
            const auto isVehicleEnabled = reinterpret_cast<IsVehicleEnabled_t>((*reinterpret_cast<void***>(missionPrep))[kPrepVtblIsVehicleEnabled]);
            if (!isVehicleEnabled(missionPrep))
                return true;
            return WriteNameAndIconSEH(*reinterpret_cast<void**>(prepCallback + kCallbackUi), *reinterpret_cast<void**>(prepCallback + kCallbackNameNode),
                                       *reinterpret_cast<void**>(prepCallback + kCallbackStyle), nullptr, nullptr, vehicle);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsVehicleIndexSEH(const std::uint8_t* vehicles, std::uint32_t index)
    {
        __try
        {
            return index < *reinterpret_cast<const std::uint16_t*>(vehicles + kVehiclesCount);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WriteResultSEH(std::int32_t* result, std::int32_t value)
    {
        __try
        {
            if (result)
                *result = value;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hk_ExhibitSelectingRelief(std::uint8_t* itemSelector, std::uint32_t row)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigExhibitSelectingRelief, itemSelector, row);
        const bool outer = t_CursorExhibit;
        const std::uint8_t outerType = t_CursorType;
        t_CursorType = TypeAtRow(itemSelector, row);
        t_CursorExhibit = true;
        g_OrigExhibitSelectingRelief(itemSelector, row);
        t_CursorExhibit = outer;
        t_CursorType = outerType;
    }

    void DisplayForSortie(VehicleDisplay_t original, std::uint8_t* missionPrep, std::uint8_t resourceId, std::int32_t coloring)
    {
        const bool outer = t_SortieDisplay;
        const std::uint8_t outerType = t_SortieDisplayType;
        t_SortieDisplayType = t_CursorExhibit ? t_CursorType : SelectedTypeFor(resourceId);
        t_SortieDisplay = true;
        original(missionPrep, resourceId, coloring);   // the model viewer asks the vehicle system to exhibit right here
        t_SortieDisplay = outer;
        t_SortieDisplayType = outerType;
    }

    void __fastcall hk_StartVehicleDisplay(std::uint8_t* missionPrep, std::uint8_t resourceId, std::int32_t coloring)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigStartVehicleDisplay, missionPrep, resourceId, coloring);
        DisplayForSortie(g_OrigStartVehicleDisplay, missionPrep, resourceId, coloring);
    }

    void __fastcall hk_ChangeVehicleDisplay(std::uint8_t* missionPrep, std::uint8_t resourceId, std::int32_t coloring)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigChangeVehicleDisplay, missionPrep, resourceId, coloring);
        DisplayForSortie(g_OrigChangeVehicleDisplay, missionPrep, resourceId, coloring);
    }

    std::int32_t* __fastcall hk_ExhibitRelief(std::uint8_t* vehicleSystem, std::int32_t* result, std::uint32_t reliefType, std::uint32_t coloring, const float* transform)
    {
        std::int32_t* returned = g_OrigExhibitRelief(vehicleSystem, result, reliefType, coloring, transform);
        if (!MissionCodeGuard::ShouldBypassHooks() && Succeeded(result))
            ShowInReliefSlot(vehicleSystem, reliefType & 0xFF, t_SortieDisplay ? t_SortieDisplayType : 0);   // Mother Base customize exhibits too and always gets the game's vehicle
        return returned;
    }

    void __fastcall hk_ReserveSelectionRelief(std::uint8_t* prepCallback)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigReserveSelectionRelief, prepCallback);
        const std::uint8_t resourceId = SelectedResourceIdSEH(PointerAtSEH(prepCallback, kCallbackPrep));
        const std::uint8_t type = SelectedTypeFor(resourceId);
        SetReserved(type);
        auto** vehicleSystem = static_cast<std::uint8_t**>(ResolveGameAddress(gAddr.VehicleSystemImpl_Instance));
        if (vehicleSystem && *vehicleSystem)
            ShowInReliefSlot(*vehicleSystem, CustomVehicles::ReliefTypeOfResource(resourceId), type);
        g_ListSelector = nullptr;
        const std::uint8_t outer = t_PayStockType;
        t_PayStockType = type;   // the game takes one of the selected resource id from Mother Base here: a mod vehicle comes out of its own count
        g_OrigReserveSelectionRelief(prepCallback);
        t_PayStockType = outer;
    }

    std::int32_t __fastcall hk_PayResourceCost(void* resources, std::uint8_t resourceId, std::int32_t count)
    {
        const std::uint8_t type = t_PayStockType != 0 ? t_PayStockType : TypeOfStockId(resourceId);
        if (type == 0)
            return g_OrigPayResourceCost(resources, resourceId, count);
        AddStock(type, -count);
        return 0;
    }

    bool __fastcall hk_CanPayResourceCost(void* resources, std::uint8_t resourceId, std::int32_t count)
    {
        const std::uint8_t type = t_PayStockType != 0 ? t_PayStockType : TypeOfStockId(resourceId);
        if (type == 0)
            return g_OrigCanPayResourceCost(resources, resourceId, count);
        return StockOf(type) >= count;
    }

    void __fastcall hk_AddResource(void* resources, std::uint8_t resourceId, std::uint64_t count, std::uint64_t visual,
                                   std::uint64_t a, std::uint64_t b, std::uint64_t c)
    {
        const std::uint8_t type = TypeOfStockId(resourceId);
        if (type == 0)
        {
            g_OrigAddResource(resources, resourceId, count, visual, a, b, c);
            return;
        }
        AddStock(type, static_cast<std::int32_t>(count));   // straight to Mother Base, like the game's own direct account
    }

    void __fastcall hk_AddTempResource(void* motherBase, std::uint8_t resourceId, std::int32_t count, std::uint64_t visual,
                                       std::uint64_t owner, std::uint64_t extra)
    {
        const std::uint8_t type = TypeOfStockId(resourceId);
        if (type == 0)
        {
            g_OrigAddTempResource(motherBase, resourceId, count, visual, owner, extra);
            return;
        }
        if (count > 0)
            g_PendingStock[type - CustomVehicles::kFirstType].fetch_add(count, std::memory_order_relaxed);
    }

    void __fastcall hk_AddResourcesFromTempResourceBuffer(void* resources)
    {
        g_OrigAddResourcesFromTemp(resources);
        if (MissionCodeGuard::ShouldBypassHooks())
            return;
        for (std::size_t i = 0; i < kMostSortieVehicles; ++i)
            AddStock(static_cast<std::uint8_t>(CustomVehicles::kFirstType + i), g_PendingStock[i].exchange(0, std::memory_order_relaxed));
    }

    void __fastcall hk_SetTopMenuVehicleName(std::uint8_t* prepCallback)
    {
        g_OrigSetTopMenuVehicleName(prepCallback);
        if (MissionCodeGuard::ShouldBypassHooks())
            return;
        std::uint8_t* missionPrep = PointerAtSEH(prepCallback, kCallbackPrep);
        SortieVehicle vehicle{};
        if (!missionPrep || !CustomVehicles::SortieVehicleOf(SelectedTypeFor(SelectedResourceIdSEH(missionPrep)), vehicle))
            return;
        if (!DrawTopMenuNameSEH(prepCallback, missionPrep, vehicle))
            LogWarnOnce("VehicleSortieRows: the Sortie Prep top menu could not be given the mod vehicle's name, the game vehicle's name stays");
    }

    std::int32_t* __fastcall hk_SpawnRelief(std::uint8_t* vehicleSystem, std::int32_t* result, std::uint32_t reliefType, const float* position, float rotY, std::uint32_t options)
    {
        std::int32_t* returned = g_OrigSpawnRelief(vehicleSystem, result, reliefType, position, rotY, options);
        if (!MissionCodeGuard::ShouldBypassHooks() && Succeeded(result))
            ShowInReliefSlot(vehicleSystem, reliefType, ReservedTypeFor(reliefType));
        return returned;
    }

    std::int32_t* __fastcall hk_DeliverRelief(std::uint8_t* vehicleSystem, std::int32_t* result, std::uint32_t reliefType, const float* position, float rotY)
    {
        std::int32_t* returned = g_OrigDeliverRelief(vehicleSystem, result, reliefType, position, rotY);
        if (!MissionCodeGuard::ShouldBypassHooks() && Succeeded(result))
        {
            SetReserved(t_DropType);   // a supply drop replaces the relief vehicle
            ShowInReliefSlot(vehicleSystem, reliefType, t_DropType);
        }
        return returned;
    }

    std::uint8_t __fastcall hk_ConvertIntoResourceId(std::uint32_t reliefType)
    {
        SortieVehicle vehicle{};
        if (t_ResourceType != 0 && CustomVehicles::SortieVehicleOf(t_ResourceType, vehicle) && vehicle.reliefType == (reliefType & 0xFF))
            return StockIdOf(t_ResourceType);   // a mod vehicle's own id: every place the game credits or pays it goes to its own count
        return g_OrigConvertIntoResourceId(reliefType);
    }

    std::uint8_t StockTypeOfVehicle(const std::uint8_t* commInfo)
    {
        std::uint8_t typeCode = 0;
        if (MissionCodeGuard::ShouldBypassHooks() || !TypeCodeSEH(commInfo, typeCode))
            return 0;
        const std::uint8_t type = typeCode >> 4;
        if (type == 0)
            return ReservedSortieType();   // the relief vehicle is the mod vehicle the player deployed with
        SortieVehicle vehicle{};
        return CustomVehicles::SortieVehicleOf(type, vehicle) ? type : 0;
    }

    std::uint8_t* CommInfoOfSEH(const std::uint8_t* vehicles, std::size_t commInfosAt, std::uint32_t index)
    {
        __try
        {
            return *reinterpret_cast<std::uint8_t* const*>(vehicles + commInfosAt) + index * kCommInfoSize;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void __fastcall hk_ReflectLifeState(void* instances, std::uint16_t index, std::uint8_t* commInfo)
    {
        const std::uint8_t outer = t_ResourceType;
        t_ResourceType = StockTypeOfVehicle(commInfo);
        g_OrigReflectLifeState(instances, index, commInfo);
        t_ResourceType = outer;
    }

    void __fastcall hk_FultonRecovery(void* maneuverController, std::uint8_t index, std::uint8_t* commInfo)
    {
        const std::uint8_t outer = t_ResourceType;
        t_ResourceType = StockTypeOfVehicle(commInfo);
        g_OrigFultonRecovery(maneuverController, index, commInfo);
        t_ResourceType = outer;
    }

    void WriteLangIdSEH(std::uint64_t* langId, std::uint64_t value)
    {
        __try
        {
            *langId = value;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint8_t* FieldCommInfoSEH(std::uint32_t index)
    {
        __try
        {
            auto** system = static_cast<std::uint8_t**>(ResolveGameAddress(gAddr.VehicleSystemImpl_Instance));
            if (!system || !*system)
                return nullptr;
            const std::uint8_t* vehicles = *reinterpret_cast<std::uint8_t* const*>(*system + kSystemVehicles);
            if (!vehicles)
                return nullptr;
            const std::uint32_t* alive = *reinterpret_cast<const std::uint32_t* const*>(vehicles + kVehiclesAliveBits);
            if (!alive || ((alive[index >> 5] >> (index & 0x1F)) & 1) == 0)
                return nullptr;
            return *reinterpret_cast<std::uint8_t* const*>(vehicles + kSeizeCommInfos) + index * kCommInfoSize;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint64_t* __fastcall hk_GetUniqueIconLangId(void* mapData, std::uint64_t* langId, std::uint16_t gameObjectId)
    {
        std::uint64_t* returned = g_OrigGetUniqueIconLangId(mapData, langId, gameObjectId);
        if (MissionCodeGuard::ShouldBypassHooks() || !langId || (gameObjectId & kGameObjectTypeMask) != kVehicleGameObjects)
            return returned;
        std::uint64_t current = 0;
        if (!ReadEntryBytesSEH(langId, &current, sizeof(current)) || (current & kLangIdMask) != 0)
            return returned;   // a mission set its own label for this vehicle
        SortieVehicle vehicle{};
        if (!CustomVehicles::SortieVehicleOf(StockTypeOfVehicle(FieldCommInfoSEH(gameObjectId & kGameObjectIndexMask)), vehicle)
            || vehicle.nameLangId == 0)
            return returned;
        WriteLangIdSEH(langId, vehicle.nameLangId);
        return returned;
    }

    std::uint8_t ResourceTypeAtRow(const std::uint8_t* resourcePage, std::uint32_t row)
    {
        if (!resourcePage || resourcePage != g_ResourcePage || row < g_ResourceFirstRow || row - g_ResourceFirstRow >= g_ResourceCount)
            return 0;
        return g_ResourceTypes[row - g_ResourceFirstRow];
    }

    bool ResourceCursorRowSEH(const std::uint8_t* resourcePage, std::uint32_t& row)
    {
        __try
        {
            const std::uint32_t rows = *reinterpret_cast<const std::uint32_t*>(resourcePage + kPageRowCount);
            if (rows == 0)
                return false;
            row = (*reinterpret_cast<const std::uint32_t*>(resourcePage + kPageTopRow)
                 + *reinterpret_cast<const std::uint32_t*>(resourcePage + kPageCursor)) % rows;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool AppendResourceRowsSEH(std::uint8_t* resourcePage, std::uint8_t category, const SortieVehicle* vehicles, std::size_t count,
                               std::uint8_t* types, std::uint32_t& firstRow, std::uint32_t& added)
    {
        __try
        {
            void* resources = *reinterpret_cast<void**>(resourcePage + kPageResources);
            if (!resources)
                return false;
            const auto categoryOf = reinterpret_cast<ResourceCategory_t>((*reinterpret_cast<void***>(resources))[kResourcesVtblCategory]);
            std::uint32_t rows = *reinterpret_cast<std::uint32_t*>(resourcePage + kPageRowCount);
            firstRow = rows;
            added = 0;
            for (std::size_t i = 0; i < count && rows < kPageMostRows; ++i)
            {
                if (categoryOf(resources, vehicles[i].resourceId) != category)
                    continue;   // the Vehicles tab only
                resourcePage[kPageRowIds + rows++] = vehicles[i].resourceId;
                types[added++] = vehicles[i].type;
            }
            *reinterpret_cast<std::uint32_t*>(resourcePage + kPageRowCount) = rows;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hk_RefreshResourceList(std::uint8_t* resourcePage, std::uint8_t category)
    {
        g_OrigRefreshResourceList(resourcePage, category);
        if (resourcePage == g_ResourcePage)
            g_ResourcePage = nullptr;
        if (!resourcePage || MissionCodeGuard::ShouldBypassHooks())
            return;
        SortieVehicle registered[kMostSortieVehicles] = {};
        const std::size_t registeredCount = CustomVehicles::SortieVehicles(registered, kMostSortieVehicles);
        SortieVehicle owned[kMostSortieVehicles] = {};
        std::size_t ownedCount = 0;
        for (std::size_t i = 0; i < registeredCount; ++i)
        {
            if (StockOf(registered[i].type) > 0)
                owned[ownedCount++] = registered[i];
        }
        std::uint32_t firstRow = 0;
        std::uint32_t added = 0;
        if (ownedCount == 0 || !AppendResourceRowsSEH(resourcePage, category, owned, ownedCount, g_ResourceTypes, firstRow, added) || added == 0)
            return;
        g_ResourceFirstRow = firstRow;
        g_ResourceCount = added;
        g_ResourcePage = resourcePage;
    }

    bool DrawResourceRowSEH(std::uint8_t* record, const SortieVehicle& vehicle, std::int32_t stock)
    {
        __try
        {
            const std::uint8_t* pageState = *reinterpret_cast<std::uint8_t* const*>(record + kRecordPage);
            void* ui = *reinterpret_cast<void* const*>(pageState + kPageUiFromState);
            void* style = *reinterpret_cast<void* const*>(pageState + kPageStyleFromState);
            if (!ui)
                return false;
            void** vtbl = *reinterpret_cast<void***>(ui);
            if (vehicle.iconFile != 0)
            {
                const auto setTexture = reinterpret_cast<SetNodeTexture_t>(vtbl[kUiVtblSetNodeTexture]);
                setTexture(ui, *reinterpret_cast<void**>(record + kResRecordIconNode), vehicle.iconFile, kIconSlot);
                setTexture(ui, *reinterpret_cast<void**>(record + kResRecordIconNode2), vehicle.iconFile, kIconSlot);
            }
            if (vehicle.nameLangId != 0)
            {
                void* text = reinterpret_cast<GetTextByLangId_t>(vtbl[kUiVtblLangText])(ui, vehicle.nameLangId);
                if (text)
                    reinterpret_cast<WriteText_t>(vtbl[kUiVtblWriteName])(ui, *reinterpret_cast<void**>(record + kResRecordNameNode), style, text, 1);
            }
            const auto write = reinterpret_cast<WriteText_t>(vtbl[kUiVtblWriteText]);
            char* countText = reinterpret_cast<char*>(record + kResRecordCountText);   // the record's own text buffers, like the game's: the row reads them later
            char* localText = reinterpret_cast<char*>(record + kResRecordLocalText);
            char* serverText = reinterpret_cast<char*>(record + kResRecordServerText);
            std::snprintf(countText, kResRecordTextSize, "%d", stock);
            std::snprintf(localText, kResRecordTextSize, "%d", stock);   // all of it on Mother Base, none on the server
            std::snprintf(serverText, kResRecordTextSize, "0");
            write(ui, *reinterpret_cast<void**>(record + kResRecordCountNode), style, countText, 1);
            write(ui, *reinterpret_cast<void**>(record + kResRecordLocalNode), style, localText, 1);
            write(ui, *reinterpret_cast<void**>(record + kResRecordServerNode), style, serverText, 1);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool RecordRowOfPageSEH(const std::uint8_t* record, const std::uint8_t*& resourcePage, std::uint32_t& row)
    {
        __try
        {
            resourcePage = *reinterpret_cast<const std::uint8_t* const*>(record + kRecordPage) - kPageStateAt;
            row = *reinterpret_cast<const std::uint32_t*>(record + kRecordRow);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hk_ResourceRecordStart(std::uint8_t* record, void* window)
    {
        g_OrigResourceRecordStart(record, window);
        const std::uint8_t* resourcePage = nullptr;
        std::uint32_t row = 0;
        SortieVehicle vehicle{};
        if (!record || !g_ResourcePage || MissionCodeGuard::ShouldBypassHooks() || !RecordRowOfPageSEH(record, resourcePage, row)
            || !CustomVehicles::SortieVehicleOf(ResourceTypeAtRow(resourcePage, row), vehicle))
            return;
        if (!DrawResourceRowSEH(record, vehicle, StockOf(vehicle.type)))
            LogWarnOnce("VehicleSortieRows: a mod vehicle's Resources row could not be given its name, the game vehicle's name stays");
    }

    bool WriteResourceInfoSEH(std::uint8_t* resourcePage, const SortieVehicle& vehicle)
    {
        __try
        {
            void* ui = *reinterpret_cast<void**>(resourcePage + kPageUi);
            if (!ui || vehicle.nameLangId == 0)
                return true;
            void** vtbl = *reinterpret_cast<void***>(ui);
            void* text = reinterpret_cast<GetTextByLangId_t>(vtbl[kUiVtblLangText])(ui, vehicle.nameLangId);
            if (text)
                reinterpret_cast<WriteInfoText_t>(vtbl[kUiVtblWriteInfo])(ui, *reinterpret_cast<void**>(resourcePage + kPageInfoNode),
                                                                         *reinterpret_cast<void**>(resourcePage + kPageInfoStyle), 8, text, 1, 1);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hk_ResourceInfoText(std::uint8_t* resourcePage)
    {
        g_OrigResourceInfoText(resourcePage);
        std::uint32_t row = 0;
        SortieVehicle vehicle{};
        if (!resourcePage || resourcePage != g_ResourcePage || MissionCodeGuard::ShouldBypassHooks() || !ResourceCursorRowSEH(resourcePage, row)
            || !CustomVehicles::SortieVehicleOf(ResourceTypeAtRow(resourcePage, row), vehicle))
            return;
        if (!WriteResourceInfoSEH(resourcePage, vehicle))
            LogWarnOnce("VehicleSortieRows: the Resources info text could not be given the mod vehicle's name");
    }

    std::int32_t __fastcall hk_ResourceUsableCount(void* resources, std::uint8_t resourceId)
    {
        if (t_SaleRowId == resourceId)
            return StockOf(t_SaleRowType);
        return g_OrigResourceUsableCount(resources, resourceId);
    }

    std::int32_t __fastcall hk_ResourceServerCount(void* resources, std::uint8_t resourceId)
    {
        if (t_SaleRowId == resourceId)
            return 0;   // none of a mod vehicle is ever on the server
        return g_OrigResourceServerCount(resources, resourceId);
    }

    void AddSaleGmpSEH(std::uint8_t* resources, std::uint8_t resourceId, std::int32_t count)
    {
        __try
        {
            const std::uint32_t price = *reinterpret_cast<const std::uint32_t*>(resources + kResourcesSalePrices + resourceId * sizeof(std::uint32_t));
            std::uint32_t gmp = price * static_cast<std::uint32_t>(count);
            if (gmp > kMostSaleGmp)
                gmp = kMostSaleGmp;   // the game's own cap
            void* wallet = *reinterpret_cast<void**>(resources + kResourcesWallet);
            if (wallet)
                reinterpret_cast<AddGmp_t>((*reinterpret_cast<void***>(wallet))[kWalletVtblAddGmp])(wallet, gmp);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint8_t ModRowUnderResourceCursor(std::uint8_t resourceId)
    {
        std::uint8_t* resourcePage = g_ResourcePage;   // the sale may run from the popup, outside the page's own update
        std::uint32_t row = 0;
        SortieVehicle vehicle{};
        if (!resourcePage || MissionCodeGuard::ShouldBypassHooks() || !ResourceCursorRowSEH(resourcePage, row)
            || !CustomVehicles::SortieVehicleOf(ResourceTypeAtRow(resourcePage, row), vehicle) || vehicle.resourceId != resourceId)
            return 0;
        return vehicle.type;
    }

    std::uint8_t PageStepSEH(const std::uint8_t* resourcePage)
    {
        __try
        {
            return resourcePage[kPageStep];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    // the popup counts straight from the resource arrays and offers a server sale for anything over the local count:
    // a mod vehicle has only its own stock, all local, and never goes to the server
    bool FitSalePopupSEH(std::uint8_t* resourcePage, const SortieVehicle& vehicle, std::int32_t stock)
    {
        __try
        {
            if (resourcePage[kPopupResourceId] != vehicle.resourceId)
                return false;
            auto& localMost = *reinterpret_cast<std::int32_t*>(resourcePage + kPopupLocalMost);
            *reinterpret_cast<std::int32_t*>(resourcePage + kPopupOwned) = stock;
            localMost = (std::max)(0, (std::min)(localMost, stock));
            *reinterpret_cast<std::int32_t*>(resourcePage + kPopupServerMost) = 0;
            std::uint8_t* quantity = *reinterpret_cast<std::uint8_t**>(resourcePage + kPopupQuantity);
            if (quantity)
            {
                *reinterpret_cast<std::int32_t*>(quantity + kQuantityCount) = localMost;
                reinterpret_cast<void(__fastcall*)(void*)>((*reinterpret_cast<void***>(quantity))[kQuantityVtblRefresh])(quantity);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SetImageRecordSEH(std::uint8_t* resourcePage, std::uint8_t* imageRecord, std::uint64_t iconFile)
    {
        __try
        {
            void* ui = *reinterpret_cast<void**>(resourcePage + kPageUi);
            if (!ui)
                return false;
            const auto setTexture = reinterpret_cast<SetNodeTexture_t>((*reinterpret_cast<void***>(ui))[kUiVtblSetNodeTexture]);
            for (const std::size_t node : { kImageRecordNode, kImageRecordNode2 })
            {
                setTexture(ui, *reinterpret_cast<void**>(imageRecord + node), iconFile, kIconSlot);
                setTexture(ui, *reinterpret_cast<void**>(imageRecord + node), iconFile, kImageSlot2);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // the big picture on the left of Resources: the game draws the vehicle the row borrows
    void __fastcall hk_RefreshImageRecord(std::uint8_t* resourcePage, std::uint8_t* imageRecord, std::uint16_t category,
                                          std::uint64_t row, std::uint16_t slot)
    {
        g_OrigRefreshImageRecord(resourcePage, imageRecord, category, row, slot);
        SortieVehicle vehicle{};
        if (!resourcePage || resourcePage != g_ResourcePage || !imageRecord || MissionCodeGuard::ShouldBypassHooks()
            || !CustomVehicles::SortieVehicleOf(ResourceTypeAtRow(resourcePage, static_cast<std::uint32_t>(row & 0xFFFF)), vehicle)
            || vehicle.iconFile == 0)
            return;
        PrefetchIconSEH(vehicle.iconFile);
        if (!SetImageRecordSEH(resourcePage, imageRecord, vehicle.iconFile))
            LogWarnOnce("VehicleSortieRows: the Resources picture could not be given the mod vehicle's picture");
    }

    bool SalePopupResourceSEH(const std::uint8_t* popup, std::uint8_t& resourceId)
    {
        __try
        {
            const std::uint8_t* state = *reinterpret_cast<const std::uint8_t* const*>(popup + kPopupState);
            if (!state || state - kPageStateAt != g_ResourcePage)
                return false;
            resourceId = state[kStateResourceId];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WriteSalePopupItemSEH(std::uint8_t* popup, const SortieVehicle& vehicle)
    {
        __try
        {
            const std::uint8_t* state = *reinterpret_cast<const std::uint8_t* const*>(popup + kPopupState);
            void* ui = *reinterpret_cast<void* const*>(state + kStateUi);
            void* style = *reinterpret_cast<void* const*>(state + kStateStyle);
            if (!ui)
                return false;
            void** vtbl = *reinterpret_cast<void***>(ui);
            if (vehicle.iconFile != 0)
            {
                const auto setTexture = reinterpret_cast<SetNodeTexture_t>(vtbl[kUiVtblSetNodeTexture]);
                setTexture(ui, *reinterpret_cast<void**>(popup + kPopupPicture), vehicle.iconFile, kIconSlot);
                setTexture(ui, *reinterpret_cast<void**>(popup + kPopupPicture2), vehicle.iconFile, kIconSlot);
            }
            if (vehicle.nameLangId != 0)
            {
                void* text = reinterpret_cast<GetTextByLangId_t>(vtbl[kUiVtblLangText])(ui, vehicle.nameLangId);
                if (text)
                    reinterpret_cast<WriteText_t>(vtbl[kUiVtblWriteText])(ui, *reinterpret_cast<void**>(popup + kPopupItemName), style, text, 1);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // the sale popup's picture and name come from the resource id: the borrowed game vehicle's
    void __fastcall hk_SalePopupRefresh(std::uint8_t* popup)
    {
        g_OrigSalePopupRefresh(popup);
        std::uint8_t resourceId = 0;
        SortieVehicle vehicle{};
        if (!popup || MissionCodeGuard::ShouldBypassHooks() || !SalePopupResourceSEH(popup, resourceId)
            || !CustomVehicles::SortieVehicleOf(ModRowUnderResourceCursor(resourceId), vehicle))
            return;
        if (vehicle.iconFile != 0)
            PrefetchIconSEH(vehicle.iconFile);
        if (!WriteSalePopupItemSEH(popup, vehicle))
            LogWarnOnce("VehicleSortieRows: the sale popup could not be given the mod vehicle's picture and name");
    }

    void __fastcall hk_SaleResource(std::uint8_t* resources, std::uint8_t resourceId, std::int32_t count)
    {
        const std::uint8_t type = ModRowUnderResourceCursor(resourceId);
        if (type == 0 || count <= 0)
        {
            g_OrigSaleResource(resources, resourceId, count);
            return;
        }
        const std::int32_t sold = (std::min)(count, StockOf(type));   // never pays for more than Mother Base has
        AddStock(type, -sold);                          // the mod vehicle leaves its own count
        AddSaleGmpSEH(resources, resourceId, sold);     // and pays what the game vehicle it counts as is worth
    }

    const DispatchRewards* RewardsOfRow(std::uint32_t row)
    {
        if (row == 0 || row >= kMostRewardRows || g_DispatchRewards[row].lines.empty())
            return nullptr;
        return &g_DispatchRewards[row];
    }

    bool VehicleNamed(const std::string& name, SortieVehicle& vehicle)
    {
        SortieVehicle vehicles[kMostSortieVehicles] = {};
        const std::size_t vehicleCount = CustomVehicles::SortieVehicles(vehicles, kMostSortieVehicles);
        for (std::size_t i = 0; i < vehicleCount; ++i)
        {
            if (name == vehicles[i].name)
            {
                vehicle = vehicles[i];
                return true;
            }
        }
        return false;
    }

    void LineLook(const RewardLine& line, std::uint64_t& iconFile, std::uint64_t& nameLangId)
    {
        iconFile = line.iconFile;
        nameLangId = line.nameLangId;
        SortieVehicle vehicle{};
        if (line.kind == RewardLine::Kind::Vehicle && VehicleNamed(line.vehicle, vehicle))
        {
            if (iconFile == 0)
                iconFile = vehicle.iconFile;
            if (nameLangId == 0)
                nameLangId = vehicle.nameLangId;
        }
    }

    bool ReadRewardEntrySEH(const std::uint8_t* rewards, std::uint64_t index, std::uint32_t& entry)
    {
        __try
        {
            const std::uint32_t* entries = *reinterpret_cast<const std::uint32_t* const*>(rewards + kRewardEntries);
            if (!entries || index >= 200)
                return false;
            entry = entries[index];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // our Rewards entries: the game's vehicle reward type on a custom reward row, the line number in the vehicle index bits
    const RewardLine* RewardLineOfEntry(std::uint32_t entry, const DispatchRewards*& rewards)
    {
        const bool vehicleType = ((entry >> 9) & 0x1F) == kPoolRewardVehicle;
        const bool special = ((entry >> 19) & 1) != 0 || ((entry >> 21) & 1) != 0;   // MB coin / online entries are the game's own
        rewards = vehicleType && !special ? RewardsOfRow(entry & 0x1FF) : nullptr;
        const std::size_t line = (entry >> 22) & 0xF;
        if (!rewards || line >= rewards->lines.size() || rewards->lines[line].kind == RewardLine::Kind::Pool)
            return nullptr;
        return &rewards->lines[line];
    }

    bool RollRewardLine(std::uint16_t seed, std::uint32_t row, std::size_t line, std::uint8_t chance)
    {
        if (chance >= 100)
            return true;
        std::uint32_t mixed = seed * 0x9E3779B1u ^ row * 0x85EBCA77u ^ static_cast<std::uint32_t>(line + 1) * 0xC2B2AE3Du;   // fixed per mission seed, like the game's own rolls
        mixed ^= mixed >> 15;
        mixed *= 0x2C1B3C6Du;
        mixed ^= mixed >> 12;
        return mixed % 100 < chance;
    }

    std::int16_t RewardRowSEH(const std::int16_t* rewardsInfo)
    {
        __try
        {
            return rewardsInfo ? *rewardsInfo : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void __fastcall hk_AddPoolRewards(std::uint8_t* rewards, const std::int16_t* rewardsInfo, std::uint8_t forFob,
                                      std::uint16_t seed, std::uint8_t rank, std::uint32_t source)
    {
        g_OrigAddPoolRewards(rewards, rewardsInfo, forFob, seed, rank, source);
        if (forFob != 0 || MissionCodeGuard::ShouldBypassHooks())
            return;
        const std::int16_t row = RewardRowSEH(rewardsInfo);
        const DispatchRewards* dispatch = row > 0 ? RewardsOfRow(static_cast<std::uint32_t>(row)) : nullptr;
        const auto addCore = reinterpret_cast<AddPoolRewardCore_t>(ResolveGameAddress(gAddr.PoolRewardControllerImpl_AddPoolRewardCore));
        if (!dispatch || !addCore)
            return;
        std::int32_t gmpLeft = 0;
        std::int32_t staffLeft = 0;
        std::int32_t resourcesLeft = 0;
        const bool capped = deployMission::VanillaRewardMaximums(gmpLeft, staffLeft, resourcesLeft);   // together never more than one vanilla reward pays: GMP, staff and resources reach FOB
        for (std::size_t i = 0; i < dispatch->lines.size(); ++i)
        {
            const RewardLine& line = dispatch->lines[i];
            if (!RollRewardLine(seed, static_cast<std::uint32_t>(row), i, line.chance))
                continue;
            if (line.kind == RewardLine::Kind::Pool)
            {
                std::int32_t& left = line.poolType == kPoolRewardGmp ? gmpLeft
                                   : line.poolType <= kPoolRewardLastStaff ? staffLeft : resourcesLeft;
                const std::int32_t amount = capped ? (std::min)(line.count, left) : 0;
                if (amount <= 0)
                    continue;
                left -= amount;
                addCore(rewards, 0, line.poolType, 0, line.section, rank, source, static_cast<std::uint32_t>(amount),
                        0, 0, 0, 0, 0, 0, 0, 0, 0);   // row 0 + amount: the game lists and pays the exact amount itself, staff join the seed's section
            }
            else
                addCore(rewards, 0, kPoolRewardVehicle, static_cast<std::uint16_t>(row), seed, rank, source, 0,
                        0, 0, 0, static_cast<std::uint8_t>(i), 0, 0, 0, 0, 0);
        }
    }

    void __fastcall hk_AcceptPoolRewardOne(std::uint8_t* rewards, std::uint64_t index)
    {
        std::uint32_t entry = 0;
        const DispatchRewards* dispatch = nullptr;
        const RewardLine* line = !MissionCodeGuard::ShouldBypassHooks() && ReadRewardEntrySEH(rewards, index, entry)
                               ? RewardLineOfEntry(entry, dispatch) : nullptr;
        if (!line)
        {
            g_OrigAcceptPoolRewardOne(rewards, index);
            return;
        }
        SortieVehicle vehicle{};
        if (line->kind == RewardLine::Kind::Vehicle)
        {
            if (VehicleNamed(line->vehicle, vehicle))
                AddStock(vehicle.type, line->count);   // the game removes the entry after this
            else
                LogWarn("Dispatch reward: vehicle '%s' of '%s' is not registered this launch, its reward is lost", line->vehicle.c_str(), dispatch->missionKey.c_str());
            return;
        }
        V_FrameWork::QueueMessage("Dispatch", "OnRewardReceived", dispatch->missionKey, line->name, line->count);
    }

    bool DrawRewardRowSEH(std::uint8_t* record, std::uint64_t row, std::uint8_t*& page, std::uint32_t& entry)
    {
        __try
        {
            page = *reinterpret_cast<std::uint8_t**>(record + kListPage);
            const std::uint8_t index = page[kListEntryIndex + (row & 0xFFFFFFFF)];
            const std::uint8_t* rewards = *reinterpret_cast<std::uint8_t* const*>(record + kListRewards) - kPageStateAt;
            return ReadRewardEntrySEH(rewards, index, entry);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WriteRewardRowSEH(std::uint8_t* record, std::uint8_t* page, std::uint64_t iconFile, std::uint64_t nameLangId, std::int32_t count)
    {
        __try
        {
            void* ui = *reinterpret_cast<void**>(page + kListPageUi);
            void* style = *reinterpret_cast<void**>(page + kListPageStyle);
            if (!ui)
                return false;
            void** vtbl = *reinterpret_cast<void***>(ui);
            if (iconFile != 0)
                reinterpret_cast<SetNodeTexture_t>(vtbl[kUiVtblSetNodeTexture])(ui, *reinterpret_cast<void**>(record + kListIconNode), iconFile, kIconSlot);
            if (nameLangId != 0)
            {
                void* text = reinterpret_cast<GetTextByLangId_t>(vtbl[kUiVtblLangText])(ui, nameLangId);
                if (text)
                    reinterpret_cast<WriteText_t>(vtbl[kUiVtblWriteText])(ui, *reinterpret_cast<void**>(record + kListNameNode), style, text, 1);
            }
            char* countText = reinterpret_cast<char*>(record + kListCountText);   // the UI reads the text later: it has to live in the record, like the game's own
            std::snprintf(countText, kListCountTextSize, "x%d", count);
            reinterpret_cast<WriteText_t>(vtbl[kUiVtblWriteText])(ui, *reinterpret_cast<void**>(record + kListCountNode), style, countText, 1);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hk_RewardListRow(std::uint8_t* record, std::uint64_t row)
    {
        g_OrigRewardListRow(record, row);
        std::uint8_t* page = nullptr;
        std::uint32_t entry = 0;
        const DispatchRewards* dispatch = nullptr;
        const RewardLine* line = record && !MissionCodeGuard::ShouldBypassHooks() && DrawRewardRowSEH(record, row, page, entry)
                               ? RewardLineOfEntry(entry, dispatch) : nullptr;
        if (!line)
            return;
        std::uint64_t iconFile = 0;
        std::uint64_t nameLangId = 0;
        LineLook(*line, iconFile, nameLangId);
        if (!WriteRewardRowSEH(record, page, iconFile, nameLangId, line->count))
            LogWarnOnce("Dispatch reward: a Rewards row could not be given its picture and name");
    }

    bool DrawPreviewVehicleSEH(std::uint8_t* deployPage, std::uint32_t& rewardRow)
    {
        __try
        {
            const std::uint8_t* info = *reinterpret_cast<const std::uint8_t* const*>(deployPage + kDeploySelected);
            if (!info || info[0] != 0 || *reinterpret_cast<const std::uint16_t*>(info + kDeployInfoSeed) == 0)
                return false;
            rewardRow = *reinterpret_cast<const std::uint16_t*>(info + kDeployInfoRewardRow);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t FirstFreePreviewRowSEH(std::uint8_t* deployPage, std::uint32_t rewardRow)
    {
        __try
        {
            std::uint32_t row = 0;
            while (row < kDeployRewardRows && std::strcmp(reinterpret_cast<const char*>(deployPage + kDeployRowAmountText + row * 0x80), " ") != 0)
                ++row;
            const std::uint8_t* rewardParam = *reinterpret_cast<const std::uint8_t* const*>(deployPage + kDeployRewardParams)
                                            + kRewardParamFirst + rewardRow * kRewardParamSize;
            const bool zeroGmpRow = rewardParam[kRewardParamMainType] == kMainRewardGmp
                                 && *reinterpret_cast<const std::int32_t*>(rewardParam + kRewardParamGmp) == 0;
            if (zeroGmpRow && row > 0)
                --row;   // the game drew "GMP 0" last only because the main reward type is GMP: our lines take that row
            return row;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kDeployRewardRows;
        }
    }

    bool DrawPreviewLineSEH(std::uint8_t* deployPage, std::uint32_t row, const RewardLine& line)
    {
        __try
        {
            void* ui = *reinterpret_cast<void**>(deployPage + kDeployUi);
            void* style = *reinterpret_cast<void**>(deployPage + kDeployStyle);
            void* rewardIface = *reinterpret_cast<void**>(deployPage + kDeployRewardParams);
            if (!ui || !rewardIface)
                return false;
            void** vtbl = *reinterpret_cast<void***>(ui);
            void** ifaceVtbl = *reinterpret_cast<void***>(rewardIface);
            alignas(16) std::uint8_t fakeParam[kRewardParamSize] = {};
            fakeParam[kFakeParamRate] = line.chance;
            fakeParam[kFakeParamCount] = static_cast<std::uint8_t>((std::min)(line.count, 255));
            const bool staff = line.kind == RewardLine::Kind::Pool && line.poolType >= kPoolRewardFirstStaff && line.poolType <= kPoolRewardLastStaff;
            const bool material = line.kind == RewardLine::Kind::Pool && line.poolType >= kPoolRewardFirstMaterial && line.poolType <= kPoolRewardLastMaterial;
            if (line.kind == RewardLine::Kind::Pool && line.poolType == kPoolRewardGmp)
            {
                const auto draw = reinterpret_cast<DrawGmpRow_t>(ResolveGameAddress(gAddr.CombatDeployCallbackImpl_DrawGmpRewardRow));
                if (!draw)
                    return false;
                draw(deployPage, row, static_cast<std::uint32_t>(line.count), 0);
            }
            else if (staff)
            {
                const auto draw = reinterpret_cast<DrawStaffRow_t>(ResolveGameAddress(gAddr.CombatDeployCallbackImpl_DrawStaffRewardRow));
                if (!draw)
                    return false;
                const std::uint8_t rank = reinterpret_cast<RewardIfaceByte_t>(ifaceVtbl[kRewardIfaceVtblRank])(rewardIface, line.poolType);
                alignas(16) std::uint8_t staffDraw[kStaffDrawSize] = {};
                std::memset(staffDraw + kStaffDrawRanks, rank, kStaffDrawRankCount);
                staffDraw[kStaffDrawKind] = 1;
                draw(deployPage, row, staffDraw, fakeParam);
            }
            else if (material)
            {
                const auto draw = reinterpret_cast<DrawMaterialRow_t>(ResolveGameAddress(gAddr.CombatDeployCallbackImpl_DrawMaterialRewardRow));
                if (!draw)
                    return false;
                const std::uint8_t materialIndex = reinterpret_cast<RewardIfaceByte_t>(ifaceVtbl[kRewardIfaceVtblMaterial])(rewardIface, line.poolType);
                draw(deployPage, row, static_cast<std::int8_t>(materialIndex), fakeParam);
            }
            else
            {
                std::uint64_t iconFile = 0;
                std::uint64_t nameLangId = 0;
                LineLook(line, iconFile, nameLangId);
                void* const* parts = reinterpret_cast<void* const*>(deployPage + (row + kDeployRowParts) * 0x18);
                for (std::uint32_t part = 0; part < 3; ++part)
                    reinterpret_cast<SetNodeVisible_t>(vtbl[kUiVtblSetVisible])(ui, parts[part], part == 2);   // the item layout, like a key item row
                if (iconFile != 0)
                {
                    PrefetchIconSEH(iconFile);
                    void* iconNode = *reinterpret_cast<void**>(deployPage + kDeployRowIcon + row * 8);
                    reinterpret_cast<SetNodeTexture_t>(vtbl[kUiVtblSetNodeTexture])(ui, iconNode, iconFile, kIconSlot);
                    alignas(16) float noShift[4] = {};
                    alignas(16) float iconTone[4] = { 0.5f, 0.0f, 0.0f, 0.0f };   // the game sets both on this node with every picture it puts there
                    reinterpret_cast<SetNodeShaderParam_t>(vtbl[kUiVtblShaderParam])(ui, iconNode, kIconShaderShift, noShift);
                    reinterpret_cast<SetNodeShaderParam_t>(vtbl[kUiVtblShaderParam])(ui, iconNode, kIconShaderTone, iconTone);
                }
                if (nameLangId != 0)
                {
                    void* text = reinterpret_cast<GetTextByLangId_t>(vtbl[kUiVtblLangText])(ui, nameLangId);
                    if (text)
                        reinterpret_cast<WriteText_t>(vtbl[kUiVtblWriteName])(ui, *reinterpret_cast<void**>(deployPage + kDeployRowName + row * 0x18), style, text, 1);
                }
            }
            char* amount = reinterpret_cast<char*>(deployPage + kDeployRowAmountText + row * 0x80);   // the game's own row buffers: the UI reads them later
            std::snprintf(amount, 0x80, "%d", line.count);
            reinterpret_cast<WriteText_t>(vtbl[kUiVtblWriteText])(ui, *reinterpret_cast<void**>(deployPage + kDeployRowAmountNode + row * 8), style, amount, 1);
            char* rate = reinterpret_cast<char*>(deployPage + kDeployRowRateText + row * 0x80);
            std::snprintf(rate, 0x80, "%u%%", static_cast<unsigned>(line.chance));
            reinterpret_cast<WriteText_t>(vtbl[kUiVtblWriteText])(ui, *reinterpret_cast<void**>(deployPage + kDeployRowRateNode + row * 8), style, rate, 1);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hk_MissionRewardsPanel(std::uint8_t* deployPage)
    {
        g_OrigMissionRewardsPanel(deployPage);
        std::uint32_t rewardRow = 0;
        if (!deployPage || MissionCodeGuard::ShouldBypassHooks() || !DrawPreviewVehicleSEH(deployPage, rewardRow))
            return;
        const DispatchRewards* dispatch = RewardsOfRow(rewardRow);
        if (!dispatch)
            return;
        std::uint32_t row = FirstFreePreviewRowSEH(deployPage, rewardRow);
        for (const RewardLine& line : dispatch->lines)
        {
            if (row >= kDeployRewardRows)
                break;   // the panel has five rows; the rest are still paid
            if (!DrawPreviewLineSEH(deployPage, row, line))
                LogWarnOnce("Dispatch reward: the Deployment reward preview could not show a reward line");
            ++row;
        }
    }

    void __fastcall hk_Seize(std::uint8_t* vehicles, std::uint16_t index, std::uint32_t recoveryType, std::uint64_t options)
    {
        const std::uint8_t outer = t_ResourceType;
        t_ResourceType = StockTypeOfVehicle(CommInfoOfSEH(vehicles, kSeizeCommInfos, index));
        g_OrigSeize(vehicles, index, recoveryType, options);
        t_ResourceType = outer;
    }

    bool __fastcall hk_RequestDropVehicle(void* supportRequest, std::uint32_t reliefType, const float* position)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigRequestDropVehicle, supportRequest, reliefType, position);
        const std::uint8_t picked = g_DropType.exchange(0, std::memory_order_relaxed);
        SortieVehicle vehicle{};
        const std::uint8_t type = picked != 0 && CustomVehicles::SortieVehicleOf(picked, vehicle) && vehicle.reliefType == reliefType ? picked : 0;
        const std::uint8_t outerType = t_DropType;
        const std::uint8_t outerPay = t_PayStockType;
        t_DropType = type;
        t_PayStockType = type;   // GMP is paid as for the game vehicle, the vehicle comes out of the mod vehicle's own count
        const bool requested = g_OrigRequestDropVehicle(supportRequest, reliefType, position);
        t_DropType = outerType;
        t_PayStockType = outerPay;
        if (!requested && type != 0)
            g_DropType.store(type, std::memory_order_relaxed);   // refused (too far, a vehicle still out): the pick stays for the next try
        return requested;
    }

    void RememberCarriedVehicle(const std::uint8_t* commInfo, std::uint8_t proxyType)
    {
        std::uint8_t typeCode = 0;
        if (!TypeCodeSEH(commInfo, typeCode) || (typeCode >> 4) == 0)
            return;   // the relief vehicle itself is carried: it stays what it is
        const auto convert = reinterpret_cast<ConvertIntoReliefType_t>(ResolveGameAddress(gAddr.Vehicle_ConvertIntoReliefType));
        const std::uint8_t type = typeCode >> 4;
        SortieVehicle vehicle{};
        const bool registered = convert && CustomVehicles::SortieVehicleOf(type, vehicle)
            && vehicle.reliefType == convert(static_cast<std::uint8_t>((proxyType << 4) | (typeCode & 0xF)));
        SetReserved(registered ? type : 0);   // LoadCarry spawns it as the relief vehicle next mission
    }

    std::uint8_t __fastcall CarriedVehicleType(const std::uint8_t* commInfo)
    {
        const std::uint8_t proxyType = g_GetVehicleType(commInfo);
        if (!MissionCodeGuard::ShouldBypassHooks())
            RememberCarriedVehicle(commInfo, proxyType);
        return proxyType;
    }

    std::uint8_t* CallTarget(std::uint8_t* site)
    {
        std::uint8_t code[kCallSize] = {};
        if (!site || !ReadEntryBytesSEH(site, code, sizeof(code)) || code[0] != 0xE8)   // call rel32
            return nullptr;
        std::int32_t rel = 0;
        std::memcpy(&rel, code + 1, sizeof(rel));
        return site + kCallSize + rel;
    }

    void* MakeThunk(void* site, void* function)
    {
        auto* thunk = static_cast<std::uint8_t*>(HookArena::AllocateNear(reinterpret_cast<std::uintptr_t>(site), kThunkSize));
        if (!thunk)
            return nullptr;
        const std::uint64_t target = reinterpret_cast<std::uint64_t>(function);
        std::memset(thunk, 0xCC, kThunkSize);
        thunk[0] = 0x48;   // mov rax, <our function>
        thunk[1] = 0xB8;
        std::memcpy(thunk + 2, &target, sizeof(target));
        thunk[10] = 0xFF;   // jmp rax
        thunk[11] = 0xE0;
        FlushInstructionCache(GetCurrentProcess(), thunk, kThunkSize);
        return thunk;
    }

    void PatchCarryCall()
    {
        auto* site = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.VehicleSystemImpl_SaveCarry_GetVehicleTypeCall));
        std::uint8_t* getVehicleType = CallTarget(site);
        const void* convert = ResolveGameAddress(gAddr.Vehicle_ConvertIntoReliefType);
        if (!getVehicleType || !convert || CallTarget(site + kCarryReliefCallAt) != convert)
        {
            LogError("VehicleSortieRows: unexpected code where the game saves the carried vehicle on %s, a mod vehicle ridden out of a mission comes back as its game vehicle",
                     GetGameBuildName(gGameBuild));
            return;
        }
        void* thunk = MakeThunk(site, reinterpret_cast<void*>(&CarriedVehicleType));
        if (!thunk)
        {
            LogError("VehicleSortieRows: no memory near the game for the carried vehicle thunk, a mod vehicle ridden out of a mission comes back as its game vehicle");
            return;
        }
        g_GetVehicleType = reinterpret_cast<GetVehicleType_t>(getVehicleType);
        g_CarryCall = {};
        g_CarryCall.target = site;
        g_CarryCall.destination = thunk;
        g_CarryCall.size = kCallSize;
        std::memcpy(g_CarryCall.expected, site, kCallSize);
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            if (InstallNativeCallPatches(&g_CarryCall, 1))
            {
                g_CarryCallPatched = true;
                return;
            }
            if (!NativePatchErrorIsTransient(GetLastError()))
                break;
            Sleep(1);
        }
        LogError("VehicleSortieRows: patching the carried vehicle call failed (%s), a mod vehicle ridden out of a mission comes back as its game vehicle",
                 NativePatchErrorName(GetLastError()));
    }

    void RestoreCarryCall()
    {
        if (!g_CarryCallPatched)
            return;
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            if (RemoveNativeCallPatches(&g_CarryCall, 1) || !NativePatchErrorIsTransient(GetLastError()))
                break;
            Sleep(1);
        }
        g_CarryCallPatched = false;
    }

    void __fastcall hk_InstanceCommand(std::uint8_t* vehicles, std::int32_t* result, lua_State* L, std::uint32_t index, std::uint32_t command)
    {
        static const std::uint32_t getResourceId = FoxHashes::StrCode32("GetResourceId");
        const std::uint8_t outer = t_ResourceType;
        if (command == getResourceId && IsVehicleIndexSEH(vehicles, index))
            t_ResourceType = StockTypeOfVehicle(CommInfoOfSEH(vehicles, kVehiclesCommInfos, index));   // the mission abort recovery credits Mother Base whatever this returns
        g_OrigInstanceCommand(vehicles, result, L, index, command);
        t_ResourceType = outer;
    }

    void __fastcall hk_SetupVehicleDetails(std::uint8_t* details)
    {
        g_OrigSetupVehicleDetails(details);
        SortieVehicle vehicle{};
        if (!details || MissionCodeGuard::ShouldBypassHooks() || !VehicleAtListCursor(vehicle))
            return;
        if (!DrawDetailsSEH(details, vehicle))
            LogWarnOnce("VehicleSortieRows: the vehicle details panel could not be given the mod vehicle's name, the game vehicle's name stays");
    }

    struct HookSite
    {
        const char*                  name;
        std::uintptr_t AddressSet::* address;
        const std::uint8_t*          entry;
        const std::uint8_t*          mask;
        std::size_t                  size;
        void*                        detour;
        void**                       original;
        void*                        target;
    };

    HookSite g_Hooks[] = {
        { "ItemSelectorCallbackImpl::ExhibitSelectingRelief", &AddressSet::ItemSelectorCallbackImpl_ExhibitSelectingRelief,
          kExhibitSelectingEntry, nullptr, sizeof(kExhibitSelectingEntry),
          reinterpret_cast<void*>(&hk_ExhibitSelectingRelief), reinterpret_cast<void**>(&g_OrigExhibitSelectingRelief), nullptr },
        { "MissionPreparationSystemImpl::StartVehicleDisplay", &AddressSet::MissionPreparationSystemImpl_StartVehicleDisplay,
          kVehicleDisplayEntry, nullptr, sizeof(kVehicleDisplayEntry),
          reinterpret_cast<void*>(&hk_StartVehicleDisplay), reinterpret_cast<void**>(&g_OrigStartVehicleDisplay), nullptr },
        { "MissionPreparationSystemImpl::ChangeVehicleDisplay", &AddressSet::MissionPreparationSystemImpl_ChangeVehicleDisplay,
          kVehicleDisplayEntry, nullptr, sizeof(kVehicleDisplayEntry),
          reinterpret_cast<void*>(&hk_ChangeVehicleDisplay), reinterpret_cast<void**>(&g_OrigChangeVehicleDisplay), nullptr },
        { "MissionPreparationCallbackImpl::ReserveSelectionRelief", &AddressSet::MissionPreparationCallbackImpl_ReserveSelectionRelief,
          kReserveEntry, kReserveEntryMask, sizeof(kReserveEntry),
          reinterpret_cast<void*>(&hk_ReserveSelectionRelief), reinterpret_cast<void**>(&g_OrigReserveSelectionRelief), nullptr },
        { "ResourceControllerImpl::PayResourceCost", &AddressSet::ResourceControllerImpl_PayResourceCost,
          kPayEntry, nullptr, sizeof(kPayEntry),
          reinterpret_cast<void*>(&hk_PayResourceCost), reinterpret_cast<void**>(&g_OrigPayResourceCost), nullptr },
        { "MissionPreparationCallbackImpl::SetTopMenuVehicleName", &AddressSet::MissionPreparationCallbackImpl_SetTopMenuVehicleName,
          kTopMenuNameEntry, nullptr, sizeof(kTopMenuNameEntry),
          reinterpret_cast<void*>(&hk_SetTopMenuVehicleName), reinterpret_cast<void**>(&g_OrigSetTopMenuVehicleName), nullptr },
        { "vehicle SystemImpl::ExhibitRelief", &AddressSet::VehicleSystemImpl_ExhibitRelief,
          kExhibitEntry, kExhibitEntryMask, sizeof(kExhibitEntry),
          reinterpret_cast<void*>(&hk_ExhibitRelief), reinterpret_cast<void**>(&g_OrigExhibitRelief), nullptr },
        { "vehicle SystemImpl::SpawnRelief", &AddressSet::VehicleSystemImpl_SpawnRelief,
          kSpawnEntry, kCookieEntryMask, sizeof(kSpawnEntry),
          reinterpret_cast<void*>(&hk_SpawnRelief), reinterpret_cast<void**>(&g_OrigSpawnRelief), nullptr },
        { "vehicle SystemImpl::DeliverRelief", &AddressSet::VehicleSystemImpl_DeliverRelief,
          kDeliverEntry, kCookieEntryMask, sizeof(kDeliverEntry),
          reinterpret_cast<void*>(&hk_DeliverRelief), reinterpret_cast<void**>(&g_OrigDeliverRelief), nullptr },
        { "vehicle GameObjectImpl::DispatchLuaCommandToInstance", &AddressSet::VehicleGameObjectImpl_DispatchLuaCommandToInstance,
          kInstanceCommandEntry, kCookieEntryMask, sizeof(kInstanceCommandEntry),
          reinterpret_cast<void*>(&hk_InstanceCommand), reinterpret_cast<void**>(&g_OrigInstanceCommand), nullptr },
        { "EquipDetailsCallbackImpl::SetupVehicleDetails", &AddressSet::EquipDetailsCallbackImpl_SetupVehicleDetails,
          kDetailsEntry, kDetailsEntryMask, sizeof(kDetailsEntry),
          reinterpret_cast<void*>(&hk_SetupVehicleDetails), reinterpret_cast<void**>(&g_OrigSetupVehicleDetails), nullptr },
        { "vehicle ConvertIntoResourceId", &AddressSet::Vehicle_ConvertIntoResourceId,
          kConvertResourceEntry, kConvertResourceEntryMask, sizeof(kConvertResourceEntry),
          reinterpret_cast<void*>(&hk_ConvertIntoResourceId), reinterpret_cast<void**>(&g_OrigConvertIntoResourceId), nullptr },
        { "vehicle InstanceImpl::ReflectLifeState", &AddressSet::VehicleInstanceImpl_ReflectLifeState,
          kReflectLifeEntry, kReflectLifeEntryMask, sizeof(kReflectLifeEntry),
          reinterpret_cast<void*>(&hk_ReflectLifeState), reinterpret_cast<void**>(&g_OrigReflectLifeState), nullptr },
        { "vehicle ManeuverControllerImpl::FultonRecovery", &AddressSet::VehicleManeuverControllerImpl_FultonRecovery,
          kFultonRecoveryEntry, nullptr, sizeof(kFultonRecoveryEntry),
          reinterpret_cast<void*>(&hk_FultonRecovery), reinterpret_cast<void**>(&g_OrigFultonRecovery), nullptr },
        { "ResourceControllerImpl::CanPayResourceCost", &AddressSet::ResourceControllerImpl_CanPayResourceCost,
          kCanPayEntry, nullptr, sizeof(kCanPayEntry),
          reinterpret_cast<void*>(&hk_CanPayResourceCost), reinterpret_cast<void**>(&g_OrigCanPayResourceCost), nullptr },
        { "SupportRequestSystemImpl::RequestDropVehicle", &AddressSet::SupportRequestSystemImpl_RequestDropVehicle,
          kRequestDropEntry, kRequestDropEntryMask, sizeof(kRequestDropEntry),
          reinterpret_cast<void*>(&hk_RequestDropVehicle), reinterpret_cast<void**>(&g_OrigRequestDropVehicle), nullptr },
        { "vehicle GameObjectImpl::Seize", &AddressSet::VehicleGameObjectImpl_Seize,
          kSeizeEntry, kSeizeEntryMask, sizeof(kSeizeEntry),
          reinterpret_cast<void*>(&hk_Seize), reinterpret_cast<void**>(&g_OrigSeize), nullptr },
        { "ResourceControllerImpl::AddResource", &AddressSet::ResourceControllerImpl_AddResource,
          kAddResourceEntry, nullptr, sizeof(kAddResourceEntry),
          reinterpret_cast<void*>(&hk_AddResource), reinterpret_cast<void**>(&g_OrigAddResource), nullptr },
        { "MotherBaseManagementSystem::AddTempResource", &AddressSet::MotherBaseManagementSystem_AddTempResource,
          kAddTempResourceEntry, nullptr, sizeof(kAddTempResourceEntry),
          reinterpret_cast<void*>(&hk_AddTempResource), reinterpret_cast<void**>(&g_OrigAddTempResource), nullptr },
        { "ResourceControllerImpl::AddResourcesFromTempResourceBuffer", &AddressSet::ResourceControllerImpl_AddResourcesFromTempResourceBuffer,
          kAddFromTempEntry, nullptr, sizeof(kAddFromTempEntry),
          reinterpret_cast<void*>(&hk_AddResourcesFromTempResourceBuffer), reinterpret_cast<void**>(&g_OrigAddResourcesFromTemp), nullptr },
        { "MotherBaseMapCommonDataImpl::GetUniqueInformationIconLangId", &AddressSet::MotherBaseMapCommonDataImpl_GetUniqueInformationIconLangId,
          kUniqueIconLangEntry, nullptr, sizeof(kUniqueIconLangEntry),
          reinterpret_cast<void*>(&hk_GetUniqueIconLangId), reinterpret_cast<void**>(&g_OrigGetUniqueIconLangId), nullptr },
        { "ResourceManagementCallbackImpl::RefreshPrefabList", &AddressSet::ResourceManagementCallbackImpl_RefreshPrefabList,
          kResourceListEntry, nullptr, sizeof(kResourceListEntry),
          reinterpret_cast<void*>(&hk_RefreshResourceList), reinterpret_cast<void**>(&g_OrigRefreshResourceList), nullptr },
        { "ResourceListRecordCallFunc::Start", &AddressSet::ResourceListRecordCallFunc_Start,
          kResourceRecordEntry, nullptr, sizeof(kResourceRecordEntry),
          reinterpret_cast<void*>(&hk_ResourceRecordStart), reinterpret_cast<void**>(&g_OrigResourceRecordStart), nullptr },
        { "ResourceManagementCallbackImpl info text", &AddressSet::ResourceManagementCallbackImpl_RefreshInfoText,
          kResourceInfoEntry, nullptr, sizeof(kResourceInfoEntry),
          reinterpret_cast<void*>(&hk_ResourceInfoText), reinterpret_cast<void**>(&g_OrigResourceInfoText), nullptr },
        { "ResourceController::GetResourceUsableCount", &AddressSet::ResourceController_GetResourceUsableCount,
          kUsableCountEntry, nullptr, sizeof(kUsableCountEntry),
          reinterpret_cast<void*>(&hk_ResourceUsableCount), reinterpret_cast<void**>(&g_OrigResourceUsableCount), nullptr },
        { "PoolRewardControllerImpl::AddPoolRewards", &AddressSet::PoolRewardControllerImpl_AddPoolRewards,
          kAddPoolRewardsEntry, nullptr, sizeof(kAddPoolRewardsEntry),
          reinterpret_cast<void*>(&hk_AddPoolRewards), reinterpret_cast<void**>(&g_OrigAddPoolRewards), nullptr },
        { "PoolRewardControllerImpl::AcceptPoolRewardOneCore", &AddressSet::PoolRewardControllerImpl_AcceptPoolRewardOneCore,
          kAcceptOneEntry, kAcceptOneEntryMask, sizeof(kAcceptOneEntry),
          reinterpret_cast<void*>(&hk_AcceptPoolRewardOne), reinterpret_cast<void**>(&g_OrigAcceptPoolRewardOne), nullptr },
        { "ResourceManagementCallbackImpl::RefreshImageRecord", &AddressSet::ResourceManagementCallbackImpl_RefreshImageRecord,
          kImageRecordEntry, nullptr, sizeof(kImageRecordEntry),
          reinterpret_cast<void*>(&hk_RefreshImageRecord), reinterpret_cast<void**>(&g_OrigRefreshImageRecord), nullptr },
        { "Resources sale popup item", &AddressSet::ResourceSalePopup_RefreshItem,
          kSalePopupEntry, kSalePopupEntryMask, sizeof(kSalePopupEntry),
          reinterpret_cast<void*>(&hk_SalePopupRefresh), reinterpret_cast<void**>(&g_OrigSalePopupRefresh), nullptr },
        { "CombatDeployCallbackImpl::RefreshMissionRewards", &AddressSet::CombatDeployCallbackImpl_RefreshMissionRewards,
          kMissionRewardsEntry, kMissionRewardsEntryMask, sizeof(kMissionRewardsEntry),
          reinterpret_cast<void*>(&hk_MissionRewardsPanel), reinterpret_cast<void**>(&g_OrigMissionRewardsPanel), nullptr },
        { "Rewards list row", &AddressSet::RewardListRecordCallFunc_Refresh,
          kRewardListRowEntry, nullptr, sizeof(kRewardListRowEntry),
          reinterpret_cast<void*>(&hk_RewardListRow), reinterpret_cast<void**>(&g_OrigRewardListRow), nullptr },
        { "ResourceControllerImpl::SaleResource", &AddressSet::ResourceControllerImpl_SaleResource,
          kSaleEntry, nullptr, sizeof(kSaleEntry),
          reinterpret_cast<void*>(&hk_SaleResource), reinterpret_cast<void**>(&g_OrigSaleResource), nullptr },
        { "ResourceController server count", &AddressSet::ResourceController_GetResourceServerCount,
          kServerCountEntry, nullptr, sizeof(kServerCountEntry),
          reinterpret_cast<void*>(&hk_ResourceServerCount), reinterpret_cast<void**>(&g_OrigResourceServerCount), nullptr },
    };

    bool InstallHook(HookSite& site)
    {
        if (site.target)
            return true;
        auto* function = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.*site.address));
        if (!function)
        {
            LogError("VehicleSortieRows: no address for %s on %s, mod vehicles get no Sortie Prep row", site.name, GetGameBuildName(gGameBuild));
            return false;
        }
        const HookEntry state = site.mask ? CheckHookEntryMasked(function, site.entry, site.mask, site.size)
                                          : CheckHookEntry(function, site.entry, site.size);
        if (state == HookEntry::Unknown)
        {
            char found[160] = {};
            DescribeFunctionEntry(function, found, sizeof(found));
            LogError("VehicleSortieRows: %s does not match %s (%s), mod vehicles get no Sortie Prep row", site.name, GetGameBuildName(gGameBuild), found);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(site.name, function);
        if (!CreateAndEnableHook(function, site.detour, site.original))
        {
            LogError("VehicleSortieRows: %s hook failed, mod vehicles get no Sortie Prep row", site.name);
            return false;
        }
        site.target = function;
        return true;
    }
}

namespace VehicleSortieRows
{
    bool Install()
    {
        if (g_Installed)
            return true;
        for (HookSite& site : g_Hooks)
        {
            if (!InstallHook(site))
            {
                Uninstall();
                return false;
            }
        }
        g_HoldsResourceStep = Acquire_ResourceManagementCallbackImpl_UpdateState_Hook();
        if (!g_HoldsResourceStep)
        {
            LogError("VehicleSortieRows: ResourceManagementCallbackImpl::UpdateState hook failed, mod vehicles get no Sortie Prep row");
            Uninstall();
            return false;
        }
        PatchCarryCall();
        g_Installed = true;
        return true;
    }

    void Uninstall()
    {
        g_Installed = false;
        g_ListSelector = nullptr;
        RestoreCarryCall();
        if (g_HoldsResourceStep)
            Release_ResourceManagementCallbackImpl_UpdateState_Hook();
        g_HoldsResourceStep = false;
        for (auto it = std::rbegin(g_Hooks); it != std::rend(g_Hooks); ++it)
        {
            if (!it->target)
                continue;
            DisableAndRemoveHook(it->target);
            it->target = nullptr;
        }
    }

    ResourceStepScope BeginResourceStep(void* resourcePageState)
    {
        auto* resourcePage = resourcePageState ? static_cast<std::uint8_t*>(resourcePageState) - kPageStateAt : nullptr;
        const ResourceStepScope outer{ t_SaleRowId, t_SaleRowType, resourcePage == g_ResourcePage ? PageStepSEH(resourcePage) : std::uint8_t{ 0 } };
        std::uint32_t row = 0;
        SortieVehicle vehicle{};
        if (g_Installed && resourcePage && resourcePage == g_ResourcePage && !MissionCodeGuard::ShouldBypassHooks()
            && ResourceCursorRowSEH(resourcePage, row) && CustomVehicles::SortieVehicleOf(ResourceTypeAtRow(resourcePage, row), vehicle))
        {
            t_SaleRowId = vehicle.resourceId;   // the page counts and sells the borrowed game id: here that means the mod vehicle's own count
            t_SaleRowType = vehicle.type;
        }
        return outer;
    }

    void EndResourceStep(const ResourceStepScope& outer)
    {
        SortieVehicle vehicle{};
        std::uint8_t* resourcePage = g_ResourcePage;
        if (t_SaleRowType != 0 && resourcePage && outer.pageState != kStepSalePopup
            && PageStepSEH(resourcePage) == kStepSalePopup && CustomVehicles::SortieVehicleOf(t_SaleRowType, vehicle)
            && !FitSalePopupSEH(resourcePage, vehicle, StockOf(vehicle.type)))
            LogWarnOnce("VehicleSortieRows: the sale popup could not be given the mod vehicle's count and name");
        t_SaleRowId = outer.saleRowId;
        t_SaleRowType = outer.saleRowType;
    }

    void AfterListBuilt(void* itemSelector)
    {
        auto* selector = static_cast<std::uint8_t*>(itemSelector);
        if (!selector)
            return;
        if (selector == g_ListSelector)
            g_ListSelector = nullptr;
        const VehicleList list = g_Installed && !MissionCodeGuard::ShouldBypassHooks() ? OpenVehicleListSEH(selector) : VehicleList::None;
        if (list == VehicleList::None)
            return;

        SortieVehicle registered[kMostSortieVehicles] = {};
        const std::size_t registeredCount = CustomVehicles::SortieVehicles(registered, kMostSortieVehicles);
        SortieVehicle vehicles[kMostSortieVehicles] = {};
        std::size_t vehicleCount = 0;
        for (std::size_t i = 0; i < registeredCount; ++i)
        {
            if (StockOf(registered[i].type) > 0)   // like the game's own: only what Mother Base has is listed
                vehicles[vehicleCount++] = registered[i];
        }
        if (vehicleCount == 0)
            return;
        const bool supplyDrop = list == VehicleList::SupplyDrop;
        const std::uint8_t selectedType = supplyDrop ? 0 : SelectedTypeFor(SelectedResourceIdSEH(PointerAtSEH(selector, kSelectorPrep)));   // the drop list marks no row in use
        std::uint32_t firstRow = 0;
        if (!AppendRowsSEH(selector, vehicles, vehicleCount, selectedType, supplyDrop, firstRow))
        {
            LogWarnOnce("VehicleSortieRows: the Sortie Prep vehicle list could not take the mod vehicles' rows");
            return;
        }
        for (std::size_t i = 0; i < vehicleCount; ++i)
        {
            g_ListTypes[i] = vehicles[i].type;
            if (vehicles[i].iconFile != 0)
                PrefetchIconSEH(vehicles[i].iconFile);
        }
        g_ListFirstRow = firstRow;
        g_ListCount = static_cast<std::uint32_t>(vehicleCount);
        g_ListSelector = selector;
    }

    void AfterRowDrawn(void* record)
    {
        auto* drawn = static_cast<std::uint8_t*>(record);
        const std::uint8_t* itemSelector = g_ListSelector;
        const std::uint8_t* rowIds = nullptr;
        std::uint32_t row = 0;
        if (!drawn || !itemSelector || !RowOfRecordSEH(drawn, rowIds, row) || rowIds != itemSelector + kSelectorRowIds)
            return;
        SortieVehicle vehicle{};
        if (!CustomVehicles::SortieVehicleOf(TypeAtRow(itemSelector, row), vehicle))
            return;
        if (!DrawRowSEH(drawn, vehicle, RecordCountText(drawn, StockOf(vehicle.type))))
            LogWarnOnce("VehicleSortieRows: a mod vehicle's Sortie Prep row could not be given its name, the game vehicle's name stays");
    }

    void BeforeRowDecided(void* itemSelector)
    {
        const auto* selector = static_cast<const std::uint8_t*>(itemSelector);
        std::uint32_t row = 0;
        if (!selector || selector != g_ListSelector || !CursorRowSEH(selector, row))
            return;
        const VehicleList list = OpenVehicleListSEH(selector);
        if (list == VehicleList::None)
            return;
        const std::uint8_t type = TypeAtRow(selector, row);
        if (list == VehicleList::SupplyDrop)
            g_DropType.store(type, std::memory_order_relaxed);   // the drop list only hands the map a relief type
        else
            SetSelected(type);
    }

    bool SetDispatchRewards(std::uint32_t rewardRow, const char* missionKey, const std::vector<DispatchRewardLine>& lines)
    {
        if (rewardRow == 0 || rewardRow >= kMostRewardRows)
            return false;
        g_DispatchRewards[rewardRow].missionKey = missionKey ? missionKey : "";
        g_DispatchRewards[rewardRow].lines.assign(lines.begin(), lines.begin() + (std::min)(lines.size(), kMostRewardLines));
        return true;
    }

    void ForgetList()
    {
        g_ListSelector = nullptr;
        g_ResourcePage = nullptr;
        for (auto& pending : g_PendingStock)
            pending.store(0, std::memory_order_relaxed);   // the mission ended without the game adding its temp resources: neither do we
    }
}
