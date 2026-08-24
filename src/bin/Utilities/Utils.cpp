#include "Utils.h"
#include <chrono>
#include <string_view>

namespace
{
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
}

namespace Utils
{
	namespace Slot
	{
		RE::BGSEquipSlot* GetLeftHandSlot()
		{
			using func_t = decltype(&GetLeftHandSlot);
			const REL::Relocation<func_t> func{ RELOCATION_ID(23150, 23607) };
			return func();
		}
		RE::BGSEquipSlot* GetVoiceSlot()
		{
			using func_t = decltype(&GetVoiceSlot);
			const REL::Relocation<func_t> func{ RELOCATION_ID(23153, 23610) };
			return func();
		}

		RE::BGSEquipSlot* GetRightHandSlot()
		{
			using func_t = decltype(&GetRightHandSlot);
			const REL::Relocation<func_t> func{ RELOCATION_ID(23151, 23608) };
			return func();
		}
		void CleanSlot(RE::PlayerCharacter* a_pc, RE::BGSEquipSlot* a_slot)
		{
			RE::ActorEquipManager* aem = RE::ActorEquipManager::GetSingleton();
			if (!aem) {
				return;
			}
			auto* dummy = RE::TESForm::LookupByID<RE::TESForm>(0x00020163)->As<RE::TESObjectWEAP>();
			//sound false, queue false, force true
			aem->EquipObject(a_pc, dummy, nullptr, 1, a_slot, false, true, false);
			aem->UnequipObject(a_pc, dummy, nullptr, 1, a_slot, false, true, false);
		}
	}

	namespace Time
	{
		float GGTM()
		{
			return RE::BSTimer::QGlobalTimeMultiplier();
		}
		void SGTM(float a_in)
		{
			static REL::Relocation<float*> g_currentTimeScale{ RELOCATION_ID(511882, 388442) };
			static REL::Relocation<float*> g_targetTimeScale{ RELOCATION_ID(511883, 388443) };

			// Keep engine-side timer bookkeeping in sync with Wheeler's direct timescale writes.
			if (auto* timer = RE::BSTimer::GetSingleton()) {
				timer->SetGlobalTimeMultiplier(a_in, false);
				timer->useGlobalTimeMultiplierTarget = false;
			}

			if (auto* current = g_currentTimeScale.get()) {
				*current = a_in;
			}
			if (auto* target = g_targetTimeScale.get()) {
				*target = a_in;
			}
		}
	}

	namespace Player
	{
		bool IsMounted()
		{
			auto* pc = RE::PlayerCharacter::GetSingleton();
			if (!pc) {
				return false;
			}
			RE::ActorPtr mountPtr = nullptr;
			return pc->GetMount(mountPtr) && mountPtr != nullptr;
		}

		bool TryCaptureMountedVelocity(RE::NiPoint3& a_outVelocity, RE::FormID* a_outMountFormID)
		{
			auto* pc = RE::PlayerCharacter::GetSingleton();
			if (!pc) {
				return false;
			}

			RE::ActorPtr mountPtr = nullptr;
			if (!pc->GetMount(mountPtr) || !mountPtr) {
				return false;
			}

			auto* controller = mountPtr->GetCharController();
			if (!controller) {
				return false;
			}

			RE::hkVector4 velocity4{};
			controller->GetLinearVelocityImpl(velocity4);
			alignas(16) float components[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
			_mm_store_ps(components, velocity4.quad);
			a_outVelocity = RE::NiPoint3{ components[0], components[1], components[2] };
			if (a_outMountFormID) {
				*a_outMountFormID = mountPtr->GetFormID();
			}

			return true;
		}

		bool TryRestoreMountedVelocity(RE::FormID a_expectedMountFormID, const RE::NiPoint3& a_velocity, bool a_preserveCurrentVertical)
		{
			auto* pc = RE::PlayerCharacter::GetSingleton();
			if (!pc) {
				return false;
			}

			RE::ActorPtr mountPtr = nullptr;
			if (!pc->GetMount(mountPtr) || !mountPtr) {
				return false;
			}

			if (a_expectedMountFormID != 0 && mountPtr->GetFormID() != a_expectedMountFormID) {
				return false;
			}

			auto* controller = mountPtr->GetCharController();
			if (!controller) {
				return false;
			}

			float targetZ = a_velocity.z;
			if (a_preserveCurrentVertical) {
				RE::hkVector4 currentVelocity4{};
				controller->GetLinearVelocityImpl(currentVelocity4);
				alignas(16) float currentComponents[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
				_mm_store_ps(currentComponents, currentVelocity4.quad);
				targetZ = currentComponents[2];
			}

			controller->SetLinearVelocityImpl(RE::hkVector4(a_velocity.x, a_velocity.y, targetZ, 0.0f));
			return true;
		}
	}

	namespace Inventory
	{
		bool TryGetInventorySnapshot(RE::PlayerCharacter* a_player, RE::TESObjectREFR::InventoryItemMap& a_outInventory, std::string_view a_context)
		{
			a_outInventory.clear();
			if (!a_player) {
				return false;
			}

			const bool ok = InvokeWithSehGuard([&]() {
				a_outInventory = a_player->GetInventory();
			});
			if (!ok) {
				static auto s_lastInventorySnapshotWarn = std::chrono::steady_clock::time_point{};
				const auto now = std::chrono::steady_clock::now();
				if (now - s_lastInventorySnapshotWarn >= std::chrono::seconds(1)) {
					const std::string context = a_context.empty() ? "unknown" : std::string(a_context);
					logger::warn("Inventory snapshot skipped after unsafe GetInventory context='{}'",
						context);
					s_lastInventorySnapshotWarn = now;
				}
				a_outInventory.clear();
				return false;
			}
			return true;
		}

		std::pair<RE::EnchantmentItem*, float> GetEntryEnchantAndHealth(const std::unique_ptr<RE::InventoryEntryData>& a_invEntry)
		{
			std::pair<RE::EnchantmentItem*, float> ret = { nullptr, -1.f };
			if (a_invEntry->extraLists == nullptr) {
				return ret;
			}
			bool foundEnchant = false, foundHealth = false;
			std::vector<RE::ExtraDataList*> extraListSnapshot;
			if (!CopyExtraListsSafe(a_invEntry->extraLists, extraListSnapshot)) {
				return ret;
			}
			for (RE::ExtraDataList* extraDataList : extraListSnapshot) {
				if (extraDataList == nullptr) {
					continue;
				}
				bool hasEnchantment = false;
				if (TryHasTypeSafe(extraDataList, RE::ExtraDataType::kEnchantment, hasEnchantment) && hasEnchantment) {
					auto Xench = GetByTypeSafe<RE::ExtraEnchantment>(extraDataList);
					if (Xench != nullptr) {
						ret.first = Xench->enchantment;
						foundEnchant = true;
						if (foundHealth) {
							return ret;
						}
					}
				}
				bool hasHealth = false;
				if (TryHasTypeSafe(extraDataList, RE::ExtraDataType::kHealth, hasHealth) && hasHealth) {
					auto Xhealth = GetByTypeSafe<RE::ExtraHealth>(extraDataList);
					if (Xhealth != nullptr) {
						ret.second = Xhealth->health;
						foundHealth = true;
						if (foundEnchant) {
							return ret;
						}
					}
				}
			}
			return ret;
		}

		/// <summary>
		/// Gets all extradatalists of one inventory entry. 
		/// Inventory entries in Skyrim is designed as follows:
		/// Each entry has its base information stored directly in its form. However, once the entry is modified
		/// by player(e.g. smithing, enchanting, poisoning), the item the entry is associated with gains an ExtraData.
		/// The trick is that items of the same form(for example, 2 iron swords), albeit their different tempering/enchants,
		///  are stored in the same InventoryEntryData. Their modifications are in turn stored in 2 extradatlists of the given entry.
		/// </summary>
		void GetEntryExtraDataLists(std::vector<RE::ExtraDataList*>& r_ret, const std::unique_ptr<RE::InventoryEntryData>& a_invEntry)
		{
			if (a_invEntry->extraLists == nullptr) {
				return;
			}
			std::vector<RE::ExtraDataList*> extraListSnapshot;
			if (!CopyExtraListsSafe(a_invEntry->extraLists, extraListSnapshot)) {
				return;
			}
			for (RE::ExtraDataList* extraDataList : extraListSnapshot) {
				if (extraDataList == nullptr) {
					continue;
				}
				r_ret.push_back(extraDataList);
			}			
		}

		uint16_t GetNextUniqueID()
		{
			auto pc = RE::PlayerCharacter::GetSingleton();
			if (!pc) {
				return 0;
			}
			auto invc = pc->GetInventoryChanges();
			if (!invc) {
				return 0;
			}
			return invc->GetNextUniqueID();
		}

		RE::ExtraHealth GetExtraHealth(RE::ExtraDataList* a_list)
		{
			return *a_list->GetByType<RE::ExtraHealth>();
		}
		RE::ExtraEnchantment GetExtraEnchant(RE::ExtraDataList* a_list)
		{
			return *a_list->GetByType<RE::ExtraEnchantment>();
		}
		RE::ExtraPoison GetExtraPoison(RE::ExtraDataList* a_list)
		{
			return *a_list->GetByType<RE::ExtraPoison>();
		}

		Hand GetWeaponEquippedHand(RE::Actor* a_actor, RE::TESObjectWEAP* a_weapon, uint32_t a_uniqueID, bool itemClean)
		{
			if (!a_actor) {
				return Hand::None;
			}
			bool lhsEquipped = false, rhsEquipped = false;
			bool lhsEquippedBase = false, rhsEquippedBase = false;
			bool lhsFormMatch = false, rhsFormMatch = false;  // Track pure formID match for fallback
			RE::InventoryEntryData* lhs = a_actor->GetEquippedEntryData(true);
			if (lhs && lhs->object && lhs->object->GetFormID() == a_weapon->GetFormID()) {
				lhsFormMatch = true;  // FormID matches - used as fallback for restored items
				if (lhs->extraLists) {
					std::vector<RE::ExtraDataList*> extraListSnapshot;
					if (CopyExtraListsSafe(lhs->extraLists, extraListSnapshot)) {
						for (auto* extraList : extraListSnapshot) {
							if (!extraList) {
								continue;
							}
							bool hasEnchantment = false;
							bool hasHealth = false;
							bool hasPoison = false;
							const bool readEnchantment = TryHasTypeSafe(extraList, RE::ExtraDataType::kEnchantment, hasEnchantment);
							const bool readHealth = TryHasTypeSafe(extraList, RE::ExtraDataType::kHealth, hasHealth);
							const bool readPoison = TryHasTypeSafe(extraList, RE::ExtraDataType::kPoison, hasPoison);
							if (!readEnchantment || !readHealth || !readPoison) {
								continue;
							}
							if (!hasEnchantment && !hasHealth && !hasPoison) {
								lhsEquippedBase = true;
							}
							bool hasUniqueID = false;
							if (TryHasTypeSafe(extraList, RE::ExtraDataType::kUniqueID, hasUniqueID) && hasUniqueID) {
								auto* uniqueData = GetByTypeSafe<RE::ExtraUniqueID>(extraList);
								if (uniqueData && uniqueData->uniqueID == a_uniqueID) {
									lhsEquipped = true;
									break;
								}
							}
						}
					}
				}
			}
			auto rhs = a_actor->GetEquippedEntryData(false);
			if (rhs && rhs->object && rhs->object->GetFormID() == a_weapon->GetFormID()) {
				rhsFormMatch = true;  // FormID matches - used as fallback for restored items
				if (rhs->extraLists) {
					std::vector<RE::ExtraDataList*> extraListSnapshot;
					if (CopyExtraListsSafe(rhs->extraLists, extraListSnapshot)) {
						for (auto* extraList : extraListSnapshot) {
							if (!extraList) {
								continue;
							}
							bool hasEnchantment = false;
							bool hasHealth = false;
							bool hasPoison = false;
							const bool readEnchantment = TryHasTypeSafe(extraList, RE::ExtraDataType::kEnchantment, hasEnchantment);
							const bool readHealth = TryHasTypeSafe(extraList, RE::ExtraDataType::kHealth, hasHealth);
							const bool readPoison = TryHasTypeSafe(extraList, RE::ExtraDataType::kPoison, hasPoison);
							if (!readEnchantment || !readHealth || !readPoison) {
								continue;
							}
							if (!hasEnchantment && !hasHealth && !hasPoison) {
								rhsEquippedBase = true;
							}
							bool hasUniqueID = false;
							if (TryHasTypeSafe(extraList, RE::ExtraDataType::kUniqueID, hasUniqueID) && hasUniqueID) {
								auto* uniqueData = GetByTypeSafe<RE::ExtraUniqueID>(extraList);
								if (uniqueData && uniqueData->uniqueID == a_uniqueID) {
									rhsEquipped = true;
									break;
								}
							}
						}
					}
				}
			}
			if (itemClean && a_uniqueID == 0) {
				if (lhsEquippedBase && rhsEquippedBase) {
					return Hand::Both;
				} else if (lhsEquippedBase) {
					return Hand::Left;
				} else if (rhsEquippedBase) {
					return Hand::Right;
				}
			}
			if (lhsEquipped && rhsEquipped) {
				return Hand::Both;
			} else if (lhsEquipped) {
				return Hand::Left;
			} else if (rhsEquipped) {
				return Hand::Right;
			}
			// Form-level fallback is only safe for sentinel items. Concrete unique rows
			// must not inherit another same-form weapon's equipped hand.
			if (a_uniqueID == 0) {
				if (lhsFormMatch && rhsFormMatch) {
					return Hand::Both;
				} else if (lhsFormMatch) {
					return Hand::Left;
				} else if (rhsFormMatch) {
					return Hand::Right;
				}
			}
			return Hand::None;
		}
		RE::InventoryEntryData* GetSelectedItemIninventory(RE::InventoryMenu* a_invMenu)
		{
			if (!a_invMenu) {
				return nullptr;
			}
			RE::ItemList* il = a_invMenu->itemList;
			if (!il) {
				return nullptr;
			}
			RE::ItemList::Item* selectedItem = il->GetSelectedItem();
			if (!selectedItem) {
				return nullptr;
			}
			RE::InventoryEntryData* invEntry = selectedItem->data.objDesc;
			return invEntry;
		}
		RE::TESForm* GetSelectedFormInMagicMenu(RE::MagicMenu* a_magMen)
		{
			if (a_magMen) {
				RE::GFxValue result;
				a_magMen->uiMovie->GetVariable(&result, "_root.Menu_mc.inventoryLists.itemList.selectedEntry.formId");
				if (result.GetType() == RE::GFxValue::ValueType::kNumber) {
					RE::FormID formID = static_cast<std::uint32_t>(result.GetNumber());
					return RE::TESForm::LookupByID(formID);
				}
			}
			return nullptr;
		}
		// ===== FavoritesMenu Selection Cache =====
		// When Wheeler opens, GFx may lose focus and selectedIndex becomes -1.
		// Solution: Cache selection at wheel toggle time, use if live query fails.
		namespace FavoritesSelectionCache
		{
			static RE::FormID cachedFormID = 0;
			static std::chrono::steady_clock::time_point cachedTime{};
			static bool isValid = false;
			static bool dumpedOnce = false; // One-time dump for debugging
			
			// Debug dump members of a GFxValue object
			static void DumpMembers(const char* tag, const RE::GFxValue& obj)
			{
				if (dumpedOnce) return;
				if (!obj.IsObject() && !obj.IsDisplayObject()) {
					logger::info("[FavDump:{}] not an object, type={}", tag, static_cast<int>(obj.GetType()));
					return;
				}
				
				logger::info("[FavDump:{}] Dumping members:", tag);
				obj.VisitMembers([&](const char* name, const RE::GFxValue& val) {
					logger::info("[FavDump:{}] member='{}' type={}", tag, name, static_cast<int>(val.GetType()));
					return true;
				});
			}
			
			bool CaptureSelection(RE::FavoritesMenu* a_favMenu)
			{
				if (!a_favMenu) {
					logger::info("FavSelCache: capture FAILED (null menu)");
					return false;
				}
				
				auto& rt = a_favMenu->GetRuntimeData();
				
				// Debug: dump root members once
				if (!dumpedOnce) {
					logger::info("FavSelCache: === DUMPING runtimeData.root ===");
					DumpMembers("root", rt.root);
				}
				
				// Try to find the list object via runtimeData.root
				RE::GFxValue list;
				bool foundList = false;
				const char* listNames[] = { "itemList", "ItemList", "favoritesList", "FavoritesList", "list", "List_mc" };
				
				for (const char* name : listNames) {
					if (rt.root.GetMember(name, &list) && (list.IsObject() || list.IsDisplayObject())) {
						logger::info("FavSelCache: found list at root.'{}'", name);
						foundList = true;
						break;
					}
				}
				
				if (!foundList) {
					// Fallback: try string path via uiMovie
					if (a_favMenu->uiMovie) {
						const char* listPaths[] = {
							"_root.Menu_mc.itemList",
							"_root.Menu_mc.ItemList",
							"_root.itemList"
						};
						for (const char* path : listPaths) {
							a_favMenu->uiMovie->GetVariable(&list, path);
							if (list.IsObject() || list.IsDisplayObject()) {
								logger::info("FavSelCache: found list at '{}'", path);
								foundList = true;
								break;
							}
						}
					}
				}
				
				if (foundList && !dumpedOnce) {
					DumpMembers("list", list);
					dumpedOnce = true;
				}
				
				if (!foundList) {
					logger::info("FavSelCache: capture FAILED (no list found)");
					return false;
				}
				
				// Try to get selectedIndex from list
				int selectedIndex = -1;
				RE::GFxValue v;
				const char* indexNames[] = { "selectedIndex", "iSelectedIndex", "_selectedIndex", "selectedIdx" };
				for (const char* name : indexNames) {
					if (list.GetMember(name, &v) && v.IsNumber()) {
						selectedIndex = static_cast<int>(v.GetNumber());
						logger::info("FavSelCache: found list.{}={}", name, selectedIndex);
						break;
					}
				}
				
				// Use native favorites array with selectedIndex
				if (selectedIndex >= 0 && selectedIndex < static_cast<int>(rt.favorites.size())) {
					RE::TESForm* form = rt.favorites[selectedIndex].item;
					if (form) {
						cachedFormID = form->GetFormID();
						cachedTime = std::chrono::steady_clock::now();
						isValid = true;
						logger::info("FavSelCache: captured formID={:08X} name='{}' via index={}", 
							cachedFormID, form->GetName(), selectedIndex);
						return true;
					}
				}
				
				// Fallback: try selectedEntry.formId
				RE::GFxValue selEntry;
				const char* entryNames[] = { "selectedEntry", "selectedItem", "selectedData" };
				for (const char* name : entryNames) {
					if (list.GetMember(name, &selEntry) && selEntry.IsObject()) {
						RE::GFxValue formIdVal;
						const char* formIdNames[] = { "formId", "formID", "formid", "id" };
						for (const char* fid : formIdNames) {
							if (selEntry.GetMember(fid, &formIdVal) && formIdVal.IsNumber()) {
								RE::FormID fid_val = static_cast<RE::FormID>(formIdVal.GetNumber());
								if (fid_val != 0) {
									cachedFormID = fid_val;
									cachedTime = std::chrono::steady_clock::now();
									isValid = true;
									logger::info("FavSelCache: captured formID={:08X} via selectedEntry.{}", cachedFormID, fid);
									return true;
								}
							}
						}
					}
				}
				
				logger::info("FavSelCache: capture FAILED (no valid selection found, idx={})", selectedIndex);
				return false;
			}
			
			RE::FormID GetCachedFormID()
			{
				if (!isValid) return 0;
				
				// Cache valid for ~2 seconds
				auto now = std::chrono::steady_clock::now();
				auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - cachedTime);
				if (elapsed.count() > 2000) {
					logger::info("FavSelCache: cache expired (elapsed={}ms)", elapsed.count());
					isValid = false;
					return 0;
				}
				
				logger::info("FavSelCache: using cached formID={:08X}", cachedFormID);
				return cachedFormID;
			}
			
			void Invalidate()
			{
				isValid = false;
				cachedFormID = 0;
			}
		}
		
		RE::TESForm* GetSelectedFormInFavoritesMenu(RE::FavoritesMenu* a_favMenu)
		{
			logger::info("GetSelectedFormInFavoritesMenu: ENTER");
			if (!a_favMenu) {
				logger::info("GetSelectedFormInFavoritesMenu: FAIL favMenu=null");
				return nullptr;
			}
			
			auto& runtimeData = a_favMenu->GetRuntimeData();
			auto& favorites = runtimeData.favorites;
			logger::info("GetSelectedFormInFavoritesMenu: favoritesCount={}", favorites.size());
			
			if (favorites.empty()) {
				logger::info("GetSelectedFormInFavoritesMenu: FAIL favorites array empty");
				return nullptr;
			}
			
			// STRATEGY 1: Try GFx selectedEntry.formId directly
			if (a_favMenu->uiMovie) {
				RE::GFxValue result;
				
				const char* formIdPaths[] = {
					"_root.Menu_mc.itemList.selectedEntry.formId",
					"_root.Menu_mc.itemList.selectedEntry.formID",
					"_root.Menu_mc.ItemList.selectedEntry.formId",
					"_root.itemList.selectedEntry.formId"
				};
				
				for (const char* path : formIdPaths) {
					a_favMenu->uiMovie->GetVariable(&result, path);
					if (result.GetType() == RE::GFxValue::ValueType::kNumber) {
						RE::FormID formID = static_cast<RE::FormID>(result.GetNumber());
						if (formID != 0) {
							RE::TESForm* form = RE::TESForm::LookupByID(formID);
							if (form) {
								logger::info("GetSelectedFormInFavoritesMenu: SUCCESS via formId path='{}' formID={:08X} name='{}'", 
									path, formID, form->GetName());
								return form;
							}
						}
					}
				}
			}
			
			// STRATEGY 2: Try GFx selectedIndex + native array lookup
			int selectedIndex = -1;
			if (a_favMenu->uiMovie) {
				RE::GFxValue result;
				
				const char* indexPaths[] = {
					"_root.Menu_mc.itemList.selectedIndex",
					"_root.Menu_mc.ItemList.selectedIndex",
					"_root.itemList.selectedIndex"
				};
				
				for (const char* path : indexPaths) {
					a_favMenu->uiMovie->GetVariable(&result, path);
					if (result.GetType() == RE::GFxValue::ValueType::kNumber) {
						selectedIndex = static_cast<int>(result.GetNumber());
						logger::info("GetSelectedFormInFavoritesMenu: foundIndex={} at path='{}'", selectedIndex, path);
						break;
					}
				}
			}
			
			if (selectedIndex >= 0 && selectedIndex < static_cast<int>(favorites.size())) {
				RE::TESForm* form = favorites[selectedIndex].item;
				if (form) {
					logger::info("GetSelectedFormInFavoritesMenu: SUCCESS via index={} formID={:08X} name='{}'", 
						selectedIndex, form->GetFormID(), form->GetName());
					return form;
				}
			}
			
			// STRATEGY 3: Use cached selection (captured at wheel toggle time)
			RE::FormID cachedID = FavoritesSelectionCache::GetCachedFormID();
			if (cachedID != 0) {
				// Verify the cached form is still in favorites
				for (auto& entry : favorites) {
					if (entry.item && entry.item->GetFormID() == cachedID) {
						logger::info("GetSelectedFormInFavoritesMenu: SUCCESS via CACHE formID={:08X} name='{}'", 
							cachedID, entry.item->GetName());
						return entry.item;
					}
				}
				logger::info("GetSelectedFormInFavoritesMenu: cached formID={:08X} not in favorites", cachedID);
			}
			
			logger::info("GetSelectedFormInFavoritesMenu: all strategies FAILED");
			return nullptr;
		}
	}
	void NotificationMessage(std::string a_message)
	{
		RE::SendHUDMessage::ShowHUDMessage(a_message.data());
	}
}

static void stripMagicItemDescriptionFormatCode(std::string& a_description)
{
	if (a_description.empty()) {
		return;
	}
	while (true) {
		size_t bracketLhs = a_description.find_first_of('<');
		if (bracketLhs == std::string::npos) {
			break; // gtfo, no more format codes
		}
		// found '<', now to find '>'
		size_t bracketRhs = a_description.find_first_of('>');
		if (bracketRhs == std::string::npos) {
			break; // shouldn't happen, an opening bracket without a closing one
		}
		size_t bracketSize = bracketRhs - bracketLhs + 1;
		a_description.erase(bracketLhs, bracketSize); // get rid of bracket.
	}

}

static char toLowerAscii(char a_char)
{
	return a_char >= 'A' && a_char <= 'Z' ? static_cast<char>(a_char - 'A' + 'a') : a_char;
}

static bool equalsAtCaseInsensitive(std::string_view a_text, size_t a_pos, std::string_view a_needle)
{
	if (a_pos + a_needle.size() > a_text.size()) {
		return false;
	}

	for (size_t i = 0; i < a_needle.size(); ++i) {
		if (toLowerAscii(a_text[a_pos + i]) != toLowerAscii(a_needle[i])) {
			return false;
		}
	}
	return true;
}

static bool startsWithCaseInsensitive(std::string_view a_text, std::string_view a_needle)
{
	return equalsAtCaseInsensitive(a_text, 0, a_needle);
}

static bool isSurvivalModeItemCardMarkerAt(std::string_view a_text, size_t a_pos)
{
	return equalsAtCaseInsensitive(a_text, a_pos, "fSURV-") ||
	       equalsAtCaseInsensitive(a_text, a_pos, "fSURV=") ||
	       equalsAtCaseInsensitive(a_text, a_pos, "SURV-") ||
	       equalsAtCaseInsensitive(a_text, a_pos, "SURV=");
}

static size_t survivalModeItemCardMarkerLengthAt(std::string_view a_text, size_t a_pos)
{
	if (equalsAtCaseInsensitive(a_text, a_pos, "fSURV-") ||
	    equalsAtCaseInsensitive(a_text, a_pos, "fSURV=")) {
		return 6;
	}
	if (equalsAtCaseInsensitive(a_text, a_pos, "SURV-") ||
	    equalsAtCaseInsensitive(a_text, a_pos, "SURV=")) {
		return 5;
	}
	return 0;
}

static size_t findSurvivalModeItemCardMarker(std::string_view a_text, size_t a_start)
{
	for (size_t pos = a_start; pos < a_text.size(); ++pos) {
		if (isSurvivalModeItemCardMarkerAt(a_text, pos)) {
			return pos;
		}
	}
	return std::string_view::npos;
}

static size_t findLineStart(std::string_view a_text, size_t a_pos)
{
	if (a_pos == 0) {
		return 0;
	}

	const size_t lineBreak = a_text.find_last_of("\r\n", a_pos - 1);
	return lineBreak == std::string_view::npos ? 0 : lineBreak + 1;
}

static size_t findLineEnd(std::string_view a_text, size_t a_pos)
{
	const size_t lineEnd = a_text.find_first_of("\r\n", a_pos);
	return lineEnd == std::string_view::npos ? a_text.size() : lineEnd;
}

static size_t consumeLineBreak(std::string_view a_text, size_t a_pos)
{
	if (a_pos < a_text.size() && a_text[a_pos] == '\r') {
		++a_pos;
		if (a_pos < a_text.size() && a_text[a_pos] == '\n') {
			++a_pos;
		}
	} else if (a_pos < a_text.size() && a_text[a_pos] == '\n') {
		++a_pos;
	}
	return a_pos;
}

static bool isInlineWhitespaceRange(std::string_view a_text, size_t a_start, size_t a_end)
{
	for (size_t i = a_start; i < a_end; ++i) {
		if (a_text[i] != ' ' && a_text[i] != '\t') {
			return false;
		}
	}
	return true;
}

static bool isDescriptionEdgeWhitespace(char a_char)
{
	return a_char == ' ' || a_char == '\t' || a_char == '\r' || a_char == '\n';
}

static void trimDescriptionEdges(std::string& a_description)
{
	size_t start = 0;
	while (start < a_description.size() && isDescriptionEdgeWhitespace(a_description[start])) {
		++start;
	}

	size_t end = a_description.size();
	while (end > start && isDescriptionEdgeWhitespace(a_description[end - 1])) {
		--end;
	}

	if (start != 0 || end != a_description.size()) {
		a_description = a_description.substr(start, end - start);
	}
}

static std::string trimSurvivalModePayload(std::string_view a_payload)
{
	size_t start = 0;
	while (start < a_payload.size() && isDescriptionEdgeWhitespace(a_payload[start])) {
		++start;
	}

	size_t end = a_payload.size();
	while (end > start && isDescriptionEdgeWhitespace(a_payload[end - 1])) {
		--end;
	}

	return std::string(a_payload.substr(start, end - start));
}

static bool startsWithSurvivalContinuation(std::string_view a_line)
{
	size_t start = 0;
	while (start < a_line.size() && (a_line[start] == ' ' || a_line[start] == '\t')) {
		++start;
	}
	a_line.remove_prefix(start);

	return startsWithCaseInsensitive(a_line, "Hunger") ||
	       startsWithCaseInsensitive(a_line, "Cold") ||
	       startsWithCaseInsensitive(a_line, "Fatigue") ||
	       startsWithCaseInsensitive(a_line, "Warmth");
}

static void stripSurvivalModeItemCardText(std::string& a_description)
{
	size_t search = 0;
	bool changed = false;

	while (search < a_description.size()) {
		std::string_view view(a_description);
		size_t marker = findSurvivalModeItemCardMarker(view, search);
		if (marker == std::string_view::npos) {
			break;
		}

		size_t removeStart = marker;
		if (marker > 0 && view[marker - 1] == '[') {
			--removeStart;
		}

		const size_t markerLen = survivalModeItemCardMarkerLengthAt(view, marker);
		size_t removeEnd = std::string_view::npos;
		if (view[removeStart] == '[') {
			const size_t close = view.find(']', marker);
			if (close != std::string_view::npos) {
				removeEnd = close + 1;
				const std::string replacement = trimSurvivalModePayload(
					view.substr(marker + markerLen, close - (marker + markerLen)));
				a_description.replace(removeStart, removeEnd - removeStart, replacement);
				search = removeStart + replacement.size();
				changed = true;
				continue;
			}
		}

		if (removeEnd == std::string_view::npos) {
			removeEnd = findLineEnd(view, marker);

			const size_t nextLineStart = consumeLineBreak(view, removeEnd);
			const size_t nextLineEnd = findLineEnd(view, nextLineStart);
			if (nextLineStart < nextLineEnd &&
			    startsWithSurvivalContinuation(view.substr(nextLineStart, nextLineEnd - nextLineStart))) {
				removeEnd = nextLineEnd;
			}
		}

		if (removeEnd <= removeStart) {
			search = marker + 1;
			continue;
		}

		const size_t lineStart = findLineStart(view, removeStart);
		const size_t lineEnd = findLineEnd(view, removeEnd);
		if (isInlineWhitespaceRange(view, lineStart, removeStart) &&
		    isInlineWhitespaceRange(view, removeEnd, lineEnd)) {
			removeStart = lineStart;
			removeEnd = consumeLineBreak(view, lineEnd);
		}

		a_description.erase(removeStart, removeEnd - removeStart);
		search = removeStart;
		changed = true;
	}

	if (changed) {
		trimDescriptionEdges(a_description);
	}
}

/// <summary>
/// Get the description of the magic item without html formatting.
/// </summary>
void Utils::Magic::GetMagicItemDescription(RE::MagicItem* a_magicItem, std::string& a_buf)
{
	RE::BSString buf;
	RE::MagicSystem::GetMagicItemDescription(buf, a_magicItem, "", "");
	a_buf = buf.c_str();
	stripMagicItemDescriptionFormatCode(a_buf);
	stripSurvivalModeItemCardText(a_buf);
}

RE::NiMatrix3 Utils::Math::MatrixFromAxisAngle(float theta, const RE::NiPoint3& axis)
{
	RE::NiPoint3 a = axis;
	float cosTheta = cosf(theta);
	float sinTheta = sinf(theta);
	RE::NiMatrix3 result;

	result.entry[0][0] = cosTheta + a.x * a.x * (1 - cosTheta);
	result.entry[0][1] = a.x * a.y * (1 - cosTheta) - a.z * sinTheta;
	result.entry[0][2] = a.x * a.z * (1 - cosTheta) + a.y * sinTheta;

	result.entry[1][0] = a.y * a.x * (1 - cosTheta) + a.z * sinTheta;
	result.entry[1][1] = cosTheta + a.y * a.y * (1 - cosTheta);
	result.entry[1][2] = a.y * a.z * (1 - cosTheta) - a.x * sinTheta;

	result.entry[2][0] = a.z * a.x * (1 - cosTheta) - a.y * sinTheta;
	result.entry[2][1] = a.z * a.y * (1 - cosTheta) + a.x * sinTheta;
	result.entry[2][2] = cosTheta + a.z * a.z * (1 - cosTheta);

	return result;
}

bool Utils::Magic::IsSummonSpell(RE::SpellItem* a_spell)
{
	if (!a_spell) {
		return false;
	}
	
	for (auto* effect : a_spell->effects) {
		if (!effect || !effect->baseEffect) {
			continue;
		}
		
		auto archetype = effect->baseEffect->GetArchetype();
		// Check for summon-type archetypes
		switch (archetype) {
			case RE::EffectSetting::Archetype::kSummonCreature:
			case RE::EffectSetting::Archetype::kCommandSummoned:
			case RE::EffectSetting::Archetype::kReanimate:
				return true;
			default:
				break;
		}
	}
	return false;
}

int Utils::Magic::CountActiveEffectsFromSpell(RE::FormID a_spellFormID)
{
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return 0;
	}
	
	auto* activeEffects = pc->AsMagicTarget()->GetActiveEffectList();
	if (!activeEffects) {
		return 0;
	}
	
	int count = 0;
	for (auto* effect : *activeEffects) {
		if (!effect || !effect->spell) {
			continue;
		}
		
		if (effect->spell->GetFormID() == a_spellFormID) {
			count++;
		}
	}
	
	return count;
}
