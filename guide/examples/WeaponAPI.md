# V_FrameWork Weapon API — Modder Reference

The weapon API has two layers:

**High-level (recommended for mods)** — `V_TppEquip` wrapper module:

| Function | Purpose |
|---|---|
| `V_TppEquip.RegisterConstantEquipId(name)` | Reserve a stable equip ID. Persisted across sessions. |
| `V_TppEquip.DeclareWPs(name)` | Declare a weapon-part constant. Returns `wpId`. |
| `V_TppEquip.DeclareSWPs(name)` | Declare a support-weapon constant. Returns `swpId`. |
| `V_TppEquip.DeclareRCs(name)` | Declare a receiver constant. Returns `rcId`. Feeds `SetGunBasic.receiverId`. |
| `V_TppEquip.DeclareAMs(name \| {names...})` | Declare ammo. Hook auto-injects on game reload. |
| `V_TppEquip.DeclareBAs(name)` | Declare a barrel-assembly constant. Returns `baId`. Feeds `SetGunBasic.barrelId`. |
| `V_TppEquip.DeclareSTs(name)` | Declare a stock constant. Returns `stId`. Feeds `SetGunBasic.stockId`. |
| `V_TppEquip.DeclareMZs(name)` | Declare a muzzle constant (suppressor / comp). Returns `mzId`. Feeds `SetGunBasic.muzzleId`. |
| `V_TppEquip.DeclareSKs(name)` | Declare a sight/scope constant. Returns `skId`. |
| `V_TppEquip.DeclareUBs(name)` | Declare an underbarrel constant (grenade launcher etc). Returns `ubId`. |
| `V_TppEquip.AddToEquipIdTable(rows)` | Register equip rows (parts/fpk paths). |
| `V_TppEquip.SetGunBasic(t)` | Wire gun components together. |
| `V_TppEquip.AddToEquipDevelopTable(key, data)` | Add Mother Base R&D entry. Returns `developId`. |
| `V_TppEquip.AddToEquipMotionDataTable(t)` | Bind `.mtar` assembly motion archives to equipIds. Hook-merged on every engine reload. |
| `V_TppEquip.SetEquipParameters(t)` | Set per-component parameters (receiver / barrel / magazine / etc). |
| `V_TppEquip.SetSupportWeaponType(id, type)` | Bind a support-weapon ID to a SWP type. |
| `V_TppEquip.SetEquipIdIconFtexPath(id, path)` | Override the inventory icon. |

**Low-level (raw bridge)** — `V_FrameWork` table:

Every wrapper above forwards 1:1 to `V_FrameWork.<same name>`. Use the
wrappers unless you need exact control over argument shape.

## The registration flow

A complete weapon needs these steps in order:

```
1. RegisterConstantEquipId  → equipId        (the slot in inventory)
2. DeclareWPs               → wpId           (the weapon part)
3. (optional) Declare components: RCs / BAs / STs / MZs / SKs / UBs / AMs
4. AddToEquipIdTable        → registers the .parts / .fpk paths
5. SetGunBasic              → wires wpId → receiver / barrel / ammo / etc
6. (optional) SetEquipParameters → fine-tune per-component stats
7. (optional) AddToEquipDevelopTable → R&D entry (icon, lang, cost)
8. (optional) AddToEquipMotionDataTable → custom animations
9. (optional) SetEquipIdIconFtexPath → custom inventory icon
```

Steps 3–8 are all optional: a minimal mod that just reuses vanilla
components (vanilla AK barrel + vanilla 5.45 ammo + new model) skips
them. Steps 4–5 are required to make the equip actually load and shoot.

## Component declarators

Each `Declare*` function reserves a unique numeric ID for a component
name, writes it into the `TppEquip` global table, and returns the ID.
Calling with the same name returns the same ID (idempotent).

ID allocation: the framework scans `TppEquip` for the highest existing
key matching the prefix and bumps from there. Floors below were chosen
to sit just above the stock max, so collisions are impossible even if
the scan misses (e.g. mod loads before vanilla data scripts).

| Declarator | Stock entries | Floor | Feeds |
|---|---|---|---|
| `DeclareWPs` | 501 | `0x203` | top-level weapon ID |
| `DeclareSWPs` | 89 | `0x59` | support-weapon ID |
| `DeclareRCs` | 234 | `0xEA` | `SetGunBasic.receiverId` |
| `DeclareAMs` | 192 | `0xC0` | `SetGunBasic.ammoId` |
| `DeclareBAs` | 115 | `0x80` | `SetGunBasic.barrelId` |
| `DeclareSTs` | 25 | `0x20` | `SetGunBasic.stockId` |
| `DeclareMZs` | 29 | `0x20` | `SetGunBasic.muzzleId` |
| `DeclareSKs` | 43 | `0x40` | sight/scope params |
| `DeclareUBs` | 23 | `0x20` | underbarrel params |

Naming convention: prefix the name with the constant tag matching the
declarator (`WP_`, `RC_`, `BA_`, etc.). The runtime scan-and-bump only
considers keys with the right prefix, so mismatched names won't collide
but also won't get auto-IDs that "look right" alongside the stock ones.

```lua
local wpId = V_TppEquip.DeclareWPs("WP_MyMod_AK12_00")
local rcId = V_TppEquip.DeclareRCs("RC_MyMod_AK12")
local baId = V_TppEquip.DeclareBAs("BA_MyMod_AK12_Long")
local mzId = V_TppEquip.DeclareMZs("MZ_MyMod_AK12_Suppressor")
local stId = V_TppEquip.DeclareSTs("ST_MyMod_AK12_Folding")
local skId = V_TppEquip.DeclareSKs("SK_MyMod_RedDot")
local ubId = V_TppEquip.DeclareUBs("UB_MyMod_M203")

-- Single ammo
V_TppEquip.DeclareAMs("AM_MyMod_762x39")
local ammoId = TppEquip.AM_MyMod_762x39

-- Or batch
V_TppEquip.DeclareAMs({
    "AM_MyMod_762x39_Standard",
    "AM_MyMod_762x39_Tracer",
    "AM_MyMod_762x39_AP",
})
```

## `V_TppEquip.SetGunBasic(t)`

Wires a weapon-part ID to its component IDs. `weaponId` is **required**;
every other field is optional and falls back to a vanilla default.

```lua
V_TppEquip.SetGunBasic({
    weaponId   = wpId,         -- REQUIRED — from DeclareWPs
    receiverId = rcId,         -- from DeclareRCs (or vanilla RC_*)
    barrelId   = baId,         -- from DeclareBAs (or vanilla BA_*)
    ammoId     = ammoId,       -- from DeclareAMs (or vanilla AM_*)
    stockId    = stId,         -- from DeclareSTs (or vanilla ST_*)
    muzzleId   = mzId,         -- from DeclareMZs (or vanilla MZ_*)
    sightId    = skId,         -- from DeclareSKs (or vanilla SK_*)
    underBarrelId = ubId,      -- from DeclareUBs (or vanilla UB_*)
    grade      = 8,            -- 1..15, develop-tree tier
})
```

Mix-and-match is the point: a weapon mod that only changes the model
(reusing vanilla AK barrel/stock/etc) only sets `weaponId` and the
component IDs it actually wants to override.

## `V_TppEquip.AddToEquipMotionDataTable(t)`

Registers `{equipId, mtarPath}` pairs that bind a weapon assembly motion
archive to an equip slot. `t` is an array of such pairs:

```lua
V_TppEquip.AddToEquipMotionDataTable({
    { equipId1, "/Assets/tpp/motion/mtar/equip/chimera/assemble/foo.mtar" },
    { equipId2, "/Assets/tpp/motion/mtar/equip/chimera/assemble/bar.mtar" },
})
```

**How it works:** the framework queues each `{equipId, mtarPath}` pair
in C++ and hooks the engine's `ReloadEquipMotionData` (`0x1463b2bf0`).
Whenever the engine reloads motion data — vanilla data scripts call
`TppEquip.ReloadEquipMotionData(t)` at startup, Chimera's framework
fires its own reload, etc. — the hook splices the queued pairs into the
caller's `arg.MotionDataTable` array before forwarding to orig. So
both vanilla entries and your additions populate the engine buffer on
every reload, with no manual reload trigger required.

**Dedup:** queueing the same `equipId` twice replaces the previous
`mtarPath` (last write wins). That makes it safe to call repeatedly
during mod init.

**Timing:** call any time before the engine reload fires. Safe to call
from your mod's load script, from `OnAllocate`, or anywhere else —
the queue is independent of Lua state. The hook merges it in at reload
time.

**Schema details:** each pair is a positional 2-tuple. The engine reads
`pair[1]` as `equipId` (compressed at `>= 0x400`) and `pair[2]` as the
.mtar path (hashed via `fox::Path::CInitWithString`). Storage is at
fixed C++ buffer `0x142a6b408 + equipId*8` (one `PathHash` per slot).

**v2 schema (per-bone motions/poses/mtars/assignments)** is a separate
engine entry point — `TppEquip.ReloadEquipMotionData2(t)` — with a
richer schema. The framework doesn't wrap that; mods using it call it
directly. Chimera's `EquipMotionDataForChimera.lua` is the canonical
example.

## Reserved ID ranges (auto-allocated)

| ID type | Vanilla range | Auto-pool starts at | Lifetime |
|---|---|---|---|
| `equipId` (RegisterConstantEquipId) | 0..0x289 | per session, gap-fill | persisted in `V_FrameWork_State.lua` |
| `developId` (AddToEquipDevelopTable) | 1..50000 | 51006 | persisted in `V_FrameWork_State.lua` |
| `flowIndex` (AddToEquipDevelopTable) | 1..921 | 922 | persisted in `V_FrameWork_State.lua` |
| `wpId` | 0..0x1F4 | 0x203 | session, name-keyed in-memory |
| `rcId` | 0..0xE9 | 0xEA | session, name-keyed in-memory |
| `baId` | 0..0x72 | 0x80 | session, name-keyed in-memory |
| `stId` | 0..0x18 | 0x20 | session, name-keyed in-memory |
| `mzId` | 0..0x1C | 0x20 | session, name-keyed in-memory |
| `skId` | 0..0x2A | 0x40 | session, name-keyed in-memory |
| `ubId` | 0..0x16 | 0x20 | session, name-keyed in-memory |
| `swpId` | 0..0x58 | 0x59 | session, name-keyed in-memory |
| `ammoId` | 0..0xBF | 0xC0 | session, name-keyed in-memory |

`equipId`, `developId`, `flowIndex` persist across game launches via
`V_FrameWork_State.lua` (keyed by the name you pass). The component
declarators (WPs/RCs/etc) re-allocate per session, but the Lua side of
your mod re-declares them at every load anyway, so this is invisible to
mod authors.

## Worked example — a fully custom assault rifle

```lua
local equipId = V_TppEquip.RegisterConstantEquipId("EQP_WP_MyMod_AK12")

-- Component IDs
local wpId = V_TppEquip.DeclareWPs("WP_MyMod_AK12_00")
local rcId = V_TppEquip.DeclareRCs("RC_MyMod_AK12")
local baId = V_TppEquip.DeclareBAs("BA_MyMod_AK12_Long")
local stId = V_TppEquip.DeclareSTs("ST_MyMod_AK12_Folding")
local mzId = V_TppEquip.DeclareMZs("MZ_MyMod_AK12_Brake")

V_TppEquip.DeclareAMs("AM_MyMod_545x39")
local ammoId = TppEquip.AM_MyMod_545x39

-- Register the model + fpk
V_TppEquip.AddToEquipIdTable({
    { equipId, TppEquip.EQP_TYPE_Assault, 290,
      TppEquip.EQP_BLOCK_MISSION,
      "/Assets/tpp/parts/wp/wp_mymod_ak12.parts",
      "/Assets/tpp/pack/wp/wp_mymod_ak12.fpk" },
})

-- Wire components
V_TppEquip.SetGunBasic({
    weaponId   = wpId,
    receiverId = rcId,
    barrelId   = baId,
    stockId    = stId,
    muzzleId   = mzId,
    ammoId     = ammoId,
    grade      = 7,
})

-- R&D entry (Mother Base develop tree)
V_TppEquip.AddToEquipDevelopTable("MyMod:AK12", {
    const = {
        equipID            = equipId,
        equipDevelopTypeID = TppMbDev.EQP_DEV_TYPE_Assault,
        langEquipName      = "wp_mymod_ak12_name",
        langEquipInfo      = "wp_mymod_ak12_desc",
        iconFtexPath       = "/Assets/tpp/ui/icon/wp_mymod_ak12_icon",
        equipDevelopGroupID = TppMbDev.EQP_DEV_GROUP_WEAPON_120,
        unk36 = 1,
    },
    flow = {
        grade              = 7,
        developGmpCost     = 50000,
        resourceType1      = "CommonMetal",
        resourceType1Count = 260,
        resourceType2      = "PreciousMetal",
        resourceType2Count = 50,
    },
})
```

That's a complete weapon: shows up in R&D, develops with GMP, equips,
shoots custom ammo, uses your model. Customize further by adding
`SetEquipParameters` entries (per-component stats), `AddToEquipMotionDataTable`
(animations), or `SetEquipIdIconFtexPath` (icon).

## Examples

See:

- `WeaponSimple.lua` — minimum viable weapon (just `wpId` + `equipId`, vanilla components)
- `WeaponFullExample.lua` — every function used, every parameter listed with its default value (reference-style; copy and prune)
