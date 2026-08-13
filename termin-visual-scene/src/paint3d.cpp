#include "termin_visual_scene/paint3d.hpp"

#include <exception>
#include <stdexcept>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {

        struct SinkAdapter3D {
            ScenePaintSink3D* sink = nullptr;
        };

        bool begin_sink(const tc_visual_view3d* view, void* user_data) {
            auto& adapter = *static_cast<SinkAdapter3D*>(user_data);
            try {
                return adapter.sink->begin(*view);
            } catch (const std::exception& error) {
                tc::Log::error("ScenePaintSink3D::begin failed: %s", error.what());
            } catch (...) {
                tc::Log::error("ScenePaintSink3D::begin failed with an unknown exception");
            }
            return false;
        }

        bool submit_sink(const tc_visual_draw_submission3d* submission, void* user_data) {
            auto& adapter = *static_cast<SinkAdapter3D*>(user_data);
            try {
                return adapter.sink->submit(*submission);
            } catch (const std::exception& error) {
                tc::Log::error("ScenePaintSink3D::submit failed: %s", error.what());
            } catch (...) {
                tc::Log::error("ScenePaintSink3D::submit failed with an unknown exception");
            }
            return false;
        }

        bool end_sink(void* user_data) {
            auto& adapter = *static_cast<SinkAdapter3D*>(user_data);
            try {
                return adapter.sink->end();
            } catch (const std::exception& error) {
                tc::Log::error("ScenePaintSink3D::end failed: %s", error.what());
            } catch (...) {
                tc::Log::error("ScenePaintSink3D::end failed with an unknown exception");
            }
            return false;
        }

        void abort_sink(void* user_data) {
            auto& adapter = *static_cast<SinkAdapter3D*>(user_data);
            try {
                adapter.sink->abort();
            } catch (const std::exception& error) {
                tc::Log::error("ScenePaintSink3D::abort failed: %s", error.what());
            } catch (...) {
                tc::Log::error("ScenePaintSink3D::abort failed with an unknown exception");
            }
        }

    } // namespace

    VisualItem3DHandle GraphicItemPaintContext3D::item() const noexcept {
        return tc_visual_item_paint_context3d_item(context_);
    }

    termin::Affine3d GraphicItemPaintContext3D::world_from_local() const noexcept {
        termin::Affine3d result = termin::Affine3d::identity();
        tc_visual_item_paint_context3d_get_world_from_local(context_, &result);
        return result;
    }

    bool GraphicItemPaintContext3D::effective_visible() const noexcept {
        return tc_visual_item_paint_context3d_effective_visible(context_);
    }

    bool GraphicItemPaintContext3D::effective_enabled() const noexcept {
        return tc_visual_item_paint_context3d_effective_enabled(context_);
    }

    const VisualView3D& GraphicItemPaintContext3D::view() const {
        const auto* result = tc_visual_item_paint_context3d_view(context_);
        if (result == nullptr)
            throw std::logic_error("stale GraphicItemPaintContext3D");
        return *result;
    }

    bool GraphicItemPaintContext3D::submit(const char* protocol, const void* payload, std::size_t payload_size) {
        return tc_visual_item_paint_context3d_submit(context_, protocol, payload, payload_size);
    }

    bool paint(const TcVisualScene3D& scene, const VisualView3D& view, ScenePaintSink3D& sink) {
        SinkAdapter3D adapter{&sink};
        tc_visual_draw_sink3d c_sink{
            .begin = begin_sink,
            .submit = submit_sink,
            .end = end_sink,
            .abort = abort_sink,
            .user_data = &adapter,
        };
        return tc_visual_scene3d_paint(scene.handle(), &view, &c_sink);
    }

} // namespace termin::visual
