#include "bin/Utilities/Utils.h"
#include "bin/Config.h"
#include "bin/Wheeler/ActionPolicy.h"
#include "WheelItemScroll.h"
#include "bin/Utilities/InventorySnapshotCache.h"

WheelItemScroll::WheelItemScroll(RE::ScrollItem* a_scroll)
{
	this->_scroll = a_scroll;
	this->_formID = a_scroll ? a_scroll->GetFormID() : 0;
	RE::BSString descriptionBuf = "";
	this->_scroll->GetDescription(descriptionBuf, nullptr);
	this->_description = descriptionBuf;
	this->_texture = Texture::GetIconImage(Texture::icon_image_type::scroll, a_scroll);
}

void WheelItemScroll::DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	std::string text = this->_scroll->GetName();
	int itemCount = a_imap.contains(this->_scroll) ? a_imap[this->_scroll].first : 0;
	if (itemCount > 0) {
		text += fmt::format(" ({})", itemCount);
	}
	this->drawSlotText(a_center, text.data(), a_drawArgs);
	this->drawSlotTexture(a_center, a_drawArgs);
}

void WheelItemScroll::DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	std::string descriptionBuf = "";
	descriptionBuf = this->_description;
	if (descriptionBuf.empty()) {
		Utils::Magic::GetMagicItemDescription(this->_scroll, descriptionBuf);
	}
	const float textShiftY = calculateHighlightTextShiftY(descriptionBuf.c_str());
	this->drawHighlightText(a_center, this->_scroll->GetName(), a_drawArgs, textShiftY);
	this->drawHighlightTexture(a_center, a_drawArgs);
	this->drawHighlightDescription(a_center, descriptionBuf.data(), a_drawArgs, textShiftY);
}

bool WheelItemScroll::IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	auto pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return false;
	}
	RE::TESForm* lhs = pc->GetEquippedObject(true);
	if (lhs) {
		if (lhs && lhs->GetFormID() == this->_scroll->GetFormID()) {
			return true;
		}
	}
	RE::TESForm* rhs = pc->GetEquippedObject(false);
	if (rhs) {
		if (rhs && rhs->GetFormID() == this->_scroll->GetFormID()) {
			return true;
		}
	}
	return false;
}

bool WheelItemScroll::IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	// Scroll is available if in inventory OR if currently equipped (for toggle-off)
	if (a_inv.contains(this->_scroll) && a_inv[this->_scroll].first > 0) {
		return true;
	}
	// Also consider available if equipped (allows unequip even if count shows 0)
	return this->IsActive(a_inv);
}

void WheelItemScroll::ActivateItemSecondary()
{
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
	if (!pc || !aeMan) {
		return;
	}
	
	ActionPolicy::SetDebugLogging(Config::Debug::LogActionPolicy);
	
	// Check BOTH hands for toggle-off - scroll might be in either hand
	RE::TESForm* lhs = pc->GetEquippedObject(true);   // left hand
	RE::TESForm* rhs = pc->GetEquippedObject(false);  // right hand
	
	// If scroll is in LEFT hand (target hand for secondary), toggle off from left
	if (lhs && lhs->GetFormID() == this->_scroll->GetFormID()) {
		InventorySnapshotCache::UnequipObject(aeMan, pc, this->_scroll, nullptr, 1, Utils::Slot::GetLeftHandSlot());
		if (Config::Debug::LogActionPolicy) {
			logger::info("[Scroll] ActivateSecondary: toggled OFF from left hand, formID={:08X}", _formID);
		}
		return;
	}
	
	// If scroll is in RIGHT hand, toggle off from right (user wants to unequip, not re-equip to other hand)
	if (rhs && rhs->GetFormID() == this->_scroll->GetFormID()) {
		InventorySnapshotCache::UnequipObject(aeMan, pc, this->_scroll, nullptr, 1, Utils::Slot::GetRightHandSlot());
		if (Config::Debug::LogActionPolicy) {
			logger::info("[Scroll] ActivateSecondary: toggled OFF from right hand (was in other hand), formID={:08X}", _formID);
		}
		return;
	}
	
	// Not equipped - check availability for equip
	bool available = false;
	{
		RE::TESObjectREFR::InventoryItemMap inventory = pc->GetInventory();
		const auto it = inventory.find(this->_scroll);
		available = it != inventory.end() && it->second.first > 0;
	}
	if (!available) {
		if (Config::Debug::LogActionPolicy) {
			logger::info("[Scroll] ActivateSecondary: not available in inventory, formID={:08X}", _formID);
		}
		return;
	}
	
	// Clear left hand if occupied by something else
	if (lhs != nullptr) {
		Utils::Slot::CleanSlot(pc, Utils::Slot::GetLeftHandSlot());
		if (Config::Debug::LogActionPolicy) {
			logger::info("[Scroll] ActivateSecondary: cleared occupied left slot, formID={:08X}", _formID);
		}
	}
	
	// Equip the scroll to left hand
	InventorySnapshotCache::EquipObject(aeMan, pc, this->_scroll, nullptr, 1, Utils::Slot::GetLeftHandSlot());
	
	// Post-condition verification
	RE::TESForm* newLhs = pc->GetEquippedObject(true);
	bool success = newLhs && newLhs->GetFormID() == this->_scroll->GetFormID();
	
	if (Config::Debug::LogActionPolicy) {
		logger::info("[Scroll] ActivateSecondary: equip result={} formID={:08X} name='{}'",
			success ? "SUCCESS" : "FAILED", _formID, _scroll->GetName());
	}
}

void WheelItemScroll::ActivateItemPrimary()
{
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
	if (!pc || !aeMan) {
		return;
	}
	
	ActionPolicy::SetDebugLogging(Config::Debug::LogActionPolicy);
	
	// Check BOTH hands for toggle-off - scroll might be in either hand
	RE::TESForm* lhs = pc->GetEquippedObject(true);   // left hand
	RE::TESForm* rhs = pc->GetEquippedObject(false);  // right hand
	
	// If scroll is in RIGHT hand (target hand for primary), toggle off from right
	if (rhs && rhs->GetFormID() == this->_scroll->GetFormID()) {
		InventorySnapshotCache::UnequipObject(aeMan, pc, this->_scroll, nullptr, 1, Utils::Slot::GetRightHandSlot());
		if (Config::Debug::LogActionPolicy) {
			logger::info("[Scroll] ActivatePrimary: toggled OFF from right hand, formID={:08X}", _formID);
		}
		return;
	}
	
	// If scroll is in LEFT hand, toggle off from left (user wants to unequip, not re-equip to other hand)
	if (lhs && lhs->GetFormID() == this->_scroll->GetFormID()) {
		InventorySnapshotCache::UnequipObject(aeMan, pc, this->_scroll, nullptr, 1, Utils::Slot::GetLeftHandSlot());
		if (Config::Debug::LogActionPolicy) {
			logger::info("[Scroll] ActivatePrimary: toggled OFF from left hand (was in other hand), formID={:08X}", _formID);
		}
		return;
	}
	
	// Not equipped - check availability for equip
	bool available = false;
	{
		RE::TESObjectREFR::InventoryItemMap inventory = pc->GetInventory();
		const auto it = inventory.find(this->_scroll);
		available = it != inventory.end() && it->second.first > 0;
	}
	if (!available) {
		if (Config::Debug::LogActionPolicy) {
			logger::info("[Scroll] ActivatePrimary: not available in inventory, formID={:08X}", _formID);
		}
		return;
	}
	
	// Clear right hand if occupied by something else
	if (rhs != nullptr) {
		Utils::Slot::CleanSlot(pc, Utils::Slot::GetRightHandSlot());
		if (Config::Debug::LogActionPolicy) {
			logger::info("[Scroll] ActivatePrimary: cleared occupied right slot, formID={:08X}", _formID);
		}
	}
	
	// Equip the scroll to right hand
	InventorySnapshotCache::EquipObject(aeMan, pc, this->_scroll, nullptr, 1, Utils::Slot::GetRightHandSlot());
	
	// Post-condition verification
	RE::TESForm* newRhs = pc->GetEquippedObject(false);
	bool success = newRhs && newRhs->GetFormID() == this->_scroll->GetFormID();
	
	if (Config::Debug::LogActionPolicy) {
		logger::info("[Scroll] ActivatePrimary: equip result={} formID={:08X} name='{}'",
			success ? "SUCCESS" : "FAILED", _formID, _scroll->GetName());
	}
}

void WheelItemScroll::SerializeIntoJsonObj(nlohmann::json& a_json)
{
	a_json["type"] = WheelItemScroll::ITEM_TYPE_STR;
	a_json["formID"] = this->_formID;
}

bool WheelItemScroll::IsInPlayerInventory() const
{
	if (_formID == 0) {
		return false;
	}
	
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return false;
	}
	
	RE::ScrollItem* scroll = RE::TESForm::LookupByID<RE::ScrollItem>(_formID);
	if (!scroll) {
		return false;
	}
	
	auto counts = pc->GetInventoryCounts();
	auto it = counts.find(scroll);
	return it != counts.end() && it->second > 0;
}
