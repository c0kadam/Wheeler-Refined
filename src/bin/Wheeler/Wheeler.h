#pragma once
#include <atomic>
#include <cstdint>
#include <array>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <optional>
#include <memory>
#include <unordered_set>
#include "nlohmann/json.hpp"
#include "imgui.h"

#include "bin/Config.h"
#include "bin/API/WheelerAPI.h"
#include "bin/Integrations/ExternalFavWheelState.h"
#include "RestorationLifecyclePolicy.h"
#include "WheelItems/LegacyWeaponRestore.h"
#include "Wheel.h"
#include "AmmoWheel.h"
class WheelItem;
class Wheeler
{
public:
	static void Init()
	{
		// insert an empty wheel
		_wheels.emplace_back(std::make_unique<Wheel>());
		ExternalFavWheelState::Prime();
		// Initialize External API
		WheelerAPI::SetInitialized(true);
	}

	/// <summary>
	/// Update wheeler, calling draw function etc...
	/// This function can only be invoked by the renderer.
	/// </summary>
	static void Update(float a_deltaTime);
	
	/// <summary>
	/// Resets everything, freeing wheels and their memebers in the hierarchy.
	/// must be called prior to reloading the wheels.
	/// </summary>
	static void Clear();
	// World disposal invalidates and drops all Wheeler-owned deferred gameplay
	// intent without mutating the outgoing player/world.
	static void DiscardTransientGameplayStateForWorldTransition(const char* a_reason);
	// Compatibility entry point retained for the existing lifecycle call sites.
	static void ResetTransientRestorationState(const char* a_reason);
	// The dMenu Reset All Wheels event can arrive outside the renderer update.
	// It requests current-world rollback and wheel reconstruction on Update's
	// serialized execution context.
	static void RequestResetAllWheelsInCurrentWorld();
	[[nodiscard]] static RestorationLifecycle::Epoch GetTransientRestorationEpoch() noexcept;
	[[nodiscard]] static bool IsTransientRestorationEpochCurrent(
		RestorationLifecycle::Epoch a_epoch) noexcept;
	static bool ExecuteTransientGameplayIfCurrent(
		RestorationLifecycle::Epoch a_epoch,
		const std::function<void()>& a_action);
	// Diagnostic-only observation. This never consumes, delays, replays, or
	// otherwise modifies the input event.
	static void ObserveHandMemoryAttackInput(
		std::uint32_t a_device,
		std::uint32_t a_rawInput,
		std::uint32_t a_mappedInput,
		std::string_view a_userEvent,
		bool a_isDown,
		bool a_isUp,
		bool a_consumed);

	static void UpdateCursorPosMouse(float a_deltaX, float a_deltaY);
	static void UpdateCursorPosGamepad(float a_x, float a_y);

	enum class InputAction : std::uint8_t
	{
		None,
		Toggle,
		ToggleIfInInventory,
		ToggleIfNotInInventory,
		ExitWheel
	};

	enum class InputDecision : std::uint8_t
	{
		None,
		OpenRequested,
		CloseRequested,
		CloseRelease,
		DeniedState,
		DeniedMenuBlocked,
		DeniedHoldThreshold,
		DeniedInventoryState,
		DeniedNoPlayer,
		DeniedNoUI,
		DeniedAmmoWheel,
		DeniedConsumed
	};

	enum class InputConsumer : std::uint8_t
	{
		None,
		MainWheel,
		AmmoWheel,
		DMenu,
		Other
	};

	enum class MutableInventoryCompatProfile : std::uint8_t
	{
		Vanilla = 0,
		EquipmentDurabilitySystem = 1
	};

	static InputAction ResolveMainWheelInputAction(std::uint32_t input, bool isGamepad);
	static void RecordMainWheelInputEvent(InputAction action, RE::INPUT_DEVICE device, std::uint32_t rawCode,
		std::uint32_t mappedCode, bool isDown, bool isUp);
	static void LogMainWheelInputDecision(InputDecision decision, InputConsumer consumer);
	static MutableInventoryCompatProfile GetMutableInventoryCompatProfile();
	static bool IsEquipmentDurabilitySystemActive();

	static void ToggleWheeler();
	static void ToggleWheelIfInInventory();
	static void ToggleWheelIfNotInInventory();

	/// <summary>
	/// Close the current wheel, if it's been opened long enough(more than 0.2 seconds).
	/// This collaborates with `Controls` that invokes this function when the user releases the wheel toggle key.
	/// Correct usage of this function and ToggleWheeler() ensures the following behavior:
	/// 	When the user presses down the toggle key for the first time, wheeler opens.
	///		If the user immediately releases the key, wheeler stays open, until the user presses the toggle key again.
	///		If the user keeps pressing the toggle key for a while and then releases the key, wheeler automatically closes.
	/// This allows the toggle key to simultaneously act as a press-open, press-close toggle, and a hold-open, release-close button.
	/// </summary>
	static void CloseWheelerIfOpenedLongEnough();
	static void CloseWheelerIfOpenedLongEnoughIfInInventory();
	static void CloseWheelerIfOpenedLongEnoughIfNotInInventory();

	
	static void TryOpenWheeler();
	static void TryCloseWheeler();

	static void OpenWheeler();
	static void CloseWheeler();
	
	/// <summary>
	/// Idempotent timescale restoration - safe to call multiple times.
	/// Restores timescale for both MainWheel and AmmoWheel if they modified it.
	/// </summary>
	static void EnsureTimescaleRestored();
	static void ResetMountedVelocityRestoreState();
	static void TryRestoreMountedVelocityAfterTimeRestore(const char* a_reason);
	
	static void NextWheel();
	static void PrevWheel();
	static bool SwitchToWheelIndexForNavigation(int a_index);
	static bool SetWheelHoveredEntryIndex(int a_wheelIndex, int a_entryIndex, bool a_moveCursorToEntry = false);
	static void PrevItemInEntry();
	static void NextItemInEntry();
	static void PrevItemInEntryGamepad();
	static void NextItemInEntryGamepad();
	static void ToggleEditModeHintsVisibility();
	static void ReturnToPreviousActionHotkeysWheel();
	static void RefreshActionHotkeysMirror();
	static void ResetActionHotkeysBridgeLayout();
	static bool TryTriggerActionHotkeysBridgeCloseAssist(std::uint32_t a_input, bool a_isGamePad);

	static bool GetCursorAngleRadian(float& r_ret);
	static bool IsLastInputGamepad();
	
	/// <summary>
	/// Get the current wheel-local cursor distance from center.
	/// Used by mouse hover guards in Wheel::Draw.
	/// </summary>
	static float GetCursorDistance();

	// Queue a poison activation to run after the wheel fully closes (prevents input-filter soft-locks).
	static bool QueuePoisonApply(RE::FormID a_poisonFormID);
	static void QueueMiscItemUse(RE::FormID a_miscItemFormID, std::uint16_t a_uniqueID = 0);
	static void QueueSGTInstrumentSpell(RE::FormID a_spellFormID);
	static bool QueueShoutActivation(RE::FormID a_shoutFormID, float a_hoverTime = 1.0f);
	static bool QueuePowerActivation(RE::FormID a_powerFormID);
	static bool QueueExternalHotkeyDispatch(
		std::uint32_t a_scanCode,
		std::uint32_t a_modifier,
		std::string_view a_displayName = {},
		std::uint32_t a_sourceSlotIndex = 0,
		std::string_view a_sourceTag = {});
	static bool IsPowerSpellType(const RE::SpellItem* spell);
	static bool IsInstantEnabledForSpell(const RE::SpellItem* spell, bool isInTransform);
	static bool IsRTUAutoInstantSpellEnabled(const RE::SpellItem* spell, bool isInTransform);
	static bool IsRTUAutoInstantShoutEnabled();
	// True while direct-cast transient equip/cast/restore pipeline is active.
	// HandMemory uses this to avoid capturing temporary direct-cast equips.
	static bool IsDirectCastPipelineActiveForHandMemory();
	static bool TryGetTrackedPersistentRestoreTargetForHandMemory(
		bool a_isLeftHand,
		RE::FormID a_currentFormID,
		RE::FormID& a_outRestoreFormID,
		LegacyWeaponRestoreToken& a_outWeaponToken);
	static void QueueDepletedConsumablesCleanup();
	static void QueueBookRead(RE::FormID a_bookFormID);

	/// <summary>
	/// Queue a concentration spell auto-stop after the specified duration.
	/// Called by WheelItemSpell when instant-casting a concentration spell in Timed mode.
	/// </summary>
	/// <param name="a_spellFormID">FormID of the spell being cast</param>
	/// <param name="a_castingSource">The casting source used (e.g., kRightHand)</param>
	/// <param name="a_maxSeconds">Maximum duration before auto-stop</param>
	static void QueueConcentrationSpellStop(RE::FormID a_spellFormID, 
		RE::MagicSystem::CastingSource a_castingSource, float a_maxSeconds);
	static void ArmAmmoWheelMenuHold(std::uint32_t mappedKey, bool isGamepad);
	static bool DisarmAmmoWheelMenuHold(std::uint32_t mappedKey, bool isGamepad);
	static bool IsAmmoWheelMenuHoldArmed(std::uint32_t mappedKey, bool isGamepad);
	static void UpdateAmmoWheelMenuHoldGate();
	
	/// <summary>
	/// Queue a deferred refund check for instant-cast summon spells.
	/// If the summon fails to spawn (effect count didn't increase), refunds the magicka difference.
	/// </summary>
	/// <param name="a_spellFormID">FormID of the summon spell</param>
	/// <param name="a_magickaBefore">Magicka value before the cast attempt</param>
	/// <param name="a_effectCountBefore">Active effect count from this spell before the cast</param>
	static void QueueInstantCastRefundCheck(RE::FormID a_spellFormID, float a_magickaBefore, int a_effectCountBefore);
	
	// Inventory prune support
	enum class PruneReason : std::uint8_t
	{
		OnOpen,   // Called when wheel opens
		OnClose,  // Called when wheel closes  
		OnGuard   // Called when inventory guard blocks an action
	};
	
	/// <summary>
	/// Prunes wheel entries that are no longer in player inventory.
	/// Does NOT depend on inventory hooks - checks current inventory state directly.
	/// Safe to call at any time when wheel is not mid-draw iteration.
	/// </summary>
	static void PruneWheelEntries_NotInInventory(PruneReason reason);

	/// <summary>
	/// Offset camera rotation with current cusor position. Returns whether a change has been made to the camera's rotation.
	/// </summary>
	static bool OffsetCamera(RE::TESCamera* a_this) = delete;

	/// <summary>
	/// Play a UI sound by editor ID if sounds are enabled. Safe to call with nullptr/empty string.
	/// </summary>
	/// <param name="a_editorID">Editor ID of the sound descriptor</param>
	/// <param name="a_volume">Volume multiplier (1.0 = normal, higher = louder)</param>
	static void PlaySoundByEditorID(const char* a_editorID, float a_volume = 1.0f);

	/// <summary>
	/// Play a shout stage sound (called when a stage threshold is crossed).
	/// </summary>
	static void PlayShoutStageSound(int stage);

	/// <summary>
	/// Update shout stage sound state based on current hover time.
	/// Called each frame while hovering a shout entry.
	/// </summary>
	static void UpdateShoutStageSounds(float hoverTime, RE::FormID shoutFormID);

	/// <summary>
	/// Reset shout stage sound state (called when entry changes or wheel closes).
	/// </summary>
	static void ResetShoutStageSounds();

	/// <summary>
	/// Activate the currently active entry with secondary (left) input, which corresponds to right mouse click or left controller trigger.
	/// If we're in edit mode:
	///  - the function first checks if there's any entry left. If not, the function calls DeleteCurrentWheel(), given there are more than 1 wheel present(must have at least 1 wheel on stack).
	///  - if there's some entry left, the function calls the current wheel's ActivateItemSecondary(), which handles deletion of entry items, or the entry via subsequent calls.
	/// If we're not in edit mode, the entry calls the current wheel's ActivateItemSecondary()
	/// </summary>
	static void ActivateHoveredEntrySecondary();

	/// <summary>
	/// Activate the currently active entry with primary (right) input, which corresponds to left mouse click or right controller trigger,
	/// The function simply invokes current entry's ActivateItemPrimary(), which either handles activation of items or addition of items, if in edit mode.
	/// </summary>
	static void ActivateHoveredEntryPrimary();

	/// <summary>
	/// Called when confirm button is pressed down. Starts hold timing for Hold-to-Use mode.
	/// </summary>
	static void OnConfirmDown();

	/// <summary>
	/// Called when confirm button is released. Triggers Hold-to-Use activation if applicable.
	/// </summary>
	static void OnConfirmUp();

	/// <summary>
	/// Called when secondary confirm button is pressed down. Starts hold timing for Hold-to-Use (left hand).
	/// </summary>
	static void OnSecondaryConfirmDown();

	/// <summary>
	/// Called when secondary confirm button is released. Triggers Hold-to-Use activation (left hand).
	/// </summary>
	static void OnSecondaryConfirmUp();

	/// <summary>
	/// Get the current activation timing (for indicator rendering).
	/// Returns hoverTime for RTU mode, or confirmHoldSeconds for Hold-to-Use mode.
	/// </summary>
	static float GetActivationTiming();

	/// <summary>
	/// Returns true if any confirm button is currently held on a valid entry.
	/// </summary>
	static bool IsConfirmHeld();

	/// <summary>
	/// Latch a Left-hand override for the currently hovered entry.
	/// Called when RMB is pressed while wheel is open.
	/// </summary>
	static void LatchHandOverrideLeft();

	/// <summary>
	/// Returns the currently hovered entry index for the active wheel, or -1 if none.
	/// </summary>
	static int GetCurrentHoveredEntryIndex();

	/// <summary>
	/// Cancel the active InstantSpell attempt (MMB action).
	/// Only cancels progress/cast - no equip, no hand override change.
	/// </summary>
	static void CancelInstantSpell();

	/// <summary>
	/// Resolve which hand to use for equip/unequip.
	/// Decoupled from RTU - works for both RTU ON and OFF.
	/// </summary>
	enum class TargetHand { Right, Left, Both };
	enum class ReleaseAction { None, Equip, CastSpell, CastShout };
	static bool QueueSpellActivation(RE::FormID a_spellFormID, TargetHand a_hand, float a_concentrationHoldSeconds = 0.0f);
	static TargetHand ResolveTargetHand(int entryIdx);
	static TargetHand ResolveTargetHandRTU(int entryIdx, const std::shared_ptr<WheelItem>& hoveredItem, ReleaseAction resolvedAction, bool a_logDecision = true);

	/// <summary>
	/// Get current InstantSpell state for UI rendering.
	/// Returns false if no active attempt.
	/// </summary>
	static bool GetInstantSpellState(int& outEntryIdx, float& outElapsed, float& outThreshold, bool& outReady, bool& outCancelled, TargetHand& outHand);

	static void ActivateHoveredEntrySpecial();

	// ========== Ammo Wheel ==========
	/// <summary>
	/// Toggle the ammo wheel open/closed. Only works if ranged weapon is equipped.
	/// </summary>
	static void ToggleAmmoWheel();

	/// <summary>
	/// Close the ammo wheel if it's been open long enough.
	/// </summary>
	static void CloseAmmoWheelIfOpenedLongEnough();

	/// <summary>
	/// Activate the hovered ammo in the ammo wheel.
	/// </summary>
	static void ActivateAmmoWheelHovered();

	/// <summary>
	/// Returns true if the ammo wheel is currently open.
	/// </summary>
	static bool IsAmmoWheelOpen();

	/// <summary>
	/// Update ammo wheel cursor position from mouse input.
	/// </summary>
	static void UpdateAmmoWheelCursorPosMouse(float a_deltaX, float a_deltaY);

	/// <summary>
	/// Update ammo wheel cursor position from gamepad input.
	/// </summary>
	static void UpdateAmmoWheelCursorPosGamepad(float a_x, float a_y);

	/// <summary>
	/// Handle mouse button input for the ammo wheel's fixed-open mode.
	/// Returns true if input was consumed.
	/// </summary>
	static bool HandleAmmoWheelMouseButton(int button, bool pressed, bool fromGamepad = false);

	/// <summary>
	/// Notify AmmoWheel that config has changed (called after dMenu saves settings).
	/// </summary>
	static void NotifyAmmoWheelConfigChanged();

	/// <summary>
	/// Rebuild AmmoWheel entry list when sort/filter settings changed while wheel is open.
	/// </summary>
	static void RefreshAmmoWheelListIfOpen();

	/// <summary>
	/// Push an empty entry to the current wheel.
	/// </summary>
	static void AddEmptyEntryToCurrentWheel();

	/// <summary>
	/// Add a new empty wheel to the set of wheels.
	/// Wheel is added only if the user is in edit mode.
	/// </summary>
	static void AddWheel();
	
	/// <summary>
	/// Push a new empty wheel to the set of wheels.
	/// Doesn't check for edit mode.
	/// </summary>
	static void PushWheel();

	/// <summary>
	/// Delete the current wheel. The deletion may be performed if and only if the current wheel is empty, and the current wheel is not the last wheel present.
	/// The caller is responsible for checking the wheel's emptiness.
	/// </summary>
	static void DeleteCurrentWheel();

	/// <summary>
	/// Move the currently active entry forward by one in the current wheel. Only available in edit mode.
	/// </summary>
	static void MoveEntryForwardInCurrentWheel();
	/// <summary>
	/// Move the currently active entry backward by one in the current wheel. Only available in edit mode.
	/// </summary>
	static void MoveEntryBackInCurrentWheel();
	
	/// <summary>
	/// Move the currently active wheel forward by one in the set of wheels. Only available in edit mode.
	/// </summary>
	static void MoveWheelForward();
	/// <summary>
	/// Move the currently active wheel backward by one in the set of wheels. Only available in edit mode.
	/// </summary>
	static void MoveWheelBack();
	
	static int GetActiveWheelIndex();
	static void SetActiveWheelIndex(int a_index);
	

	static bool IsWheelerOpen();
	static bool IsInEditMode();

	static void SerializeFromJsonObj(const nlohmann::json& a_json, SKSE::SerializationInterface* a_intfc);
	static void SerializeIntoJsonObj(nlohmann::json& a_json);

	/// <summary>
	/// Set up 2 wheels, each with 4 empty slots.
	/// Used to create a template when a user starts a new game.
	/// </summary>
	static void SetupDefaultWheels();

	// ============================================================================
	// External API Accessors
	// These methods provide access to internal state for the WheelerAPI.
	// They do NOT acquire locks - callers must handle synchronization.
	// ============================================================================

	/// <summary>
	/// Get direct access to the wheels vector for API use.
	/// Caller must hold appropriate lock (_wheelDataLock).
	/// </summary>
	static std::vector<std::unique_ptr<Wheel>>& GetWheels() { return _wheels; }

	/// <summary>
	/// Get reference to the wheel data lock for API synchronization.
	/// </summary>
	static std::shared_mutex& GetWheelDataLock() { return _wheelDataLock; }

	/// <summary>
	/// Get a wheel by index. Returns nullptr if out of range.
	/// Caller must hold appropriate lock.
	/// </summary>
	static Wheel* GetWheelByIndex(int a_index);

	/// <summary>
	/// Get the total number of wheels.
	/// </summary>
	static int GetWheelCount() { return static_cast<int>(_wheels.size()); }

private:
	enum class WheelState
	{
		KOpened,
		KClosed,
		KOpening,
		KClosing
	};
	enum class LastInputDevice : std::uint8_t
	{
		None,
		MKB,
		Gamepad
	};

	struct PendingMiscItemUse
	{
		RE::FormID formID = 0;
		std::uint16_t uniqueID = 0;
	};

	struct PendingSpellActivation
	{
		RestorationLifecycle::Epoch restorationEpoch = 0;
		RE::FormID formID = 0;
		TargetHand hand = TargetHand::Right;
		TargetHand requestedHand = TargetHand::Right;
		float concentrationHoldSeconds = 0.0f;
		std::uint8_t readyRetryFramesRemaining = 0;
		std::uint8_t postEquipWarmupFramesRemaining = 0;
		bool requiredEquipBeforeCast = false;
		bool preCastHandsCaptured = false;
		RE::FormID preCastLeftFormID = 0;
		RE::FormID preCastRightFormID = 0;
		LegacyWeaponRestoreToken preCastLeftWeapon;
		LegacyWeaponRestoreToken preCastRightWeapon;
		bool restoreOverrideLeftHand = false;
		RE::FormID restoreOverrideLeftFormID = 0;
		LegacyWeaponRestoreToken restoreOverrideLeftWeapon;
		bool restoreOverrideRightHand = false;
		RE::FormID restoreOverrideRightFormID = 0;
		LegacyWeaponRestoreToken restoreOverrideRightWeapon;
		bool preCastHadTwoHandedWeapon = false;
		// Single-hand direct-cast helper:
		// when both hands already have the same spell, temporarily clear opposite hand
		// to avoid dual-equip input latch and restore it post-cast.
		bool singleHandIsolationApplied = false;
		bool singleHandIsolationOppositeWasLeft = false;
		RE::FormID singleHandIsolationRestoreFormID = 0;
		LegacyWeaponRestoreToken singleHandIsolationRestoreWeapon;
	};

	struct EditModeGameplayInputBlocker
	{
		bool active = false;
		std::uint32_t disabledByUsMask = 0;
	};

	struct EditModeHiddenMenuTracker
	{
		bool inventory = false;
		bool magic = false;
		bool favorites = false;

		[[nodiscard]] bool Any() const noexcept
		{
			return inventory || magic || favorites;
		}

		void Clear() noexcept
		{
			inventory = false;
			magic = false;
			favorites = false;
		}
	};

	static void ExecuteScriptedMiscActivation(RE::PlayerCharacter* pc,
		RE::TESObjectMISC* miscItem, RE::ExtraDataList* extraList, std::uint16_t uniqueID);
	static PreparedWheelItemActivation PrepareHoveredWheelItemActivation(
		WheelItemActivationKind a_kind,
		std::optional<std::int32_t> a_expectedEntryIndex = std::nullopt);
	static bool ExecutePreparedWheelItemActivation(PreparedWheelItemActivation a_activation);
	static void ArmSpellHoldRelease(
		RE::INPUT_DEVICE a_device,
		std::uint32_t a_idCode,
		bool a_useLeftAttack,
		float a_holdDuration,
		bool a_waitForCasterStart = false,
		RE::FormID a_spellFormID = 0,
		RE::MagicSystem::CastingSource a_castingSource = RE::MagicSystem::CastingSource::kRightHand,
		bool a_chargeAwareRelease = false,
		float a_chargeAwareMaxTotalSeconds = 0.0f,
		bool a_releaseSecondAttack = false,
		bool a_secondUseLeftAttack = false,
		RE::INPUT_DEVICE a_secondDevice = RE::INPUT_DEVICE::kKeyboard,
		std::uint32_t a_secondIdCode = 0,
		bool a_restoreLeftHand = false,
		RE::FormID a_restoreLeftFormID = 0,
		LegacyWeaponRestoreToken a_restoreLeftWeapon = {},
		bool a_restoreRightHand = false,
		RE::FormID a_restoreRightFormID = 0,
		LegacyWeaponRestoreToken a_restoreRightWeapon = {},
		bool a_enableStartAssistTap = false,
		bool a_startAssistUseLeftAttack = false,
		RE::INPUT_DEVICE a_startAssistDevice = RE::INPUT_DEVICE::kKeyboard,
		std::uint32_t a_startAssistIdCode = 0,
		bool a_releaseSecondOnCasterStart = false,
		bool a_requirePrimaryCasterStart = false);
	static void ClearSpellHoldRelease();
	static void CancelSpellPipelineForEditMode();
	static void CancelPendingActionsForLoad(const char* a_reason);
	static void ForceCloseForLoad(const char* a_reason);
	static void QueuePostCastRestore(
		RE::FormID a_spellFormID,
		RE::MagicSystem::CastingSource a_preferredSource,
		bool a_trackPrimaryLeft,
		bool a_trackSecondary,
		bool a_trackSecondaryLeft,
		bool a_restoreLeftHand,
		RE::FormID a_restoreLeftFormID,
		LegacyWeaponRestoreToken a_restoreLeftWeapon,
		bool a_restoreRightHand,
		RE::FormID a_restoreRightFormID,
		LegacyWeaponRestoreToken a_restoreRightWeapon,
		float a_minDelaySeconds,
		float a_maxWaitSeconds);
	static void CancelTransientGameplayStateInCurrentWorld(const char* a_reason);
	static void CancelOwnedSyntheticInputInCurrentWorld();
	static void RollbackPendingSpellTransactionInCurrentWorld();
	static void RollbackPostCastTransactionInCurrentWorld();
	static void RollbackHandMemoryInCurrentWorld();
	static void RollbackTemporaryPowerSelectionInCurrentWorld();
	static void ProcessRequestedCurrentWorldWheelReset();
	static void ClearWheelData();
	static void PopulateDefaultWheels();
	static void ClearPostCastRestore();
	static void QueueShoutPostCastRestore(RE::FormID a_shoutFormID);
	static void TryRestoreQueuedShoutSelection(const char* a_reason);
	static void ArmDelayedShoutPostCastRestore(const char* a_reason);
	static void ClearShoutPostCastRestore();

	static void EnableEditModeGameplayInputBlock();
	static void DisableEditModeGameplayInputBlock();
	static void RestoreTrackedEditModeVanillaMenus(RE::UI* a_ui, const char* a_reason);
	static inline WheelState _state = WheelState::KClosed;
	static inline LastInputDevice _lastInputDevice = LastInputDevice::None;
	
	static inline bool _editMode = false;
	static inline bool _editModeHintsVisible = true;
	static inline EditModeGameplayInputBlocker _editModeGameplayInputBlocker;
	static inline EditModeHiddenMenuTracker _editModeHiddenMenus;

	static inline const char* _wheelWindowID = "##Wheeler";

	
	static inline ImVec2 _cursorPos = { 0, 0 };
	static inline ImVec2 _gamepadSmoothed = { 0.0f, 0.0f };
	static inline bool _gamepadSmoothedInitialized = false;
	static inline double _gamepadLastUpdateTime = 0.0;
	static inline float _gamepadFilteredMagnitude = 0.0f;
	static inline bool _gamepadInDeadzone = false;
	static inline bool _gamepadIntentActive = true;
	static inline bool _gamepadOuterClampActive = false;
	static inline double _gamepadOpenGraceUntil = 0.0;

	static ImVec2 getWheelCenter();
	
	static inline std::vector<std::unique_ptr<Wheel>> _wheels;
	static inline int _activeWheelIdx = 0;

	


	static inline float _openTimer = 0;
	static inline float _closeTimer = 0;

	static inline std::shared_mutex _wheelDataLock;  // global lock

	// prevent multiple activate-on-close firings during a single close
	static inline bool _activateOnCloseFired = false;
	// Suppress RTU close activation after any direct click activation in this open session
	static inline bool _directActivatedThisOpenSession = false;
	// If the player opens Wheeler before deferred post-load uniqueID repair completes,
	// defer the first inventory prune until the repair has finished and the wheel is closed.
	static inline bool _deferredPostLoadInventoryPrune = false;

	static inline int _lastHoveredEntry = -1;
	static inline float _hoveredEntryTime = 0.f;
	static inline std::vector<int> _lastHoveredEntryByWheel;

	// Hold-to-Use timing: tracks confirm button hold duration on hovered entry
	static inline bool _confirmHeld = false;
	static inline float _confirmHoldStartTime = 0.f;
	static inline float _confirmHoldSeconds = 0.f;
	static inline int _confirmHoldEntryIdx = -1;  // entry index when confirm was pressed
	static inline bool _secondaryConfirmHeld = false;
	static inline float _secondaryConfirmHoldStartTime = 0.f;
	static inline float _secondaryConfirmHoldSeconds = 0.f;
	static inline int _secondaryConfirmHoldEntryIdx = -1;  // entry index when secondary confirm was pressed
	static inline bool _secondaryImmediateConsumedUntilRelease = false;

	// RTU Anti-Slip: prevents accidental slot switching when canceling
	static inline double _antiSlipLockUntil = 0.0;  // lockout timer (ImGui::GetTime() based)
	static inline int _antiSlipPendingIdx = -1;     // candidate slot during dwell
	static inline double _antiSlipPendingSince = 0.0;  // when candidate was first seen

	static inline std::optional<RE::FormID> _pendingPoisonApplyFormID = std::nullopt;
	static inline std::optional<PendingMiscItemUse> _pendingMiscItemUse = std::nullopt;
	static inline double _lastMiscDispatchTime = 0.0;
	static inline RE::FormID _lastMiscDispatchFormID = 0;
	static inline std::uint16_t _lastMiscDispatchUniqueID = 0;
	static inline std::optional<RE::FormID> _pendingSGTInstrumentSpellFormID = std::nullopt;
	static inline std::optional<RE::FormID> _pendingShoutFormID = std::nullopt;
	static inline RestorationLifecycle::SerializedEpochDomain _transientGameplayDomain{};
	static inline RestorationLifecycle::DeferredIntentLedger _transientIntentLedger{};
	static inline std::atomic_bool _currentWorldResetAllWheelsRequested{ false };
	static inline std::optional<PendingSpellActivation> _pendingSpellActivation = std::nullopt;
	static inline std::optional<RE::FormID> _pendingPowerFormID = std::nullopt;
	static inline bool _pendingPowerRestorePending = false;
	static inline RE::FormID _pendingPowerRestoreFormID = 0;
	struct TemporaryPowerSelectionOwnership
	{
		bool active = false;
		RestorationLifecycle::Epoch epoch = 0;
		RE::FormID temporaryFormID = 0;
		RE::FormID restoreFormID = 0;
	};
	static inline TemporaryPowerSelectionOwnership _temporaryPowerSelection{};
	static inline std::optional<RE::FormID> _pendingBookReadFormID = std::nullopt;
	static inline std::optional<float> _pendingShoutHoverTime = std::nullopt;

	struct PendingExternalHotkeyDispatch
	{
		std::uint32_t scanCode = 0;
		std::uint32_t modifier = 0;
		std::string displayName;
		std::uint32_t sourceSlotIndex = 0;
		std::string sourceTag;
		double queuedAt = 0.0;
	};
	static inline std::optional<PendingExternalHotkeyDispatch> _pendingExternalHotkeyDispatch = std::nullopt;
	static inline double _lastExternalHotkeyDispatchTime = 0.0;
	static inline std::uint32_t _lastExternalHotkeyDispatchScanCode = 0;
	static inline std::uint32_t _lastExternalHotkeyDispatchModifier = 0;
	struct ActionHotkeysBridgeCloseAssistState
	{
		bool active = false;
		bool awaitingEscOutcome = false;
		bool escPhysicalWasDown = false;
		std::uint32_t scanCode = 0;
		std::uint32_t modifier = 0;
		double armedAt = 0.0;
		double expiresAt = 0.0;
		double evaluateAt = 0.0;
		std::unordered_set<std::string> baselineMenus;
		std::unordered_set<std::string> trackedMenusAtEsc;
	};
	static inline ActionHotkeysBridgeCloseAssistState _actionHotkeysBridgeCloseAssist{};
	
	// Shout hold state machine for proper input simulation
	// Send the DOWN event and start the timer.
	// Send the UP event after the hold duration elapses.
	static inline bool _shoutHoldActive = false;
	static inline float _shoutHoldStartTime = 0.0f;
	static inline float _shoutHoldDuration = 0.0f;
	static inline int _shoutHoldWordStage = 1;  // 1, 2, or 3
	static inline RE::FormID _shoutHoldFormID = 0;
	static inline bool _shoutPostCastRestorePending = false;
	static inline RE::FormID _shoutPostCastRestoreFormID = 0;
	static inline RE::FormID _shoutPostCastRestoreExpectedFormID = 0;
	static inline bool _shoutPostCastRestoreDelayActive = false;
	static inline float _shoutPostCastRestoreDelayElapsedSec = 0.0f;
	// Cached binding info for the shout key
	static inline RE::INPUT_DEVICE _shoutBindDevice = RE::INPUT_DEVICE::kKeyboard;
	static inline std::uint32_t _shoutBindIdCode = 0;

	// Spell hold state for vanilla direct-cast concentration spells (attack down -> delayed attack up).
	static inline bool _spellHoldActive = false;
	static inline RestorationLifecycle::Epoch _spellHoldRestorationEpoch = 0;
	static inline double _spellHoldStartTime = 0.0;
	static inline double _spellHoldWaitStartTime = 0.0;
	static inline float _spellHoldDuration = 0.0f;
	static inline bool _spellHoldUseLeftAttack = false;
	static inline bool _spellHoldWaitForCasterStart = false;
	static inline RE::FormID _spellHoldSpellFormID = 0;
	static inline RE::MagicSystem::CastingSource _spellHoldCastingSource = RE::MagicSystem::CastingSource::kRightHand;
	static inline bool _spellHoldChargeAwareRelease = false;
	static inline float _spellHoldChargeAwareMaxTotalSeconds = 0.0f;
	static inline double _spellHoldChargeAwareLastLogTime = 0.0;
	static inline bool _spellHoldAlsoReleaseSecondAttack = false;
	static inline bool _spellHoldSecondUseLeftAttack = false;
	static inline std::uint8_t _spellHoldStartRetryCount = 0;
	static inline double _spellHoldLastRetryPulseTime = 0.0;
	static inline bool _spellHoldObservedCasterStart = false;
	static inline bool _spellHoldObservedChargeActivity = false;
	static inline bool _spellHoldUsedFallbackStart = false;
	static inline bool _spellHoldReleaseSecondOnCasterStart = false;
	static inline bool _spellHoldRequirePrimaryCasterStart = false;
	static inline RE::MagicSystem::CastingSource _spellHoldPrimaryCastingSource = RE::MagicSystem::CastingSource::kRightHand;
	static inline bool _spellHoldStartAssistTapEnabled = false;
	static inline bool _spellHoldStartAssistTapUseLeftAttack = false;
	static inline RE::INPUT_DEVICE _spellHoldStartAssistTapDevice = RE::INPUT_DEVICE::kKeyboard;
	static inline std::uint32_t _spellHoldStartAssistTapIdCode = 0;
	static inline bool _spellHoldStartAssistTapSent = false;
	static inline bool _spellHoldRestoreHandsAfterRelease = false;
	static inline bool _spellHoldRestoreLeftHand = false;
	static inline bool _spellHoldRestoreRightHand = false;
	static inline RE::FormID _spellHoldRestoreLeftFormID = 0;
	static inline RE::FormID _spellHoldRestoreRightFormID = 0;
	static inline LegacyWeaponRestoreToken _spellHoldRestoreLeftWeapon;
	static inline LegacyWeaponRestoreToken _spellHoldRestoreRightWeapon;
	static inline bool _spellPostCastRestorePending = false;
	static inline bool _spellPostCastRestoreLeftHand = false;
	static inline bool _spellPostCastRestoreRightHand = false;
	static inline RE::FormID _spellPostCastRestoreLeftFormID = 0;
	static inline RE::FormID _spellPostCastRestoreRightFormID = 0;
	static inline LegacyWeaponRestoreToken _spellPostCastRestoreLeftWeapon;
	static inline LegacyWeaponRestoreToken _spellPostCastRestoreRightWeapon;
	static inline RE::FormID _spellPostCastRestoreSpellFormID = 0;
	static inline RE::MagicSystem::CastingSource _spellPostCastRestorePreferredSource = RE::MagicSystem::CastingSource::kRightHand;
	static inline bool _spellPostCastRestoreTrackPrimaryLeft = false;
	static inline bool _spellPostCastRestoreTrackSecondary = false;
	static inline bool _spellPostCastRestoreTrackSecondaryLeft = false;
	static inline RestorationLifecycle::PostCastContext _spellPostCastRestoreContext{};
	static inline double _spellPostCastRestoreNoEarlierThan = 0.0;
	static inline double _spellPostCastRestoreForceAt = 0.0;
	static inline double _spellPostCastRestoreLastWaitLogTime = 0.0;
	static inline RE::FormID _spellPostCastRestoreTrackedLeftOccupantFormID = 0;
	static inline RE::FormID _spellPostCastRestoreTrackedRightOccupantFormID = 0;
	static inline RE::INPUT_DEVICE _spellBindDeviceSecond = RE::INPUT_DEVICE::kKeyboard;
	static inline std::uint32_t _spellBindIdCodeSecond = 0;
	static inline RE::INPUT_DEVICE _spellBindDevice = RE::INPUT_DEVICE::kKeyboard;
	static inline std::uint32_t _spellBindIdCode = 0;
	
	static inline bool _forceCloseRequested = false;
	static inline bool _pendingDepletedConsumablesCleanup = false;
	// Independent ingress from entry-locked draw/activation paths. Update promotes
	// only a request from the current lifecycle epoch while holding the domain.
	static inline std::mutex _depletedConsumablesCleanupRequestLock;
	static inline RestorationLifecycle::Epoch _requestedDepletedConsumablesCleanupEpoch = 0;
	static inline std::array<bool, 3> _lootMenusMovieHiddenForOverride{ false, false, false };
	static inline std::array<bool, 3> _lootMenusClosedForOverride{ false, false, false };
	static inline bool _lootMenuRestorePending = false;

	// Track whether Wheeler modified the timescale (to avoid overriding external effects like Slow Time shout)
	static inline bool _wheelerModifiedTimeScale = false;
	static inline float _preWheelerTimeScale = 1.0f;
	static inline bool _wheelerRestoreMountedVelocityOnClose = false;
	static inline RE::FormID _wheelerMountedVelocityMountFormID = 0;
	static inline RE::NiPoint3 _wheelerMountedVelocitySnapshot = { 0.0f, 0.0f, 0.0f };
	static inline double _wheelerMountedMomentumAssistUntil = 0.0;
	// Track whether Wheeler opened the pause menu (kPausesGame) for vanilla pause mode
	static inline bool _wheelerOwnedPauseMenu = false;

	// Track display size for resolution change detection
	static inline ImVec2 _lastDisplaySize = { 0.f, 0.f };

	// Suppress auto-reopen (e.g. after reading a book)
	static inline double _suppressWheelOpenUntil = 0.0;
	static inline bool _suppressOpenUntilToggleUp = false;  // Latch: block until toggle key released

	// BookMenu open verification probe
	static inline int _bookMenuProbeFrames = 0;
	static inline RE::FormID _bookMenuProbeFormID = 0;

	// Ammo Wheel instance
	static inline std::unique_ptr<AmmoWheel> _ammoWheel = nullptr;

	// Shout stage sound tracking
	static inline bool _shoutStageSoundFired1 = false;
	static inline bool _shoutStageSoundFired2 = false;
	static inline bool _shoutStageSoundFired3 = false;
	static inline RE::FormID _shoutStageSoundLastShoutID = 0;
	static inline bool _shoutStageSoundForced1 = false;  // Was stage 1 forced-suppressed?
	static inline bool _shoutStageSoundForced2 = false;  // Was stage 2 forced-suppressed?
	static inline bool _shoutStageSoundForced3 = false;  // Was stage 3 forced-suppressed?
	static inline int _shoutStageSoundLastUnlockedWords = -1;  // Track unlock changes

	struct ConcentrationStopRequest
	{
		bool pending = false;
		RE::FormID spellFormID = 0;
		RE::MagicSystem::CastingSource castingSource = RE::MagicSystem::CastingSource::kRightHand;
		double stopAtTime = 0.0;  // ImGui::GetTime() based
		float scheduledDurationSeconds = 0.0f;
	};

	// Concentration spell timed-stop state machine (InstantSpell + Timed mode)
	// Keep one request per hand so dual-hand timed instant casts do not overwrite each other.
	static inline ConcentrationStopRequest _concentrationStopLeft{};
	static inline ConcentrationStopRequest _concentrationStopRight{};
	static inline bool _concentrationStopPending = false;
	static inline RestorationLifecycle::Epoch _concentrationStopEpoch = 0;
	static inline RE::FormID _concentrationStopSpellFormID = 0;
	static inline RE::MagicSystem::CastingSource _concentrationStopCastingSource = RE::MagicSystem::CastingSource::kRightHand;
	static inline double _concentrationStopAtTime = 0.0;

	// Instant cast summon refund check (deferred validation for placement failures)
	struct InstantCastRefundCheck {
		RE::FormID spellFormID = 0;
		float magickaBefore = 0.0f;
		int effectCountBefore = 0;  // Active effect count before cast
		double queuedTime = 0.0;
		int attemptsRemaining = 3;
		float delayMs = 150.0f;
	};
	static inline std::optional<InstantCastRefundCheck> _instantCastRefundCheck = std::nullopt;
	static inline RestorationLifecycle::Epoch _instantCastRefundEpoch = 0;

	// RTU Hand Override: RMB latches Left-hand equip for selected entry
	static inline bool _handOverrideActive = false;
	static inline int _handOverrideEntryIdx = -1;
	static inline bool _handOverrideLeft = false;  // true = Left, false = Right
	// Tracks which entry/hand was applied by RTU this open cycle (prevents snap-back on close)
	static inline bool _rtuAppliedThisOpen = false;
	static inline int _rtuAppliedEntryIdx = -1;
	static inline bool _rtuAppliedLeft = false;
	static inline bool _rtuConsumedByInstant = false;  // instant cast occurred - skip equip on close

	// Timed Instant Cast attempt state (countdown UI + release-to-cast)
	static inline int _instantEntryIdx = -1;
	static inline bool _instantAttemptActive = false;     // countdown visible/attempt running
	static inline bool _instantReady = false;             // reached threshold - Ready state
	static inline bool _instantCancelled = false;         // cancelled for current attempt
	static inline bool _instantSuppressForEntry = false;  // once cancelled, do not re-arm until entry changes
	static inline float _instantElapsedSec = 0.0f;        // current attempt elapsed time
	static inline bool _instantRtuSource = false;         // true = RTU hover, false = Hold-to-Use
	static inline TargetHand _instantTargetHand = TargetHand::Right;
	
	// State-change logging (avoids per-frame log spam)
	static inline int _instantLastLoggedEntry = -1;
	static inline bool _instantLastLoggedReady = false;
	static inline int _instantLastLoggedProgressBucket = -1;  // 0.25s buckets
	static inline int _lastLoggedGamepadHoveredIdx = -2;
	static inline bool _lastLoggedGamepadDeadzone = false;
	static inline bool _lastLoggedGamepadIntentActive = true;
	static inline bool _lastLoggedGamepadOuterClampActive = false;
	static inline WheelState _lastLoggedWheelState = WheelState::KClosed;

	struct MainWheelInputEvent
	{
		bool valid = false;
		InputAction action = InputAction::None;
		RE::INPUT_DEVICE device = RE::INPUT_DEVICE::kKeyboard;
		std::uint32_t rawCode = 0;
		std::uint32_t mappedCode = 0;
		bool isDown = false;
		bool isUp = false;
		std::uint32_t sequence = 0;
		double timestamp = 0.0;
	};

	struct MainWheelInputState
	{
		bool isDown = false;
		double lastDownTime = 0.0;
		std::uint32_t lastEdgeSequence = 0;
	};

	static inline MainWheelInputEvent _mainWheelLastInput{};
	static inline MainWheelInputState _mainWheelInputState{};
	static inline std::uint32_t _mainWheelInputSequence = 0;
	static inline std::uint32_t _mainWheelLastLoggedSequence = 0;
	static inline InputDecision _lastOpenDecision = InputDecision::None;
	static inline InputConsumer _lastOpenConsumer = InputConsumer::None;
	static inline InputDecision _pendingCloseDecision = InputDecision::None;

	static bool TryActivateHoveredEntryRTU(bool logDelaySkip);
	static void ProcessPendingActions();
	static void ProcessQueuedExternalHotkeys();
	static bool BeginActionHotkeysBridgeCloseAssist(bool a_triggeredByGamepad);
	static void UpdateActionHotkeysBridgeCloseAssist();
	static void ResetActionHotkeysBridgeCloseAssist();
	static void UpdateLastInputDevice(LastInputDevice device);
	static const char* GetLastInputDeviceName(LastInputDevice device);
	static const char* GetWheelStateName(WheelState state);

	// Whether the wheel should enter edit mode. Edit mode toggles whenever a game inventory UI opens up.
	static bool shouldBeInEditMode(RE::UI* a_ui);
	
	static void hideEditModeVanillaMenus(RE::UI* a_ui);
	static void showEditModeVanillaMenus(RE::UI* a_ui);

	static void enterEditMode();
	static void exitEditMode();

	// Clear/revert can leave edit actions with a stale active index while wheel list is rebuilt.
	// These helpers do not acquire _wheelDataLock; callers keep the existing synchronization model.
	static bool HasValidActiveWheel_NoLock();
	static bool EnsureValidActiveWheelForEdit_NoLock(std::optional<int> a_preferredIndex = std::nullopt,
		const char* a_context = nullptr);

	static float getCursorRadiusMax();
};
