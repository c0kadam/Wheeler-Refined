# Wheeler Preview Tool - Supported Settings

## ✅ Settings That Work In-Game

These settings are **fully supported** by the Wheeler SKSE plugin and will work when you save them:

### AmmoWheel.ini

**[Position]**
- `WheelRadius` - Size of the wheel in pixels

**[Appearance]**
- `WheelShape` - 0=Full Circle, 1=Half Circle, 2=Quarter Circle
- `ArcStartAngle` - Starting angle in degrees (180 = left side)
- `SlotShape` - **NEW!** 0=Arc, 1=RoundedRect, 2=Pill, 3=Circle
- `SlotCornerRadius` - **NEW!** Corner radius for RoundedRect shape (pixels)
- `SlotShapeScale` - **NEW!** Scale factor for non-arc shapes (0.5-1.2)

**[Geometry]**
- `InnerRadiusRatio` - Slot thickness (0.2 to 0.9)
- `SlotGapDeg` - Gap between slots in degrees (0 to 15)

**[VisualPolish]**
- `BackgroundEnabled` - Enable/disable background layer
- `BackgroundOpacity` - Background transparency (0.0-1.0)
- `BackgroundRadiusScale` - Background size multiplier
- `BorderEnabled` - Enable/disable decorative border ring (arc-only)
- `BorderInnerScale` / `BorderOuterScale` - Border ring size
- `SlotShadowEnabled` - Enable/disable slot drop shadows
- `SlotShadowOffsetX` / `SlotShadowOffsetY` - Shadow offset
- `SlotShadowAlpha` - Shadow transparency (0-255)
- `SlotHighlightEnabled` - Enable/disable hover highlight
- `SlotHighlightThickness` / `SlotHighlightAlpha` - Highlight appearance

**[Animations]**
- `HoverPulseEnabled` - Enable/disable pulsing glow on hover
- `HoverPulseSpeed` / `HoverPulseSize` - Pulse animation settings
- `SlotDividersEnabled` - Enable/disable divider lines (arc-only)
- `SlotDividerThickness` - Divider line width

**[Theme]**
- `UseSkyrimTheme` - Use Skyrim-inspired parchment/leather colors

### Styles.ini

**[AmmoWheel.Slot]**
- `UnhoveredColorBegin` - Inner color for normal slots (0xAARRGGBB)
- `UnhoveredColorEnd` - Outer color for normal slots
- `HoveredColorBegin` - Inner color for hovered slots
- `HoveredColorEnd` - Outer color for hovered slots
- `SelectedColorBegin` - Inner color for selected slots
- `SelectedColorEnd` - Outer color for selected slots

---

## 🎉 Slot Shape Feature - NOW FULLY IMPLEMENTED!

The **Slot Shape** feature has been fully implemented in the Wheeler SKSE plugin!

### Available Shapes:
| Value | Shape | Description |
|-------|-------|-------------|
| 0 | **Arc** | Default radial segments (original behavior) |
| 1 | **RoundedRect** | Rounded rectangles with configurable corner radius |
| 2 | **Pill** | Capsule/pill shapes |
| 3 | **Circle** | Circular slots |

### Shape-Specific Features:
- **Arc shapes**: Support border ring, slot dividers, arc-based indicators
- **Non-arc shapes**: Support shape-matching shadows, highlights, pulse, and indicators
- **SlotShapeScale**: Controls size of non-arc shapes (0.5-1.2)
- **SlotCornerRadius**: Controls corner rounding for RoundedRect shape

### Compatibility Notes:
- Border ring and slot dividers are **automatically disabled** for non-arc shapes
- Indicators adapt to match the slot shape
- All visual polish features (shadows, highlights, pulse) work with all shapes

---

## 💡 Recommendations

1. **All settings now save to game** - Changes made here will work in Skyrim
2. **Use Skyrim Theme** for an authentic look
3. **Test different shapes** - Each has unique visual characteristics
4. **Adjust SlotShapeScale** for non-arc shapes to fine-tune appearance

---

## 📁 File Locations

- **AmmoWheel.ini**: `Data/SKSE/Plugins/wheeler/AmmoWheel.ini`
- **Styles.ini**: `Data/SKSE/Plugins/wheeler/resources/ammo_wheel/Styles.ini`
- **dMenu UI**: `Data/SKSE/Plugins/dmenu/customSettings/Ammo Wheel.json`
