import json
from pathlib import Path


FILE_PATH = Path("Data/SKSE/Plugins/dmenu/customSettings/Wheeler Styles.json")

INVENTORY_CATEGORY_IDS = [
    "UseForWeapons",
    "UseForArmor",
    "UseForAmmo",
    "UseForPotions",
    "UseForFood",
    "UseForPoisons",
    "UseForBooks",
    "UseForScrolls",
    "UseForLights",
    "UseForMisc",
]

ALTERNATIVE_CATEGORY_IDS = [
    "UseForSpells",
    "UseForShouts",
    "UseForPowers",
]

ALL_CATEGORY_IDS = INVENTORY_CATEGORY_IDS + ALTERNATIVE_CATEGORY_IDS


def find_group_by_name(node, group_name):
    if isinstance(node, dict):
        text = node.get("text")
        if isinstance(text, dict) and text.get("name") == group_name and node.get("type") == "group":
            return node
        for value in node.values():
            found = find_group_by_name(value, group_name)
            if found is not None:
                return found
    elif isinstance(node, list):
        for item in node:
            found = find_group_by_name(item, group_name)
            if found is not None:
                return found
    return None


def entry_ini_id(entry):
    ini = entry.get("ini")
    if isinstance(ini, dict):
        return ini.get("id")
    return None


def main():
    data = json.loads(FILE_PATH.read_text(encoding="utf-8-sig"))
    i4_group = find_group_by_name(data, "Inventory Injector (I4)")
    if i4_group is None:
        raise RuntimeError("Inventory Injector (I4) group not found in JSON")

    entries = i4_group.get("entries")
    if not isinstance(entries, list):
        raise RuntimeError("I4 group entries is not a list")

    # Add control id to UseAlternativePath for requirements wiring.
    alt_path_entry = next((e for e in entries if entry_ini_id(e) == "UseAlternativePath"), None)
    if alt_path_entry is None:
        raise RuntimeError("UseAlternativePath entry not found")
    alt_path_entry.setdefault("control", {})
    alt_path_entry["control"]["id"] = "wheeler_i4_use_alternative_path"

    # Gather category entries from current flat structure.
    category_entry_map = {entry_ini_id(e): e for e in entries if entry_ini_id(e) in ALL_CATEGORY_IDS}
    missing = [cat_id for cat_id in ALL_CATEGORY_IDS if cat_id not in category_entry_map]
    if missing:
        raise RuntimeError(f"Missing expected I4 category entries: {missing}")

    first_category_index = next(i for i, e in enumerate(entries) if entry_ini_id(e) in ALL_CATEGORY_IDS)
    entries[:] = [e for e in entries if entry_ini_id(e) not in ALL_CATEGORY_IDS]

    inventory_group = {
        "control": {
            "failAction": "disable",
        },
        "entries": [category_entry_map[cat_id] for cat_id in INVENTORY_CATEGORY_IDS],
        "text": {
            "desc": "Inventory-backed categories for I4 icon routing.",
            "name": "Inventory Categories",
        },
        "translation": {
            "desc": "",
            "name": "",
        },
        "type": "group",
    }

    alternative_group = {
        "control": {
            "failAction": "disable",
            "requirements": [
                {
                    "id": "wheeler_i4_use_alternative_path",
                    "type": "checkbox",
                    "value": True,
                }
            ],
        },
        "entries": [category_entry_map[cat_id] for cat_id in ALTERNATIVE_CATEGORY_IDS],
        "text": {
            "desc": "Applies only when Use Alternative Path is enabled.",
            "name": "Alternative Path Categories",
        },
        "translation": {
            "desc": "",
            "name": "",
        },
        "type": "group",
    }

    category_group = {
        "control": {
            "failAction": "disable",
            "requirements": [
                {
                    "id": "wheeler_i4_enabled",
                    "type": "checkbox",
                    "value": True,
                }
            ],
        },
        "entries": [
            inventory_group,
            alternative_group,
        ],
        "text": {
            "desc": "Choose which categories route through I4. Disabled categories always use Wheeler fallback icons.",
            "name": "Category Routing",
        },
        "translation": {
            "desc": "",
            "name": "",
        },
        "type": "group",
    }

    entries.insert(first_category_index, category_group)
    FILE_PATH.write_text(json.dumps(data, indent=4, ensure_ascii=False) + "\n", encoding="utf-8-sig")


if __name__ == "__main__":
    main()
