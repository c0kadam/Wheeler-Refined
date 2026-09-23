#pragma once
#include "WheelItem.h"
class WheelItemShout : public WheelItem
{
public:
	WheelItemShout() = delete;
	
	WheelItemShout(RE::TESShout* a_shout);
	virtual void DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual void DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual bool IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	virtual bool IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	virtual void ActivateItemSecondary() override;
	virtual void ActivateItemPrimary() override;
	virtual void SerializeIntoJsonObj(nlohmann::json& a_json) override;
	virtual void ActivateItemSpecial() override;
	virtual RE::FormID GetFormID() const override { return _shout ? _shout->GetFormID() : 0; }
	virtual const char* GetItemTypeName() const override { return ITEM_TYPE_STR; }
	virtual bool HasCooldown() const override { return true; }
	virtual float GetCooldownRemainingSeconds() const override;
	virtual float GetCooldownTotalSeconds() const override;

	/// <summary>
	/// Attempt to cast the shout immediately (used by InstantShout feature).
	/// hoverTime controls word level selection (longer hover = more words).
	/// Returns true if cast was initiated, false if on cooldown or failed.
	/// </summary>
	bool CastImmediate(float hoverTime);

	/// <summary>
	/// Get the underlying shout form (for stage sound tracking).
	/// </summary>
	RE::TESShout* GetShout() const { return _shout; }

	static inline const char* ITEM_TYPE_STR = "WheelItemShout";

private:
	RE::TESShout* _shout = nullptr;

	bool tryCastImmediate(float hoverTime);
};
