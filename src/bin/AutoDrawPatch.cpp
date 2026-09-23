#include "AutoDrawPatch.h"

/**
 * AutoDrawPatch - Dynamic memory patching to prevent Skyrim's automatic weapon draw
 * 
 * Based on technique from StopAutomaticWeaponDrawNG by FlyingParticle/jolly-gopher
 * https://github.com/jolly-gopher/StopAutomaticWeaponDrawNG
 * 
 * The mod works by patching specific memory locations in Skyrim's code that handle
 * automatic weapon drawing when mouse buttons or gamepad triggers are pressed.
 * We insert a `return` (0xC3) instruction to make the functions exit early.
 * 
 * This implementation makes the patches toggleable at runtime by saving the original
 * bytes and restoring them when the setting is changed.
 */

namespace AutoDrawPatch
{
	struct PatchLocation
	{
		std::uintptr_t address = 0;
		std::uint8_t originalByte = 0;
		bool patched = false;
	};

	// The three functions that handle auto-draw
	static PatchLocation s_drawBothHands;  // Draws both hands on input
	static PatchLocation s_drawRightHand;  // Draws right hand on input  
	static PatchLocation s_drawLeftHand;   // Draws left hand on input

	static bool s_initialized = false;
	static bool s_preventingAutoDraw = false;
	static std::mutex s_mutex;  // Thread safety for runtime changes

	void Init()
	{
		std::lock_guard<std::mutex> lock(s_mutex);

		if (s_initialized) {
			logger::warn("[AutoDrawPatch] Already initialized, skipping");
			return;
		}

		// Get addresses using REL IDs (automatically handles SE/AE version differences)
		// These offsets are from StopAutomaticWeaponDrawNG
		// The hex values are offsets into the instructions we want to overwrite
		s_drawBothHands.address = REL::RelocationID(41345, 42419).address() + 0x90;  // SE/AE
		s_drawRightHand.address = REL::RelocationID(41364, 42438).address() + 0x4f;  // SE/AE
		s_drawLeftHand.address = REL::RelocationID(41361, 42435).address() + 0x4f;   // SE/AE

		// Save original bytes so we can restore them later
		s_drawBothHands.originalByte = *reinterpret_cast<std::uint8_t*>(s_drawBothHands.address);
		s_drawRightHand.originalByte = *reinterpret_cast<std::uint8_t*>(s_drawRightHand.address);
		s_drawLeftHand.originalByte = *reinterpret_cast<std::uint8_t*>(s_drawLeftHand.address);

		s_initialized = true;

		logger::info("[AutoDrawPatch] Initialized - drawBothHands={:x}, drawRightHand={:x}, drawLeftHand={:x}",
			s_drawBothHands.address, s_drawRightHand.address, s_drawLeftHand.address);
		logger::info("[AutoDrawPatch] Original bytes: both=0x{:02X}, right=0x{:02X}, left=0x{:02X}",
			s_drawBothHands.originalByte, s_drawRightHand.originalByte, s_drawLeftHand.originalByte);
	}

	void SetEnabled(bool preventAutoDraw)
	{
		std::lock_guard<std::mutex> lock(s_mutex);

		if (!s_initialized) {
			logger::warn("[AutoDrawPatch] Cannot set enabled state - not initialized");
			return;
		}

		if (s_preventingAutoDraw == preventAutoDraw) {
			// Already in the desired state
			return;
		}

		constexpr std::uint8_t returnInstruction = 0xC3;  // x64 return instruction

		if (preventAutoDraw) {
			// Apply patches - insert return instructions to skip auto-draw logic
			REL::safe_write(s_drawBothHands.address, returnInstruction);
			REL::safe_write(s_drawRightHand.address, returnInstruction);
			REL::safe_write(s_drawLeftHand.address, returnInstruction);

			s_drawBothHands.patched = true;
			s_drawRightHand.patched = true;
			s_drawLeftHand.patched = true;

			logger::info("[AutoDrawPatch] Patches APPLIED - auto-draw is now PREVENTED");
		} else {
			// Restore original bytes - allow normal auto-draw behavior
			REL::safe_write(s_drawBothHands.address, s_drawBothHands.originalByte);
			REL::safe_write(s_drawRightHand.address, s_drawRightHand.originalByte);
			REL::safe_write(s_drawLeftHand.address, s_drawLeftHand.originalByte);

			s_drawBothHands.patched = false;
			s_drawRightHand.patched = false;
			s_drawLeftHand.patched = false;

			logger::info("[AutoDrawPatch] Patches REMOVED - auto-draw is now ALLOWED");
		}

		s_preventingAutoDraw = preventAutoDraw;
	}

	bool IsPreventingAutoDraw()
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		return s_preventingAutoDraw;
	}
}
