#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "termin_visual_scene/scene2d.hpp"

namespace {

    struct Payload {
        tc_graphic_item item{};
        int value = 0;
        int* deletes = nullptr;
        int* destroys = nullptr;
    };

    void on_destroy(tc_graphic_item* item, tc_visual_scene* scene) {
        auto* payload = static_cast<Payload*>(item->body);
        assert(payload);
        assert(scene);
        assert(item->scene == nullptr);
        assert(tc_graphic_item_handle_is_invalid(item->handle));
        ++*payload->destroys;
    }

    const tc_graphic_item_vtable kPayloadVtable{
        .type_name = "termin.visual.test.Payload",
        .local_bounds = nullptr,
        .hit_test = nullptr,
        .paint = nullptr,
        .composition_clip = nullptr,
        .on_destroy = on_destroy,
    };

    void delete_payload(tc_graphic_item* item) {
        auto* payload = static_cast<Payload*>(item->body);
        ++*payload->deletes;
        delete payload;
    }

    Payload* make_payload(int value, int& deletes, int& destroys) {
        auto* result = new Payload{};
        result->value = value;
        result->deletes = &deletes;
        result->destroys = &destroys;
        tc_graphic_item_init_unowned(&result->item, &kPayloadVtable, TC_LANGUAGE_CXX, result);
        return result;
    }

} // namespace

int main() {
    using termin::visual::TcVisualScene;

    int deletes = 0;
    int destroys = 0;
    const auto scene_handle = tc_visual_scene_create();
    TcVisualScene scene{scene_handle};

    auto* root = make_payload(1, deletes, destroys);
    const auto root_result = scene.adopt(&root->item, delete_payload);
    assert(root_result);
    const auto root_handle = *root_result;
    auto* child = make_payload(2, deletes, destroys);
    const auto child_result = scene.adopt(&child->item, delete_payload);
    assert(child_result);
    const auto child_handle = *child_result;
    auto* grandchild = make_payload(3, deletes, destroys);
    const auto grandchild_result = scene.adopt(&grandchild->item, delete_payload);
    assert(grandchild_result);
    const auto grandchild_handle = *grandchild_result;
    assert(!tc_graphic_item_handle_is_invalid(root_handle));
    assert(scene.size() == 3);

    assert(tc_graphic_item_append_child(&root->item, &child->item));
    assert(tc_graphic_item_append_child(&child->item, &grandchild->item));
    assert(root->item.child_count == 1);
    assert(child->item.parent == &root->item);
    assert(grandchild->item.parent == &child->item);

    child->item.local_transform = tc_affine2f_translation(12.0f, -3.0f);
    child->item.visible = false;
    child->item.opacity = 0.5f;
    child->item.z_order = 17;

    auto* replacement = make_payload(20, deletes, destroys);
    assert(scene.replace(child_handle, &replacement->item, delete_payload));
    assert(deletes == 1);
    assert(destroys == 1);
    assert(scene.resolve(child_handle) == &replacement->item);
    assert(replacement->item.parent == &root->item);
    assert(replacement->item.child_count == 1);
    assert(replacement->item.children[0] == &grandchild->item);
    assert(grandchild->item.parent == &replacement->item);
    assert(replacement->item.local_transform.tx == 12.0f);
    assert(!replacement->item.visible);
    assert(replacement->item.opacity == 0.5f);
    assert(replacement->item.z_order == 17);

    assert(scene.destroy(child_handle));
    assert(deletes == 3);
    assert(destroys == 3);
    assert(scene.resolve(child_handle) == nullptr);
    assert(scene.resolve(grandchild_handle) == nullptr);
    assert(root->item.child_count == 0);

    auto* reused = make_payload(4, deletes, destroys);
    const auto reused_result = scene.adopt(&reused->item, delete_payload);
    assert(reused_result);
    const auto reused_handle = *reused_result;
    assert(scene.resolve(child_handle) == nullptr);
    if (reused_handle.index == child_handle.index) {
        assert(reused_handle.generation != child_handle.generation);
    }

    const auto foreign_handle = tc_visual_scene_create();
    TcVisualScene foreign{foreign_handle};
    auto* foreign_item = make_payload(5, deletes, destroys);
    foreign.adopt(&foreign_item->item, delete_payload);
    assert(!tc_graphic_item_append_child(&root->item, &foreign_item->item));

    scene.clear();
    foreign.clear();
    assert(deletes == 6);
    assert(destroys == 6);
    tc_visual_scene_destroy(foreign_handle);
    tc_visual_scene_destroy(scene_handle);
}
