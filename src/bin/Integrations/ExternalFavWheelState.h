#pragma once

#include <cstdint>

namespace ExternalFavWheelState
{
	enum class ProbeOwner : std::uint8_t
	{
		MainWheel,
		AmmoWheel
	};

	enum class ProbeState : std::uint8_t
	{
		ModuleMissing,
		ExportMissing,
		ApiNull,
		VersionTooOld,
		FunctionMissing,
		Closed,
		Open
	};

	struct ProbeResult
	{
		bool open{ false };
		ProbeState state{ ProbeState::ModuleMissing };
		std::uint32_t version{ 0 };
	};

	void Prime();
	ProbeResult Query(ProbeOwner owner);
}
