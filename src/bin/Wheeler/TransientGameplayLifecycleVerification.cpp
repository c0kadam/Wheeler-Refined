#include "RestorationLifecyclePolicy.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

namespace
{
	using namespace std::chrono_literals;

	bool Expect(bool a_condition, const char* a_name)
	{
		std::cout << (a_condition ? "PASS " : "FAIL ") << a_name << '\n';
		return a_condition;
	}

	struct LegacyRestorationOnlyState
	{
		bool restoration = true;
		bool poison = true;
		bool miscUse = true;
		bool bookRead = true;
		bool concentrationStop = true;
		bool syntheticDownOwned = true;
		bool delayedActionAuthorized = true;

		void LegacyRestorationOnlyReset()
		{
			restoration = false;
			// A restoration-only reset leaves the remaining intent state intact.
		}
	};
}

int RunTransientGameplayLifecycleVerification()
{
	using Kind = RestorationLifecycle::DeferredIntentKind;
	using Disposition = RestorationLifecycle::ResetDisposition;

	bool ok = true;
	RestorationLifecycle::SerializedEpochDomain domain;
	RestorationLifecycle::DeferredIntentLedger ledger;
	const auto gameAEpoch = domain.Current();

	for (std::uint8_t raw = 0; raw < static_cast<std::uint8_t>(Kind::kCount); ++raw) {
		ledger.Arm(static_cast<Kind>(raw), gameAEpoch);
	}
	const auto gameBEpoch = domain.Reset([&]() { ledger.ClearAll(); });
	ok &= Expect(gameBEpoch != gameAEpoch && ledger.Empty(), "world reset invalidates complete intent ledger");
	for (std::uint8_t raw = 0; raw < static_cast<std::uint8_t>(Kind::kCount); ++raw) {
		ok &= Expect(!ledger.IsAuthorized(static_cast<Kind>(raw), gameAEpoch),
			"Game-A deferred intent rejected in Game B");
	}

	std::atomic_int sameFormActionCount{ 0 };
	const bool sameFormExecuted = domain.ExecuteIfCurrent(gameAEpoch, [&]() { ++sameFormActionCount; });
	ok &= Expect(!sameFormExecuted && sameFormActionCount == 0,
		"Game-A action rejected even when Game-B FormID is identical");

	std::mutex gateMutex;
	std::condition_variable gateCv;
	bool actionEntered = false;
	bool allowActionFinish = false;
	std::atomic_bool resetFinished{ false };
	const auto serializedEpoch = domain.Current();
	std::thread actionThread([&]() {
		domain.ExecuteIfCurrent(serializedEpoch, [&]() {
			std::unique_lock gateLock(gateMutex);
			actionEntered = true;
			gateCv.notify_all();
			gateCv.wait(gateLock, [&]() { return allowActionFinish; });
		});
	});
	{
		std::unique_lock gateLock(gateMutex);
		gateCv.wait(gateLock, [&]() { return actionEntered; });
	}
	std::thread resetThread([&]() {
		domain.Reset([]() {});
		resetFinished.store(true, std::memory_order_release);
	});
	std::this_thread::sleep_for(50ms);
	ok &= Expect(!resetFinished.load(std::memory_order_acquire),
		"reset cannot interleave between authorization and action");
	{
		std::lock_guard gateLock(gateMutex);
		allowActionFinish = true;
	}
	gateCv.notify_all();
	actionThread.join();
	resetThread.join();
	ok &= Expect(resetFinished.load(std::memory_order_acquire),
		"reset is ordered after already-begun serialized action");
	std::atomic_bool staleActed{ false };
	ok &= Expect(!domain.ExecuteIfCurrent(serializedEpoch, [&]() { staleActed = true; }) && !staleActed,
		"old task cannot pass check then act after reset");

	const auto worldInputPlan = RestorationLifecycle::PlanSyntheticInputCancellation(
		Disposition::kWorldDiscard, true, true);
	const auto currentInputPlan = RestorationLifecycle::PlanSyntheticInputCancellation(
		Disposition::kCurrentWorldCancel, true, true);
	ok &= Expect(!worldInputPlan.releasePrimary && !worldInputPlan.releaseSecondary,
		"world disposal performs no outgoing-world synthetic input rollback");
	ok &= Expect(currentInputPlan.releasePrimary && currentInputPlan.releaseSecondary,
		"current-world cancel repays all Wheeler-owned synthetic downs");
	ok &= Expect(!RestorationLifecycle::ShouldRollbackOwnedCurrentWorldSideEffect(
		Disposition::kWorldDiscard, true), "world disposal performs no isolation/IWS rollback");
	ok &= Expect(RestorationLifecycle::ShouldRollbackOwnedCurrentWorldSideEffect(
		Disposition::kCurrentWorldCancel, true), "current-world cancel rolls back owned isolation/IWS state");

	const auto deadlineContext = RestorationLifecycle::MakePostCastContext(7, 10.0, 0.5, 2.0);
	const auto originalExpiry = deadlineContext.expiresAt;
	ok &= Expect(
		RestorationLifecycle::EvaluatePostCastContext(
			deadlineContext, 7, 11.0, RestorationLifecycle::PostCastOccupantState::kBlockingUnchanged) ==
			RestorationLifecycle::PostCastContextDecision::kWait &&
		deadlineContext.expiresAt == originalExpiry &&
		RestorationLifecycle::EvaluatePostCastContext(
			deadlineContext, 7, originalExpiry, RestorationLifecycle::PostCastOccupantState::kClear) ==
			RestorationLifecycle::PostCastContextDecision::kExpired,
		"post-cast absolute deadline remains immutable and non-renewable");

	LegacyRestorationOnlyState legacy;
	legacy.LegacyRestorationOnlyReset();
	const bool legacyBaselineFails =
		legacy.poison && legacy.miscUse && legacy.bookRead && legacy.concentrationStop &&
		legacy.syntheticDownOwned && legacy.delayedActionAuthorized;
	ok &= Expect(legacyBaselineFails,
		"legacy restoration-only reset leaves poison/misc/book/concentration, synthetic DOWN, and delayed authorization live");

	return ok ? 0 : 1;
}

#ifdef WHEELER_TRANSIENT_LIFECYCLE_VERIFICATION_MAIN
int main()
{
	return RunTransientGameplayLifecycleVerification();
}
#endif
