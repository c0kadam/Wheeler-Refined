#include "WheelItems/WeaponActiveVisualPolicy.h"

#include <array>
#include <iostream>
#include <type_traits>

int main()
{
	using namespace WeaponActiveVisualPolicy;
	static_assert(std::is_trivially_copyable_v<MemberEvidence>);
	int passed = 0;
	int failed = 0;
	const auto check = [&](std::optional<bool> actual, std::optional<bool> expected, const char* name) {
		const bool ok = actual == expected;
		(ok ? passed : failed)++;
		std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
	};
	const MemberEvidence plain{ true, false, true, false, false, 1 };
	const MemberEvidence modified{ true, true, true, true, false, 1 };
	std::array members{ plain, modified };
	check(Resolve(0, 2, true, members), false, "UID0 plain sibling of worn poisoned UID6 is inactive");
	members[0].worn = true;
	members[1].worn = false;
	check(Resolve(0, 2, true, members), true, "reverse direction: worn plain is active");
	for (const auto* kind : { "poison", "enchantment", "health/temper", "charge", "display name" }) {
		// Production maps each of these presence bits to instanceSpecific.
		members = { plain, modified };
		check(Resolve(0, 2, true, members), false, kind);
	}
	for (const auto* kind : { "1H", "greatsword", "battleaxe", "warhammer", "bow", "crossbow" }) {
		check(Resolve(0, 1, false, {}), std::nullopt, kind);
		members = { plain, modified };
		check(Resolve(0, 2, true, members), false, kind);
		members[0].worn = true;
		members[1].worn = false;
		check(Resolve(0, 2, true, members), true, kind);
	}
	for (int bits = 0; bits < 4; ++bits) {
		members = { plain, modified };
		members[1].worn = false;
		members[0].worn = (bits & 1) != 0;
		members[0].wornLeft = (bits & 2) != 0;
		check(Resolve(0, 2, true, members), bits != 0, "right OR left, including both markers on one member");
	}
	members = { plain, plain };
	check(Resolve(0, 2, true, members), std::nullopt, "equivalent plain copies retain legacy grouping");
	members[0].worn = true;
	members[1].wornLeft = true;
	check(Resolve(0, 2, true, members), std::nullopt, "same-form plain dual wield retains legacy behavior");
	check(Resolve(0, 3, true, {}), std::nullopt, "implicit plain group retains legacy behavior");
	members[0].wornReadable = false;
	check(Resolve(0, 2, true, members), std::nullopt, "unambiguous plain topology does not gain worn-read dependency");
	check(Resolve(0, 1, true, {}), std::nullopt, "different-form dual wield: each form remains single");
	members = { plain, modified };
	members[0].wornLeft = true;
	check(Resolve(0, 2, true, members), true, "same-form mixed dual wield: plain left and modified right");
	members[0].worn = true;
	check(Resolve(0, 2, true, members), true, "WornLeft wins when plain left also has Worn");
	members[0].wornLeft = false;
	check(Resolve(0, 2, true, members), false, "two different members claiming RIGHT fail closed");
	members[0].wornLeft = true;
	members[1].wornLeft = true;
	check(Resolve(0, 2, true, members), false, "two different members claiming LEFT fail closed");
	check(Resolve(6, 2, true, members), std::nullopt, "nonzero UID retains legacy resolver");
	check(Resolve(6, 0, false, {}), std::nullopt, "nonzero UID never enters new failure handling");
	check(Resolve(0, 2, false, {}), false, "missing entry");
	check(Resolve(0, 2, false, members), false, "unreadable list snapshot");
	check(Resolve(0, 0, true, members), false, "zero count");
	check(Resolve(0, -1, true, members), false, "negative count");
	members[0].wornReadable = false;
	check(Resolve(0, 2, true, members), false, "unreadable plain worn evidence");
	members = { plain, modified };
	members[0].worn = true;
	members[1].metadataReadable = false;
	check(Resolve(0, 2, true, members), false, "unreadable sibling metadata cannot mean plain");
	members[1] = modified;
	members[1].wornReadable = false;
	check(Resolve(0, 2, true, members), false, "incomplete mixed evidence fails closed despite plain proof");
	members[1] = modified;
	members[0].count = 0;
	check(Resolve(0, 2, true, members), false, "unreadable or zero member count");
	members[0].count = 2;
	check(Resolve(0, 2, true, members), false, "over-accounted mixed snapshot");
	const std::array onlyModified{ modified };
	check(Resolve(0, 2, true, onlyModified), false, "implicit plain member is not proof of worn plain");
	check(Resolve(0, 1, true, onlyModified), std::nullopt, "single modified member preserves sentinel legacy behavior");
	std::cout << passed << " passed, " << failed << " failed\n";
	return failed ? 1 : 0;
}
