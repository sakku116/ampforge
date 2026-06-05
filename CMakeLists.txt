cmake_minimum_required(VERSION 3.22)
project(GtrFxSim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.2
)

FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(GtrFxSim
    PRODUCT_NAME "Guitar VST3 Host"
)

juce_add_console_app(GuitarVST3ScanWorker
    PRODUCT_NAME "Guitar VST3 Scan Worker"
)

juce_generate_juce_header(GtrFxSim)

target_sources(GtrFxSim
    PRIVATE
        src/main.cpp
        src/AudioEngine.h
        src/AudioEngine.cpp
        src/PluginScanner.h
        src/PluginScanner.cpp
        src/PluginChain.h
        src/PluginChain.cpp
        src/PluginHost.h
        src/PluginHost.cpp
        src/MainComponent.h
        src/MainComponent.cpp
        src/HostDebug.h
        src/PluginScanGuard.h
        src/PluginScanGuard.cpp
        src/ScanSubprocess.h
        src/ScanSubprocess.cpp
)

juce_generate_juce_header(GuitarVST3ScanWorker)

target_sources(GuitarVST3ScanWorker
    PRIVATE
        src/scan_worker_main.cpp
        src/ScanSubprocess.h
        src/ScanSubprocess.cpp
)

target_compile_definitions(GuitarVST3ScanWorker
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
)

target_link_libraries(GuitarVST3ScanWorker
    PRIVATE
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_dependencies(GtrFxSim GuitarVST3ScanWorker)

add_custom_command(TARGET GuitarVST3ScanWorker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:GuitarVST3ScanWorker>"
        "$<TARGET_FILE_DIR:GtrFxSim>/GuitarVST3ScanWorker.exe"
    COMMENT "Copy scan worker next to host executable"
)

if(MSVC)
    target_compile_options(GtrFxSim PRIVATE /EHa)
endif()

# ---------------------------------------------------------------------------
# ASIO support (optional) — requires Steinberg ASIO SDK
#   Download from: https://www.steinberg.net/asiosdk/
#   Then set: -DASIO_SDK_DIR="C:/path/to/asiosdk" at configure time
#   OR place the SDK folder next to this CMakeLists.txt as "asiosdk/"
# ---------------------------------------------------------------------------
option(ENABLE_ASIO "Build with ASIO driver support" OFF)

if(NOT ENABLE_ASIO AND ASIO_SDK_DIR)
    set(ENABLE_ASIO ON)
endif()

if(NOT ENABLE_ASIO)
    foreach(_candidate
            "${CMAKE_SOURCE_DIR}/asiosdk"
            "${CMAKE_SOURCE_DIR}/asio"
            "${CMAKE_SOURCE_DIR}/ASIO_SDK"
            "C:/ASIOSDK"
            "C:/Program Files/ASIO SDK")
        if(EXISTS "${_candidate}/common/iasiodrv.h")
            set(ASIO_SDK_DIR "${_candidate}" CACHE PATH "Steinberg ASIO SDK directory")
            set(ENABLE_ASIO ON)
            break()
        endif()
    endforeach()
endif()

set(_juce_asio_flag 0)
if(ENABLE_ASIO)
    message(STATUS "ASIO enabled. SDK: ${ASIO_SDK_DIR}")
    set(_juce_asio_flag 1)
    target_include_directories(GtrFxSim PRIVATE "${ASIO_SDK_DIR}/common")
else()
    message(STATUS "ASIO not found — building with WASAPI/DirectSound only. "
                   "Set -DASIO_SDK_DIR=<path> to enable ASIO.")
endif()

target_compile_definitions(GtrFxSim
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_ASIO=${_juce_asio_flag}
)

target_link_libraries(GtrFxSim
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

include(cmake/GenerateClangdCompileDb.cmake)
guitar_host_export_clangd_compile_db(GtrFxSim)
