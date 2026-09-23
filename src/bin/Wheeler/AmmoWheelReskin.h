#pragma once
#include <d3d11.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <chrono>
#include "imgui.h"

namespace AmmoWheelReskin
{
	// ========== CONSTANTS & LIMITS ==========
	constexpr uint32_t DEFAULT_MAX_TOTAL_FRAMES = 256;
	constexpr uint32_t DEFAULT_MAX_TOTAL_BYTES_MB = 64;
	constexpr uint32_t DEFAULT_MAX_TEXTURE_SIZE = 2048;
	constexpr uint32_t DEFAULT_MAX_PNG_FILE_MB = 10;
	constexpr float DEFAULT_FPS = 24.0f;
	constexpr float MIN_FPS = 1.0f;
	constexpr float MAX_FPS = 60.0f;
	constexpr float DEFAULT_ROTATION_SAFETY_SCALE = 0.85f;

	// ========== ENUMS ==========
	enum class BlendMode : uint8_t
	{
		Alpha = 0,
		Additive = 1
	};

	enum class RotationMode : uint8_t
	{
		FollowSlot = 0,  // Rotate to match slot angle
		Upright = 1,     // Always upright (no rotation)
		Fixed = 2        // Fixed rotation offset
	};

	enum class FitMode : uint8_t
	{
		FitInsideSlot = 0,  // Scale to fit inside slot bounds
		FixedPixels = 1     // Use fixed pixel size
	};

	enum class StartTimeMode : uint8_t
	{
		Global = 0,      // All animations sync to global time
		PerSlot = 1,     // Each slot index has its own phase
		PerEntry = 2     // Each FormID seeds its own phase
	};

	enum class IndicatorType : uint8_t
	{
		Selected = 0,
		Hovered = 1,
		Active = 2,
		Charge = 3,
		Count = 4
	};

	enum class DrawableTarget : uint8_t
	{
		SlotIcon = 0,
		SlotBackground = 1,
		SlotFrame = 2,
		IndicatorSelected = 3,
		IndicatorHovered = 4,
		IndicatorActive = 5,
		IndicatorCharge = 6,
		Popup = 7,
		WheelBackground = 8,
		CenterBackground = 9,
		Count = 10
	};

	// ========== TEXTURE HANDLE ==========
	struct TextureHandle
	{
		ID3D11ShaderResourceView* srv = nullptr;
		int32_t width = 0;
		int32_t height = 0;
		uint64_t bytesEstimate = 0;
		std::chrono::steady_clock::time_point lastUsed;
		bool valid = false;

		bool IsValid() const { return valid && srv != nullptr && width > 0 && height > 0; }
	};

	// ========== FLIPBOOK DEFINITION ==========
	struct FlipbookDef
	{
		std::string pattern;           // e.g., "popup/ring_{:02}.png"
		uint32_t frameCount = 1;
		float fps = DEFAULT_FPS;
		bool loop = true;
		bool pingPong = false;
		StartTimeMode startTimeMode = StartTimeMode::Global;
		BlendMode blendMode = BlendMode::Alpha;
		RotationMode rotationMode = RotationMode::Upright;
		float rotationOffsetDeg = 0.0f;
		FitMode fitMode = FitMode::FitInsideSlot;
		float fixedSizePx = 48.0f;
		float paddingPx = 0.0f;
		float rotationSafetyScale = DEFAULT_ROTATION_SAFETY_SCALE;
	};

	// ========== FLIPBOOK INSTANCE (cached frames) ==========
	struct FlipbookInstance
	{
		std::vector<TextureHandle*> frames;  // Pointers to cached textures
		FlipbookDef def;
		bool loaded = false;
		bool loadFailed = false;
	};

	// ========== ASSET REFERENCE ==========
	struct AssetRef
	{
		bool enabled = false;
		bool isFlipbook = false;
		std::string staticPath;        // For static PNG
		FlipbookDef flipbookDef;       // For flipbook
		
		// Render params
		BlendMode blendMode = BlendMode::Alpha;
		RotationMode rotationMode = RotationMode::Upright;
		float rotationOffsetDeg = 0.0f;
		FitMode fitMode = FitMode::FitInsideSlot;
		float fixedSizePx = 48.0f;
		float paddingPx = 0.0f;
		float rotationSafetyScale = DEFAULT_ROTATION_SAFETY_SCALE;
		ImU32 tintColor = IM_COL32(255, 255, 255, 255);
		float alpha = 1.0f;
	};

	// ========== INDICATOR CONFIG ==========
	struct IndicatorConfig
	{
		bool usePrimitive = true;      // If true, use existing arc/border rendering
		AssetRef assetRef;             // If usePrimitive=false, use this asset
		
		// Primitive fallback params (used when usePrimitive=true or asset fails)
		float thickness = 3.0f;
		ImU32 colorInner = IM_COL32(218, 165, 32, 255);
		ImU32 colorOuter = IM_COL32(139, 90, 43, 200);
	};

	// ========== PRESET ==========
	struct Preset
	{
		std::string id;
		std::string name;
		
		// Drawable assets
		AssetRef slotIcon;
		AssetRef slotBackground;
		AssetRef slotFrame;
		AssetRef popup;
		AssetRef wheelBackground;
		AssetRef centerBackground;
		
		// Indicators (can be primitive or asset)
		IndicatorConfig indicators[static_cast<size_t>(IndicatorType::Count)];
		
		// Non-asset visual params (fallback colors, etc.)
		ImU32 slotColorUnhoveredInner = IM_COL32(160, 140, 110, 200);
		ImU32 slotColorUnhoveredOuter = IM_COL32(120, 100, 70, 180);
		ImU32 slotColorHoveredInner = IM_COL32(210, 180, 140, 230);
		ImU32 slotColorHoveredOuter = IM_COL32(180, 150, 110, 210);
		ImU32 textColor = IM_COL32(240, 230, 210, 255);
		ImU32 textShadowColor = IM_COL32(40, 30, 20, 255);
	};

	// ========== DRAW CONTEXT ==========
	struct DrawContext
	{
		ImVec2 center;
		float radius;
		float slotAngle;           // For FollowSlot rotation
		float alphaMult = 1.0f;
		int slotIndex = -1;
		RE::FormID formID = 0;
		float progress = 0.0f;     // For charge indicators (0..1)
	};

	// ========== TEXTURE CACHE (AmmoWheel-only) ==========
	class TextureCache
	{
	public:
		static TextureCache& GetSingleton();
		
		void Init(ID3D11Device* device);
		void Shutdown();
		void Clear();
		
		// Load or get cached texture. Returns nullptr on failure.
		TextureHandle* GetTexture(const std::string& absolutePath);
		
		// Stats
		uint64_t GetTotalBytes() const { return _totalBytes; }
		uint32_t GetTextureCount() const { return static_cast<uint32_t>(_cache.size()); }
		
		// Limits
		void SetMaxTotalBytes(uint64_t bytes) { _maxTotalBytes = bytes; }
		void SetMaxTextureSize(uint32_t size) { _maxTextureSize = size; }
		void SetMaxPngFileBytes(uint64_t bytes) { _maxPngFileBytes = bytes; }

	private:
		TextureCache() = default;
		~TextureCache() = default;
		TextureCache(const TextureCache&) = delete;
		TextureCache& operator=(const TextureCache&) = delete;

		bool LoadPngTexture(const std::string& path, TextureHandle& outHandle);
		void EvictLRU();
		std::string NormalizePath(const std::string& path);

		ID3D11Device* _device = nullptr;
		std::unordered_map<std::string, TextureHandle> _cache;
		mutable std::mutex _mutex;
		
		uint64_t _totalBytes = 0;
		uint64_t _maxTotalBytes = DEFAULT_MAX_TOTAL_BYTES_MB * 1024 * 1024;
		uint32_t _maxTextureSize = DEFAULT_MAX_TEXTURE_SIZE;
		uint64_t _maxPngFileBytes = DEFAULT_MAX_PNG_FILE_MB * 1024 * 1024;
	};

	// ========== FLIPBOOK CACHE (AmmoWheel-only) ==========
	class FlipbookCache
	{
	public:
		static FlipbookCache& GetSingleton();
		
		void Init();
		void Shutdown();
		void Clear();
		
		// Load or get cached flipbook. Returns nullptr on failure.
		FlipbookInstance* GetFlipbook(const FlipbookDef& def, const std::string& basePath);
		
		// Get current frame texture for a flipbook
		TextureHandle* GetCurrentFrame(FlipbookInstance* flipbook, float time, int slotIndex, RE::FormID formID);
		
		// Stats
		uint32_t GetTotalFrameCount() const { return _totalFrameCount; }
		
		// Limits
		void SetMaxTotalFrames(uint32_t max) { _maxTotalFrames = max; }

	private:
		FlipbookCache() = default;
		~FlipbookCache() = default;
		FlipbookCache(const FlipbookCache&) = delete;
		FlipbookCache& operator=(const FlipbookCache&) = delete;

		std::string MakeKey(const FlipbookDef& def);
		std::string ResolveFramePath(const std::string& pattern, uint32_t frameIndex, const std::string& basePath);

		std::unordered_map<std::string, FlipbookInstance> _cache;
		mutable std::mutex _mutex;
		
		uint32_t _totalFrameCount = 0;
		uint32_t _maxTotalFrames = DEFAULT_MAX_TOTAL_FRAMES;
	};

	// ========== PRESET MANAGER ==========
	class PresetManager
	{
	public:
		static PresetManager& GetSingleton();
		
		void Init();
		void Shutdown();
		void Reload();
		
		// Get preset for an ammo entry
		const Preset* ResolvePresetForEntry(RE::TESAmmo* ammo);
		
		// Get default preset
		const Preset* GetDefaultPreset() const { return &_defaultPreset; }
		
		// Check if system is enabled
		bool IsEnabled() const { return _enabled; }
		
		// Base path for assets
		const std::string& GetBasePath() const { return _basePath; }

	private:
		PresetManager() = default;
		~PresetManager() = default;
		PresetManager(const PresetManager&) = delete;
		PresetManager& operator=(const PresetManager&) = delete;

		void LoadConfig();
		void LoadStyles();
		void LoadKIDMappings();
		void BuildDefaultPreset();

		bool _enabled = false;
		bool _initialized = false;
		std::string _basePath;
		
		Preset _defaultPreset;
		std::unordered_map<std::string, Preset> _presets;  // PresetId -> Preset
		std::unordered_map<RE::FormID, std::string> _formIDToPreset;
		std::unordered_map<std::string, std::string> _keywordToPreset;
		std::string _arrowDefaultPreset;
		std::string _boltDefaultPreset;
		
		mutable std::mutex _mutex;
		std::atomic<bool> _loaded{false};
	};

	// ========== RENDERER ==========
	class Renderer
	{
	public:
		static Renderer& GetSingleton();
		
		void Init(ID3D11Device* device);
		void Shutdown();
		
		// Draw an asset at the given context
		void DrawAsset(const AssetRef& asset, const DrawContext& ctx, ImDrawList* drawList);
		
		// Draw indicator (primitive or asset based on config)
		void DrawIndicator(IndicatorType type, const IndicatorConfig& config, const DrawContext& ctx, ImDrawList* drawList);
		
		// Utility: get current global time for animations
		float GetGlobalTime() const;

	private:
		Renderer() = default;
		~Renderer() = default;
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		void DrawStaticTexture(TextureHandle* tex, const AssetRef& asset, const DrawContext& ctx, ImDrawList* drawList);
		void DrawFlipbookTexture(FlipbookInstance* flipbook, const AssetRef& asset, const DrawContext& ctx, ImDrawList* drawList);
		void DrawPrimitiveIndicator(IndicatorType type, const IndicatorConfig& config, const DrawContext& ctx, ImDrawList* drawList);

		std::chrono::steady_clock::time_point _startTime;
		bool _initialized = false;
	};

	// ========== UTILITY FUNCTIONS ==========
	bool SafeFileExists(const std::string& path);
	bool SafeGetFileSize(const std::string& path, uint64_t& outSize);
	std::string GetAbsolutePath(const std::string& relativePath);
	bool IsPathSafe(const std::string& path);  // Reject UNC/network paths

}  // namespace AmmoWheelReskin
