#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "I4Types.h"

namespace I4Integration
{
	class I4SwfIconRenderer
	{
	public:
		static I4SwfIconRenderer& GetSingleton();

		Texture::Image GetIcon(const I4IconSpec& spec, std::uint32_t requestedSizePx, bool allowExtraction = true);
		bool TryGetCachedIcon(const I4IconSpec& spec, std::uint32_t requestedSizePx, Texture::Image& outImage, bool& outPrecolored);
		bool IsPrecolored(const I4IconSpec& spec, std::uint32_t requestedSizePx);
		void ClearCache();

	private:
		struct RenderKey
		{
			std::string source;
			std::string label;
			ImU32 color = 0;
			std::uint32_t size = 0;

			bool operator==(const RenderKey& rhs) const
			{
				return source == rhs.source &&
				       label == rhs.label &&
				       color == rhs.color &&
				       size == rhs.size;
			}
		};

		struct RenderKeyHash
		{
			std::size_t operator()(const RenderKey& key) const
			{
				std::size_t h = std::hash<std::string>{}(key.source);
				h ^= std::hash<std::string>{}(key.label) + 0x9e3779b9 + (h << 6) + (h >> 2);
				h ^= std::hash<std::uint32_t>{}(key.color) + 0x9e3779b9 + (h << 6) + (h >> 2);
				h ^= std::hash<std::uint32_t>{}(key.size) + 0x9e3779b9 + (h << 6) + (h >> 2);
				return h;
			}
		};

		struct CacheEntry
		{
			Texture::Image image{};
			bool owned = false;
			bool precolored = false;
			std::uint64_t tick = 0;
		};

		struct PendingRequest
		{
			I4IconSpec spec{};
			std::uint32_t requestedSizePx = 0;
			std::uint32_t attempts = 0;
		};

		enum class JobStage : std::uint8_t
		{
			WaitLoad = 0,
			WaitRender
		};

		struct PendingJob
		{
			RenderKey key{};
			I4IconSpec spec{};
			std::uint32_t requestedSizePx = 0;
			std::uint32_t stageFrames = 0;

			float stageX = 0.0f;
			float stageY = 0.0f;
			float stageSizeX = 0.0f;
			float stageSizeY = 0.0f;

			std::uint32_t captureX = 0;
			std::uint32_t captureY = 0;
			std::uint32_t captureW = 0;
			std::uint32_t captureH = 0;

			RE::GPtr<RE::GFxMovieView> movie{ nullptr };
			RE::GFxValue containerClip{};
			RE::GFxValue iconClip{};
			RE::GFxValue iconLoader{};
			JobStage stage = JobStage::WaitLoad;
		};

		Texture::Image ResolveBuiltInIcon(const I4IconSpec& spec);
		Texture::Image TryRenderOffscreenDirect(const I4IconSpec& spec, std::uint32_t requestedSizePx) const;
		void ProcessExtractionJobs();
		void StartNextPendingJob();
		bool InitializeJob(PendingJob& job, const PendingRequest& request, const RenderKey& key);
		bool IsIconClipLoaded(const PendingJob& job) const;
		void PrepareLoadedIcon(PendingJob& job) const;
		Texture::Image CaptureJobTexture(const PendingJob& job) const;
		void FinishActiveJob(const Texture::Image& image, bool owned, bool failed, std::string_view reason);
		void CleanupJobClip(PendingJob& job) const;
		void CacheResult(const RenderKey& key, const Texture::Image& image, bool owned, bool precolored);
		void EvictCacheIfNeeded(std::uint32_t maxEntries);
		static std::string BuildInterfaceMoviePath(std::string_view source);
		static std::uint32_t ColorToRGB(ImU32 color);
		static RE::GRenderer::Cxform BuildColorTransform(std::uint32_t rgb);
		static void ApplyColorTransform(const RE::GFxValue& icon, std::uint32_t rgb);

		std::mutex _lock;
		std::unordered_map<RenderKey, CacheEntry, RenderKeyHash> _cache;
		std::unordered_map<RenderKey, PendingRequest, RenderKeyHash> _pendingRequests;
		std::optional<PendingJob> _activeJob;
		std::unordered_set<std::string> _warned;
		std::uint64_t _tickCounter = 0;
		std::uint64_t _jobCounter = 0;
		int _lastProcessedImGuiFrame = -1;
	};
}
