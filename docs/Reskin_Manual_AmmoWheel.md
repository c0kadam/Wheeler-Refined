# AmmoWheel Reskin Manual

## Flipbook Verdict

| Feature | Supported | Evidence |
|---------|-----------|----------|
| **Flipbook Popup Animations** | **YES** | `AmmoWheelReskinUnified.cpp:282-289` loads `_IsFlipbook`, `_FrameCount`, `_FPS` keys |
| **Static PNG Overlays** | **YES** | `AmmoWheelReskinUnified.cpp:291-292` loads `_Path` for static assets |
| **Indicator Flipbooks** | **YES** | `AmmoWheel_Styles.ini:177-182` documents `IndicatorHovered_IsFlipbook` |

**Requirements to activate flipbook animations:**
1. Set `[Skin] UsePresetStyles = true` in `AmmoWheel.ini`
2. Create a preset in `AmmoWheel_Styles.ini` with flipbook keys
3. Place numbered PNG frames in the correct folder

---

## Quick Start (5 Minutes)

### Step 1: Enable the Reskin System
Edit `Data\SKSE\Plugins\wheeler\AmmoWheel.ini`:
```ini
[Skin]
UseAmmoWheelStylesIni = true
UsePresetStyles = true
```

### Step 2: Create a Simple Color Reskin
Edit `Data\SKSE\Plugins\wheeler\resources\ammo_wheel\Styles.ini`:
```ini
[Preset.Default]
Slot.UnhoveredColorBegin = 0x80FF0000
Slot.UnhoveredColorEnd = 0x40800000
Slot.HoveredColorBegin = 0xFFFF4040
Slot.HoveredColorEnd = 0xFFCC2020
```

### Step 3: Verify
1. Launch Skyrim
2. Equip a bow
3. Open the ammo wheel (default: Left Shift)
4. Slots should now have red tint

---

## Configuration Files Overview

### 1. AmmoWheel.ini (Layout & Behavior)
**Path:** `Data\SKSE\Plugins\wheeler\AmmoWheel.ini`

Controls:
- Wheel position, size, shape
- Slot geometry and gaps
- Navigation feel (deadzones, smoothing)
- Feature toggles (popup, center panel, indicators)
- Input bindings
- Theme flags (`UseSkyrimTheme`, `UseMainWheelTheme`)

**Key sections for reskinning:**
```ini
[Skin]
UseAmmoWheelStylesIni = true   ; Load Styles.ini
UsePresetStyles = true          ; Enable preset system (REQUIRED for PNG assets)

[IconSystem]
UseDedicatedIconFolder = true   ; Use ammo_wheel/icons/ folder
```

### 2. Styles.ini (Visual Skin Definition)
**Path:** `Data\SKSE\Plugins\wheeler\resources\ammo_wheel\Styles.ini`

Controls:
- Slot colors (gradients)
- Text colors and shadows
- Icon placement and rotation
- Indicator styles (shape, thickness, colors, animation)
- Style presets for different ammo types

**Key sections:**
- `[AmmoWheel.Slot]` - Slot geometry and colors
- `[AmmoWheel.Text]` - Text styling
- `[AmmoWheel.Icon]` - Icon placement and rotation
- `[AmmoWheel.Indicator.Selected]` - Equipped ammo indicator
- `[AmmoWheel.Indicator.Hovered]` - Hover indicator
- `[Preset.PresetId]` - Named style presets

### 3. AmmoWheel_Styles.ini (PNG Asset Presets)
**Path:** `Data\SKSE\Plugins\wheeler\resources\ammo_wheel\AmmoWheel_Styles.ini`

Controls:
- PNG/flipbook asset definitions
- Per-target asset paths (SlotIcon, SlotBackground, Popup, etc.)
- Flipbook frame counts and FPS

### 4. AMMO_KID.ini (Ammo-to-Preset Mapping)
**Path:** `Data\SKSE\Plugins\wheeler\resources\ammo_wheel\AMMO_KID.ini`

Maps specific ammo to presets by:
- FormID (exact match)
- Keyword (pattern match)
- Type (Arrow/Bolt default)

### 5. dMenu Panel
**Path:** `Data\SKSE\Plugins\dmenu\customSettings\Ammo Wheel.json`

User-facing UI for adjusting settings. Changes are written to `AmmoWheel.ini`.

**When changes apply:**
- **Auto Save enabled:** Changes apply immediately when you close dMenu
- **Auto Save disabled:** Click "Save" button to apply
- **Live settings:** Most visual settings apply immediately
- **Require wheel reopen:** Some navigation settings

---

## Folder Structure

```
Data\SKSE\Plugins\wheeler\
├── AmmoWheel.ini                    # Main config
├── resources\
│   └── ammo_wheel\
│       ├── Styles.ini               # Visual skin definition
│       ├── AmmoWheel_Styles.ini     # PNG asset presets
│       ├── AMMO_KID.ini             # Ammo-to-preset mapping
│       ├── README.txt               # Asset documentation
│       ├── icons\                   # Default icons
│       │   ├── arrow.svg
│       │   └── bolt.svg
│       ├── icons_custom\            # Custom icon replacers
│       ├── slot_bg\                 # Slot background PNGs
│       ├── slot_frame\              # Slot frame/border PNGs
│       ├── wheel_bg\                # Wheel background PNGs
│       ├── center_bg\               # Center panel background PNGs
│       ├── popup\                   # Popup overlay PNGs (flipbook frames)
│       ├── indicators\              # Indicator overlay PNGs
│       └── skins\                   # Complete skin packages
│           └── template\            # Example skin template
```

---

## Minimum Viable Reskin Checklist

### Level 1: Colors Only (No Assets)
1. Edit `Styles.ini`
2. Change `[AmmoWheel.Slot]` colors
3. Change `[AmmoWheel.Indicator.*]` colors
4. Done - no PNG files needed

### Level 2: Colors + Static Textures
1. Complete Level 1
2. Create PNG files in appropriate folders
3. Edit `AmmoWheel_Styles.ini`:
```ini
[Preset_Default]
SlotBackground_Enabled = true
SlotBackground_Path = slot_bg/my_background.png
SlotBackground_Alpha = 0.9
```
4. Set `[Skin] UsePresetStyles = true` in `AmmoWheel.ini`

### Level 3: Full Reskin with Flipbook Animations
1. Complete Level 2
2. Create numbered PNG frames: `popup/ring_00.png` through `popup/ring_15.png`
3. Edit `AmmoWheel_Styles.ini`:
```ini
[Preset_Default]
Popup_Enabled = true
Popup_IsFlipbook = true
Popup_Path = popup/ring_{:02}.png
Popup_FrameCount = 16
Popup_FPS = 24.0
Popup_Loop = true
```

---

## Visual Targets (What Can Be Reskinned)

| Target | Description | Asset Key Prefix |
|--------|-------------|------------------|
| SlotIcon | Icon displayed in each slot | `SlotIcon_` |
| SlotBackground | Background behind each slot | `SlotBackground_` |
| SlotFrame | Border/frame around each slot | `SlotFrame_` |
| WheelBackground | Background behind entire wheel | `WheelBackground_` |
| CenterBackground | Background for center info panel | `CenterBackground_` |
| Popup | Hover popup overlay | `Popup_` |
| IndicatorSelected | Equipped ammo indicator | `IndicatorSelected_` |
| IndicatorHovered | Hovered slot indicator | `IndicatorHovered_` |
| IndicatorActive | Activation indicator | `IndicatorActive_` |
| IndicatorCharge | RTU charge indicator | `IndicatorCharge_` |

---

## Flipbook Animation Format

### File Naming
Use numbered frames with zero-padding:
```
popup/ring_00.png
popup/ring_01.png
popup/ring_02.png
...
popup/ring_15.png
```

### INI Configuration
```ini
[Preset_MyAnimation]
Popup_Enabled = true
Popup_IsFlipbook = true
Popup_Path = popup/ring_{:02}.png    ; {:<width>} = frame number placeholder
Popup_FrameCount = 16                 ; Total frames (0 to FrameCount-1)
Popup_FPS = 24.0                      ; Frames per second (1-60)
Popup_Loop = true                     ; Loop animation
Popup_Alpha = 1.0                     ; Opacity multiplier
```

### Pattern Format
- `{:02}` = 2-digit zero-padded (00, 01, 02...)
- `{:03}` = 3-digit zero-padded (000, 001, 002...)
- `{:d}` = no padding (0, 1, 2...)

---

## Icon Replacers (Known Working Pattern)

Wheeler supports custom icon replacement via the `icons_custom` folder. This is a proven pattern documented in Wheeler's official article.

### Naming Convention
| Pattern | Example | Matches |
|---------|---------|---------|
| `KWD_<keyword>.svg` | `KWD_ArrowDaedric.svg` | Ammo with keyword "ArrowDaedric" |
| `FID_<formid>.svg` | `FID_0001397D.svg` | Ammo with FormID 0x0001397D |

### Folder
`Data\SKSE\Plugins\wheeler\resources\ammo_wheel\icons_custom\`

### Priority
1. FormID match (`FID_*.svg`)
2. Keyword match (`KWD_*.svg`)
3. Default icon (`arrow.svg` or `bolt.svg`)

---

## Verification

### What Should Change
| Setting | Visual Effect |
|---------|---------------|
| `Slot.UnhoveredColorBegin/End` | Slot gradient when not hovered |
| `Slot.HoveredColorBegin/End` | Slot gradient when hovered |
| `Indicator.Selected.ColorBegin/End` | Equipped ammo ring color |
| `BackgroundOpacity` | Wheel backdrop transparency |
| `BorderEnabled` | Gold/bronze border ring visibility |
| `Popup_Enabled = true` | PNG overlay on hover popup |

### Log File Location
`Documents\My Games\Skyrim Special Edition\SKSE\wheeler.log`

### Log Lines to Look For
**Success:**
```
[AmmoWheelReskin] Loaded preset: Default
[AmmoWheelReskin] Loaded texture: popup/ring_00.png (256x256)
```

**Failure:**
```
[AmmoWheelReskin] Failed to load texture: popup/ring_00.png
[AmmoWheelReskin] Preset not found: MyPreset
```

### Debug Overlay
Enable in `AmmoWheel.ini`:
```ini
[Debug]
ShowReskinOverlay = true
LogPresetResolution = true
LogAssetLoading = true
```

---

## Troubleshooting Matrix

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| PNGs in Popup folder do nothing | `UsePresetStyles = false` | Set `[Skin] UsePresetStyles = true` |
| PNGs in Popup folder do nothing | Preset not enabled | Add `Popup_Enabled = true` to preset |
| PNGs in Popup folder do nothing | Wrong path format | Use `popup/name.png` not `popup\name.png` |
| Background opacity slider does nothing | Theme overriding | `BackgroundOpacity` is a multiplier on theme alpha |
| Border ring disappears | `BorderEnabled = false` | Set `[VisualPolish] BorderEnabled = true` |
| Border ring disappears | `BorderInnerScale >= BorderOuterScale` | Ensure Inner < Outer |
| Popup background opacity ignored | Enhanced animation disabled | Set `[Popup.Animation] Enabled = true` |
| Colors don't match INI | Theme flags active | Disable `UseSkyrimTheme` and `UseMainWheelTheme` |
| Flipbook not animating | FPS too low | Increase `_FPS` value |
| Flipbook not animating | FrameCount wrong | Verify actual frame count matches INI |
| Icon not showing | Path incorrect | Check relative path from `resources\ammo_wheel\` |
| dMenu changes don't apply | Auto Save off | Click Save button in dMenu |

### MO2/Vortex Priority Issues
- **Left pane order matters:** Mods lower in the list override higher ones
- **Overwrite folder:** Check for conflicting files in MO2's Overwrite
- **Loose files:** Loose files always override BSA-packed files
- **Verify paths:** Use MO2's "Data" tab to see final merged file structure

---

## Packaging a Reskin Mod

### Recommended Structure
```
MyAmmoWheelReskin\
├── Data\
│   └── SKSE\
│       └── Plugins\
│           └── wheeler\
│               └── resources\
│                   └── ammo_wheel\
│                       ├── Styles.ini           # Your colors
│                       ├── AmmoWheel_Styles.ini # Your asset presets
│                       ├── popup\               # Your popup PNGs
│                       ├── slot_bg\             # Your slot backgrounds
│                       └── ...
```

### Separation Strategy
- **Skin mod:** Contains `Styles.ini`, `AmmoWheel_Styles.ini`, and PNG assets
- **User config mod (optional):** Contains only `AmmoWheel.ini` with user preferences
- This allows users to mix skins with their own layout preferences

### Compatibility
- **Override order:** Your skin should load after Wheeler base
- **Partial overrides:** You can include only the files you want to change
- **Fallback:** Missing assets fall back to primitive rendering (no crash)

---

## Asset Specifications

### Recommended Sizes
| Asset Type | Recommended Size | Notes |
|------------|------------------|-------|
| Slot Icon | 64x64 to 128x128 | Square, transparent background |
| Slot Background | 256x256 | Will be stretched to fit arc |
| Popup Overlay | 256x256 to 512x512 | Centered on popup |
| Wheel Background | 512x512 to 1024x1024 | Centered on wheel |
| Indicator | 64x64 to 128x128 | Thin arc or ring shape |

### Format
- **PNG** with transparency (RGBA)
- **SVG** for icons (vector, scales cleanly)

### Performance Tips
- Keep textures ≤512px for most elements
- Limit flipbook frames to 8-24
- Use 12-16 FPS for subtle animations
- Total texture memory limit: 256MB (configurable in code)

---

## Example: Complete Popup Ring Animation

### Step 1: Create Frames
Create 16 PNG files (256x256 each):
```
Data\SKSE\Plugins\wheeler\resources\ammo_wheel\popup\
├── glow_00.png
├── glow_01.png
├── glow_02.png
...
└── glow_15.png
```

### Step 2: Configure Preset
Edit `AmmoWheel_Styles.ini`:
```ini
[Preset_Default]
Name = Animated Glow Ring

; Disable other targets (use primitives)
SlotIcon_Enabled = false
SlotBackground_Enabled = false
SlotFrame_Enabled = false
WheelBackground_Enabled = false
CenterBackground_Enabled = false

; Enable animated popup
Popup_Enabled = true
Popup_IsFlipbook = true
Popup_Path = popup/glow_{:02}.png
Popup_FrameCount = 16
Popup_FPS = 20.0
Popup_Loop = true
Popup_Alpha = 0.8
```

### Step 3: Enable System
Edit `AmmoWheel.ini`:
```ini
[Skin]
UseAmmoWheelStylesIni = true
UsePresetStyles = true
```

### Step 4: Test
1. Launch Skyrim
2. Equip bow, open ammo wheel
3. Hover over a slot
4. Popup should show animated glow ring

---

## Legacy Compatibility

The reskin system is designed for backward compatibility:

- **`[Skin] UsePresetStyles = false`:** All PNG/flipbook features disabled, pure primitive rendering
- **Missing assets:** Fall back to primitive rendering (no crash)
- **Missing presets:** Fall back to `[Preset_Default]`
- **Old configs:** Continue to work; new keys have sensible defaults

---

*Manual version: 1.0 | Last updated: December 2024*
