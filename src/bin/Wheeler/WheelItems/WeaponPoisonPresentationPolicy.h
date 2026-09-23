#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace WeaponPoisonPresentationPolicy
{
	struct MemberEvidence
	{
		bool readable = false;
		std::uint16_t uniqueID = 0;
		int count = 0;
		bool hasPoison = false;
		bool poisonPointerValid = false;
		std::uint32_t poisonFormID = 0;
		std::uint32_t poisonCount = 0;
	};

	struct WeaponPoisonPresentation
	{
		bool safelyResolved = false;
		std::uint32_t poisonFormID = 0;
		std::uint32_t poisonCount = 0;
	};

	[[nodiscard]] constexpr WeaponPoisonPresentation Resolve(
		int a_sameFormCount, std::uint16_t a_uid, bool a_enumerationReadable,
		std::span<const MemberEvidence> a_members) noexcept
	{
		if (!a_enumerationReadable || a_sameFormCount <= 0 || (a_uid == 0 && a_sameFormCount != 1)) return {};
		int represented = 0;
		const MemberEvidence* exact = nullptr;
		for (const auto& member : a_members) {
			if (!member.readable || member.count <= 0 || member.count > a_sameFormCount - represented) return {};
			represented += member.count;
			if (a_uid == 0 || member.uniqueID == a_uid) {
				if (exact || member.count != 1) return {};
				exact = &member;
			}
		}
		if (!exact || !exact->hasPoison || !exact->poisonPointerValid ||
		    exact->poisonFormID == 0 || exact->poisonCount == 0) return {};
		return { true, exact->poisonFormID, exact->poisonCount };
	}

	[[nodiscard]] constexpr WeaponPoisonPresentation ValidateForm(
		WeaponPoisonPresentation a_result, bool a_currentAlchemyLookupValid) noexcept
	{
		return a_currentAlchemyLookupValid && a_result.safelyResolved ? a_result : WeaponPoisonPresentation{};
	}

	// Owned text only; count is usage state, never a damage multiplier.
	[[nodiscard]] inline std::string AppendDescription(
		std::string a_existing, std::string_view a_name, std::string_view a_effects)
	{
		if (a_name.empty() && a_effects.empty()) return a_existing;
		if (!a_existing.empty()) a_existing += "\n\n";
		a_existing += a_name;
		if (!a_name.empty() && !a_effects.empty()) a_existing += '\n';
		a_existing += a_effects;
		return a_existing;
	}
}
