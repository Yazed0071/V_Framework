#pragma once

#include <cstdint>

bool Install_MissionPrepPlayerPartsRequest_Hook();
bool Uninstall_MissionPrepPlayerPartsRequest_Hook();

// Trigger a silent suit-commit cycle by calling
// `tpp::gm::player::impl::Player2UtilityImpl::RequestToChangePlayerPartsInMissionPreparationMode`
// at `0x1462b6590` — the 3-arg wrapper that IGNORES its `this` argument and
// calls `GetPlayer2System()` internally to resolve the correct receiver. Safe
// to invoke from any context where the 4-arg AttackActionImpl entry point
// (`0x14973DA60`) would crash due to wrong `this`.
//
// The `missionPrepCallbackSelf` and `idx` params are accepted for signature
// stability but NOT used — both can be nullptr/0.
//
// `blob` must be the 0xE8-byte commit blob (partsType, selector, variantIndex,
// headOption at +0x00..+0x03, flags at +0xBC, playerType at +0xC0).
//
// Returns true if the wrapper was resolved and fired successfully (including
// SEH-protected exception handling).
//
// Used by SuitVariantHook's silent-commit prime path to populate
// sub-controller HasHeadOptions state on initial sortie entry with a
// persisted custom suit.
bool TriggerSilentSuitCommit(
    void* missionPrepCallbackSelf,
    int idx,
    const void* blob,
    std::uint8_t apply);

// Convenience wrapper: reads live Quark state, builds a well-formed commit
// blob matching the currently-equipped custom suit, and fires
// TriggerSilentSuitCommit with it. Returns false if:
//   - wrapper address not bound (build mismatch), or
//   - no custom suit currently equipped (no prime needed), or
//   - live state read failed.
//
// Primary use case: on the first GetSelectionNum call in a sortie-prep
// session with a persisted custom enableHead=true suit, prime the game's
// internal HEAD OPTION state so entries populate without requiring the
// user to manually cycle through another suit first.
bool TriggerSilentSuitCommitFromLiveState();