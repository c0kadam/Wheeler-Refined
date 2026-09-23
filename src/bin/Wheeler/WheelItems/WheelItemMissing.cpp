#include "WheelItemMissing.h"
#include "bin/Rendering/Drawer.h"
#include "bin/Rendering/TextureManager.h"

namespace
{
	const char* GetMissingDisplayName(const std::string& name)
	{
		if (!name.empty()) {
			return name.c_str();
		}
		return "Missing Item";
	}
}

WheelItemMissing::WheelItemMissing(std::string a_originalType, RE::FormID a_formID, std::uint16_t a_uniqueID,
	MissingCategory a_category, std::string a_displayName) :
	_originalType(std::move(a_originalType)),
	_formID(a_formID),
	_uniqueID(a_uniqueID),
	_displayName(std::move(a_displayName))
{
	_missingCategory = a_category;
	_texture = Texture::GetIconImage(Texture::icon_image_type::icon_default, nullptr);
}

void WheelItemMissing::DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	const char* name = GetMissingDisplayName(_displayName);
	this->drawSlotText(a_center, name, a_drawArgs);
	this->drawSlotTexture(a_center, a_drawArgs);
}

void WheelItemMissing::DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	const char* name = GetMissingDisplayName(_displayName);
	this->drawHighlightText(a_center, name, a_drawArgs);
	this->drawHighlightTexture(a_center, a_drawArgs);
}

void WheelItemMissing::SerializeIntoJsonObj(nlohmann::json& a_json)
{
	a_json["type"] = _originalType.empty() ? ITEM_TYPE_STR : _originalType;
	a_json["formID"] = _formID;
	if (_uniqueID != 0) {
		a_json["uniqueID"] = _uniqueID;
	}
}

bool WheelItemMissing::IsInPlayerInventory() const
{
	if (_formID == 0) {
		return false;
	}
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return false;
	}
	RE::TESBoundObject* bound = RE::TESForm::LookupByID<RE::TESBoundObject>(_formID);
	if (!bound) {
		return false;
	}
	auto counts = pc->GetInventoryCounts();
	auto it = counts.find(bound);
	return it != counts.end() && it->second > 0;
}

const char* WheelItemMissing::GetItemName() const
{
	return GetMissingDisplayName(_displayName);
}
