cmake_minimum_required(VERSION 3.16)
project(termin_window VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TERMIN_WINDOW_BUILD_SHARED "Build termin-window as a shared library" ON)
option(TERMIN_WINDOW_BUILD_TESTS "Build termin-window tests" OFF)
option(TERMIN_WINDOW_BUILD_WINDOW_TESTS "Build live termin-window smoke tests" OFF)
option(TERMIN_WINDOW_BUILD_D3D11_WINDOW_TEST
       "Build the termin-window D3D11 presentation smoke test"
       ${TERMIN_WINDOW_BUILD_WINDOW_TESTS})
if(TERMIN_WINDOW_BUILD_TESTS)
    include(CTest)
endif()

if(COMMAND termin_require_package)
    termin_require_package(termin_base tcbase::termin_base)
    termin_require_package(termin_graphics tgfx::termin_graphics2)
else()
    find_package(termin_base REQUIRED)
    find_package(termin_graphics REQUIRED)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)
include(TerminSDL2)

termin_configure_sdl2()
set(TERMIN_WINDOW_HAS_SDL ${SDL2_FOUND})

set(TERMIN_WINDOW_REQUIRES_SDL2_PACKAGE OFF)
if(TERMIN_WINDOW_HAS_SDL)
    foreach(_termin_window_sdl_link_item IN LISTS SDL2_LIBRARIES)
        if(_termin_window_sdl_link_item MATCHES "^SDL2::")
            set(TERMIN_WINDOW_REQUIRES_SDL2_PACKAGE ON)
        endif()
    endforeach()
endif()

set(TERMIN_WINDOW_SOURCES src/backend_window.cpp)
if(TERMIN_WINDOW_HAS_SDL)
    list(APPEND TERMIN_WINDOW_SOURCES src/sdl_backend_window.cpp)
endif()

if(TERMIN_WINDOW_BUILD_SHARED)
    add_library(termin_window SHARED ${TERMIN_WINDOW_SOURCES})
    target_compile_definitions(termin_window PRIVATE TERMIN_WINDOW_EXPORTS)
else()
    add_library(termin_window STATIC ${TERMIN_WINDOW_SOURCES})
endif()
add_library(termin_window::termin_window ALIAS termin_window)

target_include_directories(termin_window
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(termin_window
    PUBLIC
        tcbase::termin_base
        tgfx::termin_graphics2
)

# This package is deliberately below engine display integration. Keep the
# dependency boundary executable as a configure-time invariant.
get_target_property(_termin_window_public_dependencies
    termin_window INTERFACE_LINK_LIBRARIES)
foreach(_termin_window_forbidden_dependency
        termin_scene:: termin_input:: termin_render:: termin_materials::)
    string(FIND
        "${_termin_window_public_dependencies}"
        "${_termin_window_forbidden_dependency}"
        _termin_window_forbidden_index)
    if(NOT _termin_window_forbidden_index EQUAL -1)
        message(FATAL_ERROR
            "termin-window must not depend on ${_termin_window_forbidden_dependency}")
    endif()
endforeach()

if(TERMIN_WINDOW_HAS_SDL)
    target_compile_definitions(termin_window PUBLIC TERMIN_WINDOW_HAS_SDL)
    target_include_directories(termin_window PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(termin_window PUBLIC ${SDL2_LIBRARIES})
endif()

find_package(Vulkan QUIET)
if(TGFX2_ENABLE_VULKAN)
    if(NOT Vulkan_FOUND)
        find_package(Vulkan REQUIRED)
    endif()
    target_compile_definitions(termin_window PRIVATE TGFX2_HAS_VULKAN)
    target_link_libraries(termin_window PRIVATE Vulkan::Vulkan)
endif()

if(TGFX2_ENABLE_D3D11)
    if(NOT WIN32)
        message(FATAL_ERROR "TGFX2_ENABLE_D3D11 requires Windows")
    endif()
    target_compile_definitions(termin_window PRIVATE TGFX2_HAS_D3D11)
    target_link_libraries(termin_window PRIVATE d3d11 dxgi)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(termin_window PRIVATE m)
endif()

set_property(TARGET termin_window PROPERTY
    TERMIN_WINDOW_HAS_SDL ${TERMIN_WINDOW_HAS_SDL}
)
termin_set_rpath_lib(termin_window)
if(NOT WIN32)
    set_target_properties(termin_window PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "$ORIGIN;$<TARGET_FILE_DIR:tcbase::termin_base>;$<TARGET_FILE_DIR:tgfx::termin_graphics2>"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

if(MSVC)
    target_compile_options(termin_window PRIVATE /W3 /wd4251 /wd4275)
    target_compile_definitions(termin_window PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(termin_window PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TERMIN_WINDOW_BUILD_TESTS)
    add_executable(termin_window_public_contract
        tests/test_public_contract.cpp)
    target_link_libraries(termin_window_public_contract PRIVATE termin_window)
    add_test(NAME termin_window_public_contract
        COMMAND termin_window_public_contract)
    if(COMMAND termin_add_test_labels)
        termin_add_test_labels(termin_window_public_contract
            "termin:capability:host")
    endif()
endif()

if(TERMIN_WINDOW_BUILD_TESTS AND TERMIN_WINDOW_HAS_SDL)
    add_executable(termin_window_backend_triangle
        tests/test_backend_window_triangle.cpp)
    target_link_libraries(termin_window_backend_triangle
        PRIVATE termin_window)
    termin_copy_sdl2_runtime(termin_window_backend_triangle)

    add_executable(termin_window_multiwindow_event_routing
        tests/test_multiwindow_event_routing.cpp)
    target_link_libraries(termin_window_multiwindow_event_routing
        PRIVATE termin_window)
    termin_copy_sdl2_runtime(termin_window_multiwindow_event_routing)

endif()

if(TERMIN_WINDOW_BUILD_TESTS AND TERMIN_WINDOW_HAS_SDL
   AND TERMIN_WINDOW_BUILD_WINDOW_TESTS)

    add_test(NAME termin_window_backend_triangle
        COMMAND termin_window_backend_triangle)
    set_tests_properties(termin_window_backend_triangle PROPERTIES
        TIMEOUT 10
        ENVIRONMENT "TERMIN_BACKEND=opengl"
        SKIP_RETURN_CODE 77
        SKIP_REGULAR_EXPRESSION "SDL_InitSubSystem failed|No available video device|Vulkan support is either not configured in SDL")

    add_test(NAME termin_window_multiwindow_event_routing
        COMMAND termin_window_multiwindow_event_routing)
    set_tests_properties(termin_window_multiwindow_event_routing PROPERTIES
        TIMEOUT 10
        ENVIRONMENT "TERMIN_BACKEND=opengl"
        SKIP_RETURN_CODE 77
        SKIP_REGULAR_EXPRESSION "SDL_InitSubSystem failed|No available video device|Could not initialize OpenGL")

    add_test(NAME termin_window_backend_triangle_immediate
        COMMAND termin_window_backend_triangle)
    set_tests_properties(termin_window_backend_triangle_immediate PROPERTIES
        TIMEOUT 10
        ENVIRONMENT "TERMIN_BACKEND=opengl;TERMIN_TEST_PRESENTATION_MODE=immediate"
        SKIP_RETURN_CODE 77
        SKIP_REGULAR_EXPRESSION "SDL_InitSubSystem failed|No available video device")

    if(TGFX2_ENABLE_VULKAN)
        add_test(NAME termin_window_backend_triangle_vulkan_vsync
            COMMAND termin_window_backend_triangle)
        set_tests_properties(termin_window_backend_triangle_vulkan_vsync PROPERTIES
            TIMEOUT 10
            ENVIRONMENT "TERMIN_BACKEND=vulkan;TERMIN_TEST_PRESENTATION_MODE=vsync"
            SKIP_RETURN_CODE 77
            SKIP_REGULAR_EXPRESSION "SDL_InitSubSystem failed|No available video device|Vulkan support is either not configured in SDL|No Vulkan physical devices found")

        add_test(NAME termin_window_backend_triangle_vulkan_immediate
            COMMAND termin_window_backend_triangle)
        set_tests_properties(termin_window_backend_triangle_vulkan_immediate PROPERTIES
            TIMEOUT 10
            ENVIRONMENT "TERMIN_BACKEND=vulkan;TERMIN_TEST_PRESENTATION_MODE=immediate"
            SKIP_RETURN_CODE 77
            SKIP_REGULAR_EXPRESSION "SDL_InitSubSystem failed|No available video device|Vulkan support is either not configured in SDL|No Vulkan physical devices found")
    endif()

    if(COMMAND termin_add_test_labels)
        termin_add_test_labels(termin_window_backend_triangle
            "termin:capability:window")
        termin_add_test_labels(termin_window_backend_triangle_immediate
            "termin:capability:window"
            "termin:capability:opengl")
        termin_add_test_labels(termin_window_multiwindow_event_routing
            "termin:capability:window"
            "termin:capability:opengl")
        if(TGFX2_ENABLE_VULKAN)
            termin_add_test_labels(termin_window_backend_triangle_vulkan_vsync
                "termin:capability:window"
                "termin:capability:vulkan")
            termin_add_test_labels(termin_window_backend_triangle_vulkan_immediate
                "termin:capability:window"
                "termin:capability:vulkan")
        endif()
    endif()
endif()

if(TERMIN_WINDOW_BUILD_TESTS AND TERMIN_WINDOW_HAS_SDL
   AND TGFX2_ENABLE_D3D11 AND TERMIN_WINDOW_BUILD_D3D11_WINDOW_TEST)
    add_executable(termin_window_backend_d3d11_present
        tests/test_backend_window_d3d11_present.cpp)
    target_link_libraries(termin_window_backend_d3d11_present
        PRIVATE termin_window)
    termin_copy_sdl2_runtime(termin_window_backend_d3d11_present)
    add_test(NAME termin_window_backend_d3d11_present
        COMMAND termin_window_backend_d3d11_present)
    set_tests_properties(termin_window_backend_d3d11_present PROPERTIES
        TIMEOUT 10
        SKIP_RETURN_CODE 77
        SKIP_REGULAR_EXPRESSION "SDL_InitSubSystem failed|No available video device|D3D11CreateDevice failed")
    if(COMMAND termin_add_test_labels)
        termin_add_test_labels(termin_window_backend_d3d11_present
            "termin:capability:window"
            "termin:capability:d3d11")
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS termin_window
    EXPORT termin_windowTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(TERMIN_WINDOW_HAS_SDL AND WIN32 AND SDL2_DLL)
    termin_install_sdl2_runtime(bin)
endif()

install(EXPORT termin_windowTargets
    FILE termin_windowTargets.cmake
    NAMESPACE termin_window::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_window
)

configure_package_config_file(
    ${CMAKE_CURRENT_LIST_DIR}/cmake/termin_windowConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/termin_windowConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_window
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/termin_windowConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/termin_windowConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/termin_windowConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_window
)

if(COMMAND termin_label_tests_in_directory)
    termin_label_tests_in_directory("termin-window")
endif()
