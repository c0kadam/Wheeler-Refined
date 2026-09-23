#pragma once

#include "OStimConfigPolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace OStimUnifiedWheel
{
	inline constexpr std::size_t kFixedControlCount = 4;
	inline constexpr std::size_t kMaxDynamicCapacity =
		OStimConfigPolicy::kMaxSceneActionsPerPage;
	inline constexpr std::size_t kMaxEntryCount = kFixedControlCount + kMaxDynamicCapacity;

	struct PhysicalLayout
	{
		std::array<std::size_t, kFixedControlCount> fixedIndices{};
		std::array<std::size_t, kMaxDynamicCapacity> dynamicIndices{};
		std::size_t dynamicCapacity = OStimConfigPolicy::kDefaultSceneActionsPerPage;
		std::size_t entryCount = kFixedControlCount + dynamicCapacity;
	};

	constexpr PhysicalLayout BuildPhysicalLayout(std::uint32_t a_requestedCapacity) noexcept
	{
		PhysicalLayout layout{};
		layout.dynamicCapacity = OStimConfigPolicy::ClampSceneActionsPerPage(a_requestedCapacity);
		layout.entryCount = kFixedControlCount + layout.dynamicCapacity;

		// Entry zero starts at the bottom and indices advance clockwise on screen.
		// Select the four indices nearest the left axis, resolving an exact-distance
		// tie toward the lower index to preserve the original 12-slot [4,3,2,1] map.
		const std::size_t fixedLowIndex = (layout.entryCount - 5) / 4;
		for (std::size_t i = 0; i < kFixedControlCount; ++i) {
			layout.fixedIndices[i] = fixedLowIndex + (kFixedControlCount - 1 - i);
		}

		const std::size_t dynamicStart = fixedLowIndex + kFixedControlCount;
		for (std::size_t i = 0; i < layout.dynamicCapacity; ++i) {
			layout.dynamicIndices[i] = (dynamicStart + i) % layout.entryCount;
		}
		return layout;
	}

	enum class DynamicSlotKind : std::uint8_t
	{
		Empty = 0,
		Position,
		PreviousPage,
		NextPage
	};

	struct DynamicSlot
	{
		std::size_t physicalIndex = 0;
		DynamicSlotKind kind = DynamicSlotKind::Empty;
		std::size_t positionIndex = 0;
		std::uint32_t targetPage = 0;
	};

	struct Layout
	{
		PhysicalLayout physical{};
		std::array<DynamicSlot, kMaxDynamicCapacity> dynamicSlots{};
		std::size_t dynamicSlotCount = 0;
		std::uint32_t page = 0;
		std::uint32_t pageCount = 0;
		std::size_t positionStart = 0;
		std::size_t positionCount = 0;
	};

	constexpr std::uint32_t GetPageCount(
		std::size_t a_positionCount,
		std::uint32_t a_requestedCapacity) noexcept
	{
		const std::size_t capacity =
			OStimConfigPolicy::ClampSceneActionsPerPage(a_requestedCapacity);
		if (a_positionCount == 0) {
			return 0;
		}
		if (a_positionCount <= capacity) {
			return 1;
		}

		std::uint32_t pageCount = 1;
		std::size_t remaining = a_positionCount - (capacity - 1);
		while (remaining > 0) {
			++pageCount;
			if (remaining <= capacity - 1) {
				break;
			}
			remaining -= capacity - 2;
		}
		return pageCount;
	}

	constexpr Layout BuildLayout(
		std::size_t a_positionCount,
		std::uint32_t a_requestedPage,
		std::uint32_t a_requestedCapacity) noexcept
	{
		Layout layout{};
		layout.physical = BuildPhysicalLayout(a_requestedCapacity);
		layout.dynamicSlotCount = layout.physical.dynamicCapacity;
		for (std::size_t i = 0; i < layout.dynamicSlotCount; ++i) {
			layout.dynamicSlots[i].physicalIndex = layout.physical.dynamicIndices[i];
		}

		layout.pageCount = GetPageCount(a_positionCount, a_requestedCapacity);
		if (layout.pageCount == 0) {
			return layout;
		}
		layout.page = a_requestedPage < layout.pageCount ? a_requestedPage : layout.pageCount - 1;

		for (std::uint32_t page = 0; page < layout.page; ++page) {
			const bool hasPrevious = page > 0;
			const bool hasNext = page + 1 < layout.pageCount;
			layout.positionStart += layout.dynamicSlotCount -
				static_cast<std::size_t>(hasPrevious) -
				static_cast<std::size_t>(hasNext);
		}

		const bool hasPrevious = layout.page > 0;
		const bool hasNext = layout.page + 1 < layout.pageCount;
		std::size_t slot = 0;
		if (hasPrevious) {
			layout.dynamicSlots[slot].kind = DynamicSlotKind::PreviousPage;
			layout.dynamicSlots[slot].targetPage = layout.page - 1;
			++slot;
		}

		const std::size_t positionCapacity = layout.dynamicSlotCount -
			static_cast<std::size_t>(hasPrevious) -
			static_cast<std::size_t>(hasNext);
		const std::size_t remaining = a_positionCount - layout.positionStart;
		layout.positionCount = remaining < positionCapacity ? remaining : positionCapacity;
		for (std::size_t i = 0; i < layout.positionCount; ++i, ++slot) {
			layout.dynamicSlots[slot].kind = DynamicSlotKind::Position;
			layout.dynamicSlots[slot].positionIndex = layout.positionStart + i;
		}

		if (hasNext) {
			layout.dynamicSlots[slot].kind = DynamicSlotKind::NextPage;
			layout.dynamicSlots[slot].targetPage = layout.page + 1;
		}
		return layout;
	}

	constexpr bool IsCurrentLayoutRevision(
		std::uint64_t a_payloadRevision,
		std::uint64_t a_currentRevision) noexcept
	{
		return a_payloadRevision != 0 && a_payloadRevision == a_currentRevision;
	}
}
