#pragma once
#include <memory>
#include <vector>
#include <shared_mutex>
#include <filesystem>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include "imgui.h"
#include "WheelItems/WheelItemAmmo.h"
#include "AmmoWheelReskinUnified.h"

/// <summary>
/// Cursor input filter for unified deadzone + smoothing across mouse and gamepad.
/// </summary>
struct CursorFilter {
	ImVec2 smoothedPos = {0, 0};
	float deadzone = 0.05f;
	float smoothingSpeed = 12.0f;
	
	void Reset() {
		smoothedPos = {0, 0};
	}
	
	/// Apply deadzone and optional exponential smoothing.
	/// @param input Raw input delta or position
	/// @param dt Frame delta time
	/// @param applySmoothing If true, use exponential decay smoothing
	/// @return Filtered input
	ImVec2 Apply(ImVec2 input, float dt, bool applySmoothing) {
		// 1. Deadzone - zero out small movements
		float magnitude = std::sqrt(input.x * input.x + input.y * input.y);
		if (magnitude < deadzone) {
			if (applySmoothing) {
				// Decay toward zero when in deadzone
				float blend = 1.0f - std::exp(-smoothingSpeed * dt);
				smoothedPos.x *= (1.0f - blend);
				smoothedPos.y *= (1.0f - blend);
				return smoothedPos;
			}
			return {0, 0};
		}
		
		// 2. Deadzone scaling - remap magnitude to start from 0 after deadzone
		float scaledMag = (magnitude - deadzone) / (1.0f - deadzone);
		scaledMag = std::clamp(scaledMag, 0.0f, 1.0f);
		
		ImVec2 normalized = {input.x / magnitude, input.y / magnitude};
		ImVec2 scaled = {normalized.x * scaledMag * magnitude, normalized.y * scaledMag * magnitude};
		
		// 3. Smoothing (exponential decay)
		if (applySmoothing && smoothingSpeed > 0.0f) {
			float blend = 1.0f - std::exp(-smoothingSpeed * dt);
			smoothedPos.x += (scaled.x - smoothedPos.x) * blend;
			smoothedPos.y += (scaled.y - smoothedPos.y) * blend;
			return smoothedPos;
		}
		
		return scaled;
	}
};

/// <summary>
/// Text layout result for multi-line word wrapping.
/// </summary>
struct TextLayout {
	std::vector<std::string> lines;
	float totalHeight = 0.0f;
	float maxWidth = 0.0f;
};

/// <summary>
/// Secondary radial wheel for ammo selection, activated when bow/crossbow is equipped.
/// </summary>
/// 
/// AmmoWheel Settings Integration Map:
/// ====================================
/// Setting                | INI Section  | INI Key           | Config.cpp Load      | Usage Site
/// -----------------------|--------------|-------------------|----------------------|---------------------------
/// Enabled                | General      | Enabled           | ReadAmmoWheelConfig  | Update(), TryOpen()
/// RequireWeaponEquipped  | General      | RequireWeaponEquipped | ReadAmmoWheelConfig | Legacy compat only; runtime still requires ranged weapon
/// ScreenAnchorIndex      | Position     | ScreenAnchor      | ReadAmmoWheelConfig  | calculateScreenPosition()
/// PositionX/Y            | Position     | PositionX/Y       | ReadAmmoWheelConfig  | calculateScreenPosition() (Anchor=5)
/// WheelRadius            | Position     | WheelRadius       | ReadAmmoWheelConfig  | OnConfigChanged(), draw()
/// WheelShapeIndex        | Appearance   | WheelShape        | ReadAmmoWheelConfig  | getArcAngleRad()
/// ArcStartAngle          | Appearance   | ArcStartAngle     | ReadAmmoWheelConfig  | getStartAngleRad()
/// UseMainWheelTheme      | Appearance   | UseMainWheelTheme | ReadAmmoWheelConfig  | drawSlot() colors
/// ShowAllAmmo            | Filtering    | ShowAllAmmo       | ReadAmmoWheelConfig  | RefreshAmmoList()
/// ShowModdedAmmo         | Filtering    | ShowModdedAmmo    | ReadAmmoWheelConfig  | RefreshAmmoList()
/// MinimumAmmoCount       | Filtering    | MinimumAmmoCount  | ReadAmmoWheelConfig  | RefreshAmmoList()
/// SortByCount            | Filtering    | SortByCount       | ReadAmmoWheelConfig  | RefreshAmmoList()
/// ShowAmmoCount          | Display      | ShowAmmoCount     | ReadAmmoWheelConfig  | drawSlot()
/// CountFontSize          | Display      | CountFontSize     | ReadAmmoWheelConfig  | drawAmmoCount()
/// EnableDebugOverlay     | Display      | EnableDebugOverlay| ReadAmmoWheelConfig  | draw()
/// ToggleKeyMKB           | Input        | ToggleKeyMKB      | ReadAmmoWheelConfig  | Controls::BindAllInputsFromConfig()
/// ToggleMouseButton      | Input        | ToggleMouseButton | ReadAmmoWheelConfig  | Controls::BindAllInputsFromConfig()
/// ToggleKeyGamepad       | Input        | ToggleKeyGamepad  | ReadAmmoWheelConfig  | Controls::BindAllInputsFromConfig()
///
class AmmoWheel
{
public:
	enum class WeaponType
	{
		None,
		Bow,
		Crossbow
	};

	enum class ScreenAnchor
	{
		TopLeft,
		TopRight,
		BottomLeft,
		BottomRight,
		Center,
		Custom
	};

	enum class WheelShape
	{
		FullCircle,
		HalfCircle,
		QuarterCircle
	};

	enum class WheelState
	{
		Closed,
		Opening,
		Opened,
		Closing
	};

	AmmoWheel();
	~AmmoWheel();

	/// <summary>
	/// Update the ammo wheel state and render if open.
	/// Called each frame from Wheeler::Update.
	/// </summary>
	void Update(float a_deltaTime);

	/// <summary>
	/// Check if a ranged weapon (bow/crossbow) is currently equipped.
	/// Updates internal weapon type state.
	/// </summary>
	void UpdateWeaponState();

	/// <summary>
	/// Populate the wheel with ammo matching the current weapon type.
	/// </summary>
	void RefreshAmmoList();

	/// <summary>
	/// Returns true if the ammo wheel should be available (ranged weapon equipped).
	/// </summary>
	bool ShouldBeAvailable() const { return _currentWeaponType != WeaponType::None; }

	/// <summary>
	/// Returns true if the ammo wheel is currently open or opening.
	/// </summary>
	bool IsOpen() const { return _state == WheelState::Opened || _state == WheelState::Opening; }

	/// <summary>
	/// Try to open the ammo wheel. Only succeeds if a ranged weapon is equipped.
	/// </summary>
	void TryOpen();

	/// <summary>
	/// Close the ammo wheel.
	/// </summary>
	void Close();

	/// <summary>
	/// Immediate close that bypasses RTU/activation side effects.
	/// </summary>
	void HardClose();

	/// <summary>
	/// Toggle the ammo wheel open/closed.
	/// </summary>
	void Toggle();

	/// <summary>
	/// Close the ammo wheel if it's been open long enough (for hold-to-open behavior).
	/// </summary>
	void CloseIfOpenedLongEnough();

	/// <summary>
	/// Update cursor position from mouse input.
	/// </summary>
	void UpdateCursorPosMouse(float a_deltaX, float a_deltaY);

	/// <summary>
	/// Update cursor position from gamepad input.
	/// </summary>
	void UpdateCursorPosGamepad(float a_x, float a_y);

	/// <summary>
	/// Activate (equip) the currently hovered ammo.
	/// </summary>
	void ActivateHoveredAmmo();

	/// <summary>
	/// Get the current weapon type.
	/// </summary>
	WeaponType GetWeaponType() const { return _currentWeaponType; }

	/// <summary>
	/// Get the number of ammo entries in the wheel.
	/// </summary>
	int GetNumEntries() const { return static_cast<int>(_ammoEntries.size()); }

	/// <summary>
	/// Called when config changes are detected. Recomputes all derived layout values.
	/// </summary>
	void OnConfigChanged();
	
	/// <summary>
	/// Check if AmmoWheel owns any global time-state mutation (SGTM or pause menu).
	/// </summary>
	bool HasModifiedTimescale() const { return _ammoWheelModifiedTimeScale || _ammoWheelOwnedPauseMenu; }
	
	/// <summary>
	/// Idempotent timescale restoration - safe to call multiple times.
	/// </summary>
	void RestoreTimescale();

	/// <summary>
	/// Check if the wheel can be opened (game state, menus, player control).
	/// Returns true if opening is allowed, false otherwise.
	/// </summary>
	bool CanOpen() const;

	/// <summary>
	/// Set enabled state at runtime. Handles state transitions safely.
	/// Idempotent: calling with current state is a no-op.
	/// </summary>
	void SetEnabled(bool a_enabled);

	/// <summary>
	/// Returns true if the ammo wheel is currently enabled.
	/// </summary>
	bool IsEnabled() const { return _enabled; }

	/// <summary>
	/// Returns true if the ammo wheel is blocking the main wheel from opening.
	/// Used by Wheeler::TryOpenWheeler to prevent conflicts.
	/// </summary>
	bool IsBlockingMainWheel() const { return _blockMainWheel; }

	/// <summary>
	/// Process input for chord-based open/close. Called each frame from Wheeler::Update.
	/// Returns true if input was consumed (main wheel should not process it).
	/// </summary>
	bool ProcessInput();
	
	/// <summary>
	/// Handle mouse button input for fixed-open mode.
	/// Returns true if input was consumed (game should not process attack).
	/// </summary>
	/// @param button 0=LMB, 1=RMB
	/// @param pressed true if button pressed, false if released
	bool HandleMouseButton(int button, bool pressed, bool fromGamepad = false);

private:
	void ResetMountedVelocityRestoreState();
	void TryRestoreMountedVelocityAfterTimeRestore(const char* a_reason);

	struct AmmoEntry
	{
		RE::TESAmmo* ammo = nullptr;
		int count = 0;
		bool isFavorite = false;  // Cached at collection time (NOT from MagicFavorites which is spells-only)
		std::shared_ptr<WheelItemAmmo> wheelItem;
		Texture::Image iconImage;  // Cached icon texture for this ammo
		const Config::AmmoWheel::StylePreset* resolvedPreset = nullptr;  // Resolved style preset (legacy)
		std::string resolvedIconPath;  // Resolved icon path (from preset or priority search)
		AmmoWheelReskinUnified::ResolvedEntry reskinEntry;  // Unified reskin system resolution

		// Cached per-entry damage used by center-panel highlight logic.
		float cachedDamage = 0.0f;
		bool damageValid = false;

		// Cached per-entry label layout to avoid per-frame wrap/measure loops.
		std::string labelDisplayNameCached;
		TextLayout labelLayoutCached;
		float labelFontSizeCached = 0.0f;
		float labelAvailWidthCached = 0.0f;
		float labelLineSpacingCached = 0.0f;
		float labelTotalTextHeightCached = 0.0f;
		float labelClipHalfWidthCached = 0.0f;
		float labelClipHalfHeightCached = 0.0f;
		float labelBgHalfWidthCached = 0.0f;
		bool labelUsesReskinPathCached = false;
		bool labelLayoutValid = false;
		uint32_t labelLayoutRevision = 0;
		ImVec2 labelLayoutViewport = { 0, 0 };
		float labelLayoutWheelCenterX = 0.0f;
	};

	struct CenterPanelCache
	{
		bool valid = false;
		int hoveredIndex = -1;
		RE::FormID ammoID = 0;
		uint32_t configRevision = 0;
		uint32_t damageRevision = 0;
		uint32_t rangedWeaponPoisonRevision = 0;
		ImVec2 viewport = { 0, 0 };
		ImVec2 panelCenter = { 0, 0 };
		ImVec2 wheelCenterAtBuild = { 0, 0 };
		float panelWidth = 0.0f;
		float panelHeight = 0.0f;
		float totalHeight = 0.0f;
		float maxTextWidth = 0.0f;
		float nameFontSize = 0.0f;
		float infoFontSize = 0.0f;
		float descriptionFontSize = 0.0f;
		float lineSpacing = 0.0f;
		float padding = 0.0f;
		bool highlightDamage = false;
		int roundedDamageValue = 0;
		std::vector<std::pair<std::string, float>> textLines;
		std::vector<bool> isDamageLine;
		std::vector<std::string> descriptionLines;
	};

	struct RangedWeaponPoisonPresentation
	{
		bool targetResolved = false;
		RE::FormID weaponFormID = 0;
		RE::FormID poisonFormID = 0;
		std::string poisonName;

		[[nodiscard]] bool HasActivePoison() const
		{
			return targetResolved && poisonFormID != 0 && !poisonName.empty();
		}

		bool operator==(const RangedWeaponPoisonPresentation&) const = default;
	};
	
	// Load/cache icon texture for an ammo entry (with reskin priority search)
	void loadAmmoIcon(AmmoEntry& entry);
	
	// ========== AMMO_KID.ini KEYWORD MAPPING SYSTEM ==========
	// Thread-safe keyword icon definition loading
	static void LoadKeywordIconDefinitions();
	static std::string GetIconForKeyword(const char* keyword);
	
	// ========== PRESET RESOLVER SYSTEM ==========
	// Load preset mappings from AMMO_KID.ini and presets from Styles.ini
	static void LoadPresetMappings();
	static void LoadStylePresets();
	
	// Resolve preset for an ammo entry (FormID -> Keyword -> Type -> Fallback)
	static const Config::AmmoWheel::StylePreset* ResolvePresetForAmmo(RE::TESAmmo* ammo);
	
	// Resolve icon path for an ammo entry (preset override -> priority search)
	static std::string ResolveIconPathForAmmo(RE::TESAmmo* ammo, const Config::AmmoWheel::StylePreset* preset);
	
	// KID mapping storage (thread-safe after initial load)
	static inline std::unordered_map<std::string, std::string> _kidIconMap;
	static inline std::atomic<bool> _kidMapsLoaded{false};
	static inline std::mutex _kidLoadMutex;
	
	// ========== AMMOWHEEL ICON CACHE ==========
	// Separate cache for AmmoWheel-specific icons (does not affect main Wheeler)
	static inline std::map<RE::FormID, Texture::Image> _ammoIconCacheFormID;
	static inline std::map<std::string, Texture::Image> _ammoIconCacheKeyword;
	static inline std::atomic<bool> _ammoIconCacheLoaded{false};
	static inline std::mutex _ammoIconCacheMutex;
	
	// Load AmmoWheel-specific custom icons from dedicated folder
	static void LoadAmmoWheelCustomIcons();
	
	// Smart text truncation using pixel measurement
	std::string TruncateTextToFit(const char* text, float maxWidth, float fontSize) const;

	// Runtime cache helpers
	void InvalidateRuntimeCaches();
	void EnsureCenterFieldOrderCache();
	bool RebuildDamageCache(const RE::TESObjectREFR::InventoryItemMap& a_imap);
	bool RebuildLabelLayouts(ImVec2 a_wheelCenter, float a_startAngle, float a_slotAngle);
	bool RebuildCenterPanelCache(ImVec2 a_wheelCenter, DrawArgs a_drawArgs);
	RangedWeaponPoisonPresentation ResolveRangedWeaponPoisonPresentation(
		RE::PlayerCharacter* a_player,
		const RE::TESObjectREFR::InventoryItemMap& a_inventory) const;
	void UpdateRangedWeaponPoisonPresentation(
		RE::PlayerCharacter* a_player,
		const RE::TESObjectREFR::InventoryItemMap& a_inventory,
		bool a_inventoryReadable,
		bool a_refreshDue);
	
	// Multi-line text wrapping for slot labels
	TextLayout wrapTextForSlot(const char* text, float maxWidth, float fontSize, int maxLines);
	
	// Edge-aware word wrapping for center panel text.
	TextLayout wrapTextForCenterPanel(const char* text, float fontSize, ImVec2 panelCenter, int maxLinesOverride = 0) const;
	
	// Calculate adaptive center panel position based on arc geometry
	ImVec2 calculateCenterPanelPosition(ImVec2 a_wheelCenter) const;
	
	// Clamp cursor angle to arc bounds for half-wheel mode.
	float clampAngleToArc(float angle) const;
	float getCursorMaxRadius() const;
	void syncGamepadFilterToCursor();
	
	// Reset navigation filters.
	void ResetNavigationFilters();

	// Lifecycle helpers
	void EnsureInitialized();   // Idempotent initialization
	void ResetInputLatch();     // Clear edge-detect state
	void ForceClose();          // Immediately close and clear blocking flags

	void draw(DrawArgs a_drawArgs);
	void drawSlot(int a_index, ImVec2 a_center, bool a_hovered, float a_innerRadius, float a_outerRadius, 
		float a_startAngle, float a_endAngle, RE::FormID a_equippedAmmoID, DrawArgs a_drawArgs);
	void drawHighlight(ImVec2 a_center, DrawArgs a_drawArgs, bool a_centerBgAlreadyDrawn = false);
	void drawAmmoCount(ImVec2 a_slotCenter, int a_count, DrawArgs a_drawArgs,
		const AmmoWheelReskinUnified::ResolvedEntry* a_reskinEntry = nullptr,
		float a_slotAngleRad = 0.0f);
	void drawLowAmmoWarning(ImVec2 a_center, float a_innerRadius, float a_outerRadius, float a_midAngle, DrawArgs a_drawArgs);
	void drawHoverPopup(ImVec2 a_wheelCenter, DrawArgs a_drawArgs);

	// Get currently equipped ammo FormID (0 if none)
	RE::FormID getEquippedAmmoFormID() const;
	bool IsAmmoCompatibleWithWeaponType(RE::TESAmmo* a_ammo, WeaponType a_weaponType) const;
	bool IsAmmoCompatibleWithWeaponType(RE::FormID a_ammoID, WeaponType a_weaponType) const;
	RE::FormID GetRememberedAmmoForWeaponType(WeaponType a_weaponType) const;
	void SetRememberedAmmoForWeaponType(WeaponType a_weaponType, RE::FormID a_ammoID);
	void SyncRememberedAmmoForWeaponType(WeaponType a_weaponType, RE::FormID a_ammoID, const char* a_reason);
	bool TryRestoreRememberedAmmoForWeaponType(WeaponType a_weaponType, RE::FormID a_currentAmmoID);
	
	// Find initial hover index (last selected ID > equipped ammo > last selected index > first entry)
	int FindInitialHoverIndex();
	
	// Play hover slot sound with spam prevention
	void PlayHoverSlotSound(int newHoveredIndex);

	ImVec2 calculateScreenPosition() const;
	ImVec2 calculateSlotCenter(int a_index, ImVec2 a_wheelCenter, float a_radius) const;
	int getHoveredIndex(ImVec2 a_wheelCenter, float a_cursorAngle) const;
	float getCursorAngle() const;
	float getArcAngleRad() const;
	float getStartAngleRad() const;
	float getSlotCenterAngle(int a_index) const;
	int stepHoveredIndex(int a_currentIndex, int a_delta) const;
	void processPendingMouseMotion(float a_dt);
	void syncCursorToHoveredSlot(float a_dt, bool a_immediate);

	WeaponType _currentWeaponType = WeaponType::None;
	WheelState _state = WheelState::Closed;

	enum class CursorInputSource
	{
		None,
		Mouse,
		Gamepad
	};

	// Runtime enable state (mirrors Config::AmmoWheel::Enabled but allows safe transitions)
	bool _enabled = true;
	bool _wasEnabled = true;
	bool _initialized = false;  // Set true after first EnsureInitialized()

	// Input chord state for edge detection
	bool _wasOpenChordDown = false;  // Previous frame's chord state
	bool _blockMainWheel = false;    // True while AmmoWheel is open, prevents main wheel

	std::vector<AmmoEntry> _ammoEntries;
	// Hover state
	int _hoveredIndex = -1;
	int _prevHoveredIndex = -1;  // For hover hysteresis (prevents jitter at slot boundaries)
	bool _hoverInputLock = false;  // Lock hover until user provides meaningful input (replaces time-based lock)
	float _hoveredTime = 0.f;
	int _lastSelectedIndex = -1;  // Legacy index tracking (keep for fallback)
	RE::FormID _lastSelectedAmmoID = 0; // Track by FormID for robustness against list changes
	RE::FormID _rememberedBowAmmoID = 0;
	RE::FormID _rememberedCrossbowAmmoID = 0;
	WeaponType _pendingRememberedAmmoRestoreType = WeaponType::None;
	
	// Hover sound state (spam prevention)
	int _lastHoverSoundIndex = -1;        // Last index that played a sound
	uint64_t _lastHoverSoundTimeMs = 0;   // Timestamp of last sound played
	
	// Activation debounce - prevents repeated equip spam
	bool _activationConsumed = false;  // True after activation, reset on close complete or new open

	// Time slow tracking (avoid overriding external effects like Slow Time shout)
	bool _ammoWheelModifiedTimeScale = false;
	float _preAmmoWheelTimeScale = 1.0f;
	bool _ammoWheelOwnedPauseMenu = false;
	bool _ammoWheelRestoreMountedVelocityOnClose = false;
	RE::FormID _ammoWheelMountedVelocityMountFormID = 0;
	RE::NiPoint3 _ammoWheelMountedVelocitySnapshot = { 0.0f, 0.0f, 0.0f };
	double _ammoWheelMountedMomentumAssistUntil = 0.0;
	
	// Hover popup state
	float _hoverPopupScale = 0.0f;  // Animated scale (0-1)
	static constexpr float POPUP_ANIM_SPEED = 8.0f;

	ImVec2 _cursorPos = { 0, 0 };
	
	// Cursor input filters for unified deadzone and smoothing
	CursorFilter _mouseFilter;
	CursorFilter _gamepadFilter;
	int _pendingCloseOnReleaseButton = -1;
	CursorInputSource _lastCursorInputSource = CursorInputSource::None;
	int _mousePendingHoverIndex = -1;
	ImVec2 _mouseAccumulatedDelta = { 0.0f, 0.0f };
	float _mouseAccumulatedPeak = 0.0f;
	float _mouseSlotCarry = 0.0f;
	int _mouseStepLatchDirection = 0;

	float _openTimer = 0.f;
	float _closeTimer = 0.f;

	// Config change tracking
	static inline float _configPollAccum = 0.f;
	static inline bool _configInitialized = false;
	static inline std::filesystem::file_time_type _configLastWriteTime{};
	static inline uint32_t _configRevision = 0;

	// Center-field order cache (parsed once per config revision).
	std::vector<std::string> _centerFieldOrderCache;
	uint32_t _centerFieldOrderRevision = 0;

	// Cached damage scan state for center panel.
	float _cachedMaxDamage = 0.0f;
	int _cachedMaxDamageTies = 0;
	bool _damageCacheValid = false;
	uint32_t _damageCacheRevision = 0;

	// Cached center panel layout.
	CenterPanelCache _centerPanelCache;
	RangedWeaponPoisonPresentation _rangedWeaponPoisonPresentation;
	uint32_t _rangedWeaponPoisonRevision = 0;
	// Sticky panel size used to avoid width/height jitter between ammo names.
	float _centerPanelStableWidth = 0.0f;
	float _centerPanelStableHeight = 0.0f;
	uint32_t _centerPanelStableConfigRevision = 0;
	ImVec2 _centerPanelStableViewport = { 0, 0 };

	// Perf instrumentation accumulators (flushed at 1Hz while visible).
	double _damageRebuildMsAccum = 0.0;
	int _damageRebuildCount = 0;
	double _labelRebuildMsAccum = 0.0;
	int _labelRebuildCount = 0;
	double _centerRebuildMsAccum = 0.0;
	int _centerRebuildCount = 0;

	// Cached layout values (recomputed on config change)
	float _cachedInnerRadius = 0.f;
	float _cachedOuterRadius = 0.f;
	float _cachedTextRadius = 0.f;
	float _cachedIconRadius = 0.f;
	float _cachedCountRadius = 0.f;  // Separate radius for ammo count positioning
	float _cachedIconSize = 48.f;  // Cached icon size from IconSizePx/IconSize.
	ImVec2 _cachedScreenPos = { 0, 0 };
	ImVec2 _lastViewportSize = { 0, 0 };

	// Diagnostic counters for ShowModdedAmmo (updated in RefreshAmmoList)
	int _lastTotalAmmoScanned = 0;
	int _lastBaseGameAmmo = 0;
	int _lastModdedAmmo = 0;
	int _lastFilteredByModded = 0;
	int _lastFilteredByWeapon = 0;
	int _lastFilteredByMinCount = 0;

	// Refresh flags for live config updates
	bool _needsListRefresh = false;

	std::shared_mutex _lock;
};
