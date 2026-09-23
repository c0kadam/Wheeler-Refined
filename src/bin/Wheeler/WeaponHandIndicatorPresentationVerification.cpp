#include "WheelItems/WeaponHandIndicatorPresentationPolicy.h"

#include <array>
#include <iostream>
#include <optional>

namespace
{
	using namespace WeaponHandIndicatorPresentationPolicy;
	// Integration fixture for the unchanged legacy gates. Source verification
	// separately checks those gates and the production adapter; this is not an
	// engine inventory mock. The mixed-2H decision below is production policy.
	std::optional<bool> Query(bool formMatches, int topology, bool twoHanded, bool left,
		bool clean, std::string_view signature, std::span<const MemberEvidence> members)
	{
		if (!formMatches || topology < 0) return false;
		if (topology == 0) return std::nullopt;
		if (twoHanded) return ResolveMixedTwoHanded(true, clean, signature, members);
		for (const auto& member : members) {
			if (member.readable && (left ? member.wornLeft : member.worn && !member.wornLeft) &&
			    (clean ? !member.instanceSpecific : member.signature == signature)) return true;
		}
		return false;
	}
}

int main()
{
	int passed = 0, failed = 0;
	const auto check = [&](bool ok, const std::string& name) {
		(ok ? passed : failed)++;
		if (!ok) std::cerr << "FAIL " << name << '\n';
	};
	const MemberEvidence plain{ true, false, false, false, "plain" };
	for (int category = 0; category < 4; ++category) {
		const bool twoHanded = IsTwoHanded(category == 0, category == 1, category == 2, category == 3);
		check(twoHanded, "all four production categories select 2H");
		for (const char* kind : { "poison", "enchantment", "health", "charge", "displayName" }) {
			std::array members{ plain, MemberEvidence{ true, true, true, false, kind } };
			for (bool left : { false, true }) {
				check(Query(true, 1, twoHanded, left, false, kind, members) == true, "modified worn => LR");
				check(Query(true, 1, twoHanded, left, true, "", members) == false, "plain sibling => none");
			}
			members[0].worn = true;
			members[1].worn = false;
			for (bool left : { false, true }) {
				check(Query(true, 1, twoHanded, left, true, "", members) == true, "plain worn => LR");
				check(Query(true, 1, twoHanded, left, false, kind, members) == false, "modified sibling => none");
			}
			members[0] = { true, true, true, false, "different-modified-row" };
			check(ResolveMixedTwoHanded(true, false, members[0].signature, members), "distinct modified worn row");
			check(!ResolveMixedTwoHanded(true, false, kind, members), "distinct modified sibling");
		}
	}
	check(!IsTwoHanded(false, false, false, false), "1H sword/axe/mace/dagger/staff excluded");
	std::array members{ plain, MemberEvidence{ true, true, true, false, "poison" } };
	for (bool clean : { false, true }) {
		for (bool left : { false, true }) {
			check(!Query(true, 0, true, left, clean, "poison", members).has_value(), "single-copy preserves nullopt/mirrored cache");
		}
	}
	for (bool left : { false, true }) {
		check(Query(true, 1, false, left, false, "poison", members) == !left, "1H poison right only");
		check(Query(true, 1, false, left, true, "", members) == false, "1H plain sibling absent");
	}
	members[0].wornLeft = true;
	for (bool left : { false, true }) {
		check(Query(true, 1, false, left, false, "poison", members) == !left, "same-form dual wield right row");
		check(Query(true, 1, false, left, true, "", members) == left, "same-form dual wield left row");
		check(Query(false, 1, false, left, true, "", members) == false, "different-form hand rejected");
	}
	members[1].worn = false;
	check(Query(true, 1, false, true, true, "", members) == true, "plain left only");
	check(Query(true, 1, false, false, true, "", members) == false, "plain left not right");
	members[0].wornLeft = false;
	members[0].worn = true;
	check(Query(true, 1, false, false, true, "", members) == true, "plain right only");
	check(Query(true, 1, false, true, true, "", members) == false, "plain right not left");
	check(!ResolveMixedTwoHanded(false, true, "", members), "missing/unreadable snapshot");
	check(!ResolveMixedTwoHanded(true, true, "", {}), "missing physical member/implicit quantity insufficient");
	check(!ResolveMixedTwoHanded(true, false, "", members), "missing stored signature");
	check(!ResolveMixedTwoHanded(true, false, "wrong", members), "FormID alone insufficient");
	check(Query(false, 1, true, false, true, "", members) == false, "FormID mismatch 2H");
	check(Query(true, -1, true, false, true, "", members) == false, "unknown topology rejects fallback");
	for (int index : { 0, 1 }) {
		members[index].readable = false;
		check(!ResolveMixedTwoHanded(true, true, "", members), "unreadable worn/metadata including unworn sibling");
		members[index].readable = true;
	}
	members[1].worn = true;
	check(!ResolveMixedTwoHanded(true, true, "", members), "two competing right-worn members");
	members[1].worn = false;
	members[1].wornLeft = true;
	check(!ResolveMixedTwoHanded(true, true, "", members), "competing opposite-hand member");
	members[1] = members[0];
	check(!ResolveMixedTwoHanded(true, true, "", members), "duplicate even equal-signature worn proof rejected");
	members[1] = plain;
	for (const auto flags : { std::array{ true, false }, std::array{ true, true }, std::array{ false, true }, std::array{ false, false } }) {
		members[0].worn = flags[0];
		members[0].wornLeft = flags[1];
		check(ResolveMixedTwoHanded(true, true, "", members) == (flags[0] || flags[1]), "one member worn truth table");
	}
	std::cout << "Hand indicator presentation: " << passed << " passed, " << failed << " failed\n";
	return failed ? 1 : 0;
}
