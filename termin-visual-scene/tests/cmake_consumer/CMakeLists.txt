cmake_minimum_required(VERSION 3.16)
project(termin_visual_scene_consumer LANGUAGES C CXX)

find_package(termin_visual_scene CONFIG REQUIRED)

add_executable(termin_visual_scene_consumer main.c)
target_link_libraries(termin_visual_scene_consumer PRIVATE
    termin_visual_scene::termin_visual_scene)

add_executable(termin_visual_scene_cpp_consumer main.cpp)
target_link_libraries(termin_visual_scene_cpp_consumer PRIVATE
    termin_visual_scene::termin_visual_scene)
