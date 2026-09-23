#include "MainWheelDebug.h"

#include <array>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

#include "bin/Config.h"

namespace
{
	using Clock = std::chrono::steady_clock;
	std::mutex s_rateLimitLock;
	std::unordered_map<std::string, Clock::time_point> s_lastLogTimes;

	bool IsCategoryAllowed(MainWheelDebug::Category category)
	{
		if (!Config::MainWheel::Debug::Enabled) {
			if (category == MainWheelDebug::Category::MouseHover) {
				return Config::MainWheel::Mouse::DebugHoverLog ||
				       Config::MainWheel::Mouse::LogMouseFeatures ||
				       Config::MainWheel::Mouse::LogCandidateScores ||
				       Config::MainWheel::Mouse::LogIntentState;
			}
			return false;
		}
		switch (category) {
		case MainWheelDebug::Category::OpenClose:
			return Config::MainWheel::Debug::LogOpenClose;
		case MainWheelDebug::Category::Config:
			return Config::MainWheel::Debug::LogConfig;
		case MainWheelDebug::Category::Input:
			return Config::MainWheel::Debug::LogInput;
		case MainWheelDebug::Category::Scaling:
			return Config::MainWheel::Debug::LogScaling;
		case MainWheelDebug::Category::Clamp:
			return Config::MainWheel::Debug::LogClamp;
		case MainWheelDebug::Category::Indicators:
			return Config::MainWheel::Debug::LogIndicators;
		case MainWheelDebug::Category::ReskinResolve:
			return Config::MainWheel::Debug::LogReskinResolve;
		case MainWheelDebug::Category::Assets:
			return Config::MainWheel::Debug::LogAssets;
	case MainWheelDebug::Category::Perf:
		return Config::MainWheel::Debug::LogPerf;
		case MainWheelDebug::Category::MouseHover:
			return Config::MainWheel::Mouse::DebugHoverLog ||
			       Config::MainWheel::Mouse::LogMouseFeatures ||
			       Config::MainWheel::Mouse::LogCandidateScores ||
			       Config::MainWheel::Mouse::LogIntentState;
		default:
			return false;
		}
	}
}

bool MainWheelDebug::IsEnabled()
{
	return Config::MainWheel::Debug::Enabled;
}

bool MainWheelDebug::IsCategoryEnabled(Category category)
{
	return IsCategoryAllowed(category);
}

bool MainWheelDebug::IsVerbose()
{
	return Config::MainWheel::Debug::Verbose;
}

std::uint32_t MainWheelDebug::RateLimitMs()
{
	return Config::MainWheel::Debug::RateLimitMs;
}

const char* MainWheelDebug::GetCategoryName(Category category)
{
	switch (category) {
	case Category::OpenClose:
		return "OpenClose";
	case Category::Config:
		return "Config";
	case Category::Input:
		return "Input";
	case Category::Scaling:
		return "Scaling";
	case Category::Clamp:
		return "Clamp";
	case Category::Indicators:
		return "Indicators";
	case Category::ReskinResolve:
		return "ReskinResolve";
	case Category::Assets:
		return "Assets";
	case Category::Perf:
		return "Perf";
	case Category::MouseHover:
		return "MouseHover";
	default:
		return "Unknown";
	}
}

bool MainWheelDebug::ShouldLogRateLimited(Category category, std::string_view tag)
{
	if (!IsCategoryEnabled(category)) {
		return false;
	}
	const std::uint32_t limitMs = RateLimitMs();
	if (limitMs == 0) {
		return true;
	}
	const auto now = Clock::now();
	std::string key;
	key.reserve(tag.size() + 16);
	key.append(GetCategoryName(category));
	key.push_back(':');
	key.append(tag.data(), tag.size());

	std::lock_guard lock(s_rateLimitLock);
	auto it = s_lastLogTimes.find(key);
	if (it != s_lastLogTimes.end()) {
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
		if (elapsed.count() < limitMs) {
			return false;
		}
		it->second = now;
		return true;
	}
	s_lastLogTimes.emplace(std::move(key), now);
	return true;
}
