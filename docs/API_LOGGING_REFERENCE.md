# API Logging Reference

This document lists all events logged by the Wheeler External API.
**Log Prefix:** `[WheelerAPI]`

## Log Levels

- **ERROR**: Critical failures, crashes, invalid configurations.
- **WARN**: API usage errors (invalid indices, forms not found, logic errors).
- **INFO**: Significant state changes (wheel creation, items added).
- **DEBUG**: Routine operations, flow tracking, and callback invocations.

## Logged Events Table

### Initialization
| Level | Event | Condition |
|-------|-------|-----------|
| **[INFO]** | `v{version} initialized successfully` | Wheeler::Init() called |
| **[INFO]** | `shutdown` | SetInitialized(false) called |

### Managed Wheel Lifecycle
| Level | Event | Condition |
|-------|-------|-----------|
| **[INFO]** | `CreateManagedWheel: SUCCESS` | Wheel created (logs index, entries, client, managed, label) |
| **[ERROR]** | `CreateManagedWheel failed` | Config is null or numEntries < 1 |
| **[INFO]** | `DeleteManagedWheel: SUCCESS` | Wheel deleted (logs index) |
| **[WARN]** | `DeleteManagedWheel failed` | Not a managed wheel, or attempting to delete the last wheel |
| **[ERROR]** | `DeleteManagedWheel failed` | Invalid wheel index |

### Item Operations
| Level | Event | Condition |
|-------|-------|-----------|
| **[INFO]** | `AddItemByFormID: SUCCESS` | Item added (logs wheel, entry, formID, new itemIndex) |
| **[WARN]** | `AddItemByFormID failed` | Form not found (invalid FormID) |
| **[WARN]** | `AddItemByFormID failed` | Unsupported form type (e.g., trying to add a tree) |
| **[DEBUG]** | `RemoveItem: SUCCESS` | Item removed |
| **[WARN]** | `RemoveItem failed` | Invalid wheel/entry/item index |

### Entry Operations
| Level | Event | Condition |
|-------|-------|-----------|
| **[DEBUG]** | `AddEntry: SUCCESS` | Entry added (logs new index) |
| **[DEBUG]** | `DeleteEntry: SUCCESS` | Entry deleted |
| **[DEBUG]** | `ClearEntry: SUCCESS` | Entry contents cleared |
| **[WARN]** | `... failed` | Invalid wheel/entry index |

### Wheel State
| Level | Event | Condition |
|-------|-------|-----------|
| **[DEBUG]** | `SetActiveWheelIndex: SUCCESS` | Active wheel changed |
| **[WARN]** | `SetActiveWheelIndex failed` | Invalid wheel index |

### Callbacks (Integration)
| Level | Event | Condition |
|-------|-------|-----------|
| **[INFO]** | `...Callback registered` | Client registered a callback |
| **[INFO]** | `...Callback unregistered` | Client unregistered a callback |
| **[DEBUG]** | `Notify...` | Invoking callback (logs params like formID, isOpen) |
| **[ERROR]** | `...Callback threw exception` | **CRITICAL:** Client code crashed/threw during callback |

### General Errors
| Level | Event | Condition |
|-------|-------|-----------|
| **[WARN]** | `... failed: API not initialized` | Calling API functions before Skyrim is ready |
| **[WARN]** | `AdjustManagedIndices...` | Internal index adjustment tracking (Debug) |
