#include "termin_visual_scene/visual_scene.hpp"

#include <atomic>
#include <cassert>
#include <thread>
#include <vector>

namespace {

struct Payload {
    tc_graphic_item item;
    int value;
    std::atomic<int>* deletes;
    std::atomic<int>* destroys;
    tc_visual_scene* reentry_scene = nullptr;
};

void on_destroy(tc_graphic_item* item) {
    auto* payload = static_cast<Payload*>(item->body);
    assert(payload != nullptr);
    assert(item->scene == nullptr);
    assert(tc_graphic_item_handle_is_invalid(item->handle));
    payload->destroys->fetch_add(1);
    if (payload->reentry_scene != nullptr) {
        (void)tc_visual_scene_item_count(payload->reentry_scene);
    }
}

const tc_graphic_item_vtable kPayloadVtable{
    .type_name = "termin.visual.test.Payload",
    .on_destroy = on_destroy,
};

void delete_payload(tc_graphic_item* item) {
    auto* payload = static_cast<Payload*>(item->body);
    payload->deletes->fetch_add(1);
    delete payload;
}

Payload* payload(
    int value,
    std::atomic<int>& deletes,
    std::atomic<int>& destroys) {
    auto* result = new Payload{};
    result->value = value;
    result->deletes = &deletes;
    result->destroys = &destroys;
    tc_graphic_item_init_unowned(
        &result->item,
        &kPayloadVtable,
        TC_LANGUAGE_CXX,
        result);
    return result;
}

}  // namespace

int main() {
    using termin::visual::GraphicItemView;
    using termin::visual::VisualSceneStorage;

    std::atomic<int> deletes{0};
    std::atomic<int> destroys{0};
    VisualSceneStorage scene;
    auto* root_payload = payload(1, deletes, destroys);
    root_payload->reentry_scene = scene.native_handle();
    const auto root = scene.adopt(&root_payload->item, delete_payload);
    const auto empty = scene.create(root);
    assert(scene.destroy_leaf(empty));
    auto* child_payload = payload(2, deletes, destroys);
    const auto child =
        scene.adopt(&child_payload->item, delete_payload, root);
    auto* grandchild_payload = payload(3, deletes, destroys);
    const auto grandchild =
        scene.adopt(&grandchild_payload->item, delete_payload, child);
    assert(scene.size() == 3);

    GraphicItemView view{};
    assert(scene.resolve(child, view));
    assert(view.item == &child_payload->item);
    assert(view.item->body == child_payload);
    assert(view.item->native_language == TC_LANGUAGE_CXX);
    assert(tc_graphic_item_is_attached(view.item));
    assert(view.parent.index == root.index);
    tc_graphic_item_state state{};
    assert(scene.get_state(child, state));
    assert(state.visible);
    assert(state.enabled);
    assert(state.opacity == 1.0f);
    state.local_transform = tc_affine2f_translation(12.0f, -3.0f);
    state.visible = false;
    state.opacity = 0.5f;
    state.z_order = 17;
    assert(scene.set_state(child, state));
    tc_graphic_item_state updated{};
    assert(scene.get_state(child, updated));
    assert(updated.local_transform.tx == 12.0f);
    assert(updated.local_transform.ty == -3.0f);
    assert(!updated.visible);
    assert(updated.opacity == 0.5f);
    assert(updated.z_order == 17);
    state.opacity = 2.0f;
    assert(!scene.set_state(child, state));
    assert(!scene.destroy_leaf(root));
    assert(!scene.reparent(root, grandchild));

    assert(scene.detach(child));
    assert(scene.resolve(child, view));
    assert(tc_graphic_item_handle_is_invalid(view.parent));
    assert(scene.reparent(child, root));

    assert(scene.destroy_subtree(child));
    assert(deletes.load() == 2);
    assert(destroys.load() == 2);
    assert(!scene.resolve(child, view));
    assert(!scene.resolve(grandchild, view));

    // Slot reuse never revives a stale generation.
    auto* replacement_payload = payload(4, deletes, destroys);
    const auto replacement =
        scene.adopt(&replacement_payload->item, delete_payload);
    assert(replacement.index == child.index || replacement.index == grandchild.index);
    assert(replacement.generation != child.generation ||
           replacement.index != child.index);
    assert(!scene.resolve(child, view));

    // Cross-scene topology is rejected, and failed adoption rolls ownership back.
    VisualSceneStorage foreign;
    auto* foreign_payload = payload(5, deletes, destroys);
    const auto foreign_root =
        foreign.adopt(&foreign_payload->item, delete_payload);
    assert(!scene.reparent(replacement, foreign_root));
    auto* rejected_payload = payload(6, deletes, destroys);
    tc_graphic_item_handle rejected = tc_graphic_item_handle_invalid();
    assert(!tc_visual_scene_adopt(
        scene.native_handle(), &rejected_payload->item, delete_payload,
        foreign_root, &rejected));
    assert(deletes.load() == 3);
    assert(destroys.load() == 2);

    // The scene mutex protects calls from multiple caller threads.
    constexpr int kThreads = 6;
    constexpr int kItems = 100;
    std::vector<std::thread> threads;
    for (int thread_index = 0; thread_index < kThreads; ++thread_index) {
        threads.emplace_back([&scene, &deletes, &destroys, thread_index] {
            for (int i = 0; i < kItems; ++i) {
                auto* threaded =
                    payload(
                        1000 + thread_index * kItems + i,
                        deletes,
                        destroys);
                const auto item = scene.adopt(
                    &threaded->item, delete_payload);
                assert(scene.destroy_leaf(item));
            }
        });
    }
    for (auto& thread : threads) thread.join();
    assert(deletes.load() == 3 + kThreads * kItems);

    scene.clear();
    foreign.clear();
    assert(scene.size() == 0);
    assert(foreign.size() == 0);
    assert(deletes.load() == 6 + kThreads * kItems);
    assert(destroys.load() == 5 + kThreads * kItems);
}
