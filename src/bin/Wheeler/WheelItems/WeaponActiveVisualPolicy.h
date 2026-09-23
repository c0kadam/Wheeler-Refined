#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace WeaponActiveVisualPolicy
{
	struct MemberEvidence
	{
		bool metadataReadable = false;
		bool instanceSpecific = false;
		bool wornReadable = false;
		bool worn = false;
		bool wornLeft = false;
		int count = 0;
	};

	// nullopt preserves the existing active-state resolver. This is only a
	// UID-zero mixed-row visual veto/proof, never a new identity authority.
	[[nodiscard]] constexpr std::optional<bool> Resolve(
		std::uint16_t a_uid, int a_sameFormCount, bool a_enumerationReadable,
		std::span<const MemberEvidence> a_members) noexcept
	{
		if (a_uid != 0 || a_sameFormCount == 1) {
			return std::nullopt;
		}
		if (a_sameFormCount <= 0 || !a_enumerationReadable) {
			return false;
		}
		bool mixed = false;
		for (const auto& member : a_members) {
			if (!member.metadataReadable) {
				return false;
			}
			mixed = mixed || member.instanceSpecific;
		}
		if (!mixed) {
			// Includes implicit plain stacks with no explicit members. UID alone
			// and worn markers do not split otherwise identical plain copies.
			return std::nullopt;
		}
		int represented = 0;
		bool plainWorn = false;
		bool seenRight = false;
		bool seenLeft = false;
		for (const auto& member : a_members) {
			if (!member.wornReadable || member.count <= 0 ||
			    member.count > a_sameFormCount - represented) {
				return false;
			}
			represented += member.count;
			const bool right = member.worn && !member.wornLeft;
			const bool left = member.wornLeft;
			if ((right && seenRight) || (left && seenLeft)) {
				return false;  // Two different members claiming the same hand.
			}
			seenRight = seenRight || right;
			seenLeft = seenLeft || left;
			// Either hand is sufficient, including both markers on one 2H member.
			plainWorn = plainWorn || (!member.instanceSpecific && (member.worn || member.wornLeft));
		}
		return plainWorn;
	}
}
