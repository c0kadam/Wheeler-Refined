#include "Controls.h"
#include "bin/Wheeler/Wheeler.h"
#include "bin/Config.h"
#include "bin/Integrations/ActionHotkeysBridge.h"
#include "bin/Utilities/Utils.h"

#include <dinput.h>
#include <array>
#include <chrono>
#include <optional>
#include <Windows.h>
#include <unordered_set>

// LT (Left Trigger) key code for gamepad - conflicts with vanilla Block
static constexpr uint32_t GAMEPAD_LT = 280;
static constexpr uint32_t KEY_MOUSE_OFFSET = 256;
static constexpr uint32_t KEY_GAMEPAD_OFFSET = 266;
static constexpr uint32_t KEY_MOUSE_WHEEL_UP = KEY_MOUSE_OFFSET + 8;
static constexpr uint32_t KEY_MOUSE_WHEEL_DOWN = KEY_MOUSE_OFFSET + 9;

// Track which gamepad buttons are currently held (for modifier key checking)
static std::unordered_set<Controls::KeyId> s_heldGamepadButtons;

// Track which MKB keys are currently held (for modifier key checking)
static std::unordered_set<Controls::KeyId> s_heldMkbKeys;

// Track if we have already warned about LT binding conflict (once per session)
static bool s_warnedAboutLTBinding = false;
static Controls::RebindTarget s_rebindTarget = Controls::RebindTarget::None;
static std::chrono::steady_clock::time_point s_rebindStart{};
static std::chrono::steady_clock::time_point s_rebindIgnoreUntil{};
static bool s_rebindWarnedTimeout = false;
static std::array<std::uint8_t, 256> s_rebindPrevMkb{};
static bool s_rebindWaitingAllUp = false;
static bool s_rebindLoggedGrace = false;
static bool s_rebindLoggedAllUp = false;
static constexpr auto kRebindTimeout = std::chrono::seconds(10);
static constexpr auto kRebindIgnoreWindow = std::chrono::milliseconds(250);

static void NoOpAction()
{
}

static bool IsEditModeOnlyAction(Controls::Action action)
{
	using Action = Controls::Action;
	switch (action) {
	case Action::AddWheel:
	case Action::AddEmptyEntry:
	case Action::MoveEntryForward:
	case Action::MoveEntryBack:
	case Action::MoveWheelForward:
	case Action::MoveWheelBack:
	case Action::ToggleEditHints:
		return true;
	default:
		return false;
	}
}

static const char* ActionToString(Controls::Action action)
{
	using Action = Controls::Action;
	switch (action) {
	case Action::ExitWheel:
		return "ExitWheel";
	case Action::ActivatePrimary:
		return "ActivatePrimary";
	case Action::ActivateSecondary:
		return "ActivateSecondary";
	case Action::AddWheel:
		return "AddWheel";
	case Action::AddEmptyEntry:
		return "AddEmptyEntry";
	case Action::MoveEntryForward:
		return "MoveEntryForward";
	case Action::MoveEntryBack:
		return "MoveEntryBack";
	case Action::MoveWheelForward:
		return "MoveWheelForward";
	case Action::MoveWheelBack:
		return "MoveWheelBack";
	case Action::ToggleEditHints:
		return "ToggleEditHints";
	case Action::NextWheel:
		return "NextWheel";
	case Action::PrevWheel:
		return "PrevWheel";
	case Action::Toggle:
		return "ToggleWheel";
	case Action::ToggleIfInInventory:
		return "ToggleWheelIfInInventory";
	case Action::ToggleIfNotInInventory:
		return "ToggleWheelIfNotInInventory";
	case Action::PrevItem:
		return "PrevItem";
	case Action::NextItem:
		return "NextItem";
	case Action::ToggleAmmoWheel:
		return "ToggleAmmoWheel";
	case Action::ToggleAmmoWheelMouse:
		return "ToggleAmmoWheelMouse";
	case Action::JumpActionHotkeysWheel:
		return "JumpActionHotkeysWheel";
	case Action::ReturnToPreviousWheel:
		return "ReturnToPreviousWheel";
	case Action::RefreshActionHotkeysMirror:
		return "RefreshActionHotkeysMirror";
	case Action::ResetActionHotkeysBridgeLayout:
		return "ResetActionHotkeysBridgeLayout";
	case Action::None:
	default:
		return "None";
	}
}

static bool ShouldLogRebindDebug()
{
	return Config::Debug::InputSpy || Config::Debug::LogMenuBlockReasons;
}

static void GetInputDebugMenuFlags(bool& dmenuOpen, bool& trackedMenuOpen)
{
	dmenuOpen = false;
	trackedMenuOpen = false;
	auto ui = RE::UI::GetSingleton();
	if (!ui) {
		return;
	}

	dmenuOpen = ui->IsMenuOpen("dmenu") ||
	            ui->IsMenuOpen("dmenu_Main") ||
	            ui->IsMenuOpen("dMenu") ||
	            ui->IsMenuOpen("dMenu_Main");
	trackedMenuOpen = ui->IsMenuOpen(RE::MainMenu::MENU_NAME) ||
	                  ui->IsMenuOpen(RE::TweenMenu::MENU_NAME) ||
	                  ui->IsMenuOpen(RE::Console::MENU_NAME) ||
	                  ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME) ||
	                  ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME) ||
	                  ui->IsMenuOpen(RE::MagicMenu::MENU_NAME) ||
	                  ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME) ||
	                  ui->IsMenuOpen("LootMenu") ||
	                  ui->IsMenuOpen("LootMenuCF") ||
	                  dmenuOpen;
}

void Controls::Init()
{
	BindAllInputsFromConfig();
}

void Controls::bindInput(KeyId key, FunctionPtr func, Action action, bool isDown, bool isGamePad)
{
	if (key == 0 || !func) {
		return;
	}

	if (isDown) {
		if (isGamePad) {
			_keyFunctionMapDownGamepad[key] = func;
			_keyActionMapDownGamepad[key] = action;
		} else {
			_keyFunctionMapDown[key] = func;
			_keyActionMapDown[key] = action;
		}
	} else {
		if (isGamePad) {
			_keyFunctionMapUpGamepad[key] = func;
			_keyActionMapUpGamepad[key] = action;
		} else {
			_keyFunctionMapUp[key] = func;
			_keyActionMapUp[key] = action;
		}
	}
}

void Controls::bindModifiedInput(KeyId key, KeyId requiredModifier, FunctionPtr func, Action action, bool isGamePad)
{
	if (key == 0 || !func || requiredModifier == 0 || requiredModifier == key) {
		return;
	}

	auto& map = isGamePad ? _modifiedBindingsGamepad : _modifiedBindingsMkb;
	map[key].push_back(ModifiedBindingCandidate{
		requiredModifier,
		func,
		action
	});
}

void Controls::bindBridgeWheelInput(KeyId key, KeyId requiredModifier, std::uint32_t wheelNumber, bool isGamePad)
{
	if (key == 0 || wheelNumber == 0 || requiredModifier == key) {
		return;
	}

	auto& map = isGamePad ? _bridgeWheelBindingsGamepad : _bridgeWheelBindingsMkb;
	map[key].push_back(BridgeWheelBindingCandidate{
		requiredModifier,
		wheelNumber,
		Action::JumpActionHotkeysWheel
	});
}

void Controls::bindToggle(
	KeyId key,
	KeyId requiredModifier,
	FunctionPtr onDown,
	FunctionPtr onUp,
	Action action,
	bool isGamePad,
	bool allowNonExclusiveChordFallback)
{
	if (key == 0 || (!onDown && !onUp)) {
		return;
	}

	auto& map = isGamePad ? _toggleBindingsGamepad : _toggleBindingsMkb;
	map[key].push_back(ToggleBindingCandidate{
		requiredModifier,
		onDown,
		onUp,
		action,
		allowNonExclusiveChordFallback
	});

	// Chorded toggle bindings can be configured as non-exclusive, so they must not
	// reserve action-map ownership for the base key.
	const bool reserveActionKey = !(allowNonExclusiveChordFallback && requiredModifier != 0);
	if (reserveActionKey) {
		if (onDown) {
			if (isGamePad) {
				_keyActionMapDownGamepad[key] = action;
			} else {
				_keyActionMapDown[key] = action;
			}
		}
		if (onUp) {
			if (isGamePad) {
				_keyActionMapUpGamepad[key] = action;
			} else {
				_keyActionMapUp[key] = action;
			}
		}
	}
}

bool Controls::IsModifierHeld(KeyId key, bool isGamePad)
{
	if (key == 0) {
		return true;
	}
	return isGamePad ? s_heldGamepadButtons.contains(key) : s_heldMkbKeys.contains(key);
}

bool Controls::ToggleBindingsAllowNormalFallback(const std::vector<ToggleBindingCandidate>& candidates)
{
	if (candidates.empty()) {
		return false;
	}

	// Allow fallback only when every toggle candidate on this key is a non-exclusive chord.
	for (const auto& candidate : candidates) {
		if (!candidate.allowNonExclusiveChordFallback || candidate.requiredModifier == 0) {
			return false;
		}
	}

	return true;
}

void Controls::BindAllInputsFromConfig()
{
	std::lock_guard lock(_lock);
	if (++_bindingGeneration == 0) {
		_bindingGeneration = 1;
	}
	bool skipLTBinding = false;
	if (ShouldLogRebindDebug()) {
		logger::info(
			"[RebindDebug] BindAllInputsFromConfig start: enabled={} gpToggle={} gpMod={} gpInvToggle={} gpInvMod={} gpNoInvToggle={} gpNoInvMod={} mkbToggle={} mkbMod={} LTpassthrough={} ammoEnabled={} ammoGpToggle={} ammoGpMod={}",
			Config::WheelerEnabled,
			Config::InputBindings::GamePad::toggleWheel,
			Config::InputBindings::GamePad::toggleWheelModifier,
			Config::InputBindings::GamePad::toggleWheelIfInInventory,
			Config::InputBindings::GamePad::toggleWheelIfInInventoryModifier,
			Config::InputBindings::GamePad::toggleWheelIfNotInInventory,
			Config::InputBindings::GamePad::toggleWheelIfNotInInventoryModifier,
			Config::InputBindings::MKB::toggleWheel,
			Config::InputBindings::MKB::toggleWheelModifier,
			Config::VanillaLTPassthrough,
			Config::AmmoWheel::Enabled,
			Config::AmmoWheel::GamePad::toggleAmmoWheel,
			Config::AmmoWheel::GamePad::modifierButton);
	}
	_keyFunctionMapDown.clear();
	_keyFunctionMapDownGamepad.clear();
	_keyFunctionMapUp.clear();
	_keyFunctionMapUpGamepad.clear();
	_keyActionMapDown.clear();
	_keyActionMapDownGamepad.clear();
	_keyActionMapUp.clear();
	_keyActionMapUpGamepad.clear();
	_toggleBindingsMkb.clear();
	_toggleBindingsGamepad.clear();
	_modifiedBindingsMkb.clear();
	_modifiedBindingsGamepad.clear();
	_bridgeWheelBindingsMkb.clear();
	_bridgeWheelBindingsGamepad.clear();
	_armedToggleBindings.clear();

	// Early exit if Wheeler is completely disabled
	if (!Config::WheelerEnabled) {
		if (ShouldLogRebindDebug()) {
			const auto mainGpToggle = Config::InputBindings::GamePad::toggleWheel;
			logger::info(
				"[RebindDebug] BindAllInputsFromConfig end: gpToggleKeys={} mkbToggleKeys={} armedToggles={} mainGpToggle={} mainGpRegistered={} skipLTBinding={}",
				_toggleBindingsGamepad.size(),
				_toggleBindingsMkb.size(),
				_armedToggleBindings.size(),
				mainGpToggle,
				_toggleBindingsGamepad.contains(mainGpToggle),
				skipLTBinding);
		}
		logger::info("[Controls] Wheeler disabled - no input bindings registered");
		return;
	}

	using namespace Config::InputBindings;
	{
		using namespace MKB;
		bindInput(activatePrimary, &Wheeler::OnConfirmDown, Action::ActivatePrimary, true, false);
		bindInput(activateSecondary, &Wheeler::OnSecondaryConfirmDown, Action::ActivateSecondary, true, false);
		bindInput(addWheel, &Wheeler::AddWheel, Action::AddWheel, true, false);
		bindInput(addEmptyEntry, &Wheeler::AddEmptyEntryToCurrentWheel, Action::AddEmptyEntry, true, false);
		bindInput(moveEntryForward, &Wheeler::MoveEntryForwardInCurrentWheel, Action::MoveEntryForward, true, false);
		bindInput(moveEntryBack, &Wheeler::MoveEntryBackInCurrentWheel, Action::MoveEntryBack, true, false);
		bindInput(moveWheelForward, &Wheeler::MoveWheelForward, Action::MoveWheelForward, true, false);
		bindInput(moveWheelBack, &Wheeler::MoveWheelBack, Action::MoveWheelBack, true, false);
		bindInput(toggleEditHints, &Wheeler::ToggleEditModeHintsVisibility, Action::ToggleEditHints, true, false);
		bindInput(nextWheel, &Wheeler::NextWheel, Action::NextWheel, true, false);
		bindInput(prevWheel, &Wheeler::PrevWheel, Action::PrevWheel, true, false);
		bindInput(prevItem, &Wheeler::PrevItemInEntry, Action::PrevItem, true, false);
		bindInput(nextItem, &Wheeler::NextItemInEntry, Action::NextItem, true, false);
		bindInput(closeWheel, &NoOpAction, Action::ExitWheel, true, false);
		bindInput(closeWheelAlt, &NoOpAction, Action::ExitWheel, true, false);

		bindInput(activatePrimary, &Wheeler::OnConfirmUp, Action::ActivatePrimary, false, false);
		bindInput(activateSecondary, &Wheeler::OnSecondaryConfirmUp, Action::ActivateSecondary, false, false);

		bindToggle(
			toggleWheel,
			toggleWheelModifier,
			&Wheeler::ToggleWheeler,
			&Wheeler::CloseWheelerIfOpenedLongEnough,
			Action::Toggle,
			false,
			true);

		if (Config::AmmoWheel::Enabled) {
			bindToggle(
				Config::AmmoWheel::MKB::toggleAmmoWheel,
				Config::AmmoWheel::MKB::modifierKey,
				&Wheeler::ToggleAmmoWheel,
				&Wheeler::CloseAmmoWheelIfOpenedLongEnough,
				Action::ToggleAmmoWheel,
				false);

			bindToggle(
				Config::AmmoWheel::MKB::toggleAmmoWheelMouse,
				Config::AmmoWheel::MKB::modifierKey,
				&Wheeler::ToggleAmmoWheel,
				&Wheeler::CloseAmmoWheelIfOpenedLongEnough,
				Action::ToggleAmmoWheelMouse,
				false);
		}

		auto bindBridgeCommand = [&](KeyId key, KeyId modifier, FunctionPtr func, Action action) {
			const bool isGamePadBinding = key >= KEY_GAMEPAD_OFFSET;
			if (modifier != 0) {
				bindModifiedInput(key, modifier, func, action, isGamePadBinding);
			} else {
				bindInput(key, func, action, true, isGamePadBinding);
			}
		};
		for (std::size_t i = 0; i < Config::ActionHotkeysBridge::Wheels.size(); ++i) {
			const auto& wheel = Config::ActionHotkeysBridge::Wheels[i];
			if (wheel.JumpKey == 0) {
				continue;
			}
			const bool isGamePadBinding = wheel.JumpKey >= KEY_GAMEPAD_OFFSET;
			bindBridgeWheelInput(
				wheel.JumpKey,
				wheel.JumpKeyModifier,
				static_cast<std::uint32_t>(i + 1),
				isGamePadBinding);
		}
		bindBridgeCommand(
			Config::ActionHotkeysBridge::ResetLayout,
			Config::ActionHotkeysBridge::ResetLayoutModifier,
			&Wheeler::ResetActionHotkeysBridgeLayout,
			Action::ResetActionHotkeysBridgeLayout);
		bindBridgeCommand(
			Config::ActionHotkeysBridge::RefreshMirror,
			Config::ActionHotkeysBridge::RefreshMirrorModifier,
			&Wheeler::RefreshActionHotkeysMirror,
			Action::RefreshActionHotkeysMirror);
	}

	{
		using namespace GamePad;

		if (Config::VanillaLTPassthrough) {
			if (toggleWheel == GAMEPAD_LT || toggleWheelIfInInventory == GAMEPAD_LT || toggleWheelIfNotInInventory == GAMEPAD_LT) {
				logger::info("[Controls] VanillaLTPassthrough enabled - LT (280) will NOT be bound to Wheeler, vanilla Block preserved");
				if (ShouldLogRebindDebug()) {
					logger::warn(
						"[RebindDebug] LT binding skipped because VanillaLTPassthrough=true. toggleWheel={} toggleWheelIfInInventory={} toggleWheelIfNotInInventory={}",
						toggleWheel,
						toggleWheelIfInInventory,
						toggleWheelIfNotInInventory);
				}
				skipLTBinding = true;
			}
		} else if (Config::WarnOnLTBinding && !s_warnedAboutLTBinding) {
			if (toggleWheel == GAMEPAD_LT || toggleWheelIfInInventory == GAMEPAD_LT || toggleWheelIfNotInInventory == GAMEPAD_LT) {
				logger::warn("[Controls] Wheeler is bound to LT (280) which conflicts with vanilla Block.");
				logger::warn("[Controls] If you cannot block with LT, set VanillaLTPassthrough=true in wheelBehavior.ini");
				logger::warn("[Controls] or rebind toggleWheel to a different button in Controls.ini");
				s_warnedAboutLTBinding = true;
			}
		}

		auto bindGamepadInput = [&](KeyId key, FunctionPtr func, Action action, bool isDown) {
			if (skipLTBinding && key == GAMEPAD_LT) {
				return;
			}
			bindInput(key, func, action, isDown, true);
		};

		auto bindGamepadToggle = [&](KeyId key, KeyId requiredModifier, FunctionPtr onDown, FunctionPtr onUp, Action action, bool allowNonExclusiveChordFallback = false) {
			if (skipLTBinding && key == GAMEPAD_LT) {
				return;
			}
			bindToggle(key, requiredModifier, onDown, onUp, action, true, allowNonExclusiveChordFallback);
		};

		bindGamepadInput(activatePrimary, &Wheeler::OnConfirmDown, Action::ActivatePrimary, true);
		bindGamepadInput(activateSecondary, &Wheeler::OnSecondaryConfirmDown, Action::ActivateSecondary, true);
		bindGamepadInput(addWheel, &Wheeler::AddWheel, Action::AddWheel, true);
		bindGamepadInput(addEmptyEntry, &Wheeler::AddEmptyEntryToCurrentWheel, Action::AddEmptyEntry, true);
		bindGamepadInput(moveEntryForward, &Wheeler::MoveEntryForwardInCurrentWheel, Action::MoveEntryForward, true);
		bindGamepadInput(moveEntryBack, &Wheeler::MoveEntryBackInCurrentWheel, Action::MoveEntryBack, true);
		bindGamepadInput(moveWheelForward, &Wheeler::MoveWheelForward, Action::MoveWheelForward, true);
		bindGamepadInput(moveWheelBack, &Wheeler::MoveWheelBack, Action::MoveWheelBack, true);
		bindGamepadInput(toggleEditHints, &Wheeler::ToggleEditModeHintsVisibility, Action::ToggleEditHints, true);
		bindGamepadInput(nextWheel, &Wheeler::NextWheel, Action::NextWheel, true);
		bindGamepadInput(prevWheel, &Wheeler::PrevWheel, Action::PrevWheel, true);
		bindGamepadInput(prevItem, &Wheeler::PrevItemInEntryGamepad, Action::PrevItem, true);
		bindGamepadInput(nextItem, &Wheeler::NextItemInEntryGamepad, Action::NextItem, true);

		bindGamepadInput(activatePrimary, &Wheeler::OnConfirmUp, Action::ActivatePrimary, false);
		bindGamepadInput(activateSecondary, &Wheeler::OnSecondaryConfirmUp, Action::ActivateSecondary, false);

		bindGamepadToggle(
			toggleWheel,
			toggleWheelModifier,
			&Wheeler::ToggleWheeler,
			&Wheeler::CloseWheelerIfOpenedLongEnough,
			Action::Toggle,
			true);
		bindGamepadToggle(
			toggleWheelIfNotInInventory,
			toggleWheelIfNotInInventoryModifier,
			&Wheeler::ToggleWheelIfNotInInventory,
			&Wheeler::CloseWheelerIfOpenedLongEnoughIfNotInInventory,
			Action::ToggleIfNotInInventory,
			true);
		bindGamepadToggle(
			toggleWheelIfInInventory,
			toggleWheelIfInInventoryModifier,
			&Wheeler::ToggleWheelIfInInventory,
			&Wheeler::CloseWheelerIfOpenedLongEnoughIfInInventory,
			Action::ToggleIfInInventory,
			true);
		bindGamepadInput(exitWheel, &NoOpAction, Action::ExitWheel, true);

		if (Config::AmmoWheel::Enabled && Config::AmmoWheel::GamePad::toggleAmmoWheel != 0) {
			bindGamepadToggle(
				Config::AmmoWheel::GamePad::toggleAmmoWheel,
				Config::AmmoWheel::GamePad::modifierButton,
				&Wheeler::ToggleAmmoWheel,
				&Wheeler::CloseAmmoWheelIfOpenedLongEnough,
				Action::ToggleAmmoWheel);
		}
	}

	if (Config::Debug::InputSpy) {
		struct BindingRow
		{
			bool isGamepad = false;
			KeyId baseKey = 0;
			KeyId modifierKey = 0;
			const char* module = "Unknown";
			const char* action = "None";
			const char* phase = "Down";
			bool nonExclusiveChord = false;
		};

		auto moduleForAction = [](Action action) -> const char* {
			switch (action) {
			case Action::ToggleAmmoWheel:
			case Action::ToggleAmmoWheelMouse:
				return "AmmoWheel";
			case Action::JumpActionHotkeysWheel:
			case Action::ReturnToPreviousWheel:
			case Action::RefreshActionHotkeysMirror:
			case Action::ResetActionHotkeysBridgeLayout:
				return "ActionHotkeysBridge";
			default:
				return "MainWheel";
			}
		};

		std::vector<BindingRow> rows;
		rows.reserve(
			_keyActionMapDown.size() + _keyActionMapUp.size() +
			_keyActionMapDownGamepad.size() + _keyActionMapUpGamepad.size() +
			_toggleBindingsMkb.size() + _toggleBindingsGamepad.size());

		auto collectActionMap = [&](const auto& actionMap, bool isGamepad, const char* phase) {
			for (const auto& [baseKey, action] : actionMap) {
				if (action == Action::None || baseKey == 0) {
					continue;
				}
				rows.push_back(BindingRow{
					isGamepad,
					baseKey,
					0,
					moduleForAction(action),
					ActionToString(action),
					phase,
					false
				});
			}
		};

		auto collectToggleMap = [&](const auto& toggleMap, bool isGamepad) {
			for (const auto& [baseKey, candidates] : toggleMap) {
				for (const auto& candidate : candidates) {
					if (candidate.action == Action::None || baseKey == 0) {
						continue;
					}
					rows.push_back(BindingRow{
						isGamepad,
						baseKey,
						candidate.requiredModifier,
						moduleForAction(candidate.action),
						ActionToString(candidate.action),
						"Toggle",
						candidate.allowNonExclusiveChordFallback
					});
				}
			}
		};

		auto collectBridgeMap = [&](const auto& bridgeMap, bool isGamepad) {
			for (const auto& [baseKey, candidates] : bridgeMap) {
				for (const auto& candidate : candidates) {
					if (candidate.action == Action::None || baseKey == 0) {
						continue;
					}
					rows.push_back(BindingRow{
						isGamepad,
						baseKey,
						candidate.requiredModifier,
						moduleForAction(candidate.action),
						ActionToString(candidate.action),
						"Down",
						false
					});
				}
			}
		};

		collectActionMap(_keyActionMapDown, false, "Down");
		collectActionMap(_keyActionMapUp, false, "Up");
		collectActionMap(_keyActionMapDownGamepad, true, "Down");
		collectActionMap(_keyActionMapUpGamepad, true, "Up");
		collectToggleMap(_toggleBindingsMkb, false);
		collectToggleMap(_toggleBindingsGamepad, true);
		collectBridgeMap(_bridgeWheelBindingsMkb, false);
		collectBridgeMap(_bridgeWheelBindingsGamepad, true);

		logger::info("[InputCompat] ===== Binding inventory =====");
		for (const auto& row : rows) {
			logger::info(
				"[InputCompat] module={} device={} action={} phase={} base={} modifier={} nonExclusiveChord={}",
				row.module,
				row.isGamepad ? "Gamepad" : "MKB",
				row.action,
				row.phase,
				row.baseKey,
				row.modifierKey,
				row.nonExclusiveChord ? "1" : "0");
			if (row.modifierKey != 0 && row.modifierKey == row.baseKey) {
				logger::warn(
					"[InputCompat] self-chord detected module={} action={} key={}; this binding cannot be satisfied.",
					row.module,
					row.action,
					row.baseKey);
			}
		}

		std::unordered_map<KeyId, std::vector<const BindingRow*>> mkbByBase;
		std::unordered_map<KeyId, std::vector<const BindingRow*>> gamepadByBase;
		for (const auto& row : rows) {
			if (row.baseKey == 0) {
				continue;
			}
			if (row.isGamepad) {
				gamepadByBase[row.baseKey].push_back(&row);
			} else {
				mkbByBase[row.baseKey].push_back(&row);
			}
		}

		auto logCollisions = [](const auto& byBase, const char* deviceTag) {
			for (const auto& [baseKey, list] : byBase) {
				if (list.size() <= 1) {
					continue;
				}
				std::string details;
				bool chordCollision = false;
				for (const auto* row : list) {
					if (!details.empty()) {
						details += " | ";
					}
					details += std::string(row->module) + ":" + row->action + ":" + row->phase;
					if (row->modifierKey != 0) {
						chordCollision = true;
						details += "(mod=" + std::to_string(row->modifierKey) + ")";
					}
				}
				if (chordCollision) {
					logger::warn(
						"[InputCompat] chord-base collision device={} base={} entries={}",
						deviceTag,
						baseKey,
						details);
				} else {
					logger::warn(
						"[InputCompat] base-key overlap device={} base={} entries={}",
						deviceTag,
						baseKey,
						details);
				}
			}
		};

		logCollisions(mkbByBase, "MKB");
		logCollisions(gamepadByBase, "Gamepad");
		logger::info("[InputCompat] modifier held-state source: TrackKeyState (all button edges).");
	}

	if (ShouldLogRebindDebug()) {
		const auto mainGpToggle = Config::InputBindings::GamePad::toggleWheel;
		logger::info(
			"[RebindDebug] BindAllInputsFromConfig end: gpToggleKeys={} mkbToggleKeys={} armedToggles={} mainGpToggle={} mainGpRegistered={} skipLTBinding={}",
			_toggleBindingsGamepad.size(),
			_toggleBindingsMkb.size(),
			_armedToggleBindings.size(),
			mainGpToggle,
			_toggleBindingsGamepad.contains(mainGpToggle),
			skipLTBinding);
	}
}

bool Controls::IsKeyBound(KeyId key)
{
	std::lock_guard lock(_lock);
	const ArmedToggleKey mkbKey{ key, false };
	const ArmedToggleKey gamepadKey{ key, true };
	return _keyFunctionMapDown.contains(key) ||
	       _keyFunctionMapUp.contains(key) ||
	       _keyFunctionMapDownGamepad.contains(key) ||
	       _keyFunctionMapUpGamepad.contains(key) ||
	       _toggleBindingsMkb.contains(key) ||
	       _toggleBindingsGamepad.contains(key) ||
	       _modifiedBindingsMkb.contains(key) ||
	       _modifiedBindingsGamepad.contains(key) ||
	       _bridgeWheelBindingsMkb.contains(key) ||
	       _bridgeWheelBindingsGamepad.contains(key) ||
	       _armedToggleBindings.contains(mkbKey) ||
	       _armedToggleBindings.contains(gamepadKey);
}

bool Controls::HasBridgeWheelBinding(KeyId key, bool isGamePad)
{
	std::lock_guard lock(_lock);
	const auto& bridgeMap = isGamePad ? _bridgeWheelBindingsGamepad : _bridgeWheelBindingsMkb;
	return bridgeMap.contains(key);
}

bool Controls::IsKeyExclusivelyBound(KeyId key)
{
	std::lock_guard lock(_lock);
	const auto hasExclusiveToggleBinding = [&](const auto& toggleMap) {
		auto it = toggleMap.find(key);
		return it != toggleMap.end() && !ToggleBindingsAllowNormalFallback(it->second);
	};

	return _keyFunctionMapDown.contains(key) ||
	       _keyFunctionMapUp.contains(key) ||
	       _keyFunctionMapDownGamepad.contains(key) ||
	       _keyFunctionMapUpGamepad.contains(key) ||
	       _modifiedBindingsMkb.contains(key) ||
	       _modifiedBindingsGamepad.contains(key) ||
	       _bridgeWheelBindingsMkb.contains(key) ||
	       _bridgeWheelBindingsGamepad.contains(key) ||
	       hasExclusiveToggleBinding(_toggleBindingsMkb) ||
	       hasExclusiveToggleBinding(_toggleBindingsGamepad);
}

bool Controls::IsMkbKeyHeld(KeyId key)
{
	std::lock_guard lock(_lock);
	return s_heldMkbKeys.contains(key);
}

bool Controls::IsGamepadKeyHeld(KeyId key)
{
	std::lock_guard lock(_lock);
	return s_heldGamepadButtons.contains(key);
}

void Controls::TrackKeyState(KeyId key, bool isDown, bool isGamePad)
{
	bool shouldLogGamepadEdge = false;
	bool wasHeld = false;
	bool nowHeld = false;
	std::size_t heldGamepadCount = 0;
	bool hasToggleBinding = false;
	bool hasDownBinding = false;
	bool hasUpBinding = false;

	{
		std::lock_guard lock(_lock);
		bool stateChanged = false;

		// Track key state for modifier detection (called for ALL keys, not just bound ones)
		if (isGamePad) {
			wasHeld = s_heldGamepadButtons.contains(key);
			if (isDown) {
				s_heldGamepadButtons.insert(key);
			} else {
				s_heldGamepadButtons.erase(key);
			}
			nowHeld = s_heldGamepadButtons.contains(key);
			stateChanged = wasHeld != nowHeld;
			if (Config::Debug::InputSpy && wasHeld != nowHeld) {
				shouldLogGamepadEdge = true;
				heldGamepadCount = s_heldGamepadButtons.size();
				hasToggleBinding = _toggleBindingsGamepad.contains(key);
				hasDownBinding = _keyFunctionMapDownGamepad.contains(key);
				hasUpBinding = _keyFunctionMapUpGamepad.contains(key);
			}
		} else {
			wasHeld = s_heldMkbKeys.contains(key);
			if (isDown) {
				s_heldMkbKeys.insert(key);
			} else {
				s_heldMkbKeys.erase(key);
			}
			nowHeld = s_heldMkbKeys.contains(key);
			stateChanged = wasHeld != nowHeld;
		}
		if (stateChanged) {
			++_keyStateGenerations[ArmedToggleKey{ key, isGamePad }];
		}
	}

	if (shouldLogGamepadEdge) {
		logger::info(
			"[InputDebug] gamepad edge key={} down={} wasHeld={} nowHeld={} heldCount={} hasToggleBinding={} hasDownBinding={} hasUpBinding={}",
			key,
			isDown,
			wasHeld,
			nowHeld,
			heldGamepadCount,
			hasToggleBinding,
			hasDownBinding,
			hasUpBinding);
	}
}

Controls::DispatchResult Controls::Dispatch(KeyId key, bool isDown, bool isGamePad)
{
	const bool logGamepadDispatch = isGamePad && Config::Debug::InputSpy;
	bool debugDmenuOpen = false;
	bool debugTrackedMenuOpen = false;
	if (logGamepadDispatch) {
		GetInputDebugMenuFlags(debugDmenuOpen, debugTrackedMenuOpen);
	}

	std::unique_lock lock(_lock);
	const ArmedToggleKey dispatchKey{ key, isGamePad };
	std::size_t debugToggleCandidateCount = 0;
	if (isGamePad) {
		const auto it = _toggleBindingsGamepad.find(key);
		debugToggleCandidateCount = it != _toggleBindingsGamepad.end() ? it->second.size() : 0;
	} else {
		const auto it = _toggleBindingsMkb.find(key);
		debugToggleCandidateCount = it != _toggleBindingsMkb.end() ? it->second.size() : 0;
	}

	if (logGamepadDispatch) {
		logger::info(
			"[InputDebug] dispatch gamepad key={} down={} isGamePad={} toggleCandidates={} wheelerOpen={} ammoOpen={} dmenuOpen={} menuOpen={} hasToggleBinding={} armed={} hasDownBinding={} hasUpBinding={}",
			key,
			isDown,
			isGamePad,
			debugToggleCandidateCount,
			Wheeler::IsWheelerOpen(),
			Wheeler::IsAmmoWheelOpen(),
			debugDmenuOpen,
			debugTrackedMenuOpen,
			_toggleBindingsGamepad.contains(key),
			_armedToggleBindings.contains(dispatchKey),
			_keyFunctionMapDownGamepad.contains(key),
			_keyFunctionMapUpGamepad.contains(key));
	}

	if (!isDown) {
		std::optional<ArmedToggleState> armed;
		{
			const auto it = _armedToggleBindings.find(dispatchKey);
			if (it != _armedToggleBindings.end()) {
				armed = it->second;
				_armedToggleBindings.erase(it);
			}
		}
		if (armed) {
			if (logGamepadDispatch) {
				const bool modifierHeld = armed->requiredModifier == 0 || IsModifierHeld(armed->requiredModifier, isGamePad);
				logger::info(
					"[InputDebug] toggle candidate action={} key={} requiredModifier={} modifierHeld={} onDown={} onUp={}",
					ActionToString(armed->action),
					armed->baseKey != 0 ? armed->baseKey : key,
					armed->requiredModifier,
					modifierHeld,
					false,
					armed->onUp != nullptr);
				logger::info(
					"[InputDebug] executing toggle action={} key={} phase=up",
					ActionToString(armed->action),
					armed->baseKey != 0 ? armed->baseKey : key);
			}
			lock.unlock();
			if (armed->onUp) {
				armed->onUp();
			}
			return armed->releaseResult;
		}

		FunctionPtr callback = nullptr;
		Action action = Action::None;
		if (isGamePad) {
			const auto it = _keyFunctionMapUpGamepad.find(key);
			if (it != _keyFunctionMapUpGamepad.end()) {
				callback = it->second;
			}
			const auto actionIt = _keyActionMapUpGamepad.find(key);
			if (actionIt != _keyActionMapUpGamepad.end()) {
				action = actionIt->second;
			}
		} else {
			const auto it = _keyFunctionMapUp.find(key);
			if (it != _keyFunctionMapUp.end()) {
				callback = it->second;
			}
			const auto actionIt = _keyActionMapUp.find(key);
			if (actionIt != _keyActionMapUp.end()) {
				action = actionIt->second;
			}
		}
		if (!callback) {
			if (logGamepadDispatch) {
				logger::info("[InputDebug] no gamepad binding matched key={} down={}", key, isDown);
			}
			return DispatchResult::NotHandled;
		}
		if (logGamepadDispatch) {
			logger::info(
				"[InputDebug] executing direct binding action={} key={} phase=up",
				ActionToString(action),
				key);
		}
		lock.unlock();
		callback();
		return DispatchResult::HandledPassThrough;
	}

	// Ignore key-repeat DOWN events while a toggle press is already armed and waiting
	// for its release callback; this prevents open/close thrashing when a key is held.
	if (auto armedIt = _armedToggleBindings.find(dispatchKey); armedIt != _armedToggleBindings.end()) {
		if (logGamepadDispatch) {
			logger::info(
				"[InputDebug] skip repeat armed toggle action={} key={} phase=down",
				ActionToString(armedIt->second.action),
				armedIt->second.baseKey != 0 ? armedIt->second.baseKey : key);
		}
		return armedIt->second.releaseResult;
	}

	struct ToggleDispatchCandidate
	{
		ToggleBindingCandidate binding;
		KeyId releaseTriggerKey = 0;
		std::uint64_t keyStateGeneration = 0;
		bool modifierHeld = false;
	};
	std::vector<ToggleDispatchCandidate> toggleCandidates;
	const std::uint64_t capturedBindingGeneration = _bindingGeneration;
	bool hasToggleBinding = false;
	std::vector<ToggleBindingCandidate> capturedBindings;
	if (isGamePad) {
		const auto it = _toggleBindingsGamepad.find(key);
		if (it != _toggleBindingsGamepad.end()) {
			hasToggleBinding = true;
			capturedBindings = it->second;
		}
	} else {
		const auto it = _toggleBindingsMkb.find(key);
		if (it != _toggleBindingsMkb.end()) {
			hasToggleBinding = true;
			capturedBindings = it->second;
		}
	}
	if (hasToggleBinding) {
		toggleCandidates.reserve(capturedBindings.size());
		for (const ToggleBindingCandidate& binding : capturedBindings) {
			const bool chorded = binding.requiredModifier != 0;
			const bool selfChord = chorded && binding.requiredModifier == key;
			const KeyId releaseTriggerKey =
				(chorded && isGamePad && binding.requiredModifier != 0) ? binding.requiredModifier : key;
			const ArmedToggleKey releaseKey{ releaseTriggerKey, isGamePad };
			const auto generationIt = _keyStateGenerations.find(releaseKey);
			toggleCandidates.push_back(ToggleDispatchCandidate{
				binding,
				releaseTriggerKey,
				generationIt != _keyStateGenerations.end() ? generationIt->second : 0,
				!chorded || (!selfChord && IsModifierHeld(binding.requiredModifier, isGamePad))
			});
		}
		lock.unlock();

		for (int priority = 1; priority >= 0; --priority) {
			for (const ToggleDispatchCandidate& captured : toggleCandidates) {
				const ToggleBindingCandidate& candidate = captured.binding;
				const bool chorded = candidate.requiredModifier != 0;
				const int candidatePriority = chorded ? 1 : 0;
				if (candidatePriority != priority) {
					continue;
				}
				const bool selfChord = chorded && candidate.requiredModifier == key;
				const bool modifierHeld = captured.modifierHeld;
				if (logGamepadDispatch) {
					logger::info(
						"[InputDebug] toggle candidate action={} key={} requiredModifier={} modifierHeld={} onDown={} onUp={}",
						ActionToString(candidate.action),
						key,
						candidate.requiredModifier,
						modifierHeld,
						candidate.onDown != nullptr,
						candidate.onUp != nullptr);
				}
				if (!candidate.onDown) {
					continue;
				}
				if (chorded) {
					if (selfChord) {
						if (logGamepadDispatch) {
							logger::info(
								"[InputDebug] skip toggle action={} key={} requiredModifier={} reason=self_chord",
								ActionToString(candidate.action),
								key,
								candidate.requiredModifier);
						}
						continue;  // avoid self-chords
					}
					if (!modifierHeld) {
						if (logGamepadDispatch) {
							logger::info(
								"[InputDebug] skip toggle action={} key={} requiredModifier={} modifierHeld={}",
								ActionToString(candidate.action),
								key,
								candidate.requiredModifier,
								modifierHeld);
						}
						continue;
					}
				}

				const bool beforeMainOpen = Wheeler::IsWheelerOpen();
				const bool beforeAmmoOpen = Wheeler::IsAmmoWheelOpen();
				if (logGamepadDispatch) {
					logger::info(
						"[InputDebug] executing toggle action={} key={} phase=down",
						ActionToString(candidate.action),
						key);
				}
				candidate.onDown();
				const bool afterMainOpen = Wheeler::IsWheelerOpen();
				const bool afterAmmoOpen = Wheeler::IsAmmoWheelOpen();
				const bool stateChanged = (beforeMainOpen != afterMainOpen) || (beforeAmmoOpen != afterAmmoOpen);
				if (!stateChanged) {
					if (logGamepadDispatch) {
						logger::info(
							"[InputDebug] toggle action={} key={} phase=down produced no state change mainBefore={} mainAfter={} ammoBefore={} ammoAfter={}",
							ActionToString(candidate.action),
							key,
							beforeMainOpen,
							afterMainOpen,
							beforeAmmoOpen,
							afterAmmoOpen);
					}
					continue;
				}

				const DispatchResult releaseResult = chorded ? DispatchResult::Consumed : DispatchResult::HandledPassThrough;
				// For gamepad chords, release callback is bound to the modifier key so base-key release
				// does not immediately close the wheel.
				const KeyId releaseTriggerKey = captured.releaseTriggerKey;
				const ArmedToggleKey releaseKey{ releaseTriggerKey, isGamePad };
				FunctionPtr releaseDuringCallback = nullptr;
				lock.lock();
				{
					const auto currentGenerationIt = _keyStateGenerations.find(releaseKey);
					const std::uint64_t currentKeyGeneration =
						currentGenerationIt != _keyStateGenerations.end() ? currentGenerationIt->second : 0;
					const bool generationCurrent =
						capturedBindingGeneration == _bindingGeneration &&
						captured.keyStateGeneration == currentKeyGeneration;
					const bool releaseKeyStillHeld = IsModifierHeld(releaseTriggerKey, isGamePad);
					if (candidate.onUp && generationCurrent && releaseKeyStillHeld) {
						_armedToggleBindings[releaseKey] = ArmedToggleState{
							candidate.onUp,
							releaseResult,
							candidate.action,
							key,
							candidate.requiredModifier
						};
					} else if (candidate.onUp) {
						releaseDuringCallback = candidate.onUp;
					} else if (generationCurrent) {
						_armedToggleBindings.erase(releaseKey);
					}
				}
				lock.unlock();
				if (releaseDuringCallback) {
					releaseDuringCallback();
				}
				return releaseResult;
			}
		}
		// No toggle candidate produced a real state change (chord mismatch or blocked by
		// current context). Fall through so normal key bindings sharing this base key can run.
		if (logGamepadDispatch) {
			logger::info("[InputDebug] no toggle candidate changed state key={} down={}", key, isDown);
		}
		lock.lock();
	}

	FunctionPtr modifiedCallback = nullptr;
	{
		const auto& modifiedMap = isGamePad ? _modifiedBindingsGamepad : _modifiedBindingsMkb;
		const auto modifiedIt = modifiedMap.find(key);
		if (modifiedIt != modifiedMap.end()) {
			for (const auto& candidate : modifiedIt->second) {
				if (!candidate.onDown || !IsModifierHeld(candidate.requiredModifier, isGamePad)) {
					continue;
				}
				modifiedCallback = candidate.onDown;
				break;
			}
		}
	}
	if (modifiedCallback) {
		lock.unlock();
		modifiedCallback();
		return DispatchResult::HandledPassThrough;
	}

	std::vector<std::uint32_t> eligibleWheels;
	{
		const auto& bridgeMap = isGamePad ? _bridgeWheelBindingsGamepad : _bridgeWheelBindingsMkb;
		const auto bridgeIt = bridgeMap.find(key);
		if (bridgeIt != bridgeMap.end()) {
			for (const auto& candidate : bridgeIt->second) {
				if (candidate.requiredModifier != 0 && !IsModifierHeld(candidate.requiredModifier, isGamePad)) {
					continue;
				}
				eligibleWheels.push_back(candidate.wheelNumber);
			}
		}
	}
	if (!eligibleWheels.empty()) {
		lock.unlock();
		for (const std::uint32_t wheelNumber : eligibleWheels) {
			if (ActionHotkeysBridge::JumpToWheel(wheelNumber)) {
				return DispatchResult::Consumed;
			}
		}
		lock.lock();
	}

	FunctionPtr callback = nullptr;
	Action action = Action::None;
	bool hasAction = false;
	if (isGamePad) {
		const auto callbackIt = _keyFunctionMapDownGamepad.find(key);
		if (callbackIt != _keyFunctionMapDownGamepad.end()) {
			callback = callbackIt->second;
		}
		const auto actionIt = _keyActionMapDownGamepad.find(key);
		if (actionIt != _keyActionMapDownGamepad.end()) {
			action = actionIt->second;
			hasAction = true;
		}
	} else {
		const auto callbackIt = _keyFunctionMapDown.find(key);
		if (callbackIt != _keyFunctionMapDown.end()) {
			callback = callbackIt->second;
		}
		const auto actionIt = _keyActionMapDown.find(key);
		if (actionIt != _keyActionMapDown.end()) {
			action = actionIt->second;
			hasAction = true;
		}
	}
	if (!callback) {
		if (logGamepadDispatch) {
			logger::info("[InputDebug] no gamepad binding matched key={} down={}", key, isDown);
		}
		return DispatchResult::NotHandled;
	}
	if (!hasAction) {
		if (logGamepadDispatch) {
			logger::info("[InputDebug] no gamepad binding matched key={} down={} reason=missingAction", key, isDown);
		}
		return DispatchResult::NotHandled;
	}
	lock.unlock();
	if (IsEditModeOnlyAction(action) && !Wheeler::IsInEditMode()) {
		if (logGamepadDispatch) {
			logger::info(
				"[InputDebug] skip direct binding action={} key={} reason=editModeOnly editMode={}",
				ActionToString(action),
				key,
				Wheeler::IsInEditMode());
		}
		return DispatchResult::NotHandled;
	}
	if (logGamepadDispatch) {
		logger::info(
			"[InputDebug] executing direct binding action={} key={} phase=down",
			ActionToString(action),
			key);
	}
	callback();
	return DispatchResult::HandledPassThrough;
}

Controls::Action Controls::ResolveAction(KeyId key, bool isDown, bool isGamePad)
{
	std::lock_guard lock(_lock);

	if (isGamePad) {
		if (isDown) {
			auto bridgeIt = _bridgeWheelBindingsGamepad.find(key);
			if (bridgeIt != _bridgeWheelBindingsGamepad.end()) {
				for (const auto& candidate : bridgeIt->second) {
					if (candidate.requiredModifier == 0 || IsModifierHeld(candidate.requiredModifier, true)) {
						return candidate.action;
					}
				}
			}
			auto modifiedIt = _modifiedBindingsGamepad.find(key);
			if (modifiedIt != _modifiedBindingsGamepad.end()) {
				for (const auto& candidate : modifiedIt->second) {
					if (IsModifierHeld(candidate.requiredModifier, true)) {
						return candidate.action;
					}
				}
			}
		}
		if (isDown) {
			auto it = _keyActionMapDownGamepad.find(key);
			if (it == _keyActionMapDownGamepad.end()) {
				return Action::None;
			}
			if (IsEditModeOnlyAction(it->second) && !Wheeler::IsInEditMode()) {
				return Action::None;
			}
			return it->second;
		}
		auto it = _keyActionMapUpGamepad.find(key);
		return it != _keyActionMapUpGamepad.end() ? it->second : Action::None;
	}

	if (isDown) {
		auto bridgeIt = _bridgeWheelBindingsMkb.find(key);
		if (bridgeIt != _bridgeWheelBindingsMkb.end()) {
			for (const auto& candidate : bridgeIt->second) {
				if (candidate.requiredModifier == 0 || IsModifierHeld(candidate.requiredModifier, false)) {
					return candidate.action;
				}
			}
		}
		auto modifiedIt = _modifiedBindingsMkb.find(key);
		if (modifiedIt != _modifiedBindingsMkb.end()) {
			for (const auto& candidate : modifiedIt->second) {
				if (IsModifierHeld(candidate.requiredModifier, false)) {
					return candidate.action;
				}
			}
		}
		auto it = _keyActionMapDown.find(key);
		if (it == _keyActionMapDown.end()) {
			return Action::None;
		}
		if (IsEditModeOnlyAction(it->second) && !Wheeler::IsInEditMode()) {
			return Action::None;
		}
		return it->second;
	}
	auto it = _keyActionMapUp.find(key);
	return it != _keyActionMapUp.end() ? it->second : Action::None;
}

static const char* GetRebindTargetName(Controls::RebindTarget target)
{
	switch (target) {
	case Controls::RebindTarget::AmmoWheelMKB:
		return "AmmoWheel MKB";
	case Controls::RebindTarget::AmmoWheelGamepad:
		return "AmmoWheel Gamepad";
	case Controls::RebindTarget::AmmoWheelMKBModifier:
		return "AmmoWheel MKB Modifier";
	case Controls::RebindTarget::AmmoWheelGamepadModifier:
		return "AmmoWheel Gamepad Modifier";
	case Controls::RebindTarget::AmmoWheelMouse:
		return "AmmoWheel Mouse";
	case Controls::RebindTarget::None:
	default:
		return "None";
	}
}

static bool ShouldLogRebind()
{
	return Config::AmmoWheel::Debug::LogInput;
}

static void GetRebindMenuFlags(bool& menuMode, bool& dmenuOpen)
{
	menuMode = false;
	dmenuOpen = false;
	auto ui = RE::UI::GetSingleton();
	if (!ui) {
		return;
	}
	if (ui->IsMenuOpen(RE::MainMenu::MENU_NAME) ||
		ui->IsMenuOpen(RE::TweenMenu::MENU_NAME) ||
		ui->IsMenuOpen(RE::Console::MENU_NAME)) {
		menuMode = true;
	}
	if (ui->IsMenuOpen("dmenu") ||
		ui->IsMenuOpen("dmenu_Main") ||
		ui->IsMenuOpen("dMenu") ||
		ui->IsMenuOpen("dMenu_Main")) {
		dmenuOpen = true;
	}
}

static Controls::KeyId VirtualKeyToDIK(UINT vk)
{
	const UINT scan = MapVirtualKey(vk, MAPVK_VK_TO_VSC_EX);
	if (scan == 0) {
		return 0;
	}
	const bool extended = (scan & 0xE000U) != 0;
	const UINT sc = (scan & 0xFFU);
	if (sc == 0) {
		return 0;
	}
	Controls::KeyId dik = static_cast<Controls::KeyId>(sc);
	if (extended) {
		dik |= 0x80U;
	}
	return dik;
}

static void BuildRebindKeyState(std::array<std::uint8_t, 256>& keys)
{
	keys.fill(0);
	for (UINT vk = 0; vk < 256; ++vk) {
		switch (vk) {
		case VK_LBUTTON:
		case VK_RBUTTON:
		case VK_MBUTTON:
		case VK_XBUTTON1:
		case VK_XBUTTON2:
			continue;
		default:
			break;
		}
		const SHORT state = GetAsyncKeyState(static_cast<int>(vk));
		if ((state & 0x8000) == 0) {
			continue;
		}
		const Controls::KeyId dik = VirtualKeyToDIK(vk);
		if (dik > 0 && dik < keys.size()) {
			keys[dik] = 1;
		}
	}
}

static bool IsCancelMkbKey(Controls::KeyId key)
{
	return key == DIK_ESCAPE || key == DIK_HOME;
}

void Controls::BeginRebind(RebindTarget target)
{
	// Guard: if already waiting for this target, don't reset state
	// (dMenu sends callback every frame while button is held)
	if (s_rebindTarget == target) {
		return;
	}
	
	s_rebindTarget = target;
	s_rebindStart = std::chrono::steady_clock::now();
	s_rebindIgnoreUntil = s_rebindStart + kRebindIgnoreWindow;
	s_rebindWarnedTimeout = false;
	s_rebindWaitingAllUp = true;
	s_rebindLoggedGrace = false;
	s_rebindLoggedAllUp = false;
	s_rebindPrevMkb.fill(0);
	if (ShouldLogRebind()) {
		bool menuMode = false;
		bool dmenuOpen = false;
		GetRebindMenuFlags(menuMode, dmenuOpen);
		logger::info("[Controls] Rebind started: {} (menuMode={}, dmenuOpen={})",
			GetRebindTargetName(target), menuMode, dmenuOpen);
	}
}

void Controls::CancelRebind()
{
	if (s_rebindTarget != RebindTarget::None) {
		if (ShouldLogRebind()) {
			logger::info("[Controls] Rebind cancelled: {}", GetRebindTargetName(s_rebindTarget));
		}
	}
	s_rebindTarget = RebindTarget::None;
	s_rebindWaitingAllUp = false;
}

bool Controls::IsRebindActive()
{
	return s_rebindTarget != RebindTarget::None;
}

Controls::RebindTarget Controls::GetRebindTarget()
{
	return s_rebindTarget;
}

void Controls::UpdateRebindTimeout()
{
	if (s_rebindTarget == RebindTarget::None) {
		return;
	}
	const auto now = std::chrono::steady_clock::now();
	if (!s_rebindWarnedTimeout && now - s_rebindStart > kRebindTimeout) {
		if (ShouldLogRebind()) {
			logger::warn("[Controls] Rebind timed out ({}).", GetRebindTargetName(s_rebindTarget));
		}
		s_rebindWarnedTimeout = true;
		CancelRebind();
	}
}

void Controls::PollRebindInput()
{
	// Handle all MKB-based targets (keyboard polling via GetAsyncKeyState)
	const bool isMkbTarget = (s_rebindTarget == RebindTarget::AmmoWheelMKB ||
	                          s_rebindTarget == RebindTarget::AmmoWheelMKBModifier);
	if (!isMkbTarget) {
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	if (now < s_rebindIgnoreUntil) {
		if (ShouldLogRebind() && !s_rebindLoggedGrace) {
			const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(s_rebindIgnoreUntil - now);
			logger::info("[Controls] Rebind waiting: grace={}ms", remaining.count());
			s_rebindLoggedGrace = true;
		}
		return;
	}

	std::array<std::uint8_t, 256> current{};
	BuildRebindKeyState(current);

	if (s_rebindWaitingAllUp) {
		bool anyDown = false;
		for (std::uint8_t state : current) {
			if (state) {
				anyDown = true;
				break;
			}
		}
		if (anyDown) {
			if (ShouldLogRebind() && !s_rebindLoggedAllUp) {
				logger::info("[Controls] Rebind waiting: all keys up");
				s_rebindLoggedAllUp = true;
			}
			s_rebindPrevMkb = current;
			return;
		}
		s_rebindWaitingAllUp = false;
		s_rebindPrevMkb = current;
	}

	for (Controls::KeyId key = 1; key < current.size(); ++key) {
		if (current[key] && !s_rebindPrevMkb[key]) {
			if (IsCancelMkbKey(key)) {
				if (ShouldLogRebind()) {
					logger::info("[Controls] Rebind cancelled: {} (reason=CancelKey {})",
						GetRebindTargetName(s_rebindTarget), GetKeyNameForMkb(key));
				}
				CancelRebind();
				s_rebindPrevMkb = current;
				return;
			}

			const bool conflict = IsKeyExclusivelyBound(key);
			if (ShouldLogRebind()) {
				logger::info("[Controls] Rebind detected: DIK_{} (edge=Down){} -> apply",
					GetKeyNameForMkb(key),
					conflict ? " (override)" : "");
			}
			HandleRebindInput(key, false, false);
			s_rebindPrevMkb = current;
			return;
		}
	}

	s_rebindPrevMkb = current;
}

bool Controls::HandleRebindInput(KeyId key, bool isGamePad, bool isMouse)
{
	if (s_rebindTarget == RebindTarget::None) {
		return false;
	}

	// Debug logging for rebind
	if (ShouldLogRebind()) {
		logger::info("[Controls] HandleRebindInput: key={}, isGamePad={}, isMouse={}, target={}",
			key, isGamePad, isMouse, GetRebindTargetName(s_rebindTarget));
	}

	const auto now = std::chrono::steady_clock::now();
	if (now < s_rebindIgnoreUntil) {
		if (ShouldLogRebind()) {
			logger::info("[Controls] Rebind: ignoring input (grace period)");
		}
		return true;  // consume input during ignore window
	}

	const bool wantsGamepad = (s_rebindTarget == RebindTarget::AmmoWheelGamepad || 
	                           s_rebindTarget == RebindTarget::AmmoWheelGamepadModifier);
	const bool wantsMouse = (s_rebindTarget == RebindTarget::AmmoWheelMouse);
	if (!wantsGamepad && !wantsMouse && s_rebindWaitingAllUp) {
		if (ShouldLogRebind()) {
			logger::info("[Controls] Rebind: waiting for all keys up (MKB mode)");
		}
		return true;
	}
	if (wantsGamepad != isGamePad) {
		if (ShouldLogRebind()) {
			logger::info("[Controls] Rebind: wrong device (wantsGamepad={}, got isGamePad={})", wantsGamepad, isGamePad);
		}
		return true;  // consume input while waiting for correct device
	}
	// Mouse rebind accepts mouse input; keyboard rebinds ignore mouse
	if (!wantsGamepad && !wantsMouse && isMouse) {
		if (ShouldLogRebind()) {
			logger::info("[Controls] Rebind: ignoring mouse for keyboard rebind");
		}
		return true;  // ignore mouse for keyboard keybind capture
	}

	if (key == 0 || key == static_cast<KeyId>(-1)) {
		if (ShouldLogRebind()) {
			logger::info("[Controls] Rebind: invalid key value ({})", key);
		}
		return true;
	}

	if (!wantsGamepad && IsCancelMkbKey(key)) {
		if (ShouldLogRebind()) {
			logger::info("[Controls] Rebind cancelled: {} (reason=CancelKey {})",
				GetRebindTargetName(s_rebindTarget), GetKeyNameForMkb(key));
		}
		CancelRebind();
		return true;
	}

	if (ShouldLogRebind()) {
		std::string keyName = wantsGamepad ? GetKeyNameForGamepad(key) : GetKeyNameForMkb(key);
		logger::info("[Controls] Rebind APPLYING: target={}, key={} ({})", 
			GetRebindTargetName(s_rebindTarget), key, keyName);
	}

	switch (s_rebindTarget) {
	case RebindTarget::AmmoWheelMKB:
		Config::AmmoWheel::MKB::toggleAmmoWheel = key;
		Config::AmmoWheel::ToggleKeyMKBName = GetKeyNameForMkb(key);
		Config::WriteAmmoWheelKeybindOverrides();
		Utils::NotificationMessage(std::format("AmmoWheel toggle key: {}", Config::AmmoWheel::ToggleKeyMKBName));
		break;
	case RebindTarget::AmmoWheelGamepad:
		Config::AmmoWheel::GamePad::toggleAmmoWheel = key;
		Config::AmmoWheel::ToggleKeyGamepadName = GetKeyNameForGamepad(key);
		Config::WriteAmmoWheelKeybindOverrides();
		Utils::NotificationMessage(std::format("AmmoWheel gamepad toggle: {}", Config::AmmoWheel::ToggleKeyGamepadName));
		break;
	case RebindTarget::AmmoWheelMKBModifier:
		Config::AmmoWheel::MKB::modifierKey = key;
		Config::AmmoWheel::ModifierKeyMKBName = GetKeyNameForMkb(key);
		Config::WriteAmmoWheelKeybindOverrides();
		Utils::NotificationMessage(std::format("AmmoWheel modifier key: {}", Config::AmmoWheel::ModifierKeyMKBName));
		break;
	case RebindTarget::AmmoWheelGamepadModifier:
		Config::AmmoWheel::GamePad::modifierButton = key;
		Config::AmmoWheel::ModifierButtonGamepadName = GetKeyNameForGamepad(key);
		Config::WriteAmmoWheelKeybindOverrides();
		Utils::NotificationMessage(std::format("AmmoWheel gamepad modifier: {}", Config::AmmoWheel::ModifierButtonGamepadName));
		break;
	case RebindTarget::AmmoWheelMouse:
		Config::AmmoWheel::MKB::toggleAmmoWheelMouse = key;
		Config::AmmoWheel::ToggleMouseButtonName = GetKeyNameForMkb(key);
		Config::WriteAmmoWheelKeybindOverrides();
		Utils::NotificationMessage(std::format("AmmoWheel mouse toggle: {}", Config::AmmoWheel::ToggleMouseButtonName));
		break;
	default:
		break;
	}

	BindAllInputsFromConfig();
	if (ShouldLogRebind() && wantsGamepad) {
		logger::info("[Controls] Rebind detected: {} (edge=Down) -> apply", GetKeyNameForGamepad(key));
	}
	CancelRebind();
	return true;
}

std::string Controls::GetKeyNameForMkb(KeyId key)
{
	if (key == 0) {
		return "Unbound";
	}
	if (key >= KEY_MOUSE_OFFSET && key <= KEY_MOUSE_OFFSET + 4) {
		switch (key) {
		case KEY_MOUSE_OFFSET:
			return "Mouse Left";
		case KEY_MOUSE_OFFSET + 1:
			return "Mouse Right";
		case KEY_MOUSE_OFFSET + 2:
			return "Mouse Middle";
		case KEY_MOUSE_OFFSET + 3:
			return "Mouse X1";
		case KEY_MOUSE_OFFSET + 4:
			return "Mouse X2";
		default:
			break;
		}
	}
	if (key == KEY_MOUSE_WHEEL_UP) {
		return "Mouse Wheel Up";
	}
	if (key == KEY_MOUSE_WHEEL_DOWN) {
		return "Mouse Wheel Down";
	}

	static const std::unordered_map<KeyId, const char*> kKeyNames = {
		{ DIK_ESCAPE, "Esc" },
		{ DIK_TAB, "Tab" },
		{ DIK_RETURN, "Enter" },
		{ DIK_SPACE, "Space" },
		{ DIK_BACK, "Backspace" },
		{ DIK_CAPITAL, "Caps Lock" },
		{ DIK_LSHIFT, "Left Shift" },
		{ DIK_RSHIFT, "Right Shift" },
		{ DIK_LCONTROL, "Left Ctrl" },
		{ DIK_RCONTROL, "Right Ctrl" },
		{ DIK_LMENU, "Left Alt" },
		{ DIK_RMENU, "Right Alt" },
		{ DIK_LWIN, "Left Win" },
		{ DIK_RWIN, "Right Win" },
		{ DIK_APPS, "Menu" },
		{ DIK_INSERT, "Insert" },
		{ DIK_DELETE, "Delete" },
		{ DIK_HOME, "Home" },
		{ DIK_END, "End" },
		{ DIK_PRIOR, "Page Up" },
		{ DIK_NEXT, "Page Down" },
		{ DIK_UP, "Up" },
		{ DIK_DOWN, "Down" },
		{ DIK_LEFT, "Left" },
		{ DIK_RIGHT, "Right" },
		{ DIK_GRAVE, "`" },
		{ DIK_MINUS, "-" },
		{ DIK_EQUALS, "=" },
		{ DIK_LBRACKET, "[" },
		{ DIK_RBRACKET, "]" },
		{ DIK_BACKSLASH, "\\" },
		{ DIK_SEMICOLON, ";" },
		{ DIK_APOSTROPHE, "'" },
		{ DIK_COMMA, "," },
		{ DIK_PERIOD, "." },
		{ DIK_SLASH, "/" },
		{ DIK_1, "1" },
		{ DIK_2, "2" },
		{ DIK_3, "3" },
		{ DIK_4, "4" },
		{ DIK_5, "5" },
		{ DIK_6, "6" },
		{ DIK_7, "7" },
		{ DIK_8, "8" },
		{ DIK_9, "9" },
		{ DIK_0, "0" },
		{ DIK_F1, "F1" },
		{ DIK_F2, "F2" },
		{ DIK_F3, "F3" },
		{ DIK_F4, "F4" },
		{ DIK_F5, "F5" },
		{ DIK_F6, "F6" },
		{ DIK_F7, "F7" },
		{ DIK_F8, "F8" },
		{ DIK_F9, "F9" },
		{ DIK_F10, "F10" },
		{ DIK_F11, "F11" },
		{ DIK_F12, "F12" },
		{ DIK_A, "A" },
		{ DIK_B, "B" },
		{ DIK_C, "C" },
		{ DIK_D, "D" },
		{ DIK_E, "E" },
		{ DIK_F, "F" },
		{ DIK_G, "G" },
		{ DIK_H, "H" },
		{ DIK_I, "I" },
		{ DIK_J, "J" },
		{ DIK_K, "K" },
		{ DIK_L, "L" },
		{ DIK_M, "M" },
		{ DIK_N, "N" },
		{ DIK_O, "O" },
		{ DIK_P, "P" },
		{ DIK_Q, "Q" },
		{ DIK_R, "R" },
		{ DIK_S, "S" },
		{ DIK_T, "T" },
		{ DIK_U, "U" },
		{ DIK_V, "V" },
		{ DIK_W, "W" },
		{ DIK_X, "X" },
		{ DIK_Y, "Y" },
		{ DIK_Z, "Z" }
	};

	auto it = kKeyNames.find(key);
	if (it != kKeyNames.end()) {
		return it->second;
	}

	return "Key " + std::to_string(key);
}

std::string Controls::GetKeyNameForGamepad(KeyId key)
{
	if (key == 0) {
		return "Unbound";
	}
	if (key < KEY_GAMEPAD_OFFSET) {
		return "Gamepad " + std::to_string(key);
	}

	static const std::array<const char*, 16> kGamepadNames = {
		"D-Pad Up",
		"D-Pad Down",
		"D-Pad Left",
		"D-Pad Right",
		"Start",
		"Back",
		"Left Stick",
		"Right Stick",
		"Left Shoulder",
		"Right Shoulder",
		"A",
		"B",
		"X",
		"Y",
		"Left Trigger",
		"Right Trigger"
	};

	const std::size_t index = static_cast<std::size_t>(key - KEY_GAMEPAD_OFFSET);
	if (index < kGamepadNames.size()) {
		return kGamepadNames[index];
	}
	return "Gamepad " + std::to_string(key);
}

