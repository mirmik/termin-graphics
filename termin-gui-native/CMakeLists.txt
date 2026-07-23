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
option(TERMIN_GUI_NATIVE_BUILD_WINDOW_INTEGRATION
       "Build optional termin-window adapters and compatibility hosts" ON)

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
if(TERMIN_GUI_NATIVE_BUILD_WINDOW_INTEGRATION)
    if(COMMAND termin_require_package)
        termin_require_package(termin_window termin_window::termin_window)
    else()
        find_package(termin_window REQUIRED)
    endif()
elseif(TERMIN_BUILD_PYTHON OR TERMIN_GUI_NATIVE_BUILD_EXAMPLES)
    message(FATAL_ERROR
        "Python bindings and examples currently require "
        "TERMIN_GUI_NATIVE_BUILD_WINDOW_INTEGRATION=ON")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)

add_library(termin_gui_native SHARED
    src/document.cpp
    src/document_snapshot.cpp
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
    src/overlay_layout.cpp
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
    src/frame_timeline.cpp
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
    src/tree_table_model.cpp
    src/tree_table_widget.cpp
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

add_library(termin_gui_native_document_renderer SHARED
    src/document_renderer.cpp
    src/dynamic_texture_lease.cpp
)
add_library(termin_gui_native::document_renderer ALIAS
    termin_gui_native_document_renderer)
set_target_properties(termin_gui_native_document_renderer PROPERTIES
    EXPORT_NAME document_renderer
)
if(WIN32)
    set_target_properties(termin_gui_native_document_renderer PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
target_compile_definitions(termin_gui_native_document_renderer
    PRIVATE TERMIN_GUI_NATIVE_HOST_EXPORTS)
target_include_directories(termin_gui_native_document_renderer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(termin_gui_native_document_renderer
    PUBLIC
        termin_gui_native
)
termin_set_rpath_lib(termin_gui_native_document_renderer)
if(NOT WIN32)
    set_target_properties(termin_gui_native_document_renderer PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "$ORIGIN;$<TARGET_FILE_DIR:termin_gui_native>;$<TARGET_FILE_DIR:tgfx::termin_graphics2>;$<TARGET_FILE_DIR:tcbase::termin_base>"
    )
endif()

add_library(termin_gui_native_offscreen SHARED
    src/offscreen_composition.cpp
)
add_library(termin_gui_native::offscreen ALIAS termin_gui_native_offscreen)
set_target_properties(termin_gui_native_offscreen PROPERTIES
    EXPORT_NAME offscreen
)
if(WIN32)
    set_target_properties(termin_gui_native_offscreen PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
target_compile_definitions(termin_gui_native_offscreen
    PRIVATE TERMIN_GUI_NATIVE_HOST_EXPORTS)
target_include_directories(termin_gui_native_offscreen
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(termin_gui_native_offscreen
    PUBLIC
        termin_gui_native_document_renderer
    PRIVATE
        ${CMAKE_DL_LIBS}
)
termin_set_rpath_lib(termin_gui_native_offscreen)
if(NOT WIN32)
    set_target_properties(termin_gui_native_offscreen PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "$ORIGIN;$<TARGET_FILE_DIR:termin_gui_native_document_renderer>;$<TARGET_FILE_DIR:termin_gui_native>;$<TARGET_FILE_DIR:tgfx::termin_graphics2>;$<TARGET_FILE_DIR:tcbase::termin_base>"
    )
endif()

if(TERMIN_GUI_NATIVE_BUILD_WINDOW_INTEGRATION)
# Optional application-facing input adapter. Keeping it in a separate target
# leaves the document/widget core independent from native window lifecycle.
add_library(termin_gui_native_window_input STATIC
    src/window_input.cpp
)
add_library(termin_gui_native::window_input ALIAS termin_gui_native_window_input)
set_target_properties(termin_gui_native_window_input PROPERTIES
    EXPORT_NAME window_input
)
target_include_directories(termin_gui_native_window_input
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(termin_gui_native_window_input
    PUBLIC
        termin_gui_native
        termin_window::termin_window
)

add_library(termin_gui_native_application_host SHARED
    src/application_host.cpp
    src/dynamic_texture_lease_window.cpp
)
add_library(termin_gui_native::application_host ALIAS termin_gui_native_application_host)
set_target_properties(termin_gui_native_application_host PROPERTIES
    EXPORT_NAME application_host
)
if(WIN32)
    set_target_properties(termin_gui_native_application_host PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
target_compile_definitions(termin_gui_native_application_host
    PRIVATE TERMIN_GUI_NATIVE_HOST_EXPORTS)
target_include_directories(termin_gui_native_application_host
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(termin_gui_native_application_host
    PUBLIC
        termin_gui_native
        termin_gui_native_document_renderer
        termin_window::termin_window
    PRIVATE
        termin_gui_native_window_input
        ${CMAKE_DL_LIBS}
)
termin_set_rpath_lib(termin_gui_native_application_host)
if(NOT WIN32)
    set_target_properties(termin_gui_native_application_host PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "$ORIGIN;$<TARGET_FILE_DIR:termin_gui_native>;$<TARGET_FILE_DIR:termin_window::termin_window>;$<TARGET_FILE_DIR:tgfx::termin_graphics2>;$<TARGET_FILE_DIR:tcbase::termin_base>"
    )
endif()

# Optional borrowed binding between one application-owned BackendWindow and
# one native-widget Document. Applications opt into this target explicitly;
# neither termin-window nor the widget/document core depends on it.
add_library(termin_gui_native_window_adapter SHARED
    src/window_adapter.cpp
)
add_library(termin_gui_native::window_adapter ALIAS termin_gui_native_window_adapter)
set_target_properties(termin_gui_native_window_adapter PROPERTIES
    EXPORT_NAME window_adapter
)
if(WIN32)
    set_target_properties(termin_gui_native_window_adapter PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
target_compile_definitions(termin_gui_native_window_adapter
    PRIVATE TERMIN_GUI_NATIVE_HOST_EXPORTS)
target_include_directories(termin_gui_native_window_adapter
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(termin_gui_native_window_adapter
    PUBLIC
        termin_gui_native_document_renderer
        termin_window::termin_window
    PRIVATE
        termin_gui_native_window_input
)
termin_set_rpath_lib(termin_gui_native_window_adapter)
if(NOT WIN32)
    set_target_properties(termin_gui_native_window_adapter PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "$ORIGIN;$<TARGET_FILE_DIR:termin_gui_native_document_renderer>;$<TARGET_FILE_DIR:termin_gui_native>;$<TARGET_FILE_DIR:termin_window::termin_window>;$<TARGET_FILE_DIR:tgfx::termin_graphics2>;$<TARGET_FILE_DIR:tcbase::termin_base>"
    )
endif()
endif()

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
    termin_gui_native_add_test(termin_gui_native_frame_timeline_test
        tests/test_frame_timeline.cpp
    )
    termin_gui_native_add_test(termin_gui_native_viewport3d_test
        tests/test_viewport3d.cpp
    )
    termin_gui_native_add_test(termin_gui_native_overlay_layout_test
        tests/test_overlay_layout.cpp
    )
    termin_gui_native_add_test(termin_gui_native_scene_view_test
        tests/test_scene_view.cpp
    )
    if(TARGET termin_shaderc)
        set(_termin_gui_native_offscreen_test_shaderc "$<TARGET_FILE:termin_shaderc>")
    else()
        find_program(_termin_gui_native_offscreen_test_shaderc
            NAMES termin_shaderc
            HINTS ${CMAKE_PREFIX_PATH}
            PATH_SUFFIXES bin
            REQUIRED)
    endif()
    if(TERMIN_GUI_NATIVE_BUILD_WINDOW_INTEGRATION)
    termin_gui_native_add_test(termin_gui_native_window_input_test
        tests/test_window_input.cpp
    )
    target_link_libraries(termin_gui_native_window_input_test
        PRIVATE termin_gui_native_window_input)
    termin_gui_native_add_test(termin_gui_native_window_adapter_test
        tests/test_window_adapter.cpp
    )
    target_link_libraries(termin_gui_native_window_adapter_test
        PRIVATE termin_gui_native_window_adapter)
    target_compile_definitions(termin_gui_native_window_adapter_test
        PRIVATE
            TERMIN_GUI_NATIVE_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf"
    )
    set_tests_properties(termin_gui_native_window_adapter_test PROPERTIES
        TIMEOUT 15
        SKIP_RETURN_CODE 77)
    termin_gui_native_add_test(termin_gui_native_application_host_contract_test
        tests/test_application_host_contract.cpp
    )
    target_link_libraries(termin_gui_native_application_host_contract_test
        PRIVATE termin_gui_native_application_host)
    termin_gui_native_add_test(termin_gui_native_application_host_live_test
        tests/test_application_host_live.cpp
    )
    target_link_libraries(termin_gui_native_application_host_live_test
        PRIVATE termin_gui_native_application_host)
    if(TARGET termin_shaderc)
        set(_termin_gui_native_test_shaderc "$<TARGET_FILE:termin_shaderc>")
        add_dependencies(termin_gui_native_application_host_live_test termin_shaderc)
    else()
        find_program(_termin_gui_native_test_shaderc
            NAMES termin_shaderc
            HINTS ${CMAKE_PREFIX_PATH}
            PATH_SUFFIXES bin
            REQUIRED)
    endif()
    target_compile_definitions(termin_gui_native_application_host_live_test
        PRIVATE
            TERMIN_GUI_NATIVE_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf"
            TERMIN_GUI_NATIVE_TEST_SHADERC="${_termin_gui_native_test_shaderc}"
    )
    set_tests_properties(termin_gui_native_application_host_live_test PROPERTIES
        TIMEOUT 15
        SKIP_RETURN_CODE 77
        SKIP_REGULAR_EXPRESSION "No available video device|Vulkan support is either not configured in SDL|Could not initialize OpenGL")
    termin_gui_native_add_test(termin_gui_native_application_host_injection_test
        tests/test_application_host_injection.cpp
    )
    target_link_libraries(termin_gui_native_application_host_injection_test
        PRIVATE termin_gui_native_application_host)
    target_compile_definitions(termin_gui_native_application_host_injection_test
        PRIVATE
            TERMIN_GUI_NATIVE_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf"
    )
    set_tests_properties(termin_gui_native_application_host_injection_test PROPERTIES
        TIMEOUT 15
        SKIP_RETURN_CODE 77)
    termin_gui_native_add_test(termin_gui_native_offscreen_application_test
        tests/test_offscreen_application.cpp
    )
    target_link_libraries(termin_gui_native_offscreen_application_test
        PRIVATE termin_gui_native_application_host)
    if(TARGET termin_shaderc)
        add_dependencies(termin_gui_native_offscreen_application_test termin_shaderc)
    endif()
    target_compile_definitions(termin_gui_native_offscreen_application_test
        PRIVATE
            TERMIN_GUI_NATIVE_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf"
            TERMIN_GUI_NATIVE_TEST_SHADERC="${_termin_gui_native_offscreen_test_shaderc}"
    )
    set_tests_properties(termin_gui_native_offscreen_application_test PROPERTIES
        TIMEOUT 30
        SKIP_RETURN_CODE 77
        ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=")
    termin_gui_native_add_test(termin_gui_native_dynamic_texture_lease_test
        tests/test_dynamic_texture_lease.cpp
    )
    target_link_libraries(termin_gui_native_dynamic_texture_lease_test
        PRIVATE termin_gui_native_application_host)
    target_compile_definitions(termin_gui_native_dynamic_texture_lease_test
        PRIVATE
            TERMIN_GUI_NATIVE_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf"
    )
    endif()
    termin_gui_native_add_test(termin_gui_native_offscreen_composition_test
        tests/test_offscreen_composition.cpp
    )
    target_link_libraries(termin_gui_native_offscreen_composition_test
        PRIVATE termin_gui_native_offscreen)
    if(TARGET termin_shaderc)
        add_dependencies(termin_gui_native_offscreen_composition_test termin_shaderc)
    endif()
    target_compile_definitions(termin_gui_native_offscreen_composition_test
        PRIVATE
            TERMIN_GUI_NATIVE_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf"
            TERMIN_GUI_NATIVE_TEST_SHADERC="${_termin_gui_native_offscreen_test_shaderc}"
    )
    set_tests_properties(termin_gui_native_offscreen_composition_test PROPERTIES
        TIMEOUT 30
        SKIP_RETURN_CODE 77
        ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=")
    termin_gui_native_add_test(termin_gui_native_renderer_pixel_smoke
        tests/test_draw_list_renderer_pixel_smoke.cpp
    )
    if(TARGET termin_shaderc)
        add_dependencies(termin_gui_native_renderer_pixel_smoke termin_shaderc)
        set_property(TEST termin_gui_native_renderer_pixel_smoke PROPERTY
            ENVIRONMENT "TERMIN_SHADERC=$<TARGET_FILE:termin_shaderc>"
        )
    endif()
endif()

if(TERMIN_GUI_NATIVE_BUILD_EXAMPLES)
    add_library(termin_gui_native_example_support STATIC
        examples/showcase_window.cpp
    )
    target_link_libraries(termin_gui_native_example_support
        PRIVATE
            termin_gui_native_application_host
    )
    target_include_directories(termin_gui_native_example_support
        PUBLIC
            ${CMAKE_CURRENT_LIST_DIR}/examples
    )
    if(NOT WIN32)
        set_target_properties(termin_gui_native_example_support PROPERTIES
            BUILD_RPATH "$<TARGET_FILE_DIR:termin_gui_native>;$<TARGET_FILE_DIR:termin_gui_native_application_host>;$<TARGET_FILE_DIR:termin_window::termin_window>;$<TARGET_FILE_DIR:tgfx::termin_graphics2>;$<TARGET_FILE_DIR:tcbase::termin_base>"
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
            BUILD_RPATH "$<TARGET_FILE_DIR:termin_gui_native>;$<TARGET_FILE_DIR:termin_gui_native_application_host>;$<TARGET_FILE_DIR:termin_window::termin_window>;$<TARGET_FILE_DIR:tgfx::termin_graphics2>;$<TARGET_FILE_DIR:tcbase::termin_base>"
        )
    endif()
endif()

if(TERMIN_BUILD_PYTHON)
    find_package(Python 3.8 COMPONENTS Interpreter Development REQUIRED)
    find_package(nanobind CONFIG REQUIRED)

    nanobind_add_module(_gui_native NB_SHARED
        python/bindings/gui_native_module.cpp
        python/bindings/gui_native_bindings_runtime.cpp
        python/bindings/gui_native_bindings_core.cpp
        python/bindings/gui_native_bindings_widgets.cpp
        python/bindings/gui_native_bindings_commands.cpp
        python/bindings/gui_native_bindings_views.cpp
        python/bindings/gui_native_bindings_views_scene.cpp
        python/bindings/gui_native_bindings_views_collections.cpp
        python/bindings/gui_native_bindings_views_controls.cpp
        python/bindings/gui_native_bindings_document.cpp
        python/bindings/gui_native_bindings_application_host.cpp
    )
    target_link_libraries(_gui_native
        PRIVATE
            termin_gui_native_application_host
            termin_gui_native_offscreen
            termin_gui_native_window_adapter
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

install(TARGETS
    termin_gui_native
    termin_gui_native_document_renderer
    termin_gui_native_offscreen
    EXPORT termin_gui_nativeTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(TERMIN_GUI_NATIVE_BUILD_WINDOW_INTEGRATION)
    install(TARGETS
        termin_gui_native_window_input
        termin_gui_native_application_host
        termin_gui_native_window_adapter
        EXPORT termin_gui_nativeWindowTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

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

if(TERMIN_GUI_NATIVE_BUILD_WINDOW_INTEGRATION)
    install(EXPORT termin_gui_nativeWindowTargets
        FILE termin_gui_nativeWindowTargets.cmake
        NAMESPACE termin_gui_native::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_gui_native
    )
endif()

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

if(COMMAND termin_label_tests_in_directory)
    termin_label_tests_in_directory("termin-gui-native")
endif()
