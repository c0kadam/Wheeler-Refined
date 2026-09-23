#include "PreviewDrawer.h"
#include "PreviewConfig.h"
#include <cmath>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <imgui_internal.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
    float degToRad(float deg) {
        return deg * static_cast<float>(M_PI) / 180.0f;
    }
    
    // Get current time for animations
    float getTime() {
        static auto start = std::chrono::high_resolution_clock::now();
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<float>(now - start).count();
    }
}

namespace PreviewDrawer {

    void DrawArc(ImDrawList* drawList, ImVec2 center, float radius_min, float radius_max, 
                 float ang_min, float ang_max, ImU32 colorInner, ImU32 colorOuter, int segments) {
        if (segments < 1) segments = 1;
        const float angleStep = (ang_max - ang_min) / segments;
        const ImVec2& uvWhite = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
        
        drawList->PrimReserve(segments * 6, (segments + 1) * 2);
        for (int i = 0; i <= segments; ++i) {
            float angle = ang_min + angleStep * i;
            float cosA = cosf(angle);
            float sinA = sinf(angle);
            
            if (i < segments) {
                drawList->PrimWriteIdx(drawList->_VtxCurrentIdx + 0);
                drawList->PrimWriteIdx(drawList->_VtxCurrentIdx + 2);
                drawList->PrimWriteIdx(drawList->_VtxCurrentIdx + 1);
                drawList->PrimWriteIdx(drawList->_VtxCurrentIdx + 3);
                drawList->PrimWriteIdx(drawList->_VtxCurrentIdx + 2);
                drawList->PrimWriteIdx(drawList->_VtxCurrentIdx + 1);
            }

            drawList->PrimWriteVtx(ImVec2(center.x + cosA * radius_min, center.y + sinA * radius_min), uvWhite, colorInner);
            drawList->PrimWriteVtx(ImVec2(center.x + cosA * radius_max, center.y + sinA * radius_max), uvWhite, colorOuter);
        }
    }

    void DrawWheel(ImDrawList* drawList, ImVec2 center, int numSlots, int hoveredIndex) {
        using namespace PreviewConfig;
        
        // Get config values
        float radiusOuter = AmmoWheel::WheelRadius;
        float radiusInner = radiusOuter * AmmoWheel::InnerRadiusRatio;
        float slotGapRad = degToRad(AmmoWheel::SlotGapDeg);
        
        // Arc parameters - AmmoWheel uses partial arcs
        float arcStartRad = degToRad(AmmoWheel::ArcStartAngle);
        float arcSweepRad = degToRad(AmmoWheel::ArcSweepAngle);
        
        // Calculate slot angles
        float totalGap = slotGapRad * numSlots;
        float availableAngle = arcSweepRad - totalGap;
        float slotAngle = availableAngle / numSlots;
        
        // ========== BACKGROUND ==========
        if (AmmoWheel::BackgroundEnabled) {
            float bgRadius = radiusOuter * AmmoWheel::BackgroundRadiusScale;
            ImU32 bgColor = IM_COL32(0, 0, 0, static_cast<int>(AmmoWheel::BackgroundOpacity * 255));
            
            if (AmmoWheel::UseSkyrimTheme) {
                // Layered Skyrim background
                drawList->PathClear();
                drawList->PathArcTo(center, bgRadius * 1.05f, arcStartRad, arcStartRad + arcSweepRad, 32);
                drawList->PathArcTo(center, radiusInner * 0.9f, arcStartRad + arcSweepRad, arcStartRad, 32);
                drawList->PathFillConvex(IM_COL32(70, 50, 30, 100));  // Outer glow
                
                drawList->PathClear();
                drawList->PathArcTo(center, bgRadius, arcStartRad, arcStartRad + arcSweepRad, 32);
                drawList->PathArcTo(center, radiusInner * 0.95f, arcStartRad + arcSweepRad, arcStartRad, 32);
                drawList->PathFillConvex(IM_COL32(45, 35, 25, 200));  // Mid layer
                
                drawList->PathClear();
                drawList->PathArcTo(center, bgRadius * 0.95f, arcStartRad, arcStartRad + arcSweepRad, 32);
                drawList->PathArcTo(center, radiusInner, arcStartRad + arcSweepRad, arcStartRad, 32);
                drawList->PathFillConvex(IM_COL32(20, 15, 10, 220));  // Dark inner
            } else {
                drawList->PathClear();
                drawList->PathArcTo(center, bgRadius, arcStartRad, arcStartRad + arcSweepRad, 32);
                drawList->PathArcTo(center, radiusInner - 5, arcStartRad + arcSweepRad, arcStartRad, 32);
                drawList->PathFillConvex(bgColor);
            }
        }
        
        // ========== DECORATIVE BORDER RING (arc-only) ==========
        if (AmmoWheel::BorderEnabled && AmmoWheel::SlotShape == 0) {
            float borderInner = radiusOuter * AmmoWheel::BorderInnerScale;
            float borderOuter = radiusOuter * AmmoWheel::BorderOuterScale;
            ImU32 borderInnerColor = AmmoWheel::UseSkyrimTheme ? Colors::SkyrimBorderGold : Colors::BorderColorInner;
            ImU32 borderOuterColor = AmmoWheel::UseSkyrimTheme ? Colors::SkyrimBorderBronze : Colors::BorderColorOuter;
            DrawArc(drawList, center, borderInner, borderOuter, arcStartRad, arcStartRad + arcSweepRad, borderInnerColor, borderOuterColor, 32);
        }
        
        // ========== DRAW SLOTS ==========
        for (int i = 0; i < numSlots; ++i) {
            float slotStart = arcStartRad + i * (slotAngle + slotGapRad) + slotGapRad * 0.5f;
            float slotEnd = slotStart + slotAngle;
            float slotMidAngle = (slotStart + slotEnd) / 2.0f;
            float slotMidRadius = (radiusInner + radiusOuter) / 2.0f;
            bool isHovered = (i == hoveredIndex);
            
            // Determine colors based on theme and hover state
            ImU32 colorInner, colorOuter;
            if (AmmoWheel::UseSkyrimTheme) {
                colorInner = isHovered ? Colors::SkyrimSlotHoveredInner : Colors::SkyrimSlotUnhoveredInner;
                colorOuter = isHovered ? Colors::SkyrimSlotHoveredOuter : Colors::SkyrimSlotUnhoveredOuter;
            } else {
                colorInner = isHovered ? Colors::HoveredColorBegin : Colors::UnhoveredColorBegin;
                colorOuter = isHovered ? Colors::HoveredColorEnd : Colors::UnhoveredColorEnd;
            }
            
            // Calculate solid color for non-arc shapes.
            // NOTE: ImU32 is stored as ImGui's internal packed format (ABGR). Avoid manual bit shifts.
            ImVec4 c1 = ImGui::ColorConvertU32ToFloat4(colorInner);
            ImVec4 c2 = ImGui::ColorConvertU32ToFloat4(colorOuter);
            ImVec4 c = ImVec4(
                (c1.x + c2.x) * 0.5f,
                (c1.y + c2.y) * 0.5f,
                (c1.z + c2.z) * 0.5f,
                (c1.w + c2.w) * 0.5f
            );
            ImU32 solidColor = ImGui::ColorConvertFloat4ToU32(c);
            
            // Common calculations for non-arc shapes
            float slotWidth = (radiusOuter - radiusInner) * AmmoWheel::SlotShapeScale;
            float slotArcLength = slotMidRadius * slotAngle;
            float shapeCenterX = center.x + cosf(slotMidAngle) * slotMidRadius;
            float shapeCenterY = center.y + sinf(slotMidAngle) * slotMidRadius;
            
            // Calculate icon position using IconRadiusRatio
            float iconRadius = radiusInner + (radiusOuter - radiusInner) * AmmoWheel::IconRadiusRatio;
            float iconCenterX = center.x + cosf(slotMidAngle) * iconRadius;
            float iconCenterY = center.y + sinf(slotMidAngle) * iconRadius;
            
            switch (AmmoWheel::SlotShape) {
                case 0: // Arc (default)
                {
                    // Shadow
                    if (AmmoWheel::SlotShadowEnabled) {
                        ImVec2 shadowCenter(center.x + AmmoWheel::SlotShadowOffsetX, center.y + AmmoWheel::SlotShadowOffsetY);
                        ImU32 shadowColor = IM_COL32(0, 0, 0, AmmoWheel::SlotShadowAlpha);
                        DrawArc(drawList, shadowCenter, radiusInner - 1, radiusOuter + 1, slotStart, slotEnd, shadowColor, shadowColor, 16);
                    }
                    
                    // Background
                    DrawArc(drawList, center, radiusInner, radiusOuter, slotStart, slotEnd, colorInner, colorOuter, 16);
                    
                    // Highlight
                    if (isHovered && AmmoWheel::SlotHighlightEnabled) {
                        ImU32 highlightColor = IM_COL32(255, 255, 255, AmmoWheel::SlotHighlightAlpha);
                        DrawArc(drawList, center, radiusInner, radiusInner + AmmoWheel::SlotHighlightThickness, slotStart, slotEnd, highlightColor, highlightColor, 16);
                    }
                    
                    // Hover pulse
                    if (isHovered && AmmoWheel::HoverPulseEnabled) {
                        float pulseTime = getTime() * AmmoWheel::HoverPulseSpeed;
                        float pulseFactor = 0.5f + 0.5f * sinf(pulseTime);
                        float pulseSize = AmmoWheel::HoverPulseSize * pulseFactor;
                        int pulseAlpha = static_cast<int>(120 * pulseFactor);
                        ImU32 pulseColor = IM_COL32(255, 215, 0, pulseAlpha);
                        DrawArc(drawList, center, radiusOuter, radiusOuter + pulseSize, slotStart, slotEnd, pulseColor, pulseColor, 16);
                    }
                    
                    // Selected indicator (arc shape)
                    if (i == 0 && AmmoWheel::LowAmmoIndicatorEnabled) {
                        ImU32 selectedColor = IM_COL32(50, 205, 50, 255);
                        DrawArc(drawList, center, radiusOuter + 2, radiusOuter + 2 + AmmoWheel::SelectedIndicatorThickness, 
                            slotStart, slotEnd, selectedColor, selectedColor, 16);
                    }
                }
                break;
                    
                case 1: // Rounded Rectangle
                {
                    float rectWidth = slotArcLength * 0.85f;
                    float rectHeight = slotWidth;
                    float cornerRadius = AmmoWheel::SlotCornerRadius;
                    
                    // Shadow
                    if (AmmoWheel::SlotShadowEnabled) {
                        float sx = shapeCenterX + AmmoWheel::SlotShadowOffsetX;
                        float sy = shapeCenterY + AmmoWheel::SlotShadowOffsetY;
                        drawList->AddRectFilled(ImVec2(sx - rectWidth/2, sy - rectHeight/2), ImVec2(sx + rectWidth/2, sy + rectHeight/2),
                            IM_COL32(0, 0, 0, AmmoWheel::SlotShadowAlpha), cornerRadius);
                    }
                    
                    // Background
                    drawList->AddRectFilled(ImVec2(shapeCenterX - rectWidth/2, shapeCenterY - rectHeight/2),
                        ImVec2(shapeCenterX + rectWidth/2, shapeCenterY + rectHeight/2), solidColor, cornerRadius);
                    
                    // Highlight
                    if (isHovered && AmmoWheel::SlotHighlightEnabled) {
                        drawList->AddRect(ImVec2(shapeCenterX - rectWidth/2, shapeCenterY - rectHeight/2),
                            ImVec2(shapeCenterX + rectWidth/2, shapeCenterY + rectHeight/2),
                            IM_COL32(255, 255, 255, AmmoWheel::SlotHighlightAlpha), cornerRadius, 0, AmmoWheel::SlotHighlightThickness);
                    }
                    
                    // Hover pulse
                    if (isHovered && AmmoWheel::HoverPulseEnabled) {
                        float pulseTime = getTime() * AmmoWheel::HoverPulseSpeed;
                        float pulseFactor = 0.5f + 0.5f * sinf(pulseTime);
                        float pulseExpand = AmmoWheel::HoverPulseSize * pulseFactor;
                        int pulseAlpha = static_cast<int>(120 * pulseFactor);
                        drawList->AddRect(ImVec2(shapeCenterX - rectWidth/2 - pulseExpand, shapeCenterY - rectHeight/2 - pulseExpand),
                            ImVec2(shapeCenterX + rectWidth/2 + pulseExpand, shapeCenterY + rectHeight/2 + pulseExpand),
                            IM_COL32(255, 215, 0, pulseAlpha), cornerRadius + pulseExpand * 0.5f, 0, 2.0f);
                    }
                }
                break;
                    
                case 2: // Pill (Capsule)
                {
                    float pillLength = slotArcLength * 0.8f;
                    float pillRadius = slotWidth / 2.0f;
                    
                    // Shadow
                    if (AmmoWheel::SlotShadowEnabled) {
                        float sx = shapeCenterX + AmmoWheel::SlotShadowOffsetX;
                        float sy = shapeCenterY + AmmoWheel::SlotShadowOffsetY;
                        drawList->AddRectFilled(ImVec2(sx - pillLength/2, sy - pillRadius), ImVec2(sx + pillLength/2, sy + pillRadius),
                            IM_COL32(0, 0, 0, AmmoWheel::SlotShadowAlpha), pillRadius);
                    }
                    
                    // Background
                    drawList->AddRectFilled(ImVec2(shapeCenterX - pillLength/2, shapeCenterY - pillRadius),
                        ImVec2(shapeCenterX + pillLength/2, shapeCenterY + pillRadius), solidColor, pillRadius);
                    
                    // Highlight
                    if (isHovered && AmmoWheel::SlotHighlightEnabled) {
                        drawList->AddRect(ImVec2(shapeCenterX - pillLength/2, shapeCenterY - pillRadius),
                            ImVec2(shapeCenterX + pillLength/2, shapeCenterY + pillRadius),
                            IM_COL32(255, 255, 255, AmmoWheel::SlotHighlightAlpha), pillRadius, 0, AmmoWheel::SlotHighlightThickness);
                    }
                    
                    // Hover pulse
                    if (isHovered && AmmoWheel::HoverPulseEnabled) {
                        float pulseTime = getTime() * AmmoWheel::HoverPulseSpeed;
                        float pulseFactor = 0.5f + 0.5f * sinf(pulseTime);
                        float pulseExpand = AmmoWheel::HoverPulseSize * pulseFactor;
                        int pulseAlpha = static_cast<int>(120 * pulseFactor);
                        drawList->AddRect(ImVec2(shapeCenterX - pillLength/2 - pulseExpand, shapeCenterY - pillRadius - pulseExpand),
                            ImVec2(shapeCenterX + pillLength/2 + pulseExpand, shapeCenterY + pillRadius + pulseExpand),
                            IM_COL32(255, 215, 0, pulseAlpha), pillRadius + pulseExpand, 0, 2.0f);
                    }
                }
                break;
                    
                case 3: // Circle
                {
                    float circleRadius = (std::min)(slotWidth, slotArcLength * 0.5f) * 0.85f;
                    
                    // Shadow
                    if (AmmoWheel::SlotShadowEnabled) {
                        float sx = shapeCenterX + AmmoWheel::SlotShadowOffsetX;
                        float sy = shapeCenterY + AmmoWheel::SlotShadowOffsetY;
                        drawList->AddCircleFilled(ImVec2(sx, sy), circleRadius + 1.0f, IM_COL32(0, 0, 0, AmmoWheel::SlotShadowAlpha), 24);
                    }
                    
                    // Background
                    drawList->AddCircleFilled(ImVec2(shapeCenterX, shapeCenterY), circleRadius, solidColor, 24);
                    
                    // Highlight
                    if (isHovered && AmmoWheel::SlotHighlightEnabled) {
                        drawList->AddCircle(ImVec2(shapeCenterX, shapeCenterY), circleRadius,
                            IM_COL32(255, 255, 255, AmmoWheel::SlotHighlightAlpha), 24, AmmoWheel::SlotHighlightThickness);
                    }
                    
                    // Hover pulse
                    if (isHovered && AmmoWheel::HoverPulseEnabled) {
                        float pulseTime = getTime() * AmmoWheel::HoverPulseSpeed;
                        float pulseFactor = 0.5f + 0.5f * sinf(pulseTime);
                        float pulseExpand = AmmoWheel::HoverPulseSize * pulseFactor;
                        int pulseAlpha = static_cast<int>(120 * pulseFactor);
                        drawList->AddCircle(ImVec2(shapeCenterX, shapeCenterY), circleRadius + pulseExpand,
                            IM_COL32(255, 215, 0, pulseAlpha), 24, 2.0f);
                    }
                }
                break;
                    
                default:
                    DrawArc(drawList, center, radiusInner, radiusOuter, slotStart, slotEnd, colorInner, colorOuter, 16);
                    break;
            }
            
            // ========== ICON RENDERING ==========
            if (AmmoWheel::ShowIcons) {
                float iconSize = AmmoWheel::IconSize * 0.5f;
                
                // Icon hover glow
                if (isHovered && AmmoWheel::IconHoverGlow) {
                    ImU32 glowColor = IM_COL32(255, 215, 0, 80);
                    drawList->AddCircleFilled(ImVec2(iconCenterX, iconCenterY), iconSize + 4, glowColor, 16);
                }
                
                // Icon placeholder (circle representing icon)
                ImU32 iconColor = isHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 200);
                drawList->AddCircleFilled(ImVec2(iconCenterX, iconCenterY), iconSize * 0.6f, iconColor, 16);
                
                // Arrow icon shape (triangle)
                float arrowSize = iconSize * 0.4f;
                ImVec2 p1(iconCenterX, iconCenterY - arrowSize);
                ImVec2 p2(iconCenterX - arrowSize * 0.5f, iconCenterY + arrowSize * 0.5f);
                ImVec2 p3(iconCenterX + arrowSize * 0.5f, iconCenterY + arrowSize * 0.5f);
                drawList->AddTriangleFilled(p1, p2, p3, IM_COL32(60, 60, 60, 200));
            }
            
            // ========== AMMO COUNT TEXT ==========
            if (AmmoWheel::ShowAmmoCount) {
                char countText[16];
                snprintf(countText, sizeof(countText), "x%d", 50 + i * 10);
                ImVec2 countSize = ImGui::CalcTextSize(countText);
                float countX = iconCenterX - countSize.x * 0.5f;
                float countY = iconCenterY + AmmoWheel::IconSize * 0.35f;
                
                // Text shadow
                if (AmmoWheel::TextShadowEnabled) {
                    ImU32 shadowColor = IM_COL32(0, 0, 0, AmmoWheel::TextShadowAlpha);
                    drawList->AddText(ImVec2(countX + AmmoWheel::TextShadowOffset, countY + AmmoWheel::TextShadowOffset), 
                        shadowColor, countText);
                }
                
                ImU32 textColor = IM_COL32(255, 255, 255, 255);
                drawList->AddText(ImVec2(countX, countY), textColor, countText);
            }
        }
        
        // ========== SLOT DIVIDERS (arc-only) ==========
        if (AmmoWheel::SlotDividersEnabled && AmmoWheel::SlotShape == 0 && numSlots > 1) {
            ImU32 dividerColor = Colors::SlotDividerColor;
            for (int i = 0; i <= numSlots; ++i) {
                float dividerAngle = arcStartRad + i * (slotAngle + slotGapRad);
                ImVec2 innerPt(center.x + radiusInner * 0.95f * cosf(dividerAngle), center.y + radiusInner * 0.95f * sinf(dividerAngle));
                ImVec2 outerPt(center.x + radiusOuter * 1.02f * cosf(dividerAngle), center.y + radiusOuter * 1.02f * sinf(dividerAngle));
                drawList->AddLine(innerPt, outerPt, dividerColor, AmmoWheel::SlotDividerThickness);
            }
        }
        
        // ========== CENTER PANEL ==========
        if (AmmoWheel::CenterEnabled) {
            float innerRadius = radiusOuter * AmmoWheel::InnerRadiusRatio;
            float panelRadius = innerRadius * AmmoWheel::CenterMaxWidthRatio * 0.5f;
            
            // Background
            if (AmmoWheel::CenterBgEnabled) {
                ImU32 bgColor = IM_COL32(20, 15, 10, static_cast<int>(AmmoWheel::CenterBgOpacity * 255));
                
                switch (AmmoWheel::CenterPanelShapeIndex) {
                    case 1: // Rectangle
                        drawList->AddRectFilled(
                            ImVec2(center.x - panelRadius, center.y - panelRadius * 0.6f),
                            ImVec2(center.x + panelRadius, center.y + panelRadius * 0.6f),
                            bgColor);
                        break;
                    case 2: // Circle
                        drawList->AddCircleFilled(center, panelRadius * 0.7f, bgColor, 32);
                        break;
                    case 3: // RoundedRect
                        drawList->AddRectFilled(
                            ImVec2(center.x - panelRadius, center.y - panelRadius * 0.6f),
                            ImVec2(center.x + panelRadius, center.y + panelRadius * 0.6f),
                            bgColor, AmmoWheel::CenterPanelCornerRounding);
                        break;
                    default: // Auto - use circle
                        drawList->AddCircleFilled(center, panelRadius * 0.7f, bgColor, 32);
                        break;
                }
            }
            
            // Frame decoration
            if (AmmoWheel::CenterFrameEnabled) {
                float frameAlpha = AmmoWheel::CenterPanelBorderAlpha;
                if (AmmoWheel::CenterFramePulse) {
                    float pulse = 0.7f + 0.3f * sinf(getTime() * AmmoWheel::CenterFramePulseSpeed);
                    frameAlpha *= pulse;
                }
                ImU32 frameColor = IM_COL32(139, 90, 43, static_cast<int>(frameAlpha * 255));
                
                if (AmmoWheel::CenterPanelShapeIndex == 2) {
                    drawList->AddCircle(center, panelRadius * 0.7f, frameColor, 32, AmmoWheel::CenterPanelBorderThickness);
                } else {
                    drawList->AddRect(
                        ImVec2(center.x - panelRadius, center.y - panelRadius * 0.6f),
                        ImVec2(center.x + panelRadius, center.y + panelRadius * 0.6f),
                        frameColor, AmmoWheel::CenterPanelCornerRounding, 0, AmmoWheel::CenterPanelBorderThickness);
                }
            }
            
            // Center text (sample)
            char centerText[64];
            snprintf(centerText, sizeof(centerText), "Iron Arrow\nx99");
            ImVec2 textSize = ImGui::CalcTextSize(centerText);
            drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                IM_COL32(240, 230, 210, 255), centerText);
        }
        
        // ========== POPUP BUBBLE (when hovering) ==========
        if (AmmoWheel::PopupEnabled && hoveredIndex >= 0) {
            // Calculate popup position (outside wheel, in direction of hovered slot)
            float slotMidAngle = arcStartRad + hoveredIndex * (slotAngle + slotGapRad) + slotAngle * 0.5f + slotGapRad * 0.5f;
            float popupDistance = radiusOuter + AmmoWheel::PopupOffsetPx;
            float popupX = center.x + cosf(slotMidAngle) * popupDistance;
            float popupY = center.y + sinf(slotMidAngle) * popupDistance;
            
            // Animation
            static float animProgress = 0.0f;
            static int lastHovered = -1;
            static float animStartTime = 0.0f;
            
            if (hoveredIndex != lastHovered) {
                animStartTime = getTime();
                lastHovered = hoveredIndex;
            }
            
            float animDuration = AmmoWheel::PopupAnimHoverInMs / 1000.0f;
            float elapsed = getTime() - animStartTime;
            animProgress = (std::min)(elapsed / animDuration, 1.0f);
            
            // Apply easing
            float easedProgress = animProgress;
            if (AmmoWheel::PopupAnimEasing == 1) { // OutCubic
                easedProgress = 1.0f - powf(1.0f - animProgress, 3.0f);
            } else if (AmmoWheel::PopupAnimEasing == 2) { // OutBack
                float c1 = 1.70158f;
                float c3 = c1 + 1.0f;
                easedProgress = 1.0f + c3 * powf(animProgress - 1.0f, 3.0f) + c1 * powf(animProgress - 1.0f, 2.0f);
            }
            
            // Scale animation
            float scale = AmmoWheel::PopupAnimScaleFrom + (AmmoWheel::PopupAnimScaleTo - AmmoWheel::PopupAnimScaleFrom) * easedProgress;
            float alpha = easedProgress;
            
            float bubbleRadius = AmmoWheel::PopupBubbleRadius * scale;
            
            // Draw popup
            if (AmmoWheel::PopupCircular) {
                // Circular bubble
                ImU32 bgColor = IM_COL32(20, 15, 10, static_cast<int>(AmmoWheel::PopupAnimBackgroundOpacity * 255 * alpha));
                drawList->AddCircleFilled(ImVec2(popupX, popupY), bubbleRadius, bgColor, 32);
                
                // Border
                ImU32 borderColor = IM_COL32(218, 165, 32, static_cast<int>(AmmoWheel::PopupAnimBorderOpacity * 255 * alpha));
                drawList->AddCircle(ImVec2(popupX, popupY), bubbleRadius, borderColor, 32, AmmoWheel::PopupAnimBorderThickness);
            } else {
                // Rectangular popup
                float rectW = bubbleRadius * 1.5f;
                float rectH = bubbleRadius;
                ImU32 bgColor = IM_COL32(20, 15, 10, static_cast<int>(AmmoWheel::PopupAnimBackgroundOpacity * 255 * alpha));
                drawList->AddRectFilled(
                    ImVec2(popupX - rectW, popupY - rectH),
                    ImVec2(popupX + rectW, popupY + rectH),
                    bgColor, 8.0f);
                
                ImU32 borderColor = IM_COL32(218, 165, 32, static_cast<int>(AmmoWheel::PopupAnimBorderOpacity * 255 * alpha));
                drawList->AddRect(
                    ImVec2(popupX - rectW, popupY - rectH),
                    ImVec2(popupX + rectW, popupY + rectH),
                    borderColor, 8.0f, 0, AmmoWheel::PopupAnimBorderThickness);
            }
            
            // Popup content (icon placeholder + text)
            float iconSize = AmmoWheel::PopupIconSizePx * scale * 0.5f;
            ImU32 iconPlaceholder = IM_COL32(180, 160, 140, static_cast<int>(200 * alpha));
            drawList->AddCircleFilled(ImVec2(popupX, popupY - 10), iconSize * 0.4f, iconPlaceholder, 16);
            
            // Sample text
            char popupText[32];
            snprintf(popupText, sizeof(popupText), "Slot %d", hoveredIndex + 1);
            ImVec2 textSize = ImGui::CalcTextSize(popupText);
            ImU32 textColor = IM_COL32(240, 230, 210, static_cast<int>(255 * alpha));
            drawList->AddText(ImVec2(popupX - textSize.x * 0.5f, popupY + 15), textColor, popupText);
        }
        
        // ========== DEBUG INFO ==========
        static const char* shapeNames[] = { "Arc", "Rounded", "Pill", "Circle" };
        char debugText[256];
        snprintf(debugText, sizeof(debugText), "Slots: %d | Shape: %s | Popup: %s", 
                 numSlots, shapeNames[AmmoWheel::SlotShape],
                 AmmoWheel::PopupEnabled ? "ON" : "OFF");
        ImVec2 textPos(center.x - 80, center.y + radiusOuter + 20);
        drawList->AddText(textPos, IM_COL32(255, 255, 255, 180), debugText);
    }
}

