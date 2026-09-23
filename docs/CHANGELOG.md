# Changelog

## v1.3.4

### Highlights

- Added ingredient wheel-item support and improved wheel behavior for configured Favorites and Quick Favorites workflows.
- Improved weapon switching and restoration safety for bound weapons, identical weapon instances, and supported Immersive Weapon Switch transfers.

### Transform Wheel

- Reworked Transform Wheel behavior for Werewolf, Vampire Lord, and configured Lich forms.
- Added form-specific ability discovery and configuration for mod-added Werewolf and Vampire Lord abilities.
- Improved Vampire Lord generated-wheel contents, Revert Form availability, duplicate-entry handling, and melee/Blood Magic transitions.
- Improved Lich and Undeath transformed-form handling with stable action ordering, safer staff/direct-cast behavior, protected return-form actions, and prevention of accidental transform chaining.

### Compatibility

- Added Skyrim 1.7.99 and 1.7.104 support with CommonLibSSE-NG 6.7 and Address Library v5. Skyrim 1.7.99 includes explicit structural hook validation; 1.7.104 uses the matching Address Library database and runtime-version path.

### Weapons & Poison

- Corrected active and hand indicators for identical weapons and true two-handed weapons.
- Added safer already-poisoned weapon handling and applied-poison details.

### Ammo Wheel

- Added ranged-weapon poison information to Ammo Wheel Center Fields.

### Integrations

- Improved Inventory Injector icon reliability outside the Inventory Menu, including ingredient icon controls.
- Improved dMenu input ownership and cooperative opening, allowing Ammo Wheel to remain visible while dMenu is open for live layout editing.
- Expanded OStim scene-wheel controls, navigation, and optional appearance refresh behavior.
- Fixed Action Hotkeys Bridge refresh safety while Wheeler is open.

### Controls

- Added configurable Favorites and Quick Favorites open/edit behavior with input pass-through.
- Made keyboard wheel-close bindings configurable.

## 2026-04-26
- Release metadata updated for `v1.3.3`.
- Build/version logging banner updated to report `v1.3.3` with build date `4/26/2026`.
- Windows file metadata/resource version now aligns with the `1.3.3` release.

## 2026-04-19
- Release metadata updated for `v1.3.2`.
- Build/version logging banner updated to report `v1.3.2` with build date `4/19/2026`.
- Windows file metadata/resource version now aligns with the `1.3.2` release.

## 2026-02-15
- Input filtering: while Main Wheeler is open, background menu activation user-events (`Activate`/`Accept`) are now consumed in `InventoryMenu`, `ContainerMenu`, `MagicMenu`, `FavoritesMenu`, `LootMenu`, and `LootMenuCF` to prevent vanilla equip/use behind the wheel.
- Input fallback: if that activation input is not bound to any Wheeler action, Wheeler routes it to confirm down/up as a user-event fallback (no hardcoded gamepad button IDs, no override of user bindings).
- Performance: AmmoWheel visible draw now uses `InventorySnapshotCache` (default 0.25s, clamped 0.05-1.0s) instead of calling `GetInventory()` every frame.
- Performance: AmmoWheel per-entry slot labels are precomputed into a cached layout and no longer run per-frame wrap/measure/truncate loops in `drawSlot()`.
- Performance: AmmoWheel center panel now uses cached damage/max-damage state plus cached field/text layout; per-frame full ammo damage scans and per-frame `CenterFields::Order` parsing were removed.
- Perf diagnostics: added 1Hz debug-gated telemetry (`[Perf][AmmoWheel] ...`) controlled by `AmmoWheel.ini` `[Debug] LogPerf`.

## 2026-02-13
- Performance: MainWheel and FavWheel now use a timed inventory snapshot cache while the wheel popup is visible (default refresh 0.25s, clamped 0.05-1.0s) instead of calling `GetInventory()` every frame.
- Performance: `WheelEntry` render-path missing-state checks now use the passed inventory snapshot (`IsAvailable`) and no longer call `IsInPlayerInventory()` during draw.
- Perf diagnostics: added rate-limited (1Hz) snapshot refresh telemetry under `MainWheelDebug::Perf` showing refresh ms and refresh count per second.
- Stability: snapshot cache is invalidated on wheel close to avoid stale state carryover between sessions.

## 2026-02-10
- Removed per-action modifier bindings. Kept optional modifier only for wheel toggle open/close.
- Toggle modifier keys are non-exclusive and can be reused for other bindings. Toggle only triggers when modifier is held.
- Added cooperative input compatibility routing between MainWheel and AmmoWheel to prevent cross-module key starvation on shared bindings.
- Added debug-gated InputSpy diagnostics (`[Debug] inputSpy*`) with ring buffer + dump hotkey, plus startup binding conflict inventory reporting.
