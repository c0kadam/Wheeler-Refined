from __future__ import annotations

import re
import sys
from pathlib import Path

from static_cpp_checks import extract_braced_body, normalize_cpp


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="strict")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def body(source: str, marker: str, failures: list[str]) -> str:
    try:
        return normalize_cpp(extract_braced_body(source, marker))
    except ValueError as error:
        failures.append(str(error))
        return ""


def main() -> int:
    failures: list[str] = []
    controls = read("src/bin/UserInput/Controls.cpp")
    wheel_item_h = read("src/bin/Wheeler/WheelItems/WheelItem.h")
    entry = read("src/bin/Wheeler/WheelEntry.cpp")
    wheel = read("src/bin/Wheeler/Wheel.cpp")
    wheeler = read("src/bin/Wheeler/Wheeler.cpp")
    wheeler_h = read("src/bin/Wheeler/Wheeler.h")
    misc = read("src/bin/Wheeler/WheelItems/WheelItemMisc.cpp")
    weapon_h = read("src/bin/Wheeler/WheelItems/WheelItemWeapon.h")

    dispatch = body(controls, "Controls::DispatchResult Controls::Dispatch(", failures)
    require("std::unique_lock lock(_lock)" in dispatch,
            "Controls::Dispatch does not use an explicitly releasable lock", failures)
    require("std::lock_guard" not in dispatch,
            "Controls::Dispatch still uses an unreleasable lock guard", failures)
    require("lock.unlock(); if (armed->onUp)" in dispatch,
            "armed key-up callback is not invoked after unlock", failures)
    require("lock.unlock(); callback();" in dispatch,
            "direct callback is not invoked after unlock", failures)
    require("lock.unlock(); modifiedCallback();" in dispatch,
            "modified callback is not invoked after unlock", failures)
    require("lock.unlock(); for (const std::uint32_t wheelNumber" in dispatch,
            "bridge callback is not invoked after unlock", failures)
    toggle_unlock = dispatch.find("lock.unlock();", dispatch.find("hasToggleBinding"))
    toggle_call = dispatch.find("candidate.onDown()", toggle_unlock)
    toggle_commit = dispatch.find("lock.lock()", toggle_call)
    require(toggle_unlock >= 0 and toggle_unlock < toggle_call < toggle_commit,
            "toggle-down callback is not outside the Controls lock", failures)
    release_unlock = dispatch.find("lock.unlock();", toggle_commit)
    release_call = dispatch.find("releaseDuringCallback()", release_unlock)
    require(release_unlock >= 0 and release_unlock < release_call,
            "release-during-callback onUp is not outside the Controls lock", failures)
    require("capturedBindingGeneration == _bindingGeneration" in dispatch and
            "captured.keyStateGeneration == currentKeyGeneration" in dispatch,
            "toggle armed-state commit lacks binding/per-key generation validation", failures)

    require("Controls::Dispatch(" not in wheeler,
            "Wheeler Update/runtime still routes through Controls::Dispatch", failures)
    for source_name, source in (("Controls", controls), ("Wheel", wheel), ("WheelEntry", entry)):
        require("_transientGameplayDomain" not in source and
                "ExecuteTransientGameplayIfCurrent" not in source,
                f"{source_name} directly acquires the transient gameplay domain", failures)

    payload = body(wheel_item_h, "struct PreparedWheelItemActivation", failures)
    require("std::shared_ptr<WheelItem> selectedItem" in payload,
            "prepared payload does not retain the selected item synchronously", failures)
    for forbidden in ("Wheel*", "WheelEntry", "InventoryEntryData", "ExtraDataList", "InventoryItemMap"):
        require(forbidden not in payload,
                f"prepared payload retains forbidden authority: {forbidden}", failures)

    for marker in (
        "PreparedWheelItemActivation WheelEntry::ActivateItemPrimary(",
        "PreparedWheelItemActivation WheelEntry::ActivateItemSecondary(",
        "PreparedWheelItemActivation WheelEntry::ActivateItemSpecial(",
    ):
        activation = body(entry, marker, failures)
        require("lock.unlock()" not in activation,
                f"{marker} still unlocks and invokes inline", failures)
        require("executeAfterContainerUnlock = item->MayQueueTransientGameplayAction()" in activation and
                "return prepared" in activation,
                f"{marker} does not return a prepared queue-capable handoff", failures)

    for marker in (
        "PreparedWheelItemActivation Wheel::ActivateHoveredEntryPrimary(",
        "PreparedWheelItemActivation Wheel::ActivateHoveredEntrySecondary(",
        "PreparedWheelItemActivation Wheel::ActivateHoveredEntrySpecial(",
    ):
        activation = body(wheel, marker, failures)
        require("NotifyItemActivated" not in activation,
                f"{marker} still notifies API under Wheel lock", failures)
        require("ExecuteTransientGameplayIfCurrent" not in activation,
                f"{marker} executes transient gameplay under Wheel lock", failures)

    prepare = body(wheeler, "PreparedWheelItemActivation Wheeler::PrepareHoveredWheelItemActivation(", failures)
    execute = body(wheeler, "bool Wheeler::ExecutePreparedWheelItemActivation(", failures)
    require("std::shared_lock<std::shared_mutex> wheelDataLock(_wheelDataLock)" in prepare,
            "Wheeler preparation lacks wheel-data protection", failures)
    require("ExecuteTransientGameplayIfCurrent" not in prepare,
            "Wheeler preparation acquires transient gameplay domain", failures)
    require("ExecuteTransientGameplayIfCurrent" in execute,
            "prepared execution lacks epoch-gated transient admission", failures)
    require("a_activation.selectedItem->ActivateItem" in execute,
            "prepared execution does not use the retained item as sole gameplay authority", failures)
    for forbidden in ("_wheelDataLock", "Wheel*", "WheelEntry", "GetHoveredEntryIndex", "GetEntry("):
        require(forbidden not in execute,
                f"prepared execution rediscovers container authority: {forbidden}", failures)
    require("WheelerAPI::NotifyItemActivated" in execute,
            "value-only API notification was not moved to Wheeler", failures)

    immediate = body(misc, "void WheelItemMisc::useItem()", failures)
    clear_pos = immediate.rfind("inventory.clear()")
    equip_pos = immediate.rfind("InventorySnapshotCache::EquipObject")
    require(clear_pos >= 0 and clear_pos < equip_pos,
            "Misc immediate path does not discard its fresh snapshot before EquipObject", failures)
    require("selection.extraList = nullptr" in immediate and
            "selectedExtraList = nullptr" in immediate[equip_pos:],
            "Misc immediate path does not detach/null the raw xList", failures)

    pending = body(wheeler, "void Wheeler::ProcessPendingActions()", failures)
    require(pending.count("selection.extraList = nullptr") >= 2 and
            pending.count("inv.clear()") >= 2,
            "Misc deferred paths do not detach and destroy both pointer-bearing snapshots", failures)
    require("struct PendingMiscItemUse" in wheeler_h and
            "ExtraDataList" not in body(wheeler_h, "struct PendingMiscItemUse", failures) and
            "InventoryEntryData" not in body(wheeler_h, "struct PendingMiscItemUse", failures),
            "PendingMiscItemUse is not scalar-only", failures)

    require("MayQueueTransientGameplayAction() const { return false; }" in wheel_item_h,
            "base non-queueing activation contract changed", failures)
    require("MayQueueTransientGameplayAction" not in weapon_h,
            "WheelItemWeapon overrides the accepted non-queueing contract", failures)

    if failures:
        print("Global activation boundary verification FAILED:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("Global activation boundary verification PASSED")
    print("- Controls callbacks execute after Controls::_lock release")
    print("- prepared queue-capable gameplay executes only from Wheeler's epoch gate")
    print("- prepared payload is pointer/container safe and uses retained item authority")
    print("- Wheel and WheelEntry perform preparation only while container locks are held")
    print("- Misc immediate/deferred snapshots are detached and cleared before mutation")
    print("- weapon activation remains on the non-queueing contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
