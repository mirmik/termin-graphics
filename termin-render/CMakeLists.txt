cmake_minimum_required(VERSION 3.14)
project(termin_render VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TERMIN_RENDER_BUILD_SHARED "Build shared library" ON)
option(TERMIN_RENDER_BUILD_TESTS "Build tests" ON)

find_package(termin_base REQUIRED)
find_package(termin_graphics REQUIRED)
find_package(termin_scene REQUIRED)

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
    src/tc_scene_render_ext.cpp
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
    include/termin/render/tc_scene_render_accessors.hpp
    include/tc_viewport_config.h
    include/tc_component_python_drawable.h
)

if(TERMIN_RENDER_BUILD_SHARED)
    add_library(termin_render SHARED ${TERMIN_RENDER_SOURCES} ${TERMIN_RENDER_HEADERS})
    target_compile_definitions(termin_render PRIVATE TERMIN_RENDER_EXPORTS)
else()
    add_library(termin_render STATIC ${TERMIN_RENDER_SOURCES} ${TERMIN_RENDER_HEADERS})
endif()

add_library(termin_render::termin_render ALIAS termin_render)

set_target_properties(termin_render PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH TRUE
)

target_include_directories(termin_render
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(termin_render PUBLIC cxx_std_20)

target_link_libraries(termin_render PUBLIC tcbase::termin_base tgfx::termin_graphics termin_scene::termin_scene)

if(UNIX AND NOT APPLE)
    target_link_libraries(termin_render PRIVATE m)
endif()

if(MSVC)
    target_compile_options(termin_render PRIVATE /W3)
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
