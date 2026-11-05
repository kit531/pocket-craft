# Compiles engine/render/shaders/*.{vert,frag} (GLSL 450) to SPIR-V at build time with glslc from the NDK and embeds the
# result as `<name>.<stage>.spv.inc` (comma separated uint32 list, glslc -mfmt=num) in ${CMAKE_CURRENT_BINARY_DIR}/shaders.
# vulkan_renderer.cpp #includes them. Included by engine/CMakeLists.txt before targets are created.
#
# Options: -DPC_GLSLC=/path/to/glslc overrides the search. The Android build (ANDROID) requires glslc (hard error if missing).
# The host build (pocketcraft_core, no renderer) skips shaders unless -DPC_BUILD_SHADERS=ON.
set(PC_SHADER_DIR ${CMAKE_CURRENT_LIST_DIR}/shaders)

if(ANDROID OR PC_BUILD_SHADERS)
    set(_pc_ndk_hints "")
    foreach(_v IN ITEMS "${ANDROID_NDK}" "${CMAKE_ANDROID_NDK}" "$ENV{ANDROID_NDK}" "$ENV{ANDROID_NDK_HOME}"
                        "$ENV{ANDROID_NDK_ROOT}" "$ENV{ANDROID_HOME}/ndk-bundle")
        if(_v)
            foreach(_tag IN ITEMS linux-x86_64 darwin-x86_64 windows-x86_64)
                list(APPEND _pc_ndk_hints "${_v}/shader-tools/${_tag}")
            endforeach()
        endif()
    endforeach()
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(_pc_host_tag linux-x86_64)
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(_pc_host_tag darwin-x86_64)
    else()
        set(_pc_host_tag windows-x86_64)
    endif()
    # Host tag first so a multi-host NDK picks the right binary.
    list(SORT _pc_ndk_hints)
    set(_pc_hints_sorted "")
    foreach(_h IN LISTS _pc_ndk_hints)
        if(_h MATCHES "${_pc_host_tag}$")
            list(PREPEND _pc_hints_sorted "${_h}")
        else()
            list(APPEND _pc_hints_sorted "${_h}")
        endif()
    endforeach()

    if(PC_GLSLC)
        set(_pc_glslc "${PC_GLSLC}")
    else()
        find_program(_pc_glslc glslc HINTS ${_pc_hints_sorted})
    endif()
    if(NOT _pc_glslc)
        message(FATAL_ERROR
            "glslc (shader compiler) not found. It ships with the Android NDK in <ndk>/shader-tools/<host-tag>/glslc. "
            "Set ANDROID_NDK / ANDROID_NDK_HOME, put glslc on PATH, or pass -DPC_GLSLC=/path/to/glslc.")
    endif()
    message(STATUS "pocketcraft: glslc = ${_pc_glslc}")

    set(PC_SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${PC_SHADER_OUT_DIR})
    file(GLOB PC_SHADER_SRC CONFIGURE_DEPENDS ${PC_SHADER_DIR}/*.vert ${PC_SHADER_DIR}/*.frag)
    set(PC_SHADER_OUTPUTS "")
    foreach(_src IN LISTS PC_SHADER_SRC)
        get_filename_component(_name ${_src} NAME)
        set(_out ${PC_SHADER_OUT_DIR}/${_name}.spv.inc)
        add_custom_command(
            OUTPUT ${_out}
            COMMAND ${_pc_glslc} --target-env=vulkan1.0 -O -mfmt=num -o ${_out} ${_src}
            DEPENDS ${_src}
            COMMENT "glslc ${_name}"
            VERBATIM)
        list(APPEND PC_SHADER_OUTPUTS ${_out})
    endforeach()
    add_custom_target(pc_shaders DEPENDS ${PC_SHADER_OUTPUTS})
    list(APPEND PC_RENDER_EXTRA_DEPS pc_shaders)
    include_directories(${PC_SHADER_OUT_DIR})
endif()
