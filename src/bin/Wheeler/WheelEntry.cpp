#include "bin/Rendering/Drawer.h"
#include "bin/Rendering/Drawer.h"
#include "bin/Rendering/TextureManager.h"
#include "SlotHandIndicators.h"
#include "WheelItems/WheelItem.h"
#include "WheelItems/WheelItemAlchemy.h"
#include "WheelItems/WheelItemFactory.h"
#include "WheelItems/WheelItemIngredient.h"
#include "WheelItems/WheelItemMissing.h"
#include "WheelItems/WheelItemMutable.h"
#include "WheelItems/WheelItemWeapon.h"
#include "WheelItems/WheelItemArmor.h"
#include "WheelEntry.h"
#include "MainWheelDebug.h"
#include "bin/Integrations/OStimIntegration.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	const char* GetSlotBackgroundName(Texture::icon_image_type type)
	{
		switch (type) {
		case Texture::icon_image_type::slot_background:
			return "slot_background";
		case Texture::icon_image_type::slot_active_background:
			return "slot_active_background";
		case Texture::icon_image_type::slot_highlighted_background:
			return "slot_highlighted_background";
		default:
			return "slot_background_unknown";
		}
	}

	bool IsWheelItemMissingForRender(const std::shared_ptr<WheelItem>& a_item, RE::TESObjectREFR::InventoryItemMap& a_imap)
	{
		if (!a_item) {
			return false;
		}

		if (dynamic_cast<WheelItemMissing*>(a_item.get()) != nullptr) {
			return true;
		}

		if (!a_item->IsInventoryBacked()) {
			return false;
		}

		return !a_item->IsAvailable(a_imap);
	}

	bool ShouldPreserveSelectedMutableInstance(const std::shared_ptr<WheelItem>& a_currentItem, const std::shared_ptr<WheelItem>& a_newItem)
	{
		if (!a_currentItem || !a_newItem) {
			return false;
		}

		const char* currentType = a_currentItem->GetItemTypeName();
		const char* newType = a_newItem->GetItemTypeName();
		const bool isCurrentInstanceBacked =
			currentType == WheelItemWeapon::ITEM_TYPE_STR || currentType == WheelItemArmor::ITEM_TYPE_STR;
		const bool isNewInstanceBacked =
			newType == WheelItemWeapon::ITEM_TYPE_STR || newType == WheelItemArmor::ITEM_TYPE_STR;
		if (!isCurrentInstanceBacked || !isNewInstanceBacked) {
			return false;
		}

		auto* currentMutable = dynamic_cast<WheelItemMutable*>(a_currentItem.get());
		auto* newMutable = dynamic_cast<WheelItemMutable*>(a_newItem.get());
		if (!currentMutable || !newMutable) {
			return false;
		}

		const RE::FormID currentFormID = currentMutable->GetFormID();
		const RE::FormID newFormID = newMutable->GetFormID();
		if (currentFormID == 0 || currentFormID != newFormID) {
			return false;
		}

		const std::uint16_t currentUniqueID = currentMutable->GetUniqueID();
		const std::uint16_t newUniqueID = newMutable->GetUniqueID();
		return currentUniqueID != 0 &&
		       newUniqueID != 0 &&
		       currentUniqueID != newUniqueID;
	}

	bool HighlightTargetChanged(float current, float previous)
	{
		constexpr float epsilon = 0.001f;
		return !std::isfinite(previous) || std::fabs(current - previous) > epsilon;
	}
}

void WheelEntry::UpdateAnimation(RE::TESObjectREFR::InventoryItemMap& imap, float innerSpacingRad, float entryInnerAngleMin, float entryInnerAngleMax, float entryOuterAngleMin, float entryOuterAngleMax, bool hovered)
{
	using namespace Config::Styling::Wheel;
	bool active = this->IsActive(imap);
	if (hovered) {
		float expandSize = Config::Animation::EntryHighlightExpandScale * (OuterCircleRadius - InnerCircleRadius);
		float outerTarget = 0.0f;
		if (OuterCircleRadius != 0.0f) {
			outerTarget = innerSpacingRad * (InnerCircleRadius / OuterCircleRadius);
		}
		float innerTarget = innerSpacingRad;
		if (!std::isfinite(expandSize)) {
			expandSize = 0.0f;
		}
		if (!std::isfinite(outerTarget)) {
			outerTarget = 0.0f;
		}
		if (!std::isfinite(innerTarget)) {
			innerTarget = 0.0f;
		}
		if (!_prevHovered ||
			HighlightTargetChanged(expandSize, _lastHighlightExpandSize) ||
			HighlightTargetChanged(outerTarget, _lastHighlightOuterAngleInc) ||
			HighlightTargetChanged(innerTarget, _lastHighlightInnerAngleInc)) {
			_arcRadiusIncInterpolator.InterpolateTo(expandSize, Config::Animation::EntryHighlightExpandTime);
			_arcOuterAngleIncInterpolator.InterpolateTo(outerTarget, Config::Animation::EntryHighlightExpandTime);
			_arcInnerAngleIncInterpolator.InterpolateTo(innerTarget, Config::Animation::EntryHighlightExpandTime);
			_lastHighlightExpandSize = expandSize;
			_lastHighlightOuterAngleInc = outerTarget;
			_lastHighlightInnerAngleInc = innerTarget;
			_prevHovered = true;
		}
	} else {
		if (_prevHovered != false) {
			_prevHovered = false;
			_arcRadiusIncInterpolator.InterpolateTo(0, Config::Animation::EntryHighlightRetractTime);
			_arcOuterAngleIncInterpolator.InterpolateTo(0, Config::Animation::EntryHighlightRetractTime);
			_arcInnerAngleIncInterpolator.InterpolateTo(0, Config::Animation::EntryHighlightRetractTime);
			_lastHighlightExpandSize = std::numeric_limits<float>::quiet_NaN();
			_lastHighlightOuterAngleInc = std::numeric_limits<float>::quiet_NaN();
			_lastHighlightInnerAngleInc = std::numeric_limits<float>::quiet_NaN();
		}
	}
}

void WheelEntry::DrawBackGround(
	const ImVec2 wheelCenter, const ImVec2 entryCenter, 
	float innerSpacingRad, 
	float entryInnerAngleMin, float entryInnerAngleMax,
	float entryOuterAngleMin, float entryOuterAngleMax, 
	bool hovered, 
	int numArcSegments, RE::TESObjectREFR::InventoryItemMap& inv, DrawArgs a_drawARGS)
{
	bool active = this->IsActive(inv);
	// TODO: Add independent background-texture scaling and decouple this rendering path.

	using namespace Config::Styling::Wheel;

	if (UseGeometricPrimitiveForBackgroundTexture) {
		float mainArcOuterBoundRadius = OuterCircleRadius;
		mainArcOuterBoundRadius += _arcRadiusIncInterpolator.GetValue();
		mainArcOuterBoundRadius += _arcRadiusBounceInterpolator.GetValue();

		float entryInnerAngleMinUpdated = entryInnerAngleMin - _arcInnerAngleIncInterpolator.GetValue() * 2;
		float entryInnerAngleMaxUpdated = entryInnerAngleMax + _arcInnerAngleIncInterpolator.GetValue() * 2;
		float entryOuterAngleMinUpdated = entryOuterAngleMin - _arcOuterAngleIncInterpolator.GetValue() * 2;
		float entryOuterAngleMaxUpdated = entryOuterAngleMax + _arcOuterAngleIncInterpolator.GetValue() * 2;

		Drawer::draw_arc_gradient(wheelCenter,
			InnerCircleRadius,
			mainArcOuterBoundRadius,
			entryInnerAngleMinUpdated, entryInnerAngleMaxUpdated,
			entryOuterAngleMinUpdated, entryOuterAngleMaxUpdated,
			hovered ? HoveredColorBegin : UnhoveredColorBegin,
			hovered ? HoveredColorEnd : UnhoveredColorEnd,
			numArcSegments, a_drawARGS);

		ImU32 arcColorBegin = active ? ActiveArcColorBegin : InActiveArcColorBegin;
		ImU32 arcColorEnd = active ? ActiveArcColorEnd : InActiveArcColorEnd;

		Drawer::draw_arc_gradient(wheelCenter,
			mainArcOuterBoundRadius,
			mainArcOuterBoundRadius + ActiveArcWidth,
			entryOuterAngleMinUpdated,
			entryOuterAngleMaxUpdated,
			entryOuterAngleMinUpdated,
			entryOuterAngleMaxUpdated,
			arcColorBegin,
			arcColorEnd,
			numArcSegments, a_drawARGS);
	} else {
		Texture::icon_image_type backgroundImageType = Texture::icon_image_type::slot_background;
		if (active) {
			backgroundImageType = Texture::icon_image_type::slot_active_background;
		} else if (hovered) {
			backgroundImageType = Texture::icon_image_type::slot_highlighted_background;
		}
		Texture::Image backgroundTexture = Texture::GetIconImage(backgroundImageType);
		if (!backgroundTexture.texture) {
			MainWheelDebug::LogRateLimited(MainWheelDebug::Category::Assets, GetSlotBackgroundName(backgroundImageType),
				"Slot background texture missing ({})", GetSlotBackgroundName(backgroundImageType));
		}

		const float scale = Config::Styling::Item::Slot::BackgroundTexture::Scale;
		const ImVec2 size(backgroundTexture.width * scale, backgroundTexture.height * scale);
		const ImVec2 posMin(entryCenter.x - size.x * 0.5f, entryCenter.y - size.y * 0.5f);
		const ImVec2 posMax(entryCenter.x + size.x * 0.5f, entryCenter.y + size.y * 0.5f);

		// Selected/active indicator cooldown response:
		// For items on cooldown, gradually restore the indicator from cooldown-tinted to normal.
		// Direction is kept consistent with the slot content cooldown effect (bottom-to-top).
		// Only applies to non-default indicator backgrounds (hovered/active). Default slot background is untouched.
		bool drewCooldownIndicator = false;
		if (Config::Cooldowns::Enabled && Config::Cooldowns::SelectedIndicatorEnabled && backgroundImageType != Texture::icon_image_type::slot_background) {
			const std::shared_ptr<WheelItem> selectedItem = GetSelectedItem();
			if (selectedItem && selectedItem->HasCooldown()) {
				const float remainingPercent = selectedItem->GetCooldownPercent();
				const float progress = std::clamp(1.0f - remainingPercent, 0.0f, 1.0f);
				if (progress < 1.0f) {
					// Base pass: cooldown-tinted indicator.
					const ImU32 cooldownTint = Config::Cooldowns::SelectedIndicatorTintColor;
					Drawer::draw_texture(backgroundTexture.texture, entryCenter, 0, 0, size, cooldownTint, a_drawARGS);

					// Restore pass: normal indicator clipped from bottom-to-top by cooldown progress.
					const float fillHeight = size.y * progress;
					const ImVec2 clipMin(posMin.x, posMax.y - fillHeight);
					const ImVec2 clipMax = posMax;
					ImDrawList* drawList = ImGui::GetWindowDrawList();
					drawList->PushClipRect(clipMin, clipMax, true);
					Drawer::draw_texture(backgroundTexture.texture, entryCenter, 0, 0, size, C_SKYRIMWHITE, a_drawARGS);
					drawList->PopClipRect();
					drewCooldownIndicator = true;
				}
			}
		}
		if (!drewCooldownIndicator) {
			Drawer::draw_texture(backgroundTexture.texture, entryCenter, 0, 0, size, C_SKYRIMWHITE, a_drawARGS);
		}
	
	}

	
}

void WheelEntry::DrawSlotAndHighlight(ImVec2 a_wheelCenter, ImVec2 a_entryCenter, bool a_slotOnRightSide, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs, const EquippedHandsCache& a_hands)
{
	// SAFETY: Early-out if this pointer is suspicious (near-null address check)
	// This catches cases where the entry was destroyed but pointer wasn't nulled
	if (reinterpret_cast<std::uintptr_t>(this) < 0x10000) {
		logger::error("[WheelEntry::DrawSlotAndHighlight] Invalid this pointer: {:p}", static_cast<void*>(this));
		return;
	}

	bool isMissing = false;
	{
		std::shared_lock<std::shared_mutex> lock(this->_lock);
		if (_selectedItem >= 0 && _selectedItem < _items.size()) {
			isMissing = IsWheelItemMissingForRender(_items[_selectedItem], a_imap);
		}
	}
	
	if (a_hovered) {
		this->drawHighlight(a_wheelCenter, a_imap, a_drawArgs, isMissing);
	}
	this->drawSlot(a_entryCenter, a_slotOnRightSide, a_hovered, a_imap, a_drawArgs, isMissing, a_hands);
}

void WheelEntry::drawSlot(ImVec2 a_center, bool a_slotOnRightSide, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs, bool a_isMissing, const EquippedHandsCache& a_hands)
{
	try {
		std::shared_lock<std::shared_mutex> lock(this->_lock);
		
		// Handle empty slot with entry-level missing state (for slots that were cleared but kept)
		if (_items.size() == 0) {
			if (_missingInInventory) {
				DrawArgs labelArgs = a_drawArgs;
				labelArgs.alphaMult *= 0.85f;
				const float labelSize = Config::Styling::Item::Slot::Text::Size * 0.55f;
				const float labelX = a_center.x + Config::Styling::Item::Slot::Text::OffsetX;
				const float labelY = a_center.y + Config::Styling::Item::Slot::Text::OffsetY + labelSize;
				Drawer::draw_text(labelX, labelY, "MISSING", C_SKYRIMWHITE, labelSize, labelArgs);
			}
			return;  // nothing to draw
		}
		if (_selectedItem < 0 || _selectedItem >= _items.size()) {
			return;  // out of bounds selection, skip draw to avoid crash
		}
		
		const std::shared_ptr<WheelItem>& currentItem = _items[_selectedItem];
		if (!currentItem) {
			return;
		}
		
		// Reset visual state for this item - start fresh
		DrawArgs itemArgs = a_drawArgs;
		if (a_isMissing) {
			itemArgs.alphaMult *= 0.4f;
		}
		
		currentItem->DrawSlot(a_center, a_hovered, a_imap, itemArgs);
		if (a_isMissing) {
			DrawArgs labelArgs = a_drawArgs;
			labelArgs.alphaMult *= 0.85f;
			const float labelSize = Config::Styling::Item::Slot::Text::Size * 0.55f;
			const float labelX = a_center.x + Config::Styling::Item::Slot::Text::OffsetX;
			const float labelY = a_center.y + Config::Styling::Item::Slot::Text::OffsetY + labelSize;
			Drawer::draw_text(labelX, labelY, "MISSING", C_SKYRIMWHITE, labelSize, labelArgs);
		}
		if (Config::MainWheel::ShowHandIndicator) {
			const SlotHandState handState = SlotHandIndicators::ComputeSlotHandState(_items, _selectedItem, a_imap, a_hands);
			if (handState.hasLeft || handState.hasRight) {
				Texture::Image slotBg = Texture::GetIconImage(Texture::icon_image_type::slot_background);
				const float bgScale = Config::Styling::Item::Slot::BackgroundTexture::Scale;
				const ImVec2 size(slotBg.width * bgScale, slotBg.height * bgScale);
				if (size.x > 0.0f && size.y > 0.0f) {
					const ImVec2 slotMin(a_center.x - size.x * 0.5f, a_center.y - size.y * 0.5f);
					const ImVec2 slotMax(a_center.x + size.x * 0.5f, a_center.y + size.y * 0.5f);
					SlotHandIndicators::DrawSlotHandIndicators(handState, slotMin, slotMax, a_slotOnRightSide, itemArgs);
				}
			}
		}
	} catch (std::exception& e) {
		logger::error("Exception in WheelEntry::drawSlot: {}", e.what());
	}
}

void WheelEntry::drawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs, bool a_isMissing)
{
	try {
		std::shared_lock<std::shared_mutex> lock(this->_lock);

		// Handle empty slot with entry-level missing state (for slots that were cleared but kept)
		if (_items.size() == 0) {
			if (_missingInInventory) {
				DrawArgs labelArgs = a_drawArgs;
				labelArgs.alphaMult *= 0.85f;
				const float labelSize = Config::Styling::Item::Highlight::Text::Size * 0.5f;
				const float labelX = a_center.x + Config::Styling::Item::Highlight::Text::OffsetX;
				const float labelY = a_center.y + Config::Styling::Item::Highlight::Text::OffsetY + labelSize;
				Drawer::draw_text(labelX, labelY, "MISSING", C_SKYRIMWHITE, labelSize, labelArgs);
			}
			return;  // nothing to draw
		}
		if (_selectedItem < 0 || _selectedItem >= _items.size()) {
			return;  // out of bounds selection, skip draw to avoid crash
		}
		
		const std::shared_ptr<WheelItem>& currentItem = _items[_selectedItem];
		if (!currentItem) {
			return;
		}
		
		// Reset visual state for this item - start fresh
		DrawArgs itemArgs = a_drawArgs;
		if (a_isMissing) {
			itemArgs.alphaMult *= 0.4f;
		}
		
		currentItem->DrawHighlight(a_center, a_imap, itemArgs);
		if (_items.size() > 1) {
			Drawer::draw_text(
				a_center.x + Config::Styling::Entry::Highlight::Text::OffsetX,
				a_center.y + Config::Styling::Entry::Highlight::Text::OffsetY,
				fmt::format("{} / {}", _selectedItem + 1, _items.size()).data(),
				C_SKYRIMWHITE,
				Config::Styling::Entry::Highlight::Text::Size,
				itemArgs);
		}
		if (a_isMissing) {
			DrawArgs labelArgs = a_drawArgs;
			labelArgs.alphaMult *= 0.85f;
			const float labelSize = Config::Styling::Item::Highlight::Text::Size * 0.5f;
			const float labelX = a_center.x + Config::Styling::Item::Highlight::Text::OffsetX;
			const float labelY = a_center.y + Config::Styling::Item::Highlight::Text::OffsetY + labelSize;
			Drawer::draw_text(labelX, labelY, "MISSING", C_SKYRIMWHITE, labelSize, labelArgs);
		}
	} catch (std::exception& e) {
		logger::error("Exception in WheelEntry::drawHighlight: {}", e.what());
	}
}

const float WheelEntry::GetRadiusMod()
{
	return this->_arcRadiusIncInterpolator.GetValue() + _arcRadiusBounceInterpolator.GetValue();
}

bool WheelEntry::IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	std::shared_lock<std::shared_mutex> lock(this->_lock);

	if (_items.size() == 0) {
		return false;  // nothing to draw
	}
	return _items[_selectedItem]->IsActive(a_inv);
}

bool WheelEntry::IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	std::shared_lock<std::shared_mutex> lock(this->_lock);

	if (_items.size() == 0) {
		return false;  // nothing to draw
	}
	return _items[_selectedItem]->IsAvailable(a_inv);
}

PreparedWheelItemActivation WheelEntry::ActivateItemSecondary(bool editMode)
{
	std::unique_lock<std::shared_mutex> lock(this->_lock);

	if (_items.size() == 0) {
		return {};
	}
	if (!editMode) {
		// Check if the selected item is missing - don't try to activate missing items
		const std::shared_ptr<WheelItem> item = _items[_selectedItem];
		if (!item || dynamic_cast<WheelItemMissing*>(item.get()) != nullptr) {
			return {};
		}
		// Block inventory-backed items not in player inventory
		if (item->IsInventoryBacked() && !item->IsInPlayerInventory()) {
			// Sound feedback removed for compatibility with newer CommonLibSSE-NG
			return {};
		}
		if (OStimIntegration::ShouldBlockRegularWheelActivation(item->GetItemTypeName())) {
			return {};
		}

		PreparedWheelItemActivation prepared{
			.selectedItem = item,
			.kind = WheelItemActivationKind::Secondary,
			.formID = item->GetFormID(),
			.itemIndex = _selectedItem,
			.executeAfterContainerUnlock = item->MayQueueTransientGameplayAction(),
			.isPrimaryForAPI = false,
			.accepted = true
		};
		if (prepared.executeAfterContainerUnlock) {
			_arcRadiusBounceInterpolator.InterpolateTo(Config::Animation::EntryInputBumpScale * (Config::Styling::Wheel::OuterCircleRadius - Config::Styling::Wheel::InnerCircleRadius), Config::Animation::EntryInputBumpTime);
			return prepared;
		}
		item->ActivateItemSecondary();
		_arcRadiusBounceInterpolator.InterpolateTo(Config::Animation::EntryInputBumpScale * (Config::Styling::Wheel::OuterCircleRadius - Config::Styling::Wheel::InnerCircleRadius), Config::Animation::EntryInputBumpTime);
		return prepared;
	} else {
		// remove selected item
		std::shared_ptr<WheelItem> itemToDelete = _items[_selectedItem];
		_items.erase(_items.begin() + _selectedItem);
		// move _selecteditem to the item immediately before the erased item
		if (_selectedItem > 0) {
			_selectedItem--;
		}
	}
	return {};
}

PreparedWheelItemActivation WheelEntry::ActivateItemPrimary(bool editMode)
{
	std::unique_lock<std::shared_mutex> lock(this->_lock);

	if (!editMode) { 
		if (_items.size() == 0) {
			return {};  // nothing to do
		}
		// Check if the selected item is missing - don't try to activate missing items
		const std::shared_ptr<WheelItem> item = _items[_selectedItem];
		if (!item || dynamic_cast<WheelItemMissing*>(item.get()) != nullptr) {
			return {};
		}
		// Block inventory-backed items not in player inventory
		const bool inventoryBacked = item->IsInventoryBacked();
		const bool inInventory = !inventoryBacked || item->IsInPlayerInventory();
		if (inventoryBacked && !inInventory) {
			// Sound feedback removed for compatibility with newer CommonLibSSE-NG
			return {};
		}
		if (OStimIntegration::ShouldBlockRegularWheelActivation(item->GetItemTypeName())) {
			return {};
		}
		PreparedWheelItemActivation prepared{
			.selectedItem = item,
			.kind = WheelItemActivationKind::Primary,
			.formID = item->GetFormID(),
			.itemIndex = _selectedItem,
			.executeAfterContainerUnlock = item->MayQueueTransientGameplayAction(),
			.isPrimaryForAPI = true,
			.accepted = true
		};
		if (prepared.executeAfterContainerUnlock) {
			_arcRadiusBounceInterpolator.InterpolateTo(Config::Animation::EntryInputBumpScale * (Config::Styling::Wheel::OuterCircleRadius - Config::Styling::Wheel::InnerCircleRadius), Config::Animation::EntryInputBumpTime);
			return prepared;
		}
		item->ActivateItemPrimary();
		_arcRadiusBounceInterpolator.InterpolateTo(Config::Animation::EntryInputBumpScale * (Config::Styling::Wheel::OuterCircleRadius - Config::Styling::Wheel::InnerCircleRadius), Config::Animation::EntryInputBumpTime);
		return prepared;
	} else {// append item to after _selectedItem index
		MainWheelDebug::Log(MainWheelDebug::Category::Input, 
			"BindAttempt: editMode=1 slotIdx={} currentItems={}", 
			_selectedItem, static_cast<int>(_items.size()));
		std::shared_ptr<WheelItem> newItem = WheelItemFactory::MakeWheelItemFromMenuHovered();
		if (newItem) {
			int insertIndex = _selectedItem;
			if (_selectedItem >= 0 && _selectedItem < static_cast<int>(_items.size()) &&
			    ShouldPreserveSelectedMutableInstance(_items[_selectedItem], newItem)) {
				insertIndex = (std::min)(_selectedItem + 1, static_cast<int>(_items.size()));
				if (MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Input)) {
					auto* currentMutable = dynamic_cast<WheelItemMutable*>(_items[_selectedItem].get());
					auto* newMutable = dynamic_cast<WheelItemMutable*>(newItem.get());
					MainWheelDebug::Log(
						MainWheelDebug::Category::Input,
						"BindAttempt: preserving selected mutable instance formId={:08X} currentUniqueID={} newUniqueID={} insertIdx={}",
						currentMutable ? currentMutable->GetFormID() : 0,
						currentMutable ? currentMutable->GetUniqueID() : 0,
						newMutable ? newMutable->GetUniqueID() : 0,
						insertIndex);
				}
			}
			_items.insert(_items.begin() + insertIndex, newItem);
			MainWheelDebug::Log(MainWheelDebug::Category::Input, 
				"BindResult: SUCCESS inserted at idx={}", insertIndex);
		} else {
			MainWheelDebug::Log(MainWheelDebug::Category::Input, 
				"BindResult: FAILED factory returned nullptr");
		}
	}
	return {};
}

PreparedWheelItemActivation WheelEntry::ActivateItemSpecial(bool editMode)
{
	std::unique_lock<std::shared_mutex> lock(this->_lock);
	if (editMode || _items.size() == 0) {
		return {}; // nothing to do
	}
	// Check if the selected item is missing - don't try to activate missing items
	const std::shared_ptr<WheelItem> item = _items[_selectedItem];
	if (!item || dynamic_cast<WheelItemMissing*>(item.get()) != nullptr) {
		return {};
	}
	// Block inventory-backed items not in player inventory
	if (item->IsInventoryBacked() && !item->IsInPlayerInventory()) {
		// Sound feedback removed for compatibility with newer CommonLibSSE-NG
		return {};
	}
	if (OStimIntegration::ShouldBlockRegularWheelActivation(item->GetItemTypeName())) {
		return {};
	}
	PreparedWheelItemActivation prepared{
		.selectedItem = item,
		.kind = WheelItemActivationKind::Special,
		.formID = item->GetFormID(),
		.itemIndex = _selectedItem,
		.executeAfterContainerUnlock = item->MayQueueTransientGameplayAction(),
		.isPrimaryForAPI = true,
		.accepted = true
	};
	if (prepared.executeAfterContainerUnlock) {
		return prepared;
	}
	item->ActivateItemSpecial();
	return prepared;
}

void WheelEntry::PrevItem()
{
	std::unique_lock<std::shared_mutex> lock(this->_lock);

	_selectedItem--;
	if (_selectedItem < 0) {
		if (_items.size() > 1) {
			_selectedItem = _items.size() - 1;
		} else {
			_selectedItem = 0;
		}
	}
	// Sound feedback removed for compatibility with newer CommonLibSSE-NG
}

void WheelEntry::NextItem()
{
	std::unique_lock<std::shared_mutex> lock(this->_lock);

	_selectedItem++;
	if (_selectedItem >= _items.size()) {
		_selectedItem = 0;
	}
	// Sound feedback removed for compatibility with newer CommonLibSSE-NG
}

bool WheelEntry::IsEmpty()
{
	std::shared_lock<std::shared_mutex> lock(this->_lock);
	return this->_items.empty();
}

int WheelEntry::GetNumItems()
{
	std::shared_lock<std::shared_mutex> lock(this->_lock);
	return this->_items.size();
}

bool WheelEntry::IsMissingInInventory() const
{
	std::shared_lock<std::shared_mutex> lock(this->_lock);
	return _missingInInventory;
}

MissingCategory WheelEntry::GetMissingCategory() const
{
	std::shared_lock<std::shared_mutex> lock(this->_lock);
	return _missingCategory;
}

void WheelEntry::SetMissingState(bool missing, MissingCategory category)
{
	std::unique_lock<std::shared_mutex> lock(this->_lock);
	_missingInInventory = missing;
	_missingCategory = category;
}

bool WheelEntry::ReplaceSelectedItem(std::shared_ptr<WheelItem> item)
{
	if (!item) {
		return false;
	}
	std::unique_lock<std::shared_mutex> lock(this->_lock);
	if (_items.empty() || _selectedItem < 0 || _selectedItem >= _items.size()) {
		return false;
	}
	_items[_selectedItem] = std::move(item);
	return true;
}

bool WheelEntry::ReplaceItemAt(int index, std::shared_ptr<WheelItem> item)
{
	if (!item) {
		return false;
	}
	std::unique_lock<std::shared_mutex> lock(this->_lock);
	if (_items.empty() || index < 0 || index >= static_cast<int>(_items.size())) {
		return false;
	}
	_items[index] = std::move(item);
	return true;
}

void WheelEntry::ClearDepletedConsumables()
{
	std::unique_lock<std::shared_mutex> lock(this->_lock);
	if (_items.empty()) {
		return;
	}
	if (IsKeepMissingCategoryEnabled(MissingCategory::Consumable)) {
		return;
	}

	bool anyRemoved = false;
	for (auto it = _items.begin(); it != _items.end();) {
		const std::shared_ptr<WheelItem> item = *it;
		if (std::shared_ptr<WheelItemAlchemy> alch = std::dynamic_pointer_cast<WheelItemAlchemy>(item)) {
			if (alch->IsDepletedInPlayerInventory()) {
				const int removedIndex = static_cast<int>(std::distance(_items.begin(), it));
				it = _items.erase(it);
				anyRemoved = true;

				if (_selectedItem > removedIndex) {
					_selectedItem--;
				} else if (_selectedItem == removedIndex && _selectedItem > 0) {
					_selectedItem--;
				}
				continue;
			}
		}
		if (std::shared_ptr<WheelItemIngredient> ingredient = std::dynamic_pointer_cast<WheelItemIngredient>(item)) {
			if (ingredient->IsDepletedInPlayerInventory()) {
				const int removedIndex = static_cast<int>(std::distance(_items.begin(), it));
				it = _items.erase(it);
				anyRemoved = true;

				if (_selectedItem > removedIndex) {
					_selectedItem--;
				} else if (_selectedItem == removedIndex && _selectedItem > 0) {
					_selectedItem--;
				}
				continue;
			}
		}
		++it;
	}

	if (!anyRemoved) {
		return;
	}
	if (_items.empty()) {
		_selectedItem = 0;
		_missingInInventory = false;
		_missingCategory = MissingCategory::Unknown;
		return;
	}
	_selectedItem = std::clamp(_selectedItem, 0, static_cast<int>(_items.size()) - 1);
}

void WheelEntry::ClearAllItems()
{
	std::unique_lock<std::shared_mutex> lock(this->_lock);

	// Clear all items from this entry
	_items.clear();
	_selectedItem = 0;
	_missingInInventory = false;
	_missingCategory = MissingCategory::Unknown;

	// Reset animation state for this now-empty slot
	_prevHovered = false;
	_lastHighlightExpandSize = std::numeric_limits<float>::quiet_NaN();
	_lastHighlightOuterAngleInc = std::numeric_limits<float>::quiet_NaN();
	_lastHighlightInnerAngleInc = std::numeric_limits<float>::quiet_NaN();
	_arcRadiusIncInterpolator.InterpolateTo(0, 0.1f);
	_arcInnerAngleIncInterpolator.InterpolateTo(0, 0.1f);
	_arcOuterAngleIncInterpolator.InterpolateTo(0, 0.1f);
}

// ============================================================================
// External API Accessors
// ============================================================================

WheelItem* WheelEntry::GetItem(int a_index)
{
	std::shared_lock<std::shared_mutex> lock(this->_lock);
	if (a_index < 0 || a_index >= static_cast<int>(_items.size())) {
		return nullptr;
	}
	return _items[a_index].get();
}

bool WheelEntry::RemoveItemAt(int a_index)
{
	std::unique_lock<std::shared_mutex> lock(this->_lock);
	if (a_index < 0 || a_index >= static_cast<int>(_items.size())) {
		return false;
	}
	_items.erase(_items.begin() + a_index);
	
	// Adjust selected item index if needed
	if (_items.empty()) {
		_selectedItem = 0;
	} else if (_selectedItem >= static_cast<int>(_items.size())) {
		_selectedItem = static_cast<int>(_items.size()) - 1;
	}
	return true;
}

std::shared_ptr<WheelItem> WheelEntry::GetSelectedItem()
{
	std::shared_lock<std::shared_mutex> lock(this->_lock);
	if (_items.empty()) {
		return {};
	}
	if (_selectedItem < 0 || _selectedItem >= _items.size()) {
		return {};
	}
	return _items[_selectedItem];
}


WheelEntry::WheelEntry()
{
	_selectedItem = 0;
}

void WheelEntry::PushItem(std::shared_ptr<WheelItem> item)
{
	std::unique_lock<std::shared_mutex> lock(this->_lock);
	this->_items.push_back(item);
}

WheelEntry::~WheelEntry()
{
	_items.clear();
}

int WheelEntry::GetSelectedItemIndex()
{
	std::shared_lock<std::shared_mutex> lock(this->_lock);
	return this->_selectedItem;
}

void WheelEntry::SerializeIntoJsonObj(nlohmann::json& j_entry)
{
	// setup for entry
	j_entry["items"] = nlohmann::json::array();
	for (std::shared_ptr<WheelItem> item : this->_items) {
		nlohmann::json j_item;
		item->SerializeIntoJsonObj(j_item);
		j_item["missingCategory"] = MissingCategoryToInt(item->GetMissingCategory());
		const char* itemName = item->GetItemName();
		if (itemName && itemName[0] != '\0') {
			j_item["missingName"] = itemName;
		}
		j_entry["items"].push_back(j_item);
	}
	j_entry["selecteditem"] = this->_selectedItem;
}

std::unique_ptr<WheelEntry> WheelEntry::SerializeFromJsonObj(const nlohmann::json& j_entry, SKSE::SerializationInterface* a_intfc)
{
	std::unique_ptr<WheelEntry> entry = std::make_unique<WheelEntry>();

	if (!j_entry.contains("items") || !j_entry["items"].is_array()) {
		logger::warn("Deserialize: entry missing 'items' array, creating empty entry");
		return entry;
	}
	
	const nlohmann::json& j_items = j_entry["items"];
	
	// Bounds check: truncate to max items instead of clearing (preserves user data)
	const std::size_t maxItems = static_cast<std::size_t>(Config::WheelBehavior::MaxItemsPerSlot);
	const std::size_t itemCount = j_items.size();
	const std::size_t loadCount = (std::min)(itemCount, maxItems);
	if (itemCount > maxItems) {
		logger::warn("Deserialize: item count {} exceeds max {}, truncating to {} items (excess items will be lost)",
			itemCount, maxItems, loadCount);
	}
	
	std::size_t loaded = 0;
	for (const auto& j_item : j_items) {
		if (loaded >= loadCount) {
			break;
		}
		try {
			std::shared_ptr<WheelItem> item = WheelItemFactory::MakeWheelItemFromJsonObject(j_item, a_intfc);
			if (item) {
				entry->PushItem(std::move(item));
			}
		} catch (const std::exception& e) {
			logger::warn("Deserialize: failed to load wheel item: {}", e.what());
		}
		++loaded;
	}
	
	// Safely get selecteditem with bounds checking
	int selectedItem = 0;
	if (j_entry.contains("selecteditem")) {
		try {
			selectedItem = j_entry["selecteditem"].get<int>();
		} catch (const std::exception& e) {
			logger::warn("Deserialize: failed to read selecteditem: {}", e.what());
			selectedItem = 0;
		}
	}
	const int maxIdx = entry->GetNumItems() > 0 ? entry->GetNumItems() - 1 : 0;
	selectedItem = std::clamp(selectedItem, 0, maxIdx);
	entry->SetSelectedItem(selectedItem);

	return entry;
}

void WheelEntry::ResetAnimation()
{
	_arcInnerAngleIncInterpolator.ForceFinish();
	_arcOuterAngleIncInterpolator.ForceFinish();
	_arcRadiusIncInterpolator.ForceFinish();
	_arcInnerAngleIncInterpolator.ForceValue(0);
	_arcOuterAngleIncInterpolator.ForceValue(0);
	_arcRadiusIncInterpolator.ForceValue(0);
	// don't need call interpolateto() because bounce interpolator's forceFinish() goes back to its fixed starting value (which is where we want it to be at)
	_arcRadiusBounceInterpolator.ForceFinish();
	this->_prevHovered = false;
	_lastHighlightExpandSize = std::numeric_limits<float>::quiet_NaN();
	_lastHighlightOuterAngleInc = std::numeric_limits<float>::quiet_NaN();
	_lastHighlightInnerAngleInc = std::numeric_limits<float>::quiet_NaN();
}


void WheelEntry::SetSelectedItem(int a_selected)
{
	std::unique_lock<std::shared_mutex> lock(this->_lock);
	this->_selectedItem = a_selected;
}
