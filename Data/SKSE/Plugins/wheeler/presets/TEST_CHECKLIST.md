# AmmoWheel Preset System - Test Checklist

## Pre-Test Setup
- [ ] Build project successfully (no compilation errors)
- [ ] Copy wheeler.dll to Data\SKSE\Plugins\
- [ ] Verify preset folders exist: presets\1\, presets\2\, presets\3\
- [ ] Each preset folder contains: AmmoWheel_Override.ini, Styles.ini, README.md

## Test Matrix

### Resolution Tests
- [ ] 1920x1080 (1080p) - Standard
- [ ] 2560x1440 (1440p) - High DPI
- [ ] Different UI scales (if applicable)

### Preset Selection Tests

#### Preset 0 (None/Custom)
- [ ] Set ActivePreset = 0 in AmmoWheel.ini
- [ ] Verify wheel uses base AmmoWheel.ini settings
- [ ] Verify no preset override is loaded (check logs)

#### Preset 1 (Skyrim Classic)
- [ ] Set ActivePreset = 1 in AmmoWheel.ini
- [ ] Verify arc slot shape
- [ ] Verify warm parchment/gold colors
- [ ] Verify gold border ring visible
- [ ] Verify slot shadows present
- [ ] Verify text shadows (2-layer)
- [ ] Verify hover pulse animation (gold)

#### Preset 2 (Modern Minimal)
- [ ] Set ActivePreset = 2 in AmmoWheel.ini
- [ ] Verify circle slot shape
- [ ] Verify clean gray/white colors
- [ ] Verify NO border ring (disabled)
- [ ] Verify NO slot shadows (flat design)
- [ ] Verify NO text shadows
- [ ] Verify minimal hover effect

#### Preset 3 (Dark Medieval)
- [ ] Set ActivePreset = 3 in AmmoWheel.ini
- [ ] Verify arc slot shape
- [ ] Verify iron gray/dark leather colors
- [ ] Verify iron-colored border ring
- [ ] Verify deep slot shadows
- [ ] Verify heavy text shadows (3-layer)
- [ ] Verify slower hover pulse

### dMenu Integration
- [ ] Open dMenu > Wheeler: Ammo Wheel
- [ ] Verify "Visual Presets" group appears
- [ ] Verify dropdown shows all 4 options
- [ ] Change preset via dropdown
- [ ] Verify changes apply after closing dMenu

### Visual Quality Tests

#### Readability
- [ ] Labels readable on all slot backgrounds
- [ ] Count text clearly visible
- [ ] Popup text readable
- [ ] No text clipping or overflow

#### Visual Hierarchy
- [ ] Hover state clearly distinguishable
- [ ] Selected/equipped state obvious
- [ ] Popup feels like a layer above wheel

#### Geometry
- [ ] Wheel is medium-sized (not too small/large)
- [ ] Icons optically centered in slots
- [ ] Comfortable selection space between slots

### Edge Cases

#### Long Names
- [ ] Test with "Bloodcursed Elven Arrow" (long name)
- [ ] Verify truncation/wrapping works
- [ ] Label background adjusts to text

#### Extreme Counts
- [ ] Test with count = 1
- [ ] Test with count = 999
- [ ] Test with count = 9999
- [ ] Count text doesn't overflow

#### Arrow vs Bolt
- [ ] Test with arrows equipped
- [ ] Test with bolts equipped
- [ ] Both types render correctly

### Fallback Behavior

#### Missing Assets
- [ ] Delete a preset's Styles.ini temporarily
- [ ] Verify no CTD
- [ ] Verify falls back to primitive rendering
- [ ] Restore file after test

#### Invalid Preset Number
- [ ] Set ActivePreset = 99 in INI
- [ ] Verify no CTD
- [ ] Verify falls back to preset 0 (custom)

### Animation Tests
- [ ] PopupBubble flipbook plays smoothly
- [ ] Hover pulse animates correctly
- [ ] Center frame pulse (if enabled)
- [ ] No animation stuttering

### Performance
- [ ] No noticeable FPS drop when wheel open
- [ ] Smooth slot transitions
- [ ] No texture loading hitches

## Log Verification
Enable debug logging and verify:
- [ ] "Loading preset X override from..." appears
- [ ] "Preset X loaded successfully" appears
- [ ] No error/warning messages related to presets

## Sign-Off
- [ ] All tests passed
- [ ] Date: ___________
- [ ] Tester: ___________
