# QA Notes - Inventory Snapshot Cache (MainWheel + FavWheel + AmmoWheel)

## Scope
- MainWheel (`wheeler.dll`) and FavWheel (`favwheel.dll`) inventory polling during visible wheel render.
- AmmoWheel (`wheeler.dll`) inventory polling + slot label layout + center panel damage/text layout while visible.

## Expected Behavior
- Opening MainWheel or FavWheel should no longer trigger major frame drops from per-frame inventory snapshots.
- Inventory-backed missing-state visuals still update while open, with up to ~250ms staleness by design.
- No regressions in edit mode, RTU, dMenu modal flow, gamepad navigation, or missing placeholder behavior.

## Perf Log Check
- Enable `MainWheelDebug::Perf` logging.
- While wheel is open, expect roughly 4 refreshes/sec (interval 0.25s), not one refresh per frame.
- Log format:
  - MainWheel: `[Perf] MainWheel InventorySnapshot refresh ms=... interval=0.25 countThisSecond=...`
  - FavWheel: `[Perf] FavWheel InventorySnapshot refresh ms=... interval=0.25 countThisSecond=...`

## AmmoWheel Perf Log Check
- Set `Data/SKSE/Plugins/wheeler/AmmoWheel.ini` `[Debug] LogPerf=true`.
- Optional interval tuning: `[Performance] InventorySnapshotIntervalSeconds` (default `0.25`, clamped `0.05..1.0`).
- While AmmoWheel is open, expect rate-limited 1Hz logs like:
  - `[Perf][AmmoWheel] invRefreshMs=... invRefreshCount=... damageRebuildMs=... labelRebuildMs=... centerRebuildMs=... interval=...`
- `invRefreshCount` should track interval refreshes (about 4/sec at 0.25s), not frame rate.

## Functional Test Matrix
- Small inventory and very large inventory.
- Open/close wheel repeatedly.
- Edit mode on/off.
- dMenu open while wheel is modal.
- Inventory changes while wheel is open:
  - drop/pickup, consume potion/food, equip/unequip, favorites edits.
- Confirm UI reflects changes within <= 250ms.
- AmmoWheel-specific checks:
  - Rapidly move hover across many slots: no major FPS cliff from label layout.
  - Toggle center panel fields/order + word-wrap: no per-frame stutter.
  - Damage highlight remains correct (max damage gold, others alternate color).
  - Hover/selection/reskin/popups unchanged.

## Regression Risks To Watch
- UniqueID-dependent mutable gear restore after re-acquire.
- Missing placeholders restoring back to real items.
- Any close/open edge where stale snapshot persists (cache should invalidate on close).
