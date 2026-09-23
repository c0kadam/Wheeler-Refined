#include "WheelItems/WeaponPoisonPresentationPolicy.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <type_traits>

int main()
{
	using namespace WeaponPoisonPresentationPolicy;
	static_assert(std::is_trivially_copyable_v<MemberEvidence>);
	static_assert(std::is_trivially_copyable_v<WeaponPoisonPresentation>);
	int passed = 0, failed = 0;
	const auto check = [&](bool ok, const char* name) {
		(ok ? passed : failed)++;
		std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
	};
	const auto expect = [&](WeaponPoisonPresentation value, std::uint32_t form, std::uint32_t count, const char* name) {
		check(value.safelyResolved == (form != 0) && value.poisonFormID == form && value.poisonCount == count, name);
	};
	const MemberEvidence plain{ true, 12, 1, false, false, 0, 0 };
	const MemberEvidence poisoned{ true, 10, 1, true, true, 0x1234, 3 };
	std::array one{ plain };
	expect(Resolve(1, 12, true, one), 0, 0, "1 single plain");
	one[0] = poisoned;
	expect(Resolve(1, 10, true, one), 0x1234, 3, "2 single poison preserves form and usage count");
	std::array pair{ plain, poisoned };
	for (int order = 0; order < 2; ++order) {
		expect(Resolve(2, 10, true, pair), 0x1234, 3, "3/4 poisoned sibling, both list orders");
		expect(Resolve(2, 0, true, pair), 0, 0, "3/4 UID0 plain sibling never borrows poison");
		expect(Resolve(2, 12, true, pair), 0, 0, "3/4 exact plain UID never borrows poison");
		std::reverse(pair.begin(), pair.end());
	}
	pair[0] = { true, 12, 1, true, true, 0x5678, 1 };
	for (int order = 0; order < 2; ++order) {
		expect(Resolve(2, 10, true, pair), 0x1234, 3, "5 first distinct poison");
		expect(Resolve(2, 12, true, pair), 0x5678, 1, "5 second distinct poison");
		std::reverse(pair.begin(), pair.end());
	}
	pair[0].uniqueID = 10;
	expect(Resolve(2, 10, true, pair), 0, 0, "6 duplicate UID rejects even differing poisons");
	pair[0] = plain;
	expect(Resolve(2, 99, true, pair), 0, 0, "7 stale UID does not select first member");
	expect(Resolve(2, 0, true, one), 0, 0, "8 implicit sibling still blocks UID0");
	expect(Resolve(1, 0, true, one), 0x1234, 3, "9 UID0 single explicit poisoned member");
	expect(Resolve(1, 0, true, {}), 0, 0, "9 implicit-only cannot prove poison");
	expect(Resolve(1, 10, false, one), 0, 0, "10 unreadable enumeration");
	one[0].readable = false;
	expect(Resolve(1, 10, true, one), 0, 0, "11 ExtraPoison/UID probe or field read failure");
	one[0] = poisoned;
	one[0].poisonPointerValid = false;
	expect(Resolve(1, 10, true, one), 0, 0, "12 null poison pointer");
	one[0] = poisoned;
	one[0].poisonCount = 0;
	expect(Resolve(1, 10, true, one), 0, 0, "13 exhausted poison");
	one[0] = poisoned;
	one[0].poisonFormID = 0;
	expect(Resolve(1, 10, true, one), 0, 0, "14 invalid poison FormID");
	one[0] = poisoned;
	const auto valid = Resolve(1, 10, true, one);
	expect(ValidateForm(valid, false), 0, 0, "15 missing or non-Alchemy lookup");
	expect(ValidateForm(valid, true), 0x1234, 3, "15 valid current Alchemy lookup");
	expect(ValidateForm({}, true), 0, 0, "lookup alone cannot grant ownership");
	one[0].count = 2;
	expect(Resolve(2, 10, true, one), 0, 0, "exact UID member count must be one");
	expect(Resolve(1, 10, true, one), 0, 0, "over-represented inventory");
	one[0].count = 0;
	expect(Resolve(1, 10, true, one), 0, 0, "zero member count");
	one[0].count = -1;
	expect(Resolve(1, 10, true, one), 0, 0, "negative member count");
	one[0] = poisoned;
	expect(Resolve(0, 10, true, one), 0, 0, "empty inventory");
	expect(Resolve(-1, 10, true, one), 0, 0, "invalid inventory count");
	pair[0].count = (std::numeric_limits<int>::max)();
	expect(Resolve((std::numeric_limits<int>::max)(), 10, true, pair), 0, 0, "represented sum cannot overflow");
	pair[0] = plain;
	pair[0].readable = false;
	expect(Resolve(2, 10, true, pair), 0, 0, "unreadable sibling cannot hide duplicate UID");
	one[0].hasPoison = false;
	expect(Resolve(1, 10, true, one), 0, 0, "23 poison removed despite stale fields");
	one[0] = { true, 10, 1, true, true, 0x9876, 8 };
	expect(Resolve(1, 10, true, one), 0x9876, 8, "24 poison replacement is read fresh");
	one[0].uniqueID = 22;
	expect(Resolve(1, 10, true, one), 0, 0, "27 EDS stale UID fails closed even single copy");
	expect(Resolve(1, 22, true, one), 0x9876, 8, "27 externally updated UID resolves fresh member");
	expect(Resolve(2, 0, true, one), 0, 0, "28 alias-only grouped badge grants no tooltip authority");
	one[0].poisonFormID = 0xFF001234;
	expect(ValidateForm(Resolve(1, 22, true, one), true), 0xFF001234, 8, "crafted dynamic form needs no inventory bottle");
	check(AppendDescription("Enchantment", "Poison", "Effects") == "Enchantment\n\nPoison\nEffects", "16 poison coexists with enchantment");
	check(AppendDescription("Base\nEnchantment", "Poison", "Effects") == "Base\nEnchantment\n\nPoison\nEffects", "existing text/order preserved");
	check(AppendDescription("", "Poison", "Effects") == "Poison\nEffects", "standalone poison text");
	check(AppendDescription("", "Poison", "") == "Poison", "empty description keeps valid name");
	check(AppendDescription("Base", "", "Effects") == "Base\n\nEffects", "missing name permits effects");
	check(AppendDescription("Base", "", "") == "Base", "no poison text leaves base untouched");
	check(AppendDescription("", "Poison", "Effect 1\nEffect 2") == "Poison\nEffect 1\nEffect 2", "engine multi-effect text preserved without parsing");
	std::cout << passed << " passed, " << failed << " failed\n";
	return failed ? 1 : 0;
}
