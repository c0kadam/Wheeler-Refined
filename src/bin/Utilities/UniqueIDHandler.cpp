#include "UniqueIDHandler.h"
#include "Utils.h"
#include "RE/T/TESObjectWEAP.h"

namespace
{
	constexpr std::size_t kExtraDataListLegacySize = 0x18;
	constexpr std::size_t kExtraDataListPost629Size = 0x20;

	constexpr std::size_t GetExtraDataListRuntimeSize(const REL::Version& a_runtime) noexcept
	{
		// The universal SE/AE representation does not match the loaded game's
		// runtime layout, so sizeof(RE::ExtraDataList) is not valid here.
		return a_runtime.compare(SKSE::RUNTIME_SSE_1_6_629) == std::strong_ordering::less ?
		           kExtraDataListLegacySize :
		           kExtraDataListPost629Size;
	}

	static_assert(GetExtraDataListRuntimeSize(SKSE::RUNTIME_SSE_1_5_97) == 0x18);
	static_assert(GetExtraDataListRuntimeSize(SKSE::RUNTIME_SSE_1_6_1170) == 0x20);
	static_assert(GetExtraDataListRuntimeSize(SKSE::RUNTIME_SSE_1_7_99) == 0x20);

	inline RE::ExtraDataList* InitExtraDataList(RE::ExtraDataList* a_list)
	{
		using func_t = RE::ExtraDataList* (*)(RE::ExtraDataList*);
		REL::Relocation<func_t> func{ RELOCATION_ID(11437, 11583) };
		return func(a_list);
	}

	int GetRepresentedItemCount(RE::ExtraDataList* a_list)
	{
		if (!a_list) {
			return 0;
		}

		const int count = a_list->GetCount();
		return count > 0 ? count : 1;
	}

	bool HasReferenceIdentityData(RE::ExtraDataList* a_list)
	{
		return a_list &&
		       (a_list->HasType(RE::ExtraDataType::kReferenceHandle) ||
		        a_list->HasType(RE::ExtraDataType::kOriginalReference) ||
		        a_list->HasType(RE::ExtraDataType::kAliasInstanceArray));
	}

	bool IsTransientBoundWeapon(RE::TESBoundObject* a_object)
	{
		auto* weapon = a_object ? a_object->As<RE::TESObjectWEAP>() : nullptr;
		return weapon && weapon->IsBound();
	}
}

void UniqueIDHandler::EnsureXListUniquenessInPcInventory()
{
	try {
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return;
		}
		RE::TESObjectREFR::InventoryItemMap inv;
		if (!Utils::Inventory::TryGetInventorySnapshot(pc, inv, "EnsureXListUniquenessInPcInventory")) {
			return;
		}
		for (auto& [boundObj, data] : inv) {
			auto rawCount = data.first;
			auto& entryData = data.second;

#undef GetObject
			auto* object = entryData ? entryData->GetObject() : nullptr;
			if (!object || IsTransientBoundWeapon(object)) {
				continue;
			}

			auto ft = object->GetFormType();
			if (ft != RE::FormType::Armor && ft != RE::FormType::Weapon) {
				continue;
			}

			if (entryData->extraLists) {
				for (auto& xList : *entryData->extraLists) {
					if (xList) {
						auto count = GetRepresentedItemCount(xList);
						rawCount -= count;
						try {
							if (!HasReferenceIdentityData(xList)) {
								EnsureXListUniqueness(xList);
							}
						} catch (std::exception& exception) {
							logger::error(
								"Error occured when ensuring extraDataList uniqueness: {}, item: {}",
								exception.what(),
								entryData->GetObject() ? entryData->GetObject()->GetName() : "unknown");
#ifdef UNICODE
#	define GetObject GetObjectW
#else
#	define GetObject GetObjectA
#endif	// !UNICODE
						}
					}
				}
			}

			// Avoid remove/re-add inventory writes here; they can trigger UI "item added"
			// style notifications. We can attach synthetic extra lists directly.
			while (rawCount-- > 0) {
				RE::ExtraDataList* xList = nullptr;
				EnsureXListUniqueness(xList);
				if (xList) {
					entryData->AddExtraList(xList);
				}
			}
		}
	} catch (std::exception& exception) {
		logger::error("Error occured when scanning player inventory extraDataList: {}", exception.what());
	}
}

void UniqueIDHandler::EnsureXListUniqueness(RE::ExtraDataList*& a_extraList)
{
	auto pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}

	if (a_extraList == nullptr) {
		const auto runtimeSize = GetExtraDataListRuntimeSize(REL::Module::get().version());
		a_extraList = static_cast<RE::ExtraDataList*>(Utils::Workaround::NiMemAlloc_1400F6B40(runtimeSize));
		if (!a_extraList) {
			return;
		}
		a_extraList = InitExtraDataList(a_extraList);
		if (!a_extraList) {
			return;
		}
	}

	auto invChanges = pc->GetInventoryChanges();
	if (!invChanges) {
		// Fallback: keep a valid extra list even if uniqueID allocation is unavailable right now.
		return;
	}

	if (!a_extraList->HasType(RE::ExtraDataType::kUniqueID)) {
		uint16_t nextID = invChanges->GetNextUniqueID();
		auto xID = new RE::ExtraUniqueID(0x14, nextID);
		a_extraList->Add(xID);
	}
}

std::uint16_t UniqueIDHandler::RetagXListUniqueID(RE::ExtraDataList* a_extraList)
{
	if (!a_extraList) {
		return 0;
	}

	auto pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return 0;
	}

	auto invChanges = pc->GetInventoryChanges();
	if (!invChanges) {
		return 0;
	}

	const std::uint16_t nextID = invChanges->GetNextUniqueID();
	if (nextID == 0) {
		return 0;
	}

	if (auto* uniqueIDData = a_extraList->GetByType<RE::ExtraUniqueID>()) {
		uniqueIDData->uniqueID = nextID;
		return nextID;
	}

	auto* xID = new RE::ExtraUniqueID(0x14, nextID);
	if (!xID) {
		return 0;
	}

	a_extraList->Add(xID);
	return nextID;
}

bool UniqueIDHandler::ShouldBypassInventoryHooks()
{
	return false;
}

void UniqueIDHandler::QueuePostLoadInventoryRepair(std::string_view)
{
	// FavWheel-style weapon core does not use Wheeler's older deferred post-load repair path.
}
