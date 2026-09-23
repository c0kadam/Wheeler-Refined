#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace PoisonDiagnosticPolicy
{
	using Epoch = std::uint64_t;
	using FormID = std::uint32_t;

	enum class Phase : std::uint8_t
	{
		Inactive,
		AwaitEngineBoundary,
		AwaitFollowup
	};

	enum class Decision : std::uint8_t
	{
		None,
		EmitBoundary,
		EmitFinal,
		Cancel
	};

	struct Watcher
	{
		std::uint64_t eventID = 0;
		Epoch epoch = 0;
		std::array<FormID, 2> candidateFormIDs{};
		std::uint8_t candidateCount = 0;
		Phase phase = Phase::Inactive;
		bool sawInventoryMenu = false;
		double completionDeadline = 0.0;
		double hardDeadline = 0.0;
		double followupDeadline = 0.0;
		std::uint32_t updatesRemaining = 0;

		[[nodiscard]] constexpr bool IsActive() const noexcept
		{
			return eventID != 0 && epoch != 0 && phase != Phase::Inactive;
		}

		constexpr void Clear() noexcept
		{
			*this = {};
		}
	};

	[[nodiscard]] constexpr Watcher Arm(
		std::uint64_t a_eventID,
		Epoch a_epoch,
		const std::array<FormID, 2>& a_candidates,
		std::uint8_t a_candidateCount,
		double a_now) noexcept
	{
		Watcher watcher;
		watcher.eventID = a_eventID;
		watcher.epoch = a_epoch;
		watcher.candidateFormIDs = a_candidates;
		watcher.candidateCount = a_candidateCount > watcher.candidateFormIDs.size() ?
			static_cast<std::uint8_t>(watcher.candidateFormIDs.size()) : a_candidateCount;
		watcher.phase = Phase::AwaitEngineBoundary;
		watcher.completionDeadline = a_now + 30.0;
		watcher.hardDeadline = a_now + 35.0;
		watcher.updatesRemaining = 10000;
		return watcher;
	}

	[[nodiscard]] constexpr Decision Advance(
		Watcher& a_watcher,
		Epoch a_currentEpoch,
		double a_now,
		bool a_inventoryMenuOpen) noexcept
	{
		if (!a_watcher.IsActive()) {
			return Decision::None;
		}
		if (a_watcher.epoch != a_currentEpoch) {
			a_watcher.Clear();
			return Decision::Cancel;
		}
		if (a_watcher.updatesRemaining == 0 || a_now >= a_watcher.hardDeadline) {
			a_watcher.phase = Phase::Inactive;
			return Decision::EmitFinal;
		}
		--a_watcher.updatesRemaining;

		if (a_watcher.phase == Phase::AwaitEngineBoundary) {
			a_watcher.sawInventoryMenu = a_watcher.sawInventoryMenu || a_inventoryMenuOpen;
			if ((a_watcher.sawInventoryMenu && !a_inventoryMenuOpen) ||
				a_now >= a_watcher.completionDeadline) {
				a_watcher.phase = Phase::AwaitFollowup;
				a_watcher.followupDeadline = a_now + 2.0;
				return Decision::EmitBoundary;
			}
			return Decision::None;
		}

		if (a_watcher.phase == Phase::AwaitFollowup && a_now >= a_watcher.followupDeadline) {
			a_watcher.phase = Phase::Inactive;
			return Decision::EmitFinal;
		}
		return Decision::None;
	}
}
