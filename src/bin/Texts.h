#pragma once
class Texts
{
public:
	enum class TextType
	{
		AlchemyDynamicIDConsumptionWarning,
		NoWheelPresent,
		EditHintTitle,
		EditHintDeviceHeaderMkb,
		EditHintDeviceHeaderGamepad,
		EditHintActionUsePlaceItem,
		EditHintActionRemoveItemWheel,
		EditHintActionAddEmptySlot,
		EditHintActionAddWheel,
		EditHintActionNextWheel,
		EditHintActionPreviousWheel,
		EditHintActionMoveSlotForward,
		EditHintActionMoveSlotBack,
		EditHintActionMoveWheelForward,
		EditHintActionMoveWheelBack,
		EditHintActionSettingsDMenu,
		EditHintActionExitWheel,
		EditHintNavToggleMkb,
		EditHintNavToggleGamepad,
		
		// Notifications
		WheelBehaviorDefaultsSaved,
		WheelBehaviorRestoredToDefaults,
		NoWheelBehaviorDefaultsSaved,
		PressKeyToBindAmmoWheel,
		PressGamepadButtonToBindAmmoWheel,
		KeybindCaptureCancelled,
		AmmoWheelKeybindsReset,
		PressKeyToSetModifier,
		KeyboardModifierCleared,
		PressGamepadButtonToSetModifier,
		GamepadModifierCleared,
		ClickMouseButtonToBind,
		MouseToggleCleared,
		AmmoWheelFactoryDefaultsRestored,
		AmmoWheelFactoryDefaultsFailed,
		InsufficientMagickaForInstantCast,
		PoisonAlreadyApplied,
		PoisonSafeResolutionFailed,
		
		Total
	};

	static void LoadTranslations();
	static const char* GetText(TextType a_textType);

private:
#define MAP_ENTRY(textTypeName, defaultText) \
	{                              \
		TextType::textTypeName, defaultText\
	}

	static inline std::unordered_map<TextType, std::string> _textData = {
		MAP_ENTRY(AlchemyDynamicIDConsumptionWarning, ""),
		MAP_ENTRY(NoWheelPresent, ""),
		MAP_ENTRY(EditHintTitle, "Edit Mode Controls"),
		MAP_ENTRY(EditHintDeviceHeaderMkb, "KB/M"),
		MAP_ENTRY(EditHintDeviceHeaderGamepad, "Gamepad"),
		MAP_ENTRY(EditHintActionUsePlaceItem, "Use / Place Item"),
		MAP_ENTRY(EditHintActionRemoveItemWheel, "Remove Item / Wheel"),
		MAP_ENTRY(EditHintActionAddEmptySlot, "Add Empty Slot"),
		MAP_ENTRY(EditHintActionAddWheel, "Add Wheel"),
		MAP_ENTRY(EditHintActionNextWheel, "Next Wheel"),
		MAP_ENTRY(EditHintActionPreviousWheel, "Previous Wheel"),
		MAP_ENTRY(EditHintActionMoveSlotForward, "Move Slot Forward"),
		MAP_ENTRY(EditHintActionMoveSlotBack, "Move Slot Back"),
		MAP_ENTRY(EditHintActionMoveWheelForward, "Move Wheel Forward"),
		MAP_ENTRY(EditHintActionMoveWheelBack, "Move Wheel Back"),
		MAP_ENTRY(EditHintActionSettingsDMenu, "Settings (dMenu)"),
		MAP_ENTRY(EditHintActionExitWheel, "Exit Wheel"),
		MAP_ENTRY(EditHintNavToggleMkb, "H for keybind hints"),
		MAP_ENTRY(EditHintNavToggleGamepad, "Left Stick Button for keybind hints"),
		MAP_ENTRY(WheelBehaviorDefaultsSaved, "Wheeler: Wheel Behavior defaults saved."),
		MAP_ENTRY(WheelBehaviorRestoredToDefaults, "Wheeler: Wheel Behavior restored to defaults."),
		MAP_ENTRY(NoWheelBehaviorDefaultsSaved, "Wheeler: Failed to restore Wheel Behavior factory defaults."),
		MAP_ENTRY(PressKeyToBindAmmoWheel, "Wheeler: press a key to bind Ammo Wheel."),
		MAP_ENTRY(PressGamepadButtonToBindAmmoWheel, "Wheeler: press a gamepad button to bind Ammo Wheel."),
		MAP_ENTRY(KeybindCaptureCancelled, "Wheeler: keybind capture cancelled."),
		MAP_ENTRY(AmmoWheelKeybindsReset, "Wheeler: Ammo Wheel keybinds reset."),
		MAP_ENTRY(PressKeyToSetModifier, "Wheeler: press a key to set as modifier (ESC to cancel)."),
		MAP_ENTRY(KeyboardModifierCleared, "Wheeler: Keyboard modifier cleared."),
		MAP_ENTRY(PressGamepadButtonToSetModifier, "Wheeler: press a gamepad button to set as modifier."),
		MAP_ENTRY(GamepadModifierCleared, "Wheeler: Gamepad modifier cleared."),
		MAP_ENTRY(ClickMouseButtonToBind, "Wheeler: click a mouse button to bind."),
		MAP_ENTRY(MouseToggleCleared, "Wheeler: Mouse toggle cleared."),
		MAP_ENTRY(AmmoWheelFactoryDefaultsRestored, "Wheeler: Ammo Wheel restored to factory defaults."),
		MAP_ENTRY(AmmoWheelFactoryDefaultsFailed, "Wheeler: Failed to restore Ammo Wheel factory defaults."),
		MAP_ENTRY(InsufficientMagickaForInstantCast, "Not enough magicka for instant cast."),
		MAP_ENTRY(PoisonAlreadyApplied, "The current weapon is already poisoned."),
		MAP_ENTRY(PoisonSafeResolutionFailed, "Wheeler: Unable to safely apply poison to the equipped weapon."),
		MAP_ENTRY(PoisonSafeResolutionFailed, "Wheeler: Unable to safely apply poison to the equipped weapon.")
	};
};
