# Undeath Lich Form Support (Wheeler Refined)

## Scope
This document covers Lich integration for:
- Undeath Classical Lichdom (UCL)
- Undeath Immersive Lichdom SSE
- Classical Lichdom - Vampiric addon

Implementation target is the existing generic transform subsystem, extended with a Lich policy (overlay/replace/disabled), transform guard, and config-driven detection.

## Source Analysis
This integration is based on the public scripts and package conventions used by the supported Undeath variants. Plugin mapping follows the relevant package naming and script namespaces.

### Classical Lichdom: state detection and lifecycle
Primary scripts:
- `necrolichtransformscript.psc`
- `necroreverteffectscript.psc`
- `lichnecroactorscript.psc`
- `cr_necrouclmcmscript.psc`

Observed behavior:
- Enter condition is race-based:
  - `if (TheLich.GetRace() != LichRace)` then transform path (`necrolichtransformscript.psc:81`).
  - Transition applies `TheLich.SetRace(LichRace)` (`necrolichtransformscript.psc:150`).
- Exit is race restore based:
  - Revert effect checks `TheLich.GetRace() == LichRace` (`necroreverteffectscript.psc:79`).
  - Revert restores original race: `SetRace(playerOriginalRace)` (`necroreverteffectscript.psc:164`).
- Lich abilities are explicitly added/equipped on enter:
  - `AddSpell(NecroRevert)`, `EquipSpell(NecroRevert, 2)` (`necrolichtransformscript.psc:214-215`).
  - `AddSpell(NecroUClDarkConduit)`, `AddSpell(NecroDeathGrip)` (`necrolichtransformscript.psc:217-218`).
  - Hand equips include `NecroUCLIceCoffin` and `NecroDeathGrip` (`necrolichtransformscript.psc:219-220`).
- Additional UCL scripts/MCM grant many spells/perks outside the exact transform frame (`cr_necrouclmcmscript.psc`), which can create noisy deltas if not filtered.
- Restrictions exist in scripts:
  - `Game.SetBeastForm(true)` and menu/control adjustments.
  - inventory block while lich in actor script (`lichnecroactorscript.psc:192-195`).

### Immersive Lichdom SSE: state and ability model
Primary script:
- `lichnecroactorscript.psc`

Observed behavior:
- Script has `LichRace` checks for behavior while transformed.
- Ability model is menu-driven spell selection (`SpellChange`) with many `Spell Property` entries and `EquipSpell(...)` calls (`lichnecroactorscript.psc:31-59`, `162+`).
- This indicates a large lich spell kit and supports a "seamless/hybrid" play model better than a hard-locked blank wheel.

### Classical Lichdom - Vampiric addon compatibility
Primary scripts:
- `necrolichtransformscript.psc`
- `necrolichtrackingquest.psc`

Observed behavior:
- Same core race swap model (`SetRace(LichRace)`).
- Tracking script explicitly stores vampire race variants as original race for restore (`necrolichtrackingquest.psc:98-127`).
- Transform script comments show compatibility edits for vampirism/lycanthropy handling (vampire cure lines disabled/commented, werewolf cure still conditionally called).
- This confirms coexistence/precedence handling is necessary across Lich/Vampire/Werewolf paths.

## Integration Decision
Decision: integrate Lich in `TransformWheelManager` (generic transform pipeline extension), not a separate standalone module.

Why:
- Existing architecture already manages race-based state detection, wheel tagging, pending transitions, content population, and cleanup.
- Lich differences are policy-level differences (overlay vs replace, populate strategy, guard policy), not a fundamentally separate event model.
- This keeps changes minimal and lowers regression risk for existing Werewolf/Vampire/Generic transform mods.

## Implementation Summary
### Files changed
- `src/bin/Config.h`
- `src/bin/Config.cpp`
- `src/bin/Wheeler/TransformWheelManager.h`
- `src/bin/Wheeler/TransformWheelManager.cpp`
- `src/bin/Wheeler/Wheeler.cpp`
- `src/bin/Wheeler/WheelItems/WheelItemSpell.cpp`
- `src/bin/Wheeler/WheelItems/WheelItemWeapon.cpp`
- `Data/SKSE/Plugins/wheeler/wheelBehavior.ini`

### New Lich config surface
`[LichForm]` section:
- `Enabled`
- `Mode = Overlay|Replace|Disabled`
- `PopulateMode = ModList|Delta|Manual`
- `AllowBaseWheel`
- `TransformGuard = Off|BlockOtherTransforms|BlockAllTransformsExceptExit`
- `BlockBoundSpells`
- `HideWeapons`
- `HideGear`
- `RaceEditorIDContains`, `RaceKeywords`, `RaceFormIDs`
- `SpellTokens`, `ExitSpellTokens`
- `DebugLog`

`[TransformWheels]`:
- `PrecedenceOrder = Werewolf,VampireLord,Lich,GenericOthers`

### Runtime behavior
- Lich detection:
  - race-driven, config matcher first (`RaceFormIDs`, editor id contains, keywords), with conservative fallback `editorId contains "Lich"`.
- Wheel mode:
  - `Overlay`: Lich wheel auto-select on enter, base wheels remain navigable if `AllowBaseWheel=true`.
  - `Replace`: transformed navigation restricted to transform wheels.
  - `Disabled`: no Lich policy active.
- Populate modes:
  - `ModList`: kit/race/equipped-based population (preferred default for Lich).
  - `Delta`: baseline delta based.
  - `Manual`: creates/switches wheel without auto-fill (user-curated).
- Guard:
  - transform-like spells are blocked according to `TransformGuard` while Lich is active.
  - exit/revert spells are allowed.
  - guard is enforced in both queued cast pipelines and direct equip/cast paths.
- Optional weapon blocking:
  - `HideWeapons=true` blocks weapon activation while Lich active.
- Optional gear blocking:
  - `HideGear=true` blocks armor/light/shield activation while Lich active.

## Performance / Event Model
- No new per-frame heavy scans were added.
- State check remains lightweight race check in update loop.
- Expensive spell set computation runs only during transform pending/refresh windows and uses cached baseline snapshots.
- Queue-time and activation-time guard checks are O(1) lookup/string checks per action.

## Conflicts and Edge Cases
- Precedence order controls coexistence (default: Werewolf > VampireLord > Lich > Generic).
- Guard prevents accidental transform chaining from wheel while in Lich.
- If detection is uncertain (missing matchers/assets), system degrades safely (no crash; no forced state).
- Because provided mod source paths lacked plugin binaries, FormID-specific plugin-scoped mapping is intentionally config-driven and overrideable.

## Current limitations
- With script-only source drop (no plugins), exact plugin filename/form records could not be validated here.
- Immersive package in provided path includes one source script; additional compiled scripts (if any) are outside this source snapshot.
