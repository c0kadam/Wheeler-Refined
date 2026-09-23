from __future__ import annotations

import re
import sys
from pathlib import Path

from static_cpp_checks import extract_braced_body, normalize_cpp


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "bin"


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="strict")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    failures: list[str] = []

    cache_header = read("src/bin/Utilities/InventorySnapshotCache.h")
    cache_source = read("src/bin/Utilities/InventorySnapshotCache.cpp")
    main_wheel = read("src/bin/Wheeler/Wheeler.cpp")
    ammo_wheel = read("src/bin/Wheeler/AmmoWheel.cpp")
    weapon = read("src/bin/Wheeler/WheelItems/WheelItemWeapon.cpp")

    require(
        not re.search(r"InventoryItemMap\s+_[A-Za-z]", cache_header),
        "InventorySnapshotCache owns an InventoryItemMap field",
        failures,
    )
    require("CaptureFresh(" in cache_header and "CaptureFresh(" in cache_source,
            "fresh snapshot API is missing", failures)
    require("g_mainWheelInventorySnapshot.Get(" not in main_wheel,
            "main wheel still calls the persistent Get API", failures)
    require("g_ammoWheelInventorySnapshot.Get(" not in ammo_wheel,
            "ammo wheel still calls the persistent Get API", failures)
    require(
        "RE::TESObjectREFR::InventoryItemMap inv;\n\t\tg_mainWheelInventorySnapshot.CaptureFresh(" in main_wheel,
        "main wheel does not render from a frame-local fresh snapshot",
        failures,
    )
    require(
        "RE::TESObjectREFR::InventoryItemMap imap;\n\tg_ammoWheelInventorySnapshot.CaptureFresh(" in ammo_wheel,
        "ammo wheel does not render from a frame-local fresh snapshot",
        failures,
    )

    persistent_pointer = re.compile(
        r"^\s*(?:RE::)?(?:ExtraDataList|InventoryEntryData)\s*\*\s*_[A-Za-z]",
        re.MULTILINE,
    )
    for header in SRC.rglob("*.h"):
        require(
            not persistent_pointer.search(header.read_text(encoding="utf-8", errors="strict")),
            f"persistent inventory pointer field found in {header.relative_to(ROOT)}",
            failures,
        )

    direct_mutation = re.compile(r"->(?:EquipObject|UnequipObject|EquipSpell|EquipShout)\s*\(")
    for source in SRC.rglob("*.cpp"):
        text = source.read_text(encoding="utf-8", errors="strict")
        require(
            not direct_mutation.search(text),
            f"engine equip mutation bypasses the invalidation wrapper in {source.relative_to(ROOT)}",
            failures,
        )

    hooks = read("src/bin/Hooks.cpp")
    require("InventorySnapshotCache::RunMutation" in hooks,
            "inventory hooks do not wrap mutation calls", failures)
    pickup_boundary = hooks.find("// Resolve and modify the world reference's xList only before PickUpObject.")
    pickup_call = hooks.find("_PickUpObject(this, a_object, a_count, a_arg3, a_playSound)", pickup_boundary)
    post_pickup_body = hooks[pickup_call:] if pickup_call >= 0 else hooks
    require("a_object->extraList" not in post_pickup_body,
            "PickUpObject hook dereferences the world reference xList after mutation", failures)
    require("signatureMatch && completelyUnworn" in weapon,
            "row-safe second-hand resolver no longer prefers an unworn signature match", failures)
    require("extraData = nullptr;\n\t\t\t\tinv.clear();\n\t\t\t\tInventorySnapshotCache::UnequipObject" in weapon,
            "weapon opposite-hand mutation does not discard its prior physical member", failures)
    require("post-unequip live resolve failed" in weapon,
            "weapon path does not perform a fresh post-unequip resolve", failures)

    try:
        hand_memory = extract_braced_body(main_wheel, "static void UpdateHandMemory()")
        clear_occupant = extract_braced_body(hand_memory, "auto clearIgnoredOccupantForRestore =")
    except ValueError as error:
        failures.append(str(error))
        hand_memory = ""
        clear_occupant = ""

    clear_occupant_norm = normalize_cpp(clear_occupant)
    preclear = re.search(r"inventory\s*\.\s*clear\s*\(\s*\)", clear_occupant_norm)
    clean_slot = re.search(r"Utils::Slot::CleanSlot\s*\(", clear_occupant_norm)
    refresh = re.search(
        r"inventorySnapshotReady\s*=\s*Utils::Inventory::TryGetInventorySnapshot\s*\(",
        clear_occupant_norm,
    )
    require(
        bool(preclear and clean_slot and refresh and preclear.start() < clean_slot.start() < refresh.start()),
        "HandMemory CleanSlot is not bounded by pre-mutation map discard and post-mutation refresh",
        failures,
    )
    if clean_slot and refresh:
        post_mutation = clear_occupant_norm[clean_slot.end():refresh.start()]
        require(
            not re.search(r"NormalizeRestorableHandFormID\s*\(\s*inventory\b", post_mutation),
            "HandMemory consults the pre-clean inventory map before refreshing it",
            failures,
        )

    hand_memory_norm = normalize_cpp(hand_memory)
    right_clear = hand_memory_norm.find(
        "clearIgnoredOccupantForRestore(false, g_handMemory.memRight, curRight)")
    left_clear = hand_memory_norm.find(
        "clearIgnoredOccupantForRestore(true, g_handMemory.memLeft, curLeft)")
    ready_guards = [
        match.start()
        for match in re.finditer(
            r"if\s*\(\s*!\s*inventorySnapshotReady\s*\)\s*\{\s*return\s*;\s*\}",
            hand_memory_norm,
        )
    ]
    require(
        right_clear >= 0 and left_clear > right_clear and
        len(ready_guards) >= 2 and
        right_clear < ready_guards[0] < left_clear < ready_guards[1],
        "HandMemory does not require a refreshed right-hand snapshot before evaluating the left hand",
        failures,
    )

    reconcile_match = re.search(
        r'reconcilePendingRestoreAgainstCurrentOccupant\s*\(\s*"RIGHT"\s*,\s*curRight\s*,\s*'
        r'g_handMemory\.memRight\s*,\s*g_handMemory\.memRightWeapon\s*,\s*'
        r'g_handMemory\.rightRestoreRetriesUsed\s*\)',
        hand_memory_norm,
    )
    reconcile_right = reconcile_match.start() if reconcile_match else -1
    restore_mutation = hand_memory_norm.find("EquipRestoreTargetToHand(", reconcile_right)
    final_discard = hand_memory_norm.find("inventory.clear()", reconcile_right)
    require(
        reconcile_right >= 0 and final_discard > reconcile_right and
        restore_mutation > final_discard,
        "HandMemory does not discard its final snapshot before restore equip mutation",
        failures,
    )

    for forbidden in ("WeaponBoundary", "StackMemberDiag", "DIAGNOSTIC G", "seq="):
        require(forbidden not in weapon, f"diagnostic marker remains in production weapon path: {forbidden}", failures)

    if failures:
        print("Inventory snapshot lifetime verification FAILED:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("Inventory snapshot lifetime verification PASSED")
    print("- render snapshots are frame-local")
    print("- snapshot coordinator stores no inventory map or raw xList")
    print("- equip mutations use generation invalidation wrappers")
    print("- weapon post-mutation physical-member resolution is fresh")
    print("- row-safe unworn logical-signature preference is preserved")
    print("- HandMemory discards and refreshes its snapshot across each CleanSlot")
    print("- cross-hand evaluation is gated on successful post-mutation refresh")
    return 0


if __name__ == "__main__":
    sys.exit(main())
