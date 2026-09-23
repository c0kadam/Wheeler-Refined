#include "OStimUndressVisualRefresh.h"

#include "OStimNGThreadAPI.h"
#include "OStimStateTracker.h"
#include "bin/Config.h"

#include <RE/T/TESObjectARMO.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using OStimUndressVisualRefreshPolicy::ExecutionFacts;
	using OStimUndressVisualRefreshPolicy::ExecutionStatus;
	using OStimUndressVisualRefreshPolicy::FormID;
	using OStimUndressVisualRefreshPolicy::WornArmorSet;

	struct PendingNodeObservation
	{
		std::uint64_t eventRevision = 0;
		std::int64_t observedAtMs = 0;
	};

	struct PendingAppearanceRefresh
	{
		std::int64_t dueAtMs = 0;
		FormID actorFormID = 0;
		std::uint32_t threadID = 0;
		std::uint64_t generation = 0;
		std::uint64_t eventRevision = 0;
		std::string sceneID;
	};

	struct AppearanceState
	{
		bool featureWasEnabled = false;
		bool baselineActive = false;
		bool nativeThreadEnded = false;
		std::uint64_t generation = 1;
		std::uint64_t lastNativeEventRevision = 0;
		std::uint64_t latestObservedNodeRevision = 0;
		std::uint32_t threadID = 0;
		std::set<FormID> participantFormIDs;
		std::map<FormID, WornArmorSet> wornArmorByActor;
		std::map<FormID, std::uint64_t> latestQueuedEventByActor;
		std::optional<PendingNodeObservation> pendingNodeObservation;
		std::vector<PendingAppearanceRefresh> pendingRefreshes;
	};

	AppearanceState s_state;

	std::int64_t NowMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch())
			.count();
	}

	template <class... TArgs>
	void DebugLog(const char* a_format, TArgs&&... a_args)
	{
		if (Config::OStimIntegration::DebugLog) {
			logger::info(fmt::runtime(a_format), std::forward<TArgs>(a_args)...);
		}
	}

	std::set<FormID> CollectParticipantFormIDs(const OStimSceneInfo& a_scene)
	{
		std::set<FormID> result;
		for (const auto& participant : a_scene.participants) {
			if (participant.formID != 0) {
				result.insert(participant.formID);
			}
		}
		return result;
	}

	bool ContainsParticipant(const OStimSceneInfo& a_scene, FormID a_actorFormID)
	{
		return std::any_of(
			a_scene.participants.begin(),
			a_scene.participants.end(),
			[a_actorFormID](const OStimParticipantInfo& a_participant) {
				return a_participant.formID == a_actorFormID;
			});
	}

	std::optional<WornArmorSet> CaptureWornArmor(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return std::nullopt;
		}

		std::vector<OStimUndressVisualRefreshPolicy::ArmorObservation> observations;
		{
			const RE::TESObjectREFR::InventoryItemMap inventory = a_actor->GetInventory();
			observations.reserve(inventory.size());
			for (const auto& [object, data] : inventory) {
				auto* armor = object ? object->As<RE::TESObjectARMO>() : nullptr;
				auto* entry = data.second.get();
				if (!armor || !entry) {
					continue;
				}
				observations.push_back({
					armor->GetFormID(),
					armor->GetSlotMask().underlying(),
					entry->IsWorn()
				});
			}
		}
		return OStimUndressVisualRefreshPolicy::BuildWornArmorSet(observations);
	}

	std::string FormatFormIDs(const WornArmorSet& a_forms)
	{
		std::string result;
		for (const auto formID : a_forms) {
			if (!result.empty()) {
				result.push_back(',');
			}
			result += fmt::format("{:08X}", formID);
		}
		return result;
	}

	const char* ExecutionStatusName(ExecutionStatus a_status)
	{
		switch (a_status) {
		case ExecutionStatus::Ready:
			return "ready";
		case ExecutionStatus::SettingDisabled:
			return "setting_disabled";
		case ExecutionStatus::SceneInactive:
			return "scene_inactive";
		case ExecutionStatus::GenerationChanged:
			return "generation_changed";
		case ExecutionStatus::ThreadChanged:
			return "scene_changed";
		case ExecutionStatus::ActorNotParticipant:
			return "participant_removed";
		case ExecutionStatus::RequestSuperseded:
			return "superseded";
		case ExecutionStatus::ActorMissing:
			return "actor_missing";
		case ExecutionStatus::Actor3DUnloaded:
			return "actor_3d_unloaded";
		default:
			return "unknown";
		}
	}

	void InvalidateAppearanceState(bool a_clearPendingRefreshes)
	{
		++s_state.generation;
		s_state.baselineActive = false;
		s_state.threadID = 0;
		s_state.participantFormIDs.clear();
		s_state.wornArmorByActor.clear();
		s_state.latestQueuedEventByActor.clear();
		s_state.pendingNodeObservation.reset();
		s_state.latestObservedNodeRevision = 0;
		if (a_clearPendingRefreshes) {
			s_state.pendingRefreshes.clear();
		}
	}

	bool CaptureBaseline(const OStimSceneInfo& a_scene)
	{
		const auto participants = CollectParticipantFormIDs(a_scene);
		if (!a_scene.active || participants.empty()) {
			return false;
		}

		s_state.threadID = a_scene.threadID;
		s_state.participantFormIDs = participants;
		s_state.wornArmorByActor.clear();
		for (const auto actorFormID : participants) {
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorFormID);
			const auto wornArmor = CaptureWornArmor(actor);
			if (!wornArmor) {
				continue;
			}
			s_state.wornArmorByActor.emplace(actorFormID, *wornArmor);
			DebugLog(
				"[OStimDiag] UNDRESS_REFRESH_BASELINE actor={:08X} wornCount={}",
				actorFormID,
				wornArmor->size());
		}
		s_state.baselineActive = true;
		return true;
	}

	void QueueAppearanceRefresh(
		FormID a_actorFormID,
		const OStimSceneInfo& a_scene,
		const PendingNodeObservation& a_observation)
	{
		constexpr std::size_t kMaxPendingRefreshes = 64;
		if (s_state.pendingRefreshes.size() >= kMaxPendingRefreshes) {
			s_state.pendingRefreshes.erase(s_state.pendingRefreshes.begin());
		}
		s_state.latestQueuedEventByActor[a_actorFormID] = a_observation.eventRevision;
		s_state.pendingRefreshes.push_back(PendingAppearanceRefresh{
			a_observation.observedAtMs + OStimUndressVisualRefreshPolicy::kRefreshDelayMs,
			a_actorFormID,
			a_scene.threadID,
			s_state.generation,
			a_observation.eventRevision,
			a_scene.sceneID
		});
		DebugLog(
			"[OStimDiag] UNDRESS_REFRESH_QUEUED actor={:08X} delayMs={} generation={} eventRev={}",
			a_actorFormID,
			OStimUndressVisualRefreshPolicy::kRefreshDelayMs,
			s_state.generation,
			a_observation.eventRevision);
	}

	void ProcessNodeObservation(
		const PendingNodeObservation& a_observation,
		const OStimSceneInfo& a_scene)
	{
		s_state.latestObservedNodeRevision = (std::max)(
			s_state.latestObservedNodeRevision,
			a_observation.eventRevision);
		for (const auto actorFormID : s_state.participantFormIDs) {
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorFormID);
			const auto current = CaptureWornArmor(actor);
			if (!current) {
				continue;
			}

			auto previous = s_state.wornArmorByActor.find(actorFormID);
			if (previous == s_state.wornArmorByActor.end()) {
				s_state.wornArmorByActor.emplace(actorFormID, *current);
				DebugLog(
					"[OStimDiag] UNDRESS_REFRESH_BASELINE actor={:08X} wornCount={}",
					actorFormID,
					current->size());
				continue;
			}

			const auto removed = OStimUndressVisualRefreshPolicy::GetRemovedWornArmor(
				previous->second,
				*current);
			previous->second = *current;
			const auto queued = s_state.latestQueuedEventByActor.find(actorFormID);
			const bool duplicate = queued != s_state.latestQueuedEventByActor.end() &&
				queued->second == a_observation.eventRevision;
			if (!OStimUndressVisualRefreshPolicy::ShouldArmRefresh(
					Config::OStimIntegration::RefreshAppearanceAfterUndress,
					duplicate,
					removed.size())) {
				continue;
			}

			DebugLog(
				"[OStimDiag] UNDRESS_REFRESH_DETECTED actor={:08X} eventRev={} scene='{}' removed={} removedForms='{}'",
				actorFormID,
				a_observation.eventRevision,
				a_scene.sceneID,
				removed.size(),
				FormatFormIDs(removed));
			QueueAppearanceRefresh(actorFormID, a_scene, a_observation);
		}
	}

	void ProcessPendingRefreshes(
		bool a_settingEnabled,
		bool a_sceneActive,
		const std::optional<OStimSceneInfo>& a_scene)
	{
		const std::int64_t nowMs = NowMs();
		for (auto it = s_state.pendingRefreshes.begin(); it != s_state.pendingRefreshes.end();) {
			if (it->dueAtMs > nowMs) {
				++it;
				continue;
			}

			const PendingAppearanceRefresh pending = std::move(*it);
			it = s_state.pendingRefreshes.erase(it);
			const bool participant = a_scene && ContainsParticipant(*a_scene, pending.actorFormID);
			const auto latest = s_state.latestQueuedEventByActor.find(pending.actorFormID);
			const bool requestCurrent =
				latest != s_state.latestQueuedEventByActor.end() &&
				latest->second == pending.eventRevision &&
				s_state.latestObservedNodeRevision == pending.eventRevision;

			RE::Actor* actor = nullptr;
			if (a_settingEnabled && a_sceneActive &&
				pending.generation == s_state.generation &&
				a_scene && a_scene->threadID == pending.threadID &&
				participant && requestCurrent) {
				actor = RE::TESForm::LookupByID<RE::Actor>(pending.actorFormID);
			}

			const ExecutionStatus status = OStimUndressVisualRefreshPolicy::EvaluateExecution(ExecutionFacts{
				a_settingEnabled,
				a_sceneActive && a_scene && a_scene->active,
				pending.generation == s_state.generation,
				a_scene && a_scene->threadID == pending.threadID,
				participant,
				requestCurrent,
				actor != nullptr,
				actor && actor->Is3DLoaded()
			});
			if (status != ExecutionStatus::Ready) {
				DebugLog(
					"[OStimDiag] UNDRESS_REFRESH_CANCELLED actor={:08X} eventRev={} reason={}",
					pending.actorFormID,
					pending.eventRevision,
					ExecutionStatusName(status));
				continue;
			}

			actor->Update3DModel();
			DebugLog(
				"[OStimDiag] UNDRESS_REFRESH_EXECUTED actor={:08X} eventRev={} scene='{}'",
				pending.actorFormID,
				pending.eventRevision,
				pending.sceneID);
			s_state.latestQueuedEventByActor.erase(pending.actorFormID);
		}
	}
}

void OStimUndressVisualRefresh::Reset()
{
	s_state = AppearanceState{};
}

void OStimUndressVisualRefresh::Update(bool a_ostimAvailable, bool a_sceneActive)
{
	const bool settingEnabled =
		Config::OStimIntegration::Enabled &&
		Config::OStimIntegration::RefreshAppearanceAfterUndress;
	const auto configTransition = OStimUndressVisualRefreshPolicy::EvaluateConfigTransition(
		s_state.featureWasEnabled,
		settingEnabled,
		a_sceneActive);
	if (settingEnabled != s_state.featureWasEnabled) {
		s_state.featureWasEnabled = settingEnabled;
		InvalidateAppearanceState(false);
		s_state.lastNativeEventRevision = OStimNGThreadAPI::GetEventRevision();
		if (configTransition == OStimUndressVisualRefreshPolicy::ConfigTransition::CaptureBaseline) {
			if (const auto scene = OStimStateTracker::GetCurrentSceneInfo(); scene && scene->active) {
				CaptureBaseline(*scene);
			}
		}
	}

	if (!a_ostimAvailable || !a_sceneActive) {
		if (s_state.baselineActive ||
			s_state.pendingNodeObservation ||
			!s_state.pendingRefreshes.empty()) {
			InvalidateAppearanceState(true);
		}
		s_state.nativeThreadEnded = false;
		return;
	}

	if (!settingEnabled) {
		ProcessPendingRefreshes(false, true, std::nullopt);
		return;
	}

	std::optional<PendingNodeObservation> observedNode;
	const std::uint64_t nativeRevision = OStimNGThreadAPI::GetEventRevision();
	if (nativeRevision != s_state.lastNativeEventRevision) {
		s_state.lastNativeEventRevision = nativeRevision;
		switch (OStimNGThreadAPI::GetLastEvent()) {
		case OStimNGThreadAPI::RuntimeEvent::ThreadEnded:
			s_state.nativeThreadEnded = true;
			InvalidateAppearanceState(true);
			return;
		case OStimNGThreadAPI::RuntimeEvent::ThreadStarted:
			s_state.nativeThreadEnded = false;
			InvalidateAppearanceState(true);
			return;
		case OStimNGThreadAPI::RuntimeEvent::NodeChanged:
			s_state.nativeThreadEnded = false;
			observedNode = PendingNodeObservation{ nativeRevision, NowMs() };
			s_state.latestObservedNodeRevision = nativeRevision;
			break;
		default:
			break;
		}
	}
	if (s_state.nativeThreadEnded) {
		return;
	}

	const auto scene = OStimStateTracker::GetCurrentSceneInfo();
	if (!scene || !scene->active) {
		return;
	}
	const auto participants = CollectParticipantFormIDs(*scene);
	if (!s_state.baselineActive) {
		CaptureBaseline(*scene);
		return;
	}
	if (scene->threadID != s_state.threadID || participants != s_state.participantFormIDs) {
		InvalidateAppearanceState(false);
		CaptureBaseline(*scene);
		return;
	}

	if (s_state.pendingNodeObservation) {
		const PendingNodeObservation pending = observedNode ?
			*observedNode : *s_state.pendingNodeObservation;
		s_state.pendingNodeObservation.reset();
		ProcessNodeObservation(pending, *scene);
		observedNode.reset();
	}
	if (observedNode) {
		s_state.pendingNodeObservation = *observedNode;
	}
	ProcessPendingRefreshes(true, true, scene);
}
