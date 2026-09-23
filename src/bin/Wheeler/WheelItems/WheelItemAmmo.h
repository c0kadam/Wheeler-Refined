#pragma once
#include "WheelItem.h"
class WheelItemAmmo : public WheelItem
{
public:
	WheelItemAmmo() = delete;
	WheelItemAmmo(RE::TESAmmo* a_ammo);

	virtual void DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual void DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual bool IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	virtual bool IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	virtual void ActivateItemSecondary() override;
	virtual void ActivateItemPrimary() override;

	virtual void SerializeIntoJsonObj(nlohmann::json& a_json) override;
	virtual bool IsInventoryBacked() const override { return true; }
	virtual bool IsInPlayerInventory() const override;
	virtual RE::FormID GetFormID() const override { return _ammo ? _ammo->GetFormID() : 0; }
	virtual const char* GetItemTypeName() const override { return ITEM_TYPE_STR; }
	virtual const char* GetItemName() const override { return _ammo ? _ammo->GetName() : "(deleted)"; }

	static inline const char* ITEM_TYPE_STR = "WheelItemAmmo";

private:
	RE::TESAmmo* _ammo;

	void toggleEquip();
};
