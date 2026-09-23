#include "InputBroker.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bin/Config.h"
#include "bin/Integrations/DMenuInputOwnership.h"
#include "bin/UserInput/Controls.h"

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

	struct CooperativeBindingRecord
	{
		WheelerAPI::CooperativeOpeningBinding binding{};
		std::uint32_t ordinal{ 0 };
	};

	struct CooperativeOwnerBindingSet
	{
		std::uint64_t generation{ 0 };
		std::vector<CooperativeBindingRecord> bindings;
	};

	enum class CooperativeGrantState : std::uint32_t
	{
		kPending = 0,
		kClaimed = 1
	};

	struct CooperativeGrantRecord
	{
		std::uintptr_t eventIdentity{ 0 };
		WheelerAPI::CooperativeOpeningDevice device{ WheelerAPI::CooperativeOpeningDevice::kMKB };
		std::uint32_t mappedKey{ 0 };
		WheelerAPI::CooperativeOpeningTriggerEdge edge{ WheelerAPI::CooperativeOpeningTriggerEdge::kUnknown };
		InputBroker::PluginId ownerId{ InputBroker::kNoOwner };
		std::uint64_t bindingGeneration{ 0 };
		std::uint64_t opaqueGrantToken{ 0 };
		CooperativeGrantState state{ CooperativeGrantState::kPending };
		bool mustSuppress{ true };
	};

	struct CooperativeProducerFrame
	{
		std::uint64_t dispatchGeneration{ 0 };
		std::vector<CooperativeGrantRecord> grants;
	};

	struct CooperativeConsumerObservation
	{
		std::uintptr_t eventIdentity{ 0 };
		WheelerAPI::CooperativeOpeningEventDisposition disposition{};
	};

	struct CooperativeConsumerFrame
	{
		std::uint64_t token{ 0 };
		InputBroker::PluginId ownerId{ InputBroker::kNoOwner };
		std::uint64_t expectedBindingGeneration{ 0 };
		std::vector<std::uintptr_t> upstreamObservedEventIdentities;
		std::vector<CooperativeConsumerObservation> observations;
	};

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
		std::unordered_map<InputBroker::PluginId, CooperativeOwnerBindingSet> cooperativeBindingsByOwner;

		bool wheelerReservationsInstalled{ false };
		std::uint64_t wheelerReservationSignature{ 0 };
		bool dMenuInputOwnerActive{ false };
	};

	std::mutex g_lock;
	BrokerState g_state;
	std::atomic<std::uint64_t> g_nextCooperativeDispatchGeneration{ 0 };
	std::atomic<std::uint64_t> g_nextCooperativeGrantToken{ 0 };
	std::atomic<std::uint64_t> g_nextCooperativeConsumerToken{ 0 };
	thread_local std::vector<CooperativeProducerFrame> g_cooperativeProducerFrames;
	thread_local std::vector<CooperativeConsumerFrame> g_cooperativeConsumerFrames;

	static InputBroker::PluginId GetEffectiveActiveOwnerLocked(bool dMenuActive)
	{
		return dMenuActive ? InputBroker::kDMenuInputOwnerId : g_state.activeOwner;
	}

	static const char* GetOwnerName(InputBroker::PluginId owner)
	{
		if (owner == InputBroker::kDMenuInputOwnerId) {
			return "dMenu";
		}
		if (owner == InputBroker::kWheelerRefinedPluginId) {
			return "Wheeler";
		}
		if (owner == InputBroker::kNoOwner) {
			return "None";
		}
		return "External";
	}

	static void ObserveDMenuOwnerTransitionLocked(bool dMenuActive)
	{
		if (g_state.dMenuInputOwnerActive == dMenuActive) {
			return;
		}

		const auto previousOwner = GetEffectiveActiveOwnerLocked(g_state.dMenuInputOwnerActive);
		g_state.dMenuInputOwnerActive = dMenuActive;
		const auto nextOwner = GetEffectiveActiveOwnerLocked(dMenuActive);
		logger::info(
			"[InputBroker] effective active owner changed: {} -> {}",
			GetOwnerName(previousOwner),
			GetOwnerName(nextOwner));
	}

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

	static std::uint64_t NextNonzero(std::atomic<std::uint64_t>& counter)
	{
		for (;;) {
			const auto value = counter.fetch_add(1, std::memory_order_relaxed) + 1;
			if (value != 0) {
				return value;
			}
		}
	}

	static WheelerAPI::CooperativeOpeningDevice ToCooperativeDevice(InputBroker::DeviceType device)
	{
		return device == InputBroker::DeviceType::kGamepad ?
		           WheelerAPI::CooperativeOpeningDevice::kGamepad :
		           WheelerAPI::CooperativeOpeningDevice::kMKB;
	}

	static bool IsDuplicateBinding(
		const CooperativeBindingRecord& lhs,
		const CooperativeBindingRecord& rhs)
	{
		return lhs.binding.device == rhs.binding.device &&
		       lhs.binding.primaryMappedKey == rhs.binding.primaryMappedKey &&
		       lhs.binding.modifierMappedKey == rhs.binding.modifierMappedKey &&
		       lhs.binding.triggerEdge == rhs.binding.triggerEdge;
	}

	static bool CandidateOutranks(
		const CooperativeBindingRecord& candidate,
		InputBroker::PluginId candidateOwner,
		const CooperativeBindingRecord& incumbent,
		InputBroker::PluginId incumbentOwner)
	{
		if (candidate.binding.priority != incumbent.binding.priority) {
			return candidate.binding.priority > incumbent.binding.priority;
		}
		const bool candidateHasModifier = candidate.binding.modifierMappedKey != 0;
		const bool incumbentHasModifier = incumbent.binding.modifierMappedKey != 0;
		if (candidateHasModifier != incumbentHasModifier) {
			return candidateHasModifier;
		}
		if (candidateOwner != incumbentOwner) {
			return candidateOwner < incumbentOwner;
		}
		return candidate.ordinal < incumbent.ordinal;
	}

	static CooperativeConsumerFrame* FindConsumerFrame(std::uint64_t token)
	{
		for (auto it = g_cooperativeConsumerFrames.rbegin(); it != g_cooperativeConsumerFrames.rend(); ++it) {
			if (it->token == token) {
				return std::addressof(*it);
			}
		}
		return nullptr;
	}

	static bool WasObservedByUpstreamConsumer(
		InputBroker::PluginId ownerId,
		std::uintptr_t eventIdentity)
	{
		for (auto frameIt = g_cooperativeConsumerFrames.rbegin(); frameIt != g_cooperativeConsumerFrames.rend(); ++frameIt) {
			if (frameIt->ownerId != ownerId) {
				continue;
			}
			if (std::find(
					frameIt->upstreamObservedEventIdentities.begin(),
					frameIt->upstreamObservedEventIdentities.end(),
					eventIdentity) != frameIt->upstreamObservedEventIdentities.end()) {
				return true;
			}
		}
		return false;
	}

	static bool PrepareDisposition(WheelerAPI::CooperativeOpeningEventDisposition* disposition)
	{
		if (!disposition ||
		    disposition->structSize != sizeof(WheelerAPI::CooperativeOpeningEventDisposition) ||
		    disposition->structVersion != WheelerAPI::COOPERATIVE_OPENING_EVENT_VERSION) {
			return false;
		}
		*disposition = WheelerAPI::CooperativeOpeningEventDisposition{
			.structSize = sizeof(WheelerAPI::CooperativeOpeningEventDisposition),
			.structVersion = WheelerAPI::COOPERATIVE_OPENING_EVENT_VERSION,
			.attestation = WheelerAPI::CooperativeOpeningAttestation::kObservedButNotMatched,
			.reason = WheelerAPI::CooperativeOpeningReason::kNone,
			.dispatchGeneration = 0,
			.bindingGeneration = 0,
			.opaqueGrantToken = 0,
			.mustSuppressDownstream = 0,
			.reserved = 0
		};
		return true;
	}

	static void StoreConsumerObservation(
		CooperativeConsumerFrame& frame,
		std::uintptr_t eventIdentity,
		const WheelerAPI::CooperativeOpeningEventDisposition& disposition)
	{
		for (auto& observation : frame.observations) {
			if (observation.eventIdentity == eventIdentity) {
				observation.disposition = disposition;
				return;
			}
		}
		frame.observations.push_back(CooperativeConsumerObservation{ eventIdentity, disposition });
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
	const bool dMenuActive = DMenuInputOwnership::IsActive();
	std::lock_guard<std::mutex> lock(g_lock);
	ObserveDMenuOwnerTransitionLocked(dMenuActive);
	return GetEffectiveActiveOwnerLocked(dMenuActive);
}

bool InputBroker::ShouldProcessKey(PluginId pluginIdSelf, DeviceType device, std::uint32_t key, std::uint32_t contextFlags)
{
	const bool dMenuActive = DMenuInputOwnership::IsActive();
	std::lock_guard<std::mutex> lock(g_lock);
	ObserveDMenuOwnerTransitionLocked(dMenuActive);
	const PluginId effectiveActiveOwner = GetEffectiveActiveOwnerLocked(dMenuActive);

	// A live higher UI owner is an input-safety boundary even when cooperative
	// reservation arbitration has been disabled in configuration.
	if (effectiveActiveOwner == kDMenuInputOwnerId) {
		return pluginIdSelf == kDMenuInputOwnerId;
	}

	if (!g_state.enabled) {
		return true;
	}

	if (pluginIdSelf == kNoOwner) {
		return false;
	}

	if (effectiveActiveOwner != kNoOwner && effectiveActiveOwner != pluginIdSelf) {
		if (g_state.debugLog) {
			logger::info("[InputBroker] Blocked plugin={} key={} reason=ActiveOwner({}) context={}",
				pluginIdSelf,
				key,
				effectiveActiveOwner,
				contextFlags);
		}
		return false;
	}

	// Active owner has full processing rights while owning input.
	// Reservations are primarily for arbitration when no wheel currently owns input.
	if (effectiveActiveOwner == pluginIdSelf) {
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
				effectiveActiveOwner,
				contextFlags);
		}
		return false;
	}

	return true;
}

bool InputBroker::IsBlockedByActiveOwner(PluginId pluginIdSelf)
{
	const bool dMenuActive = DMenuInputOwnership::IsActive();
	std::lock_guard<std::mutex> lock(g_lock);
	ObserveDMenuOwnerTransitionLocked(dMenuActive);
	const PluginId effectiveActiveOwner = GetEffectiveActiveOwnerLocked(dMenuActive);
	if (effectiveActiveOwner == kDMenuInputOwnerId) {
		return pluginIdSelf != kDMenuInputOwnerId;
	}
	if (!g_state.enabled) {
		return false;
	}
	return effectiveActiveOwner != kNoOwner && effectiveActiveOwner != pluginIdSelf;
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

InputBroker::CooperativeOpeningProducerScope::CooperativeOpeningProducerScope()
	: _dispatchGeneration(NextNonzero(g_nextCooperativeDispatchGeneration))
{
	g_cooperativeProducerFrames.push_back(CooperativeProducerFrame{ _dispatchGeneration, {} });
}

InputBroker::CooperativeOpeningProducerScope::~CooperativeOpeningProducerScope()
{
	if (_dispatchGeneration == 0 || g_cooperativeProducerFrames.empty() ||
	    g_cooperativeProducerFrames.back().dispatchGeneration != _dispatchGeneration) {
		logger::error("[CooperativeOpening] producer scope stack mismatch dispatch={}", _dispatchGeneration);
		return;
	}

	for (const auto& grant : g_cooperativeProducerFrames.back().grants) {
		if (grant.state == CooperativeGrantState::kPending) {
			logger::warn(
				"[CooperativeOpening] grant ended unclaimed dispatch={} owner={} key={} generation={}",
				_dispatchGeneration,
				grant.ownerId,
				grant.mappedKey,
				grant.bindingGeneration);
		}
	}
	g_cooperativeProducerFrames.pop_back();
}

WheelerAPI::CooperativeOpeningReplaceResult InputBroker::ReplaceCooperativeOpeningBindings(
	const WheelerAPI::CooperativeOpeningBindingSet* bindingSet)
{
	using Result = WheelerAPI::CooperativeOpeningReplaceResult;
	if (!bindingSet) {
		return Result::kInvalidArgument;
	}
	if (bindingSet->structSize != sizeof(WheelerAPI::CooperativeOpeningBindingSet)) {
		return Result::kInvalidSize;
	}
	if (bindingSet->structVersion != WheelerAPI::COOPERATIVE_OPENING_BINDING_SET_VERSION) {
		return Result::kUnsupportedVersion;
	}
	if (bindingSet->ownerId == kNoOwner) {
		return Result::kInvalidOwner;
	}
	if (bindingSet->bindingGeneration == 0) {
		return Result::kInvalidGeneration;
	}
	if (bindingSet->bindingCount > 16) {
		return Result::kTooManyBindings;
	}
	if (bindingSet->bindingStride != sizeof(WheelerAPI::CooperativeOpeningBinding)) {
		return Result::kInvalidSize;
	}
	if (bindingSet->bindingCount != 0 && !bindingSet->bindings) {
		return Result::kInvalidArgument;
	}

	std::vector<CooperativeBindingRecord> replacement;
	replacement.reserve(bindingSet->bindingCount);
	const auto* bindingBytes = static_cast<const std::byte*>(bindingSet->bindings);
	for (std::uint32_t ordinal = 0; ordinal < bindingSet->bindingCount; ++ordinal) {
		WheelerAPI::CooperativeOpeningBinding binding{};
		std::memcpy(
			std::addressof(binding),
			bindingBytes + (static_cast<std::size_t>(ordinal) * bindingSet->bindingStride),
			sizeof(binding));
		if (!CooperativeOpeningPolicy::IsBindingValid(binding)) {
			return Result::kInvalidBinding;
		}

		CooperativeBindingRecord record{ binding, ordinal };
		if (std::any_of(replacement.begin(), replacement.end(), [&](const auto& existing) {
				return IsDuplicateBinding(existing, record);
			})) {
			return Result::kDuplicateBinding;
		}
		replacement.push_back(record);
	}

	std::lock_guard<std::mutex> lock(g_lock);
	const auto current = g_state.cooperativeBindingsByOwner.find(bindingSet->ownerId);
	if (current != g_state.cooperativeBindingsByOwner.end() &&
	    bindingSet->bindingGeneration <= current->second.generation) {
		return Result::kStaleGeneration;
	}

	CooperativeOwnerBindingSet next{
		.generation = bindingSet->bindingGeneration,
		.bindings = std::move(replacement)
	};
	g_state.cooperativeBindingsByOwner.insert_or_assign(bindingSet->ownerId, std::move(next));
	if (g_state.debugLog) {
		logger::info(
			"[CooperativeOpening] registered owner={} generation={} bindings={}",
			bindingSet->ownerId,
			bindingSet->bindingGeneration,
			bindingSet->bindingCount);
	}
	return Result::kSuccess;
}

std::uint64_t InputBroker::BeginCooperativeOpeningConsumerScope(
	PluginId ownerId,
	std::uint64_t expectedBindingGeneration)
{
	if (ownerId == kNoOwner || expectedBindingGeneration == 0) {
		return 0;
	}

	{
		std::lock_guard<std::mutex> lock(g_lock);
		const auto owner = g_state.cooperativeBindingsByOwner.find(ownerId);
		if (owner == g_state.cooperativeBindingsByOwner.end() ||
		    owner->second.generation != expectedBindingGeneration) {
			return 0;
		}
	}

	const auto token = NextNonzero(g_nextCooperativeConsumerToken);
	g_cooperativeConsumerFrames.push_back(CooperativeConsumerFrame{
		.token = token,
		.ownerId = ownerId,
		.expectedBindingGeneration = expectedBindingGeneration,
		.upstreamObservedEventIdentities = {},
		.observations = {}
	});
	return token;
}

WheelerAPI::CooperativeOpeningAttestation InputBroker::ObserveAndClaimCooperativeOpeningEvent(
	const WheelerAPI::CooperativeOpeningEventObservation* observation,
	WheelerAPI::CooperativeOpeningEventDisposition* disposition)
{
	using Attestation = WheelerAPI::CooperativeOpeningAttestation;
	using Reason = WheelerAPI::CooperativeOpeningReason;
	if (!PrepareDisposition(disposition)) {
		return Attestation::kObservedButNotMatched;
	}
	if (!observation ||
	    observation->structSize != sizeof(WheelerAPI::CooperativeOpeningEventObservation) ||
	    observation->structVersion != WheelerAPI::COOPERATIVE_OPENING_EVENT_VERSION ||
	    observation->eventIdentity == 0) {
		disposition->reason = Reason::kInvalidScope;
		return disposition->attestation;
	}

	auto* consumerFrame = FindConsumerFrame(observation->consumerScopeToken);
	if (!consumerFrame ||
	    consumerFrame->ownerId != observation->ownerId ||
	    consumerFrame->expectedBindingGeneration != observation->expectedBindingGeneration) {
		disposition->reason = Reason::kInvalidScope;
		return disposition->attestation;
	}

	if (g_cooperativeProducerFrames.empty()) {
		if (std::find(
				consumerFrame->upstreamObservedEventIdentities.begin(),
				consumerFrame->upstreamObservedEventIdentities.end(),
				observation->eventIdentity) == consumerFrame->upstreamObservedEventIdentities.end()) {
			consumerFrame->upstreamObservedEventIdentities.push_back(observation->eventIdentity);
		}
		disposition->attestation = Attestation::kNoUpstreamAttestationAvailable;
		disposition->reason = Reason::kNoProducerFrame;
		StoreConsumerObservation(*consumerFrame, observation->eventIdentity, *disposition);
		return disposition->attestation;
	}

	auto& producerFrame = g_cooperativeProducerFrames.back();
	disposition->dispatchGeneration = producerFrame.dispatchGeneration;
	auto grantIt = std::find_if(producerFrame.grants.begin(), producerFrame.grants.end(), [&](const auto& grant) {
		return grant.eventIdentity == observation->eventIdentity;
	});
	if (grantIt == producerFrame.grants.end()) {
		disposition->reason = Reason::kNoGrantForEvent;
		StoreConsumerObservation(*consumerFrame, observation->eventIdentity, *disposition);
		return disposition->attestation;
	}

	disposition->bindingGeneration = grantIt->bindingGeneration;
	disposition->opaqueGrantToken = grantIt->opaqueGrantToken;
	if (grantIt->ownerId != observation->ownerId) {
		disposition->reason = Reason::kWrongOwner;
		StoreConsumerObservation(*consumerFrame, observation->eventIdentity, *disposition);
		return disposition->attestation;
	}

	// Once assigned, the winning owner must contain the event even if its
	// opening guard or claim correlation rejects it.
	disposition->mustSuppressDownstream = grantIt->mustSuppress ? 1u : 0u;
	if (observation->expectedBindingGeneration != grantIt->bindingGeneration) {
		disposition->reason = Reason::kWrongGeneration;
		StoreConsumerObservation(*consumerFrame, observation->eventIdentity, *disposition);
		return disposition->attestation;
	}
	{
		std::lock_guard<std::mutex> lock(g_lock);
		const auto current = g_state.cooperativeBindingsByOwner.find(grantIt->ownerId);
		if (current == g_state.cooperativeBindingsByOwner.end() ||
		    current->second.generation != grantIt->bindingGeneration) {
			disposition->reason = Reason::kGrantRevoked;
			StoreConsumerObservation(*consumerFrame, observation->eventIdentity, *disposition);
			return disposition->attestation;
		}
	}
	if (observation->device != grantIt->device) {
		disposition->reason = Reason::kWrongDevice;
		StoreConsumerObservation(*consumerFrame, observation->eventIdentity, *disposition);
		return disposition->attestation;
	}
	if (observation->mappedKey != grantIt->mappedKey) {
		disposition->reason = Reason::kWrongKey;
		StoreConsumerObservation(*consumerFrame, observation->eventIdentity, *disposition);
		return disposition->attestation;
	}
	if (observation->edge != grantIt->edge) {
		disposition->reason = Reason::kWrongEdge;
		StoreConsumerObservation(*consumerFrame, observation->eventIdentity, *disposition);
		return disposition->attestation;
	}
	if (grantIt->state != CooperativeGrantState::kPending) {
		disposition->reason = Reason::kAlreadyClaimed;
		StoreConsumerObservation(*consumerFrame, observation->eventIdentity, *disposition);
		return disposition->attestation;
	}

	grantIt->state = CooperativeGrantState::kClaimed;
	disposition->attestation = Attestation::kMatchedForThisOwner;
	disposition->reason = Reason::kMatched;
	StoreConsumerObservation(*consumerFrame, observation->eventIdentity, *disposition);
	if (IsDebugLogEnabled()) {
		logger::info(
			"[CooperativeOpening] claim dispatch={} owner={} result=matched",
			producerFrame.dispatchGeneration,
			grantIt->ownerId);
	}
	return disposition->attestation;
}

WheelerAPI::CooperativeOpeningAttestation InputBroker::GetCooperativeOpeningEventDisposition(
	std::uint64_t consumerScopeToken,
	std::uintptr_t eventIdentity,
	PluginId ownerId,
	WheelerAPI::CooperativeOpeningEventDisposition* disposition)
{
	using Attestation = WheelerAPI::CooperativeOpeningAttestation;
	using Reason = WheelerAPI::CooperativeOpeningReason;
	if (!PrepareDisposition(disposition)) {
		return Attestation::kObservedButNotMatched;
	}
	const auto* consumerFrame = FindConsumerFrame(consumerScopeToken);
	if (!consumerFrame || consumerFrame->ownerId != ownerId || eventIdentity == 0) {
		disposition->reason = Reason::kInvalidScope;
		return disposition->attestation;
	}
	const auto observation = std::find_if(
		consumerFrame->observations.begin(),
		consumerFrame->observations.end(),
		[&](const auto& candidate) { return candidate.eventIdentity == eventIdentity; });
	if (observation == consumerFrame->observations.end()) {
		disposition->reason = Reason::kNoGrantForEvent;
		return disposition->attestation;
	}
	*disposition = observation->disposition;
	return disposition->attestation;
}

void InputBroker::EndCooperativeOpeningConsumerScope(std::uint64_t consumerScopeToken)
{
	if (consumerScopeToken == 0) {
		return;
	}
	if (g_cooperativeConsumerFrames.empty() ||
	    g_cooperativeConsumerFrames.back().token != consumerScopeToken) {
		logger::error("[CooperativeOpening] consumer scope stack mismatch token={}", consumerScopeToken);
		return;
	}
	g_cooperativeConsumerFrames.pop_back();
}

InputBroker::CooperativeOpeningGrantDecision InputBroker::TryGrantCooperativeOpening(
	std::uintptr_t eventIdentity,
	DeviceType device,
	std::uint32_t mappedKey,
	bool physicalPrimaryDownEdge,
	bool mainWheelExclusiveContext)
{
	using Reason = WheelerAPI::CooperativeOpeningReason;
	CooperativeOpeningGrantDecision result{};
	if (eventIdentity == 0 || !physicalPrimaryDownEdge || !mainWheelExclusiveContext ||
	    g_cooperativeProducerFrames.empty()) {
		return result;
	}

	const auto cooperativeDevice = ToCooperativeDevice(device);
	CooperativeBindingRecord winningBinding{};
	PluginId winningOwner = kNoOwner;
	std::uint64_t winningGeneration = 0;
	bool hasWinner = false;
	std::unique_lock<std::mutex> stateLock(g_lock);
	const bool debugLog = g_state.debugLog;
	if (!g_state.enabled) {
		return result;
	}
	for (const auto& [ownerId, bindingSet] : g_state.cooperativeBindingsByOwner) {
		for (const auto& candidate : bindingSet.bindings) {
			const auto& binding = candidate.binding;
			if (binding.device != cooperativeDevice ||
			    binding.primaryMappedKey != mappedKey ||
			    binding.triggerEdge != WheelerAPI::CooperativeOpeningTriggerEdge::kPrimaryDown ||
			    binding.priority <= g_state.mainPriority) {
				continue;
			}
			if (binding.modifierMappedKey != 0) {
				const bool modifierHeld = cooperativeDevice == WheelerAPI::CooperativeOpeningDevice::kGamepad ?
				                              Controls::IsGamepadKeyHeld(binding.modifierMappedKey) :
				                              Controls::IsMkbKeyHeld(binding.modifierMappedKey);
				if (!modifierHeld) {
					continue;
				}
			}
			if (!hasWinner || CandidateOutranks(candidate, ownerId, winningBinding, winningOwner)) {
				winningBinding = candidate;
				winningOwner = ownerId;
				winningGeneration = bindingSet.generation;
				hasWinner = true;
			}
		}
	}
	if (!hasWinner) {
		return result;
	}

	// In reversed hook order the winning consumer has already observed this
	// exact event. Never grant it back to an upstream consumer.
	if (WasObservedByUpstreamConsumer(winningOwner, eventIdentity)) {
		result.reason = Reason::kUpstreamConsumerAlreadyObserved;
		return result;
	}

	auto& producerFrame = g_cooperativeProducerFrames.back();
	if (std::any_of(producerFrame.grants.begin(), producerFrame.grants.end(), [&](const auto& grant) {
			return grant.eventIdentity == eventIdentity;
		})) {
		return result;
	}

	const auto grantToken = NextNonzero(g_nextCooperativeGrantToken);
	producerFrame.grants.push_back(CooperativeGrantRecord{
		.eventIdentity = eventIdentity,
		.device = cooperativeDevice,
		.mappedKey = mappedKey,
		.edge = WheelerAPI::CooperativeOpeningTriggerEdge::kPrimaryDown,
		.ownerId = winningOwner,
		.bindingGeneration = winningGeneration,
		.opaqueGrantToken = grantToken,
		.state = CooperativeGrantState::kPending,
		.mustSuppress = true
	});
	result.granted = true;
	result.ownerId = winningOwner;
	result.bindingGeneration = winningGeneration;
	result.opaqueGrantToken = grantToken;
	result.reason = Reason::kMatched;
	stateLock.unlock();
	if (debugLog) {
		logger::info(
			"[CooperativeOpening] grant dispatch={} owner={} device={} key={} modifier={} generation={}",
			producerFrame.dispatchGeneration,
			winningOwner,
			cooperativeDevice == WheelerAPI::CooperativeOpeningDevice::kGamepad ? "Gamepad" : "MKB",
			mappedKey,
			winningBinding.binding.modifierMappedKey,
			winningGeneration);
	}
	return result;
}
