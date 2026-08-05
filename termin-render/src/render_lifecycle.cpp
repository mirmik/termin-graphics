#include "termin/render/render_lifecycle.hpp"

#include "core/tc_component.h"

namespace termin {

namespace {

RenderLifecycle* lifecycle_for(tc_component* component) {
    const tc_render_lifecycle_capability* capability =
        tc_render_lifecycle_capability_get(component);
    return capability ? static_cast<RenderLifecycle*>(capability->userdata) : nullptr;
}

} // namespace

void RenderLifecycle::install_render_lifecycle(tc_component* component) {
    component_ = component;
    tc_render_lifecycle_capability_attach(component, &vtable(), this);
}

void RenderLifecycle::attach_callback(
    tc_component* component,
    const tc_render_attachment_context* context
) {
    RenderLifecycle* lifecycle = lifecycle_for(component);
    if (lifecycle && context) {
        lifecycle->on_render_attach(
            *reinterpret_cast<const RenderAttachmentContext*>(context));
    }
}

void RenderLifecycle::prepare_callback(
    tc_component* component,
    const tc_render_prepare_context* context
) {
    RenderLifecycle* lifecycle = lifecycle_for(component);
    if (lifecycle && context) {
        lifecycle->prepare_render(
            *reinterpret_cast<const RenderPrepareContext*>(context));
    }
}

void RenderLifecycle::detach_callback(
    tc_component* component,
    const tc_render_attachment_context* context
) {
    RenderLifecycle* lifecycle = lifecycle_for(component);
    if (lifecycle && context) {
        lifecycle->on_render_detach(
            *reinterpret_cast<const RenderAttachmentContext*>(context));
    }
}

const tc_render_lifecycle_vtable& RenderLifecycle::vtable() {
    static const tc_render_lifecycle_vtable lifecycle_vtable = {
        &RenderLifecycle::attach_callback,
        &RenderLifecycle::prepare_callback,
        &RenderLifecycle::detach_callback,
    };
    return lifecycle_vtable;
}

} // namespace termin
