#include <termin/skeleton/skeleton_instance.hpp>
#include <termin/entity/entity.hpp>
#include <termin/geom/general_transform3.hpp>
#include <termin/geom/general_pose3.hpp>
#include <tcbase/tc_log.hpp>
#include <cstring>

namespace termin {

SkeletonInstance::SkeletonInstance(
    tc_skeleton* skeleton,
    std::vector<Entity> bone_entities,
    Entity skeleton_root
)
    : _skeleton(skeleton)
    , _bone_entities(std::move(bone_entities))
    , _skeleton_root(skeleton_root)
{
    if (_skeleton) {
        size_t n = _skeleton->bone_count;
        _bone_matrices.resize(n, Mat44::identity());
    }
}

void SkeletonInstance::set_skeleton(tc_skeleton* skeleton) {
    _skeleton = skeleton;
    if (_skeleton) {
        size_t n = _skeleton->bone_count;
        _bone_matrices.resize(n, Mat44::identity());
    } else {
        _bone_matrices.clear();
    }
}

void SkeletonInstance::set_bone_entities(std::vector<Entity> entities) {
    _bone_entities = std::move(entities);
}

Entity SkeletonInstance::get_bone_entity(int bone_index) const {
    if (bone_index >= 0 && bone_index < static_cast<int>(_bone_entities.size())) {
        return _bone_entities[bone_index];
    }
    return Entity();  // Invalid entity
}

Entity SkeletonInstance::get_bone_entity_by_name(const std::string& bone_name) const {
    if (!_skeleton) {
        tc::Log::warn("[SkeletonInstance::get_bone_entity_by_name] skeleton is null");
        return Entity();
    }
    int idx = tc_skeleton_find_bone(_skeleton, bone_name.c_str());
    return get_bone_entity(idx);
}

void SkeletonInstance::set_bone_transform(
    int bone_index,
    const double* translation,
    const double* rotation,
    const double* scale
) {
    Entity ent = get_bone_entity(bone_index);
    if (!ent.valid()) {
        return;
    }

    const GeneralPose3& pose = ent.transform().local_pose();

    Vec3 new_lin = translation ? Vec3{translation[0], translation[1], translation[2]} : pose.lin;
    Quat new_ang = rotation ? Quat{rotation[0], rotation[1], rotation[2], rotation[3]} : pose.ang;
    Vec3 new_scale = scale ? Vec3{scale[0], scale[1], scale[2]} : pose.scale;

    GeneralPose3 new_pose(new_ang, new_lin, new_scale);
    ent.transform().relocate(new_pose);
}

void SkeletonInstance::set_bone_transform_by_name(
    const std::string& bone_name,
    const double* translation,
    const double* rotation,
    const double* scale
) {
    if (!_skeleton) {
        return;
    }
    int idx = tc_skeleton_find_bone(_skeleton, bone_name.c_str());
    if (idx < 0) {
        return;
    }
    set_bone_transform(idx, translation, rotation, scale);
}

Entity SkeletonInstance::find_skeleton_root() {
    if (_skeleton_root.valid()) {
        return _skeleton_root;
    }

    // Try to find root as parent of root bone entities
    if (!_bone_entities.empty() && _skeleton && _skeleton->root_count > 0) {
        int root_bone_idx = _skeleton->root_indices[0];
        if (root_bone_idx >= 0 && root_bone_idx < static_cast<int>(_bone_entities.size())) {
            Entity root_bone_entity = _bone_entities[root_bone_idx];
            if (root_bone_entity.valid()) {
                GeneralTransform3 parent_transform = root_bone_entity.transform().parent();
                if (parent_transform.valid()) {
                    Entity parent_entity = parent_transform.entity();
                    if (parent_entity.valid()) {
                        _skeleton_root = parent_entity;
                        return _skeleton_root;
                    }
                }
            }
        }
    }

    tc::Log::warn("[SkeletonInstance::find_skeleton_root] could not find skeleton root");
    return Entity();
}

void SkeletonInstance::update() {
    if (_bone_entities.empty() || !_skeleton || !_skeleton->bones) return;

    // Helper to convert row-major array to column-major Mat44
    // (inverse_bind_matrix is stored as row-major in tc_bone)
    auto row_major_to_mat44 = [](const double* src) -> Mat44 {
        Mat44 m;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                m(col, row) = src[row * 4 + col];
            }
        }
        return m;
    };

    // Helper to copy column-major array to Mat44
    auto col_major_to_mat44 = [](const double* src) -> Mat44 {
        Mat44 m;
        for (int i = 0; i < 16; ++i) {
            m.data[i] = src[i];
        }
        return m;
    };

    // Get inverse of skeleton root world matrix
    Mat44 skeleton_world_inv = Mat44::identity();
    Entity root = find_skeleton_root();

    if (root.valid()) {
        double m[16];
        root.transform().world_matrix(m);  // column-major
        Mat44 skeleton_world = col_major_to_mat44(m);
        skeleton_world_inv = skeleton_world.inverse();
    }

    // Compute bone matrices
    for (size_t i = 0; i < _skeleton->bone_count; ++i) {
        const tc_bone& bone = _skeleton->bones[i];
        if (bone.index >= static_cast<int>(_bone_entities.size())) continue;

        Entity ent = _bone_entities[bone.index];
        if (!ent.valid()) continue;

        // Get bone world matrix (column-major from world_matrix)
        double bone_world_d[16];
        ent.transform().world_matrix(bone_world_d);
        Mat44 bone_world = col_major_to_mat44(bone_world_d);

        // Get inverse bind matrix (stored as row-major in tc_bone)
        Mat44 inv_bind = row_major_to_mat44(bone.inverse_bind_matrix);

        // bone_matrix = skeleton_world_inv * bone_world * inv_bind
        _bone_matrices[bone.index] = skeleton_world_inv * bone_world * inv_bind;
    }
}

void SkeletonInstance::get_bone_matrices_float(float* out) const {
    for (size_t i = 0; i < _bone_matrices.size(); ++i) {
        // Mat44 stores double, convert to float
        for (int j = 0; j < 16; ++j) {
            out[i * 16 + j] = static_cast<float>(_bone_matrices[i].data[j]);
        }
    }
}

int SkeletonInstance::bone_count() const {
    return _skeleton ? static_cast<int>(_skeleton->bone_count) : 0;
}

Mat44 SkeletonInstance::get_bone_world_matrix(int bone_index) const {
    if (bone_index >= 0 && bone_index < static_cast<int>(_bone_entities.size())) {
        Entity ent = _bone_entities[bone_index];
        if (ent.valid()) {
            double m[16];
            ent.transform().world_matrix(m);
            Mat44 result;
            for (int i = 0; i < 16; ++i) {
                result.data[i] = m[i];
            }
            return result;
        }
    }
    tc::Log::warn("[SkeletonInstance::get_bone_world_matrix] invalid bone_index=%d", bone_index);
    return Mat44::identity();
}

const Mat44& SkeletonInstance::get_bone_matrix(int bone_index) const {
    static Mat44 identity = Mat44::identity();
    if (bone_index >= 0 && bone_index < static_cast<int>(_bone_matrices.size())) {
        return _bone_matrices[bone_index];
    }
    tc::Log::warn("[SkeletonInstance::get_bone_matrix] invalid bone_index=%d", bone_index);
    return identity;
}

} // namespace termin
