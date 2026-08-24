#pragma once

#include "OStimTypes.h"

#include <string_view>

class OStimPreviewResolver
{
public:
	static void Apply(OStimPositionInfo& a_position);
	static std::vector<OStimPositionInfo> GetSceneNavigationChildren(std::string_view a_sceneID, std::string_view a_parentSceneID = {});
	static bool HasBrowsableChildScenes(std::string_view a_sceneID, std::string_view a_parentSceneID);
};
