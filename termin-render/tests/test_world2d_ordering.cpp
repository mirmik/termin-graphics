#include <cmath>
#include <limits>
#include <type_traits>
#include <vector>

#include <termin/render/world2d_ordering.hpp>

#include "guard_main.h"

namespace {

termin::World2DOrderEntry entry(
    size_t submission_index,
    int32_t layer,
    int32_t order,
    double depth,
    uint64_t stable_tie_breaker)
{
    return {
        {layer, order, depth, stable_tie_breaker},
        submission_index,
    };
}

} // namespace

TEST_CASE("world2d ordering contract remains C-like") {
    static_assert(std::is_standard_layout_v<termin::World2DOrderKey>);
    static_assert(std::is_trivially_copyable_v<termin::World2DOrderKey>);
    static_assert(std::is_standard_layout_v<termin::World2DOrderEntry>);
    static_assert(std::is_trivially_copyable_v<termin::World2DOrderEntry>);
    static_assert(std::is_standard_layout_v<termin::World2DOrderPolicy>);
}

TEST_CASE("world2d default order is layer then order then stable tie") {
    std::vector<termin::World2DOrderEntry> entries{
        entry(0, 1, 0, 100.0, 2),
        entry(1, 0, 5, -100.0, 8),
        entry(2, 1, 0, -100.0, 1),
        entry(3, 0, -2, 50.0, 9),
    };

    REQUIRE(termin::sort_world2d_order_entries(entries));

    CHECK(entries[0].submission_index == 3);
    CHECK(entries[1].submission_index == 1);
    CHECK(entries[2].submission_index == 2);
    CHECK(entries[3].submission_index == 0);
}

TEST_CASE("world2d equal authored keys use submission index as final fallback") {
    std::vector<termin::World2DOrderEntry> entries{
        entry(9, 0, 0, 0.0, 7),
        entry(2, 0, 0, 0.0, 7),
        entry(5, 0, 0, 0.0, 7),
    };

    REQUIRE(termin::sort_world2d_order_entries(entries));

    CHECK(entries[0].submission_index == 2);
    CHECK(entries[1].submission_index == 5);
    CHECK(entries[2].submission_index == 9);
}

TEST_CASE("world2d ordering never groups textures across compositing order") {
    // Three overlapping transparent quads: A and C share a texture, while B
    // must remain between them. A batcher may produce A | B | C, never AC | B.
    constexpr uint32_t texture_by_submission[] = {11, 22, 11};
    std::vector<termin::World2DOrderEntry> entries{
        entry(2, 0, 0, 0.0, 30),
        entry(0, 0, 0, 0.0, 10),
        entry(1, 0, 0, 0.0, 20),
    };

    REQUIRE(termin::sort_world2d_order_entries(entries));

    CHECK(texture_by_submission[entries[0].submission_index] == 11);
    CHECK(texture_by_submission[entries[1].submission_index] == 22);
    CHECK(texture_by_submission[entries[2].submission_index] == 11);
    CHECK(entries[0].submission_index == 0);
    CHECK(entries[1].submission_index == 1);
    CHECK(entries[2].submission_index == 2);
}

TEST_CASE("world2d depth is an optional local ordering policy") {
    std::vector<termin::World2DOrderEntry> entries{
        entry(0, 0, 0, 3.0, 1),
        entry(1, 0, 0, -2.0, 2),
        entry(2, 0, 0, 1.0, 3),
    };

    termin::World2DOrderPolicy back_to_front{};
    back_to_front.depth_mode = termin::World2DDepthSortMode::BackToFront;
    REQUIRE(termin::sort_world2d_order_entries(entries, back_to_front));
    CHECK(entries[0].submission_index == 0);
    CHECK(entries[1].submission_index == 2);
    CHECK(entries[2].submission_index == 1);

    termin::World2DOrderPolicy front_to_back{};
    front_to_back.depth_mode = termin::World2DDepthSortMode::FrontToBack;
    REQUIRE(termin::sort_world2d_order_entries(entries, front_to_back));
    CHECK(entries[0].submission_index == 1);
    CHECK(entries[1].submission_index == 2);
    CHECK(entries[2].submission_index == 0);
}

TEST_CASE("world2d enabled depth policy rejects non-finite input without mutation") {
    std::vector<termin::World2DOrderEntry> entries{
        entry(3, 0, 0, 1.0, 3),
        entry(1, 0, 0, std::numeric_limits<double>::quiet_NaN(), 1),
        entry(2, 0, 0, 2.0, 2),
    };

    termin::World2DOrderPolicy policy{};
    policy.depth_mode = termin::World2DDepthSortMode::BackToFront;
    CHECK_FALSE(termin::sort_world2d_order_entries(entries, policy));

    CHECK(entries[0].submission_index == 3);
    CHECK(entries[1].submission_index == 1);
    CHECK(entries[2].submission_index == 2);
}

GUARD_TEST_MAIN();
