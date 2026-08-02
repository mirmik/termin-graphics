cmake_minimum_required(VERSION 3.16)
project(tcplot_gui_native VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TCPLOT_GUI_NATIVE_BUILD_TESTS
    "Build tcplot-gui-native tests when built standalone" OFF)

if(COMMAND termin_require_package)
    termin_require_package(termin_gui_native termin_gui_native::termin_gui_native)
    termin_require_package(tcplot tcplot::tcplot)
else()
    find_package(termin_gui_native REQUIRED)
    find_package(tcplot REQUIRED)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(tcplot_gui_native SHARED
    src/plot2d.cpp
    src/widget_registration.cpp
)
add_library(tcplot_gui_native::tcplot_gui_native ALIAS tcplot_gui_native)

target_compile_definitions(tcplot_gui_native PRIVATE TCPLOT_GUI_NATIVE_EXPORTS)
target_include_directories(tcplot_gui_native PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(tcplot_gui_native PUBLIC
    termin_gui_native::termin_gui_native
    tcplot::tcplot
)

set_target_properties(tcplot_gui_native PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(COMMAND termin_set_rpath_lib)
    termin_set_rpath_lib(tcplot_gui_native)
endif()

if(TERMIN_BUILD_TESTS OR TCPLOT_GUI_NATIVE_BUILD_TESTS)
    add_executable(tcplot_gui_native_plot2d_test tests/test_plot2d.cpp)
    target_link_libraries(tcplot_gui_native_plot2d_test PRIVATE
        tcplot_gui_native
    )
    set_target_properties(tcplot_gui_native_plot2d_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    if(COMMAND termin_set_rpath_test)
        termin_set_rpath_test(tcplot_gui_native_plot2d_test)
    endif()
    add_test(NAME tcplot_gui_native_plot2d_test
        COMMAND tcplot_gui_native_plot2d_test)
endif()

install(TARGETS tcplot_gui_native
    EXPORT tcplot_gui_nativeTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT tcplot_gui_nativeTargets
    FILE tcplot_gui_nativeTargets.cmake
    NAMESPACE tcplot_gui_native::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tcplot_gui_native
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tcplot_gui_nativeConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/tcplot_gui_nativeConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tcplot_gui_native
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/tcplot_gui_nativeConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/tcplot_gui_nativeConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/tcplot_gui_nativeConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tcplot_gui_native
)

if(COMMAND termin_label_tests_in_directory)
    termin_label_tests_in_directory("tcplot-gui-native")
endif()
