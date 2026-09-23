#pragma once

#include <cstdint>
#include <span>

namespace AmmoRangedWeaponPoisonPresentationPolicy
{
	enum class WeaponKind : std::uint8_t
	{
		kNone,
		kOther,
		kBow,
		kCrossbow
	};

	struct MemberEvidence
	{
		bool readable = false;
		bool wornRight = false;
		bool wornLeft = false;
		bool hasPoison = false;
		bool poisonPointerValid = false;
		std::uint32_t poisonFormID = 0;
		std::uint32_t poisonCount = 0;
	};

	struct Evidence
	{
		WeaponKind weaponKind = WeaponKind::kNone;
		std::uint32_t weaponFormID = 0;
		bool inventoryReadable = false;
		bool equippedStateStable = false;
		std::span<const MemberEvidence> members{};
	};

	struct Presentation
	{
		bool targetResolved = false;
		std::uint32_t weaponFormID = 0;
		std::uint32_t poisonFormID = 0;
	};

	[[nodiscard]] constexpr bool IsRanged(WeaponKind a_kind) noexcept
	{
		return a_kind == WeaponKind::kBow || a_kind == WeaponKind::kCrossbow;
	}

	[[nodiscard]] constexpr Presentation Resolve(const Evidence& a_evidence) noexcept
	{
		if (!IsRanged(a_evidence.weaponKind) || a_evidence.weaponFormID == 0 ||
		    !a_evidence.inventoryReadable || !a_evidence.equippedStateStable) {
			return {};
		}

		const MemberEvidence* equippedMember = nullptr;
		for (const auto& member : a_evidence.members) {
			if (!member.readable) {
				return {};
			}
			if (!member.wornRight && !member.wornLeft) {
				continue;
			}
			if (equippedMember) {
				return {};
			}
			equippedMember = &member;
		}

		if (!equippedMember) {
			return {};
		}
		if (!equippedMember->hasPoison) {
			return { true, a_evidence.weaponFormID, 0 };
		}
		if (!equippedMember->poisonPointerValid || equippedMember->poisonFormID == 0 ||
		    equippedMember->poisonCount == 0) {
			return {};
		}
		return { true, a_evidence.weaponFormID, equippedMember->poisonFormID };
	}
}
