#include "bin/Rendering/Drawer.h"
#include "bin/Utilities/Utils.h"
#include "bin/Wheeler/Wheeler.h"
#include "bin/Texts.h"
#include "bin/Config.h"
#include "WheelItemAlchemy.h"
#include "RE/B/BGSEntryPointPerkEntry.h"

#include <unordered_set>

namespace
{
	constexpr RE::FormID kTrackedI4DiagFormID = 0x00057A7A;

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

	static char ToLowerAscii(char a_ch)
	{
		return a_ch >= 'A' && a_ch <= 'Z' ? static_cast<char>(a_ch - 'A' + 'a') : a_ch;
	}

	static bool ContainsCaseInsensitive(std::string_view a_text, std::string_view a_needle)
	{
		if (a_needle.empty()) {
			return true;
		}
		if (a_text.size() < a_needle.size()) {
			return false;
		}

		for (size_t pos = 0; pos + a_needle.size() <= a_text.size(); ++pos) {
			bool matched = true;
			for (size_t i = 0; i < a_needle.size(); ++i) {
				if (ToLowerAscii(a_text[pos + i]) != ToLowerAscii(a_needle[i])) {
					matched = false;
					break;
				}
			}
			if (matched) {
				return true;
			}
		}
		return false;
	}

	static const char* SafeName(const RE::TESForm* a_form)
	{
		const char* name = a_form ? a_form->GetName() : nullptr;
		return name ? name : "";
	}

	static const char* SafeEditorID(const RE::TESForm* a_form)
	{
		const char* editorID = a_form ? a_form->GetFormEditorID() : nullptr;
		return editorID ? editorID : "";
	}

	static bool IsTargetAlchemyDescriptionDiagItem(RE::AlchemyItem* a_item, std::string_view a_description)
	{
		if (!a_item) {
			return false;
		}

		const char* name = a_item->GetName();
		if (name && ContainsCaseInsensitive(name, "frost shield")) {
			return true;
		}

		return ContainsCaseInsensitive(a_description, "SURV") ||
		       ContainsCaseInsensitive(a_description, "Cold") ||
		       ContainsCaseInsensitive(a_description, "Warmth");
	}

	static bool IsRelevantDurationEntryPoint(RE::BGSEntryPoint::ENTRY_POINT a_entryPoint)
	{
		using EP = RE::BGSEntryPoint::ENTRY_POINTS;
		return a_entryPoint == EP::kModSpellDuration ||
		       a_entryPoint == EP::kModAlchemyEffectiveness ||
		       a_entryPoint == EP::kModPositiveChemDuration ||
		       a_entryPoint == EP::kModPotionsCreated;
	}

	static const char* GetEntryPointName(RE::BGSEntryPoint::ENTRY_POINT a_entryPoint)
	{
		using EP = RE::BGSEntryPoint::ENTRY_POINTS;
		switch (a_entryPoint) {
		case EP::kModSpellDuration:
			return "ModSpellDuration";
		case EP::kModAlchemyEffectiveness:
			return "ModAlchemyEffectiveness";
		case EP::kModPositiveChemDuration:
			return "ModPositiveChemDuration";
		case EP::kModPotionsCreated:
			return "ModPotionsCreated";
		default:
			return "Other";
		}
	}

	static bool IsRelevantActorValue(RE::ActorValue a_av)
	{
		return a_av == RE::ActorValue::kAlchemy ||
		       a_av == RE::ActorValue::kAlchemyModifier ||
		       a_av == RE::ActorValue::kAlchemyPowerModifier ||
		       a_av == RE::ActorValue::kRestorationPowerModifier ||
		       a_av == RE::ActorValue::kAlterationPowerModifier;
	}

	static void LogRelevantPerkEntries(RE::PlayerCharacter* a_pc)
	{
		if (!a_pc) {
			return;
		}

		using EP = RE::BGSEntryPoint::ENTRY_POINTS;
		logger::info(
			"[AlchemyDescDiag] playerEntryPoints ModSpellDuration={} ModAlchemyEffectiveness={} ModPositiveChemDuration={} ModPotionsCreated={}",
			a_pc->HasPerkEntries(EP::kModSpellDuration) ? 1 : 0,
			a_pc->HasPerkEntries(EP::kModAlchemyEffectiveness) ? 1 : 0,
			a_pc->HasPerkEntries(EP::kModPositiveChemDuration) ? 1 : 0,
			a_pc->HasPerkEntries(EP::kModPotionsCreated) ? 1 : 0);

		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			return;
		}

		int logged = 0;
		for (auto* perk : handler->GetFormArray<RE::BGSPerk>()) {
			if (!perk || !a_pc->HasPerk(perk)) {
				continue;
			}

			for (auto* entry : perk->perkEntries) {
				if (!entry || !IsRelevantDurationEntryPoint(entry->GetFunction())) {
					continue;
				}

				logger::info(
					"[AlchemyDescDiag] playerPerkEntry perk={:08X} name='{}' editor='{}' entry={} rank={} priority={}",
					perk->GetFormID(),
					SafeName(perk),
					SafeEditorID(perk),
					GetEntryPointName(entry->GetFunction()),
					static_cast<int>(entry->GetRank()),
					static_cast<int>(entry->GetPriority()));

				if (++logged >= 32) {
					logger::info("[AlchemyDescDiag] playerPerkEntry log truncated at 32 entries");
					return;
				}
			}
		}
	}

	static void LogRelevantActiveEffects(RE::PlayerCharacter* a_pc)
	{
		if (!a_pc) {
			return;
		}

		auto* magicTarget = a_pc->AsMagicTarget();
		auto* activeEffects = magicTarget ? magicTarget->GetActiveEffectList() : nullptr;
		if (!activeEffects) {
			logger::info("[AlchemyDescDiag] activeEffects unavailable");
			return;
		}

		int logged = 0;
		for (auto* active : *activeEffects) {
			if (!active || !active->effect || !active->effect->baseEffect) {
				continue;
			}

			auto* baseEffect = active->effect->baseEffect;
			const bool relevant =
				IsRelevantActorValue(baseEffect->data.primaryAV) ||
				IsRelevantActorValue(baseEffect->data.secondaryAV) ||
				ContainsCaseInsensitive(SafeName(baseEffect), "alchemy") ||
				ContainsCaseInsensitive(SafeName(baseEffect), "potion") ||
				ContainsCaseInsensitive(SafeEditorID(baseEffect), "alchemy") ||
				ContainsCaseInsensitive(SafeEditorID(baseEffect), "potion");
			if (!relevant) {
				continue;
			}

			logger::info(
				"[AlchemyDescDiag] activeEffect spell={:08X} spellName='{}' effect={:08X} effectName='{}' effectEditor='{}' archetype={} primaryAV={} secondaryAV={} mag={:.2f} dur={:.2f} elapsed={:.2f}",
				active->spell ? active->spell->GetFormID() : 0,
				active->spell ? active->spell->GetName() : "",
				baseEffect->GetFormID(),
				SafeName(baseEffect),
				SafeEditorID(baseEffect),
				static_cast<int>(baseEffect->GetArchetype()),
				static_cast<int>(baseEffect->data.primaryAV),
				static_cast<int>(baseEffect->data.secondaryAV),
				active->magnitude,
				active->duration,
				active->elapsedSeconds);

			if (++logged >= 32) {
				logger::info("[AlchemyDescDiag] activeEffect log truncated at 32 entries");
				return;
			}
		}
	}

	static void LogAlchemyDescriptionDiagnostic(RE::AlchemyItem* a_item, std::string_view a_cachedDescription)
	{
		if (!IsTargetAlchemyDescriptionDiagItem(a_item, a_cachedDescription)) {
			return;
		}

		static std::unordered_set<RE::FormID> loggedForms;
		if (!loggedForms.insert(a_item->GetFormID()).second) {
			return;
		}

		RE::BSString rawDescription;
		Utils::Magic::GetMagicItemDescription(a_item, rawDescription);

		logger::info("[AlchemyDescDiag] item form={:08X} name='{}' editor='{}' rawDesc='{}' cachedDesc='{}'",
			a_item->GetFormID(),
			a_item->GetName(),
			SafeEditorID(a_item),
			rawDescription.c_str(),
			a_cachedDescription);

		int idx = 0;
		for (auto* effect : a_item->effects) {
			auto* baseEffect = effect ? effect->baseEffect : nullptr;
			logger::info(
				"[AlchemyDescDiag] effect[{}] effectForm={:08X} effectName='{}' effectEditor='{}' magnitude={:.2f} duration={} area={} archetype={} primaryAV={} secondaryAV={} noDuration={} powerAffectsDuration={}",
				idx++,
				baseEffect ? baseEffect->GetFormID() : 0,
				SafeName(baseEffect),
				SafeEditorID(baseEffect),
				effect ? effect->GetMagnitude() : 0.0f,
				effect ? effect->GetDuration() : 0,
				effect ? effect->GetArea() : 0,
				baseEffect ? static_cast<int>(baseEffect->GetArchetype()) : -1,
				baseEffect ? static_cast<int>(baseEffect->data.primaryAV) : -1,
				baseEffect ? static_cast<int>(baseEffect->data.secondaryAV) : -1,
				baseEffect && baseEffect->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kNoDuration) ? 1 : 0,
				baseEffect && baseEffect->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsDuration) ? 1 : 0);
		}

		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (pc) {
			auto* avOwner = pc->AsActorValueOwner();
			logger::info(
				"[AlchemyDescDiag] playerAV alchemy={:.2f} alchemyMod={:.2f} alchemyPowerMod={:.2f} restorationPowerMod={:.2f} alterationPowerMod={:.2f}",
				avOwner ? avOwner->GetActorValue(RE::ActorValue::kAlchemy) : 0.0f,
				avOwner ? avOwner->GetActorValue(RE::ActorValue::kAlchemyModifier) : 0.0f,
				avOwner ? avOwner->GetActorValue(RE::ActorValue::kAlchemyPowerModifier) : 0.0f,
				avOwner ? avOwner->GetActorValue(RE::ActorValue::kRestorationPowerModifier) : 0.0f,
				avOwner ? avOwner->GetActorValue(RE::ActorValue::kAlterationPowerModifier) : 0.0f);
			LogRelevantPerkEntries(pc);
			LogRelevantActiveEffects(pc);
		}
	}
}

// The alchemy/potion classification below derives from LamasTinyHUD revision
// dd1794c46b1f87cbf04a5d60968facbed0605d02 (GNU GPL v3), inherited through
// original Wheeler and subsequently adapted for Wheeler Refined.
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
	if (_formID == kTrackedI4DiagFormID) {
		logger::info(
			"[I4Diag:00057A7A][native] name='{}' type={} isFood={} foodFlag={} vendorFood={} vendorPotion={} vendorPoison={} isPoison={} isMedicine={} iconType={}",
			_alchemyItem->GetName(),
			static_cast<std::uint32_t>(_alchemyItemType),
			_alchemyItem->IsFood(),
			_alchemyItem->data.flags.any(RE::AlchemyItem::AlchemyFlag::kFoodItem),
			_alchemyItem->HasKeywordString("VendorItemFood"),
			_alchemyItem->HasKeywordString("VendorItemPotion"),
			_alchemyItem->HasKeywordString("VendorItemPoison"),
			_alchemyItem->IsPoison(),
			_alchemyItem->data.flags.any(RE::AlchemyItem::AlchemyFlag::kMedicine),
			static_cast<std::uint32_t>(iconType));
	}
	this->_texture = Texture::GetIconImage(iconType, this->_alchemyItem);
	Utils::Magic::GetMagicItemDescription(_alchemyItem, this->_description);
	LogAlchemyDescriptionDiagnostic(_alchemyItem, this->_description);
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
		this->applyPoison();
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
		this->applyPoison();
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
	RE::ActorEquipManager::GetSingleton()->EquipObject(pc, alchemyItem);
	if (Config::WheelBehavior::ClearDepletedConsumables && countBefore <= 1 &&
		!IsKeepMissingCategoryEnabled(MissingCategory::Consumable) &&
		_alchemyItemType != WheelItemAlchemyType::kDeployable) {
		Wheeler::QueueDepletedConsumablesCleanup();
	}
}

void WheelItemAlchemy::applyPoison()
{
	RE::AlchemyItem* alchemyItem = ResolveAlchemyItem();
	if (!alchemyItem) {
		return;
	}
	if (this->_alchemyItemType != WheelItemAlchemyType::kPoison) {
		logger::warn("WheelItemAlchemy::applyPoison: called for non-poison item");
		return;
	}
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}

	// Safety: poison application triggers a follow-up UI flow (choose weapon). If the wheel is still open, Wheeler's input filter
	// can block that menu and cause a soft-lock. Queue the poison apply to run after Wheeler fully closes.
	const auto rhsWeap = pc->GetEquippedObject(false) ? pc->GetEquippedObject(false)->As<RE::TESObjectWEAP>() : nullptr;
	const auto lhsWeap = pc->GetEquippedObject(true) ? pc->GetEquippedObject(true)->As<RE::TESObjectWEAP>() : nullptr;
	if (!rhsWeap && !lhsWeap) {
		Utils::NotificationMessage("Wheeler: Cannot apply poison (no weapon equipped).");
		logger::warn("Poison: blocked apply (no weapon equipped): {}", alchemyItem->GetName());
		return;
	}

	const int countBefore = GetPlayerItemCount(alchemyItem);
	Wheeler::QueuePoisonApply(_formID);
	if (Config::WheelBehavior::ClearDepletedConsumables && countBefore <= 1 &&
		!IsKeepMissingCategoryEnabled(MissingCategory::Consumable)) {
		Wheeler::QueueDepletedConsumablesCleanup();
	}
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
		_alchemyItem = nullptr;
		return nullptr;
	}

	// For dynamic forms, make sure the player still has at least one copy.
	if (alchemyItem->IsDynamicForm()) {
		if (GetPlayerItemCount(alchemyItem) <= 0) {
			_alchemyItem = nullptr;
			return nullptr;
		}
	}

	_alchemyItem = alchemyItem;
	return _alchemyItem;
}

const char* WheelItemAlchemy::GetItemName() const
{
	RE::AlchemyItem* item = RE::TESForm::LookupByID<RE::AlchemyItem>(_formID);
	return item ? item->GetName() : "(deleted)";
}
