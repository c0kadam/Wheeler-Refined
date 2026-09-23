#pragma once

namespace ActorVirtualCompat
{
	// CommonLib 6.7's TESObjectREFR declaration inserts an extra virtual before
	// Actor::DrawWeaponMagicHands. Call the engine ABI slot directly so flat
	// Skyrim retains the known-good 0xA6 behavior. VR's table has the historical
	// extra AttachWeapon virtual and therefore uses 0xA7.
	inline void DrawWeaponMagicHands(RE::Actor* a_actor, bool a_draw)
	{
		if (!a_actor) {
			return;
		}

		REL::RelocateVirtual<decltype(&RE::Actor::DrawWeaponMagicHands)>(
			0xA6,
			0xA7,
			a_actor,
			a_draw);
	}
}
