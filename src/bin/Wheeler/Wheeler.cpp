#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>

#include "Wheel.h"
#include "Wheeler.h"
#include "TransformWheelManager.h"
#include "bin/Utilities/ActorVirtualCompat.h"
#include <RE/B/BookMenu.h>


#include "imgui.h"
#include "imgui_internal.h"

#include "bin/API/WheelerAPI.h"
#include "bin/Integrations/OStimIntegration.h"
#include "bin/Rendering/Drawer.h"
#include "bin/Rendering/ResolutionScaleContext.h"
#include "bin/Rendering/TextureManager.h"
#include "bin/Integrations/ActionHotkeysBridge.h"
#include "bin/Utilities/InventorySnapshotCache.h"
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
#include "WheelItems/WheelItemIngredient.h"
#include "WheelItems/WheelItemMisc.h"
#include "WheelItems/WheelItemShout.h"
#include "WheelItems/WheelItemMissing.h"
#include "WheelItems/WheelItemFactory.h"
#include "ShoutUtils.h"

namespace
{
	constexpr double kMountedMomentumAssistSeconds = 1.00;
	constexpr float kMountedMinimumSlowScale = 0.35f;
	constexpr float kShoutVoiceSelectionRestoreDelaySec = 1.25f;

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

	bool IsPhysicalEscDown()
	{
		return (::GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
	}

	std::unordered_set<std::string> CaptureOpenMenuNames(RE::UI* ui)
	{
		std::unordered_set<std::string> openMenus;
		if (!ui) {
			return openMenus;
		}

		for (const auto& [menuKey, menuEntry] : ui->menuMap) {
			const auto* menu = menuEntry.menu.get();
			if (!menu || !menu->OnStack()) {
				continue;
			}
			const char* rawName = menuKey.c_str();
			if (rawName && rawName[0] != '\0') {
				openMenus.emplace(rawName);
			}
		}

		return openMenus;
	}

	std::unordered_set<std::string> CaptureNewMenuNamesSinceBaseline(
		RE::UI* ui,
		const std::unordered_set<std::string>& baselineMenus)
	{
		auto openMenus = CaptureOpenMenuNames(ui);
		for (const auto& menuName : baselineMenus) {
			openMenus.erase(menuName);
		}
		return openMenus;
	}

	bool AnyTrackedMenuClosed(RE::UI* ui, const std::unordered_set<std::string>& trackedMenus)
	{
		if (trackedMenus.empty()) {
			return false;
		}

		const auto openMenus = CaptureOpenMenuNames(ui);
		for (const auto& menuName : trackedMenus) {
			if (!openMenus.contains(menuName)) {
				return true;
			}
		}
		return false;
	}

	std::string BuildExternalHotkeyLabel(std::uint32_t scanCode, std::uint32_t modifier)
	{
		const std::string keyName = Controls::GetKeyNameForMkb(scanCode);
		if (modifier == 0) {
			return keyName.empty() || keyName == "Unbound" ? fmt::format("0x{:X}", scanCode) : keyName;
		}

		const std::string modifierName = Controls::GetKeyNameForMkb(modifier);
		if (modifierName.empty() || modifierName == "Unbound") {
			return keyName.empty() || keyName == "Unbound" ? fmt::format("0x{:X}", scanCode) : keyName;
		}
		if (keyName.empty() || keyName == "Unbound") {
			return fmt::format("{} + 0x{:X}", modifierName, scanCode);
		}
		return fmt::format("{} + {}", modifierName, keyName);
	}

	bool IsMkbToggleChordConflict(std::uint32_t scanCode, std::uint32_t modifier)
	{
		using namespace Config::InputBindings::MKB;
		const auto matches = [&](std::uint32_t key, std::uint32_t requiredModifier) {
			return key != 0 && key == scanCode && requiredModifier == modifier;
		};

		return matches(toggleWheel, toggleWheelModifier);
	}

	bool BuildScanCodeInput(std::uint32_t directInputCode, bool keyUp, INPUT& outInput)
	{
		if (directInputCode == 0) {
			return false;
		}

		std::uint32_t scanCode = directInputCode;
		DWORD flags = KEYEVENTF_SCANCODE;
		if (scanCode > 0x7Fu) {
			scanCode &= 0x7Fu;
			flags |= KEYEVENTF_EXTENDEDKEY;
		}
		if (keyUp) {
			flags |= KEYEVENTF_KEYUP;
		}

		outInput = {};
		outInput.type = INPUT_KEYBOARD;
		outInput.ki.wVk = static_cast<WORD>(::MapVirtualKeyA(scanCode, MAPVK_VSC_TO_VK_EX));
		outInput.ki.wScan = static_cast<WORD>(scanCode);
		outInput.ki.dwFlags = flags;
		return true;
	}

	bool SendSingleKeyboardInput(const INPUT& input)
	{
		INPUT copy = input;
		return ::SendInput(1, &copy, sizeof(INPUT)) == 1;
	}

	bool DispatchExternalHotkeyNow(std::uint32_t scanCode, std::uint32_t modifier)
	{
		std::array<INPUT, 4> inputs{};
		UINT inputCount = 0;
		if (modifier != 0 && !BuildScanCodeInput(modifier, false, inputs[inputCount])) {
			return false;
		}
		if (modifier != 0) {
			++inputCount;
		}
		if (!BuildScanCodeInput(scanCode, false, inputs[inputCount])) {
			return false;
		}
		++inputCount;
		if (!BuildScanCodeInput(scanCode, true, inputs[inputCount])) {
			return false;
		}
		++inputCount;
		if (modifier != 0) {
			if (!BuildScanCodeInput(modifier, true, inputs[inputCount])) {
				return false;
			}
			++inputCount;
		}

		const auto stagedInputs = inputs;
		std::thread([stagedInputs, inputCount]() {
			static std::mutex s_dispatchMutex;
			std::lock_guard<std::mutex> lock(s_dispatchMutex);

			for (UINT i = 0; i < inputCount; ++i) {
				SendSingleKeyboardInput(stagedInputs[i]);
				if (i + 1 < inputCount) {
					::Sleep(100);
				}
			}
		}).detach();

		return true;
	}

	bool IsBoundWeaponSpell(RE::SpellItem* spell);

	constexpr std::array<std::string_view, 3> kLootOverrideMenus{
		RE::ContainerMenu::MENU_NAME,
		"LootMenu",   // QuickLoot / QuickLoot RE
		"LootMenuCF"  // QuickLoot EE (Container First)
	};

	bool SetMenuMovieVisibility(RE::UI* ui, std::string_view menuName, bool visible)
	{
		if (!ui) {
			return false;
		}
		auto menu = ui->GetMenu(menuName.data());
		if (!menu) {
			return false;
		}
		if (auto* movie = menu->uiMovie.get()) {
			movie->SetVisible(visible);
			return true;
		}
		return false;
	}

	bool IsSpellBlockedByTransformGuard(RE::SpellItem* spell, const char* sourceLabel)
	{
		if (!spell) {
			return false;
		}
		if (!TransformWheelManager::IsSpellActivationBlocked(spell, sourceLabel)) {
			return false;
		}

		logger::info(
			"TransformWheels: blocked spell activation source={} formId={:08X} edid='{}' name='{}'",
			sourceLabel ? sourceLabel : "",
			spell->GetFormID(),
			spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
			spell->GetName() ? spell->GetName() : "");
		return true;
	}

	bool IsShoutBlockedByTransformGuard(RE::TESShout* shout, const char* sourceLabel)
	{
		if (!shout) {
			return false;
		}
		if (!TransformWheelManager::IsShoutActivationBlocked(shout, sourceLabel)) {
			return false;
		}

		logger::info(
			"TransformWheels: blocked shout activation source={} formId={:08X} edid='{}' name='{}'",
			sourceLabel ? sourceLabel : "",
			shout->GetFormID(),
			shout->GetFormEditorID() ? shout->GetFormEditorID() : "",
			shout->GetName() ? shout->GetName() : "");
		return true;
	}

	enum class EditHintDisplayMode : std::uint32_t
	{
		Auto = 0,
		Mkb = 1,
		Gamepad = 2,
		Both = 3
	};

	enum class EditHintGamepadIconSet : std::uint32_t
	{
		Xbox = 0,
		PlayStation = 1
	};

	struct EditHintActionBinding
	{
		const char* label = "";
		struct Binding
		{
			std::uint32_t key = 0;
			std::uint32_t modifier = 0;
		};

		Binding mkb{};
		Binding gamepad{};
	};

	struct EditHintBindingVisual
	{
		struct Segment
		{
			Texture::Image icon{};
			std::string text;
			float width = 0.0f;
		};

		std::vector<Segment> segments;
		float width = 0.0f;
	};

	struct DMenuToggleBindings
	{
		EditHintActionBinding::Binding mkb{};
		EditHintActionBinding::Binding gamepad{};
	};

	constexpr std::size_t kEditHintActionCount = 12;
	constexpr std::uint32_t kGamepadOffset = 266;
	constexpr std::uint32_t kGamepadMax = kGamepadOffset + 15;
	constexpr std::uint32_t kDefaultDMenuToggleMkb = 199;  // DIK_HOME
	constexpr std::uint32_t kDefaultDMenuToggleGamepad = kGamepadOffset + 4;  // Start
	constexpr const char* kDMenuIniPath = R"(Data\SKSE\Plugins\dmenu\dmenu.ini)";

	bool IsGamepadInputCode(std::uint32_t key)
	{
		return key >= kGamepadOffset && key <= kGamepadMax;
	}

	bool TryReadIniUInt32(const CSimpleIniA& ini, const char* section, const char* key, std::uint32_t& value)
	{
		const char* raw = ini.GetValue(section, key);
		if (!raw || !raw[0]) {
			return false;
		}

		char* end = nullptr;
		const auto parsed = std::strtoul(raw, &end, 10);
		if (end == raw) {
			return false;
		}

		while (*end && std::isspace(static_cast<unsigned char>(*end))) {
			++end;
		}
		if (*end != '\0') {
			return false;
		}

		value = static_cast<std::uint32_t>(parsed);
		return true;
	}

	DMenuToggleBindings GetDMenuToggleBindings()
	{
		struct Cache
		{
			bool initialized = false;
			double nextPollTime = 0.0;
			std::filesystem::file_time_type lastWriteTime{};
			DMenuToggleBindings bindings{};
		};

		static Cache cache;

		auto resetDefaults = [&]() {
			cache.bindings.mkb = { kDefaultDMenuToggleMkb, 0 };
			cache.bindings.gamepad = { kDefaultDMenuToggleGamepad, 0 };
		};

		const double now = ImGui::GetTime();
		if (cache.initialized && now < cache.nextPollTime) {
			return cache.bindings;
		}
		cache.nextPollTime = now + 0.5;

		std::error_code ec;
		std::filesystem::file_time_type currentWriteTime{};
		if (std::filesystem::exists(kDMenuIniPath, ec)) {
			currentWriteTime = std::filesystem::last_write_time(kDMenuIniPath, ec);
			if (ec) {
				currentWriteTime = {};
			}
		}

		if (cache.initialized && cache.lastWriteTime == currentWriteTime) {
			return cache.bindings;
		}

		cache.initialized = true;
		cache.lastWriteTime = currentWriteTime;
		resetDefaults();

		if (currentWriteTime == std::filesystem::file_time_type{}) {
			return cache.bindings;
		}

		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(kDMenuIniPath) < 0) {
			return cache.bindings;
		}

		std::uint32_t legacyToggle = 0;
		std::uint32_t legacyModifier = 0;
		std::uint32_t mkbToggle = 0;
		std::uint32_t mkbModifier = 0;
		std::uint32_t gamepadToggle = 0;
		std::uint32_t gamepadModifier = 0;

		const bool hasLegacyToggle = TryReadIniUInt32(ini, "UI", "key_toggle_dmenu", legacyToggle);
		const bool hasLegacyModifier = TryReadIniUInt32(ini, "UI", "key_toggle_modifier", legacyModifier);
		const bool hasMkbToggle = TryReadIniUInt32(ini, "UI", "key_toggle_dmenu_mkb", mkbToggle);
		const bool hasMkbModifier = TryReadIniUInt32(ini, "UI", "key_toggle_modifier_mkb", mkbModifier);
		const bool hasGamepadToggle = TryReadIniUInt32(ini, "UI", "key_toggle_dmenu_gamepad", gamepadToggle);
		const bool hasGamepadModifier = TryReadIniUInt32(ini, "UI", "key_toggle_modifier_gamepad", gamepadModifier);

		if (hasMkbToggle) {
			cache.bindings.mkb.key = mkbToggle;
		}
		if (hasMkbModifier) {
			cache.bindings.mkb.modifier = mkbModifier;
		}
		if (hasGamepadToggle) {
			cache.bindings.gamepad.key = gamepadToggle;
		}
		if (hasGamepadModifier) {
			cache.bindings.gamepad.modifier = gamepadModifier;
		}

		if (hasLegacyToggle) {
			if (IsGamepadInputCode(legacyToggle)) {
				if (!hasGamepadToggle) {
					cache.bindings.gamepad.key = legacyToggle;
				}
			} else if (!hasMkbToggle) {
				cache.bindings.mkb.key = legacyToggle;
			}
		}
		if (hasLegacyModifier) {
			if (IsGamepadInputCode(legacyModifier)) {
				if (!hasGamepadModifier) {
					cache.bindings.gamepad.modifier = legacyModifier;
				}
			} else if (!hasMkbModifier) {
				cache.bindings.mkb.modifier = legacyModifier;
			}
		}

		return cache.bindings;
	}

	std::array<EditHintActionBinding, kEditHintActionCount> BuildEditHintActions()
	{
		const auto dmenuBindings = GetDMenuToggleBindings();
		return {
			EditHintActionBinding{ Texts::GetText(Texts::TextType::EditHintActionUsePlaceItem), { Config::InputBindings::MKB::activatePrimary }, { Config::InputBindings::GamePad::activatePrimary } },
			EditHintActionBinding{ Texts::GetText(Texts::TextType::EditHintActionRemoveItemWheel), { Config::InputBindings::MKB::activateSecondary }, { Config::InputBindings::GamePad::activateSecondary } },
			EditHintActionBinding{ Texts::GetText(Texts::TextType::EditHintActionAddEmptySlot), { Config::InputBindings::MKB::addEmptyEntry }, { Config::InputBindings::GamePad::addEmptyEntry } },
			EditHintActionBinding{ Texts::GetText(Texts::TextType::EditHintActionAddWheel), { Config::InputBindings::MKB::addWheel }, { Config::InputBindings::GamePad::addWheel } },
			EditHintActionBinding{ Texts::GetText(Texts::TextType::EditHintActionNextWheel), { Config::InputBindings::MKB::nextWheel }, { Config::InputBindings::GamePad::nextWheel } },
			EditHintActionBinding{ Texts::GetText(Texts::TextType::EditHintActionPreviousWheel), { Config::InputBindings::MKB::prevWheel }, { Config::InputBindings::GamePad::prevWheel } },
			EditHintActionBinding{ Texts::GetText(Texts::TextType::EditHintActionMoveSlotForward), { Config::InputBindings::MKB::moveEntryForward }, { Config::InputBindings::GamePad::moveEntryForward } },
			EditHintActionBinding{ Texts::GetText(Texts::TextType::EditHintActionMoveSlotBack), { Config::InputBindings::MKB::moveEntryBack }, { Config::InputBindings::GamePad::moveEntryBack } },
			EditHintActionBinding{ Texts::GetText(Texts::TextType::EditHintActionMoveWheelForward), { Config::InputBindings::MKB::moveWheelForward }, { Config::InputBindings::GamePad::moveWheelForward } },
			EditHintActionBinding{ Texts::GetText(Texts::TextType::EditHintActionMoveWheelBack), { Config::InputBindings::MKB::moveWheelBack }, { Config::InputBindings::GamePad::moveWheelBack } },
			EditHintActionBinding{ Texts::GetText(Texts::TextType::EditHintActionSettingsDMenu), dmenuBindings.mkb, dmenuBindings.gamepad },
			EditHintActionBinding{ Texts::GetText(Texts::TextType::EditHintActionExitWheel), { Config::InputBindings::MKB::closeWheel }, { Config::InputBindings::GamePad::exitWheel } }
		};
	}

	std::string GetEditHintBindingName(bool isGamepad, const EditHintActionBinding::Binding& binding);

	std::string BuildEditModeHintsToggleLine(bool isGamepad)
	{
		EditHintActionBinding::Binding binding{};
		binding.key = isGamepad ?
			              Config::InputBindings::GamePad::toggleEditHints :
			              Config::InputBindings::MKB::toggleEditHints;

		const std::string bindingName = GetEditHintBindingName(isGamepad, binding);
		if (bindingName == "Unbound") {
			return {};
		}

		return fmt::format("{} for keybind hints", bindingName);
	}

	std::string MakeKeyAssetPath(const std::string& fileName)
	{
		if (fileName.empty()) {
			return {};
		}
		return std::string(R"(Data\SKSE\Plugins\wheeler\resources\key\)") + fileName;
	}

	std::string ResolveMkbIconFile(const std::string& keyName)
	{
		if (keyName.empty() || keyName == "Unbound") {
			return {};
		}
		if (keyName == "Mouse Left") {
			return "Mouse_Left_Key_Dark.svg";
		}
		if (keyName == "Mouse Right") {
			return "Mouse_Right_Key_Dark.svg";
		}
		if (keyName == "Mouse Middle") {
			return "Mouse_Middle_Key_Dark.svg";
		}
		if (keyName == "Esc") {
			return "Esc_Key_Dark.svg";
		}
		if (keyName == "Tab") {
			return "Tab_Key_Dark.svg";
		}
		if (keyName == "Enter") {
			return "Enter_Key_Dark.svg";
		}
		if (keyName == "Space") {
			return "Space_Key_Dark.svg";
		}
		if (keyName == "Backspace") {
			return "Backspace_Key_Dark.svg";
		}
		if (keyName == "Caps Lock") {
			return "Caps_Lock_Key_Dark.svg";
		}
		if (keyName == "Left Shift" || keyName == "Right Shift") {
			return "Shift_Key_Dark.svg";
		}
		if (keyName == "Left Ctrl" || keyName == "Right Ctrl") {
			return "Ctrl_Key_Dark.svg";
		}
		if (keyName == "Left Alt" || keyName == "Right Alt") {
			return "Alt_Key_Dark.svg";
		}
		if (keyName == "Left Win" || keyName == "Right Win") {
			return "Win_Key_Dark.svg";
		}
		if (keyName == "Insert") {
			return "Insert_Key_Dark.svg";
		}
		if (keyName == "Delete") {
			return "Del_Key_Dark.svg";
		}
		if (keyName == "Home") {
			return "Home_Key_Dark.svg";
		}
		if (keyName == "End") {
			return "End_Key_Dark.svg";
		}
		if (keyName == "Page Up") {
			return "Page_Up_Key_Dark.svg";
		}
		if (keyName == "Page Down") {
			return "Page_Down_Key_Dark.svg";
		}
		if (keyName == "Up") {
			return "Arrow_Up_Key_Dark.svg";
		}
		if (keyName == "Down") {
			return "Arrow_Down_Key_Dark.svg";
		}
		if (keyName == "Left") {
			return "Arrow_Left_Key_Dark.svg";
		}
		if (keyName == "Right") {
			return "Arrow_Right_Key_Dark.svg";
		}
		if (keyName == "-") {
			return "Minus_Key_Dark.svg";
		}
		if (keyName == "=") {
			return "Plus_Key_Dark.svg";
		}
		if (keyName == "[") {
			return "Bracket_Left_Key_Dark.svg";
		}
		if (keyName == "]") {
			return "Bracket_Right_Key_Dark.svg";
		}
		if (keyName == ";") {
			return "Semicolon_Key_Dark.svg";
		}
		if (keyName == "/") {
			return "Slash_Key_Dark.svg";
		}
		if (keyName == "'") {
			return "Mark_Left_Key_Dark.svg";
		}
		if (keyName == "\\") {
			return "Mark_Right_Key_Dark.svg";
		}
		if (keyName.size() == 1) {
			const unsigned char ch = static_cast<unsigned char>(keyName[0]);
			if (std::isalnum(ch)) {
				char upper = static_cast<char>(std::toupper(ch));
				return std::string(1, upper) + "_Key_Dark.svg";
			}
		}
		if (keyName.size() >= 2 && keyName[0] == 'F') {
			bool isFunction = true;
			for (std::size_t i = 1; i < keyName.size(); ++i) {
				if (!std::isdigit(static_cast<unsigned char>(keyName[i]))) {
					isFunction = false;
					break;
				}
			}
			if (isFunction) {
				return keyName + "_Key_Dark.svg";
			}
		}
		return {};
	}

	std::string ResolveGamepadIconFile(const std::uint32_t key, EditHintGamepadIconSet iconSet)
	{
		if (key < kGamepadOffset) {
			return {};
		}
		const std::uint32_t index = key - kGamepadOffset;
		if (iconSet == EditHintGamepadIconSet::PlayStation) {
			switch (index) {
			case 0:
				return "PS5_Dpad_Up.svg";
			case 1:
				return "PS5_Dpad_Down.svg";
			case 2:
				return "PS5_Dpad_Left.svg";
			case 3:
				return "PS5_Dpad_Right.svg";
			case 4:
				return "PS5_Options_Alt.svg";
			case 5:
				return "PS5_Share_Alt.svg";
			case 6:
				return "PS5_Left_Stick_Click.svg";
			case 7:
				return "PS5_Right_Stick_Click.svg";
			case 8:
				return "PS5_L1.svg";
			case 9:
				return "PS5_R1.svg";
			case 10:
				return "PS5_Cross.svg";
			case 11:
				return "PS5_Circle.svg";
			case 12:
				return "PS5_Square.svg";
			case 13:
				return "PS5_Triangle.svg";
			case 14:
				return "PS5_L2.svg";
			case 15:
				return "PS5_R2.svg";
			default:
				return {};
			}
		}

		switch (index) {
		case 0:
			return "XboxSeriesX_Dpad_Up.svg";
		case 1:
			return "XboxSeriesX_Dpad_Down.svg";
		case 2:
			return "XboxSeriesX_Dpad_Left.svg";
		case 3:
			return "XboxSeriesX_Dpad_Right.svg";
		case 4:
			return "XboxSeriesX_Menu.svg";
		case 5:
			return "XboxSeriesX_View.svg";
		case 6:
			return "XboxSeriesX_Left_Stick_Click.svg";
		case 7:
			return "XboxSeriesX_Right_Stick_Click.svg";
		case 8:
			return "XboxSeriesX_LB.svg";
		case 9:
			return "XboxSeriesX_RB.svg";
		case 10:
			return "XboxSeriesX_A.svg";
		case 11:
			return "XboxSeriesX_B.svg";
		case 12:
			return "XboxSeriesX_X.svg";
		case 13:
			return "XboxSeriesX_Y.svg";
		case 14:
			return "XboxSeriesX_LT.svg";
		case 15:
			return "XboxSeriesX_RTsvg.svg";
		default:
			return {};
		}
	}

	std::string GetEditHintBindingName(bool isGamepad, const EditHintActionBinding::Binding& binding)
	{
		const auto getKeyName = [&](std::uint32_t key) {
			return isGamepad ? Controls::GetKeyNameForGamepad(key) : Controls::GetKeyNameForMkb(key);
		};

		const std::string keyName = getKeyName(binding.key);
		if (binding.key == 0 || keyName == "Unbound") {
			return "Unbound";
		}
		if (binding.modifier == 0) {
			return keyName;
		}

		const std::string modifierName = getKeyName(binding.modifier);
		if (modifierName == "Unbound") {
			return keyName;
		}

		return modifierName + " + " + keyName;
	}

	float MeasureEditHintBindingTextWidth(ImFont* font, const std::string& text)
	{
		const float fontSize = Config::MainWheel::EditHints::FontSize;
		const ImVec2 textSize = font ?
			                        font->CalcTextSizeA(fontSize, 10000.0f, 0.0f, text.c_str()) :
			                        ImGui::CalcTextSize(text.c_str());
		return textSize.x;
	}

	float GetEditHintBindingSegmentGap()
	{
		return std::clamp(Config::MainWheel::EditHints::IconSize * 0.2f, 4.0f, 14.0f);
	}

	std::string ResolveEditHintIconFile(bool isGamepad, std::uint32_t key, const std::string& keyName)
	{
		if (key == 0 || keyName.empty() || keyName == "Unbound") {
			return {};
		}
		if (isGamepad) {
			const auto iconSet =
				(Config::MainWheel::EditHints::GamepadIconSet == 1) ? EditHintGamepadIconSet::PlayStation : EditHintGamepadIconSet::Xbox;
			return ResolveGamepadIconFile(key, iconSet);
		}
		return ResolveMkbIconFile(keyName);
	}

	EditHintBindingVisual::Segment BuildEditHintBindingSegment(
		bool isGamepad,
		std::uint32_t key,
		const std::string& text,
		ImFont* font,
		bool allowIcon = true)
	{
		EditHintBindingVisual::Segment segment;
		segment.text = text;

		std::string iconFile;
		if (allowIcon) {
			iconFile = ResolveEditHintIconFile(isGamepad, key, text);
		}
		if (!iconFile.empty()) {
			segment.icon = Texture::GetImageByPath(MakeKeyAssetPath(iconFile));
			if (!segment.icon.texture && iconFile == "XboxSeriesX_RTsvg.svg") {
				segment.icon = Texture::GetImageByPath(MakeKeyAssetPath("XboxSeriesX_RT.svg"));
			}
		}

		const float iconSize = Config::MainWheel::EditHints::IconSize;
		if (segment.icon.texture && segment.icon.width > 0 && segment.icon.height > 0) {
			segment.width = iconSize * (static_cast<float>(segment.icon.width) / static_cast<float>(segment.icon.height));
		} else {
			segment.icon = {};
			segment.width = MeasureEditHintBindingTextWidth(font, segment.text);
		}
		return segment;
	}

	EditHintBindingVisual BuildEditHintBindingVisual(bool isGamepad, const EditHintActionBinding::Binding& binding, ImFont* font)
	{
		EditHintBindingVisual visual;

		const auto getKeyName = [&](std::uint32_t key) {
			return isGamepad ? Controls::GetKeyNameForGamepad(key) : Controls::GetKeyNameForMkb(key);
		};

		const std::string keyName = getKeyName(binding.key);
		if (binding.key == 0 || keyName == "Unbound") {
			visual.segments.push_back(BuildEditHintBindingSegment(isGamepad, 0, "Unbound", font, false));
		} else {
			const std::string modifierName = (binding.modifier != 0) ? getKeyName(binding.modifier) : std::string();
			if (binding.modifier != 0 && !modifierName.empty() && modifierName != "Unbound") {
				visual.segments.push_back(BuildEditHintBindingSegment(isGamepad, binding.modifier, modifierName, font));
				visual.segments.push_back(BuildEditHintBindingSegment(false, 0, "+", font, false));
			}
			visual.segments.push_back(BuildEditHintBindingSegment(isGamepad, binding.key, keyName, font));
		}

		const float segmentGap = GetEditHintBindingSegmentGap();
		for (std::size_t i = 0; i < visual.segments.size(); ++i) {
			visual.width += visual.segments[i].width;
			if (i + 1 < visual.segments.size()) {
				visual.width += segmentGap;
			}
		}
		return visual;
	}

	void DrawEditHintBindingVisual(float x, float rowTop, float rowHeight, const EditHintBindingVisual& visual,
		ImFont* font, const DrawArgs& drawArgs)
	{
		const float iconSize = Config::MainWheel::EditHints::IconSize;
		const float fontSize = Config::MainWheel::EditHints::FontSize;
		const float segmentGap = GetEditHintBindingSegmentGap();
		float cursorX = x;
		for (std::size_t i = 0; i < visual.segments.size(); ++i) {
			const auto& segment = visual.segments[i];
			if (segment.icon.texture && segment.icon.width > 0 && segment.icon.height > 0) {
				Drawer::draw_texture(
					segment.icon.texture,
					ImVec2(cursorX + segment.width * 0.5f, rowTop + rowHeight * 0.5f),
					0.0f,
					0.0f,
					ImVec2(segment.width, iconSize),
					C_SKYRIMWHITE,
					drawArgs);
			} else {
				const ImVec2 textSize = font ?
					                        font->CalcTextSizeA(fontSize, 10000.0f, 0.0f, segment.text.c_str()) :
					                        ImGui::CalcTextSize(segment.text.c_str());
				const float textY = rowTop + (rowHeight - textSize.y) * 0.5f;
				Drawer::draw_text_with_font(cursorX, textY, segment.text.c_str(), Config::MainWheel::EditHints::TextColor,
					font, fontSize, drawArgs, false);
			}
			cursorX += segment.width;
			if (i + 1 < visual.segments.size()) {
				cursorX += segmentGap;
			}
		}
	}

	void DrawEditModeHintsOverlay(const DrawArgs& drawArgs, bool lastInputGamepad)
	{
		if (!Config::MainWheel::EditHints::Enabled) {
			return;
		}

		bool showMkb = false;
		bool showGamepad = false;
		const auto mode = static_cast<EditHintDisplayMode>(Config::MainWheel::EditHints::DisplayMode);
		switch (mode) {
		case EditHintDisplayMode::Mkb:
			showMkb = true;
			break;
		case EditHintDisplayMode::Gamepad:
			showGamepad = true;
			break;
		case EditHintDisplayMode::Both:
			showMkb = true;
			showGamepad = true;
			break;
		case EditHintDisplayMode::Auto:
		default:
			showGamepad = lastInputGamepad;
			showMkb = !showGamepad;
			break;
		}
		if (!showMkb && !showGamepad) {
			showMkb = true;
		}

		ImFont* font = ImGui::GetDefaultFont();
		const auto actions = BuildEditHintActions();

		std::array<EditHintBindingVisual, kEditHintActionCount> mkbVisuals{};
		std::array<EditHintBindingVisual, kEditHintActionCount> gamepadVisuals{};
		float mkbColumnWidth = 0.0f;
		float gamepadColumnWidth = 0.0f;
		float measuredLabelWidth = 0.0f;

		for (std::size_t i = 0; i < actions.size(); ++i) {
			const ImVec2 labelSize = font ?
				                         font->CalcTextSizeA(Config::MainWheel::EditHints::FontSize, 10000.0f, 0.0f, actions[i].label) :
				                         ImGui::CalcTextSize(actions[i].label);
			measuredLabelWidth = (std::max)(measuredLabelWidth, labelSize.x);

			if (showMkb) {
				mkbVisuals[i] = BuildEditHintBindingVisual(false, actions[i].mkb, font);
				mkbColumnWidth = (std::max)(mkbColumnWidth, mkbVisuals[i].width);
			}
			if (showGamepad) {
				gamepadVisuals[i] = BuildEditHintBindingVisual(true, actions[i].gamepad, font);
				gamepadColumnWidth = (std::max)(gamepadColumnWidth, gamepadVisuals[i].width);
			}
		}

		const float padX = Config::MainWheel::EditHints::PanelPaddingX;
		const float padY = Config::MainWheel::EditHints::PanelPaddingY;
		const float keyGap = Config::MainWheel::EditHints::KeyGap;
		const float rowSpacing = Config::MainWheel::EditHints::RowSpacing;
		const float rowHeight = (std::max)(Config::MainWheel::EditHints::IconSize, Config::MainWheel::EditHints::FontSize);
		const float labelWidth = (std::max)(Config::MainWheel::EditHints::LabelWidth, measuredLabelWidth);
		const bool showDeviceHeader = showMkb && showGamepad;

		float panelWidth = padX * 2.0f + labelWidth + keyGap;
		if (showMkb) {
			panelWidth += mkbColumnWidth;
		}
		if (showGamepad) {
			if (showMkb) {
				panelWidth += keyGap;
			}
			panelWidth += gamepadColumnWidth;
		}

		float panelHeight = padY * 2.0f + static_cast<float>(actions.size()) * rowHeight +
		                    static_cast<float>((actions.size() > 0) ? actions.size() - 1 : 0) * rowSpacing;
		if (Config::MainWheel::EditHints::ShowTitle) {
			panelHeight += Config::MainWheel::EditHints::HeaderFontSize + rowSpacing;
		}
		if (showDeviceHeader) {
			panelHeight += Config::MainWheel::EditHints::FontSize + rowSpacing * 0.5f;
		}

		ImVec2 renderSize = ResolutionScale::Context::GetSingleton().GetRenderSize();
		float panelX = Config::MainWheel::EditHints::AnchorX;
		float panelY = Config::MainWheel::EditHints::AnchorY;
		panelX = std::clamp(panelX, 0.0f, (std::max)(0.0f, renderSize.x - panelWidth));
		panelY = std::clamp(panelY, 0.0f, (std::max)(0.0f, renderSize.y - panelHeight));

		auto* drawList = ImGui::GetWindowDrawList();
		if (Config::MainWheel::EditHints::ShowBackground) {
			ImU32 bgColor = Config::MainWheel::EditHints::BackgroundColor;
			Utils::Color::MultAlpha(bgColor, drawArgs.alphaMult);
			drawList->AddRectFilled(ImVec2(panelX, panelY), ImVec2(panelX + panelWidth, panelY + panelHeight), bgColor, 6.0f);
		}

		float cursorY = panelY + padY;
		const float contentX = panelX + padX;
		if (Config::MainWheel::EditHints::ShowTitle) {
			Drawer::draw_text_with_font(contentX, cursorY, Texts::GetText(Texts::TextType::EditHintTitle), Config::MainWheel::EditHints::HeaderColor,
				font, Config::MainWheel::EditHints::HeaderFontSize, drawArgs, false);
			cursorY += Config::MainWheel::EditHints::HeaderFontSize + rowSpacing;
		}

		const float keyColumnX = contentX + labelWidth + keyGap;
		if (showDeviceHeader) {
			Drawer::draw_text_with_font(keyColumnX, cursorY, Texts::GetText(Texts::TextType::EditHintDeviceHeaderMkb), Config::MainWheel::EditHints::HeaderColor,
				font, Config::MainWheel::EditHints::FontSize, drawArgs, false);
			Drawer::draw_text_with_font(keyColumnX + mkbColumnWidth + keyGap, cursorY, Texts::GetText(Texts::TextType::EditHintDeviceHeaderGamepad),
				Config::MainWheel::EditHints::HeaderColor, font, Config::MainWheel::EditHints::FontSize, drawArgs, false);
			cursorY += Config::MainWheel::EditHints::FontSize + rowSpacing * 0.5f;
		}

		for (std::size_t i = 0; i < actions.size(); ++i) {
			const float rowTop = cursorY + static_cast<float>(i) * (rowHeight + rowSpacing);
			const ImVec2 labelSize = font ?
				                         font->CalcTextSizeA(Config::MainWheel::EditHints::FontSize, 10000.0f, 0.0f, actions[i].label) :
				                         ImGui::CalcTextSize(actions[i].label);
			const float labelY = rowTop + (rowHeight - labelSize.y) * 0.5f;
			Drawer::draw_text_with_font(contentX, labelY, actions[i].label, Config::MainWheel::EditHints::TextColor,
				font, Config::MainWheel::EditHints::FontSize, drawArgs, false);

			float colX = keyColumnX;
			if (showMkb) {
				DrawEditHintBindingVisual(colX, rowTop, rowHeight, mkbVisuals[i], font, drawArgs);
				colX += mkbColumnWidth + keyGap;
			}
			if (showGamepad) {
				DrawEditHintBindingVisual(colX, rowTop, rowHeight, gamepadVisuals[i], font, drawArgs);
			}
		}
	}

	void DrawEditModeNavigationText(const DrawArgs& drawArgs)
	{
		if (!Config::MainWheel::EditHints::Enabled) {
			return;
		}

		ImFont* font = ImGui::GetDefaultFont();
		const float fontSize = std::clamp(Config::MainWheel::EditHints::FontSize - 2.0f, 12.0f, 40.0f);
		constexpr float marginX = 24.0f;
		constexpr float marginY = 24.0f;
		constexpr float lineGap = 4.0f;
		const std::string line1 = BuildEditModeHintsToggleLine(false);
		const std::string line2 = BuildEditModeHintsToggleLine(true);
		if (line1.empty() && line2.empty()) {
			return;
		}

		const ImVec2 line1Size =
			line1.empty() ?
				ImVec2(0.0f, 0.0f) :
				(font ? font->CalcTextSizeA(fontSize, 10000.0f, 0.0f, line1.c_str()) : ImGui::CalcTextSize(line1.c_str()));
		const ImVec2 line2Size =
			line2.empty() ?
				ImVec2(0.0f, 0.0f) :
				(font ? font->CalcTextSizeA(fontSize, 10000.0f, 0.0f, line2.c_str()) : ImGui::CalcTextSize(line2.c_str()));
		const float blockWidth = (std::max)(line1Size.x, line2Size.x);
		const float blockHeight =
			(line1.empty() ? 0.0f : line1Size.y) +
			(line2.empty() ? 0.0f : line2Size.y) +
			((!line1.empty() && !line2.empty()) ? lineGap : 0.0f);

		ImVec2 renderSize = ResolutionScale::Context::GetSingleton().GetRenderSize();
		float textX = renderSize.x - marginX - blockWidth;
		float textY = marginY;
		textX = std::clamp(textX, 0.0f, (std::max)(0.0f, renderSize.x - blockWidth));
		textY = std::clamp(textY, 0.0f, (std::max)(0.0f, renderSize.y - blockHeight));

		float cursorY = textY;
		if (!line1.empty()) {
			Drawer::draw_text_with_font(textX, cursorY, line1.c_str(), Config::MainWheel::EditHints::HeaderColor,
				font, fontSize, drawArgs, false);
			cursorY += line1Size.y;
			if (!line2.empty()) {
				cursorY += lineGap;
			}
		}
		if (!line2.empty()) {
			Drawer::draw_text_with_font(textX, cursorY, line2.c_str(), Config::MainWheel::EditHints::HeaderColor,
				font, fontSize, drawArgs, false);
		}
	}

	InventorySnapshotCache g_mainWheelInventorySnapshot;
	constexpr double kMainWheelInventorySnapshotIntervalSeconds = 0.25;

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
		RE::FormID active2HFormID = 0;
		bool restoreArmed = false;
		double restoreStartTime = 0.0;
		bool diagRestoreWaitLogged = false;

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

	static bool IsBowLikeForm(RE::TESForm* form)
	{
		auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
		return weapon && (weapon->IsBow() || weapon->IsCrossbow());
	}

	static bool IsBowLikeFormID(RE::FormID formID)
	{
		return formID != 0 && IsBowLikeForm(RE::TESForm::LookupByID(formID));
	}

	static RE::FormID GetFormIDOrZero(RE::TESForm* form)
	{
		return form ? form->GetFormID() : 0;
	}

	static const char* GetTwoHandedKindName(RE::TESForm* form)
	{
		auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
		if (!weapon) {
			return "none";
		}
		if (weapon->IsBow()) {
			return "bow";
		}
		if (weapon->IsCrossbow()) {
			return "crossbow";
		}
		switch (weapon->GetWeaponType()) {
		case RE::WEAPON_TYPE::kTwoHandSword:
			return "two_hand_sword";
		case RE::WEAPON_TYPE::kTwoHandAxe:
			return "two_hand_axe";
		default:
			return "other_weapon";
		}
	}

	static bool IsTwoHandedFormID(RE::FormID formID)
	{
		if (formID == 0) {
			return false;
		}
		return IsTwoHandedForm(RE::TESForm::LookupByID(formID));
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

	static bool HasInventoryBackedBoundObject(const RE::TESObjectREFR::InventoryItemMap& inv, RE::TESBoundObject* obj)
	{
		if (!obj) {
			return false;
		}

		auto it = inv.find(obj);
		if (it != inv.end() && it->second.first > 0) {
			return true;
		}

		const RE::FormID formID = obj->GetFormID();
		for (auto& [boundObj, data] : inv) {
			if (boundObj && boundObj->GetFormID() == formID && data.first > 0) {
				return true;
			}
		}

		return false;
	}

	static bool HasInventoryBackedBoundObject(RE::PlayerCharacter* pc, RE::TESBoundObject* obj)
	{
		if (!pc || !obj) {
			return false;
		}

		RE::TESObjectREFR::InventoryItemMap inv;
		if (!Utils::Inventory::TryGetInventorySnapshot(pc, inv, "HasInventoryBackedBoundObject")) {
			return false;
		}
		return HasInventoryBackedBoundObject(inv, obj);
	}

	static bool IsTransientBoundWeaponForm(const RE::TESForm* form)
	{
		const auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
		return weapon && weapon->IsBound();
	}

	static RE::FormID NormalizeRestorableHandFormID(
		RE::PlayerCharacter* pc,
		const RE::TESObjectREFR::InventoryItemMap* inventory,
		RE::TESForm* form)
	{
		if (!form) {
			return 0;
		}
		if (auto* spell = form->As<RE::SpellItem>()) {
			return spell->GetFormID();
		}
		if (IsTransientBoundWeaponForm(form)) {
			return 0;
		}
		if (auto* boundObj = form->As<RE::TESBoundObject>()) {
			if (inventory) {
				return HasInventoryBackedBoundObject(*inventory, boundObj) ? boundObj->GetFormID() : 0;
			}
			return HasInventoryBackedBoundObject(pc, boundObj) ? boundObj->GetFormID() : 0;
		}
		return 0;
	}

	static RE::FormID NormalizeRestorableHandFormID(const RE::TESObjectREFR::InventoryItemMap& inventory, RE::TESForm* form)
	{
		return NormalizeRestorableHandFormID(nullptr, &inventory, form);
	}

	static RE::FormID NormalizeRestorableHandFormID(RE::PlayerCharacter* pc, RE::TESForm* form)
	{
		return NormalizeRestorableHandFormID(pc, nullptr, form);
	}

	static RE::FormID NormalizeRestorableHandFormID(RE::PlayerCharacter* pc, RE::FormID formID)
	{
		if (formID == 0) {
			return 0;
		}
		if (auto* form = RE::TESForm::LookupByID(formID)) {
			return NormalizeRestorableHandFormID(pc, form);
		}
		return formID;
	}

	static bool TryGetTrackedPersistentHandRestoreFormIDForHandMemory(
		RE::PlayerCharacter* pc,
		bool isLeft,
		RE::TESForm* currentForm,
		RE::FormID& outRestoreFormID)
	{
		const RE::FormID currentFormID = currentForm ? currentForm->GetFormID() : 0;
		if (!Wheeler::TryGetTrackedPersistentRestoreTargetForHandMemory(isLeft, currentFormID, outRestoreFormID)) {
			const bool currentLooksTransient =
				!currentForm ||
				NormalizeRestorableHandFormID(pc, currentForm) == 0 ||
				(currentForm->As<RE::SpellItem>() && IsBoundWeaponSpell(currentForm->As<RE::SpellItem>()));
			if (!currentLooksTransient ||
				!Wheeler::TryGetTrackedPersistentRestoreTargetForHandMemory(isLeft, 0, outRestoreFormID)) {
				return false;
			}
		}
		outRestoreFormID = NormalizeRestorableHandFormID(pc, outRestoreFormID);
		return true;
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
			if (NormalizeRestorableHandFormID(pc, form) != formID) {
				return false;
			}
			RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
			if (!aeMan) {
				return false;
			}
			if (IsTwoHandedForm(form)) {
				logger::info("[HandMemoryDiag] EquipFormToHand form={:08X} kind={} requestedHand={} leftNow={:08X} rightNow={:08X}",
					formID,
					GetTwoHandedKindName(form),
					hand == HandMemoryHand::Left ? "LEFT" : "RIGHT",
					GetFormIDOrZero(pc->GetEquippedObject(true)),
					GetFormIDOrZero(pc->GetEquippedObject(false)));
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
		RE::TESForm* current2HForm = IsTwoHandedForm(curRight) ? curRight :
			(IsTwoHandedForm(curLeft) ? curLeft : nullptr);
		if (in2H && current2HForm) {
			g_handMemory.active2HFormID = current2HForm->GetFormID();
		}
		RE::TESObjectREFR::InventoryItemMap inventory;
		if (!Utils::Inventory::TryGetInventorySnapshot(pc, inventory, "UpdateHandMemory")) {
			return;
		}

		auto getRestorableHandMemoryFormID = [&](bool isLeft, RE::TESForm* form) -> RE::FormID {
			RE::FormID trackedRestoreFormID = 0;
			if (TryGetTrackedPersistentHandRestoreFormIDForHandMemory(pc, isLeft, form, trackedRestoreFormID)) {
				return trackedRestoreFormID;
			}
			return NormalizeRestorableHandFormID(inventory, form);
		};

		auto clearInvalidRestoreForm = [&](RE::FormID& formID, const char* handName) {
			if (formID == 0) {
				return;
			}
			auto* form = RE::TESForm::LookupByID(formID);
			if (NormalizeRestorableHandFormID(inventory, form) == formID) {
				return;
			}
			if (HM::DebugLog) {
				logger::info("[HandMemory] Drop non-restorable {} restore {:08X}",
					handName ? handName : "hand",
					formID);
			}
			formID = 0;
		};

		// Direct-cast can temporarily equip spells/powers to hands. Those transient equips
		// must not pollute hand-memory snapshots used for non-direct normal gameplay transitions.
		if (Wheeler::IsDirectCastPipelineActiveForHandMemory()) {
			if (HM::DebugLog && (g_handMemory.restoreArmed || g_handMemory.memLeft != 0 || g_handMemory.memRight != 0)) {
				logger::info("[HandMemory] Suppress during direct-cast pipeline (clear pending restore left={:08X} right={:08X})",
					g_handMemory.memLeft, g_handMemory.memRight);
			}
			g_handMemory.restoreArmed = false;
			g_handMemory.memLeft = 0;
			g_handMemory.memRight = 0;
			g_handMemory.diagRestoreWaitLogged = false;
			// Keep transition baseline synchronized without recording transient hands.
			g_handMemory.was2H = in2H;
			return;
		}

		if (!in2H) {
			g_handMemory.lastNon2HLeft = getRestorableHandMemoryFormID(true, curLeft);
			g_handMemory.lastNon2HRight = getRestorableHandMemoryFormID(false, curRight);
		}

		if (!g_handMemory.was2H && in2H) {
			g_handMemory.memLeft = g_handMemory.lastNon2HLeft;
			g_handMemory.memRight = g_handMemory.lastNon2HRight;
			clearInvalidRestoreForm(g_handMemory.memLeft, "left");
			clearInvalidRestoreForm(g_handMemory.memRight, "right");
			g_handMemory.restoreArmed = false;
			g_handMemory.diagRestoreWaitLogged = false;
			logger::info("[HandMemoryDiag] Enter2H kind={} active2H={:08X} leftNow={:08X} rightNow={:08X} lastNon2HLeft={:08X} lastNon2HRight={:08X} captureLeft={:08X} captureRight={:08X}",
				GetTwoHandedKindName(current2HForm),
				g_handMemory.active2HFormID,
				GetFormIDOrZero(curLeft),
				GetFormIDOrZero(curRight),
				g_handMemory.lastNon2HLeft,
				g_handMemory.lastNon2HRight,
				g_handMemory.memLeft,
				g_handMemory.memRight);
			if (HM::DebugLog) {
				logger::info("[HandMemory] Enter2H capture left={:08X} right={:08X}", g_handMemory.memLeft, g_handMemory.memRight);
			}
		} else if (g_handMemory.was2H && !in2H) {
			g_handMemory.restoreArmed = true;
			g_handMemory.restoreStartTime = GetSafeInputTimestampSeconds();
			g_handMemory.diagRestoreWaitLogged = false;
			logger::info("[HandMemoryDiag] Exit2H active2H={:08X} kind={} leftNow={:08X} rightNow={:08X} restoreLeft={:08X} restoreRight={:08X}",
				g_handMemory.active2HFormID,
				GetTwoHandedKindName(RE::TESForm::LookupByID(g_handMemory.active2HFormID)),
				GetFormIDOrZero(curLeft),
				GetFormIDOrZero(curRight),
				g_handMemory.memLeft,
				g_handMemory.memRight);
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
			g_handMemory.diagRestoreWaitLogged = false;
			return;
		}

		if (IsHandMemoryMenuBlocked()) {
			return;
		}

		clearInvalidRestoreForm(g_handMemory.memLeft, "left");
		clearInvalidRestoreForm(g_handMemory.memRight, "right");

		auto shouldIgnoreCurrentOccupantForRestore = [&](RE::TESForm* currentForm, RE::FormID targetRestoreFormID) {
			if (!currentForm) {
				return false;
			}
			if (IsTransientBoundWeaponForm(currentForm)) {
				return NormalizeRestorableHandFormID(inventory, currentForm) != targetRestoreFormID;
			}
			if (auto* currentSpell = currentForm->As<RE::SpellItem>(); currentSpell && IsBoundWeaponSpell(currentSpell)) {
				return currentSpell->GetFormID() != targetRestoreFormID;
			}
			return false;
		};

		auto clearIgnoredOccupantForRestore = [&](bool isLeft, RE::FormID targetRestoreFormID, RE::TESForm*& currentForm) {
			if (!shouldIgnoreCurrentOccupantForRestore(currentForm, targetRestoreFormID)) {
				return false;
			}
			RE::BGSEquipSlot* slot = isLeft ? Utils::Slot::GetLeftHandSlot() : Utils::Slot::GetRightHandSlot();
			if (!slot) {
				return false;
			}
			logger::info("[HandMemoryDiag] ClearIgnoredOccupant hand={} active2H={:08X} targetRestore={:08X} current={:08X}",
				isLeft ? "LEFT" : "RIGHT",
				g_handMemory.active2HFormID,
				targetRestoreFormID,
				GetFormIDOrZero(currentForm));
			Utils::Slot::CleanSlot(pc, slot);
			currentForm = pc->GetEquippedObject(isLeft);
			return true;
		};

		auto reconcilePendingRestoreAgainstCurrentOccupant = [&](const char* handName, RE::TESForm* currentForm, RE::FormID& pendingRestoreFormID) {
			if (pendingRestoreFormID == 0 || !currentForm) {
				return;
			}

			const RE::FormID currentRestorableFormID = NormalizeRestorableHandFormID(inventory, currentForm);
			if (currentRestorableFormID == 0) {
				return;
			}

			logger::info("[HandMemoryDiag] DropPendingRestore hand={} active2H={:08X} pending={:08X} current={:08X} currentRestorable={:08X}",
				handName ? handName : "?",
				g_handMemory.active2HFormID,
				pendingRestoreFormID,
				GetFormIDOrZero(currentForm),
				currentRestorableFormID);
			pendingRestoreFormID = 0;
		};

		if (!HM::RestoreLeftIfEmpty) {
			g_handMemory.memLeft = 0;
		}
		if (!HM::RestoreRightIfEmpty) {
			g_handMemory.memRight = 0;
		}

		const bool clearedIgnoredRight = clearIgnoredOccupantForRestore(false, g_handMemory.memRight, curRight);
		const bool clearedIgnoredLeft = clearIgnoredOccupantForRestore(true, g_handMemory.memLeft, curLeft);

		reconcilePendingRestoreAgainstCurrentOccupant("LEFT", curLeft, g_handMemory.memLeft);
		reconcilePendingRestoreAgainstCurrentOccupant("RIGHT", curRight, g_handMemory.memRight);

		const bool waitingOnOccupiedRight =
			g_handMemory.memRight != 0 &&
			HM::RestoreRightIfEmpty &&
			curRight != nullptr;
		const bool waitingOnOccupiedLeft =
			g_handMemory.memLeft != 0 &&
			HM::RestoreLeftIfEmpty &&
			curLeft != nullptr;
		if ((waitingOnOccupiedLeft || waitingOnOccupiedRight) && !g_handMemory.diagRestoreWaitLogged) {
			logger::info("[HandMemoryDiag] RestoreWaiting active2H={:08X} leftPending={:08X} rightPending={:08X} curLeft={:08X} curRight={:08X}",
				g_handMemory.active2HFormID,
				g_handMemory.memLeft,
				g_handMemory.memRight,
				GetFormIDOrZero(curLeft),
				GetFormIDOrZero(curRight));
			g_handMemory.diagRestoreWaitLogged = true;
		}

		bool restoredSpellThisFrame = false;

		if (g_handMemory.memRight != 0 && HM::RestoreRightIfEmpty && curRight == nullptr) {
			const RE::FormID toEquip = g_handMemory.memRight;
			g_handMemory.memRight = 0;
			const bool ok = EquipFormToHand(pc, toEquip, HandMemoryHand::Right);
			logger::info("[HandMemoryDiag] RestoreAttempt hand=RIGHT active2H={:08X} target={:08X} targetKind={} ok={} leftNow={:08X} rightNow={:08X}",
				g_handMemory.active2HFormID,
				toEquip,
				GetTwoHandedKindName(RE::TESForm::LookupByID(toEquip)),
				ok ? 1 : 0,
				GetFormIDOrZero(pc->GetEquippedObject(true)),
				GetFormIDOrZero(pc->GetEquippedObject(false)));
			if (ok) {
				if (auto* restoredForm = RE::TESForm::LookupByID(toEquip); restoredForm && restoredForm->As<RE::SpellItem>()) {
					restoredSpellThisFrame = true;
				}
			}
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
			logger::info("[HandMemoryDiag] RestoreAttempt hand=LEFT active2H={:08X} target={:08X} targetKind={} ok={} leftNow={:08X} rightNow={:08X}",
				g_handMemory.active2HFormID,
				toEquip,
				GetTwoHandedKindName(RE::TESForm::LookupByID(toEquip)),
				ok ? 1 : 0,
				GetFormIDOrZero(pc->GetEquippedObject(true)),
				GetFormIDOrZero(pc->GetEquippedObject(false)));
			if (ok) {
				if (auto* restoredForm = RE::TESForm::LookupByID(toEquip); restoredForm && restoredForm->As<RE::SpellItem>()) {
					restoredSpellThisFrame = true;
				}
			}
			if (HM::DebugLog) {
				logger::info("[HandMemory] Restore left {:08X} ok={}", toEquip, ok ? 1 : 0);
			}
		}

		if (restoredSpellThisFrame) {
			if (clearedIgnoredLeft || clearedIgnoredRight) {
				ActorVirtualCompat::DrawWeaponMagicHands(pc, false);
			}
			ActorVirtualCompat::DrawWeaponMagicHands(pc, true);
		}

		if (g_handMemory.memLeft == 0 && g_handMemory.memRight == 0) {
			g_handMemory.restoreArmed = false;
			g_handMemory.diagRestoreWaitLogged = false;
			if (!in2H) {
				g_handMemory.active2HFormID = 0;
			}
		}
	}

}

namespace HandMemory
{
	void NotifyWheelEquipOrCast(const std::shared_ptr<WheelItem>& item, Wheeler::ReleaseAction action)
	{
		const auto actionName = [](Wheeler::ReleaseAction currentAction) {
			switch (currentAction) {
			case Wheeler::ReleaseAction::Equip:
				return "Equip";
			case Wheeler::ReleaseAction::CastSpell:
				return "CastSpell";
			case Wheeler::ReleaseAction::CastShout:
				return "CastShout";
			case Wheeler::ReleaseAction::None:
			default:
				return "None";
			}
		};
		const RE::FormID itemFormID = item ? item->GetFormID() : 0;
		// While actively in a 2H state, memLeft/memRight represent the captured pre-2H build.
		// Preserve that capture until the 2H state actually exits, otherwise wheel actions
		// taken while a bow/2H item is equipped can erase the pre-2H restore target.
		if (g_handMemory.was2H && !g_handMemory.restoreArmed) {
			logger::info("[HandMemoryDiag] NotifyPreservedDuringActive2H action={} item={:08X} active2H={:08X} memLeft={:08X} memRight={:08X}",
				actionName(action),
				itemFormID,
				g_handMemory.active2HFormID,
				g_handMemory.memLeft,
				g_handMemory.memRight);
			return;
		}
		if (!g_handMemory.restoreArmed && g_handMemory.memLeft == 0 && g_handMemory.memRight == 0) {
			return;
		}
		logger::info("[HandMemoryDiag] NotifyClearedPendingRestore action={} item={:08X} active2H={:08X} memLeft={:08X} memRight={:08X}",
			actionName(action),
			itemFormID,
			g_handMemory.active2HFormID,
			g_handMemory.memLeft,
			g_handMemory.memRight);
		g_handMemory.restoreArmed = false;
		g_handMemory.memLeft = 0;
		g_handMemory.memRight = 0;
		g_handMemory.diagRestoreWaitLogged = false;
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

	bool DispatchTempRefOnRead(RE::PlayerCharacter* pc, RE::TESObjectBOOK* book)
	{
		if (!pc || !book) {
			return false;
		}
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) {
			return false;
		}

		auto tempRefPtr = pc->PlaceObjectAtMe(book, false);
		RE::TESObjectREFR* tempRef = tempRefPtr.get();
		if (!tempRef) {
			return false;
		}

		RE::VMHandle handle = vm->GetObjectHandlePolicy()->GetHandleForObject(
			RE::FormType::Reference, tempRef);

		if (handle == 0) {
			SKSE::GetTaskInterface()->AddTask([tempRefPtr]() {
				RE::TESObjectREFR* ref = tempRefPtr.get();
				if (ref) {
					ref->Disable();
					ref->SetDelete(true);
				}
			});
			return false;
		}

		auto args = RE::MakeFunctionArguments();
		vm->SendEvent(handle, RE::BSFixedString("OnRead"), args);
		delete args;

		SKSE::GetTaskInterface()->AddTask([tempRefPtr]() {
			RE::TESObjectREFR* ref = tempRefPtr.get();
			if (ref) {
				ref->Disable();
				ref->SetDelete(true);
			}
		});
		return true;
	}

	std::string TrimBookReadCompatToken(std::string_view token)
	{
		auto begin = token.begin();
		auto end = token.end();
		while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
			++begin;
		}
		while (end != begin) {
			auto prev = end;
			--prev;
			if (!std::isspace(static_cast<unsigned char>(*prev))) {
				break;
			}
			end = prev;
		}
		return std::string(begin, end);
	}

	bool EqualsBookReadCompatIgnoreCase(std::string_view lhs, std::string_view rhs)
	{
		if (lhs.size() != rhs.size()) {
			return false;
		}
		for (std::size_t i = 0; i < lhs.size(); ++i) {
			if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
				std::tolower(static_cast<unsigned char>(rhs[i]))) {
				return false;
			}
		}
		return true;
	}

	bool ContainsBookReadCompatIgnoreCase(std::string_view haystack, std::string_view needle)
	{
		if (needle.empty()) {
			return false;
		}
		if (needle.size() > haystack.size()) {
			return false;
		}
		return std::search(
			haystack.begin(),
			haystack.end(),
			needle.begin(),
			needle.end(),
			[](char lhs, char rhs) {
				return std::tolower(static_cast<unsigned char>(lhs)) ==
					std::tolower(static_cast<unsigned char>(rhs));
			}) != haystack.end();
	}

	enum class BookReadCompatAllowListMatch
	{
		None,
		FormID,
		Plugin,
		NameToken
	};

	std::optional<RE::FormID> ParseBookReadCompatFormIDToken(std::string_view token)
	{
		std::string text = TrimBookReadCompatToken(token);
		if (text.empty()) {
			return std::nullopt;
		}

		char* endPtr = nullptr;
		errno = 0;
		const unsigned long value = std::strtoul(text.c_str(), &endPtr, 16);
		if (errno != 0 || endPtr == text.c_str() || (endPtr && *endPtr != '\0') ||
			value > (std::numeric_limits<std::uint32_t>::max)()) {
			return std::nullopt;
		}

		return static_cast<RE::FormID>(value);
	}

	std::optional<RE::FormID> ResolveBookReadCompatFormIDToken(std::string_view token)
	{
		std::string text = TrimBookReadCompatToken(token);
		if (text.empty()) {
			return std::nullopt;
		}

		const auto sep = text.find_first_of("|:");
		if (sep == std::string::npos) {
			return ParseBookReadCompatFormIDToken(text);
		}

		const std::string pluginName = TrimBookReadCompatToken(std::string_view(text).substr(0, sep));
		const std::string relativeText = TrimBookReadCompatToken(std::string_view(text).substr(sep + 1));
		if (pluginName.empty() || relativeText.empty()) {
			return std::nullopt;
		}

		auto relativeId = ParseBookReadCompatFormIDToken(relativeText);
		if (!relativeId.has_value()) {
			return std::nullopt;
		}

		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			return std::nullopt;
		}
		if (auto* resolvedBook = handler->LookupForm<RE::TESObjectBOOK>(*relativeId, pluginName.c_str())) {
			return resolvedBook->GetFormID();
		}
		return std::nullopt;
	}

	bool IsBookReadCompatFormIDAllowListed(RE::TESObjectBOOK* book)
	{
		if (!book) {
			return false;
		}

		std::string_view csv = Config::WheelBehavior::BookReadCompat::OnReadFormIDs;
		std::size_t start = 0;
		while (start <= csv.size()) {
			std::size_t comma = csv.find(',', start);
			if (comma == std::string_view::npos) {
				comma = csv.size();
			}

			const std::string token = TrimBookReadCompatToken(csv.substr(start, comma - start));
			if (!token.empty()) {
				const auto resolvedFormID = ResolveBookReadCompatFormIDToken(token);
				if (resolvedFormID.has_value() && *resolvedFormID == book->GetFormID()) {
					return true;
				}
				if (!resolvedFormID.has_value() && Config::WheelBehavior::BookReadCompat::DebugLog) {
					logger::info("BookReadCompat: unresolved allowlist token '{}'", token);
				}
			}

			if (comma == csv.size()) {
				break;
			}
			start = comma + 1;
		}

		return false;
	}

	bool IsBookReadCompatPluginAllowListed(RE::TESObjectBOOK* book)
	{
		if (!book) {
			return false;
		}

		const auto* file = book->GetFile(0);
		if (!file) {
			return false;
		}

		const auto fileName = file->GetFilename();
		if (fileName.empty()) {
			return false;
		}

		std::string_view csv = Config::WheelBehavior::BookReadCompat::OnReadPlugins;
		std::size_t start = 0;
		while (start <= csv.size()) {
			std::size_t comma = csv.find(',', start);
			if (comma == std::string_view::npos) {
				comma = csv.size();
			}

			const std::string token = TrimBookReadCompatToken(csv.substr(start, comma - start));
			if (!token.empty() && EqualsBookReadCompatIgnoreCase(fileName, token)) {
				return true;
			}

			if (comma == csv.size()) {
				break;
			}
			start = comma + 1;
		}

		return false;
	}

	bool IsBookReadCompatNameTokenAllowListed(RE::TESObjectBOOK* book)
	{
		if (!book) {
			return false;
		}

		const char* rawName = book->GetName();
		const std::string_view bookName = rawName ? rawName : "";
		if (bookName.empty()) {
			return false;
		}

		std::string_view csv = Config::WheelBehavior::BookReadCompat::OnReadNameTokens;
		std::size_t start = 0;
		while (start <= csv.size()) {
			std::size_t comma = csv.find(',', start);
			if (comma == std::string_view::npos) {
				comma = csv.size();
			}

			const std::string token = TrimBookReadCompatToken(csv.substr(start, comma - start));
			if (!token.empty() && ContainsBookReadCompatIgnoreCase(bookName, token)) {
				return true;
			}

			if (comma == csv.size()) {
				break;
			}
			start = comma + 1;
		}

		return false;
	}

	BookReadCompatAllowListMatch GetBookReadCompatAllowListMatch(RE::TESObjectBOOK* book, bool scriptBacked)
	{
		if (IsBookReadCompatFormIDAllowListed(book)) {
			return BookReadCompatAllowListMatch::FormID;
		}
		if (!scriptBacked) {
			return BookReadCompatAllowListMatch::None;
		}
		if (IsBookReadCompatPluginAllowListed(book)) {
			return BookReadCompatAllowListMatch::Plugin;
		}
		if (IsBookReadCompatNameTokenAllowListed(book)) {
			return BookReadCompatAllowListMatch::NameToken;
		}
		return BookReadCompatAllowListMatch::None;
	}

	const char* BookReadCompatModeName(std::uint32_t mode)
	{
		mode = std::clamp(mode, 0u, 2u);
		switch (static_cast<Config::WheelBehavior::BookReadCompatMode>(mode)) {
		case Config::WheelBehavior::BookReadCompatMode::AutoScripted:
			return "AutoScripted";
		case Config::WheelBehavior::BookReadCompatMode::AllowListOnly:
			return "AllowListOnly";
		case Config::WheelBehavior::BookReadCompatMode::Disabled:
			return "Disabled";
		default:
			return "Unknown";
		}
	}

	bool ShouldUseBookReadCompatOnRead(RE::TESObjectBOOK* book, bool scriptBacked, bool& allowListed, const char*& reason)
	{
		const auto allowListMatch = GetBookReadCompatAllowListMatch(book, scriptBacked);
		allowListed = allowListMatch != BookReadCompatAllowListMatch::None;
		reason = "legacy_bookmenu";

		const auto mode = static_cast<Config::WheelBehavior::BookReadCompatMode>(
			std::clamp(Config::WheelBehavior::BookReadCompat::Mode, 0u, 2u));

		if (mode == Config::WheelBehavior::BookReadCompatMode::Disabled) {
			reason = allowListed ? "disabled_allowlisted" : "disabled";
			return false;
		}
		if (allowListed) {
			switch (allowListMatch) {
			case BookReadCompatAllowListMatch::FormID:
				reason = "allowlist_formid";
				break;
			case BookReadCompatAllowListMatch::Plugin:
				reason = "allowlist_plugin";
				break;
			case BookReadCompatAllowListMatch::NameToken:
				reason = "allowlist_name_token";
				break;
			case BookReadCompatAllowListMatch::None:
			default:
				reason = "allowlist";
				break;
			}
			return true;
		}
		if (mode == Config::WheelBehavior::BookReadCompatMode::AllowListOnly) {
			reason = "allowlist_only";
			return false;
		}
		if (scriptBacked) {
			reason = "auto_scripted";
			return true;
		}
		return false;
	}

	bool OpenBookMenuNow(RE::FormID bookFormID, RE::TESObjectBOOK* book, std::string_view source)
	{
		if (!book) {
			return false;
		}

		RE::NiMatrix3 rot;
		rot.entry[0][0] = 1.0f; rot.entry[0][1] = 0.0f; rot.entry[0][2] = 0.0f;
		rot.entry[1][0] = 0.0f; rot.entry[1][1] = 1.0f; rot.entry[1][2] = 0.0f;
		rot.entry[2][0] = 0.0f; rot.entry[2][1] = 0.0f; rot.entry[2][2] = 1.0f;

		RE::BookMenu::OpenMenuFromBaseForm(book, nullptr, {0, 0, 0}, rot, 1.0f, true);

		if (MainWheelDebug::IsEnabled()) {
			MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_Issued",
				"OpenMenuFromBaseForm called ({}), formID={:08X}", source, bookFormID);
		}

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
		switch (hand) {
		case Wheeler::TargetHand::Left:
			return "LEFT";
		case Wheeler::TargetHand::Both:
			return "BOTH";
		case Wheeler::TargetHand::Right:
		default:
			return "RIGHT";
		}
	}

	constexpr bool UsesLeftAttack(Wheeler::TargetHand hand)
	{
		return hand == Wheeler::TargetHand::Left || hand == Wheeler::TargetHand::Both;
	}

	constexpr bool UsesRightAttack(Wheeler::TargetHand hand)
	{
		return hand == Wheeler::TargetHand::Right || hand == Wheeler::TargetHand::Both;
	}

	bool IsModuleLoadedCompat(std::wstring_view dllName)
	{
		return dllName.empty() ? false : (::GetModuleHandleW(dllName.data()) != nullptr);
	}

	bool HasLoadedPluginCompat(std::string_view pluginName)
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler || pluginName.empty()) {
			return false;
		}

		if (dataHandler->LookupModByName(pluginName.data()) != nullptr) {
			return true;
		}

		if (dataHandler->LookupLoadedLightModByName(pluginName.data()) != nullptr) {
			return true;
		}

		return false;
	}

	bool IsHeavyDrawnWeaponRiskProfile()
	{
		static const bool cached = []() {
			const int score =
				static_cast<int>(HasLoadedPluginCompat("Left Hand Equipment Overhaul.esp")) +
				static_cast<int>(HasLoadedPluginCompat("Draw Fix - Move Equip Animation Fix.esp")) +
				static_cast<int>(HasLoadedPluginCompat("ValhallaCombat.esp")) +
				static_cast<int>(HasLoadedPluginCompat("Attack_DXP.esp")) +
				static_cast<int>(HasLoadedPluginCompat("MCO - First Person Patch.esp")) +
				static_cast<int>(HasLoadedPluginCompat("OCPA.esl")) +
				static_cast<int>(HasLoadedPluginCompat("DynamicCollisionAdjustment.esl")) +
				static_cast<int>(IsModuleLoadedCompat(L"CombatPathingRevolution.dll")) +
				static_cast<int>(IsModuleLoadedCompat(L"UnequipQuiverNG.dll")) +
				static_cast<int>(HasLoadedPluginCompat("Lux Via.esp")) +
				static_cast<int>(HasLoadedPluginCompat("NAT-ENB.esp"));
			return score >= 6;
		}();
		return cached;
	}

	bool IsEquipmentDurabilitySystemProfile()
	{
		static const bool cached =
			IsModuleLoadedCompat(L"EquipmentDurabilitySystemNG.dll") ||
			IsModuleLoadedCompat(L"EquipmentDurabilitySystem-NG.dll");
		return cached;
	}

	bool ShouldUseWeaponDispatchConfirmation()
	{
		// Regression isolation: the old working FavWheel baseline does not use this
		// pending-confirmation state machine. Temporarily keep it disabled until the
		// slot-selection CTD family is proven unrelated.
		return false;
	}

	struct WeaponHandSnapshot
	{
		RE::FormID leftFormID = 0;
		RE::FormID rightFormID = 0;
		bool drawn = false;
	};

	struct WeaponDispatchConfirmationState
	{
		enum class Phase
		{
			WaitingForFirstChange,
			WaitingForStableSnapshot
		};

		bool active = false;
		int entryIdx = -1;
		RE::FormID formID = 0;
		Wheeler::TargetHand hand = Wheeler::TargetHand::Right;
		WeaponHandSnapshot baseline{};
		WeaponHandSnapshot lastObserved{};
		Phase phase = Phase::WaitingForFirstChange;
		double armedAt = 0.0;
		double expiresAt = 0.0;
		double stableSince = 0.0;
	};

	WeaponDispatchConfirmationState g_weaponDispatchConfirmation{};

	WeaponHandSnapshot CaptureWeaponHandSnapshot(RE::PlayerCharacter* player)
	{
		WeaponHandSnapshot snapshot{};
		if (!player) {
			return snapshot;
		}

		if (auto* left = player->GetEquippedObject(true)) {
			snapshot.leftFormID = left->GetFormID();
		}
		if (auto* right = player->GetEquippedObject(false)) {
			snapshot.rightFormID = right->GetFormID();
		}
		if (auto* actorState = player->AsActorState()) {
			snapshot.drawn = actorState->IsWeaponDrawn();
		}
		return snapshot;
	}

	bool HasWeaponHandStateChanged(const WeaponHandSnapshot& baseline, const WeaponHandSnapshot& current)
	{
		return baseline.leftFormID != current.leftFormID ||
		       baseline.rightFormID != current.rightFormID ||
		       baseline.drawn != current.drawn;
	}

	bool IsWeaponDispatchConfirmationCandidate(const std::shared_ptr<WheelItem>& item, RE::FormID formID)
	{
		return item && formID != 0 && std::dynamic_pointer_cast<WheelItemWeapon>(item) != nullptr;
	}

	void ClearWeaponDispatchConfirmation()
	{
		g_weaponDispatchConfirmation = {};
	}

	void UpdateWeaponDispatchConfirmation()
	{
		ClearWeaponDispatchConfirmation();
	}

	std::optional<WeaponHandSnapshot> CaptureWeaponDispatchBaseline(const std::shared_ptr<WheelItem>& item, RE::FormID formID)
	{
		if (!ShouldUseWeaponDispatchConfirmation() || !IsWeaponDispatchConfirmationCandidate(item, formID)) {
			return std::nullopt;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return std::nullopt;
		}

		return CaptureWeaponHandSnapshot(player);
	}

	bool ShouldBlockWeaponDispatchConfirmation(
		const std::shared_ptr<WheelItem>& item,
		RE::FormID formID,
		int entryIdx,
		Wheeler::TargetHand hand)
	{
		(void)item;
		(void)formID;
		(void)entryIdx;
		(void)hand;
		return false;
	}

	void ArmWeaponDispatchConfirmation(
		const std::shared_ptr<WheelItem>& item,
		RE::FormID formID,
		int entryIdx,
		Wheeler::TargetHand hand,
		const std::optional<WeaponHandSnapshot>& baseline)
	{
		(void)item;
		(void)formID;
		(void)entryIdx;
		(void)hand;
		(void)baseline;
		ClearWeaponDispatchConfirmation();
	}

	const char* GetCastingSourceName(RE::MagicSystem::CastingSource source)
	{
		switch (source) {
		case RE::MagicSystem::CastingSource::kLeftHand:
			return "left";
		case RE::MagicSystem::CastingSource::kRightHand:
			return "right";
		case RE::MagicSystem::CastingSource::kOther:
			return "other";
		case RE::MagicSystem::CastingSource::kInstant:
			return "instant";
		default:
			return "unknown";
		}
	}

	const char* GetMagicCasterStateName(RE::MagicCaster::State state)
	{
		switch (state) {
		case RE::MagicCaster::State::kNone:
			return "None";
		case RE::MagicCaster::State::kUnk01:
			return "Unk01";
		case RE::MagicCaster::State::kUnk02:
			return "Unk02";
		case RE::MagicCaster::State::kReady:
			return "Ready";
		case RE::MagicCaster::State::kUnk04:
			return "Unk04";
		case RE::MagicCaster::State::kCharging:
			return "Charging";
		case RE::MagicCaster::State::kCasting:
			return "Casting";
		case RE::MagicCaster::State::kUnk07:
			return "Unk07";
		case RE::MagicCaster::State::kUnk08:
			return "Unk08";
		case RE::MagicCaster::State::kUnk09:
			return "Unk09";
		default:
			return "Unknown";
		}
	}

	constexpr bool IsChargeLikeCasterState(RE::MagicCaster::State state)
	{
		// Keep this broad: unknown active states are spell-specific and can still represent an in-progress charge window.
		switch (state) {
		case RE::MagicCaster::State::kNone:
		case RE::MagicCaster::State::kReady:
			return false;
		default:
			return true;
		}
	}

	struct SpellCasterSnapshot
	{
		RE::MagicSystem::CastingSource source = RE::MagicSystem::CastingSource::kRightHand;
		bool hasCaster = false;
		RE::FormID currentSpellFormID = 0;
		RE::MagicCaster::State state = RE::MagicCaster::State::kNone;
		float castingTimer = 0.0f;
	};

	std::array<RE::MagicSystem::CastingSource, 4> BuildSpellCasterProbeOrder(RE::MagicSystem::CastingSource preferred)
	{
		std::array<RE::MagicSystem::CastingSource, 4> ordered{
			preferred,
			preferred == RE::MagicSystem::CastingSource::kLeftHand ?
				RE::MagicSystem::CastingSource::kRightHand :
				RE::MagicSystem::CastingSource::kLeftHand,
			RE::MagicSystem::CastingSource::kInstant,
			RE::MagicSystem::CastingSource::kOther
		};
		std::array<RE::MagicSystem::CastingSource, 4> unique{};
		std::size_t uniqueCount = 0;
		for (auto source : ordered) {
			bool exists = false;
			for (std::size_t i = 0; i < uniqueCount; ++i) {
				if (unique[i] == source) {
					exists = true;
					break;
				}
			}
			if (!exists && uniqueCount < unique.size()) {
				unique[uniqueCount++] = source;
			}
		}
		for (std::size_t i = uniqueCount; i < unique.size(); ++i) {
			unique[i] = preferred;
		}
		return unique;
	}

	bool CaptureSpellCasterSnapshots(
		RE::PlayerCharacter* pc,
		RE::FormID spellFormID,
		RE::MagicSystem::CastingSource preferredSource,
		SpellCasterSnapshot& matchedSnapshot,
		std::array<SpellCasterSnapshot, 4>* outSnapshots)
	{
		const auto sources = BuildSpellCasterProbeOrder(preferredSource);
		bool matched = false;
		bool hasFirstMatched = false;
		SpellCasterSnapshot firstMatched{};
		for (std::size_t i = 0; i < sources.size(); ++i) {
			SpellCasterSnapshot snapshot{};
			snapshot.source = sources[i];
			if (pc) {
				if (RE::MagicCaster* caster = pc->GetMagicCaster(snapshot.source)) {
					snapshot.hasCaster = true;
					snapshot.state = caster->state.get();
					snapshot.castingTimer = caster->castingTimer;
					if (caster->currentSpell) {
						snapshot.currentSpellFormID = caster->currentSpell->GetFormID();
					}
				}
			}

			if (outSnapshots) {
				(*outSnapshots)[i] = snapshot;
			}

			if (!matched &&
				spellFormID != 0 &&
				snapshot.hasCaster &&
				snapshot.currentSpellFormID == spellFormID) {
				if (!hasFirstMatched) {
					firstMatched = snapshot;
					hasFirstMatched = true;
				}
				const bool chargeActive =
					IsChargeLikeCasterState(snapshot.state) ||
					snapshot.castingTimer > 0.01f;
				if (chargeActive) {
					matchedSnapshot = snapshot;
					matched = true;
				}
			}
		}

		if (!matched && hasFirstMatched) {
			matchedSnapshot = firstMatched;
			matched = true;
		}
		return matched;
	}

	constexpr bool IsSnapshotChargeActive(const SpellCasterSnapshot& snapshot)
	{
		return snapshot.hasCaster &&
		       (IsChargeLikeCasterState(snapshot.state) || snapshot.castingTimer > 0.01f);
	}

	const SpellCasterSnapshot* FindSnapshotBySource(
		const std::array<SpellCasterSnapshot, 4>& snapshots,
		RE::MagicSystem::CastingSource source)
	{
		for (const auto& snapshot : snapshots) {
			if (snapshot.source == source) {
				return &snapshot;
			}
		}
		return nullptr;
	}

	bool TryGetActiveSnapshotForHeldSources(
		const std::array<SpellCasterSnapshot, 4>& snapshots,
		bool useLeftPrimary,
		bool hasSecondAttack,
		bool useLeftSecond,
		SpellCasterSnapshot& outSnapshot)
	{
		const auto primarySource = useLeftPrimary ?
			RE::MagicSystem::CastingSource::kLeftHand :
			RE::MagicSystem::CastingSource::kRightHand;
		const auto* primarySnapshot = FindSnapshotBySource(snapshots, primarySource);
		if (primarySnapshot && IsSnapshotChargeActive(*primarySnapshot)) {
			outSnapshot = *primarySnapshot;
			return true;
		}

		if (hasSecondAttack) {
			const auto secondarySource = useLeftSecond ?
				RE::MagicSystem::CastingSource::kLeftHand :
				RE::MagicSystem::CastingSource::kRightHand;
			if (secondarySource != primarySource) {
				const auto* secondarySnapshot = FindSnapshotBySource(snapshots, secondarySource);
				if (secondarySnapshot && IsSnapshotChargeActive(*secondarySnapshot)) {
					outSnapshot = *secondarySnapshot;
					return true;
				}
			}
		}

		return false;
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
			// DirectCast is transient by design; do not let cast actions mutate hand-memory state.
			if (Config::WheelBehavior::InstantSpellUseDirectCast) {
				return false;
			}
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

	enum class SpellHandRule
	{
		Any,
		LeftOnly,
		RightOnly,
		BothOnly
	};

	const char* GetSpellHandRuleName(SpellHandRule rule)
	{
		switch (rule) {
		case SpellHandRule::LeftOnly:
			return "left_only";
		case SpellHandRule::RightOnly:
			return "right_only";
		case SpellHandRule::BothOnly:
			return "both_only";
		case SpellHandRule::Any:
		default:
			return "any";
		}
	}

	bool EquipSlotMatchesOrInherits(
		const RE::BGSEquipSlot* slot,
		const RE::BGSEquipSlot* target,
		std::uint8_t depth = 0)
	{
		if (!slot || !target) {
			return false;
		}
		if (slot == target) {
			return true;
		}
		if (depth >= 8) {
			return false;
		}
		for (auto* parent : slot->parentSlots) {
			if (!parent) {
				continue;
			}
			if (EquipSlotMatchesOrInherits(parent, target, static_cast<std::uint8_t>(depth + 1))) {
				return true;
			}
		}
		return false;
	}

	SpellHandRule ResolveSpellHandRule(const RE::SpellItem* spell)
	{
		if (!spell) {
			return SpellHandRule::Any;
		}
		if (spell->IsTwoHanded()) {
			return SpellHandRule::BothOnly;
		}

		const RE::BGSEquipSlot* spellSlot = spell->GetEquipSlot();
		if (!spellSlot) {
			return SpellHandRule::Any;
		}

		const RE::BGSEquipSlot* leftSlot = Utils::Slot::GetLeftHandSlot();
		const RE::BGSEquipSlot* rightSlot = Utils::Slot::GetRightHandSlot();
		const bool mapsLeft = EquipSlotMatchesOrInherits(spellSlot, leftSlot);
		const bool mapsRight = EquipSlotMatchesOrInherits(spellSlot, rightSlot);
		const bool requiresAllParents = spellSlot->flags.any(RE::BGSEquipSlot::Flag::kUseAllParents);

		if (mapsLeft && mapsRight) {
			return requiresAllParents ? SpellHandRule::BothOnly : SpellHandRule::Any;
		}
		if (mapsLeft) {
			return SpellHandRule::LeftOnly;
		}
		if (mapsRight) {
			return SpellHandRule::RightOnly;
		}
		return SpellHandRule::Any;
	}

	Wheeler::TargetHand ResolveDirectCastHandForSpell(
		const RE::SpellItem* spell,
		Wheeler::TargetHand requestedHand,
		const char* sourceTag,
		int entryIdx,
		bool a_logDecision = true)
	{
		if (!spell) {
			return requestedHand;
		}

		const SpellHandRule rule = ResolveSpellHandRule(spell);
		Wheeler::TargetHand resolvedHand = requestedHand;
		switch (rule) {
		case SpellHandRule::LeftOnly:
			resolvedHand = Wheeler::TargetHand::Left;
			break;
		case SpellHandRule::RightOnly:
			resolvedHand = Wheeler::TargetHand::Right;
			break;
		case SpellHandRule::BothOnly:
			resolvedHand = Wheeler::TargetHand::Both;
			break;
		case SpellHandRule::Any:
		default:
			break;
		}

		if (a_logDecision && resolvedHand != requestedHand) {
			const RE::BGSEquipSlot* slot = spell->GetEquipSlot();
			logger::info(
				"SpellHandPolicy[{}]: hand {} -> {} rule={} entry={} spell='{}' formId={:08X} equipSlot={:08X}",
				sourceTag ? sourceTag : "unknown",
				GetTargetHandName(requestedHand),
				GetTargetHandName(resolvedHand),
				GetSpellHandRuleName(rule),
				entryIdx,
				spell->GetName() ? spell->GetName() : "",
				spell->GetFormID(),
				slot ? slot->GetFormID() : 0);
		}

		return resolvedHand;
	}

	bool IsPowerSpellType(const RE::SpellItem* spell)
	{
		return Wheeler::IsPowerSpellType(spell);
	}

	bool IsInstantEnabledForSpell(const RE::SpellItem* spell, bool isInTransform)
	{
		return Wheeler::IsInstantEnabledForSpell(spell, isInTransform);
	}

	bool IsBoundWeaponSpell(RE::SpellItem* spell);

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
		const bool lichDirectCastSuppressed =
			TransformWheelManager::ShouldSuppressLichDirectCast(const_cast<RE::SpellItem*>(spell));
		logger::info(
			"InstantGate[{}]: spell='{}' formId={:08X} spellType={} isPower={} isInTransform={} InstantSpell={} InstantPowers={} InstantTransformations={} lichDirectCastSuppressed={} instantEnabled={}",
			source ? source : "unknown",
			spell ? spell->GetName() : "null",
			spell ? spell->GetFormID() : 0,
			spell ? static_cast<int>(spell->GetSpellType()) : -1,
			IsPowerSpellType(spell),
			isInTransform,
			Config::WheelBehavior::InstantSpell,
			Config::WheelBehavior::InstantPowers,
			Config::WheelBehavior::InstantTransformations,
			lichDirectCastSuppressed,
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
		const bool requireLeft = UsesLeftAttack(hand);
		const bool requireRight = UsesRightAttack(hand);
		const bool leftReady = !requireLeft || [&]() {
			if (auto* equipped = pc->GetEquippedObject(true)) {
				return equipped->GetFormID() == formID;
			}
			return false;
		}();
		const bool rightReady = !requireRight || [&]() {
			if (auto* equipped = pc->GetEquippedObject(false)) {
				return equipped->GetFormID() == formID;
			}
			return false;
		}();
		return leftReady && rightReady;
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

	bool IsValidMappedKeyForDevice(RE::INPUT_DEVICE device, std::uint32_t idCode)
	{
		if (idCode == 0xFF) {
			return false;
		}
		if (device == RE::INPUT_DEVICE::kMouse) {
			// Mouse button 0 (LMB) is a valid control-map binding in Skyrim.
			return true;
		}
		return idCode != 0;
	}

	bool ResolveUserEventBinding(const RE::BSFixedString& userEvent, RE::INPUT_DEVICE& outDevice, std::uint32_t& outIdCode)
	{
		outDevice = RE::INPUT_DEVICE::kKeyboard;
		outIdCode = 0;

		auto* controlMap = RE::ControlMap::GetSingleton();
		if (!controlMap || userEvent.empty()) {
			return false;
		}

		const bool preferGamepad = Wheeler::IsLastInputGamepad();
		const std::array<RE::INPUT_DEVICE, 3> preferredOrder = preferGamepad ?
			std::array<RE::INPUT_DEVICE, 3>{ RE::INPUT_DEVICE::kGamepad, RE::INPUT_DEVICE::kMouse, RE::INPUT_DEVICE::kKeyboard } :
			std::array<RE::INPUT_DEVICE, 3>{ RE::INPUT_DEVICE::kMouse, RE::INPUT_DEVICE::kKeyboard, RE::INPUT_DEVICE::kGamepad };

		for (const auto device : preferredOrder) {
			const std::uint32_t mappedId = controlMap->GetMappedKey(userEvent, device);
			if (IsValidMappedKeyForDevice(device, mappedId)) {
				outDevice = device;
				outIdCode = mappedId;
				return true;
			}
		}

		return false;
	}

	struct AttackBindingCandidate
	{
		bool useLeftAttack = false;
		RE::INPUT_DEVICE device = RE::INPUT_DEVICE::kKeyboard;
		std::uint32_t idCode = 0;
	};

	bool ResolveFallbackAttackBinding(
		bool currentUseLeftAttack,
		RE::INPUT_DEVICE currentDevice,
		std::uint32_t currentIdCode,
		bool allowOppositeEvent,
		bool allowCrossDevice,
		AttackBindingCandidate& outCandidate)
	{
		auto* controlMap = RE::ControlMap::GetSingleton();
		auto* userEvents = RE::UserEvents::GetSingleton();
		if (!controlMap || !userEvents) {
			return false;
		}

		std::array<RE::INPUT_DEVICE, 3> preferredOrder{ currentDevice, RE::INPUT_DEVICE::kKeyboard, RE::INPUT_DEVICE::kMouse };
		std::size_t orderCount = 1;
		if (allowCrossDevice) {
			const bool preferGamepad = Wheeler::IsLastInputGamepad();
			if (preferGamepad) {
				const std::array<RE::INPUT_DEVICE, 3> allDevices{
					RE::INPUT_DEVICE::kGamepad,
					RE::INPUT_DEVICE::kKeyboard,
					RE::INPUT_DEVICE::kMouse
				};
				for (auto device : allDevices) {
					if (device == currentDevice) {
						continue;
					}
					if (orderCount < preferredOrder.size()) {
						preferredOrder[orderCount++] = device;
					}
				}
			} else {
				const std::array<RE::INPUT_DEVICE, 3> allDevices{
					RE::INPUT_DEVICE::kKeyboard,
					RE::INPUT_DEVICE::kMouse,
					RE::INPUT_DEVICE::kGamepad
				};
				for (auto device : allDevices) {
					if (device == currentDevice) {
						continue;
					}
					if (orderCount < preferredOrder.size()) {
						preferredOrder[orderCount++] = device;
					}
				}
			}
		}

		std::array<AttackBindingCandidate, 6> candidates{};
		std::size_t candidateCount = 0;
		auto appendCandidates = [&](bool useLeftAttack) {
			const auto& eventName = useLeftAttack ? userEvents->leftAttack : userEvents->rightAttack;
			if (eventName.empty()) {
				return;
			}
			for (std::size_t idx = 0; idx < orderCount; ++idx) {
				const auto device = preferredOrder[idx];
				const std::uint32_t mappedId = controlMap->GetMappedKey(eventName, device);
				if (!IsValidMappedKeyForDevice(device, mappedId)) {
					continue;
				}
				bool exists = false;
				for (std::size_t i = 0; i < candidateCount; ++i) {
					if (candidates[i].useLeftAttack == useLeftAttack &&
						candidates[i].device == device &&
						candidates[i].idCode == mappedId) {
						exists = true;
						break;
					}
				}
				if (exists || candidateCount >= candidates.size()) {
					continue;
				}
				candidates[candidateCount++] = AttackBindingCandidate{ useLeftAttack, device, mappedId };
			}
		};

		appendCandidates(currentUseLeftAttack);
		if (allowOppositeEvent) {
			appendCandidates(!currentUseLeftAttack);
		}

		for (std::size_t i = 0; i < candidateCount; ++i) {
			const auto& candidate = candidates[i];
			const bool sameAsCurrent =
				candidate.useLeftAttack == currentUseLeftAttack &&
				candidate.device == currentDevice &&
				candidate.idCode == currentIdCode;
			if (sameAsCurrent) {
				continue;
			}
			outCandidate = candidate;
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

	RE::FormID GetSelectedVoiceFormID(RE::PlayerCharacter* pc)
	{
		if (!pc) {
			return 0;
		}

		const auto* selectedPower = pc->GetActorRuntimeData().selectedPower;
		return selectedPower ? selectedPower->GetFormID() : 0;
	}

	bool TrySetVoiceSelection(RE::PlayerCharacter* pc, RE::FormID targetFormID, const char* sourceLabel)
	{
		if (!pc) {
			logger::warn("{}: failed to set voice selection (no player) target={:08X}",
				sourceLabel ? sourceLabel : "VoiceSelection",
				targetFormID);
			return false;
		}

		const RE::FormID currentFormID = GetSelectedVoiceFormID(pc);
		if (currentFormID == targetFormID) {
			return true;
		}

		if (targetFormID == 0) {
			pc->GetActorRuntimeData().selectedPower = nullptr;
			return GetSelectedVoiceFormID(pc) == 0;
		}

		RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
		if (!aeMan) {
			logger::warn("{}: failed to set voice selection (no ActorEquipManager) target={:08X}",
				sourceLabel ? sourceLabel : "VoiceSelection",
				targetFormID);
			return false;
		}

		if (auto* shout = RE::TESForm::LookupByID<RE::TESShout>(targetFormID)) {
			aeMan->EquipShout(pc, shout);
			return GetSelectedVoiceFormID(pc) == targetFormID;
		}

		if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(targetFormID);
			spell && Wheeler::IsPowerSpellType(spell)) {
			aeMan->EquipSpell(pc, spell, Utils::Slot::GetVoiceSlot());
			return GetSelectedVoiceFormID(pc) == targetFormID;
		}

		logger::warn("{}: failed to set voice selection (invalid voice form) target={:08X}",
			sourceLabel ? sourceLabel : "VoiceSelection",
			targetFormID);
		return false;
	}

	bool TryRestoreVoiceSelection(RE::PlayerCharacter* pc, RE::FormID restoreFormID, RE::FormID expectedCurrentFormID, const char* sourceLabel)
	{
		if (!pc) {
			logger::warn("{}: failed to restore voice selection (no player) restore={:08X} expectedCurrent={:08X}",
				sourceLabel ? sourceLabel : "VoiceRestore",
				restoreFormID,
				expectedCurrentFormID);
			return false;
		}

		const RE::FormID currentFormID = GetSelectedVoiceFormID(pc);
		if (currentFormID != expectedCurrentFormID) {
			logger::info("{}: skipped voice restore because selection changed current={:08X} expectedCurrent={:08X} restore={:08X}",
				sourceLabel ? sourceLabel : "VoiceRestore",
				currentFormID,
				expectedCurrentFormID,
				restoreFormID);
			return false;
		}

		return TrySetVoiceSelection(pc, restoreFormID, sourceLabel);
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
	if (TransformWheelManager::ShouldSuppressLichDirectCast(const_cast<RE::SpellItem*>(spell))) {
		return false;
	}
	if (isInTransform) {
		return Config::WheelBehavior::InstantTransformations;
	}
	if (IsPowerSpellType(spell)) {
		return Config::WheelBehavior::InstantPowers;
	}
	return Config::WheelBehavior::InstantSpell;
}

bool Wheeler::IsRTUAutoInstantSpellEnabled(const RE::SpellItem* spell, bool isInTransform)
{
	if (!Config::WheelBehavior::ReleaseToUse ||
		!Config::WheelBehavior::RTUSpell ||
		!Config::WheelBehavior::RTUAutoInstantSpell) {
		return false;
	}

	return IsInstantEnabledForSpell(spell, isInTransform);
}

bool Wheeler::IsRTUAutoInstantShoutEnabled()
{
	return Config::WheelBehavior::ReleaseToUse &&
	       Config::WheelBehavior::RTUShout &&
	       Config::WheelBehavior::RTUAutoInstantShout &&
	       Config::WheelBehavior::InstantShout;
}

bool Wheeler::IsDirectCastPipelineActiveForHandMemory()
{
	if (!Config::WheelBehavior::InstantSpellUseDirectCast) {
		return false;
	}

	const bool waitingOnPersistentHandItem =
		_spellPostCastRestoreTrackedLeftOccupantFormID != 0 ||
		_spellPostCastRestoreTrackedRightOccupantFormID != 0;

	return _pendingSpellActivation.has_value() ||
	       _spellHoldActive ||
	       (_spellPostCastRestorePending && !waitingOnPersistentHandItem);
}

bool Wheeler::TryGetTrackedPersistentRestoreTargetForHandMemory(
	bool a_isLeftHand,
	RE::FormID a_currentFormID,
	RE::FormID& a_outRestoreFormID)
{
	a_outRestoreFormID = 0;
	if (!_spellPostCastRestorePending) {
		return false;
	}

	const bool restoreHand = a_isLeftHand ? _spellPostCastRestoreLeftHand : _spellPostCastRestoreRightHand;
	const RE::FormID trackedOccupantFormID = a_isLeftHand ?
		_spellPostCastRestoreTrackedLeftOccupantFormID :
		_spellPostCastRestoreTrackedRightOccupantFormID;
	if (!restoreHand || trackedOccupantFormID == 0) {
		return false;
	}
	if (a_currentFormID != 0 && trackedOccupantFormID != a_currentFormID) {
		return false;
	}

	a_outRestoreFormID = a_isLeftHand ?
		_spellPostCastRestoreLeftFormID :
		_spellPostCastRestoreRightFormID;
	return true;
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
		audioManager->GetSoundHandleByName(handle, a_editorID, 0x10);
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
	int hoveredEntryIndex = -1;
	std::shared_ptr<WheelItem> hoveredItem;
	{
		std::shared_lock<std::shared_mutex> wheelDataLock(_wheelDataLock);
		if (!HasValidActiveWheel_NoLock()) {
			return false;
		}
		Wheel* activeWheel = _wheels[_activeWheelIdx].get();
		hoveredEntryIndex = activeWheel->GetHoveredEntryIndex();
		hoveredItem = activeWheel->GetHoveredSelectedItem();
		if (!hoveredItem) {
			return false;
		}
		if (hoveredEntryIndex >= 0) {
			if (WheelEntry* entry = activeWheel->GetEntry(hoveredEntryIndex); entry && entry->IsMissingInInventory()) {
				return false;
			}
		}
	}
	std::shared_ptr<WheelItemSpell> spellItem = std::dynamic_pointer_cast<WheelItemSpell>(hoveredItem);
	std::shared_ptr<WheelItemShout> shoutItem = std::dynamic_pointer_cast<WheelItemShout>(hoveredItem);
	RE::SpellItem* guardedSpell = spellItem ? spellItem->GetSpell() : nullptr;
	const bool isInTransform = !TransformWheelManager::IsPlayerHuman();
	if (guardedSpell && IsSpellBlockedByTransformGuard(guardedSpell, "RTU")) {
		return false;
	}

	// Check if this is a shout that should bypass the global RTU delay
	const bool isShoutWithBypass = shoutItem && 
		IsRTUAutoInstantShoutEnabled() &&
		Config::WheelBehavior::ShoutIgnoreRTUDelay;

	// Apply hover delay check (bypass for shouts with ShoutIgnoreRTUDelay)
	if (!isShoutWithBypass && _hoveredEntryTime < Config::WheelBehavior::HoverActivateDelaySeconds) {
		if (logDelaySkip) {
			LOG_INFO(Activation_ActivateOnClose, "ActivateOnClose: skipped (hoverTime {} < delay {})", _hoveredEntryTime, Config::WheelBehavior::HoverActivateDelaySeconds);
		}
		return false;
	}

	// Item type checks
	if ((std::dynamic_pointer_cast<WheelItemAlchemy>(hoveredItem) ||
		 std::dynamic_pointer_cast<WheelItemIngredient>(hoveredItem)) &&
		!Config::WheelBehavior::RTUAlchemy) {
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
		const bool instantEnabled = IsInstantEnabledForSpell(spell, isInTransform);
		const bool rtuAutoInstantSpellEnabled = IsRTUAutoInstantSpellEnabled(spell, isInTransform);
		LogInstantGateDecision("RTU", spell, isInTransform, instantEnabled);

		if (rtuAutoInstantSpellEnabled) {
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
		} else if (instantEnabled &&
			(Config::WheelBehavior::InstantSpellDebugLog || Config::WheelBehavior::InstantTransformationsDebugLog)) {
			LOG_INFO(Activation_InstantSpell,
				"RTU InstantSpell: auto cast disabled for RTU close, fallback to equip/manual hold");
		}
	}

	if (resolvedAction != ReleaseAction::CastSpell &&
		shoutItem && IsRTUAutoInstantShoutEnabled()) {
		resolvedAction = ReleaseAction::CastShout;
	}

	logger::info("ReleaseResolve[RTU]: action={} entry={} formId={:08X}",
		GetReleaseActionName(resolvedAction), hoveredEntryIndex, formId);

	TargetHand resolvedHand = ResolveTargetHandRTU(hoveredEntryIndex, hoveredItem, resolvedAction);
	if (resolvedAction == ReleaseAction::CastSpell &&
		spellItem &&
		Config::WheelBehavior::InstantSpellUseDirectCast) {
		resolvedHand = ResolveDirectCastHandForSpell(
			spellItem->GetSpell(),
			resolvedHand,
			"RTU",
			hoveredEntryIndex);
	}
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
		} else if (!spell) {
			logger::info("ReleaseResolve[RTU]: cast skipped (spell null), fallback to equip entry={} formId={:08X}",
				hoveredEntryIndex, formId);
			resolvedAction = ReleaseAction::Equip;
		} else if (Config::WheelBehavior::InstantSpellUseDirectCast) {
			const float concentrationHoldSeconds =
				spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration ?
				Config::WheelBehavior::InstantSpellConcentrationMaxSeconds :
				0.0f;
			activated = QueueSpellActivation(spell->GetFormID(), resolvedHand, concentrationHoldSeconds);
			if (activated) {
				LOG_INFO(Activation_InstantSpell, "RTU: DirectCast queued entry={} hand={} holdSec={:.2f}",
					hoveredEntryIndex, GetTargetHandName(resolvedHand), concentrationHoldSeconds);
				_rtuConsumedByInstant = true;
			} else {
				logger::info("ReleaseResolve[RTU]: direct cast queue failed, fallback to equip entry={} formId={:08X}",
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
		shoutItem && IsRTUAutoInstantShoutEnabled()) {
		activated = shoutItem->CastImmediate(_hoveredEntryTime);
		if (activated) {
			LOG_INFO(Activation_InstantShout, "RTU: InstantShout cast immediate with hoverTime={:.2f}s", _hoveredEntryTime);
		} else {
			logger::info("ReleaseResolve[RTU]: shout cast failed, fallback to equip entry={} formId={:08X}",
				hoveredEntryIndex, formId);
			resolvedAction = ReleaseAction::Equip;
		}
	}

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
		logger::info("ReleaseResolve[RTU]: equip={} reason={} entry={} hand={} formId={:08X}",
			equipBlocked ? "blocked" : "allowed",
			equipReason,
			hoveredEntryIndex,
			GetTargetHandName(resolvedHand),
			formId);

		if (!hoveredItem || (hoveredItem->RequiresRuntimeFormValidation() && formId == 0)) {
			logger::info(
				"WheelDispatch: skipped weapon activation entry={} hand={} formId={:08X} reason=invalid_form",
				hoveredEntryIndex,
				GetTargetHandName(resolvedHand),
				formId);
			return false;
		}
		{
			std::shared_lock<std::shared_mutex> wheelDataLock(_wheelDataLock);
			if (!HasValidActiveWheel_NoLock()) {
				return false;
			}
			Wheel* activeWheel = _wheels[_activeWheelIdx].get();
			if (activeWheel->GetHoveredEntryIndex() != hoveredEntryIndex) {
				return false;
			}
			if (useLeft) {
				activeWheel->ActivateHoveredEntrySecondary(false);
			} else {
				activeWheel->ActivateHoveredEntryPrimary(false);
			}
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
			HandMemory::NotifyWheelEquipOrCast(hoveredItem, resolvedAction);
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

void Wheeler::QueueShoutPostCastRestore(RE::FormID a_shoutFormID)
{
	ClearShoutPostCastRestore();

	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	const RE::FormID selectedVoiceFormID = GetSelectedVoiceFormID(pc);
	if (!pc || selectedVoiceFormID == a_shoutFormID) {
		return;
	}

	_shoutPostCastRestorePending = true;
	_shoutPostCastRestoreFormID = selectedVoiceFormID;
	_shoutPostCastRestoreExpectedFormID = a_shoutFormID;
	logger::info("Shout: queued voice restore previous={:08X} requested={:08X}",
		selectedVoiceFormID,
		a_shoutFormID);
}

void Wheeler::TryRestoreQueuedShoutSelection(const char* a_reason)
{
	if (!_shoutPostCastRestorePending) {
		_shoutPostCastRestoreDelayActive = false;
		_shoutPostCastRestoreDelayElapsedSec = 0.0f;
		return;
	}

	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	const RE::FormID restoreFormID = _shoutPostCastRestoreFormID;
	const RE::FormID expectedFormID = _shoutPostCastRestoreExpectedFormID;
	const bool restored = TryRestoreVoiceSelection(pc, restoreFormID, expectedFormID, "ShoutRestore");
	logger::info("Shout: restore attempted reason={} restored={} previous={:08X} requested={:08X}",
		a_reason ? a_reason : "",
		restored ? 1 : 0,
		restoreFormID,
		expectedFormID);
	ClearShoutPostCastRestore();
}

void Wheeler::ArmDelayedShoutPostCastRestore(const char* a_reason)
{
	if (!_shoutPostCastRestorePending) {
		return;
	}

	_shoutPostCastRestoreDelayActive = true;
	_shoutPostCastRestoreDelayElapsedSec = 0.0f;
	logger::info("Shout: delayed voice restore armed reason={} delaySec={:.2f} previous={:08X} requested={:08X}",
		a_reason ? a_reason : "",
		kShoutVoiceSelectionRestoreDelaySec,
		_shoutPostCastRestoreFormID,
		_shoutPostCastRestoreExpectedFormID);
}

void Wheeler::ClearShoutPostCastRestore()
{
	_shoutPostCastRestorePending = false;
	_shoutPostCastRestoreFormID = 0;
	_shoutPostCastRestoreExpectedFormID = 0;
	_shoutPostCastRestoreDelayActive = false;
	_shoutPostCastRestoreDelayElapsedSec = 0.0f;
}

bool Wheeler::QueueShoutActivation(RE::FormID a_shoutFormID, float a_hoverTime)
{
	if (a_shoutFormID == 0) {
		return false;
	}
	if (auto* shout = RE::TESForm::LookupByID<RE::TESShout>(a_shoutFormID);
		IsShoutBlockedByTransformGuard(shout, "QueueShout")) {
		return false;
	}
	if (_pendingShoutFormID.has_value()) {
		LOG_WARN(Activation_InstantShout, "Shout: activation request skipped (already pending): pending={:08X}, new={:08X}", 
			*_pendingShoutFormID, a_shoutFormID);
		return false;
	}

	QueueShoutPostCastRestore(a_shoutFormID);
	_pendingShoutFormID = a_shoutFormID;
	_pendingShoutHoverTime = a_hoverTime;

	// Ensure the wheel closes without re-triggering RTU activation on close.
	_activateOnCloseFired = true;
	_forceCloseRequested = true;
	SHOUTPIPE("QueueShoutActivation formID={:08X} hoverTime={:.2f} state={}",
		a_shoutFormID, a_hoverTime, static_cast<int>(_state));
	LOG_INFO(Activation_InstantShout, "Shout: queued activation after wheel closes (formID={:08X}, hoverTime={:.2f}s, state={})", 
		a_shoutFormID, a_hoverTime, static_cast<int>(_state));
	return true;
}

bool Wheeler::QueueSpellActivation(RE::FormID a_spellFormID, TargetHand a_hand, float a_concentrationHoldSeconds)
{
	constexpr std::uint8_t kSpellReadyRetryFrameBudget = 240;
	constexpr std::uint8_t kInitialDispatchSettleFrameBudget = 8;
	auto updatePreCastFlags = [](PendingSpellActivation& entry) {
		entry.preCastHadTwoHandedWeapon = false;
		if (!entry.preCastHandsCaptured) {
			return;
		}
		entry.preCastHadTwoHandedWeapon =
			IsTwoHandedFormID(entry.preCastLeftFormID) ||
			IsTwoHandedFormID(entry.preCastRightFormID);
	};

	if (a_spellFormID == 0) {
		return false;
	}
	if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_spellFormID);
		IsSpellBlockedByTransformGuard(spell, "QueueSpell")) {
		return false;
	}
	if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_spellFormID);
		TransformWheelManager::ShouldSuppressLichDirectCast(spell, "QueueSpell")) {
		return false;
	}
	auto* carryPc = RE::PlayerCharacter::GetSingleton();
	const bool canInspectCarryHands = carryPc && carryPc->Is3DLoaded();
	auto getCurrentCarryFormID = [&](bool isLeft) -> RE::FormID {
		if (!canInspectCarryHands) {
			return 0;
		}
		if (auto* currentForm = carryPc->GetEquippedObject(isLeft)) {
			return currentForm->GetFormID();
		}
		return 0;
	};
	const RE::FormID currentCarryLeftFormID = getCurrentCarryFormID(true);
	const RE::FormID currentCarryRightFormID = getCurrentCarryFormID(false);
	auto shouldCarryPendingRestoreHand = [&](bool isLeft) {
		if (!_spellPostCastRestorePending) {
			return false;
		}
		const bool restoreHand = isLeft ? _spellPostCastRestoreLeftHand : _spellPostCastRestoreRightHand;
		if (!restoreHand) {
			return false;
		}
		const RE::FormID trackedOccupantFormID = isLeft ?
			_spellPostCastRestoreTrackedLeftOccupantFormID :
			_spellPostCastRestoreTrackedRightOccupantFormID;
		if (trackedOccupantFormID == 0) {
			return true;
		}
		if (!canInspectCarryHands) {
			return false;
		}
		const RE::FormID currentFormID = isLeft ? currentCarryLeftFormID : currentCarryRightFormID;
		return currentFormID == trackedOccupantFormID || currentFormID == 0;
	};
	const bool carriedRestoreLeft = shouldCarryPendingRestoreHand(true);
	const bool carriedRestoreRight = shouldCarryPendingRestoreHand(false);
	const RE::FormID carriedRestoreLeftFormID = carriedRestoreLeft ? _spellPostCastRestoreLeftFormID : 0;
	const RE::FormID carriedRestoreRightFormID = carriedRestoreRight ? _spellPostCastRestoreRightFormID : 0;
	if (_spellPostCastRestorePending) {
		if (!carriedRestoreLeft && !carriedRestoreRight) {
			logger::info("[SpellPipe] discard pending post-cast restore for new cast request formID={:08X} trackedLeft={:08X} trackedRight={:08X}",
				a_spellFormID,
				_spellPostCastRestoreTrackedLeftOccupantFormID,
				_spellPostCastRestoreTrackedRightOccupantFormID);
		} else {
			logger::info("[SpellPipe] carry pending post-cast restore into new cast request formID={:08X} left={:08X} right={:08X} trackedLeft={:08X} trackedRight={:08X}",
				a_spellFormID,
				carriedRestoreLeftFormID,
				carriedRestoreRightFormID,
				_spellPostCastRestoreTrackedLeftOccupantFormID,
				_spellPostCastRestoreTrackedRightOccupantFormID);
		}
		ClearPostCastRestore();
	}
	if (_pendingSpellActivation.has_value()) {
		auto& pending = *_pendingSpellActivation;
		if (pending.formID == a_spellFormID) {
			pending.concentrationHoldSeconds = (std::max)(pending.concentrationHoldSeconds, (std::max)(0.0f, a_concentrationHoldSeconds));
			if (!pending.preCastHandsCaptured) {
				if (auto* pc = RE::PlayerCharacter::GetSingleton(); pc && pc->Is3DLoaded()) {
					pending.preCastHandsCaptured = true;
					if (auto* leftForm = pc->GetEquippedObject(true)) {
						pending.preCastLeftFormID = leftForm->GetFormID();
					}
					if (auto* rightForm = pc->GetEquippedObject(false)) {
						pending.preCastRightFormID = rightForm->GetFormID();
					}
					updatePreCastFlags(pending);
				}
			}
			if (carriedRestoreLeft || carriedRestoreRight) {
				if (carriedRestoreLeft) {
					pending.restoreOverrideLeftHand = true;
					pending.restoreOverrideLeftFormID = NormalizeRestorableHandFormID(carryPc, carriedRestoreLeftFormID);
				}
				if (carriedRestoreRight) {
					pending.restoreOverrideRightHand = true;
					pending.restoreOverrideRightFormID = NormalizeRestorableHandFormID(carryPc, carriedRestoreRightFormID);
				}
				logger::info("[SpellPipe] pending cast seeded from carried restore override formID={:08X} left={:08X} right={:08X}",
					a_spellFormID,
					pending.restoreOverrideLeftHand ? pending.restoreOverrideLeftFormID : 0,
					pending.restoreOverrideRightHand ? pending.restoreOverrideRightFormID : 0);
			}
			updatePreCastFlags(pending);

			if (pending.hand != TargetHand::Both && a_hand != TargetHand::Both && pending.hand != a_hand) {
				pending.hand = TargetHand::Both;
				pending.requiredEquipBeforeCast = true;
				pending.postEquipWarmupFramesRemaining = 0;
				pending.readyRetryFramesRemaining = (std::max)(pending.readyRetryFramesRemaining, kSpellReadyRetryFrameBudget);
				logger::info("[SpellPipe] activation request merged to BOTH formID={:08X} requestedExisting={} requestedNew={} holdSec={:.2f}",
					a_spellFormID,
					GetTargetHandName(pending.requestedHand),
					GetTargetHandName(a_hand),
					pending.concentrationHoldSeconds);
			} else {
				logger::info("[SpellPipe] activation request coalesced formID={:08X} pendingHand={} requestedExisting={} requestedNew={} holdSec={:.2f}",
					a_spellFormID,
					GetTargetHandName(pending.hand),
					GetTargetHandName(pending.requestedHand),
					GetTargetHandName(a_hand),
					pending.concentrationHoldSeconds);
			}
			return true;
		}

		logger::warn("[SpellPipe] activation request skipped (already pending): pending={:08X}, new={:08X}",
			_pendingSpellActivation->formID, a_spellFormID);
		return false;
	}

	PendingSpellActivation pending{};
	pending.formID = a_spellFormID;
	pending.hand = a_hand;
	pending.requestedHand = a_hand;
	pending.concentrationHoldSeconds = (std::max)(0.0f, a_concentrationHoldSeconds);
	pending.readyRetryFramesRemaining = kSpellReadyRetryFrameBudget;
	// Use full settle by default for direct-cast reliability (close->equip->attack ordering).
	pending.postEquipWarmupFramesRemaining = kInitialDispatchSettleFrameBudget;
	if (auto* pc = RE::PlayerCharacter::GetSingleton(); pc && pc->Is3DLoaded()) {
		pending.preCastHandsCaptured = true;
		if (auto* leftForm = pc->GetEquippedObject(true)) {
			pending.preCastLeftFormID = leftForm->GetFormID();
		}
		if (auto* rightForm = pc->GetEquippedObject(false)) {
			pending.preCastRightFormID = rightForm->GetFormID();
		}
		updatePreCastFlags(pending);
	}
	if (carriedRestoreLeft || carriedRestoreRight) {
		if (carriedRestoreLeft) {
			pending.restoreOverrideLeftHand = true;
			pending.restoreOverrideLeftFormID = NormalizeRestorableHandFormID(carryPc, carriedRestoreLeftFormID);
		}
		if (carriedRestoreRight) {
			pending.restoreOverrideRightHand = true;
			pending.restoreOverrideRightFormID = NormalizeRestorableHandFormID(carryPc, carriedRestoreRightFormID);
		}
		logger::info("[SpellPipe] new cast seeded from carried restore override formID={:08X} left={:08X} right={:08X}",
			a_spellFormID,
			pending.restoreOverrideLeftHand ? pending.restoreOverrideLeftFormID : 0,
			pending.restoreOverrideRightHand ? pending.restoreOverrideRightFormID : 0);
	}
	updatePreCastFlags(pending);
	_pendingSpellActivation = pending;

	// Ensure the wheel closes before we invoke the vanilla attack pipeline.
	_activateOnCloseFired = true;
	_forceCloseRequested = true;
	logger::info("[SpellPipe] QueueSpellActivation formID={:08X} hand={} requested={} holdSec={:.2f} preCast2H={} state={}",
		a_spellFormID, GetTargetHandName(a_hand), GetTargetHandName(pending.requestedHand), pending.concentrationHoldSeconds, pending.preCastHadTwoHandedWeapon ? 1 : 0, static_cast<int>(_state));
	return true;
}

bool Wheeler::QueuePowerActivation(RE::FormID a_powerFormID)
{
	if (a_powerFormID == 0) {
		return false;
	}
	if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_powerFormID);
		IsSpellBlockedByTransformGuard(spell, "QueuePower")) {
		return false;
	}
	if (_pendingPowerFormID.has_value()) {
		logger::warn("[PowerPipe] activation request skipped (already pending): pending={:08X}, new={:08X}",
			*_pendingPowerFormID, a_powerFormID);
		return false;
	}

	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	const RE::FormID selectedVoiceFormID = GetSelectedVoiceFormID(pc);
	_pendingPowerFormID = a_powerFormID;
	_pendingPowerRestorePending = pc && selectedVoiceFormID != a_powerFormID;
	_pendingPowerRestoreFormID = _pendingPowerRestorePending ? selectedVoiceFormID : 0;
	if (_pendingPowerRestorePending) {
		logger::info("[PowerPipe] queued voice restore previous={:08X} requested={:08X}",
			_pendingPowerRestoreFormID,
			a_powerFormID);
	}

	// Ensure the wheel closes before we invoke the vanilla shout/power pipeline.
	_activateOnCloseFired = true;
	_forceCloseRequested = true;
	logger::info("[PowerPipe] QueuePowerActivation formID={:08X} state={}",
		a_powerFormID, static_cast<int>(_state));
	return true;
}

bool Wheeler::QueueExternalHotkeyDispatch(
	std::uint32_t a_scanCode,
	std::uint32_t a_modifier,
	std::string_view a_displayName,
	std::uint32_t a_sourceSlotIndex,
	std::string_view a_sourceTag)
{
	if (a_scanCode == 0) {
		logger::warn("ActionHotkeysBridge: dispatch blocked reason=InvalidScanCode slot={} tag={}", a_sourceSlotIndex, a_sourceTag);
		return false;
	}
	if (_editMode) {
		logger::info("ActionHotkeysBridge: dispatch blocked reason=EditMode slot={} tag={}", a_sourceSlotIndex, a_sourceTag);
		return false;
	}
	if (Controls::IsRebindActive()) {
		logger::info("ActionHotkeysBridge: dispatch blocked reason=RebindCapture slot={} tag={}", a_sourceSlotIndex, a_sourceTag);
		return false;
	}
	if (_pendingExternalHotkeyDispatch.has_value()) {
		const auto& pending = *_pendingExternalHotkeyDispatch;
		logger::info(
			"ActionHotkeysBridge: dispatch blocked reason=AlreadyPending pendingScan=0x{:X} pendingMod=0x{:X} newScan=0x{:X} newMod=0x{:X}",
			pending.scanCode,
			pending.modifier,
			a_scanCode,
			a_modifier);
		return false;
	}

	PendingExternalHotkeyDispatch pending{};
	pending.scanCode = a_scanCode;
	pending.modifier = a_modifier;
	pending.displayName = std::string(a_displayName);
	pending.sourceSlotIndex = a_sourceSlotIndex;
	pending.sourceTag = std::string(a_sourceTag);
	pending.queuedAt = ImGui::GetTime();
	_pendingExternalHotkeyDispatch = std::move(pending);

	_activateOnCloseFired = true;
	_forceCloseRequested = true;
	if (Config::ActionHotkeysBridge::DebugLog) {
		logger::info(
			"ActionHotkeysBridge: dispatch queued slot={} tag={} hotkey='{}'",
			a_sourceSlotIndex,
			a_sourceTag,
			BuildExternalHotkeyLabel(a_scanCode, a_modifier));
	}
	return true;
}

void Wheeler::ArmSpellHoldRelease(
	RE::INPUT_DEVICE a_device,
	std::uint32_t a_idCode,
	bool a_useLeftAttack,
	float a_holdDuration,
	bool a_waitForCasterStart,
	RE::FormID a_spellFormID,
	RE::MagicSystem::CastingSource a_castingSource,
	bool a_chargeAwareRelease,
	float a_chargeAwareMaxTotalSeconds,
	bool a_releaseSecondAttack,
	bool a_secondUseLeftAttack,
	RE::INPUT_DEVICE a_secondDevice,
	std::uint32_t a_secondIdCode,
	bool a_restoreLeftHand,
	RE::FormID a_restoreLeftFormID,
	bool a_restoreRightHand,
	RE::FormID a_restoreRightFormID,
	bool a_enableStartAssistTap,
	bool a_startAssistUseLeftAttack,
	RE::INPUT_DEVICE a_startAssistDevice,
	std::uint32_t a_startAssistIdCode,
	bool a_releaseSecondOnCasterStart,
	bool a_requirePrimaryCasterStart)
{
	const double now = ImGui::GetTime();
	_spellHoldActive = true;
	_spellHoldStartTime = now;
	_spellHoldWaitStartTime = now;
	_spellHoldDuration = (std::max)(0.0f, a_holdDuration);
	_spellHoldUseLeftAttack = a_useLeftAttack;
	_spellHoldWaitForCasterStart = a_waitForCasterStart && a_spellFormID != 0;
	_spellHoldSpellFormID = a_spellFormID;
	_spellHoldCastingSource = a_castingSource;
	_spellHoldChargeAwareRelease = a_chargeAwareRelease && a_spellFormID != 0;
	_spellHoldChargeAwareMaxTotalSeconds = (std::max)(0.0f, a_chargeAwareMaxTotalSeconds);
	_spellHoldChargeAwareLastLogTime = 0.0;
	_spellHoldAlsoReleaseSecondAttack = a_releaseSecondAttack;
	_spellHoldSecondUseLeftAttack = a_secondUseLeftAttack;
	_spellHoldPrimaryCastingSource = a_useLeftAttack ?
		RE::MagicSystem::CastingSource::kLeftHand :
		RE::MagicSystem::CastingSource::kRightHand;
	_spellHoldStartRetryCount = 0;
	_spellHoldLastRetryPulseTime = now;
	_spellHoldObservedCasterStart = false;
	_spellHoldObservedChargeActivity = false;
	_spellHoldUsedFallbackStart = false;
	_spellHoldReleaseSecondOnCasterStart = a_releaseSecondOnCasterStart;
	_spellHoldRequirePrimaryCasterStart = a_requirePrimaryCasterStart;
	_spellHoldStartAssistTapEnabled = a_enableStartAssistTap;
	_spellHoldStartAssistTapUseLeftAttack = a_startAssistUseLeftAttack;
	_spellHoldStartAssistTapDevice = a_startAssistDevice;
	_spellHoldStartAssistTapIdCode = a_startAssistIdCode;
	_spellHoldStartAssistTapSent = false;
	// Allow restore-to-empty (formID=0) for direct-cast temporary equips.
	_spellHoldRestoreLeftHand = a_restoreLeftHand;
	_spellHoldRestoreRightHand = a_restoreRightHand;
	_spellHoldRestoreLeftFormID = _spellHoldRestoreLeftHand ? a_restoreLeftFormID : 0;
	_spellHoldRestoreRightFormID = _spellHoldRestoreRightHand ? a_restoreRightFormID : 0;
	_spellHoldRestoreHandsAfterRelease = _spellHoldRestoreLeftHand || _spellHoldRestoreRightHand;
	_spellBindDeviceSecond = a_secondDevice;
	_spellBindIdCodeSecond = a_secondIdCode;
	_spellBindDevice = a_device;
	_spellBindIdCode = a_idCode;
}

void Wheeler::ClearSpellHoldRelease()
{
	_spellHoldActive = false;
	_spellHoldStartTime = 0.0;
	_spellHoldWaitStartTime = 0.0;
	_spellHoldDuration = 0.0f;
	_spellHoldUseLeftAttack = false;
	_spellHoldWaitForCasterStart = false;
	_spellHoldSpellFormID = 0;
	_spellHoldCastingSource = RE::MagicSystem::CastingSource::kRightHand;
	_spellHoldChargeAwareRelease = false;
	_spellHoldChargeAwareMaxTotalSeconds = 0.0f;
	_spellHoldChargeAwareLastLogTime = 0.0;
	_spellHoldAlsoReleaseSecondAttack = false;
	_spellHoldSecondUseLeftAttack = false;
	_spellHoldStartRetryCount = 0;
	_spellHoldLastRetryPulseTime = 0.0;
	_spellHoldObservedCasterStart = false;
	_spellHoldObservedChargeActivity = false;
	_spellHoldUsedFallbackStart = false;
	_spellHoldReleaseSecondOnCasterStart = false;
	_spellHoldRequirePrimaryCasterStart = false;
	_spellHoldPrimaryCastingSource = RE::MagicSystem::CastingSource::kRightHand;
	_spellHoldStartAssistTapEnabled = false;
	_spellHoldStartAssistTapUseLeftAttack = false;
	_spellHoldStartAssistTapDevice = RE::INPUT_DEVICE::kKeyboard;
	_spellHoldStartAssistTapIdCode = 0;
	_spellHoldStartAssistTapSent = false;
	_spellHoldRestoreHandsAfterRelease = false;
	_spellHoldRestoreLeftHand = false;
	_spellHoldRestoreRightHand = false;
	_spellHoldRestoreLeftFormID = 0;
	_spellHoldRestoreRightFormID = 0;
	_spellBindDeviceSecond = RE::INPUT_DEVICE::kKeyboard;
	_spellBindIdCodeSecond = 0;
	_spellBindDevice = RE::INPUT_DEVICE::kKeyboard;
	_spellBindIdCode = 0;
}

void Wheeler::QueuePostCastRestore(
	RE::FormID a_spellFormID,
	RE::MagicSystem::CastingSource a_preferredSource,
	bool a_trackPrimaryLeft,
	bool a_trackSecondary,
	bool a_trackSecondaryLeft,
	bool a_restoreLeftHand,
	RE::FormID a_restoreLeftFormID,
	bool a_restoreRightHand,
	RE::FormID a_restoreRightFormID,
	float a_minDelaySeconds,
	float a_maxWaitSeconds)
{
	// Keep explicit restore intent even when target form is 0 (means restore empty hand).
	const bool restoreLeft = a_restoreLeftHand;
	const bool restoreRight = a_restoreRightHand;
	if (!restoreLeft && !restoreRight) {
		return;
	}

	const double now = ImGui::GetTime();
	const double minDelay = (std::max)(0.0f, static_cast<float>(a_minDelaySeconds));
	const double maxWait = (std::max)(0.30f, static_cast<float>(a_maxWaitSeconds));

	_spellPostCastRestorePending = true;
	_spellPostCastRestoreLeftHand = restoreLeft;
	_spellPostCastRestoreRightHand = restoreRight;
	_spellPostCastRestoreLeftFormID = restoreLeft ? a_restoreLeftFormID : 0;
	_spellPostCastRestoreRightFormID = restoreRight ? a_restoreRightFormID : 0;
	_spellPostCastRestoreSpellFormID = a_spellFormID;
	_spellPostCastRestorePreferredSource = a_preferredSource;
	_spellPostCastRestoreTrackPrimaryLeft = a_trackPrimaryLeft;
	_spellPostCastRestoreTrackSecondary = a_trackSecondary;
	_spellPostCastRestoreTrackSecondaryLeft = a_trackSecondaryLeft;
	_spellPostCastRestoreNoEarlierThan = now + minDelay;
	_spellPostCastRestoreForceAt = _spellPostCastRestoreNoEarlierThan + maxWait;
	_spellPostCastRestoreLastWaitLogTime = 0.0;
	_spellPostCastRestoreTrackedLeftOccupantFormID = 0;
	_spellPostCastRestoreTrackedRightOccupantFormID = 0;

	logger::info("[SpellPipe] post-cast restore queued left={:08X} right={:08X} minDelay={:.2f}s maxWait={:.2f}s spell={:08X} source={}",
		_spellPostCastRestoreLeftHand ? _spellPostCastRestoreLeftFormID : 0,
		_spellPostCastRestoreRightHand ? _spellPostCastRestoreRightFormID : 0,
		minDelay,
		maxWait,
		_spellPostCastRestoreSpellFormID,
		GetCastingSourceName(_spellPostCastRestorePreferredSource));
}

void Wheeler::ClearPostCastRestore()
{
	_spellPostCastRestorePending = false;
	_spellPostCastRestoreLeftHand = false;
	_spellPostCastRestoreRightHand = false;
	_spellPostCastRestoreLeftFormID = 0;
	_spellPostCastRestoreRightFormID = 0;
	_spellPostCastRestoreSpellFormID = 0;
	_spellPostCastRestorePreferredSource = RE::MagicSystem::CastingSource::kRightHand;
	_spellPostCastRestoreTrackPrimaryLeft = false;
	_spellPostCastRestoreTrackSecondary = false;
	_spellPostCastRestoreTrackSecondaryLeft = false;
	_spellPostCastRestoreNoEarlierThan = 0.0;
	_spellPostCastRestoreForceAt = 0.0;
	_spellPostCastRestoreLastWaitLogTime = 0.0;
	_spellPostCastRestoreTrackedLeftOccupantFormID = 0;
	_spellPostCastRestoreTrackedRightOccupantFormID = 0;
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

void Wheeler::ProcessQueuedExternalHotkeys()
{
	if (!_pendingExternalHotkeyDispatch.has_value()) {
		return;
	}

	const auto pending = *_pendingExternalHotkeyDispatch;
	auto* ui = RE::UI::GetSingleton();
	if (!ui) {
		return;
	}
	if (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
		if (Config::ActionHotkeysBridge::DebugLog) {
			logger::info("ActionHotkeysBridge: dispatch delayed reason=LoadingMenu slot={} tag={}", pending.sourceSlotIndex, pending.sourceTag);
		}
		return;
	}
	if (_editMode) {
		logger::info("ActionHotkeysBridge: dispatch blocked reason=EditModeAfterClose slot={} tag={}", pending.sourceSlotIndex, pending.sourceTag);
		_pendingExternalHotkeyDispatch.reset();
		return;
	}
	if (Controls::IsRebindActive()) {
		logger::info("ActionHotkeysBridge: dispatch blocked reason=RebindCaptureAfterClose slot={} tag={}", pending.sourceSlotIndex, pending.sourceTag);
		_pendingExternalHotkeyDispatch.reset();
		return;
	}
	if (Config::ActionHotkeysBridge::BlockConflictingWheelerHotkeys &&
		IsMkbToggleChordConflict(pending.scanCode, pending.modifier)) {
		logger::warn(
			"ActionHotkeysBridge: dispatch blocked reason=ToggleConflict slot={} tag={} hotkey='{}'",
			pending.sourceSlotIndex,
			pending.sourceTag,
			BuildExternalHotkeyLabel(pending.scanCode, pending.modifier));
		_pendingExternalHotkeyDispatch.reset();
		return;
	}

	const double now = ImGui::GetTime();
	const double cooldownSeconds =
		static_cast<double>(Config::ActionHotkeysBridge::DispatchCooldownMs) / 1000.0;
	if (_lastExternalHotkeyDispatchScanCode == pending.scanCode &&
		_lastExternalHotkeyDispatchModifier == pending.modifier &&
		(now - _lastExternalHotkeyDispatchTime) < cooldownSeconds) {
		logger::info(
			"ActionHotkeysBridge: dispatch blocked reason=Cooldown slot={} tag={} hotkey='{}'",
			pending.sourceSlotIndex,
			pending.sourceTag,
			BuildExternalHotkeyLabel(pending.scanCode, pending.modifier));
		_pendingExternalHotkeyDispatch.reset();
		return;
	}

	const auto baselineMenus = CaptureOpenMenuNames(ui);
	if (!DispatchExternalHotkeyNow(pending.scanCode, pending.modifier)) {
		logger::warn(
			"ActionHotkeysBridge: dispatch failed slot={} tag={} hotkey='{}'",
			pending.sourceSlotIndex,
			pending.sourceTag,
			BuildExternalHotkeyLabel(pending.scanCode, pending.modifier));
		_pendingExternalHotkeyDispatch.reset();
		return;
	}

	_lastExternalHotkeyDispatchTime = now;
	_lastExternalHotkeyDispatchScanCode = pending.scanCode;
	_lastExternalHotkeyDispatchModifier = pending.modifier;
	_actionHotkeysBridgeCloseAssist.active = Config::ActionHotkeysBridge::CloseAssistEnabled;
	_actionHotkeysBridgeCloseAssist.awaitingEscOutcome = false;
	_actionHotkeysBridgeCloseAssist.escPhysicalWasDown = IsPhysicalEscDown();
	_actionHotkeysBridgeCloseAssist.scanCode = pending.scanCode;
	_actionHotkeysBridgeCloseAssist.modifier = pending.modifier;
	_actionHotkeysBridgeCloseAssist.armedAt = now;
	_actionHotkeysBridgeCloseAssist.expiresAt =
		now + static_cast<double>(Config::ActionHotkeysBridge::CloseAssistTimeoutMs) / 1000.0;
	_actionHotkeysBridgeCloseAssist.evaluateAt = 0.0;
	_actionHotkeysBridgeCloseAssist.baselineMenus = baselineMenus;
	_actionHotkeysBridgeCloseAssist.trackedMenusAtEsc.clear();
	if (Config::ActionHotkeysBridge::DebugLog) {
		logger::info(
			"ActionHotkeysBridge: dispatch executed slot={} tag={} hotkey='{}'",
			pending.sourceSlotIndex,
			pending.sourceTag,
			BuildExternalHotkeyLabel(pending.scanCode, pending.modifier));
	}
	_pendingExternalHotkeyDispatch.reset();
}

bool Wheeler::BeginActionHotkeysBridgeCloseAssist(bool a_triggeredByGamepad)
{
	auto& state = _actionHotkeysBridgeCloseAssist;
	if (state.awaitingEscOutcome) {
		return true;
	}

	const double now = ImGui::GetTime();
	auto* ui = RE::UI::GetSingleton();
	state.trackedMenusAtEsc = CaptureNewMenuNamesSinceBaseline(ui, state.baselineMenus);
	if (!DispatchExternalHotkeyNow(0x01, 0)) {
		logger::warn("ActionHotkeysBridge: close assist failed to dispatch ESC fallback");
		ResetActionHotkeysBridgeCloseAssist();
		return false;
	}

	state.awaitingEscOutcome = true;
	state.evaluateAt = now + 0.35;
	if (Config::ActionHotkeysBridge::DebugLog) {
		logger::info(
			"ActionHotkeysBridge: close assist armed trigger={} trackedMenus={}",
			a_triggeredByGamepad ? "GamepadB" : "Esc",
			state.trackedMenusAtEsc.size());
	}
	return true;
}

bool Wheeler::TryTriggerActionHotkeysBridgeCloseAssist(std::uint32_t a_input, bool a_isGamePad)
{
	if (!Config::ActionHotkeysBridge::CloseAssistEnabled || _state != WheelState::KClosed) {
		return false;
	}

	auto& state = _actionHotkeysBridgeCloseAssist;
	if (!state.active) {
		return false;
	}

	const double now = ImGui::GetTime();
	if (now > state.expiresAt) {
		ResetActionHotkeysBridgeCloseAssist();
		return false;
	}

	const bool escTrigger =
		!a_isGamePad &&
		Config::ActionHotkeysBridge::CloseAssistUseEsc &&
		a_input == 0x01;
	const bool gamepadBTrigger =
		a_isGamePad &&
		Config::ActionHotkeysBridge::CloseAssistUseGamepadB &&
		a_input == 277;
	if (!escTrigger && !gamepadBTrigger) {
		return false;
	}

	return BeginActionHotkeysBridgeCloseAssist(gamepadBTrigger);
}

void Wheeler::UpdateActionHotkeysBridgeCloseAssist()
{
	auto& state = _actionHotkeysBridgeCloseAssist;
	if (!state.active) {
		return;
	}

	const double now = ImGui::GetTime();
	if (now > state.expiresAt) {
		if (Config::ActionHotkeysBridge::DebugLog) {
			logger::info("ActionHotkeysBridge: close assist expired");
		}
		ResetActionHotkeysBridgeCloseAssist();
		return;
	}

	if (Config::ActionHotkeysBridge::CloseAssistUseEsc && !state.awaitingEscOutcome) {
		const bool escDown = IsPhysicalEscDown();
		if (escDown && !state.escPhysicalWasDown) {
			BeginActionHotkeysBridgeCloseAssist(false);
		}
		state.escPhysicalWasDown = escDown;
	}

	if (!state.awaitingEscOutcome || now < state.evaluateAt) {
		return;
	}

	auto* ui = RE::UI::GetSingleton();
	if (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
		return;
	}

	if (AnyTrackedMenuClosed(ui, state.trackedMenusAtEsc)) {
		if (Config::ActionHotkeysBridge::DebugLog) {
			logger::info("ActionHotkeysBridge: close assist resolved with ESC");
		}
		ResetActionHotkeysBridgeCloseAssist();
		return;
	}

	if (!DispatchExternalHotkeyNow(state.scanCode, state.modifier)) {
		logger::warn(
			"ActionHotkeysBridge: close assist fallback toggle failed hotkey='{}'",
			BuildExternalHotkeyLabel(state.scanCode, state.modifier));
		ResetActionHotkeysBridgeCloseAssist();
		return;
	}

	if (Config::ActionHotkeysBridge::DebugLog) {
		logger::info(
			"ActionHotkeysBridge: close assist fallback toggle dispatched hotkey='{}' trackedMenus={}",
			BuildExternalHotkeyLabel(state.scanCode, state.modifier),
			state.trackedMenusAtEsc.size());
	}
	ResetActionHotkeysBridgeCloseAssist();
}

void Wheeler::ResetActionHotkeysBridgeCloseAssist()
{
	_actionHotkeysBridgeCloseAssist = {};
}

void Wheeler::ProcessPendingActions()
{
	WheelItemWeapon::ProcessIWSCompatTransfer();

	UpdateActionHotkeysBridgeCloseAssist();

	const bool hasPendingPostCloseWork =
		_pendingExternalHotkeyDispatch.has_value() ||
		_pendingSpellActivation.has_value() ||
		_spellHoldActive ||
		_pendingPowerFormID.has_value() ||
		_pendingShoutFormID.has_value() ||
		_pendingPoisonApplyFormID.has_value() ||
		_pendingMiscItemUse.has_value() ||
		_pendingSGTInstrumentSpellFormID.has_value() ||
		_pendingBookReadFormID.has_value() ||
		_pendingDepletedConsumablesCleanup;
	if (hasPendingPostCloseWork) {
		_forceCloseRequested = true;
	}

	// If any activation asked to force-close the wheel, do it once we're in an opened state.
	if (_forceCloseRequested) {
		if (_state == WheelState::KOpened) {
			TryCloseWheeler();
		} else if (_state == WheelState::KClosed && !hasPendingPostCloseWork) {
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
	if (wheelClosedAndPopupClear) {
		ProcessQueuedExternalHotkeys();
	}
	if (wheelClosedAndPopupClear && _lootMenuRestorePending) {
		auto* ui = RE::UI::GetSingleton();
		auto* queue = RE::UIMessageQueue::GetSingleton();
		if (ui) {
			for (std::size_t i = 0; i < kLootOverrideMenus.size(); ++i) {
				const std::string_view menuName = kLootOverrideMenus[i];
				if (_lootMenusMovieHiddenForOverride[i]) {
					const bool shown = SetMenuMovieVisibility(ui, menuName, true);
					logger::info("MainWheel[OpenClose]: Restoring {} visibility after Wheeler close (ok={})",
						menuName,
						shown ? 1 : 0);
				}
				if (_lootMenusClosedForOverride[i] && queue && !ui->IsMenuOpen(menuName)) {
					logger::info("MainWheel[OpenClose]: Restoring {} after Wheeler close", menuName);
					queue->AddMessage(menuName, RE::UI_MESSAGE_TYPE::kShow, nullptr);
				}
			}
			_lootMenusMovieHiddenForOverride.fill(false);
			_lootMenusClosedForOverride.fill(false);
			_lootMenuRestorePending = false;
		}
	}

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

			int pendingUsableCount = unlockedWords;
			if (pendingUsableCount <= 0 && unlockState.previousCount > 0) {
				pendingUsableCount = unlockState.previousCount;
			}
			const bool pendingWithUsableCount =
				(unlockState.computeState == ShoutUtils::UnlockComputeState::Pending) &&
				(pendingUsableCount > 0);

			if (unlockState.computeState != ShoutUtils::UnlockComputeState::Pending || pendingWithUsableCount) {
				s_shoutForcedUnlockedWords = pendingUsableCount;
				s_shoutForcedFromFallback = false;
				_pendingShoutFormID = shoutFormID;
				_pendingShoutHoverTime = hoverTime;
				s_shoutWaitUnlockActive = false;
				s_shoutWaitFormID = 0;
				s_shoutWaitHoverTime = 1.0f;
				s_shoutWaitElapsedSec = 0.0f;
				if (pendingWithUsableCount) {
					SHOUTPIPE("WaitUnlockPendingStable formID={:08X} unlocked={} method={} reason={}",
						shoutFormID, pendingUsableCount, detectionMethod, failReason);
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
				ArmDelayedShoutPostCastRestore("AfterKeyUp");
			}

			_shoutHoldActive = false;
			_shoutHoldFormID = 0;
			s_shoutHoldElapsedSec = 0.0f;
			logger::info("Shout: hold sequence completed");
			setShoutPipeState(ShoutPipeState::Idle, 0, "HoldCompleted");
		}
	}

	if (_shoutPostCastRestoreDelayActive &&
		_shoutPostCastRestorePending &&
		!_pendingShoutFormID.has_value() &&
		!s_shoutWaitUnlockActive &&
		!_shoutHoldActive) {
		_shoutPostCastRestoreDelayElapsedSec += shoutDt;
		if (_shoutPostCastRestoreDelayElapsedSec >= kShoutVoiceSelectionRestoreDelaySec) {
			TryRestoreQueuedShoutSelection("DelayedAfterKeyUp");
		}
	}

	// Process direct spell hold timer (concentration direct-cast): send delayed attack UP.
	if (_spellHoldActive) {
		const double now = ImGui::GetTime();
		double spellHoldCasterStartWaitMaxSec = std::clamp(
			(std::max)(
				_spellHoldChargeAwareRelease ? static_cast<double>(_spellHoldChargeAwareMaxTotalSeconds) : 0.0,
				(std::max)(2.20, static_cast<double>(_spellHoldDuration) + 1.90)),
			2.20,
			4.50);
		if (_spellHoldChargeAwareRelease) {
			// Mod-added ritual/extended startup spells often advertise little or no chargeTime.
			// Give charge-aware direct-cast more room before we abandon observed caster start.
			spellHoldCasterStartWaitMaxSec = std::clamp(
				(std::max)(
					(std::max)(6.00, static_cast<double>(_spellHoldDuration) + 2.40),
					static_cast<double>(_spellHoldChargeAwareMaxTotalSeconds)),
				3.00,
				9.50);
		}
		constexpr double kSpellHoldRetryPulseIntervalSec = 0.22;
		const auto spellHoldStartRetryMaxCount = static_cast<std::uint8_t>(std::clamp(
			static_cast<int>(std::floor(spellHoldCasterStartWaitMaxSec / kSpellHoldRetryPulseIntervalSec)) - 1,
			4,
			14));

		if (_spellHoldWaitForCasterStart) {
			bool casterStartedCharge = false;
			SpellCasterSnapshot matchedSnapshot{};
			std::array<SpellCasterSnapshot, 4> casterSnapshots{};
			auto* pc = RE::PlayerCharacter::GetSingleton();
			const bool hasMatchedCaster = CaptureSpellCasterSnapshots(
				pc,
				_spellHoldSpellFormID,
				_spellHoldCastingSource,
				matchedSnapshot,
				&casterSnapshots);
			if (hasMatchedCaster) {
				const bool timerActive = matchedSnapshot.castingTimer > 0.01f;
				casterStartedCharge = IsChargeLikeCasterState(matchedSnapshot.state) || timerActive;
				if (casterStartedCharge) {
					_spellHoldCastingSource = matchedSnapshot.source;
				}
			}
			if (!casterStartedCharge) {
				SpellCasterSnapshot sourceSnapshot{};
				if (TryGetActiveSnapshotForHeldSources(
					casterSnapshots,
					_spellHoldUseLeftAttack,
					_spellHoldAlsoReleaseSecondAttack,
					_spellHoldSecondUseLeftAttack,
					sourceSnapshot)) {
					casterStartedCharge = true;
					matchedSnapshot = sourceSnapshot;
					_spellHoldCastingSource = sourceSnapshot.source;
				}
			}
			if (_spellHoldRequirePrimaryCasterStart) {
				const auto* primarySnapshot = FindSnapshotBySource(casterSnapshots, _spellHoldPrimaryCastingSource);
				const bool primaryStartedCharge =
					primarySnapshot &&
					primarySnapshot->hasCaster &&
					primarySnapshot->currentSpellFormID == _spellHoldSpellFormID &&
					(IsChargeLikeCasterState(primarySnapshot->state) || primarySnapshot->castingTimer > 0.01f);

				if (primaryStartedCharge) {
					matchedSnapshot = *primarySnapshot;
					casterStartedCharge = true;
					_spellHoldCastingSource = _spellHoldPrimaryCastingSource;
				} else if (casterStartedCharge) {
					// Startup assist can spuriously start on the opposite hand. Drop assist and
					// continue waiting until the requested hand owns caster start.
					if (_spellHoldAlsoReleaseSecondAttack) {
						auto* controls = RE::PlayerControls::GetSingleton();
						auto* userEvents = RE::UserEvents::GetSingleton();
						if (controls && controls->attackBlockHandler && userEvents) {
							const auto& secondEvent = _spellHoldSecondUseLeftAttack ? userEvents->leftAttack : userEvents->rightAttack;
							if (!secondEvent.empty()) {
								auto* secondUp = RE::ButtonEvent::Create(
									_spellBindDeviceSecond,
									secondEvent,
									_spellBindIdCodeSecond,
									0.0f,
									0.03f);
								if (secondUp) {
									controls->attackBlockHandler->ProcessButton(secondUp, &controls->data);
									RE::free(secondUp);
								}
								logger::warn("[SpellPipe] startup assist dropped while waiting primary source requested={} observed={}",
									GetCastingSourceName(_spellHoldPrimaryCastingSource),
									GetCastingSourceName(matchedSnapshot.source));
							}
						}
						_spellHoldAlsoReleaseSecondAttack = false;
					}
					casterStartedCharge = false;
					_spellHoldCastingSource = _spellHoldPrimaryCastingSource;
				}
			}

			if (casterStartedCharge) {
				if (_spellHoldReleaseSecondOnCasterStart && _spellHoldAlsoReleaseSecondAttack) {
					auto* controls = RE::PlayerControls::GetSingleton();
					auto* userEvents = RE::UserEvents::GetSingleton();
					if (controls && controls->attackBlockHandler && userEvents) {
						const auto& secondEvent = _spellHoldSecondUseLeftAttack ? userEvents->leftAttack : userEvents->rightAttack;
						if (!secondEvent.empty()) {
							auto* secondUp = RE::ButtonEvent::Create(
								_spellBindDeviceSecond,
								secondEvent,
								_spellBindIdCodeSecond,
								0.0f,
								0.04f);
							if (secondUp) {
								controls->attackBlockHandler->ProcessButton(secondUp, &controls->data);
								RE::free(secondUp);
							}
							logger::info("[SpellPipe] startup assist released on caster start left={} device={} idCode={}",
								_spellHoldSecondUseLeftAttack ? 1 : 0,
								GetInputDeviceName(_spellBindDeviceSecond),
								_spellBindIdCodeSecond);
						}
					}
					_spellHoldAlsoReleaseSecondAttack = false;
				}
				if (_spellHoldStartAssistTapSent && _spellHoldAlsoReleaseSecondAttack) {
					auto* controls = RE::PlayerControls::GetSingleton();
					auto* userEvents = RE::UserEvents::GetSingleton();
					if (controls && controls->attackBlockHandler && userEvents) {
						const auto& assistEvent = _spellHoldSecondUseLeftAttack ? userEvents->leftAttack : userEvents->rightAttack;
						if (!assistEvent.empty()) {
							auto* assistUp = RE::ButtonEvent::Create(
								_spellBindDeviceSecond,
								assistEvent,
								_spellBindIdCodeSecond,
								0.0f,
								0.03f);
							if (assistUp) {
								controls->attackBlockHandler->ProcessButton(assistUp, &controls->data);
								RE::free(assistUp);
							}
							logger::info("[SpellPipe] hold start assist released on caster start left={} device={} idCode={}",
								_spellHoldSecondUseLeftAttack ? 1 : 0,
								GetInputDeviceName(_spellBindDeviceSecond),
								_spellBindIdCodeSecond);
						}
					}
					// Prevent extra second UP at final release since assist was already released.
					_spellHoldAlsoReleaseSecondAttack = false;
				}
				_spellHoldObservedCasterStart = true;
				_spellHoldStartTime = now;
				_spellHoldWaitForCasterStart = false;
				logger::info("[SpellPipe] hold timer started on caster state spell={:08X} source={} state={} timer={:.3f} holdSec={:.2f}",
					_spellHoldSpellFormID,
					GetCastingSourceName(_spellHoldCastingSource),
					GetMagicCasterStateName(matchedSnapshot.state),
					matchedSnapshot.castingTimer,
					_spellHoldDuration);
			} else {
				const double waitedSec = now - _spellHoldWaitStartTime;
				bool retriedStartPulse = false;
				const double nextPulseAt =
					static_cast<double>(_spellHoldStartRetryCount + 1) * kSpellHoldRetryPulseIntervalSec;
				if (_spellHoldStartRetryCount < spellHoldStartRetryMaxCount &&
					waitedSec >= nextPulseAt &&
					now - _spellHoldLastRetryPulseTime >= (kSpellHoldRetryPulseIntervalSec * 0.80)) {
					if (auto* retryPc = RE::PlayerCharacter::GetSingleton(); retryPc) {
						// Reassert draw state while retrying synthetic cast start pulses.
						ActorVirtualCompat::DrawWeaponMagicHands(retryPc, true);
					}
					auto* controls = RE::PlayerControls::GetSingleton();
					auto* userEvents = RE::UserEvents::GetSingleton();
					if (controls && controls->attackBlockHandler && userEvents) {
						auto sendAttackPulse = [&](bool useLeftAttack, RE::INPUT_DEVICE bindDevice, std::uint32_t bindIdCode, const char* tag, bool releaseAfterPulse) {
							const auto& attackEvent = useLeftAttack ? userEvents->leftAttack : userEvents->rightAttack;
							if (attackEvent.empty()) {
								return false;
							}

							auto* upEvent = RE::ButtonEvent::Create(bindDevice, attackEvent, bindIdCode, 0.0f, 0.0f);
							if (upEvent) {
								controls->attackBlockHandler->ProcessButton(upEvent, &controls->data);
								RE::free(upEvent);
							}

							auto* downEvent = RE::ButtonEvent::Create(bindDevice, attackEvent, bindIdCode, 1.0f, 0.0f);
							if (!downEvent) {
								logger::warn("[SpellPipe] hold start retry down alloc failed{}", tag ? tag : "");
								return false;
							}
							controls->attackBlockHandler->ProcessButton(downEvent, &controls->data);
							RE::free(downEvent);
							if (releaseAfterPulse) {
								auto* tapUpEvent = RE::ButtonEvent::Create(bindDevice, attackEvent, bindIdCode, 0.0f, 0.03f);
								if (tapUpEvent) {
									controls->attackBlockHandler->ProcessButton(tapUpEvent, &controls->data);
									RE::free(tapUpEvent);
								}
							}
							logger::info("[SpellPipe] hold start retry sent attack pulse{} (left={} device={} idCode={})",
								tag ? tag : "",
								useLeftAttack ? 1 : 0,
								GetInputDeviceName(bindDevice),
								bindIdCode);
							return true;
						};

						bool retryUseLeftAttack = _spellHoldUseLeftAttack;
						RE::INPUT_DEVICE retryDevice = _spellBindDevice;
						std::uint32_t retryIdCode = _spellBindIdCode;
						AttackBindingCandidate primaryFallback{};
						const bool hasPrimaryFallback =
							ResolveFallbackAttackBinding(
								_spellHoldUseLeftAttack,
								_spellBindDevice,
								_spellBindIdCode,
								false,
								false,
								primaryFallback);
						if (hasPrimaryFallback) {
							retryUseLeftAttack = primaryFallback.useLeftAttack;
							retryDevice = primaryFallback.device;
							retryIdCode = primaryFallback.idCode;
						}

						bool pulseOk = sendAttackPulse(retryUseLeftAttack, retryDevice, retryIdCode, "", false);
						if (pulseOk) {
							if (hasPrimaryFallback) {
								logger::warn("[SpellPipe] hold start retry switched primary binding event={} device={} idCode={} (prevEvent={} prevDevice={} prevIdCode={})",
									retryUseLeftAttack ? "leftAttack" : "rightAttack",
									GetInputDeviceName(retryDevice),
									retryIdCode,
									_spellHoldUseLeftAttack ? "leftAttack" : "rightAttack",
									GetInputDeviceName(_spellBindDevice),
									_spellBindIdCode);
							}
							_spellHoldUseLeftAttack = retryUseLeftAttack;
							_spellBindDevice = retryDevice;
							_spellBindIdCode = retryIdCode;
							_spellHoldCastingSource = _spellHoldUseLeftAttack ?
								RE::MagicSystem::CastingSource::kLeftHand :
								RE::MagicSystem::CastingSource::kRightHand;
						}

						if (pulseOk && _spellHoldAlsoReleaseSecondAttack && !_spellHoldReleaseSecondOnCasterStart) {
							bool retryUseLeftAttackSecond = _spellHoldSecondUseLeftAttack;
							RE::INPUT_DEVICE retryDeviceSecond = _spellBindDeviceSecond;
							std::uint32_t retryIdCodeSecond = _spellBindIdCodeSecond;
							AttackBindingCandidate secondFallback{};
							const bool hasSecondFallback =
								ResolveFallbackAttackBinding(
									_spellHoldSecondUseLeftAttack,
									_spellBindDeviceSecond,
									_spellBindIdCodeSecond,
									false,
									false,
									secondFallback);
							if (hasSecondFallback) {
								retryUseLeftAttackSecond = secondFallback.useLeftAttack;
								retryDeviceSecond = secondFallback.device;
								retryIdCodeSecond = secondFallback.idCode;
							}

							pulseOk = sendAttackPulse(retryUseLeftAttackSecond, retryDeviceSecond, retryIdCodeSecond, " (second)", false);
							if (pulseOk) {
								if (hasSecondFallback) {
									logger::warn("[SpellPipe] hold start retry switched secondary binding event={} device={} idCode={} (prevEvent={} prevDevice={} prevIdCode={})",
										retryUseLeftAttackSecond ? "leftAttack" : "rightAttack",
										GetInputDeviceName(retryDeviceSecond),
										retryIdCodeSecond,
										_spellHoldSecondUseLeftAttack ? "leftAttack" : "rightAttack",
										GetInputDeviceName(_spellBindDeviceSecond),
										_spellBindIdCodeSecond);
								}
								_spellHoldSecondUseLeftAttack = retryUseLeftAttackSecond;
								_spellBindDeviceSecond = retryDeviceSecond;
								_spellBindIdCodeSecond = retryIdCodeSecond;
							}
						}
						if (pulseOk &&
							_spellHoldStartAssistTapEnabled &&
							!_spellHoldStartAssistTapSent &&
							waitedSec >= (kSpellHoldRetryPulseIntervalSec * 2.0)) {
							const bool assistPulseOk = sendAttackPulse(
								_spellHoldStartAssistTapUseLeftAttack,
								_spellHoldStartAssistTapDevice,
								_spellHoldStartAssistTapIdCode,
								" (assist)",
								false);
							if (assistPulseOk) {
								// Keep assist input held until caster start is observed.
								_spellHoldAlsoReleaseSecondAttack = true;
								_spellHoldSecondUseLeftAttack = _spellHoldStartAssistTapUseLeftAttack;
								_spellBindDeviceSecond = _spellHoldStartAssistTapDevice;
								_spellBindIdCodeSecond = _spellHoldStartAssistTapIdCode;
							}
							_spellHoldStartAssistTapSent = assistPulseOk;
							logger::warn("[SpellPipe] hold start assist hold sent={} left={} device={} idCode={} waited={:.2f}s",
								assistPulseOk ? 1 : 0,
								_spellHoldStartAssistTapUseLeftAttack ? 1 : 0,
								GetInputDeviceName(_spellHoldStartAssistTapDevice),
								_spellHoldStartAssistTapIdCode,
								waitedSec);
						}

						if (pulseOk) {
							_spellHoldStartRetryCount = static_cast<std::uint8_t>(_spellHoldStartRetryCount + 1);
							_spellHoldLastRetryPulseTime = now;
							logger::warn("[SpellPipe] hold start wait timeout spell={:08X} waited={:.2f}s preferredSource={}, retrying attack pulse ({}/{})",
								_spellHoldSpellFormID,
								waitedSec,
								GetCastingSourceName(_spellHoldCastingSource),
								_spellHoldStartRetryCount,
								spellHoldStartRetryMaxCount);
							retriedStartPulse = true;
						}
					}
				}

				if (!retriedStartPulse && waitedSec >= spellHoldCasterStartWaitMaxSec) {
					// If caster start cannot be observed after retry budget, restart countdown from timeout boundary.
					_spellHoldWaitForCasterStart = false;
					_spellHoldStartTime = now;
					_spellHoldUsedFallbackStart = true;
					logger::warn("[SpellPipe] hold start wait timeout spell={:08X} waited={:.2f}s preferredSource={}, using fallback timer",
						_spellHoldSpellFormID, waitedSec, GetCastingSourceName(_spellHoldCastingSource));
					for (const auto& snapshot : casterSnapshots) {
						if (!snapshot.hasCaster) {
							logger::warn("[SpellPipe] hold start timeout snapshot source={} hasCaster=0",
								GetCastingSourceName(snapshot.source));
							continue;
						}
						logger::warn("[SpellPipe] hold start timeout snapshot source={} currentSpell={:08X} state={} timer={:.3f}",
							GetCastingSourceName(snapshot.source),
							snapshot.currentSpellFormID,
							GetMagicCasterStateName(snapshot.state),
							snapshot.castingTimer);
					}
				}
			}
		}

		if (_spellHoldChargeAwareRelease &&
			_spellHoldSpellFormID != 0 &&
			!_spellHoldWaitForCasterStart &&
			now - _spellHoldStartTime >= static_cast<double>(_spellHoldDuration)) {
			bool keepHoldingForCharge = false;
			RE::MagicSystem::CastingSource observedSource = _spellHoldCastingSource;
			RE::MagicCaster::State observedState = RE::MagicCaster::State::kNone;
			float observedCastingTimer = 0.0f;
			RE::FormID observedCurrentSpellFormID = 0;

			SpellCasterSnapshot matchedSnapshot{};
			std::array<SpellCasterSnapshot, 4> casterSnapshots{};
			auto* pc = RE::PlayerCharacter::GetSingleton();
			const bool hasMatchedCaster = CaptureSpellCasterSnapshots(
				pc,
				_spellHoldSpellFormID,
				_spellHoldCastingSource,
				matchedSnapshot,
				&casterSnapshots);
			if (hasMatchedCaster) {
				_spellHoldCastingSource = matchedSnapshot.source;
				observedSource = matchedSnapshot.source;
				observedState = matchedSnapshot.state;
				observedCastingTimer = matchedSnapshot.castingTimer;
				observedCurrentSpellFormID = matchedSnapshot.currentSpellFormID;
				const bool timerActive = observedCastingTimer > 0.01f;
				keepHoldingForCharge = IsChargeLikeCasterState(observedState) || timerActive;
			}
			if (!keepHoldingForCharge) {
				SpellCasterSnapshot sourceSnapshot{};
				if (TryGetActiveSnapshotForHeldSources(
					casterSnapshots,
					_spellHoldUseLeftAttack,
					_spellHoldAlsoReleaseSecondAttack,
					_spellHoldSecondUseLeftAttack,
					sourceSnapshot)) {
					_spellHoldCastingSource = sourceSnapshot.source;
					observedSource = sourceSnapshot.source;
					observedState = sourceSnapshot.state;
					observedCastingTimer = sourceSnapshot.castingTimer;
					observedCurrentSpellFormID = sourceSnapshot.currentSpellFormID;
					keepHoldingForCharge = true;
				} else {
					const auto* preferredSnapshot = FindSnapshotBySource(casterSnapshots, _spellHoldCastingSource);
					if (preferredSnapshot && preferredSnapshot->hasCaster) {
						observedSource = preferredSnapshot->source;
						observedState = preferredSnapshot->state;
						observedCastingTimer = preferredSnapshot->castingTimer;
						observedCurrentSpellFormID = preferredSnapshot->currentSpellFormID;
					}
				}
			}

			const double heldTotal = now - _spellHoldWaitStartTime;
			const double maxTotal = static_cast<double>((std::max)(_spellHoldDuration + 0.25f, _spellHoldChargeAwareMaxTotalSeconds));

			if (keepHoldingForCharge && heldTotal < maxTotal) {
				_spellHoldObservedChargeActivity = true;
				if (_spellHoldChargeAwareLastLogTime <= 0.0 ||
					now - _spellHoldChargeAwareLastLogTime >= 0.12) {
					_spellHoldChargeAwareLastLogTime = now;
					logger::info("[SpellPipe] charge-monitor hold spell={:08X} source={} currentSpell={:08X} state={} timer={:.3f} held={:.2f}/{:.2f}",
						_spellHoldSpellFormID,
						GetCastingSourceName(observedSource),
						observedCurrentSpellFormID,
						GetMagicCasterStateName(observedState),
						observedCastingTimer,
						heldTotal,
						maxTotal);
				}
			} else if (keepHoldingForCharge && heldTotal >= maxTotal) {
				logger::warn("[SpellPipe] charge-monitor max hold reached spell={:08X} source={} currentSpell={:08X} state={} timer={:.3f} held={:.2f}/{:.2f}",
					_spellHoldSpellFormID,
					GetCastingSourceName(observedSource),
					observedCurrentSpellFormID,
					GetMagicCasterStateName(observedState),
					observedCastingTimer,
					heldTotal,
					maxTotal);
			} else {
				logger::info("[SpellPipe] charge-monitor release spell={:08X} source={} currentSpell={:08X} state={} timer={:.3f} held={:.2f}/{:.2f}",
					_spellHoldSpellFormID,
					GetCastingSourceName(observedSource),
					observedCurrentSpellFormID,
					GetMagicCasterStateName(observedState),
					observedCastingTimer,
					heldTotal,
					maxTotal);
			}

			if (keepHoldingForCharge && heldTotal < maxTotal) {
				return;
			}
		}

		if (!_spellHoldWaitForCasterStart &&
			now - _spellHoldStartTime >= static_cast<double>(_spellHoldDuration)) {
			auto* controls = RE::PlayerControls::GetSingleton();
			auto* userEvents = RE::UserEvents::GetSingleton();
			auto sendAttackUp = [&](bool useLeftAttack, RE::INPUT_DEVICE bindDevice, std::uint32_t bindIdCode, const char* tag) {
				const auto& attackEvent = (useLeftAttack && userEvents) ? userEvents->leftAttack :
					(userEvents ? userEvents->rightAttack : RE::BSFixedString{});
				if (controls && controls->attackBlockHandler && userEvents && !attackEvent.empty()) {
					auto* upEvent = RE::ButtonEvent::Create(bindDevice, attackEvent, bindIdCode, 0.0f, _spellHoldDuration);
					if (upEvent) {
						controls->attackBlockHandler->ProcessButton(upEvent, &controls->data);
						RE::free(upEvent);
					}
					logger::info("[SpellPipe] sent attack UP{} (left={}, holdSec={:.2f})",
						tag ? tag : "", useLeftAttack ? 1 : 0, _spellHoldDuration);
					return;
				}
				logger::warn("[SpellPipe] attack UP{} skipped: controls={} attackHandler={} userEvents={} eventEmpty={}",
					tag ? tag : "",
					controls ? 1 : 0,
					(controls && controls->attackBlockHandler) ? 1 : 0,
					userEvents ? 1 : 0,
					attackEvent.empty() ? 1 : 0);
			};

			sendAttackUp(_spellHoldUseLeftAttack, _spellBindDevice, _spellBindIdCode, "");
			if (_spellHoldAlsoReleaseSecondAttack) {
				sendAttackUp(_spellHoldSecondUseLeftAttack, _spellBindDeviceSecond, _spellBindIdCodeSecond, " (second)");
			}

			if (_spellHoldRestoreHandsAfterRelease) {
				const bool requireObservedStart = _spellHoldChargeAwareRelease;
				const bool canRestoreSafely =
					!requireObservedStart ||
					(_spellHoldObservedCasterStart && !_spellHoldUsedFallbackStart);
				if (canRestoreSafely) {
					const float restoreDelaySec = _spellHoldChargeAwareRelease ? 0.90f : 0.25f;
					const float restoreMaxWaitSec = _spellHoldChargeAwareRelease ? 2.80f : 1.20f;
					float restoreDelayDynamicSec = restoreDelaySec;
					float restoreMaxWaitDynamicSec = restoreMaxWaitSec;
					const float observedHoldTotalSec = static_cast<float>(now - _spellHoldWaitStartTime);
					// Long ritual-style spells can keep finalizing after caster-state looks ready.
					// Extend restore not only for 2H casts, but also for long single-hand mod spells.
					if (_spellHoldChargeAwareRelease) {
						const float effectiveHoldSec = (std::max)(_spellHoldDuration, observedHoldTotalSec);
						if (effectiveHoldSec >= 3.00f) {
							const float delayScale = _spellHoldAlsoReleaseSecondAttack ? 0.60f : 0.50f;
							const float delayMin = _spellHoldAlsoReleaseSecondAttack ? 1.40f : 1.20f;
							const float delayMax = _spellHoldAlsoReleaseSecondAttack ? 2.40f : 2.80f;
							const float waitPad = _spellHoldAlsoReleaseSecondAttack ? 3.20f : 3.40f;
							const float waitMin = _spellHoldAlsoReleaseSecondAttack ? 4.50f : 4.60f;
							const float waitMax = _spellHoldAlsoReleaseSecondAttack ? 8.00f : 9.50f;
							restoreDelayDynamicSec = std::clamp(effectiveHoldSec * delayScale, delayMin, delayMax);
							restoreMaxWaitDynamicSec = std::clamp(restoreDelayDynamicSec + waitPad, waitMin, waitMax);
							logger::info("[SpellPipe] post-cast restore delay extended spell={:08X} holdSec={:.2f} observedHold={:.2f}s delay={:.2f}s maxWait={:.2f}s dual={}",
							_spellHoldSpellFormID,
							_spellHoldDuration,
							effectiveHoldSec,
							restoreDelayDynamicSec,
							restoreMaxWaitDynamicSec,
							_spellHoldAlsoReleaseSecondAttack ? 1 : 0);
						}
					}
					QueuePostCastRestore(
						_spellHoldSpellFormID,
						_spellHoldCastingSource,
						_spellHoldUseLeftAttack,
						_spellHoldAlsoReleaseSecondAttack,
						_spellHoldSecondUseLeftAttack,
						_spellHoldRestoreLeftHand,
						_spellHoldRestoreLeftFormID,
						_spellHoldRestoreRightHand,
						_spellHoldRestoreRightFormID,
						restoreDelayDynamicSec,
						restoreMaxWaitDynamicSec);
				} else {
					const float conservativeDelaySec = _spellHoldChargeAwareRelease ?
						std::clamp((std::max)(1.20f, _spellHoldChargeAwareMaxTotalSeconds), 1.20f, 8.00f) :
						0.40f;
					const float conservativeMaxWaitSec = _spellHoldChargeAwareRelease ?
						std::clamp(conservativeDelaySec + 2.20f, 2.80f, 10.00f) :
						1.50f;
					QueuePostCastRestore(
						_spellHoldSpellFormID,
						_spellHoldCastingSource,
						_spellHoldUseLeftAttack,
						_spellHoldAlsoReleaseSecondAttack,
						_spellHoldSecondUseLeftAttack,
						_spellHoldRestoreLeftHand,
						_spellHoldRestoreLeftFormID,
						_spellHoldRestoreRightHand,
						_spellHoldRestoreRightFormID,
						conservativeDelaySec,
						conservativeMaxWaitSec);
					logger::warn("[SpellPipe] post-cast restore fallback queued (caster start unobserved): spell={:08X} source={} chargeAware={} startObserved={} fallbackStart={} chargeObserved={} delay={:.2f}s maxWait={:.2f}s",
						_spellHoldSpellFormID,
						GetCastingSourceName(_spellHoldCastingSource),
						_spellHoldChargeAwareRelease ? 1 : 0,
						_spellHoldObservedCasterStart ? 1 : 0,
						_spellHoldUsedFallbackStart ? 1 : 0,
						_spellHoldObservedChargeActivity ? 1 : 0,
						conservativeDelaySec,
						conservativeMaxWaitSec);
				}
			}

			ClearSpellHoldRelease();
		}
	}

	// Process deferred post-cast restore only after a short delay and caster-idle observation.
	if (_spellPostCastRestorePending) {
		const double now = ImGui::GetTime();
		if (now >= _spellPostCastRestoreNoEarlierThan) {
			RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
			if (!pc || !pc->Is3DLoaded()) {
				logger::warn("[SpellPipe] post-cast restore skipped: player unavailable");
				ClearPostCastRestore();
			} else {
				SpellCasterSnapshot matchedSnapshot{};
				std::array<SpellCasterSnapshot, 4> casterSnapshots{};
				CaptureSpellCasterSnapshots(
					pc,
					_spellPostCastRestoreSpellFormID,
					_spellPostCastRestorePreferredSource,
					matchedSnapshot,
					&casterSnapshots);

				bool casterStillActive = false;
				RE::MagicSystem::CastingSource observedSource = _spellPostCastRestorePreferredSource;
				RE::MagicCaster::State observedState = RE::MagicCaster::State::kNone;
				float observedTimer = 0.0f;
				RE::FormID observedSpell = 0;

				if (_spellPostCastRestoreSpellFormID != 0 &&
					matchedSnapshot.hasCaster &&
					matchedSnapshot.currentSpellFormID == _spellPostCastRestoreSpellFormID) {
					const bool active = IsChargeLikeCasterState(matchedSnapshot.state) || matchedSnapshot.castingTimer > 0.01f;
					if (active) {
						casterStillActive = true;
						observedSource = matchedSnapshot.source;
						observedState = matchedSnapshot.state;
						observedTimer = matchedSnapshot.castingTimer;
						observedSpell = matchedSnapshot.currentSpellFormID;
					}
				}

				if (!casterStillActive) {
					SpellCasterSnapshot trackedSnapshot{};
					const bool hasTrackedActive = TryGetActiveSnapshotForHeldSources(
						casterSnapshots,
						_spellPostCastRestoreTrackPrimaryLeft,
						_spellPostCastRestoreTrackSecondary,
						_spellPostCastRestoreTrackSecondaryLeft,
						trackedSnapshot);
					if (hasTrackedActive) {
						const bool sameSpellOrUnknown =
							_spellPostCastRestoreSpellFormID == 0 ||
							trackedSnapshot.currentSpellFormID == _spellPostCastRestoreSpellFormID;
						if (sameSpellOrUnknown) {
							casterStillActive = true;
							observedSource = trackedSnapshot.source;
							observedState = trackedSnapshot.state;
							observedTimer = trackedSnapshot.castingTimer;
							observedSpell = trackedSnapshot.currentSpellFormID;
						}
					}
				}

				if (casterStillActive && now < _spellPostCastRestoreForceAt) {
					if (_spellPostCastRestoreLastWaitLogTime <= 0.0 ||
						now - _spellPostCastRestoreLastWaitLogTime >= 0.15) {
						_spellPostCastRestoreLastWaitLogTime = now;
						logger::info("[SpellPipe] post-cast restore waiting spell={:08X} source={} state={} timer={:.3f} currentSpell={:08X} remaining={:.2f}s",
							_spellPostCastRestoreSpellFormID,
							GetCastingSourceName(observedSource),
							GetMagicCasterStateName(observedState),
							observedTimer,
							observedSpell,
							(std::max)(0.0, _spellPostCastRestoreForceAt - now));
					}
				} else {
					enum class PostCastRestoreHandAction : std::uint8_t
					{
						RestoreNow,
						Wait,
						Cancel
					};

					auto resolvePersistentHandItem = [&](bool isLeft,
													 bool& restoreHand,
													 RE::FormID restoreFormID,
													 RE::FormID& trackedOccupantFormID) {
						if (!restoreHand) {
							return PostCastRestoreHandAction::Cancel;
						}

						RE::TESForm* currentEquipped = pc->GetEquippedObject(isLeft);
						const RE::FormID currentFormID = currentEquipped ? currentEquipped->GetFormID() : 0;
						const char* handName = isLeft ? "LEFT" : "RIGHT";

						if (trackedOccupantFormID != 0) {
							if (currentFormID == trackedOccupantFormID) {
								if (_spellPostCastRestoreLastWaitLogTime <= 0.0 ||
									now - _spellPostCastRestoreLastWaitLogTime >= 0.15) {
									_spellPostCastRestoreLastWaitLogTime = now;
									logger::info("[SpellPipe] post-cast restore waiting {} persistentForm={:08X} spell={:08X}",
										handName,
										trackedOccupantFormID,
										_spellPostCastRestoreSpellFormID);
								}
								return PostCastRestoreHandAction::Wait;
							}
							if (currentFormID == 0) {
								logger::info("[SpellPipe] post-cast restore {} resumed after persistent item cleared formID={:08X}",
									handName,
									trackedOccupantFormID);
								trackedOccupantFormID = 0;
								return PostCastRestoreHandAction::RestoreNow;
							}

							logger::info("[SpellPipe] post-cast restore {} cancelled after persistent item changed tracked={:08X} current={:08X}",
								handName,
								trackedOccupantFormID,
								currentFormID);
							trackedOccupantFormID = 0;
							restoreHand = false;
							return PostCastRestoreHandAction::Cancel;
						}

						const bool unexpectedOccupied =
							currentFormID != 0 &&
							currentFormID != restoreFormID &&
							currentFormID != _spellPostCastRestoreSpellFormID;
						const bool looksPersistentHandItem =
							currentEquipped &&
							(currentEquipped->As<RE::TESObjectWEAP>() ||
								currentEquipped->As<RE::TESObjectARMO>() ||
								currentEquipped->As<RE::TESObjectLIGH>());
						if (unexpectedOccupied && looksPersistentHandItem) {
							trackedOccupantFormID = currentFormID;
							logger::info("[SpellPipe] post-cast restore {} waiting on persistent hand item formID={:08X} restoreTarget={:08X} spell={:08X}",
								handName,
								trackedOccupantFormID,
								restoreFormID,
								_spellPostCastRestoreSpellFormID);
							return PostCastRestoreHandAction::Wait;
						}

						return PostCastRestoreHandAction::RestoreNow;
					};

					if (_spellPostCastRestoreRightHand) {
						const auto rightAction = resolvePersistentHandItem(
							false,
							_spellPostCastRestoreRightHand,
							_spellPostCastRestoreRightFormID,
							_spellPostCastRestoreTrackedRightOccupantFormID);
						if (_spellPostCastRestoreRightHand && rightAction == PostCastRestoreHandAction::RestoreNow) {
							if (_spellPostCastRestoreRightFormID == 0) {
								if (pc->GetEquippedObject(false) != nullptr) {
									Utils::Slot::CleanSlot(pc, Utils::Slot::GetRightHandSlot());
									logger::info("[SpellPipe] post-cast restore RIGHT cleared to empty");
								}
							} else {
								const bool alreadyEquipped = [&]() {
									if (auto* rightEquipped = pc->GetEquippedObject(false)) {
										return rightEquipped->GetFormID() == _spellPostCastRestoreRightFormID;
									}
									return false;
								}();
								if (!alreadyEquipped) {
									const bool ok = EquipFormToHand(pc, _spellPostCastRestoreRightFormID, HandMemoryHand::Right);
									logger::info("[SpellPipe] post-cast restore RIGHT formID={:08X} ok={}",
										_spellPostCastRestoreRightFormID,
										ok ? 1 : 0);
								}
							}
							_spellPostCastRestoreRightHand = false;
							_spellPostCastRestoreTrackedRightOccupantFormID = 0;
						}
					}
					if (_spellPostCastRestoreLeftHand) {
						const auto leftAction = resolvePersistentHandItem(
							true,
							_spellPostCastRestoreLeftHand,
							_spellPostCastRestoreLeftFormID,
							_spellPostCastRestoreTrackedLeftOccupantFormID);
						if (_spellPostCastRestoreLeftHand && leftAction == PostCastRestoreHandAction::RestoreNow) {
							if (_spellPostCastRestoreLeftFormID == 0) {
								if (pc->GetEquippedObject(true) != nullptr) {
									Utils::Slot::CleanSlot(pc, Utils::Slot::GetLeftHandSlot());
									logger::info("[SpellPipe] post-cast restore LEFT cleared to empty");
								}
							} else {
								const bool alreadyEquipped = [&]() {
									if (auto* leftEquipped = pc->GetEquippedObject(true)) {
										return leftEquipped->GetFormID() == _spellPostCastRestoreLeftFormID;
									}
									return false;
								}();
								if (!alreadyEquipped) {
									const bool ok = EquipFormToHand(pc, _spellPostCastRestoreLeftFormID, HandMemoryHand::Left);
									logger::info("[SpellPipe] post-cast restore LEFT formID={:08X} ok={}",
										_spellPostCastRestoreLeftFormID,
										ok ? 1 : 0);
								}
							}
							_spellPostCastRestoreLeftHand = false;
							_spellPostCastRestoreTrackedLeftOccupantFormID = 0;
						}
					}
					if (!_spellPostCastRestoreLeftHand && !_spellPostCastRestoreRightHand) {
						ClearPostCastRestore();
					}
				}
			}
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
		const bool restoreVoiceSelectionPending = _pendingPowerRestorePending;
		const RE::FormID restoreVoiceFormID = _pendingPowerRestoreFormID;
		_pendingPowerRestorePending = false;
		_pendingPowerRestoreFormID = 0;

		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			logger::warn("[PowerPipe] pending activation failed: no player");
		} else {
			RE::SpellItem* powerSpell = RE::TESForm::LookupByID<RE::SpellItem>(powerFormID);
			if (!powerSpell) {
				logger::warn("[PowerPipe] pending activation failed: LookupByID {:08X}", powerFormID);
			} else if (IsSpellBlockedByTransformGuard(powerSpell, "PowerTask")) {
				logger::warn("[PowerPipe] pending activation blocked by transform guard formID={:08X}", powerFormID);
			} else if (!IsPowerSpellType(powerSpell)) {
				logger::warn("[PowerPipe] pending activation skipped: form {:08X} is not power type (spellType={})",
					powerFormID, static_cast<int>(powerSpell->GetSpellType()));
			} else {
				bool canScheduleActivation = true;
				const RE::FormID selectedVoiceFormID = GetSelectedVoiceFormID(pc);
				if (selectedVoiceFormID != powerFormID) {
					const bool equipped = TrySetVoiceSelection(pc, powerFormID, "PowerTempSelect");
					logger::info(
						"[PowerPipe] temporary power select previous={:08X} requested={:08X} equipped={}",
						selectedVoiceFormID,
						powerFormID,
						equipped ? 1 : 0);
					if (!equipped) {
						logger::warn("[PowerPipe] pending activation failed: temporary voice select failed requested={:08X}",
							powerFormID);
						canScheduleActivation = false;
					}
				}

				if (canScheduleActivation) {
					// Run activate on next task tick so close state is committed before firing.
					SKSE::GetTaskInterface()->AddTask([powerFormID, restoreVoiceSelectionPending, restoreVoiceFormID]() {
						RE::PlayerCharacter* taskPc = RE::PlayerCharacter::GetSingleton();
						if (!taskPc) {
							logger::warn("[PowerPipe] activate task failed: no player");
							return;
						}

						auto tryRestore = [&](const char* reason) {
							if (!restoreVoiceSelectionPending) {
								return;
							}
							const bool restored = TryRestoreVoiceSelection(taskPc, restoreVoiceFormID, powerFormID, "PowerRestore");
							logger::info("[PowerPipe] restore attempted reason={} restored={} previous={:08X} requested={:08X}",
								reason ? reason : "",
								restored ? 1 : 0,
								restoreVoiceFormID,
								powerFormID);
						};

						RE::UI* ui = RE::UI::GetSingleton();
						if (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
							logger::warn("[PowerPipe] activate task skipped: loading menu open");
							tryRestore("LoadingMenu");
							return;
						}

						const bool activated = TryActivateEquippedShoutOrPowerVanilla(taskPc, powerFormID);
						tryRestore(activated ? "AfterActivate" : "ActivateFailed");
						if (!activated) {
							logger::warn("[PowerPipe] activate task failed formID={:08X}", powerFormID);
							return;
						}

						logger::info("[PowerPipe] activate task succeeded formID={:08X}", powerFormID);
					});
				}
			}
		}
	}

	// Process pending direct spell activation through vanilla attack input pipeline.
	if (_pendingSpellActivation.has_value() && !_shoutHoldActive && !_spellHoldActive) {
		constexpr float kDirectTapReleaseMinSec = 0.12f;
		constexpr float kDirectTapChargeBufferSec = 0.08f;
		constexpr float kDirectTapRitualChargeThresholdSec = 2.50f;
		constexpr float kDirectTapRitualSafetyBufferSec = 0.60f;
		constexpr float kDirectTapReleaseMaxSec = 4.50f;
		constexpr float kDirectTapChargeAwareMinTotalSec = 6.00f;
		constexpr float kDirectTapChargeAwareBasePaddingSec = 1.25f;
		constexpr float kDirectTapChargeAwarePerChargeMultiplier = 4.00f;
		constexpr float kDirectTapChargeAwareMaxTotalSec = 12.00f;
		constexpr std::uint8_t kSpellReadyRetryFrameBudget = 240;
		constexpr std::uint8_t kPostEquipSettleFrameBudget = 8;
		auto& pending = *_pendingSpellActivation;

		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			logger::warn("[SpellPipe] pending activation failed: no player");
			_pendingSpellActivation.reset();
		} else {
			RE::UI* ui = RE::UI::GetSingleton();
			if (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
				logger::warn("[SpellPipe] pending activation delayed: loading menu open");
			} else {
					RE::SpellItem* spell = RE::TESForm::LookupByID<RE::SpellItem>(pending.formID);
					if (!spell) {
						logger::warn("[SpellPipe] pending activation failed: LookupByID {:08X}", pending.formID);
						_pendingSpellActivation.reset();
					} else if (IsSpellBlockedByTransformGuard(spell, "SpellTask")) {
						logger::warn("[SpellPipe] pending activation blocked by transform guard formID={:08X}", pending.formID);
						_pendingSpellActivation.reset();
					} else if (IsPowerSpellType(spell)) {
						logger::warn("[SpellPipe] pending activation skipped: form {:08X} is power type (spellType={})",
							pending.formID, static_cast<int>(spell->GetSpellType()));
					_pendingSpellActivation.reset();
				} else {
					RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
					if (!aeMan) {
						logger::warn("[SpellPipe] pending activation failed: no ActorEquipManager");
						_pendingSpellActivation.reset();
					} else {
						const bool firstReadyAttempt = pending.readyRetryFramesRemaining >= kSpellReadyRetryFrameBudget;
						if (firstReadyAttempt) {
							logger::info("[SpellPipe] pending spell meta formID={:08X} name='{}' spellType={} castingType={} delivery={} chargeTime={:.2f} hand={} requested={} preCast2H={}",
								pending.formID,
								spell->GetName() ? spell->GetName() : "",
								static_cast<int>(spell->GetSpellType()),
								static_cast<int>(spell->GetCastingType()),
								static_cast<int>(spell->GetDelivery()),
								(std::max)(0.0f, spell->GetChargeTime()),
								GetTargetHandName(pending.hand),
								GetTargetHandName(pending.requestedHand),
								pending.preCastHadTwoHandedWeapon ? 1 : 0);
							if (auto* avOwner = pc->AsActorValueOwner(); avOwner) {
								const float magickaNow = avOwner->GetActorValue(RE::ActorValue::kMagicka);
								const float magickaCost = spell->CalculateMagickaCost(pc);
								logger::info("[SpellPipe] pending spell resources formID={:08X} magickaNow={:.1f} magickaCost={:.1f}",
									pending.formID,
									magickaNow,
									magickaCost);
							}
						}

						RE::TESForm* equippedLeftNow = pc->GetEquippedObject(true);
						RE::TESForm* equippedRightNow = pc->GetEquippedObject(false);
						auto getPendingRestoreTargetFormID = [&](bool isLeft) -> RE::FormID {
							const bool hasOverride = isLeft ?
								pending.restoreOverrideLeftHand :
								pending.restoreOverrideRightHand;
							const RE::FormID overrideFormID = isLeft ?
								pending.restoreOverrideLeftFormID :
								pending.restoreOverrideRightFormID;
							if (hasOverride) {
								return NormalizeRestorableHandFormID(pc, overrideFormID);
							}
							const RE::FormID rawFormID = isLeft ?
								pending.preCastLeftFormID :
								pending.preCastRightFormID;
							return NormalizeRestorableHandFormID(pc, rawFormID);
						};

						const bool useLeftHand = UsesLeftAttack(pending.hand);
						const bool useRightHand = UsesRightAttack(pending.hand);
						const bool useBothHands = useLeftHand && useRightHand;

						// If both hands already hold the same spell and request is single-hand,
						// temporarily clear the opposite hand so vanilla attack routing does not
						// get stuck in a dual-equip/non-start state. We restore opposite hand post-cast.
						if (!useBothHands && !pending.singleHandIsolationApplied) {
							const RE::FormID equippedLeftID = equippedLeftNow ? equippedLeftNow->GetFormID() : 0;
							const RE::FormID equippedRightID = equippedRightNow ? equippedRightNow->GetFormID() : 0;
							const bool bothHandsSameRequestedSpell =
								equippedLeftID == pending.formID &&
								equippedRightID == pending.formID;
							const bool preCastBothHandsSameRequestedSpell =
								pending.preCastHandsCaptured &&
									pending.preCastLeftFormID == pending.formID &&
									pending.preCastRightFormID == pending.formID;
							if (bothHandsSameRequestedSpell && preCastBothHandsSameRequestedSpell) {
								const bool oppositeIsLeft = useRightHand;
								RE::BGSEquipSlot* oppositeSlot = oppositeIsLeft ?
									Utils::Slot::GetLeftHandSlot() :
									Utils::Slot::GetRightHandSlot();
								if (oppositeSlot) {
									Utils::Slot::CleanSlot(pc, oppositeSlot);
									pending.singleHandIsolationApplied = true;
									pending.singleHandIsolationOppositeWasLeft = oppositeIsLeft;
									pending.singleHandIsolationRestoreFormID = pending.formID;
									pending.requiredEquipBeforeCast = true;
									pending.postEquipWarmupFramesRemaining =
										(std::max)(pending.postEquipWarmupFramesRemaining, kPostEquipSettleFrameBudget);
									logger::info("[SpellPipe] single-hand isolation applied hand={} opposite={} spell={:08X}",
										GetTargetHandName(pending.hand),
										oppositeIsLeft ? "LEFT" : "RIGHT",
										pending.formID);
									// Refresh current equips after isolation before readiness checks.
									equippedLeftNow = pc->GetEquippedObject(true);
									equippedRightNow = pc->GetEquippedObject(false);
								} else {
									logger::warn("[SpellPipe] single-hand isolation skipped: opposite slot unavailable hand={} spell={:08X}",
										GetTargetHandName(pending.hand),
										pending.formID);
								}
							} else if (pending.preCastHadTwoHandedWeapon) {
								const bool oppositeIsLeft = useRightHand;
								const RE::FormID oppositeNowFormID = oppositeIsLeft ? equippedLeftID : equippedRightID;
								RE::BGSEquipSlot* castSlot = useLeftHand ?
									Utils::Slot::GetLeftHandSlot() :
									Utils::Slot::GetRightHandSlot();
								RE::BGSEquipSlot* oppositeSlot = oppositeIsLeft ?
									Utils::Slot::GetLeftHandSlot() :
									Utils::Slot::GetRightHandSlot();
								if (!castSlot || !oppositeSlot) {
									logger::warn("[SpellPipe] single-hand 2H-unwind skipped: slot unavailable hand={} castSlot={} oppositeSlot={} spell={:08X}",
										GetTargetHandName(pending.hand),
										castSlot ? 1 : 0,
										oppositeSlot ? 1 : 0,
										pending.formID);
								} else {
									const RE::FormID oppositeRestoreFormID = pending.preCastHandsCaptured ?
										getPendingRestoreTargetFormID(oppositeIsLeft) :
										NormalizeRestorableHandFormID(pc, oppositeNowFormID);

									const bool currentlyIn2H =
										IsTwoHandedForm(equippedLeftNow) ||
										IsTwoHandedForm(equippedRightNow);
									if (currentlyIn2H) {
										Utils::Slot::CleanSlot(pc, Utils::Slot::GetLeftHandSlot());
										Utils::Slot::CleanSlot(pc, Utils::Slot::GetRightHandSlot());
										logger::info("[SpellPipe] single-hand 2H-unwind cleared both hands leftNow={:08X} rightNow={:08X}",
											equippedLeftID,
											equippedRightID);
									}

									// Mirror proven dual-equip single-hand isolation:
									// 1) transiently equip both hands to requested spell (stabilize 2H->magic graph)
									// 2) then clear opposite hand so single-hand intent stays true (R/L does not become both)
									aeMan->EquipSpell(pc, spell, castSlot);
									aeMan->EquipSpell(pc, spell, oppositeSlot);
									equippedLeftNow = pc->GetEquippedObject(true);
									equippedRightNow = pc->GetEquippedObject(false);
									const RE::FormID leftAfterDualEquip = equippedLeftNow ? equippedLeftNow->GetFormID() : 0;
									const RE::FormID rightAfterDualEquip = equippedRightNow ? equippedRightNow->GetFormID() : 0;
									const bool transientDualReady =
										leftAfterDualEquip == pending.formID &&
										rightAfterDualEquip == pending.formID;
									if (!transientDualReady) {
										logger::warn("[SpellPipe] single-hand 2H-unwind transient dual-equip incomplete hand={} leftNow={:08X} rightNow={:08X} spell={:08X}",
											GetTargetHandName(pending.hand),
											leftAfterDualEquip,
											rightAfterDualEquip,
											pending.formID);
									}

									// Force a full sheathe->draw cycle to unwind residual 2H combat graph state.
									ActorVirtualCompat::DrawWeaponMagicHands(pc, false);
									ActorVirtualCompat::DrawWeaponMagicHands(pc, true);
									logger::info("[SpellPipe] single-hand 2H-unwind forced sheathe->draw cycle hand={}",
										GetTargetHandName(pending.hand));

									// If opposite hand still has the requested spell, clear it before dispatch.
									// This preserves immersive single-hand behavior while keeping pre-cast restore target.
									const RE::FormID oppositeAfterDualEquip = oppositeIsLeft ? leftAfterDualEquip : rightAfterDualEquip;
									if (oppositeAfterDualEquip == pending.formID) {
										Utils::Slot::CleanSlot(pc, oppositeSlot);
										const RE::FormID oppositeAfterClear = oppositeIsLeft ?
											(pc->GetEquippedObject(true) ? pc->GetEquippedObject(true)->GetFormID() : 0) :
											(pc->GetEquippedObject(false) ? pc->GetEquippedObject(false)->GetFormID() : 0);
										logger::info("[SpellPipe] single-hand 2H-bootstrap opposite cleared hand={} opposite={} before={:08X} after={:08X}",
											GetTargetHandName(pending.hand),
											oppositeIsLeft ? "LEFT" : "RIGHT",
											oppositeAfterDualEquip,
											oppositeAfterClear);
									}

									pending.requiredEquipBeforeCast = true;
									pending.postEquipWarmupFramesRemaining =
										(std::max)(pending.postEquipWarmupFramesRemaining, static_cast<std::uint8_t>(kPostEquipSettleFrameBudget + 16u));
									pending.singleHandIsolationApplied = true;
									pending.singleHandIsolationOppositeWasLeft = oppositeIsLeft;
									pending.singleHandIsolationRestoreFormID = oppositeRestoreFormID;
									logger::info("[SpellPipe] single-hand 2H-bootstrap applied hand={} opposite={} oppositeBefore={:08X} oppositeRestore={:08X} spell={:08X}",
										GetTargetHandName(pending.hand),
										oppositeIsLeft ? "LEFT" : "RIGHT",
										oppositeNowFormID,
										oppositeRestoreFormID,
										pending.formID);
									// Refresh current equips after bootstrap before readiness checks.
									equippedLeftNow = pc->GetEquippedObject(true);
									equippedRightNow = pc->GetEquippedObject(false);
								}
							} else if (bothHandsSameRequestedSpell && firstReadyAttempt) {
								logger::info("[SpellPipe] single-hand isolation skipped (not pre-cast dual-equip) hand={} preCastLeft={:08X} preCastRight={:08X} spell={:08X}",
									GetTargetHandName(pending.hand),
									pending.preCastLeftFormID,
									pending.preCastRightFormID,
									pending.formID);
							}
						}

						// In pre-cast 2H single-hand flows, the transient dual-equip bootstrap can
						// re-latch the opposite hand a few frames later. Re-clear opposite hand here
						// so dispatch stays strict single-hand.
						if (pending.preCastHadTwoHandedWeapon && !useBothHands && pending.singleHandIsolationApplied) {
							const bool oppositeIsLeft = useRightHand;
							RE::TESForm* oppositeEquipped = oppositeIsLeft ? equippedLeftNow : equippedRightNow;
							const RE::FormID oppositeBeforeReclear = oppositeEquipped ? oppositeEquipped->GetFormID() : 0;
							if (oppositeBeforeReclear == pending.formID) {
								RE::BGSEquipSlot* oppositeSlot = oppositeIsLeft ?
									Utils::Slot::GetLeftHandSlot() :
									Utils::Slot::GetRightHandSlot();
								if (oppositeSlot) {
									Utils::Slot::CleanSlot(pc, oppositeSlot);
									equippedLeftNow = pc->GetEquippedObject(true);
									equippedRightNow = pc->GetEquippedObject(false);
									const RE::FormID oppositeAfterReclear = oppositeIsLeft ?
										(equippedLeftNow ? equippedLeftNow->GetFormID() : 0) :
										(equippedRightNow ? equippedRightNow->GetFormID() : 0);
									pending.requiredEquipBeforeCast = true;
									pending.postEquipWarmupFramesRemaining =
										(std::max)(pending.postEquipWarmupFramesRemaining, static_cast<std::uint8_t>(kPostEquipSettleFrameBudget / 2u));
									logger::info("[SpellPipe] single-hand 2H-opposite re-clear hand={} opposite={} before={:08X} after={:08X}",
										GetTargetHandName(pending.hand),
										oppositeIsLeft ? "LEFT" : "RIGHT",
										oppositeBeforeReclear,
										oppositeAfterReclear);
								}
							}
						}

						RE::TESForm* equippedLeft = useLeftHand ? equippedLeftNow : nullptr;
						RE::TESForm* equippedRight = useRightHand ? equippedRightNow : nullptr;
						bool equipLeftReady = !useLeftHand || (equippedLeft && equippedLeft->GetFormID() == pending.formID);
						bool equipRightReady = !useRightHand || (equippedRight && equippedRight->GetFormID() == pending.formID);

						const bool periodicEquipLog = (pending.readyRetryFramesRemaining % 30u) == 0u;
						if (useLeftHand && !equipLeftReady) {
							pending.requiredEquipBeforeCast = true;
							aeMan->EquipSpell(pc, spell, Utils::Slot::GetLeftHandSlot());
							equippedLeft = pc->GetEquippedObject(true);
							equipLeftReady = equippedLeft && equippedLeft->GetFormID() == pending.formID;
							if (firstReadyAttempt || periodicEquipLog) {
								logger::info("[SpellPipe] equipped spell hand=left equipped={:08X} requested={:08X}",
									equippedLeft ? equippedLeft->GetFormID() : 0, pending.formID);
							}
						} else if (firstReadyAttempt && useLeftHand) {
							logger::info("[SpellPipe] equip skipped spell already in hand=left formID={:08X}", pending.formID);
						}

						if (useRightHand && !equipRightReady) {
							pending.requiredEquipBeforeCast = true;
							aeMan->EquipSpell(pc, spell, Utils::Slot::GetRightHandSlot());
							equippedRight = pc->GetEquippedObject(false);
							equipRightReady = equippedRight && equippedRight->GetFormID() == pending.formID;
							if (firstReadyAttempt || periodicEquipLog) {
								logger::info("[SpellPipe] equipped spell hand=right equipped={:08X} requested={:08X}",
									equippedRight ? equippedRight->GetFormID() : 0, pending.formID);
							}
						} else if (firstReadyAttempt && useRightHand) {
							logger::info("[SpellPipe] equip skipped spell already in hand=right formID={:08X}", pending.formID);
						}

						// Non-2H single-hand cross-cast case:
						// target equip can transiently make both hands hold the same spell, which
						// can stall caster start for individual L/R dispatch. Clear opposite hand.
						if (!useBothHands &&
							!pending.preCastHadTwoHandedWeapon &&
							!pending.singleHandIsolationApplied) {
							const RE::FormID leftAfterEquip = [&]() -> RE::FormID {
								if (useLeftHand && equippedLeft) {
									return equippedLeft->GetFormID();
								}
								if (RE::TESForm* left = pc->GetEquippedObject(true)) {
									return left->GetFormID();
								}
								return 0;
							}();
							const RE::FormID rightAfterEquip = [&]() -> RE::FormID {
								if (useRightHand && equippedRight) {
									return equippedRight->GetFormID();
								}
								if (RE::TESForm* right = pc->GetEquippedObject(false)) {
									return right->GetFormID();
								}
								return 0;
							}();
							const bool bothHandsNowRequestedSpell =
								leftAfterEquip == pending.formID &&
								rightAfterEquip == pending.formID;
							if (bothHandsNowRequestedSpell) {
								const bool oppositeIsLeft = useRightHand;
								RE::BGSEquipSlot* oppositeSlot = oppositeIsLeft ?
									Utils::Slot::GetLeftHandSlot() :
									Utils::Slot::GetRightHandSlot();
								if (oppositeSlot) {
									const RE::FormID oppositeRestoreFormID = pending.preCastHandsCaptured ?
										getPendingRestoreTargetFormID(oppositeIsLeft) :
										NormalizeRestorableHandFormID(pc, oppositeIsLeft ? leftAfterEquip : rightAfterEquip);
									Utils::Slot::CleanSlot(pc, oppositeSlot);
									equippedLeftNow = pc->GetEquippedObject(true);
									equippedRightNow = pc->GetEquippedObject(false);
									equippedLeft = useLeftHand ? equippedLeftNow : nullptr;
									equippedRight = useRightHand ? equippedRightNow : nullptr;
									equipLeftReady = !useLeftHand || (equippedLeft && equippedLeft->GetFormID() == pending.formID);
									equipRightReady = !useRightHand || (equippedRight && equippedRight->GetFormID() == pending.formID);
									pending.singleHandIsolationApplied = true;
									pending.singleHandIsolationOppositeWasLeft = oppositeIsLeft;
									pending.singleHandIsolationRestoreFormID = oppositeRestoreFormID;
									pending.requiredEquipBeforeCast = true;
									pending.postEquipWarmupFramesRemaining =
										(std::max)(pending.postEquipWarmupFramesRemaining, kPostEquipSettleFrameBudget);
									logger::info("[SpellPipe] single-hand late isolation applied hand={} opposite={} preCast2H=0 restore={:08X} spell={:08X}",
										GetTargetHandName(pending.hand),
										oppositeIsLeft ? "LEFT" : "RIGHT",
										oppositeRestoreFormID,
										pending.formID);
								}
							}
						}

						bool equipReady = (!useLeftHand || equipLeftReady) && (!useRightHand || equipRightReady);

						if (equipReady &&
							pending.requiredEquipBeforeCast &&
							pending.postEquipWarmupFramesRemaining == 0) {
							pending.postEquipWarmupFramesRemaining = kPostEquipSettleFrameBudget;
							logger::info("[SpellPipe] waiting post-equip settle formID={:08X} hand={} settleFrames={}",
								pending.formID,
								GetTargetHandName(pending.hand),
								pending.postEquipWarmupFramesRemaining);
						}

						const auto* actorState = pc->AsActorState();
						bool drawReady = !actorState || actorState->IsWeaponDrawn();
						if (!drawReady) {
							// If draw transition is needed, enforce a short settle window before dispatch.
							pending.requiredEquipBeforeCast = true;
							pending.postEquipWarmupFramesRemaining = (std::max)(pending.postEquipWarmupFramesRemaining, kPostEquipSettleFrameBudget);
							ActorVirtualCompat::DrawWeaponMagicHands(pc, true);
						}

						if (equipReady &&
							drawReady &&
							pending.postEquipWarmupFramesRemaining > 0) {
							pending.postEquipWarmupFramesRemaining--;
						}

						const bool castReady =
							equipReady &&
							drawReady &&
							pending.postEquipWarmupFramesRemaining == 0;

						if (!castReady) {
							if (pending.readyRetryFramesRemaining > 0) {
								if (firstReadyAttempt) {
									logger::info("[SpellPipe] waiting cast-ready state formID={:08X} hand={} leftReady={} rightReady={} drawReady={} settleFrames={} retries={}",
										pending.formID,
										GetTargetHandName(pending.hand),
										equipLeftReady ? 1 : 0,
										equipRightReady ? 1 : 0,
										drawReady ? 1 : 0,
										pending.postEquipWarmupFramesRemaining,
										pending.readyRetryFramesRemaining);
								}
								pending.readyRetryFramesRemaining--;
							} else {
								logger::warn("[SpellPipe] pending activation failed: cast-ready timeout formID={:08X} hand={} leftReady={} rightReady={} drawReady={} settleFrames={}",
									pending.formID,
									GetTargetHandName(pending.hand),
									equipLeftReady ? 1 : 0,
									equipRightReady ? 1 : 0,
									drawReady ? 1 : 0,
									pending.postEquipWarmupFramesRemaining);
								_pendingSpellActivation.reset();
							}
						} else {
							const float holdSeconds = std::clamp(pending.concentrationHoldSeconds, 0.0f, 60.0f);
							const auto canCastFromSource = [&](RE::MagicSystem::CastingSource source, RE::MagicSystem::CannotCastReason& outReason) {
								outReason = RE::MagicSystem::CannotCastReason::kOK;
								RE::MagicCaster* caster = pc->GetMagicCaster(source);
								if (!caster) {
									return false;
								}
								float strength = 0.0f;
								caster->CheckCast(spell, false, &strength, &outReason, false);
								return outReason == RE::MagicSystem::CannotCastReason::kOK;
							};
							RE::MagicSystem::CannotCastReason leftCastReason = RE::MagicSystem::CannotCastReason::kOK;
							RE::MagicSystem::CannotCastReason rightCastReason = RE::MagicSystem::CannotCastReason::kOK;
							const bool leftSourceCastable = !useLeftHand ||
								canCastFromSource(RE::MagicSystem::CastingSource::kLeftHand, leftCastReason);
							const bool rightSourceCastable = !useRightHand ||
								canCastFromSource(RE::MagicSystem::CastingSource::kRightHand, rightCastReason);
							const bool allRequestedSourcesBlocked =
								(useLeftHand ? !leftSourceCastable : true) &&
								(useRightHand ? !rightSourceCastable : true);
							if (allRequestedSourcesBlocked) {
								const bool periodicCastabilityLog = firstReadyAttempt || ((pending.readyRetryFramesRemaining % 30u) == 0u);
								if (pending.readyRetryFramesRemaining > 0) {
									if (periodicCastabilityLog) {
										logger::info("[SpellPipe] waiting castable source formID={:08X} hand={} leftCastable={} rightCastable={} leftReason={} rightReason={} retries={}",
											pending.formID,
											GetTargetHandName(pending.hand),
											leftSourceCastable ? 1 : 0,
											rightSourceCastable ? 1 : 0,
											static_cast<int>(leftCastReason),
											static_cast<int>(rightCastReason),
											pending.readyRetryFramesRemaining);
									}
									pending.readyRetryFramesRemaining--;
								} else {
									logger::warn("[SpellPipe] pending activation failed: castability timeout formID={:08X} hand={} leftCastable={} rightCastable={} leftReason={} rightReason={}",
										pending.formID,
										GetTargetHandName(pending.hand),
										leftSourceCastable ? 1 : 0,
										rightSourceCastable ? 1 : 0,
										static_cast<int>(leftCastReason),
										static_cast<int>(rightCastReason));
									_pendingSpellActivation.reset();
								}
							} else {
								auto* controls = RE::PlayerControls::GetSingleton();
								auto* userEvents = RE::UserEvents::GetSingleton();
								if (!controls || !controls->attackBlockHandler || !userEvents) {
									logger::warn("[SpellPipe] activate task failed: controls={} attackHandler={} userEvents={}",
										controls ? 1 : 0, (controls && controls->attackBlockHandler) ? 1 : 0, userEvents ? 1 : 0);
									_pendingSpellActivation.reset();
								} else {
									RE::INPUT_DEVICE leftDevice = RE::INPUT_DEVICE::kKeyboard;
									RE::INPUT_DEVICE rightDevice = RE::INPUT_DEVICE::kKeyboard;
									std::uint32_t leftIdCode = 0;
									std::uint32_t rightIdCode = 0;
									const bool hasLeftAttackBinding = ResolveUserEventBinding(userEvents->leftAttack, leftDevice, leftIdCode);
									const bool hasRightAttackBinding = ResolveUserEventBinding(userEvents->rightAttack, rightDevice, rightIdCode);
									const bool canUseLeftAttack = !useLeftHand || hasLeftAttackBinding;
									const bool canUseRightAttack = !useRightHand || hasRightAttackBinding;
									const bool preferLeftPrimary = pending.requestedHand == TargetHand::Left;

									bool runLeftAttack = useLeftHand;
									bool runRightAttack = useRightHand;
									TargetHand effectiveHand = pending.hand;
									if (useBothHands && (!canUseLeftAttack || !canUseRightAttack)) {
										if (preferLeftPrimary && canUseLeftAttack) {
											runLeftAttack = true;
											runRightAttack = false;
											effectiveHand = TargetHand::Left;
											logger::warn("[SpellPipe] both-hand requested but right binding unresolved, keeping preferred LEFT hand");
										} else if (!preferLeftPrimary && canUseRightAttack) {
											runLeftAttack = false;
											runRightAttack = true;
											effectiveHand = TargetHand::Right;
											logger::warn("[SpellPipe] both-hand requested but left binding unresolved, keeping preferred RIGHT hand");
										} else if (canUseRightAttack) {
											runLeftAttack = false;
											runRightAttack = true;
											effectiveHand = TargetHand::Right;
											logger::warn("[SpellPipe] both-hand requested but left binding unresolved, falling back to right hand only");
										} else if (canUseLeftAttack) {
											runLeftAttack = true;
											runRightAttack = false;
											effectiveHand = TargetHand::Left;
											logger::warn("[SpellPipe] both-hand requested but right binding unresolved, falling back to left hand only");
										}
									}

									if ((!runLeftAttack && !runRightAttack) ||
										(runLeftAttack && !canUseLeftAttack) ||
										(runRightAttack && !canUseRightAttack)) {
										logger::warn("[SpellPipe] activate task failed: could not resolve attack binding hand={} leftResolved={} rightResolved={}",
											GetTargetHandName(pending.hand),
											canUseLeftAttack ? 1 : 0,
											canUseRightAttack ? 1 : 0);
										_pendingSpellActivation.reset();
									} else {
										auto sendAttackDown = [&](bool useLeftAttack, RE::INPUT_DEVICE device, std::uint32_t idCode, const char* tag) {
											const auto& attackEvent = useLeftAttack ? userEvents->leftAttack : userEvents->rightAttack;
											auto* downEvent = RE::ButtonEvent::Create(device, attackEvent, idCode, 1.0f, 0.0f);
											if (!downEvent) {
												logger::warn("[SpellPipe] activate task failed: down event alloc{}", tag ? tag : "");
												return false;
											}
											controls->attackBlockHandler->ProcessButton(downEvent, &controls->data);
											RE::free(downEvent);
											logger::info("[SpellPipe] sent attack DOWN{} (left={} device={} idCode={})",
												tag ? tag : "", useLeftAttack ? 1 : 0, GetInputDeviceName(device), idCode);
											return true;
										};

										bool primaryUseLeftAttack = runLeftAttack;
										RE::INPUT_DEVICE primaryDevice = leftDevice;
										std::uint32_t primaryIdCode = leftIdCode;
										bool releaseSecondAttack = false;
										bool secondUseLeftAttack = false;
										RE::INPUT_DEVICE secondDevice = RE::INPUT_DEVICE::kKeyboard;
										std::uint32_t secondIdCode = 0;
										bool releaseSecondOnCasterStart = false;
										bool requirePrimaryCasterStart = false;
										bool enableStartAssistTap = false;
										bool startAssistUseLeftAttack = false;
										RE::INPUT_DEVICE startAssistDevice = RE::INPUT_DEVICE::kKeyboard;
										std::uint32_t startAssistIdCode = 0;

										if (runLeftAttack && runRightAttack) {
											releaseSecondAttack = true;
											if (preferLeftPrimary) {
												primaryUseLeftAttack = true;
												primaryDevice = leftDevice;
												primaryIdCode = leftIdCode;
												secondUseLeftAttack = false;
												secondDevice = rightDevice;
												secondIdCode = rightIdCode;
											} else {
												primaryUseLeftAttack = false;
												primaryDevice = rightDevice;
												primaryIdCode = rightIdCode;
												secondUseLeftAttack = true;
												secondDevice = leftDevice;
												secondIdCode = leftIdCode;
											}
											logger::info("[SpellPipe] both-hand dispatch order requested={} primary={} secondary={}",
												GetTargetHandName(pending.requestedHand),
												primaryUseLeftAttack ? "LEFT" : "RIGHT",
												secondUseLeftAttack ? "LEFT" : "RIGHT");
										} else if (runRightAttack) {
											primaryUseLeftAttack = false;
											primaryDevice = rightDevice;
											primaryIdCode = rightIdCode;
										} else {
											primaryUseLeftAttack = true;
											primaryDevice = leftDevice;
											primaryIdCode = leftIdCode;
										}

										// Final guard right before dispatch: keep opposite hand clear for
										// pre-cast 2H single-hand casts.
										if (pending.preCastHadTwoHandedWeapon && (runLeftAttack != runRightAttack)) {
											const bool oppositeIsLeft = runRightAttack;
											RE::TESForm* oppositeEquippedNow = pc->GetEquippedObject(oppositeIsLeft);
											if (oppositeEquippedNow && oppositeEquippedNow->GetFormID() == pending.formID) {
												RE::BGSEquipSlot* oppositeSlot = oppositeIsLeft ?
													Utils::Slot::GetLeftHandSlot() :
													Utils::Slot::GetRightHandSlot();
												if (oppositeSlot) {
													Utils::Slot::CleanSlot(pc, oppositeSlot);
													const RE::FormID oppositeAfterFinalClear =
														(pc->GetEquippedObject(oppositeIsLeft) ?
																pc->GetEquippedObject(oppositeIsLeft)->GetFormID() :
																0);
													logger::info("[SpellPipe] single-hand 2H dispatch pre-clear primary={} opposite={} before={:08X} after={:08X}",
														primaryUseLeftAttack ? "LEFT" : "RIGHT",
														oppositeIsLeft ? "LEFT" : "RIGHT",
														oppositeEquippedNow->GetFormID(),
														oppositeAfterFinalClear);
												}
											}
										}
										// Pre-cast 2H single-hand transitions:
										// once we run single-hand isolation/bootstrap, keep strict hand intent and
										// avoid re-arming startup dual-down (causes both-hand/glitch regressions).
										if (pending.preCastHadTwoHandedWeapon && !releaseSecondAttack) {
											const RE::FormID oppositeNowFormID = primaryUseLeftAttack ?
												(pc->GetEquippedObject(false) ? pc->GetEquippedObject(false)->GetFormID() : 0) :
												(pc->GetEquippedObject(true) ? pc->GetEquippedObject(true)->GetFormID() : 0);
											const bool strictSingleHand2H = pending.singleHandIsolationApplied;
											if (strictSingleHand2H) {
												logger::info("[SpellPipe] single-hand 2H startup assist disabled (strict hand intent) primary={} oppositeNow={:08X} spell={:08X}",
													primaryUseLeftAttack ? "LEFT" : "RIGHT",
													oppositeNowFormID,
													pending.formID);
											} else {
												const bool oppositeStillHasRequestedSpell = oppositeNowFormID == pending.formID;
												if (primaryUseLeftAttack && hasRightAttackBinding && oppositeStillHasRequestedSpell) {
													releaseSecondAttack = true;
													secondUseLeftAttack = false;
													secondDevice = rightDevice;
													secondIdCode = rightIdCode;
													releaseSecondOnCasterStart = false;
												} else if (!primaryUseLeftAttack && hasLeftAttackBinding && oppositeStillHasRequestedSpell) {
													releaseSecondAttack = true;
													secondUseLeftAttack = true;
													secondDevice = leftDevice;
													secondIdCode = leftIdCode;
													releaseSecondOnCasterStart = false;
												}
												if (releaseSecondAttack) {
													requirePrimaryCasterStart = true;
													logger::info("[SpellPipe] single-hand 2H startup dual-down armed primary={} assist={} assistDevice={} assistIdCode={}",
														primaryUseLeftAttack ? "LEFT" : "RIGHT",
														secondUseLeftAttack ? "LEFT" : "RIGHT",
														GetInputDeviceName(secondDevice),
														secondIdCode);
												} else {
													logger::info("[SpellPipe] single-hand 2H startup assist skipped primary={} oppositeNow={:08X} spell={:08X}",
														primaryUseLeftAttack ? "LEFT" : "RIGHT",
														oppositeNowFormID,
														pending.formID);
												}
											}
										}

										bool downOk = true;
										downOk = sendAttackDown(primaryUseLeftAttack, primaryDevice, primaryIdCode, "");
										if (downOk && releaseSecondAttack) {
											const char* secondTag = requirePrimaryCasterStart ? " (startup-assist)" :
												(releaseSecondOnCasterStart ? " (second-early-release)" : " (second)");
											downOk = sendAttackDown(secondUseLeftAttack, secondDevice, secondIdCode, secondTag);
										}
										if (!downOk) {
											_pendingSpellActivation.reset();
										}

										if (downOk) {
										bool restoreLeftAfterCast = false;
										bool restoreRightAfterCast = false;
										RE::FormID restoreLeftFormID = 0;
										RE::FormID restoreRightFormID = 0;
										if (pending.preCastHandsCaptured) {
											if (runLeftAttack) {
												// Restore previous left state even if it was empty (formID=0).
												const RE::FormID restoreLeftCandidate = getPendingRestoreTargetFormID(true);
												const bool leftWasAlreadyRequestedSpell =
													!pending.restoreOverrideLeftHand &&
													pending.preCastLeftFormID == pending.formID;
												const bool leftRestoreMatchesRequestedSpell =
													restoreLeftCandidate != 0 &&
													restoreLeftCandidate == pending.formID;
												if ((pending.restoreOverrideLeftHand || !leftWasAlreadyRequestedSpell) &&
													!leftRestoreMatchesRequestedSpell) {
													restoreLeftAfterCast = true;
													restoreLeftFormID = restoreLeftCandidate;
												}
											}
											if (runRightAttack) {
												// Restore previous right state even if it was empty (formID=0).
												const RE::FormID restoreRightCandidate = getPendingRestoreTargetFormID(false);
												const bool rightWasAlreadyRequestedSpell =
													!pending.restoreOverrideRightHand &&
													pending.preCastRightFormID == pending.formID;
												const bool rightRestoreMatchesRequestedSpell =
													restoreRightCandidate != 0 &&
													restoreRightCandidate == pending.formID;
												if ((pending.restoreOverrideRightHand || !rightWasAlreadyRequestedSpell) &&
													!rightRestoreMatchesRequestedSpell) {
													restoreRightAfterCast = true;
													restoreRightFormID = restoreRightCandidate;
												}
											}
										}
										// If single-hand isolation temporarily cleared the opposite hand,
										// always restore it to its pre-isolation state after cast.
										if (pending.singleHandIsolationApplied) {
											if (pending.singleHandIsolationOppositeWasLeft) {
												restoreLeftAfterCast = true;
												restoreLeftFormID = pending.singleHandIsolationRestoreFormID;
											} else {
												restoreRightAfterCast = true;
												restoreRightFormID = pending.singleHandIsolationRestoreFormID;
											}
											logger::info("[SpellPipe] single-hand isolation restore armed opposite={} formID={:08X}",
												pending.singleHandIsolationOppositeWasLeft ? "LEFT" : "RIGHT",
												pending.singleHandIsolationRestoreFormID);
										}
											if (restoreLeftAfterCast || restoreRightAfterCast) {
												logger::info("[SpellPipe] post-cast restore armed left={:08X} right={:08X} hand={}",
													restoreLeftAfterCast ? restoreLeftFormID : 0,
													restoreRightAfterCast ? restoreRightFormID : 0,
													GetTargetHandName(effectiveHand));
											}

											const bool holdConcentration =
												spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration &&
												holdSeconds > 0.0f;

											if (holdConcentration) {
												const auto primaryCastingSource = primaryUseLeftAttack ?
													RE::MagicSystem::CastingSource::kLeftHand :
													RE::MagicSystem::CastingSource::kRightHand;
												Wheeler::ArmSpellHoldRelease(
													primaryDevice,
													primaryIdCode,
													primaryUseLeftAttack,
													holdSeconds,
													false,
													pending.formID,
													primaryCastingSource,
													false,
													0.0f,
													releaseSecondAttack,
													secondUseLeftAttack,
													secondDevice,
													secondIdCode,
													restoreLeftAfterCast,
													restoreLeftFormID,
													restoreRightAfterCast,
													restoreRightFormID,
													enableStartAssistTap,
													startAssistUseLeftAttack,
													startAssistDevice,
													startAssistIdCode,
													releaseSecondOnCasterStart,
													requirePrimaryCasterStart);
												Wheeler::QueueConcentrationSpellStop(pending.formID, primaryCastingSource, holdSeconds);
												logger::info("[SpellPipe] activate task holding concentration spell formID={:08X} holdSec={:.2f} hand={}",
													pending.formID, holdSeconds, GetTargetHandName(effectiveHand));
											} else {
												// Fire-and-forget spells often need the cast bar to complete before release.
												const float chargeTime = (std::max)(0.0f, spell->GetChargeTime());
												const bool usesChargeWindow =
													spell->GetCastingType() == RE::MagicSystem::CastingType::kFireAndForget ||
													spell->GetCastingType() == RE::MagicSystem::CastingType::kScroll;
												float releaseDelay = kDirectTapReleaseMinSec;
												float releaseSafetyBufferSec = 0.0f;
												if (usesChargeWindow) {
													const bool spellIsBothOnly = ResolveSpellHandRule(spell) == SpellHandRule::BothOnly;
													if (pending.preCastHadTwoHandedWeapon &&
														spellIsBothOnly &&
														chargeTime >= kDirectTapRitualChargeThresholdSec) {
														// Ritual-scale both-hand spells are sensitive when triggered from 2H unwind.
														// Keep attack held a bit longer after charge window to avoid early release.
														releaseSafetyBufferSec = kDirectTapRitualSafetyBufferSec;
													}
													releaseDelay = std::clamp(
														(std::max)(kDirectTapReleaseMinSec, chargeTime + kDirectTapChargeBufferSec + releaseSafetyBufferSec),
														kDirectTapReleaseMinSec,
														kDirectTapReleaseMaxSec);
												}
												const auto primaryCastingSource = primaryUseLeftAttack ?
													RE::MagicSystem::CastingSource::kLeftHand :
													RE::MagicSystem::CastingSource::kRightHand;
												const float chargeAwareMaxTotalSeconds = usesChargeWindow ?
													std::clamp(
														(std::max)(
															kDirectTapChargeAwareMinTotalSec,
															(std::max)(
																releaseDelay + 1.00f,
																chargeTime * kDirectTapChargeAwarePerChargeMultiplier +
																	kDirectTapChargeAwareBasePaddingSec +
																	releaseSafetyBufferSec)),
														kDirectTapChargeAwareMinTotalSec,
														kDirectTapChargeAwareMaxTotalSec) :
													0.0f;
												Wheeler::ArmSpellHoldRelease(
													primaryDevice,
													primaryIdCode,
													primaryUseLeftAttack,
													releaseDelay,
													usesChargeWindow,
													pending.formID,
													primaryCastingSource,
													usesChargeWindow,
													chargeAwareMaxTotalSeconds,
													releaseSecondAttack,
													secondUseLeftAttack,
													secondDevice,
													secondIdCode,
													restoreLeftAfterCast,
													restoreLeftFormID,
													restoreRightAfterCast,
													restoreRightFormID,
													enableStartAssistTap,
													startAssistUseLeftAttack,
													startAssistDevice,
													startAssistIdCode,
													releaseSecondOnCasterStart,
													requirePrimaryCasterStart);
												logger::info("[SpellPipe] activate task tap-cast spell formID={:08X} hand={} releaseDelay={:.2f} chargeTime={:.2f} chargeSafety={:.2f} chargeAwareMax={:.2f}",
													pending.formID, GetTargetHandName(effectiveHand), releaseDelay, chargeTime, releaseSafetyBufferSec, chargeAwareMaxTotalSeconds);
											}

											_pendingSpellActivation.reset();
										}
									}
								}
							}
						}
					}
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
					const bool scriptBacked = book->HasVMAD();
					bool bookReadAllowListed = false;
					const char* bookReadCompatReason = nullptr;
					const bool useBookReadCompat = ShouldUseBookReadCompatOnRead(
						book, scriptBacked, bookReadAllowListed, bookReadCompatReason);

					if (MainWheelDebug::IsEnabled()) {
						MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_Exec",
							"formID={:08X} name='{}' isNote={} script={} compatMode={} allowlisted={} compatOnRead={} reason={} count={}",
							bookFormID, bookName ? bookName : "(null)", isNote, scriptBacked,
							BookReadCompatModeName(Config::WheelBehavior::BookReadCompat::Mode),
							bookReadAllowListed, useBookReadCompat,
							bookReadCompatReason ? bookReadCompatReason : "(null)", bookCount);
					}
					if (Config::WheelBehavior::BookReadCompat::DebugLog) {
						logger::info(
							"BookReadCompat: form={:08X} name='{}' script={} mode={} allowlisted={} onRead={} reason={}",
							bookFormID,
							bookName ? bookName : "(null)",
							scriptBacked,
							BookReadCompatModeName(Config::WheelBehavior::BookReadCompat::Mode),
							bookReadAllowListed,
							useBookReadCompat,
							bookReadCompatReason ? bookReadCompatReason : "(null)");
					}

					if (useBookReadCompat) {
						SKSE::GetTaskInterface()->AddTask([bookFormID]() {
							RE::PlayerCharacter* deferredPC = RE::PlayerCharacter::GetSingleton();
							RE::TESObjectBOOK* deferredBook = RE::TESForm::LookupByID<RE::TESObjectBOOK>(bookFormID);
							if (!deferredPC || !deferredBook) {
								if (MainWheelDebug::IsEnabled()) {
									MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_DeferFail", "scripted OnRead failed lookup/player");
								}
								return;
							}

							if (DispatchTempRefOnRead(deferredPC, deferredBook)) {
								if (MainWheelDebug::IsEnabled()) {
									MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_OnReadIssued",
										"TempRef OnRead dispatched, formID={:08X}", bookFormID);
								}
							} else {
								if (MainWheelDebug::IsEnabled()) {
									MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_OnReadFailed",
										"TempRef OnRead dispatch failed, falling back to BookMenu, formID={:08X}", bookFormID);
								}
								OpenBookMenuNow(bookFormID, deferredBook, "onread_fallback");
							}
						});
					} else {
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

							OpenBookMenuNow(bookFormID, deferredBook, "legacy_deferred");
						});
					}
					
					if (MainWheelDebug::IsEnabled()) {
						MainWheelDebug::Log(MainWheelDebug::Category::Input, "BookRead_Scheduled",
							"Deferred task queued, latch=ON, compatOnRead={} reason={}",
							useBookReadCompat, bookReadCompatReason ? bookReadCompatReason : "(null)");
					}
				}
			}
		}
	}

	// Process pending shout activation by starting the hold sequence.
	// When a shout is queued, we send DOWN event and start a timer.
	// The UP event is sent after the hold duration elapses.
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
				if (IsShoutBlockedByTransformGuard(shout, "PendingShout")) {
					s_shoutForcedUnlockedWords.reset();
					s_shoutForcedFromFallback = false;
					setShoutPipeState(ShoutPipeState::Idle, shoutFormID, "TransformGuardBlocked");
					ClearShoutPostCastRestore();
					return;
				}
				logger::info("Shout: processing activation after wheel closed: {} ({:08X})", 
					shout->GetName(), shout->GetFormID());
				
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
						int pendingUsableCount = unlockedWords;
						if (pendingUsableCount <= 0 && unlockState.previousCount > 0) {
							pendingUsableCount = unlockState.previousCount;
						}
						const bool pendingHasUsableCount = pendingUsableCount > 0;
						const bool hasFirstWordSlot = (unlockState.words[0].wordID != 0) || (shout->variations[0].word != nullptr);
						const bool hasSecondWordSlot = (unlockState.words[1].wordID != 0) || (shout->variations[1].word != nullptr);
						const bool isSingleWordShout = hasFirstWordSlot && !hasSecondWordSlot;
						const bool useSingleWordPendingFallback =
							!pendingHasUsableCount &&
							isSingleWordShout &&
							(unlockState.learnedCountContig >= 1);

						if (pendingHasUsableCount) {
							unlockedWords = pendingUsableCount;
							if (unlockState.finalCount <= 0 && unlockState.previousCount > 0) {
								detectionMethod = "PendingPrevStable";
							}
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
					ClearShoutPostCastRestore();
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
					ClearShoutPostCastRestore();
				} else {
					auto* userEvents = RE::UserEvents::GetSingleton();
					if (!userEvents) {
						logger::warn("Shout: failed to get UserEvents");
						setShoutPipeState(ShoutPipeState::Idle, shout->GetFormID(), "NoUserEvents");
						ClearShoutPostCastRestore();
					} else {
						const auto& shoutEvent = userEvents->shout;
						const RE::FormID selectedVoiceFormID = GetSelectedVoiceFormID(pc);
						if (selectedVoiceFormID != shout->GetFormID()) {
							const bool equipped = TrySetVoiceSelection(pc, shout->GetFormID(), "ShoutTempSelect");
							logger::info("Shout: temporary voice select previous={:08X} requested={:08X} equipped={}",
								selectedVoiceFormID,
								shout->GetFormID(),
								equipped ? 1 : 0);
							if (!equipped) {
								logger::warn("Shout: temporary voice select failed requested={:08X}", shout->GetFormID());
								ClearShoutPostCastRestore();
								setShoutPipeState(ShoutPipeState::Idle, shout->GetFormID(), "TempSelectFailed");
								return;
							}
						}
						
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

	if (_shoutPostCastRestorePending &&
		!_pendingShoutFormID.has_value() &&
		!s_shoutWaitUnlockActive &&
		!_shoutHoldActive &&
		!_shoutPostCastRestoreDelayActive) {
		logger::info("Shout: clearing unused queued voice restore requested={:08X} previous={:08X}",
			_shoutPostCastRestoreExpectedFormID,
			_shoutPostCastRestoreFormID);
		ClearShoutPostCastRestore();
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
				pc->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, refundAmount);
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

Wheeler::MutableInventoryCompatProfile Wheeler::GetMutableInventoryCompatProfile()
{
	return IsEquipmentDurabilitySystemProfile() ?
		       MutableInventoryCompatProfile::EquipmentDurabilitySystem :
		       MutableInventoryCompatProfile::Vanilla;
}

bool Wheeler::IsEquipmentDurabilitySystemActive()
{
	return GetMutableInventoryCompatProfile() == MutableInventoryCompatProfile::EquipmentDurabilitySystem;
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
							"Reloaded wheelBehavior.ini (InstantSpell={}, DirectCast={}, InstantPowers={}, InstantTransformations={}, Cooldowns.ContentDimAlpha={:.2f}, Cooldowns.SelectedIndicatorEnabled={}, Sounds.EnableSounds={})",
							Config::WheelBehavior::InstantSpell,
							Config::WheelBehavior::InstantSpellUseDirectCast,
							Config::WheelBehavior::InstantPowers,
							Config::WheelBehavior::InstantTransformations,
							Config::Cooldowns::ContentDimAlpha,
							Config::Cooldowns::SelectedIndicatorEnabled,
							Config::Sounds::EnableSounds);
						logger::debug("Config: reloaded wheel behavior settings (InstantSpell={}, DirectCast={}, InstantPowers={}, InstantTransformations={}, Cooldowns.ContentDimAlpha={}, Cooldowns.SelectedIndicatorEnabled={}, Cooldowns.SelectedIndicatorTintColor={}, Sounds.EnableSounds={})",
							Config::WheelBehavior::InstantSpell,
							Config::WheelBehavior::InstantSpellUseDirectCast,
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

	if (!RE::PlayerCharacter::GetSingleton() || !RE::PlayerCharacter::GetSingleton()->Is3DLoaded()) {
		return;
	}
	// begin draw
	auto ui = RE::UI::GetSingleton();
	if (!ui) {
		return;
	}

	ActionHotkeysBridge::Update();
	OStimIntegration::Update();

	std::shared_lock<std::shared_mutex> lock(_wheelDataLock);
	using namespace Config::Styling::Wheel;
	if (_state == WheelState::KClosed || !_editMode) {
		DisableEditModeGameplayInputBlock();
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
		if (TransformWheelManager::IsLichActive()) {
			if (_ammoWheel->IsOpen()) {
				_ammoWheel->HardClose();
				InputBroker::SyncWheelerActiveOwner(IsWheelerOpen(), IsAmmoWheelOpen());
				logger::info("AmmoWheel: hard-block force-close due active Lich form");
			}
		} else {
			_ammoWheel->Update(a_deltaTime);
		}
	}

	// Keep mount momentum continuous while timeslow is active and briefly after release.
	if (_wheelerRestoreMountedVelocityOnClose) {
		const double now = ImGui::GetTime();
		const bool assistActive = _wheelerModifiedTimeScale || (now < _wheelerMountedMomentumAssistUntil);
		if (assistActive) {
			Utils::Player::TryRestoreMountedVelocity(
				_wheelerMountedVelocityMountFormID,
				_wheelerMountedVelocitySnapshot,
				true);
		} else {
			ResetMountedVelocityRestoreState();
		}
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
	const bool wantGameplayBlock =
		Config::Control::Wheel::BlockGameInputInEditMode &&
		(_state != WheelState::KClosed) &&
		_editMode;
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

		auto* pc = RE::PlayerCharacter::GetSingleton();
		InventorySnapshotCache::Stats inventoryStats{};
		RE::TESObjectREFR::InventoryItemMap& inv = g_mainWheelInventorySnapshot.Get(
			pc,
			true,
			kMainWheelInventorySnapshotIntervalSeconds,
			&inventoryStats);
		if (inventoryStats.shouldLog) {
			MainWheelDebug::Log(
				MainWheelDebug::Category::Perf,
				"[Perf] MainWheel InventorySnapshot refresh ms={:.2f} interval={:.2f} countThisSecond={}",
				inventoryStats.lastRefreshMs,
				inventoryStats.refreshIntervalSeconds,
				inventoryStats.refreshCountThisWindow);
		}

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
					
					// Keep hold latched until explicit ConfirmUp/CloseWheeler reset.
					// Hover jitter or brief center hits must not silently drop hold state.
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
							// Respect both RTU:Shout and the RTU auto-instant shout toggle.
							// If auto RTU shout casting is disabled, stage sounds only advance while holding.
							if (IsRTUAutoInstantShoutEnabled()) {
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

								const bool isRTU = IsRTUAutoInstantSpellEnabled(spell, isInTransformForIndicator);

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
									TargetHand indicatorHand = TargetHand::Right;
									if (isRTU) {
										indicatorHand = ResolveTargetHandRTU(
											hoveredEntry,
											instantHoveredItem,
											ReleaseAction::CastSpell,
											false);
									} else {
										indicatorHand = _confirmHeld ? TargetHand::Right : TargetHand::Left;
										if (_confirmHeld &&
											_secondaryConfirmHeld &&
											Config::WheelBehavior::InstantSpellUseDirectCast) {
											indicatorHand = TargetHand::Both;
										}
									}
									if (Config::WheelBehavior::InstantSpellUseDirectCast) {
										indicatorHand = ResolveDirectCastHandForSpell(
											spell,
											indicatorHand,
											isRTU ? "IndicatorRTU" : "IndicatorHold",
											hoveredEntry,
											false);
									}
									_instantAttemptActive = true;
									_instantEntryIdx = hoveredEntry;
									_instantElapsedSec = elapsedSec;
									_instantReady = (elapsedSec >= thresholdSec);
									_instantTargetHand = indicatorHand;

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
				// Do not clear hold state on transient center hits; release/close owns reset.
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

		if (_editMode) {
			DrawEditModeNavigationText(drawArgs);
			if (_editModeHintsVisible) {
				DrawEditModeHintsOverlay(drawArgs, IsLastInputGamepad());
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
	WheelerAPI::ClearManagedWheels();
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
	_activeWheelIdx = -1;
	_lastHoveredEntry = -1;
	_hoveredEntryTime = 0.0f;
	_lastHoveredEntryByWheel.clear();
	_pendingExternalHotkeyDispatch.reset();
	_lastExternalHotkeyDispatchTime = 0.0;
	_lastExternalHotkeyDispatchScanCode = 0;
	_lastExternalHotkeyDispatchModifier = 0;
	ResetActionHotkeysBridgeCloseAssist();
	TransformWheelManager::Reset();
	ActionHotkeysBridge::Reset();
	ActionHotkeysBridge::RequestRefresh(true);
	OStimIntegration::Reset();
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

	// Hard-block ammo wheel while Lich form is active.
	if (TransformWheelManager::IsLichActive()) {
		if (_ammoWheel && _ammoWheel->IsOpen()) {
			_ammoWheel->HardClose();
			InputBroker::SyncWheelerActiveOwner(IsWheelerOpen(), IsAmmoWheelOpen());
		}
		logger::info("AmmoWheel: toggle blocked due active Lich form");
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

bool Wheeler::HandleAmmoWheelMouseButton(int button, bool pressed, bool fromGamepad)
{
	if (_ammoWheel && _ammoWheel->IsOpen()) {
		return _ammoWheel->HandleMouseButton(button, pressed, fromGamepad);
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

void Wheeler::RefreshAmmoWheelListIfOpen()
{
	if (_ammoWheel && _ammoWheel->IsOpen()) {
		_ammoWheel->RefreshAmmoList();
		logger::info("Wheeler: AmmoWheel list refreshed (sorting/filtering change)");
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

	// Keep wheel closed while direct-cast spell pipeline is still arming/holding.
	if (_pendingSpellActivation.has_value() || _spellHoldActive) {
		MainWheelDebug::Log(MainWheelDebug::Category::OpenClose,
			"TryOpen denied: spell pipeline active (pending={}, holdActive={})",
			_pendingSpellActivation.has_value() ? 1 : 0,
			_spellHoldActive ? 1 : 0);
		return;
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
			TryRestoreMountedVelocityAfterTimeRestore("TryCloseWheeler");
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
	if (ActionHotkeysBridge::IsBridgeWheelIndex(_activeWheelIdx)) {
		int fallbackIdx = 0;
		bool foundFallback = false;
		if (auto previous = ActionHotkeysBridge::GetPreviousWheelIndex();
			previous && *previous >= 0 && *previous < static_cast<int>(_wheels.size()) &&
			!ActionHotkeysBridge::IsBridgeWheelIndex(*previous) &&
			!TransformWheelManager::IsTransformWheelIndex(*previous) &&
			!WheelerAPI::IsManagedWheelIndex(*previous)) {
			fallbackIdx = *previous;
			foundFallback = true;
		}
		if (!foundFallback) {
			for (int i = 0; i < static_cast<int>(_wheels.size()); ++i) {
				if (ActionHotkeysBridge::IsBridgeWheelIndex(i) ||
					TransformWheelManager::IsTransformWheelIndex(i) ||
					WheelerAPI::IsManagedWheelIndex(i)) {
					continue;
				}
				fallbackIdx = i;
				foundFallback = true;
				break;
			}
		}
		if (foundFallback) {
			SetActiveWheelIndex(fallbackIdx);
			if (Config::ActionHotkeysBridge::DebugLog) {
				logger::info("ActionHotkeysBridge: open guard restored active wheel to {}", fallbackIdx);
			}
		}
	}
	if (OStimIntegration::IsManagedWheelIndex(_activeWheelIdx) &&
		!OStimIntegration::IsSceneActive()) {
		int fallbackIdx = 0;
		bool foundFallback = false;
		for (int i = 0; i < static_cast<int>(_wheels.size()); ++i) {
			if (ActionHotkeysBridge::IsBridgeWheelIndex(i) ||
				TransformWheelManager::IsTransformWheelIndex(i) ||
				WheelerAPI::IsManagedWheelIndex(i)) {
				continue;
			}
			fallbackIdx = i;
			foundFallback = true;
			break;
		}
		if (foundFallback) {
			SetActiveWheelIndex(fallbackIdx);
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
	if (!Config::Control::Wheel::EnableOpenInFavoritesMenu &&
		ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME)) {
		_lastOpenDecision = InputDecision::DeniedMenuBlocked;
		_lastOpenConsumer = InputConsumer::Other;
		if (Config::Debug::LogMenuBlockReasons) {
			logger::info("DeniedMenuBlocked consumer={} menu={} reason=EnableOpenInFavoritesMenu_OFF",
				GetMainWheelInputConsumerName(_lastOpenConsumer),
				RE::FavoritesMenu::MENU_NAME);
		}
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
	
	// Loot/Container menu handling - configurable via LootMenuOverride toggle.
	// Prefer movie visibility hide/show to preserve current loot target; fallback to close/show when needed.
	_lootMenusMovieHiddenForOverride.fill(false);
	_lootMenusClosedForOverride.fill(false);
	_lootMenuRestorePending = false;
	for (std::size_t i = 0; i < kLootOverrideMenus.size(); ++i) {
		const std::string_view menuName = kLootOverrideMenus[i];
		if (ui->IsMenuOpen(menuName)) {
			if (Config::WheelBehavior::LootMenuOverride) {
				bool hiddenByMovie = SetMenuMovieVisibility(ui, menuName, false);
				if (hiddenByMovie) {
					logger::info("MainWheel[OpenClose]: Hiding {} visibility to open Wheeler", menuName);
					_lootMenusMovieHiddenForOverride[i] = true;
				} else {
					logger::info("MainWheel[OpenClose]: Closing {} to open Wheeler (movie hide unavailable)", menuName);
					if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
						queue->AddMessage(menuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
					}
					_lootMenusClosedForOverride[i] = true;
				}
				_lootMenuRestorePending = true;
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
		ResetMountedVelocityRestoreState();
		if (slowScale <= 0.0f) {
			_wheelerModifiedTimeScale = false;
			OpenPauseMenu();
			_wheelerOwnedPauseMenu = true;
			logger::info("[TimeDilation] MainWheel pause-mode: SlowTimeScale=0 -> vanilla pause (kPausesGame)");
		} else if (slowScale < 1.0f) {
			const bool mountedAtOpen = Utils::Player::IsMounted();
			const float minSlowScale = mountedAtOpen ? kMountedMinimumSlowScale : 0.01f;
			const float effectiveScale = (std::max)(slowScale, minSlowScale);
			if (mountedAtOpen && slowScale < kMountedMinimumSlowScale) {
				logger::info("[TimeDilation] MainWheel mounted slow clamp: requested={:.3f}, clamped={:.3f}",
					slowScale, effectiveScale);
			}
			float currentTimeScale = Utils::Time::GGTM();
			// Only modify timescale if it's currently at normal (1.0) - don't override Slow Time shout or other effects
			if (currentTimeScale >= 0.99f && currentTimeScale <= 1.01f) {
				_preWheelerTimeScale = currentTimeScale;
				_wheelerModifiedTimeScale = true;
				_wheelerOwnedPauseMenu = false;
				if (Utils::Player::TryCaptureMountedVelocity(_wheelerMountedVelocitySnapshot, &_wheelerMountedVelocityMountFormID)) {
					_wheelerRestoreMountedVelocityOnClose = true;
					logger::info("[TimeDilation] MainWheel mounted velocity snapshot: mount={:08X}, v=({:.2f},{:.2f},{:.2f})",
						_wheelerMountedVelocityMountFormID,
						_wheelerMountedVelocitySnapshot.x,
						_wheelerMountedVelocitySnapshot.y,
						_wheelerMountedVelocitySnapshot.z);
				}
				Utils::Time::SGTM(effectiveScale);
				logger::info("[TimeDilation] MainWheel apply: before={:.3f}, after={:.3f}, cached={:.3f}",
					currentTimeScale, effectiveScale, _preWheelerTimeScale);
				if (_wheelerRestoreMountedVelocityOnClose) {
					Utils::Player::TryRestoreMountedVelocity(
						_wheelerMountedVelocityMountFormID,
						_wheelerMountedVelocitySnapshot,
						true);
				}
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
		_secondaryImmediateConsumedUntilRelease = false;
		// Sound feedback removed for compatibility with newer CommonLibSSE-NG
		// Notify External API of wheel open
		WheelerAPI::NotifyWheelStateChanged(GetActiveWheelIndex(), true);
	}
}

void Wheeler::CloseWheeler()
{
	g_mainWheelInventorySnapshot.Invalidate();

	// Cancel any in-progress hold-to-use input state when the wheel closes.
	// ConfirmUp can be blocked by broker routing after close, which would otherwise
	// leave stale hold timers and make shout indicators appear "stuck" on next open.
	_confirmHeld = false;
	_confirmHoldStartTime = 0.0f;
	_confirmHoldSeconds = 0.0f;
	_confirmHoldEntryIdx = -1;
	_secondaryConfirmHeld = false;
	_secondaryConfirmHoldStartTime = 0.0f;
	_secondaryConfirmHoldSeconds = 0.0f;
	_secondaryConfirmHoldEntryIdx = -1;
	_secondaryImmediateConsumedUntilRelease = false;
	_lastHoveredEntry = -1;
	_hoveredEntryTime = 0.0f;
	ResetShoutStageSounds();

	if (_wheelerOwnedPauseMenu) {
		logger::info("[TimeDilation] MainWheel restore: closing owned pause menu");
		ClosePauseMenu();
		_wheelerOwnedPauseMenu = false;
	}
	// Always restore pause/timescale first, even if player isn't loaded (prevents stuck slow time on save load/death)
	if (_wheelerModifiedTimeScale) {
		Utils::Time::SGTM(_preWheelerTimeScale);
		_wheelerModifiedTimeScale = false;
		TryRestoreMountedVelocityAfterTimeRestore("CloseWheeler");
	}

	DisableEditModeGameplayInputBlock();
	
	if (!RE::PlayerCharacter::GetSingleton() || !RE::PlayerCharacter::GetSingleton()->Is3DLoaded()) {
		_state = WheelState::KClosed;  // Still update state even without player
		if (!IsAmmoWheelOpen()) {
			InputBroker::ClearActiveOwner(InputBroker::kWheelerRefinedPluginId);
		}
		ActionHotkeysBridge::OnWheelClosed();
		WheelerAPI::NotifyWheelStateChanged(GetActiveWheelIndex(), false);
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
		_instantTargetHand = TargetHand::Right;
		_gamepadOpenGraceUntil = 0.0;
	}
	_state = WheelState::KClosed;
	if (!IsAmmoWheelOpen()) {
		InputBroker::ClearActiveOwner(InputBroker::kWheelerRefinedPluginId);
	}
	ActionHotkeysBridge::OnWheelClosed();
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
			controlMap->ToggleControls(flag, false, true);
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
		// Keep state armed and retry next frame when ControlMap is available.
		return;
	}

	for (const auto flag : kEditModeGameplayBlockFlags) {
		const std::uint32_t mask = static_cast<std::uint32_t>(flag);
		if ((_editModeGameplayInputBlocker.disabledByUsMask & mask) != 0) {
			controlMap->ToggleControls(flag, true, true);
		}
	}

	_editModeGameplayInputBlocker.active = false;
	_editModeGameplayInputBlocker.disabledByUsMask = 0;
}

void Wheeler::ResetMountedVelocityRestoreState()
{
	_wheelerRestoreMountedVelocityOnClose = false;
	_wheelerMountedVelocityMountFormID = 0;
	_wheelerMountedVelocitySnapshot = RE::NiPoint3{ 0.0f, 0.0f, 0.0f };
	_wheelerMountedMomentumAssistUntil = 0.0;
}

void Wheeler::TryRestoreMountedVelocityAfterTimeRestore(const char* a_reason)
{
	if (!_wheelerRestoreMountedVelocityOnClose) {
		return;
	}

	const bool restored = Utils::Player::TryRestoreMountedVelocity(
		_wheelerMountedVelocityMountFormID,
		_wheelerMountedVelocitySnapshot,
		true);

	logger::info("[TimeDilation] MainWheel mounted velocity restore ({}): ok={}, mount={:08X}, v=({:.2f},{:.2f},{:.2f})",
		a_reason ? a_reason : "unknown",
		restored,
		_wheelerMountedVelocityMountFormID,
		_wheelerMountedVelocitySnapshot.x,
		_wheelerMountedVelocitySnapshot.y,
		_wheelerMountedVelocitySnapshot.z);

	const double now = ImGui::GetTime();
	_wheelerMountedMomentumAssistUntil = (std::max)(_wheelerMountedMomentumAssistUntil, now + kMountedMomentumAssistSeconds);
	logger::info("[TimeDilation] MainWheel mounted momentum assist: duration={:.2f}s, until={:.3f}", kMountedMomentumAssistSeconds, _wheelerMountedMomentumAssistUntil);
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
		TryRestoreMountedVelocityAfterTimeRestore("EnsureTimescaleRestored");
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
		const bool restrictToTransformWheels = TransformWheelManager::ShouldRestrictNavigationToTransformWheels();
		const bool restrictToBridgeWheels = ActionHotkeysBridge::IsNavigationRestrictedToBridgeWheels();
		const bool debugTransform = Config::WheelBehavior::TransformWheels::DebugLog;
		const auto isTransformIdx = [](int idx) {
			return TransformWheelManager::IsTransformWheelIndex(idx);
		};
		const auto isBridgeIdx = [&](int idx) {
			return idx >= 0 &&
			       idx < wheelCount &&
			       _wheels[idx] &&
			       ActionHotkeysBridge::IsBridgeWheelTag(_wheels[idx]->GetClientTag());
		};

		if (restrictToBridgeWheels) {
			int startIdx = (_activeWheelIdx >= 0 && _activeWheelIdx < wheelCount) ? _activeWheelIdx : 0;
			if (!isBridgeIdx(startIdx)) {
				for (int i = 0; i < wheelCount; ++i) {
					if (isBridgeIdx(i)) {
						startIdx = i;
						break;
					}
				}
			}
			int nextIdx = startIdx;
			bool found = false;
			for (int attempts = 0; attempts < wheelCount; ++attempts) {
				nextIdx = nextIdx + 1;
				if (nextIdx >= wheelCount) {
					nextIdx = 0;
				}
				if (isBridgeIdx(nextIdx)) {
					found = true;
					break;
				}
			}
			if (!found || nextIdx == _activeWheelIdx) {
				return;
			}
			_activeWheelIdx = nextIdx;
			_wheels[_activeWheelIdx]->ResetAnimation();
			_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);
			return;
		}

		if (restrictToTransformWheels) {
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

		if (isTransformed) {
			int startIdx = (_activeWheelIdx >= 0 && _activeWheelIdx < wheelCount) ? _activeWheelIdx : 0;
			int nextIdx = startIdx;
			bool found = false;
			for (int attempts = 0; attempts < wheelCount; ++attempts) {
				nextIdx = nextIdx + 1;
				if (nextIdx >= wheelCount) {
					nextIdx = 0;
				}
				if (!isBridgeIdx(nextIdx)) {
					found = true;
					break;
				}
			}
			if (!found || nextIdx == _activeWheelIdx) {
				return;
			}
			const int oldIdx = _activeWheelIdx;
			_activeWheelIdx = nextIdx;
			_wheels[_activeWheelIdx]->ResetAnimation();
			_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);
			if (debugTransform) {
				logger::info("TransformWheels: NextWheel overlay {} -> {} tag='{}'",
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
			if (!isTransformIdx(nextIdx) && !isBridgeIdx(nextIdx)) {
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
		const bool restrictToTransformWheels = TransformWheelManager::ShouldRestrictNavigationToTransformWheels();
		const bool restrictToBridgeWheels = ActionHotkeysBridge::IsNavigationRestrictedToBridgeWheels();
		const bool debugTransform = Config::WheelBehavior::TransformWheels::DebugLog;
		const auto isTransformIdx = [](int idx) {
			return TransformWheelManager::IsTransformWheelIndex(idx);
		};
		const auto isBridgeIdx = [&](int idx) {
			return idx >= 0 &&
			       idx < wheelCount &&
			       _wheels[idx] &&
			       ActionHotkeysBridge::IsBridgeWheelTag(_wheels[idx]->GetClientTag());
		};

		if (restrictToBridgeWheels) {
			int startIdx = (_activeWheelIdx >= 0 && _activeWheelIdx < wheelCount) ? _activeWheelIdx : 0;
			if (!isBridgeIdx(startIdx)) {
				for (int i = 0; i < wheelCount; ++i) {
					if (isBridgeIdx(i)) {
						startIdx = i;
						break;
					}
				}
			}
			int nextIdx = startIdx;
			bool found = false;
			for (int attempts = 0; attempts < wheelCount; ++attempts) {
				nextIdx = nextIdx - 1;
				if (nextIdx < 0) {
					nextIdx = wheelCount - 1;
				}
				if (isBridgeIdx(nextIdx)) {
					found = true;
					break;
				}
			}
			if (!found || nextIdx == _activeWheelIdx) {
				return;
			}
			_activeWheelIdx = nextIdx;
			_wheels[_activeWheelIdx]->ResetAnimation();
			_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);
			return;
		}

		if (restrictToTransformWheels) {
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

		if (isTransformed) {
			int startIdx = (_activeWheelIdx >= 0 && _activeWheelIdx < wheelCount) ? _activeWheelIdx : 0;
			int nextIdx = startIdx;
			bool found = false;
			for (int attempts = 0; attempts < wheelCount; ++attempts) {
				nextIdx = nextIdx - 1;
				if (nextIdx < 0) {
					nextIdx = wheelCount - 1;
				}
				if (!isBridgeIdx(nextIdx)) {
					found = true;
					break;
				}
			}
			if (!found || nextIdx == _activeWheelIdx) {
				return;
			}
			const int oldIdx = _activeWheelIdx;
			_activeWheelIdx = nextIdx;
			_wheels[_activeWheelIdx]->ResetAnimation();
			_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);
			if (debugTransform) {
				logger::info("TransformWheels: PrevWheel overlay {} -> {} tag='{}'",
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
			if (!isTransformIdx(nextIdx) && !isBridgeIdx(nextIdx)) {
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

bool Wheeler::SwitchToWheelIndexForNavigation(int a_index)
{
	if (_state != WheelState::KOpened) {
		return false;
	}
	if (a_index < 0 || a_index >= static_cast<int>(_wheels.size())) {
		return false;
	}
	if (a_index == _activeWheelIdx) {
		return true;
	}

	_cursorPos = { 0, 0 };
	if (_activeWheelIdx >= 0 && _activeWheelIdx < static_cast<int>(_wheels.size()) && _wheels[_activeWheelIdx]) {
		_wheels[_activeWheelIdx]->ResetAnimation();
		_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);
	}

	_activeWheelIdx = a_index;
	if (_activeWheelIdx >= 0 && _activeWheelIdx < static_cast<int>(_wheels.size()) && _wheels[_activeWheelIdx]) {
		_wheels[_activeWheelIdx]->ResetAnimation();
		_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);
	}

	return true;
}

bool Wheeler::SetWheelHoveredEntryIndex(int a_wheelIndex, int a_entryIndex, bool a_moveCursorToEntry)
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	if (a_wheelIndex < 0 || a_wheelIndex >= static_cast<int>(_wheels.size())) {
		return false;
	}
	if (!_wheels[a_wheelIndex]) {
		return false;
	}

	const int entryCount = _wheels[a_wheelIndex]->GetNumEntries();
	int clampedIndex = a_entryIndex;
	if (entryCount <= 0) {
		clampedIndex = -1;
	} else {
		clampedIndex = (std::max)(-1, (std::min)(clampedIndex, entryCount - 1));
	}

	_wheels[a_wheelIndex]->SetHoveredEntryIndex(clampedIndex);
	if (_lastHoveredEntryByWheel.size() != _wheels.size()) {
		_lastHoveredEntryByWheel.resize(_wheels.size(), -1);
	}
	_lastHoveredEntryByWheel[a_wheelIndex] = clampedIndex;
	_hoveredEntryTime = 0.0f;

	if (a_moveCursorToEntry && _state == WheelState::KOpened && a_wheelIndex == _activeWheelIdx) {
		if (clampedIndex >= 0 && entryCount > 0) {
			const float angle = GetEntryCenterAngleRad(clampedIndex, entryCount);
			const float cursorRadius = getCursorRadiusMax();
			_cursorPos = { cosf(angle) * cursorRadius, sinf(angle) * cursorRadius };
			_gamepadIntentActive = true;
		} else {
			_cursorPos = { 0.0f, 0.0f };
		}
	}

	return true;
}

void Wheeler::RefreshActionHotkeysMirror()
{
	ActionHotkeysBridge::RequestRefresh(true);
}

void Wheeler::ResetActionHotkeysBridgeLayout()
{
	ActionHotkeysBridge::ResetPersistedLayout(true);
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
	if (_wheels.empty() || !HasValidActiveWheel_NoLock()) {
		return;
	}
	if (_state != WheelState::KOpened) {
		return;
	}
	if (_state == WheelState::KOpened) {
		const bool allowEditMutation = _editMode;
		std::unique_ptr<Wheel>& activeWheel = _wheels[_activeWheelIdx];
		if (activeWheel->IsEmpty()) {         // empty wheel, we can only delete in edit mode.
			if (allowEditMutation && _wheels.size() > 1) {  // we have more than one wheel, so it's safe to delete this one.
				DeleteCurrentWheel();
			}
		} else {
			if (allowEditMutation) {
				// Legacy Wheeler edit mode keeps delete semantics; FavWheel edit mode falls through
				// to normal activation so slot management is handled only by reorder actions.
				activeWheel->ActivateHoveredEntrySecondary(true);
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
					if (IsBowLikeFormID(formId)) {
						logger::info("[HandMemoryDiag] DirectSecondaryRTUOffBowLike entry={} formId={:08X} immediateNotify=0",
							hoveredIdx,
							formId);
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
	if (_wheels.empty() || !HasValidActiveWheel_NoLock()) {
		return;
	}
	if (_state != WheelState::KOpened) {
		return;
	}
	if (_state == WheelState::KOpened) {
		const bool allowEditMutation = _editMode;
		int hoveredIdx = -1;
		RE::FormID hoveredFormID = 0;
		if (!allowEditMutation) {
			std::unique_ptr<Wheel>& activeWheel = _wheels[_activeWheelIdx];
			if (activeWheel) {
				hoveredIdx = activeWheel->GetHoveredEntryIndex();
				if (hoveredIdx < 0) {
					return;
				}
				if (WheelEntry* entry = activeWheel->GetEntry(hoveredIdx); entry && entry->IsMissingInInventory()) {
					return;
				}
				if (std::shared_ptr<WheelItem> hoveredItem = activeWheel->GetHoveredSelectedItem()) {
					hoveredFormID = hoveredItem->GetFormID();
				}
			}
		}
		_wheels[_activeWheelIdx]->ActivateHoveredEntryPrimary(allowEditMutation);
		if (!allowEditMutation && !Config::WheelBehavior::ReleaseToUse && IsBowLikeFormID(hoveredFormID)) {
			logger::info("[HandMemoryDiag] DirectPrimaryRTUOffBowLike entry={} formId={:08X} immediateNotify=0",
				hoveredIdx,
				hoveredFormID);
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
	if (_wheels.empty() || !HasValidActiveWheel_NoLock()) {
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
	if (_state != WheelState::KOpened) {
		return;
	}
	
	// In edit mode, use immediate activation (add item to wheel)
	if (_editMode) {
		std::unique_lock<std::shared_mutex> wheelDataLock(_wheelDataLock);
		if (!EnsureValidActiveWheelForEdit_NoLock(std::nullopt, "OnConfirmDown")) {
			return;
		}
		_wheels[_activeWheelIdx]->ActivateHoveredEntryPrimary(true);
		return;
	}
	
	int hoveredIdx = -1;
	{
		std::shared_lock<std::shared_mutex> wheelDataLock(_wheelDataLock);
		if (!HasValidActiveWheel_NoLock()) {
			return;
		}
		Wheel* activeWheel = _wheels[_activeWheelIdx].get();
		hoveredIdx = activeWheel->GetHoveredEntryIndex();
		if (hoveredIdx < 0) {
			return;
		}
		if (WheelEntry* entry = activeWheel->GetEntry(hoveredIdx); entry && entry->IsMissingInInventory()) {
			return;
		}
	}
	if (_confirmHeld) {
		logger::info("ConfirmDown: ignored repeated hold-down while already active entry={} heldEntry={}", hoveredIdx, _confirmHoldEntryIdx);
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

	if (_state != WheelState::KOpened || _editMode) {
		return;
	}

	int currentHoveredIdx = -1;
	std::shared_ptr<WheelItem> hoveredItem;
	{
		std::shared_lock<std::shared_mutex> wheelDataLock(_wheelDataLock);
		if (!HasValidActiveWheel_NoLock()) {
			return;
		}
		Wheel* activeWheel = _wheels[_activeWheelIdx].get();
		currentHoveredIdx = activeWheel->GetHoveredEntryIndex();

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
		hoveredItem = activeWheel->GetHoveredSelectedItem();
		if (!hoveredItem) {
			return;
		}
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
	RE::SpellItem* guardedSpell = spellItem ? spellItem->GetSpell() : nullptr;
	if (guardedSpell && IsSpellBlockedByTransformGuard(guardedSpell, "HoldPrimary")) {
		return;
	}

	// InstantShout via Hold-to-Use
	// Threshold to distinguish Tap (Equip) vs Hold (Shout)
	// Delay prevents instant firing on quick taps.
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

	TargetHand resolvedHand = ResolveTargetHand(currentHoveredIdx);
	const bool combineWithSecondaryHeld =
		resolvedAction == ReleaseAction::CastSpell &&
		spellItem &&
		Config::WheelBehavior::InstantSpellUseDirectCast &&
		_secondaryConfirmHeld &&
		_secondaryConfirmHoldEntryIdx == heldEntryIdx;
	if (combineWithSecondaryHeld) {
		resolvedHand = TargetHand::Both;
		_secondaryConfirmHeld = false;
		_secondaryConfirmHoldSeconds = 0.0f;
		_secondaryConfirmHoldEntryIdx = -1;
		if (_handOverrideActive && _handOverrideEntryIdx == heldEntryIdx && _handOverrideLeft) {
			_handOverrideActive = false;
			_handOverrideEntryIdx = -1;
			_handOverrideLeft = false;
		}
		logger::info("ReleaseResolve[Hold]: source={} combined simultaneous hold -> hand=BOTH (primary-first)", sourceLabel);
	}
	if (resolvedAction == ReleaseAction::CastSpell &&
		spellItem &&
		Config::WheelBehavior::InstantSpellUseDirectCast) {
		resolvedHand = ResolveDirectCastHandForSpell(
			spellItem->GetSpell(),
			resolvedHand,
			sourceLabel,
			currentHoveredIdx);
	}
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
		} else if (!spell) {
			logger::info("ReleaseResolve[Hold]: cast skipped (spell null), fallback to equip entry={} formId={:08X}",
				currentHoveredIdx, formId);
			resolvedAction = ReleaseAction::Equip;
		} else if (Config::WheelBehavior::InstantSpellUseDirectCast) {
			const float concentrationHoldSeconds =
				spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration ?
				Config::WheelBehavior::InstantSpellConcentrationMaxSeconds :
				0.0f;
			if (resolvedHand == TargetHand::Both) {
				const bool queuedPrimary = QueueSpellActivation(spell->GetFormID(), TargetHand::Right, concentrationHoldSeconds);
				const bool queuedSecondary = queuedPrimary && QueueSpellActivation(spell->GetFormID(), TargetHand::Left, concentrationHoldSeconds);
				activated = queuedPrimary && queuedSecondary;
			} else {
				activated = QueueSpellActivation(spell->GetFormID(), resolvedHand, concentrationHoldSeconds);
			}
			if (activated) {
				logger::info("Hold-to-Use: DirectCast queued entry={} inTransform={} hand={} holdSec={:.2f}",
					currentHoveredIdx, isInTransform, GetTargetHandName(resolvedHand), concentrationHoldSeconds);
				_rtuConsumedByInstant = true;
			} else {
				logger::info("ReleaseResolve[Hold]: direct cast queue failed, fallback to equip entry={} formId={:08X}",
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
		if (!hoveredItem || (hoveredItem->RequiresRuntimeFormValidation() && formId == 0)) {
			logger::info(
				"WheelDispatch: skipped weapon activation entry={} hand={} formId={:08X} reason=invalid_form",
				currentHoveredIdx,
				GetTargetHandName(resolvedHand),
				formId);
			return;
		}
		
		{
			std::shared_lock<std::shared_mutex> wheelDataLock(_wheelDataLock);
			if (!HasValidActiveWheel_NoLock()) {
				return;
			}
			Wheel* activeWheel = _wheels[_activeWheelIdx].get();
			if (activeWheel->GetHoveredEntryIndex() != currentHoveredIdx) {
				return;
			}
			if (useLeft) {
				activeWheel->ActivateHoveredEntrySecondary(false);
			} else {
				activeWheel->ActivateHoveredEntryPrimary(false);
			}
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
			HandMemory::NotifyWheelEquipOrCast(hoveredItem, resolvedAction);
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
	if (_state != WheelState::KOpened) {
		return;
	}

	// In edit mode, use immediate secondary activation (delete)
	if (_editMode) {
		ActivateHoveredEntrySecondary();
		return;
	}

	int hoveredIdx = -1;
	std::shared_ptr<WheelItem> hoveredItem;
	{
		std::shared_lock<std::shared_mutex> wheelDataLock(_wheelDataLock);
		if (!HasValidActiveWheel_NoLock()) {
			return;
		}
		Wheel* activeWheel = _wheels[_activeWheelIdx].get();
		hoveredIdx = activeWheel->GetHoveredEntryIndex();
		if (hoveredIdx < 0) {
			return;
		}
		if (WheelEntry* entry = activeWheel->GetEntry(hoveredIdx); entry && entry->IsMissingInInventory()) {
			return;
		}
		hoveredItem = activeWheel->GetHoveredSelectedItem();
	}
	if (_secondaryConfirmHeld) {
		logger::info(
			"SecondaryConfirmDown: ignored repeated hold-down while already active entry={} heldEntry={}",
			hoveredIdx,
			_secondaryConfirmHoldEntryIdx);
		return;
	}

	MainWheelDebug::Log(MainWheelDebug::Category::Input, "SecondaryConfirmDown entry={}", hoveredIdx);
	if (IsControllerDebugEnabled()) {
		logger::info("[Controller] SecondaryConfirmDown entry={}", hoveredIdx);
	}

	// RTU OFF: preserve direct RMB activation behavior
	if (!Config::WheelBehavior::ReleaseToUse) {
		if (std::shared_ptr<WheelItemSpell> spellItem = std::dynamic_pointer_cast<WheelItemSpell>(hoveredItem)) {
			RE::SpellItem* spell = spellItem->GetSpell();
			const bool isInTransform = !TransformWheelManager::IsPlayerHuman();
			const bool instantEnabled = IsInstantEnabledForSpell(spell, isInTransform);

			// For instant-enabled spells, secondary input must enter hold/release pipeline
			// so direct-cast can be evaluated on SecondaryConfirmUp (left-hand cast path).
			if (instantEnabled) {
				_secondaryConfirmHeld = true;
				_secondaryConfirmHoldStartTime = ImGui::GetTime();
				_secondaryConfirmHoldSeconds = 0.f;
				_secondaryConfirmHoldEntryIdx = hoveredIdx;
				return;
			}
		}
		if (_secondaryImmediateConsumedUntilRelease) {
			logger::info("SecondaryConfirmDown: ignored repeated direct secondary until release entry={}", hoveredIdx);
			return;
		}
		_secondaryImmediateConsumedUntilRelease = true;
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
	_secondaryImmediateConsumedUntilRelease = false;

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

	if (_state != WheelState::KOpened || _editMode) {
		return;
	}

	int currentHoveredIdx = -1;
	std::shared_ptr<WheelItem> hoveredItem;
	{
		std::shared_lock<std::shared_mutex> wheelDataLock(_wheelDataLock);
		if (!HasValidActiveWheel_NoLock()) {
			return;
		}
		Wheel* activeWheel = _wheels[_activeWheelIdx].get();
		currentHoveredIdx = activeWheel->GetHoveredEntryIndex();

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
		hoveredItem = activeWheel->GetHoveredSelectedItem();
		if (!hoveredItem) {
			return;
		}
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
	RE::SpellItem* guardedSpell = spellItem ? spellItem->GetSpell() : nullptr;
	if (guardedSpell && IsSpellBlockedByTransformGuard(guardedSpell, "HoldSecondary")) {
		return;
	}

	// InstantShout via Hold-to-Use
	// Threshold to distinguish Tap (Equip) vs Hold (Shout)
	// Delay prevents instant firing on quick taps.
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

	// Secondary direct-cast intent is always left hand (RMB/LB path).
	TargetHand resolvedHand = TargetHand::Left;
	const bool combineWithPrimaryHeld =
		resolvedAction == ReleaseAction::CastSpell &&
		spellItem &&
		Config::WheelBehavior::InstantSpellUseDirectCast &&
		_confirmHeld &&
		_confirmHoldEntryIdx == heldEntryIdx;
	if (combineWithPrimaryHeld) {
		resolvedHand = TargetHand::Both;
		_confirmHeld = false;
		_confirmHoldSeconds = 0.0f;
		_confirmHoldEntryIdx = -1;
		logger::info("ReleaseResolve[Hold]: source={} combined simultaneous hold -> hand=BOTH (secondary-first)", sourceLabel);
	}
	if (resolvedAction == ReleaseAction::CastSpell &&
		spellItem &&
		Config::WheelBehavior::InstantSpellUseDirectCast) {
		resolvedHand = ResolveDirectCastHandForSpell(
			spellItem->GetSpell(),
			resolvedHand,
			sourceLabel,
			currentHoveredIdx);
	}
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
		} else if (!spell) {
			logger::info("ReleaseResolve[Hold]: cast skipped (spell null), fallback to equip entry={} formId={:08X}",
				currentHoveredIdx, formId);
			resolvedAction = ReleaseAction::Equip;
		} else if (Config::WheelBehavior::InstantSpellUseDirectCast) {
			const float concentrationHoldSeconds =
				spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration ?
				Config::WheelBehavior::InstantSpellConcentrationMaxSeconds :
				0.0f;
			if (resolvedHand == TargetHand::Both) {
				const bool queuedPrimary = QueueSpellActivation(spell->GetFormID(), TargetHand::Left, concentrationHoldSeconds);
				const bool queuedSecondary = queuedPrimary && QueueSpellActivation(spell->GetFormID(), TargetHand::Right, concentrationHoldSeconds);
				activated = queuedPrimary && queuedSecondary;
			} else {
				activated = QueueSpellActivation(spell->GetFormID(), resolvedHand, concentrationHoldSeconds);
			}
			if (activated) {
				logger::info("Hold-to-Use: DirectCast queued entry={} inTransform={} hand={} holdSec={:.2f}",
					currentHoveredIdx, isInTransform, GetTargetHandName(resolvedHand), concentrationHoldSeconds);
				_rtuConsumedByInstant = true;
			} else {
				logger::info("ReleaseResolve[Hold]: direct cast queue failed, fallback to equip entry={} formId={:08X}",
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
		if (!hoveredItem || (hoveredItem->RequiresRuntimeFormValidation() && formId == 0)) {
			logger::info(
				"WheelDispatch: skipped weapon activation entry={} hand={} formId={:08X} reason=invalid_form",
				currentHoveredIdx,
				GetTargetHandName(resolvedHand),
				formId);
			return;
		}

		{
			std::shared_lock<std::shared_mutex> wheelDataLock(_wheelDataLock);
			if (!HasValidActiveWheel_NoLock()) {
				return;
			}
			Wheel* activeWheel = _wheels[_activeWheelIdx].get();
			if (activeWheel->GetHoveredEntryIndex() != currentHoveredIdx) {
				return;
			}
			activeWheel->ActivateHoveredEntrySecondary(false);
		}
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
			HandMemory::NotifyWheelEquipOrCast(hoveredItem, resolvedAction);
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

Wheeler::TargetHand Wheeler::ResolveTargetHandRTU(int entryIdx, const std::shared_ptr<WheelItem>& hoveredItem, ReleaseAction resolvedAction, bool a_logDecision)
{
	auto fallback = [&]() {
		return ResolveTargetHand(entryIdx);
	};

	const RE::FormID formId = hoveredItem ? hoveredItem->GetFormID() : 0;
	auto logDecision = [&](TargetHand chosen, const char* reason, std::uint32_t allowedMask, int leftEmpty, int rightEmpty, SmartAssignCategory category) {
		if (!a_logDecision || !Config::WheelBehavior::RTUSmartAssignDebugLog) {
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
	// RTU direct-cast should honor explicit hand intent (R/L override) instead of
	// SmartAssign, which can otherwise force all casts to one side (e.g. LEFT-only mask).
	if (resolvedAction == ReleaseAction::CastSpell &&
		Config::WheelBehavior::InstantSpellUseDirectCast) {
		TargetHand hand = fallback();
		logDecision(hand, "cast_spell_direct_pipeline_fallback", 0, -1, -1, SmartAssignCategory::Spells);
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

bool Wheeler::GetInstantSpellState(int& outEntryIdx, float& outElapsed, float& outThreshold, bool& outReady, bool& outCancelled, TargetHand& outHand)
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
	outHand = _instantTargetHand;
	return true;
}

void Wheeler::ToggleEditModeHintsVisibility()
{
	if (!_editMode || _state == WheelState::KClosed || !Config::MainWheel::EditHints::Enabled) {
		return;
	}
	_editModeHintsVisible = !_editModeHintsVisible;
}

bool Wheeler::HasValidActiveWheel_NoLock()
{
	return !_wheels.empty() &&
	       _activeWheelIdx >= 0 &&
	       _activeWheelIdx < static_cast<int>(_wheels.size()) &&
	       _wheels[_activeWheelIdx] != nullptr;
}

bool Wheeler::EnsureValidActiveWheelForEdit_NoLock(std::optional<int> a_preferredIndex, const char* a_context)
{
	if (HasValidActiveWheel_NoLock()) {
		return true;
	}

	const int activeBefore = _activeWheelIdx;
	if (_wheels.empty()) {
		_activeWheelIdx = -1;
		return false;
	}

	auto isUsableWheel = [&](int idx) {
		return idx >= 0 &&
		       idx < static_cast<int>(_wheels.size()) &&
		       _wheels[idx] != nullptr;
	};

	int sanitizedIdx = -1;
	if (a_preferredIndex.has_value() && isUsableWheel(*a_preferredIndex)) {
		sanitizedIdx = *a_preferredIndex;
	} else if (isUsableWheel(0)) {
		sanitizedIdx = 0;
	} else {
		for (int i = 0; i < static_cast<int>(_wheels.size()); ++i) {
			if (_wheels[i]) {
				sanitizedIdx = i;
				break;
			}
		}
	}

	if (sanitizedIdx < 0) {
		static double s_lastNullWheelWarnTime = 0.0;
		const double now = ImGui::GetTime();
		if (now - s_lastNullWheelWarnTime >= 1.0) {
			logger::warn("Wheeler: wheels exist but no valid wheel object is available (active={}, wheels={})",
				activeBefore,
				_wheels.size());
			s_lastNullWheelWarnTime = now;
		}
		_activeWheelIdx = -1;
		return false;
	}

	_activeWheelIdx = sanitizedIdx;

	static double s_lastSanitizeWarnTime = 0.0;
	const double now = ImGui::GetTime();
	if (now - s_lastSanitizeWarnTime >= 1.0) {
		if (a_context && a_context[0] != '\0') {
			logger::warn("Wheeler: sanitized invalid active wheel index (active={}, wheels={}) -> {} ({})",
				activeBefore,
				_wheels.size(),
				sanitizedIdx,
				a_context);
		} else {
			logger::warn("Wheeler: sanitized invalid active wheel index (active={}, wheels={}) -> {}",
				activeBefore,
				_wheels.size(),
				sanitizedIdx);
		}
		s_lastSanitizeWarnTime = now;
	}

	return true;
}

void Wheeler::AddEmptyEntryToCurrentWheel()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	if (!_editMode || _state == WheelState::KClosed) {
		return;
	}
	// Clear/Revert can leave _activeWheelIdx stale while wheel data is rebuilt in edit mode.
	if (!EnsureValidActiveWheelForEdit_NoLock(std::nullopt, "AddEmptyEntryToCurrentWheel")) {
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
	const bool hadValidActive = HasValidActiveWheel_NoLock();
	_wheels.push_back(std::make_unique<Wheel>());
	if (!hadValidActive) {
		_activeWheelIdx = static_cast<int>(_wheels.size()) - 1;
		_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);
	}
	_lastHoveredEntryByWheel.resize(_wheels.size(), -1);
}

void Wheeler::PushWheel()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	const bool hadValidActive = HasValidActiveWheel_NoLock();
	_wheels.push_back(std::make_unique<Wheel>());
	if (!hadValidActive) {
		_activeWheelIdx = static_cast<int>(_wheels.size()) - 1;
		_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);
	}
	_lastHoveredEntryByWheel.resize(_wheels.size(), -1);
}

void Wheeler::DeleteCurrentWheel()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	if (!_editMode || _state == WheelState::KClosed) {
		return;
	}
	if (!EnsureValidActiveWheelForEdit_NoLock(std::nullopt, "DeleteCurrentWheel")) {
		return;
	}
	if (_wheels.size() > 1) {
		const int deletedIdx = _activeWheelIdx;
		std::unique_ptr<Wheel>& toDelete = _wheels[_activeWheelIdx];
		if (!toDelete || !toDelete->IsEmpty()) { // do not delete an non-empty wheel
			return;
		}
		_wheels.erase(_wheels.begin() + _activeWheelIdx);
		if (deletedIdx >= 0 && deletedIdx < static_cast<int>(_lastHoveredEntryByWheel.size())) {
			_lastHoveredEntryByWheel.erase(_lastHoveredEntryByWheel.begin() + deletedIdx);
		}
		if (_activeWheelIdx == static_cast<int>(_wheels.size()) && _activeWheelIdx != 0) {  // deleted the last wheel
			_activeWheelIdx = static_cast<int>(_wheels.size()) - 1;
		}
		if (!EnsureValidActiveWheelForEdit_NoLock(std::nullopt, "DeleteCurrentWheel/postErase")) {
			return;
		}
		_wheels[_activeWheelIdx]->SetHoveredEntryIndex(-1);  // reset active entry for new wheel
		_lastHoveredEntryByWheel.resize(_wheels.size(), -1);
	}
}

void Wheeler::MoveEntryForwardInCurrentWheel()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	if (!_editMode || _state == WheelState::KClosed) {
		return;
	}
	if (!EnsureValidActiveWheelForEdit_NoLock(std::nullopt, "MoveEntryForwardInCurrentWheel")) {
		return;
	}
	_wheels[_activeWheelIdx]->MoveHoveredEntryForward();
}

void Wheeler::MoveEntryBackInCurrentWheel()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	if (!_editMode || _state == WheelState::KClosed) {
		return;
	}
	if (!EnsureValidActiveWheelForEdit_NoLock(std::nullopt, "MoveEntryBackInCurrentWheel")) {
		return;
	}
	_wheels[_activeWheelIdx]->MoveHoveredEntryBack();
}

void Wheeler::MoveWheelForward()
{
	std::unique_lock<std::shared_mutex> lock(_wheelDataLock);
	if (!_editMode || _state == WheelState::KClosed) {
		return;
	}
	if (!EnsureValidActiveWheelForEdit_NoLock(std::nullopt, "MoveWheelForward")) {
		return;
	}
	if (_wheels.size() > 1) {
		int targetIdx;
		if (_activeWheelIdx == static_cast<int>(_wheels.size()) - 1) {
			// Move the current wheel to the very front
			targetIdx = 0;
			auto currentWheel = std::move(_wheels.back());
			_wheels.pop_back();
			_wheels.insert(_wheels.begin(), std::move(currentWheel));
			if (_lastHoveredEntryByWheel.size() == _wheels.size() && !_lastHoveredEntryByWheel.empty()) {
				const int currentHovered = _lastHoveredEntryByWheel.back();
				_lastHoveredEntryByWheel.pop_back();
				_lastHoveredEntryByWheel.insert(_lastHoveredEntryByWheel.begin(), currentHovered);
			}
		} else {
			targetIdx = _activeWheelIdx + 1;
			std::swap(_wheels[_activeWheelIdx], _wheels[targetIdx]);
			if (_activeWheelIdx >= 0 &&
				targetIdx >= 0 &&
				targetIdx < static_cast<int>(_lastHoveredEntryByWheel.size()) &&
				_activeWheelIdx < static_cast<int>(_lastHoveredEntryByWheel.size())) {
				std::swap(_lastHoveredEntryByWheel[_activeWheelIdx], _lastHoveredEntryByWheel[targetIdx]);
			}
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
	if (!EnsureValidActiveWheelForEdit_NoLock(std::nullopt, "MoveWheelBack")) {
		return;
	}
	if (_wheels.size() > 1) {
		int targetIdx;
		if (_activeWheelIdx == 0) {
			// Move the current wheel to the very end
			targetIdx = static_cast<int>(_wheels.size()) - 1;
			auto currentWheel = std::move(_wheels.front());
			_wheels.erase(_wheels.begin());
			_wheels.push_back(std::move(currentWheel));
			if (!_lastHoveredEntryByWheel.empty()) {
				const int currentHovered = _lastHoveredEntryByWheel.front();
				_lastHoveredEntryByWheel.erase(_lastHoveredEntryByWheel.begin());
				_lastHoveredEntryByWheel.push_back(currentHovered);
			}
			_activeWheelIdx = static_cast<int>(_wheels.size()) - 1;
		} else {
			targetIdx = _activeWheelIdx - 1;
			std::swap(_wheels[_activeWheelIdx], _wheels[targetIdx]);
			if (_activeWheelIdx >= 0 &&
				targetIdx >= 0 &&
				targetIdx < static_cast<int>(_lastHoveredEntryByWheel.size()) &&
				_activeWheelIdx < static_cast<int>(_lastHoveredEntryByWheel.size())) {
				std::swap(_lastHoveredEntryByWheel[_activeWheelIdx], _lastHoveredEntryByWheel[targetIdx]);
			}
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
		if (ActionHotkeysBridge::IsBridgeWheelIndex(_activeWheelIdx)) {
			if (auto previous = ActionHotkeysBridge::GetPreviousWheelIndex(); previous.has_value()) {
				if (auto mapped = mapRuntimeToUserIndex(*previous); mapped.has_value()) {
					activeSaveIdx = *mapped;
				}
			}
		} else if (auto saved = TransformWheelManager::GetSavedHumanWheelIndex(); saved.has_value()) {
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
		|| (Config::Control::Wheel::EnableEditModeInFavoritesMenu &&
			a_ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME));
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
	if (Config::Control::Wheel::EnableEditModeInFavoritesMenu &&
		a_ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME)) {
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
		_editModeHintsVisible = true;
		return;
	}
	
	// Selection was captured earlier in ToggleWheeler/ToggleWheelIfInInventory
	bool favOpen = ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME);
	if (!favOpen) {
		Utils::Inventory::FavoritesSelectionCache::Invalidate(); // Clear stale cache
	}
	
	_editMode = true;
	_editModeHintsVisible = true;
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
	RE::TESObjectREFR::InventoryItemMap inventoryMap;
	if (!Utils::Inventory::TryGetInventorySnapshot(pc, inventoryMap, "PruneWheelEntries_NotInInventory")) {
		if (MainWheelDebug::IsEnabled()) {
			MainWheelDebug::Log(MainWheelDebug::Category::Input, "InventoryPrune",
				"skipped reason={} source=unsafe_inventory_snapshot", reasonStr);
		}
		return;
	}
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
					inInventory = item->IsAvailable(inventoryMap);
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
						selectedInInventory = selectedItem->IsAvailable(inventoryMap);
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
