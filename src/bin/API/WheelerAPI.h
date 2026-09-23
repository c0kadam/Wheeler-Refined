#pragma once

// ============================================================================
// Wheeler External API
// ============================================================================
// This header provides an interface for external SKSE plugins to interact
// with Wheeler. All functions are thread-safe and can be called from any thread.
//
// Client usage:
//   #define WHEELER_API
//   #include "WheelerAPI.h"
//   auto api = GetWheelerAPI();
//   if (api && api->version >= WheelerAPI::API_VERSION) { ... }
//
// This API is optional and does not affect Wheeler's core functionality.
// If no client connects, Wheeler operates exactly as before.
// ============================================================================

#include <cstdint>
#include <string>

#ifndef WHEELER_API
#	ifdef WHEELER_EXPORTS
#		define WHEELER_API __declspec(dllexport)
#	else
#		define WHEELER_API __declspec(dllimport)
#	endif
#endif

namespace WheelerAPI
{
	// API version - bump on breaking changes
	constexpr uint32_t API_VERSION = 2;
	constexpr uint32_t INPUT_BROKER_API_VERSION = 1;
	constexpr uint32_t COOPERATIVE_OPENING_API_VERSION = 1;
	constexpr uint32_t COOPERATIVE_OPENING_BINDING_SET_VERSION = 1;
	constexpr uint32_t COOPERATIVE_OPENING_BINDING_VERSION = 1;
	constexpr uint32_t COOPERATIVE_OPENING_EVENT_VERSION = 1;

	enum class InputBrokerDevice : uint32_t
	{
		kMKB = 0,
		kGamepad = 1
	};

	namespace InputBrokerReservationFlag
	{
		inline constexpr uint32_t None = 0;
		inline constexpr uint32_t ToggleKey = 1u << 0;
		inline constexpr uint32_t NavigationKey = 1u << 1;
		inline constexpr uint32_t CategoryKey = 1u << 2;
		inline constexpr uint32_t QTakeover = 1u << 3;
	}

	namespace InputBrokerContextFlag
	{
		inline constexpr uint32_t None = 0;
		inline constexpr uint32_t IsDown = 1u << 0;
		inline constexpr uint32_t IsUp = 1u << 1;
		inline constexpr uint32_t MainWheelOpen = 1u << 2;
		inline constexpr uint32_t AmmoWheelOpen = 1u << 3;
	}

	// ============================================================================
	// Result Codes
	// ============================================================================

	enum class Result : int32_t
	{
		OK = 0,
		InvalidWheelIndex = -1,
		InvalidEntryIndex = -2,
		InvalidItemIndex = -3,
		InvalidFormID = -4,
		FormNotFound = -5,
		UnsupportedFormType = -6,
		WheelNotEmpty = -7,
		LastWheel = -8,
		NotInitialized = -9,
		NotManagedWheel = -10,
		InEditMode = -11,
		EntryNotEmpty = -12,
		InvalidArgument = -13,
		InternalError = -100
	};

	// ============================================================================
	// Configuration Structs
	// ============================================================================

	struct WheelConfig
	{
		int32_t numEntries;      // Number of empty entries to create
		int32_t position;        // Position in wheel list (-1 = append)
		bool managed;            // If true, wheel is not saved to user config
		const char* clientName;  // Name of the client managing this wheel (for display)
		bool showLabel;          // If true, show "[Managed By: clientName]" label
	};

	struct ExternalHotkeyConfig
	{
		const char* sourceTag;      // Stable external ID for update/remove/layout persistence
		const char* displayName;    // Slot label shown in Wheeler
		uint32_t scanCode;          // DIK/scancode to dispatch
		uint32_t modifier;          // Optional modifier scancode. For convenience 1/2/3 are also accepted as Alt/Ctrl/Shift.
		const char* iconPath;       // Optional absolute or relative icon path
		uint32_t iconTintARGB;      // Optional tint
		int32_t wheelNumber;        // 1-based target wheel; 0 = auto
		int32_t entryIndex;         // 0-based target slot; -1 = auto
		uint32_t flags;             // Reserved for future use
	};

	// ============================================================================
	// Change Tracking (for edit mode callbacks)
	// ============================================================================

	enum class ChangeType : int32_t
	{
		ItemAdded,
		ItemRemoved,
		EntryAdded,
		EntryRemoved,
		ItemMoved
	};

	struct WheelChange
	{
		ChangeType type;
		int32_t wheelIndex;
		int32_t entryIndex;
		int32_t itemIndex;
		uint32_t formID;
	};

	// ============================================================================
	// Callback Types
	// ============================================================================

	// Called when user activates an item
	using ItemActivatedCallback = void (*)(
		int32_t wheelIndex,
		int32_t entryIndex,
		int32_t itemIndex,
		uint32_t formID,
		bool isPrimary);

	// Called when edit mode is entered/exited
	// On exit: changes array contains all modifications made during edit session
	using EditModeCallback = void (*)(
		bool entered,
		const WheelChange* changes,
		size_t changeCount);

	// Called when wheel opens or closes
	// wheelIndex: the active wheel index at the time of the event
	using WheelStateCallback = void (*)(int32_t wheelIndex, bool isOpen);

	// ============================================================================
	// API Interface Struct
	// ============================================================================

	struct IWheelerAPI
	{
		uint32_t version;  // API_VERSION

		// --- Status ---
		bool (*IsInitialized)();
		bool (*IsInEditMode)();
		bool (*IsWheelOpen)();

		// --- Managed Wheel Lifecycle ---
		// Returns wheel index on success, negative Result on failure
		int32_t (*CreateManagedWheel)(const WheelConfig* config);
		Result (*DeleteManagedWheel)(int32_t wheelIndex);
		bool (*IsManagedWheel)(int32_t wheelIndex);

		// --- Wheel Queries ---
		int32_t (*GetWheelCount)();
		int32_t (*GetActiveWheelIndex)();
		Result (*SetActiveWheelIndex)(int32_t index);
		bool (*IsWheelEmpty)(int32_t wheelIndex);

		// --- Entry Management ---
		int32_t (*GetEntryCount)(int32_t wheelIndex);
		// Returns entry index on success, negative Result on failure
		int32_t (*AddEntry)(int32_t wheelIndex);
		Result (*DeleteEntry)(int32_t wheelIndex, int32_t entryIndex);
		bool (*IsEntryEmpty)(int32_t wheelIndex, int32_t entryIndex);

		// --- Item Management ---
		int32_t (*GetItemCount)(int32_t wheelIndex, int32_t entryIndex);
		// Returns item index on success, negative Result on failure
		int32_t (*AddItemByFormID)(int32_t wheelIndex, int32_t entryIndex, uint32_t formID, uint16_t uniqueID);
		Result (*RemoveItem)(int32_t wheelIndex, int32_t entryIndex, int32_t itemIndex);
		Result (*ClearEntry)(int32_t wheelIndex, int32_t entryIndex);
		uint32_t (*GetItemFormID)(int32_t wheelIndex, int32_t entryIndex, int32_t itemIndex);
		int32_t (*GetSelectedItemIndex)(int32_t wheelIndex, int32_t entryIndex);
		Result (*SetSelectedItemIndex)(int32_t wheelIndex, int32_t entryIndex, int32_t itemIndex);

		// --- External Hotkey Bridge ---
		Result (*UpsertExternalHotkey)(const ExternalHotkeyConfig* config);
		Result (*RemoveExternalHotkey)(const char* sourceTag);
		void (*ClearExternalHotkeys)();

		// --- Callbacks ---
		// Pass nullptr to unregister a previously registered callback
		void (*RegisterItemActivatedCallback)(ItemActivatedCallback callback);
		void (*RegisterEditModeCallback)(EditModeCallback callback);
		void (*RegisterWheelStateCallback)(WheelStateCallback callback);

		// --- Unregister Callbacks (convenience) ---
		void (*UnregisterItemActivatedCallback)();
		void (*UnregisterEditModeCallback)();
		void (*UnregisterWheelStateCallback)();
	};

	struct WheelerInputBrokerAPI
	{
		uint32_t apiVersion;  // INPUT_BROKER_API_VERSION
		bool (*RegisterReservation)(uint64_t pluginId, InputBrokerDevice device, uint32_t key, int32_t priority, uint32_t flags);
		void (*UnregisterAll)(uint64_t pluginId);
		void (*SetActiveOwner)(uint64_t pluginId);
		void (*ClearActiveOwner)(uint64_t pluginId);
		uint64_t (*GetActiveOwner)();
		bool (*ShouldProcessKey)(uint64_t pluginIdSelf, InputBrokerDevice device, uint32_t key, uint32_t contextFlags);
	};

	// Separate optional extension. WheelerInputBrokerAPI v1 is frozen and is
	// deliberately not enlarged by cooperative opening support.
	enum class CooperativeOpeningDevice : uint32_t
	{
		kMKB = 0,
		kGamepad = 1
	};

	enum class CooperativeOpeningTriggerEdge : uint32_t
	{
		kUnknown = 0,
		kPrimaryDown = 1,
		kPrimaryUp = 2
	};

	namespace CooperativeOpeningSemanticFlag
	{
		inline constexpr uint32_t kNone = 0;
		inline constexpr uint32_t kConsumeOnGrant = 1u << 0;
	}

	enum class CooperativeOpeningReplaceResult : uint32_t
	{
		kSuccess = 0,
		kInvalidArgument = 1,
		kUnsupportedVersion = 2,
		kInvalidSize = 3,
		kInvalidOwner = 4,
		kInvalidGeneration = 5,
		kStaleGeneration = 6,
		kTooManyBindings = 7,
		kInvalidBinding = 8,
		kDuplicateBinding = 9
	};

	enum class CooperativeOpeningAttestation : uint32_t
	{
		kMatchedForThisOwner = 0,
		kObservedButNotMatched = 1,
		kNoUpstreamAttestationAvailable = 2
	};

	enum class CooperativeOpeningReason : uint32_t
	{
		kNone = 0,
		kMatched = 1,
		kNoProducerFrame = 2,
		kNoGrantForEvent = 3,
		kWrongOwner = 4,
		kWrongDevice = 5,
		kWrongKey = 6,
		kWrongEdge = 7,
		kWrongGeneration = 8,
		kAlreadyClaimed = 9,
		kInvalidScope = 10,
		kUpstreamConsumerAlreadyObserved = 11,
		kGrantRevoked = 12
	};

	struct CooperativeOpeningBinding
	{
		uint32_t structSize;
		uint32_t descriptorVersion;
		CooperativeOpeningDevice device;
		uint32_t primaryMappedKey;
		uint32_t modifierMappedKey;
		CooperativeOpeningTriggerEdge triggerEdge;
		int32_t priority;
		uint32_t semanticFlags;
	};

	struct CooperativeOpeningBindingSet
	{
		uint32_t structSize;
		uint32_t structVersion;
		uint64_t ownerId;
		uint64_t bindingGeneration;
		uint32_t bindingCount;
		uint32_t bindingStride;
		const void* bindings;
	};

	struct CooperativeOpeningEventObservation
	{
		uint32_t structSize;
		uint32_t structVersion;
		uint64_t consumerScopeToken;
		uintptr_t eventIdentity;
		uint64_t ownerId;
		uint64_t expectedBindingGeneration;
		CooperativeOpeningDevice device;
		uint32_t mappedKey;
		CooperativeOpeningTriggerEdge edge;
	};

	struct CooperativeOpeningEventDisposition
	{
		uint32_t structSize;
		uint32_t structVersion;
		CooperativeOpeningAttestation attestation;
		CooperativeOpeningReason reason;
		uint64_t dispatchGeneration;
		uint64_t bindingGeneration;
		uint64_t opaqueGrantToken;
		uint32_t mustSuppressDownstream;
		uint32_t reserved;
	};

	struct CooperativeOpeningAPI
	{
		uint32_t apiVersion;
		uint32_t structSize;
		CooperativeOpeningReplaceResult (*ReplaceCooperativeOpeningBindings)(const CooperativeOpeningBindingSet* bindingSet);
		uint64_t (*BeginCooperativeOpeningConsumerScope)(uint64_t ownerId, uint64_t expectedBindingGeneration);
		CooperativeOpeningAttestation (*ObserveAndClaimCooperativeOpeningEvent)(
			const CooperativeOpeningEventObservation* observation,
			CooperativeOpeningEventDisposition* disposition);
		CooperativeOpeningAttestation (*GetCooperativeOpeningEventDisposition)(
			uint64_t consumerScopeToken,
			uintptr_t eventIdentity,
			uint64_t ownerId,
			CooperativeOpeningEventDisposition* disposition);
		void (*EndCooperativeOpeningConsumerScope)(uint64_t consumerScopeToken);
	};

	// ============================================================================
	// Internal Functions (Wheeler server only)
	// ============================================================================

	// Set initialization state (called by Wheeler::Init)
	void SetInitialized(bool initialized);

	// Notification functions - called by Wheeler to notify registered callbacks
	void NotifyItemActivated(int32_t wheelIndex, int32_t entryIndex, int32_t itemIndex, uint32_t formID, bool isPrimary);
	void NotifyEditModeChanged(bool entered, const WheelChange* changes, size_t changeCount);
	void NotifyWheelStateChanged(int32_t wheelIndex, bool isOpen);

	// Clear all managed wheel tracking (called during deserialization when wheel list is replaced)
	// Clients should use IsManagedWheel() to detect when their wheels are invalidated
	void ClearManagedWheels();

	// Delete a wheel by runtime index (internal use).
	Result DeleteWheelIndex(int32_t wheelIndex);

	// Check if a wheel is managed (for serialization exclusion)
	bool IsManagedWheelIndex(int32_t wheelIndex);

	// Get the client name for a managed wheel (returns nullptr if not managed)
	// WARNING: Returned pointer is only valid momentarily - copy immediately if storing
	const char* GetManagedWheelClientName(int32_t wheelIndex);

	// Get the client name as a safe copy (returns empty string if not managed)
	// Use this when you need to store/log the name safely
	std::string GetManagedWheelClientNameSafe(int32_t wheelIndex);

	// Check if managed wheel label should be shown
	bool ShouldShowManagedWheelLabel(int32_t wheelIndex);

}  // namespace WheelerAPI

// ============================================================================
// Main Entry Point
// ============================================================================

// Returns pointer to static IWheelerAPI instance, or nullptr if not available
extern "C" WHEELER_API WheelerAPI::IWheelerAPI* GetWheelerAPI();
extern "C" WHEELER_API const WheelerAPI::WheelerInputBrokerAPI* GetInputBrokerAPI(uint32_t requestedVersion);
extern "C" WHEELER_API const WheelerAPI::CooperativeOpeningAPI* GetCooperativeOpeningAPI(uint32_t requestedVersion);
