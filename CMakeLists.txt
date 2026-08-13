cmake_minimum_required(VERSION 3.19)
project(termin_monorepo VERSION 0.1.0 LANGUAGES C CXX)

include(CMakePackageConfigHelpers)

option(TERMIN_BUILD_MONOREPO "Build Termin as one CMake target graph" ON)
option(TERMIN_PLATFORM_ANDROID "Build Termin native runtime for Android" OFF)
option(TERMIN_PLATFORM_WEB "Build the portable Termin WebAssembly core" OFF)
option(TERMIN_ENABLE_OPENGL "Enable desktop OpenGL-dependent graphics/render targets" ON)
option(TERMIN_ENABLE_WEBGL2 "Enable the WebGL 2 constrained-GL backend" ${TERMIN_PLATFORM_WEB})
option(TERMIN_BUILD_PYTHON "Build Python/nanobind bindings" OFF)
option(TERMIN_BUILD_TESTS "Build C/C++ tests" OFF)
option(TERMIN_BUILD_TGFX2_TESTS "Build tgfx2 C/C++ tests" ON)
option(TERMIN_BUILD_WINDOW_TESTS "Build tests that require a windowing system" OFF)
option(TERMIN_ENABLE_VULKAN "Enable Vulkan support" ON)
option(TERMIN_ENABLE_SDL "Enable SDL support" ON)
option(TERMIN_BUILD_EDITOR_MINIMAL "Build minimal editor executable" OFF)
option(TERMIN_BUILD_LAUNCHER "Build launcher executable" OFF)
option(TERMIN_USE_CCACHE "Use ccache as C/C++ compiler launcher when available" ON)
option(TERMIN_ENABLE_UNITY_BUILD "Enable CMake unity build for faster clean builds" OFF)
option(TERMIN_ENABLE_PCH "Enable precompiled headers for selected C++ targets" ON)
set(TERMIN_SDK_PROFILE "full" CACHE STRING
    "SDK target graph profile: full, graphics or core")
set(TERMIN_CORE_SDK "" CACHE PATH
    "Explicit installed Core SDK for the active target platform")
set(TERMIN_CORE_BUILD_ID "" CACHE STRING
    "Required native_build_id of TERMIN_CORE_SDK")
set(TERMIN_CORE_TOOLCHAIN_VERSION "" CACHE STRING
    "Required toolchain version for a cross-compiled Core SDK")
set_property(CACHE TERMIN_SDK_PROFILE PROPERTY STRINGS full graphics core)

if(NOT TERMIN_SDK_PROFILE STREQUAL "full"
   AND NOT TERMIN_SDK_PROFILE STREQUAL "graphics"
   AND NOT TERMIN_SDK_PROFILE STREQUAL "core")
    message(FATAL_ERROR
        "Unsupported TERMIN_SDK_PROFILE='${TERMIN_SDK_PROFILE}'. "
        "Expected one of: full, graphics, core")
endif()

if(TERMIN_SDK_PROFILE STREQUAL "graphics" OR TERMIN_SDK_PROFILE STREQUAL "core")
    if(TERMIN_PLATFORM_ANDROID OR TERMIN_PLATFORM_WEB)
        message(FATAL_ERROR
            "TERMIN_SDK_PROFILE=${TERMIN_SDK_PROFILE} is a desktop SDK profile")
    endif()
    if(TERMIN_SDK_PROFILE STREQUAL "graphics")
        set(TERMIN_VISUAL_SCENE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(TERMIN_GUI_NATIVE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(TERMIN_RENDER_CORE_ONLY ON CACHE BOOL "" FORCE)
        message(STATUS
            "Termin: graphics SDK profile enabled (widgets, visual scene, 2D/3D charts)")
    else()
        set(TERMIN_RENDER_CORE_ONLY OFF CACHE BOOL "" FORCE)
        set(TERMIN_ENABLE_OPENGL OFF CACHE BOOL
            "OpenGL is outside the Core SDK product graph" FORCE)
        set(TERMIN_ENABLE_VULKAN OFF CACHE BOOL
            "Vulkan is outside the Core SDK product graph" FORCE)
        set(TERMIN_ENABLE_SDL OFF CACHE BOOL
            "SDL is outside the Core SDK product graph" FORCE)
        message(STATUS "Termin: Core SDK profile enabled")
    endif()
else()
    set(TERMIN_RENDER_CORE_ONLY OFF CACHE BOOL "" FORCE)
endif()

if(TERMIN_PLATFORM_ANDROID AND TERMIN_PLATFORM_WEB)
    message(FATAL_ERROR "TERMIN_PLATFORM_ANDROID and TERMIN_PLATFORM_WEB are mutually exclusive")
endif()

if(TERMIN_PLATFORM_WEB)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR
            "TERMIN_PLATFORM_WEB requires the Emscripten toolchain. "
            "Configure it through ./build-web-core.sh.")
    endif()
    set(TERMIN_ENABLE_OPENGL OFF CACHE BOOL "Enable desktop OpenGL-dependent graphics/render targets" FORCE)
    set(TERMIN_BUILD_PYTHON OFF CACHE BOOL "Build Python/nanobind bindings" FORCE)
    set(TERMIN_BUILD_TESTS OFF CACHE BOOL "Build C/C++ tests" FORCE)
    set(TERMIN_BUILD_TGFX2_TESTS OFF CACHE BOOL "Build tgfx2 C/C++ tests" FORCE)
    set(TERMIN_BUILD_WINDOW_TESTS OFF CACHE BOOL "Build tests that require a windowing system" FORCE)
    set(TERMIN_ENABLE_VULKAN OFF CACHE BOOL "Enable Vulkan support" FORCE)
    set(TERMIN_ENABLE_SDL OFF CACHE BOOL "Enable SDL support" FORCE)
    set(TERMIN_BUILD_EDITOR_MINIMAL OFF CACHE BOOL "Build minimal editor executable" FORCE)
    set(TERMIN_BUILD_LAUNCHER OFF CACHE BOOL "Build launcher executable" FORCE)
    set(TERMIN_ENABLE_PCH OFF CACHE BOOL "Enable precompiled headers for selected targets" FORCE)
    set(TERMIN_BASE_BUILD_SHARED OFF CACHE BOOL "Build termin-base as a shared library" FORCE)
    set(TERMIN_MESH_BUILD_SHARED OFF CACHE BOOL "Build termin-mesh as a shared library" FORCE)
    set(TI_BUILD_SHARED OFF CACHE BOOL "Build termin-inspect as a shared library" FORCE)
    set(TERMIN_SCENE_BUILD_SHARED OFF CACHE BOOL "Build termin-scene as a shared library" FORCE)
    set(TERMIN_VISUAL_SCENE_BUILD_EXAMPLES OFF CACHE BOOL "Build termin-visual-scene examples" FORCE)
    message(STATUS "Termin: WebAssembly core profile enabled")
elseif(TERMIN_PLATFORM_ANDROID)
    set(TERMIN_ENABLE_OPENGL OFF CACHE BOOL "Enable desktop OpenGL-dependent graphics/render targets" FORCE)
    set(TERMIN_BUILD_PYTHON OFF CACHE BOOL "Build Python/nanobind bindings" FORCE)
    set(TERMIN_BUILD_TESTS OFF CACHE BOOL "Build C/C++ tests" FORCE)
    set(TERMIN_BUILD_TGFX2_TESTS OFF CACHE BOOL "Build tgfx2 C/C++ tests" FORCE)
    set(TERMIN_BUILD_WINDOW_TESTS OFF CACHE BOOL "Build tests that require a windowing system" FORCE)
    set(TERMIN_ENABLE_VULKAN ON CACHE BOOL "Enable Vulkan support" FORCE)
    set(TERMIN_ENABLE_SDL OFF CACHE BOOL "Enable SDL support" FORCE)
    set(TERMIN_BUILD_EDITOR_MINIMAL OFF CACHE BOOL "Build minimal editor executable" FORCE)
    set(TERMIN_BUILD_LAUNCHER OFF CACHE BOOL "Build launcher executable" FORCE)
    set(TERMIN_ENABLE_PCH OFF CACHE BOOL "Enable precompiled headers for selected C++ targets" FORCE)
    message(STATUS "Termin: Android native render profile enabled")
endif()

if(NOT TERMIN_ENABLE_OPENGL)
    message(STATUS "Termin: desktop OpenGL backend disabled")
endif()
if(TERMIN_ENABLE_WEBGL2)
    message(STATUS "Termin: WebGL 2 constrained-GL backend enabled")
endif()

if(TERMIN_USE_CCACHE AND NOT MSVC)
    find_program(TERMIN_CCACHE_PROGRAM ccache)
    if(TERMIN_CCACHE_PROGRAM)
        if(NOT CMAKE_C_COMPILER_LAUNCHER)
            set(CMAKE_C_COMPILER_LAUNCHER "${TERMIN_CCACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
        endif()
        if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
            set(CMAKE_CXX_COMPILER_LAUNCHER "${TERMIN_CCACHE_PROGRAM}" CACHE STRING "CXX compiler launcher" FORCE)
        endif()
        message(STATUS "Termin: using ccache compiler launcher: ${TERMIN_CCACHE_PROGRAM}")
    else()
        message(STATUS "Termin: ccache requested but not found")
    endif()
endif()

function(termin_enable_unity_build_for target)
    if(TERMIN_ENABLE_UNITY_BUILD AND TARGET "${target}")
        set_target_properties("${target}" PROPERTIES
            UNITY_BUILD ON
            UNITY_BUILD_BATCH_SIZE 8
        )
    endif()
endfunction()

function(termin_enable_pch_for target)
    if(TERMIN_ENABLE_PCH AND TARGET "${target}")
        get_target_property(_termin_target_type "${target}" TYPE)
        if(MSVC AND _termin_target_type STREQUAL "SHARED_LIBRARY")
            message(STATUS "Termin: PCH skipped for MSVC shared library target ${target}")
            return()
        endif()
        target_precompile_headers("${target}" PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cmake/termin_cpp_pch.hpp>"
        )
    endif()
endfunction()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/TerminTestMetadata.cmake)

if(TERMIN_ENABLE_UNITY_BUILD)
    message(STATUS "Termin: unity build enabled for selected targets")
endif()

if(TERMIN_ENABLE_PCH)
    message(STATUS "Termin: precompiled headers enabled for selected targets")
endif()

if(TERMIN_BUILD_TESTS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/termin-thirdparty/guard/guard_c.h"
       OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/termin-thirdparty/guard/guard_main.h")
        message(FATAL_ERROR
            "TERMIN_BUILD_TESTS=ON requires the guard test framework submodule. "
            "Run: git submodule update --init termin-thirdparty/guard")
    endif()
    enable_testing()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(TerminDependencies)

if(TERMIN_BUILD_PYTHON)
    include(TerminPython)
    termin_require_canonical_python(Interpreter Development)

    if(TERMIN_SDK_PROFILE STREQUAL "core" OR TERMIN_PLATFORM_ANDROID OR TERMIN_PLATFORM_WEB)
        execute_process(
            COMMAND "${Python_EXECUTABLE}" -c "import nanobind, pathlib; print(pathlib.Path(nanobind.__file__).resolve().parent / 'cmake')"
            OUTPUT_VARIABLE TERMIN_NANOBIND_CMAKE_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE TERMIN_NANOBIND_CMAKE_RESULT
        )
        if(NOT TERMIN_NANOBIND_CMAKE_RESULT EQUAL 0 OR NOT EXISTS "${TERMIN_NANOBIND_CMAKE_DIR}/nanobind-config.cmake")
            message(FATAL_ERROR "nanobind Python package was not found for ${Python_EXECUTABLE}. Install nanobind before configuring with TERMIN_BUILD_PYTHON=ON.")
        endif()
        file(TO_CMAKE_PATH "${TERMIN_NANOBIND_CMAKE_DIR}" TERMIN_NANOBIND_CMAKE_DIR)
        set(nanobind_DIR "${TERMIN_NANOBIND_CMAKE_DIR}" CACHE PATH "nanobind CMake package directory" FORCE)
    endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

set(TERMIN_EFFECTIVE_BUILD_TGFX2_TESTS OFF)
if(TERMIN_BUILD_TESTS AND TERMIN_BUILD_TGFX2_TESTS)
    set(TERMIN_EFFECTIVE_BUILD_TGFX2_TESTS ON)
endif()

set(TERMIN_EFFECTIVE_BUILD_DISPLAY_TESTS OFF)
set(TERMIN_EFFECTIVE_BUILD_DISPLAY_WINDOW_TESTS OFF)
if(TERMIN_BUILD_TESTS)
    set(TERMIN_EFFECTIVE_BUILD_DISPLAY_TESTS ON)
    if(TERMIN_BUILD_WINDOW_TESTS AND TERMIN_ENABLE_SDL)
        set(TERMIN_EFFECTIVE_BUILD_DISPLAY_WINDOW_TESTS ON)
    endif()
endif()

set(TERMIN_BASE_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_AUDIO_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_DISPATCH_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_MODULES_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_CSG_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_CSG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(BUILD_TGFX2_TESTS ${TERMIN_EFFECTIVE_BUILD_TGFX2_TESTS} CACHE BOOL "" FORCE)
set(BUILD_TGFX2_WINDOW_TESTS ${TERMIN_BUILD_WINDOW_TESTS} CACHE BOOL "" FORCE)
set(BUILD_TGFX2_D3D11_WINDOW_TEST ${TERMIN_BUILD_WINDOW_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_DISPLAY_BUILD_TESTS ${TERMIN_EFFECTIVE_BUILD_DISPLAY_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_WINDOW_BUILD_TESTS ${TERMIN_EFFECTIVE_BUILD_DISPLAY_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_WINDOW_BUILD_WINDOW_TESTS ${TERMIN_EFFECTIVE_BUILD_DISPLAY_WINDOW_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_INPUT_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_GUI_NATIVE_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_GUI_NATIVE_BUILD_WINDOW_INTEGRATION
    ${TERMIN_ENABLE_SDL} CACHE BOOL "" FORCE)
set(TERMIN_GUI_NATIVE_BUILD_EXAMPLES
    ${TERMIN_ENABLE_SDL} CACHE BOOL "" FORCE)
set(TERMIN_PROFILER_BUILD_GUI
    ${TERMIN_ENABLE_SDL} CACHE BOOL "" FORCE)
set(TI_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TI_BUILD_PYTHON_TESTS ${TERMIN_BUILD_PYTHON} CACHE BOOL "" FORCE)
set(TERMIN_SCENE_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_ENGINE_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_LIGHTING_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_PREFAB_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_RENDER_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_RENDER_PASSES_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_RUNTIME_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_SKELETON_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_COLLISION_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_PHYSICS_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_QOPT_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_ROBOTICS_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_ROBOTICS_BUILD_PYTHON ${TERMIN_BUILD_PYTHON} CACHE BOOL "" FORCE)
set(TERMIN_COMPONENTS_KINEMATIC_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TERMIN_PHYSICS_QOPT_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TC_BUILD_TESTS ${TERMIN_BUILD_TESTS} CACHE BOOL "" FORCE)

set(BUILD_EDITOR_MINIMAL ${TERMIN_BUILD_EDITOR_MINIMAL} CACHE BOOL "" FORCE)
set(BUILD_LAUNCHER ${TERMIN_BUILD_LAUNCHER} CACHE BOOL "" FORCE)
set(TGFX2_ENABLE_VULKAN ${TERMIN_ENABLE_VULKAN} CACHE BOOL "" FORCE)
set(TGFX2_ENABLE_OPENGL ${TERMIN_ENABLE_OPENGL} CACHE BOOL "" FORCE)
set(TGFX2_ENABLE_WEBGL2 ${TERMIN_ENABLE_WEBGL2} CACHE BOOL "" FORCE)
set(TGFX2_ENABLE_WEBGPU ${TERMIN_PLATFORM_WEB} CACHE BOOL "" FORCE)
set(USE_SYSTEM_SDL2 ${TERMIN_ENABLE_SDL} CACHE BOOL "" FORCE)
set(TERMIN_BOOTSTRAP_MINIMAL_ONLY OFF CACHE BOOL "" FORCE)
set(TERMIN_RUNTIME_MINIMAL_ONLY OFF CACHE BOOL "" FORCE)
set(TERMIN_BOOTSTRAP_RENDER_ONLY ${TERMIN_PLATFORM_WEB} CACHE BOOL "" FORCE)
set(TERMIN_RUNTIME_RENDER_ONLY ${TERMIN_PLATFORM_WEB} CACHE BOOL "" FORCE)
set(TERMIN_RENDER_PASSES_CORE_ONLY ${TERMIN_PLATFORM_WEB} CACHE BOOL "" FORCE)
set(TERMIN_ENGINE_CORE_RENDER_PIPELINE ${TERMIN_PLATFORM_WEB} CACHE BOOL "" FORCE)
set(TERMIN_COMPONENTS_RENDER_CORE_ONLY ${TERMIN_PLATFORM_WEB} CACHE BOOL "" FORCE)

set(TERMIN_USE_INSTALLED_CORE OFF)
if(NOT TERMIN_SDK_PROFILE STREQUAL "core")
    set(TERMIN_USE_INSTALLED_CORE ON)
    if(NOT IS_ABSOLUTE "${TERMIN_CORE_SDK}")
        message(FATAL_ERROR
            "${TERMIN_SDK_PROFILE} builds require an absolute "
            "-DTERMIN_CORE_SDK path")
    endif()
    if(NOT TERMIN_CORE_BUILD_ID)
        message(FATAL_ERROR
            "${TERMIN_SDK_PROFILE} builds require "
            "-DTERMIN_CORE_BUILD_ID")
    endif()
    if(TERMIN_PLATFORM_ANDROID OR TERMIN_PLATFORM_WEB)
        set(_termin_core_artifact_manifest
            "${TERMIN_CORE_SDK}/termin-core-platform.json")
    else()
        set(_termin_core_artifact_manifest
            "${TERMIN_CORE_SDK}/termin-artifacts.json")
    endif()
    if(NOT EXISTS "${_termin_core_artifact_manifest}")
        message(FATAL_ERROR
            "Installed Core artifact manifest is missing: "
            "${_termin_core_artifact_manifest}")
    endif()
    file(READ "${_termin_core_artifact_manifest}" _termin_core_artifact_json)
    string(REGEX MATCH
        "\"native_build_id\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\""
        _termin_core_build_id_match "${_termin_core_artifact_json}")
    if(NOT CMAKE_MATCH_1 STREQUAL TERMIN_CORE_BUILD_ID)
        message(FATAL_ERROR
            "Installed Core build identity mismatch: expected "
            "${TERMIN_CORE_BUILD_ID}, got ${CMAKE_MATCH_1}")
    endif()
    if(TERMIN_PLATFORM_ANDROID OR TERMIN_PLATFORM_WEB)
        string(JSON _termin_core_system GET
            "${_termin_core_artifact_json}" target system)
        string(JSON _termin_core_arch GET
            "${_termin_core_artifact_json}" target architecture)
        string(JSON _termin_core_toolchain_version GET
            "${_termin_core_artifact_json}" target toolchain_version)
        if(TERMIN_PLATFORM_ANDROID)
            set(_termin_expected_core_system android)
            set(_termin_expected_core_arch "${ANDROID_ABI}")
        else()
            set(_termin_expected_core_system web)
            set(_termin_expected_core_arch wasm32)
        endif()
        if(NOT _termin_core_system STREQUAL _termin_expected_core_system)
            message(FATAL_ERROR
                "Installed Core platform mismatch: expected "
                "${_termin_expected_core_system}, got ${_termin_core_system}")
        endif()
        if(NOT _termin_core_arch STREQUAL _termin_expected_core_arch)
            message(FATAL_ERROR
                "Installed Core architecture mismatch: expected "
                "${_termin_expected_core_arch}, got ${_termin_core_arch}")
        endif()
        if(TERMIN_PLATFORM_ANDROID)
            string(JSON _termin_core_api GET
                "${_termin_core_artifact_json}" target api)
            if(NOT _termin_core_api STREQUAL ANDROID_PLATFORM)
                message(FATAL_ERROR
                    "Installed Core Android API mismatch: expected "
                    "${ANDROID_PLATFORM}, got ${_termin_core_api}")
            endif()
        endif()
        if(NOT TERMIN_CORE_TOOLCHAIN_VERSION)
            message(FATAL_ERROR
                "Cross-compiled builds require -DTERMIN_CORE_TOOLCHAIN_VERSION")
        endif()
        if(NOT _termin_core_toolchain_version STREQUAL TERMIN_CORE_TOOLCHAIN_VERSION)
            message(FATAL_ERROR
                "Installed Core toolchain mismatch: expected "
                "${TERMIN_CORE_TOOLCHAIN_VERSION}, got "
                "${_termin_core_toolchain_version}")
        endif()
    endif()
    list(PREPEND CMAKE_PREFIX_PATH "${TERMIN_CORE_SDK}")
    if(TERMIN_BUILD_PYTHON)
        set(nanobind_DIR "${TERMIN_CORE_SDK}/lib/cmake/nanobind"
            CACHE PATH "Installed Core nanobind package" FORCE)
        find_package(nanobind CONFIG REQUIRED NO_DEFAULT_PATH)
    endif()
    set(termin_base_DIR "${TERMIN_CORE_SDK}/lib/cmake/termin_base"
        CACHE PATH "Installed Core termin_base package" FORCE)
    set(termin_dispatch_DIR "${TERMIN_CORE_SDK}/lib/cmake/termin_dispatch"
        CACHE PATH "Installed Core termin_dispatch package" FORCE)
    set(termin_inspect_DIR "${TERMIN_CORE_SDK}/lib/cmake/termin_inspect"
        CACHE PATH "Installed Core termin_inspect package" FORCE)
    find_package(termin_base CONFIG REQUIRED NO_DEFAULT_PATH
        NO_CMAKE_FIND_ROOT_PATH)
    find_package(termin_dispatch CONFIG REQUIRED NO_DEFAULT_PATH
        NO_CMAKE_FIND_ROOT_PATH)
    find_package(termin_inspect CONFIG REQUIRED NO_DEFAULT_PATH
        NO_CMAKE_FIND_ROOT_PATH)
    if(TERMIN_BUILD_PYTHON)
        set(termin_python_host_DIR
            "${TERMIN_CORE_SDK}/lib/cmake/termin_python_host"
            CACHE PATH "Installed Core Python host package" FORCE)
        find_package(termin_python_host CONFIG REQUIRED NO_DEFAULT_PATH
            NO_CMAKE_FIND_ROOT_PATH)
    endif()
    message(STATUS
        "Termin: consuming installed Core ${TERMIN_CORE_BUILD_ID} from "
        "${TERMIN_CORE_SDK}")
endif()

if(TERMIN_BUILD_PYTHON AND NOT TERMIN_USE_INSTALLED_CORE)
    add_subdirectory(termin-nanobind-sdk)
    set(TERMIN_NANOBIND_RUNTIME_TARGET "nanobind-ft")
    include(termin-nanobind-sdk/cmake/TerminNanobindProfile.cmake)
    add_subdirectory(termin-python-host)
endif()

if(NOT TERMIN_USE_INSTALLED_CORE)
    add_subdirectory(termin-base)
endif()

if(TERMIN_SDK_PROFILE STREQUAL "core")
    add_subdirectory(termin-dispatch)
    add_subdirectory(termin-inspect)
elseif(TERMIN_SDK_PROFILE STREQUAL "graphics")
    add_subdirectory(termin-image)
    add_subdirectory(termin-mesh)
    add_subdirectory(termin-graphics)
    add_subdirectory(termin-materials)
    add_subdirectory(termin-render)
    if(TERMIN_ENABLE_SDL)
        add_subdirectory(termin-window)
    endif()
    add_subdirectory(termin-visual-scene)
    add_subdirectory(termin-gui-native)
    add_subdirectory(termin-nodegraph)
    add_subdirectory(tcplot)
    add_subdirectory(tcplot-gui-native)
elseif(TERMIN_PLATFORM_WEB)
    add_subdirectory(termin-image)
    add_subdirectory(termin-modules)
    add_subdirectory(termin-mesh)
    add_subdirectory(termin-scene)
    add_subdirectory(termin-input)
    add_subdirectory(termin-collision)
    add_subdirectory(termin-skeleton)
    add_subdirectory(termin-components/termin-components-mesh)
    add_subdirectory(termin-graphics)
    add_subdirectory(termin-visual-scene)
    add_subdirectory(termin-lighting)
    add_subdirectory(termin-materials)
    add_subdirectory(termin-render)
    add_subdirectory(termin-components/termin-components-skeleton)
    add_subdirectory(termin-display)
    add_subdirectory(termin-components/termin-components-render)
    add_subdirectory(termin-render-passes)
    add_subdirectory(termin-engine)
    add_subdirectory(termin-bootstrap)
    add_subdirectory(termin-runtime)
    add_subdirectory(termin-web-core)
else()
add_subdirectory(termin-profiler-remote)
add_subdirectory(termin-framegraph-remote)
add_subdirectory(termin-image)
add_subdirectory(termin-audio)
add_subdirectory(termin-modules)
add_subdirectory(termin-mesh)
add_subdirectory(termin-glb)
add_subdirectory(termin-csg)
add_subdirectory(termin-voxels)
add_subdirectory(termin-scene)
add_subdirectory(termin-prefab)
add_subdirectory(termin-lighting)
add_subdirectory(termin-input)
add_subdirectory(termin-collision)
add_subdirectory(termin-qopt)
add_subdirectory(termin-robotics)
add_subdirectory(termin-physics-qopt)
add_subdirectory(termin-physics)
add_subdirectory(termin-skeleton)
add_subdirectory(termin-components/termin-components-mesh)
add_subdirectory(termin-components/termin-components-kinematic)

add_subdirectory(termin-graphics)
add_subdirectory(termin-window)
add_subdirectory(termin-visual-scene)
add_subdirectory(termin-materials)
add_subdirectory(termin-render)
add_subdirectory(termin-components/termin-components-collision)
add_subdirectory(termin-components/termin-components-physics)
add_subdirectory(termin-components/termin-components-physics-fem)
add_subdirectory(termin-components/termin-components-skeleton)
add_subdirectory(termin-display)
add_subdirectory(termin-gui-native)
if(NOT TERMIN_PLATFORM_ANDROID)
    add_subdirectory(termin-nodegraph)
    add_subdirectory(tcplot)
    add_subdirectory(tcplot-gui-native)
endif()
add_subdirectory(termin-components/termin-components-ui)
add_subdirectory(termin-components/termin-components-physics-fem-ui)
add_subdirectory(termin-components/termin-components-render)
add_subdirectory(termin-navmesh)
add_subdirectory(termin-components/termin-components-foliage)
add_subdirectory(termin-render-passes)
add_subdirectory(termin-engine)
add_subdirectory(termin-frame-profiler)
add_subdirectory(termin-framegraph-remote-target)
add_subdirectory(termin-framegraph-remote-client)

if(NOT TERMIN_PLATFORM_ANDROID)
    add_subdirectory(termin-profiler)
    add_subdirectory(termin-animation)
endif()

if(NOT TERMIN_PLATFORM_ANDROID)
    add_subdirectory(termin-components/termin-components-animation)
endif()

add_subdirectory(termin-bootstrap)
add_subdirectory(termin-runtime)

if(TERMIN_PLATFORM_ANDROID)
    add_subdirectory(termin-openxr)
    add_subdirectory(termin-android)
endif()

if(NOT TERMIN_PLATFORM_ANDROID)
    add_subdirectory(termin-player)
    add_subdirectory(termin-cli)
    add_subdirectory(termin-app/cpp)
endif()
endif()

if(TERMIN_BUILD_TESTS)
    foreach(_termin_aggregate IN ITEMS
            TERMIN_NATIVE_TEST_TARGETS
            TERMIN_NATIVE_TEST_TARGETS_WITH_WINDOW)
        get_property(
            ${_termin_aggregate}
            GLOBAL
            PROPERTY ${_termin_aggregate}
        )
        list(REMOVE_DUPLICATES ${_termin_aggregate})
        list(LENGTH ${_termin_aggregate} _termin_aggregate_target_count)
        if(_termin_aggregate_target_count EQUAL 0)
            message(FATAL_ERROR
                "TERMIN_BUILD_TESTS=ON produced no targets for "
                "${_termin_aggregate}")
        endif()
    endforeach()
    add_custom_target(termin_native_tests)
    add_dependencies(
        termin_native_tests
        ${TERMIN_NATIVE_TEST_TARGETS}
    )
    add_custom_target(termin_native_tests_with_window)
    add_dependencies(
        termin_native_tests_with_window
        ${TERMIN_NATIVE_TEST_TARGETS_WITH_WINDOW}
    )
endif()

if(NOT TERMIN_USE_INSTALLED_CORE)
    configure_package_config_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/terminConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/terminConfig.cmake
        INSTALL_DESTINATION lib/cmake/termin
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/terminConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/terminConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/terminConfigVersion.cmake
        DESTINATION lib/cmake/termin
    )
endif()
