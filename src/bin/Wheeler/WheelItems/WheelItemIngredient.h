#pragma once
#include "WheelItem.h"

class WheelItemIngredient : public WheelItem
{
public:
	WheelItemIngredient() = delete;
	WheelItemIngredient(RE::IngredientItem* a_ingredient);

	virtual void DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual void DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual bool IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	virtual bool IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv) override;

	virtual void ActivateItemPrimary() override;
	virtual void ActivateItemSecondary() override;
	virtual void ActivateItemSpecial() override;
	virtual bool MayQueueTransientGameplayAction() const override { return true; }

	virtual void SerializeIntoJsonObj(nlohmann::json& a_json) override;

	static inline const char* ITEM_TYPE_STR = "WheelItemIngredient";

	bool IsDepletedInPlayerInventory();

	virtual bool IsInventoryBacked() const override { return true; }
	virtual bool IsInPlayerInventory() const override
	{
		return !const_cast<WheelItemIngredient*>(this)->IsDepletedInPlayerInventory();
	}
	virtual RE::FormID GetFormID() const override { return _formID; }
	virtual const char* GetItemTypeName() const override { return ITEM_TYPE_STR; }
	virtual const char* GetItemName() const override;

private:
	RE::IngredientItem* _ingredient = nullptr;
	RE::FormID _formID = 0;

	RE::IngredientItem* ResolveIngredientItem();
	void useIngredient();
};
