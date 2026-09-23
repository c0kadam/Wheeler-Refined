#include "PreviewConfig.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <filesystem>

// ========== Define statics - AmmoWheel defaults from AmmoWheel.ini ==========

// Position & Geometry
float PreviewConfig::AmmoWheel::WheelRadius = 250.0f;
float PreviewConfig::AmmoWheel::InnerRadiusRatio = 0.55f;
float PreviewConfig::AmmoWheel::SlotGapDeg = 1.5f;
float PreviewConfig::AmmoWheel::ArcStartAngle = 180.0f;    // Left side
float PreviewConfig::AmmoWheel::ArcSweepAngle = 180.0f;    // Half circle
int PreviewConfig::AmmoWheel::WheelShape = 1;              // 1 = Half circle

// Slot Shape (NOW FULLY SUPPORTED IN-GAME!)
int PreviewConfig::AmmoWheel::SlotShape = 0;               // 0=Arc, 1=RoundedRect, 2=Pill, 3=Circle
float PreviewConfig::AmmoWheel::SlotCornerRadius = 8.0f;   // Corner radius for RoundedRect
float PreviewConfig::AmmoWheel::SlotShapeScale = 0.9f;     // Scale factor for non-arc shapes

// Visual Polish - Background
bool PreviewConfig::AmmoWheel::BackgroundEnabled = true;
float PreviewConfig::AmmoWheel::BackgroundOpacity = 0.7f;
float PreviewConfig::AmmoWheel::BackgroundRadiusScale = 1.15f;

// Visual Polish - Border (arc-only)
bool PreviewConfig::AmmoWheel::BorderEnabled = true;
float PreviewConfig::AmmoWheel::BorderInnerScale = 1.05f;
float PreviewConfig::AmmoWheel::BorderOuterScale = 1.08f;

// Visual Polish - Slot Shadow
bool PreviewConfig::AmmoWheel::SlotShadowEnabled = true;
float PreviewConfig::AmmoWheel::SlotShadowOffsetX = 2.0f;
float PreviewConfig::AmmoWheel::SlotShadowOffsetY = 2.0f;
int PreviewConfig::AmmoWheel::SlotShadowAlpha = 150;

// Visual Polish - Slot Highlight
bool PreviewConfig::AmmoWheel::SlotHighlightEnabled = true;
float PreviewConfig::AmmoWheel::SlotHighlightThickness = 2.0f;
int PreviewConfig::AmmoWheel::SlotHighlightAlpha = 80;

// Animation - Hover Pulse
bool PreviewConfig::AmmoWheel::HoverPulseEnabled = true;
float PreviewConfig::AmmoWheel::HoverPulseSpeed = 3.0f;
float PreviewConfig::AmmoWheel::HoverPulseSize = 5.0f;

// Animation - Slot Dividers (arc-only)
bool PreviewConfig::AmmoWheel::SlotDividersEnabled = true;
float PreviewConfig::AmmoWheel::SlotDividerThickness = 2.0f;

// Theme
bool PreviewConfig::AmmoWheel::UseSkyrimTheme = true;

// ========== POPUP SETTINGS ==========
bool PreviewConfig::AmmoWheel::PopupEnabled = true;
float PreviewConfig::AmmoWheel::PopupIconSizePx = 96.0f;
float PreviewConfig::AmmoWheel::PopupNameFontPx = 32.0f;
float PreviewConfig::AmmoWheel::PopupCountFontPx = 24.0f;
float PreviewConfig::AmmoWheel::PopupOffsetPx = 80.0f;
float PreviewConfig::AmmoWheel::PopupPaddingPx = 15.0f;
bool PreviewConfig::AmmoWheel::PopupCircular = true;
float PreviewConfig::AmmoWheel::PopupBubbleRadius = 85.0f;
float PreviewConfig::AmmoWheel::PopupAnimationSpeed = 8.0f;

// Popup Animation
bool PreviewConfig::AmmoWheel::PopupAnimEnabled = true;
float PreviewConfig::AmmoWheel::PopupAnimHoverInMs = 120.0f;
float PreviewConfig::AmmoWheel::PopupAnimHoverOutMs = 90.0f;
float PreviewConfig::AmmoWheel::PopupAnimScaleFrom = 0.85f;
float PreviewConfig::AmmoWheel::PopupAnimScaleTo = 1.0f;
int PreviewConfig::AmmoWheel::PopupAnimEasing = 1;  // OutCubic
float PreviewConfig::AmmoWheel::PopupAnimBorderThickness = 2.5f;
float PreviewConfig::AmmoWheel::PopupAnimBorderOpacity = 0.8f;
float PreviewConfig::AmmoWheel::PopupAnimBackgroundOpacity = 0.85f;

// ========== DISPLAY SETTINGS ==========
bool PreviewConfig::AmmoWheel::ShowAmmoCount = true;
float PreviewConfig::AmmoWheel::CountFontSize = 16.0f;
bool PreviewConfig::AmmoWheel::ShowIcons = true;
float PreviewConfig::AmmoWheel::IconSize = 48.0f;
float PreviewConfig::AmmoWheel::IconRadiusRatio = 0.72f;
bool PreviewConfig::AmmoWheel::IconHoverGlow = true;

// ========== LABEL SETTINGS ==========
bool PreviewConfig::AmmoWheel::LabelShow = true;
int PreviewConfig::AmmoWheel::LabelTruncateLength = 10;
bool PreviewConfig::AmmoWheel::LabelAbbreviate = true;
bool PreviewConfig::AmmoWheel::LabelMultiLine = true;
float PreviewConfig::AmmoWheel::LabelMaxSlotArcRatio = 0.75f;

// ========== TEXT SETTINGS ==========
float PreviewConfig::AmmoWheel::NameFontPx = 24.0f;
float PreviewConfig::AmmoWheel::CountFontPx = 20.0f;
float PreviewConfig::AmmoWheel::NameTextScale = 1.0f;
bool PreviewConfig::AmmoWheel::TextShadowEnabled = true;
int PreviewConfig::AmmoWheel::TextShadowLayers = 2;
int PreviewConfig::AmmoWheel::TextShadowAlpha = 180;
float PreviewConfig::AmmoWheel::TextShadowOffset = 1.5f;
bool PreviewConfig::AmmoWheel::TextHoverGlowEnabled = true;

// ========== CENTER PANEL ==========
bool PreviewConfig::AmmoWheel::CenterEnabled = true;
bool PreviewConfig::AmmoWheel::CenterBgEnabled = true;
float PreviewConfig::AmmoWheel::CenterBgOpacity = 0.7f;
float PreviewConfig::AmmoWheel::CenterPaddingPx = 10.0f;
float PreviewConfig::AmmoWheel::CenterMaxWidthRatio = 0.75f;
float PreviewConfig::AmmoWheel::CenterLineSpacingPx = 4.0f;
float PreviewConfig::AmmoWheel::CenterFontPx = 20.0f;
int PreviewConfig::AmmoWheel::CenterPanelShapeIndex = 0;
float PreviewConfig::AmmoWheel::CenterPanelCornerRounding = 8.0f;
float PreviewConfig::AmmoWheel::CenterPanelBorderThickness = 1.5f;
float PreviewConfig::AmmoWheel::CenterPanelBorderAlpha = 0.8f;
bool PreviewConfig::AmmoWheel::CenterFrameEnabled = true;
bool PreviewConfig::AmmoWheel::CenterFramePulse = true;
float PreviewConfig::AmmoWheel::CenterFramePulseSpeed = 2.0f;

// ========== LOW AMMO INDICATOR ==========
bool PreviewConfig::AmmoWheel::LowAmmoIndicatorEnabled = true;
int PreviewConfig::AmmoWheel::LowAmmoThreshold = 10;
float PreviewConfig::AmmoWheel::LowAmmoIndicatorThickness = 2.0f;
float PreviewConfig::AmmoWheel::SelectedIndicatorThickness = 3.0f;

// ========== Colors ==========

// Slot colors (AmmoWheel blue theme)
unsigned int PreviewConfig::Colors::UnhoveredColorBegin = 0xC8285078;  // IM_COL32(40, 60, 80, 200)
unsigned int PreviewConfig::Colors::UnhoveredColorEnd = 0xB4143C50;    // IM_COL32(20, 40, 60, 180)
unsigned int PreviewConfig::Colors::HoveredColorBegin = 0xDC508CC8;    // IM_COL32(80, 140, 200, 220)
unsigned int PreviewConfig::Colors::HoveredColorEnd = 0xC83C64A0;      // IM_COL32(60, 100, 160, 200)
unsigned int PreviewConfig::Colors::SelectedColorBegin = 0xFFD0A070;
unsigned int PreviewConfig::Colors::SelectedColorEnd = 0xFFA08060;
unsigned int PreviewConfig::Colors::TextColor = 0xFFF0E6D2;

// Border colors (gold/bronze)
unsigned int PreviewConfig::Colors::BorderColorInner = 0xFFDAA520;     // Goldenrod
unsigned int PreviewConfig::Colors::BorderColorOuter = 0xFF8B4513;     // SaddleBrown

// Effect colors
unsigned int PreviewConfig::Colors::HoverPulseColor = 0x78FFD700;      // Gold with alpha
unsigned int PreviewConfig::Colors::SlotDividerColor = 0xC88B5A2B;     // Bronze

// Skyrim theme colors
unsigned int PreviewConfig::Colors::SkyrimSlotUnhoveredInner = 0xC8A08C6E;  // IM_COL32(160, 140, 110, 200)
unsigned int PreviewConfig::Colors::SkyrimSlotUnhoveredOuter = 0xB4786446;  // IM_COL32(120, 100, 70, 180)
unsigned int PreviewConfig::Colors::SkyrimSlotHoveredInner = 0xE6D2B48C;    // IM_COL32(210, 180, 140, 230)
unsigned int PreviewConfig::Colors::SkyrimSlotHoveredOuter = 0xD2B4966E;    // IM_COL32(180, 150, 110, 210)
unsigned int PreviewConfig::Colors::SkyrimBorderGold = 0xFFDAA520;          // IM_COL32(218, 165, 32, 255)
unsigned int PreviewConfig::Colors::SkyrimBorderBronze = 0xFF8B5A2B;        // IM_COL32(139, 90, 43, 255)

// Status variables - MUST be defined here
bool PreviewConfig::ConfigLoaded = false;
std::string PreviewConfig::LastLoadPath;
std::string PreviewConfig::LastError;


namespace {
    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, last - first + 1);
    }

    // Simple INI parser
    void parseIniFile(const std::string& path, std::function<void(const std::string&, const std::string&, const std::string&)> callback) {
        std::ifstream file(path);
        if (!file.is_open()) {
            spdlog::warn("Could not open INI file: {}", path);
            PreviewConfig::LastError = "Could not open: " + path;
            return;
        }
        
        spdlog::info("Successfully opened INI file: {}", path);
        std::string line;
        std::string currentSection;
        int lineCount = 0;
        int keyCount = 0;
        
        while (std::getline(file, line)) {
            lineCount++;
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            
            if (line[0] == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                spdlog::debug("Section: [{}]", currentSection);
            } else {
                size_t eq = line.find('=');
                if (eq != std::string::npos) {
                    std::string key = trim(line.substr(0, eq));
                    std::string value = trim(line.substr(eq + 1));
                    keyCount++;
                    callback(currentSection, key, value);
                }
            }
        }
        spdlog::info("Parsed {} lines, {} keys from {}", lineCount, keyCount, path);
    }
    
    unsigned int parseColor(const std::string& value) {
        try {
            // Handle 0xAARRGGBB format
            return static_cast<unsigned int>(std::stoul(value, nullptr, 0));
        } catch (...) {
            spdlog::warn("Failed to parse color: {}", value);
            return 0xFFFFFFFF;
        }
    }
}

namespace PreviewConfig {
    void Load(const std::string& dataRoot) {
        namespace fs = std::filesystem;
        fs::path root(dataRoot);
        LastLoadPath = dataRoot;
        LastError.clear();
        ConfigLoaded = false;  // Reset before loading
        
        // Smart path detection: if user entered path including "wheeler" folder, strip it back
        // Check if the path ends with common suffixes and fix
        std::string pathStr = root.string();
        
        // Check for common path mistakes and auto-correct
        if (pathStr.find("SKSE\\Plugins\\wheeler") != std::string::npos || 
            pathStr.find("SKSE/Plugins/wheeler") != std::string::npos) {
            // Path includes too much - strip back to Data folder
            size_t pos = pathStr.find("SKSE");
            if (pos != std::string::npos && pos > 0) {
                pathStr = pathStr.substr(0, pos);
                // Remove trailing slash
                while (!pathStr.empty() && (pathStr.back() == '/' || pathStr.back() == '\\')) {
                    pathStr.pop_back();
                }
                root = fs::path(pathStr);
                spdlog::warn("Path auto-corrected to: {}", root.string());
            }
        }
        
        // Also check if AmmoWheel.ini exists directly in the root (meaning user gave wheeler folder)
        if (fs::exists(root / "AmmoWheel.ini")) {
            // User gave the wheeler folder directly, go up 3 levels
            root = root.parent_path().parent_path().parent_path();
            spdlog::warn("Detected AmmoWheel.ini in root, auto-corrected to: {}", root.string());
        }
        
        // AmmoWheel.ini is in SKSE/Plugins/wheeler/
        auto iniPath = root / "SKSE" / "Plugins" / "wheeler" / "AmmoWheel.ini";
        // Styles.ini is in SKSE/Plugins/wheeler/resources/ammo_wheel/
        auto stylesPath = root / "SKSE" / "Plugins" / "wheeler" / "resources" / "ammo_wheel" / "Styles.ini";
        
        spdlog::info("Looking for AmmoWheel.ini at: {}", iniPath.string());
        spdlog::info("Looking for Styles.ini at: {}", stylesPath.string());

        
        // Load AmmoWheel.ini
        if (fs::exists(iniPath)) {
            spdlog::info("Found AmmoWheel.ini");
            parseIniFile(iniPath.string(), [](const std::string& section, const std::string& key, const std::string& value) {
                try {
                    if (section == "Position") {
                        if (key == "WheelRadius") {
                            AmmoWheel::WheelRadius = std::stof(value);
                            spdlog::info("  Loaded WheelRadius = {}", AmmoWheel::WheelRadius);
                        }
                    }
                    if (section == "Geometry") {
                        if (key == "InnerRadiusRatio") {
                            AmmoWheel::InnerRadiusRatio = std::stof(value);
                            spdlog::info("  Loaded InnerRadiusRatio = {}", AmmoWheel::InnerRadiusRatio);
                        }
                        if (key == "SlotGapDeg") {
                            AmmoWheel::SlotGapDeg = std::stof(value);
                            spdlog::info("  Loaded SlotGapDeg = {}", AmmoWheel::SlotGapDeg);
                        }
                    }
                    if (section == "Appearance") {
                        if (key == "WheelShape") {
                            AmmoWheel::WheelShape = std::stoi(value);
                            // Set sweep angle based on shape
                            if (AmmoWheel::WheelShape == 0) AmmoWheel::ArcSweepAngle = 360.0f;
                            else if (AmmoWheel::WheelShape == 1) AmmoWheel::ArcSweepAngle = 180.0f;
                            else if (AmmoWheel::WheelShape == 2) AmmoWheel::ArcSweepAngle = 90.0f;
                            spdlog::info("  Loaded WheelShape = {} (sweep={})", AmmoWheel::WheelShape, AmmoWheel::ArcSweepAngle);
                        }
                        if (key == "ArcStartAngle") {
                            AmmoWheel::ArcStartAngle = std::stof(value);
                            spdlog::info("  Loaded ArcStartAngle = {}", AmmoWheel::ArcStartAngle);
                        }
                        // Slot Shape settings (NOW IN-GAME!)
                        if (key == "SlotShape") {
                            AmmoWheel::SlotShape = std::stoi(value);
                            spdlog::info("  Loaded SlotShape = {}", AmmoWheel::SlotShape);
                        }
                        if (key == "SlotCornerRadius") {
                            AmmoWheel::SlotCornerRadius = std::stof(value);
                            spdlog::info("  Loaded SlotCornerRadius = {}", AmmoWheel::SlotCornerRadius);
                        }
                        if (key == "SlotShapeScale") {
                            AmmoWheel::SlotShapeScale = std::stof(value);
                            spdlog::info("  Loaded SlotShapeScale = {}", AmmoWheel::SlotShapeScale);
                        }
                    }
                    // Visual Polish settings
                    if (section == "VisualPolish") {
                        if (key == "BackgroundEnabled") AmmoWheel::BackgroundEnabled = (value == "true" || value == "1");
                        if (key == "BackgroundOpacity") AmmoWheel::BackgroundOpacity = std::stof(value);
                        if (key == "BackgroundRadiusScale") AmmoWheel::BackgroundRadiusScale = std::stof(value);
                        if (key == "BorderEnabled") AmmoWheel::BorderEnabled = (value == "true" || value == "1");
                        if (key == "BorderInnerScale") AmmoWheel::BorderInnerScale = std::stof(value);
                        if (key == "BorderOuterScale") AmmoWheel::BorderOuterScale = std::stof(value);
                        if (key == "SlotShadowEnabled") AmmoWheel::SlotShadowEnabled = (value == "true" || value == "1");
                        if (key == "SlotShadowOffsetX") AmmoWheel::SlotShadowOffsetX = std::stof(value);
                        if (key == "SlotShadowOffsetY") AmmoWheel::SlotShadowOffsetY = std::stof(value);
                        if (key == "SlotShadowAlpha") AmmoWheel::SlotShadowAlpha = std::stoi(value);
                        if (key == "SlotHighlightEnabled") AmmoWheel::SlotHighlightEnabled = (value == "true" || value == "1");
                        if (key == "SlotHighlightThickness") AmmoWheel::SlotHighlightThickness = std::stof(value);
                        if (key == "SlotHighlightAlpha") AmmoWheel::SlotHighlightAlpha = std::stoi(value);
                    }
                    // Animation settings
                    if (section == "Animations") {
                        if (key == "HoverPulseEnabled") AmmoWheel::HoverPulseEnabled = (value == "true" || value == "1");
                        if (key == "HoverPulseSpeed") AmmoWheel::HoverPulseSpeed = std::stof(value);
                        if (key == "HoverPulseSize") AmmoWheel::HoverPulseSize = std::stof(value);
                        if (key == "SlotDividersEnabled") AmmoWheel::SlotDividersEnabled = (value == "true" || value == "1");
                        if (key == "SlotDividerThickness") AmmoWheel::SlotDividerThickness = std::stof(value);
                    }
                    // Theme settings
                    if (section == "Theme") {
                        if (key == "UseSkyrimTheme") AmmoWheel::UseSkyrimTheme = (value == "true" || value == "1");
                    }
                    // Popup settings - matches main game [Popup] section
                    if (section == "Popup") {
                        // dMenu style keys (PopupXxx)
                        if (key == "PopupEnabled") AmmoWheel::PopupEnabled = (value == "true" || value == "1");
                        if (key == "PopupIconSizePx") AmmoWheel::PopupIconSizePx = std::stof(value);
                        if (key == "PopupNameFontPx") AmmoWheel::PopupNameFontPx = std::stof(value);
                        if (key == "PopupCountFontPx") AmmoWheel::PopupCountFontPx = std::stof(value);
                        if (key == "PopupOffsetPx") AmmoWheel::PopupOffsetPx = std::stof(value);
                        if (key == "PopupPaddingPx") AmmoWheel::PopupPaddingPx = std::stof(value);
                        // Legacy keys (without Popup prefix)
                        if (key == "Enabled") AmmoWheel::PopupEnabled = (value == "true" || value == "1");
                        if (key == "IconSizePx") AmmoWheel::PopupIconSizePx = std::stof(value);
                        if (key == "NameFontPx") AmmoWheel::PopupNameFontPx = std::stof(value);
                        if (key == "CountFontPx") AmmoWheel::PopupCountFontPx = std::stof(value);
                        if (key == "OffsetPx") AmmoWheel::PopupOffsetPx = std::stof(value);
                        if (key == "PaddingPx") AmmoWheel::PopupPaddingPx = std::stof(value);
                        if (key == "Circular") AmmoWheel::PopupCircular = (value == "true" || value == "1");
                        if (key == "BubbleRadius") AmmoWheel::PopupBubbleRadius = std::stof(value);
                        if (key == "AnimationSpeed") AmmoWheel::PopupAnimationSpeed = std::stof(value);
                    }
                    // Popup Animation settings - matches main game [Popup.Animation] section
                    if (section == "Popup.Animation" || section == "PopupAnim") {
                        if (key == "Enabled") AmmoWheel::PopupAnimEnabled = (value == "true" || value == "1");
                        if (key == "HoverInMs") AmmoWheel::PopupAnimHoverInMs = std::stof(value);
                        if (key == "HoverOutMs") AmmoWheel::PopupAnimHoverOutMs = std::stof(value);
                        if (key == "ScaleFrom") AmmoWheel::PopupAnimScaleFrom = std::stof(value);
                        if (key == "ScaleTo") AmmoWheel::PopupAnimScaleTo = std::stof(value);
                        if (key == "Easing") AmmoWheel::PopupAnimEasing = std::stoi(value);
                        if (key == "BorderThickness") AmmoWheel::PopupAnimBorderThickness = std::stof(value);
                        if (key == "BorderOpacity") AmmoWheel::PopupAnimBorderOpacity = std::stof(value);
                        if (key == "BackgroundOpacity") AmmoWheel::PopupAnimBackgroundOpacity = std::stof(value);
                    }
                    // Display settings - matches main game [Display] section
                    if (section == "Display") {
                        if (key == "ShowAmmoCount") AmmoWheel::ShowAmmoCount = (value == "true" || value == "1");
                        if (key == "CountFontSize") AmmoWheel::CountFontSize = std::stof(value);
                        if (key == "ShowIcons") AmmoWheel::ShowIcons = (value == "true" || value == "1");
                        if (key == "IconHoverGlow") AmmoWheel::IconHoverGlow = (value == "true" || value == "1");
                    }
                    // Geometry settings - matches main game [Geometry] section
                    if (section == "Geometry") {
                        if (key == "IconSize") AmmoWheel::IconSize = std::stof(value);
                        if (key == "IconRadiusRatio") AmmoWheel::IconRadiusRatio = std::stof(value);
                    }
                    // Labels settings - matches main game [Labels] section
                    if (section == "Labels") {
                        if (key == "LabelShow") AmmoWheel::LabelShow = (value == "true" || value == "1");
                        if (key == "Show") AmmoWheel::LabelShow = (value == "true" || value == "1");  // Legacy
                        if (key == "LabelTruncateLength") AmmoWheel::LabelTruncateLength = std::stoi(value);
                        if (key == "TruncateLength") AmmoWheel::LabelTruncateLength = std::stoi(value);  // Legacy
                        if (key == "LabelAbbreviate") AmmoWheel::LabelAbbreviate = (value == "true" || value == "1");
                        if (key == "Abbreviate") AmmoWheel::LabelAbbreviate = (value == "true" || value == "1");  // Legacy
                        if (key == "MultiLine") AmmoWheel::LabelMultiLine = (value == "true" || value == "1");
                        if (key == "MaxSlotArcRatio") AmmoWheel::LabelMaxSlotArcRatio = std::stof(value);
                    }
                    // Text settings
                    if (section == "Text") {
                        if (key == "NameFontPx") AmmoWheel::NameFontPx = std::stof(value);
                        if (key == "CountFontPx") AmmoWheel::CountFontPx = std::stof(value);
                        if (key == "NameTextScale") AmmoWheel::NameTextScale = std::stof(value);
                        if (key == "ShadowEnabled") AmmoWheel::TextShadowEnabled = (value == "true" || value == "1");
                        if (key == "ShadowLayers") AmmoWheel::TextShadowLayers = std::stoi(value);
                        if (key == "ShadowAlpha") AmmoWheel::TextShadowAlpha = std::stoi(value);
                        if (key == "ShadowOffset") AmmoWheel::TextShadowOffset = std::stof(value);
                        if (key == "HoverGlowEnabled") AmmoWheel::TextHoverGlowEnabled = (value == "true" || value == "1");
                    }
                    // Center Panel settings - matches main game [CenterPanel] section
                    if (section == "CenterPanel") {
                        if (key == "Enabled") AmmoWheel::CenterEnabled = (value == "true" || value == "1");
                        if (key == "BgEnabled") AmmoWheel::CenterBgEnabled = (value == "true" || value == "1");
                        if (key == "BgOpacity") AmmoWheel::CenterBgOpacity = std::stof(value);
                        if (key == "PaddingPx") AmmoWheel::CenterPaddingPx = std::stof(value);
                        if (key == "MaxWidthRatio") AmmoWheel::CenterMaxWidthRatio = std::stof(value);
                        if (key == "LineSpacingPx") AmmoWheel::CenterLineSpacingPx = std::stof(value);
                        if (key == "FontPx") AmmoWheel::CenterFontPx = std::stof(value);
                        // Main game uses ShapeType, preview uses ShapeIndex
                        if (key == "ShapeType") AmmoWheel::CenterPanelShapeIndex = std::stoi(value);
                        if (key == "ShapeIndex") AmmoWheel::CenterPanelShapeIndex = std::stoi(value);
                        if (key == "CornerRounding") AmmoWheel::CenterPanelCornerRounding = std::stof(value);
                        if (key == "BorderThickness") AmmoWheel::CenterPanelBorderThickness = std::stof(value);
                        if (key == "BorderAlpha") AmmoWheel::CenterPanelBorderAlpha = std::stof(value);
                        if (key == "FrameEnabled") AmmoWheel::CenterFrameEnabled = (value == "true" || value == "1");
                        if (key == "FramePulse") AmmoWheel::CenterFramePulse = (value == "true" || value == "1");
                        if (key == "FramePulseSpeed") AmmoWheel::CenterFramePulseSpeed = std::stof(value);
                    }
                    // Indicator settings
                    if (section == "Indicators") {
                        if (key == "LowAmmoEnabled") AmmoWheel::LowAmmoIndicatorEnabled = (value == "true" || value == "1");
                        if (key == "LowAmmoThreshold") AmmoWheel::LowAmmoThreshold = std::stoi(value);
                        if (key == "LowAmmoThickness") AmmoWheel::LowAmmoIndicatorThickness = std::stof(value);
                        if (key == "SelectedThickness") AmmoWheel::SelectedIndicatorThickness = std::stof(value);
                    }
                } catch (const std::exception& e) {
                    spdlog::warn("Failed to parse [{}] {} = {}: {}", section, key, value, e.what());
                }
            });
            ConfigLoaded = true;
        } else {
            spdlog::error("AmmoWheel.ini NOT FOUND at {}", iniPath.string());
            LastError = "AmmoWheel.ini not found at: " + iniPath.string();
        }
        
        // Load Styles.ini for colors
        if (fs::exists(stylesPath)) {
            spdlog::info("Found Styles.ini");
            parseIniFile(stylesPath.string(), [](const std::string& section, const std::string& key, const std::string& value) {
                if (section == "AmmoWheel.Slot") {
                    if (key == "UnhoveredColorBegin") {
                        Colors::UnhoveredColorBegin = parseColor(value);
                        spdlog::info("  Loaded UnhoveredColorBegin = 0x{:08X}", Colors::UnhoveredColorBegin);
                    }
                    if (key == "UnhoveredColorEnd") {
                        Colors::UnhoveredColorEnd = parseColor(value);
                    }
                    if (key == "HoveredColorBegin") {
                        Colors::HoveredColorBegin = parseColor(value);
                    }
                    if (key == "HoveredColorEnd") {
                        Colors::HoveredColorEnd = parseColor(value);
                    }
                    if (key == "SelectedColorBegin") {
                        Colors::SelectedColorBegin = parseColor(value);
                    }
                    if (key == "SelectedColorEnd") {
                        Colors::SelectedColorEnd = parseColor(value);
                    }
                    if (key == "BackgroundOpacity") {
                        AmmoWheel::BackgroundOpacity = std::stof(value);
                    }
                }
            });
        } else {
            spdlog::warn("Styles.ini NOT FOUND at {}", stylesPath.string());
        }
        
        spdlog::info("=== Config Summary ===");
        spdlog::info("  WheelRadius: {}", AmmoWheel::WheelRadius);
        spdlog::info("  InnerRadiusRatio: {}", AmmoWheel::InnerRadiusRatio);
        spdlog::info("  SlotGapDeg: {}", AmmoWheel::SlotGapDeg);
        spdlog::info("  ArcStartAngle: {}", AmmoWheel::ArcStartAngle);
        spdlog::info("  ArcSweepAngle: {}", AmmoWheel::ArcSweepAngle);
        spdlog::info("  WheelShape: {}", AmmoWheel::WheelShape);
        spdlog::info("  UnhoveredColorBegin: 0x{:08X}", Colors::UnhoveredColorBegin);
    }
    
    // Helper to update a key in an INI file content
    std::string updateIniKey(const std::string& content, const std::string& section, const std::string& key, const std::string& newValue) {
        std::string result;
        std::istringstream stream(content);
        std::string line;
        std::string currentSection;
        bool keyUpdated = false;
        
        while (std::getline(stream, line)) {
            std::string trimmedLine = line;
            // Trim
            size_t first = trimmedLine.find_first_not_of(" \t\r\n");
            if (first != std::string::npos) {
                size_t last = trimmedLine.find_last_not_of(" \t\r\n");
                trimmedLine = trimmedLine.substr(first, last - first + 1);
            } else {
                trimmedLine = "";
            }
            
            // Check for section header
            if (!trimmedLine.empty() && trimmedLine[0] == '[' && trimmedLine.back() == ']') {
                currentSection = trimmedLine.substr(1, trimmedLine.size() - 2);
            }
            
            // Check for key to update
            if (currentSection == section && !trimmedLine.empty() && trimmedLine[0] != ';' && trimmedLine[0] != '#') {
                size_t eq = trimmedLine.find('=');
                if (eq != std::string::npos) {
                    std::string currentKey = trimmedLine.substr(0, eq);
                    // Trim key
                    first = currentKey.find_first_not_of(" \t");
                    if (first != std::string::npos) {
                        size_t last = currentKey.find_last_not_of(" \t");
                        currentKey = currentKey.substr(first, last - first + 1);
                    }
                    
                    if (currentKey == key) {
                        result += key + " = " + newValue + "\n";
                        keyUpdated = true;
                        continue;
                    }
                }
            }
            
            result += line + "\n";
        }
        
        return result;
    }
    
    bool SaveAmmoWheelIni(const std::string& dataRoot) {
        namespace fs = std::filesystem;
        fs::path root(dataRoot);
        
        // Handle path auto-correction like in Load
        std::string pathStr = root.string();
        if (pathStr.find("SKSE\\Plugins\\wheeler") != std::string::npos || 
            pathStr.find("SKSE/Plugins/wheeler") != std::string::npos) {
            size_t pos = pathStr.find("SKSE");
            if (pos != std::string::npos && pos > 0) {
                pathStr = pathStr.substr(0, pos);
                while (!pathStr.empty() && (pathStr.back() == '/' || pathStr.back() == '\\')) {
                    pathStr.pop_back();
                }
                root = fs::path(pathStr);
            }
        }
        if (fs::exists(root / "AmmoWheel.ini")) {
            root = root.parent_path().parent_path().parent_path();
        }
        
        auto iniPath = root / "SKSE" / "Plugins" / "wheeler" / "AmmoWheel.ini";
        
        if (!fs::exists(iniPath)) {
            spdlog::error("Cannot save: AmmoWheel.ini not found at {}", iniPath.string());
            return false;
        }
        
        // Read existing file
        std::ifstream inFile(iniPath);
        std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
        inFile.close();
        
        // Update values
        char buf[64];
        
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::WheelRadius);
        content = updateIniKey(content, "Position", "WheelRadius", buf);
        
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::InnerRadiusRatio);
        content = updateIniKey(content, "Geometry", "InnerRadiusRatio", buf);
        
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::SlotGapDeg);
        content = updateIniKey(content, "Geometry", "SlotGapDeg", buf);
        
        snprintf(buf, sizeof(buf), "%d", AmmoWheel::WheelShape);
        content = updateIniKey(content, "Appearance", "WheelShape", buf);
        
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::ArcStartAngle);
        content = updateIniKey(content, "Appearance", "ArcStartAngle", buf);
        
        // Slot Shape settings (NOW IN-GAME!)
        snprintf(buf, sizeof(buf), "%d", AmmoWheel::SlotShape);
        content = updateIniKey(content, "Appearance", "SlotShape", buf);
        
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::SlotCornerRadius);
        content = updateIniKey(content, "Appearance", "SlotCornerRadius", buf);
        
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::SlotShapeScale);
        content = updateIniKey(content, "Appearance", "SlotShapeScale", buf);
        
        // ========== VISUAL POLISH ==========
        content = updateIniKey(content, "VisualPolish", "BackgroundEnabled", AmmoWheel::BackgroundEnabled ? "true" : "false");
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::BackgroundOpacity);
        content = updateIniKey(content, "VisualPolish", "BackgroundOpacity", buf);
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::BackgroundRadiusScale);
        content = updateIniKey(content, "VisualPolish", "BackgroundRadiusScale", buf);
        
        content = updateIniKey(content, "VisualPolish", "BorderEnabled", AmmoWheel::BorderEnabled ? "true" : "false");
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::BorderInnerScale);
        content = updateIniKey(content, "VisualPolish", "BorderInnerScale", buf);
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::BorderOuterScale);
        content = updateIniKey(content, "VisualPolish", "BorderOuterScale", buf);
        
        content = updateIniKey(content, "VisualPolish", "SlotShadowEnabled", AmmoWheel::SlotShadowEnabled ? "true" : "false");
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::SlotShadowOffsetX);
        content = updateIniKey(content, "VisualPolish", "SlotShadowOffsetX", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::SlotShadowOffsetY);
        content = updateIniKey(content, "VisualPolish", "SlotShadowOffsetY", buf);
        snprintf(buf, sizeof(buf), "%d", AmmoWheel::SlotShadowAlpha);
        content = updateIniKey(content, "VisualPolish", "SlotShadowAlpha", buf);
        
        content = updateIniKey(content, "VisualPolish", "SlotHighlightEnabled", AmmoWheel::SlotHighlightEnabled ? "true" : "false");
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::SlotHighlightThickness);
        content = updateIniKey(content, "VisualPolish", "SlotHighlightThickness", buf);
        snprintf(buf, sizeof(buf), "%d", AmmoWheel::SlotHighlightAlpha);
        content = updateIniKey(content, "VisualPolish", "SlotHighlightAlpha", buf);
        
        content = updateIniKey(content, "VisualPolish", "TextShadowEnabled", AmmoWheel::TextShadowEnabled ? "true" : "false");
        snprintf(buf, sizeof(buf), "%d", AmmoWheel::TextShadowLayers);
        content = updateIniKey(content, "VisualPolish", "TextShadowLayers", buf);
        snprintf(buf, sizeof(buf), "%d", AmmoWheel::TextShadowAlpha);
        content = updateIniKey(content, "VisualPolish", "TextShadowAlpha", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::TextShadowOffset);
        content = updateIniKey(content, "VisualPolish", "TextShadowOffset", buf);
        content = updateIniKey(content, "VisualPolish", "TextHoverGlowEnabled", AmmoWheel::TextHoverGlowEnabled ? "true" : "false");
        
        // ========== ANIMATIONS ==========
        content = updateIniKey(content, "Animations", "HoverPulseEnabled", AmmoWheel::HoverPulseEnabled ? "true" : "false");
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::HoverPulseSpeed);
        content = updateIniKey(content, "Animations", "HoverPulseSpeed", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::HoverPulseSize);
        content = updateIniKey(content, "Animations", "HoverPulseSize", buf);
        
        content = updateIniKey(content, "Animations", "SlotDividersEnabled", AmmoWheel::SlotDividersEnabled ? "true" : "false");
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::SlotDividerThickness);
        content = updateIniKey(content, "Animations", "SlotDividerThickness", buf);
        
        content = updateIniKey(content, "Animations", "CenterFrameEnabled", AmmoWheel::CenterFrameEnabled ? "true" : "false");
        content = updateIniKey(content, "Animations", "CenterFramePulse", AmmoWheel::CenterFramePulse ? "true" : "false");
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::CenterFramePulseSpeed);
        content = updateIniKey(content, "Animations", "CenterFramePulseSpeed", buf);
        
        // ========== THEME ==========
        content = updateIniKey(content, "Theme", "UseSkyrimTheme", AmmoWheel::UseSkyrimTheme ? "true" : "false");
        
        // ========== POPUP SETTINGS ==========
        content = updateIniKey(content, "Popup", "PopupEnabled", AmmoWheel::PopupEnabled ? "true" : "false");
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::PopupIconSizePx);
        content = updateIniKey(content, "Popup", "PopupIconSizePx", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::PopupNameFontPx);
        content = updateIniKey(content, "Popup", "PopupNameFontPx", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::PopupCountFontPx);
        content = updateIniKey(content, "Popup", "PopupCountFontPx", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::PopupOffsetPx);
        content = updateIniKey(content, "Popup", "PopupOffsetPx", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::PopupPaddingPx);
        content = updateIniKey(content, "Popup", "PopupPaddingPx", buf);
        content = updateIniKey(content, "Popup", "Circular", AmmoWheel::PopupCircular ? "true" : "false");
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::PopupBubbleRadius);
        content = updateIniKey(content, "Popup", "BubbleRadius", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::PopupAnimationSpeed);
        content = updateIniKey(content, "Popup", "AnimationSpeed", buf);
        
        // ========== POPUP ANIMATION ==========
        content = updateIniKey(content, "Popup.Animation", "Enabled", AmmoWheel::PopupAnimEnabled ? "true" : "false");
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::PopupAnimHoverInMs);
        content = updateIniKey(content, "Popup.Animation", "HoverInMs", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::PopupAnimHoverOutMs);
        content = updateIniKey(content, "Popup.Animation", "HoverOutMs", buf);
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::PopupAnimScaleFrom);
        content = updateIniKey(content, "Popup.Animation", "ScaleFrom", buf);
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::PopupAnimScaleTo);
        content = updateIniKey(content, "Popup.Animation", "ScaleTo", buf);
        snprintf(buf, sizeof(buf), "%d", AmmoWheel::PopupAnimEasing);
        content = updateIniKey(content, "Popup.Animation", "Easing", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::PopupAnimBorderThickness);
        content = updateIniKey(content, "Popup.Animation", "BorderThickness", buf);
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::PopupAnimBorderOpacity);
        content = updateIniKey(content, "Popup.Animation", "BorderOpacity", buf);
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::PopupAnimBackgroundOpacity);
        content = updateIniKey(content, "Popup.Animation", "BackgroundOpacity", buf);
        
        // ========== DISPLAY SETTINGS ==========
        content = updateIniKey(content, "Display", "ShowAmmoCount", AmmoWheel::ShowAmmoCount ? "true" : "false");
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::CountFontSize);
        content = updateIniKey(content, "Display", "CountFontSize", buf);
        content = updateIniKey(content, "Display", "ShowIcons", AmmoWheel::ShowIcons ? "true" : "false");
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::IconSize);
        content = updateIniKey(content, "Display", "IconSize", buf);
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::IconRadiusRatio);
        content = updateIniKey(content, "Display", "IconRadiusRatio", buf);
        content = updateIniKey(content, "Display", "IconHoverGlow", AmmoWheel::IconHoverGlow ? "true" : "false");
        
        // ========== LABELS ==========
        content = updateIniKey(content, "Labels", "LabelShow", AmmoWheel::LabelShow ? "true" : "false");
        snprintf(buf, sizeof(buf), "%d", AmmoWheel::LabelTruncateLength);
        content = updateIniKey(content, "Labels", "LabelTruncateLength", buf);
        content = updateIniKey(content, "Labels", "LabelAbbreviate", AmmoWheel::LabelAbbreviate ? "true" : "false");
        content = updateIniKey(content, "Labels", "MultiLine", AmmoWheel::LabelMultiLine ? "true" : "false");
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::LabelMaxSlotArcRatio);
        content = updateIniKey(content, "Labels", "MaxSlotArcRatio", buf);
        
        // ========== TEXT SETTINGS ==========
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::NameFontPx);
        content = updateIniKey(content, "Text", "NameFontPx", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::CountFontPx);
        content = updateIniKey(content, "Text", "CountFontPx", buf);
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::NameTextScale);
        content = updateIniKey(content, "Text", "NameTextScale", buf);
        
        // ========== CENTER PANEL ==========
        content = updateIniKey(content, "Center", "Enabled", AmmoWheel::CenterEnabled ? "true" : "false");
        content = updateIniKey(content, "Center", "BgEnabled", AmmoWheel::CenterBgEnabled ? "true" : "false");
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::CenterBgOpacity);
        content = updateIniKey(content, "Center", "BgOpacity", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::CenterPaddingPx);
        content = updateIniKey(content, "Center", "PaddingPx", buf);
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::CenterMaxWidthRatio);
        content = updateIniKey(content, "Center", "MaxWidthRatio", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::CenterLineSpacingPx);
        content = updateIniKey(content, "Center", "LineSpacingPx", buf);
        
        // CenterPanel shape settings
        snprintf(buf, sizeof(buf), "%d", AmmoWheel::CenterPanelShapeIndex);
        content = updateIniKey(content, "CenterPanel", "ShapeType", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::CenterPanelCornerRounding);
        content = updateIniKey(content, "CenterPanel", "CornerRounding", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::CenterPanelBorderThickness);
        content = updateIniKey(content, "CenterPanel", "BorderThickness", buf);
        snprintf(buf, sizeof(buf), "%.2f", AmmoWheel::CenterPanelBorderAlpha);
        content = updateIniKey(content, "CenterPanel", "BorderAlpha", buf);
        
        // ========== INDICATORS ==========
        content = updateIniKey(content, "Indicators", "LowAmmoIndicatorEnabled", AmmoWheel::LowAmmoIndicatorEnabled ? "true" : "false");
        snprintf(buf, sizeof(buf), "%d", AmmoWheel::LowAmmoThreshold);
        content = updateIniKey(content, "Indicators", "LowAmmoThreshold", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::LowAmmoIndicatorThickness);
        content = updateIniKey(content, "Indicators", "LowAmmoIndicatorThickness", buf);
        snprintf(buf, sizeof(buf), "%.1f", AmmoWheel::SelectedIndicatorThickness);
        content = updateIniKey(content, "Indicators", "SelectedIndicatorThickness", buf);
        
        // Write back
        std::ofstream outFile(iniPath);
        if (!outFile.is_open()) {
            spdlog::error("Failed to open {} for writing", iniPath.string());
            return false;
        }
        outFile << content;
        outFile.close();
        
        spdlog::info("Saved AmmoWheel.ini to {}", iniPath.string());
        return true;
    }
    
    bool SaveStylesIni(const std::string& dataRoot) {
        namespace fs = std::filesystem;
        fs::path root(dataRoot);
        
        // Handle path auto-correction
        std::string pathStr = root.string();
        if (pathStr.find("SKSE\\Plugins\\wheeler") != std::string::npos || 
            pathStr.find("SKSE/Plugins/wheeler") != std::string::npos) {
            size_t pos = pathStr.find("SKSE");
            if (pos != std::string::npos && pos > 0) {
                pathStr = pathStr.substr(0, pos);
                while (!pathStr.empty() && (pathStr.back() == '/' || pathStr.back() == '\\')) {
                    pathStr.pop_back();
                }
                root = fs::path(pathStr);
            }
        }
        if (fs::exists(root / "AmmoWheel.ini")) {
            root = root.parent_path().parent_path().parent_path();
        }
        
        auto stylesPath = root / "SKSE" / "Plugins" / "wheeler" / "resources" / "ammo_wheel" / "Styles.ini";
        
        if (!fs::exists(stylesPath)) {
            spdlog::error("Cannot save: Styles.ini not found at {}", stylesPath.string());
            return false;
        }
        
        // Read existing file
        std::ifstream inFile(stylesPath);
        std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
        inFile.close();
        
        // Update color values
        char buf[32];
        
        snprintf(buf, sizeof(buf), "0x%08X", Colors::UnhoveredColorBegin);
        content = updateIniKey(content, "AmmoWheel.Slot", "UnhoveredColorBegin", buf);
        
        snprintf(buf, sizeof(buf), "0x%08X", Colors::UnhoveredColorEnd);
        content = updateIniKey(content, "AmmoWheel.Slot", "UnhoveredColorEnd", buf);
        
        snprintf(buf, sizeof(buf), "0x%08X", Colors::HoveredColorBegin);
        content = updateIniKey(content, "AmmoWheel.Slot", "HoveredColorBegin", buf);
        
        snprintf(buf, sizeof(buf), "0x%08X", Colors::HoveredColorEnd);
        content = updateIniKey(content, "AmmoWheel.Slot", "HoveredColorEnd", buf);
        
        snprintf(buf, sizeof(buf), "0x%08X", Colors::SelectedColorBegin);
        content = updateIniKey(content, "AmmoWheel.Slot", "SelectedColorBegin", buf);
        
        snprintf(buf, sizeof(buf), "0x%08X", Colors::SelectedColorEnd);
        content = updateIniKey(content, "AmmoWheel.Slot", "SelectedColorEnd", buf);
        
        // Write back
        std::ofstream outFile(stylesPath);
        if (!outFile.is_open()) {
            spdlog::error("Failed to open {} for writing", stylesPath.string());
            return false;
        }
        outFile << content;
        outFile.close();
        
        spdlog::info("Saved Styles.ini to {}", stylesPath.string());
        return true;
    }
}

