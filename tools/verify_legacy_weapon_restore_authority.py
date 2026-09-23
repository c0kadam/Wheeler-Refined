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
    wheeler_h = read("src/bin/Wheeler/Wheeler.h")
    token_h = read("src/bin/Wheeler/WheelItems/LegacyWeaponRestore.h")
    policy_h = read("src/bin/Wheeler/WheelItems/LegacyWeaponRestorePolicy.h")
    weapon = read("src/bin/Wheeler/WheelItems/WheelItemWeapon.cpp")
    cmake = read("src/CMakeLists.txt")

    token = body(token_h, "struct LegacyWeaponRestoreToken", failures)
    require("RE::FormID formID" in token, "restore token lacks base FormID", failures)
    require("std::uint16_t uniqueID" in token, "restore token lacks UID lineage", failures)
    require("std::string logicalRowSignature" in token, "restore token lacks legacy row signature", failures)
    for pointer_type in ("ExtraDataList", "InventoryEntryData", "InventoryItemMap"):
        require(pointer_type not in token, f"restore token retains {pointer_type}", failures)

    require("sameFormCount == 1" in policy_h,
            "form-level fallback is not restricted to a single current member", failures)
    require("UIDLineageFallback" in policy_h,
            "known legacy same-UID lineage fallback is not explicit", failures)
    require("exactSignatureCandidates" in policy_h and "uidLineageCandidates" in policy_h,
            "policy does not distinguish exact row evidence from UID lineage", failures)
    require("GroupEquivalent" in policy_h and "ProvesGroupEquivalent" in policy_h,
            "bounded group-equivalent legacy policy is missing", failures)
    require("representedCount + a_evidence.implicitCount == a_evidence.sameFormCount" in policy_h and
            "distinctLogicalRows == 1" in policy_h and
            "unreadableMembers == 0" in policy_h,
            "group policy lacks full-accounting/equivalence/readability gates", failures)

    generic_restore = body(wheeler, "static bool EquipFormToHand(", failures)
    guarded_restore = last_body(wheeler, "static bool EquipRestoreTargetToHand(", failures)
    require(wheeler.count("EquipFormToHand(") == 2,
            "generic FormID equip helper has a caller outside the guarded non-weapon branch", failures)
    require("return EquipFormToHand(pc, formID, hand)" in guarded_restore,
            "guarded restore no longer delegates only its non-weapon branch", failures)
    require("form->As<RE::TESObjectWEAP>()" in guarded_restore,
            "guarded restore does not classify weapon targets", failures)
    require("weaponToken.IsValid()" in guarded_restore and "weaponToken.formID != formID" in guarded_restore,
            "weapon restore can proceed without a matching value-owned token", failures)
    require("TryGetInventorySnapshot" in guarded_restore and "ResolveLiveMember" in guarded_restore,
            "weapon restore lacks fresh snapshot/current-member resolution", failures)
    clear_pos = guarded_restore.find("inventory.clear()")
    equip_pos = guarded_restore.find("InventorySnapshotCache::EquipObject(")
    require(clear_pos >= 0 and equip_pos > clear_pos,
            "weapon restore mutates before discarding its pointer-bearing snapshot", failures)
    require("nullptr, 1" in generic_restore,
            "non-weapon legacy helper shape changed unexpectedly", failures)

    hand_memory = body(wheeler, "static void UpdateHandMemory()", failures)
    require("CaptureWornToken" in wheeler and "CaptureWeaponRestoreTokenFromSnapshot" in hand_memory,
            "HandMemory does not capture current worn legacy row evidence", failures)
    require(hand_memory.count("EquipRestoreTargetToHand(") == 2,
            "HandMemory left/right restores are not both row guarded", failures)
    require("EquipFormToHand(" not in hand_memory,
            "HandMemory still invokes FormID-only equip", failures)
    require(hand_memory.find("HandMemoryHand::Right") < hand_memory.find("HandMemoryHand::Left",
            hand_memory.find("HandMemoryHand::Right") + 1),
            "HandMemory no longer restores/evaluates right before left", failures)
    require("memLeftWeapon.Clear()" in hand_memory and "memRightWeapon.Clear()" in hand_memory,
            "HandMemory lifecycle does not clear both restore tokens", failures)

    queue_spell = body(wheeler, "bool Wheeler::QueueSpellActivation(", failures)
    require("CaptureCurrentWeaponRestoreToken" in queue_spell,
            "direct-cast pre-state does not capture weapon row evidence", failures)
    require("restoreOverrideLeftWeapon" in queue_spell and "restoreOverrideRightWeapon" in queue_spell,
            "carried post-cast restore loses companion row evidence", failures)
    require("singleHandIsolationRestoreWeapon" in wheeler_h and
            "singleHandIsolationRestoreWeapon" in wheeler,
            "single-hand isolation does not retain a value-owned row token", failures)

    arm = body(wheeler, "void Wheeler::ArmSpellHoldRelease(", failures)
    queue_post = body(wheeler, "void Wheeler::QueuePostCastRestore(", failures)
    clear_hold = body(wheeler, "void Wheeler::ClearSpellHoldRelease()", failures)
    clear_post = body(wheeler, "void Wheeler::ClearPostCastRestore()", failures)
    require("_spellHoldRestoreLeftWeapon" in arm and "_spellHoldRestoreRightWeapon" in arm,
            "hold-release state drops restore row evidence", failures)
    require("_spellPostCastRestoreLeftWeapon" in queue_post and
            "_spellPostCastRestoreRightWeapon" in queue_post,
            "post-cast state drops restore row evidence", failures)
    require("_spellHoldRestoreLeftWeapon.Clear()" in clear_hold and
            "_spellHoldRestoreRightWeapon.Clear()" in clear_hold,
            "hold-release reset leaves row tokens active", failures)
    require("_spellPostCastRestoreLeftWeapon.Clear()" in clear_post and
            "_spellPostCastRestoreRightWeapon.Clear()" in clear_post,
            "post-cast reset leaves row tokens active", failures)

    pending_rollback = body(wheeler, "void Wheeler::RollbackPendingSpellTransactionInCurrentWorld()", failures)
    post_rollback = body(wheeler, "void Wheeler::RollbackPostCastTransactionInCurrentWorld()", failures)
    memory_rollback = body(wheeler, "void Wheeler::RollbackHandMemoryInCurrentWorld()", failures)
    for name, rollback in (
        ("isolation", pending_rollback),
        ("post-cast", post_rollback),
        ("HandMemory", memory_rollback),
    ):
        require("EquipRestoreTargetToHand(" in rollback and "EquipFormToHand(" not in rollback,
                f"current-world {name} rollback is not row guarded", failures)

    require("ResolveExactWeaponInventoryState" in weapon and
            "IsStrictSingleExactInventoryState" in weapon and
            "sameFormCount == 1" in weapon and
            "exactExtraData" in weapon,
            "IWS exact-member/UID guard was weakened", failures)
    require("postUnequipInv" in weapon and "postUnequipState" in weapon,
            "IWS no longer reacquires after its unequip mutation", failures)
    require("BuildGroupEquivalentProof" in weapon and
            "groupProof.logicalRowSignature == a_token.logicalRowSignature" in weapon,
            "group restore does not freshly re-prove the captured legacy signature", failures)

    require("LegacyWeaponRestoreVerification.cpp" in cmake and
            "WHEELER_LEGACY_WEAPON_RESTORE_VERIFICATION_MAIN" in cmake,
            "production-shared restore policy verifier target is missing", failures)
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

    if failures:
        print("Legacy weapon restore authority verification FAILED:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("Legacy weapon restore authority verification PASSED")
    print("- generic FormID helper is reachable only for non-weapons")
    print("- HandMemory, direct/post-cast, isolation, and current-world rollback carry row tokens")
    print("- fresh live resolution precedes weapon mutation and the snapshot is discarded first")
    print("- UID-zero equivalent groups require full readable one-signature accounting")
    print("- conflicting/unreadable groups fail closed; same-UID cross-signature fallback remains explicit")
    print("- IWS retains its stricter UID/exact-member guard")
    print("- lifecycle clear paths invalidate all companion tokens")
    return 0


if __name__ == "__main__":
    sys.exit(main())
