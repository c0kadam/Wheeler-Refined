#pragma once
#include <memory>
#include <vector>

class WheelItem;
struct DrawArgs;
struct EquippedHandsCache;
struct ImVec2;

struct SlotHandState
{
	bool hasRight = false;
	bool hasLeft = false;
	bool currentIsRight = false;
	bool currentIsLeft = false;
	int rightLayerIdx = -1;
	int leftLayerIdx = -1;
};

namespace SlotHandIndicators
{
	SlotHandState ComputeSlotHandState(const std::vector<std::shared_ptr<WheelItem>>& a_items, int a_visibleIndex, RE::TESObjectREFR::InventoryItemMap& a_inv, const EquippedHandsCache& a_hands);
	void DrawSlotHandIndicators(const SlotHandState& a_state, const ImVec2& a_slotMin, const ImVec2& a_slotMax, bool a_slotOnRightSide, const DrawArgs& a_drawArgs);
}
