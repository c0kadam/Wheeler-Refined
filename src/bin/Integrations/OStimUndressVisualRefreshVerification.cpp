#include "OStimUndressVisualRefresh.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using namespace OStimUndressVisualRefreshPolicy;

	bool Expect(bool a_condition, std::string_view a_case)
	{
		if (a_condition) {
			std::cout << "PASS " << a_case << '\n';
			return true;
		}
		std::cerr << "FAIL " << a_case << '\n';
		return false;
	}

	std::size_t CountOccurrences(const std::string& a_text, std::string_view a_needle)
	{
		std::size_t count = 0;
		std::size_t offset = 0;
		while ((offset = a_text.find(a_needle, offset)) != std::string::npos) {
			++count;
			offset += a_needle.size();
		}
		return count;
	}
}

int main(int a_argc, char** a_argv)
{
	bool ok = true;
	constexpr FormID kClothes = 0x000209A6;
	constexpr FormID kBoots = 0x000209A5;
	constexpr FormID kNoLight = 0xFE04DEB9;
	const WornArmorSet clothed{ kBoots, kClothes };
	const WornArmorSet empty;

	const auto unchanged = GetRemovedWornArmor(clothed, clothed);
	ok &= Expect(
		unchanged.empty() && !ShouldArmRefresh(true, false, unchanged.size()),
		"case 1 unchanged Clothes+Boots does not refresh");

	const auto fullyRemoved = GetRemovedWornArmor(clothed, empty);
	ok &= Expect(
		fullyRemoved == clothed && ShouldArmRefresh(true, false, fullyRemoved.size()),
		"case 2 Clothes+Boots removal arms one refresh");

	const auto beforeWithHelper = BuildWornArmorSet({
		{ kClothes, 0x00000004, true },
		{ kBoots, 0x00000080, true },
		{ kNoLight, 0x00000000, true }
	});
	const auto afterWithHelper = BuildWornArmorSet({
		{ kNoLight, 0x00000000, true }
	});
	const auto removedWithHelper = GetRemovedWornArmor(beforeWithHelper, afterWithHelper);
	ok &= Expect(
		beforeWithHelper == clothed && afterWithHelper.empty() &&
		removedWithHelper == clothed && ShouldArmRefresh(true, false, removedWithHelper.size()),
		"case 3 slotMask=0 helper armor is excluded and real clothing removal arms once");

	const auto emptyToEmpty = GetRemovedWornArmor(empty, empty);
	ok &= Expect(
		emptyToEmpty.empty() && !ShouldArmRefresh(true, false, emptyToEmpty.size()),
		"case 4 empty to empty does not refresh");

	const auto clothingAppeared = GetRemovedWornArmor(empty, clothed);
	ok &= Expect(
		clothingAppeared.empty() && !ShouldArmRefresh(true, false, clothingAppeared.size()),
		"case 5 armor appearing does not refresh");

	const RefreshKey firstObservation{ 0x14, 77 };
	const RefreshKey duplicateObservation{ 0x14, 77 };
	ok &= Expect(
		firstObservation == duplicateObservation &&
		!ShouldArmRefresh(true, true, fullyRemoved.size()),
		"case 6 duplicate actor+event revision cannot arm twice");

	ExecutionFacts execution{
		true,
		true,
		false,
		true,
		true,
		true,
		true,
		true
	};
	ok &= Expect(
		EvaluateExecution(execution) == ExecutionStatus::GenerationChanged,
		"case 7 generation change cancels delayed execution");

	ok &= Expect(
		!ShouldArmRefresh(false, false, fullyRemoved.size()),
		"case 8 setting OFF never arms a refresh");

	ok &= Expect(
		EvaluateConfigTransition(false, true, true) == ConfigTransition::CaptureBaseline &&
		GetRemovedWornArmor(clothed, clothed).empty(),
		"case 9 enabling mid-scene captures current state without a false removal");

	execution.generationMatches = true;
	execution.actorIsParticipant = false;
	ok &= Expect(
		EvaluateExecution(execution) == ExecutionStatus::ActorNotParticipant,
		"case 10 participant removal cancels safely before actor mutation");

	ok &= Expect(kRefreshDelayMs == 250, "appearance refresh delay is exactly 250 ms");

	if (a_argc < 2) {
		std::cerr << "FAIL undress-refresh implementation source path was not supplied\n";
		ok = false;
	} else {
		std::ifstream input(a_argv[1], std::ios::binary);
		const std::string source{
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>()
		};
		const std::vector<std::string_view> forbidden{
			"UnequipItem",
			"EquipItem",
			"ActorEquipManager",
			"OUndress",
			"IdleForceDefaultState",
			"ReturnToDefault",
			"NavigateToScene",
			"NavigateToSearchResult",
			"NotifyAnimationGraph",
			"std::thread",
			"Sleep("
		};
		bool forbiddenAbsent = true;
		for (const auto token : forbidden) {
			forbiddenAbsent &= source.find(token) == std::string::npos;
		}
		ok &= Expect(
			forbiddenAbsent && CountOccurrences(source, "actor->Update3DModel();") == 1,
			"component contains one native model refresh and no forbidden mutation/navigation/thread calls");
		ok &= Expect(
			source.find("RuntimeEvent::NodeChanged") != std::string::npos &&
			source.find("GetLastEvent()") != std::string::npos &&
			source.find("QueueSceneAction") == std::string::npos &&
			source.find("NAV_DISPATCH_ACCEPTED") == std::string::npos,
			"detection is native NodeChanged-driven and independent of Wheeler dispatch");
		ok &= Expect(
			source.find("slotMask != 0") == std::string::npos &&
			source.find("BuildWornArmorSet(observations)") != std::string::npos &&
			source.find("GetRemovedWornArmor") != std::string::npos,
			"runtime uses the shared non-zero-slot removal policy");
	}

	std::cout << (ok ? "OStim undress visual refresh verification PASSED\n" :
		"OStim undress visual refresh verification FAILED\n");
	return ok ? 0 : 1;
}
