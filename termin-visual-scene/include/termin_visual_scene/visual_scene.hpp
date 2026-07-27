#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include "termin_visual_scene/tc_visual_scene.h"

namespace termin::visual {

using GraphicItemHandle = tc_graphic_item_handle;
using GraphicItemView = tc_graphic_item_view;

class VisualSceneStorage {
public:
    VisualSceneStorage() : scene_(tc_visual_scene_create()) {
        if (!scene_) throw std::runtime_error("failed to create visual scene storage");
    }
    ~VisualSceneStorage() { tc_visual_scene_destroy(scene_); }

    VisualSceneStorage(const VisualSceneStorage&) = delete;
    VisualSceneStorage& operator=(const VisualSceneStorage&) = delete;
    VisualSceneStorage(VisualSceneStorage&& other) noexcept
        : scene_(std::exchange(other.scene_, nullptr)) {}
    VisualSceneStorage& operator=(VisualSceneStorage&& other) noexcept {
        if (this != &other) {
            tc_visual_scene_destroy(scene_);
            scene_ = std::exchange(other.scene_, nullptr);
        }
        return *this;
    }

    GraphicItemHandle adopt(
        void* payload,
        tc_graphic_item_deleter deleter = nullptr,
        void* deleter_user_data = nullptr,
        GraphicItemHandle parent = tc_graphic_item_handle_invalid()) {
        GraphicItemHandle result = tc_graphic_item_handle_invalid();
        if (!tc_visual_scene_adopt(
                scene_, payload, deleter, deleter_user_data, parent, &result)) {
            throw std::runtime_error("failed to adopt graphic item");
        }
        return result;
    }

    GraphicItemHandle create(
        GraphicItemHandle parent = tc_graphic_item_handle_invalid()) {
        GraphicItemHandle result = tc_graphic_item_handle_invalid();
        if (!tc_visual_scene_create_item(scene_, parent, &result)) {
            throw std::runtime_error("failed to create graphic item");
        }
        return result;
    }

    bool resolve(GraphicItemHandle item, GraphicItemView& out) {
        return tc_visual_scene_resolve(scene_, item, &out);
    }
    bool reparent(GraphicItemHandle item, GraphicItemHandle parent) {
        return tc_visual_scene_reparent(scene_, item, parent);
    }
    bool detach(GraphicItemHandle item) {
        return tc_visual_scene_detach(scene_, item);
    }
    bool destroy_leaf(GraphicItemHandle item) {
        return tc_visual_scene_destroy_leaf(scene_, item);
    }
    bool destroy_subtree(GraphicItemHandle item) {
        return tc_visual_scene_destroy_subtree(scene_, item);
    }
    void clear() { tc_visual_scene_clear(scene_); }
    std::size_t size() { return tc_visual_scene_item_count(scene_); }
    std::uint64_t id() const { return tc_visual_scene_id(scene_); }
    tc_visual_scene* native_handle() noexcept { return scene_; }

private:
    tc_visual_scene* scene_ = nullptr;
};

}  // namespace termin::visual
