#pragma once

#include <cstdint>

#include "bin/API/WheelerAPI.h"

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
	inline constexpr PluginId kDMenuInputOwnerId = 0x444D454E55000001ULL;

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

	struct CooperativeOpeningGrantDecision
	{
		bool granted{ false };
		PluginId ownerId{ kNoOwner };
		std::uint64_t bindingGeneration{ 0 };
		std::uint64_t opaqueGrantToken{ 0 };
		WheelerAPI::CooperativeOpeningReason reason{ WheelerAPI::CooperativeOpeningReason::kNone };
	};

	class CooperativeOpeningProducerScope
	{
	public:
		CooperativeOpeningProducerScope();
		~CooperativeOpeningProducerScope();

		CooperativeOpeningProducerScope(const CooperativeOpeningProducerScope&) = delete;
		CooperativeOpeningProducerScope& operator=(const CooperativeOpeningProducerScope&) = delete;
		CooperativeOpeningProducerScope(CooperativeOpeningProducerScope&&) = delete;
		CooperativeOpeningProducerScope& operator=(CooperativeOpeningProducerScope&&) = delete;

		[[nodiscard]] std::uint64_t GetDispatchGeneration() const noexcept { return _dispatchGeneration; }

	private:
		std::uint64_t _dispatchGeneration{ 0 };
	};

	[[nodiscard]] WheelerAPI::CooperativeOpeningReplaceResult ReplaceCooperativeOpeningBindings(
		const WheelerAPI::CooperativeOpeningBindingSet* bindingSet);
	[[nodiscard]] std::uint64_t BeginCooperativeOpeningConsumerScope(
		PluginId ownerId,
		std::uint64_t expectedBindingGeneration);
	[[nodiscard]] WheelerAPI::CooperativeOpeningAttestation ObserveAndClaimCooperativeOpeningEvent(
		const WheelerAPI::CooperativeOpeningEventObservation* observation,
		WheelerAPI::CooperativeOpeningEventDisposition* disposition);
	[[nodiscard]] WheelerAPI::CooperativeOpeningAttestation GetCooperativeOpeningEventDisposition(
		std::uint64_t consumerScopeToken,
		std::uintptr_t eventIdentity,
		PluginId ownerId,
		WheelerAPI::CooperativeOpeningEventDisposition* disposition);
	void EndCooperativeOpeningConsumerScope(std::uint64_t consumerScopeToken);

	[[nodiscard]] CooperativeOpeningGrantDecision TryGrantCooperativeOpening(
		std::uintptr_t eventIdentity,
		DeviceType device,
		std::uint32_t mappedKey,
		bool physicalPrimaryDownEdge,
		bool mainWheelExclusiveContext);

	namespace CooperativeOpeningPolicy
	{
		[[nodiscard]] constexpr bool IsPrimaryInDomain(
			WheelerAPI::CooperativeOpeningDevice device,
			std::uint32_t key) noexcept
		{
			return device == WheelerAPI::CooperativeOpeningDevice::kMKB ?
			           key >= 1 && key <= 265 :
			       device == WheelerAPI::CooperativeOpeningDevice::kGamepad ?
			           key >= 266 && key <= 281 :
			           false;
		}

		[[nodiscard]] constexpr bool IsModifierInDomain(
			WheelerAPI::CooperativeOpeningDevice device,
			std::uint32_t key) noexcept
		{
			if (key == 0) {
				return true;
			}
			return device == WheelerAPI::CooperativeOpeningDevice::kMKB ?
			           key >= 1 && key <= 263 :
			       device == WheelerAPI::CooperativeOpeningDevice::kGamepad ?
			           key >= 266 && key <= 281 :
			           false;
		}

		[[nodiscard]] constexpr bool IsBindingValid(
			const WheelerAPI::CooperativeOpeningBinding& binding) noexcept
		{
			return binding.structSize == sizeof(WheelerAPI::CooperativeOpeningBinding) &&
			       binding.descriptorVersion == WheelerAPI::COOPERATIVE_OPENING_BINDING_VERSION &&
			       IsPrimaryInDomain(binding.device, binding.primaryMappedKey) &&
			       IsModifierInDomain(binding.device, binding.modifierMappedKey) &&
			       binding.primaryMappedKey != binding.modifierMappedKey &&
			       binding.triggerEdge == WheelerAPI::CooperativeOpeningTriggerEdge::kPrimaryDown &&
			       binding.semanticFlags == WheelerAPI::CooperativeOpeningSemanticFlag::kConsumeOnGrant;
		}
	}
}

