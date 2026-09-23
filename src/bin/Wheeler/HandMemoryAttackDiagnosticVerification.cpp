#include "HandMemoryAttackDiagnosticPolicy.h"

#include <iostream>
#include <type_traits>

namespace
{
	bool Expect(bool a_condition, const char* a_name)
	{
		std::cout << (a_condition ? "PASS " : "FAIL ") << a_name << '\n';
		return a_condition;
	}
}

int RunHandMemoryAttackDiagnosticVerification()
{
	using namespace HandMemoryAttackDiagnosticPolicy;
	static_assert(std::is_trivially_copyable_v<Watcher>);
	static_assert(std::is_trivially_copyable_v<Fingerprint>);
	static_assert(!std::is_pointer_v<decltype(Watcher::expectedLeft)>);
	static_assert(!std::is_pointer_v<decltype(Watcher::expectedRight)>);
	static_assert(kPostRestoreObservationSeconds > 0.0 && kPostRestoreObservationSeconds <= 5.0);
	static_assert(kOverallHardLimitSeconds > kPostRestoreObservationSeconds && kOverallHardLimitSeconds <= 6.0);
	static_assert(kPeriodicSampleSeconds == 0.5);
	static_assert(kMaximumFollowupSamples > 0 && kMaximumFollowupSamples <= 40);
	static_assert(ObserveAttackInput() == AttackInputDisposition::ObserveOnlyPassThroughUnchanged);

	bool ok = true;
	auto watcher = Arm(42, 7, 0x13790, 0x13982, 10.0);
	ok &= Expect(watcher.IsActive() && watcher.phase == Phase::Restoring,
		"restore diagnostics arm without changing restore sequencing");
	ok &= Expect(watcher.expectedLeft == 0x13790 && watcher.expectedRight == 0x13982,
		"diagnostic transaction retains scalar expected FormIDs only");

	Fingerprint initial{};
	ok &= Expect(Advance(watcher, 7, 10.1, initial) == AdvanceDecision::None,
		"restoring phase remains observation-only");
	BeginPostRestoreObservation(watcher, 10.2);
	ok &= Expect(watcher.phase == Phase::PostRestoreObservation &&
		watcher.observationDeadline == 15.2,
		"post-restore observation deadline is bounded to five seconds");
	ok &= Expect(Advance(watcher, 7, 10.2, initial) == AdvanceDecision::EmitFollowup,
		"first post-restore state requests a diagnostic sample");
	ok &= Expect(Advance(watcher, 7, 10.25, initial) == AdvanceDecision::None,
		"unchanged state is cadence-limited");
	ok &= Expect(Advance(watcher, 7, 10.7, initial) == AdvanceDecision::EmitFollowup,
		"unchanged state emits a bounded low-frequency heartbeat");
	ok &= Expect(Advance(watcher, 7, 15.2, initial) == AdvanceDecision::Timeout &&
		!watcher.IsActive(),
		"observation terminates at its hard post-restore deadline");

	auto hardBounded = Arm(44, 7, 0, 0, 30.0);
	ok &= Expect(Advance(hardBounded, 7, 36.0, initial) == AdvanceDecision::Timeout &&
		!hardBounded.IsActive(),
		"overall watcher terminates at its six-second hard deadline");

	auto stale = Arm(43, 7, 0, 0, 20.0);
	ok &= Expect(Advance(stale, 8, 20.1, initial) == AdvanceDecision::Cancel &&
		!stale.IsActive(),
		"epoch change cancels old-world diagnostic state");
	ok &= Expect(ObserveAttackInput() == AttackInputDisposition::ObserveOnlyPassThroughUnchanged,
		"attack input policy is observation-only and cannot consume or replay input");

	return ok ? 0 : 1;
}

#ifdef WHEELER_HANDMEMORY_ATTACK_DIAGNOSTIC_VERIFICATION_MAIN
int main()
{
	return RunHandMemoryAttackDiagnosticVerification();
}
#endif
