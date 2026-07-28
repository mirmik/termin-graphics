cmake_minimum_required(VERSION 3.16)
project(termin_visual_scene VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TERMIN_VISUAL_SCENE_BUILD_EXAMPLES
       "Build termin-visual-scene examples" ON)

if(COMMAND termin_require_package)
    termin_require_package(termin_base tcbase::termin_base)
    termin_require_package(termin_graphics tgfx::termin_graphics2)
else()
    find_package(termin_base REQUIRED)
    find_package(termin_graphics REQUIRED)
endif()

if(TERMIN_VISUAL_SCENE_BUILD_EXAMPLES)
    if(COMMAND termin_require_package)
        termin_require_package(termin_window termin_window::termin_window)
    else()
        find_package(termin_window REQUIRED)
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)

add_library(termin_visual_scene SHARED
    src/tc_visual_scene.c
    src/scene2d.cpp
    src/render_snapshot2d.cpp
    src/interaction2d.cpp
)
if(MSVC)
    set_source_files_properties(src/tc_visual_scene.c PROPERTIES
        COMPILE_OPTIONS "/experimental:c11atomics"
    )
endif()
add_library(termin_visual_scene::termin_visual_scene ALIAS termin_visual_scene)
target_compile_definitions(termin_visual_scene PRIVATE
    TERMIN_VISUAL_SCENE_EXPORTS)
target_include_directories(termin_visual_scene PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(termin_visual_scene PUBLIC
    tcbase::termin_base
    tgfx::termin_graphics2
)
termin_set_rpath_lib(termin_visual_scene)

if(TERMIN_VISUAL_SCENE_BUILD_EXAMPLES)
    add_executable(termin_visual_scene_draggable_example
        examples/draggable_primitives.cpp)
    target_link_libraries(termin_visual_scene_draggable_example PRIVATE
        termin_visual_scene
        termin_window::termin_window)
    termin_set_rpath_tool(termin_visual_scene_draggable_example)
    install(TARGETS termin_visual_scene_draggable_example
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(TERMIN_BUILD_TESTS)
    enable_testing()
    add_executable(termin_visual_scene_storage_test
        tests/test_visual_scene_storage.cpp)
    target_link_libraries(termin_visual_scene_storage_test PRIVATE
        termin_visual_scene)
    termin_set_rpath_test(termin_visual_scene_storage_test)
    add_test(NAME termin_visual_scene_storage_test
        COMMAND termin_visual_scene_storage_test)

    add_executable(termin_visual_scene_item_test
        tests/test_scene2d_items.cpp)
    target_link_libraries(termin_visual_scene_item_test PRIVATE
        termin_visual_scene)
    termin_set_rpath_test(termin_visual_scene_item_test)
    add_test(NAME termin_visual_scene_item_test
        COMMAND termin_visual_scene_item_test)

    add_executable(termin_visual_scene_render_snapshot_test
        tests/test_render_snapshot2d.cpp)
    target_link_libraries(termin_visual_scene_render_snapshot_test PRIVATE
        termin_visual_scene)
    termin_set_rpath_test(termin_visual_scene_render_snapshot_test)
    add_test(NAME termin_visual_scene_render_snapshot_test
        COMMAND termin_visual_scene_render_snapshot_test)

    add_executable(termin_visual_scene_interaction_test
        tests/test_interaction2d.cpp)
    target_link_libraries(termin_visual_scene_interaction_test PRIVATE
        termin_visual_scene)
    termin_set_rpath_test(termin_visual_scene_interaction_test)
    add_test(NAME termin_visual_scene_interaction_test
        COMMAND termin_visual_scene_interaction_test)

    if(TARGET termin_visual_scene_draggable_example)
        add_test(NAME termin_visual_scene_draggable_example_smoke
            COMMAND termin_visual_scene_draggable_example --headless-smoke)
        if(COMMAND termin_set_test_build_target)
            termin_set_test_build_target(
                termin_visual_scene_draggable_example_smoke
                termin_visual_scene_draggable_example)
        endif()
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS termin_visual_scene
    EXPORT termin_visual_sceneTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT termin_visual_sceneTargets
    FILE termin_visual_sceneTargets.cmake
    NAMESPACE termin_visual_scene::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_visual_scene
)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/termin_visual_sceneConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/termin_visual_sceneConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_visual_scene
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/termin_visual_sceneConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/termin_visual_sceneConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/termin_visual_sceneConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_visual_scene
)

if(COMMAND termin_label_tests_in_directory)
    termin_label_tests_in_directory("termin-visual-scene")
endif()
