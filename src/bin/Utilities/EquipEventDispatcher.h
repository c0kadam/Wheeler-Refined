#pragma once

#include <RE/Skyrim.h>

namespace EquipEventDispatcher
{
	/**
	 * Dispatches a TESEquipEvent to the game's event system.
	 * This fires the same event that triggers Papyrus OnObjectEquipped on quest aliases.
	 * 
	 * @param a_actor The actor performing the equip (usually the player)
	 * @param a_baseObject The FormID of the base object being equipped
	 * @param a_equipped True for equip, false for unequip
	 * @param a_uniqueID Optional uniqueID for stack-specific events (0 when unknown)
	 */
	void SendEquipEvent(RE::Actor* a_actor, RE::FormID a_baseObject, bool a_equipped, std::uint16_t a_uniqueID = 0);

	/**
	 * Convenience function: sends an equip event for the player character.
	 * 
	 * @param a_baseObject The FormID of the base object being equipped
	 * @param a_equipped True for equip, false for unequip
	 * @param a_uniqueID Optional uniqueID for stack-specific events (0 when unknown)
	 */
	void SendPlayerEquipEvent(RE::FormID a_baseObject, bool a_equipped, std::uint16_t a_uniqueID = 0);
}
