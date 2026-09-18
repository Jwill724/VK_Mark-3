cmake_minimum_required(VERSION 4.2)

project(
    VulkanRenderer
    VERSION 1.0
    LANGUAGES CXX
)

# ============================================================
# Project configuration
# ============================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Visual Studio project only really uses Debug / Release.
if(CMAKE_CONFIGURATION_TYPES)
    set(
        CMAKE_CONFIGURATION_TYPES
        "Debug;Release"
        CACHE STRING
        "Available build configurations"
        FORCE
    )
endif()

# ============================================================
# Output directories
# ============================================================

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

foreach(CONFIG Debug Release)
    string(TOUPPER "${CONFIG}" CONFIG_UPPER)

    set(
        CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER}
        "${CMAKE_BINARY_DIR}/bin/${CONFIG}"
    )

    set(
        CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER}
        "${CMAKE_BINARY_DIR}/lib/${CONFIG}"
    )

    set(
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER}
        "${CMAKE_BINARY_DIR}/lib/${CONFIG}"
    )
endforeach()

# ============================================================
# Windows / MSVC
# ============================================================

if(NOT WIN32)
    message(FATAL_ERROR
        "VK_Mark-3 currently expects a Windows build environment."
    )
endif()

if(NOT MSVC)
    message(FATAL_ERROR
        "VK_Mark-3 currently expects the MSVC toolchain."
    )
endif()

# Match existing project:
#
# Debug   = /MDd
# Release = /MD
#
set(
    CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

# ============================================================
# Vulkan SDK
# ============================================================

if(NOT DEFINED ENV{VULKAN_SDK})
    message(FATAL_ERROR
        "\n"
        "VULKAN_SDK environment variable is not set.\n"
        "\n"
        "Install a Vulkan 1.4 SDK from LunarG, then open a new terminal.\n"
        "VK-Mark-3 is known to work with Vulkan SDK 1.4.321.1.\n"
        "Newer Vulkan 1.4 SDKs are allowed.\n"
    )
endif()

file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" VULKAN_SDK_ROOT)

message(STATUS "VULKAN_SDK = ${VULKAN_SDK_ROOT}")

find_package(Vulkan REQUIRED)

if(DEFINED Vulkan_VERSION)
    message(STATUS "Detected Vulkan version: ${Vulkan_VERSION}")

    if(Vulkan_VERSION VERSION_LESS "1.4.0")
        message(FATAL_ERROR
            "VK-Mark-3 requires Vulkan SDK 1.4 or newer. "
            "Detected: ${Vulkan_VERSION}"
        )
    endif()
endif()

# shaderc_combined comes from the Vulkan SDK.
find_library(
    SHADERC_COMBINED_LIBRARY
    NAMES shaderc_combined
    PATHS
        "${VULKAN_SDK_ROOT}/Lib"
    NO_DEFAULT_PATH
    REQUIRED
)

message(STATUS
    "shaderc_combined = ${SHADERC_COMBINED_LIBRARY}"
)

# ============================================================
# GLFW
# ============================================================

find_library(
    GLFW_LIBRARY
    NAMES glfw3
    PATHS
        "${PROJECT_SOURCE_DIR}/vendor/GLFW/lib"
    NO_DEFAULT_PATH
    REQUIRED
)

message(STATUS "GLFW = ${GLFW_LIBRARY}")

# ============================================================
# NVIDIA NRD
# ============================================================

include(FetchContent)

# Static NRD library.
set(
    NRD_STATIC_LIBRARY
    ON
    CACHE BOOL ""
    FORCE
)

# Renderer has its own native Vulkan integration.
set(
    NRD_NRI
    OFF
    CACHE BOOL ""
    FORCE
)

# Vulkan only.
set(
    NRD_EMBEDS_DXBC_SHADERS
    OFF
    CACHE BOOL ""
    FORCE
)

set(
    NRD_EMBEDS_DXIL_SHADERS
    OFF
    CACHE BOOL ""
    FORCE
)

set(
    NRD_EMBEDS_SPIRV_SHADERS
    ON
    CACHE BOOL ""
    FORCE
)

FetchContent_Declare(
    NRD

    GIT_REPOSITORY
        https://github.com/NVIDIA-RTX/NRD.git

    # NRD 4.17.4
    #
    # Pin the exact revision rather than master.
    GIT_TAG
        9a3fe938a7558fd16b6c91a1c0456305cdcd9f16
)

FetchContent_MakeAvailable(NRD)

# ============================================================
# Renderer source
# ============================================================

file(
    GLOB_RECURSE
    RENDERER_SOURCES
    CONFIGURE_DEPENDS

    "${PROJECT_SOURCE_DIR}/src/*.cpp"
)

file(
    GLOB_RECURSE
    RENDERER_HEADERS
    CONFIGURE_DEPENDS

    "${PROJECT_SOURCE_DIR}/src/*.h"
    "${PROJECT_SOURCE_DIR}/src/*.hpp"
)

# CMake generates/manages the actual PCH itself.
# pch.cpp is only needed by the hand-written Visual Studio project.
list(
    REMOVE_ITEM
    RENDERER_SOURCES
    "${PROJECT_SOURCE_DIR}/src/common/pch.cpp"
)

# ============================================================
# Third-party source
# ============================================================

set(
    THIRD_PARTY_SOURCES

    # VMA
    "${PROJECT_SOURCE_DIR}/vendor/Vulkan/include/vma/vma.cpp"

    # enkiTS
    "${PROJECT_SOURCE_DIR}/vendor/enkiTS/TaskScheduler.cpp"

    # ImGui
    "${PROJECT_SOURCE_DIR}/vendor/im_gui/imgui.cpp"
    "${PROJECT_SOURCE_DIR}/vendor/im_gui/imgui_draw.cpp"
    "${PROJECT_SOURCE_DIR}/vendor/im_gui/imgui_widgets.cpp"
    "${PROJECT_SOURCE_DIR}/vendor/im_gui/imgui_tables.cpp"
    "${PROJECT_SOURCE_DIR}/vendor/im_gui/imgui_impl_glfw.cpp"
    "${PROJECT_SOURCE_DIR}/vendor/im_gui/imgui_impl_vulkan.cpp"

    # stb
    "${PROJECT_SOURCE_DIR}/vendor/stb_image/stb_image.cpp"

    # simdjson
    "${PROJECT_SOURCE_DIR}/vendor/simdjson/simdjson.cpp"

    # fastgltf
    "${PROJECT_SOURCE_DIR}/vendor/fastgltf/fastgltf.cpp"
    "${PROJECT_SOURCE_DIR}/vendor/fastgltf/base64.cpp"
    "${PROJECT_SOURCE_DIR}/vendor/fastgltf/io.cpp"

    # BC compression
    "${PROJECT_SOURCE_DIR}/vendor/bc7enc/bc7enc.cpp"
    "${PROJECT_SOURCE_DIR}/vendor/bc7enc/rgbcx.cpp"

    # Tracy
    "${PROJECT_SOURCE_DIR}/vendor/tracy/TracyClient.cpp"

    # fmt
    "${PROJECT_SOURCE_DIR}/vendor/fmt/format.cc"
)

# meshoptimizer is composed of a collection of individual .cpp files.
file(
    GLOB
    MESHOPTIMIZER_SOURCES
    CONFIGURE_DEPENDS

    "${PROJECT_SOURCE_DIR}/vendor/meshoptimizer/*.cpp"
)

# ============================================================
# Shader files
#
# These are added to the VS project so they remain visible/editable.
# They are NOT compiled as C++ sources.
# ============================================================

file(
    GLOB_RECURSE
    SHADER_FILES
    CONFIGURE_DEPENDS

    "${PROJECT_SOURCE_DIR}/res/shaders/*.glsl"
    "${PROJECT_SOURCE_DIR}/res/shaders/*.comp"
    "${PROJECT_SOURCE_DIR}/res/shaders/*.vert"
    "${PROJECT_SOURCE_DIR}/res/shaders/*.frag"
    "${PROJECT_SOURCE_DIR}/res/shaders/*.task"
    "${PROJECT_SOURCE_DIR}/res/shaders/*.mesh"
    "${PROJECT_SOURCE_DIR}/res/shaders/*.bat"
)

# ============================================================
# Executable
# ============================================================

add_executable(
    VulkanRenderer

    ${RENDERER_SOURCES}
    ${RENDERER_HEADERS}
    ${THIRD_PARTY_SOURCES}
    ${MESHOPTIMIZER_SOURCES}
    ${SHADER_FILES}
)

# ============================================================
# Include directories
# ============================================================

target_include_directories(
    VulkanRenderer
    PRIVATE

    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src/common"
    "${PROJECT_SOURCE_DIR}/res"

    "${PROJECT_SOURCE_DIR}/vendor"

    "${PROJECT_SOURCE_DIR}/vendor/Vulkan/include"
    "${PROJECT_SOURCE_DIR}/vendor/GLFW/include"

    "${PROJECT_SOURCE_DIR}/vendor/simdjson"
    "${PROJECT_SOURCE_DIR}/vendor/im_gui"
    "${PROJECT_SOURCE_DIR}/vendor/enkiTS"
    "${PROJECT_SOURCE_DIR}/vendor/meshoptimizer"
    "${PROJECT_SOURCE_DIR}/vendor/tracy"
    "${PROJECT_SOURCE_DIR}/vendor/fastgltf"
    "${PROJECT_SOURCE_DIR}/vendor/fmt"
    "${PROJECT_SOURCE_DIR}/vendor/bc7enc"
    "${PROJECT_SOURCE_DIR}/vendor/stb_image"
)

# NRD's CMake target supplies its own public include directories.
# Do NOT use vendor/nrd/Include in the CMake build. This guarantees
# the headers correspond to the NRD revision fetched above.

# ============================================================
# Precompiled header
# ============================================================

target_precompile_headers(
    VulkanRenderer
    PRIVATE

    "${PROJECT_SOURCE_DIR}/src/common/pch.h"
)

# Third-party source should not inherit the renderer PCH.
set_source_files_properties(
    ${THIRD_PARTY_SOURCES}
    ${MESHOPTIMIZER_SOURCES}

    PROPERTIES

    SKIP_PRECOMPILE_HEADERS ON
)

# These two files also explicitly bypass the PCH in the existing
# Visual Studio project.
set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/src/Main.cpp"
    "${PROJECT_SOURCE_DIR}/src/renderer/backend/VulkanExtensionFunctions.cpp"

    PROPERTIES

    SKIP_PRECOMPILE_HEADERS ON
)

# ============================================================
# Compiler configuration
# ============================================================

target_compile_options(
    VulkanRenderer
    PRIVATE

    /utf-8
    /W4

    # Existing Release project disables /GS.
    $<$<CONFIG:Release>:/GS->
)

# bc7enc currently builds with warnings disabled in the VS project.
set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/vendor/bc7enc/bc7enc.cpp"
    "${PROJECT_SOURCE_DIR}/vendor/bc7enc/rgbcx.cpp"

    PROPERTIES

    COMPILE_OPTIONS "/w"
)

# ============================================================
# Definitions
# ============================================================

target_compile_definitions(
    VulkanRenderer
    PRIVATE

    UNICODE
    _UNICODE

    PLATFORM_WINDOWS

    TRACY_ENABLE

    VK_API_VERSION=VK_API_VERSION_1_4

    $<$<CONFIG:Release>:NDEBUG>
)

# ============================================================
# Link libraries
# ============================================================

target_link_libraries(
    VulkanRenderer
    PRIVATE

    # Vulkan
    Vulkan::Vulkan

    # Vulkan SDK shader compiler
    "${SHADERC_COMBINED_LIBRARY}"

    # GLFW
    "${GLFW_LIBRARY}"

    # NVIDIA NRD
    NRD

    # Windows
    kernel32
    user32
    gdi32
    winmm
)

# ============================================================
# Visual Studio organization
# ============================================================

source_group(
    TREE "${PROJECT_SOURCE_DIR}/src"
    PREFIX "src"
    FILES
        ${RENDERER_SOURCES}
        ${RENDERER_HEADERS}
)

source_group(
    TREE "${PROJECT_SOURCE_DIR}/vendor"
    PREFIX "vendor"
    FILES
        ${THIRD_PARTY_SOURCES}
        ${MESHOPTIMIZER_SOURCES}
)

source_group(
    TREE "${PROJECT_SOURCE_DIR}/res/shaders"
    PREFIX "res\\shaders"
    FILES
        ${SHADER_FILES}
)

# VulkanRenderer becomes the startup project when opening the
# generated Visual Studio solution.
set_property(
    DIRECTORY "${PROJECT_SOURCE_DIR}"
    PROPERTY VS_STARTUP_PROJECT VulkanRenderer
)

# Renderer currently expects res/... etc. relative to repo root.
set_target_properties(
    VulkanRenderer
    PROPERTIES

    VS_DEBUGGER_WORKING_DIRECTORY
        "${PROJECT_SOURCE_DIR}"
)

# ============================================================
# Summary
# ============================================================

message(STATUS "")
message(STATUS "========================================")
message(STATUS "VK-Mark-3")
message(STATUS "========================================")
message(STATUS "Generator:      ${CMAKE_GENERATOR}")
message(STATUS "Compiler:       ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Vulkan SDK:     ${VULKAN_SDK_ROOT}")

if(DEFINED Vulkan_VERSION)
    message(STATUS "Vulkan version: ${Vulkan_VERSION}")
endif()

message(STATUS "NRD:            4.17.4")
message(STATUS "Configurations: ${CMAKE_CONFIGURATION_TYPES}")
message(STATUS "========================================")
message(STATUS "")