set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

set(SOURCE_DIR "${ROOT_DIR}/src")
file(GLOB_RECURSE SOURCE_FILES "${SOURCE_DIR}/*.cpp" "${SOURCE_DIR}/*.h" "${SOURCE_DIR}/*.hpp")	

# Exclude the bundled imgui_freetype implementation; it targets older
# ImGui internals and is not needed when we use the built-in font rasterizer.
list(FILTER SOURCE_FILES EXCLUDE REGEX "imgui_freetype\\.cpp$")

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
	"${SOURCE_DIR}/bin/Integrations/OStimAutoModePolicy.h"
	"${SOURCE_DIR}/bin/Integrations/OStimConfigPolicy.h"
	"${SOURCE_DIR}/bin/Integrations/OStimSceneActionClosePolicy.h"
	"${SOURCE_DIR}/bin/Integrations/OStimUnifiedWheelModel.h"
	"${SOURCE_DIR}/bin/Integrations/OStimNGSceneAPI.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimNGSceneAPI.h"
	"${SOURCE_DIR}/bin/Integrations/OStimNGThreadAPI.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimNGThreadAPI.h"
	"${SOURCE_DIR}/bin/Integrations/OStimIntegration.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimIntegration.h"
	"${SOURCE_DIR}/bin/Integrations/OStimPreviewResolver.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimPreviewResolver.h"
	"${SOURCE_DIR}/bin/Integrations/OStimSceneSemantic.h"
	"${SOURCE_DIR}/bin/Integrations/OStimStateTracker.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimStateTracker.h"
	"${SOURCE_DIR}/bin/Integrations/OStimTypes.h"
	"${SOURCE_DIR}/bin/Integrations/OStimUndressVisualRefresh.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimUndressVisualRefresh.h"
)

list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/LegacyWeaponRestoreVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/PoisonDiagnosticVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/GroupedPoisonLineageDiagnosticVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/TransientGameplayLifecycleVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/HandMemoryAttackDiagnosticVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/HandMemorySameFormRestoreVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/WeaponLiveVisualStatusVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/WeaponActiveVisualVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/WeaponHandIndicatorPresentationVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/WeaponPoisonPresentationVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/AmmoRangedWeaponPoisonPresentationVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/GroupedPoisonPresentationAliasVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/AlreadyPoisonedReapplyGuardVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Wheeler/HoverActivationSnapshotVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Integrations/OStimNavigationModelVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Integrations/OStimSceneSemanticVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Integrations/OStimAutoModeVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Integrations/OStimUnifiedWheelVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Integrations/OStimClosePolicyVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Integrations/OStimConfigVerification\\.cpp$")
list(FILTER SOURCE_FILES EXCLUDE REGEX "bin/Integrations/OStimUndressVisualRefreshVerification\\.cpp$")

# 对 SourceFile 变量里面的所有文件分类(保留资源管理器的目录结构)
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

find_package(nlohmann_json CONFIG REQUIRED)



target_link_libraries(
	"${PROJECT_NAME}"
	PRIVATE
		CommonLibSSE::CommonLibSSE
		xbyak::xbyak
		imgui::imgui
		Freetype::Freetype
		nlohmann_json::nlohmann_json
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
	wheeler_poison_diagnostic_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Wheeler/PoisonDiagnosticVerification.cpp"
	"${SOURCE_DIR}/bin/Wheeler/PoisonDiagnosticPolicy.h"
)
target_compile_features(wheeler_poison_diagnostic_verification PRIVATE cxx_std_23)
target_compile_definitions(wheeler_poison_diagnostic_verification PRIVATE WHEELER_POISON_DIAGNOSTIC_VERIFICATION_MAIN)
target_include_directories(wheeler_poison_diagnostic_verification PRIVATE "${SOURCE_DIR}/bin/Wheeler")
if(MSVC)
	target_compile_options(wheeler_poison_diagnostic_verification PRIVATE "/utf-8" "/permissive-")
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

add_executable(wheeler_weapon_poison_presentation_verification EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Wheeler/WeaponPoisonPresentationVerification.cpp")
target_compile_features(wheeler_weapon_poison_presentation_verification PRIVATE cxx_std_23)
if(MSVC)
	target_compile_options(wheeler_weapon_poison_presentation_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(wheeler_ammo_ranged_weapon_poison_presentation_verification EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Wheeler/AmmoRangedWeaponPoisonPresentationVerification.cpp")
target_compile_features(wheeler_ammo_ranged_weapon_poison_presentation_verification PRIVATE cxx_std_23)
if(MSVC)
	target_compile_options(wheeler_ammo_ranged_weapon_poison_presentation_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(wheeler_weapon_hand_indicator_presentation_verification EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Wheeler/WeaponHandIndicatorPresentationVerification.cpp")
target_compile_features(wheeler_weapon_hand_indicator_presentation_verification PRIVATE cxx_std_23)
if(MSVC)
	target_compile_options(wheeler_weapon_hand_indicator_presentation_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(wheeler_weapon_active_visual_verification EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Wheeler/WeaponActiveVisualVerification.cpp")
target_compile_features(wheeler_weapon_active_visual_verification PRIVATE cxx_std_23)
if(MSVC)
	target_compile_options(wheeler_weapon_active_visual_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(
	wheeler_weapon_live_visual_status_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Wheeler/WeaponLiveVisualStatusVerification.cpp"
	"${SOURCE_DIR}/bin/Wheeler/WheelItems/WeaponLiveVisualStatusPolicy.h"
)
target_compile_features(wheeler_weapon_live_visual_status_verification PRIVATE cxx_std_23)
target_compile_definitions(wheeler_weapon_live_visual_status_verification PRIVATE WHEELER_WEAPON_LIVE_VISUAL_STATUS_VERIFICATION_MAIN)
target_include_directories(wheeler_weapon_live_visual_status_verification PRIVATE "${SOURCE_DIR}/bin/Wheeler")
if(MSVC)
	target_compile_options(wheeler_weapon_live_visual_status_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(
	wheeler_grouped_poison_lineage_diagnostic_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Wheeler/GroupedPoisonLineageDiagnosticVerification.cpp"
	"${SOURCE_DIR}/bin/Wheeler/WheelItems/GroupedPoisonLineageDiagnosticPolicy.h"
)
target_compile_features(wheeler_grouped_poison_lineage_diagnostic_verification PRIVATE cxx_std_23)
target_compile_definitions(wheeler_grouped_poison_lineage_diagnostic_verification PRIVATE WHEELER_GROUPED_POISON_LINEAGE_DIAGNOSTIC_VERIFICATION_MAIN)
target_include_directories(wheeler_grouped_poison_lineage_diagnostic_verification PRIVATE "${SOURCE_DIR}/bin/Wheeler")
if(MSVC)
	target_compile_options(wheeler_grouped_poison_lineage_diagnostic_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(
	wheeler_grouped_poison_presentation_alias_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Wheeler/GroupedPoisonPresentationAliasVerification.cpp"
	"${SOURCE_DIR}/bin/Wheeler/WheelItems/GroupedPoisonPresentationAliasPolicy.h"
)
target_compile_features(wheeler_grouped_poison_presentation_alias_verification PRIVATE cxx_std_23)
target_compile_definitions(wheeler_grouped_poison_presentation_alias_verification PRIVATE WHEELER_GROUPED_POISON_PRESENTATION_ALIAS_VERIFICATION_MAIN)
target_include_directories(wheeler_grouped_poison_presentation_alias_verification PRIVATE "${SOURCE_DIR}/bin/Wheeler")
if(MSVC)
	target_compile_options(wheeler_grouped_poison_presentation_alias_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(
	already_poisoned_reapply_guard_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Wheeler/AlreadyPoisonedReapplyGuardVerification.cpp"
	"${SOURCE_DIR}/bin/Wheeler/WheelItems/AlreadyPoisonedReapplyGuardPolicy.h"
)
target_compile_features(already_poisoned_reapply_guard_verification PRIVATE cxx_std_23)
target_include_directories(already_poisoned_reapply_guard_verification PRIVATE "${SOURCE_DIR}/bin/Wheeler")
if(MSVC)
	target_compile_options(already_poisoned_reapply_guard_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(
	wheeler_ostim_navigation_model_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Integrations/OStimNavigationModelVerification.cpp"
)
target_compile_features(wheeler_ostim_navigation_model_verification PRIVATE cxx_std_23)
target_include_directories(wheeler_ostim_navigation_model_verification PRIVATE "${SOURCE_DIR}/bin/Integrations")
if(MSVC)
	target_compile_options(wheeler_ostim_navigation_model_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(
	wheeler_ostim_scene_semantic_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Integrations/OStimSceneSemanticVerification.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimSceneSemantic.h"
)
target_compile_features(wheeler_ostim_scene_semantic_verification PRIVATE cxx_std_23)
target_include_directories(wheeler_ostim_scene_semantic_verification PRIVATE "${SOURCE_DIR}/bin/Integrations")
target_link_libraries(wheeler_ostim_scene_semantic_verification PRIVATE nlohmann_json::nlohmann_json)
if(MSVC)
	target_compile_options(wheeler_ostim_scene_semantic_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_custom_target(
	wheeler_ostim_thread_zero_policy_verification
	COMMAND "${CMAKE_COMMAND}"
		"-DNATIVE_API_SOURCE:FILEPATH=${SOURCE_DIR}/bin/Integrations/OStimNGThreadAPI.cpp"
		"-DINTEGRATION_SOURCE:FILEPATH=${SOURCE_DIR}/bin/Integrations/OStimIntegration.cpp"
		-P "${SOURCE_DIR}/cmake/OStimThreadZeroPolicyVerification.cmake"
	COMMENT "Verifying that native OStim thread ID zero is not rejected"
	VERBATIM
)

add_executable(
	wheeler_ostim_auto_mode_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Integrations/OStimAutoModeVerification.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimAutoModePolicy.h"
)
target_compile_features(wheeler_ostim_auto_mode_verification PRIVATE cxx_std_23)
target_include_directories(wheeler_ostim_auto_mode_verification PRIVATE "${SOURCE_DIR}/bin/Integrations")
if(MSVC)
	target_compile_options(wheeler_ostim_auto_mode_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(
	wheeler_ostim_unified_wheel_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Integrations/OStimUnifiedWheelVerification.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimUnifiedWheelModel.h"
	"${SOURCE_DIR}/bin/Integrations/OStimAutoModePolicy.h"
)
target_compile_features(wheeler_ostim_unified_wheel_verification PRIVATE cxx_std_23)
target_include_directories(wheeler_ostim_unified_wheel_verification PRIVATE "${SOURCE_DIR}/bin/Integrations")
if(MSVC)
	target_compile_options(wheeler_ostim_unified_wheel_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(
	wheeler_ostim_close_policy_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Integrations/OStimClosePolicyVerification.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimSceneActionClosePolicy.h"
	"${SOURCE_DIR}/bin/Integrations/OStimUnifiedWheelModel.h"
)
target_compile_features(wheeler_ostim_close_policy_verification PRIVATE cxx_std_23)
target_include_directories(wheeler_ostim_close_policy_verification PRIVATE "${SOURCE_DIR}/bin/Integrations")
if(MSVC)
	target_compile_options(wheeler_ostim_close_policy_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(
	wheeler_ostim_config_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Integrations/OStimConfigVerification.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimConfigPolicy.h"
)
target_compile_features(wheeler_ostim_config_verification PRIVATE cxx_std_23)
target_include_directories(wheeler_ostim_config_verification PRIVATE "${SOURCE_DIR}/bin/Integrations")
target_link_libraries(wheeler_ostim_config_verification PRIVATE nlohmann_json::nlohmann_json)
if(MSVC)
	target_compile_options(wheeler_ostim_config_verification PRIVATE "/utf-8" "/permissive-")
endif()

add_executable(
	wheeler_ostim_undress_visual_refresh_verification
	EXCLUDE_FROM_ALL
	"${SOURCE_DIR}/bin/Integrations/OStimUndressVisualRefreshVerification.cpp"
	"${SOURCE_DIR}/bin/Integrations/OStimUndressVisualRefresh.h"
)
target_compile_features(wheeler_ostim_undress_visual_refresh_verification PRIVATE cxx_std_23)
target_include_directories(wheeler_ostim_undress_visual_refresh_verification PRIVATE "${SOURCE_DIR}/bin/Integrations")
if(MSVC)
	target_compile_options(wheeler_ostim_undress_visual_refresh_verification PRIVATE "/utf-8" "/permissive-")
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

target_precompile_headers(
	"${PROJECT_NAME}"
	PRIVATE
		"${SOURCE_DIR}/PCH.h"
)

# The source Data directory is the approved mod-root payload.  Refuse to
# stage a source DLL so the freshly built target remains authoritative.
set(WHEELER_SOURCE_DATA_DIR "${ROOT_DIR}/Data")
set(WHEELER_SOURCE_RUNTIME_DLL "${WHEELER_SOURCE_DATA_DIR}/SKSE/Plugins/wheeler.dll")
if(EXISTS "${WHEELER_SOURCE_RUNTIME_DLL}")
	message(FATAL_ERROR "Refusing to package source Data/SKSE/Plugins/wheeler.dll; the built wheeler target is authoritative")
endif()

# A trailing slash installs Data's contents at the package mod root.  Generic
# Wheeler legal mirrors and the sample INI are installed only through their
# dedicated package locations below.  Independent third-party notices remain
# in the approved Data payload and are also documented below.
install(
	DIRECTORY "${WHEELER_SOURCE_DATA_DIR}/"
	DESTINATION "."
	COMPONENT "main"
	REGEX "/SKSE/Plugins/README[.]md$" EXCLUDE
	REGEX "/SKSE/Plugins/LICENSE$" EXCLUDE
	REGEX "/SKSE/Plugins/NOTICE[.]md$" EXCLUDE
	REGEX "/SKSE/Plugins/THIRD_PARTY_NOTICES[.]md$" EXCLUDE
	REGEX "/SKSE/Plugins/LICENSES/BSD-3-Clause-Wheeler[.]txt$" EXCLUDE
	REGEX "/SKSE/Plugins/WheelerBridgeApiSample[.]ini$" EXCLUDE
)

# Install the build product after Data so the package contains exactly one,
# freshly built Wheeler runtime DLL.
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
	DESTINATION "SKSE/Plugins/docs/Wheeler Refined"
	COMPONENT "main"
)

install(
	FILES
		"${ROOT_DIR}/LICENSES/BSD-3-Clause-Wheeler.txt"
	DESTINATION "SKSE/Plugins/docs/Wheeler Refined/LICENSES"
	COMPONENT "main"
)

# Retain independent third-party notices in the approved Data payload and
# expose the same notices under the Wheeler-specific documentation namespace.
install(
	DIRECTORY "${WHEELER_SOURCE_DATA_DIR}/SKSE/Plugins/third-party-notices/"
	DESTINATION "SKSE/Plugins/docs/Wheeler Refined/third-party-notices"
	COMPONENT "main"
)

if("${COPY_OUTPUT}")
	add_custom_command(
		TARGET "${PROJECT_NAME}"
		POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${CompiledPluginsPath}/SKSE/Plugins/"
		VERBATIM
	)
endif()

