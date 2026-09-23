#include "ActionPolicy.h"
#include "bin/Utilities/InventorySnapshotCache.h"
#include "bin/Utilities/Utils.h"
#include "bin/Config.h"
#include <mutex>

namespace ActionPolicy
{
	// ============================================================================
	// Static State
	// ============================================================================
	
	static bool s_debugLogging = false;
	static std::mutex s_overrideMutex;
	static std::unordered_map<RE::FormID, ItemCategory> s_categoryOverrides;
	static std::unordered_map<RE::FormID, ActionPolicy> s_policyOverrides;

	// ============================================================================
	// String Conversion Helpers
	// ============================================================================

	const char* CategoryToString(ItemCategory category)
	{
		switch (category) {
		case ItemCategory::Unknown:      return "Unknown";
		case ItemCategory::Weapon:       return "Weapon";
		case ItemCategory::Armor:        return "Armor";
		case ItemCategory::Shield:       return "Shield";
		case ItemCategory::Ammo:         return "Ammo";
		case ItemCategory::PotionOrFood: return "PotionOrFood";
		case ItemCategory::Poison:       return "Poison";
		case ItemCategory::Scroll:       return "Scroll";
		case ItemCategory::Book:         return "Book";
		case ItemCategory::Spell:        return "Spell";
		case ItemCategory::Power:        return "Power";
		case ItemCategory::Shout:        return "Shout";
		case ItemCategory::Light:        return "Light";
		case ItemCategory::Misc:         return "Misc";
		case ItemCategory::Deployable:   return "Deployable";
		default:                         return "Invalid";
		}
	}

	const char* ActionToString(Action action)
	{
		switch (action) {
		case Action::NoOp:          return "NoOp";
		case Action::EquipRight:    return "EquipRight";
		case Action::EquipLeft:     return "EquipLeft";
		case Action::UnequipRight:  return "UnequipRight";
		case Action::UnequipLeft:   return "UnequipLeft";
		case Action::EquipVoice:    return "EquipVoice";
		case Action::UnequipVoice:  return "UnequipVoice";
		case Action::Use:           return "Use";
		case Action::ApplyPoison:   return "ApplyPoison";
		case Action::Read:          return "Read";
		case Action::Activate:      return "Activate";
		case Action::CastImmediate: return "CastImmediate";
		default:                    return "Invalid";
		}
	}

	const char* ResultToString(ActionResult result)
	{
		switch (result) {
		case ActionResult::Success:               return "Success";
		case ActionResult::Failed:                return "Failed";
		case ActionResult::SlotOccupied:          return "SlotOccupied";
		case ActionResult::NotInInventory:        return "NotInInventory";
		case ActionResult::InsufficientResources: return "InsufficientResources";
		case ActionResult::InvalidTarget:         return "InvalidTarget";
		case ActionResult::Blocked:               return "Blocked";
		default:                                  return "Unknown";
		}
	}

	HandPref IntentToHandPref(ActivateIntent intent)
	{
		switch (intent) {
		case ActivateIntent::Primary:
		case ActivateIntent::Special:
		case ActivateIntent::RTU:
		case ActivateIntent::HoldRelease:
			return HandPref::Right;
		case ActivateIntent::Secondary:
			return HandPref::Left;
		default:
			return HandPref::Right;
		}
	}

	// ============================================================================
	// Classification Implementation
	// ============================================================================

	bool IsBookActuallyScroll(RE::TESObjectBOOK* book)
	{
		if (!book) {
			return false;
		}
		
		// In Skyrim, actual scrolls use RE::ScrollItem (FormType::Scroll), not TESObjectBOOK.
		// TESObjectBOOK has a flag for spell tomes but those teach spells, they don't cast.
		// This function exists for paranoid edge-case checking only.
		
		// Check the book flags - books can be spell tomes but those are "read to learn"
		// Scrolls in Skyrim are a completely separate form type (RE::ScrollItem)
		
		// The OG_FLAGS from TESObjectBOOK don't have a "scroll" flag because
		// scrolls are not books in Skyrim's form system.
		return false;
	}

	ItemCategory Classify(RE::TESForm* form)
	{
		if (!form) {
			return ItemCategory::Unknown;
		}
		
		const RE::FormID formID = form->GetFormID();
		
		// Check for category override first
		{
			std::lock_guard<std::mutex> lock(s_overrideMutex);
			auto it = s_categoryOverrides.find(formID);
			if (it != s_categoryOverrides.end()) {
				if (s_debugLogging) {
					logger::info("[ActionPolicy] classify form={:08X} -> {} (OVERRIDE)",
						formID, CategoryToString(it->second));
				}
				return it->second;
			}
		}
		
		const RE::FormType formType = form->GetFormType();
		ItemCategory category = ItemCategory::Unknown;
		
		switch (formType) {
		case RE::FormType::Weapon:
			category = ItemCategory::Weapon;
			break;
			
		case RE::FormType::Armor:
		{
			RE::TESObjectARMO* armor = form->As<RE::TESObjectARMO>();
			if (armor) {
				// Check if it's a shield
				auto slot = armor->GetSlotMask();
				if (slot == RE::BGSBipedObjectForm::BipedObjectSlot::kShield) {
					category = ItemCategory::Shield;
				} else {
					category = ItemCategory::Armor;
				}
			} else {
				category = ItemCategory::Armor;
			}
		}
		break;
		
		case RE::FormType::Ammo:
			category = ItemCategory::Ammo;
			break;
			
		case RE::FormType::AlchemyItem:
		{
			RE::AlchemyItem* alchemy = form->As<RE::AlchemyItem>();
			if (alchemy) {
				if (alchemy->IsPoison()) {
					category = ItemCategory::Poison;
				} else if (alchemy->IsFood() || alchemy->IsMedicine()) {
					category = ItemCategory::PotionOrFood;
				} else {
					// Unknown alchemy - treat as deployable (Campfire, etc.)
					category = ItemCategory::Deployable;
				}
			} else {
				category = ItemCategory::PotionOrFood;
			}
		}
		break;

		case RE::FormType::Ingredient:
			category = ItemCategory::PotionOrFood;
			break;
		
		case RE::FormType::Scroll:
			// This is RE::ScrollItem - actual magic scrolls that equip to hands
			category = ItemCategory::Scroll;
			break;
			
		case RE::FormType::Book:
		{
			RE::TESObjectBOOK* book = form->As<RE::TESObjectBOOK>();
			// Double-check it's not secretly a scroll (paranoid check)
			if (book && IsBookActuallyScroll(book)) {
				category = ItemCategory::Scroll;
			} else {
				category = ItemCategory::Book;
			}
		}
		break;
		
		case RE::FormType::Spell:
		{
			RE::SpellItem* spell = form->As<RE::SpellItem>();
			if (spell) {
				auto spellType = spell->GetSpellType();
				if (spellType == RE::MagicSystem::SpellType::kPower ||
					spellType == RE::MagicSystem::SpellType::kLesserPower ||
					spellType == RE::MagicSystem::SpellType::kVoicePower) {
					category = ItemCategory::Power;
				} else {
					category = ItemCategory::Spell;
				}
			} else {
				category = ItemCategory::Spell;
			}
		}
		break;
		
		case RE::FormType::Shout:
			category = ItemCategory::Shout;
			break;
			
		case RE::FormType::Light:
		{
			RE::TESObjectLIGH* light = form->As<RE::TESObjectLIGH>();
			if (light && light->CanBeCarried()) {
				category = ItemCategory::Light;
			} else {
				category = ItemCategory::Unknown;
			}
		}
		break;
		
		case RE::FormType::Misc:
			category = ItemCategory::Misc;
			break;
			
		default:
			category = ItemCategory::Unknown;
			break;
		}
		
		if (s_debugLogging) {
			const char* name = form->GetName();
			logger::info("[ActionPolicy] classify form={:08X} type={} name='{}' -> {}",
				formID, static_cast<int>(formType), name ? name : "", CategoryToString(category));
		}
		
		return category;
	}

	// ============================================================================
	// Policy Table
	// ============================================================================

	ActionPolicy GetPolicy(ItemCategory category, HandPref hand, ActivateIntent intent)
	{
		ActionPolicy policy;
		
		switch (category) {
		case ItemCategory::Weapon:
			policy.requiresInventory = true;
			policy.toggleBehavior = true;
			if (hand == HandPref::Left) {
				// Left hand: try equip left, if fails try unequip+equip
				policy.chain = { Action::EquipLeft };
			} else {
				// Right hand (default)
				policy.chain = { Action::EquipRight };
			}
			break;
			
		case ItemCategory::Shield:
			policy.requiresInventory = true;
			policy.toggleBehavior = true;
			// Shields always go to left hand
			policy.chain = { Action::EquipLeft };
			break;
			
		case ItemCategory::Armor:
			policy.requiresInventory = true;
			policy.toggleBehavior = true;
			// Armor uses generic equip (engine handles slot)
			policy.chain = { Action::EquipRight };  // EquipRight acts as generic equip for armor
			break;
			
		case ItemCategory::Ammo:
			policy.requiresInventory = true;
			policy.toggleBehavior = false;
			policy.chain = { Action::EquipRight };  // Ammo uses generic equip
			break;
			
		case ItemCategory::PotionOrFood:
			policy.requiresInventory = true;
			policy.toggleBehavior = false;
			policy.chain = { Action::Use };
			break;
			
		case ItemCategory::Poison:
			policy.requiresInventory = true;
			policy.toggleBehavior = false;
			policy.chain = { Action::ApplyPoison };
			break;
			
		case ItemCategory::Scroll:
			policy.requiresInventory = true;
			policy.toggleBehavior = true;
			// Scrolls: primary action is equip to preferred hand
			// Fallback: unequip that hand first, then equip
			if (hand == HandPref::Left) {
				policy.chain = { 
					Action::EquipLeft,
					Action::UnequipLeft,  // Fallback: clear slot first
					Action::EquipLeft     // Then try again
				};
			} else {
				policy.chain = { 
					Action::EquipRight,
					Action::UnequipRight,  // Fallback: clear slot first
					Action::EquipRight     // Then try again
				};
			}
			break;
			
		case ItemCategory::Book:
			policy.requiresInventory = true;
			policy.toggleBehavior = false;
			policy.chain = { Action::Read };
			break;
			
		case ItemCategory::Spell:
			policy.requiresInventory = false;  // Spells don't require inventory
			policy.toggleBehavior = true;
			if (hand == HandPref::Left) {
				policy.chain = { Action::EquipLeft };
			} else {
				policy.chain = { Action::EquipRight };
			}
			break;
			
		case ItemCategory::Power:
			policy.requiresInventory = false;
			policy.toggleBehavior = true;
			policy.chain = { Action::EquipVoice };
			break;
			
		case ItemCategory::Shout:
			policy.requiresInventory = false;
			policy.toggleBehavior = true;
			policy.chain = { Action::EquipVoice };
			break;
			
		case ItemCategory::Light:
			policy.requiresInventory = true;
			policy.toggleBehavior = true;
			// Torches go to left hand
			policy.chain = { Action::EquipLeft };
			break;
			
		case ItemCategory::Misc:
			policy.requiresInventory = true;
			policy.toggleBehavior = false;
			policy.chain = { Action::Activate };
			break;
			
		case ItemCategory::Deployable:
			policy.requiresInventory = true;
			policy.toggleBehavior = false;
			policy.chain = { Action::Use };
			break;
			
		case ItemCategory::Unknown:
		default:
			policy.requiresInventory = false;
			policy.toggleBehavior = false;
			policy.chain = { Action::NoOp };
			break;
		}
		
		return policy;
	}

	// ============================================================================
	// Post-condition Verification
	// ============================================================================

	bool IsEquippedInHand(RE::TESForm* form, bool rightHand)
	{
		if (!form) {
			return false;
		}
		
		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return false;
		}
		
		RE::TESForm* equipped = pc->GetEquippedObject(!rightHand);  // GetEquippedObject(true) = left
		if (!equipped) {
			return false;
		}
		
		return equipped->GetFormID() == form->GetFormID();
	}

	bool IsEquippedAsVoice(RE::TESForm* form)
	{
		if (!form) {
			return false;
		}
		
		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return false;
		}
		
		// Check selected power
		RE::TESForm* selectedPower = pc->GetActorRuntimeData().selectedPower;
		if (selectedPower && selectedPower->GetFormID() == form->GetFormID()) {
			return true;
		}
		
		// Check selected shout (for TESShout)
		if (form->GetFormType() == RE::FormType::Shout) {
			RE::TESShout* equippedShout = pc->GetActorRuntimeData().selectedPower ?
				pc->GetActorRuntimeData().selectedPower->As<RE::TESShout>() : nullptr;
			if (equippedShout && equippedShout->GetFormID() == form->GetFormID()) {
				return true;
			}
		}
		
		return false;
	}

	int GetInventoryCount(RE::TESForm* form)
	{
		if (!form) {
			return 0;
		}
		
		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return 0;
		}
		
		auto counts = pc->GetInventoryCounts();
		auto it = counts.find(form->As<RE::TESBoundObject>());
		return it != counts.end() ? it->second : 0;
	}

	ActionResult VerifyPostCondition(const ExecutionContext& ctx, Action action)
	{
		switch (action) {
		case Action::EquipRight:
			if (IsEquippedInHand(ctx.form, true)) {
				return ActionResult::Success;
			}
			// For armor, check if it's worn (not hand-based)
			if (ctx.category == ItemCategory::Armor || ctx.category == ItemCategory::Ammo) {
				// Armor/ammo equip verification is more complex, assume success if no error
				return ActionResult::Success;
			}
			return ActionResult::Failed;
			
		case Action::EquipLeft:
			if (IsEquippedInHand(ctx.form, false)) {
				return ActionResult::Success;
			}
			return ActionResult::Failed;
			
		case Action::UnequipRight:
			if (!IsEquippedInHand(ctx.form, true)) {
				return ActionResult::Success;
			}
			return ActionResult::Failed;
			
		case Action::UnequipLeft:
			if (!IsEquippedInHand(ctx.form, false)) {
				return ActionResult::Success;
			}
			return ActionResult::Failed;
			
		case Action::EquipVoice:
			if (IsEquippedAsVoice(ctx.form)) {
				return ActionResult::Success;
			}
			return ActionResult::Failed;
			
		case Action::UnequipVoice:
			if (!IsEquippedAsVoice(ctx.form)) {
				return ActionResult::Success;
			}
			return ActionResult::Failed;
			
		case Action::Use:
		case Action::ApplyPoison:
			// For consumables, we can't easily verify without tracking count before/after
			// Accept as best-effort success
			return ActionResult::Success;
			
		case Action::Read:
			// Book reading opens a menu - hard to verify, accept as success
			return ActionResult::Success;
			
		case Action::Activate:
			// Generic activation - accept as success
			return ActionResult::Success;
			
		case Action::CastImmediate:
			// Immediate cast - accept as success
			return ActionResult::Success;
			
		case Action::NoOp:
			return ActionResult::Success;
			
		default:
			return ActionResult::Failed;
		}
	}

	// ============================================================================
	// Action Execution
	// ============================================================================

	ActionResult RunAction(ExecutionContext& ctx, Action action)
	{
		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return ActionResult::InvalidTarget;
		}
		
		RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
		if (!aeMan && action != Action::NoOp && action != Action::Read) {
			return ActionResult::Failed;
		}
		
		ctx.lastAttemptedAction = action;
		ctx.actionsAttempted++;
		
		LogActionAttempt(ctx, action);
		
		ActionResult result = ActionResult::Failed;
		const char* reason = nullptr;
		
		try {
			switch (action) {
			case Action::NoOp:
				result = ActionResult::Success;
				break;
				
			case Action::EquipRight:
			{
				// Check toggle behavior
				if (IsEquippedInHand(ctx.form, true)) {
					// Already equipped - unequip instead
					InventorySnapshotCache::UnequipObject(aeMan, pc, ctx.form->As<RE::TESBoundObject>(), nullptr, 1, 
						Utils::Slot::GetRightHandSlot());
					result = ActionResult::Success;
					reason = "toggled_off";
				} else {
					// Handle by category
					switch (ctx.category) {
					case ItemCategory::Spell:
						InventorySnapshotCache::EquipSpell(aeMan, pc, ctx.form->As<RE::SpellItem>(), Utils::Slot::GetRightHandSlot());
						break;
					case ItemCategory::Scroll:
						InventorySnapshotCache::EquipObject(aeMan, pc, ctx.form->As<RE::TESBoundObject>(), nullptr, 1, 
							Utils::Slot::GetRightHandSlot());
						break;
					default:
						InventorySnapshotCache::EquipObject(aeMan, pc, ctx.form->As<RE::TESBoundObject>());
						break;
					}
					result = VerifyPostCondition(ctx, action);
				}
			}
			break;
			
			case Action::EquipLeft:
			{
				if (IsEquippedInHand(ctx.form, false)) {
					// Already equipped - unequip instead
					InventorySnapshotCache::UnequipObject(aeMan, pc, ctx.form->As<RE::TESBoundObject>(), nullptr, 1,
						Utils::Slot::GetLeftHandSlot());
					result = ActionResult::Success;
					reason = "toggled_off";
				} else {
					switch (ctx.category) {
					case ItemCategory::Spell:
						InventorySnapshotCache::EquipSpell(aeMan, pc, ctx.form->As<RE::SpellItem>(), Utils::Slot::GetLeftHandSlot());
						break;
					case ItemCategory::Scroll:
					case ItemCategory::Light:
					case ItemCategory::Shield:
						InventorySnapshotCache::EquipObject(aeMan, pc, ctx.form->As<RE::TESBoundObject>(), nullptr, 1,
							Utils::Slot::GetLeftHandSlot());
						break;
					default:
						InventorySnapshotCache::EquipObject(aeMan, pc, ctx.form->As<RE::TESBoundObject>(), nullptr, 1,
							Utils::Slot::GetLeftHandSlot());
						break;
					}
					result = VerifyPostCondition(ctx, action);
				}
			}
			break;
			
			case Action::UnequipRight:
			{
				Utils::Slot::CleanSlot(pc, Utils::Slot::GetRightHandSlot());
				result = ActionResult::Success;
			}
			break;
			
			case Action::UnequipLeft:
			{
				Utils::Slot::CleanSlot(pc, Utils::Slot::GetLeftHandSlot());
				result = ActionResult::Success;
			}
			break;
			
			case Action::EquipVoice:
			{
				if (IsEquippedAsVoice(ctx.form)) {
					// Already equipped - unequip
					pc->GetActorRuntimeData().selectedPower = nullptr;
					result = ActionResult::Success;
					reason = "toggled_off";
				} else {
					if (ctx.category == ItemCategory::Shout) {
						RE::TESShout* shout = ctx.form->As<RE::TESShout>();
						if (shout) {
							InventorySnapshotCache::EquipShout(aeMan, pc, shout);
							result = VerifyPostCondition(ctx, action);
						}
					} else {
						RE::SpellItem* spell = ctx.form->As<RE::SpellItem>();
						if (spell) {
							InventorySnapshotCache::EquipSpell(aeMan, pc, spell, Utils::Slot::GetVoiceSlot());
							result = VerifyPostCondition(ctx, action);
						}
					}
				}
			}
			break;
			
			case Action::UnequipVoice:
			{
				pc->GetActorRuntimeData().selectedPower = nullptr;
				result = ActionResult::Success;
			}
			break;
			
			case Action::Use:
			{
				// Consume potion/food
				int countBefore = GetInventoryCount(ctx.form);
				if (countBefore <= 0) {
					result = ActionResult::NotInInventory;
					reason = "count_zero";
				} else {
					InventorySnapshotCache::EquipObject(aeMan, pc, ctx.form->As<RE::TESBoundObject>());
					result = ActionResult::Success;
				}
			}
			break;
			
			case Action::ApplyPoison:
			{
				// Poison application is handled separately because it needs weapon selection.
				// Verify poison availability before queuing application.
				int countBefore = GetInventoryCount(ctx.form);
				if (countBefore <= 0) {
					result = ActionResult::NotInInventory;
					reason = "count_zero";
				} else {
					// Queue poison apply - this opens a menu
					// The actual apply is handled by Wheeler::QueuePoisonApply
					result = ActionResult::Success;
					reason = "queued";
				}
			}
			break;
			
			case Action::Read:
			{
				// Book reading is handled by Wheeler::QueueBookRead
				// We just signal success here - the actual read happens after wheel closes
				result = ActionResult::Success;
				reason = "queued";
			}
			break;
			
			case Action::Activate:
			{
				// Generic activation - use EquipObject which triggers Papyrus scripts
				InventorySnapshotCache::EquipObject(aeMan, pc, ctx.form->As<RE::TESBoundObject>());
				result = ActionResult::Success;
			}
			break;
			
			case Action::CastImmediate:
			{
				// Immediate casting is handled by WheelItemSpell/Shout
				// This action is reserved for future policy-driven instant cast
				result = ActionResult::Success;
			}
			break;
			
			default:
				result = ActionResult::Failed;
				reason = "unknown_action";
				break;
			}
		}
		catch (const std::exception& e) {
			result = ActionResult::Failed;
			reason = e.what();
			logger::error("[ActionPolicy] Exception in RunAction: {}", e.what());
		}
		
		ctx.lastResult = result;
		if (result == ActionResult::Success) {
			ctx.actionsSucceeded++;
		}
		
		LogActionResult(ctx, action, result, reason);
		
		return result;
	}

	bool ExecutePolicy(ExecutionContext& ctx)
	{
		// Check for policy override
		ActionPolicy policy;
		{
			std::lock_guard<std::mutex> lock(s_overrideMutex);
			auto it = s_policyOverrides.find(ctx.formID);
			if (it != s_policyOverrides.end()) {
				policy = it->second;
				if (s_debugLogging) {
					logger::info("[ActionPolicy] Using policy override for form={:08X}", ctx.formID);
				}
			} else {
				policy = GetPolicy(ctx.category, ctx.handPref, ctx.intent);
			}
		}
		
		// Check inventory requirement
		if (policy.requiresInventory) {
			int count = GetInventoryCount(ctx.form);
			if (count <= 0) {
				LogActionResult(ctx, Action::NoOp, ActionResult::NotInInventory, "pre_check");
				return false;
			}
		}
		
		// Execute action chain
		for (size_t i = 0; i < policy.chain.size(); ++i) {
			Action action = policy.chain[i];
			ActionResult result = RunAction(ctx, action);
			
			if (result == ActionResult::Success) {
				return true;
			}
			
			// For unequip actions followed by equip, continue to the next action
			if ((action == Action::UnequipRight || action == Action::UnequipLeft) &&
				i + 1 < policy.chain.size()) {
				// Unequip succeeded or failed, try the equip anyway
				continue;
			}
			
			// Check if next action is a fallback (same action type)
			if (i + 1 < policy.chain.size()) {
				Action nextAction = policy.chain[i + 1];
				// If next is unequip followed by same equip, this is a fallback pattern
				if ((action == Action::EquipRight && nextAction == Action::UnequipRight) ||
					(action == Action::EquipLeft && nextAction == Action::UnequipLeft)) {
					// Continue to fallback
					continue;
				}
			}
		}
		
		return false;
	}

	// ============================================================================
	// Main Entry Points
	// ============================================================================

	bool ExecuteEntry(RE::TESForm* form, HandPref hand, ActivateIntent intent)
	{
		if (!form) {
			logger::warn("[ActionPolicy] ExecuteEntry called with null form");
			return false;
		}
		if (form->As<RE::TESObjectWEAP>()) {
			logger::warn(
				"[ActionPolicy] rejected FormID-only weapon execution form={:08X}; canonical WheelItemWeapon activation is required",
				form->GetFormID());
			return false;
		}
		
		ExecutionContext ctx;
		ctx.form = form;
		ctx.formID = form->GetFormID();
		ctx.formType = form->GetFormType();
		ctx.formName = form->GetName() ? form->GetName() : "";
		ctx.handPref = hand;
		ctx.intent = intent;
		ctx.category = Classify(form);
		
		LogActivationStart(ctx);
		
		bool success = ExecutePolicy(ctx);
		
		LogActivationEnd(ctx, success, ctx.lastAttemptedAction);
		
		return success;
	}

	bool ExecuteEntry(RE::TESForm* form, ActivateIntent intent)
	{
		return ExecuteEntry(form, IntentToHandPref(intent), intent);
	}

	// ============================================================================
	// Override System
	// ============================================================================

	void RegisterCategoryOverride(RE::FormID formID, ItemCategory category)
	{
		std::lock_guard<std::mutex> lock(s_overrideMutex);
		s_categoryOverrides[formID] = category;
		logger::info("[ActionPolicy] Registered category override: form={:08X} -> {}", 
			formID, CategoryToString(category));
	}

	void RegisterPolicyOverride(RE::FormID formID, const ActionPolicy& policy)
	{
		std::lock_guard<std::mutex> lock(s_overrideMutex);
		s_policyOverrides[formID] = policy;
		logger::info("[ActionPolicy] Registered policy override: form={:08X} ({} actions)", 
			formID, policy.chain.size());
	}

	void ClearOverrides()
	{
		std::lock_guard<std::mutex> lock(s_overrideMutex);
		s_categoryOverrides.clear();
		s_policyOverrides.clear();
		logger::info("[ActionPolicy] Cleared all overrides");
	}

	// ============================================================================
	// Debug Logging
	// ============================================================================

	void SetDebugLogging(bool enabled)
	{
		s_debugLogging = enabled;
	}

	bool IsDebugLoggingEnabled()
	{
		return s_debugLogging;
	}

	void LogActivationStart(const ExecutionContext& ctx)
	{
		if (!s_debugLogging) {
			return;
		}
		
		logger::info("[ActionPolicy] ACTIVATE START form={:08X} type={} category={} name='{}' hand={} intent={}",
			ctx.formID,
			static_cast<int>(ctx.formType),
			CategoryToString(ctx.category),
			ctx.formName,
			ctx.handPref == HandPref::Left ? "Left" : "Right",
			static_cast<int>(ctx.intent));
	}

	void LogActionAttempt(const ExecutionContext& ctx, Action action)
	{
		if (!s_debugLogging) {
			return;
		}
		
		// Check slot occupancy for relevant actions
		bool slotOccupied = false;
		if (action == Action::EquipRight) {
			RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
			slotOccupied = pc && pc->GetEquippedObject(false) != nullptr;
		} else if (action == Action::EquipLeft) {
			RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
			slotOccupied = pc && pc->GetEquippedObject(true) != nullptr;
		}
		
		logger::info("[ActionPolicy] TRY action={} hand={} occupied={}",
			ActionToString(action),
			action == Action::EquipLeft || action == Action::UnequipLeft ? "L" : "R",
			slotOccupied ? "true" : "false");
	}

	void LogActionResult(const ExecutionContext& ctx, Action action, ActionResult result, const char* reason)
	{
		if (!s_debugLogging) {
			return;
		}
		
		if (reason) {
			logger::info("[ActionPolicy] RESULT action={} success={} reason='{}'",
				ActionToString(action),
				result == ActionResult::Success ? "true" : "false",
				reason);
		} else {
			logger::info("[ActionPolicy] RESULT action={} success={}",
				ActionToString(action),
				result == ActionResult::Success ? "true" : "false");
		}
	}

	void LogActivationEnd(const ExecutionContext& ctx, bool success, Action finalAction)
	{
		if (!s_debugLogging) {
			return;
		}
		
		logger::info("[ActionPolicy] ACTIVATE END success={} finalAction={} attempts={} succeeded={}",
			success ? "true" : "false",
			ActionToString(finalAction),
			ctx.actionsAttempted,
			ctx.actionsSucceeded);
	}

}  // namespace ActionPolicy
