#include "guard_main.h"

#include <termin/skeleton/skeleton_data.hpp>

namespace {

termin::Bone bone(const char* name, int parent_index) {
    termin::Bone result;
    result.name = name;
    result.parent_index = parent_index;
    return result;
}

} // namespace

TEST_CASE("SkeletonData rebuilds deterministic name and root indexes") {
    termin::SkeletonData data({
        bone("root", -1),
        bone("spine", 0),
        bone("second_root", -1),
    });

    REQUIRE_EQ(data.get_bone_index("root"), 0);
    REQUIRE_EQ(data.get_bone_index("spine"), 1);
    REQUIRE_EQ(data.root_bone_indices().size(), 2u);
    CHECK_EQ(data.root_bone_indices()[0], 0);
    CHECK_EQ(data.root_bone_indices()[1], 2);

    data.bones_mut()[1].name = "root";
    data.bones_mut()[2].parent_index = 1;
    data.rebuild_maps();

    CHECK_EQ(data.get_bone_index("root"), 1);
    CHECK_EQ(data.get_bone_index("spine"), -1);
    REQUIRE_EQ(data.root_bone_indices().size(), 1u);
    CHECK_EQ(data.root_bone_indices()[0], 0);
}

TEST_CASE("SkeletonData add_bone keeps name and root indexes in sync") {
    termin::SkeletonData data;
    data.add_bone(bone("root", -1));
    data.add_bone(bone("child", 0));
    data.add_bone(bone("other_root", -1));

    CHECK_EQ(data.get_bone_index("child"), 1);
    REQUIRE_EQ(data.root_bone_indices().size(), 2u);
    CHECK_EQ(data.root_bone_indices()[0], 0);
    CHECK_EQ(data.root_bone_indices()[1], 2);
}

GUARD_TEST_MAIN();
