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
    termin_require_package(termin_inspect termin_inspect::termin_inspect)
    termin_require_package(termin_graphics tgfx::termin_graphics2)
else()
    find_package(termin_base REQUIRED)
    find_package(termin_inspect REQUIRED)
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

set(_TERMIN_VISUAL_SCENE_LIBRARY_TYPE SHARED)
if(EMSCRIPTEN)
    set(_TERMIN_VISUAL_SCENE_LIBRARY_TYPE STATIC)
endif()

add_library(termin_visual_scene ${_TERMIN_VISUAL_SCENE_LIBRARY_TYPE}
    src/tc_graphic_item.c
    src/tc_visual_scene.c
    src/tc_visual_scene_item2d.cpp
    src/tc_builtin_items2d.cpp
    src/graphic_item2d.cpp
    src/native_graphic_item2d.cpp
    src/graphic_item_paint_context2d.cpp
    src/items/group_item2d.cpp
    src/items/rect_item2d.cpp
    src/items/rounded_rect_item2d.cpp
    src/items/ellipse_item2d.cpp
    src/items/path_item2d.cpp
    src/items/polyline_item2d.cpp
    src/items/text_item2d.cpp
    src/items/image_item2d.cpp
    src/items/hit_region_item2d.cpp
    src/items/custom_batch_item2d.cpp
    src/scene2d.cpp
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
target_include_directories(termin_visual_scene PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(termin_visual_scene PUBLIC
    tcbase::termin_base
    termin_inspect::termin_inspect
    tgfx::termin_graphics2
)
termin_set_rpath_lib(termin_visual_scene)

if(TERMIN_BUILD_PYTHON)
    add_subdirectory(python)
endif()

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

    add_executable(termin_visual_scene_c_api_test
        tests/test_visual_scene_c_api.cpp)
    target_link_libraries(termin_visual_scene_c_api_test PRIVATE
        termin_visual_scene)
    termin_set_rpath_test(termin_visual_scene_c_api_test)
    add_test(NAME termin_visual_scene_c_api_test
        COMMAND termin_visual_scene_c_api_test)

    add_executable(termin_visual_scene_c_header_test
        tests/test_visual_scene_c_api.c)
    target_link_libraries(termin_visual_scene_c_header_test PRIVATE
        termin_visual_scene)
    termin_set_rpath_test(termin_visual_scene_c_header_test)
    add_test(NAME termin_visual_scene_c_header_test
        COMMAND termin_visual_scene_c_header_test)

    add_executable(termin_visual_scene_item_test
        tests/test_scene2d_items.cpp)
    target_link_libraries(termin_visual_scene_item_test PRIVATE
        termin_visual_scene)
    termin_set_rpath_test(termin_visual_scene_item_test)
    add_test(NAME termin_visual_scene_item_test
        COMMAND termin_visual_scene_item_test)

    add_executable(termin_visual_scene_render_test
        tests/test_scene_render2d.cpp)
    target_link_libraries(termin_visual_scene_render_test PRIVATE
        termin_visual_scene)
    termin_set_rpath_test(termin_visual_scene_render_test)
    add_test(NAME termin_visual_scene_render_test
        COMMAND termin_visual_scene_render_test)

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
        add_test(NAME termin_visual_scene_draggable_example_shader_smoke
            COMMAND termin_visual_scene_draggable_example --shader-smoke)
        add_test(
            NAME termin_visual_scene_draggable_example_missing_shader_smoke
            COMMAND termin_visual_scene_draggable_example
                --shader-smoke-missing-artifacts)
        set_tests_properties(
            termin_visual_scene_draggable_example_shader_smoke
            PROPERTIES
                TIMEOUT 30
                SKIP_RETURN_CODE 77
                SKIP_REGULAR_EXPRESSION
                    "Failed to create Vulkan device: Failed to create Vulkan instance|Failed to create Vulkan device: No Vulkan-capable GPU found")
        set_tests_properties(
            termin_visual_scene_draggable_example_missing_shader_smoke
            PROPERTIES
                TIMEOUT 30
                WILL_FAIL TRUE
                SKIP_RETURN_CODE 77)
        if(COMMAND termin_set_test_build_target)
            termin_set_test_build_target(
                termin_visual_scene_draggable_example_smoke
                termin_visual_scene_draggable_example)
            termin_set_test_build_target(
                termin_visual_scene_draggable_example_shader_smoke
                termin_visual_scene_draggable_example)
            termin_set_test_build_target(
                termin_visual_scene_draggable_example_missing_shader_smoke
                termin_visual_scene_draggable_example)
        endif()
        if(COMMAND termin_add_test_labels)
            if(TGFX2_ENABLE_VULKAN)
                termin_add_test_labels(
                    termin_visual_scene_draggable_example_shader_smoke
                    "termin:capability:vulkan")
                termin_add_test_labels(
                    termin_visual_scene_draggable_example_missing_shader_smoke
                    "termin:capability:vulkan")
            elseif(TGFX2_ENABLE_D3D11)
                termin_add_test_labels(
                    termin_visual_scene_draggable_example_shader_smoke
                    "termin:capability:d3d11")
                termin_add_test_labels(
                    termin_visual_scene_draggable_example_missing_shader_smoke
                    "termin:capability:d3d11")
            endif()
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
