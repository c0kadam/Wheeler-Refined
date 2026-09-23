#pragma once
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Controls
{
public:
	using KeyId = uint32_t;
	using FunctionPtr = void (*)();

	enum class DispatchResult
	{
		NotHandled,
		HandledPassThrough,
		Consumed
	};

	enum class Action
	{
		None,
		ExitWheel,
		ActivatePrimary,
		ActivateSecondary,
		AddWheel,
		AddEmptyEntry,
		MoveEntryForward,
		MoveEntryBack,
		MoveWheelForward,
		MoveWheelBack,
		ToggleEditHints,
		NextWheel,
		PrevWheel,
		Toggle,
		ToggleIfInInventory,
		ToggleIfNotInInventory,
		PrevItem,
		NextItem,
		ToggleAmmoWheel,
		ToggleAmmoWheelMouse,
		JumpActionHotkeysWheel,
		ReturnToPreviousWheel,
		RefreshActionHotkeysMirror,
		ResetActionHotkeysBridgeLayout
	};

	enum class RebindTarget
	{
		None,
		AmmoWheelMKB,
		AmmoWheelGamepad,
		AmmoWheelMKBModifier,
		AmmoWheelGamepadModifier,
		AmmoWheelMouse
	};

	static void Init();
	static void BindAllInputsFromConfig();

	/// <summary>
	/// Find the action for the corresponding keyID and down/up condition, dispatching the function if found.
	/// </summary>
	/// <param name="key">id of the key input</param>
	/// <param name="isDown">whether the key is being pressed down or being released</param>
	/// <param name="isGamePad">whether the input comes from game pad</param>
	/// <returns>dispatch handling result with pass-through/consume semantics.</returns>
	static DispatchResult Dispatch(KeyId key, bool isDown = true, bool isGamePad = false);
	static Action ResolveAction(KeyId key, bool isDown = true, bool isGamePad = false);

	static bool IsKeyBound(KeyId key);
	static bool IsKeyExclusivelyBound(KeyId key);
	static bool HasBridgeWheelBinding(KeyId key, bool isGamePad = false);
	static void BeginRebind(RebindTarget target);
	static void CancelRebind();
	static bool IsRebindActive();
	static RebindTarget GetRebindTarget();
	static void UpdateRebindTimeout();
	static void PollRebindInput();
	static bool HandleRebindInput(KeyId key, bool isGamePad, bool isMouse);
	static std::string GetKeyNameForMkb(KeyId key);
	static std::string GetKeyNameForGamepad(KeyId key);

	/// <summary>
	/// Check if a specific MKB key is currently held (for modifier checking)
	/// </summary>
	static bool IsMkbKeyHeld(KeyId key);
	/// <summary>
	/// Check if a specific gamepad key is currently held.
	/// </summary>
	static bool IsGamepadKeyHeld(KeyId key);

	/// <summary>
	/// Track key state for ALL keys (called from input hook before bound-key check)
	/// This allows modifier tracking even for unbound keys like Shift/Ctrl
	/// </summary>
	static void TrackKeyState(KeyId key, bool isDown, bool isGamePad);

private:
	struct ToggleBindingCandidate
	{
		KeyId requiredModifier{ 0 };
		FunctionPtr onDown{ nullptr };
		FunctionPtr onUp{ nullptr };
		Action action{ Action::None };
		bool allowNonExclusiveChordFallback{ false };
	};

	struct ModifiedBindingCandidate
	{
		KeyId requiredModifier{ 0 };
		FunctionPtr onDown{ nullptr };
		Action action{ Action::None };
	};

	struct BridgeWheelBindingCandidate
	{
		KeyId requiredModifier{ 0 };
		std::uint32_t wheelNumber{ 0 };
		Action action{ Action::None };
	};

	struct ArmedToggleState
	{
		FunctionPtr onUp{ nullptr };
		DispatchResult releaseResult{ DispatchResult::HandledPassThrough };
		Action action{ Action::None };
		KeyId baseKey{ 0 };
		KeyId requiredModifier{ 0 };
	};

	struct ArmedToggleKey
	{
		KeyId key{ 0 };
		bool isGamePad{ false };

		bool operator==(const ArmedToggleKey&) const = default;
	};

	struct ArmedToggleKeyHash
	{
		std::size_t operator()(const ArmedToggleKey& value) const noexcept
		{
			return (static_cast<std::size_t>(value.key) << 1) ^
			       static_cast<std::size_t>(value.isGamePad ? 1 : 0);
		}
	};

	static void bindInput(KeyId key, FunctionPtr func, Action action = Action::None, bool isDown = true, bool isGamePad = false);
	static void bindModifiedInput(KeyId key, KeyId requiredModifier, FunctionPtr func, Action action = Action::None, bool isGamePad = false);
	static void bindBridgeWheelInput(KeyId key, KeyId requiredModifier, std::uint32_t wheelNumber, bool isGamePad);
	static void bindToggle(KeyId key, KeyId requiredModifier, FunctionPtr onDown, FunctionPtr onUp, Action action, bool isGamePad, bool allowNonExclusiveChordFallback = false);
	static bool IsModifierHeld(KeyId key, bool isGamePad);
	static bool ToggleBindingsAllowNormalFallback(const std::vector<ToggleBindingCandidate>& candidates);

	static inline std::unordered_map<KeyId, FunctionPtr> _keyFunctionMapDown;
	static inline std::unordered_map<KeyId, FunctionPtr> _keyFunctionMapUp;
	static inline std::unordered_map<KeyId, FunctionPtr> _keyFunctionMapDownGamepad;
	static inline std::unordered_map<KeyId, FunctionPtr> _keyFunctionMapUpGamepad;

	static inline std::unordered_map<KeyId, Action> _keyActionMapDown;
	static inline std::unordered_map<KeyId, Action> _keyActionMapUp;
	static inline std::unordered_map<KeyId, Action> _keyActionMapDownGamepad;
	static inline std::unordered_map<KeyId, Action> _keyActionMapUpGamepad;

	static inline std::unordered_map<KeyId, std::vector<ToggleBindingCandidate>> _toggleBindingsMkb;
	static inline std::unordered_map<KeyId, std::vector<ToggleBindingCandidate>> _toggleBindingsGamepad;
	static inline std::unordered_map<KeyId, std::vector<ModifiedBindingCandidate>> _modifiedBindingsMkb;
	static inline std::unordered_map<KeyId, std::vector<ModifiedBindingCandidate>> _modifiedBindingsGamepad;
	static inline std::unordered_map<KeyId, std::vector<BridgeWheelBindingCandidate>> _bridgeWheelBindingsMkb;
	static inline std::unordered_map<KeyId, std::vector<BridgeWheelBindingCandidate>> _bridgeWheelBindingsGamepad;
	static inline std::unordered_map<ArmedToggleKey, ArmedToggleState, ArmedToggleKeyHash> _armedToggleBindings;
	static inline std::unordered_map<ArmedToggleKey, std::uint64_t, ArmedToggleKeyHash> _keyStateGenerations;
	static inline std::uint64_t _bindingGeneration{ 1 };

	static inline std::mutex _lock;
};
