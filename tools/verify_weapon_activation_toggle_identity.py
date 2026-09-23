from __future__ import annotations

import sys
import re
from pathlib import Path

from static_cpp_checks import extract_braced_body, normalize_cpp


ROOT = Path(__file__).resolve().parents[1]
WEAPON = ROOT / "src" / "bin" / "Wheeler" / "WheelItems" / "WheelItemWeapon.cpp"


def main() -> int:
    source = WEAPON.read_text(encoding="utf-8", errors="strict")
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    try:
        secondary = extract_braced_body(source, "void WheelItemWeapon::ActivateItemSecondary()")
        primary = extract_braced_body(source, "void WheelItemWeapon::ActivateItemPrimary()")
    except ValueError as error:
        failures.append(str(error))
        secondary = ""
        primary = ""

    for name, activation in (("secondary", secondary), ("primary", primary)):
        activation_norm = normalize_cpp(activation)
        require("MatchesLogicalRowInHandFromInventory" in activation,
                f"{name} activation does not test selected logical-row occupancy")
        require("selectedLogicalRowInTargetHand" in activation,
                f"{name} activation has no scalar logical-row toggle decision")
        require(bool(re.search(r"sameFormCount\s*==\s*1\b", activation_norm)),
                f"{name} activation does not require positive single-form proof")
        require(not re.search(r"sameFormCount\s*<=\s*1\b", activation_norm),
                f"{name} activation still treats zero inventory rows as unambiguous")
        require("Utils::Inventory::GetWeaponEquippedHand" not in activation,
                f"{name} activation still uses the broad legacy hand helper")
        require("if (selectedLogicalRowInTargetHand)" in activation,
                f"{name} activation does not require logical-row proof before toggle-off")
        require("inv.clear();" in activation,
                f"{name} activation does not discard its frame-local map before mutation")

    try:
        helper = extract_braced_body(source, "bool MatchesLogicalRowInHandFromInventory(")
    except ValueError as error:
        failures.append(str(error))
        helper = ""
    require("MatchesRequestedHandWorn" in helper,
            "toggle identity does not require current worn state")
    require("BuildLogicalRowSignature(extraList) == a_logicalRowSignature" in helper,
            "modified toggle identity does not require exact logical signature")
    require("!IsInstanceSpecificIndicatorExtraData(extraList)" in helper,
            "clean sentinel toggle identity accepts modified members")
    require("a_uniqueID" not in helper and "ExtraUniqueID" not in helper,
            "toggle identity still depends on UID physical uniqueness")

    if failures:
        print("Weapon activation toggle identity verification FAILED:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("Weapon activation toggle identity verification PASSED")
    print("- toggle-off requires selected logical-row membership in the target hand")
    print("- same-form different-row occupancy takes the equip/replace branch")
    print("- FormID fallback requires positive sameFormCount == 1 proof")
    print("- sameFormCount == 0 cannot authorize toggle identity")
    print("- activation discards the fresh map before mutation")
    return 0


if __name__ == "__main__":
    sys.exit(main())
