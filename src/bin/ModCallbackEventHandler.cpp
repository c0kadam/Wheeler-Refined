#include "ModCallbackEventHandler.h"
#include "Config.h"
#include "UserInput/Controls.h"
#include "Wheeler/Wheeler.h"
#include "Wheeler/AmmoWheelReskinUnified.h"
#include "Integrations/ActionHotkeysBridge.h"
#include "Integrations/OStimIntegration.h"
#include "Utilities/Utils.h"
#include "Texts.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace
{
	struct DmenuPanelReloadGate
	{
		bool initialized{ false };
		bool hasWriteTime{ false };
		std::filesystem::file_time_type lastWriteTime{};
		std::string lastResolvedPath{};
		std::chrono::steady_clock::time_point lastHandledAt{};
		std::chrono::steady_clock::time_point lastSkipLogAt{};
	};

	constexpr auto kDuplicateCallbackWindow = std::chrono::milliseconds(120);

	bool ShouldLogRebindDebug()
	{
		return Config::Debug::InputSpy || Config::Debug::LogMenuBlockReasons;
	}

	bool ShouldSkipDuplicatePanelReload(
		DmenuPanelReloadGate& gate,
		std::string_view panelName,
		const std::filesystem::path& preferredPath,
		const std::filesystem::path& fallbackPath = {})
	{
		std::error_code ec;
		std::filesystem::path resolvedPath;
		std::filesystem::file_time_type writeTime{};
		bool hasWriteTime = false;

		if (!preferredPath.empty() && std::filesystem::exists(preferredPath, ec) && !ec) {
			writeTime = std::filesystem::last_write_time(preferredPath, ec);
			if (!ec) {
				resolvedPath = preferredPath;
				hasWriteTime = true;
			}
		}
		if (!hasWriteTime && !fallbackPath.empty()) {
			ec.clear();
			if (std::filesystem::exists(fallbackPath, ec) && !ec) {
				writeTime = std::filesystem::last_write_time(fallbackPath, ec);
				if (!ec) {
					resolvedPath = fallbackPath;
					hasWriteTime = true;
				}
			}
		}

		const auto now = std::chrono::steady_clock::now();
		const bool fastRepeat = gate.initialized &&
			(now - gate.lastHandledAt) < kDuplicateCallbackWindow;
		const bool sameWriteTime = gate.initialized &&
			gate.hasWriteTime &&
			hasWriteTime &&
			gate.lastResolvedPath == resolvedPath.string() &&
			gate.lastWriteTime == writeTime;

		if (fastRepeat && sameWriteTime) {
			if (Config::Debug::LogMenuBlockReasons &&
			    (gate.lastSkipLogAt.time_since_epoch().count() == 0 ||
			        (now - gate.lastSkipLogAt) >= std::chrono::milliseconds(500))) {
				logger::info("[ModCallback] Skip duplicate dmenu_updateSettings panel='{}' path='{}'",
					panelName,
					resolvedPath.string());
				gate.lastSkipLogAt = now;
			}
			return true;
		}

		gate.initialized = true;
		gate.lastHandledAt = now;
		gate.hasWriteTime = hasWriteTime;
		gate.lastResolvedPath = resolvedPath.string();
		if (hasWriteTime) {
			gate.lastWriteTime = writeTime;
		}
		return false;
	}
}

EventResult ModCallbackEventHandler::ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>* a_eventSource)
{
	if (!a_event) {
		return EventResult::kContinue;
	}
	if (a_event->eventName == "dmenu_updateSettings") {
		static DmenuPanelReloadGate s_wheelBehaviorReloadGate;
		static DmenuPanelReloadGate s_ammoWheelReloadGate;
		static DmenuPanelReloadGate s_actionHotkeysBridgeReloadGate;
		static DmenuPanelReloadGate s_ostimIntegrationReloadGate;

		const std::string_view arg = a_event->strArg.data() ? std::string_view(a_event->strArg.data()) : std::string_view{};
		const bool isWheelBehaviorPanel = (arg == "Wheel Behavior") || (arg.find("Wheel Behavior") != std::string_view::npos);
		const bool isAmmoWheelPanel = (arg == "Ammo Wheel") || (arg.find("Ammo Wheel") != std::string_view::npos);
		const bool isActionHotkeysBridgePanel =
			(arg == "Action Hotkeys Bridge") ||
			(arg.find("Action Hotkeys Bridge") != std::string_view::npos);
		const bool isOStimIntegrationPanel =
			(arg == "OStim Integration") ||
			(arg.find("OStim Integration") != std::string_view::npos);
		const bool isWheelerPanel =
			(arg.rfind("Wheeler", 0) == 0) ||
			isWheelBehaviorPanel ||
			isAmmoWheelPanel ||
			isActionHotkeysBridgePanel ||
			isOStimIntegrationPanel;

		if (isWheelBehaviorPanel &&
		    ShouldSkipDuplicatePanelReload(
			    s_wheelBehaviorReloadGate,
			    arg,
			    "Data\\SKSE\\Plugins\\wheeler\\wheelBehavior.ini",
			    "Data\\SKSE\\Plugins\\wheeler\\wheelBehavior.factory.ini")) {
			return EventResult::kContinue;
		}
		if (isAmmoWheelPanel &&
		    ShouldSkipDuplicatePanelReload(
			    s_ammoWheelReloadGate,
			    arg,
			    "Data\\SKSE\\Plugins\\wheeler\\AmmoWheel.ini",
			    "Data\\SKSE\\Plugins\\wheeler\\AmmoWheel.defaults.ini")) {
			return EventResult::kContinue;
		}
		if (isActionHotkeysBridgePanel &&
		    ShouldSkipDuplicatePanelReload(
			    s_actionHotkeysBridgeReloadGate,
			    arg,
			    "Data\\SKSE\\Plugins\\wheeler\\ActionHotkeysBridge.ini",
			    "Data\\SKSE\\Plugins\\wheeler\\ActionHotkeysBridge.defaults.ini")) {
			return EventResult::kContinue;
		}
		if (isOStimIntegrationPanel &&
		    ShouldSkipDuplicatePanelReload(
			    s_ostimIntegrationReloadGate,
			    arg,
			    "Data\\SKSE\\Plugins\\wheeler\\OStimIntegration.ini",
			    "Data\\SKSE\\Plugins\\wheeler\\OStimIntegration.defaults.ini")) {
			return EventResult::kContinue;
		}

		logger::info("[ModCallback] dmenu_updateSettings received, panel='{}'", arg);
		logger::info(
			"[ModCallback] isWheelerPanel={}, isWheelBehaviorPanel={}, isAmmoWheelPanel={}, isActionHotkeysBridgePanel={}, isOStimIntegrationPanel={}",
			isWheelerPanel,
			isWheelBehaviorPanel,
			isAmmoWheelPanel,
			isActionHotkeysBridgePanel,
			isOStimIntegrationPanel);
		if (isWheelerPanel) {
			// ============ LIGHTWEIGHT VS HEAVY UPDATE PATH ============
			// Visual changes (scale, position, colors, opacity) can be applied without closing the wheel.
			// Only structural changes (keybinds, timescale, textures) require a full close/reload/open cycle.
			// This prevents flickering when dragging sliders continuously.
			
			const bool wasWheelerOpen = Wheeler::IsWheelerOpen();
			
			// Snapshot values that require a full reload cycle when changed
			const float oldSlowTimeScale = Config::Styling::Wheel::SlowTimeScale;
			const auto oldMkbToggle = Config::InputBindings::MKB::toggleWheel;
			const auto oldMkbModifier = Config::InputBindings::MKB::toggleWheelModifier;
			const auto oldGamepadToggle = Config::InputBindings::GamePad::toggleWheel;
			const auto oldGamepadModifier = Config::InputBindings::GamePad::toggleWheelModifier;
			const auto oldGamepadInvToggle = Config::InputBindings::GamePad::toggleWheelIfInInventory;
			const auto oldGamepadInvModifier = Config::InputBindings::GamePad::toggleWheelIfInInventoryModifier;
			const auto oldGamepadNoInvToggle = Config::InputBindings::GamePad::toggleWheelIfNotInInventory;
			const auto oldGamepadNoInvModifier = Config::InputBindings::GamePad::toggleWheelIfNotInInventoryModifier;
			const bool oldVanillaLTPassthrough = Config::VanillaLTPassthrough;
			const bool oldWheelerEnabled = Config::WheelerEnabled;
			
			// ============ LIGHTWEIGHT CONFIG RELOAD (NO CLOSE) ============
			// Read new config values - these are just inline variables that Draw() reads each frame
			Config::ReadStyleConfig();
			const bool logRebindDebugBeforeRead = ShouldLogRebindDebug();
			if (logRebindDebugBeforeRead) {
				logger::info(
					"[RebindDebug] dmenu_updateSettings panel='{}' before ReadControlConfig: enabled={} gpToggle={} gpMod={} gpInvToggle={} gpInvMod={} gpNoInvToggle={} gpNoInvMod={} mkbToggle={} mkbMod={} LTpassthrough={}",
					arg,
					oldWheelerEnabled,
					oldGamepadToggle,
					oldGamepadModifier,
					oldGamepadInvToggle,
					oldGamepadInvModifier,
					oldGamepadNoInvToggle,
					oldGamepadNoInvModifier,
					oldMkbToggle,
					oldMkbModifier,
					oldVanillaLTPassthrough);
			}
			Config::ReadControlConfig();
			if (logRebindDebugBeforeRead || ShouldLogRebindDebug()) {
				logger::info(
					"[RebindDebug] dmenu_updateSettings panel='{}' after ReadControlConfig: enabled={} gpToggle={} gpMod={} gpInvToggle={} gpInvMod={} gpNoInvToggle={} gpNoInvMod={} mkbToggle={} mkbMod={} LTpassthrough={}",
					arg,
					Config::WheelerEnabled,
					Config::InputBindings::GamePad::toggleWheel,
					Config::InputBindings::GamePad::toggleWheelModifier,
					Config::InputBindings::GamePad::toggleWheelIfInInventory,
					Config::InputBindings::GamePad::toggleWheelIfInInventoryModifier,
					Config::InputBindings::GamePad::toggleWheelIfNotInInventory,
					Config::InputBindings::GamePad::toggleWheelIfNotInInventoryModifier,
					Config::InputBindings::MKB::toggleWheel,
					Config::InputBindings::MKB::toggleWheelModifier,
					Config::VanillaLTPassthrough);
			}
			Config::ReadActionHotkeysBridgeConfig();
			Config::ReadOStimIntegrationConfig();
			
			bool ammoListSettingsChanged = false;
			if (isAmmoWheelPanel) {
				const bool oldShowAllAmmo = Config::AmmoWheel::ShowAllAmmo;
				const bool oldShowModdedAmmo = Config::AmmoWheel::ShowModdedAmmo;
				const int oldMinAmmoCount = Config::AmmoWheel::MinimumAmmoCount;
				const bool oldSortByCount = Config::AmmoWheel::SortByCount;
				const int oldSortPrimary = Config::AmmoWheel::Sort::Primary;
				const int oldSortSecondary = Config::AmmoWheel::Sort::Secondary;
				const int oldSortTertiary = Config::AmmoWheel::Sort::Tertiary;
				const bool oldSortPrimaryAsc = Config::AmmoWheel::Sort::DirectionPrimaryAsc;
				const bool oldSortSecondaryAsc = Config::AmmoWheel::Sort::DirectionSecondaryAsc;
				const bool oldSortTertiaryAsc = Config::AmmoWheel::Sort::DirectionTertiaryAsc;
				const bool oldSortStable = Config::AmmoWheel::Sort::Stable;
				const bool oldFavoritesFirst = Config::AmmoWheel::Sort::FavoritesFirst;
				const bool oldGroupByType = Config::AmmoWheel::Sort::GroupByType;
				const int oldArrowLimit = Config::AmmoWheel::Sort::ArrowLimit;
				const int oldBoltLimit = Config::AmmoWheel::Sort::BoltLimit;

				const auto oldMKBModifier = Config::AmmoWheel::MKB::modifierKey;
				const auto oldGamepadModifier = Config::AmmoWheel::GamePad::modifierButton;
				const auto oldMouseToggle = Config::AmmoWheel::MKB::toggleAmmoWheelMouse;

				Config::WriteAmmoWheelPresetOverrideIfActive();
				Config::ReadAmmoWheelConfig();
				ammoListSettingsChanged =
					oldShowAllAmmo != Config::AmmoWheel::ShowAllAmmo ||
					oldShowModdedAmmo != Config::AmmoWheel::ShowModdedAmmo ||
					oldMinAmmoCount != Config::AmmoWheel::MinimumAmmoCount ||
					oldSortByCount != Config::AmmoWheel::SortByCount ||
					oldSortPrimary != Config::AmmoWheel::Sort::Primary ||
					oldSortSecondary != Config::AmmoWheel::Sort::Secondary ||
					oldSortTertiary != Config::AmmoWheel::Sort::Tertiary ||
					oldSortPrimaryAsc != Config::AmmoWheel::Sort::DirectionPrimaryAsc ||
					oldSortSecondaryAsc != Config::AmmoWheel::Sort::DirectionSecondaryAsc ||
					oldSortTertiaryAsc != Config::AmmoWheel::Sort::DirectionTertiaryAsc ||
					oldSortStable != Config::AmmoWheel::Sort::Stable ||
					oldFavoritesFirst != Config::AmmoWheel::Sort::FavoritesFirst ||
					oldGroupByType != Config::AmmoWheel::Sort::GroupByType ||
					oldArrowLimit != Config::AmmoWheel::Sort::ArrowLimit ||
					oldBoltLimit != Config::AmmoWheel::Sort::BoltLimit;

				// Detect and notify on specific state transitions (Clear)
				if (oldMKBModifier != 0 && Config::AmmoWheel::MKB::modifierKey == 0) {
					Utils::NotificationMessage(Texts::GetText(Texts::TextType::KeyboardModifierCleared));
				}
				if (oldGamepadModifier != 0 && Config::AmmoWheel::GamePad::modifierButton == 0) {
					Utils::NotificationMessage(Texts::GetText(Texts::TextType::GamepadModifierCleared));
				}
				if (oldMouseToggle != 0 && Config::AmmoWheel::MKB::toggleAmmoWheelMouse == 0) {
					Utils::NotificationMessage(Texts::GetText(Texts::TextType::MouseToggleCleared));
				}
			} else {
				Config::ReadAmmoWheelConfig();
			}
			if (isAmmoWheelPanel) {
				AmmoWheelReskinUnified::ReskinSystem::GetSingleton().ReloadSmartFromIni();
			}
			Config::ResetScaleBaseCapture();
			Config::OffsetSizingToViewport();
			Config::OffsetAmmoWheelSizingToViewport();
			ActionHotkeysBridge::RequestRefresh();
			OStimIntegration::RequestRefresh();
			
			// Notify AmmoWheel to recalculate cached layout values
			Wheeler::NotifyAmmoWheelConfigChanged();
			if (isAmmoWheelPanel && ammoListSettingsChanged) {
				Wheeler::RefreshAmmoWheelListIfOpen();
			}
			
			// ============ CHECK IF HEAVY RELOAD NEEDED ============
			// Only perform close/rebind cycle if critical settings changed
			const bool timescaleChanged = (oldSlowTimeScale != Config::Styling::Wheel::SlowTimeScale);
			const bool keybindsChanged = (oldMkbToggle != Config::InputBindings::MKB::toggleWheel) ||
			                             (oldGamepadToggle != Config::InputBindings::GamePad::toggleWheel);
			const bool needsHeavyReload = timescaleChanged || keybindsChanged;
			auto bindAllInputsAfterDmenuReload = []() {
				const bool logRebindDebugBeforeBind = ShouldLogRebindDebug();
				if (logRebindDebugBeforeBind) {
					logger::info("[RebindDebug] calling BindAllInputsFromConfig after dMenu config reload");
				}
				Controls::BindAllInputsFromConfig();
				if (logRebindDebugBeforeBind || ShouldLogRebindDebug()) {
					logger::info("[RebindDebug] finished BindAllInputsFromConfig after dMenu config reload");
				}
			};
			
			if (needsHeavyReload) {
				logger::info("[ModCallback] Heavy reload required (timescale={}, keybinds={})", 
					timescaleChanged, keybindsChanged);
				
				// Force close to safely update timescale/keybinds
				if (wasWheelerOpen) {
					Wheeler::CloseWheeler();
				}
				Wheeler::EnsureTimescaleRestored();
				bindAllInputsAfterDmenuReload();
				
				// Re-open if it was open
				if (wasWheelerOpen) {
					Wheeler::OpenWheeler();
				}
			} else {
				// Lightweight path: just rebind inputs without close/open cycle
				bindAllInputsAfterDmenuReload();
				// Visual changes will be picked up on next Draw() frame automatically
				if (Config::Debug::LogMenuBlockReasons) {
					logger::trace("[ModCallback] Lightweight config update (no close/open cycle)");
				}
			}
			
			if (isWheelBehaviorPanel && Config::Debug::LogActionPolicy) {
				logger::info("[ModCallback] Wheel Behavior config reloaded: InstantSpell={}, InstantPowers={}, InstantTransformations={}, SlowTimeScale={:.2f}",
					Config::WheelBehavior::InstantSpell,
					Config::WheelBehavior::InstantPowers,
					Config::WheelBehavior::InstantTransformations,
					Config::Styling::Wheel::SlowTimeScale);
			}
		}
	} else if (a_event->eventName == "dmenu_buttonCallback") {
		if (a_event->strArg == "wheeler_reset_all_wheels") {
			Wheeler::RequestResetAllWheelsInCurrentWorld();
		} else if (a_event->strArg == "wheeler_wheelbehavior_restore_defaults") {
			if (Config::RestoreWheelBehaviorDefaults()) {
				Config::ReadStyleConfig();
				Config::ReadControlConfig();
				Config::ReadAmmoWheelConfig();
				Config::ResetScaleBaseCapture();  // Reset so fresh INI values are used as base
				Config::OffsetSizingToViewport();
				Config::OffsetAmmoWheelSizingToViewport();
				Controls::BindAllInputsFromConfig();
				Wheeler::NotifyAmmoWheelConfigChanged();
				Utils::NotificationMessage(Texts::GetText(Texts::TextType::WheelBehaviorRestoredToDefaults));
			} else {
				Utils::NotificationMessage(Texts::GetText(Texts::TextType::NoWheelBehaviorDefaultsSaved));
			}
		} else if (a_event->strArg == "wheeler_ammowheel_rebind_reset") {
			Config::AmmoWheel::MKB::toggleAmmoWheel = 42;
			Config::AmmoWheel::GamePad::toggleAmmoWheel = 0;
			Config::AmmoWheel::MKB::modifierKey = 0;
			Config::AmmoWheel::GamePad::modifierButton = 0;
			Config::AmmoWheel::MKB::toggleAmmoWheelMouse = 0;
			Config::AmmoWheel::ToggleKeyMKBName = Controls::GetKeyNameForMkb(Config::AmmoWheel::MKB::toggleAmmoWheel);
			Config::AmmoWheel::ToggleKeyGamepadName = Controls::GetKeyNameForGamepad(Config::AmmoWheel::GamePad::toggleAmmoWheel);
			Config::AmmoWheel::ModifierKeyMKBName = "None";
			Config::AmmoWheel::ModifierButtonGamepadName = "None";
			Config::AmmoWheel::ToggleMouseButtonName = "None";
			Config::WriteAmmoWheelKeybindOverrides();
			Controls::BindAllInputsFromConfig();
			Utils::NotificationMessage(Texts::GetText(Texts::TextType::AmmoWheelKeybindsReset));
		} else if (a_event->strArg == "wheeler_actionhotkeysbridge_reset_layout") {
			Wheeler::ResetActionHotkeysBridgeLayout();
		// ========== FACTORY DEFAULTS HANDLER ==========
		} else if (a_event->strArg == "wheeler_ammowheel_restore_factory_defaults") {
			Controls::CancelRebind();
			if (Config::AmmoWheel::RestoreFactoryDefaults()) {
				// Reload config from freshly-copied defaults file
				Config::ReadAmmoWheelConfig();
				AmmoWheelReskinUnified::ReskinSystem::GetSingleton().ReloadSmartFromIni();
				Config::OffsetAmmoWheelSizingToViewport();
				
				// Regenerate key names from numeric values
				Config::AmmoWheel::ToggleKeyMKBName = 
					Controls::GetKeyNameForMkb(Config::AmmoWheel::MKB::toggleAmmoWheel);
				Config::AmmoWheel::ToggleKeyGamepadName = 
					Controls::GetKeyNameForGamepad(Config::AmmoWheel::GamePad::toggleAmmoWheel);
				Config::AmmoWheel::ModifierKeyMKBName = 
					Config::AmmoWheel::MKB::modifierKey ? 
					Controls::GetKeyNameForMkb(Config::AmmoWheel::MKB::modifierKey) : "None";
				Config::AmmoWheel::ModifierButtonGamepadName = 
					Config::AmmoWheel::GamePad::modifierButton ? 
					Controls::GetKeyNameForGamepad(Config::AmmoWheel::GamePad::modifierButton) : "None";
				Config::AmmoWheel::ToggleMouseButtonName = 
					Config::AmmoWheel::MKB::toggleAmmoWheelMouse ? 
					Controls::GetKeyNameForMkb(Config::AmmoWheel::MKB::toggleAmmoWheelMouse) : "None";
				
				Controls::BindAllInputsFromConfig();
				Wheeler::NotifyAmmoWheelConfigChanged();
				Utils::NotificationMessage(Texts::GetText(Texts::TextType::AmmoWheelFactoryDefaultsRestored));
			} else {
				Utils::NotificationMessage(Texts::GetText(Texts::TextType::AmmoWheelFactoryDefaultsFailed));
			}
		}
	}

	return EventResult::kContinue;
}

bool ModCallbackEventHandler::Register()
{
	static ModCallbackEventHandler singleton;

	auto eventSource = SKSE::GetModCallbackEventSource();

	if (!eventSource) {
		ERROR("EventSource not found!");
		return false;
	}
	eventSource->AddEventSink(&singleton);
	INFO("Register {}", typeid(singleton).name());
	return true;
}
