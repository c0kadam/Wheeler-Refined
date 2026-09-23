#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

namespace ActionPolicy
{
	// ============================================================================
	// Item Categories - used to determine which action policy to apply
	// ============================================================================
	enum class ItemCategory : std::uint8_t
	{
		Unknown = 0,
		Weapon,
		Armor,
		Shield,        // Subset of Armor, but different equip rules
		Ammo,
		PotionOrFood,  // AlchemyItem that is consumed
		Poison,        // AlchemyItem that is applied to weapon
		Scroll,        // RE::ScrollItem - equips to hand
		Book,          // RE::TESObjectBOOK - opens for reading
		Spell,         // Regular spell (not power)
		Power,         // Lesser/Greater power
		Shout,
		Light,         // Torch
		Misc,          // TESObjectMISC
		Deployable,    // Special alchemy items (Campfire supplies, etc.)
	};

	const char* CategoryToString(ItemCategory category);

	// ============================================================================
	// Actions - atomic operations the system can perform
	// ============================================================================
	enum class Action : std::uint8_t
	{
		NoOp = 0,
		EquipRight,
		EquipLeft,
		UnequipRight,
		UnequipLeft,
		EquipVoice,      // For powers/shouts
		UnequipVoice,
		Use,             // Consume (potion/food)
		ApplyPoison,
		Read,            // Open book
		Activate,        // Generic activation (misc items, deployables)
		CastImmediate,   // Instant cast spell/power
	};

	const char* ActionToString(Action action);

	// ============================================================================
	// Hand preference - derived from user input
	// ============================================================================
	enum class HandPref : std::uint8_t
	{
		Right = 0,  // Primary activation (LMB)
		Left,       // Secondary activation (RMB)
		Either,     // System chooses
	};

	// ============================================================================
	// Activation Intent - what input triggered activation
	// ============================================================================
	enum class ActivateIntent : std::uint8_t
	{
		Primary,    // LMB / RT
		Secondary,  // RMB / LT
		Special,    // MMB / Stick press
		RTU,        // Release-to-Use on wheel close
		HoldRelease // Hold-to-Use release
	};

	HandPref IntentToHandPref(ActivateIntent intent);

	// ============================================================================
	// Action Result - outcome of attempting an action
	// ============================================================================
	enum class ActionResult : std::uint8_t
	{
		Success = 0,
		Failed,
		SlotOccupied,
		NotInInventory,
		InsufficientResources,  // Magicka, etc.
		InvalidTarget,
		Blocked,                // Menu blocked, etc.
	};

	const char* ResultToString(ActionResult result);

	// ============================================================================
	// Execution Context - passed through the action chain
	// ============================================================================
	struct ExecutionContext
	{
		RE::TESForm* form = nullptr;
		RE::FormID formID = 0;
		ItemCategory category = ItemCategory::Unknown;
		HandPref handPref = HandPref::Right;
		ActivateIntent intent = ActivateIntent::Primary;
		
		// State tracking
		Action lastAttemptedAction = Action::NoOp;
		ActionResult lastResult = ActionResult::Success;
		int actionsAttempted = 0;
		int actionsSucceeded = 0;
		
		// Debug info
		std::string formName;
		RE::FormType formType = RE::FormType::None;
	};

	// ============================================================================
	// Classification - TESForm -> ItemCategory
	// ============================================================================
	
	/// Classify a form into an ItemCategory for policy selection.
	/// This is the authoritative classification function.
	ItemCategory Classify(RE::TESForm* form);
	
	/// Check if a TESObjectBOOK is actually a scroll (spell tome that teaches spells
	/// should not be confused with scrolls - scrolls are RE::ScrollItem, a separate type)
	/// Note: In Skyrim, scrolls have FormType::Scroll and use RE::ScrollItem class,
	/// they are NOT stored as TESObjectBOOK. This function is for edge cases only.
	bool IsBookActuallyScroll(RE::TESObjectBOOK* book);

	// ============================================================================
	// Policy - ordered list of actions to attempt
	// ============================================================================
	struct ActionPolicy
	{
		std::vector<Action> chain;
		bool requiresInventory = false;
		bool toggleBehavior = false;  // If true, unequip if already equipped
	};

	/// Get the action policy for a given category, hand preference, and intent
	ActionPolicy GetPolicy(ItemCategory category, HandPref hand, ActivateIntent intent);

	// ============================================================================
	// Post-condition Verification
	// ============================================================================
	
	/// Verify that an action succeeded by checking game state
	ActionResult VerifyPostCondition(const ExecutionContext& ctx, Action action);
	
	/// Check if item is equipped in the specified hand
	bool IsEquippedInHand(RE::TESForm* form, bool rightHand);
	
	/// Check if item is equipped in voice/power slot
	bool IsEquippedAsVoice(RE::TESForm* form);
	
	/// Get item count in player inventory
	int GetInventoryCount(RE::TESForm* form);

	// ============================================================================
	// Action Execution
	// ============================================================================
	
	/// Execute a single action and return result
	ActionResult RunAction(ExecutionContext& ctx, Action action);
	
	/// Execute the full policy chain with fallbacks
	/// Returns true if any action in the chain succeeded
	bool ExecutePolicy(ExecutionContext& ctx);

	// ============================================================================
	// Main Entry Point
	// ============================================================================
	
	/// Execute activation for a form with the given hand preference and intent.
	/// This is the single entry point that should be called for all activations.
	/// Returns true if activation succeeded.
	bool ExecuteEntry(RE::TESForm* form, HandPref hand, ActivateIntent intent);
	
	/// Overload that derives hand preference from intent
	bool ExecuteEntry(RE::TESForm* form, ActivateIntent intent);

	// ============================================================================
	// Override System (Optional Escape Hatch)
	// ============================================================================
	
	/// Register a category override for a specific FormID
	void RegisterCategoryOverride(RE::FormID formID, ItemCategory category);
	
	/// Register a policy override for a specific FormID
	void RegisterPolicyOverride(RE::FormID formID, const ActionPolicy& policy);
	
	/// Clear all overrides
	void ClearOverrides();

	// ============================================================================
	// Debug Logging
	// ============================================================================
	
	/// Enable/disable verbose activation logging
	void SetDebugLogging(bool enabled);
	bool IsDebugLoggingEnabled();
	
	/// Log activation attempt
	void LogActivationStart(const ExecutionContext& ctx);
	
	/// Log action attempt
	void LogActionAttempt(const ExecutionContext& ctx, Action action);
	
	/// Log action result
	void LogActionResult(const ExecutionContext& ctx, Action action, ActionResult result, const char* reason = nullptr);
	
	/// Log final outcome
	void LogActivationEnd(const ExecutionContext& ctx, bool success, Action finalAction);

}  // namespace ActionPolicy
