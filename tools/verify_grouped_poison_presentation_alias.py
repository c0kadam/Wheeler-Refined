from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "src/bin/Wheeler/WheelItems/WheelItemWeapon.cpp"
HEADER = ROOT / "src/bin/Wheeler/WheelItems/WheelItemWeapon.h"
BASE = ROOT / "src/bin/Wheeler/WheelItems/WheelItem.h"
ENTRY = ROOT / "src/bin/Wheeler/WheelEntry.cpp"
POLICY = ROOT / "src/bin/Wheeler/WheelItems/GroupedPoisonPresentationAliasPolicy.h"
CMAKE = ROOT / "src/CMakeLists.txt"


def require(text: str, needle: str, message: str) -> None:
    if needle not in text:
        raise SystemExit(f"FAIL {message}: missing {needle!r}")
    print(f"PASS {message}")


def reject(text: str, needle: str, message: str) -> None:
    if needle in text:
        raise SystemExit(f"FAIL {message}: found {needle!r}")
    print(f"PASS {message}")


cpp = CPP.read_text(encoding="utf-8")
header = HEADER.read_text(encoding="utf-8")
base = BASE.read_text(encoding="utf-8")
entry = ENTRY.read_text(encoding="utf-8")
policy = POLICY.read_text(encoding="utf-8")
cmake = CMAKE.read_text(encoding="utf-8")

require(policy, "Lineage::StrongHandTransition", "creation requires strong hand lineage")
require(policy, "originatedFromUID0GroupedFallback", "creation requires UID0 grouped origin")
require(policy, "!a_evidence.otherHandHasSameForm", "same-form dual wield fails closed")
require(policy, "!a_evidence.hasCompetingModifiedMember", "competing modified row fails closed")
require(policy, "a_evidence.preMutationUID == 0", "UID is not substituted for hand-transition proof")
require(policy, "presentationCount = 1", "alias presentation count is one")
require(policy, "a_evidence.storedUID != 0", "stored slot rebound invalidates alias")
require(policy, "a_evidence.currentTargetNonPoisonSignature != a_alias.afterNonPoisonSignature",
        "fresh validation preserves non-poison logical semantics")
require(policy, "ValidationFailure::PoisonDisappeared", "poison expiration invalidation exists")
require(policy, "ValidationFailure::EpochChanged", "lifecycle invalidation exists")

for forbidden in (
    "RE::ExtraDataList*",
    "RE::InventoryEntryData*",
    "InventoryItemMap",
    "uintptr_t",
    "EquipObject",
    "UnequipObject",
    "SetUniqueID",
    "Serialize",
    "HandMemory",
    "StableWeaponIdentity",
    "CanonicalWeapon",
    "StateChanged",
):
    reject(policy, forbidden, f"alias policy excludes {forbidden}")

require(cpp, '"GROUPED_POISON_ALIAS CREATE', "alias creation is logged")
require(cpp, '"GROUPED_POISON_ALIAS VALID', "first fresh validation is logged")
require(cpp, '"GROUPED_POISON_ALIAS DROP', "alias invalidation is logged")
require(cpp, "ResolveGroupedPoisonAliasPresentation(\n\t\t_runtimePresentationSlotID",
        "alias resolution is called from weapon drawing")
if cpp.count("ResolveGroupedPoisonAliasPresentation(") != 2:
    raise SystemExit("FAIL alias resolution must have exactly one definition and one draw-only call")
print("PASS alias resolution has one draw-only consumer")
require(cpp, "itemCount = static_cast<int>(aliasPresentation.count);",
        "count override is local presentation state")
require(cpp, "aliasPresentation.poisoned", "accepted renderer receives alias poison state")
require(cpp, "_drawOnlyHandPresentation.right = aliasPresentation.right;",
        "alias supplies draw-only hand state")
require(base, "GetTransientDrawOnlyHandPresentation", "draw-only hand API is separate")
require(entry, "SlotHandIndicators::ComputeSlotHandState", "authoritative hand resolver remains called")
require(entry, "GetTransientDrawOnlyHandPresentation", "presentation is supplemented only after resolver")
require(header, "_runtimePresentationSlotID", "slot correlation is scalar and runtime-only")
require(cpp, 'DropGroupedPoisonPresentationAlias("new_wheeler_weapon_activation")',
        "new Wheeler weapon intent drops alias")
require(cpp, 'DropGroupedPoisonPresentationAlias("lifecycle_reset")',
        "lifecycle reset drops alias")
require(cpp, "DropGroupedPoisonPresentationAliasForSlot",
        "slot destruction drops alias")
require(cmake, r"GroupedPoisonPresentationAliasVerification\\.cpp$",
        "focused verifier source is excluded from the production DLL glob")
require(cmake, "wheeler_grouped_poison_presentation_alias_verification",
        "focused compiled verifier target exists")

serialize_start = cpp.index("void WheelItemWeapon::SerializeIntoJsonObj")
serialize_end = cpp.index("bool WheelItemWeapon::equipItem", serialize_start)
reject(cpp[serialize_start:serialize_end], "Alias", "serialization has no alias consumer")
is_active_start = cpp.index("bool WheelItemWeapon::IsActive")
reject(cpp[is_active_start:], "Alias", "IsActive and availability have no alias consumer")

print("PASS grouped poison alias is presentation-only, scalar-only, and fail-closed")
