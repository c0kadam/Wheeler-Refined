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
	constexpr uint32_t API_VERSION = 1;
	constexpr uint32_t INPUT_BROKER_API_VERSION = 1;

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
