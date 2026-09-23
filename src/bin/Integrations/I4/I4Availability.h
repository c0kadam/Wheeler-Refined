#pragma once

#include <chrono>
#include <string>

namespace I4Integration
{
	class I4Availability
	{
	public:
		static I4Availability& GetSingleton();

		bool IsAvailable();
		const std::string& GetStatusString() const;
		RE::GFxMovieView* GetInvokerMovie();
		void Invalidate();

	private:
		void Refresh() const;

		mutable bool _available = false;
		mutable std::string _status = "unchecked";
		mutable std::chrono::steady_clock::time_point _nextRefresh{};
		mutable RE::GPtr<RE::GFxMovieView> _cachedMovie{ nullptr };
		mutable std::string _movieSource = "none";
		mutable bool _movieFromOpenMenu = false;
	};
}
