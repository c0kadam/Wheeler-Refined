#pragma once

#include <atomic>

namespace InitState
{
	inline std::atomic_bool coreInitialized{ false };
	inline std::atomic_bool dataInitialized{ false };

	inline bool IsCoreInitialized()
	{
		return coreInitialized.load(std::memory_order_acquire);
	}

	inline bool IsDataInitialized()
	{
		return dataInitialized.load(std::memory_order_acquire);
	}

	inline void MarkCoreInitialized()
	{
		coreInitialized.store(true, std::memory_order_release);
	}

	inline void MarkDataInitialized()
	{
		dataInitialized.store(true, std::memory_order_release);
	}
}
