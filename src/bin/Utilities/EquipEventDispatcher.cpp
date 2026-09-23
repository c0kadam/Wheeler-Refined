#include "EquipEventDispatcher.h"

namespace EquipEventDispatcher
{
	void SendEquipEvent(RE::Actor* a_actor, RE::FormID a_baseObject, bool a_equipped, std::uint16_t a_uniqueID)
	{
		if (!a_actor) {
			return;
		}

		auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
		if (!eventSource) {
			return;
		}

		RE::TESEquipEvent event;
		event.actor = RE::NiPointer<RE::TESObjectREFR>(a_actor);
		event.baseObject = a_baseObject;
		event.originalRefr = 0;
		event.uniqueID = a_uniqueID;
		event.equipped = a_equipped;

		eventSource->SendEvent(&event);
	}

	void SendPlayerEquipEvent(RE::FormID a_baseObject, bool a_equipped, std::uint16_t a_uniqueID)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		SendEquipEvent(player, a_baseObject, a_equipped, a_uniqueID);
	}
}
