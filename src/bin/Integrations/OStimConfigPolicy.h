#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace OStimConfigPolicy
{
	inline constexpr std::uint32_t kMinSceneActionsPerPage = 4;
	inline constexpr std::uint32_t kDefaultSceneActionsPerPage = 8;
	inline constexpr std::uint32_t kMaxSceneActionsPerPage = 16;

	constexpr std::uint32_t ClampSceneActionsPerPage(std::uint32_t a_value) noexcept
	{
		return a_value < kMinSceneActionsPerPage ?
			kMinSceneActionsPerPage :
			(a_value > kMaxSceneActionsPerPage ? kMaxSceneActionsPerPage : a_value);
	}

	struct SettingContract
	{
		std::string_view key;
		std::string_view runtimeConsumer;
	};

	inline constexpr std::array<SettingContract, 25> kPublicSettings{
		SettingContract{ "Enabled", "OStimIntegration enable gate" },
		SettingContract{ "AutoDetect", "disabled-state OStimStateTracker polling gate" },
		SettingContract{ "CreateManagedWheel", "managed OStim wheel lifecycle gate" },
		SettingContract{ "AutoSwitchToSceneWheel", "scene-start managed-wheel activation" },
		SettingContract{ "RestorePreviousWheelOnSceneEnd", "scene-end wheel restoration" },
		SettingContract{ "CloseWheelAfterSceneAction", "accepted scene-action close policy" },
		SettingContract{ "RefreshAppearanceAfterUndress", "event-driven delayed actor model refresh after proven worn-armor removal" },
		SettingContract{ "AllowPositionBrowsing", "native navigation snapshot and dynamic scene-action gate" },
		SettingContract{ "ShowPositionPreviews", "scene-action preview resolver gate" },
		SettingContract{ "RestrictRegularWheelActionsDuringScenes", "non-OStim activation guard" },
		SettingContract{ "UseResourcePreviewFallback", "missing-metadata preview fallback resolver" },
		SettingContract{ "DebugLog", "OStim integration and native diagnostic logging" },
		SettingContract{ "MaxPositionsPerPage", "unified wheel dynamic capacity and paging" },
		SettingContract{ "SVGSlotScale", "OStim SVG slot artwork layout" },
		SettingContract{ "SVGSlotOffsetX", "OStim SVG slot artwork layout" },
		SettingContract{ "SVGSlotOffsetY", "OStim SVG slot artwork layout" },
		SettingContract{ "SVGCenterScale", "OStim SVG center artwork layout" },
		SettingContract{ "SVGCenterOffsetX", "OStim SVG center artwork layout" },
		SettingContract{ "SVGCenterOffsetY", "OStim SVG center artwork layout" },
		SettingContract{ "DDSSlotScale", "OStim DDS/PNG slot artwork layout" },
		SettingContract{ "DDSSlotOffsetX", "OStim DDS/PNG slot artwork layout" },
		SettingContract{ "DDSSlotOffsetY", "OStim DDS/PNG slot artwork layout" },
		SettingContract{ "DDSCenterScale", "OStim DDS/PNG center artwork layout" },
		SettingContract{ "DDSCenterOffsetX", "OStim DDS/PNG center artwork layout" },
		SettingContract{ "DDSCenterOffsetY", "OStim DDS/PNG center artwork layout" }
	};

	inline constexpr std::array<SettingContract, 1> kLegacyInternalSettings{
		SettingContract{
			"PreferCurrentAnimationClass",
			"legacy candidate-position fallback only; unused by native current-navigation mode" }
	};
}
