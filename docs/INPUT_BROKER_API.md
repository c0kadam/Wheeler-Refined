# Input Broker API (Wheeler Refined)

## Purpose

`wheeler.dll` now exposes an optional input arbitration service for wheel-style plugins.
It solves cross-plugin conflicts by combining:

- key reservation (with priority), and
- a single active input owner while a wheel is open.

This allows deterministic coexistence even if hook order changes.

## Export

```cpp
extern "C" WHEELER_API const WheelerAPI::WheelerInputBrokerAPI* GetInputBrokerAPI(uint32_t requestedVersion);
```

- Request version: `WheelerAPI::INPUT_BROKER_API_VERSION` (`1`).
- Returns `nullptr` if unsupported/unavailable.
- Existing `GetWheelerAPI()` users remain fully compatible.

## API Surface

Declared in `src/bin/API/WheelerAPI.h`:

- `RegisterReservation(pluginId, device, key, priority, flags)`
- `UnregisterAll(pluginId)`
- `SetActiveOwner(pluginId)`
- `ClearActiveOwner(pluginId)` (no-op unless plugin is current owner)
- `GetActiveOwner()`
- `ShouldProcessKey(pluginIdSelf, device, key, contextFlags)`

### Device

- `InputBrokerDevice::kMKB`
- `InputBrokerDevice::kGamepad`

### Reservation Flags

- `ToggleKey`
- `NavigationKey`
- `CategoryKey`
- `QTakeover`

## Wheeler Refined Integration

### Input path

`src/bin/UserInput/Input.cpp` now calls broker checks before wheel action dispatch:

- MainWheel/AmmoWheel actions are skipped when blocked.
- Blocked keys are not consumed (event chain remains intact).
- Key-state tracking still runs to avoid stuck modifiers.

### MainWheel + AmmoWheel ownership

Ownership sync points:

- `src/bin/Wheeler/Wheeler.cpp`
- `src/bin/Wheeler/AmmoWheel.cpp`

`wheeler.dll` sets owner while either wheel is open and clears it when closed.

### Auto-registered local reservations

`src/bin/InputBroker.cpp` automatically registers Wheeler-owned toggles:

- MainWheel toggle keys
- AmmoWheel toggle keys (including mouse toggle key)

Priorities are configurable.

## INI Configuration

`Data/SKSE/Plugins/wheeler/wheelBehavior.ini`:

```ini
[InputBroker]
Enabled = true
DebugLog = false
Priority_MainWheel = 50
Priority_AmmoWheel = 60
```

- `Enabled=false` makes `ShouldProcessKey` allow all input (broker bypass).
- `DebugLog=true` enables broker decision logs.

## External Plugin Integration (FavWheel pattern)

1. Resolve broker API via `GetInputBrokerAPI(1)`.
2. Register toggle reservations (`priority=100` recommended for dedicated wheel plugins).
3. Optionally reserve favorites takeover keys (`QTakeover`) when owning vanilla favorites input.
4. Call `SetActiveOwner(pluginId)` when wheel opens.
5. Call `ClearActiveOwner(pluginId)` when wheel closes.
6. Use `ShouldProcessKey` before dispatching wheel-specific actions.
7. Call `UnregisterAll(pluginId)` on shutdown/reload paths.

