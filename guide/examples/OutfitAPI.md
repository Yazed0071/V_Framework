# V_FrameWork Outfit API — Modder Reference

The outfit API has two layers:

**High-level (recommended for mods)** — `V_TppPlayer` wrapper module:

| Function | Purpose |
|---|---|
| `V_TppPlayer.AddOutfit(opts)` | Register a custom outfit. Auto-allocates and persists ids. Optionally creates an R&D table entry via `opts.develop`. Returns `partsType, developId, flowIndex` on success, `false` on failure. |
| `V_TppPlayer.SetOutfitVariant(developId, variantIndex)` | Programmatically switch active variant. |
| `V_TppPlayer.GetOutfitInfo(developId)` | Returns a Lua table with the allocated values; `nil` if not registered. |

**Low-level (raw bridge)** — `V_FrameWork` table:

| Function | Purpose |
|---|---|
| `V_FrameWork.RegisterOutfit(def)` | Same as `AddOutfit` but uses `key` instead of `name`, no R&D table integration, no field validation. Use only when the wrapper doesn't fit. |
| `V_FrameWork.SetOutfitVariant(developId, variantIndex)` | Same. |
| `V_FrameWork.GetOutfitInfo(developId)` | Same. |

## `V_TppPlayer.AddOutfit(opts)`

The recommended modder API. `opts` is a single Lua table.

`def` is a single Lua table.

### Required fields

| Field | Type | Meaning |
|---|---|---|
| `name` | string | **Required for auto-allocation.** Stable persistence key used in `V_FrameWork_State.lua`. Convention: `"ModName:OutfitName"`. Same name returns the same `developId` / `flowIndex` across sessions. |
| `playerType` | string \| int | `"Snake"` / `"DDMale"` / `"DDFemale"` / `"Avatar"`, or numeric 0..3. |
| `partsPath` | string | Body `.parts` asset path. Hashed to FoxPath code64ext. |
| `fpkPath` | string | Body `.fpk` asset path. Hashed to FoxPath code64ext. |

### Identity (auto-allocated; override only when needed)

| Field | Type | Default | Meaning |
|---|---|---|---|
| `developId` | int | auto | Auto-allocated from the persistent pool keyed by `name`. Pass an explicit value only for migration or fixed-id mods. |
| `flowIndex` | int | auto | Auto-allocated from the persistent pool keyed by `name`. |

### Sub-asset paths

Each accepts: a path string (custom), `true` (vanilla), `false` (disabled), or `nil` (per-field default).

| Field | Default | Notes |
|---|---|---|
| `camoFpk` | disabled | Custom camo pattern .fpk |
| `camoFv2` | vanilla | Custom camo FV2 |
| `faceFpk` | vanilla | Custom head/face .fpk |
| `skinFv2` | vanilla | Custom skin tone FV2 |
| `diamondFpk` | disabled | Diamond filter .fpk |
| `diamondFv2` | vanilla | Diamond filter FV2 |
| `enableArm` | `true` | `false` suppresses Snake's bionic prosthetic arm (set for non-Snake characters) |
| `armType` | `nil` | **(Snake/Avatar only)** Explicit `playerArmType` pin (0..0xFE). Default `nil` means "auto-force armType=1 if `enableArm=true` and incoming armType=0" (legacy behavior — meant for SSD-port bodies that need a separate bionic-arm overlay). Set to `0` explicitly when your body parts file already has integrated hand geometry (e.g. vanilla `sna4_main0_def_v00.parts`) so the framework does NOT load a bionic arm fpk on top. Set to `1..N` to pin a specific arm variant. |
| `armFpk` | vanilla | Direct override of the arm fpk path written to `BlockShell+0x08` post-orig. Pass a path string to ship your own Snake-skeleton-bound arm fpk; pass `false` to force the slot to null (overrides whatever orig populated). Default `true`/vanilla means "let orig's `LoadPlayerBionicArmFpk(playerType, partsType, armType)` populate the slot" — works for vanilla bionic arms when `armType > 0`. |

### Head options

| Field | Type | Default | Meaning |
|---|---|---|---|
| `headOptions` | array | empty | Up to 8 head entries. The HEAD OPTION submenu auto-enables when this array is non-empty. Each entry can be a vanilla alias string (`"NONE"`, `"BANDANA"`, `"INFINITE BANDANA"`, `"BALACLAVA"`, `"SP-HEADGEAR"`, `"HP-HEADGEAR"` — case- and separator-insensitive), a vanilla equipId number (`0x400` / `0x20E..0x212`), a custom-head name registered via `V_FrameWork.RegisterHeadOption` / `V_TppPlayer.AddHeadOption`, or the equipId returned by that call. |

> Custom heads (registered via `V_TppPlayer.AddHeadOption`) are
> develop-gated — they only appear in the submenu after the player
> researches them in MotherBase R&D. Set `flow.initialAvailable = 1`
> on the develop block to start them pre-researched. See
> `OutfitWithCustomHead.lua` for the full pattern.

### Variants & display names

| Field | Type | Default | Meaning |
|---|---|---|---|
| `displayName` | string | nil | LangId for the cycle-button label of variant 0 (the BASE appearance). Pass an `<Entry LangId=...>` value from a vanilla or modded LangId XML; the framework hashes it via StrCode64 and overrides the UNIFORMS-row label that the orig translator returns blank for our custom partsType range. Without it, the base cycle button shows whatever orig falls back to (typically blank, or one of vanilla's hardcoded STANDARD/SCARF/NAKED labels). |
| `variants` | table[] | empty | Up to 8 variant tables. Variant 0 is implicit (base outfit's paths + the top-level `displayName`); explicit entries fill variants 1..N with their own `displayName`. |

Each variant table accepts: `partsPath`, `fpkPath`, `camoFpk`, `camoFv2`, `diamondFpk`, `displayName`. Any unset field inherits from the base outfit. The `displayName` field is the LangId string (same form as the top-level field) — the framework hashes it per-variant.

### Surface-bonus camo pin

| Field | Type | Default | Meaning |
|---|---|---|---|
| `camoBonusType` | int | nil (no pin) | INHERIT a vanilla camo's bonus profile. `PlayerCamoType` value 0..116. Pass `PlayerCamoType.BATTLEDRESS` (the vanilla MGSV lua enum) or a raw 0..116 int. The framework hooks `CamouflageControllerImpl::ExecSuitCorrect` so the engine's `GetCamoufValue` indexes the chosen row of the 117×82 table — same mechanism vanilla uses to pin BATTLEDRESS / FOXTROT / etc. to specific suits. Without this, custom outfits inherit whatever camo the player last picked via the iDroid camo menu. |
| `camoBonusValues` | table | nil (no unique row) | UNIQUE per-outfit bonus row. Sparse table keyed by material name (e.g. `MTR_LEAF = 50`) or 1-based numeric index 1..82. Anything not listed defaults to 0. The framework allocates a virtual `PlayerCamoType` id (range 200..254 — pool of 55 slots) and routes the engine's bonus-table read through a `GetCamoufValue` hook to this inline row. Vanilla 117 rows are never touched. If both `camoBonusType` and `camoBonusValues` are passed, **values wins** (more specific intent). |

### Snake / Avatar (PT=0/3) recipes

The Snake and Avatar pipelines differ from DDMale/DDFemale: orig only writes ONE face slot (`BlockShell+0x10` via `LoadPlayerSnakeFaceFpk`) and ONE arm slot (`BlockShell+0x08` via `LoadPlayerBionicArmFpk`). DDMale/DDFemale go through the four-slot Soldier2FaceSystem path. Pick the recipe that matches your body parts file.

**Recipe A — body parts file already has integrated head + hands** (e.g. vanilla NORMAL Snake `sna4_main0_def_v00.parts`, or any `.parts` file the modder baked everything into):

```lua
V_TppPlayer.AddOutfit{
    name       = "MyMod:Snake_Recolor",
    playerType = "Snake",  -- or "Avatar"
    partsPath  = "/Assets/tpp/parts/chara/sna/sna4_main0_def_v00.parts",
    fpkPath    = "/Assets/mod/recolor/recolor_overlay.fpk",

    -- CRITICAL: opt out of the bionic-arm overlay so it doesn't render
    -- on top of the integrated hands.
    armType    = 0,

    -- CRITICAL: suppress the vanilla face fpk overlay so it doesn't
    -- render on top of the integrated head. The framework hooks
    -- LoadPlayerSnakeFaceFpk and writes 0 to BlockShell+0x10 when
    -- enableHead=false on a registered outfit.
    enableHead = false,

    develop = { const = { ... }, flow = { ... } },
}
```

**Recipe B — body parts file has NO integrated head/hands** (e.g. SSD-port FROGS-style, where the `.parts` file expects separate face/arm fpk overlays):

```lua
V_TppPlayer.AddOutfit{
    name       = "MyMod:Snake_FROGS",
    playerType = "Snake",
    partsPath  = "/Assets/mod/chara/sna/frogs.parts",
    fpkPath    = "/Assets/mod/pack/chara/sna/frogs.fpk",

    armType    = 1,                  -- 1 = vanilla bionic; or use armFpk for a custom arm
    enableHead = true,
    defaultSoldierFaceId = 1,        -- pick a populated FaceUnit row

    develop = { const = { ... }, flow = { ... } },
}
```

**Recipe C — modder ships a custom Snake-skeleton-bound arm fpk**:

```lua
V_TppPlayer.AddOutfit{
    name       = "MyMod:Snake_CustomArm",
    playerType = "Snake",
    partsPath  = "/Assets/mod/chara/sna/custom.parts",
    fpkPath    = "/Assets/mod/pack/chara/sna/custom.fpk",

    armType    = 1,  -- non-zero so orig dispatches the arm-load call
    armFpk     = "/Assets/mod/chara/sna/custom_arm.fpk",  -- direct override of BlockShell+0x08

    enableHead = true,
    develop = { const = { ... }, flow = { ... } },
}
```

`armFpk` is written to BlockShell+0x08 post-orig, so it overrides whatever vanilla `LoadPlayerBionicArmFpk` resolved for `(playerType, partsType, armType)`. Pass `false` to force the slot to null even if orig wrote something.

### R&D table entry (V_TppPlayer.AddOutfit only)

Optional. When present, the wrapper inserts the auto-allocated `developId` into `develop.const.p00` and the auto-allocated `flowIndex` into `develop.const.p50`, then forwards the merged table to `V_FrameWork.AddToEquipDevelopTable`. Result: a fully wired R&D entry that matches the outfit's ids without manual coordination.

| Field | Type | Meaning |
|---|---|---|
| `develop.const` | table | R&D constants (lang strings, icon, grade, type ids). See APPENDIX B in the main API reference for the full per-field list. |
| `develop.flow` | table | R&D flow params (`developGmpCost`, `developTimeMinute`, `resourceType1`/`resourceType1Count`, `initialAvailable`, etc.). |

If `develop` is omitted, the outfit registers but no R&D entry is created — the outfit is still selectable in the UNIFORMS panel via the framework's auto-injected row.

### Return value

On success: three numbers — `partsType` (0x40..0x7F), `developId`, `flowIndex`.
On failure: `false`. Common failure causes (always logged to `V_FrameWork_log.txt`):

- Missing required field
- No `name` AND no explicit `developId` / `flowIndex` (can't auto-allocate without a persistence key)
- `developId` or `flowIndex` already registered (only happens with explicit pinning that collides)
- Custom partsType / selectorCode pool exhausted (64 / 127 slots per session)
- Registry full (128 outfits)

## `V_FrameWork.SetOutfitVariant(developId, variantIndex)`

Sets the active variant for an already-registered outfit. The next time the runtime parts pipeline loads this outfit's assets, the named variant's paths are used.

`variantIndex` is clamped to the outfit's `variantCount` (0 always returns the base appearance).

Returns `true` on success, `false` if `developId` is unknown.

## `V_FrameWork.GetOutfitInfo(developId)`

Returns a table with the allocated values:

```lua
{
    partsType            = <int>,    -- 0x40..0x7F
    selectorCode         = <int>,    -- 0x80..0xFE
    flowIndex            = <int>,
    playerType           = <int>,    -- 0..3
    variantCount         = <int>,
    activeVariant        = <int>,
    supportsHeadOptions  = <bool>,
}
```

Returns `nil` if no outfit is registered for `developId`.

## Reserved id ranges (auto-allocated, no manual coordination needed)

| Id | Vanilla range | Auto-pool starts at | Lifetime |
|---|---|---|---|
| `developId` | 1..50000 | `0x1000` (4096) | persisted in `V_FrameWork_State.lua` |
| `flowIndex` | 1..921 | 922 | persisted in `V_FrameWork_State.lua` |
| `partsType` | 0x00..0x3F | 0x40 | session-only |
| `selectorCode` | 0x00..0x7F | 0x80 | session-only |

Two mods registering different `key` values always receive different `developId` and `flowIndex`. Same `key` across runs returns the same ids — same mechanism that weapons use, so weapon mods and outfit mods can never accidentally collide.

## Examples

See:

- `OutfitSimple.lua` — minimal one-asset registration
- `OutfitWithCamoAndFv2.lua` — custom camo + FV2 + diamond
- `OutfitWithHeadOptions.lua` — HEAD OPTION submenu enabled
- `OutfitWithVariants.lua` — three variants from one definition
- `OutfitFullExample.lua` — every feature combined
