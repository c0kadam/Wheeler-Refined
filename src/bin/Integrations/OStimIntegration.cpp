#include "OStimIntegration.h"

#include "OStimAutoModePolicy.h"
#include "OStimBridge.h"
#include "OStimNGSceneAPI.h"
#include "OStimNGThreadAPI.h"
#include "OStimSceneActionClosePolicy.h"
#include "OStimStateTracker.h"
#include "OStimUndressVisualRefresh.h"
#include "OStimUnifiedWheelModel.h"
#include "bin/API/WheelerAPI.h"
#include "bin/Config.h"
#include "bin/Integrations/ActionHotkeysBridge.h"
#include "bin/UserInput/Controls.h"
#include "bin/Wheeler/TransformWheelManager.h"
#include "bin/Wheeler/Wheel.h"
#include "bin/Wheeler/WheelEntry.h"
#include "bin/Wheeler/WheelItems/WheelItemFactory.h"
#include "bin/Wheeler/Wheeler.h"

#include <RE/E/ExtraUniqueID.h>
#include <RE/E/ExtraWorn.h>
#include <RE/E/ExtraWornLeft.h>
#include <RE/T/TESObjectARMO.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	constexpr std::string_view kControlWheelTag = "OStimIntegration.Control";
	constexpr std::string_view kBrowserWheelTag = "OStimIntegration.Browser";
	constexpr std::string_view kLegacyBrowserWheelTagPrefix = "OStimIntegration.Browser.";
	constexpr std::array<OStimActionKind, OStimUnifiedWheel::kFixedControlCount> kFixedControlActions{
		OStimActionKind::StopScene,
		OStimActionKind::DecreaseSpeed,
		OStimActionKind::ToggleAutoMode,
		OStimActionKind::IncreaseSpeed
	};

	enum class DiagnosticSequenceSource
	{
		NativeEvent,
		Action
	};

	struct PendingEquipmentSnapshot
	{
		std::int64_t dueAtMs = 0;
		std::uint64_t sequenceID = 0;
		DiagnosticSequenceSource source = DiagnosticSequenceSource::NativeEvent;
		std::string trigger;
		std::vector<OStimParticipantInfo> participants;
	};

	struct IntegrationState
	{
		bool refreshRequested = true;
		bool wheelRebuildRequested = true;
		bool lastSceneActive = false;
		bool lastAvailable = false;
		std::uint64_t lastAppliedRevision = 0;
		std::optional<int> previousWheelIndex;
		std::uint32_t browserPage = 0;
		std::int32_t browserFocusIndex = -1;
		std::size_t lastUnifiedEntryCount = 0;
		std::uint64_t unifiedLayoutRevision = 0;
		bool acceptedNavigationPending = false;
		std::string acceptedNavigationSourceSceneID;
		bool suppressManagedWheelsUntilSceneStops = false;
		std::string lastAvailabilityReason;
		bool diagnosticsEnabled = false;
		std::uint64_t lastDiagnosticNativeRevision = 0;
		std::vector<PendingEquipmentSnapshot> pendingEquipmentSnapshots;
	};

	struct UnifiedWheelLayout
	{
		std::vector<std::shared_ptr<WheelItem>> items;
		int previousPageIndex = -1;
		int nextPageIndex = -1;
		int firstPositionIndex = -1;
		int lastPositionIndex = -1;
		std::uint32_t page = 0;
	};

	enum class UnifiedFocusHint
	{
		None,
		PreserveCurrent,
		PreviousPage,
		NextPage
	};

	IntegrationState s_state;
	std::atomic<std::uint64_t> s_nextActionID{ 0 };

	bool IsStaleManagedWheelPayload(const OStimActionPayload* a_payload)
	{
		return a_payload &&
		       a_payload->wheelLayoutRevision != 0 &&
		       !OStimUnifiedWheel::IsCurrentLayoutRevision(
			       a_payload->wheelLayoutRevision,
			       s_state.unifiedLayoutRevision);
	}

	void ClearAcceptedNavigationPending()
	{
		s_state.acceptedNavigationPending = false;
		s_state.acceptedNavigationSourceSceneID.clear();
	}

	bool ShouldHoldAcceptedNavigation(const OStimTrackerSnapshot& a_snapshot)
	{
		const bool sceneActive = a_snapshot.sceneInfo && a_snapshot.sceneInfo->active;
		const bool currentSceneMatchesSource = sceneActive &&
			a_snapshot.sceneInfo->sceneID == s_state.acceptedNavigationSourceSceneID;
		return OStimSceneActionUI::ShouldHoldAcceptedNavigation(
			s_state.acceptedNavigationPending,
			sceneActive,
			currentSceneMatchesSource);
	}

	template <class... TArgs>
	void DebugLog(const char* a_format, TArgs&&... a_args)
	{
		if (Config::OStimIntegration::DebugLog) {
			logger::info(fmt::runtime(a_format), std::forward<TArgs>(a_args)...);
		}
	}

	bool IsSceneBlockedByMenus()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return true;
		}

		static constexpr std::array<std::string_view, 11> kBlockingMenus{
			RE::LoadingMenu::MENU_NAME,
			RE::MapMenu::MENU_NAME,
			RE::TweenMenu::MENU_NAME,
			RE::InventoryMenu::MENU_NAME,
			RE::MagicMenu::MENU_NAME,
			RE::FavoritesMenu::MENU_NAME,
			RE::Console::MENU_NAME,
			RE::MainMenu::MENU_NAME,
			RE::JournalMenu::MENU_NAME,
			RE::LockpickingMenu::MENU_NAME,
			RE::DialogueMenu::MENU_NAME
		};

		if (ui->GameIsPaused()) {
			return true;
		}
		for (auto menuName : kBlockingMenus) {
			if (ui->IsMenuOpen(menuName.data())) {
				return true;
			}
		}
		return Controls::IsRebindActive() || Wheeler::IsInEditMode();
	}

	std::optional<int> FindWheelIndexByTag(std::string_view a_tag)
	{
		std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		const int wheelCount = Wheeler::GetWheelCount();
		for (int i = 0; i < wheelCount; ++i) {
			Wheel* wheel = Wheeler::GetWheelByIndex(i);
			if (wheel && wheel->GetClientTag() == a_tag) {
				return i;
			}
		}
		return std::nullopt;
	}

	bool IsManagedTagInternal(std::string_view a_tag)
	{
		return a_tag == kControlWheelTag ||
		       a_tag == kBrowserWheelTag ||
		       a_tag.starts_with(kLegacyBrowserWheelTagPrefix);
	}

	void TagWheelIndex(int a_wheelIndex, std::string_view a_tag)
	{
		std::unique_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		if (Wheel* wheel = Wheeler::GetWheelByIndex(a_wheelIndex)) {
			wheel->SetClientTag(a_tag);
		}
	}

	std::vector<int> CollectManagedWheelIndices()
	{
		std::vector<int> indices;
		std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		const int wheelCount = Wheeler::GetWheelCount();
		indices.reserve(static_cast<std::size_t>(wheelCount));
		for (int i = 0; i < wheelCount; ++i) {
			Wheel* wheel = Wheeler::GetWheelByIndex(i);
			if (wheel && IsManagedTagInternal(wheel->GetClientTag())) {
				indices.push_back(i);
			}
		}
		return indices;
	}

	std::optional<int> FindFallbackUserWheelIndex()
	{
		std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		const int wheelCount = Wheeler::GetWheelCount();
		for (int i = 0; i < wheelCount; ++i) {
			Wheel* wheel = Wheeler::GetWheelByIndex(i);
			if (!wheel) {
				continue;
			}
			if (ActionHotkeysBridge::IsBridgeWheelTag(wheel->GetClientTag()) ||
				TransformWheelManager::IsTransformWheelIndex(i) ||
				WheelerAPI::IsManagedWheelIndex(i)) {
				continue;
			}
			return i;
		}
		return std::nullopt;
	}

	bool SwitchOrSetActiveWheel(int a_wheelIndex)
	{
		if (a_wheelIndex < 0 || a_wheelIndex >= Wheeler::GetWheelCount()) {
			return false;
		}

		if (Wheeler::IsWheelerOpen()) {
			return Wheeler::SwitchToWheelIndexForNavigation(a_wheelIndex);
		}
		Wheeler::SetActiveWheelIndex(a_wheelIndex);
		return true;
	}

	std::optional<int> GetWheelHoveredEntryIndex(std::string_view a_tag)
	{
		auto wheelIndex = FindWheelIndexByTag(a_tag);
		if (!wheelIndex.has_value()) {
			return std::nullopt;
		}

		std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		if (Wheel* wheel = Wheeler::GetWheelByIndex(*wheelIndex)) {
			return wheel->GetHoveredEntryIndex();
		}
		return std::nullopt;
	}

	bool IsActiveWheelTagged(std::string_view a_tag)
	{
		const int activeIdx = Wheeler::GetActiveWheelIndex();
		if (activeIdx < 0) {
			return false;
		}

		std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		if (Wheel* wheel = Wheeler::GetWheelByIndex(activeIdx)) {
			return wheel->GetClientTag() == a_tag;
		}
		return false;
	}

	std::int64_t DiagnosticNowMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch())
			.count();
	}

	constexpr int Bool01(bool a_value)
	{
		return a_value ? 1 : 0;
	}

	const char* RuntimeEventName(OStimNGThreadAPI::RuntimeEvent a_event)
	{
		switch (a_event) {
		case OStimNGThreadAPI::RuntimeEvent::ThreadStarted:
			return "ThreadStarted";
		case OStimNGThreadAPI::RuntimeEvent::ThreadEnded:
			return "ThreadEnded";
		case OStimNGThreadAPI::RuntimeEvent::NodeChanged:
			return "NodeChanged";
		case OStimNGThreadAPI::RuntimeEvent::ControlInput:
			return "ControlInput";
		default:
			return "None";
		}
	}

	bool IsNavigationAction(OStimActionKind a_kind)
	{
		return a_kind == OStimActionKind::SelectSpecificPosition;
	}

	const char* DiagnosticSourceName(DiagnosticSequenceSource a_source)
	{
		return a_source == DiagnosticSequenceSource::Action ? "Action" : "NativeEvent";
	}

	std::string GetActiveManagedWheelTag()
	{
		const int activeIndex = Wheeler::GetActiveWheelIndex();
		if (activeIndex < 0) {
			return {};
		}

		std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		if (Wheel* wheel = Wheeler::GetWheelByIndex(activeIndex)) {
			const auto& tag = wheel->GetClientTag();
			if (IsManagedTagInternal(tag)) {
				return std::string(tag);
			}
		}
		return {};
	}

	OStimTrackerSnapshot GetDiagnosticSnapshot()
	{
		return OStimStateTracker::GetSnapshot();
	}

	void LogStateSnapshot(
		std::string_view a_trigger,
		std::uint64_t a_nativeRevision,
		const std::optional<OStimSceneInfo>& a_scene,
		std::size_t a_navigationCount,
		std::optional<std::uint32_t> a_threadID = std::nullopt)
	{
		const OStimSceneInfo emptyScene{};
		const auto& scene = a_scene ? *a_scene : emptyScene;
		const int participantCount = scene.participantCount > 0 ?
			scene.participantCount :
			static_cast<int>(scene.participants.size());
		DebugLog(
			"[OStimDiag] STATE trigger='{}' nativeRev={} trackerRev={} thread={} scene='{}' animation='{}' node='{}' active={} participants={} speed={}/{} transition={} sequence={} controlDisabled={} auto={} nav={} browserPage={} wheel='{}'",
			a_trigger,
			a_nativeRevision,
			OStimStateTracker::GetRevision(),
			a_threadID.value_or(scene.threadID),
			scene.sceneID,
			scene.animationID,
			scene.animationName,
			Bool01(scene.active),
			participantCount,
			scene.currentSpeed,
			scene.maxSpeed,
			Bool01(scene.inTransition),
			Bool01(scene.inSequence),
			Bool01(scene.playerControlDisabled),
			Bool01(scene.autoMode),
			a_navigationCount,
			s_state.browserPage,
			GetActiveManagedWheelTag());
	}

	void LogNavigationSnapshot(
		std::uint32_t a_threadID,
		const std::vector<OStimPositionInfo>& a_positions)
	{
		DebugLog("[OStimDiag] NAV_SNAPSHOT source=Action thread={} count={}", a_threadID, a_positions.size());
		for (std::size_t index = 0; index < a_positions.size(); ++index) {
			const auto& position = a_positions[index];
			DebugLog(
				"[OStimDiag] NAV_SNAPSHOT_ITEM source=Action idx={} sceneID='{}' destinationID='{}' transition={} valid={} label='{}' description='{}'",
				index,
				position.id,
				position.destinationID,
				Bool01(position.isTransition),
				Bool01(position.isValidNow),
				position.displayName,
				position.description);
		}
	}

	struct WornExtraListDiagnostic
	{
		std::size_t ordinal = 0;
		bool extraWorn = false;
		bool extraWornLeft = false;
		bool hasUniqueID = false;
		std::uint16_t uniqueID = 0;
	};

	void LogActorEquipment(
		RE::Actor* a_actor,
		const OStimParticipantInfo& a_participant,
		std::string_view a_trigger)
	{
		const char* actorName = a_actor && a_actor->GetName() ?
			a_actor->GetName() :
			a_participant.name.c_str();
		DebugLog(
			"[OStimDiag] EQUIP actor={:08X} name='{}' player={} trigger='{}' resolved={}",
			a_participant.formID,
			actorName ? actorName : "",
			Bool01(a_participant.isPlayer),
			a_trigger,
			Bool01(a_actor != nullptr));
		if (!a_actor) {
			return;
		}

		const RE::TESObjectREFR::InventoryItemMap inventory = a_actor->GetInventory();
		for (const auto& [object, data] : inventory) {
			auto* armor = object ? object->As<RE::TESObjectARMO>() : nullptr;
			auto* entry = data.second.get();
			if (!armor || !entry) {
				continue;
			}

			const bool isWorn = entry->IsWorn();
			std::vector<WornExtraListDiagnostic> wornLists;
			std::size_t extraListCount = 0;
			bool wornLeft = false;
			if (entry->extraLists) {
				for (auto* extraList : *entry->extraLists) {
					const std::size_t ordinal = extraListCount++;
					if (!extraList) {
						continue;
					}

					WornExtraListDiagnostic detail{};
					detail.ordinal = ordinal;
					detail.extraWorn = extraList->HasType(RE::ExtraDataType::kWorn);
					detail.extraWornLeft = extraList->HasType(RE::ExtraDataType::kWornLeft);
					wornLeft = wornLeft || detail.extraWornLeft;
					if (!detail.extraWorn && !detail.extraWornLeft) {
						continue;
					}
					if (auto* uniqueID = extraList->GetByType<RE::ExtraUniqueID>()) {
						detail.hasUniqueID = true;
						detail.uniqueID = uniqueID->uniqueID;
					}
					wornLists.push_back(detail);
				}
			}

			if (!isWorn && wornLists.empty()) {
				continue;
			}

			const char* armorName = armor->GetName();
			const std::uint32_t slotMask = armor->GetSlotMask().underlying();
			if (wornLists.empty()) {
				DebugLog(
					"[OStimDiag] EQUIP_ITEM actor={:08X} form={:08X} name='{}' count={} slotMask={:08X} isWorn={} wornLeft={} xLists={} x=-1 uid=0 hasUID=0 extraWorn=0 extraWornLeft=0",
					a_participant.formID,
					armor->GetFormID(),
					armorName ? armorName : "",
					data.first,
					slotMask,
					Bool01(isWorn),
					Bool01(wornLeft),
					extraListCount);
				continue;
			}

			for (const auto& detail : wornLists) {
				DebugLog(
					"[OStimDiag] EQUIP_ITEM actor={:08X} form={:08X} name='{}' count={} slotMask={:08X} isWorn={} wornLeft={} xLists={} x={} uid={} hasUID={} extraWorn={} extraWornLeft={}",
					a_participant.formID,
					armor->GetFormID(),
					armorName ? armorName : "",
					data.first,
					slotMask,
					Bool01(isWorn),
					Bool01(wornLeft),
					extraListCount,
					detail.ordinal,
					detail.uniqueID,
					Bool01(detail.hasUniqueID),
					Bool01(detail.extraWorn),
					Bool01(detail.extraWornLeft));
			}
		}
	}

	void LogEquipmentSnapshot(
		DiagnosticSequenceSource a_source,
		std::uint64_t a_sequenceID,
		std::string_view a_trigger,
		const std::vector<OStimParticipantInfo>& a_participants)
	{
		const auto snapshot = GetDiagnosticSnapshot();
		LogStateSnapshot(
			a_trigger,
			OStimNGThreadAPI::GetEventRevision(),
			snapshot.sceneInfo,
			snapshot.positions.size());

		const OStimSceneInfo emptyScene{};
		const auto& currentScene = snapshot.sceneInfo ? *snapshot.sceneInfo : emptyScene;
		DebugLog(
			"[OStimDiag] EQUIP_SNAPSHOT sequence={} source={} trigger='{}' scene='{}' node='{}' active={} capturedParticipants={} currentParticipants={}",
			a_sequenceID,
			DiagnosticSourceName(a_source),
			a_trigger,
			currentScene.sceneID,
			currentScene.animationName,
			Bool01(currentScene.active),
			a_participants.size(),
			currentScene.participants.size());
		for (const auto& participant : a_participants) {
			LogActorEquipment(
				RE::TESForm::LookupByID<RE::Actor>(participant.formID),
				participant,
				a_trigger);
		}
	}

	void ScheduleEquipmentSnapshot(
		DiagnosticSequenceSource a_source,
		std::uint64_t a_sequenceID,
		std::string a_trigger,
		std::int64_t a_delayMs,
		std::vector<OStimParticipantInfo> a_participants)
	{
		if (!Config::OStimIntegration::DebugLog) {
			return;
		}

		constexpr std::size_t kMaxPendingEquipmentSnapshots = 64;
		if (s_state.pendingEquipmentSnapshots.size() >= kMaxPendingEquipmentSnapshots) {
			s_state.pendingEquipmentSnapshots.erase(s_state.pendingEquipmentSnapshots.begin());
		}
		s_state.pendingEquipmentSnapshots.push_back(PendingEquipmentSnapshot{
			DiagnosticNowMs() + a_delayMs,
			a_sequenceID,
			a_source,
			std::move(a_trigger),
			std::move(a_participants)
		});
	}

	void ScheduleNativeEventEquipmentSnapshots(
		const OStimNGThreadAPI::DiagnosticEvent& a_event,
		const std::optional<OStimSceneInfo>& a_scene)
	{
		const auto participants = a_scene ? a_scene->participants : std::vector<OStimParticipantInfo>{};
		auto schedule = [&](std::int64_t a_delayMs) {
			ScheduleEquipmentSnapshot(
				DiagnosticSequenceSource::NativeEvent,
				a_event.revision,
				fmt::format("{}+{}ms", RuntimeEventName(a_event.event), a_delayMs),
				a_delayMs,
				participants);
		};

		if (a_event.event == OStimNGThreadAPI::RuntimeEvent::ThreadStarted ||
			a_event.event == OStimNGThreadAPI::RuntimeEvent::ThreadEnded) {
			for (const auto delay : { 0, 250, 1000, 2500 }) {
				schedule(delay);
			}
		} else if (a_event.event == OStimNGThreadAPI::RuntimeEvent::NodeChanged) {
			for (const auto delay : { 0, 500, 1500 }) {
				schedule(delay);
			}
		}
	}

	void ProcessNativeDiagnosticEvents()
	{
		const auto events = OStimNGThreadAPI::GetDiagnosticEventsAfter(s_state.lastDiagnosticNativeRevision);
		for (const auto& event : events) {
			if (event.revision > s_state.lastDiagnosticNativeRevision + 1) {
				DebugLog(
					"[OStimDiag] NATIVE_EVENT_QUEUE_GAP expected={} received={}",
					s_state.lastDiagnosticNativeRevision + 1,
					event.revision);
			}
			s_state.lastDiagnosticNativeRevision = event.revision;
			DebugLog(
				"[OStimDiag] NATIVE_EVENT rev={} event={} thread={} control={}",
				event.revision,
				RuntimeEventName(event.event),
				event.threadID,
				event.control);

			if (event.event != OStimNGThreadAPI::RuntimeEvent::ThreadStarted &&
				event.event != OStimNGThreadAPI::RuntimeEvent::NodeChanged &&
				event.event != OStimNGThreadAPI::RuntimeEvent::ThreadEnded) {
				continue;
			}

			const auto snapshot = GetDiagnosticSnapshot();
			LogStateSnapshot(
				RuntimeEventName(event.event),
				event.revision,
				snapshot.sceneInfo,
				snapshot.positions.size(),
				event.threadID);
			LogNavigationSnapshot(event.threadID, snapshot.positions);
			ScheduleNativeEventEquipmentSnapshots(event, snapshot.sceneInfo);
		}
	}

	void ProcessPendingEquipmentSnapshots()
	{
		const std::int64_t nowMs = DiagnosticNowMs();
		for (auto it = s_state.pendingEquipmentSnapshots.begin();
			it != s_state.pendingEquipmentSnapshots.end();) {
			if (it->dueAtMs > nowMs) {
				++it;
				continue;
			}

			const auto pending = std::move(*it);
			it = s_state.pendingEquipmentSnapshots.erase(it);
			LogEquipmentSnapshot(
				pending.source,
				pending.sequenceID,
				pending.trigger,
				pending.participants);
		}
	}

	struct ActionDispatchResult
	{
		bool result = false;
		const char* path = "Rejected";
		const char* backend = "None";
		std::string target;
	};

	void LogActionEnd(
		std::uint64_t a_actionID,
		OStimActionKind a_kind,
		const ActionDispatchResult& a_result,
		std::uint32_t a_pageBefore,
		std::uint32_t a_pageAfter)
	{
		if (a_actionID == 0) {
			return;
		}
		DebugLog(
			"[OStimDiag] ACTION_END id={} kind={} kindValue={} result={} path={} backend={} target='{}' pageBefore={} pageAfter={}",
			a_actionID,
			OStimIntegration::GetActionLabel(a_kind),
			static_cast<std::uint32_t>(a_kind),
			Bool01(a_result.result),
			a_result.path,
			a_result.backend,
			a_result.target,
			a_pageBefore,
			a_pageAfter);
	}

	void InvalidateBrowserState()
	{
		s_state.browserPage = 0;
		s_state.browserFocusIndex = -1;
	}

	void RememberPreviousWheelIfNeeded()
	{
		const int activeIdx = Wheeler::GetActiveWheelIndex();
		if (activeIdx < 0 ||
			ActionHotkeysBridge::IsBridgeWheelIndex(activeIdx) ||
			TransformWheelManager::IsTransformWheelIndex(activeIdx) ||
			WheelerAPI::IsManagedWheelIndex(activeIdx)) {
			return;
		}
		s_state.previousWheelIndex = activeIdx;
	}

	void RestorePreviousWheelIfNeeded()
	{
		if (Config::OStimIntegration::RestorePreviousWheelOnSceneEnd &&
			s_state.previousWheelIndex.has_value()) {
			const int idx = *s_state.previousWheelIndex;
			if (idx >= 0 &&
				idx < Wheeler::GetWheelCount() &&
				!ActionHotkeysBridge::IsBridgeWheelIndex(idx) &&
				!TransformWheelManager::IsTransformWheelIndex(idx) &&
				!WheelerAPI::IsManagedWheelIndex(idx)) {
				SwitchOrSetActiveWheel(idx);
				return;
			}
		}

		if (auto fallback = FindFallbackUserWheelIndex(); fallback.has_value()) {
			SwitchOrSetActiveWheel(*fallback);
		}
	}

	bool EnsureManagedWheel(std::string_view a_tag, std::uint32_t a_desiredEntries)
	{
		if (FindWheelIndexByTag(a_tag).has_value()) {
			return true;
		}

		auto* api = GetWheelerAPI();
		if (!api || !api->IsInitialized()) {
			return false;
		}

		WheelerAPI::WheelConfig config{};
		config.numEntries = static_cast<int32_t>((std::max)(1u, a_desiredEntries));
		config.position = -1;
		config.managed = true;
		config.clientName = a_tag.data();
		config.showLabel = false;

		const int created = api->CreateManagedWheel(&config);
		if (created < 0) {
			return false;
		}

		TagWheelIndex(created, a_tag);
		return true;
	}

	void PopulateWheel(
		std::string_view a_tag,
		const std::vector<std::shared_ptr<WheelItem>>& a_items,
		int a_focusIndex = -1,
		bool a_preserveExistingFocus = false)
	{
		auto wheelIndex = FindWheelIndexByTag(a_tag);
		if (!wheelIndex.has_value()) {
			return;
		}

		std::unique_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(*wheelIndex);
		if (!wheel) {
			return;
		}

		int focusIndex = a_preserveExistingFocus ? wheel->GetHoveredEntryIndex() : -1;
		if (a_focusIndex >= 0) {
			focusIndex = a_focusIndex;
		}

		wheel->Clear();
		for (const auto& item : a_items) {
			auto entry = std::make_unique<WheelEntry>();
			if (item) {
				entry->PushItem(item);
			}
			wheel->PushEntry(std::move(entry));
		}
		if (a_items.empty()) {
			wheel->PushEmptyEntry();
		}
		wheel->SetClientTag(a_tag);
		if (focusIndex >= 0 && focusIndex < wheel->GetNumEntries()) {
			wheel->SetHoveredEntryIndex(focusIndex);
		} else {
			wheel->SetHoveredEntryIndex(-1);
		}
		wheel->ResetAnimation();
	}

	void DeleteManagedWheels()
	{
		auto indices = CollectManagedWheelIndices();
		if (indices.empty()) {
			s_state.previousWheelIndex.reset();
			InvalidateBrowserState();
			s_state.lastUnifiedEntryCount = 0;
			++s_state.unifiedLayoutRevision;
			return;
		}

		if (OStimIntegration::IsManagedWheelIndex(Wheeler::GetActiveWheelIndex())) {
			RestorePreviousWheelIfNeeded();
		}

		auto* api = GetWheelerAPI();
		if (api) {
			std::sort(indices.begin(), indices.end(), std::greater<>());
			for (int idx : indices) {
				api->DeleteManagedWheel(idx);
			}
		}
		DebugLog("[OStimIntegration] deleted {} managed wheel(s)", indices.size());

		s_state.previousWheelIndex.reset();
		InvalidateBrowserState();
		s_state.lastUnifiedEntryCount = 0;
		++s_state.unifiedLayoutRevision;
	}

	bool SceneAllowsDirectControls(const std::optional<OStimSceneInfo>& a_scene)
	{
		return a_scene &&
		       a_scene->active &&
		       !a_scene->inTransition &&
		       !a_scene->inSequence &&
		       !a_scene->playerControlDisabled;
	}

	const char* GetFixedControlLabel(
		OStimActionKind a_kind,
		const OStimTrackerSnapshot& a_snapshot)
	{
		switch (a_kind) {
		case OStimActionKind::StopScene:
			return "End Scene";
		case OStimActionKind::DecreaseSpeed:
			return "Speed -";
		case OStimActionKind::ToggleAutoMode:
			return GetOStimAutoModeLabel(a_snapshot.sceneInfo && a_snapshot.sceneInfo->autoMode);
		case OStimActionKind::IncreaseSpeed:
			return "Speed +";
		default:
			return OStimIntegration::GetActionLabel(a_kind);
		}
	}

	UnifiedWheelLayout BuildUnifiedWheelLayout(
		const OStimTrackerSnapshot& a_snapshot,
		std::uint64_t a_layoutRevision)
	{
		UnifiedWheelLayout layout{};
		const bool suppressPreviousNavigation = ShouldHoldAcceptedNavigation(a_snapshot);
		const std::size_t visiblePositionCount =
			!Config::OStimIntegration::AllowPositionBrowsing || suppressPreviousNavigation ?
			0 : a_snapshot.positions.size();
		const auto model = OStimUnifiedWheel::BuildLayout(
			visiblePositionCount,
			s_state.browserPage,
			Config::OStimIntegration::MaxPositionsPerPage);
		layout.items.resize(model.physical.entryCount);
		for (std::size_t i = 0; i < kFixedControlActions.size(); ++i) {
			const OStimActionKind kind = kFixedControlActions[i];
			OStimActionPayload payload{};
			payload.kind = kind;
			payload.displayName = GetFixedControlLabel(kind, a_snapshot);
			payload.requiresActiveScene = true;
			payload.wheelLayoutRevision = a_layoutRevision;
			layout.items[model.physical.fixedIndices[i]] = WheelItemFactory::MakeOStimActionItem(std::move(payload));
		}

		layout.page = model.page;
		for (std::size_t i = 0; i < model.dynamicSlotCount; ++i) {
			const auto& slot = model.dynamicSlots[i];
			OStimActionPayload payload{};
			if (slot.kind == OStimUnifiedWheel::DynamicSlotKind::PreviousPage ||
				slot.kind == OStimUnifiedWheel::DynamicSlotKind::NextPage) {
				payload.kind = OStimActionKind::OpenPositionBrowser;
				payload.displayName = slot.kind == OStimUnifiedWheel::DynamicSlotKind::PreviousPage ?
					"< Prev" : "Next >";
				payload.browserPage = slot.targetPage;
				payload.browserFocusIndex = static_cast<int>(slot.physicalIndex);
				payload.requiresActiveScene = true;
				payload.wheelLayoutRevision = a_layoutRevision;
				layout.items[slot.physicalIndex] = WheelItemFactory::MakeOStimActionItem(std::move(payload));
				if (slot.kind == OStimUnifiedWheel::DynamicSlotKind::PreviousPage) {
					layout.previousPageIndex = static_cast<int>(slot.physicalIndex);
				} else {
					layout.nextPageIndex = static_cast<int>(slot.physicalIndex);
				}
				continue;
			}
			if (slot.kind != OStimUnifiedWheel::DynamicSlotKind::Position ||
				slot.positionIndex >= a_snapshot.positions.size()) {
				continue;
			}

			const auto& position = a_snapshot.positions[slot.positionIndex];
			payload.kind = GetOStimOneHopNavigationRowActionKind();
			payload.sceneID = position.id;
			payload.positionID = GetOStimNavigationPresentationID(position);
			payload.sourceSceneID = a_snapshot.sceneInfo ? a_snapshot.sceneInfo->sceneID : std::string{};
			payload.semantic = position.semantic;
			payload.previewPath = position.previewPath;
			payload.iconPath = position.iconPath;
			payload.category = position.category;
			payload.subcategory = position.subcategory;
			payload.browserFocusIndex = static_cast<int>(slot.physicalIndex);
			payload.requiresActiveScene = position.requiresActiveScene;
			payload.wheelLayoutRevision = a_layoutRevision;
			payload.displayName = !position.description.empty() ?
				position.description :
				(!position.displayName.empty() ? position.displayName : GetOStimNavigationPresentationID(position));
			layout.items[slot.physicalIndex] = WheelItemFactory::MakeOStimActionItem(std::move(payload));
			if (layout.firstPositionIndex < 0) {
				layout.firstPositionIndex = static_cast<int>(slot.physicalIndex);
			}
			layout.lastPositionIndex = static_cast<int>(slot.physicalIndex);
		}
		return layout;
	}

	int ResolveUnifiedFocusIndex(
		const UnifiedWheelLayout& a_layout,
		UnifiedFocusHint a_focusHint,
		int a_preservedFocusIndex)
	{
		const auto hasItem = [&](int a_index) {
			return a_index >= 0 &&
			       a_index < static_cast<int>(a_layout.items.size()) &&
			       a_layout.items[static_cast<std::size_t>(a_index)] != nullptr;
		};
		switch (a_focusHint) {
		case UnifiedFocusHint::PreserveCurrent:
			if (hasItem(a_preservedFocusIndex)) {
				return a_preservedFocusIndex;
			}
			break;
		case UnifiedFocusHint::PreviousPage:
			if (hasItem(a_layout.nextPageIndex)) {
				return a_layout.nextPageIndex;
			}
			if (hasItem(a_layout.lastPositionIndex)) {
				return a_layout.lastPositionIndex;
			}
			break;
		case UnifiedFocusHint::NextPage:
			if (hasItem(a_layout.firstPositionIndex)) {
				return a_layout.firstPositionIndex;
			}
			if (hasItem(a_layout.previousPageIndex)) {
				return a_layout.previousPageIndex;
			}
			break;
		case UnifiedFocusHint::None:
		default:
			break;
		}
		return -1;
	}

	bool EnsureControlWheel(UnifiedFocusHint a_focusHint, std::string_view a_reason)
	{
		const auto snapshot = OStimStateTracker::GetSnapshot();
		if (!snapshot.sceneInfo || !snapshot.sceneInfo->active) {
			return false;
		}

		const auto physical = OStimUnifiedWheel::BuildPhysicalLayout(
			Config::OStimIntegration::MaxPositionsPerPage);
		if (!EnsureManagedWheel(kControlWheelTag, static_cast<std::uint32_t>(physical.entryCount))) {
			return false;
		}
		++s_state.unifiedLayoutRevision;
		const auto layout = BuildUnifiedWheelLayout(snapshot, s_state.unifiedLayoutRevision);
		const bool resized = s_state.lastUnifiedEntryCount != 0 &&
			s_state.lastUnifiedEntryCount != layout.items.size();
		s_state.browserPage = layout.page;
		int preservedFocusIndex = s_state.browserFocusIndex;
		if (IsActiveWheelTagged(kControlWheelTag)) {
			if (auto hoveredIndex = GetWheelHoveredEntryIndex(kControlWheelTag); hoveredIndex.has_value()) {
				preservedFocusIndex = *hoveredIndex;
			}
		}
		const int focusIndex = ResolveUnifiedFocusIndex(
			layout,
			resized ? UnifiedFocusHint::None : a_focusHint,
			resized ? -1 : preservedFocusIndex);
		PopulateWheel(kControlWheelTag, layout.items, focusIndex, false);
		s_state.browserFocusIndex = focusIndex;
		s_state.lastUnifiedEntryCount = layout.items.size();
		if (resized && IsActiveWheelTagged(kControlWheelTag)) {
			if (const auto controlIdx = FindWheelIndexByTag(kControlWheelTag); controlIdx.has_value()) {
				Wheeler::SetWheelHoveredEntryIndex(*controlIdx, -1, Wheeler::IsWheelerOpen());
			}
		}
		DebugLog(
			"[OStimDiag] UNIFIED_WHEEL_REBUILD reason={} trackerRev={} layoutRev={} sceneID='{}' navigation={} page={} dynamicCapacity={} entries={} resized={}",
			a_reason,
			snapshot.revision,
			s_state.unifiedLayoutRevision,
			snapshot.sceneInfo->sceneID,
			snapshot.positions.size(),
			s_state.browserPage,
			physical.dynamicCapacity,
			layout.items.size(),
			Bool01(resized));
		return true;
	}

	bool OpenControlWheel(int a_focusIndex = -1)
	{
		if (!EnsureControlWheel(UnifiedFocusHint::PreserveCurrent, "OpenControlWheel")) {
			return false;
		}

		auto controlIdx = FindWheelIndexByTag(kControlWheelTag);
		if (!controlIdx.has_value() || !SwitchOrSetActiveWheel(*controlIdx)) {
			return false;
		}
		const int focusIndex = a_focusIndex >= 0 ? a_focusIndex : s_state.browserFocusIndex;
		if (focusIndex >= 0) {
			Wheeler::SetWheelHoveredEntryIndex(*controlIdx, focusIndex, Wheeler::IsWheelerOpen());
		}
		return true;
	}

	bool OpenBrowserPage(std::uint32_t a_page)
	{
		const auto previousPage = s_state.browserPage;
		s_state.browserPage = a_page;
		const UnifiedFocusHint focusHint = s_state.browserPage > previousPage ?
			UnifiedFocusHint::NextPage :
			(s_state.browserPage < previousPage ?
				UnifiedFocusHint::PreviousPage :
				UnifiedFocusHint::PreserveCurrent);
		if (!EnsureControlWheel(focusHint, "ChangeUnifiedPage")) {
			return false;
		}

		auto controlIdx = FindWheelIndexByTag(kControlWheelTag);
		if (!controlIdx.has_value() || !SwitchOrSetActiveWheel(*controlIdx)) {
			return false;
		}
		if (s_state.browserFocusIndex >= 0) {
			Wheeler::SetWheelHoveredEntryIndex(
				*controlIdx,
				s_state.browserFocusIndex,
				Wheeler::IsWheelerOpen());
		}
		return true;
	}

	bool QueueWheelNavigationTask(
		OStimActionKind a_kind,
		OStimActionPayload a_payload,
		std::uint64_t a_actionID)
	{
		auto* taskInterface = SKSE::GetTaskInterface();
		if (!taskInterface) {
			DebugLog("[OStimIntegration] navigation action={} skipped because task interface is unavailable", static_cast<std::uint32_t>(a_kind));
			ActionDispatchResult result{};
			result.path = "TaskInterfaceUnavailable";
			LogActionEnd(a_actionID, a_kind, result, s_state.browserPage, s_state.browserPage);
			return false;
		}

		const auto intentEpoch = Wheeler::GetTransientRestorationEpoch();
		taskInterface->AddTask([kind = a_kind, payload = std::move(a_payload), intentEpoch, actionID = a_actionID]() {
			const std::uint32_t pageBefore = s_state.browserPage;
			const bool authorized = Wheeler::ExecuteTransientGameplayIfCurrent(intentEpoch, [kind, payload, actionID, pageBefore]() {
				if (!OStimIntegration::CanExecuteAction(kind, &payload)) {
					DebugLog("[OStimIntegration] navigation action={} failed guards before queued execution", static_cast<std::uint32_t>(kind));
					ActionDispatchResult result{};
					result.path = "QueuedGuardRejected";
					LogActionEnd(actionID, kind, result, pageBefore, s_state.browserPage);
					return;
				}

				ActionDispatchResult result{};
				result.backend = "WheelerUI";
				switch (kind) {
				case OStimActionKind::OpenControlWheel:
					result.path = "OpenControlWheel";
					result.result = OpenControlWheel(payload.browserFocusIndex);
					break;
				case OStimActionKind::ReturnToControlWheel:
					result.path = "ReturnToControlWheel";
					InvalidateBrowserState();
					result.result = OpenControlWheel(payload.browserFocusIndex);
					break;
				case OStimActionKind::OpenPositionBrowser:
					result.path = "ChangeUnifiedPage";
					result.result = OpenBrowserPage(payload.browserPage);
					break;
				default:
					result.path = "NoDispatch";
					result.backend = "None";
					break;
				}
				LogActionEnd(actionID, kind, result, pageBefore, s_state.browserPage);
			});
			if (!authorized) {
				ActionDispatchResult result{};
				result.path = "TransientEpochRejected";
				LogActionEnd(actionID, kind, result, pageBefore, s_state.browserPage);
			}
		});

		return true;
	}

	ActionDispatchResult RunActionNow(OStimActionKind a_kind, const OStimActionPayload* a_payload)
	{
		ActionDispatchResult result{};
		if (IsStaleManagedWheelPayload(a_payload)) {
			result.path = "StaleWheelLayout";
			result.backend = "WheelerUI";
			return result;
		}
		if (IsSceneBlockedByMenus()) {
			result.path = "MenuBlocked";
			return result;
		}
		if (!RE::PlayerCharacter::GetSingleton() ||
			!RE::PlayerCharacter::GetSingleton()->Is3DLoaded()) {
			result.path = "PlayerUnavailable";
			return result;
		}

		switch (a_kind) {
		case OStimActionKind::StopScene:
			result.path = "EndAnimation";
			result.backend = "Papyrus";
			result.result = OStimBridge::CallAPIMethod("EndAnimation", true);
			return result;
		case OStimActionKind::IncreaseSpeed:
		case OStimActionKind::DecreaseSpeed:
		{
			result.path = "AdjustSpeed";
			const int delta = a_kind == OStimActionKind::IncreaseSpeed ? +1 : -1;
			result.target = delta > 0 ? "+1" : "-1";
			if (OStimNGThreadAPI::AdjustSpeed(delta)) {
				result.result = true;
				result.backend = "NativeThreadAPI";
				return result;
			}
			result.backend = "PapyrusFallback";
			result.result = OStimBridge::CallAPIMethod(
				delta > 0 ? "IncreaseAnimationSpeed" : "DecreaseAnimationSpeed");
			return result;
		}
		case OStimActionKind::ToggleAutoMode:
		{
			result.path = "AutoModeRejected";
			result.backend = "NativeSceneAPI";
			const auto snapshot = OStimStateTracker::GetSnapshot();
			const bool sceneActive = snapshot.sceneInfo && snapshot.sceneInfo->active;
			const bool currentAutoMode = snapshot.sceneInfo && snapshot.sceneInfo->autoMode;
			const auto decision = BuildOStimAutoModeDecision(
				sceneActive,
				OStimNGSceneAPI::IsAvailable(),
				currentAutoMode);
			const std::uint32_t threadID = snapshot.sceneInfo ? snapshot.sceneInfo->threadID : 0;
			result.target = decision.desiredAutoMode ? "1" : "0";
			if (!decision.canDispatch) {
				DebugLog(
					"[OStimDiag] AUTO_MODE_RESULT thread={} desired={} result=0 backend=NativeSceneAPI reason='{}'",
					threadID,
					Bool01(decision.desiredAutoMode),
					decision.rejectionReason);
				return result;
			}

			DebugLog(
				"[OStimDiag] AUTO_MODE_REQUEST thread={} current={} desired={} backend=NativeSceneAPI",
				threadID,
				Bool01(decision.currentAutoMode),
				Bool01(decision.desiredAutoMode));
			OStimNGSceneAPI::DispatchResult nativeResult = OStimNGSceneAPI::DispatchResult::Failed;
			result.result = DispatchOStimAutoMode(
				decision,
				threadID,
				[&](std::uint32_t a_threadID, bool a_autoMode) {
					nativeResult = OStimNGSceneAPI::SetAutoMode(a_threadID, a_autoMode);
					return nativeResult == OStimNGSceneAPI::DispatchResult::Success;
				});
			result.path = result.result ? "AutoModeDispatchAccepted" : "AutoModeDispatchFailed";
			if (result.result) {
				DebugLog(
					"[OStimDiag] AUTO_MODE_RESULT thread={} desired={} result=1 backend=NativeSceneAPI",
					threadID,
					Bool01(decision.desiredAutoMode));
			} else {
				DebugLog(
					"[OStimDiag] AUTO_MODE_RESULT thread={} desired={} result=0 backend=NativeSceneAPI reason='{}'",
					threadID,
					Bool01(decision.desiredAutoMode),
					OStimNGSceneAPI::GetDispatchResultReason(nativeResult));
			}
			return result;
		}
		case OStimActionKind::SelectSpecificPosition:
			result.path = "NavigationRejected";
			if (!a_payload || a_payload->sceneID.empty()) {
				return result;
			}
			result.target = a_payload->sceneID;
			{
				const auto snapshot = OStimStateTracker::GetSnapshot();
				const auto validation = ValidateOStimNavigationSelection(
					snapshot.sceneInfo,
					snapshot.positions,
					*a_payload);
				if (validation != OStimNavigationSelectionStatus::Ready) {
					const std::string currentSceneID = snapshot.sceneInfo ?
						snapshot.sceneInfo->sceneID : std::string{};
					DebugLog(
						"[OStimDiag] NAV_STALE_REJECT payloadSceneID='{}' destinationID='{}' payloadSourceSceneID='{}' currentTrackerScene='{}' navigation={} reason={}",
						a_payload->sceneID,
						a_payload->positionID,
						a_payload->sourceSceneID,
						currentSceneID,
						snapshot.positions.size(),
						GetOStimNavigationSelectionStatusName(validation));
					result.backend = "TrackerSnapshot";
					OStimIntegration::RequestRefresh();
					if (IsActiveWheelTagged(kControlWheelTag)) {
						EnsureControlWheel(UnifiedFocusHint::PreserveCurrent, "NavigationRejected");
					}
					return result;
				}

				if (snapshot.availability.hasNativeThreadAPI) {
					result.backend = "NativeThreadAPI";
					if (OStimNGThreadAPI::NavigateToScene(
							snapshot.sceneInfo->threadID,
							a_payload->sceneID)) {
						result.result = true;
						result.path = "NavigationDispatchAccepted";
						DebugLog(
							"[OStimDiag] NAV_DISPATCH_ACCEPTED sceneID='{}' destinationID='{}' sourceSceneID='{}' trackerRev={}",
							a_payload->sceneID,
							a_payload->positionID,
							a_payload->sourceSceneID,
							snapshot.revision);
					} else {
						result.path = "NavigationDispatchNotAccepted";
					}
					return result;
				}

				result.backend = "LegacyPapyrus";
				result.path = "LegacyNavigationDispatch";
				result.result = OStimBridge::CallAPIMethod(
					"TravelToAnimationIfPossible",
					a_payload->sceneID);
				return result;
			}
		default:
			result.path = "NoDispatch";
			return result;
		}
	}

	bool QueueSceneAction(
		OStimActionKind a_kind,
		const OStimActionPayload* a_payload,
		std::uint64_t a_actionID)
	{
		auto* taskInterface = SKSE::GetTaskInterface();
		if (!taskInterface) {
			DebugLog("[OStimIntegration] action={} skipped because task interface is unavailable", static_cast<std::uint32_t>(a_kind));
			ActionDispatchResult result{};
			result.path = "TaskInterfaceUnavailable";
			LogActionEnd(a_actionID, a_kind, result, s_state.browserPage, s_state.browserPage);
			return false;
		}

		OStimActionPayload payload{};
		if (a_payload) {
			payload = *a_payload;
		} else {
			payload.kind = a_kind;
		}
		const auto intentEpoch = Wheeler::GetTransientRestorationEpoch();
		taskInterface->AddTask([payload, intentEpoch, actionID = a_actionID]() {
			const std::uint32_t browserPage = s_state.browserPage;
			const bool authorized = Wheeler::ExecuteTransientGameplayIfCurrent(intentEpoch, [payload, actionID, browserPage]() {
				std::vector<OStimParticipantInfo> participants;
				if (actionID != 0 && IsNavigationAction(payload.kind) && Config::OStimIntegration::DebugLog) {
					const auto snapshot = GetDiagnosticSnapshot();
					LogNavigationSnapshot(
						snapshot.sceneInfo ? snapshot.sceneInfo->threadID : 0,
						snapshot.positions);
					if (snapshot.sceneInfo) {
						participants = snapshot.sceneInfo->participants;
					}
					LogEquipmentSnapshot(
						DiagnosticSequenceSource::Action,
						actionID,
						fmt::format("{}+before", OStimIntegration::GetActionLabel(payload.kind)),
						participants);
				}

				const ActionDispatchResult result = RunActionNow(payload.kind, &payload);
				LogActionEnd(actionID, payload.kind, result, browserPage, s_state.browserPage);
				if (result.result) {
					if (payload.kind == OStimActionKind::StopScene) {
						s_state.suppressManagedWheelsUntilSceneStops = true;
						DeleteManagedWheels();
						OStimIntegration::RequestRefresh();
					}
					OStimStateTracker::MarkActionExecuted(payload.kind);
					if (actionID != 0 && IsNavigationAction(payload.kind)) {
						for (const auto delay : { 250, 1000 }) {
							ScheduleEquipmentSnapshot(
								DiagnosticSequenceSource::Action,
								actionID,
								fmt::format("{}+{}ms", OStimIntegration::GetActionLabel(payload.kind), delay),
								delay,
								participants);
						}
					}
					if (payload.kind == OStimActionKind::SelectSpecificPosition) {
						const auto policy = OStimSceneActionUI::Evaluate(
							true,
							Config::OStimIntegration::CloseWheelAfterSceneAction);
						if (policy.resetNavigationPage) {
							InvalidateBrowserState();
						}
						DebugLog(
							"[OStimDiag] SCENE_ACTION_UI_POLICY sceneID='{}' closeAfterAction={} decision={}",
							payload.sceneID,
							Bool01(Config::OStimIntegration::CloseWheelAfterSceneAction),
							policy.decision == OStimSceneActionUI::Decision::Close ? "Close" : "KeepOpen");
						if (policy.decision == OStimSceneActionUI::Decision::Close) {
							ClearAcceptedNavigationPending();
							if (Wheeler::IsWheelerOpen()) {
								Wheeler::CloseWheeler();
							}
						} else if (policy.decision == OStimSceneActionUI::Decision::KeepOpen) {
							s_state.acceptedNavigationPending = true;
							s_state.acceptedNavigationSourceSceneID = payload.sourceSceneID;
							if (IsActiveWheelTagged(kControlWheelTag)) {
								EnsureControlWheel(UnifiedFocusHint::None, "SceneActionKeepOpen");
								if (auto controlIdx = FindWheelIndexByTag(kControlWheelTag); controlIdx.has_value()) {
									Wheeler::SetWheelHoveredEntryIndex(
										*controlIdx,
										-1,
										Wheeler::IsWheelerOpen());
								}
							}
						}
					}
				} else {
					DebugLog("[OStimIntegration] action={} failed guards or scene dispatch", static_cast<std::uint32_t>(payload.kind));
				}
			});
			if (!authorized) {
				ActionDispatchResult result{};
				result.path = "TransientEpochRejected";
				LogActionEnd(actionID, payload.kind, result, browserPage, s_state.browserPage);
			}
		});
		return true;
	}

	void RebuildManagedWheelsIfNeeded(
		std::uint64_t a_revision,
		UnifiedFocusHint a_focusHint,
		std::string_view a_reason)
	{
		if (!Config::OStimIntegration::CreateManagedWheel) {
			DeleteManagedWheels();
			s_state.lastAppliedRevision = a_revision;
			return;
		}

		EnsureControlWheel(a_focusHint, a_reason);

		s_state.lastAppliedRevision = a_revision;
	}
}

void OStimIntegration::Init()
{
	DeleteManagedWheels();
	s_state = IntegrationState{};
	s_nextActionID.store(0, std::memory_order_release);
	s_state.diagnosticsEnabled = Config::OStimIntegration::DebugLog;
	OStimNGThreadAPI::SetDiagnosticsEnabled(s_state.diagnosticsEnabled);
	OStimStateTracker::Reset();
	OStimUndressVisualRefresh::Reset();
	RequestRefresh();
}

void OStimIntegration::Reset()
{
	DebugLog("[OStimDiag] RESET_BEGIN");
	// Called from Wheeler::ClearWheelData while the wheel-data lock may already
	// be exclusively held. Do not enumerate, create, delete, or switch wheels here.
	s_state = IntegrationState{};
	s_nextActionID.store(0, std::memory_order_release);
	s_state.diagnosticsEnabled = Config::OStimIntegration::DebugLog;
	OStimNGThreadAPI::SetDiagnosticsEnabled(s_state.diagnosticsEnabled);
	DebugLog("[OStimDiag] RESET_TRACKER_BEGIN");
	OStimUndressVisualRefresh::Reset();
	OStimStateTracker::Reset();
	DebugLog("[OStimDiag] RESET_TRACKER_END");
	DebugLog("[OStimDiag] RESET_END");
}

void OStimIntegration::Update()
{
	const bool diagnosticsEnabled = Config::OStimIntegration::DebugLog;
	if (diagnosticsEnabled && !s_state.diagnosticsEnabled) {
		s_state.lastDiagnosticNativeRevision = OStimNGThreadAPI::GetEventRevision();
	}
	OStimNGThreadAPI::SetDiagnosticsEnabled(diagnosticsEnabled);
	s_state.diagnosticsEnabled = diagnosticsEnabled;
	if (!diagnosticsEnabled) {
		s_state.pendingEquipmentSnapshots.clear();
	}

	if (Config::OStimIntegration::Enabled || Config::OStimIntegration::AutoDetect) {
		OStimStateTracker::Update(s_state.refreshRequested);
	}
	s_state.refreshRequested = false;
	const bool wheelRebuildRequested = s_state.wheelRebuildRequested;
	s_state.wheelRebuildRequested = false;
	if (diagnosticsEnabled) {
		ProcessNativeDiagnosticEvents();
		ProcessPendingEquipmentSnapshots();
	}

	const auto availability = OStimStateTracker::GetAvailability();
	const bool sceneActive = availability.available && OStimStateTracker::IsSceneActive();
	OStimUndressVisualRefresh::Update(availability.available, sceneActive);
	if (s_state.acceptedNavigationPending) {
		const auto snapshot = OStimStateTracker::GetSnapshot();
		if (!ShouldHoldAcceptedNavigation(snapshot)) {
			ClearAcceptedNavigationPending();
		}
	}

	if (availability.available != s_state.lastAvailable ||
		availability.reason != s_state.lastAvailabilityReason) {
		DebugLog(
			"[OStimIntegration] availability={} reason='{}' apiVersion={} nativeThreadApi={} database={}",
			availability.available,
			availability.reason,
			availability.apiVersion,
			availability.hasNativeThreadAPI,
			availability.hasDatabase);
		s_state.lastAvailable = availability.available;
		s_state.lastAvailabilityReason = availability.reason;
	}
	if (sceneActive != s_state.lastSceneActive) {
		DebugLog("[OStimIntegration] sceneActive={}", sceneActive);
	}

	if (!Config::OStimIntegration::Enabled || !availability.available) {
		if (!CollectManagedWheelIndices().empty()) {
			DeleteManagedWheels();
		}
		s_state.suppressManagedWheelsUntilSceneStops = false;
		s_state.lastSceneActive = false;
		s_state.lastAppliedRevision = OStimStateTracker::GetRevision();
		return;
	}

	const std::uint64_t revision = OStimStateTracker::GetRevision();
	if (s_state.suppressManagedWheelsUntilSceneStops) {
		if (!CollectManagedWheelIndices().empty()) {
			DeleteManagedWheels();
		}
		if (!sceneActive) {
			s_state.suppressManagedWheelsUntilSceneStops = false;
		}
		s_state.lastSceneActive = sceneActive;
		s_state.lastAppliedRevision = revision;
		return;
	}

	if (sceneActive) {
		if (!s_state.lastSceneActive && Config::OStimIntegration::AutoSwitchToSceneWheel) {
			RememberPreviousWheelIfNeeded();
		}

		if (wheelRebuildRequested || revision != s_state.lastAppliedRevision || !s_state.lastSceneActive) {
			RebuildManagedWheelsIfNeeded(
				revision,
				wheelRebuildRequested ? UnifiedFocusHint::None : UnifiedFocusHint::PreserveCurrent,
				wheelRebuildRequested ? "ConfigRefresh" : "TrackerRevision");
		}

		if (!s_state.lastSceneActive &&
			Config::OStimIntegration::AutoSwitchToSceneWheel &&
			Config::OStimIntegration::CreateManagedWheel) {
			OpenControlWheel();
		}
	} else if (s_state.lastSceneActive || !CollectManagedWheelIndices().empty()) {
		DeleteManagedWheels();
		s_state.suppressManagedWheelsUntilSceneStops = false;
	}

	s_state.lastSceneActive = sceneActive;
	if (!sceneActive) {
		s_state.lastAppliedRevision = revision;
	}
}

void OStimIntegration::RequestRefresh()
{
	s_state.refreshRequested = true;
	s_state.wheelRebuildRequested = true;
	OStimStateTracker::Update(true);
}

bool OStimIntegration::IsAvailable()
{
	return OStimStateTracker::GetAvailability().available;
}

bool OStimIntegration::IsEnabled()
{
	return Config::OStimIntegration::Enabled && IsAvailable();
}

bool OStimIntegration::IsSceneActive()
{
	return IsEnabled() && OStimStateTracker::IsSceneActive();
}

bool OStimIntegration::CanExecuteAction(OStimActionKind a_kind, const OStimActionPayload* a_payload)
{
	if (!IsEnabled() || IsStaleManagedWheelPayload(a_payload)) {
		return false;
	}

	const auto snapshot = OStimStateTracker::GetSnapshot();
	const bool sceneActive = snapshot.sceneInfo && snapshot.sceneInfo->active;
	const bool sceneControllable = SceneAllowsDirectControls(snapshot.sceneInfo);

	switch (a_kind) {
	case OStimActionKind::OpenControlWheel:
	case OStimActionKind::ReturnToControlWheel:
		return Config::OStimIntegration::CreateManagedWheel && sceneActive;
	case OStimActionKind::OpenPositionBrowser:
		return Config::OStimIntegration::CreateManagedWheel &&
		       Config::OStimIntegration::AllowPositionBrowsing &&
		       sceneControllable &&
		       !snapshot.positions.empty();
	case OStimActionKind::OpenPositionSubmenu:
	case OStimActionKind::ReturnToPositionBrowserParent:
		return false;
	case OStimActionKind::StopScene:
		return sceneActive && OStimStateTracker::CanDispatchByCooldown(a_kind);
	case OStimActionKind::IncreaseSpeed:
	case OStimActionKind::DecreaseSpeed:
		return sceneControllable && OStimStateTracker::CanDispatchByCooldown(a_kind);
	case OStimActionKind::ToggleAutoMode:
		return BuildOStimAutoModeDecision(
			       sceneActive,
			       OStimNGSceneAPI::IsAvailable(),
			       snapshot.sceneInfo && snapshot.sceneInfo->autoMode)
		           .canDispatch &&
		       OStimStateTracker::CanDispatchByCooldown(a_kind);
	case OStimActionKind::NextPosition:
	case OStimActionKind::PreviousPosition:
		return false;
	case OStimActionKind::SelectSpecificPosition:
		return a_payload &&
		       !ShouldHoldAcceptedNavigation(snapshot) &&
		       !a_payload->sceneID.empty() &&
		       ValidateOStimNavigationSelection(
			       snapshot.sceneInfo,
			       snapshot.positions,
			       *a_payload) == OStimNavigationSelectionStatus::Ready &&
		       OStimStateTracker::CanDispatchByCooldown(a_kind);
	case OStimActionKind::NextStage:
	case OStimActionKind::PreviousStage:
	case OStimActionKind::SwapPartner:
	case OStimActionKind::ChangeVariant:
		return false;
	default:
		return false;
	}
}

bool OStimIntegration::ExecuteAction(OStimActionKind a_kind, const OStimActionPayload* a_payload)
{
	const bool diagnosticsEnabled = Config::OStimIntegration::DebugLog;
	const std::uint64_t actionID = diagnosticsEnabled ?
		s_nextActionID.fetch_add(1, std::memory_order_acq_rel) + 1 :
		0;
	const bool canExecute = CanExecuteAction(a_kind, a_payload);
	if (diagnosticsEnabled) {
		const OStimActionPayload emptyPayload{};
		const auto& payload = a_payload ? *a_payload : emptyPayload;
		const auto snapshot = GetDiagnosticSnapshot();
		const OStimSceneInfo emptyScene{};
		const auto& sceneInfo = snapshot.sceneInfo ? *snapshot.sceneInfo : emptyScene;
		const char* label = payload.displayName.empty() ?
			GetActionLabel(a_kind) :
			payload.displayName.c_str();
		DebugLog(
			"[OStimDiag] ACTION_BEGIN id={} kind={} kindValue={} label='{}' sceneID='{}' destinationID='{}' sourceSceneID='{}' requiresActiveScene={} canExecute={} currentScene='{}' node='{}' nav={} page={}",
			actionID,
			GetActionLabel(a_kind),
			static_cast<std::uint32_t>(a_kind),
			label,
			payload.sceneID,
			payload.positionID,
			payload.sourceSceneID,
			Bool01(payload.requiresActiveScene),
			Bool01(canExecute),
			sceneInfo.sceneID,
			sceneInfo.animationName,
			snapshot.positions.size(),
			s_state.browserPage);
		LogStateSnapshot(
			fmt::format("Action:{}", GetActionLabel(a_kind)),
			OStimNGThreadAPI::GetEventRevision(),
			snapshot.sceneInfo,
			snapshot.positions.size());
		LogNavigationSnapshot(sceneInfo.threadID, snapshot.positions);
	}

	if (!canExecute) {
		if (a_kind == OStimActionKind::SelectSpecificPosition &&
			IsEnabled() &&
			!IsStaleManagedWheelPayload(a_payload) &&
			a_payload &&
			!a_payload->sceneID.empty()) {
			const auto snapshot = OStimStateTracker::GetSnapshot();
			if (ValidateOStimNavigationSelection(
					snapshot.sceneInfo,
					snapshot.positions,
					*a_payload) != OStimNavigationSelectionStatus::Ready) {
				return QueueSceneAction(a_kind, a_payload, actionID);
			}
		}
		ActionDispatchResult result{};
		result.path = "CanExecuteRejected";
		LogActionEnd(actionID, a_kind, result, s_state.browserPage, s_state.browserPage);
		return false;
	}

	switch (a_kind) {
	case OStimActionKind::OpenControlWheel:
	{
		OStimActionPayload payload = a_payload ? *a_payload : OStimActionPayload{};
		payload.kind = a_kind;
		return QueueWheelNavigationTask(a_kind, std::move(payload), actionID);
	}
	case OStimActionKind::ReturnToControlWheel:
	{
		OStimActionPayload payload = a_payload ? *a_payload : OStimActionPayload{};
		payload.kind = a_kind;
		return QueueWheelNavigationTask(a_kind, std::move(payload), actionID);
	}
	case OStimActionKind::OpenPositionBrowser:
	{
		OStimActionPayload payload = a_payload ? *a_payload : OStimActionPayload{};
		payload.kind = a_kind;
		return QueueWheelNavigationTask(a_kind, std::move(payload), actionID);
	}
	case OStimActionKind::StopScene:
	case OStimActionKind::IncreaseSpeed:
	case OStimActionKind::DecreaseSpeed:
	case OStimActionKind::ToggleAutoMode:
	case OStimActionKind::SelectSpecificPosition:
		return QueueSceneAction(a_kind, a_payload, actionID);
	default:
	{
		ActionDispatchResult result{};
		result.path = "NoDispatch";
		LogActionEnd(actionID, a_kind, result, s_state.browserPage, s_state.browserPage);
		return false;
	}
	}
}

std::vector<OStimPositionInfo> OStimIntegration::GetAvailablePositions()
{
	if (!IsEnabled()) {
		return {};
	}
	return OStimStateTracker::GetAvailablePositions();
}

std::optional<OStimSceneInfo> OStimIntegration::GetCurrentSceneInfo()
{
	if (!IsEnabled()) {
		return std::nullopt;
	}
	return OStimStateTracker::GetCurrentSceneInfo();
}

bool OStimIntegration::ShouldBlockRegularWheelActivation(std::string_view a_itemTypeName)
{
	if (!Config::OStimIntegration::Enabled ||
		!Config::OStimIntegration::RestrictRegularWheelActionsDuringScenes ||
		!IsSceneActive()) {
		return false;
	}

	return a_itemTypeName != "WheelItemOStimAction";
}

const char* OStimIntegration::GetActionLabel(OStimActionKind a_kind)
{
	switch (a_kind) {
	case OStimActionKind::OpenControlWheel:
		return "OStim Controls";
	case OStimActionKind::OpenPositionBrowser:
		return "Change Page";
	case OStimActionKind::OpenPositionSubmenu:
		return "Open Submenu";
	case OStimActionKind::ReturnToPositionBrowserParent:
		return "Back";
	case OStimActionKind::ReturnToControlWheel:
		return "Back To Controls";
	case OStimActionKind::StopScene:
		return "Stop Scene";
	case OStimActionKind::NextStage:
		return "Next Stage";
	case OStimActionKind::PreviousStage:
		return "Previous Stage";
	case OStimActionKind::NextPosition:
		return "Next Position";
	case OStimActionKind::PreviousPosition:
		return "Previous Position";
	case OStimActionKind::IncreaseSpeed:
		return "Speed Up";
	case OStimActionKind::DecreaseSpeed:
		return "Speed Down";
	case OStimActionKind::ToggleAutoMode:
		return "Auto Progress";
	case OStimActionKind::SwapPartner:
		return "Swap Partner";
	case OStimActionKind::ChangeVariant:
		return "Change Variant";
	case OStimActionKind::SelectSpecificPosition:
		return "Select Position";
	default:
		return "OStim";
	}
}

bool OStimIntegration::IsManagedWheelTag(std::string_view a_tag)
{
	return IsManagedTagInternal(a_tag);
}

bool OStimIntegration::IsManagedWheelIndex(int a_wheelIndex)
{
	if (a_wheelIndex < 0) {
		return false;
	}

	std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
	Wheel* wheel = Wheeler::GetWheelByIndex(a_wheelIndex);
	return wheel && IsManagedTagInternal(wheel->GetClientTag());
}
