cmake_minimum_required(VERSION 3.14)
project(edgeos_api VERSION 2026.0.1.50)

# EdgeOS SDK root: use libs from SDK instead of system for better adaptation
set(EDGEOS_SDK_ROOT "/opt/edgeos-sdk" CACHE PATH "EdgeOS SDK installation root")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable position-independent code globally (required for shared libraries)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ============================================
# Check System Dependencies
# ============================================
# Dependencies are checked directly below

# ============================================
# Drogon HTTP Framework Dependencies
# ============================================
# Option: Auto-download missing dependencies using FetchContent
option(AUTO_DOWNLOAD_DEPENDENCIES "Automatically download missing dependencies" ON)

# Check for OpenSSL (required)
find_package(OpenSSL QUIET)
if(NOT OpenSSL_FOUND)
    message(FATAL_ERROR
        "OpenSSL not found. Drogon requires OpenSSL.\n"
        "To install: sudo apt-get install libssl-dev")
endif()

# Check for UUID (required by Drogon)
find_library(UUID_LIBRARY uuid)
if(NOT UUID_LIBRARY)
    if(AUTO_DOWNLOAD_DEPENDENCIES)
        message(STATUS "UUID library not found. Drogon can work without it, but some features may be limited.")
        message(STATUS "To install: sudo apt-get install uuid-dev")
        # UUID is usually small, we can continue without it
        set(UUID_LIBRARY "")
    else()
        message(WARNING
            "UUID library not found. Some Drogon features may not work.\n"
            "To install: sudo apt-get install uuid-dev")
    endif()
else()
    message(STATUS "✓ Found UUID library")
endif()

# Check for jsoncpp - try to download if not found
find_package(Jsoncpp QUIET)
if(NOT Jsoncpp_FOUND)
    if(AUTO_DOWNLOAD_DEPENDENCIES)
        message(STATUS "Jsoncpp not found. Attempting to download using FetchContent...")
        include(FetchContent)

        # CMAKE_POSITION_INDEPENDENT_CODE is already set globally above
        FetchContent_Declare(
            jsoncpp
            GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
            GIT_TAG        1.9.5
            GIT_SHALLOW    TRUE
        )

        # Set jsoncpp build options
        set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
        set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
        set(JSONCPP_WITH_WARNING_AS_ERROR OFF CACHE BOOL "" FORCE)
        set(JSONCPP_WITH_CMAKE_PACKAGE OFF CACHE BOOL "" FORCE)  # Disable to avoid path issues
        set(JSONCPP_INCLUDE_DIR "${CMAKE_BINARY_DIR}/_deps/jsoncpp-src/include" CACHE PATH "" FORCE)

        # Build jsoncpp as shared library with -fPIC for linking into shared libraries
        set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(jsoncpp)

        # Ensure jsoncpp is built with -fPIC
        if(TARGET jsoncpp_lib)
            set_target_properties(jsoncpp_lib PROPERTIES
                POSITION_INDEPENDENT_CODE ON
            )
        endif()
        if(TARGET jsoncpp_static)
            set_target_properties(jsoncpp_static PROPERTIES
                POSITION_INDEPENDENT_CODE ON
            )
        endif()

        # After FetchContent_MakeAvailable, jsoncpp should be available
        # Use source include directory (always exists)
        set(JSONCPP_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/_deps/jsoncpp-src/include" CACHE PATH "Jsoncpp include directories" FORCE)

        # For library, jsoncpp_lib target should be available
        # The actual library file is built in build/lib/ directory, not _deps/jsoncpp-build/
        # Find the actual library file location
        find_library(JSONCPP_LIBRARIES
            NAMES jsoncpp
            PATHS
                ${CMAKE_BINARY_DIR}/lib
                ${CMAKE_BINARY_DIR}/_deps/jsoncpp-build
                ${CMAKE_BINARY_DIR}/_deps/jsoncpp-build/lib
            NO_DEFAULT_PATH
        )

        # If not found yet (not built), use expected path in build/lib/
        if(NOT JSONCPP_LIBRARIES)
            set(JSONCPP_LIBRARIES "${CMAKE_BINARY_DIR}/lib/libjsoncpp.a")
        endif()

        # Verify include directory exists
        if(NOT EXISTS "${JSONCPP_INCLUDE_DIRS}/json/json.h")
            message(FATAL_ERROR
                "Jsoncpp include directory not found: ${JSONCPP_INCLUDE_DIRS}/json/json.h")
        endif()

        # Set variables that Drogon's FindJsoncpp.cmake expects
        set(JSONCPP_LIBRARIES ${JSONCPP_LIBRARIES} CACHE FILEPATH "Jsoncpp library" FORCE)
        set(Jsoncpp_FOUND TRUE CACHE BOOL "Jsoncpp found" FORCE)

        message(STATUS "✓ Jsoncpp downloaded and configured successfully")
        message(STATUS "  Include: ${JSONCPP_INCLUDE_DIRS}")
        message(STATUS "  Library target: jsoncpp_lib (will be built automatically)")
        message(STATUS "  Library path: ${JSONCPP_LIBRARIES}")
    else()
        message(FATAL_ERROR
            "Jsoncpp not found. Drogon requires jsoncpp.\n"
            "To install: sudo apt-get install libjsoncpp-dev\n"
            "Or enable auto-download: cmake .. -DAUTO_DOWNLOAD_DEPENDENCIES=ON")
    endif()
else()
    message(STATUS "✓ Found system Jsoncpp")
endif()

# Check for yaml-cpp - try to download if not found
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    if(AUTO_DOWNLOAD_DEPENDENCIES)
        message(STATUS "yaml-cpp not found. Attempting to download using FetchContent...")
        if(NOT FetchContent_Declare)
            include(FetchContent)
        endif()

        FetchContent_Declare(
            yaml-cpp
            GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
            GIT_TAG        yaml-cpp-0.7.0
            GIT_SHALLOW    TRUE
        )

        # Set yaml-cpp build options
        set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(yaml-cpp)

        # Ensure yaml-cpp is built with -fPIC
        if(TARGET yaml-cpp)
            set_target_properties(yaml-cpp PROPERTIES
                POSITION_INDEPENDENT_CODE ON
            )
        endif()

        set(YAML_CPP_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/_deps/yaml-cpp-src/include" CACHE PATH "yaml-cpp include directories" FORCE)
        set(yaml-cpp_FOUND TRUE CACHE BOOL "yaml-cpp found" FORCE)

        message(STATUS "✓ yaml-cpp downloaded and configured successfully")
        message(STATUS "  Include: ${YAML_CPP_INCLUDE_DIRS}")
    else()
        message(STATUS "yaml-cpp not found. JSON endpoint will not work.\n"
                       "To install: sudo apt-get install libyaml-cpp-dev\n"
                       "Or enable auto-download: cmake .. -DAUTO_DOWNLOAD_DEPENDENCIES=ON")
    endif()
else()
    message(STATUS "✓ Found system yaml-cpp")
endif()

# ============================================
# Drogon HTTP Framework
# ============================================
# Option: Use FetchContent to automatically download and build Drogon
# Set DROGON_USE_FETCHCONTENT=OFF to use system-installed Drogon instead
option(DROGON_USE_FETCHCONTENT "Use FetchContent to download Drogon automatically" ON)

if(DROGON_USE_FETCHCONTENT)
    message(STATUS "Using FetchContent to download Drogon automatically...")

    # Ensure FetchContent is included (may have been included for jsoncpp)
    if(NOT DEFINED FetchContent_MakeAvailable)
        include(FetchContent)
    endif()

    # Configure Drogon version (use latest stable release)
    set(DROGON_VERSION "v1.9.0" CACHE STRING "Drogon version to fetch")

    FetchContent_Declare(
        drogon
        GIT_REPOSITORY https://github.com/drogonframework/drogon.git
        GIT_TAG        ${DROGON_VERSION}
        GIT_SHALLOW   TRUE
    )

    # Set Drogon build options
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_ORM OFF CACHE BOOL "" FORCE)  # Disable ORM if not needed

    # UUID is optional for Drogon, but FindUUID.cmake may fail if not found
    # We'll let Drogon handle it - it should work without UUID

    # Workaround: Drogon v1.9.0 has a broken try_compile for std::filesystem (check_has_std_filesystem_path.cc
    # is missing "#include <filesystem>"), which triggers "partial implementation" even with GCC 13. Force C++20
    # for Drogon so it skips that check; our project keeps CXX_STANDARD 17.
    set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard for Drogon" FORCE)
    FetchContent_MakeAvailable(drogon)
    set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard" FORCE)

    message(STATUS "✓ Drogon fetched and built successfully")
else()
    message(STATUS "Looking for system-installed Drogon...")
    find_package(Drogon REQUIRED)

    if(NOT Drogon_FOUND)
        message(FATAL_ERROR
            "Drogon not found. Please install Drogon or set DROGON_USE_FETCHCONTENT=ON")
    endif()
endif()

# ============================================
# OpenCV (required for CVEDIX SDK) - prefer EdgeOS SDK libs
# ============================================
# Prefer CUDA from EdgeOS SDK if present
if(EXISTS "${EDGEOS_SDK_ROOT}/lib/cuda")
    list(APPEND CMAKE_PREFIX_PATH "${EDGEOS_SDK_ROOT}")
endif()
if(EXISTS "/usr/local/cuda-12.8/bin/nvcc")
    set(CUDA_TOOLKIT_ROOT_DIR "/usr/local/cuda-12.8" CACHE PATH "CUDA 12.8 toolkit" FORCE)
    set(CMAKE_CUDA_COMPILER "/usr/local/cuda-12.8/bin/nvcc" CACHE FILEPATH "NVCC 12.8" FORCE)
    message(STATUS "Using CUDA 12.8 at ${CUDA_TOOLKIT_ROOT_DIR}")
endif()
# Prefer OpenCV from EdgeOS SDK
set(OPENCV_USE_SDK FALSE)
if(EXISTS "${EDGEOS_SDK_ROOT}/lib/opencv")
    list(INSERT CMAKE_LIBRARY_PATH 0 "${EDGEOS_SDK_ROOT}/lib/opencv")
    list(INSERT CMAKE_INCLUDE_PATH 0 "${EDGEOS_SDK_ROOT}/include")
    set(OPENCV_USE_SDK TRUE)
    message(STATUS "✓ Using OpenCV from EdgeOS SDK: ${EDGEOS_SDK_ROOT}/lib/opencv")
endif()
find_package(OpenCV QUIET)
if(NOT OpenCV_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(OpenCV QUIET opencv4)
        if(OpenCV_FOUND)
            message(STATUS "✓ Found OpenCV via pkg-config")
            set(OpenCV_INCLUDE_DIRS ${OpenCV_INCLUDE_DIRS})
            set(OpenCV_LIBRARIES ${OpenCV_LIBRARIES})
        endif()
    endif()
endif()

if(NOT OpenCV_FOUND)
    message(WARNING
        "OpenCV not found. CVEDIX SDK features may not work.\n"
        "To install: sudo apt-get install libopencv-dev or use EdgeOS SDK at ${EDGEOS_SDK_ROOT}")
else()
    message(STATUS "✓ Found OpenCV")
    message(STATUS "  Version: ${OpenCV_VERSION}")
    message(STATUS "  Include dirs: ${OpenCV_INCLUDE_DIRS}")
endif()

# ============================================
# Plog Logging Library (header-only)
# ============================================
if(AUTO_DOWNLOAD_DEPENDENCIES)
    message(STATUS "Downloading Plog logging library...")
    include(FetchContent)

    FetchContent_Declare(
        plog
        GIT_REPOSITORY https://github.com/SergiusTheBest/plog.git
        GIT_TAG        1.1.11
        GIT_SHALLOW    TRUE
    )

    FetchContent_MakeAvailable(plog)

    # Plog is header-only, so we just need to add the include directory
    set(PLOG_INCLUDE_DIR "${CMAKE_BINARY_DIR}/_deps/plog-src/include" CACHE PATH "Plog include directory" FORCE)

    message(STATUS "✓ Plog downloaded and configured successfully")
    message(STATUS "  Include: ${PLOG_INCLUDE_DIR}")
else()
    # Try to find system-installed Plog
    find_path(PLOG_INCLUDE_DIR plog/Log.h
        PATHS
            /usr/include
            /usr/local/include
            ${CMAKE_SOURCE_DIR}/third_party/plog/include
    )

    if(PLOG_INCLUDE_DIR)
        message(STATUS "✓ Found system Plog: ${PLOG_INCLUDE_DIR}")
    else()
        message(FATAL_ERROR
            "Plog not found. Please install Plog or enable auto-download:\n"
            "  cmake .. -DAUTO_DOWNLOAD_DEPENDENCIES=ON")
    endif()
endif()

# ============================================
# OpenSSL for SHA256 (used in cache - optional for now)
# ============================================
# find_package(OpenSSL REQUIRED)  # Uncomment when using cache

# ============================================
# Source files
# ============================================
set(SOURCES
    src/main.cpp
    src/api/health_handler.cpp
    src/api/version_handler.cpp
    src/api/watchdog_handler.cpp
    src/api/endpoints_handler.cpp
    src/api/swagger_handler.cpp
    src/api/scalar_handler.cpp
    src/api/hls_handler.cpp
    src/api/create_instance_handler.cpp
    src/api/quick_instance_handler.cpp
    src/api/instance_handler.cpp
    src/api/instance_handler_get_start_restart.cpp
    src/api/instance_handler_update_delete.cpp
    src/api/instance_handler_config_stats_frame.cpp
    src/api/instance_handler_input_stream.cpp
    src/api/instance_handler_configure_stream.cpp
    src/api/instance_handler_classes_preview.cpp
    src/api/instance_handler_options_parse.cpp
    src/api/instance_handler_batch.cpp
    src/api/instance_handler_instance_output.cpp
    src/api/instance_handler_nested_set_config.cpp
    src/api/instance_handler_log_config.cpp
    src/api/instance_handler_load_unload_state.cpp
    src/api/instance_handler_set_state_patch.cpp
    src/api/instance_handler_events_hls.cpp
    src/api/instance_handler_configure_rtsp.cpp
    src/api/instance_handler_push_encoded.cpp
    src/api/instance_handler_push_compressed.cpp
    src/api/instance_fps_handler.cpp
    src/api/solution_handler.cpp
    src/api/solution_handler_create_update.cpp
    src/api/solution_handler_delete_parameters.cpp
    src/api/solution_handler_instance_body.cpp
    src/api/solution_handler_options_defaults.cpp
    src/api/solution_handler_load_schema.cpp
    src/api/solution_handler_schema_helpers.cpp
    src/api/group_handler.cpp
    src/api/config_handler.cpp
    src/api/system_handler.cpp
    src/api/lines_handler.cpp
    src/api/jams_handler.cpp
    src/api/stops_handler.cpp
    src/api/securt_handler.cpp
    src/api/securt_line_handler.cpp
    src/api/area_handler.cpp
    src/api/node_handler.cpp
    src/api/onvif_handler.cpp
    src/api/recognition_handler.cpp
    src/api/system_info_handler.cpp
    src/api/license_handler.cpp
    src/api/log_handler.cpp
    src/models/model_upload_handler.cpp
    src/videos/video_upload_handler.cpp
    src/fonts/font_upload_handler.cpp
    src/core/watchdog.cpp
    src/core/health_monitor.cpp
    src/core/system_metrics.cpp
    src/core/device_report_collector.cpp
    src/core/device_watchdog.cpp
    src/core/endpoint_monitor.cpp
    src/core/request_middleware.cpp
    src/core/metrics_interceptor.cpp
    src/core/event_queue.cpp
    src/core/frame_input_queue.cpp
    src/core/codec_manager.cpp
    src/core/frame_decoder.cpp
    src/core/frame_processor.cpp
    src/core/cors_filter.cpp
    src/core/cors_helper.cpp
    src/core/ai_processor.cpp
    src/core/ai_watchdog.cpp
    src/core/inference_session.cpp
    src/core/ai_runtime_facade.cpp
    src/core/pipeline_helper.cpp
    src/core/backpressure_controller.cpp
    src/core/adaptive_queue_size_manager.cpp
    src/instances/instance_registry.cpp
    src/instances/instance_registry_start_instance.cpp
    src/instances/instance_registry_stop_list.cpp
    src/instances/instance_registry_update.cpp
    src/instances/instance_registry_persistent.cpp
    src/instances/instance_registry_start_pipeline.cpp
    src/instances/instance_registry_stop_pipeline.cpp
    src/instances/instance_registry_rebuild.cpp
    src/instances/instance_registry_rtmp_nodes.cpp
    src/instances/instance_registry_retry.cpp
    src/instances/instance_registry_update_config.cpp
    src/instances/instance_registry_video_loop.cpp
    src/instances/instance_registry_statistics.cpp
    src/instances/instance_registry_frame_cache.cpp
    src/instances/instance_registry_frame_capture_hook.cpp
    src/instances/instance_registry_rtmp_hooks.cpp
    src/instances/instance_registry_rtsp_monitor.cpp
    src/instances/instance_registry_rtsp_stop.cpp
    src/instances/instance_registry_rtsp_reconnect.cpp
    src/instances/instance_registry_rtmp_monitor.cpp
    src/instances/instance_state_manager.cpp
    src/instances/queue_monitor.cpp
    src/instances/inprocess_instance_manager.cpp
    src/instances/subprocess_instance_manager.cpp
    src/instances/instance_manager_factory.cpp
    src/solutions/solution_registry.cpp
    src/groups/group_registry.cpp
    src/core/pipeline_builder.cpp
    src/core/pipeline_builder_source_nodes.cpp
    src/core/pipeline_builder_destination_nodes.cpp
    src/core/pipeline_builder_detector_nodes.cpp
    src/core/pipeline_builder_broker_nodes.cpp
    src/core/pipeline_builder_behavior_analysis_nodes.cpp
    src/core/pipeline_builder_other_nodes.cpp
    src/core/rtmp_lastframe_fallback_proxy_node.cpp
    src/core/persistent_output_leg.cpp
    src/core/frame_router.cpp
    src/core/frame_router_sink_node.cpp
    src/core/cvedix_validator.cpp
    src/utils/cvedix_mqtt_client_impl.cpp
    src/utils/gstreamer_checker.cpp
    src/utils/mp4_finalizer.cpp
    src/utils/mp4_directory_watcher.cpp
    src/instances/instance_storage.cpp
    src/solutions/solution_storage.cpp
    src/groups/group_storage.cpp
    src/core/node_pool_manager.cpp
    src/core/node_storage.cpp
    src/core/node_template_registry.cpp
    src/config/system_config.cpp
    src/core/system_config_manager.cpp
    src/core/preferences_manager.cpp
    src/core/decoder_detector.cpp
    src/core/uuid_generator.cpp
    src/core/platform_detector.cpp
    src/core/log_manager.cpp
    src/core/instance_logging_config.cpp
    src/core/onvif_camera_registry.cpp
    src/core/onvif_credentials_manager.cpp
    src/core/onvif_discovery.cpp
    src/core/onvif_stream_manager.cpp
    src/core/onvif_soap_builder.cpp
    src/core/onvif_xml_parser.cpp
    src/core/onvif_http_client.cpp
    src/core/onvif_digest_auth.cpp
    src/core/onvif_camera_whitelist.cpp
    src/core/onvif_camera_handler_factory.cpp
    src/core/onvif_camera_handlers/onvif_generic_handler.cpp
    src/core/onvif_camera_handlers/onvif_tapo_handler.cpp
    src/models/create_instance_request.cpp
    src/models/update_instance_request.cpp
    src/models/solution_config.cpp
    src/models/group_info.cpp
    src/api/group_handler.cpp
    # Infrastructure components (available but not used in main yet)
    src/core/resource_manager.cpp
    src/core/shutdown_flag.cpp
    # src/core/rate_limiter.cpp
    src/core/ai_cache.cpp
    # src/core/priority_queue.cpp
    # src/core/circuit_breaker.cpp
    src/core/performance_monitor.cpp
    # AI handlers (not needed for base code)
    # src/api/ai_handler.cpp
    src/api/ai_websocket.cpp
    src/api/metrics_handler.cpp
)

# ============================================
# Create executable (edgeos-api)
# ============================================
add_executable(edgeos_api ${SOURCES})
set_target_properties(edgeos_api PROPERTIES RUNTIME_OUTPUT_NAME edgeos-api)

# Set compile options for this target (modern CMake way)
target_compile_options(edgeos_api PRIVATE -Wall -Wextra)

# Link and RPATH: prefer EdgeOS SDK libs (cvedix, opencv, cuda, cudnn, tensorrt)
set(EDGEOS_SDK_LIB_DIRS "${EDGEOS_SDK_ROOT}/lib/cvedix")
if(EXISTS "${EDGEOS_SDK_ROOT}/lib/opencv")
    list(APPEND EDGEOS_SDK_LIB_DIRS "${EDGEOS_SDK_ROOT}/lib/opencv")
endif()
if(EXISTS "${EDGEOS_SDK_ROOT}/lib/cuda")
    list(APPEND EDGEOS_SDK_LIB_DIRS "${EDGEOS_SDK_ROOT}/lib/cuda")
endif()
if(EXISTS "${EDGEOS_SDK_ROOT}/lib/cudnn")
    list(APPEND EDGEOS_SDK_LIB_DIRS "${EDGEOS_SDK_ROOT}/lib/cudnn")
endif()
if(EXISTS "${EDGEOS_SDK_ROOT}/lib/tensorrt")
    list(APPEND EDGEOS_SDK_LIB_DIRS "${EDGEOS_SDK_ROOT}/lib/tensorrt")
endif()
link_directories(${EDGEOS_SDK_LIB_DIRS})

# Set RPATH configuration (SDK libs + build dir)
set_target_properties(edgeos_api PROPERTIES
    BUILD_WITH_INSTALL_RPATH FALSE
    BUILD_RPATH_USE_ORIGIN TRUE
    BUILD_RPATH "${CMAKE_BINARY_DIR}/lib;${EDGEOS_SDK_LIB_DIRS};/usr/local/lib"
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;${EDGEOS_SDK_ROOT}/lib/cvedix;${EDGEOS_SDK_ROOT}/lib/opencv;/usr/local/lib"
    INSTALL_RPATH_USE_LINK_PATH TRUE
    LINK_FLAGS "-Wl,--disable-new-dtags"
)

# ============================================
# Link libraries
# ============================================
# Link Drogon - try Drogon::Drogon first, fallback to drogon
if(TARGET Drogon::Drogon)
    target_link_libraries(edgeos_api PRIVATE Drogon::Drogon)
elseif(TARGET drogon)
    target_link_libraries(edgeos_api PRIVATE drogon)
else()
    message(FATAL_ERROR "Drogon target not found. Cannot link.")
endif()

# ============================================
# Cereal (required by CVEDIX SDK)
# ============================================
# CVEDIX SDK expects cereal at ../../../third_party/cereal relative to its headers
# We need to create a symlink structure or add include path
message(STATUS "Checking for Cereal library...")

# Try to find cereal in system
find_path(CEREAL_INCLUDE_DIR cereal/cereal.hpp
    PATHS
        /usr/include
        /usr/local/include
        ${CMAKE_BINARY_DIR}/_deps/cereal-src/include
)

if(NOT CEREAL_INCLUDE_DIR AND AUTO_DOWNLOAD_DEPENDENCIES)
    message(STATUS "Cereal not found. Downloading headers only...")
    include(FetchContent)

    # Download cereal source without building (header-only library)
    FetchContent_Declare(
        cereal
        GIT_REPOSITORY https://github.com/USCiLab/cereal.git
        GIT_TAG        v1.3.2
        GIT_SHALLOW    TRUE
    )

    # Only fetch, don't configure/build (cereal is header-only)
    FetchContent_GetProperties(cereal)
    if(NOT cereal_POPULATED)
        FetchContent_Populate(cereal)
        set(CEREAL_INCLUDE_DIR "${cereal_SOURCE_DIR}/include" CACHE PATH "Cereal include directory" FORCE)
        message(STATUS "✓ Cereal headers downloaded to: ${CEREAL_INCLUDE_DIR}")
    endif()
endif()

if(CEREAL_INCLUDE_DIR)
    message(STATUS "✓ Found Cereal: ${CEREAL_INCLUDE_DIR}")

    # CVEDIX SDK expects cereal at ../../../third_party/cereal relative to ${EDGEOS_SDK_ROOT}/include/cvedix/nodes/broker/cereal_archive/
    # This means it looks for ${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cereal/cereal.hpp
    # Create the exact directory structure CVEDIX SDK expects in build directory
    set(CVEDIX_THIRD_PARTY_DIR "${CMAKE_BINARY_DIR}/cvedix_third_party")
    set(CVEDIX_CEREAL_DIR "${CVEDIX_THIRD_PARTY_DIR}/cereal")
    file(MAKE_DIRECTORY "${CVEDIX_THIRD_PARTY_DIR}")

    # Create symlink: build/cvedix_third_party/cereal -> cereal include dir
    if(NOT EXISTS "${CVEDIX_CEREAL_DIR}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CEREAL_INCLUDE_DIR}/cereal"
            "${CVEDIX_CEREAL_DIR}"
            RESULT_VARIABLE SYMLINK_RESULT
            ERROR_VARIABLE SYMLINK_ERROR
            OUTPUT_QUIET
        )
        if(NOT SYMLINK_RESULT EQUAL 0)
            message(WARNING "⚠ Failed to create cereal symlink in build directory: ${SYMLINK_ERROR}")
            message(WARNING "  This may cause compilation errors. Run: sudo ./scripts/dev_setup.sh --skip-deps --skip-build")
        endif()
    endif()

    # Check if system symlink exists (required for CVEDIX SDK)
    # Check both /usr/include/cvedix and ${EDGEOS_SDK_ROOT}/include/cvedix locations
    set(CEREAL_SYMLINK_FOUND FALSE)
    if(EXISTS "/usr/include/cvedix/third_party/cereal")
        message(STATUS "✓ Cereal symlink found at /usr/include/cvedix/third_party/cereal")
        set(CEREAL_SYMLINK_FOUND TRUE)
    elseif(EXISTS "${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cereal")
        message(STATUS "✓ Cereal symlink found at ${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cereal")
        set(CEREAL_SYMLINK_FOUND TRUE)
    endif()

    if(NOT CEREAL_SYMLINK_FOUND)
        message(WARNING "")
        message(WARNING "⚠ CVEDIX SDK requires cereal symlink")
        if(EXISTS "${EDGEOS_SDK_ROOT}/include/cvedix")
            message(WARNING "  For ${EDGEOS_SDK_ROOT} installation:")
            message(WARNING "    sudo mkdir -p ${EDGEOS_SDK_ROOT}/include/cvedix/third_party")
            message(WARNING "    sudo ln -sf ${CEREAL_INCLUDE_DIR}/cereal ${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cereal")
        else()
            message(WARNING "  For /usr/include installation:")
            message(WARNING "    Run: sudo bash ${CMAKE_SOURCE_DIR}/scripts/dev_setup.sh --skip-deps --skip-build")
            message(WARNING "  Or manually:")
            message(WARNING "    sudo mkdir -p /usr/include/cvedix/third_party")
            message(WARNING "    sudo ln -sf ${CEREAL_INCLUDE_DIR}/cereal /usr/include/cvedix/third_party/cereal")
        endif()
        message(WARNING "")
    endif()
    message(STATUS "✓ Configured cereal paths for CVEDIX SDK")
else()
    message(WARNING "Cereal not found. CVEDIX SDK features may not work.")
    message(WARNING "To install: sudo apt-get install libcereal-dev (if available) or enable AUTO_DOWNLOAD_DEPENDENCIES")
endif()

# ============================================
# cpp-base64 (required by CVEDIX SDK)
# ============================================
# CVEDIX SDK expects cpp_base64 at ../../../third_party/cpp_base64 relative to its headers
# This means it looks for ${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cpp_base64/base64.h
message(STATUS "Checking for cpp-base64 library...")

# Try to find cpp-base64 in system
find_path(CPP_BASE64_INCLUDE_DIR base64.h
    PATHS
        /usr/include
        /usr/local/include
        ${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cpp_base64
        ${CMAKE_BINARY_DIR}/_deps/cpp-base64-src
)

if(NOT CPP_BASE64_INCLUDE_DIR AND AUTO_DOWNLOAD_DEPENDENCIES)
    message(STATUS "cpp-base64 not found. Downloading headers only...")
    include(FetchContent)

    # Download cpp-base64 source without building (header-only library)
    FetchContent_Declare(
        cpp-base64
        GIT_REPOSITORY https://github.com/ReneNyffenegger/cpp-base64.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )

    # Only fetch, don't configure/build (cpp-base64 is header-only)
    FetchContent_GetProperties(cpp-base64)
    if(NOT cpp-base64_POPULATED)
        FetchContent_Populate(cpp-base64)
        set(CPP_BASE64_INCLUDE_DIR "${cpp-base64_SOURCE_DIR}" CACHE PATH "cpp-base64 include directory" FORCE)
        message(STATUS "✓ cpp-base64 headers downloaded to: ${CPP_BASE64_INCLUDE_DIR}")
    endif()
endif()

if(CPP_BASE64_INCLUDE_DIR)
    message(STATUS "✓ Found cpp-base64: ${CPP_BASE64_INCLUDE_DIR}")

    # CVEDIX SDK expects cpp_base64 at ../../../third_party/cpp_base64 relative to ${EDGEOS_SDK_ROOT}/include/cvedix/nodes/broker/
    # This means it looks for ${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cpp_base64/base64.h
    # Create the exact directory structure CVEDIX SDK expects in build directory
    set(CVEDIX_THIRD_PARTY_DIR "${CMAKE_BINARY_DIR}/cvedix_third_party")
    set(CVEDIX_CPP_BASE64_DIR "${CVEDIX_THIRD_PARTY_DIR}/cpp_base64")
    file(MAKE_DIRECTORY "${CVEDIX_THIRD_PARTY_DIR}")

    # Create symlink: build/cvedix_third_party/cpp_base64 -> cpp-base64 source dir
    if(NOT EXISTS "${CVEDIX_CPP_BASE64_DIR}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CPP_BASE64_INCLUDE_DIR}"
            "${CVEDIX_CPP_BASE64_DIR}"
            RESULT_VARIABLE SYMLINK_RESULT
            ERROR_VARIABLE SYMLINK_ERROR
            OUTPUT_QUIET
        )
        if(NOT SYMLINK_RESULT EQUAL 0)
            message(WARNING "⚠ Failed to create cpp-base64 symlink in build directory: ${SYMLINK_ERROR}")
            message(WARNING "  This may cause compilation errors. Run: sudo ./scripts/dev_setup.sh --skip-deps --skip-build")
        endif()
    endif()

    # Check if system symlink exists (required for CVEDIX SDK)
    # Check both /usr/include/cvedix and ${EDGEOS_SDK_ROOT}/include/cvedix locations
    set(CPP_BASE64_SYMLINK_FOUND FALSE)
    if(EXISTS "/usr/include/cvedix/third_party/cpp_base64/base64.h")
        message(STATUS "✓ cpp-base64 symlink found at /usr/include/cvedix/third_party/cpp_base64")
        set(CPP_BASE64_SYMLINK_FOUND TRUE)
    elseif(EXISTS "${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cpp_base64/base64.h")
        message(STATUS "✓ cpp-base64 symlink found at ${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cpp_base64")
        set(CPP_BASE64_SYMLINK_FOUND TRUE)
    endif()

    if(NOT CPP_BASE64_SYMLINK_FOUND)
        message(WARNING "")
        message(WARNING "⚠ CVEDIX SDK requires cpp-base64 symlink")
        if(EXISTS "${EDGEOS_SDK_ROOT}/include/cvedix")
            message(WARNING "  For ${EDGEOS_SDK_ROOT} installation:")
            message(WARNING "    sudo mkdir -p ${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cpp_base64")
            message(WARNING "    sudo ln -sf ${CPP_BASE64_INCLUDE_DIR}/base64.h ${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cpp_base64/base64.h")
        else()
            message(WARNING "  For /usr/include installation:")
            message(WARNING "    sudo mkdir -p /usr/include/cvedix/third_party/cpp_base64")
            message(WARNING "    sudo ln -sf ${CPP_BASE64_INCLUDE_DIR}/base64.h /usr/include/cvedix/third_party/cpp_base64/base64.h")
        endif()
        message(WARNING "")
    endif()
    message(STATUS "✓ Configured cpp-base64 paths for CVEDIX SDK")
else()
    message(WARNING "cpp-base64 not found. CVEDIX SDK enhanced console broker features may not work.")
    message(WARNING "To install: enable AUTO_DOWNLOAD_DEPENDENCIES or manually download from https://github.com/ReneNyffenegger/cpp-base64")
endif()

# ============================================
# CVEDIX SDK (required for AI processing)
# ============================================
# CVEDIX SDK is expected to be installed system-wide (not downloaded via FetchContent)
# CMake will search for it in standard system paths:
#   - Config files: /usr/lib/cmake/cvedix/cvedix-config.cmake
#   - Headers: /usr/include/cvedix/
#   - Libraries: /usr/lib/ (or /usr/lib64/)
#
# CVEDIX SDK may also be installed in ${EDGEOS_SDK_ROOT}/ (non-standard location)
# We check both locations and add ${EDGEOS_SDK_ROOT} to CMAKE_PREFIX_PATH if it exists
#
# Workaround: Define check_required_components if not available
# (needed for cvedix-config.cmake which uses this function)
if(NOT COMMAND check_required_components)
    function(check_required_components _package_name)
        foreach(_comp ${ARGN})
            # Check if target exists (for modern CMake packages)
            string(REPLACE "_" "::" _comp_target "${_package_name}::${_comp}")
            if(NOT TARGET "${_comp_target}")
                # Also check variable-based approach
                set(_comp_var "${_package_name}_${_comp}_FOUND")
                if(NOT ${_comp_var})
                    set(${_package_name}_FOUND FALSE)
                    set(${_package_name}_NOT_FOUND_MESSAGE "${_package_name} ${_comp} component not found")
                    if(${_package_name}_FIND_REQUIRED)
                        message(FATAL_ERROR "${${_package_name}_NOT_FOUND_MESSAGE}")
                    else()
                        if(NOT ${_package_name}_FIND_QUIETLY)
                            message(STATUS "${${_package_name}_NOT_FOUND_MESSAGE}")
                        endif()
                    endif()
                endif()
            endif()
        endforeach()
    endfunction()
endif()

# Check if CVEDIX SDK is installed in ${EDGEOS_SDK_ROOT} (standard location for this project)
if(EXISTS "${EDGEOS_SDK_ROOT}/lib/cvedix/libcvedix_core.so")
    message(STATUS "Found CVEDIX SDK in ${EDGEOS_SDK_ROOT}, adding to CMAKE_PREFIX_PATH")
    list(APPEND CMAKE_PREFIX_PATH "${EDGEOS_SDK_ROOT}")
    message(STATUS "✓ Added ${EDGEOS_SDK_ROOT} to CMAKE_PREFIX_PATH")

    # Create symlink structure for CVEDIX SDK third_party dependencies
    # CVEDIX SDK headers use relative paths like ../../third_party/cpp_base64/base64.h
    # We need to create the structure in ${EDGEOS_SDK_ROOT}/include/cvedix/third_party/
    if(CPP_BASE64_INCLUDE_DIR AND NOT EXISTS "${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cpp_base64/base64.h")
        message(STATUS "Creating cpp_base64 symlink structure for CVEDIX SDK...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E make_directory "${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cpp_base64"
            RESULT_VARIABLE MKDIR_RESULT
            ERROR_VARIABLE MKDIR_ERROR
            OUTPUT_QUIET
        )
        if(MKDIR_RESULT EQUAL 0)
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${CPP_BASE64_INCLUDE_DIR}/base64.h"
                "${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cpp_base64/base64.h"
                RESULT_VARIABLE SYMLINK_RESULT
                ERROR_VARIABLE SYMLINK_ERROR
                OUTPUT_QUIET
            )
            if(SYMLINK_RESULT EQUAL 0)
                message(STATUS "✓ Created cpp_base64 symlink in ${EDGEOS_SDK_ROOT}/include/cvedix/third_party/cpp_base64/")
            else()
                message(WARNING "⚠ Could not create cpp_base64 symlink (may need sudo):")
                message(WARNING "  Error: ${SYMLINK_ERROR}")
                message(WARNING "  Run: sudo ./scripts/dev_setup.sh --skip-deps --skip-build")
            endif()
        else()
            message(WARNING "⚠ Could not create directory (may need sudo):")
            message(WARNING "  Error: ${MKDIR_ERROR}")
            message(WARNING "  Run: sudo ./scripts/fix_all_symlinks.sh")
        endif()
    endif()
endif()

# Fix tinyexpr library path before manual SDK setup
# CVEDIX SDK expects libtinyexpr.so in /usr/lib but it may be in ${EDGEOS_SDK_ROOT}/lib/cvedix
# Create symlink in build directory to work around this issue
if(EXISTS "${EDGEOS_SDK_ROOT}/lib/cvedix/libtinyexpr.so" AND NOT EXISTS "/usr/lib/libtinyexpr.so")
    # Create a symlink in build directory
    set(TINYEXPR_BUILD_LIB "${CMAKE_BINARY_DIR}/libtinyexpr.so")
    if(NOT EXISTS "${TINYEXPR_BUILD_LIB}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${EDGEOS_SDK_ROOT}/lib/cvedix/libtinyexpr.so"
            "${TINYEXPR_BUILD_LIB}"
            RESULT_VARIABLE SYMLINK_RESULT
            ERROR_VARIABLE SYMLINK_ERROR
            OUTPUT_QUIET
        )
        if(SYMLINK_RESULT EQUAL 0)
            message(STATUS "✓ Created tinyexpr symlink in build directory")
        else()
            message(WARNING "⚠ Failed to create tinyexpr symlink in build directory: ${SYMLINK_ERROR}")
        endif()
    endif()
    # Also try to create symlink in /usr/lib (may require sudo, but try anyway)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${EDGEOS_SDK_ROOT}/lib/cvedix/libtinyexpr.so"
        "/usr/lib/libtinyexpr.so"
        RESULT_VARIABLE SYMLINK_USR_RESULT
        ERROR_VARIABLE SYMLINK_USR_ERROR
        OUTPUT_QUIET
    )
    if(SYMLINK_USR_RESULT EQUAL 0)
        message(STATUS "✓ Created tinyexpr symlink in /usr/lib")
    else()
        message(WARNING "⚠ Could not create symlink in /usr/lib (may need sudo):")
        message(WARNING "  Error: ${SYMLINK_USR_ERROR}")
        message(WARNING "  Run: sudo ./scripts/fix_all_symlinks.sh")
    endif()
endif()

# Manual CVEDIX SDK setup (since CMake config file doesn't exist)
# Check if CVEDIX SDK is installed in ${EDGEOS_SDK_ROOT}
set(EDGEOS_SDK_FOUND FALSE)
if(EXISTS "${EDGEOS_SDK_ROOT}/lib/cvedix/libcvedix_core.so")
    set(EDGEOS_SDK_FOUND TRUE)
    message(STATUS "✓ Found CVEDIX SDK in ${EDGEOS_SDK_ROOT}")
    
    # Create imported target for CVEDIX SDK
    if(NOT TARGET cvedix::cvedix_instance_sdk)
        add_library(cvedix::cvedix_instance_sdk SHARED IMPORTED)
        set_target_properties(cvedix::cvedix_instance_sdk PROPERTIES
            IMPORTED_LOCATION "${EDGEOS_SDK_ROOT}/lib/cvedix/libcvedix_core.so"
            INTERFACE_INCLUDE_DIRECTORIES "${EDGEOS_SDK_ROOT}/include"
        )
        message(STATUS "✓ Created imported target cvedix::cvedix_instance_sdk")
    endif()
else()
    message(FATAL_ERROR
        "CVEDIX SDK not found at ${EDGEOS_SDK_ROOT}/lib/cvedix/libcvedix_core.so\n"
        "Please install EdgeOS SDK at ${EDGEOS_SDK_ROOT} to continue.")
endif()

# Add all include directories for edgeos_api target (modern CMake way)
# Base project include directory
target_include_directories(edgeos_api PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

# OpenCV include directories
if(OpenCV_FOUND)
    if(TARGET opencv_core)
        # Modern CMake way - targets handle includes automatically
        # But we may still need to add include dirs for pkg-config path
        if(OpenCV_INCLUDE_DIRS)
            target_include_directories(edgeos_api PRIVATE ${OpenCV_INCLUDE_DIRS})
        endif()
    else()
        # Fallback to pkg-config libraries
        if(OpenCV_INCLUDE_DIRS)
            target_include_directories(edgeos_api PRIVATE ${OpenCV_INCLUDE_DIRS})
        endif()
    endif()
endif()

# Plog include directory
if(PLOG_INCLUDE_DIR)
    target_include_directories(edgeos_api PRIVATE ${PLOG_INCLUDE_DIR})
endif()

# CVEDIX SDK include directory (when SDK is found)
if(EDGEOS_SDK_FOUND)
    target_include_directories(edgeos_api PRIVATE "${EDGEOS_SDK_ROOT}/include")
endif()

# Add cereal and cpp-base64 include paths for CVEDIX SDK
# CVEDIX SDK headers include cereal with relative path ../../../third_party/cereal/cereal.hpp
# CVEDIX SDK headers include base64 with relative path ../../../third_party/cpp_base64/base64.h
# We've created the structure in build directory, now add it to include paths
if(CEREAL_INCLUDE_DIR)
    # Add build directory so compiler can resolve cvedix_third_party/cereal
    target_include_directories(edgeos_api PRIVATE "${CMAKE_BINARY_DIR}")
    target_include_directories(edgeos_api PRIVATE "${CEREAL_INCLUDE_DIR}")
    message(STATUS "✓ Added cereal include paths for CVEDIX SDK")
endif()

if(CPP_BASE64_INCLUDE_DIR)
    # Add build directory so compiler can resolve cvedix_third_party/cpp_base64
    # CMAKE_BINARY_DIR is already added above for cereal, so we don't need to add it again
    target_include_directories(edgeos_api PRIVATE "${CPP_BASE64_INCLUDE_DIR}")
    message(STATUS "✓ Added cpp-base64 include paths for CVEDIX SDK")
endif()

# ============================================
# Check CUDA availability and OpenCV CUDA libraries
# ============================================
set(OPENCV_CUDA_AVAILABLE FALSE)
set(OPENCV_CUDA_LIBS "")

# List of OpenCV CUDA library names
set(CUDA_LIB_NAMES
    opencv_cudaarithm
    opencv_cudabgsegm
    opencv_cudacodec
    opencv_cudafeatures2d
    opencv_cudafilters
    opencv_cudaimgproc
    opencv_cudalegacy
    opencv_cudaobjdetect
    opencv_cudaoptflow
    opencv_cudastereo
    opencv_cudawarping
    opencv_cudev
)

# Check if any OpenCV CUDA libraries exist by trying to find them
# We check both library files and CMake targets
set(SEARCH_PATHS
    /usr/local/lib
    /usr/local/lib/x86_64-linux-gnu
    /usr/lib
    /usr/lib/x86_64-linux-gnu
)

foreach(CUDA_LIB ${CUDA_LIB_NAMES})
    # Check if CMake target exists (modern OpenCV)
    if(TARGET opencv_${CUDA_LIB})
        set(OPENCV_CUDA_AVAILABLE TRUE)
        list(APPEND OPENCV_CUDA_LIBS opencv_${CUDA_LIB})
    else()
        # Check if library file exists (fallback)
        find_library(CUDA_LIB_TEMP
            NAMES ${CUDA_LIB}
            PATHS ${SEARCH_PATHS}
            NO_DEFAULT_PATH
            NO_CMAKE_PATH
            NO_CMAKE_ENVIRONMENT_PATH
            NO_SYSTEM_ENVIRONMENT_PATH
            NO_CMAKE_SYSTEM_PATH
        )
        if(CUDA_LIB_TEMP)
            set(OPENCV_CUDA_AVAILABLE TRUE)
            list(APPEND OPENCV_CUDA_LIBS ${CUDA_LIB})
        endif()
    endif()
endforeach()

if(OPENCV_CUDA_AVAILABLE)
    message(STATUS "✓ OpenCV CUDA libraries found - GPU acceleration enabled")
    message(STATUS "  Found CUDA libraries: ${OPENCV_CUDA_LIBS}")
else()
    message(STATUS "⚠ OpenCV CUDA libraries not found - building without GPU acceleration")
    message(STATUS "  This is normal for systems without CUDA/GPU support")
endif()

target_link_libraries(edgeos_api PRIVATE cvedix::cvedix_instance_sdk)
message(STATUS "✓ CVEDIX SDK linked: cvedix::cvedix_instance_sdk")

# Handle OpenCV CUDA libraries - filter them out if not available
# CVEDIX SDK may link against OpenCV CUDA libraries transitively
if(TARGET cvedix::cvedix_instance_sdk)
    get_target_property(CVEDIX_LINK_LIBS cvedix::cvedix_instance_sdk INTERFACE_LINK_LIBRARIES)
    if(CVEDIX_LINK_LIBS)
        set(FILTERED_LIBS "")
        foreach(LIB ${CVEDIX_LINK_LIBS})
            # Check if this is a CUDA library
            set(IS_CUDA_LIB FALSE)
            foreach(CUDA_LIB_NAME ${CUDA_LIB_NAMES})
                if("${LIB}" MATCHES "${CUDA_LIB_NAME}")
                    set(IS_CUDA_LIB TRUE)
                    break()
                endif()
            endforeach()

            # Only include CUDA libraries if they are available
            if(IS_CUDA_LIB)
                if(OPENCV_CUDA_AVAILABLE)
                    list(APPEND FILTERED_LIBS "${LIB}")
                endif()
            else()
                list(APPEND FILTERED_LIBS "${LIB}")
            endif()
        endforeach()

        # Update the target properties if needed
        if(NOT "${CVEDIX_LINK_LIBS}" STREQUAL "${FILTERED_LIBS}")
            set_target_properties(cvedix::cvedix_instance_sdk PROPERTIES
                INTERFACE_LINK_LIBRARIES "${FILTERED_LIBS}"
            )
            if(OPENCV_CUDA_AVAILABLE)
                message(STATUS "✓ CVEDIX SDK configured with CUDA support")
            else()
                message(STATUS "✓ Filtered unavailable CUDA libraries from CVEDIX SDK")
            endif()
        endif()
    endif()
endif()

# Link OpenCV (for CVEDIX SDK)
if(OpenCV_FOUND)
    if(TARGET opencv_core)
        # Modern CMake way
        # CVEDIX SDK requires opencv_dnn module (for cv::dnn::Net)
        # Only link the modules we actually need
        set(OPENCV_BASE_LIBS
            opencv_core
            opencv_imgproc
            opencv_imgcodecs
            opencv_videoio
            opencv_highgui
            opencv_dnn
            opencv_objdetect
        )

        # Conditionally add CUDA libraries if available
        if(OPENCV_CUDA_AVAILABLE)
            # Check which CUDA targets exist and add them
            foreach(CUDA_LIB_NAME ${CUDA_LIB_NAMES})
                if(TARGET opencv_${CUDA_LIB_NAME})
                    list(APPEND OPENCV_BASE_LIBS opencv_${CUDA_LIB_NAME})
                endif()
            endforeach()
            message(STATUS "✓ Linking OpenCV with CUDA support")
        else()
            message(STATUS "✓ Linking OpenCV without CUDA support")
        endif()

        target_link_libraries(edgeos_api PRIVATE ${OPENCV_BASE_LIBS})
    else()
        # Fallback to pkg-config libraries
        set(OPENCV_LIBS ${OpenCV_LIBRARIES})

        # Filter CUDA libraries based on availability
        if(NOT OPENCV_CUDA_AVAILABLE)
            # Remove CUDA modules from the list if CUDA is not available
            list(FILTER OPENCV_LIBS EXCLUDE REGEX "opencv_cuda")
            message(STATUS "✓ Filtered CUDA libraries from OpenCV pkg-config (CUDA not available)")
        else()
            message(STATUS "✓ Using OpenCV libraries from pkg-config (with CUDA)")
        endif()

        target_link_libraries(edgeos_api PRIVATE ${OPENCV_LIBS})
    endif()
    message(STATUS "✓ OpenCV linked to edgeos_api")
endif()

# Use linker flags to handle missing libraries gracefully
# This provides a fallback in case some libraries are still referenced
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(OPENCV_CUDA_AVAILABLE)
        # With CUDA available, use --as-needed to only link what's needed
        target_link_options(edgeos_api PRIVATE
            -Wl,--as-needed
        )
    else()
        # Without CUDA, allow missing shared library dependencies as fallback
        target_link_options(edgeos_api PRIVATE
            -Wl,--as-needed
            -Wl,--allow-shlib-undefined
        )
        message(STATUS "✓ Added linker flags to handle missing CUDA libraries gracefully")
    endif()
endif()

# ============================================
# Fix missing libopencv_freetype.so.410
# ============================================
# CVEDIX SDK requires libopencv_freetype.so.410 but OpenCV 4.10 may not have it
# Copy from OpenCV 4.6 if available (temporary workaround)
if(EXISTS "/usr/lib/x86_64-linux-gnu/libopencv_freetype.so.4.6.0")
    set(FREETYPE_410_PATH "${CMAKE_BINARY_DIR}/lib/libopencv_freetype.so.410")
    add_custom_command(TARGET edgeos_api POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/lib"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "/usr/lib/x86_64-linux-gnu/libopencv_freetype.so.4.6.0"
            "${FREETYPE_410_PATH}"
        COMMENT "Copying libopencv_freetype.so.410 for CVEDIX SDK compatibility"
    )
    message(STATUS "✓ Will copy libopencv_freetype.so.410 to build/lib")
endif()

# ============================================
# hwinfo - Hardware Information Library
# ============================================
# Try submodule first (if available)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/hwinfo/CMakeLists.txt")
    message(STATUS "Adding hwinfo submodule...")
    add_subdirectory(third_party/hwinfo)
    target_link_libraries(edgeos_api PRIVATE lfreist-hwinfo::hwinfo)
    message(STATUS "✓ hwinfo linked to edgeos_api")
    # Add system_info_handler if hwinfo is available
    target_sources(edgeos_api PRIVATE src/api/system_info_handler.cpp)
    # Define macro to enable SystemInfoHandler in main.cpp
    target_compile_definitions(edgeos_api PRIVATE ENABLE_SYSTEM_INFO_HANDLER)
    message(STATUS "✓ system_info_handler enabled (hwinfo available)")

    # Enable metrics handler - always enabled
    target_compile_definitions(edgeos_api PRIVATE ENABLE_METRICS_HANDLER)
    message(STATUS "✓ metrics_handler enabled")
elseif(AUTO_DOWNLOAD_DEPENDENCIES)
    # Use FetchContent to download hwinfo automatically
    message(STATUS "hwinfo submodule not found. Downloading via FetchContent...")

    # Ensure FetchContent is included (may have been included for other dependencies)
    if(NOT DEFINED FetchContent_MakeAvailable)
        include(FetchContent)
    endif()

    FetchContent_Declare(
        hwinfo
        GIT_REPOSITORY https://github.com/lfreist/hwinfo.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )

    FetchContent_MakeAvailable(hwinfo)

    target_link_libraries(edgeos_api PRIVATE lfreist-hwinfo::hwinfo)
    message(STATUS "✓ hwinfo downloaded and linked to edgeos_api")
    # Add system_info_handler if hwinfo is available
    target_sources(edgeos_api PRIVATE src/api/system_info_handler.cpp)
    # Define macro to enable SystemInfoHandler in main.cpp
    target_compile_definitions(edgeos_api PRIVATE ENABLE_SYSTEM_INFO_HANDLER)
    message(STATUS "✓ system_info_handler enabled (hwinfo available)")

    # Enable metrics handler - always enabled
    target_compile_definitions(edgeos_api PRIVATE ENABLE_METRICS_HANDLER)
    message(STATUS "✓ metrics_handler enabled")
else()
    message(WARNING "hwinfo not found. System info endpoints will not work.")
    message(WARNING "Options:")
    message(WARNING "  1. Initialize submodule: git submodule update --init --recursive")
    message(WARNING "  2. Enable auto-download: cmake -DAUTO_DOWNLOAD_DEPENDENCIES=ON ..")

    # Enable metrics handler even if hwinfo is not available
    target_compile_definitions(edgeos_api PRIVATE ENABLE_METRICS_HANDLER)
    message(STATUS "✓ metrics_handler enabled")
endif()

# Additional libraries (if needed)
# OpenSSL::Crypto is required for ai_cache SHA256 hashing
if(OpenSSL_FOUND)
    target_link_libraries(edgeos_api PRIVATE OpenSSL::Crypto)
endif()

# ============================================
# Git commit information
# ============================================
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT GIT_COMMIT)
        set(GIT_COMMIT "unknown")
    endif()
else()
    set(GIT_COMMIT "unknown")
endif()

# ============================================
# Build time
# ============================================
string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M:%S" UTC)

# ============================================
# CVEDIX SDK Feature Flags
# ============================================
# Check if CVEDIX SDK was built with GStreamer support
# GStreamer is required for RTSP, RTMP, Image, and UDP source nodes
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GSTREAMER QUIET gstreamer-1.0)
    if(GSTREAMER_FOUND)
        target_compile_definitions(edgeos_api PRIVATE CVEDIX_WITH_GSTREAMER)
        message(STATUS "✓ GStreamer support enabled (CVEDIX_WITH_GSTREAMER)")
    else()
        message(WARNING "⚠ GStreamer not found. RTSP/RTMP/Image/UDP source nodes will not be available.")
        message(WARNING "  To install: sudo apt-get install libgstreamer1.0-dev")
    endif()
else()
    # If pkg-config not available, check for GStreamer libraries directly
    find_library(GSTREAMER_LIBRARY
        NAMES gstreamer-1.0
        PATHS
            /usr/lib
            /usr/lib/x86_64-linux-gnu
            /usr/local/lib
    )
    if(GSTREAMER_LIBRARY)
        target_compile_definitions(edgeos_api PRIVATE CVEDIX_WITH_GSTREAMER)
        message(STATUS "✓ GStreamer support enabled (CVEDIX_WITH_GSTREAMER)")
    else()
        message(WARNING "⚠ GStreamer not found. RTSP/RTMP/Image/UDP source nodes will not be available.")
    endif()
endif()

# Check if CVEDIX SDK was built with MQTT support
# This is typically defined in CVEDIX SDK's config file, but we can also enable it here
option(CVEDIX_WITH_MQTT "Enable MQTT support (requires CVEDIX SDK built with MQTT)" ON)

if(CVEDIX_WITH_MQTT)
    target_compile_definitions(edgeos_api PRIVATE CVEDIX_WITH_MQTT)
    message(STATUS "✓ MQTT support enabled (CVEDIX_WITH_MQTT)")

    # cvedix_mqtt_client uses libmosquitto, need to link it
    find_library(MOSQUITTO_LIBRARY
        NAMES mosquitto
        PATHS
            /usr/lib
            /usr/lib/x86_64-linux-gnu
            /usr/local/lib
            ${EDGEOS_SDK_ROOT}/lib/cvedix
    )

    if(MOSQUITTO_LIBRARY)
        target_link_libraries(edgeos_api PRIVATE ${MOSQUITTO_LIBRARY})
        message(STATUS "✓ Linked mosquitto library: ${MOSQUITTO_LIBRARY}")
    else()
        # Try pkg-config
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(MOSQUITTO QUIET mosquitto)
            if(MOSQUITTO_FOUND)
                target_link_libraries(edgeos_api PRIVATE ${MOSQUITTO_LIBRARIES})
                target_include_directories(edgeos_api PRIVATE ${MOSQUITTO_INCLUDE_DIRS})
                message(STATUS "✓ Linked mosquitto library via pkg-config: ${MOSQUITTO_LIBRARIES}")
            else()
                message(WARNING "⚠ mosquitto library not found. MQTT features may not work.")
                message(WARNING "  To install: sudo apt-get install libmosquitto-dev")
            endif()
        else()
            message(WARNING "⚠ mosquitto library not found. MQTT features may not work.")
            message(WARNING "  To install: sudo apt-get install libmosquitto-dev")
        endif()
    endif()
else()
    message(STATUS "⚠ MQTT support disabled")
endif()

# ============================================
# Compile definitions
# ============================================
target_compile_definitions(edgeos_api PRIVATE
    PROJECT_VERSION="${PROJECT_VERSION}"
    BUILD_TIME="${BUILD_TIME}"
    GIT_COMMIT="${GIT_COMMIT}"
)

# ============================================
# Worker IPC sources (shared between main and worker)
# ============================================
set(WORKER_IPC_SOURCES
    src/worker/ipc_protocol.cpp
    src/worker/unix_socket.cpp
    src/worker/worker_supervisor.cpp
)

# Add worker sources to main executable
target_sources(edgeos_api PRIVATE ${WORKER_IPC_SOURCES})

# ============================================
# Edge AI Core Library (shared between main and worker)
# ============================================
# Create a shared library with core components needed by both
# the main server and worker processes
set(CORE_LIB_SOURCES
    src/core/pipeline_snapshot.cpp
    src/core/pipeline_builder.cpp
    src/core/pipeline_builder_model_resolver.cpp
    src/core/pipeline_builder_request_utils.cpp
    src/core/pipeline_builder_source_nodes.cpp
    src/core/pipeline_builder_destination_nodes.cpp
    src/core/rtmp_lastframe_fallback_proxy_node.cpp
    src/core/persistent_output_leg.cpp
    src/core/frame_router.cpp
    src/core/frame_router_sink_node.cpp
    src/core/pipeline_builder_detector_nodes.cpp
    src/core/pipeline_builder_broker_nodes.cpp
    src/core/pipeline_builder_behavior_analysis_nodes.cpp
    src/core/pipeline_builder_other_nodes.cpp
    src/core/securt_pipeline_integration.cpp
    src/core/cvedix_validator.cpp
    src/solutions/solution_registry.cpp
    src/solutions/solution_storage.cpp
    src/models/solution_config.cpp
    src/models/create_instance_request.cpp
    src/core/uuid_generator.cpp
    src/core/platform_detector.cpp
    src/utils/gstreamer_checker.cpp
    src/utils/cvedix_mqtt_client_impl.cpp
    src/utils/mp4_finalizer.cpp
    src/utils/mp4_directory_watcher.cpp
    src/config/system_config.cpp
    src/core/securt_instance_registry.cpp
    src/core/securt_statistics_collector.cpp
    src/core/securt_instance_manager.cpp
    src/core/securt_line_storage.cpp
    src/core/securt_line_manager.cpp
    src/core/securt_feature_manager.cpp
    src/core/exclusion_area_manager.cpp
    src/core/analytics_entities_manager.cpp
    src/core/area_storage.cpp
    src/core/area_manager.cpp
    src/core/runtime_update_log.cpp
)

add_library(edgeos_core SHARED ${CORE_LIB_SOURCES})

target_include_directories(edgeos_core PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

# Link dependencies to core library
if(TARGET jsoncpp_lib)
    target_link_libraries(edgeos_core PUBLIC jsoncpp_lib)
elseif(JSONCPP_LIBRARIES)
    target_link_libraries(edgeos_core PUBLIC ${JSONCPP_LIBRARIES})
    target_include_directories(edgeos_core PUBLIC ${JSONCPP_INCLUDE_DIRS})
endif()

target_link_libraries(edgeos_core PUBLIC cvedix::cvedix_instance_sdk)

if(OpenCV_FOUND)
    if(TARGET opencv_core)
        target_link_libraries(edgeos_core PUBLIC ${OPENCV_BASE_LIBS})
    else()
        target_link_libraries(edgeos_core PUBLIC ${OPENCV_LIBS})
    endif()
    if(OpenCV_INCLUDE_DIRS)
        target_include_directories(edgeos_core PUBLIC ${OpenCV_INCLUDE_DIRS})
    endif()
endif()

# CVEDIX SDK include directory (when SDK is found) - required for pipeline_builder and broker nodes
if(EDGEOS_SDK_FOUND)
    target_include_directories(edgeos_core PUBLIC "${EDGEOS_SDK_ROOT}/include")
    # Add ASIO include path (required for SSE broker node)
    if(EXISTS "${EDGEOS_SDK_ROOT}/include/cvedix/third_party/asio/include")
        target_include_directories(edgeos_core PUBLIC "${EDGEOS_SDK_ROOT}/include/cvedix/third_party/asio/include")
    endif()
endif()

if(PLOG_INCLUDE_DIR)
    target_include_directories(edgeos_core PUBLIC ${PLOG_INCLUDE_DIR})
endif()

if(CEREAL_INCLUDE_DIR)
    target_include_directories(edgeos_core PUBLIC "${CMAKE_BINARY_DIR}")
    target_include_directories(edgeos_core PUBLIC "${CEREAL_INCLUDE_DIR}")
endif()

if(CPP_BASE64_INCLUDE_DIR)
    target_include_directories(edgeos_core PUBLIC "${CPP_BASE64_INCLUDE_DIR}")
endif()

if(GSTREAMER_FOUND OR GSTREAMER_LIBRARY)
    target_compile_definitions(edgeos_core PUBLIC CVEDIX_WITH_GSTREAMER)
    # Add GStreamer include directories and libraries
    if(GSTREAMER_FOUND)
        target_include_directories(edgeos_core PUBLIC ${GSTREAMER_INCLUDE_DIRS})
        target_link_libraries(edgeos_core PUBLIC ${GSTREAMER_LIBRARIES})
    else()
        # Fallback: try to find GStreamer headers manually
        find_path(GSTREAMER_INCLUDE_DIR
            NAMES gst/gst.h
            PATHS
                /usr/include/gstreamer-1.0
                /usr/local/include/gstreamer-1.0
                ${EDGEOS_SDK_ROOT}/include/gstreamer-1.0
        )
        if(GSTREAMER_INCLUDE_DIR)
            target_include_directories(edgeos_core PUBLIC ${GSTREAMER_INCLUDE_DIR})
        endif()
        if(GSTREAMER_LIBRARY)
            target_link_libraries(edgeos_core PUBLIC ${GSTREAMER_LIBRARY})
        endif()
    endif()
endif()

# Check for Eigen library (required by BYTETracker)
find_path(EIGEN3_INCLUDE_DIR
    NAMES Eigen/Core
    PATHS
        /usr/include/eigen3
        /usr/local/include/eigen3
        ${EDGEOS_SDK_ROOT}/include/eigen3
        /usr/include
        /usr/local/include
)
if(EIGEN3_INCLUDE_DIR)
    target_include_directories(edgeos_core PUBLIC ${EIGEN3_INCLUDE_DIR})
    message(STATUS "✓ Found Eigen3: ${EIGEN3_INCLUDE_DIR}")
else()
    message(WARNING "⚠ Eigen3 not found. BYTETracker may not compile.")
    message(WARNING "  To install: sudo apt-get install libeigen3-dev")
endif()

if(CVEDIX_WITH_MQTT)
    target_compile_definitions(edgeos_core PUBLIC CVEDIX_WITH_MQTT)
    if(MOSQUITTO_LIBRARY)
        target_link_libraries(edgeos_core PUBLIC ${MOSQUITTO_LIBRARY})
    elseif(MOSQUITTO_FOUND)
        target_link_libraries(edgeos_core PUBLIC ${MOSQUITTO_LIBRARIES})
    endif()
endif()

message(STATUS "✓ Edge AI Core library configured")

# Link edgeos_core to edgeos_api
target_link_libraries(edgeos_api PRIVATE edgeos_core)

# Link yaml-cpp if available
if(TARGET yaml-cpp)
    target_link_libraries(edgeos_api PRIVATE yaml-cpp)
    target_include_directories(edgeos_api PRIVATE ${YAML_CPP_INCLUDE_DIRS})
    target_compile_definitions(edgeos_api PRIVATE YAML_CPP_FOUND)
    message(STATUS "✓ yaml-cpp linked to edgeos_api")
elseif(yaml-cpp_FOUND)
    target_link_libraries(edgeos_api PRIVATE yaml-cpp)
    target_compile_definitions(edgeos_api PRIVATE YAML_CPP_FOUND)
    message(STATUS "✓ yaml-cpp linked to edgeos_api")
endif()

message(STATUS "✓ edgeos_api linked to edgeos_core")

# ============================================
# Edge AI Worker executable (subprocess)
# ============================================
set(WORKER_SOURCES
    src/worker/edgeos_worker.cpp
    src/worker/worker_handler.cpp
    src/worker/worker_handler_handlers.cpp
    src/worker/worker_handler_handlers_query.cpp
    src/worker/worker_handler_parse_build.cpp
    src/worker/worker_handler_lifecycle.cpp
    src/worker/worker_handler_output.cpp
    src/worker/worker_handler_hooks.cpp
    src/worker/worker_handler_runtime.cpp
    src/worker/worker_handler_config.cpp
    src/worker/worker_handler_source_nodes.cpp
    src/worker/worker_handler_hotswap.cpp
    src/worker/worker_handler_args.cpp
    src/worker/worker_json_utils.cpp
    src/worker/config_file_watcher.cpp
    src/worker/ipc_protocol.cpp
    src/worker/unix_socket.cpp
)

add_executable(edgeos_worker ${WORKER_SOURCES})
set_target_properties(edgeos_worker PROPERTIES RUNTIME_OUTPUT_NAME edgeos-worker)

target_compile_options(edgeos_worker PRIVATE -Wall -Wextra)

# Link core library (includes all dependencies)
target_link_libraries(edgeos_worker PRIVATE edgeos_core)

# Link pthread for threading
target_link_libraries(edgeos_worker PRIVATE pthread)

# Set RPATH for worker (SDK libs)
set_target_properties(edgeos_worker PROPERTIES
    BUILD_WITH_INSTALL_RPATH FALSE
    BUILD_RPATH_USE_ORIGIN TRUE
    BUILD_RPATH "${CMAKE_BINARY_DIR}/lib;${CMAKE_BINARY_DIR};${EDGEOS_SDK_LIB_DIRS};/usr/local/lib"
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;${EDGEOS_SDK_ROOT}/lib/cvedix;${EDGEOS_SDK_ROOT}/lib/opencv;/usr/local/lib"
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

message(STATUS "✓ Edge AI Worker executable configured")

# ============================================
# Optional: LD_PRELOAD wrapper for TCP FIN on rtmp_des close (when SDK cannot be changed)
# ============================================
add_subdirectory(support/rtmp_fin_wrapper)

# ============================================
# Install target (optional)
# ============================================
install(TARGETS edgeos_api edgeos_worker edgeos_core close_fin
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)

# ============================================
# Unit Tests
# ============================================
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
