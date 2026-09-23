#pragma once

#include <cstdint>

namespace HandMemoryAttackDiagnosticPolicy
{
	using Epoch = std::uint64_t;
	using FormID = std::uint32_t;

	inline constexpr double kPostRestoreObservationSeconds = 5.0;
	inline constexpr double kOverallHardLimitSeconds = 6.0;
	inline constexpr double kPeriodicSampleSeconds = 0.5;
	inline constexpr std::uint32_t kMaximumFollowupSamples = 40;

	enum class Phase : std::uint8_t
	{
		Inactive,
		Restoring,
		PostRestoreObservation
	};

	enum class AdvanceDecision : std::uint8_t
	{
		None,
		EmitFollowup,
		Timeout,
		Cancel
	};

	enum class AttackInputDisposition : std::uint8_t
	{
		ObserveOnlyPassThroughUnchanged
	};

	struct Fingerprint
	{
		FormID equippedLeft = 0;
		FormID equippedRight = 0;
		std::uint32_t weaponState = 0;
		std::uint32_t attackState = 0;
		bool in2H = false;
		bool bEquipOKQuerySucceeded = false;
		bool bEquipOK = false;
		bool attackReadyQuerySucceeded = false;
		bool attackReady = false;

		[[nodiscard]] constexpr bool operator==(const Fingerprint&) const noexcept = default;
	};

	struct Watcher
	{
		std::uint64_t transactionID = 0;
		Epoch epoch = 0;
		Phase phase = Phase::Inactive;
		FormID expectedLeft = 0;
		FormID expectedRight = 0;
		double startedAt = 0.0;
		double hardDeadline = 0.0;
		double observationDeadline = 0.0;
		double nextPeriodicSampleAt = 0.0;
		std::uint32_t followupSamplesRemaining = 0;
		Fingerprint lastFingerprint{};
		bool hasLastFingerprint = false;

		[[nodiscard]] constexpr bool IsActive() const noexcept
		{
			return transactionID != 0 && epoch != 0 && phase != Phase::Inactive;
		}

		constexpr void Clear() noexcept
		{
			*this = {};
		}
	};

	[[nodiscard]] constexpr Watcher Arm(
		std::uint64_t a_transactionID,
		Epoch a_epoch,
		FormID a_expectedLeft,
		FormID a_expectedRight,
		double a_now) noexcept
	{
		Watcher watcher;
		watcher.transactionID = a_transactionID;
		watcher.epoch = a_epoch;
		watcher.phase = Phase::Restoring;
		watcher.expectedLeft = a_expectedLeft;
		watcher.expectedRight = a_expectedRight;
		watcher.startedAt = a_now;
		watcher.hardDeadline = a_now + kOverallHardLimitSeconds;
		return watcher;
	}

	constexpr void BeginPostRestoreObservation(Watcher& a_watcher, double a_now) noexcept
	{
		if (!a_watcher.IsActive()) {
			return;
		}
		a_watcher.phase = Phase::PostRestoreObservation;
		a_watcher.observationDeadline = a_now + kPostRestoreObservationSeconds;
		a_watcher.nextPeriodicSampleAt = a_now;
		a_watcher.followupSamplesRemaining = kMaximumFollowupSamples;
		a_watcher.hasLastFingerprint = false;
	}

	[[nodiscard]] constexpr AdvanceDecision Advance(
		Watcher& a_watcher,
		Epoch a_currentEpoch,
		double a_now,
		const Fingerprint& a_fingerprint) noexcept
	{
		if (!a_watcher.IsActive()) {
			return AdvanceDecision::None;
		}
		if (a_watcher.epoch != a_currentEpoch) {
			a_watcher.Clear();
			return AdvanceDecision::Cancel;
		}
		if (a_now >= a_watcher.hardDeadline ||
			(a_watcher.phase == Phase::PostRestoreObservation &&
			 a_now >= a_watcher.observationDeadline)) {
			a_watcher.Clear();
			return AdvanceDecision::Timeout;
		}
		if (a_watcher.phase != Phase::PostRestoreObservation ||
			a_watcher.followupSamplesRemaining == 0) {
			return AdvanceDecision::None;
		}

		const bool changed = !a_watcher.hasLastFingerprint ||
			a_watcher.lastFingerprint != a_fingerprint;
		const bool cadenceDue = a_now >= a_watcher.nextPeriodicSampleAt;
		if (!changed && !cadenceDue) {
			return AdvanceDecision::None;
		}

		a_watcher.lastFingerprint = a_fingerprint;
		a_watcher.hasLastFingerprint = true;
		a_watcher.nextPeriodicSampleAt = a_now + kPeriodicSampleSeconds;
		--a_watcher.followupSamplesRemaining;
		return AdvanceDecision::EmitFollowup;
	}

	[[nodiscard]] constexpr AttackInputDisposition ObserveAttackInput() noexcept
	{
		return AttackInputDisposition::ObserveOnlyPassThroughUnchanged;
	}
}
