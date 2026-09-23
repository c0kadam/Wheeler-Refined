#pragma once
#include "WheelItem.h"
class WheelItemSpell : public WheelItem // spell AND power
{
public:
	WheelItemSpell() = delete;
	WheelItemSpell(RE::SpellItem* a_spell);
	virtual void DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual void DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	virtual bool IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	virtual bool IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	virtual void ActivateItemSecondary() override;
	virtual void ActivateItemPrimary() override;
	virtual void SerializeIntoJsonObj(nlohmann::json& a_json) override;
	virtual void ActivateItemSpecial() override;
	virtual RE::FormID GetFormID() const override { return _spell ? _spell->GetFormID() : 0; }
	virtual const char* GetItemTypeName() const override { return ITEM_TYPE_STR; }
	RE::SpellItem* GetSpell() const { return _spell; }

	// Cast the spell immediately (bypasses normal charging/aiming flow).
	// If allowNonInstant is false, only spells flagged as instant and powers are allowed.
	bool CastImmediate(bool allowNonInstant);
	bool CastImmediate(bool allowNonInstant, RE::MagicSystem::CastingSource castingSource);

	// Check if a spell is a transformation power (Beast Form, Vampire Lord, Werebear, etc.)
	// Used by Wheeler to determine which instant-cast toggle applies.
	static bool IsTransformationSpell(RE::SpellItem* spell);

	virtual bool HasCooldown() const override;
	virtual float GetCooldownRemainingSeconds() const override;
	virtual float GetCooldownTotalSeconds() const override;

	static inline const char* ITEM_TYPE_STR = "WheelItemSpell";

private:
	bool isPower() const;
	bool tryCastImmediate(bool allowNonInstant, RE::MagicSystem::CastingSource castingSource);
	RE::SpellItem* _spell = nullptr;
};
