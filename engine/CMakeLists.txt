cmake_minimum_required(VERSION 3.22)
project(pocketcraft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Sources are globbed per folder so bots add files without editing this file.
file(GLOB_RECURSE PC_CORE_SRC     CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp)
file(GLOB_RECURSE PC_GAME_SRC     CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/game/*.cpp)
file(GLOB_RECURSE PC_WORLD_SRC    CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/world/*.cpp)
file(GLOB_RECURSE PC_RENDER_SRC   CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/render/*.cpp)
file(GLOB_RECURSE PC_PLATFORM_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/platform/*.cpp)

# Hook for the renderer bot: engine/render/shaders.cmake compiles GLSL -> SPIR-V headers
# (e.g. add_custom_command + add_custom_target(pc_shaders)) and may append to PC_RENDER_EXTRA_DEPS.
include(${CMAKE_CURRENT_SOURCE_DIR}/render/shaders.cmake OPTIONAL)

add_compile_options(-Wall -Wextra)

function(pc_configure target)
    target_include_directories(${target} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/game
        ${CMAKE_CURRENT_SOURCE_DIR}/world
        ${CMAKE_CURRENT_SOURCE_DIR}/render
        ${CMAKE_CURRENT_SOURCE_DIR}/platform)
endfunction()

if(ANDROID)
    add_library(pocketcraft SHARED
        ${PC_CORE_SRC} ${PC_GAME_SRC} ${PC_WORLD_SRC} ${PC_RENDER_SRC} ${PC_PLATFORM_SRC})
    pc_configure(pocketcraft)
    if(DEFINED PC_RENDER_EXTRA_DEPS)
        add_dependencies(pocketcraft ${PC_RENDER_EXTRA_DEPS})
    endif()
    # 16 KB page alignment (required for Android 15+ devices); NDK r28+ does this by default.
    target_link_options(pocketcraft PRIVATE -Wl,-z,max-page-size=16384)
    target_link_libraries(pocketcraft PRIVATE android log vulkan)
else()
    find_package(Threads REQUIRED)
    add_library(pocketcraft_core STATIC ${PC_CORE_SRC} ${PC_GAME_SRC} ${PC_WORLD_SRC})
    pc_configure(pocketcraft_core)
    target_link_libraries(pocketcraft_core PUBLIC Threads::Threads)
    # Host-only: game.cpp needs createVulkanRenderer(); tests that use Game provide a fake via tests/.
    option(POCKETCRAFT_BUILD_TESTS "Build host tests from ../tests" ON)
    if(POCKETCRAFT_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../tests/CMakeLists.txt)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tests ${CMAKE_CURRENT_BINARY_DIR}/tests)
    endif()
endif()
