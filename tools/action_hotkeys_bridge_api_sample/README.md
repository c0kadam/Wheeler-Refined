# Wheeler Bridge API Sample

Small SKSE test plugin for the Action Hotkeys bridge API.

What it exercises:
- `UpsertExternalHotkey`
- `RemoveExternalHotkey`
- `ClearExternalHotkeys`

How it works:
- The plugin reads [`WheelerBridgeApiSample.ini`](../../Data/SKSE/Plugins/WheelerBridgeApiSample.ini).
- On `kDataLoaded`, `kNewGame`, or `kPostLoadGame`, it resolves `GetWheelerAPI()` from `wheeler.dll`.
- It performs one configured action and then stops until the next game restart.

Modes:
- `UpsertDemo`: injects the enabled `[Hotkey1]..[HotkeyN]` sections.
- `RemoveOne`: by default seeds the configured hotkeys, then removes `RemoveSourceTag`.
- `ClearAll`: optionally seeds the configured hotkeys first, then clears all API-injected external hotkeys.
- `None`: no-op.

Build:
```powershell
cmake --build build --config Release --target WheelerBridgeApiSample -- /p:PostBuildEventUseInBuild=false
```

Runtime notes:
- `ActionHotkeysBridge AutoInjection=false` is the cleanest way to test pure API injection.
- `SeedBeforeMutatingAction=true` makes `RemoveOne` and `ClearAll` meaningful on a fresh game launch, because API-injected bridge slots are transient and are not serialized by Wheeler.
- `WheelNumber=0` means auto placement.
- `EntryIndex=-1` means auto placement inside the target wheel.
- `IconPath` can be absolute or relative; leave it empty for text-only slots.

Minimal direct usage example:
```cpp
#define WHEELER_API
#include "API/WheelerAPI.h"

auto* api = GetWheelerAPI();
if (api && api->version >= WheelerAPI::API_VERSION) {
    WheelerAPI::ExternalHotkeyConfig cfg{
        .sourceTag = "MyMod.Journal",
        .displayName = "Journal",
        .scanCode = 0x24,
        .modifier = 0,
        .iconPath = nullptr,
        .iconTintARGB = 0xFF3CD1BC,
        .wheelNumber = 1,
        .entryIndex = -1,
        .flags = 0
    };
    api->UpsertExternalHotkey(&cfg);
}
```
