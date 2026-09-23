# AmmoWheel Reskin Mod Key Mapping

This document maps the main Wheeler Styles.ini keys to their AmmoWheel equivalents, based on analysis of the Dragonborn Reskin mod.

## Main Wheel Styles.ini → AmmoWheel Mapping

### Geometry

| Main Wheel Key | AmmoWheel Key | Notes |
|----------------|---------------|-------|
| `[Styling.Wheel] InnerCircleRadius` | `[Position] WheelRadius` × `[Geometry] InnerRadiusRatio` | AmmoWheel uses ratio-based inner radius |
| `[Styling.Wheel] OuterCircleRadius` | `[Position] WheelRadius` | Direct mapping |
| `[Styling.Wheel] InnerSpacing` | `[Geometry] SlotGapDeg` | AmmoWheel uses degrees instead of pixels |
| `[Styling.Wheel] ActiveArcWidth` | `[Indicators] SelectedIndicatorThickness` | Similar concept |

### Colors

| Main Wheel Key | AmmoWheel Key | Notes |
|----------------|---------------|-------|
| `[Styling.Wheel] HoveredColorBegin` | `[Colors] HoverColorPreset` + `HoverOpacity` | Preset-based or custom RGBA |
| `[Styling.Wheel] HoveredColorEnd` | (same as above) | AmmoWheel uses single color |
| `[Styling.Wheel] UnhoveredColorBegin` | Inherits from main wheel or custom theme | Via `UseMainWheelTheme` |
| `[Styling.Wheel] UnhoveredColorEnd` | (same as above) | |
| `[Styling.Wheel] ActiveArcColorBegin` | `[Colors] SelectedColorPreset` + `SelectedOpacity` | |
| `[Styling.Wheel] TextColor` | `[Colors] NameTextColorPreset` + `NameTextOpacity` | |
| `[Styling.Wheel] TextShadowColor` | (not yet implemented) | Future enhancement |

### Text Sizing

| Main Wheel Key | AmmoWheel Key | Notes |
|----------------|---------------|-------|
| `[Styling.Entry.Highlight.Text] Size` | `[Text] NameFontPx` | Direct pixel size |
| `[Styling.Item.Slot.Text] Size` | `[Text] CountFontPx` | For ammo count |
| `[Styling.Item.Highlight.Text] Size` | `[Text] CenterFontPx` | Center panel text |

### Textures/Icons

| Main Wheel Key | AmmoWheel Key | Notes |
|----------------|---------------|-------|
| `[Styling.Item.Slot.Texture] Scale` | `[Geometry] IconSizePx` | AmmoWheel uses pixel size |
| `[Styling.Item.Slot.Texture] OffsetX/Y` | `[Geometry] IconRadialOffsetPx` | Radial offset only |
| `[Styling.Item.Slot.BackgroundTexture] Scale` | Skin-based | Via skin.ini |

### Animation (Future)

| Main Wheel Key | AmmoWheel Key | Notes |
|----------------|---------------|-------|
| `[Animation] EntryHighlightExpandTime` | (not yet implemented) | |
| `[Animation] FadeTime` | (not yet implemented) | |

## Resource Folder Structure

```
Data/SKSE/Plugins/wheeler/resources/
├── icons/                    # Main wheel icons
├── icons_custom/             # Main wheel custom icons
└── ammo_wheel/               # AmmoWheel-specific resources
    ├── icons/                # AmmoWheel icons (fallback to main)
    ├── icons_custom/         # AmmoWheel custom icons
    └── skins/
        └── default/
            ├── skin.ini      # Skin configuration
            ├── shapes/       # SVG shape masks
            └── textures/     # PNG textures
```

## Color Presets

AmmoWheel uses a preset system for user-friendly color selection:

| Preset Index | Color Name | RGB Values |
|--------------|------------|------------|
| 0 | Custom | Use RGBA from Colors.Advanced |
| 1 | White | 255, 255, 255 |
| 2 | Black | 0, 0, 0 |
| 3 | Red | 255, 60, 60 |
| 4 | Green | 60, 255, 60 |
| 5 | Blue | 60, 60, 255 |
| 6 | Yellow | 255, 255, 60 |
| 7 | Orange | 255, 165, 0 |
| 8 | Cyan | 60, 255, 255 |
| 9 | Magenta | 255, 60, 255 |
| 10 | Gray | 128, 128, 128 |

## Dragonborn Reskin Analysis

The Dragonborn Reskin mod (`SKSE/Plugins/wheeler/Styles.ini`) uses:

- **Geometry**: `InnerCircleRadius=250`, `OuterCircleRadius=329` (ratio ≈ 0.76)
- **Slot Background**: `Scale=0.350000`
- **Icon Scale**: `Scale=0.250000`
- **Text Sizes**: Entry=37, Slot=32, Highlight=40, Desc=30
- **Colors**: Uses semi-transparent grays for unhovered, brighter for hovered
- **Animation**: Fast expand/retract times (0.14s)

To create an AmmoWheel skin matching this style:
1. Set `InnerRadiusRatio=0.76`
2. Set `IconSizePx=82` (329 × 0.25)
3. Set `NameFontPx=37`, `CountFontPx=32`, `CenterFontPx=30`
4. Use custom colors via preset=0 and RGBA values
