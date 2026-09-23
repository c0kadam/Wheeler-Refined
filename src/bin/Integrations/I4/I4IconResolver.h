#pragma once

#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "I4Types.h"

namespace I4Integration
{
	class I4IconResolver
	{
	public:
		static I4IconResolver& GetSingleton();

		I4IconSpec Resolve(
			RE::TESForm* object,
			std::uint64_t signature = 0,
			RE::ExtraDataList* extra = nullptr,
			RE::InventoryEntryData* entryData = nullptr);
		bool TryGetCached(RE::TESForm* object, std::uint64_t signature, I4IconSpec& outSpec);

		void ClearCache();

	private:
		struct ResolveKey
		{
			RE::FormID formID = 0;
			std::uint64_t signature = 0;

			bool operator==(const ResolveKey& rhs) const
			{
				return formID == rhs.formID && signature == rhs.signature;
			}
		};

		struct ResolveKeyHash
		{
			std::size_t operator()(const ResolveKey& key) const
			{
				const std::uint64_t mixed = (static_cast<std::uint64_t>(key.formID) << 32) ^ key.signature;
				return std::hash<std::uint64_t>{}(mixed);
			}
		};

		I4IconSpec ResolveImpl(
			RE::TESForm* object,
			RE::GFxMovieView* movie,
			std::uint64_t signature,
			RE::ExtraDataList* extra,
			RE::InventoryEntryData* entryData);

		std::mutex _lock;
		std::unordered_map<ResolveKey, I4IconSpec, ResolveKeyHash> _cache;
		std::unordered_set<std::string> _warned;
	};
}
