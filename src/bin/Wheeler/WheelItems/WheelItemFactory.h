#pragma once

#include "bin/Integrations/OStimTypes.h"

#include <cstdint>
#include <string>
#include <string_view>

class WheelItem;

namespace RE
{
	class TESBoundObject;
}

class WheelItemFactory
{
public:
	/// <summary>
	/// Creates a new wheel item that's either a:
	/// WheelItemSpell, WheelItemWeapon, WheelItemArmor, WheelItemAmmo, WheelItemPower, WheelItemShout,
	/// based on the item that's currently being hovered on in the inventory.
	/// Returns nullptr if no item is being currently hovered, or the item do not match any of the above types.
	/// </summary>
	/// <returns>Created item, or null if no item is applicable.</returns>
	static std::shared_ptr<WheelItem> MakeWheelItemFromMenuHovered();

	/// <summary>
	/// Notify the main wheel inventory snapshot that an item was added or removed.
	/// </summary>
	/// <param name="a_object">Item being added/removed.</param>
	/// <param name="a_countDelta">Positive for add, negative for remove.</param>
	static void NotifyInventoryChanged(RE::TESBoundObject* a_object, std::int32_t a_countDelta);

	/// <summary>
	/// Creates a new wheel item that's either a:
	/// WheelItemSpell, WheelItemWeapon, WheelItemArmor, WheelItemAmmo, WheelItemPower, WheelItemShout,
	/// based on a .json object.
	/// Returns nullptr if no item is being currently hovered, or the item do not match any of the above types.
	/// </summary>
	/// <returns>Created item, or null if no item is applicable.</returns>
	static std::shared_ptr<WheelItem> MakeWheelItemFromJsonObject(nlohmann::json a_json, SKSE::SerializationInterface* a_intfc);

	/// <summary>
	/// Creates a wheel item from a resolved form ID and saved type string.
	/// Returns nullptr if the type is unsupported or the form cannot be resolved.
	/// </summary>
	static std::shared_ptr<WheelItem> MakeWheelItemFromResolvedForm(std::string_view a_type, RE::FormID a_formID, std::uint16_t a_uniqueID);
	static std::shared_ptr<WheelItem> MakeExternalHotkeyItem(
		std::string a_displayName,
		std::uint32_t a_scanCode,
		std::uint32_t a_modifier,
		std::string a_iconPath,
		std::uint32_t a_iconTintARGB,
		std::uint32_t a_sourceSlotIndex,
		std::string a_sourceTag,
		bool a_hasResolvedIcon);
	static std::shared_ptr<WheelItem> MakeOStimActionItem(OStimActionPayload a_payload);

	// ============================================================================
	// External API
	// ============================================================================

	/// <summary>
	/// Creates a wheel item from a FormID, auto-detecting the form type.
	/// For weapons/armor, uniqueID is required to identify the specific inventory item.
	/// Returns nullptr if form not found or form type is unsupported.
	/// </summary>
	/// <param name="a_formID">The FormID of the game form</param>
	/// <param name="a_uniqueID">UniqueID for weapons/armor (ignored for other types)</param>
	/// <returns>Created item, or null if not applicable.</returns>
	static std::shared_ptr<WheelItem> MakeWheelItemFromFormID(RE::FormID a_formID, std::uint16_t a_uniqueID = 0);
};
