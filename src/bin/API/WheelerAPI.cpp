#include "WheelerAPI.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <unordered_map>

// Include Wheeler classes for full implementation
#include "bin/Wheeler/Wheeler.h"
#include "bin/Wheeler/Wheel.h"
#include "bin/Wheeler/WheelEntry.h"
#include "bin/Wheeler/WheelItems/WheelItem.h"
#include "bin/Wheeler/WheelItems/WheelItemFactory.h"
#include "bin/Integrations/ActionHotkeysBridge.h"
#include "bin/InputBroker.h"

namespace WheelerAPI
{
	// ============================================================================
	// Logging Helpers
	// ============================================================================

	static const char* ResultToString(Result r)
	{
		switch (r) {
			case Result::OK: return "OK";
			case Result::InvalidWheelIndex: return "InvalidWheelIndex";
			case Result::InvalidEntryIndex: return "InvalidEntryIndex";
			case Result::InvalidItemIndex: return "InvalidItemIndex";
			case Result::InvalidFormID: return "InvalidFormID";
			case Result::FormNotFound: return "FormNotFound";
			case Result::UnsupportedFormType: return "UnsupportedFormType";
			case Result::WheelNotEmpty: return "WheelNotEmpty";
			case Result::LastWheel: return "LastWheel";
			case Result::NotInitialized: return "NotInitialized";
			case Result::NotManagedWheel: return "NotManagedWheel";
			case Result::InEditMode: return "InEditMode";
			case Result::EntryNotEmpty: return "EntryNotEmpty";
			case Result::InvalidArgument: return "InvalidArgument";
			case Result::InternalError: return "InternalError";
			default: return "Unknown";
		}
	}

	// ============================================================================
	// Static State
	// ============================================================================

	static std::atomic<bool> s_initialized{ false };

	// Info about a managed wheel
	struct ManagedWheelInfo
	{
		std::string clientName;
		bool showLabel;
	};

	// Maps wheel index -> managed wheel info
	static std::unordered_map<int32_t, ManagedWheelInfo> s_managedWheelClients;
	static std::shared_mutex s_managedWheelLock;

	// Callbacks - protected by s_callbackLock
	static std::mutex s_callbackLock;
	static ItemActivatedCallback s_itemActivatedCallback = nullptr;
	static EditModeCallback s_editModeCallback = nullptr;
	static WheelStateCallback s_wheelStateCallback = nullptr;

	// ============================================================================
	// Internal Helpers
	// ============================================================================

	void SetInitialized(bool initialized)
	{
		s_initialized.store(initialized, std::memory_order_release);
		if (initialized) {
			logger::info("[WheelerAPI] v{} initialized successfully", API_VERSION);
		} else {
			logger::info("[WheelerAPI] shutdown");
		}
	}

	// Called by Wheeler when item is activated
	void NotifyItemActivated(int32_t wheelIndex, int32_t entryIndex, int32_t itemIndex, uint32_t formID, bool isPrimary)
	{
		// Copy callback under lock, then invoke outside lock to avoid deadlock
		ItemActivatedCallback callback = nullptr;
		{
			std::lock_guard<std::mutex> lock(s_callbackLock);
			callback = s_itemActivatedCallback;
		}
		if (callback) {
			logger::debug("[WheelerAPI] NotifyItemActivated: wheel={} entry={} item={} formID={:08X} primary={}",
				wheelIndex, entryIndex, itemIndex, formID, isPrimary);
			try {
				callback(wheelIndex, entryIndex, itemIndex, formID, isPrimary);
			} catch (const std::exception& e) {
				logger::error("[WheelerAPI] ItemActivatedCallback threw exception: {}", e.what());
			} catch (...) {
				logger::error("[WheelerAPI] ItemActivatedCallback threw unknown exception");
			}
		}
	}

	// Called by Wheeler when edit mode changes
	void NotifyEditModeChanged(bool entered, const WheelChange* changes, size_t changeCount)
	{
		EditModeCallback callback = nullptr;
		{
			std::lock_guard<std::mutex> lock(s_callbackLock);
			callback = s_editModeCallback;
		}
		if (callback) {
			logger::debug("[WheelerAPI] NotifyEditModeChanged: entered={} changeCount={}", entered, changeCount);
			try {
				callback(entered, changes, changeCount);
			} catch (const std::exception& e) {
				logger::error("[WheelerAPI] EditModeCallback threw exception: {}", e.what());
			} catch (...) {
				logger::error("[WheelerAPI] EditModeCallback threw unknown exception");
			}
		}
	}

	// Called by Wheeler when wheel opens/closes
	void NotifyWheelStateChanged(int32_t wheelIndex, bool isOpen)
	{
		WheelStateCallback callback = nullptr;
		{
			std::lock_guard<std::mutex> lock(s_callbackLock);
			callback = s_wheelStateCallback;
		}
		if (callback) {
			logger::debug("[WheelerAPI] NotifyWheelStateChanged: wheelIndex={} isOpen={}", wheelIndex, isOpen);
			try {
				callback(wheelIndex, isOpen);
			} catch (const std::exception& e) {
				logger::error("[WheelerAPI] WheelStateCallback threw exception: {}", e.what());
			} catch (...) {
				logger::error("[WheelerAPI] WheelStateCallback threw unknown exception");
			}
		}
	}

	// Check if wheel index is managed (for serialization exclusion)
	bool IsManagedWheelIndex(int32_t wheelIndex)
	{
		std::shared_lock lock(s_managedWheelLock);
		return s_managedWheelClients.find(wheelIndex) != s_managedWheelClients.end();
	}

	// Get client name for a managed wheel (returns copy for thread safety)
	std::string GetManagedWheelClientNameSafe(int32_t wheelIndex)
	{
		std::shared_lock lock(s_managedWheelLock);
		auto it = s_managedWheelClients.find(wheelIndex);
		if (it != s_managedWheelClients.end()) {
			return it->second.clientName;
		}
		return std::string();
	}

	// Get client name for a managed wheel
	// WARNING: The returned pointer is only valid while s_managedWheelLock is held.
	const char* GetManagedWheelClientName(int32_t wheelIndex)
	{
		std::shared_lock lock(s_managedWheelLock);
		auto it = s_managedWheelClients.find(wheelIndex);
		if (it != s_managedWheelClients.end()) {
			return it->second.clientName.c_str();
		}
		return nullptr;
	}

	// Check if managed wheel label should be shown
	bool ShouldShowManagedWheelLabel(int32_t wheelIndex)
	{
		std::shared_lock lock(s_managedWheelLock);
		auto it = s_managedWheelClients.find(wheelIndex);
		if (it != s_managedWheelClients.end()) {
			return it->second.showLabel;
		}
		return false;
	}

	// Helper to adjust managed wheel indices when wheels are inserted/removed
	static void AdjustManagedIndicesAfterInsert(int32_t insertedAt)
	{
		std::unique_lock lock(s_managedWheelLock);
		std::unordered_map<int32_t, ManagedWheelInfo> adjusted;
		for (auto& [idx, info] : s_managedWheelClients) {
			if (idx >= insertedAt) {
				adjusted[idx + 1] = std::move(info);
			} else {
				adjusted[idx] = std::move(info);
			}
		}
		s_managedWheelClients = std::move(adjusted);
		logger::debug("[WheelerAPI] AdjustManagedIndicesAfterInsert: insertedAt={}", insertedAt);
	}

	static void AdjustManagedIndicesAfterRemove(int32_t removedAt)
	{
		std::unique_lock lock(s_managedWheelLock);
		s_managedWheelClients.erase(removedAt);
		std::unordered_map<int32_t, ManagedWheelInfo> adjusted;
		for (auto& [idx, info] : s_managedWheelClients) {
			if (idx > removedAt) {
				adjusted[idx - 1] = std::move(info);
			} else {
				adjusted[idx] = std::move(info);
			}
		}
		s_managedWheelClients = std::move(adjusted);
		logger::debug("[WheelerAPI] AdjustManagedIndicesAfterRemove: removedAt={}", removedAt);
	}

	// Clear all managed wheel tracking (called during deserialization)
	void ClearManagedWheels()
	{
		std::unique_lock lock(s_managedWheelLock);
		if (!s_managedWheelClients.empty()) {
			logger::info("[WheelerAPI] ClearManagedWheels: clearing {} managed wheel(s) due to deserialization",
				s_managedWheelClients.size());
			s_managedWheelClients.clear();
		}
	}

	Result DeleteWheelIndex(int32_t wheelIndex)
	{
		if (!s_initialized) {
			return Result::NotInitialized;
		}

		std::unique_lock wheelLock(Wheeler::GetWheelDataLock());
		auto& wheels = Wheeler::GetWheels();

		if (wheelIndex < 0 || wheelIndex >= static_cast<int32_t>(wheels.size())) {
			return Result::InvalidWheelIndex;
		}

		if (wheels.size() <= 1) {
			return Result::LastWheel;
		}

		wheels.erase(wheels.begin() + wheelIndex);
		AdjustManagedIndicesAfterRemove(wheelIndex);

		const int activeIdx = Wheeler::GetActiveWheelIndex();
		if (activeIdx >= static_cast<int>(wheels.size())) {
			Wheeler::SetActiveWheelIndex(static_cast<int>(wheels.size()) - 1);
		}
		return Result::OK;
	}

	// ============================================================================
	// API Implementation Functions - FULL IMPLEMENTATION WITH LOGGING
	// ============================================================================

	static bool API_IsInitialized()
	{
		return s_initialized.load(std::memory_order_acquire);
	}

	static bool API_IsInEditMode()
	{
		return Wheeler::IsInEditMode();
	}

	static bool API_IsWheelOpen()
	{
		return Wheeler::IsWheelerOpen();
	}

	static int32_t API_CreateManagedWheel(const WheelConfig* config)
	{
		if (!s_initialized) {
			logger::warn("[WheelerAPI] CreateManagedWheel failed: API not initialized");
			return static_cast<int32_t>(Result::NotInitialized);
		}
		if (!config) {
			logger::error("[WheelerAPI] CreateManagedWheel failed: config is null");
			return static_cast<int32_t>(Result::InternalError);
		}
		if (config->numEntries < 1) {
			logger::error("[WheelerAPI] CreateManagedWheel failed: numEntries={} (must be >= 1)", config->numEntries);
			return static_cast<int32_t>(Result::InternalError);
		}

		std::unique_lock wheelLock(Wheeler::GetWheelDataLock());
		auto& wheels = Wheeler::GetWheels();

		// Create wheel with empty entries
		auto wheel = std::make_unique<Wheel>();
		for (int32_t i = 0; i < config->numEntries; i++) {
			wheel->PushEmptyEntry();
		}

		// Determine insert position
		int32_t index;
		if (config->position < 0 || config->position >= static_cast<int32_t>(wheels.size())) {
			index = static_cast<int32_t>(wheels.size());
			wheels.push_back(std::move(wheel));
		} else {
			index = config->position;
			wheels.insert(wheels.begin() + index, std::move(wheel));
			AdjustManagedIndicesAfterInsert(index);
		}

		// Track as managed with client name and showLabel setting
		if (config->managed) {
			std::unique_lock lock(s_managedWheelLock);
			ManagedWheelInfo info;
			info.clientName = config->clientName ? config->clientName : "Unknown";
			info.showLabel = config->showLabel;
			s_managedWheelClients[index] = std::move(info);
		}

		logger::info("[WheelerAPI] CreateManagedWheel: SUCCESS index={} entries={} client='{}' managed={} showLabel={}",
			index, config->numEntries, config->clientName ? config->clientName : "N/A", config->managed, config->showLabel);
		return index;
	}

	static Result API_DeleteManagedWheel(int32_t wheelIndex)
	{
		if (!s_initialized) {
			logger::warn("[WheelerAPI] DeleteManagedWheel({}) failed: API not initialized", wheelIndex);
			return Result::NotInitialized;
		}

		// Lock ordering: always wheelLock first, then managedWheelLock
		std::unique_lock wheelLock(Wheeler::GetWheelDataLock());
		auto& wheels = Wheeler::GetWheels();

		// Check if managed (under wheelLock to maintain ordering)
		{
			std::shared_lock lock(s_managedWheelLock);
			if (s_managedWheelClients.find(wheelIndex) == s_managedWheelClients.end()) {
				logger::warn("[WheelerAPI] DeleteManagedWheel({}) failed: not a managed wheel", wheelIndex);
				return Result::NotManagedWheel;
			}
		}

		if (wheelIndex < 0 || wheelIndex >= static_cast<int32_t>(wheels.size())) {
			logger::error("[WheelerAPI] DeleteManagedWheel({}) failed: invalid index (wheelCount={})", 
				wheelIndex, wheels.size());
			return Result::InvalidWheelIndex;
		}

		if (wheels.size() <= 1) {
			logger::warn("[WheelerAPI] DeleteManagedWheel({}) failed: cannot delete last wheel", wheelIndex);
			return Result::LastWheel;
		}

		wheels.erase(wheels.begin() + wheelIndex);
		AdjustManagedIndicesAfterRemove(wheelIndex);

		// Adjust active wheel index if needed
		int activeIdx = Wheeler::GetActiveWheelIndex();
		if (activeIdx >= static_cast<int>(wheels.size())) {
			Wheeler::SetActiveWheelIndex(static_cast<int>(wheels.size()) - 1);
		}

		logger::info("[WheelerAPI] DeleteManagedWheel: SUCCESS deleted wheel at index {}", wheelIndex);
		return Result::OK;
	}

	static bool API_IsManagedWheel(int32_t wheelIndex)
	{
		std::shared_lock lock(s_managedWheelLock);
		return s_managedWheelClients.find(wheelIndex) != s_managedWheelClients.end();
	}

	static int32_t API_GetWheelCount()
	{
		std::shared_lock lock(Wheeler::GetWheelDataLock());
		return Wheeler::GetWheelCount();
	}

	static int32_t API_GetActiveWheelIndex()
	{
		return Wheeler::GetActiveWheelIndex();
	}

	static Result API_SetActiveWheelIndex(int32_t index)
	{
		if (!s_initialized) {
			logger::warn("[WheelerAPI] SetActiveWheelIndex({}) failed: API not initialized", index);
			return Result::NotInitialized;
		}

		std::shared_lock lock(Wheeler::GetWheelDataLock());
		if (index < 0 || index >= Wheeler::GetWheelCount()) {
			logger::warn("[WheelerAPI] SetActiveWheelIndex({}) failed: invalid index (wheelCount={})", 
				index, Wheeler::GetWheelCount());
			return Result::InvalidWheelIndex;
		}

		Wheeler::SetActiveWheelIndex(index);
		logger::debug("[WheelerAPI] SetActiveWheelIndex: SUCCESS set to {}", index);
		return Result::OK;
	}

	static bool API_IsWheelEmpty(int32_t wheelIndex)
	{
		std::shared_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
		if (!wheel) {
			return true;
		}
		return wheel->IsEmpty();
	}

	static int32_t API_GetEntryCount(int32_t wheelIndex)
	{
		std::shared_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
		if (!wheel) {
			logger::debug("[WheelerAPI] GetEntryCount({}) failed: invalid wheel index", wheelIndex);
			return static_cast<int32_t>(Result::InvalidWheelIndex);
		}
		return wheel->GetNumEntries();
	}

	static int32_t API_AddEntry(int32_t wheelIndex)
	{
		if (!s_initialized) {
			logger::warn("[WheelerAPI] AddEntry({}) failed: API not initialized", wheelIndex);
			return static_cast<int32_t>(Result::NotInitialized);
		}

		std::unique_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
		if (!wheel) {
			logger::warn("[WheelerAPI] AddEntry({}) failed: invalid wheel index", wheelIndex);
			return static_cast<int32_t>(Result::InvalidWheelIndex);
		}

		wheel->PushEmptyEntry();
		int32_t newIdx = wheel->GetNumEntries() - 1;
		logger::debug("[WheelerAPI] AddEntry: SUCCESS wheel={} newEntryIndex={}", wheelIndex, newIdx);
		return newIdx;
	}

	static Result API_DeleteEntry(int32_t wheelIndex, int32_t entryIndex)
	{
		if (!s_initialized) {
			logger::warn("[WheelerAPI] DeleteEntry({}, {}) failed: API not initialized", wheelIndex, entryIndex);
			return Result::NotInitialized;
		}

		std::unique_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
		if (!wheel) {
			logger::warn("[WheelerAPI] DeleteEntry({}, {}) failed: invalid wheel index", wheelIndex, entryIndex);
			return Result::InvalidWheelIndex;
		}

		wheel->RemoveEntryByIndex(entryIndex);
		logger::debug("[WheelerAPI] DeleteEntry: SUCCESS wheel={} entry={}", wheelIndex, entryIndex);
		return Result::OK;
	}

	static bool API_IsEntryEmpty(int32_t wheelIndex, int32_t entryIndex)
	{
		std::shared_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
		if (!wheel) {
			return true;
		}
		WheelEntry* entry = wheel->GetEntry(entryIndex);
		if (!entry) {
			return true;
		}
		return entry->IsEmpty();
	}

	static int32_t API_GetItemCount(int32_t wheelIndex, int32_t entryIndex)
	{
		std::shared_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
		if (!wheel) {
			return static_cast<int32_t>(Result::InvalidWheelIndex);
		}
		WheelEntry* entry = wheel->GetEntry(entryIndex);
		if (!entry) {
			return static_cast<int32_t>(Result::InvalidEntryIndex);
		}
		return entry->GetNumItems();
	}

	static int32_t API_AddItemByFormID(int32_t wheelIndex, int32_t entryIndex, uint32_t formID, uint16_t uniqueID)
	{
		if (!s_initialized) {
			logger::warn("[WheelerAPI] AddItemByFormID({}, {}, {:08X}) failed: API not initialized", 
				wheelIndex, entryIndex, formID);
			return static_cast<int32_t>(Result::NotInitialized);
		}

		// Validate form exists
		RE::TESForm* form = RE::TESForm::LookupByID(formID);
		if (!form) {
			logger::warn("[WheelerAPI] AddItemByFormID({}, {}, {:08X}) failed: form not found", 
				wheelIndex, entryIndex, formID);
			return static_cast<int32_t>(Result::FormNotFound);
		}

		// Create the wheel item using our new factory method
		std::shared_ptr<WheelItem> item = WheelItemFactory::MakeWheelItemFromFormID(formID, uniqueID);
		if (!item) {
			logger::warn("[WheelerAPI] AddItemByFormID({}, {}, {:08X}) failed: unsupported form type ({})", 
				wheelIndex, entryIndex, formID, static_cast<uint32_t>(form->GetFormType()));
			return static_cast<int32_t>(Result::UnsupportedFormType);
		}

		std::unique_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
		if (!wheel) {
			logger::warn("[WheelerAPI] AddItemByFormID({}, {}, {:08X}) failed: invalid wheel index", 
				wheelIndex, entryIndex, formID);
			return static_cast<int32_t>(Result::InvalidWheelIndex);
		}
		WheelEntry* entry = wheel->GetEntry(entryIndex);
		if (!entry) {
			logger::warn("[WheelerAPI] AddItemByFormID({}, {}, {:08X}) failed: invalid entry index", 
				wheelIndex, entryIndex, formID);
			return static_cast<int32_t>(Result::InvalidEntryIndex);
		}

		entry->PushItem(item);
		int32_t itemIdx = entry->GetNumItems() - 1;
		logger::info("[WheelerAPI] AddItemByFormID: SUCCESS wheel={} entry={} formID={:08X} itemIndex={}", 
			wheelIndex, entryIndex, formID, itemIdx);
		return itemIdx;  // Return index of newly added item
	}

	static Result API_RemoveItem(int32_t wheelIndex, int32_t entryIndex, int32_t itemIndex)
	{
		if (!s_initialized) {
			logger::warn("[WheelerAPI] RemoveItem({}, {}, {}) failed: API not initialized", 
				wheelIndex, entryIndex, itemIndex);
			return Result::NotInitialized;
		}

		std::unique_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
		if (!wheel) {
			logger::warn("[WheelerAPI] RemoveItem({}, {}, {}) failed: invalid wheel index", 
				wheelIndex, entryIndex, itemIndex);
			return Result::InvalidWheelIndex;
		}
		WheelEntry* entry = wheel->GetEntry(entryIndex);
		if (!entry) {
			logger::warn("[WheelerAPI] RemoveItem({}, {}, {}) failed: invalid entry index", 
				wheelIndex, entryIndex, itemIndex);
			return Result::InvalidEntryIndex;
		}
		if (!entry->RemoveItemAt(itemIndex)) {
			logger::warn("[WheelerAPI] RemoveItem({}, {}, {}) failed: invalid item index", 
				wheelIndex, entryIndex, itemIndex);
			return Result::InvalidItemIndex;
		}
		logger::debug("[WheelerAPI] RemoveItem: SUCCESS wheel={} entry={} item={}", wheelIndex, entryIndex, itemIndex);
		return Result::OK;
	}

	static Result API_ClearEntry(int32_t wheelIndex, int32_t entryIndex)
	{
		if (!s_initialized) {
			logger::warn("[WheelerAPI] ClearEntry({}, {}) failed: API not initialized", wheelIndex, entryIndex);
			return Result::NotInitialized;
		}

		std::unique_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
		if (!wheel) {
			logger::warn("[WheelerAPI] ClearEntry({}, {}) failed: invalid wheel index", wheelIndex, entryIndex);
			return Result::InvalidWheelIndex;
		}
		WheelEntry* entry = wheel->GetEntry(entryIndex);
		if (!entry) {
			logger::warn("[WheelerAPI] ClearEntry({}, {}) failed: invalid entry index", wheelIndex, entryIndex);
			return Result::InvalidEntryIndex;
		}
		entry->ClearAllItems();
		logger::debug("[WheelerAPI] ClearEntry: SUCCESS wheel={} entry={}", wheelIndex, entryIndex);
		return Result::OK;
	}

	static uint32_t API_GetItemFormID(int32_t wheelIndex, int32_t entryIndex, int32_t itemIndex)
	{
		std::shared_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
		if (!wheel) {
			return 0;
		}
		WheelEntry* entry = wheel->GetEntry(entryIndex);
		if (!entry) {
			return 0;
		}
		WheelItem* item = entry->GetItem(itemIndex);
		if (!item) {
			return 0;
		}
		return item->GetFormID();
	}

	static int32_t API_GetSelectedItemIndex(int32_t wheelIndex, int32_t entryIndex)
	{
		std::shared_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
		if (!wheel) {
			return static_cast<int32_t>(Result::InvalidWheelIndex);
		}
		WheelEntry* entry = wheel->GetEntry(entryIndex);
		if (!entry) {
			return static_cast<int32_t>(Result::InvalidEntryIndex);
		}
		return entry->GetSelectedItemIndex();
	}

	static Result API_SetSelectedItemIndex(int32_t wheelIndex, int32_t entryIndex, int32_t itemIndex)
	{
		if (!s_initialized) {
			logger::warn("[WheelerAPI] SetSelectedItemIndex({}, {}, {}) failed: API not initialized", 
				wheelIndex, entryIndex, itemIndex);
			return Result::NotInitialized;
		}

		std::unique_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIndex);
		if (!wheel) {
			logger::warn("[WheelerAPI] SetSelectedItemIndex({}, {}, {}) failed: invalid wheel index", 
				wheelIndex, entryIndex, itemIndex);
			return Result::InvalidWheelIndex;
		}
		WheelEntry* entry = wheel->GetEntry(entryIndex);
		if (!entry) {
			logger::warn("[WheelerAPI] SetSelectedItemIndex({}, {}, {}) failed: invalid entry index", 
				wheelIndex, entryIndex, itemIndex);
			return Result::InvalidEntryIndex;
		}
		if (itemIndex < 0 || itemIndex >= entry->GetNumItems()) {
			logger::warn("[WheelerAPI] SetSelectedItemIndex({}, {}, {}) failed: invalid item index (numItems={})", 
				wheelIndex, entryIndex, itemIndex, entry->GetNumItems());
			return Result::InvalidItemIndex;
		}
		entry->SetSelectedItem(itemIndex);
		logger::debug("[WheelerAPI] SetSelectedItemIndex: SUCCESS wheel={} entry={} item={}", wheelIndex, entryIndex, itemIndex);
		return Result::OK;
	}

	static Result API_UpsertExternalHotkey(const ExternalHotkeyConfig* config)
	{
		if (!s_initialized) {
			logger::warn("[WheelerAPI] UpsertExternalHotkey failed: API not initialized");
			return Result::NotInitialized;
		}
		if (!config || !config->sourceTag || !config->sourceTag[0] || config->scanCode == 0) {
			logger::warn("[WheelerAPI] UpsertExternalHotkey failed: invalid arguments");
			return Result::InvalidArgument;
		}

		ActionHotkeysInjectedSlot slot{};
		slot.sourceTag = config->sourceTag;
		slot.displayName = config->displayName ? config->displayName : "";
		slot.scanCode = config->scanCode;
		slot.modifier = config->modifier;
		slot.iconPath = config->iconPath ? config->iconPath : "";
		slot.iconTintARGB = config->iconTintARGB;
		slot.wheelNumber = config->wheelNumber;
		slot.entryIndex = config->entryIndex;

		if (!ActionHotkeysBridge::UpsertInjectedSlot(slot)) {
			logger::warn("[WheelerAPI] UpsertExternalHotkey failed: bridge rejected sourceTag='{}'", slot.sourceTag);
			return Result::InvalidArgument;
		}

		logger::info(
			"[WheelerAPI] UpsertExternalHotkey: SUCCESS sourceTag='{}' wheel={} entry={} scanCode=0x{:X}",
			slot.sourceTag,
			slot.wheelNumber,
			slot.entryIndex,
			slot.scanCode);
		return Result::OK;
	}

	static Result API_RemoveExternalHotkey(const char* sourceTag)
	{
		if (!s_initialized) {
			logger::warn("[WheelerAPI] RemoveExternalHotkey failed: API not initialized");
			return Result::NotInitialized;
		}
		if (!sourceTag || !sourceTag[0]) {
			return Result::InvalidArgument;
		}

		const bool removed = ActionHotkeysBridge::RemoveInjectedSlot(sourceTag);
		logger::info("[WheelerAPI] RemoveExternalHotkey: sourceTag='{}' removed={}", sourceTag, removed);
		return Result::OK;
	}

	static void API_ClearExternalHotkeys()
	{
		ActionHotkeysBridge::ClearInjectedSlots();
		logger::info("[WheelerAPI] ClearExternalHotkeys");
	}

	static void API_RegisterItemActivatedCallback(ItemActivatedCallback callback)
	{
		std::lock_guard<std::mutex> lock(s_callbackLock);
		s_itemActivatedCallback = callback;
		logger::info("[WheelerAPI] ItemActivatedCallback {}", callback ? "registered" : "unregistered");
	}

	static void API_RegisterEditModeCallback(EditModeCallback callback)
	{
		std::lock_guard<std::mutex> lock(s_callbackLock);
		s_editModeCallback = callback;
		logger::info("[WheelerAPI] EditModeCallback {}", callback ? "registered" : "unregistered");
	}

	static void API_RegisterWheelStateCallback(WheelStateCallback callback)
	{
		std::lock_guard<std::mutex> lock(s_callbackLock);
		s_wheelStateCallback = callback;
		logger::info("[WheelerAPI] WheelStateCallback {}", callback ? "registered" : "unregistered");
	}

	static void API_UnregisterItemActivatedCallback()
	{
		std::lock_guard<std::mutex> lock(s_callbackLock);
		s_itemActivatedCallback = nullptr;
		logger::info("[WheelerAPI] ItemActivatedCallback unregistered");
	}

	static void API_UnregisterEditModeCallback()
	{
		std::lock_guard<std::mutex> lock(s_callbackLock);
		s_editModeCallback = nullptr;
		logger::info("[WheelerAPI] EditModeCallback unregistered");
	}

	static void API_UnregisterWheelStateCallback()
	{
		std::lock_guard<std::mutex> lock(s_callbackLock);
		s_wheelStateCallback = nullptr;
		logger::info("[WheelerAPI] WheelStateCallback unregistered");
	}

	static bool API_BrokerRegisterReservation(uint64_t pluginId, InputBrokerDevice device, uint32_t key, int32_t priority, uint32_t flags)
	{
		const auto brokerDevice = (device == InputBrokerDevice::kGamepad) ?
			                          InputBroker::DeviceType::kGamepad :
			                          InputBroker::DeviceType::kMKB;
		return InputBroker::RegisterReservation(pluginId, brokerDevice, key, priority, flags);
	}

	static void API_BrokerUnregisterAll(uint64_t pluginId)
	{
		InputBroker::UnregisterAll(pluginId);
	}

	static void API_BrokerSetActiveOwner(uint64_t pluginId)
	{
		InputBroker::SetActiveOwner(pluginId);
	}

	static void API_BrokerClearActiveOwner(uint64_t pluginId)
	{
		InputBroker::ClearActiveOwner(pluginId);
	}

	static uint64_t API_BrokerGetActiveOwner()
	{
		return InputBroker::GetActiveOwner();
	}

	static bool API_BrokerShouldProcessKey(uint64_t pluginIdSelf, InputBrokerDevice device, uint32_t key, uint32_t contextFlags)
	{
		const auto brokerDevice = (device == InputBrokerDevice::kGamepad) ?
			                          InputBroker::DeviceType::kGamepad :
			                          InputBroker::DeviceType::kMKB;
		return InputBroker::ShouldProcessKey(pluginIdSelf, brokerDevice, key, contextFlags);
	}

	static CooperativeOpeningReplaceResult API_ReplaceCooperativeOpeningBindings(
		const CooperativeOpeningBindingSet* bindingSet)
	{
		return InputBroker::ReplaceCooperativeOpeningBindings(bindingSet);
	}

	static uint64_t API_BeginCooperativeOpeningConsumerScope(
		uint64_t ownerId,
		uint64_t expectedBindingGeneration)
	{
		return InputBroker::BeginCooperativeOpeningConsumerScope(ownerId, expectedBindingGeneration);
	}

	static CooperativeOpeningAttestation API_ObserveAndClaimCooperativeOpeningEvent(
		const CooperativeOpeningEventObservation* observation,
		CooperativeOpeningEventDisposition* disposition)
	{
		return InputBroker::ObserveAndClaimCooperativeOpeningEvent(observation, disposition);
	}

	static CooperativeOpeningAttestation API_GetCooperativeOpeningEventDisposition(
		uint64_t consumerScopeToken,
		uintptr_t eventIdentity,
		uint64_t ownerId,
		CooperativeOpeningEventDisposition* disposition)
	{
		return InputBroker::GetCooperativeOpeningEventDisposition(
			consumerScopeToken,
			eventIdentity,
			ownerId,
			disposition);
	}

	static void API_EndCooperativeOpeningConsumerScope(uint64_t consumerScopeToken)
	{
		InputBroker::EndCooperativeOpeningConsumerScope(consumerScopeToken);
	}

	// ============================================================================
	// API Interface Instance
	// ============================================================================

	static IWheelerAPI s_apiInstance = {
		.version = API_VERSION,
		.IsInitialized = API_IsInitialized,
		.IsInEditMode = API_IsInEditMode,
		.IsWheelOpen = API_IsWheelOpen,
		.CreateManagedWheel = API_CreateManagedWheel,
		.DeleteManagedWheel = API_DeleteManagedWheel,
		.IsManagedWheel = API_IsManagedWheel,
		.GetWheelCount = API_GetWheelCount,
		.GetActiveWheelIndex = API_GetActiveWheelIndex,
		.SetActiveWheelIndex = API_SetActiveWheelIndex,
		.IsWheelEmpty = API_IsWheelEmpty,
		.GetEntryCount = API_GetEntryCount,
		.AddEntry = API_AddEntry,
		.DeleteEntry = API_DeleteEntry,
		.IsEntryEmpty = API_IsEntryEmpty,
		.GetItemCount = API_GetItemCount,
		.AddItemByFormID = API_AddItemByFormID,
		.RemoveItem = API_RemoveItem,
		.ClearEntry = API_ClearEntry,
		.GetItemFormID = API_GetItemFormID,
		.GetSelectedItemIndex = API_GetSelectedItemIndex,
		.SetSelectedItemIndex = API_SetSelectedItemIndex,
		.UpsertExternalHotkey = API_UpsertExternalHotkey,
		.RemoveExternalHotkey = API_RemoveExternalHotkey,
		.ClearExternalHotkeys = API_ClearExternalHotkeys,
		.RegisterItemActivatedCallback = API_RegisterItemActivatedCallback,
		.RegisterEditModeCallback = API_RegisterEditModeCallback,
		.RegisterWheelStateCallback = API_RegisterWheelStateCallback,
		.UnregisterItemActivatedCallback = API_UnregisterItemActivatedCallback,
		.UnregisterEditModeCallback = API_UnregisterEditModeCallback,
		.UnregisterWheelStateCallback = API_UnregisterWheelStateCallback,
	};

	static WheelerInputBrokerAPI s_inputBrokerApi = {
		.apiVersion = INPUT_BROKER_API_VERSION,
		.RegisterReservation = API_BrokerRegisterReservation,
		.UnregisterAll = API_BrokerUnregisterAll,
		.SetActiveOwner = API_BrokerSetActiveOwner,
		.ClearActiveOwner = API_BrokerClearActiveOwner,
		.GetActiveOwner = API_BrokerGetActiveOwner,
		.ShouldProcessKey = API_BrokerShouldProcessKey,
	};

	static_assert(std::is_standard_layout_v<WheelerInputBrokerAPI>);
	static_assert(std::is_trivially_copyable_v<WheelerInputBrokerAPI>);
	static_assert(sizeof(WheelerInputBrokerAPI) == 56);
	static_assert(offsetof(WheelerInputBrokerAPI, apiVersion) == 0);
	static_assert(offsetof(WheelerInputBrokerAPI, RegisterReservation) == 8);
	static_assert(offsetof(WheelerInputBrokerAPI, ShouldProcessKey) == 48);

	static_assert(std::is_standard_layout_v<CooperativeOpeningBinding>);
	static_assert(std::is_trivially_copyable_v<CooperativeOpeningBinding>);
	static_assert(std::is_standard_layout_v<CooperativeOpeningBindingSet>);
	static_assert(std::is_trivially_copyable_v<CooperativeOpeningBindingSet>);
	static_assert(std::is_standard_layout_v<CooperativeOpeningEventObservation>);
	static_assert(std::is_trivially_copyable_v<CooperativeOpeningEventObservation>);
	static_assert(std::is_standard_layout_v<CooperativeOpeningEventDisposition>);
	static_assert(std::is_trivially_copyable_v<CooperativeOpeningEventDisposition>);
	static_assert(std::is_standard_layout_v<CooperativeOpeningAPI>);
	static_assert(std::is_trivially_copyable_v<CooperativeOpeningAPI>);

	static CooperativeOpeningAPI s_cooperativeOpeningApi = {
		.apiVersion = COOPERATIVE_OPENING_API_VERSION,
		.structSize = sizeof(CooperativeOpeningAPI),
		.ReplaceCooperativeOpeningBindings = API_ReplaceCooperativeOpeningBindings,
		.BeginCooperativeOpeningConsumerScope = API_BeginCooperativeOpeningConsumerScope,
		.ObserveAndClaimCooperativeOpeningEvent = API_ObserveAndClaimCooperativeOpeningEvent,
		.GetCooperativeOpeningEventDisposition = API_GetCooperativeOpeningEventDisposition,
		.EndCooperativeOpeningConsumerScope = API_EndCooperativeOpeningConsumerScope,
	};

}  // namespace WheelerAPI

// ============================================================================
// Main Entry Point Export
// ============================================================================

extern "C" WHEELER_API WheelerAPI::IWheelerAPI* GetWheelerAPI()
{
	logger::debug("[WheelerAPI] GetWheelerAPI called, returning API v{}", WheelerAPI::API_VERSION);
	return &WheelerAPI::s_apiInstance;
}

extern "C" WHEELER_API const WheelerAPI::WheelerInputBrokerAPI* GetInputBrokerAPI(uint32_t requestedVersion)
{
	if (requestedVersion != WheelerAPI::INPUT_BROKER_API_VERSION) {
		return nullptr;
	}
	logger::debug("[WheelerAPI] GetInputBrokerAPI called, returning broker API v{}", WheelerAPI::INPUT_BROKER_API_VERSION);
	return &WheelerAPI::s_inputBrokerApi;
}

extern "C" WHEELER_API const WheelerAPI::CooperativeOpeningAPI* GetCooperativeOpeningAPI(uint32_t requestedVersion)
{
	if (requestedVersion != WheelerAPI::COOPERATIVE_OPENING_API_VERSION) {
		return nullptr;
	}
	logger::debug(
		"[WheelerAPI] GetCooperativeOpeningAPI called, returning cooperative API v{}",
		WheelerAPI::COOPERATIVE_OPENING_API_VERSION);
	return &WheelerAPI::s_cooperativeOpeningApi;
}
