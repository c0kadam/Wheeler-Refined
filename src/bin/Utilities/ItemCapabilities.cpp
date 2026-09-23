#include "ItemCapabilities.h"

#include "RE/A/AlchemyItem.h"
#include "RE/T/TESObjectARMO.h"
#include "RE/T/TESObjectBOOK.h"
#include "RE/T/TESObjectLIGH.h"
#include "RE/T/TESObjectWEAP.h"
#include "RE/T/TESObjectMISC.h"
#include "RE/I/IngredientItem.h"

namespace ItemCapabilities
{
	bool IsPlayable(RE::TESForm* form)
	{
		if (!form) {
			return false;
		}
		if (form->IsBoundObject()) {
			return form->GetPlayable();
		}
		return true;
	}

	bool IsInInventory(RE::Actor* actor, RE::TESForm* form, int* outCount)
	{
		if (outCount) {
			*outCount = 0;
		}
		if (!actor || !form) {
			return false;
		}
		auto* boundObj = form->As<RE::TESBoundObject>();
		if (!boundObj) {
			return false;
		}
		auto counts = actor->GetInventoryCounts();
		auto it = counts.find(boundObj);
		if (it == counts.end()) {
			return false;
		}
		if (outCount) {
			*outCount = it->second;
		}
		return it->second > 0;
	}

	bool CanBeCarried(RE::TESForm* form)
	{
		if (auto* light = form ? form->As<RE::TESObjectLIGH>() : nullptr) {
			return light->CanBeCarried();
		}
		return true;
	}

	bool HasEquipSlot(RE::TESForm* form)
	{
		if (!form) {
			return false;
		}
		if (auto* weap = form->As<RE::TESObjectWEAP>()) {
			return weap->GetEquipSlot() != nullptr;
		}
		if (auto* armor = form->As<RE::TESObjectARMO>()) {
			return armor->GetEquipSlot() != nullptr;
		}
		if (auto* light = form->As<RE::TESObjectLIGH>()) {
			return light->GetEquipSlot() != nullptr;
		}
		return false;
	}

	bool IsEquippable(RE::TESForm* form)
	{
		if (!form) {
			return false;
		}
		switch (form->GetFormType()) {
		case RE::FormType::Weapon:
		case RE::FormType::Armor:
		case RE::FormType::Ammo:
		case RE::FormType::Light:
		case RE::FormType::Scroll:
		case RE::FormType::Spell:
		case RE::FormType::Shout:
			return true;
		default:
			break;
		}
		return HasEquipSlot(form);
	}

	bool IsConsumable(RE::TESForm* form)
	{
		if (!form) {
			return false;
		}
		if (form->As<RE::AlchemyItem>()) {
			return true;
		}
		if (form->As<RE::IngredientItem>()) {
			return true;
		}
		return false;
	}

	bool IsReadable(RE::TESForm* form)
	{
		if (!form) {
			return false;
		}
		if (form->As<RE::TESObjectBOOK>()) {
			return true;
		}
		if (form->GetFormType() == RE::FormType::Note) {
			return true;
		}
		return false;
	}

	bool IsUnsafePassive(RE::TESForm* form)
	{
		if (!form) {
			return true;
		}
		switch (form->GetFormType()) {
		case RE::FormType::Ingredient:
		case RE::FormType::KeyMaster:
		case RE::FormType::Apparatus:
		case RE::FormType::SoulGem:
		case RE::FormType::LeveledItem:
		case RE::FormType::Projectile:
		case RE::FormType::Hazard:
		case RE::FormType::ConstructibleObject:
			return true;
		default:
			break;
		}
		if (form->IsBoundObject() && !form->GetPlayable()) {
			return true;
		}
		return false;
	}

	Flags Evaluate(RE::TESForm* form, RE::Actor* actor)
	{
		Flags caps{};
		caps.playable = IsPlayable(form);
		caps.inInventory = IsInInventory(actor, form, nullptr);
		caps.canBeCarried = CanBeCarried(form);
		caps.hasEquipSlot = HasEquipSlot(form);
		caps.equippable = IsEquippable(form);
		caps.consumable = IsConsumable(form);
		caps.readable = IsReadable(form);
		caps.scriptBacked = form ? form->HasVMAD() : false;
		caps.unsafePassive = IsUnsafePassive(form);
		return caps;
	}
}
