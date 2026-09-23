#pragma once
#include <cstdint>
#include <string_view>

#include <fmt/format.h>

namespace MainWheelDebug
{
	enum class Category : std::uint8_t
	{
		OpenClose,
		Config,
		Input,
		Scaling,
		Clamp,
		Indicators,
		ReskinResolve,
		Assets,
		Perf,
		MouseHover,  // Mouse hover stability guards (center lock, jump guard, hysteresis)
		COUNT
	};

	bool IsEnabled();
	bool IsCategoryEnabled(Category category);
	bool IsVerbose();
	std::uint32_t RateLimitMs();
	const char* GetCategoryName(Category category);
	bool ShouldLogRateLimited(Category category, std::string_view tag);

	template <typename... Args>
	inline void Log(Category category, fmt::format_string<Args...> fmtStr, Args&&... args)
	{
		if (!IsCategoryEnabled(category)) {
			return;
		}
		logger::info("MainWheel[{}]: {}", GetCategoryName(category),
			fmt::format(fmtStr, std::forward<Args>(args)...));
	}

	template <typename... Args>
	inline bool LogRateLimited(Category category, std::string_view tag, fmt::format_string<Args...> fmtStr, Args&&... args)
	{
		if (!IsCategoryEnabled(category)) {
			return false;
		}
		if (!ShouldLogRateLimited(category, tag)) {
			return false;
		}
		logger::info("MainWheel[{}]: {}", GetCategoryName(category),
			fmt::format(fmtStr, std::forward<Args>(args)...));
		return true;
	}
}
