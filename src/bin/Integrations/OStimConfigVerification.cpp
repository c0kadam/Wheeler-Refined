#include "OStimConfigPolicy.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace
{
	bool Expect(bool a_condition, std::string_view a_case)
	{
		if (a_condition) {
			std::cout << "PASS " << a_case << '\n';
			return true;
		}
		std::cerr << "FAIL " << a_case << '\n';
		return false;
	}

	std::string ReadFile(const char* a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		return {
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>()
		};
	}

	std::string Trim(std::string a_value)
	{
		const auto first = std::find_if_not(a_value.begin(), a_value.end(), [](unsigned char a_char) {
			return std::isspace(a_char) != 0;
		});
		const auto last = std::find_if_not(a_value.rbegin(), a_value.rend(), [](unsigned char a_char) {
			return std::isspace(a_char) != 0;
		}).base();
		return first < last ? std::string(first, last) : std::string{};
	}

	std::map<std::string, std::string> ParseOStimDefaults(const std::string& a_text)
	{
		std::map<std::string, std::string> values;
		bool inSection = false;
		std::size_t start = 0;
		while (start <= a_text.size()) {
			const auto end = a_text.find('\n', start);
			std::string line = Trim(a_text.substr(start, end - start));
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (!line.empty() && line.front() != ';' && line.front() != '#') {
				if (line.front() == '[' && line.back() == ']') {
					inSection = line == "[OStimIntegration]";
				} else if (inSection) {
					const auto equals = line.find('=');
					if (equals != std::string::npos) {
						values.emplace(Trim(line.substr(0, equals)), Trim(line.substr(equals + 1)));
					}
				}
			}
			if (end == std::string::npos) {
				break;
			}
			start = end + 1;
		}
		return values;
	}

	std::set<std::string> CollectDMenuKeys(const nlohmann::json& a_root)
	{
		std::set<std::string> keys;
		for (const auto& group : a_root.at("data")) {
			if (!group.contains("entries")) {
				continue;
			}
			for (const auto& entry : group.at("entries")) {
				if (entry.contains("ini") && entry.at("ini").contains("id")) {
					keys.insert(entry.at("ini").at("id").get<std::string>());
				}
			}
		}
		return keys;
	}

	const nlohmann::json* FindDMenuEntry(const nlohmann::json& a_root, std::string_view a_key)
	{
		for (const auto& group : a_root.at("data")) {
			if (!group.contains("entries")) {
				continue;
			}
			for (const auto& entry : group.at("entries")) {
				if (entry.contains("ini") && entry.at("ini").value("id", "") == a_key) {
					return &entry;
				}
			}
		}
		return nullptr;
	}
}

int main(int a_argc, char** a_argv)
{
	bool ok = true;
	if (a_argc < 10) {
		std::cerr << "FAIL expected dMenu JSON, defaults INI, Config.h, Config.cpp, integration, preview resolver, tracker, wheel-item, and undress-refresh paths\n";
		return 1;
	}

	const std::string dmenuText = ReadFile(a_argv[1]);
	const std::string defaultsText = ReadFile(a_argv[2]);
	const std::string configHeader = ReadFile(a_argv[3]);
	const std::string configSource = ReadFile(a_argv[4]);
	const std::string integration = ReadFile(a_argv[5]);
	const std::string previewResolver = ReadFile(a_argv[6]);
	const std::string tracker = ReadFile(a_argv[7]);
	const std::string wheelItem = ReadFile(a_argv[8]);
	const std::string undressRefresh = ReadFile(a_argv[9]);

	nlohmann::json dmenu;
	try {
		dmenu = nlohmann::json::parse(dmenuText);
	} catch (const std::exception& error) {
		std::cerr << "FAIL dMenu JSON parse: " << error.what() << '\n';
		return 1;
	}
	const auto defaults = ParseOStimDefaults(defaultsText);
	const auto dmenuKeys = CollectDMenuKeys(dmenu);
	constexpr std::array<std::string_view, 4> removedObsoleteSettings{
		"ShowOnlyValidPositions",
		"ShowPositionNames",
		"HideInvalidActions",
		"PreferMetadataPreviews"
	};

	std::set<std::string> expectedPublicKeys;
	bool contractsComplete = true;
	for (const auto& setting : OStimConfigPolicy::kPublicSettings) {
		expectedPublicKeys.emplace(setting.key);
		contractsComplete &= !setting.runtimeConsumer.empty();
	}
	ok &= Expect(contractsComplete, "every retained public setting has an explicit runtime-consumer contract");
	ok &= Expect(dmenuKeys == expectedPublicKeys, "dMenu exposes exactly the retained public OStim settings");

	std::set<std::string> expectedDefaultKeys = expectedPublicKeys;
	for (const auto& setting : OStimConfigPolicy::kLegacyInternalSettings) {
		expectedDefaultKeys.emplace(setting.key);
	}
	std::set<std::string> actualDefaultKeys;
	for (const auto& [key, value] : defaults) {
		(void)value;
		actualDefaultKeys.emplace(key);
	}
	ok &= Expect(actualDefaultKeys == expectedDefaultKeys, "defaults INI contains public settings plus the one intentional legacy-only key");

	bool removedEverywhere = true;
	for (const auto key : removedObsoleteSettings) {
		removedEverywhere &= dmenuText.find(key) == std::string::npos;
		removedEverywhere &= defaultsText.find(key) == std::string::npos;
		removedEverywhere &= configHeader.find(key) == std::string::npos;
		removedEverywhere &= configSource.find(key) == std::string::npos;
		removedEverywhere &= previewResolver.find(key) == std::string::npos;
	}
	ok &= Expect(removedEverywhere, "obsolete and fake-choice settings are absent from UI, defaults, loader, state, and resolver");

	const auto* capacity = FindDMenuEntry(dmenu, "MaxPositionsPerPage");
	ok &= Expect(
		capacity && capacity->at("default") == 8 && capacity->at("style").at("min") == 4 &&
		capacity->at("style").at("max") == 16 && capacity->at("style").at("step") == 1 &&
		capacity->at("text").at("name") == "Scene Actions Per Page" &&
		capacity->at("text").at("desc") == "Maximum number of OStim scene-action slots shown on one wheel page. Higher values show more choices at once but create smaller wheel segments." &&
		defaults.at("MaxPositionsPerPage") == "8" &&
		OStimConfigPolicy::ClampSceneActionsPerPage(0) == 4 &&
		OStimConfigPolicy::ClampSceneActionsPerPage(8) == 8 &&
		OStimConfigPolicy::ClampSceneActionsPerPage(99) == 16,
		"scene-action capacity contract is min 4, default 8, max 16, step 1");

	const auto* sceneActions = FindDMenuEntry(dmenu, "AllowPositionBrowsing");
	const auto* scenePreviews = FindDMenuEntry(dmenu, "ShowPositionPreviews");
	const auto* autoDetect = FindDMenuEntry(dmenu, "AutoDetect");
	const auto* debugLog = FindDMenuEntry(dmenu, "DebugLog");
	ok &= Expect(
		sceneActions && sceneActions->at("text").at("name") == "Show Scene Actions" &&
		scenePreviews && scenePreviews->at("text").at("name") == "Show Scene Previews" &&
		autoDetect && autoDetect->at("text").at("desc").get<std::string>().find("while the integration is disabled") != std::string::npos &&
		debugLog && debugLog->at("text").at("desc").get<std::string>().find("tracker-revision diagnostics") != std::string::npos,
		"public labels and descriptions describe the current unified native workflow");

	const std::string legacyKey = "PreferCurrentAnimationClass";
	ok &= Expect(
		dmenuKeys.count(legacyKey) == 0 && defaults.count(legacyKey) == 1 &&
		configHeader.find(legacyKey) != std::string::npos && configSource.find(legacyKey) != std::string::npos &&
		tracker.find(legacyKey) != std::string::npos &&
		defaultsText.find("Legacy fallback only; has no effect on native current-navigation mode.") != std::string::npos &&
		configHeader.find("Legacy fallback only; has no effect on native current-navigation mode.") != std::string::npos,
		"PreferCurrentAnimationClass is retained only as documented legacy fallback config");

	ok &= Expect(
		configHeader.find("OStimConfigPolicy::kDefaultSceneActionsPerPage") != std::string::npos &&
		configSource.find("OStimConfigPolicy::ClampSceneActionsPerPage") != std::string::npos &&
		integration.find("Config::OStimIntegration::MaxPositionsPerPage") != std::string::npos &&
		integration.find("wheelRebuildRequested") != std::string::npos,
		"MaxPositionsPerPage is shared by defaults, loader clamp, runtime layout, and live refresh");

	const auto* undressRefreshEntry = FindDMenuEntry(dmenu, "RefreshAppearanceAfterUndress");
	ok &= Expect(
		undressRefreshEntry && undressRefreshEntry->at("default") == false &&
		undressRefreshEntry->at("text").at("name") == "Refresh Appearance After Undress" &&
		undressRefreshEntry->at("text").at("desc") == "Request a delayed actor appearance refresh when OStim has already removed worn clothing but the visual model has not updated correctly. Does not remove or equip items." &&
		defaults.at("RefreshAppearanceAfterUndress") == "false" &&
		configHeader.find("inline bool RefreshAppearanceAfterUndress = false;") != std::string::npos &&
		configSource.find("\"RefreshAppearanceAfterUndress\"") != std::string::npos &&
		integration.find("OStimUndressVisualRefresh::Update") != std::string::npos &&
		undressRefresh.find("Config::OStimIntegration::RefreshAppearanceAfterUndress") != std::string::npos,
		"undress appearance refresh is public, defaults OFF, loader-backed, live-updated, and runtime-consumed");

	const auto metadataResolution = previewResolver.find("ResolveMetadataAssetPath(a_position.previewPath)");
	const auto resourceFallback = previewResolver.find("Config::OStimIntegration::UseResourcePreviewFallback");
	const auto normalFallback = previewResolver.find("a_position.previewPath = a_position.iconPath;");
	ok &= Expect(
		metadataResolution != std::string::npos && resourceFallback > metadataResolution &&
		normalFallback > resourceFallback,
		"preview resolution is metadata first, optional Wheeler resources second, normal presentation fallback last");

	ok &= Expect(
		integration.find("GetNavigationOptions") == std::string::npos &&
		integration.find("GetNavigationPositions") == std::string::npos &&
		integration.find("GetCandidatePositions") == std::string::npos &&
		tracker.find("GetNavigationPositions") != std::string::npos &&
		tracker.find("GetCandidatePositions") != std::string::npos,
		"navigation native reads remain isolated to tracker snapshot construction");

	bool visualConsumersPresent = true;
	for (const auto& setting : OStimConfigPolicy::kPublicSettings) {
		if (setting.key.starts_with("SVG") || setting.key.starts_with("DDS")) {
			visualConsumersPresent &= wheelItem.find(setting.key) != std::string::npos;
		}
	}
	ok &= Expect(visualConsumersPresent, "all retained SVG/DDS layout settings have wheel-item consumers");

	std::cout << (ok ? "OStim config verification PASSED\n" : "OStim config verification FAILED\n");
	return ok ? 0 : 1;
}
