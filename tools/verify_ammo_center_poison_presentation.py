from __future__ import annotations

import json
from pathlib import Path

from static_cpp_checks import extract_braced_body


ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "src/bin/Wheeler/AmmoWheel.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "src/bin/Wheeler/AmmoWheel.h").read_text(encoding="utf-8")
CONFIG_CPP = (ROOT / "src/bin/Config.cpp").read_text(encoding="utf-8")
CONFIG_H = (ROOT / "src/bin/Config.h").read_text(encoding="utf-8")
DEFAULTS = (ROOT / "Data/SKSE/Plugins/wheeler/AmmoWheel.defaults.ini").read_text(encoding="utf-8")
SCHEMA = json.loads((ROOT / "Data/SKSE/Plugins/dmenu/customSettings/Ammo Wheel.json").read_text(encoding="utf-8"))
POLICY_VERIFIER = (ROOT / "src/bin/Wheeler/AmmoRangedWeaponPoisonPresentationVerification.cpp").read_text(encoding="utf-8")


def check(ok: bool, name: str) -> None:
    print(("PASS " if ok else "FAIL ") + name)
    if not ok:
        raise SystemExit(1)


def schema_entries(node: object) -> list[dict]:
    if isinstance(node, dict):
        entries = [node]
        for value in node.values():
            entries.extend(schema_entries(value))
        return entries
    if isinstance(node, list):
        entries: list[dict] = []
        for value in node:
            entries.extend(schema_entries(value))
        return entries
    return []


order_cache = extract_braced_body(CPP, "void AmmoWheel::EnsureCenterFieldOrderCache()")
center_cache = extract_braced_body(CPP, "bool AmmoWheel::RebuildCenterPanelCache(ImVec2 a_wheelCenter, DrawArgs)")
presentation = HEADER[HEADER.index("struct RangedWeaponPoisonPresentation"):HEADER.index("// Load/cache icon texture")]
center_fields_defaults = DEFAULTS[DEFAULTS.index("[CenterPanel.Fields]"):DEFAULTS.index("[CenterPanel]", DEFAULTS.index("[CenterPanel.Fields]"))]

check('inline bool ShowPoison = true;' in CONFIG_H,
      "ShowPoison defaults to true")
check('"Name,Damage,Poison,Type,Count,Source"' in CONFIG_H,
      "default field order includes Poison after Damage")
check('GetBoolValue(ini, "CenterPanel.Fields", "ShowPoison", Config::AmmoWheel::CenterFields::ShowPoison);' in CONFIG_CPP,
      "ShowPoison loads from CenterPanel.Fields")
check("ShowPoison = true" in center_fields_defaults and "Order" not in center_fields_defaults,
      "defaults add only ShowPoison without rewriting legacy Order keys")

schema_field_entries = [entry for entry in schema_entries(SCHEMA)
                        if isinstance(entry.get("ini"), dict) and
                        entry["ini"].get("section") == "CenterPanel.Fields"]
show_poison_entries = [entry for entry in schema_field_entries
                        if entry["ini"].get("id") == "ShowPoison"]
check(len(show_poison_entries) == 1 and show_poison_entries[0].get("default") is True,
      "dMenu schema exposes one default-enabled Show Poison toggle")
check(not any(entry["ini"].get("id") == "Order" for entry in schema_field_entries),
      "dMenu adds no new field-order editor")

check('token == "Name" || token == "Damage" || token == "Poison"' in order_cache,
      "field-order parser recognizes Poison")
check('std::find(_centerFieldOrderCache.begin(), _centerFieldOrderCache.end(), token)' in order_cache,
      "recognized duplicate fields are suppressed")
check('if (!knownCenterField ||' in order_cache,
      "unknown field tokens retain their legacy inert behavior")
check('hasKnownCenterField' in order_cache and 'damageIt + 1, "Poison"' in order_cache,
      "legacy recognized orders inject Poison once after Damage")
check('else {' in order_cache and '_centerFieldOrderCache.emplace_back("Poison")' in order_cache,
      "legacy recognized orders without Damage append Poison once")

check('bool poisonLineEmitted = false;' in center_cache,
      "clean ranged weapons begin with no poison line")
check('field == "Poison" && Config::AmmoWheel::CenterFields::ShowPoison' in center_cache,
      "ShowPoison gates the in-order Poison field")
check('Texts::GetText(Texts::TextType::AmmoWheelWeaponPoisonLabel)' in center_cache,
      "Poison uses the existing localized label")
check('_rangedWeaponPoisonPresentation.poisonName), cache.infoFontSize);' in center_cache,
      "Poison uses cache.infoFontSize")
check('cache.isDamageLine.push_back(false);\n\t\t\tpoisonLineEmitted = true;' in center_cache,
      "Poison follows the normal non-damage center draw path")
check(CPP.count('Texts::TextType::AmmoWheelWeaponPoisonLabel') == 1,
      "Poison is emitted only from the CenterFields loop")
check('if (poisonLineEmitted) { infoLineBudget++; }' in center_cache,
      "Poison reserves one panel line only when emitted")
check('cache.infoFontSize = std::clamp(\n\t\tConfig::AmmoWheel::CenterFontPx,' in center_cache,
      "CenterFontPx feeds the shared poison info-font path")

for required_case in (
    "exact ranged member without poison resolves clean",
    "exact ranged member poison resolves exact form",
    "poison removal changes presentation signature",
    "equipped poisoned sibling wins over clean same-form sibling",
    "multiple worn same-form members fail closed",
):
    check(required_case in POLICY_VERIFIER,
          f"existing resolver verifier retains: {required_case}")

for forbidden in ("EquipObject", "UnequipObject", "QueuePoisonApply", "ExtraDataList*", "InventoryEntryData*"):
    check(forbidden not in center_cache + presentation,
          f"center presentation retains no {forbidden} authority")

print("PASS AmmoWheel Poison CenterFields presentation integration")
