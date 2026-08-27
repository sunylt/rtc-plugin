PROJECT(rtc_plugin VERSION ${obs-studio-node_VERSION})
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Detect Architecture (Bitness)
IF( NOT CLANG_ANALYZE_CONFIG)
    math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")
ENDIF()

if(WIN32)
    set(PROJECT_PRODUCT_NAME "RTC Plugin")
    set(PROJECT_COPYRIGHT "RTC © 2025 - 2025")

    configure_file(
        ${obs-studio-node_SOURCE_DIR}/cmake/version.rc.in
        ${PROJECT_BINARY_DIR}/version.rc
        @ONLY
    )
    set(LIBOBS_PLATFORM "windows64")
endif()

if(APPLE)
    set(LIBOBS_PLATFORM "osx")
endif()


# Getting LIBOBS_VERSION from azure script



include(NodeJS)

set(STREAMLABS_BUILD ON CACHE BOOL "Whether to build for streamlabs-obs")

nodejs_init()

SET(rtc_plugin_SOURCES
    "${PROJECT_SOURCE_DIR}/source/main.cpp"
    "${PROJECT_SOURCE_DIR}/source/MediaPush.h"
    "${PROJECT_SOURCE_DIR}/source/MediaPush.cpp"
    "${PROJECT_SOURCE_DIR}/source/shared-memory-queue.h"
    "${PROJECT_SOURCE_DIR}/source/shared-memory-queue.cpp"
    "${PROJECT_SOURCE_DIR}/source/plugin_base.h"
    "${PROJECT_SOURCE_DIR}/source/plugin_c_api.cpp"
    "${PROJECT_SOURCE_DIR}/source/plugin_c_api.h"
    "${PROJECT_SOURCE_DIR}/source/resource.h"
    "${PROJECT_SOURCE_DIR}/source/RtcRawDataPlugin.cpp"
    "${PROJECT_SOURCE_DIR}/source/RtcRawDataPlugin.hpp"
    "${PROJECT_SOURCE_DIR}/source/Tracer.cpp"
    "${PROJECT_SOURCE_DIR}/source/Tracer.h"
)

add_nodejs_module(
    rtc_plugin
    ${rtc_plugin_SOURCES}
)

if (WIN32)
	set(PROJECT_LIBRARIES
		lib-streamlabs-ipc
        DelayImp.lib
	)
	set(PROJECT_INCLUDE_PATHS
		"${CMAKE_SOURCE_DIR}/source"
        "${PROJECT_SOURCE_DIR}/source/include"
        "${PROJECT_SOURCE_DIR}/include"
		"${lib-streamlabs-ipc_SOURCE_DIR}/include"
	)
endif ()

if(WIN32)
    target_sources(rtc_plugin PUBLIC ${PROJECT_BINARY_DIR}/version.rc)
    target_link_libraries(rtc_plugin lib-streamlabs-ipc)
else ()
    target_link_libraries(rtc_plugin lib-streamlabs-ipc ${COREFOUNDATION} ${COCOA} ${AVFOUNDATION} ${Security} ${Foundation})
endif()

target_include_directories(
    rtc_plugin
    PUBLIC
        "${CMAKE_SOURCE_DIR}/source/"
        "${CMAKE_SOURCE_DIR}/lib-streamlabs-ipc/include"
        ${PROJECT_INCLUDE_PATHS}
)

# Include N-API wrappers
execute_process(COMMAND node -p "require('node-addon-api').include"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE NODE_ADDON_API_DIR
        )
string(REPLACE "\n" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE ${NODE_ADDON_API_DIR})
target_link_libraries(${PROJECT_NAME} ${LIBOBS_LIBRARIES})

# Define NAPI_VERSION
add_definitions(-DNAPI_VERSION=7)

#Define the OSN_VERSION
add_compile_definitions(OSN_VERSION=\"$ENV{tagartifact}\") 
set_target_properties(
    rtc_plugin
    PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION}
    PREFIX "" SUFFIX ".node"
)

target_compile_definitions(rtc_plugin PRIVATE BUILDING_NODE_EXTENSION)

if(WIN32)
    target_compile_definitions(
        rtc_plugin
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            UNICODE
            _UNICODE
    )
endif()


IF( NOT CLANG_ANALYZE_CONFIG)
    cppcheck_add_project(${PROJECT_NAME})
ENDIF()

IF(WIN32 AND NOT CLANG_ANALYZE_CONFIG)
    install(FILES $<TARGET_PDB_FILE:rtc_plugin> DESTINATION ./ OPTIONAL)
ENDIF()



