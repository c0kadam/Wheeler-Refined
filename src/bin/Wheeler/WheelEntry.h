#pragma once
#include <cstdint>
#include <limits>
#include <memory>
#include <shared_mutex>
#include "nlohmann/json.hpp"
#include "imgui.h"

#include "bin/Animation/TimeInterpolator/TimeFloatInterpolator.h"
#include "bin/Animation/TimeBounceInterpolator.h"
#include "WheelItems/WheelItem.h"
enum class MissingCategory : std::uint8_t;
struct EquippedHandsCache;
class WheelEntry
{
public:
	WheelEntry();
	~WheelEntry();

	/// <summary>
	/// Feed in necessary information for the entry to update its interpolation animations;
	/// Called before drawing the entry.
	/// </summary>
	/// <param name="imap"></param>
	/// <param name="innerSpacingRad"></param>
	/// <param name="entryInnerAngleMin"></param>
	/// <param name="entryInnerAngleMax"></param>
	/// <param name="entryOuterAngleMin"></param>
	/// <param name="entryOuterAngleMax"></param>
	/// <param name="hovered"></param>
	void UpdateAnimation(
		RE::TESObjectREFR::InventoryItemMap& imap,
		float innerSpacingRad,
		float entryInnerAngleMin, float entryInnerAngleMax,
		float entryOuterAngleMin, float entryOuterAngleMax, bool hovered);
	/// <summary>
	/// Draw the background for this entry, including 2 arcs, one acting as the main background and other, much thinner
	/// one acting as an indicator to whether the current entry is active.
	/// The main background changes color when the entry is being hovered, and the background arc's radius linearly interpolates to an
	/// increased value.
	/// (GTA5-style)
	/// </summary>
	void DrawBackGround(const ImVec2 wheelCenter, const ImVec2 entryCenter, float innerSpacing,
		float entryInnerAngleMin, float entryInnerAngleMax,
		float entryOuterAngleMin, float entryOuterAngleMax,
		bool hovered, int numArcSegments, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawARGS);
	
	/// <summary>
	/// Draw the content in slot and (if applicable) highlight region of this wheel entry.
	/// This function should be called after DrawBackGround to prevent background from being drawn over the content.
	/// </summary> 
	void DrawSlotAndHighlight(ImVec2 a_wheelCenter, ImVec2 a_entryCenter, bool a_slotOnRightSide, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs, const EquippedHandsCache& a_hands);

	/// <summary>
	/// Get the radius changes made by arcRadiusIncInterpolator. Use this function to calculate the offset of item center.
	/// </summary>
	/// <returns></returns>
	const float GetRadiusMod();

	void DrawControlPrompt(ImVec2 a_center, DrawArgs a_drawArgs);

	bool IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv);
	bool IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv);

	/// <summary>
	/// Activate the item with secondary (left) input, which corresponds to right mouse click or left controller trigger.
	/// If we're in edit mode, the entry deletes the currently selected item until there are no items left.
	/// If we're not in edit mode, the entry calls the currently selected item's ActivateItemSecondary().
	/// </summary>
	/// <param name="editMode">Whether the wheel is in edit mode.</param>
	PreparedWheelItemActivation ActivateItemSecondary(bool editMode = false);

	/// <summary>
	/// Activate the item with primary(right) input, which corresponds to left mouse click or right controller trigger.
	/// If we're in edit mode, the entry queries WheelItemFactory for a new item (the item that the cursor is hovering over in either inventory or magic menu)
	/// and pushes it to the entry's front.
	/// If we're not in edit mode, the entry calls the currently selected item's ActivateItemPrimary().
	/// </summary>
	/// <param name="editMode">Whether the wheel is in edit mode.</param>
	PreparedWheelItemActivation ActivateItemPrimary(bool editMode = false);

	/// <summary>
	/// Activate the item with special(middle) input, which corresponds to a middle mouse click or a controller thumbstick press.
	/// </summary>
	/// <param name="editMode"></param>
	PreparedWheelItemActivation ActivateItemSpecial(bool editMode = false);


	void PrevItem();
	void NextItem();
	void PushItem(std::shared_ptr<WheelItem> item);

	std::shared_ptr<WheelItem> GetSelectedItem();
	int GetSelectedItemIndex();
	void SetSelectedItem(int a_selected);

	bool IsEmpty();
	int GetNumItems();
	bool IsMissingInInventory() const;
	MissingCategory GetMissingCategory() const;
	void SetMissingState(bool missing, MissingCategory category);
	bool ReplaceSelectedItem(std::shared_ptr<WheelItem> item);
	bool ReplaceItemAt(int index, std::shared_ptr<WheelItem> item);

	// Removes depleted consumables (alchemy items with 0 count) from this slot.
	// Leaves the entry in place (slot becomes empty).
	void ClearDepletedConsumables();

	// Clears ALL items from this entry, making it empty while preserving the slot.
	// Used by inventory prune to maintain index stability.
	void ClearAllItems();

    void SerializeIntoJsonObj(nlohmann::json& a_json);
	static std::unique_ptr<WheelEntry> SerializeFromJsonObj(const nlohmann::json& a_json, SKSE::SerializationInterface* a_intfc);

	void ResetAnimation();

	// ============================================================================
	// External API Accessors
	// ============================================================================

	/// <summary>
	/// Get item at index. Returns nullptr if out of range.
	/// Caller should hold appropriate lock if accessing from another thread.
	/// </summary>
	WheelItem* GetItem(int a_index);

	/// <summary>
	/// Remove item at index. Returns true if successful, false if out of range.
	/// </summary>
	bool RemoveItemAt(int a_index);

private:
	void drawSlot(ImVec2 a_center, bool a_slotOnRightSide, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs, bool a_isMissing, const EquippedHandsCache& a_hands);
	void drawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs, bool a_isMissing);

	bool _prevHovered = false;  // used to detect when the mouse enters the entry
	float _lastHighlightExpandSize = std::numeric_limits<float>::quiet_NaN();
	float _lastHighlightOuterAngleInc = std::numeric_limits<float>::quiet_NaN();
	float _lastHighlightInnerAngleInc = std::numeric_limits<float>::quiet_NaN();
	int _selectedItem = -1;

	mutable std::shared_mutex _lock;
	std::vector<std::shared_ptr<WheelItem>> _items;
	
	TimeFloatInterpolator _arcRadiusIncInterpolator;  // for animating the arc radius's increase when the entry is hovered
	TimeFloatInterpolator _arcInnerAngleIncInterpolator;   // for animating the arc's angle increase when the entry is hovered
	TimeFloatInterpolator _arcOuterAngleIncInterpolator;  // for animating the arc's angle increase when the entry is hovered
	TimeBounceInterpolator _arcRadiusBounceInterpolator = TimeBounceInterpolator(0);      // for animating the arc radius's bouncing when the entry is clicked
	bool _missingInInventory = false;
	MissingCategory _missingCategory = MissingCategory{};
};
