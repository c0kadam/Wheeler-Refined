# AmmoWheel Unified Reskin Author Guide

## The 3 files that matter
1. `Data/SKSE/Plugins/wheeler/AmmoWheel.ini`
   - In `[Reskin]`, set `Enabled=true` and `BasePath`.
2. `<BasePath>/AmmoWheel_Styles.ini`
   - Defines visual presets (`[Preset_<ID>]`).
3. `<BasePath>/AMMO_KID.ini`
   - Maps ammo to preset IDs.

## Minimal "create a new reskin" checklist
1. Put assets in the target folders under `<BasePath>`.
2. Create `[Preset_MyPreset]` in `AmmoWheel_Styles.ini` (or `styles/my_presets.ini`).
3. Map ammo in `AMMO_KID.ini` (or `mappings/my_mappings.ini`).

## VisualTarget folder table
| VisualTarget | Folder | Typical asset |
|---|---|---|
| `WheelBackground` | `wheel_bg/` | `.png` or `.svg` |
| `WheelBorderRing` | `wheel_border/` | `.png` or `.svg` |
| `SlotBackground` | `slot_bg/` | `.png` or `.svg` |
| `SlotFrame` | `slot_frame/` | `.png` or `.svg` |
| `SlotDivider` | `slot_divider/` | `.png` or `.svg` |
| `SlotIcon` | `icons/` | `.png` or `.svg` |
| `IndicatorSelected` | `indicators/selected/` | `.png` or `.svg` |
| `IndicatorHovered` | `indicators/hovered/` | `.png` or `.svg` |
| `IndicatorActive` | `indicators/active/` | `.png` or `.svg` |
| `IndicatorCharge` | `indicators/charge/` | `.png/.svg` or flipbook pattern |
| `CenterBackground` | `center_bg/` | `.png` or `.svg` |
| `CenterPanelFrame` | `center_frame/` | `.png` or `.svg` |
| `NamePanelBackground` | `label_bg/` | `.png` or `.svg` |
| `CursorIndicator` | `cursor/` | `.png` or `.svg` |
| `LowAmmoIndicator` | `low_ammo/` | `.png` or `.svg` |
| `Popup` | `popup/` | `.png/.svg` or flipbook pattern |
| `PopupBubble` | `popup/bubble_fill/` | `.png` or `.svg` |
| `PopupBubbleRim` | `popup/bubble_rim/` | `.png` or `.svg` |
| `PopupBubbleGlow` | `popup/bubble_glow/` | `.png` or `.svg` |
| `AmmoCountDigits` | `digits/` | digits atlas `.png`/`.svg` |

## Modular INI support
- Optional preset files: `<BasePath>/styles/*.ini`
- Optional mapping files: `<BasePath>/mappings/*.ini`
- Load order is deterministic: lexical sort by filename, after the base files.

## Legacy note
- Legacy/duplicate files are archived under `<BasePath>/_legacy` when possible.
- If your package keeps read-only files that cannot be moved, they are ignored by ReskinUnified.
