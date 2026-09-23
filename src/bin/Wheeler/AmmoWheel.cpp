#include "AmmoWheel.h"
#include "AmmoRangedWeaponPoisonPresentationPolicy.h"
#include "AmmoWheelReskin.h"
#include "AmmoWheelReskinUnified.h"
#include "bin/API/WheelerAPI.h"
#include "bin/Config.h"
#include "bin/Integrations/ExternalFavWheelState.h"
#include "bin/InputBroker.h"
#include "bin/Rendering/Drawer.h"
#include "bin/Rendering/ResolutionScaleContext.h"
#include "bin/Rendering/TextureManager.h"
#include "bin/Texts.h"
#include "bin/UserInput/Controls.h"
#include "bin/Utilities/InventorySnapshotCache.h"
#include "bin/Utilities/Utils.h"
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <set>
#include <chrono>
#include <vector>
#include <SimpleIni.h>
#include "include/lib/nanosvg.h"
#include "include/lib/nanosvgrast.h"

static const char* AMMO_WHEEL_POPUP_ID = "##AmmoWheel";
static const char* AMMO_WHEEL_INI_PATH = "Data\\SKSE\\Plugins\\wheeler\\AmmoWheel.ini";
static const char* AMMO_KID_INI_PATH = "Data\\SKSE\\Plugins\\wheeler\\AMMO_KID.ini";

namespace
{
	InventorySnapshotCache g_ammoWheelInventorySnapshot;
	constexpr double kAmmoWheelDerivedInventoryRefreshIntervalSeconds = 0.25;
	constexpr double kAmmoWheelMountedMomentumAssistSeconds = 1.00;
	constexpr float kAmmoWheelMountedMinimumSlowScale = 0.35f;
	constexpr float kAmmoDamageEpsilon = 0.01f;

	class AmmoWheelPauseMenu : public RE::IMenu
	{
	public:
		static constexpr std::string_view MENU_NAME = "AmmoWheelPauseMenu";

		AmmoWheelPauseMenu()
		{
			menuFlags.set(RE::UI_MENU_FLAGS::kPausesGame);
			// Keep non-modal/non-cursor behavior to preserve normal wheel input handling.
			depthPriority = 0;
		}

		RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage&) override
		{
			return RE::UI_MESSAGE_RESULTS::kPassOn;
		}
	};

	void EnsureAmmoWheelPauseMenuRegistered()
	{
		static bool s_registered = false;
		if (s_registered) {
			return;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return;
		}

		ui->Register(AmmoWheelPauseMenu::MENU_NAME.data(), []() -> RE::IMenu* {
			return new AmmoWheelPauseMenu();
		});
		s_registered = true;
		logger::info("[PauseMenu] Registered AmmoWheelPauseMenu");
	}

	void OpenAmmoWheelPauseMenu()
	{
		EnsureAmmoWheelPauseMenuRegistered();
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return;
		}

		if (!ui->IsMenuOpen(AmmoWheelPauseMenu::MENU_NAME.data())) {
			RE::UIMessageQueue::GetSingleton()->AddMessage(
				AmmoWheelPauseMenu::MENU_NAME.data(), RE::UI_MESSAGE_TYPE::kShow, nullptr);
		}
	}

	void CloseAmmoWheelPauseMenu()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return;
		}

		if (ui->IsMenuOpen(AmmoWheelPauseMenu::MENU_NAME.data())) {
			RE::UIMessageQueue::GetSingleton()->AddMessage(
				AmmoWheelPauseMenu::MENU_NAME.data(), RE::UI_MESSAGE_TYPE::kHide, nullptr);
		}
	}

	std::string NormalizeCenterDescriptionText(std::string text)
	{
		for (char& ch : text) {
			if (ch == '\n' || ch == '\r' || ch == '\t') {
				ch = ' ';
			}
		}

		std::string out;
		out.reserve(text.size());
		bool prevSpace = true;
		for (char ch : text) {
			if (std::isspace(static_cast<unsigned char>(ch))) {
				if (!prevSpace) {
					out.push_back(' ');
					prevSpace = true;
				}
			} else {
				out.push_back(ch);
				prevSpace = false;
			}
		}

		while (!out.empty() && out.back() == ' ') {
			out.pop_back();
		}
		return out;
	}

	template <class Fn>
	bool InvokeAmmoRangedPoisonRead(Fn&& a_fn)
	{
#if defined(_MSC_VER)
		__try {
			a_fn();
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
#else
		try {
			a_fn();
			return true;
		} catch (...) {
			return false;
		}
#endif
	}

	template <class TContainer>
	bool CopyAmmoRangedPoisonExtraLists(
		TContainer* a_extraLists, std::vector<RE::ExtraDataList*>& a_out)
	{
		if (!a_extraLists) {
			return false;
		}
		return InvokeAmmoRangedPoisonRead([&]() {
			for (auto* list : *a_extraLists) {
				a_out.push_back(list);
			}
		});
	}

	AmmoRangedWeaponPoisonPresentationPolicy::WeaponKind ClassifyAmmoRangedPoisonWeapon(
		const RE::TESObjectWEAP* a_weapon)
	{
		using AmmoRangedWeaponPoisonPresentationPolicy::WeaponKind;
		if (!a_weapon) {
			return WeaponKind::kNone;
		}
		switch (a_weapon->GetWeaponType()) {
		case RE::WEAPON_TYPE::kBow:
			return WeaponKind::kBow;
		case RE::WEAPON_TYPE::kCrossbow:
			return WeaponKind::kCrossbow;
		default:
			return WeaponKind::kOther;
		}
	}

	bool IsAmmoRangedPoisonNameUsable(const char* a_text)
	{
		if (!a_text || a_text[0] == '\0') {
			return false;
		}
		std::string compact;
		for (const unsigned char ch : std::string_view(a_text)) {
			if (std::isalnum(ch)) {
				compact.push_back(static_cast<char>(std::tolower(ch)));
			}
		}
		return !compact.empty() && compact != "null" &&
		       compact.find("missingname") == std::string::npos &&
		       compact.find("missingitem") == std::string::npos;
	}

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

	bool IsActionHotkeysModuleLoaded()
	{
		return ::GetModuleHandleW(L"ActionHotkeys.dll") != nullptr;
	}

	bool IsActionHotkeysOverlayLikelyVisible()
	{
		if (!IsActionHotkeysModuleLoaded()) {
			return false;
		}

		auto* controls = RE::PlayerControls::GetSingleton();
		if (!controls || !controls->lookHandler || !controls->movementHandler || !controls->attackBlockHandler) {
			return false;
		}

		return !controls->lookHandler->IsInputEventHandlingEnabled() &&
		       !controls->movementHandler->IsInputEventHandlingEnabled() &&
		       !controls->attackBlockHandler->IsInputEventHandlingEnabled();
	}

	bool IsExternalFavWheelOpen()
	{
		return ExternalFavWheelState::Query(ExternalFavWheelState::ProbeOwner::AmmoWheel).open;
	}

	bool IsDMenuActuallyOpen()
	{
		using DMenuIsMenuOpenFn = bool (*)();
		static DMenuIsMenuOpenFn isMenuOpen = nullptr;
		if (!isMenuOpen) {
			auto* module = ::GetModuleHandleW(L"dmenu.dll");
			if (!module) {
				return false;
			}

			isMenuOpen = reinterpret_cast<DMenuIsMenuOpenFn>(::GetProcAddress(module, "dMenu_IsMenuOpen"));
			if (!isMenuOpen) {
				return false;
			}
		}

		return isMenuOpen();
	}

	bool TryGetExternalWheelBlockingReason(std::string_view& outReason, bool a_allowDMenuOwner)
	{
		if (IsExternalFavWheelOpen()) {
			outReason = "FavWheelOpen";
			return true;
		}
		if (!InputBroker::IsBlockedByActiveOwner(InputBroker::kWheelerRefinedPluginId)) {
			return false;
		}
		if (a_allowDMenuOwner && InputBroker::GetActiveOwner() == InputBroker::kDMenuInputOwnerId) {
			return false;
		}

		outReason = "InputBrokerActiveOwner";
		return true;
	}

	constexpr std::array<std::string_view, 8> kAmmoWheelPassiveOverlayMenus{
		RE::HUDMenu::MENU_NAME,
		RE::CursorMenu::MENU_NAME,
		"Fader Menu",
		"Mist Menu",
		"TrueHUD",
		"CombatAlertOverlayMenu",
		AmmoWheelPauseMenu::MENU_NAME,
		"WheelerPauseMenu"
	};

	template <std::size_t N>
	bool ContainsMenuName(const std::array<std::string_view, N>& menuNames, std::string_view menuName)
	{
		return std::find(menuNames.begin(), menuNames.end(), menuName) != menuNames.end();
	}

	constexpr std::array<std::string_view, 4> kAmmoWheelLegacyDMenuNames{
		"dmenu",
		"dmenu_Main",
		"dMenu",
		"dMenu_Main"
	};

	bool IsAmmoWheelLegacyDMenuName(std::string_view menuName)
	{
		return ContainsMenuName(kAmmoWheelLegacyDMenuNames, menuName);
	}

	bool HasMenuMovie(const RE::IMenu* menu)
	{
		return menu && menu->uiMovie.get() != nullptr;
	}

	bool IsMenuMovieVisible(const RE::IMenu* menu)
	{
		return HasMenuMovie(menu) && menu->uiMovie->GetVisible();
	}

	bool HasActiveInteractionFlags(const RE::IMenu* menu)
	{
		if (!menu) {
			return false;
		}

		return menu->UsesCursor() ||
		       menu->UpdateUsesCursor() ||
		       menu->UsesMenuContext() ||
		       menu->Modal() ||
		       menu->ApplicationMenu() ||
		       menu->InventoryItemMenu();
	}

	bool IsLikelyPassiveAlwaysOpenOverlay(const RE::IMenu* menu)
	{
		if (!menu) {
			return false;
		}

		return menu->AlwaysOpen() && !HasActiveInteractionFlags(menu);
	}

	bool TryGetGenericBlockingMenu(RE::UI* ui, std::string_view& outMenuName, bool a_ignoreLegacyDMenuEntries = false)
	{
		if (!ui) {
			return false;
		}

		for (const auto& [menuKey, menuEntry] : ui->menuMap) {
			const auto* menu = menuEntry.menu.get();
			if (!menu || !menu->OnStack()) {
				continue;
			}

			const char* rawName = menuKey.c_str();
			const std::string_view menuName =
				(rawName && rawName[0] != '\0') ? std::string_view(rawName) : std::string_view("<unnamed>");

			if (a_ignoreLegacyDMenuEntries && IsAmmoWheelLegacyDMenuName(menuName)) {
				continue;
			}
			if (ContainsMenuName(kAmmoWheelPassiveOverlayMenus, menuName)) {
				continue;
			}
			if (!IsMenuMovieVisible(menu)) {
				continue;
			}
			if (IsLikelyPassiveAlwaysOpenOverlay(menu)) {
				continue;
			}
			if (!HasActiveInteractionFlags(menu)) {
				continue;
			}

			outMenuName = menuName;
			return true;
		}

		return false;
	}

	RE::TESAmmo* LookupOwnedAmmoByFormID(RE::PlayerCharacter* player, RE::FormID ammoID)
	{
		if (!player || ammoID == 0) {
			return nullptr;
		}

		auto* ammo = RE::TESForm::LookupByID<RE::TESAmmo>(ammoID);
		if (!ammo) {
			return nullptr;
		}

		const auto inventoryCounts = player->GetInventoryCounts();
		auto it = inventoryCounts.find(ammo);
		if (it == inventoryCounts.end() || it->second <= 0) {
			return nullptr;
		}

		return ammo;
	}

	bool CanAutoRestoreAmmoWheelSelectionNow()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}

		static constexpr std::array<std::string_view, 13> blockingMenus{
			RE::LoadingMenu::MENU_NAME,
			RE::InventoryMenu::MENU_NAME,
			RE::MagicMenu::MENU_NAME,
			RE::FavoritesMenu::MENU_NAME,
			RE::ContainerMenu::MENU_NAME,
			RE::BarterMenu::MENU_NAME,
			RE::CraftingMenu::MENU_NAME,
			RE::GiftMenu::MENU_NAME,
			RE::TweenMenu::MENU_NAME,
			RE::JournalMenu::MENU_NAME,
			"LootMenu",
			"LootMenuCF",
			"dmenu"
		};

		for (std::string_view menuName : blockingMenus) {
			if (ui->IsMenuOpen(menuName)) {
				return false;
			}
		}

		if (IsActionHotkeysOverlayLikelyVisible()) {
			return false;
		}

		std::string_view genericBlockedMenu;
		if (TryGetGenericBlockingMenu(ui, genericBlockedMenu)) {
			return false;
		}

		return true;
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

	// ========== ROTATED QUAD RENDERING ==========
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

	// ========== INDICATOR RENDERING ==========
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

void AmmoWheel::ResetMountedVelocityRestoreState()
{
	_ammoWheelRestoreMountedVelocityOnClose = false;
	_ammoWheelMountedVelocityMountFormID = 0;
	_ammoWheelMountedVelocitySnapshot = RE::NiPoint3{ 0.0f, 0.0f, 0.0f };
	_ammoWheelMountedMomentumAssistUntil = 0.0;
}

void AmmoWheel::TryRestoreMountedVelocityAfterTimeRestore(const char* a_reason)
{
	if (!_ammoWheelRestoreMountedVelocityOnClose) {
		return;
	}

	const bool restored = Utils::Player::TryRestoreMountedVelocity(
		_ammoWheelMountedVelocityMountFormID,
		_ammoWheelMountedVelocitySnapshot,
		true);

	logger::info("[TimeDilation] AmmoWheel mounted velocity restore ({}): ok={}, mount={:08X}, v=({:.2f},{:.2f},{:.2f})",
		a_reason ? a_reason : "unknown",
		restored,
		_ammoWheelMountedVelocityMountFormID,
		_ammoWheelMountedVelocitySnapshot.x,
		_ammoWheelMountedVelocitySnapshot.y,
		_ammoWheelMountedVelocitySnapshot.z);

	const double now = ImGui::GetTime();
	_ammoWheelMountedMomentumAssistUntil = (std::max)(_ammoWheelMountedMomentumAssistUntil, now + kAmmoWheelMountedMomentumAssistSeconds);
	logger::info("[TimeDilation] AmmoWheel mounted momentum assist: duration={:.2f}s, until={:.3f}", kAmmoWheelMountedMomentumAssistSeconds, _ammoWheelMountedMomentumAssistUntil);
}

void AmmoWheel::RestoreTimescale()
{
	if (_ammoWheelOwnedPauseMenu) {
		logger::info("[TimeDilation] AmmoWheel restore: closing owned pause menu");
		CloseAmmoWheelPauseMenu();
		_ammoWheelOwnedPauseMenu = false;
	}

	if (_ammoWheelModifiedTimeScale) {
		float current = Utils::Time::GGTM();
		logger::info("[TimeDilation] AmmoWheel restore: current={:.3f}, restoreTo={:.3f}", 
			current, _preAmmoWheelTimeScale);
		Utils::Time::SGTM(_preAmmoWheelTimeScale);
		_ammoWheelModifiedTimeScale = false;
		TryRestoreMountedVelocityAfterTimeRestore("RestoreTimescale");
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

	if (_ammoWheelOwnedPauseMenu) {
		logger::info("[TimeDilation] AmmoWheel restore: force-close owned pause menu");
		CloseAmmoWheelPauseMenu();
		_ammoWheelOwnedPauseMenu = false;
	}

	if (_ammoWheelModifiedTimeScale) {
		Utils::Time::SGTM(_preAmmoWheelTimeScale);
		_ammoWheelModifiedTimeScale = false;
		TryRestoreMountedVelocityAfterTimeRestore("ForceClose");
	}

	if (ImGui::GetCurrentContext() && ImGui::IsPopupOpen(AMMO_WHEEL_POPUP_ID)) {
		ImGui::SetNextWindowPos(ImVec2(-100.0f, -100.0f));
		if (ImGui::BeginPopup(AMMO_WHEEL_POPUP_ID)) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
	
	_state = WheelState::Closed;
	_hoveredIndex = -1;
	_prevHoveredIndex = -1;  // Reset hysteresis state
	_hoveredTime = 0.f;
	_cursorPos = { 0, 0 };
	_mousePendingHoverIndex = -1;
	_mouseAccumulatedDelta = { 0.0f, 0.0f };
	_mouseAccumulatedPeak = 0.0f;
	_mouseSlotCarry = 0.0f;
	_mouseStepLatchDirection = 0;
	_pendingCloseOnReleaseButton = -1;
	_openTimer = 0.f;
	_closeTimer = 0.f;
	_blockMainWheel = false;
	_activationConsumed = false;  // Reset debounce
	InputBroker::ClearActiveOwner(InputBroker::kWheelerRefinedPluginId);
	g_ammoWheelInventorySnapshot.Invalidate();
	InvalidateRuntimeCaches();
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

bool AmmoWheel::IsAmmoCompatibleWithWeaponType(RE::TESAmmo* a_ammo, WeaponType a_weaponType) const
{
	if (!a_ammo) {
		return false;
	}

	switch (a_weaponType) {
	case WeaponType::Bow:
		return !a_ammo->IsBolt();
	case WeaponType::Crossbow:
		return a_ammo->IsBolt();
	default:
		return false;
	}
}

bool AmmoWheel::IsAmmoCompatibleWithWeaponType(RE::FormID a_ammoID, WeaponType a_weaponType) const
{
	if (a_ammoID == 0) {
		return false;
	}

	return IsAmmoCompatibleWithWeaponType(RE::TESForm::LookupByID<RE::TESAmmo>(a_ammoID), a_weaponType);
}

RE::FormID AmmoWheel::GetRememberedAmmoForWeaponType(WeaponType a_weaponType) const
{
	switch (a_weaponType) {
	case WeaponType::Bow:
		return _rememberedBowAmmoID;
	case WeaponType::Crossbow:
		return _rememberedCrossbowAmmoID;
	default:
		return 0;
	}
}

void AmmoWheel::SetRememberedAmmoForWeaponType(WeaponType a_weaponType, RE::FormID a_ammoID)
{
	switch (a_weaponType) {
	case WeaponType::Bow:
		_rememberedBowAmmoID = a_ammoID;
		break;
	case WeaponType::Crossbow:
		_rememberedCrossbowAmmoID = a_ammoID;
		break;
	default:
		break;
	}
}

void AmmoWheel::SyncRememberedAmmoForWeaponType(WeaponType a_weaponType, RE::FormID a_ammoID, const char* a_reason)
{
	if (a_weaponType == WeaponType::None || !IsAmmoCompatibleWithWeaponType(a_ammoID, a_weaponType)) {
		return;
	}

	const RE::FormID previousAmmoID = GetRememberedAmmoForWeaponType(a_weaponType);
	if (previousAmmoID == a_ammoID) {
		return;
	}

	SetRememberedAmmoForWeaponType(a_weaponType, a_ammoID);
	logger::info("AmmoWheel: remembered {} ammo {:08X} -> {:08X} ({})",
		a_weaponType == WeaponType::Bow ? "bow" : "crossbow",
		previousAmmoID,
		a_ammoID,
		a_reason ? a_reason : "unknown");
}

bool AmmoWheel::TryRestoreRememberedAmmoForWeaponType(WeaponType a_weaponType, RE::FormID a_currentAmmoID)
{
	if (a_weaponType == WeaponType::None) {
		return true;
	}
	if (_state != WheelState::Closed) {
		return false;
	}
	if (!CanAutoRestoreAmmoWheelSelectionNow()) {
		return false;
	}

	const RE::FormID rememberedAmmoID = GetRememberedAmmoForWeaponType(a_weaponType);
	if (rememberedAmmoID == 0) {
		return true;
	}
	if (a_currentAmmoID == rememberedAmmoID) {
		return true;
	}
	if (!IsAmmoCompatibleWithWeaponType(rememberedAmmoID, a_weaponType)) {
		return true;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* equipManager = RE::ActorEquipManager::GetSingleton();
	if (!player || !equipManager) {
		return false;
	}

	RE::TESAmmo* rememberedAmmo = LookupOwnedAmmoByFormID(player, rememberedAmmoID);
	if (!rememberedAmmo) {
		logger::info("AmmoWheel: skipped {} ammo restore remembered={:08X} reason=not_owned",
			a_weaponType == WeaponType::Bow ? "bow" : "crossbow",
			rememberedAmmoID);
		return true;
	}

	equipManager->EquipObject(player, rememberedAmmo);
	logger::info("AmmoWheel: restored {} ammo memory current={:08X} restored={:08X}",
		a_weaponType == WeaponType::Bow ? "bow" : "crossbow",
		a_currentAmmoID,
		rememberedAmmoID);
	return true;
}

int AmmoWheel::FindInitialHoverIndex()
{
	if (_ammoEntries.empty()) return -1;
	
	// 1. Priority: Try to restore last selected specific ammo (most intuitive)
	RE::FormID preferredAmmoID = _lastSelectedAmmoID;
	if (Config::AmmoWheel::Sort::RememberAmmoByWeaponType) {
		if (const RE::FormID rememberedAmmoID = GetRememberedAmmoForWeaponType(_currentWeaponType);
			rememberedAmmoID != 0) {
			preferredAmmoID = rememberedAmmoID;
		}
	}
	if (preferredAmmoID != 0) {
		for (int i = 0; i < static_cast<int>(_ammoEntries.size()); i++) {
			if (_ammoEntries[i].ammo && _ammoEntries[i].ammo->GetFormID() == preferredAmmoID) {
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
	
	// AmmoWheel is gameplay-only and should never stay active without a ranged weapon.
	if (!ShouldBeAvailable()) {
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
		
		// Modifier-key state is not available through this input path.
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
	if ((_state == WheelState::Opened || _state == WheelState::Opening) && !CanOpen(true)) {
		logger::info("AmmoWheel: CLOSE (gameplay context lost)");
		ForceClose();
		return;
	}

	// Keep mount momentum continuous while timeslow is active and briefly after release.
	if (_ammoWheelRestoreMountedVelocityOnClose) {
		const double now = ImGui::GetTime();
		const bool assistActive = _ammoWheelModifiedTimeScale || (now < _ammoWheelMountedMomentumAssistUntil);
		if (assistActive) {
			Utils::Player::TryRestoreMountedVelocity(
				_ammoWheelMountedVelocityMountFormID,
				_ammoWheelMountedVelocitySnapshot,
				true);
		} else {
			ResetMountedVelocityRestoreState();
		}
	}

	// Handle closed state - close popup if open
	if (_state == WheelState::Closed) {
		// SAFETY: if wheel is closed but time-state flags are still set, restore immediately.
		if (_ammoWheelModifiedTimeScale || _ammoWheelOwnedPauseMenu) {
			if (_ammoWheelModifiedTimeScale) {
				logger::warn("[TimeDilation] Safety restore: AmmoWheel closed but timescale flag was stuck");
			}
			if (_ammoWheelOwnedPauseMenu) {
				logger::warn("[TimeDilation] Safety restore: AmmoWheel closed but pause menu ownership was stuck");
			}
			RestoreTimescale();
		}

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
				g_ammoWheelInventorySnapshot.Invalidate();
				InvalidateRuntimeCaches();
			}
			break;
		default:
			break;
		}

		DrawArgs drawArgs;
		// Apply CustomOpacity to the final alpha.
		float customOpacity = std::clamp(Config::AmmoWheel::CustomOpacity, 0.0f, 1.0f);
		drawArgs.alphaMult = fadeLerp * customOpacity;

		draw(drawArgs);

		drawList->PopClipRect();
		ImGui::EndPopup();
	}
}

void AmmoWheel::OnConfigChanged()
{
	// Any layout-affecting setting update should force cached label/center rebuild.
	_configRevision++;

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
	
	// ========== VALIDATE VISUAL POLISH SETTINGS ==========
	// Clamp visual polish values to safe ranges to prevent rendering issues
	Config::AmmoWheel::BorderInnerScale = std::clamp(Config::AmmoWheel::BorderInnerScale, 0.9f, 1.5f);
	Config::AmmoWheel::BorderOuterScale = std::clamp(Config::AmmoWheel::BorderOuterScale, 0.95f, 1.6f);
	Config::AmmoWheel::BackgroundRadiusScale = std::clamp(Config::AmmoWheel::BackgroundRadiusScale, 0.8f, 1.5f);
	Config::AmmoWheel::BackgroundOpacity = std::clamp(Config::AmmoWheel::BackgroundOpacity, 0.0f, 1.0f);
	Config::AmmoWheel::BackgroundSoftEdgeRatio = std::clamp(Config::AmmoWheel::BackgroundSoftEdgeRatio, 0.0f, 0.95f);
	Config::AmmoWheel::CustomOpacity = std::clamp(Config::AmmoWheel::CustomOpacity, 0.1f, 1.0f);
	
	// Ensure border scales are ordered correctly (inner < outer)
	if (Config::AmmoWheel::BorderInnerScale >= Config::AmmoWheel::BorderOuterScale) {
		Config::AmmoWheel::BorderOuterScale = Config::AmmoWheel::BorderInnerScale + 0.03f;
		if (Config::AmmoWheel::DebugLogNavigation) {
			logger::warn("[AmmoWheel] BorderInnerScale >= BorderOuterScale, auto-corrected");
		}
	}
	
	// Reset navigation filters only when navigation settings changed in Live mode.
	// Avoid resetting cursor/hover during unrelated visual slider updates.
	struct NavConfigSnapshot
	{
		float mouseDeadzone{ 0.0f };
		float mouseSmoothing{ 0.0f };
		float gamepadDeadzone{ 0.0f };
		float gamepadSmoothing{ 0.0f };
		float arcDeadbandDeg{ 0.0f };
		float gamepadHysteresisDeg{ 0.0f };
		int halfWheelClampMode{ 0 };
		int applyMode{ 0 };
	};
	auto buildNavSnapshot = []() -> NavConfigSnapshot {
		return NavConfigSnapshot{
			Config::AmmoWheel::MouseDeadzone,
			Config::AmmoWheel::MouseSmoothingSpeed,
			Config::AmmoWheel::GamepadDeadzone,
			Config::AmmoWheel::GamepadSmoothingSpeed,
			Config::AmmoWheel::ArcSelectionDeadbandDeg,
			Config::AmmoWheel::GamepadHoverHysteresisDeg,
			Config::AmmoWheel::HalfWheelClampMode,
			Config::AmmoWheel::NavigationApplyMode
		};
	};
	auto navDiffers = [](const NavConfigSnapshot& a, const NavConfigSnapshot& b) {
		constexpr float eps = 0.001f;
		return std::fabs(a.mouseDeadzone - b.mouseDeadzone) > eps ||
			std::fabs(a.mouseSmoothing - b.mouseSmoothing) > eps ||
			std::fabs(a.gamepadDeadzone - b.gamepadDeadzone) > eps ||
			std::fabs(a.gamepadSmoothing - b.gamepadSmoothing) > eps ||
			std::fabs(a.arcDeadbandDeg - b.arcDeadbandDeg) > eps ||
			std::fabs(a.gamepadHysteresisDeg - b.gamepadHysteresisDeg) > eps ||
			a.halfWheelClampMode != b.halfWheelClampMode ||
			a.applyMode != b.applyMode;
	};
	static bool s_navSnapshotValid = false;
	static NavConfigSnapshot s_lastNavSnapshot{};
	const NavConfigSnapshot navSnapshotNow = buildNavSnapshot();
	const bool navSettingsChanged = s_navSnapshotValid && navDiffers(navSnapshotNow, s_lastNavSnapshot);
	s_lastNavSnapshot = navSnapshotNow;
	s_navSnapshotValid = true;
	if (Config::AmmoWheel::NavigationApplyMode == 0 && navSettingsChanged) {
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

bool AmmoWheel::CanOpen(bool a_allowDMenuOverlay) const
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

	const bool dMenuOverlayOpen = a_allowDMenuOverlay && IsDMenuActuallyOpen();
	std::string_view externalWheelReason;
	if (TryGetExternalWheelBlockingReason(externalWheelReason, dMenuOverlayOpen)) {
		logger::debug("AmmoWheel::CanOpen rejected: external wheel is active ({})", externalWheelReason);
		return false;
	}

	// AmmoWheel is gameplay-only. Use an explicit blacklist here; unknown overlays are
	// intentionally ignored so hidden/passive HUD-style SKSE/ImGui menus cannot block open.
	// Pure ImGui overlays that do not register RE::UI menu names need a plugin-specific guard.
	static constexpr std::array<std::string_view, 35> conflictingMenus({
		RE::BookMenu::MENU_NAME,
		RE::BarterMenu::MENU_NAME,
		RE::CraftingMenu::MENU_NAME,
		RE::InventoryMenu::MENU_NAME,
		RE::JournalMenu::MENU_NAME,
		RE::LevelUpMenu::MENU_NAME,
		RE::LockpickingMenu::MENU_NAME,
		RE::LoadingMenu::MENU_NAME,
		RE::MainMenu::MENU_NAME,
		RE::MapMenu::MENU_NAME,
		RE::MessageBoxMenu::MENU_NAME,
		RE::RaceSexMenu::MENU_NAME,
		RE::SleepWaitMenu::MENU_NAME,
		RE::StatsMenu::MENU_NAME,
		RE::TrainingMenu::MENU_NAME,
		RE::TweenMenu::MENU_NAME,
		RE::TutorialMenu::MENU_NAME,
		RE::Console::MENU_NAME,
		RE::ConsoleNativeUIMenu::MENU_NAME,
		RE::DialogueMenu::MENU_NAME,
		RE::GiftMenu::MENU_NAME,
		RE::MagicMenu::MENU_NAME,
		RE::ModManagerMenu::MENU_NAME,
		RE::FavoritesMenu::MENU_NAME,
		RE::ContainerMenu::MENU_NAME,
		"LootMenu",
		"LootMenuCF",
		"BestiaryMenu",
		"CustomMenu",
		"RaceMenu",
		"ShowStats",
		"dmenu",
		"dmenu_Main",
		"dMenu",
		"dMenu_Main"
	});

	for (std::string_view menuName : conflictingMenus) {
		if (ui->IsMenuOpen(menuName)) {
			if (dMenuOverlayOpen && IsAmmoWheelLegacyDMenuName(menuName)) {
				continue;
			}
			logger::debug("AmmoWheel::CanOpen rejected: menu '{}' is open", menuName);
			return false;
		}
	}

	// Action Hotkeys uses pure ImGui windows, so there is no RE::UI menu name to blacklist.
	// It disables gameplay handlers while its grid is visible; use that plugin-specific signal.
	if (IsActionHotkeysOverlayLikelyVisible()) {
		logger::debug("AmmoWheel::CanOpen rejected: Action Hotkeys overlay is visible");
		return false;
	}

	std::string_view genericBlockedMenu;
	if (TryGetGenericBlockingMenu(ui, genericBlockedMenu, dMenuOverlayOpen)) {
		logger::debug("AmmoWheel::CanOpen rejected: active menu '{}' is open", genericBlockedMenu);
		return false;
	}

	// AmmoWheel is only available when the player is actively holding a ranged weapon.
	if (!ShouldBeAvailable()) {
		logger::debug("AmmoWheel::CanOpen rejected: no ranged weapon equipped");
		return false;
	}

	return true;
}

void AmmoWheel::UpdateWeaponState()
{
	const WeaponType previousWeaponType = _currentWeaponType;
	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		_currentWeaponType = WeaponType::None;
		_pendingRememberedAmmoRestoreType = WeaponType::None;
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
		_pendingRememberedAmmoRestoreType = WeaponType::None;
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

	if (!Config::AmmoWheel::Sort::RememberAmmoByWeaponType) {
		_pendingRememberedAmmoRestoreType = WeaponType::None;
		return;
	}

	const bool weaponTypeChanged = previousWeaponType != _currentWeaponType;
	if (_currentWeaponType == WeaponType::None) {
		_pendingRememberedAmmoRestoreType = WeaponType::None;
		return;
	}
	if (weaponTypeChanged) {
		_pendingRememberedAmmoRestoreType = _currentWeaponType;
	}

	const RE::FormID currentAmmoIDBeforeRestore = getEquippedAmmoFormID();
	if (_pendingRememberedAmmoRestoreType == _currentWeaponType) {
		if (TryRestoreRememberedAmmoForWeaponType(_currentWeaponType, currentAmmoIDBeforeRestore)) {
			_pendingRememberedAmmoRestoreType = WeaponType::None;
		}
	}

	if (_pendingRememberedAmmoRestoreType != _currentWeaponType) {
		SyncRememberedAmmoForWeaponType(_currentWeaponType, getEquippedAmmoFormID(), "UpdateWeaponState");
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

void AmmoWheel::InvalidateRuntimeCaches()
{
	_damageCacheValid = false;
	_cachedMaxDamage = 0.0f;
	_cachedMaxDamageTies = 0;
	_damageCacheRevision++;
	_centerPanelCache = CenterPanelCache{};
	_centerPanelStableWidth = 0.0f;
	_centerPanelStableHeight = 0.0f;
	_centerPanelStableConfigRevision = 0;
	_centerPanelStableViewport = { 0, 0 };
	_rangedWeaponPoisonPresentation = RangedWeaponPoisonPresentation{};
	++_rangedWeaponPoisonRevision;
	_damageRebuildMsAccum = 0.0;
	_damageRebuildCount = 0;
	_labelRebuildMsAccum = 0.0;
	_labelRebuildCount = 0;
	_centerRebuildMsAccum = 0.0;
	_centerRebuildCount = 0;

	for (auto& entry : _ammoEntries) {
		entry.cachedDamage = entry.ammo ? entry.ammo->data.damage : 0.0f;
		entry.damageValid = false;

		entry.labelDisplayNameCached.clear();
		entry.labelLayoutCached = TextLayout{};
		entry.labelFontSizeCached = 0.0f;
		entry.labelAvailWidthCached = 0.0f;
		entry.labelLineSpacingCached = 0.0f;
		entry.labelTotalTextHeightCached = 0.0f;
		entry.labelClipHalfWidthCached = 0.0f;
		entry.labelClipHalfHeightCached = 0.0f;
		entry.labelBgHalfWidthCached = 0.0f;
		entry.labelUsesReskinPathCached = false;
		entry.labelLayoutValid = false;
		entry.labelLayoutRevision = 0;
		entry.labelLayoutViewport = { 0, 0 };
		entry.labelLayoutWheelCenterX = 0.0f;
	}
}

AmmoWheel::RangedWeaponPoisonPresentation AmmoWheel::ResolveRangedWeaponPoisonPresentation(
	RE::PlayerCharacter* a_player,
	const RE::TESObjectREFR::InventoryItemMap& a_inventory) const
{
	using namespace AmmoRangedWeaponPoisonPresentationPolicy;

	if (!a_player) {
		return {};
	}

	RE::TESObjectWEAP* rightWeapon = nullptr;
	RE::TESObjectWEAP* leftWeapon = nullptr;
	RE::FormID rightFormID = 0;
	RE::FormID leftFormID = 0;
	WeaponKind rightKind = WeaponKind::kNone;
	WeaponKind leftKind = WeaponKind::kNone;
	bool scalarComplete = false;
	const bool scalarReadable = InvokeAmmoRangedPoisonRead([&]() {
		auto* rightForm = a_player->GetEquippedObject(false);
		auto* leftForm = a_player->GetEquippedObject(true);
		rightWeapon = rightForm ? rightForm->As<RE::TESObjectWEAP>() : nullptr;
		leftWeapon = leftForm ? leftForm->As<RE::TESObjectWEAP>() : nullptr;
		rightFormID = rightWeapon ? rightWeapon->GetFormID() : 0;
		leftFormID = leftWeapon ? leftWeapon->GetFormID() : 0;
		rightKind = ClassifyAmmoRangedPoisonWeapon(rightWeapon);
		leftKind = ClassifyAmmoRangedPoisonWeapon(leftWeapon);
		scalarComplete = true;
	});
	if (!scalarReadable || !scalarComplete) {
		return {};
	}

	RE::TESObjectWEAP* rangedWeapon = nullptr;
	WeaponKind rangedKind = WeaponKind::kNone;
	RE::FormID rangedFormID = 0;
	if (IsRanged(rightKind)) {
		rangedWeapon = rightWeapon;
		rangedKind = rightKind;
		rangedFormID = rightFormID;
	} else if (IsRanged(leftKind)) {
		rangedWeapon = leftWeapon;
		rangedKind = leftKind;
		rangedFormID = leftFormID;
	}
	if (!rangedWeapon || rangedFormID == 0) {
		return {};
	}

	// A physical bow/crossbow may be visible through both hand queries. A distinct
	// opposite-hand weapon makes ownership ambiguous and therefore fails closed.
	const RE::FormID oppositeFormID = rangedWeapon == rightWeapon ? leftFormID : rightFormID;
	if (oppositeFormID != 0 && oppositeFormID != rangedFormID) {
		return {};
	}

	Evidence evidence{};
	evidence.weaponKind = rangedKind;
	evidence.weaponFormID = rangedFormID;

	const auto inventoryIt = a_inventory.find(rangedWeapon);
	if (inventoryIt == a_inventory.end() || inventoryIt->second.first <= 0 ||
	    !inventoryIt->second.second) {
		return {};
	}

	RE::InventoryEntryData* entry = inventoryIt->second.second.get();
	decltype(entry->extraLists) extraLists = nullptr;
	if (!InvokeAmmoRangedPoisonRead([&]() { extraLists = entry->extraLists; }) || !extraLists) {
		return {};
	}

	std::vector<RE::ExtraDataList*> extraListSnapshot;
	if (!CopyAmmoRangedPoisonExtraLists(extraLists, extraListSnapshot)) {
		return {};
	}

	std::vector<MemberEvidence> members;
	members.reserve(extraListSnapshot.size());
	for (auto* extraList : extraListSnapshot) {
		MemberEvidence member{};
		bool complete = false;
		const bool guarded = extraList && InvokeAmmoRangedPoisonRead([&]() {
			const bool hasWorn = extraList->HasType(RE::ExtraDataType::kWorn);
			const bool hasWornLeft = extraList->HasType(RE::ExtraDataType::kWornLeft);
			member.wornLeft = hasWornLeft;
			member.wornRight = hasWorn && !hasWornLeft;

			// Poison is read only from a candidate equipped physical member. An
			// unequipped same-form sibling can never contribute poison evidence.
			if (member.wornRight || member.wornLeft) {
				member.hasPoison = extraList->HasType(RE::ExtraDataType::kPoison);
				if (member.hasPoison) {
					const auto* extraPoison = extraList->GetByType<RE::ExtraPoison>();
					if (!extraPoison) {
						return;
					}
					member.poisonCount = extraPoison->count;
					member.poisonPointerValid = extraPoison->poison != nullptr;
					if (extraPoison->poison) {
						member.poisonFormID = extraPoison->poison->GetFormID();
					}
				}
			}
			complete = true;
		});
		member.readable = guarded && complete;
		members.push_back(member);
	}

	evidence.inventoryReadable = true;
	evidence.members = members;
	RE::FormID rightFormIDAfter = 0;
	RE::FormID leftFormIDAfter = 0;
	bool stableComplete = false;
	const bool stableReadable = InvokeAmmoRangedPoisonRead([&]() {
		auto* rightForm = a_player->GetEquippedObject(false);
		auto* leftForm = a_player->GetEquippedObject(true);
		auto* right = rightForm ? rightForm->As<RE::TESObjectWEAP>() : nullptr;
		auto* left = leftForm ? leftForm->As<RE::TESObjectWEAP>() : nullptr;
		rightFormIDAfter = right ? right->GetFormID() : 0;
		leftFormIDAfter = left ? left->GetFormID() : 0;
		stableComplete = true;
	});
	evidence.equippedStateStable = stableReadable && stableComplete &&
	                               rightFormIDAfter == rightFormID &&
	                               leftFormIDAfter == leftFormID;

	const auto resolved = Resolve(evidence);
	if (!resolved.targetResolved) {
		return {};
	}

	RangedWeaponPoisonPresentation presentation{};
	presentation.targetResolved = true;
	presentation.weaponFormID = resolved.weaponFormID;
	if (resolved.poisonFormID == 0) {
		return presentation;
	}

	bool poisonComplete = false;
	const bool poisonReadable = InvokeAmmoRangedPoisonRead([&]() {
		auto* poison = RE::TESForm::LookupByID<RE::AlchemyItem>(resolved.poisonFormID);
		if (!poison || poison->GetFormID() != resolved.poisonFormID) {
			return;
		}
		const char* rawName = poison->GetName();
		if (!IsAmmoRangedPoisonNameUsable(rawName)) {
			return;
		}
		presentation.poisonFormID = resolved.poisonFormID;
		presentation.poisonName = NormalizeCenterDescriptionText(rawName);
		poisonComplete = !presentation.poisonName.empty();
	});
	return poisonReadable && poisonComplete ? presentation : RangedWeaponPoisonPresentation{};
}

void AmmoWheel::UpdateRangedWeaponPoisonPresentation(
	RE::PlayerCharacter* a_player,
	const RE::TESObjectREFR::InventoryItemMap& a_inventory,
	bool a_inventoryReadable,
	bool a_refreshDue)
{
	if (a_inventoryReadable && !a_refreshDue) {
		return;
	}

	const RangedWeaponPoisonPresentation next = a_inventoryReadable ?
		ResolveRangedWeaponPoisonPresentation(a_player, a_inventory) :
		RangedWeaponPoisonPresentation{};
	if (next == _rangedWeaponPoisonPresentation) {
		return;
	}
	_rangedWeaponPoisonPresentation = next;
	++_rangedWeaponPoisonRevision;
}

void AmmoWheel::EnsureCenterFieldOrderCache()
{
	if (_centerFieldOrderRevision == _configRevision && !_centerFieldOrderCache.empty()) {
		return;
	}

	_centerFieldOrderCache.clear();
	std::string orderStr = Config::AmmoWheel::CenterFields::Order;
	bool hasKnownCenterField = false;
	size_t start = 0;
	while (start <= orderStr.size()) {
		size_t comma = orderStr.find(',', start);
		size_t end = (comma == std::string::npos) ? orderStr.size() : comma;
		std::string token = orderStr.substr(start, end - start);

		// Trim whitespace around each field token.
		size_t left = token.find_first_not_of(" \t\r\n");
		size_t right = token.find_last_not_of(" \t\r\n");
		if (left != std::string::npos && right != std::string::npos) {
			token = token.substr(left, right - left + 1);
			if (!token.empty()) {
				const bool knownCenterField =
					token == "Name" || token == "Damage" || token == "Poison" ||
					token == "Type" || token == "Count" || token == "Source";
				hasKnownCenterField = hasKnownCenterField || knownCenterField;
				if (!knownCenterField ||
					std::find(_centerFieldOrderCache.begin(), _centerFieldOrderCache.end(), token) == _centerFieldOrderCache.end()) {
					_centerFieldOrderCache.push_back(token);
				}
			}
		}

		if (comma == std::string::npos) {
			break;
		}
		start = comma + 1;
	}

	if (_centerFieldOrderCache.empty()) {
		_centerFieldOrderCache = { "Name", "Damage", "Poison", "Type", "Count", "Source" };
	}

	// Legacy custom orders stay authoritative, except that an enabled Poison field
	// is inserted once without rewriting the user's INI.
	if (Config::AmmoWheel::CenterFields::ShowPoison && hasKnownCenterField &&
		std::find(_centerFieldOrderCache.begin(), _centerFieldOrderCache.end(), "Poison") == _centerFieldOrderCache.end()) {
		const auto damageIt = std::find(_centerFieldOrderCache.begin(), _centerFieldOrderCache.end(), "Damage");
		if (damageIt != _centerFieldOrderCache.end()) {
			_centerFieldOrderCache.insert(damageIt + 1, "Poison");
		} else {
			_centerFieldOrderCache.emplace_back("Poison");
		}
	}

	_centerFieldOrderRevision = _configRevision;
}

bool AmmoWheel::RebuildDamageCache(const RE::TESObjectREFR::InventoryItemMap& a_imap)
{
	const bool needsDamageCache = Config::AmmoWheel::CenterEnabled && Config::AmmoWheel::CenterFields::ShowDamage;
	if (!needsDamageCache) {
		if (_damageCacheValid) {
			return false;
		}
		for (auto& entry : _ammoEntries) {
			entry.cachedDamage = entry.ammo ? entry.ammo->data.damage : 0.0f;
			entry.damageValid = true;
		}
		_cachedMaxDamage = 0.0f;
		_cachedMaxDamageTies = 0;
		_damageCacheValid = true;
		_damageCacheRevision++;
		_centerPanelCache.valid = false;
		return true;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	float maxDamage = 0.0f;
	int ties = 0;

	for (auto& entry : _ammoEntries) {
		if (!entry.ammo) {
			entry.cachedDamage = 0.0f;
			entry.damageValid = false;
			continue;
		}

		float damage = entry.ammo->data.damage;
		auto it = a_imap.find(entry.ammo);
		if (it != a_imap.end() && it->second.second && player) {
			if (auto* invEntry = it->second.second.get()) {
				damage = player->GetDamage(invEntry);
			}
		}

		entry.cachedDamage = damage;
		entry.damageValid = true;

		if (damage > maxDamage + kAmmoDamageEpsilon) {
			maxDamage = damage;
			ties = 1;
		} else if (std::abs(damage - maxDamage) <= kAmmoDamageEpsilon) {
			ties++;
		}
	}

	_cachedMaxDamage = maxDamage;
	_cachedMaxDamageTies = ties;
	_damageCacheValid = true;
	_damageCacheRevision++;
	_centerPanelCache.valid = false;
	return true;
}

bool AmmoWheel::RebuildLabelLayouts(ImVec2 a_wheelCenter, float a_startAngle, float a_slotAngle)
{
	if (!Config::AmmoWheel::LabelShow || _ammoEntries.empty()) {
		return false;
	}

	const ImVec2 viewport = ResolutionScale::Context::GetSingleton().GetRenderSize();
	auto& reskinSystem = AmmoWheelReskinUnified::ReskinSystem::GetSingleton();
	const float margin = Config::AmmoWheel::NameMarginPx;
	const float padding = Config::AmmoWheel::NamePanelPaddingPx;

	bool needsRebuild = false;
	for (const auto& entry : _ammoEntries) {
		const bool useReskinPath = reskinSystem.IsEnabled() && entry.reskinEntry.preset;
		if (!entry.labelLayoutValid ||
			entry.labelLayoutRevision != _configRevision ||
			entry.labelLayoutViewport.x != viewport.x ||
			entry.labelLayoutViewport.y != viewport.y ||
			std::abs(entry.labelLayoutWheelCenterX - a_wheelCenter.x) > 0.5f ||
			entry.labelUsesReskinPathCached != useReskinPath) {
			needsRebuild = true;
			break;
		}
	}

	if (!needsRebuild) {
		return false;
	}

	ImFont* font = ImGui::GetFont();
	for (int i = 0; i < static_cast<int>(_ammoEntries.size()); ++i) {
		auto& entry = _ammoEntries[i];

		entry.labelDisplayNameCached.clear();
		entry.labelLayoutCached = TextLayout{};
		entry.labelFontSizeCached = 0.0f;
		entry.labelAvailWidthCached = 0.0f;
		entry.labelLineSpacingCached = 0.0f;
		entry.labelTotalTextHeightCached = 0.0f;
		entry.labelClipHalfWidthCached = 0.0f;
		entry.labelClipHalfHeightCached = 0.0f;
		entry.labelBgHalfWidthCached = 0.0f;
		entry.labelUsesReskinPathCached = false;
		entry.labelLayoutValid = false;
		entry.labelLayoutRevision = _configRevision;
		entry.labelLayoutViewport = viewport;
		entry.labelLayoutWheelCenterX = a_wheelCenter.x;

		if (!entry.ammo) {
			continue;
		}

		const bool useReskinPath = reskinSystem.IsEnabled() && entry.reskinEntry.preset;
		entry.labelUsesReskinPathCached = useReskinPath;

		const float slotStartAngle = a_startAngle + i * a_slotAngle;
		const float slotEndAngle = slotStartAngle + a_slotAngle;
		const float midAngle = (slotStartAngle + slotEndAngle) * 0.5f;

		const char* originalName = entry.ammo->GetName();
		std::string displayName = originalName ? originalName : "";
		if (useReskinPath && Config::AmmoWheel::LabelTruncateLength > 0 &&
			displayName.length() > static_cast<size_t>(Config::AmmoWheel::LabelTruncateLength)) {
			if (Config::AmmoWheel::LabelAbbreviate && Config::AmmoWheel::LabelTruncateLength > 3) {
				displayName = displayName.substr(0, Config::AmmoWheel::LabelTruncateLength - 3) + "...";
			} else {
				displayName = displayName.substr(0, Config::AmmoWheel::LabelTruncateLength);
			}
		}

		float textSize = Config::AmmoWheel::NameFontPx * Config::AmmoWheel::NameTextScale;
		float availWidth = 0.0f;
		if (Config::AmmoWheel::NameLayoutMode > 0) {
			const bool labelOnLeft = (a_wheelCenter.x > viewport.x * 0.5f);
			if (useReskinPath) {
				availWidth = labelOnLeft ?
					(a_wheelCenter.x - margin - padding) :
					(viewport.x - a_wheelCenter.x - margin - padding);
				availWidth = (std::max)(availWidth, 60.0f);
			} else {
				const float wheelOuterRadius = Config::AmmoWheel::WheelRadius;
				const float wheelLeft = a_wheelCenter.x - wheelOuterRadius;
				const float wheelRight = a_wheelCenter.x + wheelOuterRadius;
				availWidth = labelOnLeft ?
					(wheelLeft - margin - padding) :
					(viewport.x - wheelRight - margin - padding);
				availWidth = (std::max)(availWidth, 120.0f);
			}
			if (Config::AmmoWheel::NameMaxWidthPx > 0.0f) {
				availWidth = (std::min)(availWidth, Config::AmmoWheel::NameMaxWidthPx);
			}
		} else {
			const float slotArcLength = (slotEndAngle - slotStartAngle) * _cachedTextRadius;
			availWidth = slotArcLength * Config::AmmoWheel::LabelMaxSlotArcRatio;
			availWidth = (std::max)(availWidth, 60.0f);
		}

		TextLayout layout;
		const int maxLines = Config::AmmoWheel::NameMaxLines;
		switch (Config::AmmoWheel::NameLayoutMode) {
		case 0:
		{
			int legacyMaxLines = 1;
			if (Config::AmmoWheel::LabelMultiLine) {
				float absU = 0.0f;
				if (useReskinPath) {
					absU = std::abs(std::cos(midAngle));
				} else {
					const float arcSpan = getArcAngleRad();
					const float arcMidAngle = getStartAngleRad() + arcSpan * 0.5f;
					float u = 0.0f;
					if (arcSpan > 0.01f) {
						u = (midAngle - arcMidAngle) / (arcSpan * 0.5f);
						u = std::clamp(u, -1.0f, 1.0f);
					}
					absU = std::abs(u);
				}
				if (absU >= 0.70f) {
					legacyMaxLines = 3;
				} else if (absU >= 0.35f) {
					legacyMaxLines = 2;
				}
			}
			layout = wrapTextForSlot(displayName.c_str(), availWidth, textSize, legacyMaxLines);
			break;
		}
		case 1:
			layout = wrapTextForSlot(displayName.c_str(), availWidth, textSize, maxLines);
			break;
		case 2:
		{
			if (font) {
				ImVec2 size = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, displayName.c_str());
				const float minFont = Config::AmmoWheel::NameMinFontPx;
				while (size.x > availWidth && textSize > minFont) {
					textSize -= 1.0f;
					size = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, displayName.c_str());
				}
				if (!useReskinPath && size.x > availWidth) {
					layout.lines.push_back(TruncateTextToFit(displayName.c_str(), availWidth, textSize));
				} else {
					layout.lines.push_back(displayName);
				}
			} else {
				layout.lines.push_back(displayName);
			}
			break;
		}
		case 3:
		{
			layout = wrapTextForSlot(displayName.c_str(), availWidth, textSize, maxLines);
			if (font && !layout.lines.empty()) {
				ImVec2 lastLineSize = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, layout.lines.back().c_str());
				if (lastLineSize.x > availWidth) {
					const float minFont = Config::AmmoWheel::NameMinFontPx;
					float trySize = textSize;
					while (trySize > minFont) {
						trySize -= 1.0f;
						if (useReskinPath) {
							ImVec2 testSize = font->CalcTextSizeA(trySize, FLT_MAX, 0.0f, layout.lines.back().c_str());
							if (testSize.x <= availWidth) {
								textSize = trySize;
								break;
							}
						} else {
							TextLayout tryLayout = wrapTextForSlot(displayName.c_str(), availWidth, trySize, maxLines);
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
								layout = std::move(tryLayout);
								break;
							}
						}
					}
				}
			}
			break;
		}
		default:
			layout = wrapTextForSlot(displayName.c_str(), availWidth, textSize, maxLines);
			break;
		}

		if (layout.lines.empty()) {
			layout.lines.push_back(displayName);
		}

		const float lineSpacing = textSize + Config::AmmoWheel::NameLineSpacingPx;
		const float totalTextHeight = static_cast<float>(layout.lines.size()) * lineSpacing;
		float clipHalfWidth = 0.0f;
		float clipHalfHeight = 0.0f;
		if (Config::AmmoWheel::NameLayoutMode > 0) {
			clipHalfWidth = availWidth * 0.55f;
			clipHalfHeight = totalTextHeight + padding;
		} else {
			clipHalfWidth = useReskinPath ? (availWidth * 0.5f) : (availWidth * 0.6f);
			clipHalfHeight = useReskinPath ? (totalTextHeight * 0.5f) : (totalTextHeight * 0.8f);
		}

		float bgHalfWidth = clipHalfWidth;
		if (useReskinPath && font && !layout.lines.empty()) {
			float maxLineWidth = 0.0f;
			for (const auto& line : layout.lines) {
				ImVec2 size = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, line.c_str());
				maxLineWidth = (std::max)(maxLineWidth, size.x);
			}
			float bgWidth = maxLineWidth + Config::AmmoWheel::NameTextBgExtraPaddingPx * 2.0f + padding * 2.0f;
			if (Config::AmmoWheel::NameMaxWidthPx > 0.0f) {
				bgWidth = (std::min)(bgWidth, Config::AmmoWheel::NameMaxWidthPx);
			}
			bgWidth = (std::min)(bgWidth, availWidth + padding * 2.0f);
			bgHalfWidth = bgWidth * 0.5f;
		}

		entry.labelDisplayNameCached = displayName;
		entry.labelLayoutCached = std::move(layout);
		entry.labelFontSizeCached = textSize;
		entry.labelAvailWidthCached = availWidth;
		entry.labelLineSpacingCached = lineSpacing;
		entry.labelTotalTextHeightCached = totalTextHeight;
		entry.labelClipHalfWidthCached = clipHalfWidth;
		entry.labelClipHalfHeightCached = clipHalfHeight;
		entry.labelBgHalfWidthCached = bgHalfWidth;
		entry.labelLayoutValid = true;
	}

	return true;
}

bool AmmoWheel::RebuildCenterPanelCache(ImVec2 a_wheelCenter, DrawArgs)
{
	if (!Config::AmmoWheel::CenterEnabled) {
		_centerPanelCache = CenterPanelCache{};
		return false;
	}

	if (_hoveredIndex < 0 || _hoveredIndex >= static_cast<int>(_ammoEntries.size())) {
		_centerPanelCache = CenterPanelCache{};
		return false;
	}

	auto& entry = _ammoEntries[_hoveredIndex];
	if (!entry.ammo) {
		_centerPanelCache = CenterPanelCache{};
		return false;
	}

	const ImVec2 viewport = ResolutionScale::Context::GetSingleton().GetRenderSize();
	const RE::FormID ammoID = entry.ammo->GetFormID();
	if (_centerPanelCache.valid &&
		_centerPanelCache.hoveredIndex == _hoveredIndex &&
		_centerPanelCache.ammoID == ammoID &&
		_centerPanelCache.configRevision == _configRevision &&
		_centerPanelCache.damageRevision == _damageCacheRevision &&
		_centerPanelCache.rangedWeaponPoisonRevision == _rangedWeaponPoisonRevision &&
		_centerPanelCache.viewport.x == viewport.x &&
		_centerPanelCache.viewport.y == viewport.y) {
		return false;
	}

	EnsureCenterFieldOrderCache();

	CenterPanelCache cache{};
	cache.valid = true;
	cache.hoveredIndex = _hoveredIndex;
	cache.ammoID = ammoID;
	cache.configRevision = _configRevision;
	cache.damageRevision = _damageCacheRevision;
	cache.rangedWeaponPoisonRevision = _rangedWeaponPoisonRevision;
	cache.viewport = viewport;
	cache.wheelCenterAtBuild = a_wheelCenter;

	if (Config::AmmoWheel::CenterPanelPositionMode == 1) {
		cache.panelCenter = ImVec2(
			a_wheelCenter.x + Config::AmmoWheel::CenterPanelOffsetX,
			a_wheelCenter.y + Config::AmmoWheel::CenterPanelOffsetY);
	} else {
		cache.panelCenter = calculateCenterPanelPosition(a_wheelCenter);
	}

	cache.nameFontSize = std::clamp(
		Config::AmmoWheel::CenterFontPx * 1.2f,
		Config::AmmoWheel::CenterTextMinFontSize,
		Config::AmmoWheel::CenterTextMaxFontSize);
	cache.infoFontSize = std::clamp(
		Config::AmmoWheel::CenterFontPx,
		Config::AmmoWheel::CenterTextMinFontSize,
		Config::AmmoWheel::CenterTextMaxFontSize);
	cache.descriptionFontSize = std::clamp(
		Config::AmmoWheel::CenterFontPx * Config::AmmoWheel::CenterDescriptionFontScale,
		Config::AmmoWheel::CenterTextMinFontSize,
		Config::AmmoWheel::CenterTextMaxFontSize);
	cache.lineSpacing = Config::AmmoWheel::CenterLineSpacingPx;
	cache.padding = Config::AmmoWheel::CenterPaddingPx;

	const float damage = entry.damageValid ? entry.cachedDamage : entry.ammo->data.damage;
	const float maxDamage = _damageCacheValid ? _cachedMaxDamage : damage;
	cache.highlightDamage = (std::abs(damage - maxDamage) <= kAmmoDamageEpsilon && maxDamage > 0.0f);
	cache.roundedDamageValue = (std::max)(0, static_cast<int>(std::lround(damage)));

	const std::string ammoType = entry.ammo->IsBolt() ? "Bolt" : "Arrow";
	std::string source = "Vanilla";
	if (auto* file = entry.ammo->GetFile(0)) {
		std::string filename(file->GetFilename());
		if (filename != "Skyrim.esm" &&
			filename != "Update.esm" &&
			filename != "Dawnguard.esm" &&
			filename != "HearthFires.esm" &&
			filename != "Dragonborn.esm") {
			source = filename;
		}
	}

	std::string centerDescription;
	if (Config::AmmoWheel::CenterShowDescription) {
		RE::BSString descriptionBuf = "";
		entry.ammo->GetDescription(descriptionBuf, nullptr);
		if (descriptionBuf.c_str()) {
			centerDescription = NormalizeCenterDescriptionText(std::string(descriptionBuf.c_str()));
		}
	}

	bool poisonLineEmitted = false;
	for (const auto& field : _centerFieldOrderCache) {
		if (field == "Name" && Config::AmmoWheel::CenterFields::ShowName) {
			if (Config::AmmoWheel::EnableWordWrap) {
				TextLayout wrappedName = wrapTextForCenterPanel(entry.ammo->GetName(), cache.nameFontSize, cache.panelCenter);
				for (const auto& line : wrappedName.lines) {
					cache.textLines.emplace_back(line, cache.nameFontSize);
					cache.isDamageLine.push_back(false);
				}
			} else {
				cache.textLines.emplace_back(entry.ammo->GetName(), cache.nameFontSize);
				cache.isDamageLine.push_back(false);
			}
		} else if (field == "Damage" && Config::AmmoWheel::CenterFields::ShowDamage) {
			cache.textLines.emplace_back(fmt::format("Damage: {}", cache.roundedDamageValue), cache.infoFontSize);
			cache.isDamageLine.push_back(true);
		} else if (field == "Poison" && Config::AmmoWheel::CenterFields::ShowPoison &&
			_rangedWeaponPoisonPresentation.HasActivePoison()) {
			cache.textLines.emplace_back(fmt::format("{}: {}",
				Texts::GetText(Texts::TextType::AmmoWheelWeaponPoisonLabel),
				_rangedWeaponPoisonPresentation.poisonName), cache.infoFontSize);
			cache.isDamageLine.push_back(false);
			poisonLineEmitted = true;
		} else if (field == "Type" && Config::AmmoWheel::CenterFields::ShowType) {
			cache.textLines.emplace_back(fmt::format("Type: {}", ammoType), cache.infoFontSize);
			cache.isDamageLine.push_back(false);
		} else if (field == "Count" && Config::AmmoWheel::CenterFields::ShowCount) {
			cache.textLines.emplace_back(fmt::format("Count: {}", entry.count), cache.infoFontSize);
			cache.isDamageLine.push_back(false);
		} else if (field == "Source" && Config::AmmoWheel::CenterFields::ShowSource) {
			cache.textLines.emplace_back(fmt::format("Source: {}", source), cache.infoFontSize);
			cache.isDamageLine.push_back(false);
		}
	}

	if (Config::AmmoWheel::CenterShowDescription && !centerDescription.empty()) {
		const int maxDescLines = std::clamp(Config::AmmoWheel::CenterMaxDescriptionLines, 1, 8);
		TextLayout wrappedDescription = wrapTextForCenterPanel(
			centerDescription.c_str(),
			cache.descriptionFontSize,
			cache.panelCenter,
			maxDescLines);
		if (!wrappedDescription.lines.empty()) {
			for (const auto& line : wrappedDescription.lines) {
				cache.descriptionLines.emplace_back(line);
			}
		}
	}

	if (cache.textLines.empty()) {
		if (Config::AmmoWheel::EnableWordWrap) {
			TextLayout wrappedName = wrapTextForCenterPanel(entry.ammo->GetName(), cache.nameFontSize, cache.panelCenter);
			for (const auto& line : wrappedName.lines) {
				cache.textLines.emplace_back(line, cache.nameFontSize);
				cache.isDamageLine.push_back(false);
			}
		} else {
			cache.textLines.emplace_back(entry.ammo->GetName(), cache.nameFontSize);
			cache.isDamageLine.push_back(false);
		}
	}

	cache.totalHeight = cache.padding * 2.0f;
	cache.maxTextWidth = 0.0f;
	ImFont* font = ImGui::GetFont();
	for (size_t i = 0; i < cache.textLines.size(); ++i) {
		cache.totalHeight += cache.textLines[i].second;
		if (i > 0) {
			cache.totalHeight += cache.lineSpacing;
		}

		float width = 0.0f;
		if (font) {
			ImVec2 size = font->CalcTextSizeA(cache.textLines[i].second, FLT_MAX, 0.0f, cache.textLines[i].first.c_str());
			width = size.x;
		} else {
			width = ImGui::CalcTextSize(cache.textLines[i].first.c_str()).x;
		}
		cache.maxTextWidth = (std::max)(cache.maxTextWidth, width);
	}

	const float maxPanelWidth = (std::max)(
		1.0f,
		_cachedInnerRadius * Config::AmmoWheel::CenterMaxWidthRatio * 2.0f);
	const float maxPanelHeight = (std::max)(
		1.0f,
		viewport.y - Config::AmmoWheel::CenterPanelSafeMargin * 2.0f);

	// Keep the center panel frame independent from current text content.
	// Width/height are derived from configuration budgets (not current ammo name length).
	const float textWidthRatio = std::clamp(Config::AmmoWheel::CenterTextMaxWidthRatio, 0.1f, 1.0f);
	const float fixedTextWidth = maxPanelWidth * textWidthRatio;
	const float fixedPanelWidth = std::clamp(fixedTextWidth + cache.padding * 2.0f, 1.0f, maxPanelWidth);

	const int nameLineBudget = Config::AmmoWheel::CenterFields::ShowName ? (std::max)(1, Config::AmmoWheel::CenterTextMaxLines) : 0;
	int infoLineBudget = 0;
	if (Config::AmmoWheel::CenterFields::ShowDamage) { infoLineBudget++; }
	if (poisonLineEmitted) { infoLineBudget++; }
	if (Config::AmmoWheel::CenterFields::ShowType) { infoLineBudget++; }
	if (Config::AmmoWheel::CenterFields::ShowCount) { infoLineBudget++; }
	if (Config::AmmoWheel::CenterFields::ShowSource) { infoLineBudget++; }
	const int totalLineBudget = (std::max)(1, nameLineBudget + infoLineBudget);
	const int interLineGapCount = (std::max)(0, totalLineBudget - 1);

	float budgetContentHeight = 0.0f;
	budgetContentHeight += static_cast<float>(nameLineBudget) * cache.nameFontSize;
	budgetContentHeight += static_cast<float>(infoLineBudget) * cache.infoFontSize;
	budgetContentHeight += static_cast<float>(interLineGapCount) * cache.lineSpacing;
	const float fixedPanelHeight = std::clamp(cache.padding * 2.0f + budgetContentHeight, 1.0f, maxPanelHeight);

	_centerPanelStableWidth = fixedPanelWidth;
	_centerPanelStableHeight = fixedPanelHeight;
	_centerPanelStableConfigRevision = _configRevision;
	_centerPanelStableViewport = viewport;
	cache.panelWidth = fixedPanelWidth;
	cache.panelHeight = fixedPanelHeight;

	if (Config::AmmoWheel::CenterPanelClampToScreen) {
		float margin = Config::AmmoWheel::CenterPanelSafeMargin;
		cache.panelCenter.x = std::clamp(
			cache.panelCenter.x,
			margin + cache.panelWidth * 0.5f,
			viewport.x - margin - cache.panelWidth * 0.5f);
		cache.panelCenter.y = std::clamp(
			cache.panelCenter.y,
			margin + cache.panelHeight * 0.5f,
			viewport.y - margin - cache.panelHeight * 0.5f);
	}

	_centerPanelCache = std::move(cache);
	return true;
}

void AmmoWheel::RefreshAmmoList()
{
	std::unique_lock lock(_lock);
	_ammoEntries.clear();
	InvalidateRuntimeCaches();

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

	// ========== MULTI-CRITERIA SORTING ==========
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

	// ========== APPLY AMMO LIMITS (POST-SORT TRUNCATION) ==========
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
		_pendingCloseOnReleaseButton = -1;
		
		// Reset navigation filters on open to prevent a locked state.
		if (Config::AmmoWheel::ResetFiltersOnOpen) {
			ResetNavigationFilters();
		}
		
		// Reset hover sound state on open (prevents sound on first frame)
		_lastHoverSoundIndex = -1;
		_lastHoverSoundTimeMs = 0;
		
		// Start hover on equipped ammo or last selected
		_hoveredIndex = FindInitialHoverIndex();
		_prevHoveredIndex = _hoveredIndex;  // Initialize hysteresis to match initial selection
		_mousePendingHoverIndex = -1;
		_mouseAccumulatedDelta = { 0.0f, 0.0f };
		_mouseAccumulatedPeak = 0.0f;
		_mouseSlotCarry = 0.0f;
		_mouseStepLatchDirection = 0;
		// Lock hover until user provides meaningful input (mouse/gamepad movement)
		_hoverInputLock = true;
		
		// Initialize cursor position to point at the initial hover slot
		// This prevents the cursor from starting at center and causing selection jitter
		if (_hoveredIndex >= 0 && !_ammoEntries.empty()) {
			float initRadius = Config::AmmoWheel::WheelRadius * 0.8f;
			_cursorPos.x = initRadius * std::cos(getSlotCenterAngle(_hoveredIndex));
			_cursorPos.y = initRadius * std::sin(getSlotCenterAngle(_hoveredIndex));
		} else {
			_cursorPos = { 0, 0 };
		}
		_mouseFilter.Reset();
		syncGamepadFilterToCursor();
		_lastCursorInputSource = CursorInputSource::None;
		
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

		if (Config::AmmoWheel::TimeSlowEnabled) {
			const float slowScale = Config::AmmoWheel::TimeSlowScale;
			ResetMountedVelocityRestoreState();
			if (slowScale <= 0.0f) {
				_ammoWheelModifiedTimeScale = false;
				OpenAmmoWheelPauseMenu();
				_ammoWheelOwnedPauseMenu = true;
				logger::info("[TimeDilation] AmmoWheel pause-mode: SlowTimeScale=0 -> vanilla pause (kPausesGame)");
			} else if (slowScale < 1.0f) {
				const bool mountedAtOpen = Utils::Player::IsMounted();
				const float minSlowScale = mountedAtOpen ? kAmmoWheelMountedMinimumSlowScale : 0.01f;
				const float effectiveScale = (std::max)(slowScale, minSlowScale);
				if (mountedAtOpen && slowScale < kAmmoWheelMountedMinimumSlowScale) {
					logger::info("[TimeDilation] AmmoWheel mounted slow clamp: requested={:.3f}, clamped={:.3f}",
						slowScale, effectiveScale);
				}
				float currentTimeScale = Utils::Time::GGTM();
				// Only modify timescale if it's currently at normal (1.0) - don't override Slow Time shout or other effects
				if (currentTimeScale >= 0.99f && currentTimeScale <= 1.01f) {
					_preAmmoWheelTimeScale = currentTimeScale;
					_ammoWheelModifiedTimeScale = true;
					_ammoWheelOwnedPauseMenu = false;
					if (Utils::Player::TryCaptureMountedVelocity(_ammoWheelMountedVelocitySnapshot, &_ammoWheelMountedVelocityMountFormID)) {
						_ammoWheelRestoreMountedVelocityOnClose = true;
						logger::info("[TimeDilation] AmmoWheel mounted velocity snapshot: mount={:08X}, v=({:.2f},{:.2f},{:.2f})",
							_ammoWheelMountedVelocityMountFormID,
							_ammoWheelMountedVelocitySnapshot.x,
							_ammoWheelMountedVelocitySnapshot.y,
							_ammoWheelMountedVelocitySnapshot.z);
					}
					Utils::Time::SGTM(effectiveScale);
					logger::info("[TimeDilation] AmmoWheel apply: before={:.3f}, after={:.3f}, cached={:.3f}",
						currentTimeScale, effectiveScale, _preAmmoWheelTimeScale);
					if (_ammoWheelRestoreMountedVelocityOnClose) {
						Utils::Player::TryRestoreMountedVelocity(
							_ammoWheelMountedVelocityMountFormID,
							_ammoWheelMountedVelocitySnapshot,
							true);
					}
				} else {
					// External time effect active (e.g., Slow Time shout) - don't touch timescale
					_ammoWheelModifiedTimeScale = false;
					_ammoWheelOwnedPauseMenu = false;
					logger::info("[TimeDilation] AmmoWheel skip: external effect active (current={:.3f}, requested={:.3f}, effective={:.3f})",
						currentTimeScale, slowScale, effectiveScale);
				}
			} else {
				_ammoWheelModifiedTimeScale = false;
				_ammoWheelOwnedPauseMenu = false;
			}
		}

		if (Config::ResolutionFix::LogOncePerOpen) {
			auto& resolutionContext = ResolutionScale::Context::GetSingleton();
			resolutionContext.Update();
			const auto& state = resolutionContext.GetState();
			const char* mapping = state.active ? "Config::OffsetAmmoWheelSizingToViewport" : "None";
			logger::debug("[ResolutionFix] AmmoWheel open: display {}x{}, game {}x{}, scaleX={:.3f}, scaleY={:.3f}, uniform={:.3f}, mode={}, mapping={}",
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

		// Restore pause/timescale only if AmmoWheel owns those changes.
		if (_ammoWheelOwnedPauseMenu) {
			logger::info("[TimeDilation] AmmoWheel restore: closing owned pause menu");
			CloseAmmoWheelPauseMenu();
			_ammoWheelOwnedPauseMenu = false;
		}

		if (_ammoWheelModifiedTimeScale) {
			Utils::Time::SGTM(_preAmmoWheelTimeScale);
			_ammoWheelModifiedTimeScale = false;
			TryRestoreMountedVelocityAfterTimeRestore("Close");
		}

		logger::info("AmmoWheel: CLOSE (was state={}, hovered={})", static_cast<int>(_state), _hoveredIndex);
		_state = WheelState::Closing;
		_closeTimer = 0.f;
		_blockMainWheel = false;  // Release main wheel block on close
		InputBroker::ClearActiveOwner(InputBroker::kWheelerRefinedPluginId);
	}
}

void AmmoWheel::HardClose()
{
	ForceClose();
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

	// Apply the unified deadzone and smoothing filter for mouse input
	float dt = ImGui::GetIO().DeltaTime;
	
	// Configure filter from config
	_mouseFilter.deadzone = Config::AmmoWheel::MouseDeadzone;
	_mouseFilter.smoothingSpeed = Config::AmmoWheel::MouseSmoothingSpeed;
	if (_lastCursorInputSource != CursorInputSource::Mouse) {
		_mouseFilter.Reset();
		_mousePendingHoverIndex = -1;
		_mouseAccumulatedDelta = { 0.0f, 0.0f };
		_mouseAccumulatedPeak = 0.0f;
		_mouseSlotCarry = 0.0f;
		_mouseStepLatchDirection = 0;
		_lastCursorInputSource = CursorInputSource::Mouse;
		syncCursorToHoveredSlot(0.0f, true);
	}
	
	// Mouse move events can arrive many times per frame. Accumulate raw delta here and
	// consume it once per frame in processPendingMouseMotion() so smoothing is frame-based
	// instead of event-frequency-based.
	(void)dt;
	const ImVec2 rawDelta = { a_deltaX, a_deltaY };
	_mouseAccumulatedDelta.x += rawDelta.x;
	_mouseAccumulatedDelta.y += rawDelta.y;
	const float rawMagnitude = std::sqrt(rawDelta.x * rawDelta.x + rawDelta.y * rawDelta.y);
	_mouseAccumulatedPeak = (std::max)(_mouseAccumulatedPeak, rawMagnitude);
}

void AmmoWheel::UpdateCursorPosGamepad(float a_x, float a_y)
{
	if (!IsOpen()) {
		return;
	}

	// Apply the unified deadzone and smoothing filter for gamepad input
	float dt = ImGui::GetIO().DeltaTime;
	
	// Configure filter from config
	_gamepadFilter.deadzone = Config::AmmoWheel::GamepadDeadzone;
	_gamepadFilter.smoothingSpeed = Config::AmmoWheel::GamepadSmoothingSpeed;
	if (_lastCursorInputSource != CursorInputSource::Gamepad) {
		syncGamepadFilterToCursor();
		_mousePendingHoverIndex = -1;
		_mouseAccumulatedDelta = { 0.0f, 0.0f };
		_mouseAccumulatedPeak = 0.0f;
		_mouseSlotCarry = 0.0f;
		_mouseStepLatchDirection = 0;
		_lastCursorInputSource = CursorInputSource::Gamepad;
	}
	
	// Gamepad stick values are already normalized to [-1, 1]
	ImVec2 stickInput = {a_x, a_y};
	float rawMagnitude = std::sqrt(stickInput.x * stickInput.x + stickInput.y * stickInput.y);
	
	// Apply filter (deadzone + smoothing)
	ImVec2 filtered = _gamepadFilter.Apply(stickInput, dt, true);
	
	// If input is meaningful (above deadzone), unlock hover for cursor-based selection
	if (rawMagnitude > _gamepadFilter.deadzone) {
		_hoverInputLock = false;
	}
	
	// If hover is locked, don't update cursor position
	if (_hoverInputLock) {
		return;
	}
	
	// Convert filtered stick position to cursor position
	float maxRadius = getCursorMaxRadius();
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
	InventorySnapshotCache::EquipObject(equipManager, player, entry.ammo);
	
	// Remember this selection for next time
	_lastSelectedIndex = _hoveredIndex;
	_lastSelectedAmmoID = targetFormID;
	SyncRememberedAmmoForWeaponType(_currentWeaponType, targetFormID, "ActivateHoveredAmmo");
	
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
	ImVec2 layoutCenter = wheelCenter;
	
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

	InventorySnapshotCache::Stats invStats{};
	RE::TESObjectREFR::InventoryItemMap imap;
	g_ammoWheelInventorySnapshot.CaptureFresh(
		player,
		true,
		Config::AmmoWheel::Performance::InventorySnapshotIntervalSeconds > 0.0f ?
			Config::AmmoWheel::Performance::InventorySnapshotIntervalSeconds :
			static_cast<float>(kAmmoWheelDerivedInventoryRefreshIntervalSeconds),
		imap,
		&invStats);
	const bool inventoryCaptured = invStats.capturedFreshThisCall;
	UpdateRangedWeaponPoisonPresentation(
		player, imap, inventoryCaptured, invStats.refreshDerivedDataThisCall);
	
	// Get currently equipped ammo FormID for selection highlighting (derived each frame)
	RE::FormID equippedAmmoID = getEquippedAmmoFormID();

	// Use cached radii (recomputed on config change)
	float innerRadius = _cachedInnerRadius;
	float outerRadius = _cachedOuterRadius;

	int numEntries = static_cast<int>(_ammoEntries.size());
	float arcAngle = getArcAngleRad();
	float startAngle = getStartAngleRad();
	float slotAngle = arcAngle / static_cast<float>(numEntries);

	if (!_damageCacheValid || invStats.refreshDerivedDataThisCall) {
		const auto damageStart = std::chrono::steady_clock::now();
		if (RebuildDamageCache(imap)) {
			const auto damageEnd = std::chrono::steady_clock::now();
			_damageRebuildMsAccum += std::chrono::duration<double, std::milli>(damageEnd - damageStart).count();
			_damageRebuildCount++;
		}
	}

	const auto labelStart = std::chrono::steady_clock::now();
	if (RebuildLabelLayouts(layoutCenter, startAngle, slotAngle)) {
		const auto labelEnd = std::chrono::steady_clock::now();
		_labelRebuildMsAccum += std::chrono::duration<double, std::milli>(labelEnd - labelStart).count();
		_labelRebuildCount++;
	}

	// Update hovered index based on cursor
	float cursorAngle = getCursorAngle();
	int prevHovered = _hoveredIndex;
	
	// Skip hover recalculation while input lock is active (preserves initial selection)
	if (_lastCursorInputSource == CursorInputSource::Mouse) {
		processPendingMouseMotion(ImGui::GetIO().DeltaTime);
		if (!_hoverInputLock && _mousePendingHoverIndex >= 0 && _mousePendingHoverIndex < numEntries) {
			_hoveredIndex = _mousePendingHoverIndex;
		}
		_mousePendingHoverIndex = -1;
	} else if (!_hoverInputLock) {
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
			if (Config::AmmoWheel::SmoothSlotTransition && _lastCursorInputSource == CursorInputSource::Mouse) {
				// Soft-restart the popup on mouse slot changes so the hover replay effect stays
				// visible while still replaying the popup animation from a low scale.
				_hoverPopupScale = (std::min)(_hoverPopupScale, 0.15f);
			} else {
				_hoverPopupScale = 0.0f;
			}
		}
		
		// Play hover sound when slot changes
		if (_hoveredIndex >= 0 && _hoveredIndex != prevHovered) {
			PlayHoverSlotSound(_hoveredIndex);
		}
	}
	
	// Update _prevHoveredIndex for next frame's hysteresis calculation
	_prevHoveredIndex = _hoveredIndex;

	const auto centerStart = std::chrono::steady_clock::now();
	if (RebuildCenterPanelCache(layoutCenter, a_drawArgs)) {
		const auto centerEnd = std::chrono::steady_clock::now();
		_centerRebuildMsAccum += std::chrono::duration<double, std::milli>(centerEnd - centerStart).count();
		_centerRebuildCount++;
	}

	auto& reskinSystem = AmmoWheelReskinUnified::ReskinSystem::GetSingleton();
	auto drawSlotDividers = [&]() {
		// ========== ANIMATION: SLOT DIVIDERS ==========
		// Only draw dividers for arc-shaped slots (radial lines don't make sense for floating shapes)
		if (Config::AmmoWheel::SlotDividersEnabled && Config::AmmoWheel::SlotShape == 0 && numEntries > 1) {
			ImU32 dividerColor = Config::AmmoWheel::SlotDividerColor;
			uint8_t divAlpha = static_cast<uint8_t>((dividerColor >> 24) * a_drawArgs.alphaMult);
			dividerColor = (dividerColor & 0x00FFFFFF) | (divAlpha << 24);
			float thickness = Config::AmmoWheel::SlotDividerThickness;
			const bool hasDividerReskin = reskinSystem.IsEnabled() && !_ammoEntries.empty() && _ammoEntries[0].reskinEntry.preset;
			float dividerReskinAlphaMult = a_drawArgs.alphaMult;
			if (hasDividerReskin && Config::AmmoWheel::SlotDividerReskinBreathingEnabled) {
				const float speed = std::clamp(Config::AmmoWheel::SlotDividerReskinBreathingSpeed, 0.1f, 12.0f);
				const float intensity = std::clamp(Config::AmmoWheel::SlotDividerReskinBreathingIntensity, 0.0f, 1.0f);
				const float baseOpacity = std::clamp(Config::AmmoWheel::SlotDividerReskinBreathingOpacity, 0.0f, 1.0f);
				const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * speed * 2.0f * IM_PI);
				const float breathAlpha = (1.0f - intensity) + (intensity * pulse);
				dividerReskinAlphaMult *= baseOpacity * breathAlpha;
			}
			auto* drawList = ImGui::GetWindowDrawList();
			const auto dividerLayout = reskinSystem.GetLayoutOverrideSnapshot(
				AmmoWheelReskinUnified::VisualTarget::SlotDivider);
			const float dividerLayoutScale = (std::isfinite(dividerLayout.scale) && dividerLayout.scale > 0.0f)
				? dividerLayout.scale
				: 1.0f;
			const bool applyDividerLayoutToPrimitiveFallback =
				reskinSystem.IsEnabled() &&
				(std::fabs(dividerLayoutScale - 1.0f) > 0.001f ||
				 std::fabs(dividerLayout.offsetX) > 0.001f ||
				 std::fabs(dividerLayout.offsetY) > 0.001f);
			auto transformDividerPoint = [&](const ImVec2& pt) -> ImVec2 {
				if (!applyDividerLayoutToPrimitiveFallback) {
					return pt;
				}
				const float dx = pt.x - wheelCenter.x;
				const float dy = pt.y - wheelCenter.y;
				return ImVec2(
					wheelCenter.x + dx * dividerLayoutScale + dividerLayout.offsetX,
					wheelCenter.y + dy * dividerLayoutScale + dividerLayout.offsetY);
			};

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

				bool drewDivider = false;
				if (hasDividerReskin) {
					AmmoWheelReskinUnified::DrawContext dividerCtx{};
					dividerCtx.center = wheelCenter;
					dividerCtx.radius = outerRadius * 1.05f;
					dividerCtx.slotAngleRad = dividerAngle;
					dividerCtx.alphaMult = dividerReskinAlphaMult;
					dividerCtx.slotIndex = i;
					dividerCtx.formID = 0;
					drewDivider = reskinSystem.DrawTarget(
						AmmoWheelReskinUnified::VisualTarget::SlotDivider,
						_ammoEntries[0].reskinEntry,
						dividerCtx,
						drawList);
				}

				if (!drewDivider) {
					drawList->AddLine(
						transformDividerPoint(innerPt),
						transformDividerPoint(outerPt),
						dividerColor,
						thickness);
				}
			}
		}
	};

	auto drawSoftArcLayer = [&](float radius, ImU32 innerColor, float softEdgeRatio) {
		radius = (std::max)(0.0f, radius);
		if (radius <= 0.5f) {
			return;
		}

		softEdgeRatio = std::clamp(softEdgeRatio, 0.0f, 0.95f);
		const float solidRadius = radius * (1.0f - softEdgeRatio);

		if (softEdgeRatio <= 0.001f || solidRadius >= radius - 0.5f) {
			Drawer::draw_arc_gradient(
				wheelCenter, 0.0f, radius,
				startAngle, startAngle + arcAngle,
				startAngle, startAngle + arcAngle,
				innerColor, innerColor,
				64, a_drawArgs);
			return;
		}

		if (solidRadius > 0.5f) {
			Drawer::draw_arc_gradient(
				wheelCenter, 0.0f, solidRadius,
				startAngle, startAngle + arcAngle,
				startAngle, startAngle + arcAngle,
				innerColor, innerColor,
				64, a_drawArgs);
		}

		ImVec4 transparentOuter = ImGui::ColorConvertU32ToFloat4(innerColor);
		transparentOuter.w = 0.0f;
		ImU32 outerColor = ImGui::ColorConvertFloat4ToU32(transparentOuter);
		Drawer::draw_arc_gradient(
			wheelCenter, (std::max)(solidRadius, 0.0f), radius,
			startAngle, startAngle + arcAngle,
			startAngle, startAngle + arcAngle,
			innerColor, outerColor,
			64, a_drawArgs);
	};

	// ========== UNIFIED RESKIN: WHEEL BACKDROP (back-most layer) ==========
	if (reskinSystem.IsEnabled() && !_ammoEntries.empty() && _ammoEntries[0].reskinEntry.preset) {
		auto drawList = ImGui::GetWindowDrawList();
		AmmoWheelReskinUnified::DrawContext backdropCtx{};
		backdropCtx.center = wheelCenter;
		backdropCtx.radius = outerRadius;
		backdropCtx.alphaMult = a_drawArgs.alphaMult;
		backdropCtx.slotAngleRad = 0.0f;  // No rotation for backdrop target
		backdropCtx.slotIndex = 0;
		backdropCtx.formID = 0;
		reskinSystem.DrawTarget(
			AmmoWheelReskinUnified::VisualTarget::WheelBackdrop,
			_ammoEntries[0].reskinEntry,
			backdropCtx,
			drawList);
	}

	// When reskin is active, draw divider assets under wheel layers (background and border).
	const bool drawDividersAsUnderlay =
		reskinSystem.IsEnabled() && !_ammoEntries.empty() && _ammoEntries[0].reskinEntry.preset;
	if (drawDividersAsUnderlay) {
		drawSlotDividers();
	}

	// ========== UNIFIED RESKIN: WHEEL BACKGROUND ==========
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
			drawSoftArcLayer(bgRadius, bgColor, Config::AmmoWheel::BackgroundSoftEdgeRatio);
		} else if (Config::AmmoWheel::UseSkyrimTheme) {
			// Skyrim theme: layered arc backgrounds (respects arcAngle)
			// Apply BackgroundOpacity as multiplier to theme alpha values
			
			// Outer glow layer
			ImU32 glowColor = Config::AmmoWheel::SkyrimTheme::BgOuterGlow;
			uint8_t baseGlowA = static_cast<uint8_t>(glowColor >> 24);
			uint8_t glowA = static_cast<uint8_t>(baseGlowA * userOpacityMult * a_drawArgs.alphaMult);
			glowColor = (glowColor & 0x00FFFFFF) | (glowA << 24);
			drawSoftArcLayer(bgRadius * 1.05f, glowColor, Config::AmmoWheel::BackgroundSoftEdgeRatio);
			
			// Mid layer
			ImU32 midColor = Config::AmmoWheel::SkyrimTheme::BgMidLayer;
			uint8_t baseMidA = static_cast<uint8_t>(midColor >> 24);
			uint8_t midA = static_cast<uint8_t>(baseMidA * userOpacityMult * a_drawArgs.alphaMult);
			midColor = (midColor & 0x00FFFFFF) | (midA << 24);
			drawSoftArcLayer(bgRadius, midColor, Config::AmmoWheel::BackgroundSoftEdgeRatio);
			
			// Dark inner layer
			ImU32 darkColor = Config::AmmoWheel::SkyrimTheme::BgDarkLayer;
			uint8_t baseDarkA = static_cast<uint8_t>(darkColor >> 24);
			uint8_t darkA = static_cast<uint8_t>(baseDarkA * userOpacityMult * a_drawArgs.alphaMult);
			darkColor = (darkColor & 0x00FFFFFF) | (darkA << 24);
			drawSoftArcLayer(bgRadius * 0.95f, darkColor, Config::AmmoWheel::BackgroundSoftEdgeRatio);
		} else {
			// Default: simple arc background (respects arcAngle)
			uint8_t bgAlpha = static_cast<uint8_t>(userOpacityMult * 255.f * a_drawArgs.alphaMult);
			ImU32 bgColor = IM_COL32(0, 0, 0, bgAlpha);
			drawSoftArcLayer(bgRadius, bgColor, Config::AmmoWheel::BackgroundSoftEdgeRatio);
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

		bool drewBorderRing = false;
		if (reskinSystem.IsEnabled() && !_ammoEntries.empty() && _ammoEntries[0].reskinEntry.preset && borderOuter > 0.0f) {
			auto drawList = ImGui::GetWindowDrawList();
			AmmoWheelReskinUnified::DrawContext borderCtx{};
			borderCtx.center = wheelCenter;
			borderCtx.radius = borderOuter;
			borderCtx.slotAngleRad = 0.0f;
			borderCtx.alphaMult = a_drawArgs.alphaMult;
			borderCtx.slotIndex = 0;
			borderCtx.formID = 0;
			drewBorderRing = reskinSystem.DrawTarget(
				AmmoWheelReskinUnified::VisualTarget::WheelBorderRing,
				_ammoEntries[0].reskinEntry,
				borderCtx,
				drawList);
		}
		
		// Validate radii are positive and have meaningful thickness
		if (!drewBorderRing) {
			if (borderInner > 0.0f && borderOuter > borderInner && (borderOuter - borderInner) >= 1.0f) {
				ImU32 borderInnerColor, borderOuterColor;
				// Use the border color override when enabled
				if (Config::AmmoWheel::BorderColorOverrideEnabled) {
					borderInnerColor = Config::AmmoWheel::BorderColorComputed;
					// Outer color is slightly darker version of inner
					uint8_t r = (Config::AmmoWheel::BorderColorComputed >> IM_COL32_R_SHIFT) & 0xFF;
					uint8_t g = (Config::AmmoWheel::BorderColorComputed >> IM_COL32_G_SHIFT) & 0xFF;
					uint8_t b = (Config::AmmoWheel::BorderColorComputed >> IM_COL32_B_SHIFT) & 0xFF;
					uint8_t a = (Config::AmmoWheel::BorderColorComputed >> IM_COL32_A_SHIFT) & 0xFF;
					borderOuterColor = IM_COL32(r * 3 / 4, g * 3 / 4, b * 3 / 4, a * 3 / 4);
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
	}

	// Draw center background before slot layers when reskin is active.
	// This keeps SlotBackground visually above CenterBackground.
	bool centerBgDrawnUnderSlots = false;
	if (reskinSystem.IsEnabled() &&
		Config::AmmoWheel::CenterEnabled &&
		Config::AmmoWheel::CenterBgEnabled &&
		_centerPanelCache.valid &&
		_hoveredIndex >= 0 &&
		_hoveredIndex < numEntries &&
		_hoveredIndex < static_cast<int>(_ammoEntries.size())) {
		const auto& hoveredEntry = _ammoEntries[_hoveredIndex];
		if (hoveredEntry.reskinEntry.preset) {
			const auto& cache = _centerPanelCache;
			ImVec2 panelCenter = cache.panelCenter;
			panelCenter.x += wheelCenter.x - cache.wheelCenterAtBuild.x;
			panelCenter.y += wheelCenter.y - cache.wheelCenterAtBuild.y;
			const float panelWidth = cache.panelWidth;
			const float panelHeight = cache.panelHeight;

			AmmoWheelReskinUnified::DrawContext centerCtx{};
			centerCtx.center = panelCenter;
			centerCtx.radius = 0.5f * (std::max)(panelWidth, panelHeight);
			centerCtx.slotAngleRad = 0.0f;
			centerCtx.alphaMult = a_drawArgs.alphaMult;
			centerCtx.slotIndex = _hoveredIndex;
			centerCtx.formID = hoveredEntry.ammo ? hoveredEntry.ammo->GetFormID() : 0;
			centerCtx.hovered = true;
			centerCtx.selected = false;
			centerCtx.active = false;
			centerCtx.progress = 0.0f;
			centerBgDrawnUnderSlots = reskinSystem.DrawTarget(
				AmmoWheelReskinUnified::VisualTarget::CenterBackground,
				hoveredEntry.reskinEntry,
				centerCtx,
				ImGui::GetWindowDrawList());
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

	// Keep legacy draw order for primitive divider lines.
	if (!drawDividersAsUnderlay) {
		drawSlotDividers();
	}

	// Draw highlight for hovered item at center
	if (_hoveredIndex >= 0 && _hoveredIndex < numEntries) {
		drawHighlight(wheelCenter, a_drawArgs, centerBgDrawnUnderSlots);
	}

	// Draw cursor indicator (optional)
	if (Config::AmmoWheel::ShowCursorIndicator && _hoveredIndex >= 0) {
		float cursorDist = _cachedTextRadius;
		ImVec2 cursorTip = ImVec2(
			wheelCenter.x + cursorDist * std::cos(cursorAngle),
			wheelCenter.y + cursorDist * std::sin(cursorAngle)
		);
		bool drewCursor = false;
		if (_hoveredIndex < static_cast<int>(_ammoEntries.size())) {
			const auto& hoveredEntry = _ammoEntries[_hoveredIndex];
			if (reskinSystem.IsEnabled() && hoveredEntry.reskinEntry.preset) {
				AmmoWheelReskinUnified::DrawContext cursorCtx{};
				cursorCtx.center = cursorTip;
				cursorCtx.radius = 5.0f;
				cursorCtx.slotAngleRad = cursorAngle;
				cursorCtx.alphaMult = a_drawArgs.alphaMult;
				cursorCtx.slotIndex = _hoveredIndex;
				cursorCtx.formID = hoveredEntry.ammo ? hoveredEntry.ammo->GetFormID() : 0;
				cursorCtx.hovered = true;
				drewCursor = reskinSystem.DrawTarget(
					AmmoWheelReskinUnified::VisualTarget::CursorIndicator,
					hoveredEntry.reskinEntry,
					cursorCtx,
					ImGui::GetWindowDrawList());
			}
		}
		if (!drewCursor) {
			ImU32 cursorColor = IM_COL32(255, 255, 255, static_cast<int>(200 * a_drawArgs.alphaMult));
			ImGui::GetWindowDrawList()->AddCircleFilled(cursorTip, 5.0f, cursorColor);
		}
	}

	// Draw hover magnify popup (shows full name and large icon outside wheel)
	drawHoverPopup(wheelCenter, a_drawArgs);

	if (invStats.shouldLog) {
		if (Config::AmmoWheel::Debug::LogPerf) {
			const auto reskinPerf = reskinSystem.ConsumePerfStats();
			logger::info(
				"[Perf][AmmoWheel] invCaptureMs={:.2f} invCaptureCount={} damageRebuildMs={:.2f} damageRebuildCount={} labelRebuildMs={:.2f} labelRebuildCount={} centerRebuildMs={:.2f} centerRebuildCount={} reskinDrawTargetMs={:.2f} reskinDrawTargetCalls={} reskinGetTextureMs={:.2f} reskinGetTextureCalls={} texCacheHit={} texCacheMiss={} derivedInterval={:.2f} mutationGeneration={}",
				invStats.lastCaptureMs,
				invStats.captureCountThisWindow,
				_damageRebuildMsAccum,
				_damageRebuildCount,
				_labelRebuildMsAccum,
				_labelRebuildCount,
				_centerRebuildMsAccum,
				_centerRebuildCount,
				reskinPerf.drawTargetMs,
				reskinPerf.drawTargetCalls,
				reskinPerf.getTextureMs,
				reskinPerf.getTextureCalls,
				reskinPerf.textureCacheHits,
				reskinPerf.textureCacheMisses,
				invStats.derivedRefreshIntervalSeconds,
				invStats.mutationGeneration);
		} else {
			// Keep counters bounded even when perf logging is disabled.
			(void)reskinSystem.ConsumePerfStats();
		}
		_damageRebuildMsAccum = 0.0;
		_damageRebuildCount = 0;
		_labelRebuildMsAccum = 0.0;
		_labelRebuildCount = 0;
		_centerRebuildMsAccum = 0.0;
		_centerRebuildCount = 0;
	}

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
	auto brightenColor = [](ImU32 color, float strength) -> ImU32 {
		ImVec4 f = ImGui::ColorConvertU32ToFloat4(color);
		f.x = (std::min)(f.x * strength, 1.0f);
		f.y = (std::min)(f.y * strength, 1.0f);
		f.z = (std::min)(f.z * strength, 1.0f);
		return ImGui::ColorConvertFloat4ToU32(f);
	};

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
			bool drewLowAmmo = false;
			auto& lowAmmoReskin = AmmoWheelReskinUnified::ReskinSystem::GetSingleton();
			if (lowAmmoReskin.IsEnabled() && entry.reskinEntry.preset) {
				const float pulse = static_cast<float>(std::sin(ImGui::GetTime() * 5.0) * 0.5 + 0.5);
				const bool lowAmmoAssetConfigured = lowAmmoReskin.HasAssetForTarget(
					AmmoWheelReskinUnified::VisualTarget::LowAmmoIndicator,
					entry.reskinEntry);

				AmmoWheelReskinUnified::DrawContext lowAmmoCtx{};
				if (lowAmmoAssetConfigured) {
					// Reskin mode: keep low-ammo overlay on the same slot-frame axis/size
					// so it behaves like other slot-cover indicator assets.
					lowAmmoCtx.center = slotCenter;
					lowAmmoCtx.radius = (a_outerRadius - a_innerRadius) * 0.5f;
				} else {
					// Fallback positioning for legacy tiny marker behavior.
					const float ringThickness = a_outerRadius - a_innerRadius;
					const float radiusRatio = std::clamp(Config::AmmoWheel::LowAmmoIndicatorRadiusRatio, 0.0f, 1.0f);
					const float baseRadius = a_innerRadius + ringThickness * radiusRatio;
					const float radius = baseRadius + Config::AmmoWheel::LowAmmoIndicatorRadialOffsetPx;
					const float angle = midAngle + (Config::AmmoWheel::LowAmmoIndicatorAngularOffsetDeg * (IM_PI / 180.0f));
					lowAmmoCtx.center = ImVec2(
						a_center.x + radius * std::cos(angle),
						a_center.y + radius * std::sin(angle));
					lowAmmoCtx.radius = 8.0f;
				}
				lowAmmoCtx.slotAngleRad = midAngle;
				lowAmmoCtx.alphaMult = a_drawArgs.alphaMult * pulse;
				lowAmmoCtx.slotIndex = a_index;
				lowAmmoCtx.formID = entry.ammo ? entry.ammo->GetFormID() : 0;
				lowAmmoCtx.progress = pulse;
				lowAmmoCtx.hovered = a_hovered;
				lowAmmoCtx.selected = isEquipped;
				lowAmmoCtx.active = false;
				drewLowAmmo = lowAmmoReskin.DrawTarget(
					AmmoWheelReskinUnified::VisualTarget::LowAmmoIndicator,
					entry.reskinEntry,
					lowAmmoCtx,
					ImGui::GetWindowDrawList());
			}
			if (!drewLowAmmo) {
				drawLowAmmoWarning(a_center, a_innerRadius, a_outerRadius, midAngle, a_drawArgs);
			}
		}
	};

	// Resolved preset for data-driven indicators (legacy preset system)
	const Config::AmmoWheel::StylePreset* preset = entry.resolvedPreset;
	bool usePresetStyling = Config::AmmoWheel::Skin::UsePresetStyles 
		&& preset != nullptr 
		&& preset->PresetId != "Default";

	// Reuse the standard indicator geometry in both reskin and legacy paths.
	auto drawStandardIndicators = [&](ImDrawList* indicatorList, bool drawSelected = true, bool drawHovered = true) {
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
				
				if (drawSelected && isEquipped && Config::AmmoWheel::Skin::EnableSelectedIndicator && selInd.Enabled) {
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
				
				// Hover indicator: show while hovered without blinking
				if (drawHovered && a_hovered && Config::AmmoWheel::Skin::EnableHoveredIndicator && hovInd.Enabled) {
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
				
				if (drawSelected && isEquipped && EnableSelectedIndicator && SelectedEnabled) {
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
				
				if (drawHovered && a_hovered && EnableHoveredIndicator && HoveredEnabled) {
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
			if (drawSelected && isEquipped && EnableSelectedIndicator && selEnabled) {
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
		ctx.maxFitSizePx = 0.0f;
		
		// Get primitive fallback colors
		const auto& primitives = entry.reskinEntry.preset->primitives;
		const auto& slotBackgroundAsset = entry.reskinEntry.preset->assets[static_cast<size_t>(AmmoWheelReskinUnified::VisualTarget::SlotBackground)];
		const bool slotBackgroundAssetConfigured =
			slotBackgroundAsset.enabled &&
			!slotBackgroundAsset.usePrimitive &&
			slotBackgroundAsset.type != AmmoWheelReskinUnified::AssetType::None;
		const bool selectedIndicatorAssetConfigured =
			reskinSystem.HasAssetForTarget(AmmoWheelReskinUnified::VisualTarget::IndicatorSelected, entry.reskinEntry);
		const bool hoveredIndicatorAssetConfigured =
			reskinSystem.HasAssetForTarget(AmmoWheelReskinUnified::VisualTarget::IndicatorHovered, entry.reskinEntry);

		auto drawSlotBackgroundShade = [&]() {
			const float shadeOpacity = std::clamp(Config::AmmoWheel::SlotBackgroundShadeOpacity, 0.0f, 1.0f);
			if (shadeOpacity <= 0.001f) {
				return;
			}

			constexpr float shadeInsetPx = 2.0f;
			const ImU32 shadeArcColor = IM_COL32(0, 0, 0, static_cast<int>(shadeOpacity * 255.0f));
			const ImU32 shadeSolidColor = IM_COL32(0, 0, 0, static_cast<int>(shadeOpacity * 255.0f * a_drawArgs.alphaMult));

			switch (Config::AmmoWheel::SlotShape) {
			case 0:  // Arc
			{
				const float shadedInner = a_innerRadius + shadeInsetPx;
				const float shadedOuter = (std::max)(shadedInner + 1.0f, a_outerRadius - shadeInsetPx);
				Drawer::draw_arc_gradient(
					a_center,
					shadedInner,
					shadedOuter,
					drawStartAngle,
					drawEndAngle,
					drawStartAngle,
					drawEndAngle,
					shadeArcColor,
					shadeArcColor,
					32,
					a_drawArgs);
			}
			break;
			case 1:  // Rounded Rectangle
			{
				const float rectWidth = (std::max)(8.0f, slotArcLength * 0.85f - shadeInsetPx * 2.0f);
				const float rectHeight = (std::max)(8.0f, slotWidth - shadeInsetPx * 2.0f);
				const float cornerRadius = (std::max)(0.0f, Config::AmmoWheel::SlotCornerRadius - shadeInsetPx);
				drawList->AddRectFilled(
					ImVec2(shapeCenterX - rectWidth * 0.5f, shapeCenterY - rectHeight * 0.5f),
					ImVec2(shapeCenterX + rectWidth * 0.5f, shapeCenterY + rectHeight * 0.5f),
					shadeSolidColor,
					cornerRadius);
			}
			break;
			case 2:  // Pill
			{
				const float pillRadius = (std::max)(4.0f, slotWidth * 0.5f - shadeInsetPx);
				const float pillLength = (std::max)(8.0f, slotArcLength * 0.7f - shadeInsetPx * 2.0f);
				drawList->AddRectFilled(
					ImVec2(shapeCenterX - pillLength * 0.5f, shapeCenterY - pillRadius),
					ImVec2(shapeCenterX + pillLength * 0.5f, shapeCenterY + pillRadius),
					shadeSolidColor,
					pillRadius);
			}
			break;
			case 3:  // Circle
			{
				const float circleRadius = (std::max)(4.0f, (std::min)(slotWidth, slotArcLength * 0.5f) * 0.85f - shadeInsetPx);
				drawList->AddCircleFilled(ImVec2(shapeCenterX, shapeCenterY), circleRadius, shadeSolidColor, 24);
			}
			break;
			}
		};

		// Draw slot shade first so SlotBackground asset remains fully visible above it.
		if (slotBackgroundAssetConfigured && Config::AmmoWheel::SlotBackgroundShadeEnabled) {
			drawSlotBackgroundShade();
		}

		// Draw slot background (try asset first, fallback to primitive)
		bool backgroundDrawn = reskinSystem.DrawTarget(AmmoWheelReskinUnified::VisualTarget::SlotBackground, entry.reskinEntry, ctx, drawList);
		// Draw slot frame early so indicators are always above background PNGs.
		reskinSystem.DrawTarget(AmmoWheelReskinUnified::VisualTarget::SlotFrame, entry.reskinEntry, ctx, drawList);
		ctx.maxFitSizePx = 0.0f;
		
		if (!backgroundDrawn) {
			// Primitive fallback: respect SlotShape setting
			ImU32 colorBegin;
			ImU32 colorEnd;
			if (a_hovered) {
				colorBegin = primitives.slotHoveredInner;
				colorEnd = primitives.slotHoveredOuter;
				if (Config::AmmoWheel::HoverBrightnessEnabled) {
					const float strength = Config::AmmoWheel::HoverBrightnessStrength;
					colorBegin = brightenColor(colorBegin, strength);
					colorEnd = brightenColor(colorEnd, strength);
				}
			} else {
				colorBegin = primitives.slotUnhoveredInner;
				colorEnd = primitives.slotUnhoveredOuter;
			}
			
			// ========== APPLY SELECTED BLINK (Config setting) ==========
			if (isEquipped && Config::AmmoWheel::SelectedBlinkEnabled) {
				float time = static_cast<float>(ImGui::GetTime());
				float blinkPhase = std::sin(time * Config::AmmoWheel::SelectedBlinkSpeedHz * 2.0f * 3.14159f);
				float blinkT = 0.5f + 0.5f * blinkPhase;
				float blinkStrength = 1.0f + Config::AmmoWheel::SelectedSlotBlinkStrength * blinkT;
				
				colorBegin = brightenColor(colorBegin, blinkStrength);
				colorEnd = brightenColor(colorEnd, blinkStrength);
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

		// Highlight overlay for PNG backgrounds (hover/selected tint).
		// If dedicated indicator assets are configured, do not also draw primitive-style
		// selected/hovered overlays on top; otherwise both visuals stack and look wrong.
		const bool allowSelectedOverlay = isEquipped && !selectedIndicatorAssetConfigured;
		const bool allowHoveredOverlay = a_hovered && !hoveredIndicatorAssetConfigured;
		if (backgroundDrawn && (allowHoveredOverlay || allowSelectedOverlay)) {
			ImU32 highlightBegin;
			ImU32 highlightEnd;
			
			// Match legacy slot color selection logic
			if (usePresetStyling && preset) {
				if (allowSelectedOverlay) {
					highlightBegin = preset->SelectedColorBegin;
					highlightEnd = preset->SelectedColorEnd;
				} else if (allowHoveredOverlay) {
					highlightBegin = preset->HoveredColorBegin;
					highlightEnd = preset->HoveredColorEnd;
				} else {
					highlightBegin = preset->UnhoveredColorBegin;
					highlightEnd = preset->UnhoveredColorEnd;
				}
			} else if (Config::AmmoWheel::UseSkyrimTheme) {
				using namespace Config::AmmoWheel::SkyrimTheme;
				highlightBegin = allowHoveredOverlay ? SlotHoveredInner : SlotUnhoveredInner;
				highlightEnd = allowHoveredOverlay ? SlotHoveredOuter : SlotUnhoveredOuter;
			} else if (Config::AmmoWheel::UseMainWheelTheme) {
				using namespace Config::Styling::Wheel;
				highlightBegin = allowHoveredOverlay ? HoveredColorBegin : UnhoveredColorBegin;
				highlightEnd = allowHoveredOverlay ? HoveredColorEnd : UnhoveredColorEnd;
			} else {
				highlightBegin = allowHoveredOverlay ? Config::AmmoWheel::HoveredColorBegin : Config::AmmoWheel::UnhoveredColorBegin;
				highlightEnd = allowHoveredOverlay ? Config::AmmoWheel::HoveredColorEnd : Config::AmmoWheel::UnhoveredColorEnd;
			}
			
			if (allowHoveredOverlay && Config::AmmoWheel::HoverBrightnessEnabled) {
				float strength = Config::AmmoWheel::HoverBrightnessStrength;
				highlightBegin = brightenColor(highlightBegin, strength);
				highlightEnd = brightenColor(highlightEnd, strength);
			}
			
			if (allowSelectedOverlay && Config::AmmoWheel::SelectedBlinkEnabled) {
				float time = static_cast<float>(ImGui::GetTime());
				float blinkPhase = std::sin(time * Config::AmmoWheel::SelectedBlinkSpeedHz * 2.0f * 3.14159f);
				float blinkT = 0.5f + 0.5f * blinkPhase;
				float blinkStrength = 1.0f + Config::AmmoWheel::SelectedSlotBlinkStrength * blinkT;
				highlightBegin = brightenColor(highlightBegin, blinkStrength);
				highlightEnd = brightenColor(highlightEnd, blinkStrength);
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
		// Deterministic indicator layering for reskin assets: charge -> active -> selected -> hovered.
		const bool selectedState = isEquipped;
		const bool hoveredState = a_hovered;
		const bool selectedEnabled = Config::AmmoWheel::Skin::EnableSelectedIndicator;
		const bool hoveredEnabled = Config::AmmoWheel::Skin::EnableHoveredIndicator;
		const bool activeEnabled = Config::AmmoWheel::Skin::EnableActiveIndicator;
		const bool chargeEnabled = Config::AmmoWheel::Skin::EnableChargeIndicator;

		float indicatorProgress = 0.0f;
		const bool hasRTUCharge = Config::AmmoWheel::UseRTUSystem && a_hovered && (Config::AmmoWheel::RTUHoverDelay > 0.0f);
		if (hasRTUCharge) {
			indicatorProgress = std::clamp(_hoveredTime / Config::AmmoWheel::RTUHoverDelay, 0.0f, 1.0f);
		}
		const bool chargeState = hasRTUCharge && indicatorProgress < 1.0f;
		const bool activeState = hasRTUCharge && indicatorProgress >= 1.0f;

		AmmoWheelReskinUnified::DrawContext indicatorCtx{};
		// Reskin indicators should be slot-anchored so custom overlay assets can
		// fully cover the hovered/selected slot instead of sticking to wheel center.
		indicatorCtx.center = ctx.center;
		indicatorCtx.radius = ctx.radius;
		indicatorCtx.slotAngleRad = midAngle;
		indicatorCtx.alphaMult = a_drawArgs.alphaMult;
		indicatorCtx.slotIndex = a_index;
		indicatorCtx.formID = entry.ammo ? entry.ammo->GetFormID() : 0;
		indicatorCtx.hovered = hoveredState;
		indicatorCtx.selected = selectedState;
		indicatorCtx.progress = indicatorProgress;
		indicatorCtx.active = activeState;
		// Keep indicator fit behavior consistent with SlotBackground/CenterPanelFrame:
		// do not cap by tangential arc span, otherwise wide indicator SVG assets
		// stay too small even at high scale.
		indicatorCtx.maxFitSizePx = 0.0f;

		bool drewSelected = false;
		bool drewHovered = false;
		bool drewCharge = false;
		bool drewActive = false;

		if (chargeEnabled && chargeState) {
			drewCharge = reskinSystem.DrawTarget(
				AmmoWheelReskinUnified::VisualTarget::IndicatorCharge, entry.reskinEntry, indicatorCtx, drawList);
		}
		if (activeEnabled && activeState) {
			drewActive = reskinSystem.DrawTarget(
				AmmoWheelReskinUnified::VisualTarget::IndicatorActive, entry.reskinEntry, indicatorCtx, drawList);
		}
		if (selectedEnabled && selectedState) {
			drewSelected = reskinSystem.DrawTarget(
				AmmoWheelReskinUnified::VisualTarget::IndicatorSelected, entry.reskinEntry, indicatorCtx, drawList);
		}
		if (hoveredEnabled && hoveredState) {
			drewHovered = reskinSystem.DrawTarget(
				AmmoWheelReskinUnified::VisualTarget::IndicatorHovered, entry.reskinEntry, indicatorCtx, drawList);
		}

		// Geometry fallback is per-indicator so missing assets do not disable the existing behavior.
		drawStandardIndicators(drawList, !drewSelected, !drewHovered);
		(void)drewCharge;
		(void)drewActive;
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
		
		// Text rendering - use cached per-entry label layout
		if (Config::AmmoWheel::LabelShow && entry.labelLayoutValid && entry.labelUsesReskinPathCached) {
			const auto& layout = entry.labelLayoutCached;
			const float baseTextSize = entry.labelFontSizeCached;
			const float baseLineSpacing = entry.labelLineSpacingCached;
			const float baseTotalTextHeight = entry.labelTotalTextHeightCached;
			const float baseClipHalfWidth = entry.labelClipHalfWidthCached;
			const float baseClipHalfHeight = entry.labelClipHalfHeightCached;
			const float bgHalfWidth = entry.labelBgHalfWidthCached;
			const float namePadding = Config::AmmoWheel::NamePanelPaddingPx;

			ImVec2 panelCenter = ImVec2(a_center.x + _cachedTextRadius * std::cos(midAngle),
				a_center.y + _cachedTextRadius * std::sin(midAngle));
			ImVec2 textCenter = panelCenter;

			float textScale = 1.0f;
			float textOpacity = 1.0f;
			if (reskinSystem.IsEnabled() && entry.reskinEntry.preset) {
				const auto textOverride = reskinSystem.GetLayoutOverrideSnapshot(
					AmmoWheelReskinUnified::VisualTarget::SlotLabelText);

				if (std::isfinite(textOverride.scale) && textOverride.scale > 0.0f) {
					textScale = std::clamp(textOverride.scale, 0.1f, 6.0f);
				}
				if (std::isfinite(textOverride.opacity)) {
					textOpacity = std::clamp(textOverride.opacity, 0.0f, 1.0f);
				}

				textCenter.x += textOverride.offsetX;
				textCenter.y += textOverride.offsetY;

				const float basisAngle = midAngle + textOverride.angleOffsetDeg * (IM_PI / 180.0f);
				if (std::fabs(textOverride.offsetRadial) > 0.001f || std::fabs(textOverride.offsetTangential) > 0.001f) {
					const float cosA = std::cos(basisAngle);
					const float sinA = std::sin(basisAngle);
					textCenter.x += cosA * textOverride.offsetRadial - sinA * textOverride.offsetTangential;
					textCenter.y += sinA * textOverride.offsetRadial + cosA * textOverride.offsetTangential;
				}
			}

			const float textSize = baseTextSize * textScale;
			const float lineSpacing = baseLineSpacing * textScale;
			const float totalTextHeight = baseTotalTextHeight * textScale;
			const float clipHalfWidth = baseClipHalfWidth * textScale;
			const float clipHalfHeight = baseClipHalfHeight * textScale;

			DrawArgs textDrawArgs = a_drawArgs;
			textDrawArgs.alphaMult *= textOpacity;

			float textX = textCenter.x;
			float textY = textCenter.y;
			if (Config::AmmoWheel::ShowAmmoCount) {
				textY -= totalTextHeight * 0.5f + Config::AmmoWheel::CountFontSize * 0.5f;
			} else {
				textY -= totalTextHeight * 0.5f;
			}

			float panelTextY = panelCenter.y;
			if (Config::AmmoWheel::ShowAmmoCount) {
				panelTextY -= baseTotalTextHeight * 0.5f + Config::AmmoWheel::CountFontSize * 0.5f;
			} else {
				panelTextY -= baseTotalTextHeight * 0.5f;
			}

			ImVec2 clipMin(textCenter.x - clipHalfWidth, textY - clipHalfHeight);
			ImVec2 clipMax(textCenter.x + clipHalfWidth, textY + totalTextHeight + clipHalfHeight);
			ImGui::GetWindowDrawList()->PushClipRect(clipMin, clipMax, true);

			if (Config::AmmoWheel::NameTextBgEnabled && !layout.lines.empty()) {
				float bgPadding = Config::AmmoWheel::NameTextBgExtraPaddingPx + namePadding;
				float bgInset = Config::AmmoWheel::NameTextBgInsetPx;
				float bgLeft = panelCenter.x - bgHalfWidth - bgInset;
				float bgRight = panelCenter.x + bgHalfWidth + bgInset;
				float bgTop = panelTextY - bgPadding;
				float bgBottom = panelTextY + baseTotalTextHeight + bgPadding;

				bool drewNamePanelBg = false;
				if (reskinSystem.IsEnabled() && entry.reskinEntry.preset) {
					AmmoWheelReskinUnified::DrawContext nameBgCtx{};
					nameBgCtx.center = ImVec2((bgLeft + bgRight) * 0.5f, (bgTop + bgBottom) * 0.5f);
					nameBgCtx.radius = 0.5f * (std::max)(bgRight - bgLeft, bgBottom - bgTop);
					nameBgCtx.slotAngleRad = 0.0f;
					nameBgCtx.alphaMult = a_drawArgs.alphaMult * Config::AmmoWheel::NameTextBgOpacity;
					nameBgCtx.slotIndex = a_index;
					nameBgCtx.formID = entry.ammo ? entry.ammo->GetFormID() : 0;
					drewNamePanelBg = reskinSystem.DrawTarget(
						AmmoWheelReskinUnified::VisualTarget::NamePanelBackground,
						entry.reskinEntry,
						nameBgCtx,
						drawList);
				}

				if (!drewNamePanelBg) {
					ImU32 bgColor = Config::AmmoWheel::NameTextBgColor;
					uint8_t bgAlpha = static_cast<uint8_t>((bgColor >> 24) * Config::AmmoWheel::NameTextBgOpacity * a_drawArgs.alphaMult);
					bgColor = (bgColor & 0x00FFFFFF) | (bgAlpha << 24);

					drawList->AddRectFilled(
						ImVec2(bgLeft, bgTop),
						ImVec2(bgRight, bgBottom),
						bgColor,
						Config::AmmoWheel::NameTextBgCornerRounding);
				}
			}

			ImU32 textColor;
			const bool emphasizeLabelColor = a_hovered || isEquipped;
			if (emphasizeLabelColor && Config::AmmoWheel::ArrowLabelColorOverrideEnabled) {
				textColor = Config::AmmoWheel::ArrowLabelColorComputed;
			} else if (Config::AmmoWheel::SlotLabelColorOverrideEnabled) {
				textColor = Config::AmmoWheel::SlotLabelColorComputed;
			} else {
				textColor = primitives.textPrimary;
			}
			uint8_t textAlpha = static_cast<uint32_t>((textColor >> 24) * textDrawArgs.alphaMult);
			textColor = (textColor & 0x00FFFFFF) | (textAlpha << 24);

			for (size_t lineIdx = 0; lineIdx < layout.lines.size(); lineIdx++) {
				float lineY = textY + static_cast<float>(lineIdx) * lineSpacing;
				if (Config::AmmoWheel::TextShadowEnabled) {
					ImU32 shadowColor = primitives.textShadow;
					uint8_t shadowAlpha = static_cast<uint32_t>((shadowColor >> 24) * textDrawArgs.alphaMult);
					shadowColor = (shadowColor & 0x00FFFFFF) | (shadowAlpha << 24);
					float shadowOffset = Config::AmmoWheel::TextShadowOffset;
					Drawer::draw_text(textX + shadowOffset, lineY + shadowOffset, layout.lines[lineIdx].c_str(), shadowColor, textSize, textDrawArgs);
				}
				Drawer::draw_text(textX, lineY, layout.lines[lineIdx].c_str(), textColor, textSize, textDrawArgs);
			}

			ImGui::GetWindowDrawList()->PopClipRect();
		}
		
		// Draw ammo count
		if (Config::AmmoWheel::ShowAmmoCount) {
			ImVec2 countCenter = ImVec2(a_center.x + _cachedCountRadius * std::cos(midAngle),
				a_center.y + _cachedCountRadius * std::sin(midAngle));
			drawAmmoCount(countCenter, entry.count, a_drawArgs, &entry.reskinEntry, midAngle);
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
			if (Config::AmmoWheel::HoverBrightnessEnabled) {
				const float strength = Config::AmmoWheel::HoverBrightnessStrength;
				colorBegin = brightenColor(colorBegin, strength);
				colorEnd = brightenColor(colorEnd, strength);
			}
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
		if (a_hovered && Config::AmmoWheel::HoverBrightnessEnabled) {
			const float strength = Config::AmmoWheel::HoverBrightnessStrength;
			colorBegin = brightenColor(colorBegin, strength);
			colorEnd = brightenColor(colorEnd, strength);
		}
		activeArcBegin = Config::AmmoWheel::ActiveArcColorBegin;
		activeArcEnd = Config::AmmoWheel::ActiveArcColorEnd;
		activeArcWidth = 8.0f;  // Default width for AmmoWheel theme
	}

	// Apply selected slot blink (brightness pulse)
	if (isEquipped && Config::AmmoWheel::SelectedBlinkEnabled) {
		float time = static_cast<float>(ImGui::GetTime());
		float blinkPhase = std::sin(time * Config::AmmoWheel::SelectedBlinkSpeedHz * 2.0f * 3.14159f);
		float blinkT = 0.5f + 0.5f * blinkPhase;  // 0 to 1
		float blinkStrength = 1.0f + Config::AmmoWheel::SelectedSlotBlinkStrength * blinkT;
		
		colorBegin = brightenColor(colorBegin, blinkStrength);
		colorEnd = brightenColor(colorEnd, blinkStrength);
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

	// Text/Label Rendering with cached layout
	if (Config::AmmoWheel::LabelShow && entry.labelLayoutValid && !entry.labelUsesReskinPathCached) {
		const auto& layout = entry.labelLayoutCached;
		const float textSize = entry.labelFontSizeCached;
		const float availWidth = entry.labelAvailWidthCached;
		const float lineSpacing = entry.labelLineSpacingCached;
		const float totalTextHeight = entry.labelTotalTextHeightCached;
		const float clipHalfWidth = entry.labelClipHalfWidthCached;
		const float clipHalfHeight = entry.labelClipHalfHeightCached;
		const float namePadding = Config::AmmoWheel::NamePanelPaddingPx;
		ImVec2 screenSize = ResolutionScale::Context::GetSingleton().GetRenderSize();
		const float wheelOuterRadius = Config::AmmoWheel::WheelRadius;
		const float wheelLeft = a_center.x - wheelOuterRadius;
		const float wheelRight = a_center.x + wheelOuterRadius;
		const float wheelTop = a_center.y - wheelOuterRadius;
		const float wheelBottom = a_center.y + wheelOuterRadius;

		float textX = textCenter.x;
		float textY = textCenter.y;
		if (Config::AmmoWheel::ShowAmmoCount) {
			textY -= (totalTextHeight * 0.3f);
		}
		float lineStartY = textY - totalTextHeight * 0.5f + textSize * 0.5f;

		ImVec2 clipMin(textCenter.x - clipHalfWidth, textCenter.y - clipHalfHeight);
		ImVec2 clipMax(textCenter.x + clipHalfWidth, textCenter.y + clipHalfHeight);

		if (Config::AmmoWheel::NameTextBgEnabled && !layout.lines.empty()) {
			float bgPadding = Config::AmmoWheel::NameTextBgExtraPaddingPx + namePadding;
			float bgInset = Config::AmmoWheel::NameTextBgInsetPx;
			float bgLeft = textX - clipHalfWidth - bgPadding;
			float bgRight = textX + clipHalfWidth + bgPadding;
			float bgTop = lineStartY - textSize * 0.5f - bgPadding;
			float bgBottom = lineStartY + totalTextHeight - textSize * 0.5f + bgPadding;
			bgLeft = (std::max)(bgLeft, bgInset);
			bgRight = (std::min)(bgRight, screenSize.x - bgInset);
			bgTop = (std::max)(bgTop, bgInset);
			bgBottom = (std::min)(bgBottom, screenSize.y - bgInset);

			ImU32 bgColor = Config::AmmoWheel::NameTextBgColor;
			uint8_t bgAlpha = static_cast<uint8_t>((bgColor >> 24) * Config::AmmoWheel::NameTextBgOpacity * a_drawArgs.alphaMult);
			bgColor = (bgColor & 0x00FFFFFF) | (bgAlpha << 24);
			ImGui::GetWindowDrawList()->AddRectFilled(
				ImVec2(bgLeft, bgTop),
				ImVec2(bgRight, bgBottom),
				bgColor,
				Config::AmmoWheel::NameTextBgCornerRounding);
		}

		if (Config::AmmoWheel::DebugDrawTextRects) {
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRect(clipMin, clipMax, IM_COL32(255, 0, 0, 180), 0.0f, 0, 1.0f);
			float wrapLineX = textX + availWidth * 0.5f;
			dl->AddLine(ImVec2(wrapLineX, clipMin.y), ImVec2(wrapLineX, clipMax.y), IM_COL32(0, 255, 0, 180), 1.0f);
			dl->AddRect(ImVec2(wheelLeft, wheelTop), ImVec2(wheelRight, wheelBottom), IM_COL32(0, 0, 255, 100), 0.0f, 0, 1.0f);
		}

		ImGui::GetWindowDrawList()->PushClipRect(clipMin, clipMax, true);
		ImU32 textColor;
		const bool emphasizeLabelColor = a_hovered || isEquipped;
		if (emphasizeLabelColor && Config::AmmoWheel::ArrowLabelColorOverrideEnabled) {
			textColor = Config::AmmoWheel::ArrowLabelColorComputed;
		} else if (Config::AmmoWheel::SlotLabelColorOverrideEnabled) {
			textColor = Config::AmmoWheel::SlotLabelColorComputed;
		} else if (Config::AmmoWheel::UseSkyrimTheme) {
			textColor = Config::AmmoWheel::SkyrimTheme::TextPrimary;
		} else {
			textColor = Config::AmmoWheel::NameTextColor;
		}
		uint8_t textAlpha = static_cast<uint8_t>((textColor >> 24) * a_drawArgs.alphaMult);
		textColor = (textColor & 0x00FFFFFF) | (textAlpha << 24);

		for (size_t lineIdx = 0; lineIdx < layout.lines.size(); lineIdx++) {
			float lineY = lineStartY + static_cast<float>(lineIdx) * lineSpacing;
			if (Config::AmmoWheel::TextShadowEnabled) {
				uint8_t shadowAlpha = static_cast<uint8_t>(Config::AmmoWheel::TextShadowAlpha * a_drawArgs.alphaMult);
				ImU32 shadowColor = IM_COL32(0, 0, 0, shadowAlpha);
				float offset = Config::AmmoWheel::TextShadowOffset;
				Drawer::draw_text(textX + offset, lineY + offset, layout.lines[lineIdx].c_str(), shadowColor, textSize, a_drawArgs);
			}

			if (a_hovered && Config::AmmoWheel::TextHoverGlowEnabled) {
				ImU32 glowColor = Config::AmmoWheel::UseSkyrimTheme
					? Config::AmmoWheel::SkyrimTheme::HighlightGold
					: Config::AmmoWheel::TextHoverGlowColor;
				uint8_t glowAlpha = static_cast<uint8_t>((glowColor >> 24) * a_drawArgs.alphaMult);
				glowColor = (glowColor & 0x00FFFFFF) | (glowAlpha << 24);
				Drawer::draw_text(textX, lineY, layout.lines[lineIdx].c_str(), glowColor, textSize * 1.02f, a_drawArgs);
			}

			if (Config::AmmoWheel::NameBoldEnabled && Config::AmmoWheel::NameBoldMode == 1) {
				float boldOffset = Config::AmmoWheel::NameBoldStrengthPx;
				Drawer::draw_text(textX + boldOffset, lineY, layout.lines[lineIdx].c_str(), textColor, textSize, a_drawArgs);
				Drawer::draw_text(textX, lineY + boldOffset, layout.lines[lineIdx].c_str(), textColor, textSize, a_drawArgs);
			}

			Drawer::draw_text(textX, lineY, layout.lines[lineIdx].c_str(), textColor, textSize, a_drawArgs);
		}

		ImGui::GetWindowDrawList()->PopClipRect();
	}

	// Draw ammo count at its configured radius position
	if (Config::AmmoWheel::ShowAmmoCount) {
		ImVec2 countCenter = ImVec2(
			a_center.x + _cachedCountRadius * std::cos(midAngle),
			a_center.y + _cachedCountRadius * std::sin(midAngle)
		);
		drawAmmoCount(countCenter, entry.count, a_drawArgs, nullptr, midAngle);
	}

	drawLowAmmoAtLayer(2);
}

void AmmoWheel::drawHighlight(ImVec2 a_center, DrawArgs a_drawArgs, bool a_centerBgAlreadyDrawn)
{
	if (!Config::AmmoWheel::CenterEnabled || !_centerPanelCache.valid) {
		return;
	}
	if (_hoveredIndex < 0 || _hoveredIndex >= static_cast<int>(_ammoEntries.size())) {
		return;
	}
	const auto& entry = _ammoEntries[_hoveredIndex];

	const auto& cache = _centerPanelCache;
	if (cache.textLines.empty() && cache.descriptionLines.empty()) {
		return;
	}

	ImVec2 panelCenter = cache.panelCenter;
	panelCenter.x += a_center.x - cache.wheelCenterAtBuild.x;
	panelCenter.y += a_center.y - cache.wheelCenterAtBuild.y;
	const float panelWidth = cache.panelWidth;
	const float panelHeight = cache.panelHeight;
	const float padding = cache.padding;
	const float lineSpacing = cache.lineSpacing;

	ImVec2 bgMin = ImVec2(panelCenter.x - panelWidth * 0.5f, panelCenter.y - panelHeight * 0.5f);
	ImVec2 bgMax = ImVec2(panelCenter.x + panelWidth * 0.5f, panelCenter.y + panelHeight * 0.5f);

	int shapeType = Config::AmmoWheel::CenterPanelShapeIndex;
	if (shapeType == 0) {
		const float arcSpan = getArcAngleRad();
		shapeType = (arcSpan >= 2.0f * IM_PI * 0.9f) ? 2 : 3;
	}

	if (Config::AmmoWheel::CenterBgEnabled && !a_centerBgAlreadyDrawn) {
		auto* drawList = ImGui::GetWindowDrawList();
		bool drewCenterBg = false;

		auto& reskinSystem = AmmoWheelReskinUnified::ReskinSystem::GetSingleton();
		if (reskinSystem.IsEnabled() && entry.reskinEntry.preset) {
			AmmoWheelReskinUnified::DrawContext centerCtx{};
			centerCtx.center = panelCenter;
			centerCtx.radius = 0.5f * (std::max)(panelWidth, panelHeight);
			centerCtx.slotAngleRad = 0.0f;
			centerCtx.alphaMult = a_drawArgs.alphaMult;
			centerCtx.slotIndex = _hoveredIndex;
			centerCtx.formID = entry.ammo ? entry.ammo->GetFormID() : 0;
			centerCtx.hovered = true;
			centerCtx.selected = false;
			centerCtx.active = false;
			centerCtx.progress = 0.0f;
			drewCenterBg = reskinSystem.DrawTarget(
				AmmoWheelReskinUnified::VisualTarget::CenterBackground,
				entry.reskinEntry,
				centerCtx,
				drawList);
		}

		if (!drewCenterBg) {
			float bgAlpha = Config::AmmoWheel::CenterBgOpacity * a_drawArgs.alphaMult;
			ImU32 bgColor = IM_COL32(0, 0, 0, static_cast<int>(bgAlpha * 255));

			switch (shapeType) {
			case 1:
				drawList->AddRectFilled(bgMin, bgMax, bgColor, 0.0f);
				break;
			case 2:
			{
				float radius = (std::max)(panelWidth, panelHeight) * 0.5f * 1.1f;
				drawList->AddCircleFilled(panelCenter, radius, bgColor, 48);
				break;
			}
			case 3:
			default:
				drawList->AddRectFilled(bgMin, bgMax, bgColor, Config::AmmoWheel::CenterPanelCornerRounding);
				break;
			}
		}

		if (Config::AmmoWheel::CenterPanelBorderThickness > 0.0f) {
			float borderAlpha = Config::AmmoWheel::CenterPanelBorderAlpha * a_drawArgs.alphaMult;
			ImU32 borderColor = IM_COL32(139, 90, 43, static_cast<int>(borderAlpha * 255));
			switch (shapeType) {
			case 1:
				drawList->AddRect(bgMin, bgMax, borderColor, 0.0f, 0, Config::AmmoWheel::CenterPanelBorderThickness);
				break;
			case 2:
			{
				float radius = (std::max)(panelWidth, panelHeight) * 0.5f * 1.1f;
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

	if (Config::AmmoWheel::CenterFrameEnabled) {
		const float framePadding = 5.0f;
		ImVec2 frameMin = ImVec2(bgMin.x - framePadding, bgMin.y - framePadding);
		ImVec2 frameMax = ImVec2(bgMax.x + framePadding, bgMax.y + framePadding);

		float frameAlphaMult = a_drawArgs.alphaMult;
		if (Config::AmmoWheel::CenterFramePulse) {
			float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * Config::AmmoWheel::CenterFramePulseSpeed);
			frameAlphaMult *= pulse;
		}
		bool drewCenterFrame = false;
		auto& reskinSystem = AmmoWheelReskinUnified::ReskinSystem::GetSingleton();
		const bool useReskinIndependentCenterFrame = reskinSystem.IsEnabled() && entry.reskinEntry.preset;
		if (useReskinIndependentCenterFrame) {
			// Reskin path: keep CenterPanelFrame independent from center panel primitive layout
			// (MaxWidthRatio / font size / padding / center panel shape offsets).
			// Anchor to wheel center and let Reskin.Layout.CenterPanelFrame drive placement.
			AmmoWheelReskinUnified::DrawContext frameCtx{};
			frameCtx.center = a_center;
			frameCtx.radius = (std::max)(16.0f, _cachedInnerRadius * 0.62f);
			frameCtx.slotAngleRad = 0.0f;
			frameCtx.alphaMult = frameAlphaMult;
			frameCtx.slotIndex = _hoveredIndex;
			frameCtx.formID = entry.ammo ? entry.ammo->GetFormID() : 0;
			frameCtx.hovered = true;
			drewCenterFrame = reskinSystem.DrawTarget(
				AmmoWheelReskinUnified::VisualTarget::CenterPanelFrame,
				entry.reskinEntry,
				frameCtx,
				ImGui::GetWindowDrawList());
		}

		if (!drewCenterFrame) {
			// Primitive fallback should honor CenterPanelFrame layout overrides too,
			// so frame tuning stays independent in both reskin and primitive paths.
			const auto frameLayout = reskinSystem.GetLayoutOverrideSnapshot(
				AmmoWheelReskinUnified::VisualTarget::CenterPanelFrame);
			const float frameLayoutScale =
				(std::isfinite(frameLayout.scale) && frameLayout.scale > 0.0f) ?
					frameLayout.scale :
					1.0f;
			ImVec2 frameCenterBase = panelCenter;
			float frameHalfWidth = (panelWidth * 0.5f + framePadding);
			float frameHalfHeight = (panelHeight * 0.5f + framePadding);
			if (useReskinIndependentCenterFrame) {
				// Keep fallback aligned with reskin behavior when frame asset is missing.
				frameCenterBase = a_center;
				const float independentHalf = (std::max)(16.0f, _cachedInnerRadius * 0.62f);
				frameHalfWidth = independentHalf + framePadding;
				frameHalfHeight = independentHalf + framePadding;
			}
			const ImVec2 frameCenterAdjusted = ImVec2(
				frameCenterBase.x + frameLayout.offsetX + frameLayout.offsetRadial,
				frameCenterBase.y + frameLayout.offsetY + frameLayout.offsetTangential);
			frameHalfWidth *= frameLayoutScale;
			frameHalfHeight *= frameLayoutScale;
			frameMin = ImVec2(frameCenterAdjusted.x - frameHalfWidth, frameCenterAdjusted.y - frameHalfHeight);
			frameMax = ImVec2(frameCenterAdjusted.x + frameHalfWidth, frameCenterAdjusted.y + frameHalfHeight);

			ImU32 frameColor = Config::AmmoWheel::CenterFrameColor;
			uint8_t frameAlpha = static_cast<uint8_t>((frameColor >> 24) * frameAlphaMult);
			frameColor = (frameColor & 0x00FFFFFF) | (frameAlpha << 24);

			ImGui::GetWindowDrawList()->AddRect(frameMin, frameMax, frameColor, 6.0f, 0, 3.0f);
			if (Config::AmmoWheel::CenterCornersEnabled) {
				float len = Config::AmmoWheel::CenterCornerSize * frameLayoutScale;
				auto* drawList = ImGui::GetWindowDrawList();
				drawList->AddLine(ImVec2(frameMin.x, frameMin.y + len), ImVec2(frameMin.x, frameMin.y), frameColor, 2.0f);
				drawList->AddLine(ImVec2(frameMin.x, frameMin.y), ImVec2(frameMin.x + len, frameMin.y), frameColor, 2.0f);
				drawList->AddLine(ImVec2(frameMax.x - len, frameMin.y), ImVec2(frameMax.x, frameMin.y), frameColor, 2.0f);
				drawList->AddLine(ImVec2(frameMax.x, frameMin.y), ImVec2(frameMax.x, frameMin.y + len), frameColor, 2.0f);
				drawList->AddLine(ImVec2(frameMax.x, frameMax.y - len), ImVec2(frameMax.x, frameMax.y), frameColor, 2.0f);
				drawList->AddLine(ImVec2(frameMax.x, frameMax.y), ImVec2(frameMax.x - len, frameMax.y), frameColor, 2.0f);
				drawList->AddLine(ImVec2(frameMin.x + len, frameMax.y), ImVec2(frameMin.x, frameMax.y), frameColor, 2.0f);
				drawList->AddLine(ImVec2(frameMin.x, frameMax.y), ImVec2(frameMin.x, frameMax.y - len), frameColor, 2.0f);
			}
		}
	}

	const float textX = panelCenter.x + Config::AmmoWheel::CenterTextOffsetX;
	auto& centerReskinSystem = AmmoWheelReskinUnified::ReskinSystem::GetSingleton();
	const bool centerReskinEnabled = centerReskinSystem.IsEnabled() && entry.reskinEntry.preset;
	float textY = panelCenter.y - panelHeight * 0.5f + padding + Config::AmmoWheel::CenterTextOffsetY;
	for (size_t i = 0; i < cache.textLines.size(); i++) {
		const bool isDamageLine = (i < cache.isDamageLine.size() && cache.isDamageLine[i]);
		ImU32 textColor;
		if (isDamageLine) {
			if (cache.highlightDamage) {
				ImU32 maxColor = Config::AmmoWheel::CenterFields::MaxDamageColor;
				textColor = IM_COL32(
					(maxColor >> IM_COL32_R_SHIFT) & 0xFF,
					(maxColor >> IM_COL32_G_SHIFT) & 0xFF,
					(maxColor >> IM_COL32_B_SHIFT) & 0xFF,
					static_cast<int>(255 * a_drawArgs.alphaMult));
			} else {
				ImU32 otherColor = Config::AmmoWheel::CenterFields::OtherDamageColor;
				textColor = IM_COL32(
					(otherColor >> IM_COL32_R_SHIFT) & 0xFF,
					(otherColor >> IM_COL32_G_SHIFT) & 0xFF,
					(otherColor >> IM_COL32_B_SHIFT) & 0xFF,
					static_cast<int>(255 * a_drawArgs.alphaMult));
			}
		} else {
			if (Config::AmmoWheel::CenterLabelColorOverrideEnabled) {
				textColor = Config::AmmoWheel::CenterLabelColorComputed;
				uint8_t a = static_cast<uint8_t>((textColor >> 24) * a_drawArgs.alphaMult);
				textColor = (textColor & 0x00FFFFFF) | (a << 24);
			} else {
				textColor = IM_COL32(255, 255, 255, static_cast<int>(255 * a_drawArgs.alphaMult));
			}
		}

		if (isDamageLine && centerReskinEnabled) {
			const std::string damageDigits = std::to_string((std::max)(0, cache.roundedDamageValue));
			const float lineFontSize = cache.textLines[i].second;
			if (!damageDigits.empty() && lineFontSize > 0.0f) {
				const char* damageLabel = "Damage:";
				ImFont* font = ImGui::GetDefaultFont();
				const float labelWidth = font ? font->CalcTextSizeA(lineFontSize, FLT_MAX, 0.0f, damageLabel).x :
					ImGui::CalcTextSize(damageLabel).x;
				const float estimatedDigitsWidth = font ? font->CalcTextSizeA(lineFontSize, FLT_MAX, 0.0f, damageDigits.c_str()).x :
					ImGui::CalcTextSize(damageDigits.c_str()).x;
				const float gap = (std::max)(2.0f, lineFontSize * 0.10f);

				auto drawDamageDigits = [&](AmmoWheelReskinUnified::VisualTarget target, float alphaScale, ImVec2 center, float* outWidth) {
					AmmoWheelReskinUnified::DrawContext digitCtx{};
					digitCtx.center = center;
					digitCtx.radius = lineFontSize * 0.5f;
					digitCtx.slotAngleRad = 0.0f;
					digitCtx.alphaMult = a_drawArgs.alphaMult * alphaScale;
					return centerReskinSystem.DrawDigitString(
						target,
						entry.reskinEntry,
						digitCtx,
						damageDigits,
						lineFontSize,
						ImGui::GetWindowDrawList(),
						outWidth);
				};

				const auto primaryTarget = cache.highlightDamage ?
					AmmoWheelReskinUnified::VisualTarget::MaxDamageDigits :
					AmmoWheelReskinUnified::VisualTarget::DamageDigits;

				float renderedDigitsWidth = 0.0f;
				bool measured = drawDamageDigits(primaryTarget, 0.0f, ImVec2(textX, textY), &renderedDigitsWidth);
				if (!measured && primaryTarget == AmmoWheelReskinUnified::VisualTarget::MaxDamageDigits) {
					measured = drawDamageDigits(AmmoWheelReskinUnified::VisualTarget::DamageDigits, 0.0f, ImVec2(textX, textY), &renderedDigitsWidth);
				}
				if (!(renderedDigitsWidth > 0.0f) || !std::isfinite(renderedDigitsWidth)) {
					renderedDigitsWidth = (std::max)(1.0f, estimatedDigitsWidth);
				}

				const float totalWidth = labelWidth + gap + renderedDigitsWidth;
				const float leftX = textX - totalWidth * 0.5f;
				const float labelCenterX = leftX + labelWidth * 0.5f;
				const float digitsCenterX = leftX + labelWidth + gap + renderedDigitsWidth * 0.5f;

				bool drewDigits = drawDamageDigits(primaryTarget, 1.0f, ImVec2(digitsCenterX, textY), &renderedDigitsWidth);
				if (!drewDigits && primaryTarget == AmmoWheelReskinUnified::VisualTarget::MaxDamageDigits) {
					drewDigits = drawDamageDigits(AmmoWheelReskinUnified::VisualTarget::DamageDigits, 1.0f, ImVec2(digitsCenterX, textY), &renderedDigitsWidth);
				}

				if (drewDigits) {
					if (Config::AmmoWheel::CenterTextShadowEnabled) {
						const float shadowOffset = 1.5f;
						const ImU32 shadowColor = IM_COL32(0, 0, 0, static_cast<int>(180 * a_drawArgs.alphaMult));
						Drawer::draw_text(
							labelCenterX + shadowOffset,
							textY + shadowOffset,
							damageLabel,
							shadowColor,
							lineFontSize,
							a_drawArgs);
					}

					Drawer::draw_text(labelCenterX, textY, damageLabel, textColor, lineFontSize, a_drawArgs);
					textY += lineFontSize + lineSpacing;
					continue;
				}
			}
		}

		if (Config::AmmoWheel::CenterTextShadowEnabled) {
			float shadowOffset = 1.5f;
			ImU32 shadowColor = IM_COL32(0, 0, 0, static_cast<int>(180 * a_drawArgs.alphaMult));
			Drawer::draw_text(
				textX + shadowOffset,
				textY + shadowOffset,
				cache.textLines[i].first.c_str(),
				shadowColor,
				cache.textLines[i].second,
				a_drawArgs);
		}

		Drawer::draw_text(textX, textY, cache.textLines[i].first.c_str(), textColor, cache.textLines[i].second, a_drawArgs);
		textY += cache.textLines[i].second + lineSpacing;
	}

	if (Config::AmmoWheel::CenterShowDescription && !cache.descriptionLines.empty()) {
		auto& reskinSystem = AmmoWheelReskinUnified::ReskinSystem::GetSingleton();
		const bool reskinEnabled = reskinSystem.IsEnabled();
		const auto descLayout = reskinSystem.GetLayoutOverrideSnapshot(
			AmmoWheelReskinUnified::VisualTarget::CenterDescriptionText);

		// Reskin mode: anchor description to wheel center, completely independent from center panel box.
		// Primitive mode: preserve old center-attached behavior for backward compatibility.
		ImVec2 descAnchor = reskinEnabled ? a_center : panelCenter;
		float layoutAngleRad = reskinEnabled ? (descLayout.angleOffsetDeg * IM_PI / 180.0f) : 0.0f;
		float ca = std::cos(layoutAngleRad);
		float sa = std::sin(layoutAngleRad);
		if (reskinEnabled) {
			descAnchor.x += descLayout.offsetX + ca * descLayout.offsetRadial - sa * descLayout.offsetTangential;
			descAnchor.y += descLayout.offsetY + sa * descLayout.offsetRadial + ca * descLayout.offsetTangential;
		} else {
			descAnchor.x += Config::AmmoWheel::CenterDescriptionOffsetX;
			descAnchor.y += Config::AmmoWheel::CenterDescriptionOffsetY;
		}

		float descScale = reskinEnabled ? descLayout.scale : 1.0f;
		descScale = std::clamp(descScale, 0.05f, 8.0f);
		float descOpacity = std::clamp(Config::AmmoWheel::CenterDescriptionOpacity, 0.0f, 1.0f);
		if (reskinEnabled) {
			descOpacity *= std::clamp(descLayout.opacity, 0.0f, 1.0f);
		}
		const ImU32 descColor = Config::AmmoWheel::CenterDescriptionColor;
		const uint8_t descAlpha = static_cast<uint8_t>(
			((descColor >> IM_COL32_A_SHIFT) & 0xFF) * descOpacity * a_drawArgs.alphaMult);
		const ImU32 descTextColor = IM_COL32(
			(descColor >> IM_COL32_R_SHIFT) & 0xFF,
			(descColor >> IM_COL32_G_SHIFT) & 0xFF,
			(descColor >> IM_COL32_B_SHIFT) & 0xFF,
			descAlpha);

		float descY = descAnchor.y;
		for (const auto& line : cache.descriptionLines) {
			const float fontSize = cache.descriptionFontSize * descScale;
			if (Config::AmmoWheel::CenterTextShadowEnabled) {
				const float shadowOffset = 1.5f;
				const ImU32 shadowColor = IM_COL32(0, 0, 0, static_cast<int>(180 * a_drawArgs.alphaMult * descOpacity));
				Drawer::draw_text(
					descAnchor.x + shadowOffset,
					descY + shadowOffset,
					line.c_str(),
					shadowColor,
					fontSize,
					a_drawArgs);
			}

			Drawer::draw_text(descAnchor.x, descY, line.c_str(), descTextColor, fontSize, a_drawArgs);
			descY += fontSize + lineSpacing + Config::AmmoWheel::CenterDescriptionLineSpacingPx;
		}
	}
}

void AmmoWheel::drawAmmoCount(ImVec2 a_slotCenter, int a_count, DrawArgs a_drawArgs,
	const AmmoWheelReskinUnified::ResolvedEntry* a_reskinEntry, float a_slotAngleRad)
{
	ImU32 color = Config::AmmoWheel::CountColor;
	float fontSize = Config::AmmoWheel::CountFontPx;  // Use configured font size
	const float baselineY = a_slotCenter.y + 10.0f;
	const bool plusSuffix = a_count > 999;
	const std::string digitsOnly = plusSuffix ? "999" : std::to_string(a_count);
	float digitSpacingOffsetPx = 0.0f;

	if (a_reskinEntry) {
		auto& reskinSystem = AmmoWheelReskinUnified::ReskinSystem::GetSingleton();
		if (reskinSystem.IsEnabled() && a_reskinEntry->preset) {
			digitSpacingOffsetPx =
				reskinSystem.GetLayoutOverrideSnapshot(AmmoWheelReskinUnified::VisualTarget::AmmoCountDigits)
					.digitSpacingOffsetPx;

			AmmoWheelReskinUnified::DrawContext baseCtx{};
			baseCtx.center = ImVec2(a_slotCenter.x, baselineY);
			baseCtx.radius = fontSize * 0.5f;
			baseCtx.slotAngleRad = a_slotAngleRad;
			baseCtx.alphaMult = a_drawArgs.alphaMult;

			float renderedWidth = 0.0f;
			if (reskinSystem.DrawDigitString(
				AmmoWheelReskinUnified::VisualTarget::AmmoCountDigits,
				*a_reskinEntry,
				baseCtx,
				digitsOnly,
				fontSize,
				ImGui::GetWindowDrawList(),
				&renderedWidth)) {
				if (plusSuffix) {
					constexpr float kPlusPaddingPx = 2.0f;
					Drawer::draw_text(
						baseCtx.center.x + renderedWidth * 0.5f + kPlusPaddingPx,
						baselineY,
						"+",
						color,
						fontSize,
						a_drawArgs);
				}
				return;
			}
		}
	}

	// If digits asset falls back to primitive text, still honor Angular Spread for multi-glyph counts.
	if (std::fabs(digitSpacingOffsetPx) > 0.001f) {
		const std::string countStr = plusSuffix ? "999+" : std::to_string(a_count);
		ImFont* font = ImGui::GetDefaultFont();
		if (font && !countStr.empty()) {
			const float spacing = digitSpacingOffsetPx;
			float totalW = 0.0f;
			float maxH = 0.0f;
			std::vector<float> glyphWidths;
			glyphWidths.reserve(countStr.size());
			for (char ch : countStr) {
				const char glyph[2] = { ch, '\0' };
				const ImVec2 sz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, glyph);
				glyphWidths.push_back(sz.x);
				totalW += sz.x;
				maxH = (std::max)(maxH, sz.y);
			}
			if (countStr.size() > 1) {
				totalW += spacing * static_cast<float>(countStr.size() - 1);
			}
			float cursorX = a_slotCenter.x - totalW * 0.5f;
			const float topY = baselineY - maxH * 0.5f;
			for (size_t i = 0; i < countStr.size(); ++i) {
				const char glyph[2] = { countStr[i], '\0' };
				Drawer::draw_text_with_font(
					cursorX,
					topY,
					glyph,
					color,
					font,
					fontSize,
					a_drawArgs,
					false);
				cursorX += glyphWidths[i];
				if (i + 1 < countStr.size()) {
					cursorX += spacing;
				}
			}
			return;
		}
	}

	std::string countStr = plusSuffix ? "999+" : std::to_string(a_count);
	Drawer::draw_text(a_slotCenter.x, baselineY, countStr.c_str(), color, fontSize, a_drawArgs);
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

	// ========== POPUP ANIMATION ==========
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
	float midAngle = getSlotCenterAngle(_hoveredIndex);
	if (_lastCursorInputSource == CursorInputSource::Mouse) {
		const float cursorLen = std::sqrt(_cursorPos.x * _cursorPos.x + _cursorPos.y * _cursorPos.y);
		if (cursorLen > 0.001f) {
			midAngle = getCursorAngle();
		}
	}
	
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
	
	// ========== APPLY EASING FUNCTION ==========
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

	int popupShapeMode = std::clamp(Config::AmmoWheel::PopupShapeMode, 0, 5);
	if (popupShapeMode == 0) {
		// Backward compatibility: legacy toggle maps to circle vs rounded-rect.
		popupShapeMode = Config::AmmoWheel::PopupCircular ? 1 : 2;
	}
	const float sunDragonTone = std::clamp(Config::AmmoWheel::PopupSunDragonTone, 0.0f, 2.0f);

	auto calcPopupRectBounds = [&](ImVec2& outMin, ImVec2& outMax) {
		float popupWidth = bubbleRadius * 2.0f;
		float popupHeight = bubbleRadius * 2.0f;
		outMin = ImVec2(popupCenter.x - popupWidth * 0.5f, popupCenter.y - popupHeight * 0.5f);
		outMax = ImVec2(popupCenter.x + popupWidth * 0.5f, popupCenter.y + popupHeight * 0.5f);
	};

	auto hash01 = [](uint32_t value) -> float {
		value ^= value >> 16;
		value *= 0x7feb352dU;
		value ^= value >> 15;
		value *= 0x846ca68bU;
		value ^= value >> 16;
		return static_cast<float>(value & 0x00FFFFFFU) / 16777216.0f;
	};

	auto toneSunDragonColor = [&](int r, int g, int b, int a) -> ImU32 {
		const float t = sunDragonTone;
		const float sat = 0.45f + t * 0.45f;  // 0->desat, 1->near natural, 2->more vivid
		const float warmBlend = std::clamp(1.0f - t, 0.0f, 1.0f) * 0.38f;
		const float brighten = 1.12f - 0.16f * std::clamp(t, 0.0f, 1.0f) + 0.04f * (std::max)(t - 1.0f, 0.0f);

		const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
		float rr = luma + (r - luma) * sat;
		float gg = luma + (g - luma) * sat;
		float bb = luma + (b - luma) * sat;

		// Blend toward pale warm tones for softer "sunlight" at low tone values.
		rr = rr * (1.0f - warmBlend) + 236.0f * warmBlend;
		gg = gg * (1.0f - warmBlend) + 212.0f * warmBlend;
		bb = bb * (1.0f - warmBlend) + 164.0f * warmBlend;

		rr *= brighten;
		gg *= brighten;
		bb *= brighten;

		return IM_COL32(
			static_cast<int>(std::clamp(rr, 0.0f, 255.0f)),
			static_cast<int>(std::clamp(gg, 0.0f, 255.0f)),
			static_cast<int>(std::clamp(bb, 0.0f, 255.0f)),
			std::clamp(a, 0, 255));
	};

	auto buildOrganicBubblePoints = [&](float baseRadius, float phaseOffset) {
		const int pointCount = std::clamp(Config::AmmoWheel::PopupBlobPointCount, 8, 48);
		const float jaggedness = std::clamp(Config::AmmoWheel::PopupBlobJaggedness, 0.0f, 0.45f);
		const float wobbleSpeed = std::clamp(Config::AmmoWheel::PopupBlobWobbleSpeed, 0.0f, 8.0f);
		const float wobbleTime = static_cast<float>(ImGui::GetTime()) * wobbleSpeed;
		const float safeRadius = (std::max)(baseRadius, 4.0f);
		const uint32_t baseSeed = entry.ammo ? entry.ammo->GetFormID() :
			static_cast<uint32_t>(_hoveredIndex + 1);

		std::vector<ImVec2> points;
		points.reserve(pointCount);

		for (int i = 0; i < pointCount; ++i) {
			const float u = static_cast<float>(i) / static_cast<float>(pointCount);
			const float angle = u * (2.0f * IM_PI) + phaseOffset;
			const uint32_t seed = baseSeed ^ (0x9E3779B9u * static_cast<uint32_t>(i + 1));
			const float rand01 = hash01(seed);
			const float randSigned = rand01 * 2.0f - 1.0f;
			const float wave = std::sin(angle * 3.0f + wobbleTime * 0.75f + rand01 * 6.2831853f);
			const float wobble = std::clamp(randSigned * 0.45f + wave * 0.55f, -1.0f, 1.0f);
			const float localRadius = safeRadius * (1.0f + jaggedness * wobble);

			points.emplace_back(
				popupCenter.x + localRadius * std::cos(angle),
				popupCenter.y + localRadius * std::sin(angle));
		}

		return points;
	};

	auto buildDragonFireBubblePoints = [&](float baseRadius, float phaseOffset) {
		const int pointCount = std::clamp((std::max)(Config::AmmoWheel::PopupBlobPointCount, 16), 16, 48);
		const float jaggedness = std::clamp(Config::AmmoWheel::PopupBlobJaggedness, 0.0f, 0.45f);
		const float flickerSpeed = std::clamp(Config::AmmoWheel::PopupBlobWobbleSpeed, 0.0f, 8.0f);
		const float tFire = static_cast<float>(ImGui::GetTime()) * (flickerSpeed * 1.8f + 0.8f);
		const float safeRadius = (std::max)(baseRadius, 6.0f);
		const uint32_t baseSeed = entry.ammo ? entry.ammo->GetFormID() :
			static_cast<uint32_t>(_hoveredIndex + 1);
		const float flameAmp = 0.18f + jaggedness * 0.55f;
		const float swayAmp = 0.04f + jaggedness * 0.14f;
		const float lowerCompress = 0.12f + jaggedness * 0.18f;

		std::vector<ImVec2> points;
		points.reserve(pointCount);

		for (int i = 0; i < pointCount; ++i) {
			const float u = static_cast<float>(i) / static_cast<float>(pointCount);
			const float angle = u * (2.0f * IM_PI) + phaseOffset;
			const float dx = std::cos(angle);
			const float dy = std::sin(angle);
			const float upper = std::clamp(-dy, 0.0f, 1.0f);
			const float lower = std::clamp(dy, 0.0f, 1.0f);

			const uint32_t seed = baseSeed ^ (0x85ebca6bu * static_cast<uint32_t>(i + 3));
			const float rand01 = hash01(seed);
			const float randSigned = rand01 * 2.0f - 1.0f;

			const float lick = 0.5f + 0.5f * std::sin(tFire + angle * 4.0f + rand01 * 7.0f);
			const float pulse = 0.5f + 0.5f * std::sin(tFire * 0.65f + rand01 * 11.0f);

			float radial = safeRadius;
			radial *= 1.0f + randSigned * jaggedness * 0.14f;
			radial *= 1.0f + upper * flameAmp * (0.35f + 0.65f * lick);
			radial *= 1.0f - lower * lowerCompress * (0.45f + 0.55f * pulse);

			const float sway = std::sin(tFire * 0.55f + rand01 * 8.0f + upper * 3.0f) *
				safeRadius * swayAmp * (0.25f + 0.75f * upper);
			const float lift = upper * safeRadius * (0.05f + 0.10f * lick);

			points.emplace_back(
				popupCenter.x + radial * dx + sway,
				popupCenter.y + radial * dy - lift);
		}

		return points;
	};

	auto buildSunDragonBubblePoints = [&](float baseRadius, float phaseOffset) {
		const int pointCount = std::clamp((std::max)(Config::AmmoWheel::PopupBlobPointCount, 24), 24, 56);
		const float jaggedness = std::clamp(Config::AmmoWheel::PopupBlobJaggedness, 0.0f, 0.45f);
		const float shimmerSpeed = std::clamp(Config::AmmoWheel::PopupBlobWobbleSpeed, 0.0f, 8.0f);
		const float tSun = static_cast<float>(ImGui::GetTime()) * (shimmerSpeed * 1.9f + 0.8f);
		const float safeRadius = (std::max)(baseRadius, 6.0f);
		const uint32_t baseSeed = entry.ammo ? entry.ammo->GetFormID() :
			static_cast<uint32_t>(_hoveredIndex + 1);
		const float coronaAmp = 0.12f + jaggedness * 0.38f;
		const float spikeAmp = 0.10f + jaggedness * 0.52f;
		const float plumeAmp = 0.07f + jaggedness * 0.30f;
		const float swirlAmp = 0.02f + jaggedness * 0.10f;
		const float lowerCompress = 0.04f + jaggedness * 0.12f;

		std::vector<ImVec2> points;
		points.reserve(pointCount);

		for (int i = 0; i < pointCount; ++i) {
			const float u = static_cast<float>(i) / static_cast<float>(pointCount);
			const float angle = u * (2.0f * IM_PI) + phaseOffset;
			const float dx = std::cos(angle);
			const float dy = std::sin(angle);
			const float upper = std::clamp(-dy, 0.0f, 1.0f);
			const float lower = std::clamp(dy, 0.0f, 1.0f);

			const uint32_t seed = baseSeed ^ (0x27d4eb2du * static_cast<uint32_t>(i + 11));
			const float rand01 = hash01(seed);
			const float randSigned = rand01 * 2.0f - 1.0f;

			const float corona = 0.5f + 0.5f * std::sin(angle * 10.0f + tSun * 1.2f + rand01 * 6.2831853f);
			const float spikeWave = std::sin(angle * 12.0f - tSun * 0.95f + rand01 * 4.0f);
			const float spikeMask = std::pow((std::max)(spikeWave, 0.0f), 2.4f);
			const float plumeWave = 0.5f + 0.5f * std::sin(tSun * 0.75f + angle * 3.0f + rand01 * 5.0f);
			const float plume = std::pow(upper, 1.15f) * plumeWave;

			float radial = safeRadius;
			radial *= 1.0f + randSigned * jaggedness * 0.10f;
			radial *= 1.0f + coronaAmp * (0.35f + 0.65f * corona);
			radial *= 1.0f + spikeAmp * spikeMask;
			radial *= 1.0f + plumeAmp * plume;
			radial *= 1.0f - lower * lowerCompress;

			const float swirl = std::sin(tSun * 0.7f + angle * 1.8f + rand01 * 5.0f) *
				safeRadius * swirlAmp * (0.2f + 0.8f * upper);
			const float lift = plume * safeRadius * (0.04f + 0.08f * plumeWave);

			points.emplace_back(
				popupCenter.x + radial * dx + swirl,
				popupCenter.y + radial * dy - lift);
		}

		return points;
	};

	auto drawSunDragonFlareRays = [&](float baseRadius, float phaseOffset, float alphaScale) {
		const int rayCount = std::clamp((std::max)(Config::AmmoWheel::PopupBlobPointCount, 22), 22, 56);
		const float jaggedness = std::clamp(Config::AmmoWheel::PopupBlobJaggedness, 0.0f, 0.45f);
		const float wobbleSpeed = std::clamp(Config::AmmoWheel::PopupBlobWobbleSpeed, 0.0f, 8.0f);
		const float tSun = static_cast<float>(ImGui::GetTime()) * (wobbleSpeed * 2.1f + 0.9f) + phaseOffset;
		const float safeRadius = (std::max)(baseRadius, 6.0f);
		const uint32_t baseSeed = entry.ammo ? entry.ammo->GetFormID() :
			static_cast<uint32_t>(_hoveredIndex + 1);
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		for (int i = 0; i < rayCount; ++i) {
			const float u = static_cast<float>(i) / static_cast<float>(rayCount);
			const float angle = u * (2.0f * IM_PI) + phaseOffset * 0.35f;
			const float dx = std::cos(angle);
			const float dy = std::sin(angle);
			const float upper = std::clamp(-dy, 0.0f, 1.0f);
			const float sideBias = 0.55f + 0.45f * std::abs(std::cos(angle * 0.5f));

			const uint32_t seed = baseSeed ^ (0x165667b1u * static_cast<uint32_t>(i + 5));
			const float rand01 = hash01(seed);
			const float pulse = 0.5f + 0.5f * std::sin(tSun + angle * 6.0f + rand01 * 5.0f);
			const float beam = std::pow((std::max)(std::sin(angle * 6.0f - tSun * 0.82f + rand01 * 4.0f), 0.0f), 1.8f);

			const float tipLength = safeRadius * (0.08f + 0.18f * beam * (0.35f + 0.65f * upper * sideBias));
			const float tipRadius = safeRadius + tipLength + (2.0f + 8.0f * pulse);
			const float baseInner = safeRadius * (0.88f + 0.08f * pulse);
			const float rayWidth = safeRadius * (0.015f + 0.020f * (0.35f + 0.65f * beam));

			const ImVec2 dir(dx, dy);
			const ImVec2 perp(-dy, dx);
			const ImVec2 p0(popupCenter.x + dir.x * baseInner, popupCenter.y + dir.y * baseInner);
			const ImVec2 tip(popupCenter.x + dir.x * tipRadius, popupCenter.y + dir.y * tipRadius);
			const ImVec2 p1(tip.x + perp.x * rayWidth, tip.y + perp.y * rayWidth);
			const ImVec2 p2(tip.x - perp.x * rayWidth, tip.y - perp.y * rayWidth);

			const int aRay = static_cast<int>(std::clamp((24.0f + 78.0f * beam) * alpha * alphaScale, 0.0f, 255.0f));
			const ImU32 rayColor = (i % 3 == 0)
				? toneSunDragonColor(255, 214, 112, aRay)
				: toneSunDragonColor(255, 156, 48, aRay);
			drawList->AddTriangleFilled(p0, p1, p2, rayColor);
		}
	};
	
	// Draw the circular bubble background
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
		ImU32 bgColor = Config::AmmoWheel::PopupUseCustomColor
			? Config::AmmoWheel::PopupBackgroundColor
			: (Config::AmmoWheel::UseSkyrimTheme
				? Config::AmmoWheel::SkyrimTheme::BgDarkLayer
				: IM_COL32(20, 15, 10, 220));
		uint8_t baseBgA = static_cast<uint8_t>(bgColor >> 24);
		uint8_t bgAlpha = static_cast<uint8_t>(baseBgA * popupOpacityMult * alpha);
		bgColor = (bgColor & 0x00FFFFFF) | (bgAlpha << 24);

		switch (popupShapeMode) {
		case 1:  // Circle
			ImGui::GetWindowDrawList()->AddCircleFilled(popupCenter, animatedRadius, bgColor, 48);
			break;
		case 3:  // Organic blob
		{
			auto organicPoints = buildOrganicBubblePoints(animatedRadius, 0.0f);
			if (!organicPoints.empty()) {
				ImGui::GetWindowDrawList()->AddConvexPolyFilled(
					organicPoints.data(),
					static_cast<int>(organicPoints.size()),
					bgColor);
			}
			break;
		}
		case 4:  // Dragon-fire blob
		{
			auto firePoints = buildDragonFireBubblePoints(animatedRadius, 0.0f);
			if (!firePoints.empty()) {
				ImU32 fireFillColor = bgColor;
				if (!Config::AmmoWheel::PopupUseCustomColor) {
					uint8_t a = static_cast<uint8_t>(fireFillColor >> 24);
					fireFillColor = IM_COL32(60, 18, 10, a);
				}
				ImGui::GetWindowDrawList()->AddConvexPolyFilled(
					firePoints.data(),
					static_cast<int>(firePoints.size()),
					fireFillColor);
			}
			break;
		}
		case 5:  // Sun-dragon blob
		{
			auto sunPoints = buildSunDragonBubblePoints(animatedRadius, 0.0f);
			if (!sunPoints.empty()) {
				ImU32 sunFillColor = bgColor;
				if (!Config::AmmoWheel::PopupUseCustomColor) {
					uint8_t a = static_cast<uint8_t>(sunFillColor >> 24);
					sunFillColor = toneSunDragonColor(96, 52, 10, a);
				}
				ImGui::GetWindowDrawList()->AddConvexPolyFilled(
					sunPoints.data(),
					static_cast<int>(sunPoints.size()),
					sunFillColor);

				// Bright inner core for sun-fire look.
				auto innerCorePoints = buildSunDragonBubblePoints((std::max)(animatedRadius * 0.80f, 4.0f), 0.31f);
				if (!innerCorePoints.empty()) {
					uint8_t coreA = static_cast<uint8_t>(std::clamp(static_cast<int>(sunFillColor >> 24) + 20, 0, 255));
					ImU32 coreColor = Config::AmmoWheel::PopupUseCustomColor
						? ((sunFillColor & 0x00FFFFFF) | (coreA << 24))
						: toneSunDragonColor(196, 124, 24, coreA);
					ImGui::GetWindowDrawList()->AddConvexPolyFilled(
						innerCorePoints.data(),
						static_cast<int>(innerCorePoints.size()),
						coreColor);
				}
			}
			break;
		}
		case 2:  // Rounded rectangle
		default:
		{
			ImVec2 popupMin{};
			ImVec2 popupMax{};
			calcPopupRectBounds(popupMin, popupMax);
			ImGui::GetWindowDrawList()->AddRectFilled(popupMin, popupMax, bgColor, bubbleRadius * 0.3f);
			break;
		}
		}
	}
	
	// Add dark vignette ring for text visibility (before border rings)
	// This ensures text near the edge is readable even when overlapping transparent flipbook borders
	ImU32 vignetteColor = IM_COL32(10, 8, 5, static_cast<int>(180 * alpha));
	switch (popupShapeMode) {
	case 1:  // Circle
		ImGui::GetWindowDrawList()->AddCircle(popupCenter, animatedRadius - 8.0f, vignetteColor, 48, 16.0f);
		break;
	case 3:  // Organic blob
	{
		auto vignettePoints = buildOrganicBubblePoints((std::max)(animatedRadius - 10.0f, 4.0f), 0.12f);
		if (!vignettePoints.empty()) {
			ImGui::GetWindowDrawList()->AddPolyline(
				vignettePoints.data(),
				static_cast<int>(vignettePoints.size()),
				vignetteColor,
				true,
				10.0f);
		}
		break;
	}
	case 4:  // Dragon-fire blob
	{
		auto fireVignette = buildDragonFireBubblePoints((std::max)(animatedRadius - 9.0f, 4.0f), 0.12f);
		if (!fireVignette.empty()) {
			ImGui::GetWindowDrawList()->AddPolyline(
				fireVignette.data(),
				static_cast<int>(fireVignette.size()),
				vignetteColor,
				true,
				8.0f);
		}
		break;
	}
	case 5:  // Sun-dragon blob
	{
		auto sunVignette = buildSunDragonBubblePoints((std::max)(animatedRadius - 8.0f, 4.0f), 0.18f);
		if (!sunVignette.empty()) {
			ImU32 sunVignetteColor = toneSunDragonColor(120, 74, 16, static_cast<int>(170 * alpha));
			ImGui::GetWindowDrawList()->AddPolyline(
				sunVignette.data(),
				static_cast<int>(sunVignette.size()),
				sunVignetteColor,
				true,
				9.0f);
		}
		break;
	}
	case 2:  // Rounded rectangle
	default:
	{
		ImVec2 popupMin{};
		ImVec2 popupMax{};
		calcPopupRectBounds(popupMin, popupMax);
		ImGui::GetWindowDrawList()->AddRect(
			ImVec2(popupMin.x + 8, popupMin.y + 8),
			ImVec2(popupMax.x - 8, popupMax.y - 8),
			vignetteColor,
			bubbleRadius * 0.3f,
			0,
			16.0f);
		break;
	}
	}
	
	bool drewPopupRim = false;
	bool drewPopupGlow = false;
	if (reskinSystem.IsEnabled() && entry.reskinEntry.preset) {
		AmmoWheelReskinUnified::DrawContext rimCtx{};
		rimCtx.center = popupCenter;
		rimCtx.radius = animatedRadius;
		rimCtx.slotAngleRad = 0.0f;
		rimCtx.alphaMult = alpha;
		rimCtx.slotIndex = _hoveredIndex;
		rimCtx.formID = entry.ammo ? entry.ammo->GetFormID() : 0;
		rimCtx.hovered = true;
		rimCtx.selected = false;
		rimCtx.active = false;
		drewPopupRim = reskinSystem.DrawTarget(
			AmmoWheelReskinUnified::VisualTarget::PopupBubbleRim,
			entry.reskinEntry,
			rimCtx,
			ImGui::GetWindowDrawList());
		drewPopupGlow = reskinSystem.DrawTarget(
			AmmoWheelReskinUnified::VisualTarget::PopupBubbleGlow,
			entry.reskinEntry,
			rimCtx,
			ImGui::GetWindowDrawList());
	}

	// Draw popup rim/glow fallback layers when reskin assets are not available.
	if (popupShapeMode == 1) {
		if (!drewPopupRim) {
			ImU32 borderColor = IM_COL32(218, 165, 32, static_cast<int>(200 * alpha));
			ImGui::GetWindowDrawList()->AddCircle(popupCenter, animatedRadius, borderColor, 48, 2.5f);
		}
		if (!drewPopupGlow) {
			ImU32 glowColor = IM_COL32(255, 215, 0, static_cast<int>(60 * alpha));
			ImGui::GetWindowDrawList()->AddCircle(popupCenter, animatedRadius - 4.0f, glowColor, 48, 1.5f);
		}
	} else if (popupShapeMode == 3) {
		if (!drewPopupRim) {
			ImU32 borderColor = IM_COL32(208, 168, 98, static_cast<int>(190 * alpha));
			auto borderPoints = buildOrganicBubblePoints(animatedRadius, 0.18f);
			if (!borderPoints.empty()) {
				ImGui::GetWindowDrawList()->AddPolyline(
					borderPoints.data(),
					static_cast<int>(borderPoints.size()),
					borderColor,
					true,
					2.5f);
			}
		}
		if (!drewPopupGlow) {
			ImU32 glowColor = IM_COL32(255, 215, 140, static_cast<int>(70 * alpha));
			auto glowPoints = buildOrganicBubblePoints((std::max)(animatedRadius - 4.0f, 3.0f), 0.34f);
			if (!glowPoints.empty()) {
				ImGui::GetWindowDrawList()->AddPolyline(
					glowPoints.data(),
					static_cast<int>(glowPoints.size()),
					glowColor,
					true,
					1.6f);
			}
		}
	} else if (popupShapeMode == 4) {
		const float tFire = static_cast<float>(ImGui::GetTime()) * (std::clamp(Config::AmmoWheel::PopupBlobWobbleSpeed, 0.0f, 8.0f) * 2.0f + 0.8f);
		const float flicker = 0.5f + 0.5f * std::sin(tFire);
		if (!drewPopupRim) {
			ImU32 borderColor = IM_COL32(255, 120, 32, static_cast<int>((160.0f + 70.0f * flicker) * alpha));
			auto borderPoints = buildDragonFireBubblePoints(animatedRadius, 0.22f);
			if (!borderPoints.empty()) {
				ImGui::GetWindowDrawList()->AddPolyline(
					borderPoints.data(),
					static_cast<int>(borderPoints.size()),
					borderColor,
					true,
					2.8f);
			}
		}
		if (!drewPopupGlow) {
			ImU32 glowColor = IM_COL32(255, 190, 80, static_cast<int>((95.0f + 80.0f * flicker) * alpha));
			auto glowPoints = buildDragonFireBubblePoints((std::max)(animatedRadius - 3.0f, 3.0f), 0.38f);
			if (!glowPoints.empty()) {
				ImGui::GetWindowDrawList()->AddPolyline(
					glowPoints.data(),
					static_cast<int>(glowPoints.size()),
					glowColor,
					true,
					1.8f);
			}
			ImU32 emberColor = IM_COL32(255, 80, 20, static_cast<int>((45.0f + 45.0f * flicker) * alpha));
			auto emberPoints = buildDragonFireBubblePoints((std::max)(animatedRadius - 7.0f, 2.0f), 0.55f);
			if (!emberPoints.empty()) {
				ImGui::GetWindowDrawList()->AddPolyline(
					emberPoints.data(),
					static_cast<int>(emberPoints.size()),
					emberColor,
					true,
					1.2f);
			}
		}
	} else if (popupShapeMode == 5) {
		const float tSun = static_cast<float>(ImGui::GetTime()) * (std::clamp(Config::AmmoWheel::PopupBlobWobbleSpeed, 0.0f, 8.0f) * 1.8f + 0.7f);
		const float flare = 0.5f + 0.5f * std::sin(tSun);
		if (!drewPopupGlow) {
			drawSunDragonFlareRays(animatedRadius, 0.22f, 1.0f);
		}
		if (!drewPopupRim) {
			ImU32 borderColor = toneSunDragonColor(255, 216, 108, static_cast<int>((170.0f + 72.0f * flare) * alpha));
			auto borderPoints = buildSunDragonBubblePoints(animatedRadius, 0.24f);
			if (!borderPoints.empty()) {
				ImGui::GetWindowDrawList()->AddPolyline(
					borderPoints.data(),
					static_cast<int>(borderPoints.size()),
					borderColor,
					true,
					3.0f);
			}
		}
		if (!drewPopupGlow) {
			ImU32 glowColor = toneSunDragonColor(255, 242, 156, static_cast<int>((102.0f + 96.0f * flare) * alpha));
			auto glowPoints = buildSunDragonBubblePoints((std::max)(animatedRadius - 3.0f, 3.0f), 0.40f);
			if (!glowPoints.empty()) {
				ImGui::GetWindowDrawList()->AddPolyline(
					glowPoints.data(),
					static_cast<int>(glowPoints.size()),
					glowColor,
					true,
					2.0f);
			}
			ImU32 coronaColor = toneSunDragonColor(255, 174, 52, static_cast<int>((64.0f + 62.0f * flare) * alpha));
			auto coronaPoints = buildSunDragonBubblePoints((std::max)(animatedRadius - 8.0f, 2.0f), 0.56f);
			if (!coronaPoints.empty()) {
				ImGui::GetWindowDrawList()->AddPolyline(
					coronaPoints.data(),
					static_cast<int>(coronaPoints.size()),
					coronaColor,
					true,
					1.5f);
			}
		}
	} else {
		if (!drewPopupRim) {
			ImVec2 popupMin{};
			ImVec2 popupMax{};
			calcPopupRectBounds(popupMin, popupMax);
			ImU32 borderColor = IM_COL32(200, 160, 50, static_cast<int>(180 * alpha));
			ImGui::GetWindowDrawList()->AddRect(popupMin, popupMax, borderColor, bubbleRadius * 0.3f, 0, 2.0f);
		}
	}
	
	// Apply padding to the content area.
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

float AmmoWheel::getSlotCenterAngle(int a_index) const
{
	const int numEntries = static_cast<int>(_ammoEntries.size());
	if (numEntries <= 0) {
		return getStartAngleRad();
	}

	const int clampedIndex = std::clamp(a_index, 0, numEntries - 1);
	const float arcAngle = getArcAngleRad();
	const float startAngle = getStartAngleRad();
	const float slotAngle = arcAngle / static_cast<float>(numEntries);
	const float slotGapRad = Config::AmmoWheel::SlotGapDeg * (IM_PI / 180.0f);
	const float effectiveSlotAngle = slotAngle - slotGapRad;
	const float slotStartAngle = startAngle + slotAngle * static_cast<float>(clampedIndex) + slotGapRad * 0.5f;
	return slotStartAngle + effectiveSlotAngle * 0.5f;
}

int AmmoWheel::stepHoveredIndex(int a_currentIndex, int a_delta) const
{
	const int numEntries = static_cast<int>(_ammoEntries.size());
	if (numEntries <= 0) {
		return -1;
	}

	int next = a_currentIndex;
	if (next < 0 || next >= numEntries) {
		next = 0;
	}
	next += a_delta;

	const bool wrap = getArcAngleRad() >= (2.0f * IM_PI * 0.95f);
	if (wrap) {
		next %= numEntries;
		if (next < 0) {
			next += numEntries;
		}
		return next;
	}

	return std::clamp(next, 0, numEntries - 1);
}

void AmmoWheel::processPendingMouseMotion(float a_dt)
{
	const int numEntries = static_cast<int>(_ammoEntries.size());
	const ImVec2 rawDelta = _mouseAccumulatedDelta;
	float rawMagnitude = _mouseAccumulatedPeak;
	const float accumulatedMagnitude = std::sqrt(rawDelta.x * rawDelta.x + rawDelta.y * rawDelta.y);
	rawMagnitude = (std::max)(rawMagnitude, accumulatedMagnitude);

	_mouseAccumulatedDelta = { 0.0f, 0.0f };
	_mouseAccumulatedPeak = 0.0f;

	// Keep the visual focus cursor animated toward the hovered slot even while idle.
	if (numEntries <= 0) {
		_mouseSlotCarry = 0.0f;
		_mouseStepLatchDirection = 0;
		return;
	}

	ImVec2 filtered = _mouseFilter.Apply(rawDelta, a_dt, true);

	// With very few slots the angular distance is large. Boost drive modestly so sparse
	// wheels still step predictably without lowering the detent threshold too far.
	const float slotAngle = getArcAngleRad() / static_cast<float>(numEntries);
	constexpr float kReferenceSlotAngle = (2.0f * IM_PI) / 10.0f;
	const float ratio = slotAngle / kReferenceSlotAngle;
	if (ratio > 1.0f) {
		float gain = 1.0f + (ratio - 1.0f) * 0.65f;
		if (numEntries <= 4) {
			gain += (4.0f - static_cast<float>(numEntries)) * 0.25f;
		}
		gain = std::clamp(gain, 1.0f, 3.5f);
		filtered.x *= gain;
		filtered.y *= gain;
	}

	if (rawMagnitude > _mouseFilter.deadzone) {
		_hoverInputLock = false;
	}

	if (_hoverInputLock) {
		_mouseSlotCarry = 0.0f;
		_mouseStepLatchDirection = 0;
		syncCursorToHoveredSlot(a_dt, false);
		return;
	}

	int currentHoveredIndex = _hoveredIndex;
	if (currentHoveredIndex < 0 || currentHoveredIndex >= numEntries) {
		currentHoveredIndex = FindInitialHoverIndex();
	}
	if (currentHoveredIndex < 0 || currentHoveredIndex >= numEntries) {
		syncCursorToHoveredSlot(a_dt, false);
		return;
	}

	if (rawMagnitude <= _mouseFilter.deadzone) {
		const float settleBlend = 1.0f - std::exp(-16.0f * a_dt);
		_mouseSlotCarry += (0.0f - _mouseSlotCarry) * settleBlend;
		_mouseStepLatchDirection = 0;
		if (std::abs(_mouseSlotCarry) < 0.01f) {
			_mouseSlotCarry = 0.0f;
		}
		syncCursorToHoveredSlot(a_dt, false);
		return;
	}

	const float baseAngle = getSlotCenterAngle(currentHoveredIndex);
	const ImVec2 tangent = { -std::sin(baseAngle), std::cos(baseAngle) };
	const float rawTangential = rawDelta.x * tangent.x + rawDelta.y * tangent.y;
	const float filteredTangential = filtered.x * tangent.x + filtered.y * tangent.y;
	const float targetRadius = Config::AmmoWheel::WheelRadius * 0.8f;

	float angularDrive = 0.0f;
	const ImVec2 baseCursor = { targetRadius * std::cos(baseAngle), targetRadius * std::sin(baseAngle) };
	const ImVec2 movedCursor = { baseCursor.x + rawDelta.x, baseCursor.y + rawDelta.y };
	const float movedRadius = std::sqrt(movedCursor.x * movedCursor.x + movedCursor.y * movedCursor.y);
	if (movedRadius > 0.001f) {
		const float cross = baseCursor.x * movedCursor.y - baseCursor.y * movedCursor.x;
		const float dot = baseCursor.x * movedCursor.x + baseCursor.y * movedCursor.y;
		const float angularDelta = std::atan2(cross, dot);
		if (std::isfinite(angularDelta)) {
			angularDrive = angularDelta * targetRadius;
		}
	}

	// Filtered drive keeps transitions calm; a small raw assist preserves immediacy.
	float tangentialDrive = filteredTangential * 0.75f + rawTangential * 0.25f;
	if (!std::isfinite(tangentialDrive)) {
		tangentialDrive = 0.0f;
	}

	float signedDrive = tangentialDrive;
	const bool sparseWheel = numEntries <= 5;
	if (sparseWheel && std::isfinite(angularDrive)) {
		if (std::abs(angularDrive) > std::abs(signedDrive)) {
			signedDrive = angularDrive;
		} else if (signedDrive != 0.0f && ((signedDrive > 0.0f) == (angularDrive > 0.0f))) {
			signedDrive = signedDrive * 0.7f + angularDrive * 0.3f;
		}
	}

	if (std::abs(signedDrive) > 0.0001f) {
		if (_mouseSlotCarry != 0.0f && ((_mouseSlotCarry > 0.0f) != (signedDrive > 0.0f))) {
			_mouseSlotCarry = 0.0f;
		}

		float detentThreshold = targetRadius * slotAngle * (sparseWheel ? 0.16f : 0.24f);
		detentThreshold = sparseWheel
			? std::clamp(detentThreshold, 10.0f, 22.0f)
			: std::clamp(detentThreshold, 12.0f, 30.0f);

		if (sparseWheel && _mouseStepLatchDirection != 0) {
			const float latchSignedDrive = signedDrive * static_cast<float>(_mouseStepLatchDirection);
			if (latchSignedDrive > 0.0f) {
				_mouseSlotCarry = 0.0f;
				syncCursorToHoveredSlot(a_dt, false);
				return;
			}
			_mouseStepLatchDirection = 0;
		}

		const float maxCarry = detentThreshold * 1.15f;
		_mouseSlotCarry = std::clamp(_mouseSlotCarry + signedDrive, -maxCarry, maxCarry);

		if (std::abs(_mouseSlotCarry) >= detentThreshold) {
			const int direction = (_mouseSlotCarry > 0.0f) ? 1 : -1;
			_mousePendingHoverIndex = stepHoveredIndex(currentHoveredIndex, direction);
			_mouseSlotCarry = 0.0f;
			_mouseStepLatchDirection = sparseWheel ? direction : 0;
		}
	}

	syncCursorToHoveredSlot(a_dt, false);
}

void AmmoWheel::syncCursorToHoveredSlot(float a_dt, bool a_immediate)
{
	int targetIndex = _mousePendingHoverIndex;
	if (targetIndex < 0 || targetIndex >= static_cast<int>(_ammoEntries.size())) {
		targetIndex = _hoveredIndex;
	}
	if (targetIndex < 0 || targetIndex >= static_cast<int>(_ammoEntries.size())) {
		return;
	}

	const float targetAngle = getSlotCenterAngle(targetIndex);
	const float targetRadius = Config::AmmoWheel::WheelRadius * 0.8f;
	float currentAngle = targetAngle;
	float currentRadius = targetRadius;

	const float currentLen = std::sqrt(_cursorPos.x * _cursorPos.x + _cursorPos.y * _cursorPos.y);
	if (currentLen > 0.001f) {
		currentAngle = std::atan2(_cursorPos.y, _cursorPos.x);
		currentRadius = currentLen;
	}

	if (!a_immediate && Config::AmmoWheel::SmoothSlotTransition) {
		auto wrapSignedAngle = [](float value) -> float {
			while (value <= -IM_PI) {
				value += 2.0f * IM_PI;
			}
			while (value > IM_PI) {
				value -= 2.0f * IM_PI;
			}
			return value;
		};

		// Keep the slot-to-slot slide visible, but trim the follow lag so popup/focus
		// reaches the new slot sooner after a mouse step commit.
		const float blend = 1.0f - std::exp(-24.0f * a_dt);
		const float deltaAngle = wrapSignedAngle(targetAngle - currentAngle);
		currentAngle += deltaAngle * blend;
		currentRadius += (targetRadius - currentRadius) * blend;
	} else {
		currentAngle = targetAngle;
		currentRadius = targetRadius;
	}

	_cursorPos.x = currentRadius * std::cos(currentAngle);
	_cursorPos.y = currentRadius * std::sin(currentAngle);
}

float AmmoWheel::getCursorMaxRadius() const
{
	float maxRadius = Config::AmmoWheel::WheelRadius * 1.5f;
	const int numEntries = static_cast<int>(_ammoEntries.size());
	if (numEntries > 0 && numEntries <= 4) {
		// Keep sparse-wheel cursor on a tighter ring to reduce required travel.
		const float sparseMaxRadius =
			Config::AmmoWheel::WheelRadius * (0.65f + 0.08f * static_cast<float>(numEntries));
		maxRadius = (std::min)(maxRadius, sparseMaxRadius);
	}
	return maxRadius;
}

void AmmoWheel::syncGamepadFilterToCursor()
{
	const float maxRadius = getCursorMaxRadius();
	if (maxRadius <= 0.001f) {
		_gamepadFilter.Reset();
		return;
	}

	_gamepadFilter.smoothedPos = {
		std::clamp(_cursorPos.x / maxRadius, -1.0f, 1.0f),
		std::clamp(-_cursorPos.y / maxRadius, -1.0f, 1.0f)
	};
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
	// Don't switch slots unless the new candidate is significantly closer than the previous.
	// Mouse feels better at the exact geometric midpoint; keep the extra stickiness for gamepad only.
	const float hysteresisDeg = (_lastCursorInputSource == CursorInputSource::Gamepad)
		? Config::AmmoWheel::GamepadHoverHysteresisDeg
		: 0.0f;
	float hysteresisRad = hysteresisDeg * (IM_PI / 180.0f);
	
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

// ========== Adaptive center panel positioning ==========
ImVec2 AmmoWheel::calculateCenterPanelPosition(ImVec2 a_wheelCenter) const
{
	float arcSpan = getArcAngleRad();
	float arcStart = getStartAngleRad();
	float arcMid = arcStart + arcSpan * 0.5f;
	
	// For full circle, center is optimal
	if (arcSpan >= 2.0f * IM_PI * 0.95f) {
		return a_wheelCenter;
	}
	
	// For partial arcs, bias panel toward the arc's usable area.
	float insetFactor = std::clamp(Config::AmmoWheel::CenterPanelInsetRatio, 0.0f, 1.0f);
	float pushDistance = _cachedInnerRadius * insetFactor;
	
	// Push panel toward arc midpoint (into the visible arc area)
	ImVec2 biasedCenter = {
		a_wheelCenter.x + pushDistance * std::cos(arcMid),
		a_wheelCenter.y + pushDistance * std::sin(arcMid)
	};
	
	// Clamp to viewport safe area
	ImVec2 viewport = ResolutionScale::Context::GetSingleton().GetRenderSize();
	float margin = Config::AmmoWheel::CenterPanelSafeMargin;
	
	// Estimate panel dimensions for clamping. Reuse stable dimensions when available.
	float panelWidth = _cachedInnerRadius * Config::AmmoWheel::CenterMaxWidthRatio * 2.0f;
	if (std::isfinite(_centerPanelStableWidth) && _centerPanelStableWidth > 1.0f) {
		panelWidth = _centerPanelStableWidth;
	}
	float panelHeight = Config::AmmoWheel::CenterFontPx * 5.0f;
	if (std::isfinite(_centerPanelStableHeight) && _centerPanelStableHeight > 1.0f) {
		panelHeight = _centerPanelStableHeight;
	}
	
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

// ========== Multi-line text wrapping for slot labels ==========
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

// ========== Edge-aware word wrapping for center panel text ==========
TextLayout AmmoWheel::wrapTextForCenterPanel(const char* text, float fontSize, ImVec2 panelCenter, int maxLinesOverride) const
{
	TextLayout result;
	(void)panelCenter;
	
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
	
	// Compute wrap width from panel geometry limits so edge-positioned half wheels remain stable.
	ImVec2 viewport = ResolutionScale::Context::GetSingleton().GetRenderSize();
	float safeMargin = Config::AmmoWheel::WrapSafeMarginPx;
	const float panelMaxWidth = (std::max)(
		1.0f,
		_cachedInnerRadius * Config::AmmoWheel::CenterMaxWidthRatio * 2.0f *
			std::clamp(Config::AmmoWheel::CenterTextMaxWidthRatio, 0.1f, 1.0f));

	// Apply optional explicit width cap from config.
	float configMaxWidth = Config::AmmoWheel::WrapMaxLineWidthPx;
	if (configMaxWidth <= 0.0f) {
		configMaxWidth = viewport.x * Config::AmmoWheel::WrapMaxLineWidthRatio;
	}
	const float viewportMaxWidth = (std::max)(32.0f, viewport.x - safeMargin * 2.0f);
	float maxWidth = (std::min)((std::min)(panelMaxWidth, configMaxWidth), viewportMaxWidth);
	maxWidth = (std::max)(maxWidth, 32.0f);
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
	
	int maxLines = (std::max)(1, Config::AmmoWheel::WrapMaxLines);
	if (maxLinesOverride > 0) {
		maxLines = maxLinesOverride;
	} else if (Config::AmmoWheel::CenterTextMaxLines > 0) {
		maxLines = (std::min)(maxLines, Config::AmmoWheel::CenterTextMaxLines);
	}
	bool addHyphen = Config::AmmoWheel::AddWrapHyphen;
	bool preferWordBoundary = Config::AmmoWheel::WrapAtWordBoundary;
	bool ellipsisEnabled = Config::AmmoWheel::CenterTextEllipsisEnabled;
	
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
				if (ellipsisEnabled) {
					currentLine = TruncateTextToFit(currentLine.c_str(), maxWidth - 20.0f, fontSize) + "...";
				} else {
					currentLine = TruncateTextToFit(currentLine.c_str(), maxWidth, fontSize);
				}
			}
		}
		result.lines.push_back(currentLine);
	}
	
	result.totalHeight = result.lines.size() * fontSize * 1.2f;
	return result;
}

// ========== Clamp cursor angle to arc bounds for half-wheel mode ==========
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

// ========== Reset navigation filters ==========
void AmmoWheel::ResetNavigationFilters()
{
	_mouseFilter.Reset();
	_gamepadFilter.Reset();
	_cursorPos = {0, 0};
	_mousePendingHoverIndex = -1;
	_mouseAccumulatedDelta = { 0.0f, 0.0f };
	_mouseAccumulatedPeak = 0.0f;
	_mouseSlotCarry = 0.0f;
	_mouseStepLatchDirection = 0;
	
	if (Config::AmmoWheel::DebugLogNavigation) {
		logger::info("[AmmoWheel] Navigation filters reset");
	}
}

// ========== Mouse button handling: block attack when the wheel is open ==========
bool AmmoWheel::HandleMouseButton(int button, bool pressed, bool fromGamepad)
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

		if (!pressed) {
			if (fromGamepad && _pendingCloseOnReleaseButton == button) {
				_pendingCloseOnReleaseButton = -1;
				Close();
			}
			return true;  // Consume release to keep the bound gameplay action local to AmmoWheel
		}
		
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
			
			// For gamepad, defer close until the button release so vanilla shout/power
			// bindings on the same shoulder button never see a free release edge.
			if (Config::AmmoWheel::CloseOnSelection) {
				if (fromGamepad) {
					_pendingCloseOnReleaseButton = button;
				} else {
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
					InventorySnapshotCache::UnequipObject(equipManager, player, currentAmmo);
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
		audioManager->GetSoundHandleByName(handle, editorID, 0x10);
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
