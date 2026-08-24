# Building Wheeler Refined

This project is configured for a reproducible Windows build with the following audited toolchain:

- Wheeler Refined 1.3.3
- CMake 4.2.0
- Visual Studio 2022 17.14.x, MSVC 19.44.x (`v143`)
- Windows SDK 10.0.26100.0
- vcpkg triplet `x64-windows-static-md`
- vcpkg registry baseline `382c5b8a94b3d6b6286df7a488c7efa8d37313eb`
- CommonLibSSE-NG commit `b93280e832f263dbef44e44cbe2936622a02f91a` (MIT)

`vcpkg-configuration.json` pins the registry baseline. Do not change the manifest dependencies or their features when reproducing this build.

## Prerequisites

Install the listed Visual Studio workload, Windows SDK, CMake, and a vcpkg checkout. Set `VCPKG_ROOT` to that vcpkg checkout so the public `vs2022-windows` preset can locate its standard toolchain file.

Obtain a CommonLibSSE-NG checkout at the recorded commit. A cache variable takes precedence over the optional environment fallback:

```powershell
git clone https://github.com/CharmedBaryon/CommonLibSSE-NG.git C:\src\CommonLibSSE-NG
git -C C:\src\CommonLibSSE-NG checkout b93280e832f263dbef44e44cbe2936622a02f91a
$env:VCPKG_ROOT = 'C:\src\vcpkg'
```

## Normal no-deployment build

This configuration does not need `CompiledPluginsPath` and never copies files outside its build directory.

```powershell
cmake --preset vs2022-windows -B build-public -DCOPY_OUTPUT=OFF -DCommonLibSSEPath_NG='C:\src\CommonLibSSE-NG'
cmake --build build-public --config Release --target wheeler
```

The optional `CommonLibSSEPath_NG` environment variable remains supported for existing local setups, but `-DCommonLibSSEPath_NG=...` is the preferred explicit form. Git checkouts are validated against the recorded CommonLib commit; a non-Git source distribution emits a diagnostic instead.

## Optional deployment build

`COPY_OUTPUT` remains ON by default for established developer workflows. When it is ON, CMake requires an existing deployment root that already contains `SKSE\Plugins`.

```powershell
cmake --preset vs2022-windows -B build-deploy -DCOPY_OUTPUT=ON -DCommonLibSSEPath_NG='C:\src\CommonLibSSE-NG' -DCompiledPluginsPath='C:\staging\Wheeler'
cmake --build build-deploy --config Release --target wheeler
```

## Optional Action Hotkeys bridge API sample

The `WheelerBridgeApiSample` test plugin is excluded from ordinary builds. Enable it deliberately when developing against that API:

```powershell
cmake --preset vs2022-windows -B build-api-sample -DCOPY_OUTPUT=OFF -DCommonLibSSEPath_NG='C:\src\CommonLibSSE-NG' -DWHEELER_BUILD_API_SAMPLE=ON
cmake --build build-api-sample --config Release --target WheelerBridgeApiSample
```

The build keeps ImGui's current manifest features and uses its built-in font rasterizer (`ENABLE_FREETYPE = 0`); it does not enable ImGui FreeType rendering.
