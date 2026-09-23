#pragma once

#include "OStimTypes.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

class OStimNGThreadAPI
{
public:
	enum class RuntimeEvent : std::uint8_t
	{
		None = 0,
		ThreadStarted,
		ThreadEnded,
		NodeChanged,
		ControlInput
	};

	struct DiagnosticEvent
	{
		std::uint64_t revision = 0;
		RuntimeEvent event = RuntimeEvent::None;
		std::uint32_t threadID = 0;
		std::uint32_t control = 0;
	};

	static void Reset();
	static void SetDiagnosticsEnabled(bool a_enabled);
	static bool IsAvailable();
	static std::uint64_t GetEventRevision();
	static std::uint64_t GetLastEndedRevision();
	static RuntimeEvent GetLastEvent();
	static std::vector<DiagnosticEvent> GetDiagnosticEventsAfter(std::uint64_t a_revision);
	static std::optional<OStimSceneInfo> GetCurrentSceneInfo();
	static std::vector<OStimPositionInfo> GetNavigationPositions(const OStimSceneInfo& a_sceneInfo);
	static bool NavigateToScene(std::uint32_t a_threadID, std::string_view a_sceneID);
	static bool AdjustSpeed(int a_delta);
};
