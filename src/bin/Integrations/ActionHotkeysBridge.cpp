#include "ActionHotkeysBridge.h"

#include "bin/API/WheelerAPI.h"
#include "bin/Config.h"
#include "bin/Rendering/TextureManager.h"
#include "bin/UserInput/Controls.h"
#include "bin/Wheeler/TransformWheelManager.h"
#include "bin/Wheeler/Wheel.h"
#include "bin/Wheeler/WheelEntry.h"
#include "bin/Wheeler/WheelItems/WheelItemExternalHotkey.h"
#include "bin/Wheeler/WheelItems/WheelItemFactory.h"
#include "bin/Wheeler/Wheeler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
	constexpr std::uint32_t kMaxActionHotkeysSlots = 60;
	constexpr double kAutoRefreshPollSeconds = 0.5;
	constexpr std::string_view kBridgeWheelTagPrefix = "ActionHotkeysBridge.Wheel";
	constexpr std::string_view kLayoutWheelKeyPrimary = "WheelerWheel";
	constexpr std::string_view kLayoutWheelKeySecondary = "TargetWheel";
	constexpr std::string_view kLayoutEntryKeyPrimary = "WheelerEntry";
	constexpr std::string_view kLayoutEntryKeySecondary = "TargetEntry";

	struct MirroredSlot
	{
		std::string displayName;
		std::uint32_t scanCode = 0;
		std::uint32_t modifier = 0;
		std::string iconPath;
		std::uint32_t iconTintARGB = 0xFFFFFFFF;
		std::uint32_t sourceSlotIndex = 0;
		std::string sourceTag;
		bool hasResolvedIcon = false;
		std::optional<std::uint32_t> preferredWheelNumber;
		std::optional<std::uint32_t> preferredEntryIndex;
		bool strictPreferredWheel = false;
	};

	struct BuildResult
	{
		std::vector<std::vector<int>> wheelEntries;
		std::unordered_map<std::string, Config::ActionHotkeysBridgeSlotPlacement> actualLayout;
		std::uint32_t wheelCount = 0;
		std::uint32_t droppedSlotCount = 0;
	};

	struct SourceFiles
	{
		std::filesystem::path settingsIniPath;
		std::filesystem::path slotsIniPath;
		std::filesystem::path slotDataIniPath;
		bool usingDedicatedSlotsIni = false;
		bool usingLegacyCombinedIni = false;
	};

	struct BridgeState
	{
		bool initialized = false;
		bool navigationRestricted = false;
		bool refreshRequested = false;
		double nextPollTime = 0.0;
		double pendingRefreshAfter = 0.0;
		std::filesystem::path sourceSettingsIniPath;
		std::filesystem::path sourceSlotsIniPath;
		std::unordered_map<std::string, std::filesystem::file_time_type> observedSourceWriteTimes;
		std::unordered_set<std::string> missingSourceWarned;
		std::unordered_map<std::string, std::string> iconResolveCache;
		std::unordered_set<std::string> missingIconWarned;
		std::optional<int> previousWheelIndex;
	};

	BridgeState s_state;
	std::mutex s_injectedSlotsLock;
	std::unordered_map<std::string, MirroredSlot> s_injectedSlots;

	double GetImGuiTimeSafe()
	{
		return ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
	}

	std::string TrimCopy(std::string_view value)
	{
		std::size_t start = 0;
		while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
			++start;
		}
		std::size_t end = value.size();
		while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
			--end;
		}
		return std::string(value.substr(start, end - start));
	}

	std::string LowerCopy(std::string_view value)
	{
		std::string out(value);
		for (char& c : out) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return out;
	}

	bool ReadUInt32Value(const char* rawValue, std::uint32_t& outValue)
	{
		if (!rawValue || !rawValue[0]) {
			return false;
		}

		try {
			outValue = static_cast<std::uint32_t>(std::stoul(rawValue, nullptr, 0));
			return true;
		} catch (...) {
			return false;
		}
	}

	std::optional<std::uint32_t> ReadOptionalUInt32(const CSimpleIniA& ini, const char* section, std::string_view key)
	{
		std::uint32_t value = 0;
		if (!ReadUInt32Value(ini.GetValue(section, key.data(), nullptr), value)) {
			return std::nullopt;
		}
		return value;
	}

	std::filesystem::path ResolvePath(std::string_view rawPath)
	{
		const std::string trimmed = TrimCopy(rawPath);
		if (trimmed.empty()) {
			return {};
		}

		std::filesystem::path path(trimmed);
		if (path.is_relative()) {
			path = std::filesystem::current_path() / path;
		}
		return path.lexically_normal();
	}

	bool FileExists(const std::filesystem::path& path)
	{
		if (path.empty()) {
			return false;
		}
		std::error_code ec;
		return std::filesystem::exists(path, ec) && !ec;
	}

	bool LoadIniFile(const std::filesystem::path& path, CSimpleIniA& outIni)
	{
		outIni.Reset();
		outIni.SetUnicode();
		return !path.empty() && outIni.LoadFile(path.string().c_str()) >= 0;
	}

	std::string NormalizePathKey(const std::filesystem::path& path)
	{
		return LowerCopy(path.lexically_normal().string());
	}

	void ClearMissingSourceWarning(const std::filesystem::path& path)
	{
		if (path.empty()) {
			return;
		}
		s_state.missingSourceWarned.erase(NormalizePathKey(path));
	}

	void LogMissingSourceOnce(std::string_view label, const std::filesystem::path& path)
	{
		if (path.empty()) {
			return;
		}

		const std::string key = NormalizePathKey(path);
		if (!s_state.missingSourceWarned.insert(key).second) {
			return;
		}

		logger::info("ActionHotkeysBridge: {} missing '{}'", label, path.string());
	}

	std::uint32_t NormalizeModifierValue(std::uint32_t modifier)
	{
		switch (modifier) {
		case 1:
			return 0x38;  // Left Alt
		case 2:
			return 0x1D;  // Left Ctrl
		case 3:
			return 0x2A;  // Left Shift
		default:
			return modifier;
		}
	}

	SourceFiles ResolveConfiguredSourceFiles()
	{
		SourceFiles sources;
		sources.settingsIniPath = ResolvePath(Config::ActionHotkeysBridge::SourceIniPath);
		sources.slotsIniPath = ResolvePath(Config::ActionHotkeysBridge::SourceSlotsIniPath);

		if (FileExists(sources.slotsIniPath)) {
			sources.slotDataIniPath = sources.slotsIniPath;
			sources.usingDedicatedSlotsIni = true;
			return sources;
		}

		if (FileExists(sources.settingsIniPath)) {
			sources.slotDataIniPath = sources.settingsIniPath;
			sources.usingLegacyCombinedIni = true;
		}

		return sources;
	}

	std::string BuildWheelTag(std::uint32_t wheelNumber)
	{
		return fmt::format("{}{}", kBridgeWheelTagPrefix, wheelNumber);
	}

	std::optional<std::uint32_t> ParseWheelNumberFromTag(std::string_view tag)
	{
		if (!tag.starts_with(kBridgeWheelTagPrefix)) {
			return std::nullopt;
		}

		const std::string suffix(tag.substr(kBridgeWheelTagPrefix.size()));
		if (suffix.empty()) {
			return std::nullopt;
		}

		try {
			const std::uint32_t wheelNumber = static_cast<std::uint32_t>(std::stoul(suffix, nullptr, 10));
			if (wheelNumber == 0 || wheelNumber > Config::kActionHotkeysBridgeMaxWheels) {
				return std::nullopt;
			}
			return wheelNumber;
		} catch (...) {
			return std::nullopt;
		}
	}

	std::uint32_t GetWheelCapacity(std::uint32_t wheelNumber)
	{
		if (wheelNumber == 0 || wheelNumber > Config::ActionHotkeysBridge::Wheels.size()) {
			return 1;
		}

		return std::clamp(
			Config::ActionHotkeysBridge::Wheels[wheelNumber - 1].EntryCapacity,
			1u,
			64u);
	}

	std::optional<int> FindWheelIndexByTag(std::string_view a_tag)
	{
		std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		const int wheelCount = Wheeler::GetWheelCount();
		for (int i = 0; i < wheelCount; ++i) {
			Wheel* wheel = Wheeler::GetWheelByIndex(i);
			if (!wheel) {
				continue;
			}
			if (wheel->GetClientTag() == a_tag) {
				return i;
			}
		}
		return std::nullopt;
	}

	std::vector<int> FindBridgeWheelIndices()
	{
		std::vector<int> indices;
		std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		const int wheelCount = Wheeler::GetWheelCount();
		indices.reserve(static_cast<std::size_t>(wheelCount));
		for (int i = 0; i < wheelCount; ++i) {
			Wheel* wheel = Wheeler::GetWheelByIndex(i);
			if (!wheel) {
				continue;
			}
			if (ActionHotkeysBridge::IsBridgeWheelTag(wheel->GetClientTag())) {
				indices.push_back(i);
			}
		}
		return indices;
	}

	std::optional<int> FindFirstUserWheelIndex()
	{
		std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		const int wheelCount = Wheeler::GetWheelCount();
		for (int i = 0; i < wheelCount; ++i) {
			Wheel* wheel = Wheeler::GetWheelByIndex(i);
			if (!wheel) {
				continue;
			}
			if (ActionHotkeysBridge::IsBridgeWheelTag(wheel->GetClientTag()) ||
				TransformWheelManager::IsTransformWheelIndex(i) ||
				WheelerAPI::IsManagedWheelIndex(i)) {
				continue;
			}
			return i;
		}
		return std::nullopt;
	}

	std::optional<int> ResolvePreviousUserWheelIndex()
	{
		if (s_state.previousWheelIndex.has_value()) {
			const int idx = *s_state.previousWheelIndex;
			if (idx >= 0 &&
				idx < Wheeler::GetWheelCount() &&
				!ActionHotkeysBridge::IsBridgeWheelIndex(idx) &&
				(TransformWheelManager::IsTransformWheelIndex(idx) ||
					!WheelerAPI::IsManagedWheelIndex(idx))) {
				return idx;
			}
		}

		return FindFirstUserWheelIndex();
	}

	std::optional<std::uint32_t> ReadVisibleSlotLimit(const CSimpleIniA& ini)
	{
		constexpr std::array<const char*, 5> kPreferredSections{
			"Appearance",
			"",
			"General",
			"Main",
			"Settings"
		};

		for (const char* section : kPreferredSections) {
			std::uint32_t value = 0;
			if (ReadUInt32Value(ini.GetValue(section, "NumberOfSlots", nullptr), value)) {
				return std::clamp(value, 0u, kMaxActionHotkeysSlots);
			}
		}

		CSimpleIniA::TNamesDepend sections;
		ini.GetAllSections(sections);
		for (const auto& section : sections) {
			std::uint32_t value = 0;
			if (ReadUInt32Value(ini.GetValue(section.pItem, "NumberOfSlots", nullptr), value)) {
				return std::clamp(value, 0u, kMaxActionHotkeysSlots);
			}
		}

		return std::nullopt;
	}

	std::string BuildFallbackName(std::uint32_t scanCode, std::uint32_t sourceSlotIndex)
	{
		const std::string keyName = Controls::GetKeyNameForMkb(scanCode);
		if (!keyName.empty() && keyName != "Unbound") {
			return keyName;
		}
		return fmt::format("Action {}", sourceSlotIndex);
	}

	void AddIconCandidatesForSource(
		std::vector<std::filesystem::path>& candidates,
		const std::filesystem::path& sourceIniPath,
		const std::filesystem::path& iconPath)
	{
		if (sourceIniPath.empty()) {
			return;
		}

		if (iconPath.has_parent_path()) {
			candidates.push_back((sourceIniPath.parent_path() / iconPath).lexically_normal());
		}

		candidates.push_back((sourceIniPath.parent_path() / "Icons" / iconPath.filename()).lexically_normal());

		std::filesystem::path dataRoot = sourceIniPath.parent_path();
		for (int i = 0; i < 3 && !dataRoot.empty(); ++i) {
			dataRoot = dataRoot.parent_path();
		}
		if (!dataRoot.empty()) {
			candidates.push_back((dataRoot / "Interface" / "ActionHotkeys" / "Icons" / iconPath.filename()).lexically_normal());
			candidates.push_back((dataRoot / "Interface" / "ActionHotkeys" / "Images" / iconPath.filename()).lexically_normal());
		}
	}

	std::string ResolveIconPath(
		const SourceFiles& sourceFiles,
		const std::string& iconName,
		std::uint32_t slotIndex)
	{
		const std::string trimmed = TrimCopy(iconName);
		if (trimmed.empty()) {
			return {};
		}

		const std::string cacheKey = LowerCopy(fmt::format(
			"{}|{}|{}|{}",
			trimmed,
			sourceFiles.settingsIniPath.string(),
			sourceFiles.slotsIniPath.string(),
			Config::ActionHotkeysBridge::SourceIconsPath));
		if (auto it = s_state.iconResolveCache.find(cacheKey); it != s_state.iconResolveCache.end()) {
			return it->second;
		}

		std::filesystem::path iconPath(trimmed);
		std::vector<std::filesystem::path> candidates;
		std::unordered_set<std::string> seenCandidates;
		auto addCandidate = [&](const std::filesystem::path& candidate) {
			if (candidate.empty()) {
				return;
			}
			const std::string key = NormalizePathKey(candidate);
			if (seenCandidates.insert(key).second) {
				candidates.push_back(candidate.lexically_normal());
			}
		};

		if (iconPath.has_parent_path()) {
			addCandidate(ResolvePath(trimmed));
		}
		AddIconCandidatesForSource(candidates, sourceFiles.slotDataIniPath, iconPath);
		AddIconCandidatesForSource(candidates, sourceFiles.settingsIniPath, iconPath);
		addCandidate(ResolvePath(R"(Data\Interface\ActionHotkeys\Icons)") / iconPath.filename());
		addCandidate(ResolvePath(R"(Data\Interface\ActionHotkeys\Images)") / iconPath.filename());
		addCandidate(ResolvePath(R"(Data\SKSE\Plugins\ActionHotkeys\Icons)") / iconPath.filename());
		addCandidate(ResolvePath(R"(Data\SKSE\Plugins\Icons)") / iconPath.filename());
		if (!Config::ActionHotkeysBridge::SourceIconsPath.empty()) {
			addCandidate(ResolvePath(Config::ActionHotkeysBridge::SourceIconsPath) / iconPath.filename());
		}

		for (const auto& candidate : candidates) {
			if (FileExists(candidate)) {
				const std::string resolved = candidate.lexically_normal().string();
				s_state.iconResolveCache[cacheKey] = resolved;
				if (Config::ActionHotkeysBridge::DebugLog) {
					logger::info("ActionHotkeysBridge: resolved icon slot={} icon='{}' -> '{}'",
						slotIndex,
						trimmed,
						resolved);
				}
				return resolved;
			}
		}

		if (!s_state.missingIconWarned.contains(cacheKey)) {
			s_state.missingIconWarned.insert(cacheKey);
			if (Config::ActionHotkeysBridge::DebugLog) {
				logger::info("ActionHotkeysBridge: icon not found slot={} icon='{}'", slotIndex, trimmed);
			}
		}

		s_state.iconResolveCache[cacheKey] = {};
		return {};
	}

	std::string ResolveInjectedIconPath(std::string_view iconPath)
	{
		const std::string trimmed = TrimCopy(iconPath);
		if (trimmed.empty()) {
			return {};
		}

		const std::filesystem::path directPath = ResolvePath(trimmed);
		if (FileExists(directPath)) {
			return directPath.string();
		}

		return ResolveIconPath(ResolveConfiguredSourceFiles(), trimmed, 0);
	}

	std::vector<MirroredSlot> GetInjectedSlotsSnapshot()
	{
		std::vector<MirroredSlot> slots;
		{
			std::lock_guard<std::mutex> lock(s_injectedSlotsLock);
			slots.reserve(s_injectedSlots.size());
			for (const auto& [_, slot] : s_injectedSlots) {
				slots.push_back(slot);
			}
		}

		std::sort(slots.begin(), slots.end(), [](const MirroredSlot& lhs, const MirroredSlot& rhs) {
			return lhs.sourceTag < rhs.sourceTag;
		});
		return slots;
	}

	void MergeSlots(std::vector<MirroredSlot>& slots, std::vector<MirroredSlot>&& incoming)
	{
		std::unordered_map<std::string, std::size_t> byTag;
		byTag.reserve(slots.size() + incoming.size());
		for (std::size_t i = 0; i < slots.size(); ++i) {
			byTag[slots[i].sourceTag] = i;
		}

		for (auto& slot : incoming) {
			auto existing = byTag.find(slot.sourceTag);
			if (existing != byTag.end()) {
				slots[existing->second] = std::move(slot);
				continue;
			}

			byTag.emplace(slot.sourceTag, slots.size());
			slots.push_back(std::move(slot));
		}
	}

	std::optional<std::uint32_t> ReadPreferredWheel(const CSimpleIniA& ini, const char* section)
	{
		if (auto value = ReadOptionalUInt32(ini, section, kLayoutWheelKeyPrimary); value.has_value()) {
			return value;
		}
		return ReadOptionalUInt32(ini, section, kLayoutWheelKeySecondary);
	}

	std::optional<std::uint32_t> ReadPreferredEntry(const CSimpleIniA& ini, const char* section)
	{
		if (auto value = ReadOptionalUInt32(ini, section, kLayoutEntryKeyPrimary); value.has_value()) {
			return value;
		}
		return ReadOptionalUInt32(ini, section, kLayoutEntryKeySecondary);
	}

	bool ParseSourceSlots(const SourceFiles& sourceFiles, std::vector<MirroredSlot>& outSlots)
	{
		outSlots.clear();

		CSimpleIniA slotsIni;
		if (!LoadIniFile(sourceFiles.slotDataIniPath, slotsIni)) {
			logger::warn("ActionHotkeysBridge: failed to load slot source ini '{}'", sourceFiles.slotDataIniPath.string());
			return false;
		}

		std::optional<std::uint32_t> visibleSlotLimit;
		CSimpleIniA settingsIni;
		if (!sourceFiles.settingsIniPath.empty() &&
			sourceFiles.settingsIniPath != sourceFiles.slotDataIniPath &&
			LoadIniFile(sourceFiles.settingsIniPath, settingsIni)) {
			visibleSlotLimit = ReadVisibleSlotLimit(settingsIni);
		}
		if (!visibleSlotLimit.has_value()) {
			visibleSlotLimit = ReadVisibleSlotLimit(slotsIni);
		}

		std::uint32_t parsedSlotCount = 0;
		std::uint32_t importedSlotCount = 0;

		for (std::uint32_t slotIndex = 1; slotIndex <= kMaxActionHotkeysSlots; ++slotIndex) {
			if (visibleSlotLimit.has_value() && *visibleSlotLimit > 0 && slotIndex > *visibleSlotLimit) {
				break;
			}

			const std::string section = fmt::format("Slot{}", slotIndex);
			const char* hotkeyRaw = slotsIni.GetValue(section.c_str(), "Hotkey", nullptr);
			const char* nameRaw = slotsIni.GetValue(section.c_str(), "Name", nullptr);
			const char* iconRaw = slotsIni.GetValue(section.c_str(), "Icon", nullptr);
			const char* modifierRaw = slotsIni.GetValue(section.c_str(), "Modifier", nullptr);
			const char* iconColorRaw = slotsIni.GetValue(section.c_str(), "IconColor", nullptr);
			if (!hotkeyRaw && !nameRaw && !iconRaw && !modifierRaw && !iconColorRaw) {
				continue;
			}

			parsedSlotCount++;

			std::uint32_t hotkey = 0;
			if (!ReadUInt32Value(hotkeyRaw, hotkey) || hotkey == 0) {
				continue;
			}

			MirroredSlot slot{};
			slot.scanCode = hotkey;
			slot.sourceSlotIndex = slotIndex;
			slot.sourceTag = section;
			std::uint32_t modifierValue = 0;
			if (ReadUInt32Value(modifierRaw, modifierValue)) {
				slot.modifier = NormalizeModifierValue(modifierValue);
			}
			ReadUInt32Value(iconColorRaw, slot.iconTintARGB);

			slot.displayName = TrimCopy(nameRaw ? nameRaw : "");
			if (slot.displayName.empty()) {
				slot.displayName = BuildFallbackName(slot.scanCode, slotIndex);
			}

			if (iconRaw && iconRaw[0]) {
				slot.iconPath = ResolveIconPath(sourceFiles, iconRaw, slotIndex);
				slot.hasResolvedIcon = !slot.iconPath.empty();
			}

			slot.preferredWheelNumber = ReadPreferredWheel(slotsIni, section.c_str());
			slot.preferredEntryIndex = ReadPreferredEntry(slotsIni, section.c_str());

			outSlots.push_back(std::move(slot));
			importedSlotCount++;
		}

		if (Config::ActionHotkeysBridge::DebugLog) {
			logger::info(
				"ActionHotkeysBridge: parsed slots total={} imported={} limit={} slotSource='{}' settingsSource='{}' mode={}",
				parsedSlotCount,
				importedSlotCount,
				visibleSlotLimit.value_or(kMaxActionHotkeysSlots),
				sourceFiles.slotDataIniPath.string(),
				sourceFiles.settingsIniPath.string(),
				sourceFiles.usingDedicatedSlotsIni ? "split" :
					(sourceFiles.usingLegacyCombinedIni ? "legacy" : "direct"));
		}

		return true;
	}

	bool TryMakeInjectedSlot(const ActionHotkeysInjectedSlot& injected, MirroredSlot& outSlot)
	{
		const std::string sourceTag = TrimCopy(injected.sourceTag);
		if (sourceTag.empty() || injected.scanCode == 0) {
			return false;
		}
		if (injected.wheelNumber < 0 || injected.wheelNumber > static_cast<int>(Config::kActionHotkeysBridgeMaxWheels)) {
			return false;
		}
		if (injected.entryIndex < -1) {
			return false;
		}

		outSlot = MirroredSlot{};
		outSlot.sourceTag = sourceTag;
		outSlot.sourceSlotIndex = 0;
		outSlot.scanCode = injected.scanCode;
		outSlot.modifier = NormalizeModifierValue(injected.modifier);
		outSlot.displayName = TrimCopy(injected.displayName);
		if (outSlot.displayName.empty()) {
			outSlot.displayName = BuildFallbackName(outSlot.scanCode, 0);
		}

		outSlot.iconPath = ResolveInjectedIconPath(injected.iconPath);
		outSlot.hasResolvedIcon = !outSlot.iconPath.empty();
		outSlot.iconTintARGB = injected.iconTintARGB;
		if (injected.wheelNumber > 0) {
			outSlot.preferredWheelNumber = static_cast<std::uint32_t>(injected.wheelNumber);
			outSlot.strictPreferredWheel = true;
		}
		if (injected.entryIndex >= 0) {
			outSlot.preferredEntryIndex = static_cast<std::uint32_t>(injected.entryIndex);
		}
		return true;
	}

	bool EnsureWheelTagged(int wheelIndex, std::string_view tag)
	{
		if (wheelIndex < 0) {
			return false;
		}

		std::unique_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
		if (!wheel) {
			return false;
		}
		wheel->SetClientTag(tag);
		return true;
	}

	void EnsureLayoutWheel(std::vector<std::vector<int>>& wheelEntries, std::uint32_t wheelNumber)
	{
		while (wheelEntries.size() < wheelNumber) {
			const std::uint32_t nextWheelNumber = static_cast<std::uint32_t>(wheelEntries.size() + 1);
			wheelEntries.emplace_back(static_cast<std::size_t>(GetWheelCapacity(nextWheelNumber)), -1);
		}
	}

	bool TryPlaceSlotAt(
		BuildResult& result,
		const MirroredSlot& slot,
		int slotVectorIndex,
		std::uint32_t wheelNumber,
		std::uint32_t entryIndex)
	{
		if (wheelNumber == 0 || wheelNumber > Config::kActionHotkeysBridgeMaxWheels) {
			return false;
		}

		EnsureLayoutWheel(result.wheelEntries, wheelNumber);
		auto& entries = result.wheelEntries[wheelNumber - 1];
		if (entryIndex >= entries.size() || entries[entryIndex] != -1) {
			return false;
		}

		entries[entryIndex] = slotVectorIndex;
		result.actualLayout[slot.sourceTag] = Config::ActionHotkeysBridgeSlotPlacement{
			wheelNumber,
			entryIndex
		};
		result.wheelCount = (std::max)(result.wheelCount, wheelNumber);
		return true;
	}

	bool TryPlaceSlotOnWheel(
		BuildResult& result,
		const MirroredSlot& slot,
		int slotVectorIndex,
		std::uint32_t wheelNumber)
	{
		if (wheelNumber == 0 || wheelNumber > Config::kActionHotkeysBridgeMaxWheels) {
			return false;
		}

		EnsureLayoutWheel(result.wheelEntries, wheelNumber);
		auto& entries = result.wheelEntries[wheelNumber - 1];
		for (std::size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
			if (entries[entryIndex] != -1) {
				continue;
			}

			entries[entryIndex] = slotVectorIndex;
			result.actualLayout[slot.sourceTag] = Config::ActionHotkeysBridgeSlotPlacement{
				wheelNumber,
				static_cast<std::uint32_t>(entryIndex)
			};
			result.wheelCount = (std::max)(result.wheelCount, wheelNumber);
			return true;
		}

		return false;
	}

	bool TryPlaceSequentially(
		BuildResult& result,
		const MirroredSlot& slot,
		int slotVectorIndex,
		bool allowAutoExpand,
		std::uint32_t minimumWheelCount)
	{
		const std::uint32_t maxWheels = static_cast<std::uint32_t>(Config::kActionHotkeysBridgeMaxWheels);
		std::uint32_t searchWheelCount = (std::max)(minimumWheelCount, static_cast<std::uint32_t>(result.wheelEntries.size()));
		searchWheelCount = std::clamp(searchWheelCount, 1u, maxWheels);
		EnsureLayoutWheel(result.wheelEntries, searchWheelCount);

		for (std::uint32_t wheelNumber = 1; wheelNumber <= searchWheelCount; ++wheelNumber) {
			if (TryPlaceSlotOnWheel(result, slot, slotVectorIndex, wheelNumber)) {
				return true;
			}
		}

		if (!allowAutoExpand) {
			return false;
		}

		while (result.wheelEntries.size() < maxWheels) {
			const std::uint32_t nextWheelNumber = static_cast<std::uint32_t>(result.wheelEntries.size() + 1);
			EnsureLayoutWheel(result.wheelEntries, nextWheelNumber);
			if (TryPlaceSlotOnWheel(result, slot, slotVectorIndex, nextWheelNumber)) {
				return true;
			}
		}

		return false;
	}

	BuildResult BuildWheelLayout(const std::vector<MirroredSlot>& slots)
	{
		BuildResult result;
		const bool autoInjection = Config::ActionHotkeysBridge::AutoInjection;
		const std::uint32_t manualWheelCount = std::clamp(
			Config::ActionHotkeysBridge::ManualWheelCount,
			1u,
			static_cast<std::uint32_t>(Config::kActionHotkeysBridgeMaxWheels));

		if (slots.empty()) {
			if (!autoInjection) {
				EnsureLayoutWheel(result.wheelEntries, manualWheelCount);
				result.wheelCount = manualWheelCount;
			}
			return result;
		}

		if (!autoInjection) {
			EnsureLayoutWheel(result.wheelEntries, manualWheelCount);
			result.wheelCount = manualWheelCount;
		}

		std::vector<bool> placed(slots.size(), false);
		auto tryFixedPlacement = [&](std::size_t slotIndex, const std::optional<std::uint32_t>& preferredWheel, const std::optional<std::uint32_t>& preferredEntry) {
			if (!preferredWheel.has_value()) {
				return false;
			}

			const std::uint32_t wheelNumber = *preferredWheel;
			if (!autoInjection && wheelNumber > manualWheelCount) {
				return false;
			}

			if (preferredEntry.has_value() &&
				TryPlaceSlotAt(result, slots[slotIndex], static_cast<int>(slotIndex), wheelNumber, *preferredEntry)) {
				return true;
			}

			return TryPlaceSlotOnWheel(result, slots[slotIndex], static_cast<int>(slotIndex), wheelNumber);
		};

		for (std::size_t i = 0; i < slots.size(); ++i) {
			if (placed[i]) {
				continue;
			}

			auto persistedIt = Config::ActionHotkeysBridge::PersistedLayout.find(slots[i].sourceTag);
			if (persistedIt == Config::ActionHotkeysBridge::PersistedLayout.end()) {
				continue;
			}

			const auto& placement = persistedIt->second;
			if (tryFixedPlacement(i, placement.wheelNumber, placement.entryIndex)) {
				placed[i] = true;
			}
		}

		for (std::size_t i = 0; i < slots.size(); ++i) {
			if (placed[i]) {
				continue;
			}

			if (tryFixedPlacement(i, slots[i].preferredWheelNumber, slots[i].preferredEntryIndex)) {
				placed[i] = true;
			}
		}

		for (std::size_t i = 0; i < slots.size(); ++i) {
			if (placed[i]) {
				continue;
			}

			if (slots[i].strictPreferredWheel && slots[i].preferredWheelNumber.has_value()) {
				result.droppedSlotCount++;
				continue;
			}

			if (!TryPlaceSequentially(
					result,
					slots[i],
					static_cast<int>(i),
					autoInjection,
					autoInjection ? 1u : manualWheelCount)) {
				result.droppedSlotCount++;
			}
		}

		if (autoInjection) {
			std::uint32_t highestUsedWheel = 0;
			for (const auto& [_, placement] : result.actualLayout) {
				highestUsedWheel = (std::max)(highestUsedWheel, placement.wheelNumber);
			}
			if (highestUsedWheel == 0) {
				result.wheelEntries.clear();
				result.wheelCount = 0;
			} else {
				result.wheelEntries.resize(highestUsedWheel);
				result.wheelCount = highestUsedWheel;
			}
		}

		return result;
	}

	bool EnsureManagedWheel(std::uint32_t wheelNumber, std::uint32_t desiredEntries)
	{
		if (wheelNumber == 0 || wheelNumber > Config::kActionHotkeysBridgeMaxWheels) {
			return false;
		}

		const std::string tag = BuildWheelTag(wheelNumber);
		if (FindWheelIndexByTag(tag).has_value()) {
			return true;
		}

		auto* api = GetWheelerAPI();
		if (!api || !api->IsInitialized()) {
			logger::warn("ActionHotkeysBridge: API unavailable, cannot create reserved wheel {}", wheelNumber);
			return false;
		}

		WheelerAPI::WheelConfig config{};
		config.numEntries = static_cast<int32_t>((std::max)(1u, desiredEntries));
		config.position = -1;
		config.managed = true;
		config.clientName = tag.c_str();
		config.showLabel = false;
		const int created = api->CreateManagedWheel(&config);
		if (created < 0) {
			logger::warn("ActionHotkeysBridge: failed to create reserved wheel {} result={}", wheelNumber, created);
			return false;
		}

		EnsureWheelTagged(created, tag);
		if (Config::ActionHotkeysBridge::DebugLog) {
			logger::info("ActionHotkeysBridge: created reserved wheel {} index={} tag={}", wheelNumber, created, tag);
		}
		return true;
	}

	void DeleteBridgeWheelByIndex(int wheelIndex)
	{
		auto* api = GetWheelerAPI();
		if (!api) {
			return;
		}

		const auto result = api->DeleteManagedWheel(wheelIndex);
		if (result != WheelerAPI::Result::OK && Config::ActionHotkeysBridge::DebugLog) {
			logger::warn(
				"ActionHotkeysBridge: failed to delete reserved wheel index={} result={}",
				wheelIndex,
				static_cast<int>(result));
		}
	}

	void RemoveBridgeWheelsAbove(std::uint32_t keepWheelCount)
	{
		auto indices = FindBridgeWheelIndices();
		std::sort(indices.begin(), indices.end(), std::greater<>());
		for (int wheelIndex : indices) {
			std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
			Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
			const auto wheelNumber = wheel ? ParseWheelNumberFromTag(wheel->GetClientTag()) : std::nullopt;
			lock.unlock();

			if (!wheelNumber.has_value() || *wheelNumber <= keepWheelCount) {
				continue;
			}

			DeleteBridgeWheelByIndex(wheelIndex);
		}
	}

	void RemoveAllBridgeWheels()
	{
		RemoveBridgeWheelsAbove(0);
		Texture::InvalidateExternalRasterCache();
	}

	void PopulateWheel(
		int wheelIndex,
		std::uint32_t wheelNumber,
		const std::vector<MirroredSlot>& slots,
		const std::vector<int>& wheelEntries)
	{
		std::unique_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
		if (!wheel) {
			return;
		}

		const std::string tag = BuildWheelTag(wheelNumber);
		wheel->Clear();
		for (int slotIndex : wheelEntries) {
			auto entry = std::make_unique<WheelEntry>();
			if (slotIndex >= 0 && static_cast<std::size_t>(slotIndex) < slots.size()) {
				const MirroredSlot& slot = slots[static_cast<std::size_t>(slotIndex)];
				auto item = WheelItemFactory::MakeExternalHotkeyItem(
					slot.displayName,
					slot.scanCode,
					slot.modifier,
					slot.iconPath,
					slot.iconTintARGB,
					slot.sourceSlotIndex,
					slot.sourceTag,
					slot.hasResolvedIcon);
				if (item) {
					entry->PushItem(std::move(item));
				}
			}
			wheel->PushEntry(std::move(entry));
		}

		if (wheelEntries.empty()) {
			wheel->PushEmptyEntry();
		}

		wheel->SetClientTag(tag);
		wheel->SetHoveredEntryIndex(-1);
		wheel->ResetAnimation();
	}

	bool PersistLayoutIfChanged(const std::unordered_map<std::string, Config::ActionHotkeysBridgeSlotPlacement>& newLayout)
	{
		if (Config::ActionHotkeysBridge::PersistedLayout == newLayout) {
			return false;
		}

		Config::ActionHotkeysBridge::PersistedLayout = newLayout;
		Config::WriteActionHotkeysBridgeLayout();
		return true;
	}

	bool ObserveSourcePathChange(const std::filesystem::path& path, bool initializing)
	{
		if (path.empty()) {
			return false;
		}

		const std::string key = NormalizePathKey(path);
		const auto lastObservedIt = s_state.observedSourceWriteTimes.find(key);
		const auto lastObserved = lastObservedIt != s_state.observedSourceWriteTimes.end() ?
			                          lastObservedIt->second :
			                          std::filesystem::file_time_type{};

		std::error_code ec;
		if (!std::filesystem::exists(path, ec) || ec) {
			s_state.observedSourceWriteTimes[key] = {};
			return !initializing && lastObserved != std::filesystem::file_time_type{};
		}

		const auto currentWriteTime = std::filesystem::last_write_time(path, ec);
		if (ec) {
			return false;
		}

		s_state.observedSourceWriteTimes[key] = currentWriteTime;
		return !initializing && currentWriteTime != lastObserved;
	}

	void RebuildMirror()
	{
		if (!Config::ActionHotkeysBridge::Enabled) {
			RemoveAllBridgeWheels();
			s_state.previousWheelIndex.reset();
			s_state.navigationRestricted = false;
			return;
		}

		s_state.iconResolveCache.clear();
		s_state.missingIconWarned.clear();

		std::vector<MirroredSlot> slots;
		if (Config::ActionHotkeysBridge::AutoInjection) {
			const SourceFiles sourceFiles = ResolveConfiguredSourceFiles();
			s_state.sourceSettingsIniPath = sourceFiles.settingsIniPath;
			s_state.sourceSlotsIniPath = sourceFiles.slotsIniPath;

			if (sourceFiles.usingDedicatedSlotsIni) {
				ClearMissingSourceWarning(sourceFiles.slotsIniPath);
				ClearMissingSourceWarning(sourceFiles.settingsIniPath);
				if (!ParseSourceSlots(sourceFiles, slots)) {
					return;
				}
			} else if (sourceFiles.usingLegacyCombinedIni) {
				if (!sourceFiles.slotsIniPath.empty() && sourceFiles.slotsIniPath != sourceFiles.settingsIniPath) {
					LogMissingSourceOnce("slot source ini", sourceFiles.slotsIniPath);
				}
				ClearMissingSourceWarning(sourceFiles.settingsIniPath);
				if (!ParseSourceSlots(sourceFiles, slots)) {
					return;
				}
			} else {
				LogMissingSourceOnce("slot source ini", sourceFiles.slotsIniPath);
				if (!sourceFiles.settingsIniPath.empty() && sourceFiles.settingsIniPath != sourceFiles.slotsIniPath) {
					LogMissingSourceOnce("settings ini", sourceFiles.settingsIniPath);
				}
			}
		} else {
			s_state.sourceSettingsIniPath.clear();
			s_state.sourceSlotsIniPath.clear();
			s_state.missingSourceWarned.clear();
		}

		MergeSlots(slots, GetInjectedSlotsSnapshot());

		BuildResult buildResult = BuildWheelLayout(slots);
		if (buildResult.wheelCount == 0) {
			RemoveAllBridgeWheels();
			return;
		}

		const int activeWheelBefore = Wheeler::GetActiveWheelIndex();
		std::string activeBridgeTag;
		if (activeWheelBefore >= 0) {
			std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
			if (Wheel* activeWheel = Wheeler::GetWheelByIndex(activeWheelBefore);
				activeWheel && ActionHotkeysBridge::IsBridgeWheelTag(activeWheel->GetClientTag())) {
				activeBridgeTag = activeWheel->GetClientTag();
			}
		}

		for (std::uint32_t wheelNumber = 1; wheelNumber <= buildResult.wheelCount; ++wheelNumber) {
			const auto& wheelEntries = buildResult.wheelEntries[wheelNumber - 1];
			if (!EnsureManagedWheel(wheelNumber, static_cast<std::uint32_t>(wheelEntries.size()))) {
				continue;
			}
			if (auto wheelId = FindWheelIndexByTag(BuildWheelTag(wheelNumber)); wheelId.has_value()) {
				PopulateWheel(*wheelId, wheelNumber, slots, wheelEntries);
			}
		}

		RemoveBridgeWheelsAbove(buildResult.wheelCount);
		Texture::InvalidateExternalRasterCache();
		if (!slots.empty()) {
			PersistLayoutIfChanged(buildResult.actualLayout);
		}

		if (!activeBridgeTag.empty() && s_state.navigationRestricted) {
			if (auto activeIdx = FindWheelIndexByTag(activeBridgeTag); activeIdx.has_value()) {
				if (Wheeler::IsWheelerOpen()) {
					Wheeler::SwitchToWheelIndexForNavigation(*activeIdx);
				} else {
					Wheeler::SetActiveWheelIndex(*activeIdx);
				}
			}
		}

		if (Config::ActionHotkeysBridge::DebugLog) {
			logger::info(
				"ActionHotkeysBridge: mirror rebuild completed slots={} wheels={} dropped={}",
				slots.size(),
				buildResult.wheelCount,
				buildResult.droppedSlotCount);
		}

		if (s_state.previousWheelIndex.has_value()) {
			const int savedIdx = *s_state.previousWheelIndex;
			if (savedIdx < 0 ||
				savedIdx >= Wheeler::GetWheelCount() ||
				ActionHotkeysBridge::IsBridgeWheelIndex(savedIdx) ||
				(!TransformWheelManager::IsTransformWheelIndex(savedIdx) &&
					WheelerAPI::IsManagedWheelIndex(savedIdx))) {
				s_state.previousWheelIndex.reset();
			}
		}
	}

	void QueueRefreshAfterWriteChange()
	{
		if (!Config::ActionHotkeysBridge::AutoInjection) {
			return;
		}

		const SourceFiles sourceFiles = ResolveConfiguredSourceFiles();
		std::vector<std::filesystem::path> watchedPaths;
		if (!sourceFiles.settingsIniPath.empty()) {
			watchedPaths.push_back(sourceFiles.settingsIniPath);
		}
		if (!sourceFiles.slotsIniPath.empty() &&
			std::find(watchedPaths.begin(), watchedPaths.end(), sourceFiles.slotsIniPath) == watchedPaths.end()) {
			watchedPaths.push_back(sourceFiles.slotsIniPath);
		}

		if (watchedPaths.empty()) {
			return;
		}

		if (!s_state.initialized) {
			s_state.initialized = true;
			for (const auto& watchedPath : watchedPaths) {
				ObserveSourcePathChange(watchedPath, true);
			}
			return;
		}

		bool changed = false;
		for (const auto& watchedPath : watchedPaths) {
			changed = ObserveSourcePathChange(watchedPath, false) || changed;
		}

		if (changed) {
			s_state.pendingRefreshAfter =
				GetImGuiTimeSafe() + static_cast<double>(Config::ActionHotkeysBridge::RefreshDebounceMs) / 1000.0;
			s_state.refreshRequested = true;
		}
	}

	bool DeferBridgeMutationIfWheelOpen(double now, const char* reason)
	{
		if (!Wheeler::IsWheelerOpen()) {
			return false;
		}

		const double deferSeconds = (std::max)(
			0.05,
			static_cast<double>(Config::ActionHotkeysBridge::RefreshDebounceMs) / 1000.0);
		s_state.refreshRequested = true;
		s_state.pendingRefreshAfter = now + deferSeconds;

		static double s_lastDeferredLog = 0.0;
		if (Config::ActionHotkeysBridge::DebugLog && now - s_lastDeferredLog >= 1.0) {
			logger::info("ActionHotkeysBridge: mirror mutation deferred reason={} wheelOpen=1", reason ? reason : "Unknown");
			s_lastDeferredLog = now;
		}
		return true;
	}

	bool JumpToWheelInternal(std::uint32_t wheelNumber)
	{
		if (!Wheeler::IsWheelerOpen()) {
			return false;
		}

		auto targetWheelIdx = ActionHotkeysBridge::GetWheelIndex(wheelNumber);
		if (!targetWheelIdx.has_value()) {
			if (Config::ActionHotkeysBridge::DebugLog) {
				logger::info("ActionHotkeysBridge: jump no-op reason=WheelMissing wheel={}", wheelNumber);
			}
			return false;
		}

		const int currentIdx = Wheeler::GetActiveWheelIndex();
		if (currentIdx >= 0 &&
		    currentIdx == *targetWheelIdx &&
		    ActionHotkeysBridge::IsBridgeWheelIndex(currentIdx)) {
			return ActionHotkeysBridge::ReturnToPreviousWheel();
		}

		if (currentIdx >= 0 &&
			currentIdx != *targetWheelIdx &&
			!ActionHotkeysBridge::IsBridgeWheelIndex(currentIdx) &&
			(TransformWheelManager::IsTransformWheelIndex(currentIdx) ||
				!WheelerAPI::IsManagedWheelIndex(currentIdx))) {
			s_state.previousWheelIndex = currentIdx;
		}

		const bool switched = Wheeler::SwitchToWheelIndexForNavigation(*targetWheelIdx);
		if (switched) {
			s_state.navigationRestricted = true;
			if (Config::ActionHotkeysBridge::DebugLog) {
				logger::info("ActionHotkeysBridge: jump wheel={} index={} previous={}",
					wheelNumber,
					*targetWheelIdx,
					s_state.previousWheelIndex.value_or(-1));
			}
		}
		return switched;
	}
}

void ActionHotkeysBridge::Init()
{
	RequestRefresh(true);
}

void ActionHotkeysBridge::Update()
{
	const double now = GetImGuiTimeSafe();
	if (!Config::ActionHotkeysBridge::Enabled) {
		if (!FindBridgeWheelIndices().empty()) {
			if (DeferBridgeMutationIfWheelOpen(now, "Disabled")) {
				return;
			}
			RemoveAllBridgeWheels();
		}
		s_state.previousWheelIndex.reset();
		s_state.navigationRestricted = false;
		s_state.refreshRequested = false;
		s_state.pendingRefreshAfter = 0.0;
		return;
	}

	if (Config::ActionHotkeysBridge::AutoInjection &&
	    Config::ActionHotkeysBridge::AutoRefresh &&
	    now >= s_state.nextPollTime) {
		s_state.nextPollTime = now + kAutoRefreshPollSeconds;
		QueueRefreshAfterWriteChange();
	}

	if (!s_state.refreshRequested) {
		return;
	}
	if (s_state.pendingRefreshAfter > 0.0 && now < s_state.pendingRefreshAfter) {
		return;
	}

	if (DeferBridgeMutationIfWheelOpen(now, "Refresh")) {
		return;
	}

	s_state.refreshRequested = false;
	s_state.pendingRefreshAfter = 0.0;
	RebuildMirror();
}

void ActionHotkeysBridge::Reset()
{
	Texture::InvalidateExternalRasterCache();
	s_state = BridgeState{};
}

void ActionHotkeysBridge::RequestRefresh(bool a_forceImmediate)
{
	s_state.refreshRequested = true;
	s_state.pendingRefreshAfter = a_forceImmediate ? 0.0 :
		GetImGuiTimeSafe() + static_cast<double>(Config::ActionHotkeysBridge::RefreshDebounceMs) / 1000.0;
}

bool ActionHotkeysBridge::IsBridgeWheelTag(std::string_view a_tag)
{
	return ParseWheelNumberFromTag(a_tag).has_value();
}

bool ActionHotkeysBridge::IsBridgeWheelIndex(int a_wheelIndex)
{
	if (a_wheelIndex < 0) {
		return false;
	}

	std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
	Wheel* wheel = Wheeler::GetWheelByIndex(a_wheelIndex);
	return wheel && IsBridgeWheelTag(wheel->GetClientTag());
}

bool ActionHotkeysBridge::IsReadOnlyWheelTag(std::string_view a_tag)
{
	return IsBridgeWheelTag(a_tag);
}

bool ActionHotkeysBridge::IsNavigationRestrictedToBridgeWheels()
{
	if (!s_state.navigationRestricted || !Wheeler::IsWheelerOpen()) {
		return false;
	}

	return IsBridgeWheelIndex(Wheeler::GetActiveWheelIndex());
}

std::optional<int> ActionHotkeysBridge::GetWheelIndex(std::uint32_t a_oneBasedWheelNumber)
{
	if (a_oneBasedWheelNumber == 0 || a_oneBasedWheelNumber > Config::kActionHotkeysBridgeMaxWheels) {
		return std::nullopt;
	}
	return FindWheelIndexByTag(BuildWheelTag(a_oneBasedWheelNumber));
}

bool ActionHotkeysBridge::JumpToWheel(std::uint32_t a_wheelNumber)
{
	return JumpToWheelInternal(a_wheelNumber);
}

bool ActionHotkeysBridge::ReturnToPreviousWheel()
{
	if (!Wheeler::IsWheelerOpen() || !s_state.previousWheelIndex.has_value()) {
		return false;
	}

	const int targetIdx = *s_state.previousWheelIndex;
	if (targetIdx < 0 ||
		targetIdx >= Wheeler::GetWheelCount() ||
		IsBridgeWheelIndex(targetIdx) ||
		(!TransformWheelManager::IsTransformWheelIndex(targetIdx) &&
			WheelerAPI::IsManagedWheelIndex(targetIdx))) {
		s_state.previousWheelIndex.reset();
		return false;
	}

	const bool switched = Wheeler::SwitchToWheelIndexForNavigation(targetIdx);
	if (switched) {
		if (Config::ActionHotkeysBridge::DebugLog) {
			logger::info("ActionHotkeysBridge: returned to previous wheel index={}", targetIdx);
		}
		s_state.navigationRestricted = false;
		s_state.previousWheelIndex.reset();
	}
	return switched;
}

std::optional<int> ActionHotkeysBridge::GetPreviousWheelIndex()
{
	return s_state.previousWheelIndex;
}

bool ActionHotkeysBridge::ResetPersistedLayout(bool a_forceImmediateRefresh)
{
	const std::size_t clearedEntries = Config::ActionHotkeysBridge::PersistedLayout.size();
	Config::ActionHotkeysBridge::PersistedLayout.clear();

	const bool wroteLayout = Config::WriteActionHotkeysBridgeLayout();
	if (!wroteLayout) {
		logger::warn("ActionHotkeysBridge: failed to write cleared layout file");
	}

	if (a_forceImmediateRefresh) {
		RequestRefresh(true);
	}

	if (Config::ActionHotkeysBridge::DebugLog || clearedEntries > 0) {
		logger::info(
			"ActionHotkeysBridge: layout reset clearedEntries={} refreshImmediate={}",
			clearedEntries,
			a_forceImmediateRefresh ? 1 : 0);
	}

	return clearedEntries > 0;
}

bool ActionHotkeysBridge::UpsertInjectedSlot(const ActionHotkeysInjectedSlot& a_slot)
{
	MirroredSlot normalized;
	if (!TryMakeInjectedSlot(a_slot, normalized)) {
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(s_injectedSlotsLock);
		s_injectedSlots[normalized.sourceTag] = std::move(normalized);
	}

	RequestRefresh(true);
	return true;
}

bool ActionHotkeysBridge::RemoveInjectedSlot(std::string_view a_sourceTag)
{
	const std::string sourceTag = TrimCopy(a_sourceTag);
	if (sourceTag.empty()) {
		return false;
	}

	bool removed = false;
	{
		std::lock_guard<std::mutex> lock(s_injectedSlotsLock);
		removed = s_injectedSlots.erase(sourceTag) > 0;
	}

	if (removed) {
		Config::ActionHotkeysBridge::PersistedLayout.erase(sourceTag);
		Config::WriteActionHotkeysBridgeLayout();
		RequestRefresh(true);
	}
	return removed;
}

void ActionHotkeysBridge::ClearInjectedSlots()
{
	std::vector<std::string> removedTags;
	{
		std::lock_guard<std::mutex> lock(s_injectedSlotsLock);
		removedTags.reserve(s_injectedSlots.size());
		for (const auto& [sourceTag, _] : s_injectedSlots) {
			removedTags.push_back(sourceTag);
		}
		s_injectedSlots.clear();
	}

	bool layoutChanged = false;
	for (const auto& sourceTag : removedTags) {
		layoutChanged = Config::ActionHotkeysBridge::PersistedLayout.erase(sourceTag) > 0 || layoutChanged;
	}
	if (layoutChanged) {
		Config::WriteActionHotkeysBridgeLayout();
	}
	RequestRefresh(true);
}

void ActionHotkeysBridge::PersistCurrentLayout()
{
	std::unordered_map<std::string, Config::ActionHotkeysBridgeSlotPlacement> actualLayout;

	std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
	const int wheelCount = Wheeler::GetWheelCount();
	for (int runtimeWheelIndex = 0; runtimeWheelIndex < wheelCount; ++runtimeWheelIndex) {
		Wheel* wheel = Wheeler::GetWheelByIndex(runtimeWheelIndex);
		if (!wheel) {
			continue;
		}

		const auto wheelNumber = ParseWheelNumberFromTag(wheel->GetClientTag());
		if (!wheelNumber.has_value()) {
			continue;
		}

		const int entryCount = wheel->GetNumEntries();
		for (int entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
			WheelEntry* entry = wheel->GetEntry(entryIndex);
			if (!entry) {
				continue;
			}

			auto selected = entry->GetSelectedItem();
			auto* external = selected ? dynamic_cast<WheelItemExternalHotkey*>(selected.get()) : nullptr;
			if (!external || external->GetSourceTag().empty()) {
				continue;
			}

			actualLayout[external->GetSourceTag()] = Config::ActionHotkeysBridgeSlotPlacement{
				*wheelNumber,
				static_cast<std::uint32_t>(entryIndex)
			};
		}
	}
	lock.unlock();

	PersistLayoutIfChanged(actualLayout);
}

void ActionHotkeysBridge::OnWheelClosed()
{
	const int activeIdx = Wheeler::GetActiveWheelIndex();
	if (!IsBridgeWheelIndex(activeIdx)) {
		s_state.navigationRestricted = false;
		return;
	}

	if (auto restoreIdx = ResolvePreviousUserWheelIndex(); restoreIdx.has_value()) {
		Wheeler::SetActiveWheelIndex(*restoreIdx);
	}

	s_state.navigationRestricted = false;
}
