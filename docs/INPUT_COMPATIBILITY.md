# Input Compatibility Notes

## Supported Input Types
- `ButtonEvent`: keyboard, mouse buttons, and gamepad buttons are routed through `Controls::Dispatch`.
- `ThumbstickEvent`: right stick is consumed for wheel cursor movement while a wheel is open.
- Gamepad triggers (`LT`/`RT`): routed via mapped button codes and normalized with hysteresis thresholds in `Input.cpp` (`0.55` press, `0.35` release) for reliable edge detection.

## Router Priority
- If MainWheel is open: MainWheel owns wheel-related input and filtered menu-open events.
- Else if AmmoWheel is open: AmmoWheel owns ammo cursor/activation input, then shared controls dispatch runs.
- Else (all wheels closed): controls dispatch runs and only consumes when an action actually consumes (for example matched chord toggle) or when AmmoWheel toggle state changes.

## Chord and Consumption Rules
- Toggle chord mismatch does not consume input.
- Toggle chord match consumes when dispatch result is `Consumed`.
- Toggle release handlers run only when a matching toggle down event armed the binding.

## Debug/Diagnostics
- Enable `[Debug] inputSpy=true` in `wheelBehavior.ini` to log routed decisions.
- Ring buffer size: `[Debug] inputSpyRingBuffer`.
- Live log rate-limit: `[Debug] inputSpyRateLimitMs`.
- One-shot dump hotkey: `[Debug] inputSpyDumpHotkey` (mapped key id, `0` disables).
