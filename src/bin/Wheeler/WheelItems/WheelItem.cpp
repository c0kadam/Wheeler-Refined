#include "WheelItem.h"
#include "bin/Rendering/Drawer.h"
#include "bin/Rendering/TextureManager.h"
#include "bin/Integrations/I4/I4IconProvider.h"
#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cmath>
#include "bin/Config.h"

namespace
{
	struct WrappedTextLayout
	{
		std::vector<std::string> lines;
		float fontSize = 0.0f;
		float totalHeight = 0.0f;
		float maxLineWidth = 0.0f;
	};

	struct SingleLineTextLayout
	{
		std::string text;
		float fontSize = 0.0f;
		float width = 0.0f;
	};

	void TrimLastUtf8Codepoint(std::string& text)
	{
		if (text.empty()) {
			return;
		}

		std::size_t pos = text.size() - 1;
		while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) {
			--pos;
		}
		text.erase(pos);
	}

	float MeasureTextWidth(ImFont* font, float fontSize, const std::string& text)
	{
		if (!font || text.empty() || fontSize <= 0.0f) {
			return 0.0f;
		}

		return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str()).x;
	}

	float MeasureLineHeight(ImFont* font, float fontSize)
	{
		if (!font || fontSize <= 0.0f) {
			return 0.0f;
		}

		return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, "Ag").y;
	}

	void UpdateWrappedLayoutMetrics(WrappedTextLayout& layout, ImFont* font, float lineSpacing)
	{
		const float lineHeight = MeasureLineHeight(font, layout.fontSize);
		if (layout.lines.empty() || lineHeight <= 0.0f) {
			layout.totalHeight = 0.0f;
			layout.maxLineWidth = 0.0f;
			return;
		}

		layout.maxLineWidth = 0.0f;
		for (const auto& line : layout.lines) {
			layout.maxLineWidth = (std::max)(layout.maxLineWidth, MeasureTextWidth(font, layout.fontSize, line));
		}

		layout.totalHeight = lineHeight * static_cast<float>(layout.lines.size());
		if (layout.lines.size() > 1) {
			layout.totalHeight += lineSpacing * static_cast<float>(layout.lines.size() - 1);
		}
	}

	std::string TruncateTextToWidth(const std::string& text, ImFont* font, float fontSize, float maxWidth, std::string_view suffix = "...")
	{
		if (text.empty() || !font || fontSize <= 0.0f || maxWidth <= 0.0f) {
			return text;
		}

		if (MeasureTextWidth(font, fontSize, text) <= maxWidth) {
			return text;
		}

		const std::string suffixText(suffix);
		const float suffixWidth = MeasureTextWidth(font, fontSize, suffixText);
		if (!suffixText.empty() && suffixWidth > maxWidth) {
			return TruncateTextToWidth(suffixText, font, fontSize, maxWidth, "");
		}

		std::string truncated = text;
		while (!truncated.empty()) {
			TrimLastUtf8Codepoint(truncated);
			const std::string candidate = truncated + suffixText;
			if (candidate.empty() || MeasureTextWidth(font, fontSize, candidate) <= maxWidth) {
				return candidate;
			}
		}

		return suffixText;
	}

	std::vector<std::string> SplitWords(const std::string& text)
	{
		std::vector<std::string> words;
		std::size_t pos = 0;
		while (pos < text.size()) {
			while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
				++pos;
			}
			const std::size_t start = pos;
			while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) == 0) {
				++pos;
			}
			if (start < pos) {
				words.push_back(text.substr(start, pos - start));
			}
		}
		return words;
	}

	std::string Utf8Prefix(std::string_view text, std::size_t codepointCount)
	{
		if (codepointCount == 0 || text.empty()) {
			return {};
		}

		std::size_t bytePos = 0;
		std::size_t seen = 0;
		while (bytePos < text.size() && seen < codepointCount) {
			unsigned char c = static_cast<unsigned char>(text[bytePos]);
			std::size_t advance = 1;
			if ((c & 0x80) == 0x00) {
				advance = 1;
			} else if ((c & 0xE0) == 0xC0) {
				advance = 2;
			} else if ((c & 0xF0) == 0xE0) {
				advance = 3;
			} else if ((c & 0xF8) == 0xF0) {
				advance = 4;
			}
			bytePos = (std::min)(text.size(), bytePos + advance);
			++seen;
		}

		return std::string(text.substr(0, bytePos));
	}

	WrappedTextLayout WrapTextToWidth(const std::string& text, ImFont* font, float fontSize, float maxWidth, int maxLines, float lineSpacing)
	{
		WrappedTextLayout layout;
		layout.fontSize = fontSize;

		if (text.empty()) {
			return layout;
		}

		if (!font || fontSize <= 0.0f || maxWidth <= 0.0f || maxLines <= 0) {
			layout.lines.push_back(text);
			UpdateWrappedLayoutMetrics(layout, font, lineSpacing);
			return layout;
		}

		bool reachedLimit = false;
		std::size_t paragraphStart = 0;
		while (paragraphStart <= text.size() && !reachedLimit) {
			const std::size_t paragraphEnd = text.find('\n', paragraphStart);
			const bool hasMoreParagraphs = paragraphEnd != std::string::npos;
			const std::string paragraph = text.substr(
				paragraphStart,
				hasMoreParagraphs ? paragraphEnd - paragraphStart : std::string::npos);

			if (paragraph.empty()) {
				if (static_cast<int>(layout.lines.size()) >= maxLines) {
					reachedLimit = true;
					break;
				}
				layout.lines.emplace_back("");
			} else {
				const auto words = SplitWords(paragraph);
				std::string currentLine;
				for (std::size_t i = 0; i < words.size(); ++i) {
					const std::string& word = words[i];
					const std::string candidate = currentLine.empty() ? word : currentLine + " " + word;
					if (MeasureTextWidth(font, fontSize, candidate) <= maxWidth) {
						currentLine = candidate;
						continue;
					}

					if (static_cast<int>(layout.lines.size()) >= maxLines - 1) {
						std::string remainder = currentLine.empty() ? word : currentLine;
						for (std::size_t j = currentLine.empty() ? i + 1 : i; j < words.size(); ++j) {
							if (!remainder.empty()) {
								remainder.push_back(' ');
							}
							remainder += words[j];
						}
						layout.lines.push_back(TruncateTextToWidth(remainder, font, fontSize, maxWidth, "..."));
						reachedLimit = true;
						currentLine.clear();
						break;
					}

					if (!currentLine.empty()) {
						layout.lines.push_back(currentLine);
					}

					currentLine = MeasureTextWidth(font, fontSize, word) <= maxWidth ?
						word :
						TruncateTextToWidth(word, font, fontSize, maxWidth, "...");
				}

				if (!reachedLimit && !currentLine.empty()) {
					if (static_cast<int>(layout.lines.size()) >= maxLines) {
						reachedLimit = true;
					} else {
						layout.lines.push_back(currentLine);
					}
				}
			}

			if (!hasMoreParagraphs) {
				break;
			}

			if (static_cast<int>(layout.lines.size()) >= maxLines) {
				reachedLimit = true;
				break;
			}
			paragraphStart = paragraphEnd + 1;
		}

		if (reachedLimit && !layout.lines.empty()) {
			layout.lines.back() = TruncateTextToWidth(layout.lines.back(), font, layout.fontSize, maxWidth, "...");
		}

		UpdateWrappedLayoutMetrics(layout, font, lineSpacing);
		return layout;
	}

	WrappedTextLayout FitWrappedTextToBounds(const std::string& text,
		float baseFontSize,
		float minFontSize,
		float maxWidth,
		float maxHeight,
		float lineSpacing,
		int maxLines)
	{
		WrappedTextLayout best;
		best.fontSize = baseFontSize;

		ImFont* font = ImGui::GetFont();
		if (!font || text.empty()) {
			return best;
		}

		const int clampedMaxLines = (std::max)(1, maxLines);
		float fontSize = baseFontSize;
		while (true) {
			best = WrapTextToWidth(text, font, fontSize, maxWidth, clampedMaxLines, lineSpacing);
			if (maxHeight <= 0.0f || best.totalHeight <= maxHeight + 0.5f || fontSize <= minFontSize + 0.01f) {
				break;
			}

			const float nextFontSize = (std::max)(minFontSize, fontSize - 1.0f);
			if (nextFontSize >= fontSize) {
				break;
			}
			fontSize = nextFontSize;
		}

		if (maxHeight > 0.0f && best.totalHeight > maxHeight + 0.5f) {
			const float lineHeight = MeasureLineHeight(font, best.fontSize);
			const float lineAdvance = lineHeight + lineSpacing;
			if (lineAdvance > 0.0f) {
				const int heightLimitedLines = (std::max)(
					1,
					static_cast<int>(std::floor((maxHeight + lineSpacing) / lineAdvance)));
				if (heightLimitedLines < clampedMaxLines) {
					best = WrapTextToWidth(text, font, best.fontSize, maxWidth, heightLimitedLines, lineSpacing);
				}
			}
		}

		return best;
	}

	SingleLineTextLayout FitSingleLineText(const char* text, float baseFontSize, float minFontSize, float maxWidth)
	{
		SingleLineTextLayout layout;
		layout.fontSize = baseFontSize;
		layout.text = text ? text : "";

		ImFont* font = ImGui::GetFont();
		if (!font || layout.text.empty() || baseFontSize <= 0.0f || maxWidth <= 0.0f) {
			layout.width = MeasureTextWidth(font, layout.fontSize, layout.text);
			return layout;
		}

		layout.width = MeasureTextWidth(font, layout.fontSize, layout.text);
		while (layout.width > maxWidth && layout.fontSize > minFontSize + 0.01f) {
			layout.fontSize = (std::max)(minFontSize, layout.fontSize - 1.0f);
			layout.width = MeasureTextWidth(font, layout.fontSize, layout.text);
		}

		if (layout.width > maxWidth) {
			layout.text = TruncateTextToWidth(layout.text, font, layout.fontSize, maxWidth, "...");
			layout.width = MeasureTextWidth(font, layout.fontSize, layout.text);
		}

		return layout;
	}

	ImVec2 ComputeI4DrawSizePreserveAspect(const Texture::Image& sourceImage, const Texture::Image& fallbackImage, float scale)
	{
		const float fallbackW = fallbackImage.width > 0 ? fallbackImage.width * scale : 0.0f;
		const float fallbackH = fallbackImage.height > 0 ? fallbackImage.height * scale : 0.0f;
		if (fallbackW <= 0.0f || fallbackH <= 0.0f || sourceImage.width <= 0 || sourceImage.height <= 0) {
			return ImVec2(sourceImage.width * scale, sourceImage.height * scale);
		}

		const float srcW = static_cast<float>(sourceImage.width);
		const float srcH = static_cast<float>(sourceImage.height);
		const float fit = (std::min)(fallbackW / srcW, fallbackH / srcH);
		if (!std::isfinite(fit) || fit <= 0.0f) {
			return ImVec2(sourceImage.width * scale, sourceImage.height * scale);
		}

		return ImVec2(srcW * fit, srcH * fit);
	}

	bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needle)
	{
		if (needle.empty() || haystack.empty()) {
			return false;
		}
		auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
			[](char ch1, char ch2) {
				return std::tolower(static_cast<unsigned char>(ch1)) ==
				       std::tolower(static_cast<unsigned char>(ch2));
			});
		return it != haystack.end();
	}

	bool HasThrowableMarker(RE::TESForm* form)
	{
		if (!form) {
			return false;
		}
		
		// Patterns that indicate throwable items
		// - "Throwable": Generic throwable mods
		// - "Throwing": JZBai_ThrowingWpns keywords/model paths
		// - "Javelin": JZBai throwing javelins
		// - "JZBai_": JZBai mod prefix (all items are throwing weapons)
		static const char* throwablePatterns[] = {
			"Throwable",
			"Throwing",
			"Javelin"
		};
		static const char* editorIdPrefixes[] = {
			"JZBai_"  // JZBai mod prefix - all items are throwing weapons
		};
		
		// Check keywords
		if (auto keywordForm = form->As<RE::BGSKeywordForm>()) {
			for (std::uint32_t i = 0; i < keywordForm->numKeywords; ++i) {
				RE::BGSKeyword* kw = keywordForm->keywords[i];
				if (!kw) {
					continue;
				}
				const char* kwEditorId = kw->GetFormEditorID();
				if (kwEditorId) {
					for (const char* pattern : throwablePatterns) {
						if (ContainsCaseInsensitive(kwEditorId, pattern)) {
							return true;
						}
					}
				}
			}
		}
		
		const char* editorId = form->GetFormEditorID();
		const char* name = form->GetName();
		
		// Check editorID for patterns
		if (editorId) {
			for (const char* pattern : throwablePatterns) {
				if (ContainsCaseInsensitive(editorId, pattern)) {
					return true;
				}
			}
			for (const char* prefix : editorIdPrefixes) {
				if (ContainsCaseInsensitive(editorId, prefix)) {
					return true;
				}
			}
		}
		
		// Check display name (but not for mod prefixes like JZBai_)
		if (name) {
			for (const char* pattern : throwablePatterns) {
				if (ContainsCaseInsensitive(name, pattern)) {
					return true;
				}
			}
		}
		
		return false;
	}
}

MissingCategory MissingCategoryFromInt(int value)
{
	switch (value) {
	case 1:
		return MissingCategory::Consumable;
	case 2:
		return MissingCategory::Gear;
	case 3:
		return MissingCategory::ThrowableMod;
	default:
		return MissingCategory::Unknown;
	}
}

int MissingCategoryToInt(MissingCategory value)
{
	return static_cast<int>(value);
}

const char* MissingCategoryToString(MissingCategory category)
{
	switch (category) {
	case MissingCategory::Consumable:
		return "Consumable";
	case MissingCategory::Gear:
		return "Gear";
	case MissingCategory::ThrowableMod:
		return "ThrowableMod";
	default:
		return "Unknown";
	}
}

MissingCategory DetermineMissingCategory(RE::TESForm* form)
{
	if (!form) {
		return MissingCategory::Unknown;
	}
	if (HasThrowableMarker(form)) {
		return MissingCategory::ThrowableMod;
	}
	switch (form->GetFormType()) {
	case RE::FormType::AlchemyItem:
	case RE::FormType::Ingredient:
	case RE::FormType::Scroll:
	case RE::FormType::Book:
	case RE::FormType::Misc:
		return MissingCategory::Consumable;
	case RE::FormType::Weapon:
	case RE::FormType::Armor:
	case RE::FormType::Ammo:
	case RE::FormType::Light:
		return MissingCategory::Gear;
	default:
		return MissingCategory::Unknown;
	}
}

MissingCategory InferMissingCategoryFromItemType(std::string_view type)
{
	if (type == "WheelItemAlchemy" || type == "WheelItemIngredient" || type == "WheelItemScroll" || type == "WheelItemBook" || type == "WheelItemMisc") {
		return MissingCategory::Consumable;
	}
	if (type == "WheelItemWeapon" || type == "WheelItemArmor" || type == "WheelItemLight" || type == "WheelItemAmmo") {
		return MissingCategory::Gear;
	}
	return MissingCategory::Unknown;
}

bool IsKeepMissingCategoryEnabled(MissingCategory category)
{
	if (!Config::WheelBehavior::KeepMissing::Enabled) {
		return false;
	}
	switch (category) {
	case MissingCategory::Consumable:
		return Config::WheelBehavior::KeepMissing::KeepConsumables;
	case MissingCategory::Gear:
		return Config::WheelBehavior::KeepMissing::KeepGears;
	case MissingCategory::ThrowableMod:
		return Config::WheelBehavior::KeepMissing::KeepThrowableMods;
	default:
		return false;
	}
}
void WheelItem::DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	//Drawer::draw_element(_texture, ImVec2(0, 0), ImVec2(100, 100), 0);
}

void WheelItem::DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
}

float WheelItem::GetCooldownPercent() const
{
	if (!Config::Cooldowns::Enabled) {
		return 0.0f;
	}
	const float now = RE::Calendar::GetSingleton()->GetCurrentGameTime();
	// calendar time is in game days; convert cache window to days
	const float cacheWindowDays = Config::Cooldowns::CacheWindowSeconds / (24.0f * 60.0f * 60.0f);
	if (_cooldownCacheTime >= 0.0f && (now - _cooldownCacheTime) < cacheWindowDays) {
		return _cooldownCacheTotal > 0.0f ? std::clamp(_cooldownCacheRemaining / _cooldownCacheTotal, 0.0f, 1.0f) : 0.0f;
	}
	const float total = GetCooldownTotalSeconds();
	const float remaining = total > 0.0f ? GetCooldownRemainingSeconds() : 0.0f;
	_cooldownCacheTime = now;
	_cooldownCacheRemaining = remaining;
	_cooldownCacheTotal = total;
	if (total <= 0.0f) {
		return 0.0f;
	}
	return std::clamp(remaining / total, 0.0f, 1.0f);
}

void WheelItem::DrawCooldownOverlay(ImVec2 a_center, DrawArgs a_drawArgs)
{
	DrawCooldownOverlayInternal(a_center, a_drawArgs, false);
}

void WheelItem::DrawCooldownOverlayInternal(ImVec2 a_center, DrawArgs a_drawArgs, bool a_fillBottomToTop)
{
	if (!Config::Cooldowns::Enabled) {
		return;
	}
	const float remainingPercent = GetCooldownPercent();
	const float progress = std::clamp(1.0f - remainingPercent, 0.0f, 1.0f);
	if (progress <= 0.0f) {
		return;
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec4 col = ImGui::ColorConvertU32ToFloat4(Config::Cooldowns::OverlayColor);
	col.w *= a_drawArgs.alphaMult;
	const ImU32 overlayCol = ImGui::ColorConvertFloat4ToU32(col);

	// Cooldown progress indicator:
	// Draw only the moving progress element (no background tint pass) to avoid muddying reskins and tinting icons.
	// Progress semantics: 0 -> just used, 1 -> ready.
	Texture::Image slotBg = Texture::GetIconImage(Texture::icon_image_type::slot_background);
	const std::vector<ImVec2>* slotOutline = Texture::GetSlotBackgroundOutline();
	const float scale = Config::Styling::Item::Slot::BackgroundTexture::Scale;
	const ImVec2 size(slotBg.width * scale, slotBg.height * scale);

	const float thickness = 3.0f;
	const float baseRadius = (std::max)(size.x, size.y) * 0.5f;
	const float outwardPx = 6.0f;  // push outside slot to avoid tinting the icon
	const float outwardScale = baseRadius > 0.0f ? (1.0f + outwardPx / baseRadius) : 1.0f;

	auto draw_progress_on_closed_polyline = [&](std::vector<ImVec2>& pts, float prog, ImU32 col) {
		if (pts.size() < 3 || prog <= 0.0f) {
			return;
		}
		prog = std::clamp(prog, 0.0f, 1.0f);

		// Pick start point based on desired fill direction (used by shout overlay for legacy "bottom-up" behavior).
		std::size_t startIdx = 0;
		if (a_fillBottomToTop) {
			float bestY = pts[0].y;
			for (std::size_t i = 1; i < pts.size(); ++i) {
				if (pts[i].y > bestY) {
					bestY = pts[i].y;
					startIdx = i;
				}
			}
		} else {
			float bestY = pts[0].y;
			for (std::size_t i = 1; i < pts.size(); ++i) {
				if (pts[i].y < bestY) {
					bestY = pts[i].y;
					startIdx = i;
				}
			}
		}
		if (startIdx > 0 && startIdx < pts.size()) {
			std::rotate(pts.begin(), pts.begin() + static_cast<std::ptrdiff_t>(startIdx), pts.end());
		}

		float totalLen = 0.0f;
		for (std::size_t i = 0; i < pts.size(); ++i) {
			const ImVec2 a = pts[i];
			const ImVec2 b = pts[(i + 1) % pts.size()];
			const float dx = b.x - a.x;
			const float dy = b.y - a.y;
			totalLen += std::sqrt(dx * dx + dy * dy);
		}
		if (totalLen <= 0.0f) {
			return;
		}

		const float target = totalLen * prog;
		float acc = 0.0f;
		for (std::size_t i = 0; i < pts.size(); ++i) {
			const ImVec2 a = pts[i];
			const ImVec2 b = pts[(i + 1) % pts.size()];
			const float dx = b.x - a.x;
			const float dy = b.y - a.y;
			const float segLen = std::sqrt(dx * dx + dy * dy);
			if (segLen <= 0.0f) {
				continue;
			}
			if (acc + segLen <= target) {
				drawList->AddLine(a, b, col, thickness);
				acc += segLen;
				continue;
			}

			const float remain = target - acc;
			const float t = std::clamp(remain / segLen, 0.0f, 1.0f);
			const ImVec2 mid(a.x + dx * t, a.y + dy * t);
			drawList->AddLine(a, mid, col, thickness);
			break;
		}
	};

	if (slotOutline && slotBg.texture && slotOutline->size() >= 4 && size.x > 0.0f && size.y > 0.0f) {
		std::vector<ImVec2> pts;
		pts.reserve(slotOutline->size());

		const ImVec2 posMin(a_center.x - size.x * 0.5f, a_center.y - size.y * 0.5f);
		for (std::size_t i = 0; i + 1 < slotOutline->size(); ++i) {
			ImVec2 p = ImVec2(posMin.x + (*slotOutline)[i].x * size.x, posMin.y + (*slotOutline)[i].y * size.y);
			p = a_center + (p - a_center) * outwardScale;
			pts.push_back(p);
		}
		draw_progress_on_closed_polyline(pts, progress, overlayCol);
	} else {
		const float r = (std::max)(0.0f, baseRadius * outwardScale);
		const float rMin = (std::max)(0.0f, r - thickness * 0.5f);
		const float rMax = r + thickness * 0.5f;
		const float startAng = a_fillBottomToTop ? (IM_PI / 2.0f) : (-IM_PI / 2.0f);
		Drawer::draw_arc(a_center, rMin, rMax, startAng, startAng + 2.0f * IM_PI * progress, startAng, startAng + 2.0f * IM_PI * progress, overlayCol, 64, a_drawArgs);
	}
}

bool WheelItem::IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	return false;
}

bool WheelItem::IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	return false;
}

void WheelItem::ActivateItemSecondary()
{
}

void WheelItem::ActivateItemPrimary()
{
}

void WheelItem::ActivateItemSpecial()
{
}

WheelItemActivationResult WheelItem::ActivateItemWithResult(WheelItemActivationKind a_kind)
{
	switch (a_kind) {
	case WheelItemActivationKind::Primary:
		ActivateItemPrimary();
		break;
	case WheelItemActivationKind::Secondary:
		ActivateItemSecondary();
		break;
	case WheelItemActivationKind::Special:
		ActivateItemSpecial();
		break;
	}
	return WheelItemActivationResult::Succeeded;
}

const char* WheelItemActivationResultName(WheelItemActivationResult a_result) noexcept
{
	switch (a_result) {
	case WheelItemActivationResult::Succeeded:
		return "succeeded";
	case WheelItemActivationResult::AlreadyPoisoned:
		return "already_poisoned";
	case WheelItemActivationResult::UnsafeResolution:
		return "unsafe_resolution";
	case WheelItemActivationResult::InvalidTarget:
		return "invalid_target";
	case WheelItemActivationResult::Rejected:
		return "rejected";
	}
	return "unknown";
}

void WheelItem::SerializeIntoJsonObj(nlohmann::json& a_json)
{
	a_json["type"] = ITEM_TYPE_STR;
	a_json["WARNING"] = "This is a placeholder item. It should not be used.";
}

void WheelItem::drawItemHighlightStatIconAndValue(ImVec2 a_center, Texture::Image& a_stat_icon, float a_stat_value, DrawArgs a_drawArgs)
{
	Drawer::draw_text(
		a_center.x + Config::Styling::Item::Highlight::StatText::OffsetX,
		a_center.y + Config::Styling::Item::Highlight::StatText::OffsetY,
		fmt::format(": {}", int(std::ceil(a_stat_value))).data(),
		Config::Styling::Wheel::TextColor,
		Config::Styling::Item::Highlight::StatText::Size,
		a_drawArgs,
		false);
	
	Drawer::draw_texture(
		a_stat_icon.texture,
		a_center,
		Config::Styling::Item::Highlight::StatIcon::OffsetX,
		Config::Styling::Item::Highlight::StatIcon::OffsetY,
		ImVec2(a_stat_icon.width * Config::Styling::Item::Highlight::StatIcon::Scale, a_stat_icon.height * Config::Styling::Item::Highlight::StatIcon::Scale),
		C_SKYRIMWHITE,
		a_drawArgs);
}

float WheelItem::calculateHighlightTextShiftY(const char* a_description) const
{
	using namespace Config::Styling::Item::Highlight;

	if (!Desc::AutoShiftUp || !a_description || a_description[0] == '\0') {
		return 0.0f;
	}

	ImFont* font = ImGui::GetFont();
	if (!font) {
		return 0.0f;
	}

	const WrappedTextLayout previewLayout = WrapTextToWidth(
		a_description,
		font,
		Desc::Size,
		Desc::LineLength,
		static_cast<int>(Desc::MaxLines),
		Desc::LineSpacing);

	if (previewLayout.totalHeight <= Desc::ShiftUpThreshold) {
		return 0.0f;
	}

	const float overflow = previewLayout.totalHeight - Desc::ShiftUpThreshold;
	return (std::min)(Desc::MaxShiftUp, overflow * 0.5f);
}

void WheelItem::drawHighlightDescription(ImVec2 a_center, const char* a_text, DrawArgs a_drawArgs, float a_shiftY)
{
	std::string buf = a_text ? a_text : "";
	using namespace Config::Styling::Item::Highlight;

	if (buf.empty()) {
		return;
	}

	const float drawX = a_center.x + Desc::OffsetX;
	const float drawY = a_center.y + Desc::OffsetY - a_shiftY;

	if (!Desc::AutoFit) {
		Drawer::draw_text_block(drawX, drawY, buf, Config::Styling::Wheel::TextColor, Desc::Size, Desc::LineSpacing, Desc::LineLength, a_drawArgs);
		return;
	}

	float maxHeight = Desc::MaxHeight;
	const ImVec2 viewport = ImGui::GetIO().DisplaySize;
	if (viewport.y > 0.0f && Desc::BottomSafeMargin > 0.0f) {
		const float viewportBoundHeight = viewport.y - Desc::BottomSafeMargin - drawY;
		if (viewportBoundHeight > 0.0f) {
			maxHeight = maxHeight > 0.0f ? (std::min)(maxHeight, viewportBoundHeight) : viewportBoundHeight;
		}
	}

	WrappedTextLayout layout = FitWrappedTextToBounds(
		buf,
		Desc::Size,
		Desc::MinSize,
		Desc::LineLength,
		maxHeight,
		Desc::LineSpacing,
		static_cast<int>(Desc::MaxLines));

	const float lineHeight = MeasureLineHeight(ImGui::GetFont(), layout.fontSize);
	float lineY = drawY;
	for (const auto& line : layout.lines) {
		if (!line.empty()) {
			Drawer::draw_text(drawX, lineY, line.c_str(), Config::Styling::Wheel::TextColor, layout.fontSize, a_drawArgs);
		}
		lineY += lineHeight + Desc::LineSpacing;
	}
}

void WheelItem::drawHighlightTexture(ImVec2 a_center, DrawArgs a_drawArgs)
{
	const float scale = Config::Styling::Item::Highlight::Texture::Scale;
	const Texture::Image* iconImage = &_texture;
	ImU32 tint = C_SKYRIMWHITE;
	bool usingI4Icon = false;
	const bool tryI4 = IsInventoryBacked() || Config::I4::UseAlternativePath;
	if (tryI4) {
		float requestedBase = (std::max)(_texture.width * scale, _texture.height * scale);
		if (requestedBase < 1.0f) {
			requestedBase = static_cast<float>((std::clamp)(Config::I4::FixedRenderSize, 16u, 1024u));
		}
		const auto requestedPx = static_cast<std::uint32_t>((std::max)(1, static_cast<int>(std::lround(requestedBase))));
		const auto iconResult = I4Integration::I4IconProvider::GetSingleton().GetBestIconForItem(
			_texture,
			GetI4Form(),
			GetI4Signature(),
			requestedPx);
		if (iconResult.image.texture) {
			iconImage = &iconResult.image;
		}
		if (iconResult.usingI4) {
			tint = iconResult.tint;
			usingI4Icon = iconResult.image.texture != nullptr;
		}
	}
	Texture::Image defaultIcon{};
	if (!iconImage->texture) {
		defaultIcon = Texture::GetIconImage(Texture::icon_image_type::icon_default);
		if (defaultIcon.texture) {
			iconImage = &defaultIcon;
		}
	}
	ImVec2 drawSize(iconImage->width * scale, iconImage->height * scale);
	if (usingI4Icon && _texture.width > 0 && _texture.height > 0) {
		drawSize = ComputeI4DrawSizePreserveAspect(*iconImage, _texture, scale);
	}
	Drawer::draw_texture(iconImage->texture,
		ImVec2(a_center.x, a_center.y),
		Config::Styling::Item::Highlight::Texture::OffsetX,
		Config::Styling::Item::Highlight::Texture::OffsetY,
		drawSize,
		tint, a_drawArgs);
}

void WheelItem::drawHighlightText(ImVec2 a_center, const char* a_text, DrawArgs a_drawArgs, float a_shiftY)
{
	using namespace Config::Styling::Item::Highlight;
	const float drawX = a_center.x + Text::OffsetX;
	const float drawY = a_center.y + Text::OffsetY - a_shiftY;

	if (!Text::AutoFit) {
		Drawer::draw_text(drawX, drawY, a_text, Config::Styling::Wheel::TextColor, Text::Size, a_drawArgs);
		return;
	}

	float maxWidth = Text::MaxWidth > 0.0f ? Text::MaxWidth : Config::Styling::Item::Highlight::Desc::LineLength;
	const ImVec2 viewport = ImGui::GetIO().DisplaySize;
	if (viewport.x > 0.0f) {
		const float viewportLimit = (std::max)(0.0f, viewport.x - 80.0f);
		maxWidth = maxWidth > 0.0f ? (std::min)(maxWidth, viewportLimit) : viewportLimit;
	}

	const SingleLineTextLayout layout = FitSingleLineText(a_text, Text::Size, Text::MinSize, maxWidth);
	Drawer::draw_text(drawX, drawY, layout.text.c_str(), Config::Styling::Wheel::TextColor, layout.fontSize, a_drawArgs);
}

void WheelItem::drawSlotTexture(ImVec2 a_center, DrawArgs a_drawArgs)
{
	const float scale = Config::Styling::Item::Slot::Texture::Scale;
	const Texture::Image* iconImage = &_texture;
	ImU32 tint = C_SKYRIMWHITE;
	bool usingI4Icon = false;
	const bool tryI4 = IsInventoryBacked() || Config::I4::UseAlternativePath;
	if (tryI4) {
		float requestedBase = (std::max)(_texture.width * scale, _texture.height * scale);
		if (requestedBase < 1.0f) {
			requestedBase = static_cast<float>((std::clamp)(Config::I4::FixedRenderSize, 16u, 1024u));
		}
		const auto requestedPx = static_cast<std::uint32_t>((std::max)(1, static_cast<int>(std::lround(requestedBase))));
		const auto iconResult = I4Integration::I4IconProvider::GetSingleton().GetBestIconForItem(
			_texture,
			GetI4Form(),
			GetI4Signature(),
			requestedPx);
		if (iconResult.image.texture) {
			iconImage = &iconResult.image;
		}
		if (iconResult.usingI4) {
			tint = iconResult.tint;
			usingI4Icon = iconResult.image.texture != nullptr;
		}
	}
	Texture::Image defaultIcon{};
	if (!iconImage->texture) {
		defaultIcon = Texture::GetIconImage(Texture::icon_image_type::icon_default);
		if (defaultIcon.texture) {
			iconImage = &defaultIcon;
		}
	}
	ImVec2 drawSize(iconImage->width * scale, iconImage->height * scale);
	if (usingI4Icon && _texture.width > 0 && _texture.height > 0) {
		drawSize = ComputeI4DrawSizePreserveAspect(*iconImage, _texture, scale);
	}
	Drawer::draw_texture(iconImage->texture,
		ImVec2(a_center.x, a_center.y),
		Config::Styling::Item::Slot::Texture::OffsetX,
		Config::Styling::Item::Slot::Texture::OffsetY,
		drawSize,
		tint, a_drawArgs);
}

void WheelItem::drawSlotText(ImVec2 a_center, const char* a_text, DrawArgs a_drawArgs)
{
	using namespace Config::Styling::Item::Slot;
	const float drawX = a_center.x + Text::OffsetX;
	const float drawY = a_center.y + Text::OffsetY;

	if (!Text::AutoFit) {
		Drawer::draw_text(drawX, drawY, a_text, Config::Styling::Wheel::TextColor, Text::Size, a_drawArgs);
		return;
	}

	float maxWidth = Text::MaxWidth;
	if (maxWidth <= 0.0f) {
		const ::Texture::Image slotBackground = ::Texture::GetIconImage(::Texture::icon_image_type::slot_background);
		if (slotBackground.width > 0) {
			maxWidth = slotBackground.width * BackgroundTexture::Scale * 1.15f;
		}

		ImFont* font = ImGui::GetFont();
		if (font && Text::AutoWidthMinChars > 0) {
			const std::string autoWidthSample = Utf8Prefix(a_text ? a_text : "", static_cast<std::size_t>(Text::AutoWidthMinChars));
			if (!autoWidthSample.empty()) {
				maxWidth = (std::max)(maxWidth, MeasureTextWidth(font, Text::Size, autoWidthSample));
			}
		}
	}
	if (maxWidth <= 0.0f) {
		maxWidth = 180.0f;
	}

	const ImVec2 viewport = ImGui::GetIO().DisplaySize;
	if (viewport.x > 0.0f) {
		const float viewportLimit = (std::max)(0.0f, viewport.x - 40.0f);
		maxWidth = maxWidth > 0.0f ? (std::min)(maxWidth, viewportLimit) : viewportLimit;
	}

	float maxHeight = Text::MaxHeight;
	if (maxHeight <= 0.0f) {
		const float baseLineHeight = MeasureLineHeight(ImGui::GetFont(), Text::Size);
		if (baseLineHeight > 0.0f) {
			const int maxLines = (std::max)(1, static_cast<int>(Text::MaxLines));
			maxHeight = baseLineHeight;
			if (maxLines > 1) {
				maxHeight = baseLineHeight * (static_cast<float>(maxLines) - 0.15f);
				maxHeight += Text::LineSpacing * static_cast<float>(maxLines - 1);
			}
		}
	}

	WrappedTextLayout layout = FitWrappedTextToBounds(
		a_text ? a_text : "",
		Text::Size,
		Text::MinSize,
		maxWidth,
		maxHeight,
		Text::LineSpacing,
		static_cast<int>(Text::MaxLines));

	if (layout.lines.empty()) {
		return;
	}

	const float lineHeight = MeasureLineHeight(ImGui::GetFont(), layout.fontSize);
	float lineY = drawY - layout.totalHeight * 0.5f + lineHeight * 0.5f;
	for (const auto& line : layout.lines) {
		if (!line.empty()) {
			Drawer::draw_text(drawX, lineY, line.c_str(), Config::Styling::Wheel::TextColor, layout.fontSize, a_drawArgs);
		}
		lineY += lineHeight + Text::LineSpacing;
	}
}
