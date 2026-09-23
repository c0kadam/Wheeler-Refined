#pragma once
#include <cstdint>
#include <string>
#include <array>
#include <chrono>
#include <unordered_set>
#include <string_view>

namespace Config::Debug
{
    // ========== LOG LEVELS ==========
    enum class LogLevel : std::uint32_t {
        Off = 0,
        Error = 1,
        Warn = 2,
        Info = 3,
        Debug = 4,
        Trace = 5
    };

    // ========== CATEGORIES ==========
    // Each category maps to an INI section [Debug.<CategoryName>]
    enum class Category : std::uint32_t {
        // Core
        Core_Startup = 0,
        Core_Hooks,
        Core_Render,

        // UI
        UI_ImGui,
        UI_Font,
        UI_LayoutScaling,
        UI_ResolutionFix,

        // Input
        Input_Gamepad,
        Input_Mouse,
        Input_Controls,

        // Activation
        Activation_RTU,
        Activation_InstantSpell,
        Activation_InstantShout,
        Activation_ActivateOnClose,

        // AmmoWheel
        AmmoWheel_Core,
        AmmoWheel_Reskin,
        AmmoWheel_ReskinUnified,
        AmmoWheel_Presets,
        AmmoWheel_Navigation,

        // Texture
        Texture_BaseIcons,
        Texture_CustomIcons,
        Texture_LookupFailures,

        // FavoritesMenu
        FavoritesMenu_Cache,
        FavoritesMenu_Dump,

        // Misc
        ShoutUtils,
        Serialization,
        Translations,
        Controls_BindingConflicts,
        TransformWheel,
        ExternalAPI,

        COUNT
    };

    // ========== PER-CATEGORY CONFIG ==========
    struct CategoryConfig {
        bool Enabled = false;           // Category enabled
        LogLevel Level = LogLevel::Info; // Level threshold (if UseInherit=false)
        bool UseInherit = true;         // Inherit GlobalLevel
        std::uint32_t CooldownMs = 0;   // Rate limiting (0 = disabled)
        bool OncePerSession = false;    // One-shot logging
    };

    // ========== GLOBAL SETTINGS ==========
    inline bool MasterEnabled = false;                // DEFAULT OFF - no leaks
    inline LogLevel GlobalLevel = LogLevel::Off;      // DEFAULT OFF
    inline bool WhitelistOnlyCoreStartupLogs = true; // Default ON: keep wheeler.log quiet unless explicitly disabled via debug.ini
    inline std::array<CategoryConfig, static_cast<size_t>(Category::COUNT)> Categories{};

    // ========== RATE LIMITING STATE ==========
    struct RateLimitState {
        std::chrono::steady_clock::time_point lastLog{};
    };
    inline std::array<RateLimitState, static_cast<size_t>(Category::COUNT)> RateLimitStates{};
    inline std::unordered_set<std::string> OnceLogs{};  // Keys for one-shot logs

    // ========== API ==========

    /// Initialize logging to Off (call BEFORE InitializeLog in main.cpp)
    void InitSilent();

    /// Read lightweight pre-init options required before logger sink creation.
    void ReadWhitelistConfigOnly();

    /// Load debug.ini and configure categories
    void ReadDebugConfig();

    /// Returns true if message is in core startup allowlist.
    bool IsCoreStartupWhitelistMessage(std::string_view message);

    /// Get effective log level for a category (respects inheritance)
    LogLevel GetEffectiveLevel(Category cat);

    /// Get category name for INI/display
    const char* GetCategoryName(Category cat);

    /// Get category INI section key (e.g., "Debug.Core_Startup")
    const char* GetCategoryINISection(Category cat);

    /// Convert LogLevel to string
    const char* LogLevelToString(LogLevel lvl);

    /// Parse LogLevel from string
    LogLevel LogLevelFromString(const char* str);
}
