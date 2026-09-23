# ActionPolicy System - Test Plan

## Overview
This document provides a repeatable test checklist for the ActionPolicy activation system implemented in Wheeler. The system provides category-driven activation policies with fallback chains and post-condition verification.

## Prerequisites
1. Enable debug logging: Set `LogActionPolicy = true` in `wheelBehavior.ini` under `[Debug]`
2. Have a variety of items in inventory: weapons, shields, scrolls, potions, books, spells
3. Open Wheeler console log to monitor activation flow

## Test Checklist

### 1. Weapons
- [ ] **Equip to right hand (empty)**: LMB on weapon → equips to right hand
- [ ] **Equip to left hand (empty)**: RMB on weapon → equips to left hand
- [ ] **Toggle off from right**: LMB on equipped-right weapon → unequips
- [ ] **Toggle off from left**: RMB on equipped-left weapon → unequips
- [ ] **Swap while hands occupied**: Equip different weapon → replaces current
- [ ] **Two-handed weapon**: Equip bow/2H sword → clears both hands, equips properly

### 2. Shields
- [ ] **Equip to left hand**: Any activation on shield → equips to left hand
- [ ] **Toggle off**: Activate equipped shield → unequips
- [ ] **Weapon stays in right**: Equip shield while weapon in right → weapon remains

### 3. Armor
- [ ] **Equip armor piece**: Activate on armor → equips to correct slot
- [ ] **Toggle off**: Activate equipped armor → unequips
- [ ] **Slot conflict handling**: System handles appropriately

### 4. Potions/Food
- [ ] **Use potion**: Activate → consumes, count decreases by 1
- [ ] **Use food**: Activate → consumes, count decreases by 1
- [ ] **Last item handling**: Use last potion → slot cleared (if ClearDepletedConsumables=true)
- [ ] **Empty slot handling**: No crash on empty slot

### 5. Books
- [ ] **Read book**: Activate → opens book menu after wheel closes
- [ ] **Book is NOT treated as scroll**: Verify no equip attempt

### 6. Scrolls (Primary Focus)
- [ ] **Equip to right hand (empty)**: LMB on scroll → equips to right hand
- [ ] **Equip to left hand (empty)**: RMB on scroll → equips to left hand
- [ ] **Toggle off from right**: LMB on equipped-right scroll → unequips
- [ ] **Toggle off from left**: RMB on equipped-left scroll → unequips
- [ ] **Equip to right when occupied**:
  - Have weapon in right hand
  - LMB on scroll
  - → Weapon cleared, scroll equipped to right
  - Verify log shows: "cleared occupied right slot"
- [ ] **Equip to left when occupied**:
  - Have weapon/shield in left hand
  - RMB on scroll
  - → Item cleared, scroll equipped to left
  - Verify log shows: "cleared occupied left slot"
- [ ] **Scroll never opens as book**: Activate scroll multiple times → never opens book UI
- [ ] **Post-condition verification**: Check log shows "equip result=SUCCESS"

### 7. Ammo
- [ ] **Equip ammo**: Activate → equips as current ammo
- [ ] **Swap ammo types**: Equip different ammo → changes active ammo

### 8. Spells
- [ ] **Equip to right hand**: LMB on spell → equips to right hand
- [ ] **Equip to left hand**: RMB on spell → equips to left hand
- [ ] **Toggle behavior**: Activate equipped spell → unequips

### 9. Powers/Shouts
- [ ] **Equip power**: Activate power → equips to voice slot
- [ ] **Toggle power off**: Activate equipped power → clears voice slot
- [ ] **Equip shout**: Activate shout → equips to voice slot

### 10. Lights (Torches)
- [ ] **Equip torch**: Activate → equips to left hand
- [ ] **Toggle off**: Activate equipped torch → unequips

### 11. Misc Items
- [ ] **Activate misc**: Triggers activation (Papyrus scripts)
- [ ] **SGT instruments**: Flute/Drum/Lute work correctly (queued action)

## Debug Log Verification

When `LogActionPolicy = true`, verify these log patterns appear:

### Scroll Activation Example
```
[Scroll] ActivatePrimary: cleared occupied right slot, formID=XXXXXXXX
[Scroll] ActivatePrimary: equip result=SUCCESS formID=XXXXXXXX name='Fire Storm Scroll'
```

### ActionPolicy Flow (if using ExecuteEntry)
```
[ActionPolicy] ACTIVATE START form=XXXXXXXX type=XX category=Scroll name='...' hand=Right intent=0
[ActionPolicy] TRY action=EquipRight hand=R occupied=true
[ActionPolicy] RESULT action=EquipRight success=false reason=''
[ActionPolicy] TRY action=UnequipRight hand=R occupied=true
[ActionPolicy] RESULT action=UnequipRight success=true
[ActionPolicy] TRY action=EquipRight hand=R occupied=false
[ActionPolicy] RESULT action=EquipRight success=true
[ActionPolicy] ACTIVATE END success=true finalAction=EquipRight attempts=3 succeeded=2
```

## Acceptance Criteria

1. **Scroll activation always equips scroll to a hand** using fallback chain when needed
2. **No false UI success updates** when equip/use fails
3. **Existing working behaviors unchanged** for weapons, armor, potions, spells
4. **Logs clearly show** classification, attempted actions, and success checks
5. **Post-condition verification** confirms game state after each action
6. **Toggle behavior works correctly** for equippable items

## Known Limitations

1. Book read verification is best-effort (menu detection is limited)
2. Poison application opens weapon selection menu - queued for post-wheel-close
3. Some consumable verification relies on count decrease assumption

## Rollback

If issues occur:
1. Set `LogActionPolicy = false` to disable logging
2. The ActionPolicy system is additive - core WheelItem activation remains unchanged
3. Scroll-specific improvements can be reverted by removing the slot-clearing logic

## Files Modified

- `src/bin/Wheeler/ActionPolicy.h` - New: Policy system header
- `src/bin/Wheeler/ActionPolicy.cpp` - New: Policy system implementation
- `src/bin/Wheeler/WheelItems/WheelItemScroll.h` - Updated: Added formID caching
- `src/bin/Wheeler/WheelItems/WheelItemScroll.cpp` - Updated: Fallback chains + logging
- `src/bin/Config.h` - Updated: Added LogActionPolicy config
- `src/bin/Config.cpp` - Updated: Read LogActionPolicy from INI
- `Data/SKSE/Plugins/wheeler/wheelBehavior.ini` - Updated: Added LogActionPolicy option
