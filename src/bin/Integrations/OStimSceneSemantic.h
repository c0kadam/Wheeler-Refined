#pragma once

#include "OStimTypes.h"

#include "nlohmann/json.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

enum class OStimSceneSemanticReason : std::uint32_t
{
	MetadataUnavailable = 0,
	NoSupportedSignal,
	TransitionFlag,
	TagUndressing,
	TagIdle,
	ActionsPresent,
	NavigationReturnIcon
};

struct OStimSceneSemanticMetadata
{
	OStimSceneSemantic semantic = OStimSceneSemantic::Unknown;
	bool metadataFound = false;
	OStimSceneSemanticReason reason = OStimSceneSemanticReason::MetadataUnavailable;
};

inline const char* GetOStimSceneSemanticReasonName(OStimSceneSemanticReason a_reason)
{
	switch (a_reason) {
	case OStimSceneSemanticReason::NoSupportedSignal:
		return "NoSupportedSignal";
	case OStimSceneSemanticReason::TransitionFlag:
		return "TransitionFlag";
	case OStimSceneSemanticReason::TagUndressing:
		return "TagUndressing";
	case OStimSceneSemanticReason::TagIdle:
		return "TagIdle";
	case OStimSceneSemanticReason::ActionsPresent:
		return "ActionsPresent";
	case OStimSceneSemanticReason::NavigationReturnIcon:
		return "NavigationReturnIcon";
	default:
		return "MetadataUnavailable";
	}
}

inline std::string NormalizeOStimSemanticToken(std::string_view a_value)
{
	std::string normalized;
	normalized.reserve(a_value.size());
	for (char value : a_value) {
		const char lowered = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
		normalized.push_back(lowered == '\\' ? '/' : lowered);
	}
	return normalized;
}

inline bool OStimSceneMetadataHasTag(
	const nlohmann::json& a_sceneMetadata,
	std::string_view a_expectedTag)
{
	if (!a_sceneMetadata.is_object() ||
		!a_sceneMetadata.contains("tags") ||
		!a_sceneMetadata["tags"].is_array()) {
		return false;
	}

	const std::string expected = NormalizeOStimSemanticToken(a_expectedTag);
	for (const auto& tag : a_sceneMetadata["tags"]) {
		if (tag.is_string() &&
			NormalizeOStimSemanticToken(tag.get<std::string>()) == expected) {
			return true;
		}
	}
	return false;
}

inline std::optional<nlohmann::json> TryParseOStimSceneJson(std::string_view a_jsonText)
{
	nlohmann::json parsed = nlohmann::json::parse(a_jsonText, nullptr, false);
	if (parsed.is_discarded()) {
		return std::nullopt;
	}
	return parsed;
}

inline OStimSceneSemanticMetadata ClassifyOStimSceneMetadata(
	const nlohmann::json* a_sceneMetadata)
{
	if (!a_sceneMetadata || !a_sceneMetadata->is_object()) {
		return {};
	}

	if (OStimSceneMetadataHasTag(*a_sceneMetadata, "undressing")) {
		return {
			OStimSceneSemantic::Undressing,
			true,
			OStimSceneSemanticReason::TagUndressing
		};
	}
	if (OStimSceneMetadataHasTag(*a_sceneMetadata, "idle")) {
		return {
			OStimSceneSemantic::PositionIdle,
			true,
			OStimSceneSemanticReason::TagIdle
		};
	}
	if (a_sceneMetadata->contains("actions") &&
		(*a_sceneMetadata)["actions"].is_array() &&
		!(*a_sceneMetadata)["actions"].empty()) {
		return {
			OStimSceneSemantic::Action,
			true,
			OStimSceneSemanticReason::ActionsPresent
		};
	}

	return {
		OStimSceneSemantic::Unknown,
		true,
		OStimSceneSemanticReason::NoSupportedSignal
	};
}

inline bool IsCanonicalOStimReturnIcon(std::string_view a_icon)
{
	return NormalizeOStimSemanticToken(a_icon) == "ostim/symbols/return";
}

inline OStimSceneSemanticMetadata ClassifyOStimNavigationSemantic(
	const OStimSceneSemanticMetadata& a_sceneMetadata,
	bool a_isTransition,
	bool a_hasCanonicalReturnIcon)
{
	if (a_isTransition) {
		return {
			OStimSceneSemantic::Transition,
			a_sceneMetadata.metadataFound,
			OStimSceneSemanticReason::TransitionFlag
		};
	}
	if (a_sceneMetadata.semantic != OStimSceneSemantic::Unknown) {
		return a_sceneMetadata;
	}
	if (a_hasCanonicalReturnIcon) {
		return {
			OStimSceneSemantic::Return,
			a_sceneMetadata.metadataFound,
			OStimSceneSemanticReason::NavigationReturnIcon
		};
	}
	return a_sceneMetadata;
}
