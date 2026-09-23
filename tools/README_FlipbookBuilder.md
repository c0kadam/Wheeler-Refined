# AmmoWheel Flipbook Builder

A Python tool for generating flipbook PNG sequences and static PNGs for Wheeler's AmmoWheel reskin system. All geometry values and rendering logic are extracted directly from the C++ codebase.

## Features

- **Animated GIF Support**: Automatically extracts frames from animated GIFs
- **SVG Vector Support**: Converts SVG files to high-quality PNGs
- **PNG Support**: Processes static PNGs or folders of PNG sequences
- **Accurate Geometry**: Calculates canvas dimensions based on C++ rendering logic
- **Geometric Masking**: Generates arc and circle masks matching game rendering
- **Multiple Resize Modes**: Fill (crop), Fit (no crop), or Stretch
- **Auto INI Generation**: Creates `AmmoWheel_Styles.ini` snippets with correct rotation modes

## Installation

### Requirements

- Python 3.7 or higher
- pip (Python package manager)

### Install Dependencies

```bash
# Required dependencies
pip install Pillow

# Optional: For SVG support
pip install cairosvg
```

**Note**: SVG support requires `cairosvg`. If not installed, the tool will still work but SVG files will not be supported.

### Windows Users

If you encounter issues installing `cairosvg` on Windows:

1. Download GTK3 runtime from: https://github.com/tschoonj/GTK-for-Windows-Runtime-Environment-Installer
2. Install GTK3
3. Then run: `pip install cairosvg`

## Usage

### Launch the Tool

```bash
python tools/ammo_flipbook_builder.py
```

### Workflow

1. **Select Mod Root Folder**
   - Click "Browse..." next to "Mod Root Folder"
   - Select your mod's root directory (contains `Data/SKSE/Plugins/wheeler/`)
   - The tool will automatically parse `AmmoWheel.ini` for geometry values

2. **Select Source**
   - Click "Browse..." next to "Source"
   - Choose either:
     - **Single File**: Animated GIF, SVG, or PNG
     - **Folder**: Directory containing PNG sequence
   - For animated GIFs, frame count is auto-detected

3. **Choose Target Type**
   - Select the visual element you're creating assets for:
     - **PopupBubble**: Popup tooltip background (circle, upright)
     - **SlotBackground**: Slot fill (arc, rotates with slot)
     - **SlotIcon**: Ammo icon (circle, rotates with slot)
     - **WheelBackground**: Full wheel background (circle, upright)
     - And more...

4. **Configure Flipbook Settings** (if generating animation)
   - Frame Count: Number of frames to generate
   - FPS: Animation playback speed
   - Loop: Whether animation loops

5. **Processing Options**
   - **Resize Mode**:
     - **Fill**: Covers shape fully, crops excess (default)
     - **Fit**: Fits inside without cropping
     - **Stretch**: Forces exact dimensions
   - **Apply Geometric Mask**: Cuts image into arc/circle shape matching game rendering

6. **Set Output Name**
   - Enter a name for your flipbook (e.g., "my_popup_ring")

7. **Generate**
   - Click "Generate Flipbook"
   - Output files are saved to: `Data/SKSE/Plugins/wheeler/resources/ammo_wheel/<target_subfolder>/`
   - INI snippet is saved alongside the PNGs

## Supported Source Formats

| Format | Type | Notes |
|--------|------|-------|
| `.gif` | Animated GIF | Extracts all frames automatically |
| `.svg` | Vector | Converts to PNG at 2048x2048 (requires cairosvg) |
| `.png` | Raster | Single image or folder of images |
| `.jpg`, `.jpeg` | Raster | Single image |

## Visual Targets Reference

| Target | Shape | Rotation | Canvas Size Calculation |
|--------|-------|----------|------------------------|
| SlotIcon | Circle | FollowSlot | IconSizePx |
| SlotBackground | Arc | FollowSlot | Slot thickness * 2 |
| SlotFrame | Arc | FollowSlot | Slot thickness * 2 |
| IndicatorSelected | Arc | FollowSlot | Slot thickness * 2 |
| IndicatorHovered | Arc | FollowSlot | Slot thickness * 2 |
| Popup | Circle | Upright | Slot thickness * 2 |
| PopupBubble | Circle | Upright | PopupBubbleRadius * 2 |
| WheelBackground | Circle | Upright | WheelRadius * 2 |
| CenterBackground | Circle | Upright | InnerRadius * 1.5 |

## Geometry Values

The tool extracts geometry values from your `AmmoWheel.ini`:

- `[Position] WheelRadius` (default: 120.0)
- `[Geometry] InnerRadiusRatio` (default: 0.55)
- `[Geometry] IconSizePx` (default: 64.0)
- `[Geometry] SlotGapDeg` (default: 1.5)
- `[Popup] BubbleRadius` (default: 85.0)

If keys are missing, C++ defaults from `Config.h` are used.

## Example: Creating a Popup Bubble Animation

### From Animated GIF

1. Create or download an animated GIF (e.g., spinning ring effect)
2. Launch the tool
3. Select mod root folder
4. Browse and select your `.gif` file
5. Choose "PopupBubble" as target type
6. Frame count auto-detected from GIF
7. Set FPS (e.g., 24.0)
8. Enable "Apply Geometric Mask" for circular cutout
9. Set output name: "popup_ring"
10. Click "Generate Flipbook"

**Output**:
- `Data/SKSE/Plugins/wheeler/resources/ammo_wheel/popup/popup_ring_00.png` through `popup_ring_15.png`
- `popup_ring_snippet.ini` with configuration

### From SVG Vector

1. Create an SVG graphic (e.g., decorative border)
2. Launch the tool
3. Select mod root folder
4. Browse and select your `.svg` file
5. Choose target type (e.g., "SlotFrame")
6. Uncheck "Generate Flipbook" for static PNG
7. Set resize mode to "Fill"
8. Enable "Apply Geometric Mask" for arc cutout
9. Set output name: "ornate_frame"
10. Click "Generate Flipbook"

**Output**:
- `Data/SKSE/Plugins/wheeler/resources/ammo_wheel/slot_frame/ornate_frame.png`
- `ornate_frame_snippet.ini`

## INI Integration

After generation, copy the snippet content to your `AmmoWheel_Styles.ini`:

```ini
; Example output snippet
[Preset_MyCustom]
Name = My Custom Preset

; Popup Bubble
PopupBubble_Enabled = true
PopupBubble_IsFlipbook = true
PopupBubble_Path = popup/popup_ring_{:02}.png
PopupBubble_FrameCount = 16
PopupBubble_FPS = 24.0
PopupBubble_Loop = true
PopupBubble_RotationMode = 1
PopupBubble_RotationOffsetDeg = 0.0
PopupBubble_Alpha = 1.0
```

## Troubleshooting

### "No frames extracted from source"
- Ensure GIF is valid and not corrupted
- For SVG, verify `cairosvg` is installed
- Check file permissions

### "SVG support not available"
- Install cairosvg: `pip install cairosvg`
- On Windows, install GTK3 runtime first

### "Canvas size too small/large"
- Check your `AmmoWheel.ini` geometry values
- Verify `WheelRadius`, `InnerRadiusRatio`, etc. are reasonable

### Mask not applied correctly
- Ensure "Apply Geometric Mask" is checked
- For arc shapes, verify `SlotGapDeg` is set correctly
- Try different resize modes (Fill vs Fit)

## Technical Details

### Arc Mask Generation

Based on `AmmoWheel.cpp:2388-2390`, arc masks are generated with:
- Start angle: -90° - (arc_angle / 2)
- End angle: -90° + (arc_angle / 2)
- Arc angle: (360° / SlotCount) - SlotGapDeg

### Circle Mask Generation

Perfect circles using PIL's ellipse drawing, matching `ImGui::AddCircleFilled()` behavior.

### Rotation Modes

From `AmmoWheelReskinUnified.h:82-86`:
- **0 (FollowSlot)**: Rotates with slot angle
- **1 (Upright)**: Always vertical
- **2 (Fixed)**: Fixed rotation offset

## Version History

### v1.1 (Current)
- Added animated GIF frame extraction
- Added SVG to PNG conversion
- Auto-detection of GIF frame count
- Support for single file or folder sources

### v1.0
- Initial release
- PNG folder processing
- Geometric masking
- INI snippet generation

## Credits

Developed by the AmmoWheel project team. All geometry calculations and rendering logic extracted from Wheeler C++ source code (`Config.h`, `AmmoWheel.cpp`, `AmmoWheelReskinUnified.h`).
