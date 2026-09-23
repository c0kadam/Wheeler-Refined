#pragma once

#include <cstdint>

namespace LegacyWeaponRestorePolicy
{
	enum class RowKind : std::uint8_t
	{
		Invalid,
		Physical,
		PlainForm,
		GroupEquivalent
	};

	enum class Topology : std::uint8_t
	{
		Unknown,
		Unambiguous,
		MixedLogicalRows,
		GroupEquivalent
	};

	enum class Resolution : std::uint8_t
	{
		None,
		ExactLogicalRow,
		UIDLineageFallback,
		PlainFormLevel,
		GroupEquivalentMember,
		GroupEquivalentFormLevel
	};

	enum class DualHandCaptureDecision : std::uint8_t
	{
		PreserveExactAuthority,
		PromoteBothToGroupEquivalent,
		RejectIndistinguishableCollision
	};

	struct DualHandCaptureEvidence
	{
		RowKind leftRowKind = RowKind::Invalid;
		RowKind rightRowKind = RowKind::Invalid;
		std::uint32_t leftFormID = 0;
		std::uint32_t rightFormID = 0;
		std::uint16_t leftUniqueID = 0;
		std::uint16_t rightUniqueID = 0;
		bool logicalRowSignaturesMatch = false;
		bool distinctWornMembersProven = false;
		bool groupEquivalentProven = false;
	};

	[[nodiscard]] constexpr DualHandCaptureDecision DecideDualHandCapture(
		const DualHandCaptureEvidence& a_evidence) noexcept
	{
		if (a_evidence.leftFormID == 0 ||
			a_evidence.leftFormID != a_evidence.rightFormID) {
			return DualHandCaptureDecision::PreserveExactAuthority;
		}

		const bool hasGroupObligation =
			a_evidence.leftRowKind == RowKind::GroupEquivalent ||
			a_evidence.rightRowKind == RowKind::GroupEquivalent;
		const bool hasCollidingExactLineage =
			a_evidence.leftRowKind == RowKind::Physical &&
			a_evidence.rightRowKind == RowKind::Physical &&
			a_evidence.leftUniqueID != 0 &&
			a_evidence.leftUniqueID == a_evidence.rightUniqueID &&
			a_evidence.logicalRowSignaturesMatch;
		if (!hasGroupObligation && !hasCollidingExactLineage) {
			return DualHandCaptureDecision::PreserveExactAuthority;
		}

		return a_evidence.logicalRowSignaturesMatch &&
		       a_evidence.distinctWornMembersProven &&
		       a_evidence.groupEquivalentProven ?
			DualHandCaptureDecision::PromoteBothToGroupEquivalent :
			DualHandCaptureDecision::RejectIndistinguishableCollision;
	}

	struct GroupProofEvidence
	{
		int sameFormCount = 0;
		int representedCount = 0;
		int implicitCount = 0;
		int distinctLogicalRows = 0;
		int unreadableMembers = 0;
		bool implicitMembersCompatible = false;
		bool wornWeaponAttributable = false;
		int eligibleLogicalCount = 0;
	};

	[[nodiscard]] constexpr bool ProvesGroupEquivalent(
		const GroupProofEvidence& a_evidence,
		bool a_requireWornAttribution) noexcept
	{
		return a_evidence.sameFormCount > 1 &&
		       a_evidence.representedCount >= 0 &&
		       a_evidence.implicitCount >= 0 &&
		       a_evidence.representedCount + a_evidence.implicitCount == a_evidence.sameFormCount &&
		       a_evidence.distinctLogicalRows == 1 &&
		       a_evidence.unreadableMembers == 0 &&
		       a_evidence.implicitMembersCompatible &&
		       (!a_requireWornAttribution || a_evidence.wornWeaponAttributable);
	}

	[[nodiscard]] constexpr bool AllowsGroupEquivalentFormLevel(
		const GroupProofEvidence& a_evidence,
		bool a_signatureMatches) noexcept
	{
		return a_signatureMatches &&
		       ProvesGroupEquivalent(a_evidence, false) &&
		       a_evidence.eligibleLogicalCount > 0 &&
		       a_evidence.eligibleLogicalCount <= a_evidence.sameFormCount;
	}

	struct FailedRestoreRetryEvidence
	{
		bool tokenValid = false;
		bool targetStillPending = false;
		bool currentHandEmpty = false;
		bool epochMatches = false;
		bool restoreWindowOpen = false;
		bool gameplayIntentCurrent = false;
		std::uint8_t retriesUsed = 0;
	};

	[[nodiscard]] constexpr bool ShouldRetryFailedRestore(
		const FailedRestoreRetryEvidence& a_evidence) noexcept
	{
		return a_evidence.tokenValid &&
		       a_evidence.targetStillPending &&
		       a_evidence.currentHandEmpty &&
		       a_evidence.epochMatches &&
		       a_evidence.restoreWindowOpen &&
		       a_evidence.gameplayIntentCurrent &&
		       a_evidence.retriesUsed == 0;
	}

	struct Evidence
	{
		RowKind rowKind = RowKind::Invalid;
		Topology topology = Topology::Unknown;
		int sameFormCount = 0;
		std::uint16_t uniqueID = 0;
		int exactSignatureCandidates = 0;
		int uidLineageCandidates = 0;
		bool groupEquivalentProven = false;
		bool groupSignatureMatches = false;
		bool groupFormLevelAllowed = false;
	};

	[[nodiscard]] constexpr Resolution Decide(const Evidence& a_evidence) noexcept
	{
		if (a_evidence.sameFormCount <= 0 || a_evidence.rowKind == RowKind::Invalid) {
			return Resolution::None;
		}

		if (a_evidence.rowKind == RowKind::PlainForm) {
			return a_evidence.sameFormCount == 1 &&
			       a_evidence.topology == Topology::Unambiguous ?
				Resolution::PlainFormLevel : Resolution::None;
		}

		if (a_evidence.rowKind == RowKind::GroupEquivalent) {
			if (a_evidence.sameFormCount <= 1 ||
				!a_evidence.groupEquivalentProven ||
				!a_evidence.groupSignatureMatches) {
				return Resolution::None;
			}
			if (a_evidence.exactSignatureCandidates > 0) {
				return Resolution::GroupEquivalentMember;
			}
			return a_evidence.groupFormLevelAllowed ?
				Resolution::GroupEquivalentFormLevel : Resolution::None;
		}

		if (a_evidence.uniqueID != 0) {
			if (a_evidence.exactSignatureCandidates > 0) {
				return Resolution::ExactLogicalRow;
			}
			// Preserve this lineage fallback for legacy weapon restore behavior.
			// A same-UID member may have diverged after mutable changes.
			return a_evidence.uidLineageCandidates > 0 ?
				Resolution::UIDLineageFallback : Resolution::None;
		}

		// UID-zero rows may resolve only when the entire same-form topology is
		// positively unique and exactly one current member matches the row.
		return a_evidence.sameFormCount == 1 &&
		       a_evidence.topology == Topology::Unambiguous &&
		       a_evidence.exactSignatureCandidates == 1 ?
			Resolution::ExactLogicalRow : Resolution::None;
	}
}
