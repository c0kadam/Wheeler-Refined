#include "OStimStateTracker.h"

#include "OStimBridge.h"
#include "OStimNGThreadAPI.h"
#include "bin/Config.h"

#include <SKSE/SKSE.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <utility>

namespace
{
	using Clock = std::chrono::steady_clock;

	struct Snapshot
	{
		OStimAvailabilityInfo availability{};
		std::optional<OStimSceneInfo> sceneInfo{};
		std::vector<OStimPositionInfo> positions{};
		std::int64_t capturedAtMs = 0;
		std::int64_t lastConfirmedActiveSceneAtMs = 0;
		std::int64_t lastConfirmedPositionsAtMs = 0;
		std::uint64_t nativeEventRevision = 0;
		std::uint64_t nativeEndRevision = 0;
	};

	constexpr std::size_t kActionCooldownSlots = 32;
	constexpr std::int64_t kPollMsActiveScene = 250;
	constexpr std::int64_t kPollMsAvailableIdle = 1000;
	constexpr std::int64_t kPollMsUnavailable = 2000;
	constexpr std::int64_t kSceneDropGraceMs = 1250;
	constexpr std::int64_t kPositionDropGraceMs = 900;

	std::mutex s_snapshotLock;
	Snapshot s_snapshot;
	std::atomic_bool s_queryPending{ false };
	std::atomic_bool s_forceRefreshRequested{ true };
	std::atomic<std::uint64_t> s_revision{ 0 };
	std::atomic<std::uint64_t> s_generation{ 0 };
	std::atomic<std::int64_t> s_nextPollAtMs{ 0 };
	std::atomic<std::uint64_t> s_lastObservedNativeRevision{ 0 };
	std::array<std::atomic<std::int64_t>, kActionCooldownSlots> s_nextAllowedActionMs{};

	std::int64_t NowMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			Clock::now().time_since_epoch())
			.count();
	}

	std::size_t ToActionIndex(OStimActionKind a_action)
	{
		const auto raw = static_cast<std::size_t>(a_action);
		return raw < kActionCooldownSlots ? raw : 0;
	}

	std::int64_t GetActionCooldownMs(OStimActionKind a_action)
	{
		switch (a_action) {
		case OStimActionKind::IncreaseSpeed:
		case OStimActionKind::DecreaseSpeed:
			return 200;
		case OStimActionKind::NextPosition:
		case OStimActionKind::PreviousPosition:
		case OStimActionKind::SelectSpecificPosition:
			return 350;
		case OStimActionKind::StopScene:
			return 500;
		default:
			return 150;
		}
	}

	std::int64_t GetNextPollDelayMs(const Snapshot& a_snapshot)
	{
		if (a_snapshot.sceneInfo && a_snapshot.sceneInfo->active) {
			return kPollMsActiveScene;
		}
		if (a_snapshot.availability.available) {
			return kPollMsAvailableIdle;
		}
		return kPollMsUnavailable;
	}

	Snapshot BuildSnapshot()
	{
		Snapshot snapshot{};
		snapshot.capturedAtMs = NowMs();
		snapshot.nativeEventRevision = OStimNGThreadAPI::GetEventRevision();
		snapshot.nativeEndRevision = OStimNGThreadAPI::GetLastEndedRevision();
		snapshot.availability = OStimBridge::GetAvailability();
		if (!snapshot.availability.available) {
			return snapshot;
		}

		snapshot.sceneInfo = OStimBridge::GetCurrentSceneInfo();
		if (snapshot.sceneInfo &&
			snapshot.sceneInfo->active &&
			Config::OStimIntegration::AllowPositionBrowsing) {
			snapshot.lastConfirmedActiveSceneAtMs = snapshot.capturedAtMs;
			snapshot.positions = OStimBridge::GetCandidatePositions(
				Config::OStimIntegration::PreferCurrentAnimationClass);
		}
		if (snapshot.sceneInfo && snapshot.sceneInfo->active && snapshot.lastConfirmedActiveSceneAtMs == 0) {
			snapshot.lastConfirmedActiveSceneAtMs = snapshot.capturedAtMs;
		}
		if (!snapshot.positions.empty()) {
			snapshot.lastConfirmedPositionsAtMs = snapshot.capturedAtMs;
		}

		return snapshot;
	}

	Snapshot StabilizeSnapshot(Snapshot a_snapshot)
	{
		Snapshot previous;
		{
			std::lock_guard lock(s_snapshotLock);
			previous = s_snapshot;
		}

		const std::int64_t previousActiveSceneAtMs = previous.lastConfirmedActiveSceneAtMs > 0 ?
			previous.lastConfirmedActiveSceneAtMs :
			((previous.sceneInfo && previous.sceneInfo->active) ? previous.capturedAtMs : 0);
		const std::int64_t previousPositionsAtMs = previous.lastConfirmedPositionsAtMs > 0 ?
			previous.lastConfirmedPositionsAtMs :
			(!previous.positions.empty() ? previous.capturedAtMs : 0);

		const bool hadActiveScene = previous.sceneInfo && previous.sceneInfo->active;
		const bool lostScene = (!a_snapshot.sceneInfo || !a_snapshot.sceneInfo->active) && hadActiveScene;
		if (lostScene &&
			a_snapshot.availability.available &&
			previousActiveSceneAtMs > 0 &&
			(a_snapshot.capturedAtMs - previousActiveSceneAtMs) <= kSceneDropGraceMs &&
			a_snapshot.nativeEndRevision == previous.nativeEndRevision) {
			a_snapshot.sceneInfo = previous.sceneInfo;
			if (a_snapshot.positions.empty()) {
				a_snapshot.positions = previous.positions;
			}
			a_snapshot.lastConfirmedActiveSceneAtMs = previousActiveSceneAtMs;
			if (!a_snapshot.positions.empty()) {
				a_snapshot.lastConfirmedPositionsAtMs = previousPositionsAtMs;
			}
		}

		const bool sameActiveScene = a_snapshot.sceneInfo &&
			previous.sceneInfo &&
			a_snapshot.sceneInfo->active &&
			previous.sceneInfo->active &&
			a_snapshot.sceneInfo->sceneID == previous.sceneInfo->sceneID;
		if (sameActiveScene &&
			a_snapshot.positions.empty() &&
			!previous.positions.empty() &&
			previousPositionsAtMs > 0 &&
			(a_snapshot.capturedAtMs - previousPositionsAtMs) <= kPositionDropGraceMs) {
			a_snapshot.positions = previous.positions;
			a_snapshot.lastConfirmedPositionsAtMs = previousPositionsAtMs;
		}

		if (a_snapshot.sceneInfo && a_snapshot.sceneInfo->active && a_snapshot.lastConfirmedActiveSceneAtMs == 0) {
			a_snapshot.lastConfirmedActiveSceneAtMs = a_snapshot.capturedAtMs;
		}
		if (!a_snapshot.positions.empty() && a_snapshot.lastConfirmedPositionsAtMs == 0) {
			a_snapshot.lastConfirmedPositionsAtMs = a_snapshot.capturedAtMs;
		}

		return a_snapshot;
	}

	void PublishSnapshot(Snapshot&& a_snapshot)
	{
		{
			std::lock_guard lock(s_snapshotLock);
			s_snapshot = std::move(a_snapshot);
		}
		s_revision.fetch_add(1, std::memory_order_release);
	}
}

void OStimStateTracker::Reset()
{
	OStimNGThreadAPI::Reset();
	s_generation.fetch_add(1, std::memory_order_acq_rel);
	s_queryPending.store(false, std::memory_order_release);
	s_forceRefreshRequested.store(true, std::memory_order_release);
	s_nextPollAtMs.store(0, std::memory_order_release);
	s_lastObservedNativeRevision.store(0, std::memory_order_release);
	{
		std::lock_guard lock(s_snapshotLock);
		s_snapshot = Snapshot{};
	}
	s_revision.fetch_add(1, std::memory_order_release);
	for (auto& slot : s_nextAllowedActionMs) {
		slot.store(0, std::memory_order_release);
	}
}

void OStimStateTracker::Update(bool a_force)
{
	if (a_force) {
		s_forceRefreshRequested.store(true, std::memory_order_release);
		s_nextPollAtMs.store(0, std::memory_order_release);
	}

	const auto nativeRevision = OStimNGThreadAPI::GetEventRevision();
	if (nativeRevision != s_lastObservedNativeRevision.exchange(nativeRevision, std::memory_order_acq_rel)) {
		s_forceRefreshRequested.store(true, std::memory_order_release);
		s_nextPollAtMs.store(0, std::memory_order_release);
	}

	if (s_queryPending.load(std::memory_order_acquire)) {
		return;
	}

	const std::int64_t nowMs = NowMs();
	if (!s_forceRefreshRequested.load(std::memory_order_acquire) &&
		nowMs < s_nextPollAtMs.load(std::memory_order_acquire)) {
		return;
	}

	auto* taskInterface = SKSE::GetTaskInterface();
	if (!taskInterface) {
		return;
	}

	const std::uint64_t generation = s_generation.load(std::memory_order_acquire);
	s_queryPending.store(true, std::memory_order_release);
	s_forceRefreshRequested.store(false, std::memory_order_release);

	taskInterface->AddTask([generation]() {
		Snapshot snapshot = StabilizeSnapshot(BuildSnapshot());
		const std::int64_t nextPollDelayMs = GetNextPollDelayMs(snapshot);
		if (generation == s_generation.load(std::memory_order_acquire)) {
			PublishSnapshot(std::move(snapshot));
			s_nextPollAtMs.store(NowMs() + nextPollDelayMs, std::memory_order_release);
		}
		s_queryPending.store(false, std::memory_order_release);
	});
}

void OStimStateTracker::InvalidatePositions()
{
	s_forceRefreshRequested.store(true, std::memory_order_release);
	s_nextPollAtMs.store(0, std::memory_order_release);
}

void OStimStateTracker::MarkActionExecuted(OStimActionKind a_action)
{
	const std::int64_t nowMs = NowMs();
	s_nextAllowedActionMs[ToActionIndex(a_action)].store(
		nowMs + GetActionCooldownMs(a_action),
		std::memory_order_release);
	s_forceRefreshRequested.store(true, std::memory_order_release);
	s_nextPollAtMs.store(nowMs + 125, std::memory_order_release);
}

OStimAvailabilityInfo OStimStateTracker::GetAvailability()
{
	std::lock_guard lock(s_snapshotLock);
	return s_snapshot.availability;
}

bool OStimStateTracker::IsSceneActive()
{
	std::lock_guard lock(s_snapshotLock);
	return s_snapshot.sceneInfo && s_snapshot.sceneInfo->active;
}

std::optional<OStimSceneInfo> OStimStateTracker::GetCurrentSceneInfo()
{
	std::lock_guard lock(s_snapshotLock);
	return s_snapshot.sceneInfo;
}

std::vector<OStimPositionInfo> OStimStateTracker::GetAvailablePositions()
{
	std::lock_guard lock(s_snapshotLock);
	return s_snapshot.positions;
}

bool OStimStateTracker::CanDispatchByCooldown(OStimActionKind a_action)
{
	return NowMs() >=
	       s_nextAllowedActionMs[ToActionIndex(a_action)].load(std::memory_order_acquire);
}

std::uint64_t OStimStateTracker::GetRevision()
{
	return s_revision.load(std::memory_order_acquire);
}
