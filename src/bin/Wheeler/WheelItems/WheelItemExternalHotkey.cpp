#include "WheelItemExternalHotkey.h"

#include "bin/Rendering/Drawer.h"
#include "bin/UserInput/Controls.h"
#include "bin/Wheeler/Wheeler.h"

#include <algorithm>
#include <cmath>
#include <utility>

WheelItemExternalHotkey::WheelItemExternalHotkey(
	std::string a_displayName,
	std::uint32_t a_scanCode,
	std::uint32_t a_modifier,
	std::string a_iconPath,
	std::uint32_t a_iconTintARGB,
	std::uint32_t a_sourceSlotIndex,
	std::string a_sourceTag,
	bool a_hasResolvedIcon) :
	_displayName(std::move(a_displayName)),
	_scanCode(a_scanCode),
	_modifier(a_modifier),
	_iconPath(std::move(a_iconPath)),
	_iconTintARGB(a_iconTintARGB),
	_sourceSlotIndex(a_sourceSlotIndex),
	_sourceTag(std::move(a_sourceTag)),
	_hasResolvedIcon(a_hasResolvedIcon)
{}

void WheelItemExternalHotkey::DrawSlot(ImVec2 a_center, bool, RE::TESObjectREFR::InventoryItemMap&, DrawArgs a_drawArgs)
{
	drawSlotText(a_center, _displayName.c_str(), a_drawArgs);
	_texture = ResolveIcon();
	if (_texture.texture) {
		Drawer::draw_texture(
			_texture.texture,
			a_center,
			Config::Styling::Item::Slot::Texture::OffsetX,
			Config::Styling::Item::Slot::Texture::OffsetY,
			ComputeDrawSize(_texture, false),
			_iconTintARGB,
			a_drawArgs);
	}
}

void WheelItemExternalHotkey::DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap&, DrawArgs a_drawArgs)
{
	const std::string hotkeyLabel = BuildHotkeyLabel();
	const float textShiftY = calculateHighlightTextShiftY(hotkeyLabel.c_str());
	drawHighlightText(a_center, _displayName.c_str(), a_drawArgs, textShiftY);
	_texture = ResolveIcon();
	if (_texture.texture) {
		Drawer::draw_texture(
			_texture.texture,
			a_center,
			Config::Styling::Item::Highlight::Texture::OffsetX,
			Config::Styling::Item::Highlight::Texture::OffsetY,
			ComputeDrawSize(_texture, true),
			_iconTintARGB,
			a_drawArgs);
	}

	if (!hotkeyLabel.empty()) {
		drawHighlightDescription(a_center, hotkeyLabel.c_str(), a_drawArgs, textShiftY);
	}
}

bool WheelItemExternalHotkey::IsActive(RE::TESObjectREFR::InventoryItemMap&)
{
	return false;
}

bool WheelItemExternalHotkey::IsAvailable(RE::TESObjectREFR::InventoryItemMap&)
{
	return true;
}

void WheelItemExternalHotkey::ActivateItemPrimary()
{
	Wheeler::QueueExternalHotkeyDispatch(_scanCode, _modifier, _displayName, _sourceSlotIndex, _sourceTag);
}

void WheelItemExternalHotkey::ActivateItemSecondary()
{
	if (!Config::ActionHotkeysBridge::MirrorSecondaryActivate) {
		return;
	}

	ActivateItemPrimary();
}

void WheelItemExternalHotkey::ActivateItemSpecial()
{
	if (!Config::ActionHotkeysBridge::MirrorSpecialActivate) {
		return;
	}

	ActivateItemPrimary();
}

void WheelItemExternalHotkey::SerializeIntoJsonObj(nlohmann::json& a_json)
{
	a_json["type"] = ITEM_TYPE_STR;
	a_json["formID"] = 0;
	a_json["displayName"] = _displayName;
	a_json["scanCode"] = _scanCode;
	a_json["modifier"] = _modifier;
	a_json["iconPath"] = _iconPath;
	a_json["iconTintARGB"] = _iconTintARGB;
	a_json["sourceSlotIndex"] = _sourceSlotIndex;
	a_json["sourceTag"] = _sourceTag;
	a_json["hasResolvedIcon"] = _hasResolvedIcon;
}

std::string WheelItemExternalHotkey::BuildHotkeyLabel() const
{
	const std::string keyName = Controls::GetKeyNameForMkb(_scanCode);
	if (keyName.empty() || keyName == "Unbound") {
		return _displayName.empty() ? std::string{} : fmt::format("Hotkey: {}", _scanCode);
	}

	if (_modifier == 0) {
		return fmt::format("Hotkey: {}", keyName);
	}

	const std::string modifierName = Controls::GetKeyNameForMkb(_modifier);
	if (modifierName.empty() || modifierName == "Unbound") {
		return fmt::format("Hotkey: {}", keyName);
	}

	return fmt::format("Hotkey: {} + {}", modifierName, keyName);
}

ImVec2 WheelItemExternalHotkey::ComputeDrawSize(const Texture::Image& a_texture, bool a_highlight) const
{
	const float fallbackScale = a_highlight ?
		Config::Styling::Item::Highlight::Texture::Scale :
		Config::Styling::Item::Slot::Texture::Scale;
	ImVec2 drawSize(a_texture.width * fallbackScale, a_texture.height * fallbackScale);
	if (a_texture.width <= 0 || a_texture.height <= 0) {
		return drawSize;
	}

	Texture::Image slotBg = Texture::GetIconImage(Texture::icon_image_type::slot_background);
	const float bgScale = Config::Styling::Item::Slot::BackgroundTexture::Scale;
	ImVec2 fitBox(slotBg.width * bgScale, slotBg.height * bgScale);
	if (a_highlight) {
		fitBox.x *= 1.15f;
		fitBox.y *= 1.15f;
	}
	if (fitBox.x <= 0.0f || fitBox.y <= 0.0f) {
		return drawSize;
	}

	const float fitScale = (std::min)(
		fitBox.x / static_cast<float>(a_texture.width),
		fitBox.y / static_cast<float>(a_texture.height));
	if (!std::isfinite(fitScale) || fitScale <= 0.0f) {
		return drawSize;
	}

	const float fillRatio = a_highlight ? 0.72f : 0.58f;
	const float finalScale = (std::max)(fallbackScale, fitScale * fillRatio);
	return ImVec2(
		static_cast<float>(a_texture.width) * finalScale,
		static_cast<float>(a_texture.height) * finalScale);
}

Texture::Image WheelItemExternalHotkey::ResolveIcon()
{
	if (!_hasResolvedIcon || _iconPath.empty()) {
		return {};
	}

	return Texture::GetExternalRasterImage(_iconPath);
}
