# Regression Test Plan - Wheeler v1.1 Fixes

## Overview
This test plan covers the fixes for:
1. **Scroll unequip regression** - scrolls can now be toggled off from either hand
2. **Shovel/YPS activation regression** - misc items now route through deferred activation
3. **Input gating issues** - DeniedHoldThreshold and DeniedMenuBlocked behavior

## Prerequisites
1. Enable debug logging in `wheelBehavior.ini`:
   ```ini
   [Debug]
   LogActionPolicy = true
   LogActivateRejects = true
   LogMenuBlockReasons = true
   ```
2. Have test items in inventory:
   - At least 2 scrolls (any type)
   - Shovel (from ShovelBody mod)
   - YPS Immersive Hair items (brush, scissors, etc.)
   - Standard potions, weapons, armor

## Test 1: Scroll Toggle Behavior

### 1.1 Equip scroll to right hand, then unequip via primary
- [ ] Open wheel
- [ ] Select a scroll entry
- [ ] Primary activate (LMB/RT) → scroll equips to RIGHT hand
- [ ] **Verify log**: `[Scroll] ActivatePrimary: equip result=SUCCESS`
- [ ] Open wheel again
- [ ] Select same scroll entry
- [ ] Primary activate (LMB/RT) → scroll UNEQUIPS from right hand
- [ ] **Verify log**: `[Scroll] ActivatePrimary: toggled OFF from right hand`
- [ ] Confirm hand is now empty

### 1.2 Equip scroll to left hand, then unequip via secondary
- [ ] Open wheel
- [ ] Select a scroll entry
- [ ] Secondary activate (RMB/LT) → scroll equips to LEFT hand
- [ ] **Verify log**: `[Scroll] ActivateSecondary: equip result=SUCCESS`
- [ ] Open wheel again
- [ ] Select same scroll entry
- [ ] Secondary activate (RMB/LT) → scroll UNEQUIPS from left hand
- [ ] **Verify log**: `[Scroll] ActivateSecondary: toggled OFF from left hand`
- [ ] Confirm hand is now empty

### 1.3 Cross-hand toggle (equip right, unequip via secondary)
- [ ] Equip scroll to RIGHT hand via primary
- [ ] Open wheel, select same scroll
- [ ] Secondary activate (RMB/LT) → scroll UNEQUIPS from right hand (not re-equip to left)
- [ ] **Verify log**: `[Scroll] ActivateSecondary: toggled OFF from right hand (was in other hand)`

### 1.4 Cross-hand toggle (equip left, unequip via primary)
- [ ] Equip scroll to LEFT hand via secondary
- [ ] Open wheel, select same scroll
- [ ] Primary activate (LMB/RT) → scroll UNEQUIPS from left hand (not re-equip to right)
- [ ] **Verify log**: `[Scroll] ActivatePrimary: toggled OFF from left hand (was in other hand)`

### 1.5 Scroll with occupied slot fallback
- [ ] Equip a spell/weapon in right hand
- [ ] Open wheel, select a scroll
- [ ] Primary activate (LMB/RT)
- [ ] **Verify log**: `[Scroll] ActivatePrimary: cleared occupied right slot`
- [ ] **Verify log**: `[Scroll] ActivatePrimary: equip result=SUCCESS`
- [ ] Confirm scroll is now in right hand, previous item cleared

## Test 2: Shovel Activation

### 2.1 ShovelBody shovel activation
- [ ] Add shovel to wheel
- [ ] Open wheel, select shovel entry
- [ ] Primary activate (LMB/RT)
- [ ] **Verify log**: `[ShovelItems] Queued shovel activation: formID=XXXXXXXX`
- [ ] **Verify log**: `[MiscItem] ProcessPendingActions: executing deferred activation for 'Shovel'`
- [ ] Confirm shovel menu/interaction triggers as expected

## Test 3: YPS Immersive Hair Activation

### 3.1 YPS item activation (brush, scissors, etc.)
- [ ] Add YPS item (e.g., Shaving Knife) to wheel
- [ ] Open wheel, select YPS item entry
- [ ] Primary activate (LMB/RT)
- [ ] **Verify log**: `[MiscItem] ProcessPendingActions: executing deferred activation`
- [ ] Confirm YPS menu opens correctly after wheel closes

## Test 4: Generic Misc Item Activation

### 4.1 Non-special misc item
- [ ] Add a generic misc item to wheel
- [ ] Open wheel, select misc item entry
- [ ] Primary activate (LMB/RT)
- [ ] **Verify log**: `[MiscItem] ProcessPendingActions: executing deferred activation`
- [ ] Confirm item activates correctly

## Test 5: Input Gating (DeniedHoldThreshold)

### 5.1 Short press vs long press in Toggle mode
- [ ] Set `ToggleHoldThreshold = 0.25` in config
- [ ] Tap toggle key quickly (<250ms) → wheel opens
- [ ] Tap toggle key quickly again → wheel closes
- [ ] Hold toggle key (>250ms) then release → wheel opens on press, closes on release
- [ ] **Verify**: No unexpected `DeniedHoldThreshold` blocking normal use

### 5.2 Activation during wheel open
- [ ] Open wheel
- [ ] Hover over an entry
- [ ] Click activate button
- [ ] **Verify**: Activation succeeds without `DeniedHoldThreshold`

## Test 6: Menu Blocking (DeniedMenuBlocked)

### 6.1 Wheel blocked when conflicting menu is open
- [ ] Open inventory menu
- [ ] Try to open wheel
- [ ] **Expected**: Wheel opens in edit mode (if enabled) OR blocks with log
- [ ] Close inventory
- [ ] Open wheel normally
- [ ] **Verify**: Wheel opens without blocking

### 6.2 Wheel not blocked for allowed menus
- [ ] Open HUD menu (normal gameplay)
- [ ] Open wheel
- [ ] **Verify**: Wheel opens normally

## Test 7: External API Callbacks

### 7.1 ItemActivated callback fires on activation
- [ ] Register external API callback (via test mod or debug)
- [ ] Activate an item via wheel
- [ ] **Verify**: Callback receives correct wheelIndex, entryIndex, itemIndex, formID, isPrimary

### 7.2 Callback fires for both equip and unequip
- [ ] Equip a scroll → callback fires with isPrimary=true
- [ ] Unequip same scroll → callback fires with isPrimary=true (toggle)

## Test 8: Edge Cases

### 8.1 Last scroll in inventory
- [ ] Have exactly 1 scroll
- [ ] Equip scroll to hand
- [ ] Unequip scroll via wheel
- [ ] **Verify**: Scroll unequips cleanly, no errors

### 8.2 Scroll consumed while equipped
- [ ] Equip scroll to hand
- [ ] Cast scroll (use it, consuming it)
- [ ] Open wheel, select same scroll entry
- [ ] **Verify**: Entry shows as unavailable or handles gracefully

## Acceptance Criteria

| Test | Expected Result | Pass/Fail |
|------|-----------------|-----------|
| 1.1 | Scroll equips then unequips via same button | |
| 1.2 | Scroll equips then unequips via same button | |
| 1.3 | Scroll unequips from right when secondary pressed | |
| 1.4 | Scroll unequips from left when primary pressed | |
| 1.5 | Occupied slot cleared, scroll equips | |
| 2.1 | Shovel menu triggers via deferred activation | |
| 3.1 | YPS menu opens after wheel closes | |
| 4.1 | Generic misc item activates | |
| 5.1 | No false DeniedHoldThreshold blocks | |
| 5.2 | Activation works during wheel open | |
| 6.1 | Wheel blocked appropriately for conflicting menus | |
| 7.1 | API callback fires with correct data | |
| 8.1 | Last scroll handles correctly | |

## Log Patterns to Verify

### Successful Scroll Equip
```
[Scroll] ActivatePrimary: equip result=SUCCESS formID=XXXXXXXX name='Scroll of ...'
```

### Successful Scroll Toggle-Off
```
[Scroll] ActivatePrimary: toggled OFF from right hand, formID=XXXXXXXX
```

### Cross-Hand Toggle-Off
```
[Scroll] ActivateSecondary: toggled OFF from right hand (was in other hand), formID=XXXXXXXX
```

### Shovel Deferred Activation
```
[ShovelItems] Queued shovel activation: formID=XXXXXXXX
[MiscItem] ProcessPendingActions: executing deferred activation for 'Shovel' formID=XXXXXXXX
[MiscItem] EquipObject dispatched for 'Shovel'
```

### YPS Deferred Activation
```
[MiscItem] ProcessPendingActions: executing deferred activation for 'Shaving Knife' formID=XXXXXXXX
```

## Files Modified in This Fix

| File | Changes |
|------|---------|
| `WheelItemScroll.cpp` | Fixed toggle logic to check BOTH hands, moved availability check after toggle |
| `WheelItemScroll.h` | Added `_formID` member, `IsInPlayerInventory()` override |
| `WheelItemMisc.cpp` | Added `ShovelItems` namespace, routed all misc items through deferred activation |
| `Wheeler.cpp` | Added diagnostic logging for deferred misc item activation |
| `ActionPolicy.h/cpp` | New policy system (unused in this fix but available) |
