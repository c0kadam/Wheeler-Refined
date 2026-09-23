from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise AssertionError(f"missing function: {signature}")
    opening = text.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing body: {signature}")
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening : index + 1]
    raise AssertionError(f"unterminated body: {signature}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def verify_poison_result_propagation() -> None:
    item_h = read("src/bin/Wheeler/WheelItems/WheelItem.h")
    alchemy_h = read("src/bin/Wheeler/WheelItems/WheelItemAlchemy.h")
    alchemy_cpp = read("src/bin/Wheeler/WheelItems/WheelItemAlchemy.cpp")
    wheeler_cpp = read("src/bin/Wheeler/Wheeler.cpp")
    wheeler_h = read("src/bin/Wheeler/Wheeler.h")

    for result in (
        "Succeeded",
        "AlreadyPoisoned",
        "UnsafeResolution",
        "InvalidTarget",
    ):
        require(result in item_h, f"activation result lacks {result}")
    require("ActivateItemWithResult" in item_h, "base result-aware activation is absent")
    require("ActivateItemWithResult" in alchemy_h, "alchemy result override is absent")

    apply_body = function_body(
        alchemy_cpp, "WheelItemActivationResult WheelItemAlchemy::applyPoison()"
    )
    require(
        "Decision::kBlockAlreadyPoisoned" in apply_body
        and "WheelItemActivationResult::AlreadyPoisoned" in apply_body,
        "already-poisoned rejection is not classified",
    )
    require(
        "WheelItemActivationResult::UnsafeResolution" in apply_body,
        "ambiguous poison rejection is not classified",
    )
    require(
        "if (!Wheeler::QueuePoisonApply(_formID))" in apply_body,
        "poison queue admission is not result-aware",
    )
    require(
        apply_body.find("QueueDepletedConsumablesCleanup")
        > apply_body.find("if (!Wheeler::QueuePoisonApply(_formID))"),
        "depleted cleanup can be requested before successful poison queue admission",
    )
    require(
        "static bool QueuePoisonApply" in wheeler_h,
        "QueuePoisonApply does not expose admission success",
    )

    execute_body = function_body(
        wheeler_cpp,
        "bool Wheeler::ExecutePreparedWheelItemActivation(PreparedWheelItemActivation a_activation)",
    )
    result_check = execute_body.find("if (!IsSuccessfulActivation(activationResult))")
    api_notify = execute_body.find("WheelerAPI::NotifyItemActivated")
    require(result_check >= 0, "prepared activation ignores gameplay result")
    require(api_notify > result_check, "API notification precedes gameplay-success check")
    require(
        "return false;" in execute_body[result_check:api_notify],
        "rejected gameplay result does not stop success bookkeeping",
    )


def verify_poison_texts() -> None:
    texts_h = read("src/bin/Texts.h")
    texts_cpp = read("src/bin/Texts.cpp")
    translations = read("Data/SKSE/Plugins/wheeler/translations.txt")
    alchemy_cpp = read("src/bin/Wheeler/WheelItems/WheelItemAlchemy.cpp")
    for key in ("PoisonAlreadyApplied", "PoisonSafeResolutionFailed"):
        require(key in texts_h, f"Texts enum/default lacks {key}")
        require(key in texts_cpp, f"Texts key mapping lacks {key}")
        require(key in translations, f"translation template lacks {key}")
        require(
            f"Texts::TextType::{key}" in alchemy_cpp,
            f"poison rejection does not use localized {key}",
        )


def verify_magic_menu() -> None:
    utils = read("src/bin/Utilities/Utils.cpp")
    factory = read("src/bin/Wheeler/WheelItems/WheelItemFactory.cpp")
    body = function_body(
        utils, "RE::TESForm* GetSelectedFormInMagicMenu(RE::MagicMenu* a_magMen)"
    )
    native_tokens = (
        "GetRuntimeData().itemList",
        "GetSelectedItem()",
        "selectedItem->data.baseForm",
    )
    for token in native_tokens:
        require(token in body, f"native MagicMenu selection lacks {token}")
    legacy = '"_root.Menu_mc.inventoryLists.itemList.selectedEntry.formId"'
    require(legacy in body, "legacy MagicMenu GFx fallback was removed")
    require(
        body.find("GetRuntimeData().itemList") < body.find(legacy),
        "native MagicMenu selection is not preferred over GFx",
    )
    require(
        "ui->GetMenu<RE::MagicMenu>()" in factory,
        "factory does not use the pinned typed MagicMenu accessor",
    )


def verify_scroll_snapshot_scope() -> None:
    scroll = read("src/bin/Wheeler/WheelItems/WheelItemScroll.cpp")
    for signature in (
        "void WheelItemScroll::ActivateItemPrimary()",
        "void WheelItemScroll::ActivateItemSecondary()",
    ):
        body = function_body(scroll, signature)
        require(body.count("pc->GetInventory()") == 1, f"{signature} snapshot count changed")
        require("bool available = false;" in body, f"{signature} lacks scalar availability handoff")
        snapshot = re.search(
            r"\{\s*RE::TESObjectREFR::InventoryItemMap inventory = pc->GetInventory\(\);"
            r".*?available = .*?;\s*\}",
            body,
            re.DOTALL,
        )
        require(snapshot is not None, f"{signature} snapshot lacks lexical lifetime boundary")
        first_mutation_positions = [
            position
            for token in (
                "Utils::Slot::CleanSlot",
                "InventorySnapshotCache::EquipObject",
            )
            if (position := body.find(token)) >= 0
        ]
        require(first_mutation_positions, f"{signature} has no expected mutation")
        require(
            snapshot.end() < min(first_mutation_positions),
            f"{signature} snapshot survives to a CleanSlot/equip mutation",
        )


def main() -> int:
    try:
        verify_poison_result_propagation()
        verify_poison_texts()
        verify_magic_menu()
        verify_scroll_snapshot_scope()
    except AssertionError as error:
        print(f"TARGETED BUGFIX VERIFICATION FAILED: {error}")
        return 1

    print("TARGETED BUGFIX VERIFICATION PASSED")
    print("- poison no-op/rejection results stop API and DirectActivate success bookkeeping")
    print("- localized poison rejection messages are wired through Texts")
    print("- native vanilla MagicMenu selection precedes the retained GFx fallback")
    print("- both scroll inventory snapshots end before equipment mutation")
    return 0


if __name__ == "__main__":
    sys.exit(main())
