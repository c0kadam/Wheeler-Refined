#pragma once
#include <d3d11.h>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <array>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <chrono>
#include "imgui.h"
#include "SimpleIni.h"

namespace AmmoWheelReskinUnified
{
	// ============================================================
	// UNIFIED AMMOWHEEL RESKIN SYSTEM
	// ============================================================
	// This is the SINGLE source of truth for all AmmoWheel visuals.
	// All legacy systems (UseSkyrimTheme, UseMainWheelTheme, etc.)
	// are gated behind the ReskinEnabled flag.
	//
	// When ReskinEnabled=false: Pure primitive rendering (arcs, circles)
	// When ReskinEnabled=true:  PNG/flipbook assets with preset resolution
	// ============================================================

	// ========== CONSTANTS ==========
	constexpr uint32_t MAX_TOTAL_FRAMES = 256;
	constexpr uint32_t MAX_TOTAL_BYTES_MB = 64;
	constexpr uint32_t MAX_TEXTURE_SIZE = 2048;
	constexpr uint32_t MAX_PNG_FILE_MB = 10;
	constexpr float DEFAULT_FPS = 24.0f;
	constexpr float MIN_FPS = 1.0f;
	constexpr float MAX_FPS = 60.0f;
	constexpr float DEFAULT_ROTATION_SAFETY_SCALE = 0.85f;

	// ========== VISUAL TARGETS ==========
	// Every visual element that can be customized
	enum class VisualTarget : uint8_t
	{
		SlotIcon = 0,
		SlotBackground,
		SlotFrame,
		IndicatorSelected,
		IndicatorHovered,
		IndicatorActive,
		IndicatorCharge,
		Popup,              // Slot hover overlay (inside wheel, at slot center)
		PopupBubble,        // Popup bubble background (outside wheel, at popup center)
		WheelBackdrop,      // Back-most wheel backdrop (behind WheelBackground)
		WheelBackground,
		CenterBackground,
		WheelBorderRing,
		SlotDivider,
		NamePanelBackground,
		SlotLabelText,
		CursorIndicator,
		LowAmmoIndicator,
		PopupBubbleRim,
		PopupBubbleGlow,
		CenterPanelFrame,
		CenterDescriptionText,
		DamageDigits,
		MaxDamageDigits,
		AmmoCountDigits,
		COUNT
	};

	const char* GetTargetName(VisualTarget target);

	// ========== RESOLUTION SOURCE ==========
	// How the preset was resolved (for debug display)
	enum class ResolutionSource : uint8_t
	{
		None = 0,
		FormID,
		Keyword,
		TypeDefault,
		Fallback
	};

	const char* GetResolutionSourceName(ResolutionSource source);

	// ========== ASSET TYPES ==========
	enum class AssetType : uint8_t
	{
		None = 0,      // Use primitive fallback
		StaticPNG,     // Single PNG image
		Flipbook,      // Animated PNG sequence
		AtlasSheet     // Sprite-sheet atlas (single texture with frame grid)
	};

	enum class BlendMode : uint8_t
	{
		Alpha = 0,
		Additive
	};

	enum class RotationMode : uint8_t
	{
		FollowSlot = 0,  // Rotate to match slot angle
		Upright,         // Always upright
		Fixed            // Fixed rotation offset
	};

	enum class FitMode : uint8_t
	{
		FitInsideSlot = 0,
		FixedPixels
	};

	enum class StartTimeMode : uint8_t
	{
		Global = 0,      // All animations sync to global time
		PerSlot,         // Each slot index has its own phase
		PerEntry         // Each FormID seeds its own phase
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
		bool hasContentBounds = false;
		float contentCenterOffsetXNorm = 0.0f;  // [-0.5, 0.5], relative to full texture width
		float contentCenterOffsetYNorm = 0.0f;  // [-0.5, 0.5], relative to full texture height

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
		bool progressDriven = false;  // If true, frame index can be driven by DrawContext::progress.
		StartTimeMode startTimeMode = StartTimeMode::Global;
	};

	// ========== ASSET DEFINITION ==========
	// Defines how to render a single visual target
	struct AssetDef
	{
		bool enabled = false;
		bool usePrimitive = false;  // If true, force primitive fallback regardless of asset
		AssetType type = AssetType::None;
		
		// For StaticPNG
		std::string staticPath;
		
		// For Flipbook
		FlipbookDef flipbook;

		// Atlas metadata.
		// For AtlasSheet: frame grid and animation controls (via flipbook.*).
		// For digit targets: glyph atlas layout (non-animated).
		uint8_t atlasCols = 10;
		uint8_t atlasRows = 1;
		float digitSpacingPx = 0.0f;
		
		// Render parameters
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

	// ========== PRIMITIVE FALLBACK COLORS ==========
	// Used when asset is not enabled or fails to load
	struct PrimitiveFallback
	{
		// Slot colors
		ImU32 slotUnhoveredInner = IM_COL32(160, 140, 110, 200);
		ImU32 slotUnhoveredOuter = IM_COL32(120, 100, 70, 180);
		ImU32 slotHoveredInner = IM_COL32(210, 180, 140, 230);
		ImU32 slotHoveredOuter = IM_COL32(180, 150, 110, 210);
		
		// Indicator colors
		ImU32 indicatorSelectedInner = IM_COL32(218, 165, 32, 255);
		ImU32 indicatorSelectedOuter = IM_COL32(139, 90, 43, 200);
		ImU32 indicatorHoveredInner = IM_COL32(255, 215, 0, 180);
		ImU32 indicatorHoveredOuter = IM_COL32(218, 165, 32, 120);
		ImU32 indicatorActiveInner = IM_COL32(50, 205, 50, 255);
		ImU32 indicatorActiveOuter = IM_COL32(34, 139, 34, 200);
		ImU32 indicatorChargeInner = IM_COL32(100, 149, 237, 255);
		ImU32 indicatorChargeOuter = IM_COL32(65, 105, 225, 200);
		
		// Indicator geometry
		float indicatorSelectedThickness = 5.0f;
		float indicatorHoveredThickness = 3.0f;
		float indicatorActiveThickness = 6.0f;
		float indicatorChargeThickness = 4.0f;
		
		// Background
		ImU32 wheelBackground = IM_COL32(20, 15, 10, 200);
		ImU32 centerBackground = IM_COL32(30, 25, 20, 180);
		
		// Border
		ImU32 borderInner = IM_COL32(218, 165, 32, 200);
		ImU32 borderOuter = IM_COL32(139, 90, 43, 150);
		
		// Text
		ImU32 textPrimary = IM_COL32(240, 230, 210, 255);
		ImU32 textShadow = IM_COL32(40, 30, 20, 255);
		ImU32 textAccent = IM_COL32(218, 165, 32, 255);
	};

	// ========== PRESET ==========
	// Complete visual definition for an ammo type
	struct Preset
	{
		std::string id = "Default";
		std::string name = "Default Preset";
		
		// Assets for each visual target
		AssetDef assets[static_cast<size_t>(VisualTarget::COUNT)];
		
		// Primitive fallback colors (used when asset not enabled)
		PrimitiveFallback primitives;
	};

	// ========== RESOLVED ENTRY ==========
	// Result of resolving a preset for an ammo entry
	struct ResolvedEntry
	{
		const Preset* preset = nullptr;
		ResolutionSource source = ResolutionSource::None;
		std::string matchedKey;  // FormID hex, keyword name, or type
		
		// Per-target resolution info (for debug)
		struct TargetInfo
		{
			bool usingAsset = false;
			std::string assetPath;
			int flipbookFrame = -1;
			std::string fallbackReason;
		};
		TargetInfo targets[static_cast<size_t>(VisualTarget::COUNT)];
	};

	// ========== DRAW CONTEXT ==========
	// Parameters passed to drawing functions
	struct DrawContext
	{
		ImVec2 center;
		float radius;
		float slotAngleRad;        // For FollowSlot rotation
		float maxFitSizePx = 0.0f; // Optional cap for FitInsideSlot draw size (slot span limit)
		float sizeScale = 1.0f;    // Per-draw multiplicative scale (layout override aware)
		float alphaMult = 1.0f;
		int slotIndex = -1;
		RE::FormID formID = 0;
		float progress = 0.0f;     // For charge indicators (0..1)
		bool hovered = false;
		bool selected = false;
		bool active = false;
		bool allowSelectedTintPulse = true;
		bool autoCenterByAlphaBounds = false;
	};

	// Per-target layout tuning from AmmoWheel.ini:
	// [Reskin.Layout.<TargetName>] Scale / OffsetX / OffsetY / OffsetRadial / OffsetTangential /
	// AngleOffsetDeg / SelfRotationDeg / Opacity / DigitSpacingOffsetPx
	struct LayoutOverride
	{
		float scale = 1.0f;
		float offsetX = 0.0f;
		float offsetY = 0.0f;
		float offsetRadial = 0.0f;      // + outward, - inward (relative to slot angle)
		float offsetTangential = 0.0f;  // + clockwise, - counter-clockwise (relative to slot angle)
		float angleOffsetDeg = 0.0f;    // Additional rotation basis for FollowSlot assets
		float selfRotationDeg = 0.0f;   // Extra self-rotation applied to asset regardless of rotation mode
		float opacity = 1.0f;           // Extra alpha multiplier (0..1)
		float digitSpacingOffsetPx = 0.0f;  // Digits-only: expands/contracts spacing between glyphs
	};

	// ========== DEBUG INFO ==========
	// Runtime debug information for overlay
	struct DebugInfo
	{
		bool reskinEnabled = false;
		std::string activePresetId;
		ResolutionSource resolutionSource = ResolutionSource::None;
		std::string resolutionKey;
		
		struct TargetDebug
		{
			std::string status;  // "asset: path.png" or "PRIMITIVE (reason)"
			int flipbookFrame = -1;
			float flipbookFps = 0.0f;
		};
		TargetDebug targets[static_cast<size_t>(VisualTarget::COUNT)];
		
		uint32_t texturesLoaded = 0;
		uint64_t textureBytes = 0;
		uint32_t flipbooksLoaded = 0;
		uint32_t totalFrames = 0;
	};

	// ========== PERF STATS ==========
	// Lightweight runtime counters (consumed and reset by caller).
	struct PerfStats
	{
		double drawTargetMs = 0.0;
		uint32_t drawTargetCalls = 0;
		double getTextureMs = 0.0;
		uint32_t getTextureCalls = 0;
		uint32_t textureCacheHits = 0;
		uint32_t textureCacheMisses = 0;
	};

	// ========== RESKIN SYSTEM ==========
	// Main singleton managing the unified reskin system
	class ReskinSystem
	{
	public:
		static ReskinSystem& GetSingleton();
		
		// Lifecycle
		void Init(ID3D11Device* device);
		void Shutdown();
		void Reload();
		void ReloadSmartFromIni();
		
		// Query state
		bool IsEnabled() const { return _enabled; }
		bool IsInitialized() const { return _initialized; }
		const std::string& GetBasePath() const { return _basePath; }
		
		// Preset resolution
		// Returns resolved preset for an ammo entry, populating resolution info
		ResolvedEntry ResolveForAmmo(RE::TESAmmo* ammo);
		
		// Get default preset (emergency fallback)
		const Preset& GetDefaultPreset() const { return _defaultPreset; }
		
		// Get primitive fallback colors (used when reskin disabled)
		const PrimitiveFallback& GetPrimitiveFallback() const { return _primitiveFallback; }
		
		// Drawing
		// Draw a specific target for an entry. Returns true if asset was drawn.
		// If returns false, caller should use primitive fallback.
		bool DrawTarget(VisualTarget target, const ResolvedEntry& entry, 
			const DrawContext& ctx, ImDrawList* drawList);
		bool DrawDigitString(VisualTarget target, const ResolvedEntry& entry,
			const DrawContext& baseCtx, std::string_view digitsOnly, float digitHeightPx,
			ImDrawList* drawList, float* outTotalWidthPx = nullptr);
		bool HasAssetForTarget(VisualTarget target, const ResolvedEntry& entry) const;
		LayoutOverride GetLayoutOverrideSnapshot(VisualTarget target) const;
		
		// Get current debug info for overlay
		DebugInfo GetDebugInfo(const ResolvedEntry* currentEntry = nullptr) const;
		PerfStats ConsumePerfStats();
		
		// Texture cache stats
		uint32_t GetTextureCount() const;
		uint64_t GetTextureBytes() const;
		uint32_t GetFlipbookCount() const;
		uint32_t GetTotalFrames() const;

	private:
		ReskinSystem() = default;
		~ReskinSystem() = default;
		ReskinSystem(const ReskinSystem&) = delete;
		ReskinSystem& operator=(const ReskinSystem&) = delete;

		// Loading
		void LoadConfig();
		void LoadLayoutOverrides(const CSimpleIniA& ini);
		void LoadPresets();
		void LoadMappings();
		void BuildDefaultPreset();
		void PrewarmDefaultWeaponPresets();
		bool PrimePresetById(const std::string& presetId);
		void PrimePresetAssets(const Preset& preset);
		
		// Texture management
		TextureHandle* GetTexture(const std::string& path, int requestedMaxPx = 0);
		TextureHandle* GetFlipbookFrame(const FlipbookDef& def, float time, 
			int slotIndex, RE::FormID formID);
		void ClearTextureCache();
		
		// Drawing helpers
		void DrawStaticTexture(TextureHandle* tex, const AssetDef& asset, 
			const DrawContext& ctx, ImDrawList* drawList);
		void DrawStaticTextureRegion(TextureHandle* tex, const AssetDef& asset,
			const DrawContext& ctx, ImDrawList* drawList,
			ImVec2 uvMin, ImVec2 uvMax, float regionAspectRatio);
		bool DrawAtlasTexture(TextureHandle* tex, const AssetDef& asset,
			const DrawContext& ctx, ImDrawList* drawList);
		bool DrawFlipbookTexture(const FlipbookDef& def, const AssetDef& asset,
			const DrawContext& ctx, ImDrawList* drawList);
		const LayoutOverride& GetLayoutOverride(VisualTarget target) const;
		
		// State
		bool _enabled = false;
		bool _initialized = false;
		std::string _basePath;
		std::string _activePresetPath;  // Path to active preset folder (empty if no preset)
		ID3D11Device* _device = nullptr;
		std::chrono::steady_clock::time_point _startTime;
		
		// Presets
		Preset _defaultPreset;
		PrimitiveFallback _primitiveFallback;
		std::array<LayoutOverride, static_cast<size_t>(VisualTarget::COUNT)> _layoutOverrides{};
		std::unordered_map<std::string, Preset> _presets;
		
		// Mappings (from AMMO_KID.ini)
		std::unordered_map<RE::FormID, std::string> _formIDToPreset;
		std::unordered_map<std::string, std::string> _keywordToPreset;
		std::string _arrowDefaultPreset = "Default";
		std::string _boltDefaultPreset = "Default";
		
		// Texture cache
		std::unordered_map<std::string, TextureHandle> _textureCache;
		std::unordered_map<std::string, std::string> _normalizedPathCache;
		uint64_t _totalTextureBytes = 0;
		
		// Flipbook cache
		struct FlipbookInstance
		{
			std::vector<TextureHandle*> frames;
			FlipbookDef def;
			bool loaded = false;
		};
		std::unordered_map<std::string, FlipbookInstance> _flipbookCache;
		uint32_t _totalFrameCount = 0;
		
		// Thread safety
		mutable std::mutex _mutex;
		
		// Safety limits
		uint32_t _maxTotalFrames = MAX_TOTAL_FRAMES;
		uint64_t _maxTotalBytes = MAX_TOTAL_BYTES_MB * 1024 * 1024;
		uint32_t _maxTextureSize = MAX_TEXTURE_SIZE;
		uint64_t _maxPngFileBytes = MAX_PNG_FILE_MB * 1024 * 1024;

		// Runtime perf counters (render thread).
		double _perfDrawTargetMsAccum = 0.0;
		uint32_t _perfDrawTargetCalls = 0;
		double _perfGetTextureMsAccum = 0.0;
		uint32_t _perfGetTextureCalls = 0;
		uint32_t _perfTextureCacheHits = 0;
		uint32_t _perfTextureCacheMisses = 0;
	};

	// ========== COMPAT SHIM ==========
	// Maps legacy config keys to new system for backward compatibility
	// Logs deprecation warnings when legacy keys are used
	namespace CompatShim
	{
		// Check if legacy theme flags are set and log deprecation
		void CheckLegacyFlags();
		
		// Map legacy SkyrimTheme colors to primitive fallback
		void MapSkyrimThemeToPrimitives(PrimitiveFallback& out);
		
		// Map legacy UseMainWheelTheme to primitive fallback
		void MapMainWheelThemeToPrimitives(PrimitiveFallback& out);
		
		// Log deprecation warning for a legacy key
		void LogDeprecation(const char* legacyKey, const char* newKey);
	}

	// ========== UTILITY FUNCTIONS ==========
	bool SafeFileExists(const std::string& path);
	bool SafeGetFileSize(const std::string& path, uint64_t& outSize);
	std::string GetAbsolutePath(const std::string& relativePath);
	bool IsPathSafe(const std::string& path);
	std::string NormalizePath(const std::string& path);

}  // namespace AmmoWheelReskinUnified
