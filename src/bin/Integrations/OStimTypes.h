#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class OStimActionKind : std::uint32_t
{
	None = 0,
	OpenControlWheel,
	OpenPositionBrowser,
	OpenPositionSubmenu,
	ReturnToPositionBrowserParent,
	ReturnToControlWheel,
	StopScene,
	NextStage,
	PreviousStage,
	NextPosition,
	PreviousPosition,
	IncreaseSpeed,
	DecreaseSpeed,
	SwapPartner,
	ChangeVariant,
	SelectSpecificPosition,
	ToggleAutoMode
};

enum class OStimSceneSemantic : std::uint32_t
{
	Unknown = 0,
	PositionIdle,
	Action,
	Undressing,
	Return,
	Transition
};

inline const char* GetOStimSceneSemanticName(OStimSceneSemantic a_semantic)
{
	switch (a_semantic) {
	case OStimSceneSemantic::PositionIdle:
		return "PositionIdle";
	case OStimSceneSemantic::Action:
		return "Action";
	case OStimSceneSemantic::Undressing:
		return "Undressing";
	case OStimSceneSemantic::Return:
		return "Return";
	case OStimSceneSemantic::Transition:
		return "Transition";
	default:
		return "Unknown";
	}
}

inline const char* GetOStimSceneSemanticLabel(OStimSceneSemantic a_semantic)
{
	switch (a_semantic) {
	case OStimSceneSemantic::PositionIdle:
		return "Position / Idle";
	case OStimSceneSemantic::Action:
		return "Action";
	case OStimSceneSemantic::Undressing:
		return "Undressing";
	case OStimSceneSemantic::Return:
		return "Return";
	case OStimSceneSemantic::Transition:
		return "Transition";
	default:
		return "OStim Scene";
	}
}

inline const char* GetOStimSceneSemanticHint(OStimSceneSemantic a_semantic)
{
	switch (a_semantic) {
	case OStimSceneSemantic::PositionIdle:
		return "Changes pose. This branch may remain idle.";
	case OStimSceneSemantic::Action:
		return "Scene action. May be part of a staged branch.";
	case OStimSceneSemantic::Undressing:
		return "Opens or performs an undressing-related scene.";
	case OStimSceneSemantic::Return:
		return "Returns toward the previous scene state.";
	case OStimSceneSemantic::Transition:
		return "Moves to another scene state.";
	default:
		return "";
	}
}

inline std::string BuildOStimSceneActionPresentationLabel(
	OStimSceneSemantic a_semantic,
	std::string_view a_nativeDescription)
{
	std::string label = "[";
	label += GetOStimSceneSemanticLabel(a_semantic);
	label += "]";
	if (!a_nativeDescription.empty()) {
		label += " ";
		label += a_nativeDescription;
	}
	return label;
}

inline std::string BuildOStimSceneSemanticGuidance(OStimSceneSemantic a_semantic)
{
	std::string guidance = GetOStimSceneSemanticLabel(a_semantic);
	const std::string_view hint = GetOStimSceneSemanticHint(a_semantic);
	if (!hint.empty()) {
		guidance += "\n";
		guidance += hint;
	}
	return guidance;
}

constexpr OStimActionKind GetOStimOneHopNavigationRowActionKind() noexcept
{
	return OStimActionKind::SelectSpecificPosition;
}

struct OStimParticipantInfo
{
	RE::FormID formID = 0;
	std::string name;
	bool isPlayer = false;
};

struct OStimPositionInfo
{
	std::string id;
	std::string displayName;
	std::string category;
	std::string subcategory;
	std::string sourceSceneID;
	std::string destinationID;
	std::string description;
	OStimSceneSemantic semantic = OStimSceneSemantic::Unknown;
	std::string previewPath;
	std::string iconPath;
	bool isValidNow = false;
	bool requiresActiveScene = true;
	bool isTransition = false;
};

struct OStimSceneInfo
{
	bool active = false;
	bool playerInvolved = false;
	bool aggressive = false;
	int apiVersion = 0;
	int participantCount = 0;
	int currentSpeed = 0;
	int maxSpeed = 0;
	int currentOID = 0;
	std::uint32_t threadID = 0;
	bool inTransition = false;
	bool inSequence = false;
	bool playerControlDisabled = false;
	bool autoMode = false;
	std::string animationID;
	std::string animationName;
	std::string animationClass;
	std::string sceneID;
	std::string positionData;
	std::string sourceModule;
	std::vector<std::string> metadata;
	std::vector<OStimParticipantInfo> participants;
};

struct OStimActionPayload
{
	OStimActionKind kind = OStimActionKind::None;
	std::string sceneID;
	std::string positionID;
	std::string sourceSceneID;
	std::string displayName;
	OStimSceneSemantic semantic = OStimSceneSemantic::Unknown;
	std::string previewPath;
	std::string iconPath;
	std::string category;
	std::string subcategory;
	bool requiresActiveScene = true;
	std::uint32_t browserPage = 0;
	std::int32_t browserFocusIndex = -1;
	std::uint64_t wheelLayoutRevision = 0;
};

enum class OStimNavigationSelectionStatus : std::uint8_t
{
	Ready = 0,
	SceneInactive,
	SourceSceneMismatch,
	SceneInTransition,
	SceneInSequence,
	PlayerControlDisabled,
	TargetMissing
};

inline OStimNavigationSelectionStatus ValidateOStimNavigationSelection(
	const std::optional<OStimSceneInfo>& a_scene,
	const std::vector<OStimPositionInfo>& a_positions,
	const OStimActionPayload& a_payload)
{
	if (!a_scene || !a_scene->active) {
		return OStimNavigationSelectionStatus::SceneInactive;
	}
	if (a_payload.sourceSceneID.empty() || a_payload.sourceSceneID != a_scene->sceneID) {
		return OStimNavigationSelectionStatus::SourceSceneMismatch;
	}
	if (a_scene->inTransition) {
		return OStimNavigationSelectionStatus::SceneInTransition;
	}
	if (a_scene->inSequence) {
		return OStimNavigationSelectionStatus::SceneInSequence;
	}
	if (a_scene->playerControlDisabled) {
		return OStimNavigationSelectionStatus::PlayerControlDisabled;
	}

	for (const auto& position : a_positions) {
		if (position.isValidNow &&
			position.id == a_payload.sceneID &&
			position.sourceSceneID == a_scene->sceneID) {
			return OStimNavigationSelectionStatus::Ready;
		}
	}
	return OStimNavigationSelectionStatus::TargetMissing;
}

inline const char* GetOStimNavigationSelectionStatusName(OStimNavigationSelectionStatus a_status)
{
	switch (a_status) {
	case OStimNavigationSelectionStatus::Ready:
		return "Ready";
	case OStimNavigationSelectionStatus::SceneInactive:
		return "SceneInactive";
	case OStimNavigationSelectionStatus::SourceSceneMismatch:
		return "SourceSceneMismatch";
	case OStimNavigationSelectionStatus::SceneInTransition:
		return "SceneInTransition";
	case OStimNavigationSelectionStatus::SceneInSequence:
		return "SceneInSequence";
	case OStimNavigationSelectionStatus::PlayerControlDisabled:
		return "PlayerControlDisabled";
	case OStimNavigationSelectionStatus::TargetMissing:
		return "TargetMissing";
	default:
		return "Unknown";
	}
}

inline const std::string& GetOStimNavigationPresentationID(const OStimPositionInfo& a_position)
{
	return a_position.destinationID.empty() ? a_position.id : a_position.destinationID;
}

struct OStimAvailabilityInfo
{
	bool available = false;
	bool hasDatabase = false;
	bool hasNativeThreadAPI = false;
	int apiVersion = 0;
	std::string reason;
};
