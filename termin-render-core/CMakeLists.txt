cmake_minimum_required(VERSION 3.14)
project(termin_render_core VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TERMIN_RENDER_CORE_BUILD_SHARED "Build the render core as a shared library" ON)
option(TERMIN_RENDER_CORE_BUILD_TESTS "Build render-core tests" ON)

if(COMMAND termin_require_package)
    termin_require_package(termin_base tcbase::termin_base)
    termin_require_package(termin_graphics tgfx::termin_graphics tgfx::termin_graphics2)
    termin_require_package(termin_inspect termin_inspect::termin_inspect)
    termin_require_package(termin_materials termin_materials::termin_materials)
else()
    find_package(termin_base REQUIRED)
    find_package(termin_graphics REQUIRED)
    find_package(termin_inspect REQUIRED)
    find_package(termin_materials REQUIRED)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)

set(TERMIN_RENDER_CORE_SOURCES
    src/tc_pass.c
    src/tc_pipeline_template_registry.c
    src/tc_pipeline.c
    src/tc_frame_graph.c
    src/fbo_pool.cpp
    src/frame_pass.cpp
    src/builtin_passes.cpp
    src/frame_uniforms.cpp
    src/frame_graph_capture.cpp
    src/frame_graph_resource_registry.cpp
    src/execute_context.cpp
    src/graph_alias_pass.cpp
    src/material_pipeline.cpp
    src/material_pipeline_contracts.cpp
    src/material_pipeline_shader_assembler.cpp
    src/material_ubo_apply.cpp
    src/render_pipeline.cpp
    src/color_output.cpp
    src/render_engine.cpp
    src/render_execution_capabilities.cpp
    src/render_item_mesh.cpp
    src/render_item_source.cpp
    src/render_item_snapshot.cpp
    src/render_item_submission.cpp
    src/render_task.cpp
    src/world2d_ordering.cpp
    src/world2d_quad_geometry.cpp
    src/shader_abi.cpp
    src/shader_resource_apply.cpp
    src/shader_contract.cpp
    src/tgfx2_bridge.cpp
    src/vertex_transform_contracts.cpp
    src/tc_render_core_settings.c
    src/tc_inspect_pass_adapter.cpp
    src/tc_pass_ref.cpp
    src/unknown_pass.cpp
    src/unknown_pass_ops.cpp
)

if(TERMIN_RENDER_CORE_BUILD_SHARED)
    add_library(termin_render_core SHARED ${TERMIN_RENDER_CORE_SOURCES})
    target_compile_definitions(termin_render_core PRIVATE TERMIN_RENDER_CORE_EXPORTS TC_EXPORTS)
else()
    add_library(termin_render_core STATIC ${TERMIN_RENDER_CORE_SOURCES})
endif()
add_library(termin_render_core::termin_render_core ALIAS termin_render_core)

if(COMMAND termin_enable_unity_build_for)
    termin_enable_unity_build_for(termin_render_core)
endif()
if(COMMAND termin_enable_pch_for)
    termin_enable_pch_for(termin_render_core)
endif()

set_target_properties(termin_render_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
termin_set_rpath_lib(termin_render_core)

target_include_directories(termin_render_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(termin_render_core PUBLIC cxx_std_20)
target_link_libraries(termin_render_core PUBLIC
    tcbase::termin_base
    tgfx::termin_graphics
    tgfx::termin_graphics2
    termin_inspect::termin_inspect
    termin_materials::termin_materials
)

if(UNIX AND NOT APPLE)
    target_link_libraries(termin_render_core PRIVATE m)
endif()
if(MSVC)
    target_compile_options(termin_render_core PRIVATE /W3 /wd4251 /wd4275)
    target_compile_definitions(termin_render_core PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(termin_render_core PRIVATE -Wall -Wextra)
endif()

if(TERMIN_RENDER_CORE_BUILD_TESTS)
    enable_testing()

    function(termin_render_core_add_test name source)
        set(target "termin_render_core_${name}_test")
        add_executable(${target} "tests/${source}")
        target_include_directories(${target} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/guard)
        target_link_libraries(${target} PRIVATE termin_render_core::termin_render_core)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        add_test(NAME ${target} COMMAND ${target})
    endfunction()

    termin_render_core_add_test(material_pipeline_contracts test_material_pipeline_contracts.cpp)
    termin_render_core_add_test(material_pipeline_shader_assembler test_material_pipeline_shader_assembler.cpp)
    termin_render_core_add_test(material_ubo_apply test_material_ubo_apply.cpp)
    termin_render_core_add_test(pass_ownership test_pass_ownership.cpp)
    termin_render_core_add_test(pipeline_template test_pipeline_template.cpp)
    termin_render_core_add_test(raster_pass_contract test_raster_pass_contract.cpp)
    termin_render_core_add_test(render_execution test_render_execution.cpp)
    termin_render_core_add_test(render_item_snapshot test_render_item_snapshot.cpp)
    termin_render_core_add_test(render_item_submission test_render_item_submission.cpp)
    termin_render_core_add_test(render_task test_render_task.cpp)
    termin_render_core_add_test(shader_abi test_shader_abi.cpp)
    termin_render_core_add_test(shader_contract test_shader_contract.cpp)
    termin_render_core_add_test(unknown_pass test_unknown_pass.cpp)
    termin_render_core_add_test(vertex_transform_contracts test_vertex_transform_contracts.cpp)
    termin_render_core_add_test(world2d_ordering test_world2d_ordering.cpp)
    termin_render_core_add_test(world2d_quad_geometry test_world2d_quad_geometry.cpp)
endif()

install(TARGETS termin_render_core
    EXPORT termin_render_coreTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT termin_render_coreTargets
    FILE termin_render_coreTargets.cmake
    NAMESPACE termin_render_core::
    DESTINATION lib/cmake/termin_render_core
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/termin_render_coreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/termin_render_coreConfig.cmake
    INSTALL_DESTINATION lib/cmake/termin_render_core
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/termin_render_coreConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/termin_render_coreConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/termin_render_coreConfigVersion.cmake
    DESTINATION lib/cmake/termin_render_core
)

if(COMMAND termin_label_tests_in_directory)
    termin_label_tests_in_directory("termin-render-core")
endif()
