#pragma once

#include <cstdint>

namespace InputBroker
{
	using PluginId = std::uint64_t;

	enum class DeviceType : std::uint32_t
	{
		kMKB = 0,
		kGamepad = 1
	};

	namespace ReservationFlag
	{
		inline constexpr std::uint32_t None = 0;
		inline constexpr std::uint32_t ToggleKey = 1u << 0;
		inline constexpr std::uint32_t NavigationKey = 1u << 1;
		inline constexpr std::uint32_t CategoryKey = 1u << 2;
		inline constexpr std::uint32_t QTakeover = 1u << 3;
	}

	namespace ContextFlag
	{
		inline constexpr std::uint32_t None = 0;
		inline constexpr std::uint32_t IsDown = 1u << 0;
		inline constexpr std::uint32_t IsUp = 1u << 1;
		inline constexpr std::uint32_t MainWheelOpen = 1u << 2;
		inline constexpr std::uint32_t AmmoWheelOpen = 1u << 3;
	}

	inline constexpr PluginId kNoOwner = 0;
	inline constexpr PluginId kWheelerRefinedPluginId = 0x2893742724F3CFEAULL;

	void RefreshConfigFromSettings();
	bool IsEnabled();
	bool IsDebugLogEnabled();
	std::int32_t GetMainWheelPriority();
	std::int32_t GetAmmoWheelPriority();

	bool RegisterReservation(PluginId pluginId, DeviceType device, std::uint32_t key, std::int32_t priority, std::uint32_t flags);
	void UnregisterAll(PluginId pluginId);

	void SetActiveOwner(PluginId pluginId);
	void ClearActiveOwner(PluginId pluginId);
	PluginId GetActiveOwner();

	bool ShouldProcessKey(PluginId pluginIdSelf, DeviceType device, std::uint32_t key, std::uint32_t contextFlags);
	bool IsBlockedByActiveOwner(PluginId pluginIdSelf);

	void RefreshWheelerReservations();
	void SyncWheelerActiveOwner(bool mainWheelOpen, bool ammoWheelOpen);
}

