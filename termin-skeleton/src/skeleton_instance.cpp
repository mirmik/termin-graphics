#include "resources/tc_skeleton_registry.h"
#include <tcbase/tc_log.hpp>
#include <termin/skeleton/skeleton_instance.hpp>

namespace termin {
    namespace {
        Mat44 col_major_to_mat44(const double* source) {
            Mat44 result;
            for (int i = 0; i < 16; ++i) {
                result.data[i] = source[i];
            }
            return result;
        }

        const GeneralPose3& identity_pose() {
            static const GeneralPose3 value = GeneralPose3::identity();
            return value;
        }

        const Mat44& identity_matrix() {
            static const Mat44 value = Mat44::identity();
            return value;
        }
    } // namespace

    SkeletonInstance::SkeletonInstance(tc_skeleton* skeleton) {
        set_skeleton(skeleton);
    }

    void SkeletonInstance::set_skeleton(tc_skeleton* skeleton) {
        _skeleton = skeleton;
        resize_for_skeleton();
        reset_to_bind_pose();
    }

    void SkeletonInstance::resize_for_skeleton() {
        const size_t count = _skeleton ? _skeleton->bone_count : 0;
        _local_poses.resize(count, GeneralPose3::identity());
        _bone_world_matrices.resize(count, Mat44::identity());
        _bone_matrices.resize(count, Mat44::identity());
    }

    void SkeletonInstance::reset_to_bind_pose() {
        if (!_skeleton || !_skeleton->bones) {
            _local_poses.clear();
            _bone_world_matrices.clear();
            _bone_matrices.clear();
            return;
        }

        resize_for_skeleton();
        for (size_t i = 0; i < _skeleton->bone_count; ++i) {
            const tc_bone& bone = _skeleton->bones[i];
            _local_poses[i] = GeneralPose3(
                Quat{bone.bind_rotation[0], bone.bind_rotation[1], bone.bind_rotation[2], bone.bind_rotation[3]},
                Vec3{bone.bind_translation[0], bone.bind_translation[1], bone.bind_translation[2]},
                Vec3{bone.bind_scale[0], bone.bind_scale[1], bone.bind_scale[2]});
        }
        update();
    }

    const GeneralPose3& SkeletonInstance::local_pose(int bone_index) const {
        if (bone_index >= 0 && bone_index < static_cast<int>(_local_poses.size())) {
            return _local_poses[static_cast<size_t>(bone_index)];
        }
        tc::Log::warn("[SkeletonInstance::local_pose] invalid bone_index=%d", bone_index);
        return identity_pose();
    }

    void SkeletonInstance::set_bone_transform(int bone_index,
                                              const double* translation,
                                              const double* rotation,
                                              const double* scale) {
        if (bone_index < 0 || bone_index >= static_cast<int>(_local_poses.size())) {
            tc::Log::warn("[SkeletonInstance::set_bone_transform] invalid bone_index=%d", bone_index);
            return;
        }

        GeneralPose3& pose = _local_poses[static_cast<size_t>(bone_index)];
        if (translation) {
            pose.lin = Vec3{translation[0], translation[1], translation[2]};
        }
        if (rotation) {
            pose.ang = Quat{rotation[0], rotation[1], rotation[2], rotation[3]};
        }
        if (scale) {
            pose.scale = Vec3{scale[0], scale[1], scale[2]};
        }
    }

    void SkeletonInstance::set_bone_transform_by_name(const std::string& bone_name,
                                                      const double* translation,
                                                      const double* rotation,
                                                      const double* scale) {
        if (!_skeleton) {
            tc::Log::warn("[SkeletonInstance::set_bone_transform_by_name] skeleton is null");
            return;
        }
        const int index = tc_skeleton_find_bone(_skeleton, bone_name.c_str());
        if (index < 0) {
            tc::Log::warn("[SkeletonInstance::set_bone_transform_by_name] unknown bone '%s'", bone_name.c_str());
            return;
        }
        set_bone_transform(index, translation, rotation, scale);
    }

    bool SkeletonInstance::evaluate_world_matrix(int bone_index, std::vector<unsigned char>& state) {
        if (bone_index < 0 || bone_index >= bone_count()) {
            return false;
        }

        const size_t index = static_cast<size_t>(bone_index);
        if (state[index] == 2) {
            return true;
        }
        if (state[index] == 1) {
            tc::Log::error("[SkeletonInstance::update] skeleton contains a parent cycle at bone %d", bone_index);
            return false;
        }
        state[index] = 1;

        double local_data[16];
        _local_poses[index].matrix4(local_data);
        const Mat44 local = col_major_to_mat44(local_data);
        const int parent_index = _skeleton->bones[index].parent_index;
        if (parent_index >= 0) {
            if (!evaluate_world_matrix(parent_index, state)) {
                return false;
            }
            _bone_world_matrices[index] = _bone_world_matrices[static_cast<size_t>(parent_index)] * local;
        } else {
            _bone_world_matrices[index] = local;
        }

        state[index] = 2;
        return true;
    }

    void SkeletonInstance::update() {
        if (!_skeleton || !_skeleton->bones) {
            return;
        }
        if (_local_poses.size() != _skeleton->bone_count) {
            tc::Log::error("[SkeletonInstance::update] pose size=%zu does not match bone_count=%zu",
                           _local_poses.size(),
                           _skeleton->bone_count);
            return;
        }

        std::vector<unsigned char> state(_skeleton->bone_count, 0);
        for (size_t i = 0; i < _skeleton->bone_count; ++i) {
            if (!evaluate_world_matrix(static_cast<int>(i), state)) {
                return;
            }
        }
        for (size_t i = 0; i < _skeleton->bone_count; ++i) {
            _bone_matrices[i] = _bone_world_matrices[i] * col_major_to_mat44(_skeleton->bones[i].inverse_bind_matrix);
        }
    }

    bool SkeletonInstance::update_from_world_matrices(const Mat44& skinning_root_world,
                                                      const std::vector<Mat44>& bone_world_matrices) {
        if (!_skeleton || !_skeleton->bones) {
            tc::Log::error("[SkeletonInstance::update_from_world_matrices] skeleton is null");
            return false;
        }
        if (bone_world_matrices.size() != _skeleton->bone_count) {
            tc::Log::error(
                "[SkeletonInstance::update_from_world_matrices] matrix count=%zu does not match bone_count=%zu",
                bone_world_matrices.size(),
                _skeleton->bone_count);
            return false;
        }

        _bone_world_matrices = bone_world_matrices;
        const Mat44 root_inverse = skinning_root_world.inverse();
        for (size_t i = 0; i < _skeleton->bone_count; ++i) {
            _bone_matrices[i] = root_inverse * _bone_world_matrices[i] *
                                col_major_to_mat44(_skeleton->bones[i].inverse_bind_matrix);
        }
        return true;
    }

    void SkeletonInstance::get_bone_matrices_float(float* out) const {
        if (!out) {
            tc::Log::error("[SkeletonInstance::get_bone_matrices_float] output is null");
            return;
        }
        for (size_t i = 0; i < _bone_matrices.size(); ++i) {
            for (int j = 0; j < 16; ++j) {
                out[i * 16 + static_cast<size_t>(j)] = static_cast<float>(_bone_matrices[i].data[j]);
            }
        }
    }

    int SkeletonInstance::bone_count() const {
        return _skeleton ? static_cast<int>(_skeleton->bone_count) : 0;
    }

    const Mat44& SkeletonInstance::get_bone_world_matrix(int bone_index) const {
        if (bone_index >= 0 && bone_index < static_cast<int>(_bone_world_matrices.size())) {
            return _bone_world_matrices[static_cast<size_t>(bone_index)];
        }
        tc::Log::warn("[SkeletonInstance::get_bone_world_matrix] invalid bone_index=%d", bone_index);
        return identity_matrix();
    }

    const Mat44& SkeletonInstance::get_bone_matrix(int bone_index) const {
        if (bone_index >= 0 && bone_index < static_cast<int>(_bone_matrices.size())) {
            return _bone_matrices[static_cast<size_t>(bone_index)];
        }
        tc::Log::warn("[SkeletonInstance::get_bone_matrix] invalid bone_index=%d", bone_index);
        return identity_matrix();
    }

} // namespace termin
