// Stack-scoped evaluation and DrawList2D lowering for shared 2D composition.
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "tgfx2/composition2d.h"
#include "tgfx2/draw_list2d.hpp"

namespace tgfx {

    struct CompositionClip2D {
        Path2f path;
        FillRule rule = FillRule::NonZero;
    };

    struct CompositionLayer2D {
        termin::Affine2f transform = termin::Affine2f::identity();
        float opacity = 1.0f;
        bool visible = true;
        std::optional<CompositionClip2D> clip;
    };

    // Evaluates semantic-owner traversal without owning that traversal. A batch
    // may optionally lower the same local transform/opacity/path-clip scopes
    // into one DrawList2DBuilder. Consumers retain responsibility for pruning
    // draw submission when drawable() is false.
    //
    // Calls are deliberately explicit rather than RAII-based so C bindings and
    // recursive C traversals can use the same order. A malformed batch is
    // logged and discarded via DrawList2DBuilder::clear(); the evaluator is
    // then reusable for the next begin_batch(). Direct builder state scopes
    // must not be interleaved with scopes owned by this evaluator.
    class TGFX2_TYPE_API CompositionEvaluator2D {
    public:
        CompositionEvaluator2D();
        ~CompositionEvaluator2D();

        CompositionEvaluator2D(const CompositionEvaluator2D&) = delete;
        CompositionEvaluator2D& operator=(const CompositionEvaluator2D&) = delete;

        // Returns false when an unfinished previous batch had to be discarded;
        // the requested new batch is nevertheless started from identity.
        bool begin_batch(DrawList2DBuilder* builder = nullptr);
        // Requires every successful push() to have a matching pop().
        bool end_batch();
        void abort_batch() noexcept;

        bool push(const CompositionLayer2D& layer);
        bool pop();

        bool active() const noexcept;
        bool failed() const noexcept;
        std::size_t depth() const noexcept;

        const tgfx2_composition_state2d& state() const noexcept;
        bool drawable() const noexcept;

        bool map_point_to_world(termin::Vec2f local_point, termin::Vec2f& out_world_point) const;
        bool map_point_from_world(termin::Vec2f world_point, termin::Vec2f& out_local_point) const;
        bool map_bounds_to_world(termin::Bounds2f local_bounds, termin::Bounds2f& out_world_bounds) const;

        // Tests every inherited geometric path in world space. This never
        // substitutes conservative bounds for the actual fill-rule test.
        bool clips_contain(termin::Vec2f world_point) const;
        // nullopt means no inherited clip. Disjoint clips yield a degenerate
        // (zero-area) intersection rather than nullopt.
        std::optional<termin::Bounds2f> conservative_clip_bounds() const noexcept;

    private:
        struct Frame;
        struct EvaluatedClip;

        void reset_state_() noexcept;
        bool fail_batch_(const char* message) noexcept;

        DrawList2DBuilder* builder_ = nullptr;
        tgfx2_composition_state2d root_state_ = tgfx2_composition_state2d_identity();
        std::vector<tgfx2_composition_state2d> states_;
        std::vector<Frame> frames_;
        std::vector<EvaluatedClip> clips_;
        bool active_ = false;
        bool failed_ = false;
    };

} // namespace tgfx
