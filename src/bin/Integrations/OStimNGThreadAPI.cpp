#include "OStimNGThreadAPI.h"

#include "OStimPreviewResolver.h"
#include "Plugin.h"

#include <RE/T/TESForm.h>
#include <REL/Relocation.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace OStimNGInterop::Thread
{
	using f32 = float;

	enum class InterfaceVersion : std::uint8_t
	{
		V1
	};

	enum class APIResult : std::uint8_t
	{
		OK,
		Invalid,
		Failed
	};

	enum class ThreadEvent : std::uint8_t
	{
		ThreadStarted,
		ThreadEnded,
		NodeChanged,
		ControlInput
	};

	enum class Controls : std::uint8_t
	{
		Up,
		Down,
		Left,
		Right,
		Toggle,
		Yes,
		No,
		Menu,
		KEY_HIDE,
		AlignMenu,
		SearchMenu
	};

	struct ActorData
	{
		std::uint32_t formID;
		f32 excitement;
		bool isFemale;
		bool hasSchlong;
		std::int32_t timesClimaxed;
	};

	struct NavigationData
	{
		const char* sceneId;
		const char* destinationId;
		const char* icon;
		const char* description;
		const char* border;
		bool isTransition;
	};

	struct SceneSearchResult
	{
		const char* sceneId;
		const char* name;
		std::uint32_t actorCount;
	};

	using ThreadEventCallback = void (*)(ThreadEvent eventType, std::uint32_t threadID, void* userData);
	using ControlEventCallback = void (*)(Controls controlType, std::uint32_t threadID, void* userData);

	class IThreadInterface
	{
	public:
		virtual std::uint32_t GetPlayerThreadID() noexcept = 0;
		virtual bool IsThreadValid(std::uint32_t threadID) noexcept = 0;
		virtual const char* GetCurrentSceneID(std::uint32_t threadID) noexcept = 0;
		virtual std::uint32_t GetActorCount(std::uint32_t threadID) noexcept = 0;
		virtual std::uint32_t GetActors(std::uint32_t threadID, ActorData* buffer, std::uint32_t bufferSize) noexcept = 0;
		virtual std::uint32_t GetNavigationCount(std::uint32_t threadID) noexcept = 0;
		virtual std::uint32_t GetNavigationOptions(std::uint32_t threadID, NavigationData* buffer, std::uint32_t bufferSize) noexcept = 0;
		virtual APIResult NavigateToScene(std::uint32_t threadID, const char* sceneID) noexcept = 0;
		virtual bool IsTransition(std::uint32_t threadID) noexcept = 0;
		virtual bool IsInSequence(std::uint32_t threadID) noexcept = 0;
		virtual bool IsAutoMode(std::uint32_t threadID) noexcept = 0;
		virtual bool IsPlayerControlDisabled(std::uint32_t threadID) noexcept = 0;
		virtual void RegisterEventCallback(ThreadEventCallback callback, void* userData) noexcept = 0;
		virtual void UnregisterEventCallback(ThreadEventCallback callback) noexcept = 0;
		virtual void RegisterControlCallback(ControlEventCallback callback, void* userData) noexcept = 0;
		virtual void UnregisterControlCallback(ControlEventCallback callback) noexcept = 0;
		virtual void SetExternalUIEnabled(bool enabled) noexcept = 0;
		virtual void GetKeyData(void* outData) noexcept = 0;
		virtual const char* GetCurrentNodeName(std::uint32_t threadID) noexcept = 0;
		virtual std::int32_t GetCurrentSpeed(std::uint32_t threadID) noexcept = 0;
		virtual std::int32_t GetMaxSpeed(std::uint32_t threadID) noexcept = 0;
		virtual APIResult SetSpeed(std::uint32_t threadID, std::int32_t speed) noexcept = 0;
		virtual bool GetActorAlignment(std::uint32_t threadID, std::uint32_t actorIndex, void* outData) noexcept = 0;
		virtual APIResult SetActorAlignment(std::uint32_t threadID, std::uint32_t actorIndex, const void* data) noexcept = 0;
		virtual std::uint32_t SearchScenes(const char* query, SceneSearchResult* buffer, std::uint32_t bufferSize) noexcept = 0;
		virtual bool GetSceneInfo(const char* sceneID, SceneSearchResult* outInfo) noexcept = 0;
		virtual APIResult NavigateToSearchResult(std::uint32_t threadID, const char* sceneID) noexcept = 0;
	};

	using RequestPluginAPIThread =
		IThreadInterface* (*)(InterfaceVersion a_interfaceVersion, const char* a_pluginName, REL::Version a_pluginVersion);

	inline IThreadInterface* GetAPI(const char* a_pluginName, REL::Version a_pluginVersion)
	{
		const auto ostim = GetModuleHandleA("OStim.dll");
		if (!ostim) {
			return nullptr;
		}

		const auto requestAPI = reinterpret_cast<RequestPluginAPIThread>(
			reinterpret_cast<void*>(GetProcAddress(ostim, "RequestPluginAPI_Thread")));
		if (!requestAPI) {
			return nullptr;
		}

		return requestAPI(InterfaceVersion::V1, a_pluginName, a_pluginVersion);
	}
}

namespace
{
	using NativeAPI = OStimNGInterop::Thread::IThreadInterface;
	using NativeResult = OStimNGInterop::Thread::APIResult;
	using NativeThreadEvent = OStimNGInterop::Thread::ThreadEvent;
	using NativeControl = OStimNGInterop::Thread::Controls;

	std::mutex s_apiLock;
	std::mutex s_nativeReadLock;
	NativeAPI* s_api = nullptr;
	bool s_callbacksRegistered = false;
	std::atomic<std::uint64_t> s_eventRevision{ 0 };
	std::atomic<std::uint64_t> s_lastEndedRevision{ 0 };
	std::atomic<OStimNGThreadAPI::RuntimeEvent> s_lastEvent{ OStimNGThreadAPI::RuntimeEvent::None };
	std::atomic_bool s_diagnosticsEnabled{ false };

	constexpr std::size_t kDiagnosticEventCapacity = 64;
	struct DiagnosticEventSlot
	{
		std::atomic<std::uint64_t> revision{ 0 };
		std::atomic<OStimNGThreadAPI::RuntimeEvent> event{ OStimNGThreadAPI::RuntimeEvent::None };
		std::atomic<std::uint32_t> threadID{ 0 };
		std::atomic<std::uint32_t> control{ 0 };
	};
	std::array<DiagnosticEventSlot, kDiagnosticEventCapacity> s_diagnosticEvents{};

	struct OwnedNavigationData
	{
		std::string sceneID;
		std::string destinationID;
		std::string icon;
		std::string description;
		std::string displayName;
		bool isTransition = false;
	};

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

	void RecordDiagnosticEvent(
		std::uint64_t a_revision,
		OStimNGThreadAPI::RuntimeEvent a_event,
		std::uint32_t a_threadID,
		std::uint32_t a_control = 0)
	{
		if (!s_diagnosticsEnabled.load(std::memory_order_acquire)) {
			return;
		}

		auto& slot = s_diagnosticEvents[a_revision % kDiagnosticEventCapacity];
		slot.event.store(a_event, std::memory_order_relaxed);
		slot.threadID.store(a_threadID, std::memory_order_relaxed);
		slot.control.store(a_control, std::memory_order_relaxed);
		slot.revision.store(a_revision, std::memory_order_release);
	}

	void OnThreadEvent(NativeThreadEvent a_event, std::uint32_t a_threadID, void*)
	{
		const auto translated = TranslateEvent(a_event);
		s_lastEvent.store(translated, std::memory_order_release);
		const auto revision = s_eventRevision.fetch_add(1, std::memory_order_acq_rel) + 1;
		if (translated == OStimNGThreadAPI::RuntimeEvent::ThreadEnded) {
			s_lastEndedRevision.store(revision, std::memory_order_release);
		}
		RecordDiagnosticEvent(revision, translated, a_threadID);
	}

	void OnControlEvent(NativeControl a_control, std::uint32_t a_threadID, void*)
	{
		s_lastEvent.store(OStimNGThreadAPI::RuntimeEvent::ControlInput, std::memory_order_release);
		const auto revision = s_eventRevision.fetch_add(1, std::memory_order_acq_rel) + 1;
		RecordDiagnosticEvent(
			revision,
			OStimNGThreadAPI::RuntimeEvent::ControlInput,
			a_threadID,
			static_cast<std::uint32_t>(a_control));
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

		auto* api = OStimNGInterop::Thread::GetAPI(Plugin::NAME.data(), Plugin::VERSION);
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
	for (auto& slot : s_diagnosticEvents) {
		slot.revision.store(0, std::memory_order_release);
		slot.event.store(RuntimeEvent::None, std::memory_order_relaxed);
		slot.threadID.store(0, std::memory_order_relaxed);
		slot.control.store(0, std::memory_order_relaxed);
	}
}

void OStimNGThreadAPI::SetDiagnosticsEnabled(bool a_enabled)
{
	s_diagnosticsEnabled.store(a_enabled, std::memory_order_release);
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

std::vector<OStimNGThreadAPI::DiagnosticEvent> OStimNGThreadAPI::GetDiagnosticEventsAfter(
	std::uint64_t a_revision)
{
	std::vector<DiagnosticEvent> events;
	if (!s_diagnosticsEnabled.load(std::memory_order_acquire)) {
		return events;
	}

	const std::uint64_t currentRevision = s_eventRevision.load(std::memory_order_acquire);
	if (currentRevision <= a_revision) {
		return events;
	}

	const std::uint64_t earliestAvailable = currentRevision >= kDiagnosticEventCapacity ?
		currentRevision - kDiagnosticEventCapacity + 1 :
		1;
	const std::uint64_t firstRevision = (std::max)(a_revision + 1, earliestAvailable);
	events.reserve(static_cast<std::size_t>(currentRevision - firstRevision + 1));
	for (std::uint64_t revision = firstRevision; revision <= currentRevision; ++revision) {
		auto& slot = s_diagnosticEvents[revision % kDiagnosticEventCapacity];
		if (slot.revision.load(std::memory_order_acquire) != revision) {
			continue;
		}

		DiagnosticEvent event{};
		event.revision = revision;
		event.event = slot.event.load(std::memory_order_relaxed);
		event.threadID = slot.threadID.load(std::memory_order_relaxed);
		event.control = slot.control.load(std::memory_order_relaxed);
		if (slot.revision.load(std::memory_order_acquire) == revision) {
			events.push_back(event);
		}
	}
	return events;
}

std::optional<OStimSceneInfo> OStimNGThreadAPI::GetCurrentSceneInfo()
{
	auto* api = GetInterface();
	if (!api) {
		return std::nullopt;
	}

	std::lock_guard readLock(s_nativeReadLock);
	const std::uint32_t threadID = api->GetPlayerThreadID();
	if (!api->IsThreadValid(threadID)) {
		return std::nullopt;
	}

	const std::string sceneID = CopyString(api->GetCurrentSceneID(threadID));
	if (sceneID.empty()) {
		return std::nullopt;
	}

	OStimSceneInfo info{};
	info.active = true;
	info.threadID = threadID;
	info.sceneID = sceneID;
	info.animationID = sceneID;
	info.animationName = CopyString(api->GetCurrentNodeName(threadID));
	info.currentSpeed = api->GetCurrentSpeed(threadID);
	info.maxSpeed = api->GetMaxSpeed(threadID);
	info.inTransition = api->IsTransition(threadID);
	info.inSequence = api->IsInSequence(threadID);
	info.playerControlDisabled = api->IsPlayerControlDisabled(threadID);
	info.autoMode = api->IsAutoMode(threadID);

	const std::uint32_t actorCount = api->GetActorCount(threadID);
	if (actorCount > 0) {
		std::vector<OStimNGInterop::Thread::ActorData> actorBuffer(actorCount);
		const std::uint32_t filled = api->GetActors(threadID, actorBuffer.data(), actorCount);
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
		OStimNGInterop::Thread::SceneSearchResult sceneInfo{};
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
	if (!api) {
		return positions;
	}

	std::vector<OwnedNavigationData> ownedOptions;
	{
		std::lock_guard readLock(s_nativeReadLock);
		const bool threadValid = api->IsThreadValid(a_sceneInfo.threadID);
		if (!threadValid) {
			if (s_diagnosticsEnabled.load(std::memory_order_acquire)) {
				logger::info(
					"[OStimDiag] NATIVE_NAV_QUERY thread={} valid=0 count=0",
					a_sceneInfo.threadID);
			}
			return positions;
		}

		const std::uint32_t navigationCount = api->GetNavigationCount(a_sceneInfo.threadID);
		if (s_diagnosticsEnabled.load(std::memory_order_acquire)) {
			logger::info(
				"[OStimDiag] NATIVE_NAV_QUERY thread={} valid=1 count={}",
				a_sceneInfo.threadID,
				navigationCount);
		}
		if (navigationCount == 0) {
			return positions;
		}

		std::vector<OStimNGInterop::Thread::NavigationData> buffer(navigationCount);
		const std::uint32_t filled = api->GetNavigationOptions(
			a_sceneInfo.threadID,
			buffer.data(),
			navigationCount);
		if (s_diagnosticsEnabled.load(std::memory_order_acquire)) {
			logger::info(
				"[OStimDiag] NATIVE_NAV_FILLED thread={} requested={} filled={}",
				a_sceneInfo.threadID,
				navigationCount,
				filled);
		}
		if (filled == 0) {
			return positions;
		}

		ownedOptions.reserve(filled);
		for (std::uint32_t i = 0; i < filled; ++i) {
			const auto& option = buffer[i];
			OwnedNavigationData owned{};
			owned.sceneID = CopyString(option.sceneId);
			owned.destinationID = CopyString(option.destinationId);
			owned.icon = CopyString(option.icon);
			owned.description = CopyString(option.description);
			owned.isTransition = option.isTransition;
			ownedOptions.push_back(std::move(owned));
		}

		for (auto& option : ownedOptions) {
			const std::string& destinationID = option.destinationID.empty() ?
				option.sceneID : option.destinationID;
			OStimNGInterop::Thread::SceneSearchResult targetInfo{};
			if (api->GetSceneInfo(destinationID.c_str(), &targetInfo)) {
				option.displayName = CopyString(targetInfo.name);
			}
		}
	}

	positions.reserve(ownedOptions.size());
	std::unordered_set<std::string> seenTargets;
	for (auto& option : ownedOptions) {
		if (option.sceneID.empty() || !seenTargets.insert(option.sceneID).second) {
			continue;
		}

		OStimPositionInfo info{};
		info.id = std::move(option.sceneID);
		info.sourceSceneID = a_sceneInfo.sceneID;
		info.destinationID = option.destinationID.empty() ? info.id : std::move(option.destinationID);
		info.description = std::move(option.description);
		info.category = a_sceneInfo.animationClass.empty() ? "Scene" : a_sceneInfo.animationClass;
		info.subcategory = a_sceneInfo.sourceModule;
		info.iconPath = std::move(option.icon);
		info.requiresActiveScene = true;
		info.isValidNow = true;
		info.isTransition = option.isTransition;
		info.displayName = !option.displayName.empty() ?
			std::move(option.displayName) :
			(!info.description.empty() ? info.description : info.destinationID);

		OStimPreviewResolver::Apply(info);
		positions.push_back(std::move(info));
	}

	return positions;
}

bool OStimNGThreadAPI::NavigateToScene(std::uint32_t a_threadID, std::string_view a_sceneID)
{
	auto* api = GetInterface();
	if (!api || a_sceneID.empty()) {
		return false;
	}
	if (!api->IsThreadValid(a_threadID)) {
		return false;
	}

	const std::string sceneID(a_sceneID);
	return api->NavigateToScene(a_threadID, sceneID.c_str()) == NativeResult::OK;
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
