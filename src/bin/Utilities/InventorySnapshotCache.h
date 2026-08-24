#pragma once

#include <RE/Skyrim.h>

class InventorySnapshotCache
{
public:
	struct Stats
	{
		bool refreshedThisCall = false;
		bool shouldLog = false;
		int refreshCountThisWindow = 0;
		double lastRefreshMs = 0.0;
		double refreshIntervalSeconds = 0.25;
	};

	void Invalidate();

	RE::TESObjectREFR::InventoryItemMap& Get(
		RE::PlayerCharacter* a_player,
		bool a_visibleNow,
		double a_refreshIntervalSeconds,
		Stats* a_outStats = nullptr);

private:
	static double ClampRefreshInterval(double a_seconds);
	void InitializeLogWindow(double a_now);

	RE::TESObjectREFR::InventoryItemMap _map;
	double _nextRefreshTime = 0.0;
	bool _valid = false;
	bool _visibleLastFrame = false;

	int _refreshCountThisWindow = 0;
	double _lastRefreshMs = 0.0;
	double _nextLogTime = 0.0;
};
