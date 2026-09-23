#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <set>
#include <vector>

namespace OStimUndressVisualRefreshPolicy
{
	using FormID = std::uint32_t;
	using WornArmorSet = std::set<FormID>;

	inline constexpr std::int64_t kRefreshDelayMs = 250;

	struct ArmorObservation
	{
		FormID formID = 0;
		std::uint32_t slotMask = 0;
		bool worn = false;
	};

	inline WornArmorSet BuildWornArmorSet(const std::vector<ArmorObservation>& a_observations)
	{
		WornArmorSet result;
		for (const auto& observation : a_observations) {
			if (observation.worn && observation.slotMask != 0 && observation.formID != 0) {
				result.insert(observation.formID);
			}
		}
		return result;
	}

	inline WornArmorSet GetRemovedWornArmor(
		const WornArmorSet& a_previous,
		const WornArmorSet& a_current)
	{
		WornArmorSet removed;
		std::set_difference(
			a_previous.begin(),
			a_previous.end(),
			a_current.begin(),
			a_current.end(),
			std::inserter(removed, removed.end()));
		return removed;
	}

	struct RefreshKey
	{
		FormID actorFormID = 0;
		std::uint64_t eventRevision = 0;

		constexpr bool operator==(const RefreshKey&) const noexcept = default;
	};

	constexpr bool ShouldArmRefresh(
		bool a_settingEnabled,
		bool a_duplicate,
		std::size_t a_removedCount) noexcept
	{
		return a_settingEnabled && !a_duplicate && a_removedCount > 0;
	}

	enum class ConfigTransition : std::uint8_t
	{
		None = 0,
		CaptureBaseline,
		Disable
	};

	constexpr ConfigTransition EvaluateConfigTransition(
		bool a_wasEnabled,
		bool a_isEnabled,
		bool a_sceneActive) noexcept
	{
		if (a_wasEnabled && !a_isEnabled) {
			return ConfigTransition::Disable;
		}
		if (!a_wasEnabled && a_isEnabled && a_sceneActive) {
			return ConfigTransition::CaptureBaseline;
		}
		return ConfigTransition::None;
	}

	enum class ExecutionStatus : std::uint8_t
	{
		Ready = 0,
		SettingDisabled,
		SceneInactive,
		GenerationChanged,
		ThreadChanged,
		ActorNotParticipant,
		RequestSuperseded,
		ActorMissing,
		Actor3DUnloaded
	};

	struct ExecutionFacts
	{
		bool settingEnabled = false;
		bool sceneActive = false;
		bool generationMatches = false;
		bool threadMatches = false;
		bool actorIsParticipant = false;
		bool requestIsCurrent = false;
		bool actorExists = false;
		bool actor3DLoaded = false;
	};

	constexpr ExecutionStatus EvaluateExecution(const ExecutionFacts& a_facts) noexcept
	{
		if (!a_facts.settingEnabled) {
			return ExecutionStatus::SettingDisabled;
		}
		if (!a_facts.sceneActive) {
			return ExecutionStatus::SceneInactive;
		}
		if (!a_facts.generationMatches) {
			return ExecutionStatus::GenerationChanged;
		}
		if (!a_facts.threadMatches) {
			return ExecutionStatus::ThreadChanged;
		}
		if (!a_facts.actorIsParticipant) {
			return ExecutionStatus::ActorNotParticipant;
		}
		if (!a_facts.requestIsCurrent) {
			return ExecutionStatus::RequestSuperseded;
		}
		if (!a_facts.actorExists) {
			return ExecutionStatus::ActorMissing;
		}
		if (!a_facts.actor3DLoaded) {
			return ExecutionStatus::Actor3DUnloaded;
		}
		return ExecutionStatus::Ready;
	}
}

class OStimUndressVisualRefresh
{
public:
	static void Reset();
	static void Update(bool a_ostimAvailable, bool a_sceneActive);
};
