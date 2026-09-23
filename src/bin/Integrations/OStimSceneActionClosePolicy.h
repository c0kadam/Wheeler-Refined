#pragma once

#include <cstdint>

namespace OStimSceneActionUI
{
	enum class Decision : std::uint8_t
	{
		NotApplicable = 0,
		Close,
		KeepOpen
	};

	struct Policy
	{
		Decision decision = Decision::NotApplicable;
		bool resetNavigationPage = false;
		bool keepUnifiedWheelActive = false;
		bool suppressPreviousNavigation = false;
	};

	constexpr Policy Evaluate(
		bool a_successfulSelectPosition,
		bool a_closeAfterAction) noexcept
	{
		if (!a_successfulSelectPosition) {
			return {};
		}
		if (a_closeAfterAction) {
			return { Decision::Close, true, false, false };
		}
		return { Decision::KeepOpen, true, true, true };
	}

	constexpr bool ShouldHoldAcceptedNavigation(
		bool a_dispatchPending,
		bool a_sceneActive,
		bool a_currentSceneMatchesSource) noexcept
	{
		return a_dispatchPending && a_sceneActive && a_currentSceneMatchesSource;
	}
}
