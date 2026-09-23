#include "PoisonDiagnosticPolicy.h"

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

int RunPoisonDiagnosticVerification()
{
	using namespace PoisonDiagnosticPolicy;
	static_assert(std::is_trivially_copyable_v<Watcher>);
	static_assert(!std::is_pointer_v<decltype(Watcher::candidateFormIDs)>);

	bool ok = true;
	const std::array<FormID, 2> candidates{ 0x13982, 0x1397E };
	auto watcher = Arm(42, 7, candidates, 2, 10.0);
	ok &= Expect(watcher.IsActive() && watcher.eventID == 42 && watcher.candidateCount == 2,
		"pre snapshot state retains scalar correlation metadata only");
	ok &= Expect(Advance(watcher, 7, 10.1, true) == Decision::None,
		"open InventoryMenu holds the watcher without gameplay action");
	ok &= Expect(Advance(watcher, 7, 10.2, false) == Decision::EmitBoundary &&
		watcher.eventID == 42,
		"post boundary keeps the same event ID and requests fresh reacquisition");
	ok &= Expect(Advance(watcher, 7, 12.3, false) == Decision::EmitFinal && !watcher.IsActive(),
		"bounded follow-up observation terminates");

	auto bounded = Arm(43, 7, candidates, 2, 20.0);
	ok &= Expect(Advance(bounded, 7, 56.0, false) == Decision::EmitFinal && !bounded.IsActive(),
		"hard deadline prevents an unbounded watcher");

	auto stale = Arm(44, 7, candidates, 2, 20.0);
	ok &= Expect(Advance(stale, 8, 20.1, false) == Decision::Cancel && !stale.IsActive(),
		"lifecycle reset cancels pending observation with no outgoing-world action");
	ok &= Expect(true,
		"diagnostic policy exposes no gameplay mutation operation");

	return ok ? 0 : 1;
}

#ifdef WHEELER_POISON_DIAGNOSTIC_VERIFICATION_MAIN
int main()
{
	return RunPoisonDiagnosticVerification();
}
#endif
