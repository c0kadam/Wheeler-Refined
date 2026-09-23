#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include "bin/Rendering/TextureManager.h"
#include "bin/Config.h"
#include "nlohmann/json.hpp"
class ImVec2;
class WheelItem;

enum class WheelItemActivationKind : std::uint8_t
{
	Primary,
	Secondary,
	Special
};

enum class WheelItemActivationResult : std::uint8_t
{
	Succeeded,
	AlreadyPoisoned,
	UnsafeResolution,
	InvalidTarget,
	Rejected
};

[[nodiscard]] constexpr bool IsSuccessfulActivation(WheelItemActivationResult a_result) noexcept
{
	return a_result == WheelItemActivationResult::Succeeded;
}

const char* WheelItemActivationResultName(WheelItemActivationResult a_result) noexcept;

// Synchronous-local handoff from the locked wheel hierarchy to Wheeler.  The
// indices are telemetry/API metadata only; selectedItem is the sole gameplay
// target and is never stored in a deferred queue.
struct PreparedWheelItemActivation
{
	std::shared_ptr<WheelItem> selectedItem;
	WheelItemActivationKind kind = WheelItemActivationKind::Primary;
	std::uint64_t transientEpoch = 0;
	RE::FormID formID = 0;
	std::int32_t wheelIndex = -1;
	std::int32_t entryIndex = -1;
	std::int32_t itemIndex = -1;
	bool executeAfterContainerUnlock = false;
	bool isPrimaryForAPI = true;
	bool accepted = false;
};

enum class MissingCategory : std::uint8_t
{
	Unknown = 0,
	Consumable = 1,
	Gear = 2,
	ThrowableMod = 3
};

MissingCategory MissingCategoryFromInt(int value);
int MissingCategoryToInt(MissingCategory value);
const char* MissingCategoryToString(MissingCategory category);
MissingCategory DetermineMissingCategory(RE::TESForm* form);
MissingCategory InferMissingCategoryFromItemType(std::string_view type);
bool IsKeepMissingCategoryEnabled(MissingCategory category);

struct EquippedHandsCache
{
	RE::FormID rightFormID = 0;
	RE::FormID leftFormID = 0;
	std::uint64_t rightSignature = 0;
	std::uint64_t leftSignature = 0;
};

class WheelItem
{
public:
	WheelItem(){};
	
	/// <summary>
	/// Draw everything that's supposed to be in a wheel slot(entry)
	/// Currently, DrawSlot should draw an image of the item and its name.
	/// </summary>
	virtual void DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs);
	
	/// <summary>
	/// Draw everything of the item that's supposed to be in the highlight region i.e. center of the wheel.
	/// Currently, DrawHighlight should draw an enlarged image of the item, item description, and item stats(if applicable).
	/// </summary>
	virtual void DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs);
	virtual bool IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv);

	/// <summary>
	/// Whether the item is available. 
	/// An item is unavailable when the player lacks skill to use it, or the item is not in the player's inventory.
	/// </summary>
	virtual bool IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv);

	virtual void ActivateItemPrimary();
	virtual void ActivateItemSecondary();
	virtual void ActivateItemSpecial();
	// Result-aware synchronous dispatch used by the prepared activation boundary.
	// Existing item implementations retain their void entry points; the default
	// adapter reports success after invoking the selected entry point.
	virtual WheelItemActivationResult ActivateItemWithResult(WheelItemActivationKind a_kind);
	// Activation implementations that synchronously enqueue Wheeler-owned
	// transient gameplay work must run after WheelEntry releases its lock.
	virtual bool MayQueueTransientGameplayAction() const { return false; }

	// Cooldown overlay support (visual only).
	virtual bool HasCooldown() const { return false; }
	virtual float GetCooldownRemainingSeconds() const { return 0.0f; }
	virtual float GetCooldownTotalSeconds() const { return 0.0f; }
	virtual float GetCooldownPercent() const;
	virtual void DrawCooldownOverlay(ImVec2 a_center, DrawArgs a_drawArgs);

	virtual void SerializeIntoJsonObj(nlohmann::json& a_json);
	static std::shared_ptr<WheelItem> SerializeFromJsonObj(nlohmann::json& a_json);

	// Inventory sync support
	// Returns true if this item is backed by inventory (books, potions, weapons, armor)
	// Returns false for spells, shouts, powers that are not inventory items
	virtual bool IsInventoryBacked() const { return false; }
	
	// Returns true if the item is still present in player inventory
	// Only meaningful if IsInventoryBacked() returns true
	// Default returns true so non-inventory items are never pruned
	virtual bool IsInPlayerInventory() const { return true; }
	
	// Returns the FormID of the item for logging, 0 if not applicable
	virtual RE::FormID GetFormID() const { return 0; }
	
	// Returns the item name for logging
	virtual const char* GetItemName() const { return "Unknown"; }
	
	// Returns the item type string for logging
	virtual const char* GetItemTypeName() const { return ITEM_TYPE_STR; }
	virtual RE::TESForm* GetI4Form() const
	{
		const auto formID = GetFormID();
		return formID ? RE::TESForm::LookupByID<RE::TESForm>(formID) : nullptr;
	}
	virtual bool RequiresRuntimeFormValidation() const { return true; }
	virtual RE::TESBoundObject* GetI4BoundObject() const
	{
		auto* form = GetI4Form();
		return form ? form->As<RE::TESBoundObject>() : nullptr;
	}
	virtual std::uint64_t GetI4Signature() const { return 0; }
	virtual bool SupportsPreciseHandIndicatorMatching(RE::TESObjectREFR::InventoryItemMap& a_inv) const { return false; }
	virtual std::optional<bool> MatchesEquippedHandIndicator(
		RE::TESObjectREFR::InventoryItemMap& a_inv,
		RE::FormID a_handFormID,
		std::uint64_t a_handSignature,
		bool a_leftHand) const
	{
		return std::nullopt;
	}
	MissingCategory GetMissingCategory() const { return _missingCategory; }
	void SetMissingCategory(MissingCategory category) { _missingCategory = category; }

	static inline const char* ITEM_TYPE_STR = "WheelItem";


protected:
	Texture::Image _texture = Texture::Image();
	Texture::Image _stat_texture = Texture::Image();
	std::string _description = "";  // buffer for description.
	MissingCategory _missingCategory = MissingCategory::Unknown;
	mutable float _cooldownCacheTime = -1.0f;
	mutable float _cooldownCacheRemaining = 0.0f;
	mutable float _cooldownCacheTotal = 0.0f;

	void DrawCooldownOverlayInternal(ImVec2 a_center, DrawArgs a_drawArgs, bool a_fillBottomToTop);
	
	/// <summary>
	/// Draws stat icon and value of the item when the item is highlighted.
	/// Coordinates and scale of the icon texture and value text are determined by Config.
	/// </summary>
	void drawItemHighlightStatIconAndValue(ImVec2 a_center, Texture::Image& a_stat_icon, float a_stat_value, DrawArgs a_drawArgs);

	float calculateHighlightTextShiftY(const char* a_description) const;
	void drawHighlightDescription(ImVec2 a_center, const char* a_text, DrawArgs a_drawArgs, float a_shiftY = 0.0f);
	void drawHighlightTexture(ImVec2 a_center, DrawArgs a_drawArgs);
	void drawHighlightText(ImVec2 a_center, const char* a_text, DrawArgs a_drawArgs, float a_shiftY = 0.0f);
	void drawSlotTexture(ImVec2 a_center, DrawArgs a_drawArgs);
	void drawSlotText(ImVec2 a_center, const char* a_text, DrawArgs a_drawArgs);
};
