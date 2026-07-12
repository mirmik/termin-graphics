cmake_minimum_required(VERSION 3.16)
project(termin_gui_native VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TERMIN_GUI_NATIVE_BUILD_TESTS "Build termin-gui-native tests" ON)
option(TERMIN_GUI_NATIVE_BUILD_EXAMPLES "Build termin-gui-native examples" OFF)
option(TERMIN_BUILD_PYTHON "Build termin-gui-native Python bindings" OFF)

if(COMMAND termin_require_package)
    termin_require_package(termin_base tcbase::termin_base)
else()
    find_package(termin_base REQUIRED)
endif()
if(COMMAND termin_require_package)
    termin_require_package(termin_graphics tgfx::termin_graphics2)
else()
    find_package(termin_graphics REQUIRED)
endif()
if(COMMAND termin_require_package)
    termin_require_package(termin_inspect termin_inspect::termin_inspect)
else()
    find_package(termin_inspect REQUIRED)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)

add_library(termin_gui_native SHARED
    src/draw_list_renderer.cpp
    src/showcase.cpp
    src/tc_ui_document.c
    src/tc_ui_snapshot.c
    src/tc_ui_serialization.c
    src/tc_widget.c
    src/tc_widget_registry.c
    src/tc_ui_interaction.c
    src/tc_ui_draw_list.cpp
    src/widgets_helpers.cpp
    src/native_widget.cpp
    src/box_layout.cpp
    src/grid_layout.cpp
    src/graphics_scene.cpp
    src/group_box.cpp
    src/splitter.cpp
    src/scroll_area.cpp
    src/scene_view.cpp
    src/tab_view.cpp
    src/panel.cpp
    src/button.cpp
    src/label.cpp
    src/checkbox.cpp
    src/progress_bar.cpp
    src/separator.cpp
    src/text_input.cpp
    src/text_area.cpp
    src/rich_text_model.cpp
    src/rich_text_view.cpp
    src/frame_time_graph.cpp
    src/viewport3d.cpp
    src/slider.cpp
    src/spin_box.cpp
    src/slider_edit.cpp
    src/collection_model.cpp
    src/color_picker_model.cpp
    src/color_picker.cpp
    src/color_dialog.cpp
    src/command_model.cpp
    src/tool_bar.cpp
    src/status_bar.cpp
    src/dialog.cpp
    src/message_box.cpp
    src/input_dialog.cpp
    src/menu.cpp
    src/menu_bar.cpp
    src/selection_model.cpp
    src/list_widget.cpp
    src/file_grid_widget.cpp
    src/file_dialog_model.cpp
    src/file_dialog_overlay.cpp
    src/tree_model.cpp
    src/tree_widget.cpp
    src/table_model.cpp
    src/table_widget.cpp
    src/combo_box.cpp
    src/icon_button.cpp
    src/image_widget.cpp
    src/canvas.cpp
    src/swatch.cpp
    src/spacer.cpp
)
add_library(termin_gui_native::termin_gui_native ALIAS termin_gui_native)

# The installed C++ widget headers are the library API and the Python bindings
# are built as a separate DLL. Export their complete symbol surface on Windows.
if(WIN32)
    set_target_properties(termin_gui_native PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

target_compile_definitions(termin_gui_native PRIVATE TERMIN_GUI_NATIVE_EXPORTS)

target_include_directories(termin_gui_native
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(termin_gui_native
    PUBLIC
        tcbase::termin_base
        tgfx::termin_graphics2
        termin_inspect::termin_inspect
)

termin_set_rpath_lib(termin_gui_native)
if(NOT WIN32)
    set_target_properties(termin_gui_native PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "$ORIGIN;$<TARGET_FILE_DIR:tcbase::termin_base>;$<TARGET_FILE_DIR:tgfx::termin_graphics2>"
    )
endif()

if(MSVC)
    target_compile_options(termin_gui_native PRIVATE /W3 /wd4251 /wd4275)
    target_compile_definitions(termin_gui_native PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(termin_gui_native PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TERMIN_GUI_NATIVE_BUILD_TESTS)
    enable_testing()

    function(termin_gui_native_add_test target)
        add_executable(${target} ${ARGN})
        target_link_libraries(${target} PRIVATE termin_gui_native)
        target_compile_definitions(${target} PRIVATE
            TERMIN_GUI_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        if(MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
        if(NOT WIN32)
            set_target_properties(${target} PROPERTIES
                BUILD_RPATH "$<TARGET_FILE_DIR:termin_gui_native>;$<TARGET_FILE_DIR:tcbase::termin_base>"
            )
        endif()
        add_test(NAME ${target} COMMAND ${target})
        if(NOT WIN32)
            set_tests_properties(${target} PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:termin_gui_native>:$<TARGET_FILE_DIR:tcbase::termin_base>:$ENV{LD_LIBRARY_PATH}"
            )
        endif()
    endfunction()

    termin_gui_native_add_test(termin_gui_native_document_test
        tests/test_tc_ui_document.cpp
    )
    termin_gui_native_add_test(termin_gui_native_draw_list_test
        tests/test_ui_draw_list.cpp
    )
    termin_gui_native_add_test(termin_gui_native_widgets_test
        tests/test_widgets.cpp
        tests/test_widgets_layout.cpp
        tests/test_widgets_collections.cpp
    )
    termin_gui_native_add_test(termin_gui_native_showcase_test
        tests/test_showcase.cpp
    )
    termin_gui_native_add_test(termin_gui_native_file_dialog_test
        tests/test_file_dialog.cpp
    )
    termin_gui_native_add_test(termin_gui_native_color_dialog_test
        tests/test_color_dialog.cpp
    )
    termin_gui_native_add_test(termin_gui_native_widget_registry_test
        tests/test_widget_registry.cpp
    )
    termin_gui_native_add_test(termin_gui_native_ui_snapshot_test
        tests/test_ui_snapshot.cpp
    )
    termin_gui_native_add_test(termin_gui_native_ui_serialization_test
        tests/test_ui_serialization.cpp
    )
    termin_gui_native_add_test(termin_gui_native_rich_text_view_test
        tests/test_rich_text_view.cpp
    )
    termin_gui_native_add_test(termin_gui_native_frame_time_graph_test
        tests/test_frame_time_graph.cpp
    )
    termin_gui_native_add_test(termin_gui_native_viewport3d_test
        tests/test_viewport3d.cpp
    )
    termin_gui_native_add_test(termin_gui_native_scene_view_test
        tests/test_scene_view.cpp
    )
    termin_gui_native_add_test(termin_gui_native_renderer_pixel_smoke
        tests/test_draw_list_renderer_pixel_smoke.cpp
    )
    if(TARGET termin_shaderc)
        add_dependencies(termin_gui_native_renderer_pixel_smoke termin_shaderc)
    endif()
endif()

if(TERMIN_GUI_NATIVE_BUILD_EXAMPLES)
    if(COMMAND termin_require_package)
        termin_require_package(termin_display termin_display::termin_display)
    else()
        find_package(termin_display REQUIRED)
    endif()

    add_library(termin_gui_native_example_support STATIC
        examples/showcase_window.cpp
    )
    target_link_libraries(termin_gui_native_example_support
        PRIVATE
            termin_gui_native
            termin_display::termin_display
    )
    target_include_directories(termin_gui_native_example_support
        PUBLIC
            ${CMAKE_CURRENT_LIST_DIR}/examples
    )
    if(NOT WIN32)
        set_target_properties(termin_gui_native_example_support PROPERTIES
            BUILD_RPATH "$<TARGET_FILE_DIR:termin_gui_native>;$<TARGET_FILE_DIR:termin_display::termin_display>;$<TARGET_FILE_DIR:tgfx::termin_graphics2>;$<TARGET_FILE_DIR:tcbase::termin_base>"
        )
    endif()

    add_executable(termin_gui_native_showcase
        examples/showcase.cpp
    )
    target_link_libraries(termin_gui_native_showcase
        PRIVATE
            termin_gui_native_example_support
    )

    add_executable(termin_gui_native_rect_window
        examples/ui_rect_window.cpp
    )
    target_link_libraries(termin_gui_native_rect_window
        PRIVATE
            termin_gui_native_example_support
    )
    if(NOT WIN32)
        set_target_properties(termin_gui_native_showcase termin_gui_native_rect_window PROPERTIES
            BUILD_RPATH "$<TARGET_FILE_DIR:termin_gui_native>;$<TARGET_FILE_DIR:termin_display::termin_display>;$<TARGET_FILE_DIR:tgfx::termin_graphics2>;$<TARGET_FILE_DIR:tcbase::termin_base>"
        )
    endif()
endif()

if(TERMIN_BUILD_PYTHON)
    find_package(Python 3.8 COMPONENTS Interpreter Development REQUIRED)
    find_package(nanobind CONFIG REQUIRED)

    nanobind_add_module(_gui_native NB_SHARED
        python/bindings/gui_native_module.cpp
        python/bindings/gui_native_bindings_core.cpp
        python/bindings/gui_native_bindings_widgets.cpp
        python/bindings/gui_native_bindings_commands.cpp
        python/bindings/gui_native_bindings_views.cpp
        python/bindings/gui_native_bindings_document.cpp
    )
    target_link_libraries(_gui_native
        PRIVATE
            termin_gui_native
    )
    target_include_directories(_gui_native
        PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/include
    )
    termin_set_rpath_python(_gui_native)
    install(TARGETS _gui_native DESTINATION lib/python/termin/gui_native)
    install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/python/termin/gui_native/
            DESTINATION lib/python/termin/gui_native
            FILES_MATCHING PATTERN "*.py")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS termin_gui_native
    EXPORT termin_gui_nativeTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf"
    DESTINATION share/termin/fonts
)

install(EXPORT termin_gui_nativeTargets
    FILE termin_gui_nativeTargets.cmake
    NAMESPACE termin_gui_native::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_gui_native
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/termin_gui_nativeConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/termin_gui_nativeConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_gui_native
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/termin_gui_nativeConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/termin_gui_nativeConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/termin_gui_nativeConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_gui_native
)

termin_label_tests_in_directory("termin-gui-native")
