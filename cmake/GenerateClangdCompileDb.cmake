# Writes ${CMAKE_BINARY_DIR}/compile_commands.json for clangd (MSVC has no export).

function(_ampforge_escape_json_path out path)
    file(TO_CMAKE_PATH "${path}" _path)
    string(REPLACE "\\" "/" _path "${_path}")
    set(${out} "${_path}" PARENT_SCOPE)
endfunction()

function(ampforge_export_clangd_compile_db target)
    get_target_property(_sources ${target} SOURCES)
    if(NOT _sources)
        return()
    endif()

    set(_juce_modules "${CMAKE_BINARY_DIR}/_deps/juce-src/modules")
    set(_juce_generated "${CMAKE_BINARY_DIR}/AmpForge_artefacts/JuceLibraryCode")
    set(_vst3_sdk "${_juce_modules}/juce_audio_processors/format_types/VST3_SDK")

    set(_include_flags
        -I${_juce_generated}
        -I${_juce_modules}
        -I${_vst3_sdk}
    )

    get_target_property(_defs ${target} COMPILE_DEFINITIONS)
    set(_define_flags)
    foreach(_def IN LISTS _defs)
        if(_def MATCHES "(.+)=([^=]+)")
            list(APPEND _define_flags "-D${CMAKE_MATCH_1}=${CMAKE_MATCH_2}")
        elseif(_def)
            list(APPEND _define_flags "-D${_def}")
        endif()
    endforeach()

    # JUCE module flags come from linked targets, not COMPILE_DEFINITIONS at configure time.
    foreach(_module juce_audio_utils juce_audio_processors juce_gui_extra juce_gui_basics
            juce_graphics juce_events juce_core juce_data_structures juce_audio_basics
            juce_audio_formats juce_audio_devices)
        list(APPEND _define_flags "-DJUCE_MODULE_AVAILABLE_${_module}=1")
    endforeach()
    list(APPEND _define_flags -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DWIN32 -D_WINDOWS -D_DEBUG -DDEBUG=1)

    set(_base_flags -std=c++20 ${_include_flags} ${_define_flags})
    list(JOIN _base_flags " " _base_flags_str)

    _ampforge_escape_json_path(_source_dir_json "${CMAKE_SOURCE_DIR}")
    _ampforge_escape_json_path(_binary_dir_json "${CMAKE_BINARY_DIR}")

    set(_json "[\n")
    set(_first TRUE)

    foreach(_src IN LISTS _sources)
        if(NOT _src MATCHES "\\.(cpp|cc|c|cxx)$")
            continue()
        endif()

        if(IS_ABSOLUTE "${_src}")
            set(_src_abs "${_src}")
        else()
            set(_src_abs "${CMAKE_SOURCE_DIR}/${_src}")
        endif()

        _ampforge_escape_json_path(_src_json "${_src_abs}")
        get_filename_component(_src_name "${_src}" NAME_WE)
        set(_obj_json "${_binary_dir_json}/clangd/${_src_name}.o")

        if(NOT _first)
            string(APPEND _json ",\n")
        endif()
        set(_first FALSE)

        string(APPEND _json "  {\n")
        string(APPEND _json "    \"directory\": \"${_source_dir_json}\",\n")
        string(APPEND _json "    \"file\": \"${_src_json}\",\n")
        string(APPEND _json "    \"command\": \"clang++ ${_base_flags_str} -c ${_src_json}\",\n")
        string(APPEND _json "    \"output\": \"${_obj_json}\"\n")
        string(APPEND _json "  }")
    endforeach()

    string(APPEND _json "\n]\n")
    file(WRITE "${CMAKE_BINARY_DIR}/compile_commands.json" "${_json}")

    # Root copy for clangd tools that do not honour .clangd CompilationDatabase (gitignored).
    configure_file(
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COPYONLY)
endfunction()
