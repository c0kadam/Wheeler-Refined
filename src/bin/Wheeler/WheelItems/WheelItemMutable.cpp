#include "WheelItemMutable.h"
#include "WheelItemMutableManager.h"
#include "bin/Wheeler/MainWheelDebug.h"
#include "bin/Wheeler/Wheeler.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace
{
	std::string ToLowerTrim(std::string value)
	{
		auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
		value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
		value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		return value;
	}

	std::string NormalizeLoose(const std::string& value)
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

	bool IsPlaceholderDisplayName(const char* text)
	{
		if (!text || text[0] == '\0') {
			return true;
		}
		std::string normalized = ToLowerTrim(text);
		if (normalized.size() >= 2 && normalized.front() == '<' && normalized.back() == '>') {
			normalized = ToLowerTrim(normalized.substr(1, normalized.size() - 2));
		}
		const std::string compact = NormalizeLoose(normalized);
		return normalized.empty() ||
			normalized == "missing name" ||
			normalized == "missing item" ||
			normalized == "<missing name>" ||
			normalized == "<missing item>" ||
			normalized.find("missing name") != std::string::npos ||
			normalized.find("missing item") != std::string::npos ||
			compact.find("missingname") != std::string::npos ||
			compact.find("missingitem") != std::string::npos ||
			compact == "null" ||
			normalized == "(null)" ||
			normalized == "null";
	}

	std::string ResolveDisplayNameFallback(RE::TESBoundObject* obj)
	{
		if (!obj) {
			return "(deleted)";
		}

		const char* name = obj->GetName();
		if (!IsPlaceholderDisplayName(name)) {
			return name;
		}

		if (auto* weapon = obj->As<RE::TESObjectWEAP>(); weapon &&
			weapon->GetWeaponType() == RE::WEAPON_TYPE::kHandToHandMelee) {
			return "Unarmed";
		}

		const char* editorID = obj->GetFormEditorID();
		if (editorID && editorID[0] != '\0') {
			return editorID;
		}

		char buf[32];
		std::snprintf(buf, sizeof(buf), "Form %08X", obj->GetFormID());
		return buf;
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

	const char* GetDisplayNameSafe(RE::InventoryEntryData* a_entry)
	{
		const char* displayName = nullptr;
		if (!a_entry) {
			return nullptr;
		}
		if (!InvokeWithSehGuard([&]() { displayName = a_entry->GetDisplayName(); })) {
			return nullptr;
		}
		return displayName;
	}

	std::string GetDisplayNameForExtraListSafe(RE::TESBoundObject* a_obj, RE::ExtraDataList* a_extraList)
	{
		if (!a_obj || !a_extraList) {
			return {};
		}

		RE::InventoryEntryData isolatedEntry(a_obj, 1);
		if (!InvokeWithSehGuard([&]() { isolatedEntry.AddExtraList(a_extraList); })) {
			return {};
		}

		const char* displayName = GetDisplayNameSafe(&isolatedEntry);
		if (displayName && displayName[0] != '\0') {
			return displayName;
		}

		return {};
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

	bool IsCleanExtraListSafe(RE::ExtraDataList* a_list)
	{
		return a_list &&
		       !HasTypeSafe(a_list, RE::ExtraDataType::kEnchantment) &&
		       !HasTypeSafe(a_list, RE::ExtraDataType::kPoison) &&
		       !HasTypeSafe(a_list, RE::ExtraDataType::kHealth);
	}

	bool HasInstanceSpecificMutableDataSafe(RE::ExtraDataList* a_list)
	{
		return a_list &&
		       (HasTypeSafe(a_list, RE::ExtraDataType::kEnchantment) ||
		        HasTypeSafe(a_list, RE::ExtraDataType::kPoison) ||
		        HasTypeSafe(a_list, RE::ExtraDataType::kHealth) ||
		        HasTypeSafe(a_list, RE::ExtraDataType::kCharge) ||
		        GetByTypeSafe<RE::ExtraTextDisplayData>(a_list) != nullptr);
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

		bool hasHealth = false;
		if (!TryHasTypeSafe(a_list, RE::ExtraDataType::kHealth, hasHealth)) {
			return false;
		}
		a_outSignature.hasHealth = hasHealth;
		if (hasHealth) {
			auto* healthData = GetByTypeSafe<RE::ExtraHealth>(a_list);
			if (!healthData) {
				return false;
			}
			a_outSignature.health = healthData->health;
		}

		bool hasEnchantment = false;
		if (!TryHasTypeSafe(a_list, RE::ExtraDataType::kEnchantment, hasEnchantment)) {
			return false;
		}
		a_outSignature.hasEnchantment = hasEnchantment;
		if (hasEnchantment) {
			auto* enchantmentData = GetByTypeSafe<RE::ExtraEnchantment>(a_list);
			if (!enchantmentData) {
				return false;
			}
			a_outSignature.enchantment = enchantmentData->enchantment;
			a_outSignature.charge = enchantmentData->charge;
			a_outSignature.removeOnUnequip = enchantmentData->removeOnUnequip;
		}

		bool hasPoison = false;
		if (!TryHasTypeSafe(a_list, RE::ExtraDataType::kPoison, hasPoison)) {
			return false;
		}
		a_outSignature.hasPoison = hasPoison;
		if (hasPoison) {
			auto* poisonData = GetByTypeSafe<RE::ExtraPoison>(a_list);
			if (!poisonData) {
				return false;
			}
			a_outSignature.poison = poisonData->poison;
			a_outSignature.poisonCount = poisonData->count;
		}

		a_outSignature.clean = !a_outSignature.hasHealth &&
		                       !a_outSignature.hasEnchantment &&
		                       !a_outSignature.hasPoison;
		return true;
	}

	bool AreEquivalentExtraListsSafe(RE::ExtraDataList* a_lhs, RE::ExtraDataList* a_rhs)
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

	struct MatchingInventoryEntries
	{
		int rawItemCount = 0;
		std::vector<RE::InventoryEntryData*> entries;
	};

	struct CleanUniquePromotionCandidate
	{
		RE::ExtraDataList* extraList = nullptr;
		std::uint16_t uniqueID = 0;
		int cleanUniqueCount = 0;
	};

	MatchingInventoryEntries CollectMatchingInventoryEntries(RE::TESObjectREFR::InventoryItemMap& a_inv, RE::TESBoundObject* a_obj)
	{
		MatchingInventoryEntries result{};
		if (!a_obj) {
			return result;
		}

		const RE::FormID formID = a_obj->GetFormID();
		for (auto& [boundObj, data] : a_inv) {
			if (!boundObj || boundObj->GetFormID() != formID || !data.second) {
				continue;
			}
			result.rawItemCount += data.first;
			if (std::find(result.entries.begin(), result.entries.end(), data.second.get()) == result.entries.end()) {
				result.entries.push_back(data.second.get());
			}
		}
		if (!result.entries.empty()) {
			return result;
		}

		if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
			if (auto* invChanges = pc->GetInventoryChanges(); invChanges && invChanges->entryList) {
				for (auto* entry : *invChanges->entryList) {
					if (!entry || !entry->object || entry->object->GetFormID() != formID) {
						continue;
					}

					int countDelta = 0;
					if (!InvokeWithSehGuard([&]() { countDelta = entry->countDelta; })) {
						continue;
					}
					if (countDelta <= 0) {
						continue;
					}

					result.rawItemCount += countDelta;
					if (std::find(result.entries.begin(), result.entries.end(), entry) == result.entries.end()) {
						result.entries.push_back(entry);
					}
				}
			}
		}
		return result;
	}

	CleanUniquePromotionCandidate ResolveSingleCleanUniquePromotionCandidate(
		const std::vector<RE::ExtraDataList*>& a_extraLists,
		int a_totalCleanItemCount)
	{
		CleanUniquePromotionCandidate result{};
		if (a_totalCleanItemCount != 1) {
			return result;
		}

		for (auto* extraList : a_extraLists) {
			if (!extraList || !IsCleanExtraListSafe(extraList)) {
				continue;
			}

			auto* uniqueIDData = GetByTypeSafe<RE::ExtraUniqueID>(extraList);
			if (!uniqueIDData || uniqueIDData->uniqueID == 0) {
				continue;
			}

			if (result.extraList && result.extraList != extraList) {
				return {};
			}

			result.extraList = extraList;
			result.uniqueID = uniqueIDData->uniqueID;
			result.cleanUniqueCount++;
		}

		if (result.cleanUniqueCount != 1 || !result.extraList || result.uniqueID == 0) {
			return {};
		}

		return result;
	}

	bool ShouldAllowCleanSentinelPromotion(
		RE::TESBoundObject* a_obj,
		const std::vector<RE::ExtraDataList*>& /*a_extraLists*/)
	{
		if (!a_obj) {
			return false;
		}

		const auto formType = a_obj->GetFormType();
		if (formType != RE::FormType::Weapon && formType != RE::FormType::Armor) {
			return true;
		}

		if (Wheeler::GetMutableInventoryCompatProfile() ==
		    Wheeler::MutableInventoryCompatProfile::EquipmentDurabilitySystem) {
			return true;
		}

		// Vanilla-like lists should keep plain clean duplicate stacks base-form keyed.
		// Exact clean uniqueIDs are reserved for instance-specific rows.
		return false;
	}
}

WheelItemMutable::~WheelItemMutable()
{
	WheelItemMutableManager::GetSingleton()->UnTrack(this);
}
uint16_t WheelItemMutable::GetUniqueID()
{
	std::lock_guard<std::mutex> lock(_uniqueIDLock);
	return this->_uniqueID;
}

uint16_t WheelItemMutable::GetUniqueID() const
{
	std::lock_guard<std::mutex> lock(_uniqueIDLock);
	return _uniqueID;
}

void WheelItemMutable::SetUniqueID(uint16_t a_id)
{
	std::lock_guard<std::mutex> lock(this->_uniqueIDLock);
	this->_uniqueID = a_id;
}

RE::FormID WheelItemMutable::GetFormID()
{
	if (!this->_obj) {
		return 0;
	}
	return this->_obj->GetFormID();
}

// Const override for inventory sync
RE::FormID WheelItemMutable::GetFormID() const
{
	if (!this->_obj) {
		return 0;
	}
	return this->_obj->GetFormID();
}

bool WheelItemMutable::IsInPlayerInventory() const
{
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc || !_obj) {
		return false;
	}
	
	// Use inventory snapshot to check via GetItemExtraDataAndCount logic
	auto inv = pc->GetInventory();
	auto [count, xList] = const_cast<WheelItemMutable*>(this)->GetItemExtraDataAndCount(inv);
	if (count > 0) {
		return true;
	}
	
	// FALLBACK: If uniqueID-based check failed, do a form-based check AND fix uniqueID
	// This handles the case where player dropped and picked up the item again
	// (uniqueID changed but item IS in inventory)
	const RE::FormID formID = _obj->GetFormID();
	const std::uint16_t storedUniqueID = this->GetUniqueID();
	for (const auto& [boundObj, data] : inv) {
		if (boundObj && boundObj->formID == formID && data.first > 0) {
			// Item found by formID - try to find and update the uniqueID
			// so future equip calls work correctly
			if ((storedUniqueID == 0 || data.first <= 1) && data.second && data.second->extraLists) {
				std::vector<RE::ExtraDataList*> extraLists;
				if (CopyExtraListsSafe(data.second->extraLists, extraLists)) {
					for (auto* extraList : extraLists) {
						if (HasTypeSafe(extraList, RE::ExtraDataType::kUniqueID)) {
							auto* uniqueIDData = GetByTypeSafe<RE::ExtraUniqueID>(extraList);
							if (uniqueIDData && uniqueIDData->uniqueID != 0) {
								// Found a valid uniqueID - update our stored one
								const_cast<WheelItemMutable*>(this)->SetUniqueID(uniqueIDData->uniqueID);
								break;
							}
						}
					}
				}
			}
			return true;
		}
	}
	return false;
}

const char* WheelItemMutable::GetItemName() const
{
	static thread_local std::string nameBuf;
	nameBuf = ResolveDisplayNameFallback(_obj);
	return nameBuf.c_str();
}

bool WheelItemMutable::SupportsPreciseHandIndicatorMatching(RE::TESObjectREFR::InventoryItemMap& a_inv) const
{
	if (!this->_obj || this->GetUniqueID() == 0) {
		return false;
	}

	auto itemData = const_cast<WheelItemMutable*>(this)->GetItemExtraDataAndCount(a_inv);
	return itemData.first == 1 && itemData.second != nullptr;
}



std::pair<int, RE::ExtraDataList*> WheelItemMutable::GetItemExtraDataAndCount(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	try {
		std::pair<int, RE::ExtraDataList*> ret = { 0, nullptr };

		// Early exit if _obj is null
		if (!this->_obj) {
			return ret;
		}

		const auto matchingEntries = CollectMatchingInventoryEntries(a_inv, this->_obj);
		const int rawItemCount = matchingEntries.rawItemCount;
		if (!matchingEntries.entries.empty()) {
			int cleanItemCount = 0;       // items not modified by enchantment, poison or tempering
			int representedItemCount = 0;  // items already represented by extraLists
			uint16_t uniqueID = this->GetUniqueID();
			// iterate through the entry, searching for extradata
			bool targetClean = false;
			
			std::vector<RE::ExtraDataList*> extraListSnapshot;
			for (auto* entryData : matchingEntries.entries) {
				if (!entryData || !entryData->extraLists) {
					continue;
				}

				std::vector<RE::ExtraDataList*> entrySnapshot;
				if (!CopyExtraListsSafe(entryData->extraLists, entrySnapshot)) {
					continue;
				}
				extraListSnapshot.insert(extraListSnapshot.end(), entrySnapshot.begin(), entrySnapshot.end());
			}

			if (extraListSnapshot.empty()) {
				// FormID-only fallback: if uniqueID is 0 (sentinel), return count from inventory
				// This allows activation of newly acquired items without UniqueID
				if (uniqueID == 0 && rawItemCount > 0) {
					ret.first = rawItemCount;
					return ret;
				}
				return ret;
			}

			for (auto* extraList : extraListSnapshot) {
				if (!extraList) {
					continue;
				}
				const int extraCount = GetExtraListCountSafe(extraList);
				representedItemCount += extraCount;
				const bool thisClean = IsCleanExtraListSafe(extraList);
				if (thisClean) {
					cleanItemCount += extraCount;
				}
				if (HasTypeSafe(extraList, RE::ExtraDataType::kUniqueID)) {
					auto* uniqueIDData = GetByTypeSafe<RE::ExtraUniqueID>(extraList);
					if (uniqueIDData && uniqueID == uniqueIDData->uniqueID) {
						ret.second = extraList;
						if (thisClean) {
							targetClean = true;
						}
					}
				}
			}
			const int implicitCleanItemCount = (std::max)(0, rawItemCount - representedItemCount);
			const int totalCleanItemCount = cleanItemCount + implicitCleanItemCount;
			if (uniqueID == 0) {
				const auto cleanPromotion = ResolveSingleCleanUniquePromotionCandidate(extraListSnapshot, totalCleanItemCount);
				if (cleanPromotion.extraList &&
				    cleanPromotion.uniqueID != 0 &&
				    ShouldAllowCleanSentinelPromotion(this->_obj, extraListSnapshot)) {
					const_cast<WheelItemMutable*>(this)->SetUniqueID(cleanPromotion.uniqueID);
					ret.second = cleanPromotion.extraList;
					ret.first = GetExtraListCountSafe(cleanPromotion.extraList);
					if (MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Input)) {
						MainWheelDebug::Log(
							MainWheelDebug::Category::Input,
							"MutableRelink: promoted clean sentinel to exact uniqueID formId={:08X} oldUniqueID=0 newUniqueID={} cleanCount={} rawCount={}",
							this->_obj ? this->_obj->GetFormID() : 0,
							cleanPromotion.uniqueID,
							totalCleanItemCount,
							rawItemCount);
					}
					return ret;
				}
			}
			// If uniqueID is 0 (FormID-only sentinel) and we have clean items, return them
			if (uniqueID == 0 && totalCleanItemCount > 0) {
				ret.first = totalCleanItemCount;
				return ret;
			}
			if (targetClean) {
				const bool collapsePlainCleanExactToSentinel =
					Wheeler::GetMutableInventoryCompatProfile() ==
					    Wheeler::MutableInventoryCompatProfile::Vanilla &&
					ret.second != nullptr &&
					!HasInstanceSpecificMutableDataSafe(ret.second) &&
					totalCleanItemCount > GetExtraListCountSafe(ret.second);
				if (collapsePlainCleanExactToSentinel) {
					const auto oldUniqueID = uniqueID;
					const int exactCleanCount = GetExtraListCountSafe(ret.second);
					const_cast<WheelItemMutable*>(this)->SetUniqueID(0);
					ret.second = nullptr;
					ret.first = totalCleanItemCount;
					logger::info(
						"MutableRelink: reverted plain clean exact unique to form-level stack formId={:08X} oldUniqueID={} cleanCount={} exactCount={} profile=vanilla",
						this->_obj ? this->_obj->GetFormID() : 0,
						oldUniqueID,
						totalCleanItemCount,
						exactCleanCount);
					return ret;
				}
				ret.first = GetExtraListCountSafe(ret.second);
				return ret;
			}
			if (ret.second != nullptr) {
				int equivalentItemCount = 0;
				for (auto* extraList : extraListSnapshot) {
					if (!extraList) {
						continue;
					}
					if (AreEquivalentExtraListsSafe(ret.second, extraList)) {
						equivalentItemCount += GetExtraListCountSafe(extraList);
					}
				}
				ret.first = (std::max)(equivalentItemCount, 1);
				return ret;
			}
			
			// FALLBACK: If uniqueID-based matching failed but item exists by formID,
			// return the first available extraList. This handles dropped & re-acquired items
			// where the uniqueID changed but we still want to equip the item.
			if (ret.first == 0 && ret.second == nullptr && rawItemCount > 0) {
				const bool canRepairUniqueID = uniqueID == 0 || rawItemCount <= 1;
				if (!canRepairUniqueID) {
					if (MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Input)) {
						MainWheelDebug::Log(
							MainWheelDebug::Category::Input,
							"MutableResolve: unresolved multi-instance uniqueID formId={:08X} uniqueID={} rawCount={} entries={} extraLists={}",
							this->_obj ? this->_obj->GetFormID() : 0,
							uniqueID,
							rawItemCount,
							matchingEntries.entries.size(),
							extraListSnapshot.size());
					}
					return ret;
				}
				// Try to find ANY valid extraList for this formID
				for (auto* extraList : extraListSnapshot) {
					if (extraList) {
						// Found an extraList - use it and update our uniqueID
						if (canRepairUniqueID && HasTypeSafe(extraList, RE::ExtraDataType::kUniqueID)) {
							auto* uniqueIDData = GetByTypeSafe<RE::ExtraUniqueID>(extraList);
							if (uniqueIDData && uniqueIDData->uniqueID != 0) {
								const_cast<WheelItemMutable*>(this)->SetUniqueID(uniqueIDData->uniqueID);
							}
						}
						ret.second = extraList;
						ret.first = totalCleanItemCount > 0 ? totalCleanItemCount : 1;
						return ret;
					}
				}
				// No extraList found, but item exists - return count without extraList
				ret.first = totalCleanItemCount > 0 ? totalCleanItemCount : rawItemCount;
			}
		}
		return ret;
	} catch (std::exception& e) {
		ERROR("Exception caught in WheelItemMutable::GetItemExtraDataAndCount: {}", e.what());
		return { 0, nullptr };
	}
}

bool WheelItemMutable::CanUseGroupedEquipFallback(RE::TESObjectREFR::InventoryItemMap& a_inv, RE::ExtraDataList* a_targetExtraList, int a_count) const
{
	if (!this->_obj || a_count < 2) {
		return false;
	}
	if (!a_targetExtraList || IsCleanExtraListSafe(a_targetExtraList)) {
		return true;
	}
	// Form-level grouped equip is only safe for clean or purely tempered stacks.
	// Enchantment/poison state is instance-backed in gameplay and collapsing back to
	// nullptr extraData can make identical named copies behave like a single weapon.
	if (HasTypeSafe(a_targetExtraList, RE::ExtraDataType::kEnchantment) ||
	    HasTypeSafe(a_targetExtraList, RE::ExtraDataType::kPoison)) {
		return false;
	}

	std::unique_ptr<RE::InventoryEntryData>* pp = nullptr;
	int rawItemCount = 0;
	for (auto& [boundObj, data] : a_inv) {
		if (boundObj && boundObj->formID == this->_obj->GetFormID()) {
			pp = &data.second;
			rawItemCount = data.first;
			break;
		}
	}
	if (!pp || !pp->get() || !pp->get()->extraLists || rawItemCount <= 0) {
		return false;
	}

	std::vector<RE::ExtraDataList*> extraListSnapshot;
	if (!CopyExtraListsSafe(pp->get()->extraLists, extraListSnapshot)) {
		return false;
	}

	int representedItemCount = 0;
	for (auto* extraList : extraListSnapshot) {
		if (!extraList) {
			continue;
		}
		representedItemCount += GetExtraListCountSafe(extraList);
		if (!AreEquivalentExtraListsSafe(a_targetExtraList, extraList)) {
			return false;
		}
	}

	return representedItemCount > 0 && representedItemCount == rawItemCount;
}

void WheelItemMutable::GetItemEnchantment(RE::TESObjectREFR::InventoryItemMap& a_invMap, std::vector<RE::EnchantmentItem*>& r_enchantments)
{
	if (!this->_obj) {
		return;
	}
	
	switch (this->_obj->GetFormType()) {
	case RE::FormType::Weapon:
		{
			auto weapon = static_cast<RE::TESObjectWEAP*>(this->_obj);
			if (weapon && weapon->formEnchanting) {
				r_enchantments.push_back(weapon->formEnchanting);
			}
			break;
		}
	case RE::FormType::Armor:
		{
			auto armor = static_cast<RE::TESObjectARMO*>(this->_obj);
			if (armor && armor->formEnchanting) {
				r_enchantments.push_back(armor->formEnchanting);
			}
			break;
		}
	}
	std::unique_ptr<RE::InventoryEntryData>* pp = nullptr;
	for (auto& [boundObj, data] : a_invMap) {
		if (boundObj && boundObj->formID == this->_obj->GetFormID()) {
			pp = &data.second;
			break;
		}
	}
	if (pp && pp->get()) {
		auto* extraLists = pp->get()->extraLists;
		if (extraLists) {
			std::vector<RE::ExtraDataList*> extraListSnapshot;
			if (!CopyExtraListsSafe(extraLists, extraListSnapshot)) {
				return;
			}
			for (auto* extraList : extraListSnapshot) {
				if (!extraList) {
					continue;
				}
				if (HasTypeSafe(extraList, RE::ExtraDataType::kUniqueID)) {
					auto* uniqueIDData = GetByTypeSafe<RE::ExtraUniqueID>(extraList);
					if (uniqueIDData && this->GetUniqueID() == uniqueIDData->uniqueID) {
						if (HasTypeSafe(extraList, RE::ExtraDataType::kEnchantment)) {
							auto* enchData = GetByTypeSafe<RE::ExtraEnchantment>(extraList);
							if (enchData && enchData->enchantment) {
								r_enchantments.push_back(enchData->enchantment);
							}
						}
						break;
					}
				}
			}
		}
	}
}

std::string WheelItemMutable::GetDisplayName(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	if (!this->_obj) {
		return "(deleted)";
	}

	auto itemData = this->GetItemExtraDataAndCount(a_inv);
	if (itemData.second) {
		std::string isolatedDisplayName = GetDisplayNameForExtraListSafe(this->_obj, itemData.second);
		if (!isolatedDisplayName.empty() && !IsPlaceholderDisplayName(isolatedDisplayName.c_str())) {
			return isolatedDisplayName;
		}
		return ResolveDisplayNameFallback(this->_obj);
	}

	const auto matchingEntries = CollectMatchingInventoryEntries(a_inv, this->_obj);
	if (matchingEntries.entries.size() == 1 && matchingEntries.rawItemCount <= 1) {
		if (RE::InventoryEntryData* entryData = matchingEntries.entries.front()) {
			const char* displayName = GetDisplayNameSafe(entryData);
			if (!IsPlaceholderDisplayName(displayName)) {
				return displayName;
			}
		}
	}

	return ResolveDisplayNameFallback(this->_obj);
}

void WheelItemMutable::GetItemEnchantment(RE::InventoryEntryData* a_iData, std::vector<RE::EnchantmentItem*>& r_enchantments)
{
	if (!this->_obj) {
		return;
	}
	
	switch (this->_obj->GetFormType()) {
	case RE::FormType::Weapon:
		{
			auto weapon = static_cast<RE::TESObjectWEAP*>(this->_obj);
			if (weapon && weapon->formEnchanting) {
				r_enchantments.push_back(weapon->formEnchanting);
			}
			break;
		}
	case RE::FormType::Armor:
		{
			auto armor = static_cast<RE::TESObjectARMO*>(this->_obj);
			if (armor && armor->formEnchanting) {
				r_enchantments.push_back(armor->formEnchanting);
			}
			break;
		}
	}

	if (a_iData && a_iData->extraLists) {
		std::vector<RE::ExtraDataList*> extraListSnapshot;
		if (!CopyExtraListsSafe(a_iData->extraLists, extraListSnapshot)) {
			return;
		}

		for (RE::ExtraDataList* extraList : extraListSnapshot) {
			if (!extraList) {
				continue;
			}
			if (HasTypeSafe(extraList, RE::ExtraDataType::kUniqueID)) {
				auto* uniqueIDData = GetByTypeSafe<RE::ExtraUniqueID>(extraList);
				if (uniqueIDData && this->GetUniqueID() == uniqueIDData->uniqueID) {
					if (HasTypeSafe(extraList, RE::ExtraDataType::kEnchantment)) {
						auto* enchData = GetByTypeSafe<RE::ExtraEnchantment>(extraList);
						if (enchData && enchData->enchantment) {
							r_enchantments.push_back(enchData->enchantment);
						}
					}
					break;
				}
			}
		}
	}
}

static bool filterMutableItems(RE::TESBoundObject& a_obj)
{
	switch (a_obj.GetFormType()) {
	case RE::FormType::Weapon:
	case RE::FormType::Armor:
		return true;
	}
	return false;
}
