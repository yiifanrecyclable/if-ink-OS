# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
option(IFINK_INKBUDDY "Build the local inkbuddy r2t2 test firmware" OFF)
option(IFINK_DOCUMENT_TIMING "Build the local document timing test firmware" OFF)
set(PROJECT_VER "0.2.2-beta.2")
# 由同一开关派生版本和功能。/ Derive both the version and the feature from the same option.
if(IFINK_DOCUMENT_TIMING AND IFINK_INKBUDDY)
    message(FATAL_ERROR "Choose one local firmware test")
endif()
if(IFINK_INKBUDDY)
    set(PROJECT_VER "0.2.1-beta-r2t2")
elseif(IFINK_DOCUMENT_TIMING)
    set(PROJECT_VER "0.2.2-beta-r1t1")
endif()
if(DEFINED CACHE{PROJECT_VER} AND
   (IFINK_INKBUDDY OR "$CACHE{PROJECT_VER}" MATCHES "-r2t"))
    if(NOT "$CACHE{PROJECT_VER}" STREQUAL PROJECT_VER)
        message(FATAL_ERROR "Use IFINK_INKBUDDY to select the local r2t2 version")
    endif()
endif()
project(Read_Pico_IF)

# 一分钟的深睡诊断仅用于本地唤醒测试。/ Keep the one-minute interval local to wake diagnostics.
if(PROJECT_VER MATCHES "^0[.]2[.]1-beta-r1t[3456]$")
    idf_component_get_property(ifink_main_lib main COMPONENT_LIB)
    target_compile_definitions(${ifink_main_lib} PRIVATE IFINK_WAKE_TIMING_TEST=1)
    if(PROJECT_VER MATCHES "^0[.]2[.]1-beta-r1t[456]$")
        target_compile_definitions(${ifink_main_lib} PRIVATE IFINK_WAKE_TONE_TEST=1)
    endif()
    if(PROJECT_VER STREQUAL "0.2.1-beta-r1t5")
        target_compile_definitions(${ifink_main_lib} PRIVATE IFINK_BANNER_LAYER_TEST=1)
    endif()
endif()

# 固件描述时间统一为 UTC，与概览页标识一致；保留已有编译缓存启动器。
# Compile app-description timestamps in UTC to match Overview; preserve any compiler-cache launcher.
idf_component_get_property(app_desc_lib esp_app_format COMPONENT_LIB)
get_target_property(app_desc_launcher ${app_desc_lib} C_COMPILER_LAUNCHER)
if(NOT app_desc_launcher)
    set(app_desc_launcher "")
endif()
set(app_desc_utc_launcher "${CMAKE_COMMAND}" -E env TZ=UTC)
if(app_desc_launcher)
    list(APPEND app_desc_utc_launcher ${app_desc_launcher})
endif()
set_property(TARGET ${app_desc_lib} PROPERTY C_COMPILER_LAUNCHER "${app_desc_utc_launcher}")
