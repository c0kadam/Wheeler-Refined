#include "OStimAutoModePolicy.h"
#include "OStimUnifiedWheelModel.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using OStimUnifiedWheel::DynamicSlotKind;

	bool Expect(bool a_condition, std::string_view a_case)
	{
		if (a_condition) {
			std::cout << "PASS " << a_case << '\n';
			return true;
		}
		std::cerr << "FAIL " << a_case << '\n';
		return false;
	}

	std::size_t CountKind(const OStimUnifiedWheel::Layout& a_layout, DynamicSlotKind a_kind)
	{
		return static_cast<std::size_t>(std::count_if(
			a_layout.dynamicSlots.begin(),
			a_layout.dynamicSlots.begin() + a_layout.dynamicSlotCount,
			[a_kind](const auto& a_slot) { return a_slot.kind == a_kind; }));
	}

	bool VerifyPhysicalPartition(std::uint32_t a_capacity)
	{
		const auto layout = OStimUnifiedWheel::BuildPhysicalLayout(a_capacity);
		std::array<bool, OStimUnifiedWheel::kMaxEntryCount> occupied{};
		for (const auto index : layout.fixedIndices) {
			if (index >= layout.entryCount || occupied[index]) {
				return false;
			}
			occupied[index] = true;
		}
		for (std::size_t i = 0; i < layout.dynamicCapacity; ++i) {
			const auto index = layout.dynamicIndices[i];
			if (index >= layout.entryCount || occupied[index]) {
				return false;
			}
			occupied[index] = true;
		}
		return std::all_of(
			occupied.begin(),
			occupied.begin() + layout.entryCount,
			[](bool a_value) { return a_value; });
	}

	bool VerifyNoLostOrDuplicatedPositions(
		std::size_t a_positionCount,
		std::uint32_t a_capacity)
	{
		std::vector<std::size_t> observed;
		const auto pageCount = OStimUnifiedWheel::GetPageCount(a_positionCount, a_capacity);
		for (std::uint32_t page = 0; page < pageCount; ++page) {
			const auto layout = OStimUnifiedWheel::BuildLayout(a_positionCount, page, a_capacity);
			for (std::size_t i = 0; i < layout.dynamicSlotCount; ++i) {
				const auto& slot = layout.dynamicSlots[i];
				if (slot.kind == DynamicSlotKind::Position) {
					observed.push_back(slot.positionIndex);
				}
			}
		}
		std::sort(observed.begin(), observed.end());
		if (observed.size() != a_positionCount) {
			return false;
		}
		for (std::size_t i = 0; i < observed.size(); ++i) {
			if (observed[i] != i) {
				return false;
			}
		}
		return true;
	}

	bool VerifyPageRules(std::size_t a_positionCount, std::uint32_t a_capacity)
	{
		const std::size_t capacity = OStimConfigPolicy::ClampSceneActionsPerPage(a_capacity);
		const auto pageCount = OStimUnifiedWheel::GetPageCount(a_positionCount, a_capacity);
		if (a_positionCount == 0) {
			const auto layout = OStimUnifiedWheel::BuildLayout(0, 99, a_capacity);
			return pageCount == 0 && CountKind(layout, DynamicSlotKind::Empty) == capacity;
		}
		if (a_positionCount <= capacity) {
			const auto layout = OStimUnifiedWheel::BuildLayout(a_positionCount, 99, a_capacity);
			return pageCount == 1 && layout.page == 0 &&
			       CountKind(layout, DynamicSlotKind::Position) == a_positionCount &&
			       CountKind(layout, DynamicSlotKind::PreviousPage) == 0 &&
			       CountKind(layout, DynamicSlotKind::NextPage) == 0;
		}

		for (std::uint32_t page = 0; page < pageCount; ++page) {
			const auto layout = OStimUnifiedWheel::BuildLayout(a_positionCount, page, a_capacity);
			const bool first = page == 0;
			const bool final = page + 1 == pageCount;
			if (CountKind(layout, DynamicSlotKind::PreviousPage) != (first ? 0u : 1u) ||
				CountKind(layout, DynamicSlotKind::NextPage) != (final ? 0u : 1u)) {
				return false;
			}
			const std::size_t expectedMaximum = capacity -
				static_cast<std::size_t>(!first) - static_cast<std::size_t>(!final);
			if (layout.positionCount == 0 || layout.positionCount > expectedMaximum) {
				return false;
			}
			if (!final && layout.positionCount != expectedMaximum) {
				return false;
			}
		}
		return VerifyNoLostOrDuplicatedPositions(a_positionCount, a_capacity);
	}

	bool VerifyExactMap(
		std::uint32_t a_capacity,
		std::array<std::size_t, 4> a_fixed,
		std::vector<std::size_t> a_dynamic)
	{
		const auto layout = OStimUnifiedWheel::BuildPhysicalLayout(a_capacity);
		return layout.fixedIndices == a_fixed &&
		       std::equal(
			       a_dynamic.begin(),
			       a_dynamic.end(),
			       layout.dynamicIndices.begin(),
			       layout.dynamicIndices.begin() + layout.dynamicCapacity);
	}
}

int main(int a_argc, char** a_argv)
{
	bool ok = true;
	constexpr std::array<std::uint32_t, 6> capacities{ 4, 5, 8, 10, 12, 16 };
	constexpr std::array<std::size_t, 6> navigationCounts{ 0, 1, 8, 9, 21, 40 };

	for (const auto capacity : capacities) {
		ok &= Expect(
			VerifyPhysicalPartition(capacity),
			"capacity " + std::to_string(capacity) + " partitions C+4 physical slots exactly once");
		for (const auto fixedCount : std::array<std::size_t, 2>{ capacity, capacity + 1 }) {
			ok &= Expect(
				VerifyPageRules(fixedCount, capacity),
				"capacity " + std::to_string(capacity) + " pages navigation count " + std::to_string(fixedCount));
		}
		for (const auto navigationCount : navigationCounts) {
			ok &= Expect(
				VerifyPageRules(navigationCount, capacity),
				"capacity " + std::to_string(capacity) + " verifies navigation count " + std::to_string(navigationCount));
		}
	}

	ok &= Expect(
		VerifyExactMap(4, { 3, 2, 1, 0 }, { 4, 5, 6, 7 }) &&
		VerifyExactMap(8, { 4, 3, 2, 1 }, { 5, 6, 7, 8, 9, 10, 11, 0 }) &&
		VerifyExactMap(12, { 5, 4, 3, 2 }, { 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1 }) &&
		VerifyExactMap(16, { 6, 5, 4, 3 }, { 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1, 2 }),
		"capacity 4/8/12/16 physical maps preserve the fixed left arc and clockwise dynamic traversal");

	const std::vector<std::string> sceneIDs = [] {
		std::vector<std::string> ids;
		for (std::size_t i = 0; i < 40; ++i) {
			ids.push_back("Scene" + std::to_string(i));
		}
		return ids;
	}();
	const auto capacity8 = OStimUnifiedWheel::BuildLayout(sceneIDs.size(), 2, 8);
	const auto capacity16 = OStimUnifiedWheel::BuildLayout(sceneIDs.size(), capacity8.page, 16);
	const auto capacity4 = OStimUnifiedWheel::BuildLayout(sceneIDs.size(), capacity16.page, 4);
	ok &= Expect(
		capacity8.physical.entryCount == 12 && capacity16.physical.entryCount == 20 &&
		capacity4.physical.entryCount == 8 &&
		capacity16.page < capacity16.pageCount && capacity4.page < capacity4.pageCount &&
		VerifyNoLostOrDuplicatedPositions(sceneIDs.size(), 8) &&
		VerifyNoLostOrDuplicatedPositions(sceneIDs.size(), 16) &&
		VerifyNoLostOrDuplicatedPositions(sceneIDs.size(), 4),
		"live capacity simulation 8->16->4 preserves one snapshot, clamps pages, and loses no scene IDs");

	ok &= Expect(
		!OStimUnifiedWheel::IsCurrentLayoutRevision(11, 12) &&
		OStimUnifiedWheel::IsCurrentLayoutRevision(12, 12) &&
		!OStimUnifiedWheel::IsCurrentLayoutRevision(0, 12),
		"old physical-slot payload revisions are invalid after a wheel rebuild");

	const auto sceneLayout = OStimUnifiedWheel::BuildLayout(sceneIDs.size(), 0, 8);
	std::string dispatchedID;
	for (std::size_t i = 0; i < sceneLayout.dynamicSlotCount; ++i) {
		const auto& slot = sceneLayout.dynamicSlots[i];
		if (slot.kind == DynamicSlotKind::Position && slot.positionIndex == 1) {
			dispatchedID = sceneIDs[slot.positionIndex];
		}
	}
	ok &= Expect(dispatchedID == "Scene1", "dynamic selection preserves the exact position.id mapping");

	ok &= Expect(
		std::string_view(GetOStimAutoModeLabel(false)) == "Auto Progress: OFF" &&
		std::string_view(GetOStimAutoModeLabel(true)) == "Auto Progress: ON",
		"Auto Progress label changes without changing its fixed semantic order");

	if (a_argc < 2) {
		std::cerr << "FAIL integration source path was not supplied\n";
		ok = false;
	} else {
		std::ifstream input(a_argv[1], std::ios::binary);
		const std::string source(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		const auto ensureBegin = source.find("bool EnsureControlWheel(");
		const auto ensureEnd = source.find("bool OpenControlWheel(", ensureBegin);
		const std::string ensureBlock = ensureBegin != std::string::npos && ensureEnd != std::string::npos ?
			source.substr(ensureBegin, ensureEnd - ensureBegin) : std::string{};
		const auto pageDispatch = source.find("result.path = \"ChangeUnifiedPage\"");
		const auto pageDispatchEnd = pageDispatch == std::string::npos ?
			std::string::npos : source.find("default:", pageDispatch);
		const bool pageBlockHasNativeNavigation =
			pageDispatch != std::string::npos && pageDispatchEnd != std::string::npos &&
			source.substr(pageDispatch, pageDispatchEnd - pageDispatch).find("NavigateToScene") != std::string::npos;
		ok &= Expect(input.good() || input.eof(), "integration source opened for structural verification");
		ok &= Expect(
			ensureBlock.find("EnsureManagedWheel(kControlWheelTag") != std::string::npos &&
			ensureBlock.find("PopulateWheel(kControlWheelTag") != std::string::npos &&
			ensureBlock.find("DeleteManagedWheels") == std::string::npos &&
			ensureBlock.find("Wheeler::CloseWheeler") == std::string::npos,
			"capacity rebuild resizes the same managed wheel without deletion or close");
		ok &= Expect(
			source.find("Config::OStimIntegration::MaxPositionsPerPage") != std::string::npos &&
			source.find("wheelRebuildRequested") != std::string::npos &&
			source.find("resized ? UnifiedFocusHint::None") != std::string::npos,
			"live config refresh consumes capacity and clears unsafe resize hover");
		ok &= Expect(
			pageDispatch != std::string::npos && !pageBlockHasNativeNavigation,
			"Prev/Next rebuild Wheeler state without NavigateToScene");
		ok &= Expect(
			source.find("payload.sceneID = position.id;") != std::string::npos &&
			source.find("OStimNGThreadAPI::NavigateToScene(") != std::string::npos,
			"dynamic selection keeps exact position.id dispatch");
		ok &= Expect(
			source.find("IsStaleManagedWheelPayload") != std::string::npos &&
			source.find("StaleWheelLayout") != std::string::npos,
			"runtime rejects payloads captured before any unified-wheel rebuild");
		ok &= Expect(
			source.find("EnsureManagedWheel(kBrowserWheelTag") == std::string::npos &&
			source.find("FindWheelIndexByTag(kBrowserWheelTag") == std::string::npos,
			"normal unified flow never creates or enters a Browser wheel");
	}

	std::cout << (ok ? "OStim unified wheel verification PASSED\n" :
		"OStim unified wheel verification FAILED\n");
	return ok ? 0 : 1;
}
