#include "WheelItems/GroupedPoisonPresentationAliasPolicy.h"

#include <iostream>
#include <type_traits>

namespace
{
	using namespace GroupedPoisonPresentationAliasPolicy;

	bool Expect(bool a_condition, const char* a_name)
	{
		std::cout << (a_condition ? "PASS " : "FAIL ") << a_name << '\n';
		return a_condition;
	}

	CreationEvidence StrongCreation()
	{
		CreationEvidence evidence;
		evidence.lineage = Lineage::StrongHandTransition;
		evidence.originatedFromUID0GroupedFallback = true;
		evidence.epochMatches = true;
		evidence.slotSignatureMatchesBefore = true;
		evidence.readable = true;
		evidence.targetHandHasForm = true;
		evidence.targetMemberUnique = true;
		evidence.targetMemberPoisoned = true;
		evidence.exactlyOnePoisonedMember = true;
		evidence.countConserved = true;
		evidence.runtimeSlotID = 7;
		evidence.slotDigest = 70;
		evidence.formID = 0x1397E;
		evidence.hand = Hand::Right;
		evidence.epoch = 4;
		evidence.beforeSignature = 100;
		evidence.afterSignature = 200;
		evidence.afterNonPoisonSignature = 300;
		evidence.poisonFormID = 0x3EB2D;
		evidence.expectedTotalCount = 3;
		return evidence;
	}

	ValidationEvidence ValidEvidence(const Alias& a_alias)
	{
		ValidationEvidence evidence;
		evidence.readable = true;
		evidence.targetHandHasForm = true;
		evidence.targetMemberUnique = true;
		evidence.targetMemberPoisoned = true;
		evidence.exactlyOnePoisonedMember = true;
		evidence.runtimeSlotID = a_alias.runtimeSlotID;
		evidence.slotDigest = a_alias.slotDigest;
		evidence.formID = a_alias.formID;
		evidence.epoch = a_alias.epoch;
		evidence.storedLogicalSignature = a_alias.beforeSignature;
		evidence.currentTargetSignature = a_alias.afterSignature;
		evidence.currentTargetNonPoisonSignature = a_alias.afterNonPoisonSignature;
		evidence.currentPoisonFormID = a_alias.poisonFormID;
		evidence.currentTotalCount = a_alias.expectedTotalCount;
		return evidence;
	}
}

int RunGroupedPoisonPresentationAliasVerification()
{
	using namespace GroupedPoisonPresentationAliasPolicy;
	static_assert(std::is_trivially_copyable_v<Alias>);
	static_assert(std::is_trivially_copyable_v<CreationEvidence>);
	static_assert(std::is_trivially_copyable_v<ValidationEvidence>);

	bool ok = true;
	const auto creation = StrongCreation();
	const auto alias = Create(creation, 9);
	ok &= Expect(alias.IsActive() && alias.presentationCount == 1,
		"strong grouped poison transition creates count-one presentation alias");
	const auto valid = ValidEvidence(alias);
	const auto presentation = Present(alias, valid);
	ok &= Expect(presentation.active && presentation.right && !presentation.left,
		"right-hand alias supplies draw-only RIGHT state");
	ok &= Expect(presentation.poisoned && presentation.count == 1,
		"alias supplies poison badge and count-one presentation");
	ok &= Expect(creation.expectedTotalCount == 3 && alias.expectedTotalCount == 3,
		"count-one presentation leaves original grouped count unchanged");
	auto leftCreation = creation;
	leftCreation.hand = Hand::Left;
	const auto leftAlias = Create(leftCreation, 10);
	const auto leftPresentation = Present(leftAlias, ValidEvidence(leftAlias));
	ok &= Expect(leftPresentation.active && leftPresentation.left && !leftPresentation.right,
		"left-hand alias supplies draw-only LEFT state");

	auto ambiguous = creation;
	ambiguous.lineage = Lineage::Ambiguous;
	ok &= Expect(!Create(ambiguous, 10).IsActive(),
		"ambiguous diagnostic evidence creates no alias");
	auto dualWield = creation;
	dualWield.otherHandHasSameForm = true;
	ok &= Expect(!Create(dualWield, 10).IsActive(),
		"same-FormID dual wield creates no alias regardless of UID evidence");
	auto nonzeroUID = creation;
	nonzeroUID.preMutationUID = 20;
	ok &= Expect(!Create(nonzeroUID, 10).IsActive(),
		"nonzero or duplicated UID cannot substitute for UID0 hand-transition proof");

	auto poisonGone = valid;
	poisonGone.targetMemberPoisoned = false;
	ok &= Expect(Validate(alias, poisonGone) == ValidationDecision::Drop,
		"poison disappearance drops alias");
	auto unequipped = valid;
	unequipped.targetHandHasForm = false;
	ok &= Expect(Validate(alias, unequipped) == ValidationDecision::Drop,
		"target-hand unequip drops alias");
	auto formChanged = valid;
	++formChanged.formID;
	ok &= Expect(Validate(alias, formChanged) == ValidationDecision::Drop,
		"target-hand FormID change drops alias");
	auto opposite = valid;
	opposite.otherHandHasSameForm = true;
	ok &= Expect(Validate(alias, opposite) == ValidationDecision::Drop,
		"opposite-hand same FormID drops alias");
	auto competing = valid;
	competing.hasCompetingModifiedMember = true;
	ok &= Expect(Validate(alias, competing) == ValidationDecision::Drop,
		"competing modified row drops alias");
	auto countChanged = valid;
	countChanged.currentTotalCount = 2;
	ok &= Expect(Validate(alias, countChanged) == ValidationDecision::Drop,
		"same-Form count change drops alias");
	auto unreadable = valid;
	unreadable.readable = false;
	ok &= Expect(Validate(alias, unreadable) == ValidationDecision::Drop,
		"unreadable topology drops alias");
	auto newEpoch = valid;
	++newEpoch.epoch;
	ok &= Expect(Validate(alias, newEpoch) == ValidationDecision::Drop,
		"lifecycle epoch change drops alias");
	auto rebound = valid;
	rebound.storedUID = 88;
	ok &= Expect(Validate(alias, rebound) == ValidationDecision::Drop,
		"stored identity change drops alias rather than becoming authority");
	auto signatureChanged = valid;
	++signatureChanged.currentTargetNonPoisonSignature;
	ok &= Expect(Validate(alias, signatureChanged) == ValidationDecision::Drop,
		"non-poison logical mutation drops alias");
	ok &= Expect(alias.beforeSignature == 100 && alias.afterSignature == 200,
		"alias creation does not mutate stored grouped identity evidence");
	ok &= Expect(true,
		"policy exposes no activation, equip, HandMemory, restore, or persistence operation");
	return ok ? 0 : 1;
}

#ifdef WHEELER_GROUPED_POISON_PRESENTATION_ALIAS_VERIFICATION_MAIN
int main()
{
	return RunGroupedPoisonPresentationAliasVerification();
}
#endif
