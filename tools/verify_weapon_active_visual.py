from pathlib import Path
from static_cpp_checks import extract_braced_body, normalize_cpp

root = Path(__file__).resolve().parents[1]
source = (root / "src/bin/Wheeler/WheelItems/WheelItemWeapon.cpp").read_text(encoding="utf-8")
body = extract_braced_body(source, "bool WheelItemWeapon::IsActive(")
helper = extract_braced_body(source, "std::optional<bool> ResolveUID0ActiveVisualOverride(")
checks = 0

def require(ok, message):
    global checks
    if not ok:
        raise SystemExit("FAIL " + message)
    checks += 1
    print("PASS " + message)

maintenance = "auto itemData = this->GetItemExtraDataAndCount(a_inv);"
require(body.count(maintenance) == 1, "existing identity maintenance still executes once")
prefix = normalize_cpp(body[:body.index(maintenance)])
require(prefix == "try { auto pc = RE::PlayerCharacter::GetSingleton(); if (!pc) { return false; }",
        "no new early return or reordering before identity maintenance")
require(body.index(maintenance) < body.index("if (this->GetUniqueID() == 0)") <
        body.index("ResolveUID0ActiveVisualOverride") < body.index("ShouldBypassInstanceHandResolution"),
        "post-maintenance UID gates override before unchanged legacy resolution")
require(source.count("ResolveUID0ActiveVisualOverride(") == 2, "helper has only one IsActive consumer")
for token in ("kPoison", "kEnchantment", "kHealth", "kCharge", "kTextDisplayData"):
    require(token in helper, "metadata distinction " + token)
require("TryHasTypeSafe" in helper and "member.metadataReadable = false" in helper,
        "metadata read failure stays explicit")
require("CopyExtraListsSafe" in helper and "TryGetExtraListCount" in helper,
        "fresh local evidence uses guarded reads")
require("if (count == 1)" in helper and "return std::nullopt;" in helper,
        "single copy bypasses metadata scan")
for token in ("SetUniqueID", "EnsureXList", "EquipObject", "UnequipObject", "Alias", "_logicalRowSignature", "Canonical"):
    require(token not in helper and token not in body, "new active path excludes " + token)
require("GetEquippedHandByExactUniqueID" in body and body.count("Utils::Inventory::GetWeaponEquippedHand") == 2,
        "legacy nonzero/group/single resolver branches remain")
print(f"{checks} checks passed")
