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

static enum : std::uint32_t
{
	kInvalid = static_cast<std::uint32_t>(-1),
	kKeyboardOffset = 0,
	kMouseOffset = 256,
	kGamepadOffset = 266
};

static constexpr std::uint32_t kGamepadLeftTriggerMapped = kGamepadOffset + 14;
static constexpr std::uint32_t kGamepadRightTriggerMapped = kGamepadOffset + 15;
static constexpr std::uint32_t kGamepadBMapped = kGamepadOffset + 11;
static constexpr float kTriggerPressThreshold = 0.55f;
static constexpr float kTriggerReleaseThreshold = 0.35f;

static std::array<bool, 2> s_triggerPressed{};
static std::unordered_set<std::uint32_t> s_ammoWheelSuppressedGamepadReleaseInputs;

static void SuppressAmmoWheelGamepadInputUntilRelease(std::uint32_t input)
{
	if (input != kInvalid) {
		s_ammoWheelSuppressedGamepadReleaseInputs.insert(input);
	}
}

static bool ConsumeSuppressedAmmoWheelGamepadInput(
	std::uint32_t input,
	bool isDown,
	bool isUp,
	const char*& spyCandidates,
	const char*& spyWinner,
	const char*& spyResult)
{
	if (input == kInvalid || (!isDown && !isUp)) {
		return false;
	}

	auto it = s_ammoWheelSuppressedGamepadReleaseInputs.find(input);
	if (it == s_ammoWheelSuppressedGamepadReleaseInputs.end()) {
		return false;
	}

	spyCandidates = "AmmoWheelReleaseGuard";
	spyWinner = "AmmoWheel";
	if (isUp) {
		s_ammoWheelSuppressedGamepadReleaseInputs.erase(it);
		spyResult = "ConsumedPostCloseRelease";
	} else {
		spyResult = "ConsumedPostCloseHold";
	}
	return true;
}

static std::uint32_t MakeButtonEdgeKey(RE::INPUT_DEVICE device, std::uint32_t rawInput)
{
	return (static_cast<std::uint32_t>(device) << 24) | (rawInput & 0x00FFFFFF);
}

static bool IsButtonDownEdge(RE::INPUT_DEVICE device, std::uint32_t rawInput, bool isDown, bool isUp)
{
	static std::unordered_set<std::uint32_t> s_downButtons;
	const std::uint32_t key = MakeButtonEdgeKey(device, rawInput);

	if (isUp) {
		s_downButtons.erase(key);
		return false;
	}

	if (!isDown) {
		return false;
	}

	const auto [_, inserted] = s_downButtons.insert(key);
	return inserted;
}

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

static bool IsMovementUserEventName(std::string_view userEventName)
{
	return userEventName == "Forward" ||
	       userEventName == "Back" ||
	       userEventName == "Strafe Left" ||
	       userEventName == "Strafe Right" ||
	       userEventName == "Move" ||
	       userEventName == "Run" ||
	       userEventName == "Sprint" ||
	       userEventName == "Jump" ||
	       userEventName == "Auto-Move";
}

static bool IsPauseUserEventName(std::string_view userEventName)
{
	return userEventName == "Pause" || userEventName == "Journal";
}

static bool IsTweenMenuOpen()
{
	auto* ui = RE::UI::GetSingleton();
	return ui && ui->IsMenuOpen(RE::TweenMenu::MENU_NAME);
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
	appendMenu(RE::ContainerMenu::MENU_NAME, "Container");
	appendMenu(RE::MagicMenu::MENU_NAME, "Magic");
	appendMenu(RE::FavoritesMenu::MENU_NAME, "Favorites");
	appendMenu("LootMenu", "LootMenu");
	appendMenu("LootMenuCF", "LootMenuCF");
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

struct MenuActivationContextEntry
{
	std::string_view menuName;
	std::string_view tag;
};

static bool IsMenuActivationContextOpen(RE::UI* ui, std::string* outContext = nullptr)
{
	static constexpr std::array<MenuActivationContextEntry, 6> activationMenus{ {
		{ RE::InventoryMenu::MENU_NAME, "InventoryMenu" },
		{ RE::ContainerMenu::MENU_NAME, "ContainerMenu" },
		{ RE::MagicMenu::MENU_NAME, "MagicMenu" },
		{ RE::FavoritesMenu::MENU_NAME, "FavoritesMenu" },
		{ "LootMenu", "LootMenu" },
		{ "LootMenuCF", "LootMenuCF" }
	} };

	if (outContext) {
		outContext->clear();
	}
	if (!ui) {
		if (outContext) {
			*outContext = "NoUI";
		}
		return false;
	}

	bool anyOpen = false;
	for (const auto& menu : activationMenus) {
		if (!ui->IsMenuOpen(menu.menuName)) {
			continue;
		}

		anyOpen = true;
		if (outContext) {
			if (!outContext->empty()) {
				outContext->append("+");
			}
			outContext->append(menu.tag.data(), menu.tag.size());
		}
	}

	if (outContext && outContext->empty()) {
		*outContext = "Gameplay";
	}
	return anyOpen;
}

// Must remain user-event based; physical gamepad buttons are user-rebindable.
static bool IsMenuActivationUserEvent(std::string_view userEventName)
{
	return userEventName == "Activate" || userEventName == "Accept";
}

static void LogInventoryGamepadUserEventTrace(
	RE::UI* ui,
	const std::string& userEventName,
	std::uint32_t rawInput,
	std::uint32_t mappedInput,
	bool isDown,
	bool isUp)
{
	if (!Config::Debug::InputSpy || !ui || !ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) {
		return;
	}

	logger::info(
		"[InputSpy] InventoryMenu gamepad userEvent={} raw={} mapped={} down={} up={}",
		userEventName.empty() ? "<none>" : userEventName.c_str(),
		rawInput,
		mappedInput,
		isDown ? 1 : 0,
		isUp ? 1 : 0);
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

static bool IsEditHintsToggleKey(std::uint32_t input, bool isGamePad, bool isMouse)
{
	if (isMouse || input == 0) {
		return false;
	}
	if (isGamePad) {
		return Config::InputBindings::GamePad::toggleEditHints != 0 &&
		       input == Config::InputBindings::GamePad::toggleEditHints;
	}
	return Config::InputBindings::MKB::toggleEditHints != 0 &&
	       input == Config::InputBindings::MKB::toggleEditHints;
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

	const bool mainWheelWasOpenAtDispatchStart = Wheeler::IsWheelerOpen();
	bool mainWheelOpenedDuringThisDispatch = false;
	bool mainWheelClosedDuringThisDispatch = false;
	bool keptPassthroughEvent = false;
	bool sawPauseThisDispatch = false;

	std::unordered_set<std::uint32_t> ammoWheelDispatchSuppressedInputs;
	std::unordered_set<std::string> ammoWheelDispatchSuppressedUserEvents;
	const auto suppressAmmoWheelDispatchTail = [&](std::uint32_t input, std::string_view userEventName) {
		if (input != kInvalid) {
			ammoWheelDispatchSuppressedInputs.insert(input);
		}
		if (!userEventName.empty()) {
			ammoWheelDispatchSuppressedUserEvents.emplace(userEventName);
		}
	};
	const auto consumeAmmoWheelDispatchTailSuppression = [&](
		std::uint32_t input,
		std::string_view userEventName,
		bool isDown,
		bool isUp,
		const char*& spyCandidates,
		const char*& spyWinner,
		const char*& spyResult) {
		if (!isDown && !isUp) {
			return false;
		}

		bool matched = false;
		if (input != kInvalid) {
			auto inputIt = ammoWheelDispatchSuppressedInputs.find(input);
			if (inputIt != ammoWheelDispatchSuppressedInputs.end()) {
				ammoWheelDispatchSuppressedInputs.erase(inputIt);
				matched = true;
			}
		}

		if (!userEventName.empty()) {
			auto userEventIt = ammoWheelDispatchSuppressedUserEvents.find(std::string(userEventName));
			if (userEventIt != ammoWheelDispatchSuppressedUserEvents.end()) {
				ammoWheelDispatchSuppressedUserEvents.erase(userEventIt);
				matched = true;
			}
		}

		if (!matched) {
			return false;
		}

		spyCandidates = "AmmoWheelDispatchTailGuard";
		spyWinner = "AmmoWheel";
		spyResult = "ConsumedPostCloseAlias";
		return true;
	};

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
		const bool mainWheelOpenBeforeEvent = Wheeler::IsWheelerOpen();
		bool passthroughThisEvent = false;

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
			} else if (wheelerOpen && !brokerOwnerBlocked) {
				passthroughThisEvent = true;
				spyDevice = RE::INPUT_DEVICE::kGamepad;
				spyAnalogValue = (std::max)((std::abs)(thumbstick->xValue), (std::abs)(thumbstick->yValue));
				spyCandidates = "MainWheelThumbstick";
				spyWinner = "Vanilla";
				spyResult = "PassthroughMovementStick";
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
				const bool isDownEdge = IsButtonDownEdge(device, button->GetIDCode(), isDown, isUp);
				spyIsDown = isDown;
				spyIsUp = isUp;
				spyAnalogValue = analogValue;

				std::string userEventName;
				if (input != kInvalid) {
					RE::ControlMap* ctrlMap = RE::ControlMap::GetSingleton();
					if (ctrlMap) {
						const auto userEvent = ctrlMap->GetUserEventName(input, device);
						if (!userEvent.empty()) {
							userEventName = userEvent;
						}
					}
				}

				RE::UI* ui = RE::UI::GetSingleton();
				if (isGamePad && input != kInvalid && (isDown || isUp)) {
					LogInventoryGamepadUserEventTrace(ui, userEventName, spyRawInput, input, isDown, isUp);
				}

				if (input != kInvalid && (isDown || isUp)) {
					Controls::TrackKeyState(input, isDown, isGamePad);
				}

				if (!consumeEvent &&
				    isGamePad &&
				    ConsumeSuppressedAmmoWheelGamepadInput(
					    input,
					    isDown,
					    isUp,
					    spyCandidates,
					    spyWinner,
					    spyResult)) {
					consumeEvent = true;
				}
				if (!consumeEvent &&
				    isGamePad &&
				    consumeAmmoWheelDispatchTailSuppression(
					    input,
					    userEventName,
					    isDown,
					    isUp,
					    spyCandidates,
					    spyWinner,
					    spyResult)) {
					consumeEvent = true;
				}

				bool brokerAllowsWheelProcessing = true;
				if (!consumeEvent && input != kInvalid && (isDown || isUp)) {
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
				if (!consumeEvent && mainWheelAction != Wheeler::InputAction::None && (isDown || isUp)) {
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

				Controls::Action resolvedAction = Controls::Action::None;
				bool suppressRepeatedActivationDown = false;
				if (!consumeEvent && input != kInvalid && brokerAllowsWheelProcessing) {
					resolvedAction = Controls::ResolveAction(input, isDown, isGamePad);
					suppressRepeatedActivationDown =
						isDown &&
						!isUp &&
						!isDownEdge &&
						(resolvedAction == Controls::Action::ActivatePrimary ||
						 resolvedAction == Controls::Action::ActivateSecondary);
					if (resolvedAction == Controls::Action::ExitWheel &&
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
					const bool isAmmoToggleKey = IsAmmoToggleKey(input, isGamePad, isMouse);
					const bool wheelerOpen = Wheeler::IsWheelerOpen();
					const bool ammoWheelOpen = Wheeler::IsAmmoWheelOpen();
					const bool suppressEditHintsToggleBinding = Config::MainWheel::EditHints::Enabled &&
					                                           !Wheeler::IsInEditMode() &&
					                                           IsEditHintsToggleKey(input, isGamePad, isMouse) &&
					                                           !Controls::HasBridgeWheelBinding(input, isGamePad);
					const bool isKeyBound = !suppressEditHintsToggleBinding && Controls::IsKeyBound(input);
					const bool mainWheelMovementPassthrough =
						wheelerOpen &&
						!isKeyBound &&
						!userEventName.empty() &&
						IsMovementUserEventName(userEventName);
					const bool mainWheelPausePassthrough =
						wheelerOpen &&
						!userEventName.empty() &&
						IsPauseUserEventName(userEventName);
					if (mainWheelMovementPassthrough || mainWheelPausePassthrough) {
						passthroughThisEvent = true;
						if (mainWheelPausePassthrough && isDown) {
							sawPauseThisDispatch = true;
						}
					}
					bool controlsDispatched = false;
					bool stateChangedByDispatch = false;

					// Cooperative Input Compatibility Matrix:
					// 1) Open wheel contexts own their relevant inputs.
					// 2) Closed context routes through Controls with explicit consume/pass-through.
					// 3) Chord mismatch must not consume.
					auto runControlsDispatch = [&]() {
						const bool dispatchDown = suppressRepeatedActivationDown ? false : isDown;
						if (controlsDispatched || !isKeyBound || !(dispatchDown || isUp)) {
							return;
						}
						const bool beforeMain = Wheeler::IsWheelerOpen();
						const bool beforeAmmo = Wheeler::IsAmmoWheelOpen();
						spyDispatchResult = Controls::Dispatch(input, dispatchDown, isGamePad);
						controlsDispatched = true;
						const bool afterMain = Wheeler::IsWheelerOpen();
						const bool afterAmmo = Wheeler::IsAmmoWheelOpen();
						stateChangedByDispatch = (beforeMain != afterMain) || (beforeAmmo != afterAmmo);
					};

					if (wheelerOpen) {
						spyCandidates = "MainWheel+Controls";
						if (!consumeEvent) {
							if (isKeyBound || isAmmoToggleKey) {
								runControlsDispatch();
								if (spyDispatchResult == Controls::DispatchResult::Consumed ||
								    spyDispatchResult == Controls::DispatchResult::HandledPassThrough ||
								    stateChangedByDispatch) {
									consumeEvent = true;
									spyWinner = "MainWheel";
									spyResult = controlsDispatched ? "MainWheelOwnedBound" : "MainWheelOwned";
								} else {
									spyWinner = "Vanilla";
									spyResult = "PassThroughUnclaimed";
								}
							} else {
								bool routedToConfirm = false;
								std::string activationMenuContext;
								if (!Controls::IsRebindActive() &&
								    IsMenuActivationContextOpen(ui, &activationMenuContext) &&
								    IsMenuActivationUserEvent(userEventName)) {
									consumeEvent = true;
									if (isDown) {
										Wheeler::OnConfirmDown();
										routedToConfirm = true;
									}
									if (isUp) {
										Wheeler::OnConfirmUp();
										routedToConfirm = true;
									}
									spyWinner = "MainWheel";
									spyResult = routedToConfirm ? "MenuActivateFallbackConfirm" : "MenuActivateFiltered";
									if (Config::Debug::LogMenuBlockReasons || Config::Debug::InputSpy) {
										logger::info(
											"[InputMenuGuard] menuContext={} userEvent={} isKeyBound={} consumed={} routedToConfirm={}",
											activationMenuContext,
											userEventName.empty() ? "<none>" : userEventName.c_str(),
											isKeyBound ? 1 : 0,
											consumeEvent ? 1 : 0,
											routedToConfirm ? 1 : 0);
									}
								}

								if (!consumeEvent &&
								    !userEventName.empty() &&
								    Input::EventsToFilterWhenWheelerActive.contains(userEventName)) {
									consumeEvent = true;
									spyWinner = "MainWheel";
									spyResult = "FilteredUserEvent";
								}
							}
						}
					} else if (ammoWheelOpen) {
						spyCandidates = "AmmoWheel+Controls";
						const auto maybeSuppressPostHandleGamepadAliases = [&]() {
							if (!isGamePad || !consumeEvent) {
								return;
							}
							suppressAmmoWheelDispatchTail(input, userEventName);
							if (isDown && !Wheeler::IsAmmoWheelOpen()) {
								SuppressAmmoWheelGamepadInputUntilRelease(input);
							}
						};

						if (isGamePad || !isMouse) {
							// AmmoWheel should honor the currently bound exit/activation actions instead of
							// depending only on legacy raw button aliases.
							const auto ammoWheelAction = Controls::ResolveAction(input, isDown, isGamePad);
							if (ammoWheelAction == Controls::Action::ExitWheel && isDown) {
								Wheeler::ToggleAmmoWheel();
								consumeEvent = true;
								maybeSuppressPostHandleGamepadAliases();
								spyWinner = "AmmoWheel";
								spyResult = "AmmoExit";
							} else if ((ammoWheelAction == Controls::Action::ActivatePrimary ||
								        ammoWheelAction == Controls::Action::ActivateSecondary) &&
							           (isDown || isUp)) {
								const int mappedButton = (ammoWheelAction == Controls::Action::ActivatePrimary) ? 0 : 1;
								if (Wheeler::HandleAmmoWheelMouseButton(mappedButton, isDown, true)) {
									consumeEvent = true;
									maybeSuppressPostHandleGamepadAliases();
									spyWinner = "AmmoWheel";
									spyResult = (mappedButton == 0) ? "AmmoGamepadBoundPrimary" : "AmmoGamepadBoundSecondary";
								}
							}
						}

						if (!consumeEvent && device == DeviceType::kMouse) {
							const std::uint32_t rawButton = button->GetIDCode();
							if ((rawButton == 0 || rawButton == 1) && (isDown || isUp)) {
								if (Wheeler::HandleAmmoWheelMouseButton(static_cast<int>(rawButton), isDown, false)) {
									consumeEvent = true;
									spyWinner = "AmmoWheel";
									spyResult = "AmmoMouseAction";
								}
							}
						} else if (!consumeEvent && device == DeviceType::kGamepad) {
							const std::uint32_t rawButton = button->GetIDCode();
							if ((rawButton == 15 || rawButton == 14) && (isDown || isUp)) {
								const int mappedButton = (rawButton == 15) ? 0 : 1;
								if (Wheeler::HandleAmmoWheelMouseButton(mappedButton, isDown, true)) {
									consumeEvent = true;
									maybeSuppressPostHandleGamepadAliases();
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
						if (!isMouse &&
						    isDown &&
						    Wheeler::TryTriggerActionHotkeysBridgeCloseAssist(input, isGamePad)) {
							consumeEvent = true;
							spyWinner = "ActionHotkeysBridge";
							spyResult = (isGamePad && input == kGamepadBMapped) ?
								"BridgeCloseAssistGamepadB" :
								"BridgeCloseAssistEsc";
						} else {
							runControlsDispatch();
						}
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

		if (Wheeler::IsWheelerOpen() && !passthroughThisEvent && !consumeEvent) {
			consumeEvent = true;
			spyCandidates = "MainWheelInputLock";
			spyWinner = "MainWheel";
			spyResult = "ConsumedHardLock";
		}
		if (passthroughThisEvent && !consumeEvent) {
			keptPassthroughEvent = true;
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

		const bool mainWheelOpenAfterEvent = Wheeler::IsWheelerOpen();
		if (!mainWheelOpenBeforeEvent && mainWheelOpenAfterEvent) {
			mainWheelOpenedDuringThisDispatch = true;
		} else if (mainWheelOpenBeforeEvent && !mainWheelOpenAfterEvent) {
			mainWheelClosedDuringThisDispatch = true;
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

	const bool dispatchExclusiveRequested = mainWheelWasOpenAtDispatchStart || mainWheelOpenedDuringThisDispatch;
	const bool suppressExclusiveForPassthrough = keptPassthroughEvent || IsTweenMenuOpen() || sawPauseThisDispatch;
	const bool dispatchExclusive =
		dispatchExclusiveRequested &&
		!suppressExclusiveForPassthrough &&
		!InputBroker::IsBlockedByActiveOwner(InputBroker::kWheelerRefinedPluginId);
	if (dispatchExclusive) {
		*a_event = nullptr;
	}

	static bool s_hasLoggedMainWheelExclusiveState = false;
	static bool s_lastMainWheelDispatchExclusive = false;
	static bool s_lastMainWheelExclusiveRequested = false;
	static bool s_lastMainWheelOpenedDuringDispatch = false;
	static bool s_lastMainWheelClosedDuringDispatch = false;
	static bool s_lastMainWheelKeptPassthrough = false;
	static bool s_lastMainWheelSawPause = false;
	if (dispatchExclusiveRequested &&
	    (!s_hasLoggedMainWheelExclusiveState ||
	     dispatchExclusive != s_lastMainWheelDispatchExclusive ||
	     dispatchExclusiveRequested != s_lastMainWheelExclusiveRequested ||
	     mainWheelOpenedDuringThisDispatch != s_lastMainWheelOpenedDuringDispatch ||
	     mainWheelClosedDuringThisDispatch != s_lastMainWheelClosedDuringDispatch ||
	     keptPassthroughEvent != s_lastMainWheelKeptPassthrough ||
	     sawPauseThisDispatch != s_lastMainWheelSawPause)) {
		logger::info(
			"[MainWheel.InputLock] dispatchExclusive={} openedDuring={} closedDuring={} keptPassthrough={} pausePassthrough={}",
			dispatchExclusive ? 1 : 0,
			mainWheelOpenedDuringThisDispatch ? 1 : 0,
			mainWheelClosedDuringThisDispatch ? 1 : 0,
			keptPassthroughEvent ? 1 : 0,
			sawPauseThisDispatch ? 1 : 0);
		s_hasLoggedMainWheelExclusiveState = true;
		s_lastMainWheelDispatchExclusive = dispatchExclusive;
		s_lastMainWheelExclusiveRequested = dispatchExclusiveRequested;
		s_lastMainWheelOpenedDuringDispatch = mainWheelOpenedDuringThisDispatch;
		s_lastMainWheelClosedDuringDispatch = mainWheelClosedDuringThisDispatch;
		s_lastMainWheelKeptPassthrough = keptPassthroughEvent;
		s_lastMainWheelSawPause = sawPauseThisDispatch;
	}
}
