#pragma once

#include "termin/render/debug_geometry.hpp"
#include "termin/render/render_export.hpp"

extern "C" {
#include "core/tc_render_lifecycle.h"
}

namespace termin {

    class RenderAttachmentContext;

    class RENDER_API RenderPrepareContext {
    private:
        tc_scene_handle scene_ = TC_SCENE_HANDLE_INVALID;

    public:
        explicit RenderPrepareContext(tc_scene_handle scene)
            : scene_(scene) {}

        tc_scene_handle scene() const {
            return scene_;
        }
        DebugGeometryDrawer debug_geometry(tc_debug_geometry_type_id type_id) const;
        DebugGeometryDrawer debug_geometry(const char* stable_id) const;
    };

    class RENDER_API RenderLifecycle {
    private:
        tc_component* component_ = nullptr;

    public:
        virtual ~RenderLifecycle() = default;

        int render_prepare_priority() const {
            return tc_render_lifecycle_priority(component_);
        }
        bool set_render_prepare_priority(int priority) {
            return tc_render_lifecycle_set_priority(component_, priority);
        }

        virtual void on_render_attach(const RenderAttachmentContext& context) {
            (void)context;
        }
        virtual void prepare_render(const RenderPrepareContext& context) {
            (void)context;
        }
        virtual void on_render_detach(const RenderAttachmentContext& context) {
            (void)context;
        }

    protected:
        void install_render_lifecycle(tc_component* component);

    private:
        static void attach_callback(tc_component* component, const tc_render_attachment_context* context);
        static void prepare_callback(tc_component* component, const tc_render_prepare_context* context);
        static void detach_callback(tc_component* component, const tc_render_attachment_context* context);
        static const tc_render_lifecycle_vtable& vtable();
    };

} // namespace termin
