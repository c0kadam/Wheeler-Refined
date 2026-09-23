#pragma once

#include <cstdint>

class OStimNGSceneAPI
{
public:
	enum class DispatchResult : std::uint8_t
	{
		Success = 0,
		APIUnavailable,
		Invalid,
		Failed
	};

	static bool IsAvailable();
	static DispatchResult SetAutoMode(std::uint32_t a_threadID, bool a_autoMode);
	static const char* GetDispatchResultReason(DispatchResult a_result);
};
