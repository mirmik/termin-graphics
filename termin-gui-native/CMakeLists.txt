cmake_minimum_required(VERSION 3.16)
project(termin_gui_native VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TERMIN_GUI_NATIVE_BUILD_TESTS "Build termin-gui-native tests" ON)

if(COMMAND termin_require_package)
    termin_require_package(termin_base tcbase::termin_base)
else()
    find_package(termin_base REQUIRED)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)

add_library(termin_gui_native SHARED
    src/tc_ui_document.cpp
)
add_library(termin_gui_native::termin_gui_native ALIAS termin_gui_native)

target_compile_definitions(termin_gui_native PRIVATE TERMIN_GUI_NATIVE_EXPORTS)

target_include_directories(termin_gui_native
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(termin_gui_native
    PUBLIC
        tcbase::termin_base
)

termin_set_rpath_lib(termin_gui_native)

if(MSVC)
    target_compile_options(termin_gui_native PRIVATE /W3 /wd4251 /wd4275)
    target_compile_definitions(termin_gui_native PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(termin_gui_native PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TERMIN_GUI_NATIVE_BUILD_TESTS)
    enable_testing()

    add_executable(termin_gui_native_document_test
        tests/test_tc_ui_document.cpp
    )
    target_link_libraries(termin_gui_native_document_test PRIVATE termin_gui_native)
    if(NOT WIN32)
        set_target_properties(termin_gui_native_document_test PROPERTIES
            BUILD_RPATH "$<TARGET_FILE_DIR:termin_gui_native>;$<TARGET_FILE_DIR:tcbase::termin_base>"
        )
    endif()
    add_test(NAME termin_gui_native_document_test COMMAND termin_gui_native_document_test)
    if(NOT WIN32)
        set_tests_properties(termin_gui_native_document_test PROPERTIES
            ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:termin_gui_native>:$<TARGET_FILE_DIR:tcbase::termin_base>:$ENV{LD_LIBRARY_PATH}"
        )
    endif()
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
