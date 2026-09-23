// ShoutUtils.h
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESShout.h>

namespace ShoutUtils
{
	/// Detection method used for unlocked word count.
	enum class DetectionMethod : std::uint8_t
	{
		Unknown,
		PapyrusIsWordUnlocked,
		Engine,
		HasSpell,
		FallbackSafe
	};

	enum class UnlockComputeState : std::uint8_t
	{
		Known,
		Pending,
		Failed
	};

	/// Result struct for diagnostics and reliability.
	struct UnlockResult
	{
		int count = 0;                              // 0-3 unlocked words
		DetectionMethod method = DetectionMethod::Unknown;
		bool reliable = false;                      // true only when soul-unlocked state is reliable
	};

	struct WordUnlockState
	{
		bool learned = false;
		bool soulUnlocked = false;
		RE::FormID wordID = 0;
		std::string name;
	};

	struct ShoutUnlockState
	{
		RE::FormID shoutID = 0;
		std::string shoutName;
		bool shoutKnown = false;

		int learnedCountContig = 0;
		int soulUnlockedCountContig = 0;
		int spellOwnedCountContig = -1;
		int engineCount = -1;
		int finalCount = 0;

		bool reliable = false;
		std::string method;      // "PapyrusIsWordUnlocked", "HasSpell", "Engine", "FallbackSafe"
		std::string failReason;  // why reliable=false or why method fell back
		UnlockComputeState computeState = UnlockComputeState::Known;
		int previousCount = -1;
		bool usedLastStable = false;

		WordUnlockState words[3]{};
	};

	/// Get unlocked word count (0-3) for a shout.
	/// Uses finalCount derived from ShoutUnlockState.
	int GetUnlockedWordCount(RE::TESShout* shout, RE::PlayerCharacter* pc = nullptr);

	/// Same as above, but returns diagnostics.
	UnlockResult GetUnlockedWordCountWithMethod(RE::TESShout* shout, RE::PlayerCharacter* pc = nullptr);

	/// Get full unlock state (learned vs soul-unlocked per word).
	ShoutUnlockState GetShoutUnlockState(RE::TESShout* shout, RE::PlayerCharacter* pc = nullptr);

	/// Log unlock state (rate-limited per shout).
	void LogUnlockState(const char* tag, const char* reason, const ShoutUnlockState& state);

	/// Clear the cache (call when wheel closes, or when hovered shout changes).
	void ClearCache();

	/// Invalidate cache for a specific shout FormID.
	void InvalidateCacheForShout(RE::FormID shoutFormID);

	/// Debug: method name.
	const char* GetMethodName(DetectionMethod method);
	const char* GetComputeStateName(UnlockComputeState state);

	// ========= Stage Thresholds (single source of truth) =========
	void GetStageThresholds(float& stage1End, float& stage2End, float& stage3End);
	void GetSoundTriggerThresholds(float& sound1At, float& sound2At, float& sound3At);
	float GetMaxEffectiveTime(int unlockedWords);

	// ========= Shout pipeline debugging =========
	/// Enable or disable shout pipeline logging at runtime (wire this to your config if desired).
	void SetShoutPipelineDebugEnabled(bool enabled);

	/// Query current debug enabled state.
	bool IsShoutPipelineDebugEnabled();

	/// Dump everything relevant to word unlock detection and related pipeline state.
	/// tag: short label like "HoverChanged", "BeforeStageCompute", "AfterWheelClose"
	/// reason: optional extra string, can be nullptr
	void DebugDumpShoutPipeline(RE::TESShout* shout, RE::PlayerCharacter* pc, const char* tag, const char* reason = nullptr);

	/// Same dump by FormID.
	void DebugDumpShoutPipelineByFormID(RE::FormID shoutFormID, RE::PlayerCharacter* pc, const char* tag, const char* reason = nullptr);

	/// Force dump, bypasses rate limiting for that shout.
	void DebugDumpShoutPipelineForce(RE::TESShout* shout, RE::PlayerCharacter* pc, const char* tag, const char* reason = nullptr);

	/// Dump the shout unlock state (word-learned vs soul-unlocked) with rate limiting.
	void DebugDumpShoutUnlockState(RE::TESShout* shout, RE::PlayerCharacter* pc, const char* tag, const char* reason = nullptr);
}

#ifndef SHOUTPIPE
#define SHOUTPIPE(...)                            \
	do {                                          \
		if (ShoutUtils::IsShoutPipelineDebugEnabled()) { \
			logger::info("[ShoutPipe] " __VA_ARGS__);    \
		}                                         \
	} while (0)
#endif
