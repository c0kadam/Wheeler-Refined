#include "WheelItemWeapon.h"
#include "bin/Rendering/Drawer.h"
#include "bin/Utilities/Utils.h"
#include "bin/Utilities/ActorVirtualCompat.h"
#include "bin/Utilities/InventorySnapshotCache.h"
#include "bin/Config.h"
#include "bin/Wheeler/MainWheelDebug.h"
#include "bin/Wheeler/TransformWheelManager.h"
#include "bin/Wheeler/Wheeler.h"
#include "GroupedPoisonLineageDiagnosticPolicy.h"
#include "GroupedPoisonPresentationAliasPolicy.h"
#include "WeaponLiveVisualStatusPolicy.h"
#include "WeaponActiveVisualPolicy.h"
#include "WeaponHandIndicatorPresentationPolicy.h"
#include "WeaponPoisonPresentationPolicy.h"
#include <limits>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <tuple>
#include <string_view>

namespace
{
	std::string NormalizeLoose(std::string_view value)
	{
		std::string normalized;
		normalized.reserve(value.size());
		for (unsigned char ch : value) {
			if (std::isalnum(ch)) {
				normalized.push_back(static_cast<char>(std::tolower(ch)));
			}
		}
		return normalized;
	}

	bool IsPlaceholderName(const char* text)
	{
		if (!text || text[0] == '\0') {
			return true;
		}
		const std::string compact = NormalizeLoose(text);
		return compact.empty() ||
			compact == "null" ||
			compact.find("missingname") != std::string::npos ||
			compact.find("missingitem") != std::string::npos;
	}

	const char* GetWeaponKindName(RE::TESObjectWEAP* a_weapon)
	{
		if (!a_weapon) {
			return "none";
		}
		if (a_weapon->IsBow()) {
			return "bow";
		}
		if (a_weapon->IsCrossbow()) {
			return "crossbow";
		}
		switch (a_weapon->GetWeaponType()) {
		case RE::WEAPON_TYPE::kTwoHandSword:
			return "two_hand_sword";
		case RE::WEAPON_TYPE::kTwoHandAxe:
			return "two_hand_axe";
		default:
			return "weapon";
		}
	}

	RE::InventoryEntryData* FindInventoryEntryByForm(RE::TESObjectREFR::InventoryItemMap& a_inv, RE::FormID a_formID)
	{
		if (a_formID == 0) {
			return nullptr;
		}
		for (auto& [boundObj, data] : a_inv) {
			if (boundObj && boundObj->GetFormID() == a_formID) {
				return data.second.get();
			}
		}
		return nullptr;
	}

	float GetWeaponDamageSafe(RE::PlayerCharacter* a_player, RE::InventoryEntryData* a_entry, RE::TESObjectWEAP* a_weapon)
	{
		if (!a_weapon) {
			return 0.0f;
		}

		float damage = static_cast<float>(a_weapon->GetAttackDamage());
		if (!a_player || !a_entry) {
			return damage;
		}

#if defined(_MSC_VER)
		__try {
			damage = a_player->GetDamage(a_entry);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// Corrupted or stale InventoryEntryData can crash vanilla stat computation.
			// Keep wheel stable by falling back to base weapon damage.
		}
#else
		try {
			damage = a_player->GetDamage(a_entry);
		} catch (...) {
			// Fallback to base damage on any exception.
		}
#endif

		return damage;
	}

	template <class Fn>
	bool InvokeWithSehGuard(Fn&& a_fn)
	{
#if defined(_MSC_VER)
		__try {
			a_fn();
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
#else
		try {
			a_fn();
			return true;
		} catch (...) {
			return false;
		}
#endif
	}

	template <class TContainer>
	bool CopyExtraListsSafe(TContainer* a_extraLists, std::vector<RE::ExtraDataList*>& a_out)
	{
		if (!a_extraLists) {
			return false;
		}

		return InvokeWithSehGuard([&]() {
			for (auto* extraList : *a_extraLists) {
				a_out.push_back(extraList);
			}
		});
	}

	bool TryHasTypeSafe(RE::ExtraDataList* a_list, RE::ExtraDataType a_type, bool& a_outHasType)
	{
		a_outHasType = false;
		if (!a_list) {
			return false;
		}
		return InvokeWithSehGuard([&]() { a_outHasType = a_list->HasType(a_type); });
	}

	template <class T>
	T* GetByTypeSafe(RE::ExtraDataList* a_list)
	{
		T* result = nullptr;
		if (!a_list) {
			return nullptr;
		}
		if (!InvokeWithSehGuard([&]() { result = a_list->GetByType<T>(); })) {
			return nullptr;
		}
		return result;
	}

	bool HasTypeSafe(RE::ExtraDataList* a_list, RE::ExtraDataType a_type)
	{
		bool hasType = false;
		if (!a_list) {
			return false;
		}
		if (!InvokeWithSehGuard([&]() { hasType = a_list->HasType(a_type); })) {
			return false;
		}
		return hasType;
	}

	int GetExtraListCountSafe(RE::ExtraDataList* a_list)
	{
		int count = 1;
		if (!a_list) {
			return 0;
		}
		if (!InvokeWithSehGuard([&]() { count = a_list->GetCount(); })) {
			return 1;
		}
		return (std::max)(count, 1);
	}

	bool TryGetExtraListCount(RE::ExtraDataList* a_list, int& a_outCount)
	{
		a_outCount = 0;
		if (!a_list) {
			return false;
		}
		int count = 0;
		if (!InvokeWithSehGuard([&]() { count = a_list->GetCount(); }) || count <= 0) {
			return false;
		}
		a_outCount = count;
		return true;
	}

	struct LogicalRowMember
	{
		RE::ExtraDataList* extraData = nullptr;
		std::uint16_t uniqueID = 0;
		int count = 0;
		bool wornRight = false;
		bool wornLeft = false;
		bool wornStateReadable = false;
		bool countReadable = false;
		bool signatureReadable = false;
		bool signatureMatch = false;
		std::string signature;
	};

	std::string BuildLogicalRowSignature(RE::ExtraDataList* a_extraData)
	{
		if (!a_extraData) {
			return "none";
		}

		auto* health = GetByTypeSafe<RE::ExtraHealth>(a_extraData);
		auto* enchantment = GetByTypeSafe<RE::ExtraEnchantment>(a_extraData);
		auto* charge = GetByTypeSafe<RE::ExtraCharge>(a_extraData);
		auto* poison = GetByTypeSafe<RE::ExtraPoison>(a_extraData);
		auto* text = GetByTypeSafe<RE::ExtraTextDisplayData>(a_extraData);
		auto* ownership = GetByTypeSafe<RE::ExtraOwnership>(a_extraData);
		auto* hotkey = GetByTypeSafe<RE::ExtraHotkey>(a_extraData);

		const auto enchantmentFormID = enchantment && enchantment->enchantment ? enchantment->enchantment->GetFormID() : 0;
		const auto poisonFormID = poison && poison->poison ? poison->poison->GetFormID() : 0;
		const auto ownerFormID = ownership && ownership->owner ? ownership->owner->GetFormID() : 0;
		const auto hotkeyValue = hotkey ? static_cast<int>(hotkey->hotkey.underlying()) : -2;
		const char* displayName = text ? text->displayName.c_str() : "";

		return fmt::format(
			"health={}:{};enchantment={}:{:08X}:{}:{};charge={}:{};poison={}:{:08X}:{};name={}:{};temper={};owner={}:{:08X};hotkey={}",
			health ? 1 : 0,
			health ? health->health : 0.0F,
			enchantment ? 1 : 0,
			enchantmentFormID,
			enchantment ? enchantment->charge : 0,
			enchantment && enchantment->removeOnUnequip ? 1 : 0,
			charge ? 1 : 0,
			charge ? charge->charge : 0.0F,
			poison ? 1 : 0,
			poisonFormID,
			poison ? poison->count : 0,
			text ? 1 : 0,
			displayName ? displayName : "",
			text ? text->temperFactor : 0.0F,
			ownership ? 1 : 0,
			ownerFormID,
			hotkeyValue);
	}

	const std::string& GetPlainLogicalRowSignature()
	{
		static const std::string signature = fmt::format(
			"health={}:{};enchantment={}:{:08X}:{}:{};charge={}:{};poison={}:{:08X}:{};name={}:{};temper={};owner={}:{:08X};hotkey={}",
			0, 0.0F,
			0, 0U, 0, 0,
			0, 0.0F,
			0, 0U, 0,
			0, "", 0.0F,
			0, 0U,
			-2);
		return signature;
	}

	bool TryBuildReadableLogicalRowSignature(RE::ExtraDataList* a_extraData, std::string& a_outSignature)
	{
		a_outSignature.clear();
		if (!a_extraData) {
			return false;
		}

		constexpr std::array signatureTypes{
			RE::ExtraDataType::kHealth,
			RE::ExtraDataType::kEnchantment,
			RE::ExtraDataType::kCharge,
			RE::ExtraDataType::kPoison,
			RE::ExtraDataType::kTextDisplayData,
			RE::ExtraDataType::kOwnership,
			RE::ExtraDataType::kHotkey
		};
		for (const auto type : signatureTypes) {
			bool hasType = false;
			if (!TryHasTypeSafe(a_extraData, type, hasType)) {
				return false;
			}
			if (!hasType) {
				continue;
			}
			switch (type) {
			case RE::ExtraDataType::kHealth:
				if (!GetByTypeSafe<RE::ExtraHealth>(a_extraData)) return false;
				break;
			case RE::ExtraDataType::kEnchantment:
				if (!GetByTypeSafe<RE::ExtraEnchantment>(a_extraData)) return false;
				break;
			case RE::ExtraDataType::kCharge:
				if (!GetByTypeSafe<RE::ExtraCharge>(a_extraData)) return false;
				break;
			case RE::ExtraDataType::kPoison:
				if (!GetByTypeSafe<RE::ExtraPoison>(a_extraData)) return false;
				break;
			case RE::ExtraDataType::kTextDisplayData:
				if (!GetByTypeSafe<RE::ExtraTextDisplayData>(a_extraData)) return false;
				break;
			case RE::ExtraDataType::kOwnership:
				if (!GetByTypeSafe<RE::ExtraOwnership>(a_extraData)) return false;
				break;
			case RE::ExtraDataType::kHotkey:
				if (!GetByTypeSafe<RE::ExtraHotkey>(a_extraData)) return false;
				break;
			default:
				return false;
			}
		}
		return InvokeWithSehGuard([&]() {
			auto* health = a_extraData->GetByType<RE::ExtraHealth>();
			auto* enchantment = a_extraData->GetByType<RE::ExtraEnchantment>();
			auto* charge = a_extraData->GetByType<RE::ExtraCharge>();
			auto* poison = a_extraData->GetByType<RE::ExtraPoison>();
			auto* text = a_extraData->GetByType<RE::ExtraTextDisplayData>();
			auto* ownership = a_extraData->GetByType<RE::ExtraOwnership>();
			auto* hotkey = a_extraData->GetByType<RE::ExtraHotkey>();
			const auto enchantmentFormID =
				enchantment && enchantment->enchantment ? enchantment->enchantment->GetFormID() : 0;
			const auto poisonFormID = poison && poison->poison ? poison->poison->GetFormID() : 0;
			const auto ownerFormID = ownership && ownership->owner ? ownership->owner->GetFormID() : 0;
			const auto hotkeyValue = hotkey ? static_cast<int>(hotkey->hotkey.underlying()) : -2;
			const char* displayName = text ? text->displayName.c_str() : "";
			a_outSignature = fmt::format(
				"health={}:{};enchantment={}:{:08X}:{}:{};charge={}:{};poison={}:{:08X}:{};name={}:{};temper={};owner={}:{:08X};hotkey={}",
				health ? 1 : 0,
				health ? health->health : 0.0F,
				enchantment ? 1 : 0,
				enchantmentFormID,
				enchantment ? enchantment->charge : 0,
				enchantment && enchantment->removeOnUnequip ? 1 : 0,
				charge ? 1 : 0,
				charge ? charge->charge : 0.0F,
				poison ? 1 : 0,
				poisonFormID,
				poison ? poison->count : 0,
				text ? 1 : 0,
				displayName ? displayName : "",
				text ? text->temperFactor : 0.0F,
				ownership ? 1 : 0,
				ownerFormID,
				hotkeyValue);
		});
	}

	std::vector<LogicalRowMember> CollectLogicalRowMembers(
		RE::TESObjectREFR::InventoryItemMap& a_inv,
		RE::FormID a_formID,
		std::uint16_t a_uniqueID,
		std::string_view a_logicalRowSignature)
	{
		std::vector<LogicalRowMember> members;
		if (a_formID == 0 || a_uniqueID == 0) {
			return members;
		}

		auto* entry = FindInventoryEntryByForm(a_inv, a_formID);
		if (!entry || !entry->extraLists) {
			return members;
		}

		std::vector<RE::ExtraDataList*> extraListSnapshot;
		if (!CopyExtraListsSafe(entry->extraLists, extraListSnapshot)) {
			return members;
		}

		for (auto* extraData : extraListSnapshot) {
			if (!extraData) {
				continue;
			}

			auto* uniqueData = GetByTypeSafe<RE::ExtraUniqueID>(extraData);
			if (!uniqueData || uniqueData->uniqueID != a_uniqueID) {
				continue;
			}

			LogicalRowMember member;
			member.extraData = extraData;
			member.uniqueID = uniqueData->uniqueID;
			member.count = GetExtraListCountSafe(extraData);
			const bool readWorn = TryHasTypeSafe(extraData, RE::ExtraDataType::kWorn, member.wornRight);
			const bool readWornLeft = TryHasTypeSafe(extraData, RE::ExtraDataType::kWornLeft, member.wornLeft);
			member.wornStateReadable = readWorn && readWornLeft;
			member.signature = BuildLogicalRowSignature(extraData);
			member.signatureMatch = a_logicalRowSignature.empty() || member.signature == a_logicalRowSignature;
			members.push_back(std::move(member));
		}

		return members;
	}

	std::string CaptureLogicalRowSignature(RE::TESObjectWEAP* a_weapon, std::uint16_t a_uniqueID)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !a_weapon || a_uniqueID == 0) {
			return {};
		}

		auto inv = player->GetInventory();
		auto members = CollectLogicalRowMembers(inv, a_weapon->GetFormID(), a_uniqueID, {});
		return members.empty() ? std::string{} : members.front().signature;
	}

	struct LogicalRowResolution
	{
		RE::ExtraDataList* chosen = nullptr;
		int logicalCount = 0;
		std::vector<LogicalRowMember> members;
	};

	LogicalRowResolution ResolveLogicalRowMember(
		RE::TESObjectREFR::InventoryItemMap& a_inv,
		RE::FormID a_formID,
		std::uint16_t a_uniqueID,
		std::string_view a_logicalRowSignature,
		bool a_targetRight,
		bool a_forUnequip)
	{
		LogicalRowResolution result;
		result.members = CollectLogicalRowMembers(a_inv, a_formID, a_uniqueID, a_logicalRowSignature);
		for (const auto& member : result.members) {
			result.logicalCount += member.count;
		}

		auto choose = [&](auto&& a_predicate) {
			if (result.chosen) {
				return;
			}
			for (const auto& member : result.members) {
				if (a_predicate(member)) {
					result.chosen = member.extraData;
					return;
				}
			}
		};

		auto wornInTargetHand = [&](const LogicalRowMember& a_member) {
			return a_member.wornStateReadable &&
			       (a_targetRight ? (a_member.wornRight && !a_member.wornLeft) : a_member.wornLeft);
		};
		auto completelyUnworn = [](const LogicalRowMember& a_member) {
			return a_member.wornStateReadable && !a_member.wornRight && !a_member.wornLeft;
		};

		if (a_forUnequip) {
			choose([&](const auto& member) { return member.signatureMatch && wornInTargetHand(member); });
			choose([&](const auto& member) { return wornInTargetHand(member); });
		} else {
			choose([&](const auto& member) { return member.signatureMatch && completelyUnworn(member); });
			choose([&](const auto& member) { return completelyUnworn(member); });
			choose([&](const auto& member) { return member.signatureMatch && wornInTargetHand(member); });
			choose([&](const auto& member) { return wornInTargetHand(member); });
		}

		return result;
	}

	bool IsPerInstanceModifiedExtraData(RE::ExtraDataList* a_extraData)
	{
		if (!a_extraData) {
			return false;
		}
		return HasTypeSafe(a_extraData, RE::ExtraDataType::kEnchantment) ||
		       HasTypeSafe(a_extraData, RE::ExtraDataType::kPoison) ||
		       HasTypeSafe(a_extraData, RE::ExtraDataType::kHealth) ||
		       HasTypeSafe(a_extraData, RE::ExtraDataType::kCharge) ||
		       GetByTypeSafe<RE::ExtraTextDisplayData>(a_extraData) != nullptr;
	}

	int GetSameFormInventoryCount(const RE::TESObjectREFR::InventoryItemMap& a_inv, RE::TESObjectWEAP* a_weapon);

	bool TryReadPoisonPresentationMember(
		RE::ExtraDataList* a_list, WeaponPoisonPresentationPolicy::MemberEvidence& a_member)
	{
		bool complete = false;
		const bool guarded = InvokeWithSehGuard([&]() {
			if (!TryGetExtraListCount(a_list, a_member.count)) return;
			bool hasUID = false;
			if (!TryHasTypeSafe(a_list, RE::ExtraDataType::kUniqueID, hasUID) ||
			    !TryHasTypeSafe(a_list, RE::ExtraDataType::kPoison, a_member.hasPoison)) return;
			if (hasUID) {
				const auto* uid = a_list->GetByType<RE::ExtraUniqueID>();
				if (!uid) return;
				a_member.uniqueID = uid->uniqueID;
			}
			if (a_member.hasPoison) {
				const auto* extra = a_list->GetByType<RE::ExtraPoison>();
				if (!extra) return;
				a_member.poisonCount = extra->count;
				a_member.poisonPointerValid = extra->poison != nullptr;
				if (extra->poison) a_member.poisonFormID = extra->poison->GetFormID();
			}
			complete = true;
		});
		a_member.readable = guarded && complete;
		return a_member.readable;
	}

	WeaponPoisonPresentationPolicy::WeaponPoisonPresentation ResolveWeaponPoisonPresentation(
		const RE::TESObjectREFR::InventoryItemMap& a_inv, RE::FormID a_weaponFormID, std::uint16_t a_uid)
	{
		using namespace WeaponPoisonPresentationPolicy;
		if (a_weaponFormID == 0) return {};
		int sameFormCount = 0;
		std::vector<MemberEvidence> members;
		bool complete = false;
		const bool guarded = InvokeWithSehGuard([&]() {
			for (const auto& [object, data] : a_inv) {
				if (!object || object->GetFormID() != a_weaponFormID) continue;
				if (data.first < 0 || data.first > (std::numeric_limits<int>::max)() - sameFormCount) return;
				sameFormCount += data.first;
				if (data.first == 0) continue;
				if (!data.second) return;
				if (!data.second->extraLists) continue;  // Implicit quantity is not poison evidence.
				std::vector<RE::ExtraDataList*> lists;
				if (!CopyExtraListsSafe(data.second->extraLists, lists)) return;
				for (auto* list : lists) {
					MemberEvidence member;
					if (!TryReadPoisonPresentationMember(list, member)) return;
					members.push_back(member);
				}
			}
			complete = true;
		});
		const auto result = Resolve(sameFormCount, a_uid, guarded && complete, members);
		if (!result.safelyResolved) return {};
		bool validForm = false;
		const bool lookupReadable = InvokeWithSehGuard([&]() {
			const auto* poison = RE::TESForm::LookupByID<RE::AlchemyItem>(result.poisonFormID);
			validForm = poison && poison->GetFormID() == result.poisonFormID;
		});
		return ValidateForm(result, lookupReadable && validForm);
	}

	std::string AppendWeaponPoisonHighlightText(
		std::string a_existing, const WeaponPoisonPresentationPolicy::WeaponPoisonPresentation& a_poison)
	{
		if (!a_poison.safelyResolved) return a_existing;
		std::string name;
		std::string effects;
		bool complete = false;
		const bool guarded = InvokeWithSehGuard([&]() {
			auto* poison = RE::TESForm::LookupByID<RE::AlchemyItem>(a_poison.poisonFormID);
			if (!poison || poison->GetFormID() != a_poison.poisonFormID) return;
			const char* rawName = poison->GetName();
			if (!IsPlaceholderName(rawName)) name = rawName;
			// Do not require another bottle in inventory, including for crafted forms.
			Utils::Magic::GetMagicItemDescription(poison, effects);
			complete = true;
		});
		if (!guarded || !complete) return a_existing;
		return WeaponPoisonPresentationPolicy::AppendDescription(std::move(a_existing), name, effects);
	}

	bool TryReadLiveVisualStatusMember(
		RE::ExtraDataList* a_extraData,
		WeaponLiveVisualStatusPolicy::MemberEvidence& a_outMember)
	{
		if (!a_extraData || !TryGetExtraListCount(a_extraData, a_outMember.count)) {
			return false;
		}

		bool hasUniqueID = false;
		if (!TryHasTypeSafe(a_extraData, RE::ExtraDataType::kUniqueID, hasUniqueID)) {
			return false;
		}
		if (hasUniqueID) {
			auto* uniqueID = GetByTypeSafe<RE::ExtraUniqueID>(a_extraData);
			if (!uniqueID) {
				return false;
			}
			a_outMember.uniqueID = uniqueID->uniqueID;
		}

		bool hasPoison = false;
		if (!TryHasTypeSafe(a_extraData, RE::ExtraDataType::kPoison, hasPoison)) {
			return false;
		}
		if (hasPoison) {
			auto* poison = GetByTypeSafe<RE::ExtraPoison>(a_extraData);
			if (!poison) {
				return false;
			}
			a_outMember.poisoned = poison->poison != nullptr && poison->count > 0;
		}

		bool hasEnchantment = false;
		if (!TryHasTypeSafe(a_extraData, RE::ExtraDataType::kEnchantment, hasEnchantment)) {
			return false;
		}
		if (hasEnchantment) {
			auto* enchantment = GetByTypeSafe<RE::ExtraEnchantment>(a_extraData);
			if (!enchantment) {
				return false;
			}
			a_outMember.extraEnchanted = enchantment->enchantment != nullptr;
		}

		a_outMember.statusReadable = true;
		return true;
	}

	WeaponLiveVisualStatusPolicy::WeaponLiveVisualStatus ResolveWeaponLiveVisualStatus(
		RE::TESObjectREFR::InventoryItemMap& a_inv,
		RE::TESObjectWEAP* a_weapon,
		std::uint16_t a_storedUniqueID)
	{
		using namespace WeaponLiveVisualStatusPolicy;

		if (!a_weapon) {
			return {};
		}

		std::vector<MemberEvidence> members;
		bool enumerationReadable = true;
		const auto formID = a_weapon->GetFormID();
		const int sameFormCount = GetSameFormInventoryCount(a_inv, a_weapon);
		for (auto& [boundObj, data] : a_inv) {
			if (!boundObj || boundObj->GetFormID() != formID || data.first <= 0 ||
			    !data.second || !data.second->extraLists) {
				continue;
			}

			std::vector<RE::ExtraDataList*> extraListSnapshot;
			if (!CopyExtraListsSafe(data.second->extraLists, extraListSnapshot)) {
				enumerationReadable = false;
				break;
			}
			for (auto* extraData : extraListSnapshot) {
				MemberEvidence member{};
				if (!TryReadLiveVisualStatusMember(extraData, member)) {
					enumerationReadable = false;
					break;
				}
				members.push_back(member);
			}
			if (!enumerationReadable) {
				break;
			}
		}

		return WeaponLiveVisualStatusPolicy::Resolve({
			sameFormCount,
			a_storedUniqueID,
			enumerationReadable,
			a_weapon->formEnchanting != nullptr,
			std::span<const MemberEvidence>{ members }
		});
	}

	using GroupedPoisonLineageDiagnosticPolicy::Hand;
	using GroupedPoisonLineageDiagnosticPolicy::Lineage;
	using GroupedPoisonLineageDiagnosticPolicy::SnapshotSummary;
	using GroupedPoisonLineageDiagnosticPolicy::Topology;
	using GroupedPoisonPresentationAliasPolicy::Presentation;
	using GroupedPoisonPresentationAliasPolicy::ValidationEvidence;
	using GroupedPoisonPresentationAliasPolicy::ValidationFailure;

	struct GroupedPoisonDiagnosticMember
	{
		std::uintptr_t ephemeralAddress = 0;
		std::uint16_t uniqueID = 0;
		int count = 0;
		bool readable = false;
		bool worn = false;
		bool wornLeft = false;
		bool poisoned = false;
		bool enchanted = false;
		bool hasHealth = false;
		float health = 0.0F;
		bool modified = false;
		std::uint32_t poisonFormID = 0;
		std::uint32_t poisonCount = 0;
		std::uint64_t logicalSignatureDigest = 0;
		std::uint64_t nonPoisonSignatureDigest = 0;
	};

	struct GroupedPoisonDiagnosticSnapshot
	{
		SnapshotSummary summary{};
		std::vector<GroupedPoisonDiagnosticMember> members;
	};

	constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
	constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

	std::uint64_t HashGroupedPoisonText(std::string_view a_text)
	{
		std::uint64_t digest = kFnvOffsetBasis;
		for (const unsigned char value : a_text) {
			digest ^= value;
			digest *= kFnvPrime;
		}
		return digest;
	}

	void MixGroupedPoisonDigest(std::uint64_t& a_digest, std::uint64_t a_value)
	{
		for (std::uint32_t shift = 0; shift < 64; shift += 8) {
			a_digest ^= static_cast<std::uint8_t>(a_value >> shift);
			a_digest *= kFnvPrime;
		}
	}

	std::uint64_t BuildGroupedPoisonSlotDigest(
		std::uint64_t a_runtimeSlotID,
		RE::FormID a_formID,
		Hand a_hand,
		std::uint64_t a_beforeSignature)
	{
		auto digest = kFnvOffsetBasis;
		MixGroupedPoisonDigest(digest, a_runtimeSlotID);
		MixGroupedPoisonDigest(digest, a_formID);
		MixGroupedPoisonDigest(digest, a_hand == Hand::Left ? 1 : 0);
		MixGroupedPoisonDigest(digest, a_beforeSignature);
		return digest;
	}

	std::atomic_uint64_t g_nextWeaponPresentationSlotID{ 0 };

	std::uint64_t AllocateWeaponPresentationSlotID()
	{
		auto value = ++g_nextWeaponPresentationSlotID;
		if (value == 0) {
			value = ++g_nextWeaponPresentationSlotID;
		}
		return value;
	}

	double GetGroupedPoisonDiagnosticTime()
	{
		return std::chrono::duration<double>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	const char* GetGroupedPoisonTopologyName(Topology a_topology)
	{
		switch (a_topology) {
		case Topology::Empty:
			return "empty";
		case Topology::SingleLogicalRow:
			return "single_logical_row";
		case Topology::MixedLogicalRows:
			return "mixed_logical_rows";
		case Topology::Unknown:
		default:
			return "unknown";
		}
	}

	const char* GetGroupedPoisonLineageName(Lineage a_lineage)
	{
		switch (a_lineage) {
		case Lineage::StrongUniqueID:
			return "STRONG_UID_LINEAGE";
		case Lineage::StrongHandTransition:
			return "STRONG_HAND_TRANSITION";
		case Lineage::Ambiguous:
		default:
			return "AMBIGUOUS";
		}
	}

	bool TryCaptureGroupedPoisonMember(
		RE::ExtraDataList* a_extraData,
		GroupedPoisonDiagnosticMember& a_out)
	{
		a_out.ephemeralAddress = reinterpret_cast<std::uintptr_t>(a_extraData);
		if (!a_extraData || !TryGetExtraListCount(a_extraData, a_out.count)) {
			return false;
		}

		bool hasUniqueID = false;
		bool hasPoison = false;
		bool hasEnchantment = false;
		bool hasHealth = false;
		if (!TryHasTypeSafe(a_extraData, RE::ExtraDataType::kUniqueID, hasUniqueID) ||
		    !TryHasTypeSafe(a_extraData, RE::ExtraDataType::kWorn, a_out.worn) ||
		    !TryHasTypeSafe(a_extraData, RE::ExtraDataType::kWornLeft, a_out.wornLeft) ||
		    !TryHasTypeSafe(a_extraData, RE::ExtraDataType::kPoison, hasPoison) ||
		    !TryHasTypeSafe(a_extraData, RE::ExtraDataType::kEnchantment, hasEnchantment) ||
		    !TryHasTypeSafe(a_extraData, RE::ExtraDataType::kHealth, hasHealth)) {
			return false;
		}

		if (hasUniqueID) {
			auto* uniqueID = GetByTypeSafe<RE::ExtraUniqueID>(a_extraData);
			if (!uniqueID) {
				return false;
			}
			a_out.uniqueID = uniqueID->uniqueID;
		}

		auto* poison = hasPoison ? GetByTypeSafe<RE::ExtraPoison>(a_extraData) : nullptr;
		if (hasPoison && !poison) {
			return false;
		}
		a_out.poisoned = poison && poison->poison && poison->count > 0;
		a_out.poisonFormID = poison && poison->poison ? poison->poison->GetFormID() : 0;
		a_out.poisonCount = poison ? poison->count : 0;

		auto* enchantment = hasEnchantment ? GetByTypeSafe<RE::ExtraEnchantment>(a_extraData) : nullptr;
		if (hasEnchantment && !enchantment) {
			return false;
		}
		a_out.enchanted = enchantment && enchantment->enchantment;

		auto* health = hasHealth ? GetByTypeSafe<RE::ExtraHealth>(a_extraData) : nullptr;
		if (hasHealth && !health) {
			return false;
		}
		a_out.hasHealth = health != nullptr;
		a_out.health = health ? health->health : 0.0F;

		std::string logicalSignature;
		if (!TryBuildReadableLogicalRowSignature(a_extraData, logicalSignature)) {
			return false;
		}
		a_out.logicalSignatureDigest = HashGroupedPoisonText(logicalSignature);
		a_out.modified = logicalSignature != GetPlainLogicalRowSignature();

		auto* charge = GetByTypeSafe<RE::ExtraCharge>(a_extraData);
		auto* text = GetByTypeSafe<RE::ExtraTextDisplayData>(a_extraData);
		auto* ownership = GetByTypeSafe<RE::ExtraOwnership>(a_extraData);
		auto* hotkey = GetByTypeSafe<RE::ExtraHotkey>(a_extraData);
		const auto enchantmentFormID = enchantment && enchantment->enchantment ?
			enchantment->enchantment->GetFormID() : 0;
		const auto ownerFormID = ownership && ownership->owner ? ownership->owner->GetFormID() : 0;
		const char* displayName = text ? text->displayName.c_str() : "";
		const auto nonPoisonSignature = fmt::format(
			"health={}:{};enchantment={}:{:08X}:{}:{};charge={}:{};name={}:{};temper={};owner={}:{:08X};hotkey={}",
			health ? 1 : 0,
			health ? health->health : 0.0F,
			enchantment ? 1 : 0,
			enchantmentFormID,
			enchantment ? enchantment->charge : 0,
			enchantment && enchantment->removeOnUnequip ? 1 : 0,
			charge ? 1 : 0,
			charge ? charge->charge : 0.0F,
			text ? 1 : 0,
			displayName ? displayName : "",
			text ? text->temperFactor : 0.0F,
			ownership ? 1 : 0,
			ownerFormID,
			hotkey ? static_cast<int>(hotkey->hotkey.underlying()) : -2);
		a_out.nonPoisonSignatureDigest = HashGroupedPoisonText(nonPoisonSignature);
		a_out.readable = true;
		return true;
	}

	GroupedPoisonDiagnosticSnapshot CaptureGroupedPoisonTargetSnapshot(
		RE::PlayerCharacter* a_player,
		RE::FormID a_formID,
		Hand a_targetHand)
	{
		GroupedPoisonDiagnosticSnapshot result;
		if (!a_player || !a_player->Is3DLoaded() || a_formID == 0) {
			return result;
		}

		const bool targetLeft = a_targetHand == Hand::Left;
		auto* targetEquipped = a_player->GetEquippedObject(targetLeft);
		auto* otherEquipped = a_player->GetEquippedObject(!targetLeft);
		result.summary.targetHandHasForm = targetEquipped && targetEquipped->GetFormID() == a_formID;
		result.summary.otherHandHasSameForm = otherEquipped && otherEquipped->GetFormID() == a_formID;

		RE::TESObjectREFR::InventoryItemMap inventory;
		if (!Utils::Inventory::TryGetInventorySnapshot(
				a_player, inventory, "GroupedPoisonLineageDiagnostic")) {
			return result;
		}

		RE::InventoryEntryData* entry = nullptr;
		for (auto& [boundObject, data] : inventory) {
			if (boundObject && boundObject->GetFormID() == a_formID) {
				result.summary.totalInventoryCount = (std::max)(data.first, 0);
				entry = data.second.get();
				break;
			}
		}

		if (!entry || !entry->extraLists) {
			result.summary.implicitPlainCount = result.summary.totalInventoryCount;
			result.summary.cleanItemCount = result.summary.totalInventoryCount;
			result.summary.readable = true;
			result.summary.topology = result.summary.totalInventoryCount == 0 ?
				Topology::Empty : Topology::SingleLogicalRow;
			result.summary.topologyDigest = kFnvOffsetBasis;
			MixGroupedPoisonDigest(result.summary.topologyDigest,
				static_cast<std::uint64_t>(result.summary.totalInventoryCount));
			inventory.clear();
			return result;
		}

		std::vector<RE::ExtraDataList*> extraLists;
		if (!CopyExtraListsSafe(entry->extraLists, extraLists)) {
			inventory.clear();
			return result;
		}

		int targetIndex = -1;
		std::vector<std::uint64_t> logicalDigests;
		for (auto* extraData : extraLists) {
			GroupedPoisonDiagnosticMember member;
			result.summary.extraListCount++;
			if (!TryCaptureGroupedPoisonMember(extraData, member)) {
				result.summary.hasUnreadableMember = true;
				result.members.push_back(member);
				continue;
			}
			const auto memberIndex = static_cast<int>(result.members.size());
			result.summary.representedItemCount += member.count;
			if (member.modified) {
				result.summary.modifiedItemCount += member.count;
			} else {
				result.summary.cleanItemCount += member.count;
			}
			if (member.poisoned) {
				result.summary.poisonedMemberCount++;
				result.summary.poisonedItemCount += member.count;
			}
			const bool wornInTargetHand = targetLeft ? member.wornLeft : (member.worn && !member.wornLeft);
			if (wornInTargetHand) {
				result.summary.targetWornMemberCount++;
				targetIndex = memberIndex;
			}
			logicalDigests.push_back(member.logicalSignatureDigest);
			result.members.push_back(member);
		}

		result.summary.implicitPlainCount = (std::max)(
			0, result.summary.totalInventoryCount - result.summary.representedItemCount);
		if (result.summary.representedItemCount > result.summary.totalInventoryCount) {
			result.summary.hasUnreadableMember = true;
		}
		result.summary.cleanItemCount += result.summary.implicitPlainCount;
		if (result.summary.implicitPlainCount > 0) {
			logicalDigests.push_back(HashGroupedPoisonText(GetPlainLogicalRowSignature()));
		}
		std::sort(logicalDigests.begin(), logicalDigests.end());
		logicalDigests.erase(std::unique(logicalDigests.begin(), logicalDigests.end()), logicalDigests.end());
		if (result.summary.totalInventoryCount == 0) {
			result.summary.topology = Topology::Empty;
		} else if (!result.summary.hasUnreadableMember && logicalDigests.size() <= 1) {
			result.summary.topology = Topology::SingleLogicalRow;
		} else if (!result.summary.hasUnreadableMember) {
			result.summary.topology = Topology::MixedLogicalRows;
		}

		result.summary.targetMemberUnique = result.summary.targetWornMemberCount == 1 && targetIndex >= 0;
		if (result.summary.targetMemberUnique) {
			const auto& target = result.members[static_cast<std::size_t>(targetIndex)];
			result.summary.targetWornUniqueID = target.uniqueID;
			result.summary.targetWornXListAddress = target.ephemeralAddress;
			result.summary.targetMemberPoisoned = target.poisoned;
			result.summary.targetMemberEnchanted = target.enchanted;
			result.summary.targetPoisonFormID = target.poisonFormID;
			result.summary.targetLogicalSignatureDigest = target.logicalSignatureDigest;
			result.summary.targetNonPoisonSignatureDigest = target.nonPoisonSignatureDigest;
			result.summary.otherModifiedItemCount = result.summary.modifiedItemCount -
				(target.modified ? target.count : 0);
			if (target.uniqueID != 0) {
				const auto occurrences = std::count_if(
					result.members.begin(), result.members.end(), [&](const auto& member) {
						return member.readable && member.uniqueID == target.uniqueID;
					});
				result.summary.targetUniqueIDGloballyUnique = occurrences == 1;
			}
		} else {
			result.summary.otherModifiedItemCount = result.summary.modifiedItemCount;
		}
		result.summary.exactlyOnePoisonedMember =
			result.summary.poisonedMemberCount == 1 && result.summary.poisonedItemCount == 1;
		result.summary.readable = !result.summary.hasUnreadableMember;

		std::sort(result.members.begin(), result.members.end(), [](const auto& a_lhs, const auto& a_rhs) {
			return std::tie(a_lhs.uniqueID, a_lhs.wornLeft, a_lhs.worn, a_lhs.logicalSignatureDigest,
				a_lhs.count, a_lhs.ephemeralAddress) <
			       std::tie(a_rhs.uniqueID, a_rhs.wornLeft, a_rhs.worn, a_rhs.logicalSignatureDigest,
				a_rhs.count, a_rhs.ephemeralAddress);
		});
		result.summary.topologyDigest = kFnvOffsetBasis;
		MixGroupedPoisonDigest(result.summary.topologyDigest,
			static_cast<std::uint64_t>(result.summary.totalInventoryCount));
		MixGroupedPoisonDigest(result.summary.topologyDigest,
			static_cast<std::uint64_t>(result.summary.implicitPlainCount));
		for (const auto& member : result.members) {
			MixGroupedPoisonDigest(result.summary.topologyDigest, member.readable ? 1 : 0);
			MixGroupedPoisonDigest(result.summary.topologyDigest, member.uniqueID);
			MixGroupedPoisonDigest(result.summary.topologyDigest, static_cast<std::uint64_t>(member.count));
			MixGroupedPoisonDigest(result.summary.topologyDigest, member.worn ? 1 : 0);
			MixGroupedPoisonDigest(result.summary.topologyDigest, member.wornLeft ? 1 : 0);
			MixGroupedPoisonDigest(result.summary.topologyDigest, member.logicalSignatureDigest);
		}
		inventory.clear();
		return result;
	}

	void LogGroupedPoisonSnapshot(
		const char* a_phase,
		std::uint64_t a_transactionID,
		std::uint64_t a_updateSequence,
		RE::FormID a_formID,
		Hand a_targetHand,
		const GroupedPoisonDiagnosticSnapshot& a_snapshot)
	{
		const auto& summary = a_snapshot.summary;
		logger::debug(
			"GROUPED_POISON {} tx={} update={} form={:08X} hand={} readable={} total={} represented={} extraLists={} implicitPlain={} clean={} modified={} poisonedMembers={} poisonedItems={} targetWornMembers={} targetUID={} targetUIDGloballyUnique={} targetPoisoned={} otherHandSameForm={} topology={} topologyDigest={:016X}",
			a_phase,
			a_transactionID,
			a_updateSequence,
			a_formID,
			a_targetHand == Hand::Left ? "LEFT" : "RIGHT",
			summary.readable ? 1 : 0,
			summary.totalInventoryCount,
			summary.representedItemCount,
			summary.extraListCount,
			summary.implicitPlainCount,
			summary.cleanItemCount,
			summary.modifiedItemCount,
			summary.poisonedMemberCount,
			summary.poisonedItemCount,
			summary.targetWornMemberCount,
			summary.targetWornUniqueID,
			summary.targetUniqueIDGloballyUnique ? 1 : 0,
			summary.targetMemberPoisoned ? 1 : 0,
			summary.otherHandHasSameForm ? 1 : 0,
			GetGroupedPoisonTopologyName(summary.topology),
			summary.topologyDigest);
		for (std::size_t index = 0; index < a_snapshot.members.size(); ++index) {
			const auto& member = a_snapshot.members[index];
			logger::debug(
				"GROUPED_POISON {} tx={} member={} ephemeralXList={:016X} loggingOnly=1 readable={} uid={} count={} worn={} wornLeft={} poison={} poisonForm={:08X} poisonCount={} enchant={} health={} healthValue={} logicalSig={:016X} nonPoisonSig={:016X}",
				a_phase,
				a_transactionID,
				index,
				static_cast<std::uint64_t>(member.ephemeralAddress),
				member.readable ? 1 : 0,
				member.uniqueID,
				member.count,
				member.worn ? 1 : 0,
				member.wornLeft ? 1 : 0,
				member.poisoned ? 1 : 0,
				member.poisonFormID,
				member.poisonCount,
				member.enchanted ? 1 : 0,
				member.hasHealth ? 1 : 0,
				member.health,
				member.logicalSignatureDigest,
				member.nonPoisonSignatureDigest);
		}
	}

	GroupedPoisonLineageDiagnosticPolicy::Watcher g_groupedPoisonWatcher{};
	std::uint64_t g_nextGroupedPoisonTransactionID = 0;
	std::uint64_t g_groupedPoisonUpdateSequence = 0;
	GroupedPoisonPresentationAliasPolicy::Alias g_groupedPoisonPresentationAlias{};
	std::uint64_t g_nextGroupedPoisonAliasGeneration = 0;

	const char* GetGroupedPoisonAliasFailureName(ValidationFailure a_failure)
	{
		switch (a_failure) {
		case ValidationFailure::UnreadableTopology: return "unreadable_topology";
		case ValidationFailure::SlotMismatch: return "slot_rebound";
		case ValidationFailure::FormMismatch: return "form_changed";
		case ValidationFailure::EpochChanged: return "lifecycle_epoch_changed";
		case ValidationFailure::StoredIdentityChanged: return "stored_identity_changed";
		case ValidationFailure::TargetHandChanged: return "target_hand_unequipped_or_changed";
		case ValidationFailure::OppositeHandSameForm: return "opposite_hand_same_form";
		case ValidationFailure::TargetMemberAmbiguous: return "target_member_ambiguous";
		case ValidationFailure::PoisonDisappeared: return "poison_disappeared";
		case ValidationFailure::PoisonPopulationAmbiguous: return "poison_population_ambiguous";
		case ValidationFailure::CompetingModifiedMember: return "competing_modified_member";
		case ValidationFailure::CountChanged: return "same_form_count_changed";
		case ValidationFailure::SignatureChanged: return "logical_signature_changed";
		case ValidationFailure::PoisonFormChanged: return "poison_form_changed";
		case ValidationFailure::Inactive: return "inactive";
		case ValidationFailure::None:
		default: return "none";
		}
	}

	void DropGroupedPoisonPresentationAlias(const char* a_reason)
	{
		if (g_groupedPoisonPresentationAlias.IsActive()) {
			logger::debug(
				"GROUPED_POISON_ALIAS DROP slotDigest={:016X} form={:08X} hand={} epoch={} generation={} beforeSig={:016X} afterSig={:016X} reason={}",
				g_groupedPoisonPresentationAlias.slotDigest,
				g_groupedPoisonPresentationAlias.formID,
				g_groupedPoisonPresentationAlias.hand == Hand::Left ? "LEFT" : "RIGHT",
				g_groupedPoisonPresentationAlias.epoch,
				g_groupedPoisonPresentationAlias.generation,
				g_groupedPoisonPresentationAlias.beforeSignature,
				g_groupedPoisonPresentationAlias.afterSignature,
				a_reason ? a_reason : "unspecified");
		}
		g_groupedPoisonPresentationAlias.Clear();
	}

	void DropGroupedPoisonPresentationAliasForSlot(
		std::uint64_t a_runtimeSlotID,
		const char* a_reason)
	{
		if (g_groupedPoisonPresentationAlias.IsActive() &&
		    g_groupedPoisonPresentationAlias.runtimeSlotID == a_runtimeSlotID) {
			DropGroupedPoisonPresentationAlias(a_reason);
		}
	}

	void TryCreateGroupedPoisonPresentationAlias(
		Lineage a_lineage,
		const SnapshotSummary& a_before,
		const SnapshotSummary& a_after)
	{
		GroupedPoisonPresentationAliasPolicy::CreationEvidence evidence;
		evidence.lineage = a_lineage;
		evidence.originatedFromUID0GroupedFallback =
			g_groupedPoisonWatcher.originatedFromUID0GroupedFallback;
		evidence.epochMatches =
			g_groupedPoisonWatcher.epoch == Wheeler::GetTransientRestorationEpoch();
		evidence.slotSignatureMatchesBefore =
			g_groupedPoisonWatcher.preEquipLogicalSignatureDigest ==
			a_before.targetLogicalSignatureDigest;
		evidence.readable = a_before.readable && a_after.readable &&
			!a_before.hasUnreadableMember && !a_after.hasUnreadableMember;
		evidence.targetHandHasForm = a_after.targetHandHasForm;
		evidence.otherHandHasSameForm = a_after.otherHandHasSameForm;
		evidence.targetMemberUnique = a_after.targetMemberUnique;
		evidence.targetMemberPoisoned = a_after.targetMemberPoisoned;
		evidence.exactlyOnePoisonedMember = a_after.exactlyOnePoisonedMember;
		evidence.countConserved =
			a_before.totalInventoryCount == a_after.totalInventoryCount;
		evidence.hasCompetingModifiedMember =
			a_before.otherModifiedItemCount != 0 || a_after.otherModifiedItemCount != 0;
		evidence.preMutationUID = a_before.targetWornUniqueID;
		evidence.runtimeSlotID = g_groupedPoisonWatcher.runtimeSlotID;
		evidence.slotDigest = g_groupedPoisonWatcher.slotDigest;
		evidence.formID = g_groupedPoisonWatcher.formID;
		evidence.hand = g_groupedPoisonWatcher.targetHand;
		evidence.epoch = g_groupedPoisonWatcher.epoch;
		evidence.beforeSignature = a_before.targetLogicalSignatureDigest;
		evidence.afterSignature = a_after.targetLogicalSignatureDigest;
		evidence.afterNonPoisonSignature = a_after.targetNonPoisonSignatureDigest;
		evidence.poisonFormID = a_after.targetPoisonFormID;
		evidence.expectedTotalCount = a_before.totalInventoryCount;

		if (++g_nextGroupedPoisonAliasGeneration == 0) {
			++g_nextGroupedPoisonAliasGeneration;
		}
		auto alias = GroupedPoisonPresentationAliasPolicy::Create(
			evidence, g_nextGroupedPoisonAliasGeneration);
		if (!alias.IsActive()) {
			return;
		}
		DropGroupedPoisonPresentationAlias("superseded_by_strong_transition");
		g_groupedPoisonPresentationAlias = alias;
		logger::debug(
			"GROUPED_POISON_ALIAS CREATE slotDigest={:016X} form={:08X} hand={} epoch={} generation={} beforeSig={:016X} afterSig={:016X} reason=strong_hand_transition gameplayAuthority=0",
			alias.slotDigest,
			alias.formID,
			alias.hand == Hand::Left ? "LEFT" : "RIGHT",
			alias.epoch,
			alias.generation,
			alias.beforeSignature,
			alias.afterSignature);
	}

	Presentation ResolveGroupedPoisonAliasPresentation(
		std::uint64_t a_runtimeSlotID,
		RE::FormID a_formID,
		std::uint16_t a_storedUID,
		std::uint64_t a_storedLogicalSignature)
	{
		if (!g_groupedPoisonPresentationAlias.IsActive() ||
		    g_groupedPoisonPresentationAlias.runtimeSlotID != a_runtimeSlotID) {
			return {};
		}

		const auto slotDigest = BuildGroupedPoisonSlotDigest(
			a_runtimeSlotID,
			a_formID,
			g_groupedPoisonPresentationAlias.hand,
			a_storedLogicalSignature);
		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto snapshot = CaptureGroupedPoisonTargetSnapshot(
			player,
			g_groupedPoisonPresentationAlias.formID,
			g_groupedPoisonPresentationAlias.hand);

		ValidationEvidence evidence;
		evidence.readable = snapshot.summary.readable && !snapshot.summary.hasUnreadableMember;
		evidence.targetHandHasForm = snapshot.summary.targetHandHasForm;
		evidence.otherHandHasSameForm = snapshot.summary.otherHandHasSameForm;
		evidence.targetMemberUnique = snapshot.summary.targetMemberUnique;
		evidence.targetMemberPoisoned = snapshot.summary.targetMemberPoisoned;
		evidence.targetMemberEnchanted = snapshot.summary.targetMemberEnchanted;
		evidence.exactlyOnePoisonedMember = snapshot.summary.exactlyOnePoisonedMember;
		evidence.hasCompetingModifiedMember = snapshot.summary.otherModifiedItemCount != 0;
		evidence.storedUID = a_storedUID;
		evidence.runtimeSlotID = a_runtimeSlotID;
		evidence.slotDigest = slotDigest;
		evidence.formID = a_formID;
		evidence.epoch = Wheeler::GetTransientRestorationEpoch();
		evidence.storedLogicalSignature = a_storedLogicalSignature;
		evidence.currentTargetSignature = snapshot.summary.targetLogicalSignatureDigest;
		evidence.currentTargetNonPoisonSignature = snapshot.summary.targetNonPoisonSignatureDigest;
		evidence.currentPoisonFormID = snapshot.summary.targetPoisonFormID;
		evidence.currentTotalCount = snapshot.summary.totalInventoryCount;

		const auto failure = GroupedPoisonPresentationAliasPolicy::GetValidationFailure(
			g_groupedPoisonPresentationAlias, evidence);
		if (failure != ValidationFailure::None) {
			DropGroupedPoisonPresentationAlias(GetGroupedPoisonAliasFailureName(failure));
			return {};
		}

		if (!g_groupedPoisonPresentationAlias.validLogged) {
			logger::debug(
				"GROUPED_POISON_ALIAS VALID slotDigest={:016X} form={:08X} hand={} epoch={} generation={} beforeSig={:016X} afterSig={:016X} reason=fresh_draw_validation",
				g_groupedPoisonPresentationAlias.slotDigest,
				g_groupedPoisonPresentationAlias.formID,
				g_groupedPoisonPresentationAlias.hand == Hand::Left ? "LEFT" : "RIGHT",
				g_groupedPoisonPresentationAlias.epoch,
				g_groupedPoisonPresentationAlias.generation,
				g_groupedPoisonPresentationAlias.beforeSignature,
				g_groupedPoisonPresentationAlias.afterSignature);
			g_groupedPoisonPresentationAlias.validLogged = true;
		}
		return GroupedPoisonPresentationAliasPolicy::Present(
			g_groupedPoisonPresentationAlias, evidence);
	}

	void CancelGroupedPoisonDiagnostic(const char* a_reason)
	{
		if (g_groupedPoisonWatcher.IsActive()) {
			logger::debug(
				"GROUPED_POISON CANCEL tx={} reason={} form={:08X} hand={} samplesRemaining={}",
				g_groupedPoisonWatcher.transactionID,
				a_reason ? a_reason : "unspecified",
				g_groupedPoisonWatcher.formID,
				g_groupedPoisonWatcher.targetHand == Hand::Left ? "LEFT" : "RIGHT",
				g_groupedPoisonWatcher.samplesRemaining);
		}
		g_groupedPoisonWatcher.Clear();
	}

	void ArmGroupedPoisonDiagnostic(
		std::uint64_t a_runtimeSlotID,
		RE::FormID a_formID,
		Hand a_targetHand,
		int a_preEquipGroupCount,
		std::string_view a_preEquipLogicalSignature)
	{
		CancelGroupedPoisonDiagnostic("superseded_by_grouped_equip");
		if (++g_nextGroupedPoisonTransactionID == 0) {
			++g_nextGroupedPoisonTransactionID;
		}
		const auto signatureDigest = HashGroupedPoisonText(a_preEquipLogicalSignature);
		const auto slotDigest = BuildGroupedPoisonSlotDigest(
			a_runtimeSlotID, a_formID, a_targetHand, signatureDigest);
		g_groupedPoisonWatcher = GroupedPoisonLineageDiagnosticPolicy::Arm(
			g_nextGroupedPoisonTransactionID,
			Wheeler::GetTransientRestorationEpoch(),
			a_runtimeSlotID,
			a_formID,
			a_targetHand,
			a_preEquipGroupCount,
			signatureDigest,
			slotDigest,
			GetGroupedPoisonDiagnosticTime(),
			g_groupedPoisonUpdateSequence);
		logger::debug(
			"GROUPED_POISON ARM tx={} epoch={} slotDigest={:016X} form={:08X} hand={} preGroupCount={} preLogicalSig={:016X} lifetimeSeconds=25 scalarOnly=1",
			g_groupedPoisonWatcher.transactionID,
			g_groupedPoisonWatcher.epoch,
			g_groupedPoisonWatcher.slotDigest,
			a_formID,
			a_targetHand == Hand::Left ? "LEFT" : "RIGHT",
			a_preEquipGroupCount,
			signatureDigest);
	}

	void DrawWeaponLiveStatusOverlays(
		bool a_safelyResolved,
		bool a_poisoned,
		bool a_enchanted,
		ImVec2 a_center,
		DrawArgs a_drawArgs)
	{
		if (!a_safelyResolved || (!a_poisoned && !a_enchanted)) {
			return;
		}

		std::array<Texture::icon_image_type, 2> badges{};
		std::size_t badgeCount = 0;
		if (a_poisoned) {
			badges[badgeCount++] = Texture::icon_image_type::poison_default;
		}
		if (a_enchanted) {
			badges[badgeCount++] = Texture::icon_image_type::weapon_enchanted;
		}

		const auto slotBackground = Texture::GetIconImage(Texture::icon_image_type::slot_background);
		const float backgroundScale = Config::Styling::Item::Slot::BackgroundTexture::Scale;
		const float slotWidth = slotBackground.width > 0 ? slotBackground.width * backgroundScale : 96.0F;
		const float slotHeight = slotBackground.height > 0 ? slotBackground.height * backgroundScale : 96.0F;
		const float badgeExtent = std::clamp((std::min)(slotWidth, slotHeight) * 0.18F, 12.0F, 28.0F);
		const float spacing = badgeExtent * 1.15F;
		const float firstOffsetX = badgeCount == 1 ? 0.0F : -spacing * 0.5F;
		const float offsetY = slotHeight * 0.29F;

		for (std::size_t index = 0; index < badgeCount; ++index) {
			const auto image = Texture::GetIconImage(badges[index]);
			if (!image.texture || image.width <= 0 || image.height <= 0) {
				continue;
			}
			const float aspect = static_cast<float>(image.width) / static_cast<float>(image.height);
			const ImVec2 size = aspect >= 1.0F ?
				ImVec2(badgeExtent, badgeExtent / aspect) :
				ImVec2(badgeExtent * aspect, badgeExtent);
			Drawer::draw_texture(
				image.texture,
				a_center,
				firstOffsetX + spacing * static_cast<float>(index),
				offsetY,
				size,
				C_SKYRIMWHITE,
				a_drawArgs);
		}
	}

	bool IsOneHandedWeaponForCompat(RE::TESObjectWEAP* a_weapon)
	{
		if (!a_weapon) {
			return false;
		}

		if (a_weapon->IsBow() || a_weapon->IsCrossbow()) {
			return false;
		}

		switch (a_weapon->GetWeaponType()) {
		case RE::WEAPON_TYPE::kTwoHandSword:
		case RE::WEAPON_TYPE::kTwoHandAxe:
		case RE::WEAPON_TYPE::kStaff:
			return false;
		default:
			return true;
		}
	}

	bool IsInstanceSpecificIndicatorExtraData(RE::ExtraDataList* a_extraData)
	{
		if (!a_extraData) {
			return false;
		}
		return HasTypeSafe(a_extraData, RE::ExtraDataType::kEnchantment) ||
		       HasTypeSafe(a_extraData, RE::ExtraDataType::kPoison) ||
		       HasTypeSafe(a_extraData, RE::ExtraDataType::kHealth) ||
		       HasTypeSafe(a_extraData, RE::ExtraDataType::kCharge) ||
		       GetByTypeSafe<RE::ExtraTextDisplayData>(a_extraData) != nullptr;
	}

	bool IsTwoHandedIndicatorWeapon(const RE::TESObjectWEAP* a_weapon)
	{
		const auto type = a_weapon->GetWeaponType();
		return WeaponHandIndicatorPresentationPolicy::IsTwoHanded(
			a_weapon->IsBow(), a_weapon->IsCrossbow(),
			type == RE::WEAPON_TYPE::kTwoHandSword, type == RE::WEAPON_TYPE::kTwoHandAxe);
	}

	bool MatchesMixedTwoHandedIndicatorRow(
		RE::TESObjectREFR::InventoryItemMap& a_inv, RE::TESObjectWEAP* a_weapon,
		std::string_view a_rowSignature, bool a_cleanSentinel)
	{
		using namespace WeaponHandIndicatorPresentationPolicy;
		auto* entry = FindInventoryEntryByForm(a_inv, a_weapon->GetFormID());
		std::vector<RE::ExtraDataList*> lists;
		bool snapshotReadable = false;
		if (!InvokeWithSehGuard([&]() {
			    snapshotReadable = entry && entry->extraLists && CopyExtraListsSafe(entry->extraLists, lists);
		    }) || !snapshotReadable) {
			return false;
		}
		std::vector<MemberEvidence> evidence;
		evidence.reserve(lists.size());
		for (auto* list : lists) {
			MemberEvidence member;
			// Probe success is distinct from absence. Never reuse the gameplay
			// HasTypeSafe/clean-row predicate which collapses read failure to false.
			if (!list || !TryHasTypeSafe(list, RE::ExtraDataType::kWorn, member.worn) ||
			    !TryHasTypeSafe(list, RE::ExtraDataType::kWornLeft, member.wornLeft)) {
				return false;
			}
			constexpr std::array instanceTypes{
				RE::ExtraDataType::kEnchantment, RE::ExtraDataType::kPoison,
				RE::ExtraDataType::kHealth, RE::ExtraDataType::kCharge, RE::ExtraDataType::kTextDisplayData
			};
			for (const auto type : instanceTypes) {
				bool present = false;
				if (!TryHasTypeSafe(list, type, present)) {
					return false;
				}
				member.instanceSpecific = member.instanceSpecific || present;
			}
			if (!TryBuildReadableLogicalRowSignature(list, member.signature)) {
				return false;
			}
			member.readable = true;
			evidence.push_back(std::move(member));
		}
		return ResolveMixedTwoHanded(snapshotReadable, a_cleanSentinel, a_rowSignature, evidence);
	}

	bool MatchesRequestedHandWorn(RE::ExtraDataList* a_extraData, bool a_leftHand)
	{
		bool hasWorn = false;
		bool hasWornLeft = false;
		const bool readWorn = TryHasTypeSafe(a_extraData, RE::ExtraDataType::kWorn, hasWorn);
		const bool readWornLeft = TryHasTypeSafe(a_extraData, RE::ExtraDataType::kWornLeft, hasWornLeft);
		if (!readWorn || !readWornLeft) {
			return false;
		}
		return a_leftHand ? hasWornLeft : (hasWorn && !hasWornLeft);
	}

	enum class SameFormIndicatorTopology
	{
		kUnambiguous,
		kMixedLogicalRows,
		kUnknown
	};

	SameFormIndicatorTopology ClassifySameFormIndicatorTopology(
		RE::TESObjectREFR::InventoryItemMap& a_inv,
		RE::TESObjectWEAP* a_weapon)
	{
		if (!a_weapon) {
			return SameFormIndicatorTopology::kUnknown;
		}

		const int sameFormCount = GetSameFormInventoryCount(a_inv, a_weapon);
		if (sameFormCount == 1) {
			return SameFormIndicatorTopology::kUnambiguous;
		}
		if (sameFormCount <= 0) {
			return SameFormIndicatorTopology::kUnknown;
		}

		RE::InventoryEntryData* entry = FindInventoryEntryByForm(a_inv, a_weapon->GetFormID());
		if (!entry || !entry->extraLists) {
			return SameFormIndicatorTopology::kUnknown;
		}

		std::vector<RE::ExtraDataList*> extraListSnapshot;
		if (!CopyExtraListsSafe(entry->extraLists, extraListSnapshot)) {
			return SameFormIndicatorTopology::kUnknown;
		}

		for (auto* extraList : extraListSnapshot) {
			if (IsInstanceSpecificIndicatorExtraData(extraList)) {
				return SameFormIndicatorTopology::kMixedLogicalRows;
			}
		}
		return SameFormIndicatorTopology::kUnambiguous;
	}

	bool MatchesLogicalRowInHandFromInventory(
		RE::TESObjectREFR::InventoryItemMap& a_inv,
		RE::TESObjectWEAP* a_weapon,
		std::string_view a_logicalRowSignature,
		bool a_cleanSentinel,
		bool a_leftHand)
	{
		if (!a_weapon) {
			return false;
		}

		RE::InventoryEntryData* entry = FindInventoryEntryByForm(a_inv, a_weapon->GetFormID());
		if (!entry || !entry->extraLists) {
			return false;
		}

		std::vector<RE::ExtraDataList*> extraListSnapshot;
		if (!CopyExtraListsSafe(entry->extraLists, extraListSnapshot)) {
			return false;
		}

		if (!a_cleanSentinel && a_logicalRowSignature.empty()) {
			return false;
		}

		for (auto* extraList : extraListSnapshot) {
			if (!extraList || !MatchesRequestedHandWorn(extraList, a_leftHand)) {
				continue;
			}

			if (a_cleanSentinel) {
				if (!IsInstanceSpecificIndicatorExtraData(extraList)) {
					return true;
				}
				continue;
			}

			if (BuildLogicalRowSignature(extraList) == a_logicalRowSignature) {
				return true;
			}
		}

		return false;
	}

	int GetSameFormInventoryCount(const RE::TESObjectREFR::InventoryItemMap& a_inv, RE::TESObjectWEAP* a_weapon)
	{
		if (!a_weapon) {
			return 0;
		}

		const RE::FormID formID = a_weapon->GetFormID();
		int totalCount = 0;
		for (const auto& [boundObj, data] : a_inv) {
			if (!boundObj || boundObj->GetFormID() != formID) {
				continue;
			}
			totalCount += data.first;
		}
		return totalCount;
	}

	// Used only by IsActive, after its existing mutable identity maintenance.
	std::optional<bool> ResolveUID0ActiveVisualOverride(
		RE::TESObjectREFR::InventoryItemMap& a_inv, RE::TESObjectWEAP* a_weapon)
	{
		const int count = GetSameFormInventoryCount(a_inv, a_weapon);
		if (count == 1) {
			return std::nullopt;
		}
		if (!a_weapon || count <= 0) {
			return false;
		}
		std::vector<WeaponActiveVisualPolicy::MemberEvidence> members;
		bool foundEntry = false;
		for (const auto& [object, data] : a_inv) {
			if (!object || object->GetFormID() != a_weapon->GetFormID()) {
				continue;
			}
			if (!data.second) {
				return false;
			}
			foundEntry = true;
			if (!data.second->extraLists) {
				continue;  // An existing entry may represent an implicit plain stack.
			}
			std::vector<RE::ExtraDataList*> snapshot;
			if (!CopyExtraListsSafe(data.second->extraLists, snapshot)) {
				return false;
			}
			for (auto* list : snapshot) {
				if (!list) {
					continue;
				}
				WeaponActiveVisualPolicy::MemberEvidence member;
				member.metadataReadable = true;
				// Same distinctions as the existing indicator classifier, but read
				// failure must not masquerade as absent modification metadata.
				for (const auto type : { RE::ExtraDataType::kPoison, RE::ExtraDataType::kEnchantment,
				         RE::ExtraDataType::kHealth, RE::ExtraDataType::kCharge, RE::ExtraDataType::kTextDisplayData }) {
					bool present = false;
					if (!TryHasTypeSafe(list, type, present)) {
						member.metadataReadable = false;
					}
					if (type == RE::ExtraDataType::kTextDisplayData) {
						RE::ExtraTextDisplayData* text = nullptr;
						if (!InvokeWithSehGuard([&]() { text = list->GetByType<RE::ExtraTextDisplayData>(); }) ||
						    (present && !text)) {
							member.metadataReadable = false;
						}
						present = text != nullptr;
					}
					member.instanceSpecific = member.instanceSpecific || present;
				}
				member.wornReadable = TryHasTypeSafe(list, RE::ExtraDataType::kWorn, member.worn) &&
					TryHasTypeSafe(list, RE::ExtraDataType::kWornLeft, member.wornLeft);
				TryGetExtraListCount(list, member.count);
				members.push_back(member);
			}
		}
		return WeaponActiveVisualPolicy::Resolve(0, count, foundEntry, members);
	}

	struct LogicalRowInventoryState
	{
		int count = 0;
		bool hasPerInstanceModifiedMember = false;
	};

	LogicalRowInventoryState GetLogicalRowInventoryState(
		const RE::TESObjectREFR::InventoryItemMap& a_inv,
		RE::TESObjectWEAP* a_weapon,
		std::uint16_t a_uniqueID)
	{
		if (!a_weapon || a_uniqueID == 0) {
			return {};
		}

		const RE::FormID formID = a_weapon->GetFormID();
		for (const auto& [boundObj, data] : a_inv) {
			if (!boundObj || boundObj->GetFormID() != formID || !data.second || !data.second->extraLists) {
				continue;
			}

			std::vector<RE::ExtraDataList*> extraListSnapshot;
			if (!CopyExtraListsSafe(data.second->extraLists, extraListSnapshot)) {
				return {};
			}

			LogicalRowInventoryState state;
			for (auto* extraData : extraListSnapshot) {
				auto* uniqueData = GetByTypeSafe<RE::ExtraUniqueID>(extraData);
				if (uniqueData && uniqueData->uniqueID == a_uniqueID) {
					state.count += GetExtraListCountSafe(extraData);
					state.hasPerInstanceModifiedMember =
						state.hasPerInstanceModifiedMember || IsPerInstanceModifiedExtraData(extraData);
				}
			}
			return state;
		}

		return {};
	}

	bool ShouldBypassInstanceHandResolution(
		const RE::TESObjectREFR::InventoryItemMap& a_inv,
		RE::TESObjectWEAP* a_weapon,
		std::uint16_t a_uniqueID)
	{
		if (!a_weapon || a_uniqueID == 0) {
			return false;
		}
		const auto state = GetLogicalRowInventoryState(a_inv, a_weapon, a_uniqueID);
		return state.hasPerInstanceModifiedMember && state.count > 1;
	}

	std::uint64_t ResolveEquippedHandSignature(RE::Actor* a_actor, bool a_leftHand, RE::FormID a_expectedFormID)
	{
		if (!a_actor || a_expectedFormID == 0) {
			return 0;
		}

		RE::InventoryEntryData* entry = a_actor->GetEquippedEntryData(a_leftHand);
		if (!entry || !entry->object || entry->object->GetFormID() != a_expectedFormID || !entry->extraLists) {
			return 0;
		}

		std::vector<RE::ExtraDataList*> extraListSnapshot;
		if (!CopyExtraListsSafe(entry->extraLists, extraListSnapshot)) {
			return 0;
		}

		std::uint64_t fallbackSignature = 0;
		int uniqueCandidateCount = 0;
		for (auto* extraList : extraListSnapshot) {
			if (!extraList) {
				continue;
			}

			bool hasUniqueID = false;
			if (!TryHasTypeSafe(extraList, RE::ExtraDataType::kUniqueID, hasUniqueID) || !hasUniqueID) {
				continue;
			}

			auto* uniqueData = GetByTypeSafe<RE::ExtraUniqueID>(extraList);
			if (!uniqueData || uniqueData->uniqueID == 0) {
				continue;
			}

			++uniqueCandidateCount;
			if (fallbackSignature == 0) {
				fallbackSignature = uniqueData->uniqueID;
			}

			bool hasWorn = false;
			bool hasWornLeft = false;
			const bool readWorn = TryHasTypeSafe(extraList, RE::ExtraDataType::kWorn, hasWorn);
			const bool readWornLeft = TryHasTypeSafe(extraList, RE::ExtraDataType::kWornLeft, hasWornLeft);
			if (!readWorn || !readWornLeft) {
				continue;
			}

			if (a_leftHand ? hasWornLeft : (hasWorn && !hasWornLeft)) {
				return uniqueData->uniqueID;
			}
		}

		if (uniqueCandidateCount == 1) {
			return fallbackSignature;
		}

		return 0;
	}

	Utils::Inventory::Hand GetEquippedHandByExactUniqueID(RE::PlayerCharacter* a_player, RE::TESObjectWEAP* a_weapon, std::uint16_t a_uniqueID)
	{
		if (!a_player || !a_weapon || a_uniqueID == 0) {
			return Utils::Inventory::Hand::None;
		}

		const RE::FormID formID = a_weapon->GetFormID();
		const bool leftMatch = ResolveEquippedHandSignature(a_player, true, formID) == a_uniqueID;
		const bool rightMatch = ResolveEquippedHandSignature(a_player, false, formID) == a_uniqueID;

		if (leftMatch && rightMatch) {
			return Utils::Inventory::Hand::Both;
		}
		if (leftMatch) {
			return Utils::Inventory::Hand::Left;
		}
		if (rightMatch) {
			return Utils::Inventory::Hand::Right;
		}
		return Utils::Inventory::Hand::None;
	}

	const char* GetHandName(Utils::Inventory::Hand a_hand)
	{
		switch (a_hand) {
		case Utils::Inventory::Hand::Left:
			return "left";
		case Utils::Inventory::Hand::Right:
			return "right";
		case Utils::Inventory::Hand::Both:
			return "both";
		case Utils::Inventory::Hand::None:
		default:
			return "none";
		}
	}

	Utils::Inventory::Hand GetTargetHand(bool a_toRight)
	{
		return a_toRight ? Utils::Inventory::Hand::Right : Utils::Inventory::Hand::Left;
	}

	RE::BGSEquipSlot* GetSlotForHand(Utils::Inventory::Hand a_hand)
	{
		switch (a_hand) {
		case Utils::Inventory::Hand::Left:
			return Utils::Slot::GetLeftHandSlot();
		case Utils::Inventory::Hand::Right:
			return Utils::Slot::GetRightHandSlot();
		default:
			return nullptr;
		}
	}

	bool IsWeaponDrawnSafe(RE::PlayerCharacter* a_player)
	{
		if (!a_player) {
			return false;
		}
		auto* actorState = a_player->AsActorState();
		return actorState && actorState->IsWeaponDrawn();
	}

	bool IsImmersiveWeaponSwitchLoaded()
	{
		const bool loaded = ::GetModuleHandleW(L"ImmersiveWeaponSwitch.dll") != nullptr;
		static bool loggedLoaded = false;
		if (loaded && !loggedLoaded) {
			logger::info("IWSCompat: ImmersiveWeaponSwitch.dll detected; Wheeler exact single-copy weapon transfer safe mode enabled");
			loggedLoaded = true;
		}
		return loaded;
	}

	struct ExactWeaponInventoryState
	{
		int sameFormCount = 0;
		int representedCount = 0;
		int exactCount = 0;
		int uniqueCandidateCount = 0;
		RE::ExtraDataList* exactExtraData = nullptr;
		bool readFailed = false;
	};

	ExactWeaponInventoryState ResolveExactWeaponInventoryState(
		RE::TESObjectREFR::InventoryItemMap& a_inv,
		RE::FormID a_formID,
		std::uint16_t a_uniqueID)
	{
		ExactWeaponInventoryState state;
		if (a_formID == 0 || a_uniqueID == 0) {
			return state;
		}

		std::vector<std::uint16_t> uniqueIDs;
		for (auto& [boundObj, data] : a_inv) {
			if (!boundObj || boundObj->GetFormID() != a_formID) {
				continue;
			}

			state.sameFormCount += data.first;
			auto* entry = data.second.get();
			if (!entry || !entry->extraLists) {
				continue;
			}

			std::vector<RE::ExtraDataList*> extraListSnapshot;
			if (!CopyExtraListsSafe(entry->extraLists, extraListSnapshot)) {
				state.readFailed = true;
				continue;
			}

			for (auto* extraList : extraListSnapshot) {
				if (!extraList) {
					continue;
				}
				state.representedCount += GetExtraListCountSafe(extraList);
				auto* uniqueData = GetByTypeSafe<RE::ExtraUniqueID>(extraList);
				if (!uniqueData || uniqueData->uniqueID == 0) {
					continue;
				}
				if (std::find(uniqueIDs.begin(), uniqueIDs.end(), uniqueData->uniqueID) == uniqueIDs.end()) {
					uniqueIDs.push_back(uniqueData->uniqueID);
				}
				if (uniqueData->uniqueID != a_uniqueID) {
					continue;
				}
				state.exactExtraData = extraList;
				state.exactCount += GetExtraListCountSafe(extraList);
			}
		}

		state.uniqueCandidateCount = static_cast<int>(uniqueIDs.size());
		return state;
	}

	bool IsStrictSingleExactInventoryState(const ExactWeaponInventoryState& a_state)
	{
		return !a_state.readFailed &&
		       a_state.sameFormCount == 1 &&
		       a_state.representedCount <= 1 &&
		       a_state.exactCount == 1 &&
		       a_state.uniqueCandidateCount == 1 &&
		       a_state.exactExtraData != nullptr;
	}

	using IWSClock = std::chrono::steady_clock;
	constexpr auto kIWSCompatSheatheTimeout = std::chrono::milliseconds(1800);
	constexpr auto kIWSCompatSettleDelay = std::chrono::milliseconds(120);
	constexpr auto kIWSCompatSheathePulseDelay = std::chrono::milliseconds(250);

	struct PendingIWSExactWeaponTransfer
	{
		bool active = false;
		RE::FormID formID = 0;
		std::uint16_t uniqueID = 0;
		bool toRight = true;
		bool restoreDrawn = false;
		IWSClock::time_point queuedAt{};
		IWSClock::time_point notDrawnSince{};
		IWSClock::time_point lastSheathePulse{};
	};

	PendingIWSExactWeaponTransfer g_pendingIWSExactWeaponTransfer;

	void RestoreDrawForPendingIWSExactWeaponTransfer(const char* a_reason)
	{
		if (!g_pendingIWSExactWeaponTransfer.active || !g_pendingIWSExactWeaponTransfer.restoreDrawn) {
			return;
		}
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !pc->Is3DLoaded()) {
			return;
		}
		InvokeWithSehGuard([&]() { ActorVirtualCompat::DrawWeaponMagicHands(pc, true); });
		logger::info(
			"IWSCompat: requested draw restore after {} formId={:08X} uniqueID={} targetHand={}",
			a_reason ? a_reason : "abort",
			g_pendingIWSExactWeaponTransfer.formID,
			g_pendingIWSExactWeaponTransfer.uniqueID,
			g_pendingIWSExactWeaponTransfer.toRight ? "right" : "left");
	}

	void ClearPendingIWSExactWeaponTransfer(const char* a_reason)
	{
		if (g_pendingIWSExactWeaponTransfer.active) {
			logger::info(
				"IWSCompat: exact transfer cleared reason={} formId={:08X} uniqueID={} targetHand={}",
				a_reason ? a_reason : "unknown",
				g_pendingIWSExactWeaponTransfer.formID,
				g_pendingIWSExactWeaponTransfer.uniqueID,
			g_pendingIWSExactWeaponTransfer.toRight ? "right" : "left");
		}
		g_pendingIWSExactWeaponTransfer = {};
	}

	void AbortPendingIWSExactWeaponTransfer(const char* a_reason, bool a_restoreDraw)
	{
		if (a_restoreDraw) {
			RestoreDrawForPendingIWSExactWeaponTransfer(a_reason);
		}
		ClearPendingIWSExactWeaponTransfer(a_reason);
	}

	bool QueueIWSExactSingleWeaponTransfer(
		RE::PlayerCharacter* a_player,
		RE::TESObjectWEAP* a_weapon,
		std::uint16_t a_uniqueID,
		bool a_toRight,
		int a_count,
		int a_sameFormCount,
		RE::ExtraDataList* a_extraData,
		bool a_bypassInstanceHandResolution)
	{
		if (!IsImmersiveWeaponSwitchLoaded() || !a_player || !a_weapon || !IsOneHandedWeaponForCompat(a_weapon)) {
			return false;
		}
		if (a_bypassInstanceHandResolution || a_uniqueID == 0 || a_count != 1 || a_sameFormCount != 1 || !a_extraData) {
			return false;
		}

		const Utils::Inventory::Hand currentHand =
			GetEquippedHandByExactUniqueID(a_player, a_weapon, a_uniqueID);
		const Utils::Inventory::Hand targetHand = GetTargetHand(a_toRight);
		const bool oppositeHandTransfer =
			(currentHand == Utils::Inventory::Hand::Right && targetHand == Utils::Inventory::Hand::Left) ||
			(currentHand == Utils::Inventory::Hand::Left && targetHand == Utils::Inventory::Hand::Right);
		if (!oppositeHandTransfer) {
			return false;
		}

		const auto now = IWSClock::now();
		const bool replacingPending = g_pendingIWSExactWeaponTransfer.active;
		const bool samePending =
			replacingPending &&
			g_pendingIWSExactWeaponTransfer.formID == a_weapon->GetFormID() &&
			g_pendingIWSExactWeaponTransfer.uniqueID == a_uniqueID &&
			g_pendingIWSExactWeaponTransfer.toRight == a_toRight;
		if (samePending) {
			g_pendingIWSExactWeaponTransfer.restoreDrawn =
				g_pendingIWSExactWeaponTransfer.restoreDrawn ||
				Config::WheelBehavior::AutoDrawOnUse ||
				IsWeaponDrawnSafe(a_player);
			logger::info(
				"IWSCompat: coalesced exact single-copy transfer formId={:08X} uniqueID={} targetHand={} restoreDrawn={}",
				a_weapon->GetFormID(),
				a_uniqueID,
				a_toRight ? "right" : "left",
				g_pendingIWSExactWeaponTransfer.restoreDrawn ? 1 : 0);
			return true;
		}

		RE::TESObjectREFR::InventoryItemMap queueInv = a_player->GetInventory();
		ExactWeaponInventoryState invState = ResolveExactWeaponInventoryState(
			queueInv,
			a_weapon->GetFormID(),
			a_uniqueID);
		if (!IsStrictSingleExactInventoryState(invState)) {
			logger::warn(
				"IWSCompat: suppressed exact transfer due malformed single-copy inventory formId={:08X} uniqueID={} sameFormCount={} representedCount={} exactCount={} uniqueCandidates={} hasExtraData={} readFailed={}",
				a_weapon->GetFormID(),
				a_uniqueID,
				invState.sameFormCount,
				invState.representedCount,
				invState.exactCount,
				invState.uniqueCandidateCount,
				invState.exactExtraData ? 1 : 0,
				invState.readFailed ? 1 : 0);
			return true;
		}

		g_pendingIWSExactWeaponTransfer.active = true;
		g_pendingIWSExactWeaponTransfer.formID = a_weapon->GetFormID();
		g_pendingIWSExactWeaponTransfer.uniqueID = a_uniqueID;
		g_pendingIWSExactWeaponTransfer.toRight = a_toRight;
		g_pendingIWSExactWeaponTransfer.restoreDrawn =
			Config::WheelBehavior::AutoDrawOnUse || IsWeaponDrawnSafe(a_player);
		g_pendingIWSExactWeaponTransfer.queuedAt = now;
		g_pendingIWSExactWeaponTransfer.notDrawnSince = {};
		g_pendingIWSExactWeaponTransfer.lastSheathePulse = {};

		logger::info(
			"IWSCompat: queued exact single-copy transfer formId={:08X} uniqueID={} sourceHand={} targetHand={} restoreDrawn={} replacingPending={}",
			a_weapon->GetFormID(),
			a_uniqueID,
			GetHandName(currentHand),
			GetHandName(targetHand),
			g_pendingIWSExactWeaponTransfer.restoreDrawn ? 1 : 0,
			replacingPending ? 1 : 0);

		if (IsWeaponDrawnSafe(a_player)) {
			InvokeWithSehGuard([&]() { ActorVirtualCompat::DrawWeaponMagicHands(a_player, false); });
			g_pendingIWSExactWeaponTransfer.lastSheathePulse = now;
		}
		return true;
	}

	void ProcessPendingIWSExactWeaponTransfer()
	{
		if (!g_pendingIWSExactWeaponTransfer.active) {
			return;
		}

		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !pc->Is3DLoaded()) {
			AbortPendingIWSExactWeaponTransfer("player_unavailable", false);
			return;
		}

		auto* weapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(g_pendingIWSExactWeaponTransfer.formID);
		if (!weapon || !IsOneHandedWeaponForCompat(weapon)) {
			AbortPendingIWSExactWeaponTransfer("weapon_unavailable", true);
			return;
		}

		const auto now = IWSClock::now();
		if (IsWeaponDrawnSafe(pc)) {
			if (now - g_pendingIWSExactWeaponTransfer.queuedAt >= kIWSCompatSheatheTimeout) {
				AbortPendingIWSExactWeaponTransfer("sheathe_timeout", true);
				return;
			}

			if (g_pendingIWSExactWeaponTransfer.lastSheathePulse == IWSClock::time_point{} ||
				now - g_pendingIWSExactWeaponTransfer.lastSheathePulse >= kIWSCompatSheathePulseDelay) {
				InvokeWithSehGuard([&]() { ActorVirtualCompat::DrawWeaponMagicHands(pc, false); });
				g_pendingIWSExactWeaponTransfer.lastSheathePulse = now;
			}
			g_pendingIWSExactWeaponTransfer.notDrawnSince = {};
			return;
		}

		if (g_pendingIWSExactWeaponTransfer.notDrawnSince == IWSClock::time_point{}) {
			g_pendingIWSExactWeaponTransfer.notDrawnSince = now;
			return;
		}
		if (now - g_pendingIWSExactWeaponTransfer.notDrawnSince < kIWSCompatSettleDelay) {
			return;
		}

		RE::TESObjectREFR::InventoryItemMap inv = pc->GetInventory();
		ExactWeaponInventoryState invState = ResolveExactWeaponInventoryState(
			inv,
			g_pendingIWSExactWeaponTransfer.formID,
			g_pendingIWSExactWeaponTransfer.uniqueID);
		if (!IsStrictSingleExactInventoryState(invState)) {
			logger::warn(
				"IWSCompat: exact transfer aborted by inventory guard formId={:08X} uniqueID={} sameFormCount={} representedCount={} exactCount={} uniqueCandidates={} hasExtraData={} readFailed={}",
				g_pendingIWSExactWeaponTransfer.formID,
				g_pendingIWSExactWeaponTransfer.uniqueID,
				invState.sameFormCount,
				invState.representedCount,
				invState.exactCount,
				invState.uniqueCandidateCount,
				invState.exactExtraData ? 1 : 0,
				invState.readFailed ? 1 : 0);
			AbortPendingIWSExactWeaponTransfer("inventory_guard", true);
			return;
		}

		const Utils::Inventory::Hand sourceHand =
			GetEquippedHandByExactUniqueID(pc, weapon, g_pendingIWSExactWeaponTransfer.uniqueID);
		const Utils::Inventory::Hand targetHand = GetTargetHand(g_pendingIWSExactWeaponTransfer.toRight);
		if (sourceHand == targetHand) {
			if (g_pendingIWSExactWeaponTransfer.restoreDrawn) {
				InvokeWithSehGuard([&]() { ActorVirtualCompat::DrawWeaponMagicHands(pc, true); });
			}
			ClearPendingIWSExactWeaponTransfer("already_target_hand");
			return;
		}

		const bool sourceIsOpposite =
			(sourceHand == Utils::Inventory::Hand::Right && targetHand == Utils::Inventory::Hand::Left) ||
			(sourceHand == Utils::Inventory::Hand::Left && targetHand == Utils::Inventory::Hand::Right);
		if (!sourceIsOpposite) {
			logger::warn(
				"IWSCompat: exact transfer aborted by equipped-hand guard formId={:08X} uniqueID={} sourceHand={} targetHand={}",
				g_pendingIWSExactWeaponTransfer.formID,
				g_pendingIWSExactWeaponTransfer.uniqueID,
				GetHandName(sourceHand),
				GetHandName(targetHand));
			AbortPendingIWSExactWeaponTransfer("equipped_hand_guard", true);
			return;
		}

		auto* aeMan = RE::ActorEquipManager::GetSingleton();
		auto* sourceSlot = GetSlotForHand(sourceHand);
		auto* targetSlot = GetSlotForHand(targetHand);
		if (!aeMan || !sourceSlot || !targetSlot) {
			AbortPendingIWSExactWeaponTransfer("equip_manager_or_slot_unavailable", true);
			return;
		}

		logger::info(
			"IWSCompat: executing exact transfer while sheathed formId={:08X} uniqueID={} sourceHand={} targetHand={}",
			g_pendingIWSExactWeaponTransfer.formID,
			g_pendingIWSExactWeaponTransfer.uniqueID,
			GetHandName(sourceHand),
			GetHandName(targetHand));

		invState.exactExtraData = nullptr;
		inv.clear();
		InventorySnapshotCache::UnequipObject(aeMan, pc, weapon, nullptr, 1, sourceSlot, false, true, true);

		RE::TESObjectREFR::InventoryItemMap postUnequipInv = pc->GetInventory();
		ExactWeaponInventoryState postUnequipState = ResolveExactWeaponInventoryState(
			postUnequipInv,
			g_pendingIWSExactWeaponTransfer.formID,
			g_pendingIWSExactWeaponTransfer.uniqueID);
		if (!IsStrictSingleExactInventoryState(postUnequipState)) {
			logger::warn(
				"IWSCompat: exact transfer aborted after unequip formId={:08X} uniqueID={} sameFormCount={} representedCount={} exactCount={} uniqueCandidates={} hasExtraData={} readFailed={}",
				g_pendingIWSExactWeaponTransfer.formID,
				g_pendingIWSExactWeaponTransfer.uniqueID,
				postUnequipState.sameFormCount,
				postUnequipState.representedCount,
				postUnequipState.exactCount,
				postUnequipState.uniqueCandidateCount,
				postUnequipState.exactExtraData ? 1 : 0,
				postUnequipState.readFailed ? 1 : 0);
			AbortPendingIWSExactWeaponTransfer("post_unequip_inventory_guard", true);
			return;
		}

		InventorySnapshotCache::EquipObject(aeMan, pc, weapon, postUnequipState.exactExtraData, 1, targetSlot);
		postUnequipState.exactExtraData = nullptr;
		postUnequipInv.clear();

		if (g_pendingIWSExactWeaponTransfer.restoreDrawn) {
			InvokeWithSehGuard([&]() { ActorVirtualCompat::DrawWeaponMagicHands(pc, true); });
		}

		ClearPendingIWSExactWeaponTransfer("completed");
	}
}

namespace LegacyWeaponRestore
{
	namespace
	{
		LegacyWeaponRestorePolicy::Topology GetRestoreTopology(
			RE::TESObjectREFR::InventoryItemMap& a_inventory,
			RE::TESObjectWEAP* a_weapon,
			int a_sameFormCount)
		{
			using Topology = LegacyWeaponRestorePolicy::Topology;
			switch (ClassifySameFormIndicatorTopology(a_inventory, a_weapon)) {
			case SameFormIndicatorTopology::kUnambiguous:
				return Topology::Unambiguous;
			case SameFormIndicatorTopology::kMixedLogicalRows:
				return Topology::MixedLogicalRows;
			case SameFormIndicatorTopology::kUnknown:
			default:
				break;
			}

			// A raw form-level stack with no extra lists is positively plain even
			// when it contains multiple equivalent copies.
			if (a_sameFormCount > 0) {
				auto* entry = FindInventoryEntryByForm(a_inventory, a_weapon->GetFormID());
				if (entry && !entry->extraLists) {
					return Topology::Unambiguous;
				}
			}
			return Topology::Unknown;
		}

		struct LogicalRowMemberSnapshot
		{
			std::vector<LogicalRowMember> members;
			bool enumerationReadable = false;
			int unreadableMembers = 0;
		};

		LogicalRowMemberSnapshot CollectAllMembers(
			RE::TESObjectREFR::InventoryItemMap& a_inventory,
			RE::FormID a_formID)
		{
			LogicalRowMemberSnapshot snapshot;
			auto* entry = FindInventoryEntryByForm(a_inventory, a_formID);
			if (!entry) {
				return snapshot;
			}
			if (!entry->extraLists) {
				snapshot.enumerationReadable = true;
				return snapshot;
			}

			std::vector<RE::ExtraDataList*> extraLists;
			if (!CopyExtraListsSafe(entry->extraLists, extraLists)) {
				return snapshot;
			}
			snapshot.enumerationReadable = true;
			for (auto* extraData : extraLists) {
				if (!extraData) {
					++snapshot.unreadableMembers;
					continue;
				}
				LogicalRowMember member;
				member.extraData = extraData;
				if (auto* uniqueData = GetByTypeSafe<RE::ExtraUniqueID>(extraData)) {
					member.uniqueID = uniqueData->uniqueID;
				}
				member.countReadable = TryGetExtraListCount(extraData, member.count);
				const bool readWorn = TryHasTypeSafe(extraData, RE::ExtraDataType::kWorn, member.wornRight);
				const bool readWornLeft = TryHasTypeSafe(extraData, RE::ExtraDataType::kWornLeft, member.wornLeft);
				member.wornStateReadable = readWorn && readWornLeft;
				member.signatureReadable = TryBuildReadableLogicalRowSignature(extraData, member.signature);
				snapshot.members.push_back(std::move(member));
			}
			return snapshot;
		}

		bool IsWornInHand(const LogicalRowMember& a_member, bool a_leftHand)
		{
			return a_member.wornStateReadable &&
			       (a_leftHand ? a_member.wornLeft : (a_member.wornRight && !a_member.wornLeft));
		}

		bool IsEligibleForEquip(const LogicalRowMember& a_member, bool a_leftHand)
		{
			return a_member.wornStateReadable &&
			       ((!a_member.wornRight && !a_member.wornLeft) || IsWornInHand(a_member, a_leftHand));
		}

		enum class GroupProofFailure
		{
			None,
			UnreadablePopulation,
			ConflictingLogicalRows,
			WornWeaponNotAttributable
		};

		struct GroupEquivalentProof
		{
			LegacyWeaponRestorePolicy::GroupProofEvidence evidence;
			std::string logicalRowSignature;
			std::optional<std::size_t> eligibleMemberIndex;
			GroupProofFailure failure = GroupProofFailure::UnreadablePopulation;

			[[nodiscard]] bool IsValid(bool a_requireWornAttribution) const noexcept
			{
				return LegacyWeaponRestorePolicy::ProvesGroupEquivalent(
					evidence, a_requireWornAttribution);
			}
		};

		GroupEquivalentProof BuildGroupEquivalentProof(
			const LogicalRowMemberSnapshot& a_snapshot,
			int a_sameFormCount,
			RE::FormID a_formID,
			bool a_leftHand,
			bool a_requireWornAttribution)
		{
			GroupEquivalentProof proof;
			proof.evidence.sameFormCount = a_sameFormCount;
			proof.evidence.unreadableMembers = a_snapshot.unreadableMembers;
			if (!a_snapshot.enumerationReadable || a_sameFormCount <= 1) {
				return proof;
			}

			std::string commonSignature;
			for (std::size_t index = 0; index < a_snapshot.members.size(); ++index) {
				const auto& member = a_snapshot.members[index];
				if (!member.countReadable || !member.wornStateReadable || !member.signatureReadable) {
					++proof.evidence.unreadableMembers;
					continue;
				}
				if (member.count > a_sameFormCount - proof.evidence.representedCount) {
					++proof.evidence.unreadableMembers;
					continue;
				}
				proof.evidence.representedCount += member.count;
				if (commonSignature.empty()) {
					commonSignature = member.signature;
					proof.evidence.distinctLogicalRows = 1;
				} else if (commonSignature != member.signature) {
					proof.evidence.distinctLogicalRows = 2;
				}
				if (IsEligibleForEquip(member, a_leftHand)) {
					proof.evidence.eligibleLogicalCount += member.count;
					if (!proof.eligibleMemberIndex.has_value()) {
						proof.eligibleMemberIndex = index;
					}
				} else if ((member.wornRight || member.wornLeft) && member.count > 1) {
					// One logical unit is occupied by the opposite hand. Any remaining
					// fully-accounted units in the same equivalent row remain eligible,
					// but require the existing proven form-level group authority.
					proof.evidence.eligibleLogicalCount += member.count - 1;
				}
			}

			if (proof.evidence.representedCount <= a_sameFormCount) {
				proof.evidence.implicitCount = a_sameFormCount - proof.evidence.representedCount;
			}
			proof.evidence.eligibleLogicalCount += proof.evidence.implicitCount;
			if (commonSignature.empty() && proof.evidence.implicitCount > 0) {
				commonSignature = GetPlainLogicalRowSignature();
				proof.evidence.distinctLogicalRows = 1;
			}
			proof.evidence.implicitMembersCompatible =
				proof.evidence.implicitCount == 0 || commonSignature == GetPlainLogicalRowSignature();

			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				if (auto* equipped = player->GetEquippedObject(a_leftHand)) {
					proof.evidence.wornWeaponAttributable = equipped->GetFormID() == a_formID;
				}
			}
			proof.logicalRowSignature = std::move(commonSignature);

			if (proof.evidence.unreadableMembers != 0 ||
				proof.evidence.representedCount + proof.evidence.implicitCount != a_sameFormCount) {
				proof.failure = GroupProofFailure::UnreadablePopulation;
			} else if (proof.evidence.distinctLogicalRows != 1 ||
				!proof.evidence.implicitMembersCompatible) {
				proof.failure = GroupProofFailure::ConflictingLogicalRows;
			} else if (a_requireWornAttribution && !proof.evidence.wornWeaponAttributable) {
				proof.failure = GroupProofFailure::WornWeaponNotAttributable;
			} else {
				proof.failure = GroupProofFailure::None;
			}
			return proof;
		}

		LegacyWeaponRestore::CaptureRejectionReason ToCaptureRejectionReason(GroupProofFailure a_failure)
		{
			using Reason = LegacyWeaponRestore::CaptureRejectionReason;
			switch (a_failure) {
			case GroupProofFailure::ConflictingLogicalRows:
				return Reason::ConflictingLogicalRows;
			case GroupProofFailure::WornWeaponNotAttributable:
				return Reason::WornWeaponNotAttributable;
			case GroupProofFailure::UnreadablePopulation:
			default:
				return Reason::UnreadablePopulation;
			}
		}
	}

	bool CaptureWornToken(
		RE::TESObjectREFR::InventoryItemMap& a_inventory,
		RE::TESObjectWEAP* a_weapon,
		bool a_leftHand,
		LegacyWeaponRestoreToken& a_outToken,
		CaptureDiagnostic* a_outDiagnostic)
	{
		a_outToken.Clear();
		if (a_outDiagnostic) {
			*a_outDiagnostic = {};
		}
		if (!a_weapon) {
			if (a_outDiagnostic) {
				a_outDiagnostic->reason = CaptureRejectionReason::InvalidWeapon;
			}
			return false;
		}

		const int sameFormCount = GetSameFormInventoryCount(a_inventory, a_weapon);
		if (a_outDiagnostic) {
			a_outDiagnostic->sameFormCount = sameFormCount;
		}
		if (sameFormCount <= 0) {
			if (a_outDiagnostic) {
				a_outDiagnostic->reason = CaptureRejectionReason::NotInInventory;
			}
			return false;
		}
		const auto topology = GetRestoreTopology(a_inventory, a_weapon, sameFormCount);
		if (a_outDiagnostic) {
			a_outDiagnostic->topology = topology;
		}
		auto snapshot = CollectAllMembers(a_inventory, a_weapon->GetFormID());

		const LogicalRowMember* wornMember = nullptr;
		int wornMemberCount = 0;
		for (const auto& member : snapshot.members) {
			if (!IsWornInHand(member, a_leftHand)) {
				continue;
			}
			++wornMemberCount;
			if (!wornMember) {
				wornMember = std::addressof(member);
			}
		}

		if (!wornMember && sameFormCount == 1 && snapshot.members.size() == 1 &&
			topology == LegacyWeaponRestorePolicy::Topology::Unambiguous) {
			wornMember = std::addressof(snapshot.members.front());
			wornMemberCount = 1;
		}

		a_outToken.formID = a_weapon->GetFormID();
		if (wornMemberCount == 1 && wornMember && wornMember->signatureReadable &&
			(wornMember->uniqueID != 0 ||
				(sameFormCount == 1 && topology == LegacyWeaponRestorePolicy::Topology::Unambiguous))) {
			a_outToken.uniqueID = wornMember->uniqueID;
			a_outToken.logicalRowSignature = wornMember->signature;
			a_outToken.rowKind = LegacyWeaponRestorePolicy::RowKind::Physical;
			return true;
		}

		if (sameFormCount == 1 &&
			topology == LegacyWeaponRestorePolicy::Topology::Unambiguous &&
			snapshot.enumerationReadable && snapshot.members.empty()) {
			a_outToken.rowKind = LegacyWeaponRestorePolicy::RowKind::PlainForm;
			return true;
		}

		if (sameFormCount > 1) {
			const auto groupProof = BuildGroupEquivalentProof(
				snapshot, sameFormCount, a_weapon->GetFormID(), a_leftHand, true);
			if (groupProof.IsValid(true)) {
				a_outToken.uniqueID = 0;
				a_outToken.logicalRowSignature = groupProof.logicalRowSignature;
				a_outToken.rowKind = LegacyWeaponRestorePolicy::RowKind::GroupEquivalent;
				if (a_outDiagnostic) {
					a_outDiagnostic->topology = LegacyWeaponRestorePolicy::Topology::GroupEquivalent;
				}
				return true;
			}
			if (a_outDiagnostic) {
				a_outDiagnostic->reason = ToCaptureRejectionReason(groupProof.failure);
				a_outDiagnostic->topology =
					groupProof.failure == GroupProofFailure::ConflictingLogicalRows ?
					LegacyWeaponRestorePolicy::Topology::MixedLogicalRows : topology;
			}
		} else if (a_outDiagnostic) {
			a_outDiagnostic->reason = wornMemberCount > 1 ?
				CaptureRejectionReason::AmbiguousPhysicalMembers :
				CaptureRejectionReason::UnreadablePopulation;
		}

		a_outToken.Clear();
		return false;
	}

	DualHandCaptureReconciliation ReconcileDualHandSameFormCapture(
		RE::TESObjectREFR::InventoryItemMap& a_inventory,
		LegacyWeaponRestoreToken& a_leftToken,
		LegacyWeaponRestoreToken& a_rightToken)
	{
		if (!a_leftToken.IsValid() || !a_rightToken.IsValid() ||
			a_leftToken.formID == 0 || a_leftToken.formID != a_rightToken.formID) {
			return DualHandCaptureReconciliation::Unchanged;
		}

		auto* weapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(a_leftToken.formID);
		if (!weapon) {
			return DualHandCaptureReconciliation::Unchanged;
		}

		const int sameFormCount = GetSameFormInventoryCount(a_inventory, weapon);
		auto snapshot = CollectAllMembers(a_inventory, a_leftToken.formID);
		const bool signaturesMatch =
			!a_leftToken.logicalRowSignature.empty() &&
			a_leftToken.logicalRowSignature == a_rightToken.logicalRowSignature;

		auto findWornMemberIndex = [&](const LegacyWeaponRestoreToken& a_token, bool a_leftHand) {
			std::optional<std::size_t> result;
			for (std::size_t index = 0; index < snapshot.members.size(); ++index) {
				const auto& member = snapshot.members[index];
				if (!member.signatureReadable ||
					member.signature != a_token.logicalRowSignature ||
					!IsWornInHand(member, a_leftHand)) {
					continue;
				}
				if (a_token.rowKind == LegacyWeaponRestorePolicy::RowKind::Physical &&
					a_token.uniqueID != 0 && member.uniqueID != a_token.uniqueID) {
					continue;
				}
				if (result.has_value()) {
					return std::optional<std::size_t>{};
				}
				result = index;
			}
			return result;
		};

		const auto leftWorn = findWornMemberIndex(a_leftToken, true);
		const auto rightWorn = findWornMemberIndex(a_rightToken, false);
		const bool distinctWornMembersProven =
			leftWorn.has_value() && rightWorn.has_value() && *leftWorn != *rightWorn;
		const auto groupProof = BuildGroupEquivalentProof(
			snapshot, sameFormCount, a_leftToken.formID, false, false);
		const bool groupEquivalentProven =
			groupProof.IsValid(false) && signaturesMatch &&
			groupProof.logicalRowSignature == a_leftToken.logicalRowSignature;

		const auto decision = LegacyWeaponRestorePolicy::DecideDualHandCapture({
			a_leftToken.rowKind,
			a_rightToken.rowKind,
			a_leftToken.formID,
			a_rightToken.formID,
			a_leftToken.uniqueID,
			a_rightToken.uniqueID,
			signaturesMatch,
			distinctWornMembersProven,
			groupEquivalentProven
		});
		switch (decision) {
		case LegacyWeaponRestorePolicy::DualHandCaptureDecision::PromoteBothToGroupEquivalent:
			a_leftToken.uniqueID = 0;
			a_leftToken.rowKind = LegacyWeaponRestorePolicy::RowKind::GroupEquivalent;
			a_rightToken.uniqueID = 0;
			a_rightToken.rowKind = LegacyWeaponRestorePolicy::RowKind::GroupEquivalent;
			return DualHandCaptureReconciliation::PromotedToGroupEquivalent;
		case LegacyWeaponRestorePolicy::DualHandCaptureDecision::RejectIndistinguishableCollision:
			a_leftToken.Clear();
			a_rightToken.Clear();
			return DualHandCaptureReconciliation::RejectedUnsafeCollision;
		case LegacyWeaponRestorePolicy::DualHandCaptureDecision::PreserveExactAuthority:
		default:
			return DualHandCaptureReconciliation::Unchanged;
		}
	}

	bool ResolveLiveMember(
		RE::TESObjectREFR::InventoryItemMap& a_inventory,
		const LegacyWeaponRestoreToken& a_token,
		bool a_leftHand,
		LiveSelection& a_outSelection)
	{
		a_outSelection = {};
		if (!a_token.IsValid()) {
			return false;
		}
		auto* weapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(a_token.formID);
		if (!weapon) {
			return false;
		}

		const int sameFormCount = GetSameFormInventoryCount(a_inventory, weapon);
		const auto topology = GetRestoreTopology(a_inventory, weapon, sameFormCount);
		auto snapshot = CollectAllMembers(a_inventory, a_token.formID);
		std::vector<const LogicalRowMember*> exactCandidates;
		std::vector<const LogicalRowMember*> uidCandidates;
		for (const auto& member : snapshot.members) {
			if (!IsEligibleForEquip(member, a_leftHand)) {
				continue;
			}
			const bool uidMatches = a_token.uniqueID == 0 || member.uniqueID == a_token.uniqueID;
			if (uidMatches && member.signature == a_token.logicalRowSignature) {
				exactCandidates.push_back(std::addressof(member));
			}
			if (a_token.uniqueID != 0 && member.uniqueID == a_token.uniqueID) {
				uidCandidates.push_back(std::addressof(member));
			}
		}

		bool groupEquivalentProven = false;
		bool groupSignatureMatches = false;
		bool groupFormLevelAllowed = false;
		if (a_token.rowKind == LegacyWeaponRestorePolicy::RowKind::GroupEquivalent) {
			const auto groupProof = BuildGroupEquivalentProof(
				snapshot, sameFormCount, a_token.formID, a_leftHand, false);
			groupEquivalentProven = groupProof.IsValid(false);
			groupSignatureMatches =
				groupEquivalentProven && groupProof.logicalRowSignature == a_token.logicalRowSignature;
			groupFormLevelAllowed = LegacyWeaponRestorePolicy::AllowsGroupEquivalentFormLevel(
				groupProof.evidence, groupSignatureMatches);
		}

		const LegacyWeaponRestorePolicy::Evidence evidence{
			a_token.rowKind,
			topology,
			sameFormCount,
			a_token.uniqueID,
			static_cast<int>(exactCandidates.size()),
			static_cast<int>(uidCandidates.size()),
			groupEquivalentProven,
			groupSignatureMatches,
			groupFormLevelAllowed
		};
		a_outSelection.resolution = LegacyWeaponRestorePolicy::Decide(evidence);
		switch (a_outSelection.resolution) {
		case LegacyWeaponRestorePolicy::Resolution::ExactLogicalRow:
			a_outSelection.extraData = exactCandidates.empty() ? nullptr : exactCandidates.front()->extraData;
			return a_outSelection.extraData != nullptr;
		case LegacyWeaponRestorePolicy::Resolution::UIDLineageFallback:
			a_outSelection.extraData = uidCandidates.empty() ? nullptr : uidCandidates.front()->extraData;
			return a_outSelection.extraData != nullptr;
		case LegacyWeaponRestorePolicy::Resolution::PlainFormLevel:
		case LegacyWeaponRestorePolicy::Resolution::GroupEquivalentFormLevel:
			return true;
		case LegacyWeaponRestorePolicy::Resolution::GroupEquivalentMember:
			a_outSelection.extraData = exactCandidates.empty() ? nullptr : exactCandidates.front()->extraData;
			return a_outSelection.extraData != nullptr;
		case LegacyWeaponRestorePolicy::Resolution::None:
		default:
			return false;
		}
	}

	bool MatchesWornMember(
		RE::TESObjectREFR::InventoryItemMap& a_inventory,
		const LegacyWeaponRestoreToken& a_token,
		bool a_leftHand)
	{
		if (!a_token.IsValid()) {
			return false;
		}
		auto* weapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(a_token.formID);
		if (!weapon) {
			return false;
		}
		const int sameFormCount = GetSameFormInventoryCount(a_inventory, weapon);
		const auto topology = GetRestoreTopology(a_inventory, weapon, sameFormCount);
		if (a_token.rowKind == LegacyWeaponRestorePolicy::RowKind::PlainForm) {
			return sameFormCount == 1 && topology == LegacyWeaponRestorePolicy::Topology::Unambiguous;
		}

		const auto snapshot = CollectAllMembers(a_inventory, a_token.formID);
		if (a_token.rowKind == LegacyWeaponRestorePolicy::RowKind::GroupEquivalent) {
			const auto groupProof = BuildGroupEquivalentProof(
				snapshot, sameFormCount, a_token.formID, a_leftHand, true);
			return groupProof.IsValid(true) &&
			       groupProof.logicalRowSignature == a_token.logicalRowSignature;
		}
		const LogicalRowMember* uidFallback = nullptr;
		for (const auto& member : snapshot.members) {
			if (!IsWornInHand(member, a_leftHand)) {
				continue;
			}
			const bool uidMatches = a_token.uniqueID == 0 || member.uniqueID == a_token.uniqueID;
			if (uidMatches && member.signature == a_token.logicalRowSignature) {
				return true;
			}
			if (a_token.uniqueID != 0 && member.uniqueID == a_token.uniqueID) {
				uidFallback = std::addressof(member);
			}
		}
		return uidFallback != nullptr;
	}
}

void WheelItemWeapon::ProcessIWSCompatTransfer()
{
	ProcessPendingIWSExactWeaponTransfer();
}

void WheelItemWeapon::ProcessGroupedPoisonLineageDiagnostic()
{
	++g_groupedPoisonUpdateSequence;
	const auto decision = GroupedPoisonLineageDiagnosticPolicy::Advance(
		g_groupedPoisonWatcher,
		Wheeler::GetTransientRestorationEpoch(),
		GetGroupedPoisonDiagnosticTime(),
		g_groupedPoisonUpdateSequence);
	if (decision == GroupedPoisonLineageDiagnosticPolicy::AdvanceDecision::None) {
		return;
	}
	if (decision == GroupedPoisonLineageDiagnosticPolicy::AdvanceDecision::CancelEpoch) {
		CancelGroupedPoisonDiagnostic("lifecycle_epoch_changed");
		return;
	}
	if (decision == GroupedPoisonLineageDiagnosticPolicy::AdvanceDecision::Timeout) {
		CancelGroupedPoisonDiagnostic("bounded_timeout");
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	const auto snapshot = CaptureGroupedPoisonTargetSnapshot(
		player, g_groupedPoisonWatcher.formID, g_groupedPoisonWatcher.targetHand);
	if (!g_groupedPoisonWatcher.hasBaseline) {
		LogGroupedPoisonSnapshot(
			"PRE",
			g_groupedPoisonWatcher.transactionID,
			g_groupedPoisonUpdateSequence,
			g_groupedPoisonWatcher.formID,
			g_groupedPoisonWatcher.targetHand,
			snapshot);
		if (!snapshot.summary.readable) {
			CancelGroupedPoisonDiagnostic("initial_snapshot_unreadable");
			return;
		}
		if (!snapshot.summary.targetHandHasForm) {
			CancelGroupedPoisonDiagnostic("target_hand_occupant_changed_before_pre");
			return;
		}
		if (snapshot.summary.totalInventoryCount != g_groupedPoisonWatcher.preEquipGroupCount) {
			CancelGroupedPoisonDiagnostic("target_form_count_changed_before_pre");
			return;
		}
		g_groupedPoisonWatcher.baseline = snapshot.summary;
		g_groupedPoisonWatcher.hasBaseline = true;
		return;
	}

	if (!GroupedPoisonLineageDiagnosticPolicy::HasTopologyChanged(
			g_groupedPoisonWatcher.baseline, snapshot.summary)) {
		return;
	}

	const auto before = g_groupedPoisonWatcher.baseline;
	const auto lineage = GroupedPoisonLineageDiagnosticPolicy::Classify(before, snapshot.summary);
	const bool countConserved = before.totalInventoryCount == snapshot.summary.totalInventoryCount;
	logger::debug(
		"GROUPED_POISON MUTATION tx={} update={} classification={} countConserved={} targetHandSameForm={} preUID={} postWornUID={} exactlyOnePoisoned={} poisonedIsTargetWorn={} cleanBefore={} cleanAfter={} otherModifiedMembers={} preXList={:016X} postXList={:016X} addressesLoggingOnly=1 preLogicalSig={:016X} postLogicalSig={:016X} preTopologyDigest={:016X} postTopologyDigest={:016X}",
		g_groupedPoisonWatcher.transactionID,
		g_groupedPoisonUpdateSequence,
		GetGroupedPoisonLineageName(lineage),
		countConserved ? 1 : 0,
		snapshot.summary.targetHandHasForm ? 1 : 0,
		before.targetWornUniqueID,
		snapshot.summary.targetWornUniqueID,
		snapshot.summary.exactlyOnePoisonedMember ? 1 : 0,
		(snapshot.summary.targetMemberUnique && snapshot.summary.targetMemberPoisoned) ? 1 : 0,
		before.cleanItemCount,
		snapshot.summary.cleanItemCount,
		snapshot.summary.otherModifiedItemCount,
		static_cast<std::uint64_t>(before.targetWornXListAddress),
		static_cast<std::uint64_t>(snapshot.summary.targetWornXListAddress),
		before.targetLogicalSignatureDigest,
		snapshot.summary.targetLogicalSignatureDigest,
		before.topologyDigest,
		snapshot.summary.topologyDigest);
	LogGroupedPoisonSnapshot(
		"MUTATION",
		g_groupedPoisonWatcher.transactionID,
		g_groupedPoisonUpdateSequence,
		g_groupedPoisonWatcher.formID,
		g_groupedPoisonWatcher.targetHand,
		snapshot);
	TryCreateGroupedPoisonPresentationAlias(lineage, before, snapshot.summary);

	if (!snapshot.summary.readable) {
		CancelGroupedPoisonDiagnostic("mutation_snapshot_unreadable");
		return;
	}
	if (!snapshot.summary.targetHandHasForm) {
		CancelGroupedPoisonDiagnostic("target_hand_occupant_changed");
		return;
	}
	if (!countConserved) {
		CancelGroupedPoisonDiagnostic("target_form_count_changed");
		return;
	}
	// Continue observing the same scalar transaction so poison expiry/topology
	// reversion can be recorded within the same bounded window.
	g_groupedPoisonWatcher.baseline = snapshot.summary;
}

void WheelItemWeapon::ResetTransientStateForLifecycle()
{
	DropGroupedPoisonPresentationAlias("lifecycle_reset");
	CancelGroupedPoisonDiagnostic("lifecycle_reset");
	ClearPendingIWSExactWeaponTransfer("lifecycle_reset");
}

void WheelItemWeapon::CancelTransientStateInCurrentWorld()
{
	DropGroupedPoisonPresentationAlias("current_world_cancel");
	CancelGroupedPoisonDiagnostic("current_world_cancel");
	AbortPendingIWSExactWeaponTransfer("current_world_cancel", true);
}

void WheelItemWeapon::DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	_drawOnlyPresentationFrame = ImGui::GetFrameCount();
	_drawOnlyHandPresentation = {};
	auto* weapon = this->_obj ? this->_obj->As<RE::TESObjectWEAP>() : nullptr;
	if (TransformWheelManager::ShouldDimWeaponActivation(weapon)) {
		a_drawArgs.alphaMult *= 0.35f;
	}
	std::string text = this->GetDisplayName(a_imap);
	if (weapon && weapon->GetWeaponType() == RE::WEAPON_TYPE::kHandToHandMelee && IsPlaceholderName(text.c_str())) {
		text = "Unarmed";
	}
	int itemCount = this->GetItemExtraDataAndCount(a_imap).first;
	const auto& storedSignature = _logicalRowSignature.empty() ?
		GetPlainLogicalRowSignature() : _logicalRowSignature;
	const auto aliasPresentation = ResolveGroupedPoisonAliasPresentation(
		_runtimePresentationSlotID,
		weapon ? weapon->GetFormID() : 0,
		this->GetUniqueID(),
		HashGroupedPoisonText(storedSignature));
	if (aliasPresentation.active) {
		itemCount = static_cast<int>(aliasPresentation.count);
		_drawOnlyHandPresentation.right = aliasPresentation.right;
		_drawOnlyHandPresentation.left = aliasPresentation.left;
	}
	if (itemCount > 1) {
		text += " (" + std::to_string(itemCount) + ")";
	}
	this->drawSlotText(a_center, text.c_str(), a_drawArgs);
	this->drawSlotTexture(a_center, a_drawArgs);
	bool statusResolved = false;
	bool poisoned = false;
	bool enchanted = false;
	if (aliasPresentation.active) {
		statusResolved = true;
		poisoned = aliasPresentation.poisoned;
		enchanted = aliasPresentation.enchanted || (weapon && weapon->formEnchanting != nullptr);
	} else {
		const auto liveStatus = ResolveWeaponLiveVisualStatus(a_imap, weapon, this->GetUniqueID());
		statusResolved = liveStatus.IsSafelyResolved();
		poisoned = liveStatus.poisoned;
		enchanted = liveStatus.enchanted;
	}
	DrawWeaponLiveStatusOverlays(statusResolved, poisoned, enchanted, a_center, a_drawArgs);
}

void WheelItemWeapon::DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	std::string displayName = this->GetDisplayName(a_imap);
	auto* weapon = this->_obj ? this->_obj->As<RE::TESObjectWEAP>() : nullptr;
	if (TransformWheelManager::ShouldDimWeaponActivation(weapon)) {
		a_drawArgs.alphaMult *= 0.35f;
	}
	if (weapon && weapon->GetWeaponType() == RE::WEAPON_TYPE::kHandToHandMelee && IsPlaceholderName(displayName.c_str())) {
		displayName = "Unarmed";
	}

	RE::InventoryEntryData* invData = FindInventoryEntryByForm(a_imap, this->GetFormID());

	std::string descriptionBuf = "";
	if (!this->_description.empty()) {
		descriptionBuf = this->_description;
	}

	std::vector<RE::EnchantmentItem*> enchants;
	this->GetItemEnchantment(a_imap, enchants);
	if (!enchants.empty()) {
		std::string enchantDescription;
		for (auto* enchant : enchants) {
			if (!enchant) {
				continue;
			}
			Utils::Magic::GetMagicItemDescription(enchant, enchantDescription);
			if (!enchantDescription.empty()) {
				break;
			}
		}
		if (!enchantDescription.empty()) {
			if (descriptionBuf.empty()) {
				descriptionBuf = enchantDescription;
			} else if (descriptionBuf.find(enchantDescription) == std::string::npos) {
				descriptionBuf += "\n";
				descriptionBuf += enchantDescription;
			}
		}
	}
	const auto poisonPresentation = ResolveWeaponPoisonPresentation(a_imap, weapon ? weapon->GetFormID() : 0, this->GetUniqueID());
	descriptionBuf = AppendWeaponPoisonHighlightText(std::move(descriptionBuf), poisonPresentation);
	const float textShiftY = calculateHighlightTextShiftY(descriptionBuf.c_str());
	this->drawHighlightText(a_center, displayName.c_str(), a_drawArgs, textShiftY);
	this->drawHighlightTexture(a_center, a_drawArgs);
	
	if (!descriptionBuf.empty()) {
		this->drawHighlightDescription(a_center, descriptionBuf.data(), a_drawArgs, textShiftY);
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	const float weaponDamage = GetWeaponDamageSafe(player, invData, weapon);
	drawItemHighlightStatIconAndValue(a_center, this->_stat_texture, weaponDamage, a_drawArgs);
}

std::optional<bool> WheelItemWeapon::MatchesEquippedHandIndicator(
	RE::TESObjectREFR::InventoryItemMap& a_inv,
	RE::FormID a_handFormID,
	std::uint64_t,
	bool a_leftHand) const
{
	auto* weapon = this->_obj ? this->_obj->As<RE::TESObjectWEAP>() : nullptr;
	if (!weapon) {
		return std::nullopt;
	}
	if (a_handFormID == 0 || a_handFormID != weapon->GetFormID()) {
		return false;
	}
	const auto topology = ClassifySameFormIndicatorTopology(a_inv, weapon);
	if (topology == SameFormIndicatorTopology::kUnknown) {
		// Unknown topology is not permission to fall through to a form-only match.
		return false;
	}
	if (topology == SameFormIndicatorTopology::kUnambiguous) {
		return std::nullopt;
	}

	// Mixed same-form rows must never reach SlotHandIndicators' form-only fallback.
	// Prove membership from this frame's inventory map and current worn state.
	const bool cleanSentinel = this->GetUniqueID() == 0;
	if (IsTwoHandedIndicatorWeapon(weapon)) {
		return MatchesMixedTwoHandedIndicatorRow(a_inv, weapon, this->_logicalRowSignature, cleanSentinel);
	}
	return MatchesLogicalRowInHandFromInventory(
		a_inv,
		weapon,
		this->_logicalRowSignature,
		cleanSentinel,
		a_leftHand);
}

WeaponPresentationHandState WheelItemWeapon::GetTransientDrawOnlyHandPresentation() const
{
	if (!ImGui::GetCurrentContext() || _drawOnlyPresentationFrame != ImGui::GetFrameCount()) {
		return {};
	}
	return _drawOnlyHandPresentation;
}

WheelItemWeapon::~WheelItemWeapon()
{
	DropGroupedPoisonPresentationAliasForSlot(
		_runtimePresentationSlotID, "slot_removed_or_rebound");
}

WheelItemWeapon::WheelItemWeapon(RE::TESBoundObject* a_weapon, uint16_t a_uniqueID)
{
	_runtimePresentationSlotID = AllocateWeaponPresentationSlotID();
	this->_obj = a_weapon;
	this->SetUniqueID(a_uniqueID);
	this->_logicalRowSignature = CaptureLogicalRowSignature(a_weapon ? a_weapon->As<RE::TESObjectWEAP>() : nullptr, a_uniqueID);
	// get weapon's texture
	// TODO: add support for animated armory/2h mace
	Texture::icon_image_type iconType = Texture::icon_image_type::sword_one_handed;
	switch (a_weapon->As<RE::TESObjectWEAP>()->GetWeaponType()) {
	case RE::WEAPON_TYPE::kBow:
		iconType = Texture::icon_image_type::bow;
		break;
	case RE::WEAPON_TYPE::kCrossbow:
		iconType = Texture::icon_image_type::crossbow;
		break;
	case RE::WEAPON_TYPE::kStaff:
		iconType = Texture::icon_image_type::staff;
		break;
	case RE::WEAPON_TYPE::kHandToHandMelee:
		iconType = Texture::icon_image_type::hand_to_hand;
		break;
	case RE::WEAPON_TYPE::kOneHandSword:
		iconType = Texture::icon_image_type::sword_one_handed;
		break;
	case RE::WEAPON_TYPE::kOneHandDagger:
		iconType = Texture::icon_image_type::dagger;
		break;
	case RE::WEAPON_TYPE::kOneHandAxe:
		iconType = Texture::icon_image_type::axe_one_handed;
		break;
	case RE::WEAPON_TYPE::kOneHandMace:
		iconType = Texture::icon_image_type::mace;
		break;
	case RE::WEAPON_TYPE::kTwoHandSword:
		iconType = Texture::icon_image_type::sword_two_handed;
		break;
	case RE::WEAPON_TYPE::kTwoHandAxe:
	{
		if (a_weapon->As<RE::TESObjectWEAP>()->HasKeywordString("WeapTypeWarhammer")) {
			iconType = Texture::icon_image_type::warhammer_two_handed;
		} else {
			iconType = Texture::icon_image_type::axe_two_handed;
		}
	}
		break;
	}
	_texture = Texture::GetIconImage(iconType, a_weapon);
	_stat_texture = Texture::GetIconImage(Texture::icon_image_type::weapon_damage, nullptr);

	RE::BSString descriptionBuf = "";
	a_weapon->As<RE::TESObjectWEAP>()->GetDescription(descriptionBuf, nullptr);
	this->_description = descriptionBuf.c_str();
}

void WheelItemWeapon::ActivateItemSecondary()
{
	DropGroupedPoisonPresentationAlias("new_wheeler_weapon_activation");
	CancelGroupedPoisonDiagnostic("new_wheeler_weapon_activation");
	auto* weapon = this->_obj ? this->_obj->As<RE::TESObjectWEAP>() : nullptr;
	if (TransformWheelManager::ShouldBlockWeaponActivation(weapon)) {
		logger::info("TransformWheels: blocked weapon activation source=EquipSecondary formId={:08X} name='{}'",
			this->_obj ? this->_obj->GetFormID() : 0,
			this->_obj ? this->_obj->GetName() : "");
		return;
	}
	auto pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}
	if (!weapon) {
		return;
	}
	if (TransformWheelManager::ShouldBlockStaffActivation(weapon, "EquipSecondary")) {
		return;
	}
	auto weaponType = weapon->GetWeaponType();
	bool isTwoHanded = weapon->IsCrossbow() ||
		weapon->IsBow() ||
		weaponType == RE::WEAPON_TYPE::kTwoHandSword ||
		weaponType == RE::WEAPON_TYPE::kTwoHandAxe;

	auto inv = pc->GetInventory();
	const auto itemData = this->GetItemExtraDataAndCount(inv);
	const int count = itemData.first;
	RE::ExtraDataList* extraData = itemData.second;
	const int sameFormCount = GetSameFormInventoryCount(inv, weapon);
	const bool bypassInstanceHandResolution = ShouldBypassInstanceHandResolution(inv, weapon, this->GetUniqueID());
	const bool targetIsLeft = !isTwoHanded;
	const bool exactLogicalRowInTargetHand = MatchesLogicalRowInHandFromInventory(
		inv,
		weapon,
		this->_logicalRowSignature,
		this->GetUniqueID() == 0,
		targetIsLeft);
	const auto* targetEquippedObject = pc->GetEquippedObject(targetIsLeft);
	const bool unambiguousSingleFormFallback =
		sameFormCount == 1 &&
		targetEquippedObject &&
		targetEquippedObject->GetFormID() == weapon->GetFormID();
	const bool selectedLogicalRowInTargetHand =
		exactLogicalRowInTargetHand || unambiguousSingleFormFallback;
	if (MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Input)) {
		MainWheelDebug::Log(
			MainWheelDebug::Category::Input,
			"WeaponActivateSecondary: formId={:08X} uniqueID={} count={} sameFormCount={} hasExtraData={} useHandFallback={} logicalRowInTargetHand={}",
			this->_obj ? this->_obj->GetFormID() : 0,
			this->GetUniqueID(),
			count,
			sameFormCount,
			extraData ? 1 : 0,
			bypassInstanceHandResolution ? 1 : 0,
			selectedLogicalRowInTargetHand ? 1 : 0);
	}
	// Activation only needs scalar pre-state. Discard all inventory-owned pointers
	// before delegating to an equip/unequip path that can mutate the inventory.
	extraData = nullptr;
	inv.clear();
	if (selectedLogicalRowInTargetHand) {
		if (isTwoHanded) {
			unequipItem(Utils::Slot::GetRightHandSlot());  // note: 2 handed weapons need to be unequipped from the right hand slot to be truly unequipped.
		} else {
			unequipItem(Utils::Slot::GetLeftHandSlot());
		}
	} else {
		// Check if player was already in combat (weapon/magic drawn) BEFORE equipping
		bool wasAlreadyDrawn = pc->AsActorState()->IsWeaponDrawn();
		if (!equipItem(false)) {
			return;
		}
		if (Config::WheelBehavior::AutoDrawOnUse) {
			ActorVirtualCompat::DrawWeaponMagicHands(pc, true);  // draw whatever is in hands
		} else {
			// Only sheathe if the player was NOT already in combat.
			// If they were already drawn, maintain combat stance for smooth weapon swapping.
			if (!wasAlreadyDrawn) {
				ActorVirtualCompat::DrawWeaponMagicHands(pc, false);
			} else {
				// Force re-draw to maintain combat flow during weapon swap
				ActorVirtualCompat::DrawWeaponMagicHands(pc, true);
			}
		}
	}
}

void WheelItemWeapon::ActivateItemPrimary()
{
	DropGroupedPoisonPresentationAlias("new_wheeler_weapon_activation");
	CancelGroupedPoisonDiagnostic("new_wheeler_weapon_activation");
	auto* weapon = this->_obj ? this->_obj->As<RE::TESObjectWEAP>() : nullptr;
	if (TransformWheelManager::ShouldBlockWeaponActivation(weapon)) {
		logger::info("TransformWheels: blocked weapon activation source=EquipPrimary formId={:08X} name='{}'",
			this->_obj ? this->_obj->GetFormID() : 0,
			this->_obj ? this->_obj->GetName() : "");
		return;
	}
	auto pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}
	if (!weapon) {
		return;
	}
	if (TransformWheelManager::ShouldBlockStaffActivation(weapon, "EquipPrimary")) {
		return;
	}
	auto inv = pc->GetInventory();
	const auto itemData = this->GetItemExtraDataAndCount(inv);
	const int count = itemData.first;
	RE::ExtraDataList* extraData = itemData.second;
	const int sameFormCount = GetSameFormInventoryCount(inv, weapon);
	const bool bypassInstanceHandResolution = ShouldBypassInstanceHandResolution(inv, weapon, this->GetUniqueID());
	const bool exactLogicalRowInTargetHand = MatchesLogicalRowInHandFromInventory(
		inv,
		weapon,
		this->_logicalRowSignature,
		this->GetUniqueID() == 0,
		false);
	const auto* targetEquippedObject = pc->GetEquippedObject(false);
	const bool unambiguousSingleFormFallback =
		sameFormCount == 1 &&
		targetEquippedObject &&
		targetEquippedObject->GetFormID() == weapon->GetFormID();
	const bool selectedLogicalRowInTargetHand =
		exactLogicalRowInTargetHand || unambiguousSingleFormFallback;
	if (MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Input)) {
		MainWheelDebug::Log(
			MainWheelDebug::Category::Input,
			"WeaponActivatePrimary: formId={:08X} uniqueID={} count={} sameFormCount={} hasExtraData={} useHandFallback={} logicalRowInTargetHand={}",
			this->_obj ? this->_obj->GetFormID() : 0,
			this->GetUniqueID(),
			count,
			sameFormCount,
			extraData ? 1 : 0,
			bypassInstanceHandResolution ? 1 : 0,
			selectedLogicalRowInTargetHand ? 1 : 0);
	}
	// Activation only needs scalar pre-state. Discard all inventory-owned pointers
	// before delegating to an equip/unequip path that can mutate the inventory.
	extraData = nullptr;
	inv.clear();
	if (selectedLogicalRowInTargetHand) {
		unequipItem(Utils::Slot::GetRightHandSlot());
	} else {
		// Check if player was already in combat (weapon/magic drawn) BEFORE equipping
		bool wasAlreadyDrawn = pc->AsActorState()->IsWeaponDrawn();
		if (!equipItem(true)) {
			return;
		}
		if (Config::WheelBehavior::AutoDrawOnUse) {
			ActorVirtualCompat::DrawWeaponMagicHands(pc, true);
		} else {
			// Only sheathe if the player was NOT already in combat.
			// If they were already drawn, maintain combat stance for smooth weapon swapping.
			if (!wasAlreadyDrawn) {
				ActorVirtualCompat::DrawWeaponMagicHands(pc, false);
			} else {
				// Force re-draw to maintain combat flow during weapon swap
				ActorVirtualCompat::DrawWeaponMagicHands(pc, true);
			}
		}
	}
}

void WheelItemWeapon::SerializeIntoJsonObj(nlohmann::json& a_json)
{
	a_json["type"] = WheelItemWeapon::ITEM_TYPE_STR;
	a_json["formID"] = this->_obj->GetFormID();
	a_json["uniqueID"] = this->GetUniqueID();
	if (this->GetUniqueID() == 0) {
		a_json["formLevelStack"] = true;
	}
	if (!this->_logicalRowSignature.empty()) {
		a_json["logicalRowSignature"] = this->_logicalRowSignature;
	}
}



bool WheelItemWeapon::equipItem(bool a_toRight)
{
	try {
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !pc->Is3DLoaded()) {
			return true;
		}
		auto* weapon = this->_obj ? this->_obj->As<RE::TESObjectWEAP>() : nullptr;
		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!weapon || !equipManager) {
			return true;
		}

		RE::TESObjectREFR::InventoryItemMap inv;
		int count = 0;
		RE::ExtraDataList* extraData = nullptr;
		int sameFormCount = 0;
		bool bypassInstanceHandResolution = false;
		bool allowVanillaSentinelGroupedFallback = false;
		std::uint64_t groupedPreLogicalSignatureDigest = 0;

		// Resolve a physical member only from the current live generation. Callers
		// clear the map and pointer before every mutation, then invoke this again.
		const auto resolveFreshPhysicalMember = [&]() {
			extraData = nullptr;
			inv.clear();
			if (!Utils::Inventory::TryGetInventorySnapshot(pc, inv, "WheelItemWeapon::equipItem")) {
				return false;
			}

			auto itemData = this->GetItemExtraDataAndCount(inv);
			count = itemData.first;
			extraData = itemData.second;
			sameFormCount = GetSameFormInventoryCount(inv, weapon);
			bypassInstanceHandResolution = ShouldBypassInstanceHandResolution(inv, weapon, this->GetUniqueID());
			if (count <= 0) {
				return false;
			}

			if (bypassInstanceHandResolution) {
				if (this->_logicalRowSignature.empty() && extraData) {
					this->_logicalRowSignature = BuildLogicalRowSignature(extraData);
				}
				auto resolution = ResolveLogicalRowMember(
					inv,
					weapon->GetFormID(),
					this->GetUniqueID(),
					this->_logicalRowSignature,
					a_toRight,
					false);
				if (!resolution.chosen) {
					return false;
				}
				extraData = resolution.chosen;
				count = resolution.logicalCount;
			}

			allowVanillaSentinelGroupedFallback =
				Wheeler::GetMutableInventoryCompatProfile() == Wheeler::MutableInventoryCompatProfile::Vanilla &&
				this->GetUniqueID() == 0 &&
				!bypassInstanceHandResolution &&
				IsOneHandedWeaponForCompat(weapon) &&
				sameFormCount >= 2 &&
				this->CanUseGroupedEquipFallback(inv, extraData, sameFormCount);
			return true;
		};

		if (!resolveFreshPhysicalMember()) {
			logger::warn(
				"WeaponEquip: no safe live physical member formId={:08X} uniqueID={} targetHand={}; operation suppressed",
				weapon->GetFormID(),
				this->GetUniqueID(),
				a_toRight ? "right" : "left");
			return true;
		}

		if (QueueIWSExactSingleWeaponTransfer(
				pc,
				weapon,
				this->GetUniqueID(),
				a_toRight,
				count,
				sameFormCount,
				extraData,
				bypassInstanceHandResolution)) {
			return false;
		}

		if (!bypassInstanceHandResolution && count < 2 && !allowVanillaSentinelGroupedFallback) {  // we have less than 2, meaning we can't dual-wield
			Utils::Inventory::Hand hand = Utils::Inventory::GetWeaponEquippedHand(pc, this->_obj->As<RE::TESObjectWEAP>(), this->GetUniqueID());
			if ((hand == Utils::Inventory::Hand::Right && !a_toRight) || (hand == Utils::Inventory::Hand::Left && a_toRight)) {  // in opposite hands, simply swap l/r
				auto oppositeSlot = a_toRight ? Utils::Slot::GetLeftHandSlot() : Utils::Slot::GetRightHandSlot();  // first, clean the slot with item
				extraData = nullptr;
				inv.clear();
				InventorySnapshotCache::UnequipObject(equipManager, pc, this->_obj, nullptr, 1, oppositeSlot, false, true, true);
				if (!resolveFreshPhysicalMember()) {
					logger::warn(
						"WeaponEquip: post-unequip live resolve failed formId={:08X} uniqueID={} targetHand={}; operation suppressed",
						weapon->GetFormID(),
						this->GetUniqueID(),
						a_toRight ? "right" : "left");
					return true;
				}
			}
		}

		if (this->_obj->As<RE::TESObjectWEAP>()->IsCrossbow() || this->_obj->As<RE::TESObjectWEAP>()->IsBow()) {  // clean up both slots
			logger::debug("[HandMemoryDiag] BowLikeEquipCleanup formId={:08X} kind={} targetHand={} leftBefore={:08X} rightBefore={:08X} uniqueID={} groupedFallback={} vanillaSentinelFallback={} sameFormCount={}",
				this->_obj ? this->_obj->GetFormID() : 0,
				GetWeaponKindName(this->_obj->As<RE::TESObjectWEAP>()),
				a_toRight ? "RIGHT" : "LEFT",
				pc->GetEquippedObject(true) ? pc->GetEquippedObject(true)->GetFormID() : 0,
				pc->GetEquippedObject(false) ? pc->GetEquippedObject(false)->GetFormID() : 0,
				this->GetUniqueID(),
				0,
				allowVanillaSentinelGroupedFallback ? 1 : 0,
				sameFormCount);
			extraData = nullptr;
			inv.clear();
			Utils::Slot::CleanSlot(pc, Utils::Slot::GetLeftHandSlot());
			Utils::Slot::CleanSlot(pc, Utils::Slot::GetRightHandSlot());
			if (!resolveFreshPhysicalMember()) {
				logger::warn(
					"WeaponEquip: post-slot-cleanup live resolve failed formId={:08X} uniqueID={}; operation suppressed",
					weapon->GetFormID(),
					this->GetUniqueID());
				return true;
			}
		}

		bool usedGroupedFallback = false;
		if (!bypassInstanceHandResolution &&
		    (allowVanillaSentinelGroupedFallback || this->CanUseGroupedEquipFallback(inv, extraData, count))) {
			// Safe only when every same-form instance in the fresh snapshot belongs to
			// the same logical stack group.
			extraData = nullptr;
			usedGroupedFallback = true;
			const auto& preSignature = this->_logicalRowSignature.empty() ?
				GetPlainLogicalRowSignature() : this->_logicalRowSignature;
			groupedPreLogicalSignatureDigest = HashGroupedPoisonText(preSignature);
		}
		if (MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Input)) {
			MainWheelDebug::Log(
				MainWheelDebug::Category::Input,
				"WeaponEquip: formId={:08X} uniqueID={} count={} sameFormCount={} hasExtraData={} groupedFallback={} vanillaSentinelFallback={} targetHand={} bypassInstanceHandResolution={}",
				this->_obj ? this->_obj->GetFormID() : 0,
				this->GetUniqueID(),
				count,
				sameFormCount,
				extraData ? 1 : 0,
				usedGroupedFallback ? 1 : 0,
				allowVanillaSentinelGroupedFallback ? 1 : 0,
				a_toRight ? "right" : "left",
				bypassInstanceHandResolution ? 1 : 0);
		}

		auto slot = a_toRight ? Utils::Slot::GetRightHandSlot() : Utils::Slot::GetLeftHandSlot();
		InventorySnapshotCache::EquipObject(equipManager, pc, _obj, extraData, 1, slot);
		extraData = nullptr;
		inv.clear();
		if (usedGroupedFallback && this->GetUniqueID() == 0) {
			const auto& preSignature = this->_logicalRowSignature.empty() ?
				GetPlainLogicalRowSignature() : this->_logicalRowSignature;
			ArmGroupedPoisonDiagnostic(
				_runtimePresentationSlotID,
				weapon->GetFormID(),
				a_toRight ? Hand::Right : Hand::Left,
				sameFormCount,
				preSignature);
			logger::debug(
				"GROUPED_POISON ARM_CORRELATION preLogicalSig={:016X} source=uid0_grouped_fallback",
				groupedPreLogicalSignatureDigest);
		}
	} catch (const std::exception& e) {
		logger::error("Error while equipping weapon: {}", e.what());
	}
	return true;
}

void WheelItemWeapon::unequipItem(const RE::BGSEquipSlot* a_slot)
{
	try {
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return;
		}
		auto aeMan = RE::ActorEquipManager::GetSingleton();
		if (!aeMan) {
			return;
		}
		auto* weapon = this->_obj ? this->_obj->As<RE::TESObjectWEAP>() : nullptr;
		RE::ExtraDataList* extraData = nullptr;
		bool handAware = false;
		bool targetRight = a_slot == Utils::Slot::GetRightHandSlot();
		RE::TESObjectREFR::InventoryItemMap inv;
		if (weapon) {
			if (!Utils::Inventory::TryGetInventorySnapshot(pc, inv, "WheelItemWeapon::unequipItem")) {
				return;
			}
			auto itemData = this->GetItemExtraDataAndCount(inv);
			handAware = ShouldBypassInstanceHandResolution(inv, weapon, this->GetUniqueID());
			if (handAware) {
				if (this->_logicalRowSignature.empty() && itemData.second) {
					this->_logicalRowSignature = BuildLogicalRowSignature(itemData.second);
				}
				auto resolution = ResolveLogicalRowMember(
					inv,
					weapon->GetFormID(),
					this->GetUniqueID(),
					this->_logicalRowSignature,
					targetRight,
					true);
				if (!resolution.chosen) {
					logger::warn(
						"WeaponUnequip: no safe live physical member formId={:08X} storedUID={} targetHand={}; operation suppressed",
						weapon->GetFormID(),
						this->GetUniqueID(),
						targetRight ? "right" : "left");
					return;
				}
				extraData = resolution.chosen;
			}
		}
		InventorySnapshotCache::UnequipObject(aeMan, pc, this->_obj, extraData, 1, a_slot);
		extraData = nullptr;
		inv.clear();
	} catch (const std::exception& e) {
		logger::error("Error while unequip item: {}", e.what());
	}
}

bool WheelItemWeapon::IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	try {
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return false;
		}
		auto itemData = this->GetItemExtraDataAndCount(a_inv);
		// Read the UID after maintenance: promotion/collapse/relink keep their
		// original order and nonzero-UID active resolution remains unchanged.
		if (this->GetUniqueID() == 0) {
			if (const auto active = ResolveUID0ActiveVisualOverride(a_inv, this->_obj->As<RE::TESObjectWEAP>());
			    active.has_value()) {
				return *active;
			}
		}
		if (ShouldBypassInstanceHandResolution(a_inv, this->_obj->As<RE::TESObjectWEAP>(), this->GetUniqueID())) {
			return GetEquippedHandByExactUniqueID(pc, this->_obj->As<RE::TESObjectWEAP>(), this->GetUniqueID()) != Utils::Inventory::Hand::None;
		}
		if (itemData.first >= 2) {
			return Utils::Inventory::GetWeaponEquippedHand(pc, this->_obj->As<RE::TESObjectWEAP>(), this->GetUniqueID(), true) != Utils::Inventory::Hand::None;
		} else {
			return Utils::Inventory::GetWeaponEquippedHand(pc, this->_obj->As<RE::TESObjectWEAP>(), this->GetUniqueID()) != Utils::Inventory::Hand::None;
		}
	} catch (const std::exception& e) {
		logger::error("Error while checking if item is active: {}", e.what());
		return false;
	}

}
bool WheelItemWeapon::IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	auto pc = RE::PlayerCharacter::GetSingleton();

	auto itemData = this->GetItemExtraDataAndCount(a_inv);

	return itemData.first > 0;
}
