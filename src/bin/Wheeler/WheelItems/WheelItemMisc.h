#pragma once
#include "WheelItem.h"
class WheelItemMisc : public WheelItem
{
public:
	WheelItemMisc() = delete;
	WheelItemMisc(RE::TESObjectMISC* a_miscItem);

	virtual void DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual void DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual bool IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	virtual bool IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	virtual void ActivateItemSecondary() override;
	virtual void ActivateItemPrimary() override;
	virtual bool MayQueueTransientGameplayAction() const override { return true; }

	virtual void SerializeIntoJsonObj(nlohmann::json& a_json) override;
	virtual RE::FormID GetFormID() const override { return _miscItem ? _miscItem->GetFormID() : 0; }
	virtual const char* GetItemTypeName() const override { return ITEM_TYPE_STR; }
	virtual const char* GetItemName() const override { return _miscItem ? _miscItem->GetName() : "(deleted)"; }
	
	// Inventory sync overrides
	virtual bool IsInventoryBacked() const override { return true; }
	virtual bool IsInPlayerInventory() const override;

	static inline const char* ITEM_TYPE_STR = "WheelItemMisc";

private:
	RE::TESObjectMISC* _miscItem;

	void useItem();
};

namespace YpsItems
{
	bool IsYpsItem(RE::TESObjectMISC* a_item);
}

namespace ShovelItems
{
	bool IsShovelItem(RE::TESObjectMISC* a_item);
}
