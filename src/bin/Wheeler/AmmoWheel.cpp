#include "AmmoWheel.h"
#include "AmmoWheelReskin.h"
#include "AmmoWheelReskinUnified.h"
#include "bin/Config.h"
#include "bin/InputBroker.h"
#include "bin/Rendering/Drawer.h"
#include "bin/Rendering/ResolutionScaleContext.h"
#include "bin/Rendering/TextureManager.h"
#include "bin/UserInput/Controls.h"
#include "bin/Utilities/Utils.h"
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <set>
#include <chrono>
#include <SimpleIni.h>
#include "include/lib/nanosvg.h"
#include "include/lib/nanosvgrast.h"

// ========== AMMOWHEEL CONFIG AUDIT (Phase 2) ==========
// A) CountFontSize: FIXED - CountFontPx used in drawAmmoCount(), loaded from [Text] section
// B) IconRadiusRatio/IconSizePx/IconRadialOffset: FIXED - OnConfigChanged() now uses all three
// C) TextRadialOffsetPx range: FIXED - Extended to -200..+400 in OnConfigChanged() and dMenu JSON
// D) PopupCountFontPx: FIXED - Used in drawHoverPopup(), loaded from [Popup] section
// E) PopupPaddingPx: FIXED - Now affects content layout in drawHoverPopup()
// F) AnimationSpeed: FIXED - PopupAnimationSpeed controls popup open/close animation
// G) CustomOpacity: FIXED - Applied to alphaMult in Update() draw setup
// H) BackgroundOpacity: VERIFIED - Used in draw() for arc background alpha
// I) BorderInnerScale/BorderOuterScale: VERIFIED - Used in draw() for border ring
// J) SlotShadow: VERIFIED - Used in drawSlot() with stable params
// K) SlotHighlight: VERIFIED - Enable/Disable, Thickness, Alpha all wired in drawSlot()
// L) HoverPulse: FIXED - Implemented pulsing glow ring in drawSlot()
// M) SelectedIndicatorThickness: VERIFIED - Used via Skin::SelectedThicknessPx in drawSlot()
// N) CenterPanel LayoutMode/MaxLines/MinFontSize/MaxTextWidthRatio: VERIFIED - Used in drawCenterPanel()
// O) ShowDescription/MaxDescriptionLines: VERIFIED - Wired in drawCenterPanel()

// ========== PHASE 5: DEAD/UNCLEAR FEATURES AUDIT ==========
// - Icon Rotation: IMPLEMENTED - IconRotationMode (0=FollowSlot, 1=Upright, 2=Fixed) works via DrawRotatedTexture
// - Active Indicator: NOT IMPLEMENTED - Config exists but no draw code. Intended for activation feedback.
//   Decision: Keep in UI with tooltip explaining it's for future RTU activation visual feedback.
// - Charge Indicator: NOT IMPLEMENTED - Config exists but no draw code. Intended for hover-delay charge.
//   Decision: Keep in UI with tooltip explaining it's for future RTU charge progress visual.
// - Hovered Indicator Style: IMPLEMENTED - Uses preset system with configurable arc/glow styles.

static const char* AMMO_WHEEL_POPUP_ID = "##AmmoWheel";
static const char* AMMO_WHEEL_INI_PATH = "Data\\SKSE\\Plugins\\wheeler\\AmmoWheel.ini";
static const char* AMMO_KID_INI_PATH = "Data\\SKSE\\Plugins\\wheeler\\AMMO_KID.ini";

namespace
{
	const char* GetResolutionFixModeName(Config::ResolutionFix::Mode mode)
	{
		switch (mode) {
		case Config::ResolutionFix::Mode::ForceDisplayToGame:
			return "ForceDisplayToGame";
		case Config::ResolutionFix::Mode::ForceNone:
			return "ForceNone";
		case Config::ResolutionFix::Mode::Auto:
		default:
			return "Auto";
		}
	}
}

// ========== SAFE FILESYSTEM HELPERS (GUARD 1 & 2) ==========
namespace {
	// Check if path looks like a network/UNC path
	bool IsLikelyNetworkPath(const std::string& path) {
		return path.size() >= 2 && path[0] == '\\' && path[1] == '\\';
	}

	// Safe file existence check with timeout warning and exception handling
	bool SafeFileExists(const std::string& path) {
		try {
			if (path.empty()) return false;

			// Guard against UNC/network paths (can hang)
			if (IsLikelyNetworkPath(path)) {
				logger::warn("AmmoWheel: Skipping network path: {}", path);
				return false;
			}

			auto start = std::chrono::steady_clock::now();
			std::error_code ec;
			bool exists = std::filesystem::exists(path, ec);
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - start
			);

			if (elapsed.count() > 100) {
				logger::warn("AmmoWheel: File check slow ({} ms): {}", elapsed.count(), path);
			}
			if (ec) {
				// Only log first few errors to avoid spam
				static int errorCount = 0;
				if (errorCount++ < 5) {
					logger::error("AmmoWheel: File check error for '{}': {}", path, ec.message());
				}
				return false;
			}
			return exists;

		} catch (const std::filesystem::filesystem_error& e) {
			logger::error("AmmoWheel: Filesystem exception for '{}': {}", path, e.what());
			return false;
		} catch (const std::exception& e) {
			logger::error("AmmoWheel: Exception checking file '{}': {}", path, e.what());
			return false;
		} catch (...) {
			logger::error("AmmoWheel: Unknown exception checking file '{}'", path);
			return false;
		}
	}

	struct SvgSizeInfo
	{
		float width = 0.0f;
		float height = 0.0f;
		float viewBoxW = 0.0f;
		float viewBoxH = 0.0f;
		bool usedViewBox = false;
		bool usedFallback = false;
	};

	bool TryGetSvgAttribute(const std::string& header, const char* attr, std::string& outValue)
	{
		const size_t attrLen = std::strlen(attr);
		size_t pos = 0;
		while ((pos = header.find(attr, pos)) != std::string::npos) {
			if (pos > 0) {
				char prev = header[pos - 1];
				if (!std::isspace(static_cast<unsigned char>(prev)) && prev != '<') {
					pos += attrLen;
					continue;
				}
			}
			size_t eq = pos + attrLen;
			if (eq >= header.size() || header[eq] != '=') {
				pos += attrLen;
				continue;
			}
			size_t quotePos = eq + 1;
			while (quotePos < header.size() && std::isspace(static_cast<unsigned char>(header[quotePos]))) {
				++quotePos;
			}
			if (quotePos >= header.size()) {
				return false;
			}
			char quote = header[quotePos];
			if (quote != '"' && quote != '\'') {
				pos += attrLen;
				continue;
			}
			size_t end = header.find(quote, quotePos + 1);
			if (end == std::string::npos) {
				return false;
			}
			outValue = header.substr(quotePos + 1, end - quotePos - 1);
			return true;
		}
		return false;
	}

	bool ParseLength(const std::string& value, float& outPx, bool& outPercent)
	{
		outPercent = false;
		if (value.empty()) {
			return false;
		}
		size_t start = value.find_first_not_of(" \t\r\n");
		if (start == std::string::npos) {
			return false;
		}
		size_t end = value.find_last_not_of(" \t\r\n");
		std::string trimmed = value.substr(start, end - start + 1);
		if (!trimmed.empty() && trimmed.back() == '%') {
			outPercent = true;
			trimmed.pop_back();
		}
		char* endPtr = nullptr;
		outPx = std::strtof(trimmed.c_str(), &endPtr);
		if (endPtr == trimmed.c_str()) {
			return false;
		}
		return outPx > 0.0f;
	}

	bool ParseViewBox(const std::string& value, float& outW, float& outH)
	{
		const char* s = value.c_str();
		char* endPtr = nullptr;
		float vals[4] = {};
		for (int i = 0; i < 4; ++i) {
			vals[i] = std::strtof(s, &endPtr);
			if (endPtr == s) {
				return false;
			}
			s = endPtr;
		}
		outW = vals[2];
		outH = vals[3];
		return outW > 0.0f && outH > 0.0f;
	}

	void ReplaceOrInsertAttr(std::string& header, const char* attr, const std::string& value)
	{
		const size_t attrLen = std::strlen(attr);
		size_t pos = header.find(attr);
		while (pos != std::string::npos) {
			if (pos > 0) {
				char prev = header[pos - 1];
				if (!std::isspace(static_cast<unsigned char>(prev)) && prev != '<') {
					pos = header.find(attr, pos + attrLen);
					continue;
				}
			}
			size_t eq = pos + attrLen;
			if (eq >= header.size() || header[eq] != '=') {
				pos = header.find(attr, pos + attrLen);
				continue;
			}
			size_t quotePos = eq + 1;
			while (quotePos < header.size() && std::isspace(static_cast<unsigned char>(header[quotePos]))) {
				++quotePos;
			}
			if (quotePos >= header.size()) {
				break;
			}
			char quote = header[quotePos];
			if (quote != '"' && quote != '\'') {
				pos = header.find(attr, pos + attrLen);
				continue;
			}
			size_t end = header.find(quote, quotePos + 1);
			if (end == std::string::npos) {
				break;
			}
			std::string replacement = std::string(attr) + "=\"" + value + "\"";
			header.replace(pos, end - pos + 1, replacement);
			return;
		}
		size_t insertPos = header.find("<svg");
		if (insertPos != std::string::npos) {
			insertPos += 4;
			header.insert(insertPos, " " + std::string(attr) + "=\"" + value + "\"");
		}
	}

	bool NormalizeSvgText(std::string& svgText, SvgSizeInfo& info)
	{
		size_t svgPos = svgText.find("<svg");
		if (svgPos == std::string::npos) {
			return false;
		}
		size_t tagEnd = svgText.find('>', svgPos);
		if (tagEnd == std::string::npos) {
			return false;
		}
		std::string header = svgText.substr(svgPos, tagEnd - svgPos + 1);
		std::string rest = svgText.substr(tagEnd + 1);

		std::string widthAttr;
		std::string heightAttr;
		std::string viewBoxAttr;
		const bool hasWidthAttr = TryGetSvgAttribute(header, "width", widthAttr);
		const bool hasHeightAttr = TryGetSvgAttribute(header, "height", heightAttr);
		const bool hasViewBox = TryGetSvgAttribute(header, "viewBox", viewBoxAttr);

		float width = 0.0f;
		float height = 0.0f;
		bool widthPercent = false;
		bool heightPercent = false;
		const bool widthOk = hasWidthAttr && ParseLength(widthAttr, width, widthPercent);
		const bool heightOk = hasHeightAttr && ParseLength(heightAttr, height, heightPercent);

		float viewBoxW = 0.0f;
		float viewBoxH = 0.0f;
		const bool viewBoxOk = hasViewBox && ParseViewBox(viewBoxAttr, viewBoxW, viewBoxH);

		info.viewBoxW = viewBoxW;
		info.viewBoxH = viewBoxH;

		const bool needsFix = !widthOk || !heightOk || widthPercent || heightPercent;
		if (needsFix) {
			if (viewBoxOk) {
				width = viewBoxW;
				height = viewBoxH;
				info.usedViewBox = true;
			} else {
				width = 512.0f;
				height = 512.0f;
				info.usedFallback = true;
			}
		}

		info.width = width > 0.0f ? width : 512.0f;
		info.height = height > 0.0f ? height : 512.0f;
		ReplaceOrInsertAttr(header, "width", std::to_string(static_cast<int>(info.width + 0.5f)));
		ReplaceOrInsertAttr(header, "height", std::to_string(static_cast<int>(info.height + 0.5f)));

		svgText = svgText.substr(0, svgPos) + header + rest;
		return true;
	}

	NSVGimage* ParseSvgFromFileNormalized(const std::string& path, SvgSizeInfo& info)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file) {
			return nullptr;
		}
		std::string svgText((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (svgText.empty()) {
			return nullptr;
		}
		NormalizeSvgText(svgText, info);
		return nsvgParse(const_cast<char*>(svgText.c_str()), "px", 96.0f);
	}

	// Safe texture loading with size validation, timeout warning, and exception handling
	bool SafeLoadTexture(const std::string& path, ID3D11ShaderResourceView** srv, int& w, int& h) {
		try {
			if (!srv) return false;
			*srv = nullptr;
			w = 0;
			h = 0;

			if (!SafeFileExists(path)) return false;

			// Validate file size (SVG should not be too large - 5MB max)
			std::error_code ec;
			auto fileSize = std::filesystem::file_size(path, ec);
			if (ec) {
				logger::error("AmmoWheel: Could not query file size '{}': {}", path, ec.message());
				return false;
			}
			if (fileSize == 0 || fileSize > 5ULL * 1024ULL * 1024ULL) {
				logger::error("AmmoWheel: Invalid SVG size for '{}': {} bytes", path, static_cast<std::uintmax_t>(fileSize));
				return false;
			}

			auto start = std::chrono::steady_clock::now();
			
			// Use Texture's load function (nanosvg-based)
			// Note: We need to access the private load_texture_from_file - use a workaround
			// by loading via nanosvg directly or making a public wrapper
			// For now, we'll use the existing Texture system indirectly
			
			// Load SVG using nanosvg with deterministic sizing
			SvgSizeInfo svgInfo{};
			auto* svg = ParseSvgFromFileNormalized(path, svgInfo);
			if (!svg) {
				logger::debug("AmmoWheel: nsvgParse failed for '{}'", path);
				return false;
			}
			
			auto* rast = nsvgCreateRasterizer();
			if (!rast) {
				logger::error("AmmoWheel: nsvgCreateRasterizer failed for '{}'", path);
				nsvgDelete(svg);
				return false;
			}

			int image_width = static_cast<int>(svg->width);
			int image_height = static_cast<int>(svg->height);
			if (svgInfo.width <= 0.0f) {
				svgInfo.width = svg->width;
			}
			if (svgInfo.height <= 0.0f) {
				svgInfo.height = svg->height;
			}
			if (svgInfo.usedViewBox || svgInfo.usedFallback) {
				const char* source = svgInfo.usedFallback ? "fallback" : "viewBox";
				logger::info("AmmoWheel: SVG size '{}' viewBox({:.0f}x{:.0f}) -> {}x{} (source={})",
					path, svgInfo.viewBoxW, svgInfo.viewBoxH, image_width, image_height, source);
			}
			
			if (image_width <= 0 || image_height <= 0 || image_width > 4096 || image_height > 4096) {
				logger::error("AmmoWheel: Invalid SVG dimensions for '{}': {}x{}", path, image_width, image_height);
				nsvgDeleteRasterizer(rast);
				nsvgDelete(svg);
				return false;
			}

			auto* image_data = static_cast<unsigned char*>(malloc(image_width * image_height * 4));
			if (!image_data) {
				logger::error("AmmoWheel: Failed to allocate image buffer for '{}'", path);
				nsvgDeleteRasterizer(rast);
				nsvgDelete(svg);
				return false;
			}
			
			nsvgRasterize(rast, svg, 0, 0, 1, image_data, image_width, image_height, image_width * 4);
			nsvgDelete(svg);
			nsvgDeleteRasterizer(rast);

			// Create D3D11 texture
			if (!Texture::device_) {
				logger::error("AmmoWheel: D3D11 device not available for '{}'", path);
				free(image_data);
				return false;
			}

			D3D11_TEXTURE2D_DESC desc;
			ZeroMemory(&desc, sizeof(desc));
			desc.Width = image_width;
			desc.Height = image_height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = 0;
			desc.MiscFlags = 0;

			ID3D11Texture2D* p_texture = nullptr;
			D3D11_SUBRESOURCE_DATA sub_resource;
			sub_resource.pSysMem = image_data;
			sub_resource.SysMemPitch = desc.Width * 4;
			sub_resource.SysMemSlicePitch = 0;
			
			HRESULT hr = Texture::device_->CreateTexture2D(&desc, &sub_resource, &p_texture);
			if (FAILED(hr) || !p_texture) {
				logger::error("AmmoWheel: CreateTexture2D failed for '{}': 0x{:X}", path, static_cast<unsigned>(hr));
				free(image_data);
				return false;
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
			ZeroMemory(&srv_desc, sizeof(srv_desc));
			srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv_desc.Texture2D.MipLevels = desc.MipLevels;
			srv_desc.Texture2D.MostDetailedMip = 0;
			
			hr = Texture::device_->CreateShaderResourceView(p_texture, &srv_desc, srv);
			p_texture->Release();
			free(image_data);

			if (FAILED(hr) || !(*srv)) {
				logger::error("AmmoWheel: CreateShaderResourceView failed for '{}': 0x{:X}", path, static_cast<unsigned>(hr));
				return false;
			}

			w = image_width;
			h = image_height;

			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - start
			);
			if (elapsed.count() > 500) {
				logger::warn("AmmoWheel: Texture load slow ({} ms): {}", elapsed.count(), path);
			}

			return true;

		} catch (const std::exception& e) {
			logger::error("AmmoWheel: Exception loading texture '{}': {}", path, e.what());
			if (srv && *srv) { (*srv)->Release(); *srv = nullptr; }
			return false;
		} catch (...) {
			logger::error("AmmoWheel: Unknown exception loading texture '{}'", path);
			if (srv && *srv) { (*srv)->Release(); *srv = nullptr; }
			return false;
		}
	}

	// Normalize path separators to backslash for Windows consistency
	std::string NormalizePath(const std::string& path) {
		std::string result = path;
		for (char& c : result) {
			if (c == '/') c = '\\';
		}
		// Remove leading .\ if present (relative path marker)
		if (result.size() >= 2 && result[0] == '.' && result[1] == '\\') {
			result = result.substr(2);
		}
		return result;
	}

	// ========== ROBUST FALLBACK TEXTURE LOADER ==========
	// Tries a list of paths in order, returns first successful load
	// Logs all attempts for debugging
	struct TextureLoadResult {
		ID3D11ShaderResourceView* srv = nullptr;
		int width = 0;
		int height = 0;
		std::string loadedPath;
		bool success = false;
	};

	TextureLoadResult TryLoadTextureWithFallback(
		const std::string& assetName,
		const std::vector<std::string>& pathsToTry)
	{
		TextureLoadResult result;
		
		if (pathsToTry.empty()) {
			logger::warn("AmmoWheel Asset [{}]: No paths provided for fallback chain", assetName);
			return result;
		}

		logger::debug("AmmoWheel Asset [{}]: Starting fallback chain with {} paths", assetName, pathsToTry.size());

		for (size_t i = 0; i < pathsToTry.size(); i++) {
			std::string normalizedPath = NormalizePath(pathsToTry[i]);
			
			if (normalizedPath.empty()) {
				logger::debug("AmmoWheel Asset [{}]: Path {} is empty, skipping", assetName, i);
				continue;
			}

			logger::debug("AmmoWheel Asset [{}]: Trying path {}: '{}'", assetName, i, normalizedPath);

			// Check if file exists first
			if (!SafeFileExists(normalizedPath)) {
				logger::debug("AmmoWheel Asset [{}]: Path {} does not exist: '{}'", assetName, i, normalizedPath);
				continue;
			}

			// Try to load the texture
			int w = 0, h = 0;
			ID3D11ShaderResourceView* srv = nullptr;
			if (SafeLoadTexture(normalizedPath, &srv, w, h)) {
				result.srv = srv;
				result.width = w;
				result.height = h;
				result.loadedPath = normalizedPath;
				result.success = true;
				logger::debug("AmmoWheel Asset [{}]: Successfully loaded from path {}: '{}'", assetName, i, normalizedPath);
				return result;
			} else {
				logger::debug("AmmoWheel Asset [{}]: Load failed for path {}: '{}'", assetName, i, normalizedPath);
			}
		}

		logger::warn("AmmoWheel Asset [{}]: All {} paths failed, no texture loaded", assetName, pathsToTry.size());
		return result;
	}

	// ========== ROTATED QUAD RENDERING (PHASE 2) ==========
	// Rotate a 2D point around origin by angle (radians)
	ImVec2 RotatePoint(float x, float y, float cosA, float sinA) {
		return ImVec2(x * cosA - y * sinA, x * sinA + y * cosA);
	}

	// Draw a textured quad rotated by angle (radians) around center
	void DrawRotatedTexture(
		ImDrawList* drawList,
		ID3D11ShaderResourceView* texture,
		ImVec2 center,
		float width,
		float height,
		float angleRad,
		ImU32 tintColor)
	{
		if (!drawList || !texture) return;

		float halfW = width * 0.5f;
		float halfH = height * 0.5f;
		float cosA = std::cos(angleRad);
		float sinA = std::sin(angleRad);

		// Local corners (before rotation)
		ImVec2 p0 = RotatePoint(-halfW, -halfH, cosA, sinA);
		ImVec2 p1 = RotatePoint( halfW, -halfH, cosA, sinA);
		ImVec2 p2 = RotatePoint( halfW,  halfH, cosA, sinA);
		ImVec2 p3 = RotatePoint(-halfW,  halfH, cosA, sinA);

		// World corners
		ImVec2 v0(center.x + p0.x, center.y + p0.y);
		ImVec2 v1(center.x + p1.x, center.y + p1.y);
		ImVec2 v2(center.x + p2.x, center.y + p2.y);
		ImVec2 v3(center.x + p3.x, center.y + p3.y);

		// UVs
		ImVec2 uv0(0.0f, 0.0f);
		ImVec2 uv1(1.0f, 0.0f);
		ImVec2 uv2(1.0f, 1.0f);
		ImVec2 uv3(0.0f, 1.0f);

		drawList->AddImageQuad(
			reinterpret_cast<ImTextureID>(texture),
			v0, v1, v2, v3,
			uv0, uv1, uv2, uv3,
			tintColor
		);
	}

	// Compute icon position and size that fits inside a slot wedge
	// Returns: center position and max square size
	struct IconFitResult {
		ImVec2 center;
		float size;
		float rotationRad;
	};

	IconFitResult ComputeIconRectForSlot(
		ImVec2 wheelCenter,
		float slotMidAngle,      // Radians, center angle of slot
		float innerR,
		float outerR,
		float slotAngularSpan,   // Radians, total angle of slot
		float radialOffset,      // 0..1, where to place icon radially
		float paddingPx,
		float rotationSafetyScale,
		int rotationMode,        // 0=FollowSlot, 1=Upright, 2=Fixed
		float rotationOffsetDeg,
		float fixedAngleDeg)
	{
		IconFitResult result;
		result.center = wheelCenter;
		result.size = 0.0f;
		result.rotationRad = 0.0f;

		// Compute radial center
		float ringThickness = outerR - innerR;
		if (ringThickness <= 0.0f) return result;

		float r = innerR + ringThickness * std::clamp(radialOffset, 0.0f, 1.0f);
		result.center = ImVec2(
			wheelCenter.x + r * std::cos(slotMidAngle),
			wheelCenter.y + r * std::sin(slotMidAngle)
		);

		// Compute available radial thickness
		float availableRadial = ringThickness - 2.0f * paddingPx;
		if (availableRadial <= 0.0f) return result;

		// Compute available tangential width at radius r
		float availableTangential = 2.0f * r * std::sin(slotAngularSpan * 0.5f);
		availableTangential -= 2.0f * paddingPx;
		if (availableTangential <= 0.0f) return result;

		// Take minimum of radial and tangential
		float baseSize = (std::min)(availableRadial, availableTangential);

		// Apply rotation safety scale if rotating with slot
		if (rotationMode == 0) {  // FollowSlot
			baseSize *= std::clamp(rotationSafetyScale, 0.1f, 1.0f);
		}

		result.size = (std::max)(baseSize, 1.0f);

		// Compute rotation angle
		float offsetRad = rotationOffsetDeg * (IM_PI / 180.0f);
		switch (rotationMode) {
			case 0:  // FollowSlot - rotate to match slot direction
				// Add 90 degrees because icons point "up" by default
				result.rotationRad = slotMidAngle + (IM_PI * 0.5f) + offsetRad;
				break;
			case 1:  // Upright - no rotation
				result.rotationRad = offsetRad;
				break;
			case 2:  // Fixed
				result.rotationRad = fixedAngleDeg * (IM_PI / 180.0f) + offsetRad;
				break;
			default:
				result.rotationRad = 0.0f;
				break;
		}

		return result;
	}

	// ========== INDICATOR RENDERING (PHASE 3) ==========
	// Draw an arc indicator with configurable style
	void DrawIndicatorArc(
		ImDrawList* drawList,
		ImVec2 center,
		float radius,
		float startAngle,
		float endAngle,
		float thickness,
		ImU32 colorBegin,
		ImU32 colorEnd,
		float alpha,
		int animMode,
		float animSpeed,
		float alphaMult)
	{
		if (!drawList || thickness <= 0.0f) return;
		if (endAngle <= startAngle) return;

		// Apply animation
		float animAlpha = 1.0f;
		if (animMode == 1) {  // Pulse
			animAlpha = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * animSpeed);
		}

		float finalAlpha = alpha * animAlpha * alphaMult;
		if (finalAlpha <= 0.0f) return;

		// Apply alpha to colors
		uint8_t alphaBegin = static_cast<uint8_t>((colorBegin >> 24) * finalAlpha);
		uint8_t alphaEnd = static_cast<uint8_t>((colorEnd >> 24) * finalAlpha);
		ImU32 c1 = (colorBegin & 0x00FFFFFF) | (alphaBegin << 24);
		ImU32 c2 = (colorEnd & 0x00FFFFFF) | (alphaEnd << 24);

		// Draw arc using path with proper alpha blending
		int numSegments = static_cast<int>((endAngle - startAngle) / (IM_PI * 2.0f) * 64.0f);
		numSegments = std::clamp(numSegments, 8, 64);

		drawList->PathClear();
		for (int i = 0; i <= numSegments; i++) {
			float t = static_cast<float>(i) / static_cast<float>(numSegments);
			float angle = startAngle + t * (endAngle - startAngle);
			float r = radius;
			drawList->PathLineTo(ImVec2(center.x + r * std::cos(angle), center.y + r * std::sin(angle)));
		}
		
		// Use anti-aliasing for proper alpha blending
		drawList->PathStroke(c1, 0, thickness);
	}

} // anonymous namespace

AmmoWheel::AmmoWheel()
{
	EnsureInitialized();
}

AmmoWheel::~AmmoWheel()
{
	// CRITICAL: Restore timescale if we modified it (prevents stuck slow time on destruction)
	RestoreTimescale();
}

void AmmoWheel::RestoreTimescale()
{
	if (_ammoWheelModifiedTimeScale) {
		float current = Utils::Time::GGTM();
		logger::info("[TimeDilation] AmmoWheel restore: current={:.3f}, restoreTo={:.3f}", 
			current, _preAmmoWheelTimeScale);
		Utils::Time::SGTM(_preAmmoWheelTimeScale);
		_ammoWheelModifiedTimeScale = false;
	}
}

void AmmoWheel::EnsureInitialized()
{
	if (_initialized) {
		return;  // Idempotent
	}
	
	OnConfigChanged();  // Initialize cached layout values
	ResetInputLatch();
	_initialized = true;
	logger::info("AmmoWheel: Initialized");
}

void AmmoWheel::ResetInputLatch()
{
	_wasOpenChordDown = false;
	_blockMainWheel = false;
	logger::debug("AmmoWheel: Input latch reset");
}

void AmmoWheel::ForceClose()
{
	if (_state != WheelState::Closed) {
		logger::info("AmmoWheel: ForceClose (was state={})", static_cast<int>(_state));
	}
	if (_ammoWheelModifiedTimeScale) {
		Utils::Time::SGTM(_preAmmoWheelTimeScale);
		_ammoWheelModifiedTimeScale = false;
	}
	
	_state = WheelState::Closed;
	_hoveredIndex = -1;
	_prevHoveredIndex = -1;  // Reset hysteresis state
	_hoveredTime = 0.f;
	_cursorPos = { 0, 0 };
	_openTimer = 0.f;
	_closeTimer = 0.f;
	_blockMainWheel = false;
	_activationConsumed = false;  // Reset debounce
	InputBroker::ClearActiveOwner(InputBroker::kWheelerRefinedPluginId);
}

RE::FormID AmmoWheel::getEquippedAmmoFormID() const
{
	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return 0;
	}
	
	auto equippedAmmo = player->GetCurrentAmmo();
	if (equippedAmmo) {
		return equippedAmmo->GetFormID();
	}
	return 0;
}

int AmmoWheel::FindInitialHoverIndex()
{
	if (_ammoEntries.empty()) return -1;
	
	// 1. Priority: Try to restore last selected specific ammo (most intuitive)
	if (_lastSelectedAmmoID != 0) {
		for (int i = 0; i < static_cast<int>(_ammoEntries.size()); i++) {
			if (_ammoEntries[i].ammo && _ammoEntries[i].ammo->GetFormID() == _lastSelectedAmmoID) {
				// Verify count is sufficient if we filter by count? 
				// The list is already refreshed/filtered, so if it's here, it's valid.
				logger::info("AmmoWheel: Restored last selection '{}' at index {}", _ammoEntries[i].ammo->GetName(), i);
				return i;
			}
		}
	}
	
	// 2. Priority: Try to find equipped ammo (if persistent selection not found/unset)
	RE::FormID equippedID = getEquippedAmmoFormID();
	if (equippedID != 0) {
		for (int i = 0; i < static_cast<int>(_ammoEntries.size()); i++) {
			if (_ammoEntries[i].ammo && _ammoEntries[i].ammo->GetFormID() == equippedID) {
				logger::info("AmmoWheel: Starting hover on equipped ammo at index {}", i);
				return i;
			}
		}
	}
	
	// 3. Fallback: use last selected index if valid (continuity for position)
	if (_lastSelectedIndex >= 0 && _lastSelectedIndex < static_cast<int>(_ammoEntries.size())) {
		logger::info("AmmoWheel: Starting hover on last selected index {}", _lastSelectedIndex);
		return _lastSelectedIndex;
	}
	
	// 4. Ultimate fallback: first entry
	logger::info("AmmoWheel: Starting hover on first entry (index 0)");
	return 0;
}

bool AmmoWheel::ProcessInput()
{
	if (!_enabled) {
		return false;
	}
	
	// Check for valid weapon
	bool hasValidWeapon = (_currentWeaponType != WeaponType::None);
	if (!hasValidWeapon && Config::AmmoWheel::RequireWeaponEquipped) {
		if (_state != WheelState::Closed) {
			logger::info("AmmoWheel: CLOSE (no valid weapon)");
			ForceClose();
		}
		return false;
	}
	
	// Get current key states
	// Note: We use DirectInput key codes. The input system dispatches to us via Controls.
	// For chord detection, we need to check modifier state here.
	bool modDown = true;  // Default: no modifier required
	bool keyDown = false;
	
	// Check modifier key (if configured)
	if (Config::AmmoWheel::MKB::modifierKey != 0) {
		// Check if modifier is held using GetAsyncKeyState for DIK->VK mapping
		// For simplicity, we'll check via the input system's key state
		// This is a simplified check - the full implementation would use proper input hooks
		modDown = false;  // Will be set true if modifier is detected
		
		// For now, if a modifier is configured, we require it to be the same as toggleAmmoWheel
		// This means the user must configure a separate modifier key
		// TODO: Implement proper modifier key checking via input hooks
	}
	
	// The actual key press is handled by Controls::Dispatch calling Toggle()
	// This ProcessInput is for state management and chord detection
	
	// Update blocking state based on wheel state
	if (_state == WheelState::Opened || _state == WheelState::Opening) {
		_blockMainWheel = true;
	} else if (_state == WheelState::Closed) {
		_blockMainWheel = false;
	}
	
	return _blockMainWheel;
}

void AmmoWheel::Update(float a_deltaTime)
{
	// Poll for INI file changes (live config reload) - minimum 500ms between reloads
	// This must run even when disabled so we can detect re-enable
	_configPollAccum += a_deltaTime;
	if (_configPollAccum >= 0.5f) {
		_configPollAccum = 0.f;
		std::error_code ec;
		if (std::filesystem::exists(AMMO_WHEEL_INI_PATH, ec) && !ec) {
			auto currentWriteTime = std::filesystem::last_write_time(AMMO_WHEEL_INI_PATH, ec);
			if (!ec) {
				if (!_configInitialized) {
					_configLastWriteTime = currentWriteTime;
					_configInitialized = true;
					_enabled = Config::AmmoWheel::Enabled;
					_wasEnabled = _enabled;
					logger::debug("AmmoWheel: INI watcher initialized, path={}", AMMO_WHEEL_INI_PATH);
				} else if (currentWriteTime != _configLastWriteTime) {
					_configLastWriteTime = currentWriteTime;
					Config::ReadAmmoWheelConfig();
					
					// Handle enable/disable transition
					bool newEnabled = Config::AmmoWheel::Enabled;
					if (newEnabled != _enabled) {
						SetEnabled(newEnabled);
					}
					
					OnConfigChanged();
					Controls::BindAllInputsFromConfig();  // Rebind inputs on config change
					
					// If wheel is open, refresh ammo list immediately for filtering/sorting changes
					if (_state == WheelState::Opened || _state == WheelState::Opening) {
						RefreshAmmoList();
					}
					
					_configRevision++;
					logger::info("AmmoWheel: Config reloaded (rev {}), Enabled={}, Radius={:.0f}, Anchor={}, Theme={}",
						_configRevision, _enabled, Config::AmmoWheel::WheelRadius, 
						Config::AmmoWheel::ScreenAnchorIndex,
						Config::AmmoWheel::UseMainWheelTheme ? "Main" : "Ammo");
				}
			}
		}
	}

	// If disabled, only handle closing and cleanup
	if (!_enabled) {
		// Force close if somehow still open
		if (_state != WheelState::Closed) {
			ForceClose();
			_ammoEntries.clear();
		}
		return;
	}
	
	// Update blocking state for main wheel gating
	ProcessInput();

	// Check for viewport size changes
	ImVec2 currentViewportSize = ResolutionScale::Context::GetSingleton().GetRenderSize();
	if (_lastViewportSize.x != 0.f && _lastViewportSize.y != 0.f) {
		if (currentViewportSize.x != _lastViewportSize.x || currentViewportSize.y != _lastViewportSize.y) {
			OnConfigChanged();
			logger::debug("AmmoWheel: Viewport changed, recalculating layout");
		}
	}
	_lastViewportSize = currentViewportSize;

	// Update weapon state each frame
	UpdateWeaponState();

	// Handle closed state - close popup if open
	if (_state == WheelState::Closed) {
		if (ImGui::IsPopupOpen(AMMO_WHEEL_POPUP_ID)) {
			ImGui::SetNextWindowPos(ImVec2(-100, -100));
			ImGui::BeginPopup(AMMO_WHEEL_POPUP_ID);
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		return;
	}

	// Open popup if not already open
	// NOTE: Do NOT reset cursor/hover state here - TryOpen() already initializes them correctly
	// to point at the last selected slot. Resetting here would override that initialization.
	if (!ImGui::IsPopupOpen(AMMO_WHEEL_POPUP_ID)) {
		ImGui::OpenPopup(AMMO_WHEEL_POPUP_ID);
	}

	ImGui::SetNextWindowPos(ImVec2(-100, -100));

	if (ImGui::BeginPopup(AMMO_WHEEL_POPUP_ID)) {
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRectFullScreen();

		// Update timers and fade
		_openTimer += a_deltaTime;
		float fadeLerp = 1.0f;

		switch (_state) {
		case WheelState::Opening:
			fadeLerp = std::fminf(_openTimer / Config::Animation::FadeTime, 1.f);
			if (_openTimer >= Config::Animation::FadeTime) {
				_state = WheelState::Opened;
			}
			break;
		case WheelState::Closing:
			_closeTimer += a_deltaTime;
			fadeLerp = std::fmaxf(1.f - _closeTimer / Config::Animation::FadeTime, 0.f);
			if (_closeTimer >= Config::Animation::FadeTime) {
				_state = WheelState::Closed;
				_closeTimer = 0.f;
				InputBroker::ClearActiveOwner(InputBroker::kWheelerRefinedPluginId);
			}
			break;
		default:
			break;
		}

		DrawArgs drawArgs;
		// Phase 2G: Apply CustomOpacity to final alpha
		float customOpacity = std::clamp(Config::AmmoWheel::CustomOpacity, 0.0f, 1.0f);
		drawArgs.alphaMult = fadeLerp * customOpacity;

		draw(drawArgs);

		drawList->PopClipRect();
		ImGui::EndPopup();
	}
}

void AmmoWheel::OnConfigChanged()
{
	Config::OffsetAmmoWheelSizingToViewport();
	const auto& layoutState = Config::AmmoWheel::LayoutScaling::Runtime;

	// Recompute all derived layout values from config
	_cachedOuterRadius = Config::AmmoWheel::WheelRadius;
	_cachedInnerRadius = _cachedOuterRadius * Config::AmmoWheel::InnerRadiusRatio;
	
	// Text radius: midpoint + offset (range extended to -200..+400)
	float textRadialOffset = std::clamp(Config::AmmoWheel::TextRadialOffsetPx, -200.0f, 400.0f);
	_cachedTextRadius = (_cachedInnerRadius + _cachedOuterRadius) / 2.0f + textRadialOffset;
	
	// Icon radius: use IconRadiusRatio (0..1 between inner and outer) + offset
	// IconRadiusRatio=0.0 -> inner edge, 0.5 -> midpoint, 1.0 -> outer edge
	float ringThickness = _cachedOuterRadius - _cachedInnerRadius;
	float iconBaseRadius = _cachedInnerRadius + ringThickness * std::clamp(Config::AmmoWheel::IconRadiusRatio, 0.0f, 1.0f);
	float iconRadialOffset = std::clamp(Config::AmmoWheel::IconRadialOffsetPx, -100.0f, 100.0f);
	_cachedIconRadius = iconBaseRadius + iconRadialOffset;
	
	// Count radius: use CountRadiusRatio (0..1 between inner and outer)
	// CountRadiusRatio=0.0 -> inner edge, 0.5 -> midpoint, 1.0 -> outer edge
	float countBaseRadius = _cachedInnerRadius + ringThickness * std::clamp(Config::AmmoWheel::CountRadiusRatio, 0.0f, 1.0f);
	_cachedCountRadius = countBaseRadius;  // No offset for count
	
	// Cache icon size (use IconSizePx if set, else IconSize)
	_cachedIconSize = Config::AmmoWheel::IconSizePx > 0.0f ? Config::AmmoWheel::IconSizePx : Config::AmmoWheel::IconSize;
	_cachedIconSize = std::clamp(_cachedIconSize, 16.0f, 256.0f);
	
	// Recompute screen position based on anchor
	_cachedScreenPos = calculateScreenPosition();

	if (layoutState.LayoutActive && Config::AmmoWheel::LayoutScaling::ClampToScreen &&
		layoutState.GameW > 0.0f && layoutState.GameH > 0.0f) {
		const float safePad = Config::AmmoWheel::LayoutScaling::SafePadPx * layoutState.CombinedU;
		const float radius = _cachedOuterRadius;
		ImVec2 clamped = _cachedScreenPos;
		clamped.x = std::clamp(clamped.x, radius + safePad, layoutState.GameW - radius - safePad);
		clamped.y = std::clamp(clamped.y, radius + safePad, layoutState.GameH - radius - safePad);
		const float deltaX = clamped.x - _cachedScreenPos.x;
		const float deltaY = clamped.y - _cachedScreenPos.y;
		if (std::fabs(deltaX) > 0.1f || std::fabs(deltaY) > 0.1f) {
			logger::info("[AmmoWheel.LayoutScaling] Clamp applied: before=({:.1f},{:.1f}), after=({:.1f},{:.1f}), r={:.1f}, safePad={:.1f}",
				_cachedScreenPos.x, _cachedScreenPos.y, clamped.x, clamped.y, radius, safePad);
			_cachedScreenPos = clamped;
		}
	}
	
	// ========== PHASE 3: VALIDATE VISUAL POLISH SETTINGS ==========
	// Clamp visual polish values to safe ranges to prevent rendering issues
	Config::AmmoWheel::BorderInnerScale = std::clamp(Config::AmmoWheel::BorderInnerScale, 0.9f, 1.5f);
	Config::AmmoWheel::BorderOuterScale = std::clamp(Config::AmmoWheel::BorderOuterScale, 0.95f, 1.6f);
	Config::AmmoWheel::BackgroundRadiusScale = std::clamp(Config::AmmoWheel::BackgroundRadiusScale, 0.8f, 1.5f);
	Config::AmmoWheel::BackgroundOpacity = std::clamp(Config::AmmoWheel::BackgroundOpacity, 0.0f, 1.0f);
	Config::AmmoWheel::CustomOpacity = std::clamp(Config::AmmoWheel::CustomOpacity, 0.1f, 1.0f);
	
	// Ensure border scales are ordered correctly (inner < outer)
	if (Config::AmmoWheel::BorderInnerScale >= Config::AmmoWheel::BorderOuterScale) {
		Config::AmmoWheel::BorderOuterScale = Config::AmmoWheel::BorderInnerScale + 0.03f;
		if (Config::AmmoWheel::DebugLogNavigation) {
			logger::warn("[AmmoWheel] BorderInnerScale >= BorderOuterScale, auto-corrected");
		}
	}
	
	// Reset navigation filters if ApplyMode is Live (0) and settings changed
	if (Config::AmmoWheel::NavigationApplyMode == 0) {
		ResetNavigationFilters();
	}
	
	// Debug logging if enabled
	if (Config::AmmoWheel::Debug::LogLayout) {
		logger::info("AmmoWheel::OnConfigChanged: pos=({:.0f},{:.0f}), radius={:.0f}, inner={:.0f}, iconRadius={:.0f}, countRadius={:.0f}, iconSize={:.0f}",
			_cachedScreenPos.x, _cachedScreenPos.y, _cachedOuterRadius, _cachedInnerRadius, 
			_cachedIconRadius, _cachedCountRadius, _cachedIconSize);
	}
}

void AmmoWheel::SetEnabled(bool a_enabled)
{
	if (_enabled == a_enabled) {
		return;  // Idempotent
	}

	_wasEnabled = _enabled;
	_enabled = a_enabled;
	logger::info("AmmoWheel: SetEnabled {} -> {}", _wasEnabled, _enabled);

	ResetInputLatch();  // Always reset input state on enable/disable transition

	if (!_enabled) {
		// Disable path: force close and clear state
		ForceClose();
		_ammoEntries.clear();
		logger::info("AmmoWheel: Disabled, state cleared");
		return;
	}

	// Re-enable path: ensure initialized and reset to clean idle state
	EnsureInitialized();
	_needsListRefresh = true;
	
	// Update weapon state immediately so CanOpen() works on first key press
	UpdateWeaponState();
	
	// Rebind inputs so new bindings work without restart
	Controls::BindAllInputsFromConfig();
	
	logger::info("AmmoWheel: Re-enabled, ready to open (weapon={})", 
		_currentWeaponType == WeaponType::Bow ? "Bow" : 
		_currentWeaponType == WeaponType::Crossbow ? "Crossbow" : "None");
}

bool AmmoWheel::CanOpen() const
{
	// Check player exists and is loaded
	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->Is3DLoaded()) {
		logger::debug("AmmoWheel::CanOpen rejected: player not ready");
		return false;
	}

	// Check UI state - reject if conflicting menus are open
	auto ui = RE::UI::GetSingleton();
	if (!ui) {
		logger::debug("AmmoWheel::CanOpen rejected: UI not available");
		return false;
	}

	// List of menus that conflict with opening the ammo wheel
	static constexpr std::array<std::string_view, 19> conflictingMenus({
		RE::BookMenu::MENU_NAME,
		RE::BarterMenu::MENU_NAME,
		RE::CraftingMenu::MENU_NAME,
		RE::JournalMenu::MENU_NAME,
		RE::LevelUpMenu::MENU_NAME,
		RE::LockpickingMenu::MENU_NAME,
		RE::LoadingMenu::MENU_NAME,
		RE::MainMenu::MENU_NAME,
		RE::MapMenu::MENU_NAME,
		RE::RaceSexMenu::MENU_NAME,
		RE::SleepWaitMenu::MENU_NAME,
		RE::StatsMenu::MENU_NAME,
		RE::TweenMenu::MENU_NAME,
		RE::Console::MENU_NAME,
		RE::DialogueMenu::MENU_NAME,
		RE::GiftMenu::MENU_NAME,
		RE::ModManagerMenu::MENU_NAME,
		RE::ContainerMenu::MENU_NAME,
		"LootMenu"
	});

	for (std::string_view menuName : conflictingMenus) {
		if (ui->IsMenuOpen(menuName)) {
			logger::debug("AmmoWheel::CanOpen rejected: menu '{}' is open", menuName);
			return false;
		}
	}

	// Check if ranged weapon is equipped (unless config allows opening without)
	if (Config::AmmoWheel::RequireWeaponEquipped && _currentWeaponType == WeaponType::None) {
		logger::debug("AmmoWheel::CanOpen rejected: no ranged weapon equipped");
		return false;
	}

	return true;
}

void AmmoWheel::UpdateWeaponState()
{
	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		_currentWeaponType = WeaponType::None;
		return;
	}

	// Check equipped weapon in right hand first, then left
	auto rightEquipped = player->GetEquippedObject(false);
	auto leftEquipped = player->GetEquippedObject(true);

	RE::TESObjectWEAP* weapon = nullptr;
	if (rightEquipped) {
		weapon = rightEquipped->As<RE::TESObjectWEAP>();
	}
	if (!weapon && leftEquipped) {
		weapon = leftEquipped->As<RE::TESObjectWEAP>();
	}

	if (!weapon) {
		_currentWeaponType = WeaponType::None;
		return;
	}

	// Check weapon type
	auto weaponType = weapon->GetWeaponType();
	if (weaponType == RE::WEAPON_TYPE::kBow) {
		_currentWeaponType = WeaponType::Bow;
	} else if (weaponType == RE::WEAPON_TYPE::kCrossbow) {
		_currentWeaponType = WeaponType::Crossbow;
	} else {
		_currentWeaponType = WeaponType::None;
	}
}

// ========== AMMO_KID.ini KEYWORD MAPPING LOADER ==========
void AmmoWheel::LoadKeywordIconDefinitions()
{
	// Double-checked locking for thread safety
	if (_kidMapsLoaded.load(std::memory_order_acquire)) return;

	std::lock_guard<std::mutex> lock(_kidLoadMutex);
	if (_kidMapsLoaded.load(std::memory_order_acquire)) return;

	try {
		if (!SafeFileExists(AMMO_KID_INI_PATH)) {
			logger::info("AmmoWheel: No AMMO_KID.ini found, using defaults");
			_kidMapsLoaded.store(true, std::memory_order_release);
			return;
		}

		CSimpleIniA kidIni;
		kidIni.SetUnicode();

		if (kidIni.LoadFile(AMMO_KID_INI_PATH) < 0) {
			logger::error("AmmoWheel: Failed to load AMMO_KID.ini");
			_kidMapsLoaded.store(true, std::memory_order_release);
			return;
		}

		std::unordered_map<std::string, std::string> tempMap;
		tempMap.reserve(256);

		CSimpleIniA::TNamesDepend sections;
		kidIni.GetAllSections(sections);

		for (const auto& section : sections) {
			if (std::strcmp(section.pItem, "KeywordIcons") == 0) {
				CSimpleIniA::TNamesDepend keys;
				kidIni.GetAllKeys(section.pItem, keys);

				for (const auto& key : keys) {
					const char* iconPath = kidIni.GetValue(section.pItem, key.pItem);
					if (iconPath && std::strlen(iconPath) > 0) {
						tempMap[std::string(key.pItem)] = std::string(iconPath);
						logger::debug("AmmoWheel KID: {} -> {}", key.pItem, iconPath);
					}
				}
			}
		}

		_kidIconMap = std::move(tempMap);
		logger::info("AmmoWheel: Loaded {} keyword icon mappings from AMMO_KID.ini", _kidIconMap.size());

	} catch (const std::exception& e) {
		logger::error("AmmoWheel: Exception loading KID: {}", e.what());
	} catch (...) {
		logger::error("AmmoWheel: Unknown exception loading KID");
	}

	_kidMapsLoaded.store(true, std::memory_order_release);
}

std::string AmmoWheel::GetIconForKeyword(const char* keyword)
{
	if (!keyword || std::strlen(keyword) == 0) return "";

	LoadKeywordIconDefinitions();

	// After load, read-only access is safe (unordered_map is thread-safe for reads)
	auto it = _kidIconMap.find(keyword);
	if (it != _kidIconMap.end()) {
		return it->second;
	}
	return "";
}

// ========== PRESET SYSTEM: LOAD MAPPINGS FROM AMMO_KID.ini ==========
void AmmoWheel::LoadPresetMappings()
{
	using namespace Config::AmmoWheel::PresetSystem;
	
	// Double-checked locking
	if (PresetsLoaded.load(std::memory_order_acquire)) return;
	
	std::lock_guard<std::mutex> lock(PresetLoadMutex);
	if (PresetsLoaded.load(std::memory_order_acquire)) return;
	
	try {
		if (!SafeFileExists(AMMO_KID_INI_PATH)) {
			logger::info("AmmoWheel PresetSystem: No AMMO_KID.ini found, using defaults");
			PresetsLoaded.store(true, std::memory_order_release);
			return;
		}
		
		CSimpleIniA kidIni;
		kidIni.SetUnicode();
		
		if (kidIni.LoadFile(AMMO_KID_INI_PATH) < 0) {
			logger::error("AmmoWheel PresetSystem: Failed to load AMMO_KID.ini");
			PresetsLoaded.store(true, std::memory_order_release);
			return;
		}
		
		// [FormIDPresets] section: 0x000139C0 = Preset_Daedric
		CSimpleIniA::TNamesDepend formIdKeys;
		kidIni.GetAllKeys("FormIDPresets", formIdKeys);
		for (const auto& key : formIdKeys) {
			const char* presetId = kidIni.GetValue("FormIDPresets", key.pItem);
			if (presetId && std::strlen(presetId) > 0) {
				try {
					uint32_t formId = std::stoul(key.pItem, nullptr, 16);
					FormIDToPreset[formId] = presetId;
					logger::debug("AmmoWheel Preset: FormID 0x{:08X} -> {}", formId, presetId);
				} catch (...) {
					logger::warn("AmmoWheel Preset: Invalid FormID '{}'", key.pItem);
				}
			}
		}
		
		// [KeywordPresets] section: WeapMaterialDaedric = Preset_Daedric
		CSimpleIniA::TNamesDepend keywordKeys;
		kidIni.GetAllKeys("KeywordPresets", keywordKeys);
		for (const auto& key : keywordKeys) {
			std::string keyStr(key.pItem);
			// Check for inline overrides (keyword.property = value)
			size_t dotPos = keyStr.find('.');
			if (dotPos == std::string::npos) {
				// Simple preset mapping
				const char* presetId = kidIni.GetValue("KeywordPresets", key.pItem);
				if (presetId && std::strlen(presetId) > 0) {
					KeywordToPreset[keyStr] = presetId;
					logger::debug("AmmoWheel Preset: Keyword {} -> {}", keyStr, presetId);
				}
			} else {
				// Inline override: keyword.Icon = filename.svg
				std::string keyword = keyStr.substr(0, dotPos);
				std::string property = keyStr.substr(dotPos + 1);
				const char* value = kidIni.GetValue("KeywordPresets", key.pItem);
				if (value && std::strlen(value) > 0) {
					if (property == "Icon") {
						KeywordToIcon[keyword] = value;
						logger::debug("AmmoWheel Preset: Keyword {} icon override -> {}", keyword, value);
					}
					// Additional property overrides can be added here
				}
			}
		}
		
		// [TypePresets] section: Arrow = Preset_DefaultArrow
		CSimpleIniA::TNamesDepend typeKeys;
		kidIni.GetAllKeys("TypePresets", typeKeys);
		for (const auto& key : typeKeys) {
			const char* presetId = kidIni.GetValue("TypePresets", key.pItem);
			if (presetId && std::strlen(presetId) > 0) {
				TypeToPreset[key.pItem] = presetId;
				logger::debug("AmmoWheel Preset: Type {} -> {}", key.pItem, presetId);
			}
		}
		
		// [Fallback] section: Default = Preset_Default
		const char* fallback = kidIni.GetValue("Fallback", "Default");
		if (fallback && std::strlen(fallback) > 0) {
			FallbackPresetId = fallback;
			logger::debug("AmmoWheel Preset: Fallback -> {}", FallbackPresetId);
		}
		
		// [FormIDIcons] section for direct FormID -> icon overrides
		CSimpleIniA::TNamesDepend formIdIconKeys;
		kidIni.GetAllKeys("FormIDIcons", formIdIconKeys);
		for (const auto& key : formIdIconKeys) {
			const char* iconPath = kidIni.GetValue("FormIDIcons", key.pItem);
			if (iconPath && std::strlen(iconPath) > 0) {
				try {
					uint32_t formId = std::stoul(key.pItem, nullptr, 16);
					FormIDToIcon[formId] = iconPath;
					logger::debug("AmmoWheel Preset: FormID 0x{:08X} icon -> {}", formId, iconPath);
				} catch (...) {
					logger::warn("AmmoWheel Preset: Invalid FormID '{}'", key.pItem);
				}
			}
		}
		
		logger::info("AmmoWheel PresetSystem: Loaded {} FormID, {} Keyword, {} Type preset mappings",
			FormIDToPreset.size(), KeywordToPreset.size(), TypeToPreset.size());
		
	} catch (const std::exception& e) {
		logger::error("AmmoWheel PresetSystem: Exception loading mappings: {}", e.what());
	} catch (...) {
		logger::error("AmmoWheel PresetSystem: Unknown exception loading mappings");
	}
	
	PresetsLoaded.store(true, std::memory_order_release);
}

// ========== PRESET SYSTEM: LOAD STYLE PRESETS FROM Styles.ini ==========
void AmmoWheel::LoadStylePresets()
{
	using namespace Config::AmmoWheel;
	using namespace Config::AmmoWheel::PresetSystem;
	
	// Ensure mappings are loaded first
	LoadPresetMappings();
	
	// Check if already loaded
	if (!LoadedPresets.empty()) return;
	
	std::lock_guard<std::mutex> lock(PresetLoadMutex);
	if (!LoadedPresets.empty()) return;
	
	try {
		std::string stylesPath = Skin::StylesIniPath;
		if (!SafeFileExists(stylesPath)) {
			logger::info("AmmoWheel PresetSystem: No Styles.ini found at {}", stylesPath);
			// Add default preset
			LoadedPresets["Default"] = DefaultPreset;
			return;
		}
		
		CSimpleIniA stylesIni;
		stylesIni.SetUnicode();
		
		if (stylesIni.LoadFile(stylesPath.c_str()) < 0) {
			logger::error("AmmoWheel PresetSystem: Failed to load Styles.ini");
			LoadedPresets["Default"] = DefaultPreset;
			return;
		}
		
		// Find all [Preset.XXX] sections
		CSimpleIniA::TNamesDepend sections;
		stylesIni.GetAllSections(sections);
		
		for (const auto& section : sections) {
			std::string sectionName(section.pItem);
			if (sectionName.rfind("Preset.", 0) == 0) {
				std::string presetId = sectionName.substr(7);  // Remove "Preset." prefix
				
				StylePreset preset;
				preset.PresetId = presetId;
				
				// Helper lambda to read values
				auto readFloat = [&](const char* key, float defaultVal) -> float {
					return static_cast<float>(stylesIni.GetDoubleValue(section.pItem, key, defaultVal));
				};
				auto readInt = [&](const char* key, int defaultVal) -> int {
					return static_cast<int>(stylesIni.GetLongValue(section.pItem, key, defaultVal));
				};
				auto readBool = [&](const char* key, bool defaultVal) -> bool {
					return stylesIni.GetBoolValue(section.pItem, key, defaultVal);
				};
				auto readColor = [&](const char* key, ImU32 defaultVal) -> ImU32 {
					const char* val = stylesIni.GetValue(section.pItem, key);
					if (val && std::strlen(val) > 0) {
						try {
							return static_cast<ImU32>(std::stoul(val, nullptr, 16));
						} catch (...) {}
					}
					return defaultVal;
				};
				auto readString = [&](const char* key) -> std::string {
					const char* val = stylesIni.GetValue(section.pItem, key);
					return val ? std::string(val) : "";
				};
				
				// Slot geometry
				preset.SlotAngularPaddingDeg = readFloat("Slot.AngularPaddingDeg", 2.0f);
				preset.SlotInnerRadiusPadding = readFloat("Slot.InnerRadiusPadding", 0.0f);
				preset.SlotOuterRadiusPadding = readFloat("Slot.OuterRadiusPadding", 0.0f);
				preset.SlotCornerRounding = readFloat("Slot.CornerRounding", 0.0f);
				preset.BackgroundOpacity = readFloat("Slot.BackgroundOpacity", 0.75f);
				
				// Slot colors
				preset.UnhoveredColorBegin = readColor("Slot.UnhoveredColorBegin", IM_COL32(160, 144, 125, 128));
				preset.UnhoveredColorEnd = readColor("Slot.UnhoveredColorEnd", IM_COL32(120, 109, 94, 64));
				preset.HoveredColorBegin = readColor("Slot.HoveredColorBegin", IM_COL32(212, 196, 168, 255));
				preset.HoveredColorEnd = readColor("Slot.HoveredColorEnd", IM_COL32(181, 164, 141, 255));
				preset.SelectedColorBegin = readColor("Slot.SelectedColorBegin", IM_COL32(208, 160, 112, 255));
				preset.SelectedColorEnd = readColor("Slot.SelectedColorEnd", IM_COL32(160, 128, 96, 255));
				
				// Border/Frame
				preset.BorderEnabled = readBool("Border.Enabled", false);
				preset.BorderThickness = readFloat("Border.Thickness", 2.0f);
				preset.BorderColor = readColor("Border.Color", IM_COL32(180, 160, 140, 200));
				preset.FrameEnabled = readBool("Frame.Enabled", false);
				preset.FrameSvg = readString("Frame.Svg");
				preset.BackgroundSvg = readString("Background.Svg");
				
				// Text styling
				preset.TextColor = readColor("Text.Color", IM_COL32(240, 230, 210, 255));
				preset.TextShadowColor = readColor("Text.ShadowColor", IM_COL32(40, 30, 20, 255));
				preset.TextSize = readFloat("Text.Size", 18.0f);
				preset.TextShadowOffsetX = readFloat("Text.ShadowOffsetX", 1.0f);
				preset.TextShadowOffsetY = readFloat("Text.ShadowOffsetY", 1.0f);
				preset.TextWrapMode = readInt("Text.WrapMode", 1);
				preset.TextMaxLines = readInt("Text.MaxLines", 3);
				
				// Icon styling
				preset.IconsEnabled = readBool("Icon.Enabled", true);
				preset.IconPath = readString("Icon.Path");
				preset.IconPlacementMode = readInt("Icon.PlacementMode", 1);
				preset.IconRadialOffset = readFloat("Icon.RadialOffset", 0.55f);
				preset.IconPaddingPixels = readFloat("Icon.PaddingPixels", 6.0f);
				preset.IconRotationMode = readInt("Icon.RotationMode", 0);
				preset.IconRotationOffsetDeg = readFloat("Icon.RotationOffsetDeg", 0.0f);
				preset.IconFixedAngleDeg = readFloat("Icon.FixedAngleDeg", 0.0f);
				preset.IconRotationSafetyScale = readFloat("Icon.RotationSafetyScale", 0.85f);
				preset.IconTintColor = readColor("Icon.TintColor", IM_COL32(255, 255, 255, 255));
				
				// Helper to load indicator preset
				auto loadIndicator = [&](IndicatorPreset& ind, const std::string& prefix) {
					ind.Enabled = readBool((prefix + ".Enabled").c_str(), ind.Enabled);
					ind.Shape = readInt((prefix + ".Shape").c_str(), ind.Shape);
					ind.ThicknessPx = readFloat((prefix + ".Thickness").c_str(), ind.ThicknessPx);
					ind.RadiusOffsetPx = readFloat((prefix + ".RadiusOffset").c_str(), ind.RadiusOffsetPx);
					ind.StartAngleOffsetDeg = readFloat((prefix + ".StartAngleOffset").c_str(), ind.StartAngleOffsetDeg);
					ind.SweepDeg = readFloat((prefix + ".SweepDeg").c_str(), ind.SweepDeg);
					ind.ColorBegin = readColor((prefix + ".ColorBegin").c_str(), ind.ColorBegin);
					ind.ColorEnd = readColor((prefix + ".ColorEnd").c_str(), ind.ColorEnd);
					ind.Alpha = readFloat((prefix + ".Alpha").c_str(), ind.Alpha);
					ind.CapStyle = readInt((prefix + ".CapStyle").c_str(), ind.CapStyle);
					ind.AnimMode = readInt((prefix + ".AnimMode").c_str(), ind.AnimMode);
					ind.AnimSpeed = readFloat((prefix + ".AnimSpeed").c_str(), ind.AnimSpeed);
				};
				
				loadIndicator(preset.Selected, "Indicator.Selected");
				loadIndicator(preset.Hovered, "Indicator.Hovered");
				loadIndicator(preset.Active, "Indicator.Active");
				loadIndicator(preset.Charge, "Indicator.Charge");
				
				LoadedPresets[presetId] = std::move(preset);
				logger::info("AmmoWheel PresetSystem: Loaded preset '{}'", presetId);
			}
		}
		
		// Ensure default preset exists
		if (LoadedPresets.find("Default") == LoadedPresets.end()) {
			LoadedPresets["Default"] = DefaultPreset;
		}
		
		logger::info("AmmoWheel PresetSystem: Loaded {} presets from Styles.ini", LoadedPresets.size());
		
	} catch (const std::exception& e) {
		logger::error("AmmoWheel PresetSystem: Exception loading presets: {}", e.what());
		LoadedPresets["Default"] = DefaultPreset;
	} catch (...) {
		logger::error("AmmoWheel PresetSystem: Unknown exception loading presets");
		LoadedPresets["Default"] = DefaultPreset;
	}
}

// ========== PRESET RESOLVER: FormID -> Keyword -> Type -> Fallback ==========
const Config::AmmoWheel::StylePreset* AmmoWheel::ResolvePresetForAmmo(RE::TESAmmo* ammo)
{
	using namespace Config::AmmoWheel::PresetSystem;
	
	// ========== GATING: UsePresetStyles must be enabled ==========
	// When disabled, return nullptr to use legacy style behavior
	if (!Config::AmmoWheel::Skin::UsePresetStyles) {
		logger::debug("AmmoWheel Preset: UsePresetStyles=OFF, using legacy style mode");
		return nullptr;  // nullptr signals "use legacy styles"
	}
	
	// Ensure presets are loaded
	LoadStylePresets();
	
	if (!ammo) {
		logger::debug("AmmoWheel Preset: No ammo, using fallback preset");
		return &GetPreset(FallbackPresetId);
	}
	
	RE::FormID formID = ammo->GetFormID();
	
	// Priority 1: FormID preset
	auto formIt = FormIDToPreset.find(formID);
	if (formIt != FormIDToPreset.end()) {
		logger::debug("AmmoWheel Preset: FormID 0x{:08X} matched preset '{}'", formID, formIt->second);
		return &GetPreset(formIt->second);
	}
	
	// Priority 2: Keyword preset (first match)
	if (ammo->HasKeywordString("dummy")) {}  // Force keyword system init
	auto* keywordForm = ammo->As<RE::BGSKeywordForm>();
	if (keywordForm) {
		for (uint32_t i = 0; i < keywordForm->numKeywords; i++) {
			RE::BGSKeyword* kw = keywordForm->keywords[i];
			if (kw) {
				std::string kwEditorID = kw->GetFormEditorID();
				auto kwIt = KeywordToPreset.find(kwEditorID);
				if (kwIt != KeywordToPreset.end()) {
					logger::debug("AmmoWheel Preset: Keyword '{}' matched preset '{}'", kwEditorID, kwIt->second);
					return &GetPreset(kwIt->second);
				}
			}
		}
	}
	
	// Priority 3: Type preset (Arrow/Bolt)
	std::string typeKey = ammo->IsBolt() ? "Bolt" : "Arrow";
	auto typeIt = TypeToPreset.find(typeKey);
	if (typeIt != TypeToPreset.end()) {
		logger::debug("AmmoWheel Preset: Type '{}' matched preset '{}'", typeKey, typeIt->second);
		return &GetPreset(typeIt->second);
	}
	
	// Priority 4: Fallback
	logger::debug("AmmoWheel Preset: Using fallback preset '{}'", FallbackPresetId);
	return &GetPreset(FallbackPresetId);
}

// ========== ICON PATH RESOLVER: Preset override -> Priority search ==========
std::string AmmoWheel::ResolveIconPathForAmmo(RE::TESAmmo* ammo, const Config::AmmoWheel::StylePreset* preset)
{
	using namespace Config::AmmoWheel::PresetSystem;
	
	if (!ammo) return "";
	
	// ========== GATING: UsePresetStyles must be enabled for preset icon overrides ==========
	// When disabled, skip preset icon path resolution entirely
	if (!Config::AmmoWheel::Skin::UsePresetStyles) {
		logger::debug("AmmoWheel IconResolve: UsePresetStyles=OFF, skipping preset icon paths");
		return "";  // Empty = use legacy icon resolution in loadAmmoIcon
	}
	
	RE::FormID formID = ammo->GetFormID();
	std::string assetName = fmt::format("PresetIcon_0x{:08X}", formID);
	
	// Check preset icon override first
	if (preset && !preset->IconPath.empty()) {
		// Try icons/ folder first
		std::string fullPath = NormalizePath(Config::AmmoWheel::Skin::SkinRoot + "\\icons\\" + preset->IconPath);
		logger::debug("AmmoWheel [{}]: Checking preset icon path: {}", assetName, fullPath);
		if (SafeFileExists(fullPath)) {
			logger::debug("AmmoWheel [{}]: Found preset icon at: {}", assetName, fullPath);
			return fullPath;
		}
		// Try icons_custom/ folder
		fullPath = NormalizePath(Config::AmmoWheel::Skin::SkinRoot + "\\icons_custom\\" + preset->IconPath);
		logger::debug("AmmoWheel [{}]: Checking preset icon path: {}", assetName, fullPath);
		if (SafeFileExists(fullPath)) {
			logger::debug("AmmoWheel [{}]: Found preset icon at: {}", assetName, fullPath);
			return fullPath;
		}
		logger::debug("AmmoWheel [{}]: Preset icon '{}' not found, continuing fallback", assetName, preset->IconPath);
	}
	
	// Check FormID icon override
	auto formIconIt = FormIDToIcon.find(formID);
	if (formIconIt != FormIDToIcon.end()) {
		std::string fullPath = NormalizePath(Config::AmmoWheel::IconCustomDirectory + "\\" + formIconIt->second);
		logger::debug("AmmoWheel [{}]: Checking FormID icon override: {}", assetName, fullPath);
		if (SafeFileExists(fullPath)) {
			logger::debug("AmmoWheel [{}]: Found FormID icon at: {}", assetName, fullPath);
			return fullPath;
		}
	}
	
	// Check keyword icon overrides
	auto* keywordForm = ammo->As<RE::BGSKeywordForm>();
	if (keywordForm) {
		for (uint32_t i = 0; i < keywordForm->numKeywords; i++) {
			RE::BGSKeyword* kw = keywordForm->keywords[i];
			if (kw) {
				std::string kwEditorID = kw->GetFormEditorID();
				auto kwIconIt = KeywordToIcon.find(kwEditorID);
				if (kwIconIt != KeywordToIcon.end()) {
					std::string fullPath = NormalizePath(Config::AmmoWheel::IconCustomDirectory + "\\" + kwIconIt->second);
					logger::debug("AmmoWheel [{}]: Checking keyword icon override for '{}': {}", assetName, kwEditorID, fullPath);
					if (SafeFileExists(fullPath)) {
						logger::debug("AmmoWheel [{}]: Found keyword icon at: {}", assetName, fullPath);
						return fullPath;
					}
				}
			}
		}
	}
	
	// Fall through to existing priority search (handled in loadAmmoIcon)
	logger::debug("AmmoWheel [{}]: No preset/override icon found, will use loadAmmoIcon fallback chain", assetName);
	return "";
}

// ========== ENHANCED ICON LOADING WITH ROBUST FALLBACK ==========
void AmmoWheel::loadAmmoIcon(AmmoEntry& entry)
{
	if (!entry.ammo) {
		return;
	}
	
	// Check preset for icons enabled override
	bool iconsEnabled = Config::AmmoWheel::ShowIcons;
	if (entry.resolvedPreset) {
		iconsEnabled = entry.resolvedPreset->IconsEnabled;
	}
	
	// Early exit if icons disabled
	if (!iconsEnabled) {
		entry.iconImage.texture = nullptr;
		return;
	}

	RE::FormID formID = entry.ammo->GetFormID();
	bool isBolt = entry.ammo->IsBolt();
	std::string ammoName = entry.ammo->GetName();
	std::string assetName = fmt::format("Icon_0x{:08X}_{}", formID, isBolt ? "Bolt" : "Arrow");

	// Build the complete fallback path list
	std::vector<std::string> pathsToTry;
	pathsToTry.reserve(16);

	// ========== PRIORITY 0: Preset-resolved icon path ==========
	if (!entry.resolvedIconPath.empty()) {
		pathsToTry.push_back(entry.resolvedIconPath);
		logger::debug("AmmoWheel [{}]: Added preset-resolved path: {}", assetName, entry.resolvedIconPath);
	}

	// ========== PRIORITY 1: AmmoWheel FormID icon ==========
	if (Config::AmmoWheel::UseDedicatedIconFolder && formID != 0) {
		std::string formIdPath = fmt::format("{}\\0x{:08X}.svg",
			Config::AmmoWheel::IconCustomDirectory, formID);
		pathsToTry.push_back(formIdPath);
	}

	// ========== PRIORITY 2 & 2b: Keyword icons and KID mappings ==========
	if (Config::AmmoWheel::UseDedicatedIconFolder) {
		if (auto kwdForm = entry.ammo->As<RE::BGSKeywordForm>()) {
			if (kwdForm->numKeywords > 0 && kwdForm->numKeywords < 100) {
				// Priority 2b: KID mapping first
				for (uint32_t i = 0; i < kwdForm->numKeywords; i++) {
					auto kwd = kwdForm->keywords[i];
					if (!kwd) continue;
					const char* kwdName = kwd->GetFormEditorID();
					if (!kwdName || std::strlen(kwdName) == 0) continue;

					std::string kidIcon = GetIconForKeyword(kwdName);
					if (!kidIcon.empty()) {
						pathsToTry.push_back(fmt::format("{}\\{}", 
							Config::AmmoWheel::IconCustomDirectory, kidIcon));
					}
				}
				// Priority 2: Direct keyword filename
				for (uint32_t i = 0; i < kwdForm->numKeywords; i++) {
					auto kwd = kwdForm->keywords[i];
					if (!kwd) continue;
					const char* kwdName = kwd->GetFormEditorID();
					if (!kwdName || std::strlen(kwdName) == 0) continue;
					pathsToTry.push_back(fmt::format("{}\\{}.svg",
						Config::AmmoWheel::IconCustomDirectory, kwdName));
				}
			}
		}
	}

	// ========== PRIORITY 3: AmmoWheel type default ==========
	if (Config::AmmoWheel::UseDedicatedIconFolder) {
		const char* typeIcon = isBolt ? "bolt.svg" : "arrow.svg";
		pathsToTry.push_back(fmt::format("{}\\{}", Config::AmmoWheel::IconDirectory, typeIcon));
	}

	// ========== TRY ALL AMMOWHEEL-SPECIFIC PATHS ==========
	if (!pathsToTry.empty()) {
		TextureLoadResult result = TryLoadTextureWithFallback(assetName, pathsToTry);
		if (result.success) {
			entry.iconImage.texture = result.srv;
			entry.iconImage.width = result.width;
			entry.iconImage.height = result.height;
			logger::info("AmmoWheel [{}]: Final loaded from: {}", assetName, result.loadedPath);
			return;
		}
		logger::debug("AmmoWheel [{}]: All AmmoWheel-specific paths failed, trying Wheeler fallback", assetName);
	}

	// ========== PRIORITY 4: Main Wheeler system fallback ==========
	try {
		entry.iconImage = Texture::GetIconImage(
			isBolt ? Texture::icon_image_type::crossbow : Texture::icon_image_type::arrow,
			entry.ammo
		);
		if (entry.iconImage.texture) {
			logger::debug("AmmoWheel [{}]: Using main Wheeler icon", assetName);
			return;
		}
	} catch (const std::exception& e) {
		logger::error("AmmoWheel [{}]: Exception in Wheeler icon system: {}", assetName, e.what());
	} catch (...) {
		logger::error("AmmoWheel [{}]: Unknown exception in Wheeler icon system", assetName);
	}

	// ========== PRIORITY 5: Global fallback (icon_default) ==========
	try {
		entry.iconImage = Texture::GetIconImage(Texture::icon_image_type::icon_default);
		if (entry.iconImage.texture) {
			logger::warn("AmmoWheel [{}]: Using global fallback icon_default", assetName);
			return;
		}
	} catch (...) {
		logger::error("AmmoWheel [{}]: Exception loading icon_default", assetName);
	}

	// ========== CRITICAL: No icon available ==========
	entry.iconImage.texture = nullptr;
	entry.iconImage.width = 0;
	entry.iconImage.height = 0;
	logger::error("AmmoWheel [{}]: CRITICAL - No icon loaded, all fallbacks failed", assetName);
}

std::string AmmoWheel::TruncateTextToFit(const char* text, float maxWidth, float fontSize) const
{
	ImFont* font = ImGui::GetFont();
	if (!font || maxWidth <= 0.0f) {
		// Fallback: simple character truncation
		std::string str(text);
		if (str.length() > 12) {
			return str.substr(0, 10) + "..";
		}
		return str;
	}
	
	std::string original(text);
	ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, original.c_str());
	
	if (textSize.x <= maxWidth) {
		return original;  // Fits!
	}
	
	// Need to truncate - find optimal length
	std::string truncated = original;
	while (truncated.length() > 3) {
		truncated = original.substr(0, truncated.length() - 4) + "..";
		textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, truncated.c_str());
		if (textSize.x <= maxWidth) {
			return truncated;
		}
	}
	
	return "..";  // Ultimate fallback
}

void AmmoWheel::RefreshAmmoList()
{
	std::unique_lock lock(_lock);
	_ammoEntries.clear();

	// Reset diagnostic counters
	_lastTotalAmmoScanned = 0;
	_lastBaseGameAmmo = 0;
	_lastModdedAmmo = 0;
	_lastFilteredByModded = 0;
	_lastFilteredByWeapon = 0;
	_lastFilteredByMinCount = 0;

	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}

	// Base game ESM files for modded ammo detection
	static const std::set<std::string_view> baseGameFiles = {
		"Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"
	};

	auto inv = player->GetInventory();

	for (const auto& [item, data] : inv) {
		auto ammo = item->As<RE::TESAmmo>();
		if (!ammo) {
			continue;
		}

		_lastTotalAmmoScanned++;

		// Determine if this is modded ammo
		bool isModded = false;
		auto* file = ammo->GetFile(0);
		if (file) {
			std::string_view fileName = file->fileName;
			isModded = (baseGameFiles.find(fileName) == baseGameFiles.end());
		}
		
		if (isModded) {
			_lastModdedAmmo++;
		} else {
			_lastBaseGameAmmo++;
		}

		// Filter by weapon type (unless ShowAllAmmo is enabled)
		bool matchesWeapon = false;
		if (Config::AmmoWheel::ShowAllAmmo) {
			// Show all ammo types (arrows and bolts)
			matchesWeapon = true;
		} else if (_currentWeaponType == WeaponType::Bow && !ammo->IsBolt()) {
			matchesWeapon = true;
		} else if (_currentWeaponType == WeaponType::Crossbow && ammo->IsBolt()) {
			matchesWeapon = true;
		}

		if (!matchesWeapon) {
			_lastFilteredByWeapon++;
			continue;
		}

		// Filter modded ammo if ShowModdedAmmo is disabled
		if (!Config::AmmoWheel::ShowModdedAmmo && isModded) {
			_lastFilteredByModded++;
			continue;
		}

		int count = data.first;
		if (count < Config::AmmoWheel::MinimumAmmoCount) {
			_lastFilteredByMinCount++;
			continue;
		}

		AmmoEntry entry;
		entry.ammo = ammo;
		entry.count = count;
		entry.wheelItem = std::make_shared<WheelItemAmmo>(ammo);
		
		// Detect favorites via InventoryEntryData (NOT MagicFavorites which is spells-only)
		if (data.second) {
			entry.isFavorite = data.second->IsFavorited();
		}
		
		// Resolve unified reskin preset (when enabled, this is the single source of truth)
		auto& reskinSystem = AmmoWheelReskinUnified::ReskinSystem::GetSingleton();
		if (reskinSystem.IsEnabled()) {
			entry.reskinEntry = reskinSystem.ResolveForAmmo(ammo);
			if (Config::AmmoWheel::Debug::LogPresetResolution) {
				logger::info("[AmmoWheel] Resolved preset for {}: {} via {}",
					ammo->GetName(),
					entry.reskinEntry.preset ? entry.reskinEntry.preset->id : "null",
					AmmoWheelReskinUnified::GetResolutionSourceName(entry.reskinEntry.source));
			}
		}
		
		// Legacy: Resolve style preset for this ammo (FormID -> Keyword -> Type -> Fallback)
		entry.resolvedPreset = ResolvePresetForAmmo(ammo);
		
		// Resolve icon path (preset override -> priority search)
		entry.resolvedIconPath = ResolveIconPathForAmmo(ammo, entry.resolvedPreset);
		
		loadAmmoIcon(entry);  // Load icon texture for this ammo
		_ammoEntries.push_back(entry);
	}

	// ========== PHASE 1: MULTI-CRITERIA SORTING SYSTEM ==========
	// Sort keys: 0=None, 1=Count, 2=Power, 3=Type, 4=Favorites
	auto getSortValue = [](const AmmoEntry& entry, int sortKey) -> int {
		switch (sortKey) {
			case 1: // Count
				return entry.count;
			case 2: // Power (damage)
				return entry.ammo ? static_cast<int>(entry.ammo->GetRuntimeData().data.damage * 100) : 0;
			case 3: // Type (Arrow=0, Bolt=1)
				return entry.ammo ? (entry.ammo->IsBolt() ? 1 : 0) : 0;
			case 4: // Favorites (favorited=1, not=0)
				// Use cached isFavorite from InventoryEntryData (NOT MagicFavorites which is spells-only)
				return entry.isFavorite ? 1 : 0;
			default:
				return 0;
		}
	};
	
	auto compareByKey = [&getSortValue](const AmmoEntry& a, const AmmoEntry& b, int sortKey, bool ascending) -> int {
		if (sortKey == 0) return 0;  // None - no comparison
		int valA = getSortValue(a, sortKey);
		int valB = getSortValue(b, sortKey);
		if (valA == valB) return 0;
		if (ascending) {
			return valA < valB ? -1 : 1;
		} else {
			return valA > valB ? -1 : 1;
		}
	};
	
	// Build effective sort keys (GroupByType overrides Primary to Type)
	int effectivePrimary = Config::AmmoWheel::Sort::GroupByType ? 3 : Config::AmmoWheel::Sort::Primary;
	int effectiveSecondary = Config::AmmoWheel::Sort::Secondary;
	int effectiveTertiary = Config::AmmoWheel::Sort::Tertiary;
	
	// Legacy compatibility: if old SortByCount is true and new Sort::Primary is default, use Count
	if (Config::AmmoWheel::SortByCount && effectivePrimary == 0) {
		effectivePrimary = 1;  // Count
	}
	
	auto multiCriteriaCompare = [&](const AmmoEntry& a, const AmmoEntry& b) -> bool {
		// FavoritesFirst: always push favorites to top
		if (Config::AmmoWheel::Sort::FavoritesFirst) {
			int favA = getSortValue(a, 4);
			int favB = getSortValue(b, 4);
			if (favA != favB) {
				return favA > favB;  // Favorites first (higher = first)
			}
		}
		
		// Primary sort
		int cmp = compareByKey(a, b, effectivePrimary, Config::AmmoWheel::Sort::DirectionPrimaryAsc);
		if (cmp != 0) return cmp < 0;
		
		// Secondary sort (tie-breaker)
		cmp = compareByKey(a, b, effectiveSecondary, Config::AmmoWheel::Sort::DirectionSecondaryAsc);
		if (cmp != 0) return cmp < 0;
		
		// Tertiary sort (tie-breaker)
		cmp = compareByKey(a, b, effectiveTertiary, Config::AmmoWheel::Sort::DirectionTertiaryAsc);
		if (cmp != 0) return cmp < 0;
		
		// Ultimate tie-breaker: name (alphabetical)
		return std::string_view(a.ammo->GetName()) < std::string_view(b.ammo->GetName());
	};
	
	// Use stable_sort if configured for deterministic ordering
	if (Config::AmmoWheel::Sort::Stable) {
		std::stable_sort(_ammoEntries.begin(), _ammoEntries.end(), multiCriteriaCompare);
	} else {
		std::sort(_ammoEntries.begin(), _ammoEntries.end(), multiCriteriaCompare);
	}
	
	// Debug logging for sorting
	if (Config::AmmoWheel::Debug::LogSorting && !_ammoEntries.empty()) {
		// Count favorites
		int favCount = 0;
		for (const auto& e : _ammoEntries) {
			if (e.isFavorite) favCount++;
		}
		
		logger::info("[Sort] FavoritesFirst={} detected={}/{} Primary={} Secondary={} Tertiary={}",
			Config::AmmoWheel::Sort::FavoritesFirst, favCount, _ammoEntries.size(),
			effectivePrimary, effectiveSecondary, effectiveTertiary);
		
		// Log all entries for verification
		for (size_t i = 0; i < _ammoEntries.size(); i++) {
			const auto& e = _ammoEntries[i];
			logger::info("  [{}] {} fav={} count={} dmg={:.0f} type={}",
				i, e.ammo->GetName(),
				e.isFavorite ? "YES" : "no",
				e.count,
				e.ammo->GetRuntimeData().data.damage,
				e.ammo->IsBolt() ? "Bolt" : "Arrow");
		}
	}

	// Log diagnostic info when debug overlay is enabled
	if (Config::AmmoWheel::EnableDebugOverlay) {
		logger::debug("AmmoWheel RefreshAmmoList: total={}, baseGame={}, modded={}, filteredWeapon={}, filteredModded={}, filteredMinCount={}, shown={}",
			_lastTotalAmmoScanned, _lastBaseGameAmmo, _lastModdedAmmo,
			_lastFilteredByWeapon, _lastFilteredByModded, _lastFilteredByMinCount,
			_ammoEntries.size());
	}

	// ========== PHASE 2: APPLY AMMO LIMITS (POST-SORT TRUNCATION) ==========
	// Single-pass limiter: iterate in sorted order, keep entries while per-type quota remains
	// Preserves existing ordering - does not reorder or split/merge vectors
	int arrowLimit = Config::AmmoWheel::Sort::ArrowLimit;
	int boltLimit = Config::AmmoWheel::Sort::BoltLimit;

	if (arrowLimit > 0 || boltLimit > 0) {
		int arrowsBefore = 0, boltsBefore = 0;
		for (const auto& e : _ammoEntries) {
			if (e.ammo && e.ammo->IsBolt()) ++boltsBefore;
			else if (e.ammo) ++arrowsBefore;
			// Skip entries with null ammo
		}
		
		int arrowsKept = 0;
		int boltsKept = 0;
		
		std::vector<AmmoEntry> limited;
		limited.reserve(_ammoEntries.size());
		
		for (auto& e : _ammoEntries) {
			// Skip null ammo entries entirely (don't count toward limits)
			if (!e.ammo) continue;
			
			const bool isBolt = e.ammo->IsBolt();
			
			if (isBolt) {
				if (boltLimit > 0 && boltsKept >= boltLimit) continue;
				++boltsKept;
			} else {
				if (arrowLimit > 0 && arrowsKept >= arrowLimit) continue;
				++arrowsKept;
			}
			
			limited.push_back(std::move(e));
		}
		
		_ammoEntries = std::move(limited);
		
		if (Config::AmmoWheel::Debug::LogSorting) {
			logger::info("[Sort] AmmoLimit: arrows {}→{}, bolts {}→{}",
				arrowsBefore, arrowsKept, boltsBefore, boltsKept);
		}
	}
}


void AmmoWheel::TryOpen()
{
	if (!_enabled) {
		logger::debug("AmmoWheel::TryOpen - disabled at runtime");
		return;
	}

	// Use CanOpen() for full state gating (menus, player state, weapon check)
	if (!CanOpen()) {
		return;  // CanOpen already logs the rejection reason
	}

	if (_state == WheelState::Closed || _state == WheelState::Closing) {
		RefreshAmmoList();
		_state = WheelState::Opening;
		InputBroker::SetActiveOwner(InputBroker::kWheelerRefinedPluginId);
		_openTimer = 0.f;
		_hoveredTime = 0.f;
		_blockMainWheel = true;  // Block main wheel while AmmoWheel is open
		_activationConsumed = false;  // Reset debounce for new open session
		
		// PHASE 2: Reset navigation filters on open to prevent "locked" state
		if (Config::AmmoWheel::ResetFiltersOnOpen) {
			ResetNavigationFilters();
		}
		
		// Reset hover sound state on open (prevents sound on first frame)
		_lastHoverSoundIndex = -1;
		_lastHoverSoundTimeMs = 0;
		
		// Start hover on equipped ammo or last selected
		_hoveredIndex = FindInitialHoverIndex();
		_prevHoveredIndex = _hoveredIndex;  // Initialize hysteresis to match initial selection
		// Lock hover until user provides meaningful input (mouse/gamepad movement)
		_hoverInputLock = true;
		
		// Initialize cursor position to point at the initial hover slot
		// This prevents the cursor from starting at center and causing selection jitter
		if (_hoveredIndex >= 0 && !_ammoEntries.empty()) {
			float arcAngle = getArcAngleRad();
			float startAngle = getStartAngleRad();
			int numEntries = static_cast<int>(_ammoEntries.size());
			float slotAngle = arcAngle / static_cast<float>(numEntries);
			float slotGapRad = Config::AmmoWheel::SlotGapDeg * (IM_PI / 180.0f);
			float effectiveSlotAngle = slotAngle - slotGapRad;
			
			// Use EXACT same formula as getHoveredIndex() for slot center angle
			float slotStartAngle = startAngle + slotAngle * static_cast<float>(_hoveredIndex) + slotGapRad * 0.5f;
			float slotCenterAngle = slotStartAngle + effectiveSlotAngle * 0.5f;
			
			float initRadius = Config::AmmoWheel::WheelRadius * 0.8f;
			_cursorPos.x = initRadius * std::cos(slotCenterAngle);
			_cursorPos.y = initRadius * std::sin(slotCenterAngle);
			
			// CRITICAL: Also initialize filter smoothedPos to match cursor position
			// This ensures the smoothed cursor starts at the selected slot, not at origin
			_mouseFilter.smoothedPos = _cursorPos;
			_gamepadFilter.smoothedPos = _cursorPos;
		} else {
			_cursorPos = { 0, 0 };
			_mouseFilter.smoothedPos = { 0, 0 };
			_gamepadFilter.smoothedPos = { 0, 0 };
		}
		
		// Start popup animation from 0 so it animates in
		_hoverPopupScale = 0.0f;
		
		// Recompute layout on open to ensure latest config is applied
		OnConfigChanged();

		{
			const auto& layoutState = Config::AmmoWheel::LayoutScaling::Runtime;
			const std::string src = Config::AmmoWheel::LayoutScaling::ConfigPresent ? Config::AmmoWheel::LayoutScaling::LoadedSourceTag : "off";
			const bool geomOn = layoutState.LayoutActive && Config::AmmoWheel::LayoutScaling::ScaleGeometry;
			const bool textOn = layoutState.LayoutActive && Config::AmmoWheel::LayoutScaling::ScaleText;
			const bool styleOn = layoutState.LayoutActive && Config::AmmoWheel::LayoutScaling::ScaleStylePx;
			const float styleScaleU = styleOn ? layoutState.CombinedU : layoutState.Msu;
			const bool clampOn = layoutState.LayoutActive && Config::AmmoWheel::LayoutScaling::ClampToScreen;
			logger::info("[AmmoWheel.LayoutScaling] src={}, display {:.0f}x{:.0f}, game {:.0f}x{:.0f}, ref {:.0f}x{:.0f}, ls=({:.3f},{:.3f},{:.3f}), ms=({:.3f},{:.3f},{:.3f}), combined=({:.3f},{:.3f},{:.3f}), geom={}, text={}, stylePx=({}, scaleU={:.3f}), clamp={}, center=({:.1f},{:.1f}), radius={:.1f}, input=GameSpace",
				src,
				layoutState.DisplayW, layoutState.DisplayH,
				layoutState.GameW, layoutState.GameH,
				Config::AmmoWheel::LayoutScaling::RefW, Config::AmmoWheel::LayoutScaling::RefH,
				layoutState.Lsx, layoutState.Lsy, layoutState.Lsu,
				layoutState.Msx, layoutState.Msy, layoutState.Msu,
				layoutState.CombinedX, layoutState.CombinedY, layoutState.CombinedU,
				geomOn ? "on" : "off",
				textOn ? "on" : "off",
				styleOn ? "on" : "off",
				styleScaleU,
				clampOn ? "on" : "off",
				_cachedScreenPos.x, _cachedScreenPos.y, _cachedOuterRadius);
		}

		if (Config::AmmoWheel::TimeSlowEnabled && Config::AmmoWheel::TimeSlowScale < 1.0f) {
			float currentTimeScale = Utils::Time::GGTM();
			// Only modify timescale if it's currently at normal (1.0) - don't override Slow Time shout or other effects
			if (currentTimeScale >= 0.99f && currentTimeScale <= 1.01f) {
				_preAmmoWheelTimeScale = currentTimeScale;
				_ammoWheelModifiedTimeScale = true;
				Utils::Time::SGTM(Config::AmmoWheel::TimeSlowScale);
			} else {
				// External time effect active (e.g., Slow Time shout) - don't touch timescale
				_ammoWheelModifiedTimeScale = false;
			}
		}

		if (Config::ResolutionFix::LogOncePerOpen) {
			auto& resolutionContext = ResolutionScale::Context::GetSingleton();
			resolutionContext.Update();
			const auto& state = resolutionContext.GetState();
			const char* mapping = state.active ? "Config::OffsetAmmoWheelSizingToViewport" : "None";
			logger::info("[ResolutionFix] AmmoWheel open: display {}x{}, game {}x{}, scaleX={:.3f}, scaleY={:.3f}, uniform={:.3f}, mode={}, mapping={}",
				state.displayW, state.displayH, state.gameW, state.gameH,
				state.scaleX, state.scaleY, state.uniformScale,
				GetResolutionFixModeName(Config::ResolutionFix::ModeSetting), mapping);
		}
		
		const char* weaponTypeStr = _currentWeaponType == WeaponType::Bow ? "Bow" : "Crossbow";
		logger::info("AmmoWheel: OPEN (weapon={}, ammoCount={}, initialHover={}, blocking=1)",
			weaponTypeStr, _ammoEntries.size(), _hoveredIndex);
	}
}

void AmmoWheel::Close()
{
	if (_state == WheelState::Opened || _state == WheelState::Opening) {
		// Handle RTU activation on close if enabled
		if (Config::AmmoWheel::UseRTUSystem && _hoveredIndex >= 0) {
			if (_hoveredTime >= Config::AmmoWheel::RTUHoverDelay) {
				ActivateHoveredAmmo();
			}
		}

		// Only restore timescale if AmmoWheel was the one that modified it
		if (_ammoWheelModifiedTimeScale) {
			Utils::Time::SGTM(_preAmmoWheelTimeScale);
			_ammoWheelModifiedTimeScale = false;
		}

		logger::info("AmmoWheel: CLOSE (was state={}, hovered={})", static_cast<int>(_state), _hoveredIndex);
		_state = WheelState::Closing;
		_closeTimer = 0.f;
		_blockMainWheel = false;  // Release main wheel block on close
		InputBroker::ClearActiveOwner(InputBroker::kWheelerRefinedPluginId);
	}
}

void AmmoWheel::Toggle()
{
	if (IsOpen()) {
		Close();
	} else {
		TryOpen();
	}
}

void AmmoWheel::CloseIfOpenedLongEnough()
{
	if (_state == WheelState::Opened && _openTimer >= Config::Control::Wheel::ToggleHoldThreshold) {
		Close();
	}
}

void AmmoWheel::UpdateCursorPosMouse(float a_deltaX, float a_deltaY)
{
	if (!IsOpen()) {
		return;
	}

	// TASK 1: Apply unified deadzone + smoothing filter for mouse
	float dt = ImGui::GetIO().DeltaTime;
	
	// Configure filter from config
	_mouseFilter.deadzone = Config::AmmoWheel::MouseDeadzone;
	_mouseFilter.smoothingSpeed = Config::AmmoWheel::MouseSmoothingSpeed;
	
	// Normalize delta to a reasonable range for deadzone comparison
	// Mouse deltas can be large, so we scale them down for filtering
	float sensitivity = 1.0f;
	ImVec2 rawDelta = {a_deltaX * sensitivity, a_deltaY * sensitivity};
	
	// Apply filter (deadzone + smoothing)
	ImVec2 filtered = _mouseFilter.Apply(rawDelta, dt, true);
	
	// If input is meaningful (above deadzone), unlock hover for cursor-based selection
	if (std::abs(filtered.x) > 0.1f || std::abs(filtered.y) > 0.1f) {
		_hoverInputLock = false;
	}
	
	// If hover is locked, don't update cursor position
	if (_hoverInputLock) {
		return;
	}
	
	_cursorPos.x += filtered.x;
	_cursorPos.y += filtered.y;  // Mouse: don't negate Y (raw delta is already in screen coordinates)

	// Clamp cursor to max radius
	float maxRadius = Config::AmmoWheel::WheelRadius * 1.5f;
	float dist = std::sqrt(_cursorPos.x * _cursorPos.x + _cursorPos.y * _cursorPos.y);
	if (dist > maxRadius) {
		float scale = maxRadius / dist;
		_cursorPos.x *= scale;
		_cursorPos.y *= scale;
	}
}

void AmmoWheel::UpdateCursorPosGamepad(float a_x, float a_y)
{
	if (!IsOpen()) {
		return;
	}

	// TASK 1: Apply unified deadzone + smoothing filter for gamepad
	float dt = ImGui::GetIO().DeltaTime;
	
	// Configure filter from config
	_gamepadFilter.deadzone = Config::AmmoWheel::GamepadDeadzone;
	_gamepadFilter.smoothingSpeed = Config::AmmoWheel::GamepadSmoothingSpeed;
	
	// Gamepad stick values are already normalized to [-1, 1]
	ImVec2 stickInput = {a_x, a_y};
	
	// Apply filter (deadzone + smoothing)
	ImVec2 filtered = _gamepadFilter.Apply(stickInput, dt, true);
	
	// If input is meaningful (above deadzone), unlock hover for cursor-based selection
	if (std::abs(filtered.x) > 0.1f || std::abs(filtered.y) > 0.1f) {
		_hoverInputLock = false;
	}
	
	// If hover is locked, don't update cursor position
	if (_hoverInputLock) {
		return;
	}
	
	// Convert filtered stick position to cursor position
	float maxRadius = Config::AmmoWheel::WheelRadius * 1.5f;
	_cursorPos.x = filtered.x * maxRadius;
	_cursorPos.y = -filtered.y * maxRadius;  // Negate Y: gamepad Y-positive is up, screen Y-positive is down
}

void AmmoWheel::ActivateHoveredAmmo()
{
	std::shared_lock lock(_lock);

	// Debounce: prevent repeated activation in same open session
	if (_activationConsumed) {
		logger::debug("AmmoWheel::ActivateHoveredAmmo - activation already consumed, ignoring");
		return;
	}

	if (_hoveredIndex < 0 || _hoveredIndex >= static_cast<int>(_ammoEntries.size())) {
		logger::debug("AmmoWheel::ActivateHoveredAmmo - no valid hovered index");
		return;
	}

	auto& entry = _ammoEntries[_hoveredIndex];
	if (!entry.ammo) {
		logger::debug("AmmoWheel::ActivateHoveredAmmo - null ammo entry");
		return;
	}

	auto player = RE::PlayerCharacter::GetSingleton();
	auto equipManager = RE::ActorEquipManager::GetSingleton();
	if (!player || !equipManager) {
		logger::warn("AmmoWheel::ActivateHoveredAmmo - null player or equipManager");
		return;
	}

	// Check if this ammo is already equipped - skip redundant equip
	RE::FormID currentEquipped = getEquippedAmmoFormID();
	RE::FormID targetFormID = entry.ammo->GetFormID();
	if (currentEquipped == targetFormID) {
		logger::debug("AmmoWheel::ActivateHoveredAmmo - ammo already equipped, skipping");
		_activationConsumed = true;  // Still consume to prevent spam
		return;
	}

	// Mark activation as consumed BEFORE equipping to prevent re-entry
	_activationConsumed = true;

	// Equip the selected ammo
	equipManager->EquipObject(player, entry.ammo);
	
	// Remember this selection for next time
	_lastSelectedIndex = _hoveredIndex;
	_lastSelectedAmmoID = targetFormID;
	
	logger::info("AmmoWheel: Equipped ammo '{}' (FormID: {:08X}, saved index={}, saved FormID={:08X})", 
		entry.ammo->GetName(), targetFormID, _lastSelectedIndex, _lastSelectedAmmoID);
}

void AmmoWheel::draw(DrawArgs a_drawArgs)
{
	std::shared_lock lock(_lock);

	// For Custom anchor mode, always recalculate position (allows live updates)
	// For preset anchors, use cached position for efficiency
	ImVec2 wheelCenter;
	if (Config::AmmoWheel::ScreenAnchorIndex == static_cast<uint32_t>(ScreenAnchor::Custom)) {
		wheelCenter = calculateScreenPosition();
	} else {
		wheelCenter = _cachedScreenPos;
	}
	
	// Apply fade animation offset
	wheelCenter.y += (1.f - a_drawArgs.alphaMult) * Config::Animation::ToggleVerticalFadeDistance;
	wheelCenter.x += (1.f - a_drawArgs.alphaMult) * Config::Animation::ToggleHorizontalFadeDistance;

	if (_ammoEntries.empty()) {
		// Draw "No Ammo" message using proper Drawer
		Drawer::draw_text(wheelCenter.x, wheelCenter.y, "No Ammo", C_SKYRIMWHITE, 30.f, a_drawArgs);
		return;
	}

	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}

	RE::TESObjectREFR::InventoryItemMap imap = player->GetInventory();
	
	// Get currently equipped ammo FormID for selection highlighting (derived each frame)
	RE::FormID equippedAmmoID = getEquippedAmmoFormID();

	// Use cached radii (recomputed on config change)
	float innerRadius = _cachedInnerRadius;
	float outerRadius = _cachedOuterRadius;

	int numEntries = static_cast<int>(_ammoEntries.size());
	float arcAngle = getArcAngleRad();
	float startAngle = getStartAngleRad();
	float slotAngle = arcAngle / static_cast<float>(numEntries);

	// Update hovered index based on cursor
	float cursorAngle = getCursorAngle();
	int prevHovered = _hoveredIndex;
	
	// Skip hover recalculation while input lock is active (preserves initial selection)
	if (!_hoverInputLock) {
		_hoveredIndex = getHoveredIndex(wheelCenter, cursorAngle);
	}
	// When locked, keep _hoveredIndex as initialized by TryOpen()
	
	// Update previous hovered index for hysteresis (must update AFTER getHoveredIndex uses it)
	// Note: We update at end of frame so next frame's getHoveredIndex has correct previous value

	// Update hover time and reset popup animation when switching slots
	if (_hoveredIndex >= 0 && _hoveredIndex == prevHovered) {
		_hoveredTime += ImGui::GetIO().DeltaTime;
	} else {
		_hoveredTime = 0.f;
		// Reset popup animation when hovering a new slot
		if (_hoveredIndex >= 0 && prevHovered >= 0 && _hoveredIndex != prevHovered) {
			_hoverPopupScale = 0.0f;
		}
		
		// Play hover sound when slot changes
		if (_hoveredIndex >= 0 && _hoveredIndex != prevHovered) {
			PlayHoverSlotSound(_hoveredIndex);
		}
	}
	
	// Update _prevHoveredIndex for next frame's hysteresis calculation
	_prevHoveredIndex = _hoveredIndex;

	// ========== UNIFIED RESKIN: WHEEL BACKGROUND ==========
	auto& reskinSystem = AmmoWheelReskinUnified::ReskinSystem::GetSingleton();
	bool reskinDrawnWheelBg = false;
	if (reskinSystem.IsEnabled() && !_ammoEntries.empty() && _ammoEntries[0].reskinEntry.preset) {
		auto drawList = ImGui::GetWindowDrawList();
		AmmoWheelReskinUnified::DrawContext ctx;
		ctx.center = wheelCenter;
		ctx.radius = outerRadius;
		ctx.alphaMult = a_drawArgs.alphaMult;
		ctx.slotAngleRad = 0.0f;  // No rotation for wheel background
		
		reskinDrawnWheelBg = reskinSystem.DrawTarget(
			AmmoWheelReskinUnified::VisualTarget::WheelBackground,
			_ammoEntries[0].reskinEntry, ctx, drawList);
	}

	// ========== VISUAL POLISH: BACKGROUND LAYER ==========
	// Use arc-based drawing to respect WheelShapeIndex (half-circle, quarter, etc.)
	// BackgroundOpacity is applied as a multiplier to ALL themes (user override layer)
	if (Config::AmmoWheel::BackgroundEnabled && !reskinDrawnWheelBg) {
		float bgRadius = outerRadius * Config::AmmoWheel::BackgroundRadiusScale;
		float userOpacityMult = Config::AmmoWheel::BackgroundOpacity;  // User slider (0.0-1.0)
		
		if (reskinSystem.IsEnabled()) {
			// Reskin enabled but no wheel background asset - use primitive fallback
			const auto& primitives = reskinSystem.GetPrimitiveFallback();
			ImU32 bgColor = primitives.wheelBackground;
			uint8_t bgAlpha = static_cast<uint8_t>((bgColor >> 24) * userOpacityMult * a_drawArgs.alphaMult);
			bgColor = (bgColor & 0x00FFFFFF) | (bgAlpha << 24);
			Drawer::draw_arc_gradient(
				wheelCenter, 0.0f, bgRadius,
				startAngle, startAngle + arcAngle,
				startAngle, startAngle + arcAngle,
				bgColor, bgColor,
				64, a_drawArgs
			);
		} else if (Config::AmmoWheel::UseSkyrimTheme) {
			// Skyrim theme: layered arc backgrounds (respects arcAngle)
			// Apply BackgroundOpacity as multiplier to theme alpha values
			
			// Outer glow layer
			ImU32 glowColor = Config::AmmoWheel::SkyrimTheme::BgOuterGlow;
			uint8_t baseGlowA = static_cast<uint8_t>(glowColor >> 24);
			uint8_t glowA = static_cast<uint8_t>(baseGlowA * userOpacityMult * a_drawArgs.alphaMult);
			glowColor = (glowColor & 0x00FFFFFF) | (glowA << 24);
			Drawer::draw_arc_gradient(
				wheelCenter, 0.0f, bgRadius * 1.05f,
				startAngle, startAngle + arcAngle,
				startAngle, startAngle + arcAngle,
				glowColor, glowColor,
				64, a_drawArgs
			);
			
			// Mid layer
			ImU32 midColor = Config::AmmoWheel::SkyrimTheme::BgMidLayer;
			uint8_t baseMidA = static_cast<uint8_t>(midColor >> 24);
			uint8_t midA = static_cast<uint8_t>(baseMidA * userOpacityMult * a_drawArgs.alphaMult);
			midColor = (midColor & 0x00FFFFFF) | (midA << 24);
			Drawer::draw_arc_gradient(
				wheelCenter, 0.0f, bgRadius,
				startAngle, startAngle + arcAngle,
				startAngle, startAngle + arcAngle,
				midColor, midColor,
				64, a_drawArgs
			);
			
			// Dark inner layer
			ImU32 darkColor = Config::AmmoWheel::SkyrimTheme::BgDarkLayer;
			uint8_t baseDarkA = static_cast<uint8_t>(darkColor >> 24);
			uint8_t darkA = static_cast<uint8_t>(baseDarkA * userOpacityMult * a_drawArgs.alphaMult);
			darkColor = (darkColor & 0x00FFFFFF) | (darkA << 24);
			Drawer::draw_arc_gradient(
				wheelCenter, 0.0f, bgRadius * 0.95f,
				startAngle, startAngle + arcAngle,
				startAngle, startAngle + arcAngle,
				darkColor, darkColor,
				64, a_drawArgs
			);
		} else {
			// Default: simple arc background (respects arcAngle)
			uint8_t bgAlpha = static_cast<uint8_t>(userOpacityMult * 255.f * a_drawArgs.alphaMult);
			ImU32 bgColor = IM_COL32(0, 0, 0, bgAlpha);
			Drawer::draw_arc_gradient(
				wheelCenter, 0.0f, bgRadius,
				startAngle, startAngle + arcAngle,
				startAngle, startAngle + arcAngle,
				bgColor, bgColor,
				64, a_drawArgs
			);
		}
	}

	// ========== VISUAL POLISH: DECORATIVE BORDER RING ==========
	// Draw border ring for all slot shapes (Arc gets gradient arc, others get circle)
	if (Config::AmmoWheel::BorderEnabled) {
		// Compute border radii with validation to prevent disappearing ring
		float innerScale = std::clamp(Config::AmmoWheel::BorderInnerScale, 0.9f, 1.5f);
		float outerScale = std::clamp(Config::AmmoWheel::BorderOuterScale, 0.95f, 1.6f);
		
		// Ensure inner < outer (prevent collapsed geometry)
		if (innerScale >= outerScale) {
			outerScale = innerScale + 0.03f;
		}
		
		float borderInner = outerRadius * innerScale;
		float borderOuter = outerRadius * outerScale;
		
		// Validate radii are positive and have meaningful thickness
		if (borderInner > 0.0f && borderOuter > borderInner && (borderOuter - borderInner) >= 1.0f) {
			ImU32 borderInnerColor, borderOuterColor;
			// TASK 1: Use border color override if enabled
			if (Config::AmmoWheel::BorderColorOverrideEnabled) {
				borderInnerColor = Config::AmmoWheel::BorderColorComputed;
				// Outer color is slightly darker version of inner
				uint8_t r = (Config::AmmoWheel::BorderColorComputed >> IM_COL32_R_SHIFT) & 0xFF;
				uint8_t g = (Config::AmmoWheel::BorderColorComputed >> IM_COL32_G_SHIFT) & 0xFF;
				uint8_t b = (Config::AmmoWheel::BorderColorComputed >> IM_COL32_B_SHIFT) & 0xFF;
				uint8_t a = (Config::AmmoWheel::BorderColorComputed >> IM_COL32_A_SHIFT) & 0xFF;
				borderOuterColor = IM_COL32(r * 3/4, g * 3/4, b * 3/4, a * 3/4);
			} else if (Config::AmmoWheel::UseSkyrimTheme) {
				// Skyrim theme: gold/bronze border
				borderInnerColor = Config::AmmoWheel::SkyrimTheme::BorderGold;
				borderOuterColor = Config::AmmoWheel::SkyrimTheme::BorderBronze;
			} else {
				// Default border colors
				borderInnerColor = Config::AmmoWheel::BorderColorInner;
				borderOuterColor = Config::AmmoWheel::BorderColorOuter;
			}
			
			// Apply alpha mult to border colors
			uint8_t innerA = static_cast<uint8_t>((borderInnerColor >> 24) * a_drawArgs.alphaMult);
			uint8_t outerA = static_cast<uint8_t>((borderOuterColor >> 24) * a_drawArgs.alphaMult);
			borderInnerColor = (borderInnerColor & 0x00FFFFFF) | (innerA << 24);
			borderOuterColor = (borderOuterColor & 0x00FFFFFF) | (outerA << 24);
			
			// Draw border based on slot shape
			if (Config::AmmoWheel::SlotShape == 0) {
				// Arc shape: draw gradient arc
				Drawer::draw_arc_gradient(
					wheelCenter, borderInner, borderOuter,
					startAngle, startAngle + arcAngle,
					startAngle, startAngle + arcAngle,
					borderInnerColor, borderOuterColor,
					64, a_drawArgs
				);
			} else {
				// Non-arc shapes (Circle, Pill, RoundedRect): draw full circle border
				auto drawList = ImGui::GetWindowDrawList();
				float borderThickness = borderOuter - borderInner;
				float borderMidRadius = (borderInner + borderOuter) / 2.0f;
				drawList->AddCircle(wheelCenter, borderMidRadius, borderInnerColor, 48, borderThickness);
			}
			
			// Debug logging for style resolution
			if (Config::AmmoWheel::DebugLogStyleResolution) {
				static bool loggedOnce = false;
				if (!loggedOnce) {
					logger::info("[AmmoWheel Style] Border ring drawn: innerR={:.1f}, outerR={:.1f}, theme={}",
						borderInner, borderOuter, Config::AmmoWheel::UseSkyrimTheme ? "Skyrim" : "Default");
					loggedOnce = true;
				}
			}
		} else {
			// Log warning if border ring cannot be drawn due to invalid geometry
			if (Config::AmmoWheel::DebugLogStyleResolution) {
				static bool warnedOnce = false;
				if (!warnedOnce) {
					logger::warn("[AmmoWheel Style] Border ring skipped - invalid geometry: innerR={:.1f}, outerR={:.1f}",
						borderInner, borderOuter);
					warnedOnce = true;
				}
			}
		}
	}

	// Draw slots with unique ImGui IDs to prevent highlight bleed
	for (int i = 0; i < numEntries; i++) {
		float slotStartAngle = startAngle + i * slotAngle;
		float slotEndAngle = slotStartAngle + slotAngle;
		bool hovered = (i == _hoveredIndex);

		ImGui::PushID(i);
		drawSlot(i, wheelCenter, hovered, innerRadius, outerRadius, slotStartAngle, slotEndAngle, equippedAmmoID, a_drawArgs);
		ImGui::PopID();
	}

	// ========== ANIMATION: SLOT DIVIDERS ==========
	// Only draw dividers for arc-shaped slots (radial lines don't make sense for floating shapes)
	if (Config::AmmoWheel::SlotDividersEnabled && Config::AmmoWheel::SlotShape == 0 && numEntries > 1) {
		ImU32 dividerColor = Config::AmmoWheel::SlotDividerColor;
		uint8_t divAlpha = static_cast<uint8_t>((dividerColor >> 24) * a_drawArgs.alphaMult);
		dividerColor = (dividerColor & 0x00FFFFFF) | (divAlpha << 24);
		float thickness = Config::AmmoWheel::SlotDividerThickness;
		
		for (int i = 0; i <= numEntries; i++) {
			float dividerAngle = startAngle + i * slotAngle;
			
			ImVec2 innerPt = ImVec2(
				wheelCenter.x + innerRadius * 0.95f * std::cos(dividerAngle),
				wheelCenter.y + innerRadius * 0.95f * std::sin(dividerAngle)
			);
			ImVec2 outerPt = ImVec2(
				wheelCenter.x + outerRadius * 1.05f * std::cos(dividerAngle),
				wheelCenter.y + outerRadius * 1.05f * std::sin(dividerAngle)
			);
			
			ImGui::GetWindowDrawList()->AddLine(innerPt, outerPt, dividerColor, thickness);
		}
	}

	// Draw highlight for hovered item at center
	if (_hoveredIndex >= 0 && _hoveredIndex < numEntries) {
		drawHighlight(wheelCenter, imap, a_drawArgs);
	}

	// Draw cursor indicator
	if (_hoveredIndex >= 0) {
		float cursorDist = _cachedTextRadius;
		ImVec2 cursorTip = ImVec2(
			wheelCenter.x + cursorDist * std::cos(cursorAngle),
			wheelCenter.y + cursorDist * std::sin(cursorAngle)
		);
		ImU32 cursorColor = IM_COL32(255, 255, 255, static_cast<int>(200 * a_drawArgs.alphaMult));
		ImGui::GetWindowDrawList()->AddCircleFilled(cursorTip, 5.0f, cursorColor);
	}

	// Draw hover magnify popup (shows full name and large icon outside wheel)
	drawHoverPopup(wheelCenter, a_drawArgs);

	// Optional debug overlay
	if (Config::AmmoWheel::EnableDebugOverlay) {
		ImVec2 viewport = ResolutionScale::Context::GetSingleton().GetRenderSize();
		float debugY = wheelCenter.y - _cachedOuterRadius - 110.f;
		
		// Line 1: Enabled state and config revision
		std::string line1 = fmt::format("Enabled:{} Rev:{} Anchor:{} Pos:({:.0f},{:.0f})", 
			_enabled, _configRevision, Config::AmmoWheel::ScreenAnchorIndex,
			wheelCenter.x, wheelCenter.y);
		Drawer::draw_text(wheelCenter.x, debugY, line1.c_str(), IM_COL32(255, 255, 0, 200), 11.f, a_drawArgs);
		
		// Line 2: Theme and text settings
		const char* themeName = Config::AmmoWheel::UseMainWheelTheme ? "MainWheel" : "AmmoWheel";
		std::string line2 = fmt::format("Theme:{} NameScale:{:.2f} Radius:{:.0f}",
			themeName, Config::AmmoWheel::NameTextScale, Config::AmmoWheel::WheelRadius);
		Drawer::draw_text(wheelCenter.x, debugY + 12.f, line2.c_str(), IM_COL32(255, 255, 0, 200), 11.f, a_drawArgs);
		
		// Line 3: Indicator settings
		std::string line3 = fmt::format("LowAmmo: enabled={} thresh={} | Hovered:{}", 
			Config::AmmoWheel::LowAmmoIndicatorEnabled, Config::AmmoWheel::LowAmmoThreshold, _hoveredIndex);
		Drawer::draw_text(wheelCenter.x, debugY + 24.f, line3.c_str(), IM_COL32(255, 255, 0, 200), 11.f, a_drawArgs);
		
		// Line 4: Filtering settings
		std::string line4 = fmt::format("ShowAll:{} ShowModded:{} MinCount:{} SortByCount:{}", 
			Config::AmmoWheel::ShowAllAmmo, Config::AmmoWheel::ShowModdedAmmo,
			Config::AmmoWheel::MinimumAmmoCount, Config::AmmoWheel::SortByCount);
		Drawer::draw_text(wheelCenter.x, debugY + 36.f, line4.c_str(), IM_COL32(255, 255, 0, 200), 11.f, a_drawArgs);
		
		// Line 5: Ammo counts (diagnostic for ShowModdedAmmo)
		std::string line5 = fmt::format("Ammo: total={} base={} modded={} shown={}", 
			_lastTotalAmmoScanned, _lastBaseGameAmmo, _lastModdedAmmo, numEntries);
		Drawer::draw_text(wheelCenter.x, debugY + 48.f, line5.c_str(), IM_COL32(255, 255, 0, 200), 11.f, a_drawArgs);
		
		// Line 6: Filter stats
		std::string line6 = fmt::format("Filtered: weapon={} modded={} minCount={}", 
			_lastFilteredByWeapon, _lastFilteredByModded, _lastFilteredByMinCount);
		Drawer::draw_text(wheelCenter.x, debugY + 60.f, line6.c_str(), IM_COL32(255, 255, 0, 200), 11.f, a_drawArgs);
		
		// Line 7: Viewport and cursor
		std::string line7 = fmt::format("VP:({:.0f}x{:.0f}) Angle:{:.1f}°", 
			viewport.x, viewport.y, cursorAngle * 180.f / 3.14159f);
		Drawer::draw_text(wheelCenter.x, debugY + 72.f, line7.c_str(), IM_COL32(255, 255, 0, 200), 11.f, a_drawArgs);
		
		// Draw segment boundaries
		for (int i = 0; i <= numEntries; i++) {
			float boundaryAngle = startAngle + i * slotAngle;
			ImVec2 innerPt = ImVec2(
				wheelCenter.x + innerRadius * std::cos(boundaryAngle),
				wheelCenter.y + innerRadius * std::sin(boundaryAngle)
			);
			ImVec2 outerPt = ImVec2(
				wheelCenter.x + outerRadius * std::cos(boundaryAngle),
				wheelCenter.y + outerRadius * std::sin(boundaryAngle)
			);
			ImGui::GetWindowDrawList()->AddLine(innerPt, outerPt, IM_COL32(255, 255, 0, 100), 1.0f);
		}
	}
	
	// ========== RESKIN DEBUG OVERLAY ==========
	if (Config::AmmoWheel::Debug::ShowReskinOverlay) {
		float debugX = 10.f;
		float debugY = 10.f;
		float lineHeight = 13.f;
		ImU32 headerColor = IM_COL32(0, 255, 255, 220);
		ImU32 textColor = IM_COL32(200, 200, 200, 200);
		ImU32 valueColor = IM_COL32(100, 255, 100, 200);
		
		// Get debug info from reskin system
		const AmmoWheelReskinUnified::ResolvedEntry* hoveredEntry = nullptr;
		if (_hoveredIndex >= 0 && _hoveredIndex < static_cast<int>(_ammoEntries.size())) {
			hoveredEntry = &_ammoEntries[_hoveredIndex].reskinEntry;
		}
		auto debugInfo = reskinSystem.GetDebugInfo(hoveredEntry);
		
		// Header
		Drawer::draw_text(debugX, debugY, "[AmmoWheel Reskin Debug]", headerColor, 12.f, a_drawArgs);
		debugY += lineHeight * 1.2f;
		
		// Reskin status
		std::string statusLine = fmt::format("Reskin: {} | BasePath: {}",
			debugInfo.reskinEnabled ? "ENABLED" : "DISABLED",
			reskinSystem.GetBasePath());
		Drawer::draw_text(debugX, debugY, statusLine.c_str(), textColor, 10.f, a_drawArgs);
		debugY += lineHeight;
		
		// Hovered slot info
		if (hoveredEntry && hoveredEntry->preset) {
			std::string presetLine = fmt::format("Preset: {} | Resolved by: {} ({})",
				debugInfo.activePresetId,
				AmmoWheelReskinUnified::GetResolutionSourceName(debugInfo.resolutionSource),
				debugInfo.resolutionKey);
			Drawer::draw_text(debugX, debugY, presetLine.c_str(), valueColor, 10.f, a_drawArgs);
			debugY += lineHeight;
			
			// Per-target status
			Drawer::draw_text(debugX, debugY, "Targets:", textColor, 10.f, a_drawArgs);
			debugY += lineHeight;
			
			for (size_t i = 0; i < static_cast<size_t>(AmmoWheelReskinUnified::VisualTarget::COUNT); ++i) {
				std::string targetLine = fmt::format("  {}: {}",
					AmmoWheelReskinUnified::GetTargetName(static_cast<AmmoWheelReskinUnified::VisualTarget>(i)),
					debugInfo.targets[i].status);
				Drawer::draw_text(debugX, debugY, targetLine.c_str(), textColor, 9.f, a_drawArgs);
				debugY += lineHeight * 0.9f;
			}
		} else {
			Drawer::draw_text(debugX, debugY, "Hover a slot to see preset info", textColor, 10.f, a_drawArgs);
			debugY += lineHeight;
		}
		
		debugY += lineHeight * 0.5f;
		
		// Cache stats
		std::string statsLine = fmt::format("Cache: {} textures ({:.1f}MB) | {} flipbooks ({} frames)",
			debugInfo.texturesLoaded,
			static_cast<float>(debugInfo.textureBytes) / (1024.f * 1024.f),
			debugInfo.flipbooksLoaded,
			debugInfo.totalFrames);
		Drawer::draw_text(debugX, debugY, statsLine.c_str(), textColor, 10.f, a_drawArgs);
	}
}

void AmmoWheel::drawSlot(int a_index, ImVec2 a_center, bool a_hovered, float a_innerRadius, 
	float a_outerRadius, float a_startAngle, float a_endAngle, RE::FormID a_equippedAmmoID, DrawArgs a_drawArgs)
{
	if (a_index < 0 || a_index >= static_cast<int>(_ammoEntries.size())) {
		return;
	}

	auto& entry = _ammoEntries[a_index];
	if (!entry.ammo) {
		return;
	}

	// Apply slot gap (shrink arc by half the gap on each side)
	float slotGapRad = Config::AmmoWheel::SlotGapDeg * (IM_PI / 180.0f);
	float gapHalf = slotGapRad / 2.0f;
	float drawStartAngle = a_startAngle + gapHalf;
	float drawEndAngle = a_endAngle - gapHalf;
	
	// Ensure we don't invert the arc
	if (drawEndAngle <= drawStartAngle) {
		drawStartAngle = a_startAngle;
		drawEndAngle = a_endAngle;
	}

	// Calculate slot center for icon/text placement (use original angles for positioning)
	float midAngle = (a_startAngle + a_endAngle) / 2.0f;
	
	// Check if this ammo is currently equipped
	bool isEquipped = (entry.ammo->GetFormID() != 0 && entry.ammo->GetFormID() == a_equippedAmmoID);

	// Shared geometry used by both reskin and legacy paths
	float midRadius = (a_innerRadius + a_outerRadius) / 2.0f;
	ImVec2 slotCenter = ImVec2(
		a_center.x + midRadius * std::cos(midAngle),
		a_center.y + midRadius * std::sin(midAngle)
	);
	float slotWidth = (a_outerRadius - a_innerRadius) * Config::AmmoWheel::SlotShapeScale;
	float slotArcLength = midRadius * (drawEndAngle - drawStartAngle);
	float shapeCenterX = a_center.x + std::cos(midAngle) * midRadius;
	float shapeCenterY = a_center.y + std::sin(midAngle) * midRadius;

	const bool lowAmmoActive = Config::AmmoWheel::LowAmmoIndicatorEnabled &&
		entry.count > 0 && entry.count < Config::AmmoWheel::LowAmmoThreshold;
	const int lowAmmoLayer = Config::AmmoWheel::LowAmmoIndicatorDrawLayer;
	auto drawLowAmmoAtLayer = [&](int layer) {
		if (lowAmmoActive && lowAmmoLayer == layer) {
			drawLowAmmoWarning(a_center, a_innerRadius, a_outerRadius, midAngle, a_drawArgs);
		}
	};

	// Resolved preset for data-driven indicators (legacy preset system)
	const Config::AmmoWheel::StylePreset* preset = entry.resolvedPreset;
	bool usePresetStyling = Config::AmmoWheel::Skin::UsePresetStyles 
		&& preset != nullptr 
		&& preset->PresetId != "Default";

	// Reuse the standard indicator geometry in both reskin and legacy paths.
	auto drawStandardIndicators = [&](ImDrawList* indicatorList) {
		// ========== DATA-DRIVEN INDICATORS ==========
		// For arc shapes, use arc indicators. For non-arc shapes, use shape-appropriate indicators.
		if (Config::AmmoWheel::SlotShape == 0) {
			const bool fillSelectedIndicator = Config::AmmoWheel::UseSkyrimTheme;
			float selectedBlinkAlpha = 1.0f;
			if (Config::AmmoWheel::SelectedBlinkEnabled) {
				float time = static_cast<float>(ImGui::GetTime());
				float blinkPhase = std::sin(time * Config::AmmoWheel::SelectedBlinkSpeedHz * 2.0f * 3.14159f);
				float blinkT = 0.5f + 0.5f * blinkPhase;
				selectedBlinkAlpha = Config::AmmoWheel::SelectedBlinkMinAlpha + 
					(Config::AmmoWheel::SelectedBlinkMaxAlpha - Config::AmmoWheel::SelectedBlinkMinAlpha) * blinkT;
			}

			// Arc-based indicators (original behavior)
			if (usePresetStyling && preset) {
				const auto& selInd = preset->Selected;
				const auto& hovInd = preset->Hovered;
				
				if (isEquipped && Config::AmmoWheel::Skin::EnableSelectedIndicator && selInd.Enabled) {
					float radius = a_outerRadius + selInd.RadiusOffsetPx;
					float startOff = selInd.StartAngleOffsetDeg * (IM_PI / 180.0f);
					float sweepRad = (selInd.SweepDeg > 0.0f) 
						? selInd.SweepDeg * (IM_PI / 180.0f) 
						: (drawEndAngle - drawStartAngle);

					float indicatorAlpha = selInd.Alpha * selectedBlinkAlpha;
					ImU32 indicatorColor = Config::AmmoWheel::SelectedIndicatorColorComputed;
					if (fillSelectedIndicator) {
						const float fillAlphaScale = 0.35f;
						uint8_t fillAlpha = static_cast<uint8_t>((indicatorColor >> 24) * indicatorAlpha * fillAlphaScale * a_drawArgs.alphaMult);
						ImU32 fillColor = (indicatorColor & 0x00FFFFFF) | (fillAlpha << 24);
						
						float fillInner = a_innerRadius;
						float fillOuter = a_outerRadius;
						if (Config::AmmoWheel::SelectedIndicatorSizeScale != 1.0f) {
							float mid = (a_innerRadius + a_outerRadius) * 0.5f;
							float half = (a_outerRadius - a_innerRadius) * 0.5f * Config::AmmoWheel::SelectedIndicatorSizeScale;
							fillInner = mid - half;
							fillOuter = mid + half;
						}
						
						Drawer::draw_arc_gradient(a_center, fillInner, fillOuter,
							drawStartAngle + startOff, drawStartAngle + startOff + sweepRad,
							drawStartAngle + startOff, drawStartAngle + startOff + sweepRad,
							fillColor, fillColor, 32, a_drawArgs);
					} else {
						float thickness = Config::AmmoWheel::SelectedIndicatorThickness > 0.0f
							? Config::AmmoWheel::SelectedIndicatorThickness
							: selInd.ThicknessPx;
						
						DrawIndicatorArc(indicatorList, a_center, radius * Config::AmmoWheel::SelectedIndicatorSizeScale,
							drawStartAngle + startOff, drawStartAngle + startOff + sweepRad,
							thickness, indicatorColor, indicatorColor,
							indicatorAlpha, selInd.AnimMode, selInd.AnimSpeed, a_drawArgs.alphaMult);
					}
				}
				
				// TASK 3: Hover indicator - no blink, just show if hovered
				if (a_hovered && Config::AmmoWheel::Skin::EnableHoveredIndicator && hovInd.Enabled) {
					float radius = a_outerRadius + hovInd.RadiusOffsetPx;
					float startOff = hovInd.StartAngleOffsetDeg * (IM_PI / 180.0f);
					float sweepRad = (hovInd.SweepDeg > 0.0f) 
						? hovInd.SweepDeg * (IM_PI / 180.0f) 
						: (drawEndAngle - drawStartAngle);
					
					DrawIndicatorArc(indicatorList, a_center, radius,
						drawStartAngle + startOff, drawStartAngle + startOff + sweepRad,
						hovInd.ThicknessPx, hovInd.ColorBegin, hovInd.ColorEnd,
						hovInd.Alpha, hovInd.AnimMode, hovInd.AnimSpeed, a_drawArgs.alphaMult);
				}
			} else {
				using namespace Config::AmmoWheel::Skin;
				
				if (isEquipped && EnableSelectedIndicator && SelectedEnabled) {
					float radius = a_outerRadius + SelectedRadiusOffsetPx;
					float startOff = SelectedStartAngleOffsetDeg * (IM_PI / 180.0f);
					float sweepRad = (SelectedSweepDeg > 0.0f) 
						? SelectedSweepDeg * (IM_PI / 180.0f) 
						: (drawEndAngle - drawStartAngle);

					float indicatorAlpha = SelectedAlpha * selectedBlinkAlpha;
					ImU32 indicatorColor = Config::AmmoWheel::SelectedIndicatorColorComputed;
					if (fillSelectedIndicator) {
						const float fillAlphaScale = 0.35f;
						uint8_t fillAlpha = static_cast<uint8_t>((indicatorColor >> 24) * indicatorAlpha * fillAlphaScale * a_drawArgs.alphaMult);
						ImU32 fillColor = (indicatorColor & 0x00FFFFFF) | (fillAlpha << 24);
						
						float fillInner = a_innerRadius;
						float fillOuter = a_outerRadius;
						if (Config::AmmoWheel::SelectedIndicatorSizeScale != 1.0f) {
							float mid = (a_innerRadius + a_outerRadius) * 0.5f;
							float half = (a_outerRadius - a_innerRadius) * 0.5f * Config::AmmoWheel::SelectedIndicatorSizeScale;
							fillInner = mid - half;
							fillOuter = mid + half;
						}
						
						Drawer::draw_arc_gradient(a_center, fillInner, fillOuter,
							drawStartAngle + startOff, drawStartAngle + startOff + sweepRad,
							drawStartAngle + startOff, drawStartAngle + startOff + sweepRad,
							fillColor, fillColor, 32, a_drawArgs);
					} else {
						float thickness = Config::AmmoWheel::SelectedIndicatorThickness > 0.0f
							? Config::AmmoWheel::SelectedIndicatorThickness
							: SelectedThicknessPx;

						DrawIndicatorArc(indicatorList, a_center, radius * Config::AmmoWheel::SelectedIndicatorSizeScale,
							drawStartAngle + startOff, drawStartAngle + startOff + sweepRad,
							thickness, indicatorColor, indicatorColor,
							indicatorAlpha, SelectedAnimMode, SelectedAnimSpeed, a_drawArgs.alphaMult);
					}
				}
				
				if (a_hovered && EnableHoveredIndicator && HoveredEnabled) {
					float radius = a_outerRadius + HoveredRadiusOffsetPx;
					float startOff = HoveredStartAngleOffsetDeg * (IM_PI / 180.0f);
					float sweepRad = (HoveredSweepDeg > 0.0f) 
						? HoveredSweepDeg * (IM_PI / 180.0f) 
						: (drawEndAngle - drawStartAngle);
					
					DrawIndicatorArc(indicatorList, a_center, radius,
						drawStartAngle + startOff, drawStartAngle + startOff + sweepRad,
						HoveredThicknessPx, HoveredColorBeginInd, HoveredColorEndInd,
						HoveredAlpha, HoveredAnimMode, HoveredAnimSpeed, a_drawArgs.alphaMult);
				}
			}
		} else {
			// Shape-based indicators for non-arc slots (RoundedRect, Pill, Circle)
			// Draw a colored border around the shape to indicate selection/hover
			using namespace Config::AmmoWheel::Skin;
			
			// Get indicator colors and settings
			float selectedBlinkAlpha = 1.0f;
			if (Config::AmmoWheel::SelectedBlinkEnabled) {
				float time = static_cast<float>(ImGui::GetTime());
				float blinkPhase = std::sin(time * Config::AmmoWheel::SelectedBlinkSpeedHz * 2.0f * 3.14159f);
				float blinkT = 0.5f + 0.5f * blinkPhase;
				selectedBlinkAlpha = Config::AmmoWheel::SelectedBlinkMinAlpha + 
					(Config::AmmoWheel::SelectedBlinkMaxAlpha - Config::AmmoWheel::SelectedBlinkMinAlpha) * blinkT;
			}

			ImU32 selColor = Config::AmmoWheel::SelectedIndicatorColorComputed;
			float selThickness = Config::AmmoWheel::SelectedIndicatorThickness > 0.0f
				? Config::AmmoWheel::SelectedIndicatorThickness
				: (usePresetStyling && preset ? preset->Selected.ThicknessPx : SelectedThicknessPx);
			float selAlpha = (usePresetStyling && preset ? preset->Selected.Alpha : SelectedAlpha) * selectedBlinkAlpha;
			bool selEnabled = usePresetStyling && preset ? preset->Selected.Enabled : SelectedEnabled;
			
			// Apply alpha
			uint8_t selA = static_cast<uint8_t>(((selColor >> 24) & 0xFF) * selAlpha * a_drawArgs.alphaMult);
			selColor = (selColor & 0x00FFFFFF) | (selA << 24);
			
			// Selected indicator for non-arc shapes
			if (isEquipped && EnableSelectedIndicator && selEnabled) {
				float offset = 3.0f * Config::AmmoWheel::SelectedIndicatorSizeScale;  // Indicator offset from shape edge
				
				switch (Config::AmmoWheel::SlotShape) {
				case 1: // RoundedRect
				{
					float rectWidth = slotArcLength * 0.85f + offset * 2;
					float rectHeight = slotWidth + offset * 2;
					float cornerRadius = Config::AmmoWheel::SlotCornerRadius + offset;
					indicatorList->AddRect(
						ImVec2(shapeCenterX - rectWidth/2, shapeCenterY - rectHeight/2),
						ImVec2(shapeCenterX + rectWidth/2, shapeCenterY + rectHeight/2),
						selColor, cornerRadius, 0, selThickness
					);
				}
				break;
				case 2: // Pill
				{
					float pillLength = slotArcLength * 0.8f + offset * 2;
					float pillRadius = slotWidth / 2.0f + offset;
					indicatorList->AddRect(
						ImVec2(shapeCenterX - pillLength/2, shapeCenterY - pillRadius),
						ImVec2(shapeCenterX + pillLength/2, shapeCenterY + pillRadius),
						selColor, pillRadius, 0, selThickness
					);
				}
				break;
				case 3: // Circle
				{
					float circleRadius = (std::min)(slotWidth, slotArcLength * 0.5f) * 0.85f + offset;
					indicatorList->AddCircle(ImVec2(shapeCenterX, shapeCenterY), circleRadius, selColor, 24, selThickness);
				}
				break;
				}
			}
		}
	};
	
	// ========== UNIFIED RESKIN SYSTEM ==========
	// When enabled, this is the SINGLE source of truth for all visuals
	auto& reskinSystem = AmmoWheelReskinUnified::ReskinSystem::GetSingleton();
	if (reskinSystem.IsEnabled() && entry.reskinEntry.preset) {
		auto drawList = ImGui::GetWindowDrawList();
		
		// Build draw context for this slot
		AmmoWheelReskinUnified::DrawContext ctx;
		ctx.center = ImVec2(a_center.x + midRadius * std::cos(midAngle), a_center.y + midRadius * std::sin(midAngle));
		ctx.radius = (a_outerRadius - a_innerRadius) / 2.0f;
		ctx.slotAngleRad = midAngle;
		ctx.alphaMult = a_drawArgs.alphaMult;
		ctx.slotIndex = a_index;
		ctx.formID = entry.ammo->GetFormID();
		ctx.hovered = a_hovered;
		ctx.selected = isEquipped;
		
		// Get primitive fallback colors
		const auto& primitives = entry.reskinEntry.preset->primitives;
		
		// Draw slot background (try asset first, fallback to primitive)
		bool backgroundDrawn = reskinSystem.DrawTarget(AmmoWheelReskinUnified::VisualTarget::SlotBackground, entry.reskinEntry, ctx, drawList);
		// Draw slot frame early so indicators are always above background PNGs.
		reskinSystem.DrawTarget(AmmoWheelReskinUnified::VisualTarget::SlotFrame, entry.reskinEntry, ctx, drawList);
		
		if (!backgroundDrawn) {
			// Primitive fallback: respect SlotShape setting
			ImU32 colorBegin = a_hovered ? primitives.slotHoveredInner : primitives.slotUnhoveredInner;
			ImU32 colorEnd = a_hovered ? primitives.slotHoveredOuter : primitives.slotUnhoveredOuter;
			
			// ========== APPLY HOVER BRIGHTNESS (Config setting) ==========
			if (a_hovered && Config::AmmoWheel::HoverBrightnessEnabled) {
				float strength = Config::AmmoWheel::HoverBrightnessStrength;
				auto brighten = [strength](ImU32 color) -> ImU32 {
					ImVec4 f = ImGui::ColorConvertU32ToFloat4(color);
					f.x = (std::min)(f.x * strength, 1.0f);
					f.y = (std::min)(f.y * strength, 1.0f);
					f.z = (std::min)(f.z * strength, 1.0f);
					return ImGui::ColorConvertFloat4ToU32(f);
				};
				colorBegin = brighten(colorBegin);
				colorEnd = brighten(colorEnd);
			}
			
			// ========== APPLY SELECTED BLINK (Config setting) ==========
			if (isEquipped && Config::AmmoWheel::SelectedBlinkEnabled) {
				float time = static_cast<float>(ImGui::GetTime());
				float blinkPhase = std::sin(time * Config::AmmoWheel::SelectedBlinkSpeedHz * 2.0f * 3.14159f);
				float blinkT = 0.5f + 0.5f * blinkPhase;
				float blinkStrength = 1.0f + Config::AmmoWheel::SelectedSlotBlinkStrength * blinkT;
				
				auto brightenBlink = [blinkStrength](ImU32 color) -> ImU32 {
					ImVec4 f = ImGui::ColorConvertU32ToFloat4(color);
					f.x = (std::min)(f.x * blinkStrength, 1.0f);
					f.y = (std::min)(f.y * blinkStrength, 1.0f);
					f.z = (std::min)(f.z * blinkStrength, 1.0f);
					return ImGui::ColorConvertFloat4ToU32(f);
				};
				colorBegin = brightenBlink(colorBegin);
				colorEnd = brightenBlink(colorEnd);
			}
			
			// Convert to solid color for non-arc shapes
			ImVec4 solidF = ImGui::ColorConvertU32ToFloat4(colorBegin);
			solidF.w *= a_drawArgs.alphaMult;
			ImU32 solidColor = ImGui::ColorConvertFloat4ToU32(solidF);
			
			switch (Config::AmmoWheel::SlotShape) {
			case 0: // Arc (Default)
				Drawer::draw_arc_gradient(a_center, a_innerRadius, a_outerRadius,
					drawStartAngle, drawEndAngle, drawStartAngle, drawEndAngle,
					colorBegin, colorEnd, 32, a_drawArgs);
				break;
			case 1: // Rounded Rectangle
			{
				float rectWidth = slotArcLength * 0.85f;
				float rectHeight = slotWidth;
				float cornerRadius = Config::AmmoWheel::SlotCornerRadius;
				drawList->AddRectFilled(
					ImVec2(shapeCenterX - rectWidth/2, shapeCenterY - rectHeight/2),
					ImVec2(shapeCenterX + rectWidth/2, shapeCenterY + rectHeight/2),
					solidColor, cornerRadius
				);
			}
			break;
			case 2: // Pill (Capsule)
			{
				float pillRadius = slotWidth / 2.0f;
				float pillLength = slotArcLength * 0.7f;
				drawList->AddRectFilled(
					ImVec2(shapeCenterX - pillLength/2, shapeCenterY - pillRadius),
					ImVec2(shapeCenterX + pillLength/2, shapeCenterY + pillRadius),
					solidColor, pillRadius
				);
			}
			break;
			case 3: // Circle
			{
				float circleRadius = (std::min)(slotWidth, slotArcLength * 0.5f) * 0.85f;
				drawList->AddCircleFilled(ImVec2(shapeCenterX, shapeCenterY), circleRadius, solidColor, 24);
			}
			break;
			}
		}
		
		// Highlight overlay for PNG backgrounds (hover/selected tint)
		if (backgroundDrawn && (a_hovered || isEquipped)) {
			ImU32 highlightBegin;
			ImU32 highlightEnd;
			
			// Match legacy slot color selection logic
			if (usePresetStyling && preset) {
				if (isEquipped) {
					highlightBegin = preset->SelectedColorBegin;
					highlightEnd = preset->SelectedColorEnd;
				} else if (a_hovered) {
					highlightBegin = preset->HoveredColorBegin;
					highlightEnd = preset->HoveredColorEnd;
				} else {
					highlightBegin = preset->UnhoveredColorBegin;
					highlightEnd = preset->UnhoveredColorEnd;
				}
			} else if (Config::AmmoWheel::UseSkyrimTheme) {
				using namespace Config::AmmoWheel::SkyrimTheme;
				highlightBegin = a_hovered ? SlotHoveredInner : SlotUnhoveredInner;
				highlightEnd = a_hovered ? SlotHoveredOuter : SlotUnhoveredOuter;
			} else if (Config::AmmoWheel::UseMainWheelTheme) {
				using namespace Config::Styling::Wheel;
				highlightBegin = a_hovered ? HoveredColorBegin : UnhoveredColorBegin;
				highlightEnd = a_hovered ? HoveredColorEnd : UnhoveredColorEnd;
			} else {
				highlightBegin = a_hovered ? Config::AmmoWheel::HoveredColorBegin : Config::AmmoWheel::UnhoveredColorBegin;
				highlightEnd = a_hovered ? Config::AmmoWheel::HoveredColorEnd : Config::AmmoWheel::UnhoveredColorEnd;
			}
			
			auto brighten = [](ImU32 color, float strength) -> ImU32 {
				ImVec4 f = ImGui::ColorConvertU32ToFloat4(color);
				f.x = (std::min)(f.x * strength, 1.0f);
				f.y = (std::min)(f.y * strength, 1.0f);
				f.z = (std::min)(f.z * strength, 1.0f);
				return ImGui::ColorConvertFloat4ToU32(f);
			};
			
			if (a_hovered && Config::AmmoWheel::HoverBrightnessEnabled) {
				float strength = Config::AmmoWheel::HoverBrightnessStrength;
				highlightBegin = brighten(highlightBegin, strength);
				highlightEnd = brighten(highlightEnd, strength);
			}
			
			if (isEquipped && Config::AmmoWheel::SelectedBlinkEnabled) {
				float time = static_cast<float>(ImGui::GetTime());
				float blinkPhase = std::sin(time * Config::AmmoWheel::SelectedBlinkSpeedHz * 2.0f * 3.14159f);
				float blinkT = 0.5f + 0.5f * blinkPhase;
				float blinkStrength = 1.0f + Config::AmmoWheel::SelectedSlotBlinkStrength * blinkT;
				highlightBegin = brighten(highlightBegin, blinkStrength);
				highlightEnd = brighten(highlightEnd, blinkStrength);
			}
			
			const float overlayAlphaScale = 0.35f;
			ImVec4 beginF = ImGui::ColorConvertU32ToFloat4(highlightBegin);
			ImVec4 endF = ImGui::ColorConvertU32ToFloat4(highlightEnd);
			beginF.w *= overlayAlphaScale;
			endF.w *= overlayAlphaScale;
			
			ImU32 overlayBegin = ImGui::ColorConvertFloat4ToU32(beginF);
			ImU32 overlayEnd = ImGui::ColorConvertFloat4ToU32(endF);
			
			ImVec4 solidF{
				static_cast<float>((beginF.x + endF.x) * 0.5f),
				static_cast<float>((beginF.y + endF.y) * 0.5f),
				static_cast<float>((beginF.z + endF.z) * 0.5f),
				static_cast<float>((beginF.w + endF.w) * 0.5f * a_drawArgs.alphaMult)
			};
			ImU32 overlaySolid = ImGui::ColorConvertFloat4ToU32(solidF);
			
			switch (Config::AmmoWheel::SlotShape) {
			case 0: // Arc (Default)
				Drawer::draw_arc_gradient(a_center, a_innerRadius, a_outerRadius,
					drawStartAngle, drawEndAngle, drawStartAngle, drawEndAngle,
					overlayBegin, overlayEnd, 32, a_drawArgs);
				break;
			case 1: // Rounded Rectangle
			{
				float rectWidth = slotArcLength * 0.85f;
				float rectHeight = slotWidth;
				float cornerRadius = Config::AmmoWheel::SlotCornerRadius;
				drawList->AddRectFilled(
					ImVec2(shapeCenterX - rectWidth/2, shapeCenterY - rectHeight/2),
					ImVec2(shapeCenterX + rectWidth/2, shapeCenterY + rectHeight/2),
					overlaySolid, cornerRadius
				);
			}
			break;
			case 2: // Pill (Capsule)
			{
				float pillRadius = slotWidth / 2.0f;
				float pillLength = slotArcLength * 0.7f;
				drawList->AddRectFilled(
					ImVec2(shapeCenterX - pillLength/2, shapeCenterY - pillRadius),
					ImVec2(shapeCenterX + pillLength/2, shapeCenterY + pillRadius),
					overlaySolid, pillRadius
				);
			}
			break;
			case 3: // Circle
			{
				float circleRadius = (std::min)(slotWidth, slotArcLength * 0.5f) * 0.85f;
				drawList->AddCircleFilled(ImVec2(shapeCenterX, shapeCenterY), circleRadius, overlaySolid, 24);
			}
			break;
			}
		}
		
		// ========== DRAW INDICATORS ON TOP OF BACKGROUND ==========
		// Hybrid pipeline: PNG background + standard geometry indicators + icon/text.
		drawStandardIndicators(drawList);
		drawLowAmmoAtLayer(0);
		
		// Draw slot icon (try asset first, fallback to legacy icon system)
		ImVec2 iconCenter = ImVec2(a_center.x + _cachedIconRadius * std::cos(midAngle),
			a_center.y + _cachedIconRadius * std::sin(midAngle));
		ctx.center = iconCenter;
		ctx.radius = _cachedIconSize / 2.0f;
		
		if (!reskinSystem.DrawTarget(AmmoWheelReskinUnified::VisualTarget::SlotIcon, entry.reskinEntry, ctx, drawList)) {
			// Fallback to legacy icon rendering
			if (entry.iconImage.texture) {
				float iconSize = _cachedIconSize;
				ImU32 iconTint = IM_COL32(255, 255, 255, static_cast<int>(255 * a_drawArgs.alphaMult));
				
				// Rotate icon with slot by default
				float iconRotation = midAngle + IM_PI / 2.0f;  // Add 90 degrees to point outward
				
				DrawRotatedTexture(drawList, entry.iconImage.texture, iconCenter,
					iconSize, iconSize, iconRotation, iconTint);
			}
		}
		drawLowAmmoAtLayer(1);
		
		// Slot frame already drawn early for correct indicator layering.
		
		// Draw popup overlay if hovered
		if (a_hovered) {
			reskinSystem.DrawTarget(AmmoWheelReskinUnified::VisualTarget::Popup, entry.reskinEntry, ctx, drawList);
		}
		
		// Text rendering - use label layout system even with reskin enabled
		if (Config::AmmoWheel::LabelShow) {
			// Use the same label layout system as legacy path
			float baseFontSize = Config::AmmoWheel::NameFontPx * Config::AmmoWheel::NameTextScale;
			float textSize = baseFontSize;
			const char* originalName = entry.ammo->GetName();
			
			// Apply label truncation if enabled
			std::string displayName = originalName;
			if (Config::AmmoWheel::LabelTruncateLength > 0 && strlen(originalName) > static_cast<size_t>(Config::AmmoWheel::LabelTruncateLength)) {
				if (Config::AmmoWheel::LabelAbbreviate) {
					// Simple abbreviation - just truncate and add "..."
					displayName = std::string(originalName).substr(0, Config::AmmoWheel::LabelTruncateLength - 3) + "...";
				} else {
					displayName = std::string(originalName).substr(0, Config::AmmoWheel::LabelTruncateLength);
				}
			}
			
			// ========== AUTO-WIDTH COMPUTATION ==========
			float availWidth = 0.0f;
			float margin = Config::AmmoWheel::NameMarginPx;
			float padding = Config::AmmoWheel::NamePanelPaddingPx;
			
			// Determine label side based on wheel position
			ImVec2 viewportSize = ResolutionScale::Context::GetSingleton().GetRenderSize();
			bool labelOnLeft = (a_center.x > viewportSize.x * 0.5f);
			
			if (Config::AmmoWheel::NameLayoutMode > 0) {
				// New layout modes: compute available width from screen space
				if (labelOnLeft) {
					availWidth = a_center.x - margin - padding;
				} else {
					availWidth = viewportSize.x - a_center.x - margin - padding;
				}
				
				// Apply max width cap if configured
				if (Config::AmmoWheel::NameMaxWidthPx > 0.0f) {
					availWidth = (std::min)(availWidth, Config::AmmoWheel::NameMaxWidthPx);
				}
				
				// Ensure minimum width
				availWidth = (std::max)(availWidth, 60.0f);
			} else {
				// Legacy mode: use slot arc-based width
				float slotArcLength = (a_endAngle - a_startAngle) * _cachedTextRadius;
				availWidth = slotArcLength * Config::AmmoWheel::LabelMaxSlotArcRatio;
				availWidth = (std::max)(availWidth, 60.0f);
			}
			
			// ========== LAYOUT MODE PROCESSING ==========
			TextLayout layout;
			int maxLines = Config::AmmoWheel::NameMaxLines;
			
			switch (Config::AmmoWheel::NameLayoutMode) {
				case 0: // LegacyEllipsis - original behavior
				{
					// Calculate normalized slot position for legacy multi-line logic
					float normalizedU = std::cos(midAngle);
					float absU = std::abs(normalizedU);
					
					int legacyMaxLines = 1;
					if (Config::AmmoWheel::LabelMultiLine) {
						if (absU >= 0.70f) legacyMaxLines = 3;
						else if (absU >= 0.35f) legacyMaxLines = 2;
					}
					
					layout = wrapTextForSlot(displayName.c_str(), availWidth, textSize, legacyMaxLines);
				}
				break;
				case 1: // Wrap
					layout = wrapTextForSlot(displayName.c_str(), availWidth, textSize, maxLines);
					break;
				case 2: // ShrinkToFit
				{
					ImFont* font = ImGui::GetFont();
					if (font) {
						ImVec2 size = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, displayName.c_str());
						float minFont = Config::AmmoWheel::NameMinFontPx;
						
						while (size.x > availWidth && textSize > minFont) {
							textSize -= 1.0f;
							size = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, displayName.c_str());
						}
					}
					layout.lines.push_back(displayName);
				}
				break;
				case 3: // Hybrid
				{
					// Try wrapping first
					layout = wrapTextForSlot(displayName.c_str(), availWidth, textSize, maxLines);
					
					// If last line still too wide, try shrinking font
					if (!layout.lines.empty()) {
						ImFont* font = ImGui::GetFont();
						if (font) {
							ImVec2 lastLineSize = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, layout.lines.back().c_str());
							
							if (lastLineSize.x > availWidth) {
								// Try shrinking font
								float minFont = Config::AmmoWheel::NameMinFontPx;
								float trySize = textSize;
								
								while (trySize > minFont) {
									trySize -= 1.0f;
									ImVec2 testSize = font->CalcTextSizeA(trySize, FLT_MAX, 0.0f, layout.lines.back().c_str());
									if (testSize.x <= availWidth) {
										textSize = trySize;
										break;
									}
								}
							}
						}
					}
				}
				break;
			}
			
			// ========== POSITION CALCULATION ==========
			ImVec2 textCenter = ImVec2(a_center.x + _cachedTextRadius * std::cos(midAngle),
				a_center.y + _cachedTextRadius * std::sin(midAngle));
			float textX = textCenter.x;
			float textY = textCenter.y;
			float lineSpacing = textSize + Config::AmmoWheel::NameLineSpacingPx;
			float totalTextHeight = layout.lines.size() * lineSpacing;
			
			if (Config::AmmoWheel::ShowAmmoCount) {
				textY -= totalTextHeight * 0.5f + Config::AmmoWheel::CountFontSize * 0.5f;
			} else {
				textY -= totalTextHeight * 0.5f;
			}
			
			// ========== CLIP RECT CALCULATION ==========
			float clipHalfWidth, clipHalfHeight;
			if (Config::AmmoWheel::NameLayoutMode > 0) {
				// New modes: use full available width for clipping
				clipHalfWidth = availWidth * 0.55f;
				clipHalfHeight = totalTextHeight + padding;
			} else {
				// Legacy mode: use slot-based clipping
				clipHalfWidth = availWidth * 0.5f;
				clipHalfHeight = totalTextHeight * 0.5f;
			}
			
			ImVec2 clipMin(textCenter.x - clipHalfWidth, textY - clipHalfHeight);
			ImVec2 clipMax(textCenter.x + clipHalfWidth, textY + totalTextHeight + clipHalfHeight);
			
			ImGui::GetWindowDrawList()->PushClipRect(clipMin, clipMax, true);
			
			// ========== TEXT BACKGROUND PANEL ==========
			if (Config::AmmoWheel::NameTextBgEnabled && !layout.lines.empty()) {
				// Calculate text block width bounded by availWidth and NameMaxWidthPx
				float maxLineWidth = 0.0f;
				ImFont* font = ImGui::GetFont();
				if (font) {
					for (const auto& line : layout.lines) {
						ImVec2 size = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, line.c_str());
						maxLineWidth = (std::max)(maxLineWidth, size.x);
					}
				}
				float bgWidth = maxLineWidth + Config::AmmoWheel::NameTextBgExtraPaddingPx * 2.0f + padding * 2.0f;
				// Respect NameMaxWidthPx clamp if provided
				if (Config::AmmoWheel::NameMaxWidthPx > 0.0f) {
					bgWidth = (std::min)(bgWidth, Config::AmmoWheel::NameMaxWidthPx);
				}
				// Also cap by computed availWidth (screen-based)
				bgWidth = (std::min)(bgWidth, availWidth + padding * 2.0f);
				float bgHalfWidth = bgWidth * 0.5f;
				
				float bgPadding = Config::AmmoWheel::NameTextBgExtraPaddingPx + padding;
				float bgInset = Config::AmmoWheel::NameTextBgInsetPx;
				
				float bgLeft = textX - bgHalfWidth - bgInset;
				float bgRight = textX + bgHalfWidth + bgInset;
				float bgTop = textY - bgPadding;
				float bgBottom = textY + totalTextHeight + bgPadding;
				
				// Calculate background color with opacity (use configured dark color)
				ImU32 bgColor = Config::AmmoWheel::NameTextBgColor;
				uint8_t bgAlpha = static_cast<uint8_t>((bgColor >> 24) * Config::AmmoWheel::NameTextBgOpacity * a_drawArgs.alphaMult);
				bgColor = (bgColor & 0x00FFFFFF) | (bgAlpha << 24);
				
				ImGui::GetWindowDrawList()->AddRectFilled(
					ImVec2(bgLeft, bgTop),
					ImVec2(bgRight, bgBottom),
					bgColor,
					Config::AmmoWheel::NameTextBgCornerRounding
				);
			}
			
			// ========== TEXT RENDERING ==========
			// Use color override if enabled, otherwise fall back to primitive color
			ImU32 textColor;
			if (a_hovered && Config::AmmoWheel::ArrowLabelColorOverrideEnabled) {
				textColor = Config::AmmoWheel::ArrowLabelColorComputed;
			} else if (Config::AmmoWheel::SlotLabelColorOverrideEnabled) {
				textColor = Config::AmmoWheel::SlotLabelColorComputed;
			} else {
				textColor = primitives.textPrimary;
			}
			uint8_t textAlpha = static_cast<uint32_t>((textColor >> 24) * a_drawArgs.alphaMult);
			textColor = (textColor & 0x00FFFFFF) | (textAlpha << 24);
			
			// Draw each line
			for (size_t i = 0; i < layout.lines.size(); i++) {
				float lineY = textY + i * lineSpacing;
				
				// Text shadow (Skyrim-style)
				if (Config::AmmoWheel::TextShadowEnabled) {
					ImU32 shadowColor = primitives.textShadow;
					uint8_t shadowAlpha = static_cast<uint32_t>((shadowColor >> 24) * a_drawArgs.alphaMult);
					shadowColor = (shadowColor & 0x00FFFFFF) | (shadowAlpha << 24);
					
					float shadowOffset = Config::AmmoWheel::TextShadowOffset;
					Drawer::draw_text(textX + shadowOffset, lineY + shadowOffset, layout.lines[i].c_str(), shadowColor, textSize, a_drawArgs);
				}
				
				// Main text
				Drawer::draw_text(textX, lineY, layout.lines[i].c_str(), textColor, textSize, a_drawArgs);
			}
			
			ImGui::GetWindowDrawList()->PopClipRect();
		}
		
		// Draw ammo count
		if (Config::AmmoWheel::ShowAmmoCount) {
			ImVec2 countCenter = ImVec2(a_center.x + _cachedCountRadius * std::cos(midAngle),
				a_center.y + _cachedCountRadius * std::sin(midAngle));
			drawAmmoCount(countCenter, entry.count, a_drawArgs);
		}

		drawLowAmmoAtLayer(2);
		
		return;  // Skip legacy rendering path
	}
	// ========== END UNIFIED RESKIN SYSTEM ==========
	
	// Use cached radii with offsets for text and icon positioning
	ImVec2 textCenter = ImVec2(
		a_center.x + _cachedTextRadius * std::cos(midAngle),
		a_center.y + _cachedTextRadius * std::sin(midAngle)
	);
	ImVec2 iconCenter = ImVec2(
		a_center.x + _cachedIconRadius * std::cos(midAngle),
		a_center.y + _cachedIconRadius * std::sin(midAngle)
	);

	// NOTE: isEquipped already declared above for unified reskin system

	// Select colors based on preset or theme settings
	ImU32 colorBegin, colorEnd, activeArcBegin, activeArcEnd;
	float activeArcWidth;
	
	// Priority: Preset (if enabled) > Skyrim Theme > Main Wheel Theme > AmmoWheel Theme
	if (usePresetStyling) {
		// Use preset colors (only when UsePresetStyles=ON and non-default preset matched)
		if (isEquipped) {
			colorBegin = preset->SelectedColorBegin;
			colorEnd = preset->SelectedColorEnd;
		} else if (a_hovered) {
			colorBegin = preset->HoveredColorBegin;
			colorEnd = preset->HoveredColorEnd;
		} else {
			colorBegin = preset->UnhoveredColorBegin;
			colorEnd = preset->UnhoveredColorEnd;
		}
		activeArcBegin = preset->Selected.ColorBegin;
		activeArcEnd = preset->Selected.ColorEnd;
		activeArcWidth = preset->Selected.ThicknessPx;
	} else if (Config::AmmoWheel::UseSkyrimTheme) {
		// Skyrim theme: parchment-like slot colors
		using namespace Config::AmmoWheel::SkyrimTheme;
		colorBegin = a_hovered ? SlotHoveredInner : SlotUnhoveredInner;
		colorEnd = a_hovered ? SlotHoveredOuter : SlotUnhoveredOuter;
		activeArcBegin = ActiveArcInner;
		activeArcEnd = ActiveArcOuter;
		activeArcWidth = 8.0f;
	} else if (Config::AmmoWheel::UseMainWheelTheme) {
		// Use main wheel styling colors
		using namespace Config::Styling::Wheel;
		colorBegin = a_hovered ? HoveredColorBegin : UnhoveredColorBegin;
		colorEnd = a_hovered ? HoveredColorEnd : UnhoveredColorEnd;
		activeArcBegin = ActiveArcColorBegin;
		activeArcEnd = ActiveArcColorEnd;
		activeArcWidth = ActiveArcWidth;
	} else {
		// Use AmmoWheel-specific theme (blue theme)
		colorBegin = a_hovered ? Config::AmmoWheel::HoveredColorBegin : Config::AmmoWheel::UnhoveredColorBegin;
		colorEnd = a_hovered ? Config::AmmoWheel::HoveredColorEnd : Config::AmmoWheel::UnhoveredColorEnd;
		activeArcBegin = Config::AmmoWheel::ActiveArcColorBegin;
		activeArcEnd = Config::AmmoWheel::ActiveArcColorEnd;
		activeArcWidth = 8.0f;  // Default width for AmmoWheel theme
	}

	// ========== TASK 3: HOVER BRIGHTNESS + SELECTED BLINK ==========
	// Apply hover brightness (no blink, just brighten)
	if (a_hovered && Config::AmmoWheel::HoverBrightnessEnabled) {
		float strength = Config::AmmoWheel::HoverBrightnessStrength;
		ImU32 colorBefore = colorBegin;  // Debug
		auto brighten = [strength](ImU32 color) -> ImU32 {
			ImVec4 f = ImGui::ColorConvertU32ToFloat4(color);
			f.x = (std::min)(f.x * strength, 1.0f);
			f.y = (std::min)(f.y * strength, 1.0f);
			f.z = (std::min)(f.z * strength, 1.0f);
			return ImGui::ColorConvertFloat4ToU32(f);
		};
		colorBegin = brighten(colorBegin);
		colorEnd = brighten(colorEnd);
		
		// Debug: log once per session when hover brightness is applied
		static bool loggedHoverBrightness = false;
		if (!loggedHoverBrightness) {
			logger::info("[AmmoWheel] HoverBrightness APPLIED: strength={:.2f}, colorBefore=0x{:08X}, colorAfter=0x{:08X}",
				strength, colorBefore, colorBegin);
			loggedHoverBrightness = true;
		}
	}
	
	// Apply selected slot blink (brightness pulse)
	if (isEquipped && Config::AmmoWheel::SelectedBlinkEnabled) {
		float time = static_cast<float>(ImGui::GetTime());
		float blinkPhase = std::sin(time * Config::AmmoWheel::SelectedBlinkSpeedHz * 2.0f * 3.14159f);
		float blinkT = 0.5f + 0.5f * blinkPhase;  // 0 to 1
		float blinkStrength = 1.0f + Config::AmmoWheel::SelectedSlotBlinkStrength * blinkT;
		
		auto brightenBlink = [blinkStrength](ImU32 color) -> ImU32 {
			ImVec4 f = ImGui::ColorConvertU32ToFloat4(color);
			f.x = (std::min)(f.x * blinkStrength, 1.0f);
			f.y = (std::min)(f.y * blinkStrength, 1.0f);
			f.z = (std::min)(f.z * blinkStrength, 1.0f);
			return ImGui::ColorConvertFloat4ToU32(f);
		};
		colorBegin = brightenBlink(colorBegin);
		colorEnd = brightenBlink(colorEnd);
		
		// Debug: log once per session when selected blink is applied
		static bool loggedSelectedBlink = false;
		if (!loggedSelectedBlink) {
			logger::info("[AmmoWheel] SelectedBlink APPLIED: speed={:.1f}Hz, blinkStrength={:.2f}",
				Config::AmmoWheel::SelectedBlinkSpeedHz, Config::AmmoWheel::SelectedSlotBlinkStrength);
			loggedSelectedBlink = true;
		}
	}
	
	// ========== SLOT SHAPE RENDERING ==========
	// Calculate solid color for non-arc shapes.
	// NOTE: ImU32 is stored as ImGui's internal packed format (ABGR). Avoid manual bit shifts.
	// Use colorBegin as the canonical fill color (the arc path uses begin/end as a gradient).
	ImVec4 solidF = ImGui::ColorConvertU32ToFloat4(colorBegin);
	solidF.w *= a_drawArgs.alphaMult;
	ImU32 solidColor = ImGui::ColorConvertFloat4ToU32(solidF);
	
	auto drawList = ImGui::GetWindowDrawList();
	
	switch (Config::AmmoWheel::SlotShape) {
	case 0: // Arc (Default) - existing behavior
	{
		// Shadow
		if (Config::AmmoWheel::SlotShadowEnabled) {
			ImVec2 shadowCenter = ImVec2(
				a_center.x + Config::AmmoWheel::SlotShadowOffsetX,
				a_center.y + Config::AmmoWheel::SlotShadowOffsetY
			);
			uint8_t shadowAlpha = static_cast<uint8_t>(Config::AmmoWheel::SlotShadowAlpha * a_drawArgs.alphaMult);
			ImU32 shadowColor = IM_COL32(0, 0, 0, shadowAlpha);
			
			Drawer::draw_arc_gradient(
				shadowCenter,
				a_innerRadius - 1.f,
				a_outerRadius + 1.f,
				drawStartAngle,
				drawEndAngle,
				drawStartAngle,
				drawEndAngle,
				shadowColor,
				shadowColor,
				32,
				a_drawArgs
			);
		}
		
		// Background
		Drawer::draw_arc_gradient(
			a_center,
			a_innerRadius,
			a_outerRadius,
			drawStartAngle,
			drawEndAngle,
			drawStartAngle,
			drawEndAngle,
			colorBegin,
			colorEnd,
			32,
			a_drawArgs
		);
		
		// Highlight
		if (a_hovered && Config::AmmoWheel::SlotHighlightEnabled) {
			uint8_t highlightAlpha = static_cast<uint8_t>(Config::AmmoWheel::SlotHighlightAlpha * a_drawArgs.alphaMult);
			ImU32 highlightColor = IM_COL32(255, 255, 255, highlightAlpha);
			float thickness = Config::AmmoWheel::SlotHighlightThickness;
			
			Drawer::draw_arc(
				a_center,
				a_innerRadius,
				a_innerRadius + thickness,
				drawStartAngle,
				drawEndAngle,
				drawStartAngle,
				drawEndAngle,
				highlightColor,
				32,
				a_drawArgs
			);
		}
		
		// Hover pulse
		if (a_hovered && Config::AmmoWheel::HoverPulseEnabled) {
			float pulseTime = static_cast<float>(ImGui::GetTime()) * Config::AmmoWheel::HoverPulseSpeed;
			float pulseFactor = 0.5f + 0.5f * std::sin(pulseTime);
			float pulseSize = Config::AmmoWheel::HoverPulseSize * pulseFactor;
			
			ImU32 pulseColor = Config::AmmoWheel::HoverPulseColor;
			uint8_t pulseAlpha = static_cast<uint8_t>((pulseColor >> 24) * pulseFactor * a_drawArgs.alphaMult);
			pulseColor = (pulseColor & 0x00FFFFFF) | (pulseAlpha << 24);
			
			Drawer::draw_arc(
				a_center,
				a_outerRadius,
				a_outerRadius + pulseSize,
				drawStartAngle,
				drawEndAngle,
				drawStartAngle,
				drawEndAngle,
				pulseColor,
				32,
				a_drawArgs
			);
		}
	}
	break;
	
	case 1: // Rounded Rectangle
	{
		float rectWidth = slotArcLength * 0.85f;
		float rectHeight = slotWidth;
		float cornerRadius = Config::AmmoWheel::SlotCornerRadius;
		
		// Rotate rectangle to align with radial direction
		float cosA = std::cos(midAngle);
		float sinA = std::sin(midAngle);
		
		// Shadow
		if (Config::AmmoWheel::SlotShadowEnabled) {
			uint8_t shadowAlpha = static_cast<uint8_t>(Config::AmmoWheel::SlotShadowAlpha * a_drawArgs.alphaMult);
			ImU32 shadowColor = IM_COL32(0, 0, 0, shadowAlpha);
			float sx = shapeCenterX + Config::AmmoWheel::SlotShadowOffsetX;
			float sy = shapeCenterY + Config::AmmoWheel::SlotShadowOffsetY;
			drawList->AddRectFilled(
				ImVec2(sx - rectWidth/2, sy - rectHeight/2),
				ImVec2(sx + rectWidth/2, sy + rectHeight/2),
				shadowColor, cornerRadius
			);
		}
		
		// Background
		drawList->AddRectFilled(
			ImVec2(shapeCenterX - rectWidth/2, shapeCenterY - rectHeight/2),
			ImVec2(shapeCenterX + rectWidth/2, shapeCenterY + rectHeight/2),
			solidColor, cornerRadius
		);
		
		// Highlight border
		if (a_hovered && Config::AmmoWheel::SlotHighlightEnabled) {
			uint8_t highlightAlpha = static_cast<uint8_t>(Config::AmmoWheel::SlotHighlightAlpha * a_drawArgs.alphaMult);
			ImU32 highlightColor = IM_COL32(255, 255, 255, highlightAlpha);
			drawList->AddRect(
				ImVec2(shapeCenterX - rectWidth/2, shapeCenterY - rectHeight/2),
				ImVec2(shapeCenterX + rectWidth/2, shapeCenterY + rectHeight/2),
				highlightColor, cornerRadius, 0, Config::AmmoWheel::SlotHighlightThickness
			);
		}
		
		// Hover pulse (expanding rect)
		if (a_hovered && Config::AmmoWheel::HoverPulseEnabled) {
			float pulseTime = static_cast<float>(ImGui::GetTime()) * Config::AmmoWheel::HoverPulseSpeed;
			float pulseFactor = 0.5f + 0.5f * std::sin(pulseTime);
			float pulseExpand = Config::AmmoWheel::HoverPulseSize * pulseFactor;
			
			ImU32 pulseColor = Config::AmmoWheel::HoverPulseColor;
			uint8_t pulseAlpha = static_cast<uint8_t>((pulseColor >> 24) * pulseFactor * a_drawArgs.alphaMult);
			pulseColor = (pulseColor & 0x00FFFFFF) | (pulseAlpha << 24);
			
			drawList->AddRect(
				ImVec2(shapeCenterX - rectWidth/2 - pulseExpand, shapeCenterY - rectHeight/2 - pulseExpand),
				ImVec2(shapeCenterX + rectWidth/2 + pulseExpand, shapeCenterY + rectHeight/2 + pulseExpand),
				pulseColor, cornerRadius + pulseExpand * 0.5f, 0, 2.0f
			);
		}
	}
	break;
	
	case 2: // Pill (Capsule)
	{
		float pillLength = slotArcLength * 0.8f;
		float pillRadius = slotWidth / 2.0f;
		
		// Shadow
		if (Config::AmmoWheel::SlotShadowEnabled) {
			uint8_t shadowAlpha = static_cast<uint8_t>(Config::AmmoWheel::SlotShadowAlpha * a_drawArgs.alphaMult);
			ImU32 shadowColor = IM_COL32(0, 0, 0, shadowAlpha);
			float sx = shapeCenterX + Config::AmmoWheel::SlotShadowOffsetX;
			float sy = shapeCenterY + Config::AmmoWheel::SlotShadowOffsetY;
			drawList->AddRectFilled(
				ImVec2(sx - pillLength/2, sy - pillRadius),
				ImVec2(sx + pillLength/2, sy + pillRadius),
				shadowColor, pillRadius  // Full rounding = pill shape
			);
		}
		
		// Background
		drawList->AddRectFilled(
			ImVec2(shapeCenterX - pillLength/2, shapeCenterY - pillRadius),
			ImVec2(shapeCenterX + pillLength/2, shapeCenterY + pillRadius),
			solidColor, pillRadius
		);
		
		// Highlight border
		if (a_hovered && Config::AmmoWheel::SlotHighlightEnabled) {
			uint8_t highlightAlpha = static_cast<uint8_t>(Config::AmmoWheel::SlotHighlightAlpha * a_drawArgs.alphaMult);
			ImU32 highlightColor = IM_COL32(255, 255, 255, highlightAlpha);
			drawList->AddRect(
				ImVec2(shapeCenterX - pillLength/2, shapeCenterY - pillRadius),
				ImVec2(shapeCenterX + pillLength/2, shapeCenterY + pillRadius),
				highlightColor, pillRadius, 0, Config::AmmoWheel::SlotHighlightThickness
			);
		}
		
		// Hover pulse
		if (a_hovered && Config::AmmoWheel::HoverPulseEnabled) {
			float pulseTime = static_cast<float>(ImGui::GetTime()) * Config::AmmoWheel::HoverPulseSpeed;
			float pulseFactor = 0.5f + 0.5f * std::sin(pulseTime);
			float pulseExpand = Config::AmmoWheel::HoverPulseSize * pulseFactor;
			
			ImU32 pulseColor = Config::AmmoWheel::HoverPulseColor;
			uint8_t pulseAlpha = static_cast<uint8_t>((pulseColor >> 24) * pulseFactor * a_drawArgs.alphaMult);
			pulseColor = (pulseColor & 0x00FFFFFF) | (pulseAlpha << 24);
			
			drawList->AddRect(
				ImVec2(shapeCenterX - pillLength/2 - pulseExpand, shapeCenterY - pillRadius - pulseExpand),
				ImVec2(shapeCenterX + pillLength/2 + pulseExpand, shapeCenterY + pillRadius + pulseExpand),
				pulseColor, pillRadius + pulseExpand, 0, 2.0f
			);
		}
	}
	break;
	
	case 3: // Circle
	{
		float circleRadius = (std::min)(slotWidth, slotArcLength * 0.5f) * 0.85f;
		
		// Shadow
		if (Config::AmmoWheel::SlotShadowEnabled) {
			uint8_t shadowAlpha = static_cast<uint8_t>(Config::AmmoWheel::SlotShadowAlpha * a_drawArgs.alphaMult);
			ImU32 shadowColor = IM_COL32(0, 0, 0, shadowAlpha);
			float sx = shapeCenterX + Config::AmmoWheel::SlotShadowOffsetX;
			float sy = shapeCenterY + Config::AmmoWheel::SlotShadowOffsetY;
			drawList->AddCircleFilled(ImVec2(sx, sy), circleRadius + 1.0f, shadowColor, 24);
		}
		
		// Background
		drawList->AddCircleFilled(ImVec2(shapeCenterX, shapeCenterY), circleRadius, solidColor, 24);
		
		// Highlight border
		if (a_hovered && Config::AmmoWheel::SlotHighlightEnabled) {
			uint8_t highlightAlpha = static_cast<uint8_t>(Config::AmmoWheel::SlotHighlightAlpha * a_drawArgs.alphaMult);
			ImU32 highlightColor = IM_COL32(255, 255, 255, highlightAlpha);
			drawList->AddCircle(ImVec2(shapeCenterX, shapeCenterY), circleRadius, highlightColor, 24, Config::AmmoWheel::SlotHighlightThickness);
		}
		
		// Hover pulse
		if (a_hovered && Config::AmmoWheel::HoverPulseEnabled) {
			float pulseTime = static_cast<float>(ImGui::GetTime()) * Config::AmmoWheel::HoverPulseSpeed;
			float pulseFactor = 0.5f + 0.5f * std::sin(pulseTime);
			float pulseExpand = Config::AmmoWheel::HoverPulseSize * pulseFactor;
			
			ImU32 pulseColor = Config::AmmoWheel::HoverPulseColor;
			uint8_t pulseAlpha = static_cast<uint8_t>((pulseColor >> 24) * pulseFactor * a_drawArgs.alphaMult);
			pulseColor = (pulseColor & 0x00FFFFFF) | (pulseAlpha << 24);
			
			drawList->AddCircle(ImVec2(shapeCenterX, shapeCenterY), circleRadius + pulseExpand, pulseColor, 24, 2.0f);
		}
	}
	break;
	}

	// ========== DATA-DRIVEN INDICATORS ==========
	drawStandardIndicators(drawList);
	drawLowAmmoAtLayer(0);

	// ========== ICON RENDERING ==========
	// Check if icons are enabled (preset or legacy Skin setting)
	bool iconsEnabled = usePresetStyling && preset ? preset->IconsEnabled : Config::AmmoWheel::Skin::IconsEnabled;
	if (iconsEnabled && entry.iconImage.texture) {
		// Compute slot angular span
		float slotAngularSpan = a_endAngle - a_startAngle;
		
		// Use preset or legacy Skin icon settings for placement and rotation
		float iconRadialOffset = usePresetStyling && preset ? preset->IconRadialOffset : Config::AmmoWheel::Skin::IconRadialOffset;
		float iconPaddingPixels = usePresetStyling && preset ? preset->IconPaddingPixels : Config::AmmoWheel::Skin::IconPaddingPixels;
		float iconRotationSafetyScale = usePresetStyling && preset ? preset->IconRotationSafetyScale : Config::AmmoWheel::Skin::IconRotationSafetyScale;
		int iconRotationMode = usePresetStyling && preset ? preset->IconRotationMode : Config::AmmoWheel::Skin::IconRotationMode;
		float iconRotationOffsetDeg = usePresetStyling && preset ? preset->IconRotationOffsetDeg : Config::AmmoWheel::Skin::IconRotationOffsetDeg;
		float iconFixedAngleDeg = usePresetStyling && preset ? preset->IconFixedAngleDeg : Config::AmmoWheel::Skin::IconFixedAngleDeg;
		ImU32 iconTintColor = usePresetStyling && preset ? preset->IconTintColor : Config::AmmoWheel::Skin::IconTintColor;
		
		IconFitResult iconFit = ComputeIconRectForSlot(
			a_center,
			midAngle,
			a_innerRadius,
			a_outerRadius,
			slotAngularSpan,
			iconRadialOffset,
			iconPaddingPixels,
			iconRotationSafetyScale,
			iconRotationMode,
			iconRotationOffsetDeg,
			iconFixedAngleDeg
		);
		
		// Fallback to config size if computed size is too small
		float iconSize = (std::max)(iconFit.size, Config::AmmoWheel::IconSize * 0.5f);
		iconSize = (std::min)(iconSize, Config::AmmoWheel::IconSize * 1.5f);
		
		// Draw hover glow behind icon (if enabled and hovered)
		if (a_hovered && Config::AmmoWheel::IconHoverGlow) {
			ImU32 glowColor = Config::AmmoWheel::IconHoverGlowColor;
			uint8_t glowAlpha = static_cast<uint8_t>((glowColor >> 24) * a_drawArgs.alphaMult);
			glowColor = (glowColor & 0x00FFFFFF) | (glowAlpha << 24);
			
			DrawRotatedTexture(
				ImGui::GetWindowDrawList(),
				entry.iconImage.texture,
				iconFit.center,
				iconSize * 1.15f,
				iconSize * 1.15f,
				iconFit.rotationRad,
				glowColor
			);
		}
		
		// Draw main icon with rotation using preset or legacy tint color
		ImU32 iconTint = iconTintColor;
		uint8_t tintAlpha = static_cast<uint8_t>((iconTint >> 24) * a_drawArgs.alphaMult);
		iconTint = (iconTint & 0x00FFFFFF) | (tintAlpha << 24);
		
		DrawRotatedTexture(
			ImGui::GetWindowDrawList(),
			entry.iconImage.texture,
			iconFit.center,
			iconSize,
			iconSize,
			iconFit.rotationRad,
			iconTint
		);
	}
	drawLowAmmoAtLayer(1);

	// Text/Label Rendering with new layout system
	if (Config::AmmoWheel::LabelShow) {
		float baseFontSize = Config::AmmoWheel::NameFontPx * Config::AmmoWheel::NameTextScale;
		float textSize = baseFontSize;
		const char* originalName = entry.ammo->GetName();
		
		// Get screen dimensions for auto-width computation
		ImVec2 screenSize = ResolutionScale::Context::GetSingleton().GetRenderSize();
		float wheelCenterX = a_center.x;
		float wheelCenterY = a_center.y;
		float wheelOuterRadius = Config::AmmoWheel::WheelRadius;
		
		// Compute wheel bounding box
		float wheelLeft = wheelCenterX - wheelOuterRadius;
		float wheelRight = wheelCenterX + wheelOuterRadius;
		float wheelTop = wheelCenterY - wheelOuterRadius;
		float wheelBottom = wheelCenterY + wheelOuterRadius;
		
		// ========== AUTO-WIDTH COMPUTATION ==========
		float availWidth = 0.0f;
		float margin = Config::AmmoWheel::NameMarginPx;
		float padding = Config::AmmoWheel::NamePanelPaddingPx;
		
		// Determine label side based on wheel position
		bool labelOnLeft = (wheelCenterX > screenSize.x * 0.5f);
		
		if (Config::AmmoWheel::NameLayoutMode > 0) {
			// New layout modes: compute available width from screen space
			if (labelOnLeft) {
				availWidth = wheelLeft - margin - padding;
			} else {
				availWidth = screenSize.x - wheelRight - margin - padding;
			}
			
			// Apply max width cap if configured
			if (Config::AmmoWheel::NameMaxWidthPx > 0.0f) {
				availWidth = (std::min)(availWidth, Config::AmmoWheel::NameMaxWidthPx);
			}
			
			// Ensure minimum width
			availWidth = (std::max)(availWidth, 120.0f);
		} else {
			// Legacy mode: use slot arc-based width
			float slotArcLength = (a_endAngle - a_startAngle) * _cachedTextRadius;
			availWidth = slotArcLength * Config::AmmoWheel::LabelMaxSlotArcRatio;
			availWidth = (std::max)(availWidth, 60.0f);
		}
		
		// ========== LAYOUT MODE PROCESSING ==========
		TextLayout layout;
		int maxLines = Config::AmmoWheel::NameMaxLines;
		
		switch (Config::AmmoWheel::NameLayoutMode) {
			case 0: // LegacyEllipsis - original behavior
			{
				// Calculate normalized slot position for legacy multi-line logic
				float arcSpan = getArcAngleRad();
				float arcStart = getStartAngleRad();
				float arcMidAngle = arcStart + arcSpan * 0.5f;
				float slotAngle = (a_startAngle + a_endAngle) * 0.5f;
				float u = 0.0f;
				if (arcSpan > 0.01f) {
					u = (slotAngle - arcMidAngle) / (arcSpan * 0.5f);
					u = std::clamp(u, -1.0f, 1.0f);
				}
				float absU = std::abs(u);
				
				int legacyMaxLines = 1;
				if (Config::AmmoWheel::LabelMultiLine) {
					if (absU >= 0.70f) legacyMaxLines = 3;
					else if (absU >= 0.35f) legacyMaxLines = 2;
				}
				layout = wrapTextForSlot(originalName, availWidth, textSize, legacyMaxLines);
				break;
			}
			
			case 1: // Wrap - multi-line wrapping with full available width
			{
				layout = wrapTextForSlot(originalName, availWidth, textSize, maxLines);
				break;
			}
			
			case 2: // ShrinkToFit - reduce font size to fit on single line
			{
				ImFont* font = ImGui::GetFont();
				if (font) {
					ImVec2 size = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, originalName);
					float minFont = Config::AmmoWheel::NameMinFontPx;
					
					while (size.x > availWidth && textSize > minFont) {
						textSize -= 1.0f;
						size = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, originalName);
					}
					
					if (size.x <= availWidth) {
						layout.lines.push_back(originalName);
					} else {
						// Still too long at min font - apply ellipsis
						layout.lines.push_back(TruncateTextToFit(originalName, availWidth, textSize));
					}
				} else {
					layout.lines.push_back(originalName);
				}
				layout.totalHeight = textSize * 1.2f;
				break;
			}
			
			case 3: // Hybrid - try wrap first, then shrink if needed
			{
				layout = wrapTextForSlot(originalName, availWidth, textSize, maxLines);
				
				// Check if last line overflows
				ImFont* font = ImGui::GetFont();
				if (font && !layout.lines.empty()) {
					ImVec2 lastLineSize = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, layout.lines.back().c_str());
					
					if (lastLineSize.x > availWidth) {
						// Try shrinking font
						float minFont = Config::AmmoWheel::NameMinFontPx;
						float trySize = textSize;
						
						while (trySize > minFont) {
							trySize -= 1.0f;
							TextLayout tryLayout = wrapTextForSlot(originalName, availWidth, trySize, maxLines);
							
							bool allFit = true;
							for (const auto& line : tryLayout.lines) {
								ImVec2 lineSize = font->CalcTextSizeA(trySize, FLT_MAX, 0.0f, line.c_str());
								if (lineSize.x > availWidth) {
									allFit = false;
									break;
								}
							}
							
							if (allFit) {
								textSize = trySize;
								layout = tryLayout;
								break;
							}
						}
					}
				}
				layout.totalHeight = layout.lines.size() * textSize * (1.0f + Config::AmmoWheel::NameLineSpacingPx / textSize);
				break;
			}
		}
		
		// ========== POSITION CALCULATION ==========
		float textX = textCenter.x;
		float textY = textCenter.y;
		float lineSpacing = textSize + Config::AmmoWheel::NameLineSpacingPx;
		float totalTextHeight = layout.lines.size() * lineSpacing;
		
		if (Config::AmmoWheel::ShowAmmoCount) {
			textY -= (totalTextHeight * 0.3f);
		}
		float lineStartY = textY - totalTextHeight * 0.5f + textSize * 0.5f;
		
		// ========== CLIP RECT CALCULATION ==========
		float clipHalfWidth, clipHalfHeight;
		if (Config::AmmoWheel::NameLayoutMode > 0) {
			// New modes: use full available width for clipping
			clipHalfWidth = availWidth * 0.55f;
			clipHalfHeight = totalTextHeight + padding;
		} else {
			// Legacy mode: original clip rect
			clipHalfWidth = availWidth * 0.6f;
			clipHalfHeight = totalTextHeight * 0.8f;
		}
		
		ImVec2 clipMin(textCenter.x - clipHalfWidth, textCenter.y - clipHalfHeight);
		ImVec2 clipMax(textCenter.x + clipHalfWidth, textCenter.y + clipHalfHeight);
		
		// ========== TEXT BACKGROUND PANEL ==========
		if (Config::AmmoWheel::NameTextBgEnabled && !layout.lines.empty()) {
			// Calculate text block bounding rect
			float bgPadding = Config::AmmoWheel::NameTextBgExtraPaddingPx + padding;
			float bgInset = Config::AmmoWheel::NameTextBgInsetPx;
			
			float bgLeft = textX - clipHalfWidth - bgPadding;
			float bgRight = textX + clipHalfWidth + bgPadding;
			float bgTop = lineStartY - textSize * 0.5f - bgPadding;
			float bgBottom = lineStartY + totalTextHeight - textSize * 0.5f + bgPadding;
			
			// Apply inset from screen edges
			bgLeft = (std::max)(bgLeft, bgInset);
			bgRight = (std::min)(bgRight, screenSize.x - bgInset);
			bgTop = (std::max)(bgTop, bgInset);
			bgBottom = (std::min)(bgBottom, screenSize.y - bgInset);
			
			// Calculate background color with opacity
			ImU32 bgColor = Config::AmmoWheel::NameTextBgColor;
			uint8_t bgAlpha = static_cast<uint8_t>((bgColor >> 24) * Config::AmmoWheel::NameTextBgOpacity * a_drawArgs.alphaMult);
			bgColor = (bgColor & 0x00FFFFFF) | (bgAlpha << 24);
			
			ImGui::GetWindowDrawList()->AddRectFilled(
				ImVec2(bgLeft, bgTop),
				ImVec2(bgRight, bgBottom),
				bgColor,
				Config::AmmoWheel::NameTextBgCornerRounding
			);
		}
		
		// ========== DEBUG VISUALIZATION ==========
		if (Config::AmmoWheel::DebugDrawTextRects) {
			ImDrawList* dl = ImGui::GetWindowDrawList();
			// Clip rect (red)
			dl->AddRect(clipMin, clipMax, IM_COL32(255, 0, 0, 180), 0.0f, 0, 1.0f);
			// Available width line (green)
			float wrapLineX = textX + availWidth * 0.5f;
			dl->AddLine(ImVec2(wrapLineX, clipMin.y), ImVec2(wrapLineX, clipMax.y), IM_COL32(0, 255, 0, 180), 1.0f);
			// Wheel bbox (blue)
			dl->AddRect(ImVec2(wheelLeft, wheelTop), ImVec2(wheelRight, wheelBottom), IM_COL32(0, 0, 255, 100), 0.0f, 0, 1.0f);
		}
		
		ImGui::GetWindowDrawList()->PushClipRect(clipMin, clipMax, true);
		
		// ========== DRAW TEXT LINES ==========
		// TASK 1: Use color override if enabled, otherwise fall back to theme/default
		ImU32 textColor;
		if (a_hovered && Config::AmmoWheel::ArrowLabelColorOverrideEnabled) {
			textColor = Config::AmmoWheel::ArrowLabelColorComputed;
		} else if (Config::AmmoWheel::SlotLabelColorOverrideEnabled) {
			textColor = Config::AmmoWheel::SlotLabelColorComputed;
		} else if (Config::AmmoWheel::UseSkyrimTheme) {
			textColor = Config::AmmoWheel::SkyrimTheme::TextPrimary;
		} else {
			textColor = Config::AmmoWheel::NameTextColor;
		}
		// Apply alpha mult
		uint8_t textAlpha = static_cast<uint8_t>((textColor >> 24) * a_drawArgs.alphaMult);
		textColor = (textColor & 0x00FFFFFF) | (textAlpha << 24);
		
		for (size_t i = 0; i < layout.lines.size(); i++) {
			float lineY = lineStartY + i * lineSpacing;
			
			// Text shadow
			if (Config::AmmoWheel::TextShadowEnabled) {
				uint8_t shadowAlpha = static_cast<uint8_t>(Config::AmmoWheel::TextShadowAlpha * a_drawArgs.alphaMult);
				ImU32 shadowColor = IM_COL32(0, 0, 0, shadowAlpha);
				float offset = Config::AmmoWheel::TextShadowOffset;
				Drawer::draw_text(textX + offset, lineY + offset, layout.lines[i].c_str(), shadowColor, textSize, a_drawArgs);
			}
			
			// Hover glow
			if (a_hovered && Config::AmmoWheel::TextHoverGlowEnabled) {
				ImU32 glowColor = Config::AmmoWheel::UseSkyrimTheme 
					? Config::AmmoWheel::SkyrimTheme::HighlightGold 
					: Config::AmmoWheel::TextHoverGlowColor;
				uint8_t glowAlpha = static_cast<uint8_t>((glowColor >> 24) * a_drawArgs.alphaMult);
				glowColor = (glowColor & 0x00FFFFFF) | (glowAlpha << 24);
				Drawer::draw_text(textX, lineY, layout.lines[i].c_str(), glowColor, textSize * 1.02f, a_drawArgs);
			}
			
			// TASK 2: Faux-bold rendering (draw text multiple times with offset)
			if (Config::AmmoWheel::NameBoldEnabled && Config::AmmoWheel::NameBoldMode == 1) {
				float boldOffset = Config::AmmoWheel::NameBoldStrengthPx;
				Drawer::draw_text(textX + boldOffset, lineY, layout.lines[i].c_str(), textColor, textSize, a_drawArgs);
				Drawer::draw_text(textX, lineY + boldOffset, layout.lines[i].c_str(), textColor, textSize, a_drawArgs);
			}
			
			// Main text
			Drawer::draw_text(textX, lineY, layout.lines[i].c_str(), textColor, textSize, a_drawArgs);
		}
		
		ImGui::GetWindowDrawList()->PopClipRect();
	}

	// Draw ammo count at its configured radius position
	if (Config::AmmoWheel::ShowAmmoCount) {
		ImVec2 countCenter = ImVec2(
			a_center.x + _cachedCountRadius * std::cos(midAngle),
			a_center.y + _cachedCountRadius * std::sin(midAngle)
		);
		drawAmmoCount(countCenter, entry.count, a_drawArgs);
	}

	drawLowAmmoAtLayer(2);
}

void AmmoWheel::drawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	if (!Config::AmmoWheel::CenterEnabled) {
		return;
	}
	
	if (_hoveredIndex < 0 || _hoveredIndex >= static_cast<int>(_ammoEntries.size())) {
		return;
	}

	auto& entry = _ammoEntries[_hoveredIndex];
	if (!entry.ammo) {
		return;
	}

	// TASK 2: Use adaptive center panel position (or fixed offset)
	ImVec2 panelCenter;
	if (Config::AmmoWheel::CenterPanelPositionMode == 1) {
		// Fixed offset mode
		panelCenter = ImVec2(a_center.x + Config::AmmoWheel::CenterPanelOffsetX,
		                     a_center.y + Config::AmmoWheel::CenterPanelOffsetY);
	} else {
		// Auto inside arc mode
		panelCenter = calculateCenterPanelPosition(a_center);
	}

	// Use configured font sizes with min/max clamping
	float nameFontSize = std::clamp(Config::AmmoWheel::CenterFontPx * 1.2f, 
	                                 Config::AmmoWheel::CenterTextMinFontSize,
	                                 Config::AmmoWheel::CenterTextMaxFontSize);
	float infoFontSize = std::clamp(Config::AmmoWheel::CenterFontPx,
	                                 Config::AmmoWheel::CenterTextMinFontSize,
	                                 Config::AmmoWheel::CenterTextMaxFontSize);
	float lineSpacing = Config::AmmoWheel::CenterLineSpacingPx;
	float padding = Config::AmmoWheel::CenterPaddingPx;
	
	// ========== DAMAGE-ONLY HIGHLIGHT: Compute max damage across all entries ==========
	float maxDamage = 0.0f;
	int entriesWithMaxDamage = 0;
	constexpr float DAMAGE_EPSILON = 0.01f;  // Tolerance for floating-point comparison
	
	// Debug: Log all entries if enabled
	if (Config::AmmoWheel::Debug::LogCentralPanel) {
		static bool loggedScan = false;
		static int lastHoveredForScan = -1;
		if (_hoveredIndex != lastHoveredForScan) {
			loggedScan = false;
			lastHoveredForScan = _hoveredIndex;
		}
		if (!loggedScan) {
			logger::info("[CentralPanel] === Scanning {} entries for maxDamage ===", _ammoEntries.size());
			loggedScan = true;
		}
	}
	
	for (const auto& e : _ammoEntries) {
		if (!e.ammo) continue;
		float dmg = e.ammo->data.damage;
		if (a_imap.contains(e.ammo)) {
			RE::InventoryEntryData* invEntry = a_imap.find(e.ammo)->second.second.get();
			dmg = RE::PlayerCharacter::GetSingleton()->GetDamage(invEntry);
		}
		
		// Debug: Log each entry's damage
		if (Config::AmmoWheel::Debug::LogCentralPanel) {
			static bool loggedScan = false;
			static int lastHoveredForScan = -1;
			if (_hoveredIndex != lastHoveredForScan) {
				loggedScan = false;
				lastHoveredForScan = _hoveredIndex;
			}
			if (!loggedScan) {
				logger::info("[CentralPanel]   {} | Type: {} | Damage: {:.2f}",
					e.ammo->GetName(),
					e.ammo->IsBolt() ? "Bolt" : "Arrow",
					dmg);
			}
		}
		
		if (dmg > maxDamage + DAMAGE_EPSILON) {
			maxDamage = dmg;
			entriesWithMaxDamage = 1;
		} else if (std::abs(dmg - maxDamage) <= DAMAGE_EPSILON) {
			entriesWithMaxDamage++;
		}
	}
	
	// ========== PHASE 3: BUILD TEXT LINES FROM FIELD CONFIG ==========
	std::vector<std::pair<std::string, float>> textLines;  // {text, fontSize}
	std::vector<bool> isDamageLine;  // Track which lines are damage lines
	
	// Calculate damage for current entry
	float damage = entry.ammo->data.damage;
	if (a_imap.contains(entry.ammo)) {
		RE::InventoryEntryData* invEntry = a_imap.find(entry.ammo)->second.second.get();
		damage = RE::PlayerCharacter::GetSingleton()->GetDamage(invEntry);
	}
	
	// Determine if this entry should have highlighted damage (with epsilon tolerance)
	bool shouldHighlightDamage = (std::abs(damage - maxDamage) <= DAMAGE_EPSILON && maxDamage > 0.0f);
	
	// Debug logging (once per panel refresh)
	if (Config::AmmoWheel::Debug::LogCentralPanel) {
		static bool loggedThisRefresh = false;
		static int lastHoveredIndex = -1;
		if (_hoveredIndex != lastHoveredIndex) {
			loggedThisRefresh = false;
			lastHoveredIndex = _hoveredIndex;
		}
		if (!loggedThisRefresh) {
			logger::info("[CentralPanel] Hovered: {} | Type: {} | Damage: {:.0f} | MaxDamage: {:.0f} | Highlight: {} | TiedEntries: {}",
				entry.ammo->GetName(),
				entry.ammo->IsBolt() ? "Bolt" : "Arrow",
				damage,
				maxDamage,
				shouldHighlightDamage,
				entriesWithMaxDamage);
			loggedThisRefresh = true;
		}
	}
	
	// Determine ammo type
	std::string ammoType = entry.ammo->IsBolt() ? "Bolt" : "Arrow";
	
	// Determine source (Vanilla vs Modded)
	std::string source = "Vanilla";
	if (entry.ammo) {
		auto* file = entry.ammo->GetFile(0);
		if (file) {
			std::string filename(file->GetFilename());
			// Base game plugins
			if (filename != "Skyrim.esm" && filename != "Update.esm" && 
			    filename != "Dawnguard.esm" && filename != "HearthFires.esm" && 
			    filename != "Dragonborn.esm") {
				source = filename;  // Show mod name
			}
		}
	}
	
	// Build lines based on field toggles and order
	// Parse order string: "Name,Damage,Type,Count,Source"
	std::string orderStr = Config::AmmoWheel::CenterFields::Order;
	std::vector<std::string> fieldOrder;
	{
		size_t pos = 0;
		while ((pos = orderStr.find(',')) != std::string::npos) {
			std::string field = orderStr.substr(0, pos);
			if (!field.empty()) fieldOrder.push_back(field);
			orderStr.erase(0, pos + 1);
		}
		if (!orderStr.empty()) fieldOrder.push_back(orderStr);
	}
	
	// Add fields in configured order
	for (const auto& field : fieldOrder) {
		if (field == "Name" && Config::AmmoWheel::CenterFields::ShowName) {
			// PHASE 1: Use word wrapping for long ammo names
			if (Config::AmmoWheel::EnableWordWrap) {
				TextLayout wrappedName = wrapTextForCenterPanel(entry.ammo->GetName(), nameFontSize, panelCenter);
				for (const auto& line : wrappedName.lines) {
					textLines.push_back({line, nameFontSize});
					isDamageLine.push_back(false);
				}
			} else {
				textLines.push_back({entry.ammo->GetName(), nameFontSize});
				isDamageLine.push_back(false);
			}
		} else if (field == "Damage" && Config::AmmoWheel::CenterFields::ShowDamage) {
			textLines.push_back({fmt::format("Damage: {:.0f}", damage), infoFontSize});
			isDamageLine.push_back(true);  // Mark as damage line
		} else if (field == "Type" && Config::AmmoWheel::CenterFields::ShowType) {
			textLines.push_back({fmt::format("Type: {}", ammoType), infoFontSize});
			isDamageLine.push_back(false);
		} else if (field == "Count" && Config::AmmoWheel::CenterFields::ShowCount) {
			textLines.push_back({fmt::format("Count: {}", entry.count), infoFontSize});
			isDamageLine.push_back(false);
		} else if (field == "Source" && Config::AmmoWheel::CenterFields::ShowSource) {
			textLines.push_back({fmt::format("Source: {}", source), infoFontSize});
			isDamageLine.push_back(false);
		}
	}
	
	// Fallback if no fields configured
	if (textLines.empty()) {
		if (Config::AmmoWheel::EnableWordWrap) {
			TextLayout wrappedName = wrapTextForCenterPanel(entry.ammo->GetName(), nameFontSize, panelCenter);
			for (const auto& line : wrappedName.lines) {
				textLines.push_back({line, nameFontSize});
				isDamageLine.push_back(false);
			}
		} else {
			textLines.push_back({entry.ammo->GetName(), nameFontSize});
			isDamageLine.push_back(false);
		}
	}
	
	// Calculate total height and max width
	float totalHeight = padding * 2;
	float maxTextWidth = 0.0f;
	for (size_t i = 0; i < textLines.size(); i++) {
		totalHeight += textLines[i].second;
		if (i > 0) totalHeight += lineSpacing;
		
		// Measure text width
		ImVec2 textSize = ImGui::CalcTextSize(textLines[i].first.c_str());
		float scaledWidth = textSize.x * (textLines[i].second / ImGui::GetFontSize());
		maxTextWidth = (std::max)(maxTextWidth, scaledWidth);
	}
	
	float panelWidth = (std::min)(maxTextWidth + padding * 2, 
	                            _cachedInnerRadius * Config::AmmoWheel::CenterMaxWidthRatio * 2.0f);
	float panelHeight = totalHeight;
	
	// TASK 2: Clamp to screen if enabled
	if (Config::AmmoWheel::CenterPanelClampToScreen) {
		ImVec2 viewport = ResolutionScale::Context::GetSingleton().GetRenderSize();
		float margin = Config::AmmoWheel::CenterPanelSafeMargin;
		panelCenter.x = std::clamp(panelCenter.x, margin + panelWidth / 2.0f, viewport.x - margin - panelWidth / 2.0f);
		panelCenter.y = std::clamp(panelCenter.y, margin + panelHeight / 2.0f, viewport.y - margin - panelHeight / 2.0f);
	}
	
	// Calculate bounds
	ImVec2 bgMin = ImVec2(panelCenter.x - panelWidth / 2.0f, panelCenter.y - panelHeight / 2.0f);
	ImVec2 bgMax = ImVec2(panelCenter.x + panelWidth / 2.0f, panelCenter.y + panelHeight / 2.0f);
	
	// TASK 2: Draw shape-specific background
	if (Config::AmmoWheel::CenterBgEnabled) {
		float bgAlpha = Config::AmmoWheel::CenterBgOpacity * a_drawArgs.alphaMult;
		ImU32 bgColor = IM_COL32(0, 0, 0, static_cast<int>(bgAlpha * 255));
		auto* drawList = ImGui::GetWindowDrawList();
		
		// Determine shape: 0=Auto, 1=Rectangle, 2=Circle, 3=RoundedRect
		int shapeType = Config::AmmoWheel::CenterPanelShapeIndex;
		if (shapeType == 0) {
			// Auto: match wheel geometry
			float arcSpan = getArcAngleRad();
			if (arcSpan >= 2.0f * IM_PI * 0.9f) {
				shapeType = 2;  // Full circle -> Circle panel
			} else {
				shapeType = 3;  // Partial arc -> RoundedRect
			}
		}
		
		switch (shapeType) {
		case 1:  // Rectangle
			drawList->AddRectFilled(bgMin, bgMax, bgColor, 0.0f);
			break;
		case 2: {  // Circle
			float radius = (std::max)(panelWidth, panelHeight) / 2.0f * 1.1f;
			drawList->AddCircleFilled(panelCenter, radius, bgColor, 48);
			break;
		}
		case 3:  // RoundedRect (default)
		default:
			drawList->AddRectFilled(bgMin, bgMax, bgColor, Config::AmmoWheel::CenterPanelCornerRounding);
			break;
		}
		
		// Draw border if configured
		if (Config::AmmoWheel::CenterPanelBorderThickness > 0.0f) {
			float borderAlpha = Config::AmmoWheel::CenterPanelBorderAlpha * a_drawArgs.alphaMult;
			ImU32 borderColor = IM_COL32(139, 90, 43, static_cast<int>(borderAlpha * 255));  // Bronze
			
			switch (shapeType) {
			case 1:
				drawList->AddRect(bgMin, bgMax, borderColor, 0.0f, 0, Config::AmmoWheel::CenterPanelBorderThickness);
				break;
			case 2: {
				float radius = (std::max)(panelWidth, panelHeight) / 2.0f * 1.1f;
				drawList->AddCircle(panelCenter, radius, borderColor, 48, Config::AmmoWheel::CenterPanelBorderThickness);
				break;
			}
			case 3:
			default:
				drawList->AddRect(bgMin, bgMax, borderColor, Config::AmmoWheel::CenterPanelCornerRounding, 0, Config::AmmoWheel::CenterPanelBorderThickness);
				break;
			}
		}
	}

	// ========== ANIMATION: CENTER FRAME ==========
	if (Config::AmmoWheel::CenterFrameEnabled) {
		float framePadding = 5.0f;
		ImVec2 frameMin = ImVec2(bgMin.x - framePadding, bgMin.y - framePadding);
		ImVec2 frameMax = ImVec2(bgMax.x + framePadding, bgMax.y + framePadding);
		
		// Calculate frame color with optional pulse
		ImU32 frameColor = Config::AmmoWheel::CenterFrameColor;
		float frameAlphaMult = a_drawArgs.alphaMult;
		if (Config::AmmoWheel::CenterFramePulse) {
			float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * Config::AmmoWheel::CenterFramePulseSpeed);
			frameAlphaMult *= pulse;
		}
		uint8_t frameAlpha = static_cast<uint8_t>((frameColor >> 24) * frameAlphaMult);
		frameColor = (frameColor & 0x00FFFFFF) | (frameAlpha << 24);
		
		// Draw frame border
		ImGui::GetWindowDrawList()->AddRect(frameMin, frameMax, frameColor, 6.0f, 0, 3.0f);
		
		// Draw corner decorations
		if (Config::AmmoWheel::CenterCornersEnabled) {
			float len = Config::AmmoWheel::CenterCornerSize;
			auto* drawList = ImGui::GetWindowDrawList();
			
			// Top-left corner
			drawList->AddLine(ImVec2(frameMin.x, frameMin.y + len), ImVec2(frameMin.x, frameMin.y), frameColor, 2.0f);
			drawList->AddLine(ImVec2(frameMin.x, frameMin.y), ImVec2(frameMin.x + len, frameMin.y), frameColor, 2.0f);
			
			// Top-right corner
			drawList->AddLine(ImVec2(frameMax.x - len, frameMin.y), ImVec2(frameMax.x, frameMin.y), frameColor, 2.0f);
			drawList->AddLine(ImVec2(frameMax.x, frameMin.y), ImVec2(frameMax.x, frameMin.y + len), frameColor, 2.0f);
			
			// Bottom-right corner
			drawList->AddLine(ImVec2(frameMax.x, frameMax.y - len), ImVec2(frameMax.x, frameMax.y), frameColor, 2.0f);
			drawList->AddLine(ImVec2(frameMax.x, frameMax.y), ImVec2(frameMax.x - len, frameMax.y), frameColor, 2.0f);
			
			// Bottom-left corner
			drawList->AddLine(ImVec2(frameMin.x + len, frameMax.y), ImVec2(frameMin.x, frameMax.y), frameColor, 2.0f);
			drawList->AddLine(ImVec2(frameMin.x, frameMax.y), ImVec2(frameMin.x, frameMax.y - len), frameColor, 2.0f);
		}
	}

	// TASK 2: Draw stacked text lines with two-tier damage highlighting
	float textY = panelCenter.y - panelHeight / 2.0f + padding;
	for (size_t i = 0; i < textLines.size(); i++) {
		ImU32 textColor;
		
		// TWO-TIER DAMAGE HIGHLIGHT: Gold for max damage, light red for others
		// Damage highlight logic is preserved exactly as before
		if (i < isDamageLine.size() && isDamageLine[i]) {
			if (shouldHighlightDamage) {
				// Max damage: Gold
				ImU32 maxColor = Config::AmmoWheel::CenterFields::MaxDamageColor;
				textColor = IM_COL32(
					(maxColor >> IM_COL32_R_SHIFT) & 0xFF,
					(maxColor >> IM_COL32_G_SHIFT) & 0xFF,
					(maxColor >> IM_COL32_B_SHIFT) & 0xFF,
					static_cast<int>(255 * a_drawArgs.alphaMult)
				);
			} else {
				// Other damage: Light red
				ImU32 otherColor = Config::AmmoWheel::CenterFields::OtherDamageColor;
				textColor = IM_COL32(
					(otherColor >> IM_COL32_R_SHIFT) & 0xFF,
					(otherColor >> IM_COL32_G_SHIFT) & 0xFF,
					(otherColor >> IM_COL32_B_SHIFT) & 0xFF,
					static_cast<int>(255 * a_drawArgs.alphaMult)
				);
			}
		} else {
			// TASK 1: Non-damage lines use center label color override if enabled
			if (Config::AmmoWheel::CenterLabelColorOverrideEnabled) {
				textColor = Config::AmmoWheel::CenterLabelColorComputed;
				uint8_t a = static_cast<uint8_t>((textColor >> 24) * a_drawArgs.alphaMult);
				textColor = (textColor & 0x00FFFFFF) | (a << 24);
			} else {
				textColor = IM_COL32(255, 255, 255, static_cast<int>(255 * a_drawArgs.alphaMult));
			}
		}
		
		// Apply text shadow if enabled
		if (Config::AmmoWheel::CenterTextShadowEnabled) {
			float shadowOffset = 1.5f;
			ImU32 shadowColor = IM_COL32(0, 0, 0, static_cast<int>(180 * a_drawArgs.alphaMult));
			Drawer::draw_text(panelCenter.x + shadowOffset, textY + shadowOffset, 
			                  textLines[i].first.c_str(), shadowColor, textLines[i].second, a_drawArgs);
		}
		
		Drawer::draw_text(panelCenter.x, textY, textLines[i].first.c_str(), textColor, textLines[i].second, a_drawArgs);
		textY += textLines[i].second + lineSpacing;
	}
}

void AmmoWheel::drawAmmoCount(ImVec2 a_slotCenter, int a_count, DrawArgs a_drawArgs)
{
	std::string countStr = a_count > 999 ? "999+" : std::to_string(a_count);
	ImU32 color = Config::AmmoWheel::CountColor;
	float fontSize = Config::AmmoWheel::CountFontPx;  // Use configured font size
	Drawer::draw_text(a_slotCenter.x, a_slotCenter.y + 10.f, countStr.c_str(), color, fontSize, a_drawArgs);
}

void AmmoWheel::drawLowAmmoWarning(ImVec2 a_center, float a_innerRadius, float a_outerRadius, float a_midAngle, DrawArgs a_drawArgs)
{
	// Pulse warning indicator for low ammo using configured color
	float pulse = static_cast<float>(std::sin(ImGui::GetTime() * 5.0) * 0.5 + 0.5);
	
	// Extract RGB from configured color and apply pulsing alpha
	ImU32 baseColor = Config::AmmoWheel::LowAmmoIndicatorColor;
	int r = (baseColor >> IM_COL32_R_SHIFT) & 0xFF;
	int g = (baseColor >> IM_COL32_G_SHIFT) & 0xFF;
	int b = (baseColor >> IM_COL32_B_SHIFT) & 0xFF;
	int alpha = static_cast<int>((127 + 128 * pulse) * a_drawArgs.alphaMult);
	ImU32 warningColor = IM_COL32(r, g, b, alpha);

	const float ringThickness = a_outerRadius - a_innerRadius;
	const float radiusRatio = std::clamp(Config::AmmoWheel::LowAmmoIndicatorRadiusRatio, 0.0f, 1.0f);
	const float baseRadius = a_innerRadius + ringThickness * radiusRatio;
	const float radius = baseRadius + Config::AmmoWheel::LowAmmoIndicatorRadialOffsetPx;
	const float angle = a_midAngle + (Config::AmmoWheel::LowAmmoIndicatorAngularOffsetDeg * (IM_PI / 180.0f));

	ImVec2 center{
		a_center.x + radius * std::cos(angle),
		a_center.y + radius * std::sin(angle)
	};

	ImGui::GetWindowDrawList()->AddCircle(
		center,
		8.0f,
		warningColor,
		12,
		Config::AmmoWheel::LowAmmoIndicatorThickness
	);
}

void AmmoWheel::drawHoverPopup(ImVec2 a_wheelCenter, DrawArgs a_drawArgs)
{
	if (!Config::AmmoWheel::PopupEnabled) {
		return;
	}

	// ========== PHASE 4: ENHANCED POPUP ANIMATION ==========
	// Use time-based animation with configurable durations
	float deltaTime = ImGui::GetIO().DeltaTime;
	float hoverInSpeed = Config::AmmoWheel::PopupAnim::Enabled ? (1000.0f / Config::AmmoWheel::PopupAnim::HoverInMs) : Config::AmmoWheel::PopupAnimationSpeed;
	float hoverOutSpeed = Config::AmmoWheel::PopupAnim::Enabled ? (1000.0f / Config::AmmoWheel::PopupAnim::HoverOutMs) : Config::AmmoWheel::PopupAnimationSpeed;
	
	// DEBUG: Log popup animation progress (rate-limited, debug only)
	static float lastLoggedScale = -1.0f;
	if (Config::Debug::LogPopupAnim) {
		using Clock = std::chrono::steady_clock;
		static auto nextLogTime = Clock::time_point{};
		const auto now = Clock::now();
		if (now >= nextLogTime && std::abs(_hoverPopupScale - lastLoggedScale) > 0.1f) {
			lastLoggedScale = _hoverPopupScale;
			logger::info("  [POPUP ANIM] t={:.2f}, hoverInSpeed={:.2f}, ScaleFrom={:.2f}, ScaleTo={:.2f}",
				_hoverPopupScale, hoverInSpeed,
				Config::AmmoWheel::PopupAnim::ScaleFrom,
				Config::AmmoWheel::PopupAnim::ScaleTo);
			const auto intervalMs = Config::Debug::PopupAnimLogIntervalMs > 0 ? Config::Debug::PopupAnimLogIntervalMs : 1u;
			nextLogTime = now + std::chrono::milliseconds(intervalMs);
		}
	}

	if (_hoveredIndex < 0 || _hoveredIndex >= static_cast<int>(_ammoEntries.size())) {
		// Animate popup closing
		if (_hoverPopupScale > 0.0f) {
			_hoverPopupScale -= deltaTime * hoverOutSpeed;
			if (_hoverPopupScale < 0.0f) _hoverPopupScale = 0.0f;
		}
		if (_hoverPopupScale <= 0.0f) return;
	} else {
		// Animate popup opening
		if (_hoverPopupScale < 1.0f) {
			_hoverPopupScale += deltaTime * hoverInSpeed;
			if (_hoverPopupScale > 1.0f) _hoverPopupScale = 1.0f;
		}
	}
	
	if (_hoveredIndex < 0) return;
	if (_ammoEntries.empty() || _hoveredIndex >= static_cast<int>(_ammoEntries.size())) return;

	auto& entry = _ammoEntries[_hoveredIndex];
	if (!entry.ammo) return;
	
	// Config settings
	float bubbleRadius = Config::AmmoWheel::PopupBubbleRadius;
	float iconSize = Config::AmmoWheel::PopupIconSizePx;
	float nameSize = Config::AmmoWheel::PopupNameFontPx;
	float countSize = Config::AmmoWheel::PopupCountFontPx;
	float offset = Config::AmmoWheel::PopupOffsetPx;
	float padding = Config::AmmoWheel::PopupPaddingPx;
	
	// Calculate popup position
	float midAngle = getStartAngleRad() + 
		(getArcAngleRad() / _ammoEntries.size()) * (_hoveredIndex + 0.5f);
	
	float popupDistance = _cachedOuterRadius + offset;
	ImVec2 popupCenter = ImVec2(
		a_wheelCenter.x + popupDistance * std::cos(midAngle),
		a_wheelCenter.y + popupDistance * std::sin(midAngle)
	);
	
	// Clamp popup to viewport safe area
	ImVec2 viewport = ResolutionScale::Context::GetSingleton().GetRenderSize();
	float margin = bubbleRadius + 10.0f;
	popupCenter.x = std::clamp(popupCenter.x, margin, viewport.x - margin);
	popupCenter.y = std::clamp(popupCenter.y, margin, viewport.y - margin);
	
	// ========== PHASE 4: APPLY EASING FUNCTION ==========
	float t = _hoverPopupScale;
	float easedT = t;
	
	if (Config::AmmoWheel::PopupAnim::Enabled) {
		switch (Config::AmmoWheel::PopupAnim::Easing) {
			case 0:  // Linear
				easedT = t;
				break;
			case 1:  // OutCubic
				easedT = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
				break;
			case 2:  // OutBack (overshoot)
				{
					float c1 = 1.70158f;
					float c3 = c1 + 1.0f;
					easedT = 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
				}
				break;
			default:
				easedT = 1.0f - (1.0f - t) * (1.0f - t);  // Quadratic fallback
				break;
		}
	} else {
		easedT = 1.0f - (1.0f - t) * (1.0f - t);  // Legacy quadratic ease-out
	}
	
	// Apply scale range from config
	float scaleFrom = Config::AmmoWheel::PopupAnim::Enabled ? Config::AmmoWheel::PopupAnim::ScaleFrom : 0.95f;
	float scaleTo = Config::AmmoWheel::PopupAnim::Enabled ? Config::AmmoWheel::PopupAnim::ScaleTo : 1.0f;
	float scale = scaleFrom + (scaleTo - scaleFrom) * easedT;
	float alpha = easedT * a_drawArgs.alphaMult;
	float animatedRadius = bubbleRadius * scale;
	
	// TASK 5: Draw circular bubble background
	// PopupAnim::BackgroundOpacity is applied as a user multiplier on top of theme/custom color alpha
	float popupOpacityMult = Config::AmmoWheel::PopupAnim::BackgroundOpacity;  // User slider (0.0-1.0)
	
	// ========== UNIFIED RESKIN: PopupBubble texture support ==========
	// Try to draw textured popup bubble background via ReskinSystem
	// Falls back to primitive shapes if texture not enabled/available
	bool drewTexturedBubble = false;
	auto& reskinSystem = AmmoWheelReskinUnified::ReskinSystem::GetSingleton();
	
	if (reskinSystem.IsEnabled() && entry.reskinEntry.preset) {
		// Build DrawContext for popup bubble (outside wheel, at popupCenter)
		AmmoWheelReskinUnified::DrawContext bubbleCtx;
		bubbleCtx.center = popupCenter;
		bubbleCtx.radius = animatedRadius;  // Use animated radius for proper scaling
		bubbleCtx.slotAngleRad = 0.0f;      // Popup bubble is always upright
		bubbleCtx.alphaMult = alpha;
		bubbleCtx.slotIndex = _hoveredIndex;
		bubbleCtx.formID = entry.ammo ? entry.ammo->GetFormID() : 0;
		bubbleCtx.hovered = true;
		bubbleCtx.selected = false;
		bubbleCtx.active = false;
		
		// Attempt to draw PopupBubble target (new visual target for popup bubble outside wheel)
		drewTexturedBubble = reskinSystem.DrawTarget(
			AmmoWheelReskinUnified::VisualTarget::PopupBubble,
			entry.reskinEntry,
			bubbleCtx,
			ImGui::GetWindowDrawList()
		);
		
		if (drewTexturedBubble && Config::AmmoWheel::Debug::LogAssetLoading) {
			static bool loggedOnce = false;
			if (!loggedOnce) {
				logger::info("[AmmoWheel] PopupBubble texture drawn at ({:.0f}, {:.0f}) radius={:.0f}",
					popupCenter.x, popupCenter.y, animatedRadius);
				loggedOnce = true;
			}
		}
	}
	// ========== END UNIFIED RESKIN ==========
	
	// Fallback to primitive shapes if texture not drawn
	if (!drewTexturedBubble) {
		if (Config::AmmoWheel::PopupCircular) {
			ImU32 bgColor = Config::AmmoWheel::PopupUseCustomColor 
				? Config::AmmoWheel::PopupBackgroundColor 
				: (Config::AmmoWheel::UseSkyrimTheme 
					? Config::AmmoWheel::SkyrimTheme::BgDarkLayer 
					: IM_COL32(20, 15, 10, 220));
			uint8_t baseBgA = static_cast<uint8_t>(bgColor >> 24);
			uint8_t bgAlpha = static_cast<uint8_t>(baseBgA * popupOpacityMult * alpha);
			bgColor = (bgColor & 0x00FFFFFF) | (bgAlpha << 24);
			
			// Main circle fill
			ImGui::GetWindowDrawList()->AddCircleFilled(popupCenter, animatedRadius, bgColor, 48);
		} else {
			// Fallback: rectangular popup
			float popupWidth = bubbleRadius * 2.0f;
			float popupHeight = bubbleRadius * 2.0f;
			ImVec2 popupMin(popupCenter.x - popupWidth/2, popupCenter.y - popupHeight/2);
			ImVec2 popupMax(popupCenter.x + popupWidth/2, popupCenter.y + popupHeight/2);
			
			ImU32 bgColor = Config::AmmoWheel::PopupUseCustomColor 
				? Config::AmmoWheel::PopupBackgroundColor 
				: (Config::AmmoWheel::UseSkyrimTheme 
					? Config::AmmoWheel::SkyrimTheme::BgDarkLayer 
					: IM_COL32(20, 15, 10, 220));
			uint8_t baseBgA = static_cast<uint8_t>(bgColor >> 24);
			uint8_t bgAlpha = static_cast<uint8_t>(baseBgA * popupOpacityMult * alpha);
			bgColor = (bgColor & 0x00FFFFFF) | (bgAlpha << 24);
			
			ImGui::GetWindowDrawList()->AddRectFilled(popupMin, popupMax, bgColor, bubbleRadius * 0.3f);
		}
	}
	
	// Add dark vignette ring for text visibility (before border rings)
	// This ensures text near the edge is readable even when overlapping transparent flipbook borders
	if (Config::AmmoWheel::PopupCircular) {
		// Dark vignette ring around the edge (wider, darker background for text)
		ImU32 vignetteColor = IM_COL32(10, 8, 5, static_cast<int>(180 * alpha));
		ImGui::GetWindowDrawList()->AddCircle(popupCenter, animatedRadius - 8.0f, vignetteColor, 48, 16.0f);
	}
	
	// Always draw border rings (on top of texture or primitive background)
	if (Config::AmmoWheel::PopupCircular) {
		// Gold border ring
		ImU32 borderColor = IM_COL32(218, 165, 32, static_cast<int>(200 * alpha));
		ImGui::GetWindowDrawList()->AddCircle(popupCenter, animatedRadius, borderColor, 48, 2.5f);
		
		// Inner glow ring
		ImU32 glowColor = IM_COL32(255, 215, 0, static_cast<int>(60 * alpha));
		ImGui::GetWindowDrawList()->AddCircle(popupCenter, animatedRadius - 4.0f, glowColor, 48, 1.5f);
	} else {
		float popupWidth = bubbleRadius * 2.0f;
		float popupHeight = bubbleRadius * 2.0f;
		ImVec2 popupMin(popupCenter.x - popupWidth/2, popupCenter.y - popupHeight/2);
		ImVec2 popupMax(popupCenter.x + popupWidth/2, popupCenter.y + popupHeight/2);
		
		// Dark vignette for rectangular popup
		ImU32 vignetteColor = IM_COL32(10, 8, 5, static_cast<int>(180 * alpha));
		ImGui::GetWindowDrawList()->AddRect(
			ImVec2(popupMin.x + 8, popupMin.y + 8),
			ImVec2(popupMax.x - 8, popupMax.y - 8),
			vignetteColor, bubbleRadius * 0.3f, 0, 16.0f);
		
		ImU32 borderColor = IM_COL32(200, 160, 50, static_cast<int>(180 * alpha));
		ImGui::GetWindowDrawList()->AddRect(popupMin, popupMax, borderColor, bubbleRadius * 0.3f, 0, 2.0f);
	}
	
	// Phase 2E: Apply padding to content area
	float contentRadius = animatedRadius - padding;  // Effective content area
	
	// Draw magnified icon (centered in upper portion of bubble)
	if (entry.iconImage.texture) {
		float magnifiedIconSize = (std::min)(iconSize * 1.15f, contentRadius * 1.2f);  // Clamp to content area
		ImVec2 iconPos(popupCenter.x, popupCenter.y - contentRadius * 0.15f);
		ImU32 iconColor = IM_COL32(255, 255, 255, static_cast<int>(255 * alpha));
		
		DrawArgs popupArgs = a_drawArgs;
		popupArgs.alphaMult = alpha;
		
		Drawer::draw_texture(
			entry.iconImage.texture,
			iconPos,
			0, 0,
			ImVec2(magnifiedIconSize, magnifiedIconSize),
			iconColor,
			popupArgs
		);
	}
	
	// Draw name text (wrapped to fit bubble width minus padding)
	float maxTextWidth = contentRadius * 1.6f;
	TextLayout nameLayout = wrapTextForSlot(entry.ammo->GetName(), maxTextWidth, nameSize, 2);
	
	ImU32 textColor = IM_COL32(240, 230, 210, static_cast<int>(255 * alpha));
	DrawArgs popupArgs = a_drawArgs;
	popupArgs.alphaMult = alpha;
	
	// Calculate dynamic spacing to fit content in bubble
	float totalTextHeight = nameLayout.lines.size() * nameSize;
	float countHeight = Config::AmmoWheel::ShowAmmoCount ? countSize : 0.0f;
	float totalContentHeight = totalTextHeight + countHeight;
	
	// Calculate available space in bubble (accounting for icon space)
	float availableTextHeight = contentRadius * 1.4f; // Lower portion of bubble for text
	
	// Dynamic line spacing based on content vs available space
	float dynamicLineSpacing = nameSize; // Base spacing
	float dynamicCountSpacing = 3.0f; // Base spacing between name and count
	
	if (totalContentHeight > availableTextHeight) {
		// Scale down spacing to fit
		float scaleFactor = availableTextHeight / totalContentHeight;
		dynamicLineSpacing = nameSize * scaleFactor * 0.9f; // 0.9f to ensure some margin
		dynamicCountSpacing = 3.0f * scaleFactor;
	}
	
	// Center text block in available space
	float textStartY = popupCenter.y + animatedRadius * 0.35f;
	
	// Draw name lines with dynamic spacing
	for (size_t i = 0; i < nameLayout.lines.size(); i++) {
		Drawer::draw_text(
			popupCenter.x,
			textStartY + i * dynamicLineSpacing,
			nameLayout.lines[i].c_str(),
			textColor,
			nameSize,
			popupArgs
		);
	}
	
	// Draw count below name with dynamic spacing
	if (Config::AmmoWheel::ShowAmmoCount) {
		std::string countStr = fmt::format("x{}", entry.count);
		float countY = textStartY + nameLayout.lines.size() * dynamicLineSpacing + dynamicCountSpacing;
		Drawer::draw_text(
			popupCenter.x,
			countY,
			countStr.c_str(),
			IM_COL32(200, 200, 200, static_cast<int>(200 * alpha)),
			countSize,
			popupArgs
		);
	}
}

ImVec2 AmmoWheel::calculateScreenPosition() const
{
	ImVec2 displaySize = ResolutionScale::Context::GetSingleton().GetRenderSize();

	switch (static_cast<ScreenAnchor>(Config::AmmoWheel::ScreenAnchorIndex)) {
	case ScreenAnchor::TopLeft:
		return ImVec2(displaySize.x * 0.15f, displaySize.y * 0.25f);
	case ScreenAnchor::TopRight:
		return ImVec2(displaySize.x * 0.85f, displaySize.y * 0.25f);
	case ScreenAnchor::BottomLeft:
		return ImVec2(displaySize.x * 0.15f, displaySize.y * 0.75f);
	case ScreenAnchor::BottomRight:
		return ImVec2(displaySize.x * 0.85f, displaySize.y * 0.75f);
	case ScreenAnchor::Center:
		return ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f);
	case ScreenAnchor::Custom:
	default:
		return ImVec2(
			displaySize.x * (Config::AmmoWheel::PositionX / 100.0f),
			displaySize.y * (Config::AmmoWheel::PositionY / 100.0f)
		);
	}
}

ImVec2 AmmoWheel::calculateSlotCenter(int a_index, ImVec2 a_wheelCenter, float a_radius) const
{
	int numEntries = static_cast<int>(_ammoEntries.size());
	if (numEntries == 0) {
		return a_wheelCenter;
	}

	float arcAngle = getArcAngleRad();
	float startAngle = getStartAngleRad();
	float slotAngle = arcAngle / static_cast<float>(numEntries);
	float midAngle = startAngle + (a_index + 0.5f) * slotAngle;

	return ImVec2(
		a_wheelCenter.x + a_radius * std::cos(midAngle),
		a_wheelCenter.y + a_radius * std::sin(midAngle)
	);
}

int AmmoWheel::getHoveredIndex(ImVec2 a_wheelCenter, float a_cursorAngle) const
{
	int numEntries = static_cast<int>(_ammoEntries.size());
	if (numEntries == 0) {
		return -1;
	}

	float arcAngle = getArcAngleRad();
	float startAngle = getStartAngleRad();
	float slotAngle = arcAngle / static_cast<float>(numEntries);
	float slotGapRad = Config::AmmoWheel::SlotGapDeg * (IM_PI / 180.0f);
	float effectiveSlotAngle = slotAngle - slotGapRad;  // Usable arc per slot
	
	// ========== NEAREST-SLOT-CENTER ALGORITHM ==========
	// This fixes reverse hovering by computing angular distance to each slot's center,
	// using wrap-aware math that matches the visual slot order regardless of angle direction.
	
	// Helper: wrap-aware angular distance (always in [0, PI])
	auto wrapDistance = [](float a, float b) -> float {
		float diff = std::fmod(a - b + 3.0f * IM_PI, 2.0f * IM_PI) - IM_PI;
		return std::abs(diff);
	};
	
	// Find the slot with the smallest angular distance from cursor
	int candidateIndex = -1;
	float candidateDist = 999.0f;
	
	for (int i = 0; i < numEntries; ++i) {
		// Compute slot center angle (same formula as used for drawing)
		float slotStartAngle = startAngle + slotAngle * static_cast<float>(i) + slotGapRad * 0.5f;
		float slotCenterAngle = slotStartAngle + effectiveSlotAngle * 0.5f;
		
		float dist = wrapDistance(a_cursorAngle, slotCenterAngle);
		if (dist < candidateDist) {
			candidateDist = dist;
			candidateIndex = i;
		}
	}
	
	// ========== DEADBAND AND GAP HANDLING ==========
	// If cursor is too far from ANY slot center (in gap region), treat as no hover or use hysteresis
	float deadbandRad = Config::AmmoWheel::ArcSelectionDeadbandDeg * (IM_PI / 180.0f);
	float effectiveHalf = (effectiveSlotAngle * 0.5f) - deadbandRad;
	
	// ========== HYSTERESIS ==========
	// Don't switch slots unless the new candidate is significantly closer than the previous
	float hysteresisRad = Config::AmmoWheel::GamepadHoverHysteresisDeg * (IM_PI / 180.0f);
	
	// Get distance to previous slot's center
	float prevDist = 999.0f;
	if (_prevHoveredIndex >= 0 && _prevHoveredIndex < numEntries) {
		float prevSlotStart = startAngle + slotAngle * static_cast<float>(_prevHoveredIndex) + slotGapRad * 0.5f;
		float prevSlotCenter = prevSlotStart + effectiveSlotAngle * 0.5f;
		prevDist = wrapDistance(a_cursorAngle, prevSlotCenter);
	}
	
	// Debug logging (only if enabled)
	if (Config::AmmoWheel::DebugLogNavigation) {
		static int lastLoggedIndex = -1;
		static int lastLoggedCandidate = -1;
		if (candidateIndex != lastLoggedCandidate || _prevHoveredIndex != lastLoggedIndex) {
			logger::info("[AmmoWheel Hover] cursorAngle={:.2f}deg, candidate={}, candidateDist={:.2f}deg, prev={}, prevDist={:.2f}deg, hysteresis={:.2f}deg",
				a_cursorAngle * (180.0f / IM_PI),
				candidateIndex,
				candidateDist * (180.0f / IM_PI),
				_prevHoveredIndex,
				prevDist * (180.0f / IM_PI),
				hysteresisRad * (180.0f / IM_PI));
			lastLoggedIndex = _prevHoveredIndex;
			lastLoggedCandidate = candidateIndex;
		}
	}
	
	// Apply hysteresis: stick to previous slot unless new is clearly closer
	if (_prevHoveredIndex >= 0 && _prevHoveredIndex < numEntries) {
		// Check if we're in a gap zone but previous is still valid
		if (candidateDist > effectiveHalf && prevDist <= effectiveHalf + hysteresisRad) {
			// In gap region but previous slot is valid - keep previous
			return _prevHoveredIndex;
		}
		
		// Check if new candidate is sufficiently closer than previous
		if (candidateIndex != _prevHoveredIndex) {
			if (candidateDist + hysteresisRad >= prevDist) {
				// New slot is not significantly closer - keep previous
				return _prevHoveredIndex;
			}
		}
	}
	
	// If candidate is in gap zone (too far from center), return no hover
	// unless this is the first hover
	if (candidateDist > effectiveHalf && _prevHoveredIndex < 0) {
		// First hover but cursor is in gap - clamp to nearest edge slot for partial arcs
		if (arcAngle < 2.0f * IM_PI) {
			// Normalize cursor angle relative to arc
			float normalizedAngle = a_cursorAngle - startAngle;
			while (normalizedAngle < 0) normalizedAngle += 2.0f * IM_PI;
			while (normalizedAngle >= 2.0f * IM_PI) normalizedAngle -= 2.0f * IM_PI;
			
			if (normalizedAngle > arcAngle) {
				// Outside arc - clamp to nearest edge
				float angleToEnd = normalizedAngle - arcAngle;
				float angleToStart = 2.0f * IM_PI - normalizedAngle;
				return (angleToEnd < angleToStart) ? numEntries - 1 : 0;
			}
		}
		return candidateIndex;  // Accept candidate even if slightly in gap for first hover
	}
	
	return candidateIndex;
}

float AmmoWheel::getCursorAngle() const
{
	return std::atan2(_cursorPos.y, _cursorPos.x);
}

float AmmoWheel::getArcAngleRad() const
{
	switch (static_cast<WheelShape>(Config::AmmoWheel::WheelShapeIndex)) {
	case WheelShape::HalfCircle:
		return IM_PI;
	case WheelShape::QuarterCircle:
		return IM_PI / 2.0f;
	case WheelShape::FullCircle:
	default:
		return 2.0f * IM_PI;
	}
}

float AmmoWheel::getStartAngleRad() const
{
	return Config::AmmoWheel::ArcStartAngle * (IM_PI / 180.0f);
}

// ========== TASK 2: Adaptive center panel positioning ==========
ImVec2 AmmoWheel::calculateCenterPanelPosition(ImVec2 a_wheelCenter) const
{
	float arcSpan = getArcAngleRad();
	float arcStart = getStartAngleRad();
	float arcMid = arcStart + arcSpan * 0.5f;
	
	// For full circle, center is optimal
	if (arcSpan >= 2.0f * IM_PI * 0.95f) {
		return a_wheelCenter;
	}
	
	// For partial arcs, bias panel toward the arc's usable area
	float insetFactor = Config::AmmoWheel::CenterPanelInsetRatio;
	float pushDistance = _cachedInnerRadius * insetFactor;
	
	// Push panel toward arc midpoint (into the visible arc area)
	ImVec2 biasedCenter = {
		a_wheelCenter.x + pushDistance * std::cos(arcMid),
		a_wheelCenter.y + pushDistance * std::sin(arcMid)
	};
	
	// Clamp to viewport safe area
	ImVec2 viewport = ResolutionScale::Context::GetSingleton().GetRenderSize();
	float margin = Config::AmmoWheel::CenterPanelSafeMargin;
	
	// Estimate panel dimensions for clamping
	float panelWidth = _cachedInnerRadius * Config::AmmoWheel::CenterMaxWidthRatio * 2.0f;
	float panelHeight = Config::AmmoWheel::CenterFontPx * 5.0f;
	
	// Clamp X to keep panel on screen
	biasedCenter.x = std::clamp(biasedCenter.x, 
		margin + panelWidth / 2.0f, 
		viewport.x - margin - panelWidth / 2.0f);
	
	// Clamp Y to keep panel on screen
	biasedCenter.y = std::clamp(biasedCenter.y, 
		margin + panelHeight / 2.0f, 
		viewport.y - margin - panelHeight / 2.0f);
	
	return biasedCenter;
}

// ========== TASK 3: Multi-line text wrapping for slot labels ==========
TextLayout AmmoWheel::wrapTextForSlot(const char* text, float maxWidth, float fontSize, int maxLines)
{
	TextLayout result;
	result.maxWidth = maxWidth;
	
	if (!text || maxWidth <= 0.0f || maxLines <= 0) {
		if (text) result.lines.push_back(text);
		return result;
	}
	
	ImFont* font = ImGui::GetFont();
	if (!font) {
		result.lines.push_back(text);
		return result;
	}
	
	std::string remaining(text);
	std::vector<std::string> words;
	
	// Split by spaces
	size_t pos = 0;
	std::string temp = remaining;
	while ((pos = temp.find(' ')) != std::string::npos) {
		if (pos > 0) words.push_back(temp.substr(0, pos));
		temp.erase(0, pos + 1);
	}
	if (!temp.empty()) words.push_back(temp);
	
	// Handle single word case
	if (words.empty()) {
		result.lines.push_back(text);
		result.totalHeight = fontSize * 1.2f;
		return result;
	}
	
	// Greedy word wrapping
	std::string currentLine;
	for (size_t i = 0; i < words.size(); i++) {
		const std::string& word = words[i];
		std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
		ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, testLine.c_str());
		
		if (size.x <= maxWidth) {
			currentLine = testLine;
		} else {
			if (!currentLine.empty()) {
				result.lines.push_back(currentLine);
				currentLine = word;
			} else {
				// Single word too long - truncate it
				result.lines.push_back(TruncateTextToFit(word.c_str(), maxWidth, fontSize));
				currentLine = "";
			}
		}
		
		// Stop if we've reached max lines (save one for remaining)
		if (static_cast<int>(result.lines.size()) >= maxLines - 1 && i < words.size() - 1) {
			// Add remaining words to current line with ellipsis
			for (size_t j = i + 1; j < words.size(); j++) {
				currentLine += " " + words[j];
			}
			break;
		}
	}
	
	// Add final line
	if (!currentLine.empty()) {
		// Check if we need ellipsis
		if (static_cast<int>(result.lines.size()) >= maxLines - 1) {
			ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, currentLine.c_str());
			if (size.x > maxWidth) {
				currentLine = TruncateTextToFit(currentLine.c_str(), maxWidth - 20.0f, fontSize) + "...";
			}
		}
		result.lines.push_back(currentLine);
	}
	
	result.totalHeight = result.lines.size() * fontSize * 1.2f;
	return result;
}

// ========== PHASE 1: Edge-aware word wrapping for center panel text ==========
TextLayout AmmoWheel::wrapTextForCenterPanel(const char* text, float fontSize, ImVec2 panelCenter) const
{
	TextLayout result;
	
	if (!text || !Config::AmmoWheel::EnableWordWrap) {
		if (text) result.lines.push_back(text);
		result.totalHeight = fontSize * 1.2f;
		return result;
	}
	
	ImFont* font = ImGui::GetFont();
	if (!font) {
		result.lines.push_back(text);
		result.totalHeight = fontSize * 1.2f;
		return result;
	}
	
	// Calculate max width based on screen position and safe margins
	ImVec2 viewport = ResolutionScale::Context::GetSingleton().GetRenderSize();
	float safeMargin = Config::AmmoWheel::WrapSafeMarginPx;
	
	// Determine available width based on panel position
	float availableLeft = panelCenter.x - safeMargin;
	float availableRight = viewport.x - panelCenter.x - safeMargin;
	float availableWidth = (std::min)(availableLeft, availableRight) * 2.0f;
	
	// Apply configured max width
	float configMaxWidth = Config::AmmoWheel::WrapMaxLineWidthPx;
	if (configMaxWidth <= 0.0f) {
		configMaxWidth = viewport.x * Config::AmmoWheel::WrapMaxLineWidthRatio;
	}
	
	float maxWidth = (std::min)(availableWidth, configMaxWidth);
	result.maxWidth = maxWidth;
	
	// Check if text fits without wrapping
	ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
	if (textSize.x <= maxWidth) {
		result.lines.push_back(text);
		result.totalHeight = fontSize * 1.2f;
		return result;
	}
	
	// Word wrapping
	std::string remaining(text);
	std::vector<std::string> words;
	
	// Split by spaces
	size_t pos = 0;
	std::string temp = remaining;
	while ((pos = temp.find(' ')) != std::string::npos) {
		if (pos > 0) words.push_back(temp.substr(0, pos));
		temp.erase(0, pos + 1);
	}
	if (!temp.empty()) words.push_back(temp);
	
	if (words.empty()) {
		result.lines.push_back(text);
		result.totalHeight = fontSize * 1.2f;
		return result;
	}
	
	int maxLines = Config::AmmoWheel::WrapMaxLines;
	bool addHyphen = Config::AmmoWheel::AddWrapHyphen;
	bool preferWordBoundary = Config::AmmoWheel::WrapAtWordBoundary;
	
	// Greedy word wrapping
	std::string currentLine;
	for (size_t i = 0; i < words.size(); i++) {
		const std::string& word = words[i];
		std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
		ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, testLine.c_str());
		
		if (size.x <= maxWidth) {
			currentLine = testLine;
		} else {
			if (!currentLine.empty()) {
				// Add hyphen if configured and not at word boundary
				if (addHyphen && !preferWordBoundary) {
					currentLine += "-";
				}
				result.lines.push_back(currentLine);
				currentLine = word;
			} else {
				// Single word too long - truncate or break it
				if (preferWordBoundary) {
					result.lines.push_back(TruncateTextToFit(word.c_str(), maxWidth, fontSize));
				} else {
					// Break word with hyphen
					std::string partial;
					for (char c : word) {
						std::string test = partial + c;
						ImVec2 testSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, (test + "-").c_str());
						if (testSize.x > maxWidth && !partial.empty()) {
							result.lines.push_back(partial + "-");
							partial = std::string(1, c);
						} else {
							partial = test;
						}
					}
					currentLine = partial;
				}
			}
		}
		
		// Stop if we've reached max lines
		if (static_cast<int>(result.lines.size()) >= maxLines - 1 && i < words.size() - 1) {
			// Add remaining words to current line with ellipsis
			for (size_t j = i + 1; j < words.size(); j++) {
				currentLine += " " + words[j];
			}
			break;
		}
	}
	
	// Add final line
	if (!currentLine.empty()) {
		if (static_cast<int>(result.lines.size()) >= maxLines - 1) {
			ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, currentLine.c_str());
			if (size.x > maxWidth) {
				currentLine = TruncateTextToFit(currentLine.c_str(), maxWidth - 20.0f, fontSize) + "...";
			}
		}
		result.lines.push_back(currentLine);
	}
	
	result.totalHeight = result.lines.size() * fontSize * 1.2f;
	return result;
}

// ========== PHASE 4: Clamp cursor angle to arc bounds for half-wheel mode ==========
float AmmoWheel::clampAngleToArc(float angle) const
{
	if (Config::AmmoWheel::HalfWheelClampMode == 0) {
		return angle;  // No clamping
	}
	
	float arcSpan = getArcAngleRad();
	if (arcSpan >= 2.0f * IM_PI * 0.95f) {
		return angle;  // Full circle, no clamping needed
	}
	
	float arcStart = getStartAngleRad();
	float arcEnd = arcStart + arcSpan;
	float deadband = Config::AmmoWheel::ArcSelectionDeadbandDeg * (IM_PI / 180.0f);
	
	// Normalize angle to [0, 2*PI)
	while (angle < 0) angle += 2.0f * IM_PI;
	while (angle >= 2.0f * IM_PI) angle -= 2.0f * IM_PI;
	
	// Normalize arc bounds
	float normStart = arcStart;
	while (normStart < 0) normStart += 2.0f * IM_PI;
	while (normStart >= 2.0f * IM_PI) normStart -= 2.0f * IM_PI;
	
	float normEnd = normStart + arcSpan;
	
	// Check if angle is within arc (with deadband)
	bool inArc = false;
	if (normEnd <= 2.0f * IM_PI) {
		inArc = (angle >= normStart - deadband && angle <= normEnd + deadband);
	} else {
		// Arc wraps around 0
		inArc = (angle >= normStart - deadband || angle <= (normEnd - 2.0f * IM_PI) + deadband);
	}
	
	if (inArc) {
		return angle;
	}
	
	// Clamp to nearest edge
	float distToStart = std::abs(angle - normStart);
	float distToEnd = std::abs(angle - normEnd);
	if (distToStart > IM_PI) distToStart = 2.0f * IM_PI - distToStart;
	if (distToEnd > IM_PI) distToEnd = 2.0f * IM_PI - distToEnd;
	
	if (Config::AmmoWheel::HalfWheelClampMode == 1) {
		// ClampAngle mode - snap to nearest arc edge
		return (distToStart < distToEnd) ? normStart : (normEnd > 2.0f * IM_PI ? normEnd - 2.0f * IM_PI : normEnd);
	} else {
		// SnapToEdgeSlot mode - return edge angle (will be converted to slot index)
		return (distToStart < distToEnd) ? normStart : normEnd;
	}
}

// ========== PHASE 2: Reset navigation filters ==========
void AmmoWheel::ResetNavigationFilters()
{
	_mouseFilter.Reset();
	_gamepadFilter.Reset();
	_cursorPos = {0, 0};
	
	if (Config::AmmoWheel::DebugLogNavigation) {
		logger::info("[AmmoWheel] Navigation filters reset");
	}
}

// ========== TASK 1: Mouse button handling - block attack when wheel open ==========
bool AmmoWheel::HandleMouseButton(int button, bool pressed)
{
	if (!IsOpen()) {
		return false;
	}
	
	// Master toggle for attack blocking
	if (!Config::AmmoWheel::BlockAttackWhenOpen) {
		return false;
	}
	
	// LMB handling
	if (button == 0) {
		if (!Config::AmmoWheel::ConsumeLMBWhenOpen) {
			return false;  // Don't consume LMB if disabled
		}
		
		if (pressed) {
			// Check if we require a valid hover to select
			bool hasValidHover = (_hoveredIndex >= 0 && _hoveredIndex < static_cast<int>(_ammoEntries.size()));
			
			if (Config::AmmoWheel::ClickSelectRequiresHover && !hasValidHover) {
				// No valid hover - still consume to block attack, but don't select
				logger::debug("AmmoWheel: LMB blocked (no valid hover)");
				return true;
			}
			
			if (hasValidHover) {
				// Select the hovered ammo
				ActivateHoveredAmmo();
				logger::info("AmmoWheel: LMB selected ammo at index {}", _hoveredIndex);
				
				// Close if configured (works in both RTU and fixed-open modes)
				if (Config::AmmoWheel::CloseOnSelection) {
					Close();
				}
			}
		}
		return true;  // Always consume LMB to prevent bow attack
	}
	
	// RMB handling
	if (button == 1) {
		if (!Config::AmmoWheel::ConsumeRMBWhenOpen) {
			return false;  // Don't consume RMB if disabled
		}
		
		if (pressed && Config::AmmoWheel::AllowRMBUnequip) {
			// Unequip current ammo
			auto player = RE::PlayerCharacter::GetSingleton();
			auto equipManager = RE::ActorEquipManager::GetSingleton();
			if (player && equipManager) {
				auto currentAmmo = player->GetCurrentAmmo();
				if (currentAmmo) {
					equipManager->UnequipObject(player, currentAmmo);
					logger::info("AmmoWheel: Unequipped ammo via RMB");
				}
			}
		}
		return true;  // Consume RMB to prevent block/power attack
	}
	
	return false;
}

// ========== HOVER SLOT SOUND ==========
void AmmoWheel::PlayHoverSlotSound(int newHoveredIndex)
{
	// Master toggle check
	if (!Config::AmmoWheel::Sounds::EnableHoverSlotSound) {
		return;
	}
	
	// Must have a valid hovered slot
	if (newHoveredIndex < 0) {
		return;
	}
	
	// Only on slot change check
	if (Config::AmmoWheel::Sounds::HoverSlotSoundOnlyOnSlotChange) {
		if (newHoveredIndex == _lastHoverSoundIndex) {
			return;
		}
	}
	
	// Cooldown check (spam prevention)
	auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	
	if (nowMs - _lastHoverSoundTimeMs < Config::AmmoWheel::Sounds::HoverSlotSoundCooldownMs) {
		if (Config::AmmoWheel::Sounds::DebugLogHoverSound) {
			logger::debug("[AmmoWheel Sound] Skipped - cooldown ({} ms remaining)",
				Config::AmmoWheel::Sounds::HoverSlotSoundCooldownMs - (nowMs - _lastHoverSoundTimeMs));
		}
		return;
	}
	
	// Determine which EditorID to use
	const char* editorID = nullptr;
	if (!Config::AmmoWheel::Sounds::HoverSlotSoundEditorID.empty()) {
		editorID = Config::AmmoWheel::Sounds::HoverSlotSoundEditorID.c_str();
	} else {
		// Fallback to main Wheeler hover sound
		editorID = Config::Sounds::HoverSoundEditorID.c_str();
	}
	
	if (!editorID || editorID[0] == '\0') {
		if (Config::AmmoWheel::Sounds::DebugLogHoverSound) {
			logger::warn("[AmmoWheel Sound] No EditorID configured");
		}
		return;
	}
	
	// Check master sound toggle
	if (!Config::Sounds::EnableSounds) {
		return;
	}
	
	// Play the sound using BSAudioManager (same pattern as main Wheeler)
	RE::BSSoundHandle handle;
	handle.soundID = static_cast<uint32_t>(-1);
	handle.assumeSuccess = false;
	
	auto* audioManager = RE::BSAudioManager::GetSingleton();
	if (audioManager) {
		audioManager->BuildSoundDataFromEditorID(handle, editorID, 0x10);
		if (handle.IsValid()) {
			handle.SetVolume(Config::AmmoWheel::Sounds::HoverSlotSoundVolume);
			handle.Play();
			
			// Update state for spam prevention
			_lastHoverSoundIndex = newHoveredIndex;
			_lastHoverSoundTimeMs = nowMs;
			
			if (Config::AmmoWheel::Sounds::DebugLogHoverSound) {
				logger::info("[AmmoWheel Sound] Played '{}' at volume {:.2f} for slot {}",
					editorID, Config::AmmoWheel::Sounds::HoverSlotSoundVolume, newHoveredIndex);
			}
		} else {
			if (Config::AmmoWheel::Sounds::DebugLogHoverSound) {
				logger::warn("[AmmoWheel Sound] Failed to build sound from EditorID '{}'", editorID);
			}
		}
	}
}
