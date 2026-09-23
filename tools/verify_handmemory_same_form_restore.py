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


def last_body(source: str, marker: str, failures: list[str]) -> str:
    marker_index = source.rfind(marker)
    if marker_index < 0:
        failures.append(f"C++ marker not found: {marker}")
        return ""
    return body(source[marker_index:], marker, failures)


def main() -> int:
    failures: list[str] = []
    wheeler = read("src/bin/Wheeler/Wheeler.cpp")
    weapon = read("src/bin/Wheeler/WheelItems/WheelItemWeapon.cpp")
    token = read("src/bin/Wheeler/WheelItems/LegacyWeaponRestore.h")
    policy = read("src/bin/Wheeler/WheelItems/LegacyWeaponRestorePolicy.h")
    attack_policy = read("src/bin/Wheeler/HandMemoryAttackDiagnosticPolicy.h")
    verification = read("src/bin/Wheeler/HandMemorySameFormRestoreVerification.cpp")
    cmake = read("src/CMakeLists.txt")

    hand_memory_state = body(wheeler, "struct HandMemoryState", failures)
    update = body(wheeler, "static void UpdateHandMemory()", failures)
    guarded_restore = last_body(wheeler, "static bool EquipRestoreTargetToHand(", failures)
    reconcile = body(weapon, "DualHandCaptureReconciliation ReconcileDualHandSameFormCapture(", failures)

    require("DecideDualHandCapture" in policy and
            "PromoteBothToGroupEquivalent" in policy and
            "RejectIndistinguishableCollision" in policy,
            "same-form dual-hand capture collision policy is missing", failures)
    require("distinctWornMembersProven" in policy and
            "groupEquivalentProven" in policy,
            "collision promotion lacks distinct-member/full-group proof", failures)
    require("ReconcileDualHandSameFormCapture" in update,
            "HandMemory does not reconcile the two per-hand capture tokens together", failures)
    left_capture = update.find("g_handMemory.lastNon2HLeft = getRestorableHandMemoryFormID(")
    right_capture = update.find("g_handMemory.lastNon2HRight = getRestorableHandMemoryFormID(")
    reconcile_call = update.find("ReconcileDualHandSameFormCapture(")
    require(0 <= left_capture < right_capture < reconcile_call,
            "dual-hand reconciliation does not follow both same-snapshot hand captures", failures)
    require("IsWornInHand(member, a_leftHand)" in reconcile and
            "*leftWorn != *rightWorn" in reconcile,
            "Worn/WornLeft evidence does not prove distinct captured members", failures)
    require("RowKind::GroupEquivalent" in reconcile and
            "a_leftToken.uniqueID = 0" in reconcile and
            "a_rightToken.uniqueID = 0" in reconcile,
            "proven collision does not normalize both obligations to existing group authority", failures)

    require("eligibleLogicalCount" in policy and
            "AllowsGroupEquivalentFormLevel" in policy and
            "eligibleLogicalCount > 0" in policy,
            "group form-level resolution lacks remaining-logical-member accounting", failures)
    require("member.count - 1" in weapon and
            "AllowsGroupEquivalentFormLevel" in weapon,
            "opposite-hand reservation is not deducted before group form-level use", failures)
    require("groupProof.logicalRowSignature == a_token.logicalRowSignature" in weapon,
            "fresh group resolution does not re-prove the captured logical signature", failures)

    require(update.count("EquipRestoreTargetToHand(") == 2,
            "HandMemory restore attempt count changed", failures)
    right_attempt = update.find("HandMemoryHand::Right")
    left_attempt = update.find("HandMemoryHand::Left", right_attempt + 1)
    require(right_attempt >= 0 and left_attempt > right_attempt,
            "RIGHT-before-LEFT restore sequencing changed", failures)
    require("TryGetInventorySnapshot" in guarded_restore and
            "ResolveLiveMember" in guarded_restore,
            "each restore attempt does not reacquire and resolve a fresh inventory snapshot", failures)
    require(guarded_restore.find("inventory.clear()") <
            guarded_restore.find("InventorySnapshotCache::EquipObject("),
            "pointer-bearing snapshot survives into equip mutation", failures)

    require("ShouldRetryFailedRestore" in policy and
            "retriesUsed == 0" in policy,
            "failed-restore retry is not bounded to one", failures)
    require("RestoreRetryScheduled" in update and
            "RestoreRetryAbandoned" in update and
            "rightRestoreRetriesUsed" in hand_memory_state and
            "leftRestoreRetriesUsed" in hand_memory_state,
            "value-only per-hand retry state/reason diagnostics are missing", failures)
    require("currentHandEmpty" in policy and "epochMatches" in policy and
            "restoreWindowOpen" in policy and "gameplayIntentCurrent" in policy,
            "retry gate lacks occupant/epoch/window/intent conditions", failures)

    require("DropPendingRestore hand={}" in update and
            "reason=current_restorable_occupant" in update,
            "intentional current-occupant drop semantics are not explicit", failures)
    drop_pos = update.find("DropPendingRestore hand={}")
    right_restore_pos = update.find("HandMemoryHand::Right")
    require(0 <= drop_pos < right_restore_pos,
            "current occupant is not reconciled before restore mutation", failures)

    require("EquipFormToHand(" not in update,
            "HandMemory gained unrestricted FormID-only restore authority", failures)
    for forbidden in ("ExtraDataList*", "InventoryEntryData*", "InventoryItemMap"):
        require(forbidden not in hand_memory_state,
                f"persistent HandMemory state retains forbidden pointer-bearing type {forbidden}", failures)
    require("g_handMemory.Reset()" in wheeler and
            "restorationEpoch" in hand_memory_state,
            "lifecycle/epoch reset does not invalidate pending retry state", failures)

    for required_case in (
        "different FormIDs preserve",
        "same-form same-lineage obligations",
        "distinguishable per-hand exact UIDs",
        "count dropping to one",
        "conflicting logical rows fail closed",
        "explicit current occupant blocks retry overwrite",
        "epoch invalidation cancels retry authority",
    ):
        require(required_case in verification,
                f"focused verifier case missing: {required_case}", failures)

    require("kPostRestoreObservationSeconds = 5.0" in attack_policy and
            "kOverallHardLimitSeconds = 6.0" in attack_policy and
            "kPeriodicSampleSeconds = 0.5" in attack_policy and
            "kMaximumFollowupSamples = 40" in attack_policy,
            "accepted HandMemory attack diagnostic policy changed", failures)
    require("HandMemorySameFormRestoreVerification.cpp" in cmake and
            "WHEELER_HANDMEMORY_SAME_FORM_RESTORE_VERIFICATION_MAIN" in cmake,
            "focused same-form verifier target is missing", failures)
    require(not re.search(
        r"StableWeaponIdentity|WeaponIdentityProjection|WeaponCanonical|weapon_canonical_preflight",
        policy + token + reconcile + verification),
        "same-form remediation introduces canonical weapon authority", failures)

    if failures:
        print("HandMemory same-form dual restore verification FAILED:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("HandMemory same-form dual restore verification PASSED")
    print("- distinct exact UIDs remain exact; indistinguishable lineage becomes proven group authority")
    print("- RIGHT reservation leaves only fully-accounted equivalent capacity for LEFT")
    print("- conflicting/unreadable groups and count-one reuse fail closed")
    print("- failed restore retains value authority for at most one fresh retry")
    print("- explicit occupants, lifecycle changes, and superseding intent cancel retry")
    print("- no persistent engine pointers, unrestricted FormID authority, or canonical authority")
    return 0


if __name__ == "__main__":
    sys.exit(main())
