#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>

namespace RestorationLifecycle
{
	using Epoch = std::uint64_t;

	enum class ResetDisposition : std::uint8_t
	{
		kWorldDiscard,
		kCurrentWorldCancel
	};

	enum class DeferredIntentKind : std::uint8_t
	{
		kPendingPoison,
		kPendingMiscUse,
		kPendingBookRead,
		kConcentrationStop,
		kDepletedConsumableCleanup,
		kInstantCastRefund,
		kExternalHotkey,
		kPowerTask,
		kCount
	};

	class DeferredIntentLedger
	{
	public:
		void Arm(DeferredIntentKind a_kind, Epoch a_epoch) noexcept
		{
			_epochs[Index(a_kind)] = a_epoch;
		}

		void Disarm(DeferredIntentKind a_kind) noexcept
		{
			_epochs[Index(a_kind)] = 0;
		}

		[[nodiscard]] bool IsAuthorized(DeferredIntentKind a_kind, Epoch a_epoch) const noexcept
		{
			return a_epoch != 0 && _epochs[Index(a_kind)] == a_epoch;
		}

		void ClearAll() noexcept
		{
			_epochs.fill(0);
		}

		[[nodiscard]] bool Empty() const noexcept
		{
			for (const auto epoch : _epochs) {
				if (epoch != 0) {
					return false;
				}
			}
			return true;
		}

	private:
		[[nodiscard]] static constexpr std::size_t Index(DeferredIntentKind a_kind) noexcept
		{
			return static_cast<std::size_t>(a_kind);
		}

		std::array<Epoch, static_cast<std::size_t>(DeferredIntentKind::kCount)> _epochs{};
	};

	struct SyntheticInputCancellationPlan
	{
		bool releasePrimary = false;
		bool releaseSecondary = false;
	};

	[[nodiscard]] constexpr SyntheticInputCancellationPlan PlanSyntheticInputCancellation(
		ResetDisposition a_disposition,
		bool a_primaryDownOwned,
		bool a_secondaryDownOwned) noexcept
	{
		if (a_disposition == ResetDisposition::kWorldDiscard) {
			return {};
		}
		return { a_primaryDownOwned, a_secondaryDownOwned };
	}

	[[nodiscard]] constexpr bool ShouldRollbackOwnedCurrentWorldSideEffect(
		ResetDisposition a_disposition,
		bool a_ownedSideEffectActive) noexcept
	{
		return a_disposition == ResetDisposition::kCurrentWorldCancel && a_ownedSideEffectActive;
	}

	enum class PostCastOccupantState : std::uint8_t
	{
		kClear,
		kBlockingUnchanged,
		kChangedOrUnreadable
	};

	enum class PostCastContextDecision : std::uint8_t
	{
		kNotReady,
		kRestoreAllowed,
		kWait,
		kCancel,
		kExpired,
		kStaleEpoch
	};

	// One value-owned authorization window for the complete post-cast restore
	// context. Occupant waiting never changes or renews expiresAt.
	struct PostCastContext
	{
		Epoch epoch = 0;
		double noEarlierThan = 0.0;
		double expiresAt = 0.0;

		[[nodiscard]] constexpr bool IsArmed() const noexcept
		{
			return epoch != 0 && expiresAt > noEarlierThan;
		}
	};

	[[nodiscard]] constexpr PostCastContext MakePostCastContext(
		Epoch a_epoch,
		double a_now,
		double a_minDelaySeconds,
		double a_maxWaitSeconds) noexcept
	{
		const double delay = (std::max)(0.0, a_minDelaySeconds);
		const double wait = (std::max)(0.30, a_maxWaitSeconds);
		const double noEarlierThan = a_now + delay;
		return { a_epoch, noEarlierThan, noEarlierThan + wait };
	}

	[[nodiscard]] constexpr PostCastContextDecision EvaluatePostCastContext(
		const PostCastContext& a_context,
		Epoch a_currentEpoch,
		double a_now,
		PostCastOccupantState a_occupant) noexcept
	{
		if (!a_context.IsArmed() || a_context.epoch != a_currentEpoch) {
			return PostCastContextDecision::kStaleEpoch;
		}
		if (a_now >= a_context.expiresAt) {
			return PostCastContextDecision::kExpired;
		}
		if (a_now < a_context.noEarlierThan) {
			return PostCastContextDecision::kNotReady;
		}
		switch (a_occupant) {
		case PostCastOccupantState::kBlockingUnchanged:
			return PostCastContextDecision::kWait;
		case PostCastOccupantState::kChangedOrUnreadable:
			return PostCastContextDecision::kCancel;
		case PostCastOccupantState::kClear:
		default:
			return PostCastContextDecision::kRestoreAllowed;
		}
	}

	[[nodiscard]] constexpr bool IsOrdinaryHandMemoryWindowExpired(
		double a_now,
		double a_armedAt,
		double a_windowSeconds) noexcept
	{
		return a_now - a_armedAt > (std::max)(0.0, a_windowSeconds);
	}

	[[nodiscard]] inline Epoch CurrentEpoch(const std::atomic<Epoch>& a_epoch) noexcept
	{
		return a_epoch.load(std::memory_order_acquire);
	}

	[[nodiscard]] inline bool IsCurrentEpoch(
		const std::atomic<Epoch>& a_epoch,
		Epoch a_capturedEpoch) noexcept
	{
		return a_capturedEpoch != 0 && CurrentEpoch(a_epoch) == a_capturedEpoch;
	}

	// Production and the focused verifier share this exact reset primitive. The
	// epoch advances before any state is cleared, so already-queued work becomes
	// stale before it can observe a partially reset pipeline.
	template <class... Resetters>
	[[nodiscard]] Epoch ApplyTransientRestorationReset(
		std::atomic<Epoch>& a_epoch,
		Resetters&&... a_resetters)
	{
		static_assert(sizeof...(Resetters) > 0);
		Epoch next = a_epoch.fetch_add(1, std::memory_order_acq_rel) + 1;
		if (next == 0) {
			next = 1;
			a_epoch.store(next, std::memory_order_release);
		}
		(std::invoke(std::forward<Resetters>(a_resetters)), ...);
		return next;
	}

	// One serialized authorization domain for all Wheeler-owned deferred gameplay
	// intent. The lock establishes a total order between gameplay actions and
	// lifecycle invalidation without moving either action to a different thread.
	class SerializedEpochDomain
	{
	public:
		using Mutex = std::recursive_mutex;
		using Lock = std::unique_lock<Mutex>;

		[[nodiscard]] Lock Acquire() const
		{
			return Lock(_mutex);
		}

		[[nodiscard]] Epoch Current() const noexcept
		{
			return CurrentEpoch(_epoch);
		}

		[[nodiscard]] bool IsCurrent(Epoch a_epoch) const noexcept
		{
			return IsCurrentEpoch(_epoch, a_epoch);
		}

		template <class Action>
		bool ExecuteIfCurrent(Epoch a_epoch, Action&& a_action) const
		{
			Lock lock(_mutex);
			if (!IsCurrentEpoch(_epoch, a_epoch)) {
				return false;
			}
			std::invoke(std::forward<Action>(a_action));
			return true;
		}

		template <class... Resetters>
		[[nodiscard]] Epoch Reset(Resetters&&... a_resetters)
		{
			Lock lock(_mutex);
			return ApplyTransientRestorationReset(
				_epoch,
				std::forward<Resetters>(a_resetters)...);
		}

	private:
		mutable Mutex _mutex;
		std::atomic<Epoch> _epoch{ 1 };
	};
}
