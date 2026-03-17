find_package(Python 3.8 COMPONENTS Interpreter Development REQUIRED)
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(_tmesh_native
    bindings/tmesh_module.cpp
    bindings/mesh_bindings.cpp
)

target_link_libraries(_tmesh_native PRIVATE termin_mesh)
target_include_directories(_tmesh_native PRIVATE ${CMAKE_SOURCE_DIR}/include)

if(NOT WIN32)
    set_target_properties(_tmesh_native PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/lib"
    )
endif()

install(TARGETS _tmesh_native DESTINATION lib/python/tmesh)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tmesh/
    DESTINATION lib/python/tmesh
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
)
