#include "I4IconResolver.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

#include "I4Availability.h"
#include "I4Log.h"
#include "bin/Config.h"

namespace I4Integration
{
	namespace
	{
		constexpr const char* kDefaultIconSource = "skyui/icons_item_psychosteve.swf";

		enum class ArmorWeightClass : std::int32_t
		{
			Light = 0,
			Heavy = 1,
			None = 2,
			Clothing = 3,
			Jewelry = 4
		};

		enum class EquipType : std::int32_t
		{
			Head = 0,
			Hair = 1,
			LongHair = 2,
			Body = 3,
			Forearms = 4,
			Hands = 5,
			Shield = 6,
			Calves = 7,
			Feet = 8,
			Circlet = 9,
			Amulet = 10,
			Ears = 11,
			Ring = 12,
			Tail = 13
		};

		enum class WeaponType : std::int32_t
		{
			Melee = 0,
			Sword = 1,
			Dagger = 2,
			WarAxe = 3,
			Mace = 4,
			Greatsword = 5,
			Battleaxe = 6,
			Warhammer = 7,
			Bow = 8,
			Crossbow = 9,
			Staff = 10,
			Pickaxe = 11,
			WoodAxe = 12
		};

		enum class AmmoType : std::int32_t
		{
			Arrow = 0,
			Bolt = 1
		};

		enum class PotionType : std::int32_t
		{
			Health = 0,
			HealRate = 1,
			HealRateMult = 2,
			Magicka = 3,
			MagickaRate = 4,
			MagickaRateMult = 5,
			Stamina = 6,
			StaminaRate = 7,
			StaminaRateMult = 8,
			FireResist = 9,
			ElectricResist = 10,
			FrostResist = 11,
			Potion = 12,
			Drink = 13,
			Food = 14,
			Poison = 15
		};

		enum class BookType : std::int32_t
		{
			SpellTome = 0,
			Note = 1,
			Recipe = 2
		};

		enum class MiscType : std::int32_t
		{
			Gem = 0,
			DragonClaw = 1,
			Artifact = 2,
			Leather = 3,
			LeatherStrips = 4,
			Hide = 5,
			Remains = 6,
			Ingot = 7,
			Tool = 8,
			ChildrensClothes = 9,
			Toy = 10,
			Firewood = 11,
			HousePart = 18,
			Clutter = 19,
			Lockpick = 20,
			Gold = 21
		};

		std::string NormalizeString(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
				if (c == '/') {
					return '\\';
				}
				return static_cast<char>(std::tolower(c));
			});
			return value;
		}

		void SetNumber(RE::GFxValue& obj, const char* key, std::int32_t number)
		{
			obj.SetMember(key, static_cast<double>(number));
		}

		void SetNumber(RE::GFxValue& obj, const char* key, float number)
		{
			obj.SetMember(key, static_cast<double>(number));
		}

		void SetString(RE::GFxValue& obj, const char* key, const char* value)
		{
			if (value && value[0]) {
				obj.SetMember(key, value);
			}
		}

		void SetColorRGB(RE::GFxValue& obj, std::uint32_t rgb)
		{
			obj.SetMember("iconColor", static_cast<double>(rgb & 0x00FFFFFF));
		}

		void SetIconDefaults(RE::GFxValue& obj, const char* label, std::optional<std::uint32_t> colorRGB = std::nullopt)
		{
			obj.SetMember("iconSource", kDefaultIconSource);
			obj.SetMember("iconLabel", label);
			if (colorRGB.has_value()) {
				SetColorRGB(obj, colorRGB.value());
			}
		}

		const RE::Effect* GetPrimaryEffect(const RE::MagicItem* magicItem)
		{
			if (!magicItem) {
				return nullptr;
			}
			for (const auto* effect : magicItem->effects) {
				if (effect && effect->baseEffect) {
					return effect;
				}
			}
			return nullptr;
		}

		void BuildKeywordsObject(RE::GFxMovieView* movie, RE::GFxValue& outKeywords, const RE::BGSKeywordForm* keywordForm)
		{
			movie->CreateObject(&outKeywords);
			if (!keywordForm) {
				return;
			}
			for (std::uint32_t i = 0; i < keywordForm->GetNumKeywords(); ++i) {
				const auto keyword = keywordForm->GetKeywordAt(i).value_or(nullptr);
				if (!keyword) {
					continue;
				}
				const auto editorID = keyword->GetFormEditorID();
				if (!editorID || !editorID[0]) {
					continue;
				}
				outKeywords.SetMember(editorID, true);
			}
		}

		void BuildEffectKeywordsObject(RE::GFxMovieView* movie, RE::GFxValue& outKeywords, const RE::MagicItem* magicItem)
		{
			movie->CreateObject(&outKeywords);
			if (!magicItem) {
				return;
			}
			for (const auto* effect : magicItem->effects) {
				if (!effect || !effect->baseEffect) {
					continue;
				}
				const auto keywordForm = effect->baseEffect->As<RE::BGSKeywordForm>();
				if (!keywordForm) {
					continue;
				}
				for (std::uint32_t i = 0; i < keywordForm->GetNumKeywords(); ++i) {
					const auto keyword = keywordForm->GetKeywordAt(i).value_or(nullptr);
					if (!keyword) {
						continue;
					}
					const auto editorID = keyword->GetFormEditorID();
					if (!editorID || !editorID[0]) {
						continue;
					}
					outKeywords.SetMember(editorID, true);
				}
			}
		}

		EquipType GetArmorSubType(std::uint32_t partMask)
		{
			constexpr std::array<std::pair<RE::BIPED_MODEL::BipedObjectSlot, EquipType>, 14> map = {
				std::pair{ RE::BIPED_MODEL::BipedObjectSlot::kBody, EquipType::Body },
				std::pair{ RE::BIPED_MODEL::BipedObjectSlot::kHair, EquipType::Hair },
				std::pair{ RE::BIPED_MODEL::BipedObjectSlot::kHands, EquipType::Hands },
				std::pair{ RE::BIPED_MODEL::BipedObjectSlot::kForearms, EquipType::Forearms },
				std::pair{ RE::BIPED_MODEL::BipedObjectSlot::kFeet, EquipType::Feet },
				std::pair{ RE::BIPED_MODEL::BipedObjectSlot::kCalves, EquipType::Calves },
				std::pair{ RE::BIPED_MODEL::BipedObjectSlot::kShield, EquipType::Shield },
				std::pair{ RE::BIPED_MODEL::BipedObjectSlot::kAmulet, EquipType::Amulet },
				std::pair{ RE::BIPED_MODEL::BipedObjectSlot::kRing, EquipType::Ring },
				std::pair{ RE::BIPED_MODEL::BipedObjectSlot::kLongHair, EquipType::LongHair },
				std::pair{ RE::BIPED_MODEL::BipedObjectSlot::kEars, EquipType::Ears },
				std::pair{ RE::BIPED_MODEL::BipedObjectSlot::kHead, EquipType::Head },
				std::pair{ RE::BIPED_MODEL::BipedObjectSlot::kCirclet, EquipType::Circlet },
				std::pair{ RE::BIPED_MODEL::BipedObjectSlot::kTail, EquipType::Tail }
			};

			for (const auto& [slot, type] : map) {
				const auto bit = static_cast<std::uint32_t>(slot);
				if ((partMask & bit) != 0) {
					return type;
				}
			}

			return EquipType::Body;
		}

		std::uint32_t GetArmorMainPart(std::uint32_t partMask)
		{
			constexpr std::array<RE::BIPED_MODEL::BipedObjectSlot, 14> slotOrder = {
				RE::BIPED_MODEL::BipedObjectSlot::kBody,
				RE::BIPED_MODEL::BipedObjectSlot::kHair,
				RE::BIPED_MODEL::BipedObjectSlot::kHands,
				RE::BIPED_MODEL::BipedObjectSlot::kForearms,
				RE::BIPED_MODEL::BipedObjectSlot::kFeet,
				RE::BIPED_MODEL::BipedObjectSlot::kCalves,
				RE::BIPED_MODEL::BipedObjectSlot::kShield,
				RE::BIPED_MODEL::BipedObjectSlot::kAmulet,
				RE::BIPED_MODEL::BipedObjectSlot::kRing,
				RE::BIPED_MODEL::BipedObjectSlot::kLongHair,
				RE::BIPED_MODEL::BipedObjectSlot::kEars,
				RE::BIPED_MODEL::BipedObjectSlot::kHead,
				RE::BIPED_MODEL::BipedObjectSlot::kCirclet,
				RE::BIPED_MODEL::BipedObjectSlot::kTail
			};

			for (const auto slot : slotOrder) {
				const auto bit = static_cast<std::uint32_t>(slot);
				if ((partMask & bit) != 0) {
					return bit;
				}
			}

			return 0u;
		}

		void SetDefaultArmorIcon(RE::GFxValue& entry, ArmorWeightClass weightClass, EquipType subType)
		{
			switch (weightClass) {
			case ArmorWeightClass::Light:
				SetColorRGB(entry, 0x756000);
				switch (subType) {
				case EquipType::Head:
				case EquipType::Hair:
				case EquipType::LongHair:
					SetIconDefaults(entry, "lightarmor_head", 0x756000);
					break;
				case EquipType::Body:
				case EquipType::Tail:
					SetIconDefaults(entry, "lightarmor_body", 0x756000);
					break;
				case EquipType::Hands:
					SetIconDefaults(entry, "lightarmor_hands", 0x756000);
					break;
				case EquipType::Forearms:
					SetIconDefaults(entry, "lightarmor_forearms", 0x756000);
					break;
				case EquipType::Feet:
					SetIconDefaults(entry, "lightarmor_feet", 0x756000);
					break;
				case EquipType::Calves:
					SetIconDefaults(entry, "lightarmor_calves", 0x756000);
					break;
				case EquipType::Shield:
					SetIconDefaults(entry, "lightarmor_shield", 0x756000);
					break;
				case EquipType::Amulet:
					SetIconDefaults(entry, "armor_amulet", 0x756000);
					break;
				case EquipType::Ring:
					SetIconDefaults(entry, "armor_ring", 0x756000);
					break;
				case EquipType::Circlet:
					SetIconDefaults(entry, "armor_circlet", 0x756000);
					break;
				default:
					SetIconDefaults(entry, "default_armor", 0x756000);
					break;
				}
				break;
			case ArmorWeightClass::Heavy:
				SetColorRGB(entry, 0x6B7585);
				switch (subType) {
				case EquipType::Head:
				case EquipType::Hair:
				case EquipType::LongHair:
					SetIconDefaults(entry, "armor_head", 0x6B7585);
					break;
				case EquipType::Body:
				case EquipType::Tail:
					SetIconDefaults(entry, "armor_body", 0x6B7585);
					break;
				case EquipType::Hands:
					SetIconDefaults(entry, "armor_hands", 0x6B7585);
					break;
				case EquipType::Forearms:
					SetIconDefaults(entry, "armor_forearms", 0x6B7585);
					break;
				case EquipType::Feet:
					SetIconDefaults(entry, "armor_feet", 0x6B7585);
					break;
				case EquipType::Calves:
					SetIconDefaults(entry, "armor_calves", 0x6B7585);
					break;
				case EquipType::Shield:
					SetIconDefaults(entry, "armor_shield", 0x6B7585);
					break;
				case EquipType::Amulet:
					SetIconDefaults(entry, "armor_amulet", 0x6B7585);
					break;
				case EquipType::Ring:
					SetIconDefaults(entry, "armor_ring", 0x6B7585);
					break;
				case EquipType::Circlet:
					SetIconDefaults(entry, "armor_circlet", 0x6B7585);
					break;
				default:
					SetIconDefaults(entry, "default_armor", 0x6B7585);
					break;
				}
				break;
			case ArmorWeightClass::Jewelry:
				switch (subType) {
				case EquipType::Amulet:
					SetIconDefaults(entry, "armor_amulet", 0xEDDA87);
					break;
				case EquipType::Ring:
					SetIconDefaults(entry, "armor_ring", 0xEDDA87);
					break;
				case EquipType::Circlet:
					SetIconDefaults(entry, "armor_circlet", 0xEDDA87);
					break;
				default:
					SetIconDefaults(entry, "default_armor", 0xEDDA87);
					break;
				}
				break;
			case ArmorWeightClass::Clothing:
			case ArmorWeightClass::None:
			default:
				switch (subType) {
				case EquipType::Head:
				case EquipType::Hair:
				case EquipType::LongHair:
					SetIconDefaults(entry, "clothing_head", 0xEDDA87);
					break;
				case EquipType::Body:
				case EquipType::Tail:
					SetIconDefaults(entry, "clothing_body", 0xEDDA87);
					break;
				case EquipType::Hands:
					SetIconDefaults(entry, "clothing_hands", 0xEDDA87);
					break;
				case EquipType::Forearms:
					SetIconDefaults(entry, "clothing_forearms", 0xEDDA87);
					break;
				case EquipType::Feet:
					SetIconDefaults(entry, "clothing_feet", 0xEDDA87);
					break;
				case EquipType::Calves:
					SetIconDefaults(entry, "clothing_calves", 0xEDDA87);
					break;
				case EquipType::Shield:
					SetIconDefaults(entry, "clothing_shield", 0xEDDA87);
					break;
				default:
					SetIconDefaults(entry, "default_armor", 0xEDDA87);
					break;
				}
				break;
			}
		}

		void SetDefaultWeaponIcon(RE::GFxValue& entry, WeaponType subType)
		{
			switch (subType) {
			case WeaponType::Sword:
				SetIconDefaults(entry, "weapon_sword", 0xA4A5BF);
				return;
			case WeaponType::Dagger:
				SetIconDefaults(entry, "weapon_dagger", 0xA4A5BF);
				return;
			case WeaponType::WarAxe:
				SetIconDefaults(entry, "weapon_waraxe", 0xA4A5BF);
				return;
			case WeaponType::Mace:
				SetIconDefaults(entry, "weapon_mace", 0xA4A5BF);
				return;
			case WeaponType::Greatsword:
				SetIconDefaults(entry, "weapon_greatsword", 0xA4A5BF);
				return;
			case WeaponType::Battleaxe:
				SetIconDefaults(entry, "weapon_battleaxe", 0xA4A5BF);
				return;
			case WeaponType::Warhammer:
				SetIconDefaults(entry, "weapon_hammer", 0xA4A5BF);
				return;
			case WeaponType::Bow:
				SetIconDefaults(entry, "weapon_bow", 0xA4A5BF);
				return;
			case WeaponType::Crossbow:
				SetIconDefaults(entry, "weapon_crossbow", 0xA4A5BF);
				return;
			case WeaponType::Staff:
				SetIconDefaults(entry, "weapon_staff", 0xA4A5BF);
				return;
			case WeaponType::Pickaxe:
				SetIconDefaults(entry, "weapon_pickaxe", 0xA4A5BF);
				return;
			case WeaponType::WoodAxe:
				SetIconDefaults(entry, "weapon_woodaxe", 0xA4A5BF);
				return;
			default:
				SetIconDefaults(entry, "default_weapon", 0xA4A5BF);
				return;
			}
		}

		MiscType ClassifyMiscSubtype(const RE::TESObjectMISC* misc)
		{
			if (!misc) {
				return MiscType::Clutter;
			}
			const auto formID = misc->GetFormID();
			if (formID == 0x0000000F) {
				return MiscType::Gold;
			}
			if (formID == 0x0000000A) {
				return MiscType::Lockpick;
			}
			if (misc->HasKeywordString("VendorItemGem")) {
				return MiscType::Gem;
			}
			if (misc->HasKeywordString("VendorItemDragonClaw")) {
				return MiscType::DragonClaw;
			}
			if (misc->HasKeywordString("VendorItemArtifact")) {
				return MiscType::Artifact;
			}
			if (misc->HasKeywordString("VendorItemLeather")) {
				return MiscType::Leather;
			}
			if (misc->HasKeywordString("VendorItemAnimalHide")) {
				return MiscType::Hide;
			}
			if (misc->HasKeywordString("VendorItemOreIngot")) {
				return MiscType::Ingot;
			}
			if (misc->HasKeywordString("VendorItemTool")) {
				return MiscType::Tool;
			}
			if (misc->HasKeywordString("VendorItemToy")) {
				return MiscType::Toy;
			}
			if (misc->HasKeywordString("VendorItemHousePart")) {
				return MiscType::HousePart;
			}
			if (misc->HasKeywordString("VendorItemFireword") || misc->HasKeywordString("VendorItemFirewood")) {
				return MiscType::Firewood;
			}
			return MiscType::Clutter;
		}

		void SetDefaultMiscIcon(RE::GFxValue& entry, MiscType subtype)
		{
			switch (subtype) {
			case MiscType::Gem:
				SetIconDefaults(entry, "misc_gem", 0xFFB0D1);
				return;
			case MiscType::DragonClaw:
				SetIconDefaults(entry, "misc_dragonclaw");
				return;
			case MiscType::Artifact:
				SetIconDefaults(entry, "misc_artifact");
				return;
			case MiscType::Leather:
			case MiscType::LeatherStrips:
				SetIconDefaults(entry, subtype == MiscType::Leather ? "misc_leather" : "misc_strips", 0xBA8D23);
				return;
			case MiscType::Hide:
				SetIconDefaults(entry, "misc_hide", 0xDBB36E);
				return;
			case MiscType::Remains:
				SetIconDefaults(entry, "misc_remains");
				return;
			case MiscType::Ingot:
				SetIconDefaults(entry, "misc_ingot", 0x828282);
				return;
			case MiscType::ChildrensClothes:
				SetIconDefaults(entry, "clothing_body", 0xEDDA87);
				return;
			case MiscType::Firewood:
				SetIconDefaults(entry, "misc_wood", 0xA89E8C);
				return;
			case MiscType::Lockpick:
				SetIconDefaults(entry, "misc_lockpick");
				return;
			case MiscType::Gold:
				SetIconDefaults(entry, "misc_gold", 0xCCCC33);
				return;
			default:
				SetIconDefaults(entry, "misc_clutter");
				return;
			}
		}

		WeaponType ClassifyWeaponSubtype(const RE::TESObjectWEAP* weapon)
		{
			if (!weapon) {
				return WeaponType::Melee;
			}

			if (weapon->HasKeywordString("WeapTypePickaxe")) {
				return WeaponType::Pickaxe;
			}
			if (weapon->HasKeywordString("WeapTypeWoodAxe")) {
				return WeaponType::WoodAxe;
			}

			switch (weapon->GetWeaponType()) {
			case RE::WEAPON_TYPE::kOneHandSword:
				return WeaponType::Sword;
			case RE::WEAPON_TYPE::kOneHandDagger:
				return WeaponType::Dagger;
			case RE::WEAPON_TYPE::kOneHandAxe:
				return WeaponType::WarAxe;
			case RE::WEAPON_TYPE::kOneHandMace:
				return WeaponType::Mace;
			case RE::WEAPON_TYPE::kTwoHandSword:
				return WeaponType::Greatsword;
			case RE::WEAPON_TYPE::kTwoHandAxe:
				return weapon->HasKeywordString("WeapTypeWarhammer") ? WeaponType::Warhammer : WeaponType::Battleaxe;
			case RE::WEAPON_TYPE::kBow:
				return WeaponType::Bow;
			case RE::WEAPON_TYPE::kStaff:
				return WeaponType::Staff;
			case RE::WEAPON_TYPE::kCrossbow:
				return WeaponType::Crossbow;
			default:
				return WeaponType::Melee;
			}
		}

		bool HasKeywordPrefix(const RE::BGSKeywordForm* keywordForm, std::string_view prefix)
		{
			if (!keywordForm || prefix.empty()) {
				return false;
			}

			for (std::uint32_t i = 0; i < keywordForm->GetNumKeywords(); ++i) {
				const auto keyword = keywordForm->GetKeywordAt(i).value_or(nullptr);
				if (!keyword) {
					continue;
				}

				const auto editorID = keyword->GetFormEditorID();
				if (!editorID || !editorID[0]) {
					continue;
				}

				if (std::string_view(editorID).starts_with(prefix)) {
					return true;
				}
			}

			return false;
		}

		bool IsDrinkLikeFoodPotion(const RE::AlchemyItem* potion)
		{
			if (!potion) {
				return false;
			}

			if (potion->HasKeywordString("OCF_AlchDrink") ||
			    potion->HasKeywordString("OCF_AlchDrug") ||
			    potion->HasKeywordString("OCF_AlchSnow")) {
				return true;
			}

			const auto* keywordForm = potion->As<RE::BGSKeywordForm>();
			return HasKeywordPrefix(keywordForm, "OCF_AlchDrink") ||
			       HasKeywordPrefix(keywordForm, "VendorItemDrink");
		}

		PotionType ClassifyPotionSubtype(const RE::AlchemyItem* potion, const RE::Effect* primaryEffect)
		{
			if (!potion) {
				return PotionType::Potion;
			}
			if (potion->data.flags.all(RE::AlchemyItem::AlchemyFlag::kFoodItem)) {
				// InventoryInjector drink/drug rules expect drink-like food potions to start as Drink.
				return IsDrinkLikeFoodPotion(potion) ? PotionType::Drink : PotionType::Food;
			}
			if (potion->data.flags.all(RE::AlchemyItem::AlchemyFlag::kPoison)) {
				return PotionType::Poison;
			}
			if (!primaryEffect || !primaryEffect->baseEffect) {
				return PotionType::Potion;
			}
			switch (primaryEffect->baseEffect->data.primaryAV) {
			case RE::ActorValue::kHealth:
				return PotionType::Health;
			case RE::ActorValue::kHealRate:
				return PotionType::HealRate;
			case RE::ActorValue::kHealRateMult:
				return PotionType::HealRateMult;
			case RE::ActorValue::kMagicka:
				return PotionType::Magicka;
			case RE::ActorValue::kMagickaRate:
				return PotionType::MagickaRate;
			case RE::ActorValue::kMagickaRateMult:
				return PotionType::MagickaRateMult;
			case RE::ActorValue::kStamina:
				return PotionType::Stamina;
			case RE::ActorValue::kStaminaRate:
				return PotionType::StaminaRate;
			case RE::ActorValue::kStaminaRateMult:
				return PotionType::StaminaRateMult;
			case RE::ActorValue::kResistFire:
				return PotionType::FireResist;
			case RE::ActorValue::kResistShock:
				return PotionType::ElectricResist;
			case RE::ActorValue::kResistFrost:
				return PotionType::FrostResist;
			default:
				return PotionType::Potion;
			}
		}

		void SetDefaultPotionIcon(RE::GFxValue& entry, PotionType subtype)
		{
			switch (subtype) {
			case PotionType::Drink:
				SetIconDefaults(entry, "food_wine");
				return;
			case PotionType::Food:
				SetIconDefaults(entry, "default_food");
				return;
			case PotionType::Poison:
				SetIconDefaults(entry, "potion_poison", 0xAD00B3);
				return;
			case PotionType::Health:
			case PotionType::HealRate:
			case PotionType::HealRateMult:
				SetIconDefaults(entry, "potion_health", 0xDB2E73);
				return;
			case PotionType::Magicka:
			case PotionType::MagickaRate:
			case PotionType::MagickaRateMult:
				SetIconDefaults(entry, "potion_magic", 0x2E9FDB);
				return;
			case PotionType::Stamina:
			case PotionType::StaminaRate:
			case PotionType::StaminaRateMult:
				SetIconDefaults(entry, "potion_stam", 0x51DB2E);
				return;
			case PotionType::FireResist:
				SetIconDefaults(entry, "potion_fire", 0xC73636);
				return;
			case PotionType::ElectricResist:
				SetIconDefaults(entry, "potion_shock", 0xEAAB00);
				return;
			case PotionType::FrostResist:
				SetIconDefaults(entry, "potion_frost", 0x1FFBFF);
				return;
			default:
				SetIconDefaults(entry, "default_potion");
				return;
			}
		}

		ArmorWeightClass ClassifyArmorWeight(const RE::TESObjectARMO* armor)
		{
			if (!armor) {
				return ArmorWeightClass::None;
			}

			const auto armorType = static_cast<std::uint32_t>(armor->bipedModelData.armorType.underlying());
			if (armorType == 0) {
				return ArmorWeightClass::Light;
			}
			if (armorType == 1) {
				return ArmorWeightClass::Heavy;
			}
			if (armor->HasKeywordString("VendorItemJewelry")) {
				return ArmorWeightClass::Jewelry;
			}
			if (armor->HasKeywordString("VendorItemClothing")) {
				return ArmorWeightClass::Clothing;
			}
			return ArmorWeightClass::None;
		}

		BookType ClassifyBookSubType(const RE::TESObjectBOOK* book)
		{
			if (!book) {
				return BookType::Note;
			}
			if (book->HasKeywordString("VendorItemRecipe")) {
				return BookType::Recipe;
			}
			if (book->HasKeywordString("VendorItemSpellTome")) {
				return BookType::SpellTome;
			}
			if (book->data.type == RE::OBJ_BOOK::Type::kNoteScroll) {
				return BookType::Note;
			}
			return BookType::Note;
		}

		void SetDefaultBookIcon(RE::GFxValue& entry, BookType subtype)
		{
			switch (subtype) {
			case BookType::Recipe:
			case BookType::Note:
				SetIconDefaults(entry, "book_note");
				break;
			case BookType::SpellTome:
				SetIconDefaults(entry, "book_tome");
				break;
			default:
				SetIconDefaults(entry, "default_book");
				break;
			}
		}

		void PopulateMagicData(RE::GFxValue& entry, const RE::MagicItem* magicItem)
		{
			if (!magicItem) {
				return;
			}

			SetString(entry, "spellName", magicItem->GetName());

			const auto* effect = GetPrimaryEffect(magicItem);
			if (!effect || !effect->baseEffect) {
				return;
			}

			const auto* baseEffect = effect->baseEffect;
			SetNumber(entry, "magnitude", effect->GetMagnitude());
			SetNumber(entry, "duration", static_cast<std::int32_t>(effect->GetDuration()));
			SetNumber(entry, "area", static_cast<std::int32_t>(effect->GetArea()));
			SetString(entry, "effectName", baseEffect->GetName());
			SetNumber(entry, "subType", static_cast<std::int32_t>(baseEffect->data.associatedSkill));
			SetNumber(entry, "effectFlags", static_cast<std::int32_t>(baseEffect->data.flags.underlying()));
			SetNumber(entry, "school", static_cast<std::int32_t>(baseEffect->data.associatedSkill));
			SetNumber(entry, "skillLevel", static_cast<std::int32_t>(baseEffect->data.minimumSkill));
			SetNumber(entry, "archetype", static_cast<std::int32_t>(baseEffect->data.archetype));
			SetNumber(entry, "deliveryType", static_cast<std::int32_t>(baseEffect->data.delivery));
			SetNumber(entry, "castTime", static_cast<float>(baseEffect->data.spellmakingChargeTime));
			SetNumber(entry, "delayTime", static_cast<float>(baseEffect->data.aiDelayTimer));
			SetNumber(entry, "actorValue", static_cast<std::int32_t>(baseEffect->data.primaryAV));
			SetNumber(entry, "castType", static_cast<std::int32_t>(baseEffect->data.castingType));
			SetNumber(entry, "resistance", static_cast<std::int32_t>(baseEffect->data.resistVariable));
		}

		void PopulateSpellData(RE::GFxValue& entry, const RE::SpellItem* spell)
		{
			if (!spell) {
				return;
			}

			SetNumber(entry, "spellType", static_cast<std::int32_t>(spell->GetSpellType()));
			SetNumber(entry, "trueCost", static_cast<std::int32_t>(spell->data.costOverride));
			if (const auto equipSlot = spell->GetEquipSlot()) {
				entry.SetMember("equipSlot", static_cast<double>(equipSlot->GetFormID()));
			}
		}

		void SetDefaultSpellIcon(RE::GFxValue& entry, const RE::SpellItem* spell)
		{
			if (!spell) {
				SetIconDefaults(entry, "default_destruction");
				return;
			}

			const auto spellType = spell->GetSpellType();
			if (spellType == RE::MagicSystem::SpellType::kPower ||
			    spellType == RE::MagicSystem::SpellType::kLesserPower ||
			    spellType == RE::MagicSystem::SpellType::kVoicePower) {
				SetIconDefaults(entry, "default_power");
				return;
			}

			const auto* primaryEffect = GetPrimaryEffect(spell);
			if (!primaryEffect || !primaryEffect->baseEffect) {
				SetIconDefaults(entry, "default_destruction");
				return;
			}

			auto school = primaryEffect->baseEffect->GetMagickSkill();
			if (school == RE::ActorValue::kNone) {
				school = primaryEffect->baseEffect->data.primaryAV;
			}

			switch (school) {
			case RE::ActorValue::kAlteration:
				SetIconDefaults(entry, "default_alteration");
				break;
			case RE::ActorValue::kConjuration:
				SetIconDefaults(entry, "default_conjuration");
				break;
			case RE::ActorValue::kIllusion:
				SetIconDefaults(entry, "default_illusion");
				break;
			case RE::ActorValue::kRestoration:
				SetIconDefaults(entry, "default_restoration");
				break;
			case RE::ActorValue::kDestruction:
				switch (primaryEffect->baseEffect->data.resistVariable) {
				case RE::ActorValue::kResistFire:
					SetIconDefaults(entry, "magic_fire", 0xC73636);
					break;
				case RE::ActorValue::kResistShock:
					SetIconDefaults(entry, "magic_shock", 0xFFFF00);
					break;
				case RE::ActorValue::kResistFrost:
					SetIconDefaults(entry, "magic_frost", 0x1FFBFF);
					break;
				default:
					SetIconDefaults(entry, "default_destruction");
					break;
				}
				break;
			default:
				SetIconDefaults(entry, "default_destruction");
				break;
			}
		}
		
		std::optional<std::uint32_t> ParseColorFromGfx(const RE::GFxValue& value)
		{
			if (value.IsNumber()) {
				const std::uint32_t raw = static_cast<std::uint32_t>(value.GetNumber());
				return raw;
			}
			if (value.IsString()) {
				ImU32 color = C_SKYRIMWHITE;
				if (ParseIconColorString(value.GetString(), color)) {
					const auto col = ImGui::ColorConvertU32ToFloat4(color);
					const auto r = static_cast<std::uint32_t>(std::clamp(col.x, 0.0f, 1.0f) * 255.0f);
					const auto g = static_cast<std::uint32_t>(std::clamp(col.y, 0.0f, 1.0f) * 255.0f);
					const auto b = static_cast<std::uint32_t>(std::clamp(col.z, 0.0f, 1.0f) * 255.0f);
					const auto a = static_cast<std::uint32_t>(std::clamp(col.w, 0.0f, 1.0f) * 255.0f);
					return (a << 24) | (r << 16) | (g << 8) | b;
				}
			}
			return std::nullopt;
		}

		ImU32 ConvertColorValue(std::uint32_t raw)
		{
			if ((raw & 0xFF000000u) == 0u) {
				const auto r = static_cast<std::uint8_t>((raw >> 16) & 0xFF);
				const auto g = static_cast<std::uint8_t>((raw >> 8) & 0xFF);
				const auto b = static_cast<std::uint8_t>(raw & 0xFF);
				return IM_COL32(r, g, b, 0xFF);
			}
			const auto a = static_cast<std::uint8_t>((raw >> 24) & 0xFF);
			const auto r = static_cast<std::uint8_t>((raw >> 16) & 0xFF);
			const auto g = static_cast<std::uint8_t>((raw >> 8) & 0xFF);
			const auto b = static_cast<std::uint8_t>(raw & 0xFF);
			return IM_COL32(r, g, b, a);
		}

		bool IsDefaultSourcePath(const std::string& source)
		{
			const auto normalized = NormalizeString(source);
			return normalized.find("skyui\\icons_item_psychosteve.swf") != std::string::npos;
		}

		std::string DescribeGfxValue(const RE::GFxValue& value)
		{
			if (value.IsString()) {
				return fmt::format("string:'{}'", value.GetString());
			}
			if (value.IsNumber()) {
				return fmt::format("number:{:.3f}", value.GetNumber());
			}
			if (value.IsBool()) {
				return value.GetBool() ? "bool:true" : "bool:false";
			}
			if (value.IsNull()) {
				return "null";
			}
			if (value.IsUndefined()) {
				return "undefined";
			}
			if (value.IsArray()) {
				return "array";
			}
			if (value.IsObject()) {
				return "object";
			}
			if (value.IsDisplayObject()) {
				return "display_object";
			}
			return "unknown";
		}

		std::string ReadMemberDescription(const RE::GFxValue& object, const char* memberName)
		{
			RE::GFxValue value;
			if (!object.GetMember(memberName, &value)) {
				return "<missing>";
			}
			return DescribeGfxValue(value);
		}

	}

	I4IconResolver& I4IconResolver::GetSingleton()
	{
		static I4IconResolver singleton;
		return singleton;
	}

	bool I4IconResolver::TryGetCached(RE::TESForm* object, std::uint64_t signature, I4IconSpec& outSpec)
	{
		const bool traceCache = Config::I4::TraceLog && Config::I4::TraceCacheHits;
		const RE::FormID formID = object ? object->GetFormID() : 0;
		if (!object) {
			if (traceCache) {
				I4_LOG_INFO("[I4][TRACE][resolver.cached] form=00000000 decision=miss reason=null_form");
			}
			return false;
		}

		const ResolveKey key{ formID, signature };
		std::scoped_lock lock(_lock);
		auto it = _cache.find(key);
		if (it == _cache.end() || !it->second.valid) {
			if (traceCache) {
				I4_LOG_INFO("[I4][TRACE][resolver.cached] form={:08X} sig={} decision=miss cached={}",
					formID,
					signature,
					it != _cache.end());
			}
			return false;
		}

		outSpec = it->second;
		GetStats().resolveCacheHits.fetch_add(1, std::memory_order_relaxed);
		if (traceCache) {
			I4_LOG_INFO("[I4][TRACE][resolver.cached] form={:08X} sig={} decision=hit source='{}' label='{}' color=0x{:08X}",
				formID,
				signature,
				outSpec.iconSource,
				outSpec.iconLabel,
				static_cast<std::uint32_t>(outSpec.color));
		}
		return true;
	}

	I4IconSpec I4IconResolver::Resolve(
		RE::TESForm* object,
		std::uint64_t signature,
		RE::ExtraDataList* extra,
		RE::InventoryEntryData* entryData)
	{
		auto& stats = GetStats();
		stats.resolveCalls.fetch_add(1, std::memory_order_relaxed);
		const bool trace = Config::I4::TraceLog;
		const bool traceCache = trace && Config::I4::TraceCacheHits;
		const RE::FormID formID = object ? object->GetFormID() : 0;
		const std::uint32_t formType = object ? static_cast<std::uint32_t>(object->GetFormType()) : 0;

		if (trace) {
			I4_LOG_INFO("[I4][TRACE][resolver.req] form={:08X} type={} sig={} hasExtra={} hasEntryData={}",
				formID,
				formType,
				signature,
				extra != nullptr,
				entryData != nullptr);
		}

		if (!object) {
			if (trace) {
				I4_LOG_INFO("[I4][TRACE][resolver.out] form=00000000 decision=invalid_input reason=null_form");
			}
			return {};
		}

		auto& availability = I4Availability::GetSingleton();
		auto* movie = availability.GetInvokerMovie();
		if (!movie) {
			if (Config::I4::DebugLog) {
				const auto warnKey = fmt::format("resolve:no_movie:{}", availability.GetStatusString());
				std::scoped_lock lock(_lock);
				if (_warned.insert(warnKey).second) {
					I4_LOG_WARN("[I4] resolve form={:08X} failed: no invoker movie (status={})",
						formID,
						availability.GetStatusString());
				}
			}
			if (trace) {
				I4_LOG_INFO("[I4][TRACE][resolver.out] form={:08X} decision=fallback reason=no_invoker_movie status={}",
					formID,
					availability.GetStatusString());
			}
			return {};
		}

		const ResolveKey key{ formID, signature };
		{
			std::scoped_lock lock(_lock);
			auto it = _cache.find(key);
			if (it != _cache.end()) {
				stats.resolveCacheHits.fetch_add(1, std::memory_order_relaxed);
				if (traceCache) {
					I4_LOG_INFO("[I4][TRACE][resolver.out] form={:08X} sig={} decision=cache_hit valid={} source='{}' label='{}' color=0x{:08X}",
						formID,
						signature,
						it->second.valid,
						it->second.iconSource,
						it->second.iconLabel,
						static_cast<std::uint32_t>(it->second.color));
				}
				return it->second;
			}
		}

		I4IconSpec resolved = ResolveImpl(object, movie, signature, extra, entryData);

		{
			std::scoped_lock lock(_lock);
			_cache.emplace(key, resolved);
		}

		if (trace) {
			I4_LOG_INFO("[I4][TRACE][resolver.cache] form={:08X} sig={} action=store valid={} sourceKind={} source='{}' label='{}' color=0x{:08X}",
				formID,
				signature,
				resolved.valid,
				IsDefaultSourcePath(resolved.iconSource) ? "default" : "custom",
				resolved.iconSource,
				resolved.iconLabel,
				static_cast<std::uint32_t>(resolved.color));
		}

		if (Config::I4::DebugLog || trace) {
			I4_LOG_INFO(
				"[I4] resolve form={:08X} type={} sig={} valid={} sourceKind={} source='{}' label='{}' color=0x{:08X}",
				formID,
				formType,
				signature,
				resolved.valid,
				IsDefaultSourcePath(resolved.iconSource) ? "default" : "custom",
				resolved.iconSource,
				resolved.iconLabel,
				static_cast<std::uint32_t>(resolved.color));
		}

		return resolved;
	}

	I4IconSpec I4IconResolver::ResolveImpl(
		RE::TESForm* object,
		RE::GFxMovieView* movie,
		std::uint64_t signature,
		RE::ExtraDataList*,
		RE::InventoryEntryData*)
	{
		const bool trace = Config::I4::TraceLog;
		const RE::FormID formID = object ? object->GetFormID() : 0;
		const std::uint32_t formType = object ? static_cast<std::uint32_t>(object->GetFormType()) : 0;

		if (!movie->IsAvailable("skse.plugins.InventoryInjector.ProcessEntry")) {
			if (Config::I4::DebugLog) {
				const auto warnKey = "resolve:process_entry_unavailable";
				std::scoped_lock lock(_lock);
				if (_warned.insert(warnKey).second) {
					I4_LOG_WARN("[I4] resolve failed: skse.plugins.InventoryInjector.ProcessEntry is unavailable in invoker movie.");
				}
			}
			if (trace) {
				I4_LOG_INFO("[I4][TRACE][resolver.out] form={:08X} type={} sig={} decision=fallback reason=process_entry_unavailable",
					formID,
					formType,
					signature);
			}
			return {};
		}

		RE::GFxValue entryObj;
		movie->CreateObject(&entryObj);
		entryObj.SetMember("iconSource", kDefaultIconSource);

		SetNumber(entryObj, "formType", static_cast<std::int32_t>(object->GetFormType()));
		entryObj.SetMember("formId", static_cast<double>(object->GetFormID()));

		RE::GFxValue keywords;
		BuildKeywordsObject(movie, keywords, object->As<RE::BGSKeywordForm>());
		entryObj.SetMember("keywords", keywords);

		const auto* magicItem = object->As<RE::MagicItem>();
		if (magicItem) {
			PopulateMagicData(entryObj, magicItem);
			RE::GFxValue effectKeywords;
			BuildEffectKeywordsObject(movie, effectKeywords, magicItem);
			entryObj.SetMember("effectKeywords", effectKeywords);
		}

		switch (object->GetFormType()) {
		case RE::FormType::Armor:
			{
				auto* armor = object->As<RE::TESObjectARMO>();
				const auto partMask = static_cast<std::uint32_t>(armor->bipedModelData.bipedObjectSlots.underlying());
				const auto mainPart = GetArmorMainPart(partMask);
				const auto subType = GetArmorSubType(partMask);
				const auto weightClass = ClassifyArmorWeight(armor);
				SetNumber(entryObj, "parts", static_cast<std::int32_t>(partMask));
				SetNumber(entryObj, "partMask", static_cast<std::int32_t>(partMask));
				SetNumber(entryObj, "mainPart", static_cast<std::int32_t>(mainPart));
				SetNumber(entryObj, "mainPartMask", static_cast<std::int32_t>(mainPart));
				SetNumber(entryObj, "subType", static_cast<std::int32_t>(subType));
				SetNumber(entryObj, "weightClass", static_cast<std::int32_t>(weightClass));
				SetDefaultArmorIcon(entryObj, weightClass, subType);
			}
			break;
		case RE::FormType::Weapon:
			{
				const auto* weapon = object->As<RE::TESObjectWEAP>();
				const auto subType = ClassifyWeaponSubtype(weapon);
				SetNumber(entryObj, "subType", static_cast<std::int32_t>(subType));
				if (weapon) {
					SetNumber(entryObj, "weaponType", static_cast<std::int32_t>(weapon->GetWeaponType()));
					if (const auto equipSlot = weapon->GetEquipSlot()) {
						entryObj.SetMember("equipSlot", static_cast<double>(equipSlot->GetFormID()));
					}
				}
				SetDefaultWeaponIcon(entryObj, subType);
			}
			break;
		case RE::FormType::Ammo:
			{
				const auto* ammo = object->As<RE::TESAmmo>();
				const auto subType = ammo && ammo->GetRuntimeData().data.flags.all(RE::AMMO_DATA::Flag::kNonBolt) ?
					static_cast<std::int32_t>(AmmoType::Arrow) :
					static_cast<std::int32_t>(AmmoType::Bolt);
				SetNumber(entryObj, "subType", subType);
				if (subType == static_cast<std::int32_t>(AmmoType::Arrow)) {
					SetIconDefaults(entryObj, "weapon_arrow", 0xA89E8C);
				} else {
					SetIconDefaults(entryObj, "weapon_bolt", 0xA89E8C);
				}
			}
			break;
		case RE::FormType::AlchemyItem:
			{
				const auto* alchemy = object->As<RE::AlchemyItem>();
				const auto* primaryEffect = GetPrimaryEffect(alchemy);
				const auto subType = ClassifyPotionSubtype(alchemy, primaryEffect);
				if (alchemy) {
					SetNumber(entryObj, "flags", static_cast<std::int32_t>(alchemy->data.flags.underlying()));
				}
				SetNumber(entryObj, "subType", static_cast<std::int32_t>(subType));
				SetDefaultPotionIcon(entryObj, subType);
			}
			break;
		case RE::FormType::Ingredient:
			SetIconDefaults(entryObj, "default_ingredient");
			break;
		case RE::FormType::Book:
			{
				const auto subType = ClassifyBookSubType(object->As<RE::TESObjectBOOK>());
				SetNumber(entryObj, "subType", static_cast<std::int32_t>(subType));
				SetDefaultBookIcon(entryObj, subType);
			}
			break;
		case RE::FormType::Misc:
			{
				const auto subType = ClassifyMiscSubtype(object->As<RE::TESObjectMISC>());
				SetNumber(entryObj, "subType", static_cast<std::int32_t>(subType));
				SetDefaultMiscIcon(entryObj, subType);
			}
			break;
		case RE::FormType::Scroll:
			{
				const auto* scroll = object->As<RE::ScrollItem>();
				PopulateSpellData(entryObj, scroll);
				const auto* effect = GetPrimaryEffect(scroll);
				SetIconDefaults(entryObj, "default_scroll");
				if (effect && effect->baseEffect) {
					const auto resist = static_cast<std::int32_t>(effect->baseEffect->data.resistVariable);
					SetNumber(entryObj, "resistance", resist);
					switch (effect->baseEffect->data.resistVariable) {
					case RE::ActorValue::kResistFire:
						SetIconDefaults(entryObj, "magic_fire", 0xC73636);
						break;
					case RE::ActorValue::kResistShock:
						SetIconDefaults(entryObj, "magic_shock", 0xFFFF00);
						break;
					case RE::ActorValue::kResistFrost:
						SetIconDefaults(entryObj, "magic_frost", 0x1FFBFF);
						break;
					default:
						break;
					}
				}
			}
			break;
		case RE::FormType::Light:
			SetIconDefaults(entryObj, "misc_torch");
			break;
		case RE::FormType::Spell:
			{
				const auto* spell = object->As<RE::SpellItem>();
				PopulateSpellData(entryObj, spell);
				SetDefaultSpellIcon(entryObj, spell);
			}
			break;
		case RE::FormType::Shout:
			{
				if (const auto* shout = object->As<RE::TESShout>()) {
					SetString(entryObj, "fullName", shout->GetName());
				}
				SetIconDefaults(entryObj, "default_power");
			}
			break;
		default:
			SetIconDefaults(entryObj, "default_misc");
			break;
		}

		if (trace) {
			I4_LOG_INFO("[I4][TRACE][resolver.entry.pre] form={:08X} type={} sig={} iconSource={} iconLabel={} iconColor={} subType={} weightClass={} resist={} spellType={} formTypeField={}",
				formID,
				formType,
				signature,
				ReadMemberDescription(entryObj, "iconSource"),
				ReadMemberDescription(entryObj, "iconLabel"),
				ReadMemberDescription(entryObj, "iconColor"),
				ReadMemberDescription(entryObj, "subType"),
				ReadMemberDescription(entryObj, "weightClass"),
				ReadMemberDescription(entryObj, "resistance"),
				ReadMemberDescription(entryObj, "spellType"),
				ReadMemberDescription(entryObj, "formType"));
		}

		RE::GFxValue args[1];
		args[0] = entryObj;
		movie->InvokeNoReturn("skse.plugins.InventoryInjector.ProcessEntry", args, 1);

		I4IconSpec spec{};
		spec.iconSource = kDefaultIconSource;

		RE::GFxValue iconSource;
		if (entryObj.GetMember("iconSource", &iconSource) && iconSource.IsString()) {
			spec.iconSource = iconSource.GetString();
		}

		RE::GFxValue iconLabel;
		if (entryObj.GetMember("iconLabel", &iconLabel) && iconLabel.IsString()) {
			spec.iconLabel = iconLabel.GetString();
		}

		RE::GFxValue iconColor;
		std::string iconColorRaw = "<missing>";
		bool iconColorParsed = false;
		if (entryObj.GetMember("iconColor", &iconColor)) {
			iconColorRaw = DescribeGfxValue(iconColor);
			if (auto parsed = ParseColorFromGfx(iconColor); parsed.has_value()) {
				spec.color = ConvertColorValue(parsed.value());
				iconColorParsed = true;
			}
		}

		spec.valid = !spec.iconSource.empty() && !spec.iconLabel.empty();
		if (trace) {
			I4_LOG_INFO("[I4][TRACE][resolver.entry.post] form={:08X} type={} sig={} source='{}' label='{}' iconColorRaw={} iconColorParsed={} color=0x{:08X} valid={} sourceKind={}",
				formID,
				formType,
				signature,
				spec.iconSource,
				spec.iconLabel,
				iconColorRaw,
				iconColorParsed,
				static_cast<std::uint32_t>(spec.color),
				spec.valid,
				IsDefaultSourcePath(spec.iconSource) ? "default" : "custom");
		}
		return spec;
	}

	void I4IconResolver::ClearCache()
	{
		std::scoped_lock lock(_lock);
		if (Config::I4::TraceLog) {
			I4_LOG_INFO("[I4][TRACE][resolver.reset] clearing resolve cache entries={} warnedKeys={}",
				_cache.size(),
				_warned.size());
		}
		_cache.clear();
		_warned.clear();
	}
}
