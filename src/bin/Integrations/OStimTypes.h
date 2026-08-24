#pragma once

#include <cstdint>
#include <optional>
#include <string>
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
	SelectSpecificPosition
};

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
	std::string previewPath;
	std::string iconPath;
	std::string category;
	std::string subcategory;
	bool requiresActiveScene = true;
	std::uint32_t browserPage = 0;
	std::int32_t browserFocusIndex = -1;
};

struct OStimAvailabilityInfo
{
	bool available = false;
	bool hasDatabase = false;
	bool hasNativeThreadAPI = false;
	int apiVersion = 0;
	std::string reason;
};
