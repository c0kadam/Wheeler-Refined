#include "WheelItemOStimAction.h"

#include "bin/Integrations/OStimIntegration.h"
#include "bin/Rendering/Drawer.h"
#include "bin/Rendering/TextureManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	constexpr std::string_view kActionIconRoot = "Data\\SKSE\\Plugins\\wheeler\\resources\\ostim\\icons\\";

	std::string NormalizeLabel(std::string_view a_value)
	{
		std::string out;
		out.reserve(a_value.size());
		for (char c : a_value) {
			out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		return out;
	}

	ImVec2 ComputeFittedDrawSize(const Texture::Image& sourceImage, const Texture::Image& fallbackImage, float scale)
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

	ImVec2 ScaleSize(ImVec2 a_size, float a_scale)
	{
		return ImVec2(a_size.x * a_scale, a_size.y * a_scale);
	}

	ImVec2 ClampSizeToFitBox(ImVec2 a_size, ImVec2 a_box)
	{
		if (a_size.x <= 0.0f || a_size.y <= 0.0f ||
		    a_box.x <= 0.0f || a_box.y <= 0.0f) {
			return a_size;
		}

		if (a_size.x <= a_box.x && a_size.y <= a_box.y) {
			return a_size;
		}

		const float fitScale = (std::min)(a_box.x / a_size.x, a_box.y / a_size.y);
		if (!std::isfinite(fitScale) || fitScale <= 0.0f) {
			return a_size;
		}

		return ScaleSize(a_size, fitScale);
	}

	ImVec2 ComputeFittedSizeToBox(const Texture::Image& a_texture, ImVec2 a_fitBox)
	{
		if (a_texture.width <= 0 || a_texture.height <= 0 ||
		    a_fitBox.x <= 0.0f || a_fitBox.y <= 0.0f) {
			return ImVec2(
				static_cast<float>(a_texture.width),
				static_cast<float>(a_texture.height));
		}

		const float fitScale = (std::min)(
			a_fitBox.x / static_cast<float>(a_texture.width),
			a_fitBox.y / static_cast<float>(a_texture.height));
		if (!std::isfinite(fitScale) || fitScale <= 0.0f) {
			return ImVec2(
				static_cast<float>(a_texture.width),
				static_cast<float>(a_texture.height));
		}

		return ImVec2(
			static_cast<float>(a_texture.width) * fitScale,
			static_cast<float>(a_texture.height) * fitScale);
	}

	struct IconLayoutTuning
	{
		float scale{ 1.0f };
		float offsetX{ 0.0f };
		float offsetY{ 0.0f };
	};

	IconLayoutTuning GetManagedIconLayoutTuning(bool a_highlight)
	{
		if (a_highlight) {
			return {
				Config::OStimIntegration::SVGCenterScale,
				Config::OStimIntegration::SVGCenterOffsetX,
				Config::OStimIntegration::SVGCenterOffsetY
			};
		}

		return {
			Config::OStimIntegration::SVGSlotScale,
			Config::OStimIntegration::SVGSlotOffsetX,
			Config::OStimIntegration::SVGSlotOffsetY
		};
	}

	IconLayoutTuning GetRasterIconLayoutTuning(bool a_highlight)
	{
		if (a_highlight) {
			return {
				Config::OStimIntegration::DDSCenterScale,
				Config::OStimIntegration::DDSCenterOffsetX,
				Config::OStimIntegration::DDSCenterOffsetY
			};
		}

		return {
			Config::OStimIntegration::DDSSlotScale,
			Config::OStimIntegration::DDSSlotOffsetX,
			Config::OStimIntegration::DDSSlotOffsetY
		};
	}

	struct TexturedVertex
	{
		ImVec2 pos;
		ImVec2 uv;
	};

	float Cross2D(const ImVec2& a_lhs, const ImVec2& a_rhs)
	{
		return a_lhs.x * a_rhs.y - a_lhs.y * a_rhs.x;
	}

	float Cross2D(const ImVec2& a_origin, const ImVec2& a_a, const ImVec2& a_b)
	{
		return Cross2D(
			ImVec2(a_a.x - a_origin.x, a_a.y - a_origin.y),
			ImVec2(a_b.x - a_origin.x, a_b.y - a_origin.y));
	}

	bool IsInsideClipEdge(const ImVec2& a_edgeStart, const ImVec2& a_edgeEnd, const ImVec2& a_point)
	{
		// Slot outlines are normalized clockwise in screen space (y-down),
		// so the interior lies on the non-negative side of each directed edge.
		return Cross2D(a_edgeStart, a_edgeEnd, a_point) >= -1.0e-4f;
	}

	TexturedVertex IntersectSegmentWithClipEdge(
		const TexturedVertex& a_start,
		const TexturedVertex& a_end,
		const ImVec2& a_edgeStart,
		const ImVec2& a_edgeEnd)
	{
		const ImVec2 segDir(a_end.pos.x - a_start.pos.x, a_end.pos.y - a_start.pos.y);
		const ImVec2 edgeDir(a_edgeEnd.x - a_edgeStart.x, a_edgeEnd.y - a_edgeStart.y);
		const float denom = Cross2D(edgeDir, segDir);
		float t = 0.0f;
		if (std::fabs(denom) > 1.0e-6f) {
			const ImVec2 startToEdge(a_edgeStart.x - a_start.pos.x, a_edgeStart.y - a_start.pos.y);
			t = Cross2D(edgeDir, startToEdge) / denom;
			t = (std::clamp)(t, 0.0f, 1.0f);
		}

		return {
			ImVec2(
				a_start.pos.x + segDir.x * t,
				a_start.pos.y + segDir.y * t),
			ImVec2(
				a_start.uv.x + (a_end.uv.x - a_start.uv.x) * t,
				a_start.uv.y + (a_end.uv.y - a_start.uv.y) * t)
		};
	}

	std::vector<TexturedVertex> ClipTexturedQuadToPolygon(
		const std::vector<TexturedVertex>& a_subject,
		const std::vector<ImVec2>& a_clipPolygon)
	{
		std::vector<TexturedVertex> output = a_subject;
		if (output.empty() || a_clipPolygon.size() < 3) {
			return {};
		}

		for (std::size_t i = 0; i < a_clipPolygon.size(); ++i) {
			const ImVec2 edgeStart = a_clipPolygon[i];
			const ImVec2 edgeEnd = a_clipPolygon[(i + 1) % a_clipPolygon.size()];
			if (output.empty()) {
				break;
			}

			std::vector<TexturedVertex> input = output;
			output.clear();

			TexturedVertex previous = input.back();
			bool previousInside = IsInsideClipEdge(edgeStart, edgeEnd, previous.pos);

			for (const auto& current : input) {
				const bool currentInside = IsInsideClipEdge(edgeStart, edgeEnd, current.pos);
				if (currentInside) {
					if (!previousInside) {
						output.push_back(IntersectSegmentWithClipEdge(previous, current, edgeStart, edgeEnd));
					}
					output.push_back(current);
				} else if (previousInside) {
					output.push_back(IntersectSegmentWithClipEdge(previous, current, edgeStart, edgeEnd));
				}

				previous = current;
				previousInside = currentInside;
			}
		}

		return output;
	}

	void DrawTexturedPolygon(
		ID3D11ShaderResourceView* a_texture,
		const std::vector<TexturedVertex>& a_vertices,
		float a_alphaMult)
	{
		if (!a_texture || a_vertices.size() < 3) {
			return;
		}

		ImVec4 color = ImGui::ColorConvertU32ToFloat4(C_SKYRIMWHITE);
		color.w *= a_alphaMult;
		const ImU32 finalColor = ImGui::ColorConvertFloat4ToU32(color);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImTextureID textureID = (ImTextureID)a_texture;
		drawList->PushTextureID(textureID);
		drawList->PrimReserve(static_cast<int>((a_vertices.size() - 2) * 3), static_cast<int>(a_vertices.size()));

		for (std::size_t i = 1; i + 1 < a_vertices.size(); ++i) {
			drawList->PrimWriteIdx(drawList->_VtxCurrentIdx + 0);
			drawList->PrimWriteIdx(drawList->_VtxCurrentIdx + static_cast<ImDrawIdx>(i));
			drawList->PrimWriteIdx(drawList->_VtxCurrentIdx + static_cast<ImDrawIdx>(i + 1));
		}

		for (const auto& vertex : a_vertices) {
			drawList->PrimWriteVtx(vertex.pos, vertex.uv, finalColor);
		}
		drawList->PopTextureID();
	}

	ImVec2 ComputeManagedIconDrawSize(const Texture::Image& a_texture, bool a_highlight)
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
			fitBox.x *= 1.18f;
			fitBox.y *= 1.18f;
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

		const float fillRatio = a_highlight ? 0.78f : 0.66f;
		const float finalScale = (std::max)(fallbackScale, fitScale * fillRatio);
		return ImVec2(
			static_cast<float>(a_texture.width) * finalScale,
			static_cast<float>(a_texture.height) * finalScale);
	}

	bool PointInPolygon(const std::vector<ImVec2>& a_polygon, ImVec2 a_point)
	{
		if (a_polygon.size() < 3) {
			return false;
		}

		bool inside = false;
		std::size_t last = a_polygon.size() - 1;
		for (std::size_t i = 0; i < a_polygon.size(); last = i++) {
			const ImVec2& lhs = a_polygon[i];
			const ImVec2& rhs = a_polygon[last];
			const bool intersects = ((lhs.y > a_point.y) != (rhs.y > a_point.y)) &&
				(a_point.x < (rhs.x - lhs.x) * (a_point.y - lhs.y) / ((rhs.y - lhs.y) + 1.0e-6f) + lhs.x);
			if (intersects) {
				inside = !inside;
			}
		}

		return inside;
	}

	ImVec2 ClampSizeToSlotOutline(
		ImVec2 a_drawSize,
		ImVec2 a_containerSize,
		const std::vector<ImVec2>* a_outline)
	{
		if (!a_outline || a_outline->size() < 3 ||
		    a_containerSize.x <= 0.0f || a_containerSize.y <= 0.0f ||
		    a_drawSize.x <= 0.0f || a_drawSize.y <= 0.0f) {
			return a_drawSize;
		}

		float minX = a_outline->front().x;
		float minY = a_outline->front().y;
		float maxX = minX;
		float maxY = minY;
		for (const auto& point : *a_outline) {
			minX = (std::min)(minX, point.x);
			minY = (std::min)(minY, point.y);
			maxX = (std::max)(maxX, point.x);
			maxY = (std::max)(maxY, point.y);
		}

		const ImVec2 center((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
		const ImVec2 normalizedHalfSize(
			(a_drawSize.x / a_containerSize.x) * 0.5f,
			(a_drawSize.y / a_containerSize.y) * 0.5f);

		float shrink = 1.0f;
		for (int iteration = 0; iteration < 28; ++iteration) {
			const ImVec2 halfSize(normalizedHalfSize.x * shrink, normalizedHalfSize.y * shrink);
			const std::array<ImVec2, 4> corners{
				ImVec2(center.x - halfSize.x, center.y - halfSize.y),
				ImVec2(center.x + halfSize.x, center.y - halfSize.y),
				ImVec2(center.x + halfSize.x, center.y + halfSize.y),
				ImVec2(center.x - halfSize.x, center.y + halfSize.y)
			};

			const bool fits = std::all_of(corners.begin(), corners.end(), [&](const ImVec2& corner) {
				return PointInPolygon(*a_outline, corner);
			});
			if (fits) {
				return ScaleSize(a_drawSize, shrink * 0.98f);
			}

			shrink *= 0.92f;
		}

		return ScaleSize(a_drawSize, 0.70f);
	}

	std::optional<std::string> ResolveCustomActionIconPath(const OStimActionPayload& a_payload)
	{
		const std::string label = NormalizeLabel(a_payload.displayName);
		const auto makePath = [](std::string_view a_name) {
			return std::string(kActionIconRoot) + std::string(a_name);
		};

		switch (a_payload.kind) {
		case OStimActionKind::StopScene:
			return makePath("end_scene.svg");
		case OStimActionKind::IncreaseSpeed:
			return makePath("speed_up.svg");
		case OStimActionKind::DecreaseSpeed:
			return makePath("speed_down.svg");
		case OStimActionKind::NextPosition:
			return makePath("next_position.svg");
		case OStimActionKind::PreviousPosition:
			return makePath("previous_position.svg");
		case OStimActionKind::OpenPositionBrowser:
			if (label == "next page") {
				return makePath("next_page.svg");
			}
			if (label == "previous page") {
				return makePath("previous_page.svg");
			}
			return makePath("browse_positions.svg");
		case OStimActionKind::OpenPositionSubmenu:
			return makePath("browse_positions.svg");
		case OStimActionKind::ReturnToPositionBrowserParent:
			return makePath("back_to_positions.svg");
		case OStimActionKind::ReturnToControlWheel:
			return makePath("back_to_controls.svg");
		default:
			return std::nullopt;
		}
	}

	Texture::Image ResolveBuiltinIcon(OStimActionKind a_kind)
	{
		switch (a_kind) {
		case OStimActionKind::StopScene:
			return Texture::GetIconImage(Texture::icon_image_type::power);
		case OStimActionKind::IncreaseSpeed:
		case OStimActionKind::DecreaseSpeed:
			return Texture::GetIconImage(Texture::icon_image_type::shout);
		case OStimActionKind::OpenPositionBrowser:
		case OStimActionKind::OpenPositionSubmenu:
		case OStimActionKind::SelectSpecificPosition:
			return Texture::GetIconImage(Texture::icon_image_type::scroll);
		case OStimActionKind::ReturnToPositionBrowserParent:
		case OStimActionKind::ReturnToControlWheel:
			return Texture::GetIconImage(Texture::icon_image_type::arrow);
		default:
			return Texture::GetIconImage(Texture::icon_image_type::icon_default);
		}
	}

	struct ResolvedActionIcon
	{
		Texture::Image image;
		bool usedCustomManagedIcon{ false };
	};

	ResolvedActionIcon ResolveActionIcon(const OStimActionPayload& a_payload)
	{
		if (auto path = ResolveCustomActionIconPath(a_payload); path.has_value()) {
			if (auto image = Texture::GetImageByPath(*path); image.texture) {
				return { image, true };
			}
		}

		return { ResolveBuiltinIcon(a_payload.kind), false };
	}

	struct ResolvedRasterOverride
	{
		Texture::Image image;
		std::string sourcePath;
	};

	ResolvedRasterOverride ResolveRasterOverride(const OStimActionPayload& a_payload)
	{
		if (!a_payload.previewPath.empty()) {
			if (auto preview = Texture::GetExternalRasterImage(a_payload.previewPath); preview.texture) {
				return { preview, a_payload.previewPath };
			}
		}
		if (!a_payload.iconPath.empty()) {
			if (auto icon = Texture::GetExternalRasterImage(a_payload.iconPath); icon.texture) {
				return { icon, a_payload.iconPath };
			}
		}
		return {};
	}

	void DrawRasterOverride(
		const ResolvedRasterOverride& a_override,
		const Texture::Image& a_builtinTexture,
		ImVec2 a_center,
		DrawArgs a_drawArgs,
		bool a_highlight)
	{
		const Texture::Image& a_overrideTexture = a_override.image;
		const auto layout = GetRasterIconLayoutTuning(a_highlight);
		const float scale = a_highlight ?
			Config::Styling::Item::Highlight::Texture::Scale :
			Config::Styling::Item::Slot::Texture::Scale;
		const ImVec2 imageCenter(
			a_center.x + layout.offsetX,
			a_center.y + layout.offsetY);

		const ImVec2 baseSize = ComputeFittedDrawSize(a_overrideTexture, a_builtinTexture, scale);
		ImVec2 drawSize = ScaleSize(baseSize, a_highlight ? 1.12f : 1.04f);
		Texture::Image slotMaskedTexture{};
		const Texture::Image* drawTexture = &a_overrideTexture;
		if (!a_highlight && !a_override.sourcePath.empty()) {
			slotMaskedTexture = Texture::GetSlotMaskedExternalRasterImage(a_override.sourcePath);
			if (slotMaskedTexture.texture) {
				drawTexture = &slotMaskedTexture;
			}
		}
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		bool pushedClipRect = false;
		if (!a_highlight) {
			const Texture::Image slotBackground = Texture::GetSlotBackgroundMaskImage();
			if (slotBackground.texture && slotBackground.width > 0 && slotBackground.height > 0) {
				const float backgroundScale = Config::Styling::Item::Slot::BackgroundTexture::Scale;
				const ImVec2 containerSize(
					slotBackground.width * backgroundScale * 0.86f,
					slotBackground.height * backgroundScale * 0.86f);
				const float canonicalSlotIconSize = 512.0f * Config::Styling::Item::Slot::Texture::Scale;
				const ImVec2 canonicalFit = ComputeFittedSizeToBox(
					a_overrideTexture,
					ImVec2(canonicalSlotIconSize, canonicalSlotIconSize));
				if (canonicalFit.x > 0.0f && canonicalFit.y > 0.0f) {
					drawSize = canonicalFit;
				}

				const std::vector<ImVec2>* slotOutline = Texture::GetSlotBackgroundOutline();
				ImVec2 outlineMin(0.0f, 0.0f);
				ImVec2 outlineMax(1.0f, 1.0f);
				std::vector<ImVec2> clipPolygon;
				if (slotOutline && slotOutline->size() >= 3) {
					outlineMin = slotOutline->front();
					outlineMax = slotOutline->front();
					for (const auto& point : *slotOutline) {
						outlineMin.x = (std::min)(outlineMin.x, point.x);
						outlineMin.y = (std::min)(outlineMin.y, point.y);
						outlineMax.x = (std::max)(outlineMax.x, point.x);
						outlineMax.y = (std::max)(outlineMax.y, point.y);
						clipPolygon.push_back(ImVec2(
							a_center.x + (point.x - 0.5f) * containerSize.x,
							a_center.y + (point.y - 0.5f) * containerSize.y));
					}
					if (clipPolygon.size() > 1) {
						const ImVec2& first = clipPolygon.front();
						const ImVec2& last = clipPolygon.back();
						if (std::fabs(first.x - last.x) < 1.0e-4f && std::fabs(first.y - last.y) < 1.0e-4f) {
							clipPolygon.pop_back();
						}
					}
				}

				const ImVec2 outlineBox(
					(outlineMax.x - outlineMin.x) * containerSize.x,
					(outlineMax.y - outlineMin.y) * containerSize.y);
				if (outlineBox.x > 0.0f && outlineBox.y > 0.0f) {
					drawSize = ClampSizeToFitBox(drawSize, ScaleSize(outlineBox, 0.78f));
				} else {
					drawSize = ClampSizeToFitBox(drawSize, ScaleSize(containerSize, 0.60f));
				}
				drawSize = ClampSizeToSlotOutline(drawSize, containerSize, slotOutline);

				const ImVec2 clipMin(
					a_center.x + (outlineMin.x - 0.5f) * containerSize.x,
					a_center.y + (outlineMin.y - 0.5f) * containerSize.y);
				const ImVec2 clipMax(
					a_center.x + (outlineMax.x - 0.5f) * containerSize.x,
					a_center.y + (outlineMax.y - 0.5f) * containerSize.y);
				drawList->PushClipRect(clipMin, clipMax, true);
				pushedClipRect = true;

				if (drawTexture == &a_overrideTexture && clipPolygon.size() >= 3) {
					const std::vector<TexturedVertex> subject{
						{ ImVec2(imageCenter.x - drawSize.x * 0.5f, imageCenter.y - drawSize.y * 0.5f), ImVec2(0.0f, 0.0f) },
						{ ImVec2(imageCenter.x + drawSize.x * 0.5f, imageCenter.y - drawSize.y * 0.5f), ImVec2(1.0f, 0.0f) },
						{ ImVec2(imageCenter.x + drawSize.x * 0.5f, imageCenter.y + drawSize.y * 0.5f), ImVec2(1.0f, 1.0f) },
						{ ImVec2(imageCenter.x - drawSize.x * 0.5f, imageCenter.y + drawSize.y * 0.5f), ImVec2(0.0f, 1.0f) }
					};
					const auto clipped = ClipTexturedQuadToPolygon(subject, clipPolygon);
					if (clipped.size() >= 3) {
						DrawTexturedPolygon(drawTexture->texture, clipped, a_drawArgs.alphaMult);
						drawList->PopClipRect();
						return;
					}
				}
			}
		} else {
			// Highlight previews live in the wheel center, not inside a slot.
			// Keep them centered and size them against the wheel's inner-circle
			// space instead of the slot-sized minimum that was only intended to
			// keep tiny assets visible.
			const float minHighlightSize = (std::max)(
				180.0f,
				Config::Styling::Wheel::InnerCircleRadius * 0.82f);
			const ImVec2 highlightFitBox(
				Config::Styling::Wheel::InnerCircleRadius * 1.55f,
				Config::Styling::Wheel::InnerCircleRadius * 1.55f);
			const ImVec2 fittedToHighlight = ComputeFittedSizeToBox(a_overrideTexture, highlightFitBox);
			if (fittedToHighlight.x > 0.0f && fittedToHighlight.y > 0.0f) {
				drawSize = fittedToHighlight;
			}
			if (drawSize.x > 0.0f && drawSize.y > 0.0f) {
				const float dominant = (std::max)(drawSize.x, drawSize.y);
				if (dominant < minHighlightSize) {
					const float upscale = minHighlightSize / dominant;
					drawSize = ScaleSize(drawSize, upscale);
				}
			}
		}
		drawSize = ScaleSize(drawSize, layout.scale);
		DrawArgs centeredArgs = a_drawArgs;
		centeredArgs.rotationOffset = 0.0f;
		Drawer::draw_texture(
			drawTexture->texture,
			imageCenter,
			0.0f,
			0.0f,
			drawSize,
			C_SKYRIMWHITE,
			centeredArgs);
		if (pushedClipRect) {
			drawList->PopClipRect();
		}
	}

	void DrawManagedIconTexture(
		const Texture::Image& a_texture,
		ImVec2 a_center,
		DrawArgs a_drawArgs,
		bool a_highlight)
	{
		const auto layout = GetManagedIconLayoutTuning(a_highlight);
		const float offsetX = (a_highlight ?
			Config::Styling::Item::Highlight::Texture::OffsetX :
			Config::Styling::Item::Slot::Texture::OffsetX) +
			layout.offsetX;
		const float offsetY = (a_highlight ?
			Config::Styling::Item::Highlight::Texture::OffsetY :
			Config::Styling::Item::Slot::Texture::OffsetY) +
			layout.offsetY;
		const ImVec2 drawSize = ScaleSize(
			ComputeManagedIconDrawSize(a_texture, a_highlight),
			layout.scale);

		Drawer::draw_texture(
			a_texture.texture,
			a_center,
			offsetX,
			offsetY,
			drawSize,
			C_SKYRIMWHITE,
			a_drawArgs);
	}
}

WheelItemOStimAction::WheelItemOStimAction(OStimActionPayload a_payload) :
	_payload(std::move(a_payload))
{
	const std::string nativeLabel = _payload.displayName.empty() ?
		std::string(OStimIntegration::GetActionLabel(_payload.kind)) :
		_payload.displayName;
	_label = _payload.kind == OStimActionKind::SelectSpecificPosition ?
		BuildOStimSceneActionPresentationLabel(_payload.semantic, nativeLabel) :
		nativeLabel;
	const auto resolvedIcon = ResolveActionIcon(_payload);
	_texture = resolvedIcon.image;
	_usesCustomManagedIcon = resolvedIcon.usedCustomManagedIcon;
}

void WheelItemOStimAction::DrawSlot(ImVec2 a_center, bool, RE::TESObjectREFR::InventoryItemMap&, DrawArgs a_drawArgs)
{
	if (auto overrideTexture = ResolveRasterOverride(_payload); overrideTexture.image.texture) {
		DrawRasterOverride(overrideTexture, _texture, a_center, a_drawArgs, false);
	} else if (_texture.texture) {
		if (_usesCustomManagedIcon) {
			DrawManagedIconTexture(_texture, a_center, a_drawArgs, false);
		} else {
			drawSlotTexture(a_center, a_drawArgs);
		}
	}
	drawSlotText(a_center, _label.c_str(), a_drawArgs);
}

void WheelItemOStimAction::DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap&, DrawArgs a_drawArgs)
{
	const std::string desc = BuildDescription();
	const float textShiftY = calculateHighlightTextShiftY(desc.c_str());

	if (auto overrideTexture = ResolveRasterOverride(_payload); overrideTexture.image.texture) {
		DrawRasterOverride(overrideTexture, _texture, a_center, a_drawArgs, true);
	} else if (_texture.texture) {
		if (_usesCustomManagedIcon) {
			DrawManagedIconTexture(_texture, a_center, a_drawArgs, true);
		} else {
			drawHighlightTexture(a_center, a_drawArgs);
		}
	}
	drawHighlightText(a_center, _label.c_str(), a_drawArgs, textShiftY);

	if (!desc.empty()) {
		drawHighlightDescription(a_center, desc.c_str(), a_drawArgs, textShiftY);
	}
}

bool WheelItemOStimAction::IsActive(RE::TESObjectREFR::InventoryItemMap&)
{
	if (_payload.kind != OStimActionKind::SelectSpecificPosition) {
		return false;
	}

	const auto scene = OStimIntegration::GetCurrentSceneInfo();
	if (!scene || !scene->active) {
		return false;
	}

	return (!_payload.sceneID.empty() && scene->sceneID == _payload.sceneID) ||
	       (!_payload.positionID.empty() && scene->sceneID == _payload.positionID);
}

bool WheelItemOStimAction::IsAvailable(RE::TESObjectREFR::InventoryItemMap&)
{
	return OStimIntegration::CanExecuteAction(_payload.kind, &_payload);
}

void WheelItemOStimAction::ActivateItemPrimary()
{
	OStimIntegration::ExecuteAction(_payload.kind, &_payload);
}

void WheelItemOStimAction::ActivateItemSecondary()
{
	OStimIntegration::ExecuteAction(_payload.kind, &_payload);
}

void WheelItemOStimAction::ActivateItemSpecial()
{
	OStimIntegration::ExecuteAction(_payload.kind, &_payload);
}

void WheelItemOStimAction::SerializeIntoJsonObj(nlohmann::json& a_json)
{
	a_json["type"] = ITEM_TYPE_STR;
	a_json["formID"] = 0;
	a_json["kind"] = static_cast<std::uint32_t>(_payload.kind);
	a_json["sceneID"] = _payload.sceneID;
	a_json["positionID"] = _payload.positionID;
	a_json["sourceSceneID"] = _payload.sourceSceneID;
	a_json["displayName"] = _payload.displayName;
	a_json["previewPath"] = _payload.previewPath;
	a_json["iconPath"] = _payload.iconPath;
	a_json["category"] = _payload.category;
	a_json["subcategory"] = _payload.subcategory;
	a_json["requiresActiveScene"] = _payload.requiresActiveScene;
	a_json["browserPage"] = _payload.browserPage;
	a_json["browserFocusIndex"] = _payload.browserFocusIndex;
}

const char* WheelItemOStimAction::GetItemName() const
{
	return _label.c_str();
}

std::string WheelItemOStimAction::BuildDescription() const
{
	switch (_payload.kind) {
	case OStimActionKind::OpenPositionBrowser:
		return "Browse scene-compatible OStim positions.";
	case OStimActionKind::OpenPositionSubmenu:
		return "Legacy OStim browser action; no longer generated.";
	case OStimActionKind::ReturnToPositionBrowserParent:
		return "Legacy OStim browser action; no longer generated.";
	case OStimActionKind::ReturnToControlWheel:
		return "Return to the OStim control wheel.";
	case OStimActionKind::StopScene:
		return "Stop the active OStim scene.";
	case OStimActionKind::IncreaseSpeed:
		return "Increase OStim animation speed.";
	case OStimActionKind::DecreaseSpeed:
		return "Decrease OStim animation speed.";
	case OStimActionKind::NextPosition:
		return "Legacy OStim position action; no longer generated.";
	case OStimActionKind::PreviousPosition:
		return "Legacy OStim position action; no longer generated.";
	case OStimActionKind::SelectSpecificPosition:
		return BuildOStimSceneSemanticGuidance(_payload.semantic);
	case OStimActionKind::OpenControlWheel:
		return "Open the managed OStim control wheel.";
	case OStimActionKind::NextStage:
	case OStimActionKind::PreviousStage:
		return "This OStim build does not expose a direct stage API.";
	case OStimActionKind::SwapPartner:
		return "This OStim build does not expose a direct partner swap API.";
	case OStimActionKind::ChangeVariant:
		return "This OStim build does not expose a direct variant API.";
	default:
		return {};
	}
}
