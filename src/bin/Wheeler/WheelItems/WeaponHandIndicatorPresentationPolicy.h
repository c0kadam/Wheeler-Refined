#pragma once

#include <span>
#include <string>
#include <string_view>

// Draw-only evidence. No engine pointers, UID authority, or persistent state.
namespace WeaponHandIndicatorPresentationPolicy
{
	[[nodiscard]] constexpr bool IsTwoHanded(bool a_bow, bool a_crossbow, bool a_twoHandSword, bool a_twoHandAxe) noexcept
	{
		return a_bow || a_crossbow || a_twoHandSword || a_twoHandAxe;
	}

	struct MemberEvidence
	{
		bool readable = false;
		bool instanceSpecific = false;
		bool worn = false;
		bool wornLeft = false;
		std::string signature;
	};

	// Only called for mixed true-2H presentation. One physical member supplies
	// both presentation hands; a second worn member makes that proof ambiguous.
	[[nodiscard]] inline bool ResolveMixedTwoHanded(
		bool a_snapshotReadable, bool a_cleanSentinel, std::string_view a_rowSignature,
		std::span<const MemberEvidence> a_members)
	{
		if (!a_snapshotReadable || (!a_cleanSentinel && a_rowSignature.empty())) {
			return false;
		}
		bool foundWorn = false;
		bool matchesRow = false;
		for (const auto& member : a_members) {
			if (!member.readable) {
				return false;
			}
			if (!member.worn && !member.wornLeft) {
				continue;
			}
			if (foundWorn) {
				return false;
			}
			foundWorn = true;
			matchesRow = a_cleanSentinel ? !member.instanceSpecific : member.signature == a_rowSignature;
		}
		return foundWorn && matchesRow;
	}
}
