#pragma once
#include "WheelItem.h"
class WheelItemScroll : public WheelItem
{
public:
	WheelItemScroll() = delete;
	WheelItemScroll(RE::ScrollItem* a_scroll);

	virtual void DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual void DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual bool IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	virtual bool IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	virtual void ActivateItemSecondary() override;
	virtual void ActivateItemPrimary() override;

	virtual void SerializeIntoJsonObj(nlohmann::json& a_json) override;
	virtual RE::FormID GetFormID() const override { return _formID; }
	virtual const char* GetItemTypeName() const override { return ITEM_TYPE_STR; }
	virtual const char* GetItemName() const override { return _scroll ? _scroll->GetName() : "(deleted)"; }
	virtual bool IsInventoryBacked() const override { return true; }
	virtual bool IsInPlayerInventory() const override;

	static inline const char* ITEM_TYPE_STR = "WheelItemScroll";

private:
	RE::ScrollItem* _scroll = nullptr;
	RE::FormID _formID = 0;
};
