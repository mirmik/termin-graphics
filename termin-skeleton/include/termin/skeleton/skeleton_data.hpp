#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <optional>

#include "termin/skeleton/bone.hpp"
#include "termin/skeleton/termin_skeleton_api.hpp"

namespace termin {

/**
 * Immutable skeleton definition (bones hierarchy and inverse bind matrices).
 *
 * This is the "template" loaded from GLB/FBX files.
 * SkeletonInstance (Python) holds mutable runtime state.
 */
class TERMIN_SKELETON_API SkeletonData {
public:
    SkeletonData() = default;

    explicit SkeletonData(std::vector<Bone> bones);

    SkeletonData(std::vector<Bone> bones, std::vector<int> root_bone_indices);

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
    void add_bone(Bone bone);

    // Rebuild maps after external modification
    void rebuild_maps();

private:
    void rebuild_name_map();

    void rebuild_root_indices();

    std::vector<Bone> bones_;
    std::vector<int> root_bone_indices_;
    std::unordered_map<std::string, size_t> bone_name_map_;
};

} // namespace termin
