#pragma once

#include <cstdint>
#include <utility>

struct OStimAutoModeDecision
{
	bool canDispatch = false;
	bool currentAutoMode = false;
	bool desiredAutoMode = false;
	const char* rejectionReason = "Unknown";
};

constexpr const char* GetOStimAutoModeLabel(bool a_autoMode) noexcept
{
	return a_autoMode ? "Auto Progress: ON" : "Auto Progress: OFF";
}

constexpr OStimAutoModeDecision BuildOStimAutoModeDecision(
	bool a_sceneActive,
	bool a_sceneAPIAvailable,
	bool a_currentAutoMode) noexcept
{
	if (!a_sceneActive) {
		return { false, a_currentAutoMode, !a_currentAutoMode, "SceneInactive" };
	}
	if (!a_sceneAPIAvailable) {
		return { false, a_currentAutoMode, !a_currentAutoMode, "SceneAPIUnavailable" };
	}
	return { true, a_currentAutoMode, !a_currentAutoMode, "" };
}

template <class TSetter>
bool DispatchOStimAutoMode(
	const OStimAutoModeDecision& a_decision,
	std::uint32_t a_threadID,
	TSetter&& a_setter)
{
	if (!a_decision.canDispatch) {
		return false;
	}
	return static_cast<bool>(std::forward<TSetter>(a_setter)(
		a_threadID,
		a_decision.desiredAutoMode));
}
