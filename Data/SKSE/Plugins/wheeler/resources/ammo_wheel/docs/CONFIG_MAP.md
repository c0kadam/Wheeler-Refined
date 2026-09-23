# AmmoWheel Configuration Map

## Settings Map Table

### AmmoWheel.ini - [Reskin] Section

| Setting | Section.Key | Default | Range | Units | Code Read | Code Apply | Dependencies | Conflicts |
|---------|-------------|---------|-------|-------|-----------|------------|--------------|-----------|
| Master Enable | Reskin.Enabled | false | true/false | bool | `AmmoWheelReskinUnified.cpp:189` | `AmmoWheelReskinUnified.cpp:116` | None | Overrides all legacy theme settings |
| Arrow Default | Reskin.ArrowDefaultPreset | "Default" | preset name | string | `AmmoWheelReskinUnified.cpp:206` | `AmmoWheelReskinUnified.cpp:425` | Reskin.Enabled=true | None |
| Bolt Default | Reskin.BoltDefaultPreset | "Default" | preset name | string | `AmmoWheelReskinUnified.cpp:207` | `AmmoWheelReskinUnified.cpp:425` | Reskin.Enabled=true | None |
| Base Path | Reskin.BasePath | `resources\ammo_wheel` | path | string | `AmmoWheelReskinUnified.cpp:190-191` | `AmmoWheelReskinUnified.cpp:242,337,455,671` | Reskin.Enabled=true | None |

### AmmoWheel.ini - [Safety] Section

| Setting | Section.Key | Default | Range | Units | Code Read | Code Apply | Dependencies | Conflicts |
|---------|-------------|---------|-------|-------|-----------|------------|--------------|-----------|
| Max Frames | Safety.MaxTotalFrames | 256 | 1-1000 | count | `AmmoWheelReskinUnified.cpp:194-195` | `AmmoWheelReskinUnified.cpp:655` | Reskin.Enabled=true | None |
| Max Memory | Safety.MaxTotalBytesMB | 64 | 1-512 | MB | `AmmoWheelReskinUnified.cpp:196-198` | `AmmoWheelReskinUnified.cpp:564` | Reskin.Enabled=true | None |
| Max Texture Size | Safety.MaxTextureSize | 2048 | 256-4096 | px | `AmmoWheelReskinUnified.cpp:199-200` | `AmmoWheelReskinUnified.cpp:512-513` | Reskin.Enabled=true | None |
| Max PNG File | Safety.MaxPngFileMB | 10 | 1-50 | MB | `AmmoWheelReskinUnified.cpp:201-203` | `AmmoWheelReskinUnified.cpp:490` | Reskin.Enabled=true | None |

### AmmoWheel.ini - [Debug] Section

| Setting | Section.Key | Default | Range | Units | Code Read | Code Apply | Dependencies | Conflicts |
|---------|-------------|---------|-------|-------|-----------|------------|--------------|-----------|
| Log Preset Resolution | Debug.LogPresetResolution | false | true/false | bool | `Config.cpp` | `AmmoWheel.cpp:~980` | Reskin.Enabled=true | None |
| Log Asset Loading | Debug.LogAssetLoading | false | true/false | bool | `Config.cpp` | `AmmoWheelReskinUnified.cpp:566,3724` | Reskin.Enabled=true | None |
| Show Reskin Overlay | Debug.ShowReskinOverlay | false | true/false | bool | `Config.cpp` | `AmmoWheel.cpp` | Reskin.Enabled=true | None |

### AmmoWheel.ini - [Theme] Section (DEPRECATED)

| Setting | Section.Key | Default | Range | Units | Code Read | Code Apply | Dependencies | Conflicts |
|---------|-------------|---------|-------|-------|-----------|------------|--------------|-----------|
| Use Skyrim Theme | Theme.UseSkyrimTheme | false | true/false | bool | `Config.cpp` | Multiple | None | **DEPRECATED**: Use Reskin.Enabled instead |
| Use Main Wheel Theme | Appearance.UseMainWheelTheme | false | true/false | bool | `Config.cpp` | Multiple | None | **DEPRECATED**: Use Reskin.Enabled instead |

### AmmoWheel.ini - [Skin] Section (LEGACY - NOT USED)

| Setting | Section.Key | Default | Range | Units | Code Read | Code Apply | Dependencies | Conflicts |
|---------|-------------|---------|-------|-------|-----------|------------|--------------|-----------|
| Use Styles INI | Skin.UseAmmoWheelStylesIni | false | true/false | bool | `Config.cpp` | NONE | None | **DEAD CODE**: Not used when Reskin.Enabled=true |
| Use Preset Styles | Skin.UsePresetStyles | false | true/false | bool | `Config.cpp` | NONE | None | **DEAD CODE**: Not used when Reskin.Enabled=true |

---

## AmmoWheel_Styles.ini - Preset Definition Keys

Each `[Preset_X]` section supports these keys per visual target:

| Key Pattern | Default | Range | Units | Purpose |
|-------------|---------|-------|-------|---------|
| `<Target>_Enabled` | false | true/false | bool | Enable this target |
| `<Target>_Path` | "" | relative path | string | Asset path or flipbook pattern |
| `<Target>_IsFlipbook` | false | true/false | bool | Static PNG vs animated flipbook |
| `<Target>_FrameCount` | 1 | 1-256 | count | Number of flipbook frames |
| `<Target>_FPS` | 24.0 | 1-60 | fps | Flipbook playback speed |
| `<Target>_Loop` | true | true/false | bool | Loop flipbook animation |
| `<Target>_Alpha` | 1.0 | 0.0-1.0 | ratio | Opacity multiplier |
| `<Target>_RotationMode` | 0-2 | 0=FollowSlot, 1=Upright, 2=Fixed | enum | Rotation behavior |
| `<Target>_RotationOffsetDeg` | 0.0 | -360 to 360 | degrees | Rotation offset |
| `<Target>_FitMode` | 0 | 0=Fixed, 1=FitInside | enum | Size calculation mode |
| `<Target>_FixedSizePx` | 48.0 | 1-512 | px | Fixed size when FitMode=0 |
| `<Target>_RotationSafetyScale` | 0.85 | 0.1-1.0 | ratio | Scale for rotation clipping |

**Code Reference**: `ReskinSystem::LoadPresets()` at `AmmoWheelReskinUnified.cpp:264-313`

---

## AMMO_KID.ini - Mapping Keys

| Section | Key Format | Example | Code Read |
|---------|------------|---------|-----------|
| `[FormIDPresets]` | `<HexFormID> = <PresetName>` | `020098A0 = BloodcursedElvenArrow` | `AmmoWheelReskinUnified.cpp:348-360` |
| `[KeywordPresets]` | `<KeywordEditorID> = <PresetName>` | `DLC1BloodcursedArrow = BloodcursedElvenArrow` | `AmmoWheelReskinUnified.cpp:363-371` |
| `[TypePresets]` | `Arrow = <PresetName>`, `Bolt = <PresetName>` | `Arrow = Default` | `AmmoWheelReskinUnified.cpp:374-375` |

**Critical**: Section names MUST be exactly `[FormIDPresets]` and `[KeywordPresets]` (not `[FormID]` or `[Keyword]`).

---

## Resource Lookup Table

| Render Target | Folder(s) | Filename Pattern | Lookup Order | Fallback | Code Reference |
|---------------|-----------|------------------|--------------|----------|----------------|
| PopupBubble | `popup/` | `{pattern}_{:02}.png` or static | Preset Path → Default Preset | Primitive circle | `AmmoWheelReskinUnified.cpp:462-463`, `AmmoWheel.cpp:3717-3721` |
| Popup (slot) | `popup/` | `{pattern}_{:02}.png` | Preset Path → Default Preset | None (skip) | `AmmoWheelReskinUnified.cpp:323` |
| SlotIcon | `icons/`, `icons_custom/` | `arrow.svg`, `bolt.svg`, or custom | Custom → Default SVG | Built-in SVG | TextureManager |
| SlotBackground | `slot_bg/` | `*.png` | Preset Path | Primitive arc | `AmmoWheelReskinUnified.cpp:317` |
| SlotFrame | `slot_frame/` | `*.png` | Preset Path | None (skip) | `AmmoWheelReskinUnified.cpp:318` |
| WheelBackground | `wheel_bg/` | `*.png` | Preset Path | Primitive circle | `AmmoWheelReskinUnified.cpp:327` |
| CenterBackground | `center_bg/` | `*.png` | Preset Path | Primitive rect | `AmmoWheelReskinUnified.cpp:328` |
| IndicatorSelected | `indicators/selected/` | `*.png` | Preset Path | Primitive arc | `AmmoWheelReskinUnified.cpp:319` |
| IndicatorHovered | `indicators/hovered/` | `*.png` | Preset Path | Primitive arc | `AmmoWheelReskinUnified.cpp:320` |
| IndicatorActive | `indicators/active/` | `*.png` | Preset Path | Primitive arc | `AmmoWheelReskinUnified.cpp:321` |
| IndicatorCharge | `indicators/charge/` | `*.png` | Preset Path | Primitive arc | `AmmoWheelReskinUnified.cpp:322` |

### Path Resolution

1. Asset path from preset (e.g., `popup/blood_mist_{:02}.png`)
2. Prepend base path: `resources/ammo_wheel/` + asset path
3. Format flipbook pattern with frame number
4. Load via `GetTexture()` at `AmmoWheelReskinUnified.cpp:469-568`

---

## Flipbook Frame Loading

**Pattern Format**: `{:<width>}` where width is zero-padding (e.g., `{:02}` → 00, 01, 02...)

**Code Reference**: `GetFlipbookFrame()` at `AmmoWheelReskinUnified.cpp:643-715`

```cpp
// Pattern: popup/blood_mist_{:02}.png
// Generates: popup/blood_mist_00.png, popup/blood_mist_01.png, ...
framePath = fmt::format(fmt::runtime(def.pattern), i);
```

**Alternative Pattern**: If no `{:` in pattern, appends `_XX` before extension:
```cpp
// Pattern: popup/blood_mist.png
// Generates: popup/blood_mist_00.png, popup/blood_mist_01.png, ...
framePath = pattern.substr(0, dotPos) + fmt::format("_{:02}", i) + pattern.substr(dotPos);
```

---

## wheelBehavior.ini Keys

| Setting | Section.Key | Default | Range | Units | Code Read | Purpose |
|---------|-------------|---------|-------|-------|-----------|---------|
| Wheeler Enabled | General.WheelerEnabled | true | true/false | bool | `wheelBehavior.ini:8` | Master Wheeler toggle |
| Vanilla LT Passthrough | General.VanillaLTPassthrough | false | true/false | bool | `wheelBehavior.ini:21` | Controller compatibility |
| Release to Use | WheelBehavior.ReleaseToUse | false | true/false | bool | `wheelBehavior.ini:36` | RTU activation mode |
| Close After Use | WheelBehavior.CloseWheelAfterUse | false | true/false | bool | `wheelBehavior.ini:39` | Auto-close behavior |
| RTU Alchemy | WheelBehavior.RTUAlchemy | true | true/false | bool | `wheelBehavior.ini:42` | RTU for potions |
| RTU Spell | WheelBehavior.RTUSpell | true | true/false | bool | `wheelBehavior.ini:43` | RTU for spells |
| RTU Shout | WheelBehavior.RTUShout | true | true/false | bool | `wheelBehavior.ini:44` | RTU for shouts |
| Hover Delay | WheelBehavior.HoverActivateDelaySeconds | 0.5 | 0.0-5.0 | seconds | `wheelBehavior.ini:47` | RTU activation delay |

---

## Styles.ini (LEGACY - NOT USED BY UNIFIED SYSTEM)

This file uses a different format (`[Preset.X]` instead of `[Preset_X]`) and is **NOT read** by the unified reskin system.

**Status**: LEGACY / DEAD CODE when `[Reskin] Enabled = true`

**Original Purpose**: Define presets with color overrides for the old style system.

**Recommendation**: Migrate any custom presets to `AmmoWheel_Styles.ini` using the new `[Preset_X]` format.

---

## skins/default/skin.ini (NOT ACTIVE)

**Status**: NOT ACTIVE - requires `UseAmmoWheelStylesIni = true` which conflicts with unified system.

**Purpose**: Intended for skin pack distribution with self-contained assets.

**Current State**: Template only, not processed by current code.
