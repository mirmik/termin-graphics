find_package(Python 3.8 COMPONENTS Interpreter Development REQUIRED)
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(_tgfx_native
    bindings/tgfx_module.cpp
    bindings/graphics_bindings.cpp
    bindings/shader_bindings.cpp
    bindings/texture_bindings.cpp
)

target_link_libraries(_tgfx_native PRIVATE termin_graphics tmesh::termin_mesh OpenGL::GL)
target_include_directories(_tgfx_native PRIVATE ${CMAKE_SOURCE_DIR}/include)

if(NOT WIN32)
    set_target_properties(_tgfx_native PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/lib;${CMAKE_INSTALL_PREFIX}/lib"
    )
endif()
