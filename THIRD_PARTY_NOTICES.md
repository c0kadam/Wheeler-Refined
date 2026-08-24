# Wheeler Refined Third-Party Notices

Wheeler Refined is distributed as a combined work under `GPL-3.0-only`.
Upstream projects and components listed below retain their own licenses. The
GPL selection for Wheeler Refined does not retroactively relicense them.

An "audited revision" identifies the source snapshot used for the provenance
comparison. Where the repository does not pin an upstream revision, that fact
is recorded rather than inventing a version; dependency pinning is documented
in the build instructions.

## A. Source base and inherited implementations

### Original Wheeler

- Upstream: [D7ry/wheeler](https://github.com/D7ry/wheeler)
- Audited revision: `5124a10a24a841886fa3a16510764325f37c69f8`
- License: BSD 3-Clause
- Use: source base from which Wheeler Refined was derived
- Bundled status: inherited and modified source is part of this repository;
  the exact upstream BSD notice is preserved at
  `LICENSES/BSD-3-Clause-Wheeler.txt`

### LamasTinyHUD

- Upstream: [mlthelama/LamasTinyHUD](https://github.com/mlthelama/LamasTinyHUD)
- Audited revision: `dd1794c46b1f87cbf04a5d60968facbed0605d02`
- License: GNU GPL version 3
- Use: substantial texture-loading methods, Texture/Image mapping, renderer
  hook patterns, `Drawer::draw_texture`, and alchemy/potion classification
  implementations incorporated into original Wheeler and retained or adapted
  in Wheeler Refined
- Bundled status: derived implementation is present in source; no standalone
  LamasTinyHUD binary is bundled

## B. Adapted or copied implementations and interfaces

### StopAutomaticWeaponDrawNG

- Upstream: [jolly-gopher/StopAutomaticWeaponDrawNG](https://github.com/jolly-gopher/StopAutomaticWeaponDrawNG)
- Audited revision: `98df40fbe6996d90da072346b6eef27fd192f94e`
- License: GNU GPL version 3
- Use: the technique in `src/bin/AutoDrawPatch.h` and
  `src/bin/AutoDrawPatch.cpp` was adapted from this project
- Bundled status: adapted source is present; no upstream binary is bundled;
  the integration is dormant because its activation call is disabled

### OStimNG Thread API

- Upstream: [VersuchDrei/OStimNG](https://github.com/VersuchDrei/OStimNG)
- Audited revision: `95d9720ee3633896e893127d003dad7375459d50`
- License: GNU GPL version 3
- Use: the exact official `skse/src/ModAPI/OstimNG-API-Thread.h` header is
  vendored at `src/include/third_party/ostim/OstimNG-API-Thread.h`; Wheeler's
  wrapper compiles against its authoritative declarations
- Bundled status: the official header is bundled; `OStim.dll` is neither
  bundled nor linked and is discovered at runtime with
  `GetModuleHandleA`/`GetProcAddress`

### MaxsuDetectionMeter

- Upstream: [max-su-2019/MaxsuDetectionMeter](https://github.com/max-su-2019/MaxsuDetectionMeter)
- Audited revision: `fcc5ef75d6cdabc63db0b214bc61fc272a5b22cf`
- License: MIT
- Use: renderer/ImGui FreeType integration patterns in
  `src/bin/Rendering/RenderManager.cpp`
- Bundled status: adapted implementation is present; no upstream binary is
  bundled

## C. Build dependencies

### CommonLibSSE-NG

- Upstream: [alandtse/CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG)
- Audited release: `v6.7.0`, revision `3d81614617910e7f34b33d8750881811b5e36445`
- License: GPL-3.0-or-later with the Modding Exception and GPL-3.0 Linking Exception (with Corresponding Source)
- Use: SKSE/CommonLib headers and link target used to build Wheeler Refined
- Bundled status: dependency source is not vendored; binary distributions must
  retain the applicable CommonLibSSE-NG notice and upstream exceptions in
  `Data/SKSE/Plugins/third-party-notices/`

### DirectXMath and DirectXTK

- Upstream: [microsoft/DirectXMath](https://github.com/microsoft/DirectXMath) and [microsoft/DirectXTK](https://github.com/microsoft/DirectXTK)
- Audited revision: resolved through the pinned vcpkg registry baseline
- License: MIT
- Use: direct CommonLibSSE-NG 6.7.0 build and link requirements
- Bundled status: linked build dependencies, not vendored; generated vcpkg
  notices are retained under `Data/SKSE/Plugins/third-party-notices/`

### fmt

- Upstream: [fmtlib/fmt](https://github.com/fmtlib/fmt)
- Audited revision: resolved through the pinned vcpkg registry baseline
- License: MIT
- Use: direct CommonLibSSE-NG 6.7.0 formatting dependency
- Bundled status: linked build dependency, not vendored; its generated vcpkg
  notice is retained under `Data/SKSE/Plugins/third-party-notices/`

### toml11

- Upstream: [ToruNiina/toml11](https://github.com/ToruNiina/toml11)
- Audited revision: resolved through the pinned vcpkg registry baseline
- License: MIT
- Use: direct CommonLibSSE-NG 6.7.0 TOML dependency
- Bundled status: linked/header build dependency, not vendored; its generated
  vcpkg notice is retained under `Data/SKSE/Plugins/third-party-notices/`

### Dear ImGui

- Upstream: [ocornut/imgui](https://github.com/ocornut/imgui)
- Audited revision: no repository-level revision pin; resolved through the
  `imgui` vcpkg manifest dependency
- License: MIT
- Use: immediate-mode user-interface rendering, including DX11 and Win32
  bindings
- Bundled status: linked build dependency; Dear ImGui FreeType builder source
  files also remain in `src/include/lib/` and retain the upstream license

### FreeType

- Upstream: [freetype/freetype](https://github.com/freetype/freetype)
- Audited revision: no repository-level revision pin; resolved through the
  `freetype` vcpkg manifest dependency
- License: FreeType License (FTL) or GPL-2.0-or-later, at the recipient's option
- Use: font rasterization through Dear ImGui's FreeType integration
- Bundled status: linked build dependency, not vendored; its distribution
  notice is provided under `Data/SKSE/Plugins/third-party-notices/`

The vcpkg manifest also resolves supporting libraries such as spdlog, xbyak,
nlohmann-json, SimpleIni, rapidcsv, and their transitive dependencies. Their
generated copyright/license notices are retained under
`Data/SKSE/Plugins/third-party-notices/`.

## D. Runtime optional integrations

These integrations use public APIs, module discovery, or interoperable data.
Their upstream binaries are not bundled by Wheeler Refined.

### iEquipUtil

- Upstream: [isoku-tech/iEquipUtil](https://github.com/isoku-tech/iEquipUtil)
- Audited revision: `34ff707f1a2654eded7023dd66237e4f6d5df578`
- License: MIT
- Use: optional item identity and compatibility behavior
- Bundled status: not bundled

### Immersive Equipment Displays (IED)

- Upstream: [SlavicPotato/ied-dev](https://github.com/SlavicPotato/ied-dev)
- Audited revision: `d8e9d33002141244bcb1d377b0563f89511c8d94`
- License: MIT
- Use: optional equipment-display interoperability
- Bundled status: not bundled

### Precision

- Upstream: [Ersh1/Precision](https://github.com/Ersh1/Precision)
- Audited revision: `df3cd228795bf32288de795dc6eb3b38e46abf34`
- License: MIT
- Use: optional runtime compatibility/integration
- Bundled status: not bundled

### Inventory Interface Information Injector (InventoryInjector/I4)

- Upstream: [Exit-9B/InventoryInjector](https://github.com/Exit-9B/InventoryInjector)
- Audited revision: `45d57139f9c51cd1ca562a401cc960244aae68b1`
- License: MIT
- Use: optional inventory icon/category interoperability
- Bundled status: not bundled

### dMenu

- Upstream: [D7ry/dmenu](https://github.com/D7ry/dmenu)
- Audited revision: `1b01733cde3ad3d98cc40cb3addaa870026ac1a4`
- License: MIT
- Use: runtime settings user interface; Wheeler Refined ships compatible
  custom-settings JSON files
- Bundled status: dMenu itself is not bundled

OStimNG is also an optional runtime integration; its interface provenance and
runtime-only discovery are recorded in section B.

## E. Bundled third-party source

### NanoSVG and NanoSVG rasterizer

- Upstream: [memononen/nanosvg](https://github.com/memononen/nanosvg)
- Audited revision: current vendored `src/include/lib/nanosvg.h` and
  `src/include/lib/nanosvgrast.h`; no upstream revision is recorded in this
  repository
- License: zlib-style permissive license stated in the headers
- Use: SVG parsing and rasterization
- Bundled status: source is bundled; the exact embedded notices are reproduced
  in `Data/SKSE/Plugins/third-party-notices/BUNDLED_SOURCE_NOTICES.txt`

### stb_image

- Upstream: [nothings/stb](https://github.com/nothings/stb)
- Audited revision: current vendored `src/include/lib/stb_image.h`; no upstream
  revision is recorded in this repository
- License: MIT or Unlicense, at the recipient's option
- Use: raster image decoding
- Bundled status: source is bundled; its license notice is reproduced in
  `Data/SKSE/Plugins/third-party-notices/BUNDLED_SOURCE_NOTICES.txt`

### Dear ImGui FreeType builder files

- Upstream: Dear ImGui (project identified in section C)
- Audited revision: current vendored `src/include/lib/imgui_freetype.h` and
  `src/include/lib/imgui_freetype.cpp`; no upstream revision is recorded in
  this repository
- License: MIT
- Use: retained source for the Dear ImGui/FreeType renderer integration; the
  current CMake source list excludes the bundled `.cpp`
- Bundled status: source files are bundled and retain their upstream notice

## Distribution notice locations

- Wheeler Refined GPL-3.0-only text: `LICENSE`
- Original Wheeler BSD text: `LICENSES/BSD-3-Clause-Wheeler.txt`
- Bundled-source notices and build-dependency notices:
  `Data/SKSE/Plugins/third-party-notices/`

Redistributors must preserve the license and notice files applicable to the
source or binaries they distribute.
