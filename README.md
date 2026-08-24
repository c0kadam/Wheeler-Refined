# Wheeler Refined

<p align="center">
  <img src="images/refined/wheeler-refined-hero.jpg" alt="Wheeler Refined" width="100%">
</p>

<p align="center">
  A stability and feature overhaul of dTry/D7ry's radial quick-action menu for Skyrim Special Edition and Anniversary Edition.
</p>

<p align="center">
  <a href="https://www.nexusmods.com/skyrimspecialedition/mods/167380"><img alt="Nexus Mods" src="https://img.shields.io/badge/Nexus%20Mods-Download-DA8E35?logo=nexusmods&logoColor=white"></a>
  <a href="https://github.com/c0kadam/Wheeler-Refined/releases/latest"><img alt="Latest source release" src="https://img.shields.io/github/v/release/c0kadam/Wheeler-Refined?display_name=tag&label=source%20release"></a>
  <a href="BUILDING.md"><img alt="Build from source" src="https://img.shields.io/badge/build-from%20source-2F81F7"></a>
  <a href="https://github.com/c0kadam/Wheeler-Refined/issues"><img alt="Issues" src="https://img.shields.io/github/issues/c0kadam/Wheeler-Refined"></a>
  <a href="LICENSE"><img alt="GPL-3.0-only" src="https://img.shields.io/badge/license-GPL--3.0--only-3DA639"></a>
</p>

> [!IMPORTANT]
> End-user downloads and installation support are provided through [Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/167380). A GitHub source checkout is not a complete player installation.

## What is Wheeler Refined?

Wheeler Refined modernizes [Wheeler](https://github.com/D7ry/wheeler), the quick-action wheel created by dTry/D7ry. It keeps Wheeler's core interaction and original assets while overhauling stability, controller behavior, configuration, feedback, and supported actions.

This is an independent derivative project. It is not an official continuation and is not affiliated with or endorsed by dTry/D7ry. Original Wheeler attribution and licensing are preserved throughout the repository.

## Highlights

| Area | Refined experience |
| --- | --- |
| Stability | Broad fixes for long-standing crashes, quick-load problems, stale slots, weapon persistence, and input edge cases. |
| Controller and mouse input | More stable stick selection, center-rest snapping, safer edit-mode input blocking, smoother Ammo Wheel mouse hover, and configurable modifier/toggle behavior. |
| Ammo Wheel | A separate inventory-driven wheel for arrows and bolts, with configurable layouts, filtering, sorting, Release to Use, low-ammo feedback, and reskin support. |
| Direct actions | Optional Direct Casting and Direct Shouts reduce equipment churn while retaining configurable timing, cancellation, and feedback. |
| Readable feedback | Left/right-hand state, activation progress, shout stages and cooldowns, casting state, selected ammo, and low-ammo indicators. |
| Expanded item support | Improved handling for renamed and enchanted equipment, scripted miscellaneous items, throwables, books, instruments, transformation skills, and other mod-added actions. |
| Adaptive presentation | Global scaling, automatic screen-bound scaling, configurable wheel geometry, slot text fitting, and resolution-aware layouts. |
| dMenu configuration | In-game controls for behavior, keybinds, layout, sorting, visuals, sounds, indicators, and optional features. |
| Performance and integrations | An Ammo Wheel performance mode plus optional integrations including I4 icons, Dynamic Grip, Action Hotkeys Bridge, and OStim where installed. These companion integrations are not required for core wheel interaction. |

## Screenshots

### Main Wheel scaling

![Before-and-after comparison of Wheeler Refined automatic scaling](images/refined/main-wheel-scaling.png)

### Ammo Wheel

![Ammo Wheel displaying arrows and bolts around the player](images/refined/ammo-wheel.png)

### dMenu customization

![Wheeler Refined Ammo Wheel settings inside dMenu](images/refined/dmenu-customization.png)

### Low-ammo indicator

![Low-ammo warning displayed on an Ammo Wheel slot](images/refined/low-ammo-indicator.png)

## Installation

Use the [Wheeler Refined Nexus page](https://www.nexusmods.com/skyrimspecialedition/mods/167380) as the authoritative download and installation guide.

### Required

- [SKSE](https://skse.silverlock.org/) for the Skyrim runtime you use.
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444).
- [Wheeler - Quick Action Wheel of Skyrim](https://www.nexusmods.com/skyrimspecialedition/mods/97345). Wheeler Refined currently relies on the original package and assets for a normal runtime installation.
- The current release of [dMenu NG](https://www.nexusmods.com/skyrimspecialedition/mods/166751). Refined's settings expect its current interface.
- [Wheeler Refined](https://www.nexusmods.com/skyrimspecialedition/mods/167380).

### Recommended or conditional

- [Dragonborn Reskin - Wheeler](https://www.nexusmods.com/skyrimspecialedition/mods/100043) is strongly recommended by the author and was used while many improvements were developed.
- Install [Skyrim Souls and Wheeler Slow Time Fix](https://www.nexusmods.com/skyrimspecialedition/mods/174828) when using Skyrim Souls.
- [Typing Mode](https://www.nexusmods.com/skyrimspecialedition/mods/164851) can help when text input conflicts with other menus.

### Normal mod-manager order

1. Install original Wheeler.
2. Install dMenu NG.
3. Install Wheeler Refined after both and allow it to overwrite where appropriate.
4. Install optional visual reskins last so their assets win conflicts.

Open dMenu in game to configure **Wheeler Behaviour**, **Ammo Wheel**, controls, styles, and optional integrations.

## Core Wheeler Concepts

Wheeler uses a compact hierarchy:

```text
Wheels -> Wheel -> Slot -> Item
```

You can move among multiple wheels, place multiple slots on each wheel, and cycle several items within a slot.

![Browsing multiple Wheeler wheels](images/hierarchy_wheel.gif)

Open Wheeler while browsing the inventory or magic menu to enter edit mode. From there you can create and reorder wheels or slots, insert the highlighted item, and remove entries. During normal play, use a slot to equip, consume, or activate its current item.

![Inserting an inventory item into a Wheeler slot](images/item_insertion.gif)

Wheel state is stored per save in the SKSE co-save, including the identity needed to distinguish differently enchanted, poisoned, or tempered instances.

## Configuration and Compatibility

- **dMenu:** Most behavior, control, layout, sorting, indicator, and appearance options are exposed in game. Factory/default INIs remain the update-safe source; dMenu creates and maintains live user overrides.
- **Performance mode:** Available in the Ammo Wheel settings. It replaces animated presentation with a simpler rendering path for lower-overhead use.
- **Reskins:** Main Wheel and Ammo Wheel assets can be replaced. Install reskins after Refined and keep the supplied defaults as fallbacks. See the [Ammo Wheel reskin manual](docs/Reskin_Manual_AmmoWheel.md).
- **Fonts and glyphs:** Configure `Data/SKSE/Plugins/wheeler/resources/fonts/FontConfig.ini` and supply a compatible font in the corresponding language folder when the bundled source assets do not cover your glyphs.
- **Input:** Controller and mouse/keyboard paths are both supported, but heavily customized control maps or menu mods can still conflict. See [Input Compatibility](docs/INPUT_COMPATIBILITY.md).
- **Optional integrations:** Integrations activate only when their companion mod or API is available and configured. Review the relevant settings and logs before reporting a compatibility issue.

Compatibility depends on each load order, input setup, UI stack, and game runtime. Please report reproducible combinations rather than assuming universal compatibility.

## Building from Source

The project requires a C++20 Windows toolchain, CMake 3.22 or newer, vcpkg, and the pinned CommonLibSSE-NG revision. With `VCPKG_ROOT` set and CommonLib checked out:

```powershell
cmake --preset vs2022-windows -B build-public `
  -DCOPY_OUTPUT=OFF `
  -DCommonLibSSEPath_NG='C:\src\CommonLibSSE-NG'

cmake --build build-public --config Release --target wheeler
```

The DLL is produced at `build-public/src/Release/wheeler.dll`. See [BUILDING.md](BUILDING.md) for the exact pinned revisions, a from-zero Windows setup, deployment builds, and the optional API sample.

> [!WARNING]
> Building `wheeler.dll` alone is not necessarily a complete end-user installation. Wheeler Refined currently depends on the original Wheeler package/assets for normal runtime installation; use the Nexus packages and installation order above for play.

## Developer Documentation

- [Build guide](BUILDING.md)
- [API integration summary](docs/API_INTEGRATION_SUMMARY.md)
- [API logging reference](docs/API_LOGGING_REFERENCE.md)
- [Input compatibility](docs/INPUT_COMPATIBILITY.md)
- [Ammo Wheel reskin manual](docs/Reskin_Manual_AmmoWheel.md)
- [Action Hotkeys bridge API sample](tools/action_hotkeys_bridge_api_sample/README.md)
- [Wheeler preview tool](tools/wheeler_preview/README.md)

Contributions are welcome through [issues](https://github.com/c0kadam/Wheeler-Refined/issues) and pull requests. Please read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting code or assets.

## License and Attribution

Wheeler Refined is distributed as a combined work under [GPL-3.0-only](LICENSE).

It is a modified derivative of [Wheeler](https://github.com/D7ry/wheeler) by dTry/D7ry. Original Wheeler source remains under the [BSD 3-Clause License](LICENSES/BSD-3-Clause-Wheeler.txt), copyright © 2024 dTry. The GPL selection for Wheeler Refined does not relicense the original project or other third-party components.

See [NOTICE.md](NOTICE.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for provenance, credits, and third-party terms.
