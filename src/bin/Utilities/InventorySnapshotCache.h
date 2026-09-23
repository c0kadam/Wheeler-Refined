#pragma once

#include <RE/Skyrim.h>

#include <cstdint>
#include <utility>

class InventorySnapshotCache
{
public:
	struct Stats
	{
		bool capturedFreshThisCall = false;
		bool refreshDerivedDataThisCall = false;
		bool retriedAfterMutation = false;
		bool shouldLog = false;
		int captureCountThisWindow = 0;
		double lastCaptureMs = 0.0;
		double derivedRefreshIntervalSeconds = 0.25;
		std::uint64_t mutationGeneration = 0;
	};

	// Marks the synchronous lifetime boundary around an engine inventory mutation.
	// The generation is pointer-free and exists only as defense in depth for any
	// future derived-data cache. Fresh snapshots are still mandatory on every draw.
	class MutationBoundary final
	{
	public:
		MutationBoundary() = default;
		~MutationBoundary() { InventorySnapshotCache::NotifyInventoryMutation(); }
		MutationBoundary(const MutationBoundary&) = delete;
		MutationBoundary& operator=(const MutationBoundary&) = delete;
	};

	void Invalidate();

	bool CaptureFresh(
		RE::PlayerCharacter* a_player,
		bool a_visibleNow,
		double a_derivedRefreshIntervalSeconds,
		RE::TESObjectREFR::InventoryItemMap& a_outInventory,
		Stats* a_outStats = nullptr);

	static void NotifyInventoryMutation() noexcept;
	static std::uint64_t GetMutationGeneration() noexcept;

	template <class Callable>
	static decltype(auto) RunMutation(Callable&& a_callable)
	{
		MutationBoundary mutationBoundary;
		return std::forward<Callable>(a_callable)();
	}

	template <class... Args>
	static void EquipObject(RE::ActorEquipManager* a_manager, Args&&... a_args)
	{
		RunMutation([&]() { a_manager->EquipObject(std::forward<Args>(a_args)...); });
	}

	template <class... Args>
	static void UnequipObject(RE::ActorEquipManager* a_manager, Args&&... a_args)
	{
		RunMutation([&]() { a_manager->UnequipObject(std::forward<Args>(a_args)...); });
	}

	template <class... Args>
	static void EquipSpell(RE::ActorEquipManager* a_manager, Args&&... a_args)
	{
		RunMutation([&]() { a_manager->EquipSpell(std::forward<Args>(a_args)...); });
	}

	template <class... Args>
	static void EquipShout(RE::ActorEquipManager* a_manager, Args&&... a_args)
	{
		RunMutation([&]() { a_manager->EquipShout(std::forward<Args>(a_args)...); });
	}

private:
	static double ClampRefreshInterval(double a_seconds);
	void InitializeLogWindow(double a_now);

	// Scalar telemetry/derived-data scheduling only. This class must never own an
	// InventoryItemMap, InventoryEntryData, or ExtraDataList pointer.
	double _nextDerivedRefreshTime = 0.0;
	bool _visibleLastFrame = false;

	int _captureCountThisWindow = 0;
	double _lastCaptureMs = 0.0;
	double _nextLogTime = 0.0;
};
