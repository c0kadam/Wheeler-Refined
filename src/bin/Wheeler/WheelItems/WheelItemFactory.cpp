#include "nlohmann/json.hpp"

#include "WheelItemFactory.h"
#include "bin/Utilities/Utils.h"
#include "bin/Utilities/UniqueIDHandler.h"
#include "bin/Utilities/ItemCapabilities.h"
#include "bin/Wheeler/MainWheelDebug.h"
#include "bin/LogGate.h"
#include "bin/Config.h"
#include "WheelItem.h"
#include "WheelItemSpell.h"
#include "WheelItemWeapon.h"
#include "WheelItemArmor.h"
#include "WheelItemShout.h"
#include "WheelItemLight.h"
#include "WheelItemAmmo.h"
#include "WheelItemAlchemy.h"
#include "WheelItemIngredient.h"
#include "WheelItemScroll.h"
#include "WheelItemMisc.h"
#include "WheelItemBook.h"
#include "WheelItemMissing.h"
#include "WheelItemExternalHotkey.h"
#include "WheelItemOStimAction.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace
{
	MissingCategory ReadMissingCategory(const nlohmann::json& j_item)
	{
		if (j_item.contains("missingCategory")) {
			try {
				return MissingCategoryFromInt(j_item["missingCategory"].get<int>());
			} catch (...) {
				return MissingCategory::Unknown;
			}
		}
		return MissingCategory::Unknown;
	}

	std::string ReadMissingName(const nlohmann::json& j_item)
	{
		if (j_item.contains("missingName")) {
			try {
				return j_item["missingName"].get<std::string>();
			} catch (...) {
				return {};
			}
		}
		return {};
	}

	std::uint16_t ReadUniqueID(const nlohmann::json& j_item)
	{
		if (j_item.contains("uniqueID")) {
			try {
				return j_item["uniqueID"].get<std::uint16_t>();
			} catch (...) {
				return 0;
			}
		}
		return 0;
	}

	bool ReadFormLevelStack(const nlohmann::json& j_item)
	{
		if (j_item.contains("formLevelStack")) {
			try {
				return j_item["formLevelStack"].get<bool>();
			} catch (...) {
				return false;
			}
		}
		return false;
	}

	MissingCategory ResolveMissingCategory(const nlohmann::json& j_item, std::string_view type, RE::TESForm* baseForm)
	{
		MissingCategory category = ReadMissingCategory(j_item);
		if (category == MissingCategory::Unknown && baseForm) {
			category = DetermineMissingCategory(baseForm);
		}
		if (category == MissingCategory::Unknown) {
			category = InferMissingCategoryFromItemType(type);
		}
		return category;
	}

	std::shared_ptr<WheelItem> WithMissingCategory(std::shared_ptr<WheelItem> item, RE::TESForm* baseForm)
	{
		if (!item) {
			return nullptr;
		}
		MissingCategory category = item->GetMissingCategory();
		if (category == MissingCategory::Unknown && baseForm) {
			category = DetermineMissingCategory(baseForm);
		}
		item->SetMissingCategory(category);
		return item;
	}

	int GetExtraListCount(RE::ExtraDataList* a_extraList)
	{
		if (!a_extraList) {
			return 0;
		}

		const int count = a_extraList->GetCount();
		return (std::max)(count, 1);
	}

	bool IsCleanExtraList(RE::ExtraDataList* a_extraList)
	{
		return a_extraList &&
		       !a_extraList->HasType(RE::ExtraDataType::kEnchantment) &&
		       !a_extraList->HasType(RE::ExtraDataType::kPoison) &&
		       !a_extraList->HasType(RE::ExtraDataType::kHealth);
	}

	bool HasInstanceSpecificMutableData(RE::ExtraDataList* a_extraList)
	{
		return a_extraList &&
		       (a_extraList->HasType(RE::ExtraDataType::kEnchantment) ||
		        a_extraList->HasType(RE::ExtraDataType::kPoison) ||
		        a_extraList->HasType(RE::ExtraDataType::kHealth) ||
		        a_extraList->HasType(RE::ExtraDataType::kCharge) ||
		        a_extraList->GetByType<RE::ExtraTextDisplayData>() != nullptr);
	}

	std::uint16_t TryGetUniqueIDFromEntry(RE::InventoryEntryData* a_entry)
	{
		if (!a_entry || !a_entry->extraLists) {
			return 0;
		}

		for (auto* extraList : *a_entry->extraLists) {
			if (!extraList) {
				continue;
			}
			if (auto* uniqueIDData = extraList->GetByType<RE::ExtraUniqueID>()) {
				if (uniqueIDData->uniqueID != 0) {
					return uniqueIDData->uniqueID;
				}
			}
		}

		return 0;
	}

	std::uint16_t GetUniqueIDFromExtraList(RE::ExtraDataList* a_extraList)
	{
		if (!a_extraList) {
			return 0;
		}
		if (auto* uniqueIDData = a_extraList->GetByType<RE::ExtraUniqueID>()) {
			return uniqueIDData->uniqueID;
		}
		return 0;
	}

	std::string NormalizeDisplayName(std::string_view a_value)
	{
		std::string normalized;
		normalized.reserve(a_value.size());
		for (unsigned char ch : a_value) {
			if (std::isalnum(ch)) {
				normalized.push_back(static_cast<char>(std::tolower(ch)));
			}
		}
		return normalized;
	}

	std::optional<double> TryGetGFxNumberMember(RE::GFxValue& a_object, const char* a_memberName)
	{
		RE::GFxValue memberValue;
		if (!a_object.GetMember(a_memberName, &memberValue) ||
		    memberValue.GetType() != RE::GFxValue::ValueType::kNumber) {
			return std::nullopt;
		}

		return memberValue.GetNumber();
	}

	std::optional<bool> TryGetGFxBoolMember(RE::GFxValue& a_object, const char* a_memberName)
	{
		RE::GFxValue memberValue;
		if (!a_object.GetMember(a_memberName, &memberValue) ||
		    memberValue.GetType() != RE::GFxValue::ValueType::kBoolean) {
			return std::nullopt;
		}

		return memberValue.GetBool();
	}

	std::optional<std::string> TryGetGFxStringMember(RE::GFxValue& a_object, const char* a_memberName)
	{
		RE::GFxValue memberValue;
		if (!a_object.GetMember(a_memberName, &memberValue) ||
		    memberValue.GetType() != RE::GFxValue::ValueType::kString) {
			return std::nullopt;
		}

		const char* text = memberValue.GetString();
		if (!text || text[0] == '\0') {
			return std::nullopt;
		}

		return std::string(text);
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

	template <class TValue, class Fn>
	std::optional<TValue> InvokeValueSafe(Fn&& a_fn)
	{
		TValue value{};
		if (!InvokeWithSehGuard([&]() { value = a_fn(); })) {
			return std::nullopt;
		}
		return value;
	}

	bool NearlyEqual(double a_lhs, double a_rhs, double a_epsilon = 0.05)
	{
		return std::fabs(a_lhs - a_rhs) <= a_epsilon;
	}

	struct InventoryRowSignature
	{
		std::string normalizedLabel;
		int count = 0;
		std::optional<int> value;
		std::optional<double> weight;
		std::optional<double> damage;
		std::optional<double> armor;
		std::optional<bool> isEnchanted;
		std::optional<bool> isPoisoned;
	};

	bool HasDiscriminatingSignal(const InventoryRowSignature& a_signature)
	{
		return a_signature.value.has_value() ||
		       a_signature.weight.has_value() ||
		       a_signature.damage.has_value() ||
		       a_signature.armor.has_value() ||
		       a_signature.isEnchanted.has_value() ||
		       a_signature.isPoisoned.has_value();
	}

	InventoryRowSignature BuildSelectedInventoryRowSignature(RE::ItemList::Item* a_selectedItem)
	{
		InventoryRowSignature signature{};
		if (!a_selectedItem) {
			return signature;
		}

		signature.count = (std::max)(static_cast<int>(a_selectedItem->data.GetCount()), 1);

		if (const auto text = TryGetGFxStringMember(a_selectedItem->obj, "text")) {
			signature.normalizedLabel = NormalizeDisplayName(*text);
		} else if (const auto name = TryGetGFxStringMember(a_selectedItem->obj, "name")) {
			signature.normalizedLabel = NormalizeDisplayName(*name);
		} else if (const char* selectedName = a_selectedItem->data.GetName();
		           selectedName && selectedName[0] != '\0') {
			signature.normalizedLabel = NormalizeDisplayName(selectedName);
		}

		if (const auto value = TryGetGFxNumberMember(a_selectedItem->obj, "value")) {
			signature.value = static_cast<int>(std::lround(*value));
		}
		signature.weight = TryGetGFxNumberMember(a_selectedItem->obj, "weight");
		if (const auto damage = TryGetGFxNumberMember(a_selectedItem->obj, "damage")) {
			signature.damage = *damage;
		}
		if (const auto armor = TryGetGFxNumberMember(a_selectedItem->obj, "armor")) {
			signature.armor = *armor;
		}
		signature.isEnchanted = TryGetGFxBoolMember(a_selectedItem->obj, "isEnchanted");
		signature.isPoisoned = TryGetGFxBoolMember(a_selectedItem->obj, "isPoisoned");

		return signature;
	}

	InventoryRowSignature BuildInventoryCandidateSignature(
		RE::PlayerCharacter* a_pc,
		RE::TESBoundObject* a_boundObj,
		RE::ExtraDataList* a_extraList,
		int a_count)
	{
		InventoryRowSignature signature{};
		if (!a_boundObj || a_count <= 0) {
			return signature;
		}

		signature.count = a_count;
		RE::InventoryEntryData isolatedEntry(a_boundObj, a_count);
		if (a_extraList) {
			isolatedEntry.AddExtraList(a_extraList);
		}

		if (const auto displayName = InvokeValueSafe<const char*>([&]() { return isolatedEntry.GetDisplayName(); });
		    displayName && *displayName && (*displayName)[0] != '\0') {
			signature.normalizedLabel = NormalizeDisplayName(*displayName);
		} else if (const char* baseName = a_boundObj->GetName(); baseName && baseName[0] != '\0') {
			signature.normalizedLabel = NormalizeDisplayName(baseName);
		}

		if (const auto value = InvokeValueSafe<std::int32_t>([&]() { return isolatedEntry.GetValue(); })) {
			signature.value = *value;
		}
		if (const auto weight = InvokeValueSafe<float>([&]() { return isolatedEntry.GetWeight(); })) {
			signature.weight = *weight;
		}
		if (const auto enchanted = InvokeValueSafe<bool>([&]() { return isolatedEntry.IsEnchanted(); })) {
			signature.isEnchanted = *enchanted;
		}
		if (const auto poisoned = InvokeValueSafe<bool>([&]() { return isolatedEntry.IsPoisoned(); })) {
			signature.isPoisoned = *poisoned;
		}

		if (a_pc) {
			if (a_boundObj->As<RE::TESObjectWEAP>()) {
				if (const auto damage = InvokeValueSafe<float>([&]() { return a_pc->GetDamage(&isolatedEntry); })) {
					signature.damage = *damage;
				}
			} else if (a_boundObj->As<RE::TESObjectARMO>()) {
				if (const auto armor = InvokeValueSafe<float>([&]() { return a_pc->GetArmorValue(&isolatedEntry); })) {
					signature.armor = *armor;
				}
			}
		}

		return signature;
	}

	struct SignatureMatchResult
	{
		bool compatible = false;
		int score = 0;
	};

	SignatureMatchResult EvaluateRowSignatureMatch(
		const InventoryRowSignature& a_selected,
		const InventoryRowSignature& a_candidate)
	{
		SignatureMatchResult result{ true, 0 };

		if (a_selected.count > 0) {
			if (a_candidate.count != a_selected.count) {
				return {};
			}
			result.score += 1;
		}

		if (!a_selected.normalizedLabel.empty()) {
			if (a_candidate.normalizedLabel != a_selected.normalizedLabel) {
				return {};
			}
			result.score += 1;
		}

		if (a_selected.value) {
			if (!a_candidate.value || *a_candidate.value != *a_selected.value) {
				return {};
			}
			result.score += 2;
		}

		if (a_selected.weight) {
			if (!a_candidate.weight || !NearlyEqual(*a_candidate.weight, *a_selected.weight, 0.05)) {
				return {};
			}
			result.score += 1;
		}

		if (a_selected.damage) {
			if (!a_candidate.damage || !NearlyEqual(*a_candidate.damage, *a_selected.damage, 0.05)) {
				return {};
			}
			result.score += 3;
		}

		if (a_selected.armor) {
			if (!a_candidate.armor || !NearlyEqual(*a_candidate.armor, *a_selected.armor, 0.05)) {
				return {};
			}
			result.score += 3;
		}

		if (a_selected.isEnchanted) {
			if (!a_candidate.isEnchanted || *a_candidate.isEnchanted != *a_selected.isEnchanted) {
				return {};
			}
			result.score += 1;
		}

		if (a_selected.isPoisoned) {
			if (!a_candidate.isPoisoned || *a_candidate.isPoisoned != *a_selected.isPoisoned) {
				return {};
			}
			result.score += 1;
		}

		result.compatible = true;
		return result;
	}

	struct ExtraListSignature
	{
		bool clean = true;
		bool hasHealth = false;
		float health = 0.0f;
		bool hasEnchantment = false;
		RE::EnchantmentItem* enchantment = nullptr;
		std::uint16_t charge = 0;
		bool removeOnUnequip = false;
		bool hasPoison = false;
		RE::AlchemyItem* poison = nullptr;
		std::uint32_t poisonCount = 0;
	};

	bool TryBuildExtraListSignature(RE::ExtraDataList* a_list, ExtraListSignature& a_outSignature)
	{
		if (!a_list) {
			return false;
		}

		if (auto* healthData = a_list->GetByType<RE::ExtraHealth>()) {
			a_outSignature.hasHealth = true;
			a_outSignature.health = healthData->health;
		}

		if (auto* enchantmentData = a_list->GetByType<RE::ExtraEnchantment>()) {
			a_outSignature.hasEnchantment = true;
			a_outSignature.enchantment = enchantmentData->enchantment;
			a_outSignature.charge = enchantmentData->charge;
			a_outSignature.removeOnUnequip = enchantmentData->removeOnUnequip;
		}

		if (auto* poisonData = a_list->GetByType<RE::ExtraPoison>()) {
			a_outSignature.hasPoison = true;
			a_outSignature.poison = poisonData->poison;
			a_outSignature.poisonCount = poisonData->count;
		}

		a_outSignature.clean = !a_outSignature.hasHealth &&
		                       !a_outSignature.hasEnchantment &&
		                       !a_outSignature.hasPoison;
		return true;
	}

	bool AreEquivalentExtraLists(RE::ExtraDataList* a_lhs, RE::ExtraDataList* a_rhs)
	{
		if (a_lhs == a_rhs) {
			return true;
		}
		if (!a_lhs || !a_rhs) {
			return false;
		}

		ExtraListSignature lhsSignature{};
		ExtraListSignature rhsSignature{};
		if (!TryBuildExtraListSignature(a_lhs, lhsSignature) ||
		    !TryBuildExtraListSignature(a_rhs, rhsSignature)) {
			return false;
		}

		return lhsSignature.clean == rhsSignature.clean &&
		       lhsSignature.hasHealth == rhsSignature.hasHealth &&
		       (!lhsSignature.hasHealth || lhsSignature.health == rhsSignature.health) &&
		       lhsSignature.hasEnchantment == rhsSignature.hasEnchantment &&
		       (!lhsSignature.hasEnchantment ||
		        (lhsSignature.enchantment == rhsSignature.enchantment &&
		         lhsSignature.charge == rhsSignature.charge &&
		         lhsSignature.removeOnUnequip == rhsSignature.removeOnUnequip)) &&
		       lhsSignature.hasPoison == rhsSignature.hasPoison &&
		       (!lhsSignature.hasPoison ||
		        (lhsSignature.poison == rhsSignature.poison &&
		         lhsSignature.poisonCount == rhsSignature.poisonCount));
	}

	std::string DescribeGFxValue(const RE::GFxValue& a_value)
	{
		switch (a_value.GetType()) {
		case RE::GFxValue::ValueType::kUndefined:
			return "undefined";
		case RE::GFxValue::ValueType::kNull:
			return "null";
		case RE::GFxValue::ValueType::kBoolean:
			return a_value.GetBool() ? "true" : "false";
		case RE::GFxValue::ValueType::kNumber:
			return fmt::format("{}", a_value.GetNumber());
		case RE::GFxValue::ValueType::kString:
			return fmt::format("'{}'", a_value.GetString() ? a_value.GetString() : "");
		case RE::GFxValue::ValueType::kStringW:
			return "<wstr>";
		case RE::GFxValue::ValueType::kObject:
			return "<object>";
		case RE::GFxValue::ValueType::kArray:
			return "<array>";
		case RE::GFxValue::ValueType::kDisplayObject:
			return "<display>";
		default:
			return "<unknown>";
		}
	}

	void LogSelectedInventoryRow(RE::ItemList::Item* a_selectedItem)
	{
		if (!a_selectedItem || !MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Input)) {
			return;
		}

		std::string details = fmt::format(
			"name='{}' count={} equipState={} filterFlag={} favorite={}",
			a_selectedItem->data.GetName() ? a_selectedItem->data.GetName() : "",
			a_selectedItem->data.GetCount(),
			a_selectedItem->data.GetEquipState(),
			a_selectedItem->data.GetFilterFlag(),
			a_selectedItem->data.GetFavorite());

		constexpr std::array<const char*, 22> kMembers = {
			"formId",
			"formID",
			"id",
			"name",
			"text",
			"label",
			"count",
			"equipState",
			"filterFlag",
			"favorite",
			"enabled",
			"index",
			"itemIndex",
			"entryIndex",
			"isEnchanted",
			"isPoisoned",
			"value",
			"weight",
			"damage",
			"armor",
			"infoDamage",
			"infoArmor"
		};

		for (const char* memberName : kMembers) {
			RE::GFxValue memberValue;
			if (a_selectedItem->obj.GetMember(memberName, &memberValue)) {
				details += fmt::format(" {}={}", memberName, DescribeGFxValue(memberValue));
			}
		}

		MainWheelDebug::Log(MainWheelDebug::Category::Input, "FactoryMenuHover: selectedRow {}", details);
	}

	struct InventoryFormState
	{
		int totalCount = 0;
		int representedCount = 0;
		int cleanCount = 0;
		int plainCount = 0;
		int instanceSpecificCount = 0;
		int uniqueCandidateCount = 0;
		std::uint16_t firstUniqueID = 0;
	};

	int GetImplicitCleanCount(const InventoryFormState& a_state)
	{
		return (std::max)(0, a_state.totalCount - a_state.representedCount);
	}

	bool IsPureCleanUnresolvedStack(const InventoryFormState& a_state)
	{
		return a_state.totalCount > 0 &&
		       a_state.representedCount == 0 &&
		       a_state.cleanCount == a_state.totalCount &&
		       a_state.uniqueCandidateCount == 0;
	}

	bool ShouldAllowInstanceSentinelFallback(RE::FormType a_formType, const InventoryFormState& a_state)
	{
		if (a_formType != RE::FormType::Weapon && a_formType != RE::FormType::Armor) {
			return false;
		}

		return IsPureCleanUnresolvedStack(a_state);
	}

	bool ShouldPreferPlainStackSentinel(RE::FormType a_formType, const InventoryFormState& a_state)
	{
		if (a_formType != RE::FormType::Weapon && a_formType != RE::FormType::Armor) {
			return false;
		}

		// FavWheel keeps plain duplicate weapon/armor stacks base-form keyed even after
		// uniqueness has been assigned. Exact uniqueIDs are reserved for modified copies.
		return a_state.totalCount >= 2 &&
		       a_state.instanceSpecificCount == 0 &&
		       a_state.plainCount == a_state.totalCount &&
		       a_state.cleanCount == a_state.totalCount;
	}

	InventoryFormState AnalyzeInventoryForm(RE::PlayerCharacter* a_pc, RE::FormID a_formID)
	{
		InventoryFormState state{};
		if (!a_pc || a_formID == 0) {
			return state;
		}

		auto inventoryMap = a_pc->GetInventory();
		for (auto& [boundObj, data] : inventoryMap) {
			if (!boundObj || boundObj->GetFormID() != a_formID) {
				continue;
			}

			state.totalCount = data.first;
			if (data.second && data.second->extraLists) {
				for (auto* extraList : *data.second->extraLists) {
					if (!extraList) {
						continue;
					}

					const int listCount = GetExtraListCount(extraList);
					state.representedCount += listCount;
					if (IsCleanExtraList(extraList)) {
						state.cleanCount += listCount;
					}
					if (HasInstanceSpecificMutableData(extraList)) {
						state.instanceSpecificCount += listCount;
					} else {
						state.plainCount += listCount;
					}

					if (auto* uniqueIDData = extraList->GetByType<RE::ExtraUniqueID>()) {
						if (uniqueIDData->uniqueID != 0) {
							if (state.firstUniqueID == 0) {
								state.firstUniqueID = uniqueIDData->uniqueID;
							}
							state.uniqueCandidateCount++;
						}
					}
				}
			}

			const int implicitCleanCount = (std::max)(0, state.totalCount - state.representedCount);
			state.cleanCount += implicitCleanCount;
			state.plainCount += implicitCleanCount;
			break;
		}

		return state;
	}

	struct RowAwareMutableCandidate
	{
		RE::ExtraDataList* extraList = nullptr;
		int count = 0;
		bool clean = false;
		bool plain = false;
		bool instanceSpecific = false;
		bool implicitClean = false;
		std::uint16_t uniqueID = 0;
		bool nameMatch = false;
		InventoryRowSignature signature{};
		int signatureScore = 0;
	};

	struct RowAwareMutableBindResult
	{
		bool resolved = false;
		std::uint16_t uniqueID = 0;
		RE::ExtraDataList* selectedExtraList = nullptr;
		const char* reason = "row_unresolved";
		int rowCount = 0;
		int matchedCount = 0;
		int cleanCount = 0;
		int plainCount = 0;
		int instanceSpecificCount = 0;
		int uniqueCandidateCount = 0;
	};

	bool ShouldFallbackImplicitCleanSingletonToFormLevel(const RowAwareMutableBindResult& a_result)
	{
		return a_result.resolved &&
		       a_result.uniqueID == 0 &&
		       a_result.selectedExtraList == nullptr &&
		       a_result.rowCount == 1 &&
		       a_result.matchedCount == 1 &&
		       a_result.cleanCount == 1 &&
		       a_result.plainCount == 1 &&
		       a_result.instanceSpecificCount == 0 &&
		       a_result.uniqueCandidateCount == 0;
	}

	bool ShouldBindImplicitCleanSingletonSentinel(
		RE::TESBoundObject* a_boundObj,
		RE::ItemList::Item* a_selectedItem,
		const RowAwareMutableBindResult& a_rowAwareBind,
		const InventoryFormState& a_inventoryState)
	{
		if (!a_boundObj || !a_selectedItem || a_rowAwareBind.resolved) {
			return false;
		}

		const int selectedCount = (std::max)(static_cast<int>(a_selectedItem->data.GetCount()), 1);
		const int implicitCleanCount = GetImplicitCleanCount(a_inventoryState);
		if (selectedCount <= 0 || implicitCleanCount != selectedCount) {
			return false;
		}

		if (a_inventoryState.instanceSpecificCount <= 0 || a_inventoryState.uniqueCandidateCount <= 0) {
			return false;
		}

		if (a_inventoryState.cleanCount < selectedCount) {
			return false;
		}

		const InventoryRowSignature selectedSignature = BuildSelectedInventoryRowSignature(a_selectedItem);
		if (!HasDiscriminatingSignal(selectedSignature)) {
			return false;
		}

		const InventoryRowSignature implicitCleanSignature =
			BuildInventoryCandidateSignature(RE::PlayerCharacter::GetSingleton(), a_boundObj, nullptr, selectedCount);
		return EvaluateRowSignatureMatch(selectedSignature, implicitCleanSignature).compatible;
	}

	RowAwareMutableBindResult ResolveRowAwareMutableBinding(
		RE::TESBoundObject* a_boundObj,
		RE::InventoryEntryData* a_invEntry,
		RE::ItemList::Item* a_selectedItem)
	{
		RowAwareMutableBindResult result{};
		if (!a_boundObj || !a_invEntry || !a_selectedItem) {
			return result;
		}

		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		result.rowCount = (std::max)(static_cast<int>(a_selectedItem->data.GetCount()), 1);
		const int formTotalCount = (std::max)(a_invEntry->countDelta, result.rowCount);
		const InventoryRowSignature selectedSignature = BuildSelectedInventoryRowSignature(a_selectedItem);

		const char* selectedNameRaw = a_selectedItem->data.GetName();
		if (!selectedNameRaw || selectedNameRaw[0] == '\0') {
			selectedNameRaw = a_boundObj->GetName();
		}
		const std::string normalizedSelectedName = NormalizeDisplayName(selectedNameRaw ? selectedNameRaw : "");

		std::vector<RowAwareMutableCandidate> allCandidates;
		if (a_invEntry->extraLists) {
			for (auto* extraList : *a_invEntry->extraLists) {
				if (!extraList) {
					continue;
				}

				RowAwareMutableCandidate candidate{};
				candidate.extraList = extraList;
				candidate.count = GetExtraListCount(extraList);
				candidate.clean = IsCleanExtraList(extraList);
				candidate.instanceSpecific = HasInstanceSpecificMutableData(extraList);
				candidate.plain = !candidate.instanceSpecific;
				candidate.uniqueID = GetUniqueIDFromExtraList(extraList);
				candidate.signature = BuildInventoryCandidateSignature(pc, a_boundObj, extraList, candidate.count);

				std::string displayName;
				if (const char* entryDisplayName = a_invEntry->GetDisplayName();
				    entryDisplayName && entryDisplayName[0] != '\0') {
					displayName = entryDisplayName;
				} else if (const char* baseName = a_boundObj->GetName();
				           baseName && baseName[0] != '\0') {
					displayName = baseName;
				}
				RE::InventoryEntryData isolatedEntry(a_boundObj, candidate.count);
				isolatedEntry.AddExtraList(extraList);
				if (const char* isolatedName = isolatedEntry.GetDisplayName();
				    isolatedName && isolatedName[0] != '\0') {
					displayName = isolatedName;
				}

				if (NormalizeDisplayName(displayName) == normalizedSelectedName) {
					candidate.nameMatch = true;
				}

				allCandidates.push_back(candidate);
			}
		}

		int representedCount = 0;
		for (const auto& candidate : allCandidates) {
			representedCount += candidate.count;
		}
		const int implicitCleanCount = (std::max)(0, formTotalCount - representedCount);
		if (implicitCleanCount > 0) {
			RowAwareMutableCandidate implicitCandidate{};
			implicitCandidate.count = implicitCleanCount;
			implicitCandidate.clean = true;
			implicitCandidate.plain = true;
			implicitCandidate.instanceSpecific = false;
			implicitCandidate.implicitClean = true;
			implicitCandidate.uniqueID = 0;
			implicitCandidate.signature = BuildInventoryCandidateSignature(pc, a_boundObj, nullptr, implicitCleanCount);
			implicitCandidate.nameMatch =
				implicitCandidate.signature.normalizedLabel == normalizedSelectedName;
			allCandidates.push_back(implicitCandidate);
		}

		const bool hasNameMatch = std::any_of(
			allCandidates.begin(),
			allCandidates.end(),
			[](const RowAwareMutableCandidate& a_candidate) { return a_candidate.nameMatch; });

		std::vector<RowAwareMutableCandidate> candidates;
		candidates.reserve(allCandidates.size());
		for (const auto& candidate : allCandidates) {
			if (!hasNameMatch || candidate.nameMatch) {
				candidates.push_back(candidate);
			}
		}

		std::vector<std::uint16_t> uniqueIDs;
		for (const auto& candidate : candidates) {
			result.matchedCount += candidate.count;
			if (candidate.clean) {
				result.cleanCount += candidate.count;
			}
			if (candidate.plain) {
				result.plainCount += candidate.count;
			}
			if (candidate.instanceSpecific) {
				result.instanceSpecificCount += candidate.count;
			}

			if (candidate.uniqueID != 0) {
				if (std::find(uniqueIDs.begin(), uniqueIDs.end(), candidate.uniqueID) == uniqueIDs.end()) {
					uniqueIDs.push_back(candidate.uniqueID);
				}
			}
		}
		result.uniqueCandidateCount = static_cast<int>(uniqueIDs.size());

		std::vector<RowAwareMutableCandidate*> countMatchedCandidates;
		countMatchedCandidates.reserve(candidates.size());
		for (auto& candidate : candidates) {
			if (candidate.count != result.rowCount) {
				continue;
			}

			const auto signatureMatch = EvaluateRowSignatureMatch(selectedSignature, candidate.signature);
			candidate.signatureScore = signatureMatch.compatible ? signatureMatch.score : -1;
			countMatchedCandidates.push_back(&candidate);
		}

		auto pickResolvedCandidate = [&](RowAwareMutableCandidate* a_candidate, const char* a_reason) -> bool {
			if (!a_candidate) {
				return false;
			}

			result.resolved = true;
			result.uniqueID = a_candidate->implicitClean ? 0 : a_candidate->uniqueID;
			result.selectedExtraList = a_candidate->extraList;
			result.reason = a_reason;
			return true;
		};

		std::vector<RowAwareMutableCandidate*> signatureMatches;
		int bestSignatureScore = -1;
		for (auto* candidate : countMatchedCandidates) {
			if (!candidate || candidate->signatureScore < 0) {
				continue;
			}
			if (candidate->signatureScore > bestSignatureScore) {
				signatureMatches.clear();
				bestSignatureScore = candidate->signatureScore;
			}
			if (candidate->signatureScore == bestSignatureScore) {
				signatureMatches.push_back(candidate);
			}
		}

		if (signatureMatches.size() == 1) {
			auto* exactCandidate = signatureMatches.front();
			if (exactCandidate->implicitClean) {
				pickResolvedCandidate(
					exactCandidate,
					exactCandidate->count > 1 ? "row_clean_sentinel" : "row_clean_implicit_exact");
				return result;
			}
			if (exactCandidate->instanceSpecific) {
				pickResolvedCandidate(exactCandidate, "row_modified_exact");
				return result;
			}
			if (exactCandidate->clean && !exactCandidate->instanceSpecific) {
				pickResolvedCandidate(
					exactCandidate,
					exactCandidate->count > 1 ? "row_clean_sentinel" : "row_clean_exact");
				return result;
			}
			pickResolvedCandidate(exactCandidate, "row_exact_unique");
			return result;
		}

		const auto findSingleCandidate = [&](bool a_requireClean, bool a_requireInstanceSpecific) -> RE::ExtraDataList* {
			RE::ExtraDataList* found = nullptr;
			for (auto* candidate : countMatchedCandidates) {
				if (!candidate || candidate->uniqueID == 0) {
					continue;
				}
				if (a_requireClean && !candidate->clean) {
					continue;
				}
				if (a_requireInstanceSpecific && !candidate->instanceSpecific) {
					continue;
				}
				if (found) {
					return nullptr;
				}
				found = candidate->extraList;
			}
			return found;
		};

		if (result.rowCount > 1 &&
		    result.instanceSpecificCount == 0 &&
		    result.cleanCount >= result.rowCount &&
		    result.plainCount >= result.rowCount) {
			result.resolved = true;
			result.uniqueID = 0;
			result.reason = "row_clean_sentinel";
			return result;
		}

		const int countMatchedInstanceSpecific = static_cast<int>(std::count_if(
			countMatchedCandidates.begin(),
			countMatchedCandidates.end(),
			[](const RowAwareMutableCandidate* a_candidate) {
				return a_candidate && a_candidate->instanceSpecific && a_candidate->uniqueID != 0;
			}));
		const int countMatchedCleanExplicit = static_cast<int>(std::count_if(
			countMatchedCandidates.begin(),
			countMatchedCandidates.end(),
			[](const RowAwareMutableCandidate* a_candidate) {
				return a_candidate && !a_candidate->implicitClean && a_candidate->clean && !a_candidate->instanceSpecific &&
				       a_candidate->uniqueID != 0;
			}));
		const int countMatchedImplicitClean = static_cast<int>(std::count_if(
			countMatchedCandidates.begin(),
			countMatchedCandidates.end(),
			[](const RowAwareMutableCandidate* a_candidate) {
				return a_candidate && a_candidate->implicitClean;
			}));

		if (result.rowCount == 1 && countMatchedImplicitClean == 0) {
			if (RE::ExtraDataList* exactModified = findSingleCandidate(false, true)) {
				result.resolved = true;
				result.selectedExtraList = exactModified;
				result.uniqueID = GetUniqueIDFromExtraList(exactModified);
				result.reason = "row_modified_exact";
				if (result.uniqueID != 0) {
					return result;
				}
			}

			if (RE::ExtraDataList* exactClean = findSingleCandidate(true, false)) {
				result.resolved = true;
				result.selectedExtraList = exactClean;
				result.uniqueID = GetUniqueIDFromExtraList(exactClean);
				result.reason = "row_clean_exact";
				if (result.uniqueID != 0) {
					return result;
				}
			}

			if (RE::ExtraDataList* exactUnique = findSingleCandidate(false, false)) {
				result.resolved = true;
				result.selectedExtraList = exactUnique;
				result.uniqueID = GetUniqueIDFromExtraList(exactUnique);
				result.reason = "row_exact_unique";
				if (result.uniqueID != 0) {
					return result;
				}
			}
		}

		if (result.rowCount == 1) {
			if (countMatchedImplicitClean == 1 &&
			    countMatchedCleanExplicit == 0 &&
			    countMatchedInstanceSpecific == 0) {
				RowAwareMutableCandidate* implicitCleanExact = nullptr;
				for (auto* candidate : countMatchedCandidates) {
					if (candidate && candidate->implicitClean) {
						implicitCleanExact = candidate;
						break;
					}
				}
				pickResolvedCandidate(implicitCleanExact, "row_clean_implicit_exact");
				return result;
			}

			if (countMatchedImplicitClean == 0 && countMatchedCleanExplicit == 1 && countMatchedInstanceSpecific == 0) {
				if (RE::ExtraDataList* exactClean = findSingleCandidate(true, false)) {
					result.resolved = true;
					result.selectedExtraList = exactClean;
					result.uniqueID = GetUniqueIDFromExtraList(exactClean);
					result.reason = "row_clean_exact";
					if (result.uniqueID != 0) {
						return result;
					}
				}
			}

			if (countMatchedImplicitClean == 0 && countMatchedInstanceSpecific == 1 && countMatchedCleanExplicit == 0) {
				if (RE::ExtraDataList* exactModified = findSingleCandidate(false, true)) {
					result.resolved = true;
					result.selectedExtraList = exactModified;
					result.uniqueID = GetUniqueIDFromExtraList(exactModified);
					result.reason = "row_modified_exact";
					if (result.uniqueID != 0) {
						return result;
					}
				}
			}

		}

		result.resolved = false;
		result.uniqueID = 0;
		result.selectedExtraList = nullptr;
		result.reason = "row_unresolved";
		return result;
	}

	RE::InventoryEntryData* FindLiveInventoryEntryByForm(RE::FormID a_formID)
	{
		if (a_formID == 0) {
			return nullptr;
		}

		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return nullptr;
		}

		auto* invChanges = pc->GetInventoryChanges();
		if (!invChanges || !invChanges->entryList) {
			return nullptr;
		}

		RE::InventoryEntryData* found = nullptr;
		InvokeWithSehGuard([&]() {
			for (auto* entry : *invChanges->entryList) {
				if (!entry || !entry->object || entry->object->GetFormID() != a_formID || entry->countDelta <= 0) {
					continue;
				}
				found = entry;
				break;
			}
		});
		return found;
	}

	int CountRepresentedEntryItems(RE::InventoryEntryData* a_entry)
	{
		if (!a_entry || !a_entry->extraLists) {
			return 0;
		}

		int represented = 0;
		if (!InvokeWithSehGuard([&]() {
			    for (auto* extraList : *a_entry->extraLists) {
				    represented += GetExtraListCount(extraList);
			    }
		    })) {
			return 0;
		}
		return represented;
	}

	std::uint16_t MaterializeImplicitCleanSingletonRow(
		RE::FormID a_formID,
		int a_expectedCleanCount,
		RE::ExtraDataList*& a_outExtraList)
	{
		a_outExtraList = nullptr;
		if (a_formID == 0 || a_expectedCleanCount != 1) {
			return 0;
		}

		auto* liveEntry = FindLiveInventoryEntryByForm(a_formID);
		if (!liveEntry) {
			return 0;
		}

		int countDelta = 0;
		if (!InvokeWithSehGuard([&]() { countDelta = liveEntry->countDelta; }) || countDelta <= 0) {
			return 0;
		}

		const int implicitCleanCount = (std::max)(0, countDelta - CountRepresentedEntryItems(liveEntry));
		if (implicitCleanCount != a_expectedCleanCount) {
			return 0;
		}

		RE::ExtraDataList* extraList = nullptr;
		UniqueIDHandler::EnsureXListUniqueness(extraList);
		if (!extraList) {
			return 0;
		}

		const std::uint16_t uniqueID = GetUniqueIDFromExtraList(extraList);
		if (uniqueID == 0) {
			return 0;
		}

		if (!InvokeWithSehGuard([&]() { liveEntry->AddExtraList(extraList); })) {
			return 0;
		}

		a_outExtraList = extraList;
		return uniqueID;
	}

	bool HasDuplicateUniqueIDCollision(
		RE::PlayerCharacter* a_pc,
		RE::FormID a_formID,
		RE::ExtraDataList* a_selectedExtraList,
		std::uint16_t a_uniqueID)
	{
		if (!a_pc || !a_selectedExtraList || a_formID == 0 || a_uniqueID == 0) {
			return false;
		}

		auto inventoryMap = a_pc->GetInventory();
		for (auto& [boundObj, data] : inventoryMap) {
			if (!boundObj || boundObj->GetFormID() != a_formID || !data.second || !data.second->extraLists) {
				continue;
			}

			for (auto* extraList : *data.second->extraLists) {
				if (!extraList || extraList == a_selectedExtraList) {
					continue;
				}
				if (GetUniqueIDFromExtraList(extraList) != a_uniqueID) {
					continue;
				}
				if (!AreEquivalentExtraLists(a_selectedExtraList, extraList)) {
					return true;
				}
			}
		}

		return false;
	}

	std::uint16_t ResolveUniqueIDFromPlayerInventory(RE::FormID a_formID, bool a_allowEnsure, InventoryFormState* a_outState = nullptr)
	{
		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || a_formID == 0) {
			return 0;
		}

		InventoryFormState state = AnalyzeInventoryForm(pc, a_formID);
		if (state.firstUniqueID == 0 && a_allowEnsure && state.totalCount > 0) {
			UniqueIDHandler::EnsureXListUniquenessInPcInventory();
			state = AnalyzeInventoryForm(pc, a_formID);
		}

		if (a_outState) {
			*a_outState = state;
		}
		return state.firstUniqueID;
	}

	// Look up the uniqueID for a form from player inventory (for FavoritesMenu binding)
	std::uint16_t GetUniqueIDFromPlayerInventory(RE::FormID formID, InventoryFormState* a_outState = nullptr)
	{
		InventoryFormState state{};
		const std::uint16_t uniqueID = ResolveUniqueIDFromPlayerInventory(formID, true, &state);
		if (a_outState) {
			*a_outState = state;
		}
		if (uniqueID != 0) {
			LOG_INFO(FavoritesMenu_Cache,
				"GetUniqueIDFromPlayerInventory: formID={:08X} uniqueID={} count={} represented={} cleanCount={} plainCount={} instanceSpecific={} uniqueCandidates={}",
				formID,
				uniqueID,
				state.totalCount,
				state.representedCount,
				state.cleanCount,
				state.plainCount,
				state.instanceSpecificCount,
				state.uniqueCandidateCount);
		} else {
			LOG_INFO(FavoritesMenu_Cache,
				"GetUniqueIDFromPlayerInventory: formID={:08X} no uniqueID found count={} represented={} cleanCount={} plainCount={} instanceSpecific={} uniqueCandidates={}",
				formID,
				state.totalCount,
				state.representedCount,
				state.cleanCount,
				state.plainCount,
				state.instanceSpecificCount,
				state.uniqueCandidateCount);
		}
		return uniqueID;
	}

	struct BindingDecision
	{
		std::shared_ptr<WheelItem> item;
		const char* chosenType = "None";
		const char* reason = "unknown";
	};

	void LogBindingDecision(
		const char* source,
		RE::TESForm* form,
		const ItemCapabilities::Flags& caps,
		const char* chosenType,
		const char* reason)
	{
		if (!MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Input)) {
			return;
		}
		const RE::FormID formID = form ? form->GetFormID() : 0;
		const char* editorID = form ? form->GetFormEditorID() : "";
		const char* name = form ? form->GetName() : "(null)";
		const int formType = form ? static_cast<int>(form->GetFormType()) : -1;
		MainWheelDebug::Log(MainWheelDebug::Category::Input,
			"FactoryBind: src={} formID={:08X} editorID='{}' name='{}' type={} playable={} inv={} carry={} equip={} slot={} consume={} read={} script={} unsafe={} chosen={} reason={}",
			source ? source : "Unknown",
			formID,
			editorID ? editorID : "",
			name ? name : "",
			formType,
			caps.playable ? 1 : 0,
			caps.inInventory ? 1 : 0,
			caps.canBeCarried ? 1 : 0,
			caps.equippable ? 1 : 0,
			caps.hasEquipSlot ? 1 : 0,
			caps.consumable ? 1 : 0,
			caps.readable ? 1 : 0,
			caps.scriptBacked ? 1 : 0,
			caps.unsafePassive ? 1 : 0,
			chosenType ? chosenType : "None",
			reason ? reason : "unknown");
	}

	BindingDecision CreateWheelItemForForm(RE::TESForm* form, RE::TESBoundObject* boundObj, std::uint16_t uniqueID, const ItemCapabilities::Flags& caps)
	{
		BindingDecision res{};
		if (!form) {
			res.reason = "null_form";
			return res;
		}
		const bool isIngredient = form->GetFormType() == RE::FormType::Ingredient;
		if (isIngredient && !Config::WheelBehavior::AllowIngredientUse) {
			res.reason = "ingredient_disabled";
			return res;
		}
		if (caps.unsafePassive && !isIngredient) {
			res.reason = "unsafe_passive";
			return res;
		}

		switch (form->GetFormType()) {
		case RE::FormType::Weapon:
		{
			if (!boundObj || !caps.playable) {
				res.reason = boundObj ? "not_playable" : "not_bound";
				return res;
			}
			res.item = WheelItemMutable::CreateWheelItemMutable<WheelItemWeapon>(boundObj, uniqueID);
			res.chosenType = WheelItemWeapon::ITEM_TYPE_STR;
			res.reason = "ok";
			return res;
		}
		case RE::FormType::Armor:
		{
			if (!boundObj || !caps.playable) {
				res.reason = boundObj ? "not_playable" : "not_bound";
				return res;
			}
			res.item = WheelItemMutable::CreateWheelItemMutable<WheelItemArmor>(boundObj, uniqueID);
			res.chosenType = WheelItemArmor::ITEM_TYPE_STR;
			res.reason = "ok";
			return res;
		}
		case RE::FormType::Light:
		{
			auto* light = form->As<RE::TESObjectLIGH>();
			if (!light || !caps.canBeCarried || !caps.playable) {
				res.reason = !light ? "cast_failed" : (!caps.canBeCarried ? "not_carryable" : "not_playable");
				return res;
			}
			res.item = std::make_shared<WheelItemLight>(light);
			res.chosenType = WheelItemLight::ITEM_TYPE_STR;
			res.reason = "ok";
			return res;
		}
		case RE::FormType::Ammo:
		{
			auto* ammo = form->As<RE::TESAmmo>();
			if (!ammo || !caps.playable) {
				res.reason = !ammo ? "cast_failed" : "not_playable";
				return res;
			}
			res.item = std::make_shared<WheelItemAmmo>(ammo);
			res.chosenType = WheelItemAmmo::ITEM_TYPE_STR;
			res.reason = "ok";
			return res;
		}
		case RE::FormType::AlchemyItem:
		{
			auto* alchemy = form->As<RE::AlchemyItem>();
			if (!alchemy || !caps.playable || !caps.consumable) {
				if (!alchemy) {
					res.reason = "cast_failed";
				} else if (!caps.playable) {
					res.reason = "not_playable";
				} else {
					res.reason = "not_consumable";
				}
				return res;
			}
			res.item = std::make_shared<WheelItemAlchemy>(alchemy);
			res.chosenType = WheelItemAlchemy::ITEM_TYPE_STR;
			res.reason = "ok";
			return res;
		}
		case RE::FormType::Ingredient:
		{
			auto* ingredient = form->As<RE::IngredientItem>();
			// Vanilla ingredients can be inventory-consumable even when the generic
			// TESBoundObject playable flag is not a reliable admission signal.
			if (!ingredient || !caps.consumable) {
				res.reason = !ingredient ? "cast_failed" : "not_consumable";
				return res;
			}
			res.item = std::make_shared<WheelItemIngredient>(ingredient);
			res.chosenType = WheelItemIngredient::ITEM_TYPE_STR;
			res.reason = "ok";
			return res;
		}
		case RE::FormType::Scroll:
		{
			auto* scroll = form->As<RE::ScrollItem>();
			if (!scroll || !caps.playable) {
				res.reason = !scroll ? "cast_failed" : "not_playable";
				return res;
			}
			res.item = std::make_shared<WheelItemScroll>(scroll);
			res.chosenType = WheelItemScroll::ITEM_TYPE_STR;
			res.reason = "ok";
			return res;
		}
		case RE::FormType::Misc:
		{
			auto* misc = form->As<RE::TESObjectMISC>();
			if (!misc || !caps.playable) {
				res.reason = !misc ? "cast_failed" : "not_playable";
				return res;
			}
			if (!caps.scriptBacked && !Config::WheelBehavior::AllowUnsafeMiscActivation) {
				res.reason = "misc_no_script";
				return res;
			}
			res.item = std::make_shared<WheelItemMisc>(misc);
			res.chosenType = WheelItemMisc::ITEM_TYPE_STR;
			res.reason = "ok";
			return res;
		}
		case RE::FormType::Book:
		{
			auto* book = form->As<RE::TESObjectBOOK>();
			if (!book || !caps.playable || !caps.readable) {
				if (!book) {
					res.reason = "cast_failed";
				} else if (!caps.playable) {
					res.reason = "not_playable";
				} else {
					res.reason = "not_readable";
				}
				return res;
			}
			res.item = std::make_shared<WheelItemBook>(book);
			res.chosenType = WheelItemBook::ITEM_TYPE_STR;
			res.reason = "ok";
			return res;
		}
		case RE::FormType::Spell:
		{
			auto* spell = form->As<RE::SpellItem>();
			if (!spell) {
				res.reason = "cast_failed";
				return res;
			}
			res.item = std::make_shared<WheelItemSpell>(spell);
			res.chosenType = WheelItemSpell::ITEM_TYPE_STR;
			res.reason = "ok";
			return res;
		}
		case RE::FormType::Shout:
		{
			auto* shout = form->As<RE::TESShout>();
			if (!shout) {
				res.reason = "cast_failed";
				return res;
			}
			res.item = std::make_shared<WheelItemShout>(shout);
			res.chosenType = WheelItemShout::ITEM_TYPE_STR;
			res.reason = "ok";
			return res;
		}
		default:
			res.reason = "unsupported_form_type";
			return res;
		}
	}
}

std::shared_ptr<WheelItem> WheelItemFactory::MakeWheelItemFromMenuHovered()
{
	MainWheelDebug::Log(MainWheelDebug::Category::Input, "FactoryMenuHover: ENTER");

	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc || !pc->Is3DLoaded()) {
		MainWheelDebug::Log(MainWheelDebug::Category::Input, "FactoryMenuHover: FAIL reason=no_player_or_3d");
		return nullptr;
	}
	RE::UI* ui = RE::UI::GetSingleton();
	if (!ui) {
		MainWheelDebug::Log(MainWheelDebug::Category::Input, "FactoryMenuHover: FAIL reason=no_ui");
		return nullptr;
	}

	const bool invOpen = ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME);
	const bool magOpen = ui->IsMenuOpen(RE::MagicMenu::MENU_NAME);
	MainWheelDebug::Log(MainWheelDebug::Category::Input,
		"FactoryMenuHover: invMenu={} magMenu={}", invOpen ? 1 : 0, magOpen ? 1 : 0);

	try
	{
		if (invOpen) {
			auto* invMenu = static_cast<RE::InventoryMenu*>(ui->GetMenu(RE::InventoryMenu::MENU_NAME).get());
			if (!invMenu) {
				MainWheelDebug::Log(MainWheelDebug::Category::Input, "FactoryMenuHover: FAIL reason=invMenu_ptr_null");
				return nullptr;
			}
			RE::ItemList::Item* selectedItem = invMenu->itemList ? invMenu->itemList->GetSelectedItem() : nullptr;
			LogSelectedInventoryRow(selectedItem);
			RE::InventoryEntryData* invEntry = Utils::Inventory::GetSelectedItemIninventory(invMenu);
			if (!invEntry) {
				MainWheelDebug::Log(MainWheelDebug::Category::Input, "FactoryMenuHover: FAIL reason=invEntry_null");
				return nullptr;
			}
			RE::TESBoundObject* boundObj = invEntry->object;
			if (!boundObj) {
				MainWheelDebug::Log(MainWheelDebug::Category::Input, "FactoryMenuHover: FAIL reason=boundObj_null");
				return nullptr;
			}

			// Log the item we found
			const char* itemName = boundObj->GetName();
			RE::FormID formId = boundObj->GetFormID();
			RE::FormType formType = boundObj->GetFormType();
			bool hasExtraLists = (invEntry->extraLists != nullptr);
			MainWheelDebug::Log(MainWheelDebug::Category::Input,
				"FactoryMenuHover: item='{}' formId={:08X} formType={} hasExtraLists={}",
				itemName ? itemName : "null", formId, static_cast<int>(formType), hasExtraLists ? 1 : 0);

			// if is weapon or armor, get the item's unique ID. If there's no unique id, abort.
			uint16_t uniqueID = 0;
			switch (formType) {
			case RE::FormType::Weapon:
			case RE::FormType::Armor:
			{
				RowAwareMutableBindResult rowAwareBind{};
				uniqueID = TryGetUniqueIDFromEntry(invEntry);
				if (uniqueID == 0 && invEntry->extraLists) {
					for (auto& extraList : *invEntry->extraLists) {
						UniqueIDHandler::EnsureXListUniqueness(extraList);
					}
					uniqueID = TryGetUniqueIDFromEntry(invEntry);
				}

				InventoryFormState inventoryState{};
				if (selectedItem) {
					rowAwareBind = ResolveRowAwareMutableBinding(boundObj, invEntry, selectedItem);
				}

				if (rowAwareBind.resolved) {
					uniqueID = rowAwareBind.uniqueID;
					if (uniqueID == 0 &&
					    std::strcmp(rowAwareBind.reason, "row_clean_implicit_exact") == 0) {
						RE::ExtraDataList* materializedExtraList = nullptr;
						const std::uint16_t materializedUniqueID =
							MaterializeImplicitCleanSingletonRow(formId, rowAwareBind.rowCount, materializedExtraList);
						if (materializedUniqueID == 0 || !materializedExtraList) {
							if (ShouldFallbackImplicitCleanSingletonToFormLevel(rowAwareBind)) {
								rowAwareBind.reason = "row_clean_implicit_formlevel_fallback";
								logger::info(
									"FactoryMenuHover: row-aware mutable bind formId={:08X} uniqueID=0 reason={} rowCount={} rowMatched={} rowClean={} rowPlain={} rowInstanceSpecific={} rowUniqueCandidates={} source=materialize_failed",
									formId,
									rowAwareBind.reason,
									rowAwareBind.rowCount,
									rowAwareBind.matchedCount,
									rowAwareBind.cleanCount,
									rowAwareBind.plainCount,
									rowAwareBind.instanceSpecificCount,
									rowAwareBind.uniqueCandidateCount);
							} else {
								logger::info(
									"FactoryMenuHover: row-aware mutable bind formId={:08X} uniqueID=0 reason=row_clean_implicit_materialize_failed rowCount={} rowMatched={} rowClean={} rowPlain={} rowInstanceSpecific={} rowUniqueCandidates={}",
									formId,
									rowAwareBind.rowCount,
									rowAwareBind.matchedCount,
									rowAwareBind.cleanCount,
									rowAwareBind.plainCount,
									rowAwareBind.instanceSpecificCount,
									rowAwareBind.uniqueCandidateCount);
								return nullptr;
							}
						} else {
							rowAwareBind.uniqueID = materializedUniqueID;
							rowAwareBind.selectedExtraList = materializedExtraList;
							rowAwareBind.reason = "row_clean_implicit_materialized";
							uniqueID = materializedUniqueID;
							logger::info(
								"MutableIdentityRepair: materialized implicit clean row formId={:08X} uniqueID={} source=live_inventory_changes",
								formId,
								uniqueID);
						}
					}

					if (uniqueID != 0 &&
					    rowAwareBind.selectedExtraList &&
					    HasDuplicateUniqueIDCollision(pc, formId, rowAwareBind.selectedExtraList, uniqueID)) {
						const std::uint16_t repairedUniqueID = UniqueIDHandler::RetagXListUniqueID(rowAwareBind.selectedExtraList);
						if (repairedUniqueID == 0 || repairedUniqueID == uniqueID) {
							logger::info(
								"FactoryMenuHover: row-aware mutable bind formId={:08X} uniqueID={} reason=row_duplicate_unique_repair_failed rowCount={} rowMatched={} rowClean={} rowPlain={} rowInstanceSpecific={} rowUniqueCandidates={}",
								formId,
								uniqueID,
								rowAwareBind.rowCount,
								rowAwareBind.matchedCount,
								rowAwareBind.cleanCount,
								rowAwareBind.plainCount,
								rowAwareBind.instanceSpecificCount,
								rowAwareBind.uniqueCandidateCount);
							return nullptr;
						}
						logger::info(
							"MutableIdentityRepair: retagged duplicate uniqueID formId={:08X} oldUniqueID={} newUniqueID={}",
							formId,
							uniqueID,
							repairedUniqueID);
						uniqueID = repairedUniqueID;
					}

					inventoryState = AnalyzeInventoryForm(pc, formId);
					logger::info(
						"FactoryMenuHover: row-aware mutable bind formId={:08X} uniqueID={} reason={} rowCount={} rowMatched={} rowClean={} rowPlain={} rowInstanceSpecific={} rowUniqueCandidates={} invCount={} invClean={} invPlain={} invInstanceSpecific={} invUniqueCandidates={}",
						formId,
						uniqueID,
						rowAwareBind.reason,
						rowAwareBind.rowCount,
						rowAwareBind.matchedCount,
						rowAwareBind.cleanCount,
						rowAwareBind.plainCount,
						rowAwareBind.instanceSpecificCount,
						rowAwareBind.uniqueCandidateCount,
						inventoryState.totalCount,
						inventoryState.cleanCount,
						inventoryState.plainCount,
						inventoryState.instanceSpecificCount,
						inventoryState.uniqueCandidateCount);
				} else {
					inventoryState = AnalyzeInventoryForm(pc, formId);
					const bool bindImplicitCleanSentinel =
						ShouldBindImplicitCleanSingletonSentinel(boundObj, selectedItem, rowAwareBind, inventoryState);
					if (bindImplicitCleanSentinel) {
						logger::info(
							"FactoryMenuHover: inferred implicit clean singleton sentinel formId={:08X} selectedCount={} count={} represented={} cleanCount={} plainCount={} instanceSpecific={} uniqueCandidates={}",
							formId,
							(std::max)(static_cast<int>(selectedItem->data.GetCount()), 1),
							inventoryState.totalCount,
							inventoryState.representedCount,
							inventoryState.cleanCount,
							inventoryState.plainCount,
							inventoryState.instanceSpecificCount,
							inventoryState.uniqueCandidateCount);
						uniqueID = 0;
					}

					// If still no UniqueID (null extraLists or generation failed on existing lists),
					// force UniqueID generation via full inventory scan, then resolve from live player inventory.
					if (uniqueID == 0 && !bindImplicitCleanSentinel) {
						MainWheelDebug::Log(MainWheelDebug::Category::Input,
							"FactoryMenuHover: no uniqueID, calling EnsureXListUniquenessInPcInventory");
						UniqueIDHandler::EnsureXListUniquenessInPcInventory();

						invEntry = Utils::Inventory::GetSelectedItemIninventory(invMenu);
						uniqueID = TryGetUniqueIDFromEntry(invEntry);
						if (uniqueID == 0) {
							uniqueID = ResolveUniqueIDFromPlayerInventory(formId, false, &inventoryState);
							MainWheelDebug::Log(MainWheelDebug::Category::Input,
								"FactoryMenuHover: inventory resolve formId={:08X} count={} represented={} cleanCount={} uniqueCandidates={} firstUniqueID={}",
								formId,
								inventoryState.totalCount,
								inventoryState.representedCount,
								inventoryState.cleanCount,
								inventoryState.uniqueCandidateCount,
								inventoryState.firstUniqueID);
						}
					}
					if (inventoryState.totalCount == 0) {
						inventoryState = AnalyzeInventoryForm(pc, formId);
					}
					if (uniqueID != 0 && ShouldPreferPlainStackSentinel(formType, inventoryState)) {
						logger::info(
							"FactoryMenuHover: using plain-stack sentinel bind formId={:08X} uniqueID={} count={} represented={} cleanCount={} plainCount={} uniqueCandidates={}",
							formId,
							uniqueID,
							inventoryState.totalCount,
							inventoryState.representedCount,
							inventoryState.cleanCount,
							inventoryState.plainCount,
							inventoryState.uniqueCandidateCount);
						uniqueID = 0;
					}
					MainWheelDebug::Log(MainWheelDebug::Category::Input,
						"FactoryMenuHover: weapon/armor uniqueID={}", uniqueID);
					if (uniqueID == 0) {
						if (bindImplicitCleanSentinel ||
							ShouldAllowInstanceSentinelFallback(formType, inventoryState) ||
							ShouldPreferPlainStackSentinel(formType, inventoryState)) {
							MainWheelDebug::Log(MainWheelDebug::Category::Input,
								"FactoryMenuHover: allowing clean/plain-stack sentinel bind formId={:08X} count={} represented={} cleanCount={} plainCount={} instanceSpecific={} uniqueCandidates={} implicitSelected={}",
								formId,
								inventoryState.totalCount,
								inventoryState.representedCount,
								inventoryState.cleanCount,
								inventoryState.plainCount,
								inventoryState.instanceSpecificCount,
								inventoryState.uniqueCandidateCount,
								bindImplicitCleanSentinel ? 1 : 0);
						} else {
							MainWheelDebug::Log(MainWheelDebug::Category::Input,
								"FactoryMenuHover: rejecting instance-backed bind formId={:08X} reason=no_uniqueID_after_inventory_resolve count={} represented={} cleanCount={} plainCount={} instanceSpecific={} uniqueCandidates={}",
								formId,
								inventoryState.totalCount,
								inventoryState.representedCount,
								inventoryState.cleanCount,
								inventoryState.plainCount,
								inventoryState.instanceSpecificCount,
								inventoryState.uniqueCandidateCount);
							return nullptr;
						}
					}
				}
			}
			break;
			default:
			break;
			}

			const ItemCapabilities::Flags caps = ItemCapabilities::Evaluate(boundObj, pc);
			BindingDecision decision = CreateWheelItemForForm(boundObj, boundObj, uniqueID, caps);
			LogBindingDecision("InventoryMenu", boundObj, caps, decision.chosenType, decision.reason);
			return WithMissingCategory(decision.item, boundObj);
		} else if (ui->IsMenuOpen(RE::MagicMenu::MENU_NAME)) {
			auto magMenu = ui->GetMenu<RE::MagicMenu>();
			if (!magMenu) {
				return nullptr;
			}
			RE::TESForm* form = Utils::Inventory::GetSelectedFormInMagicMenu(magMenu.get());
			if (!form) {
				return nullptr;
			}
			const ItemCapabilities::Flags caps = ItemCapabilities::Evaluate(form, pc);
			BindingDecision decision = CreateWheelItemForForm(form, form->As<RE::TESBoundObject>(), 0, caps);
			LogBindingDecision("MagicMenu", form, caps, decision.chosenType, decision.reason);
			return WithMissingCategory(decision.item, form);
		} else if (ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME)) {
			// FavoritesMenu support: extract selected favorite and create WheelItem
			LOG_INFO(FavoritesMenu_Cache, "WheelItemFactory: FavoritesMenu branch ENTERED");
			auto* favMenu = static_cast<RE::FavoritesMenu*>(ui->GetMenu(RE::FavoritesMenu::MENU_NAME).get());
			if (!favMenu) {
				LOG_INFO(FavoritesMenu_Cache, "WheelItemFactory: FAIL favMenu_ptr_null");
				MainWheelDebug::Log(MainWheelDebug::Category::Input, "FactoryMenuHover: FAIL reason=favMenu_ptr_null");
				return nullptr;
			}
			LOG_INFO(FavoritesMenu_Cache, "WheelItemFactory: favMenu obtained, calling GetSelectedFormInFavoritesMenu");
			RE::TESForm* form = Utils::Inventory::GetSelectedFormInFavoritesMenu(favMenu);
			if (!form) {
				LOG_INFO(FavoritesMenu_Cache, "WheelItemFactory: FAIL GetSelectedFormInFavoritesMenu returned null");
				MainWheelDebug::Log(MainWheelDebug::Category::Input, "FactoryMenuHover: FAIL reason=favMenu_no_selection");
				return nullptr;
			}
			LOG_INFO(FavoritesMenu_Cache, "WheelItemFactory: form obtained formID={:08X} formType={}",
				form->GetFormID(), static_cast<int>(form->GetFormType()));

			RE::TESBoundObject* boundObj = form->As<RE::TESBoundObject>();
			RE::FormType formType = form->GetFormType();
			MainWheelDebug::Log(MainWheelDebug::Category::Input,
				"FactoryMenuHover: FavoritesMenu formID={:08X} formType={}",
				form->GetFormID(), static_cast<int>(formType));

			std::uint16_t uniqueID = 0;
			if (formType == RE::FormType::Weapon || formType == RE::FormType::Armor) {
				InventoryFormState inventoryState{};
				uniqueID = GetUniqueIDFromPlayerInventory(form->GetFormID(), &inventoryState);
				if (uniqueID != 0 && ShouldPreferPlainStackSentinel(formType, inventoryState)) {
					logger::info(
						"FactoryMenuHover: using FavoritesMenu plain-stack sentinel bind formID={:08X} uniqueID={} count={} represented={} cleanCount={} plainCount={} uniqueCandidates={}",
						form->GetFormID(),
						uniqueID,
						inventoryState.totalCount,
						inventoryState.representedCount,
						inventoryState.cleanCount,
						inventoryState.plainCount,
						inventoryState.uniqueCandidateCount);
					uniqueID = 0;
				}
				if (uniqueID == 0) {
					if (ShouldAllowInstanceSentinelFallback(formType, inventoryState) ||
						ShouldPreferPlainStackSentinel(formType, inventoryState)) {
						MainWheelDebug::Log(MainWheelDebug::Category::Input,
							"FactoryMenuHover: allowing FavoritesMenu clean/plain-stack sentinel bind formID={:08X} count={} represented={} cleanCount={} plainCount={} instanceSpecific={} uniqueCandidates={}",
							form->GetFormID(),
							inventoryState.totalCount,
							inventoryState.representedCount,
							inventoryState.cleanCount,
							inventoryState.plainCount,
							inventoryState.instanceSpecificCount,
							inventoryState.uniqueCandidateCount);
					} else {
						MainWheelDebug::Log(MainWheelDebug::Category::Input,
							"FactoryMenuHover: rejecting FavoritesMenu instance-backed bind formID={:08X} reason=no_uniqueID count={} represented={} cleanCount={} plainCount={} instanceSpecific={} uniqueCandidates={}",
							form->GetFormID(),
							inventoryState.totalCount,
							inventoryState.representedCount,
							inventoryState.cleanCount,
							inventoryState.plainCount,
							inventoryState.instanceSpecificCount,
							inventoryState.uniqueCandidateCount);
						return nullptr;
					}
				}
			}
			const ItemCapabilities::Flags caps = ItemCapabilities::Evaluate(form, pc);
			BindingDecision decision = CreateWheelItemForForm(form, boundObj, uniqueID, caps);
			LogBindingDecision("FavoritesMenu", form, caps, decision.chosenType, decision.reason);
			return WithMissingCategory(decision.item, form);
		}
	}
	catch (std::exception exception) {
		INFO("Exception: {}", exception.what());
	}

	return nullptr;
}

std::shared_ptr<WheelItem> WheelItemFactory::MakeWheelItemFromJsonObject(nlohmann::json a_json, SKSE::SerializationInterface* a_intfc)
{
	if (!a_json.contains("type")) {
		LOG_WARN(Serialization, "Deserialize: item missing 'type' field, skipping");
		return nullptr;
	}
	if (!a_json.contains("formID")) {
		LOG_WARN(Serialization, "Deserialize: item missing 'formID' field, skipping");
		return nullptr;
	}
	std::string type = a_json["type"];
	RE::FormID savedFormID = a_json["formID"].get<RE::FormID>();
	const std::uint16_t uniqueID = ReadUniqueID(a_json);
	const bool formLevelStack = ReadFormLevelStack(a_json);
	const std::string missingName = ReadMissingName(a_json);
	RE::FormID formID = savedFormID;
	if (!a_intfc->ResolveFormID(savedFormID, formID)) {
		LOG_INFO(Serialization, "Deserialize: failed to resolve formID {:08X} for type '{}', skipping", savedFormID, type);
		MissingCategory category = ResolveMissingCategory(a_json, type, nullptr);
		if (IsKeepMissingCategoryEnabled(category)) {
			auto missingItem = std::make_shared<WheelItemMissing>(type, savedFormID, uniqueID, category, missingName);
			missingItem->SetMissingCategory(category);
			return missingItem;
		}
		return nullptr;
	}

	// Lookup the form and validate it exists
	RE::TESForm* baseForm = RE::TESForm::LookupByID(formID);
	if (!baseForm) {
		LOG_INFO(Serialization, "Deserialize: form {:08X} (saved {:08X}) not found for type '{}', skipping", formID, savedFormID, type);
		MissingCategory category = ResolveMissingCategory(a_json, type, nullptr);
		if (IsKeepMissingCategoryEnabled(category)) {
			auto missingItem = std::make_shared<WheelItemMissing>(type, formID, uniqueID, category, missingName);
			missingItem->SetMissingCategory(category);
			return missingItem;
		}
		return nullptr;
	}
	MissingCategory category = ResolveMissingCategory(a_json, type, baseForm);

	// Use type-safe lookups to prevent type confusion crashes
	try
	{
		if (type == WheelItemWeapon::ITEM_TYPE_STR) {
			RE::TESObjectWEAP* weap = RE::TESForm::LookupByID<RE::TESObjectWEAP>(formID);
			if (!weap) {
				LOG_INFO(Serialization, "Deserialize: form {:08X} is not TESObjectWEAP, skipping", formID);
				return nullptr;
			}
			if (uniqueID == 0) {
				if (!formLevelStack) {
					LOG_WARN(Serialization, "Deserialize: WheelItemWeapon missing uniqueID, skipping");
					return nullptr;
				}
				LOG_INFO(Serialization, "Deserialize: WheelItemWeapon using form-level stack form {:08X}", formID);
			}
			std::shared_ptr<WheelItemWeapon> wheelItemweap = WheelItemMutable::CreateWheelItemMutable<WheelItemWeapon>(weap, uniqueID);
			wheelItemweap->SetMissingCategory(category);
			return wheelItemweap;
		} else if (type == WheelItemArmor::ITEM_TYPE_STR) {
			RE::TESObjectARMO* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(formID);
			if (!armor) {
				LOG_INFO(Serialization, "Deserialize: form {:08X} is not TESObjectARMO, skipping", formID);
				return nullptr;
			}
			if (uniqueID == 0) {
				if (!formLevelStack) {
					LOG_WARN(Serialization, "Deserialize: WheelItemArmor missing uniqueID, skipping");
					return nullptr;
				}
				LOG_INFO(Serialization, "Deserialize: WheelItemArmor using form-level stack form {:08X}", formID);
			}
			std::shared_ptr<WheelItemArmor> wheelItemArmo = WheelItemMutable::CreateWheelItemMutable<WheelItemArmor>(armor, uniqueID);
			wheelItemArmo->SetMissingCategory(category);
			return wheelItemArmo;
		} else if (type == WheelItemSpell::ITEM_TYPE_STR) {
			RE::SpellItem* spell = RE::TESForm::LookupByID<RE::SpellItem>(formID);
			if (!spell) {
				LOG_INFO(Serialization, "Deserialize: form {:08X} is not SpellItem, skipping", formID);
				return nullptr;
			}
			std::shared_ptr<WheelItemSpell> wheelItemSpell = std::make_shared<WheelItemSpell>(spell);
			wheelItemSpell->SetMissingCategory(category);
			return wheelItemSpell;
		} else if (type == WheelItemShout::ITEM_TYPE_STR) {
			RE::TESShout* shout = RE::TESForm::LookupByID<RE::TESShout>(formID);
			if (!shout) {
				LOG_INFO(Serialization, "Deserialize: form {:08X} is not TESShout, skipping", formID);
				return nullptr;
			}
			std::shared_ptr<WheelItemShout> wheelItemShout = std::make_shared<WheelItemShout>(shout);
			wheelItemShout->SetMissingCategory(category);
			return wheelItemShout;
		} else if (type == WheelItemLight::ITEM_TYPE_STR) {
			RE::TESObjectLIGH* light = RE::TESForm::LookupByID<RE::TESObjectLIGH>(formID);
			if (!light) {
				LOG_INFO(Serialization, "Deserialize: form {:08X} is not TESObjectLIGH, skipping", formID);
				return nullptr;
			}
			std::shared_ptr<WheelItemLight> wheelItemLight = std::make_shared<WheelItemLight>(light);
			wheelItemLight->SetMissingCategory(category);
			return wheelItemLight;
		} else if (type == WheelItemAmmo::ITEM_TYPE_STR) {
			RE::TESAmmo* ammo = RE::TESForm::LookupByID<RE::TESAmmo>(formID);
			if (!ammo) {
				LOG_INFO(Serialization, "Deserialize: form {:08X} is not TESAmmo, skipping", formID);
				return nullptr;
			}
			std::shared_ptr<WheelItemAmmo> wheelItemAmmo = std::make_shared<WheelItemAmmo>(ammo);
			wheelItemAmmo->SetMissingCategory(category);
			return wheelItemAmmo;
		} else if (type == WheelItemAlchemy::ITEM_TYPE_STR) {
			RE::AlchemyItem* alchemyItem = RE::TESForm::LookupByID<RE::AlchemyItem>(formID);
			if (!alchemyItem) {
				LOG_INFO(Serialization, "Deserialize: form {:08X} is not AlchemyItem, skipping", formID);
				return nullptr;
			}
			std::shared_ptr<WheelItemAlchemy> wheelItemAlchemy = std::make_shared<WheelItemAlchemy>(alchemyItem);
			wheelItemAlchemy->SetMissingCategory(category);
			return wheelItemAlchemy;
		} else if (type == WheelItemIngredient::ITEM_TYPE_STR) {
			RE::IngredientItem* ingredient = RE::TESForm::LookupByID<RE::IngredientItem>(formID);
			if (!ingredient) {
				LOG_INFO(Serialization, "Deserialize: form {:08X} is not IngredientItem, skipping", formID);
				return nullptr;
			}
			std::shared_ptr<WheelItemIngredient> wheelItemIngredient = std::make_shared<WheelItemIngredient>(ingredient);
			wheelItemIngredient->SetMissingCategory(category);
			return wheelItemIngredient;
		} else if (type == WheelItemScroll::ITEM_TYPE_STR) {
			RE::ScrollItem* scrollItem = RE::TESForm::LookupByID<RE::ScrollItem>(formID);
			if (!scrollItem) {
				LOG_INFO(Serialization, "Deserialize: form {:08X} is not ScrollItem, skipping", formID);
				return nullptr;
			}
			std::shared_ptr<WheelItemScroll> wheelItemScroll = std::make_shared<WheelItemScroll>(scrollItem);
			wheelItemScroll->SetMissingCategory(category);
			return wheelItemScroll;
		} else if (type == WheelItemMisc::ITEM_TYPE_STR) {
			RE::TESObjectMISC* miscObj = RE::TESForm::LookupByID<RE::TESObjectMISC>(formID);
			if (!miscObj) {
				LOG_INFO(Serialization, "Deserialize: form {:08X} is not TESObjectMISC, skipping", formID);
				return nullptr;
			}
			std::shared_ptr<WheelItemMisc> wheelItemMisc = std::make_shared<WheelItemMisc>(miscObj);
			wheelItemMisc->SetMissingCategory(category);
			return wheelItemMisc;
		} else if (type == WheelItemBook::ITEM_TYPE_STR) {
			RE::TESObjectBOOK* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(formID);
			if (!book) {
				LOG_INFO(Serialization, "Deserialize: form {:08X} is not TESObjectBOOK, skipping", formID);
				return nullptr;
			}
			std::shared_ptr<WheelItemBook> wheelItemBook = std::make_shared<WheelItemBook>(book);
			wheelItemBook->SetMissingCategory(category);
			return wheelItemBook;
		} else {
			// Unknown type - do not instantiate, skip safely
			LOG_WARN(Serialization, "Deserialize: unknown item type '{}' for form {:08X}, skipping", type, formID);
			return nullptr;
		}
	}
	catch (const std::exception& e) {
		LOG_WARN(Serialization, "Deserialize: exception creating item type '{}' form {:08X}: {}", type, formID, e.what());
		return nullptr;
	}
	
	return nullptr;
}

std::shared_ptr<WheelItem> WheelItemFactory::MakeWheelItemFromResolvedForm(std::string_view a_type, RE::FormID a_formID, std::uint16_t a_uniqueID)
{
	if (a_formID == 0 || a_type.empty()) {
		return nullptr;
	}

	RE::TESForm* baseForm = RE::TESForm::LookupByID(a_formID);
	if (!baseForm) {
		return nullptr;
	}

	try
	{
	if (a_type == WheelItemWeapon::ITEM_TYPE_STR) {
		RE::TESObjectWEAP* weap = baseForm->As<RE::TESObjectWEAP>();
		if (!weap) {
			return nullptr;
		}
		return WheelItemMutable::CreateWheelItemMutable<WheelItemWeapon>(weap, a_uniqueID);
	}
	if (a_type == WheelItemArmor::ITEM_TYPE_STR) {
		RE::TESObjectARMO* armor = baseForm->As<RE::TESObjectARMO>();
		if (!armor) {
			return nullptr;
		}
		return WheelItemMutable::CreateWheelItemMutable<WheelItemArmor>(armor, a_uniqueID);
	}
		if (a_type == WheelItemSpell::ITEM_TYPE_STR) {
			RE::SpellItem* spell = baseForm->As<RE::SpellItem>();
			return spell ? std::make_shared<WheelItemSpell>(spell) : nullptr;
		}
		if (a_type == WheelItemShout::ITEM_TYPE_STR) {
			RE::TESShout* shout = baseForm->As<RE::TESShout>();
			return shout ? std::make_shared<WheelItemShout>(shout) : nullptr;
		}
		if (a_type == WheelItemLight::ITEM_TYPE_STR) {
			RE::TESObjectLIGH* light = baseForm->As<RE::TESObjectLIGH>();
			return light ? std::make_shared<WheelItemLight>(light) : nullptr;
		}
		if (a_type == WheelItemAmmo::ITEM_TYPE_STR) {
			RE::TESAmmo* ammo = baseForm->As<RE::TESAmmo>();
			return ammo ? std::make_shared<WheelItemAmmo>(ammo) : nullptr;
		}
		if (a_type == WheelItemAlchemy::ITEM_TYPE_STR) {
			RE::AlchemyItem* alchemyItem = baseForm->As<RE::AlchemyItem>();
			return alchemyItem ? std::make_shared<WheelItemAlchemy>(alchemyItem) : nullptr;
		}
		if (a_type == WheelItemIngredient::ITEM_TYPE_STR) {
			RE::IngredientItem* ingredient = baseForm->As<RE::IngredientItem>();
			return ingredient ? std::make_shared<WheelItemIngredient>(ingredient) : nullptr;
		}
		if (a_type == WheelItemScroll::ITEM_TYPE_STR) {
			RE::ScrollItem* scrollItem = baseForm->As<RE::ScrollItem>();
			return scrollItem ? std::make_shared<WheelItemScroll>(scrollItem) : nullptr;
		}
		if (a_type == WheelItemMisc::ITEM_TYPE_STR) {
			RE::TESObjectMISC* miscObj = baseForm->As<RE::TESObjectMISC>();
			return miscObj ? std::make_shared<WheelItemMisc>(miscObj) : nullptr;
		}
		if (a_type == WheelItemBook::ITEM_TYPE_STR) {
			RE::TESObjectBOOK* book = baseForm->As<RE::TESObjectBOOK>();
			return book ? std::make_shared<WheelItemBook>(book) : nullptr;
		}
	}
	catch (const std::exception&) {
		return nullptr;
	}

	return nullptr;
}

std::shared_ptr<WheelItem> WheelItemFactory::MakeExternalHotkeyItem(
	std::string a_displayName,
	std::uint32_t a_scanCode,
	std::uint32_t a_modifier,
	std::string a_iconPath,
	std::uint32_t a_iconTintARGB,
	std::uint32_t a_sourceSlotIndex,
	std::string a_sourceTag,
	bool a_hasResolvedIcon)
{
	return std::make_shared<WheelItemExternalHotkey>(
		std::move(a_displayName),
		a_scanCode,
		a_modifier,
		std::move(a_iconPath),
		a_iconTintARGB,
		a_sourceSlotIndex,
		std::move(a_sourceTag),
		a_hasResolvedIcon);
}

std::shared_ptr<WheelItem> WheelItemFactory::MakeOStimActionItem(OStimActionPayload a_payload)
{
	return std::make_shared<WheelItemOStimAction>(std::move(a_payload));
}

// ============================================================================
// External API
// ============================================================================

std::shared_ptr<WheelItem> WheelItemFactory::MakeWheelItemFromFormID(RE::FormID a_formID, std::uint16_t a_uniqueID)
{
	if (a_formID == 0) {
		return nullptr;
	}

	RE::TESForm* form = RE::TESForm::LookupByID(a_formID);
	if (!form) {
		return nullptr;
	}
	try {
		RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
		const ItemCapabilities::Flags caps = ItemCapabilities::Evaluate(form, pc);
		BindingDecision decision = CreateWheelItemForForm(form, form->As<RE::TESBoundObject>(), a_uniqueID, caps);
		LogBindingDecision("API", form, caps, decision.chosenType, decision.reason);
		return WithMissingCategory(decision.item, form);
	} catch (const std::exception&) {
		return nullptr;
	}
}
