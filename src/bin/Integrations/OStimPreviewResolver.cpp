#include "OStimPreviewResolver.h"
#include "OStimSceneSemantic.h"

#include "bin/Config.h"
#include "bin/Rendering/TextureManager.h"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace
{
	constexpr const char* kMappingPath = "Data\\SKSE\\Plugins\\wheeler\\resources\\ostim\\mapping.ini";
	constexpr const char* kPreviewRoot = "Data\\SKSE\\Plugins\\wheeler\\resources\\ostim\\previews";
	constexpr const char* kIconRoot = "Data\\SKSE\\Plugins\\wheeler\\resources\\ostim\\icons";
	constexpr const char* kCategoryRoot = "Data\\SKSE\\Plugins\\wheeler\\resources\\ostim\\categories";
	constexpr const char* kSceneRoot = "Data\\SKSE\\Plugins\\OStim\\scenes";
	constexpr const char* kOStimIconRoot = "Data\\Interface\\OStim\\icons";

	struct MappingCache
	{
		bool loaded = false;
		std::filesystem::file_time_type writeTime{};
		std::unordered_map<std::string, std::string> previewByScene;
		std::unordered_map<std::string, std::string> previewByCategory;
		std::unordered_map<std::string, std::string> previewByModule;
		std::unordered_map<std::string, std::string> iconByScene;
		std::unordered_map<std::string, std::string> iconByCategory;
		std::unordered_map<std::string, std::string> iconByModule;
	};

	struct SceneNavigationMetadata
	{
		std::string destination;
		std::string description;
		std::string icon;
	};

	struct SceneMetadata
	{
		std::string displayName;
		std::vector<SceneNavigationMetadata> navigations;
		std::vector<std::string> tags;
		std::vector<std::string> actions;
		OStimSceneSemanticMetadata semanticMetadata;
		bool isTransition = false;
		std::string transitionDestination;
	};

	struct SceneCache
	{
		bool loaded = false;
		bool supplementalRootsLoaded = false;
		std::unordered_map<std::string, SceneMetadata> scenes;
		std::unordered_map<std::string, std::string> iconByDestination;
		std::unordered_map<std::string, std::string> iconByStem;
		std::unordered_map<std::string, std::string> iconByRelativePath;
		std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> iconEntriesByDirectory;
		std::vector<std::pair<std::string, std::string>> iconStemEntries;
		std::unordered_set<std::string> semanticDiagnosticsLogged;
	};

	std::mutex s_cacheLock;
	MappingCache s_cache;
	SceneCache s_sceneCache;

	std::string NormalizeKey(std::string_view a_value)
	{
		std::string out;
		out.reserve(a_value.size());
		for (char c : a_value) {
			out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		return out;
	}

	std::string Utf8FromPath(const std::filesystem::path& a_path)
	{
		const auto utf8 = a_path.generic_u8string();
		std::string out;
		out.reserve(utf8.size());
		for (char8_t ch : utf8) {
			out.push_back(static_cast<char>(ch));
		}
		return out;
	}

	bool ContainsNormalized(const std::vector<std::string>& a_values, std::string_view a_value)
	{
		const std::string needle = NormalizeKey(a_value);
		return std::find(a_values.begin(), a_values.end(), needle) != a_values.end();
	}

	bool IsReturnNavigationIcon(std::string_view a_icon)
	{
		std::string key = NormalizeKey(a_icon);
		std::replace(key.begin(), key.end(), '\\', '/');
		return key.contains("ostim/symbols/return") ||
		       key.contains("oare/oare_return") ||
		       key.ends_with("/return") ||
		       key.ends_with("_return");
	}

	bool IsPlaceholderNavigationIcon(std::string_view a_icon)
	{
		std::string key = NormalizeKey(a_icon);
		std::replace(key.begin(), key.end(), '\\', '/');
		return key.contains("ostim/symbols/placeholder") ||
		       key == "logo";
	}

	bool IsGenericNavigationIcon(std::string_view a_icon)
	{
		return IsReturnNavigationIcon(a_icon) ||
		       IsPlaceholderNavigationIcon(a_icon);
	}

	std::string TrimCopy(std::string_view a_value)
	{
		std::size_t start = 0;
		std::size_t end = a_value.size();
		while (start < end && std::isspace(static_cast<unsigned char>(a_value[start]))) {
			++start;
		}
		while (end > start && std::isspace(static_cast<unsigned char>(a_value[end - 1]))) {
			--end;
		}
		return std::string(a_value.substr(start, end - start));
	}

	std::string NormalizeSlashes(std::string a_value)
	{
		std::replace(a_value.begin(), a_value.end(), '/', '\\');
		return a_value;
	}

	bool StartsWithInsensitive(std::string_view a_value, std::string_view a_prefix)
	{
		if (a_value.size() < a_prefix.size()) {
			return false;
		}

		for (std::size_t i = 0; i < a_prefix.size(); ++i) {
			if (std::tolower(static_cast<unsigned char>(a_value[i])) !=
				std::tolower(static_cast<unsigned char>(a_prefix[i]))) {
				return false;
			}
		}

		return true;
	}

	bool EndsWithInsensitive(std::string_view a_value, std::string_view a_suffix)
	{
		if (a_value.size() < a_suffix.size()) {
			return false;
		}

		const std::size_t offset = a_value.size() - a_suffix.size();
		for (std::size_t i = 0; i < a_suffix.size(); ++i) {
			if (std::tolower(static_cast<unsigned char>(a_value[offset + i])) !=
				std::tolower(static_cast<unsigned char>(a_suffix[i]))) {
				return false;
			}
		}

		return true;
	}

	std::string StripRelativePrefix(std::string_view a_value)
	{
		std::string out = NormalizeSlashes(TrimCopy(a_value));
		while (out.starts_with(".\\") || out.starts_with("./")) {
			out.erase(0, 2);
		}
		return out;
	}

	std::string TrimToKnownIconRoot(std::string_view a_value)
	{
		std::string path = StripRelativePrefix(a_value);
		const std::string normalized = NormalizeKey(path);

		const auto tryTrim = [&](std::string_view a_marker) -> std::optional<std::string> {
			const auto offset = normalized.find(std::string(a_marker));
			if (offset == std::string::npos) {
				return std::nullopt;
			}
			return path.substr(offset);
		};

		if (auto trimmed = tryTrim("data\\interface\\ostim\\icons\\"); trimmed.has_value()) {
			return *trimmed;
		}
		if (auto trimmed = tryTrim("interface\\ostim\\icons\\"); trimmed.has_value()) {
			return *trimmed;
		}
		if (auto trimmed = tryTrim("ostim\\icons\\"); trimmed.has_value()) {
			return *trimmed;
		}

		return path;
	}

	std::string NormalizeRelativeIconKey(std::string_view a_value)
	{
		std::string key = NormalizeKey(NormalizeSlashes(TrimToKnownIconRoot(a_value)));
		if (StartsWithInsensitive(key, "data\\")) {
			key.erase(0, 5);
		}
		if (StartsWithInsensitive(key, "interface\\ostim\\icons\\")) {
			key.erase(0, std::char_traits<char>::length("interface\\ostim\\icons\\"));
		} else if (StartsWithInsensitive(key, "ostim\\icons\\")) {
			key.erase(0, std::char_traits<char>::length("ostim\\icons\\"));
		}
		return key;
	}

	std::optional<std::string> ResolveKnownRelativeIconAlias(std::string_view a_value)
	{
		const std::string key = NormalizeRelativeIconKey(a_value);
		if (key == "ostim\\sensual\\lips") {
			return std::string("ostim\\sensual\\kissing_mf");
		}
		if (key == "ostim\\positional\\sitting_f") {
			return std::string("ostim\\positional\\chair\\sitting_f");
		}
		if (key == "ostim\\positional\\table\\sitting_f") {
			return std::string("ostim\\positional\\chair\\sitting_f");
		}
		if (key == "ostim\\sexual\\lyingback_f") {
			return std::string("ostim\\positional\\lyingback_f");
		}
		if (key == "oare\\male_approach_female_all-fours\\acerearkneelingsexvariation") {
			return std::string("oare\\male_approach_female_all-fours\\rearsexfperformer");
		}
		return std::nullopt;
	}

	std::string SanitizeFileStem(std::string_view a_value)
	{
		std::string out;
		out.reserve(a_value.size());
		bool previousUnderscore = false;
		for (char c : a_value) {
			const unsigned char uc = static_cast<unsigned char>(c);
			if (std::isalnum(uc)) {
				out.push_back(static_cast<char>(std::tolower(uc)));
				previousUnderscore = false;
			} else if (!previousUnderscore) {
				out.push_back('_');
				previousUnderscore = true;
			}
		}
		while (!out.empty() && out.back() == '_') {
			out.pop_back();
		}
		return out;
	}

	std::string CanonicalizeIconStem(std::string_view a_value)
	{
		const std::string stem = NormalizeKey(a_value);
		if (stem == "doggy_mf") {
			return "doggystyle_mf";
		}
		if (stem == "holdinhead_mf") {
			return "holdinghead_mf";
		}
		if (stem == "lips") {
			return "kissing";
		}
		return stem;
	}

	std::string NormalizeSearchStem(std::string_view a_value)
	{
		std::string out;
		out.reserve(a_value.size() * 2);
		bool previousSeparator = true;
		char previous = '\0';

		for (char c : a_value) {
			const unsigned char uc = static_cast<unsigned char>(c);
			if (!std::isalnum(uc)) {
				if (!out.empty() && out.back() != '_') {
					out.push_back('_');
				}
				previousSeparator = true;
				previous = static_cast<char>(uc);
				continue;
			}

			const bool isUpper = std::isupper(uc) != 0;
			const bool previousIsLower = std::islower(static_cast<unsigned char>(previous)) != 0;
			const bool previousIsDigit = std::isdigit(static_cast<unsigned char>(previous)) != 0;
			const bool currentIsDigit = std::isdigit(uc) != 0;
			if (!previousSeparator && out.back() != '_' &&
			    ((isUpper && previousIsLower) || (currentIsDigit && std::isalpha(static_cast<unsigned char>(previous))) ||
			     (!currentIsDigit && previousIsDigit))) {
				out.push_back('_');
			}

			out.push_back(static_cast<char>(std::tolower(uc)));
			previousSeparator = false;
			previous = static_cast<char>(uc);
		}

		while (!out.empty() && out.back() == '_') {
			out.pop_back();
		}

		return out;
	}

	std::unordered_set<std::string> SplitSearchTokens(std::string_view a_value)
	{
		std::unordered_set<std::string> tokens;
		std::string normalized = NormalizeSearchStem(a_value);
		std::size_t start = 0;
		while (start < normalized.size()) {
			const std::size_t split = normalized.find('_', start);
			const std::size_t length = split == std::string::npos ? normalized.size() - start : split - start;
			if (length > 0) {
				tokens.emplace(normalized.substr(start, length));
			}
			if (split == std::string::npos) {
				break;
			}
			start = split + 1;
		}
		return tokens;
	}

	std::size_t ComputeEditDistance(std::string_view a_lhs, std::string_view a_rhs);
	const SceneMetadata* ResolveSceneMetadataWithFallback(std::string_view a_sceneID);

	int ScoreDirectoryCandidate(std::string_view a_queryStem, std::string_view a_candidateStem)
	{
		const std::string query = NormalizeSearchStem(CanonicalizeIconStem(a_queryStem));
		const std::string candidate = NormalizeSearchStem(CanonicalizeIconStem(a_candidateStem));
		if (query.empty() || candidate.empty()) {
			return (std::numeric_limits<int>::min)();
		}

		int score = 0;
		if (query == candidate) {
			score += 2000;
		}
		if (query.starts_with(candidate) || candidate.starts_with(query)) {
			score += 400;
		} else if (query.contains(candidate) || candidate.contains(query)) {
			score += 180;
		}

		const auto queryTokens = SplitSearchTokens(query);
		const auto candidateTokens = SplitSearchTokens(candidate);
		for (const auto& token : queryTokens) {
			if (candidateTokens.contains(token)) {
				score += 90;
			}
		}

		const int editDistance = static_cast<int>(ComputeEditDistance(query, candidate));
		score -= editDistance * 6;
		score -= static_cast<int>(std::abs(static_cast<int>(query.size()) - static_cast<int>(candidate.size())));
		return score;
	}

	std::size_t ComputeEditDistance(std::string_view a_lhs, std::string_view a_rhs)
	{
		const std::size_t lhsSize = a_lhs.size();
		const std::size_t rhsSize = a_rhs.size();
		if (lhsSize == 0) {
			return rhsSize;
		}
		if (rhsSize == 0) {
			return lhsSize;
		}

		std::vector<std::size_t> previous(rhsSize + 1);
		std::vector<std::size_t> current(rhsSize + 1);
		for (std::size_t j = 0; j <= rhsSize; ++j) {
			previous[j] = j;
		}

		for (std::size_t i = 0; i < lhsSize; ++i) {
			current[0] = i + 1;
			for (std::size_t j = 0; j < rhsSize; ++j) {
				const std::size_t cost = a_lhs[i] == a_rhs[j] ? 0 : 1;
				current[j + 1] = (std::min)({
					previous[j + 1] + 1,
					current[j] + 1,
					previous[j] + cost
				});
			}
			std::swap(previous, current);
		}

		return previous[rhsSize];
	}

	void LoadSectionMap(const CSimpleIniA& a_ini, const char* a_section, std::unordered_map<std::string, std::string>& a_out)
	{
		a_out.clear();
		CSimpleIniA::TNamesDepend keys;
		a_ini.GetAllKeys(a_section, keys);
		for (const auto& key : keys) {
			if (!key.pItem) {
				continue;
			}

			const char* value = a_ini.GetValue(a_section, key.pItem, "");
			if (!value || value[0] == '\0') {
				continue;
			}

			a_out.emplace(NormalizeKey(key.pItem), std::string(value));
		}
	}

	void ReloadCacheLocked()
	{
		s_cache = MappingCache{};

		CSimpleIniA ini;
		if (ini.LoadFile(kMappingPath) < 0) {
			s_cache.loaded = true;
			return;
		}

		LoadSectionMap(ini, "Preview.ByScene", s_cache.previewByScene);
		LoadSectionMap(ini, "Preview.ByCategory", s_cache.previewByCategory);
		LoadSectionMap(ini, "Preview.ByModule", s_cache.previewByModule);
		LoadSectionMap(ini, "Icon.ByScene", s_cache.iconByScene);
		LoadSectionMap(ini, "Icon.ByCategory", s_cache.iconByCategory);
		LoadSectionMap(ini, "Icon.ByModule", s_cache.iconByModule);

		std::error_code ec;
		s_cache.writeTime = std::filesystem::last_write_time(kMappingPath, ec);
		s_cache.loaded = true;
		Texture::InvalidateExternalRasterCache();
	}

	void IndexIconRoot(const std::filesystem::path& a_iconRoot)
	{
		std::error_code ec;
		std::filesystem::recursive_directory_iterator iconIt(a_iconRoot, ec);
		const std::filesystem::recursive_directory_iterator end{};
		if (ec) {
			return;
		}

		for (; iconIt != end; iconIt.increment(ec)) {
			if (ec) {
				ec.clear();
				continue;
			}

			const auto& entry = *iconIt;
			if (!entry.is_regular_file(ec) || ec) {
				ec.clear();
				continue;
			}

			const auto iconPath = entry.path();
			if (!iconPath.has_extension()) {
				continue;
			}

			const std::string extension = NormalizeKey(Utf8FromPath(iconPath.extension()));
			if (extension != ".dds" && extension != ".png") {
				continue;
			}

			std::error_code relativeEc;
			const auto relative = std::filesystem::relative(iconPath, a_iconRoot, relativeEc);
			if (relativeEc) {
				continue;
			}

			const std::string stem = NormalizeKey(Utf8FromPath(iconPath.stem()));
			const std::filesystem::path resolvedPath = std::filesystem::path(kOStimIconRoot) / relative;
			const std::string resolvedPathUtf8 = Utf8FromPath(resolvedPath.lexically_normal());
			const std::string relativeKey = NormalizeKey(NormalizeSlashes(Utf8FromPath(relative.lexically_normal())));
			if (relativeKey.empty()) {
				continue;
			}

			s_sceneCache.iconStemEntries.emplace_back(stem, resolvedPathUtf8);
			s_sceneCache.iconByStem.try_emplace(stem, resolvedPathUtf8);
			s_sceneCache.iconByRelativePath.try_emplace(relativeKey, resolvedPathUtf8);
			const std::string directoryKey = NormalizeKey(NormalizeSlashes(Utf8FromPath(relative.parent_path())));
			s_sceneCache.iconEntriesByDirectory[directoryKey].emplace_back(stem, resolvedPathUtf8);
			const auto extensionOffset = relativeKey.find_last_of('.');
			if (extensionOffset != std::string::npos) {
				s_sceneCache.iconByRelativePath.try_emplace(relativeKey.substr(0, extensionOffset), resolvedPathUtf8);
			}
		}
	}

	void IndexSceneRoot(const std::filesystem::path& a_sceneRoot)
	{
		std::error_code ec;
		std::filesystem::recursive_directory_iterator it(a_sceneRoot, ec);
		const std::filesystem::recursive_directory_iterator end{};
		if (ec) {
			return;
		}

		for (; it != end; it.increment(ec)) {
			if (ec) {
				ec.clear();
				continue;
			}

			const auto& entry = *it;
			if (!entry.is_regular_file(ec) || ec) {
				ec.clear();
				continue;
			}

			const auto path = entry.path();
			if (!path.has_extension() || NormalizeKey(Utf8FromPath(path.extension())) != ".json") {
				continue;
			}

			std::ifstream input(path);
			if (!input.good()) {
				continue;
			}

			const std::string jsonText(
				std::istreambuf_iterator<char>{ input },
				std::istreambuf_iterator<char>{});
			const auto parsedJson = TryParseOStimSceneJson(jsonText);
			if (!parsedJson.has_value()) {
				continue;
			}
			const nlohmann::json& json = *parsedJson;

			const std::string sceneID = Utf8FromPath(path.stem());
			if (sceneID.empty()) {
				continue;
			}

			const std::string normalizedSceneID = NormalizeKey(sceneID);
			if (s_sceneCache.scenes.contains(normalizedSceneID)) {
				continue;
			}

			SceneMetadata metadata{};
			metadata.semanticMetadata = ClassifyOStimSceneMetadata(&json);
			if (json.contains("name") && json["name"].is_string()) {
				metadata.displayName = json["name"].get<std::string>();
			}
			if (json.contains("destination") && json["destination"].is_string()) {
				metadata.isTransition = true;
				metadata.transitionDestination = json["destination"].get<std::string>();
			}
			if (json.contains("tags") && json["tags"].is_array()) {
				for (const auto& tag : json["tags"]) {
					if (tag.is_string()) {
						metadata.tags.push_back(NormalizeKey(tag.get<std::string>()));
					}
				}
			}
			if (json.contains("actions") && json["actions"].is_array()) {
				for (const auto& action : json["actions"]) {
					if (!action.is_object()) {
						continue;
					}
					if (action.contains("type") && action["type"].is_string()) {
						metadata.actions.push_back(NormalizeKey(action["type"].get<std::string>()));
					}
				}
			}

			auto appendNavigation = [&](const nlohmann::json& navigation) {
				if (!navigation.is_object()) {
					return;
				}

				SceneNavigationMetadata navMetadata{};
				if (navigation.contains("destination") && navigation["destination"].is_string()) {
					navMetadata.destination = navigation["destination"].get<std::string>();
				}
				if (navigation.contains("description") && navigation["description"].is_string()) {
					navMetadata.description = navigation["description"].get<std::string>();
				}
				if (navigation.contains("icon") && navigation["icon"].is_string()) {
					navMetadata.icon = navigation["icon"].get<std::string>();
				}

				if (!navMetadata.destination.empty() &&
					(!navMetadata.icon.empty() || !navMetadata.description.empty())) {
					if (!navMetadata.icon.empty() && !IsGenericNavigationIcon(navMetadata.icon)) {
						s_sceneCache.iconByDestination.try_emplace(
							NormalizeKey(navMetadata.destination),
							navMetadata.icon);
					}
					metadata.navigations.push_back(std::move(navMetadata));
				}
			};

			if (json.contains("navigations") && json["navigations"].is_array()) {
				for (const auto& navigation : json["navigations"]) {
					appendNavigation(navigation);
				}
			}

			if (json.contains("destination") && json["destination"].is_string()) {
				appendNavigation(json);
			}

			s_sceneCache.scenes.try_emplace(normalizedSceneID, std::move(metadata));
		}
	}

	void LoadSceneCacheLocked()
	{
		s_sceneCache = SceneCache{};

		IndexIconRoot(kOStimIconRoot);
		IndexSceneRoot(kSceneRoot);

		s_sceneCache.loaded = true;
	}

	void LoadSupplementalSceneCacheLocked()
	{
		if (!s_sceneCache.loaded) {
			LoadSceneCacheLocked();
		}
		if (s_sceneCache.supplementalRootsLoaded) {
			return;
		}

		s_sceneCache.supplementalRootsLoaded = true;
	}

	void EnsureCacheLoaded()
	{
		std::lock_guard lock(s_cacheLock);

		std::error_code ec;
		const auto writeTime = std::filesystem::last_write_time(kMappingPath, ec);
		if (!s_cache.loaded || (!ec && writeTime != s_cache.writeTime)) {
			ReloadCacheLocked();
		}
	}

	std::optional<std::string> Lookup(const std::unordered_map<std::string, std::string>& a_map, std::string_view a_key)
	{
		if (a_key.empty()) {
			return std::nullopt;
		}

		if (auto it = a_map.find(NormalizeKey(a_key)); it != a_map.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	void AddPathCandidate(std::vector<std::filesystem::path>& a_candidates, const std::filesystem::path& a_path)
	{
		if (a_path.empty()) {
			return;
		}

		a_candidates.push_back(a_path.lexically_normal());
		if (!a_path.has_extension()) {
			std::filesystem::path ddsPath = a_path;
			ddsPath += ".dds";
			a_candidates.push_back(ddsPath.lexically_normal());
			std::filesystem::path pngPath = a_path;
			pngPath += ".png";
			a_candidates.push_back(pngPath.lexically_normal());
		}
	}

	void EnsureSceneCacheLoaded()
	{
		std::lock_guard lock(s_cacheLock);
		if (!s_sceneCache.loaded) {
			LoadSceneCacheLocked();
		}
	}

	void EnsureSupplementalSceneCacheLoaded()
	{
		std::lock_guard lock(s_cacheLock);
		LoadSupplementalSceneCacheLocked();
	}

	std::optional<std::string> FindIconByStem(std::string_view a_value)
	{
		const std::string stem = CanonicalizeIconStem(
			Utf8FromPath(std::filesystem::path(StripRelativePrefix(a_value)).stem()));
		if (stem.empty()) {
			return std::nullopt;
		}

		if (auto it = s_sceneCache.iconByStem.find(stem); it != s_sceneCache.iconByStem.end()) {
			return it->second;
		}

		std::optional<std::string> bestMatch;
		std::size_t bestDistance = 3;
		for (const auto& [iconStem, iconPath] : s_sceneCache.iconStemEntries) {
			const std::size_t distance = ComputeEditDistance(stem, iconStem);
			if (distance < bestDistance) {
				bestDistance = distance;
				bestMatch = iconPath;
				if (distance == 1) {
					break;
				}
			}
		}

		return bestMatch;
	}

	std::optional<std::string> FindIconByRelativePath(std::string_view a_value)
	{
		std::string key = NormalizeRelativeIconKey(a_value);
		if (key.empty()) {
			return std::nullopt;
		}

		if (auto it = s_sceneCache.iconByRelativePath.find(key); it != s_sceneCache.iconByRelativePath.end()) {
			return it->second;
		}

		const std::filesystem::path keyPath(key);
		if (!keyPath.has_extension()) {
			if (auto it = s_sceneCache.iconByRelativePath.find(key + ".dds"); it != s_sceneCache.iconByRelativePath.end()) {
				return it->second;
			}
			if (auto it = s_sceneCache.iconByRelativePath.find(key + ".png"); it != s_sceneCache.iconByRelativePath.end()) {
				return it->second;
			}
		}

		if (auto alias = ResolveKnownRelativeIconAlias(key); alias.has_value()) {
			if (auto it = s_sceneCache.iconByRelativePath.find(*alias); it != s_sceneCache.iconByRelativePath.end()) {
				return it->second;
			}
			if (auto it = s_sceneCache.iconByRelativePath.find(*alias + ".dds"); it != s_sceneCache.iconByRelativePath.end()) {
				return it->second;
			}
			if (auto it = s_sceneCache.iconByRelativePath.find(*alias + ".png"); it != s_sceneCache.iconByRelativePath.end()) {
				return it->second;
			}
		}

		const std::string canonicalStem = CanonicalizeIconStem(Utf8FromPath(keyPath.stem()));
		if (canonicalStem != NormalizeKey(Utf8FromPath(keyPath.stem()))) {
			const std::string parentKey = NormalizeKey(NormalizeSlashes(Utf8FromPath(keyPath.parent_path())));
			const std::string canonicalKey = parentKey.empty() ? canonicalStem : parentKey + "\\" + canonicalStem;
			if (auto it = s_sceneCache.iconByRelativePath.find(canonicalKey); it != s_sceneCache.iconByRelativePath.end()) {
				return it->second;
			}
			if (auto it = s_sceneCache.iconByRelativePath.find(canonicalKey + ".dds"); it != s_sceneCache.iconByRelativePath.end()) {
				return it->second;
			}
			if (auto it = s_sceneCache.iconByRelativePath.find(canonicalKey + ".png"); it != s_sceneCache.iconByRelativePath.end()) {
				return it->second;
			}
		}

		const std::string directoryKey = NormalizeKey(NormalizeSlashes(Utf8FromPath(keyPath.parent_path())));
		if (auto directoryIt = s_sceneCache.iconEntriesByDirectory.find(directoryKey); directoryIt != s_sceneCache.iconEntriesByDirectory.end()) {
			const std::string queryStem = Utf8FromPath(keyPath.stem());
			const std::string canonicalQueryStem = CanonicalizeIconStem(queryStem);
			int bestScore = (std::numeric_limits<int>::min)();
			std::optional<std::string> bestMatch;
			for (const auto& [candidateStem, candidatePath] : directoryIt->second) {
				const int score = ScoreDirectoryCandidate(canonicalQueryStem, candidateStem);
				if (score > bestScore) {
					bestScore = score;
					bestMatch = candidatePath;
				}
			}
			if (bestMatch.has_value() && bestScore >= 120) {
				return bestMatch;
			}
		}

		return std::nullopt;
	}

	std::optional<std::string> ResolveMetadataAssetPath(std::string_view a_value, bool a_allowSupplementalLoad = true)
	{
		const std::string stripped = TrimToKnownIconRoot(a_value);
		if (stripped.empty()) {
			return std::nullopt;
		}

		const std::filesystem::path rawPath(stripped);
		if (rawPath.is_absolute()) {
			return Utf8FromPath(rawPath.lexically_normal());
		}

		std::vector<std::filesystem::path> candidates;
		if (StartsWithInsensitive(stripped, "Data\\")) {
			AddPathCandidate(candidates, rawPath);
		} else if (StartsWithInsensitive(stripped, "Interface\\")) {
			AddPathCandidate(candidates, std::filesystem::path("Data") / rawPath);
		} else if (StartsWithInsensitive(stripped, "OStim\\icons\\")) {
			AddPathCandidate(candidates, std::filesystem::path("Data\\Interface") / rawPath);
		} else if (StartsWithInsensitive(stripped, "OStim\\")) {
			AddPathCandidate(candidates, std::filesystem::path("Data\\Interface\\OStim\\icons") / rawPath);
		} else {
			AddPathCandidate(candidates, rawPath);
		}

		std::error_code ec;
		for (const auto& candidate : candidates) {
			if (std::filesystem::exists(candidate, ec) && !ec) {
				return Utf8FromPath(candidate);
			}
			ec.clear();
		}

		const auto resolveIndexedPath = [&]() -> std::optional<std::string> {
			if (auto byRelativePath = FindIconByRelativePath(stripped); byRelativePath.has_value()) {
				return byRelativePath;
			}
			if (auto byStem = FindIconByStem(stripped); byStem.has_value()) {
				return byStem;
			}
			return std::nullopt;
		};

		if (auto indexedPath = resolveIndexedPath(); indexedPath.has_value()) {
			return indexedPath;
		}

		if (a_allowSupplementalLoad) {
			EnsureSupplementalSceneCacheLoaded();
			if (auto indexedPath = resolveIndexedPath(); indexedPath.has_value()) {
				return indexedPath;
			}
		}

		if (!candidates.empty()) {
			return Utf8FromPath(candidates.front());
		}
		return std::nullopt;
	}

	std::optional<std::string> ResolveMetadataAssetPathLocked(std::string_view a_value)
	{
		if (auto resolved = ResolveMetadataAssetPath(a_value, false); resolved.has_value()) {
			return resolved;
		}

		if (!s_sceneCache.supplementalRootsLoaded) {
			LoadSupplementalSceneCacheLocked();
			return ResolveMetadataAssetPath(a_value, false);
		}

		return std::nullopt;
	}

	std::optional<std::string> FindExistingPng(std::string_view a_root, std::string_view a_name)
	{
		if (a_name.empty()) {
			return std::nullopt;
		}

		const std::string stem = SanitizeFileStem(a_name);
		if (stem.empty()) {
			return std::nullopt;
		}

		std::filesystem::path path = std::filesystem::path(a_root) / (stem + ".png");
		std::error_code ec;
		if (!std::filesystem::exists(path, ec) || ec) {
			return std::nullopt;
		}

		return Utf8FromPath(path);
	}

	std::optional<std::string> ResolvePath(
		const OStimPositionInfo& a_position,
		const std::unordered_map<std::string, std::string>& a_byScene,
		const std::unordered_map<std::string, std::string>& a_byCategory,
		const std::unordered_map<std::string, std::string>& a_byModule,
		std::string_view a_root)
	{
		auto resolveScene = [&](std::string_view a_sceneKey) -> std::optional<std::string> {
			if (auto scene = Lookup(a_byScene, a_sceneKey); scene.has_value()) {
				return scene;
			}
			if (auto scenePng = FindExistingPng(a_root, a_sceneKey); scenePng.has_value()) {
				return scenePng;
			}
			return std::nullopt;
		};

		if (auto destination = resolveScene(a_position.destinationID); destination.has_value()) {
			return destination;
		}
		if (auto scene = resolveScene(a_position.id); scene.has_value()) {
			return scene;
		}
		if (auto module = Lookup(a_byModule, a_position.subcategory); module.has_value()) {
			return module;
		}
		if (auto category = Lookup(a_byCategory, a_position.category); category.has_value()) {
			return category;
		}
		if (auto modulePng = FindExistingPng(a_root, a_position.subcategory); modulePng.has_value()) {
			return modulePng;
		}
		return std::nullopt;
	}

	std::optional<std::string> ResolveSceneNavigationAssetPath(const OStimPositionInfo& a_position)
	{
		if (a_position.sourceSceneID.empty()) {
			return std::nullopt;
		}

		const SceneMetadata* sourceMetadata = ResolveSceneMetadataWithFallback(a_position.sourceSceneID);
		if (!sourceMetadata) {
			return std::nullopt;
		}

		auto matchesTarget = [&](std::string_view a_destination) {
			return (!a_position.destinationID.empty() && NormalizeKey(a_destination) == NormalizeKey(a_position.destinationID)) ||
			       (!a_position.id.empty() && NormalizeKey(a_destination) == NormalizeKey(a_position.id));
		};

		for (const auto& navigation : sourceMetadata->navigations) {
			if (!matchesTarget(navigation.destination)) {
				continue;
			}

			if (!IsGenericNavigationIcon(navigation.icon)) {
				if (auto resolvedIcon = ResolveMetadataAssetPathLocked(navigation.icon); resolvedIcon.has_value()) {
					return resolvedIcon;
				}
			}
		}

		auto resolveByDestination = [&](std::string_view a_destination) -> std::optional<std::string> {
			if (a_destination.empty()) {
				return std::nullopt;
			}

			const auto destinationIt = s_sceneCache.iconByDestination.find(NormalizeKey(a_destination));
			if (destinationIt == s_sceneCache.iconByDestination.end()) {
				return std::nullopt;
			}

			if (IsGenericNavigationIcon(destinationIt->second)) {
				return std::nullopt;
			}

			if (auto resolved = ResolveMetadataAssetPathLocked(destinationIt->second); resolved.has_value() &&
			    !IsGenericNavigationIcon(*resolved)) {
				return resolved;
			}
			return std::nullopt;
		};

		if (auto destinationIcon = resolveByDestination(a_position.destinationID); destinationIcon.has_value()) {
			return destinationIcon;
		}
		if (auto sceneIcon = resolveByDestination(a_position.id); sceneIcon.has_value()) {
			return sceneIcon;
		}

		return std::nullopt;
	}

	const SceneMetadata* ResolveSceneMetadataDirect(std::string_view a_sceneID)
	{
		if (a_sceneID.empty()) {
			return nullptr;
		}

		const auto it = s_sceneCache.scenes.find(NormalizeKey(a_sceneID));
		return it == s_sceneCache.scenes.end() ? nullptr : &it->second;
	}

	const SceneMetadata* ResolveSceneMetadataDirectWithFallback(std::string_view a_sceneID)
	{
		if (const SceneMetadata* metadata = ResolveSceneMetadataDirect(a_sceneID); metadata) {
			return metadata;
		}

		if (!s_sceneCache.supplementalRootsLoaded) {
			LoadSupplementalSceneCacheLocked();
			return ResolveSceneMetadataDirect(a_sceneID);
		}

		return nullptr;
	}

	void ApplySceneSemanticMetadataLocked(OStimPositionInfo& a_position)
	{
		const std::string_view sceneID = !a_position.destinationID.empty() ?
			a_position.destinationID : a_position.id;
		const SceneMetadata* sceneMetadata = ResolveSceneMetadataDirectWithFallback(sceneID);
		const OStimSceneSemanticMetadata baseSemantic = sceneMetadata ?
			sceneMetadata->semanticMetadata : OStimSceneSemanticMetadata{};
		const OStimSceneSemanticMetadata semantic = ClassifyOStimNavigationSemantic(
			baseSemantic,
			a_position.isTransition,
			IsCanonicalOStimReturnIcon(a_position.iconPath));
		a_position.semantic = semantic.semantic;

		if (!Config::OStimIntegration::DebugLog) {
			return;
		}

		std::string diagnosticKey = NormalizeKey(sceneID);
		diagnosticKey += '|';
		diagnosticKey += GetOStimSceneSemanticName(semantic.semantic);
		diagnosticKey += '|';
		diagnosticKey += GetOStimSceneSemanticReasonName(semantic.reason);
		if (s_sceneCache.semanticDiagnosticsLogged.insert(std::move(diagnosticKey)).second) {
			logger::info(
				"[OStimDiag] SCENE_SEMANTIC sceneID='{}' semantic={} metadata={} reason={}",
				sceneID,
				GetOStimSceneSemanticName(semantic.semantic),
				semantic.metadataFound ? 1 : 0,
				GetOStimSceneSemanticReasonName(semantic.reason));
		}
	}

	const SceneMetadata* ResolveSceneMetadata(std::string_view a_sceneID)
	{
		if (a_sceneID.empty()) {
			return nullptr;
		}

		const SceneMetadata* metadata = nullptr;
		std::string current = NormalizeKey(a_sceneID);
		for (int depth = 0; depth < 4; ++depth) {
			const auto it = s_sceneCache.scenes.find(current);
			if (it == s_sceneCache.scenes.end()) {
				return metadata;
			}

			metadata = &it->second;
			if (!metadata->isTransition || metadata->transitionDestination.empty()) {
				return metadata;
			}

			current = NormalizeKey(metadata->transitionDestination);
		}

		return metadata;
	}

	const SceneMetadata* ResolveSceneMetadataWithFallback(std::string_view a_sceneID)
	{
		if (const SceneMetadata* metadata = ResolveSceneMetadata(a_sceneID); metadata) {
			return metadata;
		}

		if (!s_sceneCache.supplementalRootsLoaded) {
			LoadSupplementalSceneCacheLocked();
			return ResolveSceneMetadata(a_sceneID);
		}

		return nullptr;
	}

	std::optional<std::string> ResolveLegacySceneIconPath(const OStimPositionInfo& a_position)
	{
		const SceneMetadata* metadata = ResolveSceneMetadataWithFallback(
			!a_position.destinationID.empty() ? a_position.destinationID : a_position.id);
		if (!metadata) {
			return std::nullopt;
		}

		auto resolveLegacyIcon = [&](std::string_view a_iconKey) -> std::optional<std::string> {
			if (auto resolved = ResolveMetadataAssetPathLocked(a_iconKey); resolved.has_value() &&
			    !IsGenericNavigationIcon(*resolved)) {
				return resolved;
			}
			return std::nullopt;
		};

		const std::string normalizedSceneKey = NormalizeKey(
			!a_position.destinationID.empty() ? a_position.destinationID : a_position.id);

		if (normalizedSceneKey.contains("reversecowgirl")) return resolveLegacyIcon("OStim/sexual/reversecowgirl_mf");
		if (normalizedSceneKey.contains("cowgirl")) return resolveLegacyIcon("OStim/sexual/cowgirl_mf");
		if (normalizedSceneKey.contains("thighjob")) return resolveLegacyIcon("OStim/sexual/thighjob_mf");
		if (normalizedSceneKey.contains("sidegrind")) return resolveLegacyIcon("OStim/sexual/grindingpenis_mf");
		if (normalizedSceneKey.contains("nipple")) return resolveLegacyIcon("OStim/sexual/suckingnipples_mf");
		if (normalizedSceneKey.contains("kneelinglyingback")) return resolveLegacyIcon("OStim/positional/kneeling_m_end");
		if (normalizedSceneKey.contains("lyingkneeling")) return resolveLegacyIcon("OStim/detail/holdingleg_mf");
		if (normalizedSceneKey.contains("mfpsittinglying")) return resolveLegacyIcon("MFP/mfpicon");
		if (normalizedSceneKey.contains("oare_sitting")) return resolveLegacyIcon("OARE/oare");
		if (normalizedSceneKey.contains("sittingfootjobfrombehind")) return resolveLegacyIcon("OARE/Both_sitting/AceSittingFootjobFromBehind");
		if (normalizedSceneKey.contains("sittingfootjob")) return resolveLegacyIcon("OARE/Both_sitting/SittingFootjob");
		if (normalizedSceneKey.contains("sittingfemaleapproach")) return resolveLegacyIcon("OARE/Both_sitting_female_approach/SittingFemaleApproach");
		if (normalizedSceneKey.contains("drg_addonltn_lybmou_ho_dragohub")) return resolveLegacyIcon("OStim/detail/holdingleg_mf");

		if (ContainsNormalized(metadata->tags, "missionary")) return resolveLegacyIcon("OStim/sexual/missionary_mf");
		if (ContainsNormalized(metadata->tags, "cowgirl")) return resolveLegacyIcon("OStim/sexual/cowgirl_mf");
		if (ContainsNormalized(metadata->tags, "reversecowgirl")) return resolveLegacyIcon("OStim/sexual/reversecowgirl_mf");
		if (ContainsNormalized(metadata->tags, "doggystyle")) return resolveLegacyIcon("OStim/sexual/doggystyle_mf");
		if (ContainsNormalized(metadata->tags, "sixtynine")) return resolveLegacyIcon("OStim/sexual/sixtynine_mf");

		if (ContainsNormalized(metadata->actions, "vaginalsex")) return resolveLegacyIcon("OStim/sexual/vaginalsex_mf");
		if (ContainsNormalized(metadata->actions, "analsex")) return resolveLegacyIcon("OStim/sexual/analsex_mf");
		if (ContainsNormalized(metadata->actions, "blowjob")) return resolveLegacyIcon("OStim/sexual/blowjob_mf");
		if (ContainsNormalized(metadata->actions, "cunnilingus")) return resolveLegacyIcon("OStim/sexual/cunnilingus_mf");
		if (ContainsNormalized(metadata->actions, "boobjob")) return resolveLegacyIcon("OStim/sexual/boobjob_mf");
		if (ContainsNormalized(metadata->actions, "handjob")) return resolveLegacyIcon("OStim/sexual/handjob_mf");
		if (ContainsNormalized(metadata->actions, "thighjob")) return resolveLegacyIcon("OStim/sexual/thighjob_mf");
		if (ContainsNormalized(metadata->actions, "grindingpenis")) return resolveLegacyIcon("OStim/sexual/grindingpenis_mf");
		if (ContainsNormalized(metadata->actions, "vaginalfisting")) return resolveLegacyIcon("OStim/sexual/vaginalfisting_mf");
		if (ContainsNormalized(metadata->actions, "vaginalfingering")) return resolveLegacyIcon("OStim/sexual/vaginalfingering_mf");
		if (ContainsNormalized(metadata->actions, "vampirebite")) return resolveLegacyIcon("OStim/sensual/vampirebite");
		if (ContainsNormalized(metadata->actions, "kissing")) return resolveLegacyIcon("OStim/sensual/kissing_mf");
		if (ContainsNormalized(metadata->actions, "kissingneck")) return resolveLegacyIcon("OStim/sensual/kissingneck_mf");
		if (ContainsNormalized(metadata->actions, "licking")) return resolveLegacyIcon("OStim/sensual/licking");
		if (ContainsNormalized(metadata->actions, "teasing")) return resolveLegacyIcon("OStim/detail/tickle_f");
		if (ContainsNormalized(metadata->actions, "mounting")) return resolveLegacyIcon("OStim/sexual/cowgirl_mf");
		if (ContainsNormalized(metadata->actions, "footjob")) return resolveLegacyIcon("OARE/Both_sitting/SittingFootjob");
		if (ContainsNormalized(metadata->actions, "hug")) return resolveLegacyIcon("OStim/sensual/embrace_mf");
		if (ContainsNormalized(metadata->actions, "holdingbody")) return resolveLegacyIcon("OStim/sensual/embrace_mf");
		if (ContainsNormalized(metadata->actions, "holdingleg")) return resolveLegacyIcon("OStim/detail/holdingleg_mf");
		if (ContainsNormalized(metadata->actions, "holdingthigh")) return resolveLegacyIcon("OStim/detail/holdingleg_mf");
		if (ContainsNormalized(metadata->actions, "gropingbutt")) return resolveLegacyIcon("OStim/detail/gropingbutt_mf");
		if (ContainsNormalized(metadata->actions, "holdinghead")) return resolveLegacyIcon("OStim/detail/holdinghead_mf");
		if (ContainsNormalized(metadata->actions, "holdinghip")) return resolveLegacyIcon("OStim/detail/holdinghip_mf");
		if (ContainsNormalized(metadata->actions, "holdinghand")) return resolveLegacyIcon("OStim/detail/holdinghand_mf");
		if (ContainsNormalized(metadata->actions, "holdingneck")) return resolveLegacyIcon("OStim/detail/holdingneck_mf");

		return std::nullopt;
	}
}

void OStimPreviewResolver::Apply(OStimPositionInfo& a_position)
{
	EnsureSceneCacheLoaded();
	{
		std::lock_guard lock(s_cacheLock);
		ApplySceneSemanticMetadataLocked(a_position);
	}

	if (!Config::OStimIntegration::ShowPositionPreviews) {
		a_position.previewPath.clear();
		a_position.iconPath.clear();
		return;
	}

	if (!a_position.previewPath.empty()) {
		if (auto resolvedPreview = ResolveMetadataAssetPath(a_position.previewPath); resolvedPreview.has_value()) {
			a_position.previewPath = *resolvedPreview;
		}
	}
	if (!a_position.iconPath.empty()) {
		if (auto resolvedIcon = ResolveMetadataAssetPath(a_position.iconPath); resolvedIcon.has_value()) {
			a_position.iconPath = *resolvedIcon;
		}
	}
	if (a_position.iconPath.empty()) {
		EnsureSceneCacheLoaded();
		std::lock_guard lock(s_cacheLock);
		if (auto resolvedSceneIcon = ResolveSceneNavigationAssetPath(a_position); resolvedSceneIcon.has_value()) {
			a_position.iconPath = *resolvedSceneIcon;
		}
	}
	if (a_position.iconPath.empty()) {
		std::lock_guard lock(s_cacheLock);
		if (auto resolvedLegacyIcon = ResolveLegacySceneIconPath(a_position); resolvedLegacyIcon.has_value()) {
			a_position.iconPath = *resolvedLegacyIcon;
		}
	}

	if (Config::OStimIntegration::UseResourcePreviewFallback) {
		EnsureCacheLoaded();

		std::lock_guard lock(s_cacheLock);
		if (a_position.previewPath.empty()) {
			if (auto preview = ResolvePath(
					a_position,
					s_cache.previewByScene,
					s_cache.previewByCategory,
					s_cache.previewByModule,
					kPreviewRoot);
				preview.has_value()) {
				a_position.previewPath = *preview;
			}
		}

		if (a_position.iconPath.empty()) {
			if (auto icon = ResolvePath(
					a_position,
					s_cache.iconByScene,
					s_cache.iconByCategory,
					s_cache.iconByModule,
					kIconRoot);
				icon.has_value()) {
				a_position.iconPath = *icon;
			}
		}

		if (a_position.iconPath.empty()) {
			if (auto categoryIcon = FindExistingPng(kCategoryRoot, a_position.category); categoryIcon.has_value()) {
				a_position.iconPath = *categoryIcon;
			}
		}
	}

	if (a_position.previewPath.empty() && !a_position.iconPath.empty()) {
		a_position.previewPath = a_position.iconPath;
	}
}
