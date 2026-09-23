#include "AmmoRangedWeaponPoisonPresentationPolicy.h"

#include <array>
#include <iostream>
#include <type_traits>

int main()
{
	using namespace AmmoRangedWeaponPoisonPresentationPolicy;
	static_assert(std::is_trivially_copyable_v<MemberEvidence>);
	static_assert(std::is_trivially_copyable_v<Presentation>);

	int passed = 0;
	int failed = 0;
	const auto check = [&](bool a_ok, const char* a_name) {
		(a_ok ? passed : failed)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_name << '\n';
	};
	const auto expect = [&](const Evidence& a_evidence, bool a_resolved, std::uint32_t a_weapon,
	                        std::uint32_t a_poison, const char* a_name) {
		const auto result = Resolve(a_evidence);
		check(result.targetResolved == a_resolved && result.weaponFormID == a_weapon &&
		          result.poisonFormID == a_poison,
			a_name);
	};

	constexpr MemberEvidence cleanWorn{ true, true, false, false, false, 0, 0 };
	constexpr MemberEvidence poisonedWorn{ true, true, false, true, true, 0x1234, 3 };
	constexpr MemberEvidence cleanSibling{ true, false, false, false, false, 0, 0 };
	constexpr MemberEvidence poisonedSibling{ true, false, false, true, true, 0x5678, 2 };

	std::array one{ cleanWorn };
	expect({ WeaponKind::kNone, 0, true, true, one }, false, 0, 0,
		"no weapon has no presentation");
	expect({ WeaponKind::kOther, 0x100, true, true, one }, false, 0, 0,
		"non-ranged weapon has no presentation");
	expect({ WeaponKind::kBow, 0x100, true, true, one }, true, 0x100, 0,
		"exact ranged member without poison resolves clean");
	one[0] = poisonedWorn;
	expect({ WeaponKind::kBow, 0x100, true, true, one }, true, 0x100, 0x1234,
		"exact ranged member poison resolves exact form");

	std::array siblings{ cleanWorn, poisonedSibling };
	expect({ WeaponKind::kBow, 0x100, true, true, siblings }, true, 0x100, 0,
		"unequipped poisoned same-form sibling is never borrowed");
	siblings = { poisonedWorn, cleanSibling };
	expect({ WeaponKind::kCrossbow, 0x200, true, true, siblings }, true, 0x200, 0x1234,
		"equipped poisoned sibling wins over clean same-form sibling");
	siblings[1] = poisonedWorn;
	expect({ WeaponKind::kBow, 0x100, true, true, siblings }, false, 0, 0,
		"multiple worn same-form members fail closed");

	one[0] = poisonedWorn;
	one[0].poisonPointerValid = false;
	expect({ WeaponKind::kBow, 0x100, true, true, one }, false, 0, 0,
		"null ExtraPoison poison fails closed");
	one[0] = poisonedWorn;
	one[0].poisonFormID = 0;
	expect({ WeaponKind::kBow, 0x100, true, true, one }, false, 0, 0,
		"invalid poison FormID fails closed");
	one[0] = poisonedWorn;
	one[0].poisonCount = 0;
	expect({ WeaponKind::kBow, 0x100, true, true, one }, false, 0, 0,
		"exhausted poison fails closed");
	one[0] = poisonedWorn;
	expect({ WeaponKind::kBow, 0x100, false, true, one }, false, 0, 0,
		"unreadable inventory fails closed");
	expect({ WeaponKind::kBow, 0x100, true, false, one }, false, 0, 0,
		"equipped-state race fails closed");
	one[0].readable = false;
	expect({ WeaponKind::kBow, 0x100, true, true, one }, false, 0, 0,
		"unreadable sibling evidence fails closed");

	one[0] = poisonedWorn;
	const auto firstPoison = Resolve({ WeaponKind::kBow, 0x100, true, true, one });
	one[0].poisonFormID = 0x9876;
	const auto replacement = Resolve({ WeaponKind::kBow, 0x100, true, true, one });
	check(firstPoison.poisonFormID != replacement.poisonFormID,
		"poison replacement changes presentation signature");
	one[0] = cleanWorn;
	const auto removed = Resolve({ WeaponKind::kBow, 0x100, true, true, one });
	check(removed.targetResolved && removed.poisonFormID == 0 &&
	          removed.poisonFormID != replacement.poisonFormID,
		"poison removal changes presentation signature");

	std::cout << passed << " passed, " << failed << " failed\n";
	return failed ? 1 : 0;
}
