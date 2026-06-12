cmake_minimum_required(VERSION 3.22)
project(AmpForge VERSION 0.2.0 LANGUAGES CXX)

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

juce_add_gui_app(AmpForge
    PRODUCT_NAME "Amp Forge"
)

juce_add_console_app(AmpForgeScanWorker
    PRODUCT_NAME "AmpForgeScanWorker"
)

juce_generate_juce_header(AmpForge)

target_sources(AmpForge
    PRIVATE
        src/main.cpp
        src/AppDataDir.h
        src/AudioEngine.h
        src/AudioEngine.cpp
        src/PluginScanner.h
        src/PluginScanner.cpp
        src/PluginChain.h
        src/PluginChain.cpp
        src/Preset.h
        src/Preset.cpp
        src/ControlMap.h
        src/ControlMap.cpp
        src/TemplateManager.h
        src/TemplateManager.cpp
        src/PluginHost.h
        src/PluginHost.cpp
        src/ToneForgeLookAndFeel.h
        src/ToneForgeLookAndFeel.cpp
        src/ChainListBox.h
        src/ChainListBox.cpp
        src/MainComponent.h
        src/MainComponent.cpp
        src/HostDebug.h
        src/PluginScanGuard.h
        src/PluginScanGuard.cpp
        src/ScanSubprocess.h
        src/ScanSubprocess.cpp
)

juce_generate_juce_header(AmpForgeScanWorker)

target_sources(AmpForgeScanWorker
    PRIVATE
        src/scan_worker_main.cpp
        src/ScanSubprocess.h
        src/ScanSubprocess.cpp
)

target_compile_definitions(AmpForgeScanWorker
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
)

target_link_libraries(AmpForgeScanWorker
    PRIVATE
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_dependencies(AmpForge AmpForgeScanWorker)

add_custom_command(TARGET AmpForgeScanWorker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:AmpForgeScanWorker>"
        "$<TARGET_FILE_DIR:AmpForge>/AmpForgeScanWorker.exe"
    COMMENT "Copy scan worker next to host executable"
)

if(MSVC)
    target_compile_options(AmpForge PRIVATE /EHa)
endif()

# ---------------------------------------------------------------------------
# Link-Time / Interprocedural Optimization for optimized builds only.
# Smaller binaries and faster DSP; left off for Debug to keep builds quick.
# ---------------------------------------------------------------------------
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_output LANGUAGES CXX)

if(_ipo_supported)
    foreach(_tgt AmpForge AmpForgeScanWorker)
        set_property(TARGET ${_tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE        ON)
        set_property(TARGET ${_tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
        set_property(TARGET ${_tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL      ON)
    endforeach()
    message(STATUS "IPO/LTO enabled for optimized configurations.")
else()
    message(STATUS "IPO/LTO not supported: ${_ipo_output}")
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
            "${CMAKE_SOURCE_DIR}/ASIOSDK"
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
    target_include_directories(AmpForge PRIVATE "${ASIO_SDK_DIR}/common")
else()
    message(STATUS "ASIO not found — building with WASAPI/DirectSound only. "
                   "Set -DASIO_SDK_DIR=<path> to enable ASIO.")
endif()

# ---------------------------------------------------------------------------
# VST2 support (optional) — requires Steinberg VST2 SDK
#   Headers needed: pluginterfaces/vst2.x/aeffect.h
#   Set: -DVST2_SDK_DIR="C:/path/to/vst2sdk" at configure time
#   OR place the SDK folder next to this CMakeLists.txt as "vst2sdk/"
# ---------------------------------------------------------------------------
option(ENABLE_VST2 "Build with VST2 plugin hosting support" OFF)

if(NOT ENABLE_VST2 AND VST2_SDK_DIR)
    set(ENABLE_VST2 ON)
endif()

if(NOT ENABLE_VST2)
    foreach(_candidate
            "${CMAKE_SOURCE_DIR}/vst2sdk"
            "${CMAKE_SOURCE_DIR}/VST_SDK"
            "${CMAKE_SOURCE_DIR}/vstsdk2.4"
            "C:/VST2_SDK"
            "C:/Program Files/Steinberg/VST2 SDK")
        if(EXISTS "${_candidate}/pluginterfaces/vst2.x/aeffect.h")
            set(VST2_SDK_DIR "${_candidate}" CACHE PATH "Steinberg VST2 SDK directory")
            set(ENABLE_VST2 ON)
            break()
        endif()
    endforeach()
endif()

set(_juce_vst2_flag 0)
if(ENABLE_VST2)
    message(STATUS "VST2 enabled. SDK: ${VST2_SDK_DIR}")
    set(_juce_vst2_flag 1)
    target_include_directories(AmpForge             PRIVATE "${VST2_SDK_DIR}")
    target_include_directories(AmpForgeScanWorker PRIVATE "${VST2_SDK_DIR}")
    target_compile_definitions(AmpForgeScanWorker PRIVATE JUCE_PLUGINHOST_VST=1)
else()
    message(STATUS "VST2 SDK not found — building without VST2 support. "
                   "Set -DVST2_SDK_DIR=<path> to enable.")
endif()

target_compile_definitions(AmpForge
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_VST=${_juce_vst2_flag}
        JUCE_ASIO=${_juce_asio_flag}
        AMPFORGE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(AmpForge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

include(cmake/GenerateClangdCompileDb.cmake)
ampforge_export_clangd_compile_db(AmpForge)
