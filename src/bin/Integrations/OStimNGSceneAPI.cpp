#include "OStimNGSceneAPI.h"

#include "Plugin.h"

#include <REL/Relocation.h>

#include <Windows.h>

#include <cstdint>
#include <mutex>

namespace RE
{
	class Actor;
	class TESObjectREFR;
}

namespace OStimNGInterop::Scene
{
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

	class ISceneInterface
	{
	public:
		virtual APIResult StartScene(
			const char* pluginName,
			RE::TESObjectREFR* furniture,
			const char* startingAnimation,
			RE::Actor* actors[256],
			std::uint32_t* threadID) noexcept = 0;
		virtual APIResult StartCoupleScene(
			const char* pluginName,
			RE::TESObjectREFR* furniture,
			const char* startingAnimation,
			RE::Actor* dom,
			RE::Actor* sub,
			std::uint32_t* threadID) noexcept = 0;
		virtual APIResult StartThreesomeScene(
			const char* pluginName,
			RE::TESObjectREFR* furniture,
			const char* startingAnimation,
			RE::Actor* firstActor,
			RE::Actor* secondActor,
			RE::Actor* thirdActor,
			std::uint32_t* threadID) noexcept = 0;
		virtual APIResult StartFoursomeScene(
			const char* pluginName,
			RE::TESObjectREFR* furniture,
			const char* startingAnimation,
			RE::Actor* firstActor,
			RE::Actor* secondActor,
			RE::Actor* thirdActor,
			RE::Actor* fourthActor,
			std::uint32_t* threadID) noexcept = 0;
		virtual APIResult StopScene(const char* pluginName, std::uint32_t threadID) noexcept = 0;
		virtual APIResult SetAutoMode(
			const char* pluginName,
			std::uint32_t threadID,
			bool autoMode) noexcept = 0;
		virtual APIResult TryGetAutoMode(
			const char* pluginName,
			std::uint32_t threadID,
			bool* autoMode) noexcept = 0;
	};

	using RequestPluginAPIScene =
		ISceneInterface* (*)(InterfaceVersion a_interfaceVersion, const char* a_pluginName, REL::Version a_pluginVersion);
}

namespace
{
	using NativeAPI = OStimNGInterop::Scene::ISceneInterface;
	using NativeResult = OStimNGInterop::Scene::APIResult;

	std::mutex s_apiLock;
	NativeAPI* s_api = nullptr;
	bool s_lookupAttempted = false;
	bool s_unavailableLogged = false;

	void LogUnavailableOnce(const char* a_reason)
	{
		if (s_unavailableLogged) {
			return;
		}
		s_unavailableLogged = true;
		logger::warn(
			"[OStimIntegration] native Scene API unavailable; Auto Mode control disabled reason='{}'",
			a_reason);
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
		if (s_lookupAttempted) {
			return nullptr;
		}
		s_lookupAttempted = true;

		const auto ostim = GetModuleHandleA("OStim.dll");
		if (!ostim) {
			LogUnavailableOnce("OStimModuleMissing");
			return nullptr;
		}

		const auto requestAPI = reinterpret_cast<OStimNGInterop::Scene::RequestPluginAPIScene>(
			reinterpret_cast<void*>(GetProcAddress(ostim, "RequestPluginAPI_Scene")));
		if (!requestAPI) {
			LogUnavailableOnce("RequestPluginAPI_SceneMissing");
			return nullptr;
		}

		s_api = requestAPI(
			OStimNGInterop::Scene::InterfaceVersion::V1,
			Plugin::NAME.data(),
			Plugin::VERSION);
		if (!s_api) {
			LogUnavailableOnce("InterfaceV1Rejected");
		}
		return s_api;
	}
}

bool OStimNGSceneAPI::IsAvailable()
{
	return GetInterface() != nullptr;
}

OStimNGSceneAPI::DispatchResult OStimNGSceneAPI::SetAutoMode(
	std::uint32_t a_threadID,
	bool a_autoMode)
{
	auto* api = GetInterface();
	if (!api) {
		return DispatchResult::APIUnavailable;
	}

	switch (api->SetAutoMode(Plugin::NAME.data(), a_threadID, a_autoMode)) {
	case NativeResult::OK:
		return DispatchResult::Success;
	case NativeResult::Invalid:
		return DispatchResult::Invalid;
	case NativeResult::Failed:
	default:
		return DispatchResult::Failed;
	}
}

const char* OStimNGSceneAPI::GetDispatchResultReason(DispatchResult a_result)
{
	switch (a_result) {
	case DispatchResult::Success:
		return "";
	case DispatchResult::APIUnavailable:
		return "SceneAPIUnavailable";
	case DispatchResult::Invalid:
		return "Invalid";
	case DispatchResult::Failed:
	default:
		return "Failed";
	}
}
