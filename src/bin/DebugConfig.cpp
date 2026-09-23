#include "DebugConfig.h"
#include <SimpleIni.h>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace Config::Debug
{
    // ========== CATEGORY NAME TABLES ==========
    static constexpr const char* CategoryNames[] = {
        "Core_Startup",
        "Core_Hooks",
        "Core_Render",
        "UI_ImGui",
        "UI_Font",
        "UI_LayoutScaling",
        "UI_ResolutionFix",
        "Input_Gamepad",
        "Input_Mouse",
        "Input_Controls",
        "Activation_RTU",
        "Activation_InstantSpell",
        "Activation_InstantShout",
        "Activation_ActivateOnClose",
        "AmmoWheel_Core",
        "AmmoWheel_Reskin",
        "AmmoWheel_ReskinUnified",
        "AmmoWheel_Presets",
        "AmmoWheel_Navigation",
        "Texture_BaseIcons",
        "Texture_CustomIcons",
        "Texture_LookupFailures",
        "FavoritesMenu_Cache",
        "FavoritesMenu_Dump",
        "ShoutUtils",
        "Serialization",
        "Translations",
        "Controls_BindingConflicts",
        "TransformWheel",
        "ExternalAPI"
    };
    static_assert(std::size(CategoryNames) == static_cast<size_t>(Category::COUNT), "CategoryNames size mismatch");

    static constexpr const char* DEBUG_INI_PATH = R"(.\Data\SKSE\Plugins\wheeler\debug.ini)";

    namespace
    {
        bool LoadDebugIni(CSimpleIniA& ini)
        {
            ini.SetUnicode();

            std::error_code ec;
            if (!std::filesystem::exists(DEBUG_INI_PATH, ec)) {
                return false;
            }

            const SI_Error rc = ini.LoadFile(DEBUG_INI_PATH);
            return rc >= 0;
        }

        void ApplySpdlogLevelFromConfig()
        {
            if (!MasterEnabled) {
                return;
            }

            switch (GlobalLevel) {
            case LogLevel::Off:
                spdlog::set_level(spdlog::level::off);
                break;
            case LogLevel::Error:
                spdlog::set_level(spdlog::level::err);
                break;
            case LogLevel::Warn:
                spdlog::set_level(spdlog::level::warn);
                break;
            case LogLevel::Info:
                spdlog::set_level(spdlog::level::info);
                break;
            case LogLevel::Debug:
                spdlog::set_level(spdlog::level::debug);
                break;
            case LogLevel::Trace:
                spdlog::set_level(spdlog::level::trace);
                break;
            }
        }
    }

    // ========== HELPERS ==========
    const char* GetCategoryName(Category cat)
    {
        auto idx = static_cast<size_t>(cat);
        if (idx < std::size(CategoryNames)) {
            return CategoryNames[idx];
        }
        return "Unknown";
    }

    const char* GetCategoryINISection(Category cat)
    {
        static std::array<std::string, static_cast<size_t>(Category::COUNT)> sections;
        auto idx = static_cast<size_t>(cat);
        if (idx < sections.size()) {
            if (sections[idx].empty()) {
                sections[idx] = std::string("Debug.") + CategoryNames[idx];
            }
            return sections[idx].c_str();
        }
        return "Debug.Unknown";
    }

    const char* LogLevelToString(LogLevel lvl)
    {
        switch (lvl) {
            case LogLevel::Off:   return "Off";
            case LogLevel::Error: return "Error";
            case LogLevel::Warn:  return "Warn";
            case LogLevel::Info:  return "Info";
            case LogLevel::Debug: return "Debug";
            case LogLevel::Trace: return "Trace";
            default: return "Off";
        }
    }

    LogLevel LogLevelFromString(const char* str)
    {
        if (!str) return LogLevel::Off;
        std::string s(str);
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        
        if (s == "off")   return LogLevel::Off;
        if (s == "error") return LogLevel::Error;
        if (s == "warn")  return LogLevel::Warn;
        if (s == "info")  return LogLevel::Info;
        if (s == "debug") return LogLevel::Debug;
        if (s == "trace") return LogLevel::Trace;
        return LogLevel::Off;
    }

    LogLevel GetEffectiveLevel(Category cat)
    {
        if (!MasterEnabled) return LogLevel::Off;
        
        auto idx = static_cast<size_t>(cat);
        if (idx >= Categories.size()) return LogLevel::Off;
        
        const auto& cfg = Categories[idx];
        if (!cfg.Enabled) return LogLevel::Off;
        
        return cfg.UseInherit ? GlobalLevel : cfg.Level;
    }

    // ========== INIT SILENT ==========
    void InitSilent()
    {
        // Set spdlog to Off before anything else
        spdlog::set_level(spdlog::level::off);
    }

    void ReadWhitelistConfigOnly()
    {
        CSimpleIniA ini;
        if (!LoadDebugIni(ini)) {
            return;
        }

        WhitelistOnlyCoreStartupLogs = ini.GetBoolValue("Debug", "WhitelistOnlyCoreStartupLogs", true);
    }

    bool IsCoreStartupWhitelistMessage(std::string_view message)
    {
        auto contains = [&](std::string_view token) {
            return message.find(token) != std::string_view::npos;
        };

        if (contains("[BUILD_MARK] wheeler build")) {
            return true;
        }
        if (message.rfind("Wheeler - Refined ", 0) == 0) {
            return true;
        }
        if (message.rfind("=== Wheeler - Refined ", 0) == 0) {
            return true;
        }
        if (contains("Init: Load stage complete (trampoline=")) {
            return true;
        }
        if (contains("Init: Core init triggered by kPostPostLoad")) {
            return true;
        }
        if (contains("RenderManager: Installed hooks")) {
            return true;
        }
        if (contains("[WheelerAPI] v1 initialized successfully")) {
            return true;
        }
        if (contains("Installed hooks for (") && contains("OnChangePlayerInventory")) {
            return true;
        }
        if (contains("Installed all hooks")) {
            return true;
        }
        if (contains("Init: Core init complete")) {
            return true;
        }
        if (contains("Init: Data init triggered by")) {
            return true;
        }
        if (contains("Init: Data init complete")) {
            return true;
        }
        if (contains(": created user config '")) {
            return true;
        }
        if (contains(": failed to bootstrap '")) {
            return true;
        }
        if (contains(": failed to read defaults '") && contains("for bootstrap")) {
            return true;
        }
        if (contains(": Loading config (defaults=") || contains(": Loading config (factory=")) {
            return true;
        }
        if (contains(": Failed to load config from '")) {
            return true;
        }

        return false;
    }

    // ========== READ CONFIG ==========
    void ReadDebugConfig()
    {
        CSimpleIniA ini;
        if (!LoadDebugIni(ini)) {
            // Load failed - keep defaults (all OFF)
            return;
        }

        // Read global settings
        MasterEnabled = ini.GetBoolValue("Debug", "Enabled", false);
        GlobalLevel = LogLevelFromString(ini.GetValue("Debug", "GlobalLevel", "Off"));
        WhitelistOnlyCoreStartupLogs = ini.GetBoolValue("Debug", "WhitelistOnlyCoreStartupLogs", true);

        ApplySpdlogLevelFromConfig();

        // Read per-category settings
        for (size_t i = 0; i < static_cast<size_t>(Category::COUNT); ++i) {
            const char* section = GetCategoryINISection(static_cast<Category>(i));
            
            Categories[i].Enabled = ini.GetBoolValue(section, "Enabled", false);
            
            const char* levelStr = ini.GetValue(section, "Level", nullptr);
            if (levelStr) {
                std::string ls(levelStr);
                for (auto& c : ls) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (ls == "inherit") {
                    Categories[i].UseInherit = true;
                } else {
                    Categories[i].UseInherit = false;
                    Categories[i].Level = LogLevelFromString(levelStr);
                }
            } else {
                Categories[i].UseInherit = true;
            }

            Categories[i].CooldownMs = static_cast<std::uint32_t>(ini.GetLongValue(section, "CooldownMs", 0));
            Categories[i].OncePerSession = ini.GetBoolValue(section, "OncePerSession", false);
        }
    }
}
