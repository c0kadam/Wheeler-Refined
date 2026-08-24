#include "I4SwfIconRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

#include "RE/B/BSScaleformManager.h"
#include "RE/G/GFxLoader.h"
#include "I4Availability.h"
#include "I4Log.h"
#include "bin/Config.h"

namespace I4Integration
{
	namespace
	{
		constexpr std::uint32_t kCaptureLoadTimeoutFrames = 90;
		constexpr std::uint32_t kCaptureRenderDelayFrames = 1;
		constexpr std::uint32_t kCaptureMarginPx = 6;
		constexpr float kCropMinDimensionRatio = 0.70f;
		constexpr std::uint32_t kChromaRGB = 0x00FF00FF;  // Magenta background for keying.
		constexpr std::uint8_t kChromaR = 0xFF;
		constexpr std::uint8_t kChromaG = 0x00;
		constexpr std::uint8_t kChromaB = 0xFF;
		constexpr std::uint32_t kChromaTolLow = 10;
		constexpr std::uint32_t kChromaTolHigh = 64;
		constexpr bool kAllowLegacyOnscreenCaptureFallback = false;

		bool TraceEnabled()
		{
			return Config::I4::TraceLog;
		}

		bool TraceCacheHitsEnabled()
		{
			return Config::I4::TraceLog && Config::I4::TraceCacheHits;
		}

		std::string ToLower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
				if (c == '\\') {
					return '/';
				}
				return static_cast<char>(std::tolower(c));
			});
			return s;
		}

		bool IsDefaultSource(const std::string& source)
		{
			const std::string normalized = ToLower(source);
			return normalized.find("skyui/icons_item_psychosteve.swf") != std::string::npos;
		}

		bool IsTrackedGourmetDrugSpec(std::string_view source, std::string_view label)
		{
			return source.find("magus/gourmet/icons.swf") != std::string::npos &&
			       label == "gourmet_drugs";
		}

		void LogTrackedRenderedAverage(
			const char* stage,
			std::string_view source,
			std::string_view label,
			std::uint32_t requestedSizePx,
			std::uint32_t inputColorRGB,
			bool colorTransformApplied,
			std::uint32_t opaqueCount,
			std::uint64_t sumR,
			std::uint64_t sumG,
			std::uint64_t sumB)
		{
			if (opaqueCount == 0 || !IsTrackedGourmetDrugSpec(source, label)) {
				return;
			}

			const auto avgR = static_cast<std::uint32_t>(sumR / opaqueCount);
			const auto avgG = static_cast<std::uint32_t>(sumG / opaqueCount);
			const auto avgB = static_cast<std::uint32_t>(sumB / opaqueCount);
			logger::info(
				"[I4ColorDiag:gourmet_drugs][{}] source='{}' label='{}' size={} inputColorRGB=0x{:06X} colorTransformApplied={} opaque={} avgRGB=0x{:02X}{:02X}{:02X}",
				stage,
				source,
				label,
				requestedSizePx,
				inputColorRGB,
				colorTransformApplied,
				opaqueCount,
				avgR,
				avgG,
				avgB);
		}

		std::optional<Texture::icon_image_type> LabelToBuiltInType(const std::string& label)
		{
			const std::string key = ToLower(label);

			if (key == "weapon_sword") { return Texture::icon_image_type::sword_one_handed; }
			if (key == "weapon_dagger") { return Texture::icon_image_type::dagger; }
			if (key == "weapon_waraxe") { return Texture::icon_image_type::axe_one_handed; }
			if (key == "weapon_mace") { return Texture::icon_image_type::mace; }
			if (key == "weapon_greatsword") { return Texture::icon_image_type::sword_two_handed; }
			if (key == "weapon_battleaxe") { return Texture::icon_image_type::axe_two_handed; }
			if (key == "weapon_hammer") { return Texture::icon_image_type::warhammer_two_handed; }
			if (key == "weapon_bow") { return Texture::icon_image_type::bow; }
			if (key == "weapon_crossbow") { return Texture::icon_image_type::crossbow; }
			if (key == "weapon_staff") { return Texture::icon_image_type::staff; }
			if (key == "weapon_pickaxe") { return Texture::icon_image_type::axe_two_handed; }
			if (key == "weapon_woodaxe") { return Texture::icon_image_type::axe_one_handed; }
			if (key == "default_weapon") { return Texture::icon_image_type::sword_one_handed; }
			if (key == "weapon_arrow") { return Texture::icon_image_type::arrow; }
			if (key == "weapon_bolt") { return Texture::icon_image_type::crossbow; }
			if (key == "default_scroll") { return Texture::icon_image_type::scroll; }
			if (key == "misc_torch") { return Texture::icon_image_type::torch; }
			if (key == "default_book") { return Texture::icon_image_type::icon_default; }
			if (key == "book_tome") { return Texture::icon_image_type::icon_default; }
			if (key == "book_note") { return Texture::icon_image_type::icon_default; }
			if (key == "default_food") { return Texture::icon_image_type::food; }
			if (key == "food_wine") { return Texture::icon_image_type::food; }
			if (key == "default_potion") { return Texture::icon_image_type::potion_default; }
			if (key == "potion_poison") { return Texture::icon_image_type::poison_default; }
			if (key == "potion_health") { return Texture::icon_image_type::potion_health; }
			if (key == "potion_magic") { return Texture::icon_image_type::potion_magicka; }
			if (key == "potion_stam") { return Texture::icon_image_type::potion_stamina; }
			if (key == "potion_fire") { return Texture::icon_image_type::potion_fire_resist; }
			if (key == "potion_shock") { return Texture::icon_image_type::potion_shock_resist; }
			if (key == "potion_frost") { return Texture::icon_image_type::potion_frost_resist; }
			if (key == "default_armor") { return Texture::icon_image_type::armor_default; }
			if (key == "armor_head") { return Texture::icon_image_type::armor_heavy_head; }
			if (key == "armor_body") { return Texture::icon_image_type::armor_heavy_chest; }
			if (key == "armor_hands") { return Texture::icon_image_type::armor_heavy_arm; }
			if (key == "armor_forearms") { return Texture::icon_image_type::armor_heavy_arm; }
			if (key == "armor_feet") { return Texture::icon_image_type::armor_heavy_foot; }
			if (key == "armor_calves") { return Texture::icon_image_type::armor_heavy_foot; }
			if (key == "armor_shield") { return Texture::icon_image_type::armor_heavy_shield; }
			if (key == "lightarmor_head") { return Texture::icon_image_type::armor_light_head; }
			if (key == "lightarmor_body") { return Texture::icon_image_type::armor_light_chest; }
			if (key == "lightarmor_hands") { return Texture::icon_image_type::armor_light_arm; }
			if (key == "lightarmor_forearms") { return Texture::icon_image_type::armor_light_arm; }
			if (key == "lightarmor_feet") { return Texture::icon_image_type::armor_light_foot; }
			if (key == "lightarmor_calves") { return Texture::icon_image_type::armor_light_foot; }
			if (key == "lightarmor_shield") { return Texture::icon_image_type::armor_light_shield; }
			if (key == "clothing_head") { return Texture::icon_image_type::armor_clothing_head; }
			if (key == "clothing_body") { return Texture::icon_image_type::armor_clothing_chest; }
			if (key == "clothing_hands") { return Texture::icon_image_type::armor_clothing_arm; }
			if (key == "clothing_forearms") { return Texture::icon_image_type::armor_clothing_arm; }
			if (key == "clothing_feet") { return Texture::icon_image_type::armor_clothing_foot; }
			if (key == "clothing_calves") { return Texture::icon_image_type::armor_clothing_foot; }
			if (key == "clothing_shield") { return Texture::icon_image_type::armor_light_shield; }
			if (key == "armor_amulet") { return Texture::icon_image_type::armor_necklace; }
			if (key == "armor_ring") { return Texture::icon_image_type::armor_ring; }
			if (key == "armor_circlet") { return Texture::icon_image_type::armor_circlet; }
			if (key == "magic_fire") { return Texture::icon_image_type::destruction_fire; }
			if (key == "magic_shock") { return Texture::icon_image_type::destruction_shock; }
			if (key == "magic_frost") { return Texture::icon_image_type::destruction_frost; }
			if (key == "default_misc") { return Texture::icon_image_type::icon_default; }
			if (key == "misc_gem") { return Texture::icon_image_type::icon_default; }
			if (key == "misc_hide") { return Texture::icon_image_type::icon_default; }
			if (key == "misc_remains") { return Texture::icon_image_type::icon_default; }
			if (key == "misc_ingot") { return Texture::icon_image_type::icon_default; }
			if (key == "misc_wood") { return Texture::icon_image_type::icon_default; }
			if (key == "misc_dragonclaw") { return Texture::icon_image_type::icon_default; }
			if (key == "misc_lockpick") { return Texture::icon_image_type::icon_default; }
			if (key == "misc_gold") { return Texture::icon_image_type::icon_default; }
			if (key == "misc_leather") { return Texture::icon_image_type::icon_default; }
			if (key == "misc_strips") { return Texture::icon_image_type::icon_default; }
			if (key == "misc_clutter") { return Texture::icon_image_type::icon_default; }
			if (key == "misc_artifact") { return Texture::icon_image_type::icon_default; }
			if (key == "default_ingredient") { return Texture::icon_image_type::icon_default; }
			if (key == "default_soulgem") { return Texture::icon_image_type::icon_default; }
			if (key == "soulgem_empty") { return Texture::icon_image_type::icon_default; }
			if (key == "soulgem_partial") { return Texture::icon_image_type::icon_default; }
			if (key == "soulgem_full") { return Texture::icon_image_type::icon_default; }
			if (key == "soulgem_grandempty") { return Texture::icon_image_type::icon_default; }
			if (key == "soulgem_grandpartial") { return Texture::icon_image_type::icon_default; }
			if (key == "soulgem_grandfull") { return Texture::icon_image_type::icon_default; }
			if (key == "soulgem_azura") { return Texture::icon_image_type::icon_default; }
			if (key == "default_key") { return Texture::icon_image_type::icon_default; }
			if (key == "default_power") { return Texture::icon_image_type::power; }
			if (key == "default_alteration") { return Texture::icon_image_type::alteration; }
			if (key == "default_conjuration") { return Texture::icon_image_type::conjuration; }
			if (key == "default_destruction") { return Texture::icon_image_type::destruction; }
			if (key == "default_illusion") { return Texture::icon_image_type::illusion; }
			if (key == "default_restoration") { return Texture::icon_image_type::restoration; }

			return std::nullopt;
		}

		bool DrawCaptureBackground(RE::GFxValue& clip, float width, float height)
		{
			if (!clip.IsDisplayObject()) {
				return false;
			}

			clip.Invoke("clear");

			std::array<RE::GFxValue, 2> fillArgs{
				static_cast<double>(kChromaRGB),
				100.0
			};
			clip.Invoke("beginFill", fillArgs);

			std::array<RE::GFxValue, 2> p0{ 0.0, 0.0 };
			std::array<RE::GFxValue, 2> p1{ width, 0.0 };
			std::array<RE::GFxValue, 2> p2{ width, height };
			std::array<RE::GFxValue, 2> p3{ 0.0, height };

			clip.Invoke("moveTo", p0);
			clip.Invoke("lineTo", p1);
			clip.Invoke("lineTo", p2);
			clip.Invoke("lineTo", p3);
			clip.Invoke("lineTo", p0);
			clip.Invoke("endFill");
			return true;
		}

		float GetNumberMember(const RE::GFxValue& object, const char* memberName)
		{
			RE::GFxValue value;
			object.GetMember(memberName, &value);
			if (!value.IsNumber()) {
				return 0.0f;
			}
			return static_cast<float>(value.GetNumber());
		}

		void ExpandRangeToMinSize(std::uint32_t& minValue, std::uint32_t& maxValue, std::uint32_t maxExtent, std::uint32_t desiredSize)
		{
			if (maxExtent == 0 || minValue > maxValue) {
				return;
			}

			desiredSize = (std::clamp)(desiredSize, 1u, maxExtent);
			std::uint32_t currentSize = maxValue - minValue + 1u;
			if (currentSize >= desiredSize) {
				return;
			}

			const std::uint32_t center = (minValue + maxValue) / 2u;
			std::uint32_t newMin = center > (desiredSize / 2u) ? center - (desiredSize / 2u) : 0u;
			std::uint32_t newMax = newMin + desiredSize - 1u;
			if (newMax >= maxExtent) {
				newMax = maxExtent - 1u;
				newMin = newMax + 1u > desiredSize ? (newMax + 1u - desiredSize) : 0u;
			}

			minValue = newMin;
			maxValue = newMax;
		}
	}

	I4SwfIconRenderer& I4SwfIconRenderer::GetSingleton()
	{
		static I4SwfIconRenderer singleton;
		return singleton;
	}

	Texture::Image I4SwfIconRenderer::ResolveBuiltInIcon(const I4IconSpec& spec)
	{
		if (!IsDefaultSource(spec.iconSource)) {
			return {};
		}

		if (auto mappedType = LabelToBuiltInType(spec.iconLabel); mappedType.has_value()) {
			return Texture::GetIconImage(mappedType.value());
		}

		return {};
	}

	std::string I4SwfIconRenderer::BuildInterfaceMoviePath(std::string_view source)
	{
		std::string path(source);
		std::replace(path.begin(), path.end(), '\\', '/');

		while (path.rfind("./", 0) == 0) {
			path.erase(0, 2);
		}
		while (!path.empty() && path.front() == '/') {
			path.erase(path.begin());
		}

		std::string lower = ToLower(path);
		if (lower.rfind("data/", 0) == 0) {
			path.erase(0, 5);
			lower.erase(0, 5);
		}
		if (lower.rfind("interface/", 0) != 0) {
			path = std::string("Interface/") + path;
		}
		return path;
	}

	Texture::Image I4SwfIconRenderer::TryRenderOffscreenDirect(const I4IconSpec& spec, std::uint32_t requestedSizePx) const
	{
		Texture::Image image{};
		const bool trace = TraceEnabled();
		const std::string normalizedSource = ToLower(spec.iconSource);
		const std::string normalizedLabel = ToLower(spec.iconLabel);
		const auto fail = [&](std::string_view reason) {
			if (trace) {
				I4_LOG_INFO("[I4][TRACE][renderer.offscreen.out] source='{}' label='{}' size={} decision=fail reason={}",
					normalizedSource,
					normalizedLabel,
					requestedSizePx,
					reason);
			}
			return Texture::Image{};
		};
		if (trace) {
			I4_LOG_INFO("[I4][TRACE][renderer.offscreen.req] source='{}' label='{}' size={}",
				normalizedSource,
				normalizedLabel,
				requestedSizePx);
		}

		auto* scaleform = RE::BSScaleformManager::GetSingleton();
		if (!scaleform || !scaleform->loader) {
			return fail("missing_scaleform_loader");
		}

		const std::uint32_t sizePx = (std::clamp)(requestedSizePx, 16u, 1024u);
		const auto loadFlags = static_cast<RE::GFxLoader::LoadConstants>(
			static_cast<std::uint32_t>(RE::GFxLoader::LoadConstants::kLoadWaitCompletion) |
			static_cast<std::uint32_t>(RE::GFxLoader::LoadConstants::kLoadWaitFrame1) |
			static_cast<std::uint32_t>(RE::GFxLoader::LoadConstants::kLoadImageFiles) |
			static_cast<std::uint32_t>(RE::GFxLoader::LoadConstants::kLoadQuietOpen));

		const std::string moviePath = BuildInterfaceMoviePath(spec.iconSource);
		RE::GPtr<RE::GFxMovieDef> movieDef{ scaleform->loader->CreateMovie(moviePath.c_str(), loadFlags) };
		if (!movieDef) {
			return fail("create_movie_def_failed");
		}

		RE::GPtr<RE::GFxMovieView> movie{ movieDef->CreateInstance(true) };
		if (!movie) {
			return fail("create_movie_instance_failed");
		}

		movie->SetBackgroundAlpha(0.0f);
		movie->SetViewScaleMode(RE::GFxMovieView::ScaleModeType::kNoScale);
		movie->SetViewAlignment(RE::GFxMovieView::AlignType::kTopLeft);
		movie->SetViewport(
			static_cast<std::int32_t>(sizePx),
			static_cast<std::int32_t>(sizePx),
			0,
			0,
			static_cast<std::int32_t>(sizePx),
			static_cast<std::int32_t>(sizePx),
			RE::GViewport::kNone);

		RE::GFxValue root;
		movie->GetVariable(&root, "_root");
		if (!root.IsDisplayObject()) {
			return fail("root_not_display_object");
		}

		RE::GFxValue iconLabel(spec.iconLabel);
		root.Invoke("gotoAndStop", nullptr, &iconLabel, 1);
		root.SetMember("_visible", true);
		root.SetMember("_alpha", 100.0);
		root.SetMember("_xscale", 100.0);
		root.SetMember("_yscale", 100.0);
		root.SetMember("_x", 0.0);
		root.SetMember("_y", 0.0);

		float baseWidth = std::fabs(GetNumberMember(root, "_width"));
		float baseHeight = std::fabs(GetNumberMember(root, "_height"));
		if (baseWidth < 1.0f || baseHeight < 1.0f) {
			baseWidth = static_cast<float>(sizePx);
			baseHeight = static_cast<float>(sizePx);
		}

		const float targetWidth = (std::max)(8.0f, static_cast<float>(sizePx) * 0.92f);
		const float targetHeight = (std::max)(8.0f, static_cast<float>(sizePx) * 0.92f);
		const float fitScale = (std::min)(targetWidth / baseWidth, targetHeight / baseHeight);
		if (std::isfinite(fitScale) && fitScale > 0.01f) {
			const double scalePct = static_cast<double>(fitScale * 100.0f);
			root.SetMember("_xscale", scalePct);
			root.SetMember("_yscale", scalePct);
		}

		const float scaledWidth = std::fabs(GetNumberMember(root, "_width"));
		const float scaledHeight = std::fabs(GetNumberMember(root, "_height"));
		const float finalWidth = scaledWidth > 1.0f ? scaledWidth : baseWidth;
		const float finalHeight = scaledHeight > 1.0f ? scaledHeight : baseHeight;
		root.SetMember("_x", static_cast<double>((static_cast<float>(sizePx) - finalWidth) * 0.5f));
		root.SetMember("_y", static_cast<double>((static_cast<float>(sizePx) - finalHeight) * 0.5f));

		const bool applyColorTransform = true;
		ApplyColorTransform(root, ColorToRGB(spec.color));
		movie->Advance(0.0f);

		auto* rendererData = RE::BSGraphics::Renderer::GetRendererDataSingleton();
		auto* context = rendererData ? reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) : nullptr;
		auto* device = Texture::device_;
		if (!context || !device) {
			return fail("missing_device_or_context");
		}

		D3D11_TEXTURE2D_DESC targetDesc{};
		targetDesc.Width = sizePx;
		targetDesc.Height = sizePx;
		targetDesc.MipLevels = 1;
		targetDesc.ArraySize = 1;
		targetDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		targetDesc.SampleDesc.Count = 1;
		targetDesc.Usage = D3D11_USAGE_DEFAULT;
		targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
		targetDesc.CPUAccessFlags = 0;
		targetDesc.MiscFlags = 0;

		ID3D11Texture2D* renderTexture = nullptr;
		if (FAILED(device->CreateTexture2D(&targetDesc, nullptr, &renderTexture)) || !renderTexture) {
			return fail("render_texture_create_failed");
		}

		ID3D11RenderTargetView* renderRTV = nullptr;
		if (FAILED(device->CreateRenderTargetView(renderTexture, nullptr, &renderRTV)) || !renderRTV) {
			renderTexture->Release();
			return fail("render_rtv_create_failed");
		}

		ID3D11RenderTargetView* previousRTV = nullptr;
		ID3D11DepthStencilView* previousDSV = nullptr;
		context->OMGetRenderTargets(1, &previousRTV, &previousDSV);

		UINT previousViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		D3D11_VIEWPORT previousViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
		context->RSGetViewports(&previousViewportCount, previousViewports);

		context->OMSetRenderTargets(1, &renderRTV, nullptr);
		const D3D11_VIEWPORT captureViewport{
			0.0f, 0.0f,
			static_cast<float>(sizePx),
			static_cast<float>(sizePx),
			0.0f, 1.0f
		};
		context->RSSetViewports(1, &captureViewport);
		const float clearColor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		context->ClearRenderTargetView(renderRTV, clearColor);

		movie->Display();

		context->OMSetRenderTargets(1, &previousRTV, previousDSV);
		if (previousViewportCount > 0) {
			context->RSSetViewports(previousViewportCount, previousViewports);
		}
		if (previousRTV) {
			previousRTV->Release();
		}
		if (previousDSV) {
			previousDSV->Release();
		}
		renderRTV->Release();

		D3D11_TEXTURE2D_DESC stagingDesc = targetDesc;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ID3D11Texture2D* stagingTexture = nullptr;
		if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture)) || !stagingTexture) {
			renderTexture->Release();
			return fail("staging_texture_create_failed");
		}

		context->CopyResource(stagingTexture, renderTexture);
		renderTexture->Release();

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped))) {
			stagingTexture->Release();
			return fail("map_staging_failed");
		}

		std::vector<std::uint8_t> rgbaPixels(sizePx * sizePx * 4, 0);
		std::uint32_t opaqueCount = 0;
		std::uint64_t sumR = 0;
		std::uint64_t sumG = 0;
		std::uint64_t sumB = 0;
		for (std::uint32_t y = 0; y < sizePx; ++y) {
			const auto* srcRow = static_cast<const std::uint8_t*>(mapped.pData) + (y * mapped.RowPitch);
			auto* dstRow = rgbaPixels.data() + (y * sizePx * 4);
			for (std::uint32_t x = 0; x < sizePx; ++x) {
				const auto* src = srcRow + (x * 4);
				std::uint8_t b = src[0];
				std::uint8_t g = src[1];
				std::uint8_t r = src[2];
				const std::uint8_t a = src[3];

				if (a > 0 && a < 255) {
					const float af = static_cast<float>(a) / 255.0f;
					r = static_cast<std::uint8_t>(std::clamp(static_cast<float>(r) / af, 0.0f, 255.0f));
					g = static_cast<std::uint8_t>(std::clamp(static_cast<float>(g) / af, 0.0f, 255.0f));
					b = static_cast<std::uint8_t>(std::clamp(static_cast<float>(b) / af, 0.0f, 255.0f));
				}
				if (a > 8) {
					++opaqueCount;
					sumR += r;
					sumG += g;
					sumB += b;
				}

				auto* dst = dstRow + (x * 4);
				dst[0] = r;
				dst[1] = g;
				dst[2] = b;
				dst[3] = a;
			}
		}

		context->Unmap(stagingTexture, 0);
		stagingTexture->Release();
		LogTrackedRenderedAverage(
			"offscreen",
			normalizedSource,
			normalizedLabel,
			requestedSizePx,
			ColorToRGB(spec.color),
			applyColorTransform,
			opaqueCount,
			sumR,
			sumG,
			sumB);

		const std::uint32_t minOpaquePixels = (std::max)(12u, (sizePx * sizePx) / 800u);
		if (opaqueCount < minOpaquePixels) {
			return fail("opaque_pixels_too_low");
		}

		D3D11_TEXTURE2D_DESC outDesc{};
		outDesc.Width = sizePx;
		outDesc.Height = sizePx;
		outDesc.MipLevels = 1;
		outDesc.ArraySize = 1;
		outDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		outDesc.SampleDesc.Count = 1;
		outDesc.Usage = D3D11_USAGE_DEFAULT;
		outDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA outData{};
		outData.pSysMem = rgbaPixels.data();
		outData.SysMemPitch = sizePx * 4;

		ID3D11Texture2D* outTexture = nullptr;
		if (FAILED(device->CreateTexture2D(&outDesc, &outData, &outTexture)) || !outTexture) {
			return fail("output_texture_create_failed");
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		ID3D11ShaderResourceView* outSRV = nullptr;
		if (FAILED(device->CreateShaderResourceView(outTexture, &srvDesc, &outSRV)) || !outSRV) {
			outTexture->Release();
			return fail("output_srv_create_failed");
		}
		outTexture->Release();

		image.texture = outSRV;
		image.width = static_cast<int>(sizePx);
		image.height = static_cast<int>(sizePx);
		if (trace) {
			I4_LOG_INFO("[I4][TRACE][renderer.offscreen.out] source='{}' label='{}' size={} decision=ok image={}x{} opaque={}",
				normalizedSource,
				normalizedLabel,
				requestedSizePx,
				image.width,
				image.height,
				opaqueCount);
		}
		return image;
	}

	Texture::Image I4SwfIconRenderer::GetIcon(const I4IconSpec& spec, std::uint32_t requestedSizePx, bool allowExtraction)
	{
		if (!spec.valid) {
			if (TraceEnabled()) {
				I4_LOG_INFO("[I4][TRACE][renderer.out] decision=invalid_spec");
			}
			return {};
		}

		ProcessExtractionJobs();

		const RenderKey key{
			ToLower(spec.iconSource),
			ToLower(spec.iconLabel),
			spec.color,
			requestedSizePx
		};

		{
			std::scoped_lock lock(_lock);
			auto it = _cache.find(key);
			if (it != _cache.end()) {
				it->second.tick = ++_tickCounter;
				GetStats().renderCacheHits.fetch_add(1, std::memory_order_relaxed);
				if (TraceCacheHitsEnabled()) {
					I4_LOG_INFO("[I4][TRACE][renderer.out] source='{}' label='{}' size={} decision=cache_hit texture={} owned={} precolored={} image={}x{}",
						key.source,
						key.label,
						key.size,
						it->second.image.texture != nullptr,
						it->second.owned,
						it->second.precolored,
						it->second.image.width,
						it->second.image.height);
				}
				return it->second.image;
			}
		}

		GetStats().renderCalls.fetch_add(1, std::memory_order_relaxed);
		const bool extractionEnabled = Config::I4::ExtractionMode && allowExtraction;
		if (TraceEnabled()) {
			I4_LOG_INFO("[I4][TRACE][renderer.req] source='{}' label='{}' size={} extractionMaster={} allowExtraction={} extractionEnabled={}",
				key.source,
				key.label,
				key.size,
				Config::I4::ExtractionMode,
				allowExtraction,
				extractionEnabled);
		}
		const bool sourceIsDefault = IsDefaultSource(spec.iconSource);
		if (extractionEnabled) {
			auto directImage = TryRenderOffscreenDirect(spec, requestedSizePx);
			if (directImage.texture) {
				CacheResult(key, directImage, true, true);
				GetStats().renderOffscreenHits.fetch_add(1, std::memory_order_relaxed);
				if (TraceEnabled()) {
					I4_LOG_INFO("[I4][TRACE][renderer.out] source='{}' label='{}' size={} decision=offscreen_direct image={}x{}",
						key.source,
						key.label,
						key.size,
						directImage.width,
					directImage.height);
				}
				return directImage;
			}
			if (sourceIsDefault) {
				Texture::Image builtInAfterOffscreenFail = ResolveBuiltInIcon(spec);
				if (builtInAfterOffscreenFail.texture) {
					CacheResult(key, builtInAfterOffscreenFail, false, false);
					GetStats().renderBuiltInHits.fetch_add(1, std::memory_order_relaxed);
					if (TraceEnabled()) {
						I4_LOG_INFO("[I4][TRACE][renderer.out] source='{}' label='{}' size={} decision=builtin_after_offscreen_fail image={}x{}",
							key.source,
							key.label,
							key.size,
							builtInAfterOffscreenFail.width,
							builtInAfterOffscreenFail.height);
					}
					return builtInAfterOffscreenFail;
				}
			}
			if (!kAllowLegacyOnscreenCaptureFallback) {
				if (Config::I4::DebugLog) {
					const auto warnKey = fmt::format("{}|{}|{}|{}|{}",
						key.source,
						key.label,
						key.color,
						key.size,
						"offscreen_direct_failed");
					std::scoped_lock lock(_lock);
					if (_warned.insert(warnKey).second) {
						I4_LOG_WARN("[I4] Offscreen direct render failed for label='{}' source='{}'. Falling back to Wheeler icon.",
							key.label,
							key.source);
					}
				}
				GetStats().renderFailures.fetch_add(1, std::memory_order_relaxed);
				CacheResult(key, {}, false, false);
				if (TraceEnabled()) {
					I4_LOG_INFO("[I4][TRACE][renderer.out] source='{}' label='{}' size={} decision=fallback reason=offscreen_direct_failed_legacy_disabled",
						key.source,
						key.label,
						key.size);
				}
				return {};
			}
		}

		Texture::Image image = ResolveBuiltInIcon(spec);
		if (image.texture) {
			CacheResult(key, image, false, false);
			GetStats().renderBuiltInHits.fetch_add(1, std::memory_order_relaxed);
			if (TraceEnabled()) {
				I4_LOG_INFO("[I4][TRACE][renderer.out] source='{}' label='{}' size={} decision=builtin image={}x{}",
					key.source,
					key.label,
					key.size,
					image.width,
					image.height);
			}
			return image;
		}

		if (extractionEnabled) {
			bool enqueue = false;
			std::scoped_lock lock(_lock);
			if (!_pendingRequests.contains(key)) {
				_pendingRequests.emplace(key, PendingRequest{ spec, requestedSizePx, 0 });
				enqueue = true;
				GetStats().renderQueueEnqueued.fetch_add(1, std::memory_order_relaxed);
				if (TraceEnabled()) {
					I4_LOG_INFO("[I4][TRACE][renderer.queue] source='{}' label='{}' size={} decision=enqueued pendingCount={}",
						key.source,
						key.label,
						key.size,
						_pendingRequests.size());
				}
			}
			if (!enqueue) {
				GetStats().renderQueuePending.fetch_add(1, std::memory_order_relaxed);
				if (TraceEnabled()) {
					I4_LOG_INFO("[I4][TRACE][renderer.out] source='{}' label='{}' size={} decision=pending_existing",
						key.source,
						key.label,
						key.size);
				}
				return {};
			}
		}

		if (extractionEnabled) {
			StartNextPendingJob();
			if (TraceEnabled()) {
				I4_LOG_INFO("[I4][TRACE][renderer.out] source='{}' label='{}' size={} decision=queued_wait_capture",
					key.source,
					key.label,
					key.size);
			}
			return {};
		}

		GetStats().renderFailures.fetch_add(1, std::memory_order_relaxed);
		CacheResult(key, {}, false, false);
		if (TraceEnabled()) {
			I4_LOG_INFO("[I4][TRACE][renderer.out] source='{}' label='{}' size={} decision=fallback reason=extraction_disabled_no_builtin",
				key.source,
				key.label,
				key.size);
		}
		return {};
	}

	bool I4SwfIconRenderer::TryGetCachedIcon(
		const I4IconSpec& spec,
		std::uint32_t requestedSizePx,
		Texture::Image& outImage,
		bool& outPrecolored)
	{
		outImage = {};
		outPrecolored = false;
		if (!spec.valid) {
			if (TraceCacheHitsEnabled()) {
				I4_LOG_INFO("[I4][TRACE][renderer.cached] decision=miss reason=invalid_spec");
			}
			return false;
		}

		const RenderKey key{
			ToLower(spec.iconSource),
			ToLower(spec.iconLabel),
			spec.color,
			requestedSizePx
		};

		std::scoped_lock lock(_lock);
		auto it = _cache.find(key);
		if (it == _cache.end() || !it->second.image.texture) {
			if (TraceCacheHitsEnabled()) {
				I4_LOG_INFO("[I4][TRACE][renderer.cached] source='{}' label='{}' size={} decision=miss cached={} texture={}",
					key.source,
					key.label,
					key.size,
					it != _cache.end(),
					it != _cache.end() && it->second.image.texture != nullptr);
			}
			return false;
		}

		it->second.tick = ++_tickCounter;
		outImage = it->second.image;
		outPrecolored = it->second.precolored;
		GetStats().renderCacheHits.fetch_add(1, std::memory_order_relaxed);
		if (TraceCacheHitsEnabled()) {
			I4_LOG_INFO("[I4][TRACE][renderer.cached] source='{}' label='{}' size={} decision=hit owned={} precolored={} image={}x{}",
				key.source,
				key.label,
				key.size,
				it->second.owned,
				it->second.precolored,
				outImage.width,
				outImage.height);
		}
		return true;
	}

	bool I4SwfIconRenderer::IsPrecolored(const I4IconSpec& spec, std::uint32_t requestedSizePx)
	{
		if (!spec.valid) {
			return false;
		}

		const RenderKey key{
			ToLower(spec.iconSource),
			ToLower(spec.iconLabel),
			spec.color,
			requestedSizePx
		};

		std::scoped_lock lock(_lock);
		const auto it = _cache.find(key);
		if (it == _cache.end()) {
			return false;
		}
		return it->second.precolored;
	}

	void I4SwfIconRenderer::ProcessExtractionJobs()
	{
		const bool trace = TraceEnabled();
		// This function may be called many times per frame (once per icon draw).
		// Run extraction state transitions only once per ImGui frame, otherwise
		// the job can advance from load->capture before Scaleform has rendered.
		const int imguiFrame = ImGui::GetFrameCount();
		{
			std::scoped_lock lock(_lock);
			if (imguiFrame == _lastProcessedImGuiFrame) {
				return;
			}
			_lastProcessedImGuiFrame = imguiFrame;
		}

		std::optional<PendingJob> jobToCleanup;
		{
			std::scoped_lock lock(_lock);
			if (!Config::I4::ExtractionMode) {
				if (trace && (!_pendingRequests.empty() || _activeJob.has_value())) {
					I4_LOG_INFO("[I4][TRACE][renderer.jobs] action=clear_disabled pending={} hadActive={}",
						_pendingRequests.size(),
						_activeJob.has_value());
				}
				_pendingRequests.clear();
				if (_activeJob.has_value()) {
					jobToCleanup.emplace(std::move(_activeJob.value()));
					_activeJob.reset();
				}
			}
		}

		if (jobToCleanup.has_value()) {
			CleanupJobClip(jobToCleanup.value());
			return;
		}

		if (!Config::I4::ExtractionMode) {
			return;
		}

		StartNextPendingJob();

		std::optional<PendingJob> snapshot;
		{
			std::scoped_lock lock(_lock);
			if (_activeJob.has_value()) {
				snapshot.emplace(_activeJob.value());
			}
		}
		if (!snapshot.has_value()) {
			return;
		}

		switch (snapshot->stage) {
		case JobStage::WaitLoad:
			{
				if (IsIconClipLoaded(snapshot.value())) {
					std::scoped_lock lock(_lock);
					if (_activeJob.has_value()) {
						PrepareLoadedIcon(_activeJob.value());
						_activeJob->stage = JobStage::WaitRender;
						_activeJob->stageFrames = 0;
						if (trace) {
							I4_LOG_INFO("[I4][TRACE][renderer.jobs] source='{}' label='{}' size={} stage=wait_load decision=loaded_next=wait_render",
								_activeJob->key.source,
								_activeJob->key.label,
								_activeJob->key.size);
						}
					}
					return;
				}

				bool shouldFail = false;
				{
					std::scoped_lock lock(_lock);
					if (_activeJob.has_value()) {
						_activeJob->stageFrames += 1;
						shouldFail = _activeJob->stageFrames > kCaptureLoadTimeoutFrames;
					}
				}

				if (shouldFail) {
					if (trace) {
						I4_LOG_INFO("[I4][TRACE][renderer.jobs] source='{}' label='{}' size={} stage=wait_load decision=timeout frames={} limit={}",
							snapshot->key.source,
							snapshot->key.label,
							snapshot->key.size,
							snapshot->stageFrames + 1u,
							kCaptureLoadTimeoutFrames);
					}
					FinishActiveJob({}, false, true, "load_timeout");
				}
			}
			break;
		case JobStage::WaitRender:
			{
				bool readyToCapture = false;
				{
					std::scoped_lock lock(_lock);
					if (_activeJob.has_value()) {
						_activeJob->stageFrames += 1;
						readyToCapture = _activeJob->stageFrames > kCaptureRenderDelayFrames;
					}
				}
				if (!readyToCapture) {
					return;
				}

				Texture::Image captured = CaptureJobTexture(snapshot.value());
				if (captured.texture) {
					if (trace) {
						I4_LOG_INFO("[I4][TRACE][renderer.jobs] source='{}' label='{}' size={} stage=wait_render decision=captured image={}x{}",
							snapshot->key.source,
							snapshot->key.label,
							snapshot->key.size,
							captured.width,
							captured.height);
					}
					FinishActiveJob(captured, true, false, {});
				} else {
					if (trace) {
						I4_LOG_INFO("[I4][TRACE][renderer.jobs] source='{}' label='{}' size={} stage=wait_render decision=capture_failed",
							snapshot->key.source,
							snapshot->key.label,
							snapshot->key.size);
					}
					FinishActiveJob({}, false, true, "capture_failed");
				}
			}
			break;
		}
	}

	void I4SwfIconRenderer::StartNextPendingJob()
	{
		const bool trace = TraceEnabled();
		RenderKey nextKey{};
		PendingRequest nextRequest{};
		bool hasRequest = false;
		{
			std::scoped_lock lock(_lock);
			if (_activeJob.has_value() || _pendingRequests.empty()) {
				return;
			}
			auto it = _pendingRequests.begin();
			nextKey = it->first;
			nextRequest = it->second;
			_pendingRequests.erase(it);
			hasRequest = true;
			if (trace) {
				I4_LOG_INFO("[I4][TRACE][renderer.queue] source='{}' label='{}' size={} decision=dequeued remainingPending={}",
					nextKey.source,
					nextKey.label,
					nextKey.size,
					_pendingRequests.size());
			}
		}

		if (!hasRequest) {
			return;
		}

		PendingJob newJob{};
		if (!InitializeJob(newJob, nextRequest, nextKey)) {
			CacheResult(nextKey, {}, false, false);
			GetStats().renderFailures.fetch_add(1, std::memory_order_relaxed);
			if (trace) {
				I4_LOG_INFO("[I4][TRACE][renderer.queue] source='{}' label='{}' size={} decision=job_init_failed",
					nextKey.source,
					nextKey.label,
					nextKey.size);
			}
			const auto warnKey = fmt::format("{}|{}|{}|{}|{}",
				nextKey.source,
				nextKey.label,
				nextKey.color,
				nextKey.size,
				"job_init_failed");
			std::scoped_lock lock(_lock);
			if (_warned.insert(warnKey).second) {
				I4_LOG_WARN("[I4] ExtractionMode failed to initialize capture job for label='{}' source='{}'.",
					nextKey.label,
					nextKey.source);
			}
			return;
		}

		std::scoped_lock lock(_lock);
		_activeJob = std::move(newJob);
		if (trace) {
			I4_LOG_INFO("[I4][TRACE][renderer.queue] source='{}' label='{}' size={} decision=job_started capture=({},{} {}x{})",
				_activeJob->key.source,
				_activeJob->key.label,
				_activeJob->key.size,
				_activeJob->captureX,
				_activeJob->captureY,
				_activeJob->captureW,
				_activeJob->captureH);
		}
	}

	bool I4SwfIconRenderer::InitializeJob(PendingJob& job, const PendingRequest& request, const RenderKey& key)
	{
		const bool trace = TraceEnabled();
		const auto fail = [&](std::string_view reason) {
			if (trace) {
				I4_LOG_INFO("[I4][TRACE][renderer.job.init] source='{}' label='{}' size={} decision=fail reason={}",
					key.source,
					key.label,
					key.size,
					reason);
			}
			return false;
		};

		auto* movie = I4Availability::GetSingleton().GetInvokerMovie();
		if (!movie) {
			return fail("no_invoker_movie");
		}

		const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
		if (displaySize.x <= 0.0f || displaySize.y <= 0.0f) {
			return fail("invalid_display_size");
		}

		RE::GRectF frameRect = movie->GetVisibleFrameRect();
		float stageW = std::fabs(frameRect.right - frameRect.left);
		float stageH = std::fabs(frameRect.bottom - frameRect.top);
		if (stageW <= 0.0f || stageH <= 0.0f) {
			if (auto* def = movie->GetMovieDef()) {
				stageW = def->GetWidth();
				stageH = def->GetHeight();
				frameRect.left = 0.0f;
				frameRect.top = 0.0f;
				frameRect.right = stageW;
				frameRect.bottom = stageH;
			}
		}
		if (stageW <= 0.0f || stageH <= 0.0f) {
			return fail("invalid_stage_size");
		}

		const float stagePerPixelX = stageW / displaySize.x;
		const float stagePerPixelY = stageH / displaySize.y;
		if (stagePerPixelX <= 0.0f || stagePerPixelY <= 0.0f) {
			return fail("invalid_stage_scale");
		}

		const std::uint32_t captureSizePx = (std::clamp)(request.requestedSizePx, 16u, 1024u);
		std::uint32_t captureX = 0;
		std::uint32_t captureY = 0;
		const std::uint32_t displayW = static_cast<std::uint32_t>(displaySize.x);
		const std::uint32_t displayH = static_cast<std::uint32_t>(displaySize.y);
		if (captureSizePx >= displayW || captureSizePx >= displayH) {
			return fail("capture_too_large_for_display");
		}
		const std::uint32_t maxCaptureX = displayW - captureSizePx;
		const std::uint32_t maxCaptureY = displayH - captureSizePx;
		// Avoid the top-left HUD region where the temporary capture rectangle is noticeable.
		captureX = maxCaptureX > kCaptureMarginPx ? (maxCaptureX - kCaptureMarginPx) : 0u;
		captureY = (std::min)(kCaptureMarginPx, maxCaptureY);

		job.key = key;
		job.spec = request.spec;
		job.requestedSizePx = captureSizePx;
		job.captureX = captureX;
		job.captureY = captureY;
		job.captureW = captureSizePx;
		job.captureH = captureSizePx;
		job.stageX = frameRect.left + static_cast<float>(captureX) * stagePerPixelX;
		job.stageY = frameRect.top + static_cast<float>(captureY) * stagePerPixelY;
		job.stageSizeX = static_cast<float>(captureSizePx) * stagePerPixelX;
		job.stageSizeY = static_cast<float>(captureSizePx) * stagePerPixelY;
		job.movie.reset(movie);
		job.stage = JobStage::WaitLoad;
		job.stageFrames = 0;

		RE::GFxValue root;
		movie->GetVariable(&root, "_root");
		if (!root.IsObject()) {
			return fail("root_not_object");
		}

		RE::GFxValue depth;
		root.Invoke("getNextHighestDepth", &depth);
		const double depthValue = depth.IsNumber() ? depth.GetNumber() : 10000.0;

		std::array<RE::GFxValue, 2> createArgs{
			RE::GFxValue(fmt::format("__wheeler_i4_capture_{}", ++_jobCounter)),
			RE::GFxValue(depthValue)
		};
		root.Invoke("createEmptyMovieClip", &job.containerClip, createArgs);
		if (!job.containerClip.IsDisplayObject()) {
			return fail("container_create_failed");
		}

		job.containerClip.SetMember("_x", static_cast<double>(job.stageX));
		job.containerClip.SetMember("_y", static_cast<double>(job.stageY));
		job.containerClip.SetMember("_visible", true);

		if (!DrawCaptureBackground(job.containerClip, job.stageSizeX, job.stageSizeY)) {
			return fail("capture_background_failed");
		}

		std::array<RE::GFxValue, 2> iconCreateArgs{
			RE::GFxValue("icon"),
			RE::GFxValue(1.0)
		};
		job.containerClip.Invoke("createEmptyMovieClip", &job.iconClip, iconCreateArgs);
		if (!job.iconClip.IsDisplayObject()) {
			return fail("icon_clip_create_failed");
		}

		movie->CreateObject(&job.iconLoader, "MovieClipLoader");
		if (!job.iconLoader.IsObject()) {
			return fail("movie_clip_loader_create_failed");
		}

		std::array<RE::GFxValue, 2> loadArgs{
			RE::GFxValue(job.spec.iconSource),
			job.iconClip
		};
		job.iconLoader.Invoke("loadClip", loadArgs);
		if (trace) {
			I4_LOG_INFO("[I4][TRACE][renderer.job.init] source='{}' label='{}' size={} decision=ok capturePx=({},{} {}x{}) stage=({:.2f},{:.2f} {:.2f}x{:.2f}) display=({:.2f}x{:.2f})",
				key.source,
				key.label,
				key.size,
				job.captureX,
				job.captureY,
				job.captureW,
				job.captureH,
				job.stageX,
				job.stageY,
				job.stageSizeX,
				job.stageSizeY,
				displaySize.x,
				displaySize.y);
		}
		return true;
	}

	bool I4SwfIconRenderer::IsIconClipLoaded(const PendingJob& job) const
	{
		if (!job.iconClip.IsDisplayObject()) {
			return false;
		}

		RE::GFxValue totalFrames;
		RE::GFxValue loadedFrames;
		job.iconClip.GetMember("_totalframes", &totalFrames);
		job.iconClip.GetMember("_framesloaded", &loadedFrames);
		if (totalFrames.IsNumber()) {
			const double total = totalFrames.GetNumber();
			if (total > 0.0) {
				if (!loadedFrames.IsNumber()) {
					return true;
				}
				return loadedFrames.GetNumber() >= total;
			}
		}
		return false;
	}

	void I4SwfIconRenderer::PrepareLoadedIcon(PendingJob& job) const
	{
		const bool trace = TraceEnabled();
		if (!job.iconClip.IsDisplayObject()) {
			if (trace) {
				I4_LOG_INFO("[I4][TRACE][renderer.job.prepare] source='{}' label='{}' size={} decision=skip reason=icon_clip_not_display_object",
					job.key.source,
					job.key.label,
					job.key.size);
			}
			return;
		}

		RE::GFxValue iconLabel(job.spec.iconLabel);
		job.iconClip.Invoke("gotoAndStop", nullptr, &iconLabel, 1);
		job.iconClip.SetMember("_visible", true);
		job.iconClip.SetMember("_alpha", 100.0);
		job.iconClip.SetMember("_xscale", 100.0);
		job.iconClip.SetMember("_yscale", 100.0);
		job.iconClip.SetMember("_x", 0.0);
		job.iconClip.SetMember("_y", 0.0);

		float baseWidth = std::fabs(GetNumberMember(job.iconClip, "_width"));
		float baseHeight = std::fabs(GetNumberMember(job.iconClip, "_height"));
		if (baseWidth < 1.0f || baseHeight < 1.0f) {
			baseWidth = 128.0f;
			baseHeight = 128.0f;
		}

		const float targetWidth = (std::max)(8.0f, job.stageSizeX * 0.92f);
		const float targetHeight = (std::max)(8.0f, job.stageSizeY * 0.92f);
		const float fitScale = (std::min)(targetWidth / baseWidth, targetHeight / baseHeight);
		if (std::isfinite(fitScale) && fitScale > 0.01f) {
			const double scalePct = static_cast<double>(fitScale * 100.0f);
			job.iconClip.SetMember("_xscale", scalePct);
			job.iconClip.SetMember("_yscale", scalePct);
		}

		const float scaledWidth = std::fabs(GetNumberMember(job.iconClip, "_width"));
		const float scaledHeight = std::fabs(GetNumberMember(job.iconClip, "_height"));
		const float finalWidth = scaledWidth > 1.0f ? scaledWidth : baseWidth;
		const float finalHeight = scaledHeight > 1.0f ? scaledHeight : baseHeight;
		job.iconClip.SetMember("_x", static_cast<double>((job.stageSizeX - finalWidth) * 0.5f));
		job.iconClip.SetMember("_y", static_cast<double>((job.stageSizeY - finalHeight) * 0.5f));

		const bool applyColorTransform = true;
		ApplyColorTransform(job.iconClip, ColorToRGB(job.spec.color));
		if (trace) {
			I4_LOG_INFO("[I4][TRACE][renderer.job.prepare] source='{}' label='{}' size={} decision=ok base={}x{} target={}x{} fitScale={:.3f} final={}x{} colorRGB=0x{:06X} colorTransformApplied={}",
				job.key.source,
				job.key.label,
				job.key.size,
				baseWidth,
				baseHeight,
				targetWidth,
				targetHeight,
				fitScale,
				finalWidth,
				finalHeight,
				ColorToRGB(job.spec.color),
				applyColorTransform);
		}
	}

	Texture::Image I4SwfIconRenderer::CaptureJobTexture(const PendingJob& job) const
	{
		Texture::Image image{};
		const bool trace = TraceEnabled();
		const auto fail = [&](std::string_view reason) {
			if (trace) {
				I4_LOG_INFO("[I4][TRACE][renderer.capture.out] source='{}' label='{}' size={} decision=fail reason={}",
					job.key.source,
					job.key.label,
					job.key.size,
					reason);
			}
			return Texture::Image{};
		};
		if (trace) {
			I4_LOG_INFO("[I4][TRACE][renderer.capture.req] source='{}' label='{}' size={} capture=({},{} {}x{})",
				job.key.source,
				job.key.label,
				job.key.size,
				job.captureX,
				job.captureY,
				job.captureW,
				job.captureH);
		}

		auto* rendererData = RE::BSGraphics::Renderer::GetRendererDataSingleton();
		auto* context = rendererData ? reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) : nullptr;
		auto* device = Texture::device_;
		if (!context || !device) {
			return fail("missing_device_or_context");
		}

		ID3D11RenderTargetView* renderTarget = nullptr;
		context->OMGetRenderTargets(1, &renderTarget, nullptr);
		if (!renderTarget) {
			return fail("no_render_target");
		}

		ID3D11Resource* targetResource = nullptr;
		renderTarget->GetResource(&targetResource);
		renderTarget->Release();
		if (!targetResource) {
			return fail("no_render_target_resource");
		}

		ID3D11Texture2D* targetTexture = nullptr;
		targetResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&targetTexture));
		targetResource->Release();
		if (!targetTexture) {
			return fail("render_target_not_texture2d");
		}

		D3D11_TEXTURE2D_DESC targetDesc{};
		targetTexture->GetDesc(&targetDesc);
		if (targetDesc.SampleDesc.Count > 1) {
			targetTexture->Release();
			return fail("multisampled_target_unsupported");
		}

		if (job.captureX >= targetDesc.Width || job.captureY >= targetDesc.Height) {
			targetTexture->Release();
			return fail("capture_origin_out_of_bounds");
		}

		const std::uint32_t captureW = (std::min)(job.captureW, targetDesc.Width - job.captureX);
		const std::uint32_t captureH = (std::min)(job.captureH, targetDesc.Height - job.captureY);
		if (captureW == 0 || captureH == 0) {
			targetTexture->Release();
			return fail("capture_size_zero");
		}

		D3D11_TEXTURE2D_DESC stagingDesc{};
		stagingDesc.Width = captureW;
		stagingDesc.Height = captureH;
		stagingDesc.MipLevels = 1;
		stagingDesc.ArraySize = 1;
		stagingDesc.Format = targetDesc.Format;
		stagingDesc.SampleDesc.Count = 1;
		stagingDesc.SampleDesc.Quality = 0;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.MiscFlags = 0;

		ID3D11Texture2D* stagingTexture = nullptr;
		if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture)) || !stagingTexture) {
			targetTexture->Release();
			return fail("staging_texture_create_failed");
		}

		D3D11_BOX region{
			job.captureX,
			job.captureY,
			0,
			job.captureX + captureW,
			job.captureY + captureH,
			1
		};
		context->CopySubresourceRegion(stagingTexture, 0, 0, 0, 0, targetTexture, 0, &region);
		targetTexture->Release();

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped))) {
			stagingTexture->Release();
			return fail("map_staging_failed");
		}

		const bool isBGRA = targetDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
		                    targetDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

		std::vector<std::uint8_t> rgbaPixels(captureW * captureH * 4, 0);
		std::uint32_t chromaRemovedCount = 0;
		std::uint32_t opaqueCount = 0;
		std::uint64_t sumR = 0;
		std::uint64_t sumG = 0;
		std::uint64_t sumB = 0;
		std::uint32_t minOpaqueX = captureW;
		std::uint32_t minOpaqueY = captureH;
		std::uint32_t maxOpaqueX = 0;
		std::uint32_t maxOpaqueY = 0;
		for (std::uint32_t y = 0; y < captureH; ++y) {
			const auto* srcRow = static_cast<const std::uint8_t*>(mapped.pData) + (y * mapped.RowPitch);
			auto* dstRow = rgbaPixels.data() + (y * captureW * 4);
			for (std::uint32_t x = 0; x < captureW; ++x) {
				const auto* src = srcRow + x * 4;
				const std::uint8_t r = isBGRA ? src[2] : src[0];
				const std::uint8_t g = src[1];
				const std::uint8_t b = isBGRA ? src[0] : src[2];

				const std::uint32_t diff =
					static_cast<std::uint32_t>(std::abs(static_cast<int>(r) - static_cast<int>(kChromaR))) +
					static_cast<std::uint32_t>(std::abs(static_cast<int>(g) - static_cast<int>(kChromaG))) +
					static_cast<std::uint32_t>(std::abs(static_cast<int>(b) - static_cast<int>(kChromaB)));

				std::uint8_t a = 0xFF;
				if (diff <= kChromaTolLow) {
					a = 0;
					++chromaRemovedCount;
				} else if (diff < kChromaTolHigh) {
					const std::uint32_t scaled = (diff - kChromaTolLow) * 255u / (kChromaTolHigh - kChromaTolLow);
					a = static_cast<std::uint8_t>(scaled);
					if (a < 4) {
						++chromaRemovedCount;
					}
				}
				std::uint8_t outR = r;
				std::uint8_t outG = g;
				std::uint8_t outB = b;
				if (a > 0 && a < 255) {
					// De-spill chroma edges: recover original color from key-composited edge pixels.
					const float af = static_cast<float>(a) / 255.0f;
					const float inv = 1.0f - af;
					const auto unspill = [&](std::uint8_t src, std::uint8_t key) -> std::uint8_t {
						const float recovered = (static_cast<float>(src) - static_cast<float>(key) * inv) / af;
						return static_cast<std::uint8_t>(std::clamp(recovered, 0.0f, 255.0f));
					};
					outR = unspill(r, kChromaR);
					outG = unspill(g, kChromaG);
					outB = unspill(b, kChromaB);
				} else if (a == 0) {
					outR = 0;
					outG = 0;
					outB = 0;
				}
				if (a > 8) {
					++opaqueCount;
					sumR += outR;
					sumG += outG;
					sumB += outB;
					minOpaqueX = (std::min)(minOpaqueX, x);
					minOpaqueY = (std::min)(minOpaqueY, y);
					maxOpaqueX = (std::max)(maxOpaqueX, x);
					maxOpaqueY = (std::max)(maxOpaqueY, y);
				}

				auto* dst = dstRow + x * 4;
				dst[0] = outR;
				dst[1] = outG;
				dst[2] = outB;
				dst[3] = a;
			}
		}

		context->Unmap(stagingTexture, 0);
		stagingTexture->Release();
		LogTrackedRenderedAverage(
			"capture",
			job.key.source,
			job.key.label,
			job.key.size,
			ColorToRGB(job.spec.color),
			true,
			opaqueCount,
			sumR,
			sumG,
			sumB);

		const std::uint32_t totalPixels = captureW * captureH;
		const std::uint32_t minOpaquePixels = (std::max)(8u, totalPixels / 1000u);   // >=0.1%
		if (opaqueCount < minOpaquePixels) {
			return fail("opaque_pixels_too_low");
		}
		if (maxOpaqueX < minOpaqueX || maxOpaqueY < minOpaqueY) {
			return fail("opaque_bbox_invalid");
		}
		const std::uint32_t bboxW = maxOpaqueX >= minOpaqueX ? (maxOpaqueX - minOpaqueX + 1u) : 0u;
		const std::uint32_t bboxH = maxOpaqueY >= minOpaqueY ? (maxOpaqueY - minOpaqueY + 1u) : 0u;
		const std::uint32_t bboxArea = bboxW * bboxH;
		const std::uint32_t minBboxArea = (std::max)(16u, totalPixels / 400u);       // >=0.25%
		if (bboxW < 6u || bboxH < 6u || bboxArea < minBboxArea) {
			return fail("opaque_bbox_too_small");
		}
		const std::uint32_t bboxCoveragePct = totalPixels > 0 ? (bboxArea * 100u) / totalPixels : 100u;
		const std::uint32_t minChromaPixels = (std::max)(4u, totalPixels / 1000u);   // >=0.1%
		// A near-full-frame opaque capture with almost no chroma key pixels is likely a scene/frame grab.
		if (bboxCoveragePct > 95u && chromaRemovedCount < minChromaPixels) {
			return fail("scene_grab_detected");
		}

		constexpr std::uint32_t kCropPad = 1;
		std::uint32_t cropMinX = minOpaqueX > kCropPad ? (minOpaqueX - kCropPad) : 0u;
		std::uint32_t cropMinY = minOpaqueY > kCropPad ? (minOpaqueY - kCropPad) : 0u;
		std::uint32_t cropMaxX = (std::min)(captureW - 1u, maxOpaqueX + kCropPad);
		std::uint32_t cropMaxY = (std::min)(captureH - 1u, maxOpaqueY + kCropPad);

		const std::uint32_t desiredMinW = (std::max)(16u, static_cast<std::uint32_t>(std::lround(static_cast<double>(captureW) * kCropMinDimensionRatio)));
		const std::uint32_t desiredMinH = (std::max)(16u, static_cast<std::uint32_t>(std::lround(static_cast<double>(captureH) * kCropMinDimensionRatio)));
		ExpandRangeToMinSize(cropMinX, cropMaxX, captureW, desiredMinW);
		ExpandRangeToMinSize(cropMinY, cropMaxY, captureH, desiredMinH);

		const std::uint32_t outW = cropMaxX >= cropMinX ? (cropMaxX - cropMinX + 1u) : 0u;
		const std::uint32_t outH = cropMaxY >= cropMinY ? (cropMaxY - cropMinY + 1u) : 0u;
		if (outW == 0u || outH == 0u) {
			return fail("cropped_size_zero");
		}

		std::vector<std::uint8_t> croppedPixels(outW * outH * 4, 0);
		for (std::uint32_t y = 0; y < outH; ++y) {
			const auto* src = rgbaPixels.data() + ((cropMinY + y) * captureW + cropMinX) * 4u;
			auto* dst = croppedPixels.data() + (y * outW) * 4u;
			std::memcpy(dst, src, outW * 4u);
		}

		D3D11_TEXTURE2D_DESC outDesc{};
		outDesc.Width = outW;
		outDesc.Height = outH;
		outDesc.MipLevels = 1;
		outDesc.ArraySize = 1;
		outDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		outDesc.SampleDesc.Count = 1;
		outDesc.Usage = D3D11_USAGE_DEFAULT;
		outDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA outData{};
		outData.pSysMem = croppedPixels.data();
		outData.SysMemPitch = outW * 4;

		ID3D11Texture2D* outTexture = nullptr;
		if (FAILED(device->CreateTexture2D(&outDesc, &outData, &outTexture)) || !outTexture) {
			return fail("output_texture_create_failed");
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		ID3D11ShaderResourceView* outSRV = nullptr;
		if (FAILED(device->CreateShaderResourceView(outTexture, &srvDesc, &outSRV)) || !outSRV) {
			outTexture->Release();
			return fail("output_srv_create_failed");
		}
		outTexture->Release();

		image.texture = outSRV;
		image.width = static_cast<int>(outW);
		image.height = static_cast<int>(outH);
		if (trace) {
			I4_LOG_INFO("[I4][TRACE][renderer.capture.out] source='{}' label='{}' size={} decision=ok raw={}x{} out={}x{} opaque={} chromaRemoved={} bbox={}x{} coverage={}%",
				job.key.source,
				job.key.label,
				job.key.size,
				captureW,
				captureH,
				outW,
				outH,
				opaqueCount,
				chromaRemovedCount,
				bboxW,
				bboxH,
				bboxCoveragePct);
		}
		return image;
	}

	void I4SwfIconRenderer::FinishActiveJob(const Texture::Image& image, bool owned, bool failed, std::string_view reason)
	{
		const bool trace = TraceEnabled();
		std::optional<PendingJob> finished;
		{
			std::scoped_lock lock(_lock);
			if (_activeJob.has_value()) {
				finished.emplace(std::move(_activeJob.value()));
				_activeJob.reset();
			}
		}

		if (!finished.has_value()) {
			return;
		}

		if (trace) {
			I4_LOG_INFO("[I4][TRACE][renderer.job.finish] source='{}' label='{}' size={} failed={} reason='{}' image={}x{} owned={}",
				finished->key.source,
				finished->key.label,
				finished->key.size,
				failed,
				reason,
				image.width,
				image.height,
				owned);
		}

		CleanupJobClip(finished.value());

		if (image.texture) {
			CacheResult(finished->key, image, owned, true);
		} else {
			CacheResult(finished->key, {}, false, false);
		}

		if (failed) {
			GetStats().renderFailures.fetch_add(1, std::memory_order_relaxed);
			const auto warnKey = fmt::format("{}|{}|{}|{}|{}",
				finished->key.source,
				finished->key.label,
				finished->key.color,
				finished->key.size,
				reason);
			std::scoped_lock lock(_lock);
			if (_warned.insert(warnKey).second) {
				I4_LOG_WARN("[I4] ExtractionMode failed for label='{}' source='{}' reason='{}'. Falling back.",
					finished->key.label,
					finished->key.source,
					reason);
			}
		}

		StartNextPendingJob();
	}

	void I4SwfIconRenderer::CleanupJobClip(PendingJob& job) const
	{
		if (job.containerClip.IsDisplayObject()) {
			job.containerClip.Invoke("removeMovieClip");
		}
		job.iconClip = RE::GFxValue{};
		job.iconLoader = RE::GFxValue{};
		job.containerClip = RE::GFxValue{};
		job.movie = nullptr;
	}

	void I4SwfIconRenderer::CacheResult(const RenderKey& key, const Texture::Image& image, bool owned, bool precolored)
	{
		const std::uint32_t maxEntries = (std::clamp)(Config::I4::CacheMaxEntries, 16u, 2048u);
		bool shouldLogCapture = false;
		bool traceCacheStore = false;
		{
			bool inserted = false;
			std::scoped_lock lock(_lock);
			auto it = _cache.find(key);
			if (it != _cache.end()) {
				if (it->second.owned && it->second.image.texture && it->second.image.texture != image.texture) {
					it->second.image.texture->Release();
					it->second.image.texture = nullptr;
				}
				it->second.image = image;
				it->second.owned = owned;
				it->second.precolored = precolored;
				it->second.tick = ++_tickCounter;
			} else {
				_cache.emplace(key, CacheEntry{ image, owned, precolored, ++_tickCounter });
				inserted = true;
			}
			EvictCacheIfNeeded(maxEntries);
			shouldLogCapture = inserted && owned && image.texture && Config::I4::DebugLog;
			traceCacheStore = TraceEnabled() && inserted;
		}
		if (shouldLogCapture) {
			I4_LOG_INFO("[I4] capture source='{}' label='{}' req={} out={}x{}",
				key.source,
				key.label,
				key.size,
				image.width,
				image.height);
		}
		if (traceCacheStore) {
			I4_LOG_INFO("[I4][TRACE][renderer.cache] source='{}' label='{}' size={} action=store texture={} owned={} precolored={} image={}x{}",
				key.source,
				key.label,
				key.size,
				image.texture != nullptr,
				owned,
				precolored,
				image.width,
				image.height);
		}
	}

	void I4SwfIconRenderer::EvictCacheIfNeeded(std::uint32_t maxEntries)
	{
		while (_cache.size() > maxEntries) {
			auto lru = _cache.end();
			for (auto it = _cache.begin(); it != _cache.end(); ++it) {
				if (lru == _cache.end() || it->second.tick < lru->second.tick) {
					lru = it;
				}
			}
			if (lru == _cache.end()) {
				break;
			}
			if (TraceEnabled()) {
				I4_LOG_INFO("[I4][TRACE][renderer.cache] source='{}' label='{}' size={} action=evict owned={} texture={} cacheSizeBefore={} max={}",
					lru->first.source,
					lru->first.label,
					lru->first.size,
					lru->second.owned,
					lru->second.image.texture != nullptr,
					_cache.size(),
					maxEntries);
			}
			if (lru->second.owned && lru->second.image.texture) {
				lru->second.image.texture->Release();
				lru->second.image.texture = nullptr;
			}
			_cache.erase(lru);
		}
	}

	std::uint32_t I4SwfIconRenderer::ColorToRGB(ImU32 color)
	{
		const ImVec4 col = ImGui::ColorConvertU32ToFloat4(color);
		const auto r = static_cast<std::uint32_t>(std::clamp(col.x, 0.0f, 1.0f) * 255.0f);
		const auto g = static_cast<std::uint32_t>(std::clamp(col.y, 0.0f, 1.0f) * 255.0f);
		const auto b = static_cast<std::uint32_t>(std::clamp(col.z, 0.0f, 1.0f) * 255.0f);
		return (r << 16) | (g << 8) | b;
	}

	RE::GRenderer::Cxform I4SwfIconRenderer::BuildColorTransform(std::uint32_t rgb)
	{
		RE::GRenderer::Cxform cx{};
		std::memset(&cx, 0, sizeof(cx));
		cx.matrix[0][0] = 0.0f;
		cx.matrix[1][0] = 0.0f;
		cx.matrix[2][0] = 0.0f;
		cx.matrix[3][0] = 1.0f;
		cx.matrix[0][1] = static_cast<float>((rgb >> 16) & 0xFF);
		cx.matrix[1][1] = static_cast<float>((rgb >> 8) & 0xFF);
		cx.matrix[2][1] = static_cast<float>(rgb & 0xFF);
		cx.matrix[3][1] = 0.0f;
		return cx;
	}

	void I4SwfIconRenderer::ApplyColorTransform(const RE::GFxValue& icon, std::uint32_t rgb)
	{
		const auto cx = BuildColorTransform(rgb);
		if (icon.IsDisplayObject()) {
			const_cast<RE::GFxValue&>(icon).SetCxform(cx);
		}

		icon.VisitMembers([&](const char*, const RE::GFxValue& child) {
			if (child.IsDisplayObject()) {
				const_cast<RE::GFxValue&>(child).SetCxform(cx);
			}
			return true;
		});
	}

	void I4SwfIconRenderer::ClearCache()
	{
		std::optional<PendingJob> activeJob;
		std::size_t clearedEntries = 0;
		std::size_t clearedPending = 0;
		{
			std::scoped_lock lock(_lock);
			clearedEntries = _cache.size();
			clearedPending = _pendingRequests.size();
			for (auto& [key, entry] : _cache) {
				if (entry.owned && entry.image.texture) {
					entry.image.texture->Release();
					entry.image.texture = nullptr;
				}
			}
			_cache.clear();
			_pendingRequests.clear();
			if (_activeJob.has_value()) {
				activeJob.emplace(std::move(_activeJob.value()));
				_activeJob.reset();
			}
			_warned.clear();
			_tickCounter = 0;
			_lastProcessedImGuiFrame = -1;
		}
		if (TraceEnabled()) {
			I4_LOG_INFO("[I4][TRACE][renderer.reset] clearedEntries={} clearedPending={} hadActive={}",
				clearedEntries,
				clearedPending,
				activeJob.has_value());
		}

		if (activeJob.has_value()) {
			CleanupJobClip(activeJob.value());
		}
	}
}
