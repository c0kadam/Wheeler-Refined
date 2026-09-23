#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "bin/Rendering/TextureManager.h"

namespace I4Integration
{
	struct I4IconSpec
	{
		bool valid = false;
		std::string iconSource;
		std::string iconLabel;
		ImU32 color = IM_COL32(255, 255, 255, 255);
	};

	struct I4IconResult
	{
		Texture::Image image{};
		ImU32 tint = IM_COL32(255, 255, 255, 255);
		bool usingI4 = false;
	};

	struct I4Stats
	{
		std::atomic<std::uint64_t> providerCalls{ 0 };
		std::atomic<std::uint64_t> resolveCalls{ 0 };
		std::atomic<std::uint64_t> resolveCacheHits{ 0 };
		std::atomic<std::uint64_t> renderCalls{ 0 };
		std::atomic<std::uint64_t> renderCacheHits{ 0 };
		std::atomic<std::uint64_t> renderFailures{ 0 };
		std::atomic<std::uint64_t> renderBuiltInHits{ 0 };
		std::atomic<std::uint64_t> renderOffscreenHits{ 0 };
		std::atomic<std::uint64_t> renderQueueEnqueued{ 0 };
		std::atomic<std::uint64_t> renderQueuePending{ 0 };
		std::atomic<std::uint64_t> displayedI4{ 0 };
		std::atomic<std::uint64_t> displayedFallback{ 0 };
		std::atomic<std::uint64_t> fallbackDisabled{ 0 };
		std::atomic<std::uint64_t> fallbackUnavailable{ 0 };
		std::atomic<std::uint64_t> fallbackCategoryFiltered{ 0 };
		std::atomic<std::uint64_t> fallbackNoSpec{ 0 };
		std::atomic<std::uint64_t> fallbackNoImage{ 0 };
	};

	I4Stats& GetStats();
	void ResetStats();
	bool ParseIconColorString(const std::string& value, ImU32& outColor);
}
