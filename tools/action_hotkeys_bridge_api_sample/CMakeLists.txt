set(AHB_SAMPLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(AHB_SAMPLE_REPO_ROOT "${PROJECT_SOURCE_DIR}")

add_library(
	WheelerBridgeApiSample
	SHARED
	EXCLUDE_FROM_ALL
	"${AHB_SAMPLE_ROOT}/main.cpp"
)

target_compile_features(
	WheelerBridgeApiSample
	PRIVATE
		cxx_std_20
)

target_include_directories(
	WheelerBridgeApiSample
	PRIVATE
		"${AHB_SAMPLE_REPO_ROOT}/src"
		"${AHB_SAMPLE_REPO_ROOT}/src/bin"
)

target_compile_definitions(
	WheelerBridgeApiSample
	PRIVATE
		SPDLOG_NO_SOURCE_LOC
)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	target_compile_options(
		WheelerBridgeApiSample
		PRIVATE
			"/utf-8"
			"/permissive-"
			"/Zc:preprocessor"
			"/d1trimfile:${CMAKE_SOURCE_DIR}/"
	)
endif()

target_link_libraries(
	WheelerBridgeApiSample
	PRIVATE
		CommonLibSSE::CommonLibSSE
)

