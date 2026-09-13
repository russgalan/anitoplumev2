cmake_minimum_required(VERSION 3.20)

project(AnitoPlumeV2
    VERSION 0.1.0
    DESCRIPTION "Vulkan foundation for AnitoPlumeV2"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ANITOPLUME_V2_WARNINGS "Enable compiler warnings" ON)
option(ANITOPLUME_V2_REQUIRE_GRAPHICS_DEPS "Fail configuration when graphics dependencies are unavailable" OFF)

# Phase 1 dependency discovery. The project remains buildable as a headless
# foundation until the rendering phases consume these packages.
find_package(Vulkan QUIET)
find_package(glfw3 QUIET CONFIG)
find_package(glm QUIET CONFIG)
find_path(VMA_INCLUDE_DIR vk_mem_alloc.h PATH_SUFFIXES vma)
find_path(IMGUI_INCLUDE_DIR imgui.h)
find_program(GLSLC_EXECUTABLE glslc)

if (ANITOPLUME_V2_REQUIRE_GRAPHICS_DEPS)
    if (NOT Vulkan_FOUND)
        message(FATAL_ERROR "Vulkan development files were not found")
    endif()
    if (NOT glfw3_FOUND)
        message(FATAL_ERROR "GLFW development files were not found")
    endif()
    if (NOT glm_FOUND)
        message(FATAL_ERROR "GLM development files were not found")
    endif()
    if (NOT VMA_INCLUDE_DIR)
        message(FATAL_ERROR "Vulkan Memory Allocator header was not found")
    endif()
    if (NOT IMGUI_INCLUDE_DIR)
        message(FATAL_ERROR "Dear ImGui headers were not found")
    endif()
endif()

set(ANITOPLUME_V2_HAS_VULKAN 0)
set(ANITOPLUME_V2_HAS_GLFW 0)
set(ANITOPLUME_V2_HAS_GLM 0)
set(ANITOPLUME_V2_HAS_VMA 0)
set(ANITOPLUME_V2_HAS_IMGUI 0)
set(ANITOPLUME_V2_HAS_RENDERER 0)

if (Vulkan_FOUND)
    set(ANITOPLUME_V2_HAS_VULKAN 1)
endif()
if (glfw3_FOUND)
    set(ANITOPLUME_V2_HAS_GLFW 1)
endif()
if (glm_FOUND)
    set(ANITOPLUME_V2_HAS_GLM 1)
endif()
if (VMA_INCLUDE_DIR)
    set(ANITOPLUME_V2_HAS_VMA 1)
endif()
if (IMGUI_INCLUDE_DIR)
    set(ANITOPLUME_V2_HAS_IMGUI 1)
endif()
if (Vulkan_FOUND AND glfw3_FOUND AND (TARGET glfw OR TARGET glfw3::glfw))
    set(ANITOPLUME_V2_HAS_RENDERER 1)
endif()

if (GLSLC_EXECUTABLE)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/terrain.vert.spv
               ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/terrain.frag.spv
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders
        COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=vert ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/terrain.vert.glsl
                -o ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/terrain.vert.spv
        COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=frag ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/terrain.frag.glsl
                -o ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/terrain.frag.spv
        DEPENDS src/shaders/terrain.vert.glsl src/shaders/terrain.frag.glsl
        VERBATIM
    )
    add_custom_target(terrain_shaders DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/terrain.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/terrain.frag.spv
    )
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/foundation/build_config.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/anitoplumev2/build_config.hpp
    @ONLY
)

add_executable(anitoplumev2
    src/main.cpp
    src/rendering/camera.cpp
    src/rendering/vulkan_context.cpp
    src/rendering/image_loader.cpp
    src/terrain/terrain.cpp
    src/simulation/plume_simulation.cpp
)

if (GLSLC_EXECUTABLE)
    add_dependencies(anitoplumev2 terrain_shaders)
endif()

target_include_directories(anitoplumev2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

if (ANITOPLUME_V2_WARNINGS)
    if (MSVC)
        target_compile_options(anitoplumev2 PRIVATE /W4 /permissive-)
    else()
        target_compile_options(anitoplumev2 PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if (Vulkan_FOUND)
    target_link_libraries(anitoplumev2 PRIVATE Vulkan::Vulkan)
endif()

if (glfw3_FOUND)
    if (TARGET glfw)
        target_link_libraries(anitoplumev2 PRIVATE glfw)
    elseif (TARGET glfw3::glfw)
        target_link_libraries(anitoplumev2 PRIVATE glfw3::glfw)
    endif()
endif()

if (WIN32)
    target_link_libraries(anitoplumev2 PRIVATE windowscodecs ole32)
endif()

if (glm_FOUND AND TARGET glm::glm)
    target_link_libraries(anitoplumev2 PRIVATE glm::glm)
endif()

if (VMA_INCLUDE_DIR)
    target_include_directories(anitoplumev2 PRIVATE ${VMA_INCLUDE_DIR})
endif()

if (IMGUI_INCLUDE_DIR)
    target_include_directories(anitoplumev2 PRIVATE ${IMGUI_INCLUDE_DIR})
endif()

if (ANITOPLUME_V2_HAS_RENDERER)
    target_compile_definitions(anitoplumev2 PRIVATE ANITOPLUME_V2_RENDERER_ENABLED=1)
else()
    target_compile_definitions(anitoplumev2 PRIVATE ANITOPLUME_V2_RENDERER_ENABLED=0)
endif()

include(CTest)
if (BUILD_TESTING)
    add_test(NAME foundation_startup COMMAND anitoplumev2 --single-frame)
    set_tests_properties(foundation_startup PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(terrain_loader_test
        src/tests/terrain_loader_test.cpp
        src/terrain/terrain.cpp
    )
    target_include_directories(terrain_loader_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    if (ANITOPLUME_V2_WARNINGS)
        if (MSVC)
            target_compile_options(terrain_loader_test PRIVATE /W4 /permissive-)
        else()
            target_compile_options(terrain_loader_test PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
    add_test(NAME terrain_loader COMMAND terrain_loader_test
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tests/fixtures/triangle.obj)
    set_tests_properties(terrain_loader PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(plume_simulation_test
        src/tests/plume_simulation_test.cpp
        src/simulation/plume_simulation.cpp
    )
    target_include_directories(plume_simulation_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    if (ANITOPLUME_V2_WARNINGS)
        if (MSVC)
            target_compile_options(plume_simulation_test PRIVATE /W4 /permissive-)
        else()
            target_compile_options(plume_simulation_test PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
    add_test(NAME plume_simulation COMMAND plume_simulation_test)
endif()

message(STATUS "AnitoPlumeV2 Phase 1 dependency summary:")
message(STATUS "  Vulkan: ${ANITOPLUME_V2_HAS_VULKAN}")
message(STATUS "  GLFW:   ${ANITOPLUME_V2_HAS_GLFW}")
message(STATUS "  GLM:    ${ANITOPLUME_V2_HAS_GLM}")
message(STATUS "  VMA:    ${ANITOPLUME_V2_HAS_VMA}")
message(STATUS "  ImGui:  ${ANITOPLUME_V2_HAS_IMGUI}")
message(STATUS "  Renderer dependencies: ${ANITOPLUME_V2_HAS_RENDERER}")
message(STATUS "  glslc:  ${GLSLC_EXECUTABLE}")
