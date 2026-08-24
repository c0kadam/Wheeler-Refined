#include "InventorySnapshotCache.h"
#include "Utils.h"

#include "imgui.h"

#include <algorithm>
#include <chrono>

namespace
{
	using Clock = std::chrono::steady_clock;
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
	_map.clear();
	_nextRefreshTime = 0.0;
	_valid = false;
	_visibleLastFrame = false;
	_refreshCountThisWindow = 0;
	_lastRefreshMs = 0.0;
	_nextLogTime = 0.0;
}

RE::TESObjectREFR::InventoryItemMap& InventorySnapshotCache::Get(
	RE::PlayerCharacter* a_player,
	bool a_visibleNow,
	double a_refreshIntervalSeconds,
	Stats* a_outStats)
{
	if (a_outStats) {
		*a_outStats = Stats{};
	}

	const double now = ImGui::GetTime();
	const double refreshIntervalSeconds = ClampRefreshInterval(a_refreshIntervalSeconds);
	if (a_outStats) {
		a_outStats->refreshIntervalSeconds = refreshIntervalSeconds;
	}

	if (!a_visibleNow) {
		if (_visibleLastFrame) {
			Invalidate();
		}
		_visibleLastFrame = false;
		return _map;
	}

	InitializeLogWindow(now);
	const bool forceRefresh = !_visibleLastFrame;
	const bool shouldRefresh = !_valid || forceRefresh || now >= _nextRefreshTime;

	if (!a_player) {
		_map.clear();
		_valid = false;
		_nextRefreshTime = now + refreshIntervalSeconds;
	} else if (shouldRefresh) {
		const auto refreshStart = Clock::now();
		if (!Utils::Inventory::TryGetInventorySnapshot(a_player, _map, "InventorySnapshotCache")) {
			_valid = false;
			_nextRefreshTime = now + refreshIntervalSeconds;
			_lastRefreshMs = 0.0;
			_visibleLastFrame = true;
			return _map;
		}
		const auto refreshEnd = Clock::now();

		_valid = true;
		_nextRefreshTime = now + refreshIntervalSeconds;
		_lastRefreshMs = std::chrono::duration<double, std::milli>(refreshEnd - refreshStart).count();
		_refreshCountThisWindow++;
		if (a_outStats) {
			a_outStats->refreshedThisCall = true;
		}
	}

	if (now >= _nextLogTime) {
		if (a_outStats) {
			a_outStats->shouldLog = true;
			a_outStats->refreshCountThisWindow = _refreshCountThisWindow;
			a_outStats->lastRefreshMs = _lastRefreshMs;
		}
		_refreshCountThisWindow = 0;
		_nextLogTime = now + 1.0;
	}

	_visibleLastFrame = true;
	return _map;
}
