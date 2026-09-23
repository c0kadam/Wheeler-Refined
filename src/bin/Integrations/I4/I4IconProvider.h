#pragma once

#include <chrono>

#include "I4Types.h"

namespace I4Integration
{
	class I4IconProvider
	{
	public:
		static I4IconProvider& GetSingleton();

		I4IconResult GetBestIconForItem(
			const Texture::Image& fallback,
			RE::TESForm* object,
			std::uint64_t signature,
			std::uint32_t requestedSizePx);

		void Reset();
		std::string GetStatusString() const;

	private:
		void SyncConfig();
		void MaybeLogDebug() const;

		struct SettingsSnapshot
		{
			bool enabled = false;
			bool prefer = true;
			bool useAlternativePath = false;
			std::uint32_t cacheEntries = 256;
			bool debugLog = false;
			bool traceLog = false;
			bool traceCacheHits = false;
			std::uint32_t renderSizePolicy = 0;
			std::uint32_t fixedRenderSize = 128;
			bool extractionMode = false;
			bool useForWeapons = true;
			bool useForArmor = true;
			bool useForAmmo = true;
			bool useForPotions = true;
			bool useForFood = true;
			bool useForIngredients = true;
			bool useForPoisons = true;
			bool useForBooks = true;
			bool useForScrolls = true;
			bool useForLights = true;
			bool useForMisc = true;
			bool useForSpells = true;
			bool useForShouts = true;
			bool useForPowers = true;
			bool extractForWeapons = true;
			bool extractForArmor = true;
			bool extractForAmmo = true;
			bool extractForPotions = true;
			bool extractForFood = true;
			bool extractForIngredients = true;
			bool extractForPoisons = true;
			bool extractForBooks = true;
			bool extractForScrolls = true;
			bool extractForLights = true;
			bool extractForMisc = true;
			bool extractForSpells = true;
			bool extractForShouts = true;
			bool extractForPowers = true;

			bool operator==(const SettingsSnapshot& rhs) const
			{
				return enabled == rhs.enabled &&
				       prefer == rhs.prefer &&
				       useAlternativePath == rhs.useAlternativePath &&
				       cacheEntries == rhs.cacheEntries &&
				       debugLog == rhs.debugLog &&
				       traceLog == rhs.traceLog &&
				       traceCacheHits == rhs.traceCacheHits &&
				       renderSizePolicy == rhs.renderSizePolicy &&
				       fixedRenderSize == rhs.fixedRenderSize &&
				       extractionMode == rhs.extractionMode &&
				       useForWeapons == rhs.useForWeapons &&
				       useForArmor == rhs.useForArmor &&
				       useForAmmo == rhs.useForAmmo &&
				       useForPotions == rhs.useForPotions &&
				       useForFood == rhs.useForFood &&
				       useForIngredients == rhs.useForIngredients &&
				       useForPoisons == rhs.useForPoisons &&
				       useForBooks == rhs.useForBooks &&
				       useForScrolls == rhs.useForScrolls &&
				       useForLights == rhs.useForLights &&
				       useForMisc == rhs.useForMisc &&
				       useForSpells == rhs.useForSpells &&
				       useForShouts == rhs.useForShouts &&
				       useForPowers == rhs.useForPowers &&
				       extractForWeapons == rhs.extractForWeapons &&
				       extractForArmor == rhs.extractForArmor &&
				       extractForAmmo == rhs.extractForAmmo &&
				       extractForPotions == rhs.extractForPotions &&
				       extractForFood == rhs.extractForFood &&
				       extractForIngredients == rhs.extractForIngredients &&
				       extractForPoisons == rhs.extractForPoisons &&
				       extractForBooks == rhs.extractForBooks &&
				       extractForScrolls == rhs.extractForScrolls &&
				       extractForLights == rhs.extractForLights &&
				       extractForMisc == rhs.extractForMisc &&
				       extractForSpells == rhs.extractForSpells &&
				       extractForShouts == rhs.extractForShouts &&
				       extractForPowers == rhs.extractForPowers;
			}
		};

		SettingsSnapshot _settings{};
		mutable bool _loggedUnavailable = false;
		mutable std::chrono::steady_clock::time_point _nextDebugLog{};
	};
}
