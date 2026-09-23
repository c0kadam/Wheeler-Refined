# AmmoWheel Unified Reskin System - Canonical Guide

## Overview

The AmmoWheel visual system has evolved through multiple implementations. This guide documents the **current unified system** and explains how all configuration files work together.

## Critical Finding: Why Your Popup Flipbook Doesn't Show

### Root Cause
Your popup flipbook (`popup/blood_mist_00.png` through `blood_mist_49.png`) is NOT showing because:

1. **Preset Mismatch**: The `[Preset_Default]` section in `AmmoWheel_Styles.ini` references `PopupBubble_Path = popup/universal_{:02}.png` but those files don't exist.
2. **Mapping Issue**: Your Bloodcursed arrow is mapped to `BloodcursedElvenArrow` preset which correctly points to `popup/blood_mist_{:02}.png`, but if that preset isn't found, it falls back to `Default` which expects non-existent `universal_XX.png` files.

### Fix
**Option A**: Change Default preset to use your existing assets:
```ini
; In AmmoWheel_Styles.ini [Preset_Default]
PopupBubble_Path = popup/blood_mist_{:02}.png
PopupBubble_FrameCount = 50
```

**Option B**: Create the missing `popup/universal_XX.png` files (30 frames needed).

**Option C**: Map all arrows to existing presets in `AMMO_KID.ini`.

---

## File Hierarchy and Ownership

```
wheeler/
├── AmmoWheel.ini          # MASTER CONFIG - controls reskin enable, geometry, behavior
├── wheelBehavior.ini      # Release-to-Use and wheel behavior settings
└── resources/ammo_wheel/
    ├── AmmoWheel_Reskin.ini   # DUPLICATE of AmmoWheel.ini [Reskin] section (LEGACY)
    ├── AmmoWheel_Styles.ini   # PRESET DEFINITIONS - visual presets with [Preset_X]
    ├── AMMO_KID.ini           # MAPPINGS - FormID/Keyword to preset mapping
    ├── Styles.ini             # LEGACY PRESETS - old format with [Preset.X] (IGNORED)
    └── skins/
        ├── default/skin.ini   # NOT ACTIVE unless UseAmmoWheelStylesIni=true
        └── template/          # Template for creating new skins
```

### Which Files Are Actually Used (Code Evidence)

| File | Used By | Code Reference |
|------|---------|----------------|
| `AmmoWheel.ini` | `ReskinSystem::LoadConfig()` | `AmmoWheelReskinUnified.cpp:176-238` |
| `AmmoWheel_Styles.ini` | `ReskinSystem::LoadPresets()` | `AmmoWheelReskinUnified.cpp:240-332` |
| `AMMO_KID.ini` | `ReskinSystem::LoadMappings()` | `AmmoWheelReskinUnified.cpp:335-378` |
| `Styles.ini` | **LEGACY - NOT USED** by unified system | Only read if `UseAmmoWheelStylesIni=true` |
| `skins/*/skin.ini` | **NOT ACTIVE** | Requires `UseAmmoWheelStylesIni=true` |

---

## The Unified Reskin System

### Enable/Disable
```ini
; In AmmoWheel.ini
[Reskin]
Enabled = true    ; Master switch for unified system
```

When `Enabled = true`:
- Unified system takes over ALL visual rendering
- Reads presets from `AmmoWheel_Styles.ini`
- Reads mappings from `AMMO_KID.ini`
- Legacy systems (`UseSkyrimTheme`, `UseMainWheelTheme`, etc.) are deprecated

When `Enabled = false`:
- Falls back to primitive rendering
- Uses colors from `[Primitives]` section or theme settings

### Preset Resolution Chain

When drawing an ammo slot, the system resolves which preset to use:

```
1. FormID Mapping     → AMMO_KID.ini [FormIDPresets]
   ↓ (if not found)
2. Keyword Mapping    → AMMO_KID.ini [KeywordPresets]
   ↓ (if not found)
3. Type Default       → AmmoWheel.ini [Reskin] ArrowDefaultPreset/BoltDefaultPreset
   ↓ (if not found)
4. Fallback          → "Default" preset
```

**Code Reference**: `ReskinSystem::ResolveForAmmo()` at `AmmoWheelReskinUnified.cpp:381-438`

---

## Visual Targets

Each preset can customize these 11 visual elements:

| Target | Description | Typical Use |
|--------|-------------|-------------|
| `SlotIcon` | Icon in wheel slot | Arrow/bolt image |
| `SlotBackground` | Background behind slot | Texture overlay |
| `SlotFrame` | Border/frame on slot | Decorative border |
| `IndicatorSelected` | Equipped indicator | Golden arc |
| `IndicatorHovered` | Hover indicator | Highlight arc |
| `IndicatorActive` | Activation indicator | Green flash |
| `IndicatorCharge` | RTU charge indicator | Progress arc |
| `Popup` | Hover overlay INSIDE wheel | Slot glow |
| `PopupBubble` | Tooltip OUTSIDE wheel | **Your flipbook goes here** |
| `WheelBackground` | Background circle | Dark backdrop |
| `CenterBackground` | Center panel background | Info panel bg |

### Popup vs PopupBubble (Critical Distinction)

- **Popup**: Effect drawn at the SLOT position when hovered (inside the wheel)
- **PopupBubble**: The tooltip/info panel drawn OUTSIDE the wheel

Your flipbook animation should use `PopupBubble_*` settings, not `Popup_*`.

---

## Preset Definition Format

Presets are defined in `AmmoWheel_Styles.ini`:

```ini
[Preset_MyPresetName]
Name = Display Name
Description = Optional description

; PopupBubble flipbook animation
PopupBubble_Enabled = true
PopupBubble_IsFlipbook = true
PopupBubble_Path = popup/my_animation_{:02}.png
PopupBubble_FrameCount = 30
PopupBubble_FPS = 24.0
PopupBubble_Loop = true
PopupBubble_Alpha = 0.85

; Other targets (disabled = use primitive)
SlotIcon_Enabled = false
SlotBackground_Enabled = false
; ... etc
```

### Asset Path Format

- Paths are relative to `resources/ammo_wheel/`
- Flipbook pattern uses `{:02}` for frame numbers (00, 01, 02...)
- Example: `popup/blood_mist_{:02}.png` → `popup/blood_mist_00.png`, `popup/blood_mist_01.png`, ...

---

## Mapping Ammo to Presets

In `AMMO_KID.ini`:

```ini
[FormIDPresets]
; Format: <HexFormID> = <PresetName>
020098A0 = BloodcursedElvenArrow
0003BE11 = IronArrow

[KeywordPresets]
; Format: <KeywordEditorID> = <PresetName>
DLC1BloodcursedArrow = BloodcursedElvenArrow
```

### Finding FormIDs
1. Enable debug logging: `[Debug] LogPresetResolution = true`
2. Hover over arrow in-game
3. Check `wheeler.log` for: `Resolved preset for <ArrowName> (FormID: XXXXXXXX)`

---

## Troubleshooting Checklist

### Flipbook Not Showing

1. **Check [Reskin] Enabled = true** in `AmmoWheel.ini`
2. **Verify preset exists**: Section must be `[Preset_YourName]` (with underscore)
3. **Verify mapping exists**: Check `AMMO_KID.ini` uses `[FormIDPresets]` not `[FormID]`
4. **Verify files exist**: All PNG frames must exist with correct naming
5. **Check log for errors**: Look for `[AmmoWheelReskinUnified]` messages

### Log Messages to Check

```
[AmmoWheelReskinUnified] Initialized (enabled=true, basePath=...)
[AmmoWheelReskinUnified] Loaded preset: BloodcursedElvenArrow
[AmmoWheelReskinUnified] Loaded 15 FormID mappings, 1 Keyword mappings
[AmmoWheel] Resolved preset for Bloodcursed Elven Arrow: BloodcursedElvenArrow via FormID
[AmmoWheel] PopupBubble texture drawn at (X, Y) radius=R
```

### Common Errors

| Log Message | Cause | Fix |
|-------------|-------|-----|
| `Loaded 0 FormID mappings` | Wrong section name | Use `[FormIDPresets]` not `[FormID]` |
| `AmmoWheel_Styles.ini not found` | Wrong base path | Check file location |
| `Failed to decode: path` | Corrupted PNG | Re-export PNG files |
| `File too large` | PNG > 8MB | Reduce resolution or file size |

---

## Legacy Systems (Deprecated)

These systems are **deprecated** when `[Reskin] Enabled = true`:

| Setting | Status | Replacement |
|---------|--------|-------------|
| `UseSkyrimTheme` | DEPRECATED | Use `[Primitives]` colors |
| `UseMainWheelTheme` | DEPRECATED | Use `[Primitives]` colors |
| `UseAmmoWheelStylesIni` | DEPRECATED | Use unified presets |
| `UsePresetStyles` | DEPRECATED | Use unified presets |
| `Styles.ini [Preset.X]` | IGNORED | Use `AmmoWheel_Styles.ini [Preset_X]` |
| `skins/*/skin.ini` | NOT ACTIVE | Future feature |

---

## Quick Setup Recipes

### Recipe 1: Single Universal Animation for All Arrows

```ini
; AmmoWheel.ini
[Reskin]
Enabled = true
ArrowDefaultPreset = Default
BoltDefaultPreset = Default

; AmmoWheel_Styles.ini
[Preset_Default]
PopupBubble_Enabled = true
PopupBubble_IsFlipbook = true
PopupBubble_Path = popup/blood_mist_{:02}.png
PopupBubble_FrameCount = 50
PopupBubble_FPS = 30.0
PopupBubble_Loop = true
PopupBubble_Alpha = 0.85
```

### Recipe 2: Unique Animation per Arrow Type

```ini
; AMMO_KID.ini
[FormIDPresets]
0003BE11 = IronArrow
000139BA = DaedricArrow
020098A0 = BloodcursedElvenArrow

; AmmoWheel_Styles.ini
[Preset_IronArrow]
PopupBubble_Enabled = true
PopupBubble_Path = popup/iron_spark_{:02}.png
PopupBubble_FrameCount = 20

[Preset_DaedricArrow]
PopupBubble_Enabled = true
PopupBubble_Path = popup/daedric_fire_{:02}.png
PopupBubble_FrameCount = 40
```

### Recipe 3: Disable All Animations (Primitive Only)

```ini
; AmmoWheel.ini
[Reskin]
Enabled = false
```

---

## Code References

| Function | File:Line | Purpose |
|----------|-----------|---------|
| `ReskinSystem::Init()` | `AmmoWheelReskinUnified.cpp:105-127` | System initialization |
| `ReskinSystem::LoadConfig()` | `AmmoWheelReskinUnified.cpp:176-238` | Read AmmoWheel.ini |
| `ReskinSystem::LoadPresets()` | `AmmoWheelReskinUnified.cpp:240-332` | Read AmmoWheel_Styles.ini |
| `ReskinSystem::LoadMappings()` | `AmmoWheelReskinUnified.cpp:335-378` | Read AMMO_KID.ini |
| `ReskinSystem::ResolveForAmmo()` | `AmmoWheelReskinUnified.cpp:381-438` | Preset resolution |
| `ReskinSystem::DrawTarget()` | `AmmoWheelReskinUnified.cpp:440-467` | Draw texture/flipbook |
| `ReskinSystem::GetFlipbookFrame()` | `AmmoWheelReskinUnified.cpp:643-715` | Flipbook frame selection |
| `AmmoWheel::drawHoverPopup()` | `AmmoWheel.cpp:3592-3730` | PopupBubble rendering |

---

## Summary

1. **Master Enable**: `AmmoWheel.ini [Reskin] Enabled = true`
2. **Define Presets**: `AmmoWheel_Styles.ini [Preset_X]` sections
3. **Map Ammo**: `AMMO_KID.ini [FormIDPresets]` and `[KeywordPresets]`
4. **Assets**: Place PNGs in `resources/ammo_wheel/popup/`
5. **Debug**: Enable `LogPresetResolution` and `LogAssetLoading`

The unified system replaces all legacy visual mechanisms. When in doubt, check the log for `[AmmoWheelReskinUnified]` messages.
