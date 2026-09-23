#include "bin/Rendering/Drawer.h"
#include "bin/Utilities/Utils.h"
#include "bin/Wheeler/Wheeler.h"
#include "bin/Config.h"
#include "WheelItemIngredient.h"
#include "bin/Utilities/InventorySnapshotCache.h"

namespace
{
	static std::int32_t GetPlayerItemCount(RE::IngredientItem* a_item)
	{
		if (!a_item) {
			return 0;
		}

		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return 0;
		}

		auto counts = pc->GetInventoryCounts();
		auto it = counts.find(a_item);
		return it != counts.end() ? it->second : 0;
	}
}

WheelItemIngredient::WheelItemIngredient(RE::IngredientItem* a_ingredient)
{
	_ingredient = a_ingredient;
	if (!a_ingredient) {
		logger::warn("WheelItemIngredient: constructed with null ingredient");
		return;
	}

	_formID = a_ingredient->GetFormID();
	_missingCategory = MissingCategory::Consumable;
	_texture = Texture::GetIconImage(Texture::icon_image_type::food, a_ingredient);
	Utils::Magic::GetMagicItemDescription(a_ingredient, _description);
}

void WheelItemIngredient::DrawSlot(ImVec2 a_center, bool, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	RE::IngredientItem* ingredient = ResolveIngredientItem();
	if (!ingredient) {
		return;
	}

	const auto it = a_imap.find(ingredient);
	const int itemCount = it != a_imap.end() ? it->second.first : 0;
	if (Config::WheelBehavior::ClearDepletedConsumables &&
		itemCount <= 0 &&
		!IsKeepMissingCategoryEnabled(MissingCategory::Consumable)) {
		Wheeler::QueueDepletedConsumablesCleanup();
		return;
	}

	std::string text = fmt::format("{} ({})", ingredient->GetName(), itemCount);
	drawSlotText(a_center, text.data(), a_drawArgs);
	drawSlotTexture(a_center, a_drawArgs);
}

void WheelItemIngredient::DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	RE::IngredientItem* ingredient = ResolveIngredientItem();
	if (!ingredient) {
		return;
	}

	const auto it = a_imap.find(ingredient);
	const int itemCount = it != a_imap.end() ? it->second.first : 0;
	if (Config::WheelBehavior::ClearDepletedConsumables &&
		itemCount <= 0 &&
		!IsKeepMissingCategoryEnabled(MissingCategory::Consumable)) {
		Wheeler::QueueDepletedConsumablesCleanup();
		return;
	}

	const float textShiftY = calculateHighlightTextShiftY(_description.c_str());
	drawHighlightText(a_center, ingredient->GetName(), a_drawArgs, textShiftY);
	drawHighlightTexture(a_center, a_drawArgs);
	if (!_description.empty()) {
		drawHighlightDescription(a_center, _description.data(), a_drawArgs, textShiftY);
	}
}

bool WheelItemIngredient::IsActive(RE::TESObjectREFR::InventoryItemMap&)
{
	return false;
}

bool WheelItemIngredient::IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	RE::IngredientItem* ingredient = ResolveIngredientItem();
	if (!ingredient) {
		return false;
	}

	const auto it = a_inv.find(ingredient);
	return it != a_inv.end() && it->second.first > 0;
}

void WheelItemIngredient::ActivateItemPrimary()
{
	useIngredient();
}

void WheelItemIngredient::ActivateItemSecondary()
{
	useIngredient();
}

void WheelItemIngredient::ActivateItemSpecial()
{
	return;
}

void WheelItemIngredient::SerializeIntoJsonObj(nlohmann::json& a_json)
{
	a_json["type"] = WheelItemIngredient::ITEM_TYPE_STR;
	a_json["formID"] = _formID;
}

bool WheelItemIngredient::IsDepletedInPlayerInventory()
{
	RE::IngredientItem* ingredient = ResolveIngredientItem();
	if (!ingredient) {
		return true;
	}
	return GetPlayerItemCount(ingredient) <= 0;
}

RE::IngredientItem* WheelItemIngredient::ResolveIngredientItem()
{
	if (_formID == 0) {
		return nullptr;
	}

	RE::IngredientItem* ingredient = RE::TESForm::LookupByID<RE::IngredientItem>(_formID);
	if (!ingredient) {
		return nullptr;
	}

	return ingredient;
}

void WheelItemIngredient::useIngredient()
{
	if (!Config::WheelBehavior::AllowIngredientUse) {
		if (Config::Debug::LogActionPolicy) {
			logger::info("[IngredientItem] Activate skipped: ingredient support disabled formID={:08X}", _formID);
		}
		return;
	}

	RE::IngredientItem* ingredient = ResolveIngredientItem();
	if (!ingredient) {
		return;
	}

	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}

	const int countBefore = GetPlayerItemCount(ingredient);
	if (countBefore <= 0) {
		if (Config::WheelBehavior::ClearDepletedConsumables &&
			!IsKeepMissingCategoryEnabled(MissingCategory::Consumable)) {
			Wheeler::QueueDepletedConsumablesCleanup();
		}
		return;
	}

	RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
	if (!aeMan) {
		return;
	}

	if (Config::Debug::LogActionPolicy) {
		logger::info("[IngredientItem] Activate: '{}' formID={:08X} count={} path=immediate_equip",
			ingredient->GetName() ? ingredient->GetName() : "(null)",
			ingredient->GetFormID(),
			countBefore);
	}

	InventorySnapshotCache::EquipObject(aeMan, pc, ingredient);

	if (Config::WheelBehavior::ClearDepletedConsumables &&
		countBefore <= 1 &&
		!IsKeepMissingCategoryEnabled(MissingCategory::Consumable)) {
		Wheeler::QueueDepletedConsumablesCleanup();
	}
}

const char* WheelItemIngredient::GetItemName() const
{
	RE::IngredientItem* ingredient = RE::TESForm::LookupByID<RE::IngredientItem>(_formID);
	return ingredient ? ingredient->GetName() : "(deleted)";
}
