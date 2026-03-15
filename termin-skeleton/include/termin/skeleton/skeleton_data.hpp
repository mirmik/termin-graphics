#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <optional>

#include "termin/skeleton/bone.hpp"

namespace termin {

/**
 * Immutable skeleton definition (bones hierarchy and inverse bind matrices).
 *
 * This is the "template" loaded from GLB/FBX files.
 * SkeletonInstance (Python) holds mutable runtime state.
 */
class SkeletonData {
public:
    SkeletonData() = default;

    explicit SkeletonData(std::vector<Bone> bones)
        : bones_(std::move(bones)) {
        rebuild_maps();
    }

    SkeletonData(std::vector<Bone> bones, std::vector<int> root_bone_indices)
        : bones_(std::move(bones)), root_bone_indices_(std::move(root_bone_indices)) {
        rebuild_name_map();
    }

    // Bone access
    const std::vector<Bone>& bones() const { return bones_; }
    std::vector<Bone>& bones_mut() { return bones_; }

    size_t get_bone_count() const { return bones_.size(); }

    const Bone* get_bone(size_t index) const {
        if (index < bones_.size()) return &bones_[index];
        return nullptr;
    }

    Bone* get_bone_mut(size_t index) {
        if (index < bones_.size()) return &bones_[index];
        return nullptr;
    }

    const Bone* get_bone_by_name(const std::string& name) const {
        auto it = bone_name_map_.find(name);
        if (it != bone_name_map_.end()) {
            return &bones_[it->second];
        }
        return nullptr;
    }

    int get_bone_index(const std::string& name) const {
        auto it = bone_name_map_.find(name);
        if (it != bone_name_map_.end()) {
            return static_cast<int>(it->second);
        }
        return -1;
    }

    // Root bones
    const std::vector<int>& root_bone_indices() const { return root_bone_indices_; }

    // Add bone
    void add_bone(Bone bone) {
        bone_name_map_[bone.name] = bones_.size();
        if (bone.is_root()) {
            root_bone_indices_.push_back(static_cast<int>(bones_.size()));
        }
        bones_.push_back(std::move(bone));
    }

    // Rebuild maps after external modification
    void rebuild_maps() {
        rebuild_name_map();
        rebuild_root_indices();
    }

private:
    void rebuild_name_map() {
        bone_name_map_.clear();
        for (size_t i = 0; i < bones_.size(); ++i) {
            bone_name_map_[bones_[i].name] = i;
        }
    }

    void rebuild_root_indices() {
        root_bone_indices_.clear();
        for (size_t i = 0; i < bones_.size(); ++i) {
            if (bones_[i].is_root()) {
                root_bone_indices_.push_back(static_cast<int>(i));
            }
        }
    }

    std::vector<Bone> bones_;
    std::vector<int> root_bone_indices_;
    std::unordered_map<std::string, size_t> bone_name_map_;
};

} // namespace termin
