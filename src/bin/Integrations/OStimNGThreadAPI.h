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

	static void Reset();
	static bool IsAvailable();
	static std::uint64_t GetEventRevision();
	static std::uint64_t GetLastEndedRevision();
	static RuntimeEvent GetLastEvent();
	static std::optional<OStimSceneInfo> GetCurrentSceneInfo();
	static std::vector<OStimPositionInfo> GetNavigationPositions(const OStimSceneInfo& a_sceneInfo);
	static bool NavigateToScene(std::string_view a_sceneID);
	static bool AdjustSpeed(int a_delta);
};
