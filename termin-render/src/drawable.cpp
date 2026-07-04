#include <termin/render/drawable.hpp>

namespace termin {

bool Drawable::_cb_has_phase(tc_component* c, const char* phase_mark) {
    if (!c) return false;

    Drawable* drawable = static_cast<Drawable*>(tc_component_get_drawable_userdata(c));
    if (!drawable) return false;

    return drawable->has_phase(phase_mark ? phase_mark : "");
}

void Drawable::_cb_draw_geometry(tc_component* c, void* render_context, int geometry_id) {
    if (!c) return;

    RenderContext* ctx = static_cast<RenderContext*>(render_context);
    if (!ctx) return;

    Drawable* drawable = static_cast<Drawable*>(tc_component_get_drawable_userdata(c));
    if (!drawable) return;

    drawable->draw_geometry(*ctx, geometry_id);
}

void* Drawable::_cb_get_geometry_draws(tc_component* c, const char* phase_mark) {
    if (!c) return nullptr;

    Drawable* drawable = static_cast<Drawable*>(tc_component_get_drawable_userdata(c));
    if (!drawable) return nullptr;

    std::string phase = phase_mark ? phase_mark : "";
    const std::string* phase_ptr = phase.empty() ? nullptr : &phase;
    drawable->_cached_geometry_draws = drawable->get_geometry_draws(phase_ptr);
    return &drawable->_cached_geometry_draws;
}

tc_shader_handle Drawable::_cb_override_shader(
    tc_component* c,
    const char* phase_mark,
    int geometry_id,
    tc_shader_handle original_shader
) {
    if (!c) return original_shader;

    Drawable* drawable = static_cast<Drawable*>(tc_component_get_drawable_userdata(c));
    if (!drawable) return original_shader;

    TcShader result = drawable->override_shader(
        phase_mark ? phase_mark : "",
        geometry_id,
        TcShader(original_shader)
    );

    return result.handle;
}

TcShader override_drawable_shader(
    tc_component* component,
    const ShaderOverrideContext& context)
{
    if (!component) {
        return context.original_shader;
    }

    if (tc_component_get_drawable_vtable(component) == &Drawable::cxx_drawable_vtable()) {
        Drawable* drawable = static_cast<Drawable*>(
            tc_component_get_drawable_userdata(component));
        if (drawable) {
            return drawable->override_shader_with_context(context);
        }
    }

    return TcShader(tc_component_override_shader(
        component,
        context.phase_mark.c_str(),
        context.geometry_id,
        context.original_shader.handle));
}

void collect_drawable_shader_usages_with_context(
    tc_component* component,
    const ShaderOverrideContext& context,
    const std::function<void(TcShader)>& emit)
{
    if (!emit) {
        return;
    }

    if (!component) {
        if (context.original_shader.is_valid()) {
            emit(context.original_shader);
        }
        return;
    }

    if (tc_component_get_drawable_vtable(component) == &Drawable::cxx_drawable_vtable()) {
        Drawable* drawable = static_cast<Drawable*>(
            tc_component_get_drawable_userdata(component));
        if (drawable) {
            drawable->collect_shader_usages_with_context(context, emit);
            return;
        }
    }

    if (context.original_shader.is_valid()) {
        emit(context.original_shader);
    }
    TcShader override_shader(tc_component_override_shader(
        component,
        context.phase_mark.c_str(),
        context.geometry_id,
        context.original_shader.handle));
    if (override_shader.is_valid() &&
        (override_shader.handle.index != context.original_shader.handle.index ||
         override_shader.handle.generation != context.original_shader.handle.generation)) {
        emit(override_shader);
    }
}

void Drawable::_cb_collect_shader_usages(
    tc_component* c,
    const char* phase_mark,
    int geometry_id,
    tc_shader_handle original_shader,
    tc_shader_usage_emit_fn emit,
    void* user_data
) {
    if (!c || !emit) return;

    Drawable* drawable = static_cast<Drawable*>(tc_component_get_drawable_userdata(c));
    if (!drawable) {
        emit(c, original_shader, user_data);
        return;
    }

    drawable->collect_shader_usages(
        phase_mark ? phase_mark : "",
        geometry_id,
        TcShader(original_shader),
        [c, emit, user_data](TcShader shader) {
            emit(c, shader.handle, user_data);
        }
    );
}

Mat44f Drawable::get_model_matrix(const Entity& entity) const {
    double m[16];
    entity.transform().world_matrix(m);

    Mat44f result;
    for (int i = 0; i < 16; ++i) {
        result.data[i] = static_cast<float>(m[i]);
    }

    return result;
}

const tc_drawable_vtable& Drawable::cxx_drawable_vtable() {
    static const tc_drawable_vtable vtable = {
        &Drawable::_cb_has_phase,
        &Drawable::_cb_draw_geometry,
        &Drawable::_cb_get_geometry_draws,
        &Drawable::_cb_override_shader,
        &Drawable::_cb_collect_shader_usages
    };
    return vtable;
}

} // namespace termin
