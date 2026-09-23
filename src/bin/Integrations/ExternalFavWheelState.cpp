#include "bin/Integrations/ExternalFavWheelState.h"

#include <Windows.h>

namespace
{
	struct ExternalWheelStatusAPI
	{
		std::uint32_t version;
		bool (*IsInitialized)();
		bool (*IsInEditMode)();
		bool (*IsWheelOpen)();
	};

	ExternalFavWheelState::ProbeResult QueryImpl()
	{
		auto* favWheelModule = ::GetModuleHandleW(L"favwheel.dll");
		if (!favWheelModule) {
			return ExternalFavWheelState::ProbeResult{
				false,
				ExternalFavWheelState::ProbeState::ModuleMissing,
				0
			};
		}

		using GetApiFn = void* (*)();
		auto* getApi = reinterpret_cast<GetApiFn>(::GetProcAddress(favWheelModule, "GetWheelerAPI"));
		if (!getApi) {
			return ExternalFavWheelState::ProbeResult{
				false,
				ExternalFavWheelState::ProbeState::ExportMissing,
				0
			};
		}

		auto* api = reinterpret_cast<ExternalWheelStatusAPI*>(getApi());
		if (!api) {
			return ExternalFavWheelState::ProbeResult{
				false,
				ExternalFavWheelState::ProbeState::ApiNull,
				0
			};
		}

		if (api->version < 1) {
			return ExternalFavWheelState::ProbeResult{
				false,
				ExternalFavWheelState::ProbeState::VersionTooOld,
				api->version
			};
		}

		if (!api->IsWheelOpen) {
			return ExternalFavWheelState::ProbeResult{
				false,
				ExternalFavWheelState::ProbeState::FunctionMissing,
				api->version
			};
		}

		const bool open = api->IsWheelOpen();
		return ExternalFavWheelState::ProbeResult{
			open,
			open ? ExternalFavWheelState::ProbeState::Open : ExternalFavWheelState::ProbeState::Closed,
			api->version
		};
	}
}

void ExternalFavWheelState::Prime()
{}

ExternalFavWheelState::ProbeResult ExternalFavWheelState::Query(ProbeOwner owner)
{
	(void)owner;
	return QueryImpl();
}
