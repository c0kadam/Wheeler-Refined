from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "src/bin/Wheeler/WheelItems/WheelItemAlchemy.cpp"
POLICY = ROOT / "src/bin/Wheeler/WheelItems/AlreadyPoisonedReapplyGuardPolicy.h"
VERIFIER = ROOT / "src/bin/Wheeler/AlreadyPoisonedReapplyGuardVerification.cpp"
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
policy = POLICY.read_text(encoding="utf-8")
verifier = VERIFIER.read_text(encoding="utf-8")
cmake = CMAKE.read_text(encoding="utf-8")

require(cpp, 'TryGetInventorySnapshot(\n\t\t\t\ta_player, inventory, "WheelItemAlchemy::AlreadyPoisonedReapplyGuard")',
        "guard reacquires a fresh synchronous inventory snapshot")
require(cpp, "ExtraDataType::kWorn", "guard reads ExtraWorn")
require(cpp, "ExtraDataType::kWornLeft", "guard reads ExtraWornLeft")
require(cpp, "ExtraDataType::kPoison", "guard reads ExtraPoison presence")
require(cpp, "const bool wornRight = hasWorn && !hasWornLeft;", "Worn/WornLeft truth table is explicit")
require(cpp, "inventory.clear();", "pointer-bearing snapshot is discarded before queue decision returns")
require(cpp, '"POISON_REAPPLY_GUARD decision=skip reason={}', "blocked decision is logged once")

apply_start = cpp.index("WheelItemActivationResult WheelItemAlchemy::applyPoison()")
apply_end = cpp.index("bool WheelItemAlchemy::IsDepletedInPlayerInventory", apply_start)
apply = cpp[apply_start:apply_end]
guard_at = apply.index("EvaluateLivePoisonReapplyGuard(pc)")
queue_at = apply.index("Wheeler::QueuePoisonApply(_formID)")
if guard_at >= queue_at:
    raise SystemExit("FAIL guard must execute before QueuePoisonApply")
print("PASS guard executes before QueuePoisonApply")
blocked_path = apply[guard_at:queue_at]
require(blocked_path, "return WheelItemActivationResult::AlreadyPoisoned;",
        "already-poisoned path returns a non-success result without queueing or consuming poison")
require(blocked_path, "return WheelItemActivationResult::UnsafeResolution;",
        "ambiguous path returns a non-success result without queueing or consuming poison")

consume_start = cpp.index("void WheelItemAlchemy::consume()")
consume = cpp[consume_start:apply_start]
reject(consume, "EvaluateLivePoisonReapplyGuard", "ordinary potion and food consumption remain outside guard")

require(policy, "a_hand.targetWornMembers != 1", "physical target requires exactly one worn member")
require(policy, "a_hand.oppositeWornMembers != 0", "opposite-hand inconsistency fails closed")
require(policy, "Reason::kSameFormDualWield", "same-Form dual wield fails closed")
require(policy, "Reason::kUnreadableEvidence", "unreadable evidence fails closed")
require(policy, "!a_evidence.equippedStateStable", "equipped-state races fail closed")
require(policy, "Decision::kBlockAlreadyPoisoned", "already-poisoned target is blocked")

for forbidden in (
    "RE::ExtraDataList",
    "RE::InventoryEntryData",
    "InventoryItemMap",
    "uniqueID",
    "HandMemory",
    "GroupedPoisonPresentationAlias",
    "EquipObject",
    "UnequipObject",
    "QueuePoisonApply",
    "Serialize",
):
    reject(policy, forbidden, f"retained guard policy excludes {forbidden}")

for expected in (
    "clean RIGHT target queues",
    "clean LEFT target queues",
    "already-poisoned RIGHT target is blocked",
    "already-poisoned LEFT target is blocked",
    "blocked path preserves poison item count",
    "blocked path preserves existing weapon poison",
    "blocked path does not invoke QueuePoisonApply",
    "clean target invokes QueuePoisonApply",
    "exact nonzero UID poisoned row",
    "UID0 grouped worn poisoned member",
    "duplicate same-Form inventory",
    "same-Form dual wield fails closed",
    "unreadable evidence fails closed",
    "ambiguous worn evidence fails closed",
    "equipped-state race fails closed",
    "ordinary potion activation remains outside poison guard",
    "food activation remains outside poison guard",
):
    require(verifier, expected, expected)

require(cmake, "AlreadyPoisonedReapplyGuardVerification",
        "focused verifier source is excluded from production DLL glob")
require(cmake, "already_poisoned_reapply_guard_verification",
        "focused compiled verifier target exists")

print("PASS already-poisoned poison reapply guard is synchronous-local and fail-closed")
