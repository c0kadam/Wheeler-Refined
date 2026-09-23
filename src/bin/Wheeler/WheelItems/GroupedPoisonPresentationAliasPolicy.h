#pragma once

#include "GroupedPoisonLineageDiagnosticPolicy.h"

#include <cstdint>

namespace GroupedPoisonPresentationAliasPolicy
{
	using GroupedPoisonLineageDiagnosticPolicy::Epoch;
	using GroupedPoisonLineageDiagnosticPolicy::FormID;
	using GroupedPoisonLineageDiagnosticPolicy::Hand;
	using GroupedPoisonLineageDiagnosticPolicy::Lineage;

	struct CreationEvidence
	{
		Lineage lineage = Lineage::Ambiguous;
		bool originatedFromUID0GroupedFallback = false;
		bool epochMatches = false;
		bool slotSignatureMatchesBefore = false;
		bool readable = false;
		bool targetHandHasForm = false;
		bool otherHandHasSameForm = false;
		bool targetMemberUnique = false;
		bool targetMemberPoisoned = false;
		bool exactlyOnePoisonedMember = false;
		bool countConserved = false;
		bool hasCompetingModifiedMember = false;
		std::uint16_t preMutationUID = 0;
		std::uint64_t runtimeSlotID = 0;
		std::uint64_t slotDigest = 0;
		FormID formID = 0;
		Hand hand = Hand::Right;
		Epoch epoch = 0;
		std::uint64_t beforeSignature = 0;
		std::uint64_t afterSignature = 0;
		std::uint64_t afterNonPoisonSignature = 0;
		FormID poisonFormID = 0;
		int expectedTotalCount = 0;
	};

	struct Alias
	{
		bool active = false;
		bool validLogged = false;
		std::uint64_t runtimeSlotID = 0;
		std::uint64_t slotDigest = 0;
		FormID formID = 0;
		Hand hand = Hand::Right;
		Epoch epoch = 0;
		std::uint64_t generation = 0;
		std::uint64_t beforeSignature = 0;
		std::uint64_t afterSignature = 0;
		std::uint64_t afterNonPoisonSignature = 0;
		FormID poisonFormID = 0;
		int expectedTotalCount = 0;
		std::uint32_t presentationCount = 1;

		[[nodiscard]] constexpr bool IsActive() const noexcept
		{
			return active && runtimeSlotID != 0 && slotDigest != 0 && formID != 0 &&
			       epoch != 0 && generation != 0;
		}

		constexpr void Clear() noexcept
		{
			*this = {};
		}
	};

	struct ValidationEvidence
	{
		bool readable = false;
		bool targetHandHasForm = false;
		bool otherHandHasSameForm = false;
		bool targetMemberUnique = false;
		bool targetMemberPoisoned = false;
		bool targetMemberEnchanted = false;
		bool exactlyOnePoisonedMember = false;
		bool hasCompetingModifiedMember = false;
		std::uint16_t storedUID = 0;
		std::uint64_t runtimeSlotID = 0;
		std::uint64_t slotDigest = 0;
		FormID formID = 0;
		Epoch epoch = 0;
		std::uint64_t storedLogicalSignature = 0;
		std::uint64_t currentTargetSignature = 0;
		std::uint64_t currentTargetNonPoisonSignature = 0;
		FormID currentPoisonFormID = 0;
		int currentTotalCount = 0;
	};

	enum class ValidationDecision : std::uint8_t
	{
		Valid,
		Drop
	};

	enum class ValidationFailure : std::uint8_t
	{
		None,
		Inactive,
		UnreadableTopology,
		SlotMismatch,
		FormMismatch,
		EpochChanged,
		StoredIdentityChanged,
		TargetHandChanged,
		OppositeHandSameForm,
		TargetMemberAmbiguous,
		PoisonDisappeared,
		PoisonPopulationAmbiguous,
		CompetingModifiedMember,
		CountChanged,
		SignatureChanged,
		PoisonFormChanged
	};

	struct Presentation
	{
		bool active = false;
		bool right = false;
		bool left = false;
		bool poisoned = false;
		bool enchanted = false;
		std::uint32_t count = 0;
	};

	[[nodiscard]] constexpr bool CanCreate(const CreationEvidence& a_evidence) noexcept
	{
		return a_evidence.lineage == Lineage::StrongHandTransition &&
		       a_evidence.originatedFromUID0GroupedFallback &&
		       a_evidence.epochMatches &&
		       a_evidence.slotSignatureMatchesBefore &&
		       a_evidence.readable &&
		       a_evidence.targetHandHasForm &&
		       !a_evidence.otherHandHasSameForm &&
		       a_evidence.targetMemberUnique &&
		       a_evidence.targetMemberPoisoned &&
		       a_evidence.exactlyOnePoisonedMember &&
		       a_evidence.countConserved &&
		       !a_evidence.hasCompetingModifiedMember &&
		       a_evidence.preMutationUID == 0 &&
		       a_evidence.runtimeSlotID != 0 &&
		       a_evidence.slotDigest != 0 &&
		       a_evidence.formID != 0 &&
		       a_evidence.epoch != 0 &&
		       a_evidence.beforeSignature != 0 &&
		       a_evidence.afterSignature != 0 &&
		       a_evidence.afterNonPoisonSignature != 0 &&
		       a_evidence.poisonFormID != 0 &&
		       a_evidence.expectedTotalCount > 1;
	}

	[[nodiscard]] constexpr Alias Create(
		const CreationEvidence& a_evidence,
		std::uint64_t a_generation) noexcept
	{
		if (!CanCreate(a_evidence) || a_generation == 0) {
			return {};
		}
		Alias alias;
		alias.active = true;
		alias.runtimeSlotID = a_evidence.runtimeSlotID;
		alias.slotDigest = a_evidence.slotDigest;
		alias.formID = a_evidence.formID;
		alias.hand = a_evidence.hand;
		alias.epoch = a_evidence.epoch;
		alias.generation = a_generation;
		alias.beforeSignature = a_evidence.beforeSignature;
		alias.afterSignature = a_evidence.afterSignature;
		alias.afterNonPoisonSignature = a_evidence.afterNonPoisonSignature;
		alias.poisonFormID = a_evidence.poisonFormID;
		alias.expectedTotalCount = a_evidence.expectedTotalCount;
		alias.presentationCount = 1;
		return alias;
	}

	[[nodiscard]] constexpr ValidationFailure GetValidationFailure(
		const Alias& a_alias,
		const ValidationEvidence& a_evidence) noexcept
	{
		if (!a_alias.IsActive()) return ValidationFailure::Inactive;
		if (!a_evidence.readable) return ValidationFailure::UnreadableTopology;
		if (a_evidence.runtimeSlotID != a_alias.runtimeSlotID ||
		    a_evidence.slotDigest != a_alias.slotDigest) return ValidationFailure::SlotMismatch;
		if (a_evidence.formID != a_alias.formID) return ValidationFailure::FormMismatch;
		if (a_evidence.epoch != a_alias.epoch) return ValidationFailure::EpochChanged;
		if (a_evidence.storedUID != 0 ||
		    a_evidence.storedLogicalSignature != a_alias.beforeSignature) return ValidationFailure::StoredIdentityChanged;
		if (!a_evidence.targetHandHasForm) return ValidationFailure::TargetHandChanged;
		if (a_evidence.otherHandHasSameForm) return ValidationFailure::OppositeHandSameForm;
		if (!a_evidence.targetMemberUnique) return ValidationFailure::TargetMemberAmbiguous;
		if (!a_evidence.targetMemberPoisoned) return ValidationFailure::PoisonDisappeared;
		if (!a_evidence.exactlyOnePoisonedMember) return ValidationFailure::PoisonPopulationAmbiguous;
		if (a_evidence.hasCompetingModifiedMember) return ValidationFailure::CompetingModifiedMember;
		if (a_evidence.currentTotalCount != a_alias.expectedTotalCount) return ValidationFailure::CountChanged;
		if (a_evidence.currentTargetSignature == 0 ||
		    a_evidence.currentTargetNonPoisonSignature != a_alias.afterNonPoisonSignature) return ValidationFailure::SignatureChanged;
		if (a_evidence.currentPoisonFormID != a_alias.poisonFormID) return ValidationFailure::PoisonFormChanged;
		return ValidationFailure::None;
	}

	[[nodiscard]] constexpr ValidationDecision Validate(
		const Alias& a_alias,
		const ValidationEvidence& a_evidence) noexcept
	{
		return GetValidationFailure(a_alias, a_evidence) == ValidationFailure::None ?
			ValidationDecision::Valid : ValidationDecision::Drop;
	}

	[[nodiscard]] constexpr Presentation Present(
		const Alias& a_alias,
		const ValidationEvidence& a_evidence) noexcept
	{
		if (Validate(a_alias, a_evidence) != ValidationDecision::Valid) {
			return {};
		}
		Presentation presentation;
		presentation.active = true;
		presentation.right = a_alias.hand == Hand::Right;
		presentation.left = a_alias.hand == Hand::Left;
		presentation.poisoned = true;
		presentation.enchanted = a_evidence.targetMemberEnchanted;
		presentation.count = a_alias.presentationCount;
		return presentation;
	}
}
