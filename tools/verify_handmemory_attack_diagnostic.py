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
    wheeler = read("src/bin/Wheeler/Wheeler.cpp")
    wheeler_h = read("src/bin/Wheeler/Wheeler.h")
    input_cpp = read("src/bin/UserInput/Input.cpp")
    policy = read("src/bin/Wheeler/HandMemoryAttackDiagnosticPolicy.h")
    verification = read("src/bin/Wheeler/HandMemoryAttackDiagnosticVerification.cpp")
    cmake = read("src/CMakeLists.txt")

    for forbidden in ("ExtraDataList", "InventoryEntryData", "InventoryItemMap"):
        require(forbidden not in policy,
                f"diagnostic policy retains forbidden engine inventory type {forbidden}", failures)
    require("std::is_trivially_copyable_v<Watcher>" in verification,
            "compile-time scalar watcher verification is missing", failures)
    require("kPostRestoreObservationSeconds = 5.0" in policy,
            "post-completion observation window is not explicitly bounded to five seconds", failures)
    require("kOverallHardLimitSeconds = 6.0" in policy and
            "kPeriodicSampleSeconds = 0.5" in policy and
            "kMaximumFollowupSamples = 40" in policy,
            "diagnostic watcher lacks bounded overall time/sample limits", failures)

    for event in (
        "EXIT_2H_DETECTED",
        "RESTORE_ARMED",
        "BEFORE_RIGHT_RESTORE",
        "AFTER_RIGHT_RESTORE",
        "BEFORE_LEFT_RESTORE",
        "AFTER_LEFT_RESTORE",
        "RESTORE_PENDING_EMPTY",
        "HANDMEMORY_CURRENTLY_CONSIDERS_COMPLETE",
        "ATTACK_DOWN_OBSERVED",
        "ATTACK_UP_OBSERVED",
        "FOLLOWUP_STATE",
        "TRANSACTION_TIMEOUT",
        "TRANSACTION_CANCELLED",
    ):
        require(event in wheeler, f"required diagnostic event is missing: {event}", failures)

    hand_memory = body(wheeler, "static void UpdateHandMemory()", failures)
    require(hand_memory.count("EquipRestoreTargetToHand(") == 2,
            "HandMemory restore call count changed", failures)
    right = hand_memory.find("HandMemoryHand::Right")
    left = hand_memory.find("HandMemoryHand::Left", right + 1)
    require(right >= 0 and left > right,
            "RIGHT-before-LEFT restore ordering changed", failures)
    require("AwaitRight" not in hand_memory and "AwaitBoth" not in hand_memory,
            "final restore state machine is present", failures)
    require(hand_memory.count("DrawWeaponMagicHands") == 2,
            "HandMemory DrawWeaponMagicHands call count changed", failures)

    observe = body(wheeler, "void Wheeler::ObserveHandMemoryAttackInput(", failures)
    require("ObserveOnlyPassThroughUnchanged" in observe,
            "input observer does not enforce observation-only policy", failures)
    require("consumeEvent" not in observe and "Replay" not in observe,
            "diagnostic observer contains input consumption/replay behavior", failures)
    require("ObserveHandMemoryAttackInput(" in wheeler_h,
            "narrow diagnostic input observation entry point is missing", failures)

    hard_lock = input_cpp.find("ConsumedHardLock")
    observe_call = input_cpp.find("Wheeler::ObserveHandMemoryAttackInput(")
    unlink = input_cpp.find("RE::InputEvent* nextEvent", observe_call)
    require(hard_lock >= 0 and observe_call > hard_lock and unlink > observe_call,
            "attack observation is not placed after Wheeler consumption decisions and before dispatch unlink", failures)
    observation_window = input_cpp[max(0, observe_call - 250):observe_call + 400]
    require("consumeEvent =" not in observation_window,
            "attack diagnostic call site modifies consumption state", failures)

    discard = body(wheeler, "void Wheeler::DiscardTransientGameplayStateForWorldTransition(", failures)
    cancel = body(wheeler, "void Wheeler::CancelTransientGameplayStateInCurrentWorld(", failures)
    require("CancelHandMemoryAttackDiagnostic" in discard and
            "CancelHandMemoryAttackDiagnostic" in cancel,
            "both lifecycle reset modes do not cancel the diagnostic transaction", failures)

    require("HandMemoryAttackDiagnosticVerification.cpp" in cmake and
            "WHEELER_HANDMEMORY_ATTACK_DIAGNOSTIC_VERIFICATION_MAIN" in cmake,
            "focused compile-time verifier target is missing", failures)
    canonical_source_root = ROOT / "src/bin/Wheeler"
    for canonical_pattern in (
        "WeaponIdentity*",
        "WeaponFactoryBinding*",
        "WeaponCanonical*",
    ):
        require(not any(canonical_source_root.glob(canonical_pattern)),
                f"inactive canonical source remains: {canonical_pattern}", failures)
    require(not re.search(r"WeaponIdentity|WeaponFactoryBinding|WeaponCanonical", cmake),
            "production CMake still references inactive canonical weapon sources", failures)
    diagnostic_sources = policy + verification + observe
    require(not re.search(r"StableWeaponIdentity|WeaponIdentityProjection|WeaponCanonical", diagnostic_sources),
            "diagnostic implementation introduces canonical weapon authority", failures)

    if failures:
        print("HandMemory attack diagnostic verification FAILED:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("HandMemory attack diagnostic verification PASSED")
    print("- diagnostic transaction is scalar-only and bounded")
    print("- lifecycle reset cancels old-world diagnostic state")
    print("- RIGHT/LEFT restore sequencing and draw behavior are unchanged")
    print("- attack input is observed after policy decisions without consumption or replay")
    print("- canonical weapon authority remains excluded")
    return 0


if __name__ == "__main__":
    sys.exit(main())
