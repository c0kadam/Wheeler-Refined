#include "WheelItemLight.h"
#include "bin/Utilities/InventorySnapshotCache.h"
#include "bin/Rendering/TextureManager.h"
#include "bin/Rendering/Drawer.h"
#include "bin/Utilities/Utils.h"
#include "bin/Utilities/ItemCapabilities.h"
#include "bin/Config.h"
#include "bin/Wheeler/TransformWheelManager.h"

WheelItemLight::WheelItemLight(RE::TESObjectLIGH* a_light)
{
	if (!a_light->CanBeCarried()) {
		throw std::invalid_argument("WheelItemLight::ctor: Light must be carryable");
	}
	this->_light = a_light;
	// load texture
	this->_texture = Texture::GetIconImage(Texture::icon_image_type::torch, a_light);
}

void WheelItemLight::DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	if (TransformWheelManager::ShouldDimGearActivation()) {
		a_drawArgs.alphaMult *= 0.35f;
	}
	this->drawSlotText(a_center, _light->GetName(), a_drawArgs);
	this->drawSlotTexture(a_center, a_drawArgs);
}

void WheelItemLight::DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	if (TransformWheelManager::ShouldDimGearActivation()) {
		a_drawArgs.alphaMult *= 0.35f;
	}
	this->drawHighlightText(a_center, _light->GetName(), a_drawArgs);
	this->drawHighlightTexture(a_center, a_drawArgs);
}

bool WheelItemLight::IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	if (!a_inv.contains(this->_light)) {
		return false;
	}
	auto entry = a_inv.find(this->_light)->second.second.get();
	return entry && entry->IsWorn();
}

bool WheelItemLight::IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	return a_inv.contains(this->_light);
}

bool WheelItemLight::IsInPlayerInventory() const
{
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	return ItemCapabilities::IsInInventory(pc, _light, nullptr);
}

void WheelItemLight::ActivateItemSecondary()
{
	if (TransformWheelManager::ShouldBlockGearActivation()) {
		logger::info("TransformWheels: blocked gear activation source=LightSecondary formId={:08X} name='{}'",
			this->_light ? this->_light->GetFormID() : 0,
			this->_light ? this->_light->GetName() : "");
		return;
	}
	toggleEquip();
}

void WheelItemLight::ActivateItemPrimary()
{
	if (TransformWheelManager::ShouldBlockGearActivation()) {
		logger::info("TransformWheels: blocked gear activation source=LightPrimary formId={:08X} name='{}'",
			this->_light ? this->_light->GetFormID() : 0,
			this->_light ? this->_light->GetName() : "");
		return;
	}
	toggleEquip();
}

void WheelItemLight::SerializeIntoJsonObj(nlohmann::json& a_json)
{
	a_json["type"] = WheelItemLight::ITEM_TYPE_STR;
	a_json["formID"] = this->_light->GetFormID();
}


void WheelItemLight::toggleEquip()
{
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
	if (!pc || !aeMan) {
		logger::warn("[Light] toggleEquip: no player or equip manager");
		return;
	}
	
	RE::TESObjectREFR::InventoryItemMap invMap = pc->GetInventory();
	const RE::FormID formID = _light ? _light->GetFormID() : 0;
	const char* itemName = _light ? _light->GetName() : "(null)";
	
	if (!this->IsAvailable(invMap)) {
		logger::info("[Light] toggleEquip: '{}' not available in inventory, formID={:08X}", itemName, formID);
		return;
	}
	
	// Torches equip to left hand slot
	RE::BGSEquipSlot* leftSlot = Utils::Slot::GetLeftHandSlot();
	
	if (this->IsActive(invMap)) {
		logger::info("[Light] toggleEquip: UNEQUIP '{}' formID={:08X}", itemName, formID);
		InventorySnapshotCache::UnequipObject(aeMan, pc, this->_light, nullptr, 1, leftSlot);
	} else {
		logger::info("[Light] toggleEquip: EQUIP '{}' formID={:08X}", itemName, formID);
		InventorySnapshotCache::EquipObject(aeMan, pc, this->_light, nullptr, 1, leftSlot);
	}
}
