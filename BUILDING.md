# Building Wheeler Refined

This guide describes the supported public Windows build. It separates what the project requires from the exact toolchain used for the audited Wheeler Refined 1.3.3 build.

## Hard Requirements

- Windows x64 and Git.
- CMake **3.22 or newer** (`cmake_minimum_required(VERSION 3.22)`).
- Visual Studio 2022 with the **Desktop development with C++** workload and the `v143` toolset used by the public preset.
- A compiler with C++23 support; the `wheeler` target requests `cxx_std_23`.
- A bootstrapped [vcpkg](https://github.com/microsoft/vcpkg) checkout with `VCPKG_ROOT` set.
- An [alandtse/CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) Git checkout at tag `v6.7.0`, exactly commit `3d81614617910e7f34b33d8750881811b5e36445`.

The public preset targets `x64-windows-static-md`. CMake rejects a CommonLib Git checkout at any other revision.

## Audited / Tested Toolchain

The public source gate was run with:

- Wheeler Refined 1.3.3
- CMake 4.2.0
- Visual Studio 2022 17.14.x / MSVC 19.44.x (`v143`)
- Windows SDK 10.0.26100.0
- vcpkg triplet `x64-windows-static-md`
- vcpkg registry baseline `382c5b8a94b3d6b6286df7a488c7efa8d37313eb`
- CommonLibSSE-NG `v6.7.0`, commit `3d81614617910e7f34b33d8750881811b5e36445` (GPL-3.0-or-later with the upstream modding and linking exceptions)

The exact audited CMake, MSVC, and Windows SDK versions document the verified environment; they are not raised above the requirements expressed by the current build files. `vcpkg-configuration.json` pins the registry baseline, and CMake pins CommonLibSSE-NG. The manifest includes CommonLib 6.7.0's DirectXMath, DirectXTK, fmt, and toml11 requirements in addition to Wheeler's existing dependencies.

## From-Zero Windows Build

The following PowerShell flow creates a no-deployment Release build. Choose different empty dependency paths if `C:\src` is not suitable for your machine.

```powershell
git clone https://github.com/c0kadam/Wheeler-Refined.git
Set-Location Wheeler-Refined

git clone https://github.com/microsoft/vcpkg.git C:\src\vcpkg
C:\src\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = 'C:\src\vcpkg'

git clone https://github.com/alandtse/CommonLibSSE-NG.git C:\src\CommonLibSSE-NG
git -C C:\src\CommonLibSSE-NG checkout 3d81614617910e7f34b33d8750881811b5e36445

cmake --preset vs2022-windows -B build-public `
  -DCOPY_OUTPUT=OFF `
  -DCommonLibSSEPath_NG='C:\src\CommonLibSSE-NG'

cmake --build build-public --config Release --target wheeler
```

The generated plugin is:

```text
build-public\src\Release\wheeler.dll
```

`COPY_OUTPUT=OFF` keeps the build self-contained and does not require `CompiledPluginsPath`. The explicit `-DCommonLibSSEPath_NG=...` cache value is preferred; the `CommonLibSSEPath_NG` environment variable remains a compatibility fallback for existing developer setups.

Building the DLL does not assemble a complete end-user mod. Normal runtime installation currently also relies on the original Wheeler package/assets; see the player installation section in [README.md](README.md).

## Optional Deployment Build

`COPY_OUTPUT` is ON by default for established local workflows. When enabled, `CompiledPluginsPath` must name an existing deployment root containing `SKSE\Plugins`.

```powershell
cmake --preset vs2022-windows -B build-deploy `
  -DCOPY_OUTPUT=ON `
  -DCommonLibSSEPath_NG='C:\src\CommonLibSSE-NG' `
  -DCompiledPluginsPath='C:\staging\Wheeler'

cmake --build build-deploy --config Release --target wheeler
```

The post-build step copies the DLL, PDB, README, and license/notice files into the supplied deployment root.

## Optional Action Hotkeys Bridge API Sample

The `WheelerBridgeApiSample` SKSE test plugin is excluded from ordinary builds. Enable it deliberately when developing against the Action Hotkeys bridge API:

```powershell
cmake --preset vs2022-windows -B build-api-sample `
  -DCOPY_OUTPUT=OFF `
  -DCommonLibSSEPath_NG='C:\src\CommonLibSSE-NG' `
  -DWHEELER_BUILD_API_SAMPLE=ON

cmake --build build-api-sample --config Release --target WheelerBridgeApiSample
```

See [the sample README](tools/action_hotkeys_bridge_api_sample/README.md) for runtime configuration and API usage.

## Build Notes

- The manifest retains ImGui's current `dx11-binding` and `win32-binding` features.
- Wheeler uses ImGui's built-in font rasterizer (`ENABLE_FREETYPE = 0`) rather than compiling the bundled `imgui_freetype.cpp` implementation.
- Skyrim SE and AE support are enabled by default; VR support is disabled by default in the current build configuration.
- This CommonLib/toolchain migration does not certify a new Skyrim runtime. The input hook at `PollInputDevices + 0x7B`, D3D initialization hook at AE `+0x275`, and Present hook at `+0x9` still require separate Skyrim 1.7.99 binary and runtime verification.
