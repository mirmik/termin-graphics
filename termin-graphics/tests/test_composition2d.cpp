#ifdef NDEBUG
#undef NDEBUG
#endif

#include "tgfx2/composition2d.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <type_traits>

namespace {

    bool near(float left, float right, float epsilon = 1e-5f) {
        return std::abs(left - right) <= epsilon;
    }

    tgfx::Path2f rectangle(float x, float y, float width, float height) {
        tgfx::Path2f path;
        assert(path.move_to({x, y}));
        assert(path.line_to({x + width, y}));
        assert(path.line_to({x + width, y + height}));
        assert(path.line_to({x, y + height}));
        assert(path.close());
        return path;
    }

    void test_c_value_state() {
        static_assert(std::is_standard_layout_v<tgfx2_composition_layer2d>);
        static_assert(std::is_standard_layout_v<tgfx2_composition_state2d>);

        const auto root = tgfx2_composition_state2d_identity();
        const tgfx2_composition_layer2d parent{
            termin::Affine2f::translation(10.0f, 20.0f) * termin::Affine2f::rotation(0.5f), 0.5f, true};
        tgfx2_composition_state2d parent_state{};
        assert(tgfx2_composition_state2d_push(&root, &parent, &parent_state));

        const tgfx2_composition_layer2d child{termin::Affine2f::scaling(-2.0f, 3.0f), 0.25f, false};
        tgfx2_composition_state2d child_state{};
        assert(tgfx2_composition_state2d_push(&parent_state, &child, &child_state));
        assert(near(child_state.opacity, 0.125f));
        assert(!child_state.visible);
        assert(child_state.invertible);

        tc_vec2f world{};
        tc_vec2f local{};
        assert(tgfx2_composition_state2d_map_point_to_world(&child_state, {3.0f, 4.0f}, &world));
        assert(tgfx2_composition_state2d_map_point_from_world(&child_state, world, &local));
        assert(near(local.x, 3.0f));
        assert(near(local.y, 4.0f));

        tc_bounds2f world_bounds{};
        assert(tgfx2_composition_state2d_map_bounds_to_world(&child_state, {0.0f, 0.0f, 2.0f, 4.0f}, &world_bounds));
        assert(world_bounds.x0 <= world_bounds.x1 && world_bounds.y0 <= world_bounds.y1);

        const auto before = child_state;
        const tgfx2_composition_layer2d invalid{
            termin::Affine2f::translation(std::numeric_limits<float>::infinity(), 0.0f), 1.0f, true};
        assert(!tgfx2_composition_state2d_push(&parent_state, &invalid, &child_state));
        assert(child_state.local_to_world.tx == before.local_to_world.tx);
    }

    void test_singular_and_non_finite_mapping() {
        const auto root = tgfx2_composition_state2d_identity();
        const tgfx2_composition_layer2d singular{termin::Affine2f::scaling(0.0f, 2.0f), 1.0f, true};
        tgfx2_composition_state2d state{};
        assert(tgfx2_composition_state2d_push(&root, &singular, &state));
        assert(!state.invertible);
        tc_vec2f result{7.0f, 9.0f};
        assert(!tgfx2_composition_state2d_map_point_from_world(&state, {1.0f, 2.0f}, &result));
        assert(result.x == 7.0f && result.y == 9.0f);
        assert(!tgfx2_composition_state2d_map_point_to_world(
            &state, {std::numeric_limits<float>::quiet_NaN(), 0.0f}, &result));
    }

    void test_scoped_evaluation_and_lowering() {
        tgfx::DrawList2DBuilder builder;
        tgfx::CompositionEvaluator2D evaluator;
        assert(evaluator.begin_batch(&builder));

        tgfx::CompositionLayer2D parent;
        parent.transform = termin::Affine2f::translation(10.0f, 20.0f) * termin::Affine2f::rotation(0.25f);
        parent.opacity = 0.5f;
        parent.clip = tgfx::CompositionClip2D{rectangle(0.0f, 0.0f, 100.0f, 80.0f), tgfx::FillRule::NonZero};
        assert(evaluator.push(parent));
        assert(evaluator.depth() == 1);
        assert(evaluator.drawable());

        tgfx::CompositionLayer2D child;
        child.transform = termin::Affine2f::shear(0.2f, -0.1f) * termin::Affine2f::scaling(2.0f, 3.0f);
        child.opacity = 0.25f;
        child.clip = tgfx::CompositionClip2D{rectangle(5.0f, 5.0f, 20.0f, 10.0f), tgfx::FillRule::EvenOdd};
        assert(evaluator.push(child));
        assert(near(evaluator.state().opacity, 0.125f));

        termin::Vec2f world{};
        termin::Vec2f local{};
        assert(evaluator.map_point_to_world({8.0f, 7.0f}, world));
        assert(evaluator.map_point_from_world(world, local));
        assert(near(local.x, 8.0f));
        assert(near(local.y, 7.0f));
        assert(evaluator.clips_contain(world));

        const auto clip_bounds = evaluator.conservative_clip_bounds();
        assert(clip_bounds);
        assert(clip_bounds->x0 <= clip_bounds->x1 && clip_bounds->y0 <= clip_bounds->y1);

        tgfx::FillPaint fill{{0.2f, 0.4f, 0.6f, 1.0f}};
        assert(builder.rect({5.0f, 5.0f, 10.0f, 8.0f}, fill));
        assert(evaluator.pop());
        assert(evaluator.pop());
        assert(evaluator.end_batch());

        auto list = builder.freeze();
        assert(list);
        assert(list->size() == 13);
        assert(std::holds_alternative<tgfx::PushTransform2D>(list->commands()[0]));
        assert(std::holds_alternative<tgfx::PushOpacity2D>(list->commands()[1]));
        assert(std::holds_alternative<tgfx::PushClip2D>(list->commands()[2]));
        assert(std::get<tgfx::PushClip2D>(list->commands()[5]).rule == tgfx::FillRule::EvenOdd);
        assert(std::holds_alternative<tgfx::PopTransform2D>(list->commands().back()));
    }

    void test_exact_clip_and_recovery() {
        tgfx::CompositionEvaluator2D evaluator;
        assert(evaluator.begin_batch());
        tgfx::CompositionLayer2D clipped;
        tgfx::Path2f triangle;
        assert(triangle.move_to({0.0f, 0.0f}));
        assert(triangle.line_to({10.0f, 0.0f}));
        assert(triangle.line_to({0.0f, 10.0f}));
        assert(triangle.close());
        clipped.clip = tgfx::CompositionClip2D{std::move(triangle), tgfx::FillRule::NonZero};
        assert(evaluator.push(clipped));
        assert(evaluator.clips_contain({1.0f, 1.0f}));
        // Inside the triangle's AABB but outside its actual geometric fill.
        assert(!evaluator.clips_contain({9.0f, 9.0f}));
        assert(evaluator.pop());
        assert(evaluator.end_batch());

        tgfx::DrawList2DBuilder builder;
        assert(evaluator.begin_batch(&builder));
        assert(evaluator.push({}));
        assert(builder.rect({0.0f, 0.0f, 2.0f, 2.0f}, {{1.0f, 1.0f, 1.0f, 1.0f}}));
        assert(!evaluator.end_batch());
        auto recovered = builder.freeze();
        assert(recovered && recovered->empty());

        assert(evaluator.begin_batch(&builder));
        assert(!evaluator.pop());
        assert(evaluator.failed());
        assert(!evaluator.end_batch());
        assert(evaluator.begin_batch(&builder));
        assert(evaluator.end_batch());
    }

} // namespace

int main() {
    test_c_value_state();
    test_singular_and_non_finite_mapping();
    test_scoped_evaluation_and_lowering();
    test_exact_clip_and_recovery();
}
