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

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "VK_Mark-3 requires a 64-bit build. Configure with -A x64."
    )
endif()

# Debug = /MDd, Release = /MD.
set(
    CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

# ============================================================
# Vulkan SDK -> local vendor/Vulkan/lib
# All setup is contained in this CMakeLists.txt.
# SDK binaries remain local and must stay ignored by Git.
# ============================================================

if(NOT DEFINED ENV{VULKAN_SDK} OR "$ENV{VULKAN_SDK}" STREQUAL "")
    message(FATAL_ERROR "Install the Vulkan SDK and reopen the terminal so VULKAN_SDK is set.")
endif()
file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" VULKAN_SDK_ROOT)
set(VULKAN_LOCAL_LIB_DIR "${PROJECT_SOURCE_DIR}/vendor/Vulkan/lib")

# Both configurations must have their actual matching shaderc library.
foreach(_vk_required vulkan-1.lib shaderc_combined.lib shaderc_combinedd.lib)
    if(NOT EXISTS "${VULKAN_SDK_ROOT}/Lib/${_vk_required}")
        message(FATAL_ERROR
            "Missing SDK file: ${VULKAN_SDK_ROOT}/Lib/${_vk_required}\n"
            "Install/repair the Vulkan SDK shader compiler libraries, including Debug libraries."
        )
    endif()
endforeach()

# Copy only the three SDK libraries referenced by the supplied .vcxproj.
set(VULKAN_SDK_LIB_FILES
    "${VULKAN_SDK_ROOT}/Lib/vulkan-1.lib"
    "${VULKAN_SDK_ROOT}/Lib/shaderc_combined.lib"
    "${VULKAN_SDK_ROOT}/Lib/shaderc_combinedd.lib"
)
file(MAKE_DIRECTORY "${VULKAN_LOCAL_LIB_DIR}")
set(_vk_copy_commands)
foreach(_vk_source IN LISTS VULKAN_SDK_LIB_FILES)
    get_filename_component(_vk_name "${_vk_source}" NAME)
    file(COPY_FILE "${_vk_source}" "${VULKAN_LOCAL_LIB_DIR}/${_vk_name}"
        ONLY_IF_DIFFERENT
    )
    list(APPEND _vk_copy_commands
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_vk_source}" "${VULKAN_LOCAL_LIB_DIR}/${_vk_name}"
    )
endforeach()
list(LENGTH VULKAN_SDK_LIB_FILES _vk_lib_count)
message(STATUS "Vulkan: ${_vk_lib_count} SDK libraries copied to ${VULKAN_LOCAL_LIB_DIR}")

# Restore missing copies on builds too. No external .cmake script.
add_custom_target(StageVulkanLibraries
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VULKAN_LOCAL_LIB_DIR}"
    ${_vk_copy_commands}
    COMMENT "Updating vendor/Vulkan/lib from the installed Vulkan SDK"
    VERBATIM
)

# Use headers from the same SDK, and link the LOCAL loader copy.
set(Vulkan_INCLUDE_DIR "${VULKAN_SDK_ROOT}/Include" CACHE PATH "Vulkan SDK headers" FORCE)
set(Vulkan_LIBRARY "${VULKAN_LOCAL_LIB_DIR}/vulkan-1.lib" CACHE FILEPATH "Local Vulkan loader library" FORCE)
find_package(Vulkan 1.4 REQUIRED)

# Explicit Debug/Release paths match the existing .vcxproj exactly.
set(SHADERC_RELEASE_LIBRARY "${VULKAN_LOCAL_LIB_DIR}/shaderc_combined.lib")
set(SHADERC_DEBUG_LIBRARY "${VULKAN_LOCAL_LIB_DIR}/shaderc_combinedd.lib")
add_library(RendererShaderc STATIC IMPORTED)
set_target_properties(RendererShaderc PROPERTIES
    IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
    IMPORTED_LOCATION "${SHADERC_RELEASE_LIBRARY}"
    IMPORTED_LOCATION_DEBUG "${SHADERC_DEBUG_LIBRARY}"
    IMPORTED_LOCATION_RELEASE "${SHADERC_RELEASE_LIBRARY}"
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    MAP_IMPORTED_CONFIG_MINSIZEREL Release
    INTERFACE_INCLUDE_DIRECTORIES "${VULKAN_SDK_ROOT}/Include"
)

message(STATUS "Vulkan SDK: ${VULKAN_SDK_ROOT}")
message(STATUS "Vulkan loader: ${Vulkan_LIBRARY}")
message(STATUS "shaderc Debug: ${SHADERC_DEBUG_LIBRARY}")
message(STATUS "shaderc Release: ${SHADERC_RELEASE_LIBRARY}")

# ============================================================
# GLFW
# ============================================================

find_library(
    GLFW_LIBRARY
    NAMES glfw3
    PATHS "${PROJECT_SOURCE_DIR}/vendor/GLFW/lib"
    NO_DEFAULT_PATH
    REQUIRED
)

message(STATUS "GLFW: ${GLFW_LIBRARY}")

# ============================================================
# NVIDIA NRD
# ============================================================

include(FetchContent)

# Static NRD library with native Vulkan integration.
set(NRD_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
set(NRD_NRI OFF CACHE BOOL "" FORCE)

# Embed Vulkan shaders only.
set(NRD_EMBEDS_DXBC_SHADERS OFF CACHE BOOL "" FORCE)
set(NRD_EMBEDS_DXIL_SHADERS OFF CACHE BOOL "" FORCE)
set(NRD_EMBEDS_SPIRV_SHADERS ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    NRD
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NRD.git

    # Preserve the pinned NRD revision from the existing project.
    GIT_TAG 9a3fe938a7558fd16b6c91a1c0456305cdcd9f16
)

FetchContent_MakeAvailable(NRD)

# ============================================================
# Renderer source
# ============================================================

file(
    GLOB_RECURSE RENDERER_SOURCES
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
)

file(
    GLOB_RECURSE RENDERER_HEADERS
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.h"
    "${PROJECT_SOURCE_DIR}/src/*.hpp"
)

# CMake generates and manages the PCH.
list(
    REMOVE_ITEM RENDERER_SOURCES
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

file(
    GLOB MESHOPTIMIZER_SOURCES
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/vendor/meshoptimizer/*.cpp"
)

# ============================================================
# Shader files
#
# Visible in Visual Studio; compiled by the renderer at runtime.
# ============================================================

file(
    GLOB_RECURSE SHADER_FILES
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/res/shaders/*.glsl"
    "${PROJECT_SOURCE_DIR}/res/shaders/*.comp"
    "${PROJECT_SOURCE_DIR}/res/shaders/*.vert"
    "${PROJECT_SOURCE_DIR}/res/shaders/*.frag"
    "${PROJECT_SOURCE_DIR}/res/shaders/*.task"
    "${PROJECT_SOURCE_DIR}/res/shaders/*.mesh"
    "${PROJECT_SOURCE_DIR}/res/shaders/*.bat"
)

if(SHADER_FILES)
    set_source_files_properties(
        ${SHADER_FILES}
        PROPERTIES HEADER_FILE_ONLY ON
    )
endif()

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

add_dependencies(VulkanRenderer StageVulkanLibraries)

# ============================================================
# Include directories
# ============================================================

target_include_directories(
    VulkanRenderer
    PRIVATE

    # Prefer headers from the SDK supplying the linked libraries.
    ${Vulkan_INCLUDE_DIRS}

    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src/common"
    "${PROJECT_SOURCE_DIR}/res"

    "${PROJECT_SOURCE_DIR}/vendor"

    # Retained for bundled dependencies such as VMA.
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

# NRD supplies the headers matching its fetched revision.
# Do not add vendor/nrd/Include to this build.

# ============================================================
# Precompiled header
# ============================================================

target_precompile_headers(
    VulkanRenderer
    PRIVATE
    "${PROJECT_SOURCE_DIR}/src/common/pch.h"
)

set_source_files_properties(
    ${THIRD_PARTY_SOURCES}
    ${MESHOPTIMIZER_SOURCES}
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/src/Main.cpp"
    "${PROJECT_SOURCE_DIR}/src/renderer/backend/VulkanExtensionFunctions.cpp"
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

# ============================================================
# Compiler configuration
# ============================================================

target_compile_options(
    VulkanRenderer
    PRIVATE
    /utf-8
    /W4
    $<$<CONFIG:Release>:/GS->
)

set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/vendor/bc7enc/bc7enc.cpp"
    "${PROJECT_SOURCE_DIR}/vendor/bc7enc/rgbcx.cpp"
    PROPERTIES COMPILE_OPTIONS "/w"
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

    # Link local SDK copies; shaderc configuration matches /MDd or /MD.
    Vulkan::Vulkan
    RendererShaderc

    # Bundled GLFW library.
    "${GLFW_LIBRARY}"

    # Built through FetchContent.
    NRD

    # Windows libraries.
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
    FILES ${SHADER_FILES}
)

set_property(
    DIRECTORY "${PROJECT_SOURCE_DIR}"
    PROPERTY VS_STARTUP_PROJECT VulkanRenderer
)

# Runtime resource paths are relative to the repository root.
set_target_properties(
    VulkanRenderer
    PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
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
message(STATUS "Vulkan version: ${Vulkan_VERSION}")
message(STATUS "Vulkan loader:  ${Vulkan_LIBRARY}")
message(STATUS "shaderc Debug:  ${SHADERC_DEBUG_LIBRARY}")
message(STATUS "shaderc Release: ${SHADERC_RELEASE_LIBRARY}")
message(STATUS "Configurations: ${CMAKE_CONFIGURATION_TYPES}")
message(STATUS "========================================")
message(STATUS "")