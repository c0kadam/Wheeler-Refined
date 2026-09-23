#pragma once

namespace DMenuInputOwnership
{
	// Queries dMenu's optional runtime API at the Skyrim input-dispatch boundary.
	// No module is loaded and no function pointer is retained.
	bool IsActive() noexcept;
}
