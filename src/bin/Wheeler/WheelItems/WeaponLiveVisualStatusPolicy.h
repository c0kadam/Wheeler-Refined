#pragma once

#include <cstdint>
#include <span>

namespace WeaponLiveVisualStatusPolicy
{
	enum class OwnershipProof : std::uint8_t
	{
		kNone,
		kExactUniqueID,
		kSingleSameForm
	};

	struct MemberEvidence
	{
		std::uint16_t uniqueID = 0;
		int count = 0;
		bool statusReadable = false;
		bool poisoned = false;
		bool extraEnchanted = false;
	};

	struct ResolutionEvidence
	{
		int sameFormCount = 0;
		std::uint16_t storedUniqueID = 0;
		bool enumerationReadable = false;
		bool baseEnchanted = false;
		std::span<const MemberEvidence> members;
	};

	struct WeaponLiveVisualStatus
	{
		OwnershipProof ownership = OwnershipProof::kNone;
		bool poisoned = false;
		bool enchanted = false;

		[[nodiscard]] constexpr bool IsSafelyResolved() const noexcept
		{
			return ownership != OwnershipProof::kNone;
		}
	};

	[[nodiscard]] constexpr WeaponLiveVisualStatus Resolve(const ResolutionEvidence& a_evidence) noexcept
	{
		if (!a_evidence.enumerationReadable || a_evidence.sameFormCount <= 0) {
			return {};
		}

		int representedCount = 0;
		for (const auto& member : a_evidence.members) {
			if (member.count <= 0 || member.count > a_evidence.sameFormCount - representedCount) {
				return {};
			}
			representedCount += member.count;
		}

		if (a_evidence.storedUniqueID != 0) {
			const MemberEvidence* exact = nullptr;
			for (const auto& member : a_evidence.members) {
				if (member.uniqueID != a_evidence.storedUniqueID) {
					continue;
				}
				if (exact || member.count != 1 || !member.statusReadable) {
					return {};
				}
				exact = &member;
			}
			if (!exact) {
				return {};
			}
			return {
				OwnershipProof::kExactUniqueID,
				exact->poisoned,
				a_evidence.baseEnchanted || exact->extraEnchanted
			};
		}

		// A UID-zero slot receives live instance status only when the complete
		// same-FormID inventory count proves there is exactly one possible owner.
		if (a_evidence.sameFormCount != 1) {
			return {};
		}
		if (a_evidence.members.empty()) {
			return { OwnershipProof::kSingleSameForm, false, a_evidence.baseEnchanted };
		}
		if (a_evidence.members.size() != 1 || representedCount != 1 ||
		    !a_evidence.members.front().statusReadable) {
			return {};
		}

		const auto& only = a_evidence.members.front();
		return {
			OwnershipProof::kSingleSameForm,
			only.poisoned,
			a_evidence.baseEnchanted || only.extraEnchanted
		};
	}
}
