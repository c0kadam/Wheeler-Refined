#include "Input.h"

#include <WinUser.h>
#include <Windows.h>
#include <dinput.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <sstream>
#include <string>
#include <string_view>

#include "bin/Config.h"
#include "bin/InputBroker.h"
#include "bin/InitState.h"
#include "bin/Rendering/ResolutionScaleContext.h"
#include "bin/Utilities/Utils.h"
#include "bin/Wheeler/Wheeler.h"
#include "Controls.h"

class CharEvent : public RE::InputEvent
{
public:
	uint32_t keyCode;  // 18 (ascii code)
};


static enum : std::uint32_t
{
	kInvalid = static_cast<std::uint32_t>(-1),
	kKeyboardOffset = 0,
	kMouseOffset = 256,
	kGamepadOffset = 266
};

static constexpr std::uint32_t kGamepadLeftTriggerMapped = kGamepadOffset + 14;
static constexpr std::uint32_t kGamepadRightTriggerMapped = kGamepadOffset + 15;
static constexpr float kTriggerPressThreshold = 0.55f;
static constexpr float kTriggerReleaseThreshold = 0.35f;

static std::array<bool, 2> s_triggerPressed{};

static inline std::uint32_t GetGamepadIndex(RE::BSWin32GamepadDevice::Key a_key)
{
	using Key = RE::BSWin32GamepadDevice::Key;

	std::uint32_t index;
	switch (a_key) {
	case Key::kUp:
		index = 0;
		break;
	case Key::kDown:
		index = 1;
		break;
	case Key::kLeft:
		index = 2;
		break;
	case Key::kRight:
		index = 3;
		break;
	case Key::kStart:
		index = 4;
		break;
	case Key::kBack:
		index = 5;
		break;
	case Key::kLeftThumb:
		index = 6;
		break;
	case Key::kRightThumb:
		index = 7;
		break;
	case Key::kLeftShoulder:
		index = 8;
		break;
	case Key::kRightShoulder:
		index = 9;
		break;
	case Key::kA:
		index = 10;
		break;
	case Key::kB:
		index = 11;
		break;
	case Key::kX:
		index = 12;
		break;
	case Key::kY:
		index = 13;
		break;
	case Key::kLeftTrigger:
		index = 14;
		break;
	case Key::kRightTrigger:
		index = 15;
		break;
	default:
		index = kInvalid;
		break;
	}

	return index != kInvalid ? index + kGamepadOffset : kInvalid;
}

static bool IsAmmoToggleKey(std::uint32_t input, bool isGamePad, bool isMouse)
{
	if (!Config::AmmoWheel::Enabled) {
		return false;
	}
	if (isGamePad) {
		return Config::AmmoWheel::GamePad::toggleAmmoWheel != 0 &&
		       input == Config::AmmoWheel::GamePad::toggleAmmoWheel;
	}
	if (isMouse) {
		return Config::AmmoWheel::MKB::toggleAmmoWheelMouse != 0 &&
		       input == Config::AmmoWheel::MKB::toggleAmmoWheelMouse;
	}
	return Config::AmmoWheel::MKB::toggleAmmoWheel != 0 &&
	       input == Config::AmmoWheel::MKB::toggleAmmoWheel;
}

static bool IsAmmoWheelMenuHoldContextOpen()
{
	auto* ui = RE::UI::GetSingleton();
	if (!ui) {
		return false;
	}
	return ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME);
}

static bool IsAmmoWheelChordSatisfied(bool isGamePad)
{
	if (isGamePad) {
		const std::uint32_t modifier = Config::AmmoWheel::GamePad::modifierButton;
		return modifier == 0 || Controls::IsGamepadKeyHeld(modifier);
	}
	const std::uint32_t modifier = Config::AmmoWheel::MKB::modifierKey;
	return modifier == 0 || Controls::IsMkbKeyHeld(modifier);
}

static bool ShouldBypassWheelerInputInterception()
{
	auto* ui = RE::UI::GetSingleton();
	return ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
}

static const char* ToBool(bool value)
{
	return value ? "1" : "0";
}

static const char* GetInputEventTypeName(RE::INPUT_EVENT_TYPE type)
{
	switch (type) {
	case RE::INPUT_EVENT_TYPE::kButton:
		return "ButtonEvent";
	case RE::INPUT_EVENT_TYPE::kMouseMove:
		return "MouseMoveEvent";
	case RE::INPUT_EVENT_TYPE::kThumbstick:
		return "ThumbstickEvent";
	case RE::INPUT_EVENT_TYPE::kChar:
		return "CharEvent";
	case RE::INPUT_EVENT_TYPE::kDeviceConnect:
		return "DeviceConnectEvent";
	case RE::INPUT_EVENT_TYPE::kKinect:
		return "KinectEvent";
	default:
		return "UnknownEvent";
	}
}

static const char* GetInputDeviceName(RE::INPUT_DEVICE device)
{
	switch (device) {
	case RE::INPUT_DEVICE::kKeyboard:
		return "Keyboard";
	case RE::INPUT_DEVICE::kMouse:
		return "Mouse";
	case RE::INPUT_DEVICE::kGamepad:
		return "Gamepad";
	default:
		return "UnknownDevice";
	}
}

static const char* GetDispatchResultName(Controls::DispatchResult result)
{
	switch (result) {
	case Controls::DispatchResult::NotHandled:
		return "NotHandled";
	case Controls::DispatchResult::HandledPassThrough:
		return "HandledPassThrough";
	case Controls::DispatchResult::Consumed:
		return "Consumed";
	default:
		return "Unknown";
	}
}

struct InputSpyEntry
{
	double timestamp = 0.0;
	RE::INPUT_EVENT_TYPE eventType = RE::INPUT_EVENT_TYPE::kButton;
	RE::INPUT_DEVICE device = RE::INPUT_DEVICE::kKeyboard;
	std::uint32_t rawCode = 0;
	std::uint32_t mappedCode = 0;
	bool isDown = false;
	bool isUp = false;
	float analogValue = 0.0f;
	bool wheelerOpen = false;
	bool ammoWheelOpen = false;
	bool editMode = false;
	std::string menuContext;
	std::string candidates;
	std::string winner;
	std::string dispatchResult;
	std::string result;
	bool consumed = false;
};

static std::deque<InputSpyEntry> s_inputSpyRing;
static std::chrono::steady_clock::time_point s_inputSpyLastRealtimeLog{};

static std::string GetMenuContextTag()
{
	auto* ui = RE::UI::GetSingleton();
	if (!ui) {
		return "NoUI";
	}

	std::ostringstream oss;
	auto appendMenu = [&](std::string_view name, const char* tag) {
		if (!ui->IsMenuOpen(name)) {
			return;
		}
		if (oss.tellp() > 0) {
			oss << "+";
		}
		oss << tag;
	};

	appendMenu(RE::MainMenu::MENU_NAME, "MainMenu");
	appendMenu(RE::TweenMenu::MENU_NAME, "TweenMenu");
	appendMenu(RE::Console::MENU_NAME, "Console");
	appendMenu(RE::InventoryMenu::MENU_NAME, "Inventory");
	appendMenu(RE::FavoritesMenu::MENU_NAME, "Favorites");
	appendMenu("dmenu", "dmenu");
	appendMenu("dmenu_Main", "dmenu_Main");
	appendMenu("dMenu", "dMenu");
	appendMenu("dMenu_Main", "dMenu_Main");

	const std::string tags = oss.str();
	if (tags.empty()) {
		return "Gameplay";
	}
	return tags;
}

static bool IsInputSpyEnabled()
{
	return Config::Debug::InputSpy;
}

static void InputSpyPush(InputSpyEntry&& entry)
{
	if (!IsInputSpyEnabled()) {
		return;
	}

	const std::size_t capacity = static_cast<std::size_t>((std::max)(32u, Config::Debug::InputSpyRingBuffer));
	while (s_inputSpyRing.size() >= capacity) {
		s_inputSpyRing.pop_front();
	}
	s_inputSpyRing.push_back(std::move(entry));

	const std::uint32_t rateLimitMs = Config::Debug::InputSpyRateLimitMs;
	const auto now = std::chrono::steady_clock::now();
	if (rateLimitMs == 0 ||
	    s_inputSpyLastRealtimeLog.time_since_epoch().count() == 0 ||
	    now - s_inputSpyLastRealtimeLog >= std::chrono::milliseconds(rateLimitMs)) {
		const auto& evt = s_inputSpyRing.back();
		logger::info(
			"[InputSpy] type={} dev={} raw={} mapped={} down={} up={} value={:.3f} mainOpen={} ammoOpen={} editMode={} menu={} candidates={} winner={} dispatch={} result={} consumed={}",
			GetInputEventTypeName(evt.eventType),
			GetInputDeviceName(evt.device),
			evt.rawCode,
			evt.mappedCode,
			ToBool(evt.isDown),
			ToBool(evt.isUp),
			evt.analogValue,
			ToBool(evt.wheelerOpen),
			ToBool(evt.ammoWheelOpen),
			ToBool(evt.editMode),
			evt.menuContext,
			evt.candidates,
			evt.winner,
			evt.dispatchResult,
			evt.result,
			ToBool(evt.consumed));
		s_inputSpyLastRealtimeLog = now;
	}
}

static void DumpInputSpyRingBuffer()
{
	if (!IsInputSpyEnabled()) {
		return;
	}

	const std::filesystem::path outPath = std::filesystem::path("logs") / "input_spy_dump.txt";
	std::error_code ec;
	std::filesystem::create_directories(outPath.parent_path(), ec);

	std::ofstream out(outPath, std::ios::trunc);
	if (!out.is_open()) {
		logger::warn("[InputSpy] Failed to open dump file '{}'", outPath.string());
		return;
	}

	out << "Wheeler InputSpy Dump\n";
	out << "entries=" << s_inputSpyRing.size() << "\n\n";
	for (const auto& evt : s_inputSpyRing) {
		out << "ts=" << evt.timestamp
			<< " type=" << GetInputEventTypeName(evt.eventType)
			<< " dev=" << GetInputDeviceName(evt.device)
			<< " raw=" << evt.rawCode
			<< " mapped=" << evt.mappedCode
			<< " down=" << (evt.isDown ? 1 : 0)
			<< " up=" << (evt.isUp ? 1 : 0)
			<< " value=" << evt.analogValue
			<< " mainOpen=" << (evt.wheelerOpen ? 1 : 0)
			<< " ammoOpen=" << (evt.ammoWheelOpen ? 1 : 0)
			<< " editMode=" << (evt.editMode ? 1 : 0)
			<< " menu=" << evt.menuContext
			<< " candidates=" << evt.candidates
			<< " winner=" << evt.winner
			<< " dispatch=" << evt.dispatchResult
			<< " result=" << evt.result
			<< " consumed=" << (evt.consumed ? 1 : 0)
			<< "\n";
	}

	out.flush();
	logger::info("[InputSpy] Dumped {} events to {}", s_inputSpyRing.size(), outPath.string());
	Utils::NotificationMessage("Wheeler: InputSpy dump written to logs/input_spy_dump.txt");
}

static bool TryGetGamepadTriggerIndex(std::uint32_t mappedInput, std::size_t& outIndex)
{
	if (mappedInput == kGamepadLeftTriggerMapped) {
		outIndex = 0;
		return true;
	}
	if (mappedInput == kGamepadRightTriggerMapped) {
		outIndex = 1;
		return true;
	}
	return false;
}

static float GetButtonAnalogValue(const RE::ButtonEvent* button)
{
	if (!button) {
		return 0.0f;
	}
	return button->value;
}

static void NormalizeGamepadTriggerEdges(const RE::ButtonEvent* button, std::uint32_t mappedInput, bool& isDown, bool& isUp, float& analogValue)
{
	analogValue = GetButtonAnalogValue(button);

	std::size_t triggerIndex = 0;
	if (!TryGetGamepadTriggerIndex(mappedInput, triggerIndex)) {
		return;
	}

	if (isDown || isUp) {
		s_triggerPressed[triggerIndex] = isDown;
		return;
	}

	const bool wasPressed = s_triggerPressed[triggerIndex];
	const bool nowPressed = wasPressed ? analogValue > kTriggerReleaseThreshold : analogValue >= kTriggerPressThreshold;
	if (nowPressed != wasPressed) {
		isDown = nowPressed;
		isUp = !nowPressed;
		s_triggerPressed[triggerIndex] = nowPressed;
	}
}

void Input::ProcessAndFilter(RE::InputEvent** a_event)
{
	if (!a_event) {
		return;
	}
	if (!InitState::IsCoreInitialized() || !InitState::IsDataInitialized()) {
		return;
	}
	if (ShouldBypassWheelerInputInterception()) {
		return;
	}

	auto& resolutionContext = ResolutionScale::Context::GetSingleton();
	resolutionContext.Update();
	Controls::UpdateRebindTimeout();
	Controls::PollRebindInput();
	InputBroker::RefreshConfigFromSettings();
	InputBroker::RefreshWheelerReservations();

	RE::InputEvent* event = *a_event;
	RE::InputEvent* prev = nullptr;
	while (event != nullptr) {
		bool consumeEvent = false;
		RE::INPUT_DEVICE spyDevice = RE::INPUT_DEVICE::kKeyboard;
		std::uint32_t spyRawInput = 0;
		std::uint32_t spyMappedInput = 0;
		bool spyIsDown = false;
		bool spyIsUp = false;
		float spyAnalogValue = 0.0f;
		const char* spyCandidates = "Vanilla";
		const char* spyWinner = "Vanilla";
		const char* spyResult = "PassThrough";
		Controls::DispatchResult spyDispatchResult = Controls::DispatchResult::NotHandled;
		const bool brokerOwnerBlocked = InputBroker::IsBlockedByActiveOwner(InputBroker::kWheelerRefinedPluginId);

		if (event->eventType == RE::INPUT_EVENT_TYPE::kMouseMove) {
			const bool wheelerOpen = Wheeler::IsWheelerOpen();
			const bool ammoWheelOpen = Wheeler::IsAmmoWheelOpen();
			if (wheelerOpen && !brokerOwnerBlocked) {
				RE::MouseMoveEvent* mouseMove = static_cast<RE::MouseMoveEvent*>(event);
				const float deltaX = resolutionContext.ToGameX(mouseMove->mouseInputX);
				const float deltaY = resolutionContext.ToGameY(mouseMove->mouseInputY);
				Wheeler::UpdateCursorPosMouse(deltaX, deltaY);
				consumeEvent = true;
				spyCandidates = "MainWheelCursor";
				spyWinner = "MainWheel";
				spyResult = "ConsumedCursor";
			} else if (ammoWheelOpen && !brokerOwnerBlocked) {
				RE::MouseMoveEvent* mouseMove = static_cast<RE::MouseMoveEvent*>(event);
				const float deltaX = resolutionContext.ToGameX(mouseMove->mouseInputX);
				const float deltaY = resolutionContext.ToGameY(mouseMove->mouseInputY);
				Wheeler::UpdateAmmoWheelCursorPosMouse(deltaX, deltaY);
				consumeEvent = true;
				spyCandidates = "AmmoWheelCursor";
				spyWinner = "AmmoWheel";
				spyResult = "ConsumedCursor";
			} else if ((wheelerOpen || ammoWheelOpen) && brokerOwnerBlocked) {
				spyCandidates = "InputBroker";
				spyWinner = "InputBroker";
				spyResult = "BlockedByOwner";
			}
		} else if (event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
			const bool wheelerOpen = Wheeler::IsWheelerOpen();
			const bool ammoWheelOpen = Wheeler::IsAmmoWheelOpen();
			RE::ThumbstickEvent* thumbstick = static_cast<RE::ThumbstickEvent*>(event);
			if (wheelerOpen && !brokerOwnerBlocked && thumbstick->IsRight()) {
				Wheeler::UpdateCursorPosGamepad(thumbstick->xValue, thumbstick->yValue);
				consumeEvent = true;
				spyDevice = RE::INPUT_DEVICE::kGamepad;
				spyAnalogValue = (std::max)((std::abs)(thumbstick->xValue), (std::abs)(thumbstick->yValue));
				spyCandidates = "MainWheelThumbstick";
				spyWinner = "MainWheel";
				spyResult = "ConsumedCursor";
			} else if (ammoWheelOpen && !brokerOwnerBlocked && thumbstick->IsRight()) {
				Wheeler::UpdateAmmoWheelCursorPosGamepad(thumbstick->xValue, thumbstick->yValue);
				consumeEvent = true;
				spyDevice = RE::INPUT_DEVICE::kGamepad;
				spyAnalogValue = (std::max)((std::abs)(thumbstick->xValue), (std::abs)(thumbstick->yValue));
				spyCandidates = "AmmoWheelThumbstick";
				spyWinner = "AmmoWheel";
				spyResult = "ConsumedCursor";
			} else if ((wheelerOpen || ammoWheelOpen) && brokerOwnerBlocked) {
				spyCandidates = "InputBroker";
				spyWinner = "InputBroker";
				spyResult = "BlockedByOwner";
			}
		} else if (event->eventType == RE::INPUT_EVENT_TYPE::kButton) {
			const auto button = static_cast<RE::ButtonEvent*>(event);
			if (button) {
				spyRawInput = button->GetIDCode();
				std::uint32_t input = spyRawInput;
				using DeviceType = RE::INPUT_DEVICE;
				bool isGamePad = false;
				bool isMouse = false;
				const RE::INPUT_DEVICE device = button->device.get();
				spyDevice = device;
				switch (device) {
				case DeviceType::kMouse:
					input += kMouseOffset;
					isMouse = true;
					break;
				case DeviceType::kKeyboard:
					input += kKeyboardOffset;
					break;
				case DeviceType::kGamepad:
					input = GetGamepadIndex(static_cast<RE::BSWin32GamepadDevice::Key>(input));
					isGamePad = true;
					break;
				default:
					break;
				}
				spyMappedInput = input;

				bool isDown = button->IsDown();
				bool isUp = button->IsUp();
				float analogValue = GetButtonAnalogValue(button);
				if (isGamePad && input != kInvalid) {
					NormalizeGamepadTriggerEdges(button, input, isDown, isUp, analogValue);
				}
				spyIsDown = isDown;
				spyIsUp = isUp;
				spyAnalogValue = analogValue;

				if (input != kInvalid && (isDown || isUp)) {
					Controls::TrackKeyState(input, isDown, isGamePad);
				}

				bool brokerAllowsWheelProcessing = true;
				if (input != kInvalid && (isDown || isUp)) {
					std::uint32_t brokerContextFlags = InputBroker::ContextFlag::None;
					if (isDown) {
						brokerContextFlags |= InputBroker::ContextFlag::IsDown;
					}
					if (isUp) {
						brokerContextFlags |= InputBroker::ContextFlag::IsUp;
					}
					if (Wheeler::IsWheelerOpen()) {
						brokerContextFlags |= InputBroker::ContextFlag::MainWheelOpen;
					}
					if (Wheeler::IsAmmoWheelOpen()) {
						brokerContextFlags |= InputBroker::ContextFlag::AmmoWheelOpen;
					}
					const auto brokerDevice = isGamePad ? InputBroker::DeviceType::kGamepad : InputBroker::DeviceType::kMKB;
					brokerAllowsWheelProcessing = InputBroker::ShouldProcessKey(
						InputBroker::kWheelerRefinedPluginId,
						brokerDevice,
						input,
						brokerContextFlags);
				}

				const auto mainWheelAction = (input != kInvalid) ? Wheeler::ResolveMainWheelInputAction(input, isGamePad) : Wheeler::InputAction::None;
				if (mainWheelAction != Wheeler::InputAction::None && (isDown || isUp)) {
					Wheeler::RecordMainWheelInputEvent(
						mainWheelAction,
						device,
						button->GetIDCode(),
						input,
						isDown,
						isUp);
				}

				if (!consumeEvent &&
				    IsInputSpyEnabled() &&
				    Config::Debug::InputSpyDumpHotkey != 0 &&
				    input == Config::Debug::InputSpyDumpHotkey &&
				    isDown) {
					DumpInputSpyRingBuffer();
					consumeEvent = true;
					spyCandidates = "InputSpyDumpHotkey";
					spyWinner = "Debug";
					spyResult = "DumpTriggered";
				}

				if (!consumeEvent && input != kInvalid) {
					const bool consumedRebind = Controls::HandleRebindInput(input, isGamePad, isMouse);
					if (consumedRebind) {
						if (mainWheelAction != Wheeler::InputAction::None) {
							Wheeler::LogMainWheelInputDecision(Wheeler::InputDecision::DeniedConsumed,
								Wheeler::InputConsumer::DMenu);
						}
						consumeEvent = true;
						spyCandidates = "DMenuRebind";
						spyWinner = "DMenuRebind";
						spyResult = "Consumed";
					}
				}

				if (!consumeEvent && input != kInvalid && !brokerAllowsWheelProcessing) {
					spyCandidates = "InputBroker";
					spyWinner = "InputBroker";
					spyResult = "Blocked";
				}

				if (!consumeEvent && input != kInvalid && brokerAllowsWheelProcessing) {
					const auto resolvedAction = Controls::ResolveAction(input, isDown, isGamePad);
					if (resolvedAction == Controls::Action::ExitWheel &&
						isGamePad &&
						isDown &&
						Wheeler::IsWheelerOpen() &&
						!Wheeler::IsAmmoWheelOpen()) {
						Wheeler::RecordMainWheelInputEvent(
							Wheeler::InputAction::ExitWheel,
							device,
							button->GetIDCode(),
							input,
							true,
							false);
						Wheeler::TryCloseWheeler();
						consumeEvent = true;
						spyCandidates = "MainWheelExit";
						spyWinner = "MainWheel";
						spyResult = "ConsumedExit";
					}
				}

				if (!consumeEvent && input != kInvalid && brokerAllowsWheelProcessing) {
					bool skipControlsDispatch = false;
					const bool isAmmoToggleKey = IsAmmoToggleKey(input, isGamePad, isMouse);
					const bool wheelerOpenBefore = Wheeler::IsWheelerOpen();
					const bool ammoWheelOpenBefore = Wheeler::IsAmmoWheelOpen();

					const bool ammoMenuHoldContext =
						!wheelerOpenBefore &&
						!ammoWheelOpenBefore &&
						isAmmoToggleKey &&
						Config::AmmoWheel::InputSafeguards::MenuHoldToOpenEnabled &&
						IsAmmoWheelMenuHoldContextOpen();
					const bool ammoMenuHoldActive = ammoMenuHoldContext && IsAmmoWheelChordSatisfied(isGamePad);

					if (isUp && Wheeler::IsAmmoWheelMenuHoldArmed(input, isGamePad)) {
						const bool fired = Wheeler::DisarmAmmoWheelMenuHold(input, isGamePad);
						if (!fired) {
							skipControlsDispatch = true;
							spyCandidates = "AmmoMenuHold";
							spyWinner = "AmmoWheelHoldGate";
							spyResult = "ReleasedWithoutFire";
						}
					}
					if (ammoMenuHoldActive && isDown) {
						Wheeler::ArmAmmoWheelMenuHold(input, isGamePad);
						skipControlsDispatch = true;
						spyCandidates = "AmmoMenuHold";
						spyWinner = "AmmoWheelHoldGate";
						spyResult = "Armed";
					}

					const bool wheelerOpen = Wheeler::IsWheelerOpen();
					const bool ammoWheelOpen = Wheeler::IsAmmoWheelOpen();
					const bool isKeyBound = Controls::IsKeyBound(input);
					bool controlsDispatched = false;
					bool stateChangedByDispatch = false;

					// Cooperative Input Compatibility Matrix:
					// 1) Open wheel contexts own their relevant inputs.
					// 2) Closed context routes through Controls with explicit consume/pass-through.
					// 3) Chord mismatch must not consume.
					auto runControlsDispatch = [&]() {
						if (controlsDispatched || skipControlsDispatch || !isKeyBound || !(isDown || isUp)) {
							return;
						}
						const bool beforeMain = Wheeler::IsWheelerOpen();
						const bool beforeAmmo = Wheeler::IsAmmoWheelOpen();
						spyDispatchResult = Controls::Dispatch(input, isDown, isGamePad);
						controlsDispatched = true;
						const bool afterMain = Wheeler::IsWheelerOpen();
						const bool afterAmmo = Wheeler::IsAmmoWheelOpen();
						stateChangedByDispatch = (beforeMain != afterMain) || (beforeAmmo != afterAmmo);
					};

					if (wheelerOpen) {
						spyCandidates = "MainWheel+Controls";
						if (!Controls::IsRebindActive() && !isGamePad && !isMouse && (isDown || isUp)) {
							if (input == DIK_ESCAPE || input == DIK_TAB) {
								if (isDown) {
									Wheeler::TryCloseWheeler();
								}
								consumeEvent = true;
								spyWinner = "MainWheel";
								spyResult = "ConsumedCloseHotkey";
							}
						}

						if (!consumeEvent) {
							if (isKeyBound || isAmmoToggleKey) {
								runControlsDispatch();
								consumeEvent = true;
								spyWinner = "MainWheel";
								spyResult = controlsDispatched ? "MainWheelOwnedBound" : "MainWheelOwned";
							} else {
								RE::ControlMap* ctrlMap = RE::ControlMap::GetSingleton();
								if (ctrlMap) {
									const auto userEvent = ctrlMap->GetUserEventName(input, device);
									if (!userEvent.empty() &&
										Input::EventsToFilterWhenWheelerActive.contains(std::string(userEvent))) {
										consumeEvent = true;
										spyWinner = "MainWheel";
										spyResult = "FilteredUserEvent";
									}
								}
							}
						}
					} else if (ammoWheelOpen) {
						spyCandidates = "AmmoWheel+Controls";

						if (device == DeviceType::kMouse) {
							const std::uint32_t rawButton = button->GetIDCode();
							if ((rawButton == 0 || rawButton == 1) && (isDown || isUp)) {
								if (Wheeler::HandleAmmoWheelMouseButton(static_cast<int>(rawButton), isDown)) {
									consumeEvent = true;
									spyWinner = "AmmoWheel";
									spyResult = "AmmoMouseAction";
								}
							}
						} else if (device == DeviceType::kGamepad) {
							const std::uint32_t rawButton = button->GetIDCode();
							if ((rawButton == 15 || rawButton == 14) && (isDown || isUp)) {
								const int mappedButton = (rawButton == 15) ? 0 : 1;
								if (Wheeler::HandleAmmoWheelMouseButton(mappedButton, isDown)) {
									consumeEvent = true;
									spyWinner = "AmmoWheel";
									spyResult = "AmmoGamepadAttackBlock";
								}
							}
						}

						if (!consumeEvent) {
							runControlsDispatch();
							if (spyDispatchResult == Controls::DispatchResult::Consumed ||
								spyDispatchResult == Controls::DispatchResult::HandledPassThrough ||
								stateChangedByDispatch) {
								consumeEvent = true;
								spyWinner = "AmmoWheel";
								spyResult = "AmmoWheelOwnedBound";
							}
						}
					} else {
						spyCandidates = "Controls+Vanilla";
						runControlsDispatch();
						if (spyDispatchResult == Controls::DispatchResult::Consumed) {
							consumeEvent = true;
							spyWinner = "Controls";
							spyResult = "ConsumedByChordOrPolicy";
						} else if (isAmmoToggleKey && stateChangedByDispatch) {
							consumeEvent = true;
							spyWinner = "AmmoWheel";
							spyResult = "AmmoToggleStateChanged";
						}
					}
				}
			}
		}

		if (IsInputSpyEnabled()) {
			InputSpyEntry spyEntry{};
			spyEntry.timestamp = ImGui::GetTime();
			spyEntry.eventType = static_cast<RE::INPUT_EVENT_TYPE>(event->eventType.get());
			spyEntry.device = spyDevice;
			spyEntry.rawCode = spyRawInput;
			spyEntry.mappedCode = spyMappedInput;
			spyEntry.isDown = spyIsDown;
			spyEntry.isUp = spyIsUp;
			spyEntry.analogValue = spyAnalogValue;
			spyEntry.wheelerOpen = Wheeler::IsWheelerOpen();
			spyEntry.ammoWheelOpen = Wheeler::IsAmmoWheelOpen();
			spyEntry.editMode = Wheeler::IsInEditMode();
			spyEntry.menuContext = GetMenuContextTag();
			spyEntry.candidates = spyCandidates;
			spyEntry.winner = spyWinner;
			spyEntry.dispatchResult = GetDispatchResultName(spyDispatchResult);
			spyEntry.result = spyResult;
			spyEntry.consumed = consumeEvent;
			InputSpyPush(std::move(spyEntry));
		}

		RE::InputEvent* nextEvent = event->next;
		if (consumeEvent) {
			if (prev != nullptr) {
				prev->next = nextEvent;
			} else {
				*a_event = nextEvent;
			}
		} else {
			prev = event;
		}
		event = nextEvent;
	}
}
