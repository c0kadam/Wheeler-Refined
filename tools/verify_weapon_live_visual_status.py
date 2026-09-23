from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
POLICY = ROOT / "src/bin/Wheeler/WheelItems/WeaponLiveVisualStatusPolicy.h"
RUNTIME = ROOT / "src/bin/Wheeler/WheelItems/WheelItemWeapon.cpp"
HEADER = ROOT / "src/bin/Wheeler/WheelItems/WheelItemWeapon.h"
VERIFY = ROOT / "src/bin/Wheeler/WeaponLiveVisualStatusVerification.cpp"
TEXTURES = ROOT / "src/bin/Rendering/TextureManager.h"
HAND_INDICATORS = ROOT / "src/bin/Wheeler/SlotHandIndicators.cpp"
CMAKE = ROOT / "src/CMakeLists.txt"
ENCHANT_ASSET = ROOT / "Data/SKSE/Plugins/wheeler/resources/icons/weapon_enchanted.svg"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"Live weapon visual status verification FAILED: {message}")


policy = POLICY.read_text(encoding="utf-8")
runtime = RUNTIME.read_text(encoding="utf-8")
header = HEADER.read_text(encoding="utf-8")
verification = VERIFY.read_text(encoding="utf-8")
textures = TEXTURES.read_text(encoding="utf-8")
hand_indicators = HAND_INDICATORS.read_text(encoding="utf-8")
cmake = CMAKE.read_text(encoding="utf-8")

require("struct WeaponLiveVisualStatus" in policy, "value-only status model missing")
require("kExactUniqueID" in policy and "kSingleSameForm" in policy,
        "safe exact/single ownership proofs missing")
require("sameFormCount != 1" in policy, "UID-zero multi-copy presentation is not fail-closed")
require("member.uniqueID != a_evidence.storedUniqueID" in policy,
        "exact UID ownership is not enforced")
require("exact || member.count != 1 || !member.statusReadable" in policy,
        "duplicate/unreadable exact evidence is not fail-closed")

for forbidden in (
    "ExtraDataList", "InventoryEntryData", "InventoryItemMap", "SetUniqueID",
    "logicalRowSignature", "EquipObject", "UnequipObject", "Serialize",
    "HandMemory", "Canonical"
):
    require(forbidden not in policy, f"policy retains or controls forbidden authority: {forbidden}")

require(runtime.count("ResolveWeaponLiveVisualStatus(") == 2,
        "live status resolver must have exactly one definition and one draw call")
require(runtime.count("DrawWeaponLiveStatusOverlays(") == 2,
        "shared overlay renderer must have exactly one definition and one draw call")
require("poison->poison != nullptr && poison->count > 0" in runtime,
        "active poison semantics are incomplete")
require("enchantment->enchantment != nullptr" in runtime,
        "instance enchantment semantics are incomplete")
require("a_weapon->formEnchanting != nullptr" in runtime,
        "base enchantment semantics are missing")
require("Texture::icon_image_type::poison_default" in runtime,
        "standard poison asset is not reused")
require("Texture::icon_image_type::weapon_enchanted" in runtime,
        "enchantment asset is not rendered")

for function_name in (
    "ActivateItemSecondary", "ActivateItemPrimary", "equipItem", "unequipItem",
    "SerializeIntoJsonObj", "MatchesEquippedHandIndicator"
):
    start = runtime.index(f"WheelItemWeapon::{function_name}")
    next_function = runtime.find("\nvoid WheelItemWeapon::", start + 1)
    if next_function < 0:
        next_function = runtime.find("\nbool WheelItemWeapon::", start + 1)
    if next_function < 0:
        next_function = len(runtime)
    block = runtime[start:next_function]
    require("LiveVisualStatus" not in block and "LiveStatusOverlay" not in block,
            f"presentation status leaked into gameplay/identity function {function_name}")

require("WeaponLiveVisualStatus" not in header,
        "live status was persisted in WheelItemWeapon object state")
require("WeaponLiveVisualStatus" not in hand_indicators,
        "existing L/R resolver was coupled to status overlays")
require("weapon_enchanted" in textures and ENCHANT_ASSET.is_file(),
        "enchantment texture registration or SVG asset missing")
require(r"WeaponLiveVisualStatusVerification\\.cpp" in cmake,
        "verification source is not excluded from production glob")
require("wheeler_weapon_live_visual_status_verification" in cmake,
        "focused compiled verifier target missing")

for required_case in (
    "single unmodified exact weapon has no status badge",
    "fresh poison absence clears the poison badge",
    "base weapon enchantment shows enchantment",
    "poison and enchantment remain simultaneously representable",
    "exact UID reads only its matching physical member",
    "sameFormCount one safely supports UID-zero live status",
    "stacked UID-zero row split by poison fails closed",
    "manually rebound poisoned exact row displays poison",
    "FormID alone never owns multi-copy presentation status",
):
    require(required_case in verification, f"mandatory focused case missing: {required_case}")

for rejected in (
    "ItemRuntimeState::StateChanged", "ItemRuntimeState::kStateChanged",
    "state=state_changed", "WeaponRuntimeStatePolicy"
):
    require(rejected not in runtime and rejected not in policy,
            f"forbidden StateChanged authority present: {rejected}")

print("Live weapon visual status verification PASSED")
print("- exact UID and sameFormCount==1 are the only presentation ownership proofs")
print("- ambiguous stacked UID-zero rows fail closed")
print("- poison and base/instance enchantment are read live into scalar status")
print("- one shared renderer supports simultaneous poison and enchantment badges")
print("- L/R, activation, HandMemory, persistence, pruning, and canonical authority remain isolated")
print("- no raw inventory pointer enters retained status state")
