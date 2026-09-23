#pragma once
#include <cstdint>
#include <optional>

enum class TransformState : std::uint8_t
{
	Human = 0,
	Werewolf = 1,
	VampireLord = 2,
	Generic = 3,
	Lich = 4
};

namespace RE
{
	class SpellItem;
	class TESShout;
	class TESObjectMISC;
	class TESObjectWEAP;
}

class TransformWheelManager
{
public:
	static void Update();
	static void Reset();
	static bool IsTransformWheelIndex(int index);
	static bool IsPlayerHuman();
	static TransformState GetPlayerTransformState();
	static std::optional<int> GetSavedHumanWheelIndex();
	// True when transformed navigation should be restricted to transform wheels only.
	static bool ShouldRestrictNavigationToTransformWheels();
	// True when spell activation should be blocked by active transform guard.
	static bool IsSpellActivationBlocked(RE::SpellItem* spell, const char* source = nullptr);
	// True when a major transform-entry spell should be shown as inaccessible in the current transform state.
	static bool IsMajorTransformSpellBlockedForCurrentState(RE::SpellItem* spell);
	// True when confirmed Vampire Lord melee mode should hide a VL hand-cast spell from view.
	static bool IsVampireLordSpellHiddenForCurrentMode(RE::SpellItem* spell);
	// True when shout activation should be blocked by active transform guard.
	static bool IsShoutActivationBlocked(RE::TESShout* shout, const char* source = nullptr);
	// True when misc activation should be blocked by active lich loadout guard.
	static bool ShouldBlockMiscActivation(RE::TESObjectMISC* miscItem, const char* source = nullptr);
	// True when current transform state is lich.
	static bool IsLichActive();
	// True when lich config requests legacy full weapon activation blocking.
	static bool ShouldBlockWeaponActivation();
	// True when lich config blocks this specific weapon. Staffs are exempt from HideWeapons.
	static bool ShouldBlockWeaponActivation(RE::TESObjectWEAP* weapon);
	// True when a weapon should be rendered inaccessible by a lich-only loadout guard.
	static bool ShouldDimWeaponActivation(RE::TESObjectWEAP* weapon);
	// True only for confirmed lich form staff activation when the lich staff guard is enabled.
	static bool ShouldBlockStaffActivation(RE::TESObjectWEAP* weapon, const char* source = nullptr);
	// True only for confirmed lich form spells that should fall back to normal equip instead of direct-cast.
	static bool ShouldSuppressLichDirectCast(RE::SpellItem* spell, const char* source = nullptr);
	// True when lich config requests gear (armor/light/shield) activation blocking.
	static bool ShouldBlockGearActivation();
	// True when gear should be rendered inaccessible by a lich-only loadout guard.
	static bool ShouldDimGearActivation();
	// True when misc should be rendered inaccessible by a lich-only loadout guard.
	static bool ShouldDimMiscActivation(RE::TESObjectMISC* miscItem);
	
	/// Call when the Wheeler opens to start late-refresh polling for transform spells.
	/// This catches spells/powers that are added slightly after the wheel opens.
	static void OnWheelOpened();
};
