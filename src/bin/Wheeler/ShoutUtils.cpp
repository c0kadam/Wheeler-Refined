// ShoutUtils.cpp

#include "ShoutUtils.h"

#include "bin/Config.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>

#include <RE/A/AIProcess.h>
#include <RE/B/BSFixedString.h>
#include <RE/B/BSTSmartPointer.h>
#include <RE/F/FunctionArguments.h>
#include <RE/H/HighProcessData.h>
#include <RE/I/IFunction.h>
#include <RE/I/IStackCallbackFunctor.h>
#include <RE/O/ObjectTypeInfo.h>
#include <RE/S/SkyrimVM.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESWordOfPower.h>
#include <RE/V/Variable.h>

namespace
{
	using SteadyClock = std::chrono::steady_clock;

	constexpr auto kUnlockCacheTtl = std::chrono::milliseconds(250);
	constexpr auto kUnlockLogRateLimit = std::chrono::milliseconds(400);
	constexpr auto kStableCacheTtl = std::chrono::milliseconds(2000);
	constexpr auto kComputeLogRateLimit = std::chrono::milliseconds(500);
	constexpr float kVmPumpBudget = 0.001f;

	constexpr const char* kMethodPapyrus = "PapyrusIsWordUnlocked";
	constexpr const char* kMethodEngine = "Engine";
	constexpr const char* kMethodHasSpell = "HasSpell";
	constexpr const char* kMethodFallback = "FallbackSafe";

	constexpr const char* kReasonNone = "-";
	constexpr const char* kReasonShoutNull = "ShoutNull";
	constexpr const char* kReasonVMNull = "VMNull";
	constexpr const char* kReasonGameTypeMissing = "GameTypeMissing";
	constexpr const char* kReasonFuncMissing = "FuncMissing";
	constexpr const char* kReasonDispatchFail = "DispatchFail";
	constexpr const char* kReasonCallbackPending = "CallbackPending";
	constexpr const char* kReasonWordNull = "WordNull";
	constexpr const char* kReasonNoPlayer = "NoPlayer";

	struct UnlockCacheEntry
	{
		ShoutUtils::UnlockResult result{};
		SteadyClock::time_point last{};
		bool hasPlayerContext = false;
	};

	struct UnlockStateCacheEntry
	{
		ShoutUtils::ShoutUnlockState state{};
		SteadyClock::time_point last{};
	};

	struct StableStateCacheEntry
	{
		ShoutUtils::ShoutUnlockState state{};
		SteadyClock::time_point last{};
	};

	struct UnlockLogState
	{
		SteadyClock::time_point last{};
		int lastLearnedCount = -1;
		int lastSoulCount = -1;
		int lastFinalCount = -1;
		std::uint8_t lastLearnedMask = 0;
		std::uint8_t lastSoulMask = 0;
		ShoutUtils::UnlockComputeState lastComputeState = ShoutUtils::UnlockComputeState::Known;
		int lastPreviousCount = -1;
		bool lastUsedLastStable = false;
	};

	struct UnlockComputeLogState
	{
		SteadyClock::time_point last{};
		ShoutUtils::UnlockComputeState lastState = ShoutUtils::UnlockComputeState::Known;
		int lastFinalCount = -1;
		int lastPreviousCount = -1;
		bool lastUsedLastStable = false;
	};

	struct GameFuncInfo
	{
		bool vmAvailable = false;
		bool gameTypeFound = false;
		bool gameTypeLinked = false;
		bool funcFound = false;
		std::uint32_t globalFuncCount = 0;
		std::uint32_t funcParamCount = 0;
		bool funcIsNative = false;
		bool funcIsStatic = false;
		bool funcCallableFromTasklets = false;
		std::string funcSignature;
		std::string returnType;
		std::string paramTypes;
	};

	std::unordered_map<RE::FormID, UnlockCacheEntry> s_unlockCache{};
	std::unordered_map<RE::FormID, UnlockStateCacheEntry> s_unlockStateCache{};
	std::unordered_map<RE::FormID, StableStateCacheEntry> s_stableStateCache{};
	std::unordered_map<RE::FormID, UnlockLogState> s_unlockLogState{};
	std::unordered_map<RE::FormID, UnlockComputeLogState> s_unlockComputeLogState{};
	std::mutex s_cacheMutex{};
	std::atomic<int> s_debugOverride{ -1 };
	std::atomic<bool> s_vmInitLogged{ false };

	static const char* SafeName(const char* name)
	{
		return (name && name[0] != '\0') ? name : "-";
	}

	static std::uint8_t BuildMask(const ShoutUtils::WordUnlockState (&words)[3], bool useSoul)
	{
		std::uint8_t mask = 0;
		for (std::uint8_t i = 0; i < 3; ++i) {
			const bool set = useSoul ? words[i].soulUnlocked : words[i].learned;
			if (set) {
				mask |= (1u << i);
			}
		}
		return mask;
	}

	class BoolResultCallback final : public RE::BSScript::IStackCallbackFunctor
	{
	public:
		BoolResultCallback(bool& outValue, bool& outCalled) :
			_value(outValue),
			_called(outCalled)
		{}

		void operator()(RE::BSScript::Variable a_result) override
		{
			if (a_result.IsBool()) {
				_value = a_result.GetBool();
			} else {
				_value = false;
			}
			_called = true;
		}

		void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override
		{}

	private:
		bool& _value;
		bool& _called;
	};

	static GameFuncInfo BuildGameFuncInfo(RE::BSScript::IVirtualMachine* vm)
	{
		GameFuncInfo info{};
		info.vmAvailable = (vm != nullptr);
		if (!vm) {
			return info;
		}

		const RE::BSFixedString className("Game");
		RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> typeInfo;
		if (!vm->GetScriptObjectTypeNoLoad(className, typeInfo)) {
			vm->GetScriptObjectType(className, typeInfo);
		}

		if (!typeInfo) {
			info.gameTypeFound = false;
			return info;
		}

		info.gameTypeFound = true;
		info.gameTypeLinked = typeInfo->IsLinked();
		info.globalFuncCount = typeInfo->GetNumGlobalFuncs();

		auto* funcs = typeInfo->GetGlobalFuncIter();
		const std::uint32_t funcCount = typeInfo->GetNumGlobalFuncs();
		for (std::uint32_t i = 0; i < funcCount; ++i) {
			const auto& entry = funcs[i];
			if (!entry.func) {
				continue;
			}
			const auto& fnName = entry.func->GetName();
			if (std::strcmp(fnName.c_str(), "IsWordUnlocked") == 0) {
				info.funcFound = true;
				info.funcParamCount = entry.func->GetParamCount();
				info.funcIsNative = entry.func->GetIsNative();
				info.funcIsStatic = entry.func->GetIsStatic();
				info.funcCallableFromTasklets = entry.func->CanBeCalledFromTasklets();
				info.returnType = entry.func->GetReturnType().TypeAsString();

				std::string sig = "IsWordUnlocked(";
				std::string params;
				for (std::uint32_t p = 0; p < info.funcParamCount; ++p) {
					RE::BSFixedString paramName;
					RE::BSScript::TypeInfo paramType;
					entry.func->GetParam(p, paramName, paramType);
					const std::string typeStr = paramType.TypeAsString();
					if (p > 0) {
						sig.append(", ");
						params.append(", ");
					}
					sig.append(typeStr);
					params.append(typeStr);
				}
				sig.append(") -> ");
				sig.append(info.returnType.empty() ? "?" : info.returnType);
				info.funcSignature = sig;
				info.paramTypes = params;
				break;
			}
		}

		return info;
	}

	static void LogVmInitIfNeeded()
	{
		if (!ShoutUtils::IsShoutPipelineDebugEnabled()) {
			return;
		}
		bool expected = false;
		if (!s_vmInitLogged.compare_exchange_strong(expected, true)) {
			return;
		}

		RE::SkyrimVM* skyrimVM = RE::SkyrimVM::GetSingleton();
		RE::BSScript::IVirtualMachine* vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl.get() : nullptr;
		const GameFuncInfo info = BuildGameFuncInfo(vm);

		logger::info("[ShoutUnlockVMInit] vm={} gameTypeFound={} linked={} funcFound={} funcSig='{}' funcParams={} retType='{}' native={} static={} tasklets={} globals={}",
			fmt::ptr(vm),
			info.gameTypeFound ? 1 : 0,
			info.gameTypeLinked ? 1 : 0,
			info.funcFound ? 1 : 0,
			info.funcSignature.empty() ? "-" : info.funcSignature,
			info.funcParamCount,
			info.returnType.empty() ? "-" : info.returnType,
			info.funcIsNative ? 1 : 0,
			info.funcIsStatic ? 1 : 0,
			info.funcCallableFromTasklets ? 1 : 0,
			info.globalFuncCount);
	}

	static void LogVmCallFailure(RE::FormID shoutID, RE::FormID wordID, const char* wordName, const GameFuncInfo& info,
		bool dispatchOk, bool callbackCalled, bool pumpedTasklets, bool pumpedUpdate, const char* reason)
	{
		if (!ShoutUtils::IsShoutPipelineDebugEnabled()) {
			return;
		}

		logger::info("[ShoutUnlockVMFail] shout={:08X} word={:08X} wordName='{}' reason={} vmAvail={} gameTypeFound={} linked={} funcFound={} funcSig='{}' dispatch={} callback={} pumpTasklets={} pumpUpdate={}",
			shoutID,
			wordID,
			wordName ? wordName : "-",
			reason ? reason : "-",
			info.vmAvailable ? 1 : 0,
			info.gameTypeFound ? 1 : 0,
			info.gameTypeLinked ? 1 : 0,
			info.funcFound ? 1 : 0,
			info.funcSignature.empty() ? "-" : info.funcSignature,
			dispatchOk ? 1 : 0,
			callbackCalled ? 1 : 0,
			pumpedTasklets ? 1 : 0,
			pumpedUpdate ? 1 : 0);
	}

	static bool TryCallIsWordUnlocked(RE::TESWordOfPower* word, RE::FormID shoutID, bool& outValue, std::string& outFailReason)
	{
		outValue = false;
		outFailReason.clear();

		if (!word) {
			outFailReason = kReasonWordNull;
			return false;
		}

		RE::SkyrimVM* skyrimVM = RE::SkyrimVM::GetSingleton();
		RE::BSScript::IVirtualMachine* vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl.get() : nullptr;
		const GameFuncInfo info = BuildGameFuncInfo(vm);

		if (!info.vmAvailable) {
			outFailReason = kReasonVMNull;
			LogVmCallFailure(shoutID, word->GetFormID(), word->GetName(), info, false, false, false, false, outFailReason.c_str());
			return false;
		}
		if (!info.gameTypeFound) {
			outFailReason = kReasonGameTypeMissing;
			LogVmCallFailure(shoutID, word->GetFormID(), word->GetName(), info, false, false, false, false, outFailReason.c_str());
			return false;
		}
		if (!info.funcFound) {
			outFailReason = kReasonFuncMissing;
			LogVmCallFailure(shoutID, word->GetFormID(), word->GetName(), info, false, false, false, false, outFailReason.c_str());
			return false;
		}

		auto* wordArg = word;
		std::unique_ptr<RE::BSScript::IFunctionArguments> args(
			RE::MakeFunctionArguments<RE::TESWordOfPower*>(std::move(wordArg)));
		if (!args) {
			outFailReason = kReasonDispatchFail;
			LogVmCallFailure(shoutID, word->GetFormID(), word->GetName(), info, false, false, false, false, outFailReason.c_str());
			return false;
		}

		bool called = false;
		bool result = false;
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback =
			RE::make_smart<BoolResultCallback>(result, called);

		const RE::BSFixedString className("Game");
		const RE::BSFixedString fnName("IsWordUnlocked");
		const bool dispatched = vm->DispatchStaticCall(className, fnName, args.get(), callback);
		bool pumpedTasklets = false;
		bool pumpedUpdate = false;

		if (dispatched && !called) {
			vm->UpdateTasklets(kVmPumpBudget);
			pumpedTasklets = true;
		}
		if (dispatched && !called) {
			vm->Update(kVmPumpBudget);
			pumpedUpdate = true;
		}

		if (!dispatched) {
			outFailReason = kReasonDispatchFail;
			LogVmCallFailure(shoutID, word->GetFormID(), word->GetName(), info, false, called, pumpedTasklets, pumpedUpdate, outFailReason.c_str());
			return false;
		}
		if (!called) {
			outFailReason = kReasonCallbackPending;
			LogVmCallFailure(shoutID, word->GetFormID(), word->GetName(), info, true, false, pumpedTasklets, pumpedUpdate, outFailReason.c_str());
			return false;
		}

		outValue = result;
		return true;
	}

	static bool TryGetCachedUnlockState(RE::FormID shoutID, ShoutUtils::ShoutUnlockState& outState)
	{
		const auto now = SteadyClock::now();
		std::scoped_lock lock(s_cacheMutex);
		auto it = s_unlockStateCache.find(shoutID);
		if (it == s_unlockStateCache.end()) {
			return false;
		}
		if (now - it->second.last > kUnlockCacheTtl) {
			return false;
		}
		outState = it->second.state;
		return true;
	}

	static void UpdateUnlockStateCache(RE::FormID shoutID, const ShoutUtils::ShoutUnlockState& state)
	{
		std::scoped_lock lock(s_cacheMutex);
		UnlockStateCacheEntry entry{};
		entry.state = state;
		entry.last = SteadyClock::now();
		s_unlockStateCache[shoutID] = entry;
	}

	static bool TryGetStableUnlockState(RE::FormID shoutID, ShoutUtils::ShoutUnlockState& outState)
	{
		const auto now = SteadyClock::now();
		std::scoped_lock lock(s_cacheMutex);
		auto it = s_stableStateCache.find(shoutID);
		if (it == s_stableStateCache.end()) {
			return false;
		}
		if (now - it->second.last > kStableCacheTtl) {
			s_stableStateCache.erase(it);
			return false;
		}
		outState = it->second.state;
		return true;
	}

	static void UpdateStableUnlockState(RE::FormID shoutID, const ShoutUtils::ShoutUnlockState& state)
	{
		std::scoped_lock lock(s_cacheMutex);
		StableStateCacheEntry entry{};
		entry.state = state;
		entry.last = SteadyClock::now();
		s_stableStateCache[shoutID] = entry;
	}

	static bool ShouldLogUnlockState(RE::FormID shoutID, const ShoutUtils::ShoutUnlockState& state)
	{
		const auto now = SteadyClock::now();
		const std::uint8_t learnedMask = BuildMask(state.words, false);
		const std::uint8_t soulMask = BuildMask(state.words, true);
		std::scoped_lock lock(s_cacheMutex);
		auto& logState = s_unlockLogState[shoutID];
		const bool changed = (logState.lastLearnedCount != state.learnedCountContig) ||
		                     (logState.lastSoulCount != state.soulUnlockedCountContig) ||
		                     (logState.lastFinalCount != state.finalCount) ||
		                     (logState.lastLearnedMask != learnedMask) ||
		                     (logState.lastSoulMask != soulMask) ||
		                     (logState.lastComputeState != state.computeState) ||
		                     (logState.lastPreviousCount != state.previousCount) ||
		                     (logState.lastUsedLastStable != state.usedLastStable);
		if (changed || (now - logState.last > kUnlockLogRateLimit)) {
			logState.last = now;
			logState.lastLearnedCount = state.learnedCountContig;
			logState.lastSoulCount = state.soulUnlockedCountContig;
			logState.lastFinalCount = state.finalCount;
			logState.lastLearnedMask = learnedMask;
			logState.lastSoulMask = soulMask;
			logState.lastComputeState = state.computeState;
			logState.lastPreviousCount = state.previousCount;
			logState.lastUsedLastStable = state.usedLastStable;
			return true;
		}
		return false;
	}

	static bool ShouldLogUnlockCompute(RE::FormID shoutID, const ShoutUtils::ShoutUnlockState& state)
	{
		const auto now = SteadyClock::now();
		std::scoped_lock lock(s_cacheMutex);
		auto& logState = s_unlockComputeLogState[shoutID];
		const bool changed = (logState.lastState != state.computeState) ||
		                     (logState.lastFinalCount != state.finalCount) ||
		                     (logState.lastPreviousCount != state.previousCount) ||
		                     (logState.lastUsedLastStable != state.usedLastStable);
		if (changed || (now - logState.last > kComputeLogRateLimit)) {
			logState.last = now;
			logState.lastState = state.computeState;
			logState.lastFinalCount = state.finalCount;
			logState.lastPreviousCount = state.previousCount;
			logState.lastUsedLastStable = state.usedLastStable;
			return true;
		}
		return false;
	}

	static int GetEngineCount(RE::TESShout* shout, RE::PlayerCharacter* pc)
	{
		if (!shout || !pc) {
			return -1;
		}

		RE::TESShout* currentShout = pc->GetCurrentShout();
		const std::int32_t level = pc->GetCurrentShoutLevel();
		if (currentShout == shout && level >= 0) {
			return std::clamp(level + 1, 0, 3);
		}

		const auto& runtime = pc->GetActorRuntimeData();
		RE::AIProcess* process = runtime.currentProcess;
		if (process && process->high && process->high->currentShout == shout) {
			const auto variation = process->high->currentShoutVariation;
			const auto raw = static_cast<std::uint32_t>(variation);
			if (variation != RE::TESShout::VariationIDs::kNone &&
				variation < RE::TESShout::VariationIDs::kTotal) {
				return static_cast<int>(raw) + 1;
			}
		}

		return -1;
	}
}

namespace ShoutUtils
{
	int GetUnlockedWordCount(RE::TESShout* shout, RE::PlayerCharacter* pc)
	{
		return GetUnlockedWordCountWithMethod(shout, pc).count;
	}

	UnlockResult GetUnlockedWordCountWithMethod(RE::TESShout* shout, RE::PlayerCharacter* pc)
	{
		UnlockResult result{};
		if (!shout) {
			return result;
		}

		const RE::FormID shoutID = shout->GetFormID();
		const auto now = SteadyClock::now();

		{
			std::scoped_lock lock(s_cacheMutex);
			auto it = s_unlockCache.find(shoutID);
			if (it != s_unlockCache.end() && (now - it->second.last <= kUnlockCacheTtl)) {
				// Do not reuse no-player snapshots when we have a valid player context.
				if (!pc || it->second.hasPlayerContext) {
					return it->second.result;
				}
			}
		}

		ShoutUnlockState state{};
		if (pc) {
			state = GetShoutUnlockState(shout, pc);
		} else if (!TryGetCachedUnlockState(shoutID, state)) {
			state = GetShoutUnlockState(shout, nullptr);
		}

		result.count = std::clamp(state.finalCount, 0, 3);
		result.reliable = state.reliable;
		if (state.method == kMethodEngine) {
			result.method = DetectionMethod::Engine;
		} else if (state.method == kMethodHasSpell) {
			result.method = DetectionMethod::HasSpell;
		} else if (state.method == kMethodPapyrus) {
			result.method = DetectionMethod::PapyrusIsWordUnlocked;
		} else if (state.method == kMethodFallback) {
			result.method = DetectionMethod::FallbackSafe;
		} else {
			result.method = DetectionMethod::Unknown;
		}

		{
			std::scoped_lock lock(s_cacheMutex);
			UnlockCacheEntry entry{};
			entry.result = result;
			entry.last = now;
			entry.hasPlayerContext = (pc != nullptr);
			s_unlockCache[shoutID] = entry;
		}

		return result;
	}

	ShoutUnlockState GetShoutUnlockState(RE::TESShout* shout, RE::PlayerCharacter* pc)
	{
		LogVmInitIfNeeded();

		ShoutUnlockState cached{};
		if (shout && TryGetCachedUnlockState(shout->GetFormID(), cached)) {
			// Avoid reusing no-player snapshots once a valid player is available.
			const bool cacheHasNoPlayer = cached.failReason.find(kReasonNoPlayer) != std::string::npos;
			if (!pc || !cacheHasNoPlayer) {
				return cached;
			}
		}

		ShoutUnlockState state{};
		if (!shout) {
			state.method = kMethodFallback;
			state.failReason = kReasonShoutNull;
			state.finalCount = 0;
			state.reliable = false;
			state.computeState = UnlockComputeState::Failed;
			return state;
		}

		const RE::FormID shoutID = shout->GetFormID();
		ShoutUnlockState previousStable{};
		const bool hasPreviousStable = TryGetStableUnlockState(shoutID, previousStable);

		state.shoutID = shoutID;
		state.shoutName = SafeName(shout->GetName());
		state.shoutKnown = shout->GetKnown();
		state.reliable = true;
		state.method = kMethodPapyrus;
		state.failReason = kReasonNone;
		state.computeState = UnlockComputeState::Known;
		state.previousCount = hasPreviousStable ? previousStable.finalCount : -1;
		state.usedLastStable = false;

		bool spellOwned[3] = { false, false, false };
		std::string papyrusFail;
		bool papyrusPending = false;
		bool papyrusFailed = false;
		int wordSlotCount = 0;

		for (std::uint8_t i = 0; i < 3; ++i) {
			const auto& variation = shout->variations[i];
			WordUnlockState& w = state.words[i];
			if (!variation.word) {
				// Null word slot means this shout has no more words.
				w.wordID = 0;
				w.name = "-";
				w.learned = false;
				w.soulUnlocked = false;
				break;
			}

			++wordSlotCount;
			w.wordID = variation.word->GetFormID();
			w.name = SafeName(variation.word->GetName());
			w.learned = variation.word->GetKnown();

			if (pc && variation.spell) {
				spellOwned[i] = pc->HasSpell(variation.spell);
			}

			if (state.reliable) {
				bool soulUnlocked = false;
				std::string failReason;
				if (TryCallIsWordUnlocked(variation.word, state.shoutID, soulUnlocked, failReason)) {
					w.soulUnlocked = soulUnlocked;
				} else {
					w.soulUnlocked = false;
					state.reliable = false;
					papyrusFail = failReason;
					if (failReason == kReasonCallbackPending) {
						papyrusPending = true;
					} else {
						papyrusFailed = true;
					}
				}
			} else {
				w.soulUnlocked = false;
			}
		}

		for (std::uint8_t i = 0; i < wordSlotCount; ++i) {
			if (state.words[i].learned) {
				state.learnedCountContig = static_cast<int>(i) + 1;
			} else {
				break;
			}
		}

		for (std::uint8_t i = 0; i < wordSlotCount; ++i) {
			if (state.words[i].soulUnlocked) {
				state.soulUnlockedCountContig = static_cast<int>(i) + 1;
			} else {
				break;
			}
		}

		if (pc) {
			state.spellOwnedCountContig = 0;
			for (std::uint8_t i = 0; i < wordSlotCount; ++i) {
				if (spellOwned[i]) {
					state.spellOwnedCountContig = static_cast<int>(i) + 1;
				} else {
					break;
				}
			}
		} else {
			state.spellOwnedCountContig = -1;
		}

		state.engineCount = GetEngineCount(shout, pc);

		if (state.engineCount >= 0) {
			state.finalCount = state.engineCount;
			state.method = kMethodEngine;
			state.reliable = true;
			state.computeState = UnlockComputeState::Known;
			state.failReason = kReasonNone;
		} else if (!papyrusPending && !papyrusFailed) {
			state.finalCount = state.soulUnlockedCountContig;
			state.method = kMethodPapyrus;
			state.reliable = true;
			state.computeState = UnlockComputeState::Known;
			state.failReason = kReasonNone;
		} else if (papyrusPending) {
			state.computeState = UnlockComputeState::Pending;
			state.failReason = kReasonCallbackPending;
			if (hasPreviousStable) {
				ShoutUnlockState stableState = previousStable;
				stableState.computeState = UnlockComputeState::Pending;
				stableState.previousCount = state.previousCount;
				stableState.usedLastStable = true;
				stableState.failReason = kReasonCallbackPending;
				state = stableState;
			} else if (state.spellOwnedCountContig > 0) {
				state.finalCount = state.spellOwnedCountContig;
				state.method = kMethodHasSpell;
				state.reliable = false;
			} else {
				state.finalCount = 0;
				state.method = kMethodFallback;
				state.reliable = false;
			}
		} else {
			state.computeState = UnlockComputeState::Failed;
			if (state.spellOwnedCountContig > 0) {
				state.finalCount = state.spellOwnedCountContig;
				state.method = kMethodHasSpell;
			} else {
				state.finalCount = 0;
				state.method = kMethodFallback;
			}
			state.reliable = false;
		}

		const int maxWordCount = std::clamp(wordSlotCount, 0, 3);
		state.finalCount = std::clamp(state.finalCount, 0, maxWordCount);

		if (state.computeState == UnlockComputeState::Failed) {
			if (!papyrusFail.empty()) {
				state.failReason = papyrusFail;
			} else if (state.failReason.empty() || state.failReason == kReasonNone) {
				state.failReason = kReasonDispatchFail;
			}
		} else if (state.computeState == UnlockComputeState::Known) {
			state.failReason = kReasonNone;
		}

		if (!pc) {
			if (state.failReason == kReasonNone) {
				state.failReason = kReasonNoPlayer;
			} else if (state.failReason != kReasonNoPlayer) {
				state.failReason.append("; ");
				state.failReason.append(kReasonNoPlayer);
			}
		}

		if (state.reliable || state.usedLastStable) {
			UpdateStableUnlockState(state.shoutID, state);
		}

		const bool stateHasNoPlayer = state.failReason.find(kReasonNoPlayer) != std::string::npos;
		if (!stateHasNoPlayer) {
			UpdateUnlockStateCache(state.shoutID, state);
		}

		if (IsShoutPipelineDebugEnabled() && ShouldLogUnlockCompute(state.shoutID, state)) {
			const char* methodStr = state.method.empty() ? "-" : state.method.c_str();
			const char* failStr = state.failReason.empty() ? "-" : state.failReason.c_str();
			logger::info("[ShoutUnlockCompute] shout={:08X} state={} count={} prev={} method={} reliable={} usedStable={} reason={}",
				state.shoutID, GetComputeStateName(state.computeState), state.finalCount, state.previousCount,
				methodStr, state.reliable ? "true" : "false", state.usedLastStable ? "true" : "false", failStr);
		}

		return state;
	}

	void LogUnlockState(const char* tag, const char* reason, const ShoutUnlockState& state)
	{
		if (!IsShoutPipelineDebugEnabled()) {
			return;
		}
		if (!ShouldLogUnlockState(state.shoutID, state)) {
			return;
		}

		const char* tagStr = tag ? tag : "-";
		const char* reasonStr = reason ? reason : "-";
		const char* methodStr = state.method.empty() ? "-" : state.method.c_str();
		const char* failStr = state.failReason.empty() ? "-" : state.failReason.c_str();

		logger::info(
			"[ShoutUnlock] tag={} reason={} shout={:08X} name='{}' shoutKnown={} learnedCount={} soulUnlockedCount={} spellOwnedCount={} engineCount={} finalCount={} method={} reliable={} state={} prevCount={} usedStable={} failReason={} | "
			"w0={:08X} name='{}' learned={} soulUnlocked={} w1={:08X} name='{}' learned={} soulUnlocked={} w2={:08X} name='{}' learned={} soulUnlocked={}",
			tagStr, reasonStr,
			state.shoutID,
			state.shoutName.empty() ? "-" : state.shoutName.c_str(),
			state.shoutKnown ? 1 : 0,
			state.learnedCountContig,
			state.soulUnlockedCountContig,
			state.spellOwnedCountContig,
			state.engineCount,
			state.finalCount,
			methodStr,
			state.reliable ? "true" : "false",
			GetComputeStateName(state.computeState),
			state.previousCount,
			state.usedLastStable ? "true" : "false",
			failStr,
			state.words[0].wordID, state.words[0].name.empty() ? "-" : state.words[0].name.c_str(), state.words[0].learned ? 1 : 0, state.words[0].soulUnlocked ? 1 : 0,
			state.words[1].wordID, state.words[1].name.empty() ? "-" : state.words[1].name.c_str(), state.words[1].learned ? 1 : 0, state.words[1].soulUnlocked ? 1 : 0,
			state.words[2].wordID, state.words[2].name.empty() ? "-" : state.words[2].name.c_str(), state.words[2].learned ? 1 : 0, state.words[2].soulUnlocked ? 1 : 0);
	}

	void ClearCache()
	{
		std::scoped_lock lock(s_cacheMutex);
		s_unlockCache.clear();
		s_unlockStateCache.clear();
		s_unlockLogState.clear();
		s_unlockComputeLogState.clear();
		// NOTE: s_stableStateCache intentionally persists across transient resets.
		// It expires via TTL and prevents pending Papyrus results from collapsing to 0.
	}

	void InvalidateCacheForShout(RE::FormID shoutFormID)
	{
		std::scoped_lock lock(s_cacheMutex);
		s_unlockCache.erase(shoutFormID);
		s_unlockStateCache.erase(shoutFormID);
		s_unlockLogState.erase(shoutFormID);
		s_unlockComputeLogState.erase(shoutFormID);
	}

	const char* GetMethodName(DetectionMethod method)
	{
		switch (method) {
		case DetectionMethod::PapyrusIsWordUnlocked:
			return kMethodPapyrus;
		case DetectionMethod::Engine:
			return kMethodEngine;
		case DetectionMethod::HasSpell:
			return kMethodHasSpell;
		case DetectionMethod::FallbackSafe:
			return kMethodFallback;
		default:
			return "Unknown";
		}
	}

	const char* GetComputeStateName(UnlockComputeState state)
	{
		switch (state) {
		case UnlockComputeState::Known:
			return "Known";
		case UnlockComputeState::Pending:
			return "Pending";
		case UnlockComputeState::Failed:
			return "Failed";
		default:
			return "Unknown";
		}
	}

	void GetStageThresholds(float& stage1End, float& stage2End, float& stage3End)
	{
		const float fill = Config::WheelBehavior::ShoutStageFillSecs;
		const float hold1 = Config::WheelBehavior::ShoutStageHoldSecs1;
		const float hold2 = Config::WheelBehavior::ShoutStageHoldSecs2;
		stage1End = fill + hold1;
		stage2End = stage1End + fill + hold2;
		stage3End = stage2End + fill;
	}

	void GetSoundTriggerThresholds(float& sound1At, float& sound2At, float& sound3At)
	{
		float stage1End = 0.0f;
		float stage2End = 0.0f;
		float stage3End = 0.0f;
		GetStageThresholds(stage1End, stage2End, stage3End);
		const float fill = Config::WheelBehavior::ShoutStageFillSecs;
		sound1At = fill;
		sound2At = stage1End + fill;
		sound3At = stage2End + fill;
	}

	float GetMaxEffectiveTime(int unlockedWords)
	{
		float stage1End = 0.0f;
		float stage2End = 0.0f;
		float stage3End = 0.0f;
		GetStageThresholds(stage1End, stage2End, stage3End);

		switch (unlockedWords) {
		case 1:
			return stage1End;
		case 2:
			return stage2End;
		case 3:
			return stage3End;
		default:
			return 0.0f;
		}
	}

	void SetShoutPipelineDebugEnabled(bool enabled)
	{
		s_debugOverride.store(enabled ? 1 : 0);
		if (enabled) {
			s_vmInitLogged.store(false);
		}
	}

	bool IsShoutPipelineDebugEnabled()
	{
		const int overrideState = s_debugOverride.load();
		if (overrideState >= 0) {
			return overrideState != 0;
		}
		return Config::WheelBehavior::ShoutPipelineDebug;
	}

	void DebugDumpShoutPipeline(RE::TESShout* shout, RE::PlayerCharacter* pc, const char* tag, const char* reason)
	{
		const ShoutUnlockState state = GetShoutUnlockState(shout, pc);
		LogUnlockState(tag, reason, state);
	}

	void DebugDumpShoutPipelineByFormID(RE::FormID shoutFormID, RE::PlayerCharacter* pc, const char* tag, const char* reason)
	{
		RE::TESShout* shout = RE::TESForm::LookupByID<RE::TESShout>(shoutFormID);
		const ShoutUnlockState state = GetShoutUnlockState(shout, pc);
		LogUnlockState(tag, reason, state);
	}

	void DebugDumpShoutPipelineForce(RE::TESShout* shout, RE::PlayerCharacter* pc, const char* tag, const char* reason)
	{
		if (!IsShoutPipelineDebugEnabled()) {
			return;
		}
		const ShoutUnlockState state = GetShoutUnlockState(shout, pc);
		LogUnlockState(tag, reason, state);
	}

	void DebugDumpShoutUnlockState(RE::TESShout* shout, RE::PlayerCharacter* pc, const char* tag, const char* reason)
	{
		const ShoutUnlockState state = GetShoutUnlockState(shout, pc);
		LogUnlockState(tag, reason, state);
	}
}
