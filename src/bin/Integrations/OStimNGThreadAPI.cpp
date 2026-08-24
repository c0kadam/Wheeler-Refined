// Uses the official OStimNG Thread API header vendored from
// VersuchDrei/OStimNG revision 95d9720ee3633896e893127d003dad7375459d50
// (GNU GPL v3). OStim.dll is not linked or bundled; the optional interface is
// discovered at runtime.
#include "OStimNGThreadAPI.h"

#include "OStimPreviewResolver.h"
#include "Plugin.h"

#include <RE/T/TESForm.h>
#include <REL/Relocation.h>

#include <Windows.h>

#include "include/third_party/ostim/OstimNG-API-Thread.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
	namespace OStimThread = OstimNG_API::Thread;

	using NativeAPI = OStimThread::IThreadInterface;
	using NativeResult = OStimThread::APIResult;
	using NativeThreadEvent = OStimThread::ThreadEvent;
	using NativeControl = OStimThread::Controls;

	static_assert(sizeof(OStimThread::ActorData) == 16);
	static_assert(alignof(OStimThread::ActorData) == 4);
	static_assert(offsetof(OStimThread::ActorData, timesClimaxed) == 12);
	static_assert(sizeof(OStimThread::NavigationData) == 48);
	static_assert(alignof(OStimThread::NavigationData) == 8);
	static_assert(offsetof(OStimThread::NavigationData, isTransition) == 40);
	static_assert(sizeof(OStimThread::KeyData) == 68);
	static_assert(alignof(OStimThread::KeyData) == 4);
	static_assert(offsetof(OStimThread::KeyData, keyHideUI) == 64);
	static_assert(sizeof(OStimThread::ActorAlignmentData) == 24);
	static_assert(alignof(OStimThread::ActorAlignmentData) == 4);
	static_assert(offsetof(OStimThread::ActorAlignmentData, sosBend) == 20);
	static_assert(sizeof(OStimThread::SceneSearchResult) == 24);
	static_assert(alignof(OStimThread::SceneSearchResult) == 8);
	static_assert(offsetof(OStimThread::SceneSearchResult, actorCount) == 16);

	std::mutex s_apiLock;
	NativeAPI* s_api = nullptr;
	bool s_callbacksRegistered = false;
	std::atomic<std::uint64_t> s_eventRevision{ 0 };
	std::atomic<std::uint64_t> s_lastEndedRevision{ 0 };
	std::atomic<OStimNGThreadAPI::RuntimeEvent> s_lastEvent{ OStimNGThreadAPI::RuntimeEvent::None };

	std::string CopyString(const char* a_value)
	{
		return a_value ? std::string(a_value) : std::string{};
	}

	OStimNGThreadAPI::RuntimeEvent TranslateEvent(NativeThreadEvent a_event)
	{
		switch (a_event) {
		case NativeThreadEvent::ThreadStarted:
			return OStimNGThreadAPI::RuntimeEvent::ThreadStarted;
		case NativeThreadEvent::ThreadEnded:
			return OStimNGThreadAPI::RuntimeEvent::ThreadEnded;
		case NativeThreadEvent::NodeChanged:
			return OStimNGThreadAPI::RuntimeEvent::NodeChanged;
		case NativeThreadEvent::ControlInput:
			return OStimNGThreadAPI::RuntimeEvent::ControlInput;
		default:
			return OStimNGThreadAPI::RuntimeEvent::None;
		}
	}

	void OnThreadEvent(NativeThreadEvent a_event, std::uint32_t, void*)
	{
		const auto translated = TranslateEvent(a_event);
		s_lastEvent.store(translated, std::memory_order_release);
		const auto revision = s_eventRevision.fetch_add(1, std::memory_order_acq_rel) + 1;
		if (translated == OStimNGThreadAPI::RuntimeEvent::ThreadEnded) {
			s_lastEndedRevision.store(revision, std::memory_order_release);
		}
	}

	void OnControlEvent(NativeControl, std::uint32_t, void*)
	{
		s_lastEvent.store(OStimNGThreadAPI::RuntimeEvent::ControlInput, std::memory_order_release);
		s_eventRevision.fetch_add(1, std::memory_order_acq_rel);
	}

	NativeAPI* GetInterface()
	{
		if (s_api) {
			return s_api;
		}

		std::lock_guard lock(s_apiLock);
		if (s_api) {
			return s_api;
		}

		auto* api = OStimThread::GetAPI(Plugin::NAME.data(), Plugin::VERSION);
		if (!api) {
			return nullptr;
		}

		api->RegisterEventCallback(&OnThreadEvent, nullptr);
		api->RegisterControlCallback(&OnControlEvent, nullptr);
		s_callbacksRegistered = true;
		s_api = api;
		return s_api;
	}

	std::optional<std::uint32_t> GetActiveThreadID()
	{
		auto* api = GetInterface();
		if (!api) {
			return std::nullopt;
		}

		const std::uint32_t threadID = api->GetPlayerThreadID();
		if (!api->IsThreadValid(threadID)) {
			return std::nullopt;
		}

		return threadID;
	}
}

void OStimNGThreadAPI::Reset()
{
	std::lock_guard lock(s_apiLock);
	if (s_api && s_callbacksRegistered) {
		s_api->UnregisterEventCallback(&OnThreadEvent);
		s_api->UnregisterControlCallback(&OnControlEvent);
	}

	s_api = nullptr;
	s_callbacksRegistered = false;
	s_eventRevision.store(0, std::memory_order_release);
	s_lastEndedRevision.store(0, std::memory_order_release);
	s_lastEvent.store(RuntimeEvent::None, std::memory_order_release);
}

bool OStimNGThreadAPI::IsAvailable()
{
	return GetInterface() != nullptr;
}

std::uint64_t OStimNGThreadAPI::GetEventRevision()
{
	GetInterface();
	return s_eventRevision.load(std::memory_order_acquire);
}

std::uint64_t OStimNGThreadAPI::GetLastEndedRevision()
{
	GetInterface();
	return s_lastEndedRevision.load(std::memory_order_acquire);
}

OStimNGThreadAPI::RuntimeEvent OStimNGThreadAPI::GetLastEvent()
{
	GetInterface();
	return s_lastEvent.load(std::memory_order_acquire);
}

std::optional<OStimSceneInfo> OStimNGThreadAPI::GetCurrentSceneInfo()
{
	auto* api = GetInterface();
	const auto threadID = GetActiveThreadID();
	if (!api || !threadID.has_value()) {
		return std::nullopt;
	}

	const std::string sceneID = CopyString(api->GetCurrentSceneID(*threadID));
	if (sceneID.empty()) {
		return std::nullopt;
	}

	OStimSceneInfo info{};
	info.active = true;
	info.threadID = *threadID;
	info.sceneID = sceneID;
	info.animationID = sceneID;
	info.animationName = CopyString(api->GetCurrentNodeName(*threadID));
	info.currentSpeed = api->GetCurrentSpeed(*threadID);
	info.maxSpeed = api->GetMaxSpeed(*threadID);
	info.inTransition = api->IsTransition(*threadID);
	info.inSequence = api->IsInSequence(*threadID);
	info.playerControlDisabled = api->IsPlayerControlDisabled(*threadID);
	info.autoMode = api->IsAutoMode(*threadID);

	const std::uint32_t actorCount = api->GetActorCount(*threadID);
	if (actorCount > 0) {
		std::vector<OStimThread::ActorData> actorBuffer(actorCount);
		const std::uint32_t filled = api->GetActors(*threadID, actorBuffer.data(), actorCount);
		info.participants.reserve(filled);
		for (std::uint32_t i = 0; i < filled; ++i) {
			const auto& actorData = actorBuffer[i];
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorData.formID);
			OStimParticipantInfo participant{};
			participant.formID = actorData.formID;
			participant.name = actor ? std::string(actor->GetName() ? actor->GetName() : "") : std::string{};
			participant.isPlayer = actor ? actor->IsPlayerRef() : actorData.formID == 0x14;
			info.playerInvolved = info.playerInvolved || participant.isPlayer;
			info.participants.push_back(std::move(participant));
		}
		info.participantCount = static_cast<int>(info.participants.size());
	}

	if (info.animationName.empty()) {
		OStimThread::SceneSearchResult sceneInfo{};
		if (api->GetSceneInfo(sceneID.c_str(), &sceneInfo) && sceneInfo.name) {
			info.animationName = sceneInfo.name;
		}
	}

	return info;
}

std::vector<OStimPositionInfo> OStimNGThreadAPI::GetNavigationPositions(const OStimSceneInfo& a_sceneInfo)
{
	std::vector<OStimPositionInfo> positions;

	auto* api = GetInterface();
	const auto threadID = GetActiveThreadID();
	if (!api || !threadID.has_value()) {
		return positions;
	}

	const std::uint32_t navigationCount = api->GetNavigationCount(*threadID);
	if (navigationCount == 0) {
		return positions;
	}

	std::vector<OStimThread::NavigationData> buffer(navigationCount);
	const std::uint32_t filled = api->GetNavigationOptions(*threadID, buffer.data(), navigationCount);
	if (filled == 0) {
		return positions;
	}

	positions.reserve(filled);
	std::unordered_set<std::string> seenTargets;

	for (std::uint32_t i = 0; i < filled; ++i) {
		const auto& option = buffer[i];
		const std::string navigationSceneID = CopyString(option.sceneId);
		if (navigationSceneID.empty() || !seenTargets.insert(navigationSceneID).second) {
			continue;
		}

		OStimPositionInfo info{};
		info.id = navigationSceneID;
		info.sourceSceneID = a_sceneInfo.sceneID;
		info.destinationID = CopyString(option.destinationId);
		if (info.destinationID.empty()) {
			info.destinationID = info.id;
		}
		info.description = CopyString(option.description);
		info.category = a_sceneInfo.animationClass.empty() ? "Scene" : a_sceneInfo.animationClass;
		info.subcategory = a_sceneInfo.sourceModule;
		info.iconPath = CopyString(option.icon);
		info.requiresActiveScene = true;
		info.isValidNow = true;
		info.isTransition = option.isTransition;

		OStimThread::SceneSearchResult targetInfo{};
		if (api->GetSceneInfo(info.destinationID.c_str(), &targetInfo) && targetInfo.name) {
			info.displayName = targetInfo.name;
		} else if (!info.description.empty()) {
			info.displayName = info.description;
		} else {
			info.displayName = info.destinationID;
		}

		OStimPreviewResolver::Apply(info);
		positions.push_back(std::move(info));
	}

	return positions;
}

bool OStimNGThreadAPI::NavigateToScene(std::string_view a_sceneID)
{
	auto* api = GetInterface();
	const auto threadID = GetActiveThreadID();
	if (!api || !threadID.has_value() || a_sceneID.empty()) {
		return false;
	}

	const std::string sceneID(a_sceneID);
	return api->NavigateToScene(*threadID, sceneID.c_str()) == NativeResult::OK;
}

bool OStimNGThreadAPI::AdjustSpeed(int a_delta)
{
	auto* api = GetInterface();
	const auto threadID = GetActiveThreadID();
	if (!api || !threadID.has_value() || a_delta == 0) {
		return false;
	}

	const int currentSpeed = api->GetCurrentSpeed(*threadID);
	const int maxSpeed = api->GetMaxSpeed(*threadID);
	const int desiredSpeed = std::clamp(currentSpeed + a_delta, 0, maxSpeed);
	if (desiredSpeed == currentSpeed) {
		return false;
	}

	return api->SetSpeed(*threadID, desiredSpeed) == NativeResult::OK;
}
