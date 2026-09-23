#include "bin/Utilities/Utils.h"
#include "bin/Utilities/ActorVirtualCompat.h"
#include "bin/Rendering/Drawer.h"
#include "bin/Config.h"
#include "bin/Wheeler/Wheeler.h"
#include "bin/Wheeler/TransformWheelManager.h"
#include "bin/Texts.h"

#include "WheelItemSpell.h"
#include "bin/Utilities/InventorySnapshotCache.h"
#include <fmt/format.h>
#include <cmath>
#include <sstream>
#include <algorithm>

static ImFont* GetCooldownTimerFont()
{
	const std::uint32_t idx = Config::Cooldowns::TimerText::FontIndex;
	if (idx == 0) {
		return ImGui::GetFont();
	}

	ImGuiIO& io = ImGui::GetIO();
	const std::uint32_t atlasIndex = idx - 1;
	if (io.Fonts && atlasIndex < static_cast<std::uint32_t>(io.Fonts->Fonts.Size) && io.Fonts->Fonts[atlasIndex]) {
		return io.Fonts->Fonts[atlasIndex];
	}
	return ImGui::GetFont();
}

static bool IsConcentrationInstantAllowed(const RE::SpellItem* spell)
{
	if (!spell) {
		return true;
	}
	const bool isConcentration = spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration;
	return !isConcentration || Config::WheelBehavior::InstantSpellConcentrationMode == 1;
}

static bool IsSpellBlockedByTransformGuard(RE::SpellItem* spell, const char* sourceLabel)
{
	if (!spell) {
		return false;
	}
	if (!TransformWheelManager::IsSpellActivationBlocked(spell, sourceLabel)) {
		return false;
	}

	logger::info("TransformWheels: blocked spell activation source={} formId={:08X} edid='{}' name='{}'",
		sourceLabel ? sourceLabel : "",
		spell->GetFormID(),
		spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
		spell->GetName() ? spell->GetName() : "");
	return true;
}

WheelItemSpell::WheelItemSpell(RE::SpellItem* a_spell)
{
	_spell = a_spell;
	const RE::Effect* costliestEffect = a_spell ? a_spell->GetCostliestEffectItem() : nullptr;
	const RE::EffectSetting* effect = costliestEffect ? costliestEffect->baseEffect : nullptr;
	Texture::icon_image_type iconType = Texture::icon_image_type::spell_default;

	if (effect) {
		auto actorValue = effect->GetMagickSkill();
		if (actorValue == RE::ActorValue::kNone) {
			actorValue = effect->data.primaryAV;
		}

		switch (actorValue) {
		case RE::ActorValue::kAlteration:
			iconType = Texture::icon_image_type::alteration;
			break;
		case RE::ActorValue::kConjuration:
			iconType = Texture::icon_image_type::conjuration;
			break;
		case RE::ActorValue::kDestruction:
			switch (effect->data.resistVariable) {
			case RE::ActorValue::kResistFire:
				iconType = Texture::icon_image_type::destruction_fire;
				break;
			case RE::ActorValue::kResistFrost:
				iconType = Texture::icon_image_type::destruction_frost;
				break;
			case RE::ActorValue::kResistShock:
				iconType = Texture::icon_image_type::destruction_shock;
				break;
			default:
				iconType = Texture::icon_image_type::destruction;
			}
			break;
		case RE::ActorValue::kIllusion:
			iconType = Texture::icon_image_type::illusion;
			break;
		case RE::ActorValue::kRestoration:
			//might not fit all spells
			iconType = Texture::icon_image_type::restoration;
			break;
		default:
			iconType = Texture::icon_image_type::spell_default;
		}
	} else if (Config::WheelBehavior::TransformWheels::DebugLog) {
		logger::info("WheelItemSpell: missing costliest/base effect formId={:08X} edid='{}' name='{}'",
			a_spell ? a_spell->GetFormID() : 0,
			(a_spell && a_spell->GetFormEditorID()) ? a_spell->GetFormEditorID() : "",
			(a_spell && a_spell->GetName()) ? a_spell->GetName() : "");
	}

	if (iconType == Texture::icon_image_type::spell_default) { // haven't found spell icon yet, maybe a power
		if (a_spell->data.spellType == RE::MagicSystem::SpellType::kPower ||
			a_spell->data.spellType == RE::MagicSystem::SpellType::kLesserPower ||
			a_spell->data.spellType == RE::MagicSystem::SpellType::kVoicePower) {
			iconType = Texture::icon_image_type::power;
		}
	}
	this->_texture = Texture::GetIconImage(iconType, a_spell);
	RE::BSString descriptionBuf = "";
	this->_spell->GetDescription(descriptionBuf, nullptr);
	this->_description = descriptionBuf.c_str();
}

void WheelItemSpell::DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	if (TransformWheelManager::IsMajorTransformSpellBlockedForCurrentState(_spell) ||
		TransformWheelManager::IsVampireLordSpellHiddenForCurrentMode(_spell)) {
		a_drawArgs.alphaMult *= 0.35f;
	}

	// NOTE: cooldown detection for spells/powers is currently disabled in HasCooldown() for safety/compat.
	// This rendering path supports the desired "restore to normal" fill if cooldown data is enabled later.
	const bool cooldownsEnabled = Config::Cooldowns::Enabled;
	const float remainingPercent = cooldownsEnabled ? this->GetCooldownPercent() : 0.0f;
	const float progress = std::clamp(1.0f - remainingPercent, 0.0f, 1.0f);

	if (cooldownsEnabled && remainingPercent > 0.0f && progress < 1.0f) {
		const float dimStrength = std::clamp(Config::Cooldowns::ContentDimAlpha, 0.0f, 1.0f);
		const float dimFactor = std::clamp(1.0f - dimStrength, 0.0f, 1.0f);

		DrawArgs dimArgs = a_drawArgs;
		dimArgs.alphaMult *= dimFactor;

		this->drawSlotText(a_center, this->_spell->GetName(), dimArgs);
		this->drawSlotTexture(a_center, dimArgs);

		Texture::Image slotBg = Texture::GetIconImage(Texture::icon_image_type::slot_background);
		const float bgScale = Config::Styling::Item::Slot::BackgroundTexture::Scale;
		ImVec2 size(slotBg.width * bgScale, slotBg.height * bgScale);
		if (size.x <= 0.0f || size.y <= 0.0f) {
			const float iconScale = Config::Styling::Item::Slot::Texture::Scale;
			size = ImVec2(_texture.width * iconScale, _texture.height * iconScale);
		}

		const ImVec2 posMin(a_center.x - size.x * 0.5f, a_center.y - size.y * 0.5f);
		const ImVec2 posMax(a_center.x + size.x * 0.5f, a_center.y + size.y * 0.5f);
		const float fillHeight = size.y * progress;
		const ImVec2 clipMin(posMin.x, posMax.y - fillHeight);
		const ImVec2 clipMax(posMax.x, posMax.y);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(clipMin, clipMax, true);
		this->drawSlotText(a_center, this->_spell->GetName(), a_drawArgs);
		this->drawSlotTexture(a_center, a_drawArgs);
		drawList->PopClipRect();

		if (Config::Cooldowns::ShowTimer) {
			const float remainingSeconds = this->GetCooldownRemainingSeconds();
			if (remainingSeconds > 0.0f) {
				const std::string txt = fmt::format("{:.0f}s", std::ceil(remainingSeconds));
				DrawArgs timerArgs = a_drawArgs;
				const float fontSize = Config::Cooldowns::TimerText::Size > 0.0f ?
				                           Config::Cooldowns::TimerText::Size :
				                           (std::max)(14.0f, Config::Styling::Item::Slot::Text::Size * 0.85f);
				Drawer::draw_text_with_font(a_center.x, a_center.y, txt.c_str(), Config::Cooldowns::TimerText::Color, GetCooldownTimerFont(), fontSize, timerArgs, true);
			}
		}
		return;
	}

	this->drawSlotText(a_center, this->_spell->GetName(), a_drawArgs);
	this->drawSlotTexture(a_center, a_drawArgs);
}

void WheelItemSpell::DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	if (TransformWheelManager::IsMajorTransformSpellBlockedForCurrentState(_spell) ||
		TransformWheelManager::IsVampireLordSpellHiddenForCurrentMode(_spell)) {
		a_drawArgs.alphaMult *= 0.35f;
	}

	std::string descriptionBuf = "";
	descriptionBuf = this->_description;

	if (descriptionBuf.empty()) { // get description of magic effect only if the original description is empty
		Utils::Magic::GetMagicItemDescription(_spell, descriptionBuf);
	}
	const float textShiftY = calculateHighlightTextShiftY(descriptionBuf.c_str());
	this->drawHighlightText(a_center, this->_spell->GetName(), a_drawArgs, textShiftY);
	this->drawHighlightTexture(a_center, a_drawArgs);
	if (!descriptionBuf.empty()) {
		this->drawHighlightDescription(a_center, descriptionBuf.data(), a_drawArgs, textShiftY);
	}

}

bool WheelItemSpell::IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv)
{

	auto pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return false;
	}
	bool lhsEquipped = false;
	bool rhsEquipped = false;
	bool powerEquipped = false;

	if (this->isPower()) { // is power, meaning it can be equipped to shout slot
		auto power = pc->GetActorRuntimeData().selectedPower;
		if (power) {
			powerEquipped = power->GetFormID() == this->_spell->GetFormID();
		}
	}
	// power can also be equipped to lhs or rhs, so we're check them as well.
	auto lhs = pc->GetEquippedObject(true);
	lhsEquipped = lhs && lhs->GetFormID() == this->_spell->GetFormID();

	auto rhs = pc->GetEquippedObject(false);
	rhsEquipped = rhs && rhs->GetFormID() == this->_spell->GetFormID();

	return lhsEquipped || rhsEquipped || powerEquipped;
}

bool WheelItemSpell::IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	auto pc = RE::PlayerCharacter::GetSingleton();
	return pc && pc->HasSpell(this->_spell);
}

void WheelItemSpell::ActivateItemSecondary()
{
	auto pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}
	auto aeMan = RE::ActorEquipManager::GetSingleton();
	if (!aeMan) {
		return;
	}
	if (IsSpellBlockedByTransformGuard(this->_spell, "EquipSecondary")) {
		return;
	}

	if (this->isPower()) {
		// Powers go to the voice/power slot
		auto selectedPower = pc->GetActorRuntimeData().selectedPower;
		if (selectedPower && selectedPower->GetFormID() == this->_spell->GetFormID()) {
			// Already equipped as power - unequip by setting selectedPower to nullptr
			pc->GetActorRuntimeData().selectedPower = nullptr;
		} else {
			// Not equipped - equip it
			InventorySnapshotCache::EquipSpell(aeMan, pc, this->_spell, Utils::Slot::GetVoiceSlot());
		}
	} else {
		// Regular spells go to left hand
		auto lhs = pc->GetEquippedObject(true);
		if (lhs && lhs->GetFormID() == this->_spell->GetFormID()) {
			// Already equipped in left hand - unequip it by cleaning the slot
			Utils::Slot::CleanSlot(pc, Utils::Slot::GetLeftHandSlot());
		} else {
			// Not equipped - equip it
			// Check if player was already in combat (weapon/magic drawn) BEFORE equipping
			bool wasAlreadyDrawn = pc->AsActorState()->IsWeaponDrawn();
			InventorySnapshotCache::EquipSpell(aeMan, pc, this->_spell, Utils::Slot::GetLeftHandSlot());
			if (Config::WheelBehavior::AutoDrawOnUse) {
				ActorVirtualCompat::DrawWeaponMagicHands(pc, true);
			} else {
				// Only sheathe if the player was NOT already in combat.
				// If they were already drawn, maintain combat stance for smooth spell swapping.
				if (!wasAlreadyDrawn) {
					ActorVirtualCompat::DrawWeaponMagicHands(pc, false);
				} else {
					// Force re-draw to maintain combat flow during spell swap
					ActorVirtualCompat::DrawWeaponMagicHands(pc, true);
				}
			}
		}
	}
}

void WheelItemSpell::ActivateItemPrimary()
{
	auto pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}
	auto aeMan = RE::ActorEquipManager::GetSingleton();
	if (!aeMan) {
		return;
	}
	if (IsSpellBlockedByTransformGuard(this->_spell, "EquipPrimary")) {
		return;
	}

	if (this->isPower()) {
		// Powers go to the voice/power slot
		auto selectedPower = pc->GetActorRuntimeData().selectedPower;
		if (selectedPower && selectedPower->GetFormID() == this->_spell->GetFormID()) {
			// Already equipped as power - unequip by setting selectedPower to nullptr
			pc->GetActorRuntimeData().selectedPower = nullptr;
		} else {
			// Not equipped - equip it
			InventorySnapshotCache::EquipSpell(aeMan, pc, this->_spell, Utils::Slot::GetVoiceSlot());
		}
	} else {
		// Regular spells go to right hand
		auto rhs = pc->GetEquippedObject(false);
		if (rhs && rhs->GetFormID() == this->_spell->GetFormID()) {
			// Already equipped in right hand - unequip it by cleaning the slot
			Utils::Slot::CleanSlot(pc, Utils::Slot::GetRightHandSlot());
		} else {
			// Not equipped - equip it
			// Check if player was already in combat (weapon/magic drawn) BEFORE equipping
			bool wasAlreadyDrawn = pc->AsActorState()->IsWeaponDrawn();
			InventorySnapshotCache::EquipSpell(aeMan, pc, this->_spell, Utils::Slot::GetRightHandSlot());
			if (Config::WheelBehavior::AutoDrawOnUse) {
				ActorVirtualCompat::DrawWeaponMagicHands(pc, true);
			} else {
				// Only sheathe if the player was NOT already in combat.
				// If they were already drawn, maintain combat stance for smooth spell swapping.
				if (!wasAlreadyDrawn) {
					ActorVirtualCompat::DrawWeaponMagicHands(pc, false);
				} else {
					// Force re-draw to maintain combat flow during spell swap
					ActorVirtualCompat::DrawWeaponMagicHands(pc, true);
				}
			}
		}
	}
}


void WheelItemSpell::ActivateItemSpecial()
{
	return; // current don't do anything because I'm yet to figure out how to prevent the power from being casted when it shouldn't
	auto pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}
	if (this->_spell->data.flags.any(RE::SpellItem::SpellFlag::kInstantCast)
		|| this->_spell->GetSpellType() == RE::MagicSystem::SpellType::kPower
		|| this->_spell->GetSpellType() == RE::MagicSystem::SpellType::kLesserPower) { // insta cast the spell or power

		auto selfTargeting = this->_spell->GetDelivery() == RE::MagicSystem::Delivery::kSelf;
		RE::TESObjectREFR* target = selfTargeting ? pc : pc->GetActorRuntimeData().currentCombatTarget.get().get();
		float castMagnitude = _spell->GetCostliestEffectItem() ? _spell->GetCostliestEffectItem()->GetMagnitude() : 1.f;

		RE::MagicCaster* caster = pc->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
		float strength = 0;
		RE::MagicSystem::CannotCastReason reason = RE::MagicSystem::CannotCastReason::kOK;
		caster->CheckCast(this->_spell, false, &strength, &reason, false);
		if (reason == RE::MagicSystem::CannotCastReason::kOK) {
			caster->CastSpellImmediate(this->_spell, false, target, 1.f, false, castMagnitude, selfTargeting ? nullptr : pc);
		}
	}
}

inline bool WheelItemSpell::isPower() const
{
	auto spellType = this->_spell->GetSpellType();
	return spellType == RE::MagicSystem::SpellType::kPower ||
		spellType == RE::MagicSystem::SpellType::kLesserPower ||
		spellType == RE::MagicSystem::SpellType::kVoicePower;
}

namespace TransformationDetection
{
	// Known vanilla transformation power FormIDs
	// Skyrim.esm: Beast Form (0x00092C48)
	// Dawnguard.esm: Vampire Lord (relative 0x0000283B, need to resolve with plugin index)
	constexpr RE::FormID kBeastFormBaseID = 0x00092C48;
	constexpr RE::FormID kVampireLordRelativeID = 0x0000283B;

	// Parse comma-separated hex FormIDs from config string
	std::vector<RE::FormID> ParseFormIDList(const std::string& str)
	{
		std::vector<RE::FormID> result;
		if (str.empty()) return result;

		std::stringstream ss(str);
		std::string token;
		while (std::getline(ss, token, ',')) {
			// Trim whitespace
			size_t start = token.find_first_not_of(" \t");
			size_t end = token.find_last_not_of(" \t");
			if (start == std::string::npos) continue;
			token = token.substr(start, end - start + 1);

			// Parse hex value
			try {
				RE::FormID id = static_cast<RE::FormID>(std::stoul(token, nullptr, 16));
				if (id != 0) result.push_back(id);
			} catch (...) {
				// Skip invalid entries
			}
		}
		return result;
	}

	// Check if EditorID or Name contains transformation-related keywords
	bool HasTransformKeywords(RE::SpellItem* spell)
	{
		if (!spell) return false;

		const char* edidC = spell->GetFormEditorID();
		const char* nameC = spell->GetName();
		std::string edid = edidC ? edidC : "";
		std::string name = nameC ? nameC : "";

		// Convert to lowercase for case-insensitive matching
		std::transform(edid.begin(), edid.end(), edid.begin(), ::tolower);
		std::transform(name.begin(), name.end(), name.begin(), ::tolower);

		// Werewolf/Beast Form keywords
		if (edid.find("beastform") != std::string::npos ||
			edid.find("werewolf") != std::string::npos ||
			name.find("beast form") != std::string::npos ||
			name.find("werewolf") != std::string::npos) {
			return true;
		}

		// Vampire Lord keywords
		if (edid.find("vampirelord") != std::string::npos ||
			edid.find("dlc1vampirechange") != std::string::npos ||
			edid.find("dlc1vampire") != std::string::npos ||
			name.find("vampire lord") != std::string::npos) {
			return true;
		}

		// Vampire Lord associated powers (Revert Form, Bats, etc.)
		if (edid.find("revertform") != std::string::npos ||
			name.find("revert form") != std::string::npos ||
			edid.find("dlc1bats") != std::string::npos ||
			name.find("bats") != std::string::npos) {
			return true;
		}

		// Werebear (Dragonborn DLC)
		if (edid.find("werebear") != std::string::npos ||
			name.find("werebear") != std::string::npos) {
			return true;
		}

		return false;
	}

	// Check if spell is a transformation power (Beast Form, Vampire Lord, Werebear, etc.)
	bool IsTransformationSpell(RE::SpellItem* spell)
	{
		if (!spell) return false;

		const bool debugLog = Config::WheelBehavior::InstantTransformationsDebugLog;
		const RE::FormID formID = spell->GetFormID();

		// 1. Check user deny list first (explicitly NOT transformations)
		auto denyList = ParseFormIDList(Config::WheelBehavior::TransformationDenyFormIDs);
		for (auto id : denyList) {
			if (formID == id) {
				if (debugLog) {
					logger::info("TransformDetect: '{}' ({:08X}) - DENIED by user DenyFormIDs list",
						spell->GetName(), formID);
				}
				return false;
			}
		}

		// 2. Check user allow list (explicitly transformations)
		auto allowList = ParseFormIDList(Config::WheelBehavior::TransformationAllowFormIDs);
		for (auto id : allowList) {
			if (formID == id) {
				if (debugLog) {
					logger::info("TransformDetect: '{}' ({:08X}) - MATCHED by user AllowFormIDs list",
						spell->GetName(), formID);
				}
				return true;
			}
		}

		// 3. Must be a power type (Greater Power, Lesser Power)
		auto spellType = spell->GetSpellType();
		bool isPowerType = (spellType == RE::MagicSystem::SpellType::kPower ||
							spellType == RE::MagicSystem::SpellType::kLesserPower);

		if (!isPowerType) {
			// Regular spells are never transformations
			return false;
		}

		// 4. Check known vanilla FormIDs
		// Beast Form from Skyrim.esm
		if ((formID & 0x00FFFFFF) == kBeastFormBaseID) {
			if (debugLog) {
				logger::info("TransformDetect: '{}' ({:08X}) - MATCHED as Beast Form (vanilla FormID)",
					spell->GetName(), formID);
			}
			return true;
		}

		// Vampire Lord from Dawnguard.esm (check relative ID)
		if ((formID & 0x00FFFFFF) == kVampireLordRelativeID) {
			if (debugLog) {
				logger::info("TransformDetect: '{}' ({:08X}) - MATCHED as Vampire Lord (vanilla FormID)",
					spell->GetName(), formID);
			}
			return true;
		}

		// 5. Heuristic: Check EditorID/Name for transformation keywords
		if (HasTransformKeywords(spell)) {
			if (debugLog) {
				logger::info("TransformDetect: '{}' ({:08X}) - MATCHED by keyword heuristic",
					spell->GetName(), formID);
			}
			return true;
		}

		if (debugLog) {
			logger::info("TransformDetect: '{}' ({:08X}) - NOT a transformation (spellType={})",
				spell->GetName(), formID, static_cast<int>(spellType));
		}
		return false;
	}
}

// Static wrapper for external callers (Wheeler.cpp)
bool WheelItemSpell::IsTransformationSpell(RE::SpellItem* spell)
{
	return TransformationDetection::IsTransformationSpell(spell);
}

bool WheelItemSpell::CastImmediate(bool allowNonInstant)
{
	return tryCastImmediate(allowNonInstant, RE::MagicSystem::CastingSource::kRightHand);
}

bool WheelItemSpell::CastImmediate(bool allowNonInstant, RE::MagicSystem::CastingSource castingSource)
{
	return tryCastImmediate(allowNonInstant, castingSource);
}

bool WheelItemSpell::tryCastImmediate(bool allowNonInstant, RE::MagicSystem::CastingSource castingSource)
{
	if (!_spell) {
		return false;
	}
	if (IsSpellBlockedByTransformGuard(_spell, "CastImmediate")) {
		return false;
	}
	if (TransformWheelManager::ShouldSuppressLichDirectCast(_spell, "CastImmediate")) {
		return false;
	}
	auto pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return false;
	}

	const auto castingType = _spell->GetCastingType();
	const bool isConcentration = castingType == RE::MagicSystem::CastingType::kConcentration;
	const bool concentrationAllowed = IsConcentrationInstantAllowed(_spell);
	const bool debugLog = Config::WheelBehavior::InstantSpellDebugLog;

	// Debug: log entry into function with key info
	if (debugLog) {
		logger::info("InstantCast: tryCastImmediate called for '{}' (castingType={}, allowNonInstant={}, isConcentration={}, concentrationAllowed={})",
			_spell->GetName(), static_cast<int>(castingType), allowNonInstant, isConcentration, concentrationAllowed);
		logger::info("InstantCast: Config values: InstantSpell={}, ConcentrationMode={}, MaxSeconds={:.2f}",
			Config::WheelBehavior::InstantSpell,
			Config::WheelBehavior::InstantSpellConcentrationMode,
			Config::WheelBehavior::InstantSpellConcentrationMaxSeconds);
	}

	// Check if this is a transformation spell - they have independent toggle
	const bool isTransformation = TransformationDetection::IsTransformationSpell(_spell);

	if (isTransformation) {
		// Transformation spells use InstantTransformations toggle (independent of InstantSpell)
		if (!Config::WheelBehavior::InstantTransformations) {
			if (debugLog || Config::WheelBehavior::InstantTransformationsDebugLog) {
				logger::info("InstantCast: BLOCKED '{}' - transformation spell (InstantTransformations=false)",
					_spell->GetName());
			}
			return false;  // Fall back to normal equip path
		}
		// InstantTransformations=ON, allow this transformation to instant cast
		if (debugLog || Config::WheelBehavior::InstantTransformationsDebugLog) {
			logger::info("InstantCast: ALLOWING '{}' - transformation spell (InstantTransformations=true)",
				_spell->GetName());
		}
	}

	// Default: only instant-cast spells flagged as instant, or powers.
	bool allowInstant = _spell->data.flags.any(RE::SpellItem::SpellFlag::kInstantCast) || this->isPower();

	if (debugLog) {
		logger::info("InstantCast: initial allowInstant={} (kInstantCast={}, isPower={}, isTransformation={}, isConcentration={}, concentrationAllowed={})",
			allowInstant,
			_spell->data.flags.any(RE::SpellItem::SpellFlag::kInstantCast),
			this->isPower(),
			isTransformation,
			isConcentration,
			concentrationAllowed);
	}

	if (!allowInstant && allowNonInstant) {
		// Block constant effects and scrolls (existing behavior)
		if (castingType == RE::MagicSystem::CastingType::kConstantEffect ||
		    castingType == RE::MagicSystem::CastingType::kScroll) {
			if (debugLog) {
				logger::info("InstantCast: BLOCKED '{}' - constant effect or scroll", _spell->GetName());
			}
			return false;
		}

		allowInstant = true;
	}

	// Universal concentration policy guard:
	// concentration spells can instant-cast only when concentration mode is Timed (1).
	if (allowInstant && !concentrationAllowed) {
		if (debugLog) {
			logger::info("InstantCast: BLOCKED '{}' - concentration spell with mode={} (concentrationAllowed={}), fallback to equip",
				_spell->GetName(),
				Config::WheelBehavior::InstantSpellConcentrationMode,
				concentrationAllowed);
		}
		return false;
	}

	if (!allowInstant) {
		// Caller will fall back to equip to preserve normal charge/aim behavior.
		if (debugLog) {
			logger::info("InstantCast: BLOCKED '{}' - allowInstant=false (not flagged as instant and allowNonInstant=false)", _spell->GetName());
		}
		return false;
	}

	// Prefer requested hand caster; fall back to instant caster.
	RE::MagicCaster* caster = pc->GetMagicCaster(castingSource);
	if (!caster) {
		caster = pc->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
		if (debugLog) {
			logger::info("InstantCast: caster for source={} unavailable, falling back to Instant", static_cast<int>(castingSource));
		}
	}
	if (!caster) {
		logger::debug("WheelBehavior Spell: no caster available");
		return false;
	}

	const bool selfTargeting = _spell->GetDelivery() == RE::MagicSystem::Delivery::kSelf;
	RE::TESObjectREFR* target = selfTargeting ? pc : pc->GetActorRuntimeData().currentCombatTarget.get().get();
	if (!target) {
		target = pc;
	}
	float strength = 0;
	RE::MagicSystem::CannotCastReason reason = RE::MagicSystem::CannotCastReason::kOK;
	caster->CheckCast(this->_spell, false, &strength, &reason, false);
	if (reason != RE::MagicSystem::CannotCastReason::kOK) {
		logger::debug("WheelBehavior Spell: cannot cast reason {}", static_cast<int>(reason));
		return false;
	}
	const float castMagnitude = _spell->GetCostliestEffectItem() ? _spell->GetCostliestEffectItem()->GetMagnitude() : 1.f;

	// Magicka handling for instant cast spells
	// NOTE: CastSpellImmediate does NOT deduct magicka automatically.
	// We manually deduct magicka before casting, and for summon spells we queue a deferred
	// refund check to handle placement failures (where the summon doesn't spawn).
	float actualConcentrationDuration = Config::WheelBehavior::InstantSpellConcentrationMaxSeconds;
	float magickaBefore = 0.0f;  // For summon refund check
	int effectCountBefore = 0;   // Active effect count before cast (for summon refund)
	bool isSummonSpell = Utils::Magic::IsSummonSpell(_spell);

	const float baseCost = _spell->CalculateMagickaCost(pc);
	if (baseCost > 0.0f) {
		const float currentMagicka = pc->AsActorValueOwner()->GetActorValue(RE::ActorValue::kMagicka);
		float magickaCost = baseCost;

		if (castingType == RE::MagicSystem::CastingType::kConcentration) {
			// Concentration spells (like Flames, Healing) - cost is per-second.
			const float maxDuration = Config::WheelBehavior::InstantSpellConcentrationMaxSeconds;
			const float minDuration = 0.1f;  // Minimum 0.1 seconds to cast

			// Calculate affordable duration: how many seconds we can sustain with current magicka
			float affordableDuration = currentMagicka / baseCost;

			// Clamp to max configured duration
			actualConcentrationDuration = (std::min)(affordableDuration, maxDuration);

			// If we can't afford even the minimum duration, block the spell
			if (actualConcentrationDuration < minDuration) {
				if (debugLog) {
					logger::info("InstantCast: BLOCKED '{}' - insufficient magicka ({:.1f} < {:.1f} for min {:.1f}s)",
						_spell->GetName(), currentMagicka, baseCost * minDuration, minDuration);
				}
				Utils::NotificationMessage(Texts::GetText(Texts::TextType::InsufficientMagickaForInstantCast));
				return false;
			}

			// Calculate actual cost based on affordable duration
			magickaCost = baseCost * actualConcentrationDuration;

			if (debugLog) {
				logger::info("InstantCast: Concentration spell '{}' - cost={:.1f}/s, available={:.1f}, affordable={:.2f}s (max={:.1f}s), deducting={:.1f}",
					_spell->GetName(), baseCost, currentMagicka, actualConcentrationDuration, maxDuration, magickaCost);
			}
		} else {
			// Fire-and-forget spells (like Firebolt, Summon) require full cost upfront
			if (currentMagicka < magickaCost) {
				if (debugLog) {
					logger::info("InstantCast: BLOCKED '{}' - insufficient magicka ({:.1f} < {:.1f})",
						_spell->GetName(), currentMagicka, magickaCost);
				}
				Utils::NotificationMessage(Texts::GetText(Texts::TextType::InsufficientMagickaForInstantCast));
				return false;
			}
			if (debugLog) {
				logger::info("InstantCast: Fire-and-forget spell '{}' - cost={:.1f}, available={:.1f}, isSummon={}",
					_spell->GetName(), baseCost, currentMagicka, isSummonSpell);
			}
		}

		// Snapshot state before deduction for summon refund check
		if (isSummonSpell) {
			magickaBefore = currentMagicka;
			effectCountBefore = Utils::Magic::CountActiveEffectsFromSpell(_spell->GetFormID());
		}

		// Deduct magicka
		if (magickaCost > 0.0f) {
			pc->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, -magickaCost);
			if (debugLog) {
				logger::info("InstantCast: Deducted {:.1f} magicka for '{}'", magickaCost, _spell->GetName());
			}
		}
	}

	caster->CastSpellImmediate(this->_spell, false, target, 1.f, false, castMagnitude, selfTargeting ? nullptr : pc);

	// For summon spells, queue a deferred refund check to handle placement failures
	if (isSummonSpell && magickaBefore > 0.0f) {
		Wheeler::QueueInstantCastRefundCheck(_spell->GetFormID(), magickaBefore, effectCountBefore);
	}

	// VERIFY CAST SUCCESS for concentration spells (immediate verification)
	if (castingType == RE::MagicSystem::CastingType::kConcentration && baseCost > 0.0f) {
		// Check if caster is now casting this spell
		if (!(caster->currentSpell && caster->currentSpell->GetFormID() == _spell->GetFormID())) {
			// Cast failed - refund the magicka we deducted
			float refundAmount = baseCost * actualConcentrationDuration;
			pc->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, refundAmount);
			if (debugLog) {
				logger::info("InstantCast: REFUNDED {:.1f} magicka - concentration spell '{}' failed to start", refundAmount, _spell->GetName());
			}
			return false;
		}
	}

	// For concentration spells in Timed mode, signal Wheeler to schedule auto-stop
	// Use the actual affordable duration calculated above
	if (castingType == RE::MagicSystem::CastingType::kConcentration &&
	    Config::WheelBehavior::InstantSpellConcentrationMode == 1) {
		// Wheeler will pick this up via a pending concentration stop request
		Wheeler::QueueConcentrationSpellStop(_spell->GetFormID(),
			castingSource,
			actualConcentrationDuration);
	}

	return true;
}

bool WheelItemSpell::HasCooldown() const
{
	return false; // Powers cooldown not surfaced (safety: avoid active effect iteration cost/compat issues)
}

float WheelItemSpell::GetCooldownRemainingSeconds() const
{
	return 0.0f;
}

float WheelItemSpell::GetCooldownTotalSeconds() const
{
	return 0.0f;
}

void WheelItemSpell::SerializeIntoJsonObj(nlohmann::json& a_json)
{
	a_json["type"] = WheelItemSpell::ITEM_TYPE_STR;
	a_json["formID"] = this->_spell->GetFormID();
}
