#include "bin/Rendering/Drawer.h"

#include "Wheel.h"
#include "Wheeler.h"
#include "WheelEntry.h"
#include "TransformWheelManager.h"
#include "WheelItems/WheelItem.h"
#include "WheelItems/WheelItemAmmo.h"
#include "WheelItems/WheelItemArmor.h"
#include "WheelItems/WheelItemShout.h"
#include "WheelItems/WheelItemAlchemy.h"
#include "WheelItems/WheelItemIngredient.h"
#include "WheelItems/WheelItemLight.h"
#include "WheelItems/WheelItemOStimAction.h"
#include "WheelItems/WheelItemSpell.h"
#include "WheelItems/WheelItemMissing.h"
#include "WheelItems/WheelItemWeapon.h"
#include "bin/Config.h"
#include "bin/Rendering/ResolutionScaleContext.h"
#include "bin/Rendering/TextureManager.h"
#include "MainWheelDebug.h"
#include "ShoutUtils.h"
#include "RE/E/ExtraUniqueID.h"
#include "RE/E/ExtraWorn.h"
#include "RE/E/ExtraWornLeft.h"
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
	constexpr const char* kInstantShoutStage1AssetPath =
		"Data/SKSE/Plugins/wheeler/resources/icons/instant_shout_indicator_stage1.svg";
	constexpr const char* kInstantShoutStage2AssetPath =
		"Data/SKSE/Plugins/wheeler/resources/icons/instant_shout_indicator_stage2.svg";
	constexpr const char* kInstantShoutStage3AssetPath =
		"Data/SKSE/Plugins/wheeler/resources/icons/instant_shout_indicator_stage3.svg";

	const char* TransformStateName(TransformState state)
	{
		switch (state) {
		case TransformState::Werewolf:
			return "Werewolf";
		case TransformState::VampireLord:
			return "VampireLord";
		case TransformState::Generic:
			return "Generic";
		case TransformState::Lich:
			return "Lich";
		case TransformState::Human:
		default:
			return "Human";
		}
	}

	bool ShouldBlockTransformedBaseWheelActivation(const std::shared_ptr<WheelItem>& item, int slotIdx)
	{
		const int activeWheelIdx = Wheeler::GetActiveWheelIndex();
		if (activeWheelIdx < 0 || TransformWheelManager::IsTransformWheelIndex(activeWheelIdx)) {
			return false;
		}

		const TransformState state = TransformWheelManager::GetPlayerTransformState();
		if (state != TransformState::Werewolf && state != TransformState::VampireLord) {
			return false;
		}

		const bool blockedCategory =
			std::dynamic_pointer_cast<WheelItemWeapon>(item) != nullptr ||
			std::dynamic_pointer_cast<WheelItemArmor>(item) != nullptr ||
			std::dynamic_pointer_cast<WheelItemAmmo>(item) != nullptr ||
			std::dynamic_pointer_cast<WheelItemOStimAction>(item) != nullptr ||
			std::dynamic_pointer_cast<WheelItemLight>(item) != nullptr;
		if (!blockedCategory) {
			return false;
		}

		const RE::FormID formId = item ? item->GetFormID() : 0;
		const char* itemType = item ? item->GetItemTypeName() : "null";
		logger::info(
			"TransformWheels: blocked base-wheel activation state={} wheel={} slotIdx={} formId={:08X} itemType='{}' reason=private_build_blocked_category",
			TransformStateName(state),
			activeWheelIdx,
			slotIdx,
			formId,
			itemType);
		return true;
	}

	bool ShouldActivateWheelItem(const std::shared_ptr<WheelItem>& item, int slotIdx)
	{
		if (ShouldBlockTransformedBaseWheelActivation(item, slotIdx)) {
			return false;
		}
		if (item && !item->RequiresRuntimeFormValidation()) {
			return true;
		}

		RE::FormID runtimeFormID = 0;
		const char* slotType = "null";
		if (item) {
			runtimeFormID = item->GetFormID();
			slotType = item->GetItemTypeName();
		}
		if (runtimeFormID == 0 || !RE::TESForm::LookupByID(runtimeFormID)) {
			if (Config::Debug::LogActivateRejects) {
				logger::info("Activate skipped reason=InvalidForm runtimeFormID={:08X} slotIdx={} slotType={}",
					runtimeFormID, slotIdx, slotType);
			}
			return false;
		}
		return true;
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

	bool CopyExtraListsSafe(RE::BSSimpleList<RE::ExtraDataList*>* a_extraLists, std::vector<RE::ExtraDataList*>& a_out)
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
		if (!a_list) {
			return nullptr;
		}
		if (!InvokeWithSehGuard([&]() { result = a_list->GetByType<T>(); })) {
			return nullptr;
		}
		return result;
	}

	bool IsTwoHandedWeaponFormID(RE::FormID a_formID)
	{
		auto* weapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(a_formID);
		if (!weapon) {
			return false;
		}

		const auto weaponType = weapon->GetWeaponType();
		return weapon->IsBow() ||
		       weapon->IsCrossbow() ||
		       weaponType == RE::WEAPON_TYPE::kTwoHandSword ||
		       weaponType == RE::WEAPON_TYPE::kTwoHandAxe;
	}

	std::uint64_t ResolveEquippedHandSignature(RE::Actor* a_actor, bool a_leftHand, RE::FormID a_expectedFormID)
	{
		if (!a_actor || a_expectedFormID == 0) {
			return 0;
		}

		RE::InventoryEntryData* entry = a_actor->GetEquippedEntryData(a_leftHand);
		if (!entry || !entry->object || entry->object->GetFormID() != a_expectedFormID || !entry->extraLists) {
			return 0;
		}

		std::vector<RE::ExtraDataList*> extraListSnapshot;
		if (!CopyExtraListsSafe(entry->extraLists, extraListSnapshot)) {
			return 0;
		}

		std::uint64_t fallbackSignature = 0;
		int uniqueCandidateCount = 0;
		bool foundRequestedHandWornList = false;
		for (auto* extraList : extraListSnapshot) {
			if (!extraList) {
				continue;
			}

			bool hasWorn = false;
			bool hasWornLeft = false;
			const bool readWorn = TryHasTypeSafe(extraList, RE::ExtraDataType::kWorn, hasWorn);
			const bool readWornLeft = TryHasTypeSafe(extraList, RE::ExtraDataType::kWornLeft, hasWornLeft);
			if (!readWorn || !readWornLeft) {
				continue;
			}

			const bool matchesRequestedHand = a_leftHand ? hasWornLeft : (hasWorn && !hasWornLeft);
			if (matchesRequestedHand) {
				foundRequestedHandWornList = true;
			}

			bool hasUniqueID = false;
			if (!TryHasTypeSafe(extraList, RE::ExtraDataType::kUniqueID, hasUniqueID) || !hasUniqueID) {
				continue;
			}

			auto* uniqueData = GetByTypeSafe<RE::ExtraUniqueID>(extraList);
			if (!uniqueData || uniqueData->uniqueID == 0) {
				continue;
			}

			++uniqueCandidateCount;
			if (fallbackSignature == 0) {
				fallbackSignature = uniqueData->uniqueID;
			}

			if (matchesRequestedHand) {
				return uniqueData->uniqueID;
			}
		}

		if (foundRequestedHandWornList) {
			return 0;
		}

		if (uniqueCandidateCount == 1) {
			return fallbackSignature;
		}

		return 0;
	}
}

Wheel::Wheel() {}
Wheel::~Wheel() 
{
    this->Clear();
}
void Wheel::Draw(ImVec2 a_wheelCenter, ImVec2 a_cursorPos, float a_cursorAngle, bool a_cursorCentered, RE::TESObjectREFR::InventoryItemMap& a_imap,
	DrawArgs a_drawArgs, float a_hoveredEntryTimeSeconds, float a_hoverActivateDelaySeconds, float a_deltaTimeSeconds)
{
	// CRITICAL: Acquire shared lock for entire draw operation to prevent clear-during-draw crashes.
	// The lock protects against concurrent modifications to _entries (e.g., Wheel::Clear, inventory prune)
	// which could cause null/dangling pointer access at WheelEntry::_lock (offset +0x8).
	std::shared_lock<std::shared_mutex> drawLock(_lock);
	
	try {
		using namespace Config::Styling::Wheel;

		EquippedHandsCache handsCache{};
		if (Config::MainWheel::ShowHandIndicator) {
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				if (auto* rhs = player->GetEquippedObject(false)) {
					handsCache.rightFormID = rhs->GetFormID();
					handsCache.rightSignature = ResolveEquippedHandSignature(player, false, handsCache.rightFormID);
				}
				if (auto* lhs = player->GetEquippedObject(true)) {
					handsCache.leftFormID = lhs->GetFormID();
					handsCache.leftSignature = ResolveEquippedHandSignature(player, true, handsCache.leftFormID);
				}

				const auto mirrorTwoHandedState = [&](bool a_fromRight) {
					RE::FormID& sourceFormID = a_fromRight ? handsCache.rightFormID : handsCache.leftFormID;
					RE::FormID& targetFormID = a_fromRight ? handsCache.leftFormID : handsCache.rightFormID;
					std::uint64_t& sourceSignature = a_fromRight ? handsCache.rightSignature : handsCache.leftSignature;
					std::uint64_t& targetSignature = a_fromRight ? handsCache.leftSignature : handsCache.rightSignature;

					if (sourceFormID == 0 || !IsTwoHandedWeaponFormID(sourceFormID)) {
						return;
					}

					if (targetFormID == 0) {
						targetFormID = sourceFormID;
					}
					if (targetFormID == sourceFormID && targetSignature == 0 && sourceSignature != 0) {
						targetSignature = sourceSignature;
					}
				};

				mirrorTwoHandedState(true);
				mirrorTwoHandedState(false);
			}
		}

		// Draw background
		if (!UseGeometricPrimitiveForBackgroundTexture) {
			Texture::Image backgroundTexture = Texture::GetIconImage(Texture::icon_image_type::wheel_background);
			if (backgroundTexture.texture) {
				auto& resolutionContext = ResolutionScale::Context::GetSingleton();
				const auto& state = resolutionContext.GetState();
				float gameHeight = state.gameH;
				if (gameHeight <= 0.0f) {
					gameHeight = resolutionContext.GetRenderSize().y;
				}
				if (gameHeight <= 0.0f) {
					gameHeight = static_cast<float>(REFERENCE_HEIGHT);
				}
				const float normalize = static_cast<float>(REFERENCE_HEIGHT) / gameHeight;
				const float effectiveScale = WheelBackgroundTextureScale * normalize;
				static float lastLoggedGameHeight = 0.0f;
				if (std::fabs(gameHeight - lastLoggedGameHeight) > 0.5f) {
					logger::info("[ResolutionFix] Wheel background scale: cfg={:.3f}, normalize={:.3f}, effective={:.3f}, gameH={:.0f}",
						WheelBackgroundTextureScale, normalize, effectiveScale, gameHeight);
					lastLoggedGameHeight = gameHeight;
				}
				Drawer::draw_texture(
					backgroundTexture.texture,
					a_wheelCenter,
					0, 0,
					ImVec2(
						backgroundTexture.width * effectiveScale,
						backgroundTexture.height * effectiveScale),
					C_SKYRIMWHITE,
					a_drawArgs);
			} else {
				MainWheelDebug::LogRateLimited(MainWheelDebug::Category::Assets, "wheel_background_missing",
					"Wheel background texture missing (wheel_background)");
			}
		}

		if (this->IsEmpty()) {
			Drawer::draw_text(a_wheelCenter.x, a_wheelCenter.y, "Empty Wheel", C_SKYRIMWHITE, 40.f, a_drawArgs);
			return;  // nothing more to draw
		}
		float cursorIndicatorAngle = a_cursorAngle;

		const bool isMouseInput = !Wheeler::IsLastInputGamepad();
		const bool mouseStabilizeEnabled = isMouseInput && Config::MainWheel::MouseStabilization::Enabled;
		const float cursorRadiusRaw = std::sqrt(a_cursorPos.x * a_cursorPos.x + a_cursorPos.y * a_cursorPos.y);
		// Legacy behavior: clear hovered entry when cursor is centered.
		if (!mouseStabilizeEnabled && a_cursorCentered) {
			HoverActivationSnapshotPolicy::Store(_hoveredEntryIdx, -1);
		}

		// draw entries
		struct EntryRuntimeData
		{
			ImVec2 center{};
			bool hovered = false;
			bool slotOnRightSide = false;
		};
		std::vector<EntryRuntimeData> entryRuntimeDataVec;
		const int numEntries = static_cast<int>(_entries.size());
		const float entryArcSpan = 2.0f * IM_PI / numEntries;
		const float innerSpacingRadRaw = InnerSpacing / InnerCircleRadius / 2;
		// Keep visual spacing stable, but clamp to avoid inverted slice geometry at high slot counts.
		const float innerSpacingRadDraw = std::clamp(innerSpacingRadRaw, 0.0f, entryArcSpan * 0.49f);
		
		// ========== HOVER SELECTION (LEGACY + GUARD RAILS) ==========
		// Hover pipeline:
		// input (cursor pos/angle, dt) -> r/speed/motion hint
		// -> candidate index from angle
		// -> mouse guard rails (center hold, jump guard, boundary margin)
		// -> gamepad hysteresis (gamepad only)
		// -> commit to _hoveredEntryIdx
		auto angleInRange = [](float angle, float minAngle, float maxAngle) {
			if (angle >= minAngle) {
				return angle < maxAngle;
			}
			return angle + 2 * IM_PI < maxAngle && angle + 2 * IM_PI >= minAngle;
		};
		auto wrapAnglePi = [](float angle) {
			while (angle > IM_PI) {
				angle -= 2.0f * IM_PI;
			}
			while (angle < -IM_PI) {
				angle += 2.0f * IM_PI;
			}
			return angle;
		};
		
		// Draw angles keep visual spacing gaps; hit angles cover full slices to avoid hover dead zones.
		struct EntryAngleInfo {
			float innerMin, innerMax, outerMin, outerMax, centerAngle;
		};
		std::vector<EntryAngleInfo> entryAnglesDraw(numEntries);
		std::vector<EntryAngleInfo> entryAnglesHit(numEntries);
		for (int i = 0; i < numEntries; ++i) {
			entryAnglesDraw[i].innerMin = entryArcSpan * (i - 0.5f) + innerSpacingRadDraw + IM_PI / 2;
			entryAnglesDraw[i].innerMax = entryArcSpan * (i + 0.5f) - innerSpacingRadDraw + IM_PI / 2;
			entryAnglesDraw[i].outerMin = entryArcSpan * (i - 0.5f) + innerSpacingRadDraw * (InnerCircleRadius / OuterCircleRadius) + IM_PI / 2;
			entryAnglesDraw[i].outerMax = entryArcSpan * (i + 0.5f) - innerSpacingRadDraw * (InnerCircleRadius / OuterCircleRadius) + IM_PI / 2;
			entryAnglesDraw[i].centerAngle = (entryAnglesDraw[i].innerMin + entryAnglesDraw[i].innerMax) / 2.0f;
			if (entryAnglesDraw[i].innerMax > IM_PI * 2) {
				entryAnglesDraw[i].innerMin -= IM_PI * 2;
				entryAnglesDraw[i].innerMax -= IM_PI * 2;
				entryAnglesDraw[i].centerAngle -= IM_PI * 2;
			}
			if (entryAnglesDraw[i].outerMax > IM_PI * 2) {
				entryAnglesDraw[i].outerMin -= IM_PI * 2;
				entryAnglesDraw[i].outerMax -= IM_PI * 2;
			}

			entryAnglesHit[i].innerMin = entryArcSpan * (i - 0.5f) + IM_PI / 2;
			entryAnglesHit[i].innerMax = entryArcSpan * (i + 0.5f) + IM_PI / 2;
			entryAnglesHit[i].outerMin = entryAnglesHit[i].innerMin;
			entryAnglesHit[i].outerMax = entryAnglesHit[i].innerMax;
			entryAnglesHit[i].centerAngle = (entryAnglesHit[i].innerMin + entryAnglesHit[i].innerMax) / 2.0f;
			if (entryAnglesHit[i].innerMax > IM_PI * 2) {
				entryAnglesHit[i].innerMin -= IM_PI * 2;
				entryAnglesHit[i].innerMax -= IM_PI * 2;
				entryAnglesHit[i].centerAngle -= IM_PI * 2;
			}
			if (entryAnglesHit[i].outerMax > IM_PI * 2) {
				entryAnglesHit[i].outerMin -= IM_PI * 2;
				entryAnglesHit[i].outerMax -= IM_PI * 2;
			}
		}
		const float cursorRadiusMax = Config::Control::Wheel::CursorRadiusPerEntry * numEntries;
		const float cursorRadiusNorm = (cursorRadiusMax > 1e-4f) ? (cursorRadiusRaw / cursorRadiusMax) : 0.0f;
		const bool cursorInsideWheel = (cursorRadiusMax > 1e-4f) ? (cursorRadiusRaw <= cursorRadiusMax * 1.05f) : false;
		if (mouseStabilizeEnabled && !_mouseHoverState.loggedStabilizationConfigThisOpen) {
			MainWheelDebug::Log(MainWheelDebug::Category::Config,
				"MouseStabilization enabled=1 centerHoldR={:.3f} jumpGuardR={:.3f} maxJumpSlots={} stepToward={} "
				"boundaryMarginDeg={:.2f} lowSpeedOnly={} lowSpeedThresh={:.3f} motionHint={} motionHintSpeed={:.3f} motionHintBlend={:.2f} "
				"cursorRadiusPerEntry={:.2f} entries={} cursorRadiusMax={:.2f}",
				Config::MainWheel::MouseStabilization::CenterHoldRadius,
				Config::MainWheel::MouseStabilization::JumpGuardRadius,
				Config::MainWheel::MouseStabilization::MaxJumpSlots,
				Config::MainWheel::MouseStabilization::StepTowardEnabled ? 1 : 0,
				Config::MainWheel::MouseStabilization::BoundaryMarginDeg,
				Config::MainWheel::MouseStabilization::BoundaryMarginLowSpeedOnly ? 1 : 0,
				Config::MainWheel::MouseStabilization::LowSpeedThreshold,
				Config::MainWheel::MouseStabilization::MotionHinting::Enabled ? 1 : 0,
				Config::MainWheel::MouseStabilization::MotionHinting::SpeedThreshold,
				Config::MainWheel::MouseStabilization::MotionHinting::Blend,
				Config::Control::Wheel::CursorRadiusPerEntry,
				numEntries,
				cursorRadiusMax);
			_mouseHoverState.loggedStabilizationConfigThisOpen = true;
		} else if (!mouseStabilizeEnabled) {
			_mouseHoverState.loggedStabilizationConfigThisOpen = false;
		}
		const bool centerHoldActive = mouseStabilizeEnabled &&
			Config::MainWheel::MouseStabilization::CenterHoldRadius > 0.0f &&
			cursorRadiusNorm <= Config::MainWheel::MouseStabilization::CenterHoldRadius;
		// Center can act as a neutral rest zone (no hovered slot) to avoid instant opposite-slot flips.
		const bool centerRestActive = a_cursorCentered || centerHoldActive;

		const int lastIdx = HoverActivationSnapshotPolicy::Load(_hoveredEntryIdx);
		const bool validLast = (lastIdx >= 0 && lastIdx < numEntries);

		ImVec2 velocity{ 0.0f, 0.0f };
		float speed = 0.0f;
		float speedNorm = 0.0f;
		if (isMouseInput && a_deltaTimeSeconds > 1e-4f) {
			if (!_mouseHoverState.initialized) {
				_mouseHoverState.lastCursorPos = a_cursorPos;
				_mouseHoverState.filteredVelocity = { 0.0f, 0.0f };
				_mouseHoverState.initialized = true;
			} else {
				const float dx = a_cursorPos.x - _mouseHoverState.lastCursorPos.x;
				const float dy = a_cursorPos.y - _mouseHoverState.lastCursorPos.y;
				_mouseHoverState.lastCursorPos = a_cursorPos;
				const float invDt = 1.0f / a_deltaTimeSeconds;
				const ImVec2 rawVel{ dx * invDt, dy * invDt };
				const float halfLifeMs = (std::max)(1.0f, Config::MainWheel::Mouse::VelocityHalfLifeMs);
				const float halfLifeSec = halfLifeMs / 1000.0f;
				const float alpha = 1.0f - std::pow(0.5f, a_deltaTimeSeconds / halfLifeSec);
				_mouseHoverState.filteredVelocity.x += (rawVel.x - _mouseHoverState.filteredVelocity.x) * alpha;
				_mouseHoverState.filteredVelocity.y += (rawVel.y - _mouseHoverState.filteredVelocity.y) * alpha;
			}
			velocity = _mouseHoverState.filteredVelocity;
			speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
		}
		speedNorm = (cursorRadiusMax > 1e-4f) ? (speed / cursorRadiusMax) : 0.0f;

		float effectiveAngle = a_cursorAngle;
		bool motionHintActive = false;
		if (mouseStabilizeEnabled && Config::MainWheel::MouseStabilization::MotionHinting::Enabled) {
			const float speedThreshold = Config::MainWheel::MouseStabilization::MotionHinting::SpeedThreshold;
			if (speedThreshold > 0.0f && speedNorm >= speedThreshold && speed > 1e-4f) {
				const float velAngle = std::atan2(velocity.y, velocity.x);
				const float blend = std::clamp(Config::MainWheel::MouseStabilization::MotionHinting::Blend, 0.0f, 1.0f);
				const float delta = wrapAnglePi(velAngle - effectiveAngle);
				effectiveAngle = wrapAnglePi(effectiveAngle + delta * blend);
				motionHintActive = true;
			}
		}
		if (motionHintActive && Config::MainWheel::Mouse::DebugHoverLog) {
			MainWheelDebug::LogRateLimited(MainWheelDebug::Category::MouseHover, "motion_hint",
				"MOTION_HINT speed={:.2f} blend={:.2f} angle={:.1f}deg",
				speedNorm,
				Config::MainWheel::MouseStabilization::MotionHinting::Blend,
				effectiveAngle * (180.0f / IM_PI));
		}

		// Phase A: Find candidate index from angle (raw or motion-hinted)
		int candidateIdx = -1;
		if (cursorInsideWheel) {
			for (int i = 0; i < numEntries; ++i) {
				if (angleInRange(effectiveAngle, entryAnglesHit[i].innerMin, entryAnglesHit[i].innerMax)) {
					candidateIdx = i;
					break;
				}
			}
		}
		if (centerRestActive) {
			candidateIdx = -1;
		}
		if (candidateIdx < 0 && cursorInsideWheel && !centerRestActive) {
			float bestDelta = IM_PI + 1.0f;
			for (int i = 0; i < numEntries; ++i) {
				const float delta = std::abs(wrapAnglePi(effectiveAngle - entryAnglesHit[i].centerAngle));
				if (delta < bestDelta) {
					bestDelta = delta;
					candidateIdx = i;
				}
			}
		}
		const bool haveCandidate = candidateIdx >= 0;
		
		// Phase B: Apply gamepad hysteresis or mouse guard rails, then commit final index
		int finalIdx = lastIdx;
		const char* guardReason = "KEEP_PREV";
		int deltaSlots = -1;
		const bool rtuEnabledLocal = Config::WheelBehavior::ReleaseToUse;
		bool committedThisFrame = false;

		auto circularDelta = [&](int from, int to) {
			if (from < 0 || to < 0) {
				return -1;
			}
			int delta = std::abs(to - from);
			if (delta > numEntries / 2) {
				delta = numEntries - delta;
			}
			return delta;
		};
		auto stepToward = [&](int from, int to) {
			if (from < 0 || to < 0 || numEntries <= 0) {
				return from;
			}
			const int cw = (to - from + numEntries) % numEntries;
			const int ccw = (from - to + numEntries) % numEntries;
			if (cw <= ccw) {
				return (from + 1) % numEntries;
			}
			return (from - 1 + numEntries) % numEntries;
		};
		
		if (!isMouseInput) {
			if (centerRestActive) {
				finalIdx = -1;
				guardReason = "CENTER_REST";
				HoverActivationSnapshotPolicy::Store(_hoveredEntryIdx, finalIdx);
				committedThisFrame = true;
			} else {
				finalIdx = candidateIdx;
				// With gamepad auto-center snap OFF, block large cross-center jumps near the hub.
				// Small inward movement should enter center rest, while strong outward movement can still switch slots.
				if (!Config::WheelBehavior::Gamepad::Nav::AutoCenterRestSnap &&
					validLast && candidateIdx >= 0 && candidateIdx != lastIdx) {
					deltaSlots = circularDelta(lastIdx, candidateIdx);
					float nearCenterRadius = 0.20f;
					if (Config::WheelBehavior::Gamepad::Nav::HasInnerDeadzone) {
						nearCenterRadius =
							std::clamp(Config::WheelBehavior::Gamepad::Nav::InnerDeadzone * 1.5f, 0.14f, 0.32f);
					}
					if (deltaSlots > 1 && cursorRadiusNorm <= nearCenterRadius) {
						finalIdx = -1;
						guardReason = "CENTER_REST_GPAD";
					}
				}
				const bool useGamepadHysteresis = Config::WheelBehavior::Gamepad::Nav::HasHysteresisDegrees;
				if (useGamepadHysteresis && finalIdx == candidateIdx &&
					validLast && candidateIdx >= 0 && candidateIdx != lastIdx) {
					float hysteresisRad = Config::WheelBehavior::Gamepad::Nav::HysteresisDegrees * (IM_PI / 180.0f);
					// Keep at least half of each slice reachable; otherwise high slot counts can
					// shrink the switch band to nearly zero and make gamepad hover feel impossible.
					const float maxHysteresis = entryArcSpan * 0.25f;
					hysteresisRad = std::clamp(hysteresisRad, 0.0f, maxHysteresis);
					if (hysteresisRad > 0.0f) {
						const float minHyst = entryAnglesHit[candidateIdx].innerMin + hysteresisRad;
						const float maxHyst = entryAnglesHit[candidateIdx].innerMax - hysteresisRad;
						if (!angleInRange(a_cursorAngle, minHyst, maxHyst)) {
							finalIdx = lastIdx;  // Keep last due to hysteresis
						}
					}
				}
				HoverActivationSnapshotPolicy::Store(_hoveredEntryIdx, finalIdx);
				committedThisFrame = true;
			}
		} else if (!mouseStabilizeEnabled) {
			if (centerRestActive) {
				finalIdx = -1;
				HoverActivationSnapshotPolicy::Store(_hoveredEntryIdx, finalIdx);
				committedThisFrame = true;
			} else {
				finalIdx = candidateIdx;
				HoverActivationSnapshotPolicy::Store(_hoveredEntryIdx, finalIdx);
				committedThisFrame = true;
			}
		} else {
			if (Config::MainWheel::Mouse::DebugHoverLog &&
				cursorInsideWheel && lastIdx < 0 && _mouseHoverState.hadValidHoverThisOpen) {
				MainWheelDebug::LogRateLimited(MainWheelDebug::Category::MouseHover, "hover_reset",
					"ERROR prev=-1 insideWheel=1 r={:.2f} cand={} lastCommitted={}",
					cursorRadiusNorm, candidateIdx, _mouseHoverState.lastCommittedIdx);
			}

			if (centerRestActive) {
				finalIdx = -1;
				guardReason = "CENTER_REST";
			} else if (!cursorInsideWheel) {
				finalIdx = -1;
				guardReason = "OUTSIDE_WHEEL";
			} else if (!validLast) {
				if (haveCandidate && !centerHoldActive) {
					finalIdx = candidateIdx;
					guardReason = "INIT_SELECT";
				} else {
					finalIdx = -1;
					guardReason = "INIT_NO_CAND";
				}
			} else {
				if (!haveCandidate) {
					finalIdx = lastIdx;
					guardReason = "NO_CANDIDATE_KEEP";
				} else {
					const float jumpGuardRadius = Config::MainWheel::MouseStabilization::JumpGuardRadius;
					const int maxJumpSlots = (std::max)(0, Config::MainWheel::MouseStabilization::MaxJumpSlots);
					deltaSlots = circularDelta(lastIdx, candidateIdx);
					if (deltaSlots > maxJumpSlots) {
						if (jumpGuardRadius > 0.0f && cursorRadiusNorm <= jumpGuardRadius) {
							finalIdx = lastIdx;
							guardReason = "JUMP_GUARD_BLOCK";
						} else if (Config::MainWheel::MouseStabilization::StepTowardEnabled) {
							finalIdx = stepToward(lastIdx, candidateIdx);
							guardReason = "JUMP_GUARD_STEP";
						} else {
							finalIdx = candidateIdx;
							guardReason = "OK";
						}
					} else {
						finalIdx = candidateIdx;
						guardReason = "OK";
					}

					if (finalIdx == candidateIdx && candidateIdx != lastIdx) {
						const float boundaryMarginDeg = Config::MainWheel::MouseStabilization::BoundaryMarginDeg;
						const float maxMargin = entryArcSpan * 0.49f;
						float marginRad = boundaryMarginDeg * (IM_PI / 180.0f);
						marginRad = std::clamp(marginRad, 0.0f, maxMargin);
						const bool lowSpeedOnly = Config::MainWheel::MouseStabilization::BoundaryMarginLowSpeedOnly;
						const bool lowSpeed = speedNorm <= Config::MainWheel::MouseStabilization::LowSpeedThreshold;
						if (marginRad > 0.0f && (!lowSpeedOnly || lowSpeed)) {
							const float minHyst = entryAnglesHit[candidateIdx].innerMin + marginRad;
							const float maxHyst = entryAnglesHit[candidateIdx].innerMax - marginRad;
							if (!angleInRange(effectiveAngle, minHyst, maxHyst)) {
								finalIdx = lastIdx;
								guardReason = "BOUNDARY_HOLD";
							}
						}
					}
				}
			}

			HoverActivationSnapshotPolicy::Store(_hoveredEntryIdx, finalIdx);
			committedThisFrame = true;

			if (finalIdx >= 0) {
				_mouseHoverState.hadValidHoverThisOpen = true;
				_mouseHoverState.lastCommittedIdx = finalIdx;
			} else {
				_mouseHoverState.hadValidHoverThisOpen = false;
			}

			if (Config::MainWheel::Mouse::DebugHoverLog && finalIdx < 0 && lastIdx >= 0) {
				MainWheelDebug::LogRateLimited(MainWheelDebug::Category::MouseHover, "hover_clear",
					"MouseHoverClear hoveredIdx->-1 prev={} cand={} r={:.2f} insideWheel={} reason={}",
					lastIdx, candidateIdx, cursorRadiusNorm, cursorInsideWheel ? 1 : 0, guardReason);
			}
		}

		const int committedHoverIdx = HoverActivationSnapshotPolicy::Load(_hoveredEntryIdx);
		if (mouseStabilizeEnabled && Config::MainWheel::Mouse::DebugHoverLog &&
			committedThisFrame && committedHoverIdx != finalIdx) {
			MainWheelDebug::LogRateLimited(MainWheelDebug::Category::MouseHover, "hover_mismatch",
				"MouseHoverFinalMismatch prev={} cand={} final={} actual={} reason={}",
				lastIdx, candidateIdx, finalIdx, committedHoverIdx, guardReason);
		}

		if (Config::MainWheel::Mouse::DrawDebugOverlay) {
			_mouseHoverState.debug.valid = true;
			_mouseHoverState.debug.useNewModel = mouseStabilizeEnabled;
			_mouseHoverState.debug.rNorm = cursorRadiusNorm;
			_mouseHoverState.debug.speed = speedNorm;
			_mouseHoverState.debug.speedNorm = speedNorm;
			_mouseHoverState.debug.rawTheta = a_cursorAngle;
			_mouseHoverState.debug.stableTheta = effectiveAngle;
			_mouseHoverState.debug.currentIdx = lastIdx;
			_mouseHoverState.debug.bestIdx = candidateIdx;
			_mouseHoverState.debug.secondIdx = finalIdx;
			_mouseHoverState.debug.intentIdx = -1;
			_mouseHoverState.debug.intentActive = false;
			_mouseHoverState.debug.bestScore = 0.0f;
			_mouseHoverState.debug.secondScore = 0.0f;
			const char* reason = guardReason;
			if (std::strcmp(reason, "OK") == 0 && motionHintActive) {
				reason = "MOTION_HINT";
			}
			std::snprintf(_mouseHoverState.debug.reason.data(), _mouseHoverState.debug.reason.size(), "%s", reason);
		} else {
			_mouseHoverState.debug.valid = false;
		}

		if (mouseStabilizeEnabled && Config::MainWheel::Mouse::DebugHoverLog) {
			if (deltaSlots < 0 && validLast && candidateIdx >= 0) {
				deltaSlots = circularDelta(lastIdx, candidateIdx);
			}
			MainWheelDebug::LogRateLimited(MainWheelDebug::Category::MouseHover, "stabilize",
				"r={:.2f} prev={} cand={} final={} delta={} reason={} rtu={}",
				cursorRadiusNorm, lastIdx, candidateIdx, finalIdx, deltaSlots, guardReason, rtuEnabledLocal ? 1 : 0);
		}
		
		const bool suppressHoverVisuals = centerRestActive;
		const int hoverForDraw = HoverActivationSnapshotPolicy::Load(_hoveredEntryIdx);

		// draw background, cache data for foreground
		for (int entryIdx = 0; entryIdx < numEntries; entryIdx++) {
			const auto& angles = entryAnglesDraw[entryIdx];
			bool hovered = (!suppressHoverVisuals && hoverForDraw == entryIdx);

			// calculate wheel center
			float t1 = (OuterCircleRadius - InnerCircleRadius) / 2;
			float t2 = InnerCircleRadius + t1;
			float rad = (angles.innerMax - angles.innerMin) / 2 + angles.innerMin;
			ImVec2 entryCenter = ImVec2(
				a_wheelCenter.x + t2 * cosf(rad),
				a_wheelCenter.y + t2 * sinf(rad));
			// Side/mirror placement must be based on the stable, non-animated slot center.
			const bool slotOnRightSide = entryCenter.x >= a_wheelCenter.x;

			if (hovered) {
				if (Config::Animation::SnappyCursorIndicator) {  // update cursor indicator angle to point to item center
					cursorIndicatorAngle = rad;
				}
			}

			int numArcSegments = (int)(256 * entryArcSpan / (2 * IM_PI)) + 1;

			std::unique_ptr<WheelEntry>& wheelEntry = _entries[entryIdx];
			
			// SAFETY: Null check to prevent crash if entry was cleared during iteration
			if (!wheelEntry) {
				logger::warn("[Wheel::Draw] Null entry at index {} during background pass (entries={})", entryIdx, numEntries);
				continue;
			}

			// Update interpolators in entry
			wheelEntry->UpdateAnimation(a_imap, innerSpacingRadDraw,
				angles.innerMin, angles.innerMax, 
				angles.outerMin, angles.outerMax, hovered);
			
			// offset entry center position
			const float radiusMod = wheelEntry->GetRadiusMod();
			entryCenter.x += radiusMod * cosf(rad);
			entryCenter.y += radiusMod * sinf(rad);

			// draw background first
			wheelEntry->DrawBackGround(a_wheelCenter, entryCenter,
				innerSpacingRadDraw,
				angles.innerMin, angles.innerMax,
				angles.outerMin, angles.outerMax, hovered, numArcSegments, a_imap, a_drawArgs);

			// prepare for foreground drawing
			entryRuntimeDataVec.push_back(EntryRuntimeData{ entryCenter, hovered, slotOnRightSide });
		}

		// draw foreground in a separate pass to avoid overlapping
		for (int entryIdx = 0; entryIdx < entryRuntimeDataVec.size(); entryIdx++) {
			// SAFETY: Bounds check + null check to prevent crash during closing state
			if (entryIdx >= static_cast<int>(_entries.size())) {
				logger::warn("[Wheel::Draw] Index {} out of bounds during foreground pass (entries={})", 
					entryIdx, static_cast<int>(_entries.size()));
				break;
			}
			if (!_entries[entryIdx]) {
				logger::warn("[Wheel::Draw] Null entry at index {} during foreground pass", entryIdx);
				continue;
			}
			const auto& runtime = entryRuntimeDataVec[entryIdx];
			_entries[entryIdx]->DrawSlotAndHighlight(a_wheelCenter, runtime.center, runtime.slotOnRightSide, runtime.hovered, a_imap, a_drawArgs, handsCache);
		}

		// Activation progress indicator (drawn as an external border sweep; avoids darkening the slot interior).
		// For shouts with InstantShout enabled: use a 3-stage indicator showing word level thresholds.
		// For other items: use the generic single-progress indicator.
		// Draw when: RTU is ON, OR ShowIndicatorWhenRTUOff is enabled (allows indicator without RTU).
		// EXCEPTION: InstantShout indicators can draw when ShowIndicatorWhenRTUOff is ON, even if Activation Indicator is OFF,
		//            but ONLY during hold mode (handled inside the loop).
		const bool rtuEnabled = Config::WheelBehavior::ReleaseToUse;
		const bool allowWhenRTUOff = Config::WheelBehavior::ShowIndicatorWhenRTUOff;
		const bool normalIndicatorEnabled = Config::Styling::HoverDelay::Enabled && (rtuEnabled || allowWhenRTUOff);
		const bool instantShoutIndicatorOverride = Config::WheelBehavior::InstantShout && allowWhenRTUOff;
		const bool shouldDrawIndicator = normalIndicatorEnabled || instantShoutIndicatorOverride;
		if (shouldDrawIndicator) {
			Texture::Image slotBg = Texture::GetIconImage(Texture::icon_image_type::slot_background);
			const std::vector<ImVec2>* slotOutline = Texture::GetSlotBackgroundOutline();

			const float scale = Config::Styling::Item::Slot::BackgroundTexture::Scale;
			const ImVec2 size(slotBg.width * scale, slotBg.height * scale);

			ImDrawList* drawList = ImGui::GetWindowDrawList();

			ImVec4 bg = ImGui::ColorConvertU32ToFloat4(Config::Styling::HoverDelay::BackgroundColor);
			bg.w *= a_drawArgs.alphaMult;
			const ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(bg);
			ImVec4 fg = ImGui::ColorConvertU32ToFloat4(Config::Styling::HoverDelay::Color);
			fg.w *= a_drawArgs.alphaMult;
			const ImU32 fgCol = ImGui::ColorConvertFloat4ToU32(fg);

			const float thickness = (std::max)(0.5f, Config::Styling::HoverDelay::Thickness);
			MainWheelDebug::LogRateLimited(MainWheelDebug::Category::Indicators, "hover_delay",
				"HoverDelay indicator (hover={:.2f}s, delay={:.2f}s, thickness={:.2f}, scale={:.2f})",
				a_hoveredEntryTimeSeconds, a_hoverActivateDelaySeconds, thickness, Config::Styling::Item::Slot::BackgroundTexture::Scale);

			// Helper: draw progress on a closed polyline from start to progress fraction
			auto draw_progress_on_closed_polyline = [&](const std::vector<ImVec2>& pts, float progress, ImU32 col) {
				if (pts.size() < 3 || progress <= 0.0f) {
					return;
				}
				progress = std::clamp(progress, 0.0f, 1.0f);

				float totalLen = 0.0f;
				for (std::size_t i = 0; i < pts.size(); ++i) {
					const ImVec2 a = pts[i];
					const ImVec2 b = pts[(i + 1) % pts.size()];
					const float dx = b.x - a.x;
					const float dy = b.y - a.y;
					totalLen += std::sqrt(dx * dx + dy * dy);
				}
				if (totalLen <= 0.0f) {
					return;
				}

				const float target = totalLen * progress;
				float acc = 0.0f;
				for (std::size_t i = 0; i < pts.size(); ++i) {
					const ImVec2 a = pts[i];
					const ImVec2 b = pts[(i + 1) % pts.size()];
					const float dx = b.x - a.x;
					const float dy = b.y - a.y;
					const float segLen = std::sqrt(dx * dx + dy * dy);
					if (segLen <= 0.0f) {
						continue;
					}
					if (acc + segLen <= target) {
						drawList->AddLine(a, b, col, thickness);
						acc += segLen;
						continue;
					}

					const float remain = target - acc;
					const float t = std::clamp(remain / segLen, 0.0f, 1.0f);
					const ImVec2 mid(a.x + dx * t, a.y + dy * t);
					drawList->AddLine(a, mid, col, thickness);
					break;
				}
			};

			// Helper: draw a segment of a closed polyline from startFrac to endFrac, filled by progress (0..1)
			auto draw_segment_on_closed_polyline = [&](const std::vector<ImVec2>& pts, float startFrac, float endFrac, float progress, ImU32 col) {
				if (pts.size() < 3 || progress <= 0.0f || endFrac <= startFrac) {
					return;
				}
				progress = std::clamp(progress, 0.0f, 1.0f);
				startFrac = std::clamp(startFrac, 0.0f, 1.0f);
				endFrac = std::clamp(endFrac, 0.0f, 1.0f);

				float totalLen = 0.0f;
				for (std::size_t i = 0; i < pts.size(); ++i) {
					const ImVec2 a = pts[i];
					const ImVec2 b = pts[(i + 1) % pts.size()];
					const float dx = b.x - a.x;
					const float dy = b.y - a.y;
					totalLen += std::sqrt(dx * dx + dy * dy);
				}
				if (totalLen <= 0.0f) {
					return;
				}

				const float segmentLen = (endFrac - startFrac) * totalLen;
				const float startDist = startFrac * totalLen;
				const float targetDist = startDist + segmentLen * progress;

				float acc = 0.0f;
				bool inSegment = false;
				for (std::size_t i = 0; i < pts.size(); ++i) {
					const ImVec2 a = pts[i];
					const ImVec2 b = pts[(i + 1) % pts.size()];
					const float dx = b.x - a.x;
					const float dy = b.y - a.y;
					const float edgeLen = std::sqrt(dx * dx + dy * dy);
					if (edgeLen <= 0.0f) {
						continue;
					}

					const float edgeStart = acc;
					const float edgeEnd = acc + edgeLen;

					// Check if this edge intersects with our segment range [startDist, targetDist]
					if (edgeEnd <= startDist) {
						acc += edgeLen;
						continue;
					}
					if (edgeStart >= targetDist) {
						break;
					}

					// Calculate the portion of this edge to draw
					const float drawStart = (std::max)(startDist, edgeStart);
					const float drawEnd = (std::min)(targetDist, edgeEnd);

					const float t0 = std::clamp((drawStart - edgeStart) / edgeLen, 0.0f, 1.0f);
					const float t1 = std::clamp((drawEnd - edgeStart) / edgeLen, 0.0f, 1.0f);

					const ImVec2 p0(a.x + dx * t0, a.y + dy * t0);
					const ImVec2 p1(a.x + dx * t1, a.y + dy * t1);

					if (t1 > t0) {
						drawList->AddLine(p0, p1, col, thickness);
					}

					acc += edgeLen;
				}
			};

			for (int entryIdx = 0; entryIdx < entryRuntimeDataVec.size(); entryIdx++) {
				const bool hovered = entryRuntimeDataVec[entryIdx].hovered;
				if (!hovered) {
					continue;
				}
				// SAFETY: Bounds + null check for indicator pass
				if (entryIdx >= static_cast<int>(_entries.size()) || !_entries[entryIdx]) {
					continue;
				}
				const ImVec2 center = entryRuntimeDataVec[entryIdx].center;

				// Get the selected item for type checking
				std::shared_ptr<WheelItem> selectedItem = _entries[entryIdx]->GetSelectedItem();
				
				// Check if this is a shout with InstantShout enabled
				const bool isShoutItem = selectedItem && (dynamic_cast<WheelItemShout*>(selectedItem.get()) != nullptr);
				const bool instantShoutEnabled = isShoutItem && Config::WheelBehavior::InstantShout;
				
				// Check if this is a spell with instant cast enabled
				std::shared_ptr<WheelItemSpell> spellItem = selectedItem ? std::dynamic_pointer_cast<WheelItemSpell>(selectedItem) : nullptr;
				RE::SpellItem* spell = spellItem ? spellItem->GetSpell() : nullptr;
				const bool isSpellItem = static_cast<bool>(spellItem);
				const bool isInTransform = !TransformWheelManager::IsPlayerHuman();
				const bool instantSpellEnabled = isSpellItem && Wheeler::IsInstantEnabledForSpell(spell, isInTransform);
				
				// Get hold mode state BEFORE per-type check (needed for Instant Shout/Spell exception)
				const bool isHoldMode = Wheeler::IsConfirmHeld();
				const bool useRtuAutoInstantShout = isShoutItem && Wheeler::IsRTUAutoInstantShoutEnabled();
				
				// InstantShout Override Gate:
				// If we entered because of instantShoutIndicatorOverride (Activation Indicator is OFF but InstantShout+ShowIndicatorWhenRTUOff),
				// then ONLY draw shout indicators during hold mode. Skip all other items.
				if (!normalIndicatorEnabled && instantShoutIndicatorOverride) {
					if (!instantShoutEnabled || !isHoldMode) {
						continue;  // Not a shout or not holding - skip in override mode
					}
				}
				
				// Per-type RTU visibility check:
				// If RTU is ON but user disabled RTU for this specific item type, skip the indicator
				// EXCEPTION: For items with Instant Cast enabled, allow indicator when user is holding
				// This enables "Tap to Equip, Hold to Cast" with indicator only appearing on hold
				if (rtuEnabled && selectedItem) {
					bool rtuEnabledForType = true;  // Default: show indicator
					if (std::dynamic_pointer_cast<WheelItemAlchemy>(selectedItem) ||
						std::dynamic_pointer_cast<WheelItemIngredient>(selectedItem)) {
						rtuEnabledForType = Config::WheelBehavior::RTUAlchemy;
					} else if (isShoutItem) {
						rtuEnabledForType = Config::WheelBehavior::RTUShout;
					} else if (isSpellItem) {
						rtuEnabledForType = Config::WheelBehavior::RTUSpell;
					}
					
					// Skip indicator if RTU is disabled for this type
					// BUT: If it's an Instant Cast item and user is holding, allow it
					if (!rtuEnabledForType) {
						// Exception: Allow Instant indicator when holding
						if ((instantShoutEnabled || instantSpellEnabled) && isHoldMode) {
							// Continue to draw the indicator
						} else {
							continue;  // Skip this entry's indicator
						}
					}
				}

				// SHOUT-SPECIFIC GATING when RTU is OFF:
				// Shout indicators are driven ONLY by Hold-to-Activate, not by "Show Indicator When RTU Off".
				// This decouples shout visuals from the generic activator indicator.
				if (!rtuEnabled && instantShoutEnabled) {
					// When RTU is OFF, shout indicator only shows during active hold
					if (!isHoldMode) {
						continue;  // Not holding - skip shout indicator entirely
					}
				}

				// Check if this entry contains a shout and InstantShout is enabled
				// Works for both RTU mode and Hold-to-Use mode
				const bool useHoldOnlyInstantShout = instantShoutEnabled && rtuEnabled && !useRtuAutoInstantShout;
				bool isShout = instantShoutEnabled && (!useHoldOnlyInstantShout || isHoldMode);
				
				// Get unified activation timing (hoverTime for RTU, holdSeconds for Hold-to-Use)
				float activationTime = Wheeler::GetActivationTiming();

				// Instant Cast Hold Threshold:
				// If using Instant Shout/Spell in Hold mode, wait for threshold before showing/filling the indicator.
				// This allows "Tap" (Equip) to be visually distinct from "Hold" (Cast).
				if ((instantShoutEnabled || instantSpellEnabled) && isHoldMode) {
					const float kHoldThreshold = Config::WheelBehavior::HoldToCastSafetyThresholdMs / 1000.0f; // Configurable via INI
					if (activationTime < kHoldThreshold) {
						continue; // Hide indicator during the "Tap" window
					}
					// Adjust time so indicator starts filling from 0 after threshold
					activationTime -= kHoldThreshold;
				}

				// Build outline points (used by both indicator types)
				std::vector<ImVec2> pts;
				bool usePolyline = false;
				float circleRadius = 0.0f;

				if (slotOutline && slotBg.texture && slotOutline->size() >= 4 && size.x > 0.0f && size.y > 0.0f) {
					pts.reserve(slotOutline->size());
					const ImVec2 posMin(center.x - size.x * 0.5f, center.y - size.y * 0.5f);
					const float baseRadius = (std::max)(size.x, size.y) * 0.5f;
					const float outwardPx = (std::max)(0.0f, Config::Styling::HoverDelay::RadiusOffset);
					const float outwardScale = baseRadius > 0.0f ? (1.0f + outwardPx / baseRadius) : 1.0f;

					for (std::size_t i = 0; i + 1 < slotOutline->size(); ++i) {
						ImVec2 p = ImVec2(posMin.x + (*slotOutline)[i].x * size.x, posMin.y + (*slotOutline)[i].y * size.y);
						p = center + (p - center) * outwardScale;
						pts.push_back(p);
					}
					usePolyline = (pts.size() >= 3);
				}

				if (!usePolyline) {
					circleRadius = (std::max)(0.0f, Config::Styling::HoverDelay::Radius + Config::Styling::HoverDelay::RadiusOffset);
				}

				if (isShout) {
					// 3-stage shout indicator with fill-then-hold behavior
					// Get unlocked word count and clamp indicator to available stages
					WheelItemShout* shoutItemPtr = dynamic_cast<WheelItemShout*>(selectedItem.get());
					RE::TESShout* shoutForm = shoutItemPtr ? shoutItemPtr->GetShout() : nullptr;
					RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
					ShoutUtils::ShoutUnlockState unlockState = ShoutUtils::GetShoutUnlockState(shoutForm, pc);
					int unlockedWords = unlockState.finalCount;
					if (unlockState.computeState == ShoutUtils::UnlockComputeState::Pending &&
						unlockedWords <= 0 &&
						unlockState.learnedCountContig > 0) {
						const int stableCount = unlockState.previousCount > 0 ? unlockState.previousCount : 1;
						unlockedWords = std::clamp((std::max)(1, stableCount), 1, unlockState.learnedCountContig);
						MainWheelDebug::LogRateLimited(MainWheelDebug::Category::Indicators, "shout_pending_indicator_fallback",
							"ShoutIndicator pending fallback shout={:08X} fallbackUnlocked={} learned={} prev={}",
							shoutForm ? shoutForm->GetFormID() : 0, unlockedWords, unlockState.learnedCountContig, unlockState.previousCount);
					}
					unlockedWords = std::clamp(unlockedWords, 0, 3);

					// Get stage thresholds from single source of truth
					float seg1Done, seg2Done, seg3Done;
					ShoutUtils::GetStageThresholds(seg1Done, seg2Done, seg3Done);
					const float fill = Config::WheelBehavior::ShoutStageFillSecs;

					// Clamp effective time to last unlocked stage boundary
					const float maxEffectiveTime = ShoutUtils::GetMaxEffectiveTime(unlockedWords);
					const float t = (std::min)(activationTime, maxEffectiveTime);

					// Calculate progress for each segment (fill then hold at full)
					// Segment 1: fills during [0, fill], stays full during [fill, seg1Done]
					float p1 = 0.0f;
					if (unlockedWords >= 1) {
						p1 = (fill > 0.0f) ? std::clamp(t / fill, 0.0f, 1.0f) : 1.0f;
					}
					
					// Segment 2: fills during [seg1Done, seg1Done+fill], stays full after
					float p2 = 0.0f;
					if (unlockedWords >= 2 && t >= seg1Done) {
						const float t2 = t - seg1Done;
						p2 = (fill > 0.0f) ? std::clamp(t2 / fill, 0.0f, 1.0f) : 1.0f;
					}
					
					// Segment 3: fills during [seg2Done, seg3Done]
					float p3 = 0.0f;
					if (unlockedWords >= 3 && t >= seg2Done) {
						const float t3 = t - seg2Done;
						p3 = (fill > 0.0f) ? std::clamp(t3 / fill, 0.0f, 1.0f) : 1.0f;
					}

					// Segment fractions around the perimeter (with small gaps)
					const float gapFrac = 0.02f;  // 2% gap between segments
					const float seg1Start = 0.0f;
					const float seg1End = 0.33f - gapFrac;
					const float seg2Start = 0.33f + gapFrac;
					const float seg2End = 0.66f - gapFrac;
					const float seg3Start = 0.66f + gapFrac;
					const float seg3End = 1.0f;

					// Colors for each shout stage (configurable via dMenu/INI)
					// Alpha from config is used, then multiplied by runtime alphaMult
					ImVec4 col1 = ImGui::ColorConvertU32ToFloat4(Config::WheelBehavior::ShoutStageColor1);
					ImVec4 col2 = ImGui::ColorConvertU32ToFloat4(Config::WheelBehavior::ShoutStageColor2);
					ImVec4 col3 = ImGui::ColorConvertU32ToFloat4(Config::WheelBehavior::ShoutStageColor3);
					col1.w *= a_drawArgs.alphaMult;
					col2.w *= a_drawArgs.alphaMult;
					col3.w *= a_drawArgs.alphaMult;
					const ImU32 fgCol1 = ImGui::ColorConvertFloat4ToU32(col1);
					const ImU32 fgCol2 = ImGui::ColorConvertFloat4ToU32(col2);
					const ImU32 fgCol3 = ImGui::ColorConvertFloat4ToU32(col3);
					const float baseRadiusPx = (std::max)(size.x, size.y) * 0.5f;
					const float outwardPx = (std::max)(0.0f, Config::Styling::HoverDelay::RadiusOffset);
					const float shoutAssetScale = std::clamp(Config::Styling::HoverDelay::InstantShoutAssetScale, 0.1f, 12.0f);
					const ImVec2 shoutAssetBaseOffset(
						Config::Styling::HoverDelay::InstantShoutAssetOffsetX,
						Config::Styling::HoverDelay::InstantShoutAssetOffsetY);
					const ImVec2 shoutAssetBaseCenter(center.x + shoutAssetBaseOffset.x, center.y + shoutAssetBaseOffset.y);
					const ImVec2 stage1Offset(
						Config::Styling::HoverDelay::InstantShoutStage1OffsetX,
						Config::Styling::HoverDelay::InstantShoutStage1OffsetY);
					const ImVec2 stage2Offset(
						Config::Styling::HoverDelay::InstantShoutStage2OffsetX,
						Config::Styling::HoverDelay::InstantShoutStage2OffsetY);
					const ImVec2 stage3Offset(
						Config::Styling::HoverDelay::InstantShoutStage3OffsetX,
						Config::Styling::HoverDelay::InstantShoutStage3OffsetY);
					const float stage1RotationDeg = Config::Styling::HoverDelay::InstantShoutStage1RotationDeg;
					const float stage2RotationDeg = Config::Styling::HoverDelay::InstantShoutStage2RotationDeg;
					const float stage3RotationDeg = Config::Styling::HoverDelay::InstantShoutStage3RotationDeg;
					const bool shoutAnimateReveal = Config::Styling::HoverDelay::InstantShoutAnimateReveal;
					const float shoutAssetDiameterPx = usePolyline ?
						(std::max)(8.0f, (baseRadiusPx + outwardPx) * 2.0f * shoutAssetScale) :
						(std::max)(8.0f, circleRadius * 2.0f * shoutAssetScale);
					const auto drawShoutStageAsset = [&](const char* path,
						const ImVec4& stageColor,
						float stageProgress,
						float segStartFrac,
						float segEndFrac,
						const ImVec2& stageOffset,
						float stageRotationDeg) -> bool {
						const float p = std::clamp(stageProgress, 0.0f, 1.0f);
						if (!path || p <= 0.0f || segEndFrac <= segStartFrac) {
							return false;
						}
						const Texture::Image icon = Texture::GetImageByPath(path);
						if (!icon.texture || icon.width <= 0 || icon.height <= 0) {
							return false;
						}

						const float aspect = static_cast<float>(icon.width) / static_cast<float>(icon.height);
						const ImVec2 iconSize(shoutAssetDiameterPx * aspect, shoutAssetDiameterPx);
						const ImVec2 stageCenter(shoutAssetBaseCenter.x + stageOffset.x, shoutAssetBaseCenter.y + stageOffset.y);
						const ImVec2 posMin(stageCenter.x - iconSize.x * 0.5f, stageCenter.y - iconSize.y * 0.5f);
						const ImVec2 posMax(posMin.x + iconSize.x, posMin.y + iconSize.y);
						const ImU32 tint = ImGui::ColorConvertFloat4ToU32(stageColor);
						ImTextureID texId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(icon.texture));
						const float rotationRad = stageRotationDeg * (IM_PI / 180.0f);
						const float rotCos = std::cos(rotationRad);
						const float rotSin = std::sin(rotationRad);
						const auto rotateAroundCenter = [&](const ImVec2& p, float c, float s) -> ImVec2 {
							const float dx = p.x - stageCenter.x;
							const float dy = p.y - stageCenter.y;
							return ImVec2(
								stageCenter.x + dx * c - dy * s,
								stageCenter.y + dx * s + dy * c);
						};

						// Optional reveal animation toggle for shout stage SVG reskins.
						if (!shoutAnimateReveal || p >= 0.9999f) {
							if (std::abs(stageRotationDeg) < 0.01f) {
								ImGui::GetWindowDrawList()->AddImage(texId, posMin, posMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
							} else {
								const ImVec2 pTL = rotateAroundCenter(ImVec2(posMin.x, posMin.y), rotCos, rotSin);
								const ImVec2 pTR = rotateAroundCenter(ImVec2(posMax.x, posMin.y), rotCos, rotSin);
								const ImVec2 pBR = rotateAroundCenter(ImVec2(posMax.x, posMax.y), rotCos, rotSin);
								const ImVec2 pBL = rotateAroundCenter(ImVec2(posMin.x, posMax.y), rotCos, rotSin);
								ImGui::GetWindowDrawList()->AddImageQuad(
									texId,
									pTL, pTR, pBR, pBL,
									ImVec2(0.0f, 0.0f), ImVec2(1.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec2(0.0f, 1.0f),
									tint);
							}
							return true;
						}

						const float startAngle = -IM_PI / 2.0f + 2.0f * IM_PI * segStartFrac;
						const float segSpan = 2.0f * IM_PI * (segEndFrac - segStartFrac);
						const float endAngle = startAngle + segSpan * p;
						const float span = endAngle - startAngle;
						if (span <= 0.0001f) {
							return false;
						}
						const int segments = std::clamp(static_cast<int>(std::ceil(span / (IM_PI / 48.0f))), 1, 96);

						const auto uvFromPoint = [&](const ImVec2& point) -> ImVec2 {
							// Inverse-rotate sample point so UV lookup matches rotated stage texture.
							const ImVec2 localPoint = rotateAroundCenter(point, rotCos, -rotSin);
							const float safeW = (std::max)(1.0f, iconSize.x);
							const float safeH = (std::max)(1.0f, iconSize.y);
							const float u = std::clamp((localPoint.x - posMin.x) / safeW, 0.0f, 1.0f);
							const float v = std::clamp((localPoint.y - posMin.y) / safeH, 0.0f, 1.0f);
							return ImVec2(u, v);
						};
						const auto rayToRectEdge = [&](float angleRad) -> ImVec2 {
							const float dx = std::cos(angleRad);
							const float dy = std::sin(angleRad);
							// Intersect against rotated stage rect by transforming ray into stage-local space.
							const float dLocalX = dx * rotCos + dy * rotSin;
							const float dLocalY = -dx * rotSin + dy * rotCos;
							const float halfW = iconSize.x * 0.5f;
							const float halfH = iconSize.y * 0.5f;

							float tx = std::numeric_limits<float>::infinity();
							float ty = std::numeric_limits<float>::infinity();
							if (std::abs(dLocalX) > 0.00001f) {
								tx = halfW / std::abs(dLocalX);
							}
							if (std::abs(dLocalY) > 0.00001f) {
								ty = halfH / std::abs(dLocalY);
							}
							const float tRay = (std::min)(tx, ty);
							if (!std::isfinite(tRay) || tRay <= 0.0f) {
								return stageCenter;
							}
							return ImVec2(stageCenter.x + dx * tRay, stageCenter.y + dy * tRay);
						};
						const auto drawTexturedTri = [&](ImDrawList* list,
							const ImVec2& p0, const ImVec2& p1, const ImVec2& p2,
							const ImVec2& uv0, const ImVec2& uv1, const ImVec2& uv2) {
							list->PushTextureID(texId);
							list->PrimReserve(3, 3);
							list->PrimWriteIdx(static_cast<ImDrawIdx>(list->_VtxCurrentIdx));
							list->PrimWriteIdx(static_cast<ImDrawIdx>(list->_VtxCurrentIdx + 1));
							list->PrimWriteIdx(static_cast<ImDrawIdx>(list->_VtxCurrentIdx + 2));
							list->PrimWriteVtx(p0, uv0, tint);
							list->PrimWriteVtx(p1, uv1, tint);
							list->PrimWriteVtx(p2, uv2, tint);
							list->PopTextureID();
						};

						ImDrawList* stageList = ImGui::GetWindowDrawList();
						const ImVec2 centerUv(0.5f, 0.5f);
						for (int s = 0; s < segments; ++s) {
							const float t0 = static_cast<float>(s) / static_cast<float>(segments);
							const float t1 = static_cast<float>(s + 1) / static_cast<float>(segments);
							const float a0 = startAngle + span * t0;
							const float a1 = startAngle + span * t1;
							const ImVec2 p1 = rayToRectEdge(a0);
							const ImVec2 p2 = rayToRectEdge(a1);
							const ImVec2 uv1 = uvFromPoint(p1);
							const ImVec2 uv2 = uvFromPoint(p2);
							drawTexturedTri(stageList, stageCenter, p1, p2, centerUv, uv1, uv2);
						}
						return true;
					};


					if (usePolyline) {
						// Draw background for all 3 segments
						drawList->AddPolyline(pts.data(), static_cast<int>(pts.size()), bgCol, ImDrawFlags_Closed, thickness);

						// Draw progress for each segment (prefer SVG reskin stage assets; fallback to primitive stage arc)
						const bool drewStage1 = drawShoutStageAsset(kInstantShoutStage1AssetPath, col1, p1, seg1Start, seg1End, stage1Offset, stage1RotationDeg);
						const bool drewStage2 = drawShoutStageAsset(kInstantShoutStage2AssetPath, col2, p2, seg2Start, seg2End, stage2Offset, stage2RotationDeg);
						const bool drewStage3 = drawShoutStageAsset(kInstantShoutStage3AssetPath, col3, p3, seg3Start, seg3End, stage3Offset, stage3RotationDeg);
						if (!drewStage1) {
							draw_segment_on_closed_polyline(pts, seg1Start, seg1End, p1, fgCol1);
						}
						if (!drewStage2) {
							draw_segment_on_closed_polyline(pts, seg2Start, seg2End, p2, fgCol2);
						}
						if (!drewStage3) {
							draw_segment_on_closed_polyline(pts, seg3Start, seg3End, p3, fgCol3);
						}
					} else {
						// Circular fallback
						const float r = circleRadius;
						const float rMin = (std::max)(0.0f, r - thickness * 0.5f);
						const float rMax = r + thickness * 0.5f;
						const float startAng = -IM_PI / 2.0f;
						const float fullArc = 2.0f * IM_PI;

						// Background
						Drawer::draw_arc(center, rMin, rMax, startAng, startAng + fullArc, startAng, startAng + fullArc, bgCol, 64, a_drawArgs);

						// Segment 1
						const float ang1Start = startAng + fullArc * seg1Start;
						const float ang1End = startAng + fullArc * (seg1Start + (seg1End - seg1Start) * p1);
						const bool drewStage1 = drawShoutStageAsset(kInstantShoutStage1AssetPath, col1, p1, seg1Start, seg1End, stage1Offset, stage1RotationDeg);
						if (p1 > 0.0f && !drewStage1) {
							Drawer::draw_arc(center, rMin, rMax, ang1Start, ang1End, ang1Start, ang1End, fgCol1, 64, a_drawArgs);
						}

						// Segment 2
						const float ang2Start = startAng + fullArc * seg2Start;
						const float ang2End = startAng + fullArc * (seg2Start + (seg2End - seg2Start) * p2);
						const bool drewStage2 = drawShoutStageAsset(kInstantShoutStage2AssetPath, col2, p2, seg2Start, seg2End, stage2Offset, stage2RotationDeg);
						if (p2 > 0.0f && !drewStage2) {
							Drawer::draw_arc(center, rMin, rMax, ang2Start, ang2End, ang2Start, ang2End, fgCol2, 64, a_drawArgs);
						}

						// Segment 3
						const float ang3Start = startAng + fullArc * seg3Start;
						const float ang3End = startAng + fullArc * (seg3Start + (seg3End - seg3Start) * p3);
						const bool drewStage3 = drawShoutStageAsset(kInstantShoutStage3AssetPath, col3, p3, seg3Start, seg3End, stage3Offset, stage3RotationDeg);
						if (p3 > 0.0f && !drewStage3) {
							Drawer::draw_arc(center, rMin, rMax, ang3Start, ang3End, ang3Start, ang3End, fgCol3, 64, a_drawArgs);
						}
					}
				} else {
					// Generic single-progress indicator (original behavior)
					// Use unified activation timing for both RTU and Hold-to-Use modes
					
					// Determine delay scale:
					// - For Instant Spell in Hold mode: use (HoldThreshold - 0.6s) to match activationTime adjustment
					// - Otherwise: use RTU delay or default 1.0s
					float delayScale = (a_hoverActivateDelaySeconds > 0.0f) ? a_hoverActivateDelaySeconds : 1.0f;
					if (instantSpellEnabled && isHoldMode) {
						const float configThreshold = Config::WheelBehavior::InstantSpellHoldThresholdMs / 1000.0f;
						const float safetyThreshold = Config::WheelBehavior::HoldToCastSafetyThresholdMs / 1000.0f;
						const float fullThreshold = (std::max)(safetyThreshold, configThreshold);
						// Subtract safety offset to sync with activationTime (which was also reduced by 0.6s)
						const float kSafetyThreshold = Config::WheelBehavior::HoldToCastSafetyThresholdMs / 1000.0f;
						delayScale = fullThreshold - kSafetyThreshold;
						if (delayScale <= 0.0f) delayScale = 0.1f; // Failsafe
					}
					const float progress = std::clamp(activationTime / delayScale, 0.0f, 1.0f);

					if (usePolyline) {
						drawList->AddPolyline(pts.data(), static_cast<int>(pts.size()), bgCol, ImDrawFlags_Closed, thickness);
						draw_progress_on_closed_polyline(pts, progress, fgCol);
					} else {
						const float r = circleRadius;
						const float rMin = (std::max)(0.0f, r - thickness * 0.5f);
						const float rMax = r + thickness * 0.5f;
						const float startAng = -IM_PI / 2.0f;

						Drawer::draw_arc(center, rMin, rMax, startAng, startAng + 2.0f * IM_PI, startAng, startAng + 2.0f * IM_PI, bgCol, 64, a_drawArgs);
						Drawer::draw_arc(center, rMin, rMax, startAng, startAng + 2.0f * IM_PI * progress, startAng, startAng + 2.0f * IM_PI * progress, fgCol, 64, a_drawArgs);
					}
				}
			}
		}

		// Instant cast countdown arc indicator (separate from shout stage indicator).
		// Only draws when instant gating allows the hovered spell category and there's an active attempt.
		int instantEntryIdx = -1;
		float instantElapsed = 0.0f, instantThreshold = 3.0f;
		bool instantReady = false, instantCancelled = false;
		Wheeler::TargetHand instantTargetHand = Wheeler::TargetHand::Right;
		if (Wheeler::GetInstantSpellState(instantEntryIdx, instantElapsed, instantThreshold, instantReady, instantCancelled, instantTargetHand)) {
			// Only draw for the slot that matches the instant attempt
			if (instantEntryIdx >= 0 && instantEntryIdx < static_cast<int>(_entries.size()) && !instantCancelled) {
				const auto& slotEntry = entryRuntimeDataVec[instantEntryIdx];
				const ImVec2 slotCenter = slotEntry.center;  // entry center
				
				// Calculate progress (0..1)
				const float progress = std::clamp(instantElapsed / instantThreshold, 0.0f, 1.0f);
				const bool useReskinAssets = Config::Styling::HoverDelay::InstantSpellUseReskinAssets;
				const bool useAtlasAnimation = Config::Styling::HoverDelay::InstantSpellUseAtlasAnimation;
				const bool useCustomIndicator = useReskinAssets || useAtlasAnimation;
				const float assetScale = std::clamp(Config::Styling::HoverDelay::InstantSpellAssetScale, 0.1f, 12.0f);
				const float assetOpacity = std::clamp(Config::Styling::HoverDelay::InstantSpellAssetOpacity, 0.0f, 1.0f);
				const ImVec2 assetOffset(
					Config::Styling::HoverDelay::InstantSpellAssetOffsetX,
					Config::Styling::HoverDelay::InstantSpellAssetOffsetY);
				const ImVec2 indicatorCenter = useCustomIndicator ?
					ImVec2(slotCenter.x + assetOffset.x, slotCenter.y + assetOffset.y) :
					slotCenter;
				
				// Use dedicated InstantSpell styling (separate from activation indicator)
				ImVec4 arcColor = ImGui::ColorConvertU32ToFloat4(Config::Styling::HoverDelay::InstantSpellColor);
				float arcThickness = Config::Styling::HoverDelay::Thickness;
				
				// Pulse effect when ready (alpha pulsing only to avoid jitter)
				if (instantReady) {
					// Smooth sinusoidal pulse ~0.8Hz on alpha
					const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(ImGui::GetTime()) * 5.0f);
					arcColor.w *= (0.5f + 0.5f * pulse);  // Pulse between 50% and 100% alpha
				}
				ImVec4 arcColorForArc = arcColor;
				arcColorForArc.w *= a_drawArgs.alphaMult;
				const ImU32 arcCol = ImGui::ColorConvertFloat4ToU32(arcColorForArc);
				
				// Calculate arc radius based on slot - use slot outline if available
				const float arcRadius = (Config::Styling::HoverDelay::Radius + Config::Styling::HoverDelay::RadiusOffset) * 
					Config::Styling::Item::Slot::BackgroundTexture::Scale *
					(useCustomIndicator ? assetScale : 1.0f);
				const float rMin = arcRadius - arcThickness * 0.5f;
				const float rMax = arcRadius + arcThickness * 0.5f;
				
				// Draw background ring
				ImVec4 bgColor = ImGui::ColorConvertU32ToFloat4(Config::Styling::HoverDelay::InstantSpellBackgroundColor);
				ImVec4 bgColorForArc = bgColor;
				bgColorForArc.w *= a_drawArgs.alphaMult;
				const ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(bgColorForArc);
				const auto drawInstantAsset = [&](const std::string& path, const ImVec4& color) -> bool {
					if (path.empty()) {
						return false;
					}
					const Texture::Image icon = Texture::GetImageByPath(path);
					if (!icon.texture || icon.width <= 0 || icon.height <= 0) {
						return false;
					}
					const float iconHeight = (std::max)(8.0f, arcRadius * 2.0f);
					const float aspect = static_cast<float>(icon.width) / static_cast<float>(icon.height);
					const ImVec2 iconSize(iconHeight * aspect, iconHeight);
					ImVec4 assetColor = color;
					assetColor.w *= assetOpacity * a_drawArgs.alphaMult;
					const ImU32 tint = ImGui::ColorConvertFloat4ToU32(assetColor);
					DrawArgs assetDrawArgs = a_drawArgs;
					assetDrawArgs.rotationOffset = 0.0f;
					Drawer::draw_texture(icon.texture, indicatorCenter, 0.0f, 0.0f, iconSize, tint, assetDrawArgs);
					return true;
				};
				const auto drawInstantHandOverlay = [&](Wheeler::TargetHand hand) -> bool {
					const std::string* assetPath = nullptr;
					const char* fallbackText = nullptr;
					switch (hand) {
					case Wheeler::TargetHand::Left:
						assetPath = &Config::Styling::HoverDelay::InstantSpellHandLeftAssetPath;
						fallbackText = "L";
						break;
					case Wheeler::TargetHand::Both:
						assetPath = &Config::Styling::HoverDelay::InstantSpellHandBothAssetPath;
						fallbackText = "LR";
						break;
					case Wheeler::TargetHand::Right:
					default:
						assetPath = &Config::Styling::HoverDelay::InstantSpellHandRightAssetPath;
						fallbackText = "R";
						break;
					}

					const float baseScale = std::clamp(Config::Styling::HoverDelay::InstantSpellHandIndicatorScale, 0.2f, 4.0f);
					const float baseOpacity = std::clamp(Config::Styling::HoverDelay::InstantSpellHandIndicatorOpacity, 0.0f, 1.0f);
					const float blinkPhase = std::fmod(static_cast<float>(ImGui::GetTime()) * 2.25f, 1.0f);
					const float blinkAlpha = blinkPhase < 0.5f ? 1.0f : 0.28f;
					const float effectiveAlpha = blinkAlpha * baseOpacity * a_drawArgs.alphaMult;
					if (effectiveAlpha <= 0.001f) {
						return false;
					}

					const ImVec2 baseOffset = [&]() {
						switch (instantTargetHand) {
						case Wheeler::TargetHand::Left:
							return ImVec2(
								Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetX,
								Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetY);
						case Wheeler::TargetHand::Both:
							return ImVec2(
								Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetX,
								Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetY);
						case Wheeler::TargetHand::Right:
						default:
							return ImVec2(
								Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetX,
								Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetY);
						}
					}();
					const ImVec2 handCenter(
						indicatorCenter.x + baseOffset.x,
						indicatorCenter.y - arcRadius * 0.55f + baseOffset.y);

					if (assetPath && !assetPath->empty()) {
						const Texture::Image icon = Texture::GetImageByPath(*assetPath);
						if (icon.texture && icon.width > 0 && icon.height > 0) {
							const float iconHeight = (std::max)(10.0f, arcRadius * 0.58f * baseScale);
							const float aspect = static_cast<float>(icon.width) / static_cast<float>(icon.height);
							const ImVec2 iconSize(iconHeight * aspect, iconHeight);
							const ImVec4 handTint(1.0f, 1.0f, 1.0f, effectiveAlpha);
							const ImU32 tint = ImGui::ColorConvertFloat4ToU32(handTint);
							DrawArgs handDrawArgs = a_drawArgs;
							handDrawArgs.alphaMult = 1.0f;
							handDrawArgs.rotationOffset = 0.0f;
							Drawer::draw_texture(icon.texture, handCenter, 0.0f, 0.0f, iconSize, tint, handDrawArgs);
							return true;
						}
					}

					ImFont* font = ImGui::GetFont();
					const float fontSize = (std::max)(10.0f, arcRadius * 0.34f * baseScale);
					const ImVec2 textSize = font ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fallbackText) : ImVec2(fontSize, fontSize);
					const ImVec2 textPos(handCenter.x - textSize.x * 0.5f, handCenter.y - textSize.y * 0.5f);
					const ImU32 shadowCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, effectiveAlpha * 0.6f));
					const ImU32 textCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.95f, 0.8f, effectiveAlpha));
					ImDrawList* drawList = ImGui::GetForegroundDrawList();
					drawList->AddText(font, fontSize, ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), shadowCol, fallbackText);
					drawList->AddText(font, fontSize, textPos, textCol, fallbackText);
					return true;
				};
				const auto drawInstantRevealOverlay = [&](const std::string& path, const ImVec4& color) -> bool {
					if (path.empty()) {
						return false;
					}
					const Texture::Image image = Texture::GetImageByPath(path);
					if (!image.texture || image.width <= 0 || image.height <= 0) {
						return false;
					}
					const float iconHeight = (std::max)(8.0f, arcRadius * 2.0f);
					const float aspect = static_cast<float>(image.width) / static_cast<float>(image.height);
					const ImVec2 iconSize(iconHeight * aspect, iconHeight);
					const ImVec2 posMin(indicatorCenter.x - iconSize.x * 0.5f, indicatorCenter.y - iconSize.y * 0.5f);
					const ImVec2 posMax(posMin.x + iconSize.x, posMin.y + iconSize.y);

					ImVec4 revealColor = color;
					revealColor.w *= assetOpacity * a_drawArgs.alphaMult;
					const ImU32 tint = ImGui::ColorConvertFloat4ToU32(revealColor);
					ImTextureID texId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(image.texture));
					if (progress <= 0.0001f) {
						return false;
					}
					const float progressT = std::clamp(progress, 0.0f, 1.0f);
					// Keep reveal and spin exactly in sync with hold-threshold time.
					const float revealProgress = progressT;
					const float spinProgress = progressT;

					// Rotate overlay with slight phase/speed drift from reveal sweep so details (e.g. dragon head)
					// do not stay locked on the cut boundary for the whole timer.
					// One full spin per threshold window: finishes where it started when timer completes.
					const float spinTurns = 1.0f;
					const float spinPhaseTurns = -0.08f;  // initial alignment tweak
					const float spinAngle = 2.0f * IM_PI * (spinPhaseTurns + spinTurns * spinProgress);
					const float spinCos = std::cos(spinAngle);
					const float spinSin = std::sin(spinAngle);
					const auto rotateAroundCenter = [&](const ImVec2& p, float c, float s) -> ImVec2 {
						const float dx = p.x - indicatorCenter.x;
						const float dy = p.y - indicatorCenter.y;
						return ImVec2(
							indicatorCenter.x + dx * c - dy * s,
							indicatorCenter.y + dx * s + dy * c);
					};
					const auto uvFromPoint = [&](const ImVec2& p) -> ImVec2 {
						// Inverse rotate sample point so UV lookup tracks rotating texture.
						const ImVec2 pUnrotated = rotateAroundCenter(p, spinCos, -spinSin);
						const float safeW = (std::max)(1.0f, iconSize.x);
						const float safeH = (std::max)(1.0f, iconSize.y);
						const float u = std::clamp((pUnrotated.x - posMin.x) / safeW, 0.0f, 1.0f);
						const float v = std::clamp((pUnrotated.y - posMin.y) / safeH, 0.0f, 1.0f);
						return ImVec2(u, v);
					};
					const auto rayToRectEdge = [&](float angleRad) -> ImVec2 {
						const float dx = std::cos(angleRad);
						const float dy = std::sin(angleRad);
						// Intersect against the rotated texture rect in local (unrotated) space.
						const float dLocalX = dx * spinCos + dy * spinSin;
						const float dLocalY = -dx * spinSin + dy * spinCos;
						const float halfW = iconSize.x * 0.5f;
						const float halfH = iconSize.y * 0.5f;

						float tx = std::numeric_limits<float>::infinity();
						float ty = std::numeric_limits<float>::infinity();
						if (std::abs(dLocalX) > 0.00001f) {
							tx = halfW / std::abs(dLocalX);
						}
						if (std::abs(dLocalY) > 0.00001f) {
							ty = halfH / std::abs(dLocalY);
						}
						const float t = (std::min)(tx, ty);
						if (t <= 0.0f || !std::isfinite(t)) {
							return indicatorCenter;
						}
						return ImVec2(indicatorCenter.x + dx * t, indicatorCenter.y + dy * t);
					};
					const auto drawTexturedTri = [&](ImDrawList* drawList,
						const ImVec2& p0, const ImVec2& p1, const ImVec2& p2,
						const ImVec2& uv0, const ImVec2& uv1, const ImVec2& uv2,
						ImU32 triTint) {
						drawList->PushTextureID(texId);
						drawList->PrimReserve(3, 3);
						drawList->PrimWriteIdx(static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx));
						drawList->PrimWriteIdx(static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx + 1));
						drawList->PrimWriteIdx(static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx + 2));
						drawList->PrimWriteVtx(p0, uv0, triTint);
						drawList->PrimWriteVtx(p1, uv1, triTint);
						drawList->PrimWriteVtx(p2, uv2, triTint);
						drawList->PopTextureID();
					};

					const float startAngle = -IM_PI / 2.0f;  // top
					const float endAngle = startAngle + 2.0f * IM_PI * revealProgress;
					const float span = endAngle - startAngle;
					if (span <= 0.0001f) {
						return false;
					}
					const int segments = std::clamp(static_cast<int>(std::ceil(span / (IM_PI / 48.0f))), 1, 96);
					const bool isRevealComplete = progressT >= 0.999f;
					// Feather the cut edges so the dragon appears from a soft opening instead of a hard slice.
					const float maxFeather = IM_PI / 10.0f;  // ~18 deg
					const float featherSpan = isRevealComplete ? 0.0f : (std::min)(maxFeather, span * 0.60f);
					const std::uint32_t baseAlpha = (tint >> IM_COL32_A_SHIFT) & 0xFFu;
					const ImVec2 centerUv(0.5f, 0.5f);
					ImDrawList* drawList = ImGui::GetWindowDrawList();
					const bool useDragonTailLateGate =
						path.find("instant_spell_indicator_fg.svg") != std::string::npos;
					for (int s = 0; s < segments; ++s) {
						const float t0 = static_cast<float>(s) / static_cast<float>(segments);
						const float t1 = static_cast<float>(s + 1) / static_cast<float>(segments);
						const float a0 = startAngle + span * t0;
						const float a1 = startAngle + span * t1;
						float edgeFade = 1.0f;
						if (featherSpan > 0.0001f) {
							const float amid = (a0 + a1) * 0.5f;
							const float startFade = std::clamp((amid - startAngle) / featherSpan, 0.0f, 1.0f);
							const float endFade = std::clamp((endAngle - amid) / featherSpan, 0.0f, 1.0f);
							edgeFade = (std::min)(startFade, endFade);
							edgeFade = edgeFade * edgeFade * (3.0f - 2.0f * edgeFade);  // smoothstep
						}
						const ImVec2 p1 = rayToRectEdge(a0);
						const ImVec2 p2 = rayToRectEdge(a1);
						const ImVec2 uv1 = uvFromPoint(p1);
						const ImVec2 uv2 = uvFromPoint(p2);
						// Dragon-specific late-tail gate:
						// keep the lower-right outer-ring tail area mostly hidden until the end of the timer.
						if (useDragonTailLateGate && !isRevealComplete) {
							const ImVec2 uvMid(
								(centerUv.x + uv1.x + uv2.x) / 3.0f,
								(centerUv.y + uv1.y + uv2.y) / 3.0f);
							const float du = uvMid.x - 0.5f;
							const float dv = uvMid.y - 0.5f;
							const float uvRadius = std::sqrt(du * du + dv * dv);
							const bool inTailZone = (uvMid.x > 0.58f) && (uvMid.y > 0.52f) && (uvRadius > 0.42f);
							if (inTailZone) {
								const float tailStart = 0.88f;
								const float tailEnd = 0.995f;
								float tailGate = std::clamp((progressT - tailStart) / (tailEnd - tailStart), 0.0f, 1.0f);
								tailGate = tailGate * tailGate * (3.0f - 2.0f * tailGate);  // smoothstep
								edgeFade *= tailGate;
							}
						}
						const std::uint32_t triA = static_cast<std::uint32_t>(std::clamp(static_cast<float>(baseAlpha) * edgeFade, 0.0f, 255.0f));
						if (triA == 0u) {
							continue;
						}
						const ImU32 triTint = (tint & 0x00FFFFFFu) | (triA << IM_COL32_A_SHIFT);
						drawTexturedTri(drawList, indicatorCenter, p1, p2, centerUv, uv1, uv2, triTint);
					}
					return true;
				};

				bool drewCustomIndicator = false;
				if (useReskinAssets) {
					drewCustomIndicator = drawInstantAsset(Config::Styling::HoverDelay::InstantSpellBackgroundAssetPath, bgColor) || drewCustomIndicator;
				}

				bool drewAtlasOverlay = false;
				if (useAtlasAnimation) {
					drewAtlasOverlay = drawInstantRevealOverlay(Config::Styling::HoverDelay::InstantSpellAtlasAssetPath, arcColor);
					drewCustomIndicator = drewAtlasOverlay || drewCustomIndicator;
				}
				if (useReskinAssets) {
					if (!drewAtlasOverlay) {
						drewCustomIndicator = drawInstantAsset(Config::Styling::HoverDelay::InstantSpellOverlayAssetPath, arcColor) || drewCustomIndicator;
					}
				}
				if (!useCustomIndicator || !drewCustomIndicator) {
					Drawer::draw_arc(indicatorCenter, rMin, rMax, -IM_PI / 2.0f, -IM_PI / 2.0f + 2.0f * IM_PI,
						-IM_PI / 2.0f, -IM_PI / 2.0f + 2.0f * IM_PI, bgCol, 64, a_drawArgs);

					// Draw progress arc
					const float startAngle = -IM_PI / 2.0f;  // Start at top
					const float endAngle = startAngle + 2.0f * IM_PI * progress;
					Drawer::draw_arc(indicatorCenter, rMin, rMax, startAngle, endAngle,
						startAngle, endAngle, arcCol, 64, a_drawArgs);
				}
				drawInstantHandOverlay(instantTargetHand);
			}
		}

		// Debug rings for mouse stabilization (center hold + jump guard)
		if (mouseStabilizeEnabled && Config::MainWheel::Mouse::DrawDebugOverlay) {
			auto applyAlpha = [&](ImU32 color) {
				const float alpha = std::clamp(static_cast<float>(a_drawArgs.alphaMult), 0.0f, 1.0f);
				ImU32 a = (color >> 24) & 0xFF;
				a = static_cast<ImU32>(std::clamp(static_cast<float>(a) * alpha, 0.0f, 255.0f));
				return (color & 0x00FFFFFFu) | (a << 24);
			};
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const float centerRadius = cursorRadiusMax * Config::MainWheel::MouseStabilization::CenterHoldRadius;
			const float jumpRadius = cursorRadiusMax * Config::MainWheel::MouseStabilization::JumpGuardRadius;
			if (centerRadius > 0.0f) {
				const ImU32 centerColor = applyAlpha(IM_COL32(80, 220, 80, 160));
				drawList->AddCircle(a_wheelCenter, centerRadius, centerColor, 64, 1.5f);
			}
			if (jumpRadius > 0.0f) {
				const ImU32 jumpColor = applyAlpha(IM_COL32(255, 200, 64, 160));
				drawList->AddCircle(a_wheelCenter, jumpRadius, jumpColor, 64, 1.5f);
			}
		}

		// draw cursor indicator
		if (!suppressHoverVisuals) {
			float cursorIndicatorToCenterDist = InnerCircleRadius - CursorIndicatorDist;
			MainWheelDebug::LogRateLimited(MainWheelDebug::Category::Indicators, "cursor_indicator",
				"CursorIndicator angle={:.3f} dist={:.1f} arcWidth={:.1f} arcAngle={:.3f}",
				cursorIndicatorAngle, cursorIndicatorToCenterDist, CusorIndicatorArcWidth, CursorIndicatorArcAngle);
			Drawer::draw_arc(a_wheelCenter,
				cursorIndicatorToCenterDist - (CusorIndicatorArcWidth / 2),
				cursorIndicatorToCenterDist + (CusorIndicatorArcWidth / 2),
				cursorIndicatorAngle - (CursorIndicatorArcAngle / 2), cursorIndicatorAngle + (CursorIndicatorArcAngle / 2),
				cursorIndicatorAngle - (CursorIndicatorArcAngle / 2), cursorIndicatorAngle + (CursorIndicatorArcAngle / 2),
				CursorIndicatorColor,
				32, a_drawArgs);
			ImVec2 cursorIndicatorTriPts[3];
			if (Config::Styling::Wheel::CursorIndicatorInwardFacing) {
				cursorIndicatorTriPts[0] = { cursorIndicatorToCenterDist - (CusorIndicatorArcWidth / 2), +CursorIndicatorTriangleSideLength };
				cursorIndicatorTriPts[1] = { cursorIndicatorToCenterDist - (CusorIndicatorArcWidth / 2), -CursorIndicatorTriangleSideLength };
				cursorIndicatorTriPts[2] = { cursorIndicatorToCenterDist - (CusorIndicatorArcWidth / 2) - CursorIndicatorTriangleSideLength, 0 };
			} else {
				cursorIndicatorTriPts[0] = { cursorIndicatorToCenterDist + (CusorIndicatorArcWidth / 2), +CursorIndicatorTriangleSideLength };
				cursorIndicatorTriPts[1] = { cursorIndicatorToCenterDist + (CusorIndicatorArcWidth / 2), -CursorIndicatorTriangleSideLength };
				cursorIndicatorTriPts[2] = { cursorIndicatorToCenterDist + (CusorIndicatorArcWidth / 2) + CursorIndicatorTriangleSideLength, 0 };
			}
			for (ImVec2& pos : cursorIndicatorTriPts) {
				pos = ImRotate(pos, cos(cursorIndicatorAngle), sin(cursorIndicatorAngle));
			}
			Drawer::draw_triangle_filled(cursorIndicatorTriPts[0] + a_wheelCenter, cursorIndicatorTriPts[1] + a_wheelCenter, cursorIndicatorTriPts[2] + a_wheelCenter, CursorIndicatorColor, a_drawArgs);
		}
	} catch (const std::exception& e) {
		logger::error("Exception in Wheel::Draw: {}", e.what());
	}
}
void Wheel::PushEntry(std::unique_ptr<WheelEntry> a_entry) 
{
	std::unique_lock<std::shared_mutex> lock(_lock);
    this->_entries.push_back(std::move(a_entry));
}

void Wheel::PushEmptyEntry()
{
	this->PushEntry(std::make_unique<WheelEntry>());  // no need for lock here since it's already locked in PushEntry
}

bool Wheel::IsEmpty()
{
	std::shared_lock<std::shared_mutex> lock(_lock);
    return this->_entries.empty();
}

void Wheel::Clear()
{
	std::unique_lock<std::shared_mutex> lock(_lock);
    this->_entries.clear();
}

void Wheel::PrevItemInHoveredEntry()
{
	const auto capturedHover = HoverActivationSnapshotPolicy::Capture(_hoveredEntryIdx, _entries.size());
	if (!capturedHover) {
        return;
    }
	this->_entries[static_cast<std::size_t>(*capturedHover)]->PrevItem();
}

void Wheel::NextItemInHoveredEntry()
{
	const auto capturedHover = HoverActivationSnapshotPolicy::Capture(_hoveredEntryIdx, _entries.size());
	if (!capturedHover) {
        return;
    }
	this->_entries[static_cast<std::size_t>(*capturedHover)]->NextItem();

}

void Wheel::ResetAnimation()
{
	std::shared_lock<std::shared_mutex> lock(_lock);
	for (auto& entry : this->_entries) {
		entry->ResetAnimation();
	}
}

PreparedWheelItemActivation Wheel::ActivateHoveredEntryPrimary(bool a_editMode)
{
	std::shared_lock<std::shared_mutex> lock(_lock);  // might involve editing the wheel, so unique lock
	const int32_t hoverSnapshot = HoverActivationSnapshotPolicy::Load(_hoveredEntryIdx);
	
	// Debug: log which entry we're trying to activate
	MainWheelDebug::Log(MainWheelDebug::Category::Input, 
		"ActivateEntry: editMode={} hoveredIdx={} activationSnapshot={} totalEntries={}",
		a_editMode ? 1 : 0, hoverSnapshot, hoverSnapshot, static_cast<int>(_entries.size()));
	
	if (!HoverActivationSnapshotPolicy::IsValid(hoverSnapshot, _entries.size())) {
        return {};
    }
	WheelEntry* const entry = _entries[static_cast<std::size_t>(hoverSnapshot)].get();
	if (!entry) {
		return {};
	}
	if (!a_editMode) {
		const std::shared_ptr<WheelItem> item = entry->GetSelectedItem();
		// Check if the CURRENTLY SELECTED item is a WheelItemMissing placeholder
		// NOTE: We only check for WheelItemMissing here, NOT IsInPlayerInventory()
		// The full inventory check happens in WheelEntry::ActivateItemPrimary to avoid
		// duplicate IsInPlayerInventory() calls which have side effects (uniqueID updates)
		if (item && dynamic_cast<WheelItemMissing*>(item.get()) != nullptr) {
			return {};
		}
		if (!ShouldActivateWheelItem(item, hoverSnapshot)) {
			return {};
		}
		
		// WheelEntry either executes the accepted entry-locked item (weapons and
		// other non-queueing families) or returns a retained queue-capable item for
		// Wheeler to execute after every container lock has been released.
		PreparedWheelItemActivation prepared = entry->ActivateItemPrimary(false);
		prepared.entryIndex = hoverSnapshot;
		return prepared;
	}
	(void)entry->ActivateItemPrimary(true);
	return {};
}


void Wheel::ClearDepletedConsumables()
{
	std::unique_lock<std::shared_mutex> lock(_lock);
	for (auto& entry : _entries) {
		if (entry) {
			entry->ClearDepletedConsumables();
		}
	}
}

std::shared_ptr<WheelItem> Wheel::GetHoveredSelectedItem()
{
	std::shared_lock<std::shared_mutex> lock(_lock);
	const auto capturedHover = HoverActivationSnapshotPolicy::Capture(_hoveredEntryIdx, _entries.size());
	if (!capturedHover) {
		return {};
	}
	return _entries[static_cast<std::size_t>(*capturedHover)]->GetSelectedItem();
}

PreparedWheelItemActivation Wheel::ActivateHoveredEntrySecondary(bool a_editMode)
{
	// Edit-mode path mutates the entry list, so keep exclusive lock.
	if (a_editMode) {
		std::unique_lock<std::shared_mutex> lock(_lock);
		const int32_t hoverSnapshot = HoverActivationSnapshotPolicy::Load(_hoveredEntryIdx);
		if (!HoverActivationSnapshotPolicy::IsValid(hoverSnapshot, _entries.size())) {
			return {};
		}
		const std::size_t entryOffset = static_cast<std::size_t>(hoverSnapshot);
		std::unique_ptr<WheelEntry>& entry = _entries[entryOffset];
		if (entry->IsEmpty()) { // remove the entry if it's empty
			_entries.erase(_entries.begin() + hoverSnapshot);
			if (hoverSnapshot >= static_cast<int>(_entries.size())) {
				HoverActivationSnapshotPolicy::Store(_hoveredEntryIdx, static_cast<int32_t>(_entries.size()) - 1);
			}
			return {};
		}
		(void)entry->ActivateItemSecondary(true);
		return {};
	}

	// Runtime activation path should not mutate wheel structure. Match primary path
	// by using a shared lock to avoid unnecessary exclusive lock contention.
	std::shared_lock<std::shared_mutex> lock(_lock);
	const int32_t hoverSnapshot = HoverActivationSnapshotPolicy::Load(_hoveredEntryIdx);
	if (!HoverActivationSnapshotPolicy::IsValid(hoverSnapshot, _entries.size())) {
		return {};
	}
	WheelEntry* const entry = _entries[static_cast<std::size_t>(hoverSnapshot)].get();
	if (!entry || entry->IsEmpty()) {
		return {};
	}

	const std::shared_ptr<WheelItem> item = entry->GetSelectedItem();
	// Check if the CURRENTLY SELECTED item is a WheelItemMissing placeholder
	// NOTE: We only check for WheelItemMissing here, NOT IsInPlayerInventory()
	// The full inventory check happens in WheelEntry::ActivateItemSecondary to avoid
	// duplicate IsInPlayerInventory() calls which have side effects (uniqueID updates)
	if (item && dynamic_cast<WheelItemMissing*>(item.get()) != nullptr) {
		return {};
	}
	if (!ShouldActivateWheelItem(item, hoverSnapshot)) {
		return {};
	}

	PreparedWheelItemActivation prepared = entry->ActivateItemSecondary(false);
	prepared.entryIndex = hoverSnapshot;
	return prepared;
}

PreparedWheelItemActivation Wheel::ActivateHoveredEntrySpecial(bool a_editMode)
{
	std::shared_lock<std::shared_mutex> lock(_lock);
	const int32_t hoverSnapshot = HoverActivationSnapshotPolicy::Load(_hoveredEntryIdx);
	if (!HoverActivationSnapshotPolicy::IsValid(hoverSnapshot, _entries.size()) || a_editMode) {  // don't do anything if in edit mode
		return {};
	}
	WheelEntry* const entry = _entries[static_cast<std::size_t>(hoverSnapshot)].get();
	if (!entry) {
		return {};
	}
	const std::shared_ptr<WheelItem> item = entry->GetSelectedItem();
	// Check if the CURRENTLY SELECTED item is a WheelItemMissing placeholder
	// NOTE: We only check for WheelItemMissing here, NOT IsInPlayerInventory()
	// The full inventory check happens in WheelEntry::ActivateItemSpecial to avoid
	// duplicate IsInPlayerInventory() calls which have side effects (uniqueID updates)
	if (item && dynamic_cast<WheelItemMissing*>(item.get()) != nullptr) {
		return {};
	}
	if (!ShouldActivateWheelItem(item, hoverSnapshot)) {
		return {};
	}

	PreparedWheelItemActivation prepared = entry->ActivateItemSpecial(false);
	prepared.entryIndex = hoverSnapshot;
	return prepared;
}

void Wheel::MoveHoveredEntryForward()
{
	std::unique_lock<std::shared_mutex> lock(_lock);  // might involve editing the wheel, so unique lock
	const auto capturedHover = HoverActivationSnapshotPolicy::Capture(_hoveredEntryIdx, _entries.size());
	if (!capturedHover) {
		return;
	}
	const int32_t hoverSnapshot = *capturedHover;
	const int target = hoverSnapshot == static_cast<int32_t>(_entries.size()) - 1 ? 0 : hoverSnapshot + 1;  // wrap around
	std::swap(_entries[static_cast<std::size_t>(hoverSnapshot)], _entries[static_cast<std::size_t>(target)]);
}

void Wheel::MoveHoveredEntryBack()
{
	std::unique_lock<std::shared_mutex> lock(_lock);  // might involve editing the wheel, so unique loc
	const auto capturedHover = HoverActivationSnapshotPolicy::Capture(_hoveredEntryIdx, _entries.size());
	if (!capturedHover) {
		return;
	}
	const int32_t hoverSnapshot = *capturedHover;
	const int target = hoverSnapshot == 0 ? static_cast<int>(_entries.size()) - 1 : hoverSnapshot - 1;  // wrap around
	std::swap(_entries[static_cast<std::size_t>(hoverSnapshot)], _entries[static_cast<std::size_t>(target)]);
}

void Wheel::SerializeIntoJsonObj(nlohmann::json& j_wheel)
{
	if (!_clientTag.empty()) {
		j_wheel["clienttag"] = _clientTag;
	}
	j_wheel["entries"] = nlohmann::json::array();
	for (const std::unique_ptr<WheelEntry>& entry : this->_entries) {
		nlohmann::json j_entry;
		// setup for entry
		entry->SerializeIntoJsonObj(j_entry);

		j_wheel["entries"].push_back(j_entry);
	}
}

std::unique_ptr<Wheel> Wheel::SerializeFromJsonObj(const nlohmann::json& j_wheel, SKSE::SerializationInterface* a_intfc)
{
	std::unique_ptr<Wheel> wheel = std::make_unique<Wheel>();
	if (j_wheel.contains("clienttag") && j_wheel["clienttag"].is_string()) {
		wheel->SetClientTag(j_wheel["clienttag"].get<std::string>());
	}
	if (!j_wheel.contains("entries") || !j_wheel["entries"].is_array()) {
		logger::warn("Deserialize: wheel missing 'entries' array, creating empty wheel");
		return wheel;
	}
	const nlohmann::json& j_entries = j_wheel["entries"];
	
	// Bounds check: reject absurdly large entry counts
	constexpr std::size_t MAX_ENTRIES_PER_WHEEL = 64;
	if (j_entries.size() > MAX_ENTRIES_PER_WHEEL) {
		logger::warn("Deserialize: entry count {} exceeds max {}, creating empty wheel", j_entries.size(), MAX_ENTRIES_PER_WHEEL);
		return wheel;
	}
	
	for (const auto& j_entry : j_entries) {
		try {
			std::unique_ptr<WheelEntry> entry = WheelEntry::SerializeFromJsonObj(j_entry, a_intfc);
			if (entry) {
				wheel->PushEntry(std::move(entry));
			}
		} catch (const std::exception& e) {
			logger::warn("Deserialize: failed to load wheel entry: {}", e.what());
		}
	}
	return wheel;
}

void Wheel::SetHoveredEntryIndex(int a_index)
{
	HoverActivationSnapshotPolicy::Store(_hoveredEntryIdx, static_cast<int32_t>(a_index));
	_mouseHoverState.lastHoverIdx = a_index;
	_mouseHoverState.dwellTime = 0.0f;
	if (a_index >= 0) {
		_mouseHoverState.lastCommittedIdx = a_index;
		_mouseHoverState.hadValidHoverThisOpen = true;
	} else {
		_mouseHoverState.initialized = false;
		_mouseHoverState.lastCursorPos = { 0.0f, 0.0f };
		_mouseHoverState.filteredVelocity = { 0.0f, 0.0f };
		_mouseHoverState.hadValidHoverThisOpen = false;
		_mouseHoverState.loggedStabilizationConfigThisOpen = false;
	}
}

int Wheel::GetNumEntries()
{
	return this->_entries.size();
}

WheelEntry* Wheel::GetEntry(int index)
{
	std::shared_lock<std::shared_mutex> lock(_lock);
	if (index < 0 || index >= static_cast<int>(_entries.size())) {
		return nullptr;
	}
	return _entries[index].get();
}

void Wheel::RemoveEntryByIndex(int index)
{
	std::unique_lock<std::shared_mutex> lock(_lock);
	if (index >= 0 && index < static_cast<int>(_entries.size())) {
		_entries.erase(_entries.begin() + index);
	}
}

bool Wheel::ClearEntryByIndex(int index)
{
	std::shared_lock<std::shared_mutex> lock(_lock);  // shared lock - entry clears itself with its own lock
	if (index < 0 || index >= static_cast<int>(_entries.size())) {
		return false;
	}
	
	WheelEntry* entry = _entries[index].get();
	if (!entry) {
		return false;
	}
	
	// Check if already empty to avoid redundant clears
	if (entry->IsEmpty()) {
		return false;
	}
	
	entry->ClearAllItems();
	return true;
}
