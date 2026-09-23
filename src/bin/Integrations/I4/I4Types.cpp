#include "I4Types.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace I4Integration
{
	namespace
	{
		I4Stats g_stats;
	}

	I4Stats& GetStats()
	{
		return g_stats;
	}

	void ResetStats()
	{
		g_stats.providerCalls.store(0, std::memory_order_relaxed);
		g_stats.resolveCalls.store(0, std::memory_order_relaxed);
		g_stats.resolveCacheHits.store(0, std::memory_order_relaxed);
		g_stats.renderCalls.store(0, std::memory_order_relaxed);
		g_stats.renderCacheHits.store(0, std::memory_order_relaxed);
		g_stats.renderFailures.store(0, std::memory_order_relaxed);
		g_stats.renderBuiltInHits.store(0, std::memory_order_relaxed);
		g_stats.renderOffscreenHits.store(0, std::memory_order_relaxed);
		g_stats.renderQueueEnqueued.store(0, std::memory_order_relaxed);
		g_stats.renderQueuePending.store(0, std::memory_order_relaxed);
		g_stats.displayedI4.store(0, std::memory_order_relaxed);
		g_stats.displayedFallback.store(0, std::memory_order_relaxed);
		g_stats.fallbackDisabled.store(0, std::memory_order_relaxed);
		g_stats.fallbackUnavailable.store(0, std::memory_order_relaxed);
		g_stats.fallbackCategoryFiltered.store(0, std::memory_order_relaxed);
		g_stats.fallbackNoSpec.store(0, std::memory_order_relaxed);
		g_stats.fallbackNoImage.store(0, std::memory_order_relaxed);
	}

	bool ParseIconColorString(const std::string& value, ImU32& outColor)
	{
		std::string s = value;
		s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) {
			return std::isspace(c) != 0;
		}), s.end());

		if (s.empty()) {
			return false;
		}

		if (s[0] == '#') {
			s.erase(s.begin());
		} else if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
			s.erase(0, 2);
		}

		if (s.size() != 6 && s.size() != 8) {
			return false;
		}

		for (char c : s) {
			if (!std::isxdigit(static_cast<unsigned char>(c))) {
				return false;
			}
		}

		unsigned long raw = std::strtoul(s.c_str(), nullptr, 16);
		std::uint8_t a = 0xFF;
		std::uint8_t r = 0;
		std::uint8_t g = 0;
		std::uint8_t b = 0;
		if (s.size() == 6) {
			r = static_cast<std::uint8_t>((raw >> 16) & 0xFF);
			g = static_cast<std::uint8_t>((raw >> 8) & 0xFF);
			b = static_cast<std::uint8_t>(raw & 0xFF);
		} else {
			a = static_cast<std::uint8_t>((raw >> 24) & 0xFF);
			r = static_cast<std::uint8_t>((raw >> 16) & 0xFF);
			g = static_cast<std::uint8_t>((raw >> 8) & 0xFF);
			b = static_cast<std::uint8_t>(raw & 0xFF);
		}

		outColor = IM_COL32(r, g, b, a);
		return true;
	}
}
