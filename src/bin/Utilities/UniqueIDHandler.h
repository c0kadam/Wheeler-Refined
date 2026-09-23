#pragma once
#include <string_view>
class UniqueIDHandler
{
public:
	static void EnsureXListUniquenessInPcInventory();

	/// <summary>
	/// Add a uniqueID to the given extraDatalist. If the given extradatalist is a nullptr, initialize it.
	/// Do nothing if the given extraDatalist already has a uniqueID.
	/// </summary>
	/// <param name="a_extraList"></param>
	/// <param name="a_count"></param>
	static void EnsureXListUniqueness(RE::ExtraDataList*& a_extraList);
	static std::uint16_t RetagXListUniqueID(RE::ExtraDataList* a_extraList);

	// Compatibility shims for newer call sites outside the weapon restore path.
	static bool ShouldBypassInventoryHooks();
	static void QueuePostLoadInventoryRepair(std::string_view a_reason = {});
};
