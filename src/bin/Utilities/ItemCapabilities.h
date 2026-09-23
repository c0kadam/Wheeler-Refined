#pragma once

#include "RE/A/Actor.h"
#include "RE/T/TESForm.h"

namespace ItemCapabilities
{
	struct Flags
	{
		bool playable = false;
		bool inInventory = false;
		bool canBeCarried = true;
		bool equippable = false;
		bool hasEquipSlot = false;
		bool consumable = false;
		bool readable = false;
		bool scriptBacked = false;
		bool unsafePassive = false;
	};

	bool IsPlayable(RE::TESForm* form);
	bool IsInInventory(RE::Actor* actor, RE::TESForm* form, int* outCount = nullptr);
	bool CanBeCarried(RE::TESForm* form);
	bool HasEquipSlot(RE::TESForm* form);
	bool IsEquippable(RE::TESForm* form);
	bool IsConsumable(RE::TESForm* form);
	bool IsReadable(RE::TESForm* form);
	bool IsUnsafePassive(RE::TESForm* form);
	Flags Evaluate(RE::TESForm* form, RE::Actor* actor);
}
