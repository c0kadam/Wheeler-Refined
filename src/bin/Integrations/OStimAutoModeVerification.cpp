#include "OStimAutoModePolicy.h"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{
	bool Expect(bool a_condition, std::string_view a_case)
	{
		if (a_condition) {
			std::cout << "PASS " << a_case << '\n';
			return true;
		}
		std::cerr << "FAIL " << a_case << '\n';
		return false;
	}
}

int main()
{
	bool ok = true;

	const auto off = BuildOStimAutoModeDecision(true, true, false);
	std::uint32_t requestedThread = 99;
	bool requestedValue = false;
	int setterCalls = 0;
	const bool offResult = DispatchOStimAutoMode(
		off,
		0,
		[&](std::uint32_t a_threadID, bool a_autoMode) {
			++setterCalls;
			requestedThread = a_threadID;
			requestedValue = a_autoMode;
			return true;
		});
	ok &= Expect(
		std::string_view(GetOStimAutoModeLabel(false)) == "Auto Progress: OFF" &&
		off.canDispatch && off.desiredAutoMode && offResult &&
		setterCalls == 1 && requestedThread == 0 && requestedValue,
		"OFF presentation requests true and accepts thread ID zero");

	const auto on = BuildOStimAutoModeDecision(true, true, true);
	requestedValue = true;
	setterCalls = 0;
	const bool onResult = DispatchOStimAutoMode(
		on,
		17,
		[&](std::uint32_t, bool a_autoMode) {
			++setterCalls;
			requestedValue = a_autoMode;
			return true;
		});
	ok &= Expect(
		std::string_view(GetOStimAutoModeLabel(true)) == "Auto Progress: ON" &&
		on.canDispatch && !on.desiredAutoMode && onResult &&
		setterCalls == 1 && !requestedValue,
		"ON presentation requests false");

	setterCalls = 0;
	const auto inactive = BuildOStimAutoModeDecision(false, true, false);
	const bool inactiveResult = DispatchOStimAutoMode(
		inactive,
		0,
		[&](std::uint32_t, bool) {
			++setterCalls;
			return true;
		});
	ok &= Expect(
		!inactive.canDispatch && !inactiveResult && setterCalls == 0 &&
		std::string_view(inactive.rejectionReason) == "SceneInactive",
		"inactive scene rejects without dispatch");

	setterCalls = 0;
	const auto unavailable = BuildOStimAutoModeDecision(true, false, false);
	const bool unavailableResult = DispatchOStimAutoMode(
		unavailable,
		0,
		[&](std::uint32_t, bool) {
			++setterCalls;
			return true;
		});
	ok &= Expect(
		!unavailable.canDispatch && !unavailableResult && setterCalls == 0 &&
		std::string_view(unavailable.rejectionReason) == "SceneAPIUnavailable",
		"missing Scene API rejects without fallback dispatch");

	bool authoritativeSnapshotAutoMode = false;
	const auto failed = BuildOStimAutoModeDecision(true, true, authoritativeSnapshotAutoMode);
	const bool failedResult = DispatchOStimAutoMode(
		failed,
		0,
		[](std::uint32_t, bool) { return false; });
	ok &= Expect(
		!failedResult && !authoritativeSnapshotAutoMode &&
		std::string_view(GetOStimAutoModeLabel(authoritativeSnapshotAutoMode)) == "Auto Progress: OFF",
		"failed dispatch does not optimistically flip snapshot state");

	const auto accepted = BuildOStimAutoModeDecision(true, true, authoritativeSnapshotAutoMode);
	const bool acceptedResult = DispatchOStimAutoMode(
		accepted,
		0,
		[](std::uint32_t, bool) { return true; });
	ok &= Expect(
		acceptedResult && !authoritativeSnapshotAutoMode &&
		std::string_view(GetOStimAutoModeLabel(authoritativeSnapshotAutoMode)) == "Auto Progress: OFF",
		"successful dispatch leaves tracker snapshot authoritative");

	std::cout << (ok ? "OStim Auto Mode verification PASSED\n" :
		"OStim Auto Mode verification FAILED\n");
	return ok ? 0 : 1;
}
