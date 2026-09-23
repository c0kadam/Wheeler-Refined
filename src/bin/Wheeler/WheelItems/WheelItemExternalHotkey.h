#pragma once

#include "WheelItem.h"

class WheelItemExternalHotkey : public WheelItem
{
public:
	WheelItemExternalHotkey() = delete;
	WheelItemExternalHotkey(
		std::string a_displayName,
		std::uint32_t a_scanCode,
		std::uint32_t a_modifier,
		std::string a_iconPath,
		std::uint32_t a_iconTintARGB,
		std::uint32_t a_sourceSlotIndex,
		std::string a_sourceTag,
		bool a_hasResolvedIcon);

	void DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	void DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs) override;
	bool IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	bool IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv) override;
	void ActivateItemPrimary() override;
	void ActivateItemSecondary() override;
	void ActivateItemSpecial() override;
	bool MayQueueTransientGameplayAction() const override { return true; }
	void SerializeIntoJsonObj(nlohmann::json& a_json) override;

	bool RequiresRuntimeFormValidation() const override { return false; }
	const char* GetItemTypeName() const override { return ITEM_TYPE_STR; }
	const char* GetItemName() const override { return _displayName.c_str(); }

	std::uint32_t GetScanCode() const { return _scanCode; }
	std::uint32_t GetModifier() const { return _modifier; }
	const std::string& GetIconPath() const { return _iconPath; }
	std::uint32_t GetIconTintARGB() const { return _iconTintARGB; }
	std::uint32_t GetSourceSlotIndex() const { return _sourceSlotIndex; }
	const std::string& GetSourceTag() const { return _sourceTag; }
	bool HasResolvedIcon() const { return _hasResolvedIcon; }

	static inline const char* ITEM_TYPE_STR = "WheelItemExternalHotkey";

private:
	std::string BuildHotkeyLabel() const;
	ImVec2 ComputeDrawSize(const Texture::Image& a_texture, bool a_highlight) const;
	Texture::Image ResolveIcon();

	std::string _displayName;
	std::uint32_t _scanCode = 0;
	std::uint32_t _modifier = 0;
	std::string _iconPath;
	std::uint32_t _iconTintARGB = 0xFFFFFFFF;
	std::uint32_t _sourceSlotIndex = 0;
	std::string _sourceTag;
	bool _hasResolvedIcon = false;
};
