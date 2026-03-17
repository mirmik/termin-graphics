cmake_minimum_required(VERSION 3.14)
project(termin_render VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TERMIN_RENDER_BUILD_SHARED "Build shared library" ON)
option(TERMIN_RENDER_BUILD_TESTS "Build tests" ON)
option(TERMIN_BUILD_PYTHON "Build Python bindings for termin-render" OFF)

find_package(termin_base REQUIRED)
find_package(termin_graphics REQUIRED)
find_package(termin_inspect REQUIRED)
find_package(termin_scene REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)

set(TERMIN_RENDER_SOURCES
    src/termin_render_version.c
    src/tc_component_python_drawable.c
    src/tc_drawable_capability.c
    src/tc_scene_drawable.c
    src/tc_scene_pipeline_template.c
    src/tc_scene_render_mount.c
    src/tc_scene_render_state.c
    src/tc_scene_skybox.c
    src/tc_viewport_config.c
    src/tc_pass.c
    src/tc_pipeline.c
    src/tc_frame_graph.c
    src/tc_scene_render_ext.cpp
    src/drawable.cpp
    src/fbo_pool.cpp
    src/frame_pass.cpp
    src/frame_graph_debugger_core.cpp
    src/graph_compiler.cpp
    src/graph_data.cpp
    src/render_pipeline.cpp
    src/render_engine.cpp
    src/scene_pipeline_template.cpp
    src/shadow.cpp
    src/tc_project_settings.c
    src/tc_inspect_pass_adapter.cpp
    src/tc_pass_ref.cpp
)

set(TERMIN_RENDER_HEADERS
    include/termin_render/termin_render.h
    include/termin_render/version.h
    include/core/tc_scene_lighting.h
    include/core/tc_scene_pipeline_template.h
    include/core/tc_scene_render_mount.h
    include/core/tc_scene_render_state.h
    include/core/tc_scene_skybox.h
    include/core/tc_drawable_capability.h
    include/core/tc_drawable_protocol.h
    include/core/tc_scene_drawable.h
    include/render/tc_pipeline_pool.h
    include/render/tc_pass.h
    include/render/tc_pipeline.h
    include/render/tc_frame_graph.h
    include/tc_pipeline_registry.h
    include/inspect/tc_inspect_pass_adapter.h
    include/termin/render/tc_scene_render_accessors.hpp
    include/termin/render/render_export.hpp
    include/termin/render/resource_spec.hpp
    include/termin/render/fbo_pool.hpp
    include/termin/render/render_context.hpp
    include/termin/render/execute_context.hpp
    include/termin/render/drawable.hpp
    include/termin/render/tc_pass.hpp
    include/termin/render/frame_pass.hpp
    include/termin/render/frame_graph_debugger_core.hpp
    include/termin/render/graph_compiler.hpp
    include/termin/render/graph_data.hpp
    include/termin/render/render_pipeline.hpp
    include/termin/render/render_engine.hpp
    include/termin/render/scene_pipeline_template.hpp
    include/termin/lighting/shadow.hpp
    include/termin/lighting/shadow_settings.hpp
    include/tc_project_settings.h
    include/tc_viewport_config.h
    include/tc_component_python_drawable.h
)

if(TERMIN_RENDER_BUILD_SHARED)
    add_library(termin_render SHARED ${TERMIN_RENDER_SOURCES} ${TERMIN_RENDER_HEADERS})
    target_compile_definitions(termin_render PRIVATE TERMIN_RENDER_EXPORTS TC_EXPORTS)
else()
    add_library(termin_render STATIC ${TERMIN_RENDER_SOURCES} ${TERMIN_RENDER_HEADERS})
endif()

add_library(termin_render::termin_render ALIAS termin_render)

set_target_properties(termin_render PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
termin_set_rpath_lib(termin_render)

target_include_directories(termin_render
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(termin_render PUBLIC cxx_std_20)

target_link_libraries(termin_render PUBLIC tcbase::termin_base tgfx::termin_graphics termin_scene::termin_scene termin_inspect::termin_inspect)

if(UNIX AND NOT APPLE)
    target_link_libraries(termin_render PRIVATE m)
endif()

if(MSVC)
    target_compile_options(termin_render PRIVATE /W3 /wd4251 /wd4275)
    target_compile_definitions(termin_render PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(termin_render PRIVATE -Wall -Wextra)
endif()

if(TERMIN_RENDER_BUILD_TESTS)
    enable_testing()

    add_executable(termin_render_drawable_test tests/test_drawable_capability.c)
    target_link_libraries(termin_render_drawable_test PRIVATE termin_render)
    add_test(NAME termin_render_drawable_test COMMAND termin_render_drawable_test)

    set_target_properties(
        termin_render_drawable_test
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

if(TERMIN_BUILD_PYTHON)
    find_package(Python 3.8 COMPONENTS Interpreter Development REQUIRED)
    find_package(nanobind CONFIG REQUIRED)

    nanobind_add_module(_render_framework_native NB_SHARED
        python/render_framework_bindings.cpp
        python/scene_pipeline_template_bindings.cpp
        python/tc_pass_bindings.cpp
    )
    target_link_libraries(_render_framework_native PRIVATE termin_render termin_inspect::termin_inspect_python)
    target_include_directories(_render_framework_native PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    termin_set_rpath_python(_render_framework_native)
    install(TARGETS _render_framework_native DESTINATION lib/python/termin/render_framework)
endif()

install(TARGETS termin_render
    EXPORT termin_renderTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
)

install(EXPORT termin_renderTargets
    FILE termin_renderTargets.cmake
    NAMESPACE termin_render::
    DESTINATION lib/cmake/termin_render
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/termin_renderConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/termin_renderConfig.cmake
    INSTALL_DESTINATION lib/cmake/termin_render
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/termin_renderConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/termin_renderConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/termin_renderConfigVersion.cmake
    DESTINATION lib/cmake/termin_render
)

if(MSVC AND TERMIN_RENDER_BUILD_SHARED)
    # Keep the local SDK import library in sync when Windows install leaves stale files locked.
    add_custom_command(TARGET termin_render POST_BUILD
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "try { Copy-Item -Force '$<TARGET_LINKER_FILE:termin_render>' '${CMAKE_CURRENT_SOURCE_DIR}/../sdk/lib/termin_render.lib' -ErrorAction Stop } catch { Write-Host $_.Exception.Message }"
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "try { Copy-Item -Force '$<TARGET_FILE:termin_render>' '${CMAKE_CURRENT_SOURCE_DIR}/../sdk/bin/termin_render.dll' -ErrorAction Stop } catch { Write-Host $_.Exception.Message }"
    )
endif()
