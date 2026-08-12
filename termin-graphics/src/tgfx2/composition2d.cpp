#include "tgfx2/composition2d.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

#include <tcbase/tc_log.h>

namespace {

    bool finite(tc_vec2f value) {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    bool valid_bounds(tc_bounds2f value) {
        return std::isfinite(value.x0) && std::isfinite(value.y0) && std::isfinite(value.x1) &&
               std::isfinite(value.y1) && value.x0 <= value.x1 && value.y0 <= value.y1;
    }

    bool valid_rule(tgfx::FillRule rule) {
        return rule == tgfx::FillRule::NonZero || rule == tgfx::FillRule::EvenOdd;
    }

    bool identity(const tc_affine2f& value) {
        return value.m00 == 1.0f && value.m01 == 0.0f && value.m10 == 0.0f && value.m11 == 1.0f &&
               value.tx == 0.0f && value.ty == 0.0f;
    }

} // namespace

extern "C" {

    tgfx2_composition_layer2d tgfx2_composition_layer2d_identity(void) {
        return {tc_affine2f_identity(), 1.0f, true};
    }

    tgfx2_composition_state2d tgfx2_composition_state2d_identity(void) {
        return {tc_affine2f_identity(), tc_affine2f_identity(), 1.0f, true, true};
    }

    bool tgfx2_composition_state2d_push(const tgfx2_composition_state2d* parent,
                                        const tgfx2_composition_layer2d* local,
                                        tgfx2_composition_state2d* out_state) {
        if (!parent || !local || !out_state || !tc_affine2f_is_finite(parent->local_to_world) ||
            !tc_affine2f_is_finite(local->transform) || !std::isfinite(parent->opacity) ||
            parent->opacity < 0.0f || parent->opacity > 1.0f || !std::isfinite(local->opacity) ||
            local->opacity < 0.0f || local->opacity > 1.0f) {
            tc_log_error("[Composition2D] finite transforms and opacity in [0, 1] required");
            return false;
        }

        tgfx2_composition_state2d next{};
        next.local_to_world = tc_affine2f_mul(parent->local_to_world, local->transform);
        next.opacity = parent->opacity * local->opacity;
        next.visible = parent->visible && local->visible;
        if (!tc_affine2f_is_finite(next.local_to_world) || !std::isfinite(next.opacity)) {
            tc_log_error("[Composition2D] accumulated state overflowed");
            return false;
        }
        next.invertible = tc_affine2f_try_inverse(next.local_to_world, 1e-8f, &next.world_to_local);
        if (!next.invertible) {
            next.world_to_local = tc_affine2f_identity();
        }
        *out_state = next;
        return true;
    }

    bool tgfx2_composition_state2d_map_point_to_world(const tgfx2_composition_state2d* state,
                                                      tc_vec2f local_point,
                                                      tc_vec2f* out_world_point) {
        if (!state || !out_world_point || !tc_affine2f_is_finite(state->local_to_world) || !finite(local_point)) {
            tc_log_error("[Composition2D] cannot map invalid local point or state");
            return false;
        }
        const tc_vec2f result = tc_affine2f_transform_point(state->local_to_world, local_point);
        if (!finite(result)) {
            tc_log_error("[Composition2D] local-to-world point mapping overflowed");
            return false;
        }
        *out_world_point = result;
        return true;
    }

    bool tgfx2_composition_state2d_map_point_from_world(const tgfx2_composition_state2d* state,
                                                        tc_vec2f world_point,
                                                        tc_vec2f* out_local_point) {
        if (!state || !out_local_point || !state->invertible || !tc_affine2f_is_finite(state->world_to_local) ||
            !finite(world_point)) {
            tc_log_error("[Composition2D] cannot map world point through singular or invalid state");
            return false;
        }
        const tc_vec2f result = tc_affine2f_transform_point(state->world_to_local, world_point);
        if (!finite(result)) {
            tc_log_error("[Composition2D] world-to-local point mapping overflowed");
            return false;
        }
        *out_local_point = result;
        return true;
    }

    bool tgfx2_composition_state2d_map_bounds_to_world(const tgfx2_composition_state2d* state,
                                                       tc_bounds2f local_bounds,
                                                       tc_bounds2f* out_world_bounds) {
        if (!state || !out_world_bounds || !tc_affine2f_is_finite(state->local_to_world) ||
            !valid_bounds(local_bounds)) {
            tc_log_error("[Composition2D] cannot map invalid local bounds or state");
            return false;
        }
        const tc_bounds2f result = tc_affine2f_transform_bounds(state->local_to_world, local_bounds);
        if (!valid_bounds(result)) {
            tc_log_error("[Composition2D] local-to-world bounds mapping overflowed");
            return false;
        }
        *out_world_bounds = result;
        return true;
    }

} // extern "C"

namespace tgfx {

    struct CompositionEvaluator2D::Frame {
        std::size_t clip_count = 0;
        bool transform_emitted = false;
        bool opacity_emitted = false;
        bool clip_emitted = false;
    };

    struct CompositionEvaluator2D::EvaluatedClip {
        FlattenedPath2f flattened;
        termin::Bounds2f bounds{};
        FillRule rule = FillRule::NonZero;
    };

    CompositionEvaluator2D::CompositionEvaluator2D() {
        reset_state_();
    }

    CompositionEvaluator2D::~CompositionEvaluator2D() {
        if (active_) {
            fail_batch_("evaluator destroyed before end_batch");
        }
    }

    void CompositionEvaluator2D::reset_state_() noexcept {
        builder_ = nullptr;
        states_.clear();
        root_state_ = tgfx2_composition_state2d_identity();
        frames_.clear();
        clips_.clear();
        active_ = false;
    }

    bool CompositionEvaluator2D::fail_batch_(const char* message) noexcept {
        tc_log_error("[Composition2D] %s; discarding batch", message);
        if (builder_) {
            builder_->clear();
        }
        reset_state_();
        failed_ = true;
        return false;
    }

    bool CompositionEvaluator2D::begin_batch(DrawList2DBuilder* builder) {
        bool clean = true;
        if (active_) {
            fail_batch_("begin_batch called before the previous batch ended");
            clean = false;
        }
        reset_state_();
        builder_ = builder;
        active_ = true;
        failed_ = false;
        return clean;
    }

    bool CompositionEvaluator2D::end_batch() {
        if (!active_) {
            tc_log_error("[Composition2D] end_batch called without an active batch");
            failed_ = false;
            return false;
        }
        if (!frames_.empty()) {
            return fail_batch_("end_batch found unclosed composition scopes");
        }
        reset_state_();
        failed_ = false;
        return true;
    }

    void CompositionEvaluator2D::abort_batch() noexcept {
        if (builder_) {
            builder_->clear();
        }
        reset_state_();
        failed_ = false;
    }

    bool CompositionEvaluator2D::push(const CompositionLayer2D& layer) {
        if (!active_) {
            tc_log_error("[Composition2D] push called without an active batch");
            failed_ = true;
            return false;
        }
        if (layer.clip && (layer.clip->path.empty() || !valid_rule(layer.clip->rule))) {
            return fail_batch_("empty or invalid geometric clip");
        }

        const tgfx2_composition_layer2d local{layer.transform, layer.opacity, layer.visible};
        tgfx2_composition_state2d next{};
        if (!tgfx2_composition_state2d_push(&state(), &local, &next)) {
            return fail_batch_("invalid local composition state");
        }

        Frame frame;
        frame.clip_count = clips_.size();
        try {
            if (builder_) {
                frame.transform_emitted = !identity(layer.transform);
                if (frame.transform_emitted && !builder_->push_transform(layer.transform)) {
                    return fail_batch_("DrawList2D rejected composition transform");
                }
                frame.opacity_emitted = layer.opacity != 1.0f;
                if (frame.opacity_emitted && !builder_->push_opacity(layer.opacity)) {
                    return fail_batch_("DrawList2D rejected composition opacity");
                }
                frame.clip_emitted = layer.clip.has_value();
                if (frame.clip_emitted && !builder_->push_clip(layer.clip->path, layer.clip->rule)) {
                    return fail_batch_("DrawList2D rejected composition clip");
                }
            }

            if (layer.clip) {
                EvaluatedClip evaluated;
                evaluated.flattened = layer.clip->path.flatten(0.25f, next.local_to_world);
                evaluated.bounds = layer.clip->path.transformed_bounds(next.local_to_world);
                evaluated.rule = layer.clip->rule;
                clips_.push_back(std::move(evaluated));
            }
            states_.push_back(next);
            frames_.push_back(frame);
            return true;
        } catch (const std::exception& error) {
            tc_log_error("[Composition2D] scope allocation failed: %s", error.what());
            return fail_batch_("scope allocation failure");
        }
    }

    bool CompositionEvaluator2D::pop() {
        if (!active_ || frames_.empty()) {
            if (active_) {
                return fail_batch_("pop has no matching composition scope");
            }
            tc_log_error("[Composition2D] pop called without an active batch");
            failed_ = true;
            return false;
        }

        const Frame frame = frames_.back();
        if (builder_) {
            if (frame.clip_emitted && !builder_->pop_clip()) {
                return fail_batch_("DrawList2D clip scope was disturbed");
            }
            if (frame.opacity_emitted && !builder_->pop_opacity()) {
                return fail_batch_("DrawList2D opacity scope was disturbed");
            }
            if (frame.transform_emitted && !builder_->pop_transform()) {
                return fail_batch_("DrawList2D transform scope was disturbed");
            }
        }
        clips_.resize(frame.clip_count);
        frames_.pop_back();
        states_.pop_back();
        return true;
    }

    bool CompositionEvaluator2D::active() const noexcept {
        return active_;
    }

    bool CompositionEvaluator2D::failed() const noexcept {
        return failed_;
    }

    std::size_t CompositionEvaluator2D::depth() const noexcept {
        return frames_.size();
    }

    const tgfx2_composition_state2d& CompositionEvaluator2D::state() const noexcept {
        return states_.empty() ? root_state_ : states_.back();
    }

    bool CompositionEvaluator2D::drawable() const noexcept {
        return state().visible && state().opacity > 0.0f;
    }

    bool CompositionEvaluator2D::map_point_to_world(termin::Vec2f local_point,
                                                    termin::Vec2f& out_world_point) const {
        return tgfx2_composition_state2d_map_point_to_world(&state(), local_point, &out_world_point);
    }

    bool CompositionEvaluator2D::map_point_from_world(termin::Vec2f world_point,
                                                      termin::Vec2f& out_local_point) const {
        return tgfx2_composition_state2d_map_point_from_world(&state(), world_point, &out_local_point);
    }

    bool CompositionEvaluator2D::map_bounds_to_world(termin::Bounds2f local_bounds,
                                                     termin::Bounds2f& out_world_bounds) const {
        return tgfx2_composition_state2d_map_bounds_to_world(&state(), local_bounds, &out_world_bounds);
    }

    bool CompositionEvaluator2D::clips_contain(termin::Vec2f world_point) const {
        if (!finite(world_point)) {
            tc_log_error("[Composition2D] cannot clip-test non-finite point");
            return false;
        }
        return std::all_of(clips_.begin(), clips_.end(), [&](const EvaluatedClip& clip) {
            return clip.flattened.contains(world_point, clip.rule);
        });
    }

    std::optional<termin::Bounds2f> CompositionEvaluator2D::conservative_clip_bounds() const noexcept {
        if (clips_.empty()) {
            return std::nullopt;
        }
        termin::Bounds2f result = clips_.front().bounds;
        for (std::size_t index = 1; index < clips_.size(); ++index) {
            const auto& bounds = clips_[index].bounds;
            result.x0 = std::max(result.x0, bounds.x0);
            result.y0 = std::max(result.y0, bounds.y0);
            result.x1 = std::min(result.x1, bounds.x1);
            result.y1 = std::min(result.y1, bounds.y1);
            if (result.x0 > result.x1) {
                result.x1 = result.x0;
            }
            if (result.y0 > result.y1) {
                result.y1 = result.y0;
            }
        }
        return result;
    }

} // namespace tgfx
