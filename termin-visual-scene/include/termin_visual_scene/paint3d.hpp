#pragma once

#include <cstddef>

#include "termin_visual_scene/scene3d.hpp"

namespace termin::visual {

    using VisualView3D = tc_visual_view3d;
    using DrawPacket3D = tc_visual_draw_packet3d;
    using DrawSubmission3D = tc_visual_draw_submission3d;

    // Borrowed facade valid only for the duration of one item paint callback.
    class TERMIN_VISUAL_SCENE_API GraphicItemPaintContext3D {
    public:
        explicit GraphicItemPaintContext3D(tc_visual_item_paint_context3d* context) noexcept
            : context_(context) {}

        VisualItem3DHandle item() const noexcept;
        termin::Affine3d world_from_local() const noexcept;
        bool effective_visible() const noexcept;
        bool effective_enabled() const noexcept;
        const VisualView3D& view() const;
        bool submit(const char* protocol, const void* payload = nullptr, std::size_t payload_size = 0);

    private:
        tc_visual_item_paint_context3d* context_ = nullptr;
    };

    // Adapter seam for renderer-specific collectors. Implementations stage all
    // submissions until end succeeds and discard the staged batch on abort.
    class TERMIN_VISUAL_SCENE_API ScenePaintSink3D {
    public:
        virtual ~ScenePaintSink3D() = default;
        virtual bool begin(const VisualView3D& view) = 0;
        virtual bool submit(const DrawSubmission3D& submission) = 0;
        virtual bool end() = 0;
        virtual void abort() = 0;
    };

    TERMIN_VISUAL_SCENE_API bool paint(const TcVisualScene3D& scene, const VisualView3D& view, ScenePaintSink3D& sink);

} // namespace termin::visual
