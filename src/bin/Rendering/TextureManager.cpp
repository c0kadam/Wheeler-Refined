#include "TextureManager.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <set>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <wincodec.h>
#include <wrl/client.h>
#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "include/lib/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "include/lib/nanosvgrast.h"

extern "C" {
	unsigned char* stbi_load_from_memory(unsigned char const* buffer, int len, int* x, int* y, int* channels_in_file, int desired_channels);
	void stbi_image_free(void* retval_from_stbi_load);
	const char* stbi_failure_reason(void);
}

namespace
{
	bool starts_with_insensitive(std::string_view value, std::string_view prefix)
	{
		if (value.size() < prefix.size()) {
			return false;
		}

		for (std::size_t i = 0; i < prefix.size(); ++i) {
			if (std::tolower(static_cast<unsigned char>(value[i])) !=
				std::tolower(static_cast<unsigned char>(prefix[i]))) {
				return false;
			}
		}

		return true;
	}

	std::filesystem::path normalize_external_asset_path(std::string_view rawPath)
	{
		std::filesystem::path parsedPath(rawPath);
		if (parsedPath.is_absolute()) {
			return parsedPath.lexically_normal();
		}

		std::string normalized(rawPath);
		std::replace(normalized.begin(), normalized.end(), '/', '\\');
		while (starts_with_insensitive(normalized, ".\\")) {
			normalized.erase(0, 2);
		}

		std::string lowered = normalized;
		std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});

		// Wheeler-owned OStim resources live under
		// Data\SKSE\Plugins\wheeler\resources\ostim\icons\...
		// and must not be remapped into OStim's Interface icon root.
		if (lowered.find("data\\skse\\plugins\\wheeler\\resources\\ostim\\icons\\") != std::string::npos ||
		    lowered.find("skse\\plugins\\wheeler\\resources\\ostim\\icons\\") != std::string::npos ||
		    lowered.find("wheeler\\resources\\ostim\\icons\\") != std::string::npos) {
			return std::filesystem::path(normalized).lexically_normal();
		}

		const auto trimToMarker = [&](std::string_view marker, std::string_view replacementPrefix) -> std::optional<std::filesystem::path> {
			const auto offset = lowered.find(std::string(marker));
			if (offset == std::string::npos) {
				return std::nullopt;
			}

			std::string rebuilt(replacementPrefix);
			rebuilt += normalized.substr(offset + marker.size());
			return std::filesystem::path(rebuilt).lexically_normal();
		};

		if (auto trimmed = trimToMarker("data\\interface\\ostim\\icons\\", "Data\\Interface\\OStim\\icons\\"); trimmed.has_value()) {
			return *trimmed;
		}
		if (auto trimmed = trimToMarker("interface\\ostim\\icons\\", "Data\\Interface\\OStim\\icons\\"); trimmed.has_value()) {
			return *trimmed;
		}
		if (auto trimmed = trimToMarker("ostim\\icons\\", "Data\\Interface\\OStim\\icons\\"); trimmed.has_value()) {
			return *trimmed;
		}

		return std::filesystem::path(normalized).lexically_normal();
	}

	std::string normalize_texture_cache_path(const std::filesystem::path& path)
	{
		std::string out = path.lexically_normal().string();
		for (char& c : out) {
			if (c == '/') {
				c = '\\';
			} else {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
		}
		return out;
	}

	void release_cached_images(std::unordered_map<std::string, Texture::Image>& cache)
	{
		for (auto& [_, image] : cache) {
			if (image.texture) {
				image.texture->Release();
				image.texture = nullptr;
			}
			image.width = 0;
			image.height = 0;
		}
		cache.clear();
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

	bool LoadRasterImageWithWIC(
		const std::filesystem::path& a_path,
		int& a_outWidth,
		int& a_outHeight,
		std::vector<unsigned char>& a_outPixels)
	{
		a_outWidth = 0;
		a_outHeight = 0;
		a_outPixels.clear();

		const HRESULT initResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool shouldUninitialize = SUCCEEDED(initResult);
		if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) {
			logger::error("WIC init failed for '{}' (hr=0x{:08X})", a_path.string(), static_cast<std::uint32_t>(initResult));
			return false;
		}

		struct CoUninitGuard
		{
			bool active = false;
			~CoUninitGuard()
			{
				if (active) {
					CoUninitialize();
				}
			}
		} coUninitGuard{ shouldUninitialize };

		Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
		const HRESULT factoryResult = CoCreateInstance(
			CLSID_WICImagingFactory,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
		if (FAILED(factoryResult) || !factory) {
			logger::error("WIC factory creation failed for '{}' (hr=0x{:08X})", a_path.string(), static_cast<std::uint32_t>(factoryResult));
			return false;
		}

		Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
		const std::wstring widePath = a_path.wstring();
		const HRESULT decoderResult = factory->CreateDecoderFromFilename(
			widePath.c_str(),
			nullptr,
			GENERIC_READ,
			WICDecodeMetadataCacheOnDemand,
			decoder.ReleaseAndGetAddressOf());
		if (FAILED(decoderResult) || !decoder) {
			logger::error("WIC decoder creation failed for '{}' (hr=0x{:08X})", a_path.string(), static_cast<std::uint32_t>(decoderResult));
			return false;
		}

		Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
		const HRESULT frameResult = decoder->GetFrame(0, frame.ReleaseAndGetAddressOf());
		if (FAILED(frameResult) || !frame) {
			logger::error("WIC frame decode failed for '{}' (hr=0x{:08X})", a_path.string(), static_cast<std::uint32_t>(frameResult));
			return false;
		}

		UINT width = 0;
		UINT height = 0;
		const HRESULT sizeResult = frame->GetSize(&width, &height);
		if (FAILED(sizeResult) || width == 0 || height == 0) {
			logger::error("WIC size query failed for '{}' (hr=0x{:08X})", a_path.string(), static_cast<std::uint32_t>(sizeResult));
			return false;
		}

		const std::size_t rowPitch = static_cast<std::size_t>(width) * 4u;
		const std::size_t pixelBytes = rowPitch * static_cast<std::size_t>(height);
		if (rowPitch / 4u != width || pixelBytes == 0) {
			logger::error("WIC image size overflow for '{}' ({}x{})", a_path.string(), width, height);
			return false;
		}

		Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
		const HRESULT converterResult = factory->CreateFormatConverter(converter.ReleaseAndGetAddressOf());
		if (FAILED(converterResult) || !converter) {
			logger::error("WIC format converter creation failed for '{}' (hr=0x{:08X})", a_path.string(), static_cast<std::uint32_t>(converterResult));
			return false;
		}

		const HRESULT initializeResult = converter->Initialize(
			frame.Get(),
			GUID_WICPixelFormat32bppRGBA,
			WICBitmapDitherTypeNone,
			nullptr,
			0.0,
			WICBitmapPaletteTypeCustom);
		if (FAILED(initializeResult)) {
			logger::error("WIC format conversion failed for '{}' (hr=0x{:08X})", a_path.string(), static_cast<std::uint32_t>(initializeResult));
			return false;
		}

		a_outPixels.resize(pixelBytes);
		const HRESULT copyResult = converter->CopyPixels(
			nullptr,
			static_cast<UINT>(rowPitch),
			static_cast<UINT>(pixelBytes),
			a_outPixels.data());
		if (FAILED(copyResult)) {
			logger::error("WIC pixel copy failed for '{}' (hr=0x{:08X})", a_path.string(), static_cast<std::uint32_t>(copyResult));
			a_outPixels.clear();
			return false;
		}

		a_outWidth = static_cast<int>(width);
		a_outHeight = static_cast<int>(height);
		return true;
	}

	bool DecodeRasterImageToRgba(
		const std::filesystem::path& a_path,
		int& a_outWidth,
		int& a_outHeight,
		std::vector<unsigned char>& a_outPixels)
	{
		a_outWidth = 0;
		a_outHeight = 0;
		a_outPixels.clear();

		std::string ext = a_path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});

		if (ext == ".png") {
			std::ifstream file(a_path, std::ios::binary);
			if (!file) {
				logger::error("Failed to open PNG '{}'", a_path.string());
				return false;
			}

			std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			if (bytes.empty()) {
				logger::error("PNG file '{}' is empty", a_path.string());
				return false;
			}

			int channels = 0;
			unsigned char* decoded = stbi_load_from_memory(
				bytes.data(),
				static_cast<int>(bytes.size()),
				&a_outWidth,
				&a_outHeight,
				&channels,
				4);
			if (!decoded || a_outWidth <= 0 || a_outHeight <= 0) {
				logger::error(
					"stbi_load_from_memory failed for '{}' ({})",
					a_path.string(),
					stbi_failure_reason() ? stbi_failure_reason() : "unknown");
				if (decoded) {
					stbi_image_free(decoded);
				}
				return false;
			}

			const std::size_t pixelBytes = static_cast<std::size_t>(a_outWidth) * static_cast<std::size_t>(a_outHeight) * 4u;
			a_outPixels.assign(decoded, decoded + pixelBytes);
			stbi_image_free(decoded);
			return true;
		}

		if (ext == ".dds") {
			if (!LoadRasterImageWithWIC(a_path, a_outWidth, a_outHeight, a_outPixels) ||
			    a_outWidth <= 0 ||
			    a_outHeight <= 0 ||
			    a_outPixels.empty()) {
				logger::error("WIC DDS decode failed for '{}'", a_path.string());
				return false;
			}
			return true;
		}

		return false;
	}

	float SampleAlphaBilinear(
		const std::vector<unsigned char>& a_alpha,
		int a_width,
		int a_height,
		float a_u,
		float a_v)
	{
		if (a_alpha.empty() || a_width <= 0 || a_height <= 0) {
			return 0.0f;
		}

		const float x = std::clamp(a_u, 0.0f, 1.0f) * static_cast<float>((std::max)(a_width - 1, 0));
		const float y = std::clamp(a_v, 0.0f, 1.0f) * static_cast<float>((std::max)(a_height - 1, 0));
		const int x0 = static_cast<int>(std::floor(x));
		const int y0 = static_cast<int>(std::floor(y));
		const int x1 = (std::min)(x0 + 1, a_width - 1);
		const int y1 = (std::min)(y0 + 1, a_height - 1);
		const float tx = x - static_cast<float>(x0);
		const float ty = y - static_cast<float>(y0);

		const auto alphaAt = [&](int a_px, int a_py) {
			return static_cast<float>(a_alpha[static_cast<std::size_t>(a_py) * static_cast<std::size_t>(a_width) + static_cast<std::size_t>(a_px)]) / 255.0f;
		};

		const float a00 = alphaAt(x0, y0);
		const float a10 = alphaAt(x1, y0);
		const float a01 = alphaAt(x0, y1);
		const float a11 = alphaAt(x1, y1);
		const float a0 = a00 + (a10 - a00) * tx;
		const float a1 = a01 + (a11 - a01) * tx;
		return a0 + (a1 - a0) * ty;
	}

	void ResizeRgbaIntoCanvas(
		const std::vector<unsigned char>& a_sourcePixels,
		int a_sourceWidth,
		int a_sourceHeight,
		int a_canvasSize,
		std::vector<unsigned char>& a_outCanvas)
	{
		a_outCanvas.assign(static_cast<std::size_t>(a_canvasSize) * static_cast<std::size_t>(a_canvasSize) * 4u, 0);
		if (a_sourcePixels.empty() || a_sourceWidth <= 0 || a_sourceHeight <= 0 || a_canvasSize <= 0) {
			return;
		}

		const float fitScale = (std::min)(
			static_cast<float>(a_canvasSize) / static_cast<float>(a_sourceWidth),
			static_cast<float>(a_canvasSize) / static_cast<float>(a_sourceHeight));
		if (!std::isfinite(fitScale) || fitScale <= 0.0f) {
			return;
		}

		const float destWidth = static_cast<float>(a_sourceWidth) * fitScale;
		const float destHeight = static_cast<float>(a_sourceHeight) * fitScale;
		const float offsetX = (static_cast<float>(a_canvasSize) - destWidth) * 0.5f;
		const float offsetY = (static_cast<float>(a_canvasSize) - destHeight) * 0.5f;

		for (int y = 0; y < a_canvasSize; ++y) {
			for (int x = 0; x < a_canvasSize; ++x) {
				const float srcXF = (static_cast<float>(x) + 0.5f - offsetX) / fitScale - 0.5f;
				const float srcYF = (static_cast<float>(y) + 0.5f - offsetY) / fitScale - 0.5f;
				if (srcXF < 0.0f || srcYF < 0.0f ||
				    srcXF > static_cast<float>(a_sourceWidth - 1) ||
				    srcYF > static_cast<float>(a_sourceHeight - 1)) {
					continue;
				}

				const int x0 = static_cast<int>(std::floor(srcXF));
				const int y0 = static_cast<int>(std::floor(srcYF));
				const int x1 = (std::min)(x0 + 1, a_sourceWidth - 1);
				const int y1 = (std::min)(y0 + 1, a_sourceHeight - 1);
				const float tx = srcXF - static_cast<float>(x0);
				const float ty = srcYF - static_cast<float>(y0);

				auto sample = [&](int a_px, int a_py, int a_channel) {
					return static_cast<float>(
						a_sourcePixels[(static_cast<std::size_t>(a_py) * static_cast<std::size_t>(a_sourceWidth) +
							static_cast<std::size_t>(a_px)) * 4u + static_cast<std::size_t>(a_channel)]);
				};

				const std::size_t dstIndex = (static_cast<std::size_t>(y) * static_cast<std::size_t>(a_canvasSize) +
					static_cast<std::size_t>(x)) * 4u;
				for (int c = 0; c < 4; ++c) {
					const float c00 = sample(x0, y0, c);
					const float c10 = sample(x1, y0, c);
					const float c01 = sample(x0, y1, c);
					const float c11 = sample(x1, y1, c);
					const float c0 = c00 + (c10 - c00) * tx;
					const float c1 = c01 + (c11 - c01) * tx;
					const float value = c0 + (c1 - c0) * ty;
					a_outCanvas[dstIndex + static_cast<std::size_t>(c)] =
						static_cast<unsigned char>(std::clamp(value, 0.0f, 255.0f));
				}
			}
		}
	}

	bool PointInNormalizedPolygon(const std::vector<ImVec2>& a_polygon, float a_x, float a_y)
	{
		if (a_polygon.size() < 3) {
			return false;
		}

		bool inside = false;
		std::size_t last = a_polygon.size() - 1;
		for (std::size_t i = 0; i < a_polygon.size(); last = i++) {
			const ImVec2& lhs = a_polygon[i];
			const ImVec2& rhs = a_polygon[last];
			const bool intersects = ((lhs.y > a_y) != (rhs.y > a_y)) &&
				(a_x < (rhs.x - lhs.x) * (a_y - lhs.y) / ((rhs.y - lhs.y) + 1.0e-6f) + lhs.x);
			if (intersects) {
				inside = !inside;
			}
		}

		return inside;
	}

	const std::vector<unsigned char>* GetFilledSlotPolygonMask(int a_canvasSize)
	{
		if (a_canvasSize <= 0) {
			return nullptr;
		}

		static std::unordered_map<int, std::vector<unsigned char>> s_polygonMaskCache;

		if (auto it = s_polygonMaskCache.find(a_canvasSize);
		    it != s_polygonMaskCache.end()) {
			return &it->second;
		}

		const auto* outline = Texture::GetSlotBackgroundOutline();
		if (!outline || outline->size() < 3) {
			return nullptr;
		}

		std::vector<unsigned char> mask(static_cast<std::size_t>(a_canvasSize) * static_cast<std::size_t>(a_canvasSize), 0);
		constexpr std::array<ImVec2, 4> kSubsamples{
			ImVec2(0.25f, 0.25f),
			ImVec2(0.75f, 0.25f),
			ImVec2(0.25f, 0.75f),
			ImVec2(0.75f, 0.75f)
		};

		for (int y = 0; y < a_canvasSize; ++y) {
			for (int x = 0; x < a_canvasSize; ++x) {
				int covered = 0;
				for (const auto& sample : kSubsamples) {
					const float u = (static_cast<float>(x) + sample.x) / static_cast<float>(a_canvasSize);
					const float v = (static_cast<float>(y) + sample.y) / static_cast<float>(a_canvasSize);
					if (PointInNormalizedPolygon(*outline, u, v)) {
						++covered;
					}
				}

				mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(a_canvasSize) + static_cast<std::size_t>(x)] =
					static_cast<unsigned char>((covered * 255) / static_cast<int>(kSubsamples.size()));
			}
		}

		auto [insertedIt, _] = s_polygonMaskCache.emplace(a_canvasSize, std::move(mask));
		return &insertedIt->second;
	}

	bool CreateTextureFromRgbaPixels(
		const unsigned char* a_pixels,
		int a_width,
		int a_height,
		ID3D11ShaderResourceView** a_outSrv)
	{
		if (!Texture::device_ || !a_pixels || !a_outSrv || a_width <= 0 || a_height <= 0) {
			return false;
		}

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = static_cast<UINT>(a_width);
		desc.Height = static_cast<UINT>(a_height);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA subResource{};
		subResource.pSysMem = a_pixels;
		subResource.SysMemPitch = static_cast<UINT>(a_width * 4);

		ID3D11Texture2D* texture = nullptr;
		HRESULT hr = Texture::device_->CreateTexture2D(&desc, &subResource, &texture);
		if (FAILED(hr) || !texture) {
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		hr = Texture::device_->CreateShaderResourceView(texture, &srvDesc, a_outSrv);
		texture->Release();
		return SUCCEEDED(hr) && *a_outSrv != nullptr;
	}

	static bool TryGetSvgAttribute(const std::string& header, const char* attr, std::string& outValue)
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

	static bool ParseLength(const std::string& value, float& outPx, bool& outPercent)
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

	static bool ParseViewBox(const std::string& value, float& outW, float& outH)
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

	static void ReplaceOrInsertAttr(std::string& header, const char* attr, const std::string& value)
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

	static bool NormalizeSvgText(std::string& svgText, SvgSizeInfo& info)
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

	static NSVGimage* ParseSvgFromFileNormalized(const char* filename, SvgSizeInfo& info)
	{
		std::ifstream file(filename, std::ios::binary);
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

	void build_slot_outline_from_svg(const NSVGimage* svg, std::vector<ImVec2>& out_outline);
}

void Texture::Init()
{
	logger::info("Texture::Init: loading base icons from '{}'", icon_directory);
	//load_images(image_type_name_map, image_struct, img_directory);
	load_images(icon_type_name_map, icon_struct, icon_directory);
	logger::info("Texture::Init: loading custom icons from '{}'", icon_custom_directory);
	load_custom_icon_images();
	//load_images(key_icon_name_map, key_struct, key_directory);
	//load_images(default_key_icon_name_map, default_key_struct, key_directory);
	//load_images(gamepad_ps_icon_name_map, ps_key_struct, key_directory);
	//load_images(gamepad_xbox_icon_name_map, xbox_key_struct, key_directory);
}
Texture::Image Texture::GetIconImage(icon_image_type a_imageType, RE::TESForm* a_form)
{
	// Helper to validate an Image has a usable texture
	auto isValidImage = [](const Image& img) -> bool {
		return img.texture != nullptr && img.width > 0 && img.height > 0;
	};
	
	// Get the default fallback image for this type
	auto getDefaultImage = [&]() -> Image {
		int32_t typeIndex = static_cast<int32_t>(a_imageType);
		if (icon_struct.contains(typeIndex)) {
			const Image& defaultImg = icon_struct[typeIndex];
			if (isValidImage(defaultImg)) {
				return defaultImg;
			}
		}
		// Ultimate fallback: icon_default
		int32_t defaultIndex = static_cast<int32_t>(icon_image_type::icon_default);
		if (icon_struct.contains(defaultIndex)) {
			const Image& fallback = icon_struct[defaultIndex];
			if (isValidImage(fallback)) {
				return fallback;
			}
		}
		// Return empty image if nothing available (draw_texture will handle null check)
		return Image{};
	};
	
	// look for formId matches
	if (a_form) {
		RE::FormID formId = a_form->GetFormID();
		if (icon_struct_formID.contains(formId)) {
			const Image& customImg = icon_struct_formID[formId];
			if (isValidImage(customImg)) {
				return customImg;
			}
			// Custom icon has invalid texture - log once and fall through to default
			static std::set<RE::FormID> warnedFormIds;
			if (warnedFormIds.find(formId) == warnedFormIds.end()) {
				warnedFormIds.insert(formId);
				logger::warn("Texture::GetIconImage: Custom icon for FormID 0x{:X} has invalid texture, using fallback", formId);
			}
		}
		// look for keyword matches
		const auto keywordForm = a_form->As<RE::BGSKeywordForm>();
		if (keywordForm) {
			for (auto& entry : icon_struct_keyword) {
				if (keywordForm->HasKeywordString(entry.first)) {
					if (isValidImage(entry.second)) {
						return entry.second;
					}
					// Keyword icon has invalid texture - log once and continue searching
					static std::set<std::string> warnedKeywords;
					if (warnedKeywords.find(entry.first) == warnedKeywords.end()) {
						warnedKeywords.insert(entry.first);
						logger::warn("Texture::GetIconImage: Keyword icon '{}' has invalid texture, using fallback", entry.first);
					}
				}
			}
		}
	}
	
	// Return default image for this type (with fallback chain)
	return getDefaultImage();
}

Texture::Image Texture::GetImageByPath(const std::string& a_path)
{
	if (a_path.empty()) {
		return {};
	}

	std::filesystem::path path = normalize_external_asset_path(a_path);
	if (path.is_relative()) {
		path = std::filesystem::path(".") / path;
	}
	const std::string cacheKey = normalize_texture_cache_path(path);
	if (auto cached = dynamic_image_struct.find(cacheKey); cached != dynamic_image_struct.end()) {
		return cached->second;
	}

	Image loaded{};
	if (!load_texture_from_file(path.string().c_str(), &loaded.texture, loaded.width, loaded.height)) {
		dynamic_image_struct[cacheKey] = {};
		return {};
	}

	dynamic_image_struct[cacheKey] = loaded;
	return loaded;
}

Texture::Image Texture::GetExternalRasterImage(const std::string& a_path)
{
	if (a_path.empty()) {
		return {};
	}

	std::filesystem::path path = normalize_external_asset_path(a_path);
	if (path.is_relative()) {
		path = std::filesystem::path(".") / path;
	}
	const std::string cacheKey = normalize_texture_cache_path(path);
	if (auto cached = external_raster_image_struct.find(cacheKey); cached != external_raster_image_struct.end()) {
		return cached->second;
	}

	if (path.extension().string().empty()) {
		external_raster_image_struct[cacheKey] = {};
		return {};
	}

	std::string ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	if (ext != ".png" && ext != ".dds") {
		external_raster_image_struct[cacheKey] = {};
		return {};
	}

	Image loaded{};
	if (!load_texture_from_file(path.string().c_str(), &loaded.texture, loaded.width, loaded.height)) {
		external_raster_image_struct[cacheKey] = {};
		return {};
	}

	external_raster_image_struct[cacheKey] = loaded;
	return loaded;
}

Texture::Image Texture::GetSlotMaskedExternalRasterImage(const std::string& a_path)
{
	if (a_path.empty()) {
		return {};
	}

	std::filesystem::path path = normalize_external_asset_path(a_path);
	if (path.is_relative()) {
		path = std::filesystem::path(".") / path;
	}
	const std::string cacheKey = normalize_texture_cache_path(path) + "|slotmask";
	if (auto cached = external_slot_masked_image_struct.find(cacheKey); cached != external_slot_masked_image_struct.end()) {
		return cached->second;
	}

	std::string ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	if (ext != ".png" && ext != ".dds") {
		external_slot_masked_image_struct[cacheKey] = {};
		return {};
	}

	constexpr int kSlotMaskCanvasSize = 384;
	const auto* polygonMask = GetFilledSlotPolygonMask(kSlotMaskCanvasSize);
	if (!polygonMask || polygonMask->empty()) {
		external_slot_masked_image_struct[cacheKey] = {};
		return {};
	}

	int sourceWidth = 0;
	int sourceHeight = 0;
	std::vector<unsigned char> sourcePixels;
	if (!DecodeRasterImageToRgba(path, sourceWidth, sourceHeight, sourcePixels) ||
	    sourceWidth <= 0 ||
	    sourceHeight <= 0 ||
	    sourcePixels.empty()) {
		external_slot_masked_image_struct[cacheKey] = {};
		return {};
	}

	std::vector<unsigned char> maskedCanvas;
	ResizeRgbaIntoCanvas(sourcePixels, sourceWidth, sourceHeight, kSlotMaskCanvasSize, maskedCanvas);

	for (int y = 0; y < kSlotMaskCanvasSize; ++y) {
		for (int x = 0; x < kSlotMaskCanvasSize; ++x) {
			const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(kSlotMaskCanvasSize) +
				static_cast<std::size_t>(x)) * 4u + 3u;
			const float maskAlpha = static_cast<float>((*polygonMask)[static_cast<std::size_t>(y) * static_cast<std::size_t>(kSlotMaskCanvasSize) +
				static_cast<std::size_t>(x)]) / 255.0f;
			const float srcAlpha = static_cast<float>(maskedCanvas[idx]) / 255.0f;
			maskedCanvas[idx] = static_cast<unsigned char>(std::clamp(srcAlpha * maskAlpha * 255.0f, 0.0f, 255.0f));
		}
	}

	Image loaded{};
	if (!CreateTextureFromRgbaPixels(maskedCanvas.data(), kSlotMaskCanvasSize, kSlotMaskCanvasSize, &loaded.texture)) {
		external_slot_masked_image_struct[cacheKey] = {};
		return {};
	}

	loaded.width = kSlotMaskCanvasSize;
	loaded.height = kSlotMaskCanvasSize;
	external_slot_masked_image_struct[cacheKey] = loaded;
	return loaded;
}

void Texture::InvalidateExternalRasterCache()
{
	release_cached_images(external_raster_image_struct);
	release_cached_images(external_slot_masked_image_struct);
}

const std::vector<ImVec2>* Texture::GetSlotBackgroundOutline()
{
	return slot_background_outline.size() >= 3 ? &slot_background_outline : nullptr;
}

Texture::Image Texture::GetSlotBackgroundMaskImage()
{
	if (slot_background_mask.texture) {
		return slot_background_mask;
	}
	return GetIconImage(icon_image_type::slot_background);
}

bool Texture::load_texture_from_file(const char* filename, ID3D11ShaderResourceView** out_srv, int& out_width, int& out_height, std::vector<ImVec2>* out_outline, ID3D11ShaderResourceView** out_mask_srv)
{
	if (!device_) {
		logger::error("Texture::device_ is null, cannot load texture '{}'", filename);
		return false;
	}

	// Load from disk into a raw RGBA buffer
	int image_width = 0;
	int image_height = 0;
	unsigned char* image_data = nullptr;
	bool imageDataFromStbi = false;
	std::string ext = std::filesystem::path(filename ? filename : "").extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});

	if (ext == ".png") {
		std::ifstream file(filename, std::ios::binary);
		if (!file) {
			logger::error("Failed to open PNG '{}'", filename);
			return false;
		}
		std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (bytes.empty()) {
			logger::error("PNG file '{}' is empty", filename);
			return false;
		}
		int channels = 0;
		image_data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &image_width, &image_height, &channels, 4);
		if (!image_data || image_width <= 0 || image_height <= 0) {
			logger::error("stbi_load_from_memory failed for '{}' ({})",
				filename,
				stbi_failure_reason() ? stbi_failure_reason() : "unknown");
			if (image_data) {
				stbi_image_free(image_data);
			}
			return false;
		}
		imageDataFromStbi = true;
	} else if (ext == ".dds") {
		std::vector<unsigned char> pixels;
		if (!LoadRasterImageWithWIC(std::filesystem::path(filename), image_width, image_height, pixels) ||
			image_width <= 0 ||
			image_height <= 0 ||
			pixels.empty()) {
			logger::error("WIC DDS decode failed for '{}'", filename);
			return false;
		}

		image_data = static_cast<unsigned char*>(malloc(pixels.size()));
		if (!image_data) {
			logger::error("malloc failed for DDS '{}' ({}x{})", filename, image_width, image_height);
			return false;
		}

		std::memcpy(image_data, pixels.data(), pixels.size());
	} else {
		SvgSizeInfo svgInfo{};
		auto* svg = ParseSvgFromFileNormalized(filename, svgInfo);
		if (!svg) {
			logger::error("nsvgParse failed for '{}'", filename);
			return false;
		}
		auto* rast = nsvgCreateRasterizer();
		if (!rast) {
			logger::error("nsvgCreateRasterizer failed for '{}'", filename);
			nsvgDelete(svg);
			return false;
		}

		image_width = static_cast<int>(svg->width);
		image_height = static_cast<int>(svg->height);
		if (svgInfo.width <= 0.0f) {
			svgInfo.width = svg->width;
		}
		if (svgInfo.height <= 0.0f) {
			svgInfo.height = svg->height;
		}

		const bool isWheelBackground = filename && std::strstr(filename, "wheel_background") != nullptr;
		if (isWheelBackground || svgInfo.usedViewBox || svgInfo.usedFallback) {
			const char* source = svgInfo.usedFallback ? "fallback" : (svgInfo.usedViewBox ? "viewBox" : "native");
			logger::info("Texture: SVG size '{}' viewBox({:.0f}x{:.0f}) -> {}x{} (source={})",
				filename, svgInfo.viewBoxW, svgInfo.viewBoxH, image_width, image_height, source);
		}

		image_data = static_cast<unsigned char*>(malloc(image_width * image_height * 4));
		if (!image_data) {
			nsvgDelete(svg);
			nsvgDeleteRasterizer(rast);
			logger::error("malloc failed for '{}' ({}x{})", filename, image_width, image_height);
			return false;
		}
		nsvgRasterize(rast, svg, 0, 0, 1, image_data, image_width, image_height, image_width * 4);

		if (out_outline) {
			build_slot_outline_from_svg(svg, *out_outline);
		}
		nsvgDelete(svg);
		nsvgDeleteRasterizer(rast);
	}

	// Create texture
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
	device_->CreateTexture2D(&desc, &sub_resource, &p_texture);

	// Create texture view
	D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
	ZeroMemory(&srv_desc, sizeof srv_desc);
	srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srv_desc.Texture2D.MipLevels = desc.MipLevels;
	srv_desc.Texture2D.MostDetailedMip = 0;
	device_->CreateShaderResourceView(p_texture, &srv_desc, out_srv);
	p_texture->Release();

	// Optional alpha-mask texture: keep alpha, force RGB to white so ImGui tinting behaves like a pure overlay.
	if (out_mask_srv) {
		auto mask_data = (unsigned char*)malloc(image_width * image_height * 4);
		if (mask_data) {
			slot_background_mask_alpha.resize(static_cast<std::size_t>(image_width) * static_cast<std::size_t>(image_height));
			slot_background_mask_alpha_width = image_width;
			slot_background_mask_alpha_height = image_height;
			for (int i = 0; i < image_width * image_height; ++i) {
				const unsigned char a = image_data[i * 4 + 3];
				mask_data[i * 4 + 0] = 255;
				mask_data[i * 4 + 1] = 255;
				mask_data[i * 4 + 2] = 255;
				mask_data[i * 4 + 3] = a;
				slot_background_mask_alpha[static_cast<std::size_t>(i)] = a;
			}

			ID3D11Texture2D* p_mask_texture = nullptr;
			D3D11_SUBRESOURCE_DATA mask_sub_resource;
			mask_sub_resource.pSysMem = mask_data;
			mask_sub_resource.SysMemPitch = desc.Width * 4;
			mask_sub_resource.SysMemSlicePitch = 0;
			device_->CreateTexture2D(&desc, &mask_sub_resource, &p_mask_texture);
			if (p_mask_texture) {
				device_->CreateShaderResourceView(p_mask_texture, &srv_desc, out_mask_srv);
				p_mask_texture->Release();
			}
			free(mask_data);
		}
	}

	if (imageDataFromStbi) {
		stbi_image_free(image_data);
	} else {
		free(image_data);
	}

	out_width = image_width;
	out_height = image_height;

	return true;
}

// Trim leading/trailing whitespace from a string
static std::string trimWhitespace(const std::string& str)
{
	size_t start = 0;
	while (start < str.size() && std::isspace(static_cast<unsigned char>(str[start]))) {
		++start;
	}
	size_t end = str.size();
	while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
		--end;
	}
	return str.substr(start, end - start);
}

// Parse hex string to uint32_t, returns false if invalid
static bool parseHexToUInt32(const std::string& hexString, uint32_t& outValue)
{
	std::string trimmed = trimWhitespace(hexString);
	if (trimmed.empty()) {
		return false;
	}
	
	// Remove "0x" or "0X" prefix if present
	if (trimmed.size() >= 2 && trimmed[0] == '0' && (trimmed[1] == 'x' || trimmed[1] == 'X')) {
		trimmed = trimmed.substr(2);
	}
	
	if (trimmed.empty()) {
		return false;
	}
	
	// Validate all characters are hex digits
	for (char c : trimmed) {
		if (!std::isxdigit(static_cast<unsigned char>(c))) {
			return false;
		}
	}
	
	// Parse using strtoull for safety
	char* endPtr = nullptr;
	unsigned long long val = std::strtoull(trimmed.c_str(), &endPtr, 16);
	if (endPtr == trimmed.c_str() || *endPtr != '\0') {
		return false;
	}
	if (val > 0xFFFFFFFFULL) {
		return false;  // Out of uint32_t range
	}
	
	outValue = static_cast<uint32_t>(val);
	return true;
}

void Texture::load_custom_icon_images()
{
	auto tesDataHandler = RE::TESDataHandler::GetSingleton();
	if (!tesDataHandler) {
		logger::error("Texture::load_custom_icon_images: TESDataHandler not available");
		return;
	}

	if (!std::filesystem::exists(icon_custom_directory)) {
		logger::info("Texture::load_custom_icon_images: custom icon directory '{}' does not exist", icon_custom_directory);
		return;
	}

	logger::info("Texture::load_custom_icon_images: scanning '{}'", icon_custom_directory);

	std::size_t loadedFID = 0;
	std::size_t loadedKWD = 0;
	std::size_t skipped = 0;
	std::size_t parseErrors = 0;
	std::size_t lookupFailures = 0;
	std::size_t textureFailures = 0;

	for (const auto& entry : std::filesystem::directory_iterator(icon_custom_directory)) {
		if (entry.path().filename().extension() != ".svg") {
			++skipped;
			continue;
		}
		std::string fileName = entry.path().filename().string();
		
		// Handle double FID_ prefix (e.g., "FID_FID_plugin.esp_0x123.svg")
		while (fileName.find("FID_FID_") == 0) {
			logger::warn("Texture::load_custom_icon_images: '{}' has double FID_ prefix, stripping one", fileName);
			fileName = fileName.substr(4);  // Remove one "FID_"
		}
		while (fileName.find("KWD_KWD_") == 0) {
			logger::warn("Texture::load_custom_icon_images: '{}' has double KWD_ prefix, stripping one", fileName);
			fileName = fileName.substr(4);  // Remove one "KWD_"
		}
		
		bool fid = false;  // whether we're looking for a formID match
		size_t idx = fileName.find("FID_");
		if (idx == 0) {
			fid = true;
		} else {
			idx = fileName.find("KWD_");
			if (idx != 0) {
				++skipped;
				continue;  // not a valid file name
			}
		}
		if (fid) {
			const size_t pluginNameBegin = 4;
			size_t pluginNameEnd = fileName.find("_0x", pluginNameBegin);  // find the 2nd '_'
			if (pluginNameEnd == std::string::npos) {
				pluginNameEnd = fileName.find("_0X", pluginNameBegin);
				if (pluginNameEnd == std::string::npos) {
					logger::warn("Texture::load_custom_icon_images: '{}' has no '_0x' / '_0X' pattern, skipping", fileName);
					++parseErrors;
					continue;
				}
			}
			
			// Extract and validate plugin name (must end with .esp/.esm/.esl)
			std::string pluginName = trimWhitespace(fileName.substr(pluginNameBegin, pluginNameEnd - pluginNameBegin));
			if (pluginName.empty()) {
				logger::warn("Texture::load_custom_icon_images: '{}' has empty plugin name, skipping", fileName);
				++parseErrors;
				continue;
			}
			
			// Validate plugin extension
			bool validExt = false;
			for (const char* ext : { ".esp", ".esm", ".esl", ".ESP", ".ESM", ".ESL" }) {
				if (pluginName.size() > 4 && pluginName.substr(pluginName.size() - 4) == ext) {
					validExt = true;
					break;
				}
			}
			if (!validExt) {
				logger::warn("Texture::load_custom_icon_images: '{}' plugin name '{}' doesn't end with .esp/.esm/.esl, skipping", 
					fileName, pluginName);
				++parseErrors;
				continue;
			}
			
			// Extract and validate formId
			size_t formIdEnd = fileName.find(".svg");
			if (formIdEnd == std::string::npos) {
				logger::warn("Texture::load_custom_icon_images: '{}' missing .svg extension marker, skipping", fileName);
				++parseErrors;
				continue;
			}
			size_t formIdBegin = pluginNameEnd + 1;
			std::string formIdStr = trimWhitespace(fileName.substr(formIdBegin, formIdEnd - formIdBegin));
			
			uint32_t formId = 0;
			if (!parseHexToUInt32(formIdStr, formId)) {
				logger::warn("Texture::load_custom_icon_images: '{}' has invalid hex formId '{}', skipping", 
					fileName, formIdStr);
				++parseErrors;
				continue;
			}
			
			// Validate formId is in reasonable range (local formId should be < 0x01000000)
			if (formId >= 0x01000000) {
				logger::warn("Texture::load_custom_icon_images: '{}' formId 0x{:X} looks like a runtime FormID, not a local ID. Skipping.",
					fileName, formId);
				++parseErrors;
				continue;
			}
			
			// Attempt LookupForm
			RE::TESForm* form = tesDataHandler->LookupForm(formId, pluginName);
			if (!form) {
				// Only log first few failures to avoid spam
				if (lookupFailures < 10) {
					logger::debug("Texture::load_custom_icon_images: LookupForm failed for {}:0x{:X} (from '{}') - plugin may not be loaded",
						pluginName, formId, entry.path().filename().string());
				} else if (lookupFailures == 10) {
					logger::debug("Texture::load_custom_icon_images: (suppressing further LookupForm failure warnings)");
				}
				++lookupFailures;
				continue;  // DO NOT register - this is the key fix
			}
			
			RE::FormID runtimeFormID = form->GetFormID();
			
			// Load texture - only register if successful
			Image img;
			img.texture = nullptr;  // Ensure initialized
			if (load_texture_from_file(entry.path().string().c_str(), &img.texture, img.width, img.height, nullptr, nullptr)) {
				if (img.texture) {  // Double-check texture is valid
					icon_struct_formID[runtimeFormID] = img;
					++loadedFID;
				} else {
					logger::error("Texture::load_custom_icon_images: load_texture_from_file returned true but texture is null for '{}'", 
						entry.path().string());
					++textureFailures;
				}
			} else {
				logger::error("Texture::load_custom_icon_images: failed to load texture file '{}'", entry.path().string());
				++textureFailures;
			}
		} else { // KWD (keyword)
			const size_t keywordBegin = 4;
			size_t keywordEnd = fileName.find(".svg");
			if (keywordEnd == std::string::npos) {
				logger::warn("Texture::load_custom_icon_images: '{}' missing .svg extension marker, skipping", fileName);
				++parseErrors;
				continue;
			}
			std::string keyWord = trimWhitespace(fileName.substr(keywordBegin, keywordEnd - keywordBegin));
			if (keyWord.empty()) {
				logger::warn("Texture::load_custom_icon_images: '{}' has empty keyword, skipping", fileName);
				++parseErrors;
				continue;
			}
			
			Image img;
			img.texture = nullptr;  // Ensure initialized
			if (load_texture_from_file(entry.path().string().c_str(), &img.texture, img.width, img.height, nullptr, nullptr)) {
				if (img.texture) {  // Double-check texture is valid
					icon_struct_keyword[keyWord] = img;
					++loadedKWD;
				} else {
					logger::error("Texture::load_custom_icon_images: load_texture_from_file returned true but texture is null for '{}'", 
						entry.path().string());
					++textureFailures;
				}
			} else {
				logger::error("Texture::load_custom_icon_images: failed to load keyword texture file '{}'", entry.path().string());
				++textureFailures;
			}
		}
	}

	logger::info(
		"Texture::load_custom_icon_images: finished. FID icons: {}, keyword icons: {}, skipped: {}, parse errors: {}, lookup failures: {}, texture failures: {}",
		loadedFID, loadedKWD, skipped, parseErrors, lookupFailures, textureFailures);
}
#include <algorithm>
#include <filesystem>
#include <vector>

namespace
{
	static ImVec2 bezier_point(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t)
	{
		const float u = 1.0f - t;
		const float tt = t * t;
		const float uu = u * u;
		const float uuu = uu * u;
		const float ttt = tt * t;

		ImVec2 p = ImVec2(0.0f, 0.0f);
		p.x = uuu * p0.x + 3.0f * uu * t * p1.x + 3.0f * u * tt * p2.x + ttt * p3.x;
		p.y = uuu * p0.y + 3.0f * uu * t * p1.y + 3.0f * u * tt * p2.y + ttt * p3.y;
		return p;
	}

	static float signed_area_y_down(const std::vector<ImVec2>& pts)
	{
		if (pts.size() < 3) {
			return 0.0f;
		}
		float area = 0.0f;
		for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
			area += pts[i].x * pts[i + 1].y - pts[i + 1].x * pts[i].y;
		}
		return area * 0.5f;
	}

	static void rotate_to_topmost(std::vector<ImVec2>& pts)
	{
		if (pts.size() < 3) {
			return;
		}
		// Assume last point == first point; operate on open polyline.
		const bool closed = pts.front().x == pts.back().x && pts.front().y == pts.back().y;
		if (closed) {
			pts.pop_back();
		}

		std::size_t best = 0;
		for (std::size_t i = 1; i < pts.size(); ++i) {
			if (pts[i].y < pts[best].y || (pts[i].y == pts[best].y && pts[i].x < pts[best].x)) {
				best = i;
			}
		}
		std::rotate(pts.begin(), pts.begin() + best, pts.end());

		if (closed) {
			pts.push_back(pts.front());
		}
	}

	void build_slot_outline_from_svg(const NSVGimage* svg, std::vector<ImVec2>& out_outline)
	{
		out_outline.clear();
		if (!svg || svg->width <= 0.0f || svg->height <= 0.0f) {
			return;
		}

		const NSVGpath* bestPath = nullptr;
		float bestBoundsArea = -1.0f;

		for (const NSVGshape* shape = svg->shapes; shape; shape = shape->next) {
			for (const NSVGpath* path = shape->paths; path; path = path->next) {
				const float w = path->bounds[2] - path->bounds[0];
				const float h = path->bounds[3] - path->bounds[1];
				const float area = w * h;
				if (area > bestBoundsArea) {
					bestBoundsArea = area;
					bestPath = path;
				}
			}
		}

		if (!bestPath || bestPath->npts < 4) {
			return;
		}

		const int subdivisions = 12;
		std::vector<ImVec2> pts;
		pts.reserve(static_cast<std::size_t>(bestPath->npts) * static_cast<std::size_t>(subdivisions));

		for (int i = 0; i < bestPath->npts - 1; i += 3) {
			const float* p0f = &bestPath->pts[i * 2];
			const float* p1f = &bestPath->pts[(i + 1) * 2];
			const float* p2f = &bestPath->pts[(i + 2) * 2];
			const float* p3f = &bestPath->pts[(i + 3) * 2];

			const ImVec2 p0(p0f[0], p0f[1]);
			const ImVec2 p1(p1f[0], p1f[1]);
			const ImVec2 p2(p2f[0], p2f[1]);
			const ImVec2 p3(p3f[0], p3f[1]);

			for (int s = 0; s <= subdivisions; ++s) {
				const float t = static_cast<float>(s) / static_cast<float>(subdivisions);
				ImVec2 p = bezier_point(p0, p1, p2, p3, t);
				// Normalize to 0..1 in SVG space.
				p.x /= svg->width;
				p.y /= svg->height;
				if (!pts.empty()) {
					const ImVec2 last = pts.back();
					const float dx = p.x - last.x;
					const float dy = p.y - last.y;
					if ((dx * dx + dy * dy) < 1.0e-8f) {
						continue;
					}
				}
				pts.push_back(p);
			}
		}

		if (pts.size() < 3) {
			return;
		}

		// Close the path for polyline drawing.
		if (!(pts.front().x == pts.back().x && pts.front().y == pts.back().y)) {
			pts.push_back(pts.front());
		}

		// Ensure clockwise orientation in y-down screen space.
		if (signed_area_y_down(pts) < 0.0f) {
			pts.pop_back();
			std::reverse(pts.begin(), pts.end());
			pts.push_back(pts.front());
		}

		// Start at the top-most point so "progress" begins at the top.
		rotate_to_topmost(pts);

		out_outline = std::move(pts);
	}
}
