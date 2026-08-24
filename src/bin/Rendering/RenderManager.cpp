#include "RenderManager.h"

#include <d3d11.h>

#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <dxgi.h>
#include <sstream>
#include <algorithm>
#include <cctype>

#include "imgui_internal.h"
// Renderer hook/setup patterns derive from LamasTinyHUD revision
// dd1794c46b1f87cbf04a5d60968facbed0605d02 (GNU GPL v3), inherited through
// original Wheeler. The ImGui FreeType integration also follows
// MaxsuDetectionMeter revision fcc5ef75d6cdabc63db0b214bc61fc272a5b22cf
// (MIT).
#include "include/lib/imgui_freetype.h"

#include "bin/Wheeler/Wheeler.h"
#include "bin/Wheeler/AmmoWheelReskin.h"
#include "bin/Wheeler/AmmoWheelReskinUnified.h"
#include "bin/Rendering/TextureManager.h"
#include "bin/Rendering/ResolutionScaleContext.h"
#include "bin/InitState.h"
#include "bin/Config.h"
#include "bin/HookValidation.h"

#include "bin/Animation/TimeInterpolator/TimeInterpolatorManager.h"

// ========== GLYPH RANGE BUILDING ==========
namespace GlyphRanges
{
	// Static glyph range arrays (must persist for ImGui font building)
	// Preset 0: Minimal (ASCII only)
	static const ImWchar RangesMinimal[] = {
		0x0020, 0x007F,  // Basic Latin (ASCII printable)
		0
	};

	// Preset 1: Latin Basic (ASCII + Latin-1 Supplement)
	static const ImWchar RangesLatinBasic[] = {
		0x0020, 0x007F,  // Basic Latin
		0x00A0, 0x00FF,  // Latin-1 Supplement (German, French, Spanish, etc.)
		0
	};

	// Preset 2: Latin Extended (covers most European languages)
	static const ImWchar RangesLatinExtended[] = {
		0x0020, 0x007F,  // Basic Latin
		0x00A0, 0x00FF,  // Latin-1 Supplement
		0x0100, 0x017F,  // Latin Extended-A (Central European: Polish, Czech, Hungarian, Turkish, etc.)
		0x0180, 0x024F,  // Latin Extended-B (Romanian, Croatian, Slovenian, etc.)
		0
	};

	// Preset 3: Latin Full (comprehensive European + phonetic)
	static const ImWchar RangesLatinFull[] = {
		0x0020, 0x007F,  // Basic Latin
		0x00A0, 0x00FF,  // Latin-1 Supplement
		0x0100, 0x017F,  // Latin Extended-A
		0x0180, 0x024F,  // Latin Extended-B
		0x0250, 0x02AF,  // IPA Extensions (phonetic symbols)
		0x02B0, 0x02FF,  // Spacing Modifier Letters
		0x1E00, 0x1EFF,  // Latin Extended Additional (Vietnamese, Welsh, etc.)
		0x2000, 0x206F,  // General Punctuation
		0x20A0, 0x20CF,  // Currency Symbols
		0
	};

	// Storage for custom parsed ranges
	static std::vector<ImWchar> CustomRangesStorage;

	// Parse custom ranges string like "0020-007E,00A0-00FF,0100-017F"
	static bool ParseCustomRanges(const std::string& rangesStr, std::vector<ImWchar>& outRanges)
	{
		outRanges.clear();
		if (rangesStr.empty()) {
			return false;
		}

		std::stringstream ss(rangesStr);
		std::string token;
		
		while (std::getline(ss, token, ',')) {
			// Trim whitespace
			token.erase(0, token.find_first_not_of(" \t"));
			token.erase(token.find_last_not_of(" \t") + 1);
			
			if (token.empty()) continue;
			
			// Parse "XXXX-YYYY" format
			size_t dashPos = token.find('-');
			if (dashPos == std::string::npos || dashPos == 0 || dashPos == token.length() - 1) {
				logger::warn("[Font] Invalid range format: '{}' - expected XXXX-YYYY", token);
				continue;
			}
			
			std::string startStr = token.substr(0, dashPos);
			std::string endStr = token.substr(dashPos + 1);
			
			try {
				unsigned long startVal = std::stoul(startStr, nullptr, 16);
				unsigned long endVal = std::stoul(endStr, nullptr, 16);
				
				// Validate range (ImWchar is typically 16-bit, so cap at 0xFFFF for BMP)
				if (startVal > 0xFFFF || endVal > 0xFFFF) {
					logger::warn("[Font] Range exceeds BMP (0xFFFF): {}-{}, clamping", startStr, endStr);
					if (startVal > 0xFFFF) startVal = 0xFFFF;
					if (endVal > 0xFFFF) endVal = 0xFFFF;
				}
				
				if (startVal > endVal) {
					logger::warn("[Font] Invalid range (start > end): {}-{}", startStr, endStr);
					continue;
				}
				
				outRanges.push_back(static_cast<ImWchar>(startVal));
				outRanges.push_back(static_cast<ImWchar>(endVal));
			}
			catch (const std::exception& e) {
				logger::warn("[Font] Failed to parse range '{}': {}", token, e.what());
				continue;
			}
		}
		
		// Terminate with 0
		if (!outRanges.empty()) {
			outRanges.push_back(0);
			return true;
		}
		return false;
	}

	// Get glyph ranges based on preset
	static const ImWchar* GetGlyphRangesForPreset(int preset, const std::string& customRanges)
	{
		switch (preset) {
		case 0:  // Minimal
			INFO("[Font] Using Minimal glyph preset (ASCII only)");
			return RangesMinimal;
			
		case 1:  // Latin Basic
			INFO("[Font] Using Latin Basic glyph preset (ASCII + Latin-1 Supplement)");
			return RangesLatinBasic;
			
		case 2:  // Latin Extended (default)
			INFO("[Font] Using Latin Extended glyph preset (Latin-1 + Extended-A + Extended-B)");
			return RangesLatinExtended;
			
		case 3:  // Latin Full
			INFO("[Font] Using Latin Full glyph preset (Extended + Additional + IPA + Currency)");
			return RangesLatinFull;
			
		case 4:  // Custom
			if (ParseCustomRanges(customRanges, CustomRangesStorage)) {
				INFO("[Font] Using Custom glyph ranges: {}", customRanges);
				return CustomRangesStorage.data();
			} else {
				logger::warn("[Font] Custom ranges parsing failed, falling back to Latin Extended");
				return RangesLatinExtended;
			}
			
		default:
			logger::warn("[Font] Unknown glyph preset {}, falling back to Latin Extended", preset);
			return RangesLatinExtended;
		}
	}

	// Get preset name for logging
	static const char* GetPresetName(int preset)
	{
		switch (preset) {
		case 0: return "Minimal";
		case 1: return "Latin Basic";
		case 2: return "Latin Extended";
		case 3: return "Latin Full";
		case 4: return "Custom";
		default: return "Unknown";
		}
	}

	// Count glyphs in a range array
	static int CountGlyphsInRanges(const ImWchar* ranges)
	{
		if (!ranges) return 0;
		int count = 0;
		for (int i = 0; ranges[i] != 0; i += 2) {
			count += (ranges[i + 1] - ranges[i] + 1);
		}
		return count;
	}

	// Log range details
	static void LogRangeDetails(const ImWchar* ranges)
	{
		if (!ranges) return;
		INFO("[Font] Glyph ranges:");
		for (int i = 0; ranges[i] != 0; i += 2) {
			INFO("[Font]   U+{:04X} - U+{:04X} ({} glyphs)", 
				(unsigned int)ranges[i], (unsigned int)ranges[i + 1], 
				(ranges[i + 1] - ranges[i] + 1));
		}
		INFO("[Font] Total potential glyphs: {}", CountGlyphsInRanges(ranges));
	}
}
// ========== END GLYPH RANGE BUILDING ==========


namespace stl
{
	using namespace SKSE::stl;

	template <class T>
	void write_thunk_call()
	{
		auto& trampoline = SKSE::GetTrampoline();
		const REL::Relocation<std::uintptr_t> hook{ T::id, T::offset };
		T::func = trampoline.write_call<5>(hook.address(), T::thunk);
	}
}

LRESULT RenderManager::WndProcHook::thunk(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	auto& io = ImGui::GetIO();
	if (uMsg == WM_KILLFOCUS) {
		io.ClearInputCharacters();
		io.ClearInputKeys();
	}

	return func(hWnd, uMsg, wParam, lParam);
}

void RenderManager::D3DInitHook::thunk()
{
	func();

	INFO("RenderManager: Initializing...");
	// Updated for newer CommonLibSSE-NG: use BSGraphics::Renderer
	auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
	if (!renderer) {
		ERROR("Cannot find BSGraphics::Renderer. Initialization failed!");
		return;
	}

	auto* render_data = RE::BSGraphics::Renderer::GetRendererDataSingleton();
	if (!render_data) {
		ERROR("Cannot get renderer data. Initialization failed!");
		return;
	}

	INFO("Getting swapchain...");
	auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
	auto* swapchain = window ? window->swapChain : nullptr;
	if (!swapchain) {
		ERROR("Cannot find swapchain. Initialization failed!");
		return;
	}

	device = reinterpret_cast<ID3D11Device*>(render_data->forwarder);
	Texture::device_ = device;
	context = reinterpret_cast<ID3D11DeviceContext*>(render_data->context);
	
	// Initialize AmmoWheel reskin systems
	AmmoWheelReskin::Renderer::GetSingleton().Init(device);  // Legacy system
	AmmoWheelReskinUnified::ReskinSystem::GetSingleton().Init(device);  // Unified system

	INFO("Initializing ImGui...");
	ImGui::CreateContext();
	auto hwnd = window ? reinterpret_cast<HWND>(window->hWnd) : nullptr;
	if (!hwnd || !ImGui_ImplWin32_Init(hwnd)) {
		ERROR("ImGui initialization failed (Win32)");
		return;
	}
	if (!ImGui_ImplDX11_Init(device, context)) {
		ERROR("ImGui initialization failed (DX11)");
		return;
	}

	INFO("...ImGui Initialized");

	ResolutionScale::Context::GetSingleton().Initialize(reinterpret_cast<IDXGISwapChain*>(swapchain), hwnd);

	initialized.store(true);

	WndProcHook::func = reinterpret_cast<WNDPROC>(
		SetWindowLongPtrA(
			hwnd,
			GWLP_WNDPROC,
			reinterpret_cast<LONG_PTR>(WndProcHook::thunk)));
	if (!WndProcHook::func)
		ERROR("SetWindowLongPtrA failed!");

	INFO("Building font atlas...");
	
	// Load font configuration from FontConfig.ini
	std::filesystem::path fontPath;
	bool foundCustomFont = false;
	const ImWchar* glyphRanges = nullptr;
	std::string languageStr;
	
#define FONTSETTING_PATH "Data\\SKSE\\Plugins\\wheeler\\resources\\fonts\\FontConfig.ini"
	CSimpleIniA ini;
	ini.LoadFile(FONTSETTING_PATH);
	
	// Read glyph preset from FontConfig.ini (overrides Config::Font::GlyphPreset)
	int glyphPreset = Config::Font::GlyphPreset;
	std::string customRanges = Config::Font::CustomRanges;
	bool logAtlasInfo = Config::Font::Debug::LogAtlasInfo;
	bool showTestOverlay = Config::Font::Debug::ShowGlyphTestOverlay;
	
	// Check for GlyphPreset in INI
	const char* presetStr = ini.GetValue("config", "GlyphPreset", nullptr);
	if (presetStr) {
		glyphPreset = std::atoi(presetStr);
		INFO("[Font] GlyphPreset from INI: {}", glyphPreset);
	}
	
	// Check for CustomRanges in INI
	const char* customRangesStr = ini.GetValue("config", "CustomRanges", nullptr);
	if (customRangesStr) {
		customRanges = customRangesStr;
	}
	
	// Check for debug settings in INI
	const char* logAtlasStr = ini.GetValue("config.debug", "LogAtlasInfo", nullptr);
	if (logAtlasStr) {
		logAtlasInfo = (std::string(logAtlasStr) == "true" || std::string(logAtlasStr) == "1");
	}
	const char* showOverlayStr = ini.GetValue("config.debug", "ShowGlyphTestOverlay", nullptr);
	if (showOverlayStr) {
		showTestOverlay = (std::string(showOverlayStr) == "true" || std::string(showOverlayStr) == "1");
		Config::Font::Debug::ShowGlyphTestOverlay = showTestOverlay;  // Update config for runtime
	}
	
	if (!ini.IsEmpty()) {
		const char* language = ini.GetValue("config", "font", nullptr);
		if (language) {
			languageStr = language;
			std::string fontDir = R"(Data\SKSE\Plugins\wheeler\resources\fonts\)" + languageStr;
			// check if folder exists
			if (std::filesystem::exists(fontDir) && std::filesystem::is_directory(fontDir)) {
				for (const auto& entry : std::filesystem::directory_iterator(fontDir)) {
					auto entryPath = entry.path();
					if (entryPath.extension() == ".ttf" || entryPath.extension() == ".ttc") {
						fontPath = entryPath;
						foundCustomFont = true;
						break;
					}
				}
			}
			if (foundCustomFont) {
				INFO("[Font] Loading font: {}", fontPath.string().c_str());
				
				// Determine glyph ranges based on language or preset
				// Asian languages use ImGui built-in ranges
				if (languageStr == "Chinese") {
					INFO("[Font] Glyph range set to Chinese (ImGui built-in)");
					glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesChineseFull();
				} else if (languageStr == "Korean") {
					INFO("[Font] Glyph range set to Korean (ImGui built-in)");
					glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesKorean();
				} else if (languageStr == "Japanese") {
					INFO("[Font] Glyph range set to Japanese (ImGui built-in)");
					glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesJapanese();
				} else if (languageStr == "Thai") {
					INFO("[Font] Glyph range set to Thai (ImGui built-in)");
					glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesThai();
				} else if (languageStr == "Vietnamese") {
					INFO("[Font] Glyph range set to Vietnamese (ImGui built-in)");
					glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesVietnamese();
				} else if (languageStr == "Cyrillic") {
					INFO("[Font] Glyph range set to Cyrillic (ImGui built-in)");
					glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesCyrillic();
				} else {
					// For all Latin-based languages, use the glyph preset system
					// This covers: LatinExt, Turkish, Polish, Czech, Slovak, Hungarian, Romanian,
					// Croatian, Slovenian, Lithuanian, Latvian, Estonian, Albanian, Icelandic,
					// Bosnian, SerbianLatin, Norwegian, Swedish, Danish, Finnish, German, etc.
					INFO("[Font] Language '{}' - using GlyphPreset {} ({})", 
						languageStr, glyphPreset, GlyphRanges::GetPresetName(glyphPreset));
					glyphRanges = GlyphRanges::GetGlyphRangesForPreset(glyphPreset, customRanges);
				}
			} else {
				INFO("[Font] No font found for language: {}", language);
			}
		}
	}
	
	// If no language-specific font, still apply glyph preset for default font
	if (!foundCustomFont && glyphPreset >= 0) {
		INFO("[Font] No custom font, using default with GlyphPreset {} ({})", 
			glyphPreset, GlyphRanges::GetPresetName(glyphPreset));
		glyphRanges = GlyphRanges::GetGlyphRangesForPreset(glyphPreset, customRanges);
	}
	
	// Log glyph range details if debug logging enabled
	if (logAtlasInfo && glyphRanges) {
		GlyphRanges::LogRangeDetails(glyphRanges);
	}
	
#define ENABLE_FREETYPE 0
#if ENABLE_FREETYPE
	ImFontAtlas* atlas = ImGui::GetIO().Fonts;
	atlas->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
	atlas->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
#endif

	// Add font with computed glyph ranges
	if (foundCustomFont) {
		ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 64.0f, nullptr, glyphRanges);
	}
	
	// Build the atlas and log size info
	ImGui::GetIO().Fonts->Build();
	
	if (logAtlasInfo) {
		int atlasWidth = 0, atlasHeight = 0;
		unsigned char* pixels = nullptr;
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);
		INFO("[Font] Atlas built: {}x{} pixels", atlasWidth, atlasHeight);
		
		// Warn if atlas is very large (>4096 in either dimension)
		if (atlasWidth > 4096 || atlasHeight > 4096) {
			logger::warn("[Font] Atlas is large ({}x{}). Consider using a smaller glyph preset.", atlasWidth, atlasHeight);
		}
	}
	
	INFO("...font atlas built");

	INFO("RenderManager: Initialized");

}

void RenderManager::DXGIPresentHook::thunk(std::uint32_t a_p1)
{
	func(a_p1);

	if (!D3DInitHook::initialized.load())
		return;

	// prologue
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// do stuff
	RenderManager::draw();

	// epilogue
	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

struct ImageSet
{
	std::int32_t my_image_width = 0;
	std::int32_t my_image_height = 0;
	ID3D11ShaderResourceView* my_texture = nullptr;
};


void RenderManager::MessageCallback(SKSE::MessagingInterface::Message* msg)  //CallBack & LoadTextureFromFile should called after resource loaded.
{
	if (msg->type == SKSE::MessagingInterface::kDataLoaded && D3DInitHook::initialized) {
		auto& io = ImGui::GetIO();
		io.MouseDrawCursor = true;
		io.WantSetMousePos = true;
	}
}

bool RenderManager::Install()
{
	static std::atomic_bool installed{ false };
	if (installed.load()) {
		logger::info("RenderManager: Install called again, skipping");
		return true;
	}

	auto g_message = SKSE::GetMessagingInterface();
	if (!g_message) {
		ERROR("Messaging Interface Not Found!");
		return false;
	}

	if (!g_message->RegisterListener(MessageCallback)) {
		ERROR("RenderManager: Failed to register SKSE message callback");
		return false;
	}

	const REL::Relocation<std::uintptr_t> d3dAnchor{ D3DInitHook::id };
	const REL::Relocation<std::uintptr_t> d3dInitHook{ D3DInitHook::id, D3DInitHook::offset };
	const REL::Relocation<std::uintptr_t> presentAnchor{ DXGIPresentHook::id };
	const REL::Relocation<std::uintptr_t> presentHook{ DXGIPresentHook::id, DXGIPresentHook::offset };
	if (!HookValidation::ValidateCallHookSite(
			d3dAnchor.address(),
			d3dInitHook.address(),
			77396,
			"RenderManager",
			"D3DInit",
			[anchor = d3dAnchor.address(), hookSite = d3dInitHook.address()]() {
				return HookValidation::MatchBytes(anchor + 0x264, { 0x48, 0x8B, 0x0D }) &&
				       HookValidation::MatchBytes(anchor + 0x26B, { 0x48, 0x8B, 0x49, 0x48, 0xFF, 0x15 }) &&
				       HookValidation::MatchBytes(hookSite + 5, { 0x44, 0x39, 0x6E, 0x2C, 0x74, 0x2E, 0x33, 0xC9 }) &&
				       HookValidation::MatchBytes(hookSite + 13, { 0xE8 });
			}) ||
		!HookValidation::ValidateCallHookSite(
			presentAnchor.address(),
			presentHook.address(),
			109135,
			"RenderManager",
			"DXGIPresent",
			[anchor = presentAnchor.address(), hookSite = presentHook.address()]() {
				return HookValidation::MatchBytes(anchor, { 0x48, 0x83, 0xEC, 0x48, 0xB9, 0x01, 0x00, 0x00, 0x00 }) &&
				       HookValidation::MatchBytes(hookSite + 5, { 0x80, 0x3D }) &&
				       HookValidation::MatchBytes(hookSite + 11, { 0x00, 0x74, 0x70 });
			})) {
		return false;
	}

	stl::write_thunk_call<D3DInitHook>();
	stl::write_thunk_call<DXGIPresentHook>();

	installed.store(true);
	logger::info("RenderManager: Installed hooks");

	return true;
}



float RenderManager::GetResolutionScaleWidth()
{
	return ImGui::GetIO().DisplaySize.x / 1920.f;
}

float RenderManager::GetResolutionScaleHeight()
{
	return ImGui::GetIO().DisplaySize.y / 1080.f;
}


void RenderManager::draw()
{
	if (!InitState::IsCoreInitialized() || !InitState::IsDataInitialized()) {
		return;
	}

	// Add UI elements here
	float deltaTime = ImGui::GetIO().DeltaTime;
	ResolutionScale::Context::GetSingleton().Update();
	Wheeler::Update(deltaTime);
	TimeFloatInterpolatorManager::Update(deltaTime);

	// Font glyph test overlay (debug feature)
	if (Config::Font::Debug::ShowGlyphTestOverlay) {
		DrawGlyphTestOverlay();
	}
}

void RenderManager::DrawGlyphTestOverlay()
{
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
	                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
	                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
	
	// Position in top-left corner with some padding
	ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.85f);
	
	if (ImGui::Begin("Font Glyph Test", nullptr, flags)) {
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Wheeler Font Glyph Test");
		ImGui::Separator();
		
		// Current preset info
		ImGui::Text("Preset: %d (%s)", Config::Font::GlyphPreset, 
			GlyphRanges::GetPresetName(Config::Font::GlyphPreset));
		ImGui::Separator();
		
		// Test strings for various European languages
		// Using raw UTF-8 bytes in regular string literals (source file is UTF-8)
		// German: AOU aou ss
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "DE:");
		ImGui::SameLine();
		ImGui::Text("\xc3\x84\xc3\x96\xc3\x9c \xc3\xa4\xc3\xb6\xc3\xbc \xc3\x9f | Ger\xc3\xbc" "st \xc3\x9c" "bung \xc3\x84rger");
		
		// Polish: ACELNOSZ acelnoszzz
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "PL:");
		ImGui::SameLine();
		ImGui::Text("\xc4\x84\xc4\x86\xc4\x98\xc5\x81\xc5\x83\xc3\x93\xc5\x9a\xc5\xb9\xc5\xbb \xc4\x85\xc4\x87\xc4\x99\xc5\x82\xc5\x84\xc3\xb3\xc5\x9b\xc5\xba\xc5\xbc");
		
		// Turkish: GIS iou c
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "TR:");
		ImGui::SameLine();
		ImGui::Text("\xc4\x9e\xc4\xb0\xc5\x9e \xc4\xb1\xc3\xb6\xc3\xbc \xc3\xa7 | T\xc3\xbcrk\xc3\xa7""e");
		
		// Czech: CDNRSTZ cdnrstz
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "CZ:");
		ImGui::SameLine();
		ImGui::Text("\xc4\x8c\xc4\x8e\xc5\x87\xc5\x98\xc5\xa0\xc5\xa4\xc5\xbd \xc4\x8d\xc4\x8f\xc5\x88\xc5\x99\xc5\xa1\xc5\xa5\xc5\xbe | \xc4\x8c""esky");
		
		// Romanian: AAIST aaist
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "RO:");
		ImGui::SameLine();
		ImGui::Text("\xc4\x82\xc3\x82\xc3\x8e\xc8\x98\xc8\x9a \xc4\x83\xc3\xa2\xc3\xae\xc8\x99\xc8\x9b | Rom\xc3\xa2n\xc4\x83");
		
		// Swedish: AAO aao
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "SE:");
		ImGui::SameLine();
		ImGui::Text("\xc3\x85\xc3\x84\xc3\x96 \xc3\xa5\xc3\xa4\xc3\xb6 | Svenska");
		
		// Icelandic: PDAO pdao
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "IS:");
		ImGui::SameLine();
		ImGui::Text("\xc3\x9e\xc3\x90\xc3\x86\xc3\x96 \xc3\xbe\xc3\xb0\xc3\xa6\xc3\xb6 | \xc3\x8dslenska");
		
		// Croatian: CCDSZ ccdsz
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "HR:");
		ImGui::SameLine();
		ImGui::Text("\xc4\x8c\xc4\x86\xc4\x90\xc5\xa0\xc5\xbd \xc4\x8d\xc4\x87\xc4\x91\xc5\xa1\xc5\xbe | Hrvatski");
		
		// Lithuanian: ACEEISUUZ aceeisuuz
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "LT:");
		ImGui::SameLine();
		ImGui::Text("\xc4\x84\xc4\x8c\xc4\x98\xc4\x96\xc4\xae\xc5\xa0\xc5\xb2\xc5\xaa\xc5\xbd \xc4\x85\xc4\x8d\xc4\x99\xc4\x97\xc4\xaf\xc5\xa1\xc5\xb3\xc5\xab\xc5\xbe");
		
		// Latvian: ACEGIKLNORSUZ
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "LV:");
		ImGui::SameLine();
		ImGui::Text("\xc4\x80\xc4\x8c\xc4\x92\xc4\xa2\xc4\xaa\xc4\xb6\xc4\xbb\xc5\x85\xc5\x8c\xc5\x96\xc5\xa0\xc5\xaa\xc5\xbd");
		
		ImGui::Separator();
		
		// Common punctuation and currency
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "Symbols:");
		ImGui::Text("Euro: \xe2\x82\xac | Pound: \xc2\xa3 | Yen: \xc2\xa5");
		ImGui::Text("Quotes: \"text\" 'text' <<text>>");
		ImGui::Text("Dash: - \xe2\x80\x93 | Ellipsis: \xe2\x80\xa6");
		
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[Font.Debug] ShowGlyphTestOverlay = false to hide");
	}
	ImGui::End();
}
