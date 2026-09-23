#include "OStimStateTracker.h"

#include "OStimBridge.h"
#include "OStimNGThreadAPI.h"
#include "bin/Config.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>
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
		std::uint64_t nativeEventRevision = 0;
		std::uint64_t nativeEndRevision = 0;
	};

	constexpr std::size_t kActionCooldownSlots = 32;
	constexpr std::int64_t kPollMsActiveScene = 1000;
	constexpr std::int64_t kPollMsAvailableIdle = 1000;
	constexpr std::int64_t kPollMsUnavailable = 2000;
	constexpr std::int64_t kSceneDropGraceMs = 1250;

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

		if (snapshot.availability.hasNativeThreadAPI) {
			snapshot.sceneInfo = OStimNGThreadAPI::GetCurrentSceneInfo();
			if (snapshot.sceneInfo) {
				snapshot.sceneInfo->apiVersion = snapshot.availability.apiVersion;
			}
			if (snapshot.sceneInfo && snapshot.sceneInfo->active) {
				snapshot.lastConfirmedActiveSceneAtMs = snapshot.capturedAtMs;
				if (Config::OStimIntegration::AllowPositionBrowsing &&
					!snapshot.sceneInfo->inTransition &&
					!snapshot.sceneInfo->inSequence &&
					!snapshot.sceneInfo->playerControlDisabled) {
					snapshot.positions = OStimNGThreadAPI::GetNavigationPositions(*snapshot.sceneInfo);
				}
			}
			return snapshot;
		}

		snapshot.sceneInfo = OStimBridge::GetCurrentSceneInfo();
		if (snapshot.sceneInfo && snapshot.sceneInfo->active) {
			snapshot.lastConfirmedActiveSceneAtMs = snapshot.capturedAtMs;
			if (Config::OStimIntegration::AllowPositionBrowsing &&
				!snapshot.sceneInfo->inTransition &&
				!snapshot.sceneInfo->inSequence &&
				!snapshot.sceneInfo->playerControlDisabled) {
				snapshot.positions = OStimBridge::GetCandidatePositions(
					Config::OStimIntegration::PreferCurrentAnimationClass);
			}
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
		const bool hadActiveScene = previous.sceneInfo && previous.sceneInfo->active;
		const bool lostScene = (!a_snapshot.sceneInfo || !a_snapshot.sceneInfo->active) && hadActiveScene;
		if (lostScene &&
			a_snapshot.availability.available &&
			previousActiveSceneAtMs > 0 &&
			(a_snapshot.capturedAtMs - previousActiveSceneAtMs) <= kSceneDropGraceMs &&
			a_snapshot.nativeEndRevision == previous.nativeEndRevision) {
			a_snapshot.sceneInfo = previous.sceneInfo;
			a_snapshot.lastConfirmedActiveSceneAtMs = previousActiveSceneAtMs;
		}

		if (a_snapshot.sceneInfo && a_snapshot.sceneInfo->active && a_snapshot.lastConfirmedActiveSceneAtMs == 0) {
			a_snapshot.lastConfirmedActiveSceneAtMs = a_snapshot.capturedAtMs;
		}
		return a_snapshot;
	}

	bool ParticipantSemanticsEqual(
		const std::vector<OStimParticipantInfo>& a_lhs,
		const std::vector<OStimParticipantInfo>& a_rhs)
	{
		return a_lhs.size() == a_rhs.size() &&
		       std::equal(a_lhs.begin(), a_lhs.end(), a_rhs.begin(), [](const auto& a_left, const auto& a_right) {
			       return std::tie(a_left.formID, a_left.name, a_left.isPlayer) ==
			              std::tie(a_right.formID, a_right.name, a_right.isPlayer);
		       });
	}

	bool NavigationSemanticsEqual(
		const std::vector<OStimPositionInfo>& a_lhs,
		const std::vector<OStimPositionInfo>& a_rhs)
	{
		return a_lhs.size() == a_rhs.size() &&
		       std::equal(a_lhs.begin(), a_lhs.end(), a_rhs.begin(), [](const auto& a_left, const auto& a_right) {
			       return std::tie(
					          a_left.id,
					          a_left.displayName,
					          a_left.category,
					          a_left.subcategory,
					          a_left.sourceSceneID,
					          a_left.destinationID,
					          a_left.description,
					          a_left.previewPath,
					          a_left.iconPath,
					          a_left.isValidNow,
					          a_left.requiresActiveScene,
					          a_left.isTransition) ==
			              std::tie(
					          a_right.id,
					          a_right.displayName,
					          a_right.category,
					          a_right.subcategory,
					          a_right.sourceSceneID,
					          a_right.destinationID,
					          a_right.description,
					          a_right.previewPath,
					          a_right.iconPath,
					          a_right.isValidNow,
					          a_right.requiresActiveScene,
					          a_right.isTransition);
		       });
	}

	std::string DescribeSemanticChanges(const Snapshot& a_previous, const Snapshot& a_current)
	{
		std::string changed;
		auto record = [&](bool a_changed, std::string_view a_name) {
			if (!a_changed) {
				return;
			}
			if (!changed.empty()) {
				changed.push_back(',');
			}
			changed.append(a_name);
		};

		record(
			std::tie(
				a_previous.availability.available,
				a_previous.availability.hasDatabase,
				a_previous.availability.hasNativeThreadAPI,
				a_previous.availability.apiVersion,
				a_previous.availability.reason) !=
				std::tie(
					a_current.availability.available,
					a_current.availability.hasDatabase,
					a_current.availability.hasNativeThreadAPI,
					a_current.availability.apiVersion,
					a_current.availability.reason),
			"availability");
		record(a_previous.sceneInfo.has_value() != a_current.sceneInfo.has_value(), "scenePresence");

		const OStimSceneInfo emptyScene{};
		const auto& previousScene = a_previous.sceneInfo ? *a_previous.sceneInfo : emptyScene;
		const auto& currentScene = a_current.sceneInfo ? *a_current.sceneInfo : emptyScene;
		record(
			std::tie(
				previousScene.active,
				previousScene.threadID,
				previousScene.sceneID,
				previousScene.animationID,
				previousScene.animationName,
				previousScene.animationClass,
				previousScene.positionData,
				previousScene.sourceModule,
				previousScene.currentOID) !=
				std::tie(
					currentScene.active,
					currentScene.threadID,
					currentScene.sceneID,
					currentScene.animationID,
					currentScene.animationName,
					currentScene.animationClass,
					currentScene.positionData,
					currentScene.sourceModule,
					currentScene.currentOID),
			"scene");
		record(
			std::tie(previousScene.currentSpeed, previousScene.maxSpeed) !=
				std::tie(currentScene.currentSpeed, currentScene.maxSpeed),
			"speed");
		record(
			std::tie(
				previousScene.playerInvolved,
				previousScene.aggressive,
				previousScene.inTransition,
				previousScene.inSequence,
				previousScene.playerControlDisabled,
				previousScene.autoMode) !=
				std::tie(
					currentScene.playerInvolved,
					currentScene.aggressive,
					currentScene.inTransition,
					currentScene.inSequence,
					currentScene.playerControlDisabled,
					currentScene.autoMode),
			"flags");
		record(previousScene.metadata != currentScene.metadata, "metadata");
		record(!ParticipantSemanticsEqual(previousScene.participants, currentScene.participants), "participants");
		record(!NavigationSemanticsEqual(a_previous.positions, a_current.positions), "navigation");
		return changed;
	}

	bool PublishSnapshotIfGenerationCurrent(
		Snapshot&& a_snapshot,
		std::uint64_t a_generation,
		std::int64_t a_nextPollDelayMs)
	{
		const bool diagnosticsEnabled = Config::OStimIntegration::DebugLog;
		std::string changed;
		std::string sceneID;
		std::string nodeName;
		std::size_t navigationCount = 0;
		std::uint64_t nativeRevision = 0;
		std::uint64_t revision = 0;
		std::string backend;
		std::vector<OStimPositionInfo> publishedPositions;
		bool semanticChanged = false;
		{
			std::lock_guard lock(s_snapshotLock);
			if (a_generation != s_generation.load(std::memory_order_acquire)) {
				return false;
			}
			changed = DescribeSemanticChanges(s_snapshot, a_snapshot);
			semanticChanged = !changed.empty();
			s_snapshot = std::move(a_snapshot);
			if (semanticChanged) {
				revision = s_revision.fetch_add(1, std::memory_order_release) + 1;
			} else {
				revision = s_revision.load(std::memory_order_acquire);
			}
			if (diagnosticsEnabled && semanticChanged) {
				if (s_snapshot.sceneInfo) {
					sceneID = s_snapshot.sceneInfo->sceneID;
					nodeName = s_snapshot.sceneInfo->animationName;
				}
				navigationCount = s_snapshot.positions.size();
				nativeRevision = s_snapshot.nativeEventRevision;
				backend = s_snapshot.availability.hasNativeThreadAPI ? "NativeThreadAPI" : "LegacyPapyrus";
				publishedPositions = s_snapshot.positions;
			}
			s_nextPollAtMs.store(NowMs() + a_nextPollDelayMs, std::memory_order_release);
		}

		if (diagnosticsEnabled && semanticChanged) {
			logger::info(
				"[OStimDiag] TRACKER_PUBLISH rev={} eventRev={} backend={} scene='{}' node='{}' nav={} changed='{}'",
				revision,
				nativeRevision,
				backend,
				sceneID,
				nodeName,
				navigationCount,
				changed);
			logger::info(
				"[OStimDiag] NAV_SNAPSHOT trackerRev={} eventRev={} backend={} sceneID='{}' count={}",
				revision,
				nativeRevision,
				backend,
				sceneID,
				publishedPositions.size());
			for (std::size_t index = 0; index < publishedPositions.size(); ++index) {
				const auto& position = publishedPositions[index];
				logger::info(
					"[OStimDiag] NAV_SNAPSHOT_ITEM index={} sceneID='{}' destinationID='{}' sourceSceneID='{}' transition={}",
					index,
					position.id,
					position.destinationID,
					position.sourceSceneID,
					position.isTransition ? 1 : 0);
			}
		}
		return semanticChanged;
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
		PublishSnapshotIfGenerationCurrent(std::move(snapshot), generation, nextPollDelayMs);
		if (generation == s_generation.load(std::memory_order_acquire)) {
			s_queryPending.store(false, std::memory_order_release);
		}
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

OStimTrackerSnapshot OStimStateTracker::GetSnapshot()
{
	std::lock_guard lock(s_snapshotLock);
	OStimTrackerSnapshot snapshot{};
	snapshot.availability = s_snapshot.availability;
	snapshot.sceneInfo = s_snapshot.sceneInfo;
	snapshot.positions = s_snapshot.positions;
	snapshot.revision = s_revision.load(std::memory_order_acquire);
	return snapshot;
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
