#include "Config.h"
#include "AutoDrawPatch.h"
#include "UserInput/Controls.h"
#include "imgui.h"
#include "Wheeler/Wheeler.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "bin/Rendering/ResolutionScaleContext.h"
#define STYLEDEFAULTS_PATH "Data\\SKSE\\Plugins\\wheeler\\Styles.defaults.ini"
#define STYLESETTINGS_PATH "Data\\SKSE\\Plugins\\wheeler\\Styles.ini"
#define I4_DEFAULTS_PATH "Data\\SKSE\\Plugins\\wheeler\\I4.defaults.ini"
#define I4_SETTINGS_PATH "Data\\SKSE\\Plugins\\wheeler\\I4.ini"
#define MAINWHEEL_LAYOUT_TEMPLATE_PATH "Data\\SKSE\\Plugins\\wheeler\\MainWheel.Layout.ini"
#define MAINWHEEL_LAYOUT_USER_PATH "Data\\SKSE\\Plugins\\wheeler\\user\\MainWheel.Layout.ini"
#define MAINWHEEL_LAYOUT_USER_DIR "Data\\SKSE\\Plugins\\wheeler\\user"
#define AMMOWHEEL_LAYOUT_LEGACY_PATH "Data\\SKSE\\Plugins\\wheeler\\AmmoWheel.Layout.ini"
#define WHEELBEHAVIOR_FACTORY_PATH "Data\\SKSE\\Plugins\\wheeler\\wheelBehavior.factory.ini"
#define WHEELBEHAVIORSETTINGS_PATH "Data\\SKSE\\Plugins\\wheeler\\wheelBehavior.ini"
#define LEGACY_WHEELBEHAVIORSETTINGS_PATH "Data\\SKSE\\Plugins\\wheeler\\InstantUse.ini"
#define CONTROLDEFAULTS_PATH "Data\\SKSE\\Plugins\\wheeler\\Controls.defaults.ini"
#define CONTROLSETTINGS_PATH "Data\\SKSE\\Plugins\\wheeler\\Controls.ini"
#define ACTIONHOTKEYSBRIDGE_DEFAULTS_PATH "Data\\SKSE\\Plugins\\wheeler\\ActionHotkeysBridge.defaults.ini"
#define ACTIONHOTKEYSBRIDGE_SETTINGS_PATH "Data\\SKSE\\Plugins\\wheeler\\ActionHotkeysBridge.ini"
#define ACTIONHOTKEYSBRIDGE_LAYOUT_PATH "Data\\SKSE\\Plugins\\wheeler\\ActionHotkeysBridge.layout.ini"
#define OSTIMINTEGRATION_DEFAULTS_PATH "Data\\SKSE\\Plugins\\wheeler\\OStimIntegration.defaults.ini"
#define OSTIMINTEGRATION_SETTINGS_PATH "Data\\SKSE\\Plugins\\wheeler\\OStimIntegration.ini"
#define AMMOWHEEL_DEFAULTS_PATH "Data\\SKSE\\Plugins\\wheeler\\AmmoWheel.defaults.ini"
#define AMMOWHEELSETTINGS_PATH "Data\\SKSE\\Plugins\\wheeler\\AmmoWheel.ini"

bool GetBoolValue(const CSimpleIniA& ini, const char* section, const char* key, bool& value);
bool GetUInt32Value(const CSimpleIniA& ini, const char* section, const char* key, uint32_t& value);
bool GetFloatValue(const CSimpleIniA& ini, const char* section, const char* key, float& value);
bool GetStringValue(const CSimpleIniA& ini, const char* section, const char* key, std::string& value);

namespace
{
	constexpr const char* kMainWheelLeftIndicatorAssetPath =
		"Data/SKSE/Plugins/wheeler/resources/icons/left_hand_indicator.svg";
	constexpr const char* kMainWheelRightIndicatorAssetPath =
		"Data/SKSE/Plugins/wheeler/resources/icons/right_hand_indicator.svg";
	constexpr const char* kMainWheelDualIndicatorAssetPath =
		"Data/SKSE/Plugins/wheeler/resources/icons/dual_lr_indicator.svg";
	constexpr const char* kMainWheelLeftSubSlotIndicatorAssetPath =
		"Data/SKSE/Plugins/wheeler/resources/icons/left_hand_indicator_subslot.svg";
	constexpr const char* kMainWheelRightSubSlotIndicatorAssetPath =
		"Data/SKSE/Plugins/wheeler/resources/icons/right_hand_indicator_subslot.svg";
	constexpr const char* kInstantSpellIndicatorBackgroundAssetPath =
		"Data/SKSE/Plugins/wheeler/resources/icons/instant_spell_indicator_bg.svg";
	constexpr const char* kInstantSpellIndicatorOverlayAssetPath =
		"Data/SKSE/Plugins/wheeler/resources/icons/instant_spell_indicator_fg.svg";
	constexpr const char* kInstantSpellIndicatorAtlasAssetPath =
		"Data/SKSE/Plugins/wheeler/resources/icons/instant_spell_indicator_fg.svg";
	constexpr const char* kInstantSpellHandLeftIndicatorAssetPath =
		"Data/SKSE/Plugins/wheeler/resources/icons/instant_spell_hand_left.svg";
	constexpr const char* kInstantSpellHandRightIndicatorAssetPath =
		"Data/SKSE/Plugins/wheeler/resources/icons/instant_spell_hand_right.svg";
	constexpr const char* kInstantSpellHandBothIndicatorAssetPath =
		"Data/SKSE/Plugins/wheeler/resources/icons/instant_spell_hand_both.svg";

	void ApplyMainWheelIndicatorAssetPathHardcoded()
	{
		Config::MainWheel::HandIndicators::Left.AssetPath = kMainWheelLeftIndicatorAssetPath;
		Config::MainWheel::HandIndicators::Left.SecondaryAssetPath = kMainWheelLeftSubSlotIndicatorAssetPath;
		Config::MainWheel::HandIndicators::Right.AssetPath = kMainWheelRightIndicatorAssetPath;
		Config::MainWheel::HandIndicators::Right.SecondaryAssetPath = kMainWheelRightSubSlotIndicatorAssetPath;
		Config::MainWheel::HandIndicators::Dual.AssetPath = kMainWheelDualIndicatorAssetPath;
	}

	void ApplyInstantSpellIndicatorAssetPathHardcoded()
	{
		Config::Styling::HoverDelay::InstantSpellBackgroundAssetPath = kInstantSpellIndicatorBackgroundAssetPath;
		Config::Styling::HoverDelay::InstantSpellOverlayAssetPath = kInstantSpellIndicatorOverlayAssetPath;
		Config::Styling::HoverDelay::InstantSpellAtlasAssetPath = kInstantSpellIndicatorAtlasAssetPath;
		Config::Styling::HoverDelay::InstantSpellHandLeftAssetPath = kInstantSpellHandLeftIndicatorAssetPath;
		Config::Styling::HoverDelay::InstantSpellHandRightAssetPath = kInstantSpellHandRightIndicatorAssetPath;
		Config::Styling::HoverDelay::InstantSpellHandBothAssetPath = kInstantSpellHandBothIndicatorAssetPath;
	}

	void SeedInstantSpellHandIndicatorOffsetsFromShared(
		const float sharedOffsetX,
		const float sharedOffsetY,
		float& leftOffsetX,
		float& leftOffsetY,
		float& rightOffsetX,
		float& rightOffsetY,
		float& bothOffsetX,
		float& bothOffsetY)
	{
		leftOffsetX = sharedOffsetX;
		leftOffsetY = sharedOffsetY;
		rightOffsetX = sharedOffsetX;
		rightOffsetY = sharedOffsetY;
		bothOffsetX = sharedOffsetX;
		bothOffsetY = sharedOffsetY;
	}

	void LoadInstantSpellHandIndicatorOffsetsFromIni(
		const CSimpleIniA& ini,
		const char* section,
		float& sharedOffsetX,
		float& sharedOffsetY,
		float& leftOffsetX,
		float& leftOffsetY,
		float& rightOffsetX,
		float& rightOffsetY,
		float& bothOffsetX,
		float& bothOffsetY)
	{
		GetFloatValue(ini, section, "InstantSpellHandIndicatorOffsetX", sharedOffsetX);
		GetFloatValue(ini, section, "InstantSpellHandIndicatorOffsetY", sharedOffsetY);
		SeedInstantSpellHandIndicatorOffsetsFromShared(
			sharedOffsetX,
			sharedOffsetY,
			leftOffsetX,
			leftOffsetY,
			rightOffsetX,
			rightOffsetY,
			bothOffsetX,
			bothOffsetY);
		GetFloatValue(ini, section, "InstantSpellHandIndicatorLeftOffsetX", leftOffsetX);
		GetFloatValue(ini, section, "InstantSpellHandIndicatorLeftOffsetY", leftOffsetY);
		GetFloatValue(ini, section, "InstantSpellHandIndicatorRightOffsetX", rightOffsetX);
		GetFloatValue(ini, section, "InstantSpellHandIndicatorRightOffsetY", rightOffsetY);
		GetFloatValue(ini, section, "InstantSpellHandIndicatorBothOffsetX", bothOffsetX);
		GetFloatValue(ini, section, "InstantSpellHandIndicatorBothOffsetY", bothOffsetY);
	}

	void LoadInstantSpellHandIndicatorOffsetsWithFallback(
		const CSimpleIniA* loadedIni,
		const CSimpleIniA& fallbackIni,
		const char* section,
		float& sharedOffsetX,
		float& sharedOffsetY,
		float& leftOffsetX,
		float& leftOffsetY,
		float& rightOffsetX,
		float& rightOffsetY,
		float& bothOffsetX,
		float& bothOffsetY)
	{
		auto loadFloat = [&](const char* key, float& value) {
			if (!loadedIni || !GetFloatValue(*loadedIni, section, key, value)) {
				GetFloatValue(fallbackIni, section, key, value);
			}
		};

		loadFloat("InstantSpellHandIndicatorOffsetX", sharedOffsetX);
		loadFloat("InstantSpellHandIndicatorOffsetY", sharedOffsetY);
		SeedInstantSpellHandIndicatorOffsetsFromShared(
			sharedOffsetX,
			sharedOffsetY,
			leftOffsetX,
			leftOffsetY,
			rightOffsetX,
			rightOffsetY,
			bothOffsetX,
			bothOffsetY);
		loadFloat("InstantSpellHandIndicatorLeftOffsetX", leftOffsetX);
		loadFloat("InstantSpellHandIndicatorLeftOffsetY", leftOffsetY);
		loadFloat("InstantSpellHandIndicatorRightOffsetX", rightOffsetX);
		loadFloat("InstantSpellHandIndicatorRightOffsetY", rightOffsetY);
		loadFloat("InstantSpellHandIndicatorBothOffsetX", bothOffsetX);
		loadFloat("InstantSpellHandIndicatorBothOffsetY", bothOffsetY);
	}

	void ClampInstantSpellHandIndicatorOffsets(
		float& sharedOffsetX,
		float& sharedOffsetY,
		float& leftOffsetX,
		float& leftOffsetY,
		float& rightOffsetX,
		float& rightOffsetY,
		float& bothOffsetX,
		float& bothOffsetY)
	{
		sharedOffsetX = std::clamp(sharedOffsetX, -500.0f, 500.0f);
		sharedOffsetY = std::clamp(sharedOffsetY, -500.0f, 500.0f);
		leftOffsetX = std::clamp(leftOffsetX, -500.0f, 500.0f);
		leftOffsetY = std::clamp(leftOffsetY, -500.0f, 500.0f);
		rightOffsetX = std::clamp(rightOffsetX, -500.0f, 500.0f);
		rightOffsetY = std::clamp(rightOffsetY, -500.0f, 500.0f);
		bothOffsetX = std::clamp(bothOffsetX, -500.0f, 500.0f);
		bothOffsetY = std::clamp(bothOffsetY, -500.0f, 500.0f);
	}

	void MergeIniInto(const CSimpleIniA& overlay, CSimpleIniA& target)
	{
		CSimpleIniA::TNamesDepend sections;
		overlay.GetAllSections(sections);
		for (const auto& sectionEntry : sections) {
			const char* section = sectionEntry.pItem;
			if (!section) {
				continue;
			}

			CSimpleIniA::TNamesDepend keys;
			overlay.GetAllKeys(section, keys);
			for (const auto& keyEntry : keys) {
				const char* key = keyEntry.pItem;
				const char* value = overlay.GetValue(section, key, nullptr);
				if (key && value) {
					target.SetValue(section, key, value);
				}
			}
		}
	}

	std::size_t AddMissingIniValues(const CSimpleIniA& defaultsIni, CSimpleIniA& userIni)
	{
		std::size_t added = 0;
		CSimpleIniA::TNamesDepend sections;
		defaultsIni.GetAllSections(sections);
		for (const auto& sectionEntry : sections) {
			const char* section = sectionEntry.pItem;
			if (!section) {
				continue;
			}

			CSimpleIniA::TNamesDepend keys;
			defaultsIni.GetAllKeys(section, keys);
			for (const auto& keyEntry : keys) {
				const char* key = keyEntry.pItem;
				if (!key || userIni.GetValue(section, key, nullptr) != nullptr) {
					continue;
				}

				const char* value = defaultsIni.GetValue(section, key, nullptr);
				if (value) {
					userIni.SetValue(section, key, value);
					++added;
				}
			}
		}
		return added;
	}

	std::string TrimIniToken(std::string token)
	{
		const auto start = token.find_first_not_of(" \t\r\n");
		if (start == std::string::npos) {
			return {};
		}
		const auto end = token.find_last_not_of(" \t\r\n");
		return token.substr(start, end - start + 1);
	}

	std::string NormalizeIniCsvToken(std::string token)
	{
		token = TrimIniToken(std::move(token));
		for (char& ch : token) {
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		}
		return token;
	}

	std::string NormalizeIniModeToken(std::string token)
	{
		token = TrimIniToken(std::move(token));
		std::string normalized;
		normalized.reserve(token.size());
		for (char ch : token) {
			if (ch == '_' || ch == '-' || std::isspace(static_cast<unsigned char>(ch))) {
				continue;
			}
			normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
		}
		return normalized;
	}

	bool TryParseBookReadCompatModeText(const char* raw, std::uint32_t& mode)
	{
		if (!raw || *raw == '\0') {
			return false;
		}

		const std::string normalized = NormalizeIniModeToken(raw);
		if (normalized.empty()) {
			return false;
		}
		if (normalized == "0" || normalized == "auto" || normalized == "autoscripted" ||
			normalized == "scripted" || normalized == "scriptedbacked") {
			mode = static_cast<std::uint32_t>(Config::WheelBehavior::BookReadCompatMode::AutoScripted);
			return true;
		}
		if (normalized == "1" || normalized == "allowlist" || normalized == "allowlistonly" ||
			normalized == "whitelist" || normalized == "whitelistonly") {
			mode = static_cast<std::uint32_t>(Config::WheelBehavior::BookReadCompatMode::AllowListOnly);
			return true;
		}
		if (normalized == "2" || normalized == "disabled" || normalized == "disable" ||
			normalized == "off" || normalized == "false") {
			mode = static_cast<std::uint32_t>(Config::WheelBehavior::BookReadCompatMode::Disabled);
			return true;
		}
		return false;
	}

	std::uint32_t ReadBookReadCompatModeValue(
		const CSimpleIniA& ini,
		const char* section,
		const char* key,
		const std::uint32_t fallback)
	{
		std::uint32_t mode = fallback;
		if (TryParseBookReadCompatModeText(ini.GetValue(section, key, nullptr), mode)) {
			return std::clamp(mode, 0u, 2u);
		}
		if (!GetUInt32Value(ini, section, key, mode)) {
			float modeFloat = static_cast<float>(mode);
			if (GetFloatValue(ini, section, key, modeFloat)) {
				mode = static_cast<std::uint32_t>((std::max)(0.0f, std::round(modeFloat)));
			}
		}
		return std::clamp(mode, 0u, 2u);
	}

	std::vector<std::string> SplitIniCsv(const char* value)
	{
		std::vector<std::string> tokens;
		if (!value || *value == '\0') {
			return tokens;
		}

		std::stringstream stream(value);
		std::string token;
		while (std::getline(stream, token, ',')) {
			token = TrimIniToken(std::move(token));
			if (!token.empty()) {
				tokens.push_back(std::move(token));
			}
		}
		return tokens;
	}

	void AppendFactoryCsvDefaults(CSimpleIniA& target, const CSimpleIniA& factory, const char* section, const char* key)
	{
		const char* factoryValue = factory.GetValue(section, key, nullptr);
		if (!factoryValue || *factoryValue == '\0') {
			return;
		}

		std::vector<std::string> merged = SplitIniCsv(target.GetValue(section, key, ""));
		std::vector<std::string> normalized;
		normalized.reserve(merged.size());
		for (const auto& token : merged) {
			normalized.push_back(NormalizeIniCsvToken(token));
		}

		std::size_t added = 0;
		for (auto token : SplitIniCsv(factoryValue)) {
			const std::string keyToken = NormalizeIniCsvToken(token);
			if (keyToken.empty()) {
				continue;
			}
			if (std::find(normalized.begin(), normalized.end(), keyToken) != normalized.end()) {
				continue;
			}
			normalized.push_back(keyToken);
			merged.push_back(std::move(token));
			++added;
		}

		if (added == 0) {
			return;
		}

		std::string joined;
		for (std::size_t i = 0; i < merged.size(); ++i) {
			if (i != 0) {
				joined += ",";
			}
			joined += merged[i];
		}
		target.SetValue(section, key, joined.c_str());
		logger::info("WheelBehavior: [TransformConfigMerge] appended factory defaults section={} key={} added={}",
			section ? section : "",
			key ? key : "",
			added);
	}

	void AppendTransformFactoryCsvDefaults(CSimpleIniA& target, const CSimpleIniA& factory)
	{
		struct CsvKey
		{
			const char* section;
			const char* key;
		};

		// These transform lists are deliberately additive: factory updates carry safety
		// FormIDs/tokens, while user INIs can still add mod-specific entries.
		constexpr CsvKey keys[] = {
			{ "VampireLordForm", "SpellTokens" },
			{ "VampireLordForm", "ExitSpellTokens" },
			{ "VampireLordForm", "AdditionalSpellFormIDs" },
			{ "VampireLordForm", "HiddenSpellFormIDs" },
			{ "VampireLordForm", "HiddenSpellTokens" },
			{ "LichForm", "SpellTokens" },
			{ "LichForm", "ExitSpellTokens" },
			{ "LichForm", "AdditionalSpellFormIDs" }
		};

		for (const auto& entry : keys) {
			AppendFactoryCsvDefaults(target, factory, entry.section, entry.key);
		}
	}

	bool LoadLayeredIni(
		const char* defaultsPath,
		const char* userPath,
		CSimpleIniA& outIni,
		bool* defaultsLoaded = nullptr,
		bool* userLoaded = nullptr)
	{
		outIni.Reset();
		outIni.SetUnicode();

		bool loadedAny = false;
		bool loadedDefaults = false;
		bool loadedUser = false;

		if (defaultsPath && outIni.LoadFile(defaultsPath) >= 0) {
			loadedAny = true;
			loadedDefaults = true;
		} else {
			outIni.Reset();
			outIni.SetUnicode();
		}

		if (userPath) {
			CSimpleIniA userIni;
			userIni.SetUnicode();
			if (userIni.LoadFile(userPath) >= 0) {
				MergeIniInto(userIni, outIni);
				loadedAny = true;
				loadedUser = true;
			}
		}

		if (defaultsLoaded) {
			*defaultsLoaded = loadedDefaults;
		}
		if (userLoaded) {
			*userLoaded = loadedUser;
		}

		return loadedAny;
	}

	bool EnsureUserIniBootstrapped(
		const char* defaultsPath,
		const char* userPath,
		const char* logLabel)
	{
		if (!defaultsPath || !userPath) {
			return false;
		}

		bool userConfigExists = false;
		{
			std::ifstream existingUserFile(userPath, std::ios::binary);
			userConfigExists = existingUserFile.good();
		}

		CSimpleIniA defaultsIni;
		defaultsIni.SetUnicode();
		const SI_Error loadDefaultsRc = defaultsIni.LoadFile(defaultsPath);
		if (loadDefaultsRc < 0) {
			logger::warn(
				"{}: failed to read defaults '{}' for bootstrap (rc={})",
				logLabel,
				defaultsPath,
				static_cast<int>(loadDefaultsRc));
			return false;
		}

		if (!userConfigExists) {
			const SI_Error saveUserRc = defaultsIni.SaveFile(userPath);
			if (saveUserRc >= 0) {
				logger::info("{}: created user config '{}' from '{}'", logLabel, userPath, defaultsPath);
				return true;
			}

			logger::warn(
				"{}: failed to bootstrap '{}' from '{}' (rc={})",
				logLabel,
				userPath,
				defaultsPath,
				static_cast<int>(saveUserRc));
			return false;
		}

		CSimpleIniA userIni;
		userIni.SetUnicode();
		const SI_Error loadUserRc = userIni.LoadFile(userPath);
		if (loadUserRc < 0) {
			logger::warn(
				"{}: existing user config '{}' could not be read for default-key backfill (rc={})",
				logLabel,
				userPath,
				static_cast<int>(loadUserRc));
			return false;
		}

		const std::size_t missingKeysAdded = AddMissingIniValues(defaultsIni, userIni);
		if (missingKeysAdded == 0) {
			return false;
		}

		const SI_Error saveUserRc = userIni.SaveFile(userPath);
		if (saveUserRc >= 0) {
			logger::info(
				"{}: added {} missing default config keys to '{}' from '{}'",
				logLabel,
				missingKeysAdded,
				userPath,
				defaultsPath);
			return true;
		}

		logger::warn(
			"{}: failed to save default-key backfill for '{}' from '{}' (rc={})",
			logLabel,
			userPath,
			defaultsPath,
			static_cast<int>(saveUserRc));
		return false;
	}

	struct I4ConfigValues
	{
		bool Enabled = false;
		bool PreferI4Icons = true;
		bool UseAlternativePath = false;
		std::uint32_t CacheMaxEntries = 256;
		bool DebugLog = false;
		bool TraceLog = false;
		bool TraceCacheHits = false;
		std::uint32_t RenderSizePolicy = 0;
		std::uint32_t FixedRenderSize = 128;
		bool ExtractionMode = false;
		bool UseForWeapons = true;
		bool UseForArmor = true;
		bool UseForAmmo = true;
		bool UseForPotions = true;
		bool UseForFood = true;
		bool UseForIngredients = true;
		bool UseForPoisons = true;
		bool UseForBooks = true;
		bool UseForScrolls = true;
		bool UseForLights = true;
		bool UseForMisc = true;
		bool UseForSpells = true;
		bool UseForShouts = true;
		bool UseForPowers = true;
		bool ExtractForWeapons = true;
		bool ExtractForArmor = true;
		bool ExtractForAmmo = true;
		bool ExtractForPotions = true;
		bool ExtractForFood = true;
		bool ExtractForIngredients = true;
		bool ExtractForPoisons = true;
		bool ExtractForBooks = true;
		bool ExtractForScrolls = true;
		bool ExtractForLights = true;
		bool ExtractForMisc = true;
		bool ExtractForSpells = true;
		bool ExtractForShouts = true;
		bool ExtractForPowers = true;

		bool operator==(const I4ConfigValues&) const = default;
	};

	void LoadI4ConfigValues(const CSimpleIniA& ini, I4ConfigValues& values)
	{
		GetBoolValue(ini, "I4", "Enabled", values.Enabled);
		GetBoolValue(ini, "I4", "PreferI4Icons", values.PreferI4Icons);
		GetBoolValue(ini, "I4", "UseAlternativePath", values.UseAlternativePath);
		GetUInt32Value(ini, "I4", "CacheMaxEntries", values.CacheMaxEntries);
		GetBoolValue(ini, "I4", "DebugLog", values.DebugLog);
		GetBoolValue(ini, "I4", "TraceLog", values.TraceLog);
		GetBoolValue(ini, "I4", "TraceCacheHits", values.TraceCacheHits);
		GetUInt32Value(ini, "I4", "RenderSizePolicy", values.RenderSizePolicy);
		GetUInt32Value(ini, "I4", "FixedRenderSize", values.FixedRenderSize);
		GetBoolValue(ini, "I4", "ExtractionMode", values.ExtractionMode);
		GetBoolValue(ini, "I4", "UseForWeapons", values.UseForWeapons);
		GetBoolValue(ini, "I4", "UseForArmor", values.UseForArmor);
		GetBoolValue(ini, "I4", "UseForAmmo", values.UseForAmmo);
		GetBoolValue(ini, "I4", "UseForPotions", values.UseForPotions);
		GetBoolValue(ini, "I4", "UseForFood", values.UseForFood);
		if (!GetBoolValue(ini, "I4", "UseForIngredients", values.UseForIngredients)) {
			values.UseForIngredients = values.UseForFood;
		}
		GetBoolValue(ini, "I4", "UseForPoisons", values.UseForPoisons);
		GetBoolValue(ini, "I4", "UseForBooks", values.UseForBooks);
		GetBoolValue(ini, "I4", "UseForScrolls", values.UseForScrolls);
		GetBoolValue(ini, "I4", "UseForLights", values.UseForLights);
		GetBoolValue(ini, "I4", "UseForMisc", values.UseForMisc);
		GetBoolValue(ini, "I4", "UseForSpells", values.UseForSpells);
		GetBoolValue(ini, "I4", "UseForShouts", values.UseForShouts);
		GetBoolValue(ini, "I4", "UseForPowers", values.UseForPowers);
		GetBoolValue(ini, "I4", "ExtractForWeapons", values.ExtractForWeapons);
		GetBoolValue(ini, "I4", "ExtractForArmor", values.ExtractForArmor);
		GetBoolValue(ini, "I4", "ExtractForAmmo", values.ExtractForAmmo);
		GetBoolValue(ini, "I4", "ExtractForPotions", values.ExtractForPotions);
		GetBoolValue(ini, "I4", "ExtractForFood", values.ExtractForFood);
		if (!GetBoolValue(ini, "I4", "ExtractForIngredients", values.ExtractForIngredients)) {
			values.ExtractForIngredients = values.ExtractForFood;
		}
		GetBoolValue(ini, "I4", "ExtractForPoisons", values.ExtractForPoisons);
		GetBoolValue(ini, "I4", "ExtractForBooks", values.ExtractForBooks);
		GetBoolValue(ini, "I4", "ExtractForScrolls", values.ExtractForScrolls);
		GetBoolValue(ini, "I4", "ExtractForLights", values.ExtractForLights);
		GetBoolValue(ini, "I4", "ExtractForMisc", values.ExtractForMisc);
		GetBoolValue(ini, "I4", "ExtractForSpells", values.ExtractForSpells);
		GetBoolValue(ini, "I4", "ExtractForShouts", values.ExtractForShouts);
		GetBoolValue(ini, "I4", "ExtractForPowers", values.ExtractForPowers);
	}

	void ClampI4ConfigValues(I4ConfigValues& values)
	{
		values.CacheMaxEntries = std::clamp(values.CacheMaxEntries, 16u, 2048u);
		values.RenderSizePolicy = std::clamp(values.RenderSizePolicy, 0u, 1u);
		values.FixedRenderSize = std::clamp(values.FixedRenderSize, 16u, 1024u);
	}

	void ApplyI4ConfigValues(const I4ConfigValues& values)
	{
		Config::I4::Enabled = values.Enabled;
		Config::I4::PreferI4Icons = values.PreferI4Icons;
		Config::I4::UseAlternativePath = values.UseAlternativePath;
		Config::I4::CacheMaxEntries = values.CacheMaxEntries;
		Config::I4::DebugLog = values.DebugLog;
		Config::I4::TraceLog = values.TraceLog;
		Config::I4::TraceCacheHits = values.TraceCacheHits;
		Config::I4::RenderSizePolicy = values.RenderSizePolicy;
		Config::I4::FixedRenderSize = values.FixedRenderSize;
		Config::I4::ExtractionMode = values.ExtractionMode;
		Config::I4::UseForWeapons = values.UseForWeapons;
		Config::I4::UseForArmor = values.UseForArmor;
		Config::I4::UseForAmmo = values.UseForAmmo;
		Config::I4::UseForPotions = values.UseForPotions;
		Config::I4::UseForFood = values.UseForFood;
		Config::I4::UseForIngredients = values.UseForIngredients;
		Config::I4::UseForPoisons = values.UseForPoisons;
		Config::I4::UseForBooks = values.UseForBooks;
		Config::I4::UseForScrolls = values.UseForScrolls;
		Config::I4::UseForLights = values.UseForLights;
		Config::I4::UseForMisc = values.UseForMisc;
		Config::I4::UseForSpells = values.UseForSpells;
		Config::I4::UseForShouts = values.UseForShouts;
		Config::I4::UseForPowers = values.UseForPowers;
		Config::I4::ExtractForWeapons = values.ExtractForWeapons;
		Config::I4::ExtractForArmor = values.ExtractForArmor;
		Config::I4::ExtractForAmmo = values.ExtractForAmmo;
		Config::I4::ExtractForPotions = values.ExtractForPotions;
		Config::I4::ExtractForFood = values.ExtractForFood;
		Config::I4::ExtractForIngredients = values.ExtractForIngredients;
		Config::I4::ExtractForPoisons = values.ExtractForPoisons;
		Config::I4::ExtractForBooks = values.ExtractForBooks;
		Config::I4::ExtractForScrolls = values.ExtractForScrolls;
		Config::I4::ExtractForLights = values.ExtractForLights;
		Config::I4::ExtractForMisc = values.ExtractForMisc;
		Config::I4::ExtractForSpells = values.ExtractForSpells;
		Config::I4::ExtractForShouts = values.ExtractForShouts;
		Config::I4::ExtractForPowers = values.ExtractForPowers;
	}

	I4ConfigValues LoadI4DefaultsSnapshot()
	{
		I4ConfigValues values;

		CSimpleIniA defaultsIni;
		defaultsIni.SetUnicode();
		if (defaultsIni.LoadFile(I4_DEFAULTS_PATH) >= 0) {
			LoadI4ConfigValues(defaultsIni, values);
			ClampI4ConfigValues(values);
		}

		return values;
	}

	void ResetActionHotkeysBridgeConfigToDefaults()
	{
		Config::ActionHotkeysBridge::Enabled = false;
		Config::ActionHotkeysBridge::SourceIniPath = R"(Data\SKSE\Plugins\ActionHotkeys.ini)";
		Config::ActionHotkeysBridge::SourceSlotsIniPath = R"(Data\SKSE\Plugins\ActionSlots.ini)";
		Config::ActionHotkeysBridge::SourceIconsPath.clear();
		Config::ActionHotkeysBridge::AutoInjection = true;
		Config::ActionHotkeysBridge::ManualWheelCount = 2;
		Config::ActionHotkeysBridge::AutoRefresh = true;
		Config::ActionHotkeysBridge::RefreshDebounceMs = 500;
		Config::ActionHotkeysBridge::DispatchCooldownMs = 150;
		Config::ActionHotkeysBridge::CloseAssistEnabled = true;
		Config::ActionHotkeysBridge::CloseAssistUseEsc = true;
		Config::ActionHotkeysBridge::CloseAssistUseGamepadB = true;
		Config::ActionHotkeysBridge::CloseAssistTimeoutMs = 2000;
		Config::ActionHotkeysBridge::BlockConflictingWheelerHotkeys = true;
		Config::ActionHotkeysBridge::MirrorSecondaryActivate = true;
		Config::ActionHotkeysBridge::MirrorSpecialActivate = false;
		Config::ActionHotkeysBridge::DebugLog = false;
		for (auto& wheel : Config::ActionHotkeysBridge::Wheels) {
			wheel = {};
			wheel.EntryCapacity = 10;
		}
		Config::ActionHotkeysBridge::ResetLayout = 0;
		Config::ActionHotkeysBridge::ResetLayoutModifier = 0;
		Config::ActionHotkeysBridge::ReturnToPrevious = 0;
		Config::ActionHotkeysBridge::ReturnToPreviousModifier = 0;
		Config::ActionHotkeysBridge::RefreshMirror = 0;
		Config::ActionHotkeysBridge::RefreshMirrorModifier = 0;
	}

	void ResetOStimIntegrationConfigToDefaults()
	{
		Config::OStimIntegration::Enabled = false;
		Config::OStimIntegration::AutoDetect = true;
		Config::OStimIntegration::CreateManagedWheel = true;
		Config::OStimIntegration::AutoSwitchToSceneWheel = false;
		Config::OStimIntegration::RestorePreviousWheelOnSceneEnd = true;
		Config::OStimIntegration::AllowPositionBrowsing = true;
		Config::OStimIntegration::ShowOnlyValidPositions = true;
		Config::OStimIntegration::ShowPositionNames = true;
		Config::OStimIntegration::ShowPositionPreviews = true;
		Config::OStimIntegration::RestrictRegularWheelActionsDuringScenes = false;
		Config::OStimIntegration::HideInvalidActions = true;
		Config::OStimIntegration::PreferMetadataPreviews = true;
		Config::OStimIntegration::UseResourcePreviewFallback = true;
		Config::OStimIntegration::PreferCurrentAnimationClass = true;
		Config::OStimIntegration::DebugLog = false;
		Config::OStimIntegration::MaxPositionsPerPage = 6;
		Config::OStimIntegration::SVGSlotScale = 1.0f;
		Config::OStimIntegration::SVGSlotOffsetX = 0.0f;
		Config::OStimIntegration::SVGSlotOffsetY = 0.0f;
		Config::OStimIntegration::SVGCenterScale = 1.0f;
		Config::OStimIntegration::SVGCenterOffsetX = 0.0f;
		Config::OStimIntegration::SVGCenterOffsetY = 0.0f;
		Config::OStimIntegration::DDSSlotScale = 1.0f;
		Config::OStimIntegration::DDSSlotOffsetX = 0.0f;
		Config::OStimIntegration::DDSSlotOffsetY = 0.0f;
		Config::OStimIntegration::DDSCenterScale = 1.0f;
		Config::OStimIntegration::DDSCenterOffsetX = 0.0f;
		Config::OStimIntegration::DDSCenterOffsetY = 0.0f;
	}

	std::string BuildActionHotkeysBridgeWheelSection(std::size_t wheelIndex)
	{
		return fmt::format("ActionHotkeysBridge.Wheel{}", wheelIndex + 1);
	}

	std::string BuildActionHotkeysBridgeLayoutSection(std::string_view slotId)
	{
		return fmt::format("ActionHotkeysBridge.Layout.{}", slotId);
	}

	bool WriteActionHotkeysBridgeUserConfig()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		const SI_Error rc = ini.LoadFile(ACTIONHOTKEYSBRIDGE_SETTINGS_PATH);
		if (rc < 0) {
			logger::warn(
				"ActionHotkeysBridge: Failed to load '{}', writing new file with current overrides",
				ACTIONHOTKEYSBRIDGE_SETTINGS_PATH);
		}

		ini.SetBoolValue("ActionHotkeysBridge", "Enabled", Config::ActionHotkeysBridge::Enabled);
		ini.SetValue("ActionHotkeysBridge", "SourceIniPath", Config::ActionHotkeysBridge::SourceIniPath.c_str());
		ini.SetValue("ActionHotkeysBridge", "SourceSlotsIniPath", Config::ActionHotkeysBridge::SourceSlotsIniPath.c_str());
		ini.SetValue("ActionHotkeysBridge", "SourceIconsPath", Config::ActionHotkeysBridge::SourceIconsPath.c_str());
		ini.SetBoolValue("ActionHotkeysBridge", "AutoInjection", Config::ActionHotkeysBridge::AutoInjection);
		ini.SetLongValue("ActionHotkeysBridge", "ManualWheelCount", static_cast<long>(Config::ActionHotkeysBridge::ManualWheelCount));
		ini.SetBoolValue("ActionHotkeysBridge", "AutoRefresh", Config::ActionHotkeysBridge::AutoRefresh);
		ini.SetLongValue("ActionHotkeysBridge", "RefreshDebounceMs", static_cast<long>(Config::ActionHotkeysBridge::RefreshDebounceMs));
		ini.SetLongValue("ActionHotkeysBridge", "DispatchCooldownMs", static_cast<long>(Config::ActionHotkeysBridge::DispatchCooldownMs));
		ini.SetBoolValue("ActionHotkeysBridge", "CloseAssistEnabled", Config::ActionHotkeysBridge::CloseAssistEnabled);
		ini.SetBoolValue("ActionHotkeysBridge", "CloseAssistUseEsc", Config::ActionHotkeysBridge::CloseAssistUseEsc);
		ini.SetBoolValue("ActionHotkeysBridge", "CloseAssistUseGamepadB", Config::ActionHotkeysBridge::CloseAssistUseGamepadB);
		ini.SetLongValue("ActionHotkeysBridge", "CloseAssistTimeoutMs", static_cast<long>(Config::ActionHotkeysBridge::CloseAssistTimeoutMs));
		ini.SetBoolValue(
			"ActionHotkeysBridge",
			"BlockConflictingWheelerHotkeys",
			Config::ActionHotkeysBridge::BlockConflictingWheelerHotkeys);
		ini.SetBoolValue(
			"ActionHotkeysBridge",
			"MirrorSecondaryActivate",
			Config::ActionHotkeysBridge::MirrorSecondaryActivate);
		ini.SetBoolValue(
			"ActionHotkeysBridge",
			"MirrorSpecialActivate",
			Config::ActionHotkeysBridge::MirrorSpecialActivate);
		ini.SetBoolValue("ActionHotkeysBridge", "DebugLog", Config::ActionHotkeysBridge::DebugLog);

		ini.SetLongValue(
			"ActionHotkeysBridge.Bindings",
			"ResetLayout",
			static_cast<long>(Config::ActionHotkeysBridge::ResetLayout));
		ini.SetLongValue(
			"ActionHotkeysBridge.Bindings",
			"ResetLayoutModifier",
			static_cast<long>(Config::ActionHotkeysBridge::ResetLayoutModifier));
		ini.SetLongValue(
			"ActionHotkeysBridge.Bindings",
			"RefreshMirror",
			static_cast<long>(Config::ActionHotkeysBridge::RefreshMirror));
		ini.SetLongValue(
			"ActionHotkeysBridge.Bindings",
			"RefreshMirrorModifier",
			static_cast<long>(Config::ActionHotkeysBridge::RefreshMirrorModifier));

		for (std::size_t i = 0; i < Config::ActionHotkeysBridge::Wheels.size(); ++i) {
			const auto& wheel = Config::ActionHotkeysBridge::Wheels[i];
			const std::string section = BuildActionHotkeysBridgeWheelSection(i);
			ini.SetLongValue(section.c_str(), "EntryCapacity", static_cast<long>(wheel.EntryCapacity));
			ini.SetLongValue(section.c_str(), "JumpKey", static_cast<long>(wheel.JumpKey));
			ini.SetLongValue(section.c_str(), "JumpKeyModifier", static_cast<long>(wheel.JumpKeyModifier));
		}

		return ini.SaveFile(ACTIONHOTKEYSBRIDGE_SETTINGS_PATH) >= 0;
	}

	bool WriteActionHotkeysBridgeLayoutConfig()
	{
		CSimpleIniA ini;
		ini.SetUnicode();

		for (const auto& [slotId, placement] : Config::ActionHotkeysBridge::PersistedLayout) {
			const std::string section = BuildActionHotkeysBridgeLayoutSection(slotId);
			ini.SetLongValue(section.c_str(), "Wheel", static_cast<long>(placement.wheelNumber));
			ini.SetLongValue(section.c_str(), "Entry", static_cast<long>(placement.entryIndex));
		}

		return ini.SaveFile(ACTIONHOTKEYSBRIDGE_LAYOUT_PATH) >= 0;
	}

	struct WheelBehaviorSnapshot
	{
		// [WheelBehavior] (and legacy [InstantUse])
		bool releaseToUse{ false };
		bool closeWheelAfterUse{ false };
		bool rtuAlchemy{ true };
		bool rtuSpell{ true };
		bool rtuShout{ true };
		bool rtuSmartAssignEnabled{ false };
		std::uint32_t rtuSmartAssignTargetHands{ 2 };
		std::uint32_t rtuSmartAssignCategoryMask{ 0 };
		std::uint32_t rtuSmartAssignBothEmptyPriority{ 2 };
		std::uint32_t rtuSmartAssignOverwriteMode{ 2 };
		bool rtuSmartAssignDebugLog{ false };
		float hoverActivateDelaySeconds{ 0.5f };
		bool autoDrawOnUse{ false };
		bool instantSpell{ false };
		bool instantSpellUseDirectCast{ false };
		bool rtuAutoInstantSpell{ true };
		bool instantPowers{ false };
		std::uint32_t instantSpellConcentrationMode{ 0 };
		float instantSpellConcentrationMaxSeconds{ 3.0f };
		bool instantSpellDebugLog{ false };
		bool shoutPipelineDebug{ false };
		bool instantShout{ false };
		bool rtuAutoInstantShout{ true };
		float shoutWord2Threshold{ 0.35f };
		float shoutWord3Threshold{ 0.75f };
		float shoutHoldSecsWord1{ 0.1f };
		float shoutHoldSecsWord2{ 0.5f };
		float shoutHoldSecsWord3{ 1.0f };
		bool shoutIgnoreRTUDelay{ true };
		float shoutStageFillSecs{ 0.08f };
		float shoutStageHoldSecs1{ 0.40f };
		float shoutStageHoldSecs2{ 0.40f };
		bool clearDepletedConsumables{ true };
		std::uint32_t scriptedMiscDispatchMode{ 0 };
		std::uint32_t bookReadCompatMode{ static_cast<std::uint32_t>(Config::WheelBehavior::BookReadCompatMode::AutoScripted) };
		std::string bookReadCompatOnReadFormIDs{};
		std::string bookReadCompatOnReadPlugins{ "TheArcaneTome.esp" };
		std::string bookReadCompatOnReadNameTokens{};
		bool bookReadCompatDebugLog{ false };
		bool keepMissingEnabled{ false };
		bool keepMissingConsumables{ true };
		bool keepMissingGears{ true };
		bool keepMissingThrowableMods{ true };
		bool handMemoryEnabled{ true };
		bool handMemoryDebugLog{ false };
		float handMemoryRestoreDelaySeconds{ 0.05f };
		float handMemoryRestoreWindowSeconds{ 1.25f };
		bool handMemoryRestoreLeftIfEmpty{ true };
		bool handMemoryRestoreRightIfEmpty{ true };
		bool rtuAntiSlipEnabled{ true };
		float rtuAntiSlipStrength{ 0.5f };
		bool lootMenuOverride{ true };
		bool heavyListCompatibilityMode{ false };
		float heavyListCompatibilitySettleMs{ 420.0f };
		bool mutableInventoryHooks{ false };
		bool transformWheelsEnabled{ true };
		std::uint32_t transformWheelsMode{ 1 };
		bool transformWheelsRestorePrevious{ true };
		bool transformWheelsIncludeShouts{ false };
		std::uint32_t transformWheelsRegenerateThrottleMs{ 250 };
		std::uint32_t transformWheelsRetryWindowMs{ 8000 };
		std::uint32_t transformWheelsStableTicks{ 2 };
		std::uint32_t transformWheelsMinEntries{ 1 };
		bool transformWheelsCacheHumanSnapshot{ true };
		bool transformWheelsPersistGeneratedWheels{ false };
		bool transformWheelsUpdateOnlyOnChange{ true };
		bool transformWheelsDebugLog{ false };
		bool transformWheelsGenericEnabled{ false };
		std::string transformWheelsGenericStateID{ "GenericTransform" };
		std::string transformWheelsGenericWheelID{ "Wheel_GenericTransform" };
		std::string transformWheelsGenericRaceEditorIDContains{};
		std::string transformWheelsGenericRaceKeywords{};
		std::string transformWheelsGenericRaceFormIDs{};
		std::string transformWheelsPrecedenceOrder{ "Werewolf,VampireLord,Lich,GenericOthers" };
		bool werewolfAllowBaseWheel{ false };
		bool vampireLordAllowBaseWheel{ false };
		bool werewolfAllowBaseWheelSpells{ false };
		bool werewolfAllowBaseWheelShouts{ false };
		bool werewolfFormEnabled{ true };
		std::string werewolfFormPopulateMode{ "Delta" };
		std::string werewolfFormRaceEditorIDContains{ "Werewolf" };
		std::string werewolfFormRaceKeywords{ "ActorTypeWerewolf" };
		std::string werewolfFormRaceFormIDs{ "Skyrim.esm|0x000CDD84" };
		std::string werewolfFormSpellTokens{ "Werewolf,Howl,Growl,Lycan,Manbeast,Night Eye,Totem,Predator,Savage,Terror,Hunt,Brotherhood,Call of the Wild,Moonlight,Feed" };
		std::string werewolfFormExitSpellTokens{ "Revert,HumanForm,Human Form,Return to Human,Mortal Form" };
		std::string werewolfFormAdditionalSpellFormIDs{};
		bool werewolfFormDebugLog{ false };
		bool vampireLordFormEnabled{ true };
		std::string vampireLordFormPopulateMode{ "Delta" };
		bool vampireLordFormHideTransformSpell{ true };
		bool vampireLordFormHideForcedRightHandSpells{ true };
		bool vampireLordFormBlockHiddenSpellActivation{ true };
		bool vampireLordFormBlockRegularSpellsInMeleeMode{ true };
		std::string vampireLordFormRaceEditorIDContains{ "DLC1VampireBeastRace,VampireLord" };
		std::string vampireLordFormRaceKeywords{};
		std::string vampireLordFormRaceFormIDs{ "Dawnguard.esm|0x0000283A" };
		std::string vampireLordFormSpellTokens{ "VampireLord,DLC1Vampire,Vampiric,Vampire Lord,Vampiric Grip,Conjure Gargoyle,Gargoyle,Conjure Death Hound,Death Hound,Mist Form,Bats,Hunter's Sight,Hunters Sight,Vampire's Sight,Vampires Sight,Vampire Sight,Blood Storm,Raze,Raise Dead,Raised Dead,Choke Hold,Chokehold" };
		std::string vampireLordFormExitSpellTokens{ "Revert,Revert Form,Change Form" };
		std::string vampireLordFormAdditionalSpellFormIDs{ "Skyrim.esm|0x000C4DE1" };
		std::string vampireLordFormHiddenSpellFormIDs{ "Dawnguard.esm|0x0000BFED,Dawnguard.esm|0x00013EC9" };
		std::string vampireLordFormHiddenSpellTokens{ "Vampiric Drain" };
		bool vampireLordFormDebugLog{ false };
		bool lichFormEnabled{ true };
		std::string lichFormMode{ "Overlay" };
		std::string lichFormPopulateMode{ "ModList" };
		bool lichFormAllowBaseWheel{ true };
		std::string lichFormTransformGuard{ "BlockOtherTransforms" };
		bool lichFormBlockBoundSpells{ true };
		bool lichFormHideWeapons{ false };
		bool lichFormHideGear{ true };
		bool lichFormBlockStaffSwapping{ true };
		bool lichFormSuppressDirectCast{ true };
		std::string lichFormRaceEditorIDContains{ "Lich,Necro,UCL" };
		std::string lichFormRaceKeywords{};
		std::string lichFormRaceFormIDs{};
		std::string lichFormSpellTokens{ "Death Grip,Ice Coffin,Dark Conduit,Revert,Revert Form,Return to Human,Human Form,Mortal Form,Return to Mortal" };
		std::string lichFormExitSpellTokens{ "Revert,NecroRevert,Revert Form,Return to Human,Human Form,Mortal Form,Return to Mortal" };
		std::string lichFormAdditionalSpellFormIDs{};
		bool lichFormDebugLog{ false };
		bool actionHotkeysBridgeEnabled{ true };
		std::string actionHotkeysBridgeSourceIniPath{ R"(Data\SKSE\Plugins\ActionHotkeys.ini)" };
		std::string actionHotkeysBridgeSourceIconsPath{};
		std::uint32_t actionHotkeysBridgeMirroredWheelCount{ 2 };
		std::uint32_t actionHotkeysBridgeMaxMirrorSlots{ 20 };
		bool actionHotkeysBridgeAutoRefresh{ true };
		std::uint32_t actionHotkeysBridgeRefreshDebounceMs{ 500 };
		std::uint32_t actionHotkeysBridgeDispatchCooldownMs{ 150 };
		bool actionHotkeysBridgeBlockConflictingWheelerHotkeys{ true };
		bool actionHotkeysBridgeMirrorSecondaryActivate{ true };
		bool actionHotkeysBridgeMirrorSpecialActivate{ false };
		bool actionHotkeysBridgeDebugLog{ true };

		// [Cooldowns]
		bool cooldownsEnabled{ false };
		bool cooldownsShowTimer{ false };
		float cooldownsContentDimAlpha{ 0.47f };
		bool cooldownsSelectedIndicatorEnabled{ true };
		ImU32 cooldownsOverlayColor{ IM_COL32(255, 0, 0, 120) };
		ImU32 cooldownsSelectedIndicatorTintColor{ IM_COL32(255, 0, 0, 120) };
		float cooldownsCacheWindowSeconds{ 0.1f };

		// [Cooldowns.TimerText]
		std::uint32_t cooldownsTimerTextFontIndex{ 0 };
		float cooldownsTimerTextSize{ 26.0f };
		ImU32 cooldownsTimerTextColor{ IM_COL32(255, 255, 255, 140) };

		// [Styling.HoverDelay]
		bool hoverDelayEnabled{ true };
		float hoverDelayRadius{ 45.0f };
		float hoverDelayRadiusOffset{ 6.0f };
		float hoverDelayThickness{ 4.0f };
		ImU32 hoverDelayColor{ 4291543295u };
		ImU32 hoverDelayBackgroundColor{ 1006632959u };
		ImU32 hoverDelayInstantSpellColor{ 4291543295u };
		ImU32 hoverDelayInstantSpellBackgroundColor{ 1006632959u };
		bool hoverDelayInstantSpellUseReskinAssets{ false };
		float hoverDelayInstantSpellAssetScale{ 1.0f };
		float hoverDelayInstantSpellAssetOffsetX{ 0.0f };
		float hoverDelayInstantSpellAssetOffsetY{ 0.0f };
		float hoverDelayInstantSpellAssetOpacity{ 1.0f };
		float hoverDelayInstantSpellHandIndicatorScale{ 1.0f };
		float hoverDelayInstantSpellHandIndicatorOffsetX{ 0.0f };
		float hoverDelayInstantSpellHandIndicatorOffsetY{ -18.0f };
		float hoverDelayInstantSpellHandIndicatorLeftOffsetX{ 0.0f };
		float hoverDelayInstantSpellHandIndicatorLeftOffsetY{ -18.0f };
		float hoverDelayInstantSpellHandIndicatorRightOffsetX{ 0.0f };
		float hoverDelayInstantSpellHandIndicatorRightOffsetY{ -18.0f };
		float hoverDelayInstantSpellHandIndicatorBothOffsetX{ 0.0f };
		float hoverDelayInstantSpellHandIndicatorBothOffsetY{ -18.0f };
		float hoverDelayInstantSpellHandIndicatorOpacity{ 1.0f };
		bool hoverDelayInstantSpellUseAtlasAnimation{ false };
		std::uint32_t hoverDelayInstantSpellAtlasCols{ 8 };
		std::uint32_t hoverDelayInstantSpellAtlasRows{ 8 };
		std::uint32_t hoverDelayInstantSpellAtlasFrameCount{ 64 };

		// [Sounds]
		bool soundsEnabled{ true };
		std::string soundsHoverEditorID{ "UIFavorite" };
		std::string soundsActivateEditorID{ "UIMenuOK" };
		float soundsHoverVolume{ 1.0f };
		float soundsActivateVolume{ 1.0f };
		
		// Shout stage sounds
		bool enableShoutStageSounds{ true };
		std::uint32_t shoutStageSoundMode{ 1 };  // 0=Off, 1=UI, 2=VOC
		std::string shoutUISoundEditorID{ "UIMenuOK" };
		float shoutUIStageVolume1{ 0.35f };
		float shoutUIStageVolume2{ 0.65f };
		float shoutUIStageVolume3{ 1.00f };
		float shoutUIStagePitch1{ 1.00f };
		float shoutUIStagePitch2{ 1.03f };
		float shoutUIStagePitch3{ 1.06f };
		std::string shoutWord1SoundEditorID{ "" };
		std::string shoutWord2SoundEditorID{ "" };
		std::string shoutWord3SoundEditorID{ "" };

		// [MainWheel.Debug]
		bool mainWheelDebugEnabled{ false };
		bool mainWheelDebugOverlayEnabled{ false };
		bool mainWheelDebugVerbose{ false };
		std::uint32_t mainWheelDebugRateLimitMs{ 250 };
		bool mainWheelDebugLogOpenClose{ true };
		bool mainWheelDebugLogConfig{ true };
		bool mainWheelDebugLogInput{ false };
		bool mainWheelDebugLogScaling{ true };
		bool mainWheelDebugLogClamp{ true };
		bool mainWheelDebugLogIndicators{ true };
		bool mainWheelDebugLogReskinResolve{ false };
		bool mainWheelDebugLogAssets{ false };
		bool mainWheelDebugLogPerf{ false };

		// [MainWheel.Mouse]
		float mainWheelCenterLockRadiusPx{ 45.0f };
		float mainWheelJumpGuardRadiusPx{ 140.0f };
		float mainWheelHysteresisDegrees{ 8.0f };
		bool mainWheelCenterSlowdownEnabled{ true };
		float mainWheelGainCenter{ 0.25f };
		float mainWheelGainOuter{ 1.0f };
		float mainWheelCurvePower{ 2.0f };
		float mainWheelInnerDeadZoneR{ 0.18f };
		float mainWheelInnerBlendZoneR{ 0.35f };
		float mainWheelMinStableSpeed{ 0.05f };
		float mainWheelVelocityHalfLifeMs{ 40.0f };
		float mainWheelStableDirHalfLifeMs{ 60.0f };
		float mainWheelIntentMinSpeed{ 0.60f };
		float mainWheelIntentSustainSpeed{ 0.20f };
		float mainWheelIntentConfirmMs{ 80.0f };
		float mainWheelIntentConeDeg{ 25.0f };
		float mainWheelIntentReleaseConeDeg{ 45.0f };
		float mainWheelIntentMinAngleDeg{ 90.0f };
		float mainWheelIntentReleaseSpeed{ 0.08f };
		float mainWheelIntentReleaseDwellMs{ 120.0f };
		float mainWheelIntentBias{ 0.80f };
		float mainWheelIntentNeighborBias{ 0.35f };
		float mainWheelScoreWeightAngle{ 1.0f };
		float mainWheelScoreWeightMotion{ 0.5f };
		float mainWheelScoreWeightInertia{ 0.35f };
		float mainWheelScoreWeightStickiness{ 0.2f };
		float mainWheelSwitchConfidenceMargin{ 0.2f };
		float mainWheelSwitchDwellMs{ 60.0f };
		int mainWheelInnerDeadzoneMaxSlotDelta{ 1 };
		bool mainWheelDebugHoverLog{ false };
		bool mainWheelLogMouseFeatures{ false };
		bool mainWheelLogCandidateScores{ false };
		bool mainWheelLogIntentState{ false };
		bool mainWheelDrawMouseDebugOverlay{ false };

		// [MainWheel.MouseStabilization]
		bool mainWheelMouseStabilizationEnabled{ false };
		float mainWheelMouseCenterHoldRadius{ 0.22f };
		float mainWheelMouseJumpGuardRadius{ 0.33f };
		int mainWheelMouseMaxJumpSlots{ 1 };
		bool mainWheelMouseStepTowardEnabled{ false };
		float mainWheelMouseBoundaryMarginDeg{ 6.0f };
		bool mainWheelMouseBoundaryLowSpeedOnly{ true };
		float mainWheelMouseLowSpeedThreshold{ 0.12f };
		bool mainWheelMouseMotionHintEnabled{ false };
		float mainWheelMouseMotionHintSpeedThreshold{ 0.35f };
		float mainWheelMouseMotionHintBlend{ 0.15f };

		// [MainWheel.LowEnd]
		bool mainWheelDisableBlurOnOpen{ false };
		bool mainWheelPreferPrimitiveBackgrounds{ false };

		// [MainWheel.Indicators]
		bool mainWheelShowHandIndicator{ false };
		ImU32 mainWheelHandLeftColor{ C_SKYRIMWHITE };
		float mainWheelHandLeftOpacity{ 1.0f };
		float mainWheelHandLeftSizeScale{ 1.0f };
		float mainWheelHandLeftThickness{ 0.0f };
		float mainWheelHandLeftOffsetX{ 0.0f };
		float mainWheelHandLeftOffsetY{ 0.0f };
		float mainWheelHandLeftSlotLeftOffsetX{ 0.0f };
		float mainWheelHandLeftSlotLeftOffsetY{ 0.0f };
		float mainWheelHandLeftSlotRightOffsetX{ 0.0f };
		float mainWheelHandLeftSlotRightOffsetY{ 0.0f };
		std::string mainWheelHandLeftAssetPath{};
		ImU32 mainWheelHandRightColor{ C_SKYRIMWHITE };
		float mainWheelHandRightOpacity{ 1.0f };
		float mainWheelHandRightSizeScale{ 1.0f };
		float mainWheelHandRightThickness{ 0.0f };
		float mainWheelHandRightOffsetX{ 0.0f };
		float mainWheelHandRightOffsetY{ 0.0f };
		float mainWheelHandRightSlotLeftOffsetX{ 0.0f };
		float mainWheelHandRightSlotLeftOffsetY{ 0.0f };
		float mainWheelHandRightSlotRightOffsetX{ 0.0f };
		float mainWheelHandRightSlotRightOffsetY{ 0.0f };
		std::string mainWheelHandRightAssetPath{};
	};

	static bool s_loggedGamepadNavSources = false;
	static bool s_loggedGamepadOpenSources = false;
	static bool s_loggedGamepadDpadSources = false;
	static bool s_loggedControllerDebugSources = false;

	static const char* KeySourceLabel(bool hasKey)
	{
		return hasKey ? "ini" : "legacy";
	}

	static void LogGamepadConfigSourcesOnce()
	{
		if (!s_loggedGamepadNavSources) {
			logger::info("Gamepad.Nav sources: innerDeadzone={}, outerDeadzone={}, intentMagnitude={}, smoothing={}, hysteresis={}, autoCenterRestSnap={}",
				KeySourceLabel(Config::WheelBehavior::Gamepad::Nav::HasInnerDeadzone),
				KeySourceLabel(Config::WheelBehavior::Gamepad::Nav::HasOuterDeadzone),
				KeySourceLabel(Config::WheelBehavior::Gamepad::Nav::HasIntentMagnitude),
				KeySourceLabel(Config::WheelBehavior::Gamepad::Nav::HasSmoothingHalfLifeMs),
				KeySourceLabel(Config::WheelBehavior::Gamepad::Nav::HasHysteresisDegrees),
				KeySourceLabel(Config::WheelBehavior::Gamepad::Nav::HasAutoCenterRestSnap));
			s_loggedGamepadNavSources = true;
		}
		if (!s_loggedGamepadOpenSources) {
			logger::info("Gamepad.Open sources: useLastSelection={}, openGraceMs={}",
				KeySourceLabel(Config::WheelBehavior::Gamepad::Open::HasUseLastSelectionOnOpen),
				KeySourceLabel(Config::WheelBehavior::Gamepad::Open::HasOpenGraceMs));
			s_loggedGamepadOpenSources = true;
		}
		if (!s_loggedGamepadDpadSources) {
			logger::info("Gamepad.DPad sources: mode={}",
				KeySourceLabel(Config::WheelBehavior::Gamepad::DPad::HasMode));
			s_loggedGamepadDpadSources = true;
		}
		if (!s_loggedControllerDebugSources) {
			logger::info("Debug.Controller sources: enabled={}, overlay={}",
				KeySourceLabel(Config::WheelBehavior::Gamepad::DebugController::HasEnabled),
				KeySourceLabel(Config::WheelBehavior::Gamepad::DebugController::HasOverlay));
			s_loggedControllerDebugSources = true;
		}
	}

	static WheelBehaviorSnapshot DefaultWheelBehaviorSnapshot()
	{
		return {};
	}

	static bool NearlyEqual(const float a, const float b, const float eps = 1e-5f)
	{
		return std::fabs(a - b) <= eps;
	}

	static bool SnapshotEquals(const WheelBehaviorSnapshot& a, const WheelBehaviorSnapshot& b)
	{
		return a.releaseToUse == b.releaseToUse &&
		       a.closeWheelAfterUse == b.closeWheelAfterUse &&
		       a.rtuAlchemy == b.rtuAlchemy &&
		       a.rtuSpell == b.rtuSpell &&
		       a.rtuShout == b.rtuShout &&
		       a.rtuSmartAssignEnabled == b.rtuSmartAssignEnabled &&
		       a.rtuSmartAssignTargetHands == b.rtuSmartAssignTargetHands &&
		       a.rtuSmartAssignCategoryMask == b.rtuSmartAssignCategoryMask &&
		       a.rtuSmartAssignBothEmptyPriority == b.rtuSmartAssignBothEmptyPriority &&
		       a.rtuSmartAssignOverwriteMode == b.rtuSmartAssignOverwriteMode &&
		       a.rtuSmartAssignDebugLog == b.rtuSmartAssignDebugLog &&
		       NearlyEqual(a.hoverActivateDelaySeconds, b.hoverActivateDelaySeconds) &&
		       a.autoDrawOnUse == b.autoDrawOnUse &&
		       a.instantSpell == b.instantSpell &&
		       a.instantSpellUseDirectCast == b.instantSpellUseDirectCast &&
		       a.rtuAutoInstantSpell == b.rtuAutoInstantSpell &&
		       a.instantPowers == b.instantPowers &&
		       a.shoutPipelineDebug == b.shoutPipelineDebug &&
		       a.instantShout == b.instantShout &&
		       a.rtuAutoInstantShout == b.rtuAutoInstantShout &&
		       NearlyEqual(a.shoutWord2Threshold, b.shoutWord2Threshold) &&
		       NearlyEqual(a.shoutWord3Threshold, b.shoutWord3Threshold) &&
		       NearlyEqual(a.shoutHoldSecsWord1, b.shoutHoldSecsWord1) &&
		       NearlyEqual(a.shoutHoldSecsWord2, b.shoutHoldSecsWord2) &&
		       NearlyEqual(a.shoutHoldSecsWord3, b.shoutHoldSecsWord3) &&
		       a.shoutIgnoreRTUDelay == b.shoutIgnoreRTUDelay &&
		       NearlyEqual(a.shoutStageFillSecs, b.shoutStageFillSecs) &&
		       NearlyEqual(a.shoutStageHoldSecs1, b.shoutStageHoldSecs1) &&
		       NearlyEqual(a.shoutStageHoldSecs2, b.shoutStageHoldSecs2) &&
		       a.clearDepletedConsumables == b.clearDepletedConsumables &&
		       a.scriptedMiscDispatchMode == b.scriptedMiscDispatchMode &&
		       a.bookReadCompatMode == b.bookReadCompatMode &&
		       a.bookReadCompatOnReadFormIDs == b.bookReadCompatOnReadFormIDs &&
		       a.bookReadCompatOnReadPlugins == b.bookReadCompatOnReadPlugins &&
		       a.bookReadCompatOnReadNameTokens == b.bookReadCompatOnReadNameTokens &&
		       a.bookReadCompatDebugLog == b.bookReadCompatDebugLog &&
		       a.keepMissingEnabled == b.keepMissingEnabled &&
		       a.keepMissingConsumables == b.keepMissingConsumables &&
		       a.keepMissingGears == b.keepMissingGears &&
		       a.keepMissingThrowableMods == b.keepMissingThrowableMods &&
		       a.handMemoryEnabled == b.handMemoryEnabled &&
		       a.handMemoryDebugLog == b.handMemoryDebugLog &&
		       NearlyEqual(a.handMemoryRestoreDelaySeconds, b.handMemoryRestoreDelaySeconds) &&
		       NearlyEqual(a.handMemoryRestoreWindowSeconds, b.handMemoryRestoreWindowSeconds) &&
		       a.handMemoryRestoreLeftIfEmpty == b.handMemoryRestoreLeftIfEmpty &&
		       a.handMemoryRestoreRightIfEmpty == b.handMemoryRestoreRightIfEmpty &&
		       a.rtuAntiSlipEnabled == b.rtuAntiSlipEnabled &&
		       NearlyEqual(a.rtuAntiSlipStrength, b.rtuAntiSlipStrength) &&
		       a.heavyListCompatibilityMode == b.heavyListCompatibilityMode &&
		       NearlyEqual(a.heavyListCompatibilitySettleMs, b.heavyListCompatibilitySettleMs) &&
		       a.mutableInventoryHooks == b.mutableInventoryHooks &&
		       a.transformWheelsEnabled == b.transformWheelsEnabled &&
		       a.transformWheelsMode == b.transformWheelsMode &&
		       a.transformWheelsRestorePrevious == b.transformWheelsRestorePrevious &&
		       a.transformWheelsIncludeShouts == b.transformWheelsIncludeShouts &&
		       a.transformWheelsRegenerateThrottleMs == b.transformWheelsRegenerateThrottleMs &&
		       a.transformWheelsRetryWindowMs == b.transformWheelsRetryWindowMs &&
		       a.transformWheelsStableTicks == b.transformWheelsStableTicks &&
		       a.transformWheelsMinEntries == b.transformWheelsMinEntries &&
		       a.transformWheelsCacheHumanSnapshot == b.transformWheelsCacheHumanSnapshot &&
		       a.transformWheelsPersistGeneratedWheels == b.transformWheelsPersistGeneratedWheels &&
		       a.transformWheelsUpdateOnlyOnChange == b.transformWheelsUpdateOnlyOnChange &&
		       a.transformWheelsDebugLog == b.transformWheelsDebugLog &&
		       a.transformWheelsGenericEnabled == b.transformWheelsGenericEnabled &&
		       a.transformWheelsGenericStateID == b.transformWheelsGenericStateID &&
		       a.transformWheelsGenericWheelID == b.transformWheelsGenericWheelID &&
		       a.transformWheelsGenericRaceEditorIDContains == b.transformWheelsGenericRaceEditorIDContains &&
		       a.transformWheelsGenericRaceKeywords == b.transformWheelsGenericRaceKeywords &&
		       a.transformWheelsGenericRaceFormIDs == b.transformWheelsGenericRaceFormIDs &&
		       a.transformWheelsPrecedenceOrder == b.transformWheelsPrecedenceOrder &&
		       a.werewolfAllowBaseWheel == b.werewolfAllowBaseWheel &&
		       a.vampireLordAllowBaseWheel == b.vampireLordAllowBaseWheel &&
		       a.werewolfAllowBaseWheelSpells == b.werewolfAllowBaseWheelSpells &&
		       a.werewolfAllowBaseWheelShouts == b.werewolfAllowBaseWheelShouts &&
		       a.werewolfFormEnabled == b.werewolfFormEnabled &&
		       a.werewolfFormPopulateMode == b.werewolfFormPopulateMode &&
		       a.werewolfFormRaceEditorIDContains == b.werewolfFormRaceEditorIDContains &&
		       a.werewolfFormRaceKeywords == b.werewolfFormRaceKeywords &&
		       a.werewolfFormRaceFormIDs == b.werewolfFormRaceFormIDs &&
		       a.werewolfFormSpellTokens == b.werewolfFormSpellTokens &&
		       a.werewolfFormExitSpellTokens == b.werewolfFormExitSpellTokens &&
		       a.werewolfFormAdditionalSpellFormIDs == b.werewolfFormAdditionalSpellFormIDs &&
		       a.werewolfFormDebugLog == b.werewolfFormDebugLog &&
		       a.vampireLordFormEnabled == b.vampireLordFormEnabled &&
		       a.vampireLordFormPopulateMode == b.vampireLordFormPopulateMode &&
		       a.vampireLordFormHideTransformSpell == b.vampireLordFormHideTransformSpell &&
		       a.vampireLordFormHideForcedRightHandSpells == b.vampireLordFormHideForcedRightHandSpells &&
		       a.vampireLordFormBlockHiddenSpellActivation == b.vampireLordFormBlockHiddenSpellActivation &&
		       a.vampireLordFormBlockRegularSpellsInMeleeMode == b.vampireLordFormBlockRegularSpellsInMeleeMode &&
		       a.vampireLordFormRaceEditorIDContains == b.vampireLordFormRaceEditorIDContains &&
		       a.vampireLordFormRaceKeywords == b.vampireLordFormRaceKeywords &&
		       a.vampireLordFormRaceFormIDs == b.vampireLordFormRaceFormIDs &&
		       a.vampireLordFormSpellTokens == b.vampireLordFormSpellTokens &&
		       a.vampireLordFormExitSpellTokens == b.vampireLordFormExitSpellTokens &&
		       a.vampireLordFormAdditionalSpellFormIDs == b.vampireLordFormAdditionalSpellFormIDs &&
		       a.vampireLordFormHiddenSpellFormIDs == b.vampireLordFormHiddenSpellFormIDs &&
		       a.vampireLordFormHiddenSpellTokens == b.vampireLordFormHiddenSpellTokens &&
		       a.vampireLordFormDebugLog == b.vampireLordFormDebugLog &&
		       a.lichFormEnabled == b.lichFormEnabled &&
		       a.lichFormMode == b.lichFormMode &&
		       a.lichFormPopulateMode == b.lichFormPopulateMode &&
		       a.lichFormAllowBaseWheel == b.lichFormAllowBaseWheel &&
		       a.lichFormTransformGuard == b.lichFormTransformGuard &&
		       a.lichFormBlockBoundSpells == b.lichFormBlockBoundSpells &&
		       a.lichFormHideWeapons == b.lichFormHideWeapons &&
		       a.lichFormHideGear == b.lichFormHideGear &&
		       a.lichFormBlockStaffSwapping == b.lichFormBlockStaffSwapping &&
		       a.lichFormSuppressDirectCast == b.lichFormSuppressDirectCast &&
		       a.lichFormRaceEditorIDContains == b.lichFormRaceEditorIDContains &&
		       a.lichFormRaceKeywords == b.lichFormRaceKeywords &&
		       a.lichFormRaceFormIDs == b.lichFormRaceFormIDs &&
		       a.lichFormSpellTokens == b.lichFormSpellTokens &&
		       a.lichFormExitSpellTokens == b.lichFormExitSpellTokens &&
		       a.lichFormAdditionalSpellFormIDs == b.lichFormAdditionalSpellFormIDs &&
		       a.lichFormDebugLog == b.lichFormDebugLog &&

		       a.cooldownsEnabled == b.cooldownsEnabled &&
		       a.cooldownsShowTimer == b.cooldownsShowTimer &&
		       NearlyEqual(a.cooldownsContentDimAlpha, b.cooldownsContentDimAlpha) &&
		       a.cooldownsSelectedIndicatorEnabled == b.cooldownsSelectedIndicatorEnabled &&
		       a.cooldownsOverlayColor == b.cooldownsOverlayColor &&
		       a.cooldownsSelectedIndicatorTintColor == b.cooldownsSelectedIndicatorTintColor &&
		       NearlyEqual(a.cooldownsCacheWindowSeconds, b.cooldownsCacheWindowSeconds) &&
		       a.cooldownsTimerTextFontIndex == b.cooldownsTimerTextFontIndex &&
		       NearlyEqual(a.cooldownsTimerTextSize, b.cooldownsTimerTextSize) &&
		       a.cooldownsTimerTextColor == b.cooldownsTimerTextColor &&

		       a.hoverDelayEnabled == b.hoverDelayEnabled &&
		       NearlyEqual(a.hoverDelayRadius, b.hoverDelayRadius) &&
		       NearlyEqual(a.hoverDelayRadiusOffset, b.hoverDelayRadiusOffset) &&
		       NearlyEqual(a.hoverDelayThickness, b.hoverDelayThickness) &&
		       a.hoverDelayColor == b.hoverDelayColor &&
		       a.hoverDelayBackgroundColor == b.hoverDelayBackgroundColor &&
		       a.hoverDelayInstantSpellColor == b.hoverDelayInstantSpellColor &&
		       a.hoverDelayInstantSpellBackgroundColor == b.hoverDelayInstantSpellBackgroundColor &&
		       a.hoverDelayInstantSpellUseReskinAssets == b.hoverDelayInstantSpellUseReskinAssets &&
		       NearlyEqual(a.hoverDelayInstantSpellAssetScale, b.hoverDelayInstantSpellAssetScale) &&
		       NearlyEqual(a.hoverDelayInstantSpellAssetOffsetX, b.hoverDelayInstantSpellAssetOffsetX) &&
		       NearlyEqual(a.hoverDelayInstantSpellAssetOffsetY, b.hoverDelayInstantSpellAssetOffsetY) &&
		       NearlyEqual(a.hoverDelayInstantSpellAssetOpacity, b.hoverDelayInstantSpellAssetOpacity) &&
		       NearlyEqual(a.hoverDelayInstantSpellHandIndicatorScale, b.hoverDelayInstantSpellHandIndicatorScale) &&
		       NearlyEqual(a.hoverDelayInstantSpellHandIndicatorOffsetX, b.hoverDelayInstantSpellHandIndicatorOffsetX) &&
		       NearlyEqual(a.hoverDelayInstantSpellHandIndicatorOffsetY, b.hoverDelayInstantSpellHandIndicatorOffsetY) &&
		       NearlyEqual(a.hoverDelayInstantSpellHandIndicatorLeftOffsetX, b.hoverDelayInstantSpellHandIndicatorLeftOffsetX) &&
		       NearlyEqual(a.hoverDelayInstantSpellHandIndicatorLeftOffsetY, b.hoverDelayInstantSpellHandIndicatorLeftOffsetY) &&
		       NearlyEqual(a.hoverDelayInstantSpellHandIndicatorRightOffsetX, b.hoverDelayInstantSpellHandIndicatorRightOffsetX) &&
		       NearlyEqual(a.hoverDelayInstantSpellHandIndicatorRightOffsetY, b.hoverDelayInstantSpellHandIndicatorRightOffsetY) &&
		       NearlyEqual(a.hoverDelayInstantSpellHandIndicatorBothOffsetX, b.hoverDelayInstantSpellHandIndicatorBothOffsetX) &&
		       NearlyEqual(a.hoverDelayInstantSpellHandIndicatorBothOffsetY, b.hoverDelayInstantSpellHandIndicatorBothOffsetY) &&
		       NearlyEqual(a.hoverDelayInstantSpellHandIndicatorOpacity, b.hoverDelayInstantSpellHandIndicatorOpacity) &&
		       a.hoverDelayInstantSpellUseAtlasAnimation == b.hoverDelayInstantSpellUseAtlasAnimation &&
		       a.hoverDelayInstantSpellAtlasCols == b.hoverDelayInstantSpellAtlasCols &&
		       a.hoverDelayInstantSpellAtlasRows == b.hoverDelayInstantSpellAtlasRows &&
		       a.hoverDelayInstantSpellAtlasFrameCount == b.hoverDelayInstantSpellAtlasFrameCount &&

		       a.soundsEnabled == b.soundsEnabled &&
		       a.soundsHoverEditorID == b.soundsHoverEditorID &&
		       a.soundsActivateEditorID == b.soundsActivateEditorID &&

		       a.enableShoutStageSounds == b.enableShoutStageSounds &&
		       a.shoutStageSoundMode == b.shoutStageSoundMode &&
		       a.shoutUISoundEditorID == b.shoutUISoundEditorID &&
		       NearlyEqual(a.shoutUIStageVolume1, b.shoutUIStageVolume1) &&
		       NearlyEqual(a.shoutUIStageVolume2, b.shoutUIStageVolume2) &&
		       NearlyEqual(a.shoutUIStageVolume3, b.shoutUIStageVolume3) &&
		       NearlyEqual(a.shoutUIStagePitch1, b.shoutUIStagePitch1) &&
		       NearlyEqual(a.shoutUIStagePitch2, b.shoutUIStagePitch2) &&
		       NearlyEqual(a.shoutUIStagePitch3, b.shoutUIStagePitch3) &&
		       a.shoutWord1SoundEditorID == b.shoutWord1SoundEditorID &&
		       a.shoutWord2SoundEditorID == b.shoutWord2SoundEditorID &&
		       a.shoutWord3SoundEditorID == b.shoutWord3SoundEditorID &&

		       a.mainWheelDebugEnabled == b.mainWheelDebugEnabled &&
		       a.mainWheelDebugOverlayEnabled == b.mainWheelDebugOverlayEnabled &&
		       a.mainWheelDebugVerbose == b.mainWheelDebugVerbose &&
		       a.mainWheelDebugRateLimitMs == b.mainWheelDebugRateLimitMs &&
		       a.mainWheelDebugLogOpenClose == b.mainWheelDebugLogOpenClose &&
		       a.mainWheelDebugLogConfig == b.mainWheelDebugLogConfig &&
		       a.mainWheelDebugLogInput == b.mainWheelDebugLogInput &&
		       a.mainWheelDebugLogScaling == b.mainWheelDebugLogScaling &&
		       a.mainWheelDebugLogClamp == b.mainWheelDebugLogClamp &&
		       a.mainWheelDebugLogIndicators == b.mainWheelDebugLogIndicators &&
		       a.mainWheelDebugLogReskinResolve == b.mainWheelDebugLogReskinResolve &&
		       a.mainWheelDebugLogAssets == b.mainWheelDebugLogAssets &&
		       a.mainWheelDebugLogPerf == b.mainWheelDebugLogPerf &&

		       NearlyEqual(a.mainWheelCenterLockRadiusPx, b.mainWheelCenterLockRadiusPx) &&
		       NearlyEqual(a.mainWheelJumpGuardRadiusPx, b.mainWheelJumpGuardRadiusPx) &&
		       NearlyEqual(a.mainWheelHysteresisDegrees, b.mainWheelHysteresisDegrees) &&
		       a.mainWheelCenterSlowdownEnabled == b.mainWheelCenterSlowdownEnabled &&
		       NearlyEqual(a.mainWheelGainCenter, b.mainWheelGainCenter) &&
		       NearlyEqual(a.mainWheelGainOuter, b.mainWheelGainOuter) &&
		       NearlyEqual(a.mainWheelCurvePower, b.mainWheelCurvePower) &&
		       NearlyEqual(a.mainWheelInnerDeadZoneR, b.mainWheelInnerDeadZoneR) &&
		       NearlyEqual(a.mainWheelInnerBlendZoneR, b.mainWheelInnerBlendZoneR) &&
		       NearlyEqual(a.mainWheelMinStableSpeed, b.mainWheelMinStableSpeed) &&
		       NearlyEqual(a.mainWheelVelocityHalfLifeMs, b.mainWheelVelocityHalfLifeMs) &&
		       NearlyEqual(a.mainWheelStableDirHalfLifeMs, b.mainWheelStableDirHalfLifeMs) &&
		       NearlyEqual(a.mainWheelIntentMinSpeed, b.mainWheelIntentMinSpeed) &&
		       NearlyEqual(a.mainWheelIntentSustainSpeed, b.mainWheelIntentSustainSpeed) &&
		       NearlyEqual(a.mainWheelIntentConfirmMs, b.mainWheelIntentConfirmMs) &&
		       NearlyEqual(a.mainWheelIntentConeDeg, b.mainWheelIntentConeDeg) &&
		       NearlyEqual(a.mainWheelIntentReleaseConeDeg, b.mainWheelIntentReleaseConeDeg) &&
		       NearlyEqual(a.mainWheelIntentMinAngleDeg, b.mainWheelIntentMinAngleDeg) &&
		       NearlyEqual(a.mainWheelIntentReleaseSpeed, b.mainWheelIntentReleaseSpeed) &&
		       NearlyEqual(a.mainWheelIntentReleaseDwellMs, b.mainWheelIntentReleaseDwellMs) &&
		       NearlyEqual(a.mainWheelIntentBias, b.mainWheelIntentBias) &&
		       NearlyEqual(a.mainWheelIntentNeighborBias, b.mainWheelIntentNeighborBias) &&
		       NearlyEqual(a.mainWheelScoreWeightAngle, b.mainWheelScoreWeightAngle) &&
		       NearlyEqual(a.mainWheelScoreWeightMotion, b.mainWheelScoreWeightMotion) &&
		       NearlyEqual(a.mainWheelScoreWeightInertia, b.mainWheelScoreWeightInertia) &&
		       NearlyEqual(a.mainWheelScoreWeightStickiness, b.mainWheelScoreWeightStickiness) &&
		       NearlyEqual(a.mainWheelSwitchConfidenceMargin, b.mainWheelSwitchConfidenceMargin) &&
		       NearlyEqual(a.mainWheelSwitchDwellMs, b.mainWheelSwitchDwellMs) &&
		       a.mainWheelInnerDeadzoneMaxSlotDelta == b.mainWheelInnerDeadzoneMaxSlotDelta &&
		       a.mainWheelDebugHoverLog == b.mainWheelDebugHoverLog &&
		       a.mainWheelLogMouseFeatures == b.mainWheelLogMouseFeatures &&
		       a.mainWheelLogCandidateScores == b.mainWheelLogCandidateScores &&
		       a.mainWheelLogIntentState == b.mainWheelLogIntentState &&
		       a.mainWheelDrawMouseDebugOverlay == b.mainWheelDrawMouseDebugOverlay &&
		       a.mainWheelMouseStabilizationEnabled == b.mainWheelMouseStabilizationEnabled &&
		       NearlyEqual(a.mainWheelMouseCenterHoldRadius, b.mainWheelMouseCenterHoldRadius) &&
		       NearlyEqual(a.mainWheelMouseJumpGuardRadius, b.mainWheelMouseJumpGuardRadius) &&
		       a.mainWheelMouseMaxJumpSlots == b.mainWheelMouseMaxJumpSlots &&
		       a.mainWheelMouseStepTowardEnabled == b.mainWheelMouseStepTowardEnabled &&
		       NearlyEqual(a.mainWheelMouseBoundaryMarginDeg, b.mainWheelMouseBoundaryMarginDeg) &&
		       a.mainWheelMouseBoundaryLowSpeedOnly == b.mainWheelMouseBoundaryLowSpeedOnly &&
		       NearlyEqual(a.mainWheelMouseLowSpeedThreshold, b.mainWheelMouseLowSpeedThreshold) &&
		       a.mainWheelMouseMotionHintEnabled == b.mainWheelMouseMotionHintEnabled &&
		       NearlyEqual(a.mainWheelMouseMotionHintSpeedThreshold, b.mainWheelMouseMotionHintSpeedThreshold) &&
		       NearlyEqual(a.mainWheelMouseMotionHintBlend, b.mainWheelMouseMotionHintBlend) &&

		       a.mainWheelDisableBlurOnOpen == b.mainWheelDisableBlurOnOpen &&
		       a.mainWheelPreferPrimitiveBackgrounds == b.mainWheelPreferPrimitiveBackgrounds &&
		       a.mainWheelShowHandIndicator == b.mainWheelShowHandIndicator &&
		       a.mainWheelHandLeftColor == b.mainWheelHandLeftColor &&
		       NearlyEqual(a.mainWheelHandLeftOpacity, b.mainWheelHandLeftOpacity) &&
		       NearlyEqual(a.mainWheelHandLeftSizeScale, b.mainWheelHandLeftSizeScale) &&
		       NearlyEqual(a.mainWheelHandLeftThickness, b.mainWheelHandLeftThickness) &&
		       NearlyEqual(a.mainWheelHandLeftOffsetX, b.mainWheelHandLeftOffsetX) &&
		       NearlyEqual(a.mainWheelHandLeftOffsetY, b.mainWheelHandLeftOffsetY) &&
		       NearlyEqual(a.mainWheelHandLeftSlotLeftOffsetX, b.mainWheelHandLeftSlotLeftOffsetX) &&
		       NearlyEqual(a.mainWheelHandLeftSlotLeftOffsetY, b.mainWheelHandLeftSlotLeftOffsetY) &&
		       NearlyEqual(a.mainWheelHandLeftSlotRightOffsetX, b.mainWheelHandLeftSlotRightOffsetX) &&
		       NearlyEqual(a.mainWheelHandLeftSlotRightOffsetY, b.mainWheelHandLeftSlotRightOffsetY) &&
		       a.mainWheelHandLeftAssetPath == b.mainWheelHandLeftAssetPath &&
		       a.mainWheelHandRightColor == b.mainWheelHandRightColor &&
		       NearlyEqual(a.mainWheelHandRightOpacity, b.mainWheelHandRightOpacity) &&
		       NearlyEqual(a.mainWheelHandRightSizeScale, b.mainWheelHandRightSizeScale) &&
		       NearlyEqual(a.mainWheelHandRightThickness, b.mainWheelHandRightThickness) &&
		       NearlyEqual(a.mainWheelHandRightOffsetX, b.mainWheelHandRightOffsetX) &&
		       NearlyEqual(a.mainWheelHandRightOffsetY, b.mainWheelHandRightOffsetY) &&
		       NearlyEqual(a.mainWheelHandRightSlotLeftOffsetX, b.mainWheelHandRightSlotLeftOffsetX) &&
		       NearlyEqual(a.mainWheelHandRightSlotLeftOffsetY, b.mainWheelHandRightSlotLeftOffsetY) &&
		       NearlyEqual(a.mainWheelHandRightSlotRightOffsetX, b.mainWheelHandRightSlotRightOffsetX) &&
		       NearlyEqual(a.mainWheelHandRightSlotRightOffsetY, b.mainWheelHandRightSlotRightOffsetY) &&
		       a.mainWheelHandRightAssetPath == b.mainWheelHandRightAssetPath;
	}

	static WheelBehaviorSnapshot ReadWheelBehaviorSnapshotFromIni(const CSimpleIniA& ini)
	{
		WheelBehaviorSnapshot snapshot = DefaultWheelBehaviorSnapshot();

		auto readSection = [&](const char* section) {
			GetBoolValue(ini, section, "ReleaseToUse", snapshot.releaseToUse);
			GetBoolValue(ini, section, "CloseWheelAfterUse", snapshot.closeWheelAfterUse);
			GetBoolValue(ini, section, "RTUAlchemy", snapshot.rtuAlchemy);
			GetBoolValue(ini, section, "RTUSpell", snapshot.rtuSpell);
			GetBoolValue(ini, section, "RTUShout", snapshot.rtuShout);
			GetBoolValue(ini, section, "RTUSmartAssignEnabled", snapshot.rtuSmartAssignEnabled);
			{
				std::uint32_t tmp = snapshot.rtuSmartAssignTargetHands;
				if (!GetUInt32Value(ini, section, "RTUSmartAssignTargetHands", tmp)) {
					float tmpFloat = static_cast<float>(tmp);
					if (GetFloatValue(ini, section, "RTUSmartAssignTargetHands", tmpFloat)) {
						tmp = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
					}
				}
				snapshot.rtuSmartAssignTargetHands = tmp;
			}
			{
				std::uint32_t tmp = snapshot.rtuSmartAssignCategoryMask;
				if (!GetUInt32Value(ini, section, "RTUSmartAssignCategoryMask", tmp)) {
					float tmpFloat = static_cast<float>(tmp);
					if (GetFloatValue(ini, section, "RTUSmartAssignCategoryMask", tmpFloat)) {
						tmp = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
					}
				}
				snapshot.rtuSmartAssignCategoryMask = tmp;
			}
			{
				std::uint32_t tmp = snapshot.rtuSmartAssignBothEmptyPriority;
				if (!GetUInt32Value(ini, section, "RTUSmartAssignBothEmptyPriority", tmp)) {
					float tmpFloat = static_cast<float>(tmp);
					if (GetFloatValue(ini, section, "RTUSmartAssignBothEmptyPriority", tmpFloat)) {
						tmp = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
					}
				}
				snapshot.rtuSmartAssignBothEmptyPriority = tmp;
			}
			{
				std::uint32_t tmp = snapshot.rtuSmartAssignOverwriteMode;
				if (!GetUInt32Value(ini, section, "RTUSmartAssignOverwriteMode", tmp)) {
					float tmpFloat = static_cast<float>(tmp);
					if (GetFloatValue(ini, section, "RTUSmartAssignOverwriteMode", tmpFloat)) {
						tmp = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
					}
				}
				snapshot.rtuSmartAssignOverwriteMode = tmp;
			}
			GetBoolValue(ini, section, "RTUSmartAssignDebugLog", snapshot.rtuSmartAssignDebugLog);
			GetFloatValue(ini, section, "HoverActivateDelaySeconds", snapshot.hoverActivateDelaySeconds);
			GetBoolValue(ini, section, "AutoDrawOnUse", snapshot.autoDrawOnUse);
			GetBoolValue(ini, section, "InstantSpell", snapshot.instantSpell);
			GetBoolValue(ini, section, "InstantSpellUseDirectCast", snapshot.instantSpellUseDirectCast);
			GetBoolValue(ini, section, "RTUAutoInstantSpell", snapshot.rtuAutoInstantSpell);
			GetBoolValue(ini, section, "InstantPowers", snapshot.instantPowers);
			{
				// dMenu sliders write floating point values, so read as float and cast
				float tempMode = static_cast<float>(snapshot.instantSpellConcentrationMode);
				const char* rawValue = ini.GetValue(section, "InstantSpellConcentrationMode", nullptr);
				bool found = GetFloatValue(ini, section, "InstantSpellConcentrationMode", tempMode);
				logger::info("[Config] InstantSpellConcentrationMode: section='{}', rawValue='{}', GetFloatValue returned {} with tempMode={:.2f}",
					section, rawValue ? rawValue : "NULL", found, tempMode);
				if (found) {
					snapshot.instantSpellConcentrationMode = static_cast<std::uint32_t>(tempMode);
				}
			}
			GetFloatValue(ini, section, "InstantSpellConcentrationMaxSeconds", snapshot.instantSpellConcentrationMaxSeconds);
			GetBoolValue(ini, section, "InstantSpellDebugLog", snapshot.instantSpellDebugLog);
			GetBoolValue(ini, section, "ShoutPipelineDebug", snapshot.shoutPipelineDebug);
			GetBoolValue(ini, section, "InstantShout", snapshot.instantShout);
			GetBoolValue(ini, section, "RTUAutoInstantShout", snapshot.rtuAutoInstantShout);
			GetFloatValue(ini, section, "ShoutWord2Threshold", snapshot.shoutWord2Threshold);
			GetFloatValue(ini, section, "ShoutWord3Threshold", snapshot.shoutWord3Threshold);
			GetFloatValue(ini, section, "ShoutHoldSecsWord1", snapshot.shoutHoldSecsWord1);
			GetFloatValue(ini, section, "ShoutHoldSecsWord2", snapshot.shoutHoldSecsWord2);
			GetFloatValue(ini, section, "ShoutHoldSecsWord3", snapshot.shoutHoldSecsWord3);
			GetBoolValue(ini, section, "ShoutIgnoreRTUDelay", snapshot.shoutIgnoreRTUDelay);
			GetFloatValue(ini, section, "ShoutStageFillSecs", snapshot.shoutStageFillSecs);
			GetFloatValue(ini, section, "ShoutStageHoldSecs1", snapshot.shoutStageHoldSecs1);
			GetFloatValue(ini, section, "ShoutStageHoldSecs2", snapshot.shoutStageHoldSecs2);
			GetBoolValue(ini, section, "ClearDepletedConsumables", snapshot.clearDepletedConsumables);
			{
				std::uint32_t tmp = snapshot.scriptedMiscDispatchMode;
				if (!GetUInt32Value(ini, section, "ScriptedMiscDispatchMode", tmp)) {
					float tmpFloat = static_cast<float>(tmp);
					if (GetFloatValue(ini, section, "ScriptedMiscDispatchMode", tmpFloat)) {
						tmp = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
					}
				}
				snapshot.scriptedMiscDispatchMode = std::clamp(tmp, 0u, 4u);
			}
			GetBoolValue(ini, section, "RTUAntiSlipEnabled", snapshot.rtuAntiSlipEnabled);
			GetFloatValue(ini, section, "RTUAntiSlipStrength", snapshot.rtuAntiSlipStrength);
			GetBoolValue(ini, section, "LootMenuOverride", snapshot.lootMenuOverride);
			GetBoolValue(ini, section, "HeavyListCompatibilityMode", snapshot.heavyListCompatibilityMode);
			GetFloatValue(ini, section, "HeavyListCompatibilitySettleMs", snapshot.heavyListCompatibilitySettleMs);
			snapshot.heavyListCompatibilitySettleMs = std::clamp(snapshot.heavyListCompatibilitySettleMs, 150.0f, 1000.0f);
			GetBoolValue(ini, section, "MutableInventoryHooks", snapshot.mutableInventoryHooks);
		};

		// Backward compatibility: read legacy section first, then allow the new section to override it.
		readSection("InstantUse");
		readSection("WheelBehavior");

		snapshot.bookReadCompatMode = ReadBookReadCompatModeValue(
			ini, "BookReadCompat", "Mode", snapshot.bookReadCompatMode);
		GetStringValue(ini, "BookReadCompat", "OnReadFormIDs", snapshot.bookReadCompatOnReadFormIDs);
		GetStringValue(ini, "BookReadCompat", "OnReadPlugins", snapshot.bookReadCompatOnReadPlugins);
		GetStringValue(ini, "BookReadCompat", "OnReadNameTokens", snapshot.bookReadCompatOnReadNameTokens);
		GetBoolValue(ini, "BookReadCompat", "DebugLog", snapshot.bookReadCompatDebugLog);

		GetBoolValue(ini, "WheelBehavior.KeepMissing", "Enabled", snapshot.keepMissingEnabled);
		GetBoolValue(ini, "WheelBehavior.KeepMissing", "KeepConsumables", snapshot.keepMissingConsumables);
		GetBoolValue(ini, "WheelBehavior.KeepMissing", "KeepGears", snapshot.keepMissingGears);
		GetBoolValue(ini, "WheelBehavior.KeepMissing", "KeepThrowableMods", snapshot.keepMissingThrowableMods);
		GetBoolValue(ini, "WheelBehavior.HandMemory", "Enabled", snapshot.handMemoryEnabled);
		GetBoolValue(ini, "WheelBehavior.HandMemory", "DebugLog", snapshot.handMemoryDebugLog);
		GetFloatValue(ini, "WheelBehavior.HandMemory", "RestoreDelaySeconds", snapshot.handMemoryRestoreDelaySeconds);
		GetFloatValue(ini, "WheelBehavior.HandMemory", "RestoreWindowSeconds", snapshot.handMemoryRestoreWindowSeconds);
		GetBoolValue(ini, "WheelBehavior.HandMemory", "RestoreLeftIfEmpty", snapshot.handMemoryRestoreLeftIfEmpty);
		GetBoolValue(ini, "WheelBehavior.HandMemory", "RestoreRightIfEmpty", snapshot.handMemoryRestoreRightIfEmpty);

		GetBoolValue(ini, "TransformWheels", "Enabled", snapshot.transformWheelsEnabled);
		{
			float tmpMode = static_cast<float>(snapshot.transformWheelsMode);
			if (GetFloatValue(ini, "TransformWheels", "Mode", tmpMode)) {
				snapshot.transformWheelsMode = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpMode)));
			}
		}
		GetBoolValue(ini, "TransformWheels", "RestorePreviousWheel", snapshot.transformWheelsRestorePrevious);
		GetBoolValue(ini, "TransformWheels", "IncludeShouts", snapshot.transformWheelsIncludeShouts);
		{
			std::uint32_t tmpThrottle = snapshot.transformWheelsRegenerateThrottleMs;
			if (!GetUInt32Value(ini, "TransformWheels", "RegenerateThrottleMs", tmpThrottle)) {
				float tmpFloat = static_cast<float>(tmpThrottle);
				if (GetFloatValue(ini, "TransformWheels", "RegenerateThrottleMs", tmpFloat)) {
					tmpThrottle = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
				}
			}
			snapshot.transformWheelsRegenerateThrottleMs = tmpThrottle;
		}
		{
			std::uint32_t tmpRetry = snapshot.transformWheelsRetryWindowMs;
			if (!GetUInt32Value(ini, "TransformWheels", "RetryWindowMs", tmpRetry)) {
				float tmpFloat = static_cast<float>(tmpRetry);
				if (GetFloatValue(ini, "TransformWheels", "RetryWindowMs", tmpFloat)) {
					tmpRetry = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
				}
			}
			snapshot.transformWheelsRetryWindowMs = tmpRetry;
		}
		{
			std::uint32_t tmpStable = snapshot.transformWheelsStableTicks;
			if (!GetUInt32Value(ini, "TransformWheels", "StableTicks", tmpStable)) {
				float tmpFloat = static_cast<float>(tmpStable);
				if (GetFloatValue(ini, "TransformWheels", "StableTicks", tmpFloat)) {
					tmpStable = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
				}
			}
			snapshot.transformWheelsStableTicks = tmpStable;
		}
		{
			std::uint32_t tmpMin = snapshot.transformWheelsMinEntries;
			if (!GetUInt32Value(ini, "TransformWheels", "MinEntries", tmpMin)) {
				float tmpFloat = static_cast<float>(tmpMin);
				if (GetFloatValue(ini, "TransformWheels", "MinEntries", tmpFloat)) {
					tmpMin = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
				}
			}
			snapshot.transformWheelsMinEntries = tmpMin;
		}
		GetBoolValue(ini, "TransformWheels", "CacheHumanSnapshot", snapshot.transformWheelsCacheHumanSnapshot);
		GetBoolValue(ini, "TransformWheels", "PersistGeneratedWheels", snapshot.transformWheelsPersistGeneratedWheels);
		GetBoolValue(ini, "TransformWheels", "UpdateOnlyOnChange", snapshot.transformWheelsUpdateOnlyOnChange);
		GetBoolValue(ini, "TransformWheels", "DebugLog", snapshot.transformWheelsDebugLog);
		GetBoolValue(ini, "TransformWheels", "GenericEnabled", snapshot.transformWheelsGenericEnabled);
		GetStringValue(ini, "TransformWheels", "GenericStateID", snapshot.transformWheelsGenericStateID);
		GetStringValue(ini, "TransformWheels", "GenericWheelID", snapshot.transformWheelsGenericWheelID);
		GetStringValue(ini, "TransformWheels", "GenericRaceEditorIDContains", snapshot.transformWheelsGenericRaceEditorIDContains);
		GetStringValue(ini, "TransformWheels", "GenericRaceKeywords", snapshot.transformWheelsGenericRaceKeywords);
		GetStringValue(ini, "TransformWheels", "GenericRaceFormIDs", snapshot.transformWheelsGenericRaceFormIDs);
		GetStringValue(ini, "TransformWheels", "PrecedenceOrder", snapshot.transformWheelsPrecedenceOrder);
		GetBoolValue(ini, "TransformWheels", "WerewolfAllowBaseWheel", snapshot.werewolfAllowBaseWheel);
		GetBoolValue(ini, "TransformWheels", "VampireLordAllowBaseWheel", snapshot.vampireLordAllowBaseWheel);
		GetBoolValue(ini, "TransformWheels", "WerewolfAllowBaseWheelSpells", snapshot.werewolfAllowBaseWheelSpells);
		GetBoolValue(ini, "TransformWheels", "WerewolfAllowBaseWheelShouts", snapshot.werewolfAllowBaseWheelShouts);

		GetBoolValue(ini, "WerewolfForm", "Enabled", snapshot.werewolfFormEnabled);
		GetStringValue(ini, "WerewolfForm", "PopulateMode", snapshot.werewolfFormPopulateMode);
		GetStringValue(ini, "WerewolfForm", "RaceEditorIDContains", snapshot.werewolfFormRaceEditorIDContains);
		GetStringValue(ini, "WerewolfForm", "RaceKeywords", snapshot.werewolfFormRaceKeywords);
		GetStringValue(ini, "WerewolfForm", "RaceFormIDs", snapshot.werewolfFormRaceFormIDs);
		GetStringValue(ini, "WerewolfForm", "SpellTokens", snapshot.werewolfFormSpellTokens);
		GetStringValue(ini, "WerewolfForm", "ExitSpellTokens", snapshot.werewolfFormExitSpellTokens);
		GetStringValue(ini, "WerewolfForm", "AdditionalSpellFormIDs", snapshot.werewolfFormAdditionalSpellFormIDs);
		GetBoolValue(ini, "WerewolfForm", "DebugLog", snapshot.werewolfFormDebugLog);

		GetBoolValue(ini, "VampireLordForm", "Enabled", snapshot.vampireLordFormEnabled);
		GetStringValue(ini, "VampireLordForm", "PopulateMode", snapshot.vampireLordFormPopulateMode);
		GetBoolValue(ini, "VampireLordForm", "HideTransformSpell", snapshot.vampireLordFormHideTransformSpell);
		GetBoolValue(ini, "VampireLordForm", "HideForcedRightHandSpells", snapshot.vampireLordFormHideForcedRightHandSpells);
		GetBoolValue(ini, "VampireLordForm", "BlockHiddenSpellActivation", snapshot.vampireLordFormBlockHiddenSpellActivation);
		GetBoolValue(ini, "VampireLordForm", "BlockRegularSpellsInMeleeMode", snapshot.vampireLordFormBlockRegularSpellsInMeleeMode);
		GetStringValue(ini, "VampireLordForm", "RaceEditorIDContains", snapshot.vampireLordFormRaceEditorIDContains);
		GetStringValue(ini, "VampireLordForm", "RaceKeywords", snapshot.vampireLordFormRaceKeywords);
		GetStringValue(ini, "VampireLordForm", "RaceFormIDs", snapshot.vampireLordFormRaceFormIDs);
		GetStringValue(ini, "VampireLordForm", "SpellTokens", snapshot.vampireLordFormSpellTokens);
		GetStringValue(ini, "VampireLordForm", "ExitSpellTokens", snapshot.vampireLordFormExitSpellTokens);
		GetStringValue(ini, "VampireLordForm", "AdditionalSpellFormIDs", snapshot.vampireLordFormAdditionalSpellFormIDs);
		GetStringValue(ini, "VampireLordForm", "HiddenSpellFormIDs", snapshot.vampireLordFormHiddenSpellFormIDs);
		GetStringValue(ini, "VampireLordForm", "HiddenSpellTokens", snapshot.vampireLordFormHiddenSpellTokens);
		GetBoolValue(ini, "VampireLordForm", "DebugLog", snapshot.vampireLordFormDebugLog);

		GetBoolValue(ini, "LichForm", "Enabled", snapshot.lichFormEnabled);
		GetStringValue(ini, "LichForm", "Mode", snapshot.lichFormMode);
		GetStringValue(ini, "LichForm", "PopulateMode", snapshot.lichFormPopulateMode);
		GetBoolValue(ini, "LichForm", "AllowBaseWheel", snapshot.lichFormAllowBaseWheel);
		GetStringValue(ini, "LichForm", "TransformGuard", snapshot.lichFormTransformGuard);
		GetBoolValue(ini, "LichForm", "BlockBoundSpells", snapshot.lichFormBlockBoundSpells);
		GetBoolValue(ini, "LichForm", "HideWeapons", snapshot.lichFormHideWeapons);
		GetBoolValue(ini, "LichForm", "HideGear", snapshot.lichFormHideGear);
		GetBoolValue(ini, "LichForm", "BlockStaffSwapping", snapshot.lichFormBlockStaffSwapping);
		GetBoolValue(ini, "LichForm", "SuppressDirectCast", snapshot.lichFormSuppressDirectCast);
		GetStringValue(ini, "LichForm", "RaceEditorIDContains", snapshot.lichFormRaceEditorIDContains);
		GetStringValue(ini, "LichForm", "RaceKeywords", snapshot.lichFormRaceKeywords);
		GetStringValue(ini, "LichForm", "RaceFormIDs", snapshot.lichFormRaceFormIDs);
		GetStringValue(ini, "LichForm", "SpellTokens", snapshot.lichFormSpellTokens);
		GetStringValue(ini, "LichForm", "ExitSpellTokens", snapshot.lichFormExitSpellTokens);
		GetStringValue(ini, "LichForm", "AdditionalSpellFormIDs", snapshot.lichFormAdditionalSpellFormIDs);
		GetBoolValue(ini, "LichForm", "DebugLog", snapshot.lichFormDebugLog);
		GetBoolValue(ini, "WheelBehavior.ActionHotkeysBridge", "Enabled", snapshot.actionHotkeysBridgeEnabled);
		GetStringValue(ini, "WheelBehavior.ActionHotkeysBridge", "SourceIniPath", snapshot.actionHotkeysBridgeSourceIniPath);
		GetStringValue(ini, "WheelBehavior.ActionHotkeysBridge", "SourceIconsPath", snapshot.actionHotkeysBridgeSourceIconsPath);
		GetUInt32Value(ini, "WheelBehavior.ActionHotkeysBridge", "MirroredWheelCount", snapshot.actionHotkeysBridgeMirroredWheelCount);
		GetUInt32Value(ini, "WheelBehavior.ActionHotkeysBridge", "MaxMirrorSlots", snapshot.actionHotkeysBridgeMaxMirrorSlots);
		GetBoolValue(ini, "WheelBehavior.ActionHotkeysBridge", "AutoRefresh", snapshot.actionHotkeysBridgeAutoRefresh);
		GetUInt32Value(ini, "WheelBehavior.ActionHotkeysBridge", "RefreshDebounceMs", snapshot.actionHotkeysBridgeRefreshDebounceMs);
		GetUInt32Value(ini, "WheelBehavior.ActionHotkeysBridge", "DispatchCooldownMs", snapshot.actionHotkeysBridgeDispatchCooldownMs);
		GetBoolValue(ini, "WheelBehavior.ActionHotkeysBridge", "BlockConflictingWheelerHotkeys", snapshot.actionHotkeysBridgeBlockConflictingWheelerHotkeys);
		GetBoolValue(ini, "WheelBehavior.ActionHotkeysBridge", "MirrorSecondaryActivate", snapshot.actionHotkeysBridgeMirrorSecondaryActivate);
		GetBoolValue(ini, "WheelBehavior.ActionHotkeysBridge", "MirrorSpecialActivate", snapshot.actionHotkeysBridgeMirrorSpecialActivate);
		GetBoolValue(ini, "WheelBehavior.ActionHotkeysBridge", "DebugLog", snapshot.actionHotkeysBridgeDebugLog);

		GetBoolValue(ini, "Cooldowns", "Enabled", snapshot.cooldownsEnabled);
		GetBoolValue(ini, "Cooldowns", "ShowTimer", snapshot.cooldownsShowTimer);
		GetUInt32Value(ini, "Cooldowns", "OverlayColor", snapshot.cooldownsOverlayColor);
		GetFloatValue(ini, "Cooldowns", "ContentDimAlpha", snapshot.cooldownsContentDimAlpha);
		GetBoolValue(ini, "Cooldowns", "SelectedIndicatorEnabled", snapshot.cooldownsSelectedIndicatorEnabled);
		GetUInt32Value(ini, "Cooldowns", "SelectedIndicatorTintColor", snapshot.cooldownsSelectedIndicatorTintColor);
		GetFloatValue(ini, "Cooldowns", "CacheWindowSeconds", snapshot.cooldownsCacheWindowSeconds);

		{
			std::uint32_t tmpIndex = snapshot.cooldownsTimerTextFontIndex;
			if (!GetUInt32Value(ini, "Cooldowns.TimerText", "FontIndex", tmpIndex)) {
				float tmpFloat = static_cast<float>(tmpIndex);
				if (GetFloatValue(ini, "Cooldowns.TimerText", "FontIndex", tmpFloat)) {
					tmpIndex = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
				}
			}
			snapshot.cooldownsTimerTextFontIndex = tmpIndex;
		}
		GetFloatValue(ini, "Cooldowns.TimerText", "Size", snapshot.cooldownsTimerTextSize);
		GetUInt32Value(ini, "Cooldowns.TimerText", "Color", snapshot.cooldownsTimerTextColor);

		{
			float tmpContentDim = snapshot.cooldownsContentDimAlpha;
			if (!GetFloatValue(ini, "Cooldowns", "ContentDimAlpha", tmpContentDim)) {
				snapshot.cooldownsContentDimAlpha = ImGui::ColorConvertU32ToFloat4(snapshot.cooldownsOverlayColor).w;
			}
			std::uint32_t tmpTint = snapshot.cooldownsSelectedIndicatorTintColor;
			if (!GetUInt32Value(ini, "Cooldowns", "SelectedIndicatorTintColor", tmpTint)) {
				snapshot.cooldownsSelectedIndicatorTintColor = snapshot.cooldownsOverlayColor;
			}
		}

		GetBoolValue(ini, "Styling.HoverDelay", "Enabled", snapshot.hoverDelayEnabled);
		GetFloatValue(ini, "Styling.HoverDelay", "Radius", snapshot.hoverDelayRadius);
		GetFloatValue(ini, "Styling.HoverDelay", "RadiusOffset", snapshot.hoverDelayRadiusOffset);
		GetFloatValue(ini, "Styling.HoverDelay", "Thickness", snapshot.hoverDelayThickness);
		GetUInt32Value(ini, "Styling.HoverDelay", "Color", snapshot.hoverDelayColor);
		GetUInt32Value(ini, "Styling.HoverDelay", "BackgroundColor", snapshot.hoverDelayBackgroundColor);
		if (!GetUInt32Value(ini, "Styling.HoverDelay", "InstantSpellColor", snapshot.hoverDelayInstantSpellColor)) {
			snapshot.hoverDelayInstantSpellColor = snapshot.hoverDelayColor;
		}
		if (!GetUInt32Value(ini, "Styling.HoverDelay", "InstantSpellBackgroundColor", snapshot.hoverDelayInstantSpellBackgroundColor)) {
			snapshot.hoverDelayInstantSpellBackgroundColor = snapshot.hoverDelayBackgroundColor;
		}
		GetBoolValue(ini, "Styling.HoverDelay", "InstantSpellUseReskinAssets", snapshot.hoverDelayInstantSpellUseReskinAssets);
		GetBoolValue(ini, "Styling.HoverDelay", "InstantSpellAssetsEnabled", snapshot.hoverDelayInstantSpellUseReskinAssets);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAssetScale", snapshot.hoverDelayInstantSpellAssetScale);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAssetOffsetX", snapshot.hoverDelayInstantSpellAssetOffsetX);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAssetOffsetY", snapshot.hoverDelayInstantSpellAssetOffsetY);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAssetOpacity", snapshot.hoverDelayInstantSpellAssetOpacity);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellHandIndicatorScale", snapshot.hoverDelayInstantSpellHandIndicatorScale);
		LoadInstantSpellHandIndicatorOffsetsFromIni(
			ini,
			"Styling.HoverDelay",
			snapshot.hoverDelayInstantSpellHandIndicatorOffsetX,
			snapshot.hoverDelayInstantSpellHandIndicatorOffsetY,
			snapshot.hoverDelayInstantSpellHandIndicatorLeftOffsetX,
			snapshot.hoverDelayInstantSpellHandIndicatorLeftOffsetY,
			snapshot.hoverDelayInstantSpellHandIndicatorRightOffsetX,
			snapshot.hoverDelayInstantSpellHandIndicatorRightOffsetY,
			snapshot.hoverDelayInstantSpellHandIndicatorBothOffsetX,
			snapshot.hoverDelayInstantSpellHandIndicatorBothOffsetY);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellHandIndicatorOpacity", snapshot.hoverDelayInstantSpellHandIndicatorOpacity);
		GetBoolValue(ini, "Styling.HoverDelay", "InstantSpellUseAtlasAnimation", snapshot.hoverDelayInstantSpellUseAtlasAnimation);
		GetBoolValue(ini, "Styling.HoverDelay", "InstantSpellAtlasEnabled", snapshot.hoverDelayInstantSpellUseAtlasAnimation);
		{
			float tmpCols = static_cast<float>(snapshot.hoverDelayInstantSpellAtlasCols);
			if (GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAtlasCols", tmpCols)) {
				snapshot.hoverDelayInstantSpellAtlasCols = static_cast<std::uint32_t>((std::max)(1.0f, std::round(tmpCols)));
			}
		}
		{
			float tmpRows = static_cast<float>(snapshot.hoverDelayInstantSpellAtlasRows);
			if (GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAtlasRows", tmpRows)) {
				snapshot.hoverDelayInstantSpellAtlasRows = static_cast<std::uint32_t>((std::max)(1.0f, std::round(tmpRows)));
			}
		}
		{
			float tmpFrames = static_cast<float>(snapshot.hoverDelayInstantSpellAtlasFrameCount);
			if (GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAtlasFrameCount", tmpFrames)) {
				snapshot.hoverDelayInstantSpellAtlasFrameCount = static_cast<std::uint32_t>((std::max)(1.0f, std::round(tmpFrames)));
			}
		}
		snapshot.hoverDelayInstantSpellAssetScale = std::clamp(snapshot.hoverDelayInstantSpellAssetScale, 0.1f, 12.0f);
		snapshot.hoverDelayInstantSpellAssetOffsetX = std::clamp(snapshot.hoverDelayInstantSpellAssetOffsetX, -200.0f, 200.0f);
		snapshot.hoverDelayInstantSpellAssetOffsetY = std::clamp(snapshot.hoverDelayInstantSpellAssetOffsetY, -200.0f, 200.0f);
		snapshot.hoverDelayInstantSpellAssetOpacity = std::clamp(snapshot.hoverDelayInstantSpellAssetOpacity, 0.0f, 1.0f);
		snapshot.hoverDelayInstantSpellHandIndicatorScale = std::clamp(snapshot.hoverDelayInstantSpellHandIndicatorScale, 0.1f, 12.0f);
		ClampInstantSpellHandIndicatorOffsets(
			snapshot.hoverDelayInstantSpellHandIndicatorOffsetX,
			snapshot.hoverDelayInstantSpellHandIndicatorOffsetY,
			snapshot.hoverDelayInstantSpellHandIndicatorLeftOffsetX,
			snapshot.hoverDelayInstantSpellHandIndicatorLeftOffsetY,
			snapshot.hoverDelayInstantSpellHandIndicatorRightOffsetX,
			snapshot.hoverDelayInstantSpellHandIndicatorRightOffsetY,
			snapshot.hoverDelayInstantSpellHandIndicatorBothOffsetX,
			snapshot.hoverDelayInstantSpellHandIndicatorBothOffsetY);
		snapshot.hoverDelayInstantSpellHandIndicatorOpacity = std::clamp(snapshot.hoverDelayInstantSpellHandIndicatorOpacity, 0.0f, 1.0f);
		snapshot.hoverDelayInstantSpellAtlasCols = std::clamp(snapshot.hoverDelayInstantSpellAtlasCols, 1u, 64u);
		snapshot.hoverDelayInstantSpellAtlasRows = std::clamp(snapshot.hoverDelayInstantSpellAtlasRows, 1u, 64u);
		const std::uint32_t maxFrames = snapshot.hoverDelayInstantSpellAtlasCols * snapshot.hoverDelayInstantSpellAtlasRows;
		snapshot.hoverDelayInstantSpellAtlasFrameCount = std::clamp(snapshot.hoverDelayInstantSpellAtlasFrameCount, 1u, (std::max)(1u, maxFrames));

		// Sound settings
		GetBoolValue(ini, "Sounds", "EnableSounds", snapshot.soundsEnabled);
		GetStringValue(ini, "Sounds", "HoverSoundEditorID", snapshot.soundsHoverEditorID);
		GetStringValue(ini, "Sounds", "ActivateSoundEditorID", snapshot.soundsActivateEditorID);
		GetFloatValue(ini, "Sounds", "HoverSoundVolume", snapshot.soundsHoverVolume);
		GetFloatValue(ini, "Sounds", "ActivateSoundVolume", snapshot.soundsActivateVolume);

		// Shout stage sounds
		GetBoolValue(ini, "Sounds", "EnableShoutStageSounds", snapshot.enableShoutStageSounds);
		GetUInt32Value(ini, "Sounds", "ShoutStageSoundMode", snapshot.shoutStageSoundMode);
		GetStringValue(ini, "Sounds", "ShoutUISoundEditorID", snapshot.shoutUISoundEditorID);
		GetFloatValue(ini, "Sounds", "ShoutUIStageVolume1", snapshot.shoutUIStageVolume1);
		GetFloatValue(ini, "Sounds", "ShoutUIStageVolume2", snapshot.shoutUIStageVolume2);
		GetFloatValue(ini, "Sounds", "ShoutUIStageVolume3", snapshot.shoutUIStageVolume3);
		GetFloatValue(ini, "Sounds", "ShoutUIStagePitch1", snapshot.shoutUIStagePitch1);
		GetFloatValue(ini, "Sounds", "ShoutUIStagePitch2", snapshot.shoutUIStagePitch2);
		GetFloatValue(ini, "Sounds", "ShoutUIStagePitch3", snapshot.shoutUIStagePitch3);
		GetStringValue(ini, "Sounds", "ShoutWord1SoundEditorID", snapshot.shoutWord1SoundEditorID);
		GetStringValue(ini, "Sounds", "ShoutWord2SoundEditorID", snapshot.shoutWord2SoundEditorID);
		GetStringValue(ini, "Sounds", "ShoutWord3SoundEditorID", snapshot.shoutWord3SoundEditorID);

		// MainWheel debug settings
		GetBoolValue(ini, "MainWheel.Debug", "Enabled", snapshot.mainWheelDebugEnabled);
		GetBoolValue(ini, "MainWheel.Debug", "OverlayEnabled", snapshot.mainWheelDebugOverlayEnabled);
		GetBoolValue(ini, "MainWheel.Debug", "Verbose", snapshot.mainWheelDebugVerbose);
		GetUInt32Value(ini, "MainWheel.Debug", "RateLimitMs", snapshot.mainWheelDebugRateLimitMs);
		GetBoolValue(ini, "MainWheel.Debug", "LogOpenClose", snapshot.mainWheelDebugLogOpenClose);
		GetBoolValue(ini, "MainWheel.Debug", "LogConfig", snapshot.mainWheelDebugLogConfig);
		GetBoolValue(ini, "MainWheel.Debug", "LogInput", snapshot.mainWheelDebugLogInput);
		GetBoolValue(ini, "MainWheel.Debug", "LogScaling", snapshot.mainWheelDebugLogScaling);
		GetBoolValue(ini, "MainWheel.Debug", "LogClamp", snapshot.mainWheelDebugLogClamp);
		GetBoolValue(ini, "MainWheel.Debug", "LogIndicators", snapshot.mainWheelDebugLogIndicators);
		GetBoolValue(ini, "MainWheel.Debug", "LogReskinResolve", snapshot.mainWheelDebugLogReskinResolve);
		GetBoolValue(ini, "MainWheel.Debug", "LogAssets", snapshot.mainWheelDebugLogAssets);
		GetBoolValue(ini, "MainWheel.Debug", "LogPerf", snapshot.mainWheelDebugLogPerf);

		// MainWheel mouse hover settings
		GetFloatValue(ini, "MainWheel.Mouse", "CenterLockRadiusPx", snapshot.mainWheelCenterLockRadiusPx);
		GetFloatValue(ini, "MainWheel.Mouse", "JumpGuardRadiusPx", snapshot.mainWheelJumpGuardRadiusPx);
		GetFloatValue(ini, "MainWheel.Mouse", "HysteresisDegrees", snapshot.mainWheelHysteresisDegrees);
		GetBoolValue(ini, "MainWheel.Mouse", "CenterSlowdownEnabled", snapshot.mainWheelCenterSlowdownEnabled);
		GetFloatValue(ini, "MainWheel.Mouse", "GainCenter", snapshot.mainWheelGainCenter);
		GetFloatValue(ini, "MainWheel.Mouse", "GainOuter", snapshot.mainWheelGainOuter);
		GetFloatValue(ini, "MainWheel.Mouse", "CurvePower", snapshot.mainWheelCurvePower);
		GetFloatValue(ini, "MainWheel.Mouse", "InnerDeadZoneR", snapshot.mainWheelInnerDeadZoneR);
		GetFloatValue(ini, "MainWheel.Mouse", "InnerBlendZoneR", snapshot.mainWheelInnerBlendZoneR);
		GetFloatValue(ini, "MainWheel.Mouse", "MinStableSpeed", snapshot.mainWheelMinStableSpeed);
		GetFloatValue(ini, "MainWheel.Mouse", "VelocityHalfLifeMs", snapshot.mainWheelVelocityHalfLifeMs);
		GetFloatValue(ini, "MainWheel.Mouse", "StableDirHalfLifeMs", snapshot.mainWheelStableDirHalfLifeMs);
		GetFloatValue(ini, "MainWheel.Mouse", "IntentMinSpeed", snapshot.mainWheelIntentMinSpeed);
		GetFloatValue(ini, "MainWheel.Mouse", "IntentSustainSpeed", snapshot.mainWheelIntentSustainSpeed);
		GetFloatValue(ini, "MainWheel.Mouse", "IntentConfirmMs", snapshot.mainWheelIntentConfirmMs);
		GetFloatValue(ini, "MainWheel.Mouse", "IntentConeDeg", snapshot.mainWheelIntentConeDeg);
		GetFloatValue(ini, "MainWheel.Mouse", "IntentReleaseConeDeg", snapshot.mainWheelIntentReleaseConeDeg);
		GetFloatValue(ini, "MainWheel.Mouse", "IntentMinAngleDeg", snapshot.mainWheelIntentMinAngleDeg);
		GetFloatValue(ini, "MainWheel.Mouse", "IntentReleaseSpeed", snapshot.mainWheelIntentReleaseSpeed);
		GetFloatValue(ini, "MainWheel.Mouse", "IntentReleaseDwellMs", snapshot.mainWheelIntentReleaseDwellMs);
		GetFloatValue(ini, "MainWheel.Mouse", "IntentBias", snapshot.mainWheelIntentBias);
		GetFloatValue(ini, "MainWheel.Mouse", "IntentNeighborBias", snapshot.mainWheelIntentNeighborBias);
		GetFloatValue(ini, "MainWheel.Mouse", "ScoreWeightAngle", snapshot.mainWheelScoreWeightAngle);
		GetFloatValue(ini, "MainWheel.Mouse", "ScoreWeightMotion", snapshot.mainWheelScoreWeightMotion);
		GetFloatValue(ini, "MainWheel.Mouse", "ScoreWeightInertia", snapshot.mainWheelScoreWeightInertia);
		GetFloatValue(ini, "MainWheel.Mouse", "ScoreWeightStickiness", snapshot.mainWheelScoreWeightStickiness);
		GetFloatValue(ini, "MainWheel.Mouse", "SwitchConfidenceMargin", snapshot.mainWheelSwitchConfidenceMargin);
		GetFloatValue(ini, "MainWheel.Mouse", "SwitchDwellMs", snapshot.mainWheelSwitchDwellMs);
		{
			std::uint32_t temp = static_cast<std::uint32_t>(snapshot.mainWheelInnerDeadzoneMaxSlotDelta);
			if (GetUInt32Value(ini, "MainWheel.Mouse", "InnerDeadzoneMaxSlotDelta", temp)) {
				snapshot.mainWheelInnerDeadzoneMaxSlotDelta = static_cast<int>(temp);
			}
		}
		GetBoolValue(ini, "MainWheel.Mouse", "DebugHoverLog", snapshot.mainWheelDebugHoverLog);
		GetBoolValue(ini, "MainWheel.Mouse", "LogMouseFeatures", snapshot.mainWheelLogMouseFeatures);
		GetBoolValue(ini, "MainWheel.Mouse", "LogCandidateScores", snapshot.mainWheelLogCandidateScores);
		GetBoolValue(ini, "MainWheel.Mouse", "LogIntentState", snapshot.mainWheelLogIntentState);
		GetBoolValue(ini, "MainWheel.Mouse", "DrawDebugOverlay", snapshot.mainWheelDrawMouseDebugOverlay);

		// MainWheel mouse stabilization (legacy guard rails)
		GetBoolValue(ini, "MainWheel.MouseStabilization", "Enabled", snapshot.mainWheelMouseStabilizationEnabled);
		GetFloatValue(ini, "MainWheel.MouseStabilization", "CenterHoldRadius", snapshot.mainWheelMouseCenterHoldRadius);
		GetFloatValue(ini, "MainWheel.MouseStabilization", "JumpGuardRadius", snapshot.mainWheelMouseJumpGuardRadius);
		{
			std::uint32_t temp = static_cast<std::uint32_t>(snapshot.mainWheelMouseMaxJumpSlots);
			if (GetUInt32Value(ini, "MainWheel.MouseStabilization", "MaxJumpSlots", temp)) {
				snapshot.mainWheelMouseMaxJumpSlots = static_cast<int>(temp);
			}
		}
		GetBoolValue(ini, "MainWheel.MouseStabilization", "StepTowardEnabled", snapshot.mainWheelMouseStepTowardEnabled);
		GetFloatValue(ini, "MainWheel.MouseStabilization", "BoundaryMarginDeg", snapshot.mainWheelMouseBoundaryMarginDeg);
		GetBoolValue(ini, "MainWheel.MouseStabilization", "BoundaryMarginLowSpeedOnly", snapshot.mainWheelMouseBoundaryLowSpeedOnly);
		GetFloatValue(ini, "MainWheel.MouseStabilization", "LowSpeedThreshold", snapshot.mainWheelMouseLowSpeedThreshold);
		GetBoolValue(ini, "MainWheel.MouseStabilization", "MotionHintingEnabled", snapshot.mainWheelMouseMotionHintEnabled);
		GetFloatValue(ini, "MainWheel.MouseStabilization", "MotionHintingSpeedThreshold", snapshot.mainWheelMouseMotionHintSpeedThreshold);
		GetFloatValue(ini, "MainWheel.MouseStabilization", "MotionHintingBlend", snapshot.mainWheelMouseMotionHintBlend);

		// MainWheel low-end overrides
		GetBoolValue(ini, "MainWheel.LowEnd", "DisableBlurOnOpen", snapshot.mainWheelDisableBlurOnOpen);
		GetBoolValue(ini, "MainWheel.LowEnd", "PreferPrimitiveBackgrounds", snapshot.mainWheelPreferPrimitiveBackgrounds);

		// MainWheel indicator toggles
		GetBoolValue(ini, "MainWheel.Indicators", "ShowHandIndicator", snapshot.mainWheelShowHandIndicator);
		if (!GetUInt32Value(ini, "MainWheel.Indicators.Left", "Color", snapshot.mainWheelHandLeftColor)) {
			snapshot.mainWheelHandLeftColor = Config::Styling::Wheel::TextColor;
		}
		GetFloatValue(ini, "MainWheel.Indicators.Left", "Opacity", snapshot.mainWheelHandLeftOpacity);
		GetFloatValue(ini, "MainWheel.Indicators.Left", "SizeScale", snapshot.mainWheelHandLeftSizeScale);
		GetFloatValue(ini, "MainWheel.Indicators.Left", "Thickness", snapshot.mainWheelHandLeftThickness);
		GetFloatValue(ini, "MainWheel.Indicators.Left", "OffsetX", snapshot.mainWheelHandLeftOffsetX);
		GetFloatValue(ini, "MainWheel.Indicators.Left", "OffsetY", snapshot.mainWheelHandLeftOffsetY);
		GetFloatValue(ini, "MainWheel.Indicators.Left", "SlotLeftOffsetX", snapshot.mainWheelHandLeftSlotLeftOffsetX);
		GetFloatValue(ini, "MainWheel.Indicators.Left", "SlotLeftOffsetY", snapshot.mainWheelHandLeftSlotLeftOffsetY);
		GetFloatValue(ini, "MainWheel.Indicators.Left", "WheelLeftOffsetX", snapshot.mainWheelHandLeftSlotLeftOffsetX);
		GetFloatValue(ini, "MainWheel.Indicators.Left", "WheelLeftOffsetY", snapshot.mainWheelHandLeftSlotLeftOffsetY);
		GetFloatValue(ini, "MainWheel.Indicators.Left", "SlotRightOffsetX", snapshot.mainWheelHandLeftSlotRightOffsetX);
		GetFloatValue(ini, "MainWheel.Indicators.Left", "SlotRightOffsetY", snapshot.mainWheelHandLeftSlotRightOffsetY);
		GetFloatValue(ini, "MainWheel.Indicators.Left", "WheelRightOffsetX", snapshot.mainWheelHandLeftSlotRightOffsetX);
		GetFloatValue(ini, "MainWheel.Indicators.Left", "WheelRightOffsetY", snapshot.mainWheelHandLeftSlotRightOffsetY);
		GetStringValue(ini, "MainWheel.Indicators.Left", "AssetPath", snapshot.mainWheelHandLeftAssetPath);
		if (!GetUInt32Value(ini, "MainWheel.Indicators.Right", "Color", snapshot.mainWheelHandRightColor)) {
			snapshot.mainWheelHandRightColor = Config::Styling::Wheel::TextColor;
		}
		GetFloatValue(ini, "MainWheel.Indicators.Right", "Opacity", snapshot.mainWheelHandRightOpacity);
		GetFloatValue(ini, "MainWheel.Indicators.Right", "SizeScale", snapshot.mainWheelHandRightSizeScale);
		GetFloatValue(ini, "MainWheel.Indicators.Right", "Thickness", snapshot.mainWheelHandRightThickness);
		GetFloatValue(ini, "MainWheel.Indicators.Right", "OffsetX", snapshot.mainWheelHandRightOffsetX);
		GetFloatValue(ini, "MainWheel.Indicators.Right", "OffsetY", snapshot.mainWheelHandRightOffsetY);
		GetFloatValue(ini, "MainWheel.Indicators.Right", "SlotLeftOffsetX", snapshot.mainWheelHandRightSlotLeftOffsetX);
		GetFloatValue(ini, "MainWheel.Indicators.Right", "SlotLeftOffsetY", snapshot.mainWheelHandRightSlotLeftOffsetY);
		GetFloatValue(ini, "MainWheel.Indicators.Right", "WheelLeftOffsetX", snapshot.mainWheelHandRightSlotLeftOffsetX);
		GetFloatValue(ini, "MainWheel.Indicators.Right", "WheelLeftOffsetY", snapshot.mainWheelHandRightSlotLeftOffsetY);
		GetFloatValue(ini, "MainWheel.Indicators.Right", "SlotRightOffsetX", snapshot.mainWheelHandRightSlotRightOffsetX);
		GetFloatValue(ini, "MainWheel.Indicators.Right", "SlotRightOffsetY", snapshot.mainWheelHandRightSlotRightOffsetY);
		GetFloatValue(ini, "MainWheel.Indicators.Right", "WheelRightOffsetX", snapshot.mainWheelHandRightSlotRightOffsetX);
		GetFloatValue(ini, "MainWheel.Indicators.Right", "WheelRightOffsetY", snapshot.mainWheelHandRightSlotRightOffsetY);
		GetStringValue(ini, "MainWheel.Indicators.Right", "AssetPath", snapshot.mainWheelHandRightAssetPath);
		snapshot.mainWheelHandLeftOpacity = std::clamp(snapshot.mainWheelHandLeftOpacity, 0.0f, 1.0f);
		snapshot.mainWheelHandLeftSizeScale = std::clamp(snapshot.mainWheelHandLeftSizeScale, 0.5f, 2.0f);
		snapshot.mainWheelHandLeftThickness = std::clamp(snapshot.mainWheelHandLeftThickness, 0.0f, 3.0f);
		snapshot.mainWheelHandLeftOffsetX = std::clamp(snapshot.mainWheelHandLeftOffsetX, -200.0f, 200.0f);
		snapshot.mainWheelHandLeftOffsetY = std::clamp(snapshot.mainWheelHandLeftOffsetY, -200.0f, 200.0f);
		snapshot.mainWheelHandLeftSlotLeftOffsetX = std::clamp(snapshot.mainWheelHandLeftSlotLeftOffsetX, -200.0f, 200.0f);
		snapshot.mainWheelHandLeftSlotLeftOffsetY = std::clamp(snapshot.mainWheelHandLeftSlotLeftOffsetY, -200.0f, 200.0f);
		snapshot.mainWheelHandLeftSlotRightOffsetX = std::clamp(snapshot.mainWheelHandLeftSlotRightOffsetX, -200.0f, 200.0f);
		snapshot.mainWheelHandLeftSlotRightOffsetY = std::clamp(snapshot.mainWheelHandLeftSlotRightOffsetY, -200.0f, 200.0f);
		snapshot.mainWheelHandRightOpacity = std::clamp(snapshot.mainWheelHandRightOpacity, 0.0f, 1.0f);
		snapshot.mainWheelHandRightSizeScale = std::clamp(snapshot.mainWheelHandRightSizeScale, 0.5f, 2.0f);
		snapshot.mainWheelHandRightThickness = std::clamp(snapshot.mainWheelHandRightThickness, 0.0f, 3.0f);
		snapshot.mainWheelHandRightOffsetX = std::clamp(snapshot.mainWheelHandRightOffsetX, -200.0f, 200.0f);
		snapshot.mainWheelHandRightOffsetY = std::clamp(snapshot.mainWheelHandRightOffsetY, -200.0f, 200.0f);
		snapshot.mainWheelHandRightSlotLeftOffsetX = std::clamp(snapshot.mainWheelHandRightSlotLeftOffsetX, -200.0f, 200.0f);
		snapshot.mainWheelHandRightSlotLeftOffsetY = std::clamp(snapshot.mainWheelHandRightSlotLeftOffsetY, -200.0f, 200.0f);
		snapshot.mainWheelHandRightSlotRightOffsetX = std::clamp(snapshot.mainWheelHandRightSlotRightOffsetX, -200.0f, 200.0f);
		snapshot.mainWheelHandRightSlotRightOffsetY = std::clamp(snapshot.mainWheelHandRightSlotRightOffsetY, -200.0f, 200.0f);

		// Backward compatibility with older keys (legacy naming).
		auto hasKey = [&](const char* section, const char* key) {
			return ini.GetValue(section, key, nullptr) != nullptr;
		};

		const bool hasReleaseToUse = hasKey("WheelBehavior", "ReleaseToUse") || hasKey("InstantUse", "ReleaseToUse");
		if (!hasReleaseToUse) {
			bool activateOnClose = false;
			if (GetBoolValue(ini, "WheelBehavior", "ActivateOnClose", activateOnClose) ||
			    GetBoolValue(ini, "InstantUse", "ActivateOnClose", activateOnClose)) {
				snapshot.releaseToUse = activateOnClose;
			}
		}

		const bool hasRTUAlchemy = hasKey("WheelBehavior", "RTUAlchemy") || hasKey("InstantUse", "RTUAlchemy");
		if (!hasRTUAlchemy) {
			bool oldAlchemy = true;
			if (GetBoolValue(ini, "WheelBehavior", "Alchemy", oldAlchemy) ||
			    GetBoolValue(ini, "InstantUse", "Alchemy", oldAlchemy)) {
				snapshot.rtuAlchemy = oldAlchemy;
			}
		}

		const bool hasRTUSpell = hasKey("WheelBehavior", "RTUSpell") || hasKey("InstantUse", "RTUSpell");
		if (!hasRTUSpell) {
			bool oldSpell = true;
			if (GetBoolValue(ini, "WheelBehavior", "Spell", oldSpell) ||
			    GetBoolValue(ini, "InstantUse", "Spell", oldSpell)) {
				snapshot.rtuSpell = oldSpell;
			}
		}

		const bool hasRTUShout = hasKey("WheelBehavior", "RTUShout") || hasKey("InstantUse", "RTUShout");
		if (!hasRTUShout) {
			bool oldShout = true;
			if (GetBoolValue(ini, "WheelBehavior", "Shout", oldShout) ||
			    GetBoolValue(ini, "InstantUse", "Shout", oldShout)) {
				snapshot.rtuShout = oldShout;
			}
		}

		const bool hasInstantSpell = hasKey("WheelBehavior", "InstantSpell") || hasKey("InstantUse", "InstantSpell");
		if (!hasInstantSpell) {
			std::uint32_t oldSpellMode = 0;
			if (GetUInt32Value(ini, "WheelBehavior", "SpellMode", oldSpellMode) ||
			    GetUInt32Value(ini, "InstantUse", "SpellMode", oldSpellMode)) {
				snapshot.instantSpell = oldSpellMode == 2;
			} else {
				bool oldSpellInstantCastNonInstant = false;
				if (GetBoolValue(ini, "WheelBehavior", "SpellInstantCastNonInstant", oldSpellInstantCastNonInstant) ||
				    GetBoolValue(ini, "InstantUse", "SpellInstantCastNonInstant", oldSpellInstantCastNonInstant)) {
					snapshot.instantSpell = oldSpellInstantCastNonInstant;
				}
			}
		}

		const bool hasInstantPowers = hasKey("WheelBehavior", "InstantPowers") || hasKey("InstantUse", "InstantPowers");
		if (!hasInstantPowers) {
			snapshot.instantPowers = snapshot.instantSpell;
		}

		return snapshot;
	}

	static void ApplyWheelBehaviorSnapshotToConfig(const WheelBehaviorSnapshot& snapshot)
	{
		Config::WheelBehavior::ReleaseToUse = snapshot.releaseToUse;
		Config::WheelBehavior::CloseWheelAfterUse = snapshot.closeWheelAfterUse;
		Config::WheelBehavior::RTUAlchemy = snapshot.rtuAlchemy;
		Config::WheelBehavior::RTUSpell = snapshot.rtuSpell;
		Config::WheelBehavior::RTUShout = snapshot.rtuShout;
		Config::WheelBehavior::RTUSmartAssignEnabled = snapshot.rtuSmartAssignEnabled;
		Config::WheelBehavior::RTUSmartAssignTargetHands = snapshot.rtuSmartAssignTargetHands;
		Config::WheelBehavior::RTUSmartAssignCategoryMask = snapshot.rtuSmartAssignCategoryMask;
		Config::WheelBehavior::RTUSmartAssignBothEmptyPriority = snapshot.rtuSmartAssignBothEmptyPriority;
		Config::WheelBehavior::RTUSmartAssignOverwriteMode = snapshot.rtuSmartAssignOverwriteMode;
		Config::WheelBehavior::RTUSmartAssignDebugLog = snapshot.rtuSmartAssignDebugLog;
		Config::WheelBehavior::HoverActivateDelaySeconds = snapshot.hoverActivateDelaySeconds;
		Config::WheelBehavior::AutoDrawOnUse = snapshot.autoDrawOnUse;
		Config::WheelBehavior::InstantSpell = snapshot.instantSpell;
		Config::WheelBehavior::InstantSpellUseDirectCast = snapshot.instantSpellUseDirectCast;
		Config::WheelBehavior::RTUAutoInstantSpell = snapshot.rtuAutoInstantSpell;
		Config::WheelBehavior::InstantPowers = snapshot.instantPowers;
		Config::WheelBehavior::InstantSpellConcentrationMode = snapshot.instantSpellConcentrationMode;
		Config::WheelBehavior::InstantSpellConcentrationMaxSeconds = snapshot.instantSpellConcentrationMaxSeconds;
		Config::WheelBehavior::InstantSpellDebugLog = snapshot.instantSpellDebugLog;
		Config::WheelBehavior::ShoutPipelineDebug = snapshot.shoutPipelineDebug;
		Config::WheelBehavior::InstantShout = snapshot.instantShout;
		Config::WheelBehavior::RTUAutoInstantShout = snapshot.rtuAutoInstantShout;
		Config::WheelBehavior::ShoutWord2Threshold = snapshot.shoutWord2Threshold;
		Config::WheelBehavior::ShoutWord3Threshold = snapshot.shoutWord3Threshold;
		Config::WheelBehavior::ShoutHoldSecsWord1 = snapshot.shoutHoldSecsWord1;
		Config::WheelBehavior::ShoutHoldSecsWord2 = snapshot.shoutHoldSecsWord2;
		Config::WheelBehavior::ShoutHoldSecsWord3 = snapshot.shoutHoldSecsWord3;
		Config::WheelBehavior::ShoutIgnoreRTUDelay = snapshot.shoutIgnoreRTUDelay;
		Config::WheelBehavior::ShoutStageFillSecs = snapshot.shoutStageFillSecs;
		Config::WheelBehavior::ShoutStageHoldSecs1 = snapshot.shoutStageHoldSecs1;
		Config::WheelBehavior::ShoutStageHoldSecs2 = snapshot.shoutStageHoldSecs2;
		Config::WheelBehavior::ClearDepletedConsumables = snapshot.clearDepletedConsumables;
		Config::WheelBehavior::ScriptedMiscDispatchModeValue = std::clamp(snapshot.scriptedMiscDispatchMode, 0u, 4u);
		Config::WheelBehavior::BookReadCompat::Mode = std::clamp(snapshot.bookReadCompatMode, 0u, 2u);
		Config::WheelBehavior::BookReadCompat::OnReadFormIDs = snapshot.bookReadCompatOnReadFormIDs;
		Config::WheelBehavior::BookReadCompat::OnReadPlugins = snapshot.bookReadCompatOnReadPlugins;
		Config::WheelBehavior::BookReadCompat::OnReadNameTokens = snapshot.bookReadCompatOnReadNameTokens;
		Config::WheelBehavior::BookReadCompat::DebugLog = snapshot.bookReadCompatDebugLog;
		Config::WheelBehavior::KeepMissing::Enabled = snapshot.keepMissingEnabled;
		Config::WheelBehavior::KeepMissing::KeepConsumables = snapshot.keepMissingConsumables;
		Config::WheelBehavior::KeepMissing::KeepGears = snapshot.keepMissingGears;
		Config::WheelBehavior::KeepMissing::KeepThrowableMods = snapshot.keepMissingThrowableMods;
	Config::WheelBehavior::HandMemory::Enabled = snapshot.handMemoryEnabled;
	Config::WheelBehavior::HandMemory::DebugLog = snapshot.handMemoryDebugLog;
	Config::WheelBehavior::HandMemory::RestoreDelaySeconds = snapshot.handMemoryRestoreDelaySeconds;
	Config::WheelBehavior::HandMemory::RestoreWindowSeconds = snapshot.handMemoryRestoreWindowSeconds;
	Config::WheelBehavior::HandMemory::RestoreLeftIfEmpty = snapshot.handMemoryRestoreLeftIfEmpty;
	Config::WheelBehavior::HandMemory::RestoreRightIfEmpty = snapshot.handMemoryRestoreRightIfEmpty;
		Config::WheelBehavior::RTUAntiSlipEnabled = snapshot.rtuAntiSlipEnabled;
		Config::WheelBehavior::RTUAntiSlipStrength = snapshot.rtuAntiSlipStrength;
		Config::WheelBehavior::LootMenuOverride = snapshot.lootMenuOverride;
		Config::WheelBehavior::HeavyListCompatibilityMode = snapshot.heavyListCompatibilityMode;
		Config::WheelBehavior::HeavyListCompatibilitySettleMs = snapshot.heavyListCompatibilitySettleMs;
		Config::WheelBehavior::MutableInventoryHooks = snapshot.mutableInventoryHooks;
		Config::WheelBehavior::TransformWheels::Enabled = snapshot.transformWheelsEnabled;
		Config::WheelBehavior::TransformWheels::Mode = snapshot.transformWheelsMode;
		Config::WheelBehavior::TransformWheels::RestorePreviousWheel = snapshot.transformWheelsRestorePrevious;
		Config::WheelBehavior::TransformWheels::IncludeShouts = snapshot.transformWheelsIncludeShouts;
		Config::WheelBehavior::TransformWheels::RegenerateThrottleMs = snapshot.transformWheelsRegenerateThrottleMs;
		Config::WheelBehavior::TransformWheels::RetryWindowMs = snapshot.transformWheelsRetryWindowMs;
		Config::WheelBehavior::TransformWheels::StableTicks = snapshot.transformWheelsStableTicks;
		Config::WheelBehavior::TransformWheels::MinEntries = snapshot.transformWheelsMinEntries;
		Config::WheelBehavior::TransformWheels::CacheHumanSnapshot = snapshot.transformWheelsCacheHumanSnapshot;
		Config::WheelBehavior::TransformWheels::PersistGeneratedWheels = snapshot.transformWheelsPersistGeneratedWheels;
		Config::WheelBehavior::TransformWheels::UpdateOnlyOnChange = snapshot.transformWheelsUpdateOnlyOnChange;
		Config::WheelBehavior::TransformWheels::DebugLog = snapshot.transformWheelsDebugLog;
		Config::WheelBehavior::TransformWheels::GenericEnabled = snapshot.transformWheelsGenericEnabled;
		Config::WheelBehavior::TransformWheels::GenericStateID = snapshot.transformWheelsGenericStateID;
		Config::WheelBehavior::TransformWheels::GenericWheelID = snapshot.transformWheelsGenericWheelID;
		Config::WheelBehavior::TransformWheels::GenericRaceEditorIDContains = snapshot.transformWheelsGenericRaceEditorIDContains;
		Config::WheelBehavior::TransformWheels::GenericRaceKeywords = snapshot.transformWheelsGenericRaceKeywords;
		Config::WheelBehavior::TransformWheels::GenericRaceFormIDs = snapshot.transformWheelsGenericRaceFormIDs;
		Config::WheelBehavior::TransformWheels::PrecedenceOrder = snapshot.transformWheelsPrecedenceOrder;
		Config::WheelBehavior::TransformWheels::WerewolfAllowBaseWheel = snapshot.werewolfAllowBaseWheel;
		Config::WheelBehavior::TransformWheels::VampireLordAllowBaseWheel = snapshot.vampireLordAllowBaseWheel;
		Config::WheelBehavior::TransformWheels::WerewolfAllowBaseWheelSpells = snapshot.werewolfAllowBaseWheelSpells;
		Config::WheelBehavior::TransformWheels::WerewolfAllowBaseWheelShouts = snapshot.werewolfAllowBaseWheelShouts;
		Config::WheelBehavior::TransformWheels::WerewolfForm::Enabled = snapshot.werewolfFormEnabled;
		Config::WheelBehavior::TransformWheels::WerewolfForm::PopulateMode = snapshot.werewolfFormPopulateMode;
		Config::WheelBehavior::TransformWheels::WerewolfForm::RaceEditorIDContains = snapshot.werewolfFormRaceEditorIDContains;
		Config::WheelBehavior::TransformWheels::WerewolfForm::RaceKeywords = snapshot.werewolfFormRaceKeywords;
		Config::WheelBehavior::TransformWheels::WerewolfForm::RaceFormIDs = snapshot.werewolfFormRaceFormIDs;
		Config::WheelBehavior::TransformWheels::WerewolfForm::SpellTokens = snapshot.werewolfFormSpellTokens;
		Config::WheelBehavior::TransformWheels::WerewolfForm::ExitSpellTokens = snapshot.werewolfFormExitSpellTokens;
		Config::WheelBehavior::TransformWheels::WerewolfForm::AdditionalSpellFormIDs = snapshot.werewolfFormAdditionalSpellFormIDs;
		Config::WheelBehavior::TransformWheels::WerewolfForm::DebugLog = snapshot.werewolfFormDebugLog;
		Config::WheelBehavior::TransformWheels::VampireLordForm::Enabled = snapshot.vampireLordFormEnabled;
		Config::WheelBehavior::TransformWheels::VampireLordForm::PopulateMode = snapshot.vampireLordFormPopulateMode;
		Config::WheelBehavior::TransformWheels::VampireLordForm::HideTransformSpell = snapshot.vampireLordFormHideTransformSpell;
		Config::WheelBehavior::TransformWheels::VampireLordForm::HideForcedRightHandSpells = snapshot.vampireLordFormHideForcedRightHandSpells;
		Config::WheelBehavior::TransformWheels::VampireLordForm::BlockHiddenSpellActivation = snapshot.vampireLordFormBlockHiddenSpellActivation;
		Config::WheelBehavior::TransformWheels::VampireLordForm::BlockRegularSpellsInMeleeMode = snapshot.vampireLordFormBlockRegularSpellsInMeleeMode;
		Config::WheelBehavior::TransformWheels::VampireLordForm::RaceEditorIDContains = snapshot.vampireLordFormRaceEditorIDContains;
		Config::WheelBehavior::TransformWheels::VampireLordForm::RaceKeywords = snapshot.vampireLordFormRaceKeywords;
		Config::WheelBehavior::TransformWheels::VampireLordForm::RaceFormIDs = snapshot.vampireLordFormRaceFormIDs;
		Config::WheelBehavior::TransformWheels::VampireLordForm::SpellTokens = snapshot.vampireLordFormSpellTokens;
		Config::WheelBehavior::TransformWheels::VampireLordForm::ExitSpellTokens = snapshot.vampireLordFormExitSpellTokens;
		Config::WheelBehavior::TransformWheels::VampireLordForm::AdditionalSpellFormIDs = snapshot.vampireLordFormAdditionalSpellFormIDs;
		Config::WheelBehavior::TransformWheels::VampireLordForm::HiddenSpellFormIDs = snapshot.vampireLordFormHiddenSpellFormIDs;
		Config::WheelBehavior::TransformWheels::VampireLordForm::HiddenSpellTokens = snapshot.vampireLordFormHiddenSpellTokens;
		Config::WheelBehavior::TransformWheels::VampireLordForm::DebugLog = snapshot.vampireLordFormDebugLog;
		Config::WheelBehavior::TransformWheels::LichForm::Enabled = snapshot.lichFormEnabled;
		Config::WheelBehavior::TransformWheels::LichForm::Mode = snapshot.lichFormMode;
		Config::WheelBehavior::TransformWheels::LichForm::PopulateMode = snapshot.lichFormPopulateMode;
		Config::WheelBehavior::TransformWheels::LichForm::AllowBaseWheel = snapshot.lichFormAllowBaseWheel;
		Config::WheelBehavior::TransformWheels::LichForm::TransformGuard = snapshot.lichFormTransformGuard;
		Config::WheelBehavior::TransformWheels::LichForm::BlockBoundSpells = snapshot.lichFormBlockBoundSpells;
		Config::WheelBehavior::TransformWheels::LichForm::HideWeapons = snapshot.lichFormHideWeapons;
		Config::WheelBehavior::TransformWheels::LichForm::HideGear = snapshot.lichFormHideGear;
		Config::WheelBehavior::TransformWheels::LichForm::BlockStaffSwapping = snapshot.lichFormBlockStaffSwapping;
		Config::WheelBehavior::TransformWheels::LichForm::SuppressDirectCast = snapshot.lichFormSuppressDirectCast;
		Config::WheelBehavior::TransformWheels::LichForm::RaceEditorIDContains = snapshot.lichFormRaceEditorIDContains;
		Config::WheelBehavior::TransformWheels::LichForm::RaceKeywords = snapshot.lichFormRaceKeywords;
		Config::WheelBehavior::TransformWheels::LichForm::RaceFormIDs = snapshot.lichFormRaceFormIDs;
		Config::WheelBehavior::TransformWheels::LichForm::SpellTokens = snapshot.lichFormSpellTokens;
		Config::WheelBehavior::TransformWheels::LichForm::ExitSpellTokens = snapshot.lichFormExitSpellTokens;
		Config::WheelBehavior::TransformWheels::LichForm::AdditionalSpellFormIDs = snapshot.lichFormAdditionalSpellFormIDs;
		Config::WheelBehavior::TransformWheels::LichForm::DebugLog = snapshot.lichFormDebugLog;
		Config::Cooldowns::Enabled = snapshot.cooldownsEnabled;
		Config::Cooldowns::ShowTimer = snapshot.cooldownsShowTimer;
		Config::Cooldowns::ContentDimAlpha = snapshot.cooldownsContentDimAlpha;
		Config::Cooldowns::SelectedIndicatorEnabled = snapshot.cooldownsSelectedIndicatorEnabled;
		Config::Cooldowns::OverlayColor = snapshot.cooldownsOverlayColor;
		Config::Cooldowns::SelectedIndicatorTintColor = snapshot.cooldownsSelectedIndicatorTintColor;
		Config::Cooldowns::CacheWindowSeconds = snapshot.cooldownsCacheWindowSeconds;
		Config::Cooldowns::TimerText::FontIndex = snapshot.cooldownsTimerTextFontIndex;
		Config::Cooldowns::TimerText::Size = snapshot.cooldownsTimerTextSize;
		Config::Cooldowns::TimerText::Color = snapshot.cooldownsTimerTextColor;

		Config::Styling::HoverDelay::Enabled = snapshot.hoverDelayEnabled;
		Config::Styling::HoverDelay::Radius = snapshot.hoverDelayRadius;
		Config::Styling::HoverDelay::RadiusOffset = snapshot.hoverDelayRadiusOffset;
		Config::Styling::HoverDelay::Thickness = snapshot.hoverDelayThickness;
		Config::Styling::HoverDelay::Color = snapshot.hoverDelayColor;
		Config::Styling::HoverDelay::BackgroundColor = snapshot.hoverDelayBackgroundColor;
		Config::Styling::HoverDelay::InstantSpellColor = snapshot.hoverDelayInstantSpellColor;
		Config::Styling::HoverDelay::InstantSpellBackgroundColor = snapshot.hoverDelayInstantSpellBackgroundColor;
		Config::Styling::HoverDelay::InstantSpellUseReskinAssets = snapshot.hoverDelayInstantSpellUseReskinAssets;
		Config::Styling::HoverDelay::InstantSpellAssetScale = std::clamp(snapshot.hoverDelayInstantSpellAssetScale, 0.1f, 12.0f);
		Config::Styling::HoverDelay::InstantSpellAssetOffsetX = std::clamp(snapshot.hoverDelayInstantSpellAssetOffsetX, -200.0f, 200.0f);
		Config::Styling::HoverDelay::InstantSpellAssetOffsetY = std::clamp(snapshot.hoverDelayInstantSpellAssetOffsetY, -200.0f, 200.0f);
		Config::Styling::HoverDelay::InstantSpellAssetOpacity = std::clamp(snapshot.hoverDelayInstantSpellAssetOpacity, 0.0f, 1.0f);
		Config::Styling::HoverDelay::InstantSpellHandIndicatorScale = std::clamp(snapshot.hoverDelayInstantSpellHandIndicatorScale, 0.1f, 12.0f);
		Config::Styling::HoverDelay::InstantSpellHandIndicatorOffsetX = std::clamp(snapshot.hoverDelayInstantSpellHandIndicatorOffsetX, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantSpellHandIndicatorOffsetY = std::clamp(snapshot.hoverDelayInstantSpellHandIndicatorOffsetY, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetX = std::clamp(snapshot.hoverDelayInstantSpellHandIndicatorLeftOffsetX, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetY = std::clamp(snapshot.hoverDelayInstantSpellHandIndicatorLeftOffsetY, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetX = std::clamp(snapshot.hoverDelayInstantSpellHandIndicatorRightOffsetX, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetY = std::clamp(snapshot.hoverDelayInstantSpellHandIndicatorRightOffsetY, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetX = std::clamp(snapshot.hoverDelayInstantSpellHandIndicatorBothOffsetX, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetY = std::clamp(snapshot.hoverDelayInstantSpellHandIndicatorBothOffsetY, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantSpellHandIndicatorOpacity = std::clamp(snapshot.hoverDelayInstantSpellHandIndicatorOpacity, 0.0f, 1.0f);
		Config::Styling::HoverDelay::InstantSpellUseAtlasAnimation = snapshot.hoverDelayInstantSpellUseAtlasAnimation;
		Config::Styling::HoverDelay::InstantSpellAtlasCols = std::clamp(snapshot.hoverDelayInstantSpellAtlasCols, 1u, 64u);
		Config::Styling::HoverDelay::InstantSpellAtlasRows = std::clamp(snapshot.hoverDelayInstantSpellAtlasRows, 1u, 64u);
		{
			const std::uint32_t maxFrames = Config::Styling::HoverDelay::InstantSpellAtlasCols * Config::Styling::HoverDelay::InstantSpellAtlasRows;
			Config::Styling::HoverDelay::InstantSpellAtlasFrameCount = std::clamp(snapshot.hoverDelayInstantSpellAtlasFrameCount, 1u, (std::max)(1u, maxFrames));
		}
		ApplyInstantSpellIndicatorAssetPathHardcoded();

		Config::Sounds::EnableSounds = snapshot.soundsEnabled;
		Config::Sounds::HoverSoundEditorID = snapshot.soundsHoverEditorID;
		Config::Sounds::ActivateSoundEditorID = snapshot.soundsActivateEditorID;
		Config::Sounds::HoverSoundVolume = snapshot.soundsHoverVolume;
		Config::Sounds::ActivateSoundVolume = snapshot.soundsActivateVolume;

		Config::Sounds::EnableShoutStageSounds = snapshot.enableShoutStageSounds;
		Config::Sounds::ShoutStageSoundMode = snapshot.shoutStageSoundMode;
		Config::Sounds::ShoutUISoundEditorID = snapshot.shoutUISoundEditorID;
		Config::Sounds::ShoutUIStageVolume1 = snapshot.shoutUIStageVolume1;
		Config::Sounds::ShoutUIStageVolume2 = snapshot.shoutUIStageVolume2;
		Config::Sounds::ShoutUIStageVolume3 = snapshot.shoutUIStageVolume3;
		Config::Sounds::ShoutUIStagePitch1 = snapshot.shoutUIStagePitch1;
		Config::Sounds::ShoutUIStagePitch2 = snapshot.shoutUIStagePitch2;
		Config::Sounds::ShoutUIStagePitch3 = snapshot.shoutUIStagePitch3;
		Config::Sounds::ShoutWord1SoundEditorID = snapshot.shoutWord1SoundEditorID;
		Config::Sounds::ShoutWord2SoundEditorID = snapshot.shoutWord2SoundEditorID;
		Config::Sounds::ShoutWord3SoundEditorID = snapshot.shoutWord3SoundEditorID;

		Config::MainWheel::Debug::Enabled = snapshot.mainWheelDebugEnabled;
		Config::MainWheel::Debug::OverlayEnabled = snapshot.mainWheelDebugOverlayEnabled;
		Config::MainWheel::Debug::Verbose = snapshot.mainWheelDebugVerbose;
		Config::MainWheel::Debug::RateLimitMs = snapshot.mainWheelDebugRateLimitMs;
		Config::MainWheel::Debug::LogOpenClose = snapshot.mainWheelDebugLogOpenClose;
		Config::MainWheel::Debug::LogConfig = snapshot.mainWheelDebugLogConfig;
		Config::MainWheel::Debug::LogInput = snapshot.mainWheelDebugLogInput;
		Config::MainWheel::Debug::LogScaling = snapshot.mainWheelDebugLogScaling;
		Config::MainWheel::Debug::LogClamp = snapshot.mainWheelDebugLogClamp;
		Config::MainWheel::Debug::LogIndicators = snapshot.mainWheelDebugLogIndicators;
		Config::MainWheel::Debug::LogReskinResolve = snapshot.mainWheelDebugLogReskinResolve;
		Config::MainWheel::Debug::LogAssets = snapshot.mainWheelDebugLogAssets;
		Config::MainWheel::Debug::LogPerf = snapshot.mainWheelDebugLogPerf;

		Config::MainWheel::Mouse::CenterLockRadiusPx = snapshot.mainWheelCenterLockRadiusPx;
		Config::MainWheel::Mouse::JumpGuardRadiusPx = snapshot.mainWheelJumpGuardRadiusPx;
		Config::MainWheel::Mouse::HysteresisDegrees = snapshot.mainWheelHysteresisDegrees;
		Config::MainWheel::Mouse::CenterSlowdownEnabled = snapshot.mainWheelCenterSlowdownEnabled;
		Config::MainWheel::Mouse::GainCenter = snapshot.mainWheelGainCenter;
		Config::MainWheel::Mouse::GainOuter = snapshot.mainWheelGainOuter;
		Config::MainWheel::Mouse::CurvePower = snapshot.mainWheelCurvePower;
		Config::MainWheel::Mouse::InnerDeadZoneR = snapshot.mainWheelInnerDeadZoneR;
		Config::MainWheel::Mouse::InnerBlendZoneR = snapshot.mainWheelInnerBlendZoneR;
		Config::MainWheel::Mouse::MinStableSpeed = snapshot.mainWheelMinStableSpeed;
		Config::MainWheel::Mouse::VelocityHalfLifeMs = snapshot.mainWheelVelocityHalfLifeMs;
		Config::MainWheel::Mouse::StableDirHalfLifeMs = snapshot.mainWheelStableDirHalfLifeMs;
		Config::MainWheel::Mouse::IntentMinSpeed = snapshot.mainWheelIntentMinSpeed;
		Config::MainWheel::Mouse::IntentSustainSpeed = snapshot.mainWheelIntentSustainSpeed;
		Config::MainWheel::Mouse::IntentConfirmMs = snapshot.mainWheelIntentConfirmMs;
		Config::MainWheel::Mouse::IntentConeDeg = snapshot.mainWheelIntentConeDeg;
		Config::MainWheel::Mouse::IntentReleaseConeDeg = snapshot.mainWheelIntentReleaseConeDeg;
		Config::MainWheel::Mouse::IntentMinAngleDeg = snapshot.mainWheelIntentMinAngleDeg;
		Config::MainWheel::Mouse::IntentReleaseSpeed = snapshot.mainWheelIntentReleaseSpeed;
		Config::MainWheel::Mouse::IntentReleaseDwellMs = snapshot.mainWheelIntentReleaseDwellMs;
		Config::MainWheel::Mouse::IntentBias = snapshot.mainWheelIntentBias;
		Config::MainWheel::Mouse::IntentNeighborBias = snapshot.mainWheelIntentNeighborBias;
		Config::MainWheel::Mouse::ScoreWeightAngle = snapshot.mainWheelScoreWeightAngle;
		Config::MainWheel::Mouse::ScoreWeightMotion = snapshot.mainWheelScoreWeightMotion;
		Config::MainWheel::Mouse::ScoreWeightInertia = snapshot.mainWheelScoreWeightInertia;
		Config::MainWheel::Mouse::ScoreWeightStickiness = snapshot.mainWheelScoreWeightStickiness;
		Config::MainWheel::Mouse::SwitchConfidenceMargin = snapshot.mainWheelSwitchConfidenceMargin;
		Config::MainWheel::Mouse::SwitchDwellMs = snapshot.mainWheelSwitchDwellMs;
		Config::MainWheel::Mouse::InnerDeadzoneMaxSlotDelta = snapshot.mainWheelInnerDeadzoneMaxSlotDelta;
		Config::MainWheel::Mouse::DebugHoverLog = snapshot.mainWheelDebugHoverLog;
		Config::MainWheel::Mouse::LogMouseFeatures = snapshot.mainWheelLogMouseFeatures;
		Config::MainWheel::Mouse::LogCandidateScores = snapshot.mainWheelLogCandidateScores;
		Config::MainWheel::Mouse::LogIntentState = snapshot.mainWheelLogIntentState;
		Config::MainWheel::Mouse::DrawDebugOverlay = snapshot.mainWheelDrawMouseDebugOverlay;

		Config::MainWheel::MouseStabilization::Enabled = snapshot.mainWheelMouseStabilizationEnabled;
		Config::MainWheel::MouseStabilization::CenterHoldRadius = snapshot.mainWheelMouseCenterHoldRadius;
		Config::MainWheel::MouseStabilization::JumpGuardRadius = snapshot.mainWheelMouseJumpGuardRadius;
		Config::MainWheel::MouseStabilization::MaxJumpSlots = snapshot.mainWheelMouseMaxJumpSlots;
		Config::MainWheel::MouseStabilization::StepTowardEnabled = snapshot.mainWheelMouseStepTowardEnabled;
		Config::MainWheel::MouseStabilization::BoundaryMarginDeg = snapshot.mainWheelMouseBoundaryMarginDeg;
		Config::MainWheel::MouseStabilization::BoundaryMarginLowSpeedOnly = snapshot.mainWheelMouseBoundaryLowSpeedOnly;
		Config::MainWheel::MouseStabilization::LowSpeedThreshold = snapshot.mainWheelMouseLowSpeedThreshold;
		Config::MainWheel::MouseStabilization::MotionHinting::Enabled = snapshot.mainWheelMouseMotionHintEnabled;
		Config::MainWheel::MouseStabilization::MotionHinting::SpeedThreshold = snapshot.mainWheelMouseMotionHintSpeedThreshold;
		Config::MainWheel::MouseStabilization::MotionHinting::Blend = snapshot.mainWheelMouseMotionHintBlend;

		Config::MainWheel::LowEnd::DisableBlurOnOpen = snapshot.mainWheelDisableBlurOnOpen;
		Config::MainWheel::LowEnd::PreferPrimitiveBackgrounds = snapshot.mainWheelPreferPrimitiveBackgrounds;
		Config::MainWheel::ShowHandIndicator = snapshot.mainWheelShowHandIndicator;
		Config::MainWheel::HandIndicators::Left.Color = snapshot.mainWheelHandLeftColor;
		Config::MainWheel::HandIndicators::Left.Opacity = snapshot.mainWheelHandLeftOpacity;
		Config::MainWheel::HandIndicators::Left.SizeScale = snapshot.mainWheelHandLeftSizeScale;
		Config::MainWheel::HandIndicators::Left.Thickness = snapshot.mainWheelHandLeftThickness;
		Config::MainWheel::HandIndicators::Left.OffsetX = snapshot.mainWheelHandLeftOffsetX;
		Config::MainWheel::HandIndicators::Left.OffsetY = snapshot.mainWheelHandLeftOffsetY;
		Config::MainWheel::HandIndicators::Left.SlotLeftOffsetX = snapshot.mainWheelHandLeftSlotLeftOffsetX;
		Config::MainWheel::HandIndicators::Left.SlotLeftOffsetY = snapshot.mainWheelHandLeftSlotLeftOffsetY;
		Config::MainWheel::HandIndicators::Left.SlotRightOffsetX = snapshot.mainWheelHandLeftSlotRightOffsetX;
		Config::MainWheel::HandIndicators::Left.SlotRightOffsetY = snapshot.mainWheelHandLeftSlotRightOffsetY;
		Config::MainWheel::HandIndicators::Right.Color = snapshot.mainWheelHandRightColor;
		Config::MainWheel::HandIndicators::Right.Opacity = snapshot.mainWheelHandRightOpacity;
		Config::MainWheel::HandIndicators::Right.SizeScale = snapshot.mainWheelHandRightSizeScale;
		Config::MainWheel::HandIndicators::Right.Thickness = snapshot.mainWheelHandRightThickness;
		Config::MainWheel::HandIndicators::Right.OffsetX = snapshot.mainWheelHandRightOffsetX;
		Config::MainWheel::HandIndicators::Right.OffsetY = snapshot.mainWheelHandRightOffsetY;
		Config::MainWheel::HandIndicators::Right.SlotLeftOffsetX = snapshot.mainWheelHandRightSlotLeftOffsetX;
		Config::MainWheel::HandIndicators::Right.SlotLeftOffsetY = snapshot.mainWheelHandRightSlotLeftOffsetY;
		Config::MainWheel::HandIndicators::Right.SlotRightOffsetX = snapshot.mainWheelHandRightSlotRightOffsetX;
		Config::MainWheel::HandIndicators::Right.SlotRightOffsetY = snapshot.mainWheelHandRightSlotRightOffsetY;
		ApplyMainWheelIndicatorAssetPathHardcoded();
		if (Config::MainWheel::LowEnd::DisableBlurOnOpen) {
			Config::Styling::Wheel::BlurOnOpen = false;
		}
		if (Config::MainWheel::LowEnd::PreferPrimitiveBackgrounds) {
			Config::Styling::Wheel::UseGeometricPrimitiveForBackgroundTexture = true;
		}
	}

	static bool WriteWheelBehaviorIniFromSnapshot(const WheelBehaviorSnapshot& snapshot, const bool overwrite)
	{
		if (!overwrite) {
			std::error_code ec;
			if (std::filesystem::exists(WHEELBEHAVIORSETTINGS_PATH, ec) && !ec) {
				return false;
			}
		}

		CSimpleIniA out;
		out.SetUnicode();

		out.SetBoolValue("WheelBehavior", "ReleaseToUse", snapshot.releaseToUse);
		out.SetBoolValue("WheelBehavior", "CloseWheelAfterUse", snapshot.closeWheelAfterUse);
		out.SetBoolValue("WheelBehavior", "RTUAlchemy", snapshot.rtuAlchemy);
		out.SetBoolValue("WheelBehavior", "RTUSpell", snapshot.rtuSpell);
		out.SetBoolValue("WheelBehavior", "RTUShout", snapshot.rtuShout);
		out.SetBoolValue("WheelBehavior", "RTUSmartAssignEnabled", snapshot.rtuSmartAssignEnabled);
		out.SetLongValue("WheelBehavior", "RTUSmartAssignTargetHands", static_cast<long>(snapshot.rtuSmartAssignTargetHands));
		if (snapshot.rtuSmartAssignCategoryMask != 0) {
			out.SetLongValue("WheelBehavior", "RTUSmartAssignCategoryMask", static_cast<long>(snapshot.rtuSmartAssignCategoryMask));
		}
		out.SetLongValue("WheelBehavior", "RTUSmartAssignBothEmptyPriority", static_cast<long>(snapshot.rtuSmartAssignBothEmptyPriority));
		out.SetLongValue("WheelBehavior", "RTUSmartAssignOverwriteMode", static_cast<long>(snapshot.rtuSmartAssignOverwriteMode));
		out.SetBoolValue("WheelBehavior", "RTUSmartAssignDebugLog", snapshot.rtuSmartAssignDebugLog);
		out.SetDoubleValue("WheelBehavior", "HoverActivateDelaySeconds", snapshot.hoverActivateDelaySeconds);
		out.SetBoolValue("WheelBehavior", "AutoDrawOnUse", snapshot.autoDrawOnUse);
		out.SetBoolValue("WheelBehavior", "InstantSpell", snapshot.instantSpell);
		out.SetBoolValue("WheelBehavior", "InstantSpellUseDirectCast", snapshot.instantSpellUseDirectCast);
		out.SetBoolValue("WheelBehavior", "RTUAutoInstantSpell", snapshot.rtuAutoInstantSpell);
		out.SetBoolValue("WheelBehavior", "InstantPowers", snapshot.instantPowers);
		out.SetLongValue("WheelBehavior", "InstantSpellConcentrationMode", static_cast<long>(snapshot.instantSpellConcentrationMode));
		out.SetDoubleValue("WheelBehavior", "InstantSpellConcentrationMaxSeconds", snapshot.instantSpellConcentrationMaxSeconds);
		out.SetBoolValue("WheelBehavior", "InstantSpellDebugLog", snapshot.instantSpellDebugLog);
		out.SetBoolValue("WheelBehavior", "ShoutPipelineDebug", snapshot.shoutPipelineDebug);
		out.SetBoolValue("WheelBehavior", "InstantShout", snapshot.instantShout);
		out.SetBoolValue("WheelBehavior", "RTUAutoInstantShout", snapshot.rtuAutoInstantShout);
		out.SetDoubleValue("WheelBehavior", "ShoutWord2Threshold", snapshot.shoutWord2Threshold);
		out.SetDoubleValue("WheelBehavior", "ShoutWord3Threshold", snapshot.shoutWord3Threshold);
		out.SetDoubleValue("WheelBehavior", "ShoutHoldSecsWord1", snapshot.shoutHoldSecsWord1);
		out.SetDoubleValue("WheelBehavior", "ShoutHoldSecsWord2", snapshot.shoutHoldSecsWord2);
		out.SetDoubleValue("WheelBehavior", "ShoutHoldSecsWord3", snapshot.shoutHoldSecsWord3);
		out.SetBoolValue("WheelBehavior", "ShoutIgnoreRTUDelay", snapshot.shoutIgnoreRTUDelay);
		out.SetDoubleValue("WheelBehavior", "ShoutStageFillSecs", snapshot.shoutStageFillSecs);
		out.SetDoubleValue("WheelBehavior", "ShoutStageHoldSecs1", snapshot.shoutStageHoldSecs1);
		out.SetDoubleValue("WheelBehavior", "ShoutStageHoldSecs2", snapshot.shoutStageHoldSecs2);
		out.SetBoolValue("WheelBehavior", "ClearDepletedConsumables", snapshot.clearDepletedConsumables);
		out.SetLongValue("WheelBehavior", "ScriptedMiscDispatchMode", static_cast<long>(snapshot.scriptedMiscDispatchMode));
		out.SetBoolValue("WheelBehavior", "RTUAntiSlipEnabled", snapshot.rtuAntiSlipEnabled);
		out.SetDoubleValue("WheelBehavior", "RTUAntiSlipStrength", snapshot.rtuAntiSlipStrength);
		out.SetBoolValue("WheelBehavior", "LootMenuOverride", snapshot.lootMenuOverride);
		out.SetBoolValue("WheelBehavior", "HeavyListCompatibilityMode", snapshot.heavyListCompatibilityMode);
		out.SetDoubleValue("WheelBehavior", "HeavyListCompatibilitySettleMs", snapshot.heavyListCompatibilitySettleMs);
		out.SetBoolValue("WheelBehavior", "MutableInventoryHooks", snapshot.mutableInventoryHooks);

		out.SetBoolValue("WheelBehavior.KeepMissing", "Enabled", snapshot.keepMissingEnabled);
		out.SetBoolValue("WheelBehavior.KeepMissing", "KeepConsumables", snapshot.keepMissingConsumables);
		out.SetBoolValue("WheelBehavior.KeepMissing", "KeepGears", snapshot.keepMissingGears);
		out.SetBoolValue("WheelBehavior.KeepMissing", "KeepThrowableMods", snapshot.keepMissingThrowableMods);
	out.SetBoolValue("WheelBehavior.HandMemory", "Enabled", snapshot.handMemoryEnabled);
	out.SetBoolValue("WheelBehavior.HandMemory", "DebugLog", snapshot.handMemoryDebugLog);
	out.SetDoubleValue("WheelBehavior.HandMemory", "RestoreDelaySeconds", snapshot.handMemoryRestoreDelaySeconds);
	out.SetDoubleValue("WheelBehavior.HandMemory", "RestoreWindowSeconds", snapshot.handMemoryRestoreWindowSeconds);
		out.SetBoolValue("WheelBehavior.HandMemory", "RestoreLeftIfEmpty", snapshot.handMemoryRestoreLeftIfEmpty);
		out.SetBoolValue("WheelBehavior.HandMemory", "RestoreRightIfEmpty", snapshot.handMemoryRestoreRightIfEmpty);

		out.SetLongValue("BookReadCompat", "Mode", static_cast<long>(snapshot.bookReadCompatMode));
		out.SetValue("BookReadCompat", "OnReadFormIDs", snapshot.bookReadCompatOnReadFormIDs.c_str());
		out.SetValue("BookReadCompat", "OnReadPlugins", snapshot.bookReadCompatOnReadPlugins.c_str());
		out.SetValue("BookReadCompat", "OnReadNameTokens", snapshot.bookReadCompatOnReadNameTokens.c_str());
		out.SetBoolValue("BookReadCompat", "DebugLog", snapshot.bookReadCompatDebugLog);

		out.SetBoolValue("TransformWheels", "Enabled", snapshot.transformWheelsEnabled);
		out.SetLongValue("TransformWheels", "Mode", static_cast<long>(snapshot.transformWheelsMode));
		out.SetBoolValue("TransformWheels", "RestorePreviousWheel", snapshot.transformWheelsRestorePrevious);
		out.SetBoolValue("TransformWheels", "IncludeShouts", snapshot.transformWheelsIncludeShouts);
		out.SetLongValue("TransformWheels", "RegenerateThrottleMs", static_cast<long>(snapshot.transformWheelsRegenerateThrottleMs));
		out.SetLongValue("TransformWheels", "RetryWindowMs", static_cast<long>(snapshot.transformWheelsRetryWindowMs));
		out.SetLongValue("TransformWheels", "StableTicks", static_cast<long>(snapshot.transformWheelsStableTicks));
		out.SetLongValue("TransformWheels", "MinEntries", static_cast<long>(snapshot.transformWheelsMinEntries));
		out.SetBoolValue("TransformWheels", "CacheHumanSnapshot", snapshot.transformWheelsCacheHumanSnapshot);
		out.SetBoolValue("TransformWheels", "PersistGeneratedWheels", snapshot.transformWheelsPersistGeneratedWheels);
		out.SetBoolValue("TransformWheels", "UpdateOnlyOnChange", snapshot.transformWheelsUpdateOnlyOnChange);
		out.SetBoolValue("TransformWheels", "DebugLog", snapshot.transformWheelsDebugLog);
		out.SetBoolValue("TransformWheels", "GenericEnabled", snapshot.transformWheelsGenericEnabled);
		out.SetValue("TransformWheels", "GenericStateID", snapshot.transformWheelsGenericStateID.c_str());
		out.SetValue("TransformWheels", "GenericWheelID", snapshot.transformWheelsGenericWheelID.c_str());
		out.SetValue("TransformWheels", "GenericRaceEditorIDContains", snapshot.transformWheelsGenericRaceEditorIDContains.c_str());
		out.SetValue("TransformWheels", "GenericRaceKeywords", snapshot.transformWheelsGenericRaceKeywords.c_str());
		out.SetValue("TransformWheels", "GenericRaceFormIDs", snapshot.transformWheelsGenericRaceFormIDs.c_str());
		out.SetValue("TransformWheels", "PrecedenceOrder", snapshot.transformWheelsPrecedenceOrder.c_str());
		out.SetBoolValue("TransformWheels", "WerewolfAllowBaseWheel", snapshot.werewolfAllowBaseWheel);
		out.SetBoolValue("TransformWheels", "VampireLordAllowBaseWheel", snapshot.vampireLordAllowBaseWheel);
		out.SetBoolValue("TransformWheels", "WerewolfAllowBaseWheelSpells", snapshot.werewolfAllowBaseWheelSpells);
		out.SetBoolValue("TransformWheels", "WerewolfAllowBaseWheelShouts", snapshot.werewolfAllowBaseWheelShouts);
		out.SetBoolValue("WerewolfForm", "Enabled", snapshot.werewolfFormEnabled);
		out.SetValue("WerewolfForm", "PopulateMode", snapshot.werewolfFormPopulateMode.c_str());
		out.SetValue("WerewolfForm", "RaceEditorIDContains", snapshot.werewolfFormRaceEditorIDContains.c_str());
		out.SetValue("WerewolfForm", "RaceKeywords", snapshot.werewolfFormRaceKeywords.c_str());
		out.SetValue("WerewolfForm", "RaceFormIDs", snapshot.werewolfFormRaceFormIDs.c_str());
		out.SetValue("WerewolfForm", "SpellTokens", snapshot.werewolfFormSpellTokens.c_str());
		out.SetValue("WerewolfForm", "ExitSpellTokens", snapshot.werewolfFormExitSpellTokens.c_str());
		out.SetValue("WerewolfForm", "AdditionalSpellFormIDs", snapshot.werewolfFormAdditionalSpellFormIDs.c_str());
		out.SetBoolValue("WerewolfForm", "DebugLog", snapshot.werewolfFormDebugLog);
		out.SetBoolValue("VampireLordForm", "Enabled", snapshot.vampireLordFormEnabled);
		out.SetValue("VampireLordForm", "PopulateMode", snapshot.vampireLordFormPopulateMode.c_str());
		out.SetBoolValue("VampireLordForm", "HideTransformSpell", snapshot.vampireLordFormHideTransformSpell);
		out.SetBoolValue("VampireLordForm", "HideForcedRightHandSpells", snapshot.vampireLordFormHideForcedRightHandSpells);
		out.SetBoolValue("VampireLordForm", "BlockHiddenSpellActivation", snapshot.vampireLordFormBlockHiddenSpellActivation);
		out.SetBoolValue("VampireLordForm", "BlockRegularSpellsInMeleeMode", snapshot.vampireLordFormBlockRegularSpellsInMeleeMode);
		out.SetValue("VampireLordForm", "RaceEditorIDContains", snapshot.vampireLordFormRaceEditorIDContains.c_str());
		out.SetValue("VampireLordForm", "RaceKeywords", snapshot.vampireLordFormRaceKeywords.c_str());
		out.SetValue("VampireLordForm", "RaceFormIDs", snapshot.vampireLordFormRaceFormIDs.c_str());
		out.SetValue("VampireLordForm", "SpellTokens", snapshot.vampireLordFormSpellTokens.c_str());
		out.SetValue("VampireLordForm", "ExitSpellTokens", snapshot.vampireLordFormExitSpellTokens.c_str());
		out.SetValue("VampireLordForm", "AdditionalSpellFormIDs", snapshot.vampireLordFormAdditionalSpellFormIDs.c_str());
		out.SetValue("VampireLordForm", "HiddenSpellFormIDs", snapshot.vampireLordFormHiddenSpellFormIDs.c_str());
		out.SetValue("VampireLordForm", "HiddenSpellTokens", snapshot.vampireLordFormHiddenSpellTokens.c_str());
		out.SetBoolValue("VampireLordForm", "DebugLog", snapshot.vampireLordFormDebugLog);
		out.SetBoolValue("LichForm", "Enabled", snapshot.lichFormEnabled);
		out.SetValue("LichForm", "Mode", snapshot.lichFormMode.c_str());
		out.SetValue("LichForm", "PopulateMode", snapshot.lichFormPopulateMode.c_str());
		out.SetBoolValue("LichForm", "AllowBaseWheel", snapshot.lichFormAllowBaseWheel);
		out.SetValue("LichForm", "TransformGuard", snapshot.lichFormTransformGuard.c_str());
		out.SetBoolValue("LichForm", "BlockBoundSpells", snapshot.lichFormBlockBoundSpells);
		out.SetBoolValue("LichForm", "HideWeapons", snapshot.lichFormHideWeapons);
		out.SetBoolValue("LichForm", "HideGear", snapshot.lichFormHideGear);
		out.SetBoolValue("LichForm", "BlockStaffSwapping", snapshot.lichFormBlockStaffSwapping);
		out.SetBoolValue("LichForm", "SuppressDirectCast", snapshot.lichFormSuppressDirectCast);
		out.SetValue("LichForm", "RaceEditorIDContains", snapshot.lichFormRaceEditorIDContains.c_str());
		out.SetValue("LichForm", "RaceKeywords", snapshot.lichFormRaceKeywords.c_str());
		out.SetValue("LichForm", "RaceFormIDs", snapshot.lichFormRaceFormIDs.c_str());
		out.SetValue("LichForm", "SpellTokens", snapshot.lichFormSpellTokens.c_str());
		out.SetValue("LichForm", "ExitSpellTokens", snapshot.lichFormExitSpellTokens.c_str());
		out.SetValue("LichForm", "AdditionalSpellFormIDs", snapshot.lichFormAdditionalSpellFormIDs.c_str());
		out.SetBoolValue("LichForm", "DebugLog", snapshot.lichFormDebugLog);
		out.SetBoolValue("Styling.HoverDelay", "Enabled", snapshot.hoverDelayEnabled);
		out.SetDoubleValue("Styling.HoverDelay", "Radius", snapshot.hoverDelayRadius);
		out.SetDoubleValue("Styling.HoverDelay", "RadiusOffset", snapshot.hoverDelayRadiusOffset);
		out.SetDoubleValue("Styling.HoverDelay", "Thickness", snapshot.hoverDelayThickness);
		out.SetLongValue("Styling.HoverDelay", "Color", snapshot.hoverDelayColor);
		out.SetLongValue("Styling.HoverDelay", "BackgroundColor", snapshot.hoverDelayBackgroundColor);
		out.SetLongValue("Styling.HoverDelay", "InstantSpellColor", snapshot.hoverDelayInstantSpellColor);
		out.SetLongValue("Styling.HoverDelay", "InstantSpellBackgroundColor", snapshot.hoverDelayInstantSpellBackgroundColor);
		out.SetBoolValue("Styling.HoverDelay", "InstantSpellUseReskinAssets", snapshot.hoverDelayInstantSpellUseReskinAssets);
		out.SetDoubleValue("Styling.HoverDelay", "InstantSpellAssetScale", snapshot.hoverDelayInstantSpellAssetScale);
		out.SetDoubleValue("Styling.HoverDelay", "InstantSpellAssetOffsetX", snapshot.hoverDelayInstantSpellAssetOffsetX);
		out.SetDoubleValue("Styling.HoverDelay", "InstantSpellAssetOffsetY", snapshot.hoverDelayInstantSpellAssetOffsetY);
		out.SetDoubleValue("Styling.HoverDelay", "InstantSpellAssetOpacity", snapshot.hoverDelayInstantSpellAssetOpacity);
		out.SetDoubleValue("Styling.HoverDelay", "InstantSpellHandIndicatorScale", snapshot.hoverDelayInstantSpellHandIndicatorScale);
		out.SetDoubleValue("Styling.HoverDelay", "InstantSpellHandIndicatorLeftOffsetX", snapshot.hoverDelayInstantSpellHandIndicatorLeftOffsetX);
		out.SetDoubleValue("Styling.HoverDelay", "InstantSpellHandIndicatorLeftOffsetY", snapshot.hoverDelayInstantSpellHandIndicatorLeftOffsetY);
		out.SetDoubleValue("Styling.HoverDelay", "InstantSpellHandIndicatorRightOffsetX", snapshot.hoverDelayInstantSpellHandIndicatorRightOffsetX);
		out.SetDoubleValue("Styling.HoverDelay", "InstantSpellHandIndicatorRightOffsetY", snapshot.hoverDelayInstantSpellHandIndicatorRightOffsetY);
		out.SetDoubleValue("Styling.HoverDelay", "InstantSpellHandIndicatorBothOffsetX", snapshot.hoverDelayInstantSpellHandIndicatorBothOffsetX);
		out.SetDoubleValue("Styling.HoverDelay", "InstantSpellHandIndicatorBothOffsetY", snapshot.hoverDelayInstantSpellHandIndicatorBothOffsetY);
		out.SetDoubleValue("Styling.HoverDelay", "InstantSpellHandIndicatorOpacity", snapshot.hoverDelayInstantSpellHandIndicatorOpacity);
		out.SetBoolValue("Styling.HoverDelay", "InstantSpellUseAtlasAnimation", snapshot.hoverDelayInstantSpellUseAtlasAnimation);
		out.SetLongValue("Styling.HoverDelay", "InstantSpellAtlasCols", static_cast<long>(snapshot.hoverDelayInstantSpellAtlasCols));
		out.SetLongValue("Styling.HoverDelay", "InstantSpellAtlasRows", static_cast<long>(snapshot.hoverDelayInstantSpellAtlasRows));
		out.SetLongValue("Styling.HoverDelay", "InstantSpellAtlasFrameCount", static_cast<long>(snapshot.hoverDelayInstantSpellAtlasFrameCount));

		out.SetBoolValue("Cooldowns", "Enabled", snapshot.cooldownsEnabled);
		out.SetLongValue("Cooldowns", "OverlayColor", snapshot.cooldownsOverlayColor);
		out.SetBoolValue("Cooldowns", "ShowTimer", snapshot.cooldownsShowTimer);
		out.SetDoubleValue("Cooldowns", "ContentDimAlpha", snapshot.cooldownsContentDimAlpha);
		out.SetBoolValue("Cooldowns", "SelectedIndicatorEnabled", snapshot.cooldownsSelectedIndicatorEnabled);
		out.SetLongValue("Cooldowns", "SelectedIndicatorTintColor", snapshot.cooldownsSelectedIndicatorTintColor);
		out.SetDoubleValue("Cooldowns", "CacheWindowSeconds", snapshot.cooldownsCacheWindowSeconds);

		out.SetLongValue("Cooldowns.TimerText", "FontIndex", snapshot.cooldownsTimerTextFontIndex);
		out.SetDoubleValue("Cooldowns.TimerText", "Size", snapshot.cooldownsTimerTextSize);
		out.SetLongValue("Cooldowns.TimerText", "Color", snapshot.cooldownsTimerTextColor);

		out.SetBoolValue("Sounds", "EnableSounds", snapshot.soundsEnabled);
		out.SetValue("Sounds", "HoverSoundEditorID", snapshot.soundsHoverEditorID.c_str());
		out.SetValue("Sounds", "ActivateSoundEditorID", snapshot.soundsActivateEditorID.c_str());
		out.SetDoubleValue("Sounds", "HoverSoundVolume", snapshot.soundsHoverVolume);
		out.SetDoubleValue("Sounds", "ActivateSoundVolume", snapshot.soundsActivateVolume);

		out.SetBoolValue("Sounds", "EnableShoutStageSounds", snapshot.enableShoutStageSounds);
		out.SetLongValue("Sounds", "ShoutStageSoundMode", static_cast<long>(snapshot.shoutStageSoundMode));
		out.SetValue("Sounds", "ShoutUISoundEditorID", snapshot.shoutUISoundEditorID.c_str());
		out.SetDoubleValue("Sounds", "ShoutUIStageVolume1", snapshot.shoutUIStageVolume1);
		out.SetDoubleValue("Sounds", "ShoutUIStageVolume2", snapshot.shoutUIStageVolume2);
		out.SetDoubleValue("Sounds", "ShoutUIStageVolume3", snapshot.shoutUIStageVolume3);
		out.SetDoubleValue("Sounds", "ShoutUIStagePitch1", snapshot.shoutUIStagePitch1);
		out.SetDoubleValue("Sounds", "ShoutUIStagePitch2", snapshot.shoutUIStagePitch2);
		out.SetDoubleValue("Sounds", "ShoutUIStagePitch3", snapshot.shoutUIStagePitch3);
		out.SetValue("Sounds", "ShoutWord1SoundEditorID", snapshot.shoutWord1SoundEditorID.c_str());
		out.SetValue("Sounds", "ShoutWord2SoundEditorID", snapshot.shoutWord2SoundEditorID.c_str());
		out.SetValue("Sounds", "ShoutWord3SoundEditorID", snapshot.shoutWord3SoundEditorID.c_str());

		out.SetBoolValue("MainWheel.Debug", "Enabled", snapshot.mainWheelDebugEnabled);
		out.SetBoolValue("MainWheel.Debug", "OverlayEnabled", snapshot.mainWheelDebugOverlayEnabled);
		out.SetBoolValue("MainWheel.Debug", "Verbose", snapshot.mainWheelDebugVerbose);
		out.SetLongValue("MainWheel.Debug", "RateLimitMs", snapshot.mainWheelDebugRateLimitMs);
		out.SetBoolValue("MainWheel.Debug", "LogOpenClose", snapshot.mainWheelDebugLogOpenClose);
		out.SetBoolValue("MainWheel.Debug", "LogConfig", snapshot.mainWheelDebugLogConfig);
		out.SetBoolValue("MainWheel.Debug", "LogInput", snapshot.mainWheelDebugLogInput);
		out.SetBoolValue("MainWheel.Debug", "LogScaling", snapshot.mainWheelDebugLogScaling);
		out.SetBoolValue("MainWheel.Debug", "LogClamp", snapshot.mainWheelDebugLogClamp);
		out.SetBoolValue("MainWheel.Debug", "LogIndicators", snapshot.mainWheelDebugLogIndicators);
		out.SetBoolValue("MainWheel.Debug", "LogReskinResolve", snapshot.mainWheelDebugLogReskinResolve);
		out.SetBoolValue("MainWheel.Debug", "LogAssets", snapshot.mainWheelDebugLogAssets);
		out.SetBoolValue("MainWheel.Debug", "LogPerf", snapshot.mainWheelDebugLogPerf);

		out.SetDoubleValue("MainWheel.Mouse", "CenterLockRadiusPx", snapshot.mainWheelCenterLockRadiusPx);
		out.SetDoubleValue("MainWheel.Mouse", "JumpGuardRadiusPx", snapshot.mainWheelJumpGuardRadiusPx);
		out.SetDoubleValue("MainWheel.Mouse", "HysteresisDegrees", snapshot.mainWheelHysteresisDegrees);
		out.SetBoolValue("MainWheel.Mouse", "CenterSlowdownEnabled", snapshot.mainWheelCenterSlowdownEnabled);
		out.SetDoubleValue("MainWheel.Mouse", "GainCenter", snapshot.mainWheelGainCenter);
		out.SetDoubleValue("MainWheel.Mouse", "GainOuter", snapshot.mainWheelGainOuter);
		out.SetDoubleValue("MainWheel.Mouse", "CurvePower", snapshot.mainWheelCurvePower);
		out.SetDoubleValue("MainWheel.Mouse", "InnerDeadZoneR", snapshot.mainWheelInnerDeadZoneR);
		out.SetDoubleValue("MainWheel.Mouse", "InnerBlendZoneR", snapshot.mainWheelInnerBlendZoneR);
		out.SetDoubleValue("MainWheel.Mouse", "MinStableSpeed", snapshot.mainWheelMinStableSpeed);
		out.SetDoubleValue("MainWheel.Mouse", "VelocityHalfLifeMs", snapshot.mainWheelVelocityHalfLifeMs);
		out.SetDoubleValue("MainWheel.Mouse", "StableDirHalfLifeMs", snapshot.mainWheelStableDirHalfLifeMs);
		out.SetDoubleValue("MainWheel.Mouse", "IntentMinSpeed", snapshot.mainWheelIntentMinSpeed);
		out.SetDoubleValue("MainWheel.Mouse", "IntentSustainSpeed", snapshot.mainWheelIntentSustainSpeed);
		out.SetDoubleValue("MainWheel.Mouse", "IntentConfirmMs", snapshot.mainWheelIntentConfirmMs);
		out.SetDoubleValue("MainWheel.Mouse", "IntentConeDeg", snapshot.mainWheelIntentConeDeg);
		out.SetDoubleValue("MainWheel.Mouse", "IntentReleaseConeDeg", snapshot.mainWheelIntentReleaseConeDeg);
		out.SetDoubleValue("MainWheel.Mouse", "IntentMinAngleDeg", snapshot.mainWheelIntentMinAngleDeg);
		out.SetDoubleValue("MainWheel.Mouse", "IntentReleaseSpeed", snapshot.mainWheelIntentReleaseSpeed);
		out.SetDoubleValue("MainWheel.Mouse", "IntentReleaseDwellMs", snapshot.mainWheelIntentReleaseDwellMs);
		out.SetDoubleValue("MainWheel.Mouse", "IntentBias", snapshot.mainWheelIntentBias);
		out.SetDoubleValue("MainWheel.Mouse", "IntentNeighborBias", snapshot.mainWheelIntentNeighborBias);
		out.SetDoubleValue("MainWheel.Mouse", "ScoreWeightAngle", snapshot.mainWheelScoreWeightAngle);
		out.SetDoubleValue("MainWheel.Mouse", "ScoreWeightMotion", snapshot.mainWheelScoreWeightMotion);
		out.SetDoubleValue("MainWheel.Mouse", "ScoreWeightInertia", snapshot.mainWheelScoreWeightInertia);
		out.SetDoubleValue("MainWheel.Mouse", "ScoreWeightStickiness", snapshot.mainWheelScoreWeightStickiness);
		out.SetDoubleValue("MainWheel.Mouse", "SwitchConfidenceMargin", snapshot.mainWheelSwitchConfidenceMargin);
		out.SetDoubleValue("MainWheel.Mouse", "SwitchDwellMs", snapshot.mainWheelSwitchDwellMs);
		out.SetLongValue("MainWheel.Mouse", "InnerDeadzoneMaxSlotDelta", snapshot.mainWheelInnerDeadzoneMaxSlotDelta);
		out.SetBoolValue("MainWheel.Mouse", "DebugHoverLog", snapshot.mainWheelDebugHoverLog);
		out.SetBoolValue("MainWheel.Mouse", "LogMouseFeatures", snapshot.mainWheelLogMouseFeatures);
		out.SetBoolValue("MainWheel.Mouse", "LogCandidateScores", snapshot.mainWheelLogCandidateScores);
		out.SetBoolValue("MainWheel.Mouse", "LogIntentState", snapshot.mainWheelLogIntentState);
		out.SetBoolValue("MainWheel.Mouse", "DrawDebugOverlay", snapshot.mainWheelDrawMouseDebugOverlay);

		out.SetBoolValue("MainWheel.MouseStabilization", "Enabled", snapshot.mainWheelMouseStabilizationEnabled);
		out.SetDoubleValue("MainWheel.MouseStabilization", "CenterHoldRadius", snapshot.mainWheelMouseCenterHoldRadius);
		out.SetDoubleValue("MainWheel.MouseStabilization", "JumpGuardRadius", snapshot.mainWheelMouseJumpGuardRadius);
		out.SetLongValue("MainWheel.MouseStabilization", "MaxJumpSlots", snapshot.mainWheelMouseMaxJumpSlots);
		out.SetBoolValue("MainWheel.MouseStabilization", "StepTowardEnabled", snapshot.mainWheelMouseStepTowardEnabled);
		out.SetDoubleValue("MainWheel.MouseStabilization", "BoundaryMarginDeg", snapshot.mainWheelMouseBoundaryMarginDeg);
		out.SetBoolValue("MainWheel.MouseStabilization", "BoundaryMarginLowSpeedOnly", snapshot.mainWheelMouseBoundaryLowSpeedOnly);
		out.SetDoubleValue("MainWheel.MouseStabilization", "LowSpeedThreshold", snapshot.mainWheelMouseLowSpeedThreshold);
		out.SetBoolValue("MainWheel.MouseStabilization", "MotionHintingEnabled", snapshot.mainWheelMouseMotionHintEnabled);
		out.SetDoubleValue("MainWheel.MouseStabilization", "MotionHintingSpeedThreshold", snapshot.mainWheelMouseMotionHintSpeedThreshold);
		out.SetDoubleValue("MainWheel.MouseStabilization", "MotionHintingBlend", snapshot.mainWheelMouseMotionHintBlend);

		out.SetBoolValue("MainWheel.LowEnd", "DisableBlurOnOpen", snapshot.mainWheelDisableBlurOnOpen);
		out.SetBoolValue("MainWheel.LowEnd", "PreferPrimitiveBackgrounds", snapshot.mainWheelPreferPrimitiveBackgrounds);
		out.SetBoolValue("MainWheel.Indicators", "ShowHandIndicator", snapshot.mainWheelShowHandIndicator);
		out.SetLongValue("MainWheel.Indicators.Left", "Color", snapshot.mainWheelHandLeftColor);
		out.SetDoubleValue("MainWheel.Indicators.Left", "Opacity", snapshot.mainWheelHandLeftOpacity);
		out.SetDoubleValue("MainWheel.Indicators.Left", "SizeScale", snapshot.mainWheelHandLeftSizeScale);
		out.SetDoubleValue("MainWheel.Indicators.Left", "Thickness", snapshot.mainWheelHandLeftThickness);
		out.SetDoubleValue("MainWheel.Indicators.Left", "OffsetX", snapshot.mainWheelHandLeftOffsetX);
		out.SetDoubleValue("MainWheel.Indicators.Left", "OffsetY", snapshot.mainWheelHandLeftOffsetY);
		out.SetDoubleValue("MainWheel.Indicators.Left", "SlotLeftOffsetX", snapshot.mainWheelHandLeftSlotLeftOffsetX);
		out.SetDoubleValue("MainWheel.Indicators.Left", "SlotLeftOffsetY", snapshot.mainWheelHandLeftSlotLeftOffsetY);
		out.SetDoubleValue("MainWheel.Indicators.Left", "SlotRightOffsetX", snapshot.mainWheelHandLeftSlotRightOffsetX);
		out.SetDoubleValue("MainWheel.Indicators.Left", "SlotRightOffsetY", snapshot.mainWheelHandLeftSlotRightOffsetY);
		out.SetValue("MainWheel.Indicators.Left", "AssetPath", snapshot.mainWheelHandLeftAssetPath.c_str());
		out.SetLongValue("MainWheel.Indicators.Right", "Color", snapshot.mainWheelHandRightColor);
		out.SetDoubleValue("MainWheel.Indicators.Right", "Opacity", snapshot.mainWheelHandRightOpacity);
		out.SetDoubleValue("MainWheel.Indicators.Right", "SizeScale", snapshot.mainWheelHandRightSizeScale);
		out.SetDoubleValue("MainWheel.Indicators.Right", "Thickness", snapshot.mainWheelHandRightThickness);
		out.SetDoubleValue("MainWheel.Indicators.Right", "OffsetX", snapshot.mainWheelHandRightOffsetX);
		out.SetDoubleValue("MainWheel.Indicators.Right", "OffsetY", snapshot.mainWheelHandRightOffsetY);
		out.SetDoubleValue("MainWheel.Indicators.Right", "SlotLeftOffsetX", snapshot.mainWheelHandRightSlotLeftOffsetX);
		out.SetDoubleValue("MainWheel.Indicators.Right", "SlotLeftOffsetY", snapshot.mainWheelHandRightSlotLeftOffsetY);
		out.SetDoubleValue("MainWheel.Indicators.Right", "SlotRightOffsetX", snapshot.mainWheelHandRightSlotRightOffsetX);
		out.SetDoubleValue("MainWheel.Indicators.Right", "SlotRightOffsetY", snapshot.mainWheelHandRightSlotRightOffsetY);
		out.SetValue("MainWheel.Indicators.Right", "AssetPath", snapshot.mainWheelHandRightAssetPath.c_str());

		return out.SaveFile(WHEELBEHAVIORSETTINGS_PATH) >= 0;
	}
}
bool GetBoolValue(const CSimpleIniA& ini, const char* section, const char* key, bool& value)
{
	const char* strValue = ini.GetValue(section, key, nullptr);
	if (strValue != nullptr) {
		value = ini.GetBoolValue(section, key, value);
		return true;
	}
	return false;
}

bool GetUInt32Value(const CSimpleIniA& ini, const char* section, const char* key, uint32_t& value)
{
	const char* strValue = ini.GetValue(section, key, nullptr);
	if (strValue == nullptr || *strValue == '\0') {
		return false;
	}

	// Skip leading whitespace
	const char* p = strValue;
	while (*p && std::isspace(static_cast<unsigned char>(*p))) {
		++p;
	}
	if (!*p) {
		return false;
	}

	// Parse as unsigned long long to handle values near UINT32_MAX without overflow
	char* end = nullptr;
	errno = 0;
	unsigned long long v = std::strtoull(p, &end, 0);

	if (end == p) {
		ERROR("Failed to parse {}: {} when reading uint32 value (no digits).", section, key);
		return false;
	}

	// Allow ".000000" suffix (dMenu float slider serialization)
	if (end && *end == '.') {
		const char* frac = end + 1;
		while (*frac && std::isdigit(static_cast<unsigned char>(*frac))) {
			++frac;
		}
		while (*frac && std::isspace(static_cast<unsigned char>(*frac))) {
			++frac;
		}
		if (*frac != '\0') {
			ERROR("Failed to parse {}: {} when reading uint32 value (invalid suffix).", section, key);
			return false;
		}
	} else if (end) {
		const char* t = end;
		while (*t && std::isspace(static_cast<unsigned char>(*t))) {
			++t;
		}
		if (*t != '\0') {
			ERROR("Failed to parse {}: {} when reading uint32 value (trailing chars).", section, key);
			return false;
		}
	}

	// Clamp to UINT32_MAX instead of crashing on out-of-range
	if (errno == ERANGE || v > (std::numeric_limits<uint32_t>::max)()) {
		logger::warn("Value for {}: {} exceeds uint32 max, clamping to {}", section, key, (std::numeric_limits<uint32_t>::max)());
		v = (std::numeric_limits<uint32_t>::max)();
	}

	value = static_cast<uint32_t>(v);
	return true;
}

bool GetFloatValue(const CSimpleIniA& ini, const char* section, const char* key, float& value)
{
	const char* strValue = ini.GetValue(section, key, nullptr);
	if (strValue != nullptr) {
		try {
			value = std::stof(strValue);
			return true;
		} catch (const std::invalid_argument&) {
			// Conversion failed, value remains unchanged
			ERROR("Failed to parse {}: {} when reading float value.", section, key);
			return false;
		}
	}
	return false;
}

bool GetStringValue(const CSimpleIniA& ini, const char* section, const char* key, std::string& value)
{
	const char* strValue = ini.GetValue(section, key, nullptr);
	if (strValue != nullptr) {
		value = strValue;
		return true;
	}
	return false;
}

static Config::ResolutionFix::Mode ParseResolutionFixMode(std::string value)
{
	auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
	value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), is_space));
	value.erase(std::find_if_not(value.rbegin(), value.rend(), is_space).base(), value.end());

	std::string normalized;
	normalized.reserve(value.size());
	for (unsigned char c : value) {
		if (std::isspace(c) || c == '_' || c == '-') {
			continue;
		}
		normalized.push_back(static_cast<char>(std::tolower(c)));
	}

	if (normalized.empty()) {
		return Config::ResolutionFix::Mode::Auto;
	}
	if (normalized == "auto") {
		return Config::ResolutionFix::Mode::Auto;
	}
	if (normalized == "forcedisplaytogame" || normalized == "displaytogame") {
		return Config::ResolutionFix::Mode::ForceDisplayToGame;
	}
	if (normalized == "forcenone" || normalized == "none" || normalized == "off") {
		return Config::ResolutionFix::Mode::ForceNone;
	}

	bool numeric = std::all_of(normalized.begin(), normalized.end(),
		[](unsigned char c) { return std::isdigit(c) != 0; });
	if (numeric) {
		int val = std::atoi(normalized.c_str());
		val = std::clamp(val, 0, 2);
		return static_cast<Config::ResolutionFix::Mode>(val);
	}

	return Config::ResolutionFix::Mode::Auto;
}

static Config::WheelBehavior::Gamepad::DPad::Mode ParseGamepadDpadMode(std::string value)
{
	auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
	value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), is_space));
	value.erase(std::find_if_not(value.rbegin(), value.rend(), is_space).base(), value.end());

	std::string normalized;
	normalized.reserve(value.size());
	for (unsigned char c : value) {
		if (std::isspace(c) || c == '_' || c == '-') {
			continue;
		}
		normalized.push_back(static_cast<char>(std::tolower(c)));
	}

	if (normalized.empty()) {
		return Config::WheelBehavior::Gamepad::DPad::Mode::Items;
	}
	if (normalized == "items" || normalized == "item") {
		return Config::WheelBehavior::Gamepad::DPad::Mode::Items;
	}
	if (normalized == "slots" || normalized == "slot") {
		return Config::WheelBehavior::Gamepad::DPad::Mode::Slots;
	}

	bool numeric = std::all_of(normalized.begin(), normalized.end(),
		[](unsigned char c) { return std::isdigit(c) != 0; });
	if (numeric) {
		int val = std::atoi(normalized.c_str());
		val = std::clamp(val, 0, 1);
		return static_cast<Config::WheelBehavior::Gamepad::DPad::Mode>(val);
	}

	return Config::WheelBehavior::Gamepad::DPad::Mode::Items;
}

static void ReadResolutionFixConfig(const CSimpleIniA& ini)
{
	GetBoolValue(ini, "ResolutionFix", "Enabled", Config::ResolutionFix::Enabled);
	GetFloatValue(ini, "ResolutionFix", "Epsilon", Config::ResolutionFix::Epsilon);
	GetBoolValue(ini, "ResolutionFix", "LogOncePerOpen", Config::ResolutionFix::LogOncePerOpen);

	std::string modeStr;
	if (GetStringValue(ini, "ResolutionFix", "Mode", modeStr)) {
		Config::ResolutionFix::ModeSetting = ParseResolutionFixMode(modeStr);
	} else {
		uint32_t modeVal = static_cast<uint32_t>(Config::ResolutionFix::ModeSetting);
		if (GetUInt32Value(ini, "ResolutionFix", "Mode", modeVal)) {
			modeVal = std::clamp(modeVal, 0u, 2u);
			Config::ResolutionFix::ModeSetting = static_cast<Config::ResolutionFix::Mode>(modeVal);
		}
	}
}

static bool ReadMainWheelLayoutScalingConfig(const CSimpleIniA& ini, const char* sourcePath, const char* sourceTag)
{
	const auto* section = ini.GetSection("MainWheel.LayoutScaling");
	if (!section) {
		return false;
	}
	Config::MainWheel::LayoutScaling::ConfigPresent = true;
	Config::MainWheel::LayoutScaling::LoadedSourceTag = sourceTag ? sourceTag : "off";
	if (sourcePath) {
		Config::MainWheel::LayoutScaling::LoadedSourcePath = sourcePath;
	} else {
		Config::MainWheel::LayoutScaling::LoadedSourcePath.clear();
	}

	GetBoolValue(ini, "MainWheel.LayoutScaling", "Enabled", Config::MainWheel::LayoutScaling::Enabled);
	GetFloatValue(ini, "MainWheel.LayoutScaling", "RefW", Config::MainWheel::LayoutScaling::RefW);
	GetFloatValue(ini, "MainWheel.LayoutScaling", "RefH", Config::MainWheel::LayoutScaling::RefH);
	GetBoolValue(ini, "MainWheel.LayoutScaling", "ClampToScreen", Config::MainWheel::LayoutScaling::ClampToScreen);
	GetFloatValue(ini, "MainWheel.LayoutScaling", "SafePadPx", Config::MainWheel::LayoutScaling::SafePadPx);
	GetBoolValue(ini, "MainWheel.LayoutScaling", "AutoCreateUserFile", Config::MainWheel::LayoutScaling::AutoCreateUserFile);
	return true;
}

static bool ReadAmmoWheelLayoutScalingConfig(const CSimpleIniA& ini, const char* sourcePath, const char* sourceTag)
{
	const auto* section = ini.GetSection("AmmoWheel.LayoutScaling");
	if (!section) {
		return false;
	}
	Config::AmmoWheel::LayoutScaling::ConfigPresent = true;
	Config::AmmoWheel::LayoutScaling::LoadedSourceTag = sourceTag ? sourceTag : "off";
	if (sourcePath) {
		Config::AmmoWheel::LayoutScaling::LoadedSourcePath = sourcePath;
	} else {
		Config::AmmoWheel::LayoutScaling::LoadedSourcePath.clear();
	}

	GetBoolValue(ini, "AmmoWheel.LayoutScaling", "Enabled", Config::AmmoWheel::LayoutScaling::Enabled);
	GetFloatValue(ini, "AmmoWheel.LayoutScaling", "RefW", Config::AmmoWheel::LayoutScaling::RefW);
	GetFloatValue(ini, "AmmoWheel.LayoutScaling", "RefH", Config::AmmoWheel::LayoutScaling::RefH);
	GetBoolValue(ini, "AmmoWheel.LayoutScaling", "ClampToScreen", Config::AmmoWheel::LayoutScaling::ClampToScreen);
	GetFloatValue(ini, "AmmoWheel.LayoutScaling", "SafePadPx", Config::AmmoWheel::LayoutScaling::SafePadPx);
	GetBoolValue(ini, "AmmoWheel.LayoutScaling", "AutoCreateUserFile", Config::AmmoWheel::LayoutScaling::AutoCreateUserFile);
	GetBoolValue(ini, "AmmoWheel.LayoutScaling", "ScaleGeometry", Config::AmmoWheel::LayoutScaling::ScaleGeometry);
	GetBoolValue(ini, "AmmoWheel.LayoutScaling", "ScaleText", Config::AmmoWheel::LayoutScaling::ScaleText);
	GetBoolValue(ini, "AmmoWheel.LayoutScaling", "ScaleStylePx", Config::AmmoWheel::LayoutScaling::ScaleStylePx);
	return true;
}

static void LoadMainWheelLayoutIniOverrides(const CSimpleIniA& styleIni)
{
	std::error_code ec;
	const bool templateExists = std::filesystem::exists(MAINWHEEL_LAYOUT_TEMPLATE_PATH, ec) && !ec;
	if (ec) {
		ec.clear();
	}

	bool loadedTemplate = false;
	bool loadedTemplateAmmo = false;
	if (templateExists) {
		CSimpleIniA layoutIni;
		layoutIni.SetUnicode();
		if (layoutIni.LoadFile(MAINWHEEL_LAYOUT_TEMPLATE_PATH) >= 0) {
			loadedTemplate = ReadMainWheelLayoutScalingConfig(layoutIni, MAINWHEEL_LAYOUT_TEMPLATE_PATH, "template");
			if (loadedTemplate) {
				logger::info("[MainWheel.LayoutScaling] Loaded from template MainWheel.Layout.ini (Enabled={}, Ref={}x{}, Clamp={}, SafePad={:.1f}, AutoCreate={})",
					Config::MainWheel::LayoutScaling::Enabled,
					Config::MainWheel::LayoutScaling::RefW,
					Config::MainWheel::LayoutScaling::RefH,
					Config::MainWheel::LayoutScaling::ClampToScreen,
					Config::MainWheel::LayoutScaling::SafePadPx,
					Config::MainWheel::LayoutScaling::AutoCreateUserFile);
			} else {
				logger::warn("[MainWheel.LayoutScaling] MainWheel.Layout.ini found but [MainWheel.LayoutScaling] section is missing.");
			}

			loadedTemplateAmmo = ReadAmmoWheelLayoutScalingConfig(layoutIni, MAINWHEEL_LAYOUT_TEMPLATE_PATH, "template");
			if (loadedTemplateAmmo) {
				logger::info("[AmmoWheel.LayoutScaling] Loaded from template MainWheel.Layout.ini (Enabled={}, Ref={}x{}, Clamp={}, SafePad={:.1f}, AutoCreate={}, Geometry={}, Text={}, StylePx={})",
					Config::AmmoWheel::LayoutScaling::Enabled,
					Config::AmmoWheel::LayoutScaling::RefW,
					Config::AmmoWheel::LayoutScaling::RefH,
					Config::AmmoWheel::LayoutScaling::ClampToScreen,
					Config::AmmoWheel::LayoutScaling::SafePadPx,
					Config::AmmoWheel::LayoutScaling::AutoCreateUserFile,
					Config::AmmoWheel::LayoutScaling::ScaleGeometry,
					Config::AmmoWheel::LayoutScaling::ScaleText,
					Config::AmmoWheel::LayoutScaling::ScaleStylePx);
			} else {
				logger::warn("[AmmoWheel.LayoutScaling] MainWheel.Layout.ini found but [AmmoWheel.LayoutScaling] section is missing.");
			}
		} else {
			logger::warn("[MainWheel.LayoutScaling] Failed to read MainWheel.Layout.ini.");
		}
	}

	bool loadedUser = false;
	bool loadedUserAmmo = false;
	const bool userExists = std::filesystem::exists(MAINWHEEL_LAYOUT_USER_PATH, ec) && !ec;
	if (ec) {
		ec.clear();
	}
	if (userExists) {
		CSimpleIniA userIni;
		userIni.SetUnicode();
		if (userIni.LoadFile(MAINWHEEL_LAYOUT_USER_PATH) >= 0) {
			loadedUser = ReadMainWheelLayoutScalingConfig(userIni, MAINWHEEL_LAYOUT_USER_PATH, "user");
			if (loadedUser) {
				logger::info("[MainWheel.LayoutScaling] Loaded from user override (Enabled={}, Ref={}x{}, Clamp={}, SafePad={:.1f})",
					Config::MainWheel::LayoutScaling::Enabled,
					Config::MainWheel::LayoutScaling::RefW,
					Config::MainWheel::LayoutScaling::RefH,
					Config::MainWheel::LayoutScaling::ClampToScreen,
					Config::MainWheel::LayoutScaling::SafePadPx);
			} else {
				logger::warn("[MainWheel.LayoutScaling] user/MainWheel.Layout.ini found but [MainWheel.LayoutScaling] section is missing.");
			}

			loadedUserAmmo = ReadAmmoWheelLayoutScalingConfig(userIni, MAINWHEEL_LAYOUT_USER_PATH, "user");
			if (loadedUserAmmo) {
				logger::info("[AmmoWheel.LayoutScaling] Loaded from user override (Enabled={}, Ref={}x{}, Clamp={}, SafePad={:.1f}, Geometry={}, Text={}, StylePx={})",
					Config::AmmoWheel::LayoutScaling::Enabled,
					Config::AmmoWheel::LayoutScaling::RefW,
					Config::AmmoWheel::LayoutScaling::RefH,
					Config::AmmoWheel::LayoutScaling::ClampToScreen,
					Config::AmmoWheel::LayoutScaling::SafePadPx,
					Config::AmmoWheel::LayoutScaling::ScaleGeometry,
					Config::AmmoWheel::LayoutScaling::ScaleText,
					Config::AmmoWheel::LayoutScaling::ScaleStylePx);
			} else {
				logger::warn("[AmmoWheel.LayoutScaling] user/MainWheel.Layout.ini found but [AmmoWheel.LayoutScaling] section is missing.");
			}
		} else {
			logger::warn("[MainWheel.LayoutScaling] Failed to read user/MainWheel.Layout.ini.");
		}
	}

	if (!loadedTemplate && !loadedUser) {
		if (ReadMainWheelLayoutScalingConfig(styleIni, STYLESETTINGS_PATH, "legacy")) {
			logger::warn("[MainWheel.LayoutScaling] Loaded from legacy source '{}' (DEPRECATED). Move to user/MainWheel.Layout.ini",
				STYLESETTINGS_PATH);
		}
	}

	if (!loadedTemplateAmmo && !loadedUserAmmo) {
		std::error_code legacyEc;
		if (std::filesystem::exists(AMMOWHEEL_LAYOUT_LEGACY_PATH, legacyEc) && !legacyEc) {
			CSimpleIniA legacyIni;
			legacyIni.SetUnicode();
			if (legacyIni.LoadFile(AMMOWHEEL_LAYOUT_LEGACY_PATH) >= 0) {
				if (ReadAmmoWheelLayoutScalingConfig(legacyIni, AMMOWHEEL_LAYOUT_LEGACY_PATH, "legacy")) {
					logger::warn("[AmmoWheel.LayoutScaling] Loaded from legacy source '{}' (DEPRECATED). Move to user/MainWheel.Layout.ini",
						AMMOWHEEL_LAYOUT_LEGACY_PATH);
				} else {
					logger::warn("[AmmoWheel.LayoutScaling] Legacy AmmoWheel.Layout.ini found but [AmmoWheel.LayoutScaling] section is missing.");
				}
			} else {
				logger::warn("[AmmoWheel.LayoutScaling] Failed to read legacy AmmoWheel.Layout.ini.");
			}
		}
	}
}

static bool g_layoutAutoCreateAttempted = false;
static bool g_layoutAutoCreateFailed = false;
static bool g_ammoLayoutRepairAttempted = false;
static bool g_ammoLayoutRepairFailed = false;

static bool WriteMainWheelLayoutUserIni(float displayW, float displayH, int& outRefW, int& outRefH, std::string& outError)
{
	// Use runtime display resolution for auto-scaling reference
	outRefW = static_cast<int>(std::round(displayW));
	outRefH = static_cast<int>(std::round(displayH));
	if (outRefW <= 0 || outRefH <= 0) {
		outError = "invalid display size";
		return false;
	}

	std::error_code ec;
	std::filesystem::create_directories(MAINWHEEL_LAYOUT_USER_DIR, ec);
	if (ec) {
		outError = ec.message();
		return false;
	}

	std::ofstream file(MAINWHEEL_LAYOUT_USER_PATH, std::ios::out | std::ios::trunc);
	if (!file.is_open()) {
		outError = "open failed";
		return false;
	}

	file << "[MainWheel.LayoutScaling]\n";
	file << "Enabled = true\n";
	file << "RefW = " << outRefW << "\n";
	file << "RefH = " << outRefH << "\n";
	file << "ClampToScreen = true\n";
	file << "SafePadPx = 8.0\n";
	file << "\n[AmmoWheel.LayoutScaling]\n";
	file << "Enabled = true\n";
	file << "RefW = " << outRefW << "\n";
	file << "RefH = " << outRefH << "\n";
	file << "ClampToScreen = true\n";
	file << "SafePadPx = 8.0\n";
	file << "ScaleGeometry = true\n";
	file << "ScaleText = false\n";
	file << "ScaleStylePx = true\n";
	return true;
}

static bool AppendAmmoWheelLayoutSection(float displayW, float displayH, int& outRefW, int& outRefH, std::string& outError)
{
	// Use runtime display resolution for auto-scaling reference
	outRefW = static_cast<int>(std::round(displayW));
	outRefH = static_cast<int>(std::round(displayH));
	if (outRefW <= 0 || outRefH <= 0) {
		outError = "invalid display size";
		return false;
	}

	std::ofstream file(MAINWHEEL_LAYOUT_USER_PATH, std::ios::out | std::ios::app);
	if (!file.is_open()) {
		outError = "open failed";
		return false;
	}

	file << "\n[AmmoWheel.LayoutScaling]\n";
	file << "Enabled = true\n";
	file << "RefW = " << outRefW << "\n";
	file << "RefH = " << outRefH << "\n";
	file << "ClampToScreen = true\n";
	file << "SafePadPx = 8.0\n";
	file << "ScaleGeometry = true\n";
	file << "ScaleText = false\n";
	file << "ScaleStylePx = true\n";
	return true;
}

static void MaybeAutoCreateMainWheelLayoutUserIni(const ResolutionScale::State& state)
{
	if (!Config::MainWheel::LayoutScaling::AutoCreateUserFile && !Config::AmmoWheel::LayoutScaling::AutoCreateUserFile) {
		return;
	}

	std::error_code ec;
	const bool userExists = std::filesystem::exists(MAINWHEEL_LAYOUT_USER_PATH, ec) && !ec;
	if (ec) {
		ec.clear();
	}

	if (userExists) {
		if (!Config::AmmoWheel::LayoutScaling::AutoCreateUserFile || g_ammoLayoutRepairAttempted || g_ammoLayoutRepairFailed) {
			return;
		}
		if (state.displayW <= 0.0f || state.displayH <= 0.0f) {
			return;
		}
		CSimpleIniA userIni;
		userIni.SetUnicode();
		if (userIni.LoadFile(MAINWHEEL_LAYOUT_USER_PATH) >= 0) {
			if (!userIni.GetSection("AmmoWheel.LayoutScaling")) {
				g_ammoLayoutRepairAttempted = true;
				int refW = 0;
				int refH = 0;
				std::string error;
				if (AppendAmmoWheelLayoutSection(state.displayW, state.displayH, refW, refH, error)) {
					logger::info("[AmmoWheel.LayoutScaling] Repaired user override. Appended missing section with Ref={}x{}.",
						refW, refH);
					CSimpleIniA reloadIni;
					reloadIni.SetUnicode();
					if (reloadIni.LoadFile(MAINWHEEL_LAYOUT_USER_PATH) >= 0) {
						ReadMainWheelLayoutScalingConfig(reloadIni, MAINWHEEL_LAYOUT_USER_PATH, "user");
						ReadAmmoWheelLayoutScalingConfig(reloadIni, MAINWHEEL_LAYOUT_USER_PATH, "user");
					}
				} else {
					g_ammoLayoutRepairFailed = true;
					logger::warn("[AmmoWheel.LayoutScaling] Failed to repair user override (reason={}). LayoutScaling will remain OFF.",
						error.empty() ? "unknown" : error);
				}
			} else {
				g_ammoLayoutRepairAttempted = true;
			}
		} else {
			g_ammoLayoutRepairFailed = true;
			logger::warn("[AmmoWheel.LayoutScaling] Failed to read user override for repair. LayoutScaling will remain OFF.");
		}
		return;
	}

	if (g_layoutAutoCreateAttempted || g_layoutAutoCreateFailed) {
		return;
	}
	if (state.displayW <= 0.0f || state.displayH <= 0.0f) {
		return;
	}

	g_layoutAutoCreateAttempted = true;
	int refW = 0;
	int refH = 0;
	std::string error;
	if (WriteMainWheelLayoutUserIni(state.displayW, state.displayH, refW, refH, error)) {
		logger::info("[MainWheel.LayoutScaling] Created user override at '{}' (Ref={}x{}, Enabled=true)",
			MAINWHEEL_LAYOUT_USER_PATH, refW, refH);

		CSimpleIniA userIni;
		userIni.SetUnicode();
		if (userIni.LoadFile(MAINWHEEL_LAYOUT_USER_PATH) >= 0) {
			ReadMainWheelLayoutScalingConfig(userIni, MAINWHEEL_LAYOUT_USER_PATH, "user");
			ReadAmmoWheelLayoutScalingConfig(userIni, MAINWHEEL_LAYOUT_USER_PATH, "user");
		}
	} else {
		g_layoutAutoCreateFailed = true;
		logger::warn("[MainWheel.LayoutScaling] Failed to create user override (reason={}). LayoutScaling will remain OFF.",
			error.empty() ? "unknown" : error);
	}
}

enum class ScaleAxis
{
	Uniform,
	X,
	Y
};

enum class ScaleGroup
{
	Geometry,
	Text,
	Style
};

struct ScaleEntry
{
	float* value = nullptr;
	float base = 0.0f;
	ScaleAxis axis = ScaleAxis::Uniform;
	ScaleGroup group = ScaleGroup::Geometry;
};

struct ScaleSet
{
	float scaleX = 1.0f;
	float scaleY = 1.0f;
	float scaleU = 1.0f;
};

static std::vector<ScaleEntry> g_scaleEntries;
static bool g_scaleEntriesInit = false;
static bool g_scaleBaseCaptured = false;
static std::vector<ScaleEntry> g_ammoScaleEntries;
static bool g_ammoScaleEntriesInit = false;
static bool g_ammoScaleBaseCaptured = false;
static std::unordered_map<std::string, Config::AmmoWheel::StylePreset> g_ammoPresetBase;
static Config::AmmoWheel::StylePreset g_ammoDefaultPresetBase;
static bool g_ammoPresetBaseCaptured = false;

static void EnsureScaleEntries()
{
	if (g_scaleEntriesInit) {
		return;
	}
	auto add = [&](float& value, ScaleAxis axis) {
		g_scaleEntries.push_back({ &value, value, axis });
	};

	add(Config::Control::Wheel::CursorRadiusPerEntry, ScaleAxis::Uniform);
	add(Config::Styling::Wheel::CursorIndicatorDist, ScaleAxis::Uniform);
	add(Config::Styling::Wheel::CusorIndicatorArcWidth, ScaleAxis::Uniform);
	add(Config::Styling::Wheel::CursorIndicatorTriangleSideLength, ScaleAxis::Uniform);
	add(Config::Styling::Wheel::WheelIndicatorOffsetX, ScaleAxis::X);
	add(Config::Styling::Wheel::WheelIndicatorOffsetY, ScaleAxis::Y);
	add(Config::Styling::Wheel::WheelIndicatorSize, ScaleAxis::Uniform);
	add(Config::Styling::Wheel::WheelIndicatorSpacing, ScaleAxis::Uniform);
	add(Config::Styling::Wheel::InnerCircleRadius, ScaleAxis::Uniform);
	add(Config::Styling::Wheel::OuterCircleRadius, ScaleAxis::Uniform);
	add(Config::Styling::Wheel::InnerSpacing, ScaleAxis::Uniform);
	add(Config::Styling::Wheel::ActiveArcWidth, ScaleAxis::Uniform);
	add(Config::Styling::Wheel::CenterOffsetX, ScaleAxis::X);
	add(Config::Styling::Wheel::CenterOffsetY, ScaleAxis::Y);
	add(Config::Styling::HoverDelay::Radius, ScaleAxis::Uniform);
	add(Config::Styling::HoverDelay::RadiusOffset, ScaleAxis::Uniform);
	add(Config::Styling::HoverDelay::Thickness, ScaleAxis::Uniform);
	add(Config::MainWheel::HandIndicators::Left.Thickness, ScaleAxis::Uniform);
	add(Config::MainWheel::HandIndicators::Right.Thickness, ScaleAxis::Uniform);
	add(Config::MainWheel::HandIndicators::Left.OffsetX, ScaleAxis::X);
	add(Config::MainWheel::HandIndicators::Left.OffsetY, ScaleAxis::Y);
	add(Config::MainWheel::HandIndicators::Left.SecondaryOffsetX, ScaleAxis::X);
	add(Config::MainWheel::HandIndicators::Left.SecondaryOffsetY, ScaleAxis::Y);
	add(Config::MainWheel::HandIndicators::Left.DualTopOffsetX, ScaleAxis::X);
	add(Config::MainWheel::HandIndicators::Left.DualTopOffsetY, ScaleAxis::Y);
	add(Config::MainWheel::HandIndicators::Left.SlotLeftOffsetX, ScaleAxis::X);
	add(Config::MainWheel::HandIndicators::Left.SlotLeftOffsetY, ScaleAxis::Y);
	add(Config::MainWheel::HandIndicators::Left.SlotRightOffsetX, ScaleAxis::X);
	add(Config::MainWheel::HandIndicators::Left.SlotRightOffsetY, ScaleAxis::Y);
	add(Config::MainWheel::HandIndicators::Right.OffsetX, ScaleAxis::X);
	add(Config::MainWheel::HandIndicators::Right.OffsetY, ScaleAxis::Y);
	add(Config::MainWheel::HandIndicators::Right.SecondaryOffsetX, ScaleAxis::X);
	add(Config::MainWheel::HandIndicators::Right.SecondaryOffsetY, ScaleAxis::Y);
	add(Config::MainWheel::HandIndicators::Right.DualTopOffsetX, ScaleAxis::X);
	add(Config::MainWheel::HandIndicators::Right.DualTopOffsetY, ScaleAxis::Y);
	add(Config::MainWheel::HandIndicators::Right.SlotLeftOffsetX, ScaleAxis::X);
	add(Config::MainWheel::HandIndicators::Right.SlotLeftOffsetY, ScaleAxis::Y);
	add(Config::MainWheel::HandIndicators::Right.SlotRightOffsetX, ScaleAxis::X);
	add(Config::MainWheel::HandIndicators::Right.SlotRightOffsetY, ScaleAxis::Y);
	add(Config::MainWheel::HandIndicators::Dual.Thickness, ScaleAxis::Uniform);
	add(Config::MainWheel::HandIndicators::Dual.OffsetX, ScaleAxis::X);
	add(Config::MainWheel::HandIndicators::Dual.OffsetY, ScaleAxis::Y);
	add(Config::MainWheel::HandIndicators::RightSideOffsetX, ScaleAxis::X);
	add(Config::MainWheel::HandIndicators::RightSideOffsetY, ScaleAxis::Y);
	add(Config::MainWheel::HandIndicators::DualRightSideOffsetX, ScaleAxis::X);
	add(Config::MainWheel::HandIndicators::DualRightSideOffsetY, ScaleAxis::Y);
	add(Config::Styling::HoverDelay::InstantSpellAssetOffsetX, ScaleAxis::X);
	add(Config::Styling::HoverDelay::InstantSpellAssetOffsetY, ScaleAxis::Y);
	add(Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetX, ScaleAxis::X);
	add(Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetY, ScaleAxis::Y);
	add(Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetX, ScaleAxis::X);
	add(Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetY, ScaleAxis::Y);
	add(Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetX, ScaleAxis::X);
	add(Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetY, ScaleAxis::Y);

	add(Config::Styling::Entry::Highlight::Text::OffsetX, ScaleAxis::X);
	add(Config::Styling::Entry::Highlight::Text::OffsetY, ScaleAxis::Y);
	add(Config::Styling::Entry::Highlight::Text::Size, ScaleAxis::Uniform);

	add(Config::Styling::Item::Highlight::Texture::OffsetX, ScaleAxis::X);
	add(Config::Styling::Item::Highlight::Texture::OffsetY, ScaleAxis::Y);
	add(Config::Styling::Item::Highlight::Texture::Scale, ScaleAxis::Uniform);

	add(Config::Styling::Item::Highlight::Text::OffsetX, ScaleAxis::X);
	add(Config::Styling::Item::Highlight::Text::OffsetY, ScaleAxis::Y);
	add(Config::Styling::Item::Highlight::Text::Size, ScaleAxis::Uniform);
	add(Config::Styling::Item::Highlight::Text::MinSize, ScaleAxis::Uniform);
	add(Config::Styling::Item::Highlight::Text::MaxWidth, ScaleAxis::X);

	add(Config::Styling::Item::Highlight::Desc::OffsetX, ScaleAxis::X);
	add(Config::Styling::Item::Highlight::Desc::OffsetY, ScaleAxis::Y);
	add(Config::Styling::Item::Highlight::Desc::Size, ScaleAxis::Uniform);
	add(Config::Styling::Item::Highlight::Desc::LineLength, ScaleAxis::X);
	add(Config::Styling::Item::Highlight::Desc::LineSpacing, ScaleAxis::Y);
	add(Config::Styling::Item::Highlight::Desc::MinSize, ScaleAxis::Uniform);
	add(Config::Styling::Item::Highlight::Desc::MaxHeight, ScaleAxis::Y);
	add(Config::Styling::Item::Highlight::Desc::BottomSafeMargin, ScaleAxis::Y);
	add(Config::Styling::Item::Highlight::Desc::ShiftUpThreshold, ScaleAxis::Y);
	add(Config::Styling::Item::Highlight::Desc::MaxShiftUp, ScaleAxis::Y);

	add(Config::Styling::Item::Highlight::StatIcon::OffsetX, ScaleAxis::X);
	add(Config::Styling::Item::Highlight::StatIcon::OffsetY, ScaleAxis::Y);
	add(Config::Styling::Item::Highlight::StatIcon::Scale, ScaleAxis::Uniform);

	add(Config::Styling::Item::Highlight::StatText::OffsetX, ScaleAxis::X);
	add(Config::Styling::Item::Highlight::StatText::OffsetY, ScaleAxis::Y);
	add(Config::Styling::Item::Highlight::StatText::Size, ScaleAxis::Uniform);

	add(Config::Styling::Item::Slot::Texture::OffsetX, ScaleAxis::X);
	add(Config::Styling::Item::Slot::Texture::OffsetY, ScaleAxis::Y);
	add(Config::Styling::Item::Slot::Texture::Scale, ScaleAxis::Uniform);

	add(Config::Styling::Item::Slot::Text::OffsetX, ScaleAxis::X);
	add(Config::Styling::Item::Slot::Text::OffsetY, ScaleAxis::Y);
	add(Config::Styling::Item::Slot::Text::Size, ScaleAxis::Uniform);
	add(Config::Styling::Item::Slot::Text::MinSize, ScaleAxis::Uniform);
	add(Config::Styling::Item::Slot::Text::MaxWidth, ScaleAxis::X);
	add(Config::Styling::Item::Slot::Text::MaxHeight, ScaleAxis::Y);
	add(Config::Styling::Item::Slot::Text::LineSpacing, ScaleAxis::Y);

	add(Config::Styling::Item::Slot::BackgroundTexture::Scale, ScaleAxis::Uniform);

	add(Config::Animation::ToggleVerticalFadeDistance, ScaleAxis::Y);
	add(Config::Animation::ToggleHorizontalFadeDistance, ScaleAxis::X);

	g_scaleEntriesInit = true;
}

static void CaptureScaleBaseValues()
{
	EnsureScaleEntries();
	for (auto& entry : g_scaleEntries) {
		if (entry.value) {
			entry.base = *entry.value;
		}
	}
	g_scaleBaseCaptured = true;
}

void Config::MainWheel::LayoutScaling::UpdateRuntimeState()
{
	auto& resolutionContext = ResolutionScale::Context::GetSingleton();
	resolutionContext.Update();
	const auto& state = resolutionContext.GetState();

	Runtime.DisplayW = state.displayW;
	Runtime.DisplayH = state.displayH;
	Runtime.GameW = state.gameW;
	Runtime.GameH = state.gameH;

	Runtime.MismatchActive = state.active;
	Runtime.Msx = state.active ? state.scaleX : 1.0f;
	Runtime.Msy = state.active ? state.scaleY : 1.0f;
	Runtime.Msu = state.active ? state.uniformScale : 1.0f;

	Runtime.LayoutActive = false;
	Runtime.Lsx = 1.0f;
	Runtime.Lsy = 1.0f;
	Runtime.Lsu = 1.0f;

	MaybeAutoCreateMainWheelLayoutUserIni(state);

	const bool wantsLayout = ConfigPresent && Enabled && !g_layoutAutoCreateFailed;
	if (wantsLayout) {
		if (RefW > 0.0f && RefH > 0.0f && state.displayW > 0.0f && state.displayH > 0.0f) {
			Runtime.Lsx = state.displayW / RefW;
			Runtime.Lsy = state.displayH / RefH;
			Runtime.Lsu = (std::min)(Runtime.Lsx, Runtime.Lsy);
			Runtime.LayoutActive = true;
		} else {
			static bool warnedInvalidRef = false;
			if (!warnedInvalidRef) {
				logger::warn("[MainWheel.LayoutScaling] Invalid reference or display size (Ref={}x{}, Display={}x{}). Layout scaling disabled.",
					RefW, RefH, state.displayW, state.displayH);
				warnedInvalidRef = true;
			}
		}
	}

	Runtime.CombinedX = Runtime.Lsx * Runtime.Msx;
	Runtime.CombinedY = Runtime.Lsy * Runtime.Msy;
	Runtime.CombinedU = Runtime.Lsu * Runtime.Msu;
}

void Config::AmmoWheel::LayoutScaling::UpdateRuntimeState()
{
	auto& resolutionContext = ResolutionScale::Context::GetSingleton();
	resolutionContext.Update();
	const auto& state = resolutionContext.GetState();

	Runtime.DisplayW = state.displayW;
	Runtime.DisplayH = state.displayH;
	Runtime.GameW = state.gameW;
	Runtime.GameH = state.gameH;

	Runtime.MismatchActive = state.active;
	Runtime.Msx = state.active ? state.scaleX : 1.0f;
	Runtime.Msy = state.active ? state.scaleY : 1.0f;
	Runtime.Msu = state.active ? state.uniformScale : 1.0f;

	Runtime.LayoutActive = false;
	Runtime.Lsx = 1.0f;
	Runtime.Lsy = 1.0f;
	Runtime.Lsu = 1.0f;

	MaybeAutoCreateMainWheelLayoutUserIni(state);

	const bool wantsLayout = ConfigPresent && Enabled && !g_layoutAutoCreateFailed;
	if (wantsLayout) {
		if (RefW > 0.0f && RefH > 0.0f && state.displayW > 0.0f && state.displayH > 0.0f) {
			Runtime.Lsx = state.displayW / RefW;
			Runtime.Lsy = state.displayH / RefH;
			Runtime.Lsu = (std::min)(Runtime.Lsx, Runtime.Lsy);
			Runtime.LayoutActive = true;
		} else {
			static bool warnedInvalidRef = false;
			if (!warnedInvalidRef) {
				logger::warn("[AmmoWheel.LayoutScaling] Invalid reference or display size (Ref={}x{}, Display={}x{}). Layout scaling disabled.",
					RefW, RefH, state.displayW, state.displayH);
				warnedInvalidRef = true;
			}
		}
	}

	Runtime.CombinedX = Runtime.Lsx * Runtime.Msx;
	Runtime.CombinedY = Runtime.Lsy * Runtime.Msy;
	Runtime.CombinedU = Runtime.Lsu * Runtime.Msu;
}

static void EnsureAmmoScaleEntries()
{
	if (g_ammoScaleEntriesInit) {
		return;
	}
	auto add = [&](float& value, ScaleAxis axis, ScaleGroup group) {
		g_ammoScaleEntries.push_back({ &value, value, axis, group });
	};

	add(Config::AmmoWheel::WheelRadius, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::SlotCornerRadius, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::CountFontSize, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::IconSize, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::IconSizePx, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::IconRadialOffsetPx, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::TextRadialOffsetPx, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::CenterPaddingPx, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::CenterLineSpacingPx, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::CenterPanelSafeMargin, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::NameFontPx, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::CountFontPx, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::CenterFontPx, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::PopupIconSizePx, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::PopupNameFontPx, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::PopupCountFontPx, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::PopupOffsetPx, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::PopupPaddingPx, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::PopupBubbleRadius, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::NameMinFontPx, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::NameMaxWidthPx, ScaleAxis::X, ScaleGroup::Text);
	add(Config::AmmoWheel::NamePanelPaddingPx, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::NameLineSpacingPx, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::NameMarginPx, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::NameTextBgCornerRounding, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::NameTextBgExtraPaddingPx, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::NameTextBgInsetPx, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::NameBoldStrengthPx, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::SlotShadowOffsetX, ScaleAxis::X, ScaleGroup::Geometry);
	add(Config::AmmoWheel::SlotShadowOffsetY, ScaleAxis::Y, ScaleGroup::Geometry);
	add(Config::AmmoWheel::SlotHighlightThickness, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::TextShadowOffset, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::HoverPulseSize, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::SlotDividerThickness, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::CenterCornerSize, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::LowAmmoIndicatorThickness, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::LowAmmoIndicatorRadialOffsetPx, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::SelectedIndicatorThickness, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::CenterPanelCornerRounding, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::CenterPanelBorderThickness, ScaleAxis::Uniform, ScaleGroup::Geometry);
	add(Config::AmmoWheel::CenterPanelOffsetX, ScaleAxis::X, ScaleGroup::Geometry);
	add(Config::AmmoWheel::CenterPanelOffsetY, ScaleAxis::Y, ScaleGroup::Geometry);
	add(Config::AmmoWheel::CenterTextMinFontSize, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::CenterTextMaxFontSize, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::CenterTextOffsetX, ScaleAxis::X, ScaleGroup::Text);
	add(Config::AmmoWheel::CenterTextOffsetY, ScaleAxis::Y, ScaleGroup::Text);
	add(Config::AmmoWheel::WrapMaxLineWidthPx, ScaleAxis::X, ScaleGroup::Text);
	add(Config::AmmoWheel::WrapSafeMarginPx, ScaleAxis::Uniform, ScaleGroup::Text);
	add(Config::AmmoWheel::PopupAnim::BorderThickness, ScaleAxis::Uniform, ScaleGroup::Geometry);

	add(Config::AmmoWheel::Skin::SlotInnerRadiusPadding, ScaleAxis::Uniform, ScaleGroup::Style);
	add(Config::AmmoWheel::Skin::SlotOuterRadiusPadding, ScaleAxis::Uniform, ScaleGroup::Style);
	add(Config::AmmoWheel::Skin::SlotCornerRounding, ScaleAxis::Uniform, ScaleGroup::Style);
	add(Config::AmmoWheel::Skin::TextSize, ScaleAxis::Uniform, ScaleGroup::Style);
	add(Config::AmmoWheel::Skin::TextShadowOffsetX, ScaleAxis::X, ScaleGroup::Style);
	add(Config::AmmoWheel::Skin::TextShadowOffsetY, ScaleAxis::Y, ScaleGroup::Style);
	add(Config::AmmoWheel::Skin::IconPaddingPixels, ScaleAxis::Uniform, ScaleGroup::Style);
	add(Config::AmmoWheel::Skin::SelectedThicknessPx, ScaleAxis::Uniform, ScaleGroup::Style);
	add(Config::AmmoWheel::Skin::SelectedRadiusOffsetPx, ScaleAxis::Uniform, ScaleGroup::Style);
	add(Config::AmmoWheel::Skin::HoveredThicknessPx, ScaleAxis::Uniform, ScaleGroup::Style);
	add(Config::AmmoWheel::Skin::HoveredRadiusOffsetPx, ScaleAxis::Uniform, ScaleGroup::Style);
	add(Config::AmmoWheel::Skin::ActiveThicknessPx, ScaleAxis::Uniform, ScaleGroup::Style);
	add(Config::AmmoWheel::Skin::ActiveRadiusOffsetPx, ScaleAxis::Uniform, ScaleGroup::Style);
	add(Config::AmmoWheel::Skin::ChargeThicknessPx, ScaleAxis::Uniform, ScaleGroup::Style);
	add(Config::AmmoWheel::Skin::ChargeRadiusOffsetPx, ScaleAxis::Uniform, ScaleGroup::Style);

	g_ammoScaleEntriesInit = true;
}

static void CaptureAmmoScaleBaseValues()
{
	EnsureAmmoScaleEntries();
	for (auto& entry : g_ammoScaleEntries) {
		if (entry.value) {
			entry.base = *entry.value;
		}
	}
	g_ammoScaleBaseCaptured = true;

	g_ammoPresetBase = Config::AmmoWheel::PresetSystem::LoadedPresets;
	g_ammoDefaultPresetBase = Config::AmmoWheel::PresetSystem::DefaultPreset;
	g_ammoPresetBaseCaptured = true;
}

static void ApplyAmmoWheelPerformanceTier()
{
	using Config::AmmoWheel::Performance::OverrideAnimations;
	using Config::AmmoWheel::Performance::OverrideCenterPanel;
	using Config::AmmoWheel::Performance::OverrideIndicators;
	using Config::AmmoWheel::Performance::OverrideLabels;
	using Config::AmmoWheel::Performance::OverridePopup;
	using Config::AmmoWheel::Performance::OverrideVisualPolish;

	const int tier = Config::AmmoWheel::Performance::Tier;
	if (tier <= 0) {
		return;
	}

	const bool visualOverride = OverrideVisualPolish;
	const bool animOverride = OverrideAnimations;
	const bool popupOverride = OverridePopup;
	const bool labelOverride = OverrideLabels;
	const bool centerOverride = OverrideCenterPanel;
	const bool indicatorOverride = OverrideIndicators;

	if (tier == 1) {  // Low
		if (!visualOverride) {
			Config::AmmoWheel::BackgroundEnabled = false;
			Config::AmmoWheel::BorderEnabled = false;
			Config::AmmoWheel::SlotShadowEnabled = false;
			Config::AmmoWheel::SlotHighlightEnabled = false;
			Config::AmmoWheel::TextShadowEnabled = false;
			Config::AmmoWheel::TextHoverGlowEnabled = false;
			Config::AmmoWheel::IconHoverGlow = false;
		}
		if (!animOverride) {
			Config::AmmoWheel::HoverPulseEnabled = false;
			Config::AmmoWheel::SlotDividersEnabled = false;
			Config::AmmoWheel::SlotDividerReskinBreathingEnabled = false;
			Config::AmmoWheel::CenterFrameEnabled = false;
			Config::AmmoWheel::CenterFramePulse = false;
			Config::AmmoWheel::CenterCornersEnabled = false;
		}
		if (!popupOverride) {
			Config::AmmoWheel::PopupEnabled = false;
			Config::AmmoWheel::PopupFlipbookEnabled = false;
			Config::AmmoWheel::PopupAnim::Enabled = false;
		}
		if (!labelOverride) {
			Config::AmmoWheel::LabelMultiLine = false;
			Config::AmmoWheel::NameLayoutMode = 0;
			Config::AmmoWheel::NameMaxLines = 1;
			Config::AmmoWheel::LabelTruncateLength = (std::min)(Config::AmmoWheel::LabelTruncateLength, 12);
			Config::AmmoWheel::NameTextBgEnabled = false;
		}
		if (!centerOverride) {
			Config::AmmoWheel::CenterEnabled = false;
		}
		if (!indicatorOverride) {
			Config::AmmoWheel::HoverBrightnessEnabled = false;
			Config::AmmoWheel::SelectedBlinkEnabled = false;
		}
	} else if (tier == 2) {  // Balanced
		if (!visualOverride) {
			Config::AmmoWheel::SlotShadowEnabled = false;
			Config::AmmoWheel::TextHoverGlowEnabled = false;
			Config::AmmoWheel::IconHoverGlow = false;
		}
		if (!animOverride) {
			Config::AmmoWheel::HoverPulseEnabled = false;
			Config::AmmoWheel::CenterFramePulse = false;
		}
		if (!popupOverride) {
			Config::AmmoWheel::PopupFlipbookEnabled = false;
		}
	} else {
		// High: no overrides beyond user config
	}
}

static void ReadWheelBehaviorConfigFromIni(const CSimpleIniA& ini)
{
	// ========== MASTER ENABLE AND CONTROLLER PASSTHROUGH ==========
	// These are critical for vanilla controller compatibility
	GetBoolValue(ini, "General", "WheelerEnabled", Config::WheelerEnabled);
	GetBoolValue(ini, "General", "VanillaLTPassthrough", Config::VanillaLTPassthrough);
	GetBoolValue(ini, "General", "WarnOnLTBinding", Config::WarnOnLTBinding);
	
	// Also check WheelBehavior section for these (alternative location)
	GetBoolValue(ini, "WheelBehavior", "WheelerEnabled", Config::WheelerEnabled);
	GetBoolValue(ini, "WheelBehavior", "VanillaLTPassthrough", Config::VanillaLTPassthrough);
	GetBoolValue(ini, "WheelBehavior", "WarnOnLTBinding", Config::WarnOnLTBinding);
	
	auto readSection = [&](const char* section) {
		GetBoolValue(ini, section, "ReleaseToUse", Config::WheelBehavior::ReleaseToUse);
		GetBoolValue(ini, section, "CloseWheelAfterUse", Config::WheelBehavior::CloseWheelAfterUse);
		GetBoolValue(ini, section, "RTUAlchemy", Config::WheelBehavior::RTUAlchemy);
		GetBoolValue(ini, section, "RTUSpell", Config::WheelBehavior::RTUSpell);
		GetBoolValue(ini, section, "RTUShout", Config::WheelBehavior::RTUShout);
		GetBoolValue(ini, section, "RTUSmartAssignEnabled", Config::WheelBehavior::RTUSmartAssignEnabled);
		{
			std::uint32_t tmp = Config::WheelBehavior::RTUSmartAssignTargetHands;
			if (!GetUInt32Value(ini, section, "RTUSmartAssignTargetHands", tmp)) {
				float tmpFloat = static_cast<float>(tmp);
				if (GetFloatValue(ini, section, "RTUSmartAssignTargetHands", tmpFloat)) {
					tmp = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
				}
			}
			Config::WheelBehavior::RTUSmartAssignTargetHands = tmp;
		}
		{
			std::uint32_t tmp = Config::WheelBehavior::RTUSmartAssignCategoryMask;
			if (!GetUInt32Value(ini, section, "RTUSmartAssignCategoryMask", tmp)) {
				float tmpFloat = static_cast<float>(tmp);
				if (GetFloatValue(ini, section, "RTUSmartAssignCategoryMask", tmpFloat)) {
					tmp = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
				}
			}
			Config::WheelBehavior::RTUSmartAssignCategoryMask = tmp;
		}
		{
			std::uint32_t tmp = Config::WheelBehavior::RTUSmartAssignBothEmptyPriority;
			if (!GetUInt32Value(ini, section, "RTUSmartAssignBothEmptyPriority", tmp)) {
				float tmpFloat = static_cast<float>(tmp);
				if (GetFloatValue(ini, section, "RTUSmartAssignBothEmptyPriority", tmpFloat)) {
					tmp = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
				}
			}
			Config::WheelBehavior::RTUSmartAssignBothEmptyPriority = tmp;
		}
		{
			std::uint32_t tmp = Config::WheelBehavior::RTUSmartAssignOverwriteMode;
			if (!GetUInt32Value(ini, section, "RTUSmartAssignOverwriteMode", tmp)) {
				float tmpFloat = static_cast<float>(tmp);
				if (GetFloatValue(ini, section, "RTUSmartAssignOverwriteMode", tmpFloat)) {
					tmp = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
				}
			}
			Config::WheelBehavior::RTUSmartAssignOverwriteMode = tmp;
		}
		GetBoolValue(ini, section, "RTUSmartAssignDebugLog", Config::WheelBehavior::RTUSmartAssignDebugLog);
		GetFloatValue(ini, section, "HoverActivateDelaySeconds", Config::WheelBehavior::HoverActivateDelaySeconds);
		GetBoolValue(ini, section, "AutoDrawOnUse", Config::WheelBehavior::AutoDrawOnUse);
		GetBoolValue(ini, section, "InstantSpell", Config::WheelBehavior::InstantSpell);
		GetBoolValue(ini, section, "InstantSpellUseDirectCast", Config::WheelBehavior::InstantSpellUseDirectCast);
		GetBoolValue(ini, section, "RTUAutoInstantSpell", Config::WheelBehavior::RTUAutoInstantSpell);
		GetBoolValue(ini, section, "InstantPowers", Config::WheelBehavior::InstantPowers);
		{
			// dMenu sliders write floating point values, so read as float and cast
			float tempMode = static_cast<float>(Config::WheelBehavior::InstantSpellConcentrationMode);
			if (GetFloatValue(ini, section, "InstantSpellConcentrationMode", tempMode)) {
				Config::WheelBehavior::InstantSpellConcentrationMode = static_cast<std::uint32_t>(tempMode);
			}
		}
		GetFloatValue(ini, section, "InstantSpellConcentrationMaxSeconds", Config::WheelBehavior::InstantSpellConcentrationMaxSeconds);
		GetBoolValue(ini, section, "InstantSpellDebugLog", Config::WheelBehavior::InstantSpellDebugLog);
		GetFloatValue(ini, section, "InstantSpellHoldThresholdMs", Config::WheelBehavior::InstantSpellHoldThresholdMs);
		GetFloatValue(ini, section, "HoldToCastSafetyThresholdMs", Config::WheelBehavior::HoldToCastSafetyThresholdMs);
		Config::WheelBehavior::HoldToCastSafetyThresholdMs = std::clamp(Config::WheelBehavior::HoldToCastSafetyThresholdMs, 100.0f, 2000.0f);
		GetBoolValue(ini, section, "ShoutPipelineDebug", Config::WheelBehavior::ShoutPipelineDebug);
		GetBoolValue(ini, section, "InstantShout", Config::WheelBehavior::InstantShout);
		GetBoolValue(ini, section, "RTUAutoInstantShout", Config::WheelBehavior::RTUAutoInstantShout);
		GetFloatValue(ini, section, "ShoutWord2Threshold", Config::WheelBehavior::ShoutWord2Threshold);
		GetFloatValue(ini, section, "ShoutWord3Threshold", Config::WheelBehavior::ShoutWord3Threshold);
		GetFloatValue(ini, section, "ShoutHoldSecsWord1", Config::WheelBehavior::ShoutHoldSecsWord1);
		GetFloatValue(ini, section, "ShoutHoldSecsWord2", Config::WheelBehavior::ShoutHoldSecsWord2);
		GetFloatValue(ini, section, "ShoutHoldSecsWord3", Config::WheelBehavior::ShoutHoldSecsWord3);
		GetBoolValue(ini, section, "ShoutIgnoreRTUDelay", Config::WheelBehavior::ShoutIgnoreRTUDelay);
		GetFloatValue(ini, section, "ShoutStageFillSecs", Config::WheelBehavior::ShoutStageFillSecs);
		GetFloatValue(ini, section, "ShoutStageHoldSecs1", Config::WheelBehavior::ShoutStageHoldSecs1);
		GetFloatValue(ini, section, "ShoutStageHoldSecs2", Config::WheelBehavior::ShoutStageHoldSecs2);
		// Shout stage indicator colors (3-segment ring)
		GetUInt32Value(ini, section, "ShoutStageColor1", Config::WheelBehavior::ShoutStageColor1);
		GetUInt32Value(ini, section, "ShoutStageColor2", Config::WheelBehavior::ShoutStageColor2);
		GetUInt32Value(ini, section, "ShoutStageColor3", Config::WheelBehavior::ShoutStageColor3);
		// Allow 3-phase shout indicator when RTU is off
		GetBoolValue(ini, section, "ShowIndicatorWhenRTUOff", Config::WheelBehavior::ShowIndicatorWhenRTUOff);
		GetBoolValue(ini, section, "ClearDepletedConsumables", Config::WheelBehavior::ClearDepletedConsumables);
		GetBoolValue(ini, section, "AllowIngredientUse", Config::WheelBehavior::AllowIngredientUse);
		GetBoolValue(ini, section, "AllowUnsafeMiscActivation", Config::WheelBehavior::AllowUnsafeMiscActivation);
		{
			std::uint32_t tmp = Config::WheelBehavior::ScriptedMiscDispatchModeValue;
			if (!GetUInt32Value(ini, section, "ScriptedMiscDispatchMode", tmp)) {
				float tmpFloat = static_cast<float>(tmp);
				if (GetFloatValue(ini, section, "ScriptedMiscDispatchMode", tmpFloat)) {
					tmp = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
				}
			}
			Config::WheelBehavior::ScriptedMiscDispatchModeValue = std::clamp(tmp, 0u, 4u);
		}
		GetBoolValue(ini, section, "RTUAntiSlipEnabled", Config::WheelBehavior::RTUAntiSlipEnabled);
		GetFloatValue(ini, section, "RTUAntiSlipStrength", Config::WheelBehavior::RTUAntiSlipStrength);
		GetBoolValue(ini, section, "LootMenuOverride", Config::WheelBehavior::LootMenuOverride);
		GetBoolValue(ini, section, "HeavyListCompatibilityMode", Config::WheelBehavior::HeavyListCompatibilityMode);
		GetFloatValue(ini, section, "HeavyListCompatibilitySettleMs", Config::WheelBehavior::HeavyListCompatibilitySettleMs);
		Config::WheelBehavior::HeavyListCompatibilitySettleMs = std::clamp(Config::WheelBehavior::HeavyListCompatibilitySettleMs, 150.0f, 1000.0f);
		GetBoolValue(ini, section, "MutableInventoryHooks", Config::WheelBehavior::MutableInventoryHooks);
		
		// Global scale for entire wheel (user preference for 4K/1440p etc.)
		GetFloatValue(ini, section, "GlobalScale", Config::WheelBehavior::GlobalScale);
		Config::WheelBehavior::GlobalScale = std::clamp(Config::WheelBehavior::GlobalScale, 0.5f, 2.0f);
		
		// Max items per slot (deserialization limit)
		{
			std::uint32_t tmpMax = static_cast<std::uint32_t>(Config::WheelBehavior::MaxItemsPerSlot);
			if (!GetUInt32Value(ini, section, "MaxItemsPerSlot", tmpMax)) {
				float tmpFloat = static_cast<float>(tmpMax);
				if (GetFloatValue(ini, section, "MaxItemsPerSlot", tmpFloat)) {
					tmpMax = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
				}
			}
			Config::WheelBehavior::MaxItemsPerSlot = static_cast<int>(std::clamp(tmpMax, 10u, 64u));
		}
	};

	// Backward compatibility: read legacy section first, then allow the new section to override it.
	readSection("InstantUse");
	readSection("WheelBehavior");

	Config::WheelBehavior::BookReadCompat::Mode = ReadBookReadCompatModeValue(
		ini, "BookReadCompat", "Mode", Config::WheelBehavior::BookReadCompat::Mode);
	GetStringValue(ini, "BookReadCompat", "OnReadFormIDs", Config::WheelBehavior::BookReadCompat::OnReadFormIDs);
	GetStringValue(ini, "BookReadCompat", "OnReadPlugins", Config::WheelBehavior::BookReadCompat::OnReadPlugins);
	GetStringValue(ini, "BookReadCompat", "OnReadNameTokens", Config::WheelBehavior::BookReadCompat::OnReadNameTokens);
	GetBoolValue(ini, "BookReadCompat", "DebugLog", Config::WheelBehavior::BookReadCompat::DebugLog);

	// RTU SmartAssign: optional per-category flags (dMenu writes these)
	{
		auto hasKey = [&](const char* section, const char* key) {
			return ini.GetValue(section, key, nullptr) != nullptr;
		};
		const bool hasMaskKey = hasKey("WheelBehavior", "RTUSmartAssignCategoryMask") || hasKey("InstantUse", "RTUSmartAssignCategoryMask");

		bool catSpells = true;
		bool catWeapons1H = true;
		bool catStaffs = false;
		bool catShields = false;
		bool catTorches = false;

		bool hasCatKeys = false;
		auto readCat = [&](const char* key, bool& value) {
			if (hasKey("WheelBehavior.RTUSmartAssign", key)) {
				hasCatKeys = true;
			}
			GetBoolValue(ini, "WheelBehavior.RTUSmartAssign", key, value);
		};

		readCat("CatSpells", catSpells);
		readCat("CatWeapons1H", catWeapons1H);
		readCat("CatStaffs", catStaffs);
		readCat("CatShields", catShields);
		readCat("CatTorches", catTorches);

		if (!hasMaskKey && hasCatKeys) {
			std::uint32_t mask = 0;
			if (catSpells) {
				mask |= static_cast<std::uint32_t>(Config::WheelBehavior::RTUSmartAssignCategory::Spells);
			}
			if (catWeapons1H) {
				mask |= static_cast<std::uint32_t>(Config::WheelBehavior::RTUSmartAssignCategory::Weapons1H);
			}
			if (catStaffs) {
				mask |= static_cast<std::uint32_t>(Config::WheelBehavior::RTUSmartAssignCategory::Staffs);
			}
			if (catShields) {
				mask |= static_cast<std::uint32_t>(Config::WheelBehavior::RTUSmartAssignCategory::Shields);
			}
			if (catTorches) {
				mask |= static_cast<std::uint32_t>(Config::WheelBehavior::RTUSmartAssignCategory::Torches);
			}
			Config::WheelBehavior::RTUSmartAssignCategoryMask = mask;
		}
	}

	Config::WheelBehavior::RTUSmartAssignTargetHands =
		std::clamp(Config::WheelBehavior::RTUSmartAssignTargetHands, 0u, 3u);
	Config::WheelBehavior::RTUSmartAssignBothEmptyPriority =
		std::clamp(Config::WheelBehavior::RTUSmartAssignBothEmptyPriority, 1u, 2u);
	Config::WheelBehavior::RTUSmartAssignOverwriteMode =
		std::clamp(Config::WheelBehavior::RTUSmartAssignOverwriteMode, 0u, 3u);
	Config::WheelBehavior::RTUSmartAssignCategoryMask &=
		static_cast<std::uint32_t>(Config::WheelBehavior::RTUSmartAssignCategory::Spells) |
		static_cast<std::uint32_t>(Config::WheelBehavior::RTUSmartAssignCategory::Weapons1H) |
		static_cast<std::uint32_t>(Config::WheelBehavior::RTUSmartAssignCategory::Staffs) |
		static_cast<std::uint32_t>(Config::WheelBehavior::RTUSmartAssignCategory::Shields) |
		static_cast<std::uint32_t>(Config::WheelBehavior::RTUSmartAssignCategory::Torches);

	// Transformation spell handling for InstantSpell (simplified toggle)
	GetBoolValue(ini, "WheelBehavior", "InstantTransformations", Config::WheelBehavior::InstantTransformations);
	GetBoolValue(ini, "WheelBehavior", "InstantTransformationsDebugLog", Config::WheelBehavior::InstantTransformationsDebugLog);
	GetStringValue(ini, "WheelBehavior", "TransformationAllowFormIDs", Config::WheelBehavior::TransformationAllowFormIDs);
	GetStringValue(ini, "WheelBehavior", "TransformationDenyFormIDs", Config::WheelBehavior::TransformationDenyFormIDs);

	// Keep missing items (main wheel only)
	GetBoolValue(ini, "WheelBehavior.KeepMissing", "Enabled", Config::WheelBehavior::KeepMissing::Enabled);
	GetBoolValue(ini, "WheelBehavior.KeepMissing", "KeepConsumables", Config::WheelBehavior::KeepMissing::KeepConsumables);
	GetBoolValue(ini, "WheelBehavior.KeepMissing", "KeepGears", Config::WheelBehavior::KeepMissing::KeepGears);
	GetBoolValue(ini, "WheelBehavior.KeepMissing", "KeepThrowableMods", Config::WheelBehavior::KeepMissing::KeepThrowableMods);
	// Vanilla-style equipped hand memory (left/right restore when leaving 2H)
	GetBoolValue(ini, "WheelBehavior.HandMemory", "Enabled", Config::WheelBehavior::HandMemory::Enabled);
	GetBoolValue(ini, "WheelBehavior.HandMemory", "DebugLog", Config::WheelBehavior::HandMemory::DebugLog);
	GetFloatValue(ini, "WheelBehavior.HandMemory", "RestoreDelaySeconds", Config::WheelBehavior::HandMemory::RestoreDelaySeconds);
	GetFloatValue(ini, "WheelBehavior.HandMemory", "RestoreWindowSeconds", Config::WheelBehavior::HandMemory::RestoreWindowSeconds);
	GetBoolValue(ini, "WheelBehavior.HandMemory", "RestoreLeftIfEmpty", Config::WheelBehavior::HandMemory::RestoreLeftIfEmpty);
	GetBoolValue(ini, "WheelBehavior.HandMemory", "RestoreRightIfEmpty", Config::WheelBehavior::HandMemory::RestoreRightIfEmpty);
	Config::WheelBehavior::HandMemory::RestoreDelaySeconds =
		(std::max)(0.0f, Config::WheelBehavior::HandMemory::RestoreDelaySeconds);
	Config::WheelBehavior::HandMemory::RestoreWindowSeconds =
		(std::max)(0.0f, Config::WheelBehavior::HandMemory::RestoreWindowSeconds);


	GetBoolValue(ini, "TransformWheels", "Enabled", Config::WheelBehavior::TransformWheels::Enabled);
	{
		float tmpMode = static_cast<float>(Config::WheelBehavior::TransformWheels::Mode);
		if (GetFloatValue(ini, "TransformWheels", "Mode", tmpMode)) {
			Config::WheelBehavior::TransformWheels::Mode =
				static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpMode)));
		}
	}
	GetBoolValue(ini, "TransformWheels", "RestorePreviousWheel", Config::WheelBehavior::TransformWheels::RestorePreviousWheel);
	GetBoolValue(ini, "TransformWheels", "IncludeShouts", Config::WheelBehavior::TransformWheels::IncludeShouts);
	{
		std::uint32_t tmpThrottle = Config::WheelBehavior::TransformWheels::RegenerateThrottleMs;
		if (!GetUInt32Value(ini, "TransformWheels", "RegenerateThrottleMs", tmpThrottle)) {
			float tmpFloat = static_cast<float>(tmpThrottle);
			if (GetFloatValue(ini, "TransformWheels", "RegenerateThrottleMs", tmpFloat)) {
				tmpThrottle = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
			}
		}
		Config::WheelBehavior::TransformWheels::RegenerateThrottleMs =
			std::clamp(tmpThrottle, 0u, 10000u);
	}
	{
		std::uint32_t tmpRetry = Config::WheelBehavior::TransformWheels::RetryWindowMs;
		if (!GetUInt32Value(ini, "TransformWheels", "RetryWindowMs", tmpRetry)) {
			float tmpFloat = static_cast<float>(tmpRetry);
			if (GetFloatValue(ini, "TransformWheels", "RetryWindowMs", tmpFloat)) {
				tmpRetry = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
			}
		}
		Config::WheelBehavior::TransformWheels::RetryWindowMs =
			std::clamp(tmpRetry, 0u, 60000u);
	}
	{
		std::uint32_t tmpStable = Config::WheelBehavior::TransformWheels::StableTicks;
		if (!GetUInt32Value(ini, "TransformWheels", "StableTicks", tmpStable)) {
			float tmpFloat = static_cast<float>(tmpStable);
			if (GetFloatValue(ini, "TransformWheels", "StableTicks", tmpFloat)) {
				tmpStable = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
			}
		}
		Config::WheelBehavior::TransformWheels::StableTicks =
			std::clamp(tmpStable, 1u, 10u);
	}
	{
		std::uint32_t tmpMin = Config::WheelBehavior::TransformWheels::MinEntries;
		if (!GetUInt32Value(ini, "TransformWheels", "MinEntries", tmpMin)) {
			float tmpFloat = static_cast<float>(tmpMin);
			if (GetFloatValue(ini, "TransformWheels", "MinEntries", tmpFloat)) {
				tmpMin = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
			}
		}
		Config::WheelBehavior::TransformWheels::MinEntries =
			std::clamp(tmpMin, 1u, 64u);
	}
	GetBoolValue(ini, "TransformWheels", "CacheHumanSnapshot", Config::WheelBehavior::TransformWheels::CacheHumanSnapshot);
	GetBoolValue(ini, "TransformWheels", "PersistGeneratedWheels", Config::WheelBehavior::TransformWheels::PersistGeneratedWheels);
	GetBoolValue(ini, "TransformWheels", "UpdateOnlyOnChange", Config::WheelBehavior::TransformWheels::UpdateOnlyOnChange);
	GetBoolValue(ini, "TransformWheels", "DebugLog", Config::WheelBehavior::TransformWheels::DebugLog);
	GetBoolValue(ini, "TransformWheels", "GenericEnabled", Config::WheelBehavior::TransformWheels::GenericEnabled);
	GetStringValue(ini, "TransformWheels", "GenericStateID", Config::WheelBehavior::TransformWheels::GenericStateID);
	GetStringValue(ini, "TransformWheels", "GenericWheelID", Config::WheelBehavior::TransformWheels::GenericWheelID);
	GetStringValue(ini, "TransformWheels", "GenericRaceEditorIDContains", Config::WheelBehavior::TransformWheels::GenericRaceEditorIDContains);
	GetStringValue(ini, "TransformWheels", "GenericRaceKeywords", Config::WheelBehavior::TransformWheels::GenericRaceKeywords);
	GetStringValue(ini, "TransformWheels", "GenericRaceFormIDs", Config::WheelBehavior::TransformWheels::GenericRaceFormIDs);
	GetStringValue(ini, "TransformWheels", "PrecedenceOrder", Config::WheelBehavior::TransformWheels::PrecedenceOrder);
	GetBoolValue(ini, "TransformWheels", "WerewolfAllowBaseWheel", Config::WheelBehavior::TransformWheels::WerewolfAllowBaseWheel);
	GetBoolValue(ini, "TransformWheels", "VampireLordAllowBaseWheel", Config::WheelBehavior::TransformWheels::VampireLordAllowBaseWheel);
	GetBoolValue(ini, "TransformWheels", "WerewolfAllowBaseWheelSpells", Config::WheelBehavior::TransformWheels::WerewolfAllowBaseWheelSpells);
	GetBoolValue(ini, "TransformWheels", "WerewolfAllowBaseWheelShouts", Config::WheelBehavior::TransformWheels::WerewolfAllowBaseWheelShouts);
	GetBoolValue(ini, "WerewolfForm", "Enabled", Config::WheelBehavior::TransformWheels::WerewolfForm::Enabled);
	GetStringValue(ini, "WerewolfForm", "PopulateMode", Config::WheelBehavior::TransformWheels::WerewolfForm::PopulateMode);
	GetStringValue(ini, "WerewolfForm", "RaceEditorIDContains", Config::WheelBehavior::TransformWheels::WerewolfForm::RaceEditorIDContains);
	GetStringValue(ini, "WerewolfForm", "RaceKeywords", Config::WheelBehavior::TransformWheels::WerewolfForm::RaceKeywords);
	GetStringValue(ini, "WerewolfForm", "RaceFormIDs", Config::WheelBehavior::TransformWheels::WerewolfForm::RaceFormIDs);
	GetStringValue(ini, "WerewolfForm", "SpellTokens", Config::WheelBehavior::TransformWheels::WerewolfForm::SpellTokens);
	GetStringValue(ini, "WerewolfForm", "ExitSpellTokens", Config::WheelBehavior::TransformWheels::WerewolfForm::ExitSpellTokens);
	GetStringValue(ini, "WerewolfForm", "AdditionalSpellFormIDs", Config::WheelBehavior::TransformWheels::WerewolfForm::AdditionalSpellFormIDs);
	GetBoolValue(ini, "WerewolfForm", "DebugLog", Config::WheelBehavior::TransformWheels::WerewolfForm::DebugLog);
	GetBoolValue(ini, "VampireLordForm", "Enabled", Config::WheelBehavior::TransformWheels::VampireLordForm::Enabled);
	GetStringValue(ini, "VampireLordForm", "PopulateMode", Config::WheelBehavior::TransformWheels::VampireLordForm::PopulateMode);
	GetBoolValue(ini, "VampireLordForm", "HideTransformSpell", Config::WheelBehavior::TransformWheels::VampireLordForm::HideTransformSpell);
	GetBoolValue(ini, "VampireLordForm", "HideForcedRightHandSpells", Config::WheelBehavior::TransformWheels::VampireLordForm::HideForcedRightHandSpells);
	GetBoolValue(ini, "VampireLordForm", "BlockHiddenSpellActivation", Config::WheelBehavior::TransformWheels::VampireLordForm::BlockHiddenSpellActivation);
	GetBoolValue(ini, "VampireLordForm", "BlockRegularSpellsInMeleeMode", Config::WheelBehavior::TransformWheels::VampireLordForm::BlockRegularSpellsInMeleeMode);
	GetStringValue(ini, "VampireLordForm", "RaceEditorIDContains", Config::WheelBehavior::TransformWheels::VampireLordForm::RaceEditorIDContains);
	GetStringValue(ini, "VampireLordForm", "RaceKeywords", Config::WheelBehavior::TransformWheels::VampireLordForm::RaceKeywords);
	GetStringValue(ini, "VampireLordForm", "RaceFormIDs", Config::WheelBehavior::TransformWheels::VampireLordForm::RaceFormIDs);
	GetStringValue(ini, "VampireLordForm", "SpellTokens", Config::WheelBehavior::TransformWheels::VampireLordForm::SpellTokens);
	GetStringValue(ini, "VampireLordForm", "ExitSpellTokens", Config::WheelBehavior::TransformWheels::VampireLordForm::ExitSpellTokens);
	GetStringValue(ini, "VampireLordForm", "AdditionalSpellFormIDs", Config::WheelBehavior::TransformWheels::VampireLordForm::AdditionalSpellFormIDs);
	GetStringValue(ini, "VampireLordForm", "HiddenSpellFormIDs", Config::WheelBehavior::TransformWheels::VampireLordForm::HiddenSpellFormIDs);
	GetStringValue(ini, "VampireLordForm", "HiddenSpellTokens", Config::WheelBehavior::TransformWheels::VampireLordForm::HiddenSpellTokens);
	GetBoolValue(ini, "VampireLordForm", "DebugLog", Config::WheelBehavior::TransformWheels::VampireLordForm::DebugLog);
	GetBoolValue(ini, "LichForm", "Enabled", Config::WheelBehavior::TransformWheels::LichForm::Enabled);
	GetStringValue(ini, "LichForm", "Mode", Config::WheelBehavior::TransformWheels::LichForm::Mode);
	GetStringValue(ini, "LichForm", "PopulateMode", Config::WheelBehavior::TransformWheels::LichForm::PopulateMode);
	GetBoolValue(ini, "LichForm", "AllowBaseWheel", Config::WheelBehavior::TransformWheels::LichForm::AllowBaseWheel);
	GetStringValue(ini, "LichForm", "TransformGuard", Config::WheelBehavior::TransformWheels::LichForm::TransformGuard);
	GetBoolValue(ini, "LichForm", "BlockBoundSpells", Config::WheelBehavior::TransformWheels::LichForm::BlockBoundSpells);
	GetBoolValue(ini, "LichForm", "HideWeapons", Config::WheelBehavior::TransformWheels::LichForm::HideWeapons);
	GetBoolValue(ini, "LichForm", "HideGear", Config::WheelBehavior::TransformWheels::LichForm::HideGear);
	GetBoolValue(ini, "LichForm", "BlockStaffSwapping", Config::WheelBehavior::TransformWheels::LichForm::BlockStaffSwapping);
	GetBoolValue(ini, "LichForm", "SuppressDirectCast", Config::WheelBehavior::TransformWheels::LichForm::SuppressDirectCast);
	GetStringValue(ini, "LichForm", "RaceEditorIDContains", Config::WheelBehavior::TransformWheels::LichForm::RaceEditorIDContains);
	GetStringValue(ini, "LichForm", "RaceKeywords", Config::WheelBehavior::TransformWheels::LichForm::RaceKeywords);
	GetStringValue(ini, "LichForm", "RaceFormIDs", Config::WheelBehavior::TransformWheels::LichForm::RaceFormIDs);
	GetStringValue(ini, "LichForm", "SpellTokens", Config::WheelBehavior::TransformWheels::LichForm::SpellTokens);
	GetStringValue(ini, "LichForm", "ExitSpellTokens", Config::WheelBehavior::TransformWheels::LichForm::ExitSpellTokens);
	GetStringValue(ini, "LichForm", "AdditionalSpellFormIDs", Config::WheelBehavior::TransformWheels::LichForm::AdditionalSpellFormIDs);
	GetBoolValue(ini, "LichForm", "DebugLog", Config::WheelBehavior::TransformWheels::LichForm::DebugLog);

	// Gamepad navigation behavior (main wheel only)
	{
		auto hasKeyPresent = [&](const char* section, const char* key) {
			return ini.GetValue(section, key, nullptr) != nullptr;
		};

		Config::WheelBehavior::Gamepad::Nav::HasInnerDeadzone = hasKeyPresent("Gamepad.Nav", "InnerDeadzone");
		if (Config::WheelBehavior::Gamepad::Nav::HasInnerDeadzone) {
			GetFloatValue(ini, "Gamepad.Nav", "InnerDeadzone", Config::WheelBehavior::Gamepad::Nav::InnerDeadzone);
			Config::WheelBehavior::Gamepad::Nav::InnerDeadzone =
				std::clamp(Config::WheelBehavior::Gamepad::Nav::InnerDeadzone, 0.0f, 0.95f);
		}

		Config::WheelBehavior::Gamepad::Nav::HasOuterDeadzone = hasKeyPresent("Gamepad.Nav", "OuterDeadzone");
		if (Config::WheelBehavior::Gamepad::Nav::HasOuterDeadzone) {
			GetFloatValue(ini, "Gamepad.Nav", "OuterDeadzone", Config::WheelBehavior::Gamepad::Nav::OuterDeadzone);
			Config::WheelBehavior::Gamepad::Nav::OuterDeadzone =
				std::clamp(Config::WheelBehavior::Gamepad::Nav::OuterDeadzone, 0.0f, 1.0f);
		}

		Config::WheelBehavior::Gamepad::Nav::HasIntentMagnitude = hasKeyPresent("Gamepad.Nav", "IntentMagnitude");
		if (Config::WheelBehavior::Gamepad::Nav::HasIntentMagnitude) {
			GetFloatValue(ini, "Gamepad.Nav", "IntentMagnitude", Config::WheelBehavior::Gamepad::Nav::IntentMagnitude);
			Config::WheelBehavior::Gamepad::Nav::IntentMagnitude =
				std::clamp(Config::WheelBehavior::Gamepad::Nav::IntentMagnitude, 0.0f, 1.0f);
		}

		Config::WheelBehavior::Gamepad::Nav::HasSmoothingHalfLifeMs = hasKeyPresent("Gamepad.Nav", "SmoothingHalfLifeMs");
		if (Config::WheelBehavior::Gamepad::Nav::HasSmoothingHalfLifeMs) {
			GetFloatValue(ini, "Gamepad.Nav", "SmoothingHalfLifeMs", Config::WheelBehavior::Gamepad::Nav::SmoothingHalfLifeMs);
			Config::WheelBehavior::Gamepad::Nav::SmoothingHalfLifeMs =
				std::clamp(Config::WheelBehavior::Gamepad::Nav::SmoothingHalfLifeMs, 0.0f, 2000.0f);
		}

		Config::WheelBehavior::Gamepad::Nav::HasHysteresisDegrees = hasKeyPresent("Gamepad.Nav", "HysteresisDegrees");
		if (Config::WheelBehavior::Gamepad::Nav::HasHysteresisDegrees) {
			GetFloatValue(ini, "Gamepad.Nav", "HysteresisDegrees", Config::WheelBehavior::Gamepad::Nav::HysteresisDegrees);
			Config::WheelBehavior::Gamepad::Nav::HysteresisDegrees =
				std::clamp(Config::WheelBehavior::Gamepad::Nav::HysteresisDegrees, 0.0f, 45.0f);
		}

		Config::WheelBehavior::Gamepad::Nav::HasAutoCenterRestSnap = hasKeyPresent("Gamepad.Nav", "AutoCenterRestSnap");
		if (Config::WheelBehavior::Gamepad::Nav::HasAutoCenterRestSnap) {
			GetBoolValue(ini, "Gamepad.Nav", "AutoCenterRestSnap", Config::WheelBehavior::Gamepad::Nav::AutoCenterRestSnap);
		}

		Config::WheelBehavior::Gamepad::Open::HasUseLastSelectionOnOpen =
			hasKeyPresent("Gamepad.Open", "UseLastSelectionOnOpen");
		if (Config::WheelBehavior::Gamepad::Open::HasUseLastSelectionOnOpen) {
			GetBoolValue(ini, "Gamepad.Open", "UseLastSelectionOnOpen",
				Config::WheelBehavior::Gamepad::Open::UseLastSelectionOnOpen);
		}

		Config::WheelBehavior::Gamepad::Open::HasOpenGraceMs = hasKeyPresent("Gamepad.Open", "OpenGraceMs");
		if (Config::WheelBehavior::Gamepad::Open::HasOpenGraceMs) {
			GetFloatValue(ini, "Gamepad.Open", "OpenGraceMs", Config::WheelBehavior::Gamepad::Open::OpenGraceMs);
			Config::WheelBehavior::Gamepad::Open::OpenGraceMs =
				std::clamp(Config::WheelBehavior::Gamepad::Open::OpenGraceMs, 0.0f, 2000.0f);
		}

		Config::WheelBehavior::Gamepad::DPad::HasMode = hasKeyPresent("Gamepad.DPad", "Mode");
		if (Config::WheelBehavior::Gamepad::DPad::HasMode) {
			std::string modeStr;
			if (GetStringValue(ini, "Gamepad.DPad", "Mode", modeStr)) {
				Config::WheelBehavior::Gamepad::DPad::ModeValue = ParseGamepadDpadMode(modeStr);
			} else {
				std::uint32_t modeVal = static_cast<std::uint32_t>(Config::WheelBehavior::Gamepad::DPad::ModeValue);
				if (GetUInt32Value(ini, "Gamepad.DPad", "Mode", modeVal)) {
					modeVal = std::clamp(modeVal, 0u, 1u);
					Config::WheelBehavior::Gamepad::DPad::ModeValue =
						static_cast<Config::WheelBehavior::Gamepad::DPad::Mode>(modeVal);
				}
			}
		}

		Config::WheelBehavior::Gamepad::DebugController::HasEnabled = hasKeyPresent("Debug.Controller", "Enabled");
		if (Config::WheelBehavior::Gamepad::DebugController::HasEnabled) {
			GetBoolValue(ini, "Debug.Controller", "Enabled",
				Config::WheelBehavior::Gamepad::DebugController::Enabled);
		}

		Config::WheelBehavior::Gamepad::DebugController::HasOverlay = hasKeyPresent("Debug.Controller", "Overlay");
		if (Config::WheelBehavior::Gamepad::DebugController::HasOverlay) {
			GetBoolValue(ini, "Debug.Controller", "Overlay",
				Config::WheelBehavior::Gamepad::DebugController::Overlay);
		}

		LogGamepadConfigSourcesOnce();
	}

	// Cooldown overlay settings
	GetBoolValue(ini, "Cooldowns", "Enabled", Config::Cooldowns::Enabled);
	GetBoolValue(ini, "Cooldowns", "ShowTimer", Config::Cooldowns::ShowTimer);
	GetUInt32Value(ini, "Cooldowns", "OverlayColor", Config::Cooldowns::OverlayColor);
	GetFloatValue(ini, "Cooldowns", "ContentDimAlpha", Config::Cooldowns::ContentDimAlpha);
	GetBoolValue(ini, "Cooldowns", "SelectedIndicatorEnabled", Config::Cooldowns::SelectedIndicatorEnabled);
	GetUInt32Value(ini, "Cooldowns", "SelectedIndicatorTintColor", Config::Cooldowns::SelectedIndicatorTintColor);
	GetFloatValue(ini, "Cooldowns", "CacheWindowSeconds", Config::Cooldowns::CacheWindowSeconds);

	// Cooldown timer text styling
	{
		std::uint32_t tmpIndex = Config::Cooldowns::TimerText::FontIndex;
		if (!GetUInt32Value(ini, "Cooldowns.TimerText", "FontIndex", tmpIndex)) {
			float tmpFloat = static_cast<float>(tmpIndex);
			if (GetFloatValue(ini, "Cooldowns.TimerText", "FontIndex", tmpFloat)) {
				tmpIndex = static_cast<std::uint32_t>((std::max)(0.0f, std::round(tmpFloat)));
			}
		}
		Config::Cooldowns::TimerText::FontIndex = tmpIndex;
	}
	GetFloatValue(ini, "Cooldowns.TimerText", "Size", Config::Cooldowns::TimerText::Size);
	GetUInt32Value(ini, "Cooldowns.TimerText", "Color", Config::Cooldowns::TimerText::Color);

	// Backward compatibility: if new cooldown keys aren't present, fall back to OverlayColor.
	// - ContentDimAlpha uses OverlayColor alpha
	// - SelectedIndicatorTintColor uses OverlayColor full ARGB
	{
		float tmpContentDim = Config::Cooldowns::ContentDimAlpha;
		if (!GetFloatValue(ini, "Cooldowns", "ContentDimAlpha", tmpContentDim)) {
			Config::Cooldowns::ContentDimAlpha = ImGui::ColorConvertU32ToFloat4(Config::Cooldowns::OverlayColor).w;
		}
		std::uint32_t tmpTint = Config::Cooldowns::SelectedIndicatorTintColor;
		if (!GetUInt32Value(ini, "Cooldowns", "SelectedIndicatorTintColor", tmpTint)) {
			Config::Cooldowns::SelectedIndicatorTintColor = Config::Cooldowns::OverlayColor;
		}
	}

	// RTU HoverDelay styling (kept with wheel behavior settings).
	GetBoolValue(ini, "Styling.HoverDelay", "Enabled", Config::Styling::HoverDelay::Enabled);
	GetFloatValue(ini, "Styling.HoverDelay", "Radius", Config::Styling::HoverDelay::Radius);
	GetFloatValue(ini, "Styling.HoverDelay", "RadiusOffset", Config::Styling::HoverDelay::RadiusOffset);
	GetFloatValue(ini, "Styling.HoverDelay", "Thickness", Config::Styling::HoverDelay::Thickness);
	GetUInt32Value(ini, "Styling.HoverDelay", "Color", Config::Styling::HoverDelay::Color);
	GetUInt32Value(ini, "Styling.HoverDelay", "BackgroundColor", Config::Styling::HoverDelay::BackgroundColor);

	// InstantSpell Indicator colors (fallback to HoverDelay colors if not set)
	if (!GetUInt32Value(ini, "Styling.HoverDelay", "InstantSpellColor", Config::Styling::HoverDelay::InstantSpellColor)) {
		Config::Styling::HoverDelay::InstantSpellColor = Config::Styling::HoverDelay::Color;
	}
	if (!GetUInt32Value(ini, "Styling.HoverDelay", "InstantSpellBackgroundColor", Config::Styling::HoverDelay::InstantSpellBackgroundColor)) {
		Config::Styling::HoverDelay::InstantSpellBackgroundColor = Config::Styling::HoverDelay::BackgroundColor;
	}
	GetBoolValue(ini, "Styling.HoverDelay", "InstantSpellUseReskinAssets", Config::Styling::HoverDelay::InstantSpellUseReskinAssets);
	GetBoolValue(ini, "Styling.HoverDelay", "InstantSpellAssetsEnabled", Config::Styling::HoverDelay::InstantSpellUseReskinAssets);
	GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAssetScale", Config::Styling::HoverDelay::InstantSpellAssetScale);
	GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAssetOffsetX", Config::Styling::HoverDelay::InstantSpellAssetOffsetX);
	GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAssetOffsetY", Config::Styling::HoverDelay::InstantSpellAssetOffsetY);
	GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAssetOpacity", Config::Styling::HoverDelay::InstantSpellAssetOpacity);
	GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellHandIndicatorScale", Config::Styling::HoverDelay::InstantSpellHandIndicatorScale);
	LoadInstantSpellHandIndicatorOffsetsFromIni(
		ini,
		"Styling.HoverDelay",
		Config::Styling::HoverDelay::InstantSpellHandIndicatorOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorOffsetY,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetY,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetY,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetY);
	GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellHandIndicatorOpacity", Config::Styling::HoverDelay::InstantSpellHandIndicatorOpacity);
	GetBoolValue(ini, "Styling.HoverDelay", "InstantSpellUseAtlasAnimation", Config::Styling::HoverDelay::InstantSpellUseAtlasAnimation);
	GetBoolValue(ini, "Styling.HoverDelay", "InstantSpellAtlasEnabled", Config::Styling::HoverDelay::InstantSpellUseAtlasAnimation);
	{
		float tmpCols = static_cast<float>(Config::Styling::HoverDelay::InstantSpellAtlasCols);
		if (GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAtlasCols", tmpCols)) {
			Config::Styling::HoverDelay::InstantSpellAtlasCols = static_cast<std::uint32_t>((std::max)(1.0f, std::round(tmpCols)));
		}
	}
	{
		float tmpRows = static_cast<float>(Config::Styling::HoverDelay::InstantSpellAtlasRows);
		if (GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAtlasRows", tmpRows)) {
			Config::Styling::HoverDelay::InstantSpellAtlasRows = static_cast<std::uint32_t>((std::max)(1.0f, std::round(tmpRows)));
		}
	}
	{
		float tmpFrames = static_cast<float>(Config::Styling::HoverDelay::InstantSpellAtlasFrameCount);
		if (GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAtlasFrameCount", tmpFrames)) {
			Config::Styling::HoverDelay::InstantSpellAtlasFrameCount = static_cast<std::uint32_t>((std::max)(1.0f, std::round(tmpFrames)));
		}
	}
	Config::Styling::HoverDelay::InstantSpellAssetScale = std::clamp(Config::Styling::HoverDelay::InstantSpellAssetScale, 0.1f, 12.0f);
	Config::Styling::HoverDelay::InstantSpellAssetOffsetX = std::clamp(Config::Styling::HoverDelay::InstantSpellAssetOffsetX, -200.0f, 200.0f);
	Config::Styling::HoverDelay::InstantSpellAssetOffsetY = std::clamp(Config::Styling::HoverDelay::InstantSpellAssetOffsetY, -200.0f, 200.0f);
	Config::Styling::HoverDelay::InstantSpellAssetOpacity = std::clamp(Config::Styling::HoverDelay::InstantSpellAssetOpacity, 0.0f, 1.0f);
	Config::Styling::HoverDelay::InstantSpellHandIndicatorScale = std::clamp(Config::Styling::HoverDelay::InstantSpellHandIndicatorScale, 0.1f, 12.0f);
	ClampInstantSpellHandIndicatorOffsets(
		Config::Styling::HoverDelay::InstantSpellHandIndicatorOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorOffsetY,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetY,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetY,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetY);
	Config::Styling::HoverDelay::InstantSpellHandIndicatorOpacity = std::clamp(Config::Styling::HoverDelay::InstantSpellHandIndicatorOpacity, 0.0f, 1.0f);
	Config::Styling::HoverDelay::InstantSpellAtlasCols = std::clamp(Config::Styling::HoverDelay::InstantSpellAtlasCols, 1u, 64u);
	Config::Styling::HoverDelay::InstantSpellAtlasRows = std::clamp(Config::Styling::HoverDelay::InstantSpellAtlasRows, 1u, 64u);
	{
		const std::uint32_t maxFrames = Config::Styling::HoverDelay::InstantSpellAtlasCols * Config::Styling::HoverDelay::InstantSpellAtlasRows;
		Config::Styling::HoverDelay::InstantSpellAtlasFrameCount = std::clamp(Config::Styling::HoverDelay::InstantSpellAtlasFrameCount, 1u, (std::max)(1u, maxFrames));
	}
	ApplyInstantSpellIndicatorAssetPathHardcoded();

	// Sound settings
	GetBoolValue(ini, "Sounds", "EnableSounds", Config::Sounds::EnableSounds);
	GetStringValue(ini, "Sounds", "HoverSoundEditorID", Config::Sounds::HoverSoundEditorID);
	GetStringValue(ini, "Sounds", "ActivateSoundEditorID", Config::Sounds::ActivateSoundEditorID);
	GetFloatValue(ini, "Sounds", "HoverSoundVolume", Config::Sounds::HoverSoundVolume);
	GetFloatValue(ini, "Sounds", "ActivateSoundVolume", Config::Sounds::ActivateSoundVolume);

	// Shout stage sounds
	GetBoolValue(ini, "Sounds", "EnableShoutStageSounds", Config::Sounds::EnableShoutStageSounds);
	GetUInt32Value(ini, "Sounds", "ShoutStageSoundMode", Config::Sounds::ShoutStageSoundMode);
	GetStringValue(ini, "Sounds", "ShoutUISoundEditorID", Config::Sounds::ShoutUISoundEditorID);
	GetFloatValue(ini, "Sounds", "ShoutUIStageVolume1", Config::Sounds::ShoutUIStageVolume1);
	GetFloatValue(ini, "Sounds", "ShoutUIStageVolume2", Config::Sounds::ShoutUIStageVolume2);
	GetFloatValue(ini, "Sounds", "ShoutUIStageVolume3", Config::Sounds::ShoutUIStageVolume3);
	GetFloatValue(ini, "Sounds", "ShoutUIStagePitch1", Config::Sounds::ShoutUIStagePitch1);
	GetFloatValue(ini, "Sounds", "ShoutUIStagePitch2", Config::Sounds::ShoutUIStagePitch2);
	GetFloatValue(ini, "Sounds", "ShoutUIStagePitch3", Config::Sounds::ShoutUIStagePitch3);
	GetStringValue(ini, "Sounds", "ShoutWord1SoundEditorID", Config::Sounds::ShoutWord1SoundEditorID);
	GetStringValue(ini, "Sounds", "ShoutWord2SoundEditorID", Config::Sounds::ShoutWord2SoundEditorID);
	GetStringValue(ini, "Sounds", "ShoutWord3SoundEditorID", Config::Sounds::ShoutWord3SoundEditorID);

	// MainWheel debug settings
	GetBoolValue(ini, "MainWheel.Debug", "Enabled", Config::MainWheel::Debug::Enabled);
	GetBoolValue(ini, "MainWheel.Debug", "OverlayEnabled", Config::MainWheel::Debug::OverlayEnabled);
	GetBoolValue(ini, "MainWheel.Debug", "Verbose", Config::MainWheel::Debug::Verbose);
	GetUInt32Value(ini, "MainWheel.Debug", "RateLimitMs", Config::MainWheel::Debug::RateLimitMs);
	GetBoolValue(ini, "MainWheel.Debug", "LogOpenClose", Config::MainWheel::Debug::LogOpenClose);
	GetBoolValue(ini, "MainWheel.Debug", "LogConfig", Config::MainWheel::Debug::LogConfig);
	GetBoolValue(ini, "MainWheel.Debug", "LogInput", Config::MainWheel::Debug::LogInput);
	GetBoolValue(ini, "MainWheel.Debug", "LogScaling", Config::MainWheel::Debug::LogScaling);
	GetBoolValue(ini, "MainWheel.Debug", "LogClamp", Config::MainWheel::Debug::LogClamp);
	GetBoolValue(ini, "MainWheel.Debug", "LogIndicators", Config::MainWheel::Debug::LogIndicators);
	GetBoolValue(ini, "MainWheel.Debug", "LogReskinResolve", Config::MainWheel::Debug::LogReskinResolve);
	GetBoolValue(ini, "MainWheel.Debug", "LogAssets", Config::MainWheel::Debug::LogAssets);
	GetBoolValue(ini, "MainWheel.Debug", "LogPerf", Config::MainWheel::Debug::LogPerf);

	// MainWheel indicator toggles
	GetBoolValue(ini, "MainWheel.Indicators", "ShowHandIndicator", Config::MainWheel::ShowHandIndicator);
	GetFloatValue(ini, "MainWheel.Indicators", "RightSideOffsetX", Config::MainWheel::HandIndicators::RightSideOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators", "RightSideOffsetY", Config::MainWheel::HandIndicators::RightSideOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators", "WheelRightSideOffsetX", Config::MainWheel::HandIndicators::RightSideOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators", "WheelRightSideOffsetY", Config::MainWheel::HandIndicators::RightSideOffsetY);
	if (!GetUInt32Value(ini, "MainWheel.Indicators.Left", "Color", Config::MainWheel::HandIndicators::Left.Color)) {
		Config::MainWheel::HandIndicators::Left.Color = Config::Styling::Wheel::TextColor;
	}
	GetFloatValue(ini, "MainWheel.Indicators.Left", "Opacity", Config::MainWheel::HandIndicators::Left.Opacity);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SizeScale", Config::MainWheel::HandIndicators::Left.SizeScale);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "Thickness", Config::MainWheel::HandIndicators::Left.Thickness);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "OffsetX", Config::MainWheel::HandIndicators::Left.OffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "OffsetY", Config::MainWheel::HandIndicators::Left.OffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SecondaryOffsetX", Config::MainWheel::HandIndicators::Left.SecondaryOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SecondaryOffsetY", Config::MainWheel::HandIndicators::Left.SecondaryOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SubSlotOffsetX", Config::MainWheel::HandIndicators::Left.SecondaryOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SubSlotOffsetY", Config::MainWheel::HandIndicators::Left.SecondaryOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "DualTopOffsetX", Config::MainWheel::HandIndicators::Left.DualTopOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "DualTopOffsetY", Config::MainWheel::HandIndicators::Left.DualTopOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "BothSelectedOffsetX", Config::MainWheel::HandIndicators::Left.DualTopOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "BothSelectedOffsetY", Config::MainWheel::HandIndicators::Left.DualTopOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SlotLeftOffsetX", Config::MainWheel::HandIndicators::Left.SlotLeftOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SlotLeftOffsetY", Config::MainWheel::HandIndicators::Left.SlotLeftOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "WheelLeftOffsetX", Config::MainWheel::HandIndicators::Left.SlotLeftOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "WheelLeftOffsetY", Config::MainWheel::HandIndicators::Left.SlotLeftOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SlotRightOffsetX", Config::MainWheel::HandIndicators::Left.SlotRightOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SlotRightOffsetY", Config::MainWheel::HandIndicators::Left.SlotRightOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "WheelRightOffsetX", Config::MainWheel::HandIndicators::Left.SlotRightOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "WheelRightOffsetY", Config::MainWheel::HandIndicators::Left.SlotRightOffsetY);
	if (!GetUInt32Value(ini, "MainWheel.Indicators.Right", "Color", Config::MainWheel::HandIndicators::Right.Color)) {
		Config::MainWheel::HandIndicators::Right.Color = Config::Styling::Wheel::TextColor;
	}
	GetFloatValue(ini, "MainWheel.Indicators.Right", "Opacity", Config::MainWheel::HandIndicators::Right.Opacity);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SizeScale", Config::MainWheel::HandIndicators::Right.SizeScale);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "Thickness", Config::MainWheel::HandIndicators::Right.Thickness);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "OffsetX", Config::MainWheel::HandIndicators::Right.OffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "OffsetY", Config::MainWheel::HandIndicators::Right.OffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SecondaryOffsetX", Config::MainWheel::HandIndicators::Right.SecondaryOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SecondaryOffsetY", Config::MainWheel::HandIndicators::Right.SecondaryOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SubSlotOffsetX", Config::MainWheel::HandIndicators::Right.SecondaryOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SubSlotOffsetY", Config::MainWheel::HandIndicators::Right.SecondaryOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "DualTopOffsetX", Config::MainWheel::HandIndicators::Right.DualTopOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "DualTopOffsetY", Config::MainWheel::HandIndicators::Right.DualTopOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "BothSelectedOffsetX", Config::MainWheel::HandIndicators::Right.DualTopOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "BothSelectedOffsetY", Config::MainWheel::HandIndicators::Right.DualTopOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SlotLeftOffsetX", Config::MainWheel::HandIndicators::Right.SlotLeftOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SlotLeftOffsetY", Config::MainWheel::HandIndicators::Right.SlotLeftOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "WheelLeftOffsetX", Config::MainWheel::HandIndicators::Right.SlotLeftOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "WheelLeftOffsetY", Config::MainWheel::HandIndicators::Right.SlotLeftOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SlotRightOffsetX", Config::MainWheel::HandIndicators::Right.SlotRightOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SlotRightOffsetY", Config::MainWheel::HandIndicators::Right.SlotRightOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "WheelRightOffsetX", Config::MainWheel::HandIndicators::Right.SlotRightOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "WheelRightOffsetY", Config::MainWheel::HandIndicators::Right.SlotRightOffsetY);
	if (!GetUInt32Value(ini, "MainWheel.Indicators.Dual", "Color", Config::MainWheel::HandIndicators::Dual.Color)) {
		Config::MainWheel::HandIndicators::Dual.Color = Config::Styling::Wheel::TextColor;
	}
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "Opacity", Config::MainWheel::HandIndicators::Dual.Opacity);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "SizeScale", Config::MainWheel::HandIndicators::Dual.SizeScale);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "Thickness", Config::MainWheel::HandIndicators::Dual.Thickness);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "OffsetX", Config::MainWheel::HandIndicators::Dual.OffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "OffsetY", Config::MainWheel::HandIndicators::Dual.OffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "BothSelectedOffsetX", Config::MainWheel::HandIndicators::Dual.OffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "BothSelectedOffsetY", Config::MainWheel::HandIndicators::Dual.OffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "RightSideOffsetX", Config::MainWheel::HandIndicators::DualRightSideOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "RightSideOffsetY", Config::MainWheel::HandIndicators::DualRightSideOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "DualRightSideOffsetX", Config::MainWheel::HandIndicators::DualRightSideOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "DualRightSideOffsetY", Config::MainWheel::HandIndicators::DualRightSideOffsetY);
	Config::MainWheel::HandIndicators::Left.Opacity = std::clamp(Config::MainWheel::HandIndicators::Left.Opacity, 0.0f, 1.0f);
	Config::MainWheel::HandIndicators::Left.SizeScale = std::clamp(Config::MainWheel::HandIndicators::Left.SizeScale, 0.5f, 2.0f);
	Config::MainWheel::HandIndicators::Left.Thickness = std::clamp(Config::MainWheel::HandIndicators::Left.Thickness, 0.0f, 3.0f);
	Config::MainWheel::HandIndicators::Left.OffsetX = std::clamp(Config::MainWheel::HandIndicators::Left.OffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.OffsetY = std::clamp(Config::MainWheel::HandIndicators::Left.OffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.SecondaryOffsetX = std::clamp(Config::MainWheel::HandIndicators::Left.SecondaryOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.SecondaryOffsetY = std::clamp(Config::MainWheel::HandIndicators::Left.SecondaryOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.DualTopOffsetX = std::clamp(Config::MainWheel::HandIndicators::Left.DualTopOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.DualTopOffsetY = std::clamp(Config::MainWheel::HandIndicators::Left.DualTopOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.SlotLeftOffsetX = std::clamp(Config::MainWheel::HandIndicators::Left.SlotLeftOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.SlotLeftOffsetY = std::clamp(Config::MainWheel::HandIndicators::Left.SlotLeftOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.SlotRightOffsetX = std::clamp(Config::MainWheel::HandIndicators::Left.SlotRightOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.SlotRightOffsetY = std::clamp(Config::MainWheel::HandIndicators::Left.SlotRightOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.Opacity = std::clamp(Config::MainWheel::HandIndicators::Right.Opacity, 0.0f, 1.0f);
	Config::MainWheel::HandIndicators::Right.SizeScale = std::clamp(Config::MainWheel::HandIndicators::Right.SizeScale, 0.5f, 2.0f);
	Config::MainWheel::HandIndicators::Right.Thickness = std::clamp(Config::MainWheel::HandIndicators::Right.Thickness, 0.0f, 3.0f);
	Config::MainWheel::HandIndicators::Right.OffsetX = std::clamp(Config::MainWheel::HandIndicators::Right.OffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.OffsetY = std::clamp(Config::MainWheel::HandIndicators::Right.OffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.SecondaryOffsetX = std::clamp(Config::MainWheel::HandIndicators::Right.SecondaryOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.SecondaryOffsetY = std::clamp(Config::MainWheel::HandIndicators::Right.SecondaryOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.DualTopOffsetX = std::clamp(Config::MainWheel::HandIndicators::Right.DualTopOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.DualTopOffsetY = std::clamp(Config::MainWheel::HandIndicators::Right.DualTopOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.SlotLeftOffsetX = std::clamp(Config::MainWheel::HandIndicators::Right.SlotLeftOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.SlotLeftOffsetY = std::clamp(Config::MainWheel::HandIndicators::Right.SlotLeftOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.SlotRightOffsetX = std::clamp(Config::MainWheel::HandIndicators::Right.SlotRightOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.SlotRightOffsetY = std::clamp(Config::MainWheel::HandIndicators::Right.SlotRightOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Dual.Opacity = std::clamp(Config::MainWheel::HandIndicators::Dual.Opacity, 0.0f, 1.0f);
	Config::MainWheel::HandIndicators::Dual.SizeScale = std::clamp(Config::MainWheel::HandIndicators::Dual.SizeScale, 0.5f, 2.0f);
	Config::MainWheel::HandIndicators::Dual.Thickness = std::clamp(Config::MainWheel::HandIndicators::Dual.Thickness, 0.0f, 3.0f);
	Config::MainWheel::HandIndicators::Dual.OffsetX = std::clamp(Config::MainWheel::HandIndicators::Dual.OffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Dual.OffsetY = std::clamp(Config::MainWheel::HandIndicators::Dual.OffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::RightSideOffsetX = std::clamp(Config::MainWheel::HandIndicators::RightSideOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::RightSideOffsetY = std::clamp(Config::MainWheel::HandIndicators::RightSideOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::DualRightSideOffsetX = std::clamp(Config::MainWheel::HandIndicators::DualRightSideOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::DualRightSideOffsetY = std::clamp(Config::MainWheel::HandIndicators::DualRightSideOffsetY, -200.0f, 200.0f);
	ApplyMainWheelIndicatorAssetPathHardcoded();

	// MainWheel edit mode hints
	GetBoolValue(ini, "MainWheel.EditHints", "Enabled", Config::MainWheel::EditHints::Enabled);
	GetUInt32Value(ini, "MainWheel.EditHints", "DisplayMode", Config::MainWheel::EditHints::DisplayMode);
	GetUInt32Value(ini, "MainWheel.EditHints", "GamepadIconSet", Config::MainWheel::EditHints::GamepadIconSet);
	GetBoolValue(ini, "MainWheel.EditHints", "ShowBackground", Config::MainWheel::EditHints::ShowBackground);
	GetBoolValue(ini, "MainWheel.EditHints", "ShowTitle", Config::MainWheel::EditHints::ShowTitle);
	GetFloatValue(ini, "MainWheel.EditHints", "AnchorX", Config::MainWheel::EditHints::AnchorX);
	GetFloatValue(ini, "MainWheel.EditHints", "AnchorY", Config::MainWheel::EditHints::AnchorY);
	GetFloatValue(ini, "MainWheel.EditHints", "LabelWidth", Config::MainWheel::EditHints::LabelWidth);
	GetFloatValue(ini, "MainWheel.EditHints", "FontSize", Config::MainWheel::EditHints::FontSize);
	GetFloatValue(ini, "MainWheel.EditHints", "HeaderFontSize", Config::MainWheel::EditHints::HeaderFontSize);
	GetFloatValue(ini, "MainWheel.EditHints", "IconSize", Config::MainWheel::EditHints::IconSize);
	GetFloatValue(ini, "MainWheel.EditHints", "RowSpacing", Config::MainWheel::EditHints::RowSpacing);
	GetFloatValue(ini, "MainWheel.EditHints", "KeyGap", Config::MainWheel::EditHints::KeyGap);
	GetFloatValue(ini, "MainWheel.EditHints", "PanelPaddingX", Config::MainWheel::EditHints::PanelPaddingX);
	GetFloatValue(ini, "MainWheel.EditHints", "PanelPaddingY", Config::MainWheel::EditHints::PanelPaddingY);
	GetUInt32Value(ini, "MainWheel.EditHints", "TextColor", Config::MainWheel::EditHints::TextColor);
	GetUInt32Value(ini, "MainWheel.EditHints", "HeaderColor", Config::MainWheel::EditHints::HeaderColor);
	GetUInt32Value(ini, "MainWheel.EditHints", "BackgroundColor", Config::MainWheel::EditHints::BackgroundColor);

	Config::MainWheel::EditHints::DisplayMode = std::clamp(Config::MainWheel::EditHints::DisplayMode, 0u, 3u);
	Config::MainWheel::EditHints::GamepadIconSet = std::clamp(Config::MainWheel::EditHints::GamepadIconSet, 0u, 1u);
	Config::MainWheel::EditHints::AnchorX = std::clamp(Config::MainWheel::EditHints::AnchorX, -2000.0f, 2000.0f);
	Config::MainWheel::EditHints::AnchorY = std::clamp(Config::MainWheel::EditHints::AnchorY, -2000.0f, 2000.0f);
	Config::MainWheel::EditHints::LabelWidth = std::clamp(Config::MainWheel::EditHints::LabelWidth, 80.0f, 700.0f);
	Config::MainWheel::EditHints::FontSize = std::clamp(Config::MainWheel::EditHints::FontSize, 10.0f, 80.0f);
	Config::MainWheel::EditHints::HeaderFontSize = std::clamp(Config::MainWheel::EditHints::HeaderFontSize, 10.0f, 80.0f);
	Config::MainWheel::EditHints::IconSize = std::clamp(Config::MainWheel::EditHints::IconSize, 10.0f, 120.0f);
	Config::MainWheel::EditHints::RowSpacing = std::clamp(Config::MainWheel::EditHints::RowSpacing, 0.0f, 60.0f);
	Config::MainWheel::EditHints::KeyGap = std::clamp(Config::MainWheel::EditHints::KeyGap, 0.0f, 100.0f);
	Config::MainWheel::EditHints::PanelPaddingX = std::clamp(Config::MainWheel::EditHints::PanelPaddingX, 0.0f, 80.0f);
	Config::MainWheel::EditHints::PanelPaddingY = std::clamp(Config::MainWheel::EditHints::PanelPaddingY, 0.0f, 80.0f);

	// Debug settings (safe patch)
	GetBoolValue(ini, "Debug", "LogActivateRejects", Config::Debug::LogActivateRejects);
	GetBoolValue(ini, "Debug", "LogMenuBlockReasons", Config::Debug::LogMenuBlockReasons);
	GetBoolValue(ini, "Debug", "LogPopupAnim", Config::Debug::LogPopupAnim);
	GetBoolValue(ini, "Debug", "LogActionPolicy", Config::Debug::LogActionPolicy);
	GetBoolValue(ini, "Debug", "inputSpy", Config::Debug::InputSpy);
	GetUInt32Value(ini, "Debug", "inputSpyDumpHotkey", Config::Debug::InputSpyDumpHotkey);
	{
		std::uint32_t tmpRate = Config::Debug::InputSpyRateLimitMs;
		if (GetUInt32Value(ini, "Debug", "inputSpyRateLimitMs", tmpRate)) {
			Config::Debug::InputSpyRateLimitMs = std::clamp(tmpRate, 0u, 5000u);
		}
		std::uint32_t tmpRing = Config::Debug::InputSpyRingBuffer;
		if (GetUInt32Value(ini, "Debug", "inputSpyRingBuffer", tmpRing)) {
			Config::Debug::InputSpyRingBuffer = std::clamp(tmpRing, 32u, 8192u);
		}
	}
	{
		float tmpInterval = static_cast<float>(Config::Debug::PopupAnimLogIntervalMs);
		if (GetFloatValue(ini, "Debug", "PopupAnimLogIntervalMs", tmpInterval)) {
			tmpInterval = std::clamp(tmpInterval, 1.0f, 60000.0f);
			Config::Debug::PopupAnimLogIntervalMs = static_cast<std::uint32_t>(tmpInterval);
		}
	}

	// Input broker arbitration (cross-plugin input ownership + key reservation).
	GetBoolValue(ini, "InputBroker", "Enabled", Config::InputBroker::Enabled);
	GetBoolValue(ini, "InputBroker", "DebugLog", Config::InputBroker::DebugLog);
	{
		float mainPriority = static_cast<float>(Config::InputBroker::Priority_MainWheel);
		if (GetFloatValue(ini, "InputBroker", "Priority_MainWheel", mainPriority)) {
			mainPriority = std::clamp(mainPriority, -1000.0f, 1000.0f);
			Config::InputBroker::Priority_MainWheel = static_cast<std::int32_t>(mainPriority);
		}
		float ammoPriority = static_cast<float>(Config::InputBroker::Priority_AmmoWheel);
		if (GetFloatValue(ini, "InputBroker", "Priority_AmmoWheel", ammoPriority)) {
			ammoPriority = std::clamp(ammoPriority, -1000.0f, 1000.0f);
			Config::InputBroker::Priority_AmmoWheel = static_cast<std::int32_t>(ammoPriority);
		}
	}

	// MainWheel low-end overrides
	GetBoolValue(ini, "MainWheel.LowEnd", "DisableBlurOnOpen", Config::MainWheel::LowEnd::DisableBlurOnOpen);
	GetBoolValue(ini, "MainWheel.LowEnd", "PreferPrimitiveBackgrounds", Config::MainWheel::LowEnd::PreferPrimitiveBackgrounds);
	if (Config::MainWheel::LowEnd::DisableBlurOnOpen) {
		Config::Styling::Wheel::BlurOnOpen = false;
	}
	if (Config::MainWheel::LowEnd::PreferPrimitiveBackgrounds) {
		Config::Styling::Wheel::UseGeometricPrimitiveForBackgroundTexture = true;
	}

	// MainWheel mouse hover stability settings (center lock, jump guard, hysteresis, slowdown)
	GetFloatValue(ini, "MainWheel.Mouse", "CenterLockRadiusPx", Config::MainWheel::Mouse::CenterLockRadiusPx);
	GetFloatValue(ini, "MainWheel.Mouse", "JumpGuardRadiusPx", Config::MainWheel::Mouse::JumpGuardRadiusPx);
	GetFloatValue(ini, "MainWheel.Mouse", "HysteresisDegrees", Config::MainWheel::Mouse::HysteresisDegrees);
	GetBoolValue(ini, "MainWheel.Mouse", "CenterSlowdownEnabled", Config::MainWheel::Mouse::CenterSlowdownEnabled);
	GetFloatValue(ini, "MainWheel.Mouse", "GainCenter", Config::MainWheel::Mouse::GainCenter);
	GetFloatValue(ini, "MainWheel.Mouse", "GainOuter", Config::MainWheel::Mouse::GainOuter);
	GetFloatValue(ini, "MainWheel.Mouse", "CurvePower", Config::MainWheel::Mouse::CurvePower);
	GetBoolValue(ini, "MainWheel.Mouse", "DebugHoverLog", Config::MainWheel::Mouse::DebugHoverLog);
	GetFloatValue(ini, "MainWheel.Mouse", "InnerDeadZoneR", Config::MainWheel::Mouse::InnerDeadZoneR);
	GetFloatValue(ini, "MainWheel.Mouse", "InnerBlendZoneR", Config::MainWheel::Mouse::InnerBlendZoneR);
	GetFloatValue(ini, "MainWheel.Mouse", "MinStableSpeed", Config::MainWheel::Mouse::MinStableSpeed);
	GetFloatValue(ini, "MainWheel.Mouse", "VelocityHalfLifeMs", Config::MainWheel::Mouse::VelocityHalfLifeMs);
	GetFloatValue(ini, "MainWheel.Mouse", "StableDirHalfLifeMs", Config::MainWheel::Mouse::StableDirHalfLifeMs);
	GetFloatValue(ini, "MainWheel.Mouse", "IntentMinSpeed", Config::MainWheel::Mouse::IntentMinSpeed);
	GetFloatValue(ini, "MainWheel.Mouse", "IntentSustainSpeed", Config::MainWheel::Mouse::IntentSustainSpeed);
	GetFloatValue(ini, "MainWheel.Mouse", "IntentConfirmMs", Config::MainWheel::Mouse::IntentConfirmMs);
	GetFloatValue(ini, "MainWheel.Mouse", "IntentConeDeg", Config::MainWheel::Mouse::IntentConeDeg);
	GetFloatValue(ini, "MainWheel.Mouse", "IntentReleaseConeDeg", Config::MainWheel::Mouse::IntentReleaseConeDeg);
	GetFloatValue(ini, "MainWheel.Mouse", "IntentMinAngleDeg", Config::MainWheel::Mouse::IntentMinAngleDeg);
	GetFloatValue(ini, "MainWheel.Mouse", "IntentReleaseSpeed", Config::MainWheel::Mouse::IntentReleaseSpeed);
	GetFloatValue(ini, "MainWheel.Mouse", "IntentReleaseDwellMs", Config::MainWheel::Mouse::IntentReleaseDwellMs);
	GetFloatValue(ini, "MainWheel.Mouse", "IntentBias", Config::MainWheel::Mouse::IntentBias);
	GetFloatValue(ini, "MainWheel.Mouse", "IntentNeighborBias", Config::MainWheel::Mouse::IntentNeighborBias);
	GetFloatValue(ini, "MainWheel.Mouse", "ScoreWeightAngle", Config::MainWheel::Mouse::ScoreWeightAngle);
	GetFloatValue(ini, "MainWheel.Mouse", "ScoreWeightMotion", Config::MainWheel::Mouse::ScoreWeightMotion);
	GetFloatValue(ini, "MainWheel.Mouse", "ScoreWeightInertia", Config::MainWheel::Mouse::ScoreWeightInertia);
	GetFloatValue(ini, "MainWheel.Mouse", "ScoreWeightStickiness", Config::MainWheel::Mouse::ScoreWeightStickiness);
	GetFloatValue(ini, "MainWheel.Mouse", "SwitchConfidenceMargin", Config::MainWheel::Mouse::SwitchConfidenceMargin);
	GetFloatValue(ini, "MainWheel.Mouse", "SwitchDwellMs", Config::MainWheel::Mouse::SwitchDwellMs);
	{
		std::uint32_t temp = static_cast<std::uint32_t>(Config::MainWheel::Mouse::InnerDeadzoneMaxSlotDelta);
		if (GetUInt32Value(ini, "MainWheel.Mouse", "InnerDeadzoneMaxSlotDelta", temp)) {
			Config::MainWheel::Mouse::InnerDeadzoneMaxSlotDelta = static_cast<int>(temp);
		}
	}
	GetBoolValue(ini, "MainWheel.Mouse", "LogMouseFeatures", Config::MainWheel::Mouse::LogMouseFeatures);
	GetBoolValue(ini, "MainWheel.Mouse", "LogCandidateScores", Config::MainWheel::Mouse::LogCandidateScores);
	GetBoolValue(ini, "MainWheel.Mouse", "LogIntentState", Config::MainWheel::Mouse::LogIntentState);
	GetBoolValue(ini, "MainWheel.Mouse", "DrawDebugOverlay", Config::MainWheel::Mouse::DrawDebugOverlay);

	// MainWheel mouse stabilization (legacy guard rails)
	GetBoolValue(ini, "MainWheel.MouseStabilization", "Enabled", Config::MainWheel::MouseStabilization::Enabled);
	GetFloatValue(ini, "MainWheel.MouseStabilization", "CenterHoldRadius", Config::MainWheel::MouseStabilization::CenterHoldRadius);
	GetFloatValue(ini, "MainWheel.MouseStabilization", "JumpGuardRadius", Config::MainWheel::MouseStabilization::JumpGuardRadius);
	{
		std::uint32_t temp = static_cast<std::uint32_t>(Config::MainWheel::MouseStabilization::MaxJumpSlots);
		if (GetUInt32Value(ini, "MainWheel.MouseStabilization", "MaxJumpSlots", temp)) {
			Config::MainWheel::MouseStabilization::MaxJumpSlots = static_cast<int>(temp);
		}
	}
	GetBoolValue(ini, "MainWheel.MouseStabilization", "StepTowardEnabled", Config::MainWheel::MouseStabilization::StepTowardEnabled);
	GetFloatValue(ini, "MainWheel.MouseStabilization", "BoundaryMarginDeg", Config::MainWheel::MouseStabilization::BoundaryMarginDeg);
	GetBoolValue(ini, "MainWheel.MouseStabilization", "BoundaryMarginLowSpeedOnly", Config::MainWheel::MouseStabilization::BoundaryMarginLowSpeedOnly);
	GetFloatValue(ini, "MainWheel.MouseStabilization", "LowSpeedThreshold", Config::MainWheel::MouseStabilization::LowSpeedThreshold);
	GetBoolValue(ini, "MainWheel.MouseStabilization", "MotionHintingEnabled", Config::MainWheel::MouseStabilization::MotionHinting::Enabled);
	GetFloatValue(ini, "MainWheel.MouseStabilization", "MotionHintingSpeedThreshold", Config::MainWheel::MouseStabilization::MotionHinting::SpeedThreshold);
	GetFloatValue(ini, "MainWheel.MouseStabilization", "MotionHintingBlend", Config::MainWheel::MouseStabilization::MotionHinting::Blend);

	// Backward compatibility with older keys (legacy naming).
	// We only apply these aliases if the new key isn't present in either section.
	auto hasKey = [&](const char* section, const char* key) {
		return ini.GetValue(section, key, nullptr) != nullptr;
	};

	const bool hasReleaseToUse = hasKey("WheelBehavior", "ReleaseToUse") || hasKey("InstantUse", "ReleaseToUse");
	if (!hasReleaseToUse) {
		bool activateOnClose = false;
		if (GetBoolValue(ini, "WheelBehavior", "ActivateOnClose", activateOnClose) ||
			GetBoolValue(ini, "InstantUse", "ActivateOnClose", activateOnClose)) {
			Config::WheelBehavior::ReleaseToUse = activateOnClose;
		}
	}

	const bool hasRTUAlchemy = hasKey("WheelBehavior", "RTUAlchemy") || hasKey("InstantUse", "RTUAlchemy");
	if (!hasRTUAlchemy) {
		bool oldAlchemy = true;
		if (GetBoolValue(ini, "WheelBehavior", "Alchemy", oldAlchemy) ||
			GetBoolValue(ini, "InstantUse", "Alchemy", oldAlchemy)) {
			Config::WheelBehavior::RTUAlchemy = oldAlchemy;
		}
	}

	const bool hasRTUSpell = hasKey("WheelBehavior", "RTUSpell") || hasKey("InstantUse", "RTUSpell");
	if (!hasRTUSpell) {
		bool oldSpell = true;
		if (GetBoolValue(ini, "WheelBehavior", "Spell", oldSpell) ||
			GetBoolValue(ini, "InstantUse", "Spell", oldSpell)) {
			Config::WheelBehavior::RTUSpell = oldSpell;
		}
	}

	// Shout didn't previously have its own key; keep default unless explicitly set.
	const bool hasRTUShout = hasKey("WheelBehavior", "RTUShout") || hasKey("InstantUse", "RTUShout");
	if (!hasRTUShout) {
		bool oldShout = true;
		if (GetBoolValue(ini, "WheelBehavior", "Shout", oldShout) ||
			GetBoolValue(ini, "InstantUse", "Shout", oldShout)) {
			Config::WheelBehavior::RTUShout = oldShout;
		}
	}

	const bool hasInstantSpell = hasKey("WheelBehavior", "InstantSpell") || hasKey("InstantUse", "InstantSpell");
	if (!hasInstantSpell) {
		std::uint32_t oldSpellMode = 0;
		if (GetUInt32Value(ini, "WheelBehavior", "SpellMode", oldSpellMode) ||
			GetUInt32Value(ini, "InstantUse", "SpellMode", oldSpellMode)) {
			Config::WheelBehavior::InstantSpell = oldSpellMode == 2;
		} else {
			bool oldSpellInstantCastNonInstant = false;
			if (GetBoolValue(ini, "WheelBehavior", "SpellInstantCastNonInstant", oldSpellInstantCastNonInstant) ||
				GetBoolValue(ini, "InstantUse", "SpellInstantCastNonInstant", oldSpellInstantCastNonInstant)) {
				Config::WheelBehavior::InstantSpell = oldSpellInstantCastNonInstant;
			}
		}
	}

	const bool hasInstantPowers = hasKey("WheelBehavior", "InstantPowers") || hasKey("InstantUse", "InstantPowers");
	if (!hasInstantPowers) {
		Config::WheelBehavior::InstantPowers = Config::WheelBehavior::InstantSpell;
	}

	// DISABLED: AutoDrawPatch was causing gamepad RT/LT to stop working globally.
	// Native Skyrim behavior already doesn't auto-draw, so no need to patch the engine.
	// If "Auto Draw on Use" is desired, it should be handled in Wheeler.cpp when equipping items.
	// AutoDrawPatch::SetEnabled(!Config::WheelBehavior::AutoDrawOnUse);
}

static void ReadWheelBehaviorConfig(const CSimpleIniA& fallbackIni)
{
	EnsureUserIniBootstrapped(WHEELBEHAVIOR_FACTORY_PATH, WHEELBEHAVIORSETTINGS_PATH, "WheelBehavior");

	CSimpleIniA behaviorIni;
	behaviorIni.SetUnicode();
	bool factoryLayerLoaded = false;
	bool userLayerLoaded = false;
	const bool behaviorLoaded = LoadLayeredIni(
		WHEELBEHAVIOR_FACTORY_PATH,
		WHEELBEHAVIORSETTINGS_PATH,
		behaviorIni,
		&factoryLayerLoaded,
		&userLayerLoaded);

	CSimpleIniA factoryIni;
	factoryIni.SetUnicode();
	const bool factoryLoaded = factoryIni.LoadFile(WHEELBEHAVIOR_FACTORY_PATH) >= 0;

	CSimpleIniA legacyIni;
	legacyIni.SetUnicode();
	const bool legacyLoaded = legacyIni.LoadFile(LEGACY_WHEELBEHAVIORSETTINGS_PATH) >= 0;

	const CSimpleIniA* loadedIni = nullptr;
	const char* activeSourceLabel = "styles_fallback";
	const char* activeSourcePath = STYLESETTINGS_PATH;
	auto logActiveWheelBehaviorConfig = [&](const char* sourceLabel, const char* sourcePath) {
		logger::info(
			"WheelBehavior: Active config source={} path={} factoryLayerLoaded={} userLayerLoaded={} legacyLoaded={} HeavyListCompatibilityMode={} HeavyListCompatibilitySettleMs={:.1f} MutableInventoryHooks={} InstantSpell={} DirectCast={} InstantPowers={} InstantTransformations={} BookReadCompatMode={} BookReadCompatDebugLog={}",
			sourceLabel ? sourceLabel : "unknown",
			sourcePath ? sourcePath : "unknown",
			factoryLayerLoaded,
			userLayerLoaded,
			legacyLoaded,
			Config::WheelBehavior::HeavyListCompatibilityMode,
			Config::WheelBehavior::HeavyListCompatibilitySettleMs,
			Config::WheelBehavior::MutableInventoryHooks,
			Config::WheelBehavior::InstantSpell,
			Config::WheelBehavior::InstantSpellUseDirectCast,
			Config::WheelBehavior::InstantPowers,
			Config::WheelBehavior::InstantTransformations,
			Config::WheelBehavior::BookReadCompat::Mode,
			Config::WheelBehavior::BookReadCompat::DebugLog);
	};

	if (!behaviorLoaded && !legacyLoaded) {
		// Backward compatible: allow wheel behavior settings to live in Styles.ini if no external INI is present.
		ReadWheelBehaviorConfigFromIni(fallbackIni);
		logActiveWheelBehaviorConfig(activeSourceLabel, activeSourcePath);
		return;
	}

	logger::info(
		"WheelBehavior: Loading config (factory={}, user={}, legacy={})",
		factoryLayerLoaded,
		userLayerLoaded,
		legacyLoaded);

	if (!behaviorLoaded && legacyLoaded) {
		ReadWheelBehaviorConfigFromIni(legacyIni);
		loadedIni = &legacyIni;
		activeSourceLabel = "legacy_only";
		activeSourcePath = LEGACY_WHEELBEHAVIORSETTINGS_PATH;

		// Best-effort migration: if only the legacy file exists, write the new file so dMenu can edit it cleanly.
		try {
			if (WriteWheelBehaviorIniFromSnapshot(ReadWheelBehaviorSnapshotFromIni(legacyIni), false)) {
				INFO("Migrated legacy settings to {}", WHEELBEHAVIORSETTINGS_PATH);
			}
		} catch (...) {
			// ignore migration failures; loading still succeeded
		}
	} else if (behaviorLoaded) {
		if (factoryLoaded) {
			AppendTransformFactoryCsvDefaults(behaviorIni, factoryIni);
		}
		ReadWheelBehaviorConfigFromIni(behaviorIni);
		loadedIni = &behaviorIni;
		activeSourceLabel = userLayerLoaded ?
			(factoryLayerLoaded ? "layered_user+factory" : "user_only") :
			"factory_only";
		activeSourcePath = userLayerLoaded ? WHEELBEHAVIORSETTINGS_PATH : WHEELBEHAVIOR_FACTORY_PATH;

		// If a legacy config exists and the current behavior INI appears to be untouched defaults,
		// automatically migrate the legacy config into the new file to preserve user settings on upgrade.
		if (legacyLoaded) {
			const WheelBehaviorSnapshot defaults = factoryLoaded ?
				ReadWheelBehaviorSnapshotFromIni(factoryIni) :
				DefaultWheelBehaviorSnapshot();
			const WheelBehaviorSnapshot behaviorSnap = ReadWheelBehaviorSnapshotFromIni(behaviorIni);
			const WheelBehaviorSnapshot legacySnap = ReadWheelBehaviorSnapshotFromIni(legacyIni);

			const bool behaviorIsDefault = SnapshotEquals(behaviorSnap, defaults);
			const bool legacyIsNonDefault = !SnapshotEquals(legacySnap, defaults);

			if (behaviorIsDefault && legacyIsNonDefault) {
				ApplyWheelBehaviorSnapshotToConfig(legacySnap);
				loadedIni = &legacyIni;
				activeSourceLabel = "legacy_migrated_over_default_behavior";
				activeSourcePath = LEGACY_WHEELBEHAVIORSETTINGS_PATH;
				try {
					if (WriteWheelBehaviorIniFromSnapshot(legacySnap, true)) {
						INFO("Migrated legacy settings into {}", WHEELBEHAVIORSETTINGS_PATH);
					}
				} catch (...) {
					// ignore migration failures; config is still loaded from legacy for this run
				}
			}
		}
	}

	// Backward compatible: if the user hasn't added Styling.HoverDelay to the behavior INI yet,
	// allow reading it from Styles.ini (reskins/older configs).
	if (!loadedIni || !GetBoolValue(*loadedIni, "Styling.HoverDelay", "Enabled", Config::Styling::HoverDelay::Enabled)) {
		GetBoolValue(fallbackIni, "Styling.HoverDelay", "Enabled", Config::Styling::HoverDelay::Enabled);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "Radius", Config::Styling::HoverDelay::Radius)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "Radius", Config::Styling::HoverDelay::Radius);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "RadiusOffset", Config::Styling::HoverDelay::RadiusOffset)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "RadiusOffset", Config::Styling::HoverDelay::RadiusOffset);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "Thickness", Config::Styling::HoverDelay::Thickness)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "Thickness", Config::Styling::HoverDelay::Thickness);
	}
	if (!loadedIni || !GetUInt32Value(*loadedIni, "Styling.HoverDelay", "Color", Config::Styling::HoverDelay::Color)) {
		GetUInt32Value(fallbackIni, "Styling.HoverDelay", "Color", Config::Styling::HoverDelay::Color);
	}
	if (!loadedIni || !GetUInt32Value(*loadedIni, "Styling.HoverDelay", "BackgroundColor", Config::Styling::HoverDelay::BackgroundColor)) {
		GetUInt32Value(fallbackIni, "Styling.HoverDelay", "BackgroundColor", Config::Styling::HoverDelay::BackgroundColor);
	}
	bool hasInstantShoutAnimateReveal = loadedIni &&
		GetBoolValue(*loadedIni, "Styling.HoverDelay", "InstantShoutAnimateReveal", Config::Styling::HoverDelay::InstantShoutAnimateReveal);
	if (!hasInstantShoutAnimateReveal && loadedIni) {
		hasInstantShoutAnimateReveal =
			GetBoolValue(*loadedIni, "Styling.HoverDelay", "InstantShoutUseRevealAnimation", Config::Styling::HoverDelay::InstantShoutAnimateReveal);
	}
	if (!hasInstantShoutAnimateReveal) {
		if (!GetBoolValue(fallbackIni, "Styling.HoverDelay", "InstantShoutAnimateReveal", Config::Styling::HoverDelay::InstantShoutAnimateReveal)) {
			GetBoolValue(fallbackIni, "Styling.HoverDelay", "InstantShoutUseRevealAnimation", Config::Styling::HoverDelay::InstantShoutAnimateReveal);
		}
	}
	logActiveWheelBehaviorConfig(activeSourceLabel, activeSourcePath);
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantShoutAssetScale", Config::Styling::HoverDelay::InstantShoutAssetScale)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantShoutAssetScale", Config::Styling::HoverDelay::InstantShoutAssetScale);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantShoutAssetOffsetX", Config::Styling::HoverDelay::InstantShoutAssetOffsetX)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantShoutAssetOffsetX", Config::Styling::HoverDelay::InstantShoutAssetOffsetX);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantShoutAssetOffsetY", Config::Styling::HoverDelay::InstantShoutAssetOffsetY)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantShoutAssetOffsetY", Config::Styling::HoverDelay::InstantShoutAssetOffsetY);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantShoutStage1OffsetX", Config::Styling::HoverDelay::InstantShoutStage1OffsetX)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantShoutStage1OffsetX", Config::Styling::HoverDelay::InstantShoutStage1OffsetX);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantShoutStage1OffsetY", Config::Styling::HoverDelay::InstantShoutStage1OffsetY)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantShoutStage1OffsetY", Config::Styling::HoverDelay::InstantShoutStage1OffsetY);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantShoutStage1RotationDeg", Config::Styling::HoverDelay::InstantShoutStage1RotationDeg)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantShoutStage1RotationDeg", Config::Styling::HoverDelay::InstantShoutStage1RotationDeg);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantShoutStage2OffsetX", Config::Styling::HoverDelay::InstantShoutStage2OffsetX)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantShoutStage2OffsetX", Config::Styling::HoverDelay::InstantShoutStage2OffsetX);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantShoutStage2OffsetY", Config::Styling::HoverDelay::InstantShoutStage2OffsetY)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantShoutStage2OffsetY", Config::Styling::HoverDelay::InstantShoutStage2OffsetY);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantShoutStage2RotationDeg", Config::Styling::HoverDelay::InstantShoutStage2RotationDeg)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantShoutStage2RotationDeg", Config::Styling::HoverDelay::InstantShoutStage2RotationDeg);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantShoutStage3OffsetX", Config::Styling::HoverDelay::InstantShoutStage3OffsetX)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantShoutStage3OffsetX", Config::Styling::HoverDelay::InstantShoutStage3OffsetX);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantShoutStage3OffsetY", Config::Styling::HoverDelay::InstantShoutStage3OffsetY)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantShoutStage3OffsetY", Config::Styling::HoverDelay::InstantShoutStage3OffsetY);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantShoutStage3RotationDeg", Config::Styling::HoverDelay::InstantShoutStage3RotationDeg)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantShoutStage3RotationDeg", Config::Styling::HoverDelay::InstantShoutStage3RotationDeg);
	}
	if (!loadedIni || !GetUInt32Value(*loadedIni, "Styling.HoverDelay", "InstantSpellColor", Config::Styling::HoverDelay::InstantSpellColor)) {
		GetUInt32Value(fallbackIni, "Styling.HoverDelay", "InstantSpellColor", Config::Styling::HoverDelay::InstantSpellColor);
	}
	if (!loadedIni || !GetUInt32Value(*loadedIni, "Styling.HoverDelay", "InstantSpellBackgroundColor", Config::Styling::HoverDelay::InstantSpellBackgroundColor)) {
		GetUInt32Value(fallbackIni, "Styling.HoverDelay", "InstantSpellBackgroundColor", Config::Styling::HoverDelay::InstantSpellBackgroundColor);
	}
	bool hasInstantReskinEnabled = loadedIni && GetBoolValue(*loadedIni, "Styling.HoverDelay", "InstantSpellUseReskinAssets", Config::Styling::HoverDelay::InstantSpellUseReskinAssets);
	if (!hasInstantReskinEnabled && loadedIni) {
		hasInstantReskinEnabled = GetBoolValue(*loadedIni, "Styling.HoverDelay", "InstantSpellAssetsEnabled", Config::Styling::HoverDelay::InstantSpellUseReskinAssets);
	}
	if (!hasInstantReskinEnabled) {
		if (!GetBoolValue(fallbackIni, "Styling.HoverDelay", "InstantSpellUseReskinAssets", Config::Styling::HoverDelay::InstantSpellUseReskinAssets)) {
			GetBoolValue(fallbackIni, "Styling.HoverDelay", "InstantSpellAssetsEnabled", Config::Styling::HoverDelay::InstantSpellUseReskinAssets);
		}
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantSpellAssetScale", Config::Styling::HoverDelay::InstantSpellAssetScale)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantSpellAssetScale", Config::Styling::HoverDelay::InstantSpellAssetScale);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantSpellAssetOffsetX", Config::Styling::HoverDelay::InstantSpellAssetOffsetX)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantSpellAssetOffsetX", Config::Styling::HoverDelay::InstantSpellAssetOffsetX);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantSpellAssetOffsetY", Config::Styling::HoverDelay::InstantSpellAssetOffsetY)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantSpellAssetOffsetY", Config::Styling::HoverDelay::InstantSpellAssetOffsetY);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantSpellAssetOpacity", Config::Styling::HoverDelay::InstantSpellAssetOpacity)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantSpellAssetOpacity", Config::Styling::HoverDelay::InstantSpellAssetOpacity);
	}
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantSpellHandIndicatorScale", Config::Styling::HoverDelay::InstantSpellHandIndicatorScale)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantSpellHandIndicatorScale", Config::Styling::HoverDelay::InstantSpellHandIndicatorScale);
	}
	LoadInstantSpellHandIndicatorOffsetsWithFallback(
		loadedIni,
		fallbackIni,
		"Styling.HoverDelay",
		Config::Styling::HoverDelay::InstantSpellHandIndicatorOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorOffsetY,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetY,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetY,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetY);
	if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantSpellHandIndicatorOpacity", Config::Styling::HoverDelay::InstantSpellHandIndicatorOpacity)) {
		GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantSpellHandIndicatorOpacity", Config::Styling::HoverDelay::InstantSpellHandIndicatorOpacity);
	}
	bool hasInstantAtlasEnabled = loadedIni && GetBoolValue(*loadedIni, "Styling.HoverDelay", "InstantSpellUseAtlasAnimation", Config::Styling::HoverDelay::InstantSpellUseAtlasAnimation);
	if (!hasInstantAtlasEnabled && loadedIni) {
		hasInstantAtlasEnabled = GetBoolValue(*loadedIni, "Styling.HoverDelay", "InstantSpellAtlasEnabled", Config::Styling::HoverDelay::InstantSpellUseAtlasAnimation);
	}
	if (!hasInstantAtlasEnabled) {
		if (!GetBoolValue(fallbackIni, "Styling.HoverDelay", "InstantSpellUseAtlasAnimation", Config::Styling::HoverDelay::InstantSpellUseAtlasAnimation)) {
			GetBoolValue(fallbackIni, "Styling.HoverDelay", "InstantSpellAtlasEnabled", Config::Styling::HoverDelay::InstantSpellUseAtlasAnimation);
		}
	}
	{
		float tmpCols = static_cast<float>(Config::Styling::HoverDelay::InstantSpellAtlasCols);
		if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantSpellAtlasCols", tmpCols)) {
			GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantSpellAtlasCols", tmpCols);
		}
		Config::Styling::HoverDelay::InstantSpellAtlasCols = static_cast<std::uint32_t>((std::max)(1.0f, std::round(tmpCols)));
	}
	{
		float tmpRows = static_cast<float>(Config::Styling::HoverDelay::InstantSpellAtlasRows);
		if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantSpellAtlasRows", tmpRows)) {
			GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantSpellAtlasRows", tmpRows);
		}
		Config::Styling::HoverDelay::InstantSpellAtlasRows = static_cast<std::uint32_t>((std::max)(1.0f, std::round(tmpRows)));
	}
	{
		float tmpFrames = static_cast<float>(Config::Styling::HoverDelay::InstantSpellAtlasFrameCount);
		if (!loadedIni || !GetFloatValue(*loadedIni, "Styling.HoverDelay", "InstantSpellAtlasFrameCount", tmpFrames)) {
			GetFloatValue(fallbackIni, "Styling.HoverDelay", "InstantSpellAtlasFrameCount", tmpFrames);
		}
		Config::Styling::HoverDelay::InstantSpellAtlasFrameCount = static_cast<std::uint32_t>((std::max)(1.0f, std::round(tmpFrames)));
	}
	Config::Styling::HoverDelay::InstantSpellAssetScale = std::clamp(Config::Styling::HoverDelay::InstantSpellAssetScale, 0.1f, 12.0f);
	Config::Styling::HoverDelay::InstantSpellAssetOffsetX = std::clamp(Config::Styling::HoverDelay::InstantSpellAssetOffsetX, -200.0f, 200.0f);
	Config::Styling::HoverDelay::InstantSpellAssetOffsetY = std::clamp(Config::Styling::HoverDelay::InstantSpellAssetOffsetY, -200.0f, 200.0f);
	Config::Styling::HoverDelay::InstantSpellAssetOpacity = std::clamp(Config::Styling::HoverDelay::InstantSpellAssetOpacity, 0.0f, 1.0f);
	Config::Styling::HoverDelay::InstantSpellHandIndicatorScale = std::clamp(Config::Styling::HoverDelay::InstantSpellHandIndicatorScale, 0.1f, 12.0f);
	ClampInstantSpellHandIndicatorOffsets(
		Config::Styling::HoverDelay::InstantSpellHandIndicatorOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorOffsetY,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetY,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetY,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetX,
		Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetY);
	Config::Styling::HoverDelay::InstantSpellHandIndicatorOpacity = std::clamp(Config::Styling::HoverDelay::InstantSpellHandIndicatorOpacity, 0.0f, 1.0f);
	Config::Styling::HoverDelay::InstantShoutAssetScale = std::clamp(Config::Styling::HoverDelay::InstantShoutAssetScale, 0.1f, 12.0f);
	Config::Styling::HoverDelay::InstantShoutAssetOffsetX = std::clamp(Config::Styling::HoverDelay::InstantShoutAssetOffsetX, -500.0f, 500.0f);
	Config::Styling::HoverDelay::InstantShoutAssetOffsetY = std::clamp(Config::Styling::HoverDelay::InstantShoutAssetOffsetY, -500.0f, 500.0f);
	Config::Styling::HoverDelay::InstantShoutStage1OffsetX = std::clamp(Config::Styling::HoverDelay::InstantShoutStage1OffsetX, -500.0f, 500.0f);
	Config::Styling::HoverDelay::InstantShoutStage1OffsetY = std::clamp(Config::Styling::HoverDelay::InstantShoutStage1OffsetY, -500.0f, 500.0f);
	Config::Styling::HoverDelay::InstantShoutStage1RotationDeg = std::clamp(Config::Styling::HoverDelay::InstantShoutStage1RotationDeg, -360.0f, 360.0f);
	Config::Styling::HoverDelay::InstantShoutStage2OffsetX = std::clamp(Config::Styling::HoverDelay::InstantShoutStage2OffsetX, -500.0f, 500.0f);
	Config::Styling::HoverDelay::InstantShoutStage2OffsetY = std::clamp(Config::Styling::HoverDelay::InstantShoutStage2OffsetY, -500.0f, 500.0f);
	Config::Styling::HoverDelay::InstantShoutStage2RotationDeg = std::clamp(Config::Styling::HoverDelay::InstantShoutStage2RotationDeg, -360.0f, 360.0f);
	Config::Styling::HoverDelay::InstantShoutStage3OffsetX = std::clamp(Config::Styling::HoverDelay::InstantShoutStage3OffsetX, -500.0f, 500.0f);
	Config::Styling::HoverDelay::InstantShoutStage3OffsetY = std::clamp(Config::Styling::HoverDelay::InstantShoutStage3OffsetY, -500.0f, 500.0f);
	Config::Styling::HoverDelay::InstantShoutStage3RotationDeg = std::clamp(Config::Styling::HoverDelay::InstantShoutStage3RotationDeg, -360.0f, 360.0f);
	Config::Styling::HoverDelay::InstantSpellAtlasCols = std::clamp(Config::Styling::HoverDelay::InstantSpellAtlasCols, 1u, 64u);
	Config::Styling::HoverDelay::InstantSpellAtlasRows = std::clamp(Config::Styling::HoverDelay::InstantSpellAtlasRows, 1u, 64u);
	{
		const std::uint32_t maxFrames = Config::Styling::HoverDelay::InstantSpellAtlasCols * Config::Styling::HoverDelay::InstantSpellAtlasRows;
		Config::Styling::HoverDelay::InstantSpellAtlasFrameCount = std::clamp(Config::Styling::HoverDelay::InstantSpellAtlasFrameCount, 1u, (std::max)(1u, maxFrames));
	}
	ApplyInstantSpellIndicatorAssetPathHardcoded();
}

bool Config::StoreWheelBehaviorDefaultsIfMissing()
{
	return false;
}

bool Config::RestoreWheelBehaviorDefaults()
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (!fs::exists(WHEELBEHAVIOR_FACTORY_PATH, ec) || ec) {
		return false;
	}
	ec.clear();
	const bool ok = fs::copy_file(WHEELBEHAVIOR_FACTORY_PATH, WHEELBEHAVIORSETTINGS_PATH, fs::copy_options::overwrite_existing, ec);
	return ok && !ec;
}
void Config::ReadStyleConfig()
{
	EnsureUserIniBootstrapped(STYLEDEFAULTS_PATH, STYLESETTINGS_PATH, "Styles");

	CSimpleIniA ini;
	ini.SetUnicode();
	bool defaultsLoaded = false;
	bool userLoaded = false;
	if (!LoadLayeredIni(STYLEDEFAULTS_PATH, STYLESETTINGS_PATH, ini, &defaultsLoaded, &userLoaded)) {
		logger::warn(
			"Styles: Failed to load config from '{}' or '{}', using runtime defaults",
			STYLESETTINGS_PATH,
			STYLEDEFAULTS_PATH);
	} else {
		logger::info(
			"Styles: Loading config (defaults={}, user={})",
			defaultsLoaded,
			userLoaded);
	}

	Config::MainWheel::LayoutScaling::ConfigPresent = false;
	Config::MainWheel::LayoutScaling::LoadedSourceTag = "off";
	Config::MainWheel::LayoutScaling::LoadedSourcePath.clear();
	Config::AmmoWheel::LayoutScaling::ConfigPresent = false;
	Config::AmmoWheel::LayoutScaling::LoadedSourceTag = "off";
	Config::AmmoWheel::LayoutScaling::LoadedSourcePath.clear();

	const I4ConfigValues codeDefaultI4{};
	const I4ConfigValues i4Defaults = LoadI4DefaultsSnapshot();
	I4ConfigValues effectiveI4 = codeDefaultI4;
	std::ifstream i4UserFile(I4_SETTINGS_PATH, std::ios::binary);
	const bool hasI4UserConfig = i4UserFile.good();
	i4UserFile.close();
	bool i4DefaultsLoaded = false;
	bool i4UserLoaded = false;
	CSimpleIniA i4Ini;
	i4Ini.SetUnicode();
	if (hasI4UserConfig) {
		if (!LoadLayeredIni(I4_DEFAULTS_PATH, I4_SETTINGS_PATH, i4Ini, &i4DefaultsLoaded, &i4UserLoaded)) {
			logger::warn(
				"I4: Failed to load dedicated config from '{}' or '{}'; using runtime defaults",
				I4_SETTINGS_PATH,
				I4_DEFAULTS_PATH);
		} else {
			logger::info(
				"I4: Loading config (defaults={}, user={})",
				i4DefaultsLoaded,
				i4UserLoaded);
			LoadI4ConfigValues(i4Ini, effectiveI4);
			ClampI4ConfigValues(effectiveI4);
		}
	} else {
		logger::info(
			"I4: '{}' missing; keeping dedicated feature off/code defaults",
			I4_SETTINGS_PATH);
	}
	ApplyI4ConfigValues(effectiveI4);

	GetFloatValue(ini, "Styling.Wheel", "CursorIndicatorDist", Config::Styling::Wheel::CursorIndicatorDist);
	GetFloatValue(ini, "Styling.Wheel", "CusorIndicatorArcWidth", Config::Styling::Wheel::CusorIndicatorArcWidth);
	GetFloatValue(ini, "Styling.Wheel", "CursorIndicatorArcAngle", Config::Styling::Wheel::CursorIndicatorArcAngle);
	GetFloatValue(ini, "Styling.Wheel", "CursorIndicatorTriangleSideLength", Config::Styling::Wheel::CursorIndicatorTriangleSideLength);
	GetUInt32Value(ini, "Styling.Wheel", "CursorIndicatorColor", Config::Styling::Wheel::CursorIndicatorColor);
	GetBoolValue(ini, "Styling.Wheel", "CursorIndicatorInwardFacing", Config::Styling::Wheel::CursorIndicatorInwardFacing);

	GetBoolValue(ini, "Styling.Wheel", "UseGeometricPrimitiveForBackgroundTexture", Config::Styling::Wheel::UseGeometricPrimitiveForBackgroundTexture);
	GetFloatValue(ini, "Styling.Wheel", "WheelBackgroundTextureScale", Config::Styling::Wheel::WheelBackgroundTextureScale);

	GetFloatValue(ini, "Styling.Wheel", "WheelIndicatorOffsetX", Config::Styling::Wheel::WheelIndicatorOffsetX);
	GetFloatValue(ini, "Styling.Wheel", "WheelIndicatorOffsetY", Config::Styling::Wheel::WheelIndicatorOffsetY);
	GetFloatValue(ini, "Styling.Wheel", "WheelIndicatorSize", Config::Styling::Wheel::WheelIndicatorSize);
	GetFloatValue(ini, "Styling.Wheel", "WheelIndicatorSpacing", Config::Styling::Wheel::WheelIndicatorSpacing);
	GetUInt32Value(ini, "Styling.Wheel", "WheelIndicatorActiveColor", Config::Styling::Wheel::WheelIndicatorActiveColor);
	GetUInt32Value(ini, "Styling.Wheel", "WheelIndicatorInactiveColor", Config::Styling::Wheel::WheelIndicatorInactiveColor);
	{
		uint32_t alignment = static_cast<uint32_t>(Config::Styling::Wheel::WheelIndicatorAlignment);
		if (GetUInt32Value(ini, "Styling.Wheel", "WheelIndicatorAlignment", alignment)) {
			if (alignment <= static_cast<uint32_t>(Config::WidgetAlignment::kCenter)) {
				Config::Styling::Wheel::WheelIndicatorAlignment =
					static_cast<Config::WidgetAlignment>(alignment);
			}
		}
	}
	GetFloatValue(ini, "Styling.Wheel", "InnerCircleRadius", Config::Styling::Wheel::InnerCircleRadius);
	GetFloatValue(ini, "Styling.Wheel", "OuterCircleRadius", Config::Styling::Wheel::OuterCircleRadius);
	GetFloatValue(ini, "Styling.Wheel", "InnerSpacing", Config::Styling::Wheel::InnerSpacing);

	GetUInt32Value(ini, "Styling.Wheel", "HoveredColorBegin", Config::Styling::Wheel::HoveredColorBegin);
	GetUInt32Value(ini, "Styling.Wheel", "HoveredColorEnd", Config::Styling::Wheel::HoveredColorEnd);
	GetUInt32Value(ini, "Styling.Wheel", "UnhoveredColorBegin", Config::Styling::Wheel::UnhoveredColorBegin);
	GetUInt32Value(ini, "Styling.Wheel", "UnhoveredColorEnd", Config::Styling::Wheel::UnhoveredColorEnd);
	GetUInt32Value(ini, "Styling.Wheel", "ActiveArcColorBegin", Config::Styling::Wheel::ActiveArcColorBegin);
	GetUInt32Value(ini, "Styling.Wheel", "ActiveArcColorEnd", Config::Styling::Wheel::ActiveArcColorEnd);
	GetUInt32Value(ini, "Styling.Wheel", "InActiveArcColorBegin", Config::Styling::Wheel::InActiveArcColorBegin);
	GetUInt32Value(ini, "Styling.Wheel", "InActiveArcColorEnd", Config::Styling::Wheel::InActiveArcColorEnd);
	GetFloatValue(ini, "Styling.Wheel", "ActiveArcWidth", Config::Styling::Wheel::ActiveArcWidth);
	GetBoolValue(ini, "Styling.Wheel", "BlurOnOpen", Config::Styling::Wheel::BlurOnOpen);
	GetFloatValue(ini, "Styling.Wheel", "SlowTimeScale", Config::Styling::Wheel::SlowTimeScale);
	GetFloatValue(ini, "Styling.Wheel", "CenterOffsetX", Config::Styling::Wheel::CenterOffsetX);
	GetFloatValue(ini, "Styling.Wheel", "CenterOffsetY", Config::Styling::Wheel::CenterOffsetY);

	GetUInt32Value(ini, "Styling.Wheel", "TextShadowColor", Config::Styling::Wheel::TextShadowColor);
	GetUInt32Value(ini, "Styling.Wheel", "TextColor", Config::Styling::Wheel::TextColor);

	// AmmoWheel-specific styling (reskin support)
	// These override the defaults in Config::AmmoWheel when UseMainWheelTheme is false
	GetUInt32Value(ini, "Styling.AmmoWheel", "UnhoveredColorBegin", Config::AmmoWheel::UnhoveredColorBegin);
	GetUInt32Value(ini, "Styling.AmmoWheel", "UnhoveredColorEnd", Config::AmmoWheel::UnhoveredColorEnd);
	GetUInt32Value(ini, "Styling.AmmoWheel", "HoveredColorBegin", Config::AmmoWheel::HoveredColorBegin);
	GetUInt32Value(ini, "Styling.AmmoWheel", "HoveredColorEnd", Config::AmmoWheel::HoveredColorEnd);
	GetUInt32Value(ini, "Styling.AmmoWheel", "ActiveArcColorBegin", Config::AmmoWheel::ActiveArcColorBegin);
	GetUInt32Value(ini, "Styling.AmmoWheel", "ActiveArcColorEnd", Config::AmmoWheel::ActiveArcColorEnd);
	GetUInt32Value(ini, "Styling.AmmoWheel", "LowAmmoIndicatorColor", Config::AmmoWheel::LowAmmoIndicatorColor);
	GetUInt32Value(ini, "Styling.AmmoWheel", "SelectedIndicatorColor", Config::AmmoWheel::SelectedIndicatorColor);
	GetUInt32Value(ini, "Styling.AmmoWheel", "HoverHighlightColor", Config::AmmoWheel::HoverHighlightColor);
	GetUInt32Value(ini, "Styling.AmmoWheel", "NameTextColor", Config::AmmoWheel::NameTextColor);

	GetFloatValue(ini, "Styling.Entry.Highlight.Text", "OffsetX", Config::Styling::Entry::Highlight::Text::OffsetX);
	GetFloatValue(ini, "Styling.Entry.Highlight.Text", "OffsetY", Config::Styling::Entry::Highlight::Text::OffsetY);
	GetFloatValue(ini, "Styling.Entry.Highlight.Text", "Size", Config::Styling::Entry::Highlight::Text::Size);

	GetFloatValue(ini, "Styling.Item.Highlight.Texture", "OffsetX", Config::Styling::Item::Highlight::Texture::OffsetX);
	GetFloatValue(ini, "Styling.Item.Highlight.Texture", "OffsetY", Config::Styling::Item::Highlight::Texture::OffsetY);
	GetFloatValue(ini, "Styling.Item.Highlight.Texture", "Scale", Config::Styling::Item::Highlight::Texture::Scale);
	GetFloatValue(ini, "Styling.Item.Highlight.Text", "OffsetX", Config::Styling::Item::Highlight::Text::OffsetX);
	GetFloatValue(ini, "Styling.Item.Highlight.Text", "OffsetY", Config::Styling::Item::Highlight::Text::OffsetY);
	GetFloatValue(ini, "Styling.Item.Highlight.Text", "Size", Config::Styling::Item::Highlight::Text::Size);
	GetBoolValue(ini, "Styling.Item.Highlight.Text", "AutoFit", Config::Styling::Item::Highlight::Text::AutoFit);
	GetFloatValue(ini, "Styling.Item.Highlight.Text", "MinSize", Config::Styling::Item::Highlight::Text::MinSize);
	GetFloatValue(ini, "Styling.Item.Highlight.Text", "MaxWidth", Config::Styling::Item::Highlight::Text::MaxWidth);
	Config::Styling::Item::Highlight::Text::MinSize =
		std::clamp(Config::Styling::Item::Highlight::Text::MinSize, 8.0f, Config::Styling::Item::Highlight::Text::Size);
	Config::Styling::Item::Highlight::Text::MaxWidth =
		(std::max)(0.0f, Config::Styling::Item::Highlight::Text::MaxWidth);
	GetFloatValue(ini, "Styling.Item.Highlight.Desc", "OffsetX", Config::Styling::Item::Highlight::Desc::OffsetX);
	GetFloatValue(ini, "Styling.Item.Highlight.Desc", "OffsetY", Config::Styling::Item::Highlight::Desc::OffsetY);
	GetFloatValue(ini, "Styling.Item.Highlight.Desc", "Size", Config::Styling::Item::Highlight::Desc::Size);
	GetFloatValue(ini, "Styling.Item.Highlight.Desc", "LineLength", Config::Styling::Item::Highlight::Desc::LineLength);
	GetFloatValue(ini, "Styling.Item.Highlight.Desc", "LineSpacing", Config::Styling::Item::Highlight::Desc::LineSpacing);
	GetBoolValue(ini, "Styling.Item.Highlight.Desc", "AutoFit", Config::Styling::Item::Highlight::Desc::AutoFit);
	GetFloatValue(ini, "Styling.Item.Highlight.Desc", "MinSize", Config::Styling::Item::Highlight::Desc::MinSize);
	GetFloatValue(ini, "Styling.Item.Highlight.Desc", "MaxHeight", Config::Styling::Item::Highlight::Desc::MaxHeight);
	{
		uint32_t maxLines = Config::Styling::Item::Highlight::Desc::MaxLines;
		if (GetUInt32Value(ini, "Styling.Item.Highlight.Desc", "MaxLines", maxLines)) {
			Config::Styling::Item::Highlight::Desc::MaxLines = std::clamp(maxLines, 1u, 12u);
		}
	}
	GetFloatValue(ini, "Styling.Item.Highlight.Desc", "BottomSafeMargin", Config::Styling::Item::Highlight::Desc::BottomSafeMargin);
	GetBoolValue(ini, "Styling.Item.Highlight.Desc", "AutoShiftUp", Config::Styling::Item::Highlight::Desc::AutoShiftUp);
	GetFloatValue(ini, "Styling.Item.Highlight.Desc", "ShiftUpThreshold", Config::Styling::Item::Highlight::Desc::ShiftUpThreshold);
	GetFloatValue(ini, "Styling.Item.Highlight.Desc", "MaxShiftUp", Config::Styling::Item::Highlight::Desc::MaxShiftUp);
	Config::Styling::Item::Highlight::Desc::MinSize =
		std::clamp(Config::Styling::Item::Highlight::Desc::MinSize, 8.0f, Config::Styling::Item::Highlight::Desc::Size);
	Config::Styling::Item::Highlight::Desc::MaxHeight =
		(std::max)(0.0f, Config::Styling::Item::Highlight::Desc::MaxHeight);
	Config::Styling::Item::Highlight::Desc::BottomSafeMargin =
		(std::max)(0.0f, Config::Styling::Item::Highlight::Desc::BottomSafeMargin);
	Config::Styling::Item::Highlight::Desc::ShiftUpThreshold =
		(std::max)(0.0f, Config::Styling::Item::Highlight::Desc::ShiftUpThreshold);
	Config::Styling::Item::Highlight::Desc::MaxShiftUp =
		(std::max)(0.0f, Config::Styling::Item::Highlight::Desc::MaxShiftUp);
	Config::Styling::Item::Highlight::Desc::LineSpacing =
		(std::max)(0.0f, Config::Styling::Item::Highlight::Desc::LineSpacing);
	
	GetFloatValue(ini, "Styling.Item.Highlight.StatIcon", "OffsetX", Config::Styling::Item::Highlight::StatIcon::OffsetX);
	GetFloatValue(ini, "Styling.Item.Highlight.StatIcon", "OffsetY", Config::Styling::Item::Highlight::StatIcon::OffsetY);
	GetFloatValue(ini, "Styling.Item.Highlight.StatIcon", "Scale", Config::Styling::Item::Highlight::StatIcon::Scale);

	GetFloatValue(ini, "Styling.Item.Highlight.StatText", "OffsetX", Config::Styling::Item::Highlight::StatText::OffsetX);
	GetFloatValue(ini, "Styling.Item.Highlight.StatText", "OffsetY", Config::Styling::Item::Highlight::StatText::OffsetY);
	GetFloatValue(ini, "Styling.Item.Highlight.StatText", "Size", Config::Styling::Item::Highlight::StatText::Size);

	GetFloatValue(ini, "Styling.Item.Slot.Texture", "OffsetX", Config::Styling::Item::Slot::Texture::OffsetX);
	GetFloatValue(ini, "Styling.Item.Slot.Texture", "OffsetY", Config::Styling::Item::Slot::Texture::OffsetY);
	GetFloatValue(ini, "Styling.Item.Slot.Texture", "Scale", Config::Styling::Item::Slot::Texture::Scale);
	GetFloatValue(ini, "Styling.Item.Slot.Text", "OffsetX", Config::Styling::Item::Slot::Text::OffsetX);
	GetFloatValue(ini, "Styling.Item.Slot.Text", "OffsetY", Config::Styling::Item::Slot::Text::OffsetY);
	GetFloatValue(ini, "Styling.Item.Slot.Text", "Size", Config::Styling::Item::Slot::Text::Size);
	GetBoolValue(ini, "Styling.Item.Slot.Text", "AutoFit", Config::Styling::Item::Slot::Text::AutoFit);
	GetFloatValue(ini, "Styling.Item.Slot.Text", "MinSize", Config::Styling::Item::Slot::Text::MinSize);
	GetFloatValue(ini, "Styling.Item.Slot.Text", "MaxWidth", Config::Styling::Item::Slot::Text::MaxWidth);
	{
		uint32_t autoWidthMinChars = Config::Styling::Item::Slot::Text::AutoWidthMinChars;
		if (GetUInt32Value(ini, "Styling.Item.Slot.Text", "AutoWidthMinChars", autoWidthMinChars)) {
			Config::Styling::Item::Slot::Text::AutoWidthMinChars = std::clamp(autoWidthMinChars, 4u, 30u);
		}
	}
	GetFloatValue(ini, "Styling.Item.Slot.Text", "MaxHeight", Config::Styling::Item::Slot::Text::MaxHeight);
	{
		uint32_t maxLines = Config::Styling::Item::Slot::Text::MaxLines;
		if (GetUInt32Value(ini, "Styling.Item.Slot.Text", "MaxLines", maxLines)) {
			Config::Styling::Item::Slot::Text::MaxLines = std::clamp(maxLines, 1u, 4u);
		}
	}
	GetFloatValue(ini, "Styling.Item.Slot.Text", "LineSpacing", Config::Styling::Item::Slot::Text::LineSpacing);
	Config::Styling::Item::Slot::Text::MinSize =
		std::clamp(Config::Styling::Item::Slot::Text::MinSize, 8.0f, Config::Styling::Item::Slot::Text::Size);
	Config::Styling::Item::Slot::Text::MaxWidth =
		(std::max)(0.0f, Config::Styling::Item::Slot::Text::MaxWidth);
	Config::Styling::Item::Slot::Text::MaxHeight =
		(std::max)(0.0f, Config::Styling::Item::Slot::Text::MaxHeight);
	Config::Styling::Item::Slot::Text::LineSpacing =
		(std::max)(0.0f, Config::Styling::Item::Slot::Text::LineSpacing);

	GetFloatValue(ini, "Styling.Item.Slot.BackgroundTexture", "Scale", Config::Styling::Item::Slot::BackgroundTexture::Scale);

	GetFloatValue(ini, "Animation", "EntryHighlightExpandTime", Config::Animation::EntryHighlightExpandTime);
	GetFloatValue(ini, "Animation", "EntryHighlightRetractTime", Config::Animation::EntryHighlightRetractTime);
	GetFloatValue(ini, "Animation", "EntryHighlightExpandScale", Config::Animation::EntryHighlightExpandScale);
	GetFloatValue(ini, "Animation", "EntryInputBumpScale", Config::Animation::EntryInputBumpScale);
	GetFloatValue(ini, "Animation", "EntryInputBumpTime", Config::Animation::EntryInputBumpTime);
	GetFloatValue(ini, "Animation", "ToggleHorizontalFadeDistance", Config::Animation::ToggleHorizontalFadeDistance);
	GetFloatValue(ini, "Animation", "ToggleVerticalFadeDistance", Config::Animation::ToggleVerticalFadeDistance);
	GetFloatValue(ini, "Animation", "FadeTime", Config::Animation::FadeTime);

	// Font / Glyph settings (also read from Styles.ini for convenience)
	{
		int glyphPresetInt = Config::Font::GlyphPreset;
		if (GetUInt32Value(ini, "Font", "GlyphPreset", reinterpret_cast<uint32_t&>(glyphPresetInt))) {
			Config::Font::GlyphPreset = glyphPresetInt;
		}
	}
	GetStringValue(ini, "Font", "CustomRanges", Config::Font::CustomRanges);
	GetBoolValue(ini, "Font.Debug", "ShowGlyphTestOverlay", Config::Font::Debug::ShowGlyphTestOverlay);
	GetBoolValue(ini, "Font.Debug", "LogAtlasInfo", Config::Font::Debug::LogAtlasInfo);

	// Resolution mismatch handling (optional)
	ReadResolutionFixConfig(ini);

	// Wheel behavior settings are stored in a separate INI for compatibility with reskins.
	ReadWheelBehaviorConfig(ini);

	// dMenu Wheeler Styles owns instant-cast reskin visual controls.
	// Read these after wheelBehavior so Styles.ini remains authoritative.
	{
		if (!GetUInt32Value(ini, "Styling.HoverDelay", "InstantSpellColor", Config::Styling::HoverDelay::InstantSpellColor)) {
			Config::Styling::HoverDelay::InstantSpellColor = Config::Styling::HoverDelay::Color;
		}
		if (!GetUInt32Value(ini, "Styling.HoverDelay", "InstantSpellBackgroundColor", Config::Styling::HoverDelay::InstantSpellBackgroundColor)) {
			Config::Styling::HoverDelay::InstantSpellBackgroundColor = Config::Styling::HoverDelay::BackgroundColor;
		}
		if (!GetBoolValue(ini, "Styling.HoverDelay", "InstantSpellUseReskinAssets", Config::Styling::HoverDelay::InstantSpellUseReskinAssets)) {
			GetBoolValue(ini, "Styling.HoverDelay", "InstantSpellAssetsEnabled", Config::Styling::HoverDelay::InstantSpellUseReskinAssets);
		}
		GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAssetScale", Config::Styling::HoverDelay::InstantSpellAssetScale);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAssetOffsetX", Config::Styling::HoverDelay::InstantSpellAssetOffsetX);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAssetOffsetY", Config::Styling::HoverDelay::InstantSpellAssetOffsetY);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellAssetOpacity", Config::Styling::HoverDelay::InstantSpellAssetOpacity);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellHandIndicatorScale", Config::Styling::HoverDelay::InstantSpellHandIndicatorScale);
		LoadInstantSpellHandIndicatorOffsetsFromIni(
			ini,
			"Styling.HoverDelay",
			Config::Styling::HoverDelay::InstantSpellHandIndicatorOffsetX,
			Config::Styling::HoverDelay::InstantSpellHandIndicatorOffsetY,
			Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetX,
			Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetY,
			Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetX,
			Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetY,
			Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetX,
			Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetY);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantSpellHandIndicatorOpacity", Config::Styling::HoverDelay::InstantSpellHandIndicatorOpacity);
		if (!GetBoolValue(ini, "Styling.HoverDelay", "InstantSpellUseAtlasAnimation", Config::Styling::HoverDelay::InstantSpellUseAtlasAnimation)) {
			GetBoolValue(ini, "Styling.HoverDelay", "InstantSpellAtlasEnabled", Config::Styling::HoverDelay::InstantSpellUseAtlasAnimation);
		}
		Config::Styling::HoverDelay::InstantSpellAssetScale = std::clamp(Config::Styling::HoverDelay::InstantSpellAssetScale, 0.1f, 12.0f);
		Config::Styling::HoverDelay::InstantSpellAssetOffsetX = std::clamp(Config::Styling::HoverDelay::InstantSpellAssetOffsetX, -200.0f, 200.0f);
		Config::Styling::HoverDelay::InstantSpellAssetOffsetY = std::clamp(Config::Styling::HoverDelay::InstantSpellAssetOffsetY, -200.0f, 200.0f);
		Config::Styling::HoverDelay::InstantSpellAssetOpacity = std::clamp(Config::Styling::HoverDelay::InstantSpellAssetOpacity, 0.0f, 1.0f);
		Config::Styling::HoverDelay::InstantSpellHandIndicatorScale = std::clamp(Config::Styling::HoverDelay::InstantSpellHandIndicatorScale, 0.1f, 12.0f);
		ClampInstantSpellHandIndicatorOffsets(
			Config::Styling::HoverDelay::InstantSpellHandIndicatorOffsetX,
			Config::Styling::HoverDelay::InstantSpellHandIndicatorOffsetY,
			Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetX,
			Config::Styling::HoverDelay::InstantSpellHandIndicatorLeftOffsetY,
			Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetX,
			Config::Styling::HoverDelay::InstantSpellHandIndicatorRightOffsetY,
			Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetX,
			Config::Styling::HoverDelay::InstantSpellHandIndicatorBothOffsetY);
		Config::Styling::HoverDelay::InstantSpellHandIndicatorOpacity = std::clamp(Config::Styling::HoverDelay::InstantSpellHandIndicatorOpacity, 0.0f, 1.0f);
	}

	// dMenu Wheeler Styles owns shout stage reskin placement/toggle controls.
	{
		if (!GetBoolValue(ini, "Styling.HoverDelay", "InstantShoutAnimateReveal", Config::Styling::HoverDelay::InstantShoutAnimateReveal)) {
			GetBoolValue(ini, "Styling.HoverDelay", "InstantShoutUseRevealAnimation", Config::Styling::HoverDelay::InstantShoutAnimateReveal);
		}
		GetFloatValue(ini, "Styling.HoverDelay", "InstantShoutAssetScale", Config::Styling::HoverDelay::InstantShoutAssetScale);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantShoutAssetOffsetX", Config::Styling::HoverDelay::InstantShoutAssetOffsetX);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantShoutAssetOffsetY", Config::Styling::HoverDelay::InstantShoutAssetOffsetY);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantShoutStage1OffsetX", Config::Styling::HoverDelay::InstantShoutStage1OffsetX);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantShoutStage1OffsetY", Config::Styling::HoverDelay::InstantShoutStage1OffsetY);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantShoutStage1RotationDeg", Config::Styling::HoverDelay::InstantShoutStage1RotationDeg);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantShoutStage2OffsetX", Config::Styling::HoverDelay::InstantShoutStage2OffsetX);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantShoutStage2OffsetY", Config::Styling::HoverDelay::InstantShoutStage2OffsetY);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantShoutStage2RotationDeg", Config::Styling::HoverDelay::InstantShoutStage2RotationDeg);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantShoutStage3OffsetX", Config::Styling::HoverDelay::InstantShoutStage3OffsetX);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantShoutStage3OffsetY", Config::Styling::HoverDelay::InstantShoutStage3OffsetY);
		GetFloatValue(ini, "Styling.HoverDelay", "InstantShoutStage3RotationDeg", Config::Styling::HoverDelay::InstantShoutStage3RotationDeg);
		Config::Styling::HoverDelay::InstantShoutAssetScale = std::clamp(Config::Styling::HoverDelay::InstantShoutAssetScale, 0.1f, 12.0f);
		Config::Styling::HoverDelay::InstantShoutAssetOffsetX = std::clamp(Config::Styling::HoverDelay::InstantShoutAssetOffsetX, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantShoutAssetOffsetY = std::clamp(Config::Styling::HoverDelay::InstantShoutAssetOffsetY, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantShoutStage1OffsetX = std::clamp(Config::Styling::HoverDelay::InstantShoutStage1OffsetX, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantShoutStage1OffsetY = std::clamp(Config::Styling::HoverDelay::InstantShoutStage1OffsetY, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantShoutStage1RotationDeg = std::clamp(Config::Styling::HoverDelay::InstantShoutStage1RotationDeg, -360.0f, 360.0f);
		Config::Styling::HoverDelay::InstantShoutStage2OffsetX = std::clamp(Config::Styling::HoverDelay::InstantShoutStage2OffsetX, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantShoutStage2OffsetY = std::clamp(Config::Styling::HoverDelay::InstantShoutStage2OffsetY, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantShoutStage2RotationDeg = std::clamp(Config::Styling::HoverDelay::InstantShoutStage2RotationDeg, -360.0f, 360.0f);
		Config::Styling::HoverDelay::InstantShoutStage3OffsetX = std::clamp(Config::Styling::HoverDelay::InstantShoutStage3OffsetX, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantShoutStage3OffsetY = std::clamp(Config::Styling::HoverDelay::InstantShoutStage3OffsetY, -500.0f, 500.0f);
		Config::Styling::HoverDelay::InstantShoutStage3RotationDeg = std::clamp(Config::Styling::HoverDelay::InstantShoutStage3RotationDeg, -360.0f, 360.0f);
	}

	// dMenu Wheeler Styles owns the hand indicator visibility + offset controls.
	// Keep behavior INI values as fallback, then allow Styles.ini to override.
	GetBoolValue(ini, "MainWheel.Indicators", "ShowHandIndicator", Config::MainWheel::ShowHandIndicator);
	GetFloatValue(ini, "MainWheel.Indicators", "RightSideOffsetX", Config::MainWheel::HandIndicators::RightSideOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators", "RightSideOffsetY", Config::MainWheel::HandIndicators::RightSideOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators", "WheelRightSideOffsetX", Config::MainWheel::HandIndicators::RightSideOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators", "WheelRightSideOffsetY", Config::MainWheel::HandIndicators::RightSideOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "OffsetX", Config::MainWheel::HandIndicators::Left.OffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "OffsetY", Config::MainWheel::HandIndicators::Left.OffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SecondaryOffsetX", Config::MainWheel::HandIndicators::Left.SecondaryOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SecondaryOffsetY", Config::MainWheel::HandIndicators::Left.SecondaryOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SubSlotOffsetX", Config::MainWheel::HandIndicators::Left.SecondaryOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SubSlotOffsetY", Config::MainWheel::HandIndicators::Left.SecondaryOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "DualTopOffsetX", Config::MainWheel::HandIndicators::Left.DualTopOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "DualTopOffsetY", Config::MainWheel::HandIndicators::Left.DualTopOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "BothSelectedOffsetX", Config::MainWheel::HandIndicators::Left.DualTopOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "BothSelectedOffsetY", Config::MainWheel::HandIndicators::Left.DualTopOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SlotLeftOffsetX", Config::MainWheel::HandIndicators::Left.SlotLeftOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SlotLeftOffsetY", Config::MainWheel::HandIndicators::Left.SlotLeftOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "WheelLeftOffsetX", Config::MainWheel::HandIndicators::Left.SlotLeftOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "WheelLeftOffsetY", Config::MainWheel::HandIndicators::Left.SlotLeftOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SlotRightOffsetX", Config::MainWheel::HandIndicators::Left.SlotRightOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "SlotRightOffsetY", Config::MainWheel::HandIndicators::Left.SlotRightOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "WheelRightOffsetX", Config::MainWheel::HandIndicators::Left.SlotRightOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Left", "WheelRightOffsetY", Config::MainWheel::HandIndicators::Left.SlotRightOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "OffsetX", Config::MainWheel::HandIndicators::Right.OffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "OffsetY", Config::MainWheel::HandIndicators::Right.OffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SecondaryOffsetX", Config::MainWheel::HandIndicators::Right.SecondaryOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SecondaryOffsetY", Config::MainWheel::HandIndicators::Right.SecondaryOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SubSlotOffsetX", Config::MainWheel::HandIndicators::Right.SecondaryOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SubSlotOffsetY", Config::MainWheel::HandIndicators::Right.SecondaryOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "DualTopOffsetX", Config::MainWheel::HandIndicators::Right.DualTopOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "DualTopOffsetY", Config::MainWheel::HandIndicators::Right.DualTopOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "BothSelectedOffsetX", Config::MainWheel::HandIndicators::Right.DualTopOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "BothSelectedOffsetY", Config::MainWheel::HandIndicators::Right.DualTopOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SlotLeftOffsetX", Config::MainWheel::HandIndicators::Right.SlotLeftOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SlotLeftOffsetY", Config::MainWheel::HandIndicators::Right.SlotLeftOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "WheelLeftOffsetX", Config::MainWheel::HandIndicators::Right.SlotLeftOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "WheelLeftOffsetY", Config::MainWheel::HandIndicators::Right.SlotLeftOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SlotRightOffsetX", Config::MainWheel::HandIndicators::Right.SlotRightOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "SlotRightOffsetY", Config::MainWheel::HandIndicators::Right.SlotRightOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "WheelRightOffsetX", Config::MainWheel::HandIndicators::Right.SlotRightOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Right", "WheelRightOffsetY", Config::MainWheel::HandIndicators::Right.SlotRightOffsetY);
	if (!GetUInt32Value(ini, "MainWheel.Indicators.Dual", "Color", Config::MainWheel::HandIndicators::Dual.Color)) {
		Config::MainWheel::HandIndicators::Dual.Color = Config::Styling::Wheel::TextColor;
	}
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "Opacity", Config::MainWheel::HandIndicators::Dual.Opacity);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "SizeScale", Config::MainWheel::HandIndicators::Dual.SizeScale);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "Thickness", Config::MainWheel::HandIndicators::Dual.Thickness);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "OffsetX", Config::MainWheel::HandIndicators::Dual.OffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "OffsetY", Config::MainWheel::HandIndicators::Dual.OffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "BothSelectedOffsetX", Config::MainWheel::HandIndicators::Dual.OffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "BothSelectedOffsetY", Config::MainWheel::HandIndicators::Dual.OffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "RightSideOffsetX", Config::MainWheel::HandIndicators::DualRightSideOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "RightSideOffsetY", Config::MainWheel::HandIndicators::DualRightSideOffsetY);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "DualRightSideOffsetX", Config::MainWheel::HandIndicators::DualRightSideOffsetX);
	GetFloatValue(ini, "MainWheel.Indicators.Dual", "DualRightSideOffsetY", Config::MainWheel::HandIndicators::DualRightSideOffsetY);
	Config::MainWheel::HandIndicators::Left.OffsetX = std::clamp(Config::MainWheel::HandIndicators::Left.OffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.OffsetY = std::clamp(Config::MainWheel::HandIndicators::Left.OffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.SecondaryOffsetX = std::clamp(Config::MainWheel::HandIndicators::Left.SecondaryOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.SecondaryOffsetY = std::clamp(Config::MainWheel::HandIndicators::Left.SecondaryOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.DualTopOffsetX = std::clamp(Config::MainWheel::HandIndicators::Left.DualTopOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.DualTopOffsetY = std::clamp(Config::MainWheel::HandIndicators::Left.DualTopOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.SlotLeftOffsetX = std::clamp(Config::MainWheel::HandIndicators::Left.SlotLeftOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.SlotLeftOffsetY = std::clamp(Config::MainWheel::HandIndicators::Left.SlotLeftOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.SlotRightOffsetX = std::clamp(Config::MainWheel::HandIndicators::Left.SlotRightOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Left.SlotRightOffsetY = std::clamp(Config::MainWheel::HandIndicators::Left.SlotRightOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.OffsetX = std::clamp(Config::MainWheel::HandIndicators::Right.OffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.OffsetY = std::clamp(Config::MainWheel::HandIndicators::Right.OffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.SecondaryOffsetX = std::clamp(Config::MainWheel::HandIndicators::Right.SecondaryOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.SecondaryOffsetY = std::clamp(Config::MainWheel::HandIndicators::Right.SecondaryOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.DualTopOffsetX = std::clamp(Config::MainWheel::HandIndicators::Right.DualTopOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.DualTopOffsetY = std::clamp(Config::MainWheel::HandIndicators::Right.DualTopOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.SlotLeftOffsetX = std::clamp(Config::MainWheel::HandIndicators::Right.SlotLeftOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.SlotLeftOffsetY = std::clamp(Config::MainWheel::HandIndicators::Right.SlotLeftOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.SlotRightOffsetX = std::clamp(Config::MainWheel::HandIndicators::Right.SlotRightOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Right.SlotRightOffsetY = std::clamp(Config::MainWheel::HandIndicators::Right.SlotRightOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Dual.Opacity = std::clamp(Config::MainWheel::HandIndicators::Dual.Opacity, 0.0f, 1.0f);
	Config::MainWheel::HandIndicators::Dual.SizeScale = std::clamp(Config::MainWheel::HandIndicators::Dual.SizeScale, 0.5f, 2.0f);
	Config::MainWheel::HandIndicators::Dual.Thickness = std::clamp(Config::MainWheel::HandIndicators::Dual.Thickness, 0.0f, 3.0f);
	Config::MainWheel::HandIndicators::Dual.OffsetX = std::clamp(Config::MainWheel::HandIndicators::Dual.OffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::Dual.OffsetY = std::clamp(Config::MainWheel::HandIndicators::Dual.OffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::RightSideOffsetX = std::clamp(Config::MainWheel::HandIndicators::RightSideOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::RightSideOffsetY = std::clamp(Config::MainWheel::HandIndicators::RightSideOffsetY, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::DualRightSideOffsetX = std::clamp(Config::MainWheel::HandIndicators::DualRightSideOffsetX, -200.0f, 200.0f);
	Config::MainWheel::HandIndicators::DualRightSideOffsetY = std::clamp(Config::MainWheel::HandIndicators::DualRightSideOffsetY, -200.0f, 200.0f);
	ApplyMainWheelIndicatorAssetPathHardcoded();

	// Capture base values for resolution-aware scaling (display-space).
	CaptureScaleBaseValues();

	// Main wheel layout scaling overrides (non-reskin).
	LoadMainWheelLayoutIniOverrides(ini);

}

void Config::ReadControlConfig()
{
	EnsureUserIniBootstrapped(CONTROLDEFAULTS_PATH, CONTROLSETTINGS_PATH, "Controls");

	CSimpleIniA ini;
	ini.SetUnicode();
	bool defaultsLoaded = false;
	bool userLoaded = false;
	if (!LoadLayeredIni(CONTROLDEFAULTS_PATH, CONTROLSETTINGS_PATH, ini, &defaultsLoaded, &userLoaded)) {
		logger::warn(
			"Controls: Failed to load config from '{}' or '{}', using runtime defaults",
			CONTROLSETTINGS_PATH,
			CONTROLDEFAULTS_PATH);
	} else {
		logger::info(
			"Controls: Loading config (defaults={}, user={})",
			defaultsLoaded,
			userLoaded);
	}
	GetUInt32Value(ini, "InputBindings.GamePad", "nextWheel", Config::InputBindings::GamePad::nextWheel);
	GetUInt32Value(ini, "InputBindings.GamePad", "prevWheel", Config::InputBindings::GamePad::prevWheel);
	GetUInt32Value(ini, "InputBindings.GamePad", "toggleWheel", Config::InputBindings::GamePad::toggleWheel);
	GetUInt32Value(ini, "InputBindings.GamePad", "toggleEditHints", Config::InputBindings::GamePad::toggleEditHints);
	GetUInt32Value(ini, "InputBindings.GamePad", "toggleWheelModifier", Config::InputBindings::GamePad::toggleWheelModifier);
	GetUInt32Value(ini, "InputBindings.GamePad", "nextItem", Config::InputBindings::GamePad::nextItem);
	GetUInt32Value(ini, "InputBindings.GamePad", "prevItem", Config::InputBindings::GamePad::prevItem);
	GetUInt32Value(ini, "InputBindings.GamePad", "activatePrimary", Config::InputBindings::GamePad::activatePrimary);
	GetUInt32Value(ini, "InputBindings.GamePad", "activateSecondary", Config::InputBindings::GamePad::activateSecondary);
	GetUInt32Value(ini, "InputBindings.GamePad", "addWheel", Config::InputBindings::GamePad::addWheel);
	GetUInt32Value(ini, "InputBindings.GamePad", "addEmptyEntry", Config::InputBindings::GamePad::addEmptyEntry);
	GetUInt32Value(ini, "InputBindings.GamePad", "moveEntryForward", Config::InputBindings::GamePad::moveEntryForward);
	GetUInt32Value(ini, "InputBindings.GamePad", "moveEntryBack", Config::InputBindings::GamePad::moveEntryBack);
	GetUInt32Value(ini, "InputBindings.GamePad", "moveWheelForward", Config::InputBindings::GamePad::moveWheelForward);
	GetUInt32Value(ini, "InputBindings.GamePad", "moveWheelBack", Config::InputBindings::GamePad::moveWheelBack);
	GetUInt32Value(ini, "InputBindings.GamePad", "toggleWheelIfInInventory", Config::InputBindings::GamePad::toggleWheelIfInInventory);
	GetUInt32Value(ini, "InputBindings.GamePad", "toggleWheelIfInInventoryModifier", Config::InputBindings::GamePad::toggleWheelIfInInventoryModifier);
	GetUInt32Value(ini, "InputBindings.GamePad", "toggleWheelIfNotInInventory", Config::InputBindings::GamePad::toggleWheelIfNotInInventory);
	GetUInt32Value(ini, "InputBindings.GamePad", "toggleWheelIfNotInInventoryModifier", Config::InputBindings::GamePad::toggleWheelIfNotInInventoryModifier);
	GetUInt32Value(ini, "InputBindings.GamePad", "exitWheel", Config::InputBindings::GamePad::exitWheel);

	GetUInt32Value(ini, "InputBindings.MKB", "nextWheel", Config::InputBindings::MKB::nextWheel);
	GetUInt32Value(ini, "InputBindings.MKB", "prevWheel", Config::InputBindings::MKB::prevWheel);
	GetUInt32Value(ini, "InputBindings.MKB", "toggleWheel", Config::InputBindings::MKB::toggleWheel);
	GetUInt32Value(ini, "InputBindings.MKB", "toggleEditHints", Config::InputBindings::MKB::toggleEditHints);
	GetUInt32Value(ini, "InputBindings.MKB", "closeWheel", Config::InputBindings::MKB::closeWheel);
	GetUInt32Value(ini, "InputBindings.MKB", "closeWheelAlt", Config::InputBindings::MKB::closeWheelAlt);
	GetUInt32Value(ini, "InputBindings.MKB", "toggleWheelModifier", Config::InputBindings::MKB::toggleWheelModifier);
	GetUInt32Value(ini, "InputBindings.MKB", "nextItem", Config::InputBindings::MKB::nextItem);
	GetUInt32Value(ini, "InputBindings.MKB", "prevItem", Config::InputBindings::MKB::prevItem);
	GetUInt32Value(ini, "InputBindings.MKB", "activatePrimary", Config::InputBindings::MKB::activatePrimary);
	GetUInt32Value(ini, "InputBindings.MKB", "activateSecondary", Config::InputBindings::MKB::activateSecondary);
	GetUInt32Value(ini, "InputBindings.MKB", "addWheel", Config::InputBindings::MKB::addWheel);
	GetUInt32Value(ini, "InputBindings.MKB", "addEmptyEntry", Config::InputBindings::MKB::addEmptyEntry);
	GetUInt32Value(ini, "InputBindings.MKB", "moveEntryForward", Config::InputBindings::MKB::moveEntryForward);
	GetUInt32Value(ini, "InputBindings.MKB", "moveEntryBack", Config::InputBindings::MKB::moveEntryBack);
	GetUInt32Value(ini, "InputBindings.MKB", "moveWheelForward", Config::InputBindings::MKB::moveWheelForward);
	GetUInt32Value(ini, "InputBindings.MKB", "moveWheelBack", Config::InputBindings::MKB::moveWheelBack);

	GetFloatValue(ini, "Control.Wheel", "CursorRadiusPerEntry", Config::Control::Wheel::CursorRadiusPerEntry);
	GetBoolValue(ini, "Control.Wheel", "DoubleActivateDisable", Config::Control::Wheel::DoubleActivateDisable);
	GetFloatValue(ini, "Control.Wheel", "ToggleHoldThreshold", Config::Control::Wheel::ToggleHoldThreshold);
	GetBoolValue(ini, "Control.Wheel", "BlockGameInputInEditMode", Config::Control::Wheel::BlockGameInputInEditMode);
	GetBoolValue(ini, "Control.Wheel", "EnableOpenInFavoritesMenu", Config::Control::Wheel::EnableOpenInFavoritesMenu);
	GetBoolValue(ini, "Control.Wheel", "EnableEditModeInFavoritesMenu", Config::Control::Wheel::EnableEditModeInFavoritesMenu);
	GetBoolValue(ini, "Control.Wheel", "HideGameUIInEditMode", Config::Control::Wheel::HideGameUIInEditMode);

	// Hand indicator assets are intentionally fixed to the default icons folder.
	ApplyMainWheelIndicatorAssetPathHardcoded();
	ApplyInstantSpellIndicatorAssetPathHardcoded();
}

void Config::ReadActionHotkeysBridgeConfig()
{
	ResetActionHotkeysBridgeConfigToDefaults();

	CSimpleIniA ini;
	ini.SetUnicode();
	bool defaultsLoaded = false;
	bool userLoaded = false;
	if (!LoadLayeredIni(
			ACTIONHOTKEYSBRIDGE_DEFAULTS_PATH,
			ACTIONHOTKEYSBRIDGE_SETTINGS_PATH,
			ini,
			&defaultsLoaded,
			&userLoaded)) {
		logger::info(
			"ActionHotkeysBridge: Config files '{}' / '{}' missing or unreadable; keeping runtime defaults",
			ACTIONHOTKEYSBRIDGE_SETTINGS_PATH,
			ACTIONHOTKEYSBRIDGE_DEFAULTS_PATH);
	} else {
		logger::info(
			"ActionHotkeysBridge: Loading config (defaults={}, user={})",
			defaultsLoaded,
			userLoaded);
	}

	GetBoolValue(ini, "ActionHotkeysBridge", "Enabled", Config::ActionHotkeysBridge::Enabled);
	GetStringValue(ini, "ActionHotkeysBridge", "SourceIniPath", Config::ActionHotkeysBridge::SourceIniPath);
	GetStringValue(ini, "ActionHotkeysBridge", "SourceSlotsIniPath", Config::ActionHotkeysBridge::SourceSlotsIniPath);
	GetStringValue(ini, "ActionHotkeysBridge", "SourceIconsPath", Config::ActionHotkeysBridge::SourceIconsPath);
	GetBoolValue(ini, "ActionHotkeysBridge", "AutoInjection", Config::ActionHotkeysBridge::AutoInjection);
	GetUInt32Value(ini, "ActionHotkeysBridge", "ManualWheelCount", Config::ActionHotkeysBridge::ManualWheelCount);
	GetBoolValue(ini, "ActionHotkeysBridge", "AutoRefresh", Config::ActionHotkeysBridge::AutoRefresh);
	GetUInt32Value(ini, "ActionHotkeysBridge", "RefreshDebounceMs", Config::ActionHotkeysBridge::RefreshDebounceMs);
	GetUInt32Value(ini, "ActionHotkeysBridge", "DispatchCooldownMs", Config::ActionHotkeysBridge::DispatchCooldownMs);
	GetBoolValue(ini, "ActionHotkeysBridge", "CloseAssistEnabled", Config::ActionHotkeysBridge::CloseAssistEnabled);
	GetBoolValue(ini, "ActionHotkeysBridge", "CloseAssistUseEsc", Config::ActionHotkeysBridge::CloseAssistUseEsc);
	GetBoolValue(ini, "ActionHotkeysBridge", "CloseAssistUseGamepadB", Config::ActionHotkeysBridge::CloseAssistUseGamepadB);
	GetUInt32Value(ini, "ActionHotkeysBridge", "CloseAssistTimeoutMs", Config::ActionHotkeysBridge::CloseAssistTimeoutMs);
	GetBoolValue(
		ini,
		"ActionHotkeysBridge",
		"BlockConflictingWheelerHotkeys",
		Config::ActionHotkeysBridge::BlockConflictingWheelerHotkeys);
	GetBoolValue(
		ini,
		"ActionHotkeysBridge",
		"MirrorSecondaryActivate",
		Config::ActionHotkeysBridge::MirrorSecondaryActivate);
	GetBoolValue(
		ini,
		"ActionHotkeysBridge",
		"MirrorSpecialActivate",
		Config::ActionHotkeysBridge::MirrorSpecialActivate);
	GetBoolValue(ini, "ActionHotkeysBridge", "DebugLog", Config::ActionHotkeysBridge::DebugLog);

	GetUInt32Value(
		ini,
		"ActionHotkeysBridge.Bindings",
		"ResetLayout",
		Config::ActionHotkeysBridge::ResetLayout);
	GetUInt32Value(
		ini,
		"ActionHotkeysBridge.Bindings",
		"ResetLayoutModifier",
		Config::ActionHotkeysBridge::ResetLayoutModifier);
	GetUInt32Value(
		ini,
		"ActionHotkeysBridge.Bindings",
		"ReturnToPrevious",
		Config::ActionHotkeysBridge::ReturnToPrevious);
	GetUInt32Value(
		ini,
		"ActionHotkeysBridge.Bindings",
		"ReturnToPreviousModifier",
		Config::ActionHotkeysBridge::ReturnToPreviousModifier);
	GetUInt32Value(
		ini,
		"ActionHotkeysBridge.Bindings",
		"RefreshMirror",
		Config::ActionHotkeysBridge::RefreshMirror);
	GetUInt32Value(
		ini,
		"ActionHotkeysBridge.Bindings",
		"RefreshMirrorModifier",
		Config::ActionHotkeysBridge::RefreshMirrorModifier);

	for (std::size_t i = 0; i < Config::ActionHotkeysBridge::Wheels.size(); ++i) {
		const std::string section = BuildActionHotkeysBridgeWheelSection(i);
		auto& wheel = Config::ActionHotkeysBridge::Wheels[i];
		GetUInt32Value(ini, section.c_str(), "EntryCapacity", wheel.EntryCapacity);
		GetUInt32Value(ini, section.c_str(), "JumpKey", wheel.JumpKey);
		GetUInt32Value(ini, section.c_str(), "JumpKeyModifier", wheel.JumpKeyModifier);
	}

	Config::ActionHotkeysBridge::PersistedLayout.clear();
	{
		CSimpleIniA layoutIni;
		layoutIni.SetUnicode();
		if (layoutIni.LoadFile(ACTIONHOTKEYSBRIDGE_LAYOUT_PATH) >= 0) {
			CSimpleIniA::TNamesDepend sections;
			layoutIni.GetAllSections(sections);
			for (const auto& sectionEntry : sections) {
				const char* sectionName = sectionEntry.pItem;
				if (!sectionName) {
					continue;
				}
				const std::string_view sectionView(sectionName);
				constexpr std::string_view kLayoutPrefix = "ActionHotkeysBridge.Layout.";
				if (!sectionView.starts_with(kLayoutPrefix)) {
					continue;
				}

				std::uint32_t wheelNumber = 0;
				std::uint32_t entryIndex = 0;
				if (!GetUInt32Value(layoutIni, sectionName, "Wheel", wheelNumber) ||
					!GetUInt32Value(layoutIni, sectionName, "Entry", entryIndex)) {
					continue;
				}

				const std::string slotId(sectionView.substr(kLayoutPrefix.size()));
				if (slotId.empty()) {
					continue;
				}

				Config::ActionHotkeysBridge::PersistedLayout[slotId] = Config::ActionHotkeysBridgeSlotPlacement{
					wheelNumber,
					entryIndex
				};
			}
		}
	}

	auto clampBridgeConfig = [] {
		Config::ActionHotkeysBridge::ManualWheelCount =
			std::clamp(
				Config::ActionHotkeysBridge::ManualWheelCount,
				1u,
				static_cast<std::uint32_t>(Config::kActionHotkeysBridgeMaxWheels));
		Config::ActionHotkeysBridge::RefreshDebounceMs =
			std::clamp(Config::ActionHotkeysBridge::RefreshDebounceMs, 0u, 10000u);
		Config::ActionHotkeysBridge::DispatchCooldownMs =
			std::clamp(Config::ActionHotkeysBridge::DispatchCooldownMs, 50u, 2000u);
		Config::ActionHotkeysBridge::CloseAssistTimeoutMs =
			std::clamp(Config::ActionHotkeysBridge::CloseAssistTimeoutMs, 250u, 30000u);
		for (auto& wheel : Config::ActionHotkeysBridge::Wheels) {
			wheel.EntryCapacity = std::clamp(wheel.EntryCapacity, 1u, 64u);
		}
		for (auto it = Config::ActionHotkeysBridge::PersistedLayout.begin();
		     it != Config::ActionHotkeysBridge::PersistedLayout.end();) {
			const bool invalidWheel =
				it->second.wheelNumber == 0 ||
				it->second.wheelNumber > Config::kActionHotkeysBridgeMaxWheels;
			if (invalidWheel) {
				it = Config::ActionHotkeysBridge::PersistedLayout.erase(it);
				continue;
			}

			const std::size_t wheelIndex = static_cast<std::size_t>(it->second.wheelNumber - 1);
			if (it->second.entryIndex >= Config::ActionHotkeysBridge::Wheels[wheelIndex].EntryCapacity) {
				it = Config::ActionHotkeysBridge::PersistedLayout.erase(it);
				continue;
			}

			++it;
		}
	};

	clampBridgeConfig();
}

void Config::ReadOStimIntegrationConfig()
{
	ResetOStimIntegrationConfigToDefaults();

	CSimpleIniA ini;
	ini.SetUnicode();
	bool defaultsLoaded = false;
	bool userLoaded = false;
	if (!LoadLayeredIni(
			OSTIMINTEGRATION_DEFAULTS_PATH,
			OSTIMINTEGRATION_SETTINGS_PATH,
			ini,
			&defaultsLoaded,
			&userLoaded)) {
		logger::info(
			"OStimIntegration: Config files '{}' / '{}' missing or unreadable; keeping runtime defaults",
			OSTIMINTEGRATION_SETTINGS_PATH,
			OSTIMINTEGRATION_DEFAULTS_PATH);
	} else {
		logger::info(
			"OStimIntegration: Loading config (defaults={}, user={})",
			defaultsLoaded,
			userLoaded);
	}

	GetBoolValue(ini, "OStimIntegration", "Enabled", Config::OStimIntegration::Enabled);
	GetBoolValue(ini, "OStimIntegration", "AutoDetect", Config::OStimIntegration::AutoDetect);
	GetBoolValue(ini, "OStimIntegration", "CreateManagedWheel", Config::OStimIntegration::CreateManagedWheel);
	GetBoolValue(ini, "OStimIntegration", "AutoSwitchToSceneWheel", Config::OStimIntegration::AutoSwitchToSceneWheel);
	GetBoolValue(
		ini,
		"OStimIntegration",
		"RestorePreviousWheelOnSceneEnd",
		Config::OStimIntegration::RestorePreviousWheelOnSceneEnd);
	GetBoolValue(
		ini,
		"OStimIntegration",
		"AllowPositionBrowsing",
		Config::OStimIntegration::AllowPositionBrowsing);
	GetBoolValue(
		ini,
		"OStimIntegration",
		"ShowOnlyValidPositions",
		Config::OStimIntegration::ShowOnlyValidPositions);
	GetBoolValue(
		ini,
		"OStimIntegration",
		"ShowPositionNames",
		Config::OStimIntegration::ShowPositionNames);
	GetBoolValue(
		ini,
		"OStimIntegration",
		"ShowPositionPreviews",
		Config::OStimIntegration::ShowPositionPreviews);
	GetBoolValue(
		ini,
		"OStimIntegration",
		"RestrictRegularWheelActionsDuringScenes",
		Config::OStimIntegration::RestrictRegularWheelActionsDuringScenes);
	GetBoolValue(
		ini,
		"OStimIntegration",
		"HideInvalidActions",
		Config::OStimIntegration::HideInvalidActions);
	GetBoolValue(
		ini,
		"OStimIntegration",
		"PreferMetadataPreviews",
		Config::OStimIntegration::PreferMetadataPreviews);
	GetBoolValue(
		ini,
		"OStimIntegration",
		"UseResourcePreviewFallback",
		Config::OStimIntegration::UseResourcePreviewFallback);
	GetBoolValue(
		ini,
		"OStimIntegration",
		"PreferCurrentAnimationClass",
		Config::OStimIntegration::PreferCurrentAnimationClass);
	GetBoolValue(ini, "OStimIntegration", "DebugLog", Config::OStimIntegration::DebugLog);
	GetUInt32Value(
		ini,
		"OStimIntegration",
		"MaxPositionsPerPage",
		Config::OStimIntegration::MaxPositionsPerPage);
	GetFloatValue(ini, "OStimIntegration", "SVGSlotScale", Config::OStimIntegration::SVGSlotScale);
	GetFloatValue(ini, "OStimIntegration", "SVGSlotOffsetX", Config::OStimIntegration::SVGSlotOffsetX);
	GetFloatValue(ini, "OStimIntegration", "SVGSlotOffsetY", Config::OStimIntegration::SVGSlotOffsetY);
	GetFloatValue(ini, "OStimIntegration", "SVGCenterScale", Config::OStimIntegration::SVGCenterScale);
	GetFloatValue(ini, "OStimIntegration", "SVGCenterOffsetX", Config::OStimIntegration::SVGCenterOffsetX);
	GetFloatValue(ini, "OStimIntegration", "SVGCenterOffsetY", Config::OStimIntegration::SVGCenterOffsetY);
	GetFloatValue(ini, "OStimIntegration", "DDSSlotScale", Config::OStimIntegration::DDSSlotScale);
	GetFloatValue(ini, "OStimIntegration", "DDSSlotOffsetX", Config::OStimIntegration::DDSSlotOffsetX);
	GetFloatValue(ini, "OStimIntegration", "DDSSlotOffsetY", Config::OStimIntegration::DDSSlotOffsetY);
	GetFloatValue(ini, "OStimIntegration", "DDSCenterScale", Config::OStimIntegration::DDSCenterScale);
	GetFloatValue(ini, "OStimIntegration", "DDSCenterOffsetX", Config::OStimIntegration::DDSCenterOffsetX);
	GetFloatValue(ini, "OStimIntegration", "DDSCenterOffsetY", Config::OStimIntegration::DDSCenterOffsetY);

	Config::OStimIntegration::MaxPositionsPerPage =
		std::clamp(Config::OStimIntegration::MaxPositionsPerPage, 4u, 10u);
	Config::OStimIntegration::SVGSlotScale = std::clamp(Config::OStimIntegration::SVGSlotScale, 0.1f, 4.0f);
	Config::OStimIntegration::SVGSlotOffsetX = std::clamp(Config::OStimIntegration::SVGSlotOffsetX, -500.0f, 500.0f);
	Config::OStimIntegration::SVGSlotOffsetY = std::clamp(Config::OStimIntegration::SVGSlotOffsetY, -500.0f, 500.0f);
	Config::OStimIntegration::SVGCenterScale = std::clamp(Config::OStimIntegration::SVGCenterScale, 0.1f, 4.0f);
	Config::OStimIntegration::SVGCenterOffsetX = std::clamp(Config::OStimIntegration::SVGCenterOffsetX, -500.0f, 500.0f);
	Config::OStimIntegration::SVGCenterOffsetY = std::clamp(Config::OStimIntegration::SVGCenterOffsetY, -500.0f, 500.0f);
	Config::OStimIntegration::DDSSlotScale = std::clamp(Config::OStimIntegration::DDSSlotScale, 0.1f, 4.0f);
	Config::OStimIntegration::DDSSlotOffsetX = std::clamp(Config::OStimIntegration::DDSSlotOffsetX, -500.0f, 500.0f);
	Config::OStimIntegration::DDSSlotOffsetY = std::clamp(Config::OStimIntegration::DDSSlotOffsetY, -500.0f, 500.0f);
	Config::OStimIntegration::DDSCenterScale = std::clamp(Config::OStimIntegration::DDSCenterScale, 0.1f, 4.0f);
	Config::OStimIntegration::DDSCenterOffsetX = std::clamp(Config::OStimIntegration::DDSCenterOffsetX, -500.0f, 500.0f);
	Config::OStimIntegration::DDSCenterOffsetY = std::clamp(Config::OStimIntegration::DDSCenterOffsetY, -500.0f, 500.0f);
}

bool Config::WriteAmmoWheelKeybindOverrides()
{
	CSimpleIniA ini;
	ini.SetUnicode();
	SI_Error rc = ini.LoadFile(AMMOWHEELSETTINGS_PATH);
	if (rc < 0) {
		logger::warn("AmmoWheel: Failed to load '{}', writing new file with keybind overrides", AMMOWHEELSETTINGS_PATH);
	}
	// Toggle keys (existing)
	ini.SetLongValue("Input", "ToggleKeyMKB", Config::AmmoWheel::MKB::toggleAmmoWheel);
	ini.SetLongValue("Input", "ToggleKeyGamepad", Config::AmmoWheel::GamePad::toggleAmmoWheel);
	ini.SetValue("Input", "ToggleKeyMKBName", Config::AmmoWheel::ToggleKeyMKBName.c_str());
	ini.SetValue("Input", "ToggleKeyGamepadName", Config::AmmoWheel::ToggleKeyGamepadName.c_str());
	// Modifier keys (new)
	ini.SetLongValue("Input", "ModifierKeyMKB", Config::AmmoWheel::MKB::modifierKey);
	ini.SetLongValue("Input", "ModifierButtonGamepad", Config::AmmoWheel::GamePad::modifierButton);
	ini.SetValue("Input", "ModifierKeyMKBName", Config::AmmoWheel::ModifierKeyMKBName.c_str());
	ini.SetValue("Input", "ModifierButtonGamepadName", Config::AmmoWheel::ModifierButtonGamepadName.c_str());
	// Mouse toggle (new)
	ini.SetLongValue("Input", "ToggleMouseButton", Config::AmmoWheel::MKB::toggleAmmoWheelMouse);
	ini.SetValue("Input", "ToggleMouseButtonName", Config::AmmoWheel::ToggleMouseButtonName.c_str());
	return ini.SaveFile(AMMOWHEELSETTINGS_PATH) >= 0;
}

bool Config::WriteActionHotkeysBridgeLayout()
{
	return WriteActionHotkeysBridgeLayoutConfig();
}

bool Config::AmmoWheel::RestoreFactoryDefaults()
{
	namespace fs = std::filesystem;
	std::error_code ec;
	
	// 1. Check if defaults file exists
	if (!fs::exists(AMMOWHEEL_DEFAULTS_PATH, ec) || ec) {
		logger::error("[AmmoWheel] Defaults file not found: '{}'", AMMOWHEEL_DEFAULTS_PATH);
		return false;
	}
	
	// 2. Backup current INI if it exists
	if (fs::exists(AMMOWHEELSETTINGS_PATH, ec) && !ec) {
		auto now = std::chrono::system_clock::now();
		auto tt = std::chrono::system_clock::to_time_t(now);
		std::tm tm{};
		localtime_s(&tm, &tt);
		char buf[32];
		std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
		std::string backupPath = std::string(AMMOWHEELSETTINGS_PATH) + ".bak." + buf;
		
		logger::info("[AmmoWheel] Creating backup: {}", backupPath);
		ec.clear();
		if (!fs::copy_file(AMMOWHEELSETTINGS_PATH, backupPath, 
				fs::copy_options::overwrite_existing, ec)) {
			logger::warn("[AmmoWheel] Backup failed (continuing): {}", ec.message());
		}
	}
	
	// 3. Copy defaults file to active INI location
	ec.clear();
	if (!fs::copy_file(AMMOWHEEL_DEFAULTS_PATH, AMMOWHEELSETTINGS_PATH, 
			fs::copy_options::overwrite_existing, ec)) {
		logger::error("[AmmoWheel] Failed to copy defaults: {}", ec.message());
		return false;
	}
	
	logger::info("[AmmoWheel] Factory defaults restored from '{}'", AMMOWHEEL_DEFAULTS_PATH);
	return true;
}

// ========== AMMOWHEEL STYLES.INI LOADER ==========
// Loads the data-driven skin config from Styles.ini
static void LoadAmmoWheelStylesIni()
{
	using namespace Config::AmmoWheel::Skin;
	
	std::string stylesPath = StylesIniPath;
	
	// Check if file exists
	std::error_code ec;
	if (!std::filesystem::exists(stylesPath, ec)) {
		logger::warn("AmmoWheel Styles.ini not found at '{}', using code defaults", stylesPath);
		StylesLoaded = false;
		return;
	}
	
	CSimpleIniA styleIni;
	styleIni.SetUnicode();
	SI_Error rc = styleIni.LoadFile(stylesPath.c_str());
	if (rc < 0) {
		logger::error("AmmoWheel: Failed to parse Styles.ini at '{}', using code defaults", stylesPath);
		StylesLoaded = false;
		return;
	}
	
	logger::info("AmmoWheel: Loading skin from '{}'", stylesPath);
	
	// Helper lambda for hex color parsing (0xAARRGGBB format)
	auto GetHexColor = [&styleIni](const char* section, const char* key, ImU32& outColor) {
		const char* val = styleIni.GetValue(section, key);
		if (val && std::strlen(val) > 0) {
			try {
				uint32_t parsed = static_cast<uint32_t>(std::stoul(val, nullptr, 0));
				outColor = parsed;
			} catch (...) {
				// Keep default
			}
		}
	};
	
	// [AmmoWheel.Slot]
	GetFloatValue(styleIni, "AmmoWheel.Slot", "SlotAngularPaddingDeg", SlotAngularPaddingDeg);
	GetFloatValue(styleIni, "AmmoWheel.Slot", "SlotInnerRadiusPadding", SlotInnerRadiusPadding);
	GetFloatValue(styleIni, "AmmoWheel.Slot", "SlotOuterRadiusPadding", SlotOuterRadiusPadding);
	GetFloatValue(styleIni, "AmmoWheel.Slot", "SlotCornerRounding", SlotCornerRounding);
	GetFloatValue(styleIni, "AmmoWheel.Slot", "BackgroundOpacity", BackgroundOpacity);
	BackgroundOpacity = std::clamp(BackgroundOpacity, 0.0f, 1.0f);
	
	GetHexColor("AmmoWheel.Slot", "UnhoveredColorBegin", UnhoveredColorBegin);
	GetHexColor("AmmoWheel.Slot", "UnhoveredColorEnd", UnhoveredColorEnd);
	GetHexColor("AmmoWheel.Slot", "HoveredColorBegin", HoveredColorBegin);
	GetHexColor("AmmoWheel.Slot", "HoveredColorEnd", HoveredColorEnd);
	GetHexColor("AmmoWheel.Slot", "SelectedColorBegin", SelectedColorBegin);
	GetHexColor("AmmoWheel.Slot", "SelectedColorEnd", SelectedColorEnd);
	
	// [AmmoWheel.Text]
	GetHexColor("AmmoWheel.Text", "TextColor", TextColor);
	GetHexColor("AmmoWheel.Text", "TextShadowColor", TextShadowColor);
	GetFloatValue(styleIni, "AmmoWheel.Text", "TextSize", TextSize);
	GetFloatValue(styleIni, "AmmoWheel.Text", "TextShadowOffsetX", TextShadowOffsetX);
	GetFloatValue(styleIni, "AmmoWheel.Text", "TextShadowOffsetY", TextShadowOffsetY);
	{
		uint32_t wm = static_cast<uint32_t>(TextWrapMode);
		GetUInt32Value(styleIni, "AmmoWheel.Text", "WrapMode", wm);
		TextWrapMode = std::clamp(static_cast<int>(wm), 0, 2);
	}
	{
		uint32_t ml = static_cast<uint32_t>(TextMaxLines);
		GetUInt32Value(styleIni, "AmmoWheel.Text", "MaxLines", ml);
		TextMaxLines = std::clamp(static_cast<int>(ml), 1, 10);
	}
	
	// [AmmoWheel.Icon]
	GetBoolValue(styleIni, "AmmoWheel.Icon", "IconsEnabled", IconsEnabled);
	{
		uint32_t pm = static_cast<uint32_t>(IconPlacementMode);
		GetUInt32Value(styleIni, "AmmoWheel.Icon", "PlacementMode", pm);
		IconPlacementMode = std::clamp(static_cast<int>(pm), 0, 2);
	}
	GetFloatValue(styleIni, "AmmoWheel.Icon", "RadialOffset", IconRadialOffset);
	IconRadialOffset = std::clamp(IconRadialOffset, 0.0f, 1.0f);
	GetFloatValue(styleIni, "AmmoWheel.Icon", "PaddingPixels", IconPaddingPixels);
	IconPaddingPixels = std::clamp(IconPaddingPixels, 0.0f, 50.0f);
	{
		uint32_t rm = static_cast<uint32_t>(IconRotationMode);
		GetUInt32Value(styleIni, "AmmoWheel.Icon", "RotationMode", rm);
		IconRotationMode = std::clamp(static_cast<int>(rm), 0, 2);
	}
	GetFloatValue(styleIni, "AmmoWheel.Icon", "RotationOffsetDeg", IconRotationOffsetDeg);
	GetFloatValue(styleIni, "AmmoWheel.Icon", "FixedAngleDeg", IconFixedAngleDeg);
	GetFloatValue(styleIni, "AmmoWheel.Icon", "RotationSafetyScale", IconRotationSafetyScale);
	IconRotationSafetyScale = std::clamp(IconRotationSafetyScale, 0.1f, 1.0f);
	GetBoolValue(styleIni, "AmmoWheel.Icon", "ClampInsideSlot", IconClampInsideSlot);
	{
		uint32_t cm = static_cast<uint32_t>(IconClipMode);
		GetUInt32Value(styleIni, "AmmoWheel.Icon", "ClipMode", cm);
		IconClipMode = std::clamp(static_cast<int>(cm), 0, 2);
	}
	GetHexColor("AmmoWheel.Icon", "TintColor", IconTintColor);
	
	// [AmmoWheel.Indicator.Selected]
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Selected", "Enabled", SelectedEnabled);
	{
		uint32_t sh = static_cast<uint32_t>(SelectedShape);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Selected", "Shape", sh);
		SelectedShape = std::clamp(static_cast<int>(sh), 0, 3);
	}
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Selected", "ThicknessPixels", SelectedThicknessPx);
	SelectedThicknessPx = std::clamp(SelectedThicknessPx, 0.0f, 50.0f);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Selected", "RadiusOffsetPixels", SelectedRadiusOffsetPx);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Selected", "StartAngleOffsetDeg", SelectedStartAngleOffsetDeg);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Selected", "SweepDeg", SelectedSweepDeg);
	GetHexColor("AmmoWheel.Indicator.Selected", "ColorBegin", SelectedColorBeginInd);
	GetHexColor("AmmoWheel.Indicator.Selected", "ColorEnd", SelectedColorEndInd);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Selected", "Alpha", SelectedAlpha);
	SelectedAlpha = std::clamp(SelectedAlpha, 0.0f, 1.0f);
	{
		uint32_t cs = static_cast<uint32_t>(SelectedCapStyle);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Selected", "CapStyle", cs);
		SelectedCapStyle = std::clamp(static_cast<int>(cs), 0, 1);
	}
	{
		uint32_t am = static_cast<uint32_t>(SelectedAnimMode);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Selected", "AnimationMode", am);
		SelectedAnimMode = std::clamp(static_cast<int>(am), 0, 2);
	}
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Selected", "AnimationSpeed", SelectedAnimSpeed);
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Selected", "ShowWhenSelected", SelectedShowWhenSelected);
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Selected", "ShowWhenHovered", SelectedShowWhenHovered);
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Selected", "HideIfAnotherStateActive", SelectedHideIfAnotherStateActive);
	{
		uint32_t pr = static_cast<uint32_t>(SelectedPriority);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Selected", "Priority", pr);
		SelectedPriority = static_cast<int>(pr);
	}
	
	// [AmmoWheel.Indicator.Hovered]
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Hovered", "Enabled", HoveredEnabled);
	{
		uint32_t sh = static_cast<uint32_t>(HoveredShape);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Hovered", "Shape", sh);
		HoveredShape = std::clamp(static_cast<int>(sh), 0, 3);
	}
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Hovered", "ThicknessPixels", HoveredThicknessPx);
	HoveredThicknessPx = std::clamp(HoveredThicknessPx, 0.0f, 50.0f);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Hovered", "RadiusOffsetPixels", HoveredRadiusOffsetPx);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Hovered", "StartAngleOffsetDeg", HoveredStartAngleOffsetDeg);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Hovered", "SweepDeg", HoveredSweepDeg);
	GetHexColor("AmmoWheel.Indicator.Hovered", "ColorBegin", HoveredColorBeginInd);
	GetHexColor("AmmoWheel.Indicator.Hovered", "ColorEnd", HoveredColorEndInd);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Hovered", "Alpha", HoveredAlpha);
	HoveredAlpha = std::clamp(HoveredAlpha, 0.0f, 1.0f);
	{
		uint32_t cs = static_cast<uint32_t>(HoveredCapStyle);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Hovered", "CapStyle", cs);
		HoveredCapStyle = std::clamp(static_cast<int>(cs), 0, 1);
	}
	{
		uint32_t am = static_cast<uint32_t>(HoveredAnimMode);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Hovered", "AnimationMode", am);
		HoveredAnimMode = std::clamp(static_cast<int>(am), 0, 2);
	}
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Hovered", "AnimationSpeed", HoveredAnimSpeed);
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Hovered", "ShowWhenHovered", HoveredShowWhenHovered);
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Hovered", "ShowWhenSelected", HoveredShowWhenSelected);
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Hovered", "HideIfAnotherStateActive", HoveredHideIfAnotherStateActive);
	{
		uint32_t pr = static_cast<uint32_t>(HoveredPriority);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Hovered", "Priority", pr);
		HoveredPriority = static_cast<int>(pr);
	}
	
	// [AmmoWheel.Indicator.Active]
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Active", "Enabled", ActiveEnabled);
	{
		uint32_t sh = static_cast<uint32_t>(ActiveShape);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Active", "Shape", sh);
		ActiveShape = std::clamp(static_cast<int>(sh), 0, 3);
	}
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Active", "ThicknessPixels", ActiveThicknessPx);
	ActiveThicknessPx = std::clamp(ActiveThicknessPx, 0.0f, 50.0f);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Active", "RadiusOffsetPixels", ActiveRadiusOffsetPx);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Active", "StartAngleOffsetDeg", ActiveStartAngleOffsetDeg);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Active", "SweepDeg", ActiveSweepDeg);
	GetHexColor("AmmoWheel.Indicator.Active", "ColorBegin", ActiveColorBeginInd);
	GetHexColor("AmmoWheel.Indicator.Active", "ColorEnd", ActiveColorEndInd);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Active", "Alpha", ActiveAlpha);
	ActiveAlpha = std::clamp(ActiveAlpha, 0.0f, 1.0f);
	{
		uint32_t cs = static_cast<uint32_t>(ActiveCapStyle);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Active", "CapStyle", cs);
		ActiveCapStyle = std::clamp(static_cast<int>(cs), 0, 1);
	}
	{
		uint32_t am = static_cast<uint32_t>(ActiveAnimMode);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Active", "AnimationMode", am);
		ActiveAnimMode = std::clamp(static_cast<int>(am), 0, 2);
	}
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Active", "AnimationSpeed", ActiveAnimSpeed);
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Active", "ShowWhenActive", ActiveShowWhenActive);
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Active", "ShowWhenSelected", ActiveShowWhenSelected);
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Active", "HideIfAnotherStateActive", ActiveHideIfAnotherStateActive);
	{
		uint32_t pr = static_cast<uint32_t>(ActivePriority);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Active", "Priority", pr);
		ActivePriority = static_cast<int>(pr);
	}
	
	// [AmmoWheel.Indicator.Charge]
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Charge", "Enabled", ChargeEnabled);
	{
		uint32_t sh = static_cast<uint32_t>(ChargeShape);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Charge", "Shape", sh);
		ChargeShape = std::clamp(static_cast<int>(sh), 0, 3);
	}
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Charge", "ThicknessPixels", ChargeThicknessPx);
	ChargeThicknessPx = std::clamp(ChargeThicknessPx, 0.0f, 50.0f);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Charge", "RadiusOffsetPixels", ChargeRadiusOffsetPx);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Charge", "StartAngleOffsetDeg", ChargeStartAngleOffsetDeg);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Charge", "SweepDeg", ChargeSweepDeg);
	GetHexColor("AmmoWheel.Indicator.Charge", "ColorBegin", ChargeColorBeginInd);
	GetHexColor("AmmoWheel.Indicator.Charge", "ColorEnd", ChargeColorEndInd);
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Charge", "Alpha", ChargeAlpha);
	ChargeAlpha = std::clamp(ChargeAlpha, 0.0f, 1.0f);
	{
		uint32_t cs = static_cast<uint32_t>(ChargeCapStyle);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Charge", "CapStyle", cs);
		ChargeCapStyle = std::clamp(static_cast<int>(cs), 0, 1);
	}
	{
		uint32_t am = static_cast<uint32_t>(ChargeAnimMode);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Charge", "AnimationMode", am);
		ChargeAnimMode = std::clamp(static_cast<int>(am), 0, 2);
	}
	GetFloatValue(styleIni, "AmmoWheel.Indicator.Charge", "AnimationSpeed", ChargeAnimSpeed);
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Charge", "ShowWhenCharging", ChargeShowWhenCharging);
	GetBoolValue(styleIni, "AmmoWheel.Indicator.Charge", "HideIfAnotherStateActive", ChargeHideIfAnotherStateActive);
	{
		uint32_t pr = static_cast<uint32_t>(ChargePriority);
		GetUInt32Value(styleIni, "AmmoWheel.Indicator.Charge", "Priority", pr);
		ChargePriority = static_cast<int>(pr);
	}
	
	StylesLoaded = true;
	logger::info("AmmoWheel: Skin loaded successfully from '{}'", stylesPath);
}

void Config::ReadAmmoWheelConfig()
{
	EnsureUserIniBootstrapped(AMMOWHEEL_DEFAULTS_PATH, AMMOWHEELSETTINGS_PATH, "AmmoWheel");

	CSimpleIniA ini;
	ini.SetUnicode();
	bool defaultsLoaded = false;
	bool userLoaded = false;
	if (!LoadLayeredIni(AMMOWHEEL_DEFAULTS_PATH, AMMOWHEELSETTINGS_PATH, ini, &defaultsLoaded, &userLoaded)) {
		logger::warn(
			"AmmoWheel: Failed to load config from '{}' or '{}', using runtime defaults",
			AMMOWHEELSETTINGS_PATH,
			AMMOWHEEL_DEFAULTS_PATH);
		return;
	}

	logger::info(
		"AmmoWheel: Loading config (defaults={}, user={})",
		defaultsLoaded,
		userLoaded);
	
	// DEBUG: Log raw INI values for Popup.Animation section
	const char* rawEnabled = ini.GetValue("Popup.Animation", "Enabled", "NOT_FOUND");
	const char* rawHoverIn = ini.GetValue("Popup.Animation", "HoverInMs", "NOT_FOUND");
	const char* rawScaleFrom = ini.GetValue("Popup.Animation", "ScaleFrom", "NOT_FOUND");
	logger::info("  [RAW INI] Popup.Animation: Enabled='{}', HoverInMs='{}', ScaleFrom='{}'", 
		rawEnabled, rawHoverIn, rawScaleFrom);

	// Packed ImU32 color keys (0xAABBGGRR) override legacy R/G/B/A entries.
	auto unpackColor = [](uint32_t packed, uint32_t& r, uint32_t& g, uint32_t& b, uint32_t& a) {
		r = packed & 0xFFu;
		g = (packed >> 8) & 0xFFu;
		b = (packed >> 16) & 0xFFu;
		a = (packed >> 24) & 0xFFu;
	};

	auto readPackedColor = [&](const char* section, const char* key,
		uint32_t& r, uint32_t& g, uint32_t& b, uint32_t& a) {
		uint32_t packed = 0;
		if (!GetUInt32Value(ini, section, key, packed)) {
			return false;
		}
		unpackColor(packed, r, g, b, a);
		return true;
	};

	// [General]
	GetBoolValue(ini, "General", "Enabled", Config::AmmoWheel::Enabled);
	GetBoolValue(ini, "General", "RequireWeaponEquipped", Config::AmmoWheel::RequireWeaponEquipped);

	// [Position]
	GetUInt32Value(ini, "Position", "ScreenAnchor", Config::AmmoWheel::ScreenAnchorIndex);
	GetFloatValue(ini, "Position", "PositionX", Config::AmmoWheel::PositionX);
	GetFloatValue(ini, "Position", "PositionY", Config::AmmoWheel::PositionY);
	GetFloatValue(ini, "Position", "WheelRadius", Config::AmmoWheel::WheelRadius);

	// [Appearance]
	GetUInt32Value(ini, "Appearance", "WheelShape", Config::AmmoWheel::WheelShapeIndex);
	GetFloatValue(ini, "Appearance", "ArcStartAngle", Config::AmmoWheel::ArcStartAngle);
	GetBoolValue(ini, "Appearance", "UseMainWheelTheme", Config::AmmoWheel::UseMainWheelTheme);
	GetFloatValue(ini, "Appearance", "CustomOpacity", Config::AmmoWheel::CustomOpacity);
	
	// Slot Shape settings
	{
		uint32_t slotShapeVal = static_cast<uint32_t>(Config::AmmoWheel::SlotShape);
		logger::info("[Config] SlotShape BEFORE read: {}", Config::AmmoWheel::SlotShape);
		if (GetUInt32Value(ini, "Appearance", "SlotShape", slotShapeVal)) {
			Config::AmmoWheel::SlotShape = std::clamp(static_cast<int>(slotShapeVal), 0, 3);
			logger::info("[Config] SlotShape read from INI: {} -> clamped to {}", slotShapeVal, Config::AmmoWheel::SlotShape);
		} else {
			logger::warn("[Config] SlotShape NOT found in INI, keeping default: {}", Config::AmmoWheel::SlotShape);
		}
	}
	GetFloatValue(ini, "Appearance", "SlotCornerRadius", Config::AmmoWheel::SlotCornerRadius);
	GetFloatValue(ini, "Appearance", "SlotShapeScale", Config::AmmoWheel::SlotShapeScale);
	logger::info("[Config] SlotShapeScale={}, SlotCornerRadius={}", Config::AmmoWheel::SlotShapeScale, Config::AmmoWheel::SlotCornerRadius);

	// [Behavior]
	GetBoolValue(ini, "Behavior", "CloseOnSelection", Config::AmmoWheel::CloseOnSelection);
	GetBoolValue(ini, "Behavior", "UseRTUSystem", Config::AmmoWheel::UseRTUSystem);
	GetFloatValue(ini, "Behavior", "RTUHoverDelay", Config::AmmoWheel::RTUHoverDelay);
	
	// [TimeSlow]
	GetBoolValue(ini, "TimeSlow", "Enabled", Config::AmmoWheel::TimeSlowEnabled);
	GetFloatValue(ini, "TimeSlow", "SlowTimeScale", Config::AmmoWheel::TimeSlowScale);

	// [Performance] - Quality/performance tiers (optional, no-op when Tier=0)
	{
		uint32_t tierVal = static_cast<uint32_t>(Config::AmmoWheel::Performance::Tier);
		if (GetUInt32Value(ini, "Performance", "Tier", tierVal)) {
			Config::AmmoWheel::Performance::Tier = std::clamp(static_cast<int>(tierVal), 0, 3);
		}
		GetBoolValue(ini, "Performance", "OverrideVisualPolish", Config::AmmoWheel::Performance::OverrideVisualPolish);
		GetBoolValue(ini, "Performance", "OverrideAnimations", Config::AmmoWheel::Performance::OverrideAnimations);
		GetBoolValue(ini, "Performance", "OverridePopup", Config::AmmoWheel::Performance::OverridePopup);
		GetBoolValue(ini, "Performance", "OverrideLabels", Config::AmmoWheel::Performance::OverrideLabels);
		GetBoolValue(ini, "Performance", "OverrideCenterPanel", Config::AmmoWheel::Performance::OverrideCenterPanel);
		GetBoolValue(ini, "Performance", "OverrideIndicators", Config::AmmoWheel::Performance::OverrideIndicators);
		GetFloatValue(ini, "Performance", "InventorySnapshotIntervalSeconds", Config::AmmoWheel::Performance::InventorySnapshotIntervalSeconds);
		Config::AmmoWheel::Performance::InventorySnapshotIntervalSeconds =
			std::clamp(Config::AmmoWheel::Performance::InventorySnapshotIntervalSeconds, 0.05f, 1.0f);
	}

	// [Presets] - Visual preset selection
	{
		uint32_t presetVal = static_cast<uint32_t>(Config::AmmoWheel::ActivePreset);
		if (GetUInt32Value(ini, "Presets", "ActivePreset", presetVal)) {
			Config::AmmoWheel::ActivePreset = std::clamp(static_cast<int>(presetVal), 0, 3);
		}
		uint32_t appliedVal = static_cast<uint32_t>(Config::AmmoWheel::PresetApplied);
		if (GetUInt32Value(ini, "Presets", "PresetApplied", appliedVal)) {
			Config::AmmoWheel::PresetApplied = std::clamp(static_cast<int>(appliedVal), 0, 3);
		}
	}
	
	bool shouldWritePreset = false;
	if (Config::AmmoWheel::ActivePreset == 0 && Config::AmmoWheel::PresetApplied != 0) {
		Config::AmmoWheel::PresetApplied = 0;
		ini.SetLongValue("Presets", "PresetApplied", 0);
		shouldWritePreset = true;
	}

	// If a preset is active and hasn't been applied yet, load its override INI
	if (Config::AmmoWheel::ActivePreset > 0 &&
		Config::AmmoWheel::ActivePreset != Config::AmmoWheel::PresetApplied) {
		std::string presetPath = fmt::format("{}\\{}\\AmmoWheel_Override.ini", 
			Config::AmmoWheel::PresetBasePath, Config::AmmoWheel::ActivePreset);
		
		CSimpleIniA presetIni;
		presetIni.SetUnicode();
		if (presetIni.LoadFile(presetPath.c_str()) >= 0) {
			logger::info("AmmoWheel: Loading preset {} override from '{}'", 
				Config::AmmoWheel::ActivePreset, presetPath);

			auto applyFloat = [&](const char* section, const char* key, float& outVal) {
				float value = outVal;
				if (GetFloatValue(presetIni, section, key, value)) {
					outVal = value;
					ini.SetDoubleValue(section, key, value);
					shouldWritePreset = true;
				}
			};
			auto applyBool = [&](const char* section, const char* key, bool& outVal) {
				bool value = outVal;
				if (GetBoolValue(presetIni, section, key, value)) {
					outVal = value;
					ini.SetBoolValue(section, key, value);
					shouldWritePreset = true;
				}
			};
			auto applyUIntClamped = [&](const char* section, const char* key, uint32_t& outVal, uint32_t minVal, uint32_t maxVal) {
				uint32_t value = outVal;
				if (GetUInt32Value(presetIni, section, key, value)) {
					value = std::clamp(value, minVal, maxVal);
					outVal = value;
					ini.SetLongValue(section, key, static_cast<long>(value));
					shouldWritePreset = true;
				}
			};
			
			// Override geometry/appearance from preset
			applyFloat("Position", "WheelRadius", Config::AmmoWheel::WheelRadius);
			applyFloat("Geometry", "InnerRadiusRatio", Config::AmmoWheel::InnerRadiusRatio);
			applyFloat("Geometry", "SlotGapDeg", Config::AmmoWheel::SlotGapDeg);
			applyFloat("Geometry", "IconSizePx", Config::AmmoWheel::IconSizePx);
			
			{
				uint32_t slotShapeVal = static_cast<uint32_t>(Config::AmmoWheel::SlotShape);
				applyUIntClamped("Appearance", "SlotShape", slotShapeVal, 0, 3);
				Config::AmmoWheel::SlotShape = static_cast<int>(slotShapeVal);
			}
			applyFloat("Appearance", "SlotCornerRadius", Config::AmmoWheel::SlotCornerRadius);
			applyFloat("Appearance", "SlotShapeScale", Config::AmmoWheel::SlotShapeScale);
			
			// Override display settings
			applyFloat("Display", "IconSize", Config::AmmoWheel::IconSize);
			applyFloat("Display", "IconRadiusRatio", Config::AmmoWheel::IconRadiusRatio);
			applyFloat("Display", "CountRadiusRatio", Config::AmmoWheel::CountRadiusRatio);
			applyFloat("Display", "CountFontSize", Config::AmmoWheel::CountFontSize);
			
			// Override label settings
			{
				uint32_t val = static_cast<uint32_t>(Config::AmmoWheel::NameLayoutMode);
				applyUIntClamped("Labels", "NameLayoutMode", val, 0, 3);
				Config::AmmoWheel::NameLayoutMode = static_cast<int>(val);
			}
			applyFloat("Labels", "NameMaxWidthPx", Config::AmmoWheel::NameMaxWidthPx);
			applyFloat("Labels", "NamePanelPaddingPx", Config::AmmoWheel::NamePanelPaddingPx);
			applyBool("Labels", "NameTextBgEnabled", Config::AmmoWheel::NameTextBgEnabled);
			applyFloat("Labels", "NameTextBgOpacity", Config::AmmoWheel::NameTextBgOpacity);
			applyFloat("Labels", "NameTextBgCornerRounding", Config::AmmoWheel::NameTextBgCornerRounding);
			
			// Override popup settings
			applyFloat("Popup", "BubbleRadius", Config::AmmoWheel::PopupBubbleRadius);
			applyFloat("Popup", "PopupOffsetPx", Config::AmmoWheel::PopupOffsetPx);
			applyFloat("Popup", "PopupPaddingPx", Config::AmmoWheel::PopupPaddingPx);
			
			// Override visual polish
			applyBool("VisualPolish", "BackgroundEnabled", Config::AmmoWheel::BackgroundEnabled);
			applyFloat("VisualPolish", "BackgroundOpacity", Config::AmmoWheel::BackgroundOpacity);
			applyFloat("VisualPolish", "BackgroundSoftEdgeRatio", Config::AmmoWheel::BackgroundSoftEdgeRatio);
			applyBool("VisualPolish", "BorderEnabled", Config::AmmoWheel::BorderEnabled);
			applyBool("VisualPolish", "SlotShadowEnabled", Config::AmmoWheel::SlotShadowEnabled);
			applyBool("VisualPolish", "SlotHighlightEnabled", Config::AmmoWheel::SlotHighlightEnabled);
			applyBool("VisualPolish", "SlotBackgroundShadeEnabled", Config::AmmoWheel::SlotBackgroundShadeEnabled);
			applyFloat("VisualPolish", "SlotBackgroundShadeOpacity", Config::AmmoWheel::SlotBackgroundShadeOpacity);
			
			// Override animations
			applyBool("Animations", "HoverPulseEnabled", Config::AmmoWheel::HoverPulseEnabled);
			applyFloat("Animations", "HoverPulseSpeed", Config::AmmoWheel::HoverPulseSpeed);
			applyFloat("Animations", "HoverPulseSize", Config::AmmoWheel::HoverPulseSize);
			applyBool("Animations", "SlotDividerReskinBreathingEnabled", Config::AmmoWheel::SlotDividerReskinBreathingEnabled);
			applyFloat("Animations", "SlotDividerReskinBreathingSpeed", Config::AmmoWheel::SlotDividerReskinBreathingSpeed);
			applyFloat("Animations", "SlotDividerReskinBreathingIntensity", Config::AmmoWheel::SlotDividerReskinBreathingIntensity);
			applyFloat("Animations", "SlotDividerReskinBreathingOpacity", Config::AmmoWheel::SlotDividerReskinBreathingOpacity);

			// Override time slow
			applyBool("TimeSlow", "Enabled", Config::AmmoWheel::TimeSlowEnabled);
			applyFloat("TimeSlow", "SlowTimeScale", Config::AmmoWheel::TimeSlowScale);

			// Override low ammo indicator positioning
			applyBool("Indicators", "LowAmmoIndicatorEnabled", Config::AmmoWheel::LowAmmoIndicatorEnabled);
			{
				uint32_t threshold = static_cast<uint32_t>(Config::AmmoWheel::LowAmmoThreshold);
				applyUIntClamped("Indicators", "LowAmmoThreshold", threshold, 1, 9999);
				Config::AmmoWheel::LowAmmoThreshold = static_cast<int>(threshold);
			}
			applyFloat("Indicators", "LowAmmoIndicatorThickness", Config::AmmoWheel::LowAmmoIndicatorThickness);
			applyFloat("Indicators", "LowAmmoIndicatorRadiusRatio", Config::AmmoWheel::LowAmmoIndicatorRadiusRatio);
			applyFloat("Indicators", "LowAmmoIndicatorAngularOffsetDeg", Config::AmmoWheel::LowAmmoIndicatorAngularOffsetDeg);
			applyFloat("Indicators", "LowAmmoIndicatorRadialOffsetPx", Config::AmmoWheel::LowAmmoIndicatorRadialOffsetPx);
			{
				uint32_t drawLayer = static_cast<uint32_t>(Config::AmmoWheel::LowAmmoIndicatorDrawLayer);
				applyUIntClamped("Indicators", "LowAmmoIndicatorDrawLayer", drawLayer, 0, 2);
				Config::AmmoWheel::LowAmmoIndicatorDrawLayer = static_cast<int>(drawLayer);
			}
			
			// Override theme
			applyBool("Theme", "UseSkyrimTheme", Config::AmmoWheel::UseSkyrimTheme);
			
			logger::info("AmmoWheel: Preset {} loaded successfully", Config::AmmoWheel::ActivePreset);
			Config::AmmoWheel::PresetApplied = Config::AmmoWheel::ActivePreset;
			ini.SetLongValue("Presets", "PresetApplied", Config::AmmoWheel::PresetApplied);
			shouldWritePreset = true;
		} else {
			logger::warn("AmmoWheel: Preset {} override INI not found at '{}'", 
				Config::AmmoWheel::ActivePreset, presetPath);
		}
	}

	if (shouldWritePreset) {
		ini.SaveFile(AMMOWHEELSETTINGS_PATH);
	}

	// [Debug] - Phase 0: Debug logging toggles
	GetBoolValue(ini, "Debug", "LogConfigApply", Config::AmmoWheel::Debug::LogConfigApply);
	GetBoolValue(ini, "Debug", "LogInput", Config::AmmoWheel::Debug::LogInput);
	GetBoolValue(ini, "Debug", "LogSorting", Config::AmmoWheel::Debug::LogSorting);
	GetBoolValue(ini, "Debug", "LogLayout", Config::AmmoWheel::Debug::LogLayout);
	GetBoolValue(ini, "Debug", "LogCentralPanel", Config::AmmoWheel::Debug::LogCentralPanel);
	GetBoolValue(ini, "Debug", "LogPerf", Config::AmmoWheel::Debug::LogPerf);
	GetBoolValue(ini, "Debug", "ShowReskinOverlay", Config::AmmoWheel::Debug::ShowReskinOverlay);
	GetBoolValue(ini, "Debug", "LogPresetResolution", Config::AmmoWheel::Debug::LogPresetResolution);
	GetBoolValue(ini, "Debug", "LogAssetLoading", Config::AmmoWheel::Debug::LogAssetLoading);
	
	// [Sort] - Multi-criteria sorting system
	{
		uint32_t val = static_cast<uint32_t>(Config::AmmoWheel::Sort::Primary);
		if (GetUInt32Value(ini, "Sort", "Primary", val)) {
			Config::AmmoWheel::Sort::Primary = std::clamp(static_cast<int>(val), 0, 4);
		}
		val = static_cast<uint32_t>(Config::AmmoWheel::Sort::Secondary);
		if (GetUInt32Value(ini, "Sort", "Secondary", val)) {
			Config::AmmoWheel::Sort::Secondary = std::clamp(static_cast<int>(val), 0, 4);
		}
		val = static_cast<uint32_t>(Config::AmmoWheel::Sort::Tertiary);
		if (GetUInt32Value(ini, "Sort", "Tertiary", val)) {
			Config::AmmoWheel::Sort::Tertiary = std::clamp(static_cast<int>(val), 0, 4);
		}
	}
	GetBoolValue(ini, "Sort", "DirectionPrimaryAsc", Config::AmmoWheel::Sort::DirectionPrimaryAsc);
	GetBoolValue(ini, "Sort", "DirectionSecondaryAsc", Config::AmmoWheel::Sort::DirectionSecondaryAsc);
	GetBoolValue(ini, "Sort", "DirectionTertiaryAsc", Config::AmmoWheel::Sort::DirectionTertiaryAsc);
	GetBoolValue(ini, "Sort", "Stable", Config::AmmoWheel::Sort::Stable);
	GetBoolValue(ini, "Sort", "FavoritesFirst", Config::AmmoWheel::Sort::FavoritesFirst);
	GetBoolValue(ini, "Sort", "GroupByType", Config::AmmoWheel::Sort::GroupByType);
	GetBoolValue(ini, "Sort", "RememberAmmoByWeaponType", Config::AmmoWheel::Sort::RememberAmmoByWeaponType);
	
	// [Sort] - Ammo Limits (0 = no limit, >0 = top N items after sorting)
	{
		uint32_t val = static_cast<uint32_t>(Config::AmmoWheel::Sort::ArrowLimit);
		if (GetUInt32Value(ini, "Sort", "ArrowLimit", val)) {
			Config::AmmoWheel::Sort::ArrowLimit = std::clamp(static_cast<int>(val), 0, 128);
		}
		val = static_cast<uint32_t>(Config::AmmoWheel::Sort::BoltLimit);
		if (GetUInt32Value(ini, "Sort", "BoltLimit", val)) {
			Config::AmmoWheel::Sort::BoltLimit = std::clamp(static_cast<int>(val), 0, 128);
		}
	}
	
	// Log config apply if debug enabled
	if (Config::AmmoWheel::Debug::LogConfigApply) {
		logger::info("AmmoWheel Config: Sort Primary={}, Secondary={}, Tertiary={}, FavoritesFirst={}, RememberAmmoByWeaponType={}, ArrowLimit={}, BoltLimit={}",
			Config::AmmoWheel::Sort::Primary, Config::AmmoWheel::Sort::Secondary,
			Config::AmmoWheel::Sort::Tertiary, Config::AmmoWheel::Sort::FavoritesFirst,
			Config::AmmoWheel::Sort::RememberAmmoByWeaponType,
			Config::AmmoWheel::Sort::ArrowLimit, Config::AmmoWheel::Sort::BoltLimit);
	}


	// [Navigation] - mouse, gamepad and selection behavior (TASK 1)
	GetFloatValue(ini, "Navigation", "MouseDeadzone", Config::AmmoWheel::MouseDeadzone);
	GetFloatValue(ini, "Navigation", "MouseSmoothingSpeed", Config::AmmoWheel::MouseSmoothingSpeed);
	GetFloatValue(ini, "Navigation", "MouseMaxAngularSpeed", Config::AmmoWheel::MouseMaxAngularSpeed);
	GetFloatValue(ini, "Navigation", "GamepadDeadzone", Config::AmmoWheel::GamepadDeadzone);
	GetFloatValue(ini, "Navigation", "GamepadSmoothingSpeed", Config::AmmoWheel::GamepadSmoothingSpeed);
	GetBoolValue(ini, "Navigation", "StartOnLastSelected", Config::AmmoWheel::StartOnLastSelected);
	GetBoolValue(ini, "Navigation", "RememberLastAcrossSessions", Config::AmmoWheel::RememberLastAcrossSessions);
	GetBoolValue(ini, "Navigation", "SmoothSlotTransition", Config::AmmoWheel::SmoothSlotTransition);
	GetBoolValue(ini, "Navigation", "ResetFiltersOnOpen", Config::AmmoWheel::ResetFiltersOnOpen);
	GetBoolValue(ini, "Navigation", "DebugLogNavigation", Config::AmmoWheel::DebugLogNavigation);
	
	// Debug logging for style/theme resolution
	GetBoolValue(ini, "Debug", "LogStyleResolution", Config::AmmoWheel::DebugLogStyleResolution);
	GetBoolValue(ini, "Debug", "LogThemeState", Config::AmmoWheel::DebugLogThemeState);
	GetBoolValue(ini, "Debug", "LogResourceState", Config::AmmoWheel::DebugLogResourceState);
	
	// Log theme state on config reload if enabled
	if (Config::AmmoWheel::DebugLogThemeState) {
		logger::info("[AmmoWheel Theme State] UseSkyrimTheme={}, BorderEnabled={}, BackgroundEnabled={}, SlotShape={}",
			Config::AmmoWheel::UseSkyrimTheme,
			Config::AmmoWheel::BorderEnabled,
			Config::AmmoWheel::BackgroundEnabled,
			Config::AmmoWheel::SlotShape);
		logger::info("[AmmoWheel Theme State] BackgroundOpacity={:.2f}, BorderInnerScale={:.2f}, BorderOuterScale={:.2f}",
			Config::AmmoWheel::BackgroundOpacity,
			Config::AmmoWheel::BorderInnerScale,
			Config::AmmoWheel::BorderOuterScale);
		logger::info("[AmmoWheel Theme State] PopupAnim::BackgroundOpacity={:.2f}, PopupUseCustomColor={}",
			Config::AmmoWheel::PopupAnim::BackgroundOpacity,
			Config::AmmoWheel::PopupUseCustomColor);
	}
	{
		uint32_t val = static_cast<uint32_t>(Config::AmmoWheel::NavigationApplyMode);
		if (GetUInt32Value(ini, "Navigation", "ApplyMode", val)) {
			Config::AmmoWheel::NavigationApplyMode = std::clamp(static_cast<int>(val), 0, 1);
		}
	}
	{
		uint32_t val = static_cast<uint32_t>(Config::AmmoWheel::HalfWheelClampMode);
		if (GetUInt32Value(ini, "Navigation", "HalfWheelClampMode", val)) {
			Config::AmmoWheel::HalfWheelClampMode = std::clamp(static_cast<int>(val), 0, 2);
		}
	}
	GetFloatValue(ini, "Navigation", "ArcSelectionDeadbandDeg", Config::AmmoWheel::ArcSelectionDeadbandDeg);
	GetFloatValue(ini, "Navigation", "GamepadHoverHysteresisDeg", Config::AmmoWheel::GamepadHoverHysteresisDeg);
	Config::AmmoWheel::GamepadHoverHysteresisDeg = std::clamp(Config::AmmoWheel::GamepadHoverHysteresisDeg, 0.0f, 20.0f);
	
	// Clamp navigation values to safe ranges and log if clamped
	float origMouseDz = Config::AmmoWheel::MouseDeadzone;
	float origMouseSmooth = Config::AmmoWheel::MouseSmoothingSpeed;
	float origGamepadDz = Config::AmmoWheel::GamepadDeadzone;
	float origGamepadSmooth = Config::AmmoWheel::GamepadSmoothingSpeed;
	
	Config::AmmoWheel::MouseDeadzone = std::clamp(Config::AmmoWheel::MouseDeadzone, 0.0f, 0.5f);
	Config::AmmoWheel::MouseSmoothingSpeed = std::clamp(Config::AmmoWheel::MouseSmoothingSpeed, 1.0f, 50.0f);
	Config::AmmoWheel::GamepadDeadzone = std::clamp(Config::AmmoWheel::GamepadDeadzone, 0.0f, 0.9f);
	Config::AmmoWheel::GamepadSmoothingSpeed = std::clamp(Config::AmmoWheel::GamepadSmoothingSpeed, 1.0f, 50.0f);
	
	// Log once if values were clamped
	static bool navClampLogged = false;
	if (!navClampLogged) {
		if (origMouseDz != Config::AmmoWheel::MouseDeadzone ||
		    origMouseSmooth != Config::AmmoWheel::MouseSmoothingSpeed ||
		    origGamepadDz != Config::AmmoWheel::GamepadDeadzone ||
		    origGamepadSmooth != Config::AmmoWheel::GamepadSmoothingSpeed) {
			logger::warn("AmmoWheel: Navigation values clamped to safe range");
			navClampLogged = true;
		}
	}

	// [Filtering]
	GetBoolValue(ini, "Filtering", "ShowAllAmmo", Config::AmmoWheel::ShowAllAmmo);
	GetBoolValue(ini, "Filtering", "ShowModdedAmmo", Config::AmmoWheel::ShowModdedAmmo);
	{
		uint32_t minCount = static_cast<uint32_t>(Config::AmmoWheel::MinimumAmmoCount);
		if (GetUInt32Value(ini, "Filtering", "MinimumAmmoCount", minCount)) {
			Config::AmmoWheel::MinimumAmmoCount = static_cast<int>(minCount);
		}
	}
	GetBoolValue(ini, "Filtering", "SortByCount", Config::AmmoWheel::SortByCount);

	// [Display]
	GetBoolValue(ini, "Display", "ShowAmmoCount", Config::AmmoWheel::ShowAmmoCount);
	GetFloatValue(ini, "Display", "CountFontSize", Config::AmmoWheel::CountFontSize);
	GetUInt32Value(ini, "Display", "CountColor", Config::AmmoWheel::CountColor);
	GetBoolValue(ini, "Display", "EnableDebugOverlay", Config::AmmoWheel::EnableDebugOverlay);
	
	// Icon display
	GetBoolValue(ini, "Display", "ShowIcons", Config::AmmoWheel::ShowIcons);
	GetFloatValue(ini, "Display", "IconSize", Config::AmmoWheel::IconSize);
	GetFloatValue(ini, "Display", "IconRadiusRatio", Config::AmmoWheel::IconRadiusRatio);
	GetFloatValue(ini, "Display", "CountRadiusRatio", Config::AmmoWheel::CountRadiusRatio);
	GetBoolValue(ini, "Display", "IconHoverGlow", Config::AmmoWheel::IconHoverGlow);
	GetBoolValue(ini, "Display", "ShowCursorIndicator", Config::AmmoWheel::ShowCursorIndicator);
	GetBoolValue(ini, "Display", "ShowCursorDot", Config::AmmoWheel::ShowCursorIndicator);  // Legacy-safe alias
	{
		uint32_t r = 255, g = 215, b = 0, a = 100;
		GetUInt32Value(ini, "Display", "IconHoverGlowColorR", r);
		GetUInt32Value(ini, "Display", "IconHoverGlowColorG", g);
		GetUInt32Value(ini, "Display", "IconHoverGlowColorB", b);
		GetUInt32Value(ini, "Display", "IconHoverGlowColorA", a);
		Config::AmmoWheel::IconHoverGlowColor = IM_COL32(r, g, b, a);
	}
	
	// DEBUG: Log display settings
	logger::info("  [Display] ShowIcons={}, IconSize={:.0f}, IconRadiusRatio={:.2f}, CountRadiusRatio={:.2f}, IconHoverGlow={}, ShowCursorIndicator={}",
		Config::AmmoWheel::ShowIcons, Config::AmmoWheel::IconSize, 
		Config::AmmoWheel::IconRadiusRatio, Config::AmmoWheel::CountRadiusRatio,
		Config::AmmoWheel::IconHoverGlow, Config::AmmoWheel::ShowCursorIndicator);

	// [Geometry] - wheel size and layout
	GetFloatValue(ini, "Geometry", "InnerRadiusRatio", Config::AmmoWheel::InnerRadiusRatio);
	GetFloatValue(ini, "Geometry", "SlotGapDeg", Config::AmmoWheel::SlotGapDeg);
	GetFloatValue(ini, "Geometry", "IconSizePx", Config::AmmoWheel::IconSizePx);
	GetFloatValue(ini, "Geometry", "IconRadialOffsetPx", Config::AmmoWheel::IconRadialOffsetPx);
	GetFloatValue(ini, "Geometry", "TextRadialOffsetPx", Config::AmmoWheel::TextRadialOffsetPx);
	
	// DEBUG: Log geometry settings
	logger::info("  [Geometry] InnerRadiusRatio={:.2f}, SlotGapDeg={:.1f}, IconSizePx={:.0f}",
		Config::AmmoWheel::InnerRadiusRatio, Config::AmmoWheel::SlotGapDeg, Config::AmmoWheel::IconSizePx);

	// [Center] - center panel settings
	GetBoolValue(ini, "Center", "Enabled", Config::AmmoWheel::CenterEnabled);
	GetBoolValue(ini, "Center", "BgEnabled", Config::AmmoWheel::CenterBgEnabled);
	GetFloatValue(ini, "Center", "BgOpacity", Config::AmmoWheel::CenterBgOpacity);
	GetFloatValue(ini, "Center", "PaddingPx", Config::AmmoWheel::CenterPaddingPx);
	GetFloatValue(ini, "Center", "MaxWidthRatio", Config::AmmoWheel::CenterMaxWidthRatio);
	GetFloatValue(ini, "Center", "LineSpacingPx", Config::AmmoWheel::CenterLineSpacingPx);
	// TASK 2: Center panel positioning
	GetFloatValue(ini, "Center", "PanelInsetRatio", Config::AmmoWheel::CenterPanelInsetRatio);
	GetFloatValue(ini, "Center", "SafeMargin", Config::AmmoWheel::CenterPanelSafeMargin);

	// [Text] - font sizing and text customization
	GetFloatValue(ini, "Text", "NameFontPx", Config::AmmoWheel::NameFontPx);
	GetFloatValue(ini, "Text", "CountFontPx", Config::AmmoWheel::CountFontPx);
	GetFloatValue(ini, "Text", "CenterFontPx", Config::AmmoWheel::CenterFontPx);
	GetFloatValue(ini, "Text", "NameTextScale", Config::AmmoWheel::NameTextScale);

	// [Popup] - dMenu uses "PopupXxx" keys, also check legacy "Xxx" keys for compatibility
	GetBoolValue(ini, "Popup", "PopupEnabled", Config::AmmoWheel::PopupEnabled);
	GetBoolValue(ini, "Popup", "Enabled", Config::AmmoWheel::PopupEnabled);  // Legacy fallback
	GetFloatValue(ini, "Popup", "PopupIconSizePx", Config::AmmoWheel::PopupIconSizePx);
	GetFloatValue(ini, "Popup", "IconSizePx", Config::AmmoWheel::PopupIconSizePx);  // Legacy fallback
	GetFloatValue(ini, "Popup", "PopupNameFontPx", Config::AmmoWheel::PopupNameFontPx);
	GetFloatValue(ini, "Popup", "NameFontPx", Config::AmmoWheel::PopupNameFontPx);  // Legacy fallback
	GetFloatValue(ini, "Popup", "PopupCountFontPx", Config::AmmoWheel::PopupCountFontPx);
	GetFloatValue(ini, "Popup", "CountFontPx", Config::AmmoWheel::PopupCountFontPx);  // Legacy fallback
	GetFloatValue(ini, "Popup", "PopupOffsetPx", Config::AmmoWheel::PopupOffsetPx);
	GetFloatValue(ini, "Popup", "OffsetPx", Config::AmmoWheel::PopupOffsetPx);  // Legacy fallback
	GetFloatValue(ini, "Popup", "PopupPaddingPx", Config::AmmoWheel::PopupPaddingPx);
	GetFloatValue(ini, "Popup", "PaddingPx", Config::AmmoWheel::PopupPaddingPx);  // Legacy fallback
	GetBoolValue(ini, "Popup", "PopupUseCustomColor", Config::AmmoWheel::PopupUseCustomColor);
	GetBoolValue(ini, "Popup", "UseCustomColor", Config::AmmoWheel::PopupUseCustomColor);  // Legacy fallback
	GetUInt32Value(ini, "Popup", "PopupBackgroundColor", Config::AmmoWheel::PopupBackgroundColor);
	GetUInt32Value(ini, "Popup", "BackgroundColor", Config::AmmoWheel::PopupBackgroundColor);  // Legacy fallback
	// TASK 5: Circular bubble popup
	GetFloatValue(ini, "Popup", "BubbleRadius", Config::AmmoWheel::PopupBubbleRadius);
	GetBoolValue(ini, "Popup", "Circular", Config::AmmoWheel::PopupCircular);
	{
		uint32_t popupShapeModeVal = static_cast<uint32_t>(Config::AmmoWheel::PopupShapeMode);
		if (GetUInt32Value(ini, "Popup", "ShapeMode", popupShapeModeVal) ||
			GetUInt32Value(ini, "Popup", "PopupShapeMode", popupShapeModeVal)) {
			Config::AmmoWheel::PopupShapeMode = static_cast<int>(std::clamp<uint32_t>(popupShapeModeVal, 0, 5));
		}
	}
	GetFloatValue(ini, "Popup", "BlobJaggedness", Config::AmmoWheel::PopupBlobJaggedness);
	GetFloatValue(ini, "Popup", "BlobWobbleSpeed", Config::AmmoWheel::PopupBlobWobbleSpeed);
	{
		uint32_t blobPointCountVal = static_cast<uint32_t>(Config::AmmoWheel::PopupBlobPointCount);
		if (GetUInt32Value(ini, "Popup", "BlobPointCount", blobPointCountVal)) {
			Config::AmmoWheel::PopupBlobPointCount = static_cast<int>(std::clamp<uint32_t>(blobPointCountVal, 8, 48));
		}
	}
	GetFloatValue(ini, "Popup", "SunDragonTone", Config::AmmoWheel::PopupSunDragonTone);
	GetFloatValue(ini, "Popup", "PopupSunDragonTone", Config::AmmoWheel::PopupSunDragonTone);  // Legacy-safe alias
	GetFloatValue(ini, "Popup", "AnimationSpeed", Config::AmmoWheel::PopupAnimationSpeed);
	Config::AmmoWheel::PopupBlobJaggedness = std::clamp(Config::AmmoWheel::PopupBlobJaggedness, 0.0f, 0.45f);
	Config::AmmoWheel::PopupBlobWobbleSpeed = std::clamp(Config::AmmoWheel::PopupBlobWobbleSpeed, 0.0f, 8.0f);
	Config::AmmoWheel::PopupBlobPointCount = std::clamp(Config::AmmoWheel::PopupBlobPointCount, 8, 48);
	Config::AmmoWheel::PopupSunDragonTone = std::clamp(Config::AmmoWheel::PopupSunDragonTone, 0.0f, 2.0f);
	
	// [Popup.Animation] - Enhanced popup animation
	GetBoolValue(ini, "Popup.Animation", "Enabled", Config::AmmoWheel::PopupAnim::Enabled);
	GetFloatValue(ini, "Popup.Animation", "HoverInMs", Config::AmmoWheel::PopupAnim::HoverInMs);
	GetFloatValue(ini, "Popup.Animation", "HoverOutMs", Config::AmmoWheel::PopupAnim::HoverOutMs);
	GetFloatValue(ini, "Popup.Animation", "ScaleFrom", Config::AmmoWheel::PopupAnim::ScaleFrom);
	GetFloatValue(ini, "Popup.Animation", "ScaleTo", Config::AmmoWheel::PopupAnim::ScaleTo);
	{
		uint32_t val = static_cast<uint32_t>(Config::AmmoWheel::PopupAnim::Easing);
		if (GetUInt32Value(ini, "Popup.Animation", "Easing", val)) {
			Config::AmmoWheel::PopupAnim::Easing = std::clamp(static_cast<int>(val), 0, 2);
		}
	}
	GetFloatValue(ini, "Popup.Animation", "BorderThickness", Config::AmmoWheel::PopupAnim::BorderThickness);
	GetFloatValue(ini, "Popup.Animation", "BorderOpacity", Config::AmmoWheel::PopupAnim::BorderOpacity);
	GetFloatValue(ini, "Popup.Animation", "BackgroundOpacity", Config::AmmoWheel::PopupAnim::BackgroundOpacity);
	// Clamp values
	Config::AmmoWheel::PopupAnim::HoverInMs = std::clamp(Config::AmmoWheel::PopupAnim::HoverInMs, 10.0f, 1000.0f);
	Config::AmmoWheel::PopupAnim::HoverOutMs = std::clamp(Config::AmmoWheel::PopupAnim::HoverOutMs, 10.0f, 1000.0f);
	Config::AmmoWheel::PopupAnim::ScaleFrom = std::clamp(Config::AmmoWheel::PopupAnim::ScaleFrom, 0.1f, 1.0f);
	Config::AmmoWheel::PopupAnim::ScaleTo = std::clamp(Config::AmmoWheel::PopupAnim::ScaleTo, 0.5f, 2.0f);
	Config::AmmoWheel::PopupAnim::BorderOpacity = std::clamp(Config::AmmoWheel::PopupAnim::BorderOpacity, 0.0f, 1.0f);
	Config::AmmoWheel::PopupAnim::BackgroundOpacity = std::clamp(Config::AmmoWheel::PopupAnim::BackgroundOpacity, 0.0f, 1.0f);
	
	// DEBUG: Log popup animation values
	logger::info("  [Popup.Animation] Enabled={}, HoverIn={:.0f}ms, HoverOut={:.0f}ms, ScaleFrom={:.2f}, ScaleTo={:.2f}, Easing={}",
		Config::AmmoWheel::PopupAnim::Enabled,
		Config::AmmoWheel::PopupAnim::HoverInMs,
		Config::AmmoWheel::PopupAnim::HoverOutMs,
		Config::AmmoWheel::PopupAnim::ScaleFrom,
		Config::AmmoWheel::PopupAnim::ScaleTo,
		Config::AmmoWheel::PopupAnim::Easing);

	// [Labels] - dMenu uses "LabelXxx" keys, also check legacy "Xxx" keys for compatibility
	GetBoolValue(ini, "Labels", "LabelShow", Config::AmmoWheel::LabelShow);
	GetBoolValue(ini, "Labels", "Show", Config::AmmoWheel::LabelShow);  // Legacy fallback
	{
		uint32_t val = static_cast<uint32_t>(Config::AmmoWheel::LabelTruncateLength);
		if (GetUInt32Value(ini, "Labels", "LabelTruncateLength", val)) {
			Config::AmmoWheel::LabelTruncateLength = static_cast<int>(val);
		}
		if (GetUInt32Value(ini, "Labels", "TruncateLength", val)) {  // Legacy fallback
			Config::AmmoWheel::LabelTruncateLength = static_cast<int>(val);
		}
	}
	GetBoolValue(ini, "Labels", "LabelAbbreviate", Config::AmmoWheel::LabelAbbreviate);
	GetBoolValue(ini, "Labels", "Abbreviate", Config::AmmoWheel::LabelAbbreviate);  // Legacy fallback
	// TASK 3: Multi-line text stacking
	GetBoolValue(ini, "Labels", "MultiLine", Config::AmmoWheel::LabelMultiLine);
	GetFloatValue(ini, "Labels", "MaxSlotArcRatio", Config::AmmoWheel::LabelMaxSlotArcRatio);
	
	// Name Label Layout System
	{
		uint32_t val = static_cast<uint32_t>(Config::AmmoWheel::NameLayoutMode);
		if (GetUInt32Value(ini, "Labels", "NameLayoutMode", val)) {
			Config::AmmoWheel::NameLayoutMode = std::clamp(static_cast<int>(val), 0, 3);
		}
	}
	{
		uint32_t val = static_cast<uint32_t>(Config::AmmoWheel::NameMaxLines);
		if (GetUInt32Value(ini, "Labels", "NameMaxLines", val)) {
			Config::AmmoWheel::NameMaxLines = std::clamp(static_cast<int>(val), 1, 5);
		}
	}
	GetFloatValue(ini, "Labels", "NameMinFontPx", Config::AmmoWheel::NameMinFontPx);
	Config::AmmoWheel::NameMinFontPx = std::clamp(Config::AmmoWheel::NameMinFontPx, 10.0f, 48.0f);
	GetFloatValue(ini, "Labels", "NameMaxWidthPx", Config::AmmoWheel::NameMaxWidthPx);
	GetFloatValue(ini, "Labels", "NamePanelPaddingPx", Config::AmmoWheel::NamePanelPaddingPx);
	GetFloatValue(ini, "Labels", "NameLineSpacingPx", Config::AmmoWheel::NameLineSpacingPx);
	GetFloatValue(ini, "Labels", "NameMarginPx", Config::AmmoWheel::NameMarginPx);
	
	// Text background panel
	GetBoolValue(ini, "Labels", "NameTextBgEnabled", Config::AmmoWheel::NameTextBgEnabled);
	GetFloatValue(ini, "Labels", "NameTextBgOpacity", Config::AmmoWheel::NameTextBgOpacity);
	Config::AmmoWheel::NameTextBgOpacity = std::clamp(Config::AmmoWheel::NameTextBgOpacity, 0.0f, 1.0f);
	{
		uint32_t r = 0, g = 0, b = 0, a = 255;
		GetUInt32Value(ini, "Labels", "NameTextBgColorR", r);
		GetUInt32Value(ini, "Labels", "NameTextBgColorG", g);
		GetUInt32Value(ini, "Labels", "NameTextBgColorB", b);
		GetUInt32Value(ini, "Labels", "NameTextBgColorA", a);
		Config::AmmoWheel::NameTextBgColor = IM_COL32(
			std::clamp(r, 0u, 255u),
			std::clamp(g, 0u, 255u),
			std::clamp(b, 0u, 255u),
			std::clamp(a, 0u, 255u)
		);
	}
	GetFloatValue(ini, "Labels", "NameTextBgCornerRounding", Config::AmmoWheel::NameTextBgCornerRounding);
	GetFloatValue(ini, "Labels", "NameTextBgExtraPaddingPx", Config::AmmoWheel::NameTextBgExtraPaddingPx);
	GetFloatValue(ini, "Labels", "NameTextBgInsetPx", Config::AmmoWheel::NameTextBgInsetPx);
	
	// Debug visualization
	GetBoolValue(ini, "Labels", "DebugDrawTextRects", Config::AmmoWheel::DebugDrawTextRects);

	// [InputBlocking] - TASK 1: Block attack when wheel is open
	GetBoolValue(ini, "InputBlocking", "BlockAttackWhenOpen", Config::AmmoWheel::BlockAttackWhenOpen);
	GetBoolValue(ini, "InputBlocking", "ConsumeLMBWhenOpen", Config::AmmoWheel::ConsumeLMBWhenOpen);
	GetBoolValue(ini, "InputBlocking", "ConsumeRMBWhenOpen", Config::AmmoWheel::ConsumeRMBWhenOpen);
	GetBoolValue(ini, "InputBlocking", "ConsumeGamepadAttackWhenOpen", Config::AmmoWheel::ConsumeGamepadAttackWhenOpen);
	GetBoolValue(ini, "InputBlocking", "ClickSelectRequiresHover", Config::AmmoWheel::ClickSelectRequiresHover);
	GetBoolValue(ini, "InputBlocking", "AllowRMBUnequip", Config::AmmoWheel::AllowRMBUnequip);
	GetBoolValue(ini, "InputBlocking", "AllowChordFallback", Config::AmmoWheel::AllowChordFallback);

	// [CenterPanel] - TASK 2: Shape and positioning
	{
		uint32_t val = static_cast<uint32_t>(Config::AmmoWheel::CenterPanelShapeIndex);
		if (GetUInt32Value(ini, "CenterPanel", "ShapeType", val)) {
			Config::AmmoWheel::CenterPanelShapeIndex = std::clamp(static_cast<int>(val), 0, 3);
		}
	}
	GetFloatValue(ini, "CenterPanel", "CornerRounding", Config::AmmoWheel::CenterPanelCornerRounding);
	GetFloatValue(ini, "CenterPanel", "BorderThickness", Config::AmmoWheel::CenterPanelBorderThickness);
	GetFloatValue(ini, "CenterPanel", "BorderAlpha", Config::AmmoWheel::CenterPanelBorderAlpha);
	GetBoolValue(ini, "CenterPanel", "ClampToScreen", Config::AmmoWheel::CenterPanelClampToScreen);
	{
		uint32_t val = static_cast<uint32_t>(Config::AmmoWheel::CenterPanelPositionMode);
		if (GetUInt32Value(ini, "CenterPanel", "PositionMode", val)) {
			Config::AmmoWheel::CenterPanelPositionMode = std::clamp(static_cast<int>(val), 0, 1);
		}
	}
	GetFloatValue(ini, "CenterPanel", "OffsetX", Config::AmmoWheel::CenterPanelOffsetX);
	GetFloatValue(ini, "CenterPanel", "OffsetY", Config::AmmoWheel::CenterPanelOffsetY);

	// [CenterPanel.Text] - TASK 2: Text layout
	GetBoolValue(ini, "CenterPanel.Text", "Enabled", Config::AmmoWheel::CenterTextEnabled);
	GetFloatValue(ini, "CenterPanel.Text", "MinFontSize", Config::AmmoWheel::CenterTextMinFontSize);
	GetFloatValue(ini, "CenterPanel.Text", "MaxFontSize", Config::AmmoWheel::CenterTextMaxFontSize);
	{
		uint32_t val = static_cast<uint32_t>(Config::AmmoWheel::CenterTextLayoutMode);
		if (GetUInt32Value(ini, "CenterPanel.Text", "LayoutMode", val)) {
			Config::AmmoWheel::CenterTextLayoutMode = std::clamp(static_cast<int>(val), 0, 2);
		}
		val = static_cast<uint32_t>(Config::AmmoWheel::CenterTextMaxLines);
		if (GetUInt32Value(ini, "CenterPanel.Text", "MaxLines", val)) {
			Config::AmmoWheel::CenterTextMaxLines = std::clamp(static_cast<int>(val), 1, 10);
		}
	}
	GetBoolValue(ini, "CenterPanel.Text", "EllipsisEnabled", Config::AmmoWheel::CenterTextEllipsisEnabled);
	GetFloatValue(ini, "CenterPanel.Text", "MaxTextWidthRatio", Config::AmmoWheel::CenterTextMaxWidthRatio);
	GetBoolValue(ini, "CenterPanel.Text", "PreferWordSplit", Config::AmmoWheel::CenterTextPreferWordSplit);
	GetBoolValue(ini, "CenterPanel.Text", "ShadowEnabled", Config::AmmoWheel::CenterTextShadowEnabled);
	GetBoolValue(ini, "CenterPanel.Text", "OutlineEnabled", Config::AmmoWheel::CenterTextOutlineEnabled);
	GetFloatValue(ini, "CenterPanel.Text", "OffsetX", Config::AmmoWheel::CenterTextOffsetX);
	GetFloatValue(ini, "CenterPanel.Text", "OffsetY", Config::AmmoWheel::CenterTextOffsetY);
	
	// [CenterPanel.Text] - Word wrapping for long ammo names
	GetBoolValue(ini, "CenterPanel.Text", "EnableWordWrap", Config::AmmoWheel::EnableWordWrap);
	GetBoolValue(ini, "CenterPanel.Text", "WrapAtWordBoundary", Config::AmmoWheel::WrapAtWordBoundary);
	GetFloatValue(ini, "CenterPanel.Text", "WrapMaxLineWidthRatio", Config::AmmoWheel::WrapMaxLineWidthRatio);
	GetFloatValue(ini, "CenterPanel.Text", "WrapMaxLineWidthPx", Config::AmmoWheel::WrapMaxLineWidthPx);
	GetFloatValue(ini, "CenterPanel.Text", "WrapSafeMarginPx", Config::AmmoWheel::WrapSafeMarginPx);
	GetBoolValue(ini, "CenterPanel.Text", "AddWrapHyphen", Config::AmmoWheel::AddWrapHyphen);
	{
		uint32_t val = static_cast<uint32_t>(Config::AmmoWheel::WrapMaxLines);
		if (GetUInt32Value(ini, "CenterPanel.Text", "WrapMaxLines", val)) {
			Config::AmmoWheel::WrapMaxLines = std::clamp(static_cast<int>(val), 1, 5);
		}
	}
	
	GetBoolValue(ini, "CenterPanel", "ShowDescription", Config::AmmoWheel::CenterShowDescription);
	{
		uint32_t val = static_cast<uint32_t>(Config::AmmoWheel::CenterMaxDescriptionLines);
		if (GetUInt32Value(ini, "CenterPanel", "MaxDescriptionLines", val)) {
			Config::AmmoWheel::CenterMaxDescriptionLines = std::clamp(static_cast<int>(val), 1, 8);
		}
	}
	GetFloatValue(ini, "CenterPanel.Description", "FontScale", Config::AmmoWheel::CenterDescriptionFontScale);
	GetFloatValue(ini, "CenterPanel.Description", "OffsetX", Config::AmmoWheel::CenterDescriptionOffsetX);
	GetFloatValue(ini, "CenterPanel.Description", "OffsetY", Config::AmmoWheel::CenterDescriptionOffsetY);
	GetFloatValue(ini, "CenterPanel.Description", "LineSpacingPx", Config::AmmoWheel::CenterDescriptionLineSpacingPx);
	GetFloatValue(ini, "CenterPanel.Description", "Opacity", Config::AmmoWheel::CenterDescriptionOpacity);
	Config::AmmoWheel::CenterDescriptionFontScale = std::clamp(Config::AmmoWheel::CenterDescriptionFontScale, 0.25f, 2.5f);
	Config::AmmoWheel::CenterDescriptionLineSpacingPx = std::clamp(Config::AmmoWheel::CenterDescriptionLineSpacingPx, -20.0f, 60.0f);
	Config::AmmoWheel::CenterDescriptionOpacity = std::clamp(Config::AmmoWheel::CenterDescriptionOpacity, 0.0f, 1.0f);
	{
		uint32_t r = 220, g = 220, b = 220, a = 255;
		if (!readPackedColor("CenterPanel.Description", "Color", r, g, b, a)) {
			GetUInt32Value(ini, "CenterPanel.Description", "ColorR", r);
			GetUInt32Value(ini, "CenterPanel.Description", "ColorG", g);
			GetUInt32Value(ini, "CenterPanel.Description", "ColorB", b);
			GetUInt32Value(ini, "CenterPanel.Description", "ColorA", a);
		}
		Config::AmmoWheel::CenterDescriptionColor = IM_COL32(
			std::clamp(r, 0u, 255u),
			std::clamp(g, 0u, 255u),
			std::clamp(b, 0u, 255u),
			std::clamp(a, 0u, 255u));
	}

	// [CenterPanel.Fields] - Center panel field toggles
	GetBoolValue(ini, "CenterPanel.Fields", "ShowName", Config::AmmoWheel::CenterFields::ShowName);
	GetBoolValue(ini, "CenterPanel.Fields", "ShowDamage", Config::AmmoWheel::CenterFields::ShowDamage);
	GetBoolValue(ini, "CenterPanel.Fields", "ShowPoison", Config::AmmoWheel::CenterFields::ShowPoison);
	GetBoolValue(ini, "CenterPanel.Fields", "ShowType", Config::AmmoWheel::CenterFields::ShowType);
	GetBoolValue(ini, "CenterPanel.Fields", "ShowCount", Config::AmmoWheel::CenterFields::ShowCount);
	GetBoolValue(ini, "CenterPanel.Fields", "ShowSource", Config::AmmoWheel::CenterFields::ShowSource);
	GetStringValue(ini, "CenterPanel.Fields", "Order", Config::AmmoWheel::CenterFields::Order);
	
	// Damage highlight colors (two-tier system)
	{
		uint32_t maxR = 255, maxG = 200, maxB = 100, maxA = 255;
		uint32_t otherR = 255, otherG = 120, otherB = 120, otherA = 255;
		
		if (!readPackedColor("CenterPanel.Fields", "MaxDamageColor", maxR, maxG, maxB, maxA)) {
			GetUInt32Value(ini, "CenterPanel.Fields", "MaxDamageColorR", maxR);
			GetUInt32Value(ini, "CenterPanel.Fields", "MaxDamageColorG", maxG);
			GetUInt32Value(ini, "CenterPanel.Fields", "MaxDamageColorB", maxB);
			GetUInt32Value(ini, "CenterPanel.Fields", "MaxDamageColorA", maxA);
		}
		if (!readPackedColor("CenterPanel.Fields", "OtherDamageColor", otherR, otherG, otherB, otherA)) {
			GetUInt32Value(ini, "CenterPanel.Fields", "OtherDamageColorR", otherR);
			GetUInt32Value(ini, "CenterPanel.Fields", "OtherDamageColorG", otherG);
			GetUInt32Value(ini, "CenterPanel.Fields", "OtherDamageColorB", otherB);
			GetUInt32Value(ini, "CenterPanel.Fields", "OtherDamageColorA", otherA);
		}
		
		Config::AmmoWheel::CenterFields::MaxDamageColor = IM_COL32(
			std::clamp(maxR, 0u, 255u),
			std::clamp(maxG, 0u, 255u),
			std::clamp(maxB, 0u, 255u),
			std::clamp(maxA, 0u, 255u)
		);
		Config::AmmoWheel::CenterFields::OtherDamageColor = IM_COL32(
			std::clamp(otherR, 0u, 255u),
			std::clamp(otherG, 0u, 255u),
			std::clamp(otherB, 0u, 255u),
			std::clamp(otherA, 0u, 255u)
		);
	}
	
	// [CenterPanel.Skin] - SVG skin support
	GetBoolValue(ini, "CenterPanel.Skin", "UseSVG", Config::AmmoWheel::CenterSkin::UseSVG);
	GetStringValue(ini, "CenterPanel.Skin", "SVGPath", Config::AmmoWheel::CenterSkin::SVGPath);
	GetFloatValue(ini, "CenterPanel.Skin", "Opacity", Config::AmmoWheel::CenterSkin::Opacity);
	GetFloatValue(ini, "CenterPanel.Skin", "Scale", Config::AmmoWheel::CenterSkin::Scale);
	Config::AmmoWheel::CenterSkin::Opacity = std::clamp(Config::AmmoWheel::CenterSkin::Opacity, 0.0f, 1.0f);
	Config::AmmoWheel::CenterSkin::Scale = std::clamp(Config::AmmoWheel::CenterSkin::Scale, 0.1f, 3.0f);

	// [IconSystem] - AmmoWheel-specific icon folders for reskin mods
	GetStringValue(ini, "IconSystem", "IconDirectory", Config::AmmoWheel::IconDirectory);
	GetStringValue(ini, "IconSystem", "IconCustomDirectory", Config::AmmoWheel::IconCustomDirectory);
	GetBoolValue(ini, "IconSystem", "UseDedicatedIconFolder", Config::AmmoWheel::UseDedicatedIconFolder);
	
	// Validate icon paths - use defaults if empty
	if (Config::AmmoWheel::IconDirectory.empty()) {
		logger::warn("AmmoWheel: IconDirectory empty, using default");
		Config::AmmoWheel::IconDirectory = R"(.\Data\SKSE\Plugins\wheeler\resources\ammo_wheel\icons)";
	}
	if (Config::AmmoWheel::IconCustomDirectory.empty()) {
		logger::warn("AmmoWheel: IconCustomDirectory empty, using default");
		Config::AmmoWheel::IconCustomDirectory = R"(.\Data\SKSE\Plugins\wheeler\resources\ammo_wheel\icons_custom)";
	}
	
	// [VisualOverrides] - AmmoWheel-specific visual styles for reskin mods
	GetBoolValue(ini, "VisualOverrides", "UseCustomStyles", Config::AmmoWheel::UseCustomStyles);
	GetUInt32Value(ini, "VisualOverrides", "CustomUnhoveredColorBegin", Config::AmmoWheel::CustomUnhoveredColorBegin);
	GetUInt32Value(ini, "VisualOverrides", "CustomUnhoveredColorEnd", Config::AmmoWheel::CustomUnhoveredColorEnd);
	GetUInt32Value(ini, "VisualOverrides", "CustomHoveredColorBegin", Config::AmmoWheel::CustomHoveredColorBegin);
	GetUInt32Value(ini, "VisualOverrides", "CustomHoveredColorEnd", Config::AmmoWheel::CustomHoveredColorEnd);
	GetUInt32Value(ini, "VisualOverrides", "CustomActiveArcColorBegin", Config::AmmoWheel::CustomActiveArcColorBegin);
	GetUInt32Value(ini, "VisualOverrides", "CustomActiveArcColorEnd", Config::AmmoWheel::CustomActiveArcColorEnd);
	GetUInt32Value(ini, "VisualOverrides", "CustomTextColor", Config::AmmoWheel::CustomTextColor);
	GetUInt32Value(ini, "VisualOverrides", "CustomTextShadowColor", Config::AmmoWheel::CustomTextShadowColor);
	GetFloatValue(ini, "VisualOverrides", "CustomBackgroundOpacity", Config::AmmoWheel::CustomBackgroundOpacity);
	// Clamp opacity to valid range
	Config::AmmoWheel::CustomBackgroundOpacity = std::clamp(Config::AmmoWheel::CustomBackgroundOpacity, 0.0f, 1.0f);

	// [Skin] - Data-driven skin system settings
	GetBoolValue(ini, "Skin", "UseAmmoWheelStylesIni", Config::AmmoWheel::Skin::UseAmmoWheelStylesIni);
	GetStringValue(ini, "Skin", "SkinRoot", Config::AmmoWheel::Skin::SkinRoot);
	GetStringValue(ini, "Skin", "StylesIniPath", Config::AmmoWheel::Skin::StylesIniPath);
	// Preset system toggle - default OFF for vanilla compatibility
	GetBoolValue(ini, "Skin", "UsePresetStyles", Config::AmmoWheel::Skin::UsePresetStyles);
	
	// Log preset mode status
	logger::info("AmmoWheel: UsePresetStyles = {} ({})", 
		Config::AmmoWheel::Skin::UsePresetStyles ? "ON" : "OFF",
		Config::AmmoWheel::Skin::UsePresetStyles ? "Preset overrides enabled" : "Legacy style mode");
	
	// [Indicators] - Toggle indicators from AmmoWheel.ini
	GetBoolValue(ini, "Indicators", "EnableSelectedIndicator", Config::AmmoWheel::Skin::EnableSelectedIndicator);
	GetBoolValue(ini, "Indicators", "EnableHoveredIndicator", Config::AmmoWheel::Skin::EnableHoveredIndicator);
	GetBoolValue(ini, "Indicators", "EnableActiveIndicator", Config::AmmoWheel::Skin::EnableActiveIndicator);
	GetBoolValue(ini, "Indicators", "EnableChargeIndicator", Config::AmmoWheel::Skin::EnableChargeIndicator);
	
	// Load AmmoWheel Styles.ini if enabled
	if (Config::AmmoWheel::Skin::UseAmmoWheelStylesIni) {
		LoadAmmoWheelStylesIni();
	}

	// [Theme] - skin and resource settings
	GetStringValue(ini, "Theme", "SkinName", Config::AmmoWheel::SkinName);
	GetStringValue(ini, "Theme", "ResourceRoot", Config::AmmoWheel::ResourceRoot);
	GetBoolValue(ini, "Theme", "UseSkyrimTheme", Config::AmmoWheel::UseSkyrimTheme);

	// [SkyrimTheme] - Skyrim-inspired color palette
	{
		// Background layers
		uint32_t r, g, b, a;
		r = 20; g = 15; b = 10; a = 220;
		GetUInt32Value(ini, "SkyrimTheme", "BgDarkLayerR", r);
		GetUInt32Value(ini, "SkyrimTheme", "BgDarkLayerG", g);
		GetUInt32Value(ini, "SkyrimTheme", "BgDarkLayerB", b);
		GetUInt32Value(ini, "SkyrimTheme", "BgDarkLayerA", a);
		Config::AmmoWheel::SkyrimTheme::BgDarkLayer = IM_COL32(r, g, b, a);
		
		r = 45; g = 35; b = 25; a = 200;
		GetUInt32Value(ini, "SkyrimTheme", "BgMidLayerR", r);
		GetUInt32Value(ini, "SkyrimTheme", "BgMidLayerG", g);
		GetUInt32Value(ini, "SkyrimTheme", "BgMidLayerB", b);
		GetUInt32Value(ini, "SkyrimTheme", "BgMidLayerA", a);
		Config::AmmoWheel::SkyrimTheme::BgMidLayer = IM_COL32(r, g, b, a);
		
		r = 70; g = 50; b = 30; a = 100;
		GetUInt32Value(ini, "SkyrimTheme", "BgOuterGlowR", r);
		GetUInt32Value(ini, "SkyrimTheme", "BgOuterGlowG", g);
		GetUInt32Value(ini, "SkyrimTheme", "BgOuterGlowB", b);
		GetUInt32Value(ini, "SkyrimTheme", "BgOuterGlowA", a);
		Config::AmmoWheel::SkyrimTheme::BgOuterGlow = IM_COL32(r, g, b, a);
		
		// Slot colors
		r = 160; g = 140; b = 110; a = 200;
		GetUInt32Value(ini, "SkyrimTheme", "SlotUnhoveredInnerR", r);
		GetUInt32Value(ini, "SkyrimTheme", "SlotUnhoveredInnerG", g);
		GetUInt32Value(ini, "SkyrimTheme", "SlotUnhoveredInnerB", b);
		GetUInt32Value(ini, "SkyrimTheme", "SlotUnhoveredInnerA", a);
		Config::AmmoWheel::SkyrimTheme::SlotUnhoveredInner = IM_COL32(r, g, b, a);
		
		r = 120; g = 100; b = 70; a = 180;
		GetUInt32Value(ini, "SkyrimTheme", "SlotUnhoveredOuterR", r);
		GetUInt32Value(ini, "SkyrimTheme", "SlotUnhoveredOuterG", g);
		GetUInt32Value(ini, "SkyrimTheme", "SlotUnhoveredOuterB", b);
		GetUInt32Value(ini, "SkyrimTheme", "SlotUnhoveredOuterA", a);
		Config::AmmoWheel::SkyrimTheme::SlotUnhoveredOuter = IM_COL32(r, g, b, a);
		
		r = 210; g = 180; b = 140; a = 230;
		GetUInt32Value(ini, "SkyrimTheme", "SlotHoveredInnerR", r);
		GetUInt32Value(ini, "SkyrimTheme", "SlotHoveredInnerG", g);
		GetUInt32Value(ini, "SkyrimTheme", "SlotHoveredInnerB", b);
		GetUInt32Value(ini, "SkyrimTheme", "SlotHoveredInnerA", a);
		Config::AmmoWheel::SkyrimTheme::SlotHoveredInner = IM_COL32(r, g, b, a);
		
		r = 180; g = 150; b = 110; a = 210;
		GetUInt32Value(ini, "SkyrimTheme", "SlotHoveredOuterR", r);
		GetUInt32Value(ini, "SkyrimTheme", "SlotHoveredOuterG", g);
		GetUInt32Value(ini, "SkyrimTheme", "SlotHoveredOuterB", b);
		GetUInt32Value(ini, "SkyrimTheme", "SlotHoveredOuterA", a);
		Config::AmmoWheel::SkyrimTheme::SlotHoveredOuter = IM_COL32(r, g, b, a);
		
		// Border colors
		r = 218; g = 165; b = 32; a = 255;
		GetUInt32Value(ini, "SkyrimTheme", "BorderGoldR", r);
		GetUInt32Value(ini, "SkyrimTheme", "BorderGoldG", g);
		GetUInt32Value(ini, "SkyrimTheme", "BorderGoldB", b);
		GetUInt32Value(ini, "SkyrimTheme", "BorderGoldA", a);
		Config::AmmoWheel::SkyrimTheme::BorderGold = IM_COL32(r, g, b, a);
		
		r = 139; g = 90; b = 43; a = 255;
		GetUInt32Value(ini, "SkyrimTheme", "BorderBronzeR", r);
		GetUInt32Value(ini, "SkyrimTheme", "BorderBronzeG", g);
		GetUInt32Value(ini, "SkyrimTheme", "BorderBronzeB", b);
		GetUInt32Value(ini, "SkyrimTheme", "BorderBronzeA", a);
		Config::AmmoWheel::SkyrimTheme::BorderBronze = IM_COL32(r, g, b, a);
		
		// Active arc colors
		r = 218; g = 165; b = 32; a = 255;
		GetUInt32Value(ini, "SkyrimTheme", "ActiveArcInnerR", r);
		GetUInt32Value(ini, "SkyrimTheme", "ActiveArcInnerG", g);
		GetUInt32Value(ini, "SkyrimTheme", "ActiveArcInnerB", b);
		GetUInt32Value(ini, "SkyrimTheme", "ActiveArcInnerA", a);
		Config::AmmoWheel::SkyrimTheme::ActiveArcInner = IM_COL32(r, g, b, a);
		
		r = 139; g = 90; b = 43; a = 200;
		GetUInt32Value(ini, "SkyrimTheme", "ActiveArcOuterR", r);
		GetUInt32Value(ini, "SkyrimTheme", "ActiveArcOuterG", g);
		GetUInt32Value(ini, "SkyrimTheme", "ActiveArcOuterB", b);
		GetUInt32Value(ini, "SkyrimTheme", "ActiveArcOuterA", a);
		Config::AmmoWheel::SkyrimTheme::ActiveArcOuter = IM_COL32(r, g, b, a);
		
		// Text colors
		r = 240; g = 230; b = 210; a = 255;
		GetUInt32Value(ini, "SkyrimTheme", "TextPrimaryR", r);
		GetUInt32Value(ini, "SkyrimTheme", "TextPrimaryG", g);
		GetUInt32Value(ini, "SkyrimTheme", "TextPrimaryB", b);
		GetUInt32Value(ini, "SkyrimTheme", "TextPrimaryA", a);
		Config::AmmoWheel::SkyrimTheme::TextPrimary = IM_COL32(r, g, b, a);
		
		r = 218; g = 165; b = 32; a = 255;
		GetUInt32Value(ini, "SkyrimTheme", "TextAccentR", r);
		GetUInt32Value(ini, "SkyrimTheme", "TextAccentG", g);
		GetUInt32Value(ini, "SkyrimTheme", "TextAccentB", b);
		GetUInt32Value(ini, "SkyrimTheme", "TextAccentA", a);
		Config::AmmoWheel::SkyrimTheme::TextAccent = IM_COL32(r, g, b, a);
	}

	struct PresetColor {
		uint32_t r;
		uint32_t g;
		uint32_t b;
	};

	// LEGACY palette for old Colors section
	// 0=Custom, 1=White, 2=Black, 3=Red, 4=Green, 5=Blue, 6=Yellow, 7=Orange, 8=Cyan, 9=Magenta, 10=Gray
	constexpr PresetColor kLegacyPalette[] = {
		{0, 0, 0},       // Custom (unused)
		{255, 255, 255}, // White
		{0, 0, 0},       // Black
		{255, 60, 60},   // Red
		{60, 255, 60},   // Green
		{60, 60, 255},   // Blue
		{255, 255, 60},  // Yellow
		{255, 165, 0},   // Orange
		{60, 255, 255},  // Cyan
		{255, 60, 255},  // Magenta
		{128, 128, 128}  // Gray
	};
	constexpr size_t kLegacyPaletteSize = sizeof(kLegacyPalette) / sizeof(kLegacyPalette[0]);
	
	// Skyrim-themed palette for ColorOverrides/Indicators
	// 0=Custom, 1=White, 2=Cream, 3=Gold, 4=Silver, 5=Copper, 6=Ice Blue, 7=Blood Red, 8=Forest Green, 9=Shadow, 10=Parchment
	constexpr PresetColor kSkyrimPalette[] = {
		{0, 0, 0},       // Custom (unused)
		{255, 255, 255}, // White
		{255, 253, 240}, // Cream
		{255, 215, 0},   // Gold
		{192, 192, 192}, // Silver
		{184, 115, 51},  // Copper
		{173, 216, 230}, // Ice Blue
		{139, 0, 0},     // Blood Red
		{34, 139, 34},   // Forest Green
		{47, 47, 47},    // Shadow
		{240, 230, 210}  // Parchment
	};
	constexpr size_t kSkyrimPaletteSize = sizeof(kSkyrimPalette) / sizeof(kSkyrimPalette[0]);

	auto applyPresetRGB = [](const PresetColor* palette, size_t paletteSize, uint32_t preset,
		uint32_t& r, uint32_t& g, uint32_t& b) {
		if (!palette || paletteSize == 0 || preset == 0) {
			return;
		}
		uint32_t maxIndex = static_cast<uint32_t>(paletteSize - 1);
		uint32_t index = std::clamp(preset, 1u, maxIndex);
		r = palette[index].r;
		g = palette[index].g;
		b = palette[index].b;
	};

	auto toImU32 = [](uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
		const float inv255 = 1.0f / 255.0f;
		ImVec4 color(
			std::clamp(r, 0u, 255u) * inv255,
			std::clamp(g, 0u, 255u) * inv255,
			std::clamp(b, 0u, 255u) * inv255,
			std::clamp(a, 0u, 255u) * inv255);
		return ImGui::ColorConvertFloat4ToU32(color);
	};

	// [Colors] - color presets and opacity
	GetUInt32Value(ini, "Colors", "NameTextColorPreset", Config::AmmoWheel::NameTextColorPreset);
	GetUInt32Value(ini, "Colors", "NameTextOpacity", Config::AmmoWheel::NameTextOpacity);
	GetUInt32Value(ini, "Colors", "LowAmmoColorPreset", Config::AmmoWheel::LowAmmoColorPreset);
	GetUInt32Value(ini, "Colors", "LowAmmoOpacity", Config::AmmoWheel::LowAmmoOpacity);
	GetUInt32Value(ini, "Colors", "SelectedColorPreset", Config::AmmoWheel::SelectedColorPreset);
	GetUInt32Value(ini, "Colors", "SelectedOpacity", Config::AmmoWheel::SelectedOpacity);
	GetUInt32Value(ini, "Colors", "HoverColorPreset", Config::AmmoWheel::HoverColorPreset);
	GetUInt32Value(ini, "Colors", "HoverOpacity", Config::AmmoWheel::HoverOpacity);

	// [Colors.Advanced] - custom RGBA (used when preset == 0)
	GetUInt32Value(ini, "Colors.Advanced", "NameTextColorR", Config::AmmoWheel::NameTextColorR);
	GetUInt32Value(ini, "Colors.Advanced", "NameTextColorG", Config::AmmoWheel::NameTextColorG);
	GetUInt32Value(ini, "Colors.Advanced", "NameTextColorB", Config::AmmoWheel::NameTextColorB);
	GetUInt32Value(ini, "Colors.Advanced", "NameTextColorA", Config::AmmoWheel::NameTextColorA);
	GetUInt32Value(ini, "Colors.Advanced", "LowAmmoColorR", Config::AmmoWheel::LowAmmoColorR);
	GetUInt32Value(ini, "Colors.Advanced", "LowAmmoColorG", Config::AmmoWheel::LowAmmoColorG);
	GetUInt32Value(ini, "Colors.Advanced", "LowAmmoColorB", Config::AmmoWheel::LowAmmoColorB);
	GetUInt32Value(ini, "Colors.Advanced", "LowAmmoColorA", Config::AmmoWheel::LowAmmoColorA);
	GetUInt32Value(ini, "Colors.Advanced", "SelectedColorR", Config::AmmoWheel::SelectedColorR);
	GetUInt32Value(ini, "Colors.Advanced", "SelectedColorG", Config::AmmoWheel::SelectedColorG);
	GetUInt32Value(ini, "Colors.Advanced", "SelectedColorB", Config::AmmoWheel::SelectedColorB);
	GetUInt32Value(ini, "Colors.Advanced", "SelectedColorA", Config::AmmoWheel::SelectedColorA);
	GetUInt32Value(ini, "Colors.Advanced", "HoverColorR", Config::AmmoWheel::HoverColorR);
	GetUInt32Value(ini, "Colors.Advanced", "HoverColorG", Config::AmmoWheel::HoverColorG);
	GetUInt32Value(ini, "Colors.Advanced", "HoverColorB", Config::AmmoWheel::HoverColorB);
	GetUInt32Value(ini, "Colors.Advanced", "HoverColorA", Config::AmmoWheel::HoverColorA);

	// Compute final colors from presets + opacity (or custom RGBA if preset == 0)
	// NOTE: Legacy [Colors] section uses the old palette (White, Black, Red, Green, Blue...)
	{
		uint32_t r = Config::AmmoWheel::NameTextColorR;
		uint32_t g = Config::AmmoWheel::NameTextColorG;
		uint32_t b = Config::AmmoWheel::NameTextColorB;
		applyPresetRGB(kLegacyPalette, kLegacyPaletteSize, Config::AmmoWheel::NameTextColorPreset, r, g, b);
		Config::AmmoWheel::NameTextColor = toImU32(r, g, b, Config::AmmoWheel::NameTextOpacity);
	}
	{
		uint32_t r = Config::AmmoWheel::LowAmmoColorR;
		uint32_t g = Config::AmmoWheel::LowAmmoColorG;
		uint32_t b = Config::AmmoWheel::LowAmmoColorB;
		applyPresetRGB(kLegacyPalette, kLegacyPaletteSize, Config::AmmoWheel::LowAmmoColorPreset, r, g, b);
		Config::AmmoWheel::LowAmmoIndicatorColor = toImU32(r, g, b, Config::AmmoWheel::LowAmmoOpacity);
	}
	{
		uint32_t r = Config::AmmoWheel::SelectedColorR;
		uint32_t g = Config::AmmoWheel::SelectedColorG;
		uint32_t b = Config::AmmoWheel::SelectedColorB;
		applyPresetRGB(kLegacyPalette, kLegacyPaletteSize, Config::AmmoWheel::SelectedColorPreset, r, g, b);
		Config::AmmoWheel::SelectedIndicatorColor = toImU32(r, g, b, Config::AmmoWheel::SelectedOpacity);
	}
	{
		uint32_t r = Config::AmmoWheel::HoverColorR;
		uint32_t g = Config::AmmoWheel::HoverColorG;
		uint32_t b = Config::AmmoWheel::HoverColorB;
		applyPresetRGB(kLegacyPalette, kLegacyPaletteSize, Config::AmmoWheel::HoverColorPreset, r, g, b);
		Config::AmmoWheel::HoverHighlightColor = toImU32(r, g, b, Config::AmmoWheel::HoverOpacity);
	}

	// ========== TASK 1: COLOR OVERRIDE SYSTEM ==========
	// [ColorOverrides] - Slot label text color override
	GetBoolValue(ini, "ColorOverrides", "SlotLabelColorOverrideEnabled", Config::AmmoWheel::SlotLabelColorOverrideEnabled);
	GetUInt32Value(ini, "ColorOverrides", "SlotLabelColorPreset", Config::AmmoWheel::SlotLabelColorPreset);
	if (!readPackedColor("ColorOverrides", "SlotLabelColor",
		Config::AmmoWheel::SlotLabelColorR,
		Config::AmmoWheel::SlotLabelColorG,
		Config::AmmoWheel::SlotLabelColorB,
		Config::AmmoWheel::SlotLabelColorA)) {
		GetUInt32Value(ini, "ColorOverrides", "SlotLabelColorR", Config::AmmoWheel::SlotLabelColorR);
		GetUInt32Value(ini, "ColorOverrides", "SlotLabelColorG", Config::AmmoWheel::SlotLabelColorG);
		GetUInt32Value(ini, "ColorOverrides", "SlotLabelColorB", Config::AmmoWheel::SlotLabelColorB);
		GetUInt32Value(ini, "ColorOverrides", "SlotLabelColorA", Config::AmmoWheel::SlotLabelColorA);
	}
	GetFloatValue(ini, "ColorOverrides", "SlotLabelColorOpacity", Config::AmmoWheel::SlotLabelColorOpacity);
	{
		uint32_t r = Config::AmmoWheel::SlotLabelColorR;
		uint32_t g = Config::AmmoWheel::SlotLabelColorG;
		uint32_t b = Config::AmmoWheel::SlotLabelColorB;
		uint32_t a = Config::AmmoWheel::SlotLabelColorA;
		applyPresetRGB(kSkyrimPalette, kSkyrimPaletteSize, Config::AmmoWheel::SlotLabelColorPreset, r, g, b);
		a = static_cast<uint32_t>(a * Config::AmmoWheel::SlotLabelColorOpacity);
		Config::AmmoWheel::SlotLabelColorComputed = toImU32(r, g, b, a);
	}

	// [ColorOverrides] - Arrow label (active slot name) override
	GetBoolValue(ini, "ColorOverrides", "ArrowLabelColorOverrideEnabled", Config::AmmoWheel::ArrowLabelColorOverrideEnabled);
	GetUInt32Value(ini, "ColorOverrides", "ArrowLabelColorPreset", Config::AmmoWheel::ArrowLabelColorPreset);
	if (!readPackedColor("ColorOverrides", "ArrowLabelColor",
		Config::AmmoWheel::ArrowLabelColorR,
		Config::AmmoWheel::ArrowLabelColorG,
		Config::AmmoWheel::ArrowLabelColorB,
		Config::AmmoWheel::ArrowLabelColorA)) {
		GetUInt32Value(ini, "ColorOverrides", "ArrowLabelColorR", Config::AmmoWheel::ArrowLabelColorR);
		GetUInt32Value(ini, "ColorOverrides", "ArrowLabelColorG", Config::AmmoWheel::ArrowLabelColorG);
		GetUInt32Value(ini, "ColorOverrides", "ArrowLabelColorB", Config::AmmoWheel::ArrowLabelColorB);
		GetUInt32Value(ini, "ColorOverrides", "ArrowLabelColorA", Config::AmmoWheel::ArrowLabelColorA);
	}
	GetFloatValue(ini, "ColorOverrides", "ArrowLabelColorOpacity", Config::AmmoWheel::ArrowLabelColorOpacity);
	{
		uint32_t r = Config::AmmoWheel::ArrowLabelColorR;
		uint32_t g = Config::AmmoWheel::ArrowLabelColorG;
		uint32_t b = Config::AmmoWheel::ArrowLabelColorB;
		uint32_t a = Config::AmmoWheel::ArrowLabelColorA;
		applyPresetRGB(kSkyrimPalette, kSkyrimPaletteSize, Config::AmmoWheel::ArrowLabelColorPreset, r, g, b);
		a = static_cast<uint32_t>(a * Config::AmmoWheel::ArrowLabelColorOpacity);
		Config::AmmoWheel::ArrowLabelColorComputed = toImU32(r, g, b, a);
	}
	
	// [ColorOverrides] - Border color override
	GetBoolValue(ini, "ColorOverrides", "BorderColorOverrideEnabled", Config::AmmoWheel::BorderColorOverrideEnabled);
	GetUInt32Value(ini, "ColorOverrides", "BorderColorPreset", Config::AmmoWheel::BorderColorPreset);
	if (!readPackedColor("ColorOverrides", "BorderColor",
		Config::AmmoWheel::BorderColorR,
		Config::AmmoWheel::BorderColorG,
		Config::AmmoWheel::BorderColorB,
		Config::AmmoWheel::BorderColorA)) {
		GetUInt32Value(ini, "ColorOverrides", "BorderColorR", Config::AmmoWheel::BorderColorR);
		GetUInt32Value(ini, "ColorOverrides", "BorderColorG", Config::AmmoWheel::BorderColorG);
		GetUInt32Value(ini, "ColorOverrides", "BorderColorB", Config::AmmoWheel::BorderColorB);
		GetUInt32Value(ini, "ColorOverrides", "BorderColorA", Config::AmmoWheel::BorderColorA);
	}
	GetFloatValue(ini, "ColorOverrides", "BorderColorOpacity", Config::AmmoWheel::BorderColorOpacity);
	{
		uint32_t r = Config::AmmoWheel::BorderColorR;
		uint32_t g = Config::AmmoWheel::BorderColorG;
		uint32_t b = Config::AmmoWheel::BorderColorB;
		uint32_t a = Config::AmmoWheel::BorderColorA;
		applyPresetRGB(kSkyrimPalette, kSkyrimPaletteSize, Config::AmmoWheel::BorderColorPreset, r, g, b);
		a = static_cast<uint32_t>(a * Config::AmmoWheel::BorderColorOpacity);
		Config::AmmoWheel::BorderColorComputed = toImU32(r, g, b, a);
	}
	
	// [ColorOverrides] - Center panel label color override (non-damage)
	GetBoolValue(ini, "ColorOverrides", "CenterLabelColorOverrideEnabled", Config::AmmoWheel::CenterLabelColorOverrideEnabled);
	GetUInt32Value(ini, "ColorOverrides", "CenterLabelColorPreset", Config::AmmoWheel::CenterLabelColorPreset);
	if (!readPackedColor("ColorOverrides", "CenterLabelColor",
		Config::AmmoWheel::CenterLabelColorR,
		Config::AmmoWheel::CenterLabelColorG,
		Config::AmmoWheel::CenterLabelColorB,
		Config::AmmoWheel::CenterLabelColorA)) {
		GetUInt32Value(ini, "ColorOverrides", "CenterLabelColorR", Config::AmmoWheel::CenterLabelColorR);
		GetUInt32Value(ini, "ColorOverrides", "CenterLabelColorG", Config::AmmoWheel::CenterLabelColorG);
		GetUInt32Value(ini, "ColorOverrides", "CenterLabelColorB", Config::AmmoWheel::CenterLabelColorB);
		GetUInt32Value(ini, "ColorOverrides", "CenterLabelColorA", Config::AmmoWheel::CenterLabelColorA);
	}
	GetFloatValue(ini, "ColorOverrides", "CenterLabelColorOpacity", Config::AmmoWheel::CenterLabelColorOpacity);
	{
		uint32_t r = Config::AmmoWheel::CenterLabelColorR;
		uint32_t g = Config::AmmoWheel::CenterLabelColorG;
		uint32_t b = Config::AmmoWheel::CenterLabelColorB;
		uint32_t a = Config::AmmoWheel::CenterLabelColorA;
		applyPresetRGB(kSkyrimPalette, kSkyrimPaletteSize, Config::AmmoWheel::CenterLabelColorPreset, r, g, b);
		a = static_cast<uint32_t>(a * Config::AmmoWheel::CenterLabelColorOpacity);
		Config::AmmoWheel::CenterLabelColorComputed = toImU32(r, g, b, a);
	}
	
	// ========== TASK 2: BOLD LABEL FORMATTING ==========
	GetBoolValue(ini, "Labels", "NameBoldEnabled", Config::AmmoWheel::NameBoldEnabled);
	{
		uint32_t mode = static_cast<uint32_t>(Config::AmmoWheel::NameBoldMode);
		GetUInt32Value(ini, "Labels", "NameBoldMode", mode);
		Config::AmmoWheel::NameBoldMode = std::clamp(static_cast<int>(mode), 0, 1);
	}
	GetFloatValue(ini, "Labels", "NameBoldStrengthPx", Config::AmmoWheel::NameBoldStrengthPx);
	Config::AmmoWheel::NameBoldStrengthPx = std::clamp(Config::AmmoWheel::NameBoldStrengthPx, 0.3f, 3.0f);
	
	// ========== TASK 3: INDICATOR REDESIGN ==========
	GetBoolValue(ini, "Indicators", "HoverBrightnessEnabled", Config::AmmoWheel::HoverBrightnessEnabled);
	GetFloatValue(ini, "Indicators", "HoverBrightnessStrength", Config::AmmoWheel::HoverBrightnessStrength);
	Config::AmmoWheel::HoverBrightnessStrength = std::clamp(Config::AmmoWheel::HoverBrightnessStrength, 1.0f, 2.0f);
	
	GetBoolValue(ini, "Indicators", "SelectedBlinkEnabled", Config::AmmoWheel::SelectedBlinkEnabled);
	GetFloatValue(ini, "Indicators", "SelectedBlinkSpeedHz", Config::AmmoWheel::SelectedBlinkSpeedHz);
	Config::AmmoWheel::SelectedBlinkSpeedHz = std::clamp(Config::AmmoWheel::SelectedBlinkSpeedHz, 0.5f, 10.0f);
	GetFloatValue(ini, "Indicators", "SelectedBlinkMinAlpha", Config::AmmoWheel::SelectedBlinkMinAlpha);
	GetFloatValue(ini, "Indicators", "SelectedBlinkMaxAlpha", Config::AmmoWheel::SelectedBlinkMaxAlpha);
	GetUInt32Value(ini, "Indicators", "SelectedIndicatorColorPreset", Config::AmmoWheel::SelectedIndicatorColorPreset);
	if (!readPackedColor("Indicators", "SelectedIndicatorColor",
		Config::AmmoWheel::SelectedIndicatorColorR,
		Config::AmmoWheel::SelectedIndicatorColorG,
		Config::AmmoWheel::SelectedIndicatorColorB,
		Config::AmmoWheel::SelectedIndicatorColorA)) {
		GetUInt32Value(ini, "Indicators", "SelectedIndicatorColorR", Config::AmmoWheel::SelectedIndicatorColorR);
		GetUInt32Value(ini, "Indicators", "SelectedIndicatorColorG", Config::AmmoWheel::SelectedIndicatorColorG);
		GetUInt32Value(ini, "Indicators", "SelectedIndicatorColorB", Config::AmmoWheel::SelectedIndicatorColorB);
		GetUInt32Value(ini, "Indicators", "SelectedIndicatorColorA", Config::AmmoWheel::SelectedIndicatorColorA);
	}
	{
		uint32_t r = Config::AmmoWheel::SelectedIndicatorColorR;
		uint32_t g = Config::AmmoWheel::SelectedIndicatorColorG;
		uint32_t b = Config::AmmoWheel::SelectedIndicatorColorB;
		uint32_t a = Config::AmmoWheel::SelectedIndicatorColorA;
		applyPresetRGB(kSkyrimPalette, kSkyrimPaletteSize, Config::AmmoWheel::SelectedIndicatorColorPreset, r, g, b);
		Config::AmmoWheel::SelectedIndicatorColorComputed = toImU32(r, g, b, a);
	}
	GetFloatValue(ini, "Indicators", "SelectedIndicatorSizeScale", Config::AmmoWheel::SelectedIndicatorSizeScale);
	Config::AmmoWheel::SelectedIndicatorSizeScale = std::clamp(Config::AmmoWheel::SelectedIndicatorSizeScale, 0.1f, 8.0f);
	GetFloatValue(ini, "Indicators", "SelectedSlotBlinkStrength", Config::AmmoWheel::SelectedSlotBlinkStrength);
	Config::AmmoWheel::SelectedSlotBlinkStrength = std::clamp(Config::AmmoWheel::SelectedSlotBlinkStrength, 0.0f, 1.0f);
	
	// ========== TASK 4: POPUP FLIPBOOK TOGGLE ==========
	GetBoolValue(ini, "Popup", "PopupFlipbookEnabled", Config::AmmoWheel::PopupFlipbookEnabled);

	// [VisualPolish] - background, border, shadows, text effects
	GetBoolValue(ini, "VisualPolish", "BackgroundEnabled", Config::AmmoWheel::BackgroundEnabled);
	GetFloatValue(ini, "VisualPolish", "BackgroundOpacity", Config::AmmoWheel::BackgroundOpacity);
	GetFloatValue(ini, "VisualPolish", "BackgroundRadiusScale", Config::AmmoWheel::BackgroundRadiusScale);
	GetFloatValue(ini, "VisualPolish", "BackgroundSoftEdgeRatio", Config::AmmoWheel::BackgroundSoftEdgeRatio);
	Config::AmmoWheel::BackgroundSoftEdgeRatio = std::clamp(Config::AmmoWheel::BackgroundSoftEdgeRatio, 0.0f, 0.95f);
	
	GetBoolValue(ini, "VisualPolish", "BorderEnabled", Config::AmmoWheel::BorderEnabled);
	GetFloatValue(ini, "VisualPolish", "BorderInnerScale", Config::AmmoWheel::BorderInnerScale);
	GetFloatValue(ini, "VisualPolish", "BorderOuterScale", Config::AmmoWheel::BorderOuterScale);
	{
		uint32_t r = 184, g = 134, b = 11, a = 200;
		GetUInt32Value(ini, "VisualPolish", "BorderColorInnerR", r);
		GetUInt32Value(ini, "VisualPolish", "BorderColorInnerG", g);
		GetUInt32Value(ini, "VisualPolish", "BorderColorInnerB", b);
		GetUInt32Value(ini, "VisualPolish", "BorderColorInnerA", a);
		Config::AmmoWheel::BorderColorInner = IM_COL32(r, g, b, a);
	}
	{
		uint32_t r = 139, g = 115, b = 85, a = 150;
		GetUInt32Value(ini, "VisualPolish", "BorderColorOuterR", r);
		GetUInt32Value(ini, "VisualPolish", "BorderColorOuterG", g);
		GetUInt32Value(ini, "VisualPolish", "BorderColorOuterB", b);
		GetUInt32Value(ini, "VisualPolish", "BorderColorOuterA", a);
		Config::AmmoWheel::BorderColorOuter = IM_COL32(r, g, b, a);
	}
	
	GetBoolValue(ini, "VisualPolish", "SlotShadowEnabled", Config::AmmoWheel::SlotShadowEnabled);
	GetFloatValue(ini, "VisualPolish", "SlotShadowOffsetX", Config::AmmoWheel::SlotShadowOffsetX);
	GetFloatValue(ini, "VisualPolish", "SlotShadowOffsetY", Config::AmmoWheel::SlotShadowOffsetY);
	GetUInt32Value(ini, "VisualPolish", "SlotShadowAlpha", Config::AmmoWheel::SlotShadowAlpha);
	
	GetBoolValue(ini, "VisualPolish", "SlotHighlightEnabled", Config::AmmoWheel::SlotHighlightEnabled);
	GetFloatValue(ini, "VisualPolish", "SlotHighlightThickness", Config::AmmoWheel::SlotHighlightThickness);
	GetUInt32Value(ini, "VisualPolish", "SlotHighlightAlpha", Config::AmmoWheel::SlotHighlightAlpha);
	GetBoolValue(ini, "VisualPolish", "SlotBackgroundShadeEnabled", Config::AmmoWheel::SlotBackgroundShadeEnabled);
	GetFloatValue(ini, "VisualPolish", "SlotBackgroundShadeOpacity", Config::AmmoWheel::SlotBackgroundShadeOpacity);
	Config::AmmoWheel::SlotBackgroundShadeOpacity = std::clamp(Config::AmmoWheel::SlotBackgroundShadeOpacity, 0.0f, 1.0f);
	
	GetBoolValue(ini, "VisualPolish", "TextShadowEnabled", Config::AmmoWheel::TextShadowEnabled);
	GetUInt32Value(ini, "VisualPolish", "TextShadowLayers", Config::AmmoWheel::TextShadowLayers);
	GetUInt32Value(ini, "VisualPolish", "TextShadowAlpha", Config::AmmoWheel::TextShadowAlpha);
	GetFloatValue(ini, "VisualPolish", "TextShadowOffset", Config::AmmoWheel::TextShadowOffset);
	
	GetBoolValue(ini, "VisualPolish", "TextHoverGlowEnabled", Config::AmmoWheel::TextHoverGlowEnabled);
	{
		uint32_t r = 255, g = 215, b = 0, a = 120;
		GetUInt32Value(ini, "VisualPolish", "TextHoverGlowColorR", r);
		GetUInt32Value(ini, "VisualPolish", "TextHoverGlowColorG", g);
		GetUInt32Value(ini, "VisualPolish", "TextHoverGlowColorB", b);
		GetUInt32Value(ini, "VisualPolish", "TextHoverGlowColorA", a);
		Config::AmmoWheel::TextHoverGlowColor = IM_COL32(r, g, b, a);
	}

	// [Animations] - hover pulse, slot dividers, center panel decoration
	GetBoolValue(ini, "Animations", "HoverPulseEnabled", Config::AmmoWheel::HoverPulseEnabled);
	GetFloatValue(ini, "Animations", "HoverPulseSpeed", Config::AmmoWheel::HoverPulseSpeed);
	GetFloatValue(ini, "Animations", "HoverPulseSize", Config::AmmoWheel::HoverPulseSize);
	{
		uint32_t r = 255, g = 215, b = 0, a = 120;
		GetUInt32Value(ini, "Animations", "HoverPulseColorR", r);
		GetUInt32Value(ini, "Animations", "HoverPulseColorG", g);
		GetUInt32Value(ini, "Animations", "HoverPulseColorB", b);
		GetUInt32Value(ini, "Animations", "HoverPulseColorA", a);
		Config::AmmoWheel::HoverPulseColor = IM_COL32(r, g, b, a);
	}
	
	GetBoolValue(ini, "Animations", "SlotDividersEnabled", Config::AmmoWheel::SlotDividersEnabled);
	GetBoolValue(ini, "Animations", "SlotDividerReskinBreathingEnabled", Config::AmmoWheel::SlotDividerReskinBreathingEnabled);
	GetFloatValue(ini, "Animations", "SlotDividerReskinBreathingSpeed", Config::AmmoWheel::SlotDividerReskinBreathingSpeed);
	GetFloatValue(ini, "Animations", "SlotDividerReskinBreathingIntensity", Config::AmmoWheel::SlotDividerReskinBreathingIntensity);
	GetFloatValue(ini, "Animations", "SlotDividerReskinBreathingOpacity", Config::AmmoWheel::SlotDividerReskinBreathingOpacity);
	Config::AmmoWheel::SlotDividerReskinBreathingSpeed = std::clamp(Config::AmmoWheel::SlotDividerReskinBreathingSpeed, 0.1f, 12.0f);
	Config::AmmoWheel::SlotDividerReskinBreathingIntensity = std::clamp(Config::AmmoWheel::SlotDividerReskinBreathingIntensity, 0.0f, 1.0f);
	Config::AmmoWheel::SlotDividerReskinBreathingOpacity = std::clamp(Config::AmmoWheel::SlotDividerReskinBreathingOpacity, 0.0f, 1.0f);
	GetFloatValue(ini, "Animations", "SlotDividerThickness", Config::AmmoWheel::SlotDividerThickness);
	{
		uint32_t r = 139, g = 69, b = 19, a = 200;
		GetUInt32Value(ini, "Animations", "SlotDividerColorR", r);
		GetUInt32Value(ini, "Animations", "SlotDividerColorG", g);
		GetUInt32Value(ini, "Animations", "SlotDividerColorB", b);
		GetUInt32Value(ini, "Animations", "SlotDividerColorA", a);
		Config::AmmoWheel::SlotDividerColor = IM_COL32(r, g, b, a);
	}
	
	GetBoolValue(ini, "Animations", "CenterFrameEnabled", Config::AmmoWheel::CenterFrameEnabled);
	GetBoolValue(ini, "Animations", "CenterFramePulse", Config::AmmoWheel::CenterFramePulse);
	GetFloatValue(ini, "Animations", "CenterFramePulseSpeed", Config::AmmoWheel::CenterFramePulseSpeed);
	{
		uint32_t r = 139, g = 69, b = 19, a = 150;
		GetUInt32Value(ini, "Animations", "CenterFrameColorR", r);
		GetUInt32Value(ini, "Animations", "CenterFrameColorG", g);
		GetUInt32Value(ini, "Animations", "CenterFrameColorB", b);
		GetUInt32Value(ini, "Animations", "CenterFrameColorA", a);
		Config::AmmoWheel::CenterFrameColor = IM_COL32(r, g, b, a);
	}
	GetBoolValue(ini, "Animations", "CenterCornersEnabled", Config::AmmoWheel::CenterCornersEnabled);
	GetFloatValue(ini, "Animations", "CenterCornerSize", Config::AmmoWheel::CenterCornerSize);

	// [Indicators] - low ammo, selected, hover
	GetBoolValue(ini, "Indicators", "LowAmmoIndicatorEnabled", Config::AmmoWheel::LowAmmoIndicatorEnabled);
	{
		uint32_t threshold = static_cast<uint32_t>(Config::AmmoWheel::LowAmmoThreshold);
		if (GetUInt32Value(ini, "Indicators", "LowAmmoThreshold", threshold)) {
			Config::AmmoWheel::LowAmmoThreshold = static_cast<int>(threshold);
		}
	}
	GetFloatValue(ini, "Indicators", "LowAmmoIndicatorThickness", Config::AmmoWheel::LowAmmoIndicatorThickness);
	GetFloatValue(ini, "Indicators", "LowAmmoIndicatorRadiusRatio", Config::AmmoWheel::LowAmmoIndicatorRadiusRatio);
	GetFloatValue(ini, "Indicators", "LowAmmoIndicatorAngularOffsetDeg", Config::AmmoWheel::LowAmmoIndicatorAngularOffsetDeg);
	GetFloatValue(ini, "Indicators", "LowAmmoIndicatorRadialOffsetPx", Config::AmmoWheel::LowAmmoIndicatorRadialOffsetPx);
	{
		uint32_t drawLayer = static_cast<uint32_t>(Config::AmmoWheel::LowAmmoIndicatorDrawLayer);
		if (GetUInt32Value(ini, "Indicators", "LowAmmoIndicatorDrawLayer", drawLayer)) {
			Config::AmmoWheel::LowAmmoIndicatorDrawLayer = std::clamp(static_cast<int>(drawLayer), 0, 2);
		}
	}
	GetFloatValue(ini, "Indicators", "SelectedIndicatorThickness", Config::AmmoWheel::SelectedIndicatorThickness);

	// [Input]
	GetUInt32Value(ini, "Input", "ToggleKeyMKB", Config::AmmoWheel::MKB::toggleAmmoWheel);
	GetUInt32Value(ini, "Input", "ToggleMouseButton", Config::AmmoWheel::MKB::toggleAmmoWheelMouse);
	GetUInt32Value(ini, "Input", "ModifierKeyMKB", Config::AmmoWheel::MKB::modifierKey);
	GetUInt32Value(ini, "Input", "ToggleKeyGamepad", Config::AmmoWheel::GamePad::toggleAmmoWheel);
	GetUInt32Value(ini, "Input", "ModifierButtonGamepad", Config::AmmoWheel::GamePad::modifierButton);
	const bool hasAmmoMkbName = GetStringValue(ini, "Input", "ToggleKeyMKBName", Config::AmmoWheel::ToggleKeyMKBName);
	const bool hasAmmoGamepadName = GetStringValue(ini, "Input", "ToggleKeyGamepadName", Config::AmmoWheel::ToggleKeyGamepadName);
	if (!hasAmmoMkbName || Config::AmmoWheel::ToggleKeyMKBName.empty()) {
		Config::AmmoWheel::ToggleKeyMKBName = Controls::GetKeyNameForMkb(Config::AmmoWheel::MKB::toggleAmmoWheel);
	}
	if (!hasAmmoGamepadName || Config::AmmoWheel::ToggleKeyGamepadName.empty()) {
		Config::AmmoWheel::ToggleKeyGamepadName = Controls::GetKeyNameForGamepad(Config::AmmoWheel::GamePad::toggleAmmoWheel);
	}
	// Modifier and mouse toggle name strings (new)
	const bool hasModMkbName = GetStringValue(ini, "Input", "ModifierKeyMKBName", Config::AmmoWheel::ModifierKeyMKBName);
	const bool hasModGamepadName = GetStringValue(ini, "Input", "ModifierButtonGamepadName", Config::AmmoWheel::ModifierButtonGamepadName);
	const bool hasMouseName = GetStringValue(ini, "Input", "ToggleMouseButtonName", Config::AmmoWheel::ToggleMouseButtonName);
	// Generate names from DIK/gamepad codes if not present
	if (!hasModMkbName || Config::AmmoWheel::ModifierKeyMKBName.empty()) {
		if (Config::AmmoWheel::MKB::modifierKey == 0) {
			Config::AmmoWheel::ModifierKeyMKBName = "None";
		} else {
			Config::AmmoWheel::ModifierKeyMKBName = Controls::GetKeyNameForMkb(Config::AmmoWheel::MKB::modifierKey);
		}
	}
	if (!hasModGamepadName || Config::AmmoWheel::ModifierButtonGamepadName.empty()) {
		if (Config::AmmoWheel::GamePad::modifierButton == 0) {
			Config::AmmoWheel::ModifierButtonGamepadName = "None";
		} else {
			Config::AmmoWheel::ModifierButtonGamepadName = Controls::GetKeyNameForGamepad(Config::AmmoWheel::GamePad::modifierButton);
		}
	}
	if (!hasMouseName || Config::AmmoWheel::ToggleMouseButtonName.empty()) {
		if (Config::AmmoWheel::MKB::toggleAmmoWheelMouse == 0) {
			Config::AmmoWheel::ToggleMouseButtonName = "None";
		} else {
			Config::AmmoWheel::ToggleMouseButtonName = Controls::GetKeyNameForMkb(Config::AmmoWheel::MKB::toggleAmmoWheelMouse);
		}
	}


	// [InputSafeguards]
	GetBoolValue(ini, "InputSafeguards", "MenuHoldToOpenEnabled", Config::AmmoWheel::InputSafeguards::MenuHoldToOpenEnabled);
	GetFloatValue(ini, "InputSafeguards", "MenuHoldToOpenSeconds", Config::AmmoWheel::InputSafeguards::MenuHoldToOpenSeconds);
	Config::AmmoWheel::InputSafeguards::MenuHoldToOpenSeconds =
		std::clamp(Config::AmmoWheel::InputSafeguards::MenuHoldToOpenSeconds, 0.10f, 2.0f);

	// [Sounds] - AmmoWheel hover sound settings
	GetBoolValue(ini, "Sounds", "EnableHoverSlotSound", Config::AmmoWheel::Sounds::EnableHoverSlotSound);
	GetStringValue(ini, "Sounds", "HoverSlotSoundEditorID", Config::AmmoWheel::Sounds::HoverSlotSoundEditorID);
	GetFloatValue(ini, "Sounds", "HoverSlotSoundVolume", Config::AmmoWheel::Sounds::HoverSlotSoundVolume);
	{
		uint32_t cooldown = Config::AmmoWheel::Sounds::HoverSlotSoundCooldownMs;
		if (GetUInt32Value(ini, "Sounds", "HoverSlotSoundCooldownMs", cooldown)) {
			Config::AmmoWheel::Sounds::HoverSlotSoundCooldownMs = std::clamp(cooldown, 0u, 500u);
		}
	}
	GetBoolValue(ini, "Sounds", "HoverSlotSoundOnlyOnSlotChange", Config::AmmoWheel::Sounds::HoverSlotSoundOnlyOnSlotChange);
	GetBoolValue(ini, "Sounds", "HoverSlotSoundIgnoreWhileFiring", Config::AmmoWheel::Sounds::HoverSlotSoundIgnoreWhileFiring);
	GetBoolValue(ini, "Sounds", "DebugLogHoverSound", Config::AmmoWheel::Sounds::DebugLogHoverSound);
	
	// Clamp volume to safe range
	Config::AmmoWheel::Sounds::HoverSlotSoundVolume = std::clamp(Config::AmmoWheel::Sounds::HoverSlotSoundVolume, 0.0f, 3.0f);
	
	// Log sound settings if debug enabled
	if (Config::AmmoWheel::Sounds::DebugLogHoverSound) {
		logger::info("  [Sounds] EnableHoverSlotSound={}, EditorID='{}', Volume={:.2f}, CooldownMs={}, OnlyOnSlotChange={}",
			Config::AmmoWheel::Sounds::EnableHoverSlotSound,
			Config::AmmoWheel::Sounds::HoverSlotSoundEditorID.empty() ? "(use main wheel)" : Config::AmmoWheel::Sounds::HoverSlotSoundEditorID,
			Config::AmmoWheel::Sounds::HoverSlotSoundVolume,
			Config::AmmoWheel::Sounds::HoverSlotSoundCooldownMs,
			Config::AmmoWheel::Sounds::HoverSlotSoundOnlyOnSlotChange);
	}

	// Apply performance tier overrides after all config values are loaded.
	ApplyAmmoWheelPerformanceTier();

	// Log all settings for debugging
	logger::info("AmmoWheel config loaded:");
	logger::info("  [Position] Anchor={}, PosX={:.0f}, PosY={:.0f}, Radius={:.0f}",
		Config::AmmoWheel::ScreenAnchorIndex, Config::AmmoWheel::PositionX, 
		Config::AmmoWheel::PositionY, Config::AmmoWheel::WheelRadius);
	logger::info("  [Geometry] InnerRatio={:.2f}, SlotGap={:.1f}, IconSize={:.0f}",
		Config::AmmoWheel::InnerRadiusRatio, Config::AmmoWheel::SlotGapDeg, Config::AmmoWheel::IconSizePx);
	logger::info("  [Center] Enabled={}, BgEnabled={}, BgOpacity={:.2f}, MaxWidth={:.2f}",
		Config::AmmoWheel::CenterEnabled, Config::AmmoWheel::CenterBgEnabled,
		Config::AmmoWheel::CenterBgOpacity, Config::AmmoWheel::CenterMaxWidthRatio);
	logger::info("  [Text] NameFontPx={:.0f}, CountFontPx={:.0f}, CenterFontPx={:.0f}, Scale={:.2f}",
		Config::AmmoWheel::NameFontPx, Config::AmmoWheel::CountFontPx,
		Config::AmmoWheel::CenterFontPx, Config::AmmoWheel::NameTextScale);
	logger::info("  [Colors] NamePreset={}, LowPreset={}, SelectedPreset={}, HoverPreset={}",
		Config::AmmoWheel::NameTextColorPreset, Config::AmmoWheel::LowAmmoColorPreset,
		Config::AmmoWheel::SelectedColorPreset, Config::AmmoWheel::HoverColorPreset);
	logger::info("  [TimeSlow] Enabled={}, SlowTimeScale={:.2f}",
		Config::AmmoWheel::TimeSlowEnabled, Config::AmmoWheel::TimeSlowScale);
	logger::info("  [Performance] Tier={}, Overrides=(visual={}, anim={}, popup={}, labels={}, center={}, indicators={}), InvSnapshotInterval={:.2f}s",
		Config::AmmoWheel::Performance::Tier,
		Config::AmmoWheel::Performance::OverrideVisualPolish,
		Config::AmmoWheel::Performance::OverrideAnimations,
		Config::AmmoWheel::Performance::OverridePopup,
		Config::AmmoWheel::Performance::OverrideLabels,
		Config::AmmoWheel::Performance::OverrideCenterPanel,
		Config::AmmoWheel::Performance::OverrideIndicators,
		Config::AmmoWheel::Performance::InventorySnapshotIntervalSeconds);
	logger::info("  [Indicators] HoverBrightness={} (str={:.2f}), SelectedBlink={} (speed={:.1f}Hz, alpha={:.2f}-{:.2f})",
		Config::AmmoWheel::HoverBrightnessEnabled, Config::AmmoWheel::HoverBrightnessStrength,
		Config::AmmoWheel::SelectedBlinkEnabled, Config::AmmoWheel::SelectedBlinkSpeedHz,
		Config::AmmoWheel::SelectedBlinkMinAlpha, Config::AmmoWheel::SelectedBlinkMaxAlpha);
	logger::info("  [ColorOverrides] SlotLabel={} (preset={}), ArrowLabel={} (preset={}), Border={} (preset={}), CenterLabel={} (preset={})",
		Config::AmmoWheel::SlotLabelColorOverrideEnabled, Config::AmmoWheel::SlotLabelColorPreset,
		Config::AmmoWheel::ArrowLabelColorOverrideEnabled, Config::AmmoWheel::ArrowLabelColorPreset,
		Config::AmmoWheel::BorderColorOverrideEnabled, Config::AmmoWheel::BorderColorPreset,
		Config::AmmoWheel::CenterLabelColorOverrideEnabled, Config::AmmoWheel::CenterLabelColorPreset);
	logger::info("  [Theme] Skin='{}', ResourceRoot='{}'",
		Config::AmmoWheel::SkinName, Config::AmmoWheel::ResourceRoot);
	logger::info("  [Input] KeyMKB={}, ModMKB={}, MouseBtn={}, Gamepad={}, ModGP={}",
		Config::AmmoWheel::MKB::toggleAmmoWheel, Config::AmmoWheel::MKB::modifierKey,
		Config::AmmoWheel::MKB::toggleAmmoWheelMouse,
		Config::AmmoWheel::GamePad::toggleAmmoWheel, Config::AmmoWheel::GamePad::modifierButton);

	// Capture base values for resolution-aware scaling (display-space).
	CaptureAmmoScaleBaseValues();
}

void Config::WriteAmmoWheelPresetOverrideIfActive()
{
	if (Config::AmmoWheel::ActivePreset <= 0) {
		return;
	}

	const std::string presetPath = fmt::format("{}\\{}\\AmmoWheel_Override.ini",
		Config::AmmoWheel::PresetBasePath, Config::AmmoWheel::ActivePreset);

	std::error_code ec;
	std::filesystem::path presetDir = std::filesystem::path(presetPath).parent_path();
	if (!presetDir.empty()) {
		std::filesystem::create_directories(presetDir, ec);
		if (ec) {
			logger::warn("AmmoWheel: Failed to create preset override directory '{}': {}",
				presetDir.string(), ec.message());
			return;
		}
	}

	CSimpleIniA presetIni;
	presetIni.SetUnicode();
	presetIni.LoadFile(presetPath.c_str());

	presetIni.SetBoolValue("Indicators", "LowAmmoIndicatorEnabled", Config::AmmoWheel::LowAmmoIndicatorEnabled);
	presetIni.SetLongValue("Indicators", "LowAmmoThreshold", Config::AmmoWheel::LowAmmoThreshold);
	presetIni.SetDoubleValue("Indicators", "LowAmmoIndicatorThickness", Config::AmmoWheel::LowAmmoIndicatorThickness);
	presetIni.SetDoubleValue("Indicators", "LowAmmoIndicatorRadiusRatio", Config::AmmoWheel::LowAmmoIndicatorRadiusRatio);
	presetIni.SetDoubleValue("Indicators", "LowAmmoIndicatorAngularOffsetDeg", Config::AmmoWheel::LowAmmoIndicatorAngularOffsetDeg);
	presetIni.SetDoubleValue("Indicators", "LowAmmoIndicatorRadialOffsetPx", Config::AmmoWheel::LowAmmoIndicatorRadialOffsetPx);
	presetIni.SetLongValue("Indicators", "LowAmmoIndicatorDrawLayer", Config::AmmoWheel::LowAmmoIndicatorDrawLayer);
	presetIni.SetBoolValue("TimeSlow", "Enabled", Config::AmmoWheel::TimeSlowEnabled);
	presetIni.SetDoubleValue("TimeSlow", "SlowTimeScale", Config::AmmoWheel::TimeSlowScale);

	if (presetIni.SaveFile(presetPath.c_str()) < 0) {
		logger::warn("AmmoWheel: Failed to save preset override to '{}'", presetPath);
	}
}

void Config::OffsetSizingToViewport()
{
	Config::MainWheel::LayoutScaling::UpdateRuntimeState();
	const auto& layoutState = Config::MainWheel::LayoutScaling::Runtime;

	const float baseScale = Config::WheelBehavior::GlobalScale;

	EnsureScaleEntries();
	if (!g_scaleBaseCaptured) {
		CaptureScaleBaseValues();
	}

	for (auto& entry : g_scaleEntries) {
		if (!entry.value) {
			continue;
		}
		float scaled = entry.base * baseScale;
		switch (entry.axis) {
		case ScaleAxis::X:
			scaled *= layoutState.CombinedX;
			break;
		case ScaleAxis::Y:
			scaled *= layoutState.CombinedY;
			break;
		case ScaleAxis::Uniform:
		default:
			scaled *= layoutState.CombinedU;
			break;
		}
		*entry.value = scaled;
	}
}

void Config::ResetScaleBaseCapture()
{
	g_scaleBaseCaptured = false;
	g_ammoScaleBaseCaptured = false;
}

static Config::AmmoWheel::StylePreset ScaleStylePreset(
	const Config::AmmoWheel::StylePreset& base,
	const ScaleSet& scale,
	float baseScale)
{
	auto scaleUniform = [&](float value) {
		return value * baseScale * scale.scaleU;
	};
	auto scaleX = [&](float value) {
		return value * baseScale * scale.scaleX;
	};
	auto scaleY = [&](float value) {
		return value * baseScale * scale.scaleY;
	};

	Config::AmmoWheel::StylePreset scaled = base;
	scaled.SlotInnerRadiusPadding = scaleUniform(base.SlotInnerRadiusPadding);
	scaled.SlotOuterRadiusPadding = scaleUniform(base.SlotOuterRadiusPadding);
	scaled.SlotCornerRounding = scaleUniform(base.SlotCornerRounding);
	scaled.BorderThickness = scaleUniform(base.BorderThickness);
	scaled.TextSize = scaleUniform(base.TextSize);
	scaled.TextShadowOffsetX = scaleX(base.TextShadowOffsetX);
	scaled.TextShadowOffsetY = scaleY(base.TextShadowOffsetY);
	scaled.IconPaddingPixels = scaleUniform(base.IconPaddingPixels);

	auto scaleIndicator = [&](Config::AmmoWheel::IndicatorPreset& out, const Config::AmmoWheel::IndicatorPreset& src) {
		out = src;
		out.ThicknessPx = scaleUniform(src.ThicknessPx);
		out.RadiusOffsetPx = scaleUniform(src.RadiusOffsetPx);
	};

	scaleIndicator(scaled.Selected, base.Selected);
	scaleIndicator(scaled.Hovered, base.Hovered);
	scaleIndicator(scaled.Active, base.Active);
	scaleIndicator(scaled.Charge, base.Charge);
	return scaled;
}

void Config::OffsetAmmoWheelSizingToViewport()
{
	Config::AmmoWheel::LayoutScaling::UpdateRuntimeState();
	const auto& layoutState = Config::AmmoWheel::LayoutScaling::Runtime;

	const float baseScale = 1.0f;

	EnsureAmmoScaleEntries();
	if (!g_ammoScaleBaseCaptured) {
		CaptureAmmoScaleBaseValues();
	}

	ScaleSet mismatchScale{ layoutState.Msx, layoutState.Msy, layoutState.Msu };
	ScaleSet geomScale = mismatchScale;
	ScaleSet textScale = mismatchScale;
	ScaleSet styleScale = mismatchScale;

	if (layoutState.LayoutActive && Config::AmmoWheel::LayoutScaling::ScaleGeometry) {
		geomScale.scaleX *= layoutState.Lsx;
		geomScale.scaleY *= layoutState.Lsy;
		geomScale.scaleU *= layoutState.Lsu;
	}
	if (layoutState.LayoutActive && Config::AmmoWheel::LayoutScaling::ScaleText) {
		textScale.scaleX *= layoutState.Lsx;
		textScale.scaleY *= layoutState.Lsy;
		textScale.scaleU *= layoutState.Lsu;
	}
	if (layoutState.LayoutActive && Config::AmmoWheel::LayoutScaling::ScaleStylePx) {
		styleScale.scaleX *= layoutState.Lsx;
		styleScale.scaleY *= layoutState.Lsy;
		styleScale.scaleU *= layoutState.Lsu;
	}

	for (auto& entry : g_ammoScaleEntries) {
		if (!entry.value) {
			continue;
		}
		const ScaleSet* scale = &geomScale;
		switch (entry.group) {
		case ScaleGroup::Text:
			scale = &textScale;
			break;
		case ScaleGroup::Style:
			scale = &styleScale;
			break;
		case ScaleGroup::Geometry:
		default:
			scale = &geomScale;
			break;
		}

		float scaled = entry.base * baseScale;
		switch (entry.axis) {
		case ScaleAxis::X:
			scaled *= scale->scaleX;
			break;
		case ScaleAxis::Y:
			scaled *= scale->scaleY;
			break;
		case ScaleAxis::Uniform:
		default:
			scaled *= scale->scaleU;
			break;
		}
		*entry.value = scaled;
	}

	if (g_ammoPresetBaseCaptured) {
		for (const auto& [presetId, preset] : g_ammoPresetBase) {
			Config::AmmoWheel::PresetSystem::LoadedPresets[presetId] = ScaleStylePreset(preset, styleScale, baseScale);
		}
		Config::AmmoWheel::PresetSystem::DefaultPreset = ScaleStylePreset(g_ammoDefaultPresetBase, styleScale, baseScale);
	}

	static float lastLoggedDisplayH = 0.0f;
	static float lastLoggedGameH = 0.0f;
	if (layoutState.MismatchActive && (layoutState.DisplayH != lastLoggedDisplayH || layoutState.GameH != lastLoggedGameH)) {
		logger::info("[ResolutionFix] AmmoWheel scaling applied at Config::OffsetAmmoWheelSizingToViewport (displayH={:.0f}, gameH={:.0f}, scaleX={:.3f}, scaleY={:.3f}, uniform={:.3f})",
			layoutState.DisplayH, layoutState.GameH, layoutState.Msx, layoutState.Msy, layoutState.Msu);
		lastLoggedDisplayH = layoutState.DisplayH;
		lastLoggedGameH = layoutState.GameH;
	}
}

