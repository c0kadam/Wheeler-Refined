#include "TransformWheelManager.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "imgui.h"

#include "bin/API/WheelerAPI.h"
#include "bin/Config.h"
#include "bin/Wheeler/Wheel.h"
#include "bin/Wheeler/WheelEntry.h"
#include "bin/Wheeler/WheelItems/WheelItemFactory.h"
#include "bin/Wheeler/Wheeler.h"

#include "RE/B/BSContainer.h"
#include "RE/B/BGSListForm.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESLevSpell.h"
#include "RE/T/TESNPC.h"
#include "RE/T/TESRace.h"
#include "RE/T/TESShout.h"
#include "RE/T/TESObjectWEAP.h"
#include "RE/T/TESSpellList.h"

namespace
{
	constexpr const char* kSkyrimPlugin = "Skyrim.esm";
	constexpr const char* kDawnguardPlugin = "Dawnguard.esm";
	constexpr RE::FormID kBeastFormPowerFormID = 0x00092C48;
	constexpr RE::FormID kWerewolfRaceFormID = 0x000CDD84;
	constexpr RE::FormID kVampireLordRaceFormID = 0x0000283A;
	constexpr RE::FormID kVampireLordPowerFormID = 0x0000283B;
	constexpr RE::FormID kVampireLordForcedDrainFormID = 0x00019AD5;
	constexpr RE::FormID kVampireLordSpellsPowersFormListID = 0x00019AD9;
	constexpr std::array<RE::FormID, 5> kVampireLordRaiseDeadRelativeFormIDs{
		0x0000BA54,
		0x00013EC8,
		0x00013EC9,
		0x00013ECA,
		0x00013ECB
	};
	constexpr std::uint32_t kVampireLordModeDebounceSamples = 3;
	constexpr double kVampireLordModeDebounceSeconds = 0.100;
	constexpr const char* kWerewolfRaceEditorId = "WerewolfBeastRace";
	constexpr const char* kVampireLordRaceEditorId = "DLC1VampireBeastRace";
	constexpr const char* kWerewolfWheelId = "Wheel_Werewolf";
	constexpr const char* kVampireLordWheelId = "Wheel_VampireLord";
	constexpr const char* kGenericWheelIdFallback = "Wheel_GenericTransform";
	constexpr const char* kGenericStateIdFallback = "GenericTransform";
	constexpr const char* kLichWheelIdFallback = "Wheel_LichForm";
	constexpr const char* kLichStateIdFallback = "LichForm";
	constexpr std::size_t kTransformWheelPageSize = 10;
	constexpr std::size_t kMaxDeltaSpellsAllowed = 200;
	constexpr std::size_t kMaxTransformEntriesTotal = 50;

	struct FormWheelInfo
	{
		std::optional<int> index;
		std::uint64_t lastHash = 0;
		bool hasHash = false;
		double lastGeneratedAt = 0.0;
		std::size_t pageCount = 0;
	};

	struct GenericTriggerConfigCache
	{
		bool initialized = false;
		std::size_t signature = 0;
		bool enabled = false;
		bool hasAnyMatchers = false;
		std::string stateId;
		std::string wheelId;
		std::vector<std::string> raceEditorIdContains;
		std::vector<std::string> raceKeywords;
		std::unordered_set<RE::FormID> raceFormIDs;
	};

	struct VampireLordConfigCache
	{
		bool initialized = false;
		std::size_t signature = 0;
		bool enabled = true;
		std::string populateMode = "Delta";
		bool hideTransformSpell = true;
		bool hideForcedRightHandSpells = true;
		bool blockHiddenSpellActivation = true;
		bool blockRegularSpellsInMeleeMode = true;
		bool debugLog = false;
		std::vector<std::string> raceEditorIdContains;
		std::vector<std::string> raceKeywords;
		std::unordered_set<RE::FormID> raceFormIDs;
		std::unordered_set<RE::FormID> additionalSpellFormIDs;
		std::unordered_set<RE::FormID> hiddenSpellFormIDs;
		std::vector<std::string> spellTokens;
		std::vector<std::string> exitSpellTokens;
		std::vector<std::string> hiddenSpellTokens;
	};

	struct WerewolfConfigCache
	{
		bool initialized = false;
		std::size_t signature = 0;
		bool enabled = true;
		std::string populateMode = "Delta";
		bool debugLog = false;
		std::vector<std::string> raceEditorIdContains;
		std::vector<std::string> raceKeywords;
		std::unordered_set<RE::FormID> raceFormIDs;
		std::unordered_set<RE::FormID> additionalSpellFormIDs;
		std::vector<std::string> spellTokens;
		std::vector<std::string> exitSpellTokens;
	};

	enum class LichMode : std::uint8_t
	{
		Overlay = 0,
		Replace = 1,
		Disabled = 2
	};

	enum class LichPopulateMode : std::uint8_t
	{
		ModList = 0,
		Delta = 1,
		Manual = 2
	};

	enum class LichTransformGuardMode : std::uint8_t
	{
		Off = 0,
		BlockOtherTransforms = 1,
		BlockAllTransformsExceptExit = 2
	};

	enum class VampireLordCombatMode : std::uint8_t
	{
		Unknown = 0,
		Melee = 1,
		BloodMagic = 2
	};

	enum class VampireLordModeObservationStatus : std::uint8_t
	{
		Candidate = 0,
		InitialTransform,
		GraphUnavailableOrMixed,
		ActorUnsettled,
		DirectCastActive
	};

	struct VampireLordModeDebounceState
	{
		VampireLordCombatMode candidate = VampireLordCombatMode::Unknown;
		std::uint32_t samples = 0;
		double since = 0.0;
	};

	constexpr std::optional<VampireLordCombatMode> ObserveVampireLordModeCandidate(
		VampireLordModeDebounceState& state,
		VampireLordCombatMode candidate,
		double now,
		bool acceptSample)
	{
		if (!acceptSample || candidate == VampireLordCombatMode::Unknown) {
			state = {};
			return std::nullopt;
		}
		if (candidate != state.candidate) {
			state.candidate = candidate;
			state.samples = 1;
			state.since = now;
			return std::nullopt;
		}

		if (state.samples < kVampireLordModeDebounceSamples) {
			++state.samples;
		}
		if (state.samples >= kVampireLordModeDebounceSamples &&
			(now - state.since) >= kVampireLordModeDebounceSeconds) {
			return candidate;
		}
		return std::nullopt;
	}

	constexpr bool IsVampireLordRaiseDeadRelativeID(RE::FormID relativeID)
	{
		for (const auto familyID : kVampireLordRaiseDeadRelativeFormIDs) {
			if (relativeID == familyID) {
				return true;
			}
		}
		return false;
	}

	template <class Range>
	constexpr bool ContainsFormID(const Range& forms, RE::FormID formID)
	{
		for (const auto candidate : forms) {
			if (candidate == formID) {
				return true;
			}
		}
		return false;
	}

	template <class CandidateRange, class EquippedRange, class KnownRange>
	constexpr RE::FormID SelectVampireLordRaiseDeadRepresentative(
		const CandidateRange& candidates,
		const EquippedRange& equipped,
		RE::FormID previous,
		const KnownRange& known,
		bool directCastActive)
	{
		if (!directCastActive) {
			for (const auto formID : equipped) {
				if (ContainsFormID(candidates, formID)) {
					return formID;
				}
			}
		}
		if (previous != 0 && ContainsFormID(candidates, previous)) {
			return previous;
		}
		for (const auto formID : known) {
			if (ContainsFormID(candidates, formID)) {
				return formID;
			}
		}
		for (const auto formID : candidates) {
			return formID;
		}
		return 0;
	}

	constexpr bool VerifyVampireLordModeDebouncePolicy()
	{
		VampireLordModeDebounceState state;
		if (ObserveVampireLordModeCandidate(state, VampireLordCombatMode::BloodMagic, 0.000, true)) {
			return false;
		}
		if (ObserveVampireLordModeCandidate(state, VampireLordCombatMode::BloodMagic, 0.050, true)) {
			return false;
		}
		if (ObserveVampireLordModeCandidate(state, VampireLordCombatMode::BloodMagic, 0.100, true) != VampireLordCombatMode::BloodMagic) {
			return false;
		}
		if (ObserveVampireLordModeCandidate(state, VampireLordCombatMode::Melee, 0.200, true)) {
			return false;
		}
		if (ObserveVampireLordModeCandidate(state, VampireLordCombatMode::BloodMagic, 0.210, true) ||
			state.candidate != VampireLordCombatMode::BloodMagic || state.samples != 1) {
			return false;
		}
		if (ObserveVampireLordModeCandidate(state, VampireLordCombatMode::Unknown, 0.250, true) || state.samples != 0) {
			return false;
		}
		if (ObserveVampireLordModeCandidate(state, VampireLordCombatMode::Melee, 0.300, false) || state.samples != 0) {
			return false;
		}
		if (ObserveVampireLordModeCandidate(state, VampireLordCombatMode::Melee, 0.400, true) ||
			ObserveVampireLordModeCandidate(state, VampireLordCombatMode::Melee, 0.450, true) ||
			ObserveVampireLordModeCandidate(state, VampireLordCombatMode::Melee, 0.510, true) != VampireLordCombatMode::Melee) {
			return false;
		}
		return true;
	}

	constexpr bool VerifyVampireLordRaiseDeadPolicy()
	{
		constexpr std::array<RE::FormID, 2> candidates{ 0x0000BA54, 0x00013ECA };
		constexpr std::array<RE::FormID, 1> equipped{ 0x00013ECA };
		constexpr std::array<RE::FormID, 1> known{ 0x0000BA54 };
		constexpr std::array<RE::FormID, 0> none{};
		return SelectVampireLordRaiseDeadRepresentative(candidates, equipped, 0x0000BA54, known, false) == 0x00013ECA &&
			SelectVampireLordRaiseDeadRepresentative(candidates, none, 0x00013ECA, known, false) == 0x00013ECA &&
			SelectVampireLordRaiseDeadRepresentative(candidates, equipped, 0x0000BA54, known, true) == 0x0000BA54 &&
			SelectVampireLordRaiseDeadRepresentative(candidates, none, 0, known, false) == 0x0000BA54 &&
			SelectVampireLordRaiseDeadRepresentative(candidates, none, 0, none, false) == 0x0000BA54 &&
			IsVampireLordRaiseDeadRelativeID(0x00013ECB) &&
			!IsVampireLordRaiseDeadRelativeID(0x00ABCDEF);
	}

	static_assert(VerifyVampireLordModeDebouncePolicy());
	static_assert(VerifyVampireLordRaiseDeadPolicy());

	struct VampireLordModeDiagnosticForm
	{
		RE::FormID formID = 0;
		std::uint32_t formType = 0;
		std::int32_t weaponType = -1;
		std::int32_t spellType = -1;
		std::int32_t castingType = -1;
		std::string editorID;
		std::string name;
		std::string source;

		bool operator==(const VampireLordModeDiagnosticForm&) const = default;
	};

	template <class T>
	struct VampireLordModeDiagnosticGraphValue
	{
		bool queried = false;
		T value{};

		bool operator==(const VampireLordModeDiagnosticGraphValue&) const = default;
	};

	struct VampireLordModeDiagnosticSnapshot
	{
		VampireLordCombatMode rawMode = VampireLordCombatMode::Unknown;
		VampireLordCombatMode handObjectMode = VampireLordCombatMode::Unknown;
		VampireLordCombatMode effectiveMode = VampireLordCombatMode::Unknown;
		VampireLordCombatMode debounceCandidate = VampireLordCombatMode::Unknown;
		VampireLordModeObservationStatus observationStatus = VampireLordModeObservationStatus::GraphUnavailableOrMixed;
		VampireLordModeDiagnosticForm left;
		VampireLordModeDiagnosticForm right;
		VampireLordModeDiagnosticForm selectedPower;
		VampireLordModeDiagnosticForm race;
		bool actorStateAvailable = false;
		std::uint32_t weaponState = 0;
		std::uint32_t attackState = 0;
		std::uint32_t lifeState = 0;
		bool weaponDrawn = false;
		bool inCombat = false;
		bool actorSettled = false;
		bool directCastActive = false;
		VampireLordModeDiagnosticGraphValue<std::uint32_t> leftCasterState;
		VampireLordModeDiagnosticGraphValue<std::uint32_t> rightCasterState;
		VampireLordModeDiagnosticGraphValue<bool> equipOK;
		VampireLordModeDiagnosticGraphValue<bool> attackReady;
		VampireLordModeDiagnosticGraphValue<bool> leftMagicReady;
		VampireLordModeDiagnosticGraphValue<bool> rightMagicReady;
		VampireLordModeDiagnosticGraphValue<bool> leftHand;
		VampireLordModeDiagnosticGraphValue<bool> rightHand;
		VampireLordModeDiagnosticGraphValue<int> leftHandType;
		VampireLordModeDiagnosticGraphValue<int> rightHandType;
		VampireLordModeDiagnosticGraphValue<int> leftHandEquipped;
		VampireLordModeDiagnosticGraphValue<int> rightHandEquipped;

		bool operator==(const VampireLordModeDiagnosticSnapshot&) const = default;
	};

	struct LichConfigCache
	{
		bool initialized = false;
		std::size_t signature = 0;
		bool enabled = true;
		std::string stateId;
		std::string wheelId;
		LichMode mode = LichMode::Overlay;
		LichPopulateMode populateMode = LichPopulateMode::ModList;
		bool allowBaseWheel = true;
		LichTransformGuardMode transformGuard = LichTransformGuardMode::BlockOtherTransforms;
		bool blockBoundSpells = false;
		bool hideWeapons = false;
		bool hideGear = false;
		bool blockStaffSwapping = true;
		bool suppressDirectCast = true;
		bool blockHiddenSpellActivation = true;
		bool debugLog = false;
		bool hasAnyMatchers = false;
		std::vector<std::string> raceEditorIdContains;
		std::vector<std::string> raceKeywords;
		std::unordered_set<RE::FormID> raceFormIDs;
		std::unordered_set<RE::FormID> additionalSpellFormIDs;
		std::unordered_set<RE::FormID> hiddenSpellFormIDs;
		std::vector<std::string> spellTokens;
		std::vector<std::string> exitSpellTokens;
		std::vector<std::string> hiddenSpellTokens;
	};

	struct PendingTransform
	{
		bool active = false;
		TransformState state = TransformState::Human;
		RE::FormID genericRaceContext = 0;
		double startedAt = 0.0;
		double nextPollAt = 0.0;
		double expiresAt = 0.0;
		std::uint64_t lastHash = 0;
		bool hasLastHash = false;
		std::uint32_t stableTicks = 0;
		bool switched = false;
		std::size_t lastEligible = 0;
		// Monotonic set during pending - forms can only be added, never removed
		std::unordered_set<RE::FormID> latchedForms;
	};

	// Late refresh: poll for newly-added spells after wheel opens
	struct LateRefresh
	{
		bool active = false;
		int triesLeft = 0;
		double nextPollAt = 0.0;
		std::uint64_t lastHash = 0;
		static constexpr int kMaxTries = 12;        // ~1.5s at 125ms intervals
		static constexpr double kPollIntervalSec = 0.125;
	};
	static LateRefresh s_lateRefresh;

	static TransformState s_lastState = TransformState::Human;
	static std::optional<int> s_savedHumanWheelIdx;
	static std::unordered_set<RE::FormID> s_humanSpellSnapshot;
	static std::unordered_set<RE::FormID> s_humanShoutSnapshot;
	static std::unordered_set<RE::FormID> s_humanEquippedSpellSnapshot;
	static double s_lastHumanSnapshotAt = 0.0;
	static FormWheelInfo s_werewolfWheel;
	static FormWheelInfo s_vampireWheel;
	static FormWheelInfo s_genericWheel;
	static FormWheelInfo s_lichWheel;
	static PendingTransform s_pendingTransform;
	static bool s_switchedToTransform = false;
	static GenericTriggerConfigCache s_genericTriggerCache;
	static VampireLordConfigCache s_vampireLordConfigCache;
	static WerewolfConfigCache s_werewolfConfigCache;
	static LichConfigCache s_lichConfigCache;
	static std::unordered_set<RE::FormID> s_vampireLordSessionSpellForms;
	static std::unordered_set<RE::FormID> s_vampireLordLoggedRejectedEquippedForms;
	static VampireLordCombatMode s_lastVampireLordCombatMode = VampireLordCombatMode::Unknown;
	static VampireLordModeDebounceState s_vampireLordModeDebounce;
	static bool s_loggedVampireLordUnknownHold = false;
	static std::unordered_set<std::uint64_t> s_loggedVampireLordModeHiddenForms;
	static std::optional<VampireLordModeDiagnosticSnapshot> s_lastVampireLordModeDiagnosticSnapshot;
	static RE::FormID s_vampireLordRaiseDeadRepresentative = 0;
	static std::uint64_t s_lastVampireLordRaiseDeadLogSignature = 0;
	static bool s_hasVampireLordRaiseDeadLogSignature = false;
	static std::vector<RE::FormID> s_lichStableSpellOrder;
	static std::unordered_set<RE::FormID> s_loggedLichRaceMatches;
	static std::vector<std::string> s_knownGenericWheelIds{ std::string(kGenericWheelIdFallback) };
	static std::vector<std::string> s_knownLichWheelIds{ std::string(kLichWheelIdFallback) };
	static RE::FormID s_activeGenericRaceContext = 0;
	static RE::FormID s_lastGenericRaceContext = 0;
	static std::size_t s_precedenceSignature = 0;
	static std::vector<TransformState> s_precedenceOrder{ TransformState::Werewolf, TransformState::VampireLord, TransformState::Lich, TransformState::Generic };

	enum class MajorTransformKind : std::uint8_t
	{
		None = 0,
		Werewolf,
		VampireLord,
		Lich,
		Polymorph
	};

	bool IsTransformKitSpell(RE::SpellItem* spell, TransformState state);
	bool IsTransformKitShout(RE::TESShout* shout, TransformState state);
	bool IsActivatableTransformSpell(RE::SpellItem* spell, TransformState state);
	bool IsLichExitSpell(RE::SpellItem* spell);
	TransformState GetCurrentTransformState(RE::PlayerCharacter* pc);
	MajorTransformKind GetMajorTransformSpellKind(RE::SpellItem* spell);
	bool ShouldHideMajorTransformSpellForState(RE::SpellItem* spell, TransformState state, const char* source);
	bool ShouldBlockMajorTransformSpellForCurrentState(RE::SpellItem* spell, const char* source);
	VampireLordCombatMode GetVampireLordCombatMode(RE::PlayerCharacter* pc);
	VampireLordCombatMode GetEffectiveVampireLordCombatMode(RE::PlayerCharacter* pc);
	bool ShouldHideVampireLordSpellForCombatMode(RE::SpellItem* spell, RE::PlayerCharacter* pc, const char* source);
	const char* ToString(VampireLordCombatMode mode);

	std::string TrimCopy(std::string_view value)
	{
		auto begin = value.begin();
		auto end = value.end();
		while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
			++begin;
		}
		while (end != begin) {
			auto prev = end;
			--prev;
			if (!std::isspace(static_cast<unsigned char>(*prev))) {
				break;
			}
			end = prev;
		}
		return std::string(begin, end);
	}

	std::vector<std::string> SplitCsv(std::string_view csv)
	{
		std::vector<std::string> tokens;
		std::size_t start = 0;
		while (start <= csv.size()) {
			std::size_t comma = csv.find(',', start);
			if (comma == std::string_view::npos) {
				comma = csv.size();
			}
			std::string token = TrimCopy(csv.substr(start, comma - start));
			if (!token.empty()) {
				tokens.push_back(std::move(token));
			}
			if (comma == csv.size()) {
				break;
			}
			start = comma + 1;
		}
		return tokens;
	}

	std::optional<RE::FormID> ParseFormIDToken(std::string_view token)
	{
		std::string text = TrimCopy(token);
		if (text.empty()) {
			return std::nullopt;
		}

		char* endPtr = nullptr;
		errno = 0;
		const unsigned long value = std::strtoul(text.c_str(), &endPtr, 0);
		if (errno != 0 || endPtr == text.c_str() || (endPtr && *endPtr != '\0') ||
			value > (std::numeric_limits<std::uint32_t>::max)()) {
			return std::nullopt;
		}

		return static_cast<RE::FormID>(value);
	}

	std::optional<RE::FormID> ResolveConfiguredFormIDToken(std::string_view token)
	{
		std::string text = TrimCopy(token);
		if (text.empty()) {
			return std::nullopt;
		}

		const auto sep = text.find_first_of("|:");
		if (sep == std::string::npos) {
			return ParseFormIDToken(text);
		}

		std::string pluginName = TrimCopy(std::string_view(text).substr(0, sep));
		std::string relativeText = TrimCopy(std::string_view(text).substr(sep + 1));
		if (pluginName.empty() || relativeText.empty()) {
			return std::nullopt;
		}
		auto relativeId = ParseFormIDToken(relativeText);
		if (!relativeId.has_value()) {
			return std::nullopt;
		}

		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			return std::nullopt;
		}
		if (auto* form = handler->LookupForm<RE::TESForm>(*relativeId, pluginName.c_str())) {
			return form->GetFormID();
		}
		return std::nullopt;
	}

	void RememberKnownGenericWheelId(std::string_view wheelId)
	{
		std::string normalized = TrimCopy(wheelId);
		if (normalized.empty()) {
			return;
		}
		const auto it = std::find(s_knownGenericWheelIds.begin(), s_knownGenericWheelIds.end(), normalized);
		if (it == s_knownGenericWheelIds.end()) {
			s_knownGenericWheelIds.push_back(std::move(normalized));
		}
	}

	void RememberKnownLichWheelId(std::string_view wheelId)
	{
		std::string normalized = TrimCopy(wheelId);
		if (normalized.empty()) {
			return;
		}
		const auto it = std::find(s_knownLichWheelIds.begin(), s_knownLichWheelIds.end(), normalized);
		if (it == s_knownLichWheelIds.end()) {
			s_knownLichWheelIds.push_back(std::move(normalized));
		}
	}

	std::size_t BuildGenericConfigSignature()
	{
		std::size_t signature = 0;
		auto mix = [&](std::size_t value) {
			signature ^= value + 0x9e3779b9u + (signature << 6) + (signature >> 2);
		};

		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::GenericEnabled));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::GenericStateID));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::GenericWheelID));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::GenericRaceEditorIDContains));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::GenericRaceKeywords));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::GenericRaceFormIDs));
		return signature;
	}

	void RefreshGenericTriggerCache()
	{
		const std::size_t signature = BuildGenericConfigSignature();
		if (s_genericTriggerCache.initialized && s_genericTriggerCache.signature == signature) {
			return;
		}

		s_genericTriggerCache.initialized = true;
		s_genericTriggerCache.signature = signature;
		s_genericTriggerCache.enabled = Config::WheelBehavior::TransformWheels::GenericEnabled;
		s_genericTriggerCache.stateId = TrimCopy(Config::WheelBehavior::TransformWheels::GenericStateID);
		if (s_genericTriggerCache.stateId.empty()) {
			s_genericTriggerCache.stateId = kGenericStateIdFallback;
		}
		s_genericTriggerCache.wheelId = TrimCopy(Config::WheelBehavior::TransformWheels::GenericWheelID);
		if (s_genericTriggerCache.wheelId.empty()) {
			s_genericTriggerCache.wheelId = kGenericWheelIdFallback;
		}
		if (s_genericTriggerCache.wheelId == kWerewolfWheelId || s_genericTriggerCache.wheelId == kVampireLordWheelId) {
			s_genericTriggerCache.wheelId = kGenericWheelIdFallback;
		}
		RememberKnownGenericWheelId(kGenericWheelIdFallback);
		RememberKnownGenericWheelId(s_genericTriggerCache.wheelId);

		s_genericTriggerCache.raceEditorIdContains = SplitCsv(Config::WheelBehavior::TransformWheels::GenericRaceEditorIDContains);
		s_genericTriggerCache.raceKeywords = SplitCsv(Config::WheelBehavior::TransformWheels::GenericRaceKeywords);
		s_genericTriggerCache.raceFormIDs.clear();
		for (const auto& token : SplitCsv(Config::WheelBehavior::TransformWheels::GenericRaceFormIDs)) {
			if (auto formId = ParseFormIDToken(token); formId.has_value()) {
				s_genericTriggerCache.raceFormIDs.insert(*formId);
			}
		}
		s_genericTriggerCache.hasAnyMatchers =
			!s_genericTriggerCache.raceEditorIdContains.empty() ||
			!s_genericTriggerCache.raceKeywords.empty() ||
			!s_genericTriggerCache.raceFormIDs.empty();
	}

	std::string LowerCopy(std::string_view text)
	{
		std::string out(text);
		for (char& ch : out) {
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		}
		return out;
	}

	std::size_t BuildVampireLordConfigSignature()
	{
		std::size_t signature = 0;
		auto mix = [&](std::size_t value) {
			signature ^= value + 0x9e3779b9u + (signature << 6) + (signature >> 2);
		};

		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::Enabled));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::PopulateMode));
		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::HideTransformSpell));
		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::HideForcedRightHandSpells));
		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::BlockHiddenSpellActivation));
		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::BlockRegularSpellsInMeleeMode));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::RaceEditorIDContains));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::RaceKeywords));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::RaceFormIDs));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::SpellTokens));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::ExitSpellTokens));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::AdditionalSpellFormIDs));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::HiddenSpellFormIDs));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::HiddenSpellTokens));
		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::VampireLordForm::DebugLog));
		return signature;
	}

	void RefreshVampireLordConfigCache()
	{
		const std::size_t signature = BuildVampireLordConfigSignature();
		if (s_vampireLordConfigCache.initialized && s_vampireLordConfigCache.signature == signature) {
			return;
		}

		s_vampireLordConfigCache.initialized = true;
		s_vampireLordConfigCache.signature = signature;
		s_vampireLordConfigCache.enabled = Config::WheelBehavior::TransformWheels::VampireLordForm::Enabled;
		s_vampireLordConfigCache.populateMode = TrimCopy(Config::WheelBehavior::TransformWheels::VampireLordForm::PopulateMode);
		s_vampireLordConfigCache.hideTransformSpell = Config::WheelBehavior::TransformWheels::VampireLordForm::HideTransformSpell;
		s_vampireLordConfigCache.hideForcedRightHandSpells = Config::WheelBehavior::TransformWheels::VampireLordForm::HideForcedRightHandSpells;
		s_vampireLordConfigCache.blockHiddenSpellActivation = Config::WheelBehavior::TransformWheels::VampireLordForm::BlockHiddenSpellActivation;
		s_vampireLordConfigCache.blockRegularSpellsInMeleeMode = Config::WheelBehavior::TransformWheels::VampireLordForm::BlockRegularSpellsInMeleeMode;
		s_vampireLordConfigCache.debugLog =
			Config::WheelBehavior::TransformWheels::VampireLordForm::DebugLog ||
			Config::WheelBehavior::TransformWheels::DebugLog;
		s_vampireLordConfigCache.hiddenSpellTokens =
			SplitCsv(Config::WheelBehavior::TransformWheels::VampireLordForm::HiddenSpellTokens);
		s_vampireLordConfigCache.raceEditorIdContains =
			SplitCsv(Config::WheelBehavior::TransformWheels::VampireLordForm::RaceEditorIDContains);
		s_vampireLordConfigCache.raceKeywords =
			SplitCsv(Config::WheelBehavior::TransformWheels::VampireLordForm::RaceKeywords);
		s_vampireLordConfigCache.spellTokens =
			SplitCsv(Config::WheelBehavior::TransformWheels::VampireLordForm::SpellTokens);
		s_vampireLordConfigCache.exitSpellTokens =
			SplitCsv(Config::WheelBehavior::TransformWheels::VampireLordForm::ExitSpellTokens);

		s_vampireLordConfigCache.raceFormIDs.clear();
		for (const auto& token : SplitCsv(Config::WheelBehavior::TransformWheels::VampireLordForm::RaceFormIDs)) {
			if (auto formId = ResolveConfiguredFormIDToken(token); formId.has_value()) {
				s_vampireLordConfigCache.raceFormIDs.insert(*formId);
			}
		}

		s_vampireLordConfigCache.additionalSpellFormIDs.clear();
		for (const auto& token : SplitCsv(Config::WheelBehavior::TransformWheels::VampireLordForm::AdditionalSpellFormIDs)) {
			if (auto formId = ResolveConfiguredFormIDToken(token); formId.has_value()) {
				s_vampireLordConfigCache.additionalSpellFormIDs.insert(*formId);
			}
		}

		s_vampireLordConfigCache.hiddenSpellFormIDs.clear();
		for (const auto& token : SplitCsv(Config::WheelBehavior::TransformWheels::VampireLordForm::HiddenSpellFormIDs)) {
			if (auto formId = ResolveConfiguredFormIDToken(token); formId.has_value()) {
				s_vampireLordConfigCache.hiddenSpellFormIDs.insert(*formId);
			}
		}
	}

	std::size_t BuildWerewolfConfigSignature()
	{
		std::size_t signature = 0;
		auto mix = [&](std::size_t value) {
			signature ^= value + 0x9e3779b9u + (signature << 6) + (signature >> 2);
		};

		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::WerewolfForm::Enabled));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::WerewolfForm::PopulateMode));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::WerewolfForm::RaceEditorIDContains));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::WerewolfForm::RaceKeywords));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::WerewolfForm::RaceFormIDs));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::WerewolfForm::SpellTokens));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::WerewolfForm::ExitSpellTokens));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::WerewolfForm::AdditionalSpellFormIDs));
		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::WerewolfForm::DebugLog));
		return signature;
	}

	void RefreshWerewolfConfigCache()
	{
		const std::size_t signature = BuildWerewolfConfigSignature();
		if (s_werewolfConfigCache.initialized && s_werewolfConfigCache.signature == signature) {
			return;
		}

		s_werewolfConfigCache.initialized = true;
		s_werewolfConfigCache.signature = signature;
		s_werewolfConfigCache.enabled = Config::WheelBehavior::TransformWheels::WerewolfForm::Enabled;
		s_werewolfConfigCache.populateMode = TrimCopy(Config::WheelBehavior::TransformWheels::WerewolfForm::PopulateMode);
		s_werewolfConfigCache.debugLog =
			Config::WheelBehavior::TransformWheels::WerewolfForm::DebugLog ||
			Config::WheelBehavior::TransformWheels::DebugLog;
		s_werewolfConfigCache.raceEditorIdContains =
			SplitCsv(Config::WheelBehavior::TransformWheels::WerewolfForm::RaceEditorIDContains);
		s_werewolfConfigCache.raceKeywords =
			SplitCsv(Config::WheelBehavior::TransformWheels::WerewolfForm::RaceKeywords);
		s_werewolfConfigCache.spellTokens =
			SplitCsv(Config::WheelBehavior::TransformWheels::WerewolfForm::SpellTokens);
		s_werewolfConfigCache.exitSpellTokens =
			SplitCsv(Config::WheelBehavior::TransformWheels::WerewolfForm::ExitSpellTokens);

		s_werewolfConfigCache.raceFormIDs.clear();
		for (const auto& token : SplitCsv(Config::WheelBehavior::TransformWheels::WerewolfForm::RaceFormIDs)) {
			if (auto formId = ResolveConfiguredFormIDToken(token); formId.has_value()) {
				s_werewolfConfigCache.raceFormIDs.insert(*formId);
			}
		}

		s_werewolfConfigCache.additionalSpellFormIDs.clear();
		for (const auto& token : SplitCsv(Config::WheelBehavior::TransformWheels::WerewolfForm::AdditionalSpellFormIDs)) {
			if (auto formId = ResolveConfiguredFormIDToken(token); formId.has_value()) {
				s_werewolfConfigCache.additionalSpellFormIDs.insert(*formId);
			}
		}
	}

	LichMode ParseLichMode(std::string_view value)
	{
		const std::string normalized = LowerCopy(TrimCopy(value));
		if (normalized == "replace") {
			return LichMode::Replace;
		}
		if (normalized == "disabled" || normalized == "off") {
			return LichMode::Disabled;
		}
		return LichMode::Overlay;
	}

	LichPopulateMode ParseLichPopulateMode(std::string_view value)
	{
		const std::string normalized = LowerCopy(TrimCopy(value));
		if (normalized == "delta") {
			return LichPopulateMode::Delta;
		}
		if (normalized == "manual") {
			return LichPopulateMode::Manual;
		}
		return LichPopulateMode::ModList;
	}

	LichTransformGuardMode ParseLichTransformGuard(std::string_view value)
	{
		const std::string normalized = LowerCopy(TrimCopy(value));
		if (normalized == "blockalltransformsexceptexit") {
			return LichTransformGuardMode::BlockAllTransformsExceptExit;
		}
		if (normalized == "blockothertransforms") {
			return LichTransformGuardMode::BlockOtherTransforms;
		}
		return LichTransformGuardMode::Off;
	}

	std::size_t BuildLichConfigSignature()
	{
		std::size_t signature = 0;
		auto mix = [&](std::size_t value) {
			signature ^= value + 0x9e3779b9u + (signature << 6) + (signature >> 2);
		};

		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::LichForm::Enabled));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::LichForm::Mode));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::LichForm::PopulateMode));
		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::LichForm::AllowBaseWheel));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::LichForm::TransformGuard));
		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::LichForm::BlockBoundSpells));
		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::LichForm::HideWeapons));
		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::LichForm::HideGear));
		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::LichForm::BlockStaffSwapping));
		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::LichForm::SuppressDirectCast));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::LichForm::RaceEditorIDContains));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::LichForm::RaceKeywords));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::LichForm::RaceFormIDs));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::LichForm::SpellTokens));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::LichForm::ExitSpellTokens));
		mix(std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::LichForm::AdditionalSpellFormIDs));
		mix(std::hash<bool>{}(Config::WheelBehavior::TransformWheels::LichForm::DebugLog));
		return signature;
	}

	void RefreshLichConfigCache()
	{
		const std::size_t signature = BuildLichConfigSignature();
		if (s_lichConfigCache.initialized && s_lichConfigCache.signature == signature) {
			return;
		}

		s_lichConfigCache.initialized = true;
		s_lichConfigCache.signature = signature;
		s_lichConfigCache.enabled = Config::WheelBehavior::TransformWheels::LichForm::Enabled;
		s_lichConfigCache.mode = ParseLichMode(Config::WheelBehavior::TransformWheels::LichForm::Mode);
		s_lichConfigCache.populateMode = ParseLichPopulateMode(Config::WheelBehavior::TransformWheels::LichForm::PopulateMode);
		s_lichConfigCache.allowBaseWheel = Config::WheelBehavior::TransformWheels::LichForm::AllowBaseWheel;
		s_lichConfigCache.transformGuard = ParseLichTransformGuard(Config::WheelBehavior::TransformWheels::LichForm::TransformGuard);
		s_lichConfigCache.blockBoundSpells = Config::WheelBehavior::TransformWheels::LichForm::BlockBoundSpells;
		s_lichConfigCache.hideWeapons = Config::WheelBehavior::TransformWheels::LichForm::HideWeapons;
		s_lichConfigCache.hideGear = Config::WheelBehavior::TransformWheels::LichForm::HideGear;
		s_lichConfigCache.blockStaffSwapping = Config::WheelBehavior::TransformWheels::LichForm::BlockStaffSwapping;
		s_lichConfigCache.suppressDirectCast = Config::WheelBehavior::TransformWheels::LichForm::SuppressDirectCast;
		s_lichConfigCache.debugLog = Config::WheelBehavior::TransformWheels::LichForm::DebugLog ||
			Config::WheelBehavior::TransformWheels::DebugLog;
		s_lichConfigCache.stateId = kLichStateIdFallback;
		s_lichConfigCache.wheelId = kLichWheelIdFallback;
		s_lichConfigCache.raceEditorIdContains = SplitCsv(Config::WheelBehavior::TransformWheels::LichForm::RaceEditorIDContains);
		s_lichConfigCache.raceKeywords = SplitCsv(Config::WheelBehavior::TransformWheels::LichForm::RaceKeywords);
		s_lichConfigCache.raceFormIDs.clear();
		for (const auto& token : SplitCsv(Config::WheelBehavior::TransformWheels::LichForm::RaceFormIDs)) {
			if (auto formId = ParseFormIDToken(token); formId.has_value()) {
				s_lichConfigCache.raceFormIDs.insert(*formId);
			}
		}
		s_lichConfigCache.additionalSpellFormIDs.clear();
		for (const auto& token : SplitCsv(Config::WheelBehavior::TransformWheels::LichForm::AdditionalSpellFormIDs)) {
			if (auto formId = ParseFormIDToken(token); formId.has_value()) {
				s_lichConfigCache.additionalSpellFormIDs.insert(*formId);
			}
		}
		s_lichConfigCache.spellTokens = SplitCsv(Config::WheelBehavior::TransformWheels::LichForm::SpellTokens);
		s_lichConfigCache.exitSpellTokens = SplitCsv(Config::WheelBehavior::TransformWheels::LichForm::ExitSpellTokens);
		s_lichConfigCache.hasAnyMatchers =
			!s_lichConfigCache.raceEditorIdContains.empty() ||
			!s_lichConfigCache.raceKeywords.empty() ||
			!s_lichConfigCache.raceFormIDs.empty();
		RememberKnownLichWheelId(kLichWheelIdFallback);
		RememberKnownLichWheelId(s_lichConfigCache.wheelId);
	}

	std::size_t BuildPrecedenceSignature()
	{
		return std::hash<std::string>{}(Config::WheelBehavior::TransformWheels::PrecedenceOrder);
	}

	void RefreshPrecedenceOrderCache()
	{
		const std::size_t signature = BuildPrecedenceSignature();
		if (signature == s_precedenceSignature) {
			return;
		}
		s_precedenceSignature = signature;

		std::vector<TransformState> parsed;
		for (const auto& tokenRaw : SplitCsv(Config::WheelBehavior::TransformWheels::PrecedenceOrder)) {
			const std::string token = LowerCopy(tokenRaw);
			TransformState mapped = TransformState::Human;
			bool valid = true;
			if (token == "werewolf") {
				mapped = TransformState::Werewolf;
			} else if (token == "vampirelord" || token == "vampire") {
				mapped = TransformState::VampireLord;
			} else if (token == "lich") {
				mapped = TransformState::Lich;
			} else if (token == "genericothers" || token == "generic") {
				mapped = TransformState::Generic;
			} else {
				valid = false;
			}
			if (!valid) {
				continue;
			}
			if (std::find(parsed.begin(), parsed.end(), mapped) == parsed.end()) {
				parsed.push_back(mapped);
			}
		}

		const std::array<TransformState, 4> defaults{
			TransformState::Werewolf,
			TransformState::VampireLord,
			TransformState::Lich,
			TransformState::Generic
		};
		for (auto state : defaults) {
			if (std::find(parsed.begin(), parsed.end(), state) == parsed.end()) {
				parsed.push_back(state);
			}
		}
		s_precedenceOrder = std::move(parsed);
	}

	const char* ToString(TransformState state)
	{
		switch (state) {
		case TransformState::Werewolf:
			return "Werewolf";
		case TransformState::VampireLord:
			return "VampireLord";
		case TransformState::Generic:
			return "Generic";
		case TransformState::Lich:
			return "Lich";
		case TransformState::Human:
		default:
			return "Human";
		}
	}

	std::string GetWheelId(TransformState state)
	{
		switch (state) {
		case TransformState::Werewolf:
			return kWerewolfWheelId;
		case TransformState::VampireLord:
			return kVampireLordWheelId;
		case TransformState::Generic:
			RefreshGenericTriggerCache();
			return s_genericTriggerCache.wheelId;
		case TransformState::Lich:
			RefreshLichConfigCache();
			return s_lichConfigCache.wheelId;
		case TransformState::Human:
		default:
			return kGenericWheelIdFallback;
		}
	}

	bool StartsWith(std::string_view value, std::string_view prefix)
	{
		if (value.size() < prefix.size()) {
			return false;
		}
		return value.compare(0, prefix.size(), prefix) == 0;
	}

	std::string MakeWheelId(TransformState state, std::size_t pageIndex)
	{
		std::string id = GetWheelId(state);
		if (state == TransformState::Generic && s_activeGenericRaceContext != 0) {
			char raceSuffix[11]{};
			std::snprintf(raceSuffix, sizeof(raceSuffix), "R%08X", s_activeGenericRaceContext);
			id += "_";
			id += raceSuffix;
		}
		id += "_P";
		id += std::to_string(pageIndex + 1);
		return id;
	}

	bool IsTransformWheelTagForState(std::string_view tag, TransformState state)
	{
		if (state == TransformState::Generic) {
			RefreshGenericTriggerCache();
			for (const auto& genericWheelId : s_knownGenericWheelIds) {
				if (!genericWheelId.empty() && StartsWith(tag, genericWheelId)) {
					return true;
				}
			}
			return false;
		}
		if (state == TransformState::Lich) {
			RefreshLichConfigCache();
			for (const auto& lichWheelId : s_knownLichWheelIds) {
				if (!lichWheelId.empty() && StartsWith(tag, lichWheelId)) {
					return true;
				}
			}
			return false;
		}
		return StartsWith(tag, GetWheelId(state));
	}

	bool IsTransformWheelTag(std::string_view tag)
	{
		if (StartsWith(tag, kWerewolfWheelId) || StartsWith(tag, kVampireLordWheelId) || StartsWith(tag, kLichWheelIdFallback)) {
			return true;
		}
		RefreshGenericTriggerCache();
		for (const auto& genericWheelId : s_knownGenericWheelIds) {
			if (!genericWheelId.empty() && StartsWith(tag, genericWheelId)) {
				return true;
			}
		}
		RefreshLichConfigCache();
		for (const auto& lichWheelId : s_knownLichWheelIds) {
			if (!lichWheelId.empty() && StartsWith(tag, lichWheelId)) {
				return true;
			}
		}
		return false;
	}

	bool WheelIndexMatchesTag(int index, std::string_view tag)
	{
		if (index < 0 || tag.empty()) {
			return false;
		}
		std::shared_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(index);
		if (!wheel) {
			return false;
		}
		const std::string& currentTag = wheel->GetClientTag();
		return !currentTag.empty() && currentTag == tag;
	}

	std::optional<int> FindWheelIndexByTag(std::string_view tag)
	{
		if (tag.empty()) {
			return std::nullopt;
		}
		std::shared_lock lock(Wheeler::GetWheelDataLock());
		const int count = Wheeler::GetWheelCount();
		for (int i = 0; i < count; ++i) {
			Wheel* wheel = Wheeler::GetWheelByIndex(i);
			if (!wheel) {
				continue;
			}
			const std::string& currentTag = wheel->GetClientTag();
			if (!currentTag.empty() && currentTag == tag) {
				return i;
			}
		}
		return std::nullopt;
	}

	void TagWheelIndex(int index, std::string_view tag)
	{
		if (index < 0 || tag.empty()) {
			return;
		}
		std::unique_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(index);
		if (wheel) {
			wheel->SetClientTag(tag);
		}
	}

	bool IsTransformWheelIndexInternal(int index)
	{
		if (index < 0) {
			return false;
		}
		std::shared_lock lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(index);
		if (!wheel) {
			return false;
		}
		return IsTransformWheelTag(wheel->GetClientTag());
	}

	struct TaggedWheelInfo
	{
		int index = -1;
		std::string tag;
	};

	std::vector<TaggedWheelInfo> FindTaggedWheelsForState(TransformState state)
	{
		std::vector<TaggedWheelInfo> result;
		std::shared_lock lock(Wheeler::GetWheelDataLock());
		const int count = Wheeler::GetWheelCount();
		result.reserve(static_cast<std::size_t>(count));
		for (int i = 0; i < count; ++i) {
			Wheel* wheel = Wheeler::GetWheelByIndex(i);
			if (!wheel) {
				continue;
			}
			const std::string& currentTag = wheel->GetClientTag();
			if (!IsTransformWheelTagForState(currentTag, state)) {
				continue;
			}
			TaggedWheelInfo info{};
			info.index = i;
			info.tag = currentTag;
			result.push_back(std::move(info));
		}
		return result;
	}

	bool HasAnyTransformWheels()
	{
		std::shared_lock lock(Wheeler::GetWheelDataLock());
		const int count = Wheeler::GetWheelCount();
		for (int i = 0; i < count; ++i) {
			Wheel* wheel = Wheeler::GetWheelByIndex(i);
			if (!wheel) {
				continue;
			}
			if (IsTransformWheelTag(wheel->GetClientTag())) {
				return true;
			}
		}
		return false;
	}

	bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs)
	{
		if (lhs.size() != rhs.size()) {
			return false;
		}
		for (size_t i = 0; i < lhs.size(); ++i) {
			unsigned char a = static_cast<unsigned char>(lhs[i]);
			unsigned char b = static_cast<unsigned char>(rhs[i]);
			if (std::tolower(a) != std::tolower(b)) {
				return false;
			}
		}
		return true;
	}

	bool ContainsIgnoreCase(std::string_view haystack, std::string_view needle)
	{
		if (needle.empty()) {
			return true;
		}
		if (haystack.size() < needle.size()) {
			return false;
		}
		for (size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
			bool match = true;
			for (size_t j = 0; j < needle.size(); ++j) {
				unsigned char a = static_cast<unsigned char>(haystack[i + j]);
				unsigned char b = static_cast<unsigned char>(needle[j]);
				if (std::tolower(a) != std::tolower(b)) {
					match = false;
					break;
				}
			}
			if (match) {
				return true;
			}
		}
		return false;
	}

	bool MatchesAnySubstringToken(std::string_view haystack, const std::vector<std::string>& tokens, std::size_t minTokenLength = 1)
	{
		for (const auto& token : tokens) {
			const std::string trimmed = TrimCopy(token);
			if (trimmed.size() < minTokenLength) {
				continue;
			}
			if (ContainsIgnoreCase(haystack, trimmed)) {
				return true;
			}
		}
		return false;
	}

		std::string_view GetSourceFileName(RE::TESForm* form)
		{
			if (!form) {
				return {};
			}
			if (auto* file = form->GetFile(0)) {
				const auto name = file->GetFilename();
				if (!name.empty()) {
					return name;
				}
			}
			return {};
		}

	bool IsWerewolfConfiguredAdditionalSpell(RE::SpellItem* spell)
	{
		if (!spell) {
			return false;
		}
		RefreshWerewolfConfigCache();
		return s_werewolfConfigCache.enabled &&
			s_werewolfConfigCache.additionalSpellFormIDs.contains(spell->GetFormID());
	}

	bool IsWerewolfExitSpell(RE::SpellItem* spell)
	{
		if (!spell) {
			return false;
		}
		RefreshWerewolfConfigCache();

		const char* edidC = spell->GetFormEditorID();
		const char* nameC = spell->GetName();
		std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};

		return ContainsIgnoreCase(edid, "Revert") ||
			ContainsIgnoreCase(name, "Revert") ||
			ContainsIgnoreCase(edid, "HumanForm") ||
			ContainsIgnoreCase(name, "Human Form") ||
			ContainsIgnoreCase(name, "Return to Human") ||
			ContainsIgnoreCase(name, "Mortal Form") ||
			(s_werewolfConfigCache.enabled &&
				(MatchesAnySubstringToken(edid, s_werewolfConfigCache.exitSpellTokens, 2) ||
					MatchesAnySubstringToken(name, s_werewolfConfigCache.exitSpellTokens, 2)));
	}

	bool IsWerewolfTransformEntrySpell(RE::SpellItem* spell)
	{
		if (!spell) {
			return false;
		}
		if (IsWerewolfExitSpell(spell)) {
			return false;
		}

		const RE::FormID formID = spell->GetFormID();
		if ((formID & 0x00FFFFFF) == kBeastFormPowerFormID) {
			return true;
		}

		const char* edidC = spell->GetFormEditorID();
		const char* nameC = spell->GetName();
		std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};

		const bool explicitBeastForm = ContainsIgnoreCase(edid, "BeastForm") ||
			ContainsIgnoreCase(name, "Beast Form");
		const bool otherTransformEntry = ContainsIgnoreCase(edid, "VampireLord") ||
			ContainsIgnoreCase(edid, "DLC1Vampire") ||
			ContainsIgnoreCase(name, "Vampire Lord") ||
			ContainsIgnoreCase(edid, "Werebear") ||
			ContainsIgnoreCase(name, "Werebear") ||
			ContainsIgnoreCase(edid, "Lich") ||
			ContainsIgnoreCase(name, "Lich");
		const bool transformVerb = ContainsIgnoreCase(edid, "Transform") ||
			ContainsIgnoreCase(name, "Transform") ||
			ContainsIgnoreCase(edid, "Transformation") ||
			ContainsIgnoreCase(name, "Transformation") ||
			ContainsIgnoreCase(edid, "ChangeForm") ||
			ContainsIgnoreCase(name, "Change Form") ||
			ContainsIgnoreCase(edid, "Polymorph") ||
			ContainsIgnoreCase(name, "Polymorph") ||
			ContainsIgnoreCase(edid, "Morph") ||
			ContainsIgnoreCase(name, "Morph") ||
			ContainsIgnoreCase(edid, "Shift") ||
			ContainsIgnoreCase(name, "Shift");
		const bool werebeastContext = ContainsIgnoreCase(edid, "Werewolf") ||
			ContainsIgnoreCase(name, "Werewolf") ||
			ContainsIgnoreCase(edid, "Lycan") ||
			ContainsIgnoreCase(name, "Lycan") ||
			ContainsIgnoreCase(edid, "Beast") ||
			ContainsIgnoreCase(name, "Beast");

		return explicitBeastForm || otherTransformEntry || (transformVerb && werebeastContext);
	}

	bool IsWerewolfSafeWheelSpell(RE::SpellItem* spell)
	{
		if (!spell) {
			return false;
		}
		if (!IsActivatableTransformSpell(spell, TransformState::Werewolf)) {
			return false;
		}
		if (IsWerewolfExitSpell(spell)) {
			return true;
		}
		return !IsWerewolfTransformEntrySpell(spell);
	}

	std::size_t FilterWerewolfUnsafeSpellSet(std::unordered_set<RE::FormID>& forms)
	{
		std::size_t removed = 0;
		for (auto it = forms.begin(); it != forms.end();) {
			auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(*it);
			if (!spell || !IsWerewolfSafeWheelSpell(spell)) {
				it = forms.erase(it);
				++removed;
				continue;
			}
			++it;
		}
		return removed;
	}

	std::unordered_set<RE::FormID> FilterWerewolfEquippedSpells(
		const std::unordered_set<RE::FormID>& equippedSpells,
		const std::unordered_set<RE::FormID>& deltaSpells,
		const std::unordered_set<RE::FormID>& raceSpells,
		const std::unordered_set<RE::FormID>& kitSpells,
		RE::FormID selectedPowerFormId,
		bool hasBaseline,
		const std::unordered_set<RE::FormID>& humanEquippedBaseline)
	{
		std::unordered_set<RE::FormID> filtered;
		filtered.reserve(equippedSpells.size());

		const bool hasHumanEquippedBaseline = !humanEquippedBaseline.empty();
		for (const auto formId : equippedSpells) {
			auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
			if (!spell || !IsWerewolfSafeWheelSpell(spell)) {
				continue;
			}

			if (deltaSpells.find(formId) != deltaSpells.end() ||
				raceSpells.find(formId) != raceSpells.end() ||
				kitSpells.find(formId) != kitSpells.end()) {
				filtered.insert(formId);
				continue;
			}

			if (IsWerewolfExitSpell(spell)) {
				filtered.insert(formId);
				continue;
			}

			const bool missingFromHumanBaseline = !hasHumanEquippedBaseline ||
				humanEquippedBaseline.find(formId) == humanEquippedBaseline.end();
			if (!missingFromHumanBaseline) {
				continue;
			}

			if (formId == selectedPowerFormId) {
				filtered.insert(formId);
				continue;
			}

			if (!hasBaseline && IsTransformKitSpell(spell, TransformState::Werewolf)) {
				filtered.insert(formId);
			}
		}

		return filtered;
	}

	std::unordered_set<RE::FormID> FilterWerewolfShoutForms(
		const std::unordered_set<RE::FormID>& shoutForms,
		const std::unordered_set<RE::FormID>& raceShouts,
		RE::FormID selectedPowerFormId,
		bool hasHumanShoutBaseline,
		const std::unordered_set<RE::FormID>& humanShoutBaseline)
	{
		std::unordered_set<RE::FormID> filtered;
		filtered.reserve(shoutForms.size());

		for (const auto formId : shoutForms) {
			auto* shout = RE::TESForm::LookupByID<RE::TESShout>(formId);
			if (!shout) {
				continue;
			}

			if (raceShouts.find(formId) != raceShouts.end() ||
				IsTransformKitShout(shout, TransformState::Werewolf)) {
				filtered.insert(formId);
				continue;
			}

			const bool missingFromHumanBaseline = !hasHumanShoutBaseline ||
				humanShoutBaseline.find(formId) == humanShoutBaseline.end();
			if (formId == selectedPowerFormId && missingFromHumanBaseline) {
				filtered.insert(formId);
			}
		}

		return filtered;
	}

	bool IsLichTransformRace(RE::TESRace* race)
	{
		if (!race) {
			return false;
		}

		RefreshLichConfigCache();
		if (!s_lichConfigCache.enabled || s_lichConfigCache.mode == LichMode::Disabled) {
			return false;
		}

		bool matched = false;
		if (s_lichConfigCache.raceFormIDs.find(race->GetFormID()) != s_lichConfigCache.raceFormIDs.end()) {
			matched = true;
		}
		if (!matched && s_lichConfigCache.hasAnyMatchers) {
			if (const char* editorId = race->GetFormEditorID(); editorId) {
				if (MatchesAnySubstringToken(editorId, s_lichConfigCache.raceEditorIdContains, 2)) {
					matched = true;
				}
			}
			if (!matched) {
				for (const auto& keyword : s_lichConfigCache.raceKeywords) {
					if (!keyword.empty() && race->HasKeywordString(keyword.c_str())) {
						matched = true;
						break;
					}
				}
			}
		}
		if (!matched) {
			if (const char* editorId = race->GetFormEditorID(); editorId) {
				// Conservative fallback to support Undeath variants without hard FormIDs.
				if (ContainsIgnoreCase(editorId, "Lich")) {
					matched = true;
				}
			}
		}

		if (!matched) {
			return false;
		}

		if (s_lichConfigCache.debugLog && s_loggedLichRaceMatches.insert(race->GetFormID()).second) {
			const auto sourceFile = GetSourceFileName(race);
			logger::info("TransformWheels: lich race match {:08X} edid='{}' source='{}'",
				race->GetFormID(),
				race->GetFormEditorID() ? race->GetFormEditorID() : "",
				sourceFile);
		}
		return true;
	}

	bool IsGenericTransformRace(RE::TESRace* race)
	{
		if (!race) {
			return false;
		}
		if (IsLichTransformRace(race)) {
			return false;
		}

		RefreshGenericTriggerCache();
		if (!s_genericTriggerCache.enabled || !s_genericTriggerCache.hasAnyMatchers) {
			return false;
		}

		if (s_genericTriggerCache.raceFormIDs.find(race->GetFormID()) != s_genericTriggerCache.raceFormIDs.end()) {
			return true;
		}

		if (const char* editorId = race->GetFormEditorID(); editorId) {
			const std::string_view view(editorId);
			for (const auto& pattern : s_genericTriggerCache.raceEditorIdContains) {
				if (!pattern.empty() && ContainsIgnoreCase(view, pattern)) {
					return true;
				}
			}
		}

		for (const auto& keyword : s_genericTriggerCache.raceKeywords) {
			if (!keyword.empty() && race->HasKeywordString(keyword.c_str())) {
				return true;
			}
		}

		return false;
	}

	RE::FormID ResolveGenericRaceContext(RE::PlayerCharacter* pc)
	{
		if (!pc) {
			return 0;
		}

		auto* race = pc->GetRace();
		if (!race) {
			return 0;
		}

		if (!IsGenericTransformRace(race)) {
			return 0;
		}

		return race->GetFormID();
	}

	bool FormHasRelativeID(const RE::TESForm* form, RE::FormID relativeId)
	{
		return form && ((form->GetFormID() & 0x00FFFFFF) == relativeId);
	}

	bool FormMatchesPluginRelativeID(const RE::TESForm* form, const char* pluginName, RE::FormID relativeId)
	{
		if (!form || !pluginName) {
			return false;
		}
		if (auto* handler = RE::TESDataHandler::GetSingleton()) {
			if (auto* expectedForm = handler->LookupForm<RE::TESForm>(relativeId, pluginName)) {
				return form == expectedForm;
			}
		}
		return FormHasRelativeID(form, relativeId);
	}

	bool IsVampireLordConfiguredAdditionalSpell(RE::SpellItem* spell)
	{
		if (!spell) {
			return false;
		}
		RefreshVampireLordConfigCache();
		return s_vampireLordConfigCache.enabled &&
			s_vampireLordConfigCache.additionalSpellFormIDs.contains(spell->GetFormID());
	}

	RE::BGSListForm* GetVampireLordSpellsPowersFormList()
	{
		if (auto* handler = RE::TESDataHandler::GetSingleton()) {
			return handler->LookupForm<RE::BGSListForm>(kVampireLordSpellsPowersFormListID, kDawnguardPlugin);
		}
		return nullptr;
	}

	bool IsVampireLordFormListSpell(RE::SpellItem* spell)
	{
		auto* formList = GetVampireLordSpellsPowersFormList();
		return spell && formList && formList->HasForm(spell);
	}

	bool SpellHasVampireLordRevertEffect(RE::SpellItem* spell)
	{
		if (!spell) {
			return false;
		}
		// The real Vampire Lord exit action is the script-added DLC1Revert spell.
		// In localized builds the spell name can miss English exit tokens, so use
		// the Dawnguard revert magic effect as the narrow, VL-specific signal.
		if (!EqualsIgnoreCase(GetSourceFileName(spell), kDawnguardPlugin)) {
			return false;
		}
		for (auto* effect : spell->effects) {
			auto* baseEffect = effect ? effect->baseEffect : nullptr;
			if (!baseEffect) {
				continue;
			}
			const char* edidC = baseEffect->GetFormEditorID();
			const char* nameC = baseEffect->GetName();
			const std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
			const std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};
			if (ContainsIgnoreCase(edid, "DLC1Revert") ||
				ContainsIgnoreCase(name, "Revert") ||
				ContainsIgnoreCase(name, "Change Form")) {
				return true;
			}
		}
		return false;
	}

	bool IsVampireLordExitSpell(RE::SpellItem* spell)
	{
		if (!spell) {
			return false;
		}
		RefreshVampireLordConfigCache();
		if (!s_vampireLordConfigCache.enabled) {
			return false;
		}

		const char* edidC = spell->GetFormEditorID();
		const char* nameC = spell->GetName();
		std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};
		return MatchesAnySubstringToken(edid, s_vampireLordConfigCache.exitSpellTokens, 2) ||
			MatchesAnySubstringToken(name, s_vampireLordConfigCache.exitSpellTokens, 2) ||
			SpellHasVampireLordRevertEffect(spell);
	}

	bool IsVampireLordHiddenSpell(RE::SpellItem* spell)
	{
		if (!spell) {
			return false;
		}
		RefreshVampireLordConfigCache();
		if (!s_vampireLordConfigCache.enabled) {
			return false;
		}
		if (s_vampireLordConfigCache.hiddenSpellFormIDs.contains(spell->GetFormID())) {
			return true;
		}
		if (s_vampireLordConfigCache.hideTransformSpell &&
			FormMatchesPluginRelativeID(spell, kDawnguardPlugin, kVampireLordPowerFormID)) {
			return true;
		}
		if (s_vampireLordConfigCache.hideForcedRightHandSpells &&
			FormMatchesPluginRelativeID(spell, kDawnguardPlugin, kVampireLordForcedDrainFormID)) {
			return true;
		}

		const char* edidC = spell->GetFormEditorID();
		const char* nameC = spell->GetName();
		std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};
		return MatchesAnySubstringToken(edid, s_vampireLordConfigCache.hiddenSpellTokens, 3) ||
			MatchesAnySubstringToken(name, s_vampireLordConfigCache.hiddenSpellTokens, 3);
	}

	bool IsVampireLordHiddenShout(RE::TESShout* shout)
	{
		if (!shout) {
			return false;
		}
		RefreshVampireLordConfigCache();
		if (!s_vampireLordConfigCache.enabled) {
			return false;
		}
		// Vampire Lord mods can expose non-castable duplicate abilities as TESShout forms.
		// This uses the VL-only hidden form list so normal shout wheels and other transforms
		// keep their existing behavior.
		if (s_vampireLordConfigCache.hiddenSpellFormIDs.contains(shout->GetFormID())) {
			return true;
		}

		const char* edidC = shout->GetFormEditorID();
		const char* nameC = shout->GetName();
		std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};
		return MatchesAnySubstringToken(edid, s_vampireLordConfigCache.hiddenSpellTokens, 3) ||
			MatchesAnySubstringToken(name, s_vampireLordConfigCache.hiddenSpellTokens, 3);
	}

	bool IsTrustedVampireLordWheelSpell(RE::SpellItem* spell)
	{
		if (!spell) {
			return false;
		}
		if (IsVampireLordHiddenSpell(spell)) {
			return false;
		}
		const bool vampireLordExitSpell = IsVampireLordExitSpell(spell);
		if (!vampireLordExitSpell &&
			spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect) {
			return false;
		}
		if (!vampireLordExitSpell &&
			!IsActivatableTransformSpell(spell, TransformState::VampireLord)) {
			return false;
		}
		if (vampireLordExitSpell) {
			return true;
		}
		if (IsVampireLordFormListSpell(spell)) {
			return true;
		}

		// VL generated-wheel persistence must be narrower than normal/base wheel spell access.
		// Only configured or token-recognized VL kit spells are sticky; arbitrary equipped
		// vampire/human powers are left out so they do not leak into the transform wheel.
		return IsTransformKitSpell(spell, TransformState::VampireLord);
	}

	bool IsVampireLordRaiseDeadFamilySpell(RE::SpellItem* spell)
	{
		if (!spell || !EqualsIgnoreCase(GetSourceFileName(spell), kDawnguardPlugin)) {
			return false;
		}
		return IsVampireLordRaiseDeadRelativeID(spell->GetFormID() & 0x00FFFFFF);
	}

	bool IsVampireLordRaiseDeadFamilyForm(RE::FormID formID)
	{
		return IsVampireLordRaiseDeadFamilySpell(RE::TESForm::LookupByID<RE::SpellItem>(formID));
	}

	template <class Range>
	void AppendEligibleVampireLordRaiseDeadForms(const Range& forms, std::vector<RE::FormID>& out)
	{
		for (const auto formID : forms) {
			auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formID);
			if (IsVampireLordRaiseDeadFamilySpell(spell) && IsTrustedVampireLordWheelSpell(spell)) {
				out.push_back(formID);
			}
		}
	}

	void SortUniqueFormIDs(std::vector<RE::FormID>& forms)
	{
		std::sort(forms.begin(), forms.end());
		forms.erase(std::unique(forms.begin(), forms.end()), forms.end());
	}

	void NormalizeVampireLordRaiseDeadSet(
		std::unordered_set<RE::FormID>& forms,
		RE::FormID representative)
	{
		for (auto it = forms.begin(); it != forms.end();) {
			if (IsVampireLordRaiseDeadFamilyForm(*it) && *it != representative) {
				it = forms.erase(it);
			} else {
				++it;
			}
		}
	}

	void NormalizeVampireLordRaiseDeadForms(
		std::vector<RE::FormID>& forms,
		RE::FormID representative)
	{
		forms.erase(std::remove_if(forms.begin(), forms.end(), [&](RE::FormID formID) {
			return IsVampireLordRaiseDeadFamilyForm(formID) && formID != representative;
		}), forms.end());
	}

	std::string FormatVampireLordRaiseDeadRejectedForms(
		const std::vector<RE::FormID>& candidates,
		RE::FormID representative)
	{
		std::string result;
		for (const auto formID : candidates) {
			if (formID == representative) {
				continue;
			}
			char buffer[16]{};
			std::snprintf(buffer, sizeof(buffer), "%08X", formID);
			if (!result.empty()) {
				result.push_back(',');
			}
			result.append(buffer);
		}
		return result;
	}

	std::uint64_t HashVampireLordRaiseDeadSelection(
		const std::vector<RE::FormID>& candidates,
		RE::FormID representative)
	{
		std::uint64_t hash = 1469598103934665603ull;
		auto mix = [&](std::uint64_t value) {
			hash ^= value;
			hash *= 1099511628211ull;
		};
		mix(representative);
		for (const auto formID : candidates) {
			mix(formID);
		}
		return hash;
	}

	void NormalizeVampireLordRaiseDeadFamily(
		std::unordered_set<RE::FormID>& candidateSpells,
		RE::PlayerCharacter* pc,
		const std::unordered_set<RE::FormID>& currentAllSpells)
	{
		std::vector<RE::FormID> candidates;
		AppendEligibleVampireLordRaiseDeadForms(candidateSpells, candidates);
		AppendEligibleVampireLordRaiseDeadForms(s_vampireLordSessionSpellForms, candidates);
		if (s_pendingTransform.active && s_pendingTransform.state == TransformState::VampireLord) {
			AppendEligibleVampireLordRaiseDeadForms(s_pendingTransform.latchedForms, candidates);
		}

		std::vector<RE::FormID> equipped;
		if (pc) {
			for (const bool isLeft : { true, false }) {
				auto* equippedForm = pc->GetEquippedObject(isLeft);
				auto* spell = equippedForm ? equippedForm->As<RE::SpellItem>() : nullptr;
				if (IsVampireLordRaiseDeadFamilySpell(spell) && IsTrustedVampireLordWheelSpell(spell)) {
					equipped.push_back(spell->GetFormID());
				}
			}
		}
		candidates.insert(candidates.end(), equipped.begin(), equipped.end());

		std::vector<RE::FormID> known;
		AppendEligibleVampireLordRaiseDeadForms(currentAllSpells, known);
		candidates.insert(candidates.end(), known.begin(), known.end());
		SortUniqueFormIDs(candidates);
		SortUniqueFormIDs(equipped);
		SortUniqueFormIDs(known);

		const RE::FormID previous = s_vampireLordRaiseDeadRepresentative;
		const bool directCastActive = Wheeler::IsDirectCastPipelineActiveForHandMemory();
		const RE::FormID representative = SelectVampireLordRaiseDeadRepresentative(
			candidates,
			equipped,
			previous,
			known,
			directCastActive);

		const char* source = "none";
		if (representative != 0) {
			if (!directCastActive && ContainsFormID(equipped, representative)) {
				source = "equipped";
			} else if (representative == previous) {
				source = "session";
			} else if (ContainsFormID(known, representative)) {
				source = "known";
			} else {
				source = "deterministic";
			}
		}

		s_vampireLordRaiseDeadRepresentative = representative;
		NormalizeVampireLordRaiseDeadSet(candidateSpells, representative);
		if (representative != 0) {
			candidateSpells.insert(representative);
		}
		NormalizeVampireLordRaiseDeadSet(s_vampireLordSessionSpellForms, representative);
		if (s_pendingTransform.active && s_pendingTransform.state == TransformState::VampireLord) {
			NormalizeVampireLordRaiseDeadSet(s_pendingTransform.latchedForms, representative);
		}

		const std::uint64_t signature = HashVampireLordRaiseDeadSelection(candidates, representative);
		if ((s_vampireLordConfigCache.debugLog || Config::WheelBehavior::TransformWheels::DebugLog) &&
			(!s_hasVampireLordRaiseDeadLogSignature || signature != s_lastVampireLordRaiseDeadLogSignature)) {
			logger::info(
				"TransformWheels: [VampireLordRaiseDead] selected={:08X} source={} rejected={} directCast={} candidates={}",
				representative,
				source,
				FormatVampireLordRaiseDeadRejectedForms(candidates, representative),
				directCastActive,
				candidates.size());
		}
		s_lastVampireLordRaiseDeadLogSignature = signature;
		s_hasVampireLordRaiseDeadLogSignature = true;
	}

	bool IsVampireLordMeleeHandObject(RE::TESForm* form)
	{
		if (!form) {
			return false;
		}
		auto* weapon = form->As<RE::TESObjectWEAP>();
		if (!weapon) {
			return false;
		}
		if (weapon->GetWeaponType() == RE::WEAPON_TYPE::kHandToHandMelee) {
			return true;
		}

		const char* edidC = weapon->GetFormEditorID();
		const char* nameC = weapon->GetName();
		const std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		const std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};
		const std::string_view sourceFile = GetSourceFileName(weapon);
		const bool vampireLike =
			ContainsIgnoreCase(edid, "Vampire") ||
			ContainsIgnoreCase(name, "Vampire") ||
			ContainsIgnoreCase(sourceFile, "Dawnguard");
		const bool meleeLike =
			ContainsIgnoreCase(edid, "Melee") ||
			ContainsIgnoreCase(name, "Melee") ||
			ContainsIgnoreCase(edid, "Claw") ||
			ContainsIgnoreCase(name, "Claw") ||
			ContainsIgnoreCase(edid, "HandToHand") ||
			ContainsIgnoreCase(edid, "Unarmed");
		return vampireLike && meleeLike;
	}

	bool IsVampireLordMagicHandObject(RE::TESForm* form)
	{
		return form && form->As<RE::SpellItem>() != nullptr;
	}

	VampireLordCombatMode GetVampireLordHandObjectMode(RE::PlayerCharacter* pc)
	{
		if (!pc) {
			return VampireLordCombatMode::Unknown;
		}
		RE::TESForm* left = pc->GetEquippedObject(true);
		RE::TESForm* right = pc->GetEquippedObject(false);
		// If a transition briefly exposes both signals, prefer melee. The visual
		// filter is VL-only and only removes hand-cast spells, so this fails toward
		// the safer state without hiding powers.
		if (IsVampireLordMeleeHandObject(left) || IsVampireLordMeleeHandObject(right)) {
			return VampireLordCombatMode::Melee;
		}
		if (IsVampireLordMagicHandObject(left) || IsVampireLordMagicHandObject(right)) {
			return VampireLordCombatMode::BloodMagic;
		}
		return VampireLordCombatMode::Unknown;
	}

	VampireLordCombatMode GetVampireLordCombatMode(RE::PlayerCharacter* pc)
	{
		if (!pc) {
			return VampireLordCombatMode::Unknown;
		}
		auto* fixedStrings = RE::FixedStrings::GetSingleton();
		if (!fixedStrings) {
			return VampireLordCombatMode::Unknown;
		}

		bool leftMagicReady = false;
		bool rightMagicReady = false;
		if (!pc->GetGraphVariableBool(fixedStrings->bMLh_Ready, leftMagicReady) ||
			!pc->GetGraphVariableBool(fixedStrings->bMRh_Ready, rightMagicReady)) {
			return VampireLordCombatMode::Unknown;
		}
		if (leftMagicReady && rightMagicReady) {
			return VampireLordCombatMode::BloodMagic;
		}
		if (!leftMagicReady && !rightMagicReady) {
			return VampireLordCombatMode::Melee;
		}
		return VampireLordCombatMode::Unknown;
	}

	VampireLordCombatMode GetEffectiveVampireLordCombatMode(RE::PlayerCharacter* pc)
	{
		return pc ? s_lastVampireLordCombatMode : VampireLordCombatMode::Unknown;
	}

	bool IsVampireLordHandCasterSettled(RE::MagicCaster* caster)
	{
		if (!caster) {
			return false;
		}
		const auto state = caster->state.get();
		return state == RE::MagicCaster::State::kNone ||
			state == RE::MagicCaster::State::kReady;
	}

	bool IsVampireLordActorSettledForModeObservation(RE::PlayerCharacter* pc)
	{
		if (!pc) {
			return false;
		}
		auto* actorState = pc->AsActorState();
		if (!actorState ||
			actorState->GetWeaponState() != RE::WEAPON_STATE::kDrawn ||
			actorState->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone) {
			return false;
		}

		return IsVampireLordHandCasterSettled(
				   pc->GetMagicCaster(RE::MagicSystem::CastingSource::kLeftHand)) &&
			IsVampireLordHandCasterSettled(
				pc->GetMagicCaster(RE::MagicSystem::CastingSource::kRightHand));
	}

	const char* ToString(VampireLordModeObservationStatus status)
	{
		switch (status) {
		case VampireLordModeObservationStatus::Candidate:
			return "candidate";
		case VampireLordModeObservationStatus::InitialTransform:
			return "initial_transform";
		case VampireLordModeObservationStatus::GraphUnavailableOrMixed:
			return "graph_unavailable_or_mixed";
		case VampireLordModeObservationStatus::ActorUnsettled:
			return "actor_unsettled";
		case VampireLordModeObservationStatus::DirectCastActive:
			return "direct_cast_active";
		default:
			return "unknown";
		}
	}

	VampireLordModeDiagnosticForm DescribeVampireLordModeDiagnosticForm(RE::TESForm* form)
	{
		VampireLordModeDiagnosticForm result;
		if (!form) {
			return result;
		}

		result.formID = form->GetFormID();
		result.formType = static_cast<std::uint32_t>(form->GetFormType());
		result.editorID = form->GetFormEditorID() ? form->GetFormEditorID() : "";
		result.name = form->GetName() ? form->GetName() : "";
		result.source = GetSourceFileName(form);
		if (auto* weapon = form->As<RE::TESObjectWEAP>()) {
			result.weaponType = static_cast<std::int32_t>(weapon->GetWeaponType());
		}
		if (auto* spell = form->As<RE::SpellItem>()) {
			result.spellType = static_cast<std::int32_t>(spell->GetSpellType());
			result.castingType = static_cast<std::int32_t>(spell->GetCastingType());
		}
		return result;
	}

	VampireLordModeDiagnosticSnapshot CaptureVampireLordModeDiagnosticSnapshot(
		RE::PlayerCharacter* pc,
		VampireLordCombatMode rawMode,
		VampireLordCombatMode effectiveMode,
		VampireLordCombatMode debounceCandidate,
		VampireLordModeObservationStatus observationStatus,
		bool actorSettled,
		bool directCastActive)
	{
		VampireLordModeDiagnosticSnapshot snapshot;
		snapshot.rawMode = rawMode;
		snapshot.effectiveMode = effectiveMode;
		snapshot.debounceCandidate = debounceCandidate;
		snapshot.observationStatus = observationStatus;
		snapshot.actorSettled = actorSettled;
		snapshot.directCastActive = directCastActive;
		if (!pc) {
			return snapshot;
		}
		snapshot.handObjectMode = GetVampireLordHandObjectMode(pc);

		snapshot.left = DescribeVampireLordModeDiagnosticForm(pc->GetEquippedObject(true));
		snapshot.right = DescribeVampireLordModeDiagnosticForm(pc->GetEquippedObject(false));
		snapshot.selectedPower = DescribeVampireLordModeDiagnosticForm(pc->GetActorRuntimeData().selectedPower);
		snapshot.race = DescribeVampireLordModeDiagnosticForm(pc->GetRace());
		snapshot.inCombat = pc->IsInCombat();

		if (auto* actorState = pc->AsActorState()) {
			snapshot.actorStateAvailable = true;
			snapshot.weaponState = static_cast<std::uint32_t>(actorState->GetWeaponState());
			snapshot.attackState = static_cast<std::uint32_t>(actorState->GetAttackState());
			snapshot.lifeState = static_cast<std::uint32_t>(actorState->GetLifeState());
			snapshot.weaponDrawn = actorState->IsWeaponDrawn();
		}

		if (auto* fixedStrings = RE::FixedStrings::GetSingleton()) {
			snapshot.equipOK.queried = pc->GetGraphVariableBool(fixedStrings->bEquipOK, snapshot.equipOK.value);
			snapshot.attackReady.queried = pc->GetGraphVariableBool(fixedStrings->isAttackReady, snapshot.attackReady.value);
			snapshot.leftMagicReady.queried = pc->GetGraphVariableBool(fixedStrings->bMLh_Ready, snapshot.leftMagicReady.value);
			snapshot.rightMagicReady.queried = pc->GetGraphVariableBool(fixedStrings->bMRh_Ready, snapshot.rightMagicReady.value);
			snapshot.leftHand.queried = pc->GetGraphVariableBool(fixedStrings->bLeftHand, snapshot.leftHand.value);
			snapshot.rightHand.queried = pc->GetGraphVariableBool(fixedStrings->bRightHand, snapshot.rightHand.value);
			snapshot.leftHandType.queried = pc->GetGraphVariableInt(fixedStrings->iLeftHandType, snapshot.leftHandType.value);
			snapshot.rightHandType.queried = pc->GetGraphVariableInt(fixedStrings->iRightHandType, snapshot.rightHandType.value);
			snapshot.leftHandEquipped.queried = pc->GetGraphVariableInt(fixedStrings->iLeftHandEquipped, snapshot.leftHandEquipped.value);
			snapshot.rightHandEquipped.queried = pc->GetGraphVariableInt(fixedStrings->iRightHandEquipped, snapshot.rightHandEquipped.value);
		}
		if (auto* caster = pc->GetMagicCaster(RE::MagicSystem::CastingSource::kLeftHand)) {
			snapshot.leftCasterState.queried = true;
			snapshot.leftCasterState.value = static_cast<std::uint32_t>(caster->state.get());
		}
		if (auto* caster = pc->GetMagicCaster(RE::MagicSystem::CastingSource::kRightHand)) {
			snapshot.rightCasterState.queried = true;
			snapshot.rightCasterState.value = static_cast<std::uint32_t>(caster->state.get());
		}
		return snapshot;
	}

	void UpdateVampireLordModeDiagnostics(
		RE::PlayerCharacter* pc,
		VampireLordCombatMode rawMode,
		VampireLordCombatMode effectiveMode,
		VampireLordCombatMode debounceCandidate,
		std::uint32_t debounceSamples,
		double debounceSince,
		double now,
		VampireLordModeObservationStatus observationStatus,
		bool actorSettled,
		bool directCastActive,
		bool initial)
	{
		if (!pc || !(s_vampireLordConfigCache.debugLog || Config::WheelBehavior::TransformWheels::DebugLog)) {
			s_lastVampireLordModeDiagnosticSnapshot.reset();
			return;
		}

		auto snapshot = CaptureVampireLordModeDiagnosticSnapshot(
			pc,
			rawMode,
			effectiveMode,
			debounceCandidate,
			observationStatus,
			actorSettled,
			directCastActive);
		const bool firstSnapshot = initial || !s_lastVampireLordModeDiagnosticSnapshot.has_value();
		if (!firstSnapshot && snapshot == *s_lastVampireLordModeDiagnosticSnapshot) {
			return;
		}

		const auto& left = snapshot.left;
		const auto& right = snapshot.right;
		const auto& power = snapshot.selectedPower;
		const auto& race = snapshot.race;
		logger::info(
			"TransformWheels: [VampireLordModeDiag] event={} raw={} handRaw={} effective={} candidate={} debounceSamples={} debounceMs={:.1f} observation={} directCast={} settled={} "
			"left[id={:08X} type={} weaponType={} spellType={} castingType={} edid='{}' name='{}' source='{}'] "
			"right[id={:08X} type={} weaponType={} spellType={} castingType={} edid='{}' name='{}' source='{}'] "
			"selected[id={:08X} type={} weaponType={} spellType={} castingType={} edid='{}' name='{}' source='{}'] "
			"race[id={:08X} type={} edid='{}' name='{}' source='{}'] "
			"actor[stateQuery={} weaponState={} attackState={} lifeState={} weaponDrawn={} inCombat={} leftCaster={}/{} rightCaster={}/{}] "
			"graph[bEquipOk={}/{} attackReady={}/{} bMLhReady={}/{} bMRhReady={}/{} bLeftHand={}/{} bRightHand={}/{} "
			"iLeftHandType={}/{} iRightHandType={}/{} iLeftHandEquipped={}/{} iRightHandEquipped={}/{}]",
			firstSnapshot ? "initial" : "changed",
			ToString(snapshot.rawMode),
			ToString(snapshot.handObjectMode),
			ToString(snapshot.effectiveMode),
			ToString(snapshot.debounceCandidate),
			debounceSamples,
			(debounceSamples != 0) ? (std::max)(0.0, (now - debounceSince) * 1000.0) : 0.0,
			ToString(snapshot.observationStatus),
			snapshot.directCastActive,
			snapshot.actorSettled,
			left.formID,
			left.formType,
			left.weaponType,
			left.spellType,
			left.castingType,
			left.editorID,
			left.name,
			left.source,
			right.formID,
			right.formType,
			right.weaponType,
			right.spellType,
			right.castingType,
			right.editorID,
			right.name,
			right.source,
			power.formID,
			power.formType,
			power.weaponType,
			power.spellType,
			power.castingType,
			power.editorID,
			power.name,
			power.source,
			race.formID,
			race.formType,
			race.editorID,
			race.name,
			race.source,
			snapshot.actorStateAvailable,
			snapshot.weaponState,
			snapshot.attackState,
			snapshot.lifeState,
			snapshot.weaponDrawn,
			snapshot.inCombat,
			snapshot.leftCasterState.queried,
			snapshot.leftCasterState.value,
			snapshot.rightCasterState.queried,
			snapshot.rightCasterState.value,
			snapshot.equipOK.queried,
			snapshot.equipOK.value,
			snapshot.attackReady.queried,
			snapshot.attackReady.value,
			snapshot.leftMagicReady.queried,
			snapshot.leftMagicReady.value,
			snapshot.rightMagicReady.queried,
			snapshot.rightMagicReady.value,
			snapshot.leftHand.queried,
			snapshot.leftHand.value,
			snapshot.rightHand.queried,
			snapshot.rightHand.value,
			snapshot.leftHandType.queried,
			snapshot.leftHandType.value,
			snapshot.rightHandType.queried,
			snapshot.rightHandType.value,
			snapshot.leftHandEquipped.queried,
			snapshot.leftHandEquipped.value,
			snapshot.rightHandEquipped.queried,
			snapshot.rightHandEquipped.value);

		s_lastVampireLordModeDiagnosticSnapshot = std::move(snapshot);
	}

	bool ShouldHideVampireLordSpellForCombatMode(RE::SpellItem* spell, RE::PlayerCharacter* pc, const char* source)
	{
		if (!spell || !pc) {
			return false;
		}
		if (GetCurrentTransformState(pc) != TransformState::VampireLord) {
			return false;
		}
		RefreshVampireLordConfigCache();
		if (!s_vampireLordConfigCache.enabled ||
			!s_vampireLordConfigCache.blockRegularSpellsInMeleeMode) {
			return false;
		}
		if (spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell) {
			return false;
		}
		if (IsVampireLordExitSpell(spell)) {
			return false;
		}
		// Keep this narrow: only VL kit/FormList/config spells get the melee-mode
		// visibility rule. Human/base wheel spells that merely exist during VL form
		// are not globally hidden by type.
		if (!IsTrustedVampireLordWheelSpell(spell)) {
			return false;
		}

		const VampireLordCombatMode mode = GetEffectiveVampireLordCombatMode(pc);
		if (mode != VampireLordCombatMode::Melee) {
			return false;
		}

		if (s_vampireLordConfigCache.debugLog) {
			const std::uint64_t key =
				(static_cast<std::uint64_t>(static_cast<std::uint8_t>(mode)) << 56) |
				static_cast<std::uint64_t>(spell->GetFormID());
			if (s_loggedVampireLordModeHiddenForms.insert(key).second) {
				RE::TESForm* left = pc->GetEquippedObject(true);
				RE::TESForm* right = pc->GetEquippedObject(false);
				logger::info(
					"TransformWheels: [VampireLordModeFilter] hid regular spell {:08X} edid='{}' name='{}' reason=melee_mode source='{}' mode={} rawMode={} left={:08X} right={:08X}",
					spell->GetFormID(),
					spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
					spell->GetName() ? spell->GetName() : "",
					source ? source : "",
					ToString(mode),
					ToString(GetVampireLordCombatMode(pc)),
					left ? left->GetFormID() : 0,
					right ? right->GetFormID() : 0);
			}
		}
		return true;
	}

	void ClearVampireLordSessionSpells(const char* reason)
	{
		s_vampireLordRaiseDeadRepresentative = 0;
		s_lastVampireLordRaiseDeadLogSignature = 0;
		s_hasVampireLordRaiseDeadLogSignature = false;
		if (s_vampireLordSessionSpellForms.empty()) {
			s_vampireLordLoggedRejectedEquippedForms.clear();
			return;
		}
		if (s_vampireLordConfigCache.debugLog || Config::WheelBehavior::TransformWheels::DebugLog) {
			logger::info(
				"TransformWheels: [VampireLordSticky] cleared session spells count={} reason={}",
				s_vampireLordSessionSpellForms.size(),
				reason ? reason : "");
		}
		s_vampireLordSessionSpellForms.clear();
		s_vampireLordLoggedRejectedEquippedForms.clear();
	}

	std::size_t PruneVampireLordSessionSpells()
	{
		std::size_t removed = 0;
		for (auto it = s_vampireLordSessionSpellForms.begin(); it != s_vampireLordSessionSpellForms.end();) {
			auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(*it);
			if (!IsTrustedVampireLordWheelSpell(spell) ||
				(IsVampireLordRaiseDeadFamilySpell(spell) && *it != s_vampireLordRaiseDeadRepresentative)) {
				it = s_vampireLordSessionSpellForms.erase(it);
				++removed;
				continue;
			}
			++it;
		}
		return removed;
	}

	bool AddVampireLordSessionSpell(RE::FormID formId, const char* source)
	{
		auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
		if (!IsTrustedVampireLordWheelSpell(spell)) {
			return false;
		}
		if (IsVampireLordRaiseDeadFamilySpell(spell) &&
			formId != s_vampireLordRaiseDeadRepresentative) {
			return false;
		}
		const bool inserted = s_vampireLordSessionSpellForms.insert(formId).second;
		if (inserted && s_vampireLordConfigCache.debugLog) {
			logger::info(
				"TransformWheels: [VampireLordSticky] remembered spell {:08X} edid='{}' name='{}' source={}",
				formId,
				spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
				spell->GetName() ? spell->GetName() : "",
				source ? source : "");
		}
		return inserted;
	}

	std::size_t AddVampireLordSessionSpells(
		const std::unordered_set<RE::FormID>& forms,
		const char* source)
	{
		std::size_t added = 0;
		for (const auto formId : forms) {
			if (AddVampireLordSessionSpell(formId, source)) {
				++added;
			}
		}
		return added;
	}

	// Check if a spell looks like it belongs to the transform kit based on EditorID/Name
	bool IsTransformKitSpell(RE::SpellItem* spell, TransformState state)
	{
		if (!spell) {
			return false;
		}
		const char* edidC = spell->GetFormEditorID();
		const char* nameC = spell->GetName();
		std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};

		if (state == TransformState::Werewolf) {
			RefreshWerewolfConfigCache();
			if (IsWerewolfConfiguredAdditionalSpell(spell)) {
				return true;
			}
			if (IsWerewolfExitSpell(spell)) {
				return true;
			}
			if (IsWerewolfTransformEntrySpell(spell)) {
				return false;
			}
			return (s_werewolfConfigCache.enabled &&
					(MatchesAnySubstringToken(edid, s_werewolfConfigCache.spellTokens, 2) ||
						MatchesAnySubstringToken(name, s_werewolfConfigCache.spellTokens, 2))) ||
				ContainsIgnoreCase(edid, "Werewolf") ||
				ContainsIgnoreCase(edid, "Howl") ||
				ContainsIgnoreCase(edid, "Growl") ||
				ContainsIgnoreCase(edid, "Lycan") ||
				ContainsIgnoreCase(edid, "Manbeast") ||
				ContainsIgnoreCase(name, "Werewolf") ||
				ContainsIgnoreCase(name, "Howl") ||
				ContainsIgnoreCase(name, "Night Eye") ||
				ContainsIgnoreCase(name, "Totem") ||
				ContainsIgnoreCase(name, "Predator") ||
				ContainsIgnoreCase(name, "Savage") ||
				ContainsIgnoreCase(name, "Terror") ||
				ContainsIgnoreCase(name, "Hunt") ||
				ContainsIgnoreCase(name, "Brotherhood") ||
				ContainsIgnoreCase(name, "Call of the Wild") ||
				ContainsIgnoreCase(name, "Moonlight") ||
				ContainsIgnoreCase(name, "Lycan") ||
				ContainsIgnoreCase(name, "Feed");
		}
		if (state == TransformState::VampireLord) {
			if (IsVampireLordHiddenSpell(spell)) {
				return false;
			}
			if (IsVampireLordExitSpell(spell)) {
				return true;
			}
			if (IsVampireLordConfiguredAdditionalSpell(spell)) {
				return true;
			}
			return (s_vampireLordConfigCache.enabled &&
					(MatchesAnySubstringToken(edid, s_vampireLordConfigCache.spellTokens, 2) ||
						MatchesAnySubstringToken(name, s_vampireLordConfigCache.spellTokens, 2))) ||
				ContainsIgnoreCase(edid, "VampireLord") ||
				ContainsIgnoreCase(edid, "DLC1Vampire") ||
				ContainsIgnoreCase(edid, "Vampiric") ||
				ContainsIgnoreCase(name, "Vampire Lord") ||
				ContainsIgnoreCase(name, "Vampiric");
		}
		if (state == TransformState::Lich) {
			RefreshLichConfigCache();
			if (s_lichConfigCache.additionalSpellFormIDs.contains(spell->GetFormID())) {
				return true;
			}
			if (IsLichExitSpell(spell)) {
				return true;
			}
			if (ContainsIgnoreCase(name, "Revert") ||
				ContainsIgnoreCase(name, "Death Grip") ||
				ContainsIgnoreCase(name, "Ice Coffin") ||
				ContainsIgnoreCase(name, "Dark Conduit") ||
				ContainsIgnoreCase(edid, "DeathGrip") ||
				ContainsIgnoreCase(edid, "IceCoffin") ||
				ContainsIgnoreCase(edid, "DarkConduit")) {
				return true;
			}
			if (MatchesAnySubstringToken(edid, s_lichConfigCache.spellTokens, 2) ||
				MatchesAnySubstringToken(name, s_lichConfigCache.spellTokens, 2)) {
				return true;
			}
		}
		return false;
	}

	bool IsGenericTransformHintSpell(RE::SpellItem* spell, bool allowGenericTokens)
	{
		if (!spell) {
			return false;
		}

		RefreshGenericTriggerCache();

		const char* edidC = spell->GetFormEditorID();
		const char* nameC = spell->GetName();
		std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};

		// Common convention across transform mods (Lost Grimoire, Undeath, etc.).
		if (ContainsIgnoreCase(edid, "Revert") || ContainsIgnoreCase(name, "Revert")) {
			return true;
		}

		if (!allowGenericTokens) {
			return false;
		}

		auto matchesToken = [&](std::string_view token) {
			const std::string trimmed = TrimCopy(token);
			if (trimmed.size() < 3) {
				return false;
			}
			const std::string_view pattern(trimmed);
			return ContainsIgnoreCase(edid, pattern) || ContainsIgnoreCase(name, pattern);
		};

		for (const auto& token : s_genericTriggerCache.raceEditorIdContains) {
			if (matchesToken(token)) {
				return true;
			}
		}
		for (const auto& token : s_genericTriggerCache.raceKeywords) {
			if (matchesToken(token)) {
				return true;
			}
		}

		return false;
	}

	bool IsLichExitSpell(RE::SpellItem* spell)
	{
		if (!spell) {
			return false;
		}
		RefreshLichConfigCache();
		const char* edidC = spell->GetFormEditorID();
		const char* nameC = spell->GetName();
		std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};
		if (ContainsIgnoreCase(edid, "Revert") || ContainsIgnoreCase(name, "Revert")) {
			return true;
		}
		return MatchesAnySubstringToken(edid, s_lichConfigCache.exitSpellTokens, 2) ||
			MatchesAnySubstringToken(name, s_lichConfigCache.exitSpellTokens, 2);
	}

	bool IsTransformExitSpellForState(RE::SpellItem* spell, TransformState state)
	{
		if (!spell) {
			return false;
		}
		switch (state) {
		case TransformState::VampireLord:
			return IsVampireLordExitSpell(spell);
		case TransformState::Lich:
			return IsLichExitSpell(spell);
		default:
			return false;
		}
	}

	std::vector<std::string> BuildTransformTokensForGuard()
	{
		std::vector<std::string> tokens{
			"Transform", "Transformation", "Change", "Polymorph", "Beast", "Werewolf",
			"Werebear", "Vampire", "VampireLord", "Lich", "Aspect", "Revert Form"
		};
		RefreshGenericTriggerCache();
		RefreshWerewolfConfigCache();
		RefreshVampireLordConfigCache();
		RefreshLichConfigCache();
		for (const auto& token : s_genericTriggerCache.raceEditorIdContains) {
			tokens.push_back(token);
		}
		for (const auto& token : s_genericTriggerCache.raceKeywords) {
			tokens.push_back(token);
		}
		if (s_werewolfConfigCache.enabled) {
			for (const auto& token : s_werewolfConfigCache.spellTokens) {
				tokens.push_back(token);
			}
			for (const auto& token : s_werewolfConfigCache.exitSpellTokens) {
				tokens.push_back(token);
			}
		}
		if (s_vampireLordConfigCache.enabled) {
			for (const auto& token : s_vampireLordConfigCache.spellTokens) {
				tokens.push_back(token);
			}
			for (const auto& token : s_vampireLordConfigCache.exitSpellTokens) {
				tokens.push_back(token);
			}
		}
		for (const auto& token : s_lichConfigCache.spellTokens) {
			tokens.push_back(token);
		}
		return tokens;
	}

	bool IsLikelyTransformEntrySpell(RE::SpellItem* spell)
	{
		if (!spell) {
			return false;
		}
		const auto spellType = spell->GetSpellType();
		const bool likelyTransformType =
			spellType == RE::MagicSystem::SpellType::kPower ||
			spellType == RE::MagicSystem::SpellType::kLesserPower ||
			spellType == RE::MagicSystem::SpellType::kVoicePower;
		if (!likelyTransformType) {
			return false;
		}
		const char* edidC = spell->GetFormEditorID();
		const char* nameC = spell->GetName();
		std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};
		static std::vector<std::string> s_cachedTokens;
		static std::size_t s_tokenSignature = 0;
		std::size_t signature = BuildGenericConfigSignature() ^
			(BuildWerewolfConfigSignature() << 1) ^
			(BuildVampireLordConfigSignature() << 2) ^
			(BuildLichConfigSignature() << 3);
		if (signature != s_tokenSignature) {
			s_tokenSignature = signature;
			s_cachedTokens = BuildTransformTokensForGuard();
		}
		return MatchesAnySubstringToken(edid, s_cachedTokens, 3) ||
			MatchesAnySubstringToken(name, s_cachedTokens, 3);
	}

	const char* ToString(MajorTransformKind kind)
	{
		switch (kind) {
		case MajorTransformKind::Werewolf:
			return "Werewolf";
		case MajorTransformKind::VampireLord:
			return "VampireLord";
		case MajorTransformKind::Lich:
			return "Lich";
		case MajorTransformKind::Polymorph:
			return "Polymorph";
		case MajorTransformKind::None:
		default:
			return "None";
		}
	}

	const char* ToString(VampireLordCombatMode mode)
	{
		switch (mode) {
		case VampireLordCombatMode::Melee:
			return "Melee";
		case VampireLordCombatMode::BloodMagic:
			return "BloodMagic";
		case VampireLordCombatMode::Unknown:
		default:
			return "Unknown";
		}
	}

	bool IsMajorTransformRuntimeState(TransformState state)
	{
		switch (state) {
		case TransformState::Werewolf:
		case TransformState::VampireLord:
		case TransformState::Lich:
		case TransformState::Generic:
			return true;
		case TransformState::Human:
		default:
			return false;
		}
	}

	bool IsTransformDebugEnabledForState(TransformState state)
	{
		if (Config::WheelBehavior::TransformWheels::DebugLog) {
			return true;
		}
		switch (state) {
		case TransformState::Werewolf:
			RefreshWerewolfConfigCache();
			return s_werewolfConfigCache.debugLog;
		case TransformState::VampireLord:
			RefreshVampireLordConfigCache();
			return s_vampireLordConfigCache.debugLog;
		case TransformState::Lich:
			RefreshLichConfigCache();
			return s_lichConfigCache.debugLog;
		case TransformState::Generic:
		case TransformState::Human:
		default:
			return false;
		}
	}

	bool HasTransformVerb(std::string_view edid, std::string_view name)
	{
		return ContainsIgnoreCase(edid, "Transform") ||
			ContainsIgnoreCase(name, "Transform") ||
			ContainsIgnoreCase(edid, "Transformation") ||
			ContainsIgnoreCase(name, "Transformation") ||
			ContainsIgnoreCase(edid, "ChangeForm") ||
			ContainsIgnoreCase(name, "Change Form") ||
			ContainsIgnoreCase(edid, "Assume") ||
			ContainsIgnoreCase(name, "Assume") ||
			ContainsIgnoreCase(edid, "Become") ||
			ContainsIgnoreCase(name, "Become") ||
			ContainsIgnoreCase(edid, "Polymorph") ||
			ContainsIgnoreCase(name, "Polymorph") ||
			ContainsIgnoreCase(edid, "Morph") ||
			ContainsIgnoreCase(name, "Morph") ||
			ContainsIgnoreCase(edid, "Shift") ||
			ContainsIgnoreCase(name, "Shift");
	}

	bool IsTransformEntrySpellType(RE::SpellItem* spell)
	{
		if (!spell) {
			return false;
		}
		switch (spell->GetSpellType()) {
		case RE::MagicSystem::SpellType::kSpell:
		case RE::MagicSystem::SpellType::kPower:
		case RE::MagicSystem::SpellType::kLesserPower:
		case RE::MagicSystem::SpellType::kVoicePower:
			return true;
		default:
			return false;
		}
	}

	bool MatchesGenericTransformToken(std::string_view edid, std::string_view name)
	{
		RefreshGenericTriggerCache();
		return MatchesAnySubstringToken(edid, s_genericTriggerCache.raceEditorIdContains, 3) ||
			MatchesAnySubstringToken(name, s_genericTriggerCache.raceEditorIdContains, 3) ||
			MatchesAnySubstringToken(edid, s_genericTriggerCache.raceKeywords, 3) ||
			MatchesAnySubstringToken(name, s_genericTriggerCache.raceKeywords, 3);
	}

	bool IsKnownTransformExitSpell(RE::SpellItem* spell)
	{
		return IsWerewolfExitSpell(spell) ||
			IsVampireLordExitSpell(spell) ||
			IsLichExitSpell(spell);
	}

	bool IsWerewolfMajorTransformSpell(RE::SpellItem* spell, std::string_view edid, std::string_view name)
	{
		if (!spell || IsWerewolfExitSpell(spell)) {
			return false;
		}
		if (FormMatchesPluginRelativeID(spell, kSkyrimPlugin, kBeastFormPowerFormID)) {
			return true;
		}
		if (ContainsIgnoreCase(edid, "BeastForm") || EqualsIgnoreCase(name, "Beast Form")) {
			return true;
		}
		const bool werebeastContext =
			ContainsIgnoreCase(edid, "Werewolf") ||
			ContainsIgnoreCase(name, "Werewolf") ||
			ContainsIgnoreCase(edid, "Werebear") ||
			ContainsIgnoreCase(name, "Werebear") ||
			ContainsIgnoreCase(edid, "Lycan") ||
			ContainsIgnoreCase(name, "Lycan");
		return werebeastContext && HasTransformVerb(edid, name);
	}

	bool IsVampireLordMajorTransformSpell(RE::SpellItem* spell, std::string_view edid, std::string_view name)
	{
		if (!spell || IsVampireLordExitSpell(spell)) {
			return false;
		}
		if (FormMatchesPluginRelativeID(spell, kDawnguardPlugin, kVampireLordPowerFormID)) {
			return true;
		}
		const bool vampireLordContext =
			ContainsIgnoreCase(edid, "VampireLord") ||
			ContainsIgnoreCase(edid, "VampireBeast") ||
			ContainsIgnoreCase(name, "Vampire Lord");
		return EqualsIgnoreCase(name, "Vampire Lord") ||
			(vampireLordContext && HasTransformVerb(edid, name));
	}

	bool IsLichMajorTransformSpell(RE::SpellItem* spell, std::string_view edid, std::string_view name)
	{
		if (!spell || IsLichExitSpell(spell)) {
			return false;
		}
		const bool explicitLichForm =
			ContainsIgnoreCase(edid, "AssumeLich") ||
			ContainsIgnoreCase(name, "Assume Lich") ||
			EqualsIgnoreCase(edid, "LichForm") ||
			EqualsIgnoreCase(name, "Lich Form") ||
			ContainsIgnoreCase(edid, "BecomeLich") ||
			ContainsIgnoreCase(name, "Become Lich");
		if (explicitLichForm) {
			return true;
		}
		const bool lichContext =
			ContainsIgnoreCase(edid, "Lich") ||
			ContainsIgnoreCase(name, "Lich") ||
			ContainsIgnoreCase(edid, "NecroLich") ||
			ContainsIgnoreCase(name, "Necro Lich") ||
			ContainsIgnoreCase(edid, "UCL");
		return lichContext && HasTransformVerb(edid, name);
	}

	bool IsPolymorphMajorTransformSpell(RE::SpellItem* spell, std::string_view edid, std::string_view name)
	{
		if (!spell || IsKnownTransformExitSpell(spell)) {
			return false;
		}
		if (!IsTransformEntrySpellType(spell)) {
			return false;
		}
		if (spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect) {
			return false;
		}

		// Polymorph mods commonly ship regular Spell records, not Powers. Treat
		// only explicit polymorph entries or configured generic-transform tokens
		// with a transform verb as conflicting transforms.
		if (ContainsIgnoreCase(edid, "Polymorph") || ContainsIgnoreCase(name, "Polymorph")) {
			return true;
		}
		return HasTransformVerb(edid, name) && MatchesGenericTransformToken(edid, name);
	}

	MajorTransformKind GetMajorTransformSpellKind(RE::SpellItem* spell)
	{
		if (!spell) {
			return MajorTransformKind::None;
		}
		const char* edidC = spell->GetFormEditorID();
		const char* nameC = spell->GetName();
		const std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		const std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};

		if (IsWerewolfMajorTransformSpell(spell, edid, name)) {
			return MajorTransformKind::Werewolf;
		}
		if (IsVampireLordMajorTransformSpell(spell, edid, name)) {
			return MajorTransformKind::VampireLord;
		}
		if (IsLichMajorTransformSpell(spell, edid, name)) {
			return MajorTransformKind::Lich;
		}
		if (IsPolymorphMajorTransformSpell(spell, edid, name)) {
			return MajorTransformKind::Polymorph;
		}
		return MajorTransformKind::None;
	}

	bool ShouldHideMajorTransformSpellForState(RE::SpellItem* spell, TransformState state, const char* source)
	{
		if (!spell || !IsMajorTransformRuntimeState(state)) {
			return false;
		}
		const MajorTransformKind kind = GetMajorTransformSpellKind(spell);
		if (kind == MajorTransformKind::None) {
			return false;
		}
		if (IsTransformDebugEnabledForState(state)) {
			static std::unordered_set<std::uint64_t> s_loggedHiddenMajorTransformSpells;
			const std::uint64_t key =
				(static_cast<std::uint64_t>(static_cast<std::uint8_t>(state)) << 56) |
				static_cast<std::uint64_t>(spell->GetFormID());
			if (s_loggedHiddenMajorTransformSpells.insert(key).second) {
				logger::info(
					"TransformWheels: [TransformSpellFilter] hid major transform spell {:08X} sourceFile='{}' edid='{}' name='{}' state={} spellKind={} source={}",
					spell->GetFormID(),
					GetSourceFileName(spell),
					spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
					spell->GetName() ? spell->GetName() : "",
					ToString(state),
					ToString(kind),
					source ? source : "");
			}
		}
		return true;
	}

	bool ShouldBlockMajorTransformSpellForCurrentState(RE::SpellItem* spell, const char* source)
	{
		if (!spell) {
			return false;
		}
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return false;
		}
		const TransformState currentState = GetCurrentTransformState(pc);
		if (!IsMajorTransformRuntimeState(currentState)) {
			return false;
		}
		const MajorTransformKind kind = GetMajorTransformSpellKind(spell);
		if (kind == MajorTransformKind::None) {
			return false;
		}

		// Major transformation entries are actor-state mutations, not ordinary combat
		// spells. Block them only after a confirmed transformed race/state so Lich
		// base-wheel access and ordinary transform-kit abilities remain untouched.
		logger::info(
			"TransformWheels: [TransformSpellGuard] blocked major transform spell {:08X} sourceFile='{}' edid='{}' name='{}' currentState={} spellKind={} source={} reason=already_transformed",
			spell->GetFormID(),
			GetSourceFileName(spell),
			spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
			spell->GetName() ? spell->GetName() : "",
			ToString(currentState),
			ToString(kind),
			source ? source : "");
		return true;
	}

	const char* SpellTypeToString(RE::MagicSystem::SpellType type)
	{
		switch (type) {
		case RE::MagicSystem::SpellType::kSpell:
			return "Spell";
		case RE::MagicSystem::SpellType::kPower:
			return "Power";
		case RE::MagicSystem::SpellType::kLesserPower:
			return "LesserPower";
		case RE::MagicSystem::SpellType::kAbility:
			return "Ability";
		case RE::MagicSystem::SpellType::kDisease:
			return "Disease";
		case RE::MagicSystem::SpellType::kPoison:
			return "Poison";
		case RE::MagicSystem::SpellType::kVoicePower:
			return "VoicePower";
		default:
			return "Other";
		}
	}

	const char* CastingTypeToString(RE::MagicSystem::CastingType type)
	{
		switch (type) {
		case RE::MagicSystem::CastingType::kConstantEffect:
			return "ConstantEffect";
		case RE::MagicSystem::CastingType::kFireAndForget:
			return "FireAndForget";
		case RE::MagicSystem::CastingType::kConcentration:
			return "Concentration";
		default:
			return "Other";
		}
	}

	bool IsConfiguredSpellForState(RE::SpellItem* spell, TransformState state)
	{
		if (!spell) {
			return false;
		}
		switch (state) {
		case TransformState::Werewolf:
			return IsWerewolfConfiguredAdditionalSpell(spell);
		case TransformState::VampireLord:
			return IsVampireLordConfiguredAdditionalSpell(spell);
		case TransformState::Lich:
			RefreshLichConfigCache();
			return s_lichConfigCache.additionalSpellFormIDs.contains(spell->GetFormID());
		case TransformState::Generic:
		case TransformState::Human:
		default:
			return false;
		}
	}

	bool ShouldTraceTransformCandidateSpell(RE::SpellItem* spell, TransformState state)
	{
		if (!spell || !IsTransformDebugEnabledForState(state)) {
			return false;
		}
		const char* edidC = spell->GetFormEditorID();
		const char* nameC = spell->GetName();
		const std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		const std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};
		const std::string_view source = GetSourceFileName(spell);
		auto matchesTraceToken = [&](std::string_view token) {
			return ContainsIgnoreCase(edid, token) ||
				ContainsIgnoreCase(name, token) ||
				ContainsIgnoreCase(source, token);
		};

		if (state == TransformState::Lich) {
			constexpr std::array<std::string_view, 14> kLichTraceTokens{
				"Lich", "Necro", "UCL", "Undeath", "Revert", "Reaping", "Maelstrom",
				"Malestorm", "Gutwrench", "Echolocation", "Tremble", "Death Grip",
				"Ice Coffin", "Dark Conduit"
			};
			for (const auto token : kLichTraceTokens) {
				if (matchesTraceToken(token)) {
					return true;
				}
			}
			return false;
		}
		if (state == TransformState::VampireLord) {
			constexpr std::array<std::string_view, 19> kVampireTraceTokens{
				"Vampire", "VampireLord", "DLC1Vampire", "Raze", "Raise Dead",
				"Raised Dead", "Choke", "Sight", "Hunter", "Blood Storm", "Bats",
				"Gargoyle", "Death Hound", "Drain", "Mist Form", "Vampiric",
				"Sacrosanct", "Revert", "Change Form"
			};
			for (const auto token : kVampireTraceTokens) {
				if (matchesTraceToken(token)) {
					return true;
				}
			}
			if (EqualsIgnoreCase(source, kDawnguardPlugin)) {
				const auto spellType = spell->GetSpellType();
				return spellType == RE::MagicSystem::SpellType::kPower ||
					spellType == RE::MagicSystem::SpellType::kLesserPower ||
					spellType == RE::MagicSystem::SpellType::kVoicePower ||
					SpellHasVampireLordRevertEffect(spell) ||
					IsVampireLordFormListSpell(spell);
			}
			return false;
		}
		return false;
	}

	const char* GetTransformPopulationReason(
		RE::SpellItem* spell,
		TransformState state,
		bool configured,
		bool inCandidate)
	{
		if (!spell) {
			return "not_spell";
		}
		if (spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect &&
			!IsTransformExitSpellForState(spell, state)) {
			return "constant_effect";
		}
		if (state == TransformState::VampireLord && IsVampireLordHiddenSpell(spell)) {
			return "vampire_lord_hidden";
		}
		if (IsMajorTransformRuntimeState(state) &&
			GetMajorTransformSpellKind(spell) != MajorTransformKind::None) {
			return "major_transform_entry";
		}
		if (inCandidate) {
			return "candidate";
		}
		if (!IsActivatableTransformSpell(spell, state)) {
			return "not_activatable_type";
		}
		if (state == TransformState::Werewolf && !IsWerewolfSafeWheelSpell(spell)) {
			return "werewolf_unsafe";
		}
		if (!configured && !IsTransformKitSpell(spell, state)) {
			return "token_or_config_miss";
		}
		return "source_filtered";
	}

	void LogTransformCandidateDiagnostics(
		TransformState state,
		RE::PlayerCharacter* pc,
		const std::unordered_set<RE::FormID>& currentAllSpells,
		const std::unordered_set<RE::FormID>& equippedSpells,
		const std::unordered_set<RE::FormID>& raceSpells,
		const std::unordered_set<RE::FormID>& kitSpells,
		const std::unordered_set<RE::FormID>& candidateSpells)
	{
		if ((state != TransformState::Lich && state != TransformState::VampireLord) ||
			!pc ||
			!IsTransformDebugEnabledForState(state)) {
			return;
		}

		std::unordered_set<RE::FormID> forms = currentAllSpells;
		forms.insert(equippedSpells.begin(), equippedSpells.end());
		forms.insert(raceSpells.begin(), raceSpells.end());
		forms.insert(kitSpells.begin(), kitSpells.end());
		forms.insert(candidateSpells.begin(), candidateSpells.end());

		static std::unordered_set<std::uint64_t> s_loggedPopulationDiagnostics;
		for (const auto formId : forms) {
			auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
			if (!ShouldTraceTransformCandidateSpell(spell, state)) {
				continue;
			}

			const std::uint64_t key =
				(static_cast<std::uint64_t>(static_cast<std::uint8_t>(state)) << 56) |
				static_cast<std::uint64_t>(formId);
			if (!s_loggedPopulationDiagnostics.insert(key).second) {
				continue;
			}

			const bool known = currentAllSpells.contains(formId);
			const bool equipped = equippedSpells.contains(formId);
			const bool race = raceSpells.contains(formId);
			const bool kit = kitSpells.contains(formId);
			const bool candidate = candidateSpells.contains(formId);
			const bool owned = pc->HasSpell(spell);
			const bool configured = IsConfiguredSpellForState(spell, state);
			const MajorTransformKind transformKind = GetMajorTransformSpellKind(spell);
			logger::info(
				"TransformWheels: [{}PopulateDiag] spell {:08X} edid='{}' name='{}' sourceFile='{}' known={} owned={} equipped={} race={} kit={} candidate={} configured={} transformKind={} spellType={} castingType={} reason={}",
				ToString(state),
				formId,
				spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
				spell->GetName() ? spell->GetName() : "",
				GetSourceFileName(spell),
				known,
				owned,
				equipped,
				race,
				kit,
				candidate,
				configured,
				ToString(transformKind),
				SpellTypeToString(spell->GetSpellType()),
				CastingTypeToString(spell->GetCastingType()),
				GetTransformPopulationReason(spell, state, configured, candidate));
		}
	}

	// Collect spells from allSpells that look like transform kit abilities
	void CollectTransformKitSpells(
		const std::unordered_set<RE::FormID>& allSpells,
		TransformState state,
		std::unordered_set<RE::FormID>& out)
	{
		for (auto id : allSpells) {
			auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(id);
			if (!spell) {
				continue;
			}
			const bool transformExitSpell = IsTransformExitSpellForState(spell, state);
			// Vampire Lord and Undeath can expose their Revert Form actions as
			// constant-effect powers. Keep the constant-effect exclusion for normal
			// abilities, but allow confirmed form-exit spells so each transform wheel
			// still offers a safe way back out.
			if (!transformExitSpell &&
				spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect) {
				continue;
			}
			if (ShouldHideMajorTransformSpellForState(spell, state, "kit")) {
				continue;
			}
			if (!transformExitSpell && !IsTransformKitSpell(spell, state)) {
				continue;
			}
			out.insert(id);
		}
	}

	std::size_t CollectVampireLordFormListSpells(
		RE::PlayerCharacter* pc,
		const std::unordered_set<RE::FormID>& currentAllSpells,
		const std::unordered_set<RE::FormID>& equippedSpells,
		const std::unordered_set<RE::FormID>& raceSpells,
		std::unordered_set<RE::FormID>& out)
	{
		RefreshVampireLordConfigCache();
		if (!pc || !s_vampireLordConfigCache.enabled) {
			return 0;
		}
		auto* formList = GetVampireLordSpellsPowersFormList();
		if (!formList) {
			if (s_vampireLordConfigCache.debugLog) {
				logger::info(
					"TransformWheels: [VampireLordFormList] missing {}|{:06X}",
					kDawnguardPlugin,
					kVampireLordSpellsPowersFormListID);
			}
			return 0;
		}

		std::size_t added = 0;
		for (auto* form : formList->forms) {
			auto* spell = form ? form->As<RE::SpellItem>() : nullptr;
			if (!spell) {
				continue;
			}
			const auto formId = spell->GetFormID();
			if (IsVampireLordHiddenSpell(spell)) {
				continue;
			}
			const bool exitSpell = IsVampireLordExitSpell(spell);
			if (!exitSpell &&
				spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect) {
				continue;
			}
			if (!exitSpell && !IsActivatableTransformSpell(spell, TransformState::VampireLord)) {
				continue;
			}

			const bool knownNow =
				currentAllSpells.contains(formId) ||
				equippedSpells.contains(formId) ||
				raceSpells.contains(formId) ||
				pc->HasSpell(spell);
			if (!knownNow) {
				if (s_vampireLordConfigCache.debugLog) {
					logger::info(
						"TransformWheels: [VampireLordFormList] skipped spell {:08X} edid='{}' name='{}' reason=not_known",
						formId,
						spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
						spell->GetName() ? spell->GetName() : "");
				}
				continue;
			}

			if (out.insert(formId).second) {
				++added;
				if (s_vampireLordConfigCache.debugLog) {
					logger::info(
						"TransformWheels: [VampireLordFormList] added spell {:08X} edid='{}' name='{}' exit={}",
						formId,
						spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
						spell->GetName() ? spell->GetName() : "",
						exitSpell);
				}
			}
		}
		return added;
	}

	std::size_t CollectConfiguredLichSpells(
		RE::PlayerCharacter* pc,
		const std::unordered_set<RE::FormID>& currentAllSpells,
		const std::unordered_set<RE::FormID>& equippedSpells,
		const std::unordered_set<RE::FormID>& raceSpells,
		std::unordered_set<RE::FormID>& out)
	{
		RefreshLichConfigCache();
		if (!pc || s_lichConfigCache.additionalSpellFormIDs.empty()) {
			return 0;
		}

		std::size_t added = 0;
		for (const auto formId : s_lichConfigCache.additionalSpellFormIDs) {
			auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
			if (!spell) {
				if (s_lichConfigCache.debugLog) {
					logger::info("TransformWheels: [LichPopulate] skipped configured spell {:08X} reason=not_spell", formId);
				}
				continue;
			}
			const bool lichExitSpell = IsLichExitSpell(spell);
			if (!lichExitSpell && !IsActivatableTransformSpell(spell, TransformState::Lich)) {
				if (s_lichConfigCache.debugLog) {
					logger::info("TransformWheels: [LichPopulate] skipped configured spell {:08X} reason=not_activatable", formId);
				}
				continue;
			}

			const bool knownNow =
				currentAllSpells.contains(formId) ||
				equippedSpells.contains(formId) ||
				raceSpells.contains(formId) ||
				pc->HasSpell(spell);
			if (!knownNow) {
				if (s_lichConfigCache.debugLog) {
					logger::info("TransformWheels: [LichPopulate] skipped configured spell {:08X} reason=not_known", formId);
				}
				continue;
			}

			if (out.insert(formId).second) {
				++added;
				if (s_lichConfigCache.debugLog) {
					logger::info("TransformWheels: [LichPopulate] added configured spell {:08X} edid='{}' name='{}'",
						formId,
						spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
						spell->GetName() ? spell->GetName() : "");
				}
			}
		}
		return added;
	}

	std::size_t CollectConfiguredVampireLordSpells(
		RE::PlayerCharacter* pc,
		const std::unordered_set<RE::FormID>& currentAllSpells,
		const std::unordered_set<RE::FormID>& equippedSpells,
		const std::unordered_set<RE::FormID>& raceSpells,
		std::unordered_set<RE::FormID>& out)
	{
		RefreshVampireLordConfigCache();
		if (!pc ||
			!s_vampireLordConfigCache.enabled ||
			s_vampireLordConfigCache.additionalSpellFormIDs.empty()) {
			return 0;
		}

		std::size_t added = 0;
		for (const auto formId : s_vampireLordConfigCache.additionalSpellFormIDs) {
			auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
			if (!spell) {
				if (s_vampireLordConfigCache.debugLog) {
					logger::info("TransformWheels: [VampireLordPopulate] skipped configured spell {:08X} reason=not_spell", formId);
				}
				continue;
			}
			if (IsVampireLordHiddenSpell(spell)) {
				if (s_vampireLordConfigCache.debugLog) {
					logger::info("TransformWheels: [VampireLordPopulate] skipped configured spell {:08X} reason=hidden", formId);
				}
				continue;
			}
			const bool vampireLordExitSpell = IsVampireLordExitSpell(spell);
			if (!vampireLordExitSpell &&
				!IsActivatableTransformSpell(spell, TransformState::VampireLord)) {
				if (s_vampireLordConfigCache.debugLog) {
					logger::info("TransformWheels: [VampireLordPopulate] skipped configured spell {:08X} reason=not_activatable", formId);
				}
				continue;
			}

			const bool knownNow =
				currentAllSpells.contains(formId) ||
				equippedSpells.contains(formId) ||
				raceSpells.contains(formId) ||
				pc->HasSpell(spell);
			if (!knownNow) {
				if (s_vampireLordConfigCache.debugLog) {
					logger::info("TransformWheels: [VampireLordPopulate] skipped configured spell {:08X} reason=not_known", formId);
				}
				continue;
			}

			if (out.insert(formId).second) {
				++added;
				if (s_vampireLordConfigCache.debugLog) {
					logger::info("TransformWheels: [VampireLordPopulate] added configured spell {:08X} edid='{}' name='{}'",
						formId,
						spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
						spell->GetName() ? spell->GetName() : "");
				}
			}
		}
		return added;
	}

	std::size_t CollectConfiguredWerewolfSpells(
		RE::PlayerCharacter* pc,
		const std::unordered_set<RE::FormID>& currentAllSpells,
		const std::unordered_set<RE::FormID>& equippedSpells,
		const std::unordered_set<RE::FormID>& raceSpells,
		std::unordered_set<RE::FormID>& out)
	{
		RefreshWerewolfConfigCache();
		if (!pc ||
			!s_werewolfConfigCache.enabled ||
			s_werewolfConfigCache.additionalSpellFormIDs.empty()) {
			return 0;
		}

		std::size_t added = 0;
		for (const auto formId : s_werewolfConfigCache.additionalSpellFormIDs) {
			auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
			if (!spell) {
				if (s_werewolfConfigCache.debugLog) {
					logger::info("TransformWheels: [WerewolfPopulate] skipped configured spell {:08X} reason=not_spell", formId);
				}
				continue;
			}
			if (!IsWerewolfSafeWheelSpell(spell)) {
				if (s_werewolfConfigCache.debugLog) {
					logger::info("TransformWheels: [WerewolfPopulate] skipped configured spell {:08X} reason=unsafe", formId);
				}
				continue;
			}

			const bool knownNow =
				currentAllSpells.contains(formId) ||
				equippedSpells.contains(formId) ||
				raceSpells.contains(formId) ||
				pc->HasSpell(spell);
			if (!knownNow) {
				if (s_werewolfConfigCache.debugLog) {
					logger::info("TransformWheels: [WerewolfPopulate] skipped configured spell {:08X} reason=not_known", formId);
				}
				continue;
			}

			if (out.insert(formId).second) {
				++added;
				if (s_werewolfConfigCache.debugLog) {
					logger::info("TransformWheels: [WerewolfPopulate] added configured spell {:08X} edid='{}' name='{}'",
						formId,
						spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
						spell->GetName() ? spell->GetName() : "");
				}
			}
		}
		return added;
	}

	// Check if a shout looks like it belongs to the transform kit based on EditorID/Name
	bool IsTransformKitShout(RE::TESShout* shout, TransformState state)
	{
		if (!shout) {
			return false;
		}
		const char* edidC = shout->GetFormEditorID();
		const char* nameC = shout->GetName();
		std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};

		if (state == TransformState::Werewolf) {
			RefreshWerewolfConfigCache();
			return (s_werewolfConfigCache.enabled &&
					(MatchesAnySubstringToken(edid, s_werewolfConfigCache.spellTokens, 2) ||
						MatchesAnySubstringToken(name, s_werewolfConfigCache.spellTokens, 2) ||
						MatchesAnySubstringToken(edid, s_werewolfConfigCache.exitSpellTokens, 2) ||
						MatchesAnySubstringToken(name, s_werewolfConfigCache.exitSpellTokens, 2))) ||
				ContainsIgnoreCase(edid, "Werewolf") ||
				ContainsIgnoreCase(edid, "Howl") ||
				ContainsIgnoreCase(edid, "HRI_Howl") ||  // Growl EditorID prefix
				ContainsIgnoreCase(edid, "Growl") ||
				ContainsIgnoreCase(edid, "Lycan") ||
				ContainsIgnoreCase(edid, "Manbeast") ||
				ContainsIgnoreCase(name, "Howl") ||
				ContainsIgnoreCase(name, "Revert Form") ||
				ContainsIgnoreCase(name, "Call of the Wild") ||  // Growl shout
				ContainsIgnoreCase(name, "Brotherhood") ||       // Growl shout
				ContainsIgnoreCase(name, "Terror") ||
				ContainsIgnoreCase(name, "Hunt") ||
				ContainsIgnoreCase(name, "Totem") ||
				ContainsIgnoreCase(name, "Predator") ||
				ContainsIgnoreCase(name, "Savage") ||
				ContainsIgnoreCase(name, "Moonlight") ||
				ContainsIgnoreCase(name, "Lycan") ||
				ContainsIgnoreCase(edid, "Revert");
		}
		if (state == TransformState::VampireLord) {
			// Vampire Lord menu entries are SpellItem powers/spells driven by
			// DLC1VampireSpellsPowers. Accepting generic "Revert Form" shouts here
			// can leak werewolf/generic revert shouts into the VL wheel.
			return false;
		}
		if (state == TransformState::Lich) {
			RefreshLichConfigCache();
			return ContainsIgnoreCase(edid, "Lich") ||
				ContainsIgnoreCase(edid, "Necro") ||
				ContainsIgnoreCase(name, "Lich") ||
				ContainsIgnoreCase(name, "Revert") ||
				MatchesAnySubstringToken(edid, s_lichConfigCache.spellTokens, 2) ||
				MatchesAnySubstringToken(name, s_lichConfigCache.spellTokens, 2);
		}
		return false;
	}

	// Collect shouts the player knows that look like transform kit abilities
	// Scans all game shouts via TESDataHandler and filters by GetKnown() + kit keywords
	void CollectTransformKitShouts(
		TransformState state,
		std::unordered_set<RE::FormID>& out)
	{
		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			return;
		}
		const bool debugLog = Config::WheelBehavior::TransformWheels::DebugLog;
		auto& shouts = handler->GetFormArray<RE::TESShout>();

		// Log all known shouts once per transform (for debugging modlist-specific naming)
		static bool s_loggedKnownShouts = false;
		if (debugLog && state == TransformState::Werewolf && !s_loggedKnownShouts) {
			s_loggedKnownShouts = true;
			logger::info("TransformWheels: [DEBUG] Scanning all player-known shouts for Werewolf kit...");
			for (auto* shout : shouts) {
				if (!shout || !shout->GetKnown()) {
					continue;
				}
				const char* edidC = shout->GetFormEditorID();
				const char* nameC = shout->GetName();
				std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
				std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};
				bool isKit = IsTransformKitShout(shout, state);
				logger::info("TransformWheels: [DEBUG] Known shout {:08X} edid='{}' name='{}' isKit={}",
					shout->GetFormID(), edid, name, isKit);
			}
		}

		for (auto* shout : shouts) {
			if (!shout) {
				continue;
			}
			// Check if player knows this shout (at least first word)
			if (!shout->GetKnown()) {
				continue;
			}
			if (state == TransformState::VampireLord && IsVampireLordHiddenShout(shout)) {
				if (s_vampireLordConfigCache.debugLog) {
					logger::info("TransformWheels: [VampireLordFilter] hid shout {:08X} edid='{}' name='{}' source=kitShout",
						shout->GetFormID(),
						shout->GetFormEditorID() ? shout->GetFormEditorID() : "",
						shout->GetName() ? shout->GetName() : "");
				}
				continue;
			}
			// Check if it matches transform kit pattern
			if (!IsTransformKitShout(shout, state)) {
				continue;
			}
			out.insert(shout->GetFormID());
		}
	}

	RE::TESRace* GetWerewolfRace()
	{
		static RE::TESRace* cached = nullptr;
		if (!cached) {
			if (auto* handler = RE::TESDataHandler::GetSingleton()) {
				cached = handler->LookupForm<RE::TESRace>(kWerewolfRaceFormID, kSkyrimPlugin);
			}
		}
		return cached;
	}

	RE::TESRace* GetVampireLordRace()
	{
		static RE::TESRace* cached = nullptr;
		if (!cached) {
			if (auto* handler = RE::TESDataHandler::GetSingleton()) {
				cached = handler->LookupForm<RE::TESRace>(kVampireLordRaceFormID, kDawnguardPlugin);
			}
		}
		return cached;
	}

	bool IsWerewolfRace(RE::TESRace* race)
	{
		if (!race) {
			return false;
		}
		if (auto* known = GetWerewolfRace(); known && race == known) {
			return true;
		}
		RefreshWerewolfConfigCache();
		if (s_werewolfConfigCache.enabled &&
			s_werewolfConfigCache.raceFormIDs.contains(race->GetFormID())) {
			return true;
		}
		const char* editorId = race->GetFormEditorID();
		if (editorId) {
			std::string_view view(editorId);
			if (EqualsIgnoreCase(view, kWerewolfRaceEditorId) || ContainsIgnoreCase(view, "Werewolf")) {
				return true;
			}
			if (s_werewolfConfigCache.enabled &&
				MatchesAnySubstringToken(view, s_werewolfConfigCache.raceEditorIdContains, 3)) {
				return true;
			}
		}
		if (race->HasKeywordString("ActorTypeWerewolf")) {
			return true;
		}
		if (s_werewolfConfigCache.enabled) {
			for (const auto& keyword : s_werewolfConfigCache.raceKeywords) {
				const std::string trimmed = TrimCopy(keyword);
				if (!trimmed.empty() && race->HasKeywordString(trimmed.c_str())) {
					return true;
				}
			}
		}
		return false;
	}

	bool IsVampireLordRace(RE::TESRace* race)
	{
		if (!race) {
			return false;
		}
		if (auto* known = GetVampireLordRace(); known && race == known) {
			return true;
		}
		RefreshVampireLordConfigCache();
		if (s_vampireLordConfigCache.enabled &&
			s_vampireLordConfigCache.raceFormIDs.contains(race->GetFormID())) {
			return true;
		}
		const char* editorId = race->GetFormEditorID();
		if (editorId) {
			std::string_view view(editorId);
			if (EqualsIgnoreCase(view, kVampireLordRaceEditorId) ||
				ContainsIgnoreCase(view, "VampireLord") ||
				ContainsIgnoreCase(view, "VampireBeast")) {
				return true;
			}
			if (s_vampireLordConfigCache.enabled &&
				MatchesAnySubstringToken(view, s_vampireLordConfigCache.raceEditorIdContains, 3)) {
				return true;
			}
		}
		if (race->HasKeywordString("VampireLord")) {
			return true;
		}
		if (s_vampireLordConfigCache.enabled) {
			for (const auto& keyword : s_vampireLordConfigCache.raceKeywords) {
				const std::string trimmed = TrimCopy(keyword);
				if (!trimmed.empty() && race->HasKeywordString(trimmed.c_str())) {
					return true;
				}
			}
		}
		return false;
	}

	TransformState GetCurrentTransformState(RE::PlayerCharacter* pc)
	{
		if (!pc) {
			return TransformState::Human;
		}
		RE::TESRace* race = pc->GetRace();
		if (!race) {
			return TransformState::Human;
		}
		RefreshPrecedenceOrderCache();
		for (auto state : s_precedenceOrder) {
			switch (state) {
			case TransformState::Werewolf:
				if (IsWerewolfRace(race)) {
					return TransformState::Werewolf;
				}
				break;
			case TransformState::VampireLord:
				if (IsVampireLordRace(race)) {
					return TransformState::VampireLord;
				}
				break;
			case TransformState::Lich:
				if (IsLichTransformRace(race)) {
					return TransformState::Lich;
				}
				break;
			case TransformState::Generic:
				if (IsGenericTransformRace(race)) {
					return TransformState::Generic;
				}
				break;
			case TransformState::Human:
			default:
				break;
			}
		}
		return TransformState::Human;
	}

	bool TryResolveVisitedSpellFormID(RE::SpellItem* spell, RE::FormID& outFormID)
	{
		outFormID = 0;
		if (!spell) {
			return false;
		}

#if defined(_MSC_VER)
		__try {
			outFormID = spell->GetFormID();
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
#else
		try {
			outFormID = spell->GetFormID();
		} catch (...) {
			return false;
		}
#endif

		return outFormID != 0 && RE::TESForm::LookupByID<RE::SpellItem>(outFormID) != nullptr;
	}

	struct SpellCollector : RE::Actor::ForEachSpellVisitor
	{
		explicit SpellCollector(std::unordered_set<RE::FormID>& out) :
			forms(out)
		{}

		RE::BSContainer::ForEachResult Visit(RE::SpellItem* a_spell) override
		{
			if (a_spell) {
				RE::FormID formId = 0;
				if (TryResolveVisitedSpellFormID(a_spell, formId)) {
					forms.insert(formId);
				}
			}
			return RE::BSContainer::ForEachResult::kContinue;
		}

		std::unordered_set<RE::FormID>& forms;
	};

	std::unordered_set<RE::FormID> CollectPlayerSpells(RE::PlayerCharacter* pc)
	{
		std::unordered_set<RE::FormID> result;
		if (!pc) {
			return result;
		}
		SpellCollector collector(result);
		pc->VisitSpells(collector);
		return result;
	}

	void CollectSpellDataSpells(RE::TESSpellList::SpellData* data, std::unordered_set<RE::FormID>& out)
	{
		if (!data || !data->spells) {
			return;
		}
		for (std::uint32_t i = 0; i < data->numSpells; ++i) {
			RE::SpellItem* spell = data->spells[i];
			if (spell) {
				out.insert(spell->GetFormID());
			}
		}
	}

	void CollectSpellDataShouts(RE::TESSpellList::SpellData* data, std::unordered_set<RE::FormID>& out)
	{
		if (!data || !data->shouts) {
			return;
		}
		for (std::uint32_t i = 0; i < data->numShouts; ++i) {
			RE::TESShout* shout = data->shouts[i];
			if (shout) {
				out.insert(shout->GetFormID());
			}
		}
	}

	void CollectLeveledSpellForms(RE::TESLevSpell* leveled, std::unordered_set<RE::FormID>& out, std::uint32_t depth = 0)
	{
		if (!leveled || depth > 4) {
			return;
		}
		const auto forms = leveled->GetContainedForms();
		for (auto* form : forms) {
			if (!form) {
				continue;
			}
			if (auto* spell = form->As<RE::SpellItem>()) {
				out.insert(spell->GetFormID());
				continue;
			}
			if (auto* nested = form->As<RE::TESLevSpell>()) {
				CollectLeveledSpellForms(nested, out, depth + 1);
			}
		}
	}

	void CollectSpellDataLevSpells(RE::TESSpellList::SpellData* data, std::unordered_set<RE::FormID>& out)
	{
		if (!data || !data->levSpells) {
			return;
		}
		for (std::uint32_t i = 0; i < data->numlevSpells; ++i) {
			RE::TESLevSpell* leveled = data->levSpells[i];
			if (!leveled) {
				continue;
			}
			CollectLeveledSpellForms(leveled, out);
		}
	}

	// Collect spells from the ActorBase (TESNPC) spell list - this includes base spells
	// that may not be in the runtime VisitSpells list, such as werewolf howls
	void CollectActorBaseSpells(RE::PlayerCharacter* pc, std::unordered_set<RE::FormID>& out)
	{
		if (!pc) {
			return;
		}
		auto* npc = pc->GetActorBase();
		if (!npc) {
			return;
		}
		auto* spellData = npc->GetSpellList();
		if (!spellData) {
			return;
		}
		// Collect direct spells
		CollectSpellDataSpells(spellData, out);
		// Collect leveled spells
		CollectSpellDataLevSpells(spellData, out);
	}

	// Collect all player spells: union of VisitSpells and ActorBase spell list
	std::unordered_set<RE::FormID> CollectPlayerAllSpells(RE::PlayerCharacter* pc,
		std::size_t* outVisitSpellsCount = nullptr,
		std::size_t* outActorBaseCount = nullptr)
	{
		std::unordered_set<RE::FormID> result;
		if (!pc) {
			if (outVisitSpellsCount) *outVisitSpellsCount = 0;
			if (outActorBaseCount) *outActorBaseCount = 0;
			return result;
		}
		// Collect via VisitSpells (runtime spells)
		SpellCollector collector(result);
		pc->VisitSpells(collector);
		const std::size_t visitSpellsCount = result.size();
		if (outVisitSpellsCount) {
			*outVisitSpellsCount = visitSpellsCount;
		}
		// Collect from ActorBase (TESNPC spell list)
		const std::size_t beforeActorBase = result.size();
		CollectActorBaseSpells(pc, result);
		const std::size_t afterActorBase = result.size();
		if (outActorBaseCount) {
			*outActorBaseCount = afterActorBase - beforeActorBase;
		}
		return result;
	}

	std::unordered_set<RE::FormID> CollectRaceSpells(RE::TESRace* race)
	{
		std::unordered_set<RE::FormID> result;
		if (!race) {
			return result;
		}
		CollectSpellDataSpells(race->actorEffects, result);
		CollectSpellDataLevSpells(race->actorEffects, result);
		return result;
	}

	std::unordered_set<RE::FormID> CollectRaceShouts(RE::TESRace* race)
	{
		std::unordered_set<RE::FormID> result;
		if (!race) {
			return result;
		}
		CollectSpellDataShouts(race->actorEffects, result);
		return result;
	}

	std::unordered_set<RE::FormID> CollectPlayerShouts(RE::PlayerCharacter* pc)
	{
		std::unordered_set<RE::FormID> result;
		if (!pc) {
			return result;
		}
		auto* npc = pc->GetActorBase();
		auto* spellData = npc ? npc->GetSpellList() : nullptr;
		if (!spellData || !spellData->shouts) {
			return result;
		}
		for (std::uint32_t i = 0; i < spellData->numShouts; ++i) {
			RE::TESShout* shout = spellData->shouts[i];
			if (shout) {
				result.insert(shout->GetFormID());
			}
		}
		return result;
	}

	void CollectEquippedSpellForms(RE::PlayerCharacter* pc, std::unordered_set<RE::FormID>& out)
	{
		if (!pc) {
			return;
		}
		if (auto* left = pc->GetEquippedObject(true)) {
			if (auto* spell = left->As<RE::SpellItem>()) {
				out.insert(spell->GetFormID());
			}
		}
		if (auto* right = pc->GetEquippedObject(false)) {
			if (auto* spell = right->As<RE::SpellItem>()) {
				out.insert(spell->GetFormID());
			}
		}
		if (auto* power = pc->GetActorRuntimeData().selectedPower) {
			out.insert(power->GetFormID());
		}
	}

	RE::TESRace* GetTransformRace(TransformState state, RE::PlayerCharacter* pc)
	{
		if (!pc) {
			return nullptr;
		}
		if (state == TransformState::Werewolf) {
			if (auto* race = GetWerewolfRace()) {
				return race;
			}
		} else if (state == TransformState::VampireLord) {
			if (auto* race = GetVampireLordRace()) {
				return race;
			}
		}
		return pc->GetRace();
	}

	std::unordered_set<RE::FormID> ComputeAddedForms(
		const std::unordered_set<RE::FormID>& base,
		const std::unordered_set<RE::FormID>& current)
	{
		std::unordered_set<RE::FormID> added;
		added.reserve(current.size());
		for (const auto formId : current) {
			if (base.find(formId) == base.end()) {
				added.insert(formId);
			}
		}
		return added;
	}

	struct SpellDeltaStats
	{
		std::size_t total = 0;
		std::size_t eligible = 0;
		std::size_t constantEffect = 0;
		std::size_t typeSpell = 0;
		std::size_t typePower = 0;
		std::size_t typeLesserPower = 0;
		std::size_t typeAbility = 0;
		std::size_t typeDisease = 0;
		std::size_t typePoison = 0;
		std::size_t typeVoicePower = 0;
		std::size_t typeOther = 0;
	};

	// State-aware eligibility: Both Werewolf and VampireLord allow Power/LesserPower/VoicePower
	// VampireLord additionally allows Spell type. Werewolf howls are VoicePower, so we must include them.
	bool IsActivatableTransformSpell(RE::SpellItem* spell, TransformState state)
	{
		if (!spell) {
			return false;
		}
		if (spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect) {
			return false;
		}
		const auto type = spell->GetSpellType();
		if (state == TransformState::Werewolf) {
			// Werewolf: Power, LesserPower, VoicePower, and Ability (Growl mod uses kAbility)
			switch (type) {
			case RE::MagicSystem::SpellType::kPower:
			case RE::MagicSystem::SpellType::kLesserPower:
			case RE::MagicSystem::SpellType::kVoicePower:
			case RE::MagicSystem::SpellType::kAbility:  // Growl werewolf mod compatibility
				return true;
			default:
				return false;
			}
		}
		if (state == TransformState::VampireLord) {
			// VampireLord: Spell/Power/LesserPower/VoicePower
			switch (type) {
			case RE::MagicSystem::SpellType::kSpell:
			case RE::MagicSystem::SpellType::kPower:
			case RE::MagicSystem::SpellType::kLesserPower:
			case RE::MagicSystem::SpellType::kVoicePower:
				return true;
			default:
				return false;
			}
		}
		if (state == TransformState::Lich) {
			// Lich: allow regular spells plus powers/lesser/voice and optional abilities.
			switch (type) {
			case RE::MagicSystem::SpellType::kSpell:
			case RE::MagicSystem::SpellType::kPower:
			case RE::MagicSystem::SpellType::kLesserPower:
			case RE::MagicSystem::SpellType::kVoicePower:
			case RE::MagicSystem::SpellType::kAbility:
				return true;
			default:
				return false;
			}
		}
		// Generic fallback: include Ability to support non-vanilla transform kits.
		switch (type) {
		case RE::MagicSystem::SpellType::kSpell:
		case RE::MagicSystem::SpellType::kPower:
		case RE::MagicSystem::SpellType::kLesserPower:
		case RE::MagicSystem::SpellType::kVoicePower:
		case RE::MagicSystem::SpellType::kAbility:
			return true;
		default:
			return false;
		}
	}

	std::vector<RE::FormID> BuildEligibleSpellForms(
		const std::unordered_set<RE::FormID>& spellForms,
		SpellDeltaStats& stats,
		TransformState state)
	{
		std::vector<RE::FormID> result;
		result.reserve(spellForms.size());

		for (const auto formId : spellForms) {
			RE::SpellItem* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
			if (!spell) {
				continue;
			}
			++stats.total;
			const auto type = spell->GetSpellType();
			switch (type) {
			case RE::MagicSystem::SpellType::kSpell:
				++stats.typeSpell;
				break;
			case RE::MagicSystem::SpellType::kPower:
				++stats.typePower;
				break;
			case RE::MagicSystem::SpellType::kLesserPower:
				++stats.typeLesserPower;
				break;
			case RE::MagicSystem::SpellType::kAbility:
				++stats.typeAbility;
				break;
			case RE::MagicSystem::SpellType::kDisease:
				++stats.typeDisease;
				break;
			case RE::MagicSystem::SpellType::kPoison:
				++stats.typePoison;
				break;
			case RE::MagicSystem::SpellType::kVoicePower:
				++stats.typeVoicePower;
				break;
			default:
				++stats.typeOther;
				break;
			}
			const bool transformExitSpell = IsTransformExitSpellForState(spell, state);
			if (!transformExitSpell &&
				spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect) {
				++stats.constantEffect;
				continue;
			}
			if (state == TransformState::VampireLord && IsVampireLordHiddenSpell(spell)) {
				if (s_vampireLordConfigCache.debugLog) {
					logger::info("TransformWheels: [VampireLordFilter] hid spell {:08X} edid='{}' name='{}' source=eligible",
						formId,
						spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
						spell->GetName() ? spell->GetName() : "");
				}
				continue;
			}
			if (!transformExitSpell && !IsActivatableTransformSpell(spell, state)) {
				continue;
			}
			if (state == TransformState::Werewolf && !IsWerewolfSafeWheelSpell(spell)) {
				continue;
			}
			if (state == TransformState::Lich) {
				RefreshLichConfigCache();
				if (s_lichConfigCache.blockBoundSpells) {
					const char* edidC = spell->GetFormEditorID();
					const char* nameC = spell->GetName();
					std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
					std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};
					if (ContainsIgnoreCase(edid, "Bound") || ContainsIgnoreCase(name, "Bound")) {
						continue;
					}
				}
			}
			result.push_back(formId);
		}

		std::sort(result.begin(), result.end());
		stats.eligible = result.size();
		return result;
	}

	std::unordered_set<RE::FormID> FilterVampireLordDeltaSpells(
		const std::unordered_set<RE::FormID>& deltaSpells,
		std::size_t& outDroppedNonKitSpellType)
	{
		std::unordered_set<RE::FormID> filtered;
		filtered.reserve(deltaSpells.size());
		outDroppedNonKitSpellType = 0;

		for (const auto formId : deltaSpells) {
			RE::SpellItem* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
			if (!spell) {
				continue;
			}
			const bool vampireLordExitSpell = IsVampireLordExitSpell(spell);
			if (!vampireLordExitSpell &&
				spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect) {
				continue;
			}
			if (!IsTrustedVampireLordWheelSpell(spell)) {
				++outDroppedNonKitSpellType;
				continue;
			}
			filtered.insert(formId);
		}

		return filtered;
	}

	std::vector<RE::FormID> LimitFormsWithPriority(
		const std::vector<RE::FormID>& forms,
		std::size_t maxEntries,
		const std::unordered_set<RE::FormID>& raceSpells,
		const std::unordered_set<RE::FormID>& equippedSpells,
		const std::unordered_set<RE::FormID>& kitSpells,
		const std::unordered_set<RE::FormID>& kitShouts,
		bool& outTrimmed)
	{
		outTrimmed = forms.size() > maxEntries;
		if (!outTrimmed || maxEntries == 0) {
			return forms;
		}

		std::vector<RE::FormID> limited;
		limited.reserve(maxEntries);
		std::unordered_set<RE::FormID> selected;
		selected.reserve(maxEntries * 2);

		auto addPriority = [&](const std::unordered_set<RE::FormID>& source) {
			if (limited.size() >= maxEntries || source.empty()) {
				return;
			}
			for (const auto formId : forms) {
				if (limited.size() >= maxEntries) {
					break;
				}
				if (source.find(formId) == source.end()) {
					continue;
				}
				if (selected.insert(formId).second) {
					limited.push_back(formId);
				}
			}
		};

		// Priority order: race -> equipped -> kit spells -> kit shouts.
		addPriority(raceSpells);
		addPriority(equippedSpells);
		addPriority(kitSpells);
		addPriority(kitShouts);

		if (limited.size() < maxEntries) {
			for (const auto formId : forms) {
				if (limited.size() >= maxEntries) {
					break;
				}
				if (selected.insert(formId).second) {
					limited.push_back(formId);
				}
			}
		}

		return limited;
	}

	std::uint64_t HashFormIDs(const std::vector<RE::FormID>& forms)
	{
		std::uint64_t hash = 1469598103934665603ull;
		for (const auto formId : forms) {
			hash ^= static_cast<std::uint64_t>(formId);
			hash *= 1099511628211ull;
		}
		return hash;
	}

	std::vector<RE::FormID> ApplyLichStableSpellOrder(const std::vector<RE::FormID>& forms)
	{
		if (forms.empty()) {
			s_lichStableSpellOrder.clear();
			return forms;
		}

		std::unordered_set<RE::FormID> remaining(forms.begin(), forms.end());
		std::vector<RE::FormID> ordered;
		ordered.reserve(forms.size());

		for (const auto formId : s_lichStableSpellOrder) {
			if (remaining.erase(formId) != 0) {
				ordered.push_back(formId);
			}
		}

		// The input list has already been sorted/filtered. Append newly discovered
		// lich spells in that deterministic order instead of letting currently
		// equipped spells reshuffle established slots after every cast.
		for (const auto formId : forms) {
			if (remaining.erase(formId) != 0) {
				ordered.push_back(formId);
			}
		}

		s_lichStableSpellOrder = ordered;
		return ordered;
	}

	FormWheelInfo& GetWheelInfo(TransformState state)
	{
		switch (state) {
		case TransformState::Werewolf:
			return s_werewolfWheel;
		case TransformState::VampireLord:
			return s_vampireWheel;
		case TransformState::Generic:
			return s_genericWheel;
		case TransformState::Lich:
			return s_lichWheel;
		case TransformState::Human:
		default:
			return s_genericWheel;
		}
	}

	int EnsureFormWheelPage(TransformState state, std::size_t pageIndex, std::size_t entryCount)
	{
		auto& info = GetWheelInfo(state);
		const std::string tag = MakeWheelId(state, pageIndex);
		if (pageIndex == 0 && info.index) {
			if (WheelIndexMatchesTag(*info.index, tag)) {
				return *info.index;
			}
			// Page-one pointer became stale; force a content refresh once we resolve/recreate it.
			info.index.reset();
			info.lastGeneratedAt = 0.0;
			info.lastHash = 0;
			info.hasHash = false;
			info.pageCount = 0;
		}

		if (auto existing = FindWheelIndexByTag(tag); existing.has_value()) {
			if (pageIndex == 0) {
				info.index = existing;
			}
			return *existing;
		}

		// Upgrade legacy single-tag wheels into page-one tags.
		if (pageIndex == 0) {
			if (auto legacy = FindWheelIndexByTag(GetWheelId(state)); legacy.has_value()) {
				TagWheelIndex(*legacy, tag);
				info.index = legacy;
				return *legacy;
			}
		}

		auto* api = GetWheelerAPI();
		if (!api || !api->IsInitialized()) {
			logger::warn("TransformWheels: API not initialized, cannot create form wheel");
			return -1;
		}

		WheelerAPI::WheelConfig config{};
		const std::size_t desiredEntries = (std::max<std::size_t>)(1, (std::min<std::size_t>)(entryCount, kTransformWheelPageSize));
		config.numEntries = static_cast<int32_t>(desiredEntries);
		config.position = -1;
		config.managed = true;
		config.clientName = tag.c_str();
		config.showLabel = false;

		const int created = api->CreateManagedWheel(&config);
		if (created < 0) {
			logger::warn("TransformWheels: failed to create form wheel (state={} page={} result={})",
				ToString(state), pageIndex + 1, created);
			return -1;
		}
		TagWheelIndex(created, tag);
		if (pageIndex == 0) {
			info.index = created;
		}
		if (Config::WheelBehavior::TransformWheels::DebugLog) {
			logger::info("TransformWheels: created {} wheel page={} index={} tag={}",
				ToString(state), pageIndex + 1, created, tag);
		}
		return created;
	}

	int EnsureFormWheel(TransformState state, std::size_t entryCount)
	{
		return EnsureFormWheelPage(state, 0, entryCount);
	}

	void PopulateTransformWheelSlice(
		int wheelIdx,
		const std::vector<RE::FormID>& forms,
		std::size_t startIndex,
		std::size_t count)
	{
		std::unique_lock<std::shared_mutex> lock(Wheeler::GetWheelDataLock());
		Wheel* wheel = Wheeler::GetWheelByIndex(wheelIdx);
		if (!wheel) {
			return;
		}
		wheel->Clear();
		const std::size_t endIndex = (std::min)(forms.size(), startIndex + count);
		for (std::size_t i = startIndex; i < endIndex; ++i) {
			const auto formId = forms[i];
			std::shared_ptr<WheelItem> item = WheelItemFactory::MakeWheelItemFromFormID(formId, 0);
			if (!item) {
				continue;
			}
			auto entry = std::make_unique<WheelEntry>();
			entry->PushItem(item);
			wheel->PushEntry(std::move(entry));
		}
		wheel->SetHoveredEntryIndex(-1);
		wheel->ResetAnimation();
	}

	void SwitchToWheel(int wheelIdx, TransformState state)
	{
		if (wheelIdx < 0) {
			return;
		}
		Wheeler::SetActiveWheelIndex(wheelIdx);
		if (Config::WheelBehavior::TransformWheels::DebugLog) {
			logger::info("TransformWheels: switched active wheel to {} (index={})", ToString(state), wheelIdx);
		}
	}

	void RestoreHumanWheel()
	{
		const int wheelCount = Wheeler::GetWheelCount();
		if (Config::WheelBehavior::TransformWheels::RestorePreviousWheel &&
			s_savedHumanWheelIdx && *s_savedHumanWheelIdx >= 0 && *s_savedHumanWheelIdx < wheelCount) {
			Wheeler::SetActiveWheelIndex(*s_savedHumanWheelIdx);
			if (Config::WheelBehavior::TransformWheels::DebugLog) {
				logger::info("TransformWheels: restored previous wheel index={}", *s_savedHumanWheelIdx);
			}
		} else if (wheelCount > 0) {
			Wheeler::SetActiveWheelIndex(0);
			if (Config::WheelBehavior::TransformWheels::DebugLog) {
				logger::info("TransformWheels: restored default wheel index=0");
			}
		}
		s_savedHumanWheelIdx.reset();
		s_switchedToTransform = false;
	}

	void SaveHumanWheel()
	{
		if (!Config::WheelBehavior::TransformWheels::RestorePreviousWheel) {
			return;
		}
		const int activeIdx = Wheeler::GetActiveWheelIndex();
		if (activeIdx < 0) {
			return;
		}
		if (WheelerAPI::IsManagedWheelIndex(activeIdx)) {
			return;
		}
		if (IsTransformWheelIndexInternal(activeIdx)) {
			return;
		}
		s_savedHumanWheelIdx = activeIdx;
	}

	void ResetWheelInfo(FormWheelInfo& info)
	{
		info.index.reset();
		info.lastGeneratedAt = 0.0;
		info.lastHash = 0;
		info.hasHash = false;
		info.pageCount = 0;
	}

	bool RemoveTransformWheel(TransformState state)
	{
		auto& info = GetWheelInfo(state);
		auto* api = GetWheelerAPI();
		if (!api || !api->IsInitialized()) {
			return false;
		}

		auto stateWheels = FindTaggedWheelsForState(state);
		if (stateWheels.empty()) {
			ResetWheelInfo(info);
			return false;
		}

		std::sort(stateWheels.begin(), stateWheels.end(), [](const auto& lhs, const auto& rhs) {
			return lhs.index > rhs.index;
		});

		bool removedAny = false;
		for (const auto& wheelInfo : stateWheels) {
			const auto result = WheelerAPI::DeleteWheelIndex(wheelInfo.index);
			if (result != WheelerAPI::Result::OK) {
				continue;
			}
			removedAny = true;
			if (Config::WheelBehavior::TransformWheels::DebugLog) {
				logger::info("TransformWheels: removed {} wheel index={} tag={}",
					ToString(state), wheelInfo.index, wheelInfo.tag);
			}
		}

		ResetWheelInfo(info);
		return removedAny;
	}

	void RemoveOtherTransformWheels(TransformState activeState)
	{
		if (activeState == TransformState::Werewolf) {
			RemoveTransformWheel(TransformState::VampireLord);
			RemoveTransformWheel(TransformState::Lich);
			RemoveTransformWheel(TransformState::Generic);
		} else if (activeState == TransformState::VampireLord) {
			RemoveTransformWheel(TransformState::Werewolf);
			RemoveTransformWheel(TransformState::Lich);
			RemoveTransformWheel(TransformState::Generic);
		} else if (activeState == TransformState::Lich) {
			RemoveTransformWheel(TransformState::Werewolf);
			RemoveTransformWheel(TransformState::VampireLord);
			RemoveTransformWheel(TransformState::Generic);
		} else if (activeState == TransformState::Generic) {
			RemoveTransformWheel(TransformState::Werewolf);
			RemoveTransformWheel(TransformState::VampireLord);
			RemoveTransformWheel(TransformState::Lich);
		}
	}

	void RemoveAllTransformWheels()
	{
		RemoveTransformWheel(TransformState::Werewolf);
		RemoveTransformWheel(TransformState::VampireLord);
		RemoveTransformWheel(TransformState::Lich);
		RemoveTransformWheel(TransformState::Generic);
	}

	void RemoveUnusedTransformPages(
		TransformState state,
		const std::unordered_set<std::string>& keepTags,
		int pageOneIndex)
	{
		auto* api = GetWheelerAPI();
		if (!api || !api->IsInitialized()) {
			return;
		}

		std::vector<TaggedWheelInfo> stale;
		for (const auto& wheelInfo : FindTaggedWheelsForState(state)) {
			if (keepTags.find(wheelInfo.tag) != keepTags.end()) {
				continue;
			}
			stale.push_back(wheelInfo);
		}
		if (stale.empty()) {
			return;
		}

		const int activeWheel = Wheeler::GetActiveWheelIndex();
		bool activeWillBeDeleted = false;
		for (const auto& staleWheel : stale) {
			if (staleWheel.index == activeWheel) {
				activeWillBeDeleted = true;
				break;
			}
		}
		if (activeWillBeDeleted && pageOneIndex >= 0 && activeWheel != pageOneIndex) {
			SwitchToWheel(pageOneIndex, state);
		}

		std::sort(stale.begin(), stale.end(), [](const auto& lhs, const auto& rhs) {
			return lhs.index > rhs.index;
		});
		for (const auto& staleWheel : stale) {
			const auto result = WheelerAPI::DeleteWheelIndex(staleWheel.index);
			if (result != WheelerAPI::Result::OK) {
				continue;
			}
			if (Config::WheelBehavior::TransformWheels::DebugLog) {
				logger::info("TransformWheels: removed stale {} page index={} tag={}",
					ToString(state), staleWheel.index, staleWheel.tag);
			}
		}
	}

	void StartPendingTransform(TransformState state, double now)
	{
		const bool debugLog = Config::WheelBehavior::TransformWheels::DebugLog;
		RemoveOtherTransformWheels(state);
		if (state == TransformState::VampireLord) {
			ClearVampireLordSessionSpells("new_vampire_lord_transform");
		} else {
			ClearVampireLordSessionSpells("other_transform_started");
		}
		s_pendingTransform.active = true;
		s_pendingTransform.state = state;
		s_pendingTransform.genericRaceContext = (state == TransformState::Generic) ? s_activeGenericRaceContext : 0;
		s_pendingTransform.startedAt = now;
		s_pendingTransform.nextPollAt = now;
		const double retryWindowSec = (std::max)(0.0, static_cast<double>(Config::WheelBehavior::TransformWheels::RetryWindowMs) / 1000.0);
		s_pendingTransform.expiresAt = now + retryWindowSec;
		s_pendingTransform.lastHash = 0;
		s_pendingTransform.hasLastHash = false;
		s_pendingTransform.stableTicks = 0;
		s_pendingTransform.switched = false;
		s_pendingTransform.lastEligible = 0;
		s_pendingTransform.latchedForms.clear();  // Clear latched set for new transform
		s_switchedToTransform = false;
		SaveHumanWheel();
		if (Wheeler::IsWheelerOpen()) {
			Wheeler::CloseWheeler();
		}
		if (debugLog) {
			logger::info("TransformWheels: pending start state={} contextRace={:08X} retryWindowMs={} pollMs={} minEntries={} stableTicks={}",
				ToString(state),
				s_pendingTransform.genericRaceContext,
				Config::WheelBehavior::TransformWheels::RetryWindowMs,
				Config::WheelBehavior::TransformWheels::RegenerateThrottleMs,
				Config::WheelBehavior::TransformWheels::MinEntries,
				Config::WheelBehavior::TransformWheels::StableTicks);
		}
	}

	bool TryPopulateTransformWheel(TransformState state, RE::PlayerCharacter* pc, double now)
	{
		if (!pc) {
			return false;
		}

		const bool isLichState = state == TransformState::Lich;
		RefreshLichConfigCache();
		RefreshVampireLordConfigCache();
		RefreshWerewolfConfigCache();
		LichPopulateMode statePopulateMode = LichPopulateMode::Delta;
		if (isLichState) {
			statePopulateMode = s_lichConfigCache.populateMode;
		} else if (state == TransformState::VampireLord && s_vampireLordConfigCache.enabled) {
			statePopulateMode = ParseLichPopulateMode(s_vampireLordConfigCache.populateMode);
		} else if (state == TransformState::Werewolf && s_werewolfConfigCache.enabled) {
			statePopulateMode = ParseLichPopulateMode(s_werewolfConfigCache.populateMode);
		}
		const bool manualPopulate = statePopulateMode == LichPopulateMode::Manual;
		const bool useDeltaPopulate = statePopulateMode == LichPopulateMode::Delta;
		const bool debugLog = Config::WheelBehavior::TransformWheels::DebugLog;
		const bool includeShouts = Config::WheelBehavior::TransformWheels::IncludeShouts;
		// Werewolf howls are TESShout forms. Vampire Lord entries are SpellItems
		// driven by DLC1VampireSpellsPowers, so VL does not opt into shout sources.
		const bool includeRaceShouts =
			includeShouts ||
			(state == TransformState::Werewolf);
		const bool cacheHuman = Config::WheelBehavior::TransformWheels::CacheHumanSnapshot;
		const bool hasBaseline = cacheHuman && !s_humanSpellSnapshot.empty();
		std::size_t vampireDroppedEquipped = 0;
		std::size_t vampireStickyAdded = 0;
		std::size_t vampireStickyPruned = 0;
		std::size_t vampireStickyReused = 0;
		std::size_t vampireFormListSpellCount = 0;

		if (manualPopulate) {
			int wheelIdx = EnsureFormWheel(state, 1);
			if (wheelIdx < 0) {
				return false;
			}
			auto& info = GetWheelInfo(state);
			info.index = wheelIdx;
			info.pageCount = 1;

			const std::uint64_t manualHash = 0x5452464Dull ^ static_cast<std::uint64_t>(state);  // "TRFM"
			if (s_pendingTransform.hasLastHash && s_pendingTransform.lastHash == manualHash) {
				s_pendingTransform.stableTicks += 1;
			} else {
				s_pendingTransform.stableTicks = 1;
			}
			s_pendingTransform.lastHash = manualHash;
			s_pendingTransform.hasLastHash = true;
			s_pendingTransform.lastEligible = 1;

			const float throttleMs = static_cast<float>(Config::WheelBehavior::TransformWheels::RegenerateThrottleMs);
			const double throttleSec = (std::max)(0.0f, throttleMs / 1000.0f);
			const std::uint32_t stableTarget = (std::max)(1u, Config::WheelBehavior::TransformWheels::StableTicks);
			const bool stableEnough = s_pendingTransform.stableTicks >= stableTarget;
			const bool nearExpiry = s_pendingTransform.expiresAt > 0.0 && (s_pendingTransform.expiresAt - now) <= throttleSec;
			if (!s_pendingTransform.switched && (stableEnough || nearExpiry)) {
				SwitchToWheel(wheelIdx, state);
				s_pendingTransform.switched = true;
				s_switchedToTransform = true;
				if (debugLog || s_lichConfigCache.debugLog || s_vampireLordConfigCache.debugLog || s_werewolfConfigCache.debugLog) {
					logger::info("TransformWheels: [{}] manual populate mode -> switched to wheel index={}", ToString(state), wheelIdx);
				}
			}
			return true;
		}

		// Always collect current spells (needed for both baseline delta and kit-sniff fallback)
		std::size_t visitSpellsCount = 0;
		std::size_t actorBaseCount = 0;
		const auto currentAllSpells = CollectPlayerAllSpells(pc, &visitSpellsCount, &actorBaseCount);
		const RE::FormID selectedPowerFormId =
			(pc->GetActorRuntimeData().selectedPower != nullptr) ?
				pc->GetActorRuntimeData().selectedPower->GetFormID() :
				0;

		// Compute delta from baseline with a contamination guard for PSB-sized deltas.
		std::unordered_set<RE::FormID> actorDeltaSpells;
		std::unordered_set<RE::FormID> actorDeltaFilteredSpells;
		std::size_t actorDeltaCount = 0;
		std::size_t actorDeltaFilteredCount = 0;
		std::size_t vampireDroppedNonKitSpellType = 0;
		std::size_t werewolfDroppedUnsafeDelta = 0;
		bool deltaFloodDetected = false;
		if (hasBaseline && useDeltaPopulate) {
			actorDeltaSpells = ComputeAddedForms(s_humanSpellSnapshot, currentAllSpells);
			actorDeltaCount = actorDeltaSpells.size();
			deltaFloodDetected = actorDeltaCount > kMaxDeltaSpellsAllowed;
			if (deltaFloodDetected) {
				if (s_pendingTransform.active && s_pendingTransform.state == state) {
					s_pendingTransform.latchedForms.clear();
				}
				static double s_lastWerewolfFloodLogAt = -1000.0;
				static double s_lastVampireFloodLogAt = -1000.0;
				double& lastLogAt = (state == TransformState::Werewolf) ? s_lastWerewolfFloodLogAt : s_lastVampireFloodLogAt;
				if (debugLog && (now - lastLogAt) >= 1.0) {
					logger::info(
						"TransformWheels: [{}] delta flood detected (delta={} baseline={} total={}), ignoring baseline delta",
						ToString(state),
						actorDeltaCount,
						s_humanSpellSnapshot.size(),
						currentAllSpells.size());
					lastLogAt = now;
				}
			} else {
				actorDeltaFilteredSpells = actorDeltaSpells;
				if (state == TransformState::VampireLord) {
					actorDeltaFilteredSpells = FilterVampireLordDeltaSpells(actorDeltaSpells, vampireDroppedNonKitSpellType);
				} else if (state == TransformState::Werewolf) {
					werewolfDroppedUnsafeDelta = FilterWerewolfUnsafeSpellSet(actorDeltaFilteredSpells);
				}
			}
			actorDeltaFilteredCount = actorDeltaFilteredSpells.size();
		}

		// Kit-sniff: scan for spells that look like transform abilities by EditorID/Name
		std::unordered_set<RE::FormID> kitSpells;
		CollectTransformKitSpells(currentAllSpells, state, kitSpells);
		std::size_t werewolfDroppedUnsafeKit = 0;
		if (state == TransformState::Werewolf) {
			werewolfDroppedUnsafeKit = FilterWerewolfUnsafeSpellSet(kitSpells);
		}

		// Equipped spells
		std::unordered_set<RE::FormID> equippedSpells;
		CollectEquippedSpellForms(pc, equippedSpells);
		const std::size_t equippedSpellCount = equippedSpells.size();

		// Race spells
		std::unordered_set<RE::FormID> raceSpells;
		if (RE::TESRace* race = GetTransformRace(state, pc)) {
			if (race->actorEffects) {
				CollectSpellDataSpells(race->actorEffects, raceSpells);
				std::unordered_set<RE::FormID> leveledSpells;
				CollectSpellDataLevSpells(race->actorEffects, leveledSpells);
				raceSpells.insert(leveledSpells.begin(), leveledSpells.end());
			}
		}
		std::size_t werewolfDroppedUnsafeRace = 0;
		if (state == TransformState::Werewolf) {
			werewolfDroppedUnsafeRace = FilterWerewolfUnsafeSpellSet(raceSpells);
		}
		const std::size_t raceSpellCount = raceSpells.size();

		if (state == TransformState::VampireLord) {
			vampireFormListSpellCount = CollectVampireLordFormListSpells(
				pc,
				currentAllSpells,
				equippedSpells,
				raceSpells,
				kitSpells);
		}

		std::size_t configuredVampireLordSpellCount = 0;
		if (state == TransformState::VampireLord) {
			configuredVampireLordSpellCount = CollectConfiguredVampireLordSpells(
				pc,
				currentAllSpells,
				equippedSpells,
				raceSpells,
				kitSpells);
		}
		std::size_t configuredWerewolfSpellCount = 0;
		if (state == TransformState::Werewolf) {
			configuredWerewolfSpellCount = CollectConfiguredWerewolfSpells(
				pc,
				currentAllSpells,
				equippedSpells,
				raceSpells,
				kitSpells);
		}
		std::size_t configuredLichSpellCount = 0;
		if (isLichState) {
			configuredLichSpellCount = CollectConfiguredLichSpells(
				pc,
				currentAllSpells,
				equippedSpells,
				raceSpells,
				kitSpells);
		}
		const std::size_t kitSpellCount = kitSpells.size();

		// Generic hardening: keep equipped forms only if they are likely
		// transform-owned to avoid leaking unrelated human spells.
		std::unordered_set<RE::FormID> equippedTransformSpells = equippedSpells;
		if (state == TransformState::Werewolf) {
			equippedTransformSpells = FilterWerewolfEquippedSpells(
				equippedSpells,
				actorDeltaFilteredSpells,
				raceSpells,
				kitSpells,
				selectedPowerFormId,
				hasBaseline,
				s_humanEquippedSpellSnapshot);
		} else if (state == TransformState::VampireLord) {
			equippedTransformSpells.clear();
			for (const auto formId : equippedSpells) {
				auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
				const bool allowRaceSpell = raceSpells.find(formId) != raceSpells.end() &&
					spell &&
					!IsVampireLordHiddenSpell(spell) &&
					IsActivatableTransformSpell(spell, TransformState::VampireLord);
				if (allowRaceSpell || IsTrustedVampireLordWheelSpell(spell)) {
					equippedTransformSpells.insert(formId);
					continue;
				}

				++vampireDroppedEquipped;
				if (s_vampireLordConfigCache.debugLog &&
					spell &&
					s_vampireLordLoggedRejectedEquippedForms.insert(formId).second) {
					logger::info(
						"TransformWheels: [VampireLordEquipped] ignored equipped non-kit spell {:08X} edid='{}' name='{}'",
						formId,
						spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
						spell->GetName() ? spell->GetName() : "");
				}
			}
		} else if (state == TransformState::Generic) {
			equippedTransformSpells.clear();
			const bool hasHumanEquippedBaseline = !s_humanEquippedSpellSnapshot.empty();
			bool selectedPowerLooksTransform = false;
			if (selectedPowerFormId != 0) {
				if (actorDeltaFilteredSpells.find(selectedPowerFormId) != actorDeltaFilteredSpells.end() ||
					raceSpells.find(selectedPowerFormId) != raceSpells.end()) {
					selectedPowerLooksTransform = true;
				} else if (auto* selectedPowerSpell = RE::TESForm::LookupByID<RE::SpellItem>(selectedPowerFormId)) {
					// For selected power, allow generic token matching so transform-family
					// powers (NW_/GRIM/etc.) can still signal a valid generic context.
					selectedPowerLooksTransform = IsGenericTransformHintSpell(selectedPowerSpell, true);
				}
			}
			for (const auto formId : equippedSpells) {
				if (actorDeltaFilteredSpells.find(formId) != actorDeltaFilteredSpells.end() ||
					raceSpells.find(formId) != raceSpells.end()) {
					equippedTransformSpells.insert(formId);
					continue;
				}
				auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
				// Generic token-based hinting is restricted to selected power only.
				// This prevents left/right human polymorph spells from leaking.
				const bool allowGenericTokens = (formId == selectedPowerFormId);
				if (IsGenericTransformHintSpell(spell, allowGenericTokens)) {
					equippedTransformSpells.insert(formId);
					continue;
				}

				// Fallback for mods that add/equip form spells without race entries:
				// when baseline is unavailable, include hand-equipped activatable spells
				// only if selected power clearly indicates an active transform context.
				if (!hasBaseline && selectedPowerLooksTransform &&
					IsActivatableTransformSpell(spell, state)) {
					if (!hasHumanEquippedBaseline ||
						s_humanEquippedSpellSnapshot.find(formId) == s_humanEquippedSpellSnapshot.end()) {
						equippedTransformSpells.insert(formId);
					}
				}
			}
		} else if (state == TransformState::Lich) {
			equippedTransformSpells.clear();
			const bool hasHumanEquippedBaseline = !s_humanEquippedSpellSnapshot.empty();
			for (const auto formId : equippedSpells) {
				if (actorDeltaFilteredSpells.find(formId) != actorDeltaFilteredSpells.end() ||
					raceSpells.find(formId) != raceSpells.end() ||
					kitSpells.find(formId) != kitSpells.end()) {
					equippedTransformSpells.insert(formId);
					continue;
				}

				auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
				if (IsTransformKitSpell(spell, TransformState::Lich) || IsLichExitSpell(spell)) {
					equippedTransformSpells.insert(formId);
					continue;
				}

				if (!hasBaseline &&
					IsActivatableTransformSpell(spell, state) &&
					(!hasHumanEquippedBaseline ||
						s_humanEquippedSpellSnapshot.find(formId) == s_humanEquippedSpellSnapshot.end())) {
					equippedTransformSpells.insert(formId);
				}
			}
		}
		const std::size_t equippedTransformSpellCount = equippedTransformSpells.size();

		// Build candidates = filteredDelta + kit + equipped + race
		std::unordered_set<RE::FormID> candidateSpells;
		if (useDeltaPopulate) {
			candidateSpells = actorDeltaFilteredSpells;
		}
		candidateSpells.insert(kitSpells.begin(), kitSpells.end());
		candidateSpells.insert(equippedTransformSpells.begin(), equippedTransformSpells.end());
		candidateSpells.insert(raceSpells.begin(), raceSpells.end());
		if (state == TransformState::VampireLord) {
			NormalizeVampireLordRaiseDeadFamily(candidateSpells, pc, currentAllSpells);
			// Sacrosanct and similar overhauls can expose VL perk spells only while they
			// are currently equipped. Once a spell is positively recognized as VL kit,
			// keep it for this Vampire Lord session without adding/removing player spells.
			vampireStickyAdded += AddVampireLordSessionSpells(kitSpells, "kit");
			vampireStickyAdded += AddVampireLordSessionSpells(equippedTransformSpells, "equipped");
			vampireStickyAdded += AddVampireLordSessionSpells(actorDeltaFilteredSpells, "delta");
			vampireStickyPruned = PruneVampireLordSessionSpells();
			for (const auto formId : s_vampireLordSessionSpellForms) {
				if (candidateSpells.insert(formId).second) {
					++vampireStickyReused;
				}
			}
		}
		LogTransformCandidateDiagnostics(
			state,
			pc,
			currentAllSpells,
			equippedSpells,
			raceSpells,
			kitSpells,
			candidateSpells);

		if (debugLog && !hasBaseline) {
			logger::info("TransformWheels: [{}] baseline missing, using kit-sniff fallback (kit={} race={} equipped={})",
				ToString(state), kitSpellCount, raceSpellCount, equippedTransformSpellCount);
		} else if (debugLog) {
			logger::info(
				"TransformWheels: [{}] spellSources visitSpells={} actorBase={} total={} deltaRaw={} deltaUsed={} flood={} vampDroppedSpellType={} equippedRaw={} equippedUsed={}",
				ToString(state),
				visitSpellsCount,
				actorBaseCount,
				currentAllSpells.size(),
				actorDeltaCount,
				actorDeltaFilteredCount,
				deltaFloodDetected,
				vampireDroppedNonKitSpellType,
				equippedSpellCount,
				equippedTransformSpellCount);
		}

		// Debug: Log all delta spells that are Powers or LesserPowers (werewolf abilities are often these)
		static bool s_loggedDeltaSpells = false;
		if (debugLog && state == TransformState::Werewolf && hasBaseline && !s_loggedDeltaSpells && !actorDeltaFilteredSpells.empty()) {
			s_loggedDeltaSpells = true;
			logger::info("TransformWheels: [DEBUG] Delta spells (filtered) count={}:", actorDeltaFilteredSpells.size());
			for (auto id : actorDeltaFilteredSpells) {
				auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(id);
				if (!spell) continue;
				const char* edidC = spell->GetFormEditorID();
				const char* nameC = spell->GetName();
				std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
				std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};
				auto castType = spell->GetCastingType();
				auto spellType = spell->GetSpellType();
				const char* castStr = (castType == RE::MagicSystem::CastingType::kConstantEffect) ? "Constant" :
					(castType == RE::MagicSystem::CastingType::kFireAndForget) ? "FireForget" :
					(castType == RE::MagicSystem::CastingType::kConcentration) ? "Conc" : "Other";
				const char* typeStr = (spellType == RE::MagicSystem::SpellType::kPower) ? "Power" :
					(spellType == RE::MagicSystem::SpellType::kLesserPower) ? "LesserPower" :
					(spellType == RE::MagicSystem::SpellType::kSpell) ? "Spell" :
					(spellType == RE::MagicSystem::SpellType::kVoicePower) ? "VoicePower" : "Other";
				bool isKit = IsTransformKitSpell(spell, state);
				logger::info("TransformWheels: [DEBUG] Delta {:08X} edid='{}' name='{}' cast={} type={} isKit={}",
					id, edid, name, castStr, typeStr, isKit);
			}
		}

		std::unordered_set<RE::FormID> currentShouts;
		std::unordered_set<RE::FormID> candidateShouts;
		std::unordered_set<RE::FormID> raceShouts;
		std::size_t raceShoutCount = 0;
		std::size_t actorDeltaShoutCount = 0;
		if (includeShouts || includeRaceShouts) {
			if (includeShouts) {
				currentShouts = CollectPlayerShouts(pc);
				const bool hasShoutBaseline = cacheHuman && !s_humanShoutSnapshot.empty();
				candidateShouts = hasShoutBaseline ? ComputeAddedForms(s_humanShoutSnapshot, currentShouts) : currentShouts;
				if (state == TransformState::Werewolf) {
					candidateShouts = FilterWerewolfShoutForms(
						candidateShouts,
						raceShouts,
						selectedPowerFormId,
						hasShoutBaseline,
						s_humanShoutSnapshot);
				}
				actorDeltaShoutCount = candidateShouts.size();
			}
			if (includeRaceShouts) {
				if (RE::TESRace* race = GetTransformRace(state, pc)) {
					if (race->actorEffects) {
						CollectSpellDataShouts(race->actorEffects, raceShouts);
						raceShoutCount = raceShouts.size();
						candidateShouts.insert(raceShouts.begin(), raceShouts.end());
					}
				}
			}
		}
		// NEW: Kit shouts - scan all game shouts the player knows that match transform patterns
		std::unordered_set<RE::FormID> kitShouts;
		CollectTransformKitShouts(state, kitShouts);
		const std::size_t kitShoutCount = kitShouts.size();

		SpellDeltaStats deltaStats;
		BuildEligibleSpellForms(actorDeltaFilteredSpells, deltaStats, state);
		SpellDeltaStats candidateStats;
		std::vector<RE::FormID> forms = BuildEligibleSpellForms(candidateSpells, candidateStats, state);

		// Add kit shouts to forms
		for (auto id : kitShouts) {
			forms.push_back(id);
		}

		// Add race/delta shouts if enabled
		if (includeShouts || includeRaceShouts) {
			for (const auto formId : candidateShouts) {
				RE::TESShout* shout = RE::TESForm::LookupByID<RE::TESShout>(formId);
				if (shout) {
					if (state == TransformState::VampireLord && IsVampireLordHiddenShout(shout)) {
						if (s_vampireLordConfigCache.debugLog) {
							logger::info("TransformWheels: [VampireLordFilter] hid shout {:08X} edid='{}' name='{}' source=candidateShout",
								shout->GetFormID(),
								shout->GetFormEditorID() ? shout->GetFormEditorID() : "",
								shout->GetName() ? shout->GetName() : "");
						}
						continue;
					}
					if (state == TransformState::VampireLord) {
						continue;
					}
					forms.push_back(formId);
				}
			}
		}
		if (!forms.empty()) {
			std::sort(forms.begin(), forms.end());
			forms.erase(std::unique(forms.begin(), forms.end()), forms.end());
		}
		std::size_t werewolfDroppedUnsafeFinal = 0;
		if (state == TransformState::Werewolf) {
			const auto beforeFilter = forms.size();
			forms.erase(std::remove_if(forms.begin(), forms.end(), [](RE::FormID formId) {
				auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
				return spell != nullptr && !IsWerewolfSafeWheelSpell(spell);
			}), forms.end());
			werewolfDroppedUnsafeFinal = beforeFilter - forms.size();
		}
		std::size_t majorTransformDroppedFinal = 0;
		auto filterMajorTransformEntries = [&](const char* source) {
			const auto beforeFilter = forms.size();
			forms.erase(std::remove_if(forms.begin(), forms.end(), [&](RE::FormID formId) {
				auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
				return spell != nullptr && ShouldHideMajorTransformSpellForState(spell, state, source);
			}), forms.end());
			majorTransformDroppedFinal += beforeFilter - forms.size();
		};
		filterMajorTransformEntries("final");

		std::size_t vampireLordModeHiddenFinal = 0;
		auto filterVampireLordModeHiddenEntries = [&](const char* source) {
			if (state != TransformState::VampireLord) {
				return;
			}
			const auto beforeFilter = forms.size();
			forms.erase(std::remove_if(forms.begin(), forms.end(), [&](RE::FormID formId) {
				auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
				return spell != nullptr && ShouldHideVampireLordSpellForCombatMode(spell, pc, source);
			}), forms.end());
			vampireLordModeHiddenFinal += beforeFilter - forms.size();
		};
		filterVampireLordModeHiddenEntries("final");

		const std::unordered_set<RE::FormID> emptyEquippedPriority;
		const auto& equippedPrioritySpells =
			(state == TransformState::Lich) ? emptyEquippedPriority : equippedTransformSpells;
		std::unordered_set<RE::FormID> firstPrioritySpells = raceSpells;
		if (state == TransformState::VampireLord || state == TransformState::Lich) {
			for (const auto formId : forms) {
				auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId);
				if (IsTransformExitSpellForState(spell, state)) {
					firstPrioritySpells.insert(formId);
				}
			}
		}

		bool trimmedBySafetyCap = false;
		forms = LimitFormsWithPriority(
			forms,
			kMaxTransformEntriesTotal,
			firstPrioritySpells,
			equippedPrioritySpells,
			kitSpells,
			kitShouts,
			trimmedBySafetyCap);

		// Latch: during pending, forms can only grow (monotonic accumulation).
		// For Generic transform contexts, avoid monotonic latching to prevent
		// cross-form contamination when scripts swap abilities between races.
		const bool allowMonotonicLatch = (state != TransformState::Generic);
		if (allowMonotonicLatch && s_pendingTransform.active && s_pendingTransform.state == state) {
			for (auto id : forms) {
				s_pendingTransform.latchedForms.insert(id);
			}
			if (!s_pendingTransform.latchedForms.empty()) {
				forms.assign(s_pendingTransform.latchedForms.begin(), s_pendingTransform.latchedForms.end());
				std::sort(forms.begin(), forms.end());
				forms.erase(std::unique(forms.begin(), forms.end()), forms.end());
				bool trimmedLatchedBySafetyCap = false;
				forms = LimitFormsWithPriority(
					forms,
					kMaxTransformEntriesTotal,
					firstPrioritySpells,
					equippedPrioritySpells,
					kitSpells,
					kitShouts,
					trimmedLatchedBySafetyCap);
				if (trimmedLatchedBySafetyCap || s_pendingTransform.latchedForms.size() != forms.size()) {
					s_pendingTransform.latchedForms.clear();
					s_pendingTransform.latchedForms.insert(forms.begin(), forms.end());
					trimmedBySafetyCap = trimmedBySafetyCap || trimmedLatchedBySafetyCap;
				}
			}
		}
		if (state == TransformState::VampireLord) {
			NormalizeVampireLordRaiseDeadForms(forms, s_vampireLordRaiseDeadRepresentative);
		}
		filterMajorTransformEntries("latched");
		filterVampireLordModeHiddenEntries("latched");
		if (state == TransformState::VampireLord) {
			const auto beforeFilter = forms.size();
			forms.erase(std::remove_if(forms.begin(), forms.end(), [](RE::FormID formId) {
				if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formId)) {
					return IsVampireLordHiddenSpell(spell);
				}
				if (auto* shout = RE::TESForm::LookupByID<RE::TESShout>(formId)) {
					return IsVampireLordHiddenShout(shout);
				}
				return false;
			}), forms.end());
			if (beforeFilter != forms.size() && s_vampireLordConfigCache.debugLog) {
				logger::info(
					"TransformWheels: [VampireLordFilter] removed hidden forms at final pass before={} after={}",
					beforeFilter,
					forms.size());
			}
		}

		if (state == TransformState::Lich) {
			forms = ApplyLichStableSpellOrder(forms);
		}

		const std::size_t eligibleTotal = forms.size();
		const std::size_t pageCount = (eligibleTotal + kTransformWheelPageSize - 1) / kTransformWheelPageSize;

		std::uint64_t hash = HashFormIDs(forms);
		if (state == TransformState::Generic) {
			hash ^= static_cast<std::uint64_t>(s_activeGenericRaceContext);
			hash *= 1099511628211ull;
		}
		if (s_pendingTransform.hasLastHash && hash == s_pendingTransform.lastHash) {
			s_pendingTransform.stableTicks += 1;
		} else {
			s_pendingTransform.stableTicks = 1;
		}
		s_pendingTransform.lastHash = hash;
		s_pendingTransform.hasLastHash = true;
		s_pendingTransform.lastEligible = eligibleTotal;

		if (debugLog) {
			if (includeShouts || includeRaceShouts) {
				logger::info(
					"TransformWheels: [{}] baseline={} deltaRaw={} deltaUsed={} kitSpells={} kitShouts={} race={} equipped={} eligible={} pages={} latched={} stableTicks={} trimmed={} shouts{{delta={} race={}}}",
					ToString(state),
					hasBaseline ? s_humanSpellSnapshot.size() : 0,
					actorDeltaCount,
					actorDeltaFilteredCount,
					kitSpellCount,
					kitShoutCount,
					raceSpellCount,
					equippedTransformSpellCount,
					eligibleTotal,
					pageCount,
					s_pendingTransform.latchedForms.size(),
					s_pendingTransform.stableTicks,
					trimmedBySafetyCap,
					actorDeltaShoutCount,
					raceShoutCount);
			} else {
				logger::info(
					"TransformWheels: [{}] baseline={} deltaRaw={} deltaUsed={} kitSpells={} kitShouts={} race={} equipped={} eligible={} pages={} latched={} stableTicks={} trimmed={}",
					ToString(state),
					hasBaseline ? s_humanSpellSnapshot.size() : 0,
					actorDeltaCount,
					actorDeltaFilteredCount,
					kitSpellCount,
					kitShoutCount,
					raceSpellCount,
					equippedTransformSpellCount,
					eligibleTotal,
					pageCount,
					s_pendingTransform.latchedForms.size(),
					s_pendingTransform.stableTicks,
					trimmedBySafetyCap);
			}
			if (state == TransformState::Werewolf &&
				(werewolfDroppedUnsafeDelta != 0 || werewolfDroppedUnsafeKit != 0 ||
					werewolfDroppedUnsafeRace != 0 || werewolfDroppedUnsafeFinal != 0)) {
				logger::info(
					"TransformWheels: [Werewolf] filtered unsafe forms delta={} kit={} race={} final={}",
					werewolfDroppedUnsafeDelta,
					werewolfDroppedUnsafeKit,
					werewolfDroppedUnsafeRace,
					werewolfDroppedUnsafeFinal);
			}
			if (state == TransformState::Werewolf && configuredWerewolfSpellCount != 0) {
				logger::info("TransformWheels: [WerewolfPopulate] configured spells added={}", configuredWerewolfSpellCount);
			}
			if (state == TransformState::VampireLord && configuredVampireLordSpellCount != 0) {
				logger::info("TransformWheels: [VampireLordPopulate] configured spells added={}", configuredVampireLordSpellCount);
			}
			if (state == TransformState::VampireLord && vampireFormListSpellCount != 0) {
				logger::info("TransformWheels: [VampireLordFormList] spells added={}", vampireFormListSpellCount);
			}
			if (state == TransformState::VampireLord &&
				(vampireStickyAdded != 0 || vampireStickyPruned != 0 ||
					vampireStickyReused != 0 || vampireDroppedEquipped != 0)) {
				logger::info(
					"TransformWheels: [VampireLordSticky] session={} new={} reused={} pruned={} equippedRejected={}",
					s_vampireLordSessionSpellForms.size(),
					vampireStickyAdded,
					vampireStickyReused,
					vampireStickyPruned,
					vampireDroppedEquipped);
			}
			if (majorTransformDroppedFinal != 0) {
				logger::info(
					"TransformWheels: [{}] filtered major transform entries count={}",
					ToString(state),
					majorTransformDroppedFinal);
			}
			if (state == TransformState::VampireLord && vampireLordModeHiddenFinal != 0) {
				logger::info(
					"TransformWheels: [VampireLordModeFilter] filtered regular spells hidden={} mode={} rawMode={}",
					vampireLordModeHiddenFinal,
					ToString(GetEffectiveVampireLordCombatMode(pc)),
					ToString(GetVampireLordCombatMode(pc)));
			}
			if (state == TransformState::Lich && configuredLichSpellCount != 0) {
				logger::info("TransformWheels: [LichPopulate] configured spells added={}", configuredLichSpellCount);
			}
		}

		const std::uint32_t minEntries = (std::max)(1u, Config::WheelBehavior::TransformWheels::MinEntries);
		if (eligibleTotal < minEntries) {
			if (debugLog) {
				logger::info("TransformWheels: eligible={} below minEntries={} (no switch)", eligibleTotal, minEntries);
			}
			return false;
		}

		const std::size_t pageOneEntries = (std::min<std::size_t>)(kTransformWheelPageSize, forms.size());
		int wheelIdx = EnsureFormWheel(state, pageOneEntries);
		if (wheelIdx < 0) {
			return false;
		}
		const std::string pageOneTag = MakeWheelId(state, 0);
		if (auto pageOne = FindWheelIndexByTag(pageOneTag); pageOne.has_value()) {
			wheelIdx = *pageOne;
		}

		auto& info = GetWheelInfo(state);
		const bool updateOnlyOnChange = Config::WheelBehavior::TransformWheels::UpdateOnlyOnChange;
		const bool contentChanged = !updateOnlyOnChange || !info.hasHash || info.lastHash != hash;
		const float throttleMs = static_cast<float>(Config::WheelBehavior::TransformWheels::RegenerateThrottleMs);
		const double throttleSec = (std::max)(0.0f, throttleMs / 1000.0f);
		const bool throttleReady = info.lastGeneratedAt <= 0.0 || (now - info.lastGeneratedAt) >= throttleSec;

		if (contentChanged) {
			if (!throttleReady) {
				return false;
			}
			std::unordered_set<std::string> keepTags;
			keepTags.reserve(pageCount);
			for (std::size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
				const std::size_t start = pageIndex * kTransformWheelPageSize;
				const std::size_t remaining = forms.size() - start;
				const std::size_t count = (std::min)(kTransformWheelPageSize, remaining);
				const int pageWheelIdx = EnsureFormWheelPage(state, pageIndex, count);
				if (pageWheelIdx < 0) {
					return false;
				}
				const std::string pageTag = MakeWheelId(state, pageIndex);
				keepTags.insert(pageTag);
				PopulateTransformWheelSlice(pageWheelIdx, forms, start, count);
			}

			if (auto refreshedPageOne = FindWheelIndexByTag(pageOneTag); refreshedPageOne.has_value()) {
				wheelIdx = *refreshedPageOne;
				info.index = refreshedPageOne;
			}

			RemoveUnusedTransformPages(state, keepTags, wheelIdx);
			if (auto refreshedPageOne = FindWheelIndexByTag(pageOneTag); refreshedPageOne.has_value()) {
				wheelIdx = *refreshedPageOne;
				info.index = refreshedPageOne;
			}

			info.lastGeneratedAt = now;
			info.lastHash = hash;
			info.hasHash = true;
			info.pageCount = pageCount;
			if (debugLog) {
				logger::info("TransformWheels: populated {} wheel pages={} entries={} pageSize={}",
					ToString(state), pageCount, forms.size(), kTransformWheelPageSize);
			}
		} else {
			info.pageCount = pageCount;
		}

		const std::uint32_t stableTarget = (std::max)(1u, Config::WheelBehavior::TransformWheels::StableTicks);
		const bool stableEnough = s_pendingTransform.stableTicks >= stableTarget;
		const bool nearExpiry = s_pendingTransform.expiresAt > 0.0 && (s_pendingTransform.expiresAt - now) <= throttleSec;

		if (!s_pendingTransform.switched && (stableEnough || nearExpiry)) {
			SwitchToWheel(wheelIdx, state);
			s_pendingTransform.switched = true;
			s_switchedToTransform = true;
		}
		return true;
	}

	void ProcessPendingTransform(RE::PlayerCharacter* pc, TransformState currentState, double now)
	{
		if (!s_pendingTransform.active) {
			return;
		}
		if (currentState != s_pendingTransform.state) {
			s_pendingTransform.active = false;
			return;
		}
		if (currentState == TransformState::Generic &&
			s_pendingTransform.genericRaceContext != 0 &&
			s_pendingTransform.genericRaceContext != s_activeGenericRaceContext) {
			s_pendingTransform.active = false;
			return;
		}
		if (now >= s_pendingTransform.expiresAt) {
			const std::uint32_t minEntries = (std::max)(1u, Config::WheelBehavior::TransformWheels::MinEntries);
			if (s_pendingTransform.lastEligible < minEntries) {
				RemoveTransformWheel(s_pendingTransform.state);
			}
			if (Config::WheelBehavior::TransformWheels::DebugLog) {
				logger::info("TransformWheels: retry window expired; keeping current wheel");
			}
			s_pendingTransform.active = false;
			return;
		}
		if (now < s_pendingTransform.nextPollAt) {
			return;
		}

		const float throttleMs = static_cast<float>(Config::WheelBehavior::TransformWheels::RegenerateThrottleMs);
		const double throttleSec = (std::max)(0.0f, throttleMs / 1000.0f);
		s_pendingTransform.nextPollAt = now + throttleSec;

		TryPopulateTransformWheel(s_pendingTransform.state, pc, now);

		// Keep polling until the retry window expires so late-added abilities are captured.
	}

	bool ShouldRestrictToTransformWheelsForState(TransformState state)
	{
		switch (state) {
		case TransformState::Werewolf:
			return !Config::WheelBehavior::TransformWheels::WerewolfAllowBaseWheel;
		case TransformState::VampireLord:
			return !Config::WheelBehavior::TransformWheels::VampireLordAllowBaseWheel;
		case TransformState::Generic:
			return true;
		case TransformState::Lich:
			RefreshLichConfigCache();
			if (!s_lichConfigCache.enabled || s_lichConfigCache.mode == LichMode::Disabled) {
				return true;
			}
			if (s_lichConfigCache.mode == LichMode::Replace) {
				return true;
			}
			return !s_lichConfigCache.allowBaseWheel;
		case TransformState::Human:
		default:
			return false;
		}
	}

	bool IsWerewolfBaseWheelActive()
	{
		const int activeWheelIdx = Wheeler::GetActiveWheelIndex();
		return activeWheelIdx >= 0 && !IsTransformWheelIndexInternal(activeWheelIdx);
	}

	bool ShouldBypassWerewolfBaseWheelSpellGuard(RE::SpellItem* spell)
	{
		if (!spell || !Config::WheelBehavior::TransformWheels::WerewolfAllowBaseWheelSpells) {
			return false;
		}
		if (!Config::WheelBehavior::TransformWheels::WerewolfAllowBaseWheel) {
			return false;
		}
		if (!IsWerewolfBaseWheelActive()) {
			return false;
		}
		// Keep transform-entry-like powers blocked even when human base-wheel magic is allowed.
		if (IsWerewolfTransformEntrySpell(spell) || IsLikelyTransformEntrySpell(spell)) {
			return false;
		}
		return true;
	}

	bool ShouldBypassWerewolfBaseWheelShoutGuard(RE::TESShout* shout)
	{
		if (!shout || !Config::WheelBehavior::TransformWheels::WerewolfAllowBaseWheelShouts) {
			return false;
		}
		if (!Config::WheelBehavior::TransformWheels::WerewolfAllowBaseWheel) {
			return false;
		}
		return IsWerewolfBaseWheelActive();
	}

	bool ShouldBlockSpellByWerewolfGuard(RE::SpellItem* spell, const char* source)
	{
		if (!spell) {
			return false;
		}
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return false;
		}
		if (GetCurrentTransformState(pc) != TransformState::Werewolf) {
			return false;
		}
		if (ShouldBypassWerewolfBaseWheelSpellGuard(spell)) {
			if (Config::WheelBehavior::TransformWheels::DebugLog) {
				logger::info(
					"TransformWheels: [WerewolfGuard] allowing base-wheel spell {:08X} edid='{}' name='{}' source='{}' reason=private_ini_override",
					spell->GetFormID(),
					spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
					spell->GetName() ? spell->GetName() : "",
					source ? source : "");
			}
			return false;
		}

		const bool blockedByType = !IsActivatableTransformSpell(spell, TransformState::Werewolf);
		const bool blockedByEntry = IsWerewolfTransformEntrySpell(spell);
		const bool block = blockedByType || blockedByEntry;
		if (block && Config::WheelBehavior::TransformWheels::DebugLog) {
			logger::info(
				"TransformWheels: [WerewolfGuard] blocked spell {:08X} edid='{}' name='{}' source='{}' typeBlocked={} entryBlocked={}",
				spell->GetFormID(),
				spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
				spell->GetName() ? spell->GetName() : "",
				source ? source : "",
				blockedByType,
				blockedByEntry);
		}
		return block;
	}

	bool ShouldBlockSpellByVampireLordGuard(RE::SpellItem* spell, const char* source)
	{
		if (!spell) {
			return false;
		}
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return false;
		}
		if (GetCurrentTransformState(pc) != TransformState::VampireLord) {
			return false;
		}
		RefreshVampireLordConfigCache();
		if (!s_vampireLordConfigCache.enabled) {
			return false;
		}

		if (s_vampireLordConfigCache.blockHiddenSpellActivation &&
			IsVampireLordHiddenSpell(spell)) {
			if (s_vampireLordConfigCache.debugLog) {
				logger::info(
					"TransformWheels: [VampireLordGuard] blocked hidden spell {:08X} edid='{}' name='{}' source='{}'",
					spell->GetFormID(),
					spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
					spell->GetName() ? spell->GetName() : "",
					source ? source : "");
			}
			return true;
		}

		// This guard is intentionally VL-only and regular-spell-only. Vampire Lord
		// melee form should not receive hand-spell equips, but powers such as Bats
		// and Vampire's Sight still use the voice/power path and are left alone.
		return ShouldHideVampireLordSpellForCombatMode(spell, pc, source);
	}

	bool ShouldBlockShoutByVampireLordGuard(RE::TESShout* shout, const char* source)
	{
		if (!shout) {
			return false;
		}
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return false;
		}
		if (GetCurrentTransformState(pc) != TransformState::VampireLord) {
			return false;
		}
		RefreshVampireLordConfigCache();
		if (!s_vampireLordConfigCache.enabled ||
			!s_vampireLordConfigCache.blockHiddenSpellActivation ||
			!IsVampireLordHiddenShout(shout)) {
			return false;
		}

		if (s_vampireLordConfigCache.debugLog) {
			logger::info(
				"TransformWheels: [VampireLordGuard] blocked hidden shout {:08X} edid='{}' name='{}' source='{}'",
				shout->GetFormID(),
				shout->GetFormEditorID() ? shout->GetFormEditorID() : "",
				shout->GetName() ? shout->GetName() : "",
				source ? source : "");
		}
		return true;
	}

	bool ShouldBlockShoutByWerewolfGuard(RE::TESShout* shout, const char* source)
	{
		if (!shout) {
			return false;
		}
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return false;
		}
		if (GetCurrentTransformState(pc) != TransformState::Werewolf) {
			return false;
		}
		if (ShouldBypassWerewolfBaseWheelShoutGuard(shout)) {
			if (Config::WheelBehavior::TransformWheels::DebugLog) {
				logger::info(
					"TransformWheels: [WerewolfGuard] allowing base-wheel shout {:08X} edid='{}' name='{}' source='{}' reason=private_ini_override",
					shout->GetFormID(),
					shout->GetFormEditorID() ? shout->GetFormEditorID() : "",
					shout->GetName() ? shout->GetName() : "",
					source ? source : "");
			}
			return false;
		}

		if (IsTransformKitShout(shout, TransformState::Werewolf)) {
			return false;
		}

		std::unordered_set<RE::FormID> raceShouts;
		if (auto* race = GetTransformRace(TransformState::Werewolf, pc); race && race->actorEffects) {
			CollectSpellDataShouts(race->actorEffects, raceShouts);
		}
		const bool block = raceShouts.find(shout->GetFormID()) == raceShouts.end();
		if (block && Config::WheelBehavior::TransformWheels::DebugLog) {
			logger::info(
				"TransformWheels: [WerewolfGuard] blocked shout {:08X} edid='{}' name='{}' source='{}'",
				shout->GetFormID(),
				shout->GetFormEditorID() ? shout->GetFormEditorID() : "",
				shout->GetName() ? shout->GetName() : "",
				source ? source : "");
		}
		return block;
	}

	struct SpellLoadoutTraits
	{
		bool boundLike = false;
		bool summonLike = false;
		bool weaponLike = false;
		bool gearLike = false;
		bool instrumentLike = false;
	};

	template <std::size_t N>
	bool MatchesAnyToken(std::string_view a, std::string_view b, std::string_view c, const std::array<std::string_view, N>& tokens)
	{
		for (const auto token : tokens) {
			if (ContainsIgnoreCase(a, token) || ContainsIgnoreCase(b, token) || ContainsIgnoreCase(c, token)) {
				return true;
			}
		}
		return false;
	}

	SpellLoadoutTraits ClassifySpellLoadoutTraits(RE::SpellItem* spell)
	{
		SpellLoadoutTraits traits{};
		if (!spell) {
			return traits;
		}

		const char* edidC = spell->GetFormEditorID();
		const char* nameC = spell->GetName();
		const std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		const std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};
		const std::string_view source = GetSourceFileName(spell);

		constexpr std::array<std::string_view, 10> kSummonTokens{
			"bound", "conjure", "summon", "manifest", "spawn", "create", "spectral", "ethereal", "phantom", "ghost"
		};
		constexpr std::array<std::string_view, 16> kWeaponTokens{
			"weapon", "sword", "dagger", "axe", "mace", "hammer", "bow", "crossbow",
			"staff", "blade", "greatsword", "warhammer", "armament", "arsenal", "halberd", "scythe"
		};
		constexpr std::array<std::string_view, 15> kGearTokens{
			"armor", "armour", "shield", "robe", "hood", "helmet", "gauntlet", "glove",
			"boots", "greaves", "cuirass", "cloak", "outfit", "attire", "lantern"
		};
		constexpr std::array<std::string_view, 7> kInstrumentTokens{
			"instrument", "bard", "lute", "drum", "flute", "lyre", "tambourine"
		};

		traits.boundLike = ContainsIgnoreCase(edid, "bound") || ContainsIgnoreCase(name, "bound");
		traits.summonLike = traits.boundLike || MatchesAnyToken(edid, name, source, kSummonTokens);
		traits.weaponLike = MatchesAnyToken(edid, name, source, kWeaponTokens);
		traits.gearLike = MatchesAnyToken(edid, name, source, kGearTokens);
		traits.instrumentLike = MatchesAnyToken(edid, name, source, kInstrumentTokens);

		// "Bound" spells are almost always loadout-affecting (weapon or shield), keep this conservative.
		if (traits.boundLike) {
			traits.weaponLike = true;
		}

		return traits;
	}

	bool IsLichLoadoutGuardActive(TransformState state)
	{
		if (state != TransformState::Lich) {
			return false;
		}
		RefreshLichConfigCache();
		return s_lichConfigCache.enabled && s_lichConfigCache.mode != LichMode::Disabled;
	}

	bool ShouldBlockSpellByLichLoadout(RE::SpellItem* spell, const char* source)
	{
		if (!spell) {
			return false;
		}
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return false;
		}
		const TransformState currentState = GetCurrentTransformState(pc);
		if (!IsLichLoadoutGuardActive(currentState)) {
			return false;
		}

		const SpellLoadoutTraits traits = ClassifySpellLoadoutTraits(spell);
		const bool blockBound = s_lichConfigCache.blockBoundSpells && traits.boundLike;
		const bool blockWeaponSpawn = s_lichConfigCache.hideWeapons &&
			traits.weaponLike &&
			(traits.summonLike || ContainsIgnoreCase(spell->GetFormEditorID() ? spell->GetFormEditorID() : "", "armament") ||
				ContainsIgnoreCase(spell->GetName() ? spell->GetName() : "", "armament"));
		const bool blockGearSpawn = s_lichConfigCache.hideGear &&
			((traits.gearLike && traits.summonLike) || traits.instrumentLike);

		const bool block = blockBound || blockWeaponSpawn || blockGearSpawn;
		if (block && s_lichConfigCache.debugLog) {
			logger::info(
				"TransformWheels: [LichLoadout] blocked spell {:08X} edid='{}' name='{}' source='{}' bound={} weaponSpawn={} gearSpawn={} summonLike={} weaponLike={} gearLike={} instrumentLike={}",
				spell->GetFormID(),
				spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
				spell->GetName() ? spell->GetName() : "",
				source ? source : "",
				blockBound,
				blockWeaponSpawn,
				blockGearSpawn,
				traits.summonLike,
				traits.weaponLike,
				traits.gearLike,
				traits.instrumentLike);
		}
		return block;
	}

	bool ShouldBlockSpellByLichGuard(RE::SpellItem* spell, const char* source)
	{
		if (!spell) {
			return false;
		}
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return false;
		}
		const TransformState currentState = GetCurrentTransformState(pc);
		if (currentState != TransformState::Lich) {
			return false;
		}
		RefreshLichConfigCache();
		if (!s_lichConfigCache.enabled || s_lichConfigCache.mode == LichMode::Disabled) {
			return false;
		}
		const auto guardMode = s_lichConfigCache.transformGuard;
		if (guardMode == LichTransformGuardMode::Off) {
			return false;
		}

		const bool exitSpell = IsLichExitSpell(spell);
		if (exitSpell) {
			return false;
		}
		const bool transformEntryLike = IsLikelyTransformEntrySpell(spell);
		if (!transformEntryLike) {
			return false;
		}

		bool block = false;
		if (guardMode == LichTransformGuardMode::BlockAllTransformsExceptExit) {
			block = true;
		} else if (guardMode == LichTransformGuardMode::BlockOtherTransforms) {
			// Keep lich kit abilities usable, only block "other transform" entries.
			block = !IsTransformKitSpell(spell, TransformState::Lich);
		}

		if (block && s_lichConfigCache.debugLog) {
			logger::info(
				"TransformWheels: [LichGuard] blocked spell {:08X} edid='{}' name='{}' source='{}' mode={} exit={} kit={} transformLike={}",
				spell->GetFormID(),
				spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
				spell->GetName() ? spell->GetName() : "",
				source ? source : "",
				static_cast<int>(guardMode),
				exitSpell,
				IsTransformKitSpell(spell, TransformState::Lich),
				transformEntryLike);
		}
		return block;
	}

	bool IsMiscWearableLike(RE::TESObjectMISC* miscItem)
	{
		if (!miscItem) {
			return false;
		}

		const RE::FormID fullFormID = miscItem->GetFormID();
		const RE::FormID baseFormID = fullFormID & 0x00FFFFFF;
		constexpr RE::FormID kFlute = 0x000DABA7;
		constexpr RE::FormID kFlute01 = 0x00105177;
		constexpr RE::FormID kDancersFlute = 0x00105109;
		constexpr RE::FormID kDrum = 0x000DABA9;
		constexpr RE::FormID kLute = 0x000DABAB;
		if (fullFormID == kFlute || fullFormID == kFlute01 || fullFormID == kDancersFlute ||
			fullFormID == kDrum || fullFormID == kLute ||
			baseFormID == kFlute || baseFormID == kFlute01 || baseFormID == kDancersFlute ||
			baseFormID == kDrum || baseFormID == kLute) {
			return true;
		}

		const char* edidC = miscItem->GetFormEditorID();
		const char* nameC = miscItem->GetName();
		const std::string_view edid = edidC ? std::string_view(edidC) : std::string_view{};
		const std::string_view name = nameC ? std::string_view(nameC) : std::string_view{};
		const std::string_view source = GetSourceFileName(miscItem);
		constexpr std::array<std::string_view, 11> kWearableMiscTokens{
			"torch", "lantern", "instrument", "bard", "lute", "drum", "flute", "lyre", "tambourine", "wearable", "equippable"
		};
		if (MatchesAnyToken(edid, name, source, kWearableMiscTokens)) {
			return true;
		}

		auto* keywordForm = miscItem->As<RE::BGSKeywordForm>();
		if (!keywordForm) {
			return false;
		}
		for (std::uint32_t i = 0; i < keywordForm->numKeywords; ++i) {
			auto* keyword = keywordForm->keywords[i];
			if (!keyword) {
				continue;
			}
			const char* keywordEdid = keyword->GetFormEditorID();
			if (!keywordEdid) {
				continue;
			}
			if (MatchesAnyToken(std::string_view(keywordEdid), {}, {}, kWearableMiscTokens)) {
				return true;
			}
		}

		return false;
	}

	bool ShouldBlockMiscByLichLoadout(RE::TESObjectMISC* miscItem, const char* source, bool logBlock)
	{
		if (!miscItem) {
			return false;
		}
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return false;
		}
		const TransformState currentState = GetCurrentTransformState(pc);
		if (!IsLichLoadoutGuardActive(currentState)) {
			return false;
		}
		if (!s_lichConfigCache.hideGear) {
			return false;
		}
		if (!IsMiscWearableLike(miscItem)) {
			return false;
		}

		if (logBlock && s_lichConfigCache.debugLog) {
			logger::info("TransformWheels: [LichLoadout] blocked misc {:08X} edid='{}' name='{}' source='{}'",
				miscItem->GetFormID(),
				miscItem->GetFormEditorID() ? miscItem->GetFormEditorID() : "",
				miscItem->GetName() ? miscItem->GetName() : "",
				source ? source : "");
		}
		return true;
	}

	bool ShouldBlockWeaponByLichLoadout(RE::TESObjectWEAP* weapon)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return false;
		}
		const TransformState currentState = GetCurrentTransformState(pc);
		if (!IsLichLoadoutGuardActive(currentState)) {
			return false;
		}

		// AllowBaseWheel only controls navigation. The item-level loadout guard
		// remains config-driven so users can keep human wheels visible in lich form
		// while still making unsafe non-staff weapon entries inaccessible.
		if (s_lichConfigCache.hideWeapons) {
			return !weapon || weapon->GetWeaponType() != RE::WEAPON_TYPE::kStaff;
		}

		return weapon &&
			weapon->GetWeaponType() == RE::WEAPON_TYPE::kStaff &&
			s_lichConfigCache.blockStaffSwapping;
	}

	void UpdateVampireLordCombatModeTracking(RE::PlayerCharacter* pc, TransformState currentState, double now, bool stateJustChanged)
	{
		if (currentState != TransformState::VampireLord) {
			if (s_lastVampireLordCombatMode != VampireLordCombatMode::Unknown) {
				s_loggedVampireLordModeHiddenForms.clear();
			}
			s_lastVampireLordCombatMode = VampireLordCombatMode::Unknown;
			s_vampireLordModeDebounce = {};
			s_loggedVampireLordUnknownHold = false;
			s_lastVampireLordModeDiagnosticSnapshot.reset();
			return;
		}

		RefreshVampireLordConfigCache();
		if (stateJustChanged) {
			s_lastVampireLordCombatMode = VampireLordCombatMode::Unknown;
			s_vampireLordModeDebounce = {};
			s_loggedVampireLordUnknownHold = false;
			s_loggedVampireLordModeHiddenForms.clear();
		}

		const VampireLordCombatMode rawMode = GetVampireLordCombatMode(pc);
		const bool directCastActive = Wheeler::IsDirectCastPipelineActiveForHandMemory();
		const bool actorSettled = IsVampireLordActorSettledForModeObservation(pc);
		VampireLordModeObservationStatus observationStatus = VampireLordModeObservationStatus::Candidate;
		if (stateJustChanged) {
			observationStatus = VampireLordModeObservationStatus::InitialTransform;
		} else if (directCastActive) {
			observationStatus = VampireLordModeObservationStatus::DirectCastActive;
		} else if (rawMode == VampireLordCombatMode::Unknown) {
			observationStatus = VampireLordModeObservationStatus::GraphUnavailableOrMixed;
		} else if (!actorSettled) {
			observationStatus = VampireLordModeObservationStatus::ActorUnsettled;
		}

		const bool acceptSample = observationStatus == VampireLordModeObservationStatus::Candidate;
		const VampireLordCombatMode previousMode = s_lastVampireLordCombatMode;
		const auto confirmedMode = ObserveVampireLordModeCandidate(
			s_vampireLordModeDebounce,
			rawMode,
			now,
			acceptSample);
		const bool modeChanged = confirmedMode.has_value() && *confirmedMode != previousMode;
		if (modeChanged) {
			s_lastVampireLordCombatMode = *confirmedMode;
			s_loggedVampireLordModeHiddenForms.clear();
		}

		UpdateVampireLordModeDiagnostics(
			pc,
			rawMode,
			s_lastVampireLordCombatMode,
			s_vampireLordModeDebounce.candidate,
			s_vampireLordModeDebounce.samples,
			s_vampireLordModeDebounce.since,
			now,
			observationStatus,
			actorSettled,
			directCastActive,
			stateJustChanged);

		if (rawMode == VampireLordCombatMode::Unknown &&
			s_lastVampireLordCombatMode != VampireLordCombatMode::Unknown) {
			if (!s_loggedVampireLordUnknownHold &&
				(s_vampireLordConfigCache.debugLog || Config::WheelBehavior::TransformWheels::DebugLog)) {
				RE::TESForm* left = pc ? pc->GetEquippedObject(true) : nullptr;
				RE::TESForm* right = pc ? pc->GetEquippedObject(false) : nullptr;
				logger::info(
					"TransformWheels: [VampireLordMode] raw Unknown, keeping previous mode {} left={:08X} right={:08X}",
					ToString(s_lastVampireLordCombatMode),
					left ? left->GetFormID() : 0,
					right ? right->GetFormID() : 0);
				s_loggedVampireLordUnknownHold = true;
			}
		} else if (rawMode != VampireLordCombatMode::Unknown) {
			s_loggedVampireLordUnknownHold = false;
		}
		if (!modeChanged) {
			return;
		}

		if (s_vampireLordConfigCache.debugLog || Config::WheelBehavior::TransformWheels::DebugLog) {
			RE::TESForm* left = pc ? pc->GetEquippedObject(true) : nullptr;
			RE::TESForm* right = pc ? pc->GetEquippedObject(false) : nullptr;
			logger::info(
				"TransformWheels: [VampireLordMode] mode changed {} -> {} left={:08X} right={:08X}",
				ToString(previousMode),
				ToString(s_lastVampireLordCombatMode),
				left ? left->GetFormID() : 0,
				right ? right->GetFormID() : 0);
		}

		if (stateJustChanged) {
			return;
		}

		auto& info = GetWheelInfo(TransformState::VampireLord);
		info.lastGeneratedAt = 0.0;
		TryPopulateTransformWheel(TransformState::VampireLord, pc, now);
	}
}

void TransformWheelManager::Update()
{
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc || !pc->Is3DLoaded()) {
		return;
	}

	const bool enabled = Config::WheelBehavior::TransformWheels::Enabled &&
		Config::WheelBehavior::TransformWheels::Mode != 0;
	const TransformState currentState = GetCurrentTransformState(pc);
	s_activeGenericRaceContext = (currentState == TransformState::Generic) ? ResolveGenericRaceContext(pc) : 0;
	const double now = ImGui::GetTime();

	if (Config::WheelBehavior::TransformWheels::CacheHumanSnapshot && currentState == TransformState::Human) {
		const float throttleMs = static_cast<float>(Config::WheelBehavior::TransformWheels::RegenerateThrottleMs);
		const double throttleSec = (std::max)(0.0f, throttleMs / 1000.0f);
		if (now - s_lastHumanSnapshotAt >= throttleSec) {
			// Use CollectPlayerAllSpells to include ActorBase spells (werewolf howls, etc.)
			s_humanSpellSnapshot = CollectPlayerAllSpells(pc);
			s_humanEquippedSpellSnapshot.clear();
			CollectEquippedSpellForms(pc, s_humanEquippedSpellSnapshot);
			if (Config::WheelBehavior::TransformWheels::IncludeShouts) {
				s_humanShoutSnapshot = CollectPlayerShouts(pc);
			} else {
				s_humanShoutSnapshot.clear();
			}
			s_lastHumanSnapshotAt = now;
		}
	}

	if (!enabled) {
		RemoveAllTransformWheels();
		ClearVampireLordSessionSpells("disabled");
		if (IsTransformWheelIndexInternal(Wheeler::GetActiveWheelIndex())) {
			RestoreHumanWheel();
		}
		s_pendingTransform.active = false;
		s_savedHumanWheelIdx.reset();
		s_switchedToTransform = false;
		s_lastState = currentState;
		s_lastVampireLordCombatMode = VampireLordCombatMode::Unknown;
		s_vampireLordModeDebounce = {};
		s_loggedVampireLordUnknownHold = false;
		s_loggedVampireLordModeHiddenForms.clear();
		s_lastVampireLordModeDiagnosticSnapshot.reset();
		s_lastGenericRaceContext = 0;
		s_activeGenericRaceContext = 0;
		return;
	}

	if (currentState != TransformState::VampireLord) {
		ClearVampireLordSessionSpells("left_vampire_lord");
	}
	if (currentState != TransformState::Lich) {
		s_lichStableSpellOrder.clear();
	}

	ProcessPendingTransform(pc, currentState, now);

	if (currentState == TransformState::Generic &&
		s_lastState == TransformState::Generic &&
		s_lastGenericRaceContext != 0 &&
		s_activeGenericRaceContext != 0 &&
		s_lastGenericRaceContext != s_activeGenericRaceContext) {
		if (Config::WheelBehavior::TransformWheels::DebugLog) {
			logger::info("TransformWheels: generic race context changed {:08X} -> {:08X}",
				s_lastGenericRaceContext, s_activeGenericRaceContext);
		}
		StartPendingTransform(currentState, now);
	}

	// Late refresh: poll for newly-added spells after wheel opens (while in transform state)
	if (s_lateRefresh.active && currentState != TransformState::Human) {
		if (now >= s_lateRefresh.nextPollAt) {
			s_lateRefresh.nextPollAt = now + LateRefresh::kPollIntervalSec;
			s_lateRefresh.triesLeft--;
			
			// Re-populate the transform wheel and check if content changed
			TryPopulateTransformWheel(currentState, pc, now);
			auto& info = GetWheelInfo(currentState);
			
			if (info.hasHash && info.lastHash != s_lateRefresh.lastHash) {
				// Content changed - update our tracking hash
				if (Config::WheelBehavior::TransformWheels::DebugLog) {
					logger::info("TransformWheels: late refresh detected change (tries left={})", s_lateRefresh.triesLeft);
				}
				s_lateRefresh.lastHash = info.lastHash;
			}
			
			if (s_lateRefresh.triesLeft <= 0) {
				if (Config::WheelBehavior::TransformWheels::DebugLog) {
					logger::info("TransformWheels: late refresh complete");
				}
				s_lateRefresh.active = false;
			}
		}
	} else if (s_lateRefresh.active && currentState == TransformState::Human) {
		// Reverted to human - stop late refresh
		s_lateRefresh.active = false;
	}

	// Fix: Restore human wheel FIRST (while transform wheel still exists), then delete transform wheels.
	// This prevents deletion from failing due to active wheel index mismatch.
	if (currentState == TransformState::Human &&
		IsTransformWheelIndexInternal(Wheeler::GetActiveWheelIndex())) {
		RestoreHumanWheel();
	}

	if (currentState == TransformState::Human && HasAnyTransformWheels()) {
		RemoveAllTransformWheels();
	}

	const bool stateChanged = currentState != s_lastState;
	if (stateChanged) {
		if (Config::WheelBehavior::TransformWheels::DebugLog) {
			logger::info("TransformWheels: state change {} -> {}", ToString(s_lastState), ToString(currentState));
		}
		switch (currentState) {
		case TransformState::Werewolf:
		case TransformState::VampireLord:
		case TransformState::Lich:
		case TransformState::Generic:
			StartPendingTransform(currentState, now);
			break;
		case TransformState::Human:
		default:
			// Fix: Restore first, then delete (same ordering as the non-state-change path above)
			ClearVampireLordSessionSpells("returned_human");
			RestoreHumanWheel();
			RemoveAllTransformWheels();
			break;
		}
		s_lastState = currentState;
	}

	UpdateVampireLordCombatModeTracking(pc, currentState, now, stateChanged);
	s_lastGenericRaceContext = (currentState == TransformState::Generic) ? s_activeGenericRaceContext : 0;
}

void TransformWheelManager::Reset()
{
	s_lastState = TransformState::Human;
	s_savedHumanWheelIdx.reset();
	s_humanSpellSnapshot.clear();
	s_humanShoutSnapshot.clear();
	s_humanEquippedSpellSnapshot.clear();
	s_lastHumanSnapshotAt = 0.0;
	s_werewolfWheel = {};
	s_vampireWheel = {};
	s_genericWheel = {};
	s_lichWheel = {};
	s_pendingTransform = {};
	s_lateRefresh = {};
	s_switchedToTransform = false;
	s_activeGenericRaceContext = 0;
	s_lastGenericRaceContext = 0;
	s_genericTriggerCache = {};
	s_vampireLordConfigCache = {};
	s_vampireLordSessionSpellForms.clear();
	s_vampireLordLoggedRejectedEquippedForms.clear();
	s_lastVampireLordCombatMode = VampireLordCombatMode::Unknown;
	s_vampireLordModeDebounce = {};
	s_loggedVampireLordUnknownHold = false;
	s_loggedVampireLordModeHiddenForms.clear();
	s_lastVampireLordModeDiagnosticSnapshot.reset();
	s_vampireLordRaiseDeadRepresentative = 0;
	s_lastVampireLordRaiseDeadLogSignature = 0;
	s_hasVampireLordRaiseDeadLogSignature = false;
	s_lichStableSpellOrder.clear();
	s_loggedLichRaceMatches.clear();
	s_lichConfigCache = {};
	s_knownGenericWheelIds.clear();
	s_knownGenericWheelIds.push_back(kGenericWheelIdFallback);
	s_knownLichWheelIds.clear();
	s_knownLichWheelIds.push_back(kLichWheelIdFallback);
	s_precedenceSignature = 0;
	s_precedenceOrder = { TransformState::Werewolf, TransformState::VampireLord, TransformState::Lich, TransformState::Generic };
}

bool TransformWheelManager::IsTransformWheelIndex(int index)
{
	return IsTransformWheelIndexInternal(index);
}

bool TransformWheelManager::IsPlayerHuman()
{
	auto* pc = RE::PlayerCharacter::GetSingleton();
	return GetCurrentTransformState(pc) == TransformState::Human;
}

TransformState TransformWheelManager::GetPlayerTransformState()
{
	auto* pc = RE::PlayerCharacter::GetSingleton();
	return GetCurrentTransformState(pc);
}

std::optional<int> TransformWheelManager::GetSavedHumanWheelIndex()
{
	return s_savedHumanWheelIdx;
}

bool TransformWheelManager::ShouldRestrictNavigationToTransformWheels()
{
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return false;
	}
	const TransformState currentState = GetCurrentTransformState(pc);
	return ShouldRestrictToTransformWheelsForState(currentState);
}

bool TransformWheelManager::IsSpellActivationBlocked(RE::SpellItem* spell, const char* source)
{
	if (ShouldBlockMajorTransformSpellForCurrentState(spell, source)) {
		return true;
	}
	if (ShouldBlockSpellByVampireLordGuard(spell, source)) {
		return true;
	}
	if (ShouldBlockSpellByWerewolfGuard(spell, source)) {
		return true;
	}
	if (ShouldBlockSpellByLichGuard(spell, source)) {
		return true;
	}
	return ShouldBlockSpellByLichLoadout(spell, source);
}

bool TransformWheelManager::IsMajorTransformSpellBlockedForCurrentState(RE::SpellItem* spell)
{
	if (!spell) {
		return false;
	}
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return false;
	}
	const TransformState currentState = GetCurrentTransformState(pc);
	return IsMajorTransformRuntimeState(currentState) &&
		GetMajorTransformSpellKind(spell) != MajorTransformKind::None;
}

bool TransformWheelManager::IsVampireLordSpellHiddenForCurrentMode(RE::SpellItem* spell)
{
	auto* pc = RE::PlayerCharacter::GetSingleton();
	return ShouldHideVampireLordSpellForCombatMode(spell, pc, "Draw");
}

bool TransformWheelManager::IsShoutActivationBlocked(RE::TESShout* shout, const char* source)
{
	if (ShouldBlockShoutByVampireLordGuard(shout, source)) {
		return true;
	}
	return ShouldBlockShoutByWerewolfGuard(shout, source);
}

bool TransformWheelManager::ShouldBlockMiscActivation(RE::TESObjectMISC* miscItem, const char* source)
{
	return ShouldBlockMiscByLichLoadout(miscItem, source, true);
}

bool TransformWheelManager::IsLichActive()
{
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return false;
	}
	return GetCurrentTransformState(pc) == TransformState::Lich;
}

bool TransformWheelManager::ShouldBlockWeaponActivation()
{
	return ShouldBlockWeaponByLichLoadout(nullptr);
}

bool TransformWheelManager::ShouldBlockWeaponActivation(RE::TESObjectWEAP* weapon)
{
	return ShouldBlockWeaponByLichLoadout(weapon);
}

bool TransformWheelManager::ShouldDimWeaponActivation(RE::TESObjectWEAP* weapon)
{
	return ShouldBlockWeaponByLichLoadout(weapon);
}

bool TransformWheelManager::ShouldBlockStaffActivation(RE::TESObjectWEAP* weapon, const char* source)
{
	if (!weapon || weapon->GetWeaponType() != RE::WEAPON_TYPE::kStaff) {
		return false;
	}
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return false;
	}
	if (GetCurrentTransformState(pc) != TransformState::Lich) {
		return false;
	}
	RefreshLichConfigCache();
	if (!s_lichConfigCache.enabled ||
		s_lichConfigCache.mode == LichMode::Disabled ||
		!s_lichConfigCache.blockStaffSwapping) {
		return false;
	}
	if (s_lichConfigCache.hideWeapons) {
		// HideWeapons now treats staffs as a Lich-only exception because staffs are
		// spell tools for these setups. Non-staff weapons stay blocked by the
		// loadout guard above; this does not affect normal form or other transforms.
		return false;
	}

	// Lich form intentionally keeps base-wheel navigation available, so this guard
	// only blocks the known-unsafe staff equip/swap path while confirmed in lich form.
	logger::info("TransformWheels: [LichStaffGuard] blocked staff activation source={} formId={:08X} name='{}'",
		source ? source : "unknown",
		weapon->GetFormID(),
		weapon->GetName() ? weapon->GetName() : "");
	return true;
}

bool TransformWheelManager::ShouldSuppressLichDirectCast(RE::SpellItem* spell, const char* source)
{
	if (!spell) {
		return false;
	}
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return false;
	}
	if (GetCurrentTransformState(pc) != TransformState::Lich) {
		return false;
	}
	RefreshLichConfigCache();
	if (!s_lichConfigCache.enabled ||
		s_lichConfigCache.mode == LichMode::Disabled ||
		!s_lichConfigCache.suppressDirectCast) {
		return false;
	}

	const bool configuredLichSpell = s_lichConfigCache.additionalSpellFormIDs.contains(spell->GetFormID());
	const bool lichKitSpell = IsTransformKitSpell(spell, TransformState::Lich) || IsLichExitSpell(spell);
	if (!configuredLichSpell && !lichKitSpell) {
		return false;
	}

	if (source) {
		logger::info("TransformWheels: [LichDirectCastGuard] suppressed direct-cast source={} formId={:08X} edid='{}' name='{}' configured={} kit={}",
			source,
			spell->GetFormID(),
			spell->GetFormEditorID() ? spell->GetFormEditorID() : "",
			spell->GetName() ? spell->GetName() : "",
			configuredLichSpell,
			lichKitSpell);
	}
	return true;
}

bool TransformWheelManager::ShouldBlockGearActivation()
{
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return false;
	}
	if (GetCurrentTransformState(pc) != TransformState::Lich) {
		return false;
	}
	RefreshLichConfigCache();
	return s_lichConfigCache.enabled &&
		s_lichConfigCache.mode != LichMode::Disabled &&
		s_lichConfigCache.hideGear;
}

bool TransformWheelManager::ShouldDimGearActivation()
{
	return ShouldBlockGearActivation();
}

bool TransformWheelManager::ShouldDimMiscActivation(RE::TESObjectMISC* miscItem)
{
	return ShouldBlockMiscByLichLoadout(miscItem, nullptr, false);
}

void TransformWheelManager::OnWheelOpened()
{
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}
	
	const TransformState state = GetCurrentTransformState(pc);
	if (state == TransformState::Human) {
		// Not in transform state - no late refresh needed
		return;
	}
	
	if (!Config::WheelBehavior::TransformWheels::Enabled) {
		return;
	}
	
	// Start late refresh polling
	const double now = ImGui::GetTime();
	s_lateRefresh.active = true;
	s_lateRefresh.triesLeft = LateRefresh::kMaxTries;
	s_lateRefresh.nextPollAt = now + LateRefresh::kPollIntervalSec;
	s_lateRefresh.lastHash = 0;
	
	// Get current hash for comparison
	auto& info = GetWheelInfo(state);
	if (info.hasHash) {
		s_lateRefresh.lastHash = info.lastHash;
	}
	
	if (Config::WheelBehavior::TransformWheels::DebugLog) {
		logger::info("TransformWheels: OnWheelOpened - started late refresh (state={}, tries={})",
			ToString(state), s_lateRefresh.triesLeft);
	}
}
