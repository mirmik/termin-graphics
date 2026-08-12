#include <memory>

#include <termin_visual_scene/items/primitive_item3d.hpp>

int main() {
    auto geometry = std::make_shared<termin::visual::PrimitiveGeometry3D>();
    geometry->vertices = {
        {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    geometry->triangles = {0, 1, 2};
    termin::visual::PrimitiveItem3D item(geometry);
    return item.geometry() == geometry ? 0 : 1;
}
