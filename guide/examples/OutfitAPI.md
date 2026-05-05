# V_FrameWork Outfit API — Modder Reference

The outfit API has two layers:

**High-level (recommended for mods)** — `V_TppPlayer` wrapper module:

| Function | Purpose |
|---|---|
| `V_TppPlayer.AddOutfit(opts)` | Register a custom outfit. Auto-allocates and persists ids. Optionally creates an R&D table entry via `opts.develop`. Returns `partsType, developId, flowIndex` on success, `false` on failure. |
| `V_TppPlayer.SetOutfitVariant(developId, variantIndex)` | Programmatically switch active variant. |
| `V_TppPlayer.GetOutfitInfo(developId)` | Returns a Lua table with the allocated values; `nil` if not registered. |

## `V_TppPlayer.AddOutfit(opts)`

The recommended modder API. `opts` is a single Lua table.

### Required outfit-level fields

Only `name` is required at the outfit level. `name` keys the entry in
`V_FrameWork_State.lua`; `developId` and `flowIndex` are auto-allocated under
this key and persist across sessions. They cannot be passed manually.

| Field | Type | Meaning |
|---|---|---|
| `name` | string | **Required.** Stable persistence key. Convention: `"ModName:OutfitName"`. |
| at least one of `snake` / `ddMale` / `ddFemale` / `avatar` | table | **Required.** Per-playerType branch table — see below. |

### Per-playerType branches

Each branch is a sub-table on `opts` keyed by playerType name. **Only `name`
and `develop` are shared across branches** — every other field lives inside
each branch independently.

| Branch key | playerType |
|---|---|
| `snake` | Snake (0) |
| `ddMale` | DDMale (1) |
| `ddFemale` | DDFemale (2) |
| `avatar` | Avatar (3) |

**Snake↔Avatar bridge:** if you supply `snake` but omit `avatar` (or vice
versa), the outfit auto-mirrors to the missing side using the same data.
DDMale and DDFemale do NOT bridge.

### Per-branch fields

#### Required

| Field | Type | Meaning |
|---|---|---|
| `partsPath` | string | Body `.parts` asset path (hashed to FoxPath code64ext). |
| `fpkPath` | string | Body `.fpk` asset path. |

#### Sub-asset overrides (optional)

Each accepts: a path string (custom asset), `true` (use vanilla), `false`
(disable load), or `nil` (per-field default).

| Field | Default | Meaning |
|---|---|---|
| `camoFpk` | disabled | Custom camo pattern .fpk |
| `camoFv2` | vanilla | Custom camo .fv2 |
| `faceFpk` | vanilla | Custom face .fpk. **Bool form**: `true` = load engine's vanilla face; `false` = load no face (use when the body bakes its own head). String form lets you ship your own face FPK. |
| `skinFv2` | vanilla | Custom skin tone .fv2. **Bool form**: `true` = load vanilla skin; `false` = disable (body has integrated skin). String form ships your own. |
| `diamondFpk` | disabled | Diamond filter .fpk |
| `diamondFv2` | vanilla | Diamond filter .fv2 |
| `voiceFpk` | vanilla | Voice .fpk routed through `LoadPlayerVoiceFpk` |

> The bool form of these fields is a shortcut — they're path fields under
> the hood. `true` writes the sentinel "load vanilla", `false` writes the
> sentinel "load nothing". Same applies inside variant entries.

#### Variants and display name

| Field | Type | Default | Meaning |
|---|---|---|---|
| `displayName` | string | nil | LangId for variant 0 (this branch's BASE) cycle-button label. Hashed via StrCode64. |
| `displayNameHash` | int | nil | Pre-computed StrCode64 hash (used if `displayName` not set). |
| `variants` | table[] | empty | Up to 14 alternate variants. Each variant table accepts `partsPath`, `fpkPath`, `camoFpk`, `camoFv2`, `diamondFpk`, `diamondFv2`, `voiceFpk`, `displayName`, `displayNameHash`. Any unset field inherits from the branch base. |

#### Per-PT behavior flags

| Field | Type | Default | Meaning |
|---|---|---|---|
| `enableArm` | bool | `true` | `false` suppresses Snake's bionic prosthetic arm |
| `enableHead` | bool | `false` | `true` keeps the orig face/head FPK pipeline live for outfits whose body has no integrated head |
| `defaultSoldierFaceId` | int | 0 | Override for `info+0x04` when `enableHead=true` and the player slot has 0 |

#### Per-PT iDroid suit-name

| Field | Type | Meaning |
|---|---|---|
| `langEquipName` | string | LangId for this PT's iDroid suit-name. Overrides the orig translator's blank return for our custom partsType range. Hashed via StrCode64. |

#### Per-PT HEAD OPTION submenu

| Field | Type | Default | Meaning |
|---|---|---|---|
| `headOptions` | string[] \| int[] | empty | HEAD OPTION submenu entries. See list of accepted entries below. |
| `supportsHeadOptions` | bool | auto | Auto-implies `true` when `headOptions` is non-empty. |

Each entry in `headOptions = {...}` can be:

- A vanilla alias string (case- and separator-insensitive): `"NONE"`,
  `"BANDANA"`, `"INFINITE BANDANA"`, `"BALACLAVA"`, `"SP-HEADGEAR"`,
  `"HP-HEADGEAR"`
- A vanilla equipId number (`0x400` / `0x20E..0x212`)
- A custom-head NAME registered via `V_TppPlayer.AddHeadOption` /
  `V_FrameWork.RegisterHeadOption`
- A custom-head equipId number

Note: BANDANA / INFINITE BANDANA only render on Snake/Avatar — DDMale and
DDFemale should use the BALACLAVA family.

#### Per-PT camo bonus

Either pin to a vanilla camo's bonus row (INHERIT) OR define a custom
82-material row (UNIQUE). If both are passed on the same branch,
`camoBonusValues` wins (more specific intent).

| Field | Type | Meaning |
|---|---|---|
| `camoBonusType` | int | Pin to a vanilla `PlayerCamoType` 0..116. Branch inherits that camo's bonus row. |
| `camoBonusValues` | table | Sparse 82-material custom bonus row (keyed by material name like `MTR_LEAF` or 1-based index 1..82). The framework allocates a virtual `PlayerCamoType` id (200..254 range; pool of 55 slots, shared across all custom branches). |

### Outfit-level optional field: `develop`

The R&D table entry (cost, time, lang strings, icon, etc.). The wrapper
auto-fills `develop.const.p00` and `develop.const.p50` with the allocated
`developId` / `flowIndex`. If omitted, the outfit registers without an R&D
entry — it's still selectable in the UNIFORMS panel via the framework's
auto-injected row.

| Field | Type | Meaning |
|---|---|---|
| `develop.const` | table | R&D constants. See APPENDIX B in the main API reference. |
| `develop.flow` | table | R&D flow params (`developGmpCost`, `developTimeMinute`, `resourceType1`/`resourceType1Count`, `initialAvailable`, etc.). |

### Return value

On success: three numbers — `partsType` (0x40..0x7F), `developId`, `flowIndex`.
On failure: `false`. Common failure causes (always logged):

- Missing `name`
- User passed `developId` or `flowIndex` (framework-owned)
- No populated playerType branch
- Branch missing required `partsPath` or `fpkPath`
- Custom partsType / selectorCode pool exhausted (64 / 127 slots per session)
- Registry full (128 outfits)
- Camo virtual-id pool exhausted (55 slots) — only affects branches that
  declare unique `camoBonusValues`

## `V_TppPlayer.SetOutfitVariant(developId, variantIndex)`

Sets the active variant. Looks up paths from the LIVE playerType branch.
`variantIndex` is clamped to the outfit's `variantCount` (0 always returns
the branch base).

Returns `true` on success, `false` if `developId` is unknown.

## `V_TppPlayer.GetOutfitInfo(developId)`

```lua
{
    partsType            = <int>,    -- 0x40..0x7F
    selectorCode         = <int>,    -- 0x80..0xFE
    flowIndex            = <int>,

    -- Per-PT support flags (Snake↔Avatar bridge reflected here).
    supportsSnake        = <bool>,
    supportsDDMale       = <bool>,
    supportsDDFemale     = <bool>,
    supportsAvatar       = <bool>,

    variantCount         = <int>,    -- max across populated branches
    activeVariant        = <int>,
    supportsHeadOptions  = <bool>,
}
```

Returns `nil` if no outfit is registered for `developId`.

## Examples

- `OutfitSimple.lua` — minimal one-asset registration
- `OutfitWithCamoAndFv2.lua` — custom camo + FV2 + diamond
- `OutfitWithHeadOptions.lua` — HEAD OPTION submenu enabled
- `OutfitWithVariants.lua` — per-PT variants
- `OutfitWithCamoBonus.lua` — surface-bonus camo pin
- `OutfitWithCustomHead.lua` — custom HEAD OPTION entry
- `OutfitFullExample.lua` — every feature combined
