#include "bin/Rendering/Drawer.h"
#include "bin/Utilities/Utils.h"
#include "bin/Wheeler/Wheeler.h"
#include "bin/Wheeler/MainWheelDebug.h"
#include "bin/Texts.h"
#include "bin/Config.h"
#include "WheelItemAlchemy.h"
#include "AlreadyPoisonedReapplyGuardPolicy.h"
#include "bin/Utilities/InventorySnapshotCache.h"

#include <vector>

namespace
{
	static std::int32_t GetPlayerItemCount(RE::AlchemyItem* a_item)
	{
		if (!a_item) {
			return 0;
		}

		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return 0;
		}

		// Use TESObjectREFR::GetInventoryCounts from CommonLibSSE-NG to count items
		auto counts = pc->GetInventoryCounts();
		auto it = counts.find(a_item);
		return it != counts.end() ? it->second : 0;
	}

	template <class Fn>
	bool InvokeWithSehGuard(Fn&& a_fn)
	{
#if defined(_MSC_VER)
		__try {
			a_fn();
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
#else
		try {
			a_fn();
			return true;
		} catch (...) {
			return false;
		}
#endif
	}

	template <class TContainer>
	bool CopyExtraListsSafe(TContainer* a_extraLists, std::vector<RE::ExtraDataList*>& a_out)
	{
		if (!a_extraLists) {
			return false;
		}
		return InvokeWithSehGuard([&]() {
			for (auto* extraList : *a_extraLists) {
				a_out.push_back(extraList);
			}
		});
	}

	bool TryHasTypeSafe(RE::ExtraDataList* a_list, RE::ExtraDataType a_type, bool& a_outHasType)
	{
		a_outHasType = false;
		if (!a_list) {
			return false;
		}
		return InvokeWithSehGuard([&]() { a_outHasType = a_list->HasType(a_type); });
	}

	template <class T>
	T* GetByTypeSafe(RE::ExtraDataList* a_list)
	{
		T* result = nullptr;
		if (!a_list || !InvokeWithSehGuard([&]() { result = a_list->GetByType<T>(); })) {
			return nullptr;
		}
		return result;
	}

	RE::FormID GetEquippedWeaponFormID(RE::PlayerCharacter* a_player, bool a_leftHand)
	{
		if (!a_player) {
			return 0;
		}
		auto* equipped = a_player->GetEquippedObject(a_leftHand);
		auto* weapon = equipped ? equipped->As<RE::TESObjectWEAP>() : nullptr;
		return weapon ? weapon->GetFormID() : 0;
	}

	bool IsSinglePhysicalTwoHandedPoisonTarget(const RE::TESObjectWEAP* a_weapon)
	{
		if (!a_weapon) {
			return false;
		}
		switch (a_weapon->GetWeaponType()) {
		case RE::WEAPON_TYPE::kTwoHandSword:
		case RE::WEAPON_TYPE::kTwoHandAxe:
		case RE::WEAPON_TYPE::kBow:
		case RE::WEAPON_TYPE::kCrossbow:
			return true;
		default:
			return false;
		}
	}

	RE::TESObjectWEAP* ResolveSinglePhysicalTwoHandedScalarTarget(
		RE::TESObjectWEAP* a_right,
		RE::TESObjectWEAP* a_left)
	{
		auto* candidate = IsSinglePhysicalTwoHandedPoisonTarget(a_right) ? a_right :
		                  IsSinglePhysicalTwoHandedPoisonTarget(a_left) ? a_left : nullptr;
		if (!candidate || candidate->GetFormID() == 0) {
			return nullptr;
		}

		auto* opposite = candidate == a_right ? a_left : a_right;
		if (opposite && opposite->GetFormID() != candidate->GetFormID()) {
			return nullptr;
		}
		return candidate;
	}

	struct LivePoisonReapplyGuardEvaluation
	{
		AlreadyPoisonedReapplyGuardPolicy::Evidence evidence{};
		AlreadyPoisonedReapplyGuardPolicy::Result result{};
		std::int32_t singlePhysicalWeaponType = -1;
	};

	void MarkEvidenceUnreadableForForm(
		AlreadyPoisonedReapplyGuardPolicy::Evidence& a_evidence,
		RE::FormID a_formID)
	{
		if (a_evidence.targetModel == AlreadyPoisonedReapplyGuardPolicy::TargetModel::kSinglePhysicalTwoHanded &&
		    a_evidence.singlePhysicalTarget.formID == a_formID) {
			a_evidence.singlePhysicalTarget.readable = false;
			return;
		}
		if (a_evidence.right.equipped && a_evidence.right.formID == a_formID) {
			a_evidence.right.readable = false;
		}
		if (a_evidence.left.equipped && a_evidence.left.formID == a_formID) {
			a_evidence.left.readable = false;
		}
	}

	void AccumulateSinglePhysicalWornMember(
		AlreadyPoisonedReapplyGuardPolicy::HandEvidence& a_target,
		bool a_wornRight,
		bool a_wornLeft,
		bool a_containsExtraPoison)
	{
		if (!a_wornRight && !a_wornLeft) {
			return;
		}
		++a_target.targetWornMembers;
		if (a_containsExtraPoison) {
			++a_target.poisonedTargetMembers;
		}
	}

	void AccumulateWornMember(
		AlreadyPoisonedReapplyGuardPolicy::HandEvidence& a_hand,
		bool a_targetIsLeft,
		bool a_wornRight,
		bool a_wornLeft,
		bool a_containsExtraPoison)
	{
		const bool isTarget = a_targetIsLeft ? a_wornLeft : a_wornRight;
		const bool isOpposite = a_targetIsLeft ? a_wornRight : a_wornLeft;
		if (isTarget) {
			++a_hand.targetWornMembers;
			if (a_containsExtraPoison) {
				++a_hand.poisonedTargetMembers;
			}
		}
		if (isOpposite) {
			++a_hand.oppositeWornMembers;
		}
	}

	LivePoisonReapplyGuardEvaluation EvaluateLivePoisonReapplyGuard(RE::PlayerCharacter* a_player)
	{
		using namespace AlreadyPoisonedReapplyGuardPolicy;

		LivePoisonReapplyGuardEvaluation evaluation{};
		auto* rightEquipped = a_player ? a_player->GetEquippedObject(false) : nullptr;
		auto* leftEquipped = a_player ? a_player->GetEquippedObject(true) : nullptr;
		auto* rightWeapon = rightEquipped ? rightEquipped->As<RE::TESObjectWEAP>() : nullptr;
		auto* leftWeapon = leftEquipped ? leftEquipped->As<RE::TESObjectWEAP>() : nullptr;
		evaluation.evidence.right.formID = rightWeapon ? rightWeapon->GetFormID() : 0;
		evaluation.evidence.left.formID = leftWeapon ? leftWeapon->GetFormID() : 0;
		evaluation.evidence.right.equipped = evaluation.evidence.right.formID != 0;
		evaluation.evidence.left.equipped = evaluation.evidence.left.formID != 0;

		if (auto* singlePhysicalTarget = ResolveSinglePhysicalTwoHandedScalarTarget(rightWeapon, leftWeapon)) {
			evaluation.evidence.targetModel = TargetModel::kSinglePhysicalTwoHanded;
			evaluation.evidence.singlePhysicalTarget.equipped = true;
			evaluation.evidence.singlePhysicalTarget.formID = singlePhysicalTarget->GetFormID();
			evaluation.singlePhysicalWeaponType = static_cast<std::int32_t>(singlePhysicalTarget->GetWeaponType());
		}

		// Same-FormID dual wield is deliberately rejected before any physical-member
		// ownership is inferred. A worn marker, never FormID or UID alone, is required.
		if (evaluation.evidence.targetModel == TargetModel::kPerHand &&
		    evaluation.evidence.right.equipped && evaluation.evidence.left.equipped &&
		    evaluation.evidence.right.formID == evaluation.evidence.left.formID) {
			evaluation.result = Evaluate(evaluation.evidence);
			return evaluation;
		}

		RE::TESObjectREFR::InventoryItemMap inventory;
		if (!Utils::Inventory::TryGetInventorySnapshot(
				a_player, inventory, "WheelItemAlchemy::AlreadyPoisonedReapplyGuard")) {
			evaluation.result = Evaluate(evaluation.evidence);
			return evaluation;
		}

		if (evaluation.evidence.targetModel == TargetModel::kSinglePhysicalTwoHanded) {
			evaluation.evidence.singlePhysicalTarget.readable = true;
		} else {
			evaluation.evidence.right.readable = evaluation.evidence.right.equipped;
			evaluation.evidence.left.readable = evaluation.evidence.left.equipped;
		}

		for (auto& [boundObject, data] : inventory) {
			if (!boundObject) {
				continue;
			}
			const RE::FormID formID = boundObject->GetFormID();
			const bool matchesSinglePhysical =
				evaluation.evidence.targetModel == TargetModel::kSinglePhysicalTwoHanded &&
				evaluation.evidence.singlePhysicalTarget.formID == formID;
			const bool matchesRight = evaluation.evidence.targetModel == TargetModel::kPerHand &&
				evaluation.evidence.right.equipped &&
				evaluation.evidence.right.formID == formID;
			const bool matchesLeft = evaluation.evidence.targetModel == TargetModel::kPerHand &&
				evaluation.evidence.left.equipped &&
				evaluation.evidence.left.formID == formID;
			if (!matchesSinglePhysical && !matchesRight && !matchesLeft) {
				continue;
			}

			RE::InventoryEntryData* entry = data.second.get();
			decltype(entry->extraLists) extraLists = nullptr;
			if (data.first <= 0 || !entry ||
			    !InvokeWithSehGuard([&]() { extraLists = entry->extraLists; }) ||
			    !extraLists) {
				MarkEvidenceUnreadableForForm(evaluation.evidence, formID);
				continue;
			}

			std::vector<RE::ExtraDataList*> extraListSnapshot;
			if (!CopyExtraListsSafe(extraLists, extraListSnapshot)) {
				MarkEvidenceUnreadableForForm(evaluation.evidence, formID);
				continue;
			}

			for (auto* extraList : extraListSnapshot) {
				bool hasWorn = false;
				bool hasWornLeft = false;
				if (!TryHasTypeSafe(extraList, RE::ExtraDataType::kWorn, hasWorn) ||
				    !TryHasTypeSafe(extraList, RE::ExtraDataType::kWornLeft, hasWornLeft)) {
					MarkEvidenceUnreadableForForm(evaluation.evidence, formID);
					continue;
				}

				// CommonLib's worn truth table treats WornLeft as left authority even if
				// both markers are present; Worn alone identifies the right member.
				const bool wornLeft = hasWornLeft;
				const bool wornRight = hasWorn && !hasWornLeft;
				if (!wornRight && !wornLeft) {
					continue;
				}

				bool hasPoison = false;
				if (!TryHasTypeSafe(extraList, RE::ExtraDataType::kPoison, hasPoison) ||
				    (hasPoison && !GetByTypeSafe<RE::ExtraPoison>(extraList))) {
					MarkEvidenceUnreadableForForm(evaluation.evidence, formID);
					continue;
				}

				if (matchesSinglePhysical) {
					AccumulateSinglePhysicalWornMember(
						evaluation.evidence.singlePhysicalTarget, wornRight, wornLeft, hasPoison);
				} else if (matchesRight) {
					AccumulateWornMember(
						evaluation.evidence.right, false, wornRight, wornLeft, hasPoison);
				}
				if (matchesLeft) {
					AccumulateWornMember(
						evaluation.evidence.left, true, wornRight, wornLeft, hasPoison);
				}
			}
		}

		inventory.clear();

		// The equipped scalar forms must still match the snapshot boundary. Any
		// concurrent hand transition makes attribution ambiguous and fails closed.
		if (GetEquippedWeaponFormID(a_player, false) != evaluation.evidence.right.formID ||
		    GetEquippedWeaponFormID(a_player, true) != evaluation.evidence.left.formID) {
			evaluation.evidence.equippedStateStable = false;
		}

		evaluation.result = Evaluate(evaluation.evidence);
		return evaluation;
	}

}

WheelItemAlchemy::WheelItemAlchemy(RE::AlchemyItem* a_alchemyItem)
{
	this->_alchemyItem = a_alchemyItem;
	if (!a_alchemyItem) {
		logger::warn("WheelItemAlchemy: constructed with null alchemy item");
		return;
	}
	this->_formID = a_alchemyItem->GetFormID();
	
	Texture::icon_image_type iconType = Texture::icon_image_type::food;
	if (_alchemyItem->data.flags.any(RE::AlchemyItem::AlchemyFlag::kFoodItem)
		|| _alchemyItem->HasKeywordString("VendorItemFood")) {
		iconType = Texture::icon_image_type::food;
		this->_alchemyItemType = WheelItemAlchemyType::kFood;
	} else if (_alchemyItem->data.flags.any(RE::AlchemyItem::AlchemyFlag::kPoison)
			   || _alchemyItem->HasKeywordString("VendorItemPoison")) {
		iconType = Texture::icon_image_type::poison_default;
		this->_alchemyItemType = WheelItemAlchemyType::kPoison;
	} else if (_alchemyItem->data.flags.any(RE::AlchemyItem::AlchemyFlag::kMedicine)
			   || _alchemyItem->HasKeywordString("VendorItemPotion")) {
		iconType = Texture::icon_image_type::potion_default;
		this->_alchemyItemType = WheelItemAlchemyType::kPotion;
		// Safely get effect - GetCostliestEffectItem() can return null
		const RE::Effect* costliestEffect = _alchemyItem->GetCostliestEffectItem();
		if (costliestEffect && costliestEffect->baseEffect) {
			const RE::EffectSetting* effect = costliestEffect->baseEffect;
			RE::ActorValue actorValue = effect->GetMagickSkill();
			if (actorValue == RE::ActorValue::kNone) {
				actorValue = effect->data.primaryAV;
			}
			switch (actorValue) {
			case RE::ActorValue::kHealth:
			case RE::ActorValue::kHealRateMult:
			case RE::ActorValue::kHealRate:
				iconType = Texture::icon_image_type::potion_health;
				break;
			case RE::ActorValue::kStamina:
			case RE::ActorValue::kStaminaRateMult:
			case RE::ActorValue::kStaminaRate:
				iconType = Texture::icon_image_type::potion_stamina;
				break;
			case RE::ActorValue::kMagicka:
			case RE::ActorValue::kMagickaRateMult:
			case RE::ActorValue::kMagickaRate:
				iconType = Texture::icon_image_type::potion_magicka;
				break;
			case RE::ActorValue::kResistFire:
				iconType = Texture::icon_image_type::potion_fire_resist;
				break;
			case RE::ActorValue::kResistShock:
				iconType = Texture::icon_image_type::potion_shock_resist;
				break;
			case RE::ActorValue::kResistFrost:
				iconType = Texture::icon_image_type::potion_frost_resist;
				break;
			case RE::ActorValue::kResistMagic:
				iconType = Texture::icon_image_type::potion_magic_resist;
				break;
			default:
				iconType = Texture::icon_image_type::potion_default;
			}
		}
	} else {
		// Unknown alchemy item (Campfire supplies, deployables, etc.)
		// Treat as deployable - will be "used" via EquipObject
		iconType = Texture::icon_image_type::icon_default;
		this->_alchemyItemType = WheelItemAlchemyType::kDeployable;
	}
	this->_texture = Texture::GetIconImage(iconType, this->_alchemyItem);
	Utils::Magic::GetMagicItemDescription(_alchemyItem, this->_description);
}

void WheelItemAlchemy::DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	RE::AlchemyItem* alchemyItem = ResolveAlchemyItem();
	if (!alchemyItem) {
		// Item no longer exists (e.g. crafted potion consumed), treat slot as empty.
		return;
	}
	int itemCount = a_imap.contains(alchemyItem) ? a_imap.find(alchemyItem)->second.first : 0;
	const bool keepMissing = IsKeepMissingCategoryEnabled(MissingCategory::Consumable);
	const bool isDeployable = _alchemyItemType == WheelItemAlchemyType::kDeployable;
	if (Config::WheelBehavior::ClearDepletedConsumables && itemCount <= 0 && !keepMissing && !isDeployable) {
		Wheeler::QueueDepletedConsumablesCleanup();
		return;
	}
	std::string text = fmt::format("{} ({})", alchemyItem->GetName(), itemCount);
	this->drawSlotText(a_center, text.data(), a_drawArgs);
	this->drawSlotTexture(a_center, a_drawArgs);
	// Potion cooldown overlays intentionally skipped unless a supporting mod is integrated.
}

void WheelItemAlchemy::DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	RE::AlchemyItem* alchemyItem = ResolveAlchemyItem();
	if (!alchemyItem) {
		return;
	}
	int itemCount = a_imap.contains(alchemyItem) ? a_imap.find(alchemyItem)->second.first : 0;
	const bool keepMissing = IsKeepMissingCategoryEnabled(MissingCategory::Consumable);
	const bool isDeployable = _alchemyItemType == WheelItemAlchemyType::kDeployable;
	if (Config::WheelBehavior::ClearDepletedConsumables && itemCount <= 0 && !keepMissing && !isDeployable) {
		Wheeler::QueueDepletedConsumablesCleanup();
		return;
	}
	const float textShiftY = calculateHighlightTextShiftY(this->_description.c_str());
	this->drawHighlightText(a_center, alchemyItem->GetName(), a_drawArgs, textShiftY);
	this->drawHighlightTexture(a_center, a_drawArgs);
	if (!this->_description.empty()) {
		this->drawHighlightDescription(a_center, this->_description.data(), a_drawArgs, textShiftY);
	}
}

bool WheelItemAlchemy::IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	return false;
}

bool WheelItemAlchemy::IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	RE::AlchemyItem* alchemyItem = ResolveAlchemyItem();
	if (!alchemyItem) {
		return false;
	}
	return a_inv.contains(alchemyItem);
}

void WheelItemAlchemy::ActivateItemPrimary()
{
	if (!ResolveAlchemyItem()) {
		logger::debug("Alchemy: failed to resolve item, aborting");
		return;
	}
	switch (this->_alchemyItemType) {
	case WheelItemAlchemyType::kPotion:
	case WheelItemAlchemyType::kFood:
		this->consume();
		break;
	case WheelItemAlchemyType::kPoison:
		(void)this->applyPoison();
		break;
	case WheelItemAlchemyType::kDeployable:
	case WheelItemAlchemyType::kNone:
	default:
		// Fallback for unknown alchemy items (Campfire, deployables, custom mods)
		this->consume();
		break;
	}
}

void WheelItemAlchemy::ActivateItemSecondary()
{
	if (!ResolveAlchemyItem()) {
		logger::debug("Alchemy (secondary): failed to resolve item, aborting");
		return;
	}
	switch (this->_alchemyItemType) {
	case WheelItemAlchemyType::kPotion:
	case WheelItemAlchemyType::kFood:
		this->consume();
		break;
	case WheelItemAlchemyType::kPoison:
		(void)this->applyPoison();
		break;
	case WheelItemAlchemyType::kDeployable:
	case WheelItemAlchemyType::kNone:
	default:
		// Fallback for unknown alchemy items (Campfire, deployables, custom mods)
		this->consume();
		break;
	}
}

void WheelItemAlchemy::ActivateItemSpecial()
{
	return;
}

WheelItemActivationResult WheelItemAlchemy::ActivateItemWithResult(WheelItemActivationKind a_kind)
{
	if ((a_kind == WheelItemActivationKind::Primary ||
		 a_kind == WheelItemActivationKind::Secondary) &&
		_alchemyItemType == WheelItemAlchemyType::kPoison) {
		return applyPoison();
	}
	return WheelItem::ActivateItemWithResult(a_kind);
}

void WheelItemAlchemy::SerializeIntoJsonObj(nlohmann::json& a_json)
{
	a_json["type"] = WheelItemAlchemy::ITEM_TYPE_STR;
	a_json["formID"] = this->_formID;
}

void WheelItemAlchemy::consume()
{
	RE::AlchemyItem* alchemyItem = ResolveAlchemyItem();
	if (!alchemyItem) {
		return;
	}
	// Note: consume() is now also used for kDeployable items (Campfire, etc.)
	// The EquipObject call below triggers Papyrus scripts for these items.
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}
	const int countBefore = GetPlayerItemCount(alchemyItem);
	if (countBefore <= 0) {
		if (Config::WheelBehavior::ClearDepletedConsumables &&
			!IsKeepMissingCategoryEnabled(MissingCategory::Consumable) &&
			_alchemyItemType != WheelItemAlchemyType::kDeployable) {
			Wheeler::QueueDepletedConsumablesCleanup();
		}
		return;
	}
	if (!Config::WheelBehavior::ClearDepletedConsumables && alchemyItem->IsDynamicForm() && countBefore <= 1) {
		Utils::NotificationMessage(Texts::GetText(Texts::TextType::AlchemyDynamicIDConsumptionWarning));
		return;
	}
	InventorySnapshotCache::EquipObject(RE::ActorEquipManager::GetSingleton(), pc, alchemyItem);
	if (Config::WheelBehavior::ClearDepletedConsumables && countBefore <= 1 &&
		!IsKeepMissingCategoryEnabled(MissingCategory::Consumable) &&
		_alchemyItemType != WheelItemAlchemyType::kDeployable) {
		Wheeler::QueueDepletedConsumablesCleanup();
	}
}

WheelItemActivationResult WheelItemAlchemy::applyPoison()
{
	RE::AlchemyItem* alchemyItem = ResolveAlchemyItem();
	if (!alchemyItem) {
		return WheelItemActivationResult::InvalidTarget;
	}
	if (this->_alchemyItemType != WheelItemAlchemyType::kPoison) {
		logger::warn("WheelItemAlchemy::applyPoison: called for non-poison item");
		return WheelItemActivationResult::InvalidTarget;
	}
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return WheelItemActivationResult::InvalidTarget;
	}

	// Safety: poison application triggers a follow-up UI flow (choose weapon). If the wheel is still open, Wheeler's input filter
	// can block that menu and cause a soft-lock. Queue the poison apply to run after Wheeler fully closes.
	const auto rhsWeap = pc->GetEquippedObject(false) ? pc->GetEquippedObject(false)->As<RE::TESObjectWEAP>() : nullptr;
	const auto lhsWeap = pc->GetEquippedObject(true) ? pc->GetEquippedObject(true)->As<RE::TESObjectWEAP>() : nullptr;
	if (!rhsWeap && !lhsWeap) {
		Utils::NotificationMessage("Wheeler: Cannot apply poison (no weapon equipped).");
		logger::warn("Poison: blocked apply (no weapon equipped): {}", alchemyItem->GetName());
		return WheelItemActivationResult::InvalidTarget;
	}

	const int countBefore = GetPlayerItemCount(alchemyItem);
	if (countBefore <= 0) {
		logger::warn("Poison: blocked apply (poison unavailable): {:08X}", _formID);
		return WheelItemActivationResult::InvalidTarget;
	}
	const auto guard = EvaluateLivePoisonReapplyGuard(pc);
	if (!guard.result.AllowsQueue()) {
		logger::warn(
			"POISON_REAPPLY_GUARD decision=skip reason={} poisonForm={:08X} targetModel={} weaponType={} physicalForm={:08X} physicalReadable={} physicalMembers={} physicalPoisoned={} equippedStateStable={} rightForm={:08X} rightReadable={} rightTargetMembers={} rightOppositeMembers={} rightPoisoned={} leftForm={:08X} leftReadable={} leftTargetMembers={} leftOppositeMembers={} leftPoisoned={}",
			AlreadyPoisonedReapplyGuardPolicy::ReasonName(guard.result.reason),
			_formID,
			AlreadyPoisonedReapplyGuardPolicy::TargetModelName(guard.evidence.targetModel),
			guard.singlePhysicalWeaponType,
			guard.evidence.singlePhysicalTarget.formID,
			guard.evidence.singlePhysicalTarget.readable ? 1 : 0,
			guard.evidence.singlePhysicalTarget.targetWornMembers,
			guard.evidence.singlePhysicalTarget.poisonedTargetMembers,
			guard.evidence.equippedStateStable ? 1 : 0,
			guard.evidence.right.formID,
			guard.evidence.right.readable ? 1 : 0,
			guard.evidence.right.targetWornMembers,
			guard.evidence.right.oppositeWornMembers,
			guard.evidence.right.poisonedTargetMembers,
			guard.evidence.left.formID,
			guard.evidence.left.readable ? 1 : 0,
			guard.evidence.left.targetWornMembers,
			guard.evidence.left.oppositeWornMembers,
			guard.evidence.left.poisonedTargetMembers);
		if (guard.result.decision == AlreadyPoisonedReapplyGuardPolicy::Decision::kBlockAlreadyPoisoned) {
			Utils::NotificationMessage(Texts::GetText(Texts::TextType::PoisonAlreadyApplied));
			return WheelItemActivationResult::AlreadyPoisoned;
		}
		Utils::NotificationMessage(Texts::GetText(Texts::TextType::PoisonSafeResolutionFailed));
		return WheelItemActivationResult::UnsafeResolution;
	}
	if (!Wheeler::QueuePoisonApply(_formID)) {
		logger::warn("Poison: queue admission rejected for {:08X}", _formID);
		return WheelItemActivationResult::Rejected;
	}
	if (Config::WheelBehavior::ClearDepletedConsumables && countBefore <= 1 &&
		!IsKeepMissingCategoryEnabled(MissingCategory::Consumable)) {
		Wheeler::QueueDepletedConsumablesCleanup();
	}
	return WheelItemActivationResult::Succeeded;
}

bool WheelItemAlchemy::IsDepletedInPlayerInventory()
{
	RE::AlchemyItem* alchemyItem = ResolveAlchemyItem();
	if (!alchemyItem) {
		return true;
	}
	return GetPlayerItemCount(alchemyItem) <= 0;
}

RE::AlchemyItem* WheelItemAlchemy::ResolveAlchemyItem()
{
	if (_formID == 0) {
		return nullptr;
	}

	// Always resolve from the form table to avoid holding on to stale pointers
	// for dynamic (crafted) items that the game may have destroyed.
	RE::AlchemyItem* alchemyItem = RE::TESForm::LookupByID<RE::AlchemyItem>(_formID);
	if (!alchemyItem) {
		return nullptr;
	}

	// For dynamic forms, make sure the player still has at least one copy.
	if (alchemyItem->IsDynamicForm()) {
		if (GetPlayerItemCount(alchemyItem) <= 0) {
			return nullptr;
		}
	}

	return alchemyItem;
}

const char* WheelItemAlchemy::GetItemName() const
{
	RE::AlchemyItem* item = RE::TESForm::LookupByID<RE::AlchemyItem>(_formID);
	return item ? item->GetName() : "(deleted)";
}
