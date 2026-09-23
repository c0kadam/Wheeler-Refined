cmake_minimum_required(VERSION 3.22)

project(
	wheeler
	VERSION 1.3.4
	LANGUAGES CXX
)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "in-source builds are not allowed")
endif()

option(COPY_OUTPUT "Copy the output of build operations to the game directory" ON)
option(ENABLE_SKYRIM_SE "Enable support for Skyrim SE in the dynamic runtime feature." ON)
option(ENABLE_SKYRIM_AE "Enable support for Skyrim AE in the dynamic runtime feature." ON)
option(ENABLE_SKYRIM_VR "Enable support for Skyrim VR in the dynamic runtime feature." OFF)
set(BUILD_TESTS OFF)

set(CommonLibSSEPath_NG "" CACHE PATH "Path to a CommonLibSSE-NG checkout")
if(NOT CommonLibSSEPath_NG AND DEFINED ENV{CommonLibSSEPath_NG} AND NOT "$ENV{CommonLibSSEPath_NG}" STREQUAL "")
	set(CommonLibSSEPath_NG "$ENV{CommonLibSSEPath_NG}" CACHE PATH "Path to a CommonLibSSE-NG checkout" FORCE)
endif()

if(NOT IS_DIRECTORY "${CommonLibSSEPath_NG}")
	message(FATAL_ERROR "CommonLibSSEPath_NG must name a CommonLibSSE-NG checkout")
endif()

set(WHEELER_COMMONLIBSSE_NG_REVISION "3d81614617910e7f34b33d8750881811b5e36445")
find_package(Git REQUIRED)
execute_process(
	COMMAND "${GIT_EXECUTABLE}" -C "${CommonLibSSEPath_NG}" rev-parse HEAD
	OUTPUT_VARIABLE WHEELER_COMMONLIBSSE_NG_ACTUAL_REVISION
	OUTPUT_STRIP_TRAILING_WHITESPACE
	RESULT_VARIABLE WHEELER_COMMONLIBSSE_NG_REVISION_RESULT
)
if(NOT WHEELER_COMMONLIBSSE_NG_REVISION_RESULT EQUAL 0 OR NOT WHEELER_COMMONLIBSSE_NG_ACTUAL_REVISION STREQUAL WHEELER_COMMONLIBSSE_NG_REVISION)
	message(FATAL_ERROR "CommonLibSSE-NG must be at ${WHEELER_COMMONLIBSSE_NG_REVISION}; found '${WHEELER_COMMONLIBSSE_NG_ACTUAL_REVISION}'")
endif()

if(COPY_OUTPUT)
	set(CompiledPluginsPath "" CACHE PATH "Existing deployment root containing SKSE/Plugins")
	if(NOT CompiledPluginsPath AND DEFINED ENV{CompiledPluginsPath} AND NOT "$ENV{CompiledPluginsPath}" STREQUAL "")
		set(CompiledPluginsPath "$ENV{CompiledPluginsPath}" CACHE PATH "Existing deployment root containing SKSE/Plugins" FORCE)
	endif()
	if(NOT IS_DIRECTORY "${CompiledPluginsPath}/SKSE/Plugins")
		message(FATAL_ERROR "COPY_OUTPUT=ON requires a valid CompiledPluginsPath")
	endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

add_subdirectory(src)
add_subdirectory(tools/action_hotkeys_bridge_api_sample)
include(cmake/packaging.cmake)
