#pragma once

#include <cstdint>

namespace AlreadyPoisonedReapplyGuardPolicy
{
	using FormID = std::uint32_t;

	enum class Decision : std::uint8_t
	{
		kAllowQueue,
		kBlockAlreadyPoisoned,
		kBlockAmbiguous
	};

	enum class Reason : std::uint8_t
	{
		kCleanResolvedTarget,
		kTargetAlreadyPoisoned,
		kNoEquippedWeapon,
		kSameFormDualWield,
		kUnreadableEvidence,
		kTargetMemberNotUnique,
		kOppositeHandMemberPresent
	};

	enum class TargetModel : std::uint8_t
	{
		kPerHand,
		kSinglePhysicalTwoHanded
	};

	struct HandEvidence
	{
		bool equipped = false;
		FormID formID = 0;
		bool readable = false;
		std::uint32_t targetWornMembers = 0;
		std::uint32_t oppositeWornMembers = 0;
		std::uint32_t poisonedTargetMembers = 0;
	};

	struct Evidence
	{
		HandEvidence right{};
		HandEvidence left{};
		bool equippedStateStable = true;
		TargetModel targetModel = TargetModel::kPerHand;
		HandEvidence singlePhysicalTarget{};
	};

	struct Result
	{
		Decision decision = Decision::kBlockAmbiguous;
		Reason reason = Reason::kUnreadableEvidence;

		[[nodiscard]] constexpr bool AllowsQueue() const noexcept
		{
			return decision == Decision::kAllowQueue;
		}
	};

	[[nodiscard]] constexpr Result ValidateHand(const HandEvidence& a_hand) noexcept
	{
		if (!a_hand.equipped) {
			return { Decision::kAllowQueue, Reason::kCleanResolvedTarget };
		}
		if (!a_hand.readable || a_hand.poisonedTargetMembers > a_hand.targetWornMembers) {
			return { Decision::kBlockAmbiguous, Reason::kUnreadableEvidence };
		}
		if (a_hand.targetWornMembers != 1) {
			return { Decision::kBlockAmbiguous, Reason::kTargetMemberNotUnique };
		}
		if (a_hand.oppositeWornMembers != 0) {
			return { Decision::kBlockAmbiguous, Reason::kOppositeHandMemberPresent };
		}
		return { Decision::kAllowQueue, Reason::kCleanResolvedTarget };
	}

	[[nodiscard]] constexpr Result Evaluate(const Evidence& a_evidence) noexcept
	{
		if (a_evidence.targetModel == TargetModel::kSinglePhysicalTwoHanded) {
			if (!a_evidence.singlePhysicalTarget.equipped) {
				return { Decision::kBlockAmbiguous, Reason::kNoEquippedWeapon };
			}
			if (!a_evidence.equippedStateStable) {
				return { Decision::kBlockAmbiguous, Reason::kUnreadableEvidence };
			}

			const Result target = ValidateHand(a_evidence.singlePhysicalTarget);
			if (!target.AllowsQueue()) {
				return target;
			}
			if (a_evidence.singlePhysicalTarget.poisonedTargetMembers != 0) {
				return { Decision::kBlockAlreadyPoisoned, Reason::kTargetAlreadyPoisoned };
			}
			return { Decision::kAllowQueue, Reason::kCleanResolvedTarget };
		}

		if (!a_evidence.right.equipped && !a_evidence.left.equipped) {
			return { Decision::kBlockAmbiguous, Reason::kNoEquippedWeapon };
		}
		if (!a_evidence.equippedStateStable) {
			return { Decision::kBlockAmbiguous, Reason::kUnreadableEvidence };
		}
		if (a_evidence.right.equipped && a_evidence.left.equipped &&
		    a_evidence.right.formID != 0 &&
		    a_evidence.right.formID == a_evidence.left.formID) {
			return { Decision::kBlockAmbiguous, Reason::kSameFormDualWield };
		}

		const Result right = ValidateHand(a_evidence.right);
		if (!right.AllowsQueue()) {
			return right;
		}
		const Result left = ValidateHand(a_evidence.left);
		if (!left.AllowsQueue()) {
			return left;
		}

		if (a_evidence.right.poisonedTargetMembers != 0 ||
		    a_evidence.left.poisonedTargetMembers != 0) {
			return { Decision::kBlockAlreadyPoisoned, Reason::kTargetAlreadyPoisoned };
		}
		return { Decision::kAllowQueue, Reason::kCleanResolvedTarget };
	}

	[[nodiscard]] constexpr const char* TargetModelName(TargetModel a_model) noexcept
	{
		switch (a_model) {
		case TargetModel::kPerHand:
			return "per_hand";
		case TargetModel::kSinglePhysicalTwoHanded:
			return "single_two_handed";
		}
		return "unknown";
	}

	[[nodiscard]] constexpr const char* ReasonName(Reason a_reason) noexcept
	{
		switch (a_reason) {
		case Reason::kCleanResolvedTarget:
			return "clean_resolved_target";
		case Reason::kTargetAlreadyPoisoned:
			return "target_already_poisoned";
		case Reason::kNoEquippedWeapon:
			return "no_equipped_weapon";
		case Reason::kSameFormDualWield:
			return "same_form_dual_wield_ambiguous";
		case Reason::kUnreadableEvidence:
			return "target_evidence_unreadable";
		case Reason::kTargetMemberNotUnique:
			return "target_worn_member_not_unique";
		case Reason::kOppositeHandMemberPresent:
			return "opposite_hand_member_present";
		}
		return "target_attribution_ambiguous";
	}
}
