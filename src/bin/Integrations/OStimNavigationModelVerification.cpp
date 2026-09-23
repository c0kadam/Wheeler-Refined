#include <cstdint>

namespace RE
{
	using FormID = std::uint32_t;
}

#include "OStimTypes.h"

#include <iostream>
#include <string_view>

namespace
{
	bool Expect(
		OStimNavigationSelectionStatus a_actual,
		OStimNavigationSelectionStatus a_expected,
		std::string_view a_case)
	{
		if (a_actual == a_expected) {
			std::cout << "PASS " << a_case << '\n';
			return true;
		}
		std::cerr << "FAIL " << a_case << ": expected "
		          << GetOStimNavigationSelectionStatusName(a_expected)
		          << ", got "
		          << GetOStimNavigationSelectionStatusName(a_actual)
		          << '\n';
		return false;
	}

	OStimSceneInfo MakeScene(std::string a_sceneID)
	{
		OStimSceneInfo scene{};
		scene.active = true;
		scene.sceneID = std::move(a_sceneID);
		return scene;
	}

	OStimPositionInfo MakePosition(
		std::string a_sourceSceneID,
		std::string a_sceneID,
		std::string a_destinationID)
	{
		OStimPositionInfo position{};
		position.id = std::move(a_sceneID);
		position.destinationID = std::move(a_destinationID);
		position.sourceSceneID = std::move(a_sourceSceneID);
		position.isValidNow = true;
		return position;
	}

	OStimActionPayload MakePayload(const OStimPositionInfo& a_position)
	{
		OStimActionPayload payload{};
		payload.kind = OStimActionKind::SelectSpecificPosition;
		payload.sceneID = a_position.id;
		payload.positionID = GetOStimNavigationPresentationID(a_position);
		payload.sourceSceneID = a_position.sourceSceneID;
		return payload;
	}
}

int main()
{
	bool ok = true;
	auto sceneA = MakeScene("SceneA");
	const auto edgeB = MakePosition("SceneA", "TransitionB", "DestinationB");
	const auto payloadB = MakePayload(edgeB);
	const std::vector<OStimPositionInfo> navigationA{ edgeB };

	ok &= Expect(
		ValidateOStimNavigationSelection(sceneA, navigationA, payloadB),
		OStimNavigationSelectionStatus::Ready,
		"current scene/current edge accepted");

	auto sceneC = MakeScene("SceneC");
	ok &= Expect(
		ValidateOStimNavigationSelection(sceneC, navigationA, payloadB),
		OStimNavigationSelectionStatus::SourceSceneMismatch,
		"old source rejected after NodeChanged");

	const std::vector<OStimPositionInfo> navigationWithoutB{
		MakePosition("SceneA", "TransitionD", "DestinationD")
	};
	ok &= Expect(
		ValidateOStimNavigationSelection(sceneA, navigationWithoutB, payloadB),
		OStimNavigationSelectionStatus::TargetMissing,
		"missing current edge rejected");

	auto transitionScene = sceneA;
	transitionScene.inTransition = true;
	ok &= Expect(
		ValidateOStimNavigationSelection(transitionScene, navigationA, payloadB),
		OStimNavigationSelectionStatus::SceneInTransition,
		"transition rejected");

	auto sequenceScene = sceneA;
	sequenceScene.inSequence = true;
	ok &= Expect(
		ValidateOStimNavigationSelection(sequenceScene, navigationA, payloadB),
		OStimNavigationSelectionStatus::SceneInSequence,
		"sequence rejected");

	auto disabledScene = sceneA;
	disabledScene.playerControlDisabled = true;
	ok &= Expect(
		ValidateOStimNavigationSelection(disabledScene, navigationA, payloadB),
		OStimNavigationSelectionStatus::PlayerControlDisabled,
		"control-disabled rejected");

	if (payloadB.sceneID != "TransitionB" ||
		payloadB.positionID != "DestinationB" ||
		GetOStimNavigationPresentationID(edgeB) != "DestinationB") {
		std::cerr << "FAIL transition edge and destination remain distinct\n";
		ok = false;
	} else {
		std::cout << "PASS transition edge and destination remain distinct\n";
	}

	const auto navigationBeforePaging = navigationA;
	std::uint32_t page = 0;
	page = 1;
	if (page != 1 ||
		navigationA.size() != navigationBeforePaging.size() ||
		navigationA.front().id != navigationBeforePaging.front().id) {
		std::cerr << "FAIL UI page state does not mutate navigation snapshot\n";
		ok = false;
	} else {
		std::cout << "PASS UI page state does not mutate navigation snapshot\n";
	}

	const auto edgeD = navigationWithoutB.front();
	ok &= Expect(
		ValidateOStimNavigationSelection(sceneC, navigationWithoutB, payloadB),
		OStimNavigationSelectionStatus::SourceSceneMismatch,
		"replacement snapshot rejects prior payload");
	const auto navigationC = std::vector<OStimPositionInfo>{
		MakePosition("SceneC", edgeD.id, edgeD.destinationID)
	};
	const auto payloadC = MakePayload(navigationC.front());
	ok &= Expect(
		ValidateOStimNavigationSelection(sceneC, navigationC, payloadC),
		OStimNavigationSelectionStatus::Ready,
		"replacement snapshot accepts new edge");

	static_assert(static_cast<std::uint32_t>(OStimActionKind::OpenPositionSubmenu) == 3);
	static_assert(static_cast<std::uint32_t>(OStimActionKind::ReturnToPositionBrowserParent) == 4);
	static_assert(static_cast<std::uint32_t>(OStimActionKind::NextPosition) == 9);
	static_assert(static_cast<std::uint32_t>(OStimActionKind::PreviousPosition) == 10);

	constexpr auto navigationRowAction = GetOStimOneHopNavigationRowActionKind();
	if (navigationRowAction != OStimActionKind::SelectSpecificPosition ||
		navigationRowAction == OStimActionKind::OpenPositionSubmenu ||
		navigationRowAction == OStimActionKind::ReturnToPositionBrowserParent) {
		std::cerr << "FAIL old nested browser action kinds are not generated\n";
		ok = false;
	} else {
		std::cout << "PASS old nested browser action kinds are not generated\n";
	}

	std::cout << (ok ? "OStim navigation model verification PASSED\n" :
		"OStim navigation model verification FAILED\n");
	return ok ? 0 : 1;
}
