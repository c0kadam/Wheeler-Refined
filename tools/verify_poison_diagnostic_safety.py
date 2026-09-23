from __future__ import annotations

import re
import sys
from pathlib import Path

from static_cpp_checks import extract_braced_body, normalize_cpp


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="strict")


def main() -> int:
    failures: list[str] = []
    wheeler = read("src/bin/Wheeler/Wheeler.cpp")
    policy = read("src/bin/Wheeler/PoisonDiagnosticPolicy.h")
    cmake = read("src/CMakeLists.txt")

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    try:
        watcher = extract_braced_body(policy, "struct Watcher")
        snapshot = extract_braced_body(wheeler, "void EmitPoisonDiagnosticSnapshot(")
        process = extract_braced_body(wheeler, "void ProcessPoisonDiagnosticWatcher()")
    except ValueError as error:
        failures.append(str(error))
        watcher = snapshot = process = ""

    for forbidden in ("ExtraDataList", "InventoryEntryData", "InventoryItemMap"):
        require(forbidden not in watcher, f"poison watcher persists forbidden type/token: {forbidden}")
    require(not re.search(r"\b[A-Za-z_:][A-Za-z0-9_:<>]*\s*\*\s*[A-Za-z_]", watcher),
            "poison watcher persists a raw pointer field")
    require("eventID" in watcher and "candidateFormIDs" in watcher and
            "completionDeadline" in watcher and "updatesRemaining" in watcher,
            "watcher lacks scalar correlation and bounded lifetime fields")
    require("TryGetInventorySnapshot" in snapshot,
            "poison snapshot does not reacquire inventory fresh")
    require("InventoryItemMap inventory" in snapshot and "inventory.clear()" in snapshot,
            "poison snapshot is not a synchronous local map")
    require("std::thread" not in wheeler and "detach(" not in wheeler,
            "poison diagnostics introduced detached/background work")
    require("EmitBoundary" in process and "EmitFinal" in process,
            "post-boundary/follow-up observations are not wired")

    poison_call = wheeler.find("InventorySnapshotCache::EquipObject(aeMan, pc, poison);")
    pre_snapshot = wheeler.rfind("EmitPoisonDiagnosticSnapshot(", 0, poison_call)
    require(poison_call >= 0 and pre_snapshot >= 0 and pre_snapshot < poison_call,
            "pre snapshot is not emitted before the engine poison mutation")
    require(wheeler.count('CancelPoisonDiagnosticWatcher("world_transition_discard")') == 1 and
            wheeler.count('CancelPoisonDiagnosticWatcher("current_world_cancel")') == 1,
            "both lifecycle reset modes do not cancel the watcher")
    require("PoisonDiagnosticVerification.cpp" in cmake and
            "WHEELER_POISON_DIAGNOSTIC_VERIFICATION_MAIN" in cmake,
            "production-shared poison diagnostic verifier target is missing")

    # The diagnostic implementation may observe and log, but it must not expose
    # any new weapon mutation helper of its own.
    policy_norm = normalize_cpp(policy)
    for mutation in ("EquipObject", "UnequipObject", "AddExtra", "RemoveExtra", "CleanSlot"):
        require(mutation not in policy_norm, f"diagnostic policy exposes gameplay mutation: {mutation}")

    if failures:
        print("Poison diagnostic safety verification FAILED:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("Poison diagnostic safety verification PASSED")
    print("- watcher retains scalar event/epoch/FormID/deadline state only")
    print("- pre and post observations use fresh synchronous inventory snapshots")
    print("- lifecycle reset cancels without outgoing-world observation")
    print("- diagnostic policy adds no gameplay mutation")
    return 0


if __name__ == "__main__":
    sys.exit(main())
