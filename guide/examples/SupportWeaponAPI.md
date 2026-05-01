# Custom support-weapon system

A complete create-not-reuse pipeline for brand-new support weapons. Every data layer is independent: the framework gives you a fresh SWP_TYPE_* id, fresh inventory id, fresh blast values, fresh damage values, fresh ammo/grade row.

The one engine-level constraint: **throw mechanics must pick one of 5 native templates.** `ThrowingImpl::UpdateAction` at retail `0x1411F5DC0` is a hardcoded switch on swpType that dispatches to `UpdateActionGrenade`/`UpdateActionSmoke`/`UpdateActionMolotovCocktail`/`UpdateActionDecoy`/`UpdateActionKibidango`. There is no sixth implementation in the binary, so a new throwable must inherit one of those for its per-frame physics, animations, particles, sound, and detonation trigger.

Everything else is yours.

## API

```lua
local categoryId = V_TppEquip.RegisterSupportWeaponCategory{
    name     = "MyChaff",          -- REQUIRED: TppEquip.SWP_TYPE_MyChaff = categoryId
    behavior = "smoke",            -- optional, default "grenade"
    blast    = { ... } | BLA_id,   -- optional
    damage   = { ... } | ATK_id,   -- optional
    swpRow   = { ... },            -- optional
}
```

### `behavior`

Selects the native throw template. Accepts a string alias or a vanilla `TppEquip.SWP_TYPE_*` number.

| Alias | Aliases | Native function | Vibe |
|---|---|---|---|
| `"grenade"` (default) | — | `UpdateActionGrenade` | Frag / fuse / explode |
| `"smoke"` | `"stun"` | `UpdateActionSmoke` | Smoke / stun / flares |
| `"molotov"` | `"fire"` | `UpdateActionMolotovCocktail` | Burning AoE |
| `"decoy"` | — | `UpdateActionDecoy` | Sound emitter |
| `"kibidango"` | `"lure"` | `UpdateActionKibidango` | Animal lure |

### `blast`

Two forms:

**Inline** — your own values:
```lua
blast = {
    flag     = 2,    -- 0..4: 0=ground 1=smoke 2=stun 3=fire 4=flash
    maxRange = 15,   -- meters
    optRange = 10,
}
```
Framework owns a synthetic 6-byte BlastParameter struct stored on the category and returns its pointer from the `ThrowingImpl::GetBlastParamByEquipId` hook.

**Linked** — reuse an existing row:
```lua
blast = TppEquip.BLA_StunGrenade   -- or any BLA_* number
```
Framework computes the live BlastParameter table row at hook time. Reflects any `EquipParameterTables.lua` content / runtime edits.

### `damage`

Two forms:

**Inline** — your own DamageParameter row (26 bytes of native struct). Field names match IH/muffins' canonical DamageParameterTables.lua row layout (cols 2..32; col 1 = AttackId is auto-allocated):

```lua
damage = {
    lethalDamage    = 4500,
    staminaDamage   = 0,
    impactForce     = 2000,
    lethalDamageUI  = 4500,
    injureType      = TppDamage.INJ_TYPE_DISLOCATED,
    injurePart      = TppDamage.INJ_PART_ALL,
    damageSource    = TppDamage.DAM_SOURCE_Throwing,
    hitNPC          = 1,
    isStun          = 0,
    isTranq         = 0,
    isFire          = 0,
    isGas           = 0,
    isElectric      = 0,
    unk3 = 3500, unk4 = 4000, unk7 = 4000, unk8 = 4000,
    unk11 = 4, unk12 = 20,
}
```

Or paste a positional row (vanilla `EquipParameterTables.lua` style, dropping column 1 = AttackId):

```lua
damage = {
    row = { 4500, 3500, 4000, 0, 0, 4000, 4000,
            TppDamage.INJ_TYPE_DISLOCATED, TppDamage.INJ_PART_ALL, 4, 20,
            1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            TppDamage.DAM_SOURCE_Throwing,
            4500, 0, 2000 },
}
```

When both `row[]` and named keys are supplied, named keys override.

The framework allocates a synthetic AttackId in `0x8000..0xFFFE`, plumbs it through `EquipParameterTablesImpl::GetAttackIdByEquipId`, and intercepts `DamageParameterTable::GetDamageParameter` to return the framework-owned struct.

**Linked** — reuse an existing AttackId:
```lua
damage = TppEquip.ATK_StunGrenade  -- or any AttackId number
```
Framework hooks the equipId→AttackId resolver to redirect; the orig DamageParameterTable lookup picks up the right row naturally.

### `swpRow`

`SupportWeaponParameter` row (8 bytes of native struct):
```lua
swpRow = {
    ammo  = 4,    -- capacity (clamped: <1 → 0x3FFF)
    p1    = 3,    -- parameter 1 (×10 internally)
    p2    = 0,    -- parameter 2 (×10 internally)
    grade = 1,    -- grade level
}
```
Returned by the `EquipParameterTablesImpl::GetSupportWeaponParameterBlock` hook. Required for the inventory/HUD/ammo machinery to recognize your custom equipId — vanilla data has no row for swpIds outside the original allocation.

## Identification at runtime

```lua
-- Lua-side: see THROUGH the framework's spoof to the real category id.
local cat = V_TppEquip.GetSupportWeaponCategory(equipId)
if cat == TppEquip.SWP_TYPE_MyChaff then
    -- ... your custom logic ...
end

-- Vanilla-compat: returns the BEHAVIOR template's swpType (e.g. 2 for "smoke").
-- IH-style scripts that branch on SWP_TYPE_StunGrenade etc. keep working.
local vanillaType = TppEquip.GetSupportWeaponTypeId(equipId)
```

## Data layer reference

| Layer | Lookup function (retail) | Struct size | Hook |
|---|---|---|---|
| Identity (swpType) | `EquipIdTableImpl::GetSupportWeaponTypeId` `0x140A29FE0` | enum (0..0x16) | always |
| BlastParameter | `ThrowingImpl::GetBlastParamByEquipId` `0x1415D6D20` | 6 bytes | always |
| Ammo / grade row | `EquipParameterTablesImpl::GetSupportWeaponParameterBlock` `0x140A3C980` | 8 bytes | always |
| AttackId | `EquipParameterTablesImpl::GetAttackIdByEquipId` `0x140A3B5E0` | ushort | always |
| DamageParameter | `DamageParameterTable::GetDamageParameter` (vtable[0]) | 26 bytes (0x1A) | lazy on first inline-damage register |

The DamageParameter hook target is fetched from the singleton's vtable via `fox::GetQuarkSystemTable()` (retail `0x140BFF3F0`) → `qs[0x98] → +0xa0 → vtable[0]` because the function body has no fixed retail address (it's reached via vtable dispatch from many call sites).

## Examples

- [SupportWeaponSimple.lua](SupportWeaponSimple.lua) — minimal recipe (smoke template + inline blast + inline ammo row).
- [SupportWeaponFull.lua](SupportWeaponFull.lua) — every layer custom (blast + damage + swpRow all inline).
