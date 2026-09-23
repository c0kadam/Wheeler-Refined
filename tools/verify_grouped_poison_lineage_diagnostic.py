from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "src/bin/Wheeler/WheelItems/WheelItemWeapon.cpp"
HEADER = ROOT / "src/bin/Wheeler/WheelItems/WheelItemWeapon.h"
POLICY = ROOT / "src/bin/Wheeler/WheelItems/GroupedPoisonLineageDiagnosticPolicy.h"
WHEELER = ROOT / "src/bin/Wheeler/Wheeler.cpp"


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
policy = POLICY.read_text(encoding="utf-8")
wheeler = WHEELER.read_text(encoding="utf-8")

require(cpp, "usedGroupedFallback && this->GetUniqueID() == 0",
        "watcher arms only after the UID0 grouped fallback mutation returns")
require(cpp, 'LogGroupedPoisonSnapshot(\n\t\t\t"PRE"', "fresh post-equip baseline is explicit")
require(cpp, '"GROUPED_POISON MUTATION', "topology mutations are explicit")
require(cpp, "TryGetInventorySnapshot(\n\t\t\t\ta_player, inventory, \"GroupedPoisonLineageDiagnostic\")",
        "each observation reacquires a fresh inventory snapshot")
require(cpp, "boundObject->GetFormID() == a_formID",
        "observation filters immediately to the target FormID")
require(cpp, "ephemeralXList", "xList address is labelled ephemeral")
require(cpp, "addressesLoggingOnly=1", "address comparison is logging-only")
require(cpp, 'CancelGroupedPoisonDiagnostic("new_wheeler_weapon_activation")',
        "new Wheeler weapon activation cancels prior evidence")
require(cpp, 'CancelGroupedPoisonDiagnostic("lifecycle_reset")',
        "lifecycle reset cancels prior evidence")
require(cpp, "target_hand_occupant_changed", "hand occupant changes invalidate evidence")
require(cpp, "target_form_count_changed", "count changes invalidate evidence")
require(cpp, "g_groupedPoisonWatcher.baseline = snapshot.summary",
        "watcher retains only the scalar snapshot summary")
require(wheeler, "WheelItemWeapon::ProcessGroupedPoisonLineageDiagnostic();",
        "later Update drives the watcher")
require(header, "static void ProcessGroupedPoisonLineageDiagnostic();",
        "diagnostic Update entry point is declared")
require(policy, "watcher.hardDeadline = a_now + 25.0;", "watcher has a 25-second deadline")
require(policy, "watcher.samplesRemaining = 250;", "watcher has a finite sample cap")
require(policy, "a_watcher.nextSampleAt = a_now + 0.1;", "polling is throttled")
require(policy, "StrongUniqueID", "same-UID classification exists")
require(policy, "targetUniqueIDGloballyUnique", "strong UID evidence rejects duplicate UIDs")
require(policy, "StrongHandTransition", "unique-hand transition classification exists")
require(policy, "otherHandHasSameForm", "dual-hand same-form ambiguity is represented")

for forbidden in (
    "RE::ExtraDataList*",
    "RE::InventoryEntryData*",
    "InventoryItemMap",
    "EquipObject",
    "UnequipObject",
    "SetUniqueID",
    "Serialize",
    "StableWeaponIdentity",
    "CanonicalWeapon",
):
    reject(policy, forbidden, f"scalar policy excludes {forbidden}")

print("PASS grouped poison lineage diagnostic is bounded and non-authoritative")
