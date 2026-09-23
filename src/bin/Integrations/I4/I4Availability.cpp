#include "I4Availability.h"

#include <array>
#include <filesystem>

#include "I4Log.h"
#include "bin/Config.h"

namespace I4Integration
{
	namespace
	{
		constexpr auto kRefreshInterval = std::chrono::seconds(2);

		struct InvokerMovieInfo
		{
			RE::GPtr<RE::GFxMovieView> movie{ nullptr };
			std::string menuName{ "none" };
			bool fromOpenMenu = false;
		};

		InvokerMovieInfo FindMovieForI4()
		{
			InvokerMovieInfo info{};
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				return info;
			}

			constexpr std::array<std::string_view, 4> menuNames = {
				RE::InventoryMenu::MENU_NAME,
				RE::MagicMenu::MENU_NAME,
				RE::FavoritesMenu::MENU_NAME,
				RE::HUDMenu::MENU_NAME
			};

			// Prefer currently open menus so extraction clip is actually rendered this frame.
			for (const auto name : menuNames) {
				if (!ui->IsMenuOpen(name)) {
					continue;
				}
				auto menu = ui->GetMenu(name);
				if (!menu || !menu->uiMovie) {
					continue;
				}
				info.movie = menu->uiMovie;
				info.menuName = name;
				info.fromOpenMenu = true;
				return info;
			}

			// Fallback: any loaded movie that exposes ProcessEntry (resolve-only use-case).
			for (const auto name : menuNames) {
				auto menu = ui->GetMenu(name);
				if (!menu) {
					continue;
				}
				if (!menu->uiMovie) {
					continue;
				}
				info.movie = menu->uiMovie;
				info.menuName = name;
				info.fromOpenMenu = false;
				return info;
			}

			return info;
		}
	}

	I4Availability& I4Availability::GetSingleton()
	{
		static I4Availability singleton;
		return singleton;
	}

	void I4Availability::Refresh() const
	{
		const auto now = std::chrono::steady_clock::now();
		if (now < _nextRefresh) {
			return;
		}
		_nextRefresh = now + kRefreshInterval;

		const auto applyStatus = [&](bool available,
			                       std::string_view status,
			                       RE::GPtr<RE::GFxMovieView> movie,
			                       std::string_view movieSource,
			                       bool fromOpenMenu) {
			const bool changed = _available != available ||
			                     _status != status ||
			                     _cachedMovie.get() != movie.get() ||
			                     _movieSource != movieSource ||
			                     _movieFromOpenMenu != fromOpenMenu;
			_available = available;
			_status = status;
			_cachedMovie = std::move(movie);
			_movieSource = movieSource;
			_movieFromOpenMenu = fromOpenMenu;
			if (changed && (Config::I4::DebugLog || Config::I4::TraceLog)) {
				I4_LOG_INFO("[I4][TRACE][availability] status='{}' available={} sourceMenu='{}' sourceKind={}",
					_status,
					_available,
					_movieSource,
					_movieFromOpenMenu ? "open" : "loaded");
			}
		};

		if (!Config::I4::Enabled) {
			applyStatus(false, "disabled", nullptr, "none", false);
			return;
		}

		if (!std::filesystem::exists("Data/SKSE/Plugins/InventoryInjector.dll")) {
			applyStatus(false, "plugin_dll_missing", nullptr, "none", false);
			return;
		}

		auto movieInfo = FindMovieForI4();
		if (!movieInfo.movie) {
			applyStatus(false, "no_scaleform_movie", nullptr, "none", false);
			return;
		}

		if (!movieInfo.movie->IsAvailable("skse.plugins.InventoryInjector.ProcessEntry")) {
			applyStatus(false, "process_entry_unavailable", nullptr, movieInfo.menuName, movieInfo.fromOpenMenu);
			return;
		}

		applyStatus(true, "available", movieInfo.movie, movieInfo.menuName, movieInfo.fromOpenMenu);
	}

	bool I4Availability::IsAvailable()
	{
		Refresh();
		return _available;
	}

	const std::string& I4Availability::GetStatusString() const
	{
		Refresh();
		return _status;
	}

	RE::GFxMovieView* I4Availability::GetInvokerMovie()
	{
		Refresh();
		return _cachedMovie.get();
	}

	void I4Availability::Invalidate()
	{
		_nextRefresh = std::chrono::steady_clock::time_point{};
		_cachedMovie = nullptr;
		_available = false;
		_status = "unchecked";
		_movieSource = "none";
		_movieFromOpenMenu = false;
	}
}
