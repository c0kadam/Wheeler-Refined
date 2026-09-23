# Optional Patch: Fix Default Preset PopupBubble Path

## Issue Summary

The Default preset in `AmmoWheel_Styles.ini` references `popup/universal_{:02}.png` files that don't exist. Your actual flipbook files are `popup/blood_mist_XX.png` (50 frames).

This causes all arrows without specific mappings to show NO popup bubble animation because the fallback Default preset can't load its configured assets.

## Root Cause Analysis

**Code Path**:
1. `ReskinSystem::ResolveForAmmo()` at `AmmoWheelReskinUnified.cpp:381-438`
   - Resolves preset for ammo item
   - Falls back to Default preset if no mapping exists

2. `ReskinSystem::DrawTarget()` at `AmmoWheelReskinUnified.cpp:440-467`
   - Tries to load asset from preset
   - Returns false if files don't exist

3. `ReskinSystem::GetFlipbookFrame()` at `AmmoWheelReskinUnified.cpp:643-715`
   - Generates frame paths from pattern
   - Returns nullptr if files missing

**Current State**:
```ini
; AmmoWheel_Styles.ini [Preset_Default]
PopupBubble_Path = popup/universal_{:02}.png  ; FILES DON'T EXIST
PopupBubble_FrameCount = 30
```

**Available Assets**:
```
popup/blood_mist_00.png through popup/blood_mist_49.png (50 files exist)
```

## Fix Options

### Option A: Update Default Preset Path (Recommended)

**File**: `AmmoWheel_Styles.ini`

**Change**:
```ini
[Preset_Default]
; Change this:
; PopupBubble_Path = popup/universal_{:02}.png
; PopupBubble_FrameCount = 30

; To this:
PopupBubble_Path = popup/blood_mist_{:02}.png
PopupBubble_FrameCount = 50
```

**Impact**: All arrows will use blood_mist animation by default.

**Backward Compatibility**: Safe. Only affects visual appearance.

### Option B: Create Missing Files

Create 30 PNG files:
```
popup/universal_00.png through popup/universal_29.png
```

**Impact**: Provides distinct "universal" animation separate from blood_mist.

**Effort**: Requires creating/copying 30 PNG files.

### Option C: Disable Default PopupBubble

**File**: `AmmoWheel_Styles.ini`

**Change**:
```ini
[Preset_Default]
PopupBubble_Enabled = false
```

**Impact**: Arrows without mappings show primitive circle instead of animation.

**Use Case**: If you only want animations for specifically mapped arrows.

## Recommended Fix

Apply **Option A** - it's the simplest and uses your existing assets:

```diff
; AmmoWheel_Styles.ini

 [Preset_Default]
 Name = Default Preset
 
 ; PopupBubble - the actual popup bubble OUTSIDE the wheel (tooltip panel)
 ; Now uses universal flipbook animation for all arrows
 PopupBubble_Enabled = true
 PopupBubble_IsFlipbook = true
-PopupBubble_Path = popup/universal_{:02}.png
-PopupBubble_FrameCount = 30
+PopupBubble_Path = popup/blood_mist_{:02}.png
+PopupBubble_FrameCount = 50
 PopupBubble_FPS = 24.0
 PopupBubble_Loop = true
 PopupBubble_Alpha = 0.85
```

## Verification

After applying the fix:

1. Enable debug logging:
   ```ini
   [Debug]
   LogAssetLoading = true
   LogPresetResolution = true
   ```

2. Launch game, open ammo wheel, hover over any arrow

3. Check `wheeler.log` for:
   ```
   [AmmoWheel] PopupBubble texture drawn at (X, Y) radius=R
   ```

4. Visual confirmation: Blood mist animation should appear in popup bubble

## No Code Changes Required

This issue is purely configuration. No C++ code changes needed.
