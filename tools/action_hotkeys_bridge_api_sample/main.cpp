#define WHEELER_API
#include "API/WheelerAPI.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <SimpleIni.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	namespace logger = SKSE::log;
	#define DLLEXPORT __declspec(dllexport)

	constexpr auto kPluginName = "WheelerBridgeApiSample";
	constexpr auto kPluginVersion = REL::Version{ 1, 0, 0, 0 };
	constexpr auto kConfigPath = "Data/SKSE/Plugins/WheelerBridgeApiSample.ini";
	enum class TestMode
	{
		kUpsertDemo,
		kRemoveOne,
		kClearAll,
		kNone
	};

	struct DemoHotkey
	{
		bool enabled = true;
		std::string sourceTag;
		std::string displayName;
		std::uint32_t scanCode = 0;
		std::uint32_t modifier = 0;
		std::string iconPath;
		std::uint32_t iconTintARGB = 0xFFFFFFFF;
		std::int32_t wheelNumber = 0;
		std::int32_t entryIndex = -1;
		std::uint32_t flags = 0;
	};

	struct Settings
	{
		bool enabled = true;
		bool seedBeforeMutatingAction = true;
		TestMode mode = TestMode::kUpsertDemo;
		std::string removeSourceTag = "Sample.Journal";
		std::vector<DemoHotkey> hotkeys{
			DemoHotkey{ true, "Sample.Journal", "Journal", 0x24, 0, "", 0xFF3CD1BC, 1, -1, 0 },
			DemoHotkey{ true, "Sample.Map", "Map", 0x32, 0, "", 0xFF984318, 1, -1, 0 },
			DemoHotkey{ true, "Sample.Inventory", "Inventory", 0x17, 0, "", 0xFF2A2A78, 2, -1, 0 },
			DemoHotkey{ true, "Sample.Followers", "Followers", 0x26, 0, "", 0xFF3BA64B, 2, -1, 0 }
		};
	};

	std::atomic_bool g_actionApplied{ false };

	void InitializeLog()
	{
		std::shared_ptr<spdlog::sinks::sink> baseSink;

#ifndef NDEBUG
		baseSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
		auto path = logger::log_directory();
		if (!path) {
			return;
		}

		*path /= "WheelerBridgeApiSample.log";
		baseSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

		auto log = std::make_shared<spdlog::logger>("global log", std::move(baseSink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%^%l%$] %v");
	}

	std::string TrimCopy(std::string value)
	{
		const auto notSpace = [](unsigned char ch) {
			return !std::isspace(ch);
		};

		value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
		value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
		return value;
	}

	std::string ToLowerCopy(std::string value)
	{
		std::transform(
			value.begin(),
			value.end(),
			value.begin(),
			[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		return value;
	}

	bool TryReadString(const CSimpleIniA& ini, const char* section, const char* key, std::string& value)
	{
		if (const char* raw = ini.GetValue(section, key, nullptr)) {
			value = TrimCopy(raw);
			return true;
		}
		return false;
	}

	bool TryParseUInt32(const char* raw, std::uint32_t& value)
	{
		if (!raw || !raw[0]) {
			return false;
		}

		try {
			const auto parsed = std::stoull(TrimCopy(raw), nullptr, 0);
			if (parsed > (std::numeric_limits<std::uint32_t>::max)()) {
				return false;
			}
			value = static_cast<std::uint32_t>(parsed);
			return true;
		} catch (...) {
			return false;
		}
	}

	bool TryParseInt32(const char* raw, std::int32_t& value)
	{
		if (!raw || !raw[0]) {
			return false;
		}

		try {
			const auto parsed = std::stoll(TrimCopy(raw), nullptr, 0);
			if (parsed < (std::numeric_limits<std::int32_t>::min)() ||
				parsed > (std::numeric_limits<std::int32_t>::max)()) {
				return false;
			}
			value = static_cast<std::int32_t>(parsed);
			return true;
		} catch (...) {
			return false;
		}
	}

	void TryReadUInt32(const CSimpleIniA& ini, const char* section, const char* key, std::uint32_t& value)
	{
		TryParseUInt32(ini.GetValue(section, key, nullptr), value);
	}

	void TryReadInt32(const CSimpleIniA& ini, const char* section, const char* key, std::int32_t& value)
	{
		TryParseInt32(ini.GetValue(section, key, nullptr), value);
	}

	TestMode ParseMode(std::string value)
	{
		value = ToLowerCopy(TrimCopy(std::move(value)));
		if (value == "upsertdemo" || value == "upsert") {
			return TestMode::kUpsertDemo;
		}
		if (value == "removeone" || value == "remove") {
			return TestMode::kRemoveOne;
		}
		if (value == "clearall" || value == "clear") {
			return TestMode::kClearAll;
		}
		return TestMode::kNone;
	}

	std::string BuildHotkeySection(std::size_t index)
	{
		return "Hotkey" + std::to_string(index + 1);
	}

	std::optional<std::size_t> ParseHotkeySectionOrdinal(std::string_view sectionName)
	{
		constexpr std::string_view prefix = "Hotkey";
		if (!sectionName.starts_with(prefix) || sectionName.size() <= prefix.size()) {
			return std::nullopt;
		}

		const std::string ordinalString(sectionName.substr(prefix.size()));
		try {
			const auto ordinal = std::stoull(ordinalString, nullptr, 10);
			if (ordinal == 0 || ordinal > (std::numeric_limits<std::size_t>::max)()) {
				return std::nullopt;
			}
			return static_cast<std::size_t>(ordinal);
		} catch (...) {
			return std::nullopt;
		}
	}

	Settings LoadSettings()
	{
		Settings settings;

		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(kConfigPath) < 0) {
			logger::warn("{}: could not load '{}', using built-in defaults", kPluginName, kConfigPath);
			return settings;
		}

		settings.enabled = ini.GetBoolValue("General", "Enabled", settings.enabled);
		settings.seedBeforeMutatingAction =
			ini.GetBoolValue("General", "SeedBeforeMutatingAction", settings.seedBeforeMutatingAction);

		std::string mode;
		if (TryReadString(ini, "General", "Mode", mode)) {
			settings.mode = ParseMode(mode);
		}
		TryReadString(ini, "General", "RemoveSourceTag", settings.removeSourceTag);

		CSimpleIniA::TNamesDepend sections;
		ini.GetAllSections(sections);

		std::vector<std::pair<std::size_t, DemoHotkey>> configuredHotkeys;
		configuredHotkeys.reserve(sections.size());

		for (const auto& sectionEntry : sections) {
			if (!sectionEntry.pItem) {
				continue;
			}

			const std::string_view sectionName(sectionEntry.pItem);
			const auto ordinal = ParseHotkeySectionOrdinal(sectionName);
			if (!ordinal.has_value()) {
				continue;
			}

			DemoHotkey hotkey{};
			if (*ordinal >= 1 && *ordinal <= settings.hotkeys.size()) {
				hotkey = settings.hotkeys[*ordinal - 1];
			}

			hotkey.enabled = ini.GetBoolValue(sectionEntry.pItem, "Enabled", hotkey.enabled);
			TryReadString(ini, sectionEntry.pItem, "SourceTag", hotkey.sourceTag);
			TryReadString(ini, sectionEntry.pItem, "DisplayName", hotkey.displayName);
			TryReadString(ini, sectionEntry.pItem, "IconPath", hotkey.iconPath);
			TryReadUInt32(ini, sectionEntry.pItem, "ScanCode", hotkey.scanCode);
			TryReadUInt32(ini, sectionEntry.pItem, "Modifier", hotkey.modifier);
			TryReadUInt32(ini, sectionEntry.pItem, "IconTintARGB", hotkey.iconTintARGB);
			TryReadInt32(ini, sectionEntry.pItem, "WheelNumber", hotkey.wheelNumber);
			TryReadInt32(ini, sectionEntry.pItem, "EntryIndex", hotkey.entryIndex);
			TryReadUInt32(ini, sectionEntry.pItem, "Flags", hotkey.flags);

			configuredHotkeys.emplace_back(*ordinal, std::move(hotkey));
		}

		if (!configuredHotkeys.empty()) {
			std::sort(
				configuredHotkeys.begin(),
				configuredHotkeys.end(),
				[](const auto& lhs, const auto& rhs) {
					return lhs.first < rhs.first;
				});

			settings.hotkeys.clear();
			settings.hotkeys.reserve(configuredHotkeys.size());
			for (auto& [_, hotkey] : configuredHotkeys) {
				settings.hotkeys.push_back(std::move(hotkey));
			}
		}

		return settings;
	}

	using GetWheelerApiFn = WheelerAPI::IWheelerAPI* (*)();

	WheelerAPI::IWheelerAPI* ResolveWheelerApi()
	{
		const HMODULE module = GetModuleHandleW(L"wheeler.dll");
		if (!module) {
			logger::warn("{}: wheeler.dll is not loaded yet", kPluginName);
			return nullptr;
		}

		const auto exportFn = reinterpret_cast<GetWheelerApiFn>(GetProcAddress(module, "GetWheelerAPI"));
		if (!exportFn) {
			logger::warn("{}: GetWheelerAPI export missing", kPluginName);
			return nullptr;
		}

		auto* api = exportFn();
		if (!api) {
			logger::warn("{}: Wheeler API returned nullptr", kPluginName);
			return nullptr;
		}
		if (api->version < WheelerAPI::API_VERSION ||
			!api->UpsertExternalHotkey ||
			!api->RemoveExternalHotkey ||
			!api->ClearExternalHotkeys) {
			logger::warn(
				"{}: Wheeler API v{} does not expose the external hotkey bridge surface",
				kPluginName,
				api->version);
			return nullptr;
		}
		if (!api->IsInitialized || !api->IsInitialized()) {
			logger::warn("{}: Wheeler API not initialized yet", kPluginName);
			return nullptr;
		}

		return api;
	}

	bool ApplyUpserts(const Settings& settings, WheelerAPI::IWheelerAPI& api)
	{
		bool attempted = false;
		bool allSucceeded = true;

		for (const auto& hotkey : settings.hotkeys) {
			if (!hotkey.enabled || hotkey.sourceTag.empty() || hotkey.scanCode == 0) {
				continue;
			}

			attempted = true;
			const WheelerAPI::ExternalHotkeyConfig config{
				.sourceTag = hotkey.sourceTag.c_str(),
				.displayName = hotkey.displayName.empty() ? nullptr : hotkey.displayName.c_str(),
				.scanCode = hotkey.scanCode,
				.modifier = hotkey.modifier,
				.iconPath = hotkey.iconPath.empty() ? nullptr : hotkey.iconPath.c_str(),
				.iconTintARGB = hotkey.iconTintARGB,
				.wheelNumber = hotkey.wheelNumber,
				.entryIndex = hotkey.entryIndex,
				.flags = hotkey.flags
			};

			const auto result = api.UpsertExternalHotkey(&config);
			logger::info(
				"{}: UpsertExternalHotkey sourceTag='{}' result={}",
				kPluginName,
				hotkey.sourceTag,
				static_cast<int>(result));
			allSucceeded &= result == WheelerAPI::Result::OK;
		}

		if (!attempted) {
			logger::warn("{}: no enabled demo hotkeys were configured for upsert", kPluginName);
			return false;
		}

		return allSucceeded;
	}

	bool ApplyRemove(const Settings& settings, WheelerAPI::IWheelerAPI& api)
	{
		if (settings.removeSourceTag.empty()) {
			logger::warn("{}: RemoveOne requested but RemoveSourceTag is empty", kPluginName);
			return false;
		}

		const auto result = api.RemoveExternalHotkey(settings.removeSourceTag.c_str());
		logger::info(
			"{}: RemoveExternalHotkey sourceTag='{}' result={}",
			kPluginName,
			settings.removeSourceTag,
			static_cast<int>(result));
		return result == WheelerAPI::Result::OK;
	}

	bool ApplyClear(WheelerAPI::IWheelerAPI& api)
	{
		api.ClearExternalHotkeys();
		logger::info("{}: ClearExternalHotkeys", kPluginName);
		return true;
	}

	bool TryApplyConfiguredAction(const char* trigger)
	{
		if (g_actionApplied.load()) {
			return true;
		}

		const Settings settings = LoadSettings();
		if (!settings.enabled) {
			logger::info("{}: disabled in '{}'", kPluginName, kConfigPath);
			g_actionApplied = true;
			return true;
		}
		if (settings.mode == TestMode::kNone) {
			logger::info("{}: Mode=None, no API action requested", kPluginName);
			g_actionApplied = true;
			return true;
		}

		auto* api = ResolveWheelerApi();
		if (!api) {
			logger::info("{}: deferred API action after {}", kPluginName, trigger);
			return false;
		}

		bool success = false;
		switch (settings.mode) {
		case TestMode::kUpsertDemo:
			success = ApplyUpserts(settings, *api);
			break;
		case TestMode::kRemoveOne:
			if (settings.seedBeforeMutatingAction) {
				ApplyUpserts(settings, *api);
			}
			success = ApplyRemove(settings, *api);
			break;
		case TestMode::kClearAll:
			if (settings.seedBeforeMutatingAction) {
				ApplyUpserts(settings, *api);
			}
			success = ApplyClear(*api);
			break;
		case TestMode::kNone:
			success = true;
			break;
		}

		if (success) {
			logger::info("{}: completed configured API action after {}", kPluginName, trigger);
			g_actionApplied = true;
		}

		return success;
	}

	void MessageHandler(SKSE::MessagingInterface::Message* message)
	{
		if (!message) {
			return;
		}

		switch (message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			TryApplyConfiguredAction("kDataLoaded");
			break;
		case SKSE::MessagingInterface::kNewGame:
			TryApplyConfiguredAction("kNewGame");
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			TryApplyConfiguredAction("kPostLoadGame");
			break;
		default:
			break;
		}
	}
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData version;
	version.PluginVersion(kPluginVersion);
	version.PluginName(kPluginName);
	version.UsesAddressLibrary();
	version.UsesNoStructs();
	return version;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* skse)
{
	InitializeLog();
	logger::info("{} {} loading", kPluginName, kPluginVersion.string());

	SKSE::Init(skse);

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener("SKSE", MessageHandler)) {
		logger::critical("{}: failed to register SKSE message listener", kPluginName);
		return false;
	}

	return true;
}
