#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <optional>

#include "Wheel.h"
#include "Wheeler.h"
#include "TransformWheelManager.h"
#include <RE/B/BookMenu.h>


#include "imgui.h"
#include "imgui_internal.h"

#include "bin/Rendering/Drawer.h"
#include "bin/Rendering/ResolutionScaleContext.h"
#include "bin/Rendering/TextureManager.h"
#include "bin/Utilities/Utils.h"
#include "bin/Utilities/EquipEventDispatcher.h"
#include "bin/Utilities/HandMemory.h"
#include "bin/Texts.h"
#include "bin/LogGate.h"
#include "bin/InputBroker.h"
#include "MainWheelDebug.h"
#include "bin/UserInput/Controls.h"

#include "WheelItems/WheelItem.h"
#include "WheelItems/WheelItemMutable.h"
#include "WheelItems/WheelItemAlchemy.h"
#include "WheelItems/WheelItemMisc.h"
#include "WheelItems/WheelItemShout.h"
#include "WheelItems/WheelItemMissing.h"
#include "WheelItems/WheelItemFactory.h"
#include "ShoutUtils.h"

namespace
{
	ImVec2 ClampMainWheelCenter(ImVec2 center, float radius, float safePad,
		const Config::MainWheel::LayoutScaling::RuntimeState& scaleState, float* outClampDistance)
	{
		if (scaleState.GameW <= 0.0f || scaleState.GameH <= 0.0f || radius <= 0.0f) {
			if (outClampDistance) {
				*outClampDistance = 0.0f;
			}
			return center;
		}

		const float pad = (std::max)(safePad, 0.0f);
		float minX = radius + pad;
		float maxX = scaleState.GameW - radius - pad;
		float minY = radius + pad;
		float maxY = scaleState.GameH - radius - pad;

		if (maxX < minX) {
			maxX = minX;
		}
		if (maxY < minY) {
			maxY = minY;
		}

		ImVec2 clamped{ std::clamp(center.x, minX, maxX), std::clamp(center.y, minY, maxY) };
		if (outClampDistance) {
			const float dx = clamped.x - center.x;
			const float dy = clamped.y - center.y;
			*outClampDistance = std::sqrt(dx * dx + dy * dy);
		}
		return clamped;
	}

	ImVec2 GetMainWheelCenterWithLayout(float* outClampDistance, bool* outClamped)
	{
		using namespace Config::Styling::Wheel;
		const auto& layoutState = Config::MainWheel::LayoutScaling::Runtime;
		ImVec2 renderSize = ResolutionScale::Context::GetSingleton().GetRenderSize();
		ImVec2 center(renderSize.x / 2 + CenterOffsetX, renderSize.y / 2 + CenterOffsetY);

		if (!layoutState.LayoutActive || !Config::MainWheel::LayoutScaling::ClampToScreen) {
			if (outClampDistance) {
				*outClampDistance = 0.0f;
			}
			if (outClamped) {
				*outClamped = false;
			}
			return center;
		}

		const float safePad = Config::MainWheel::LayoutScaling::SafePadPx * layoutState.CombinedU;
		ImVec2 clamped = ClampMainWheelCenter(center, OuterCircleRadius, safePad, layoutState, outClampDistance);
		if (outClamped) {
			*outClamped = (clamped.x != center.x) || (clamped.y != center.y);
		}
		return clamped;
	}
}
#include "WheelItems/WheelItemSpell.h"
#include "WheelItems/WheelItemWeapon.h"

#include <algorithm>
#include <filesystem>

namespace
{
	double GetSafeInputTimestampSeconds()
	{
		using Clock = std::chrono::steady_clock;
		static const auto s_start = Clock::now();
		return std::chrono::duration<double>(Clock::now() - s_start).count();
	}

	class WheelerPauseMenu : public RE::IMenu
	{
	public:
		static constexpr std::string_view MENU_NAME = "WheelerPauseMenu";

		WheelerPauseMenu()
		{
			menuFlags.set(RE::UI_MENU_FLAGS::kPausesGame);
			// Do not set cursor or modal flags; keep input pass-through.
			depthPriority = 0;
		}

		RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage&) override
		{
			// Always pass on; never consume input.
			return RE::UI_MESSAGE_RESULTS::kPassOn;
		}
	};

	void EnsurePauseMenuRegistered()
	{
		static bool s_registered = false;
		if (s_registered) {
			return;
		}
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return;
		}
		ui->Register(WheelerPauseMenu::MENU_NAME.data(), []() -> RE::IMenu* {
			return new WheelerPauseMenu();
		});
		s_registered = true;
		logger::info("[PauseMenu] Registered WheelerPauseMenu");
	}

	void OpenPauseMenu()
	{
		EnsurePauseMenuRegistered();
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return;
		}
		if (!ui->IsMenuOpen(WheelerPauseMenu::MENU_NAME.data())) {
			RE::UIMessageQueue::GetSingleton()->AddMessage(
				WheelerPauseMenu::MENU_NAME.data(), RE::UI_MESSAGE_TYPE::kShow, nullptr);
		}
	}

	void ClosePauseMenu()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return;
		}
		if (ui->IsMenuOpen(WheelerPauseMenu::MENU_NAME.data())) {
			RE::UIMessageQueue::GetSingleton()->AddMessage(
				WheelerPauseMenu::MENU_NAME.data(), RE::UI_MESSAGE_TYPE::kHide, nullptr);
		}
	}

	using UEFlag = RE::UserEvents::USER_EVENT_FLAG;
	constexpr std::array<UEFlag, 10> kEditModeGameplayBlockFlags = {
		UEFlag::kMovement,
		UEFlag::kLooking,
		UEFlag::kActivate,
		UEFlag::kPOVSwitch,
		UEFlag::kFighting,
		UEFlag::kSneaking,
		UEFlag::kMainFour,
		UEFlag::kWheelZoom,
		UEFlag::kJumping,
		UEFlag::kVATS
	};

	enum class HandMemoryHand
	{
		Left,
		Right
	};

	struct HandMemoryState
	{
		bool was2H = false;
		RE::FormID lastNon2HLeft = 0;
		RE::FormID lastNon2HRight = 0;
		RE::FormID memLeft = 0;
		RE::FormID memRight = 0;
		bool restoreArmed = false;
		double restoreStartTime = 0.0;

		void Reset()
		{
			*this = {};
		}
	};

	static HandMemoryState g_handMemory{};

	static bool IsTwoHandedForm(RE::TESForm* form)
	{
		auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
		if (!weapon) {
			return false;
		}
		const auto weaponType = weapon->GetWeaponType();
		return weapon->IsCrossbow() ||
		       weapon->IsBow() ||
		       weaponType == RE::WEAPON_TYPE::kTwoHandSword ||
		       weaponType == RE::WEAPON_TYPE::kTwoHandAxe;
	}

	static bool IsHandMemoryMenuBlocked()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}
		if (ui->GameIsPaused()) {
			return true;
		}
		return ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME) ||
		       ui->IsMenuOpen(RE::MagicMenu::MENU_NAME) ||
		       ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME);
	}

	static bool EquipFormToHand(RE::PlayerCharacter* pc, RE::FormID formID, HandMemoryHand hand)
	{
		if (!pc || formID == 0) {
			return false;
		}
		RE::TESForm* form = RE::TESForm::LookupByID(formID);
		if (!form) {
			return false;
		}

		if (auto* spell = form->As<RE::SpellItem>()) {
			RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
			if (!aeMan) {
				return false;
			}
			RE::BGSEquipSlot* slot = (hand == HandMemoryHand::Left) ?
				Utils::Slot::GetLeftHandSlot() : Utils::Slot::GetRightHandSlot();
			if (!slot) {
				return false;
			}
			aeMan->EquipSpell(pc, spell, slot);
			return true;
		}

		if (auto* boundObj = form->As<RE::TESBoundObject>()) {
			RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
			if (!aeMan) {
				return false;
			}
			RE::BGSEquipSlot* slot = (hand == HandMemoryHand::Left) ?
				Utils::Slot::GetLeftHandSlot() : Utils::Slot::GetRightHandSlot();
			aeMan->EquipObject(pc, boundObj, nullptr, 1, slot, false, true, true, false);
			return true;
		}

		return false;
	}

	static void UpdateHandMemory()
	{
		namespace HM = Config::WheelBehavior::HandMemory;
		if (!HM::Enabled) {
			if (g_handMemory.was2H || g_handMemory.restoreArmed || g_handMemory.memLeft != 0 || g_handMemory.memRight != 0) {
				g_handMemory.Reset();
			}
			return;
		}

		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !pc->Is3DLoaded()) {
			return;
		}

		RE::TESForm* curLeft = pc->GetEquippedObject(true);
		RE::TESForm* curRight = pc->GetEquippedObject(false);
		const bool in2H = IsTwoHandedForm(curRight) || IsTwoHandedForm(curLeft);

		if (!in2H) {
			g_handMemory.lastNon2HLeft = curLeft ? curLeft->GetFormID() : 0;
			g_handMemory.lastNon2HRight = curRight ? curRight->GetFormID() : 0;
		}

		if (!g_handMemory.was2H && in2H) {
			g_handMemory.memLeft = g_handMemory.lastNon2HLeft;
			g_handMemory.memRight = g_handMemory.lastNon2HRight;
			g_handMemory.restoreArmed = false;
			if (HM::DebugLog) {
				logger::info("[HandMemory] Enter2H capture left={:08X} right={:08X}", g_handMemory.memLeft, g_handMemory.memRight);
			}
		} else if (g_handMemory.was2H && !in2H) {
			g_handMemory.restoreArmed = true;
			g_handMemory.restoreStartTime = GetSafeInputTimestampSeconds();
			if (HM::DebugLog) {
				logger::info("[HandMemory] Exit2H arm restore (left={:08X} right={:08X})", g_handMemory.memLeft, g_handMemory.memRight);
			}
		}

		g_handMemory.was2H = in2H;

		if (!g_handMemory.restoreArmed) {
			return;
		}

		const double now = GetSafeInputTimestampSeconds();
		const double elapsed = now - g_handMemory.restoreStartTime;
		if (elapsed < static_cast<double>(HM::RestoreDelaySeconds)) {
			return;
		}
		if (elapsed > static_cast<double>(HM::RestoreWindowSeconds)) {
			if (HM::DebugLog) {
				logger::info("[HandMemory] Restore window expired (left={:08X} right={:08X})", g_handMemory.memLeft, g_handMemory.memRight);
			}
			g_handMemory.memLeft = 0;
			g_handMemory.memRight = 0;
			g_handMemory.restoreArmed = false;
			return;
		}

		if (IsHandMemoryMenuBlocked()) {
			return;
		}

		if (!HM::RestoreLeftIfEmpty) {
			g_handMemory.memLeft = 0;
		}
		if (!HM::RestoreRightIfEmpty) {
			g_handMemory.memRight = 0;
		}

		if (curLeft != nullptr) {
			g_handMemory.memLeft = 0;
		}
		if (curRight != nullptr) {
			g_handMemory.memRight = 0;
		}

		if (g_handMemory.memRight != 0 && HM::RestoreRightIfEmpty && curRight == nullptr) {
			const RE::FormID toEquip = g_handMemory.memRight;
			g_handMemory.memRight = 0;
			const bool ok = EquipFormToHand(pc, toEquip, HandMemoryHand::Right);
			if (HM::DebugLog) {
				logger::info("[HandMemory] Restore right {:08X} ok={}", toEquip, ok ? 1 : 0);
			}
		}

		curRight = pc->GetEquippedObject(false);
		curLeft = pc->GetEquippedObject(true);

		if (g_handMemory.memLeft != 0 && HM::RestoreLeftIfEmpty && curLeft == nullptr) {
			const RE::FormID toEquip = g_handMemory.memLeft;
			g_handMemory.memLeft = 0;
			const bool ok = EquipFormToHand(pc, toEquip, HandMemoryHand::Left);
			if (HM::DebugLog) {
				logger::info("[HandMemory] Restore left {:08X} ok={}", toEquip, ok ? 1 : 0);
			}
		}

		if (g_handMemory.memLeft == 0 && g_handMemory.memRight == 0) {
			g_handMemory.restoreArmed = false;
		}
	}

}

namespace HandMemory
{
	void NotifyWheelEquipOrCast()
	{
		if (!g_handMemory.restoreArmed && g_handMemory.memLeft == 0 && g_handMemory.memRight == 0) {
			return;
		}
		g_handMemory.restoreArmed = false;
		g_handMemory.memLeft = 0;
		g_handMemory.memRight = 0;
	}
}

namespace
{


	struct AmmoWheelMenuHoldGateState
	{
		bool armed = false;
		bool fired = false;
		bool isGamepad = false;
		std::uint32_t key = 0;
		double startSec = 0.0;
	};

	static AmmoWheelMenuHoldGateState g_ammoWheelMenuHold{};

	static void ResetAmmoWheelMenuHoldGate()
	{
		g_ammoWheelMenuHold = {};
	}

	static bool IsAmmoWheelMenuHoldContextOpen()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}
		return ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME);
	}

	const char* GetResolutionFixModeName(Config::ResolutionFix::Mode mode)
	{
		switch (mode) {
		case Config::ResolutionFix::Mode::ForceDisplayToGame:
			return "ForceDisplayToGame";
		case Config::ResolutionFix::Mode::ForceNone:
			return "ForceNone";
		case Config::ResolutionFix::Mode::Auto:
		default:
			return "Auto";
		}
	}

	const char* GetMainWheelInputActionName(Wheeler::InputAction action)
	{
		switch (action) {
		case Wheeler::InputAction::Toggle:
			return "Toggle";
		case Wheeler::InputAction::ToggleIfInInventory:
			return "ToggleIfInInventory";
		case Wheeler::InputAction::ToggleIfNotInInventory:
			return "ToggleIfNotInInventory";
		case Wheeler::InputAction::ExitWheel:
			return "ExitWheel";
		case Wheeler::InputAction::None:
		default:
			return "None";
		}
	}

	bool IsControllerDebugEnabled()
	{
		return Config::WheelBehavior::Gamepad::DebugController::HasEnabled &&
			Config::WheelBehavior::Gamepad::DebugController::Enabled;
	}

	/// <summary>
	/// Check if dMenu (settings overlay) is currently open.
	/// Used to prevent Wheeler from closing when user interacts with dMenu for real-time editing.
	/// </summary>
	bool IsDMenuOpen()
	{
		RE::UI* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}
		return ui->IsMenuOpen("dmenu") ||
			ui->IsMenuOpen("dmenu_Main") ||
			ui->IsMenuOpen("dMenu") ||
			ui->IsMenuOpen("dMenu_Main");
	}

	struct InventorySelection
	{
		int count = 0;
		bool hasExtraList = false;
		std::uint16_t uniqueID = 0;
		RE::ExtraDataList* extraList = nullptr;
	};

	InventorySelection ResolveInventorySelection(const RE::TESObjectREFR::InventoryItemMap& inv,
		RE::TESBoundObject* obj, std::uint16_t preferredUniqueID)
	{
		InventorySelection result{};
		if (!obj) {
			return result;
		}

		RE::InventoryEntryData* entry = nullptr;
		auto it = inv.find(obj);
		if (it != inv.end()) {
			result.count = it->second.first;
			entry = it->second.second.get();
		} else {
			const RE::FormID formID = obj->GetFormID();
			for (auto& [boundObj, data] : inv) {
				if (boundObj && boundObj->GetFormID() == formID) {
					result.count = data.first;
					entry = data.second.get();
					break;
				}
			}
		}

		if (!entry || !entry->extraLists) {
			return result;
		}

		RE::ExtraDataList* firstList = nullptr;
		std::uint16_t firstUniqueID = 0;

		for (auto* extraList : *entry->extraLists) {
			if (!extraList) {
				continue;
			}
			result.hasExtraList = true;
			if (!firstList) {
				firstList = extraList;
				if (auto* uniqueData = extraList->GetByType<RE::ExtraUniqueID>()) {
					firstUniqueID = uniqueData->uniqueID;
				}
			}

			if (preferredUniqueID != 0) {
				if (auto* uniqueData = extraList->GetByType<RE::ExtraUniqueID>()) {
					if (uniqueData->uniqueID == preferredUniqueID) {
						result.extraList = extraList;
						result.uniqueID = uniqueData->uniqueID;
						return result;
					}
				}
			}
		}

		if (firstList) {
			result.extraList = firstList;
			result.uniqueID = firstUniqueID;
		}
		return result;
	}

	struct ScriptedMiscDispatchDecision
	{
		Config::WheelBehavior::ScriptedMiscDispatchMode mode;
		const char* reason;
	};

	const char* ScriptedMiscDispatchModeToString(Config::WheelBehavior::ScriptedMiscDispatchMode mode)
	{
		using Mode = Config::WheelBehavior::ScriptedMiscDispatchMode;
		switch (mode) {
		case Mode::Auto:
			return "Auto";
		case Mode::EquipObjectOnly:
			return "EquipObjectOnly";
		case Mode::EquipEventOnly:
			return "EquipEventOnly";
		case Mode::TempRefOnEquippedOnly:
			return "TempRefOnEquippedOnly";
		case Mode::LegacyTripleDispatch:
			return "LegacyTripleDispatch";
		default:
			return "Unknown";
		}
	}

	ScriptedMiscDispatchDecision ResolveScriptedMiscDispatchMode(RE::TESObjectMISC* miscItem)
	{
		using Mode = Config::WheelBehavior::ScriptedMiscDispatchMode;
		std::uint32_t raw = Config::WheelBehavior::ScriptedMiscDispatchModeValue;
		if (raw > 4) {
			raw = 0;
		}
		Mode mode = static_cast<Mode>(raw);
		if (mode != Mode::Auto) {
			return { mode, "config" };
		}
		if (miscItem && YpsItems::IsYpsItem(miscItem)) {
			return { Mode::TempRefOnEquippedOnly, "auto=yps" };
		}
		if (miscItem && ShovelItems::IsShovelItem(miscItem)) {
			return { Mode::TempRefOnEquippedOnly, "auto=shovel" };
		}
		return { Mode::EquipObjectOnly, "auto=default_equip_only" };
	}

	bool DispatchTempRefOnEquipped(RE::PlayerCharacter* pc, RE::TESObjectMISC* miscItem)
	{
		if (!pc || !miscItem) {
			return false;
		}
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) {
			return false;
		}

		auto tempRefPtr = pc->PlaceObjectAtMe(miscItem, false);
		RE::TESObjectREFR* tempRef = tempRefPtr.get();
		if (!tempRef) {
			return false;
		}

		RE::VMHandle handle = vm->GetObjectHandlePolicy()->GetHandleForObject(
			RE::FormType::Reference, tempRef);

		if (handle != 0) {
			auto args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(pc));
			vm->SendEvent(handle, RE::BSFixedString("OnEquipped"), args);
			delete args;
		}

		// Queue deletion of temp ref after script has a chance to run
		SKSE::GetTaskInterface()->AddTask([tempRefPtr]() {
			RE::TESObjectREFR* ref = tempRefPtr.get();
			if (ref) {
				ref->Disable();
				ref->SetDelete(true);
			}
		});
		return true;
	}

	bool IsControllerDebugOverlayEnabled()
	{
		return IsControllerDebugEnabled() &&
			Config::WheelBehavior::Gamepad::DebugController::HasOverlay &&
			Config::WheelBehavior::Gamepad::DebugController::Overlay;
	}

	float GetEntryCenterAngleRad(int entryIdx, int entryCount)
	{
		if (entryCount <= 0) {
			return 0.0f;
		}
		const float entryArcSpan = 2.0f * IM_PI / entryCount;
		float innerSpacingRad = 0.0f;
		if (Config::Styling::Wheel::InnerCircleRadius > 0.0f) {
			innerSpacingRad = Config::Styling::Wheel::InnerSpacing / Config::Styling::Wheel::InnerCircleRadius / 2.0f;
		}
		float entryInnerAngleMin = entryArcSpan * (entryIdx - 0.5f) + innerSpacingRad + IM_PI / 2.0f;
		float entryInnerAngleMax = entryArcSpan * (entryIdx + 0.5f) - innerSpacingRad + IM_PI / 2.0f;
		if (entryInnerAngleMax > IM_PI * 2.0f) {
			entryInnerAngleMin -= IM_PI * 2.0f;
			entryInnerAngleMax -= IM_PI * 2.0f;
		}
		return (entryInnerAngleMax - entryInnerAngleMin) * 0.5f + entryInnerAngleMin;
	}

	const char* GetMainWheelInputDecisionName(Wheeler::InputDecision decision)
	{
		switch (decision) {
		case Wheeler::InputDecision::OpenRequested:
			return "OpenRequested";
		case Wheeler::InputDecision::CloseRequested:
			return "CloseRequested";
		case Wheeler::InputDecision::CloseRelease:
			return "CloseRelease";
		case Wheeler::InputDecision::DeniedState:
			return "DeniedState";
		case Wheeler::InputDecision::DeniedMenuBlocked:
			return "DeniedMenuBlocked";
		case Wheeler::InputDecision::DeniedHoldThreshold:
			return "DeniedHoldThreshold";
		case Wheeler::InputDecision::DeniedInventoryState:
			return "DeniedInventoryState";
		case Wheeler::InputDecision::DeniedNoPlayer:
			return "DeniedNoPlayer";
		case Wheeler::InputDecision::DeniedNoUI:
			return "DeniedNoUI";
		case Wheeler::InputDecision::DeniedAmmoWheel:
			return "DeniedAmmoWheel";
		case Wheeler::InputDecision::DeniedConsumed:
			return "DeniedConsumed";
		case Wheeler::InputDecision::None:
		default:
			return "None";
		}
	}

	const char* GetMainWheelInputConsumerName(Wheeler::InputConsumer consumer)
	{
		switch (consumer) {
		case Wheeler::InputConsumer::MainWheel:
			return "MainWheel";
		case Wheeler::InputConsumer::AmmoWheel:
			return "AmmoWheel";
		case Wheeler::InputConsumer::DMenu:
			return "dMenu";
		case Wheeler::InputConsumer::Other:
			return "Other";
		case Wheeler::InputConsumer::None:
		default:
			return "None";
		}
	}

	const char* GetInputDeviceName(RE::INPUT_DEVICE device)
	{
		switch (device) {
		case RE::INPUT_DEVICE::kMouse:
			return "Mouse";
		case RE::INPUT_DEVICE::kKeyboard:
			return "Keyboard";
		case RE::INPUT_DEVICE::kGamepad:
			return "Gamepad";
		default:
			return "Unknown";
		}
	}

	using ReleaseAction = Wheeler::ReleaseAction;

	const char* GetReleaseActionName(ReleaseAction action)
	{
		switch (action) {
		case ReleaseAction::Equip:
			return "equip";
		case ReleaseAction::CastSpell:
			return "cast_spell";
		case ReleaseAction::CastShout:
			return "cast_shout";
		case ReleaseAction::None:
		default:
			return "none";
		}
	}

	const char* GetTargetHandName(Wheeler::TargetHand hand)
	{
		return hand == Wheeler::TargetHand::Left ? "LEFT" : "RIGHT";
	}

	enum class SmartAssignCategory : std::uint32_t
	{
		None = 0,
		Spells = 1,
		Weapons1H = 2,
		Staffs = 4,
		Shields = 8,
		Torches = 16
	};

	const char* GetSmartAssignCategoryName(SmartAssignCategory category)
	{
		switch (category) {
		case SmartAssignCategory::Spells:
			return "Spells";
		case SmartAssignCategory::Weapons1H:
			return "Weapons1H";
		case SmartAssignCategory::Staffs:
			return "Staffs";
		case SmartAssignCategory::Shields:
			return "Shields";
		case SmartAssignCategory::Torches:
			return "Torches";
		case SmartAssignCategory::None:
		default:
			return "None";
		}
	}

	bool ShouldNotifyHandMemory(const std::shared_ptr<WheelItem>& item, ReleaseAction action)
	{
		if (action == ReleaseAction::CastSpell) {
			return true;
		}
		if (action == ReleaseAction::CastShout) {
			return false;
		}
		if (!item) {
			return false;
		}
		if (std::dynamic_pointer_cast<WheelItemSpell>(item)) {
			return true;
		}
		const RE::FormID formId = item->GetFormID();
		RE::TESForm* form = formId != 0 ? RE::TESForm::LookupByID(formId) : nullptr;
		if (!form) {
			return false;
		}
		if (form->As<RE::TESObjectWEAP>()) {
			return true;
		}
		if (auto* armor = form->As<RE::TESObjectARMO>()) {
			if (armor->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kShield)) {
				return true;
			}
		}
		if (form->As<RE::TESObjectLIGH>()) {
			return true;
		}
		if (form->GetFormType() == RE::FormType::Scroll) {
			return true;
		}
		return false;
	}

	RE::MagicSystem::CastingSource GetCastingSourceForHand(Wheeler::TargetHand hand)
	{
		return hand == Wheeler::TargetHand::Left ?
		           RE::MagicSystem::CastingSource::kLeftHand :
		           RE::MagicSystem::CastingSource::kRightHand;
	}

	bool IsPowerSpellType(const RE::SpellItem* spell)
	{
		return Wheeler::IsPowerSpellType(spell);
	}

	bool IsInstantEnabledForSpell(const RE::SpellItem* spell, bool isInTransform)
	{
		return Wheeler::IsInstantEnabledForSpell(spell, isInTransform);
	}

	bool IsBoundWeaponSpell(RE::SpellItem* spell)
	{
		if (!spell) {
			return false;
		}

		for (auto* effect : spell->effects) {
			if (!effect || !effect->baseEffect) {
				continue;
			}
			if (effect->baseEffect->GetArchetype() == RE::EffectSetting::Archetype::kBoundWeapon) {
				return true;
			}
		}

		return false;
	}

	void LogInstantGateDecision(const char* source, const RE::SpellItem* spell, bool isInTransform, bool instantEnabled)
	{
		if (!(Config::WheelBehavior::InstantSpellDebugLog || Config::WheelBehavior::InstantTransformationsDebugLog)) {
			return;
		}
		logger::info(
			"InstantGate[{}]: spell='{}' formId={:08X} spellType={} isPower={} isInTransform={} InstantSpell={} InstantPowers={} InstantTransformations={} instantEnabled={}",
			source ? source : "unknown",
			spell ? spell->GetName() : "null",
			spell ? spell->GetFormID() : 0,
			spell ? static_cast<int>(spell->GetSpellType()) : -1,
			IsPowerSpellType(spell),
			isInTransform,
			Config::WheelBehavior::InstantSpell,
			Config::WheelBehavior::InstantPowers,
			Config::WheelBehavior::InstantTransformations,
			instantEnabled);
	}

	bool IsConcentrationSpellType(const RE::SpellItem* spell)
	{
		return spell && spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration;
	}

	bool IsConcentrationInstantAllowed(const RE::SpellItem* spell)
	{
		return !IsConcentrationSpellType(spell) ||
		       Config::WheelBehavior::InstantSpellConcentrationMode == 1;
	}

	bool IsSpellEquippedInHand(RE::PlayerCharacter* pc, RE::FormID formID, Wheeler::TargetHand hand)
	{
		if (!pc || formID == 0) {
			return false;
		}
		const bool leftHand = (hand == Wheeler::TargetHand::Left);
		if (auto* equipped = pc->GetEquippedObject(leftHand)) {
			return equipped->GetFormID() == formID;
		}
		return false;
	}

	bool ResolveShoutPowerBinding(RE::INPUT_DEVICE& outDevice, std::uint32_t& outIdCode)
	{
		outDevice = RE::INPUT_DEVICE::kKeyboard;
		outIdCode = 0;

		auto* controlMap = RE::ControlMap::GetSingleton();
		auto* userEvents = RE::UserEvents::GetSingleton();
		if (!controlMap || !userEvents) {
			return false;
		}

		const std::uint32_t keyboardId = controlMap->GetMappedKey(userEvents->shout, RE::INPUT_DEVICE::kKeyboard);
		if (keyboardId != 0xFF && keyboardId != 0) {
			outDevice = RE::INPUT_DEVICE::kKeyboard;
			outIdCode = keyboardId;
			return true;
		}

		const std::uint32_t gamepadId = controlMap->GetMappedKey(userEvents->shout, RE::INPUT_DEVICE::kGamepad);
		if (gamepadId != 0xFF && gamepadId != 0) {
			outDevice = RE::INPUT_DEVICE::kGamepad;
			outIdCode = gamepadId;
			return true;
		}

		return false;
	}

	bool TryActivateEquippedShoutOrPowerVanilla(RE::PlayerCharacter* pc, RE::FormID expectedPowerFormID)
	{
		if (!pc) {
			return false;
		}

		auto* controls = RE::PlayerControls::GetSingleton();
		auto* userEvents = RE::UserEvents::GetSingleton();
		if (!controls || !controls->shoutHandler || !userEvents) {
			logger::warn("[PowerPipe] Vanilla activate failed: controls={} shoutHandler={} userEvents={}",
				controls ? 1 : 0, (controls && controls->shoutHandler) ? 1 : 0, userEvents ? 1 : 0);
			return false;
		}

		RE::INPUT_DEVICE device = RE::INPUT_DEVICE::kKeyboard;
		std::uint32_t idCode = 0;
		if (!ResolveShoutPowerBinding(device, idCode)) {
			logger::warn("[PowerPipe] Vanilla activate failed: could not resolve shout/power binding");
			return false;
		}

		if (expectedPowerFormID != 0) {
			const auto* selectedPower = pc->GetActorRuntimeData().selectedPower;
			if (!selectedPower || selectedPower->GetFormID() != expectedPowerFormID) {
				logger::warn("[PowerPipe] Vanilla activate skipped: selectedPower={:08X} expected={:08X}",
					selectedPower ? selectedPower->GetFormID() : 0, expectedPowerFormID);
				return false;
			}
		}

		const auto& shoutEvent = userEvents->shout;
		auto* downEvent = RE::ButtonEvent::Create(device, shoutEvent, idCode, 1.0f, 0.0f);
		if (!downEvent) {
			logger::warn("[PowerPipe] Vanilla activate failed: down event alloc");
			return false;
		}
		controls->shoutHandler->ProcessButton(downEvent, &controls->data);
		RE::free(downEvent);

		// Tap-style press/release mirrors vanilla "press shout/power key once" behavior.
		auto* upEvent = RE::ButtonEvent::Create(device, shoutEvent, idCode, 0.0f, 0.05f);
		if (!upEvent) {
			logger::warn("[PowerPipe] Vanilla activate failed: up event alloc");
			return false;
		}
		controls->shoutHandler->ProcessButton(upEvent, &controls->data);
		RE::free(upEvent);

		logger::info("[PowerPipe] Vanilla activate issued selectedPower={:08X} device={} idCode={} event={}",
			expectedPowerFormID, static_cast<int>(device), idCode, shoutEvent.c_str());
		return true;
	}
}

bool Wheeler::IsPowerSpellType(const RE::SpellItem* spell)
{
	if (!spell) {
		return false;
	}
	const auto spellType = spell->GetSpellType();
	return spellType == RE::MagicSystem::SpellType::kPower ||
	       spellType == RE::MagicSystem::SpellType::kLesserPower ||
	       spellType == RE::MagicSystem::SpellType::kVoicePower;
}

bool Wheeler::IsInstantEnabledForSpell(const RE::SpellItem* spell, bool isInTransform)
{
	if (isInTransform) {
		return Config::WheelBehavior::InstantTransformations;
	}
	if (IsPowerSpellType(spell)) {
		return Config::WheelBehavior::InstantPowers;
	}
	return Config::WheelBehavior::InstantSpell;
}

void Wheeler::PlaySoundByEditorID(const char* a_editorID, float a_volume)
{
	if (!Config::Sounds::EnableSounds) {
		return;
	}
	if (!a_editorID || a_editorID[0] == '\0') {
		return;
	}

	RE::BSSoundHandle handle;
	handle.soundID = static_cast<uint32_t>(-1);
	handle.assumeSuccess = false;
	auto* audioManager = RE::BSAudioManager::GetSingleton();
	if (audioManager) {
		audioManager->BuildSoundDataFromEditorID(handle, a_editorID, 0x10);
		if (handle.IsValid()) {
			handle.SetVolume(a_volume);
			handle.Play();
		}
	}
}

void Wheeler::PlayShoutStageSound(int stage)
{
	if (!Config::Sounds::EnableSounds || !Config::Sounds::EnableShoutStageSounds) {
		return;
	}

	const auto mode = static_cast<Config::ShoutStageSoundMode>(Config::Sounds::ShoutStageSoundMode);
	if (mode == Config::ShoutStageSoundMode::Off) {
		return;
	}

	if (mode == Config::ShoutStageSoundMode::UI) {
		// UI mode: same sound with rising volume per stage
		const char* editorID = Config::Sounds::ShoutUISoundEditorID.c_str();
		float volume = 1.0f;
		switch (stage) {
		case 1: volume = Config::Sounds::ShoutUIStageVolume1; break;
		case 2: volume = Config::Sounds::ShoutUIStageVolume2; break;
		case 3: volume = Config::Sounds::ShoutUIStageVolume3; break;
		}
		PlaySoundByEditorID(editorID, volume);
	} else if (mode == Config::ShoutStageSoundMode::VOC) {
		// VOC mode: distinct sound per stage
		const char* editorID = nullptr;
		switch (stage) {
		case 1: editorID = Config::Sounds::ShoutWord1SoundEditorID.c_str(); break;
		case 2: editorID = Config::Sounds::ShoutWord2SoundEditorID.c_str(); break;
		case 3: editorID = Config::Sounds::ShoutWord3SoundEditorID.c_str(); break;
		}
		if (editorID && editorID[0] != '\0') {
			PlaySoundByEditorID(editorID, 1.0f);
		}
	}
}

void Wheeler::UpdateShoutStageSounds(float hoverTime, RE::FormID shoutFormID)
{
	if (!Config::Sounds::EnableShoutStageSounds) {
		return;
	}

	const auto mode = static_cast<Config::ShoutStageSoundMode>(Config::Sounds::ShoutStageSoundMode);
	if (mode == Config::ShoutStageSoundMode::Off) {
		return;
	}

	// Detect state changes (shout changed or unlock count changed)
	const bool shoutChanged = (shoutFormID != _shoutStageSoundLastShoutID);
	if (shoutChanged) {
		ResetShoutStageSounds();
		_shoutStageSoundLastShoutID = shoutFormID;
		ShoutUtils::InvalidateCacheForShout(shoutFormID);
	}

	// Get unlocked word count for this shout
	RE::TESShout* shout = RE::TESForm::LookupByID<RE::TESShout>(shoutFormID);
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	ShoutUtils::ShoutUnlockState unlockState = ShoutUtils::GetShoutUnlockState(shout, pc);
	int unlockedWords = unlockState.finalCount;
	if (unlockState.computeState == ShoutUtils::UnlockComputeState::Pending &&
		unlockedWords <= 0 &&
		unlockState.learnedCountContig > 0) {
		const int stableCount = unlockState.previousCount > 0 ? unlockState.previousCount : 1;
		unlockedWords = std::clamp((std::max)(1, stableCount), 1, unlockState.learnedCountContig);
		SHOUTPIPE("StageSoundPendingFallback formID={:08X} fallbackUnlocked={} learned={} prev={}",
			shoutFormID, unlockedWords, unlockState.learnedCountContig, unlockState.previousCount);
	}
	unlockedWords = std::clamp(unlockedWords, 0, 3);

	// Detect unlock count change (e.g., console unlock mid-hover)
	const bool unlockChanged = (unlockedWords != _shoutStageSoundLastUnlockedWords);
	if (unlockChanged) {
		_shoutStageSoundLastUnlockedWords = unlockedWords;
	}

	// Apply force-arm/release only when state changes (not every frame)
	const bool applyCapUpdate = shoutChanged || unlockChanged;
	if (applyCapUpdate) {
		// Stage 1: suppress if no words unlocked, release if unlocked
		if (unlockedWords < 1) {
			_shoutStageSoundFired1 = true;
			_shoutStageSoundForced1 = true;
		} else if (_shoutStageSoundForced1) {
			// Word was unlocked mid-hover - release the suppression
			_shoutStageSoundFired1 = false;
			_shoutStageSoundForced1 = false;
		}

		// Stage 2: suppress if < 2 words unlocked, release if unlocked
		if (unlockedWords < 2) {
			_shoutStageSoundFired2 = true;
			_shoutStageSoundForced2 = true;
		} else if (_shoutStageSoundForced2) {
			_shoutStageSoundFired2 = false;
			_shoutStageSoundForced2 = false;
		}

		// Stage 3: suppress if < 3 words unlocked, release if unlocked
		if (unlockedWords < 3) {
			_shoutStageSoundFired3 = true;
			_shoutStageSoundForced3 = true;
		} else if (_shoutStageSoundForced3) {
			_shoutStageSoundFired3 = false;
			_shoutStageSoundForced3 = false;
		}
	}

	// Early exit if no words unlocked
	if (unlockedWords < 1) {
		return;
	}

	// Get sound trigger thresholds (sounds fire at end of fill, not end of hold)
	float sound1At, sound2At, sound3At;
	ShoutUtils::GetSoundTriggerThresholds(sound1At, sound2At, sound3At);

	// Fire sounds when thresholds are crossed
	if (!_shoutStageSoundFired1 && hoverTime >= sound1At) {
		_shoutStageSoundFired1 = true;
		PlayShoutStageSound(1);
	}
	if (!_shoutStageSoundFired2 && hoverTime >= sound2At) {
		_shoutStageSoundFired2 = true;
		PlayShoutStageSound(2);
	}
	if (!_shoutStageSoundFired3 && hoverTime >= sound3At) {
		_shoutStageSoundFired3 = true;
		PlayShoutStageSound(3);
	}
}

void Wheeler::ResetShoutStageSounds()
{
	_shoutStageSoundFired1 = false;
	_shoutStageSoundFired2 = false;
	_shoutStageSoundFired3 = false;
	_shoutStageSoundForced1 = false;
	_shoutStageSoundForced2 = false;
	_shoutStageSoundForced3 = false;
	_shoutStageSoundLastShoutID = 0;
	_shoutStageSoundLastUnlockedWords = -1;
	ShoutUtils::ClearCache();  // Invalidate cached unlock counts
}

// Shared Release-to-Use activation logic used both when the wheel closes and (optionally) while open.
bool Wheeler::TryActivateHoveredEntryRTU(bool logDelaySkip)
{
	if (!Config::WheelBehavior::ReleaseToUse) {
		return false;
	}
	if (_directActivatedThisOpenSession) {
		if (logDelaySkip) {
			logger::info("ReleaseResolve[RTU]: skipped because directActivatedThisOpenSession=true");
		}
		return false;
	}
	if (_activateOnCloseFired) {
		return false;
	}
	if (_editMode) {
		return false;
	}
	if (_activeWheelIdx < 0 || _activeWheelIdx >= _wheels.size()) {
		return false;
	}

	// Anti-slip cancel check: block activation if cursor is in deadzone or lockout
	if (Config::WheelBehavior::RTUAntiSlipEnabled) {
		const float cursorLen = std::sqrt(_cursorPos.x * _cursorPos.x + _cursorPos.y * _cursorPos.y);
		const float maxCursorRadius = getCursorRadiusMax();
		const float rNorm = (maxCursorRadius > 1e-4f) ? (cursorLen / maxCursorRadius) : 0.0f;
		const float strength = std::clamp(Config::WheelBehavior::RTUAntiSlipStrength, 0.0f, 1.0f);
		const float cancelRadiusFrac = 0.08f + 0.14f * strength;
		const double now = ImGui::GetTime();
		
		// Block if in deadzone or lockout period
		if (rNorm < cancelRadiusFrac || now < _antiSlipLockUntil) {
			const double lockRemainRaw = _antiSlipLockUntil - now;
			const float lockRemain = static_cast<float>(lockRemainRaw > 0.0 ? lockRemainRaw : 0.0);
			LOG_INFO(Activation_RTU, "RTU: blocked by anti-slip (rNorm={:.2f}, cancelFrac={:.2f}, lockRemain={:.2f}s)", 
				rNorm, cancelRadiusFrac, lockRemain);
			return false;
		}
	}

	// Get hovered item early to check if it's a shout (for delay bypass)
	std::unique_ptr<Wheel>& activeWheel = _wheels[_activeWheelIdx];
	const int hoveredEntryIndex = activeWheel ? activeWheel->GetHoveredEntryIndex() : -1;
	std::shared_ptr<WheelItem> hoveredItem = activeWheel ? activeWheel->GetHoveredSelectedItem() : nullptr;
	if (!hoveredItem) {
		return false;
	}
	std::shared_ptr<WheelItemSpell> spellItem = std::dynamic_pointer_cast<WheelItemSpell>(hoveredItem);
	std::shared_ptr<WheelItemShout> shoutItem = std::dynamic_pointer_cast<WheelItemShout>(hoveredItem);
	if (hoveredEntryIndex >= 0 && activeWheel) {
		if (WheelEntry* entry = activeWheel->GetEntry(hoveredEntryIndex); entry && entry->IsMissingInInventory()) {
			return false;
		}
	}

	// Check if this is a shout that should bypass the global RTU delay
	const bool isShoutWithBypass = shoutItem && 
		Config::WheelBehavior::InstantShout && 
		Config::WheelBehavior::RTUShout && 
		Config::WheelBehavior::ShoutIgnoreRTUDelay;

	// Apply hover delay check (bypass for shouts with ShoutIgnoreRTUDelay)
	if (!isShoutWithBypass && _hoveredEntryTime < Config::WheelBehavior::HoverActivateDelaySeconds) {
		if (logDelaySkip) {
			LOG_INFO(Activation_ActivateOnClose, "ActivateOnClose: skipped (hoverTime {} < delay {})", _hoveredEntryTime, Config::WheelBehavior::HoverActivateDelaySeconds);
		}
		return false;
	}

	// Item type checks
	if (std::dynamic_pointer_cast<WheelItemAlchemy>(hoveredItem) && !Config::WheelBehavior::RTUAlchemy) {
		return false;
	}
	if (shoutItem && !Config::WheelBehavior::RTUShout) {
		return false;
	}
	if (spellItem && !Config::WheelBehavior::RTUSpell) {
		return false;
	}

	LOG_INFO(Activation_RTU, "RTU: activating (wheel={}, entry={}, hoverTime={})", _activeWheelIdx, hoveredEntryIndex, _hoveredEntryTime);
	bool activated = false;

	const RE::FormID formId = hoveredItem ? hoveredItem->GetFormID() : 0;
	ReleaseAction resolvedAction = ReleaseAction::Equip;

	// Check instant spell with timed threshold (release-to-cast)
	// Compute per-category instantEnabled through centralized gate helper.
	if (spellItem && Config::WheelBehavior::RTUSpell) {
		RE::SpellItem* spell = spellItem->GetSpell();
		const bool isInTransform = !TransformWheelManager::IsPlayerHuman();
		const bool instantEnabled = IsInstantEnabledForSpell(spell, isInTransform);
		LogInstantGateDecision("RTU", spell, isInTransform, instantEnabled);

		if (instantEnabled) {
			const float thresholdSec = Config::WheelBehavior::InstantSpellHoldThresholdMs / 1000.0f;
			const bool reachedThreshold = _hoveredEntryTime >= thresholdSec;
			const bool isConcentration = IsConcentrationSpellType(spell);
			const bool concentrationAllowed = IsConcentrationInstantAllowed(spell);

			// Log threshold status
			if (Config::WheelBehavior::InstantSpellDebugLog || Config::WheelBehavior::InstantTransformationsDebugLog) {
				LOG_INFO(Activation_InstantSpell, "RTU InstantSpell: hoverTime={:.2f}s, threshold={:.2f}s, ready={}, mode={}, cancelled={}, inTransform={}, isConcentration={}, concentrationAllowed={}",
					_hoveredEntryTime,
					thresholdSec,
					reachedThreshold,
					Config::WheelBehavior::InstantSpellConcentrationMode,
					_instantCancelled,
					isInTransform,
					isConcentration,
					concentrationAllowed);
			}

			// Only cast if threshold reached and concentration policy allows this spell.
			if (concentrationAllowed && reachedThreshold && !_instantCancelled && !_instantSuppressForEntry) {
				resolvedAction = ReleaseAction::CastSpell;
			} else {
				// Threshold not met or cancelled - fall through to normal equip
				if (Config::WheelBehavior::InstantSpellDebugLog || Config::WheelBehavior::InstantTransformationsDebugLog) {
					LOG_INFO(Activation_InstantSpell, "RTU InstantSpell: NOT casting (threshold not met, cancelled, suppressed, or concentration disallowed), fallback to equip");
				}
			}
		}
	}

	if (resolvedAction != ReleaseAction::CastSpell &&
		shoutItem && Config::WheelBehavior::InstantShout && Config::WheelBehavior::RTUShout) {
		resolvedAction = ReleaseAction::CastShout;
	}

	logger::info("ReleaseResolve[RTU]: action={} entry={} formId={:08X}",
		GetReleaseActionName(resolvedAction), hoveredEntryIndex, formId);

	const TargetHand resolvedHand = ResolveTargetHandRTU(hoveredEntryIndex, hoveredItem, resolvedAction);
	logger::info("ReleaseResolve[RTU]: hand={} entry={} formId={:08X}",
		GetTargetHandName(resolvedHand), hoveredEntryIndex, formId);

	if (resolvedAction == ReleaseAction::CastSpell && spellItem) {
		RE::SpellItem* spell = spellItem->GetSpell();
		if (spell && IsPowerSpellType(spell)) {
			activated = QueuePowerActivation(spell->GetFormID());
			if (activated) {
				logger::info("ReleaseResolve[RTU]: queued vanilla power activation entry={} formId={:08X}",
					hoveredEntryIndex, spell->GetFormID());
				_rtuConsumedByInstant = true;
			} else {
				logger::info("ReleaseResolve[RTU]: power queue failed, fallback to equip entry={} formId={:08X}",
					hoveredEntryIndex, formId);
				resolvedAction = ReleaseAction::Equip;
			}
		} else {
			const auto castingSource = GetCastingSourceForHand(resolvedHand);
			activated = spellItem->CastImmediate(true, castingSource);
			if (activated) {
				LOG_INFO(Activation_InstantSpell, "RTU: InstantSpell cast on release (Timed) entry={} hand={}",
					hoveredEntryIndex, GetTargetHandName(resolvedHand));
				_rtuConsumedByInstant = true;
			} else {
				logger::info("ReleaseResolve[RTU]: cast failed, fallback to equip entry={} formId={:08X}",
					hoveredEntryIndex, formId);
				resolvedAction = ReleaseAction::Equip;
			}
		}
	}

	if (!activated && resolvedAction == ReleaseAction::CastShout &&
		shoutItem && Config::WheelBehavior::InstantShout && Config::WheelBehavior::RTUShout) {
		activated = shoutItem->CastImmediate(_hoveredEntryTime);
		if (activated) {
			LOG_INFO(Activation_InstantShout, "RTU: InstantShout cast immediate with hoverTime={:.2f}s", _hoveredEntryTime);
		} else {
			logger::info("ReleaseResolve[RTU]: shout cast failed, fallback to equip entry={} formId={:08X}",
				hoveredEntryIndex, formId);
			resolvedAction = ReleaseAction::Equip;
		}
	}

	if (!activated && activeWheel) {
		// Use centralized hand resolution (decoupled from RTU)
		const bool useLeft = (resolvedHand == TargetHand::Left);

		bool equipBlocked = false;
		const char* equipReason = "ok";
		if (spellItem) {
			RE::SpellItem* spell = spellItem->GetSpell();
			if (spell && !IsPowerSpellType(spell)) {
				equipBlocked = IsSpellEquippedInHand(RE::PlayerCharacter::GetSingleton(), formId, resolvedHand);
				if (equipBlocked) {
					equipReason = "blocked_because_target_hand_already_had_it";
				}
			}
		}
		logger::info("ReleaseResolve[RTU]: equip={} reason={} entry={} hand={} formId={:08X}",
			equipBlocked ? "blocked" : "allowed",
			equipReason,
			hoveredEntryIndex,
			GetTargetHandName(resolvedHand),
			formId);

		if (useLeft) {
			activeWheel->ActivateHoveredEntrySecondary(false);
		} else {
			activeWheel->ActivateHoveredEntryPrimary(false);
		}
		activated = true;
		// Record what was applied (prevents snap-back on close)
		_rtuAppliedThisOpen = true;
		_rtuAppliedEntryIdx = hoveredEntryIndex;
		_rtuAppliedLeft = useLeft;

		// Diagnostic log (gated, once per activation)
		LOG_INFO(Activation_RTU, "Activation: source=RTU rtu={}, entry={}, hand={}, action=equip, formId={:08X}",
			Config::WheelBehavior::ReleaseToUse ? "ON" : "OFF",
			hoveredEntryIndex,
			useLeft ? "LEFT" : "RIGHT",
			formId);
	}
	if (activated) {
		if (ShouldNotifyHandMemory(hoveredItem, resolvedAction)) {
			HandMemory::NotifyWheelEquipOrCast();
		}
		_activateOnCloseFired = true;
		// Suppress generic activate sound for shouts when stage sounds are active
		const bool shoutStageSoundsActive = Config::Sounds::EnableShoutStageSounds &&
			Config::Sounds::ShoutStageSoundMode != static_cast<std::uint32_t>(Config::ShoutStageSoundMode::Off);
		if (!(shoutItem && shoutStageSoundsActive)) {
			PlaySoundByEditorID(Config::Sounds::ActivateSoundEditorID.c_str(), Config::Sounds::ActivateSoundVolume);
		}
	}
	return activated;
}

void Wheeler::QueuePoisonApply(RE::FormID a_poisonFormID)
{
	if (a_poisonFormID == 0) {
		return;
	}
	if (_pendingPoisonApplyFormID.has_value()) {
		LOG_WARN(Activation_RTU, "Poison: apply request skipped (already pending): pending={}, new={}", *_pendingPoisonApplyFormID, a_poisonFormID);
		return;
	}
	_pendingPoisonApplyFormID = a_poisonFormID;

	// Ensure the wheel closes without re-triggering RTU activation on close.
	_activateOnCloseFired = true;
	_forceCloseRequested = true;
	LOG_INFO(Activation_RTU, "Poison: queued apply after wheel closes (formID={}, state={})", a_poisonFormID, static_cast<int>(_state));
}

void Wheeler::QueueMiscItemUse(RE::FormID a_miscItemFormID, std::uint16_t a_uniqueID)
{
	if (a_miscItemFormID == 0) {
		return;
	}
	if (_pendingMiscItemUse.has_value()) {
		LOG_WARN(Activation_RTU, "MiscItem: use request skipped (already pending): pending={}, new={}",
			_pendingMiscItemUse->formID, a_miscItemFormID);
		return;
	}
	_pendingMiscItemUse = PendingMiscItemUse{ a_miscItemFormID, a_uniqueID };

	// Ensure the wheel closes without re-triggering RTU activation on close.
	_activateOnCloseFired = true;
	_forceCloseRequested = true;
	LOG_INFO(Activation_RTU, "MiscItem: queued use after wheel closes (formID={}, uniqueID={}, state={})",
		a_miscItemFormID, a_uniqueID, static_cast<int>(_state));
}

void Wheeler::ExecuteScriptedMiscActivation(RE::PlayerCharacter* pc,
	RE::TESObjectMISC* miscItem, RE::ExtraDataList* extraList, std::uint16_t uniqueID)
{
	if (!pc || !miscItem) {
		return;
	}

	constexpr double kDedupeWindowSec = 0.2;
	const RE::FormID formID = miscItem->GetFormID();
	const double now = ImGui::GetTime();
	const double delta = now - _lastMiscDispatchTime;

	if (formID == _lastMiscDispatchFormID && uniqueID == _lastMiscDispatchUniqueID &&
		delta >= 0.0 && delta < kDedupeWindowSec) {
		LOG_INFO(Activation_RTU, "ScriptedMiscUse: dedupe skip formId={:08X} uid={} dtMs={:.0f}",
			formID, uniqueID, delta * 1000.0);
		return;
	}

	_lastMiscDispatchFormID = formID;
	_lastMiscDispatchUniqueID = uniqueID;
	_lastMiscDispatchTime = now;

	const auto decision = ResolveScriptedMiscDispatchMode(miscItem);
	const char* editorID = miscItem->GetFormEditorID();
	LOG_INFO(Activation_RTU, "ScriptedMiscUse: formId={:08X} uid={} editorID='{}' mode={} reason={}",
		formID, uniqueID, editorID ? editorID : "(null)",
		ScriptedMiscDispatchModeToString(decision.mode),
		decision.reason ? decision.reason : "n/a");

	using Mode = Config::WheelBehavior::ScriptedMiscDispatchMode;
	switch (decision.mode) {
	case Mode::EquipObjectOnly:
		{
			RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
			if (!aeMan) {
				LOG_WARN(Activation_RTU, "ScriptedMiscUse: EquipObjectOnly failed (no ActorEquipManager) formId={:08X}", formID);
				return;
			}
			aeMan->EquipObject(pc, miscItem, extraList, 1, nullptr, false, true, true, false);
		}
		break;
	case Mode::EquipEventOnly:
		EquipEventDispatcher::SendPlayerEquipEvent(formID, true, uniqueID);
		break;
	case Mode::TempRefOnEquippedOnly:
		if (!DispatchTempRefOnEquipped(pc, miscItem)) {
			LOG_WARN(Activation_RTU, "ScriptedMiscUse: TempRefOnEquipped failed formId={:08X}", formID);
		}
		break;
	case Mode::LegacyTripleDispatch:
		{
			RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
			if (aeMan) {
				aeMan->EquipObject(pc, miscItem, extraList, 1, nullptr, false, true, true, false);
			} else {
				LOG_WARN(Activation_RTU, "ScriptedMiscUse: Legacy EquipObject failed (no ActorEquipManager) formId={:08X}", formID);
			}
			EquipEventDispatcher::SendPlayerEquipEvent(formID, true, uniqueID);
			DispatchTempRefOnEquipped(pc, miscItem);
		}
		break;
	case Mode::Auto:
	default:
		{
			RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
			if (!aeMan) {
				LOG_WARN(Activation_RTU, "ScriptedMiscUse: Auto fallback EquipObject failed (no ActorEquipManager) formId={:08X}", formID);
				return;
			}
			aeMan->EquipObject(pc, miscItem, extraList, 1, nullptr, false, true, true, false);
		}
		break;
	}
}

void Wheeler::QueueSGTInstrumentSpell(RE::FormID a_spellFormID)
{
	if (a_spellFormID == 0) {
		return;
	}
	if (_pendingSGTInstrumentSpellFormID.has_value()) {
		LOG_WARN(Activation_RTU, "SGTInstrument: spell request skipped (already pending): pending={:08X}, new={:08X}", 
			*_pendingSGTInstrumentSpellFormID, a_spellFormID);
		return;
	}
	_pendingSGTInstrumentSpellFormID = a_spellFormID;

	// Ensure the wheel closes without re-triggering RTU activation on close.
	_activateOnCloseFired = true;
	_forceCloseRequested = true;
	LOG_INFO(Activation_RTU, "SGTInstrument: queued spell cast after wheel closes (formID={:08X}, state={})", 
		a_spellFormID, static_cast<int>(_state));
}

void Wheeler::QueueShoutActivation(RE::FormID a_shoutFormID, float a_hoverTime)
{
	if (a_shoutFormID == 0) {
		return;
	}
	if (_pendingShoutFormID.has_value()) {
		LOG_WARN(Activation_InstantShout, "Shout: activation request skipped (already pending): pending={:08X}, new={:08X}", 
			*_pendingShoutFormID, a_shoutFormID);
		return;
	}
	_pendingShoutFormID = a_shoutFormID;
	_pendingShoutHoverTime = a_hoverTime;

	// Ensure the wheel closes without re-triggering RTU activation on close.
	_activateOnCloseFired = true;
	_forceCloseRequested = true;
	SHOUTPIPE("QueueShoutActivation formID={:08X} hoverTime={:.2f} state={}",
		a_shoutFormID, a_hoverTime, static_cast<int>(_state));
	LOG_INFO(Activation_InstantShout, "Shout: queued activation after wheel closes (formID={:08X}, hoverTime={:.2f}s, state={})", 
		a_shoutFormID, a_hoverTime, static_cast<int>(_state));
}

bool Wheeler::QueuePowerActivation(RE::FormID a_powerFormID)
{
	if (a_powerFormID == 0) {
		return false;
	}
	if (_pendingPowerFormID.has_value()) {
		logger::warn("[PowerPipe] activation request skipped (already pending): pending={:08X}, new={:08X}",
			*_pendingPowerFormID, a_powerFormID);
		return false;
	}

	_pendingPowerFormID = a_powerFormID;

	// Ensure the wheel closes before we invoke the vanilla shout/power pipeline.
	_activateOnCloseFired = true;
	_forceCloseRequested = true;
	logger::info("[PowerPipe] QueuePowerActivation formID={:08X} state={}",
		a_powerFormID, static_cast<int>(_state));
	return true;
}

void Wheeler::QueueDepletedConsumablesCleanup()
{
	_pendingDepletedConsumablesCleanup = true;
}

void Wheeler::QueueBookRead(RE::FormID a_bookFormID)
{
	if (a_bookFormID == 0) {
		return;
	}
	if (_pendingBookReadFormID.has_value()) {
		if (MainWheelDebug::IsEnabled()) {
			MainWheelDebug::LogRateLimited(MainWheelDebug::Category::Input, "BookRead_Skip", 
				"BookRead: skipped (already pending): pending={:08X}, new={:08X}", 
				*_pendingBookReadFormID, a_bookFormID);
		}
		return;
	}
	_pendingBookReadFormID = a_bookFormID;
	
	// Ensure the wheel closes without re-triggering RTU activation
	_activateOnCloseFired = true;
	_forceCloseRequested = true;
	
	if (MainWheelDebug::IsEnabled()) {
		MainWheelDebug::LogRateLimited(MainWheelDebug::Category::Input, "BookRead_Queue",
			"BookRead: queued open after wheel closes (formID={:08X}, state={})", 
			a_bookFormID, static_cast<int>(_state));
	}
}

void Wheeler::QueueConcentrationSpellStop(RE::FormID a_spellFormID, 
	RE::MagicSystem::CastingSource a_castingSource, float a_maxSeconds)
{
	if (a_spellFormID == 0 || a_maxSeconds <= 0.0f) {
		return;
	}

	// Replace any existing pending stop (last cast wins)
	_concentrationStopPending = true;
	_concentrationStopSpellFormID = a_spellFormID;
	_concentrationStopCastingSource = a_castingSource;
	_concentrationStopAtTime = ImGui::GetTime() + static_cast<double>(a_maxSeconds);

	if (Config::WheelBehavior::InstantSpellDebugLog) {
		LOG_INFO(Activation_InstantSpell, "InstantCast: scheduled concentration spell stop (spellFormID={:08X}, source={}, stopAt={:.2f}s from now)",
			a_spellFormID, static_cast<int>(a_castingSource), a_maxSeconds);
	}
}

void Wheeler::QueueInstantCastRefundCheck(RE::FormID a_spellFormID, float a_magickaBefore, int a_effectCountBefore)
{
	if (a_spellFormID == 0) {
		return;
	}
	
	InstantCastRefundCheck check;
	check.spellFormID = a_spellFormID;
	check.magickaBefore = a_magickaBefore;
	check.effectCountBefore = a_effectCountBefore;
	check.queuedTime = ImGui::GetTime();
	check.attemptsRemaining = 3;
	check.delayMs = 150.0f;
	
	_instantCastRefundCheck = check;
	
	if (Config::WheelBehavior::InstantSpellDebugLog) {
		logger::info("InstantCast: queued summon refund check (spellFormID={:08X}, magickaBefore={:.1f}, effectsBefore={})",
			a_spellFormID, a_magickaBefore, a_effectCountBefore);
	}
}

void Wheeler::ProcessPendingActions()
{
	// If any activation asked to force-close the wheel, do it once we're in an opened state.
	if (_forceCloseRequested) {
		if (_state == WheelState::KOpened) {
			_forceCloseRequested = false;
			TryCloseWheeler();
		} else if (_state == WheelState::KClosed) {
			_forceCloseRequested = false;
		}
	}

	using ShoutClock = std::chrono::steady_clock;
	enum class ShoutPipeState : std::uint8_t
	{
		Idle,
		Queued,
		WaitUnlock,
		Holding
	};

	constexpr float kShoutUnlockWaitTimeoutSec = 0.15f;  // 150ms retry window
	constexpr float kShoutTickLogIntervalSec = 0.10f;    // keep debug output readable

	static bool s_shoutTickInitialized = false;
	static ShoutClock::time_point s_shoutLastTick{};
	static float s_shoutHoldElapsedSec = 0.0f;
	static bool s_shoutWaitUnlockActive = false;
	static RE::FormID s_shoutWaitFormID = 0;
	static float s_shoutWaitHoverTime = 1.0f;
	static float s_shoutWaitElapsedSec = 0.0f;
	static std::optional<int> s_shoutForcedUnlockedWords = std::nullopt;
	static bool s_shoutForcedFromFallback = false;
	static float s_shoutTickLogAccumSec = 0.0f;
	static ShoutPipeState s_shoutPipeState = ShoutPipeState::Idle;

	auto getShoutPipeStateName = [](ShoutPipeState state) -> const char* {
		switch (state) {
		case ShoutPipeState::Queued:
			return "Queued";
		case ShoutPipeState::WaitUnlock:
			return "WaitUnlock";
		case ShoutPipeState::Holding:
			return "Holding";
		case ShoutPipeState::Idle:
		default:
			return "Idle";
		}
	};

	auto setShoutPipeState = [&](ShoutPipeState nextState, RE::FormID formID, const char* reason) {
		if (s_shoutPipeState == nextState) {
			return;
		}
		SHOUTPIPE("StateTransition {}->{} formID={:08X} reason={}",
			getShoutPipeStateName(s_shoutPipeState), getShoutPipeStateName(nextState),
			formID, reason ? reason : "-");
		s_shoutPipeState = nextState;
	};

	const auto shoutNow = ShoutClock::now();
	if (!s_shoutTickInitialized) {
		s_shoutLastTick = shoutNow;
		s_shoutTickInitialized = true;
	}
	float shoutDt = std::chrono::duration<float>(shoutNow - s_shoutLastTick).count();
	s_shoutLastTick = shoutNow;
	if (shoutDt < 0.0f || shoutDt > 0.5f) {
		shoutDt = 0.0f;
	}

	if (_shoutHoldActive) {
		setShoutPipeState(ShoutPipeState::Holding, _shoutHoldFormID, "HoldActive");
	} else if (s_shoutWaitUnlockActive) {
		setShoutPipeState(ShoutPipeState::WaitUnlock, s_shoutWaitFormID, "UnlockPending");
	} else if (_pendingShoutFormID.has_value()) {
		setShoutPipeState(ShoutPipeState::Queued, *_pendingShoutFormID, "QueuedActivation");
	} else {
		setShoutPipeState(ShoutPipeState::Idle, 0, "NoPendingWork");
	}

	const bool shoutPipeActive = _pendingShoutFormID.has_value() || s_shoutWaitUnlockActive || _shoutHoldActive;
	if (shoutPipeActive) {
		s_shoutTickLogAccumSec += shoutDt;
		if (s_shoutTickLogAccumSec >= kShoutTickLogIntervalSec) {
			const bool popupOpen = ImGui::IsPopupOpen(_wheelWindowID);
			SHOUTPIPE("Tick state={} dtMs={:.1f} wheelState={} popupOpen={} pending={} wait={} hold={}",
				getShoutPipeStateName(s_shoutPipeState),
				shoutDt * 1000.0f,
				static_cast<int>(_state),
				popupOpen ? 1 : 0,
				_pendingShoutFormID.has_value() ? 1 : 0,
				s_shoutWaitUnlockActive ? 1 : 0,
				_shoutHoldActive ? 1 : 0);
			s_shoutTickLogAccumSec = 0.0f;
		}
	} else {
		s_shoutTickLogAccumSec = 0.0f;
	}

	const bool wheelClosedAndPopupClear = (_state == WheelState::KClosed) && !ImGui::IsPopupOpen(_wheelWindowID);

	// Retry unlock compute while VM callback is pending instead of collapsing to equip-only.
	if (s_shoutWaitUnlockActive && !_shoutHoldActive && wheelClosedAndPopupClear) {
		s_shoutWaitElapsedSec += shoutDt;
		const RE::FormID shoutFormID = s_shoutWaitFormID;
		const float hoverTime = s_shoutWaitHoverTime;
		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		RE::TESShout* shout = RE::TESForm::LookupByID<RE::TESShout>(shoutFormID);

		if (!pc || !shout) {
			logger::warn("Shout: WaitUnlock aborted (pc={} shout={} formID={:08X})",
				pc ? 1 : 0, shout ? 1 : 0, shoutFormID);
			s_shoutWaitUnlockActive = false;
			s_shoutWaitFormID = 0;
			s_shoutWaitHoverTime = 1.0f;
			s_shoutWaitElapsedSec = 0.0f;
			s_shoutForcedUnlockedWords.reset();
			s_shoutForcedFromFallback = false;
			setShoutPipeState(ShoutPipeState::Idle, shoutFormID, "WaitUnlockAbort");
		} else {
			ShoutUtils::InvalidateCacheForShout(shoutFormID);
			SHOUTPIPE("WaitUnlockRetry formID={:08X} elapsedMs={:.1f}", shoutFormID, s_shoutWaitElapsedSec * 1000.0f);
			ShoutUtils::ShoutUnlockState unlockState = ShoutUtils::GetShoutUnlockState(shout, pc);
			const int unlockedWords = unlockState.finalCount;
			const char* detectionMethod = unlockState.method.empty() ? "-" : unlockState.method.c_str();
			const char* failReason = unlockState.failReason.empty() ? "-" : unlockState.failReason.c_str();
			const char* computeState = ShoutUtils::GetComputeStateName(unlockState.computeState);
			SHOUTPIPE("WaitUnlockResult formID={:08X} unlocked={} method={} reliable={} state={} prev={} usedStable={} failReason={} learned={} soul={} spellOwned={} engine={}",
				shoutFormID, unlockedWords, detectionMethod, unlockState.reliable ? "true" : "false",
				computeState, unlockState.previousCount, unlockState.usedLastStable ? "true" : "false", failReason,
				unlockState.learnedCountContig, unlockState.soulUnlockedCountContig, unlockState.spellOwnedCountContig, unlockState.engineCount);

			const bool pendingWithUsableCount =
				(unlockState.computeState == ShoutUtils::UnlockComputeState::Pending) &&
				(unlockedWords > 0);

			if (unlockState.computeState != ShoutUtils::UnlockComputeState::Pending || pendingWithUsableCount) {
				s_shoutForcedUnlockedWords = unlockedWords;
				s_shoutForcedFromFallback = false;
				_pendingShoutFormID = shoutFormID;
				_pendingShoutHoverTime = hoverTime;
				s_shoutWaitUnlockActive = false;
				s_shoutWaitFormID = 0;
				s_shoutWaitHoverTime = 1.0f;
				s_shoutWaitElapsedSec = 0.0f;
				if (pendingWithUsableCount) {
					SHOUTPIPE("WaitUnlockPendingStable formID={:08X} unlocked={} method={} reason={}",
						shoutFormID, unlockedWords, detectionMethod, failReason);
				}
				setShoutPipeState(ShoutPipeState::Queued, shoutFormID,
					pendingWithUsableCount ? "WaitUnlockPendingStable" : "WaitUnlockResolved");
			} else if (s_shoutWaitElapsedSec >= kShoutUnlockWaitTimeoutSec) {
				const int learnedCount = (std::max)(1, unlockState.learnedCountContig);
				int stableCount = unlockState.previousCount;
				if (unlockState.usedLastStable && unlockState.finalCount > 0) {
					stableCount = unlockState.finalCount;
				}
				const int fallbackUnlockedWords = std::clamp((std::max)(1, stableCount), 1, learnedCount);
				s_shoutForcedUnlockedWords = fallbackUnlockedWords;
				s_shoutForcedFromFallback = true;
				_pendingShoutFormID = shoutFormID;
				_pendingShoutHoverTime = hoverTime;
				s_shoutWaitUnlockActive = false;
				s_shoutWaitFormID = 0;
				s_shoutWaitHoverTime = 1.0f;
				s_shoutWaitElapsedSec = 0.0f;
				SHOUTPIPE("WaitUnlockFallback formID={:08X} timeoutMs={:.1f} stableCount={} learnedCount={} fallbackUnlocked={}",
					shoutFormID, kShoutUnlockWaitTimeoutSec * 1000.0f, stableCount, learnedCount, fallbackUnlockedWords);
				setShoutPipeState(ShoutPipeState::Queued, shoutFormID, "WaitUnlockTimeoutFallback");
			}
		}
	}

	// Process shout hold timer with steady-clock dt so it keeps progressing even if wheel state changes.
	if (_shoutHoldActive) {
		s_shoutHoldElapsedSec += shoutDt;
		if (s_shoutHoldElapsedSec >= _shoutHoldDuration) {
			auto* controls = RE::PlayerControls::GetSingleton();
			auto* userEvents = RE::UserEvents::GetSingleton();

			if (controls && controls->shoutHandler && userEvents) {
				const auto& shoutEvent = userEvents->shout;
				logger::info("Shout: sending UP (elapsed={:.2f}s, heldDownSecs={:.2f})",
					s_shoutHoldElapsedSec, _shoutHoldDuration);

				auto* upEvent = RE::ButtonEvent::Create(_shoutBindDevice, shoutEvent, _shoutBindIdCode, 0.0f, _shoutHoldDuration);
				if (upEvent) {
					controls->shoutHandler->ProcessButton(upEvent, &controls->data);
					RE::free(upEvent);
				}
				SHOUTPIPE("AfterKeyUp formID={:08X} device={} idCode={}",
					_shoutHoldFormID, static_cast<int>(_shoutBindDevice), _shoutBindIdCode);

				RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
				if (pc) {
					float cooldownAfter = pc->GetVoiceRecoveryTime();
					logger::info("Shout: voice recovery time after = {} seconds", cooldownAfter);
					SHOUTPIPE("VoiceRecoveryTime formID={:08X} seconds={:.3f}", _shoutHoldFormID, cooldownAfter);
				}
			}

			_shoutHoldActive = false;
			_shoutHoldFormID = 0;
			s_shoutHoldElapsedSec = 0.0f;
			logger::info("Shout: hold sequence completed");
			setShoutPipeState(ShoutPipeState::Idle, 0, "HoldCompleted");
		}
	}

	// Only run queued gameplay actions once the wheel is fully closed (input no longer filtered).
	if (_state != WheelState::KClosed) {
		return;
	}
	// If the ImGui popup is still open, wait until the next frame so we don't open game menus while Wheeler is still on-screen.
	if (ImGui::IsPopupOpen(_wheelWindowID)) {
		return;
	}

	// Process pending power activation through vanilla shout/power key pipeline.
	if (_pendingPowerFormID.has_value() && !_shoutHoldActive) {
		const RE::FormID powerFormID = *_pendingPowerFormID;
		_pendingPowerFormID.reset();

		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			logger::warn("[PowerPipe] pending activation failed: no player");
		} else {
			RE::SpellItem* powerSpell = RE::TESForm::LookupByID<RE::SpellItem>(powerFormID);
			if (!powerSpell) {
				logger::warn("[PowerPipe] pending activation failed: LookupByID {:08X}", powerFormID);
			} else if (!IsPowerSpellType(powerSpell)) {
				logger::warn("[PowerPipe] pending activation skipped: form {:08X} is not power type (spellType={})",
					powerFormID, static_cast<int>(powerSpell->GetSpellType()));
			} else {
				RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
				if (!aeMan) {
					logger::warn("[PowerPipe] pending activation failed: no ActorEquipManager");
				} else {
					RE::TESForm* selectedPower = pc->GetActorRuntimeData().selectedPower;
					if (!selectedPower || selectedPower->GetFormID() != powerFormID) {
						aeMan->EquipSpell(pc, powerSpell, Utils::Slot::GetVoiceSlot());
						selectedPower = pc->GetActorRuntimeData().selectedPower;
						logger::info("[PowerPipe] equipped power selectedPower={:08X} requested={:08X}",
							selectedPower ? selectedPower->GetFormID() : 0, powerFormID);
					} else {
						logger::info("[PowerPipe] equip skipped power already selected formID={:08X}", powerFormID);
					}

					// Run activate on next task tick so equip/close state is committed before firing.
					SKSE::GetTaskInterface()->AddTask([powerFormID]() {
						RE::PlayerCharacter* taskPc = RE::PlayerCharacter::GetSingleton();
						if (!taskPc) {
							logger::warn("[PowerPipe] activate task failed: no player");
							return;
						}
						RE::UI* ui = RE::UI::GetSingleton();
						if (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
							logger::warn("[PowerPipe] activate task skipped: loading menu open");
							return;
						}

						if (!TryActivateEquippedShoutOrPowerVanilla(taskPc, powerFormID)) {
							logger::warn("[PowerPipe] activate task failed formID={:08X}", powerFormID);
							return;
						}

						logger::info("[PowerPipe] activate task succeeded formID={:08X}", powerFormID);
					});
				}
			}
		}
	}

	// Process pending poison apply
	if (_pendingPoisonApplyFormID.has_value()) {
		const RE::FormID poisonFormID = *_pendingPoisonApplyFormID;
		_pendingPoisonApplyFormID.reset();

		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			LOG_WARN(Activation_RTU, "Poison: pending apply failed (no player)");
		} else {
			RE::AlchemyItem* poison = RE::TESForm::LookupByID<RE::AlchemyItem>(poisonFormID);
			if (!poison) {
				Utils::NotificationMessage("Wheeler: Poison not found (may have been removed).");
				LOG_WARN(Activation_RTU, "Poison: pending apply failed (LookupByID failed): {}", poisonFormID);
			} else {
				RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
				if (!aeMan) {
					LOG_WARN(Activation_RTU, "Poison: pending apply failed (no ActorEquipManager)");
				} else {
					LOG_INFO(Activation_RTU, "Poison: executing EquipObject after wheel closed: {}", poison->GetName());
					aeMan->EquipObject(pc, poison);
				}
			}
		}
	}

	// Process pending misc item use (instruments, Yps items, shovels, etc.)
	if (_pendingMiscItemUse.has_value()) {
		const PendingMiscItemUse pending = *_pendingMiscItemUse;
		_pendingMiscItemUse.reset();

		const RE::FormID miscFormID = pending.formID;
		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		RE::TESForm* baseForm = miscFormID ? RE::TESForm::LookupByID(miscFormID) : nullptr;
		RE::TESObjectMISC* miscItem = baseForm ? baseForm->As<RE::TESObjectMISC>() : nullptr;
		
		if (!pc) {
			logger::warn("[MiscItem] pending use failed: no player");
		} else if (!baseForm) {
			logger::warn("[MiscItem] pending use failed: LookupByID {:08X}", miscFormID);
		} else if (!miscItem) {
			RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
			RE::TESBoundObject* boundObj = baseForm->As<RE::TESBoundObject>();
			if (!aeMan || !boundObj) {
				logger::warn("[MiscItem] pending use failed: non-misc form {:08X} has no bound object or equip manager", miscFormID);
			} else {
				const auto inv = pc->GetInventory();
				const auto selection = ResolveInventorySelection(inv, boundObj, pending.uniqueID);
				if (Config::Debug::LogActionPolicy) {
					const char* itemName = baseForm->GetName();
					logger::info("[MiscItem] Deferred fallback: '{}' formID={:08X} formType={} count={} extraList={} uniqueID={}",
						itemName ? itemName : "(null)", miscFormID,
						static_cast<int>(baseForm->GetFormType()),
						selection.count, selection.hasExtraList ? 1 : 0, selection.uniqueID);
				}

				if (selection.count <= 0) {
					if (Config::Debug::LogActionPolicy) {
						logger::info("[MiscItem] Deferred fallback skipped: not in inventory, formID={:08X}", miscFormID);
					}
				} else {
					const RE::BGSEquipSlot* slot = nullptr;
					if (auto* weap = baseForm->As<RE::TESObjectWEAP>()) {
						slot = weap->GetEquipSlot();
					} else if (auto* light = baseForm->As<RE::TESObjectLIGH>()) {
						slot = light->GetEquipSlot();
					}
					aeMan->EquipObject(pc, boundObj, selection.extraList, 1, slot, false, true, true, false);
					EquipEventDispatcher::SendPlayerEquipEvent(miscFormID, true, selection.uniqueID);
				}
			}
		} else {
			const char* itemName = miscItem->GetName();
			const auto inv = pc->GetInventory();
			const auto selection = ResolveInventorySelection(inv, miscItem, pending.uniqueID);
			if (Config::Debug::LogActionPolicy) {
				logger::info("[MiscItem] Deferred use: '{}' formID={:08X} formType={} count={} extraList={} uniqueID={}",
					itemName ? itemName : "(null)", miscFormID,
					static_cast<int>(miscItem->GetFormType()),
					selection.count, selection.hasExtraList ? 1 : 0, selection.uniqueID);
			}

			if (selection.count <= 0) {
				if (Config::Debug::LogActionPolicy) {
					logger::info("[MiscItem] Deferred use skipped: not in inventory, formID={:08X}", miscFormID);
				}
			} else {
				ExecuteScriptedMiscActivation(pc, miscItem, selection.extraList, selection.uniqueID);
			}
		}
	}

	// Process pending SGT instrument spell cast
	// OAR animations use HasSpell condition to detect if player has the instrument spell
	// So we need to ADD the spell to the player, not just cast it
	if (_pendingSGTInstrumentSpellFormID.has_value()) {
		const RE::FormID spellFormID = *_pendingSGTInstrumentSpellFormID;
		_pendingSGTInstrumentSpellFormID.reset();

		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			LOG_WARN(Activation_RTU, "SGTInstrument: pending spell failed (no player)");
		} else {
			RE::SpellItem* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellFormID);
			if (!spell) {
				LOG_WARN(Activation_RTU, "SGTInstrument: pending spell failed (LookupByID failed): {:08X}", spellFormID);
			} else {
				LOG_INFO(Activation_RTU, "SGTInstrument: processing spell after wheel closed: {} ({:08X})", 
					spell->GetName(), spell->GetFormID());
				
				// Check if player already has the spell
				bool hadSpell = pc->HasSpell(spell);
				LOG_INFO(Activation_RTU, "SGTInstrument: player {} spell before", hadSpell ? "HAS" : "does NOT have");
				
				// Add the spell to the player if they don't have it
				// This is what triggers OAR's HasSpell condition for animations
				if (!hadSpell) {
					pc->AddSpell(spell);
					LOG_INFO(Activation_RTU, "SGTInstrument: added spell to player");
				}
				
				// Also cast the spell to trigger the magic effect
				auto* caster = pc->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
				if (caster) {
					caster->CastSpellImmediate(spell, false, pc, 1.0f, false, 0.0f, pc);
					LOG_INFO(Activation_RTU, "SGTInstrument: CastSpellImmediate completed");
				} else {
					LOG_WARN(Activation_RTU, "SGTInstrument: failed to get magic caster");
				}
				
				// Verify spell was added
				bool hasSpellNow = pc->HasSpell(spell);
				LOG_INFO(Activation_RTU, "SGTInstrument: player {} spell after", hasSpellNow ? "HAS" : "does NOT have");
			}
		}
	}

	// Process pending book read
	if (_pendingBookReadFormID.has_value()) {
		const RE::FormID bookFormID = *_pendingBookReadFormID;
		_pendingBookReadFormID.reset();

		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		RE::UI* ui = RE::UI::GetSingleton();
		
		if (!pc || !ui) {
			if (MainWheelDebug::IsEnabled()) {
				MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_Fail", "no player or UI");
			}
		} else if (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
			if (MainWheelDebug::IsEnabled()) {
				MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_Fail", "LoadingMenu open");
			}
		} else {
			RE::TESObjectBOOK* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(bookFormID);
			if (!book) {
				if (MainWheelDebug::IsEnabled()) {
					MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_Fail", "LookupByID failed");
				}
			} else {
				// INVENTORY GUARD: Validate book is still in player inventory before opening
				auto invCounts = pc->GetInventoryCounts();
				auto countIt = invCounts.find(book);
				const int bookCount = (countIt != invCounts.end()) ? countIt->second : 0;
				
				if (bookCount <= 0) {
					// Book is not in inventory (dropped, sold, removed)
					const char* bookName = book->GetName();
					if (MainWheelDebug::IsEnabled()) {
						MainWheelDebug::Log(MainWheelDebug::Category::Input, "InventoryGuard", 
							"Blocked action type=Book name='{}' formID={:08X} reason=NotInInventory",
							bookName ? bookName : "(null)", bookFormID);
					}
					// Do not open BookMenu - item is no longer available
				} else {
					// Book is in inventory - proceed with opening
					// Set toggle-release latch and failsafe timer
					_suppressOpenUntilToggleUp = true;
					_suppressWheelOpenUntil = ImGui::GetTime() + 2.0; // 2 second failsafe
					
					// Log book info for diagnostics
					const char* bookName = book->GetName();
					const bool isNote = book->IsNote();
					
					if (MainWheelDebug::IsEnabled()) {
						MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_Exec", 
							"formID={:08X} name='{}' isNote={} count={}", bookFormID, bookName ? bookName : "(null)", isNote, bookCount);
					}
				
					// Defer book opening to next frame using SKSE task interface
					// This avoids timing conflicts with wheel close happening on the same frame
					SKSE::GetTaskInterface()->AddTask([bookFormID]() {
						RE::TESObjectBOOK* deferredBook = RE::TESForm::LookupByID<RE::TESObjectBOOK>(bookFormID);
						if (!deferredBook) {
							if (MainWheelDebug::IsEnabled()) {
								MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_DeferFail", "LookupByID failed in deferred task");
							}
							return;
						}
						
						// Get the book's description for OpenBookMenu
						RE::BSString desc;
						deferredBook->GetDescription(desc, nullptr);
						
						// Create identity rotation matrix
						RE::NiMatrix3 rot;
						rot.entry[0][0] = 1.0f; rot.entry[0][1] = 0.0f; rot.entry[0][2] = 0.0f;
						rot.entry[1][0] = 0.0f; rot.entry[1][1] = 1.0f; rot.entry[1][2] = 0.0f;
						rot.entry[2][0] = 0.0f; rot.entry[2][1] = 0.0f; rot.entry[2][2] = 1.0f;
						
						// Open the book using vanilla BookMenu::OpenBookMenu
						// Signature: OpenBookMenu(description, extraList, targetRef, book, pos, rot, scale, showNotes)
						RE::BookMenu::OpenBookMenu(desc, nullptr, nullptr, deferredBook, {0, 0, 0}, rot, 1.0f, true);
						
						if (MainWheelDebug::IsEnabled()) {
							MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_Issued", 
								"OpenBookMenu called (deferred), formID={:08X}", bookFormID);
						}
						
						// Schedule a second task to verify the menu opened
						SKSE::GetTaskInterface()->AddTask([bookFormID]() {
							RE::UI* deferredUI = RE::UI::GetSingleton();
							if (deferredUI && deferredUI->IsMenuOpen(RE::BookMenu::MENU_NAME)) {
								if (MainWheelDebug::IsEnabled()) {
									MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_MenuOpen", 
										"BookMenu IS OPEN (formID={:08X})", bookFormID);
								}
							} else {
								if (MainWheelDebug::IsEnabled()) {
									MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_MenuFailed", 
										"BookMenu did NOT open (formID={:08X})", bookFormID);
								}
							}
						});
					});
					
					if (MainWheelDebug::IsEnabled()) {
						MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_Scheduled", 
							"Deferred task queued, latch=ON");
					}
				}
			}
		}
	}

	// Process pending shout activation - Phase 1: Start hold sequence
	// When a shout is queued, we send DOWN event and start a timer.
	// The UP event is sent after the hold duration elapses (in Phase 2 below).
	if (_pendingShoutFormID.has_value() && !_shoutHoldActive) {
		const RE::FormID shoutFormID = *_pendingShoutFormID;
		const float hoverTime = _pendingShoutHoverTime.value_or(1.0f);
		_pendingShoutFormID.reset();
		_pendingShoutHoverTime.reset();

		SHOUTPIPE("ProcessPendingShout begin formID={:08X} hoverTime={:.2f}", shoutFormID, hoverTime);

		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			logger::warn("Shout: pending activation failed (no player)");
			s_shoutForcedUnlockedWords.reset();
			s_shoutForcedFromFallback = false;
			setShoutPipeState(ShoutPipeState::Idle, shoutFormID, "NoPlayer");
		} else {
			RE::TESShout* shout = RE::TESForm::LookupByID<RE::TESShout>(shoutFormID);
			if (!shout) {
				logger::warn("Shout: pending activation failed (LookupByID failed): {:08X}", shoutFormID);
				s_shoutForcedUnlockedWords.reset();
				s_shoutForcedFromFallback = false;
				setShoutPipeState(ShoutPipeState::Idle, shoutFormID, "LookupFailed");
			} else {
				logger::info("Shout: processing activation after wheel closed: {} ({:08X})", 
					shout->GetName(), shout->GetFormID());
				
				// First, equip the shout so it becomes the selected power
				RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
				if (aeMan) {
					RE::TESForm* selectedPower = pc->GetActorRuntimeData().selectedPower;
					if (!selectedPower || selectedPower->GetFormID() != shout->GetFormID()) {
						aeMan->EquipShout(pc, shout);
						logger::info("Shout: equipped shout");
						RE::TESForm* selectedPowerAfter = pc->GetActorRuntimeData().selectedPower;
						SHOUTPIPE("AfterEquipShout formID={:08X} selectedPower={:08X}",
							shout->GetFormID(), selectedPowerAfter ? selectedPowerAfter->GetFormID() : 0);
					} else {
						SHOUTPIPE("EquipShout skipped formID={:08X} selectedPower={:08X}", shout->GetFormID(), selectedPower->GetFormID());
					}
				} else {
					SHOUTPIPE("EquipShout failed: no ActorEquipManager formID={:08X}", shout->GetFormID());
				}
				
				// Determine word stage and hold duration based on staged hover timing
				// Get stage thresholds from single source of truth
				float seg1Done, seg2Done, seg3Done;
				ShoutUtils::GetStageThresholds(seg1Done, seg2Done, seg3Done);

				int unlockedWords = 0;
				const char* detectionMethod = "-";
				const char* failReason = "-";
				const char* computeState = "Known";
				bool proceedWithCast = true;

				if (s_shoutForcedUnlockedWords.has_value()) {
					unlockedWords = *s_shoutForcedUnlockedWords;
					detectionMethod = s_shoutForcedFromFallback ? "PendingFallback" : "PendingResolved";
					failReason = s_shoutForcedFromFallback ? "CallbackPendingTimeoutFallback" : "-";
					computeState = s_shoutForcedFromFallback ? "PendingTimeout" : "Known";
					SHOUTPIPE("AfterUnlockComputeForced formID={:08X} unlocked={} method={} state={} reason={}",
						shout->GetFormID(), unlockedWords, detectionMethod, computeState, failReason);
					s_shoutForcedUnlockedWords.reset();
					s_shoutForcedFromFallback = false;
				} else {
					SHOUTPIPE("BeforeUnlockCompute formID={:08X}", shout->GetFormID());
					ShoutUtils::ShoutUnlockState unlockState = ShoutUtils::GetShoutUnlockState(shout, pc);
					unlockedWords = unlockState.finalCount;
					detectionMethod = unlockState.method.empty() ? "-" : unlockState.method.c_str();
					failReason = unlockState.failReason.empty() ? "-" : unlockState.failReason.c_str();
					computeState = ShoutUtils::GetComputeStateName(unlockState.computeState);
					const bool shoutKnown = shout->GetKnown();
					SHOUTPIPE("AfterUnlockCompute formID={:08X} unlocked={} method={} reliable={} state={} prev={} usedStable={} failReason={} learned={} soul={} spellOwned={} engine={} shoutKnown={} w0L={} w0S={} w1L={} w1S={} w2L={} w2S={}",
						shout->GetFormID(), unlockedWords, detectionMethod, unlockState.reliable ? "true" : "false",
						computeState, unlockState.previousCount, unlockState.usedLastStable ? "true" : "false", failReason,
						unlockState.learnedCountContig, unlockState.soulUnlockedCountContig, unlockState.spellOwnedCountContig, unlockState.engineCount,
						shoutKnown ? "true" : "false",
						unlockState.words[0].learned ? "true" : "false", unlockState.words[0].soulUnlocked ? "true" : "false",
						unlockState.words[1].learned ? "true" : "false", unlockState.words[1].soulUnlocked ? "true" : "false",
						unlockState.words[2].learned ? "true" : "false", unlockState.words[2].soulUnlocked ? "true" : "false");

					if (unlockState.computeState == ShoutUtils::UnlockComputeState::Pending) {
						const bool pendingHasUsableCount = unlockedWords > 0;
						const bool isSingleWordShout = (shout->variations[0].word != nullptr) && (shout->variations[1].word == nullptr);
						const bool useSingleWordPendingFallback =
							!pendingHasUsableCount &&
							isSingleWordShout &&
							(unlockState.learnedCountContig >= 1);

						if (pendingHasUsableCount) {
							SHOUTPIPE("PendingImmediate formID={:08X} unlocked={} method={} reason={} prev={}",
								shout->GetFormID(), unlockedWords, detectionMethod, failReason, unlockState.previousCount);
						} else if (useSingleWordPendingFallback) {
							unlockedWords = 1;
							detectionMethod = "PendingSingleWordFallback";
							failReason = "CallbackPendingSingleWord";
							SHOUTPIPE("PendingImmediateSingleWord formID={:08X} learned={} fallbackUnlocked={}",
								shout->GetFormID(), unlockState.learnedCountContig, unlockedWords);
						} else {
							s_shoutWaitUnlockActive = true;
							s_shoutWaitFormID = shout->GetFormID();
							s_shoutWaitHoverTime = hoverTime;
							s_shoutWaitElapsedSec = 0.0f;
							SHOUTPIPE("WaitUnlockStart formID={:08X} timeoutMs={:.1f} method={} reason={}",
								s_shoutWaitFormID, kShoutUnlockWaitTimeoutSec * 1000.0f, detectionMethod, failReason);
							setShoutPipeState(ShoutPipeState::WaitUnlock, s_shoutWaitFormID, "UnlockPending");
							proceedWithCast = false;
						}
					}
				}

				// Handle 0 unlocked words: reject activation gracefully
				if (proceedWithCast && unlockedWords < 1) {
					logger::info("Shout: 0 words unlocked, equip-only mode (method={})", detectionMethod);
					SHOUTPIPE("CastDeferred formID={:08X} unlocked={} method={} state={} reason={}",
						shout->GetFormID(), unlockedWords, detectionMethod, computeState, failReason);
					setShoutPipeState(ShoutPipeState::Idle, shout->GetFormID(), "NoUnlockedWords");
					proceedWithCast = false;
				}

				if (!proceedWithCast) {
					return;
				}

				// Calculate word stage from hoverTime
				int wordStage = 1;
				if (hoverTime >= seg2Done) {
					wordStage = 3;
				} else if (hoverTime >= seg1Done) {
					wordStage = 2;
				}
				const int originalStage = wordStage;

				// Clamp word stage by unlocked word count
				wordStage = (std::min)(wordStage, unlockedWords);

				// Derive hold duration from clamped stage
				float holdDuration = Config::WheelBehavior::ShoutHoldSecsWord1;
				switch (wordStage) {
				case 2:
					holdDuration = Config::WheelBehavior::ShoutHoldSecsWord2;
					break;
				case 3:
					holdDuration = Config::WheelBehavior::ShoutHoldSecsWord3;
					break;
				default:
					break;
				}
				
				logger::info("Shout: formID={:08X}, unlockedWords={}, method={}, hoverTime={:.2f}s, computedStage={}, clampedStage={}, holdDuration={:.2f}s",
					shout->GetFormID(), unlockedWords, detectionMethod, hoverTime, originalStage, wordStage, holdDuration);
				SHOUTPIPE("CastExecute formID={:08X} unlocked={} method={} state={} stage={} holdDuration={:.2f}",
					shout->GetFormID(), unlockedWords, detectionMethod, computeState, wordStage, holdDuration);
				
				// Resolve shout keybind from ControlMap
				auto* controlMap = RE::ControlMap::GetSingleton();
				RE::INPUT_DEVICE device = RE::INPUT_DEVICE::kKeyboard;
				std::uint32_t idCode = 0;
				if (controlMap) {
					// Try to get the shout key binding
					auto* userEvents = RE::UserEvents::GetSingleton();
					if (userEvents) {
						// Get keyboard binding first
						idCode = controlMap->GetMappedKey(userEvents->shout, RE::INPUT_DEVICE::kKeyboard);
						if (idCode != 0xFF && idCode != 0) {
							device = RE::INPUT_DEVICE::kKeyboard;
							logger::info("Shout: resolved keybind - keyboard idCode={}", idCode);
						} else {
							// Try gamepad
							idCode = controlMap->GetMappedKey(userEvents->shout, RE::INPUT_DEVICE::kGamepad);
							if (idCode != 0xFF && idCode != 0) {
								device = RE::INPUT_DEVICE::kGamepad;
								logger::info("Shout: resolved keybind - gamepad idCode={}", idCode);
							} else {
								// Fallback to keyboard with idCode 0
								device = RE::INPUT_DEVICE::kKeyboard;
								idCode = 0;
								logger::warn("Shout: could not resolve keybind, using fallback");
							}
						}
					}
				}
				
				// Send DOWN event (button pressed)
				auto* controls = RE::PlayerControls::GetSingleton();
				if (!controls || !controls->shoutHandler) {
					logger::warn("Shout: failed to get PlayerControls or shoutHandler");
					setShoutPipeState(ShoutPipeState::Idle, shout->GetFormID(), "NoShoutHandler");
				} else {
					auto* userEvents = RE::UserEvents::GetSingleton();
					if (!userEvents) {
						logger::warn("Shout: failed to get UserEvents");
						setShoutPipeState(ShoutPipeState::Idle, shout->GetFormID(), "NoUserEvents");
					} else {
						const auto& shoutEvent = userEvents->shout;
						
						logger::info("Shout: sending DOWN (device={}, idCode={}, userEvent={})",
							static_cast<int>(device), idCode, shoutEvent.c_str());
						SHOUTPIPE("BeforeKeyDown formID={:08X} device={} idCode={} event={}",
							shout->GetFormID(), static_cast<int>(device), idCode, shoutEvent.c_str());
						
						auto* downEvent = RE::ButtonEvent::Create(device, shoutEvent, idCode, 1.0f, 0.0f);
						if (downEvent) {
							controls->shoutHandler->ProcessButton(downEvent, &controls->data);
							RE::free(downEvent);
						}
						
						// Start hold timer - UP event will be sent after holdDuration
						SHOUTPIPE("BeforeScheduleKeyUp formID={:08X} holdDuration={:.2f} stage={}",
							shout->GetFormID(), holdDuration, wordStage);
						_shoutHoldActive = true;
						_shoutHoldStartTime = ImGui::GetTime();
						_shoutHoldDuration = holdDuration;
						_shoutHoldWordStage = wordStage;
						_shoutHoldFormID = shoutFormID;
						_shoutBindDevice = device;
						_shoutBindIdCode = idCode;
						s_shoutHoldElapsedSec = 0.0f;
						
						logger::info("Shout: scheduled UP in {:.2f}s (stage={})", holdDuration, wordStage);
						setShoutPipeState(ShoutPipeState::Holding, shoutFormID, "KeyDownSent");
					}
				}
			}
		}
	}
	
	// Depleted consumables cleanup (removes alchemy items with 0 count from their slots).
	// Runs only when requested and only when the wheel is fully closed.
	if (_pendingDepletedConsumablesCleanup && Config::WheelBehavior::ClearDepletedConsumables) {
		_pendingDepletedConsumablesCleanup = false;
		for (auto& wheel : _wheels) {
			if (wheel) {
				wheel->ClearDepletedConsumables();
			}
		}
	}

	// Concentration spell timed-stop: check if we need to stop a concentration spell
	// This runs every frame regardless of wheel state (spell keeps casting even if wheel closes)
	if (_concentrationStopPending) {
		const double now = ImGui::GetTime();
		if (now >= _concentrationStopAtTime) {
			// Time to stop the concentration spell
			RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
			if (pc) {
				RE::MagicCaster* caster = pc->GetMagicCaster(_concentrationStopCastingSource);
				if (caster) {
					// Check if still casting the same spell
					RE::MagicItem* currentSpell = caster->currentSpell;
					if (currentSpell && currentSpell->GetFormID() == _concentrationStopSpellFormID) {
						// Stop the casting
						caster->InterruptCast(false);
						const float elapsed = static_cast<float>(now - (_concentrationStopAtTime - Config::WheelBehavior::InstantSpellConcentrationMaxSeconds));
						if (Config::WheelBehavior::InstantSpellDebugLog) {
							logger::info("InstantCast: stopped concentration spell '{}' after {:.2f}s",
								currentSpell->GetName(), elapsed);
						}
					} else if (Config::WheelBehavior::InstantSpellDebugLog) {
						logger::info("InstantCast: concentration stop skipped (spell changed or finished casting)");
					}
				}
			}
			// Clear state regardless
			_concentrationStopPending = false;
			_concentrationStopSpellFormID = 0;
		}
	}
	
	// Instant cast summon refund check: verify summon succeeded after delay
	if (_instantCastRefundCheck.has_value()) {
		const double now = ImGui::GetTime();
		auto& check = *_instantCastRefundCheck;
		const double elapsedMs = (now - check.queuedTime) * 1000.0;
		
		if (elapsedMs >= check.delayMs) {
			// Time to check if summon succeeded
			RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
			bool shouldRefund = false;
			float refundAmount = 0.0f;
			
			if (pc) {
				// Check if active effect count increased (new summon spawned)
				int effectCountNow = Utils::Magic::CountActiveEffectsFromSpell(check.spellFormID);
				float currentMagicka = pc->AsActorValueOwner()->GetActorValue(RE::ActorValue::kMagicka);
				float magickaDiff = check.magickaBefore - currentMagicka;
				
				// Summon failed if effect count didn't increase AND magicka was consumed
				if (effectCountNow <= check.effectCountBefore && magickaDiff > 0.5f) {
					shouldRefund = true;
					refundAmount = magickaDiff;
				}
				
				if (Config::WheelBehavior::InstantSpellDebugLog) {
					logger::info("InstantCast: summon refund check (attempt {}/3) - effectsBefore={}, effectsNow={}, magickaBefore={:.1f}, magickaNow={:.1f}, diff={:.1f}, refund={}",
						4 - check.attemptsRemaining, check.effectCountBefore, effectCountNow, check.magickaBefore, currentMagicka, magickaDiff, shouldRefund);
				}
			}
			
			if (shouldRefund && pc) {
				// Refund the magicka
				pc->AsActorValueOwner()->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, refundAmount);
				if (Config::WheelBehavior::InstantSpellDebugLog) {
					logger::info("InstantCast: REFUNDED {:.1f} magicka for failed summon (spellFormID={:08X})",
						refundAmount, check.spellFormID);
				}
				_instantCastRefundCheck.reset();
			} else {
				// Either succeeded or need to retry
				check.attemptsRemaining--;
				if (check.attemptsRemaining <= 0) {
					// Out of attempts - assume success (effect count increased or magicka unchanged)
					if (Config::WheelBehavior::InstantSpellDebugLog) {
						logger::info("InstantCast: summon refund check complete - no refund needed");
					}
					_instantCastRefundCheck.reset();
				} else {
					// Schedule next check
					check.queuedTime = now;
				}
			}
		}
	}
}

void Wheeler::Update(float a_deltaTime)
{
	InputBroker::RefreshConfigFromSettings();
	InputBroker::RefreshWheelerReservations();
	InputBroker::SyncWheelerActiveOwner(IsWheelerOpen(), IsAmmoWheelOpen());

	// BookMenu open probe: verify if BookMenu actually opened after book read
	if (_bookMenuProbeFrames > 0) {
		_bookMenuProbeFrames--;
		RE::UI* ui = RE::UI::GetSingleton();
		if (ui && ui->IsMenuOpen(RE::BookMenu::MENU_NAME)) {
			if (MainWheelDebug::IsEnabled()) {
				MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_MenuOpen", 
					"BookMenu IS OPEN (formID={:08X})", _bookMenuProbeFormID);
			}
			_bookMenuProbeFrames = 0; // Stop probing
		} else if (_bookMenuProbeFrames == 0) {
			if (MainWheelDebug::IsEnabled()) {
				MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_MenuFailed", 
					"BookMenu did NOT open within 30 frames (formID={:08X})", _bookMenuProbeFormID);
			}
		}
	}

	const bool perfEnabled = MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Perf);
	const auto perfStart = perfEnabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
	// dMenu does not always emit a reliable ModCallback event on every setup, so also poll for INI changes.
	// This makes slider/color changes apply without restarting the game.
	{
		static float pollAccumSeconds = 0.0f;
		static bool initialized = false;
		static std::filesystem::file_time_type lastWriteTime{};

		pollAccumSeconds += a_deltaTime;
		if (pollAccumSeconds >= 0.25f) {
			pollAccumSeconds = 0.0f;
			if (ImGui::GetCurrentContext()) {
				std::error_code ec;
				const char* preferred = "Data\\SKSE\\Plugins\\wheeler\\wheelBehavior.ini";
				const char* legacyPath = "Data\\SKSE\\Plugins\\wheeler\\InstantUse.ini";
				const bool preferExists = std::filesystem::exists(preferred, ec) && !ec;
				if (ec) {
					ec.clear();
				}
				const auto current = std::filesystem::last_write_time(preferExists ? preferred : legacyPath, ec);
				if (!ec) {
					if (!initialized) {
						lastWriteTime = current;
						initialized = true;
					} else if (current != lastWriteTime) {
						lastWriteTime = current;
						Config::ReadStyleConfig();
						Config::ResetScaleBaseCapture();
						Config::OffsetSizingToViewport();
						Config::OffsetAmmoWheelSizingToViewport();
						MainWheelDebug::LogRateLimited(MainWheelDebug::Category::Config, "ini_reload",
							"Reloaded wheelBehavior.ini (InstantSpell={}, InstantPowers={}, InstantTransformations={}, Cooldowns.ContentDimAlpha={:.2f}, Cooldowns.SelectedIndicatorEnabled={}, Sounds.EnableSounds={})",
							Config::WheelBehavior::InstantSpell,
							Config::WheelBehavior::InstantPowers,
							Config::WheelBehavior::InstantTransformations,
							Config::Cooldowns::ContentDimAlpha,
							Config::Cooldowns::SelectedIndicatorEnabled,
							Config::Sounds::EnableSounds);
						logger::debug("Config: reloaded wheel behavior settings (InstantSpell={}, InstantPowers={}, InstantTransformations={}, Cooldowns.ContentDimAlpha={}, Cooldowns.SelectedIndicatorEnabled={}, Cooldowns.SelectedIndicatorTintColor={}, Sounds.EnableSounds={})",
							Config::WheelBehavior::InstantSpell,
							Config::WheelBehavior::InstantPowers,
							Config::WheelBehavior::InstantTransformations,
							Config::Cooldowns::ContentDimAlpha,
							Config::Cooldowns::SelectedIndicatorEnabled,
							Config::Cooldowns::SelectedIndicatorTintColor,
							Config::Sounds::EnableSounds);
					}
				}
			}
		}
	}

	UpdateHandMemory();
	TransformWheelManager::Update();

	std::shared_lock<std::shared_mutex> lock(_wheelDataLock);
	using namespace Config::Styling::Wheel;
	if (!RE::PlayerCharacter::GetSingleton() || !RE::PlayerCharacter::GetSingleton()->Is3DLoaded()) {
		return;
	}
	// begin draw
	auto ui = RE::UI::GetSingleton();
	if (!ui) {
		return;
	}

	// Check for resolution change and recalculate sizing if needed
	auto& resolutionContext = ResolutionScale::Context::GetSingleton();
	resolutionContext.Update();
	ImVec2 currentDisplaySize = resolutionContext.GetRenderSize();
	if (_lastDisplaySize.x != 0.f && _lastDisplaySize.y != 0.f) {
		if (currentDisplaySize.x != _lastDisplaySize.x || currentDisplaySize.y != _lastDisplaySize.y) {
			MainWheelDebug::LogRateLimited(MainWheelDebug::Category::Scaling, "render_size_change",
				"Render size change detected ({}x{} -> {}x{}), recalculating layout",
				_lastDisplaySize.x, _lastDisplaySize.y, currentDisplaySize.x, currentDisplaySize.y);
			logger::info("Wheeler: Render size change detected ({}x{} -> {}x{}), recalculating layout",
				_lastDisplaySize.x, _lastDisplaySize.y, currentDisplaySize.x, currentDisplaySize.y);
			Config::OffsetSizingToViewport();
			Config::OffsetAmmoWheelSizingToViewport();
		}
	}
	_lastDisplaySize = currentDisplaySize;

	// AmmoWheel inventory hold-to-open gate.
	UpdateAmmoWheelMenuHoldGate();

	// Update Ammo Wheel (runs independently of main wheel)
	if (_ammoWheel) {
		_ammoWheel->Update(a_deltaTime);
	}

	if (_state == WheelState::KClosed) {                  // should close
		// SAFETY: If wheel is closed but timescale flag is still set, restore it
		// This catches edge cases where close path was bypassed somehow
		if (_wheelerModifiedTimeScale || _wheelerOwnedPauseMenu) {
			if (_wheelerModifiedTimeScale) {
				logger::warn("[TimeDilation] Safety restore: MainWheel closed but timescale flag was stuck");
			}
			if (_wheelerOwnedPauseMenu) {
				logger::warn("[TimeDilation] Safety restore: MainWheel closed but pause menu ownership was stuck");
			}
			EnsureTimescaleRestored();
		}
		DisableEditModeGameplayInputBlock();
		
		if (ImGui::IsPopupOpen(_wheelWindowID)) {         // if it's open, close it
			ImGui::SetNextWindowPos(ImVec2(-100, -100));  // set the pop-up pos to be outside the screen space.
			ImGui::BeginPopup(_wheelWindowID);
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			if (_activeWheelIdx >= 0 && _activeWheelIdx < _wheels.size()) {
				_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);  // reset active entry on close
			}
			//ImGui::GetIO().MouseDrawCursor = false;
			if (_editMode) {
				showEditModeVanillaMenus(ui);
			}
		}
		ProcessPendingActions();
		return;
	}
	// state is opened, opening, or closing, draw the wheel with different alphas.

	if (!ImGui::IsPopupOpen(_wheelWindowID)) {  // should open, but not opened yet
		//ImGui::GetIO().MouseDrawCursor = true;
		ImGui::OpenPopup(_wheelWindowID);
		if (_activeWheelIdx >= 0 && _activeWheelIdx < _wheels.size()) {
			_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);  // reset active entry on reopen
		}
		int lastIdx = -1;
		const bool useLastSelectionOnOpen = Config::WheelBehavior::Gamepad::Open::HasUseLastSelectionOnOpen &&
			Config::WheelBehavior::Gamepad::Open::UseLastSelectionOnOpen &&
			IsLastInputGamepad();
		if (useLastSelectionOnOpen && _activeWheelIdx >= 0 && _activeWheelIdx < _wheels.size()) {
			if (_lastHoveredEntryByWheel.size() != _wheels.size()) {
				_lastHoveredEntryByWheel.resize(_wheels.size(), -1);
			}
			lastIdx = _lastHoveredEntryByWheel[_activeWheelIdx];
			if (lastIdx >= 0 && _wheels[_activeWheelIdx]) {
				const int entryCount = _wheels[_activeWheelIdx]->GetNumEntries();
				if (lastIdx >= entryCount) {
					lastIdx = -1;
				}
			} else {
				lastIdx = -1;
			}
		}
		if (lastIdx >= 0 && _activeWheelIdx >= 0 && _activeWheelIdx < _wheels.size() && _wheels[_activeWheelIdx]) {
			const float angle = GetEntryCenterAngleRad(lastIdx, _wheels[_activeWheelIdx]->GetNumEntries());
			const float cursorRadius = getCursorRadiusMax();
			_cursorPos = { cosf(angle) * cursorRadius, sinf(angle) * cursorRadius };
			_wheels[_activeWheelIdx]->SetHoveredEntryIndex(lastIdx);
			if (IsControllerDebugEnabled()) {
				logger::info("[Controller] Open init hover={}", lastIdx);
			}
		} else {
			_cursorPos = { 0, 0 };  // reset cursor pos
		}
	}

	//ImGui::GetWindowDrawList()->AddCircleFilled(_cursorPos, 10, ImGuiCol_ButtonHovered, 32);

	ImGui::SetNextWindowPos(ImVec2(-100, -100));  // set the pop-up pos to be outside the screen space.

	if (shouldBeInEditMode(ui)) {
		if (!_editMode) {
			enterEditMode();
		}
		hideEditModeVanillaMenus(ui);
	} else {
		if (_editMode) {
			exitEditMode();
		}
	}
	const bool wantGameplayBlock = (_state != WheelState::KClosed) && _editMode;
	if (wantGameplayBlock) {
		EnableEditModeGameplayInputBlock();
	} else {
		DisableEditModeGameplayInputBlock();
	}

	// Use modal popup when in edit mode OR when dMenu is open for real-time editing.
	// Modal popups don't close on click-outside, allowing users to interact with dMenu settings
	// while keeping Wheeler visible for real-time visual feedback.
	const bool useModalPopup = _editMode || IsDMenuOpen();
	bool poppedUp = useModalPopup ? ImGui::BeginPopupModal(_wheelWindowID) : ImGui::BeginPopup(_wheelWindowID);
	if (poppedUp) {
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRectFullScreen();

		// update fade timer, alpha and wheel state.
		_openTimer += a_deltaTime;

		float fadeLerp = 1.0f;
		switch (_state) {
		case WheelState::KOpening:
			fadeLerp = std::fminf(_openTimer / Config::Animation::FadeTime, 1.f);
			if (_openTimer >= Config::Animation::FadeTime) {
				_state = WheelState::KOpened;
			}
			break;
		case WheelState::KClosing:
			_closeTimer += a_deltaTime;
			fadeLerp = std::fmaxf(1 - _closeTimer / Config::Animation::FadeTime, 0.f);
			if (_closeTimer >= Config::Animation::FadeTime) {
				CloseWheeler();
				_closeTimer = 0;
			}
			break;
		}

		if (IsControllerDebugEnabled() && _state != _lastLoggedWheelState) {
			logger::info("[Controller] WheelState={}", GetWheelStateName(_state));
			_lastLoggedWheelState = _state;
		}
		
		DrawArgs drawArgs;
		drawArgs.alphaMult = fadeLerp;
		// get ready to draw the wheel

		// lerp wheel center
		ImVec2 wheelCenter = getWheelCenter();
		wheelCenter.y += (1 - fadeLerp) * Config::Animation::ToggleVerticalFadeDistance;
		wheelCenter.x += (1 - fadeLerp) * Config::Animation::ToggleHorizontalFadeDistance;
		Config::MainWheel::LayoutScaling::UpdateRuntimeState();
		const auto& layoutState = Config::MainWheel::LayoutScaling::Runtime;
		if (layoutState.LayoutActive && Config::MainWheel::LayoutScaling::ClampToScreen) {
			const float safePad = Config::MainWheel::LayoutScaling::SafePadPx * layoutState.CombinedU;
			wheelCenter = ClampMainWheelCenter(wheelCenter, Config::Styling::Wheel::OuterCircleRadius, safePad, layoutState, nullptr);
		}

		RE::TESObjectREFR::InventoryItemMap inv = RE::PlayerCharacter::GetSingleton()->GetInventory();

		float cursorAngle = atan2f(_cursorPos.y, _cursorPos.x);  // where the cursor is pointing to

		if (_wheels.empty()) {
			Drawer::draw_text(wheelCenter.x, wheelCenter.y, Texts::GetText(Texts::TextType::NoWheelPresent), C_SKYRIMWHITE, 40.F, drawArgs);
		} else {
			int safeActiveWheelIdx = -1;
			if (_activeWheelIdx >= 0 && _activeWheelIdx < _wheels.size() && _wheels[_activeWheelIdx]) {
				safeActiveWheelIdx = _activeWheelIdx;
			} else {
				for (int i = 0; i < _wheels.size(); ++i) {
					if (_wheels[i]) {
						safeActiveWheelIdx = i;
						break;
					}
				}
				if (safeActiveWheelIdx != -1) {
					logger::warn("Wheeler: active wheel index invalid (active={}, wheels={}), using {} for rendering", _activeWheelIdx, _wheels.size(), safeActiveWheelIdx);
				} else {
					logger::warn("Wheeler: wheels vector has no valid wheel objects (active={}, wheels={})", _activeWheelIdx, _wheels.size());
				}
			}

			if (safeActiveWheelIdx == -1) {
				Drawer::draw_text(wheelCenter.x, wheelCenter.y, Texts::GetText(Texts::TextType::NoWheelPresent), C_SKYRIMWHITE, 40.F, drawArgs);
				drawList->PopClipRect();
				ImGui::EndPopup();
				return;
			}

			if (_lastHoveredEntryByWheel.size() != _wheels.size()) {
				_lastHoveredEntryByWheel.resize(_wheels.size(), -1);
			}
			
			// Cursor center state + radial metrics.
			const float cursorRadius = std::sqrt(_cursorPos.x * _cursorPos.x + _cursorPos.y * _cursorPos.y);
			const float maxCursorRadius = getCursorRadiusMax();
			const float cursorRadiusNorm = (maxCursorRadius > 1e-4f) ? (cursorRadius / maxCursorRadius) : 0.0f;
			bool isCursorCentered = _cursorPos.x == 0 && _cursorPos.y == 0;
			// With AutoCenterRestSnap OFF, allow a small manual center-rest zone without forcing auto recenter.
			if (!isCursorCentered && IsLastInputGamepad() && !Config::WheelBehavior::Gamepad::Nav::AutoCenterRestSnap) {
				float manualCenterRestRadiusFrac = 0.10f;
				if (Config::WheelBehavior::Gamepad::Nav::HasInnerDeadzone) {
					manualCenterRestRadiusFrac =
						std::clamp(Config::WheelBehavior::Gamepad::Nav::InnerDeadzone * 1.0f, 0.08f, 0.20f);
				}
				if (cursorRadiusNorm <= manualCenterRestRadiusFrac) {
					isCursorCentered = true;
				}
			}
			
			// Anti-slip only applies in RTU mode when enabled
			const bool antiSlipActive = Config::WheelBehavior::ReleaseToUse && 
				Config::WheelBehavior::RTUAntiSlipEnabled && !_editMode;
			const bool gateHoverTime = Config::WheelBehavior::Gamepad::Nav::HasIntentMagnitude &&
				IsLastInputGamepad() && !_gamepadIntentActive;
			
			if (antiSlipActive) {
				// Normalize cursor position using the actual cursor radius bounds
				const float rNorm = cursorRadiusNorm;
				
				// Map strength (0..1) to internal parameters - tuned for normalized radius
				const float strength = std::clamp(Config::WheelBehavior::RTUAntiSlipStrength, 0.0f, 1.0f);
				const float cancelRadiusFrac = 0.08f + 0.14f * strength;  // 0.08 to 0.22
				const float cancelLockSecs = 0.00f + 0.08f * strength;    // 0.00 to 0.08
				const float switchDwellSecs = 0.00f + 0.08f * strength;   // 0.00 to 0.08
				
				const double now = ImGui::GetTime();
				
				// Check if cursor is in cancel deadzone (using normalized radius)
				const bool inDeadzone = rNorm < cancelRadiusFrac;
				
				if (inDeadzone) {
					// Cursor entered deadzone - start lockout
					if (_antiSlipLockUntil < now) {
						_antiSlipLockUntil = now + cancelLockSecs;
					}
					// Force cursor centered (no selection)
					isCursorCentered = true;
					// Clear pending dwell and reset hover time
					_antiSlipPendingIdx = -1;
					_hoveredEntryTime = 0.f;
				} else if (now < _antiSlipLockUntil) {
					// Still in lockout period - force cursor centered
					isCursorCentered = true;
					_antiSlipPendingIdx = -1;
					_hoveredEntryTime = 0.f;
				} else {
					// Outside deadzone and lockout expired - apply dwell logic
					// Get what the wheel would select based on cursor angle
					int candidateIdx = -1;
					const int numEntries = _wheels[safeActiveWheelIdx]->GetNumEntries();
					if (numEntries > 0) {
						const float entryArcSpan = 2.0f * IM_PI / numEntries;
						const float innerSpacingRad = Config::Styling::Wheel::InnerSpacing / Config::Styling::Wheel::InnerCircleRadius / 2.0f;
						for (int i = 0; i < numEntries; ++i) {
							float entryInnerAngleMin = entryArcSpan * (i - 0.5f) + innerSpacingRad + IM_PI / 2.0f;
							float entryInnerAngleMax = entryArcSpan * (i + 0.5f) - innerSpacingRad + IM_PI / 2.0f;
							if (entryInnerAngleMax > IM_PI * 2) {
								entryInnerAngleMin -= IM_PI * 2;
								entryInnerAngleMax -= IM_PI * 2;
							}
							if (cursorAngle >= entryInnerAngleMin && cursorAngle < entryInnerAngleMax) {
								candidateIdx = i;
								break;
							} else if (cursorAngle + 2 * IM_PI < entryInnerAngleMax && 
								cursorAngle + 2 * IM_PI >= entryInnerAngleMin) {
								candidateIdx = i;
								break;
							}
						}
					}
					
					const int currentHovered = _wheels[safeActiveWheelIdx]->GetHoveredEntryIndex();
					
					// Only apply dwell when clearly outside center region
					const float dwellMargin = 0.05f;
					if (candidateIdx >= 0 && candidateIdx != currentHovered && rNorm >= cancelRadiusFrac + dwellMargin) {
						// Candidate differs from current - apply dwell
						if (_antiSlipPendingIdx != candidateIdx) {
							// New candidate - start dwell timer
							_antiSlipPendingIdx = candidateIdx;
							_antiSlipPendingSince = now;
						}
						// Check if dwell completed
						if (now - _antiSlipPendingSince < switchDwellSecs) {
							// Dwell not complete - keep current selection by forcing centered
							// (Wheel::Draw won't update selection)
							isCursorCentered = true;
						} else {
							// Dwell complete - allow selection change
							_antiSlipPendingIdx = -1;
						}
					} else {
						// Candidate matches current or no candidate - clear pending
						_antiSlipPendingIdx = -1;
					}
				}
			}
			
			_wheels[safeActiveWheelIdx]->Draw(wheelCenter, _cursorPos, cursorAngle, isCursorCentered, inv, drawArgs,
				_hoveredEntryTime, Config::WheelBehavior::HoverActivateDelaySeconds, a_deltaTime);
			// track hover duration for activate-on-close delay
			if (!isCursorCentered) {
				int hoveredEntry = _wheels[safeActiveWheelIdx]->GetHoveredEntryIndex();
				if (hoveredEntry == _lastHoveredEntry && hoveredEntry >= 0) {
					_hoveredEntryTime = gateHoverTime ? 0.f : (_hoveredEntryTime + a_deltaTime);
				} else {
					const int prevHovered = _lastHoveredEntry;
					if (IsControllerDebugEnabled() && IsLastInputGamepad() && hoveredEntry != _lastLoggedGamepadHoveredIdx) {
						logger::info("[Controller] Hover change {} -> {} angle={:.2f} mag={:.3f} deadzone={} intent={} snap={}",
							prevHovered,
							hoveredEntry,
							cursorAngle,
							_gamepadFilteredMagnitude,
							_gamepadInDeadzone ? "inside" : "outside",
							_gamepadIntentActive ? "active" : "inactive",
							hoveredEntry);
						_lastLoggedGamepadHoveredIdx = hoveredEntry;
					}
					// Hovered entry changed - play hover sound (anti-spam: only when index actually changes)
					if (hoveredEntry >= 0 && hoveredEntry != _lastHoveredEntry) {
						if (safeActiveWheelIdx >= 0 && safeActiveWheelIdx < static_cast<int>(_lastHoveredEntryByWheel.size())) {
							_lastHoveredEntryByWheel[safeActiveWheelIdx] = hoveredEntry;
						}
						// Clear hand override when entry changes (user must press RMB again for new entry)
						if (_handOverrideActive && hoveredEntry != _handOverrideEntryIdx) {
							logger::info("RTU: hand override cleared on entry change (from={} to={})", _handOverrideEntryIdx, hoveredEntry);
							_handOverrideActive = false;
							_handOverrideEntryIdx = -1;
							_handOverrideLeft = false;
						}
						// Reset InstantSpell cancel suppression on entry change
						// (allows new entry to trigger instant cast even if previous was cancelled)
						if (_instantSuppressForEntry || _instantCancelled) {
							_instantSuppressForEntry = false;
							_instantCancelled = false;
							_instantAttemptActive = false;
							_instantReady = false;
							_instantEntryIdx = -1;
							_instantElapsedSec = 0.0f;
							// Reset logging state for new entry
							_instantLastLoggedEntry = -1;
							_instantLastLoggedReady = false;
							_instantLastLoggedProgressBucket = -1;
						}
						// Suppress hover sound for shouts when stage sounds are active
						std::shared_ptr<WheelItem> newHoveredItem = _wheels[safeActiveWheelIdx]->GetHoveredSelectedItem();
						const bool isShout = std::dynamic_pointer_cast<WheelItemShout>(newHoveredItem) != nullptr;
						const bool shoutStageSoundsActive = Config::Sounds::EnableShoutStageSounds &&
							Config::Sounds::ShoutStageSoundMode != static_cast<std::uint32_t>(Config::ShoutStageSoundMode::Off);
						if (!(isShout && shoutStageSoundsActive)) {
							PlaySoundByEditorID(Config::Sounds::HoverSoundEditorID.c_str(), Config::Sounds::HoverSoundVolume);
						}
						// Reset shout stage sounds when entry changes
						ResetShoutStageSounds();
					}
					_lastHoveredEntry = hoveredEntry;
					_hoveredEntryTime = (!gateHoverTime && hoveredEntry >= 0) ? a_deltaTime : 0.f;
					
					// Reset confirm hold if entry changed while holding
					if (_confirmHeld && hoveredEntry != _confirmHoldEntryIdx) {
						_confirmHeld = false;
						_confirmHoldSeconds = 0.f;
						_confirmHoldEntryIdx = -1;
					}
					if (_secondaryConfirmHeld && hoveredEntry != _secondaryConfirmHoldEntryIdx) {
						_secondaryConfirmHeld = false;
						_secondaryConfirmHoldSeconds = 0.f;
						_secondaryConfirmHoldEntryIdx = -1;
					}
					// Reset instant attempt state on entry change
					if (_instantEntryIdx != hoveredEntry) {
						if (_instantAttemptActive) {
							logger::info("InstantSpell: attempt reset on entry change (from={} to={})", _instantEntryIdx, hoveredEntry);
						}
						_instantEntryIdx = hoveredEntry;
						_instantAttemptActive = false;
						_instantReady = false;
						_instantCancelled = false;
						_instantSuppressForEntry = false;
						_instantElapsedSec = 0.0f;
					}
				}
				
				const bool holdActive = _confirmHeld || _secondaryConfirmHeld;
				const float holdSeconds = _confirmHeld ? _confirmHoldSeconds : _secondaryConfirmHoldSeconds;

				// Update shout stage sounds if hovering a shout AND InstantShout is enabled
				// Gate by input mode: RTU uses hoverTime, Hold-to-Use only advances while Confirm is held
				// If InstantShout is OFF, shout stage sounds should not play at all
				if (hoveredEntry >= 0 && Config::WheelBehavior::InstantShout) {
					std::shared_ptr<WheelItem> hoveredItem = _wheels[safeActiveWheelIdx]->GetHoveredSelectedItem();
					if (std::shared_ptr<WheelItemShout> shoutItem = std::dynamic_pointer_cast<WheelItemShout>(hoveredItem)) {
						RE::TESShout* shout = shoutItem->GetShout();
						if (shout) {
							// Respect Per-Type RTU setting for shout sounds
							// If RTU:Shout is OFF, sounds should only advance while holding
							const bool isRTU = Config::WheelBehavior::ReleaseToUse && Config::WheelBehavior::RTUShout;
							if (isRTU) {
								// RTU mode: use hover time
								UpdateShoutStageSounds(_hoveredEntryTime, shout->GetFormID());
							} else {
								// Hold-to-Use mode: only advance while Confirm is held, with threshold
								constexpr float kHoldThreshold = 0.10f; // Must match Wheel.cpp indicator
								if (holdActive && holdSeconds >= kHoldThreshold) {
									// Adjust time so sounds start at threshold (sync with indicator)
									UpdateShoutStageSounds(holdSeconds - kHoldThreshold, shout->GetFormID());
								} else {
									// Not holding or under threshold - reset sounds
									ResetShoutStageSounds();
								}
							}
						}
					}
				}
				
				// Update instant spell attempt state for countdown UI.
				const bool canProcessInstantIndicator =
					hoveredEntry >= 0 && !_instantSuppressForEntry && !_instantCancelled;

				if (canProcessInstantIndicator) {
					std::shared_ptr<WheelItem> instantHoveredItem = _wheels[safeActiveWheelIdx]->GetHoveredSelectedItem();
					if (std::shared_ptr<WheelItemSpell> spellItem = std::dynamic_pointer_cast<WheelItemSpell>(instantHoveredItem)) {
						RE::SpellItem* spell = spellItem->GetSpell();
						const bool isInTransformForIndicator = !TransformWheelManager::IsPlayerHuman();
						const bool instantEnabledForIndicator = IsInstantEnabledForSpell(spell, isInTransformForIndicator);
						if (!instantEnabledForIndicator) {
							_instantAttemptActive = false;
							_instantReady = false;
							_instantEntryIdx = -1;
							_instantElapsedSec = 0.0f;
						} else {
							const bool isConcentration = IsConcentrationSpellType(spell);
							const bool concentrationAllowed = IsConcentrationInstantAllowed(spell);
							if (!concentrationAllowed) {
								_instantAttemptActive = false;
								_instantReady = false;
								_instantEntryIdx = -1;
								_instantElapsedSec = 0.0f;
								if (Config::WheelBehavior::InstantSpellDebugLog || Config::WheelBehavior::InstantTransformationsDebugLog) {
									logger::info("InstantSpell: indicator blocked for spell '{}' (mode={}, isConcentration={}, concentrationAllowed={})",
										spell ? spell->GetName() : "null",
										Config::WheelBehavior::InstantSpellConcentrationMode,
										isConcentration,
										concentrationAllowed);
								}
							} else {
								// Use max of safety threshold and user hold threshold.
								const float configThreshold = Config::WheelBehavior::InstantSpellHoldThresholdMs / 1000.0f;
								const float safetyThreshold = Config::WheelBehavior::HoldToCastSafetyThresholdMs / 1000.0f;
								const float kInstantSpellThreshold = (std::max)(safetyThreshold, configThreshold);

								// Respect Per-Type RTU setting for instant activation logic.
								const bool isRTU = Config::WheelBehavior::ReleaseToUse && Config::WheelBehavior::RTUSpell;

								// Determine elapsed time and effective threshold based on input mode.
								float elapsedSec = 0.0f;
								float thresholdSec = kInstantSpellThreshold;

								if (isRTU) {
									elapsedSec = _hoveredEntryTime;
									_instantRtuSource = true;
								} else if (holdActive) {
									const float kSafetyThreshold = Config::WheelBehavior::HoldToCastSafetyThresholdMs / 1000.0f;
									if (holdSeconds >= kSafetyThreshold) {
										elapsedSec = holdSeconds - kSafetyThreshold;
										thresholdSec = kInstantSpellThreshold - kSafetyThreshold;
									}
									_instantRtuSource = false;
								}

								// Update attempt state.
								if (elapsedSec > 0.0f) {
									_instantAttemptActive = true;
									_instantEntryIdx = hoveredEntry;
									_instantElapsedSec = elapsedSec;
									_instantReady = (elapsedSec >= thresholdSec);

									// State-change logging (no frame spam).
									const int progressBucket = static_cast<int>(elapsedSec / 0.25f);
									if (_instantEntryIdx != _instantLastLoggedEntry) {
										logger::info("InstantSpell: entry changed to {} (elapsed={:.2f}s)",
											_instantEntryIdx, elapsedSec);
										_instantLastLoggedEntry = _instantEntryIdx;
										_instantLastLoggedProgressBucket = progressBucket;
										_instantLastLoggedReady = _instantReady;
									} else if (_instantReady && !_instantLastLoggedReady) {
										logger::info("InstantSpell: ready=true (entry={}, elapsed={:.2f}s)",
											_instantEntryIdx, elapsedSec);
										_instantLastLoggedReady = true;
									} else if (progressBucket != _instantLastLoggedProgressBucket) {
										_instantLastLoggedProgressBucket = progressBucket;
									}
								} else {
									// Not hovering long enough or not holding confirm.
									_instantAttemptActive = false;
									_instantReady = false;
								}
							}
						}
					} else {
						// Not a spell - no instant attempt.
						_instantAttemptActive = false;
						_instantReady = false;
						_instantEntryIdx = -1;
						_instantElapsedSec = 0.0f;
					}
				} else {
					_instantAttemptActive = false;
					_instantReady = false;
					_instantEntryIdx = -1;
					_instantElapsedSec = 0.0f;
				}
			} else {
				_lastHoveredEntry = -1;
				_hoveredEntryTime = 0.f;
				// Reset shout stage sounds when cursor centers
				ResetShoutStageSounds();
				// Reset confirm hold if cursor moved to center
				if (_confirmHeld) {
					_confirmHeld = false;
					_confirmHoldSeconds = 0.f;
					_confirmHoldEntryIdx = -1;
				}
				if (_secondaryConfirmHeld) {
					_secondaryConfirmHeld = false;
					_secondaryConfirmHoldSeconds = 0.f;
					_secondaryConfirmHoldEntryIdx = -1;
				}
			}
			
			// Update confirm hold duration if held
			if (_confirmHeld) {
				_confirmHoldSeconds = static_cast<float>(ImGui::GetTime() - _confirmHoldStartTime);
			}
			if (_secondaryConfirmHeld) {
				_secondaryConfirmHoldSeconds = static_cast<float>(ImGui::GetTime() - _secondaryConfirmHoldStartTime);
			}
		}

		// If CloseWheelAfterUse is on, allow RTU to fire while the wheel is open (after delay), then close.
		if (_state == WheelState::KOpened && Config::WheelBehavior::CloseWheelAfterUse) {
			if (TryActivateHoveredEntryRTU(false)) {
				TryCloseWheeler();
			}
		}

		ProcessPendingActions();


		// draw wheel indicator
		for (int i = 0; i < _wheels.size(); i++) {
			bool isWheelActive = i == _activeWheelIdx;
			ImVec2 wheelIndicatorPos = {
				wheelCenter.x + Config::Styling::Wheel::WheelIndicatorOffsetX +
					i * Config::Styling::Wheel::WheelIndicatorSpacing,
				wheelCenter.y + Config::Styling::Wheel::WheelIndicatorOffsetY };

			if (Config::Styling::Wheel::WheelIndicatorAlignment ==
				Config::WidgetAlignment::kCenter) {  // offset from center
				wheelIndicatorPos.x -=
					(_wheels.size() - 1) * Config::Styling::Wheel::WheelIndicatorSpacing / 2.f;
			}

			if (!Config::Styling::Wheel::UseGeometricPrimitiveForBackgroundTexture) {
				Texture::Image wheelIndicatorTexture =
					isWheelActive ?
						Texture::GetIconImage(Texture::icon_image_type::wheel_indicator_active) :
						Texture::GetIconImage(Texture::icon_image_type::wheel_indicator_inactive);
				Drawer::draw_texture(
					wheelIndicatorTexture.texture,
					wheelIndicatorPos,
					0, 0,
					{ Config::Styling::Wheel::WheelIndicatorSize,
						Config::Styling::Wheel::WheelIndicatorSize },
					C_SKYRIMWHITE,
					drawArgs);
			} else {
				Drawer::draw_circle_filled(
					wheelIndicatorPos,
					Config::Styling::Wheel::WheelIndicatorSize / 2,
					isWheelActive ? Config::Styling::Wheel::WheelIndicatorActiveColor :
									Config::Styling::Wheel::WheelIndicatorInactiveColor,
					10,
					drawArgs);
			}
		}

		if (MainWheelDebug::IsEnabled() && Config::MainWheel::Debug::OverlayEnabled) {
			const int wheelCount = static_cast<int>(_wheels.size());
			int hoveredIdx = -1;
			int entryCount = 0;
			if (_activeWheelIdx >= 0 && _activeWheelIdx < wheelCount && _wheels[_activeWheelIdx]) {
				hoveredIdx = _wheels[_activeWheelIdx]->GetHoveredEntryIndex();
				entryCount = _wheels[_activeWheelIdx]->GetNumEntries();
			}
			const char* stateLabel = "Closed";
			switch (_state) {
			case WheelState::KOpened:
				stateLabel = "Opened";
				break;
			case WheelState::KOpening:
				stateLabel = "Opening";
				break;
			case WheelState::KClosing:
				stateLabel = "Closing";
				break;
			case WheelState::KClosed:
			default:
				stateLabel = "Closed";
				break;
			}
			DrawArgs overlayArgs = drawArgs;
			overlayArgs.alphaMult = 1.0f;
			const float overlayX = 24.0f;
			float overlayY = 24.0f;
			const float overlaySize = 16.0f;
			const ImU32 overlayColor = C_SKYRIMWHITE;
			const std::string line1 = fmt::format("MainWheel [{}] wheels={} entries={} hovered={}",
				stateLabel, wheelCount, entryCount, hoveredIdx);
			const std::string line2 = fmt::format("Center=({:.1f},{:.1f}) Radius={:.1f} Lsu={:.3f} Msu={:.3f}",
				wheelCenter.x, wheelCenter.y, Config::Styling::Wheel::OuterCircleRadius,
				layoutState.Lsu, layoutState.Msu);
			Drawer::draw_text(overlayX, overlayY, line1.c_str(), overlayColor, overlaySize, overlayArgs, false);
			overlayY += overlaySize + 4.0f;
			Drawer::draw_text(overlayX, overlayY, line2.c_str(), overlayColor, overlaySize, overlayArgs, false);
		}

		if (IsControllerDebugOverlayEnabled()) {
			DrawArgs overlayArgs = drawArgs;
			overlayArgs.alphaMult = 1.0f;
			const float overlayX = 24.0f;
			float overlayY = 24.0f;
			const float overlaySize = 16.0f;
			const ImU32 overlayColor = C_SKYRIMWHITE;
			if (MainWheelDebug::IsEnabled() && Config::MainWheel::Debug::OverlayEnabled) {
				overlayY += (overlaySize + 4.0f) * 2 + 4.0f;
			}
			const double now = ImGui::GetTime();
			float graceRemainingMs = 0.0f;
			if (Config::WheelBehavior::Gamepad::Open::HasOpenGraceMs && now < _gamepadOpenGraceUntil) {
				graceRemainingMs = static_cast<float>((_gamepadOpenGraceUntil - now) * 1000.0);
			}
			const std::string line1 = fmt::format("Controller [{}] mag={:.3f} deadzone={} intent={} clamp={}",
				GetLastInputDeviceName(_lastInputDevice),
				_gamepadFilteredMagnitude,
				_gamepadInDeadzone ? "in" : "out",
				_gamepadIntentActive ? "on" : "off",
				_gamepadOuterClampActive ? "on" : "off");
			const std::string line2 = fmt::format("Stick=({:.3f},{:.3f}) graceMs={:.0f}",
				_gamepadSmoothed.x, _gamepadSmoothed.y, graceRemainingMs);
			Drawer::draw_text(overlayX, overlayY, line1.c_str(), overlayColor, overlaySize, overlayArgs, false);
			overlayY += overlaySize + 4.0f;
			Drawer::draw_text(overlayX, overlayY, line2.c_str(), overlayColor, overlaySize, overlayArgs, false);
		}

		if (Config::MainWheel::Mouse::DrawDebugOverlay) {
			DrawArgs overlayArgs = drawArgs;
			overlayArgs.alphaMult = 1.0f;
			const float overlayX = 24.0f;
			float overlayY = 24.0f;
			const float overlaySize = 16.0f;
			const ImU32 overlayColor = C_SKYRIMWHITE;
			if (MainWheelDebug::IsEnabled() && Config::MainWheel::Debug::OverlayEnabled) {
				overlayY += (overlaySize + 4.0f) * 2 + 4.0f;
			}
			if (IsControllerDebugOverlayEnabled()) {
				overlayY += (overlaySize + 4.0f) * 2 + 4.0f;
			}
			if (_activeWheelIdx >= 0 && _activeWheelIdx < static_cast<int>(_wheels.size()) && _wheels[_activeWheelIdx]) {
				const Wheel::MouseHoverDebugInfo& hoverDebug = _wheels[_activeWheelIdx]->GetMouseHoverDebugInfo();
				if (hoverDebug.valid) {
					const float stableDeg = hoverDebug.stableTheta * (180.0f / IM_PI);
					const float rawDeg = hoverDebug.rawTheta * (180.0f / IM_PI);
					const std::string line1 = fmt::format("MouseHover stabilize={} r={:.2f} spd={:.2f} ang={:.1f}/{:.1f}",
						hoverDebug.useNewModel ? "on" : "off",
						hoverDebug.rNorm, hoverDebug.speedNorm, stableDeg, rawDeg);
					const std::string line2 = fmt::format("idx prev={} cand={} final={} why={}",
						hoverDebug.currentIdx, hoverDebug.bestIdx, hoverDebug.secondIdx,
						hoverDebug.reason.data());
					Drawer::draw_text(overlayX, overlayY, line1.c_str(), overlayColor, overlaySize, overlayArgs, false);
					overlayY += overlaySize + 4.0f;
					Drawer::draw_text(overlayX, overlayY, line2.c_str(), overlayColor, overlaySize, overlayArgs, false);
				}
			}
		}


		drawList->PopClipRect();
		ImGui::EndPopup();
	}

	if (perfEnabled) {
		const auto elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - perfStart).count();
		MainWheelDebug::LogRateLimited(MainWheelDebug::Category::Perf, "update",
			"Update {:.2f} ms (state={}, wheels={}, ammoOpen={})",
			elapsedMs, static_cast<int>(_state), static_cast<int>(_wheels.size()),
			_ammoWheel && _ammoWheel->IsOpen());
	}
}

void Wheeler::Clear()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	InputBroker::ClearActiveOwner(InputBroker::kWheelerRefinedPluginId);
	if (_state != WheelState::KClosed) {
		CloseWheeler();  // force close menu, since we're loading items
	}
	if (_editMode) {
		exitEditMode();
	}
	// clean up old wheels
	for (auto& wheel : _wheels) {
		wheel->Clear();
	}
	_wheels.clear();
	TransformWheelManager::Reset();
}

// ========== Ammo Wheel Methods ==========

void Wheeler::ArmAmmoWheelMenuHold(std::uint32_t mappedKey, bool isGamepad)
{
	if (g_ammoWheelMenuHold.armed &&
	    g_ammoWheelMenuHold.key == mappedKey &&
	    g_ammoWheelMenuHold.isGamepad == isGamepad) {
		return;
	}
	g_ammoWheelMenuHold.armed = true;
	g_ammoWheelMenuHold.fired = false;
	g_ammoWheelMenuHold.isGamepad = isGamepad;
	g_ammoWheelMenuHold.key = mappedKey;
	g_ammoWheelMenuHold.startSec = GetSafeInputTimestampSeconds();
}

bool Wheeler::DisarmAmmoWheelMenuHold(std::uint32_t mappedKey, bool isGamepad)
{
	if (!g_ammoWheelMenuHold.armed ||
	    g_ammoWheelMenuHold.key != mappedKey ||
	    g_ammoWheelMenuHold.isGamepad != isGamepad) {
		return false;
	}
	const bool fired = g_ammoWheelMenuHold.fired;
	ResetAmmoWheelMenuHoldGate();
	return fired;
}

bool Wheeler::IsAmmoWheelMenuHoldArmed(std::uint32_t mappedKey, bool isGamepad)
{
	return g_ammoWheelMenuHold.armed &&
	       g_ammoWheelMenuHold.key == mappedKey &&
	       g_ammoWheelMenuHold.isGamepad == isGamepad;
}

void Wheeler::UpdateAmmoWheelMenuHoldGate()
{
	if (!g_ammoWheelMenuHold.armed) {
		return;
	}
	if (!Config::AmmoWheel::InputSafeguards::MenuHoldToOpenEnabled) {
		ResetAmmoWheelMenuHoldGate();
		return;
	}
	if (_state != WheelState::KClosed || IsAmmoWheelOpen()) {
		ResetAmmoWheelMenuHoldGate();
		return;
	}
	if (!IsAmmoWheelMenuHoldContextOpen()) {
		ResetAmmoWheelMenuHoldGate();
		return;
	}

	const bool keyHeld = g_ammoWheelMenuHold.isGamepad ?
		Controls::IsGamepadKeyHeld(g_ammoWheelMenuHold.key) :
		Controls::IsMkbKeyHeld(g_ammoWheelMenuHold.key);
	if (!keyHeld) {
		ResetAmmoWheelMenuHoldGate();
		return;
	}

	const bool modifierHeld = g_ammoWheelMenuHold.isGamepad ?
		(Config::AmmoWheel::GamePad::modifierButton == 0 ||
			Controls::IsGamepadKeyHeld(Config::AmmoWheel::GamePad::modifierButton)) :
		(Config::AmmoWheel::MKB::modifierKey == 0 ||
			Controls::IsMkbKeyHeld(Config::AmmoWheel::MKB::modifierKey));
	if (!modifierHeld) {
		ResetAmmoWheelMenuHoldGate();
		return;
	}

	if (g_ammoWheelMenuHold.fired) {
		return;
	}

	const double heldSeconds = GetSafeInputTimestampSeconds() - g_ammoWheelMenuHold.startSec;
	if (heldSeconds >= static_cast<double>(Config::AmmoWheel::InputSafeguards::MenuHoldToOpenSeconds)) {
		g_ammoWheelMenuHold.fired = true;
		Controls::Dispatch(g_ammoWheelMenuHold.key, true, g_ammoWheelMenuHold.isGamepad);
	}
}

void Wheeler::ToggleAmmoWheel()
{
	// Don't allow ammo wheel if main wheel is open
	if (_state != WheelState::KClosed) {
		return;
	}

	// Initialize ammo wheel if needed
	if (!_ammoWheel) {
		_ammoWheel = std::make_unique<AmmoWheel>();
	}

	_ammoWheel->Toggle();
	InputBroker::SyncWheelerActiveOwner(IsWheelerOpen(), IsAmmoWheelOpen());
}

void Wheeler::CloseAmmoWheelIfOpenedLongEnough()
{
	if (_ammoWheel) {
		_ammoWheel->CloseIfOpenedLongEnough();
	}
}

void Wheeler::ActivateAmmoWheelHovered()
{
	if (_ammoWheel && _ammoWheel->IsOpen()) {
		_ammoWheel->ActivateHoveredAmmo();
	}
}

bool Wheeler::IsAmmoWheelOpen()
{
	return _ammoWheel && _ammoWheel->IsOpen();
}

void Wheeler::UpdateAmmoWheelCursorPosMouse(float a_deltaX, float a_deltaY)
{
	if (_ammoWheel && _ammoWheel->IsOpen()) {
		_ammoWheel->UpdateCursorPosMouse(a_deltaX, a_deltaY);
	}
}

void Wheeler::UpdateAmmoWheelCursorPosGamepad(float a_x, float a_y)
{
	if (_ammoWheel && _ammoWheel->IsOpen()) {
		_ammoWheel->UpdateCursorPosGamepad(a_x, a_y);
	}
}

Wheeler::InputAction Wheeler::ResolveMainWheelInputAction(std::uint32_t input, bool isGamepad)
{
	if (isGamepad) {
		if (Config::InputBindings::GamePad::toggleWheel != 0 &&
			input == Config::InputBindings::GamePad::toggleWheel) {
			return InputAction::Toggle;
		}
		if (Config::InputBindings::GamePad::toggleWheelIfInInventory != 0 &&
			input == Config::InputBindings::GamePad::toggleWheelIfInInventory) {
			return InputAction::ToggleIfInInventory;
		}
		if (Config::InputBindings::GamePad::toggleWheelIfNotInInventory != 0 &&
			input == Config::InputBindings::GamePad::toggleWheelIfNotInInventory) {
			return InputAction::ToggleIfNotInInventory;
		}
		return InputAction::None;
	}

	if (Config::InputBindings::MKB::toggleWheel != 0 &&
		input == Config::InputBindings::MKB::toggleWheel) {
		return InputAction::Toggle;
	}
	return InputAction::None;
}

void Wheeler::RecordMainWheelInputEvent(InputAction action, RE::INPUT_DEVICE device, std::uint32_t rawCode,
	std::uint32_t mappedCode, bool isDown, bool isUp)
{
	if (action == InputAction::None) {
		return;
	}
	if (!isDown && !isUp) {
		return;
	}

	_mainWheelLastInput.valid = true;
	_mainWheelLastInput.action = action;
	_mainWheelLastInput.device = device;
	_mainWheelLastInput.rawCode = rawCode;
	_mainWheelLastInput.mappedCode = mappedCode;
	_mainWheelLastInput.isDown = isDown;
	_mainWheelLastInput.isUp = isUp;
	_mainWheelLastInput.sequence = ++_mainWheelInputSequence;
	_mainWheelLastInput.timestamp = ImGui::GetTime();

	switch (device) {
	case RE::INPUT_DEVICE::kGamepad:
		UpdateLastInputDevice(LastInputDevice::Gamepad);
		break;
	case RE::INPUT_DEVICE::kMouse:
	case RE::INPUT_DEVICE::kKeyboard:
		UpdateLastInputDevice(LastInputDevice::MKB);
		break;
	default:
		break;
	}
}

void Wheeler::UpdateLastInputDevice(LastInputDevice device)
{
	if (_lastInputDevice == device) {
		return;
	}
	_lastInputDevice = device;
	if (IsControllerDebugEnabled()) {
		logger::info("[Controller] InputDevice={}", GetLastInputDeviceName(device));
	}
}

const char* Wheeler::GetLastInputDeviceName(LastInputDevice device)
{
	switch (device) {
	case LastInputDevice::MKB:
		return "MKB";
	case LastInputDevice::Gamepad:
		return "Gamepad";
	case LastInputDevice::None:
	default:
		return "None";
	}
}

const char* Wheeler::GetWheelStateName(WheelState state)
{
	switch (state) {
	case WheelState::KOpened:
		return "Opened";
	case WheelState::KOpening:
		return "Opening";
	case WheelState::KClosing:
		return "Closing";
	case WheelState::KClosed:
	default:
		return "Closed";
	}
}

void Wheeler::LogMainWheelInputDecision(InputDecision decision, InputConsumer consumer)
{
	if (!_mainWheelLastInput.valid) {
		return;
	}

	const auto& evt = _mainWheelLastInput;
	const double now = ImGui::GetTime();

	bool repeat = false;
	const char* edge = "None";
	float heldMs = 0.0f;
	if (evt.isDown) {
		if (!_mainWheelInputState.isDown) {
			edge = "Down";
			_mainWheelInputState.isDown = true;
			_mainWheelInputState.lastDownTime = now;
			_mainWheelInputState.lastEdgeSequence = evt.sequence;
		} else {
			repeat = true;
		}
	}
	if (evt.isUp) {
		edge = "Up";
		if (_mainWheelInputState.isDown) {
			heldMs = static_cast<float>((now - _mainWheelInputState.lastDownTime) * 1000.0);
		}
		_mainWheelInputState.isDown = false;
		_mainWheelInputState.lastEdgeSequence = evt.sequence;
	}

	const bool ammoOpen = _ammoWheel && _ammoWheel->IsOpen();
	const bool otherWheelOpen = ammoOpen;
	const int state = evt.isDown ? 1 : (evt.isUp ? 0 : (_mainWheelInputState.isDown ? 1 : 0));
	const bool shouldLog = MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Input);

	if (shouldLog) {
		MainWheelDebug::Log(MainWheelDebug::Category::Input,
			"action={} raw={} mapped={} device={} state={} edge={} repeat={} heldMs={:.1f} editMode={} ammoOpen={} otherWheelOpen={} inputConsumedBy={} decision={}",
			GetMainWheelInputActionName(evt.action),
			evt.rawCode,
			evt.mappedCode,
			GetInputDeviceName(evt.device),
			state,
			edge,
			repeat ? "true" : "false",
			heldMs,
			_editMode ? "true" : "false",
			ammoOpen ? "true" : "false",
			otherWheelOpen ? "true" : "false",
			GetMainWheelInputConsumerName(consumer),
			GetMainWheelInputDecisionName(decision));
	}

	_mainWheelLastLoggedSequence = evt.sequence;
	_mainWheelLastInput.valid = false;
}

bool Wheeler::HandleAmmoWheelMouseButton(int button, bool pressed)
{
	if (_ammoWheel && _ammoWheel->IsOpen()) {
		return _ammoWheel->HandleMouseButton(button, pressed);
	}
	return false;
}

void Wheeler::NotifyAmmoWheelConfigChanged()
{
	if (_ammoWheel) {
		_ammoWheel->OnConfigChanged();
		logger::info("Wheeler: AmmoWheel config reloaded and layout recalculated");
	}
}

void Wheeler::ToggleWheeler()
{
	if (_state == WheelState::KClosed) {
		// CRITICAL: Capture FavoritesMenu selection BEFORE opening (while GFx has focus)
		RE::UI* ui = RE::UI::GetSingleton();
		if (ui && ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME)) {
			auto favMenu = ui->GetMenu<RE::FavoritesMenu>();
			if (favMenu) {
				Utils::Inventory::FavoritesSelectionCache::CaptureSelection(favMenu.get());
			}
		}
		
		_pendingCloseDecision = InputDecision::None;
		TryOpenWheeler();
	} else {
		_pendingCloseDecision = InputDecision::CloseRequested;
		TryCloseWheeler();
	}
}

void Wheeler::ToggleWheelIfInInventory()
{
	RE::UI* ui = RE::UI::GetSingleton();
	if (!ui) {
		LogMainWheelInputDecision(InputDecision::DeniedNoUI, InputConsumer::Other);
		return;
	}
	if (!shouldBeInEditMode(ui)) {
		LogMainWheelInputDecision(InputDecision::DeniedInventoryState, InputConsumer::MainWheel);
		return;
	}
	
	// CRITICAL: Capture FavoritesMenu selection BEFORE opening wheel (while GFx has focus)
	if (ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME)) {
		auto favMenu = ui->GetMenu<RE::FavoritesMenu>();
		if (favMenu) {
			bool ok = Utils::Inventory::FavoritesSelectionCache::CaptureSelection(favMenu.get());
			logger::info("ToggleWheelIfInInventory: FavoritesMenu selection capture {}", ok ? "OK" : "FAILED");
		}
	}
	
	ToggleWheeler();
}

void Wheeler::ToggleWheelIfNotInInventory()
{
	RE::UI* ui = RE::UI::GetSingleton();
	if (!ui) {
		LogMainWheelInputDecision(InputDecision::DeniedNoUI, InputConsumer::Other);
		return;
	}
	if (shouldBeInEditMode(ui)) {
		LogMainWheelInputDecision(InputDecision::DeniedInventoryState, InputConsumer::MainWheel);
		return;
	}
	ToggleWheeler();
}

void Wheeler::CloseWheelerIfOpenedLongEnough()
{
	if (_openTimer > Config::Control::Wheel::ToggleHoldThreshold) {
		_pendingCloseDecision = InputDecision::CloseRelease;
		TryCloseWheeler();
	} else {
		LogMainWheelInputDecision(InputDecision::DeniedHoldThreshold, InputConsumer::MainWheel);
	}
}

void Wheeler::CloseWheelerIfOpenedLongEnoughIfInInventory()
{
	RE::UI* ui = RE::UI::GetSingleton();
	if (!ui) {
		LogMainWheelInputDecision(InputDecision::DeniedNoUI, InputConsumer::Other);
		return;
	}
	if (!shouldBeInEditMode(ui)) {
		LogMainWheelInputDecision(InputDecision::DeniedInventoryState, InputConsumer::MainWheel);
		return;
	}
	CloseWheelerIfOpenedLongEnough();
}

void Wheeler::CloseWheelerIfOpenedLongEnoughIfNotInInventory()
{
	RE::UI* ui = RE::UI::GetSingleton();
	if (!ui) {
		LogMainWheelInputDecision(InputDecision::DeniedNoUI, InputConsumer::Other);
		return;
	}
	if (shouldBeInEditMode(ui)) {
		LogMainWheelInputDecision(InputDecision::DeniedInventoryState, InputConsumer::MainWheel);
		return;
	}
	CloseWheelerIfOpenedLongEnough();
}

void Wheeler::TryOpenWheeler()
{
	// Toggle-release latch: block open until the toggle key is released after a book read
	if (_suppressOpenUntilToggleUp) {
		const bool toggleDown = _mainWheelInputState.isDown;
		const double now = ImGui::GetTime();
		if (toggleDown && now < _suppressWheelOpenUntil) {
			// Still held and within failsafe window - deny
			if (MainWheelDebug::IsEnabled()) {
				MainWheelDebug::LogRateLimited(MainWheelDebug::Category::OpenClose, "TryOpen_Suppressed",
					"TryOpen denied: waiting toggle UP (failsafe {:.1f}s remaining)",
					_suppressWheelOpenUntil - now);
			}
			return;
		}
		// Toggle released or failsafe expired - clear latch
		_suppressOpenUntilToggleUp = false;
	}
	// here we straight up open the wheel, and set state to opening if we have a fade time.
	// this is because for the fade to start showing the wheel has to be actually fully opened,
	// but to track the state we give it a "opening" state.
	MainWheelDebug::Log(MainWheelDebug::Category::OpenClose, "TryOpen (state={}, wheels={}, editMode={})",
		static_cast<int>(_state), static_cast<int>(_wheels.size()), _editMode);
	
	// Prune entries that are no longer in player inventory BEFORE wheel becomes visible
	PruneWheelEntries_NotInInventory(PruneReason::OnOpen);
	
	_activateOnCloseFired = false;
	_directActivatedThisOpenSession = false;
	_lastHoveredEntry = -1;
	_hoveredEntryTime = 0.f;
	_lastLoggedGamepadHoveredIdx = -2;
	_pendingCloseDecision = InputDecision::None;
	OpenWheeler();
	
	// Notify TransformWheelManager to start late refresh polling for transform spells
	TransformWheelManager::OnWheelOpened();
	if (IsControllerDebugEnabled()) {
		logger::info("[Controller] TryOpen decision={} consumer={}",
			GetMainWheelInputDecisionName(_lastOpenDecision),
			GetMainWheelInputConsumerName(_lastOpenConsumer));
	}
	LogMainWheelInputDecision(_lastOpenDecision, _lastOpenConsumer);
}

void Wheeler::TryCloseWheeler()
{
	if (_state == WheelState::KClosed || _state == WheelState::KClosing) {
		LogMainWheelInputDecision(InputDecision::DeniedState, InputConsumer::MainWheel);
		_pendingCloseDecision = InputDecision::None;
		return;
	}
	const InputDecision decision = (_pendingCloseDecision != InputDecision::None) ? _pendingCloseDecision : InputDecision::CloseRequested;
	_pendingCloseDecision = InputDecision::None;
	LogMainWheelInputDecision(decision, InputConsumer::MainWheel);
	if (IsControllerDebugEnabled()) {
		logger::info("[Controller] TryClose decision={} consumer={}",
			GetMainWheelInputDecisionName(decision),
			GetMainWheelInputConsumerName(InputConsumer::MainWheel));
	}
	MainWheelDebug::Log(MainWheelDebug::Category::OpenClose, "TryClose (state={}, editMode={})",
		static_cast<int>(_state), _editMode);
	// Optional: Release-to-Use (activate hovered entry when the wheel closes).
	TryActivateHoveredEntryRTU(true);
	if (Config::Animation::FadeTime == 0) {
		CloseWheeler();  // close directly
	} else {
		// Restore timescale prior to closing animation (only if Wheeler modified it)
		if (_wheelerModifiedTimeScale) {
			Utils::Time::SGTM(_preWheelerTimeScale);
			_wheelerModifiedTimeScale = false;
		}
		_state = WheelState::KClosing;  // set state to closing, will be closed once time out
		_closeTimer = 0;
	}
}

void Wheeler::OpenWheeler()
{
	_lastOpenDecision = InputDecision::OpenRequested;
	_lastOpenConsumer = InputConsumer::MainWheel;

	if (_state == WheelState::KOpened || _state == WheelState::KOpening) {
		_lastOpenDecision = InputDecision::DeniedState;
		_lastOpenConsumer = InputConsumer::MainWheel;
		return;
	}

	if (!RE::PlayerCharacter::GetSingleton() || !RE::PlayerCharacter::GetSingleton()->Is3DLoaded()) {
		_lastOpenDecision = InputDecision::DeniedNoPlayer;
		_lastOpenConsumer = InputConsumer::Other;
		return;
	}

	if (TransformWheelManager::IsPlayerHuman() &&
		TransformWheelManager::IsTransformWheelIndex(_activeWheelIdx)) {
		int fallbackIdx = 0;
		if (auto saved = TransformWheelManager::GetSavedHumanWheelIndex();
			saved && *saved >= 0 && *saved < static_cast<int>(_wheels.size()) &&
			!TransformWheelManager::IsTransformWheelIndex(*saved)) {
			fallbackIdx = *saved;
		}
		SetActiveWheelIndex(fallbackIdx);
		if (Config::WheelBehavior::TransformWheels::DebugLog) {
			logger::info("TransformWheels: open guard restored active wheel to {}", fallbackIdx);
		}
	}
	
	// Don't open main wheel if AmmoWheel is blocking (open or opening)
	if (_ammoWheel && _ammoWheel->IsBlockingMainWheel()) {
		_lastOpenDecision = InputDecision::DeniedAmmoWheel;
		_lastOpenConsumer = InputConsumer::AmmoWheel;
		return;
	}
	
	auto ui = RE::UI::GetSingleton();
	if (!ui) {
		_lastOpenDecision = InputDecision::DeniedNoUI;
		_lastOpenConsumer = InputConsumer::Other;
		return;
	}
	static constexpr std::array<std::string_view, 17> conflictingMenus({
		RE::BookMenu::MENU_NAME,
		RE::BarterMenu::MENU_NAME,
		RE::CraftingMenu::MENU_NAME,
		RE::JournalMenu::MENU_NAME,
		RE::LevelUpMenu::MENU_NAME,
		RE::LockpickingMenu::MENU_NAME,
		RE::LoadingMenu::MENU_NAME,
		RE::MainMenu::MENU_NAME,
		RE::MapMenu::MENU_NAME,
		RE::RaceSexMenu::MENU_NAME,
		RE::SleepWaitMenu::MENU_NAME,
		RE::StatsMenu::MENU_NAME,
		RE::TweenMenu::MENU_NAME,
		RE::Console::MENU_NAME,
		RE::DialogueMenu::MENU_NAME,
		RE::GiftMenu::MENU_NAME,
		RE::ModManagerMenu::MENU_NAME
		// NOTE: dMenu variants intentionally NOT in blocking list to allow real-time editing.
		// When dMenu is open, Wheeler uses modal popup to prevent click-outside closing.
		// ContainerMenu and LootMenu are handled separately - Wheeler closes them instead of being blocked
	});
	const bool inventoryOpen = ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME);
	const bool magicOpen = ui->IsMenuOpen(RE::MagicMenu::MENU_NAME);
	for (std::string_view menuName : conflictingMenus) {
		const bool menuOpen = ui->IsMenuOpen(menuName);
		if (menuOpen && !inventoryOpen && !magicOpen) {
			_lastOpenDecision = InputDecision::DeniedMenuBlocked;
			_lastOpenConsumer = InputConsumer::Other;
			if (Config::Debug::LogMenuBlockReasons) {
				logger::info("DeniedMenuBlocked consumer={} menu={} inventoryOpen={} magicOpen={}",
					GetMainWheelInputConsumerName(_lastOpenConsumer),
					menuName,
					inventoryOpen ? "true" : "false",
					magicOpen ? "true" : "false");
			}
			return;
		}
	}
	
	// Loot/Container menu handling - configurable via LootMenuOverride toggle
	static constexpr std::array<std::string_view, 3> lootMenus({
		RE::ContainerMenu::MENU_NAME,
		"LootMenu",      // QuickLoot / QuickLoot RE
		"LootMenuCF"     // QuickLoot EE (Container First)
	});
	for (std::string_view menuName : lootMenus) {
		if (ui->IsMenuOpen(menuName)) {
			if (Config::WheelBehavior::LootMenuOverride) {
				// Close loot menus when Wheeler opens
				logger::info("MainWheel[OpenClose]: Closing {} to open Wheeler", menuName);
				RE::UIMessageQueue::GetSingleton()->AddMessage(menuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			} else {
				// Block Wheeler when loot menus are open (original behavior)
				_lastOpenDecision = InputDecision::DeniedMenuBlocked;
				_lastOpenConsumer = InputConsumer::Other;
				if (Config::Debug::LogMenuBlockReasons) {
					logger::info("DeniedMenuBlocked consumer={} menu={} (LootMenuOverride=OFF)",
						GetMainWheelInputConsumerName(_lastOpenConsumer), menuName);
				}
				return;
			}
		}
	}

	if (_state != WheelState::KOpened && _state != WheelState::KOpening) {
		MainWheelDebug::Log(MainWheelDebug::Category::OpenClose, "Open (activeWheel={}, wheels={}, editMode={})",
			_activeWheelIdx, static_cast<int>(_wheels.size()), _editMode);
		if (MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::ReskinResolve)) {
			MainWheelDebug::Log(MainWheelDebug::Category::ReskinResolve, "BackgroundMode={}",
				Config::Styling::Wheel::UseGeometricPrimitiveForBackgroundTexture ? "primitive" : "texture");
		}
		const bool logScale = MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Scaling);
		const bool logClamp = MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Clamp);
		const bool logConfig = MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Config);
		if (logScale || logClamp || logConfig) {
			Config::MainWheel::LayoutScaling::UpdateRuntimeState();
			const auto& layoutState = Config::MainWheel::LayoutScaling::Runtime;
			float clampDistance = 0.0f;
			bool clamped = false;
			ImVec2 center = GetMainWheelCenterWithLayout(&clampDistance, &clamped);
			if (logScale) {
				MainWheelDebug::Log(MainWheelDebug::Category::Scaling,
					"Display {}x{} Game {}x{} Ref {}x{} ls=({:.3f},{:.3f},{:.3f}) ms=({:.3f},{:.3f},{:.3f}) combined=({:.3f},{:.3f},{:.3f})",
					layoutState.DisplayW, layoutState.DisplayH,
					layoutState.GameW, layoutState.GameH,
					Config::MainWheel::LayoutScaling::RefW, Config::MainWheel::LayoutScaling::RefH,
					layoutState.Lsx, layoutState.Lsy, layoutState.Lsu,
					layoutState.Msx, layoutState.Msy, layoutState.Msu,
					layoutState.CombinedX, layoutState.CombinedY, layoutState.CombinedU);
			}
			if (logClamp && (clamped || MainWheelDebug::IsVerbose())) {
				MainWheelDebug::Log(MainWheelDebug::Category::Clamp,
					"Clamp={} center=({:.1f},{:.1f}) radius={:.1f} safePad={:.1f} shift={:.1f}",
					clamped ? "on" : "off",
					center.x, center.y, Config::Styling::Wheel::OuterCircleRadius,
					Config::MainWheel::LayoutScaling::SafePadPx * layoutState.CombinedU,
					clampDistance);
			}
			if (logConfig) {
				int entryCount = 0;
				if (_activeWheelIdx >= 0 && _activeWheelIdx < _wheels.size() && _wheels[_activeWheelIdx]) {
					entryCount = _wheels[_activeWheelIdx]->GetNumEntries();
				}
				MainWheelDebug::Log(MainWheelDebug::Category::Config,
					"Center=({:.1f},{:.1f}) OuterR={:.1f} InnerR={:.1f} Spacing={:.2f} entries={}",
					center.x, center.y,
					Config::Styling::Wheel::OuterCircleRadius,
					Config::Styling::Wheel::InnerCircleRadius,
					Config::Styling::Wheel::InnerSpacing,
					entryCount);
			}
		}
		if (Config::ResolutionFix::LogOncePerOpen) {
			auto& resolutionContext = ResolutionScale::Context::GetSingleton();
			resolutionContext.Update();
			const auto& state = resolutionContext.GetState();
			const char* mapping = state.active ? "Config::OffsetSizingToViewport" : "None";
			logger::info("[ResolutionFix] MainWheel open: display {}x{}, game {}x{}, scaleX={:.3f}, scaleY={:.3f}, uniform={:.3f}, mode={}, mapping={}",
				state.displayW, state.displayH, state.gameW, state.gameH,
				state.scaleX, state.scaleY, state.uniformScale,
				GetResolutionFixModeName(Config::ResolutionFix::ModeSetting), mapping);

			const auto& layoutState = Config::MainWheel::LayoutScaling::Runtime;
			float clampDistance = 0.0f;
			bool clamped = false;
			ImVec2 center = GetMainWheelCenterWithLayout(&clampDistance, &clamped);
			const char* src = "off";
			if (Config::MainWheel::LayoutScaling::ConfigPresent &&
				Config::MainWheel::LayoutScaling::Enabled &&
				layoutState.LayoutActive) {
				if (!Config::MainWheel::LayoutScaling::LoadedSourceTag.empty()) {
					src = Config::MainWheel::LayoutScaling::LoadedSourceTag.c_str();
				}
			}

			logger::info("[MainWheel.LayoutScaling] src={}, display {}x{}, game {}x{}, ref {}x{}, ls=({:.3f},{:.3f},{:.3f}), ms=({:.3f},{:.3f},{:.3f}), combined=({:.3f},{:.3f},{:.3f}), clamp={}, center=({:.1f},{:.1f}), radius={:.1f}, input=GameSpace",
				src,
				layoutState.DisplayW, layoutState.DisplayH, layoutState.GameW, layoutState.GameH,
				Config::MainWheel::LayoutScaling::RefW, Config::MainWheel::LayoutScaling::RefH,
				layoutState.Lsx, layoutState.Lsy, layoutState.Lsu,
				layoutState.Msx, layoutState.Msy, layoutState.Msu,
				layoutState.CombinedX, layoutState.CombinedY, layoutState.CombinedU,
				Config::MainWheel::LayoutScaling::ClampToScreen ? "on" : "off",
				center.x, center.y, Config::Styling::Wheel::OuterCircleRadius);

			if (clamped) {
				const float clampThreshold = (std::max)(5.0f, Config::MainWheel::LayoutScaling::SafePadPx * layoutState.CombinedU);
				if (clampDistance > clampThreshold) {
					logger::info("[MainWheel.LayoutScaling] Clamp shift {:.1f}px (threshold {:.1f}px).", clampDistance, clampThreshold);
				}
			}
		}
		// SlowTimeScale <= 0 now means vanilla pause (kPausesGame); no SGTM.
		const float slowScale = Config::Styling::Wheel::SlowTimeScale;
		if (slowScale <= 0.0f) {
			_wheelerModifiedTimeScale = false;
			OpenPauseMenu();
			_wheelerOwnedPauseMenu = true;
			logger::info("[TimeDilation] MainWheel pause-mode: SlowTimeScale=0 -> vanilla pause (kPausesGame)");
		} else if (slowScale < 1.0f) {
			const float effectiveScale = (std::max)(slowScale, 0.01f);
			float currentTimeScale = Utils::Time::GGTM();
			// Only modify timescale if it's currently at normal (1.0) - don't override Slow Time shout or other effects
			if (currentTimeScale >= 0.99f && currentTimeScale <= 1.01f) {
				_preWheelerTimeScale = currentTimeScale;
				_wheelerModifiedTimeScale = true;
				_wheelerOwnedPauseMenu = false;
				Utils::Time::SGTM(effectiveScale);
				logger::info("[TimeDilation] MainWheel apply: before={:.3f}, after={:.3f}, cached={:.3f}",
					currentTimeScale, effectiveScale, _preWheelerTimeScale);
			} else {
				// External time effect active (e.g., Slow Time shout) - don't touch timescale
				_wheelerModifiedTimeScale = false;
				_wheelerOwnedPauseMenu = false;
				logger::info("[TimeDilation] MainWheel skip: external effect active (current={:.3f}, requested={:.3f}, effective={:.3f})",
					currentTimeScale, slowScale, effectiveScale);
			}
		} else {
			_wheelerModifiedTimeScale = false;
			_wheelerOwnedPauseMenu = false;
		}
		if (Config::Styling::Wheel::BlurOnOpen) {
			RE::UIBlurManager::GetSingleton()->IncrementBlurCount();
		}
		if (_activeWheelIdx >= 0 && _activeWheelIdx < _wheels.size()) {
			_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);  // reset active entry on OPEN
			_wheels[_activeWheelIdx]->ResetAnimation();
		}
		_state = Config::Animation::FadeTime > 0 ? WheelState::KOpening : WheelState::KOpened;
		_openTimer = 0;
		InputBroker::SetActiveOwner(InputBroker::kWheelerRefinedPluginId);
		if (Config::WheelBehavior::Gamepad::Open::HasOpenGraceMs && IsLastInputGamepad()) {
			const float graceMs = Config::WheelBehavior::Gamepad::Open::OpenGraceMs;
			if (graceMs > 0.0f) {
				_gamepadOpenGraceUntil = ImGui::GetTime() + (graceMs / 1000.0f);
				if (IsControllerDebugEnabled()) {
					logger::info("[Controller] OpenGrace active {:.0f}ms", graceMs);
				}
			} else {
				_gamepadOpenGraceUntil = 0.0;
			}
		} else {
			_gamepadOpenGraceUntil = 0.0;
		}
		// Sound feedback removed for compatibility with newer CommonLibSSE-NG
		// Notify External API of wheel open
		WheelerAPI::NotifyWheelStateChanged(GetActiveWheelIndex(), true);
	}
}

void Wheeler::CloseWheeler()
{
	if (_wheelerOwnedPauseMenu) {
		logger::info("[TimeDilation] MainWheel restore: closing owned pause menu");
		ClosePauseMenu();
		_wheelerOwnedPauseMenu = false;
	}
	// Always restore pause/timescale first, even if player isn't loaded (prevents stuck slow time on save load/death)
	if (_wheelerModifiedTimeScale) {
		Utils::Time::SGTM(_preWheelerTimeScale);
		_wheelerModifiedTimeScale = false;
	}

	DisableEditModeGameplayInputBlock();
	
	if (!RE::PlayerCharacter::GetSingleton() || !RE::PlayerCharacter::GetSingleton()->Is3DLoaded()) {
		_state = WheelState::KClosed;  // Still update state even without player
		return;
	}
	if (_state != WheelState::KClosed) {
		MainWheelDebug::Log(MainWheelDebug::Category::OpenClose, "Close (state={}, editMode={})",
			static_cast<int>(_state), _editMode);
		if (Config::Styling::Wheel::BlurOnOpen) {
			RE::UIBlurManager::GetSingleton()->DecrementBlurCount();
		}
		if (_activeWheelIdx >= 0 && _activeWheelIdx < _wheels.size()) {
			_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);  // reset active entry on close
			_wheels[_activeWheelIdx]->ResetAnimation();
		}
		_openTimer = 0;
		_closeTimer = 0;
		// Reset RTU hand override state for next wheel open
		_handOverrideActive = false;
		_handOverrideEntryIdx = -1;
		_handOverrideLeft = false;
		_rtuAppliedThisOpen = false;
		_rtuAppliedEntryIdx = -1;
		_rtuAppliedLeft = false;
		_rtuConsumedByInstant = false;
		// Reset timed instant cast attempt state
		_instantEntryIdx = -1;
		_instantAttemptActive = false;
		_instantReady = false;
		_instantCancelled = false;
		_instantSuppressForEntry = false;
		_instantElapsedSec = 0.0f;
		_instantRtuSource = false;
		_gamepadOpenGraceUntil = 0.0;
	}
	_state = WheelState::KClosed;
	if (!IsAmmoWheelOpen()) {
		InputBroker::ClearActiveOwner(InputBroker::kWheelerRefinedPluginId);
	}
	// Notify External API of wheel close
	WheelerAPI::NotifyWheelStateChanged(GetActiveWheelIndex(), false);
}

void Wheeler::EnableEditModeGameplayInputBlock()
{
	if (_editModeGameplayInputBlocker.active) {
		return;
	}

	RE::ControlMap* controlMap = RE::ControlMap::GetSingleton();
	if (!controlMap) {
		return;
	}

	_editModeGameplayInputBlocker.disabledByUsMask = 0;
	for (const auto flag : kEditModeGameplayBlockFlags) {
		if (controlMap->AreControlsEnabled(flag)) {
			controlMap->ToggleControls(flag, false);
			_editModeGameplayInputBlocker.disabledByUsMask |= static_cast<std::uint32_t>(flag);
		}
	}
	_editModeGameplayInputBlocker.active = true;
}

void Wheeler::DisableEditModeGameplayInputBlock()
{
	if (!_editModeGameplayInputBlocker.active) {
		return;
	}

	RE::ControlMap* controlMap = RE::ControlMap::GetSingleton();
	if (!controlMap) {
		_editModeGameplayInputBlocker.active = false;
		_editModeGameplayInputBlocker.disabledByUsMask = 0;
		return;
	}

	for (const auto flag : kEditModeGameplayBlockFlags) {
		const std::uint32_t mask = static_cast<std::uint32_t>(flag);
		if ((_editModeGameplayInputBlocker.disabledByUsMask & mask) != 0) {
			controlMap->ToggleControls(flag, true);
		}
	}

	_editModeGameplayInputBlocker.active = false;
	_editModeGameplayInputBlocker.disabledByUsMask = 0;
}

void Wheeler::EnsureTimescaleRestored()
{
	if (_wheelerOwnedPauseMenu) {
		logger::info("[TimeDilation] MainWheel restore: closing owned pause menu");
		ClosePauseMenu();
		_wheelerOwnedPauseMenu = false;
	}
	// MainWheel timescale restore (idempotent)
	if (_wheelerModifiedTimeScale) {
		float current = Utils::Time::GGTM();
		logger::info("[TimeDilation] MainWheel restore: current={:.3f}, restoreTo={:.3f}", 
			current, _preWheelerTimeScale);
		Utils::Time::SGTM(_preWheelerTimeScale);
		_wheelerModifiedTimeScale = false;
	}
	
	// AmmoWheel timescale restore (idempotent)
	if (_ammoWheel && _ammoWheel->HasModifiedTimescale()) {
		_ammoWheel->RestoreTimescale();
	}
}

void Wheeler::UpdateCursorPosMouse(float a_deltaX, float a_deltaY)
{
	if (_state == WheelState::KClosed) {
		return;
	}
	UpdateLastInputDevice(LastInputDevice::MKB);
	
	// Center Slowdown: scale mouse delta by distance-based gain for "reduced DPI" feel near center.
	// Only applies to Main Wheel (not AmmoWheel, which has its own cursor).
	float gain = 1.0f;
	if (Config::MainWheel::Mouse::CenterSlowdownEnabled) {
		const float maxRadius = getCursorRadiusMax();
		const float dist = std::sqrt(_cursorPos.x * _cursorPos.x + _cursorPos.y * _cursorPos.y);
		// Normalized distance (0 at center, 1 at edge)
		const float t = (maxRadius > 1e-4f) ? std::clamp(dist / maxRadius, 0.0f, 1.0f) : 0.0f;
		// Apply power curve for smoother transition
		const float tCurved = std::pow(t, Config::MainWheel::Mouse::CurvePower);
		// Interpolate gain between GainCenter and GainOuter
		gain = Config::MainWheel::Mouse::GainCenter + 
		       (Config::MainWheel::Mouse::GainOuter - Config::MainWheel::Mouse::GainCenter) * tCurved;
	}
	
	ImVec2 newPos = _cursorPos + ImVec2{ a_deltaX * gain, a_deltaY * gain };
	// Calculate the distance from the wheel center to the new cursor position
	float distanceFromCenter = sqrt(newPos.x * newPos.x + newPos.y * newPos.y);

	// If the distance exceeds the cursor radius, adjust the cursor position
	float cursorRadius = getCursorRadiusMax();
	if (distanceFromCenter > cursorRadius) {
		// Calculate the normalized direction vector from the center to the new position
		ImVec2 direction = newPos / distanceFromCenter;

		// Set the cursor position at the edge of the cursor radius
		newPos = direction * cursorRadius;
	}

	_cursorPos = newPos;
}

void Wheeler::UpdateCursorPosGamepad(float a_x, float a_y)
{
	if (_state == WheelState::KClosed) {
		return;
	}
	UpdateLastInputDevice(LastInputDevice::Gamepad);

	const bool hasInnerDeadzone = Config::WheelBehavior::Gamepad::Nav::HasInnerDeadzone;
	const bool hasOuterDeadzone = Config::WheelBehavior::Gamepad::Nav::HasOuterDeadzone;
	const bool hasIntentMagnitude = Config::WheelBehavior::Gamepad::Nav::HasIntentMagnitude;
	const bool hasSmoothing = Config::WheelBehavior::Gamepad::Nav::HasSmoothingHalfLifeMs;
	const bool autoCenterRestSnap = Config::WheelBehavior::Gamepad::Nav::AutoCenterRestSnap;
	const bool hasOpenGrace = Config::WheelBehavior::Gamepad::Open::HasOpenGraceMs &&
		Config::WheelBehavior::Gamepad::Open::OpenGraceMs > 0.0f;

	const float rawX = a_x;
	const float rawY = -a_y;
	const float rawMag = std::sqrt(rawX * rawX + rawY * rawY);
	const double now = ImGui::GetTime();

	if (hasOpenGrace && now < _gamepadOpenGraceUntil) {
		float graceThreshold = 0.1f;
		if (hasIntentMagnitude) {
			graceThreshold = Config::WheelBehavior::Gamepad::Nav::IntentMagnitude;
		} else if (hasInnerDeadzone) {
			graceThreshold = Config::WheelBehavior::Gamepad::Nav::InnerDeadzone;
		}
		if (rawMag < graceThreshold) {
			return;
		}
	}

	bool inDeadzone = false;
	ImVec2 target{ rawX, rawY };

	if (hasInnerDeadzone) {
		const float inner = std::clamp(Config::WheelBehavior::Gamepad::Nav::InnerDeadzone, 0.0f, 0.95f);
		inDeadzone = rawMag < inner;
		if (inDeadzone || rawMag <= 1e-4f) {
			target = { 0.0f, 0.0f };
		} else {
			float mag2 = (rawMag - inner) / (1.0f - inner);
			mag2 = std::clamp(mag2, 0.0f, 1.0f);
			const float invMag = 1.0f / rawMag;
			target = { rawX * invMag * mag2, rawY * invMag * mag2 };
		}
	} else {
		static constexpr float kLegacyDeadzone = 0.1f;
		inDeadzone = std::fabs(a_x) <= kLegacyDeadzone && std::fabs(a_y) <= kLegacyDeadzone;
	}

	_gamepadInDeadzone = inDeadzone;
	if (IsControllerDebugEnabled() && inDeadzone != _lastLoggedGamepadDeadzone) {
		logger::info("[Controller] Deadzone={} mag={:.3f}",
			inDeadzone ? "inside" : "outside",
			rawMag);
		_lastLoggedGamepadDeadzone = inDeadzone;
	}

	// Deadzone behavior:
	// - AutoCenterRestSnap ON: hard-snap to center rest.
	// - AutoCenterRestSnap OFF: smoothly decay toward center so slight pull back can rest at center.
	if (inDeadzone) {
		_gamepadFilteredMagnitude = 0.0f;
		if (hasIntentMagnitude) {
			const bool intentActive = false;
			if (IsControllerDebugEnabled() && intentActive != _lastLoggedGamepadIntentActive) {
				logger::info("[Controller] Intent={} mag={:.3f} threshold={:.3f}",
					intentActive ? "active" : "inactive",
					rawMag,
					Config::WheelBehavior::Gamepad::Nav::IntentMagnitude);
				_lastLoggedGamepadIntentActive = intentActive;
			}
			_gamepadIntentActive = intentActive;
		} else {
			_gamepadIntentActive = true;
		}
		if (autoCenterRestSnap) {
			_gamepadSmoothed = { 0.0f, 0.0f };
			_gamepadSmoothedInitialized = hasSmoothing;
			_gamepadLastUpdateTime = now;
			_cursorPos = { 0.0f, 0.0f };
			return;
		}
		// Manual center-rest mode: keep processing and let smoothing decay to zero.
		target = { 0.0f, 0.0f };
	}

	bool outerClampActive = false;
	if (hasOuterDeadzone) {
		const float outer = std::clamp(Config::WheelBehavior::Gamepad::Nav::OuterDeadzone, 0.0f, 1.0f);
		const float mag = std::sqrt(target.x * target.x + target.y * target.y);
		if (mag > outer && mag > 1e-4f) {
			const float scale = outer / mag;
			target.x *= scale;
			target.y *= scale;
			outerClampActive = true;
		}
	}
	_gamepadOuterClampActive = outerClampActive;
	if (IsControllerDebugEnabled() && outerClampActive != _lastLoggedGamepadOuterClampActive) {
		logger::info("[Controller] OuterClamp={} limit={:.3f}",
			outerClampActive ? "on" : "off",
			Config::WheelBehavior::Gamepad::Nav::OuterDeadzone);
		_lastLoggedGamepadOuterClampActive = outerClampActive;
	}

	ImVec2 smoothed = target;
	if (hasSmoothing) {
		const float halfLifeSec = Config::WheelBehavior::Gamepad::Nav::SmoothingHalfLifeMs / 1000.0f;
		float alpha = 1.0f;
		if (_gamepadSmoothedInitialized && halfLifeSec > 0.0f) {
			const float dt = static_cast<float>(now - _gamepadLastUpdateTime);
			if (dt > 0.0f) {
				alpha = 1.0f - std::exp2(-dt / halfLifeSec);
			}
		}
		smoothed.x = _gamepadSmoothed.x + (target.x - _gamepadSmoothed.x) * alpha;
		smoothed.y = _gamepadSmoothed.y + (target.y - _gamepadSmoothed.y) * alpha;
		_gamepadSmoothed = smoothed;
		_gamepadSmoothedInitialized = true;
		_gamepadLastUpdateTime = now;
	} else {
		_gamepadSmoothed = smoothed;
		_gamepadSmoothedInitialized = false;
		_gamepadLastUpdateTime = now;
	}

	const float filteredMag = std::sqrt(smoothed.x * smoothed.x + smoothed.y * smoothed.y);
	_gamepadFilteredMagnitude = filteredMag;

	if (hasIntentMagnitude) {
		const bool intentActive = filteredMag >= Config::WheelBehavior::Gamepad::Nav::IntentMagnitude;
		if (IsControllerDebugEnabled() && intentActive != _lastLoggedGamepadIntentActive) {
			logger::info("[Controller] Intent={} mag={:.3f} threshold={:.3f}",
				intentActive ? "active" : "inactive",
				filteredMag,
				Config::WheelBehavior::Gamepad::Nav::IntentMagnitude);
			_lastLoggedGamepadIntentActive = intentActive;
		}
		_gamepadIntentActive = intentActive;
	} else {
		_gamepadIntentActive = true;
	}

	if (hasIntentMagnitude && !_gamepadIntentActive && !(hasInnerDeadzone && inDeadzone)) {
		return;
	}

	const float cursorRadius = getCursorRadiusMax();
	_cursorPos.x = smoothed.x * cursorRadius;
	_cursorPos.y = smoothed.y * cursorRadius;
}

void Wheeler::NextWheel()
{
	if (_state == WheelState::KOpened) {
		if (_wheels.empty()) {
			return;
		}

		_cursorPos = { 0, 0 };
		if (_activeWheelIdx >= 0 && _activeWheelIdx < static_cast<int>(_wheels.size())) {
			_wheels[_activeWheelIdx]->ResetAnimation();
			_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1); // reset active entry for current wheel
		}

		const int wheelCount = static_cast<int>(_wheels.size());
		const bool isTransformed = !TransformWheelManager::IsPlayerHuman();
		const bool debugTransform = Config::WheelBehavior::TransformWheels::DebugLog;
		const auto isTransformIdx = [](int idx) {
			return TransformWheelManager::IsTransformWheelIndex(idx);
		};

		if (isTransformed) {
			int startIdx = (_activeWheelIdx >= 0 && _activeWheelIdx < wheelCount) ? _activeWheelIdx : 0;
			if (!isTransformIdx(startIdx)) {
				int firstTransform = -1;
				for (int i = 0; i < wheelCount; ++i) {
					if (isTransformIdx(i)) {
						firstTransform = i;
						break;
					}
				}
				if (firstTransform < 0) {
					return;
				}
				startIdx = firstTransform;
			}

			int nextIdx = startIdx;
			bool found = false;
			for (int attempts = 0; attempts < wheelCount; ++attempts) {
				nextIdx = nextIdx + 1;
				if (nextIdx >= wheelCount) {
					nextIdx = 0;
				}
				if (isTransformIdx(nextIdx)) {
					found = true;
					break;
				}
			}
			if (!found) {
				return;
			}
			if (nextIdx == _activeWheelIdx) {
				return;
			}

			const int oldIdx = _activeWheelIdx;
			_activeWheelIdx = nextIdx;
			_wheels[_activeWheelIdx]->ResetAnimation();
			_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);  // reset active entry for new wheel
			if (debugTransform) {
				logger::info("TransformWheels: NextWheel transformed {} -> {} tag='{}'",
					oldIdx, _activeWheelIdx, _wheels[_activeWheelIdx]->GetClientTag());
			}
			return;
		}

		int startIdx = (_activeWheelIdx >= 0 && _activeWheelIdx < wheelCount) ? _activeWheelIdx : 0;
		int nextIdx = startIdx;
		bool found = false;
		for (int attempts = 0; attempts < wheelCount; ++attempts) {
			nextIdx = nextIdx + 1;
			if (nextIdx >= wheelCount) {
				nextIdx = 0;
			}
			if (!isTransformIdx(nextIdx)) {
				found = true;
				break;
			}
		}
		if (!found) {
			return;
		}
		_activeWheelIdx = nextIdx;
		_wheels[_activeWheelIdx]->ResetAnimation();
		_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);  // reset active entry for new wheel
		// Sound feedback removed for compatibility with newer CommonLibSSE-NG
	}
}

void Wheeler::PrevWheel()
{
	if (_state == WheelState::KOpened) {
		if (_wheels.empty()) {
			return;
		}

		_cursorPos = { 0, 0 };
		if (_activeWheelIdx >= 0 && _activeWheelIdx < static_cast<int>(_wheels.size())) {
			_wheels[_activeWheelIdx]->ResetAnimation();
			_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1); // reset active entry for current wheel
		}

		const int wheelCount = static_cast<int>(_wheels.size());
		const bool isTransformed = !TransformWheelManager::IsPlayerHuman();
		const bool debugTransform = Config::WheelBehavior::TransformWheels::DebugLog;
		const auto isTransformIdx = [](int idx) {
			return TransformWheelManager::IsTransformWheelIndex(idx);
		};

		if (isTransformed) {
			int startIdx = (_activeWheelIdx >= 0 && _activeWheelIdx < wheelCount) ? _activeWheelIdx : 0;
			if (!isTransformIdx(startIdx)) {
				int firstTransform = -1;
				for (int i = 0; i < wheelCount; ++i) {
					if (isTransformIdx(i)) {
						firstTransform = i;
						break;
					}
				}
				if (firstTransform < 0) {
					return;
				}
				startIdx = firstTransform;
			}

			int nextIdx = startIdx;
			bool found = false;
			for (int attempts = 0; attempts < wheelCount; ++attempts) {
				nextIdx = nextIdx - 1;
				if (nextIdx < 0) {
					nextIdx = wheelCount - 1;
				}
				if (isTransformIdx(nextIdx)) {
					found = true;
					break;
				}
			}
			if (!found) {
				return;
			}
			if (nextIdx == _activeWheelIdx) {
				return;
			}

			const int oldIdx = _activeWheelIdx;
			_activeWheelIdx = nextIdx;
			_wheels[_activeWheelIdx]->ResetAnimation();
			_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);  // reset active entry for new wheel
			if (debugTransform) {
				logger::info("TransformWheels: PrevWheel transformed {} -> {} tag='{}'",
					oldIdx, _activeWheelIdx, _wheels[_activeWheelIdx]->GetClientTag());
			}
			return;
		}

		int startIdx = (_activeWheelIdx >= 0 && _activeWheelIdx < wheelCount) ? _activeWheelIdx : 0;
		int nextIdx = startIdx;
		bool found = false;
		for (int attempts = 0; attempts < wheelCount; ++attempts) {
			nextIdx = nextIdx - 1;
			if (nextIdx < 0) {
				nextIdx = wheelCount - 1;
			}
			if (!isTransformIdx(nextIdx)) {
				found = true;
				break;
			}
		}
		if (!found) {
			return;
		}
		_activeWheelIdx = nextIdx;
		_wheels[_activeWheelIdx]->ResetAnimation();
		_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);  // reset active entry for new wheel
		// Sound feedback removed for compatibility with newer CommonLibSSE-NG
	}
}

void Wheeler::PrevItemInEntry()
{
	if (_state == WheelState::KOpened) {
		_wheels[_activeWheelIdx]->PrevItemInHoveredEntry();
	}
}

void Wheeler::NextItemInEntry()
{
	if (_state == WheelState::KOpened) {
		_wheels[_activeWheelIdx]->NextItemInHoveredEntry();
	}
}

void Wheeler::PrevItemInEntryGamepad()
{
	UpdateLastInputDevice(LastInputDevice::Gamepad);
	if (!Config::WheelBehavior::Gamepad::DPad::HasMode ||
		Config::WheelBehavior::Gamepad::DPad::ModeValue == Config::WheelBehavior::Gamepad::DPad::Mode::Items) {
		PrevItemInEntry();
		return;
	}
	if (_state != WheelState::KOpened) {
		return;
	}
	if (_wheels.empty() || _activeWheelIdx < 0 || _activeWheelIdx >= static_cast<int>(_wheels.size())) {
		return;
	}
	if (!_wheels[_activeWheelIdx]) {
		return;
	}
	const int entryCount = _wheels[_activeWheelIdx]->GetNumEntries();
	if (entryCount <= 0) {
		return;
	}
	int currentIdx = _wheels[_activeWheelIdx]->GetHoveredEntryIndex();
	if (currentIdx < 0 || currentIdx >= entryCount) {
		if (_activeWheelIdx >= 0 && _activeWheelIdx < static_cast<int>(_lastHoveredEntryByWheel.size())) {
			const int lastIdx = _lastHoveredEntryByWheel[_activeWheelIdx];
			if (lastIdx >= 0 && lastIdx < entryCount) {
				currentIdx = lastIdx;
			}
		}
		if (currentIdx < 0 || currentIdx >= entryCount) {
			currentIdx = 0;
		}
	}
	int nextIdx = currentIdx - 1;
	if (nextIdx < 0) {
		nextIdx = entryCount - 1;
	}
	_wheels[_activeWheelIdx]->SetHoveredEntryIndex(nextIdx);
	const float angle = GetEntryCenterAngleRad(nextIdx, entryCount);
	const float cursorRadius = getCursorRadiusMax();
	_cursorPos = { cosf(angle) * cursorRadius, sinf(angle) * cursorRadius };
	_gamepadIntentActive = true;
}

void Wheeler::NextItemInEntryGamepad()
{
	UpdateLastInputDevice(LastInputDevice::Gamepad);
	if (!Config::WheelBehavior::Gamepad::DPad::HasMode ||
		Config::WheelBehavior::Gamepad::DPad::ModeValue == Config::WheelBehavior::Gamepad::DPad::Mode::Items) {
		NextItemInEntry();
		return;
	}
	if (_state != WheelState::KOpened) {
		return;
	}
	if (_wheels.empty() || _activeWheelIdx < 0 || _activeWheelIdx >= static_cast<int>(_wheels.size())) {
		return;
	}
	if (!_wheels[_activeWheelIdx]) {
		return;
	}
	const int entryCount = _wheels[_activeWheelIdx]->GetNumEntries();
	if (entryCount <= 0) {
		return;
	}
	int currentIdx = _wheels[_activeWheelIdx]->GetHoveredEntryIndex();
	if (currentIdx < 0 || currentIdx >= entryCount) {
		if (_activeWheelIdx >= 0 && _activeWheelIdx < static_cast<int>(_lastHoveredEntryByWheel.size())) {
			const int lastIdx = _lastHoveredEntryByWheel[_activeWheelIdx];
			if (lastIdx >= 0 && lastIdx < entryCount) {
				currentIdx = lastIdx;
			}
		}
		if (currentIdx < 0 || currentIdx >= entryCount) {
			currentIdx = 0;
		}
	}
	int nextIdx = currentIdx + 1;
	if (nextIdx >= entryCount) {
		nextIdx = 0;
	}
	_wheels[_activeWheelIdx]->SetHoveredEntryIndex(nextIdx);
	const float angle = GetEntryCenterAngleRad(nextIdx, entryCount);
	const float cursorRadius = getCursorRadiusMax();
	_cursorPos = { cosf(angle) * cursorRadius, sinf(angle) * cursorRadius };
	_gamepadIntentActive = true;
}

bool Wheeler::GetCursorAngleRadian(float& r_ret)
{
	if (_cursorPos.x != 0 || _cursorPos.y != 0) {
		r_ret = atan2(_cursorPos.y, _cursorPos.x);
		return true;
	}
	return false;
}

bool Wheeler::IsLastInputGamepad()
{
	return _lastInputDevice == LastInputDevice::Gamepad;
}

float Wheeler::GetCursorDistance()
{
	return std::sqrt(_cursorPos.x * _cursorPos.x + _cursorPos.y * _cursorPos.y);
}


void Wheeler::ActivateHoveredEntrySecondary()
{
	if (_wheels.empty()) {
		return;
	}
	if (_state != WheelState::KOpened) {
		return;
	}
	if (_state == WheelState::KOpened) {
		std::unique_ptr<Wheel>& activeWheel = _wheels[_activeWheelIdx];
		if (activeWheel->IsEmpty()) {         // empty wheel, we can only delete in edit mode.
			if (_editMode && _wheels.size() > 1) {  // we have more than one wheel, so it's safe to delete this one.
				DeleteCurrentWheel();
			}
		} else {
			if (_editMode) {
				// Edit mode: deletion behavior (unchanged)
				activeWheel->ActivateHoveredEntrySecondary(_editMode);
			} else {
				// Non-edit mode: RMB pressed
				// RTU OFF: directly equip to left hand (vanilla behavior)
				// RTU ON: latch left-hand override for RTU release
				const int hoveredIdx = activeWheel->GetHoveredEntryIndex();
				if (hoveredIdx < 0) {
					return;
				}
				if (WheelEntry* entry = activeWheel->GetEntry(hoveredIdx); entry && entry->IsMissingInInventory()) {
					return;
				}
				if (!Config::WheelBehavior::ReleaseToUse) {
					// RTU OFF: directly equip to left hand now
					activeWheel->ActivateHoveredEntrySecondary(false);
					_activateOnCloseFired = true;
					
					// Diagnostic log
					std::shared_ptr<WheelItem> item = activeWheel->GetHoveredSelectedItem();
					RE::FormID formId = item ? item->GetFormID() : 0;
					logger::info("Activation: rtu=OFF, entry={}, hand=LEFT, action=equip (RMB direct), formId={:08X}",
						hoveredIdx, formId);
					if (ShouldNotifyHandMemory(item, ReleaseAction::Equip)) {
						HandMemory::NotifyWheelEquipOrCast();
					}
					
					PlaySoundByEditorID(Config::Sounds::ActivateSoundEditorID.c_str(), Config::Sounds::ActivateSoundVolume);
					if (Config::WheelBehavior::CloseWheelAfterUse) {
						TryCloseWheeler();
					}
				} else {
					// RTU ON: latch left-hand override (equip happens on RTU release)
					LatchHandOverrideLeft();
				}
			}
		}
	}
}

void Wheeler::ActivateHoveredEntryPrimary()
{
	if (_wheels.empty()) {
		return;
	}
	if (_state != WheelState::KOpened) {
		return;
	}
	if (_state == WheelState::KOpened) {
		std::shared_ptr<WheelItem> hoveredItem;
		if (!_editMode) {
			std::unique_ptr<Wheel>& activeWheel = _wheels[_activeWheelIdx];
			if (activeWheel) {
				const int hoveredIdx = activeWheel->GetHoveredEntryIndex();
				if (hoveredIdx < 0) {
					return;
				}
				if (WheelEntry* entry = activeWheel->GetEntry(hoveredIdx); entry && entry->IsMissingInInventory()) {
					return;
				}
				hoveredItem = activeWheel->GetHoveredSelectedItem();
			}
		}
		_wheels[_activeWheelIdx]->ActivateHoveredEntryPrimary(_editMode);
		if (!_editMode && hoveredItem && ShouldNotifyHandMemory(hoveredItem, ReleaseAction::Equip)) {
			HandMemory::NotifyWheelEquipOrCast();
		}
		if (!_editMode && Config::WheelBehavior::CloseWheelAfterUse) {
			// Prevent RTU activation on close from double-activating the hovered slot.
			_activateOnCloseFired = true;
			TryCloseWheeler();
		}
	}
}

void Wheeler::ActivateHoveredEntrySpecial()
{
	if (_wheels.empty()) {
		return;
	}
	if (_state == WheelState::KOpened) {
		if (!_editMode) {
			std::unique_ptr<Wheel>& activeWheel = _wheels[_activeWheelIdx];
			if (activeWheel) {
				const int hoveredIdx = activeWheel->GetHoveredEntryIndex();
				if (hoveredIdx < 0) {
					return;
				}
				if (WheelEntry* entry = activeWheel->GetEntry(hoveredIdx); entry && entry->IsMissingInInventory()) {
					return;
				}
			}
		}
		_wheels[_activeWheelIdx]->ActivateHoveredEntrySpecial(_editMode);
		if (!_editMode && Config::WheelBehavior::CloseWheelAfterUse) {
			// Prevent RTU activation on close from double-activating the hovered slot.
			_activateOnCloseFired = true;
			TryCloseWheeler();
		}
	}
}

void Wheeler::OnConfirmDown()
{
	if (_wheels.empty() || _state != WheelState::KOpened) {
		return;
	}
	
	// In edit mode, use immediate activation (add item to wheel)
	if (_editMode) {
		_wheels[_activeWheelIdx]->ActivateHoveredEntryPrimary(true);
		return;
	}
	
	std::unique_ptr<Wheel>& activeWheel = _wheels[_activeWheelIdx];
	if (!activeWheel) {
		return;
	}
	
	const int hoveredIdx = activeWheel->GetHoveredEntryIndex();
	if (hoveredIdx < 0) {
		return;
	}
	if (WheelEntry* entry = activeWheel->GetEntry(hoveredIdx); entry && entry->IsMissingInInventory()) {
		return;
	}
	
	// Start hold timing for Hold-to-Use
	_confirmHeld = true;
	_confirmHoldStartTime = ImGui::GetTime();
	_confirmHoldSeconds = 0.f;
	_confirmHoldEntryIdx = hoveredIdx;

	MainWheelDebug::Log(MainWheelDebug::Category::Input, "ConfirmDown entry={}", hoveredIdx);
	if (IsControllerDebugEnabled()) {
		logger::info("[Controller] ConfirmDown entry={}", hoveredIdx);
	}
}

void Wheeler::OnConfirmUp()
{
	if (!_confirmHeld) {
		return;
	}
	
	const float holdDuration = _confirmHoldSeconds;
	const int heldEntryIdx = _confirmHoldEntryIdx;
	
	// Reset hold state
	_confirmHeld = false;
	_confirmHoldSeconds = 0.f;
	_confirmHoldEntryIdx = -1;
	
	// Reset shout stage sounds on confirm release (Hold-to-Use mode)
	ResetShoutStageSounds();
	
	if (_wheels.empty() || _state != WheelState::KOpened || _editMode) {
		return;
	}
	
	std::unique_ptr<Wheel>& activeWheel = _wheels[_activeWheelIdx];
	if (!activeWheel) {
		return;
	}
	
	const int currentHoveredIdx = activeWheel->GetHoveredEntryIndex();
	
	// Only activate if still hovering the same entry
	if (currentHoveredIdx != heldEntryIdx || currentHoveredIdx < 0) {
		MainWheelDebug::Log(MainWheelDebug::Category::Input, "ConfirmUp entry changed ({} -> {})", heldEntryIdx, currentHoveredIdx);
		if (IsControllerDebugEnabled()) {
			logger::info("[Controller] ConfirmUp entry changed ({} -> {})", heldEntryIdx, currentHoveredIdx);
		}
		return;
	}
	
	if (WheelEntry* entry = activeWheel->GetEntry(currentHoveredIdx); entry && entry->IsMissingInInventory()) {
		return;
	}
	std::shared_ptr<WheelItem> hoveredItem = activeWheel->GetHoveredSelectedItem();
	if (!hoveredItem) {
		return;
	}
	
	if (IsControllerDebugEnabled()) {
		logger::info("[Controller] ConfirmUp entry={} hold={:.2f}s", currentHoveredIdx, holdDuration);
	}
	MainWheelDebug::Log(MainWheelDebug::Category::Input, "ConfirmUp entry={} hold={:.2f}s", currentHoveredIdx, holdDuration);
	
	// Try instant activation for shouts/spells (works even when RTU is OFF)
	bool activated = false;
	const char* sourceLabel = "Primary";

	ReleaseAction resolvedAction = ReleaseAction::Equip;
	const RE::FormID formId = hoveredItem ? hoveredItem->GetFormID() : 0;
	std::shared_ptr<WheelItemShout> shoutItem = std::dynamic_pointer_cast<WheelItemShout>(hoveredItem);
	std::shared_ptr<WheelItemSpell> spellItem = std::dynamic_pointer_cast<WheelItemSpell>(hoveredItem);

	// InstantShout via Hold-to-Use
	// Threshold to distinguish Tap (Equip) vs Hold (Shout)
	// User requested delay to prevent instant firing on quick taps
	constexpr float kInstantShoutThreshold = 0.60f; // 600ms
	const bool shoutReady = shoutItem && Config::WheelBehavior::InstantShout && holdDuration >= kInstantShoutThreshold;
	if (shoutReady) {
		resolvedAction = ReleaseAction::CastShout;
	}

	// InstantSpell via Hold-to-Use (with threshold check for release-to-cast)
	// Compute per-category instantEnabled through centralized gate helper.
	bool spellReady = false;
	bool isInTransform = false;
	if (resolvedAction != ReleaseAction::CastShout && spellItem) {
		isInTransform = !TransformWheelManager::IsPlayerHuman();
		RE::SpellItem* spell = spellItem->GetSpell();
		const bool instantEnabled = IsInstantEnabledForSpell(spell, isInTransform);
		LogInstantGateDecision("HoldPrimary", spell, isInTransform, instantEnabled);

		if (instantEnabled) {
			// Use max of 0.6s (safety) and User Config Threshold (Hold Threshold)
			const float configThreshold = Config::WheelBehavior::InstantSpellHoldThresholdMs / 1000.0f;
			const float safetyThreshold = Config::WheelBehavior::HoldToCastSafetyThresholdMs / 1000.0f;
						const float kInstantSpellThreshold = (std::max)(safetyThreshold, configThreshold);
			const bool reachedThreshold = holdDuration >= kInstantSpellThreshold;
			const float thresholdSec = kInstantSpellThreshold; // For logging
			const bool isConcentration = IsConcentrationSpellType(spell);
			const bool concentrationAllowed = IsConcentrationInstantAllowed(spell);

			// Log threshold status
			if (Config::WheelBehavior::InstantSpellDebugLog || Config::WheelBehavior::InstantTransformationsDebugLog) {
				logger::info("Hold-to-Use InstantSpell: holdDuration={:.2f}s, threshold={:.2f}s, ready={}, mode={}, cancelled={}, inTransform={}, isConcentration={}, concentrationAllowed={}",
					holdDuration,
					thresholdSec,
					reachedThreshold,
					Config::WheelBehavior::InstantSpellConcentrationMode,
					_instantCancelled,
					isInTransform,
					isConcentration,
					concentrationAllowed);
			}

			// Only cast if threshold reached and concentration policy allows this spell.
			if (concentrationAllowed && reachedThreshold && !_instantCancelled && !_instantSuppressForEntry) {
				spellReady = true;
				resolvedAction = ReleaseAction::CastSpell;
			} else {
				// Threshold not met or cancelled - fall through to normal equip
				if (Config::WheelBehavior::InstantSpellDebugLog || Config::WheelBehavior::InstantTransformationsDebugLog) {
					logger::info("Hold-to-Use InstantSpell: NOT casting (threshold not met, cancelled, suppressed, or concentration disallowed), fallback to equip");
				}
			}
		}
	}

	logger::info("ReleaseResolve[Hold]: source={} action={} entry={} formId={:08X}",
		sourceLabel, GetReleaseActionName(resolvedAction), currentHoveredIdx, formId);

	const TargetHand resolvedHand = ResolveTargetHand(currentHoveredIdx);
	logger::info("ReleaseResolve[Hold]: source={} hand={} entry={} formId={:08X}",
		sourceLabel, GetTargetHandName(resolvedHand), currentHoveredIdx, formId);

	if (resolvedAction == ReleaseAction::CastShout && shoutItem && Config::WheelBehavior::InstantShout) {
		if (shoutReady) {
			// Adjust effective time so the shout logic sees time starting from 0 after threshold
			// This matches the indicator visual change we will make
			activated = shoutItem->CastImmediate(holdDuration - kInstantShoutThreshold);
			if (activated) {
				logger::info("Hold-to-Use: InstantShout cast with holdDuration={:.2f}s (eff={:.2f}s)",
					holdDuration, holdDuration - kInstantShoutThreshold);
			} else {
				logger::info("ReleaseResolve[Hold]: shout cast failed, fallback to equip entry={} formId={:08X}",
					currentHoveredIdx, formId);
			}
		}
		if (!activated) {
			resolvedAction = ReleaseAction::Equip;
		}
	}

	if (!activated && resolvedAction == ReleaseAction::CastSpell && spellItem && spellReady) {
		RE::SpellItem* spell = spellItem->GetSpell();
		if (spell && IsPowerSpellType(spell)) {
			activated = QueuePowerActivation(spell->GetFormID());
			if (activated) {
				logger::info("Hold-to-Use: queued vanilla power activation entry={} hand={} formId={:08X}",
					currentHoveredIdx, GetTargetHandName(resolvedHand), spell->GetFormID());
				_rtuConsumedByInstant = true;
			} else {
				logger::info("ReleaseResolve[Hold]: power queue failed, fallback to equip entry={} formId={:08X}",
					currentHoveredIdx, formId);
				resolvedAction = ReleaseAction::Equip;
			}
		} else {
			const auto castingSource = GetCastingSourceForHand(resolvedHand);
			activated = spellItem->CastImmediate(true, castingSource);
			if (activated) {
				logger::info("Hold-to-Use: InstantSpell cast on release (Timed) entry={} inTransform={} hand={}",
					currentHoveredIdx, isInTransform, GetTargetHandName(resolvedHand));
				_rtuConsumedByInstant = true;
			} else {
				logger::info("ReleaseResolve[Hold]: cast failed, fallback to equip entry={} formId={:08X}",
					currentHoveredIdx, formId);
				resolvedAction = ReleaseAction::Equip;
			}
		}
	}
	
	// Fallback to normal activation (equip)
	if (!activated) {
		// Use centralized hand resolution (decoupled from RTU)
		const bool useLeft = (resolvedHand == TargetHand::Left);

		bool equipBlocked = false;
		const char* equipReason = "ok";
		if (spellItem) {
			RE::SpellItem* spell = spellItem->GetSpell();
			if (spell && !IsPowerSpellType(spell)) {
				equipBlocked = IsSpellEquippedInHand(RE::PlayerCharacter::GetSingleton(), formId, resolvedHand);
				if (equipBlocked) {
					equipReason = "blocked_because_target_hand_already_had_it";
				}
			}
		}
		logger::info("ReleaseResolve[Hold]: source={} equip={} reason={} entry={} hand={} formId={:08X}",
			sourceLabel,
			equipBlocked ? "blocked" : "allowed",
			equipReason,
			currentHoveredIdx,
			GetTargetHandName(resolvedHand),
			formId);
		
		if (useLeft) {
			activeWheel->ActivateHoveredEntrySecondary(false);
		} else {
			activeWheel->ActivateHoveredEntryPrimary(false);
		}
		activated = true;
		
		// Diagnostic log (gated, once per activation)
		logger::info("Activation: source=HoldPrimary rtu={}, entry={}, hand={}, action=equip, formId={:08X}",
			Config::WheelBehavior::ReleaseToUse ? "ON" : "OFF",
			currentHoveredIdx,
			useLeft ? "LEFT" : "RIGHT",
			formId);
	}
	
	if (activated) {
		if (ShouldNotifyHandMemory(hoveredItem, resolvedAction)) {
			HandMemory::NotifyWheelEquipOrCast();
		}
		if (!_directActivatedThisOpenSession) {
			_directActivatedThisOpenSession = true;
			logger::info("DirectActivate: set directActivatedThisOpenSession=true source={} entry={} hand={}",
				sourceLabel, currentHoveredIdx, GetTargetHandName(resolvedHand));
		}
		_activateOnCloseFired = true;  // Prevent RTU double-activation
		// Suppress generic activate sound for shouts when stage sounds are active
		const bool shoutStageSoundsActive = Config::Sounds::EnableShoutStageSounds &&
			Config::Sounds::ShoutStageSoundMode != static_cast<std::uint32_t>(Config::ShoutStageSoundMode::Off);
		const bool isShout = std::dynamic_pointer_cast<WheelItemShout>(hoveredItem) != nullptr;
		if (!(isShout && shoutStageSoundsActive)) {
			PlaySoundByEditorID(Config::Sounds::ActivateSoundEditorID.c_str(), Config::Sounds::ActivateSoundVolume);
		}
		if (Config::WheelBehavior::CloseWheelAfterUse) {
			TryCloseWheeler();
		}
	}
}

void Wheeler::OnSecondaryConfirmDown()
{
	if (_wheels.empty() || _state != WheelState::KOpened) {
		return;
	}

	// In edit mode, use immediate secondary activation (delete)
	if (_editMode) {
		ActivateHoveredEntrySecondary();
		return;
	}

	std::unique_ptr<Wheel>& activeWheel = _wheels[_activeWheelIdx];
	if (!activeWheel) {
		return;
	}

	const int hoveredIdx = activeWheel->GetHoveredEntryIndex();
	if (hoveredIdx < 0) {
		return;
	}
	if (WheelEntry* entry = activeWheel->GetEntry(hoveredIdx); entry && entry->IsMissingInInventory()) {
		return;
	}

	MainWheelDebug::Log(MainWheelDebug::Category::Input, "SecondaryConfirmDown entry={}", hoveredIdx);
	if (IsControllerDebugEnabled()) {
		logger::info("[Controller] SecondaryConfirmDown entry={}", hoveredIdx);
	}

	// RTU OFF: preserve direct RMB activation behavior
	if (!Config::WheelBehavior::ReleaseToUse) {
		std::shared_ptr<WheelItem> hoveredItem = activeWheel->GetHoveredSelectedItem();
		if (std::shared_ptr<WheelItemSpell> spellItem = std::dynamic_pointer_cast<WheelItemSpell>(hoveredItem)) {
			RE::SpellItem* spell = spellItem->GetSpell();
			const bool isInTransform = !TransformWheelManager::IsPlayerHuman();
			const bool instantEnabled = IsInstantEnabledForSpell(spell, isInTransform);

			if (instantEnabled && IsBoundWeaponSpell(spell)) {
				_secondaryConfirmHeld = true;
				_secondaryConfirmHoldStartTime = ImGui::GetTime();
				_secondaryConfirmHoldSeconds = 0.f;
				_secondaryConfirmHoldEntryIdx = hoveredIdx;
				return;
			}
		}
		ActivateHoveredEntrySecondary();
		return;
	}

	// RTU ON: latch left hand for RTU close if held, and track hold timing
	LatchHandOverrideLeft();
	_secondaryConfirmHeld = true;
	_secondaryConfirmHoldStartTime = ImGui::GetTime();
	_secondaryConfirmHoldSeconds = 0.f;
	_secondaryConfirmHoldEntryIdx = hoveredIdx;
}

void Wheeler::OnSecondaryConfirmUp()
{
	if (!_secondaryConfirmHeld) {
		return;
	}

	const float holdDuration = _secondaryConfirmHoldSeconds;
	const int heldEntryIdx = _secondaryConfirmHoldEntryIdx;

	// Reset hold state
	_secondaryConfirmHeld = false;
	_secondaryConfirmHoldSeconds = 0.f;
	_secondaryConfirmHoldEntryIdx = -1;

	// Clear hand override after RMB release
	if (_handOverrideActive && _handOverrideEntryIdx == heldEntryIdx && _handOverrideLeft) {
		_handOverrideActive = false;
		_handOverrideEntryIdx = -1;
		_handOverrideLeft = false;
		logger::info("RTU: hand override cleared after secondary confirm release (entry={})", heldEntryIdx);
	}

	// Reset shout stage sounds on confirm release (Hold-to-Use mode)
	ResetShoutStageSounds();

	if (_wheels.empty() || _state != WheelState::KOpened || _editMode) {
		return;
	}

	std::unique_ptr<Wheel>& activeWheel = _wheels[_activeWheelIdx];
	if (!activeWheel) {
		return;
	}

	const int currentHoveredIdx = activeWheel->GetHoveredEntryIndex();

	// Only activate if still hovering the same entry
	if (currentHoveredIdx != heldEntryIdx || currentHoveredIdx < 0) {
		MainWheelDebug::Log(MainWheelDebug::Category::Input, "SecondaryConfirmUp entry changed ({} -> {})", heldEntryIdx, currentHoveredIdx);
		if (IsControllerDebugEnabled()) {
			logger::info("[Controller] SecondaryConfirmUp entry changed ({} -> {})", heldEntryIdx, currentHoveredIdx);
		}
		return;
	}

	if (WheelEntry* entry = activeWheel->GetEntry(currentHoveredIdx); entry && entry->IsMissingInInventory()) {
		return;
	}
	std::shared_ptr<WheelItem> hoveredItem = activeWheel->GetHoveredSelectedItem();
	if (!hoveredItem) {
		return;
	}

	if (IsControllerDebugEnabled()) {
		logger::info("[Controller] SecondaryConfirmUp entry={} hold={:.2f}s", currentHoveredIdx, holdDuration);
	}
	MainWheelDebug::Log(MainWheelDebug::Category::Input, "SecondaryConfirmUp entry={} hold={:.2f}s", currentHoveredIdx, holdDuration);

	// Try instant activation for shouts/spells (works even when RTU is OFF)
	bool activated = false;
	const char* sourceLabel = "Secondary";

	ReleaseAction resolvedAction = ReleaseAction::Equip;
	const RE::FormID formId = hoveredItem ? hoveredItem->GetFormID() : 0;
	std::shared_ptr<WheelItemShout> shoutItem = std::dynamic_pointer_cast<WheelItemShout>(hoveredItem);
	std::shared_ptr<WheelItemSpell> spellItem = std::dynamic_pointer_cast<WheelItemSpell>(hoveredItem);

	// InstantShout via Hold-to-Use
	// Threshold to distinguish Tap (Equip) vs Hold (Shout)
	// User requested delay to prevent instant firing on quick taps
	constexpr float kInstantShoutThreshold = 0.60f; // 600ms
	const bool shoutReady = shoutItem && Config::WheelBehavior::InstantShout && holdDuration >= kInstantShoutThreshold;
	if (shoutReady) {
		resolvedAction = ReleaseAction::CastShout;
	}

	// InstantSpell via Hold-to-Use (with threshold check for release-to-cast)
	// Compute per-category instantEnabled through centralized gate helper.
	bool spellReady = false;
	bool isInTransform = false;
	if (resolvedAction != ReleaseAction::CastShout && spellItem) {
		isInTransform = !TransformWheelManager::IsPlayerHuman();
		RE::SpellItem* spell = spellItem->GetSpell();
		const bool instantEnabled = IsInstantEnabledForSpell(spell, isInTransform);
		LogInstantGateDecision("HoldSecondary", spell, isInTransform, instantEnabled);

		if (instantEnabled) {
			// Use max of 0.6s (safety) and User Config Threshold (Hold Threshold)
			const float configThreshold = Config::WheelBehavior::InstantSpellHoldThresholdMs / 1000.0f;
			const float safetyThreshold = Config::WheelBehavior::HoldToCastSafetyThresholdMs / 1000.0f;
						const float kInstantSpellThreshold = (std::max)(safetyThreshold, configThreshold);
			const bool reachedThreshold = holdDuration >= kInstantSpellThreshold;
			const float thresholdSec = kInstantSpellThreshold; // For logging
			const bool isConcentration = IsConcentrationSpellType(spell);
			const bool concentrationAllowed = IsConcentrationInstantAllowed(spell);

			// Log threshold status
			if (Config::WheelBehavior::InstantSpellDebugLog || Config::WheelBehavior::InstantTransformationsDebugLog) {
				logger::info("Hold-to-Use InstantSpell: holdDuration={:.2f}s, threshold={:.2f}s, ready={}, mode={}, cancelled={}, inTransform={}, isConcentration={}, concentrationAllowed={}",
					holdDuration,
					thresholdSec,
					reachedThreshold,
					Config::WheelBehavior::InstantSpellConcentrationMode,
					_instantCancelled,
					isInTransform,
					isConcentration,
					concentrationAllowed);
			}

			// Only cast if threshold reached and concentration policy allows this spell.
			if (concentrationAllowed && reachedThreshold && !_instantCancelled && !_instantSuppressForEntry) {
				spellReady = true;
				resolvedAction = ReleaseAction::CastSpell;
			} else {
				// Threshold not met or cancelled - fall through to normal equip
				if (Config::WheelBehavior::InstantSpellDebugLog || Config::WheelBehavior::InstantTransformationsDebugLog) {
					logger::info("Hold-to-Use InstantSpell: NOT casting (threshold not met, cancelled, suppressed, or concentration disallowed), fallback to equip");
				}
			}
		}
	}

	logger::info("ReleaseResolve[Hold]: source={} action={} entry={} formId={:08X}",
		sourceLabel, GetReleaseActionName(resolvedAction), currentHoveredIdx, formId);

	const TargetHand resolvedHand = TargetHand::Left;
	logger::info("ReleaseResolve[Hold]: source={} hand={} entry={} formId={:08X}",
		sourceLabel, GetTargetHandName(resolvedHand), currentHoveredIdx, formId);

	if (resolvedAction == ReleaseAction::CastShout && shoutItem && Config::WheelBehavior::InstantShout) {
		if (shoutReady) {
			// Adjust effective time so the shout logic sees time starting from 0 after threshold
			// This matches the indicator visual change we will make
			activated = shoutItem->CastImmediate(holdDuration - kInstantShoutThreshold);
			if (activated) {
				logger::info("Hold-to-Use: InstantShout cast with holdDuration={:.2f}s (eff={:.2f}s)",
					holdDuration, holdDuration - kInstantShoutThreshold);
			} else {
				logger::info("ReleaseResolve[Hold]: shout cast failed, fallback to equip entry={} formId={:08X}",
					currentHoveredIdx, formId);
			}
		}
		if (!activated) {
			resolvedAction = ReleaseAction::Equip;
		}
	}

	if (!activated && resolvedAction == ReleaseAction::CastSpell && spellItem && spellReady) {
		RE::SpellItem* spell = spellItem->GetSpell();
		if (spell && IsPowerSpellType(spell)) {
			activated = QueuePowerActivation(spell->GetFormID());
			if (activated) {
				logger::info("Hold-to-Use: queued vanilla power activation entry={} hand={} formId={:08X}",
					currentHoveredIdx, GetTargetHandName(resolvedHand), spell->GetFormID());
				_rtuConsumedByInstant = true;
			} else {
				logger::info("ReleaseResolve[Hold]: power queue failed, fallback to equip entry={} formId={:08X}",
					currentHoveredIdx, formId);
				resolvedAction = ReleaseAction::Equip;
			}
		} else {
			const auto castingSource = GetCastingSourceForHand(resolvedHand);
			activated = spellItem->CastImmediate(true, castingSource);
			if (activated) {
				logger::info("Hold-to-Use: InstantSpell cast on release (Timed) entry={} inTransform={} hand={}",
					currentHoveredIdx, isInTransform, GetTargetHandName(resolvedHand));
				_rtuConsumedByInstant = true;
			} else {
				logger::info("ReleaseResolve[Hold]: cast failed, fallback to equip entry={} formId={:08X}",
					currentHoveredIdx, formId);
				resolvedAction = ReleaseAction::Equip;
			}
		}
	}

	// Fallback to normal activation (equip)
	if (!activated) {
		const bool useLeft = true;

		bool equipBlocked = false;
		const char* equipReason = "ok";
		if (spellItem) {
			RE::SpellItem* spell = spellItem->GetSpell();
			if (spell && !IsPowerSpellType(spell)) {
				equipBlocked = IsSpellEquippedInHand(RE::PlayerCharacter::GetSingleton(), formId, resolvedHand);
				if (equipBlocked) {
					equipReason = "blocked_because_target_hand_already_had_it";
				}
			}
		}
		logger::info("ReleaseResolve[Hold]: source={} equip={} reason={} entry={} hand={} formId={:08X}",
			sourceLabel,
			equipBlocked ? "blocked" : "allowed",
			equipReason,
			currentHoveredIdx,
			GetTargetHandName(resolvedHand),
			formId);

		activeWheel->ActivateHoveredEntrySecondary(false);
		activated = true;

		// Diagnostic log (gated, once per activation)
		logger::info("Activation: source=HoldSecondary rtu={}, entry={}, hand={}, action=equip, formId={:08X}",
			Config::WheelBehavior::ReleaseToUse ? "ON" : "OFF",
			currentHoveredIdx,
			useLeft ? "LEFT" : "RIGHT",
			formId);
	}

	if (activated) {
		if (ShouldNotifyHandMemory(hoveredItem, resolvedAction)) {
			HandMemory::NotifyWheelEquipOrCast();
		}
		if (!_directActivatedThisOpenSession) {
			_directActivatedThisOpenSession = true;
			logger::info("DirectActivate: set directActivatedThisOpenSession=true source={} entry={} hand={}",
				sourceLabel, currentHoveredIdx, GetTargetHandName(resolvedHand));
		}
		_activateOnCloseFired = true;  // Prevent RTU double-activation
		// Suppress generic activate sound for shouts when stage sounds are active
		const bool shoutStageSoundsActive = Config::Sounds::EnableShoutStageSounds &&
			Config::Sounds::ShoutStageSoundMode != static_cast<std::uint32_t>(Config::ShoutStageSoundMode::Off);
		const bool isShout = std::dynamic_pointer_cast<WheelItemShout>(hoveredItem) != nullptr;
		if (!(isShout && shoutStageSoundsActive)) {
			PlaySoundByEditorID(Config::Sounds::ActivateSoundEditorID.c_str(), Config::Sounds::ActivateSoundVolume);
		}
		if (Config::WheelBehavior::CloseWheelAfterUse) {
			TryCloseWheeler();
		}
	}
}

float Wheeler::GetActivationTiming()
{
	// If confirm is held, return hold duration (for Hold-to-Use indicator)
	if (_confirmHeld) {
		return _confirmHoldSeconds;
	}
	if (_secondaryConfirmHeld) {
		return _secondaryConfirmHoldSeconds;
	}
	// Otherwise return hover time (for RTU indicator)
	return _hoveredEntryTime;
}

bool Wheeler::IsConfirmHeld()
{
	return _confirmHeld || _secondaryConfirmHeld;
}

void Wheeler::LatchHandOverrideLeft()
{
	if (_state == WheelState::KClosed) {
		return;
	}
	std::shared_lock<std::shared_mutex> lock(_wheelDataLock);
	if (_activeWheelIdx < 0 || _activeWheelIdx >= static_cast<int>(_wheels.size())) {
		return;
	}
	int hoveredIdx = _wheels[_activeWheelIdx]->GetHoveredEntryIndex();
	if (hoveredIdx < 0) {
		return;
	}
	_handOverrideActive = true;
	_handOverrideEntryIdx = hoveredIdx;
	_handOverrideLeft = true;
	logger::info("RTU: hand override latched LEFT via RMB (entry={})", hoveredIdx);
}

int Wheeler::GetCurrentHoveredEntryIndex()
{
	if (_state == WheelState::KClosed) {
		return -1;
	}
	std::shared_lock<std::shared_mutex> lock(_wheelDataLock);
	if (_activeWheelIdx < 0 || _activeWheelIdx >= static_cast<int>(_wheels.size())) {
		return -1;
	}
	return _wheels[_activeWheelIdx]->GetHoveredEntryIndex();
}

void Wheeler::CancelInstantSpell()
{
	if (_state == WheelState::KClosed) {
		return;
	}
	// Only cancel if there's an active attempt
	if (!_instantAttemptActive && _instantEntryIdx < 0) {
		return;
	}
	// Store which entry is being suppressed (scoped suppression)
	const int suppressedEntry = _instantEntryIdx;
	
	// Cancel state changes
	_instantCancelled = true;
	_instantSuppressForEntry = true;
	_instantAttemptActive = false;
	_instantReady = false;
	
	// Single gated log
	if (IsControllerDebugEnabled()) {
		logger::info("[Controller] CancelInstantSpell entry={}", suppressedEntry);
	}
}

Wheeler::TargetHand Wheeler::ResolveTargetHand(int entryIdx)
{
	// Hand override takes precedence - decoupled from RTU
	if (_handOverrideActive && entryIdx == _handOverrideEntryIdx && _handOverrideLeft) {
		return TargetHand::Left;
	}
	// TODO: Check entry-specific left-hand forced flag if implemented
	return TargetHand::Right;
}

Wheeler::TargetHand Wheeler::ResolveTargetHandRTU(int entryIdx, const std::shared_ptr<WheelItem>& hoveredItem, ReleaseAction resolvedAction)
{
	auto fallback = [&]() {
		return ResolveTargetHand(entryIdx);
	};

	const RE::FormID formId = hoveredItem ? hoveredItem->GetFormID() : 0;
	auto logDecision = [&](TargetHand chosen, const char* reason, std::uint32_t allowedMask, int leftEmpty, int rightEmpty, SmartAssignCategory category) {
		if (!Config::WheelBehavior::RTUSmartAssignDebugLog) {
			return;
		}
		logger::info("RTU SmartAssign: entry={} formId={:08X} category={} allowedMask={} leftEmpty={} rightEmpty={} chosen={} reason={}",
			entryIdx,
			formId,
			GetSmartAssignCategoryName(category),
			allowedMask,
			leftEmpty,
			rightEmpty,
			GetTargetHandName(chosen),
			reason ? reason : "unknown");
	};

	// Step 0: Manual override wins
	if (_handOverrideActive && entryIdx == _handOverrideEntryIdx && _handOverrideLeft) {
		logDecision(TargetHand::Left, "manual_override_left", 0, -1, -1, SmartAssignCategory::None);
		return TargetHand::Left;
	}

	// Step 1: Gate
	if (!Config::WheelBehavior::ReleaseToUse) {
		TargetHand hand = fallback();
		logDecision(hand, "rtu_disabled", 0, -1, -1, SmartAssignCategory::None);
		return hand;
	}
	if (!Config::WheelBehavior::RTUSmartAssignEnabled) {
		TargetHand hand = fallback();
		logDecision(hand, "smartassign_disabled", 0, -1, -1, SmartAssignCategory::None);
		return hand;
	}
	if (resolvedAction == ReleaseAction::CastShout) {
		TargetHand hand = fallback();
		logDecision(hand, "cast_shout", 0, -1, -1, SmartAssignCategory::None);
		return hand;
	}
	if (!hoveredItem) {
		TargetHand hand = fallback();
		logDecision(hand, "no_hovered_item", 0, -1, -1, SmartAssignCategory::None);
		return hand;
	}

	// Step 2: Determine category
	SmartAssignCategory category = SmartAssignCategory::None;
	const char* bypassReason = nullptr;

	if (std::dynamic_pointer_cast<WheelItemSpell>(hoveredItem)) {
		category = SmartAssignCategory::Spells;
	} else if (RE::TESForm* form = formId != 0 ? RE::TESForm::LookupByID(formId) : nullptr) {
		if (auto* weap = form->As<RE::TESObjectWEAP>()) {
			const auto weaponType = weap->GetWeaponType();
			if (weaponType == RE::WEAPON_TYPE::kStaff) {
				category = SmartAssignCategory::Staffs;
			} else {
				const bool isTwoHanded =
					weap->IsBow() ||
					weap->IsCrossbow() ||
					weaponType == RE::WEAPON_TYPE::kTwoHandSword ||
					weaponType == RE::WEAPON_TYPE::kTwoHandAxe;
				if (isTwoHanded) {
					bypassReason = "two_handed_weapon";
				} else {
					category = SmartAssignCategory::Weapons1H;
				}
			}
		} else if (auto* armor = form->As<RE::TESObjectARMO>()) {
			if (armor->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kShield)) {
				category = SmartAssignCategory::Shields;
			} else {
				bypassReason = "armor_not_shield";
			}
		} else if (form->As<RE::TESObjectLIGH>()) {
			category = SmartAssignCategory::Torches;
		} else {
			bypassReason = "unknown_category";
		}
	} else {
		bypassReason = "form_lookup_failed";
	}

	if (bypassReason || category == SmartAssignCategory::None) {
		TargetHand hand = fallback();
		logDecision(hand, bypassReason ? bypassReason : "unknown_category", 0, -1, -1, category);
		return hand;
	}

	// Step 3: Category mask filter
	const std::uint32_t categoryBit = static_cast<std::uint32_t>(category);
	if ((categoryBit & Config::WheelBehavior::RTUSmartAssignCategoryMask) == 0) {
		TargetHand hand = fallback();
		logDecision(hand, "category_mask_block", 0, -1, -1, category);
		return hand;
	}

	// Step 4: Allowed target hands
	std::uint32_t allowed = Config::WheelBehavior::RTUSmartAssignTargetHands & 0x3u;
	if (allowed == 0) {
		TargetHand hand = fallback();
		logDecision(hand, "target_hands_none", 0, -1, -1, category);
		return hand;
	}

	// Step 5: Per-item hand restrictions
	if (category == SmartAssignCategory::Shields || category == SmartAssignCategory::Torches) {
		allowed &= 0x1u;  // left only
		if (allowed == 0) {
			TargetHand hand = fallback();
			logDecision(hand, "left_only_restriction_block", 0, -1, -1, category);
			return hand;
		}
	}

	// Step 6: Inspect current hands
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		TargetHand hand = fallback();
		logDecision(hand, "no_player", allowed, -1, -1, category);
		return hand;
	}
	RE::TESForm* leftObj = pc->GetEquippedObject(true);
	RE::TESForm* rightObj = pc->GetEquippedObject(false);
	const bool leftEmpty = (leftObj == nullptr);
	const bool rightEmpty = (rightObj == nullptr);

	// Step 7: Decision tree
	const bool allowLeft = (allowed & 0x1u) != 0;
	const bool allowRight = (allowed & 0x2u) != 0;
	const char* reason = "unknown";
	TargetHand chosen = TargetHand::Right;

	auto resolveOverwrite = [&](const char*& outReason) {
		switch (Config::WheelBehavior::RTUSmartAssignOverwriteMode) {
		case 0:
			outReason = "overwrite_never_fallback";
			return fallback();
		case 1:
			if (allowLeft) {
				outReason = "overwrite_prefer_left";
				return TargetHand::Left;
			}
			break;
		case 2:
			if (allowRight) {
				outReason = "overwrite_prefer_right";
				return TargetHand::Right;
			}
			break;
		case 3:
			outReason = "overwrite_prefer_vanilla";
			return fallback();
		default:
			break;
		}
		outReason = "overwrite_fallback";
		return fallback();
	};

	if (allowLeft && allowRight) {
		if (leftEmpty && rightEmpty) {
			if (Config::WheelBehavior::RTUSmartAssignBothEmptyPriority == 1) {
				chosen = TargetHand::Left;
				reason = "both_empty_left_first";
			} else {
				chosen = TargetHand::Right;
				reason = "both_empty_right_first";
			}
		} else if (leftEmpty) {
			chosen = TargetHand::Left;
			reason = "left_empty";
		} else if (rightEmpty) {
			chosen = TargetHand::Right;
			reason = "right_empty";
		} else {
			chosen = resolveOverwrite(reason);
		}
	} else if (allowLeft) {
		if (leftEmpty) {
			chosen = TargetHand::Left;
			reason = "left_only_empty";
		} else {
			chosen = resolveOverwrite(reason);
		}
	} else if (allowRight) {
		if (rightEmpty) {
			chosen = TargetHand::Right;
			reason = "right_only_empty";
		} else {
			chosen = resolveOverwrite(reason);
		}
	} else {
		chosen = fallback();
		reason = "allowed_none";
	}

	logDecision(chosen, reason, allowed, leftEmpty ? 1 : 0, rightEmpty ? 1 : 0, category);
	return chosen;
}

bool Wheeler::GetInstantSpellState(int& outEntryIdx, float& outElapsed, float& outThreshold, bool& outReady, bool& outCancelled)
{
	if (_state == WheelState::KClosed) {
		return false;
	}
	// Check if there's an active attempt
	if (!_instantAttemptActive || _instantEntryIdx < 0) {
		return false;
	}

	RE::SpellItem* spell = nullptr;
	{
		std::shared_lock<std::shared_mutex> lock(_wheelDataLock);
		if (_activeWheelIdx < 0 || _activeWheelIdx >= static_cast<int>(_wheels.size()) || !_wheels[_activeWheelIdx]) {
			return false;
		}
		WheelEntry* entry = _wheels[_activeWheelIdx]->GetEntry(_instantEntryIdx);
		if (!entry) {
			return false;
		}
		if (std::shared_ptr<WheelItemSpell> spellItem = std::dynamic_pointer_cast<WheelItemSpell>(entry->GetSelectedItem())) {
			spell = spellItem->GetSpell();
		}
	}

	if (!spell) {
		return false;
	}

	const bool isInTransform = !TransformWheelManager::IsPlayerHuman();
	if (!IsInstantEnabledForSpell(spell, isInTransform)) {
		return false;
	}

	outEntryIdx = _instantEntryIdx;
	outElapsed = _instantElapsedSec;
	
	// Calculate threshold - must match the Update loop logic
	const float configThreshold = Config::WheelBehavior::InstantSpellHoldThresholdMs / 1000.0f;
	const float safetyThreshold = Config::WheelBehavior::HoldToCastSafetyThresholdMs / 1000.0f;
	const float fullThreshold = (std::max)(safetyThreshold, configThreshold);
	
	// If in Hold mode (not RTU), threshold was reduced by safety offset
	// This syncs the Arc progress with the Ready state
	if (!_instantRtuSource) {
		const float kSafetyThreshold = Config::WheelBehavior::HoldToCastSafetyThresholdMs / 1000.0f;
		outThreshold = fullThreshold - kSafetyThreshold;
		if (outThreshold <= 0.0f) outThreshold = 0.1f; // Failsafe
	} else {
		outThreshold = fullThreshold;
	}
	
	outReady = _instantReady;
	outCancelled = _instantCancelled;
	return true;
}

void Wheeler::AddEmptyEntryToCurrentWheel()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	if (!_editMode || _state == WheelState::KClosed || _wheels.empty() || _activeWheelIdx == -1) {
		return;
	}
	_wheels[_activeWheelIdx]->PushEmptyEntry();
}


void Wheeler::AddWheel()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	if (!_editMode || _state == WheelState::KClosed) {
		return;
	}
	_wheels.push_back(std::make_unique<Wheel>());
}

void Wheeler::PushWheel()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	_wheels.push_back(std::make_unique<Wheel>());
}

void Wheeler::DeleteCurrentWheel()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	if (!_editMode || _state == WheelState::KClosed) {
		return;
	}
	if (_wheels.size() > 1) {
		std::unique_ptr<Wheel>& toDelete = _wheels[_activeWheelIdx];
		if (!toDelete->IsEmpty()) { // do not delete an non-empty wheel
			return;
		}
		_wheels.erase(_wheels.begin() + _activeWheelIdx);
		if (_activeWheelIdx == _wheels.size() && _activeWheelIdx != 0) {  //deleted the last wheel
			_activeWheelIdx = _wheels.size() - 1;
		}
		_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);  // reset active entry for new wheel
	}
}

void Wheeler::MoveEntryForwardInCurrentWheel()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	if (!_editMode || _state == WheelState::KClosed) {
		return;
	}
	if (_activeWheelIdx != -1) {
		_wheels[_activeWheelIdx]->MoveHoveredEntryForward();
	}
}

void Wheeler::MoveEntryBackInCurrentWheel()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	if (!_editMode || _state == WheelState::KClosed) {
		return;
	}
	if (_activeWheelIdx != -1) {
		_wheels[_activeWheelIdx]->MoveHoveredEntryBack();
	}
}

void Wheeler::MoveWheelForward()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	if (!_editMode || _state == WheelState::KClosed) {
		return;
	}
	if (_wheels.size() > 1) {
		int targetIdx;
		if (_activeWheelIdx == _wheels.size() - 1) {
			// Move the current wheel to the very front
			targetIdx = 0;
			auto currentWheel = std::move(_wheels.back());
			_wheels.pop_back();
			_wheels.insert(_wheels.begin(), std::move(currentWheel));
		} else {
			targetIdx = _activeWheelIdx + 1;
			std::swap(_wheels[_activeWheelIdx], _wheels[targetIdx]);
		}
		_activeWheelIdx = targetIdx;
	}
}

void Wheeler::MoveWheelBack()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	if (!_editMode || _state == WheelState::KClosed) {
		return;
	}
	if (_wheels.size() > 1) {
		int targetIdx;
		if (_activeWheelIdx == 0) {
			// Move the current wheel to the very end
			targetIdx = _wheels.size() - 1;
			auto currentWheel = std::move(_wheels.front());
			_wheels.erase(_wheels.begin());
			_wheels.push_back(std::move(currentWheel));
			_activeWheelIdx = _wheels.size() - 1;
		} else {
			targetIdx = _activeWheelIdx - 1;
			std::swap(_wheels[_activeWheelIdx], _wheels[targetIdx]);
		}
		_activeWheelIdx = targetIdx;
	}
}

int Wheeler::GetActiveWheelIndex()
{
	return _activeWheelIdx;
}

Wheel* Wheeler::GetWheelByIndex(int a_index)
{
	if (a_index < 0 || a_index >= static_cast<int>(_wheels.size())) {
		return nullptr;
	}
	return _wheels[a_index].get();
}

void Wheeler::SetActiveWheelIndex(int a_index)
{
	if (a_index < 0) {
		_activeWheelIdx = -1;
		return;
	}
	if (_wheels.empty()) {
		_activeWheelIdx = -1;
		return;
	}
	_activeWheelIdx = std::clamp(a_index, 0, static_cast<int>(_wheels.size()) - 1);
}

bool Wheeler::IsWheelerOpen() { return _state != WheelState::KClosed; }

bool Wheeler::IsInEditMode() { return _editMode; }

void Wheeler::SerializeFromJsonObj(const nlohmann::json& j_wheeler, SKSE::SerializationInterface* a_intfc)
{
	if (!j_wheeler.contains("wheels") || !j_wheeler["wheels"].is_array()) {
		logger::warn("Deserialize: missing or invalid 'wheels' array; leaving wheels empty");
		_wheels.clear();
		_activeWheelIdx = -1;
		return;
	}

	const nlohmann::json& j_wheels = j_wheeler["wheels"];
	
	// Bounds check: reject absurdly large wheel counts
	constexpr std::size_t MAX_WHEELS = 100;
	if (j_wheels.size() > MAX_WHEELS) {
		logger::warn("Deserialize: wheel count {} exceeds max {}, likely corrupted data; clearing state", j_wheels.size(), MAX_WHEELS);
		_wheels.clear();
		_activeWheelIdx = -1;
		return;
	}
	
	for (const auto& j_wheel : j_wheels) {
		try {
			std::unique_ptr<Wheel> wheel = Wheel::SerializeFromJsonObj(j_wheel, a_intfc);
			if (wheel) {
				_wheels.push_back(std::move(wheel));
			}
		} catch (const std::exception& e) {
			logger::warn("Deserialize: failed to load wheel: {}", e.what());
		}
	}

	const int activeIdxFromSave = j_wheeler.value("activewheel", 0);
	SetActiveWheelIndex(activeIdxFromSave);
}

void Wheeler::SerializeIntoJsonObj(nlohmann::json& j_wheeler)
{
	j_wheeler["wheels"] = nlohmann::json::array();
	auto mapRuntimeToUserIndex = [&](int runtimeIdx) -> std::optional<int> {
		if (runtimeIdx < 0) {
			return std::nullopt;
		}
		int userIdx = 0;
		for (size_t i = 0; i < _wheels.size(); ++i) {
			const int idx = static_cast<int>(i);
			if (WheelerAPI::IsManagedWheelIndex(idx) || TransformWheelManager::IsTransformWheelIndex(idx)) {
				continue;
			}
			if (idx == runtimeIdx) {
				return userIdx;
			}
			++userIdx;
		}
		return std::nullopt;
	};

	for (size_t i = 0; i < _wheels.size(); ++i) {
		// Skip managed wheels - they are owned by API clients and should not be saved
		const int idx = static_cast<int>(i);
		if (WheelerAPI::IsManagedWheelIndex(idx) || TransformWheelManager::IsTransformWheelIndex(idx)) {
			continue;
		}
		nlohmann::json j_wheel;
		_wheels[i]->SerializeIntoJsonObj(j_wheel);
		j_wheeler["wheels"].push_back(j_wheel);
	}

	int activeSaveIdx = 0;
	if (auto mapped = mapRuntimeToUserIndex(_activeWheelIdx); mapped.has_value()) {
		activeSaveIdx = *mapped;
	}
	if (_activeWheelIdx >= 0 &&
		(WheelerAPI::IsManagedWheelIndex(_activeWheelIdx) ||
			TransformWheelManager::IsTransformWheelIndex(_activeWheelIdx))) {
		if (auto saved = TransformWheelManager::GetSavedHumanWheelIndex(); saved.has_value()) {
			if (auto mapped = mapRuntimeToUserIndex(*saved); mapped.has_value()) {
				activeSaveIdx = *mapped;
			} else {
				activeSaveIdx = 0;
			}
		} else {
			activeSaveIdx = 0;
		}
	}

	if (Config::WheelBehavior::TransformWheels::DebugLog) {
		logger::info(
			"TransformWheels: serialize wheels={} activeRuntime={} activeSaved={} activeManaged={}",
			j_wheeler["wheels"].size(),
			_activeWheelIdx,
			activeSaveIdx,
			_activeWheelIdx >= 0 &&
				(WheelerAPI::IsManagedWheelIndex(_activeWheelIdx) ||
					TransformWheelManager::IsTransformWheelIndex(_activeWheelIdx)));
	}

	j_wheeler["activewheel"] = activeSaveIdx;
}

void Wheeler::SetupDefaultWheels()
{
	const int defaultWheelNum = 2;
	const int defaultEntryNum = 4;
	Wheeler::Clear();
	int wheelIdx = 0;
	while (wheelIdx < defaultWheelNum) {
		Wheeler::PushWheel();
		int entryIdx = 0;
		while (entryIdx < defaultEntryNum) {
			_wheels[wheelIdx]->PushEmptyEntry();
			entryIdx++;
		}
		wheelIdx++;
	}
	Wheeler::SetActiveWheelIndex(0);

}

inline ImVec2 Wheeler::getWheelCenter()
{
	using namespace Config::Styling::Wheel;
	ImVec2 renderSize = ResolutionScale::Context::GetSingleton().GetRenderSize();
	return ImVec2(renderSize.x / 2 + CenterOffsetX, renderSize.y / 2 + CenterOffsetY);
}

bool Wheeler::shouldBeInEditMode(RE::UI* a_ui)
{
	return a_ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME) 
		|| a_ui->IsMenuOpen(RE::MagicMenu::MENU_NAME)
		|| a_ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME);
}

void Wheeler::hideEditModeVanillaMenus(RE::UI* a_ui)
{
	if (!Config::Control::Wheel::HideGameUIInEditMode) {
		return; // don't hide
	}
	if (a_ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) {
		RE::GFxMovieView* uiMovie = a_ui->GetMenu<RE::InventoryMenu>()->uiMovie.get();
		if (uiMovie) {
			uiMovie->SetVisible(false);
		}
	}
	if (a_ui->IsMenuOpen(RE::MagicMenu::MENU_NAME)) {
		RE::GFxMovieView* uiMovie = a_ui->GetMenu<RE::MagicMenu>()->uiMovie.get();
		if (uiMovie) {
			uiMovie->SetVisible(false);
		}
	}
	if (a_ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME)) {
		RE::GFxMovieView* uiMovie = a_ui->GetMenu<RE::FavoritesMenu>()->uiMovie.get();
		if (uiMovie) {
			uiMovie->SetVisible(false);
		}
	}
}

void Wheeler::showEditModeVanillaMenus(RE::UI* a_ui)
{
	if (a_ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) {
		RE::GFxMovieView* uiMovie = a_ui->GetMenu<RE::InventoryMenu>()->uiMovie.get();
		if (uiMovie) {
			uiMovie->SetVisible(true);
		}
	}
	if (a_ui->IsMenuOpen(RE::MagicMenu::MENU_NAME)) {
		RE::GFxMovieView* uiMovie = a_ui->GetMenu<RE::MagicMenu>()->uiMovie.get();
		if (uiMovie) {
			uiMovie->SetVisible(true);
		}
	}
	if (a_ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME)) {
		RE::GFxMovieView* uiMovie = a_ui->GetMenu<RE::FavoritesMenu>()->uiMovie.get();
		if (uiMovie) {
			uiMovie->SetVisible(true);
		}
	}
}

void Wheeler::enterEditMode()
{
	if (_editMode) {
		return;
	}
	RE::UI* ui = RE::UI::GetSingleton();
	if (!ui) {
		_editMode = true;
		return;
	}
	
	// Selection was captured earlier in ToggleWheeler/ToggleWheelIfInInventory
	bool favOpen = ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME);
	if (!favOpen) {
		Utils::Inventory::FavoritesSelectionCache::Invalidate(); // Clear stale cache
	}
	
	_editMode = true;
}

void Wheeler::exitEditMode()
{
	if (!_editMode) {
		return;
	}
	_editMode = false;
}

float Wheeler::getCursorRadiusMax()
{
	if (_activeWheelIdx < 0 || _wheels.empty() || _activeWheelIdx >= _wheels.size() || !_wheels[_activeWheelIdx]) {
		return 0.0f;
	}
	return Config::Control::Wheel::CursorRadiusPerEntry * _wheels[_activeWheelIdx]->GetNumEntries();
}


// bool Wheeler::OffsetCamera(RE::TESCamera* a_this)
// {
// 	using namespace Config::Animation;
// 	if (!CameraRotation || _state == WheelState::KClosed || (_cursorPos.x == 0 && _cursorPos.y == 0)) {
// 		return false;
// 	}
// 	float cursorRadius = getCursorRadiusMax();
// 	// Calculate yaw rotation matrix
// 	RE::NiMatrix3 yawRotation = Utils::Math::MatrixFromAxisAngle(-_cursorPos.x / cursorRadius * 0.05, Utils::Math::HORIZONTAL_AXIS);
// 	// Set roll component to zero
// 	yawRotation.entry[3][3] = 1.0f;

// 	// Calculate pitch rotation matrix
// 	RE::NiMatrix3 pitchRotation = Utils::Math::MatrixFromAxisAngle(-_cursorPos.y / cursorRadius * 0.05, Utils::Math::VERTICAL_AXIS);
// 	// Set roll component to zero
// 	pitchRotation.entry[3][3] = 1.0f;

// 	// Apply rotations to the camera root's local rotation matrix
// 	a_this->cameraRoot->local.rotate = a_this->cameraRoot->local.rotate * yawRotation;
// 	a_this->cameraRoot->local.rotate = a_this->cameraRoot->local.rotate * pitchRotation;

// 	return true;

// 	return true;
// }

void Wheeler::PruneWheelEntries_NotInInventory(PruneReason reason)
{
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}
	
	const char* reasonStr = "Unknown";
	switch (reason) {
		case PruneReason::OnOpen: reasonStr = "OnOpen"; break;
		case PruneReason::OnClose: reasonStr = "OnClose"; break;
		case PruneReason::OnGuard: reasonStr = "OnGuard"; break;
	}
	
	if (MainWheelDebug::IsEnabled()) {
		MainWheelDebug::Log(MainWheelDebug::Category::Input, "InventoryPrune", 
			"start reason={} wheels={}", reasonStr, _wheels.size());
	}

	const bool logMissing = MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Input);
	auto inventoryMap = pc->GetInventory();
	auto getInventoryInfoByFormID = [&](RE::FormID formID, std::uint16_t* outUniqueID) -> int {
		if (outUniqueID) {
			*outUniqueID = 0;
		}
		if (formID == 0) {
			return 0;
		}
		for (auto& [boundObj, data] : inventoryMap) {
			if (boundObj && boundObj->formID == formID) {
				if (outUniqueID && data.second && data.second->extraLists) {
					for (auto* extraList : *data.second->extraLists) {
						if (!extraList || !extraList->HasType(RE::ExtraDataType::kUniqueID)) {
							continue;
						}
						auto* uniqueIDData = extraList->GetByType<RE::ExtraUniqueID>();
						if (uniqueIDData) {
							*outUniqueID = uniqueIDData->uniqueID;
							break;
						}
					}
				}
				return data.first;
			}
		}
		return 0;
	};
	auto getInventoryCountByFormID = [&](RE::FormID formID) -> int {
		return getInventoryInfoByFormID(formID, nullptr);
	};
	auto resolveMissingCategory = [&](const std::shared_ptr<WheelItem>& item) -> MissingCategory {
		MissingCategory category = item->GetMissingCategory();
		if (category != MissingCategory::Unknown) {
			return category;
		}
		const RE::FormID formID = item->GetFormID();
		RE::TESForm* form = formID != 0 ? RE::TESForm::LookupByID(formID) : nullptr;
		if (form) {
			category = DetermineMissingCategory(form);
		}
		if (category == MissingCategory::Unknown) {
			const char* typeName = item->GetItemTypeName();
			category = InferMissingCategoryFromItemType(typeName ? typeName : "");
		}
		item->SetMissingCategory(category);
		return category;
	};
	auto getUniqueID = [&](const std::shared_ptr<WheelItem>& item) -> std::uint16_t {
		if (auto mutableItem = dynamic_cast<WheelItemMutable*>(item.get())) {
			return mutableItem->GetUniqueID();
		}
		if (auto missingItem = dynamic_cast<WheelItemMissing*>(item.get())) {
			return missingItem->GetUniqueID();
		}
		return 0;
	};
	
	// Track statistics for logging
	int totalItemsRemoved = 0;
	int slotsCleared = 0;
	
	// Iterate all wheels and slots to find and remove missing items
	for (int w = 0; w < static_cast<int>(_wheels.size()); ++w) {
		Wheel* wheel = _wheels[w].get();
		if (!wheel) continue;
		
		const int numEntries = wheel->GetNumEntries();
		for (int s = 0; s < numEntries; ++s) {
			WheelEntry* entry = wheel->GetEntry(s);
			if (!entry) continue;
			
			const int numItems = entry->GetNumItems();
			if (numItems == 0) continue;
			
			// Iterate through ALL items in this slot (not just selected item)
			// Use reverse iteration to safely remove items without index shifting issues
			int itemsRemovedFromSlot = 0;
			for (int i = numItems - 1; i >= 0; --i) {
				WheelItem* item = entry->GetItem(i);
				if (!item) continue;
				
				const MissingCategory category = resolveMissingCategory(std::shared_ptr<WheelItem>(item, [](WheelItem*) {}));
				const bool treatAsInventoryBacked = item->IsInventoryBacked() ||
					category != MissingCategory::Unknown ||
					dynamic_cast<WheelItemMissing*>(item) != nullptr;
				if (!treatAsInventoryBacked) {
					continue;
				}

				bool inInventory = false;
				bool usedInventoryFallback = false;
				std::uint16_t inventoryUniqueID = 0;
				
				// ALWAYS check inventory count by formID, regardless of current missing state
				// This ensures items are restored when player re-acquires them
				const int countByForm = getInventoryInfoByFormID(item->GetFormID(), &inventoryUniqueID);
				
				if (item->IsInventoryBacked()) {
					inInventory = item->IsInPlayerInventory();
					if (!inInventory && countByForm > 0) {
						inInventory = true;
						usedInventoryFallback = true;
					}
				} else {
					// For non-inventory-backed items (like WheelItemMissing), check by formID
					inInventory = countByForm > 0;
				}

				// Item is in inventory - keep it and potentially restore from missing state
				if (inInventory) {
					if (usedInventoryFallback && inventoryUniqueID != 0) {
						if (auto mutableItem = dynamic_cast<WheelItemMutable*>(item)) {
							mutableItem->SetUniqueID(inventoryUniqueID);
						}
					}
					
					// CRITICAL: Restore WheelItemMissing placeholders back to real items
					// This handles the case where player re-acquires a previously lost item
					if (auto missingItem = dynamic_cast<WheelItemMissing*>(item)) {
						std::uint16_t resolvedUniqueID = inventoryUniqueID != 0 ? inventoryUniqueID : missingItem->GetUniqueID();
						std::shared_ptr<WheelItem> resolved = WheelItemFactory::MakeWheelItemFromResolvedForm(
							missingItem->GetOriginalType(),
							missingItem->GetFormID(),
							resolvedUniqueID);
						if (resolved) {
							resolved->SetMissingCategory(category);
							entry->ReplaceItemAt(i, resolved);
							if (logMissing) {
								MainWheelDebug::Log(MainWheelDebug::Category::Input, "InventoryPrune",
									"restored wheel={} slot={} itemIdx={} formID={:08X} name='{}' type={} reason={}",
									w, s, i, missingItem->GetFormID(), 
									missingItem->GetItemName() ? missingItem->GetItemName() : "(null)",
									missingItem->GetOriginalType(), reasonStr);
							}
						}
					}
					continue;
				}

				// Item is missing - check if we should keep it
				if (IsKeepMissingCategoryEnabled(category)) {
					// Convert to WheelItemMissing placeholder if not already
					if (dynamic_cast<WheelItemMissing*>(item) == nullptr) {
						const RE::FormID formID = item->GetFormID();
						if (formID != 0 && !RE::TESForm::LookupByID(formID)) {
							std::string typeName = item->GetItemTypeName() ? item->GetItemTypeName() : "";
							std::string name = item->GetItemName() ? item->GetItemName() : "";
							std::uint16_t uniqueID = 0;
							if (auto mutableItem = dynamic_cast<WheelItemMutable*>(item)) {
								uniqueID = mutableItem->GetUniqueID();
							}
							auto missingItem = std::make_shared<WheelItemMissing>(typeName, formID, uniqueID, category, name);
							missingItem->SetMissingCategory(category);
							// Replace this specific item in the stack
							if (i == entry->GetSelectedItemIndex()) {
								entry->ReplaceSelectedItem(missingItem);
							}
						}
					}
					if (logMissing) {
						MainWheelDebug::Log(MainWheelDebug::Category::Input, "InventoryPrune",
							"kept missing wheel={} slot={} itemIdx={} formID={:08X} name='{}' type={} category={} reason={}",
							w, s, i, item->GetFormID(), item->GetItemName() ? item->GetItemName() : "(null)",
							item->GetItemTypeName(), MissingCategoryToString(category), reasonStr);
					}
					continue;
				}

				// Item is missing and should be removed - remove ONLY this item from the stack
				if (logMissing) {
					MainWheelDebug::Log(MainWheelDebug::Category::Input, "InventoryPrune",
						"removing wheel={} slot={} itemIdx={} formID={:08X} name='{}' type={} reason={}",
						w, s, i, item->GetFormID(), item->GetItemName() ? item->GetItemName() : "(null)",
						item->GetItemTypeName(), reasonStr);
				}
				entry->RemoveItemAt(i);
				itemsRemovedFromSlot++;
				totalItemsRemoved++;
			}
			
			// After pruning, update entry-level missing state and handle WheelItemMissing resolution
			if (entry->GetNumItems() == 0) {
				// Slot is now empty after removing all items
				entry->SetMissingState(false, MissingCategory::Unknown);
				slotsCleared++;
			} else {
				// Slot still has items - check if selected item needs resolution
				std::shared_ptr<WheelItem> selectedItem = entry->GetSelectedItem();
				if (selectedItem) {
					const MissingCategory category = resolveMissingCategory(selectedItem);
					bool selectedInInventory = false;
					std::uint16_t inventoryUniqueID = 0;
					if (selectedItem->IsInventoryBacked()) {
						selectedInInventory = selectedItem->IsInPlayerInventory();
						if (!selectedInInventory && Config::WheelBehavior::KeepMissing::Enabled) {
							const int countByForm = getInventoryInfoByFormID(selectedItem->GetFormID(), &inventoryUniqueID);
							if (countByForm > 0) {
								selectedInInventory = true;
							}
						}
					}
					
					// Resolve WheelItemMissing back to real item if it's now in inventory
					if (auto missingItem = dynamic_cast<WheelItemMissing*>(selectedItem.get())) {
						if (selectedInInventory) {
							std::uint16_t resolvedUniqueID = inventoryUniqueID != 0 ? inventoryUniqueID : missingItem->GetUniqueID();
							std::shared_ptr<WheelItem> resolved = WheelItemFactory::MakeWheelItemFromResolvedForm(
								missingItem->GetOriginalType(),
								missingItem->GetFormID(),
								resolvedUniqueID);
							if (resolved) {
								resolved->SetMissingCategory(category);
								entry->ReplaceSelectedItem(resolved);
								if (logMissing) {
									MainWheelDebug::Log(MainWheelDebug::Category::Input, "InventoryPrune",
										"restored wheel={} slot={} formID={:08X} name='{}' type={} reason={}",
										w, s, selectedItem->GetFormID(), selectedItem->GetItemName() ? selectedItem->GetItemName() : "(null)",
										selectedItem->GetItemTypeName(), reasonStr);
								}
							}
						}
					}
					
					// Update entry-level missing state based on selected item
					const bool selectedIsMissing = !selectedInInventory && 
						(selectedItem->IsInventoryBacked() || category != MissingCategory::Unknown);
					entry->SetMissingState(selectedIsMissing, category);
				}
			}
		}
	}
	
	// Note: Serialization is handled by the existing SKSE callback mechanism
	// when the game saves, so no explicit dirty flag needed here.
	
	if (MainWheelDebug::IsEnabled()) {
		MainWheelDebug::Log(MainWheelDebug::Category::Input, "InventoryPrune", 
			"done removed={} items, cleared={} slots, reason={}", totalItemsRemoved, slotsCleared, reasonStr);
	}
}
