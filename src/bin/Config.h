#pragma once
#include <array>
#include <string>
#include <unordered_map>

#include "imgui.h"

static ImU32 C_SKYRIMGREY = IM_COL32(255, 255, 255, 100 );
static ImU32 C_SKYRIMWHITE = IM_COL32(255, 255, 255, 255);
static ImU32 C_BLACK = IM_COL32(0, 0, 0, 255);
static ImU32 C_SKYRIMDARKGREY_MENUBACKGROUND = IM_COL32(0, 0, 0, 125);
static ImU32 C_QUARTERTRANSPARENT = IM_COL32(255, 255, 255, (int)(255.f * .25));
static ImU32 C_HALFTRANSPARENT = IM_COL32(255, 255, 255, (int)(255.f * .5f));
static ImU32 C_TRIQUARTERTRANSPARENT = IM_COL32(255, 255, 255, (int)(255.f * .75));
static ImU32 C_VOID = IM_COL32(255, 255, 255, 0);

namespace Config
{
	#define REFERENCE_WIDTH 1920
	#define REFERENCE_HEIGHT 1080

	// Runtime config layering:
	// - shipped defaults/factory files seed new keys for updates
	// - live *.ini files are treated as user overrides and may be omitted from releases

	void ReadStyleConfig();
	void ReadControlConfig();
	void ReadActionHotkeysBridgeConfig();
	void ReadOStimIntegrationConfig();
	void ReadAmmoWheelConfig();
	void WriteAmmoWheelPresetOverrideIfActive();
	bool WriteAmmoWheelKeybindOverrides();
	bool WriteActionHotkeysBridgeLayout();

	void OffsetSizingToViewport();
	void OffsetAmmoWheelSizingToViewport();
	void ResetScaleBaseCapture();  // Call before OffsetSizingToViewport on config reload

	namespace ResolutionFix
	{
		enum class Mode
		{
			Auto = 0,
			ForceDisplayToGame = 1,
			ForceNone = 2
		};

		inline bool Enabled = true;
		inline Mode ModeSetting = Mode::Auto;
		inline float Epsilon = 0.01f;
		inline bool LogOncePerOpen = true;
	}

	namespace I4
	{
		// Master toggle for InventoryInjector icon integration.
		// Default OFF for backward compatibility.
		inline bool Enabled = false;
		inline bool PreferI4Icons = true;
		// Alternative I4 resolver path.
		// - Allows I4 attempts for non-inventory wheel items (spell/shout/power).
		// If I4 resolve/render fails, Wheeler still falls back to its normal icon pipeline.
		inline bool UseAlternativePath = false;
		inline std::uint32_t CacheMaxEntries = 256;
		inline bool DebugLog = false;
		// Very verbose I4 pipeline trace logs (provider/resolver/renderer decisions).
		inline bool TraceLog = false;
		// Include cache-hit level traces (can be very noisy when TraceLog is enabled).
		inline bool TraceCacheHits = false;
		// 0 = MatchSlotIconSize, 1 = Fixed
		inline std::uint32_t RenderSizePolicy = 0;
		inline std::uint32_t FixedRenderSize = 128;
		// Optional on-screen extraction capture path for non-default SWF icon sources.
		inline bool ExtractionMode = false;
		// Per-category opt-in toggles for I4 pipeline. When false, Wheeler uses its normal fallback icon chain.
		inline bool UseForWeapons = true;
		inline bool UseForArmor = true;
		inline bool UseForAmmo = true;
		inline bool UseForPotions = true;
		inline bool UseForFood = true;
		inline bool UseForIngredients = true;
		inline bool UseForPoisons = true;
		inline bool UseForBooks = true;
		inline bool UseForScrolls = true;
		inline bool UseForLights = true;
		inline bool UseForMisc = true;
		inline bool UseForSpells = true;
		inline bool UseForShouts = true;
		inline bool UseForPowers = true;
		// Per-category extraction routing (only applies when ExtractionMode=true).
		// When false for a category, renderer uses built-in I4 fallback mapping for that category.
		inline bool ExtractForWeapons = true;
		inline bool ExtractForArmor = true;
		inline bool ExtractForAmmo = true;
		inline bool ExtractForPotions = true;
		inline bool ExtractForFood = true;
		inline bool ExtractForIngredients = true;
		inline bool ExtractForPoisons = true;
		inline bool ExtractForBooks = true;
		inline bool ExtractForScrolls = true;
		inline bool ExtractForLights = true;
		inline bool ExtractForMisc = true;
		inline bool ExtractForSpells = true;
		inline bool ExtractForShouts = true;
		inline bool ExtractForPowers = true;
	}

	inline constexpr std::size_t kActionHotkeysBridgeMaxWheels = 8;

	struct ActionHotkeysBridgeWheelSettings
	{
		std::uint32_t EntryCapacity = 10;
		std::uint32_t JumpKey = 0;
		std::uint32_t JumpKeyModifier = 0;
	};

	struct ActionHotkeysBridgeSlotPlacement
	{
		std::uint32_t wheelNumber = 0;
		std::uint32_t entryIndex = 0;

		bool operator==(const ActionHotkeysBridgeSlotPlacement&) const = default;
	};

	namespace MainWheel
	{
		namespace LayoutScaling
		{
			inline bool Enabled = false;
			inline float RefW = 0.0f;
			inline float RefH = 0.0f;
			inline bool ClampToScreen = true;
			inline float SafePadPx = 8.0f;
			inline bool AutoCreateUserFile = true;
			inline bool ConfigPresent = false;  // Section presence gate for backward compatibility
			inline std::string LoadedSourceTag = "off";
			inline std::string LoadedSourcePath;

			struct RuntimeState
			{
				bool LayoutActive = false;
				bool MismatchActive = false;
				float DisplayW = 0.0f;
				float DisplayH = 0.0f;
				float GameW = 0.0f;
				float GameH = 0.0f;
				float Lsx = 1.0f;
				float Lsy = 1.0f;
				float Lsu = 1.0f;
				float Msx = 1.0f;
				float Msy = 1.0f;
				float Msu = 1.0f;
				float CombinedX = 1.0f;
				float CombinedY = 1.0f;
				float CombinedU = 1.0f;
			};

			inline RuntimeState Runtime{};
			void UpdateRuntimeState();
		}

		namespace Debug
		{
			inline bool Enabled = false;
			inline bool OverlayEnabled = false;
			inline bool Verbose = false;
			inline std::uint32_t RateLimitMs = 250;
			inline bool LogOpenClose = true;
			inline bool LogConfig = true;
			inline bool LogInput = false;
			inline bool LogScaling = true;
			inline bool LogClamp = true;
			inline bool LogIndicators = true;
			inline bool LogReskinResolve = false;
			inline bool LogAssets = false;
			inline bool LogPerf = false;
		}

		namespace LowEnd
		{
			inline bool DisableBlurOnOpen = false;
			inline bool PreferPrimitiveBackgrounds = false;
		}

		namespace EditHints
		{
			// Master toggle for on-screen edit mode control hints.
			inline bool Enabled = true;
			// 0=Auto (last input), 1=MKB, 2=Gamepad, 3=Both
			inline std::uint32_t DisplayMode = 0;
			// 0=Xbox glyphs, 1=PlayStation glyphs
			inline std::uint32_t GamepadIconSet = 0;
			inline bool ShowBackground = true;
			inline bool ShowTitle = true;
			inline float AnchorX = 36.0f;
			inline float AnchorY = 140.0f;
			inline float LabelWidth = 210.0f;
			inline float FontSize = 18.0f;
			inline float HeaderFontSize = 20.0f;
			inline float IconSize = 26.0f;
			inline float RowSpacing = 7.0f;
			inline float KeyGap = 12.0f;
			inline float PanelPaddingX = 12.0f;
			inline float PanelPaddingY = 10.0f;
			inline ImU32 TextColor = C_SKYRIMWHITE;
			inline ImU32 HeaderColor = C_SKYRIMWHITE;
			inline ImU32 BackgroundColor = IM_COL32(0, 0, 0, 170);
		}

		inline bool ShowHandIndicator = false;

		struct HandIndicatorStyle
		{
			ImU32 Color = C_SKYRIMWHITE;
			float Opacity = 1.0f;
			float SizeScale = 1.0f;
			float Thickness = 0.0f;
			float OffsetX = 0.0f;
			float OffsetY = 0.0f;
			float SecondaryOffsetX = 0.0f;
			float SecondaryOffsetY = 0.0f;
			float DualTopOffsetX = 0.0f;
			float DualTopOffsetY = 0.0f;
			float SlotLeftOffsetX = 0.0f;
			float SlotLeftOffsetY = 0.0f;
			float SlotRightOffsetX = 0.0f;
			float SlotRightOffsetY = 0.0f;
			std::string AssetPath{};
			std::string SecondaryAssetPath{};
		};

		namespace HandIndicators
		{
			inline HandIndicatorStyle Left{};
			inline HandIndicatorStyle Right{};
			inline HandIndicatorStyle Dual{};
			inline float RightSideOffsetX = 0.0f;
			inline float RightSideOffsetY = 0.0f;
			inline float DualRightSideOffsetX = 0.0f;
			inline float DualRightSideOffsetY = 0.0f;
		}

		// Mouse hover stability settings (Main Wheel only)
		namespace Mouse
		{

			// Center Lock: do not change selection when cursor dist < threshold (0 = disabled)
			// Prevents opposite-side jumps caused by atan2 instability near center
			inline float CenterLockRadiusPx = 45.0f;
			
			// Jump Guard: block teleports (|delta| > 1) when cursor dist < threshold (0 = disabled)
			inline float JumpGuardRadiusPx = 140.0f;
			
			// Hysteresis: keep last slot unless candidate is angularly better by this many degrees
			inline float HysteresisDegrees = 8.0f;
			
			// Center Slowdown: scale mouse delta by distance-based gain for "reduced DPI" feel
			inline bool CenterSlowdownEnabled = true;
			inline float GainCenter = 0.25f;   // Gain when cursor is at center (0..1)
			inline float GainOuter = 1.0f;     // Gain when cursor is at outer radius
			inline float CurvePower = 2.0f;    // Interpolation curve (higher = more sudden transition)
			
			// New hover model zones (normalized radius 0..1)
			inline float InnerDeadZoneR = 0.18f;
			inline float InnerBlendZoneR = 0.35f;

			// New hover model smoothing + stability
			inline float MinStableSpeed = 0.05f;       // Min speed (normalized) to update stable dir in dead zone
			inline float VelocityHalfLifeMs = 40.0f;   // Velocity smoothing half-life (ms)
			inline float StableDirHalfLifeMs = 60.0f;  // Stable direction smoothing half-life (ms)

			// Intent detection (cross-wheel prediction)
			inline float IntentMinSpeed = 0.60f;      // Speed (normalized) for fast intent confirmation
			inline float IntentSustainSpeed = 0.20f;  // Speed (normalized) for sustained intent tracking
			inline float IntentConfirmMs = 80.0f;     // Confirmation window (ms)
			inline float IntentConeDeg = 25.0f;       // Directional cone for intent target
			inline float IntentReleaseConeDeg = 45.0f;// Release cone when direction changes
			inline float IntentMinAngleDeg = 90.0f;   // Min angular separation from current to consider intent
			inline float IntentReleaseSpeed = 0.08f;  // Release when speed drops below this
			inline float IntentReleaseDwellMs = 120.0f; // Release when dwelling on target at rim
			inline float IntentBias = 0.80f;          // Score bias for locked intent target
			inline float IntentNeighborBias = 0.35f;  // Score bias for neighbors of intent target

			// Scoring weights
			inline float ScoreWeightAngle = 1.0f;
			inline float ScoreWeightMotion = 0.5f;
			inline float ScoreWeightInertia = 0.35f;
			inline float ScoreWeightStickiness = 0.2f;

			// Commit rules
			inline float SwitchConfidenceMargin = 0.2f;  // Required score margin for switching
			inline float SwitchDwellMs = 60.0f;          // Dwell time before switching (ms)
			inline int InnerDeadzoneMaxSlotDelta = 1;    // Max slot delta allowed in inner dead zone

			// Debug logging for mouse hover guards (logs to SKSE log)
			inline bool DebugHoverLog = false;

			// New hover model debug toggles
			inline bool LogMouseFeatures = false;
			inline bool LogCandidateScores = false;
			inline bool LogIntentState = false;
			inline bool DrawDebugOverlay = false;
		}

		// Optional guard rails for legacy mouse hover (disabled by default).
		namespace MouseStabilization
		{
			inline bool Enabled = false;
			inline float CenterHoldRadius = 0.22f;   // Normalized to cursor radius max (CursorRadiusPerEntry * entries)
			inline float JumpGuardRadius = 0.33f;    // Normalized to cursor radius max (CursorRadiusPerEntry * entries)
			inline int MaxJumpSlots = 1;
			inline bool StepTowardEnabled = false;
			inline float BoundaryMarginDeg = 6.0f;
			inline bool BoundaryMarginLowSpeedOnly = true;
			inline float LowSpeedThreshold = 0.12f;  // Normalized speed (cursor radii/sec)

			namespace MotionHinting
			{
				inline bool Enabled = false;
				inline float SpeedThreshold = 0.35f;  // Normalized speed (radii/sec)
				inline float Blend = 0.15f;           // 0..1 blend toward velocity direction
			}
		}
	}

	// Wheel Behavior defaults:
	// - Wheeler no longer snapshots the current wheelBehavior.ini on panel save.
	// - The "Restore Defaults" button restores directly from the shipped factory file.
	bool StoreWheelBehaviorDefaultsIfMissing();
	bool RestoreWheelBehaviorDefaults();

	// ========== MASTER ENABLE FLAGS ==========
	// Master toggle for the main Wheeler wheel (not AmmoWheel)
	inline bool WheelerEnabled = true;
	
	// ========== VANILLA CONTROLLER PASSTHROUGH ==========
	// When true, LT (280) will NOT be bound to toggleWheel even if configured,
	// allowing vanilla Block to work. Users must use a different binding.
	// This prevents conflicts with Skyrim's default controller layout.
	inline bool VanillaLTPassthrough = false;
	
	// Show a one-time warning in logs when LT is bound to Wheeler (potential Block conflict)
	inline bool WarnOnLTBinding = true;

	enum WidgetAlignment
	{
		kLeft = 0,
		kCenter = 1
	};

	namespace InputBindings
	{
		namespace GamePad
		{                                       // right thumb
			inline uint32_t nextWheel = 281;          // right trigger
			inline uint32_t prevWheel = 0;    // unmapped
			inline uint32_t toggleWheel = 280;        // left trigger
			inline uint32_t toggleEditHints = 272;    // left stick click
			inline uint32_t toggleWheelModifier = 0;  // optional toggle modifier
			inline uint32_t nextItem = 269;           // DPAD right
			inline uint32_t prevItem = 268;      // DPAD left
			inline uint32_t activatePrimary = 275;  // right shoulder
			inline uint32_t activateSecondary = 274;  // left shoulder
			inline uint32_t addWheel = 0;             // unmapped
			inline uint32_t addEmptyEntry = 0;        // unmapped
			inline uint32_t moveEntryForward = 0;     // unmapped
			inline uint32_t moveEntryBack = 0;        // unmapped
			inline uint32_t moveWheelForward = 0;     // unmapped
			inline uint32_t moveWheelBack = 0;        // unmapped

			inline uint32_t toggleWheelIfInInventory = 0;  // unmapped
			inline uint32_t toggleWheelIfInInventoryModifier = 0;  // optional toggle modifier
			inline uint32_t toggleWheelIfNotInInventory = 0; // unmapped
			inline uint32_t toggleWheelIfNotInInventoryModifier = 0;  // optional toggle modifier
			inline uint32_t exitWheel = 277;  // B / Circle
		}
		namespace MKB
		{
			inline uint32_t nextWheel = 0x12;  // e
			inline uint32_t prevWheel = 0x10;  // q
			inline uint32_t toggleWheel = 58;  // capslock
			inline uint32_t toggleEditHints = 35;  // h
			inline uint32_t closeWheel = 15;  // tab
			inline uint32_t closeWheelAlt = 1;  // esc
			inline uint32_t toggleWheelModifier = 0;  // optional toggle modifier
			inline uint32_t prevItem = 264;    // mouse wheel up
			inline uint32_t nextItem = 265;    // mouse wheel down
			inline uint32_t activatePrimary = 256;  // left mouse button
			inline uint32_t activateSecondary = 257;  // right mouse button
			inline uint32_t addWheel = 49;            // N
			inline uint32_t addEmptyEntry = 50;       // M
			inline uint32_t moveEntryForward = 200;   // up arrow
			inline uint32_t moveEntryBack = 208;      // down arrow
			inline uint32_t moveWheelForward = 205;   // right arrow
			inline uint32_t moveWheelBack = 203;      // left arrow
		}

	}

	namespace ActionHotkeysBridge
	{
		inline bool Enabled = false;
		inline std::string SourceIniPath = R"(Data\SKSE\Plugins\ActionHotkeys.ini)";
		inline std::string SourceSlotsIniPath = R"(Data\SKSE\Plugins\ActionSlots.ini)";
		inline std::string SourceIconsPath = "";
		inline bool AutoInjection = true;
		inline std::uint32_t ManualWheelCount = 2;
		inline bool AutoRefresh = true;
		inline std::uint32_t RefreshDebounceMs = 500;
		inline std::uint32_t DispatchCooldownMs = 150;
		inline bool CloseAssistEnabled = true;
		inline bool CloseAssistUseEsc = true;
		inline bool CloseAssistUseGamepadB = true;
		inline std::uint32_t CloseAssistTimeoutMs = 2000;
		inline bool BlockConflictingWheelerHotkeys = true;
		inline bool MirrorSecondaryActivate = true;
		inline bool MirrorSpecialActivate = false;
		inline bool DebugLog = false;
	    inline std::array<ActionHotkeysBridgeWheelSettings, kActionHotkeysBridgeMaxWheels> Wheels = [] {
			std::array<ActionHotkeysBridgeWheelSettings, kActionHotkeysBridgeMaxWheels> wheels{};
			for (auto& wheel : wheels) {
				wheel.EntryCapacity = 10;
			}
			return wheels;
		}();
		inline std::uint32_t ResetLayout = 0;
		inline std::uint32_t ResetLayoutModifier = 0;
		inline std::uint32_t ReturnToPrevious = 0;
		inline std::uint32_t ReturnToPreviousModifier = 0;
		inline std::uint32_t RefreshMirror = 0;
		inline std::uint32_t RefreshMirrorModifier = 0;
		inline std::unordered_map<std::string, ActionHotkeysBridgeSlotPlacement> PersistedLayout;
	}

	namespace OStimIntegration
	{
		inline bool Enabled = false;
		inline bool AutoDetect = true;
		inline bool CreateManagedWheel = true;
		inline bool AutoSwitchToSceneWheel = false;
		inline bool RestorePreviousWheelOnSceneEnd = true;
		inline bool AllowPositionBrowsing = true;
		inline bool ShowOnlyValidPositions = true;
		inline bool ShowPositionNames = true;
		inline bool ShowPositionPreviews = true;
		inline bool RestrictRegularWheelActionsDuringScenes = false;
		inline bool HideInvalidActions = true;
		inline bool PreferMetadataPreviews = true;
		inline bool UseResourcePreviewFallback = true;
		inline bool PreferCurrentAnimationClass = true;
		inline bool DebugLog = false;
		inline std::uint32_t MaxPositionsPerPage = 6;
		inline float SVGSlotScale = 1.0f;
		inline float SVGSlotOffsetX = 0.0f;
		inline float SVGSlotOffsetY = 0.0f;
		inline float SVGCenterScale = 1.0f;
		inline float SVGCenterOffsetX = 0.0f;
		inline float SVGCenterOffsetY = 0.0f;
		inline float DDSSlotScale = 1.0f;
		inline float DDSSlotOffsetX = 0.0f;
		inline float DDSSlotOffsetY = 0.0f;
		inline float DDSCenterScale = 1.0f;
		inline float DDSCenterOffsetX = 0.0f;
		inline float DDSCenterOffsetY = 0.0f;
	}
	namespace Sound
	{
		static inline const char* SD_WHEELSWITCH = "UIFavorite";
		static inline const char* SD_ENTRYSWITCH = "UIMenuFocus";
		static inline const char* SD_WHEELERTOGGLE = "UIInventoryOpenSD";
		static inline const char* SD_ITEMSWITCH = "UIMenuPrevNextSD";

	}

	namespace Sounds
	{
		// Master toggle for all Wheeler UI sounds.
		inline bool EnableSounds = true;

		// Editor IDs for looking up sounds (configurable via INI).
		// These are passed directly to BSAudioManager::GetSoundHandleByName.
		inline std::string HoverSoundEditorID = "UIFavorite";
		inline std::string ActivateSoundEditorID = "UIMenuOK";

		// Volume multipliers (1.0 = normal game volume, higher = louder)
		inline float HoverSoundVolume = 1.0f;
		inline float ActivateSoundVolume = 1.0f;

		// Shout stage sound feedback
		inline bool EnableShoutStageSounds = true;
		inline std::uint32_t ShoutStageSoundMode = 1;  // 0=Off, 1=UI, 2=VOC

		// UI mode: same sound with rising volume per stage
		inline std::string ShoutUISoundEditorID = "UIMenuOK";
		inline float ShoutUIStageVolume1 = 0.35f;
		inline float ShoutUIStageVolume2 = 0.65f;
		inline float ShoutUIStageVolume3 = 1.00f;
		inline float ShoutUIStagePitch1 = 1.00f;
		inline float ShoutUIStagePitch2 = 1.03f;
		inline float ShoutUIStagePitch3 = 1.06f;

		// VOC mode: distinct sound per stage
		inline std::string ShoutWord1SoundEditorID = "";
		inline std::string ShoutWord2SoundEditorID = "";
		inline std::string ShoutWord3SoundEditorID = "";
	}

	// Enum for ShoutStageSoundMode
	enum class ShoutStageSoundMode : std::uint32_t
	{
		Off = 0,
		UI = 1,
		VOC = 2
	};

	namespace Control
	{
		namespace Wheel
		{
			// radius that bounds the mouse cursor. Increases with each entry in wheel to make sure MKB users don't rotate the cursor too fast.
			inline float CursorRadiusPerEntry = 10.f;
			inline bool DoubleActivateDisable = true;
			
			// if the user presses longer than this(without sending close), the wheel will close on release
			// the the user presses shorter than this, the wheel will close on a second press.
			inline float ToggleHoldThreshold = 0.25f;  

			inline bool BlockGameInputInEditMode = true;
			inline bool EnableOpenInFavoritesMenu = true;
			inline bool EnableEditModeInFavoritesMenu = true;
			inline bool HideGameUIInEditMode = true;
		}

	}

	namespace Animation
	{
		inline float EntryHighlightExpandTime = 0.2f;
		inline float EntryHighlightRetractTime = 0.2f;
		inline float EntryHighlightExpandScale = 0.15f;
		inline float EntryInputBumpTime = 0.1f;
		inline float EntryInputBumpScale = -0.1f;
		
		inline float ToggleVerticalFadeDistance = 0.f;
		inline float ToggleHorizontalFadeDistance = 0.f;
		inline float FadeTime = 0.08f;  // time it takes to fade in/out, set to 0 to disable.

		inline bool SnappyCursorIndicator = false;
		//inline bool CameraRotation = true;
	}

	namespace Styling
	{
		namespace HoverDelay
		{
			inline bool Enabled = true;
			inline float Radius = 45.0f;
			inline float RadiusOffset = 6.0f;
			inline float Thickness = 4.0f;
			inline ImU32 Color = IM_COL32(255, 220, 0, 200);
			inline ImU32 BackgroundColor = IM_COL32(255, 255, 255, 60);
			// Instant Shout SVG stage reskin placement/toggle controls (Wheeler Styles).
			inline bool InstantShoutAnimateReveal = true;
			inline float InstantShoutAssetScale = 1.0f;
			inline float InstantShoutAssetOffsetX = 0.0f;
			inline float InstantShoutAssetOffsetY = 0.0f;
			// Per-stage transform overrides (applied on top of base shout asset offset/scale).
			inline float InstantShoutStage1OffsetX = 0.0f;
			inline float InstantShoutStage1OffsetY = 0.0f;
			inline float InstantShoutStage1RotationDeg = 0.0f;
			inline float InstantShoutStage2OffsetX = 0.0f;
			inline float InstantShoutStage2OffsetY = 0.0f;
			inline float InstantShoutStage2RotationDeg = 0.0f;
			inline float InstantShoutStage3OffsetX = 0.0f;
			inline float InstantShoutStage3OffsetY = 0.0f;
			inline float InstantShoutStage3RotationDeg = 0.0f;
			// Instant Spell Indicator colors (separate from activation indicator)
			inline ImU32 InstantSpellColor = 3355443455u;
			inline ImU32 InstantSpellBackgroundColor = 1023410175u;
			// Hybrid reskin assets for Instant Spell indicator (background/overlay SVG).
			inline bool InstantSpellUseReskinAssets = false;
			inline float InstantSpellAssetScale = 1.9f;
			inline float InstantSpellAssetOffsetX = 2.0f;
			inline float InstantSpellAssetOffsetY = 0.0f;
			inline float InstantSpellAssetOpacity = 1.0f;
			inline bool InstantSpellUseAtlasAnimation = true;
			inline std::uint32_t InstantSpellAtlasCols = 8;
			inline std::uint32_t InstantSpellAtlasRows = 8;
			inline std::uint32_t InstantSpellAtlasFrameCount = 64;
			inline std::string InstantSpellBackgroundAssetPath{};
			inline std::string InstantSpellOverlayAssetPath{};
			inline std::string InstantSpellAtlasAssetPath{};
			inline float InstantSpellHandIndicatorScale = 3.0f;
			inline float InstantSpellHandIndicatorOffsetX = 0.0f;
			inline float InstantSpellHandIndicatorOffsetY = -18.0f;
			inline float InstantSpellHandIndicatorLeftOffsetX = -105.0f;
			inline float InstantSpellHandIndicatorLeftOffsetY = -10.0f;
			inline float InstantSpellHandIndicatorRightOffsetX = 105.0f;
			inline float InstantSpellHandIndicatorRightOffsetY = -10.0f;
			inline float InstantSpellHandIndicatorBothOffsetX = 0.5f;
			inline float InstantSpellHandIndicatorBothOffsetY = -10.0f;
			inline float InstantSpellHandIndicatorOpacity = 1.0f;
			inline std::string InstantSpellHandLeftAssetPath{};
			inline std::string InstantSpellHandRightAssetPath{};
			inline std::string InstantSpellHandBothAssetPath{};
		}

		namespace Wheel
		{
			inline bool UseGeometricPrimitiveForBackgroundTexture = false;

			inline float WheelBackgroundTextureScale = 1.f;

			inline float CursorIndicatorDist = 10.f; // distance from cusor indicator to the inner circle
			inline float CusorIndicatorArcWidth = 3.f; 
			inline float CursorIndicatorArcAngle = 2 * IM_PI * 1 / 12.f;  // 1/12 of a circle
			inline float CursorIndicatorTriangleSideLength = 5.f;
			inline ImU32 CursorIndicatorColor = C_SKYRIMWHITE;
			inline bool CursorIndicatorInwardFacing = true;

			inline float WheelIndicatorOffsetX = 260.f;
			inline float WheelIndicatorOffsetY = 340.f;
			inline float WheelIndicatorSize = 10.f;
			inline float WheelIndicatorSpacing = 25.f;
			inline ImU32 WheelIndicatorActiveColor = C_SKYRIMWHITE;
			inline ImU32 WheelIndicatorInactiveColor = C_SKYRIMGREY;

			inline WidgetAlignment WheelIndicatorAlignment = WidgetAlignment::kLeft;

			inline float InnerCircleRadius = 220.0f;
			inline float OuterCircleRadius = 360.0f;
			inline float InnerSpacing = 10.f;

			inline ImU32 HoveredColorBegin = C_QUARTERTRANSPARENT;
			inline ImU32 HoveredColorEnd = C_HALFTRANSPARENT;

			inline ImU32 UnhoveredColorBegin = C_SKYRIMDARKGREY_MENUBACKGROUND;
			inline ImU32 UnhoveredColorEnd = C_SKYRIMDARKGREY_MENUBACKGROUND;

			inline ImU32 ActiveArcColorBegin = C_SKYRIMWHITE;
			inline ImU32 ActiveArcColorEnd = C_SKYRIMWHITE;
			
			inline ImU32 InActiveArcColorBegin = C_SKYRIMGREY;
			inline ImU32 InActiveArcColorEnd = C_SKYRIMGREY;

			inline float ActiveArcWidth = 7.f;
			
			inline bool BlurOnOpen = true;
			inline float SlowTimeScale = 0.5f;

			
			// offset of wheel center, to which everything else is relative to
			inline float CenterOffsetX = 450.f;
			inline float CenterOffsetY = 0.f;

			inline ImU32 TextColor = C_SKYRIMWHITE;
			inline ImU32 TextShadowColor = C_BLACK;
		}

		namespace Entry
		{
			namespace Highlight
			{
				namespace Text
				{
					inline float OffsetX = 0;
					inline float OffsetY = -130;
					inline float Size = 27;
				}
			}

		}
		
		namespace Item
		{
			namespace Highlight
			{
				namespace Texture
				{
					inline float OffsetX = 0;
					inline float OffsetY = -50;
					inline float Scale = .2f;
				}

				namespace Text
				{
					inline float OffsetX = 0;
					inline float OffsetY = 20;
					inline float Size = 35;
					inline bool AutoFit = true;
					inline float MinSize = 24.0f;
					inline float MaxWidth = 500.0f;
				}

				namespace Desc
				{
					inline float OffsetX = 0;
					inline float OffsetY = 50;
					inline float Size = 30;
					inline float LineLength = 500.f;
					inline float LineSpacing = 5.f;
					inline bool AutoFit = true;
					inline float MinSize = 18.0f;
					inline float MaxHeight = 190.0f;
					inline std::uint32_t MaxLines = 8;
					inline float BottomSafeMargin = 150.0f;
					inline bool AutoShiftUp = true;
					inline float ShiftUpThreshold = 110.0f;
					inline float MaxShiftUp = 32.0f;
				}

				namespace StatIcon
				{
					inline float OffsetX = 0;
					inline float OffsetY = 0;
					inline float Scale = .2f;
				}

				namespace StatText
				{
					inline float OffsetX = 0;
					inline float OffsetY = 0;
					inline float Size = 35;
				}
			}
			namespace Slot
			{
				namespace Texture
				{
					inline float OffsetX = 0;
					inline float OffsetY = -25;
					inline float Scale = .1f;
				}

				namespace Text
				{
					inline float OffsetX = 0;
					inline float OffsetY = 10;
					inline float Size = 30;
					inline bool AutoFit = true;
					inline float MinSize = 18.0f;
					inline float MaxWidth = 0.0f;
					inline std::uint32_t AutoWidthMinChars = 10;
					inline float MaxHeight = 0.0f;
					inline std::uint32_t MaxLines = 2;
					inline float LineSpacing = 0.0f;
				}

				namespace BackgroundTexture
				{
					inline float Scale = .1f;
				}

			}
			
		}

		
	}

	namespace WheelBehavior
	{
		// Core toggle: when true, closing the wheel (releasing the toggle key) activates the hovered slot.
		inline bool ReleaseToUse = false;

		// If true, closes the wheel after a successful activation via click (primary/secondary/special).
		// Note: RTU activation happens on wheel close already; this primarily affects click-activations while the wheel is open.
		inline bool CloseWheelAfterUse = false;

		// Per-category Release-to-Use enables (only used when ReleaseToUse is true).
		inline bool RTUAlchemy = true;
		inline bool RTUSpell = true;
		inline bool RTUShout = true;

		// SmartAssign: RTU-only intelligent hand selection matrix
		inline bool RTUSmartAssignEnabled = false;
		// 1=Left, 2=Right, 3=Both
		inline std::uint32_t RTUSmartAssignTargetHands = 2;
		// Bitmask of categories (see RTUSmartAssignCategory enum below)
		inline std::uint32_t RTUSmartAssignCategoryMask = 0;
		// 1=LeftFirst, 2=RightFirst (vanilla)
		inline std::uint32_t RTUSmartAssignBothEmptyPriority = 2;
		// 0=NeverOverwrite, 1=OverwritePreferLeft, 2=OverwritePreferRight, 3=OverwritePreferVanilla
		inline std::uint32_t RTUSmartAssignOverwriteMode = 2;
		inline bool RTUSmartAssignDebugLog = false;

		enum class RTUSmartAssignCategory : std::uint32_t
		{
			None = 0,
			Spells = 1,
			Weapons1H = 2,
			Staffs = 4,
			Shields = 8,
			Torches = 16
		};

		enum class RTUSmartAssignOverwrite : std::uint32_t
		{
			NeverOverwrite = 0,
			OverwritePreferLeft = 1,
			OverwritePreferRight = 2,
			OverwritePreferVanilla = 3
		};

		// Require a minimum hover time before Release-to-Use triggers.
		inline float HoverActivateDelaySeconds = 0.5f;

		// After equipping via wheel activation, draw weapons/magic hands.
		inline bool AutoDrawOnUse = false;

		// If true, spells will be cast instantly on wheel activation (bypasses charge time; "cheaty").
		// Hold-to-cast inputs can still use this when RTU auto instant is disabled.
		inline bool InstantSpell = false;

		// If true, instant-cast spells use vanilla attack input pipeline after wheel close
		// (equip in hand + synthetic attack press/release) instead of CastSpellImmediate.
		// This preserves cast animations and vanilla timing behavior better.
		inline bool InstantSpellUseDirectCast = false;

		// If false, RTU close will not auto-trigger InstantSpell/DirectCast/InstantPowers.
		// Users can still trigger those manually via the primary/secondary confirm hold path.
		inline bool RTUAutoInstantSpell = true;

		// If true, powers (Power/LesserPower/VoicePower) can instant-cast while human.
		// If missing in INI, config loader falls back to InstantSpell for backward compatibility.
		inline bool InstantPowers = false;

		// Concentration spell handling mode for InstantSpell:
		// 0 = Block (skip instant cast for concentration spells, fall back to equip)
		// 1 = Timed (allow instant cast but auto-stop after configured seconds)
		inline std::uint32_t InstantSpellConcentrationMode = 0;

		// Maximum duration in seconds for concentration spells before auto-stop (Timed mode only).
		inline float InstantSpellConcentrationMaxSeconds = 3.0f;

		// Enable debug logging for instant cast decisions (spell name, mode, block/allow).
		inline bool InstantSpellDebugLog = false;

		// Hold/hover time in milliseconds required to reach "Ready" for instant cast.
		// Applies when instant spell casting is enabled (InstantSpell/InstantTransformations).
		inline float InstantSpellHoldThresholdMs = 3000.0f;

		// Hold-to-Cast safety threshold in milliseconds (default 600ms).
		// Time user must hold activation key before instant cast mode activates when RTU is OFF.
		inline float HoldToCastSafetyThresholdMs = 600.0f;

		// If true, transformation powers (Beast Form, Vampire Lord, Werebear, etc.) can be instant-cast.
		// Only applies when InstantSpell is enabled. Default: false (transformations use normal equip).
		// WARNING: Transformations trigger race changes and complex animations - instant casting may
		// cause stuck states or animation issues.
		inline bool InstantTransformations = false;
		
		// Debug logging for transformation detection decisions.
		inline bool InstantTransformationsDebugLog = false;
		
		// User-defined FormID lists for custom transformation detection.
		// Format: comma-separated hex FormIDs (e.g., "0x00092C48,0x0200283B")
		inline std::string TransformationAllowFormIDs;  // Forms to treat as transformations
		inline std::string TransformationDenyFormIDs;   // Forms to NOT treat as transformations

		// If true, shouts will be cast immediately on activation instead of just equipping.
		// Hold-to-cast inputs can still use this when RTU auto instant is disabled.
		inline bool InstantShout = false;

		// If false, RTU close will only equip shouts. Manual confirm hold can still instant-cast.
		inline bool RTUAutoInstantShout = true;

		// Shout word level selection thresholds (in seconds of hover time).
		// If hover time < ShoutWord2Threshold, use 1-word shout.
		// If hover time < ShoutWord3Threshold, use 2-word shout.
		// Otherwise, use 3-word shout (if unlocked).
		inline float ShoutWord2Threshold = 0.35f;
		inline float ShoutWord3Threshold = 0.75f;

		// Shout hold durations for input simulation (how long the shout key is "held").
		// These control word level selection in the vanilla shout pipeline.
		inline float ShoutHoldSecsWord1 = 0.1f;
		inline float ShoutHoldSecsWord2 = 0.5f;
		inline float ShoutHoldSecsWord3 = 1.0f;

		// Enable shout pipeline instrumentation (unlock-state tracing).
		inline bool ShoutPipelineDebug = false;

		// Staged shout hover timing model.
		// If true, shouts bypass the global RTU hover delay and use their own staged timing.
		inline bool ShoutIgnoreRTUDelay = true;
		// How fast each segment fills (seconds).
		inline float ShoutStageFillSecs = 0.08f;
		// How long to hold at each stage before advancing to the next (seconds).
		inline float ShoutStageHoldSecs1 = 0.40f;
		inline float ShoutStageHoldSecs2 = 0.40f;

		// Shout stage indicator colors (3-segment progress ring around shout slots).
		// Colors are with alpha pre-multiplied by runtime alphaMult during rendering.
		// Default: Stage1=Green (77,230,77), Stage2=Yellow (230,230,51), Stage3=Orange (230,128,26)
		inline ImU32 ShoutStageColor1 = IM_COL32(77, 230, 77, 200);   // Green
		inline ImU32 ShoutStageColor2 = IM_COL32(230, 230, 51, 200);  // Yellow
		inline ImU32 ShoutStageColor3 = IM_COL32(230, 128, 26, 200);  // Orange

		// Allow 3-phase shout indicator even when RTU is disabled (default OFF to preserve original behavior).
		inline bool ShowIndicatorWhenRTUOff = false;

		// If true, any alchemy item (food/potion/poison) that reaches 0 count will be removed from its wheel slot.
		// This prevents stale slots showing "0" after the last consumable is used.
		inline bool ClearDepletedConsumables = true;
		// Allow ingredients to be bound to Wheeler and used via the same game API path as consumables.
		inline bool AllowIngredientUse = true;
		// Allow non-scripted misc items to activate via EquipObject (unsafe by default).
		inline bool AllowUnsafeMiscActivation = false;
		
		// Dispatch policy for scripted (VMAD) misc items activated after wheel close.
		enum class ScriptedMiscDispatchMode : std::uint32_t
		{
			Auto = 0,
			EquipObjectOnly = 1,
			EquipEventOnly = 2,
			TempRefOnEquippedOnly = 3,
			LegacyTripleDispatch = 4
		};
		// Config key: [WheelBehavior] ScriptedMiscDispatchMode (default 0 = Auto)
		inline std::uint32_t ScriptedMiscDispatchModeValue = static_cast<std::uint32_t>(ScriptedMiscDispatchMode::Auto);

		// Dispatch policy for script-backed books that need inventory-style OnRead events.
		enum class BookReadCompatMode : std::uint32_t
		{
			AutoScripted = 0,
			AllowListOnly = 1,
			Disabled = 2
		};
		namespace BookReadCompat
		{
			// Config key: [BookReadCompat] Mode
			// 0/AutoScripted: any VMAD book uses temp-ref OnRead; allowlist always uses OnRead.
			// 1/AllowListOnly: only this section's allowlist entries use temp-ref OnRead.
			// 2/Disabled: all books use Wheeler's legacy deferred BookMenu path.
			inline std::uint32_t Mode = static_cast<std::uint32_t>(BookReadCompatMode::AutoScripted);
			inline std::string OnReadFormIDs{};
			inline std::string OnReadPlugins = "TheArcaneTome.esp";
			inline std::string OnReadNameTokens{};
			inline bool DebugLog = false;
		}

		// Optional vanilla-style hand memory for restoring left/right when leaving 2H.
		namespace HandMemory
		{
			inline bool Enabled = true;
			inline bool DebugLog = false;
			inline float RestoreDelaySeconds = 0.05f;
			inline float RestoreWindowSeconds = 1.25f;
			inline bool RestoreLeftIfEmpty = true;
			inline bool RestoreRightIfEmpty = true;
		}

		// Keep missing items in wheel slots (configurable per category).
		namespace KeepMissing
		{
			inline bool Enabled = false;
			inline bool KeepConsumables = true;
			inline bool KeepGears = true;
			inline bool KeepThrowableMods = true;
		}

		// RTU Anti-Slip: prevents accidental slot switching when canceling by pulling cursor to center.
		// Only applies when ReleaseToUse is enabled.
		inline bool RTUAntiSlipEnabled = true;
		// Strength slider (0..1) that maps to internal parameters.
		// Higher = larger deadzone, longer lockout, longer dwell.
		inline float RTUAntiSlipStrength = 0.5f;

		// Loot Menu Override: when true, Wheeler closes QuickLoot/ContainerMenu when opening.
		// When false, Wheeler is blocked by these menus (original behavior).
		inline bool LootMenuOverride = true;

		// Heavy-list compatibility mode for unresolved sentinel weapons (`uniqueID == 0`).
		// When enabled, same-hand unresolved weapon activations enforce a minimum settle/cooldown
		// window even if the game reports an equip state change immediately.
		// Default OFF so normal lists keep original behavior.
		inline bool HeavyListCompatibilityMode = false;
		// Minimum settle/cooldown window in milliseconds while HeavyListCompatibilityMode is enabled.
		// Applies only to unresolved sentinel weapon activations.
		inline float HeavyListCompatibilitySettleMs = 420.0f;
		// Startup-only opt-in for the global weapon/armor inventory mutation hooks.
		// Default OFF while the broader armor/BaseExtraList crash risk remains unresolved.
		inline bool MutableInventoryHooks = false;

		// Global scale multiplier for all main wheel sizing (1.0 = default)
		// Applied on top of resolution scaling. Allows users to scale the entire wheel.
		// Range: 0.5 to 2.0
		inline float GlobalScale = 1.0f;

		// Maximum items allowed per slot during deserialization (load).
		// Items beyond this limit are truncated on load. Lowering this may cause data loss.
		// Range: 10 to 64
		inline int MaxItemsPerSlot = 64;

		// Auto-populate and switch to dedicated form wheels when transforming.
		namespace TransformWheels
		{
			inline bool Enabled = true;
			inline std::uint32_t Mode = 1;  // 0=Off, 1=Switch to dedicated form wheel
			inline bool RestorePreviousWheel = true;
			inline bool IncludeShouts = false;
			inline std::uint32_t RegenerateThrottleMs = 250;
			inline std::uint32_t RetryWindowMs = 8000;
			inline std::uint32_t StableTicks = 2;
			inline std::uint32_t MinEntries = 1;
			inline bool CacheHumanSnapshot = true;
			inline bool PersistGeneratedWheels = false;
			inline bool UpdateOnlyOnChange = true;
			inline bool DebugLog = false;
			inline bool GenericEnabled = false;
			inline std::string GenericStateID = "GenericTransform";
			inline std::string GenericWheelID = "Wheel_GenericTransform";
			inline std::string GenericRaceEditorIDContains = "";
			inline std::string GenericRaceKeywords = "";
			inline std::string GenericRaceFormIDs = "";
			// Order evaluated when multiple transform detectors match.
			// Tokens: Werewolf, VampireLord, Lich, GenericOthers.
			inline std::string PrecedenceOrder = "Werewolf,VampireLord,Lich,GenericOthers";
			// Private-test opt-in. When enabled, transformed navigation may leave the
			// dedicated transform wheel, but runtime activation on base wheels remains
			// conservatively limited to consumables.
			inline bool WerewolfAllowBaseWheel = false;
			inline bool VampireLordAllowBaseWheel = false;
			// Private-test opt-in. Only affects werewolf state while a non-transform
			// wheel is active. Keeps transform-entry powers blocked and only relaxes
			// the human spell/shout runtime guard on base wheels.
			inline bool WerewolfAllowBaseWheelSpells = false;
			inline bool WerewolfAllowBaseWheelShouts = false;

			namespace WerewolfForm
			{
				inline bool Enabled = true;
				// Delta preserves the existing werewolf generated-wheel behavior.
				// ModList can be used to rely on configured tokens/race/equipped sources.
				inline std::string PopulateMode = "Delta";
				inline std::string RaceEditorIDContains = "Werewolf";
				inline std::string RaceKeywords = "ActorTypeWerewolf";
				inline std::string RaceFormIDs = "Skyrim.esm|0x000CDD84";
				inline std::string SpellTokens = "Werewolf,Howl,Growl,Lycan,Manbeast,Night Eye,Totem,Predator,Savage,Terror,Hunt,Brotherhood,Call of the Wild,Moonlight,Feed";
				inline std::string ExitSpellTokens = "Revert,HumanForm,Human Form,Return to Human,Mortal Form";
				// Optional plugin-qualified entries are accepted as Plugin.esp|0x123456.
				inline std::string AdditionalSpellFormIDs = "";
				inline bool DebugLog = false;
			}

			namespace VampireLordForm
			{
				inline bool Enabled = true;
				// Delta preserves the existing Vampire Lord generated-wheel behavior.
				// ModList can be used to rely on configured tokens/race/equipped sources.
				inline std::string PopulateMode = "Delta";
				inline bool HideTransformSpell = true;
				inline bool HideForcedRightHandSpells = true;
				inline bool BlockHiddenSpellActivation = true;
				// VL-only safety guard: in confirmed melee mode, block regular hand-spell equips.
				// Powers still go through the voice/power path.
				inline bool BlockRegularSpellsInMeleeMode = true;
				inline std::string RaceEditorIDContains = "DLC1VampireBeastRace,VampireLord";
				inline std::string RaceKeywords = "";
				inline std::string RaceFormIDs = "Dawnguard.esm|0x0000283A";
				inline std::string SpellTokens = "VampireLord,DLC1Vampire,Vampiric,Vampire Lord,Vampiric Grip,Conjure Gargoyle,Gargoyle,Conjure Death Hound,Death Hound,Mist Form,Bats,Hunter's Sight,Hunters Sight,Vampire's Sight,Vampires Sight,Vampire Sight,Blood Storm,Raze,Raise Dead,Raised Dead,Choke Hold,Chokehold";
				inline std::string ExitSpellTokens = "Revert,Revert Form,Change Form";
				// Optional plugin-qualified entries are accepted as Plugin.esp|0x123456.
				inline std::string AdditionalSpellFormIDs = "Skyrim.esm|0x000C4DE1";
				// VL-only hidden magic forms; accepts SpellItem and TESShout forms.
				inline std::string HiddenSpellFormIDs = "Dawnguard.esm|0x0000BFED,Dawnguard.esm|0x00013EC9";
				inline std::string HiddenSpellTokens = "Vampiric Drain,Unbind Slot";
				inline bool DebugLog = false;
			}

			namespace LichForm
			{
				inline bool Enabled = true;
				// Overlay: switch to lich wheel on entry but allow base wheel access (if AllowBaseWheel=true).
				// Replace: transform-only wheel navigation (classic behavior).
				// Disabled: bypass lich-specific handling.
				inline std::string Mode = "Overlay";
				// ModList: use curated lich tokens (+ race/equipped hints).
				// Delta: include snapshot delta in addition to ModList.
				// Manual: do not auto-populate (keep/create empty managed wheel).
				inline std::string PopulateMode = "ModList";
				inline bool AllowBaseWheel = true;
				// Off | BlockOtherTransforms | BlockAllTransformsExceptExit
				inline std::string TransformGuard = "BlockOtherTransforms";
				inline bool BlockBoundSpells = true;
				inline bool HideWeapons = false;
				inline bool HideGear = true;
				inline bool BlockStaffSwapping = true;
				inline bool SuppressDirectCast = true;
				inline bool BlockHiddenSpellActivation = true;
				inline std::string RaceEditorIDContains = "Lich,Necro,UCL";
				inline std::string RaceKeywords = "";
				inline std::string RaceFormIDs = "";
				// Conservative Lich kit tokens. Add broader lich-themed combat spells manually via SpellTokens or AdditionalSpellFormIDs if desired.
				inline std::string SpellTokens = "Necrotic Rejuvenation,Sacrifice Thrall,Bane of Life,Ice Coffin,Seed Of Pestilence,Poison Shroud,Mind Flay,Enslave Mind,Enslave Undead,Mass Reanimate,World Of Corpses,Summon Diilonthur,True Sight,Devour Soul,Dark Conduit,Revert,Revert Form,Return to Human,Human Form,Mortal Form,Return to Mortal";
				// Exit/revert tokens always allowed when transform guard is active.
				inline std::string ExitSpellTokens = "Revert,NecroRevert,Revert Form,Return to Human,Human Form,Mortal Form,Return to Mortal";
				// Optional comma-separated FormIDs for upgraded lich spells that do not expose stable tokens.
				inline std::string AdditionalSpellFormIDs = "";
				inline std::string HiddenSpellFormIDs = "";
				inline std::string HiddenSpellTokens = "";
				inline bool DebugLog = false;
			}
		}

		// Gamepad navigation behavior (main wheel only)
		namespace Gamepad
		{
			namespace Nav
			{
				inline float InnerDeadzone = 0.12f;
				inline bool HasInnerDeadzone = false;
				inline float OuterDeadzone = 0.98f;
				inline bool HasOuterDeadzone = false;
				inline float IntentMagnitude = 0.18f;
				inline bool HasIntentMagnitude = false;
				inline float SmoothingHalfLifeMs = 55.0f;
				inline bool HasSmoothingHalfLifeMs = false;
				inline float HysteresisDegrees = 8.0f;
				inline bool HasHysteresisDegrees = false;
				// If true, stick returning to deadzone hard-snaps cursor to center rest.
				// If false, cursor holds the last slot until user moves it.
				inline bool AutoCenterRestSnap = false;
				inline bool HasAutoCenterRestSnap = false;
			}

			namespace Open
			{
				inline bool UseLastSelectionOnOpen = true;
				inline bool HasUseLastSelectionOnOpen = false;
				inline float OpenGraceMs = 120.0f;
				inline bool HasOpenGraceMs = false;
			}

			namespace DPad
			{
				enum class Mode : std::uint8_t
				{
					Items = 0,
					Slots = 1
				};
				inline Mode ModeValue = Mode::Items;
				inline bool HasMode = false;
			}

			namespace DebugController
			{
				inline bool Enabled = false;
				inline bool HasEnabled = false;
				inline bool Overlay = false;
				inline bool HasOverlay = false;
			}
		}
	}

	// Debug logging toggles (safe patch, disabled by default).
	namespace Debug
	{
		inline bool LogActivateRejects = false;
		inline bool LogMenuBlockReasons = false;
		inline bool LogPopupAnim = false;
		inline std::uint32_t PopupAnimLogIntervalMs = 250;
		inline bool LogActionPolicy = false;  // ActionPolicy activation flow logging
		inline bool InputSpy = false;  // logs routed input decisions when enabled
		inline std::uint32_t InputSpyRateLimitMs = 50;  // live log spam guard
		inline std::uint32_t InputSpyRingBuffer = 256;  // in-memory event history size
		inline std::uint32_t InputSpyDumpHotkey = 0;  // mapped key ID, 0 disables hotkey dump
	}

	namespace InputBroker
	{
		inline bool Enabled = true;
		inline bool DebugLog = false;
		inline std::int32_t Priority_MainWheel = 50;
		inline std::int32_t Priority_AmmoWheel = 60;
	}

	namespace AmmoWheel
	{
		// Factory defaults reset: copies AmmoWheel.defaults.ini -> AmmoWheel.ini, then reloads.
		// Creates backup of current INI before overwriting.
		bool RestoreFactoryDefaults();
		
		// Master toggle for the ammo wheel feature
		inline bool Enabled = true;

		// Legacy compatibility toggle. AmmoWheel runtime is gameplay-only and still requires
		// a ranged weapon to be equipped before it can open.
		inline bool RequireWeaponEquipped = true;

		namespace LayoutScaling
		{
			inline bool Enabled = false;
			inline float RefW = 0.0f;
			inline float RefH = 0.0f;
			inline bool ClampToScreen = true;
			inline float SafePadPx = 8.0f;
			inline bool AutoCreateUserFile = true;
			inline bool ScaleGeometry = true;
			inline bool ScaleText = false;
			inline bool ScaleStylePx = true;
			inline bool ConfigPresent = false;
			inline std::string LoadedSourceTag = "off";
			inline std::string LoadedSourcePath;

			struct RuntimeState
			{
				bool LayoutActive = false;
				bool MismatchActive = false;
				float DisplayW = 0.0f;
				float DisplayH = 0.0f;
				float GameW = 0.0f;
				float GameH = 0.0f;
				float Lsx = 1.0f;
				float Lsy = 1.0f;
				float Lsu = 1.0f;
				float Msx = 1.0f;
				float Msy = 1.0f;
				float Msu = 1.0f;
				float CombinedX = 1.0f;
				float CombinedY = 1.0f;
				float CombinedU = 1.0f;
			};

			inline RuntimeState Runtime{};
			void UpdateRuntimeState();
		}

		// Positioning
		inline uint32_t ScreenAnchorIndex = 3;  // 0=TopLeft, 1=TopRight, 2=BottomLeft, 3=BottomRight, 4=Center, 5=Custom
		inline float PositionX = 85.0f;  // Custom position X (percentage of screen width)
		inline float PositionY = 75.0f;  // Custom position Y (percentage of screen height)
		inline float WheelRadius = 120.0f;

		// Appearance
		inline uint32_t WheelShapeIndex = 1;  // 0=FullCircle, 1=HalfCircle, 2=QuarterCircle
		inline float ArcStartAngle = 180.0f;  // Start angle in degrees (0 = right, 90 = down, 180 = left, 270 = up)
		inline bool UseMainWheelTheme = true;
		inline float CustomOpacity = 0.85f;
		
		// Slot Shape (individual slot appearance)
		// 0=Arc (default radial segments), 1=RoundedRect, 2=Pill (capsule), 3=Circle
		inline int SlotShape = 0;
		inline float SlotCornerRadius = 8.0f;  // Corner radius for RoundedRect shape
		inline float SlotShapeScale = 0.9f;    // Scale factor for non-arc shapes (0.5-1.2)
		
		// AmmoWheel-specific theme colors (used when UseMainWheelTheme is false)
		inline ImU32 UnhoveredColorBegin = IM_COL32(40, 60, 80, 200);   // Dark blue-gray
		inline ImU32 UnhoveredColorEnd = IM_COL32(20, 40, 60, 180);
		inline ImU32 HoveredColorBegin = IM_COL32(80, 140, 200, 220);   // Bright blue
		inline ImU32 HoveredColorEnd = IM_COL32(60, 100, 160, 200);
		inline ImU32 ActiveArcColorBegin = IM_COL32(100, 200, 255, 255); // Cyan highlight
		inline ImU32 ActiveArcColorEnd = IM_COL32(60, 160, 220, 255);

		// Behavior
		inline bool CloseOnSelection = true;
		inline bool UseRTUSystem = true;
		inline float RTUHoverDelay = 0.3f;
		
		// ========== PRESET SYSTEM ==========
		// Visual preset selection: 0=None (custom), 1=Skyrim Classic, 2=Modern Minimal, 3=Dark Medieval
		inline int ActivePreset = 0;
		// Tracks which preset was last applied to populate values (prevents re-locking UI on reload).
		inline int PresetApplied = 0;
		inline std::string PresetBasePath = R"(.\Data\SKSE\Plugins\wheeler\presets)";
		
		// ========== DEBUG LOGGING (Phase 0) ==========
		namespace Debug {
			inline bool LogConfigApply = false;   // Log when config values are applied
			inline bool LogInput = false;         // Log input events and navigation
			inline bool LogSorting = false;       // Log sorting decisions and comparisons
			inline bool LogLayout = false;        // Log layout calculations and positioning
			inline bool LogCentralPanel = false;  // Log central panel highlight decisions
			inline bool LogPerf = false;          // Log 1Hz AmmoWheel perf counters
			inline bool DebugShoutPipeline = false; // Log shouts pipeline
			
			// ========== RESKIN DEBUG (Task D) ==========
			inline bool ShowReskinOverlay = false;    // Show runtime reskin debug overlay
			inline bool LogPresetResolution = false;  // Log preset resolution for each ammo
			inline bool LogAssetLoading = false;      // Log PNG/flipbook asset loading
		}
		
		// ========== SORTING SYSTEM ==========
		// Sort keys: 0=None, 1=Count, 2=Power, 3=Type, 4=Favorites
		namespace Sort {
			inline int Primary = 1;               // Default: Count
			inline int Secondary = 0;             // Default: None
			inline int Tertiary = 0;              // Default: None
			inline bool DirectionPrimaryAsc = false;   // false=Desc (highest first), true=Asc
			inline bool DirectionSecondaryAsc = false;
			inline bool DirectionTertiaryAsc = false;
			inline bool Stable = true;            // Use stable sort for deterministic ordering
			inline bool FavoritesFirst = true;    // Push favorites to top regardless of other criteria
			inline bool GroupByType = false;      // Convenience: sets Primary=Type when enabled
			inline bool RememberAmmoByWeaponType = false;  // Restore separate arrow/bolt memory when switching bow/crossbow
			
			// Ammo limits: 0 = no limit (show all), >0 = show only top N items after sorting
			inline int ArrowLimit = 0;
			inline int BoltLimit = 0;
		}

		
		// ========== NAVIGATION ==========
		// Mouse input settings (TASK 1)
		inline float MouseDeadzone = 0.02f;         // Mouse deadzone (0.0..0.1) - smaller than gamepad
		inline float MouseSmoothingSpeed = 15.0f;   // Mouse smoothing speed (5..25)
		inline float MouseMaxAngularSpeed = 720.0f; // Max angular speed in degrees/sec (prevents runaway)
		
		// Gamepad input settings
		inline float GamepadDeadzone = 0.15f;       // Stick deadzone (0.05..0.5)
		inline float GamepadSmoothingSpeed = 10.0f; // Cursor smoothing speed (1..20)
		
		// Navigation reliability
		inline int NavigationApplyMode = 1;         // 0=Live (instant), 1=OnOpen (apply when wheel opens)
		inline bool ResetFiltersOnOpen = true;      // Reset smoothing filters when wheel opens
		inline bool DebugLogNavigation = false;     // Log navigation state changes
		
		// Debug logging for style/theme resolution
		inline bool DebugLogStyleResolution = false;  // Log theme flags and resolved visual values
		inline bool DebugLogThemeState = false;       // Log theme state on config reload
		inline bool DebugLogResourceState = false;    // Log resource loading state
		
		// Default values for reset functionality
		namespace NavigationDefaults {
			constexpr float MouseDeadzone = 0.02f;
			constexpr float MouseSmoothingSpeed = 15.0f;
			constexpr float MouseMaxAngularSpeed = 720.0f;
			constexpr float GamepadDeadzone = 0.15f;
			constexpr float GamepadSmoothingSpeed = 10.0f;
		}
		
		// ========== SELECTION BEHAVIOR ==========
		inline bool StartOnLastSelected = true;     // Open with last selected ammo highlighted
		inline bool RememberLastAcrossSessions = true; // Persist last selected ammo across game sessions
		inline bool SmoothSlotTransition = true;    // Smooth visual transition between slots
		
		// Half-wheel arc constraint
		inline int HalfWheelClampMode = 1;          // 0=Off, 1=ClampAngle, 2=SnapToEdgeSlot
		inline float ArcSelectionDeadbandDeg = 2.0f; // Prevents jitter at arc boundaries
		
		// Hover hysteresis: prevents jitter at slot boundaries
		// When previous slot is valid, require cursor to be this many degrees closer to new slot
		inline float GamepadHoverHysteresisDeg = 3.0f;

		// Filtering
		inline bool ShowAllAmmo = false;
		inline bool ShowModdedAmmo = true;
		inline int MinimumAmmoCount = 1;
		inline bool SortByCount = true;

		// Display
		inline bool ShowAmmoCount = true;
		inline float CountFontSize = 16.0f;
		inline ImU32 CountColor = IM_COL32(255, 255, 255, 255);
		inline bool EnableDebugOverlay = false;  // Show debug info (hovered index, cursor angle, etc)
		
		// Ammo count display
		inline float CountRadiusRatio = 0.72f;  // Position ammo count at this ratio between inner and outer radius
		
		// Icon display
		inline bool ShowIcons = true;
		inline float IconSize = 48.0f;
		inline float IconRadiusRatio = 0.72f;  // Position icons at this ratio between inner and outer radius
		inline bool IconHoverGlow = true;
		inline bool ShowCursorIndicator = true;  // Draw the cursor marker dot/asset on the wheel ring
		inline ImU32 IconHoverGlowColor = IM_COL32(255, 215, 0, 100);  // Gold glow
		
		// ========== GEOMETRY ==========
		// Wheel size and shape
		inline float InnerRadiusRatio = 0.55f;  // Inner radius as ratio of outer (0.20..0.85), controls slot thickness
		inline float SlotGapDeg = 1.5f;         // Gap between slots in degrees (0..8)
		
		// Icon layout
		inline float IconSizePx = 64.0f;        // Icon size in pixels (16..128)
		inline float IconRadialOffsetPx = 0.0f; // Radial offset for icons (-50..50)
		inline float TextRadialOffsetPx = 0.0f; // Radial offset for slot text (-50..50)
		
		// Center panel (info area)
		inline bool CenterEnabled = true;
		inline bool CenterBgEnabled = true;
		inline float CenterBgOpacity = 0.7f;    // 0.0..1.0
		inline float CenterPaddingPx = 10.0f;   // 0..40
		inline float CenterMaxWidthRatio = 0.75f; // 0.3..1.0, width relative to inner radius
		inline float CenterLineSpacingPx = 4.0f;  // 0..20
		
		// Center panel positioning (TASK 2)
		inline float CenterPanelInsetRatio = 0.5f;  // How far to push panel toward arc mid (0..1)
		inline float CenterPanelSafeMargin = 60.0f; // Viewport edge margin in pixels
		
		// ========== TEXT/FONT SIZING ==========
		// Font pixel sizes (select nearest from atlas: 14,16,18,20,24,28,32,36,42,48)
		inline float NameFontPx = 24.0f;        // Ammo name text size (10..64)
		inline float CountFontPx = 20.0f;       // Ammo count text size (10..64)
		inline float CenterFontPx = 20.0f;      // Center panel text size (10..64)
		

		// Legacy scale (kept for compatibility, applied on top of font size)
		inline float NameTextScale = 1.0f;      // Scale factor for ammo name text (0.5..3.5)

		// ========== POPUP SETTINGS ==========
		inline bool PopupEnabled = true;
		inline float PopupIconSizePx = 96.0f;
		inline float PopupNameFontPx = 32.0f;
		inline float PopupCountFontPx = 24.0f;
		inline float PopupOffsetPx = 80.0f;     // Distance from wheel outer edge
		inline float PopupPaddingPx = 15.0f;
		inline bool PopupUseCustomColor = false;
		inline ImU32 PopupBackgroundColor = IM_COL32(0, 0, 0, 180);
		
		// Circular bubble popup (TASK 5)
		inline float PopupBubbleRadius = 85.0f;     // Fixed bubble radius in pixels
		inline bool PopupCircular = true;           // Use circular bubble instead of rectangle
		// Popup shape mode: 0=Legacy (uses PopupCircular), 1=Circle, 2=RoundedRect, 3=OrganicBlob,
		// 4=DragonFireBlob, 5=SunDragonBlob
		inline int PopupShapeMode = 0;
		// Organic/DragonFire/SunDragon blob tuning (used when PopupShapeMode=3, 4, or 5)
		inline float PopupBlobJaggedness = 0.16f;   // 0.0..0.45, higher = more irregular silhouette
		inline float PopupBlobWobbleSpeed = 1.0f;   // 0.0..8.0, wobble animation speed
		inline int PopupBlobPointCount = 18;        // 8..48, polygon complexity
		// SunDragon palette tone: 0.0=pale/soft, 1.0=default, 2.0=more vivid
		inline float PopupSunDragonTone = 0.65f;
		inline float PopupAnimationSpeed = 8.0f;    // Scale/fade animation speed
		
		// ========== POPUP ANIMATION ==========
		namespace PopupAnim {
			inline bool Enabled = true;
			inline float HoverInMs = 120.0f;        // Animation duration for hover in (ms)
			inline float HoverOutMs = 90.0f;        // Animation duration for hover out (ms)
			inline float ScaleFrom = 0.85f;         // Starting scale
			inline float ScaleTo = 1.0f;            // Ending scale
			inline int Easing = 1;                  // 0=Linear, 1=OutCubic, 2=OutBack (overshoot)
			inline float BorderThickness = 2.5f;    // Border line thickness
			inline float BorderOpacity = 0.8f;      // Border opacity
			inline float BackgroundOpacity = 0.85f; // Background fill opacity
		}

		// ========== LABEL SETTINGS ==========
		inline bool LabelShow = true;           // Show text labels on slots
		inline int LabelTruncateLength = 10;    // Max chars before truncation
		inline bool LabelAbbreviate = true;     // Use abbreviations if available
		
		// Multi-line text stacking (TASK 3)
		inline bool LabelMultiLine = true;      // Enable dynamic multi-line stacking
		inline float LabelMaxSlotArcRatio = 0.75f; // Max text width as ratio of slot arc length
		
		// ========== NAME LABEL LAYOUT SYSTEM ==========
		// Layout modes: 0=LegacyEllipsis, 1=Wrap, 2=ShrinkToFit, 3=Hybrid
		inline int NameLayoutMode = 0;          // 0=Legacy (backwards compatible)
		inline int NameMaxLines = 2;            // Max lines for Wrap/Hybrid modes
		inline float NameMinFontPx = 16.0f;     // Min font size for Shrink/Hybrid modes
		inline float NameMaxWidthPx = 0.0f;     // 0=Auto, >0=clamp wrap width
		inline float NamePanelPaddingPx = 12.0f;
		inline float NameLineSpacingPx = 2.0f;
		inline float NameMarginPx = 16.0f;      // Margin from screen edges
		
		// Text background panel (behind name+count block)
		inline bool NameTextBgEnabled = false;
		inline float NameTextBgOpacity = 0.60f; // 0.0..1.0
		inline ImU32 NameTextBgColor = IM_COL32(20, 20, 22, 255);  // dark neutral for readability
		inline float NameTextBgCornerRounding = 6.0f;
		inline float NameTextBgExtraPaddingPx = 6.0f;
		inline float NameTextBgInsetPx = 0.0f;
		
		// Debug visualization
		inline bool DebugDrawTextRects = false;
		
		// ========== COLOR PRESETS ==========
		// Preset indices: 0=Custom, 1=White, 2=Black, 3=Red, 4=Green, 5=Blue, 6=Yellow, 7=Orange, 8=Cyan, 9=Magenta, 10=Gray
		inline uint32_t NameTextColorPreset = 1;      // Default: White
		inline uint32_t NameTextOpacity = 255;        // 0..255
		inline uint32_t LowAmmoColorPreset = 3;       // Default: Red
		inline uint32_t LowAmmoOpacity = 255;
		inline uint32_t SelectedColorPreset = 1;      // Default: White
		inline uint32_t SelectedOpacity = 255;
		inline uint32_t HoverColorPreset = 1;         // Default: White
		inline uint32_t HoverOpacity = 60;            // Translucent
		
		// Computed colors (from preset + opacity, or custom RGBA)
		inline ImU32 NameTextColor = IM_COL32(255, 255, 255, 255);
		inline ImU32 LowAmmoIndicatorColor = IM_COL32(255, 60, 60, 255);
		inline ImU32 SelectedIndicatorColor = IM_COL32(255, 255, 255, 255);
		inline ImU32 HoverHighlightColor = IM_COL32(255, 255, 255, 60);
		
		// Custom RGBA (used when preset == 0)
		inline uint32_t NameTextColorR = 255, NameTextColorG = 255, NameTextColorB = 255, NameTextColorA = 255;
		inline uint32_t LowAmmoColorR = 255, LowAmmoColorG = 60, LowAmmoColorB = 60, LowAmmoColorA = 255;
		inline uint32_t SelectedColorR = 255, SelectedColorG = 255, SelectedColorB = 255, SelectedColorA = 255;
		inline uint32_t HoverColorR = 255, HoverColorG = 255, HoverColorB = 255, HoverColorA = 60;
		
		// ========== TASK 1: COLOR OVERRIDE SYSTEM ==========
		// Override colors persist as packed ImU32 (0xAABBGGRR) in AmmoWheel.ini:
		// SlotLabelColor, ArrowLabelColor, BorderColor, CenterLabelColor, SelectedIndicatorColor.
		// Legacy R/G/B/A keys remain supported for backward compatibility.
		// Slot label text color override
		inline bool SlotLabelColorOverrideEnabled = false;
		inline uint32_t SlotLabelColorPreset = 1;  // 0=Custom, 1=White, 2=Cream, 3=Gold, 4=Silver, 5=Copper, 6=Ice, 7=Blood, 8=Forest, 9=Shadow, 10=Parchment
		inline uint32_t SlotLabelColorR = 255, SlotLabelColorG = 255, SlotLabelColorB = 255, SlotLabelColorA = 255;
		inline float SlotLabelColorOpacity = 1.0f;
		inline ImU32 SlotLabelColorComputed = IM_COL32(255, 255, 255, 255);

		// Arrow label text color override (active/hovered slot label)
		inline bool ArrowLabelColorOverrideEnabled = false;
		inline uint32_t ArrowLabelColorPreset = 1;  // 0=Custom, 1=White, 2=Cream, 3=Gold, 4=Silver, 5=Copper, 6=Ice, 7=Blood, 8=Forest, 9=Shadow, 10=Parchment
		inline uint32_t ArrowLabelColorR = 255, ArrowLabelColorG = 255, ArrowLabelColorB = 255, ArrowLabelColorA = 255;
		inline float ArrowLabelColorOpacity = 1.0f;
		inline ImU32 ArrowLabelColorComputed = IM_COL32(255, 255, 255, 255);
		
		// Border color override
		inline bool BorderColorOverrideEnabled = false;
		inline uint32_t BorderColorPreset = 3;  // Default: Gold
		inline uint32_t BorderColorR = 184, BorderColorG = 134, BorderColorB = 11, BorderColorA = 200;
		inline float BorderColorOpacity = 1.0f;
		inline ImU32 BorderColorComputed = IM_COL32(184, 134, 11, 200);
		
		// Center panel label color override (non-damage lines)
		inline bool CenterLabelColorOverrideEnabled = false;
		inline uint32_t CenterLabelColorPreset = 1;  // Default: White
		inline uint32_t CenterLabelColorR = 255, CenterLabelColorG = 255, CenterLabelColorB = 255, CenterLabelColorA = 255;
		inline float CenterLabelColorOpacity = 1.0f;
		inline ImU32 CenterLabelColorComputed = IM_COL32(255, 255, 255, 255);
		
		// ========== TASK 2: BOLD LABEL FORMATTING ==========
		inline bool NameBoldEnabled = false;
		inline int NameBoldMode = 1;  // 0=FontVariant (if available), 1=FauxBold
		inline float NameBoldStrengthPx = 0.8f;  // Faux-bold offset in pixels (0.5-2.0)
		
		// ========== TASK 3: INDICATOR REDESIGN ==========
		// Hover indicator (brightness only, no blink)
		inline bool HoverBrightnessEnabled = true;
		inline float HoverBrightnessStrength = 1.3f;  // Multiplier (1.0-2.0)
		
		// Selected indicator (blinking end marker + slot highlight)
		inline bool SelectedBlinkEnabled = true;
		inline float SelectedBlinkSpeedHz = 2.0f;  // Blinks per second
		inline float SelectedBlinkMinAlpha = 0.4f;
		inline float SelectedBlinkMaxAlpha = 1.0f;
		inline uint32_t SelectedIndicatorColorPreset = 3;  // Gold
		inline uint32_t SelectedIndicatorColorR = 255, SelectedIndicatorColorG = 215, SelectedIndicatorColorB = 0, SelectedIndicatorColorA = 255;
		inline ImU32 SelectedIndicatorColorComputed = IM_COL32(255, 215, 0, 255);
		inline float SelectedIndicatorSizeScale = 1.0f;
		inline float SelectedSlotBlinkStrength = 0.3f;  // How much the slot brightens (0-1)
		
		// ========== TASK 4: POPUP FLIPBOOK TOGGLE ==========
		inline bool PopupFlipbookEnabled = true;  // When false, block only flipbook popup-bubble assets
		
		// ========== VISUAL POLISH ==========
		// Background layer (blurred backdrop behind wheel)
		inline bool BackgroundEnabled = true;
		inline float BackgroundOpacity = 0.6f;
		inline float BackgroundRadiusScale = 1.12f;  // Scale relative to outer radius
		inline float BackgroundSoftEdgeRatio = 0.28f;  // 0=no fade, 1=full radial fade
		
		// Decorative border ring
		inline bool BorderEnabled = true;
		inline float BorderInnerScale = 1.02f;   // Inner edge scale relative to outer radius
		inline float BorderOuterScale = 1.06f;   // Outer edge scale relative to outer radius
		inline ImU32 BorderColorInner = IM_COL32(184, 134, 11, 200);  // DarkGoldenRod
		inline ImU32 BorderColorOuter = IM_COL32(139, 115, 85, 150);  // Darker brown
		
		// Slot shadow/depth effects
		inline bool SlotShadowEnabled = true;
		inline float SlotShadowOffsetX = 2.0f;
		inline float SlotShadowOffsetY = 2.0f;
		inline uint32_t SlotShadowAlpha = 80;
		
		// Inner highlight on hover (depth effect)
		inline bool SlotHighlightEnabled = true;
		inline float SlotHighlightThickness = 2.0f;
		inline uint32_t SlotHighlightAlpha = 100;

		// Reskin slot background shade (helps foreground assets pop)
		inline bool SlotBackgroundShadeEnabled = false;
		inline float SlotBackgroundShadeOpacity = 0.0f;  // 0.0..1.0
		
		// Text shadow (Skyrim-style multi-layer shadow)
		inline bool TextShadowEnabled = true;
		inline uint32_t TextShadowLayers = 2;  // 1 = simple, 2 = medium, 3 = full 8-direction
		inline uint32_t TextShadowAlpha = 180;
		inline float TextShadowOffset = 1.5f;
		
		// Hover glow effect on text
		inline bool TextHoverGlowEnabled = true;
		inline ImU32 TextHoverGlowColor = IM_COL32(255, 215, 0, 120);  // Gold glow

		// ========== PERFORMANCE TIERS ==========
		// 0=Custom (no tier), 1=Low, 2=Balanced, 3=High
		namespace Performance {
			inline int Tier = 0;
			// Per-feature overrides: when true, the tier will not modify that feature set
			inline bool OverrideVisualPolish = false;
			inline bool OverrideAnimations = false;
			inline bool OverridePopup = false;
			inline bool OverrideLabels = false;
			inline bool OverrideCenterPanel = false;
			inline bool OverrideIndicators = false;
			inline float InventorySnapshotIntervalSeconds = 0.25f;
		}

		// ========== ANIMATIONS ==========
		// Hover pulse animation (pulsing glow around hovered slot)
		inline bool HoverPulseEnabled = true;
		inline float HoverPulseSpeed = 3.0f;  // Pulse frequency
		inline float HoverPulseSize = 5.0f;   // Max glow extension in pixels
		inline ImU32 HoverPulseColor = IM_COL32(255, 215, 0, 120);  // Gold
		
		// Slot dividers (decorative lines between slots)
		inline bool SlotDividersEnabled = true;
		inline bool SlotDividerReskinBreathingEnabled = false;  // Pulse alpha for reskin slot divider assets
		inline float SlotDividerReskinBreathingSpeed = 2.2f;      // Pulse speed (Hz-like multiplier)
		inline float SlotDividerReskinBreathingIntensity = 0.45f; // 0=static, 1=full pulse range
		inline float SlotDividerReskinBreathingOpacity = 1.0f;    // Base opacity multiplier
		inline float SlotDividerThickness = 2.0f;
		inline ImU32 SlotDividerColor = IM_COL32(139, 69, 19, 200);  // SaddleBrown
		
		// Center panel decoration
		inline bool CenterFrameEnabled = true;
		inline bool CenterFramePulse = true;
		inline float CenterFramePulseSpeed = 2.0f;
		inline ImU32 CenterFrameColor = IM_COL32(139, 69, 19, 150);  // SaddleBrown
		inline bool CenterCornersEnabled = true;
		inline float CenterCornerSize = 12.0f;

		// ========== TIME SLOW ==========
		inline bool TimeSlowEnabled = false;
		// 1.0 = normal time, 0.5 = Skyrim Souls RE-style half speed
		inline float TimeSlowScale = 0.5f;
		
		// ========== INDICATORS ==========
		inline bool LowAmmoIndicatorEnabled = true;
		inline int LowAmmoThreshold = 10;
		inline float LowAmmoIndicatorThickness = 2.0f;
		// Low ammo indicator positioning (relative to slot ring)
		// RadiusRatio: 0=inner edge, 0.5=mid, 1=outer edge
		inline float LowAmmoIndicatorRadiusRatio = 0.5f;
		inline float LowAmmoIndicatorAngularOffsetDeg = 0.0f;
		inline float LowAmmoIndicatorRadialOffsetPx = 0.0f;
		// 0=UnderIcons, 1=OverIcons, 2=OverEverything
		inline int LowAmmoIndicatorDrawLayer = 2;
		inline float SelectedIndicatorThickness = 3.0f;
		
		// ========== THEME/SKIN ==========
		// Skin folder name under resources/ammo_wheel/skins/ (default = "default")
		inline std::string SkinName = "default";
		// Resource root for AmmoWheel-specific icons (fallback to main wheel if not found)
		inline std::string ResourceRoot = "Data/SKSE/Plugins/wheeler/resources/ammo_wheel";
		
		// Skyrim-themed color palette toggle
		inline bool UseSkyrimTheme = true;  // When true, uses Skyrim-inspired parchment/leather/gold colors
		
		// ========== SKYRIM THEME COLORS ==========
		namespace SkyrimTheme {
			// Background layers (dark leather)
			inline ImU32 BgDarkLayer = IM_COL32(20, 15, 10, 220);
			inline ImU32 BgMidLayer = IM_COL32(45, 35, 25, 200);
			inline ImU32 BgOuterGlow = IM_COL32(70, 50, 30, 100);
			
			// Slot colors (parchment-like)
			inline ImU32 SlotUnhoveredInner = IM_COL32(160, 140, 110, 200);
			inline ImU32 SlotUnhoveredOuter = IM_COL32(120, 100, 70, 180);
			inline ImU32 SlotHoveredInner = IM_COL32(210, 180, 140, 230);
			inline ImU32 SlotHoveredOuter = IM_COL32(180, 150, 110, 210);
			
			// Accent colors (metal/gold trim)
			inline ImU32 BorderGold = IM_COL32(218, 165, 32, 255);
			inline ImU32 BorderBronze = IM_COL32(139, 90, 43, 255);
			inline ImU32 HighlightGold = IM_COL32(255, 215, 0, 180);
			
			// Active/equipped indicator
			inline ImU32 ActiveArcInner = IM_COL32(218, 165, 32, 255);
			inline ImU32 ActiveArcOuter = IM_COL32(139, 90, 43, 200);
			
			// Text colors
			inline ImU32 TextPrimary = IM_COL32(240, 230, 210, 255);
			inline ImU32 TextShadow = IM_COL32(40, 30, 20, 255);
			inline ImU32 TextAccent = IM_COL32(218, 165, 32, 255);
			
			// Status indicators
			inline ImU32 EquippedIndicator = IM_COL32(50, 205, 50, 255);
			inline ImU32 LowAmmoWarning = IM_COL32(220, 20, 60, 255);
		}

		// Input bindings
		namespace MKB
		{
			inline uint32_t toggleAmmoWheel = 42;  // Left Shift (DIK code)
			inline uint32_t toggleAmmoWheelMouse = 0;  // Mouse button (256=Left, 257=Right, 258=Middle, 259=X1, 260=X2)
			inline uint32_t modifierKey = 0;  // Optional modifier key (0 = no modifier required)
		}
		namespace GamePad
		{
			inline uint32_t toggleAmmoWheel = 0;  // Unmapped by default
			inline uint32_t modifierButton = 0;  // Optional modifier button (0 = no modifier required)
		}
		// Human-readable key names for UI display (read/write from AmmoWheel.ini)
		inline std::string ToggleKeyMKBName = "Left Shift";
		inline std::string ToggleKeyGamepadName = "Unbound";
		inline std::string ModifierKeyMKBName = "None";
		inline std::string ModifierButtonGamepadName = "None";
		inline std::string ToggleMouseButtonName = "None";
		
		// ========== CHORD FALLBACK BEHAVIOR ==========
		// When AmmoWheel has a modifier (e.g., Shift+MMB) but cannot open (no ranged weapon),
		// allow the base key (MMB) to fall through to MainWheel binding.
		// OFF = strict: modified chord blocks base key when modifier is held
		// ON  = lenient: if modified chord fails, try unmodified bindings
		inline bool AllowChordFallback = true;
		// ========== INPUT SAFEGUARDS ==========
		// In Inventory menu, require holding the AmmoWheel toggle to open.
		namespace InputSafeguards
		{
			inline bool MenuHoldToOpenEnabled = true;
			inline float MenuHoldToOpenSeconds = 0.50f;
		}

		
		// ========== INPUT BLOCKING (TASK 1) ==========
		// Block attack input when AmmoWheel is open
		inline bool BlockAttackWhenOpen = true;       // Master toggle for attack blocking
		inline bool ConsumeLMBWhenOpen = true;        // Consume LMB (select ammo, no attack)
		inline bool ConsumeRMBWhenOpen = true;        // Consume RMB (unequip ammo)
		inline bool ConsumeGamepadAttackWhenOpen = true;  // Block RT/LT attack on gamepad
		inline bool ClickSelectRequiresHover = true;  // LMB only selects if hovering valid slot
		inline bool AllowRMBUnequip = true;           // RMB unequips current ammo
		
		// ========== CENTER PANEL SHAPE (TASK 2) ==========
		// Shape: 0=Auto, 1=Rectangle, 2=Circle, 3=RoundedRect
		inline int CenterPanelShapeIndex = 0;
		inline float CenterPanelCornerRounding = 8.0f;  // For RoundedRect shape
		inline float CenterPanelBorderThickness = 1.5f;
		inline float CenterPanelBorderAlpha = 0.8f;
		inline bool CenterPanelClampToScreen = true;
		// Position mode: 0=AutoInsideArc, 1=FixedOffset
		inline int CenterPanelPositionMode = 0;
		inline float CenterPanelOffsetX = 0.0f;
		inline float CenterPanelOffsetY = 0.0f;
		
		// Center panel text layout
		inline bool CenterTextEnabled = true;
		inline float CenterTextMinFontSize = 12.0f;
		inline float CenterTextMaxFontSize = 28.0f;
		// Layout mode: 0=Stacked, 1=Wrapped, 2=SingleLine
		inline int CenterTextLayoutMode = 0;
		inline int CenterTextMaxLines = 4;
		inline bool CenterTextEllipsisEnabled = true;
		inline float CenterTextMaxWidthRatio = 0.9f;
		inline bool CenterTextPreferWordSplit = true;
		inline bool CenterTextShadowEnabled = true;
		inline bool CenterTextOutlineEnabled = false;
		inline float CenterTextOffsetX = 0.0f;  // Per-panel text block offset (pixels)
		inline float CenterTextOffsetY = 0.0f;  // Per-panel text block offset (pixels)
		inline bool CenterShowDescription = false;
		inline int CenterMaxDescriptionLines = 2;
		inline float CenterDescriptionFontScale = 0.72f;
		inline float CenterDescriptionOffsetX = 0.0f;
		inline float CenterDescriptionOffsetY = 0.0f;
		inline float CenterDescriptionLineSpacingPx = 0.0f;
		inline float CenterDescriptionOpacity = 1.0f;
		inline ImU32 CenterDescriptionColor = IM_COL32(220, 220, 220, 255);
		
		// ========== CENTER PANEL TEXT WRAPPING ==========
		// Word wrap settings for long ammo names in half-wheel edge layouts
		inline bool EnableWordWrap = true;          // Enable word wrapping for long text
		inline bool WrapAtWordBoundary = true;      // Prefer word boundaries over mid-word breaks
		inline float WrapMaxLineWidthRatio = 0.42f; // Max line width relative to screen width
		inline float WrapMaxLineWidthPx = 0.0f;     // If >0, overrides ratio (absolute pixels)
		inline float WrapSafeMarginPx = 24.0f;      // Keep text inside screen edges
		inline bool AddWrapHyphen = false;          // Add '-' at end of wrapped lines
		inline int WrapMaxLines = 3;                // Maximum number of wrapped lines
		
		// ========== CENTER PANEL FIELDS ==========
		namespace CenterFields {
			inline bool ShowName = true;
			inline bool ShowDamage = true;
			inline bool ShowPoison = true;
			inline bool ShowType = true;
			inline bool ShowCount = true;
			inline bool ShowSource = true;  // Vanilla vs Modded
			// Field order: comma-separated list of field names
			inline std::string Order = "Name,Damage,Poison,Type,Count,Source";
			
			// Damage highlight colors
			inline ImU32 MaxDamageColor = IM_COL32(255, 200, 100, 255);  // Gold for highest damage
			inline ImU32 OtherDamageColor = IM_COL32(255, 120, 120, 255); // Light red for other damage values
		}
		
		// ========== CENTER PANEL SVG SKIN ==========
		namespace CenterSkin {
			inline bool UseSVG = false;
			inline std::string SVGPath = R"(Data\SKSE\Plugins\wheeler\resources\ammo_wheel\center_panel.svg)";
			inline ImU32 TintColor = IM_COL32(255, 255, 255, 255);
			inline float Opacity = 1.0f;
			inline float Scale = 1.0f;
		}
		
		// ========== ICON SYSTEM (RESKIN SUPPORT) ==========
		// AmmoWheel-specific icon folders for reskin mods
		inline std::string IconDirectory = R"(.\Data\SKSE\Plugins\wheeler\resources\ammo_wheel\icons)";
		inline std::string IconCustomDirectory = R"(.\Data\SKSE\Plugins\wheeler\resources\ammo_wheel\icons_custom)";
		inline bool UseDedicatedIconFolder = true;  // If false, uses main Wheeler icons only (emergency rollback)
		
		// ========== SKIN SYSTEM (FULLY DATA-DRIVEN RESKIN) ==========
		namespace Skin {
			// Skin root and loader settings
			inline bool UseAmmoWheelStylesIni = true;  // Load from dedicated Styles.ini
			inline std::string SkinRoot = R"(.\Data\SKSE\Plugins\wheeler\resources\ammo_wheel)";
			inline std::string StylesIniPath = R"(.\Data\SKSE\Plugins\wheeler\resources\ammo_wheel\Styles.ini)";
			inline bool StylesLoaded = false;  // Track if styles have been loaded
			
			// ========== PRESET SYSTEM TOGGLE ==========
			// When FALSE (default): Use legacy style behavior from [AmmoWheel.*] sections only.
			//                       Ignore [Preset.*] sections entirely. Vanilla fallback works.
			// When TRUE: Enable [Preset.*] overrides and Icon.Path mappings.
			//            Presets apply per-slot based on AMMO_KID.ini mappings.
			inline bool UsePresetStyles = false;  // Default OFF for vanilla compatibility
			
			// ========== SLOT GEOMETRY ==========
			inline float SlotAngularPaddingDeg = 2.0f;      // Gap between slots in degrees
			inline float SlotInnerRadiusPadding = 0.0f;     // Padding from inner radius
			inline float SlotOuterRadiusPadding = 0.0f;     // Padding from outer radius
			inline float SlotCornerRounding = 0.0f;         // Corner rounding (if applicable)
			inline float BackgroundOpacity = 0.75f;
			
			// ========== SLOT COLORS ==========
			inline ImU32 UnhoveredColorBegin = IM_COL32(160, 144, 125, 128);
			inline ImU32 UnhoveredColorEnd = IM_COL32(120, 109, 94, 64);
			inline ImU32 HoveredColorBegin = IM_COL32(212, 196, 168, 255);
			inline ImU32 HoveredColorEnd = IM_COL32(181, 164, 141, 255);
			inline ImU32 SelectedColorBegin = IM_COL32(208, 160, 112, 255);
			inline ImU32 SelectedColorEnd = IM_COL32(160, 128, 96, 255);
			
			// ========== TEXT STYLING ==========
			inline ImU32 TextColor = IM_COL32(240, 230, 210, 255);
			inline ImU32 TextShadowColor = IM_COL32(40, 30, 20, 255);
			inline float TextSize = 18.0f;
			inline float TextShadowOffsetX = 1.0f;
			inline float TextShadowOffsetY = 1.0f;
			inline int TextWrapMode = 1;  // 0=None, 1=Word, 2=Char
			inline int TextMaxLines = 3;
			
			// ========== ICON STYLING ==========
			inline bool IconsEnabled = true;
			inline int IconPlacementMode = 1;       // 0=Center, 1=RadialMid, 2=CustomPolar
			inline float IconRadialOffset = 0.55f;  // 0..1 of ring thickness
			inline float IconPaddingPixels = 6.0f;
			inline int IconRotationMode = 0;        // 0=FollowSlot, 1=Upright, 2=Fixed
			inline float IconRotationOffsetDeg = 0.0f;
			inline float IconFixedAngleDeg = 0.0f;  // Used when RotationMode=Fixed
			inline float IconRotationSafetyScale = 0.85f;  // Scale down for rotation clearance
			inline bool IconClampInsideSlot = true;
			inline int IconClipMode = 2;            // 0=None, 1=Rect, 2=ConservativeFit
			inline ImU32 IconTintColor = IM_COL32(255, 255, 255, 255);
			
			// ========== INDICATOR ENUMS ==========
			// Shape types for indicators
			// 0=Arc, 1=Ring, 2=BorderSweep, 3=DashedArc
			// Cap styles: 0=Butt, 1=Round
			// Animation modes: 0=None, 1=Pulse, 2=Sweep
			
			// ========== INDICATOR: SELECTED ==========
			inline bool SelectedEnabled = true;
			inline int SelectedShape = 0;           // Arc
			inline float SelectedThicknessPx = 5.0f;
			inline float SelectedRadiusOffsetPx = -2.0f;
			inline float SelectedStartAngleOffsetDeg = 0.0f;
			inline float SelectedSweepDeg = 0.0f;   // 0 = match slot
			inline ImU32 SelectedColorBeginInd = IM_COL32(218, 165, 32, 255);
			inline ImU32 SelectedColorEndInd = IM_COL32(139, 115, 85, 255);
			inline float SelectedAlpha = 1.0f;
			inline int SelectedCapStyle = 1;        // Round
			inline int SelectedAnimMode = 0;        // None
			inline float SelectedAnimSpeed = 1.0f;
			inline bool SelectedShowWhenSelected = true;
			inline bool SelectedShowWhenHovered = false;
			inline bool SelectedHideIfAnotherStateActive = false;
			inline int SelectedPriority = 2;
			
			// ========== INDICATOR: HOVERED ==========
			inline bool HoveredEnabled = true;
			inline int HoveredShape = 0;            // Arc
			inline float HoveredThicknessPx = 3.0f;
			inline float HoveredRadiusOffsetPx = 2.0f;
			inline float HoveredStartAngleOffsetDeg = 0.0f;
			inline float HoveredSweepDeg = 0.0f;
			inline ImU32 HoveredColorBeginInd = IM_COL32(255, 255, 255, 200);
			inline ImU32 HoveredColorEndInd = IM_COL32(200, 200, 200, 150);
			inline float HoveredAlpha = 1.0f;
			inline int HoveredCapStyle = 1;
			inline int HoveredAnimMode = 1;         // Pulse
			inline float HoveredAnimSpeed = 3.0f;
			inline bool HoveredShowWhenHovered = true;
			inline bool HoveredShowWhenSelected = false;
			inline bool HoveredHideIfAnotherStateActive = false;
			inline int HoveredPriority = 3;
			
			// ========== INDICATOR: ACTIVE ==========
			inline bool ActiveEnabled = true;
			inline int ActiveShape = 0;             // Arc
			inline float ActiveThicknessPx = 6.0f;
			inline float ActiveRadiusOffsetPx = 0.0f;
			inline float ActiveStartAngleOffsetDeg = 0.0f;
			inline float ActiveSweepDeg = 0.0f;
			inline ImU32 ActiveColorBeginInd = IM_COL32(50, 205, 50, 255);
			inline ImU32 ActiveColorEndInd = IM_COL32(34, 139, 34, 255);
			inline float ActiveAlpha = 1.0f;
			inline int ActiveCapStyle = 1;
			inline int ActiveAnimMode = 0;
			inline float ActiveAnimSpeed = 1.0f;
			inline bool ActiveShowWhenActive = true;
			inline bool ActiveShowWhenSelected = false;
			inline bool ActiveHideIfAnotherStateActive = false;
			inline int ActivePriority = 4;
			
			// ========== INDICATOR: CHARGE ==========
			inline bool ChargeEnabled = false;
			inline int ChargeShape = 2;             // BorderSweep
			inline float ChargeThicknessPx = 4.0f;
			inline float ChargeRadiusOffsetPx = 4.0f;
			inline float ChargeStartAngleOffsetDeg = 0.0f;
			inline float ChargeSweepDeg = 0.0f;     // Animated based on charge progress
			inline ImU32 ChargeColorBeginInd = IM_COL32(100, 149, 237, 255);
			inline ImU32 ChargeColorEndInd = IM_COL32(65, 105, 225, 255);
			inline float ChargeAlpha = 1.0f;
			inline int ChargeCapStyle = 1;
			inline int ChargeAnimMode = 2;          // Sweep
			inline float ChargeAnimSpeed = 1.0f;
			inline bool ChargeShowWhenCharging = true;
			inline bool ChargeHideIfAnotherStateActive = true;
			inline int ChargePriority = 5;
			
			// ========== INDICATOR TOGGLES (INI) ==========
			inline bool EnableSelectedIndicator = true;
			inline bool EnableHoveredIndicator = true;
			inline bool EnableActiveIndicator = true;
			inline bool EnableChargeIndicator = false;
		}
		
		// ========== STYLE PRESET SYSTEM ==========
		// A StylePreset contains ALL visual parameters for rendering an ammo slot.
		// Presets are resolved per-entry based on FormID, Keyword, Type, or Fallback.
		struct IndicatorPreset {
			bool Enabled = true;
			int Shape = 0;              // 0=Arc, 1=Ring, 2=BorderSweep, 3=DashedArc
			float ThicknessPx = 5.0f;
			float RadiusOffsetPx = -2.0f;
			float StartAngleOffsetDeg = 0.0f;
			float SweepDeg = 0.0f;      // 0 = match slot
			ImU32 ColorBegin = IM_COL32(218, 165, 32, 255);
			ImU32 ColorEnd = IM_COL32(139, 115, 85, 255);
			float Alpha = 1.0f;
			int CapStyle = 1;           // 0=Butt, 1=Round
			int AnimMode = 0;           // 0=None, 1=Pulse, 2=Sweep
			float AnimSpeed = 1.0f;
		};
		
		struct StylePreset {
			std::string PresetId = "Default";
			
			// Slot geometry
			float SlotAngularPaddingDeg = 2.0f;
			float SlotInnerRadiusPadding = 0.0f;
			float SlotOuterRadiusPadding = 0.0f;
			float SlotCornerRounding = 0.0f;
			float BackgroundOpacity = 0.75f;
			
			// Slot colors
			ImU32 UnhoveredColorBegin = IM_COL32(160, 144, 125, 128);
			ImU32 UnhoveredColorEnd = IM_COL32(120, 109, 94, 64);
			ImU32 HoveredColorBegin = IM_COL32(212, 196, 168, 255);
			ImU32 HoveredColorEnd = IM_COL32(181, 164, 141, 255);
			ImU32 SelectedColorBegin = IM_COL32(208, 160, 112, 255);
			ImU32 SelectedColorEnd = IM_COL32(160, 128, 96, 255);
			
			// Border/Frame
			bool BorderEnabled = false;
			float BorderThickness = 2.0f;
			ImU32 BorderColor = IM_COL32(180, 160, 140, 200);
			bool FrameEnabled = false;
			std::string FrameSvg;       // Optional SVG frame overlay
			
			// Background
			std::string BackgroundSvg;  // Optional SVG background
			
			// Text styling
			ImU32 TextColor = IM_COL32(240, 230, 210, 255);
			ImU32 TextShadowColor = IM_COL32(40, 30, 20, 255);
			float TextSize = 18.0f;
			float TextShadowOffsetX = 1.0f;
			float TextShadowOffsetY = 1.0f;
			int TextWrapMode = 1;       // 0=None, 1=Word, 2=Char
			int TextMaxLines = 3;
			
			// Icon styling
			bool IconsEnabled = true;
			std::string IconPath;       // Override icon path (empty = use priority search)
			int IconPlacementMode = 1;  // 0=Center, 1=RadialMid, 2=CustomPolar
			float IconRadialOffset = 0.55f;
			float IconPaddingPixels = 6.0f;
			int IconRotationMode = 0;   // 0=FollowSlot, 1=Upright, 2=Fixed
			float IconRotationOffsetDeg = 0.0f;
			float IconFixedAngleDeg = 0.0f;
			float IconRotationSafetyScale = 0.85f;
			ImU32 IconTintColor = IM_COL32(255, 255, 255, 255);
			
			// Indicators
			IndicatorPreset Selected;
			IndicatorPreset Hovered;
			IndicatorPreset Active;
			IndicatorPreset Charge;
			
			// Initialize with default indicator values
			StylePreset() {
				Selected.Enabled = true;
				Selected.ThicknessPx = 5.0f;
				Selected.RadiusOffsetPx = -2.0f;
				Selected.ColorBegin = IM_COL32(218, 165, 32, 255);
				Selected.ColorEnd = IM_COL32(139, 115, 85, 255);
				Selected.AnimMode = 0;
				
				Hovered.Enabled = true;
				Hovered.ThicknessPx = 3.0f;
				Hovered.RadiusOffsetPx = 2.0f;
				Hovered.ColorBegin = IM_COL32(255, 255, 255, 200);
				Hovered.ColorEnd = IM_COL32(200, 200, 200, 150);
				Hovered.AnimMode = 1;  // Pulse
				Hovered.AnimSpeed = 3.0f;
				
				Active.Enabled = true;
				Active.ThicknessPx = 6.0f;
				Active.RadiusOffsetPx = 0.0f;
				Active.ColorBegin = IM_COL32(50, 205, 50, 255);
				Active.ColorEnd = IM_COL32(34, 139, 34, 255);
				
				Charge.Enabled = false;
				Charge.Shape = 2;  // BorderSweep
				Charge.ThicknessPx = 4.0f;
				Charge.RadiusOffsetPx = 4.0f;
				Charge.ColorBegin = IM_COL32(100, 149, 237, 255);
				Charge.ColorEnd = IM_COL32(65, 105, 225, 255);
				Charge.AnimMode = 2;  // Sweep
			}
		};
		
		// Preset cache and resolver
		namespace PresetSystem {
			// Loaded presets from Styles.ini
			inline std::unordered_map<std::string, StylePreset> LoadedPresets;
			
			// Mapping tables from AMMO_KID.ini
			inline std::unordered_map<uint32_t, std::string> FormIDToPreset;      // FormID -> PresetId
			inline std::unordered_map<std::string, std::string> KeywordToPreset;  // Keyword EditorID -> PresetId
			inline std::unordered_map<std::string, std::string> TypeToPreset;     // "Arrow"/"Bolt" -> PresetId
			inline std::string FallbackPresetId = "Default";
			
			// Per-mapping icon overrides (keyword -> icon filename)
			inline std::unordered_map<std::string, std::string> KeywordToIcon;
			inline std::unordered_map<uint32_t, std::string> FormIDToIcon;
			
			// Thread-safe loading state
			inline std::atomic<bool> PresetsLoaded{false};
			inline std::mutex PresetLoadMutex;
			
			// Default preset (emergency fallback)
			inline StylePreset DefaultPreset;
			
			// Get preset by ID (returns default if not found)
			inline const StylePreset& GetPreset(const std::string& presetId) {
				auto it = LoadedPresets.find(presetId);
				if (it != LoadedPresets.end()) {
					return it->second;
				}
				return DefaultPreset;
			}
		}
		
		// ========== LEGACY VISUAL OVERRIDES (BACKWARD COMPAT) ==========
		// Allow AmmoWheel-specific visual styles without affecting main Wheeler
		inline bool UseCustomStyles = false;
		inline uint32_t CustomUnhoveredColorBegin = 0x80A0907D;
		inline uint32_t CustomUnhoveredColorEnd = 0x40786D5E;
		inline uint32_t CustomHoveredColorBegin = 0xFFD4C4A8;
		inline uint32_t CustomHoveredColorEnd = 0xFFB5A48D;
		inline uint32_t CustomActiveArcColorBegin = 0xFFDAA520;
		inline uint32_t CustomActiveArcColorEnd = 0xFF8B7355;
		inline uint32_t CustomTextColor = 0xFFF0E6D2;
		inline uint32_t CustomTextShadowColor = 0xFF281E14;
		inline float CustomBackgroundOpacity = 0.75f;
		
		// ========== AMMOWHEEL SOUNDS ==========
		namespace Sounds {
			// Master toggle for AmmoWheel hover sounds
			inline bool EnableHoverSlotSound = true;
			
			// Sound EditorID (same pattern as main Wheeler)
			// Default: UIMenuFocus for Skyrim-native hover feedback
			inline std::string HoverSlotSoundEditorID = "UIMenuFocus";  // Skyrim's native menu focus sound
			
			// Volume multiplier (0.0..3.0, 1.0 = normal, higher = louder)
			inline float HoverSlotSoundVolume = 1.0f;
			
			// Spam prevention
			inline uint32_t HoverSlotSoundCooldownMs = 35;   // Minimum ms between sounds
			inline bool HoverSlotSoundOnlyOnSlotChange = true;  // Only play when slot actually changes
			
			// Optional: ignore sound while player is attacking
			inline bool HoverSlotSoundIgnoreWhileFiring = false;
			
			// Debug logging
			inline bool DebugLogHoverSound = false;
		}
	}

	// ========== FONT / GLYPH CONFIGURATION ==========
	namespace Font
	{
		// Glyph preset modes:
		// 0 = Minimal (ASCII + common punctuation)
		// 1 = Latin Basic (Latin-1 Supplement: U+0000-00FF)
		// 2 = Latin Extended (Latin-1 + Extended-A + Extended-B: covers most European)
		// 3 = Latin Full (Extended + Additional + IPA + Spacing Modifiers)
		// 4 = Custom (explicit ranges from CustomRanges string)
		inline int GlyphPreset = 2;  // Default to Latin Extended for broad European support
		
		// Custom glyph ranges string (only used when GlyphPreset = 4)
		// Format: "0020-007E,00A0-00FF,0100-017F" (hex ranges, comma-separated)
		inline std::string CustomRanges = "";
		
		// Debug options
		namespace Debug
		{
			// Show glyph test overlay with European character samples
			inline bool ShowGlyphTestOverlay = false;
			// Log glyph ranges and atlas size on build
			inline bool LogAtlasInfo = true;
		}
	}

	namespace Cooldowns
	{
		inline bool Enabled = false;
		inline bool ShowTimer = false;
		// Legacy: used by older configs. Kept for backward compatibility only.
		inline ImU32 OverlayColor = IM_COL32(255, 0, 0, 120);
		// Content-only cooldown visuals (Shouts/Spells):
		// 0 = no dim, 1 = fully dimmed (only the restored region is visible).
		inline float ContentDimAlpha = 0.47f;
		// Selected/active indicator cooldown visuals:
		inline bool SelectedIndicatorEnabled = true;
		inline ImU32 SelectedIndicatorTintColor = IM_COL32(255, 0, 0, 120);
		inline float CacheWindowSeconds = 0.1f;  // throttle expensive lookups

		// Cooldown timer text styling (centered text shown on cooldown slots).
		namespace TimerText
		{
			// 0 = UI font (current ImGui font), 1 = default font (font atlas index 0).
			inline uint32_t FontIndex = 0;
			// Pixel size for the timer text.
			inline float Size = 26.0f;
			// Timer text color (ARGB, same uint32 format as other Wheeler colors).
			inline ImU32 Color = IM_COL32(255, 255, 255, 140);
		}
	}

}
