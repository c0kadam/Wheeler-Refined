from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/bin/Wheeler/Wheel.h"
CPP = ROOT / "src/bin/Wheeler/Wheel.cpp"
POLICY = ROOT / "src/bin/Wheeler/HoverActivationSnapshotPolicy.h"
VERIFIER = ROOT / "src/bin/Wheeler/HoverActivationSnapshotVerification.cpp"
CMAKE = ROOT / "src/CMakeLists.txt"


def require(text: str, needle: str, message: str) -> None:
    if needle not in text:
        raise SystemExit(f"FAIL {message}: missing {needle!r}")
    print(f"PASS {message}")


def reject(text: str, needle: str, message: str) -> None:
    if needle in text:
        raise SystemExit(f"FAIL {message}: found {needle!r}")
    print(f"PASS {message}")


def function_body(text: str, signature: str, next_signature: str) -> str:
    start = text.index(signature)
    end = text.index(next_signature, start)
    return text[start:end]


header = HEADER.read_text(encoding="utf-8")
cpp = CPP.read_text(encoding="utf-8")
policy = POLICY.read_text(encoding="utf-8")
verifier = VERIFIER.read_text(encoding="utf-8")
cmake = CMAKE.read_text(encoding="utf-8")

require(header, "std::atomic<std::int32_t> _hoveredEntryIdx{ -1 };",
        "hover index is a transient atomic scalar")
reject(header, "int _hoveredEntryIdx", "legacy non-atomic hover field is absent")
require(header, "HoverActivationSnapshotPolicy::Load(_hoveredEntryIdx)",
        "public hover getter uses atomic load")
require(policy, ".load(std::memory_order_relaxed)", "policy performs explicit atomic load")
require(policy, ".store(a_value, std::memory_order_relaxed)", "policy performs explicit atomic store")
require(policy, "snapshot >= 0", "snapshot rejects negative indices")
require(policy, "static_cast<std::size_t>(a_snapshot) < a_entryCount",
        "snapshot rejects indices outside the entry list")

for line_no, line in enumerate(cpp.splitlines(), 1):
    if "_hoveredEntryIdx" not in line or line.lstrip().startswith("//"):
        continue
    if not any(token in line for token in (
        "HoverActivationSnapshotPolicy::Load(",
        "HoverActivationSnapshotPolicy::Store(",
        "HoverActivationSnapshotPolicy::Capture(",
    )):
        raise SystemExit(f"FAIL unsynchronized hover access at Wheel.cpp:{line_no}: {line.strip()}")
print("PASS every Wheel.cpp hover access is explicitly atomic")

reject(cpp, "_entries[_hoveredEntryIdx]", "no direct live-hover entry indexing remains")
reject(cpp, "this->_entries[_hoveredEntryIdx]", "no qualified live-hover entry indexing remains")

primary = function_body(cpp, "PreparedWheelItemActivation Wheel::ActivateHoveredEntryPrimary", "void Wheel::ClearDepletedConsumables")
secondary = function_body(cpp, "PreparedWheelItemActivation Wheel::ActivateHoveredEntrySecondary", "PreparedWheelItemActivation Wheel::ActivateHoveredEntrySpecial")
special = function_body(cpp, "PreparedWheelItemActivation Wheel::ActivateHoveredEntrySpecial", "void Wheel::MoveHoveredEntryForward")

for name, body, dispatch in (
    ("primary", primary, "entry->ActivateItemPrimary"),
    ("secondary", secondary, "entry->ActivateItemSecondary"),
    ("special", special, "entry->ActivateItemSpecial"),
):
    require(body, "hoverSnapshot", f"{name} captures a hover snapshot")
    require(body, "HoverActivationSnapshotPolicy::IsValid", f"{name} validates the captured index")
    require(body, dispatch, f"{name} dispatches through the captured synchronous entry")
    reject(body, "_entries[_hoveredEntryIdx]", f"{name} has no live-hover dispatch")

require(primary, "const std::shared_ptr<WheelItem> item = entry->GetSelectedItem();",
        "primary retains selected WheelItem ownership")
require(secondary, "const std::shared_ptr<WheelItem> item = entry->GetSelectedItem();",
        "secondary retains selected WheelItem ownership")
require(special, "const std::shared_ptr<WheelItem> item = entry->GetSelectedItem();",
        "special retains selected WheelItem ownership")
require(primary, "std::shared_lock<std::shared_mutex> lock(_lock)",
        "primary preserves shared structural lock")
require(secondary, "std::shared_lock<std::shared_mutex> lock(_lock)",
        "secondary runtime path preserves shared structural lock")
require(special, "std::shared_lock<std::shared_mutex> lock(_lock)",
        "special preserves shared structural lock")

private_state = header[header.index("private:"):]
reject(private_state, "WheelEntry* _", "Wheel stores no persistent raw entry pointer")
reject(policy, "WheelEntry", "snapshot policy contains no entry pointer or ownership")
reject(policy, "Serialize", "snapshot policy is not persistent")
reject(policy, "ExtraDataList", "snapshot policy contains no ExtraDataList authority")
reject(policy, "InventoryEntryData", "snapshot policy contains no InventoryEntryData authority")

require(verifier, "primary dispatches captured entry zero", "compiled primary retarget regression exists")
require(verifier, "secondary dispatches captured entry zero", "compiled secondary retarget regression exists")
require(verifier, "special dispatches captured entry zero", "compiled special retarget regression exists")
require(verifier, "exclusive structural mutation is blocked", "compiled structural lifetime regression exists")
require(verifier, "concurrent hover reads and writes use atomic scalar access",
        "compiled concurrent atomic regression exists")
require(verifier, "draw transition valid to negative", "compiled draw transition regressions exist")
require(cmake, 'list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/HoverActivationSnapshotVerification',
        "focused verifier source is excluded from production DLL glob")
require(cmake, "wheeler_hover_activation_snapshot_verification",
        "focused verifier target exists")

for forbidden in (
    "StableWeaponIdentity",
    "CanonicalWeapon",
    "StateChanged",
    "EquipObject",
    "ExtraDataList*",
    "InventoryEntryData*",
):
    reject(policy + verifier, forbidden, f"hover verifier excludes unrelated {forbidden}")

print("PASS hover activation snapshot remediation is atomic, bounded, and synchronous-local")
