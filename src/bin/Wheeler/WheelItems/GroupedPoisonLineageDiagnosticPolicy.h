#pragma once

#include <cstdint>

namespace GroupedPoisonLineageDiagnosticPolicy
{
	using Epoch = std::uint64_t;
	using FormID = std::uint32_t;

	enum class Hand : std::uint8_t
	{
		Right,
		Left
	};

	enum class Topology : std::uint8_t
	{
		Unknown,
		Empty,
		SingleLogicalRow,
		MixedLogicalRows
	};

	enum class Lineage : std::uint8_t
	{
		Ambiguous,
		StrongUniqueID,
		StrongHandTransition
	};

	enum class AdvanceDecision : std::uint8_t
	{
		None,
		Sample,
		CancelEpoch,
		Timeout
	};

	struct SnapshotSummary
	{
		bool readable = false;
		bool targetHandHasForm = false;
		bool otherHandHasSameForm = false;
		bool targetMemberUnique = false;
		bool targetUniqueIDGloballyUnique = false;
		bool targetMemberPoisoned = false;
		bool targetMemberEnchanted = false;
		bool exactlyOnePoisonedMember = false;
		bool hasUnreadableMember = false;
		int totalInventoryCount = 0;
		int representedItemCount = 0;
		int implicitPlainCount = 0;
		int cleanItemCount = 0;
		int modifiedItemCount = 0;
		int otherModifiedItemCount = 0;
		std::uint32_t extraListCount = 0;
		std::uint32_t poisonedMemberCount = 0;
		std::uint32_t poisonedItemCount = 0;
		std::uint32_t targetWornMemberCount = 0;
		std::uint16_t targetWornUniqueID = 0;
		FormID targetPoisonFormID = 0;
		std::uintptr_t targetWornXListAddress = 0;
		std::uint64_t targetLogicalSignatureDigest = 0;
		std::uint64_t targetNonPoisonSignatureDigest = 0;
		std::uint64_t topologyDigest = 0;
		Topology topology = Topology::Unknown;
	};

	struct Watcher
	{
		std::uint64_t transactionID = 0;
		Epoch epoch = 0;
		std::uint64_t runtimeSlotID = 0;
		FormID formID = 0;
		Hand targetHand = Hand::Right;
		int preEquipGroupCount = 0;
		std::uint64_t preEquipLogicalSignatureDigest = 0;
		std::uint64_t slotDigest = 0;
		double armedAt = 0.0;
		double hardDeadline = 0.0;
		double nextSampleAt = 0.0;
		std::uint64_t armedUpdateSequence = 0;
		std::uint32_t samplesRemaining = 0;
		bool hasBaseline = false;
		bool originatedFromUID0GroupedFallback = false;
		SnapshotSummary baseline{};

		[[nodiscard]] constexpr bool IsActive() const noexcept
		{
			return transactionID != 0 && epoch != 0 && formID != 0;
		}

		constexpr void Clear() noexcept
		{
			*this = {};
		}
	};

	[[nodiscard]] constexpr Watcher Arm(
		std::uint64_t a_transactionID,
		Epoch a_epoch,
		std::uint64_t a_runtimeSlotID,
		FormID a_formID,
		Hand a_targetHand,
		int a_preEquipGroupCount,
		std::uint64_t a_preEquipLogicalSignatureDigest,
		std::uint64_t a_slotDigest,
		double a_now,
		std::uint64_t a_updateSequence) noexcept
	{
		Watcher watcher;
		watcher.transactionID = a_transactionID;
		watcher.epoch = a_epoch;
		watcher.runtimeSlotID = a_runtimeSlotID;
		watcher.formID = a_formID;
		watcher.targetHand = a_targetHand;
		watcher.preEquipGroupCount = a_preEquipGroupCount;
		watcher.preEquipLogicalSignatureDigest = a_preEquipLogicalSignatureDigest;
		watcher.slotDigest = a_slotDigest;
		watcher.armedAt = a_now;
		watcher.hardDeadline = a_now + 25.0;
		watcher.nextSampleAt = a_now;
		watcher.armedUpdateSequence = a_updateSequence;
		watcher.samplesRemaining = 250;
		watcher.originatedFromUID0GroupedFallback = true;
		return watcher;
	}

	[[nodiscard]] constexpr AdvanceDecision Advance(
		Watcher& a_watcher,
		Epoch a_currentEpoch,
		double a_now,
		std::uint64_t a_updateSequence) noexcept
	{
		if (!a_watcher.IsActive()) {
			return AdvanceDecision::None;
		}
		if (a_watcher.epoch != a_currentEpoch) {
			return AdvanceDecision::CancelEpoch;
		}
		if (a_now >= a_watcher.hardDeadline || a_watcher.samplesRemaining == 0) {
			return AdvanceDecision::Timeout;
		}
		if (a_updateSequence <= a_watcher.armedUpdateSequence || a_now < a_watcher.nextSampleAt) {
			return AdvanceDecision::None;
		}
		--a_watcher.samplesRemaining;
		a_watcher.nextSampleAt = a_now + 0.1;
		return AdvanceDecision::Sample;
	}

	[[nodiscard]] constexpr bool HasTopologyChanged(
		const SnapshotSummary& a_before,
		const SnapshotSummary& a_after) noexcept
	{
		return a_before.readable != a_after.readable ||
		       a_before.targetHandHasForm != a_after.targetHandHasForm ||
		       a_before.otherHandHasSameForm != a_after.otherHandHasSameForm ||
		       a_before.totalInventoryCount != a_after.totalInventoryCount ||
		       a_before.topologyDigest != a_after.topologyDigest;
	}

	[[nodiscard]] constexpr Lineage Classify(
		const SnapshotSummary& a_before,
		const SnapshotSummary& a_after) noexcept
	{
		const bool commonProof =
			a_before.readable && a_after.readable &&
			!a_before.hasUnreadableMember && !a_after.hasUnreadableMember &&
			a_before.targetHandHasForm && a_after.targetHandHasForm &&
			a_before.totalInventoryCount == a_after.totalInventoryCount &&
			a_before.targetMemberUnique && a_after.targetMemberUnique &&
			a_before.targetMemberPoisoned != a_after.targetMemberPoisoned &&
			a_before.targetNonPoisonSignatureDigest != 0 &&
			a_before.targetNonPoisonSignatureDigest == a_after.targetNonPoisonSignatureDigest;
		if (!commonProof) {
			return Lineage::Ambiguous;
		}

		const bool poisonPopulationMatchesTransition = a_after.targetMemberPoisoned ?
			a_after.exactlyOnePoisonedMember : a_before.exactlyOnePoisonedMember;
		if (!poisonPopulationMatchesTransition) {
			return Lineage::Ambiguous;
		}

		if (a_before.targetWornUniqueID != 0 &&
		    a_before.targetWornUniqueID == a_after.targetWornUniqueID &&
		    a_before.targetUniqueIDGloballyUnique &&
		    a_after.targetUniqueIDGloballyUnique) {
			return Lineage::StrongUniqueID;
		}

		if (a_before.targetWornUniqueID == 0 &&
		    !a_before.targetMemberPoisoned && a_after.targetMemberPoisoned &&
		    !a_before.otherHandHasSameForm && !a_after.otherHandHasSameForm &&
		    a_before.otherModifiedItemCount == 0 && a_after.otherModifiedItemCount == 0) {
			return Lineage::StrongHandTransition;
		}

		return Lineage::Ambiguous;
	}
}
