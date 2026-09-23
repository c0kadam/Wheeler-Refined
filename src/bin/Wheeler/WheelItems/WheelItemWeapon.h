#pragma once
#include "WheelItemMutable.h"
#include "LegacyWeaponRestore.h"
#include "bin/Animation/TimeColorInterpolator.h"

#include <string>
#include <utility>

class TimeColorInterpolator;
class WheelItemWeapon : public WheelItemMutable 
{
public:
	WheelItemWeapon() = delete;
	void DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	void DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	bool IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	bool IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	WheelItemWeapon(RE::TESBoundObject* a_weapon, uint16_t a_uniqueID);

	~WheelItemWeapon();
	void ActivateItemSecondary() override;
	void ActivateItemPrimary() override;
	virtual const char* GetItemTypeName() const override { return ITEM_TYPE_STR; }
	std::optional<bool> MatchesEquippedHandIndicator(
		RE::TESObjectREFR::InventoryItemMap& a_inv,
		RE::FormID a_handFormID,
		std::uint64_t a_handSignature,
		bool a_leftHand) const override;
	WeaponPresentationHandState GetTransientDrawOnlyHandPresentation() const override;
	
	virtual void SerializeIntoJsonObj(nlohmann::json& a_json) override;
	void RestoreLogicalRowSignature(std::string a_signature) { _logicalRowSignature = std::move(a_signature); }


	static inline const char* ITEM_TYPE_STR = "WheelItemWeapon";
	static void ProcessIWSCompatTransfer();
	static void ProcessGroupedPoisonLineageDiagnostic();
	static void ResetTransientStateForLifecycle();
	static void CancelTransientStateInCurrentWorld();

private:
	// Returns false when activation was safely deferred and owns post-equip draw restoration.
	bool equipItem(bool a_toRight = true);
	void unequipItem(const RE::BGSEquipSlot* a_slot);
	std::string _logicalRowSignature;
	std::uint64_t _runtimePresentationSlotID = 0;
	int _drawOnlyPresentationFrame = -1;
	WeaponPresentationHandState _drawOnlyHandPresentation{};
};
