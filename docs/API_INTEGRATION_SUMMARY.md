# Wheeler External API Integration - Summary

## Overview

This document summarizes the integration of the External API into the Wheeler project, enabling compatibility with client plugins like **On Cue**.

---

## Source Reference

**Modder's Repository:** `https://github.com/LandingCrew/wheeler` (branch: `feature/external-api`)

### Files Used as Reference
| Modder's File | Usage |
|---------------|-------|
| `src/bin/API/WheelerAPI.h` | Adapted struct definitions, result codes, callbacks |
| `src/bin/API/WheelerAPI.cpp` | Reference for implementation patterns |
| `src/bin/Wheeler/Wheeler.h` | Accessor method signatures |

---

## New Files Created

| File | Lines | Description |
|------|-------|-------------|
| `src/bin/API/WheelerAPI.h` | 195 | API header with `IWheelerAPI` struct, result codes, callbacks |
| `src/bin/API/WheelerAPI.cpp` | 480 | Full API implementation with 25+ functions |

---

## Modified Files

### CMakeLists.txt
```diff
+ WHEELER_EXPORTS  # Enable DLL export for External API
```

### Wheeler.h
```diff
+ #include "bin/API/WheelerAPI.h"

  static void Init() {
      _wheels.emplace_back(std::make_unique<Wheel>());
+     WheelerAPI::SetInitialized(true);
  }

+ // External API Accessors
+ static std::vector<std::unique_ptr<Wheel>>& GetWheels();
+ static std::shared_mutex& GetWheelDataLock();
+ static Wheel* GetWheelByIndex(int a_index);
+ static int GetWheelCount();
```

### Wheeler.cpp
```diff
+ Wheel* Wheeler::GetWheelByIndex(int a_index) {
+     if (a_index < 0 || a_index >= static_cast<int>(_wheels.size())) {
+         return nullptr;
+     }
+     return _wheels[a_index].get();
+ }

  void Wheeler::OpenWheeler() {
      // ... existing code ...
+     WheelerAPI::NotifyWheelStateChanged(true);
  }

  void Wheeler::CloseWheeler() {
      // ... existing code ...
+     WheelerAPI::NotifyWheelStateChanged(false);
  }
```

### WheelEntry.h
```diff
+ // External API Accessors
+ WheelItem* GetItem(int a_index);
+ bool RemoveItemAt(int a_index);
```

### WheelEntry.cpp
```diff
+ WheelItem* WheelEntry::GetItem(int a_index) {
+     std::shared_lock<std::shared_mutex> lock(this->_lock);
+     if (a_index < 0 || a_index >= static_cast<int>(_items.size())) {
+         return nullptr;
+     }
+     return _items[a_index].get();
+ }

+ bool WheelEntry::RemoveItemAt(int a_index) {
+     std::unique_lock<std::shared_mutex> lock(this->_lock);
+     if (a_index < 0 || a_index >= static_cast<int>(_items.size())) {
+         return false;
+     }
+     _items.erase(_items.begin() + a_index);
+     // ... adjust selected item index ...
+     return true;
+ }
```

### WheelItemFactory.h
```diff
+ // External API
+ static std::shared_ptr<WheelItem> MakeWheelItemFromFormID(RE::FormID a_formID, uint16_t a_uniqueID = 0);
```

### WheelItemFactory.cpp
```diff
+ std::shared_ptr<WheelItem> WheelItemFactory::MakeWheelItemFromFormID(RE::FormID a_formID, uint16_t a_uniqueID) {
+     // Auto-detect form type and create appropriate WheelItem
+     // Supports: Spell, Shout, Weapon, Armor, Ammo, Alchemy, Scroll, Light, Misc, Book
+ }
```

---

## Key Differences from Modder's Implementation

| Aspect | Modder's Version | Our Version |
|--------|------------------|-------------|
| **Codebase** | Original D7ry Wheeler (22KB) | Extended Wheeler with AmmoWheel, InstantSpell, RTU (128KB) |
| **FormID Item Creation** | `MakeWheelItemFromFormID` | Same, adapted to work with our `WheelItemMutable` system |
| **Callbacks** | Full change tracking in edit mode | Simplified (wheel state only, edit mode tracking optional) |
| **Logging** | Uses `INFO()` macro | Uses `logger::info()` / `logger::debug()` |

---

## API Functions Implemented

### Status
- `IsInitialized()`, `IsInEditMode()`, `IsWheelOpen()`

### Managed Wheel Lifecycle
- `CreateManagedWheel()`, `DeleteManagedWheel()`, `IsManagedWheel()`

### Wheel Queries
- `GetWheelCount()`, `GetActiveWheelIndex()`, `SetActiveWheelIndex()`, `IsWheelEmpty()`

### Entry Management
- `GetEntryCount()`, `AddEntry()`, `DeleteEntry()`, `IsEntryEmpty()`

### Item Management
- `GetItemCount()`, `AddItemByFormID()`, `RemoveItem()`, `ClearEntry()`
- `GetItemFormID()`, `GetSelectedItemIndex()`, `SetSelectedItemIndex()`

### Callbacks
- `RegisterItemActivatedCallback()`, `RegisterEditModeCallback()`, `RegisterWheelStateCallback()`
- `UnregisterItemActivatedCallback()`, `UnregisterEditModeCallback()`, `UnregisterWheelStateCallback()`

---

## What Was NOT Changed

- **No existing function logic modified** - all changes are additive
- **No existing class structures altered**
- **No breaking changes to serialization format**
- **AmmoWheel, InstantSpell, RTU functionality unchanged**

---

## Critical Implementation: Managed Wheel Exclusion

```cpp
// Wheeler.cpp - SerializeIntoJsonObj
for (size_t i = 0; i < _wheels.size(); ++i) {
    // Skip managed wheels - they are owned by API clients and should not be saved
    if (WheelerAPI::IsManagedWheelIndex(static_cast<int32_t>(i))) {
        continue;
    }
    // ... serialize wheel ...
}
```

This ensures that API-created wheels are NOT saved to the user's config file.

---

## Possible future extensions

- Edit mode change callbacks (`enterEditMode`/`exitEditMode`)
- Managed wheel label display in UI
- ItemActivated callback hook
