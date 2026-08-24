# Changelog

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
