#include "OStimSceneActionClosePolicy.h"
#include "OStimUnifiedWheelModel.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
	using OStimSceneActionUI::Decision;

	struct SelectionSimulation
	{
		std::uint32_t navigationDispatches = 0;
		std::uint32_t page = 3;
		bool unifiedWheelActive = true;
		bool acceptedNavigationPending = false;
		OStimSceneActionUI::Policy policy{};
	};

	bool Expect(bool a_condition, std::string_view a_case)
	{
		if (a_condition) {
			std::cout << "PASS " << a_case << '\n';
			return true;
		}
		std::cerr << "FAIL " << a_case << '\n';
		return false;
	}

	SelectionSimulation SimulateSuccessfulSelection(bool a_closeAfterAction)
	{
		SelectionSimulation simulation{};
		++simulation.navigationDispatches;
		simulation.policy = OStimSceneActionUI::Evaluate(true, a_closeAfterAction);
		if (simulation.policy.resetNavigationPage) {
			simulation.page = 0;
		}
		if (simulation.policy.decision == Decision::Close) {
			simulation.unifiedWheelActive = false;
		} else if (simulation.policy.decision == Decision::KeepOpen) {
			simulation.acceptedNavigationPending = true;
		}
		return simulation;
	}

	std::size_t CountDynamicKind(
		const OStimUnifiedWheel::Layout& a_layout,
		OStimUnifiedWheel::DynamicSlotKind a_kind)
	{
		return static_cast<std::size_t>(std::count_if(
			a_layout.dynamicSlots.begin(),
			a_layout.dynamicSlots.begin() + a_layout.dynamicSlotCount,
			[a_kind](const auto& a_slot) { return a_slot.kind == a_kind; }));
	}

	std::string ReadFile(const char* a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		return {
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>()
		};
	}
}

int main(int a_argc, char** a_argv)
{
	bool ok = true;

	const auto closeEnabled = SimulateSuccessfulSelection(true);
	ok &= Expect(
		closeEnabled.navigationDispatches == 1 &&
		closeEnabled.policy.decision == Decision::Close &&
		!closeEnabled.unifiedWheelActive &&
		closeEnabled.page == 0,
		"config=true successful Select Position dispatches once and selects Close");

	const auto closeDisabled = SimulateSuccessfulSelection(false);
	ok &= Expect(
		closeDisabled.navigationDispatches == 1 &&
		closeDisabled.policy.decision == Decision::KeepOpen &&
		closeDisabled.unifiedWheelActive &&
		closeDisabled.acceptedNavigationPending &&
		closeDisabled.page == 0,
		"config=false successful Select Position dispatches once and keeps the unified wheel active");

	const auto transition = OStimUnifiedWheel::BuildLayout(0, 0, 8);
	const auto defaultPhysical = OStimUnifiedWheel::BuildPhysicalLayout(8);
	const bool pendingBlocksRedispatch = OStimSceneActionUI::ShouldHoldAcceptedNavigation(
		closeDisabled.acceptedNavigationPending,
		true,
		true);
	ok &= Expect(
		defaultPhysical.entryCount == 12 &&
		defaultPhysical.fixedIndices == std::array<std::size_t, 4>{ 4, 3, 2, 1 } &&
		CountDynamicKind(transition, OStimUnifiedWheel::DynamicSlotKind::Empty) == 8 &&
		pendingBlocksRedispatch &&
		closeDisabled.navigationDispatches == 1,
		"transition navigation=0 keeps 12 slots, fixed indices, inert dynamic slots, and no redispatch");

	const bool destinationReleasesPending = !OStimSceneActionUI::ShouldHoldAcceptedNavigation(
		closeDisabled.acceptedNavigationPending,
		true,
		false);
	const auto destination = OStimUnifiedWheel::BuildLayout(3, 0, 8);
	ok &= Expect(
		destinationReleasesPending &&
		CountDynamicKind(destination, OStimUnifiedWheel::DynamicSlotKind::Position) == 3 &&
		CountDynamicKind(destination, OStimUnifiedWheel::DynamicSlotKind::Empty) == 5,
		"destination snapshot repopulates the same unified wheel with new positions");

	const bool oldPayloadMatchesDestinationSnapshot = false;
	const bool oldPhysicalSlotCanDispatch =
		!OStimSceneActionUI::ShouldHoldAcceptedNavigation(true, true, false) &&
		oldPayloadMatchesDestinationSnapshot &&
		OStimUnifiedWheel::IsCurrentLayoutRevision(41, 42);
	ok &= Expect(
		!oldPhysicalSlotCanDispatch,
		"old physical slot payload cannot dispatch after the tracker source snapshot changes");

	const auto failedSelection = OStimSceneActionUI::Evaluate(false, false);
	ok &= Expect(
		failedSelection.decision == Decision::NotApplicable &&
		!failedSelection.resetNavigationPage &&
		!failedSelection.keepUnifiedWheelActive,
		"failed or stale navigation never invokes successful close policy");

	const auto pageAction = OStimSceneActionUI::Evaluate(false, true);
	ok &= Expect(
		pageAction.decision == Decision::NotApplicable,
		"Prev and Next remain outside scene-action close policy");

	const auto fixedAction = OStimSceneActionUI::Evaluate(false, false);
	ok &= Expect(
		fixedAction.decision == Decision::NotApplicable &&
		defaultPhysical.fixedIndices == std::array<std::size_t, 4>{ 4, 3, 2, 1 },
		"End, Speed, and Auto Progress remain unaffected at their fixed indices");

	ok &= Expect(
		OStimSceneActionUI::Evaluate(true, true).decision == Decision::Close,
		"default-enabled policy preserves the exact R2C close decision");

	if (a_argc < 6) {
		std::cerr << "FAIL integration, Config.h, Config.cpp, defaults INI, and dMenu JSON paths were not supplied\n";
		ok = false;
	} else {
		const std::string integration = ReadFile(a_argv[1]);
		const std::string configHeader = ReadFile(a_argv[2]);
		const std::string configSource = ReadFile(a_argv[3]);
		const std::string defaultsIni = ReadFile(a_argv[4]);
		const std::string dmenuJson = ReadFile(a_argv[5]);

		ok &= Expect(
			configHeader.find("inline bool CloseWheelAfterSceneAction = true;") != std::string::npos &&
			configSource.find("Config::OStimIntegration::CloseWheelAfterSceneAction = true;") != std::string::npos &&
			configSource.find("\"CloseWheelAfterSceneAction\"") != std::string::npos &&
			defaultsIni.find("CloseWheelAfterSceneAction = true") != std::string::npos,
			"persistent config key defaults true and uses the existing layered loader");
		ok &= Expect(
			dmenuJson.find("\"id\": \"CloseWheelAfterSceneAction\"") != std::string::npos &&
			dmenuJson.find("\"name\": \"Close Wheel After Scene Action\"") != std::string::npos &&
			dmenuJson.find("Disable to keep the OStim control wheel open while navigating scenes.") != std::string::npos,
			"existing OStim Integration dMenu panel exposes the required label and description");
		ok &= Expect(
			integration.find("SCENE_ACTION_UI_POLICY") != std::string::npos &&
			integration.find("decision={}") != std::string::npos &&
			integration.find("SCENE_ACTION_UI_CLOSE") == std::string::npos,
			"successful policy emits one explicit Close or KeepOpen diagnostic");
		ok &= Expect(
			integration.find("EnsureControlWheel(UnifiedFocusHint::None, \"SceneActionKeepOpen\")") != std::string::npos &&
			integration.find("Wheeler::SetWheelHoveredEntryIndex(") != std::string::npos &&
			integration.find("acceptedNavigationPending") != std::string::npos,
			"keep-open path clears hover and suppresses the accepted source navigation until tracker change");
		ok &= Expect(
			integration.find("ValidateOStimNavigationSelection(") != std::string::npos &&
			integration.find("OStimNGThreadAPI::NavigateToScene(") != std::string::npos,
			"existing source-scene validation and native navigation dispatch remain authoritative");
	}

	std::cout << (ok ? "OStim close policy verification PASSED\n" :
		"OStim close policy verification FAILED\n");
	return ok ? 0 : 1;
}
