#include "bin/Integrations/DMenuInputOwnership.h"

#include <Windows.h>

namespace
{
	using QueryFlag = bool (*)();
}

bool DMenuInputOwnership::IsActive() noexcept
{
	auto* module = ::GetModuleHandleW(L"dmenu.dll");
	if (!module) {
		return false;
	}

	auto* isMenuOpen = reinterpret_cast<QueryFlag>(::GetProcAddress(module, "dMenu_IsMenuOpen"));
	if (!isMenuOpen) {
		return false;
	}

	// Readiness is part of the current API but remains optional for compatibility
	// with older dMenu builds that already exported dMenu_IsMenuOpen.
	auto* isReady = reinterpret_cast<QueryFlag>(::GetProcAddress(module, "dMenu_IsReady"));
	if (isReady && !isReady()) {
		return false;
	}

	return isMenuOpen();
}
