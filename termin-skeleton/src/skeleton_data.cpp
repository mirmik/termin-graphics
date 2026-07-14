#include <termin/skeleton/skeleton_data.hpp>

namespace termin {

SkeletonData::SkeletonData(std::vector<Bone> bones)
    : bones_(std::move(bones)) {
    rebuild_maps();
}

SkeletonData::SkeletonData(std::vector<Bone> bones, std::vector<int> root_bone_indices)
    : bones_(std::move(bones)), root_bone_indices_(std::move(root_bone_indices)) {
    rebuild_name_map();
}

void SkeletonData::add_bone(Bone bone) {
    bone_name_map_[bone.name] = bones_.size();
    if (bone.is_root()) {
        root_bone_indices_.push_back(static_cast<int>(bones_.size()));
    }
    bones_.push_back(std::move(bone));
}

void SkeletonData::rebuild_maps() {
    rebuild_name_map();
    rebuild_root_indices();
}

void SkeletonData::rebuild_name_map() {
    bone_name_map_.clear();
    for (size_t i = 0; i < bones_.size(); ++i) {
        bone_name_map_[bones_[i].name] = i;
    }
}

void SkeletonData::rebuild_root_indices() {
    root_bone_indices_.clear();
    for (size_t i = 0; i < bones_.size(); ++i) {
        if (bones_[i].is_root()) {
            root_bone_indices_.push_back(static_cast<int>(i));
        }
    }
}

} // namespace termin
