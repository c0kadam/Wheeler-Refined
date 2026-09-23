cmake_minimum_required(VERSION 3.22)
project(wheeler_preview)

# Download and build dependencies
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.9
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
)
FetchContent_MakeAvailable(spdlog)

add_executable(wheeler_preview WIN32
    main.cpp
    PreviewDrawer.cpp
    PreviewDrawer.h
    PreviewConfig.cpp
    PreviewConfig.h
    # ImGui source files
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    # ImGui backends
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

target_compile_features(wheeler_preview PRIVATE cxx_std_20)

target_include_directories(wheeler_preview PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/include
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(wheeler_preview PRIVATE
    spdlog::spdlog
    d3d11
    dxgi
)

# Set subsystem to Windows GUI
set_target_properties(wheeler_preview PROPERTIES
    WIN32_EXECUTABLE TRUE
)
