#include "WheelItems/WeaponLiveVisualStatusPolicy.h"

#include <array>
#include <iostream>
#include <type_traits>

namespace
{
	using WeaponLiveVisualStatusPolicy::MemberEvidence;
	using WeaponLiveVisualStatusPolicy::OwnershipProof;
	using WeaponLiveVisualStatusPolicy::ResolutionEvidence;
	using WeaponLiveVisualStatusPolicy::WeaponLiveVisualStatus;

	bool Expect(const WeaponLiveVisualStatus& a_actual, const WeaponLiveVisualStatus& a_expected, const char* a_name)
	{
		const bool passed = a_actual.ownership == a_expected.ownership &&
		                    a_actual.poisoned == a_expected.poisoned &&
		                    a_actual.enchanted == a_expected.enchanted;
		std::cout << (passed ? "PASS " : "FAIL ") << a_name << '\n';
		return passed;
	}

	WeaponLiveVisualStatus Resolve(
		int a_sameFormCount,
		std::uint16_t a_storedUniqueID,
		bool a_baseEnchanted,
		std::span<const MemberEvidence> a_members,
		bool a_readable = true)
	{
		return WeaponLiveVisualStatusPolicy::Resolve({
			a_sameFormCount,
			a_storedUniqueID,
			a_readable,
			a_baseEnchanted,
			a_members
		});
	}
}

int RunWeaponLiveVisualStatusVerification()
{
	static_assert(std::is_trivially_copyable_v<MemberEvidence>);
	static_assert(std::is_trivially_copyable_v<WeaponLiveVisualStatus>);
	static_assert(!std::is_pointer_v<decltype(MemberEvidence::uniqueID)>);

	bool ok = true;
	const std::array plain{ MemberEvidence{ 71, 1, true, false, false } };
	const std::array poisoned{ MemberEvidence{ 71, 1, true, true, false } };
	const std::array enchanted{ MemberEvidence{ 71, 1, true, false, true } };
	const std::array both{ MemberEvidence{ 71, 1, true, true, true } };

	ok &= Expect(Resolve(1, 71, false, plain),
		{ OwnershipProof::kExactUniqueID, false, false },
		"single unmodified exact weapon has no status badge");
	ok &= Expect(Resolve(1, 71, false, poisoned),
		{ OwnershipProof::kExactUniqueID, true, false },
		"single safely resolved poisoned weapon shows poison");
	ok &= Expect(Resolve(1, 71, false, plain),
		{ OwnershipProof::kExactUniqueID, false, false },
		"fresh poison absence clears the poison badge");
	ok &= Expect(Resolve(1, 71, false, enchanted),
		{ OwnershipProof::kExactUniqueID, false, true },
		"safely resolved extra-enchanted weapon shows enchantment");
	ok &= Expect(Resolve(1, 71, true, plain),
		{ OwnershipProof::kExactUniqueID, false, true },
		"base weapon enchantment shows enchantment");
	ok &= Expect(Resolve(1, 71, true, both),
		{ OwnershipProof::kExactUniqueID, true, true },
		"poison and enchantment remain simultaneously representable");

	const std::array several{
		MemberEvidence{ 70, 1, true, true, false },
		MemberEvidence{ 71, 1, true, false, true },
		MemberEvidence{ 72, 1, true, false, false }
	};
	ok &= Expect(Resolve(3, 71, false, several),
		{ OwnershipProof::kExactUniqueID, false, true },
		"exact UID reads only its matching physical member");
	ok &= Expect(Resolve(3, 70, false, several),
		{ OwnershipProof::kExactUniqueID, true, false },
		"manually rebound poisoned exact row displays poison");

	const std::array singleUidZero{ MemberEvidence{ 88, 1, true, true, false } };
	ok &= Expect(Resolve(1, 0, false, singleUidZero),
		{ OwnershipProof::kSingleSameForm, true, false },
		"sameFormCount one safely supports UID-zero live status");
	ok &= Expect(Resolve(1, 0, true, std::span<const MemberEvidence>{}),
		{ OwnershipProof::kSingleSameForm, false, true },
		"single implicit member supports base enchantment only");

	const std::array splitStack{
		MemberEvidence{ 0, 2, true, false, false },
		MemberEvidence{ 91, 1, true, true, false }
	};
	ok &= Expect(Resolve(3, 0, false, splitStack), {},
		"stacked UID-zero row split by poison fails closed");
	ok &= Expect(Resolve(3, 0, true, splitStack), {},
		"ambiguous grouped row cannot inherit poison or base-enchantment status");

	const std::array duplicateUid{
		MemberEvidence{ 71, 1, true, true, false },
		MemberEvidence{ 71, 1, true, false, true }
	};
	ok &= Expect(Resolve(2, 71, false, duplicateUid), {},
		"duplicate exact UID evidence fails closed");
	const std::array unreadable{ MemberEvidence{ 71, 1, false, true, true } };
	ok &= Expect(Resolve(1, 71, true, unreadable), {},
		"unreadable physical-member state fails closed");
	ok &= Expect(Resolve(1, 71, true, plain, false), {},
		"unreadable inventory enumeration fails closed");
	ok &= Expect(Resolve(2, 0, false, plain), {},
		"FormID alone never owns multi-copy presentation status");

	return ok ? 0 : 1;
}

#ifdef WHEELER_WEAPON_LIVE_VISUAL_STATUS_VERIFICATION_MAIN
int main()
{
	return RunWeaponLiveVisualStatusVerification();
}
#endif
