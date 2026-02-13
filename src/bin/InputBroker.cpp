#include "InputBroker.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "bin/Config.h"

namespace
{
	struct ReservationKey
	{
		InputBroker::DeviceType device{ InputBroker::DeviceType::kMKB };
		std::uint32_t key{ 0 };

		bool operator==(const ReservationKey&) const = default;
	};

	struct ReservationKeyHash
	{
		std::size_t operator()(const ReservationKey& value) const noexcept
		{
			return (static_cast<std::size_t>(value.key) << 1) ^
			       static_cast<std::size_t>(value.device == InputBroker::DeviceType::kGamepad ? 1 : 0);
		}
	};

	struct ReservationRecord
	{
		std::int32_t priority{ 0 };
		std::uint32_t flags{ InputBroker::ReservationFlag::None };
		std::uint64_t sequence{ 0 };
	};

	struct ReservationWinner
	{
		InputBroker::PluginId owner{ InputBroker::kNoOwner };
		std::int32_t priority{ (std::numeric_limits<std::int32_t>::min)() };
		std::uint32_t flags{ InputBroker::ReservationFlag::None };
		std::uint64_t sequence{ (std::numeric_limits<std::uint64_t>::max)() };
		bool valid{ false };
	};

	using ReservationTable = std::unordered_map<InputBroker::PluginId, ReservationRecord>;

	struct BrokerState
	{
		bool enabled{ true };
		bool debugLog{ false };
		std::int32_t mainPriority{ 50 };
		std::int32_t ammoPriority{ 60 };
		InputBroker::PluginId activeOwner{ InputBroker::kNoOwner };

		std::uint64_t registrationSequence{ 0 };
		std::unordered_map<ReservationKey, ReservationTable, ReservationKeyHash> reservationsByKey;
		std::unordered_map<InputBroker::PluginId, std::unordered_set<ReservationKey, ReservationKeyHash>> keysByPlugin;

		bool wheelerReservationsInstalled{ false };
		std::uint64_t wheelerReservationSignature{ 0 };
	};

	std::mutex g_lock;
	BrokerState g_state;

	static ReservationWinner SelectWinner(const ReservationTable& table)
	{
		ReservationWinner winner;
		for (const auto& [pluginId, record] : table) {
			if (!winner.valid ||
			    record.priority > winner.priority ||
			    (record.priority == winner.priority && record.sequence < winner.sequence)) {
				winner.owner = pluginId;
				winner.priority = record.priority;
				winner.flags = record.flags;
				winner.sequence = record.sequence;
				winner.valid = true;
			}
		}
		return winner;
	}

	static bool RegisterReservationLocked(InputBroker::PluginId pluginId, InputBroker::DeviceType device,
		std::uint32_t key, std::int32_t priority, std::uint32_t flags)
	{
		if (pluginId == InputBroker::kNoOwner || key == 0) {
			return false;
		}

		ReservationKey reservationKey{ device, key };
		auto& table = g_state.reservationsByKey[reservationKey];
		const ReservationWinner winnerBefore = SelectWinner(table);

		auto it = table.find(pluginId);
		if (it == table.end()) {
			table.emplace(pluginId, ReservationRecord{
				priority,
				flags,
				++g_state.registrationSequence
			});
		} else {
			it->second.priority = priority;
			it->second.flags = flags;
		}
		g_state.keysByPlugin[pluginId].insert(reservationKey);

		const ReservationWinner winnerAfter = SelectWinner(table);
		if (g_state.debugLog) {
			logger::info("[InputBroker] Reserve key={} device={} owner={} prio={} flags={} winner={} winnerPrio={}",
				key,
				device == InputBroker::DeviceType::kGamepad ? "Gamepad" : "MKB",
				pluginId,
				priority,
				flags,
				winnerAfter.owner,
				winnerAfter.priority);
		}

		if (winnerBefore.valid &&
		    winnerAfter.valid &&
		    winnerBefore.owner != winnerAfter.owner &&
		    g_state.debugLog) {
			logger::info("[InputBroker] Reservation owner changed key={} device={} {} -> {}",
				key,
				device == InputBroker::DeviceType::kGamepad ? "Gamepad" : "MKB",
				winnerBefore.owner,
				winnerAfter.owner);
		}

		return winnerAfter.valid && winnerAfter.owner == pluginId;
	}

	static void UnregisterAllLocked(InputBroker::PluginId pluginId)
	{
		if (pluginId == InputBroker::kNoOwner) {
			return;
		}

		auto pluginIt = g_state.keysByPlugin.find(pluginId);
		if (pluginIt != g_state.keysByPlugin.end()) {
			for (const ReservationKey& key : pluginIt->second) {
				auto keyIt = g_state.reservationsByKey.find(key);
				if (keyIt == g_state.reservationsByKey.end()) {
					continue;
				}
				keyIt->second.erase(pluginId);
				if (keyIt->second.empty()) {
					g_state.reservationsByKey.erase(keyIt);
				}
			}
			g_state.keysByPlugin.erase(pluginIt);
		}

		if (g_state.activeOwner == pluginId) {
			g_state.activeOwner = InputBroker::kNoOwner;
			if (g_state.debugLog) {
				logger::info("[InputBroker] ActiveOwner cleared during unregister owner={}", pluginId);
			}
		}
	}

	static std::uint64_t HashCombine(std::uint64_t seed, std::uint64_t value)
	{
		seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
		return seed;
	}
}

void InputBroker::RefreshConfigFromSettings()
{
	std::lock_guard<std::mutex> lock(g_lock);

	const bool oldEnabled = g_state.enabled;
	const bool oldDebug = g_state.debugLog;
	const std::int32_t oldMainPriority = g_state.mainPriority;
	const std::int32_t oldAmmoPriority = g_state.ammoPriority;

	g_state.enabled = Config::InputBroker::Enabled;
	g_state.debugLog = Config::InputBroker::DebugLog;
	g_state.mainPriority = Config::InputBroker::Priority_MainWheel;
	g_state.ammoPriority = Config::InputBroker::Priority_AmmoWheel;

	if (!g_state.enabled && g_state.activeOwner != kNoOwner) {
		g_state.activeOwner = kNoOwner;
	}

	if (g_state.debugLog &&
	    (oldEnabled != g_state.enabled ||
	        oldDebug != g_state.debugLog ||
	        oldMainPriority != g_state.mainPriority ||
	        oldAmmoPriority != g_state.ammoPriority)) {
		logger::info("[InputBroker] Config enabled={} debug={} prioMain={} prioAmmo={}",
			g_state.enabled ? 1 : 0,
			g_state.debugLog ? 1 : 0,
			g_state.mainPriority,
			g_state.ammoPriority);
	}
}

bool InputBroker::IsEnabled()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_state.enabled;
}

bool InputBroker::IsDebugLogEnabled()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_state.debugLog;
}

std::int32_t InputBroker::GetMainWheelPriority()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_state.mainPriority;
}

std::int32_t InputBroker::GetAmmoWheelPriority()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_state.ammoPriority;
}

bool InputBroker::RegisterReservation(PluginId pluginId, DeviceType device, std::uint32_t key, std::int32_t priority, std::uint32_t flags)
{
	std::lock_guard<std::mutex> lock(g_lock);
	return RegisterReservationLocked(pluginId, device, key, priority, flags);
}

void InputBroker::UnregisterAll(PluginId pluginId)
{
	std::lock_guard<std::mutex> lock(g_lock);
	UnregisterAllLocked(pluginId);

	if (pluginId == kWheelerRefinedPluginId) {
		g_state.wheelerReservationsInstalled = false;
		g_state.wheelerReservationSignature = 0;
	}
}

void InputBroker::SetActiveOwner(PluginId pluginId)
{
	if (pluginId == kNoOwner) {
		return;
	}

	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_state.enabled) {
		return;
	}
	if (g_state.activeOwner == pluginId) {
		return;
	}

	g_state.activeOwner = pluginId;
	if (g_state.debugLog) {
		logger::info("[InputBroker] ActiveOwner set owner={}", pluginId);
	}
}

void InputBroker::ClearActiveOwner(PluginId pluginId)
{
	if (pluginId == kNoOwner) {
		return;
	}

	std::lock_guard<std::mutex> lock(g_lock);
	if (g_state.activeOwner != pluginId) {
		return;
	}

	g_state.activeOwner = kNoOwner;
	if (g_state.debugLog) {
		logger::info("[InputBroker] ActiveOwner cleared owner={}", pluginId);
	}
}

InputBroker::PluginId InputBroker::GetActiveOwner()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_state.activeOwner;
}

bool InputBroker::ShouldProcessKey(PluginId pluginIdSelf, DeviceType device, std::uint32_t key, std::uint32_t contextFlags)
{
	std::lock_guard<std::mutex> lock(g_lock);

	if (!g_state.enabled) {
		return true;
	}

	if (pluginIdSelf == kNoOwner) {
		return false;
	}

	if (g_state.activeOwner != kNoOwner && g_state.activeOwner != pluginIdSelf) {
		if (g_state.debugLog) {
			logger::info("[InputBroker] Blocked plugin={} key={} reason=ActiveOwner({}) context={}",
				pluginIdSelf,
				key,
				g_state.activeOwner,
				contextFlags);
		}
		return false;
	}

	// Active owner has full processing rights while owning input.
	// Reservations are primarily for arbitration when no wheel currently owns input.
	if (g_state.activeOwner == pluginIdSelf) {
		return true;
	}

	ReservationKey reservationKey{ device, key };
	auto reservationIt = g_state.reservationsByKey.find(reservationKey);
	if (reservationIt == g_state.reservationsByKey.end()) {
		return true;
	}

	const ReservationWinner winner = SelectWinner(reservationIt->second);
	if (winner.valid && winner.owner != pluginIdSelf) {
		if (g_state.debugLog) {
			logger::info("[InputBroker] Blocked plugin={} key={} reservedBy={} prio={} activeOwner={} context={}",
				pluginIdSelf,
				key,
				winner.owner,
				winner.priority,
				g_state.activeOwner,
				contextFlags);
		}
		return false;
	}

	return true;
}

bool InputBroker::IsBlockedByActiveOwner(PluginId pluginIdSelf)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_state.enabled) {
		return false;
	}
	return g_state.activeOwner != kNoOwner && g_state.activeOwner != pluginIdSelf;
}

void InputBroker::RefreshWheelerReservations()
{
	RefreshConfigFromSettings();

	const bool wheelEnabled = Config::WheelerEnabled;
	const bool ammoEnabled = Config::AmmoWheel::Enabled;
	const std::uint32_t mainMkbToggle = Config::InputBindings::MKB::toggleWheel;
	const std::uint32_t mainGamepadToggle = Config::InputBindings::GamePad::toggleWheel;
	const std::uint32_t mainInvToggle = Config::InputBindings::GamePad::toggleWheelIfInInventory;
	const std::uint32_t mainNonInvToggle = Config::InputBindings::GamePad::toggleWheelIfNotInInventory;
	const std::uint32_t ammoMkbToggle = Config::AmmoWheel::MKB::toggleAmmoWheel;
	const std::uint32_t ammoMouseToggle = Config::AmmoWheel::MKB::toggleAmmoWheelMouse;
	const std::uint32_t ammoGamepadToggle = Config::AmmoWheel::GamePad::toggleAmmoWheel;

	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_state.enabled || !wheelEnabled) {
		if (g_state.wheelerReservationsInstalled) {
			UnregisterAllLocked(kWheelerRefinedPluginId);
			g_state.wheelerReservationsInstalled = false;
			g_state.wheelerReservationSignature = 0;
		}
		return;
	}

	std::uint64_t signature = 0;
	signature = HashCombine(signature, static_cast<std::uint64_t>(mainMkbToggle));
	signature = HashCombine(signature, static_cast<std::uint64_t>(mainGamepadToggle));
	signature = HashCombine(signature, static_cast<std::uint64_t>(mainInvToggle));
	signature = HashCombine(signature, static_cast<std::uint64_t>(mainNonInvToggle));
	signature = HashCombine(signature, static_cast<std::uint64_t>(ammoMkbToggle));
	signature = HashCombine(signature, static_cast<std::uint64_t>(ammoMouseToggle));
	signature = HashCombine(signature, static_cast<std::uint64_t>(ammoGamepadToggle));
	signature = HashCombine(signature, ammoEnabled ? 1ULL : 0ULL);
	signature = HashCombine(signature, static_cast<std::uint64_t>(g_state.mainPriority));
	signature = HashCombine(signature, static_cast<std::uint64_t>(g_state.ammoPriority));

	if (g_state.wheelerReservationsInstalled && signature == g_state.wheelerReservationSignature) {
		return;
	}

	UnregisterAllLocked(kWheelerRefinedPluginId);

	if (mainMkbToggle != 0) {
		RegisterReservationLocked(kWheelerRefinedPluginId, DeviceType::kMKB, mainMkbToggle, g_state.mainPriority, ReservationFlag::ToggleKey);
	}
	if (mainGamepadToggle != 0) {
		RegisterReservationLocked(kWheelerRefinedPluginId, DeviceType::kGamepad, mainGamepadToggle, g_state.mainPriority, ReservationFlag::ToggleKey);
	}
	if (mainInvToggle != 0) {
		RegisterReservationLocked(kWheelerRefinedPluginId, DeviceType::kGamepad, mainInvToggle, g_state.mainPriority, ReservationFlag::ToggleKey);
	}
	if (mainNonInvToggle != 0) {
		RegisterReservationLocked(kWheelerRefinedPluginId, DeviceType::kGamepad, mainNonInvToggle, g_state.mainPriority, ReservationFlag::ToggleKey);
	}

	if (ammoEnabled) {
		if (ammoMkbToggle != 0) {
			RegisterReservationLocked(kWheelerRefinedPluginId, DeviceType::kMKB, ammoMkbToggle, g_state.ammoPriority, ReservationFlag::ToggleKey);
		}
		if (ammoMouseToggle != 0) {
			RegisterReservationLocked(kWheelerRefinedPluginId, DeviceType::kMKB, ammoMouseToggle, g_state.ammoPriority, ReservationFlag::ToggleKey);
		}
		if (ammoGamepadToggle != 0) {
			RegisterReservationLocked(kWheelerRefinedPluginId, DeviceType::kGamepad, ammoGamepadToggle, g_state.ammoPriority, ReservationFlag::ToggleKey);
		}
	}

	g_state.wheelerReservationsInstalled = true;
	g_state.wheelerReservationSignature = signature;
}

void InputBroker::SyncWheelerActiveOwner(bool mainWheelOpen, bool ammoWheelOpen)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_state.enabled) {
		return;
	}

	const bool shouldOwn = mainWheelOpen || ammoWheelOpen;
	if (shouldOwn) {
		if (g_state.activeOwner != kWheelerRefinedPluginId) {
			g_state.activeOwner = kWheelerRefinedPluginId;
			if (g_state.debugLog) {
				logger::info("[InputBroker] ActiveOwner set owner={} (mainOpen={} ammoOpen={})",
					kWheelerRefinedPluginId,
					mainWheelOpen ? 1 : 0,
					ammoWheelOpen ? 1 : 0);
			}
		}
		return;
	}

	if (g_state.activeOwner == kWheelerRefinedPluginId) {
		g_state.activeOwner = kNoOwner;
		if (g_state.debugLog) {
			logger::info("[InputBroker] ActiveOwner cleared owner={}", kWheelerRefinedPluginId);
		}
	}
}
