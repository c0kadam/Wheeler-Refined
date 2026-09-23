#include "Texts.h"

#include <fstream>

namespace
{
	static void loadTextFromIni(CSimpleIniA& a_ini, const char* key, std::string& r_text)
	{
		const char* val = a_ini.GetValue("Texts", key, r_text.data());
		if (val) {
			r_text = std::string(val);
		}
	}

	std::string trim(const std::string& s)
	{
		auto begin = std::find_if_not(s.begin(), s.end(), [](unsigned char ch) { return std::isspace(ch); });
		auto end = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
		if (begin >= end) {
			return {};
		}
		return std::string(begin, end);
	}

	const char* GetKeyForTextType(Texts::TextType type)
	{
		switch (type) {
		case Texts::TextType::AlchemyDynamicIDConsumptionWarning:
			return "AlchemyDynamicIDConsumptionWarning";
		case Texts::TextType::NoWheelPresent:
			return "NoWheelPresent";
		case Texts::TextType::EditHintTitle:
			return "EditHintTitle";
		case Texts::TextType::EditHintDeviceHeaderMkb:
			return "EditHintDeviceHeaderMkb";
		case Texts::TextType::EditHintDeviceHeaderGamepad:
			return "EditHintDeviceHeaderGamepad";
		case Texts::TextType::EditHintActionUsePlaceItem:
			return "EditHintActionUsePlaceItem";
		case Texts::TextType::EditHintActionRemoveItemWheel:
			return "EditHintActionRemoveItemWheel";
		case Texts::TextType::EditHintActionAddEmptySlot:
			return "EditHintActionAddEmptySlot";
		case Texts::TextType::EditHintActionAddWheel:
			return "EditHintActionAddWheel";
		case Texts::TextType::EditHintActionNextWheel:
			return "EditHintActionNextWheel";
		case Texts::TextType::EditHintActionPreviousWheel:
			return "EditHintActionPreviousWheel";
		case Texts::TextType::EditHintActionMoveSlotForward:
			return "EditHintActionMoveSlotForward";
		case Texts::TextType::EditHintActionMoveSlotBack:
			return "EditHintActionMoveSlotBack";
		case Texts::TextType::EditHintActionMoveWheelForward:
			return "EditHintActionMoveWheelForward";
		case Texts::TextType::EditHintActionMoveWheelBack:
			return "EditHintActionMoveWheelBack";
		case Texts::TextType::EditHintActionSettingsDMenu:
			return "EditHintActionSettingsDMenu";
		case Texts::TextType::EditHintActionExitWheel:
			return "EditHintActionExitWheel";
		case Texts::TextType::EditHintNavToggleMkb:
			return "EditHintNavToggleMkb";
		case Texts::TextType::EditHintNavToggleGamepad:
			return "EditHintNavToggleGamepad";
		case Texts::TextType::WheelBehaviorDefaultsSaved:
			return "WheelBehaviorDefaultsSaved";
		case Texts::TextType::WheelBehaviorRestoredToDefaults:
			return "WheelBehaviorRestoredToDefaults";
		case Texts::TextType::NoWheelBehaviorDefaultsSaved:
			return "NoWheelBehaviorDefaultsSaved";
		case Texts::TextType::PressKeyToBindAmmoWheel:
			return "PressKeyToBindAmmoWheel";
		case Texts::TextType::PressGamepadButtonToBindAmmoWheel:
			return "PressGamepadButtonToBindAmmoWheel";
		case Texts::TextType::KeybindCaptureCancelled:
			return "KeybindCaptureCancelled";
		case Texts::TextType::AmmoWheelKeybindsReset:
			return "AmmoWheelKeybindsReset";
		case Texts::TextType::PressKeyToSetModifier:
			return "PressKeyToSetModifier";
		case Texts::TextType::KeyboardModifierCleared:
			return "KeyboardModifierCleared";
		case Texts::TextType::PressGamepadButtonToSetModifier:
			return "PressGamepadButtonToSetModifier";
		case Texts::TextType::GamepadModifierCleared:
			return "GamepadModifierCleared";
		case Texts::TextType::ClickMouseButtonToBind:
			return "ClickMouseButtonToBind";
		case Texts::TextType::MouseToggleCleared:
			return "MouseToggleCleared";
		case Texts::TextType::AmmoWheelFactoryDefaultsRestored:
			return "AmmoWheelFactoryDefaultsRestored";
		case Texts::TextType::AmmoWheelFactoryDefaultsFailed:
			return "AmmoWheelFactoryDefaultsFailed";
		case Texts::TextType::InsufficientMagickaForInstantCast:
			return "InsufficientMagickaForInstantCast";
		case Texts::TextType::PoisonAlreadyApplied:
			return "PoisonAlreadyApplied";
		case Texts::TextType::PoisonSafeResolutionFailed:
			return "PoisonSafeResolutionFailed";
		case Texts::TextType::AmmoWheelWeaponPoisonLabel:
			return "AmmoWheelWeaponPoisonLabel";
		default:
			return "";
		}
	}
}

void Texts::LoadTranslations()
{
	// Base texts from INI (existing behavior)
	CSimpleIniA ini;
	ini.SetUnicode();
#define TEXTS_PATH "Data\\SKSE\\Plugins\\wheeler\\Texts.ini"
	ini.LoadFile(TEXTS_PATH);
	try {
		for (auto& [textType, text] : _textData) {
			const char* key = GetKeyForTextType(textType);
			if (key && *key) {
				loadTextFromIni(ini, key, text);
			}
		}
	}
	catch (std::exception e) {
		ERROR("Error loading from Texts.ini: {}", e.what());
	}

	// Optional overrides from a simple UTF-8 translation file:
	// Data\SKSE\Plugins\wheeler\translations.txt
	const std::string path = "Data\\SKSE\\Plugins\\wheeler\\translations.txt";
	std::ifstream file(path);
	if (!file.is_open()) {
		INFO("Custom text translation file not found: {}", path);
		return;
	}

	INFO("Loading Wheeler text translations from {}", path);

	std::unordered_map<std::string, std::string> overrides;
	std::unordered_map<std::string, int> keyFirstLineNum;  // Track first occurrence line for duplicate warnings
	std::string line;
	int lineNum = 0;
	int entryCount = 0;
	while (std::getline(file, line)) {
		++lineNum;
		if (line.empty()) {
			continue;
		}
		// handle UTF-8 BOM if present on first line
		if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
			static_cast<unsigned char>(line[1]) == 0xBB &&
			static_cast<unsigned char>(line[2]) == 0xBF) {
			line.erase(0, 3);
		}
		if (line.empty() || line[0] == '#' || line[0] == ';') {
			continue;
		}

		auto pos = line.find_first_of("=\t");
		if (pos == std::string::npos) {
			continue;
		}

		std::string key = trim(line.substr(0, pos));
		std::string value = trim(line.substr(pos + 1));
		if (key.empty()) {
			continue;
		}

		// Handle duplicates with logging
		if (auto it = overrides.find(key); it != overrides.end()) {
			// Duplicate key detected
			int firstLine = keyFirstLineNum[key];
			if (!value.empty()) {
				// New value is non-empty: override
				WARN("translations.txt: Duplicate key '{}' at line {} (first at line {}). Non-empty value wins: '{}'",
					key, lineNum, firstLine, value);
				it->second = value;
			} else {
				// New value is empty: keep existing non-empty value
				WARN("translations.txt: Duplicate key '{}' at line {} (first at line {}). Keeping existing non-empty value.",
					key, lineNum, firstLine);
			}
		} else {
			// First occurrence of this key
			if (!value.empty()) {
				overrides[key] = value;
				keyFirstLineNum[key] = lineNum;
				++entryCount;
			}
		}
	}

	for (auto& [textType, text] : _textData) {
		const char* id = GetKeyForTextType(textType);
		if (!id || *id == '\0') {
			continue;
		}
		if (auto it = overrides.find(id); it != overrides.end()) {
			text = it->second;
		}
	}

	INFO("Loaded {} translation entries from {}", entryCount, path);
}

const char* Texts::GetText(TextType a_textType)
{
	return _textData[a_textType].data();
}
