#pragma once
#include <string>

namespace PreviewConfig {
    // AmmoWheel-specific settings
    struct AmmoWheel {
        // Position & Geometry
        static float WheelRadius;
        static float InnerRadiusRatio;
        static float SlotGapDeg;
        static float ArcStartAngle;      // Degrees (180 = left)
        static float ArcSweepAngle;      // Degrees (180 = half circle)
        static int WheelShape;           // 0=Full, 1=Half, 2=Quarter
        
        // Slot Shape (NOW FULLY SUPPORTED IN-GAME!)
        static int SlotShape;            // 0=Arc, 1=RoundedRect, 2=Pill, 3=Circle
        static float SlotCornerRadius;   // Corner radius for RoundedRect shape
        static float SlotShapeScale;     // Scale factor for non-arc shapes (0.5-1.2)
        
        // Visual Polish - Background
        static bool BackgroundEnabled;
        static float BackgroundOpacity;
        static float BackgroundRadiusScale;
        
        // Visual Polish - Border (arc-only)
        static bool BorderEnabled;
        static float BorderInnerScale;
        static float BorderOuterScale;
        
        // Visual Polish - Slot Shadow
        static bool SlotShadowEnabled;
        static float SlotShadowOffsetX;
        static float SlotShadowOffsetY;
        static int SlotShadowAlpha;
        
        // Visual Polish - Slot Highlight
        static bool SlotHighlightEnabled;
        static float SlotHighlightThickness;
        static int SlotHighlightAlpha;
        
        // Animation - Hover Pulse
        static bool HoverPulseEnabled;
        static float HoverPulseSpeed;
        static float HoverPulseSize;
        
        // Animation - Slot Dividers (arc-only)
        static bool SlotDividersEnabled;
        static float SlotDividerThickness;
        
        // Theme
        static bool UseSkyrimTheme;
        
        // ========== POPUP SETTINGS ==========
        static bool PopupEnabled;
        static float PopupIconSizePx;
        static float PopupNameFontPx;
        static float PopupCountFontPx;
        static float PopupOffsetPx;
        static float PopupPaddingPx;
        static bool PopupCircular;
        static float PopupBubbleRadius;
        static float PopupAnimationSpeed;
        
        // Popup Animation
        static bool PopupAnimEnabled;
        static float PopupAnimHoverInMs;
        static float PopupAnimHoverOutMs;
        static float PopupAnimScaleFrom;
        static float PopupAnimScaleTo;
        static int PopupAnimEasing;          // 0=Linear, 1=OutCubic, 2=OutBack
        static float PopupAnimBorderThickness;
        static float PopupAnimBorderOpacity;
        static float PopupAnimBackgroundOpacity;
        
        // ========== DISPLAY SETTINGS ==========
        static bool ShowAmmoCount;
        static float CountFontSize;
        static bool ShowIcons;
        static float IconSize;
        static float IconRadiusRatio;
        static bool IconHoverGlow;
        
        // ========== LABEL SETTINGS ==========
        static bool LabelShow;
        static int LabelTruncateLength;
        static bool LabelAbbreviate;
        static bool LabelMultiLine;
        static float LabelMaxSlotArcRatio;
        
        // ========== TEXT SETTINGS ==========
        static float NameFontPx;
        static float CountFontPx;
        static float NameTextScale;
        static bool TextShadowEnabled;
        static int TextShadowLayers;
        static int TextShadowAlpha;
        static float TextShadowOffset;
        static bool TextHoverGlowEnabled;
        
        // ========== CENTER PANEL ==========
        static bool CenterEnabled;
        static bool CenterBgEnabled;
        static float CenterBgOpacity;
        static float CenterPaddingPx;
        static float CenterMaxWidthRatio;
        static float CenterLineSpacingPx;
        static float CenterFontPx;
        static int CenterPanelShapeIndex;    // 0=Auto, 1=Rectangle, 2=Circle, 3=RoundedRect
        static float CenterPanelCornerRounding;
        static float CenterPanelBorderThickness;
        static float CenterPanelBorderAlpha;
        static bool CenterFrameEnabled;
        static bool CenterFramePulse;
        static float CenterFramePulseSpeed;
        
        // ========== LOW AMMO INDICATOR ==========
        static bool LowAmmoIndicatorEnabled;
        static int LowAmmoThreshold;
        static float LowAmmoIndicatorThickness;
        static float SelectedIndicatorThickness;
    };
    
    struct Colors {
        // Slot colors
        static unsigned int UnhoveredColorBegin;
        static unsigned int UnhoveredColorEnd;
        static unsigned int HoveredColorBegin;
        static unsigned int HoveredColorEnd;
        static unsigned int SelectedColorBegin;
        static unsigned int SelectedColorEnd;
        static unsigned int TextColor;
        
        // Border colors
        static unsigned int BorderColorInner;
        static unsigned int BorderColorOuter;
        
        // Effect colors
        static unsigned int HoverPulseColor;
        static unsigned int SlotDividerColor;
        
        // Skyrim theme colors
        static unsigned int SkyrimSlotUnhoveredInner;
        static unsigned int SkyrimSlotUnhoveredOuter;
        static unsigned int SkyrimSlotHoveredInner;
        static unsigned int SkyrimSlotHoveredOuter;
        static unsigned int SkyrimBorderGold;
        static unsigned int SkyrimBorderBronze;
    };
    
    // Status - defined in cpp
    extern bool ConfigLoaded;
    extern std::string LastLoadPath;
    extern std::string LastError;
    
    // Load/Save functions
    void Load(const std::string& dataRoot);
    bool SaveAmmoWheelIni(const std::string& dataRoot);
    bool SaveStylesIni(const std::string& dataRoot);
}


