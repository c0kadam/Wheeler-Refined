# AmmoWheel Conflict Matrix

## Precedence Chains

### 1. Preset Selection Precedence

```
Priority 1: FormID Mapping (AMMO_KID.ini [FormIDPresets])
    ↓ not found
Priority 2: Keyword Mapping (AMMO_KID.ini [KeywordPresets])
    ↓ not found
Priority 3: Type Default (AmmoWheel.ini [Reskin] ArrowDefaultPreset/BoltDefaultPreset)
    ↓ not found
Priority 4: Fallback ("Default" preset)
```

**Code Reference**: `ReskinSystem::ResolveForAmmo()` at `AmmoWheelReskinUnified.cpp:381-438`

### 2. Style Selection Precedence

When `[Reskin] Enabled = true`:
```
AmmoWheel_Styles.ini [Preset_X] sections → ACTIVE
Styles.ini [Preset.X] sections → IGNORED
skins/*/skin.ini → IGNORED
```

When `[Reskin] Enabled = false`:
```
[Primitives] colors → ACTIVE (if defined)
Theme settings (UseSkyrimTheme, UseMainWheelTheme) → ACTIVE
AmmoWheel_Styles.ini → IGNORED
```

### 3. Asset Selection Precedence

For each visual target:
```
1. Preset-defined asset path (if _Enabled = true and file exists)
    ↓ asset not enabled or file missing
2. Default preset asset path (if _Enabled = true and file exists)
    ↓ asset not enabled or file missing
3. Primitive fallback rendering (arcs, circles, etc.)
```

**Code Reference**: `ReskinSystem::DrawTarget()` at `AmmoWheelReskinUnified.cpp:440-467`

---

## Conflict Pairs

### If A Enabled → B Ignored

| If This Is Enabled | These Are Ignored | Reason |
|--------------------|-------------------|--------|
| `[Reskin] Enabled = true` | `UseSkyrimTheme`, `UseMainWheelTheme`, `UseCustomStyles` | Unified system overrides all legacy theme settings |
| `[Reskin] Enabled = true` | `Styles.ini` (entire file) | Unified system reads only `AmmoWheel_Styles.ini` |
| `[Reskin] Enabled = true` | `skins/*/skin.ini` | Skin system not implemented with unified system |
| `[Reskin] Enabled = true` | `[Skin] UseAmmoWheelStylesIni` | Legacy toggle, superseded |
| `[Reskin] Enabled = true` | `[Skin] UsePresetStyles` | Legacy toggle, superseded |
| FormID mapping exists | Keyword mapping for same ammo | FormID takes priority |
| FormID mapping exists | Type default for same ammo | FormID takes priority |
| Keyword mapping exists | Type default for same ammo | Keyword takes priority |

### If A Enabled → B Must Be Disabled

| Setting A | Setting B | Reason |
|-----------|-----------|--------|
| `[Reskin] Enabled = true` | `[Skin] UseAmmoWheelStylesIni = true` | Both try to control visual system |
| `UseSkyrimTheme = true` | `UseMainWheelTheme = true` | Mutually exclusive themes |

### Dead Code / Unused Settings

| Setting | Status | Reason |
|---------|--------|--------|
| `[Skin] UseAmmoWheelStylesIni` | DEAD CODE | Not read when unified system enabled |
| `[Skin] UsePresetStyles` | DEAD CODE | Not read when unified system enabled |
| `Styles.ini` all keys | IGNORED | Unified system reads `AmmoWheel_Styles.ini` only |
| `skins/*/skin.ini` | NOT ACTIVE | Skin pack system not implemented |

---

## Toggle Decision Table

### Master State Combinations

| Reskin.Enabled | UseSkyrimTheme | UseMainWheelTheme | Result |
|----------------|----------------|-------------------|--------|
| **true** | any | any | Unified system active, themes ignored |
| false | true | false | SkyrimTheme colors |
| false | false | true | MainWheel colors |
| false | false | false | Default/Primitive colors |

### Visual Outcome by State

| State | Slot Background | Indicators | PopupBubble | Hover Effects |
|-------|-----------------|------------|-------------|---------------|
| Reskin ON + Preset has assets | PNG/Flipbook | PNG/Flipbook | PNG/Flipbook | PNG/Flipbook |
| Reskin ON + Preset disabled | Primitive arc | Primitive arc | Primitive circle | None |
| Reskin OFF + SkyrimTheme | Primitive (gold) | Primitive (gold) | Primitive | Primitive |
| Reskin OFF + MainWheelTheme | Primitive (main) | Primitive (main) | Primitive | Primitive |
| Reskin OFF + No theme | Primitive (blue) | Primitive (blue) | Primitive | Primitive |

---

## PopupBubble Specific Conflicts

### Why PopupBubble Might Not Show

| Condition | Result | Fix |
|-----------|--------|-----|
| `[Reskin] Enabled = false` | No PopupBubble textures | Enable reskin |
| `PopupBubble_Enabled = false` | Primitive circle only | Set `PopupBubble_Enabled = true` |
| `PopupBubble_Path` files missing | Falls through to primitive | Create PNG files |
| Wrong section name `[Preset.X]` | Preset not loaded | Use `[Preset_X]` format |
| Wrong mapping section `[FormID]` | Mapping not loaded | Use `[FormIDPresets]` |
| Preset name mismatch | Falls to Default | Verify exact name match |

### PopupBubble vs Popup Confusion

| Setting | Where It Renders | Use Case |
|---------|------------------|----------|
| `Popup_*` | At slot center (inside wheel) | Slot hover glow |
| `PopupBubble_*` | Outside wheel (tooltip panel) | Your flipbook animation |

**Common Mistake**: Configuring `Popup_*` settings expecting them to appear in the tooltip bubble.

---

## Section Name Conflicts

### Correct vs Incorrect Section Names

| File | Correct | Incorrect (Won't Work) |
|------|---------|------------------------|
| `AmmoWheel_Styles.ini` | `[Preset_MyName]` | `[Preset.MyName]`, `[MyName]` |
| `AMMO_KID.ini` | `[FormIDPresets]` | `[FormID]`, `[FormIdPresets]` |
| `AMMO_KID.ini` | `[KeywordPresets]` | `[Keyword]`, `[Keywords]` |
| `Styles.ini` | `[Preset.MyName]` | N/A - entire file ignored |

---

## Recommended Configuration

### Minimal Working Setup

```ini
; AmmoWheel.ini
[Reskin]
Enabled = true
ArrowDefaultPreset = Default
BoltDefaultPreset = Default

[Skin]
UseAmmoWheelStylesIni = false   ; MUST be false
UsePresetStyles = false          ; MUST be false

[Theme]
UseSkyrimTheme = false           ; Ignored but set false for clarity
```

```ini
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

### Files Required

| File | Required | Purpose |
|------|----------|---------|
| `AmmoWheel.ini` | YES | Master config |
| `AmmoWheel_Styles.ini` | YES | Preset definitions |
| `AMMO_KID.ini` | Optional | Custom mappings |
| `popup/*.png` | YES | Your animation frames |
| `Styles.ini` | NO | Legacy, ignored |
| `skins/*/skin.ini` | NO | Not active |

---

## Troubleshooting Flow

```
PopupBubble not showing?
    │
    ├─ Check: [Reskin] Enabled = true?
    │   └─ NO → Enable it
    │
    ├─ Check: Preset loaded? (see log for "Loaded preset: X")
    │   └─ NO → Fix section name to [Preset_X]
    │
    ├─ Check: Mapping loaded? (see log for "FormID mappings")
    │   └─ 0 mappings → Fix section to [FormIDPresets]
    │
    ├─ Check: PopupBubble_Enabled = true in preset?
    │   └─ NO or missing → Add it
    │
    ├─ Check: PNG files exist at specified path?
    │   └─ NO → Create files with correct naming
    │
    └─ Check: Frame count matches actual files?
        └─ NO → Adjust PopupBubble_FrameCount
```
