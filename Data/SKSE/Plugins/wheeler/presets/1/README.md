# Preset 1: Skyrim Classic

A warm, lore-friendly visual theme inspired by Skyrim's vanilla UI aesthetic.

## Visual Style
- **Slot Shape**: Arc (radial segments)
- **Colors**: Parchment/leather tones with gold accents
- **Feel**: Warm, aged, lore-friendly

## Key Features
- Gold border ring around wheel
- Subtle slot shadows for depth
- Parchment-colored slots with leather undertones
- Gold hover pulse animation
- Text shadow for readability
- Dark semi-transparent label background

## Settings Changed
- WheelRadius: 280px (medium-large)
- SlotShape: Arc (0)
- InnerRadiusRatio: 0.50
- IconSize: 96px
- UseSkyrimTheme: true

## Activation
Set `ActivePreset = 1` in `[Presets]` section of AmmoWheel.ini, or use the dMenu preset selector.

## Fallback Behavior
If any asset is missing, the system falls back to primitive rendering using the Skyrim theme colors defined in Config.h.
