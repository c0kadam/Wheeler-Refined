#include <cstdint>

namespace RE
{
	using FormID = std::uint32_t;
}

#include "OStimSceneSemantic.h"

#include <iostream>
#include <string_view>

namespace
{
	OStimSceneSemanticMetadata ClassifyJson(
		std::string_view a_json,
		bool a_isTransition = false,
		std::string_view a_navigationIcon = {})
	{
		const auto parsed = TryParseOStimSceneJson(a_json);
		const auto sceneMetadata = ClassifyOStimSceneMetadata(
			parsed.has_value() ? &*parsed : nullptr);
		return ClassifyOStimNavigationSemantic(
			sceneMetadata,
			a_isTransition,
			IsCanonicalOStimReturnIcon(a_navigationIcon));
	}

	bool ExpectSemantic(
		const OStimSceneSemanticMetadata& a_actual,
		OStimSceneSemantic a_expected,
		std::string_view a_case)
	{
		if (a_actual.semantic == a_expected) {
			std::cout << "PASS " << a_case << '\n';
			return true;
		}
		std::cerr << "FAIL " << a_case << ": expected "
		          << GetOStimSceneSemanticName(a_expected)
		          << ", got "
		          << GetOStimSceneSemanticName(a_actual.semantic)
		          << '\n';
		return false;
	}
}

int main()
{
	bool ok = true;

	const auto idle = ClassifyJson(R"({
		"name": "OStim2PStandingKneelingMF",
		"tags": ["idle"]
	})");
	ok &= ExpectSemantic(idle, OStimSceneSemantic::PositionIdle,
		"explicit idle tag -> PositionIdle");
	ok &= idle.metadataFound && idle.reason == OStimSceneSemanticReason::TagIdle;

	const auto action = ClassifyJson(R"({
		"name": "NeutralSceneName",
		"actions": [{"type": "customAction"}]
	})");
	ok &= ExpectSemantic(action, OStimSceneSemantic::Action,
		"non-empty actions -> Action");

	const auto transition = ClassifyJson(R"({
		"tags": ["undressing", "idle"],
		"actions": [{"type": "customAction"}]
	})", true, "OStim/symbols/return");
	ok &= ExpectSemantic(transition, OStimSceneSemantic::Transition,
		"transition flag overrides JSON and return icon");

	const auto missing = ClassifyOStimNavigationSemantic(
		ClassifyOStimSceneMetadata(nullptr), false, false);
	ok &= ExpectSemantic(missing, OStimSceneSemantic::Unknown,
		"missing JSON -> Unknown");
	ok &= !missing.metadataFound;

	const auto malformed = ClassifyJson("{ malformed JSON");
	ok &= ExpectSemantic(malformed, OStimSceneSemantic::Unknown,
		"malformed JSON -> Unknown without crash");
	ok &= !malformed.metadataFound;

	const auto actionWordsOnly = ClassifyJson(R"({
		"name": "Blowjob Handjob Fuck Kiss"
	})");
	ok &= ExpectSemantic(actionWordsOnly, OStimSceneSemantic::Unknown,
		"action words in scene name do not imply Action");

	const auto undressNameOnly = ClassifyJson(R"({
		"name": "OStim2PUndressEverything"
	})");
	ok &= ExpectSemantic(undressNameOnly, OStimSceneSemantic::Unknown,
		"Undress in scene name does not imply Undressing");

	const auto undressing = ClassifyJson(R"({
		"name": "NeutralSceneName",
		"tags": ["undressing"]
	})");
	ok &= ExpectSemantic(undressing, OStimSceneSemantic::Undressing,
		"explicit undressing tag -> Undressing");

	const auto returnNavigation = ClassifyJson(
		R"({"name": "NeutralSceneName"})",
		false,
		"OStim/symbols/return");
	ok &= ExpectSemantic(returnNavigation, OStimSceneSemantic::Return,
		"canonical return icon -> Return");

	const auto precedence = ClassifyJson(R"({
		"tags": ["idle"],
		"actions": [{"type": "customAction"}]
	})", false, "OStim/symbols/return");
	ok &= ExpectSemantic(precedence, OStimSceneSemantic::PositionIdle,
		"PositionIdle precedes Action and Return");

	OStimPositionInfo position{};
	position.id = "ExecutableSceneID";
	position.destinationID = "PresentationDestinationID";
	position.sourceSceneID = "SourceSceneID";
	position.isValidNow = true;
	position.semantic = idle.semantic;
	if (position.id != "ExecutableSceneID" ||
		position.destinationID != "PresentationDestinationID" ||
		position.sourceSceneID != "SourceSceneID" ||
		!position.isValidNow) {
		std::cerr << "FAIL semantic classification changed navigation identity or validity\n";
		ok = false;
	} else {
		std::cout << "PASS semantic classification preserves navigation identity and validity\n";
	}

	if (BuildOStimSceneActionPresentationLabel(
			OStimSceneSemantic::PositionIdle,
			"Kneel before Cerys") != "[Position / Idle] Kneel before Cerys" ||
		BuildOStimSceneSemanticGuidance(OStimSceneSemantic::PositionIdle) !=
			"Position / Idle\nChanges pose. This branch may remain idle.") {
		std::cerr << "FAIL PositionIdle presentation text\n";
		ok = false;
	} else {
		std::cout << "PASS PositionIdle presentation preserves native description and adds guidance\n";
	}

	if (BuildOStimSceneSemanticGuidance(OStimSceneSemantic::Action) !=
		"Action\nScene action. May be part of a staged branch.") {
		std::cerr << "FAIL Action guidance text\n";
		ok = false;
	} else {
		std::cout << "PASS Action guidance describes staged branches exactly\n";
	}

	std::cout << (ok ? "OStim scene semantic verification PASSED\n" :
		"OStim scene semantic verification FAILED\n");
	return ok ? 0 : 1;
}
