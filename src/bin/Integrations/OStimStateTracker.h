#pragma once

#include "OStimTypes.h"

#include <optional>
#include <vector>

struct OStimTrackerSnapshot
{
	OStimAvailabilityInfo availability{};
	std::optional<OStimSceneInfo> sceneInfo{};
	std::vector<OStimPositionInfo> positions{};
	std::uint64_t revision = 0;
};

class OStimStateTracker
{
public:
	static void Reset();
	static void Update(bool a_force = false);
	static void InvalidatePositions();
	static void MarkActionExecuted(OStimActionKind a_action);

	static OStimAvailabilityInfo GetAvailability();
	static bool IsSceneActive();
	static OStimTrackerSnapshot GetSnapshot();
	static std::optional<OStimSceneInfo> GetCurrentSceneInfo();
	static std::vector<OStimPositionInfo> GetAvailablePositions();
	static bool CanDispatchByCooldown(OStimActionKind a_action);
	static std::uint64_t GetRevision();
};
