#include "AmmoWheelReskinUnified.h"
#include "bin/Config.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <fmt/format.h>
#include "SimpleIni.h"
#include "include/lib/nanosvg.h"
#include "include/lib/nanosvgrast.h"

// Use stb_image from the existing reskin implementation
// (already defined in AmmoWheelReskin.cpp, so we just declare extern usage)
extern "C" {
	unsigned char* stbi_load_from_memory(unsigned char const* buffer, int len, int* x, int* y, int* channels_in_file, int desired_channels);
	void stbi_image_free(void* retval_from_stbi_load);
	const char* stbi_failure_reason(void);
}

namespace
{
	constexpr const char* kAmmoWheelIniPath = R"(.\Data\SKSE\Plugins\wheeler\AmmoWheel.ini)";
	constexpr const char* kAmmoWheelDefaultsIniPath = R"(.\Data\SKSE\Plugins\wheeler\AmmoWheel.defaults.ini)";

	struct SvgSizeInfo
	{
		float width = 0.0f;
		float height = 0.0f;
		float viewBoxW = 0.0f;
		float viewBoxH = 0.0f;
		bool usedViewBox = false;
		bool usedFallback = false;
	};

	bool EndsWithNoCase(const std::string& path, const char* suffix)
	{
		if (!suffix) {
			return false;
		}
		const size_t suffixLen = std::strlen(suffix);
		if (path.size() < suffixLen) {
			return false;
		}
		const size_t offset = path.size() - suffixLen;
		for (size_t i = 0; i < suffixLen; ++i) {
			const unsigned char a = static_cast<unsigned char>(path[offset + i]);
			const unsigned char b = static_cast<unsigned char>(suffix[i]);
			if (std::tolower(a) != std::tolower(b)) {
				return false;
			}
		}
		return true;
	}

	int ComputeSvgRasterBucketPx(int requestedMaxPx, uint32_t maxTextureSize)
	{
		const int maxPx = static_cast<int>((std::max)(maxTextureSize, 1u));
		const int minPx = (std::min)(64, maxPx);
		const float requested = requestedMaxPx > 0 ? static_cast<float>(requestedMaxPx) : 256.0f;
		const int oversampled = static_cast<int>(std::ceil(requested * 1.25f));
		const int clamped = std::clamp(oversampled, minPx, maxPx);
		const int bucketed = ((clamped + 63) / 64) * 64;
		return std::clamp(bucketed, minPx, maxPx);
	}

	bool TryGetSvgAttribute(const std::string& header, const char* attr, std::string& outValue)
	{
		const size_t attrLen = std::strlen(attr);
		size_t pos = 0;
		while ((pos = header.find(attr, pos)) != std::string::npos) {
			if (pos > 0) {
				const char prev = header[pos - 1];
				if (!std::isspace(static_cast<unsigned char>(prev)) && prev != '<') {
					pos += attrLen;
					continue;
				}
			}
			const size_t eq = pos + attrLen;
			if (eq >= header.size() || header[eq] != '=') {
				pos += attrLen;
				continue;
			}
			size_t quotePos = eq + 1;
			while (quotePos < header.size() && std::isspace(static_cast<unsigned char>(header[quotePos]))) {
				++quotePos;
			}
			if (quotePos >= header.size()) {
				return false;
			}
			const char quote = header[quotePos];
			if (quote != '"' && quote != '\'') {
				pos += attrLen;
				continue;
			}
			const size_t end = header.find(quote, quotePos + 1);
			if (end == std::string::npos) {
				return false;
			}
			outValue = header.substr(quotePos + 1, end - quotePos - 1);
			return true;
		}
		return false;
	}

	bool ParseLength(const std::string& value, float& outPx, bool& outPercent)
	{
		outPercent = false;
		if (value.empty()) {
			return false;
		}
		const size_t start = value.find_first_not_of(" \t\r\n");
		if (start == std::string::npos) {
			return false;
		}
		const size_t end = value.find_last_not_of(" \t\r\n");
		std::string trimmed = value.substr(start, end - start + 1);
		if (!trimmed.empty() && trimmed.back() == '%') {
			outPercent = true;
			trimmed.pop_back();
		}
		char* endPtr = nullptr;
		outPx = std::strtof(trimmed.c_str(), &endPtr);
		if (endPtr == trimmed.c_str()) {
			return false;
		}
		return outPx > 0.0f;
	}

	bool ParseViewBox(const std::string& value, float& outW, float& outH)
	{
		const char* s = value.c_str();
		char* endPtr = nullptr;
		float vals[4] = {};
		for (int i = 0; i < 4; ++i) {
			vals[i] = std::strtof(s, &endPtr);
			if (endPtr == s) {
				return false;
			}
			s = endPtr;
		}
		outW = vals[2];
		outH = vals[3];
		return outW > 0.0f && outH > 0.0f;
	}

	void ReplaceOrInsertAttr(std::string& header, const char* attr, const std::string& value)
	{
		const size_t attrLen = std::strlen(attr);
		size_t pos = header.find(attr);
		while (pos != std::string::npos) {
			if (pos > 0) {
				const char prev = header[pos - 1];
				if (!std::isspace(static_cast<unsigned char>(prev)) && prev != '<') {
					pos = header.find(attr, pos + attrLen);
					continue;
				}
			}
			const size_t eq = pos + attrLen;
			if (eq >= header.size() || header[eq] != '=') {
				pos = header.find(attr, pos + attrLen);
				continue;
			}
			size_t quotePos = eq + 1;
			while (quotePos < header.size() && std::isspace(static_cast<unsigned char>(header[quotePos]))) {
				++quotePos;
			}
			if (quotePos >= header.size()) {
				break;
			}
			const char quote = header[quotePos];
			if (quote != '"' && quote != '\'') {
				pos = header.find(attr, pos + attrLen);
				continue;
			}
			const size_t end = header.find(quote, quotePos + 1);
			if (end == std::string::npos) {
				break;
			}
			const std::string replacement = std::string(attr) + "=\"" + value + "\"";
			header.replace(pos, end - pos + 1, replacement);
			return;
		}
		size_t insertPos = header.find("<svg");
		if (insertPos != std::string::npos) {
			insertPos += 4;
			header.insert(insertPos, " " + std::string(attr) + "=\"" + value + "\"");
		}
	}

	bool NormalizeSvgText(std::string& svgText, SvgSizeInfo& info)
	{
		const size_t svgPos = svgText.find("<svg");
		if (svgPos == std::string::npos) {
			return false;
		}
		const size_t tagEnd = svgText.find('>', svgPos);
		if (tagEnd == std::string::npos) {
			return false;
		}
		std::string header = svgText.substr(svgPos, tagEnd - svgPos + 1);
		const std::string rest = svgText.substr(tagEnd + 1);

		std::string widthAttr;
		std::string heightAttr;
		std::string viewBoxAttr;
		const bool hasWidthAttr = TryGetSvgAttribute(header, "width", widthAttr);
		const bool hasHeightAttr = TryGetSvgAttribute(header, "height", heightAttr);
		const bool hasViewBox = TryGetSvgAttribute(header, "viewBox", viewBoxAttr);

		float width = 0.0f;
		float height = 0.0f;
		bool widthPercent = false;
		bool heightPercent = false;
		const bool widthOk = hasWidthAttr && ParseLength(widthAttr, width, widthPercent);
		const bool heightOk = hasHeightAttr && ParseLength(heightAttr, height, heightPercent);

		float viewBoxW = 0.0f;
		float viewBoxH = 0.0f;
		const bool viewBoxOk = hasViewBox && ParseViewBox(viewBoxAttr, viewBoxW, viewBoxH);

		info.viewBoxW = viewBoxW;
		info.viewBoxH = viewBoxH;

		const bool needsFix = !widthOk || !heightOk || widthPercent || heightPercent;
		if (needsFix) {
			if (viewBoxOk) {
				width = viewBoxW;
				height = viewBoxH;
				info.usedViewBox = true;
			} else {
				width = 512.0f;
				height = 512.0f;
				info.usedFallback = true;
			}
		}

		info.width = width > 0.0f ? width : 512.0f;
		info.height = height > 0.0f ? height : 512.0f;
		ReplaceOrInsertAttr(header, "width", std::to_string(static_cast<int>(info.width + 0.5f)) + "px");
		ReplaceOrInsertAttr(header, "height", std::to_string(static_cast<int>(info.height + 0.5f)) + "px");

		svgText = svgText.substr(0, svgPos) + header + rest;
		return true;
	}

	bool DecodeSvgToRgba(const std::vector<unsigned char>& fileData, int rasterBucketPx,
		uint32_t maxTex, std::vector<unsigned char>& outRgba, int& outW, int& outH)
	{
		outRgba.clear();
		outW = 0;
		outH = 0;
		if (fileData.empty()) {
			return false;
		}

		std::string svgText(reinterpret_cast<const char*>(fileData.data()), fileData.size());
		SvgSizeInfo info{};
		(void)NormalizeSvgText(svgText, info);  // Best effort; parsing may still succeed without changes.

		NSVGimage* svg = nsvgParse(svgText.data(), "px", 96.0f);
		if (!svg) {
			return false;
		}
		NSVGrasterizer* rast = nsvgCreateRasterizer();
		if (!rast) {
			nsvgDelete(svg);
			return false;
		}

		float baseW = svg->width;
		float baseH = svg->height;
		if (baseW <= 0.0f) {
			baseW = info.width > 0.0f ? info.width : (info.viewBoxW > 0.0f ? info.viewBoxW : 512.0f);
		}
		if (baseH <= 0.0f) {
			baseH = info.height > 0.0f ? info.height : (info.viewBoxH > 0.0f ? info.viewBoxH : 512.0f);
		}
		if (baseW <= 0.0f || baseH <= 0.0f) {
			nsvgDeleteRasterizer(rast);
			nsvgDelete(svg);
			return false;
		}

		const float longest = (std::max)(baseW, baseH);
		const float scale = longest > 0.0f ? static_cast<float>((std::max)(rasterBucketPx, 1)) / longest : 1.0f;
		if (!(scale > 0.0f) || !std::isfinite(scale)) {
			nsvgDeleteRasterizer(rast);
			nsvgDelete(svg);
			return false;
		}

		const int maxTexturePx = static_cast<int>((std::max)(maxTex, 1u));
		const int width = std::clamp(static_cast<int>(std::ceil(baseW * scale)), 1, maxTexturePx);
		const int height = std::clamp(static_cast<int>(std::ceil(baseH * scale)), 1, maxTexturePx);

		const uint64_t pixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
		if (pixelCount == 0 || pixelCount > static_cast<uint64_t>((std::numeric_limits<size_t>::max)() / 4)) {
			nsvgDeleteRasterizer(rast);
			nsvgDelete(svg);
			return false;
		}

		outRgba.resize(static_cast<size_t>(pixelCount) * 4);
		nsvgRasterize(rast, svg, 0.0f, 0.0f, scale, outRgba.data(), width, height, width * 4);
		nsvgDeleteRasterizer(rast);
		nsvgDelete(svg);
		outW = width;
		outH = height;
		return true;
	}

	bool LoadIniFileIntoSimpleIni(const std::string& path, CSimpleIniA& outIni)
	{
		try {
			outIni.Reset();
			outIni.SetUnicode();
			if (outIni.LoadFile(path.c_str()) != SI_OK) {
				logger::warn("[AmmoWheelReskinUnified] Failed to parse ini: {}", path);
				return false;
			}
			return true;
		} catch (const std::exception& e) {
			logger::warn("[AmmoWheelReskinUnified] Exception while loading ini '{}': {}", path, e.what());
			return false;
		} catch (...) {
			logger::warn("[AmmoWheelReskinUnified] Unknown exception while loading ini '{}'", path);
			return false;
		}
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

	bool LoadAmmoWheelConfigIni(CSimpleIniA& outIni)
	{
		outIni.Reset();
		outIni.SetUnicode();

		bool loadedAny = false;
		std::error_code ec;
		if (std::filesystem::exists(kAmmoWheelDefaultsIniPath, ec) && !ec) {
			if (LoadIniFileIntoSimpleIni(kAmmoWheelDefaultsIniPath, outIni)) {
				loadedAny = true;
			}
		}

		CSimpleIniA userIni;
		ec.clear();
		if (std::filesystem::exists(kAmmoWheelIniPath, ec) && !ec) {
			if (LoadIniFileIntoSimpleIni(kAmmoWheelIniPath, userIni)) {
				MergeIniInto(userIni, outIni);
				loadedAny = true;
			}
		}

		return loadedAny;
	}

	std::vector<std::string> GetSortedIniFiles(const std::string& dirPath)
	{
		std::vector<std::string> files;
		std::error_code ec;
		if (!std::filesystem::exists(dirPath, ec) || ec) {
			return files;
		}
		if (!std::filesystem::is_directory(dirPath, ec) || ec) {
			return files;
		}

		std::filesystem::directory_iterator endIt;
		for (std::filesystem::directory_iterator it(dirPath, ec); !ec && it != endIt; it.increment(ec)) {
			if (ec) {
				break;
			}
			if (!it->is_regular_file(ec) || ec) {
				continue;
			}
			std::string ext = it->path().extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (ext == ".ini") {
				files.push_back(it->path().string());
			}
		}
		if (ec) {
			logger::warn("[AmmoWheelReskinUnified] Failed to enumerate INI files in '{}': {}", dirPath, ec.message());
		}

		std::sort(files.begin(), files.end(),
			[](const std::string& a, const std::string& b) {
				std::string fa = std::filesystem::path(a).filename().string();
				std::string fb = std::filesystem::path(b).filename().string();
				std::transform(fa.begin(), fa.end(), fa.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				std::transform(fb.begin(), fb.end(), fb.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (fa != fb) {
					return fa < fb;
				}
				return a < b;
			});
		return files;
	}

	float SanitizeLayoutScale(float value)
	{
		if (!std::isfinite(value) || value <= 0.0f) {
			return 1.0f;
		}
		return std::clamp(value, 0.05f, 8.0f);
	}

	float SanitizeLayoutOffset(float value)
	{
		if (!std::isfinite(value)) {
			return 0.0f;
		}
		return std::clamp(value, -5000.0f, 5000.0f);
	}

	float SanitizeLayoutAngleDeg(float value)
	{
		if (!std::isfinite(value)) {
			return 0.0f;
		}
		return std::clamp(value, -720.0f, 720.0f);
	}

	float SanitizeLayoutOpacity(float value)
	{
		if (!std::isfinite(value)) {
			return 1.0f;
		}
		return std::clamp(value, 0.0f, 1.0f);
	}
}

namespace AmmoWheelReskinUnified
{
	// ========== TARGET NAMES ==========
	const char* GetTargetName(VisualTarget target)
	{
		switch (target) {
			case VisualTarget::SlotIcon: return "SlotIcon";
			case VisualTarget::SlotBackground: return "SlotBackground";
			case VisualTarget::SlotFrame: return "SlotFrame";
			case VisualTarget::IndicatorSelected: return "IndicatorSelected";
			case VisualTarget::IndicatorHovered: return "IndicatorHovered";
			case VisualTarget::IndicatorActive: return "IndicatorActive";
			case VisualTarget::IndicatorCharge: return "IndicatorCharge";
			case VisualTarget::Popup: return "Popup";
			case VisualTarget::PopupBubble: return "PopupBubble";
			case VisualTarget::WheelBackdrop: return "WheelBackdrop";
			case VisualTarget::WheelBackground: return "WheelBackground";
			case VisualTarget::CenterBackground: return "CenterBackground";
			case VisualTarget::WheelBorderRing: return "WheelBorderRing";
			case VisualTarget::SlotDivider: return "SlotDivider";
			case VisualTarget::NamePanelBackground: return "NamePanelBackground";
			case VisualTarget::SlotLabelText: return "SlotLabelText";
			case VisualTarget::CursorIndicator: return "CursorIndicator";
			case VisualTarget::LowAmmoIndicator: return "LowAmmoIndicator";
			case VisualTarget::PopupBubbleRim: return "PopupBubbleRim";
			case VisualTarget::PopupBubbleGlow: return "PopupBubbleGlow";
			case VisualTarget::CenterPanelFrame: return "CenterPanelFrame";
			case VisualTarget::CenterDescriptionText: return "CenterDescriptionText";
			case VisualTarget::DamageDigits: return "DamageDigits";
			case VisualTarget::MaxDamageDigits: return "MaxDamageDigits";
			case VisualTarget::AmmoCountDigits: return "AmmoCountDigits";
			default: return "Unknown";
		}
	}

	namespace
	{
		bool IsSlotIndicatorTarget(VisualTarget target)
		{
			return target == VisualTarget::IndicatorSelected ||
				target == VisualTarget::IndicatorHovered ||
				target == VisualTarget::IndicatorActive ||
				target == VisualTarget::IndicatorCharge ||
				target == VisualTarget::LowAmmoIndicator;
		}
	}

	const char* GetResolutionSourceName(ResolutionSource source)
	{
		switch (source) {
			case ResolutionSource::FormID: return "FormID";
			case ResolutionSource::Keyword: return "Keyword";
			case ResolutionSource::TypeDefault: return "TypeDefault";
			case ResolutionSource::Fallback: return "Fallback";
			default: return "None";
		}
	}

	// ========== UTILITY FUNCTIONS ==========
	bool SafeFileExists(const std::string& path)
	{
		if (path.empty()) return false;
		if (!IsPathSafe(path)) return false;
		
		std::error_code ec;
		return std::filesystem::exists(path, ec) && !ec;
	}

	bool SafeGetFileSize(const std::string& path, uint64_t& outSize)
	{
		if (!SafeFileExists(path)) return false;
		
		std::error_code ec;
		auto size = std::filesystem::file_size(path, ec);
		if (ec) return false;
		
		outSize = size;
		return true;
	}

	std::string GetAbsolutePath(const std::string& relativePath)
	{
		std::error_code ec;
		auto absPath = std::filesystem::absolute(relativePath, ec);
		return ec ? relativePath : absPath.string();
	}

	bool IsPathSafe(const std::string& path)
	{
		if (path.empty()) return false;
		if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') return false;  // UNC
		if (path.find("..") != std::string::npos) return false;  // Traversal
		return true;
	}

	std::string NormalizePath(const std::string& path)
	{
		// Fast lexical normalization for per-frame cache lookups.
		// Avoid filesystem canonicalization here; it is too expensive in draw-time hot paths.
		std::string result;
		result.reserve(path.size());
		for (unsigned char c : path) {
			if (c == '/') {
				result.push_back('\\');
			} else {
				result.push_back(static_cast<char>(std::tolower(c)));
			}
		}
		return result;
	}

	// ========== RESKIN SYSTEM SINGLETON ==========
	ReskinSystem& ReskinSystem::GetSingleton()
	{
		static ReskinSystem instance;
		return instance;
	}

	void ReskinSystem::Init(ID3D11Device* device)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_initialized) return;
		
		_device = device;
		_startTime = std::chrono::steady_clock::now();
		
		BuildDefaultPreset();
		LoadConfig();
		
		if (_enabled) {
			LoadPresets();
			LoadMappings();
			PrewarmDefaultWeaponPresets();
		}
		
		// Check for legacy flags and log deprecation warnings
		CompatShim::CheckLegacyFlags();
		
		_initialized = true;
		logger::info("[AmmoWheelReskinUnified] Initialized (enabled={}, basePath={})", 
			_enabled, _basePath);
	}

	void ReskinSystem::Shutdown()
	{
		std::lock_guard<std::mutex> lock(_mutex);
		ClearTextureCache();
		_presets.clear();
		_formIDToPreset.clear();
		_keywordToPreset.clear();
		_initialized = false;
		_device = nullptr;
	}

	void ReskinSystem::Reload()
	{
		std::lock_guard<std::mutex> lock(_mutex);
		
		ClearTextureCache();
		_presets.clear();
		_formIDToPreset.clear();
		_keywordToPreset.clear();
		
		BuildDefaultPreset();
		LoadConfig();
		
		if (_enabled) {
			LoadPresets();
			LoadMappings();
			PrewarmDefaultWeaponPresets();
		}
		
		logger::info("[AmmoWheelReskinUnified] Reloaded");
	}

	void ReskinSystem::ReloadSmartFromIni()
	{
		bool needsHeavyReload = false;
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (!_initialized) {
				return;
			}

			CSimpleIniA ini;
			if (!LoadAmmoWheelConfigIni(ini)) {
				return;
			}

			const bool newEnabled = ini.GetBoolValue("Reskin", "Enabled", false);
			const std::string newBasePath = ini.GetValue("Reskin", "BasePath",
				R"(.\Data\SKSE\Plugins\wheeler\resources\ammo_wheel)");

			std::string newActivePresetPath;
			const int activePreset = static_cast<int>(ini.GetLongValue("Presets", "ActivePreset", 0));
			if (activePreset > 0 && activePreset <= 3) {
				newActivePresetPath = fmt::format(R"(.\Data\SKSE\Plugins\wheeler\presets\{})", activePreset);
			}

			const uint32_t newMaxTotalFrames = static_cast<uint32_t>(
				ini.GetLongValue("Safety", "MaxTotalFrames", MAX_TOTAL_FRAMES));
			const uint64_t newMaxTotalBytes = static_cast<uint64_t>(static_cast<uint32_t>(
				ini.GetLongValue("Safety", "MaxTotalBytesMB", MAX_TOTAL_BYTES_MB))) * 1024ULL * 1024ULL;
			const uint32_t newMaxTextureSize = static_cast<uint32_t>(
				ini.GetLongValue("Safety", "MaxTextureSize", MAX_TEXTURE_SIZE));
			const uint64_t newMaxPngFileBytes = static_cast<uint64_t>(static_cast<uint32_t>(
				ini.GetLongValue("Safety", "MaxPngFileMB", MAX_PNG_FILE_MB))) * 1024ULL * 1024ULL;

			needsHeavyReload =
				(newEnabled != _enabled) ||
				(newBasePath != _basePath) ||
				(newActivePresetPath != _activePresetPath) ||
				(newMaxTotalFrames != _maxTotalFrames) ||
				(newMaxTotalBytes != _maxTotalBytes) ||
				(newMaxTextureSize != _maxTextureSize) ||
				(newMaxPngFileBytes != _maxPngFileBytes);

			if (!needsHeavyReload) {
				auto readColor = [&](const char* key, ImU32 defaultVal) -> ImU32 {
					const char* val = ini.GetValue("Primitives", key, nullptr);
					if (!val) {
						return defaultVal;
					}
					try {
						return static_cast<ImU32>(std::stoul(val, nullptr, 0));
					} catch (...) {
						return defaultVal;
					}
				};

				_primitiveFallback.slotUnhoveredInner = readColor("SlotUnhoveredInner", _defaultPreset.primitives.slotUnhoveredInner);
				_primitiveFallback.slotUnhoveredOuter = readColor("SlotUnhoveredOuter", _defaultPreset.primitives.slotUnhoveredOuter);
				_primitiveFallback.slotHoveredInner = readColor("SlotHoveredInner", _defaultPreset.primitives.slotHoveredInner);
				_primitiveFallback.slotHoveredOuter = readColor("SlotHoveredOuter", _defaultPreset.primitives.slotHoveredOuter);
				_primitiveFallback.indicatorSelectedInner = readColor("IndicatorSelectedInner", _defaultPreset.primitives.indicatorSelectedInner);
				_primitiveFallback.indicatorSelectedOuter = readColor("IndicatorSelectedOuter", _defaultPreset.primitives.indicatorSelectedOuter);
				_primitiveFallback.textPrimary = readColor("TextPrimary", _defaultPreset.primitives.textPrimary);
				_primitiveFallback.textShadow = readColor("TextShadow", _defaultPreset.primitives.textShadow);

				LoadLayoutOverrides(ini);
			}
		}

		if (needsHeavyReload) {
			logger::info("[AmmoWheelReskinUnified] ReloadSmartFromIni: critical reskin config changed, doing full reload");
			Reload();
		}
	}

	PerfStats ReskinSystem::ConsumePerfStats()
	{
		PerfStats out;
		out.drawTargetMs = _perfDrawTargetMsAccum;
		out.drawTargetCalls = _perfDrawTargetCalls;
		out.getTextureMs = _perfGetTextureMsAccum;
		out.getTextureCalls = _perfGetTextureCalls;
		out.textureCacheHits = _perfTextureCacheHits;
		out.textureCacheMisses = _perfTextureCacheMisses;

		_perfDrawTargetMsAccum = 0.0;
		_perfDrawTargetCalls = 0;
		_perfGetTextureMsAccum = 0.0;
		_perfGetTextureCalls = 0;
		_perfTextureCacheHits = 0;
		_perfTextureCacheMisses = 0;
		return out;
	}

	void ReskinSystem::BuildDefaultPreset()
	{
		_defaultPreset.id = "Default";
		_defaultPreset.name = "Default Preset";
		
		// All assets disabled by default = pure primitive rendering
		for (size_t i = 0; i < static_cast<size_t>(VisualTarget::COUNT); ++i) {
			_defaultPreset.assets[i].enabled = false;
			_defaultPreset.assets[i].type = AssetType::None;
		}
		
		// Default primitive colors (Skyrim-inspired)
		_defaultPreset.primitives = PrimitiveFallback{};
		_primitiveFallback = _defaultPreset.primitives;
		_layoutOverrides.fill(LayoutOverride{});
	}

	void ReskinSystem::LoadConfig()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		if (!LoadAmmoWheelConfigIni(ini)) {
			logger::info("[AmmoWheelReskinUnified] AmmoWheel config not found, reskin disabled");
			_enabled = false;
			_layoutOverrides.fill(LayoutOverride{});
			return;
		}
		
		// Master enable
		_enabled = ini.GetBoolValue("Reskin", "Enabled", false);
		_basePath = ini.GetValue("Reskin", "BasePath", 
			R"(.\Data\SKSE\Plugins\wheeler\resources\ammo_wheel)");
		
		// Check for active preset and override base path for styles
		int activePreset = static_cast<int>(ini.GetLongValue("Presets", "ActivePreset", 0));
		if (activePreset > 0 && activePreset <= 3) {
			_activePresetPath = fmt::format(R"(.\Data\SKSE\Plugins\wheeler\presets\{})", activePreset);
			logger::info("[AmmoWheelReskinUnified] Active preset: {} (path: {})", activePreset, _activePresetPath);
		} else {
			_activePresetPath.clear();
		}
		
		// Safety limits
		_maxTotalFrames = static_cast<uint32_t>(
			ini.GetLongValue("Safety", "MaxTotalFrames", MAX_TOTAL_FRAMES));
		uint32_t maxBytesMB = static_cast<uint32_t>(
			ini.GetLongValue("Safety", "MaxTotalBytesMB", MAX_TOTAL_BYTES_MB));
		_maxTotalBytes = static_cast<uint64_t>(maxBytesMB) * 1024 * 1024;
		_maxTextureSize = static_cast<uint32_t>(
			ini.GetLongValue("Safety", "MaxTextureSize", MAX_TEXTURE_SIZE));
		uint32_t maxPngMB = static_cast<uint32_t>(
			ini.GetLongValue("Safety", "MaxPngFileMB", MAX_PNG_FILE_MB));
		_maxPngFileBytes = static_cast<uint64_t>(maxPngMB) * 1024 * 1024;
		
		// Type defaults
		_arrowDefaultPreset = ini.GetValue("Reskin", "ArrowDefaultPreset", "Default");
		_boltDefaultPreset = ini.GetValue("Reskin", "BoltDefaultPreset", "Default");
		
		// Load primitive fallback colors from [Primitives] section
		auto readColor = [&](const char* key, ImU32 defaultVal) -> ImU32 {
			const char* val = ini.GetValue("Primitives", key, nullptr);
			if (!val) return defaultVal;
			try {
				return static_cast<ImU32>(std::stoul(val, nullptr, 0));
			} catch (...) {
				return defaultVal;
			}
		};
		
		_primitiveFallback.slotUnhoveredInner = readColor("SlotUnhoveredInner", 
			_primitiveFallback.slotUnhoveredInner);
		_primitiveFallback.slotUnhoveredOuter = readColor("SlotUnhoveredOuter",
			_primitiveFallback.slotUnhoveredOuter);
		_primitiveFallback.slotHoveredInner = readColor("SlotHoveredInner",
			_primitiveFallback.slotHoveredInner);
		_primitiveFallback.slotHoveredOuter = readColor("SlotHoveredOuter",
			_primitiveFallback.slotHoveredOuter);
		_primitiveFallback.indicatorSelectedInner = readColor("IndicatorSelectedInner",
			_primitiveFallback.indicatorSelectedInner);
		_primitiveFallback.indicatorSelectedOuter = readColor("IndicatorSelectedOuter",
			_primitiveFallback.indicatorSelectedOuter);
		_primitiveFallback.textPrimary = readColor("TextPrimary",
			_primitiveFallback.textPrimary);
		_primitiveFallback.textShadow = readColor("TextShadow",
			_primitiveFallback.textShadow);

		LoadLayoutOverrides(ini);
		
		logger::info("[AmmoWheelReskinUnified] Config loaded: enabled={}", _enabled);
	}

	void ReskinSystem::LoadLayoutOverrides(const CSimpleIniA& ini)
	{
		for (size_t i = 0; i < static_cast<size_t>(VisualTarget::COUNT); ++i) {
			const auto target = static_cast<VisualTarget>(i);
			const char* targetName = GetTargetName(target);
			const std::string section = std::string("Reskin.Layout.") + targetName;

			LayoutOverride overrideValue{};
			overrideValue.scale = SanitizeLayoutScale(static_cast<float>(
				ini.GetDoubleValue(section.c_str(), "Scale", 1.0)));
			overrideValue.offsetX = SanitizeLayoutOffset(static_cast<float>(
				ini.GetDoubleValue(section.c_str(), "OffsetX", 0.0)));
			overrideValue.offsetY = SanitizeLayoutOffset(static_cast<float>(
				ini.GetDoubleValue(section.c_str(), "OffsetY", 0.0)));
			overrideValue.offsetRadial = SanitizeLayoutOffset(static_cast<float>(
				ini.GetDoubleValue(section.c_str(), "OffsetRadial", 0.0)));
			overrideValue.offsetTangential = SanitizeLayoutOffset(static_cast<float>(
				ini.GetDoubleValue(section.c_str(), "OffsetTangential", 0.0)));
			overrideValue.angleOffsetDeg = SanitizeLayoutAngleDeg(static_cast<float>(
				ini.GetDoubleValue(section.c_str(), "AngleOffsetDeg", 0.0)));
			overrideValue.selfRotationDeg = SanitizeLayoutAngleDeg(static_cast<float>(
				ini.GetDoubleValue(section.c_str(), "SelfRotationDeg", 0.0)));
			overrideValue.opacity = SanitizeLayoutOpacity(static_cast<float>(
				ini.GetDoubleValue(section.c_str(), "Opacity", 1.0)));
			overrideValue.digitSpacingOffsetPx = SanitizeLayoutOffset(static_cast<float>(
				ini.GetDoubleValue(section.c_str(), "DigitSpacingOffsetPx", 0.0)));

			_layoutOverrides[i] = overrideValue;
		}
	}

	void ReskinSystem::LoadPresets()
	{
		// If a preset is active, load its Styles.ini first (takes priority)
		if (!_activePresetPath.empty()) {
			std::string presetStylesPath = _activePresetPath + "\\Styles.ini";
			CSimpleIniA presetIni;
			if (LoadIniFileIntoSimpleIni(presetStylesPath, presetIni)) {
				logger::info("[AmmoWheelReskinUnified] Loading preset Styles.ini from {}", presetStylesPath);
				
				// Load primitive colors from preset
				auto readColor = [&](const char* key, ImU32 defaultVal) -> ImU32 {
					const char* val = presetIni.GetValue("Primitives", key, nullptr);
					if (!val) return defaultVal;
					try {
						return static_cast<ImU32>(std::stoul(val, nullptr, 0));
					} catch (...) {
						return defaultVal;
					}
				};
				
				_primitiveFallback.slotUnhoveredInner = readColor("SlotUnhoveredInner", _primitiveFallback.slotUnhoveredInner);
				_primitiveFallback.slotUnhoveredOuter = readColor("SlotUnhoveredOuter", _primitiveFallback.slotUnhoveredOuter);
				_primitiveFallback.slotHoveredInner = readColor("SlotHoveredInner", _primitiveFallback.slotHoveredInner);
				_primitiveFallback.slotHoveredOuter = readColor("SlotHoveredOuter", _primitiveFallback.slotHoveredOuter);
				_primitiveFallback.indicatorSelectedInner = readColor("IndicatorSelectedInner", _primitiveFallback.indicatorSelectedInner);
				_primitiveFallback.indicatorSelectedOuter = readColor("IndicatorSelectedOuter", _primitiveFallback.indicatorSelectedOuter);
				_primitiveFallback.indicatorHoveredInner = readColor("IndicatorHoveredInner", _primitiveFallback.indicatorHoveredInner);
				_primitiveFallback.indicatorHoveredOuter = readColor("IndicatorHoveredOuter", _primitiveFallback.indicatorHoveredOuter);
				_primitiveFallback.textPrimary = readColor("TextPrimary", _primitiveFallback.textPrimary);
				_primitiveFallback.textShadow = readColor("TextShadow", _primitiveFallback.textShadow);
				
				logger::info("[AmmoWheelReskinUnified] Preset primitive colors loaded");
			}
		}
		
		std::string stylesPath = _basePath + "\\AmmoWheel_Styles.ini";
		uint32_t totalPresetSections = 0;
		uint32_t extraFilesDiscovered = 0;
		uint32_t extraFilesLoaded = 0;

		auto parsePresetSection = [&](const CSimpleIniA& ini, const std::string& sectionName) -> bool {
			if (sectionName.rfind("Preset_", 0) != 0) {
				return false;
			}
			
			std::string presetId = sectionName.substr(7);
			
			// Create a fresh preset with all assets disabled
			// Do NOT inherit from _defaultPreset - that's only used as fallback when no preset matches
			Preset preset;
			preset.id = presetId;
			preset.name = ini.GetValue(sectionName.c_str(), "Name", presetId.c_str());
			
			// Initialize all assets as disabled
			for (size_t i = 0; i < static_cast<size_t>(VisualTarget::COUNT); ++i) {
				preset.assets[i].enabled = false;
				preset.assets[i].type = AssetType::None;
			}
			
			// Copy primitive fallback colors from default
			preset.primitives = _defaultPreset.primitives;
			
			// Load asset definitions for each target
			auto loadAsset = [&](AssetDef& asset, const char* prefix, RotationMode defaultRotation = RotationMode::Upright) {
				std::string enabledKey = std::string(prefix) + "_Enabled";
				std::string usePrimitiveKey = std::string(prefix) + "_UsePrimitive";
				std::string pathKey = std::string(prefix) + "_Path";
				std::string flipbookKey = std::string(prefix) + "_IsFlipbook";
				std::string atlasKey = std::string(prefix) + "_IsAtlas";
				std::string frameCountKey = std::string(prefix) + "_FrameCount";
				std::string fpsKey = std::string(prefix) + "_FPS";
				std::string loopKey = std::string(prefix) + "_Loop";
				std::string progressDrivenKey = std::string(prefix) + "_ProgressDriven";
				std::string startTimeModeKey = std::string(prefix) + "_StartTimeMode";
				std::string alphaKey = std::string(prefix) + "_Alpha";
				std::string rotationModeKey = std::string(prefix) + "_RotationMode";
				std::string rotationOffsetKey = std::string(prefix) + "_RotationOffsetDeg";
				std::string fitModeKey = std::string(prefix) + "_FitMode";
				std::string fixedSizeKey = std::string(prefix) + "_FixedSizePx";
				std::string safetyScaleKey = std::string(prefix) + "_RotationSafetyScale";
				// Optional atlas keys (used by AmmoCountDigits):
				// <Prefix>_AtlasCols, <Prefix>_AtlasRows, <Prefix>_DigitSpacingPx
				std::string atlasColsKey = std::string(prefix) + "_AtlasCols";
				std::string atlasRowsKey = std::string(prefix) + "_AtlasRows";
				std::string digitSpacingKey = std::string(prefix) + "_DigitSpacingPx";
				
				asset.enabled = ini.GetBoolValue(sectionName.c_str(), enabledKey.c_str(), false);
				asset.usePrimitive = ini.GetBoolValue(sectionName.c_str(), usePrimitiveKey.c_str(), false);
				if (!asset.enabled) return;
				asset.atlasCols = static_cast<uint8_t>(std::clamp(
					static_cast<int>(ini.GetLongValue(sectionName.c_str(), atlasColsKey.c_str(), 10)),
					1, 255));
				asset.atlasRows = static_cast<uint8_t>(std::clamp(
					static_cast<int>(ini.GetLongValue(sectionName.c_str(), atlasRowsKey.c_str(), 1)),
					1, 255));
				asset.digitSpacingPx = static_cast<float>(
					ini.GetDoubleValue(sectionName.c_str(), digitSpacingKey.c_str(), 0.0));
				
				bool isFlipbook = ini.GetBoolValue(sectionName.c_str(), flipbookKey.c_str(), false);
				bool isAtlas = ini.GetBoolValue(sectionName.c_str(), atlasKey.c_str(), false);
				if (isFlipbook && isAtlas) {
					logger::warn("[AmmoWheelReskinUnified] [{}] {} has both IsFlipbook and IsAtlas=true; preferring flipbook",
						sectionName, prefix);
					isAtlas = false;
				}
				
				if (isFlipbook) {
					asset.type = AssetType::Flipbook;
					asset.flipbook.pattern = ini.GetValue(sectionName.c_str(), pathKey.c_str(), "");
					asset.flipbook.frameCount = static_cast<uint32_t>(
						ini.GetLongValue(sectionName.c_str(), frameCountKey.c_str(), 1));
					asset.flipbook.fps = static_cast<float>(
						ini.GetDoubleValue(sectionName.c_str(), fpsKey.c_str(), DEFAULT_FPS));
					asset.flipbook.loop = ini.GetBoolValue(sectionName.c_str(), loopKey.c_str(), true);
					// Optional: <Prefix>_ProgressDriven=1 uses DrawContext::progress to pick frame.
					asset.flipbook.progressDriven = ini.GetBoolValue(
						sectionName.c_str(), progressDrivenKey.c_str(), false);
					int startTimeMode = static_cast<int>(ini.GetLongValue(
						sectionName.c_str(), startTimeModeKey.c_str(),
						static_cast<long>(StartTimeMode::Global)));
					asset.flipbook.startTimeMode = static_cast<StartTimeMode>(std::clamp(startTimeMode, 0, 2));
				} else if (isAtlas) {
					asset.type = AssetType::AtlasSheet;
					asset.staticPath = ini.GetValue(sectionName.c_str(), pathKey.c_str(), "");
					asset.flipbook.frameCount = static_cast<uint32_t>(
						ini.GetLongValue(sectionName.c_str(), frameCountKey.c_str(), 0));
					asset.flipbook.fps = static_cast<float>(
						ini.GetDoubleValue(sectionName.c_str(), fpsKey.c_str(), DEFAULT_FPS));
					asset.flipbook.loop = ini.GetBoolValue(sectionName.c_str(), loopKey.c_str(), true);
					asset.flipbook.progressDriven = ini.GetBoolValue(
						sectionName.c_str(), progressDrivenKey.c_str(), false);
					int startTimeMode = static_cast<int>(ini.GetLongValue(
						sectionName.c_str(), startTimeModeKey.c_str(),
						static_cast<long>(StartTimeMode::Global)));
					asset.flipbook.startTimeMode = static_cast<StartTimeMode>(std::clamp(startTimeMode, 0, 2));
				} else {
					asset.type = AssetType::StaticPNG;
					asset.staticPath = ini.GetValue(sectionName.c_str(), pathKey.c_str(), "");
				}
				
				asset.alpha = static_cast<float>(
					ini.GetDoubleValue(sectionName.c_str(), alphaKey.c_str(), 1.0f));
				
				// Rotation settings
				int rotMode = static_cast<int>(ini.GetLongValue(sectionName.c_str(), rotationModeKey.c_str(), 
					static_cast<long>(defaultRotation)));
				asset.rotationMode = static_cast<RotationMode>(std::clamp(rotMode, 0, 2));
				asset.rotationOffsetDeg = static_cast<float>(
					ini.GetDoubleValue(sectionName.c_str(), rotationOffsetKey.c_str(), 0.0));
				
				// Fit settings
				int fitMode = static_cast<int>(ini.GetLongValue(sectionName.c_str(), fitModeKey.c_str(), 0));
				asset.fitMode = static_cast<FitMode>(std::clamp(fitMode, 0, 1));
				asset.fixedSizePx = static_cast<float>(
					ini.GetDoubleValue(sectionName.c_str(), fixedSizeKey.c_str(), 48.0));
				asset.rotationSafetyScale = static_cast<float>(
					ini.GetDoubleValue(sectionName.c_str(), safetyScaleKey.c_str(), DEFAULT_ROTATION_SAFETY_SCALE));
			};
			
			// SlotIcon defaults to FollowSlot rotation (rotate with slot angle)
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::SlotIcon)], "SlotIcon", RotationMode::FollowSlot);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::SlotBackground)], "SlotBackground", RotationMode::FollowSlot);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::SlotFrame)], "SlotFrame", RotationMode::FollowSlot);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::IndicatorSelected)], "IndicatorSelected", RotationMode::FollowSlot);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::IndicatorHovered)], "IndicatorHovered", RotationMode::FollowSlot);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::IndicatorActive)], "IndicatorActive", RotationMode::FollowSlot);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::IndicatorCharge)], "IndicatorCharge", RotationMode::FollowSlot);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::Popup)], "Popup", RotationMode::Upright);
			// PopupBubble: the actual popup bubble outside the wheel (distinct from slot hover overlay)
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::PopupBubble)], "PopupBubble", RotationMode::Upright);
			// Wheel backdrop/background and center background default to Upright (no rotation)
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::WheelBackdrop)], "WheelBackdrop", RotationMode::Upright);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::WheelBackground)], "WheelBackground", RotationMode::Upright);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::CenterBackground)], "CenterBackground", RotationMode::Upright);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::WheelBorderRing)], "WheelBorderRing", RotationMode::Upright);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::SlotDivider)], "SlotDivider", RotationMode::FollowSlot);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::NamePanelBackground)], "NamePanelBackground", RotationMode::Upright);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::SlotLabelText)], "SlotLabelText", RotationMode::Upright);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::CursorIndicator)], "CursorIndicator", RotationMode::Upright);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::LowAmmoIndicator)], "LowAmmoIndicator", RotationMode::Upright);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::PopupBubbleRim)], "PopupBubbleRim", RotationMode::Upright);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::PopupBubbleGlow)], "PopupBubbleGlow", RotationMode::Upright);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::CenterPanelFrame)], "CenterPanelFrame", RotationMode::Upright);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::CenterDescriptionText)], "CenterDescriptionText", RotationMode::Upright);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::DamageDigits)], "DamageDigits", RotationMode::Upright);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::MaxDamageDigits)], "MaxDamageDigits", RotationMode::Upright);
			loadAsset(preset.assets[static_cast<size_t>(VisualTarget::AmmoCountDigits)], "AmmoCountDigits", RotationMode::Upright);
			
			_presets[presetId] = preset;
			return true;
		};

		auto loadPresetSectionsFromIni = [&](const CSimpleIniA& ini, const std::string& sourcePath) {
			CSimpleIniA::TNamesDepend sections;
			ini.GetAllSections(sections);

			uint32_t loadedFromFile = 0;
			for (const auto& section : sections) {
				const std::string sectionName = section.pItem;
				if (parsePresetSection(ini, sectionName)) {
					loadedFromFile++;
					totalPresetSections++;
				}
			}

			if (loadedFromFile > 0 || Config::AmmoWheel::Debug::LogAssetLoading) {
				logger::info("[AmmoWheelReskinUnified] Preset file '{}' loaded {} preset section(s)",
					sourcePath, loadedFromFile);
			}
		};

		std::error_code ec;
		if (std::filesystem::exists(stylesPath, ec) && !ec) {
			CSimpleIniA baseIni;
			if (LoadIniFileIntoSimpleIni(stylesPath, baseIni)) {
				loadPresetSectionsFromIni(baseIni, stylesPath);
			}
		} else {
			logger::info("[AmmoWheelReskinUnified] AmmoWheel_Styles.ini not found at {}", stylesPath);
		}

		const std::string stylesDir = _basePath + "\\styles";
		const auto styleFiles = GetSortedIniFiles(stylesDir);
		extraFilesDiscovered = static_cast<uint32_t>(styleFiles.size());
		for (const auto& styleFile : styleFiles) {
			CSimpleIniA modularIni;
			if (!LoadIniFileIntoSimpleIni(styleFile, modularIni)) {
				continue;
			}
			extraFilesLoaded++;
			loadPresetSectionsFromIni(modularIni, styleFile);
		}

		logger::info(
			"[AmmoWheelReskinUnified] Presets summary: sections={} uniquePresets={} extraFilesLoaded={} extraFilesDiscovered={}",
			totalPresetSections,
			_presets.size(),
			extraFilesLoaded,
			extraFilesDiscovered);
	}

	void ReskinSystem::LoadMappings()
	{
		std::string kidPath = _basePath + "\\AMMO_KID.ini";
		uint32_t appliedFormEntries = 0;
		uint32_t appliedKeywordEntries = 0;
		uint32_t extraFilesDiscovered = 0;
		uint32_t extraFilesLoaded = 0;

		auto mergeMappingsFromIni = [&](const CSimpleIniA& ini, const std::string& sourcePath) {
			uint32_t fileFormCount = 0;
			uint32_t fileKeywordCount = 0;

			CSimpleIniA::TNamesDepend keys;
			ini.GetAllKeys("FormIDPresets", keys);
			for (const auto& key : keys) {
				const std::string formIDStr = key.pItem;
				const std::string presetId = ini.GetValue("FormIDPresets", formIDStr.c_str(), "");
				if (presetId.empty()) {
					continue;
				}

				try {
					RE::FormID formID = static_cast<RE::FormID>(std::stoul(formIDStr, nullptr, 16));
					_formIDToPreset[formID] = presetId;
					fileFormCount++;
					appliedFormEntries++;

					if (Config::AmmoWheel::Debug::LogPresetResolution) {
						if (presetId == "BloodcursedElvenArrow" || presetId == "SunhallowedElvenArrow") {
							logger::info("[AmmoWheelReskinUnified] Stored mapping: 0x{:08X} = {}", formID, presetId);
						}
					}
				} catch (...) {
					logger::warn("[AmmoWheelReskinUnified] Invalid FormID '{}' in {}", formIDStr, sourcePath);
				}
			}

			keys.clear();
			ini.GetAllKeys("KeywordPresets", keys);
			for (const auto& key : keys) {
				const std::string keyword = key.pItem;
				const std::string presetId = ini.GetValue("KeywordPresets", keyword.c_str(), "");
				if (!presetId.empty()) {
					_keywordToPreset[keyword] = presetId;
					fileKeywordCount++;
					appliedKeywordEntries++;
				}
			}

			const char* arrowPreset = ini.GetValue("TypePresets", "Arrow", nullptr);
			if (arrowPreset && arrowPreset[0] != '\0') {
				_arrowDefaultPreset = arrowPreset;
			}
			const char* boltPreset = ini.GetValue("TypePresets", "Bolt", nullptr);
			if (boltPreset && boltPreset[0] != '\0') {
				_boltDefaultPreset = boltPreset;
			}

			if (fileFormCount > 0 || fileKeywordCount > 0 || Config::AmmoWheel::Debug::LogPresetResolution) {
				logger::info("[AmmoWheelReskinUnified] Mapping file '{}' applied FormID={} Keyword={}",
					sourcePath, fileFormCount, fileKeywordCount);
			}
		};

		std::error_code ec;
		if (std::filesystem::exists(kidPath, ec) && !ec) {
			CSimpleIniA baseIni;
			if (LoadIniFileIntoSimpleIni(kidPath, baseIni)) {
				mergeMappingsFromIni(baseIni, kidPath);
			}
		} else {
			logger::info("[AmmoWheelReskinUnified] AMMO_KID.ini not found at {}", kidPath);
		}

		const std::string mappingsDir = _basePath + "\\mappings";
		const auto mappingFiles = GetSortedIniFiles(mappingsDir);
		extraFilesDiscovered = static_cast<uint32_t>(mappingFiles.size());
		for (const auto& mappingFile : mappingFiles) {
			CSimpleIniA modularIni;
			if (!LoadIniFileIntoSimpleIni(mappingFile, modularIni)) {
				continue;
			}
			extraFilesLoaded++;
			mergeMappingsFromIni(modularIni, mappingFile);
		}

		logger::info(
			"[AmmoWheelReskinUnified] Mappings summary: formIDs={} keywords={} appliedFormEntries={} appliedKeywordEntries={} extraFilesLoaded={} extraFilesDiscovered={} typeDefaults=(Arrow='{}', Bolt='{}')",
			_formIDToPreset.size(),
			_keywordToPreset.size(),
			appliedFormEntries,
			appliedKeywordEntries,
			extraFilesLoaded,
			extraFilesDiscovered,
			_arrowDefaultPreset,
			_boltDefaultPreset);
	}

	void ReskinSystem::PrewarmDefaultWeaponPresets()
	{
		if (!_enabled) {
			return;
		}

		uint32_t warmedCount = 0;
		if (PrimePresetById("Default")) {
			warmedCount++;
		}
		if (!_arrowDefaultPreset.empty() && _arrowDefaultPreset != "Default") {
			if (PrimePresetById(_arrowDefaultPreset)) {
				warmedCount++;
			}
		}
		if (!_boltDefaultPreset.empty() && _boltDefaultPreset != "Default" &&
			_boltDefaultPreset != _arrowDefaultPreset) {
			if (PrimePresetById(_boltDefaultPreset)) {
				warmedCount++;
			}
		}

		if (Config::AmmoWheel::Debug::LogAssetLoading || Config::AmmoWheel::Debug::LogPerf) {
			logger::info("[AmmoWheelReskinUnified] Prewarmed {} default preset(s)", warmedCount);
		}
	}

	bool ReskinSystem::PrimePresetById(const std::string& presetId)
	{
		if (presetId.empty()) {
			return false;
		}

		auto it = _presets.find(presetId);
		if (it == _presets.end()) {
			return false;
		}

		PrimePresetAssets(it->second);
		return true;
	}

	void ReskinSystem::PrimePresetAssets(const Preset& preset)
	{
		for (size_t i = 0; i < static_cast<size_t>(VisualTarget::COUNT); ++i) {
			const AssetDef& asset = preset.assets[i];
			if (!asset.enabled || asset.usePrimitive || asset.type == AssetType::None) {
				continue;
			}

			if (asset.type == AssetType::StaticPNG || asset.type == AssetType::AtlasSheet) {
				std::string fullPath = _basePath + "\\" + asset.staticPath;
				int warmRequestPx = 256;
				if (asset.type == AssetType::AtlasSheet) {
					const int atlasMult = (std::max)(1, (std::max)(
						static_cast<int>(asset.atlasCols),
						static_cast<int>(asset.atlasRows)));
					warmRequestPx = std::clamp(warmRequestPx * atlasMult, 64, static_cast<int>(_maxTextureSize));
				}
				(void)GetTexture(fullPath, warmRequestPx);
			} else if (asset.type == AssetType::Flipbook) {
				// Warm and cache this flipbook ahead of first render-time use.
				(void)GetFlipbookFrame(asset.flipbook, 0.0f, 0, 0);
			}
		}
	}

	ResolvedEntry ReskinSystem::ResolveForAmmo(RE::TESAmmo* ammo)
	{
		ResolvedEntry result;
		result.preset = &_defaultPreset;
		result.source = ResolutionSource::Fallback;
		result.matchedKey = "Default";
		
		if (!_enabled || !ammo) {
			return result;
		}
		
		RE::FormID formID = ammo->GetFormID();
	
	// Debug: Log FormID for Bloodcursed/Sunhallowed arrows
	if (Config::AmmoWheel::Debug::LogPresetResolution) {
		std::string name = ammo->GetName();
		if (name.find("Bloodcursed") != std::string::npos || name.find("Sunhallowed") != std::string::npos) {
			logger::info("[AmmoWheelReskinUnified] Resolving {} with FormID 0x{:08X}", name, formID);
			logger::info("[AmmoWheelReskinUnified] FormID map has {} entries", _formIDToPreset.size());
			
			// Check if the specific FormIDs are in the map
			auto it1 = _formIDToPreset.find(0x020098A0);
			auto it2 = _formIDToPreset.find(0x020098A1);
			logger::info("[AmmoWheelReskinUnified] Map contains 0x020098A0: {}", it1 != _formIDToPreset.end());
			logger::info("[AmmoWheelReskinUnified] Map contains 0x020098A1: {}", it2 != _formIDToPreset.end());
			if (it1 != _formIDToPreset.end()) {
				logger::info("[AmmoWheelReskinUnified] 0x020098A0 maps to: {}", it1->second);
			}
			if (it2 != _formIDToPreset.end()) {
				logger::info("[AmmoWheelReskinUnified] 0x020098A1 maps to: {}", it2->second);
			}
		}
	}
	
	// Priority 1: FormID
	{
		auto it = _formIDToPreset.find(formID);
		if (it != _formIDToPreset.end()) {
			auto presetIt = _presets.find(it->second);
			if (presetIt != _presets.end()) {
				result.preset = &presetIt->second;
				result.source = ResolutionSource::FormID;
				result.matchedKey = fmt::format("0x{:08X}", formID);
				
				if (Config::AmmoWheel::Debug::LogPresetResolution) {
					std::string name = ammo->GetName();
					if (name.find("Bloodcursed") != std::string::npos || name.find("Sunhallowed") != std::string::npos) {
						logger::info("[AmmoWheelReskinUnified] SUCCESS: Found preset {} for FormID 0x{:08X}", it->second, formID);
					}
				}
				return result;
			} else {
				if (Config::AmmoWheel::Debug::LogPresetResolution) {
					logger::warn("[AmmoWheelReskinUnified] FormID 0x{:08X} mapped to preset '{}' but preset not found!", formID, it->second);
				}
			}
		}
	}
		
		// Priority 2: Keyword
		const auto keywordForm = ammo->As<RE::BGSKeywordForm>();
		if (keywordForm) {
			for (const auto& [keyword, presetId] : _keywordToPreset) {
				if (keywordForm->HasKeywordString(keyword)) {
					auto presetIt = _presets.find(presetId);
					if (presetIt != _presets.end()) {
						result.preset = &presetIt->second;
						result.source = ResolutionSource::Keyword;
						result.matchedKey = keyword;
						return result;
					}
				}
			}
		}
		
		// Priority 3: Type default
		const std::string& typeDefault = ammo->IsBolt() ? _boltDefaultPreset : _arrowDefaultPreset;
		{
			auto presetIt = _presets.find(typeDefault);
			if (presetIt != _presets.end()) {
				result.preset = &presetIt->second;
				result.source = ResolutionSource::TypeDefault;
				result.matchedKey = ammo->IsBolt() ? "Bolt" : "Arrow";
				return result;
			}
		}
		
		// Fallback
		return result;
	}

	bool ReskinSystem::DrawTarget(VisualTarget target, const ResolvedEntry& entry,
		const DrawContext& ctx, ImDrawList* drawList)
	{
		const bool perfEnabled = Config::AmmoWheel::Debug::LogPerf;
		const auto perfStart = perfEnabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		auto finalizePerf = [&]() {
			if (perfEnabled) {
				const auto perfEnd = std::chrono::steady_clock::now();
				_perfDrawTargetMsAccum += std::chrono::duration<double, std::milli>(perfEnd - perfStart).count();
				_perfDrawTargetCalls++;
			}
		};

		if (!_enabled || !entry.preset || !drawList) {
			finalizePerf();
			return false;
		}
		const bool logThisTarget = Config::AmmoWheel::Debug::LogAssetLoading &&
			(target == VisualTarget::PopupBubble || target == VisualTarget::SlotDivider);

		DrawContext ctxAdjusted = ctx;
		if (!(ctxAdjusted.sizeScale > 0.0f) || !std::isfinite(ctxAdjusted.sizeScale)) {
			ctxAdjusted.sizeScale = 1.0f;
		}
		LayoutOverride layoutOverride = GetLayoutOverrideSnapshot(target);
		if (IsSlotIndicatorTarget(target)) {
			// Keep indicator transforms locked to SlotFrame so slot-cover
			// indicator assets stay aligned with frame tuning across all slots.
			const LayoutOverride slotFrameLayout = GetLayoutOverrideSnapshot(VisualTarget::SlotFrame);
			layoutOverride.scale = SanitizeLayoutScale(slotFrameLayout.scale * layoutOverride.scale);
			layoutOverride.offsetX = SanitizeLayoutOffset(slotFrameLayout.offsetX + layoutOverride.offsetX);
			layoutOverride.offsetY = SanitizeLayoutOffset(slotFrameLayout.offsetY + layoutOverride.offsetY);
			layoutOverride.offsetRadial = SanitizeLayoutOffset(slotFrameLayout.offsetRadial + layoutOverride.offsetRadial);
			layoutOverride.offsetTangential = SanitizeLayoutOffset(slotFrameLayout.offsetTangential + layoutOverride.offsetTangential);
			layoutOverride.angleOffsetDeg = SanitizeLayoutAngleDeg(slotFrameLayout.angleOffsetDeg + layoutOverride.angleOffsetDeg);
			layoutOverride.selfRotationDeg = SanitizeLayoutAngleDeg(slotFrameLayout.selfRotationDeg + layoutOverride.selfRotationDeg);
			layoutOverride.opacity = SanitizeLayoutOpacity(slotFrameLayout.opacity * layoutOverride.opacity);
		}
		const float baseAngle = ctxAdjusted.slotAngleRad;
		ctxAdjusted.sizeScale = SanitizeLayoutScale(ctxAdjusted.sizeScale * layoutOverride.scale);
		ctxAdjusted.center.x += layoutOverride.offsetX;
		ctxAdjusted.center.y += layoutOverride.offsetY;
		if (std::fabs(layoutOverride.offsetRadial) > 0.001f || std::fabs(layoutOverride.offsetTangential) > 0.001f) {
			const float cosA = std::cos(baseAngle);
			const float sinA = std::sin(baseAngle);
			ctxAdjusted.center.x += cosA * layoutOverride.offsetRadial - sinA * layoutOverride.offsetTangential;
			ctxAdjusted.center.y += sinA * layoutOverride.offsetRadial + cosA * layoutOverride.offsetTangential;
		}
		ctxAdjusted.slotAngleRad = baseAngle + layoutOverride.angleOffsetDeg * (3.14159265f / 180.0f);
		ctxAdjusted.alphaMult *= layoutOverride.opacity;

		// Reskin-only pulse for hovered indicator overlays so custom slot-cover
		// assets can achieve a smooth breathing highlight while hovering.
		if (target == VisualTarget::IndicatorHovered && ctxAdjusted.hovered && Config::AmmoWheel::HoverPulseEnabled) {
			const float pulseTime = static_cast<float>(ImGui::GetTime()) * Config::AmmoWheel::HoverPulseSpeed;
			const float pulse = 0.5f + 0.5f * std::sin(pulseTime);
			const float sizeAmp = std::clamp(Config::AmmoWheel::HoverPulseSize / 100.0f, 0.0f, 0.30f);
			ctxAdjusted.sizeScale = SanitizeLayoutScale(ctxAdjusted.sizeScale * (1.0f + sizeAmp * pulse));
			ctxAdjusted.alphaMult *= (0.70f + 0.30f * pulse);
		}
		// Match primitive selected blink behavior for selected-indicator assets:
		// alpha pulses using SelectedBlink* config so the whole selected slot can
		// appear to "breathe" when indicator artwork covers the slot.
		if (target == VisualTarget::IndicatorSelected && ctxAdjusted.selected) {
			if (Config::AmmoWheel::SelectedBlinkEnabled) {
				const float time = static_cast<float>(ImGui::GetTime());
				const float phase = std::sin(time * Config::AmmoWheel::SelectedBlinkSpeedHz * 2.0f * 3.14159f);
				const float t = 0.5f + 0.5f * phase;
				const float blinkAlpha = std::clamp(
					Config::AmmoWheel::SelectedBlinkMinAlpha +
						(Config::AmmoWheel::SelectedBlinkMaxAlpha - Config::AmmoWheel::SelectedBlinkMinAlpha) * t,
					0.0f,
					1.0f);
				ctxAdjusted.alphaMult *= blinkAlpha;
			}
			ctxAdjusted.sizeScale = SanitizeLayoutScale(ctxAdjusted.sizeScale * Config::AmmoWheel::SelectedIndicatorSizeScale);
		}
		if (IsSlotIndicatorTarget(target)) {
			// Indicator targets already have explicit pulse wiring in DrawTarget.
			// Disable generic selected-tint pulse to avoid stacked double blinking.
			ctxAdjusted.allowSelectedTintPulse = false;
			// Keep indicator rendering deterministic; geometry is derived from SlotFrame
			// below so indicator overlays follow the same slot-frame axis/fit.
			ctxAdjusted.autoCenterByAlphaBounds = false;
		}
		
		size_t idx = static_cast<size_t>(target);
		const AssetDef* assetPtr = &entry.preset->assets[idx];
		
		// CHECK FOR PRIMITIVE FALLBACK FLAG
		// If usePrimitive is true, force primitive fallback regardless of asset
		if (assetPtr->usePrimitive) {
			if (logThisTarget) {
				logger::info("[AmmoWheelReskinUnified] DrawTarget: {} usePrimitive=true, forcing primitive fallback", 
					GetTargetName(target));
			}
			finalizePerf();
			return false;  // Force primitive fallback
		}
		
		// FALLBACK CHAIN:
		// 1. Try the resolved preset's asset
		// 2. If not enabled, try the Default preset's asset as secondary fallback
		// 3. If still not enabled, return false (caller uses primitive)
		
		if (!assetPtr->enabled || assetPtr->type == AssetType::None) {
			// Secondary fallback: check Default preset (from _presets, not _defaultPreset)
			if (logThisTarget) {
				logger::info("[AmmoWheelReskinUnified] DrawTarget: {} asset not enabled in preset '{}', checking Default fallback", 
					GetTargetName(target), entry.preset->id);
			}
			
			auto defaultIt = _presets.find("Default");
			if (defaultIt != _presets.end()) {
				const AssetDef& defaultAsset = defaultIt->second.assets[idx];
				if (defaultAsset.enabled && defaultAsset.type != AssetType::None) {
					if (logThisTarget) {
						logger::info("[AmmoWheelReskinUnified] DrawTarget: Using Default preset's {} asset as fallback", 
							GetTargetName(target));
					}
					assetPtr = &defaultAsset;
				} else {
					if (logThisTarget) {
						logger::info("[AmmoWheelReskinUnified] DrawTarget: Default preset also doesn't have {} enabled, using primitive", 
							GetTargetName(target));
					}
					finalizePerf();
					return false;  // Default preset also doesn't have this asset enabled
				}
			} else {
				if (logThisTarget) {
					logger::warn("[AmmoWheelReskinUnified] DrawTarget: No Default preset found in _presets map!");
				}
				finalizePerf();
				return false;  // No Default preset found
			}
		}
		
		const AssetDef& asset = *assetPtr;
		const AssetDef* slotFrameGeometryAsset = nullptr;
		if (IsSlotIndicatorTarget(target)) {
			const auto isDrawableAsset = [](const AssetDef& candidate) {
				return candidate.enabled &&
					!candidate.usePrimitive &&
					candidate.type != AssetType::None;
			};

			const size_t slotFrameIdx = static_cast<size_t>(VisualTarget::SlotFrame);
			if (slotFrameIdx < static_cast<size_t>(VisualTarget::COUNT)) {
				const AssetDef& presetSlotFrame = entry.preset->assets[slotFrameIdx];
				if (isDrawableAsset(presetSlotFrame)) {
					slotFrameGeometryAsset = &presetSlotFrame;
				} else {
					auto defaultIt = _presets.find("Default");
					if (defaultIt != _presets.end()) {
						const AssetDef& defaultSlotFrame = defaultIt->second.assets[slotFrameIdx];
						if (isDrawableAsset(defaultSlotFrame)) {
							slotFrameGeometryAsset = &defaultSlotFrame;
						}
					}
				}
			}
		}

		auto buildDrawAssetForTarget = [&](const AssetDef& sourceAsset) {
			AssetDef drawAsset = sourceAsset;
			if (slotFrameGeometryAsset) {
				// For indicator targets, inherit SlotFrame placement geometry so
				// indicator overlays sit exactly on top of slot frames.
				drawAsset.fitMode = slotFrameGeometryAsset->fitMode;
				drawAsset.fixedSizePx = slotFrameGeometryAsset->fixedSizePx;
				drawAsset.paddingPx = slotFrameGeometryAsset->paddingPx;
				drawAsset.rotationSafetyScale = slotFrameGeometryAsset->rotationSafetyScale;
				drawAsset.rotationMode = slotFrameGeometryAsset->rotationMode;
				drawAsset.rotationOffsetDeg = slotFrameGeometryAsset->rotationOffsetDeg;
			}
			drawAsset.rotationOffsetDeg = SanitizeLayoutAngleDeg(drawAsset.rotationOffsetDeg + layoutOverride.selfRotationDeg);
			return drawAsset;
		};

		auto computeRequestedMaxPx = [&](const AssetDef& drawAsset) -> int {
			float drawSize = drawAsset.fixedSizePx;
			if (drawAsset.fitMode == FitMode::FitInsideSlot) {
				const float heightLimit =
					ctxAdjusted.radius * 2.0f * drawAsset.rotationSafetyScale - drawAsset.paddingPx * 2.0f;
				float widthLimit = heightLimit;
				if (ctxAdjusted.maxFitSizePx > 1.0f && std::isfinite(ctxAdjusted.maxFitSizePx)) {
					widthLimit = ctxAdjusted.maxFitSizePx - drawAsset.paddingPx * 2.0f;
				}
				drawSize = (std::max)(heightLimit, widthLimit);
			}
			if (!std::isfinite(drawSize)) {
				drawSize = drawAsset.fixedSizePx;
			}
			drawSize *= ctxAdjusted.sizeScale;
			drawSize = (std::max)(drawSize, 1.0f);
			return static_cast<int>(std::ceil(drawSize));
		};

		auto computeRequestedTexturePx = [&](const AssetDef& drawAsset) -> int {
			int requestedPx = computeRequestedMaxPx(drawAsset);
			if (drawAsset.type == AssetType::AtlasSheet) {
				const int atlasMult = (std::max)(1, (std::max)(
					static_cast<int>(drawAsset.atlasCols),
					static_cast<int>(drawAsset.atlasRows)));
				requestedPx = std::clamp(requestedPx * atlasMult, 1, static_cast<int>(_maxTextureSize));
			}
			return requestedPx;
		};

		const bool popupFlipbookBlocked =
			target == VisualTarget::PopupBubble && !Config::AmmoWheel::PopupFlipbookEnabled;
		
		// Try to draw the asset
		bool drewAsset = false;
		const AssetDef drawAsset = buildDrawAssetForTarget(asset);
		if (drawAsset.type == AssetType::StaticPNG) {
			std::string fullPath = _basePath + "\\" + drawAsset.staticPath;
			TextureHandle* tex = GetTexture(fullPath, computeRequestedTexturePx(drawAsset));
			if (tex && tex->IsValid()) {
				DrawStaticTexture(tex, drawAsset, ctxAdjusted, drawList);
				drewAsset = true;
			}
		} else if (drawAsset.type == AssetType::AtlasSheet) {
			std::string fullPath = _basePath + "\\" + drawAsset.staticPath;
			TextureHandle* tex = GetTexture(fullPath, computeRequestedTexturePx(drawAsset));
			if (tex && tex->IsValid()) {
				drewAsset = DrawAtlasTexture(tex, drawAsset, ctxAdjusted, drawList);
			}
		} else if (drawAsset.type == AssetType::Flipbook) {
			if (popupFlipbookBlocked) {
				if (logThisTarget) {
					static bool loggedOnce = false;
					if (!loggedOnce) {
						logger::info("[AmmoWheelReskinUnified] PopupFlipbookEnabled=false, blocking PopupBubble flipbook (static/atlas still allowed)");
						loggedOnce = true;
					}
				}
			} else {
				drewAsset = DrawFlipbookTexture(drawAsset.flipbook, drawAsset, ctxAdjusted, drawList);
			}
		}
		
		// If asset loading failed AND we're not already using Default preset, try Default as fallback
		if (!drewAsset && entry.preset->id != "Default") {
			if (logThisTarget) {
				logger::info("[AmmoWheelReskinUnified] DrawTarget: {} asset loading failed for preset '{}', trying Default fallback", 
					GetTargetName(target), entry.preset->id);
			}
			
			auto defaultIt = _presets.find("Default");
			if (defaultIt != _presets.end()) {
				const AssetDef& defaultAsset = defaultIt->second.assets[idx];
				if (defaultAsset.enabled && defaultAsset.type != AssetType::None) {
					if (logThisTarget) {
						logger::info("[AmmoWheelReskinUnified] DrawTarget: Using Default preset's {} asset as fallback after load failure", 
							GetTargetName(target));
					}
					
					// Try to draw Default preset's asset
					const AssetDef fallbackDrawAsset = buildDrawAssetForTarget(defaultAsset);
					if (fallbackDrawAsset.type == AssetType::StaticPNG) {
						std::string fullPath = _basePath + "\\" + fallbackDrawAsset.staticPath;
						TextureHandle* tex = GetTexture(fullPath, computeRequestedTexturePx(fallbackDrawAsset));
						if (tex && tex->IsValid()) {
							DrawStaticTexture(tex, fallbackDrawAsset, ctxAdjusted, drawList);
							finalizePerf();
							return true;
						}
					} else if (fallbackDrawAsset.type == AssetType::AtlasSheet) {
						std::string fullPath = _basePath + "\\" + fallbackDrawAsset.staticPath;
						TextureHandle* tex = GetTexture(fullPath, computeRequestedTexturePx(fallbackDrawAsset));
						if (tex && tex->IsValid()) {
							const bool drewFallbackAtlas = DrawAtlasTexture(
								tex,
								fallbackDrawAsset,
								ctxAdjusted,
								drawList);
							finalizePerf();
							return drewFallbackAtlas;
						}
					} else if (fallbackDrawAsset.type == AssetType::Flipbook) {
						if (popupFlipbookBlocked) {
							finalizePerf();
							return false;
						}
						const bool drewFallbackFlipbook = DrawFlipbookTexture(
							fallbackDrawAsset.flipbook,
							fallbackDrawAsset,
							ctxAdjusted,
							drawList);
						finalizePerf();
						return drewFallbackFlipbook;
					}
				}
			}
		}
		
		finalizePerf();
		return drewAsset;
	}

	bool ReskinSystem::DrawDigitString(VisualTarget target, const ResolvedEntry& entry,
		const DrawContext& baseCtx, std::string_view digitsOnly, float digitHeightPx,
		ImDrawList* drawList, float* outTotalWidthPx)
	{
		if (outTotalWidthPx) {
			*outTotalWidthPx = 0.0f;
		}
		const bool isDigitAtlasTarget =
			target == VisualTarget::AmmoCountDigits ||
			target == VisualTarget::DamageDigits ||
			target == VisualTarget::MaxDamageDigits;
		if (!isDigitAtlasTarget || !_enabled || !entry.preset || !drawList ||
			digitsOnly.empty() || !(digitHeightPx > 0.0f)) {
			return false;
		}

		DrawContext adjustedBaseCtx = baseCtx;
		if (!(adjustedBaseCtx.sizeScale > 0.0f) || !std::isfinite(adjustedBaseCtx.sizeScale)) {
			adjustedBaseCtx.sizeScale = 1.0f;
		}
		const LayoutOverride layoutOverride = GetLayoutOverrideSnapshot(target);
		const float baseAngle = adjustedBaseCtx.slotAngleRad;
		adjustedBaseCtx.sizeScale = SanitizeLayoutScale(adjustedBaseCtx.sizeScale * layoutOverride.scale);
		adjustedBaseCtx.center.x += layoutOverride.offsetX;
		adjustedBaseCtx.center.y += layoutOverride.offsetY;
		if (std::fabs(layoutOverride.offsetRadial) > 0.001f || std::fabs(layoutOverride.offsetTangential) > 0.001f) {
			const float cosA = std::cos(baseAngle);
			const float sinA = std::sin(baseAngle);
			adjustedBaseCtx.center.x += cosA * layoutOverride.offsetRadial - sinA * layoutOverride.offsetTangential;
			adjustedBaseCtx.center.y += sinA * layoutOverride.offsetRadial + cosA * layoutOverride.offsetTangential;
		}
		adjustedBaseCtx.slotAngleRad = baseAngle + layoutOverride.angleOffsetDeg * (3.14159265f / 180.0f);
		adjustedBaseCtx.alphaMult *= layoutOverride.opacity;

		const size_t idx = static_cast<size_t>(target);
		const AssetDef* assetPtr = &entry.preset->assets[idx];
		if (assetPtr->usePrimitive) {
			return false;
		}
		if (!assetPtr->enabled || assetPtr->type == AssetType::None) {
			auto defaultIt = _presets.find("Default");
			if (defaultIt == _presets.end()) {
				return false;
			}
			const AssetDef& defaultAsset = defaultIt->second.assets[idx];
			if (!defaultAsset.enabled || defaultAsset.usePrimitive || defaultAsset.type == AssetType::None) {
				return false;
			}
			assetPtr = &defaultAsset;
		}

		const AssetDef& asset = *assetPtr;
		if (asset.type != AssetType::StaticPNG || asset.staticPath.empty()) {
			return false;
		}

		const int cols = (std::max)(1, static_cast<int>(asset.atlasCols));
		const int rows = (std::max)(1, static_cast<int>(asset.atlasRows));
		const float safeDigitHeightPx = (std::max)(digitHeightPx, 1.0f);
		const float effectiveScale = SanitizeLayoutScale(adjustedBaseCtx.sizeScale);
		const int requestedMaxPx = static_cast<int>(std::ceil(
			safeDigitHeightPx * effectiveScale * static_cast<float>((std::max)(cols, rows))));
		std::string fullPath = _basePath + "\\" + asset.staticPath;
		TextureHandle* tex = GetTexture(fullPath, requestedMaxPx);
		if (!tex || !tex->IsValid()) {
			return false;
		}

		const float cellW = static_cast<float>(tex->width) / static_cast<float>(cols);
		const float cellH = static_cast<float>(tex->height) / static_cast<float>(rows);
		if (!(cellW > 0.0f) || !(cellH > 0.0f)) {
			return false;
		}
		const float cellAspect = cellW / cellH;
		if (!(cellAspect > 0.0f) || !std::isfinite(cellAspect)) {
			return false;
		}
		const float digitW = safeDigitHeightPx * cellAspect * effectiveScale;
		if (!(digitW > 0.0f) || !std::isfinite(digitW)) {
			return false;
		}

		size_t validDigitCount = 0;
		for (char c : digitsOnly) {
			if (c >= '0' && c <= '9') {
				++validDigitCount;
			}
		}
		if (validDigitCount == 0) {
			return false;
		}

		const float baseSpacing = std::isfinite(asset.digitSpacingPx) ? asset.digitSpacingPx : 0.0f;
		const float spacingPx = baseSpacing + layoutOverride.digitSpacingOffsetPx;
		float spacing = spacingPx * effectiveScale;
		// Keep center-step positive to avoid mirrored/reversed digit order.
		spacing = (std::max)(spacing, -digitW + 1.0f);
		const float totalW = static_cast<float>(validDigitCount) * digitW +
			static_cast<float>((validDigitCount > 1) ? (validDigitCount - 1) : 0) * spacing;
		if (outTotalWidthPx) {
			*outTotalWidthPx = totalW;
		}
		const float startX = adjustedBaseCtx.center.x - totalW * 0.5f + digitW * 0.5f;
		AssetDef digitAsset = asset;
		digitAsset.fitMode = FitMode::FixedPixels;
		digitAsset.fixedSizePx = safeDigitHeightPx;
		digitAsset.rotationOffsetDeg = SanitizeLayoutAngleDeg(
			digitAsset.rotationOffsetDeg + layoutOverride.selfRotationDeg);

		size_t renderIndex = 0;
		for (char c : digitsOnly) {
			if (c < '0' || c > '9') {
				continue;
			}
			const int digit = c - '0';
			const int col = digit % cols;
			int row = digit / cols;
			if (row >= rows) {
				row = rows - 1;
			}

			const float colsF = static_cast<float>(cols);
			const float rowsF = static_cast<float>(rows);
			const ImVec2 uvMin(
				static_cast<float>(col) / colsF,
				static_cast<float>(row) / rowsF);
			const ImVec2 uvMax(
				static_cast<float>(col + 1) / colsF,
				static_cast<float>(row + 1) / rowsF);

			DrawContext digitCtx = adjustedBaseCtx;
			digitCtx.center = ImVec2(
				startX + static_cast<float>(renderIndex) * (digitW + spacing),
				adjustedBaseCtx.center.y);
			digitCtx.radius = safeDigitHeightPx * 0.5f;
			DrawStaticTextureRegion(tex, digitAsset, digitCtx, drawList, uvMin, uvMax, cellAspect);
			++renderIndex;
		}

		return renderIndex > 0;
	}

	LayoutOverride ReskinSystem::GetLayoutOverrideSnapshot(VisualTarget target) const
	{
		std::lock_guard<std::mutex> lock(_mutex);
		return GetLayoutOverride(target);
	}

	bool ReskinSystem::HasAssetForTarget(VisualTarget target, const ResolvedEntry& entry) const
	{
		if (!_enabled || !entry.preset) {
			return false;
		}

		std::lock_guard<std::mutex> lock(_mutex);
		const size_t idx = static_cast<size_t>(target);
		if (idx >= static_cast<size_t>(VisualTarget::COUNT)) {
			return false;
		}

		const AssetDef& presetAsset = entry.preset->assets[idx];
		if (presetAsset.usePrimitive) {
			return false;
		}
		if (presetAsset.enabled && presetAsset.type != AssetType::None) {
			return true;
		}

		auto defaultIt = _presets.find("Default");
		if (defaultIt == _presets.end()) {
			return false;
		}

		const AssetDef& defaultAsset = defaultIt->second.assets[idx];
		if (defaultAsset.usePrimitive) {
			return false;
		}
		return defaultAsset.enabled && defaultAsset.type != AssetType::None;
	}

	TextureHandle* ReskinSystem::GetTexture(const std::string& path, int requestedMaxPx)
	{
		const bool perfEnabled = Config::AmmoWheel::Debug::LogPerf;
		const auto perfStart = perfEnabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		auto finalizeHit = [&]() {
			if (perfEnabled) {
				const auto perfEnd = std::chrono::steady_clock::now();
				_perfGetTextureMsAccum += std::chrono::duration<double, std::milli>(perfEnd - perfStart).count();
			}
			_perfGetTextureCalls++;
			_perfTextureCacheHits++;
		};
		auto finalizeMiss = [&]() {
			if (perfEnabled) {
				const auto perfEnd = std::chrono::steady_clock::now();
				_perfGetTextureMsAccum += std::chrono::duration<double, std::milli>(perfEnd - perfStart).count();
			}
			_perfGetTextureCalls++;
			_perfTextureCacheMisses++;
		};

		if (!_device) {
			finalizeMiss();
			return nullptr;
		}

		auto normalizedPathIt = _normalizedPathCache.find(path);
		if (normalizedPathIt == _normalizedPathCache.end()) {
			normalizedPathIt = _normalizedPathCache.emplace(path, NormalizePath(path)).first;
		}
		const std::string& normalizedPath = normalizedPathIt->second;
		const bool isSvg = EndsWithNoCase(normalizedPath, ".svg");
		const int svgBucketPx = isSvg ? ComputeSvgRasterBucketPx(requestedMaxPx, _maxTextureSize) : 0;
		const std::string cacheKey = isSvg ? (normalizedPath + "|svg@" + std::to_string(svgBucketPx)) : normalizedPath;
		auto cacheFailureAndReturn = [&]() -> TextureHandle* {
			TextureHandle failed;
			failed.valid = false;
			failed.lastUsed = std::chrono::steady_clock::now();
			_textureCache[cacheKey] = failed;
			finalizeMiss();
			return nullptr;
		};

		auto it = _textureCache.find(cacheKey);
		if (it != _textureCache.end()) {
			it->second.lastUsed = std::chrono::steady_clock::now();
			finalizeHit();
			return it->second.IsValid() ? &it->second : nullptr;
		}

		if (!SafeFileExists(path)) {
			return cacheFailureAndReturn();
		}

		uint64_t fileSize = 0;
		if (!SafeGetFileSize(path, fileSize) || fileSize > _maxPngFileBytes ||
			fileSize > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
			logger::warn("[AmmoWheelReskinUnified] File too large or unreadable: {}", path);
			return cacheFailureAndReturn();
		}

		std::ifstream file(path, std::ios::binary);
		if (!file) {
			return cacheFailureAndReturn();
		}

		std::vector<unsigned char> fileData(static_cast<size_t>(fileSize));
		file.read(reinterpret_cast<char*>(fileData.data()), static_cast<std::streamsize>(fileData.size()));
		if (!file) {
			return cacheFailureAndReturn();
		}
		file.close();

		std::vector<unsigned char> decodedRgba;
		int width = 0;
		int height = 0;
		if (isSvg) {
			if (!DecodeSvgToRgba(fileData, svgBucketPx, _maxTextureSize, decodedRgba, width, height)) {
				logger::warn("[AmmoWheelReskinUnified] Failed to decode SVG: {}", path);
				return cacheFailureAndReturn();
			}
			if (Config::AmmoWheel::Debug::LogAssetLoading || Config::AmmoWheel::Debug::LogPerf) {
				logger::info("[AmmoWheelReskinUnified] Decoded SVG: path='{}' bucket={} output={}x{}",
					path, svgBucketPx, width, height);
			}
		} else {
			int channels = 0;
			unsigned char* imageData = stbi_load_from_memory(
				fileData.data(), static_cast<int>(fileData.size()),
				&width, &height, &channels, 4);
			if (!imageData) {
				logger::warn("[AmmoWheelReskinUnified] Failed to decode PNG: {} ({})",
					path, stbi_failure_reason() ? stbi_failure_reason() : "unknown");
				return cacheFailureAndReturn();
			}
			if (width <= 0 || height <= 0 || width > static_cast<int>(_maxTextureSize) ||
				height > static_cast<int>(_maxTextureSize)) {
				stbi_image_free(imageData);
				logger::warn("[AmmoWheelReskinUnified] Texture too large or invalid size: {}", path);
				return cacheFailureAndReturn();
			}
			const uint64_t pixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
			if (pixelCount == 0 || pixelCount > static_cast<uint64_t>((std::numeric_limits<size_t>::max)() / 4)) {
				stbi_image_free(imageData);
				logger::warn("[AmmoWheelReskinUnified] PNG decode overflow guard triggered: {}", path);
				return cacheFailureAndReturn();
			}
			decodedRgba.assign(imageData, imageData + static_cast<size_t>(pixelCount) * 4);
			stbi_image_free(imageData);
		}

		if (width <= 0 || height <= 0 || decodedRgba.empty()) {
			return cacheFailureAndReturn();
		}

		// Compute visible-content center from alpha bounds once per texture.
		// This is used by indicator targets to auto-center non-uniform canvases.
		bool hasContentBounds = false;
		float contentOffsetXNorm = 0.0f;
		float contentOffsetYNorm = 0.0f;
		{
			int minX = width;
			int minY = height;
			int maxX = -1;
			int maxY = -1;
			const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
			for (size_t p = 0; p < pixelCount; ++p) {
				const uint8_t alpha = decodedRgba[p * 4 + 3];
				if (alpha <= 8) {
					continue;
				}
				const int x = static_cast<int>(p % static_cast<size_t>(width));
				const int y = static_cast<int>(p / static_cast<size_t>(width));
				minX = (std::min)(minX, x);
				minY = (std::min)(minY, y);
				maxX = (std::max)(maxX, x);
				maxY = (std::max)(maxY, y);
			}
			if (maxX >= minX && maxY >= minY) {
				const float contentCx = (static_cast<float>(minX + maxX) + 1.0f) * 0.5f;
				const float contentCy = (static_cast<float>(minY + maxY) + 1.0f) * 0.5f;
				contentOffsetXNorm = std::clamp(contentCx / static_cast<float>(width) - 0.5f, -0.5f, 0.5f);
				contentOffsetYNorm = std::clamp(contentCy / static_cast<float>(height) - 0.5f, -0.5f, 0.5f);
				hasContentBounds = true;
			}
		}

		const uint64_t bytesEstimate = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4;
		if (bytesEstimate > _maxTotalBytes) {
			logger::warn("[AmmoWheelReskinUnified] Texture exceeds Safety.MaxTotalBytesMB: {} ({} bytes > {})",
				path, bytesEstimate, _maxTotalBytes);
			return cacheFailureAndReturn();
		}

		// LRU eviction to enforce Safety.MaxTotalBytesMB across PNG + SVG variants.
		bool evictedAny = false;
		while (_totalTextureBytes + bytesEstimate > _maxTotalBytes && !_textureCache.empty()) {
			auto lruIt = _textureCache.end();
			for (auto entryIt = _textureCache.begin(); entryIt != _textureCache.end(); ++entryIt) {
				if (lruIt == _textureCache.end() || entryIt->second.lastUsed < lruIt->second.lastUsed) {
					lruIt = entryIt;
				}
			}
			if (lruIt == _textureCache.end()) {
				break;
			}
			if (lruIt->second.srv) {
				lruIt->second.srv->Release();
				lruIt->second.srv = nullptr;
			}
			if (lruIt->second.bytesEstimate >= _totalTextureBytes) {
				_totalTextureBytes = 0;
			} else {
				_totalTextureBytes -= lruIt->second.bytesEstimate;
			}
			_textureCache.erase(lruIt);
			evictedAny = true;
		}
		if (evictedAny) {
			// Flipbook instances store TextureHandle*. Any eviction can invalidate those pointers.
			_flipbookCache.clear();
			_totalFrameCount = 0;
		}
		if (_totalTextureBytes + bytesEstimate > _maxTotalBytes) {
			logger::warn("[AmmoWheelReskinUnified] Unable to evict enough texture cache bytes for '{}'", path);
			return cacheFailureAndReturn();
		}

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = static_cast<UINT>(width);
		desc.Height = static_cast<UINT>(height);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA subResource = {};
		subResource.pSysMem = decodedRgba.data();
		subResource.SysMemPitch = width * 4;

		ID3D11Texture2D* texture = nullptr;
		HRESULT hr = _device->CreateTexture2D(&desc, &subResource, &texture);
		if (FAILED(hr)) {
			logger::error("[AmmoWheelReskinUnified] CreateTexture2D failed: {}", path);
			return cacheFailureAndReturn();
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		TextureHandle handle;
		hr = _device->CreateShaderResourceView(texture, &srvDesc, &handle.srv);
		texture->Release();
		if (FAILED(hr)) {
			logger::error("[AmmoWheelReskinUnified] CreateSRV failed: {}", path);
			return cacheFailureAndReturn();
		}

		handle.width = width;
		handle.height = height;
		handle.bytesEstimate = bytesEstimate;
		handle.valid = true;
		handle.lastUsed = std::chrono::steady_clock::now();
		handle.hasContentBounds = hasContentBounds;
		handle.contentCenterOffsetXNorm = contentOffsetXNorm;
		handle.contentCenterOffsetYNorm = contentOffsetYNorm;

		_textureCache[cacheKey] = handle;
		_totalTextureBytes += handle.bytesEstimate;

		logger::debug("[AmmoWheelReskinUnified] Loaded texture: {} ({}x{})", path, width, height);
		finalizeMiss();
		return &_textureCache[cacheKey];
	}

	void ReskinSystem::DrawStaticTexture(TextureHandle* tex, const AssetDef& asset,
		const DrawContext& ctx, ImDrawList* drawList)
	{
		if (!tex || !tex->IsValid()) {
			return;
		}
		const float fullAspect = static_cast<float>(tex->width) / static_cast<float>(tex->height);
		DrawStaticTextureRegion(tex, asset, ctx, drawList,
			ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), fullAspect);
	}

	void ReskinSystem::DrawStaticTextureRegion(TextureHandle* tex, const AssetDef& asset,
		const DrawContext& ctx, ImDrawList* drawList,
		ImVec2 uvMin, ImVec2 uvMax, float regionAspectRatio)
	{
		if (!tex || !tex->IsValid() || !drawList) {
			return;
		}

		float drawSize = asset.fixedSizePx;
		float widthLimit = drawSize;
		float heightLimit = drawSize;
		bool hasFitBox = false;
		if (asset.fitMode == FitMode::FitInsideSlot) {
			heightLimit = ctx.radius * 2.0f * asset.rotationSafetyScale - asset.paddingPx * 2.0f;
			if (!std::isfinite(heightLimit) || heightLimit <= 0.0f) {
				heightLimit = asset.fixedSizePx;
			}
			widthLimit = heightLimit;
			if (ctx.maxFitSizePx > 1.0f && std::isfinite(ctx.maxFitSizePx)) {
				widthLimit = ctx.maxFitSizePx - asset.paddingPx * 2.0f;
			}
			if (!std::isfinite(widthLimit) || widthLimit <= 0.0f) {
				widthLimit = heightLimit;
			}
			drawSize = (std::max)(heightLimit, widthLimit);
			hasFitBox = (widthLimit > 0.0f && heightLimit > 0.0f);
		}
		if (!std::isfinite(drawSize) || drawSize <= 0.0f) {
			drawSize = asset.fixedSizePx;
		}
		if (!std::isfinite(drawSize) || drawSize <= 0.0f) {
			drawSize = 1.0f;
		}

		float aspectRatio = regionAspectRatio;
		if (!(aspectRatio > 0.0f) || !std::isfinite(aspectRatio)) {
			aspectRatio = static_cast<float>(tex->width) / static_cast<float>(tex->height);
		}
		float drawWidth = drawSize;
		float drawHeight = drawSize;
		if (hasFitBox) {
			// Preserve asset aspect while fitting to slot box (tangential width + radial height).
			const float boxAspect = widthLimit / heightLimit;
			if (aspectRatio >= boxAspect) {
				drawWidth = widthLimit;
				drawHeight = widthLimit / aspectRatio;
			} else {
				drawHeight = heightLimit;
				drawWidth = heightLimit * aspectRatio;
			}
		} else if (aspectRatio > 1.0f) {
			drawHeight = drawSize / aspectRatio;
		} else {
			drawWidth = drawSize * aspectRatio;
		}
		const float sizeScale = SanitizeLayoutScale(ctx.sizeScale);
		drawWidth *= sizeScale;
		drawHeight *= sizeScale;

		float rotation = 0.0f;
		switch (asset.rotationMode) {
			case RotationMode::FollowSlot:
				rotation = ctx.slotAngleRad + asset.rotationOffsetDeg * (3.14159265f / 180.0f);
				break;
			case RotationMode::Upright:
			case RotationMode::Fixed:
				rotation = asset.rotationOffsetDeg * (3.14159265f / 180.0f);
				break;
		}

		ImU32 tint = asset.tintColor;

		// Apply hover brightness effect if enabled (tint the PNG brighter)
		if (ctx.hovered && Config::AmmoWheel::HoverBrightnessEnabled) {
			float strength = Config::AmmoWheel::HoverBrightnessStrength;
			ImVec4 f = ImGui::ColorConvertU32ToFloat4(tint);
			f.x = (std::min)(f.x * strength, 1.0f);
			f.y = (std::min)(f.y * strength, 1.0f);
			f.z = (std::min)(f.z * strength, 1.0f);
			tint = ImGui::ColorConvertFloat4ToU32(f);
		}

		// Apply selected blink effect if enabled (brightness pulse)
		if (ctx.allowSelectedTintPulse && ctx.selected && Config::AmmoWheel::SelectedBlinkEnabled) {
			float time = static_cast<float>(ImGui::GetTime());
			float blinkPhase = std::sin(time * Config::AmmoWheel::SelectedBlinkSpeedHz * 2.0f * 3.14159f);
			float blinkT = 0.5f + 0.5f * blinkPhase;  // 0 to 1
			float blinkStrength = 1.0f + Config::AmmoWheel::SelectedSlotBlinkStrength * blinkT;

			ImVec4 f = ImGui::ColorConvertU32ToFloat4(tint);
			f.x = (std::min)(f.x * blinkStrength, 1.0f);
			f.y = (std::min)(f.y * blinkStrength, 1.0f);
			f.z = (std::min)(f.z * blinkStrength, 1.0f);
			tint = ImGui::ColorConvertFloat4ToU32(f);
		}

		uint8_t alpha = static_cast<uint8_t>((tint >> 24) * asset.alpha * ctx.alphaMult);
		tint = (tint & 0x00FFFFFF) | (alpha << 24);

		float halfW = drawWidth * 0.5f;
		float halfH = drawHeight * 0.5f;
		float cosR = std::cos(rotation);
		float sinR = std::sin(rotation);
		float localCenterX = 0.0f;
		float localCenterY = 0.0f;
		if (ctx.autoCenterByAlphaBounds && tex->hasContentBounds) {
			localCenterX = -tex->contentCenterOffsetXNorm * drawWidth;
			localCenterY = -tex->contentCenterOffsetYNorm * drawHeight;
		}

		ImVec2 corners[4] = {
			{ localCenterX - halfW, localCenterY - halfH }, { localCenterX + halfW, localCenterY - halfH },
			{ localCenterX + halfW, localCenterY + halfH }, { localCenterX - halfW, localCenterY + halfH }
		};
		ImVec2 uvs[4] = {
			{ std::clamp(uvMin.x, 0.0f, 1.0f), std::clamp(uvMin.y, 0.0f, 1.0f) },
			{ std::clamp(uvMax.x, 0.0f, 1.0f), std::clamp(uvMin.y, 0.0f, 1.0f) },
			{ std::clamp(uvMax.x, 0.0f, 1.0f), std::clamp(uvMax.y, 0.0f, 1.0f) },
			{ std::clamp(uvMin.x, 0.0f, 1.0f), std::clamp(uvMax.y, 0.0f, 1.0f) }
		};

		for (int i = 0; i < 4; ++i) {
			float x = corners[i].x * cosR - corners[i].y * sinR;
			float y = corners[i].x * sinR + corners[i].y * cosR;
			corners[i] = ImVec2(ctx.center.x + x, ctx.center.y + y);
		}

		drawList->AddImageQuad(
			reinterpret_cast<ImTextureID>(tex->srv),
			corners[0], corners[1], corners[2], corners[3],
			uvs[0], uvs[1], uvs[2], uvs[3],
			tint);
	}

	bool ReskinSystem::DrawAtlasTexture(TextureHandle* tex, const AssetDef& asset,
		const DrawContext& ctx, ImDrawList* drawList)
	{
		if (!tex || !tex->IsValid() || !drawList) {
			return false;
		}

		const int cols = (std::max)(1, static_cast<int>(asset.atlasCols));
		const int rows = (std::max)(1, static_cast<int>(asset.atlasRows));
		const int totalCells = (std::max)(1, cols * rows);
		const int configuredFrameCount = static_cast<int>(asset.flipbook.frameCount);
		const int frameCount = std::clamp(
			configuredFrameCount > 0 ? configuredFrameCount : totalCells,
			1,
			totalCells);

		int frameIndex = 0;
		if (frameCount > 1) {
			if (asset.flipbook.progressDriven) {
				if (ctx.progress > 1.0f) {
					frameIndex = static_cast<int>(std::lround(ctx.progress));
				} else {
					const float t = std::clamp(ctx.progress, 0.0f, 1.0f);
					frameIndex = static_cast<int>(std::lround(t * static_cast<float>(frameCount - 1)));
				}
			} else {
				const float fps = std::clamp(asset.flipbook.fps, MIN_FPS, MAX_FPS);
				const float safeFps = (fps > 0.0f && std::isfinite(fps)) ? fps : DEFAULT_FPS;
				float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - _startTime).count();

				if (asset.flipbook.startTimeMode == StartTimeMode::PerSlot && ctx.slotIndex >= 0) {
					t += static_cast<float>(ctx.slotIndex) * 0.1f;
				} else if (asset.flipbook.startTimeMode == StartTimeMode::PerEntry) {
					t += static_cast<float>(ctx.formID % 1000) * 0.001f;
				}

				float framePos = t * safeFps;
				if (asset.flipbook.loop) {
					framePos = std::fmod(framePos, static_cast<float>(frameCount));
					if (framePos < 0.0f) {
						framePos += static_cast<float>(frameCount);
					}
				} else {
					framePos = std::clamp(framePos, 0.0f, static_cast<float>(frameCount - 1));
				}
				frameIndex = static_cast<int>(std::floor(framePos));
			}
		}

		frameIndex = std::clamp(frameIndex, 0, frameCount - 1);
		const int frameX = frameIndex % cols;
		const int frameY = frameIndex / cols;

		const float invCols = 1.0f / static_cast<float>(cols);
		const float invRows = 1.0f / static_cast<float>(rows);
		const ImVec2 uvMin(frameX * invCols, frameY * invRows);
		const ImVec2 uvMax((frameX + 1) * invCols, (frameY + 1) * invRows);

		const float cellW = static_cast<float>(tex->width) * invCols;
		const float cellH = static_cast<float>(tex->height) * invRows;
		const float cellAspect = (cellH > 0.0f) ? (cellW / cellH) : 1.0f;

		DrawStaticTextureRegion(tex, asset, ctx, drawList, uvMin, uvMax, cellAspect);
		return true;
	}

	const LayoutOverride& ReskinSystem::GetLayoutOverride(VisualTarget target) const
	{
		static const LayoutOverride kDefault{};
		const size_t idx = static_cast<size_t>(target);
		if (idx >= _layoutOverrides.size()) {
			return kDefault;
		}
		return _layoutOverrides[idx];
	}

	bool ReskinSystem::DrawFlipbookTexture(const FlipbookDef& def, const AssetDef& asset,
		const DrawContext& ctx, ImDrawList* drawList)
	{
		TextureHandle* frame = nullptr;
		if (def.progressDriven) {
			// Ensure frames are loaded/cached; frame choice below is driven by DrawContext::progress.
			(void)GetFlipbookFrame(def, 0.0f, ctx.slotIndex, ctx.formID);

			const std::string key = def.pattern + "|" + std::to_string(def.frameCount);
			auto it = _flipbookCache.find(key);
			if (it != _flipbookCache.end() && it->second.loaded && !it->second.frames.empty()) {
				const int frameCount = static_cast<int>(it->second.frames.size());
				int frameIndex = 0;
				if (ctx.progress > 1.0f) {
					// progress > 1 uses absolute frame index mode.
					frameIndex = static_cast<int>(std::lround(ctx.progress));
				} else {
					const float t = std::clamp(ctx.progress, 0.0f, 1.0f);
					frameIndex = static_cast<int>(std::lround(t * static_cast<float>(frameCount - 1)));
				}
				frameIndex = std::clamp(frameIndex, 0, frameCount - 1);
				frame = it->second.frames[static_cast<size_t>(frameIndex)];
			}
		} else {
			frame = GetFlipbookFrame(def,
				std::chrono::duration<float>(std::chrono::steady_clock::now() - _startTime).count(),
				ctx.slotIndex, ctx.formID);
		}
		
		if (frame && frame->IsValid()) {
			DrawStaticTexture(frame, asset, ctx, drawList);
			return true;  // Successfully drew flipbook frame
		}
		
		return false;  // No valid frame to draw - should use primitive fallback
	}

	TextureHandle* ReskinSystem::GetFlipbookFrame(const FlipbookDef& def, float time,
		int slotIndex, RE::FormID formID)
	{
		std::string key = def.pattern + "|" + std::to_string(def.frameCount);
		
		auto it = _flipbookCache.find(key);
		if (it == _flipbookCache.end()) {
			// Load flipbook
			FlipbookInstance instance;
			instance.def = def;
			instance.frames.reserve(def.frameCount);
			
			for (uint32_t i = 0; i < def.frameCount && _totalFrameCount < _maxTotalFrames; ++i) {
				std::string framePath;
				if (def.pattern.find("{:") != std::string::npos) {
					try {
						framePath = fmt::format(fmt::runtime(def.pattern), i);
					} catch (...) {
						framePath = def.pattern;
					}
				} else {
					size_t dotPos = def.pattern.rfind('.');
					if (dotPos != std::string::npos) {
						framePath = def.pattern.substr(0, dotPos) + 
							fmt::format("_{:02}", i) + def.pattern.substr(dotPos);
					}
				}
				
				std::string fullPath = _basePath + "\\" + framePath;
				TextureHandle* tex = GetTexture(fullPath, 0);
				instance.frames.push_back(tex);
				if (tex) _totalFrameCount++;
			}
			
			instance.loaded = !instance.frames.empty();
			_flipbookCache[key] = instance;
			it = _flipbookCache.find(key);
		}
		
		if (!it->second.loaded || it->second.frames.empty()) {
			return nullptr;
		}
		
		float fps = std::clamp(def.fps, MIN_FPS, MAX_FPS);
		float frameDuration = 1.0f / fps;
		float totalDuration = frameDuration * it->second.frames.size();
		
		float phaseOffset = 0.0f;
		switch (def.startTimeMode) {
			case StartTimeMode::PerSlot:
				phaseOffset = static_cast<float>(slotIndex) * 0.1f;
				break;
			case StartTimeMode::PerEntry:
				phaseOffset = static_cast<float>(formID % 1000) * 0.001f * totalDuration;
				break;
			default:
				break;
		}
		
		float adjustedTime = time + phaseOffset;
		uint32_t frameIndex = 0;
		
		if (def.loop) {
			float cycleTime = std::fmod(adjustedTime, totalDuration);
			frameIndex = static_cast<uint32_t>(cycleTime / frameDuration);
		} else {
			frameIndex = static_cast<uint32_t>((std::min)(adjustedTime / frameDuration,
				static_cast<float>(it->second.frames.size() - 1)));
		}
		
		frameIndex = (std::min)(frameIndex, static_cast<uint32_t>(it->second.frames.size() - 1));
		return it->second.frames[frameIndex];
	}

	void ReskinSystem::ClearTextureCache()
	{
		for (auto& [path, handle] : _textureCache) {
			if (handle.srv) {
				handle.srv->Release();
				handle.srv = nullptr;
			}
		}
		_textureCache.clear();
		_normalizedPathCache.clear();
		_flipbookCache.clear();
		_totalTextureBytes = 0;
		_totalFrameCount = 0;
		_perfDrawTargetMsAccum = 0.0;
		_perfDrawTargetCalls = 0;
		_perfGetTextureMsAccum = 0.0;
		_perfGetTextureCalls = 0;
		_perfTextureCacheHits = 0;
		_perfTextureCacheMisses = 0;
	}

	DebugInfo ReskinSystem::GetDebugInfo(const ResolvedEntry* currentEntry) const
	{
		DebugInfo info;
		info.reskinEnabled = _enabled;
		info.texturesLoaded = static_cast<uint32_t>(_textureCache.size());
		info.textureBytes = _totalTextureBytes;
		info.flipbooksLoaded = static_cast<uint32_t>(_flipbookCache.size());
		info.totalFrames = _totalFrameCount;
		
		if (currentEntry && currentEntry->preset) {
			info.activePresetId = currentEntry->preset->id;
			info.resolutionSource = currentEntry->source;
			info.resolutionKey = currentEntry->matchedKey;
			
			for (size_t i = 0; i < static_cast<size_t>(VisualTarget::COUNT); ++i) {
				const AssetDef& asset = currentEntry->preset->assets[i];
				if (asset.enabled && asset.type != AssetType::None) {
					if (asset.type == AssetType::StaticPNG) {
						info.targets[i].status = "asset: " + asset.staticPath;
					} else if (asset.type == AssetType::AtlasSheet) {
						info.targets[i].status = fmt::format(
							"atlas: {} [{}x{}]",
							asset.staticPath,
							static_cast<int>(asset.atlasCols),
							static_cast<int>(asset.atlasRows));
					} else {
						info.targets[i].status = "flipbook: " + asset.flipbook.pattern;
						info.targets[i].flipbookFps = asset.flipbook.fps;
					}
				} else {
					info.targets[i].status = "PRIMITIVE";
				}
			}
		}
		
		return info;
	}

	uint32_t ReskinSystem::GetTextureCount() const { return static_cast<uint32_t>(_textureCache.size()); }
	uint64_t ReskinSystem::GetTextureBytes() const { return _totalTextureBytes; }
	uint32_t ReskinSystem::GetFlipbookCount() const { return static_cast<uint32_t>(_flipbookCache.size()); }
	uint32_t ReskinSystem::GetTotalFrames() const { return _totalFrameCount; }

	// ========== COMPAT SHIM ==========
	namespace CompatShim
	{
		void CheckLegacyFlags()
		{
			if (Config::AmmoWheel::UseSkyrimTheme) {
				LogDeprecation("UseSkyrimTheme", "[Reskin] Enabled + [Primitives] colors");
			}
			if (Config::AmmoWheel::UseMainWheelTheme) {
				LogDeprecation("UseMainWheelTheme", "[Reskin] Enabled=false (uses primitives)");
			}
			if (Config::AmmoWheel::UseCustomStyles) {
				LogDeprecation("UseCustomStyles", "[Reskin] Enabled + AmmoWheel_Styles.ini presets");
			}
		}
		
		void MapSkyrimThemeToPrimitives(PrimitiveFallback& out)
		{
			using namespace Config::AmmoWheel::SkyrimTheme;
			out.slotUnhoveredInner = SlotUnhoveredInner;
			out.slotUnhoveredOuter = SlotUnhoveredOuter;
			out.slotHoveredInner = SlotHoveredInner;
			out.slotHoveredOuter = SlotHoveredOuter;
			out.indicatorSelectedInner = ActiveArcInner;
			out.indicatorSelectedOuter = ActiveArcOuter;
			out.textPrimary = TextPrimary;
			out.textShadow = TextShadow;
			out.borderInner = BorderGold;
			out.borderOuter = BorderBronze;
		}
		
		void MapMainWheelThemeToPrimitives(PrimitiveFallback& out)
		{
			using namespace Config::Styling::Wheel;
			out.slotUnhoveredInner = UnhoveredColorBegin;
			out.slotUnhoveredOuter = UnhoveredColorEnd;
			out.slotHoveredInner = HoveredColorBegin;
			out.slotHoveredOuter = HoveredColorEnd;
			out.indicatorSelectedInner = ActiveArcColorBegin;
			out.indicatorSelectedOuter = ActiveArcColorEnd;
			out.textPrimary = TextColor;
			out.textShadow = TextShadowColor;
		}
		
		void LogDeprecation(const char* legacyKey, const char* newKey)
		{
			logger::warn("[AmmoWheelReskinUnified] DEPRECATED: '{}' is deprecated. Use '{}' instead. "
				"This key will be removed in a future version.", legacyKey, newKey);
		}
	}

}  // namespace AmmoWheelReskinUnified
