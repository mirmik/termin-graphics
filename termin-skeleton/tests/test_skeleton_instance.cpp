#include "guard_main.h"

#include <cmath>
#include <termin/skeleton/skeleton_instance.hpp>

namespace {
    struct SkeletonFixture {
        tc_bone bones[2];
        tc_skeleton skeleton{};

        SkeletonFixture() {
            tc_bone_init(&bones[0]);
            tc_bone_init(&bones[1]);
            bones[0].index = 0;
            bones[0].parent_index = -1;
            bones[0].bind_translation[0] = 1.0;
            bones[1].index = 1;
            bones[1].parent_index = 0;
            bones[1].bind_translation[0] = 2.0;
            skeleton.bones = bones;
            skeleton.bone_count = 2;
        }
    };

    void check_near(double actual, double expected) {
        CHECK(std::abs(actual - expected) < 1.0e-9);
    }
} // namespace

TEST_CASE("SkeletonInstance evaluates a portable local pose hierarchy") {
    SkeletonFixture fixture;
    termin::SkeletonInstance instance(&fixture.skeleton);

    REQUIRE_EQ(instance.bone_count(), 2);
    check_near(instance.get_bone_world_matrix(0)(3, 0), 1.0);
    check_near(instance.get_bone_world_matrix(1)(3, 0), 3.0);
    check_near(instance.get_bone_matrix(1)(3, 0), 3.0);

    const double child_translation[3] = {4.0, 0.0, 0.0};
    instance.set_bone_transform(1, child_translation, nullptr, nullptr);
    instance.update();
    check_near(instance.get_bone_world_matrix(1)(3, 0), 5.0);
}

TEST_CASE("SkeletonInstance accepts scene-neutral externally evaluated matrices") {
    SkeletonFixture fixture;
    termin::SkeletonInstance instance(&fixture.skeleton);
    std::vector<termin::Mat44> bone_world = {
        termin::Mat44::translation(11.0, 0.0, 0.0),
        termin::Mat44::translation(13.0, 0.0, 0.0),
    };

    REQUIRE(instance.update_from_world_matrices(
        termin::Mat44::translation(10.0, 0.0, 0.0), bone_world));
    check_near(instance.get_bone_matrix(0)(3, 0), 1.0);
    check_near(instance.get_bone_matrix(1)(3, 0), 3.0);
}

GUARD_TEST_MAIN();
