#include "SlotHandIndicators.h"
#include "WheelItems/WheelItem.h"
#include "bin/Config.h"
#include "bin/Rendering/TextureManager.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>

namespace
{
	enum class IndicatorOffsetMode
	{
		Primary,
		Secondary,
		DualTop
	};

	struct CornerAnchor
	{
		ImVec2 point{};
		bool alignRight = false;
		bool alignBottom = false;
	};

	ImVec2 GetIndicatorOffset(const Config::MainWheel::HandIndicatorStyle& style, IndicatorOffsetMode mode, bool slotOnRightSide)
	{
		ImVec2 offset{ style.OffsetX, style.OffsetY };
		switch (mode) {
		case IndicatorOffsetMode::Secondary:
			offset.x += style.SecondaryOffsetX;
			offset.y += style.SecondaryOffsetY;
			break;
		case IndicatorOffsetMode::DualTop:
			offset.x += style.DualTopOffsetX;
			offset.y += style.DualTopOffsetY;
			break;
		case IndicatorOffsetMode::Primary:
		default:
			break;
		}
		if (slotOnRightSide) {
			offset.x += Config::MainWheel::HandIndicators::RightSideOffsetX;
			offset.y += Config::MainWheel::HandIndicators::RightSideOffsetY;
		}
		return offset;
	}

	ImVec2 GetTextPos(const CornerAnchor& anchor, const ImVec2& textSize)
	{
		ImVec2 pos = anchor.point;
		if (anchor.alignRight) {
			pos.x -= textSize.x;
		}
		if (anchor.alignBottom) {
			pos.y -= textSize.y;
		}
		return pos;
	}
}

SlotHandState SlotHandIndicators::ComputeSlotHandState(const std::vector<std::shared_ptr<WheelItem>>& a_items, int a_visibleIndex, RE::TESObjectREFR::InventoryItemMap& a_inv, const EquippedHandsCache& a_hands)
{
	SlotHandState state{};
	if (a_items.empty()) {
		return state;
	}
	const bool checkRight = a_hands.rightFormID != 0;
	const bool checkLeft = a_hands.leftFormID != 0;
	if (!checkRight && !checkLeft) {
		return state;
	}

	const int count = static_cast<int>(a_items.size());
	auto matchesHand = [&](const std::shared_ptr<WheelItem>& item, RE::FormID formID, std::uint64_t signature, bool leftHand) {
		if (!item) {
			return false;
		}
		const RE::FormID itemFormID = item->GetFormID();
		if (itemFormID == 0 || itemFormID != formID) {
			return false;
		}
		if (const auto overrideMatch = item->MatchesEquippedHandIndicator(a_inv, formID, signature, leftHand);
		    overrideMatch.has_value()) {
			return *overrideMatch;
		}

		const std::uint64_t itemSignature = item->GetI4Signature();
		const bool supportsPreciseMatching = item->SupportsPreciseHandIndicatorMatching(a_inv);
		if (supportsPreciseMatching && itemSignature != 0) {
			if (signature == 0) {
				// When the equipped-hand snapshot has not resolved a concrete unique signature yet,
				// falling back to form-only matching makes same-form sibling instances flash the wrong
				// hand indicator for a frame. Prefer a brief missing indicator over incorrect bleed.
				return false;
			}
			return itemSignature == signature;
		}

		return true;
	};
	for (int i = 0; i < count; ++i) {
		const auto& item = a_items[i];
		if (!item) {
			continue;
		}
		if (checkRight && !state.hasRight && matchesHand(item, a_hands.rightFormID, a_hands.rightSignature, false)) {
			state.hasRight = true;
			state.rightLayerIdx = i;
		}
		if (checkLeft && !state.hasLeft && matchesHand(item, a_hands.leftFormID, a_hands.leftSignature, true)) {
			state.hasLeft = true;
			state.leftLayerIdx = i;
		}
		if (state.hasRight && state.hasLeft) {
			break;
		}
	}

	if (a_visibleIndex >= 0 && a_visibleIndex < count) {
		state.currentIsRight = state.hasRight && state.rightLayerIdx == a_visibleIndex;
		state.currentIsLeft = state.hasLeft && state.leftLayerIdx == a_visibleIndex;
	}

	return state;
}

void SlotHandIndicators::DrawSlotHandIndicators(const SlotHandState& a_state, const ImVec2& a_slotMin, const ImVec2& a_slotMax, bool a_slotOnRightSide, const DrawArgs& a_drawArgs)
{
	if (!Config::MainWheel::ShowHandIndicator) {
		return;
	}
	if (!a_state.hasRight && !a_state.hasLeft) {
		return;
	}

	const float width = a_slotMax.x - a_slotMin.x;
	const float height = a_slotMax.y - a_slotMin.y;
	if (width <= 0.0f || height <= 0.0f) {
		return;
	}

	const float minDim = (std::min)(width, height);
	const float padding = (std::max)(2.0f, minDim * 0.06f);
	const float primarySize = (std::max)(10.0f, minDim * 0.28f);
	const float secondarySize = (std::max)(9.0f, minDim * 0.22f);
	const float secondaryAlpha = 0.65f;
	const auto& leftStyle = Config::MainWheel::HandIndicators::Left;
	const auto& rightStyle = Config::MainWheel::HandIndicators::Right;
	const auto& dualStyle = Config::MainWheel::HandIndicators::Dual;
	auto applyAlpha = [&](ImU32 color, float alpha) {
		ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
		c.w *= alpha;
		return ImGui::ColorConvertFloat4ToU32(c);
	};

	CornerAnchor primaryCorner{};
	CornerAnchor secondaryCorner{};
	if (a_slotOnRightSide) {
		primaryCorner.point = ImVec2(a_slotMax.x - padding, a_slotMin.y + padding);
		primaryCorner.alignRight = true;
		secondaryCorner.point = ImVec2(a_slotMin.x + padding, a_slotMax.y - padding);
		secondaryCorner.alignBottom = true;
	} else {
		primaryCorner.point = ImVec2(a_slotMin.x + padding, a_slotMin.y + padding);
		secondaryCorner.point = ImVec2(a_slotMax.x - padding, a_slotMax.y - padding);
		secondaryCorner.alignRight = true;
		secondaryCorner.alignBottom = true;
	}

	auto drawLabel = [&](const char* label,
		bool primaryStyle,
		const CornerAnchor& anchor,
		const Config::MainWheel::HandIndicatorStyle& style,
		IndicatorOffsetMode offsetMode) {
		const float fontSize = (primaryStyle ? primarySize : secondarySize) * style.SizeScale;
		ImFont* font = ImGui::GetFont();
		DrawArgs labelArgs = a_drawArgs;
		labelArgs.alphaMult *= style.Opacity;
		if (!primaryStyle) {
			labelArgs.alphaMult *= secondaryAlpha;
		}
		const ImVec2 indicatorOffset = GetIndicatorOffset(style, offsetMode, a_slotOnRightSide);
		const std::string& assetPath =
			(offsetMode == IndicatorOffsetMode::Secondary && !style.SecondaryAssetPath.empty()) ?
				style.SecondaryAssetPath :
				style.AssetPath;

		if (!assetPath.empty()) {
			const Texture::Image icon = Texture::GetImageByPath(assetPath);
			if (icon.texture && icon.width > 0 && icon.height > 0) {
				const float aspect = static_cast<float>(icon.width) / static_cast<float>(icon.height);
				const ImVec2 iconSize(fontSize * aspect, fontSize);
				ImVec2 iconPos = GetTextPos(anchor, iconSize);
				iconPos.x += indicatorOffset.x;
				iconPos.y += indicatorOffset.y;
				const ImU32 iconTint = applyAlpha(style.Color, labelArgs.alphaMult);
				ImTextureID texId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(icon.texture));
				ImGui::GetForegroundDrawList()->AddImage(
					texId,
					iconPos,
					ImVec2(iconPos.x + iconSize.x, iconPos.y + iconSize.y),
					ImVec2(0.0f, 0.0f),
					ImVec2(1.0f, 1.0f),
					iconTint);
				return;
			}
		}

		const ImVec2 textSize = font ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label) : ImVec2(fontSize, fontSize);
		ImVec2 pos = GetTextPos(anchor, textSize);
		pos.x += indicatorOffset.x;
		pos.y += indicatorOffset.y;

		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		const ImU32 shadowColor = applyAlpha(Config::Styling::Wheel::TextShadowColor, labelArgs.alphaMult);
		const float shadowOffset = fontSize * 0.05f;
		drawList->AddText(font, fontSize, ImVec2(pos.x + shadowOffset, pos.y + shadowOffset), shadowColor, label);

		const ImU32 textColor = applyAlpha(style.Color, labelArgs.alphaMult);
		const float boldOffset = (std::max)(0.0f, style.Thickness);
		if (boldOffset > 0.0f) {
			drawList->AddText(font, fontSize, ImVec2(pos.x + boldOffset, pos.y), textColor, label);
			drawList->AddText(font, fontSize, ImVec2(pos.x, pos.y + boldOffset), textColor, label);
		}
		drawList->AddText(font, fontSize, pos, textColor, label);
	};

	const bool rightPrimaryStyle = a_state.currentIsRight;
	const bool leftPrimaryStyle = a_state.currentIsLeft;

	if (a_state.currentIsRight && a_state.currentIsLeft) {
		const Texture::Image icon = Texture::GetImageByPath(dualStyle.AssetPath);
		DrawArgs iconArgs = a_drawArgs;
		iconArgs.alphaMult *= dualStyle.Opacity;
		const float iconHeight = (std::max)(12.0f, primarySize * dualStyle.SizeScale);
		const ImVec2 slotCenter((a_slotMin.x + a_slotMax.x) * 0.5f, (a_slotMin.y + a_slotMax.y) * 0.5f);
		ImVec2 iconOffset(dualStyle.OffsetX, dualStyle.OffsetY);
		if (a_slotOnRightSide) {
			iconOffset.x += Config::MainWheel::HandIndicators::DualRightSideOffsetX;
			iconOffset.y += Config::MainWheel::HandIndicators::DualRightSideOffsetY;
		}

		if (icon.texture && icon.width > 0 && icon.height > 0) {
			const float aspect = static_cast<float>(icon.width) / static_cast<float>(icon.height);
			const ImVec2 iconSize(iconHeight * aspect, iconHeight);
			ImVec2 iconPos(slotCenter.x - iconSize.x * 0.5f, a_slotMin.y + padding);
			iconPos.x += iconOffset.x;
			iconPos.y += iconOffset.y;
			const ImU32 tint = applyAlpha(dualStyle.Color, iconArgs.alphaMult);
			ImTextureID texId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(icon.texture));
			ImGui::GetForegroundDrawList()->AddImage(
				texId,
				iconPos,
				ImVec2(iconPos.x + iconSize.x, iconPos.y + iconSize.y),
				ImVec2(0.0f, 0.0f),
				ImVec2(1.0f, 1.0f),
				tint);
		} else {
			const char* fallback = "LR";
			ImFont* font = ImGui::GetFont();
			const float fontSize = iconHeight;
			const ImVec2 textSize = font ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fallback) : ImVec2(fontSize, fontSize);
			const ImVec2 pos(slotCenter.x - textSize.x * 0.5f + iconOffset.x, a_slotMin.y + padding + iconOffset.y);
			const ImU32 shadowColor = applyAlpha(Config::Styling::Wheel::TextShadowColor, iconArgs.alphaMult);
			const float shadowOffset = fontSize * 0.05f;
			ImDrawList* drawList = ImGui::GetForegroundDrawList();
			drawList->AddText(font, fontSize, ImVec2(pos.x + shadowOffset, pos.y + shadowOffset), shadowColor, fallback);
			const ImU32 textColor = applyAlpha(dualStyle.Color, iconArgs.alphaMult);
			const float boldOffset = (std::max)(0.0f, dualStyle.Thickness);
			if (boldOffset > 0.0f) {
				drawList->AddText(font, fontSize, ImVec2(pos.x + boldOffset, pos.y), textColor, fallback);
				drawList->AddText(font, fontSize, ImVec2(pos.x, pos.y + boldOffset), textColor, fallback);
			}
			drawList->AddText(font, fontSize, pos, textColor, fallback);
		}
		return;
	}

	if (a_state.hasRight && a_state.hasLeft) {
		bool primaryIsRight = true;
		if (rightPrimaryStyle && !leftPrimaryStyle) {
			primaryIsRight = true;
		} else if (leftPrimaryStyle && !rightPrimaryStyle) {
			primaryIsRight = false;
		}

		if (primaryIsRight) {
			drawLabel("R", rightPrimaryStyle, primaryCorner, rightStyle, IndicatorOffsetMode::Primary);
			drawLabel("L", leftPrimaryStyle, secondaryCorner, leftStyle, IndicatorOffsetMode::Secondary);
		} else {
			drawLabel("L", leftPrimaryStyle, primaryCorner, leftStyle, IndicatorOffsetMode::Primary);
			drawLabel("R", rightPrimaryStyle, secondaryCorner, rightStyle, IndicatorOffsetMode::Secondary);
		}
		return;
	}

	if (a_state.hasRight) {
		const IndicatorOffsetMode mode = rightPrimaryStyle ? IndicatorOffsetMode::Primary : IndicatorOffsetMode::Secondary;
		drawLabel("R", rightPrimaryStyle, primaryCorner, rightStyle, mode);
		return;
	}

	if (a_state.hasLeft) {
		const IndicatorOffsetMode mode = leftPrimaryStyle ? IndicatorOffsetMode::Primary : IndicatorOffsetMode::Secondary;
		drawLabel("L", leftPrimaryStyle, primaryCorner, leftStyle, mode);
	}
}
