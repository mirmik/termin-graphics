find_package(Python 3.8 COMPONENTS Interpreter Development REQUIRED)
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(_tcplot_native
    bindings/tcplot_module.cpp
    bindings/colors_and_data_bindings.cpp
    bindings/camera_bindings.cpp
    bindings/engine_bindings.cpp
)

target_link_libraries(_tcplot_native PRIVATE
    tcplot
    tgfx::termin_graphics2
    tmesh::termin_mesh
    tcbase::termin_base
    OpenGL::GL
)
target_include_directories(_tcplot_native PRIVATE ${CMAKE_SOURCE_DIR}/include)

if(NOT WIN32)
    set_target_properties(_tcplot_native PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/lib;${CMAKE_INSTALL_PREFIX}/lib"
    )
endif()

install(TARGETS _tcplot_native DESTINATION lib/python/tcplot)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tcplot/
    DESTINATION lib/python/tcplot
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
)
