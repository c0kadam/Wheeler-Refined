set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

set(SOURCE_DIR "${ROOT_DIR}/src")
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS "${SOURCE_DIR}/*.cpp" "${SOURCE_DIR}/*.h" "${SOURCE_DIR}/*.hpp")

# Exclude the bundled imgui_freetype implementation; it targets older
# ImGui internals and is not needed when we use the built-in font rasterizer.
list(FILTER SOURCE_FILES EXCLUDE REGEX "imgui_freetype\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/TransientGameplayLifecycleVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/HoverActivationSnapshotVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/LegacyWeaponRestoreVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/HandMemoryAttackDiagnosticVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/HandMemorySameFormRestoreVerification\\.cpp$")

# Explicitly include newly added capability helpers (guards against stale glob caches).
list(APPEND SOURCE_FILES
	"${SOURCE_DIR}/bin/Utilities/ItemCapabilities.cpp"
	"${SOURCE_DIR}/bin/Utilities/ItemCapabilities.h"
	"${SOURCE_DIR}/bin/Integrations/ExternalFavWheelState.cpp"
	"${SOURCE_DIR}/bin/Integrations/ExternalFavWheelState.h"
	"${SOURCE_DIR}/bin/Wheeler/SlotHandIndicators.cpp"
	"${SOURCE_DIR}/bin/Wheeler/SlotHandIndicators.h"
	"${SOURCE_DIR}/bin/Wheeler/RestorationLifecyclePolicy.h"
	"${SOURCE_DIR}/bin/Wheeler/HandMemoryAttackDiagnosticPolicy.h"
	"${SOURCE_DIR}/bin/Wheeler/WheelItems/WheelItemExternalHotkey.cpp"
	"${SOURCE_DIR}/bin/Wheeler/WheelItems/WheelItemExternalHotkey.h"
	"${SOURCE_DIR}/bin/Wheeler/WheelItems/WheelItemIngredient.cpp"
	"${SOURCE_DIR}/bin/Wheeler/WheelItems/WheelItemIngredient.h"
	"${SOURCE_DIR}/bin/Wheeler/WheelItems/WheelItemOStimAction.cpp"
	"${SOURCE_DIR}/bin/Wheeler/WheelItems/WheelItemOStimAction.h"
	"${SOURCE_DIR}/bin/Integrations/ActionHotkeysBridge.cpp"
	"${SOURCE_DIR}/bin/Integrations/ActionHotkeysBridge.h"
	"${SOURCE_DIR}/bin/Integrations/OStimBridge.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimBridge.h"
	"${SOURCE_DIR}/bin/Integrations/OStimNGThreadAPI.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimNGThreadAPI.h"
	"${SOURCE_DIR}/bin/Integrations/OStimIntegration.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimIntegration.h"
	"${SOURCE_DIR}/bin/Integrations/OStimPreviewResolver.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimPreviewResolver.h"
	"${SOURCE_DIR}/bin/Integrations/OStimStateTracker.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimStateTracker.h"
	"${SOURCE_DIR}/bin/Integrations/OStimTypes.h"
)

# ? SourceFile ???????????(????????????)
foreach(fileItem ${SOURCE_FILES})       
    # Get the directory of the source file
    get_filename_component(PARENT_DIR "${fileItem}" DIRECTORY)
    # Remove common directory prefix to make the group
    string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" GROUP "${PARENT_DIR}")
    # Make sure we are using windows slashes
    string(REPLACE "/" "\\" GROUP "${GROUP}")
    # Group into "Source Files" and "Header Files"
    set(GROUP "${GROUP}")
    source_group("${GROUP}" FILES "${fileItem}")
endforeach()

source_group(TREE "${ROOT_DIR}" FILES ${SOURCE_FILES})

set(VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src/Plugin.h")
configure_file(
	"${ROOT_DIR}/cmake/Plugin.h.in"
	"${VERSION_HEADER}"
	@ONLY
)

source_group("src" FILES "${VERSION_HEADER}")

configure_file(
	"${ROOT_DIR}/cmake/version.rc.in"
	"${CMAKE_CURRENT_BINARY_DIR}/version.rc"
	@ONLY
)

add_library(
	"${PROJECT_NAME}"
	SHARED
	${SOURCE_FILES}
	"${VERSION_HEADER}"
	"${CMAKE_CURRENT_BINARY_DIR}/version.rc"
	"${ROOT_DIR}/.clang-format"
	"${ROOT_DIR}/.editorconfig"
)

target_compile_features(
	"${PROJECT_NAME}"
	PRIVATE
		cxx_std_23
)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	target_compile_options(
		"${PROJECT_NAME}"
		PRIVATE
			"/sdl"	# Enable Additional Security Checks
			"/utf-8"	# Set Source and Executable character sets to UTF-8
			"/Zi"	# Debug Information Format

			"/permissive-"	# Standards conformance
			"/Zc:preprocessor"	# Enable preprocessor conformance mode

			"/wd4200" # nonstandard extension used : zero-sized array in struct/union

			# Privacy: Strip absolute paths from __FILE__ macro (use basename only)
			"/d1trimfile:${CMAKE_SOURCE_DIR}/"

			"$<$<CONFIG:DEBUG>:>"
			"$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3>"
	)

	target_link_options(
		"${PROJECT_NAME}"
		PRIVATE
			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif()
target_include_directories(
	"${PROJECT_NAME}"
	PRIVATE
		"${CMAKE_CURRENT_BINARY_DIR}/src"
		"${SOURCE_DIR}"
)

# Privacy: Prevent absolute source paths from being embedded in the binary
target_compile_definitions(
	"${PROJECT_NAME}"
	PRIVATE
		SPDLOG_NO_SOURCE_LOC
		WHEELER_EXPORTS  # Enable DLL export for External API (GetWheelerAPI)
)

set(SKSE_SUPPORT_XBYAK ON)

add_subdirectory("${CommonLibSSEPath_NG}" CommonLibSSE EXCLUDE_FROM_ALL)

find_package(xbyak REQUIRED CONFIG)

find_package(imgui REQUIRED)

find_package(Freetype REQUIRED)



target_link_libraries(
	"${PROJECT_NAME}"
	PRIVATE
		CommonLibSSE::CommonLibSSE
		xbyak::xbyak
		imgui::imgui
		Freetype::Freetype
		ole32
		windowscodecs
)

add_executable(
	wheeler_transient_lifecycle_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Wheeler/TransientGameplayLifecycleVerification.cpp"
	"${SOURCE_DIR}/bin/Wheeler/RestorationLifecyclePolicy.h"
)
target_compile_features(wheeler_transient_lifecycle_verification PRIVATE cxx_std_23)
target_compile_definitions(wheeler_transient_lifecycle_verification PRIVATE WHEELER_TRANSIENT_LIFECYCLE_VERIFICATION_MAIN)
target_include_directories(wheeler_transient_lifecycle_verification PRIVATE "${SOURCE_DIR}/bin/Wheeler")
if(MSVC)
	target_compile_options(wheeler_transient_lifecycle_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(
	wheeler_hover_activation_snapshot_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Wheeler/HoverActivationSnapshotVerification.cpp"
	"${SOURCE_DIR}/bin/Wheeler/HoverActivationSnapshotPolicy.h"
)
target_compile_features(wheeler_hover_activation_snapshot_verification PRIVATE cxx_std_23)
target_compile_definitions(wheeler_hover_activation_snapshot_verification PRIVATE WHEELER_HOVER_ACTIVATION_SNAPSHOT_VERIFICATION_MAIN)
target_include_directories(wheeler_hover_activation_snapshot_verification PRIVATE "${SOURCE_DIR}/bin/Wheeler")
if(MSVC)
	target_compile_options(wheeler_hover_activation_snapshot_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(
	wheeler_legacy_weapon_restore_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Wheeler/LegacyWeaponRestoreVerification.cpp"
	"${SOURCE_DIR}/bin/Wheeler/WheelItems/LegacyWeaponRestorePolicy.h"
)
target_compile_features(wheeler_legacy_weapon_restore_verification PRIVATE cxx_std_23)
target_compile_definitions(wheeler_legacy_weapon_restore_verification PRIVATE WHEELER_LEGACY_WEAPON_RESTORE_VERIFICATION_MAIN)
target_include_directories(wheeler_legacy_weapon_restore_verification PRIVATE "${SOURCE_DIR}/bin/Wheeler")
if(MSVC)
	target_compile_options(wheeler_legacy_weapon_restore_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(
	wheeler_handmemory_attack_diagnostic_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Wheeler/HandMemoryAttackDiagnosticVerification.cpp"
	"${SOURCE_DIR}/bin/Wheeler/HandMemoryAttackDiagnosticPolicy.h"
)
target_compile_features(wheeler_handmemory_attack_diagnostic_verification PRIVATE cxx_std_23)
target_compile_definitions(wheeler_handmemory_attack_diagnostic_verification PRIVATE WHEELER_HANDMEMORY_ATTACK_DIAGNOSTIC_VERIFICATION_MAIN)
target_include_directories(wheeler_handmemory_attack_diagnostic_verification PRIVATE "${SOURCE_DIR}/bin/Wheeler")
if(MSVC)
	target_compile_options(wheeler_handmemory_attack_diagnostic_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(
	wheeler_handmemory_same_form_restore_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Wheeler/HandMemorySameFormRestoreVerification.cpp"
	"${SOURCE_DIR}/bin/Wheeler/WheelItems/LegacyWeaponRestorePolicy.h"
)
target_compile_features(wheeler_handmemory_same_form_restore_verification PRIVATE cxx_std_23)
target_compile_definitions(wheeler_handmemory_same_form_restore_verification PRIVATE WHEELER_HANDMEMORY_SAME_FORM_RESTORE_VERIFICATION_MAIN)
target_include_directories(wheeler_handmemory_same_form_restore_verification PRIVATE "${SOURCE_DIR}/bin/Wheeler")
if(MSVC)
	target_compile_options(wheeler_handmemory_same_form_restore_verification PRIVATE "/utf-8" "/permissive-")
endif()

target_precompile_headers(
	"${PROJECT_NAME}"
	PRIVATE
		"${SOURCE_DIR}/PCH.h"
)

install(
	FILES
		"$<TARGET_FILE:${PROJECT_NAME}>"
	DESTINATION "SKSE/Plugins"
	COMPONENT "main"
)

install(
	FILES
		"${ROOT_DIR}/README.md"
		"${ROOT_DIR}/LICENSE"
		"${ROOT_DIR}/NOTICE.md"
		"${ROOT_DIR}/THIRD_PARTY_NOTICES.md"
	DESTINATION "."
	COMPONENT "main"
)

install(
	FILES
		"$<TARGET_PDB_FILE:${PROJECT_NAME}>"
	DESTINATION "/"
	COMPONENT "pdbs"
)

add_custom_command(
	TARGET "${PROJECT_NAME}"
	POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ROOT_DIR}/README.md" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/README.md"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ROOT_DIR}/LICENSE" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/LICENSE"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ROOT_DIR}/NOTICE.md" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/NOTICE.md"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ROOT_DIR}/THIRD_PARTY_NOTICES.md" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/THIRD_PARTY_NOTICES.md"
	VERBATIM
)

if("${COPY_OUTPUT}")
	add_custom_command(
		TARGET "${PROJECT_NAME}"
		POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${CompiledPluginsPath}/SKSE/Plugins/"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${CompiledPluginsPath}/SKSE/Plugins/"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ROOT_DIR}/README.md" "${CompiledPluginsPath}/SKSE/Plugins/"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ROOT_DIR}/LICENSE" "${CompiledPluginsPath}/SKSE/Plugins/"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ROOT_DIR}/NOTICE.md" "${CompiledPluginsPath}/SKSE/Plugins/"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ROOT_DIR}/THIRD_PARTY_NOTICES.md" "${CompiledPluginsPath}/SKSE/Plugins/"
		VERBATIM
	)
endif()

