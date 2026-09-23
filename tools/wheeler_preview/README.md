# Wheeler Preview Tool

An external desktop preview tool for the Wheeler Skyrim mod. This tool allows reskinners and developers to visualize wheel configurations (`AmmoWheel.ini`, `Styles.ini`) and themes without launching the game.

## Features
- **Live Preview**: Renders the wheel using the same logic as the mod (ImGui + custom arc drawing).
- **Config Loading**: Reads `AmmoWheel.ini` and `Styles.ini` from a specified Data folder.
- **Reskin Support**: Visualizes custom colors, sizes, and layouts.
- **Simulation**: Adjustable slot count and start angle for testing different conditions.

## Building
This tool uses CMake and Vcpkg.

1. Ensure you have the project dependencies installed via Vcpkg (SDL2, Glad, ImGui, etc.).
2. From the project root:
   ```bash
   cmake -B build -S .
   cmake --build build --target wheeler_preview
   ```

## Usage
Run the executable `wheeler_preview.exe`.

### UI Controls
- **Data Root**: text input for the path to your Skyrim Data directory (or a mock folder structure).
  - Example: `C:\Games\Skyrim Special Edition\Data`
  - Or a development folder: `.../wheeler-dev/Data`
- **Reload Config**: Button to re-read settings from the specified path.
- **Simulation**: Sliders to change slot count and rotation.

### Supported Configuration
The tool currently reads:
- **Geometry**: `WheelRadius`, `InnerRadiusRatio`, `SlotGapDeg`.
- **Colors**: `UnhoveredColor`, `HoveredColor` (Begin/End gradients).
- **Styles**: Supports `Styles.ini` overrides.

## Implementation Details
- **Rendering**: Uses ImGui with custom primitive drawing (ported from Wheeler source) to render arcs and gradients.
- **Independent**: Does not require SKSE or Skyrim runtime. Defines its own configuration structures to mirror the mod's logic.
