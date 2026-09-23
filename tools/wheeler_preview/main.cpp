// Wheeler Preview Tool - Win32/DX11 Version
#include <d3d11.h>
#include <tchar.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "PreviewDrawer.h"
#include "PreviewConfig.h"

// Data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Main entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Setup spdlog console
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Wheeler Preview Tool starting...");
    
    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"Wheeler Preview", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Wheeler Preview Tool - AmmoWheel", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // State
    static char dataRoot[512] = "Data"; 
    static int numSlots = 8;
    static int hoveredSlot = 0;
    
    // Auto-load on start
    PreviewConfig::Load(dataRoot);

    // Main loop
    bool done = false;
    ImVec4 clear_color = ImVec4(0.1f, 0.12f, 0.15f, 1.00f);

    while (!done)
    {
        // Poll and handle messages
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Control Panel
        {
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(380, 750), ImGuiCond_FirstUseEver);
            ImGui::Begin("AmmoWheel Settings Editor");
            
            // Data folder section
            if (ImGui::CollapsingHeader("Data Folder", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##DataRoot", dataRoot, sizeof(dataRoot));
                
                if (ImGui::Button("Reload from Disk", ImVec2(-1, 25))) {
                    spdlog::info("=== Reloading Config ===");
                    PreviewConfig::Load(dataRoot);
                }
                
                if (PreviewConfig::ConfigLoaded) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Config Loaded OK");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Config NOT Loaded");
                }
            }
            
            ImGui::Separator();
            
            // Simulation Controls
            if (ImGui::CollapsingHeader("Preview Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderInt("Slot Count", &numSlots, 1, 20);
                ImGui::SliderInt("Hovered Slot", &hoveredSlot, -1, numSlots - 1);
            }
            
            ImGui::Separator();
            
            // Geometry Settings (editable)
            if (ImGui::CollapsingHeader("In-Game Geometry Settings [SAVES]", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "These settings affect the actual plugin:");
                ImGui::SliderFloat("Wheel Radius", &PreviewConfig::AmmoWheel::WheelRadius, 50.0f, 500.0f, "%.0f");
                ImGui::SliderFloat("Inner Ratio", &PreviewConfig::AmmoWheel::InnerRadiusRatio, -10.0f, 20.0f, "%.2f");
                ImGui::SliderFloat("Slot Gap (deg)", &PreviewConfig::AmmoWheel::SlotGapDeg, 0.0f, 15.0f, "%.1f");
                ImGui::SliderFloat("Start Angle", &PreviewConfig::AmmoWheel::ArcStartAngle, 0.0f, 360.0f, "%.0f");
                
                // Wheel shape as slider (0=Full, 1=Half, 2=Quarter)
                static const char* wheelShapeNames[] = { "Full (360)", "Half (180)", "Quarter (90)" };
                if (ImGui::SliderInt("Wheel Shape", &PreviewConfig::AmmoWheel::WheelShape, 0, 2)) {
                    // Auto-update sweep angle based on shape
                    if (PreviewConfig::AmmoWheel::WheelShape == 0) PreviewConfig::AmmoWheel::ArcSweepAngle = 360.0f;
                    else if (PreviewConfig::AmmoWheel::WheelShape == 1) PreviewConfig::AmmoWheel::ArcSweepAngle = 180.0f;
                    else if (PreviewConfig::AmmoWheel::WheelShape == 2) PreviewConfig::AmmoWheel::ArcSweepAngle = 90.0f;
                }
                ImGui::SameLine();
                ImGui::Text("%s", wheelShapeNames[PreviewConfig::AmmoWheel::WheelShape]);
            }
            
            ImGui::Separator();

            // Slot Shape Settings (NOW IN-GAME!)
            if (ImGui::CollapsingHeader("Slot Shape [SAVES - NOW IN-GAME!]", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "These settings are NOW fully supported in-game!");
                
                // Slot shape (0=Arc, 1=Rounded, 2=Pill, 3=Circle)
                static const char* slotShapeNames[] = { "Arc", "RoundedRect", "Pill", "Circle" };
                ImGui::SliderInt("Slot Shape", &PreviewConfig::AmmoWheel::SlotShape, 0, 3);
                ImGui::SameLine();
                ImGui::Text("%s", slotShapeNames[PreviewConfig::AmmoWheel::SlotShape]);
                
                // Corner radius (only applicable for Rounded shape)
                if (PreviewConfig::AmmoWheel::SlotShape == 1) {
                    ImGui::SliderFloat("Corner Radius", &PreviewConfig::AmmoWheel::SlotCornerRadius, -10.0f, 20.0f, "%.0f");
                }
                
                // Shape scale (for non-arc shapes)
                if (PreviewConfig::AmmoWheel::SlotShape != 0) {
                    ImGui::SliderFloat("Shape Scale", &PreviewConfig::AmmoWheel::SlotShapeScale, 0.5f, 1.2f, "%.2f");
                }
            }
            
            ImGui::Separator();
            
            // Visual Polish Settings
            if (ImGui::CollapsingHeader("Visual Polish [SAVES]")) {
                // Theme
                ImGui::Checkbox("Use Skyrim Theme", &PreviewConfig::AmmoWheel::UseSkyrimTheme);
                
                ImGui::Separator();
                ImGui::Text("Background:");
                ImGui::Checkbox("Background Enabled", &PreviewConfig::AmmoWheel::BackgroundEnabled);
                ImGui::SliderFloat("BG Opacity", &PreviewConfig::AmmoWheel::BackgroundOpacity, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("BG Radius Scale", &PreviewConfig::AmmoWheel::BackgroundRadiusScale, -10.0f, 20.0f, "%.2f");
                
                ImGui::Separator();
                ImGui::Text("Border (Arc-only):");
                ImGui::Checkbox("Border Enabled", &PreviewConfig::AmmoWheel::BorderEnabled);
                ImGui::SliderFloat("Border Inner Scale", &PreviewConfig::AmmoWheel::BorderInnerScale, -10.0f, 20.0f, "%.2f");
                ImGui::SliderFloat("Border Outer Scale", &PreviewConfig::AmmoWheel::BorderOuterScale, -10.0f, 20.0f, "%.2f");
                
                ImGui::Separator();
                ImGui::Text("Slot Shadow:");
                ImGui::Checkbox("Shadow Enabled", &PreviewConfig::AmmoWheel::SlotShadowEnabled);
                ImGui::SliderFloat("Shadow Offset X", &PreviewConfig::AmmoWheel::SlotShadowOffsetX, 0.0f, 10.0f, "%.1f");
                ImGui::SliderFloat("Shadow Offset Y", &PreviewConfig::AmmoWheel::SlotShadowOffsetY, 0.0f, 10.0f, "%.1f");
                ImGui::SliderInt("Shadow Alpha", &PreviewConfig::AmmoWheel::SlotShadowAlpha, 0, 255);
                
                ImGui::Separator();
                ImGui::Text("Slot Highlight:");
                ImGui::Checkbox("Highlight Enabled", &PreviewConfig::AmmoWheel::SlotHighlightEnabled);
                ImGui::SliderFloat("Highlight Thickness", &PreviewConfig::AmmoWheel::SlotHighlightThickness, 0.5f, 5.0f, "%.1f");
                ImGui::SliderInt("Highlight Alpha", &PreviewConfig::AmmoWheel::SlotHighlightAlpha, 0, 255);
            }
            
            // Animation Settings
            if (ImGui::CollapsingHeader("Animations [SAVES]")) {
                ImGui::Text("Hover Pulse:");
                ImGui::Checkbox("Pulse Enabled", &PreviewConfig::AmmoWheel::HoverPulseEnabled);
                ImGui::SliderFloat("Pulse Speed", &PreviewConfig::AmmoWheel::HoverPulseSpeed, 1.0f, 10.0f, "%.1f");
                ImGui::SliderFloat("Pulse Size", &PreviewConfig::AmmoWheel::HoverPulseSize, 1.0f, 15.0f, "%.1f");
                
                ImGui::Separator();
                ImGui::Text("Slot Dividers (Arc-only):");
                ImGui::Checkbox("Dividers Enabled", &PreviewConfig::AmmoWheel::SlotDividersEnabled);
                ImGui::SliderFloat("Divider Thickness", &PreviewConfig::AmmoWheel::SlotDividerThickness, 0.5f, 5.0f, "%.1f");
            }
            
            ImGui::Separator();
            
            // Color Settings - Unhovered
            if (ImGui::CollapsingHeader("Unhovered Colors [SAVES]", ImGuiTreeNodeFlags_DefaultOpen)) {

                // Extract RGBA from packed color (format: 0xAARRGGBB or 0xAABBGGRR depending on endianness)
                // ImGui uses ABGR internally, but we store as ARGB
                static int uA1 = 128, uR1 = 160, uG1 = 144, uB1 = 125;
                static int uA2 = 64,  uR2 = 120, uG2 = 109, uB2 = 94;
                static bool colorInit = false;
                if (!colorInit) {
                    // Parse from current values
                    uA1 = (PreviewConfig::Colors::UnhoveredColorBegin >> 24) & 0xFF;
                    uR1 = (PreviewConfig::Colors::UnhoveredColorBegin >> 16) & 0xFF;
                    uG1 = (PreviewConfig::Colors::UnhoveredColorBegin >> 8) & 0xFF;
                    uB1 = (PreviewConfig::Colors::UnhoveredColorBegin) & 0xFF;
                    uA2 = (PreviewConfig::Colors::UnhoveredColorEnd >> 24) & 0xFF;
                    uR2 = (PreviewConfig::Colors::UnhoveredColorEnd >> 16) & 0xFF;
                    uG2 = (PreviewConfig::Colors::UnhoveredColorEnd >> 8) & 0xFF;
                    uB2 = (PreviewConfig::Colors::UnhoveredColorEnd) & 0xFF;
                    colorInit = true;
                }
                
                ImGui::Text("Begin (Inner):");
                if (ImGui::SliderInt("Alpha##UB", &uA1, 0, 255)) 
                    PreviewConfig::Colors::UnhoveredColorBegin = (uA1 << 24) | (uR1 << 16) | (uG1 << 8) | uB1;
                if (ImGui::SliderInt("Red##UB", &uR1, 0, 255))
                    PreviewConfig::Colors::UnhoveredColorBegin = (uA1 << 24) | (uR1 << 16) | (uG1 << 8) | uB1;
                if (ImGui::SliderInt("Green##UB", &uG1, 0, 255))
                    PreviewConfig::Colors::UnhoveredColorBegin = (uA1 << 24) | (uR1 << 16) | (uG1 << 8) | uB1;
                if (ImGui::SliderInt("Blue##UB", &uB1, 0, 255))
                    PreviewConfig::Colors::UnhoveredColorBegin = (uA1 << 24) | (uR1 << 16) | (uG1 << 8) | uB1;
                
                ImGui::Text("End (Outer):");
                if (ImGui::SliderInt("Alpha##UE", &uA2, 0, 255))
                    PreviewConfig::Colors::UnhoveredColorEnd = (uA2 << 24) | (uR2 << 16) | (uG2 << 8) | uB2;
                if (ImGui::SliderInt("Red##UE", &uR2, 0, 255))
                    PreviewConfig::Colors::UnhoveredColorEnd = (uA2 << 24) | (uR2 << 16) | (uG2 << 8) | uB2;
                if (ImGui::SliderInt("Green##UE", &uG2, 0, 255))
                    PreviewConfig::Colors::UnhoveredColorEnd = (uA2 << 24) | (uR2 << 16) | (uG2 << 8) | uB2;
                if (ImGui::SliderInt("Blue##UE", &uB2, 0, 255))
                    PreviewConfig::Colors::UnhoveredColorEnd = (uA2 << 24) | (uR2 << 16) | (uG2 << 8) | uB2;
                
                // Preview color box
                ImVec4 previewColor1 = ImVec4(uR1/255.0f, uG1/255.0f, uB1/255.0f, uA1/255.0f);
                ImVec4 previewColor2 = ImVec4(uR2/255.0f, uG2/255.0f, uB2/255.0f, uA2/255.0f);
                ImGui::ColorButton("##PreviewUB", previewColor1, 0, ImVec2(40, 20));
                ImGui::SameLine();
                ImGui::Text("->");
                ImGui::SameLine();
                ImGui::ColorButton("##PreviewUE", previewColor2, 0, ImVec2(40, 20));
            }
            
            // Color Settings - Hovered
            if (ImGui::CollapsingHeader("Hovered Slot Colors")) {
                static int hA1 = 255, hR1 = 212, hG1 = 196, hB1 = 168;
                static int hA2 = 255, hR2 = 181, hG2 = 164, hB2 = 141;
                static bool hColorInit = false;
                if (!hColorInit) {
                    hA1 = (PreviewConfig::Colors::HoveredColorBegin >> 24) & 0xFF;
                    hR1 = (PreviewConfig::Colors::HoveredColorBegin >> 16) & 0xFF;
                    hG1 = (PreviewConfig::Colors::HoveredColorBegin >> 8) & 0xFF;
                    hB1 = (PreviewConfig::Colors::HoveredColorBegin) & 0xFF;
                    hA2 = (PreviewConfig::Colors::HoveredColorEnd >> 24) & 0xFF;
                    hR2 = (PreviewConfig::Colors::HoveredColorEnd >> 16) & 0xFF;
                    hG2 = (PreviewConfig::Colors::HoveredColorEnd >> 8) & 0xFF;
                    hB2 = (PreviewConfig::Colors::HoveredColorEnd) & 0xFF;
                    hColorInit = true;
                }
                
                ImGui::Text("Begin (Inner):");
                if (ImGui::SliderInt("Alpha##HB", &hA1, 0, 255))
                    PreviewConfig::Colors::HoveredColorBegin = (hA1 << 24) | (hR1 << 16) | (hG1 << 8) | hB1;
                if (ImGui::SliderInt("Red##HB", &hR1, 0, 255))
                    PreviewConfig::Colors::HoveredColorBegin = (hA1 << 24) | (hR1 << 16) | (hG1 << 8) | hB1;
                if (ImGui::SliderInt("Green##HB", &hG1, 0, 255))
                    PreviewConfig::Colors::HoveredColorBegin = (hA1 << 24) | (hR1 << 16) | (hG1 << 8) | hB1;
                if (ImGui::SliderInt("Blue##HB", &hB1, 0, 255))
                    PreviewConfig::Colors::HoveredColorBegin = (hA1 << 24) | (hR1 << 16) | (hG1 << 8) | hB1;
                
                ImGui::Text("End (Outer):");
                if (ImGui::SliderInt("Alpha##HE", &hA2, 0, 255))
                    PreviewConfig::Colors::HoveredColorEnd = (hA2 << 24) | (hR2 << 16) | (hG2 << 8) | hB2;
                if (ImGui::SliderInt("Red##HE", &hR2, 0, 255))
                    PreviewConfig::Colors::HoveredColorEnd = (hA2 << 24) | (hR2 << 16) | (hG2 << 8) | hB2;
                if (ImGui::SliderInt("Green##HE", &hG2, 0, 255))
                    PreviewConfig::Colors::HoveredColorEnd = (hA2 << 24) | (hR2 << 16) | (hG2 << 8) | hB2;
                if (ImGui::SliderInt("Blue##HE", &hB2, 0, 255))
                    PreviewConfig::Colors::HoveredColorEnd = (hA2 << 24) | (hR2 << 16) | (hG2 << 8) | hB2;
                    
                ImVec4 previewH1 = ImVec4(hR1/255.0f, hG1/255.0f, hB1/255.0f, hA1/255.0f);
                ImVec4 previewH2 = ImVec4(hR2/255.0f, hG2/255.0f, hB2/255.0f, hA2/255.0f);
                ImGui::ColorButton("##PreviewHB", previewH1, 0, ImVec2(40, 20));
                ImGui::SameLine();
                ImGui::Text("->");
                ImGui::SameLine();
                ImGui::ColorButton("##PreviewHE", previewH2, 0, ImVec2(40, 20));
            }
            
            ImGui::Separator();
            
            // Popup Settings
            if (ImGui::CollapsingHeader("Popup Settings [SAVES]")) {
                ImGui::Checkbox("Popup Enabled", &PreviewConfig::AmmoWheel::PopupEnabled);
                ImGui::Checkbox("Circular Bubble", &PreviewConfig::AmmoWheel::PopupCircular);
                ImGui::SliderFloat("Bubble Radius", &PreviewConfig::AmmoWheel::PopupBubbleRadius, 40.0f, 150.0f, "%.0f px");
                ImGui::SliderFloat("Offset from Wheel", &PreviewConfig::AmmoWheel::PopupOffsetPx, 20.0f, 150.0f, "%.0f px");
                ImGui::SliderFloat("Padding", &PreviewConfig::AmmoWheel::PopupPaddingPx, 5.0f, 30.0f, "%.0f px");
                
                ImGui::Separator();
                ImGui::Text("Popup Fonts:");
                ImGui::SliderFloat("Popup Icon Size", &PreviewConfig::AmmoWheel::PopupIconSizePx, 32.0f, 200.0f, "%.0f px");
                ImGui::SliderFloat("Name Font", &PreviewConfig::AmmoWheel::PopupNameFontPx, 16.0f, 48.0f, "%.0f px");
                ImGui::SliderFloat("Count Font", &PreviewConfig::AmmoWheel::PopupCountFontPx, 12.0f, 36.0f, "%.0f px");
            }
            
            // Popup Animation Settings
            if (ImGui::CollapsingHeader("Popup Animation [SAVES]")) {
                ImGui::Checkbox("Animation Enabled", &PreviewConfig::AmmoWheel::PopupAnimEnabled);
                ImGui::SliderFloat("Hover In (ms)", &PreviewConfig::AmmoWheel::PopupAnimHoverInMs, 50.0f, 300.0f, "%.0f");
                ImGui::SliderFloat("Hover Out (ms)", &PreviewConfig::AmmoWheel::PopupAnimHoverOutMs, 30.0f, 200.0f, "%.0f");
                ImGui::SliderFloat("Scale From", &PreviewConfig::AmmoWheel::PopupAnimScaleFrom, 0.5f, 1.0f, "%.2f");
                ImGui::SliderFloat("Scale To", &PreviewConfig::AmmoWheel::PopupAnimScaleTo, 0.8f, 1.2f, "%.2f");
                
                static const char* easingNames[] = { "Linear", "OutCubic", "OutBack (Overshoot)" };
                ImGui::Combo("Easing", &PreviewConfig::AmmoWheel::PopupAnimEasing, easingNames, 3);
                
                ImGui::Separator();
                ImGui::Text("Border & Background:");
                ImGui::SliderFloat("Border Thickness", &PreviewConfig::AmmoWheel::PopupAnimBorderThickness, 0.5f, 5.0f, "%.1f");
                ImGui::SliderFloat("Border Opacity", &PreviewConfig::AmmoWheel::PopupAnimBorderOpacity, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Background Opacity", &PreviewConfig::AmmoWheel::PopupAnimBackgroundOpacity, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Animation Speed", &PreviewConfig::AmmoWheel::PopupAnimationSpeed, 1.0f, 20.0f, "%.1f");
            }
            
            // Display Settings
            if (ImGui::CollapsingHeader("Display Settings [SAVES]")) {
                ImGui::Text("Ammo Count:");
                ImGui::Checkbox("Show Ammo Count", &PreviewConfig::AmmoWheel::ShowAmmoCount);
                ImGui::SliderFloat("Count Font Size", &PreviewConfig::AmmoWheel::CountFontSize, 10.0f, 32.0f, "%.0f px");
                
                ImGui::Separator();
                ImGui::Text("Icons:");
                ImGui::Checkbox("Show Icons", &PreviewConfig::AmmoWheel::ShowIcons);
                ImGui::SliderFloat("Icon Size", &PreviewConfig::AmmoWheel::IconSize, 24.0f, 200.0f, "%.0f px");
                ImGui::SliderFloat("Icon Radius Ratio", &PreviewConfig::AmmoWheel::IconRadiusRatio, -10.0f, 20.0f, "%.2f");
                ImGui::Checkbox("Icon Hover Glow", &PreviewConfig::AmmoWheel::IconHoverGlow);
            }
            
            // Label Settings
            if (ImGui::CollapsingHeader("Label Settings [SAVES]")) {
                ImGui::Checkbox("Show Labels", &PreviewConfig::AmmoWheel::LabelShow);
                ImGui::SliderInt("Truncate Length", &PreviewConfig::AmmoWheel::LabelTruncateLength, 5, 30);
                ImGui::Checkbox("Use Abbreviations", &PreviewConfig::AmmoWheel::LabelAbbreviate);
                ImGui::Checkbox("Multi-Line Labels", &PreviewConfig::AmmoWheel::LabelMultiLine);
                ImGui::SliderFloat("Max Arc Ratio", &PreviewConfig::AmmoWheel::LabelMaxSlotArcRatio, -10.0f, 20.0f, "%.2f");
            }
            
            // Text Settings
            if (ImGui::CollapsingHeader("Text Settings [SAVES]")) {
                ImGui::SliderFloat("Name Font (px)", &PreviewConfig::AmmoWheel::NameFontPx, 10.0f, 48.0f, "%.0f");
                ImGui::SliderFloat("Count Font (px)", &PreviewConfig::AmmoWheel::CountFontPx, 10.0f, 36.0f, "%.0f");
                ImGui::SliderFloat("Name Text Scale", &PreviewConfig::AmmoWheel::NameTextScale, 0.5f, 2.0f, "%.2f");
                
                ImGui::Separator();
                ImGui::Text("Text Shadow:");
                ImGui::Checkbox("Shadow Enabled", &PreviewConfig::AmmoWheel::TextShadowEnabled);
                ImGui::SliderInt("Shadow Layers", &PreviewConfig::AmmoWheel::TextShadowLayers, 1, 3);
                ImGui::SliderInt("Shadow Alpha", &PreviewConfig::AmmoWheel::TextShadowAlpha, 0, 255);
                ImGui::SliderFloat("Shadow Offset", &PreviewConfig::AmmoWheel::TextShadowOffset, 0.5f, 4.0f, "%.1f");
                ImGui::Checkbox("Hover Glow", &PreviewConfig::AmmoWheel::TextHoverGlowEnabled);
            }
            
            // Center Panel Settings
            if (ImGui::CollapsingHeader("Center Panel [SAVES]")) {
                ImGui::Checkbox("Center Panel Enabled", &PreviewConfig::AmmoWheel::CenterEnabled);
                ImGui::Checkbox("Background Enabled##CP", &PreviewConfig::AmmoWheel::CenterBgEnabled);
                ImGui::SliderFloat("BG Opacity##CP", &PreviewConfig::AmmoWheel::CenterBgOpacity, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Padding (px)", &PreviewConfig::AmmoWheel::CenterPaddingPx, 0.0f, 40.0f, "%.0f");
                ImGui::SliderFloat("Max Width Ratio", &PreviewConfig::AmmoWheel::CenterMaxWidthRatio, -10.0f, 20.0f, "%.2f");
                ImGui::SliderFloat("Line Spacing", &PreviewConfig::AmmoWheel::CenterLineSpacingPx, -10.0f, 20.0f, "%.0f");
                ImGui::SliderFloat("Font Size##CP", &PreviewConfig::AmmoWheel::CenterFontPx, 10.0f, 36.0f, "%.0f");
                
                ImGui::Separator();
                ImGui::Text("Panel Shape:");
                static const char* shapeNames[] = { "Auto", "Rectangle", "Circle", "RoundedRect" };
                ImGui::Combo("Shape##CP", &PreviewConfig::AmmoWheel::CenterPanelShapeIndex, shapeNames, 4);
                if (PreviewConfig::AmmoWheel::CenterPanelShapeIndex == 3) {
                    ImGui::SliderFloat("Corner Rounding##CP", &PreviewConfig::AmmoWheel::CenterPanelCornerRounding, 0.0f, 30.0f, "%.0f");
                }
                ImGui::SliderFloat("Border Thickness##CP", &PreviewConfig::AmmoWheel::CenterPanelBorderThickness, -10.0f, 20.0f, "%.1f");
                ImGui::SliderFloat("Border Alpha##CP", &PreviewConfig::AmmoWheel::CenterPanelBorderAlpha, -10.0f, 20.0f, "%.2f");
                
                ImGui::Separator();
                ImGui::Text("Frame Decoration:");
                ImGui::Checkbox("Frame Enabled", &PreviewConfig::AmmoWheel::CenterFrameEnabled);
                ImGui::Checkbox("Frame Pulse", &PreviewConfig::AmmoWheel::CenterFramePulse);
                ImGui::SliderFloat("Pulse Speed##CP", &PreviewConfig::AmmoWheel::CenterFramePulseSpeed, 0.5f, 5.0f, "%.1f");
            }
            
            // Indicator Settings
            if (ImGui::CollapsingHeader("Indicators [SAVES]")) {
                ImGui::Text("Low Ammo Warning:");
                ImGui::Checkbox("Low Ammo Indicator", &PreviewConfig::AmmoWheel::LowAmmoIndicatorEnabled);
                ImGui::SliderInt("Threshold", &PreviewConfig::AmmoWheel::LowAmmoThreshold, 1, 50);
                ImGui::SliderFloat("Indicator Thickness##LA", &PreviewConfig::AmmoWheel::LowAmmoIndicatorThickness, 1.0f, 5.0f, "%.1f");
                
                ImGui::Separator();
                ImGui::Text("Selected Indicator:");
                ImGui::SliderFloat("Thickness##SI", &PreviewConfig::AmmoWheel::SelectedIndicatorThickness, 1.0f, 8.0f, "%.1f");
            }
            
            ImGui::Separator();
            
            // Save Buttons
            if (ImGui::CollapsingHeader("Save Changes", ImGuiTreeNodeFlags_DefaultOpen)) {
                static std::string saveStatus;
                
                if (ImGui::Button("Save to AmmoWheel.ini", ImVec2(-1, 30))) {
                    if (PreviewConfig::SaveAmmoWheelIni(dataRoot)) {
                        saveStatus = "AmmoWheel.ini saved!";
                        spdlog::info("Saved AmmoWheel.ini successfully");
                    } else {
                        saveStatus = "Failed to save AmmoWheel.ini";
                        spdlog::error("Failed to save AmmoWheel.ini");
                    }
                }
                
                if (ImGui::Button("Save to Styles.ini", ImVec2(-1, 30))) {
                    if (PreviewConfig::SaveStylesIni(dataRoot)) {
                        saveStatus = "Styles.ini saved!";
                        spdlog::info("Saved Styles.ini successfully");
                    } else {
                        saveStatus = "Failed to save Styles.ini";
                        spdlog::error("Failed to save Styles.ini");
                    }
                }
                
                if (!saveStatus.empty()) {
                    ImGui::TextWrapped("%s", saveStatus.c_str());
                }
            }
            
            ImGui::Separator();
            ImGui::Text("FPS: %.1f", io.Framerate);
            
            ImGui::End();
        }

        // Get window size for centering
        RECT rect;
        GetClientRect(hwnd, &rect);
        float windowWidth = static_cast<float>(rect.right - rect.left);
        float windowHeight = static_cast<float>(rect.bottom - rect.top);
        
        // Center wheel in the right portion of the screen (after control panel)
        float wheelCenterX = 400 + (windowWidth - 400) / 2;
        float wheelCenterY = windowHeight / 2;

        // Render AmmoWheel
        PreviewDrawer::DrawWheel(ImGui::GetBackgroundDrawList(), ImVec2(wheelCenterX, wheelCenterY), numSlots, hoveredSlot);


        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // Present with vsync
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
