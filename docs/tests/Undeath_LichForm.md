# Undeath Lich Form Test Checklist

## Test setup
- Enable debug when validating:
  - `Data/SKSE/Plugins/wheeler/wheelBehavior.ini`
  - `[TransformWheels] DebugLog = true`
  - `[LichForm] DebugLog = true`
- Ensure `[LichForm] Enabled = true`.
- Keep a baseline save before entering any transform.

## Logging signals to verify
Expected log themes in `wheeler.log`:
- Lich race detection and source reason.
- State transition to `Lich`.
- Chosen mode/policy (`Overlay/Replace`, guard mode).
- Populate result counts (`eligible/forms/pages` style logs).
- Guard blocks: `TransformWheels: [LichGuard] blocked ...` or `blocked spell activation source=...`.

## Matrix
### 1. Classical Lichdom
1. Enter lich form from human.
2. Confirm Lich wheel is selected automatically.
3. Confirm base wheel access behavior:
   - `Mode=Overlay`, `AllowBaseWheel=true`: can cycle to base wheels.
   - `Mode=Replace` or `AllowBaseWheel=false`: only transform wheels.
4. Confirm lich kit spells appear (e.g. revert + lich spells) without unrelated spillover.
5. Cast normal non-lich spell from base wheel (Overlay mode): should still work.
6. Trigger guard check:
   - try activating other transform powers from wheel.
   - expected: blocked by guard, no accidental second transform.
7. Exit lich (revert spell).
8. Confirm prior human wheel context restored.

### 2. Immersive Lichdom SSE
1. Enter lich.
2. Confirm Lich state detection occurs.
3. Confirm wheel populates with available lich spell kit (menu-selected/equipped spells included).
4. Repeat guard test (other transforms blocked per policy).
5. Exit lich and verify wheel context restore.

### 3. Classical Lichdom - Vampiric addon
1. Test with vampire-enabled character.
2. Enter lich and confirm Lich wheel policy applies.
3. Exit and verify return race/context is correct.
4. If werewolf/vampire transforms are also available, validate precedence:
   - default `PrecedenceOrder = Werewolf,VampireLord,Lich,GenericOthers`.
5. Confirm no cross-state wheel contamination.

### 4. Regression (existing supported transforms)
1. Werewolf flow still creates/uses werewolf wheel.
2. Vampire Lord flow still creates/uses VL wheel.
3. Existing generic mods (Wrath/Champion/Lost Grimoire) still create isolated per-form wheels.
4. Verify no FPS drop or UI hitch when idle in human form.

## Guard mode-specific checks
### `TransformGuard = BlockOtherTransforms`
- Lich kit spells usable.
- Non-lich transform entries blocked.

### `TransformGuard = BlockAllTransformsExceptExit`
- Any transform-like spell blocked except configured exit/revert.

### `TransformGuard = Off`
- No block behavior.

### `HideGear = true`
- Armor/light/shield activations from wheel are blocked while lich is active.

## Populate mode checks
### `PopulateMode = ModList`
- Primary expected mode for Undeath.
- Lich kit spells should appear from kit/race/equipped sources.

### `PopulateMode = Delta`
- Use as fallback; verify baseline is captured before transform.

### `PopulateMode = Manual`
- No auto fill expected; user-edited Lich wheel should be used as-is.

## Failure triage hints
- Lich state not detected:
  - broaden `[LichForm] RaceEditorIDContains`/`RaceKeywords`/`RaceFormIDs`.
- Wrong spells in wheel:
  - tighten `[LichForm] SpellTokens`.
  - switch `PopulateMode` (`ModList` <-> `Delta` <-> `Manual`) to isolate source.
- Revert blocked incorrectly:
  - ensure `[LichForm] ExitSpellTokens` includes revert naming used by mod.
