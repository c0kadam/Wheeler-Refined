from __future__ import annotations

import sys
from pathlib import Path

from static_cpp_checks import extract_braced_body, normalize_cpp


ROOT = Path(__file__).resolve().parents[1]
WEAPON = ROOT / "src" / "bin" / "Wheeler" / "WheelItems" / "WheelItemWeapon.cpp"
SLOT_INDICATORS = ROOT / "src" / "bin" / "Wheeler" / "SlotHandIndicators.cpp"


def main() -> int:
    source = WEAPON.read_text(encoding="utf-8", errors="strict")
    slot_source = SLOT_INDICATORS.read_text(encoding="utf-8", errors="strict")
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    try:
        override = extract_braced_body(
            source,
            "std::optional<bool> WheelItemWeapon::MatchesEquippedHandIndicator(",
        )
        classifier = extract_braced_body(source, "SameFormIndicatorTopology ClassifySameFormIndicatorTopology(")
        generic_matcher = extract_braced_body(slot_source, "auto matchesHand =")
    except ValueError as error:
        failures.append(str(error))
        override = ""
        classifier = ""
        generic_matcher = ""

    require("this->GetUniqueID() != 0" not in override,
            "modified rows still bypass the weapon indicator override")
    require("ClassifySameFormIndicatorTopology" in override,
            "same-form indicator topology is not classified")
    require("MatchesLogicalRowInHandFromInventory" in override,
            "mixed rows do not use strict logical-row matching")
    require("this->_logicalRowSignature" in override,
            "modified-row signature is not consulted")

    classifier_norm = normalize_cpp(classifier)
    require("sameFormCount == 1" in classifier_norm,
            "single-form topology is not positively proven by count == 1")
    require(classifier_norm.count("return SameFormIndicatorTopology::kUnknown;") >= 3,
            "missing/failed topology inspection does not consistently classify as unknown")
    require("return SameFormIndicatorTopology::kMixedLogicalRows;" in classifier_norm,
            "mixed logical-row topology is not represented explicitly")

    override_norm = normalize_cpp(override)
    require(
        "if (topology == SameFormIndicatorTopology::kUnknown) { return false; }" in override_norm,
        "unknown weapon topology can still escape to the generic matcher",
    )
    require(
        "if (topology == SameFormIndicatorTopology::kUnambiguous) { return std::nullopt; }" in override_norm,
        "generic fallback is not confined to positively unambiguous topology",
    )

    generic_norm = normalize_cpp(generic_matcher)
    override_check = generic_norm.find("MatchesEquippedHandIndicator(")
    form_fallback = generic_norm.rfind("return true;")
    require(override_check >= 0 and form_fallback > override_check,
            "generic matcher structure could not be verified")

    helper_start = source.find("bool MatchesLogicalRowInHandFromInventory(")
    helper_end = source.find("int GetSameFormInventoryCount(", helper_start)
    helper = source[helper_start:helper_end]

    require(helper_start >= 0 and helper_end > helper_start,
            "logical-row indicator helper was not found")
    require("MatchesRequestedHandWorn" in helper,
            "current worn state is not required")
    require("BuildLogicalRowSignature(extraList) == a_logicalRowSignature" in helper,
            "modified rows are not matched by logical signature")
    require("!IsInstanceSpecificIndicatorExtraData(extraList)" in helper,
            "clean sentinels are not kept distinct from modified rows")
    require("return false;" in helper,
            "ambiguous mixed-row resolution does not fail closed")
    require("a_handSignature" not in helper,
            "UID is still being used as physical hand identity")

    two_hand = extract_braced_body(source, "bool MatchesMixedTwoHandedIndicatorRow(")
    classify_2h = extract_braced_body(source, "bool IsTwoHandedIndicatorWeapon(")
    require(all(token in classify_2h for token in (
        "IsBow()", "IsCrossbow()", "kTwoHandSword", "kTwoHandAxe")),
        "presentation 2H categories differ from the existing wheel semantics")
    require("TryBuildReadableLogicalRowSignature(list, member.signature)" in two_hand,
            "mixed 2H signature must use read-success-aware capture")
    require("TryHasTypeSafe" in two_hand and "HasTypeSafe(" not in two_hand.replace("TryHasTypeSafe(", ""),
            "mixed 2H metadata must distinguish read failure from absence")
    require(all(token in two_hand for token in (
        "kWorn", "kWornLeft", "kEnchantment", "kPoison", "kHealth", "kCharge", "kTextDisplayData")),
        "mixed 2H evidence is incomplete")
    require("ResolveMixedTwoHanded(" in two_hand,
            "production does not consume the tested unique worn-member policy")
    require("a_leftHand" not in two_hand,
            "2H physical proof must be independent of the presentation side")
    two_hand_gate = override_norm.find("if (IsTwoHandedIndicatorWeapon(weapon))")
    require(two_hand_gate > override_norm.find("kUnambiguous") >= 0 and
            override_norm.find("return MatchesLogicalRowInHandFromInventory(") > two_hand_gate,
            "2H branch must preserve unambiguous nullopt and the 1H helper tail")
    require("if (a_handFormID == 0 || a_handFormID != weapon->GetFormID()) { return false; }" in override_norm,
            "hand FormID gate was changed")
    require("a_leftHand);" in override_norm,
            "1H requested-hand semantics were removed")

    if failures:
        print("Weapon hand-indicator identity verification FAILED:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("Weapon hand-indicator identity verification PASSED")
    print("- mixed modified rows cannot fall through to FormID-only matching")
    print("- modified rows require logical signature plus current worn state")
    print("- clean sentinels require a genuinely clean current worn member")
    print("- unavailable or ambiguous topology returns false before generic FormID matching")
    print("- generic fallback remains available only for positively unambiguous topology")
    return 0


if __name__ == "__main__":
    sys.exit(main())
