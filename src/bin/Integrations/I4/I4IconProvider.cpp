#include "I4IconProvider.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "I4Availability.h"
#include "I4IconResolver.h"
#include "I4Log.h"
#include "I4SwfIconRenderer.h"
#include "bin/Config.h"

namespace I4Integration
{
	namespace
	{
		constexpr auto kDebugLogInterval = std::chrono::seconds(5);
		constexpr std::uint32_t kExtractionMinRenderSize = 96;
		constexpr std::uint32_t kExtractionSupersampleFactor = 2;

		enum class IconCategory : std::uint8_t
		{
			kUnknown = 0,
			kWeapon,
			kArmor,
			kAmmo,
			kPotion,
			kFood,
			kIngredient,
			kPoison,
			kBook,
			kScroll,
			kLight,
			kMisc,
			kSpell,
			kShout,
			kPower
		};

		IconCategory ClassifyIconCategory(const RE::TESForm* form)
		{
			if (!form) {
				return IconCategory::kUnknown;
			}

			switch (form->GetFormType()) {
			case RE::FormType::Weapon:
				return IconCategory::kWeapon;
			case RE::FormType::Armor:
				return IconCategory::kArmor;
			case RE::FormType::Ammo:
				return IconCategory::kAmmo;
			case RE::FormType::Book:
				return IconCategory::kBook;
			case RE::FormType::Scroll:
				return IconCategory::kScroll;
			case RE::FormType::Light:
				return IconCategory::kLight;
			case RE::FormType::Misc:
				return IconCategory::kMisc;
			case RE::FormType::Shout:
				return IconCategory::kShout;
			case RE::FormType::AlchemyItem:
				{
					const auto* alchemy = form->As<RE::AlchemyItem>();
					if (!alchemy) {
						return IconCategory::kPotion;
					}
					if (alchemy->IsPoison()) {
						return IconCategory::kPoison;
					}
					if (alchemy->IsFood()) {
						return IconCategory::kFood;
					}
					return IconCategory::kPotion;
				}
			case RE::FormType::Ingredient:
				return IconCategory::kIngredient;
			case RE::FormType::Spell:
				{
					const auto* spell = form->As<RE::SpellItem>();
					if (!spell) {
						return IconCategory::kSpell;
					}
					const auto spellType = spell->GetSpellType();
					if (spellType == RE::MagicSystem::SpellType::kPower ||
					    spellType == RE::MagicSystem::SpellType::kLesserPower ||
					    spellType == RE::MagicSystem::SpellType::kVoicePower) {
						return IconCategory::kPower;
					}
					return IconCategory::kSpell;
				}
			default:
				return IconCategory::kUnknown;
			}
		}

		bool IsCategoryEnabled(IconCategory category)
		{
			switch (category) {
			case IconCategory::kWeapon:
				return Config::I4::UseForWeapons;
			case IconCategory::kArmor:
				return Config::I4::UseForArmor;
			case IconCategory::kAmmo:
				return Config::I4::UseForAmmo;
			case IconCategory::kPotion:
				return Config::I4::UseForPotions;
			case IconCategory::kFood:
				return Config::I4::UseForFood;
			case IconCategory::kIngredient:
				return Config::I4::UseForIngredients;
			case IconCategory::kPoison:
				return Config::I4::UseForPoisons;
			case IconCategory::kBook:
				return Config::I4::UseForBooks;
			case IconCategory::kScroll:
				return Config::I4::UseForScrolls;
			case IconCategory::kLight:
				return Config::I4::UseForLights;
			case IconCategory::kMisc:
				return Config::I4::UseForMisc;
			case IconCategory::kSpell:
				return Config::I4::UseForSpells;
			case IconCategory::kShout:
				return Config::I4::UseForShouts;
			case IconCategory::kPower:
				return Config::I4::UseForPowers;
			default:
				return true;
			}
		}

		bool IsExtractionAllowedForCategory(IconCategory category)
		{
			switch (category) {
			case IconCategory::kWeapon:
				return Config::I4::ExtractForWeapons;
			case IconCategory::kArmor:
				return Config::I4::ExtractForArmor;
			case IconCategory::kAmmo:
				return Config::I4::ExtractForAmmo;
			case IconCategory::kPotion:
				return Config::I4::ExtractForPotions;
			case IconCategory::kFood:
				return Config::I4::ExtractForFood;
			case IconCategory::kIngredient:
				return Config::I4::ExtractForIngredients;
			case IconCategory::kPoison:
				return Config::I4::ExtractForPoisons;
			case IconCategory::kBook:
				return Config::I4::ExtractForBooks;
			case IconCategory::kScroll:
				return Config::I4::ExtractForScrolls;
			case IconCategory::kLight:
				return Config::I4::ExtractForLights;
			case IconCategory::kMisc:
				return Config::I4::ExtractForMisc;
			case IconCategory::kSpell:
				return Config::I4::ExtractForSpells;
			case IconCategory::kShout:
				return Config::I4::ExtractForShouts;
			case IconCategory::kPower:
				return Config::I4::ExtractForPowers;
			default:
				return true;
			}
		}

		const char* CategoryToString(IconCategory category)
		{
			switch (category) {
			case IconCategory::kWeapon:
				return "weapon";
			case IconCategory::kArmor:
				return "armor";
			case IconCategory::kAmmo:
				return "ammo";
			case IconCategory::kPotion:
				return "potion";
			case IconCategory::kFood:
				return "food";
			case IconCategory::kIngredient:
				return "ingredient";
			case IconCategory::kPoison:
				return "poison";
			case IconCategory::kBook:
				return "book";
			case IconCategory::kScroll:
				return "scroll";
			case IconCategory::kLight:
				return "light";
			case IconCategory::kMisc:
				return "misc";
			case IconCategory::kSpell:
				return "spell";
			case IconCategory::kShout:
				return "shout";
			case IconCategory::kPower:
				return "power";
			default:
				return "unknown";
			}
		}

		bool IsDefaultSourcePath(std::string source)
		{
			std::transform(source.begin(), source.end(), source.begin(), [](unsigned char c) {
				if (c == '/') {
					return '\\';
				}
				return static_cast<char>(std::tolower(c));
			});
			return source.find("skyui\\icons_item_psychosteve.swf") != std::string::npos;
		}

	}

	I4IconProvider& I4IconProvider::GetSingleton()
	{
		static I4IconProvider singleton;
		return singleton;
	}

	void I4IconProvider::SyncConfig()
	{
		const SettingsSnapshot now{
			Config::I4::Enabled,
			Config::I4::PreferI4Icons,
			Config::I4::UseAlternativePath,
			Config::I4::CacheMaxEntries,
			Config::I4::DebugLog,
			Config::I4::TraceLog,
			Config::I4::TraceCacheHits,
			Config::I4::RenderSizePolicy,
			Config::I4::FixedRenderSize,
			Config::I4::ExtractionMode,
			Config::I4::UseForWeapons,
			Config::I4::UseForArmor,
			Config::I4::UseForAmmo,
			Config::I4::UseForPotions,
			Config::I4::UseForFood,
			Config::I4::UseForIngredients,
			Config::I4::UseForPoisons,
			Config::I4::UseForBooks,
			Config::I4::UseForScrolls,
			Config::I4::UseForLights,
			Config::I4::UseForMisc,
			Config::I4::UseForSpells,
			Config::I4::UseForShouts,
			Config::I4::UseForPowers,
			Config::I4::ExtractForWeapons,
			Config::I4::ExtractForArmor,
			Config::I4::ExtractForAmmo,
			Config::I4::ExtractForPotions,
			Config::I4::ExtractForFood,
			Config::I4::ExtractForIngredients,
			Config::I4::ExtractForPoisons,
			Config::I4::ExtractForBooks,
			Config::I4::ExtractForScrolls,
			Config::I4::ExtractForLights,
			Config::I4::ExtractForMisc,
			Config::I4::ExtractForSpells,
			Config::I4::ExtractForShouts,
			Config::I4::ExtractForPowers
		};
		if (now == _settings) {
			return;
		}

		if (Config::I4::DebugLog || Config::I4::TraceLog) {
			I4_LOG_INFO("[I4][TRACE][provider.config] changed enabled={} prefer={} altPath={} cache={} debug={} trace={} traceCacheHits={} renderPolicy={} fixedRenderSize={} extraction={} extract(misc={}, potion={}, food={}, ingredient={}, poison={}, spell={}, shout={}, power={})",
				now.enabled,
				now.prefer,
				now.useAlternativePath,
				now.cacheEntries,
				now.debugLog,
				now.traceLog,
				now.traceCacheHits,
				now.renderSizePolicy,
				now.fixedRenderSize,
				now.extractionMode,
				now.extractForMisc,
				now.extractForPotions,
				now.extractForFood,
				now.extractForIngredients,
				now.extractForPoisons,
				now.extractForSpells,
				now.extractForShouts,
				now.extractForPowers);
		}

		_settings = now;
		Reset();
	}

	I4IconResult I4IconProvider::GetBestIconForItem(
		const Texture::Image& fallback,
		RE::TESForm* object,
		std::uint64_t signature,
		std::uint32_t requestedSizePx)
	{
		SyncConfig();
		auto& stats = GetStats();
		stats.providerCalls.fetch_add(1, std::memory_order_relaxed);

		I4IconResult result{};
		result.image = fallback;
		result.tint = C_SKYRIMWHITE;
		result.usingI4 = false;
		const bool trace = Config::I4::TraceLog;
		const RE::FormID formID = object ? object->GetFormID() : 0;
		const std::uint32_t formType = object ? static_cast<std::uint32_t>(object->GetFormType()) : 0;
		const auto category = ClassifyIconCategory(object);
		const bool categoryEnabled = IsCategoryEnabled(category);
		const bool extractionCategoryEnabled = IsExtractionAllowedForCategory(category);
		const bool extractionActiveForRequest = Config::I4::ExtractionMode && extractionCategoryEnabled;
		const std::uint32_t renderSize = Config::I4::RenderSizePolicy == 1 ?
			(std::clamp)(Config::I4::FixedRenderSize, 16u, 1024u) :
			(std::clamp)(requestedSizePx, 16u, 1024u);
		std::uint32_t effectiveRenderSize = renderSize;
		if (extractionActiveForRequest && Config::I4::RenderSizePolicy == 0) {
			const std::uint64_t supersampled = static_cast<std::uint64_t>(renderSize) * kExtractionSupersampleFactor;
			const std::uint32_t supersampledClamped = static_cast<std::uint32_t>((std::min<std::uint64_t>)(1024u, supersampled));
			effectiveRenderSize = (std::clamp)((std::max)(supersampledClamped, kExtractionMinRenderSize), 16u, 1024u);
		}

		if (trace) {
			I4_LOG_INFO("[I4][TRACE][provider.req] form={:08X} type={} sig={} category={} categoryEnabled={} extractionCategoryEnabled={} extractionActive={} reqPx={} fallback={}x{} enabled={} prefer={} altPath={} extractionMaster={}",
				formID,
				formType,
				signature,
				CategoryToString(category),
				categoryEnabled,
				extractionCategoryEnabled,
				extractionActiveForRequest,
				requestedSizePx,
				fallback.width,
				fallback.height,
				Config::I4::Enabled,
				Config::I4::PreferI4Icons,
				Config::I4::UseAlternativePath,
				Config::I4::ExtractionMode);
		}

		if (!Config::I4::Enabled || !Config::I4::PreferI4Icons || !object) {
			stats.displayedFallback.fetch_add(1, std::memory_order_relaxed);
			stats.fallbackDisabled.fetch_add(1, std::memory_order_relaxed);
			_loggedUnavailable = false;
			if (trace) {
				I4_LOG_INFO("[I4][TRACE][provider.out] form={:08X} decision=fallback reason=disabled enabled={} prefer={} object={}",
					formID,
					Config::I4::Enabled,
					Config::I4::PreferI4Icons,
					object != nullptr);
			}
			return result;
		}

		if (!categoryEnabled) {
			stats.displayedFallback.fetch_add(1, std::memory_order_relaxed);
			stats.fallbackCategoryFiltered.fetch_add(1, std::memory_order_relaxed);
			if (trace) {
				I4_LOG_INFO("[I4][TRACE][provider.out] form={:08X} decision=fallback reason=category_filtered category={}",
					formID,
					CategoryToString(category));
			}
			MaybeLogDebug();
			return result;
		}

		auto& availability = I4Availability::GetSingleton();
		if (!availability.IsAvailable()) {
			I4IconSpec cachedSpec{};
			Texture::Image cachedImage{};
			bool cachedPrecolored = false;
			const bool cachedResolved = I4IconResolver::GetSingleton().TryGetCached(object, signature, cachedSpec);
			const bool cachedRendered = cachedResolved &&
			                            I4SwfIconRenderer::GetSingleton().TryGetCachedIcon(
				                            cachedSpec,
				                            effectiveRenderSize,
				                            cachedImage,
				                            cachedPrecolored);
			if (cachedRendered && cachedImage.texture) {
				result.image = cachedImage;
				result.tint = cachedPrecolored ? C_SKYRIMWHITE : cachedSpec.color;
				result.usingI4 = true;
				stats.displayedI4.fetch_add(1, std::memory_order_relaxed);
				if (trace) {
					I4_LOG_INFO("[I4][TRACE][provider.out] form={:08X} decision=i4_cached_unavailable status={} precolored={} tint=0x{:08X} image={}x{}",
						formID,
						availability.GetStatusString(),
						cachedPrecolored,
						static_cast<std::uint32_t>(result.tint),
						result.image.width,
						result.image.height);
				}
				MaybeLogDebug();
				return result;
			}
			if (!_loggedUnavailable) {
				_loggedUnavailable = true;
				I4_LOG_WARN("[I4] Enabled but unavailable: {}", availability.GetStatusString());
			}
			stats.displayedFallback.fetch_add(1, std::memory_order_relaxed);
			stats.fallbackUnavailable.fetch_add(1, std::memory_order_relaxed);
			if (trace) {
				I4_LOG_INFO("[I4][TRACE][provider.out] form={:08X} decision=fallback reason=unavailable status={} cachedResolved={} cachedRendered={}",
					formID,
					availability.GetStatusString(),
					cachedResolved,
					cachedRendered);
			}
			MaybeLogDebug();
			return result;
		}
		_loggedUnavailable = false;

		I4IconSpec spec = I4IconResolver::GetSingleton().Resolve(object, signature);
		if (!spec.valid) {
			stats.displayedFallback.fetch_add(1, std::memory_order_relaxed);
			stats.fallbackNoSpec.fetch_add(1, std::memory_order_relaxed);
			if (trace) {
				I4_LOG_INFO("[I4][TRACE][provider.out] form={:08X} decision=fallback reason=no_spec",
					formID);
			}
			MaybeLogDebug();
			return result;
		}

		if (trace) {
			I4_LOG_INFO("[I4][TRACE][provider.render] form={:08X} sourceKind={} source='{}' label='{}' color=0x{:08X} size(requested={}, effective={}) extractionActive={}",
				formID,
				IsDefaultSourcePath(spec.iconSource) ? "default" : "custom",
				spec.iconSource,
				spec.iconLabel,
				static_cast<std::uint32_t>(spec.color),
				renderSize,
				effectiveRenderSize,
				extractionActiveForRequest);
		}

		auto image = I4SwfIconRenderer::GetSingleton().GetIcon(spec, effectiveRenderSize, extractionActiveForRequest);
		if (!image.texture) {
			stats.displayedFallback.fetch_add(1, std::memory_order_relaxed);
			stats.fallbackNoImage.fetch_add(1, std::memory_order_relaxed);
			if (trace) {
				I4_LOG_INFO("[I4][TRACE][provider.out] form={:08X} decision=fallback reason=no_image source='{}' label='{}' size={} extractionActive={}",
					formID,
					spec.iconSource,
					spec.iconLabel,
					effectiveRenderSize,
					extractionActiveForRequest);
			}
			MaybeLogDebug();
			return result;
		}

		const bool precolored = I4SwfIconRenderer::GetSingleton().IsPrecolored(spec, effectiveRenderSize);
		result.image = image;
		result.tint = precolored ? C_SKYRIMWHITE : spec.color;
		result.usingI4 = true;
		stats.displayedI4.fetch_add(1, std::memory_order_relaxed);
		if (trace) {
			I4_LOG_INFO("[I4][TRACE][provider.out] form={:08X} decision=i4_ok precolored={} tint=0x{:08X} image={}x{}",
				formID,
				precolored,
				static_cast<std::uint32_t>(result.tint),
				result.image.width,
				result.image.height);
		}
		MaybeLogDebug();
		return result;
	}

	void I4IconProvider::Reset()
	{
		if (Config::I4::TraceLog) {
			I4_LOG_INFO("[I4][TRACE][provider.reset] clearing availability/resolver/renderer caches");
		}
		_loggedUnavailable = false;
		_nextDebugLog = std::chrono::steady_clock::time_point{};
		I4Availability::GetSingleton().Invalidate();
		I4IconResolver::GetSingleton().ClearCache();
		I4SwfIconRenderer::GetSingleton().ClearCache();
		ResetStats();
	}

	std::string I4IconProvider::GetStatusString() const
	{
		return I4Availability::GetSingleton().GetStatusString();
	}

	void I4IconProvider::MaybeLogDebug() const
	{
		if (!Config::I4::DebugLog) {
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		if (now < _nextDebugLog) {
			return;
		}
		_nextDebugLog = now + kDebugLogInterval;
		auto& stats = GetStats();

		I4_LOG_INFO("[I4] status={} enabled={} prefer={} altPath={} extraction={} resolve(calls={}, hits={}) render(calls={}, hits={}, failures={})",
			I4Availability::GetSingleton().GetStatusString(),
			Config::I4::Enabled,
			Config::I4::PreferI4Icons,
			Config::I4::UseAlternativePath,
			Config::I4::ExtractionMode,
			stats.resolveCalls.load(std::memory_order_relaxed),
			stats.resolveCacheHits.load(std::memory_order_relaxed),
			stats.renderCalls.load(std::memory_order_relaxed),
			stats.renderCacheHits.load(std::memory_order_relaxed),
			stats.renderFailures.load(std::memory_order_relaxed));

		I4_LOG_INFO("[I4] pipeline(providerCalls={}, builtInHits={}, offscreenHits={}, queued={}, pending={})",
			stats.providerCalls.load(std::memory_order_relaxed),
			stats.renderBuiltInHits.load(std::memory_order_relaxed),
			stats.renderOffscreenHits.load(std::memory_order_relaxed),
			stats.renderQueueEnqueued.load(std::memory_order_relaxed),
			stats.renderQueuePending.load(std::memory_order_relaxed));

		I4_LOG_INFO("[I4] extraction(master={}): inv(weap={}, armor={}, ammo={}, potion={}, food={}, ingredient={}, poison={}, book={}, scroll={}, light={}, misc={}) alt(spell={}, shout={}, power={})",
			Config::I4::ExtractionMode,
			Config::I4::ExtractForWeapons,
			Config::I4::ExtractForArmor,
			Config::I4::ExtractForAmmo,
			Config::I4::ExtractForPotions,
			Config::I4::ExtractForFood,
			Config::I4::ExtractForIngredients,
			Config::I4::ExtractForPoisons,
			Config::I4::ExtractForBooks,
			Config::I4::ExtractForScrolls,
			Config::I4::ExtractForLights,
			Config::I4::ExtractForMisc,
			Config::I4::ExtractForSpells,
			Config::I4::ExtractForShouts,
			Config::I4::ExtractForPowers);

		I4_LOG_INFO("[I4] display(i4={}, fallback={}) fallbackReasons(disabled={}, unavailable={}, category={}, noSpec={}, noImage={})",
			stats.displayedI4.load(std::memory_order_relaxed),
			stats.displayedFallback.load(std::memory_order_relaxed),
			stats.fallbackDisabled.load(std::memory_order_relaxed),
			stats.fallbackUnavailable.load(std::memory_order_relaxed),
			stats.fallbackCategoryFiltered.load(std::memory_order_relaxed),
			stats.fallbackNoSpec.load(std::memory_order_relaxed),
			stats.fallbackNoImage.load(std::memory_order_relaxed));
	}
}
