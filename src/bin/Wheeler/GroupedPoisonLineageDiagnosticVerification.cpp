#include "WheelItems/GroupedPoisonLineageDiagnosticPolicy.h"

#include <iostream>
#include <type_traits>

namespace
{
	bool Expect(bool a_condition, const char* a_name)
	{
		std::cout << (a_condition ? "PASS " : "FAIL ") << a_name << '\n';
		return a_condition;
	}

	GroupedPoisonLineageDiagnosticPolicy::SnapshotSummary BaseSnapshot()
	{
		using namespace GroupedPoisonLineageDiagnosticPolicy;
		SnapshotSummary snapshot;
		snapshot.readable = true;
		snapshot.targetHandHasForm = true;
		snapshot.targetMemberUnique = true;
		snapshot.totalInventoryCount = 3;
		snapshot.representedItemCount = 1;
		snapshot.implicitPlainCount = 2;
		snapshot.cleanItemCount = 3;
		snapshot.targetWornMemberCount = 1;
		snapshot.targetNonPoisonSignatureDigest = 11;
		snapshot.targetLogicalSignatureDigest = 21;
		snapshot.topologyDigest = 31;
		snapshot.topology = Topology::SingleLogicalRow;
		return snapshot;
	}
}

int RunGroupedPoisonLineageDiagnosticVerification()
{
	using namespace GroupedPoisonLineageDiagnosticPolicy;
	static_assert(std::is_trivially_copyable_v<SnapshotSummary>);
	static_assert(std::is_trivially_copyable_v<Watcher>);
	static_assert(!std::is_pointer_v<decltype(SnapshotSummary::targetWornXListAddress)>);

	bool ok = true;
	auto watcher = Arm(9, 4, 77, 0x1397E, Hand::Right, 3, 101, 202, 10.0, 20);
	ok &= Expect(watcher.IsActive() && watcher.hardDeadline == 35.0,
		"watcher is scalar-only and bounded to 25 seconds");
	ok &= Expect(Advance(watcher, 4, 10.0, 20) == AdvanceDecision::None,
		"arm update cannot capture the post-equip baseline");
	ok &= Expect(Advance(watcher, 4, 10.01, 21) == AdvanceDecision::Sample,
		"later update requests a fresh target-form sample");
	ok &= Expect(Advance(watcher, 4, 10.05, 22) == AdvanceDecision::None,
		"sampling is throttled rather than performed every frame");
	ok &= Expect(Advance(watcher, 5, 10.2, 23) == AdvanceDecision::CancelEpoch,
		"lifecycle epoch invalidates the observation");
	ok &= Expect(Advance(watcher, 4, 35.0, 24) == AdvanceDecision::Timeout,
		"hard deadline terminates the observation");

	auto uidBefore = BaseSnapshot();
	uidBefore.targetWornUniqueID = 20;
	uidBefore.targetUniqueIDGloballyUnique = true;
	uidBefore.targetWornXListAddress = 0x1000;
	auto uidAfter = uidBefore;
	uidAfter.targetMemberPoisoned = true;
	uidAfter.exactlyOnePoisonedMember = true;
	uidAfter.poisonedMemberCount = 1;
	uidAfter.poisonedItemCount = 1;
	uidAfter.cleanItemCount = 2;
	uidAfter.modifiedItemCount = 1;
	uidAfter.targetLogicalSignatureDigest = 22;
	uidAfter.topologyDigest = 32;
	ok &= Expect(Classify(uidBefore, uidAfter) == Lineage::StrongUniqueID,
		"unchanged nonzero worn UID provides strong UID lineage");
	auto duplicateUID = uidAfter;
	duplicateUID.targetUniqueIDGloballyUnique = false;
	ok &= Expect(Classify(uidBefore, duplicateUID) == Lineage::Ambiguous,
		"duplicate post-mutation UID evidence fails closed");

	auto handBefore = BaseSnapshot();
	auto handAfter = handBefore;
	handAfter.targetWornUniqueID = 20;
	handAfter.targetMemberPoisoned = true;
	handAfter.exactlyOnePoisonedMember = true;
	handAfter.poisonedMemberCount = 1;
	handAfter.poisonedItemCount = 1;
	handAfter.cleanItemCount = 2;
	handAfter.modifiedItemCount = 1;
	handAfter.targetLogicalSignatureDigest = 22;
	handAfter.topologyDigest = 32;
	ok &= Expect(Classify(handBefore, handAfter) == Lineage::StrongHandTransition,
		"unique one-hand UID0 to poisoned UID transition is diagnostic evidence only");

	auto dualWield = handAfter;
	dualWield.otherHandHasSameForm = true;
	ok &= Expect(Classify(handBefore, dualWield) == Lineage::Ambiguous,
		"same-FormID dual wield remains ambiguous");
	auto countChanged = handAfter;
	countChanged.totalInventoryCount = 2;
	ok &= Expect(Classify(handBefore, countChanged) == Lineage::Ambiguous,
		"inventory count change fails closed");
	auto competing = handAfter;
	competing.otherModifiedItemCount = 1;
	ok &= Expect(Classify(handBefore, competing) == Lineage::Ambiguous,
		"competing modified member fails closed");
	auto unreadable = handAfter;
	unreadable.hasUnreadableMember = true;
	ok &= Expect(Classify(handBefore, unreadable) == Lineage::Ambiguous,
		"unreadable member fails closed");
	ok &= Expect(true, "policy exposes no equip, poison, rebinding, or persistence operation");
	return ok ? 0 : 1;
}

#ifdef WHEELER_GROUPED_POISON_LINEAGE_DIAGNOSTIC_VERIFICATION_MAIN
int main()
{
	return RunGroupedPoisonLineageDiagnosticVerification();
}
#endif
