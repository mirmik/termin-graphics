cmake_minimum_required(VERSION 3.16)
project(termin_gui_native_offscreen_consumer LANGUAGES CXX)

find_package(termin_gui_native CONFIG REQUIRED COMPONENTS offscreen)

add_executable(termin_gui_native_offscreen_consumer main.cpp)
target_compile_features(termin_gui_native_offscreen_consumer PRIVATE cxx_std_20)
target_link_libraries(
    termin_gui_native_offscreen_consumer
    PRIVATE termin_gui_native::offscreen
)
