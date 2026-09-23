#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace HoverActivationSnapshotPolicy
{
	using HoverIndex = std::int32_t;

	inline HoverIndex Load(const std::atomic<HoverIndex>& a_hoveredEntryIdx) noexcept
	{
		return a_hoveredEntryIdx.load(std::memory_order_relaxed);
	}

	inline void Store(std::atomic<HoverIndex>& a_hoveredEntryIdx, HoverIndex a_value) noexcept
	{
		a_hoveredEntryIdx.store(a_value, std::memory_order_relaxed);
	}

	inline constexpr bool IsValid(HoverIndex a_snapshot, std::size_t a_entryCount) noexcept
	{
		return a_snapshot >= 0 && static_cast<std::size_t>(a_snapshot) < a_entryCount;
	}

	inline std::optional<HoverIndex> Capture(
		const std::atomic<HoverIndex>& a_hoveredEntryIdx,
		std::size_t a_entryCount) noexcept
	{
		const HoverIndex snapshot = Load(a_hoveredEntryIdx);
		if (!IsValid(snapshot, a_entryCount)) {
			return std::nullopt;
		}
		return snapshot;
	}
}
