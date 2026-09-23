#include "UserInput/Input.h"
#include "UserInput/Controls.h"

#include "Rendering/RenderManager.h"
#include "Rendering/TextureManager.h"

#include "Hooks.h"

#include "Wheeler/WheelItems/WheelItemMutableManager.h"
#include "Wheeler/Wheeler.h"
#include "Utilities/UniqueIDHandler.h"
#include "Serialization/SerializationEntry.h"

#include "InitState.h"
#include "Config.h"
#include "DebugConfig.h"
#include "LogGate.h"
#include "Texts.h"
#include "ModCallbackEventHandler.h"
#include "Integrations/ActionHotkeysBridge.h"
#include "Integrations/OStimIntegration.h"

namespace
{
	constexpr std::size_t kUnifiedTrampolineSize = 1 << 7;  // 128 bytes for all call hooks
	std::atomic_bool g_coreInitDone{ false };
	std::atomic_bool g_dataInitDone{ false };

	bool IsModuleLoaded(std::wstring_view dllName)
	{
		return dllName.empty() ? false : (::GetModuleHandleW(dllName.data()) != nullptr);
	}

	bool HasLoadedPlugin(std::string_view pluginName)
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler || pluginName.empty()) {
			return false;
		}

		if (dataHandler->LookupModByName(pluginName.data()) != nullptr) {
			return true;
		}

		if (dataHandler->LookupLoadedLightModByName(pluginName.data()) != nullptr) {
			return true;
		}

		return false;
	}

	template <class Predicate>
	std::string JoinPresentNames(std::initializer_list<const char*> names, Predicate&& predicate)
	{
		std::string result;
		for (const char* name : names) {
			if (!name || !predicate(name)) {
				continue;
			}
			if (!result.empty()) {
				result += ", ";
			}
			result += name;
		}
		return result.empty() ? std::string("none") : result;
	}

	void LogEnvironmentCompatibilityProbe()
	{
		const std::string equipDlls = JoinPresentNames(
			{ "EquipEnchantmentFix.dll",
			  "EquipmentDurabilitySystemNG.dll",
			  "ImmersiveWeaponSwitch.dll",
			  "UnequipQuiverNG.dll",
			  "DynamicCollisionAdjustment.dll",
			  "CombatPathingRevolution.dll",
			  "valhallaCombat.dll",
			  "MCO.dll",
			  "Precision.dll" },
			[](const char* name) {
				std::wstring wide(name, name + std::char_traits<char>::length(name));
				return IsModuleLoaded(wide);
			});

		const std::string oarDlls = JoinPresentNames(
			{ "OpenAnimationReplacer.dll",
			  "OpenAnimationReplacer-IEDConditionExtensions.dll",
			  "OpenAnimationReplacer-DetectionPlugin.dll",
			  "OpenAnimationReplacer-DialoguePlugin.dll" },
			[](const char* name) {
				std::wstring wide(name, name + std::char_traits<char>::length(name));
				return IsModuleLoaded(wide);
			});

		const std::string renderDlls = JoinPresentNames(
			{ "hdtSMP64.dll",
			  "po3_ENBLightForEffectShaders.dll",
			  "StormLightning.dll",
			  "Vibrant weapons.dll",
			  "ImmersiveEquipmentDisplays.dll" },
			[](const char* name) {
				std::wstring wide(name, name + std::char_traits<char>::length(name));
				return IsModuleLoaded(wide);
			});

		const std::string combatPlugins = JoinPresentNames(
			{ "Left Hand Equipment Overhaul.esp",
			  "Draw Fix - Move Equip Animation Fix.esp",
			  "ValhallaCombat.esp",
			  "Attack_DXP.esp",
			  "MCO - First Person Patch.esp",
			  "scar-adxp-patch.esp",
			  "OCPA.esl",
			  "DynamicCollisionAdjustment.esl" },
			[](const char* name) {
				return HasLoadedPlugin(name);
			});

		const std::string scenePlugins = JoinPresentNames(
			{ "Lux.esp",
			  "Lux Orbis.esp",
			  "Lux Via.esp",
			  "Water for ENB (Shades of Skyrim).esp",
			  "NAT-ENB.esp",
			  "ENB Light.esp" },
			[](const char* name) {
				return HasLoadedPlugin(name);
			});

		const int nolvusLikeScore =
			static_cast<int>(HasLoadedPlugin("Left Hand Equipment Overhaul.esp")) +
			static_cast<int>(HasLoadedPlugin("Draw Fix - Move Equip Animation Fix.esp")) +
			static_cast<int>(HasLoadedPlugin("ValhallaCombat.esp")) +
			static_cast<int>(HasLoadedPlugin("Attack_DXP.esp")) +
			static_cast<int>(HasLoadedPlugin("MCO - First Person Patch.esp")) +
			static_cast<int>(HasLoadedPlugin("OCPA.esl")) +
			static_cast<int>(HasLoadedPlugin("DynamicCollisionAdjustment.esl")) +
			static_cast<int>(IsModuleLoaded(L"CombatPathingRevolution.dll")) +
			static_cast<int>(IsModuleLoaded(L"UnequipQuiverNG.dll")) +
			static_cast<int>(HasLoadedPlugin("Lux Via.esp")) +
			static_cast<int>(HasLoadedPlugin("NAT-ENB.esp"));

		logger::info("CompatProbe: equip/combat DLL cluster = {}", equipDlls);
		logger::info("CompatProbe: OAR cluster = {}", oarDlls);
		logger::info("CompatProbe: render/equipment visual DLL cluster = {}", renderDlls);
		logger::info("CompatProbe: drawn-weapon plugin cluster = {}", combatPlugins);
		logger::info("CompatProbe: scene/render plugin cluster = {}", scenePlugins);
		logger::info(
			"CompatProbe: mutable inventory profile={} edsActive={}",
			Wheeler::IsEquipmentDurabilitySystemActive() ? "equipment_durability_system" : "vanilla",
			Wheeler::IsEquipmentDurabilitySystemActive() ? 1 : 0);
		logger::info(
			"CompatProbe: profile score={} assessment={}",
			nolvusLikeScore,
			nolvusLikeScore >= 6 ? "heavy_list_drawn_weapon_risk" : "general_profile");
	}

	class CoreStartupWhitelistSink final : public spdlog::sinks::sink
	{
	public:
		explicit CoreStartupWhitelistSink(std::shared_ptr<spdlog::sinks::sink> inner) :
			_innerSink(std::move(inner))
		{}

		void log(const spdlog::details::log_msg& msg) override
		{
			if (Config::Debug::WhitelistOnlyCoreStartupLogs) {
				const std::string_view payload{ msg.payload.data(), msg.payload.size() };
				if (!Config::Debug::IsCoreStartupWhitelistMessage(payload)) {
					return;
				}
			}
			_innerSink->log(msg);
		}

		void flush() override
		{
			_innerSink->flush();
		}

		void set_pattern(const std::string& pattern) override
		{
			_innerSink->set_pattern(pattern);
		}

		void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) override
		{
			_innerSink->set_formatter(std::move(sink_formatter));
		}

	private:
		std::shared_ptr<spdlog::sinks::sink> _innerSink;
	};

	const char* GetMessageTypeName(std::uint32_t type)
	{
		switch (type) {
		case SKSE::MessagingInterface::kPostLoad:
			return "kPostLoad";
		case SKSE::MessagingInterface::kPostPostLoad:
			return "kPostPostLoad";
		case SKSE::MessagingInterface::kInputLoaded:
			return "kInputLoaded";
		case SKSE::MessagingInterface::kDataLoaded:
			return "kDataLoaded";
		case SKSE::MessagingInterface::kNewGame:
			return "kNewGame";
		case SKSE::MessagingInterface::kPostLoadGame:
			return "kPostLoadGame";
		case SKSE::MessagingInterface::kSaveGame:
			return "kSaveGame";
		case SKSE::MessagingInterface::kDeleteGame:
			return "kDeleteGame";
		default:
			return "kUnknown";
		}
	}

	void EnsureCoreInit(const char* trigger)
	{
		if (g_coreInitDone.exchange(true)) {
			logger::info("Init: Core already done, skipping ({})", trigger);
			return;
		}

		logger::info("Init: Core init triggered by {}", trigger);

		if (!RenderManager::Install()) {
			logger::warn("Init: RenderManager::Install returned false");
		}

		Wheeler::Init();
		Hooks::Install();

		auto serialization = SKSE::GetSerializationInterface();
		if (serialization) {
			serialization->SetUniqueID(WHEELER_SERIALIZATION_ID);
			SerializationEntry::BindSerializationCallbacks(serialization);
		} else {
			logger::warn("Init: Serialization interface unavailable during core init");
		}

		InitState::MarkCoreInitialized();
		logger::info("Init: Core init complete");
	}

	void EnsureDataInit(const char* trigger)
	{
		if (g_dataInitDone.exchange(true)) {
			logger::info("Init: Data already done, skipping ({})", trigger);
			return;
		}

		logger::info("Init: Data init triggered by {}", trigger);
		EnsureCoreInit("kDataLoaded fallback");

		WheelItemMutableManager::GetSingleton()->Register();
		Config::ReadStyleConfig();
		Hooks::InstallMutableInventoryHooksFromConfig();
		Config::ReadControlConfig();
		Config::ReadActionHotkeysBridgeConfig();
		Config::ReadOStimIntegrationConfig();
		Config::ReadAmmoWheelConfig();
		Config::OffsetSizingToViewport();
		Config::OffsetAmmoWheelSizingToViewport();
		Controls::BindAllInputsFromConfig();
		Texture::Init();
		Texts::LoadTranslations();
		ActionHotkeysBridge::Init();
		OStimIntegration::Init();
		ModCallbackEventHandler::Register();
		LogEnvironmentCompatibilityProbe();

		InitState::MarkDataInitialized();
		logger::info("Init: Data init complete");
	}

	void InitializeLog()
	{
#ifndef NDEBUG
		auto baseSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
		const auto level = spdlog::level::trace;
#else
		auto path = logger::log_directory();
		if (!path) {
			util::report_and_fail("Failed to find standard logging directory"sv);
		}

		*path /= fmt::format("{}.log"sv, Plugin::NAME);
		auto baseSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		// Trim log noise in release: keep info+ (warnings/errors still shown)
		const auto level = spdlog::level::info;
#endif

		auto sink = std::make_shared<CoreStartupWhitelistSink>(std::move(baseSink));
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(level);
		log->flush_on(level);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%^%l%$] %v"s);
	}
}

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
	if (!a_msg) {
		return;
	}

	logger::debug("Init: Message received {}", GetMessageTypeName(a_msg->type));

	switch (a_msg->type) {
	case SKSE::MessagingInterface::kPostPostLoad:
		EnsureCoreInit("kPostPostLoad");
		break;
	case SKSE::MessagingInterface::kDataLoaded:
		EnsureDataInit("kDataLoaded");
		break;
	case SKSE::MessagingInterface::kPostLoad:
		break;
	case SKSE::MessagingInterface::kSaveGame:
		break;
	case SKSE::MessagingInterface::kNewGame:
		Wheeler::SetupDefaultWheels();
		[[fallthrough]];
	case SKSE::MessagingInterface::kPostLoadGame:
		UniqueIDHandler::QueuePostLoadInventoryRepair(GetMessageTypeName(a_msg->type));
		break;
	default:
		break;
	}
}

std::string wstring2string(const std::wstring& wstr, UINT CodePage)

{

	std::string ret;

	int len = WideCharToMultiByte(CodePage, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);

	ret.resize((size_t)len, 0);

	WideCharToMultiByte(CodePage, 0, wstr.c_str(), (int)wstr.size(), &ret[0], len, NULL, NULL);

	return ret;

}


extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = Plugin::NAME.data();
	a_info->version = Plugin::VERSION[0];

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
	if (ver < SKSE::RUNTIME_SSE_1_5_39) {
		logger::critical(FMT_STRING("Unsupported runtime version {}"), ver.string());
		return false;
	}

	return true;
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;

	v.PluginVersion(Plugin::VERSION);
	v.PluginName(Plugin::NAME);

	v.UsesAddressLibrary();
	v.UsesNoStructs();

	v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST });
	return v;
}();


extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	// Initialize logging to Off FIRST (no leaks before debug.ini is loaded)
	Config::Debug::InitSilent();
	// Read allowlist toggle before creating the sink wrapper
	Config::Debug::ReadWhitelistConfigOnly();
	InitializeLog();
	// Load debug.ini and configure log levels
	Config::Debug::ReadDebugConfig();

	logger::critical("[BUILD_MARK] wheeler build {} {}", __DATE__, __TIME__);

	// Startup banner - ALWAYS log version/author info (not gated)
	logger::info("{} {} by {} | Build: {}"sv, Plugin::DISPLAY_NAME, Plugin::DISPLAY_VERSION, Plugin::AUTHOR, Plugin::BUILD_TIMESTAMP);
	logger::info("=== Wheeler - Refined v1.3.3 | 4/26/2026 | External API Enabled | C0kadam ===");

	SKSE::Init(a_skse);

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		return false;
	}

	SKSE::AllocTrampoline(kUnifiedTrampolineSize);
	logger::info("Init: Load stage complete (trampoline={} bytes)", kUnifiedTrampolineSize);

	return true;
}
