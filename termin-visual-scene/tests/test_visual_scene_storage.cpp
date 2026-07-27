#include "termin_visual_scene/visual_scene.hpp"

#include <atomic>
#include <cassert>
#include <thread>
#include <vector>

namespace {

struct Payload {
    int value;
    std::atomic<int>* deletes;
};

void delete_payload(void* raw, void*) {
    auto* payload = static_cast<Payload*>(raw);
    payload->deletes->fetch_add(1);
    delete payload;
}

Payload* payload(int value, std::atomic<int>& deletes) {
    return new Payload{value, &deletes};
}

}  // namespace

int main() {
    using termin::visual::GraphicItemView;
    using termin::visual::VisualSceneStorage;

    std::atomic<int> deletes{0};
    VisualSceneStorage scene;
    const auto root = scene.adopt(payload(1, deletes), delete_payload);
    const auto empty = scene.create(root);
    assert(scene.destroy_leaf(empty));
    const auto child = scene.adopt(payload(2, deletes), delete_payload, nullptr, root);
    const auto grandchild =
        scene.adopt(payload(3, deletes), delete_payload, nullptr, child);
    assert(scene.size() == 3);

    GraphicItemView view{};
    assert(scene.resolve(child, view));
    assert(view.parent.index == root.index);
    assert(!scene.destroy_leaf(root));
    assert(!scene.reparent(root, grandchild));

    assert(scene.detach(child));
    assert(scene.resolve(child, view));
    assert(tc_graphic_item_handle_is_invalid(view.parent));
    assert(scene.reparent(child, root));

    assert(scene.destroy_subtree(child));
    assert(deletes.load() == 2);
    assert(!scene.resolve(child, view));
    assert(!scene.resolve(grandchild, view));

    // Slot reuse never revives a stale generation.
    const auto replacement = scene.adopt(payload(4, deletes), delete_payload);
    assert(replacement.index == child.index || replacement.index == grandchild.index);
    assert(replacement.generation != child.generation ||
           replacement.index != child.index);
    assert(!scene.resolve(child, view));

    // Cross-scene topology is rejected, and failed adoption rolls ownership back.
    VisualSceneStorage foreign;
    const auto foreign_root =
        foreign.adopt(payload(5, deletes), delete_payload);
    assert(!scene.reparent(replacement, foreign_root));
    tc_graphic_item_handle rejected = tc_graphic_item_handle_invalid();
    assert(!tc_visual_scene_adopt(
        scene.native_handle(), payload(6, deletes), delete_payload, nullptr,
        foreign_root, &rejected));
    assert(deletes.load() == 3);

    // The scene mutex protects calls from multiple caller threads.
    constexpr int kThreads = 6;
    constexpr int kItems = 100;
    std::vector<std::thread> threads;
    for (int thread_index = 0; thread_index < kThreads; ++thread_index) {
        threads.emplace_back([&scene, &deletes, thread_index] {
            for (int i = 0; i < kItems; ++i) {
                const auto item = scene.adopt(
                    payload(1000 + thread_index * kItems + i, deletes),
                    delete_payload);
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
    assert(deletes.load() == 5 + kThreads * kItems);
}
