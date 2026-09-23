// WheelItemShout.cpp  (instrumented for shout pipeline debugging)

#include "bin/Rendering/Drawer.h"
#include "bin/Rendering/TextureManager.h"
#include "bin/Wheeler/TransformWheelManager.h"
#include "bin/Wheeler/Wheeler.h"
#include "WheelItemShout.h"
#include "bin/Utilities/InventorySnapshotCache.h"

#include "bin/Wheeler/ShoutUtils.h"

#include <fmt/format.h>
#include <cmath>
#include <string>

static ImFont* GetCooldownTimerFont()
{
	const std::uint32_t idx = Config::Cooldowns::TimerText::FontIndex;
	if (idx == 0) {
		return ImGui::GetFont();
	}

	ImGuiIO& io = ImGui::GetIO();
	const std::uint32_t atlasIndex = idx - 1;
	if (io.Fonts && atlasIndex < static_cast<std::uint32_t>(io.Fonts->Fonts.Size) && io.Fonts->Fonts[atlasIndex]) {
		return io.Fonts->Fonts[atlasIndex];
	}
	return ImGui::GetFont();
}

// Debug helper. Centralizes shout pipeline dumps for this file.
// Enable via ShoutUtils::SetShoutPipelineDebugEnabled(true) somewhere in your init/config path.
static void ShoutDbg_Dump(RE::TESShout* shout, RE::PlayerCharacter* pc, const char* tag, const std::string& reason)
{
	if (!ShoutUtils::IsShoutPipelineDebugEnabled()) {
		return;
	}
	ShoutUtils::DebugDumpShoutPipeline(shout, pc, tag, reason.empty() ? nullptr : reason.c_str());
}

static bool IsShoutBlockedByTransformGuard(RE::TESShout* shout, const char* sourceLabel)
{
	if (!shout) {
		return false;
	}
	if (!TransformWheelManager::IsShoutActivationBlocked(shout, sourceLabel)) {
		return false;
	}

	logger::info("TransformWheels: blocked shout activation source={} formId={:08X} edid='{}' name='{}'",
		sourceLabel ? sourceLabel : "",
		shout->GetFormID(),
		shout->GetFormEditorID() ? shout->GetFormEditorID() : "",
		shout->GetName() ? shout->GetName() : "");
	return true;
}

WheelItemShout::WheelItemShout(RE::TESShout* a_shout)
{
	this->_shout = a_shout;
	this->_texture = Texture::GetIconImage(Texture::icon_image_type::shout, a_shout);
	RE::BSString descriptionBuf = "";
	this->_shout->GetDescription(descriptionBuf, nullptr);
	this->_description = descriptionBuf.c_str();

	if (ShoutUtils::IsShoutPipelineDebugEnabled()) {
		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		const std::string reason = fmt::format("ctor shout={:08X} name='{}'", _shout ? _shout->GetFormID() : 0, _shout ? _shout->GetName() : "(null)");
		ShoutDbg_Dump(_shout, pc, "WheelItemShout:Ctor", reason);
	}
}

void WheelItemShout::DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	const bool cooldownsEnabled = Config::Cooldowns::Enabled;
	const float remainingPercent = cooldownsEnabled ? this->GetCooldownPercent() : 0.0f;
	const float progress = std::clamp(1.0f - remainingPercent, 0.0f, 1.0f);

	// Desired visual: slot content is dimmed on cooldown, then gradually restores from bottom-to-top.
	// Reskin slot background is not touched; we only re-draw the slot's own content (text + icon).
	if (cooldownsEnabled && remainingPercent > 0.0f && progress < 1.0f) {
		const float dimStrength = std::clamp(Config::Cooldowns::ContentDimAlpha, 0.0f, 1.0f);
		const float dimFactor = std::clamp(1.0f - dimStrength, 0.0f, 1.0f);

		DrawArgs dimArgs = a_drawArgs;
		dimArgs.alphaMult *= dimFactor;

		// Draw fully-dimmed content first.
		this->drawSlotText(a_center, this->_shout->GetName(), dimArgs);
		this->drawSlotTexture(a_center, dimArgs);

		// Then restore the normal content from bottom-to-top based on progress.
		Texture::Image slotBg = Texture::GetIconImage(Texture::icon_image_type::slot_background);
		const float bgScale = Config::Styling::Item::Slot::BackgroundTexture::Scale;
		ImVec2 size(slotBg.width * bgScale, slotBg.height * bgScale);
		if (size.x <= 0.0f || size.y <= 0.0f) {
			// Fallback to icon bounds if slot background isn't available for sizing.
			const float iconScale = Config::Styling::Item::Slot::Texture::Scale;
			size = ImVec2(_texture.width * iconScale, _texture.height * iconScale);
		}

		const ImVec2 posMin(a_center.x - size.x * 0.5f, a_center.y - size.y * 0.5f);
		const ImVec2 posMax(a_center.x + size.x * 0.5f, a_center.y + size.y * 0.5f);
		const float fillHeight = size.y * progress;
		const ImVec2 clipMin(posMin.x, posMax.y - fillHeight);
		const ImVec2 clipMax(posMax.x, posMax.y);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(clipMin, clipMax, true);
		this->drawSlotText(a_center, this->_shout->GetName(), a_drawArgs);
		this->drawSlotTexture(a_center, a_drawArgs);
		drawList->PopClipRect();

		if (Config::Cooldowns::ShowTimer) {
			const float remainingSeconds = this->GetCooldownRemainingSeconds();
			if (remainingSeconds > 0.0f) {
				const std::string txt = fmt::format("{:.0f}s", std::ceil(remainingSeconds));
				DrawArgs timerArgs = a_drawArgs;
				const float fontSize = Config::Cooldowns::TimerText::Size > 0.0f ?
				                           Config::Cooldowns::TimerText::Size :
				                           (std::max)(14.0f, Config::Styling::Item::Slot::Text::Size * 0.85f);
				Drawer::draw_text_with_font(a_center.x, a_center.y, txt.c_str(), Config::Cooldowns::TimerText::Color, GetCooldownTimerFont(), fontSize, timerArgs, true);
			}
		}
		return;
	}

	// Default: no cooldown, or fully restored => draw normally.
	this->drawSlotText(a_center, this->_shout->GetName(), a_drawArgs);
	this->drawSlotTexture(a_center, a_drawArgs);
}

void WheelItemShout::DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	const float textShiftY = calculateHighlightTextShiftY(this->_description.c_str());
	this->drawHighlightText(a_center, this->_shout->GetName(), a_drawArgs, textShiftY);
	this->drawHighlightTexture(a_center, a_drawArgs);
	if (!this->_description.empty()) {
		this->drawHighlightDescription(a_center, this->_description.data(), a_drawArgs, textShiftY);
	}
}

bool WheelItemShout::IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return false;
	}
	RE::TESForm* selectedPower = pc->GetActorRuntimeData().selectedPower;
	if (selectedPower) {
		return selectedPower->GetFormID() == this->_shout->GetFormID();
	}
	return false;
}

bool WheelItemShout::IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	return false;
}

// TODO: check if shout's been unlocked, block equipment if not unlocked.
void WheelItemShout::ActivateItemSecondary()
{
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}
	RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
	if (!aeMan) {
		return;
	}
	if (IsShoutBlockedByTransformGuard(this->_shout, "EquipSecondary")) {
		return;
	}

	RE::TESForm* selectedPower = pc->GetActorRuntimeData().selectedPower;

	if (ShoutUtils::IsShoutPipelineDebugEnabled()) {
		const std::string reason = fmt::format("secondary selectedPower={:08X} this={:08X}",
			selectedPower ? selectedPower->GetFormID() : 0, _shout ? _shout->GetFormID() : 0);
		ShoutDbg_Dump(_shout, pc, "WheelItemShout:ActivateSecondary:Before", reason);
	}

	if (selectedPower && selectedPower->GetFormID() == this->_shout->GetFormID()) {
		pc->GetActorRuntimeData().selectedPower = nullptr;
		if (ShoutUtils::IsShoutPipelineDebugEnabled()) {
			ShoutDbg_Dump(_shout, pc, "WheelItemShout:ActivateSecondary:Unequip", "selectedPower set to nullptr");
		}
	} else {
		InventorySnapshotCache::EquipShout(aeMan, pc, this->_shout);
		if (ShoutUtils::IsShoutPipelineDebugEnabled()) {
			ShoutDbg_Dump(_shout, pc, "WheelItemShout:ActivateSecondary:Equip", "ActorEquipManager::EquipShout called");
		}
	}
}

void WheelItemShout::ActivateItemPrimary()
{
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}
	RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
	if (!aeMan) {
		return;
	}
	if (IsShoutBlockedByTransformGuard(this->_shout, "EquipPrimary")) {
		return;
	}

	RE::TESForm* selectedPower = pc->GetActorRuntimeData().selectedPower;

	if (ShoutUtils::IsShoutPipelineDebugEnabled()) {
		const std::string reason = fmt::format("primary selectedPower={:08X} this={:08X}",
			selectedPower ? selectedPower->GetFormID() : 0, _shout ? _shout->GetFormID() : 0);
		ShoutDbg_Dump(_shout, pc, "WheelItemShout:ActivatePrimary:Before", reason);
	}

	if (selectedPower && selectedPower->GetFormID() == this->_shout->GetFormID()) {
		pc->GetActorRuntimeData().selectedPower = nullptr;
		if (ShoutUtils::IsShoutPipelineDebugEnabled()) {
			ShoutDbg_Dump(_shout, pc, "WheelItemShout:ActivatePrimary:Unequip", "selectedPower set to nullptr");
		}
	} else {
		InventorySnapshotCache::EquipShout(aeMan, pc, this->_shout);
		if (ShoutUtils::IsShoutPipelineDebugEnabled()) {
			ShoutDbg_Dump(_shout, pc, "WheelItemShout:ActivatePrimary:Equip", "ActorEquipManager::EquipShout called");
		}
	}
}

void WheelItemShout::ActivateItemSpecial()
{
}

float WheelItemShout::GetCooldownRemainingSeconds() const
{
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return 0.0f;
	}
	const float remainingSeconds = pc->GetVoiceRecoveryTime();
	return remainingSeconds > 0.0f ? remainingSeconds : 0.0f;
}

float WheelItemShout::GetCooldownTotalSeconds() const
{
	if (!_shout) {
		return 0.0f;
	}
	const auto& var = _shout->variations[0];
	return (std::max)(0.0f, var.recoveryTime);
}

void WheelItemShout::SerializeIntoJsonObj(nlohmann::json& a_json)
{
	a_json["type"] = WheelItemShout::ITEM_TYPE_STR;
	a_json["formID"] = this->_shout->GetFormID();
}

bool WheelItemShout::CastImmediate(float hoverTime)
{
	return tryCastImmediate(hoverTime);
}

bool WheelItemShout::tryCastImmediate(float hoverTime)
{
	const RE::FormID shoutID = _shout ? _shout->GetFormID() : 0;
	logger::critical("[SHOUT_MARK] tryCastImmediate enter shout={:08X}", shoutID);

	if (!_shout) {
		logger::warn("WheelItemShout::tryCastImmediate: no shout");
		return false;
	}

	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		logger::warn("WheelItemShout::tryCastImmediate: no player");
		return false;
	}
	if (IsShoutBlockedByTransformGuard(_shout, "CastImmediate")) {
		return false;
	}

	if (ShoutUtils::IsShoutPipelineDebugEnabled()) {
		const float vr = pc->GetVoiceRecoveryTime();
		const float total = this->GetCooldownTotalSeconds();
		const std::string reason = fmt::format("enter hoverTime={:.3f} voiceRecovery={:.3f} totalRecoveryVar0={:.3f}",
			hoverTime, vr, total);
		ShoutDbg_Dump(_shout, pc, "WheelItemShout:TryCastImmediate:Enter", reason);
	}

	// Check if shout is on cooldown
	float cooldownRemaining = pc->GetVoiceRecoveryTime();
	if (cooldownRemaining > 0.0f) {
		logger::info("WheelItemShout::tryCastImmediate: shout on cooldown ({:.1f}s remaining)", cooldownRemaining);

		if (ShoutUtils::IsShoutPipelineDebugEnabled()) {
			const std::string reason = fmt::format("blocked cooldownRemaining={:.3f}", cooldownRemaining);
			ShoutDbg_Dump(_shout, pc, "WheelItemShout:TryCastImmediate:BlockedCooldown", reason);
		}
		return false;
	}

	// Queue the shout activation to happen after the wheel closes
	// hoverTime is used to determine word level selection
	logger::info("WheelItemShout::tryCastImmediate: queueing shout {} ({:08X}) with hoverTime={:.2f}s",
		_shout->GetName(), _shout->GetFormID(), hoverTime);

	if (ShoutUtils::IsShoutPipelineDebugEnabled()) {
		const std::string reason = fmt::format("queue shoutFormID={:08X} hoverTime={:.3f}", _shout->GetFormID(), hoverTime);
		ShoutDbg_Dump(_shout, pc, "WheelItemShout:TryCastImmediate:Queue", reason);
	}

	const bool queued = Wheeler::QueueShoutActivation(_shout->GetFormID(), hoverTime);

	if (ShoutUtils::IsShoutPipelineDebugEnabled()) {
		ShoutDbg_Dump(_shout, pc, "WheelItemShout:TryCastImmediate:Exit", queued ? "queued" : "queue_rejected");
	}

	return queued;
}
