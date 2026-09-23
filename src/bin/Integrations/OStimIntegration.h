#pragma once

#include "OStimTypes.h"

#include <optional>
#include <string_view>
#include <vector>

class Wheel;

class OStimIntegration
{
public:
	static void Init();
	static void Reset();
	static void Update();
	static void RequestRefresh();

	static bool IsAvailable();
	static bool IsEnabled();
	static bool IsSceneActive();
	static bool CanExecuteAction(OStimActionKind a_kind, const OStimActionPayload* a_payload = nullptr);
	static bool ExecuteAction(OStimActionKind a_kind, const OStimActionPayload* a_payload = nullptr);
	static std::vector<OStimPositionInfo> GetAvailablePositions();
	static std::optional<OStimSceneInfo> GetCurrentSceneInfo();
	static bool ShouldBlockRegularWheelActivation(std::string_view a_itemTypeName);
	static const char* GetActionLabel(OStimActionKind a_kind);
	static bool IsManagedWheelTag(std::string_view a_tag);
	static bool IsManagedWheelIndex(int a_wheelIndex);
};
