#include "OStimIntegration.h"

#include "OStimBridge.h"
#include "OStimNGThreadAPI.h"
#include "OStimPreviewResolver.h"
#include "OStimStateTracker.h"
#include "bin/API/WheelerAPI.h"
#include "bin/Config.h"
#include "bin/Integrations/ActionHotkeysBridge.h"
#include "bin/UserInput/Controls.h"
#include "bin/Wheeler/TransformWheelManager.h"
#include "bin/Wheeler/Wheel.h"
#include "bin/Wheeler/WheelEntry.h"
#include "bin/Wheeler/WheelItems/WheelItemFactory.h"
#include "bin/Wheeler/Wheeler.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	constexpr std::string_view kControlWheelTag = "OStimIntegration.Control";
	constexpr std::string_view kBrowserWheelTag = "OStimIntegration.Browser";
	constexpr std::string_view kLegacyBrowserWheelTagPrefix = "OStimIntegration.Browser.";
	constexpr std::array<OStimActionKind, 8> kControlLayout{
		OStimActionKind::StopScene,
		OStimActionKind::PreviousPosition,
		OStimActionKind::NextPosition,
		OStimActionKind::DecreaseSpeed,
		OStimActionKind::IncreaseSpeed,
		OStimActionKind::OpenPositionBrowser,
		OStimActionKind::PreviousStage,
		OStimActionKind::NextStage
	};

	struct IntegrationState
	{
		struct BrowserContextLevel
		{
			std::string sceneID;
			std::string parentSceneID;
			std::string displayName;
			std::string previewPath;
			std::string iconPath;
			std::string category;
			std::string subcategory;
			std::vector<OStimPositionInfo> positions;
			std::uint32_t page = 0;
			std::int32_t focusIndex = -1;
			std::int32_t parentReturnFocusIndex = -1;
			bool allowDirectSelection = false;
		};

		bool refreshRequested = true;
		bool lastSceneActive = false;
		bool lastAvailable = false;
		std::uint64_t lastAppliedRevision = 0;
		std::optional<int> previousWheelIndex;
		std::vector<BrowserContextLevel> browserStack;
		bool suppressManagedWheelsUntilSceneStops = false;
		std::string lastAvailabilityReason;
	};

	struct BrowserWheelLayout
	{
		std::vector<std::shared_ptr<WheelItem>> items;
		int previousPageIndex = -1;
		int nextPageIndex = -1;
		int defaultFocusIndex = -1;
	};

	enum class BrowserFocusHint
	{
		None,
		PreserveCurrent,
		PreviousPage,
		NextPage
	};

	IntegrationState s_state;

	template <class... TArgs>
	void DebugLog(const char* a_format, TArgs&&... a_args)
	{
		if (Config::OStimIntegration::DebugLog) {
			logger::info(fmt::runtime(a_format), std::forward<TArgs>(a_args)...);
		}
	}

	std::string BuildBrowserWheelTag(std::uint32_t a_page)
	{
		(void)a_page;
		return std::string(kBrowserWheelTag);
	}

	bool IsSceneBlockedByMenus()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return true;
		}

		static constexpr std::array<std::string_view, 11> kBlockingMenus{
			RE::LoadingMenu::MENU_NAME,
			RE::MapMenu::MENU_NAME,
			RE::TweenMenu::MENU_NAME,
			RE::InventoryMenu::MENU_NAME,
			RE::MagicMenu::MENU_NAME,
			RE::FavoritesMenu::MENU_NAME,
			RE::Console::MENU_NAME,
			RE::MainMenu::MENU_NAME,
			RE::JournalMenu::MENU_NAME,
			RE::LockpickingMenu::MENU_NAME,
			RE::DialogueMenu::MENU_NAME
		};

		if (ui->GameIsPaused()) {
			return true;
		}
		for (auto menuName : kBlockingMenus) {
			if (ui->IsMenuOpen(menuName.data())) {
				return true;
			}
		}
		return Controls::IsRebindActive() || Wheeler::IsInEditMode();
	}

	std::optional<int> FindWheelIndexByTag(std::string_view a_tag)
	{
		std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		const int wheelCount = Wheeler::GetWheelCount();
		for (int i = 0; i < wheelCount; ++i) {
			Wheel* wheel = Wheeler::GetWheelByIndex(i);
			if (wheel && wheel->GetClientTag() == a_tag) {
				return i;
			}
		}
		return std::nullopt;
	}

	std::optional<std::uint32_t> ParseBrowserPage(std::string_view a_tag)
	{
		if (a_tag == kBrowserWheelTag) {
			if (s_state.browserStack.empty()) {
				return 0;
			}
			return s_state.browserStack.back().page;
		}
		if (!a_tag.starts_with(kLegacyBrowserWheelTagPrefix)) {
			return std::nullopt;
		}

		const std::string suffix(a_tag.substr(kLegacyBrowserWheelTagPrefix.size()));
		if (suffix.empty()) {
			return std::nullopt;
		}

		try {
			return static_cast<std::uint32_t>(std::stoul(suffix, nullptr, 10));
		} catch (...) {
			return std::nullopt;
		}
	}

	bool IsManagedTagInternal(std::string_view a_tag)
	{
		return a_tag == kControlWheelTag ||
		       a_tag == kBrowserWheelTag ||
		       a_tag.starts_with(kLegacyBrowserWheelTagPrefix);
	}

	bool IsBrowserWheelTag(std::string_view a_tag)
	{
		return a_tag == kBrowserWheelTag || a_tag.starts_with(kLegacyBrowserWheelTagPrefix);
	}

	void TagWheelIndex(int a_wheelIndex, std::string_view a_tag)
	{
		std::unique_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		if (Wheel* wheel = Wheeler::GetWheelByIndex(a_wheelIndex)) {
			wheel->SetClientTag(a_tag);
		}
	}

	std::vector<int> CollectManagedWheelIndices()
	{
		std::vector<int> indices;
		std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		const int wheelCount = Wheeler::GetWheelCount();
		indices.reserve(static_cast<std::size_t>(wheelCount));
		for (int i = 0; i < wheelCount; ++i) {
			Wheel* wheel = Wheeler::GetWheelByIndex(i);
			if (wheel && IsManagedTagInternal(wheel->GetClientTag())) {
				indices.push_back(i);
			}
		}
		return indices;
	}

	std::optional<int> FindFallbackUserWheelIndex()
	{
		std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		const int wheelCount = Wheeler::GetWheelCount();
		for (int i = 0; i < wheelCount; ++i) {
			Wheel* wheel = Wheeler::GetWheelByIndex(i);
			if (!wheel) {
				continue;
			}
			if (ActionHotkeysBridge::IsBridgeWheelTag(wheel->GetClientTag()) ||
				TransformWheelManager::IsTransformWheelIndex(i) ||
				WheelerAPI::IsManagedWheelIndex(i)) {
				continue;
			}
			return i;
		}
		return std::nullopt;
	}

	bool SwitchOrSetActiveWheel(int a_wheelIndex)
	{
		if (a_wheelIndex < 0 || a_wheelIndex >= Wheeler::GetWheelCount()) {
			return false;
		}

		if (Wheeler::IsWheelerOpen()) {
			return Wheeler::SwitchToWheelIndexForNavigation(a_wheelIndex);
		}
		Wheeler::SetActiveWheelIndex(a_wheelIndex);
		return true;
	}

	std::optional<int> GetWheelHoveredEntryIndex(std::string_view a_tag)
	{
		auto wheelIndex = FindWheelIndexByTag(a_tag);
		if (!wheelIndex.has_value()) {
			return std::nullopt;
		}

		std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		if (Wheel* wheel = Wheeler::GetWheelByIndex(*wheelIndex)) {
			return wheel->GetHoveredEntryIndex();
		}
		return std::nullopt;
	}

	bool IsActiveWheelTagged(std::string_view a_tag)
	{
		const int activeIdx = Wheeler::GetActiveWheelIndex();
		if (activeIdx < 0) {
			return false;
		}

		std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		if (Wheel* wheel = Wheeler::GetWheelByIndex(activeIdx)) {
			return wheel->GetClientTag() == a_tag;
		}
		return false;
	}

	std::string HumanizeBrowserLabel(std::string_view a_value)
	{
		std::string out(a_value);
		if (out.empty()) {
			return out;
		}

		if (!out.empty() && out.front() == '$') {
			out.erase(0, 1);
		}

		constexpr std::string_view kNavPrefix = "ostim_nav_";
		std::string lowered(out);
		std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		if (lowered.starts_with(kNavPrefix)) {
			out.erase(0, kNavPrefix.size());
		}

		std::string cleaned;
		cleaned.reserve(out.size());
		bool inBraces = false;
		for (char c : out) {
			if (c == '{') {
				inBraces = true;
				continue;
			}
			if (inBraces) {
				if (c == '}') {
					inBraces = false;
				}
				continue;
			}
			if (c == '_' || c == '-' || c == '/') {
				cleaned.push_back(' ');
				continue;
			}
			if (std::isalnum(static_cast<unsigned char>(c)) || std::isspace(static_cast<unsigned char>(c))) {
				cleaned.push_back(c);
			}
		}

		while (!cleaned.empty() && std::isspace(static_cast<unsigned char>(cleaned.back()))) {
			cleaned.pop_back();
		}
		if (const auto split = cleaned.find_last_of(' '); split != std::string::npos) {
			const auto suffixSize = cleaned.size() - split - 1;
			if (suffixSize == 1 &&
				std::isalpha(static_cast<unsigned char>(cleaned.back()))) {
				cleaned.erase(split);
			}
		}

		bool newWord = true;
		for (char& c : cleaned) {
			if (std::isspace(static_cast<unsigned char>(c))) {
				newWord = true;
				continue;
			}
			c = static_cast<char>(newWord ?
				std::toupper(static_cast<unsigned char>(c)) :
				std::tolower(static_cast<unsigned char>(c)));
			newWord = false;
		}

		return cleaned;
	}

	std::string NormalizeSceneKey(std::string_view a_value)
	{
		std::string out(a_value);
		std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return out;
	}

	bool SceneKeysEqual(std::string_view a_lhs, std::string_view a_rhs)
	{
		return !a_lhs.empty() &&
		       !a_rhs.empty() &&
		       NormalizeSceneKey(a_lhs) == NormalizeSceneKey(a_rhs);
	}

	bool NeedsDisplayNameHydration(const OStimPositionInfo& a_position)
	{
		return a_position.displayName.empty() ||
		       a_position.displayName.front() == '$' ||
		       a_position.displayName == a_position.id ||
		       a_position.displayName == a_position.destinationID;
	}

	void HydratePositionDisplayNames(std::vector<OStimPositionInfo>& a_positions)
	{
		std::vector<std::string> sceneIDs;
		std::vector<std::size_t> sceneIndices;
		sceneIDs.reserve(a_positions.size());
		sceneIndices.reserve(a_positions.size());

		for (std::size_t i = 0; i < a_positions.size(); ++i) {
			if (!NeedsDisplayNameHydration(a_positions[i])) {
				continue;
			}

			const auto& sceneID = a_positions[i].destinationID.empty() ?
				a_positions[i].id :
				a_positions[i].destinationID;
			if (sceneID.empty()) {
				continue;
			}

			sceneIndices.push_back(i);
			sceneIDs.push_back(sceneID);
		}

		if (!sceneIDs.empty()) {
			std::vector<std::string> sceneNames;
			if (OStimBridge::GetSceneNames(sceneIDs, sceneNames) && sceneNames.size() == sceneIDs.size()) {
				for (std::size_t i = 0; i < sceneNames.size(); ++i) {
					if (!sceneNames[i].empty() && sceneNames[i].front() != '$') {
						a_positions[sceneIndices[i]].displayName = sceneNames[i];
					}
				}
			}
		}

		for (auto& position : a_positions) {
			if (!NeedsDisplayNameHydration(position)) {
				continue;
			}

			if (!position.description.empty()) {
				position.displayName = HumanizeBrowserLabel(position.description);
			}
			if (position.displayName.empty()) {
				const auto& fallbackSceneID = position.destinationID.empty() ? position.id : position.destinationID;
				position.displayName = HumanizeBrowserLabel(fallbackSceneID);
			}
			if (position.displayName.empty()) {
				position.displayName = position.destinationID.empty() ? position.id : position.destinationID;
			}
		}
	}

	bool HasBrowserEntries(const IntegrationState::BrowserContextLevel& a_level)
	{
		return !a_level.positions.empty() ||
		       (a_level.allowDirectSelection && !a_level.sceneID.empty());
	}

	IntegrationState::BrowserContextLevel* GetCurrentBrowserLevel()
	{
		return s_state.browserStack.empty() ? nullptr : &s_state.browserStack.back();
	}

	void InvalidateBrowserState()
	{
		s_state.browserStack.clear();
	}

	void RememberPreviousWheelIfNeeded()
	{
		const int activeIdx = Wheeler::GetActiveWheelIndex();
		if (activeIdx < 0 ||
			ActionHotkeysBridge::IsBridgeWheelIndex(activeIdx) ||
			TransformWheelManager::IsTransformWheelIndex(activeIdx) ||
			WheelerAPI::IsManagedWheelIndex(activeIdx)) {
			return;
		}
		s_state.previousWheelIndex = activeIdx;
	}

	void RestorePreviousWheelIfNeeded()
	{
		if (Config::OStimIntegration::RestorePreviousWheelOnSceneEnd &&
			s_state.previousWheelIndex.has_value()) {
			const int idx = *s_state.previousWheelIndex;
			if (idx >= 0 &&
				idx < Wheeler::GetWheelCount() &&
				!ActionHotkeysBridge::IsBridgeWheelIndex(idx) &&
				!TransformWheelManager::IsTransformWheelIndex(idx) &&
				!WheelerAPI::IsManagedWheelIndex(idx)) {
				SwitchOrSetActiveWheel(idx);
				return;
			}
		}

		if (auto fallback = FindFallbackUserWheelIndex(); fallback.has_value()) {
			SwitchOrSetActiveWheel(*fallback);
		}
	}

	bool EnsureManagedWheel(std::string_view a_tag, std::uint32_t a_desiredEntries)
	{
		if (FindWheelIndexByTag(a_tag).has_value()) {
			return true;
		}

		auto* api = GetWheelerAPI();
		if (!api || !api->IsInitialized()) {
			return false;
		}

		WheelerAPI::WheelConfig config{};
		config.numEntries = static_cast<int32_t>((std::max)(1u, a_desiredEntries));
		config.position = -1;
		config.managed = true;
		config.clientName = a_tag.data();
		config.showLabel = false;

		const int created = api->CreateManagedWheel(&config);
		if (created < 0) {
			return false;
		}

		TagWheelIndex(created, a_tag);
		return true;
	}

	void PopulateWheel(
		std::string_view a_tag,
		const std::vector<std::shared_ptr<WheelItem>>& a_items,
		int a_focusIndex = -1,
		bool a_preserveExistingFocus = false)
	{
		auto wheelIndex = FindWheelIndexByTag(a_tag);
		if (!wheelIndex.has_value()) {
			return;
		}

		std::unique_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(*wheelIndex);
		if (!wheel) {
			return;
		}

		int focusIndex = a_preserveExistingFocus ? wheel->GetHoveredEntryIndex() : -1;
		if (a_focusIndex >= 0) {
			focusIndex = a_focusIndex;
		}

		wheel->Clear();
		for (const auto& item : a_items) {
			auto entry = std::make_unique<WheelEntry>();
			if (item) {
				entry->PushItem(item);
			}
			wheel->PushEntry(std::move(entry));
		}
		if (a_items.empty()) {
			wheel->PushEmptyEntry();
		}
		wheel->SetClientTag(a_tag);
		if (focusIndex >= 0 && focusIndex < wheel->GetNumEntries()) {
			wheel->SetHoveredEntryIndex(focusIndex);
		} else {
			wheel->SetHoveredEntryIndex(-1);
		}
		wheel->ResetAnimation();
	}

	void DeleteManagedWheels()
	{
		auto indices = CollectManagedWheelIndices();
		if (indices.empty()) {
			s_state.previousWheelIndex.reset();
			InvalidateBrowserState();
			return;
		}

		if (OStimIntegration::IsManagedWheelIndex(Wheeler::GetActiveWheelIndex())) {
			RestorePreviousWheelIfNeeded();
		}

		auto* api = GetWheelerAPI();
		if (api) {
			std::sort(indices.begin(), indices.end(), std::greater<>());
			for (int idx : indices) {
				api->DeleteManagedWheel(idx);
			}
		}
		DebugLog("[OStimIntegration] deleted {} managed wheel(s)", indices.size());

		s_state.previousWheelIndex.reset();
		InvalidateBrowserState();
	}

	std::size_t GetBrowserPageCount(const std::vector<OStimPositionInfo>& a_positions)
	{
		const std::size_t perPage = static_cast<std::size_t>(
			(std::max)(4u, Config::OStimIntegration::MaxPositionsPerPage));
		return a_positions.empty() ? 0 :
			((a_positions.size() + perPage - 1) / perPage);
	}

	std::uint32_t ClampBrowserPage(std::uint32_t a_page, const std::vector<OStimPositionInfo>& a_positions)
	{
		const std::size_t pageCount = GetBrowserPageCount(a_positions);
		if (pageCount == 0) {
			return 0;
		}
		return static_cast<std::uint32_t>((std::min<std::size_t>)(a_page, pageCount - 1));
	}

	std::optional<int> GetControlWheelEntryIndex(OStimActionKind a_kind)
	{
		for (std::size_t i = 0; i < kControlLayout.size(); ++i) {
			if (kControlLayout[i] == a_kind) {
				return static_cast<int>(i);
			}
		}
		return std::nullopt;
	}

	bool SceneAllowsDirectControls(const std::optional<OStimSceneInfo>& a_scene)
	{
		return a_scene &&
		       a_scene->active &&
		       !a_scene->inTransition &&
		       !a_scene->inSequence &&
		       !a_scene->playerControlDisabled;
	}

	bool HasBrowserSnapshot()
	{
		const auto* level = GetCurrentBrowserLevel();
		return level && HasBrowserEntries(*level);
	}

	std::vector<OStimPositionInfo> GetActionPositions(const OStimSceneInfo& a_scene)
	{
		auto positions = OStimStateTracker::GetAvailablePositions();
		if (!positions.empty()) {
			return positions;
		}

		if (OStimNGThreadAPI::IsAvailable()) {
			positions = OStimNGThreadAPI::GetNavigationPositions(a_scene);
			if (!positions.empty()) {
				return positions;
			}
		}

		return OStimBridge::GetCandidatePositions(Config::OStimIntegration::PreferCurrentAnimationClass);
	}

	bool NavigateToPosition(const OStimPositionInfo& a_position)
	{
		if (!a_position.id.empty() && OStimNGThreadAPI::NavigateToScene(a_position.id)) {
			return true;
		}

		return !a_position.id.empty() &&
		       OStimBridge::CallAPIMethod("TravelToAnimationIfPossible", a_position.id);
	}

	bool ShouldShowControlAction(OStimActionKind a_kind)
	{
		const auto scene = OStimStateTracker::GetCurrentSceneInfo();
		const bool sceneActive = scene && scene->active;
		const bool sceneControllable = SceneAllowsDirectControls(scene);
		const auto positions = OStimStateTracker::GetAvailablePositions();

		switch (a_kind) {
		case OStimActionKind::StopScene:
			return sceneActive;
		case OStimActionKind::IncreaseSpeed:
		case OStimActionKind::DecreaseSpeed:
			return sceneControllable;
		case OStimActionKind::NextPosition:
		case OStimActionKind::PreviousPosition:
			return sceneControllable && !positions.empty();
		case OStimActionKind::OpenPositionBrowser:
			return sceneControllable &&
			       Config::OStimIntegration::AllowPositionBrowsing &&
			       !positions.empty();
		case OStimActionKind::NextStage:
		case OStimActionKind::PreviousStage:
		case OStimActionKind::SwapPartner:
		case OStimActionKind::ChangeVariant:
			return false;
		default:
			return sceneActive;
		}
	}

	std::vector<std::shared_ptr<WheelItem>> BuildControlWheelItems()
	{
		std::vector<std::shared_ptr<WheelItem>> items;
		items.resize(kControlLayout.size());

		for (std::size_t i = 0; i < kControlLayout.size(); ++i) {
			OStimActionPayload payload{};
			payload.kind = kControlLayout[i];
			payload.displayName = OStimIntegration::GetActionLabel(payload.kind);
			payload.requiresActiveScene = payload.kind != OStimActionKind::OpenControlWheel;

			if (!ShouldShowControlAction(payload.kind) &&
				Config::OStimIntegration::HideInvalidActions) {
				continue;
			}

			items[i] = WheelItemFactory::MakeOStimActionItem(std::move(payload));
		}

		return items;
	}

	bool PositionOpensSubmenu(const OStimPositionInfo& a_position)
	{
		return !a_position.id.empty() &&
		       OStimPreviewResolver::HasBrowsableChildScenes(a_position.id, a_position.sourceSceneID);
	}

	IntegrationState::BrowserContextLevel& EnsureRootBrowserLevel(bool a_refresh)
	{
		if (s_state.browserStack.empty()) {
			s_state.browserStack.emplace_back();
			a_refresh = true;
		}

		auto& root = s_state.browserStack.front();
		if (a_refresh || root.positions.empty()) {
			const auto preservedPage = root.page;
			const auto preservedFocus = root.focusIndex;
			root = IntegrationState::BrowserContextLevel{};
			root.page = preservedPage;
			root.focusIndex = preservedFocus;
			root.positions = OStimIntegration::GetAvailablePositions();
			HydratePositionDisplayNames(root.positions);
			root.page = ClampBrowserPage(root.page, root.positions);
		}

		return root;
	}

	void CollapseBrowserToRoot(bool a_refreshRoot)
	{
		EnsureRootBrowserLevel(a_refreshRoot);
		if (s_state.browserStack.size() > 1) {
			s_state.browserStack.resize(1);
		}
	}

	std::optional<std::size_t> FindBrowserLevelIndexByScene(std::string_view a_sceneID)
	{
		if (a_sceneID.empty()) {
			return std::nullopt;
		}

		for (std::size_t i = 0; i < s_state.browserStack.size(); ++i) {
			if (SceneKeysEqual(s_state.browserStack[i].sceneID, a_sceneID)) {
				return i;
			}
		}

		return std::nullopt;
	}

	void CollapseBrowserToLevel(std::size_t a_levelIndex)
	{
		if (a_levelIndex >= s_state.browserStack.size()) {
			return;
		}
		s_state.browserStack.resize(a_levelIndex + 1);
	}

	BrowserWheelLayout BuildBrowserWheelLayout(const IntegrationState::BrowserContextLevel& a_level)
	{
		const std::uint32_t page = ClampBrowserPage(a_level.page, a_level.positions);
		const std::size_t perPage = static_cast<std::size_t>(
			(std::max)(4u, Config::OStimIntegration::MaxPositionsPerPage));
		const std::size_t start = static_cast<std::size_t>(page) * perPage;
		const std::size_t end = (std::min)(a_level.positions.size(), start + perPage);
		const std::size_t pageCount = GetBrowserPageCount(a_level.positions);
		const bool isSubmenu = s_state.browserStack.size() > 1;

		BrowserWheelLayout layout{};

		OStimActionPayload returnPayload{};
		returnPayload.kind = isSubmenu ?
			OStimActionKind::ReturnToPositionBrowserParent :
			OStimActionKind::ReturnToControlWheel;
		returnPayload.displayName = OStimIntegration::GetActionLabel(returnPayload.kind);
		returnPayload.requiresActiveScene = false;
		layout.items.push_back(WheelItemFactory::MakeOStimActionItem(std::move(returnPayload)));

		if (a_level.allowDirectSelection && !a_level.sceneID.empty()) {
			OStimActionPayload selectCurrent{};
			selectCurrent.kind = OStimActionKind::SelectSpecificPosition;
			selectCurrent.sceneID = a_level.sceneID;
			selectCurrent.positionID = a_level.sceneID;
			selectCurrent.sourceSceneID = a_level.parentSceneID;
			selectCurrent.displayName = Config::OStimIntegration::ShowPositionNames ?
				a_level.displayName :
				a_level.sceneID;
			selectCurrent.previewPath = a_level.previewPath;
			selectCurrent.iconPath = a_level.iconPath;
			selectCurrent.category = a_level.category;
			selectCurrent.subcategory = a_level.subcategory;
			selectCurrent.requiresActiveScene = true;
			layout.defaultFocusIndex = static_cast<int>(layout.items.size());
			layout.items.push_back(WheelItemFactory::MakeOStimActionItem(std::move(selectCurrent)));
		}

		if (page > 0) {
			OStimActionPayload previousPage{};
			previousPage.kind = OStimActionKind::OpenPositionBrowser;
			previousPage.displayName = "Previous Page";
			previousPage.browserPage = page - 1;
			previousPage.browserFocusIndex = static_cast<int>(layout.items.size());
			previousPage.requiresActiveScene = true;
			layout.previousPageIndex = static_cast<int>(layout.items.size());
			layout.items.push_back(WheelItemFactory::MakeOStimActionItem(std::move(previousPage)));
		}

		for (std::size_t i = start; i < end; ++i) {
			const auto& position = a_level.positions[i];
			OStimActionPayload payload{};
			payload.kind = PositionOpensSubmenu(position) ?
				OStimActionKind::OpenPositionSubmenu :
				OStimActionKind::SelectSpecificPosition;
			payload.sceneID = position.id;
			payload.positionID = position.destinationID.empty() ? position.id : position.destinationID;
			payload.sourceSceneID = position.sourceSceneID;
			payload.previewPath = position.previewPath;
			payload.iconPath = position.iconPath;
			payload.category = position.category;
			payload.subcategory = position.subcategory;
			payload.browserFocusIndex = static_cast<int>(layout.items.size());
			payload.requiresActiveScene = position.requiresActiveScene;
			payload.displayName = Config::OStimIntegration::ShowPositionNames ?
				position.displayName :
				(position.destinationID.empty() ? position.id : position.destinationID);
			if (layout.defaultFocusIndex < 0) {
				layout.defaultFocusIndex = static_cast<int>(layout.items.size());
			}
			layout.items.push_back(WheelItemFactory::MakeOStimActionItem(std::move(payload)));
		}

		if (page + 1 < pageCount) {
			OStimActionPayload nextPage{};
			nextPage.kind = OStimActionKind::OpenPositionBrowser;
			nextPage.displayName = "Next Page";
			nextPage.browserPage = page + 1;
			nextPage.browserFocusIndex = static_cast<int>(layout.items.size());
			nextPage.requiresActiveScene = true;
			layout.nextPageIndex = static_cast<int>(layout.items.size());
			layout.items.push_back(WheelItemFactory::MakeOStimActionItem(std::move(nextPage)));
		}

		if (layout.defaultFocusIndex < 0 && !layout.items.empty()) {
			layout.defaultFocusIndex = 0;
		}

		return layout;
	}

	int ResolveBrowserFocusIndex(
		const BrowserWheelLayout& a_layout,
		BrowserFocusHint a_focusHint,
		int a_preservedFocusIndex)
	{
		switch (a_focusHint) {
		case BrowserFocusHint::PreserveCurrent:
			if (a_preservedFocusIndex >= 0 &&
				a_preservedFocusIndex < static_cast<int>(a_layout.items.size())) {
				return a_preservedFocusIndex;
			}
			break;
		case BrowserFocusHint::PreviousPage:
			if (a_layout.previousPageIndex >= 0) {
				return a_layout.previousPageIndex;
			}
			if (a_layout.nextPageIndex >= 0) {
				return a_layout.nextPageIndex;
			}
			break;
		case BrowserFocusHint::NextPage:
			if (a_layout.nextPageIndex >= 0) {
				return a_layout.nextPageIndex;
			}
			if (a_layout.previousPageIndex >= 0) {
				return a_layout.previousPageIndex;
			}
			break;
		case BrowserFocusHint::None:
		default:
			break;
		}

		return a_layout.defaultFocusIndex;
	}

	bool EnsureControlWheel()
	{
		if (!EnsureManagedWheel(kControlWheelTag, static_cast<std::uint32_t>(kControlLayout.size()))) {
			return false;
		}
		PopulateWheel(kControlWheelTag, BuildControlWheelItems(), -1, IsActiveWheelTagged(kControlWheelTag));
		return true;
	}

	bool EnsureBrowserWheel(BrowserFocusHint a_focusHint, bool a_refreshRoot)
	{
		if (a_refreshRoot) {
			EnsureRootBrowserLevel(true);
		}

		auto* level = GetCurrentBrowserLevel();
		if (!level || !HasBrowserEntries(*level)) {
			return false;
		}

		level->page = ClampBrowserPage(level->page, level->positions);
		const auto layout = BuildBrowserWheelLayout(*level);
		const std::string tag = BuildBrowserWheelTag(level->page);
		int preservedFocusIndex = level->focusIndex;
		if (IsActiveWheelTagged(tag)) {
			if (auto hoveredIndex = GetWheelHoveredEntryIndex(tag); hoveredIndex.has_value()) {
				preservedFocusIndex = *hoveredIndex;
			}
		}
		const int focusIndex = ResolveBrowserFocusIndex(layout, a_focusHint, preservedFocusIndex);
		if (!EnsureManagedWheel(tag, static_cast<std::uint32_t>((std::max<std::size_t>)(1, layout.items.size())))) {
			return false;
		}
		PopulateWheel(tag, layout.items, focusIndex, false);
		level->focusIndex = focusIndex;
		return true;
	}

	bool OpenControlWheel(int a_focusIndex = -1)
	{
		if (!EnsureControlWheel()) {
			return false;
		}

		auto controlIdx = FindWheelIndexByTag(kControlWheelTag);
		if (!controlIdx.has_value() || !SwitchOrSetActiveWheel(*controlIdx)) {
			return false;
		}
		if (a_focusIndex >= 0) {
			Wheeler::SetWheelHoveredEntryIndex(*controlIdx, a_focusIndex, Wheeler::IsWheelerOpen());
		}
		return true;
	}

	bool OpenCurrentBrowserLevel(BrowserFocusHint a_focusHint, bool a_refreshRoot)
	{
		if (!EnsureBrowserWheel(a_focusHint, a_refreshRoot)) {
			return false;
		}

		auto browserIdx = FindWheelIndexByTag(BuildBrowserWheelTag(0));
		if (!browserIdx.has_value() || !SwitchOrSetActiveWheel(*browserIdx)) {
			return false;
		}

		if (const auto* level = GetCurrentBrowserLevel();
			level && level->focusIndex >= 0) {
			Wheeler::SetWheelHoveredEntryIndex(*browserIdx, level->focusIndex, Wheeler::IsWheelerOpen());
		}
		return true;
	}

	bool OpenBrowserPage(std::uint32_t a_page, bool a_forceRoot)
	{
		const bool alreadyBrowsing = IsActiveWheelTagged(kBrowserWheelTag);
		if (a_forceRoot || !alreadyBrowsing) {
			CollapseBrowserToRoot(true);
		} else if (s_state.browserStack.empty()) {
			EnsureRootBrowserLevel(true);
		}

		auto* level = GetCurrentBrowserLevel();
		if (!level) {
			return false;
		}

		const auto previousPage = level->page;
		level->page = ClampBrowserPage(a_page, level->positions);
		const BrowserFocusHint focusHint = alreadyBrowsing ?
			(level->page > previousPage ? BrowserFocusHint::NextPage :
			(level->page < previousPage ? BrowserFocusHint::PreviousPage :
				BrowserFocusHint::PreserveCurrent)) :
			BrowserFocusHint::None;
		return OpenCurrentBrowserLevel(focusHint, false);
	}

	bool OpenPositionSubmenu(const OStimActionPayload& a_payload)
	{
		if (a_payload.sceneID.empty()) {
			return false;
		}

		if (s_state.browserStack.empty()) {
			EnsureRootBrowserLevel(true);
		}
		auto* currentLevel = GetCurrentBrowserLevel();
		if (!currentLevel) {
			return false;
		}

		if (SceneKeysEqual(currentLevel->sceneID, a_payload.sceneID)) {
			DebugLog(
				"[OStimIntegration] submenu duplicate ignored scene='{}' depth={}",
				a_payload.sceneID,
				s_state.browserStack.size());
			return OpenCurrentBrowserLevel(BrowserFocusHint::PreserveCurrent, false);
		}

		if (IsActiveWheelTagged(kBrowserWheelTag)) {
			if (auto hoveredIndex = GetWheelHoveredEntryIndex(kBrowserWheelTag); hoveredIndex.has_value()) {
				currentLevel->focusIndex = *hoveredIndex;
			}
		}
		if (a_payload.browserFocusIndex >= 0) {
			currentLevel->focusIndex = a_payload.browserFocusIndex;
		}

		if (auto existingLevelIndex = FindBrowserLevelIndexByScene(a_payload.sceneID); existingLevelIndex.has_value()) {
			DebugLog(
				"[OStimIntegration] submenu collapse scene='{}' fromDepth={} toDepth={}",
				a_payload.sceneID,
				s_state.browserStack.size(),
				*existingLevelIndex + 1);
			CollapseBrowserToLevel(*existingLevelIndex);
			return OpenCurrentBrowserLevel(BrowserFocusHint::PreserveCurrent, false);
		}

		auto childPositions = OStimPreviewResolver::GetSceneNavigationChildren(
			a_payload.sceneID,
			a_payload.sourceSceneID);
		HydratePositionDisplayNames(childPositions);
		if (childPositions.empty()) {
			DebugLog(
				"[OStimIntegration] submenu missing children scene='{}' parent='{}'",
				a_payload.sceneID,
				currentLevel->sceneID);
			return false;
		}

		IntegrationState::BrowserContextLevel submenu{};
		submenu.sceneID = a_payload.sceneID;
		submenu.parentSceneID = a_payload.sourceSceneID;
		submenu.displayName = a_payload.displayName.empty() ?
			HumanizeBrowserLabel(a_payload.sceneID) :
			a_payload.displayName;
		submenu.previewPath = a_payload.previewPath;
		submenu.iconPath = a_payload.iconPath;
		submenu.category = a_payload.category;
		submenu.subcategory = a_payload.subcategory;
		submenu.positions = std::move(childPositions);
		submenu.parentReturnFocusIndex = currentLevel->focusIndex;
		submenu.allowDirectSelection = true;

		DebugLog(
			"[OStimIntegration] submenu push scene='{}' parent='{}' depth={} entries={} returnFocus={}",
			submenu.sceneID,
			currentLevel->sceneID,
			s_state.browserStack.size() + 1,
			submenu.positions.size(),
			submenu.parentReturnFocusIndex);
		s_state.browserStack.push_back(std::move(submenu));
		return OpenCurrentBrowserLevel(BrowserFocusHint::None, false);
	}

	bool ReturnToParentBrowser()
	{
		if (s_state.browserStack.size() <= 1) {
			return false;
		}

		const auto childLevel = std::move(s_state.browserStack.back());
		s_state.browserStack.pop_back();
		int restoredFocusIndex = childLevel.parentReturnFocusIndex;
		const std::string childSceneID = childLevel.sceneID;

		while (s_state.browserStack.size() > 1) {
			auto* duplicateLevel = GetCurrentBrowserLevel();
			if (!duplicateLevel || !SceneKeysEqual(duplicateLevel->sceneID, childSceneID)) {
				break;
			}

			if (duplicateLevel->parentReturnFocusIndex >= 0) {
				restoredFocusIndex = duplicateLevel->parentReturnFocusIndex;
			}

			DebugLog(
				"[OStimIntegration] browser back skipping duplicate scene='{}' depth={}",
				duplicateLevel->sceneID,
				s_state.browserStack.size());
			s_state.browserStack.pop_back();
		}

		auto* parentLevel = GetCurrentBrowserLevel();
		if (!parentLevel) {
			return false;
		}

		if (restoredFocusIndex >= 0) {
			parentLevel->focusIndex = restoredFocusIndex;
		}

		DebugLog(
			"[OStimIntegration] browser back child='{}' depth={} focus={}",
			childSceneID,
			s_state.browserStack.size(),
			parentLevel->focusIndex);
		return OpenCurrentBrowserLevel(BrowserFocusHint::PreserveCurrent, false);
	}

	bool QueueWheelNavigationTask(OStimActionKind a_kind, OStimActionPayload a_payload)
	{
		auto* taskInterface = SKSE::GetTaskInterface();
		if (!taskInterface) {
			DebugLog("[OStimIntegration] navigation action={} skipped because task interface is unavailable", static_cast<std::uint32_t>(a_kind));
			return false;
		}

		taskInterface->AddTask([kind = a_kind, payload = std::move(a_payload)]() {
			if (!OStimIntegration::CanExecuteAction(kind, &payload)) {
				DebugLog("[OStimIntegration] navigation action={} failed guards before queued execution", static_cast<std::uint32_t>(kind));
				return;
			}

			switch (kind) {
			case OStimActionKind::OpenControlWheel:
				OpenControlWheel(payload.browserFocusIndex);
				break;
			case OStimActionKind::OpenPositionSubmenu:
				OpenPositionSubmenu(payload);
				break;
			case OStimActionKind::ReturnToPositionBrowserParent:
				ReturnToParentBrowser();
				break;
			case OStimActionKind::ReturnToControlWheel:
				InvalidateBrowserState();
				OpenControlWheel(payload.browserFocusIndex >= 0 ?
					payload.browserFocusIndex :
					GetControlWheelEntryIndex(OStimActionKind::OpenPositionBrowser).value_or(-1));
				break;
			case OStimActionKind::OpenPositionBrowser:
				OpenBrowserPage(payload.browserPage, !IsActiveWheelTagged(kBrowserWheelTag));
				break;
			default:
				break;
			}
		});

		return true;
	}

	std::optional<std::size_t> FindCurrentPositionIndex(
		const std::vector<OStimPositionInfo>& a_positions,
		const std::string& a_sceneID)
	{
		for (std::size_t i = 0; i < a_positions.size(); ++i) {
			if (a_positions[i].id == a_sceneID || a_positions[i].destinationID == a_sceneID) {
				return i;
			}
		}
		return std::nullopt;
	}

	bool RunActionNow(OStimActionKind a_kind, const OStimActionPayload* a_payload)
	{
		if (IsSceneBlockedByMenus()) {
			return false;
		}
		if (!RE::PlayerCharacter::GetSingleton() ||
			!RE::PlayerCharacter::GetSingleton()->Is3DLoaded()) {
			return false;
		}

		switch (a_kind) {
		case OStimActionKind::StopScene:
			return OStimBridge::CallAPIMethod("EndAnimation", true);
		case OStimActionKind::IncreaseSpeed:
			return OStimNGThreadAPI::AdjustSpeed(+1) || OStimBridge::CallAPIMethod("IncreaseAnimationSpeed");
		case OStimActionKind::DecreaseSpeed:
			return OStimNGThreadAPI::AdjustSpeed(-1) || OStimBridge::CallAPIMethod("DecreaseAnimationSpeed");
		case OStimActionKind::SelectSpecificPosition:
			return a_payload &&
			       !a_payload->sceneID.empty() &&
			       (OStimNGThreadAPI::NavigateToScene(a_payload->sceneID) ||
			        OStimBridge::CallAPIMethod("TravelToAnimationIfPossible", a_payload->sceneID));
		case OStimActionKind::NextPosition:
		case OStimActionKind::PreviousPosition:
		{
			const auto scene = OStimBridge::GetCurrentSceneInfo();
			if (!SceneAllowsDirectControls(scene)) {
				return false;
			}

			auto positions = GetActionPositions(*scene);
			if (positions.empty()) {
				return false;
			}

			std::size_t targetIndex = 0;
			if (auto currentIndex = FindCurrentPositionIndex(positions, scene->sceneID); currentIndex.has_value()) {
				const std::size_t delta = a_kind == OStimActionKind::NextPosition ? 1 : positions.size() - 1;
				targetIndex = ((*currentIndex) + delta) % positions.size();
			} else {
				targetIndex = a_kind == OStimActionKind::NextPosition ? 0 : positions.size() - 1;
			}

			return NavigateToPosition(positions[targetIndex]);
		}
		default:
			return false;
		}
	}

	bool QueuePapyrusAction(OStimActionKind a_kind, const OStimActionPayload* a_payload)
	{
		auto* taskInterface = SKSE::GetTaskInterface();
		if (!taskInterface) {
			DebugLog("[OStimIntegration] action={} skipped because task interface is unavailable", static_cast<std::uint32_t>(a_kind));
			return false;
		}

		OStimActionPayload payload{};
		if (a_payload) {
			payload = *a_payload;
		} else {
			payload.kind = a_kind;
		}
	taskInterface->AddTask([payload]() {
		if (RunActionNow(payload.kind, &payload)) {
			if (payload.kind == OStimActionKind::StopScene) {
				s_state.suppressManagedWheelsUntilSceneStops = true;
				DeleteManagedWheels();
				OStimIntegration::RequestRefresh();
			}
			OStimStateTracker::MarkActionExecuted(payload.kind);
		} else {
			DebugLog("[OStimIntegration] action={} failed guards or scene dispatch", static_cast<std::uint32_t>(payload.kind));
		}
	});
		return true;
	}

	void RebuildManagedWheelsIfNeeded(std::uint64_t a_revision)
	{
		if (!Config::OStimIntegration::CreateManagedWheel) {
			DeleteManagedWheels();
			s_state.lastAppliedRevision = a_revision;
			return;
		}

		if (Wheeler::IsWheelerOpen()) {
			if (auto activeIdx = Wheeler::GetActiveWheelIndex();
				OStimIntegration::IsManagedWheelIndex(activeIdx)) {
				std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
				if (Wheel* wheel = Wheeler::GetWheelByIndex(activeIdx)) {
					DebugLog(
						"[OStimIntegration] deferred live refresh revision={} tag='{}' depth={}",
						a_revision,
						wheel->GetClientTag(),
						s_state.browserStack.size());
					s_state.lastAppliedRevision = a_revision;
					return;
				}
			}
		}

		EnsureControlWheel();
		if (auto activeIdx = Wheeler::GetActiveWheelIndex();
			OStimIntegration::IsManagedWheelIndex(activeIdx)) {
			std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
			if (Wheel* wheel = Wheeler::GetWheelByIndex(activeIdx)) {
				if (IsBrowserWheelTag(wheel->GetClientTag())) {
					lock.unlock();
					DebugLog(
						"[OStimIntegration] browser refresh requested while not open revision={} depth={}",
						a_revision,
						s_state.browserStack.size());
				}
			}
		}

		s_state.lastAppliedRevision = a_revision;
	}
}

void OStimIntegration::Init()
{
	s_state = IntegrationState{};
	OStimStateTracker::Reset();
	RequestRefresh();
}

void OStimIntegration::Reset()
{
	s_state = IntegrationState{};
	OStimStateTracker::Reset();
}

void OStimIntegration::Update()
{
	if (Config::OStimIntegration::Enabled || Config::OStimIntegration::AutoDetect) {
		OStimStateTracker::Update(s_state.refreshRequested);
	}
	s_state.refreshRequested = false;

	const auto availability = OStimStateTracker::GetAvailability();
	const bool sceneActive = availability.available && OStimStateTracker::IsSceneActive();

	if (availability.available != s_state.lastAvailable ||
		availability.reason != s_state.lastAvailabilityReason) {
		DebugLog(
			"[OStimIntegration] availability={} reason='{}' apiVersion={} nativeThreadApi={} database={}",
			availability.available,
			availability.reason,
			availability.apiVersion,
			availability.hasNativeThreadAPI,
			availability.hasDatabase);
		s_state.lastAvailable = availability.available;
		s_state.lastAvailabilityReason = availability.reason;
	}
	if (sceneActive != s_state.lastSceneActive) {
		DebugLog("[OStimIntegration] sceneActive={}", sceneActive);
	}

	if (!Config::OStimIntegration::Enabled || !availability.available) {
		if (!CollectManagedWheelIndices().empty()) {
			DeleteManagedWheels();
		}
		s_state.suppressManagedWheelsUntilSceneStops = false;
		s_state.lastSceneActive = false;
		s_state.lastAppliedRevision = OStimStateTracker::GetRevision();
		return;
	}

	const std::uint64_t revision = OStimStateTracker::GetRevision();
	if (s_state.suppressManagedWheelsUntilSceneStops) {
		if (!CollectManagedWheelIndices().empty()) {
			DeleteManagedWheels();
		}
		if (!sceneActive) {
			s_state.suppressManagedWheelsUntilSceneStops = false;
		}
		s_state.lastSceneActive = sceneActive;
		s_state.lastAppliedRevision = revision;
		return;
	}

	if (sceneActive) {
		if (!s_state.lastSceneActive && Config::OStimIntegration::AutoSwitchToSceneWheel) {
			RememberPreviousWheelIfNeeded();
		}

		if (revision != s_state.lastAppliedRevision || !s_state.lastSceneActive) {
			RebuildManagedWheelsIfNeeded(revision);
		}

		if (!s_state.lastSceneActive &&
			Config::OStimIntegration::AutoSwitchToSceneWheel &&
			Config::OStimIntegration::CreateManagedWheel) {
			OpenControlWheel();
		}
	} else if (s_state.lastSceneActive || !CollectManagedWheelIndices().empty()) {
		DeleteManagedWheels();
		s_state.suppressManagedWheelsUntilSceneStops = false;
	}

	s_state.lastSceneActive = sceneActive;
	if (!sceneActive) {
		s_state.lastAppliedRevision = revision;
	}
}

void OStimIntegration::RequestRefresh()
{
	s_state.refreshRequested = true;
	OStimStateTracker::Update(true);
}

bool OStimIntegration::IsAvailable()
{
	return OStimStateTracker::GetAvailability().available;
}

bool OStimIntegration::IsEnabled()
{
	return Config::OStimIntegration::Enabled && IsAvailable();
}

bool OStimIntegration::IsSceneActive()
{
	return IsEnabled() && OStimStateTracker::IsSceneActive();
}

bool OStimIntegration::CanExecuteAction(OStimActionKind a_kind, const OStimActionPayload* a_payload)
{
	if (!IsEnabled()) {
		return false;
	}

	const auto scene = OStimStateTracker::GetCurrentSceneInfo();
	const bool sceneActive = scene && scene->active;
	const bool sceneControllable = SceneAllowsDirectControls(scene);
	const auto positions = OStimStateTracker::GetAvailablePositions();
	const bool hasBrowserSnapshot = HasBrowserSnapshot();
	const bool browsingManagedWheel = IsActiveWheelTagged(kBrowserWheelTag);

	switch (a_kind) {
	case OStimActionKind::OpenControlWheel:
	case OStimActionKind::ReturnToControlWheel:
		return Config::OStimIntegration::CreateManagedWheel && sceneActive;
	case OStimActionKind::OpenPositionBrowser:
		return Config::OStimIntegration::CreateManagedWheel &&
		       Config::OStimIntegration::AllowPositionBrowsing &&
		       ((sceneControllable && (!positions.empty() || hasBrowserSnapshot)) ||
		        (browsingManagedWheel && hasBrowserSnapshot));
	case OStimActionKind::OpenPositionSubmenu:
		return Config::OStimIntegration::CreateManagedWheel &&
		       Config::OStimIntegration::AllowPositionBrowsing &&
		       a_payload &&
		       !a_payload->sceneID.empty() &&
		       ((sceneControllable && (!positions.empty() || hasBrowserSnapshot)) ||
		        (browsingManagedWheel && hasBrowserSnapshot)) &&
		       OStimPreviewResolver::HasBrowsableChildScenes(a_payload->sceneID, a_payload->sourceSceneID);
	case OStimActionKind::ReturnToPositionBrowserParent:
		return Config::OStimIntegration::CreateManagedWheel &&
		       browsingManagedWheel &&
		       s_state.browserStack.size() > 1;
	case OStimActionKind::StopScene:
		return sceneActive && OStimStateTracker::CanDispatchByCooldown(a_kind);
	case OStimActionKind::IncreaseSpeed:
	case OStimActionKind::DecreaseSpeed:
		return sceneControllable && OStimStateTracker::CanDispatchByCooldown(a_kind);
	case OStimActionKind::NextPosition:
	case OStimActionKind::PreviousPosition:
		return sceneControllable &&
		       !positions.empty() &&
		       OStimStateTracker::CanDispatchByCooldown(a_kind);
	case OStimActionKind::SelectSpecificPosition:
		return sceneControllable &&
		       a_payload &&
		       !a_payload->sceneID.empty() &&
		       OStimStateTracker::CanDispatchByCooldown(a_kind);
	case OStimActionKind::NextStage:
	case OStimActionKind::PreviousStage:
	case OStimActionKind::SwapPartner:
	case OStimActionKind::ChangeVariant:
		return false;
	default:
		return false;
	}
}

bool OStimIntegration::ExecuteAction(OStimActionKind a_kind, const OStimActionPayload* a_payload)
{
	if (!CanExecuteAction(a_kind, a_payload)) {
		return false;
	}

	switch (a_kind) {
	case OStimActionKind::OpenControlWheel:
	{
		OStimActionPayload payload = a_payload ? *a_payload : OStimActionPayload{};
		payload.kind = a_kind;
		return QueueWheelNavigationTask(a_kind, std::move(payload));
	}
	case OStimActionKind::OpenPositionSubmenu:
	case OStimActionKind::ReturnToPositionBrowserParent:
	case OStimActionKind::ReturnToControlWheel:
	{
		OStimActionPayload payload = a_payload ? *a_payload : OStimActionPayload{};
		payload.kind = a_kind;
		if (a_kind == OStimActionKind::ReturnToControlWheel &&
			payload.browserFocusIndex < 0) {
			payload.browserFocusIndex = GetControlWheelEntryIndex(OStimActionKind::OpenPositionBrowser).value_or(-1);
		}
		return QueueWheelNavigationTask(a_kind, std::move(payload));
	}
	case OStimActionKind::OpenPositionBrowser:
	{
		OStimActionPayload payload = a_payload ? *a_payload : OStimActionPayload{};
		payload.kind = a_kind;
		return QueueWheelNavigationTask(a_kind, std::move(payload));
	}
	case OStimActionKind::StopScene:
	case OStimActionKind::IncreaseSpeed:
	case OStimActionKind::DecreaseSpeed:
	case OStimActionKind::NextPosition:
	case OStimActionKind::PreviousPosition:
	case OStimActionKind::SelectSpecificPosition:
		return QueuePapyrusAction(a_kind, a_payload);
	default:
		return false;
	}
}

std::vector<OStimPositionInfo> OStimIntegration::GetAvailablePositions()
{
	if (!IsEnabled()) {
		return {};
	}
	return OStimStateTracker::GetAvailablePositions();
}

std::optional<OStimSceneInfo> OStimIntegration::GetCurrentSceneInfo()
{
	if (!IsEnabled()) {
		return std::nullopt;
	}
	return OStimStateTracker::GetCurrentSceneInfo();
}

bool OStimIntegration::ShouldBlockRegularWheelActivation(std::string_view a_itemTypeName)
{
	if (!Config::OStimIntegration::Enabled ||
		!Config::OStimIntegration::RestrictRegularWheelActionsDuringScenes ||
		!IsSceneActive()) {
		return false;
	}

	return a_itemTypeName != "WheelItemOStimAction";
}

const char* OStimIntegration::GetActionLabel(OStimActionKind a_kind)
{
	switch (a_kind) {
	case OStimActionKind::OpenControlWheel:
		return "OStim Controls";
	case OStimActionKind::OpenPositionBrowser:
		return "Browse Positions";
	case OStimActionKind::OpenPositionSubmenu:
		return "Open Submenu";
	case OStimActionKind::ReturnToPositionBrowserParent:
		return "Back";
	case OStimActionKind::ReturnToControlWheel:
		return "Back To Controls";
	case OStimActionKind::StopScene:
		return "Stop Scene";
	case OStimActionKind::NextStage:
		return "Next Stage";
	case OStimActionKind::PreviousStage:
		return "Previous Stage";
	case OStimActionKind::NextPosition:
		return "Next Position";
	case OStimActionKind::PreviousPosition:
		return "Previous Position";
	case OStimActionKind::IncreaseSpeed:
		return "Speed Up";
	case OStimActionKind::DecreaseSpeed:
		return "Speed Down";
	case OStimActionKind::SwapPartner:
		return "Swap Partner";
	case OStimActionKind::ChangeVariant:
		return "Change Variant";
	case OStimActionKind::SelectSpecificPosition:
		return "Select Position";
	default:
		return "OStim";
	}
}

bool OStimIntegration::IsManagedWheelTag(std::string_view a_tag)
{
	return IsManagedTagInternal(a_tag);
}

bool OStimIntegration::IsManagedWheelIndex(int a_wheelIndex)
{
	if (a_wheelIndex < 0) {
		return false;
	}

	std::shared_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
	Wheel* wheel = Wheeler::GetWheelByIndex(a_wheelIndex);
	return wheel && IsManagedTagInternal(wheel->GetClientTag());
}
