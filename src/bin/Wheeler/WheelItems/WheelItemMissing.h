#pragma once
#include <string>
#include "WheelItem.h"

class WheelItemMissing : public WheelItem
{
public:
	WheelItemMissing(std::string a_originalType, RE::FormID a_formID, std::uint16_t a_uniqueID,
		MissingCategory a_category, std::string a_displayName);

	void DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	void DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	bool IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv) override { return false; }
	bool IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv) override { return false; }
	void ActivateItemPrimary() override {}
	void ActivateItemSecondary() override {}
	void ActivateItemSpecial() override {}
	void SerializeIntoJsonObj(nlohmann::json& a_json) override;

	bool IsInventoryBacked() const override { return true; }
	bool IsInPlayerInventory() const override;
	RE::FormID GetFormID() const override { return _formID; }
	const char* GetItemName() const override;
	const char* GetItemTypeName() const override { return _originalType.c_str(); }

	const std::string& GetOriginalType() const { return _originalType; }
	std::uint16_t GetUniqueID() const { return _uniqueID; }
	const std::string& GetDisplayName() const { return _displayName; }

	static inline const char* ITEM_TYPE_STR = "WheelItemMissing";

private:
	std::string _originalType;
	RE::FormID _formID = 0;
	std::uint16_t _uniqueID = 0;
	std::string _displayName;
};
