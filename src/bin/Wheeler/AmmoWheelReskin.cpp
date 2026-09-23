#include "AmmoWheelReskin.h"
#include "bin/Config.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <fmt/format.h>

// Only include stb_image implementation once
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO  // We'll use our own file loading for safety
#include "include/lib/stb_image.h"

#include "SimpleIni.h"

namespace AmmoWheelReskin
{
	// ========== UTILITY FUNCTIONS ==========
	
	bool SafeFileExists(const std::string& path)
	{
		if (path.empty()) return false;
		
		// Reject UNC/network paths for safety
		if (!IsPathSafe(path)) return false;
		
		std::error_code ec;
		bool exists = std::filesystem::exists(path, ec);
		if (ec) {
			return false;
		}
		return exists;
	}

	bool SafeGetFileSize(const std::string& path, uint64_t& outSize)
	{
		if (!SafeFileExists(path)) return false;
		
		std::error_code ec;
		auto size = std::filesystem::file_size(path, ec);
		if (ec) {
			return false;
		}
		outSize = size;
		return true;
	}

	std::string GetAbsolutePath(const std::string& relativePath)
	{
		std::error_code ec;
		auto absPath = std::filesystem::absolute(relativePath, ec);
		if (ec) {
			return relativePath;
		}
		return absPath.string();
	}

	bool IsPathSafe(const std::string& path)
	{
		if (path.empty()) return false;
		
		// Reject UNC paths (\\server\share)
		if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') {
			return false;
		}
		
		// Reject paths with .. to prevent directory traversal
		if (path.find("..") != std::string::npos) {
			return false;
		}
		
		return true;
	}

	// ========== TEXTURE CACHE IMPLEMENTATION ==========

	TextureCache& TextureCache::GetSingleton()
	{
		static TextureCache instance;
		return instance;
	}

	void TextureCache::Init(ID3D11Device* device)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_device = device;
		logger::info("[AmmoWheelReskin] TextureCache initialized");
	}

	void TextureCache::Shutdown()
	{
		Clear();
		_device = nullptr;
	}

	void TextureCache::Clear()
	{
		std::lock_guard<std::mutex> lock(_mutex);
		for (auto& [path, handle] : _cache) {
			if (handle.srv) {
				handle.srv->Release();
				handle.srv = nullptr;
			}
		}
		_cache.clear();
		_totalBytes = 0;
		logger::info("[AmmoWheelReskin] TextureCache cleared");
	}

	std::string TextureCache::NormalizePath(const std::string& path)
	{
		std::error_code ec;
		auto canonical = std::filesystem::weakly_canonical(path, ec);
		if (ec) {
			return path;
		}
		std::string result = canonical.string();
		// Convert to lowercase for case-insensitive matching on Windows
		std::transform(result.begin(), result.end(), result.begin(), 
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return result;
	}

	TextureHandle* TextureCache::GetTexture(const std::string& absolutePath)
	{
		if (!_device) {
			logger::error("[AmmoWheelReskin] TextureCache::GetTexture called before Init");
			return nullptr;
		}

		std::string normalizedPath = NormalizePath(absolutePath);
		
		{
			std::lock_guard<std::mutex> lock(_mutex);
			auto it = _cache.find(normalizedPath);
			if (it != _cache.end()) {
				it->second.lastUsed = std::chrono::steady_clock::now();
				return it->second.IsValid() ? &it->second : nullptr;
			}
		}

		// Not in cache, try to load
		TextureHandle newHandle;
		if (!LoadPngTexture(absolutePath, newHandle)) {
			// Cache the failure to avoid repeated load attempts
			std::lock_guard<std::mutex> lock(_mutex);
			newHandle.valid = false;
			_cache[normalizedPath] = newHandle;
			return nullptr;
		}

		// Check if we need to evict before adding
		{
			std::lock_guard<std::mutex> lock(_mutex);
			while (_totalBytes + newHandle.bytesEstimate > _maxTotalBytes && !_cache.empty()) {
				EvictLRU();
			}
			
			newHandle.lastUsed = std::chrono::steady_clock::now();
			_cache[normalizedPath] = newHandle;
			_totalBytes += newHandle.bytesEstimate;
			
			logger::debug("[AmmoWheelReskin] Loaded texture: {} ({}x{}, {} bytes)", 
				absolutePath, newHandle.width, newHandle.height, newHandle.bytesEstimate);
			
			return &_cache[normalizedPath];
		}
	}

	bool TextureCache::LoadPngTexture(const std::string& path, TextureHandle& outHandle)
	{
		// Safety checks
		if (!SafeFileExists(path)) {
			logger::warn("[AmmoWheelReskin] File not found: {}", path);
			return false;
		}

		uint64_t fileSize = 0;
		if (!SafeGetFileSize(path, fileSize)) {
			logger::warn("[AmmoWheelReskin] Cannot get file size: {}", path);
			return false;
		}

		if (fileSize > _maxPngFileBytes) {
			logger::warn("[AmmoWheelReskin] File too large ({} bytes, max {}): {}", 
				fileSize, _maxPngFileBytes, path);
			return false;
		}

		// Read file into memory
		std::ifstream file(path, std::ios::binary);
		if (!file) {
			logger::warn("[AmmoWheelReskin] Cannot open file: {}", path);
			return false;
		}

		std::vector<unsigned char> fileData(static_cast<size_t>(fileSize));
		file.read(reinterpret_cast<char*>(fileData.data()), fileSize);
		if (!file) {
			logger::warn("[AmmoWheelReskin] Failed to read file: {}", path);
			return false;
		}
		file.close();

		// Decode PNG using stb_image
		int width = 0, height = 0, channels = 0;
		unsigned char* imageData = stbi_load_from_memory(
			fileData.data(), static_cast<int>(fileData.size()),
			&width, &height, &channels, 4  // Force RGBA
		);

		if (!imageData) {
			logger::warn("[AmmoWheelReskin] Failed to decode PNG: {} ({})", path, stbi_failure_reason());
			return false;
		}

		// Check texture size limits
		if (width > static_cast<int>(_maxTextureSize) || height > static_cast<int>(_maxTextureSize)) {
			logger::warn("[AmmoWheelReskin] Texture too large ({}x{}, max {}): {}", 
				width, height, _maxTextureSize, path);
			stbi_image_free(imageData);
			return false;
		}

		// Create D3D11 texture
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA subResource = {};
		subResource.pSysMem = imageData;
		subResource.SysMemPitch = width * 4;

		ID3D11Texture2D* texture = nullptr;
		HRESULT hr = _device->CreateTexture2D(&desc, &subResource, &texture);
		
		if (FAILED(hr)) {
			logger::error("[AmmoWheelReskin] CreateTexture2D failed: {} (HRESULT: 0x{:X})", path, hr);
			stbi_image_free(imageData);
			return false;
		}

		// Create shader resource view
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		hr = _device->CreateShaderResourceView(texture, &srvDesc, &outHandle.srv);
		texture->Release();
		stbi_image_free(imageData);

		if (FAILED(hr)) {
			logger::error("[AmmoWheelReskin] CreateShaderResourceView failed: {} (HRESULT: 0x{:X})", path, hr);
			return false;
		}

		outHandle.width = width;
		outHandle.height = height;
		outHandle.bytesEstimate = static_cast<uint64_t>(width) * height * 4;
		outHandle.valid = true;

		return true;
	}

	void TextureCache::EvictLRU()
	{
		// Must be called with mutex held
		if (_cache.empty()) return;

		auto oldest = _cache.begin();
		for (auto it = _cache.begin(); it != _cache.end(); ++it) {
			if (it->second.lastUsed < oldest->second.lastUsed) {
				oldest = it;
			}
		}

		if (oldest->second.srv) {
			oldest->second.srv->Release();
		}
		_totalBytes -= oldest->second.bytesEstimate;
		logger::debug("[AmmoWheelReskin] Evicted texture: {}", oldest->first);
		_cache.erase(oldest);
	}

	// ========== FLIPBOOK CACHE IMPLEMENTATION ==========

	FlipbookCache& FlipbookCache::GetSingleton()
	{
		static FlipbookCache instance;
		return instance;
	}

	void FlipbookCache::Init()
	{
		logger::info("[AmmoWheelReskin] FlipbookCache initialized");
	}

	void FlipbookCache::Shutdown()
	{
		Clear();
	}

	void FlipbookCache::Clear()
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_cache.clear();
		_totalFrameCount = 0;
		logger::info("[AmmoWheelReskin] FlipbookCache cleared");
	}

	std::string FlipbookCache::MakeKey(const FlipbookDef& def)
	{
		return fmt::format("{}|{}", def.pattern, def.frameCount);
	}

	std::string FlipbookCache::ResolveFramePath(const std::string& pattern, uint32_t frameIndex, const std::string& basePath)
	{
		// Support patterns like "popup/ring_{:02}.png" or "popup/ring_00.png"
		std::string result;
		
		// Check if pattern contains fmt-style placeholder
		if (pattern.find("{:") != std::string::npos || pattern.find("{}") != std::string::npos) {
			try {
				result = fmt::format(fmt::runtime(pattern), frameIndex);
			} catch (...) {
				// Fallback: try simple numeric suffix
				size_t dotPos = pattern.rfind('.');
				if (dotPos != std::string::npos) {
					result = pattern.substr(0, dotPos) + fmt::format("_{:02}", frameIndex) + pattern.substr(dotPos);
				} else {
					result = pattern + fmt::format("_{:02}", frameIndex);
				}
			}
		} else {
			// Pattern is a base name, append frame number
			size_t dotPos = pattern.rfind('.');
			if (dotPos != std::string::npos) {
				result = pattern.substr(0, dotPos) + fmt::format("_{:02}", frameIndex) + pattern.substr(dotPos);
			} else {
				result = pattern + fmt::format("_{:02}.png", frameIndex);
			}
		}

		// Combine with base path
		if (!basePath.empty()) {
			std::filesystem::path fullPath = std::filesystem::path(basePath) / result;
			return fullPath.string();
		}
		return result;
	}

	FlipbookInstance* FlipbookCache::GetFlipbook(const FlipbookDef& def, const std::string& basePath)
	{
		std::string key = MakeKey(def);
		
		{
			std::lock_guard<std::mutex> lock(_mutex);
			auto it = _cache.find(key);
			if (it != _cache.end()) {
				return it->second.loaded ? &it->second : nullptr;
			}
		}

		// Not in cache, try to load
		FlipbookInstance instance;
		instance.def = def;
		instance.frames.reserve(def.frameCount);

		uint32_t loadedFrames = 0;
		uint32_t missingFrames = 0;
		TextureHandle* firstValidFrame = nullptr;

		for (uint32_t i = 0; i < def.frameCount; ++i) {
			// Check global frame limit
			if (_totalFrameCount + loadedFrames >= _maxTotalFrames) {
				logger::warn("[AmmoWheelReskin] Max total frames reached ({}), stopping flipbook load", _maxTotalFrames);
				break;
			}

			std::string framePath = ResolveFramePath(def.pattern, i, basePath);
			TextureHandle* tex = TextureCache::GetSingleton().GetTexture(framePath);
			
			if (tex && tex->IsValid()) {
				instance.frames.push_back(tex);
				if (!firstValidFrame) {
					firstValidFrame = tex;
				}
				loadedFrames++;
			} else {
				// Missing frame - use first valid frame as fallback, or nullptr
				instance.frames.push_back(firstValidFrame);
				missingFrames++;
				if (missingFrames == 1) {
					logger::warn("[AmmoWheelReskin] Missing frame {} in flipbook: {}", i, def.pattern);
				}
			}
		}

		if (loadedFrames == 0) {
			logger::warn("[AmmoWheelReskin] Failed to load any frames for flipbook: {}", def.pattern);
			instance.loadFailed = true;
			
			std::lock_guard<std::mutex> lock(_mutex);
			_cache[key] = instance;
			return nullptr;
		}

		instance.loaded = true;
		
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_totalFrameCount += loadedFrames;
			_cache[key] = instance;
			
			logger::info("[AmmoWheelReskin] Loaded flipbook: {} ({}/{} frames)", 
				def.pattern, loadedFrames, def.frameCount);
			
			return &_cache[key];
		}
	}

	TextureHandle* FlipbookCache::GetCurrentFrame(FlipbookInstance* flipbook, float time, int slotIndex, RE::FormID formID)
	{
		if (!flipbook || flipbook->frames.empty()) {
			return nullptr;
		}

		const auto& def = flipbook->def;
		float fps = std::clamp(def.fps, MIN_FPS, MAX_FPS);
		float frameDuration = 1.0f / fps;
		float totalDuration = frameDuration * flipbook->frames.size();

		// Calculate phase offset based on StartTimeMode
		float phaseOffset = 0.0f;
		switch (def.startTimeMode) {
			case StartTimeMode::Global:
				phaseOffset = 0.0f;
				break;
			case StartTimeMode::PerSlot:
				phaseOffset = static_cast<float>(slotIndex) * 0.1f;  // 100ms offset per slot
				break;
			case StartTimeMode::PerEntry:
				// Use FormID to seed a pseudo-random phase
				phaseOffset = static_cast<float>(formID % 1000) * 0.001f * totalDuration;
				break;
		}

		float adjustedTime = time + phaseOffset;

		// Calculate frame index
		uint32_t frameIndex = 0;
		if (def.loop) {
			if (def.pingPong && flipbook->frames.size() > 1) {
				// Ping-pong: 0,1,2,3,2,1,0,1,2,3...
				float cycleLength = totalDuration * 2.0f - frameDuration * 2.0f;
				float cycleTime = std::fmod(adjustedTime, cycleLength);
				if (cycleTime < totalDuration) {
					frameIndex = static_cast<uint32_t>(cycleTime / frameDuration);
				} else {
					float reverseTime = cycleTime - totalDuration + frameDuration;
					frameIndex = static_cast<uint32_t>((totalDuration - reverseTime) / frameDuration);
				}
			} else {
				// Normal loop
				float cycleTime = std::fmod(adjustedTime, totalDuration);
				frameIndex = static_cast<uint32_t>(cycleTime / frameDuration);
			}
		} else {
			// No loop - clamp to last frame
			frameIndex = static_cast<uint32_t>((std::min)(adjustedTime / frameDuration, 
				static_cast<float>(flipbook->frames.size() - 1)));
		}

		frameIndex = (std::min)(frameIndex, static_cast<uint32_t>(flipbook->frames.size() - 1));
		return flipbook->frames[frameIndex];
	}

	// ========== PRESET MANAGER IMPLEMENTATION ==========

	PresetManager& PresetManager::GetSingleton()
	{
		static PresetManager instance;
		return instance;
	}

	void PresetManager::Init()
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_initialized) return;

		BuildDefaultPreset();
		LoadConfig();
		LoadStyles();
		LoadKIDMappings();

		_initialized = true;
		_loaded = true;
		logger::info("[AmmoWheelReskin] PresetManager initialized (enabled={})", _enabled);
	}

	void PresetManager::Shutdown()
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_presets.clear();
		_formIDToPreset.clear();
		_keywordToPreset.clear();
		_initialized = false;
		_loaded = false;
	}

	void PresetManager::Reload()
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_presets.clear();
		_formIDToPreset.clear();
		_keywordToPreset.clear();
		
		BuildDefaultPreset();
		LoadConfig();
		LoadStyles();
		LoadKIDMappings();
		
		logger::info("[AmmoWheelReskin] PresetManager reloaded");
	}

	void PresetManager::BuildDefaultPreset()
	{
		_defaultPreset.id = "Default";
		_defaultPreset.name = "Default Preset";
		
		// All assets disabled by default - use primitive rendering
		_defaultPreset.slotIcon.enabled = false;
		_defaultPreset.slotBackground.enabled = false;
		_defaultPreset.slotFrame.enabled = false;
		_defaultPreset.popup.enabled = false;
		_defaultPreset.wheelBackground.enabled = false;
		_defaultPreset.centerBackground.enabled = false;
		
		// Indicators use primitives by default
		for (size_t i = 0; i < static_cast<size_t>(IndicatorType::Count); ++i) {
			_defaultPreset.indicators[i].usePrimitive = true;
			_defaultPreset.indicators[i].assetRef.enabled = false;
		}
		
		// Default colors (Skyrim theme)
		_defaultPreset.slotColorUnhoveredInner = IM_COL32(160, 140, 110, 200);
		_defaultPreset.slotColorUnhoveredOuter = IM_COL32(120, 100, 70, 180);
		_defaultPreset.slotColorHoveredInner = IM_COL32(210, 180, 140, 230);
		_defaultPreset.slotColorHoveredOuter = IM_COL32(180, 150, 110, 210);
		_defaultPreset.textColor = IM_COL32(240, 230, 210, 255);
		_defaultPreset.textShadowColor = IM_COL32(40, 30, 20, 255);
		
		// Indicator colors
		_defaultPreset.indicators[static_cast<size_t>(IndicatorType::Selected)].colorInner = IM_COL32(218, 165, 32, 255);
		_defaultPreset.indicators[static_cast<size_t>(IndicatorType::Selected)].colorOuter = IM_COL32(139, 90, 43, 200);
		_defaultPreset.indicators[static_cast<size_t>(IndicatorType::Hovered)].colorInner = IM_COL32(255, 215, 0, 180);
		_defaultPreset.indicators[static_cast<size_t>(IndicatorType::Hovered)].colorOuter = IM_COL32(218, 165, 32, 120);
		_defaultPreset.indicators[static_cast<size_t>(IndicatorType::Active)].colorInner = IM_COL32(218, 165, 32, 255);
		_defaultPreset.indicators[static_cast<size_t>(IndicatorType::Active)].colorOuter = IM_COL32(139, 90, 43, 200);
		_defaultPreset.indicators[static_cast<size_t>(IndicatorType::Charge)].colorInner = IM_COL32(100, 200, 255, 200);
		_defaultPreset.indicators[static_cast<size_t>(IndicatorType::Charge)].colorOuter = IM_COL32(50, 100, 200, 150);
	}

	void PresetManager::LoadConfig()
	{
		// Read from AmmoWheel.ini [Reskin] section
		const char* iniPath = R"(.\Data\SKSE\Plugins\wheeler\AmmoWheel.ini)";
		const char* defaultsPath = R"(.\Data\SKSE\Plugins\wheeler\AmmoWheel.defaults.ini)";
		
		CSimpleIniA ini;
		ini.SetUnicode();
		auto mergeIni = [](const CSimpleIniA& overlay, CSimpleIniA& target) {
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
		};

		bool loadedAny = false;
		std::error_code ec;
		if (std::filesystem::exists(defaultsPath, ec) && !ec && ini.LoadFile(defaultsPath) == SI_OK) {
			loadedAny = true;
		}

		CSimpleIniA userIni;
		userIni.SetUnicode();
		ec.clear();
		if (std::filesystem::exists(iniPath, ec) && !ec && userIni.LoadFile(iniPath) == SI_OK) {
			mergeIni(userIni, ini);
			loadedAny = true;
		}

		if (!loadedAny) {
			logger::info("[AmmoWheelReskin] AmmoWheel config not found, using built-in defaults");
			_enabled = false;
			return;
		}

		_enabled = ini.GetBoolValue("Reskin", "Enabled", false);
		_basePath = ini.GetValue("Reskin", "BasePath", R"(.\Data\SKSE\Plugins\wheeler\resources\ammo_wheel)");
		
		// Safety limits
		uint32_t maxFrames = static_cast<uint32_t>(ini.GetLongValue("Safety", "MaxTotalFrames", DEFAULT_MAX_TOTAL_FRAMES));
		uint32_t maxBytesMB = static_cast<uint32_t>(ini.GetLongValue("Safety", "MaxTotalBytesMB", DEFAULT_MAX_TOTAL_BYTES_MB));
		uint32_t maxTexSize = static_cast<uint32_t>(ini.GetLongValue("Safety", "MaxTextureSize", DEFAULT_MAX_TEXTURE_SIZE));
		uint32_t maxPngMB = static_cast<uint32_t>(ini.GetLongValue("Safety", "MaxPngFileMB", DEFAULT_MAX_PNG_FILE_MB));
		
		FlipbookCache::GetSingleton().SetMaxTotalFrames(maxFrames);
		TextureCache::GetSingleton().SetMaxTotalBytes(static_cast<uint64_t>(maxBytesMB) * 1024 * 1024);
		TextureCache::GetSingleton().SetMaxTextureSize(maxTexSize);
		TextureCache::GetSingleton().SetMaxPngFileBytes(static_cast<uint64_t>(maxPngMB) * 1024 * 1024);
		
		// Default presets for arrow/bolt
		_arrowDefaultPreset = ini.GetValue("Reskin", "ArrowDefaultPreset", "Default");
		_boltDefaultPreset = ini.GetValue("Reskin", "BoltDefaultPreset", "Default");
		
		logger::info("[AmmoWheelReskin] Config loaded: enabled={}, basePath={}", _enabled, _basePath);
	}

	void PresetManager::LoadStyles()
	{
		if (!_enabled) return;
		
		std::string stylesPath = _basePath + "\\AmmoWheel_Styles.ini";
		
		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(stylesPath.c_str()) != SI_OK) {
			logger::info("[AmmoWheelReskin] AmmoWheel_Styles.ini not found at {}", stylesPath);
			return;
		}

		// Get all sections that start with "Preset_"
		CSimpleIniA::TNamesDepend sections;
		ini.GetAllSections(sections);
		
		for (const auto& section : sections) {
			std::string sectionName = section.pItem;
			if (sectionName.rfind("Preset_", 0) != 0) continue;
			
			std::string presetId = sectionName.substr(7);  // Remove "Preset_" prefix
			Preset preset = _defaultPreset;  // Start with defaults
			preset.id = presetId;
			preset.name = ini.GetValue(sectionName.c_str(), "Name", presetId.c_str());
			
			// Load asset references
			auto loadAssetRef = [&](AssetRef& ref, const char* prefix) {
				std::string enabledKey = std::string(prefix) + "_Enabled";
				std::string pathKey = std::string(prefix) + "_Path";
				std::string flipbookKey = std::string(prefix) + "_IsFlipbook";
				std::string frameCountKey = std::string(prefix) + "_FrameCount";
				std::string fpsKey = std::string(prefix) + "_FPS";
				std::string loopKey = std::string(prefix) + "_Loop";
				
				ref.enabled = ini.GetBoolValue(sectionName.c_str(), enabledKey.c_str(), false);
				if (!ref.enabled) return;
				
				ref.isFlipbook = ini.GetBoolValue(sectionName.c_str(), flipbookKey.c_str(), false);
				
				if (ref.isFlipbook) {
					ref.flipbookDef.pattern = ini.GetValue(sectionName.c_str(), pathKey.c_str(), "");
					ref.flipbookDef.frameCount = static_cast<uint32_t>(ini.GetLongValue(sectionName.c_str(), frameCountKey.c_str(), 1));
					ref.flipbookDef.fps = static_cast<float>(ini.GetDoubleValue(sectionName.c_str(), fpsKey.c_str(), DEFAULT_FPS));
					ref.flipbookDef.loop = ini.GetBoolValue(sectionName.c_str(), loopKey.c_str(), true);
				} else {
					ref.staticPath = ini.GetValue(sectionName.c_str(), pathKey.c_str(), "");
				}
				
				// Common render params
				std::string alphaKey = std::string(prefix) + "_Alpha";
				ref.alpha = static_cast<float>(ini.GetDoubleValue(sectionName.c_str(), alphaKey.c_str(), 1.0f));
			};
			
			loadAssetRef(preset.slotIcon, "SlotIcon");
			loadAssetRef(preset.slotBackground, "SlotBackground");
			loadAssetRef(preset.slotFrame, "SlotFrame");
			loadAssetRef(preset.popup, "Popup");
			loadAssetRef(preset.wheelBackground, "WheelBackground");
			loadAssetRef(preset.centerBackground, "CenterBackground");
			
			// Load indicator configs
			auto loadIndicator = [&](IndicatorConfig& config, const char* prefix) {
				std::string usePrimitiveKey = std::string(prefix) + "_UsePrimitive";
				config.usePrimitive = ini.GetBoolValue(sectionName.c_str(), usePrimitiveKey.c_str(), true);
				
				if (!config.usePrimitive) {
					loadAssetRef(config.assetRef, prefix);
				}
			};
			
			loadIndicator(preset.indicators[static_cast<size_t>(IndicatorType::Selected)], "IndicatorSelected");
			loadIndicator(preset.indicators[static_cast<size_t>(IndicatorType::Hovered)], "IndicatorHovered");
			loadIndicator(preset.indicators[static_cast<size_t>(IndicatorType::Active)], "IndicatorActive");
			loadIndicator(preset.indicators[static_cast<size_t>(IndicatorType::Charge)], "IndicatorCharge");
			
			_presets[presetId] = preset;
			logger::info("[AmmoWheelReskin] Loaded preset: {}", presetId);
		}
	}

	void PresetManager::LoadKIDMappings()
	{
		if (!_enabled) return;
		
		std::string kidPath = _basePath + "\\AMMO_KID.ini";
		
		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(kidPath.c_str()) != SI_OK) {
			logger::info("[AmmoWheelReskin] AMMO_KID.ini not found at {}", kidPath);
			return;
		}

		// [FormID] section: FormID = PresetId
		CSimpleIniA::TNamesDepend keys;
		ini.GetAllKeys("FormID", keys);
		for (const auto& key : keys) {
			std::string formIDStr = key.pItem;
			std::string presetId = ini.GetValue("FormID", formIDStr.c_str(), "");
			if (presetId.empty()) continue;
			
			// Parse FormID (hex)
			RE::FormID formID = 0;
			try {
				formID = static_cast<RE::FormID>(std::stoul(formIDStr, nullptr, 16));
			} catch (...) {
				logger::warn("[AmmoWheelReskin] Invalid FormID in AMMO_KID.ini: {}", formIDStr);
				continue;
			}
			
			_formIDToPreset[formID] = presetId;
		}

		// [Keyword] section: Keyword = PresetId
		keys.clear();
		ini.GetAllKeys("Keyword", keys);
		for (const auto& key : keys) {
			std::string keyword = key.pItem;
			std::string presetId = ini.GetValue("Keyword", keyword.c_str(), "");
			if (presetId.empty()) continue;
			
			_keywordToPreset[keyword] = presetId;
		}

		logger::info("[AmmoWheelReskin] Loaded {} FormID mappings, {} Keyword mappings", 
			_formIDToPreset.size(), _keywordToPreset.size());
	}

	const Preset* PresetManager::ResolvePresetForEntry(RE::TESAmmo* ammo)
	{
		if (!_enabled || !ammo) {
			return &_defaultPreset;
		}

		// Priority 1: FormID mapping
		RE::FormID formID = ammo->GetFormID();
		{
			auto it = _formIDToPreset.find(formID);
			if (it != _formIDToPreset.end()) {
				auto presetIt = _presets.find(it->second);
				if (presetIt != _presets.end()) {
					return &presetIt->second;
				}
			}
		}

		// Priority 2: Keyword mapping
		const auto keywordForm = ammo->As<RE::BGSKeywordForm>();
		if (keywordForm) {
			for (const auto& [keyword, presetId] : _keywordToPreset) {
				if (keywordForm->HasKeywordString(keyword)) {
					auto presetIt = _presets.find(presetId);
					if (presetIt != _presets.end()) {
						return &presetIt->second;
					}
				}
			}
		}

		// Priority 3: Type default (Arrow vs Bolt)
		const std::string& typeDefault = ammo->IsBolt() ? _boltDefaultPreset : _arrowDefaultPreset;
		{
			auto presetIt = _presets.find(typeDefault);
			if (presetIt != _presets.end()) {
				return &presetIt->second;
			}
		}

		// Fallback: default preset
		return &_defaultPreset;
	}

	// ========== RENDERER IMPLEMENTATION ==========

	Renderer& Renderer::GetSingleton()
	{
		static Renderer instance;
		return instance;
	}

	void Renderer::Init(ID3D11Device* device)
	{
		_startTime = std::chrono::steady_clock::now();
		_initialized = true;
		
		TextureCache::GetSingleton().Init(device);
		FlipbookCache::GetSingleton().Init();
		PresetManager::GetSingleton().Init();
		
		logger::info("[AmmoWheelReskin] Renderer initialized");
	}

	void Renderer::Shutdown()
	{
		PresetManager::GetSingleton().Shutdown();
		FlipbookCache::GetSingleton().Shutdown();
		TextureCache::GetSingleton().Shutdown();
		_initialized = false;
	}

	float Renderer::GetGlobalTime() const
	{
		auto now = std::chrono::steady_clock::now();
		return std::chrono::duration<float>(now - _startTime).count();
	}

	void Renderer::DrawAsset(const AssetRef& asset, const DrawContext& ctx, ImDrawList* drawList)
	{
		if (!asset.enabled || !drawList) return;

		if (asset.isFlipbook) {
			FlipbookInstance* flipbook = FlipbookCache::GetSingleton().GetFlipbook(
				asset.flipbookDef, PresetManager::GetSingleton().GetBasePath());
			if (flipbook) {
				DrawFlipbookTexture(flipbook, asset, ctx, drawList);
			}
		} else {
			std::string fullPath = PresetManager::GetSingleton().GetBasePath() + "\\" + asset.staticPath;
			TextureHandle* tex = TextureCache::GetSingleton().GetTexture(fullPath);
			if (tex && tex->IsValid()) {
				DrawStaticTexture(tex, asset, ctx, drawList);
			}
		}
	}

	void Renderer::DrawStaticTexture(TextureHandle* tex, const AssetRef& asset, const DrawContext& ctx, ImDrawList* drawList)
	{
		if (!tex || !tex->IsValid() || !drawList) return;

		// Calculate size based on FitMode
		float drawSize = asset.fixedSizePx;
		if (asset.fitMode == FitMode::FitInsideSlot) {
			// Fit inside slot with rotation safety margin
			drawSize = ctx.radius * 2.0f * asset.rotationSafetyScale - asset.paddingPx * 2.0f;
		}

		// Maintain aspect ratio
		float aspectRatio = static_cast<float>(tex->width) / static_cast<float>(tex->height);
		float drawWidth = drawSize;
		float drawHeight = drawSize;
		if (aspectRatio > 1.0f) {
			drawHeight = drawSize / aspectRatio;
		} else {
			drawWidth = drawSize * aspectRatio;
		}

		// Calculate rotation
		float rotation = 0.0f;
		switch (asset.rotationMode) {
			case RotationMode::FollowSlot:
				rotation = ctx.slotAngle + asset.rotationOffsetDeg * (3.14159265f / 180.0f);
				break;
			case RotationMode::Upright:
				rotation = asset.rotationOffsetDeg * (3.14159265f / 180.0f);
				break;
			case RotationMode::Fixed:
				rotation = asset.rotationOffsetDeg * (3.14159265f / 180.0f);
				break;
		}

		// Apply alpha
		ImU32 tint = asset.tintColor;
		uint8_t alpha = static_cast<uint8_t>((tint >> 24) * asset.alpha * ctx.alphaMult);
		tint = (tint & 0x00FFFFFF) | (alpha << 24);

		// Draw rotated quad
		float halfW = drawWidth * 0.5f;
		float halfH = drawHeight * 0.5f;
		float cosR = std::cos(rotation);
		float sinR = std::sin(rotation);

		ImVec2 corners[4] = {
			{ -halfW, -halfH },
			{  halfW, -halfH },
			{  halfW,  halfH },
			{ -halfW,  halfH }
		};

		ImVec2 uvs[4] = {
			{ 0.0f, 0.0f },
			{ 1.0f, 0.0f },
			{ 1.0f, 1.0f },
			{ 0.0f, 1.0f }
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
			tint
		);
	}

	void Renderer::DrawFlipbookTexture(FlipbookInstance* flipbook, const AssetRef& asset, const DrawContext& ctx, ImDrawList* drawList)
	{
		if (!flipbook || !drawList) return;

		TextureHandle* frame = FlipbookCache::GetSingleton().GetCurrentFrame(
			flipbook, GetGlobalTime(), ctx.slotIndex, ctx.formID);
		
		if (frame && frame->IsValid()) {
			DrawStaticTexture(frame, asset, ctx, drawList);
		}
	}

	void Renderer::DrawIndicator(IndicatorType type, const IndicatorConfig& config, const DrawContext& ctx, ImDrawList* drawList)
	{
		if (!drawList) return;

		if (config.usePrimitive || !config.assetRef.enabled) {
			DrawPrimitiveIndicator(type, config, ctx, drawList);
		} else {
			DrawAsset(config.assetRef, ctx, drawList);
		}
	}

	void Renderer::DrawPrimitiveIndicator(IndicatorType type, const IndicatorConfig& config, const DrawContext& ctx, ImDrawList* drawList)
	{
		// This is a placeholder - the actual primitive drawing is done by AmmoWheel.cpp
		// This function exists for when we want to draw indicators as assets but fall back to primitives
		// The AmmoWheel rendering code will check if reskin is enabled and call appropriate methods
		(void)type;
		(void)config;
		(void)ctx;
		(void)drawList;
	}

}  // namespace AmmoWheelReskin
