#pragma once
#include <imgui.h>

namespace PreviewDrawer {
    void DrawWheel(ImDrawList* drawList, ImVec2 center, int numSlots = 8, int hoveredIndex = -1);
    void DrawArc(ImDrawList* drawList, ImVec2 center, float radius_min, float radius_max, 
                 float ang_min, float ang_max, ImU32 colorInner, ImU32 colorOuter, int segments);
}
