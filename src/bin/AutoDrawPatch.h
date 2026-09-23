#pragma once

/**
 * AutoDrawPatch - Dynamic memory patching to prevent Skyrim's automatic weapon draw
 * 
 * Based on technique from StopAutomaticWeaponDrawNG by FlyingParticle/jolly-gopher
 * https://github.com/jolly-gopher/StopAutomaticWeaponDrawNG
 * 
 * Patches 3 engine functions that trigger auto-draw when mouse buttons or
 * gamepad triggers are pressed while weapons are sheathed.
 */

namespace AutoDrawPatch
{
	/**
	 * Initialize patch locations and save original bytes.
	 * Call once during plugin load, after SKSE is initialized.
	 */
	void Init();

	/**
	 * Enable or disable the auto-draw prevention patches.
	 * 
	 * @param preventAutoDraw If true, patches are applied (weapons won't auto-draw).
	 *                        If false, original bytes are restored (normal behavior).
	 * 
	 * This is inverted from the AutoDrawOnUse config setting:
	 * - AutoDrawOnUse = true  -> call SetEnabled(false) [allow auto-draw]
	 * - AutoDrawOnUse = false -> call SetEnabled(true)  [prevent auto-draw]
	 */
	void SetEnabled(bool preventAutoDraw);

	/**
	 * Check if auto-draw prevention is currently active.
	 * @return true if patches are applied and auto-draw is being prevented.
	 */
	bool IsPreventingAutoDraw();
}
