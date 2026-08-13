cmake_minimum_required(VERSION 3.20)
project(termin_graphics_installed_consumers LANGUAGES CXX)

if(NOT DEFINED GRAPHICS_SDK_ROOT OR NOT IS_ABSOLUTE "${GRAPHICS_SDK_ROOT}")
    message(FATAL_ERROR "GRAPHICS_SDK_ROOT must name the absolute relocated Graphics SDK")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY ON)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)

macro(find_graphics_package package_name)
    find_package(${package_name} CONFIG REQUIRED PATHS "${GRAPHICS_SDK_ROOT}" NO_DEFAULT_PATH)
endmacro()

find_graphics_package(termin_base)
find_graphics_package(termin_mesh)
find_graphics_package(termin_graphics)
find_graphics_package(termin_materials)
find_graphics_package(termin_skeleton)
find_graphics_package(termin_animation)
find_graphics_package(termin_glb)

add_executable(graphics_native_consumer native_consumer.cpp)
target_link_libraries(
    graphics_native_consumer PRIVATE
    tcbase::termin_base
    tmesh::termin_mesh
    tgfx::termin_graphics
    termin_materials::termin_materials
    termin_skeleton::termin_skeleton
    termin_animation::termin_animation
    termin_glb::termin_glb
)
