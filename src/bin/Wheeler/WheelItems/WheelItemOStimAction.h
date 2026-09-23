#pragma once

#include "bin/Integrations/OStimTypes.h"

#include "WheelItem.h"

class WheelItemOStimAction : public WheelItem
{
public:
	WheelItemOStimAction() = delete;
	explicit WheelItemOStimAction(OStimActionPayload a_payload);

	void DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	void DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	bool IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	bool IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	void ActivateItemPrimary() override;
	void ActivateItemSecondary() override;
	void ActivateItemSpecial() override;
	void SerializeIntoJsonObj(nlohmann::json& a_json) override;

	bool RequiresRuntimeFormValidation() const override { return false; }
	const char* GetItemTypeName() const override { return ITEM_TYPE_STR; }
	const char* GetItemName() const override;

	const OStimActionPayload& GetPayload() const { return _payload; }

	static inline const char* ITEM_TYPE_STR = "WheelItemOStimAction";

private:
	std::string BuildDescription() const;

	OStimActionPayload _payload;
	std::string _label;
	bool _usesCustomManagedIcon{ false };
};
