#include "InventorySnapshotCache.h"
#include "Utils.h"

#include "imgui.h"

#include <algorithm>
#include <atomic>
#include <chrono>

namespace
{
	using Clock = std::chrono::steady_clock;
	std::atomic<std::uint64_t> g_inventoryMutationGeneration{ 0 };
}

double InventorySnapshotCache::ClampRefreshInterval(double a_seconds)
{
	return std::clamp(a_seconds, 0.05, 1.0);
}

void InventorySnapshotCache::InitializeLogWindow(double a_now)
{
	if (_nextLogTime <= 0.0) {
		_nextLogTime = a_now + 1.0;
	}
}

void InventorySnapshotCache::Invalidate()
{
	_nextDerivedRefreshTime = 0.0;
	_visibleLastFrame = false;
	_captureCountThisWindow = 0;
	_lastCaptureMs = 0.0;
	_nextLogTime = 0.0;
}

void InventorySnapshotCache::NotifyInventoryMutation() noexcept
{
	g_inventoryMutationGeneration.fetch_add(1, std::memory_order_release);
}

std::uint64_t InventorySnapshotCache::GetMutationGeneration() noexcept
{
	return g_inventoryMutationGeneration.load(std::memory_order_acquire);
}

bool InventorySnapshotCache::CaptureFresh(
	RE::PlayerCharacter* a_player,
	bool a_visibleNow,
	double a_derivedRefreshIntervalSeconds,
	RE::TESObjectREFR::InventoryItemMap& a_outInventory,
	Stats* a_outStats)
{
	a_outInventory.clear();
	if (a_outStats) {
		*a_outStats = Stats{};
	}

	const double now = ImGui::GetTime();
	const double derivedRefreshIntervalSeconds = ClampRefreshInterval(a_derivedRefreshIntervalSeconds);
	if (a_outStats) {
		a_outStats->derivedRefreshIntervalSeconds = derivedRefreshIntervalSeconds;
		a_outStats->mutationGeneration = GetMutationGeneration();
	}

	if (!a_visibleNow) {
		_visibleLastFrame = false;
		_nextDerivedRefreshTime = 0.0;
		return false;
	}

	InitializeLogWindow(now);
	const bool refreshDerivedData = !_visibleLastFrame || now >= _nextDerivedRefreshTime;
	if (!a_player) {
		_visibleLastFrame = true;
		return false;
	}

	const auto captureStart = Clock::now();
	bool captured = false;
	bool retriedAfterMutation = false;
	std::uint64_t stableGeneration = GetMutationGeneration();
	for (int attempt = 0; attempt < 2; ++attempt) {
		const std::uint64_t generationBefore = GetMutationGeneration();
		if (!Utils::Inventory::TryGetInventorySnapshot(
				a_player,
				a_outInventory,
				"InventorySnapshotCache::CaptureFresh")) {
			a_outInventory.clear();
			break;
		}
		const std::uint64_t generationAfter = GetMutationGeneration();
		if (generationBefore == generationAfter) {
			captured = true;
			stableGeneration = generationAfter;
			break;
		}

		a_outInventory.clear();
		retriedAfterMutation = true;
	}
	const auto captureEnd = Clock::now();
	_lastCaptureMs = std::chrono::duration<double, std::milli>(captureEnd - captureStart).count();
	_captureCountThisWindow++;

	if (captured && refreshDerivedData) {
		_nextDerivedRefreshTime = now + derivedRefreshIntervalSeconds;
	}
	if (a_outStats) {
		a_outStats->capturedFreshThisCall = captured;
		a_outStats->refreshDerivedDataThisCall = captured && refreshDerivedData;
		a_outStats->retriedAfterMutation = retriedAfterMutation;
		a_outStats->mutationGeneration = stableGeneration;
	}

	if (now >= _nextLogTime) {
		if (a_outStats) {
			a_outStats->shouldLog = true;
			a_outStats->captureCountThisWindow = _captureCountThisWindow;
			a_outStats->lastCaptureMs = _lastCaptureMs;
		}
		_captureCountThisWindow = 0;
		_nextLogTime = now + 1.0;
	}

	_visibleLastFrame = true;
	return captured;
}
