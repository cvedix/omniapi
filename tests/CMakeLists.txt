# Unit Tests Configuration
cmake_minimum_required(VERSION 3.14)

# Include Google Test using FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

# Test sources - organized by feature in unit/ subdirectories
set(TEST_SOURCES
    # Core API tests
    unit/Core_API/test_health_handler.cpp
    unit/Core_API/test_version_handler.cpp
    unit/Core_API/test_swagger_handler.cpp
    unit/Core_API/test_watchdog_handler.cpp
    unit/Core_API/test_endpoints_handler.cpp
    unit/Core_API/test_config_handler.cpp
    unit/Core_API/test_system_info_handler.cpp
    unit/Core_API/test_system_handler.cpp
    unit/Core_API/test_system_config_manager.cpp
    unit/Core_API/test_preferences_manager.cpp
    unit/Core_API/test_decoder_detector.cpp
    unit/Core_API/test_inference_session.cpp
    unit/Core_API/test_ai_processor.cpp
    unit/Core_API/test_metrics_handler.cpp
    
    # Instance Management tests
    unit/Instance_Management/test_instance_storage.cpp
    unit/Instance_Management/test_instance_update.cpp
    unit/Instance_Management/test_instance_status_summary.cpp
    unit/Instance_Management/test_instance_get_config.cpp
    unit/Instance_Management/test_instance_configure_stream.cpp
    unit/Instance_Management/test_create_instance_handler.cpp
    unit/Instance_Management/test_instance_fps_handler.cpp
    
    # Recognition tests
    unit/Recognition/test_recognition_handler.cpp
    
    # Solutions tests
    unit/Solutions/test_solution_handler.cpp
    
    # Groups tests
    unit/Groups/test_group_handler.cpp
    
    # Nodes tests
    unit/Nodes/test_node_handler.cpp
    # unit/Nodes/test_node_pool_handler.cpp  # Commented out: node_pool_handler.cpp implementation missing
    
    # Analytics tests
    unit/Analytics/test_lines_handler.cpp
    unit/Analytics/test_jams_handler.cpp
    unit/Analytics/test_stops_handler.cpp
    unit/Analytics/test_ba_jam_detection_params.cpp
    unit/Analytics/test_ba_jam_pipeline.cpp
    unit/Analytics/test_securt_handler.cpp
    unit/Analytics/test_area_handler.cpp
    # unit/Analytics/test_ai_handler.cpp  # Commented out: requires C++20 for counting_semaphore, but project uses C++17
    
    # Main test entry point
    unit/test_main.cpp
)

# Create test executable
add_executable(edgeos_api_tests ${TEST_SOURCES})

# Link libraries
# Note: We use gtest (not gtest_main) because we have our own main() in test_main.cpp
# This allows us to add custom signal handlers and test event listeners
target_link_libraries(edgeos_api_tests PRIVATE
    gtest
)

# Link Drogon (same as main project)
if(TARGET Drogon::Drogon)
    target_link_libraries(edgeos_api_tests PRIVATE Drogon::Drogon)
elseif(TARGET drogon)
    target_link_libraries(edgeos_api_tests PRIVATE drogon)
endif()

# Link source files (we need to compile them for tests)
target_sources(edgeos_api_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/api/health_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/version_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/swagger_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/scalar_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/watchdog_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/endpoints_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/solution_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/instance_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/recognition_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/config_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/system_info_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/system_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/core/system_config_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/preferences_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/decoder_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/api/metrics_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/node_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/group_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/create_instance_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/instance_fps_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/lines_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/jams_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/stops_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/securt_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/api/area_handler.cpp
    # ${CMAKE_SOURCE_DIR}/src/api/ai_handler.cpp  # Commented out: requires C++20 for counting_semaphore, but project uses C++17
    ${CMAKE_SOURCE_DIR}/src/core/watchdog.cpp
    ${CMAKE_SOURCE_DIR}/src/core/health_monitor.cpp
    ${CMAKE_SOURCE_DIR}/src/core/endpoint_monitor.cpp
    ${CMAKE_SOURCE_DIR}/src/core/request_middleware.cpp
    ${CMAKE_SOURCE_DIR}/src/core/metrics_interceptor.cpp
    ${CMAKE_SOURCE_DIR}/src/core/event_queue.cpp
    ${CMAKE_SOURCE_DIR}/src/core/frame_input_queue.cpp
    ${CMAKE_SOURCE_DIR}/src/core/codec_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/frame_decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/core/frame_processor.cpp
    ${CMAKE_SOURCE_DIR}/src/core/inference_session.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ai_runtime_facade.cpp
    ${CMAKE_SOURCE_DIR}/src/core/pipeline_helper.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ai_processor.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ai_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/instances/instance_storage.cpp
    ${CMAKE_SOURCE_DIR}/src/instances/instance_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/instances/instance_registry_rtmp_monitor.cpp
    ${CMAKE_SOURCE_DIR}/src/instances/instance_state_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/instances/inprocess_instance_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/instances/subprocess_instance_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/instances/instance_manager_factory.cpp
    ${CMAKE_SOURCE_DIR}/src/worker/worker_supervisor.cpp
    ${CMAKE_SOURCE_DIR}/src/worker/unix_socket.cpp
    ${CMAKE_SOURCE_DIR}/src/worker/ipc_protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/core/pipeline_builder.cpp
    ${CMAKE_SOURCE_DIR}/src/core/pipeline_builder_model_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/core/pipeline_builder_request_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/pipeline_builder_source_nodes.cpp
    ${CMAKE_SOURCE_DIR}/src/core/pipeline_builder_destination_nodes.cpp
    ${CMAKE_SOURCE_DIR}/src/core/pipeline_builder_detector_nodes.cpp
    ${CMAKE_SOURCE_DIR}/src/core/pipeline_builder_broker_nodes.cpp
    ${CMAKE_SOURCE_DIR}/src/core/pipeline_builder_behavior_analysis_nodes.cpp
    ${CMAKE_SOURCE_DIR}/src/core/pipeline_builder_other_nodes.cpp
    ${CMAKE_SOURCE_DIR}/src/core/securt_pipeline_integration.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cvedix_validator.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/cvedix_mqtt_client_impl.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/mp4_finalizer.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/mp4_directory_watcher.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/gstreamer_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/solutions/solution_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/solutions/solution_storage.cpp
    ${CMAKE_SOURCE_DIR}/src/config/system_config.cpp
    ${CMAKE_SOURCE_DIR}/src/core/platform_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/models/solution_config.cpp
    ${CMAKE_SOURCE_DIR}/src/models/update_instance_request.cpp
    ${CMAKE_SOURCE_DIR}/src/models/create_instance_request.cpp
    ${CMAKE_SOURCE_DIR}/src/core/uuid_generator.cpp
    ${CMAKE_SOURCE_DIR}/src/core/node_pool_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/node_template_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/core/node_storage.cpp
    ${CMAKE_SOURCE_DIR}/src/core/performance_monitor.cpp
    ${CMAKE_SOURCE_DIR}/src/core/backpressure_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/core/adaptive_queue_size_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/groups/group_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/groups/group_storage.cpp
    ${CMAKE_SOURCE_DIR}/src/models/group_info.cpp
    ${CMAKE_SOURCE_DIR}/src/core/priority_queue.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ai_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/core/rate_limiter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/resource_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/shutdown_flag.cpp
    ${CMAKE_SOURCE_DIR}/src/core/securt_instance_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/securt_instance_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/core/securt_statistics_collector.cpp
    ${CMAKE_SOURCE_DIR}/src/core/securt_line_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/securt_line_storage.cpp
    ${CMAKE_SOURCE_DIR}/src/core/securt_feature_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/exclusion_area_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/area_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/area_storage.cpp
    ${CMAKE_SOURCE_DIR}/src/core/analytics_entities_manager.cpp
)

# Link jsoncpp if available
if(TARGET jsoncpp_lib)
    target_link_libraries(edgeos_api_tests PRIVATE jsoncpp_lib)
elseif(Jsoncpp_FOUND)
    target_link_libraries(edgeos_api_tests PRIVATE ${JSONCPP_LIBRARIES})
    target_include_directories(edgeos_api_tests PRIVATE ${JSONCPP_INCLUDE_DIRS})
endif()

# ============================================
# Check CUDA availability (same as main project)
# ============================================
set(OPENCV_CUDA_AVAILABLE FALSE)
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

foreach(CUDA_LIB ${CUDA_LIB_NAMES})
    find_library(CUDA_LIB_TEMP
        NAMES ${CUDA_LIB}
        PATHS
            /usr/local/lib
            /usr/local/lib/x86_64-linux-gnu
            /usr/lib
            /usr/lib/x86_64-linux-gnu
        NO_DEFAULT_PATH
    )
    if(CUDA_LIB_TEMP)
        set(OPENCV_CUDA_AVAILABLE TRUE)
        break()
    endif()
endforeach()

# Link CVEDIX SDK (required for instance_registry and pipeline_builder)
if(TARGET cvedix::cvedix_instance_sdk)
    target_link_libraries(edgeos_api_tests PRIVATE cvedix::cvedix_instance_sdk)
    message(STATUS "✓ CVEDIX SDK linked to tests")

    # Handle OpenCV CUDA libraries - filter them out if not available
    get_target_property(CVEDIX_LINK_LIBS cvedix::cvedix_instance_sdk INTERFACE_LINK_LIBRARIES)
    if(CVEDIX_LINK_LIBS)
        set(FILTERED_LIBS "")
        foreach(LIB ${CVEDIX_LINK_LIBS})
            set(IS_CUDA_LIB FALSE)
            foreach(CUDA_LIB_NAME ${CUDA_LIB_NAMES})
                if("${LIB}" MATCHES "${CUDA_LIB_NAME}")
                    set(IS_CUDA_LIB TRUE)
                    break()
                endif()
            endforeach()

            if(IS_CUDA_LIB)
                if(OPENCV_CUDA_AVAILABLE)
                    list(APPEND FILTERED_LIBS "${LIB}")
                endif()
            else()
                list(APPEND FILTERED_LIBS "${LIB}")
            endif()
        endforeach()

        if(NOT "${CVEDIX_LINK_LIBS}" STREQUAL "${FILTERED_LIBS}")
            set_target_properties(cvedix::cvedix_instance_sdk PROPERTIES
                INTERFACE_LINK_LIBRARIES "${FILTERED_LIBS}"
            )
            if(OPENCV_CUDA_AVAILABLE)
                message(STATUS "✓ CVEDIX SDK configured with CUDA support for tests")
            else()
                message(STATUS "✓ Filtered unavailable CUDA libraries from CVEDIX SDK for tests")
            endif()
        endif()
    endif()

    # Use linker flags based on CUDA availability
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        if(OPENCV_CUDA_AVAILABLE)
            target_link_options(edgeos_api_tests PRIVATE
                -Wl,--as-needed
            )
        else()
            target_link_options(edgeos_api_tests PRIVATE
                -Wl,--as-needed
                -Wl,--allow-shlib-undefined
            )
            message(STATUS "✓ Added linker flags to handle missing CUDA libraries for tests")
        endif()
    endif()
endif()

# Link mosquitto library if MQTT support is enabled (required for JSON MQTT broker node)
if(CVEDIX_WITH_MQTT)
    find_library(MOSQUITTO_LIBRARY
        NAMES mosquitto
        PATHS
            /usr/lib
            /usr/lib/x86_64-linux-gnu
            /usr/local/lib
            ${EDGEOS_SDK_ROOT}/lib/cvedix
    )

    if(MOSQUITTO_LIBRARY)
        target_link_libraries(edgeos_api_tests PRIVATE ${MOSQUITTO_LIBRARY})
        message(STATUS "✓ Linked mosquitto library to tests: ${MOSQUITTO_LIBRARY}")
    else()
        # Try pkg-config
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(MOSQUITTO QUIET mosquitto)
            if(MOSQUITTO_FOUND)
                target_link_libraries(edgeos_api_tests PRIVATE ${MOSQUITTO_LIBRARIES})
                target_include_directories(edgeos_api_tests PRIVATE ${MOSQUITTO_INCLUDE_DIRS})
                message(STATUS "✓ Linked mosquitto library to tests via pkg-config: ${MOSQUITTO_LIBRARIES}")
            endif()
        endif()
    endif()
endif()

# Link OpenCV (required for CVEDIX SDK)
if(OpenCV_FOUND)
    if(TARGET opencv_core)
        # CVEDIX SDK requires opencv_dnn module (for cv::dnn::Net)
        # opencv_objdetect is required for cv::FaceDetectorYN
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
            foreach(CUDA_LIB_NAME ${CUDA_LIB_NAMES})
                if(TARGET opencv_${CUDA_LIB_NAME})
                    list(APPEND OPENCV_BASE_LIBS opencv_${CUDA_LIB_NAME})
                endif()
            endforeach()
            message(STATUS "✓ Linking OpenCV with CUDA support for tests")
        else()
            message(STATUS "✓ Linking OpenCV without CUDA support for tests")
        endif()

        target_link_libraries(edgeos_api_tests PRIVATE ${OPENCV_BASE_LIBS})
    else()
        # Fallback to pkg-config libraries
        set(OPENCV_LIBS ${OpenCV_LIBRARIES})

        # Filter CUDA libraries based on availability
        if(NOT OPENCV_CUDA_AVAILABLE)
            list(FILTER OPENCV_LIBS EXCLUDE REGEX "opencv_cuda")
            message(STATUS "✓ Filtered CUDA libraries from OpenCV pkg-config for tests (CUDA not available)")
        else()
            message(STATUS "✓ Using OpenCV libraries from pkg-config for tests (with CUDA)")
        endif()

        target_link_libraries(edgeos_api_tests PRIVATE ${OPENCV_LIBS})
    endif()
    message(STATUS "✓ OpenCV linked to tests")
endif()

# Add include directories for cereal and CVEDIX (same as main project)
# Note: CEREAL_INCLUDE_DIR is set in parent CMakeLists.txt
if(DEFINED CEREAL_INCLUDE_DIR AND CEREAL_INCLUDE_DIR)
    target_include_directories(edgeos_api_tests PRIVATE "${CMAKE_BINARY_DIR}")
    target_include_directories(edgeos_api_tests PRIVATE "${CEREAL_INCLUDE_DIR}")
endif()

# Add CVEDIX SDK include directories (required for broker nodes with ASIO)
if(EXISTS "${EDGEOS_SDK_ROOT}/include")
    target_include_directories(edgeos_api_tests PRIVATE "${EDGEOS_SDK_ROOT}/include")
    # Add ASIO include path (required for SSE broker node)
    target_include_directories(edgeos_api_tests PRIVATE "${EDGEOS_SDK_ROOT}/include/cvedix/third_party/asio/include")
endif()

# Add Plog include directory (same as main project)
if(DEFINED PLOG_INCLUDE_DIR AND PLOG_INCLUDE_DIR)
    target_include_directories(edgeos_api_tests PRIVATE ${PLOG_INCLUDE_DIR})
endif()

# Add hwinfo include directory (from FetchContent or submodule)
# hwinfo is typically in build/_deps/hwinfo-src/include or via target
if(EXISTS "${CMAKE_BINARY_DIR}/_deps/hwinfo-src/include")
    target_include_directories(edgeos_api_tests PRIVATE "${CMAKE_BINARY_DIR}/_deps/hwinfo-src/include")
elseif(TARGET lfreist-hwinfo::hwinfo)
    # If hwinfo target exists, get its include directories
    get_target_property(HWINFO_INCLUDE_DIRS lfreist-hwinfo::hwinfo INTERFACE_INCLUDE_DIRECTORIES)
    if(HWINFO_INCLUDE_DIRS)
        target_include_directories(edgeos_api_tests PRIVATE ${HWINFO_INCLUDE_DIRS})
    endif()
endif()

# Link stdc++fs for std::experimental::filesystem (if needed)
# Some systems require explicit linking for filesystem library
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(edgeos_api_tests PRIVATE stdc++fs)
endif()

# Set compile definitions (same as main project)
target_compile_definitions(edgeos_api_tests PRIVATE
    PROJECT_VERSION="${PROJECT_VERSION}"
    BUILD_TIME="Test Build"
    GIT_COMMIT="test"
)

# Add CVEDIX SDK feature flags (same as main project)
# Check for GStreamer support
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GSTREAMER QUIET gstreamer-1.0)
    if(GSTREAMER_FOUND)
        target_compile_definitions(edgeos_api_tests PRIVATE CVEDIX_WITH_GSTREAMER)
        target_include_directories(edgeos_api_tests PRIVATE ${GSTREAMER_INCLUDE_DIRS})
        target_link_libraries(edgeos_api_tests PRIVATE ${GSTREAMER_LIBRARIES})
    endif()
else()
    find_library(GSTREAMER_LIBRARY
        NAMES gstreamer-1.0
        PATHS
            /usr/lib
            /usr/lib/x86_64-linux-gnu
            /usr/local/lib
    )
    if(GSTREAMER_LIBRARY)
        target_compile_definitions(edgeos_api_tests PRIVATE CVEDIX_WITH_GSTREAMER)
        # Fallback: try to find GStreamer headers manually
        find_path(GSTREAMER_INCLUDE_DIR
            NAMES gst/gst.h
            PATHS
                /usr/include/gstreamer-1.0
                /usr/local/include/gstreamer-1.0
                ${EDGEOS_SDK_ROOT}/include/gstreamer-1.0
        )
        if(GSTREAMER_INCLUDE_DIR)
            target_include_directories(edgeos_api_tests PRIVATE ${GSTREAMER_INCLUDE_DIR})
        endif()
        target_link_libraries(edgeos_api_tests PRIVATE ${GSTREAMER_LIBRARY})
    endif()
endif()

# Add Eigen include directory (required by BYTETracker)
if(DEFINED EIGEN3_INCLUDE_DIR AND EIGEN3_INCLUDE_DIR)
    target_include_directories(edgeos_api_tests PRIVATE ${EIGEN3_INCLUDE_DIR})
else()
    # Try to find Eigen if not already found
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
        target_include_directories(edgeos_api_tests PRIVATE ${EIGEN3_INCLUDE_DIR})
    endif()
endif()

# Add MQTT support if enabled
if(CVEDIX_WITH_MQTT)
    target_compile_definitions(edgeos_api_tests PRIVATE CVEDIX_WITH_MQTT)
endif()

# Link OpenSSL Crypto (required for ai_cache SHA256)
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    target_link_libraries(edgeos_api_tests PRIVATE OpenSSL::Crypto)
endif()

# Link hwinfo (required for system_info_handler)
if(TARGET lfreist-hwinfo::hwinfo)
    target_link_libraries(edgeos_api_tests PRIVATE lfreist-hwinfo::hwinfo)
endif()

# Enable testing
enable_testing()
add_test(NAME edgeos_api_tests COMMAND edgeos_api_tests)
# Set LD_LIBRARY_PATH for test to find OpenCV libraries
set_tests_properties(edgeos_api_tests PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=/opt/edgeos-sdk/lib/cvedix:/opt/edgeos-sdk/lib/opencv:/usr/local/lib:$<TARGET_FILE_DIR:edgeos_api_tests>/../lib:$ENV{LD_LIBRARY_PATH}"
)
