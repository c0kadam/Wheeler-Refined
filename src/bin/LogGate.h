#pragma once
#include "DebugConfig.h"
#include <chrono>
#include <string>

namespace LogGate
{
    using Cat = Config::Debug::Category;
    using Level = Config::Debug::LogLevel;

    // ========== CORE GATING ==========

    /// Check if a category is enabled at the given level
    inline bool Enabled(Cat cat, Level lvl)
    {
        if (!Config::Debug::MasterEnabled) return false;
        
        auto idx = static_cast<size_t>(cat);
        if (idx >= Config::Debug::Categories.size()) return false;
        
        const auto& cfg = Config::Debug::Categories[idx];
        if (!cfg.Enabled) return false;
        
        Level effective = cfg.UseInherit ? Config::Debug::GlobalLevel : cfg.Level;
        return static_cast<uint32_t>(lvl) <= static_cast<uint32_t>(effective);
    }

    /// Check with rate limiting (returns true if should log)
    inline bool RateLimited(Cat cat, Level lvl, std::uint32_t cooldownMs)
    {
        if (!Enabled(cat, lvl)) return false;
        if (cooldownMs == 0) return true;  // No rate limiting
        
        auto idx = static_cast<size_t>(cat);
        if (idx >= Config::Debug::RateLimitStates.size()) return false;
        
        auto& state = Config::Debug::RateLimitStates[idx];
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.lastLog).count();
        
        if (elapsed >= static_cast<long long>(cooldownMs)) {
            state.lastLog = now;
            return true;
        }
        return false;
    }

    /// Check for one-shot logging (returns true only on first call per key)
    inline bool EnabledOnce(Cat cat, Level lvl, const char* key)
    {
        if (!Enabled(cat, lvl)) return false;
        
        std::string k = std::string(Config::Debug::GetCategoryName(cat)) + ":" + key;
        if (Config::Debug::OnceLogs.find(k) != Config::Debug::OnceLogs.end()) {
            return false;  // Already logged
        }
        Config::Debug::OnceLogs.insert(k);
        return true;
    }

    /// Check with auto-cooldown from category config
    inline bool RateLimitedAuto(Cat cat, Level lvl)
    {
        auto idx = static_cast<size_t>(cat);
        if (idx >= Config::Debug::Categories.size()) return false;
        return RateLimited(cat, lvl, Config::Debug::Categories[idx].CooldownMs);
    }
}

// ========== GATING MACROS ==========
// These short-circuit string formatting when disabled.
// Usage: LOG_INFO(Core_Startup, "Message {}", arg);

#define LOG_INFO(cat, ...) \
    do { if (LogGate::Enabled(LogGate::Cat::cat, LogGate::Level::Info)) { \
        logger::info(__VA_ARGS__); \
    }} while(0)

#define LOG_WARN(cat, ...) \
    do { if (LogGate::Enabled(LogGate::Cat::cat, LogGate::Level::Warn)) { \
        logger::warn(__VA_ARGS__); \
    }} while(0)

#define LOG_DEBUG(cat, ...) \
    do { if (LogGate::Enabled(LogGate::Cat::cat, LogGate::Level::Debug)) { \
        logger::debug(__VA_ARGS__); \
    }} while(0)

#define LOG_ERROR(cat, ...) \
    do { if (LogGate::Enabled(LogGate::Cat::cat, LogGate::Level::Error)) { \
        logger::error(__VA_ARGS__); \
    }} while(0)

#define LOG_TRACE(cat, ...) \
    do { if (LogGate::Enabled(LogGate::Cat::cat, LogGate::Level::Trace)) { \
        logger::trace(__VA_ARGS__); \
    }} while(0)

// ========== RATE-LIMITED VARIANTS ==========
// Usage: LOG_INFO_RATE(Activation_RTU, 250, "Message {}", arg);

#define LOG_INFO_RATE(cat, cooldownMs, ...) \
    do { if (LogGate::RateLimited(LogGate::Cat::cat, LogGate::Level::Info, cooldownMs)) { \
        logger::info(__VA_ARGS__); \
    }} while(0)

#define LOG_DEBUG_RATE(cat, cooldownMs, ...) \
    do { if (LogGate::RateLimited(LogGate::Cat::cat, LogGate::Level::Debug, cooldownMs)) { \
        logger::debug(__VA_ARGS__); \
    }} while(0)

// ========== ONE-SHOT VARIANTS ==========
// Usage: LOG_INFO_ONCE(Core_Startup, "startup_banner", "Message");

#define LOG_INFO_ONCE(cat, key, ...) \
    do { if (LogGate::EnabledOnce(LogGate::Cat::cat, LogGate::Level::Info, key)) { \
        logger::info(__VA_ARGS__); \
    }} while(0)

#define LOG_WARN_ONCE(cat, key, ...) \
    do { if (LogGate::EnabledOnce(LogGate::Cat::cat, LogGate::Level::Warn, key)) { \
        logger::warn(__VA_ARGS__); \
    }} while(0)

// ========== AUTO RATE-LIMITED (uses category's CooldownMs) ==========
// Usage: LOG_INFO_AUTO(UI_LayoutScaling, "Message");

#define LOG_INFO_AUTO(cat, ...) \
    do { if (LogGate::RateLimitedAuto(LogGate::Cat::cat, LogGate::Level::Info)) { \
        logger::info(__VA_ARGS__); \
    }} while(0)

#define LOG_DEBUG_AUTO(cat, ...) \
    do { if (LogGate::RateLimitedAuto(LogGate::Cat::cat, LogGate::Level::Debug)) { \
        logger::debug(__VA_ARGS__); \
    }} while(0)
