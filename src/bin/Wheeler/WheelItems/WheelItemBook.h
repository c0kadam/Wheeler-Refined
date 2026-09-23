#pragma once
#include "WheelItem.h"

class WheelItemBook : public WheelItem
{
public:
	WheelItemBook() = delete;
	WheelItemBook(RE::TESObjectBOOK* a_book);

	virtual void DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual void DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual bool IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	virtual bool IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	virtual void ActivateItemSecondary() override;
	virtual void ActivateItemPrimary() override;
	virtual bool MayQueueTransientGameplayAction() const override { return true; }

	virtual void SerializeIntoJsonObj(nlohmann::json& a_json) override;
	
	// Inventory sync overrides
	virtual bool IsInventoryBacked() const override { return true; }
	virtual bool IsInPlayerInventory() const override;  // implemented in .cpp
	virtual RE::FormID GetFormID() const override { return _formID; }
	virtual const char* GetItemTypeName() const override { return ITEM_TYPE_STR; }
	virtual const char* GetItemName() const override;

	static inline const char* ITEM_TYPE_STR = "WheelItemBook";

private:
	RE::TESObjectBOOK* _book = nullptr;
	RE::FormID _formID = 0;

	void readBook();
	
	// Safe resolution from FormID instead of raw pointer
	RE::TESObjectBOOK* ResolveBook();
};
