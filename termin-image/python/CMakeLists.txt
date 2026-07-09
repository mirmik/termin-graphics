find_package(Python 3.8 COMPONENTS Interpreter Development REQUIRED)
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(_image_native NB_SHARED
    bindings/image_module.cpp
)

target_link_libraries(_image_native PRIVATE termin_image)
target_include_directories(_image_native PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
termin_set_rpath_python(_image_native)

install(TARGETS _image_native DESTINATION lib/python/termin/image)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/termin/image/
    DESTINATION lib/python/termin/image
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
)
