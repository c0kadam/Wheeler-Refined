#include "WheelItems/LegacyWeaponRestorePolicy.h"

#include <iostream>

namespace
{
	bool Expect(bool a_condition, const char* a_name)
	{
		std::cout << (a_condition ? "PASS " : "FAIL ") << a_name << '\n';
		return a_condition;
	}

	LegacyWeaponRestorePolicy::DualHandCaptureEvidence MakeCaptureEvidence()
	{
		using namespace LegacyWeaponRestorePolicy;
		return {
			RowKind::Physical,
			RowKind::Physical,
			0x1397E,
			0x1397E,
			5,
			5,
			true,
			true,
			true
		};
	}

	LegacyWeaponRestorePolicy::GroupProofEvidence MakeEquivalentGroup(
		int a_sameFormCount,
		int a_eligibleLogicalCount)
	{
		LegacyWeaponRestorePolicy::GroupProofEvidence evidence;
		evidence.sameFormCount = a_sameFormCount;
		evidence.representedCount = a_sameFormCount;
		evidence.distinctLogicalRows = 1;
		evidence.implicitMembersCompatible = true;
		evidence.wornWeaponAttributable = true;
		evidence.eligibleLogicalCount = a_eligibleLogicalCount;
		return evidence;
	}
}

int RunHandMemorySameFormRestoreVerification()
{
	using namespace LegacyWeaponRestorePolicy;
	bool ok = true;

	auto differentForms = MakeCaptureEvidence();
	differentForms.rightFormID = 0x13790;
	ok &= Expect(
		DecideDualHandCapture(differentForms) == DualHandCaptureDecision::PreserveExactAuthority,
		"different FormIDs preserve independent exact restore authority");

	auto collidingLineage = MakeCaptureEvidence();
	ok &= Expect(
		DecideDualHandCapture(collidingLineage) ==
			DualHandCaptureDecision::PromoteBothToGroupEquivalent,
		"same-form same-lineage obligations become one proven group with multiplicity two");

	auto distinctExact = MakeCaptureEvidence();
	distinctExact.rightUniqueID = 6;
	ok &= Expect(
		DecideDualHandCapture(distinctExact) ==
			DualHandCaptureDecision::PreserveExactAuthority,
		"distinguishable per-hand exact UIDs remain exact and distinct");

	auto mixedExactAndGroup = MakeCaptureEvidence();
	mixedExactAndGroup.rightRowKind = RowKind::GroupEquivalent;
	mixedExactAndGroup.rightUniqueID = 0;
	ok &= Expect(
		DecideDualHandCapture(mixedExactAndGroup) ==
			DualHandCaptureDecision::PromoteBothToGroupEquivalent,
		"mixed exact/group same-row obligations normalize to shared group authority");

	auto unreadableCollision = MakeCaptureEvidence();
	unreadableCollision.groupEquivalentProven = false;
	ok &= Expect(
		DecideDualHandCapture(unreadableCollision) ==
			DualHandCaptureDecision::RejectIndistinguishableCollision,
		"indistinguishable collision without full group proof fails closed");

	auto nonDistinctCollision = MakeCaptureEvidence();
	nonDistinctCollision.distinctWornMembersProven = false;
	ok &= Expect(
		DecideDualHandCapture(nonDistinctCollision) ==
			DualHandCaptureDecision::RejectIndistinguishableCollision,
		"one physical member cannot satisfy both captured hand obligations");

	const auto twoCopiesBeforeRestore = MakeEquivalentGroup(2, 2);
	ok &= Expect(
		AllowsGroupEquivalentFormLevel(twoCopiesBeforeRestore, true),
		"two equivalent copies authorize a hand-neutral group restore");

	const auto oneRemainingAfterRight = MakeEquivalentGroup(2, 1);
	ok &= Expect(
		AllowsGroupEquivalentFormLevel(oneRemainingAfterRight, true),
		"RIGHT reservation leaves one proven logical member eligible for LEFT");

	const auto countDroppedToOne = MakeEquivalentGroup(1, 0);
	ok &= Expect(
		!AllowsGroupEquivalentFormLevel(countDroppedToOne, true),
		"same-form count dropping to one cannot duplicate or reuse the RIGHT member");

	auto conflictingRows = MakeEquivalentGroup(2, 1);
	conflictingRows.distinctLogicalRows = 2;
	ok &= Expect(
		!AllowsGroupEquivalentFormLevel(conflictingRows, true),
		"conflicting logical rows fail closed");

	const FailedRestoreRetryEvidence firstSafeFailure{
		true, true, true, true, true, true, 0
	};
	ok &= Expect(
		ShouldRetryFailedRestore(firstSafeFailure),
		"first safe failure retains the value token for one fresh retry");

	auto retryExhausted = firstSafeFailure;
	retryExhausted.retriesUsed = 1;
	ok &= Expect(
		!ShouldRetryFailedRestore(retryExhausted),
		"retry bound permits no second retry");

	auto explicitOccupant = firstSafeFailure;
	explicitOccupant.currentHandEmpty = false;
	ok &= Expect(
		!ShouldRetryFailedRestore(explicitOccupant),
		"explicit current occupant blocks retry overwrite");

	auto staleEpoch = firstSafeFailure;
	staleEpoch.epochMatches = false;
	ok &= Expect(
		!ShouldRetryFailedRestore(staleEpoch),
		"epoch invalidation cancels retry authority");

	auto supersededIntent = firstSafeFailure;
	supersededIntent.gameplayIntentCurrent = false;
	ok &= Expect(
		!ShouldRetryFailedRestore(supersededIntent),
		"new gameplay intent cancels retry authority");

	return ok ? 0 : 1;
}

#ifdef WHEELER_HANDMEMORY_SAME_FORM_RESTORE_VERIFICATION_MAIN
int main()
{
	return RunHandMemorySameFormRestoreVerification();
}
#endif
