#pragma once
#include "WheelItem.h"
class WheelItemAlchemy : public WheelItem
{
public:
	WheelItemAlchemy() = delete;
	
	WheelItemAlchemy(RE::AlchemyItem* a_alchemyItem);
	
	~WheelItemAlchemy(){};
	
	virtual void DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual void DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	
	virtual bool IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	virtual bool IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv) override;

	virtual void ActivateItemPrimary() override;
	virtual void ActivateItemSecondary() override;
	virtual void ActivateItemSpecial() override;
	virtual WheelItemActivationResult ActivateItemWithResult(WheelItemActivationKind a_kind) override;
	virtual bool MayQueueTransientGameplayAction() const override { return true; }
	
	virtual void SerializeIntoJsonObj(nlohmann::json& a_json) override;
	
	static inline const char* ITEM_TYPE_STR = "WheelItemAlchemy";

	// Returns true if this alchemy item should be treated as missing/depleted in the player's inventory.
	// Safe for dynamic (crafted) items: returns true if the form cannot be resolved or player has 0 copies.
	bool IsDepletedInPlayerInventory();
	
	// Inventory sync overrides
	virtual bool IsInventoryBacked() const override { return true; }
	virtual bool IsInPlayerInventory() const override { 
		return !const_cast<WheelItemAlchemy*>(this)->IsDepletedInPlayerInventory(); 
	}
	virtual RE::FormID GetFormID() const override { return _formID; }
	virtual const char* GetItemTypeName() const override { return ITEM_TYPE_STR; }
	virtual const char* GetItemName() const override;

private:
	enum class WheelItemAlchemyType
	{
		kFood,
		kPotion,
		kPoison,
		kDeployable,  // Campfire tents, placeable items, other usable ALCH items
		kNone
	};
	// Cached pointer to the alchemy item. For dynamic (crafted) items this
	// pointer can become invalid once all copies are consumed, so gameplay
	// code must go through ResolveAlchemyItem() instead of using it directly.
	RE::AlchemyItem* _alchemyItem = nullptr;
	// FormID used to safely re-resolve the item from the game's form table.
	RE::FormID _formID = 0;
	WheelItemAlchemyType _alchemyItemType = WheelItemAlchemyType::kNone;

	// Returns a valid RE::AlchemyItem* if the item still exists and, for
	// dynamic forms, the player still has at least one copy. Returns nullptr
	// if the form cannot be resolved or should be treated as missing.
	RE::AlchemyItem* ResolveAlchemyItem();

	void consume();
	WheelItemActivationResult applyPoison();
};
