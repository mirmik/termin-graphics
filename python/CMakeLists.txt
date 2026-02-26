find_package(Python 3.8 COMPONENTS Interpreter Development REQUIRED)

execute_process(
    COMMAND ${Python_EXECUTABLE} -m nanobind --cmake_dir
    OUTPUT_VARIABLE nanobind_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(_tgfx_native
    bindings/tgfx_module.cpp
    bindings/graphics_bindings.cpp
    bindings/shader_bindings.cpp
    bindings/texture_bindings.cpp
    bindings/mesh_bindings.cpp
)

target_link_libraries(_tgfx_native PRIVATE termin_graphics OpenGL::GL)
target_include_directories(_tgfx_native PRIVATE ${CMAKE_SOURCE_DIR}/include)
