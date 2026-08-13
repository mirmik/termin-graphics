#pragma once

#include <string>
#include <termin/geom/general_pose3.hpp>
#include <termin/geom/mat44.hpp>
#include <termin/skeleton/termin_skeleton_api.hpp>
#include <vector>

extern "C" {
#include "resources/tc_skeleton.h"
}

namespace termin {

    // Scene-neutral mutable skeleton pose and skinning-matrix runtime.
    //
    // The instance owns only local bone poses and derived matrices. Scene/ECS
    // adapters may feed externally evaluated world matrices through
    // update_from_world_matrices(), but no Entity contract leaks into this
    // Graphics-owned type.
    class TERMIN_SKELETON_API SkeletonInstance {
    public:
        static constexpr int MAX_BONES = 128;

        // Non-owning resource pointer. Lifetime is managed by TcSkeleton or the
        // resource registry used by the caller.
        tc_skeleton* _skeleton = nullptr;

    private:
        std::vector<GeneralPose3> _local_poses;
        std::vector<Mat44> _bone_world_matrices;
        std::vector<Mat44> _bone_matrices;

    public:
        SkeletonInstance() = default;
        explicit SkeletonInstance(tc_skeleton* skeleton);

        tc_skeleton* skeleton() const {
            return _skeleton;
        }
        void set_skeleton(tc_skeleton* skeleton);

        void reset_to_bind_pose();

        const GeneralPose3& local_pose(int bone_index) const;

        void set_bone_transform(int bone_index,
                                const double* translation,
                                const double* rotation,
                                const double* scale);

        void set_bone_transform_by_name(const std::string& bone_name,
                                        const double* translation,
                                        const double* rotation,
                                        const double* scale);

        // Evaluate world and skinning matrices from the owned local pose.
        void update();

        // Evaluate skinning matrices from world matrices supplied by a host
        // adapter. The matrices must follow skeleton bone order.
        bool update_from_world_matrices(const Mat44& skinning_root_world,
                                        const std::vector<Mat44>& bone_world_matrices);

        void get_bone_matrices_float(float* out) const;
        int bone_count() const;
        const Mat44& get_bone_world_matrix(int bone_index) const;
        const Mat44& get_bone_matrix(int bone_index) const;

    private:
        void resize_for_skeleton();
        bool evaluate_world_matrix(int bone_index, std::vector<unsigned char>& state);
    };

} // namespace termin
