#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct ActionHotkeysInjectedSlot
{
	std::string sourceTag;
	std::string displayName;
	std::uint32_t scanCode = 0;
	std::uint32_t modifier = 0;
	std::string iconPath;
	std::uint32_t iconTintARGB = 0xFFFFFFFF;
	int wheelNumber = 0;  // 1-based; 0 = auto
	int entryIndex = -1;  // 0-based; -1 = auto
};

class ActionHotkeysBridge
{
public:
	static void Init();
	static void Update();
	static void Reset();
	static void RequestRefresh(bool a_forceImmediate = false);

	static bool IsBridgeWheelTag(std::string_view a_tag);
	static bool IsBridgeWheelIndex(int a_wheelIndex);
	static bool IsReadOnlyWheelTag(std::string_view a_tag);
	static bool IsNavigationRestrictedToBridgeWheels();
	static void OnWheelClosed();

	static bool JumpToWheel(std::uint32_t a_wheelNumber);
	static bool ReturnToPreviousWheel();
	static std::optional<int> GetPreviousWheelIndex();
	static bool ResetPersistedLayout(bool a_forceImmediateRefresh = true);
	static void PersistCurrentLayout();
	static bool UpsertInjectedSlot(const ActionHotkeysInjectedSlot& a_slot);
	static bool RemoveInjectedSlot(std::string_view a_sourceTag);
	static void ClearInjectedSlots();

	static std::optional<int> GetWheelIndex(std::uint32_t a_oneBasedWheelNumber);
};
