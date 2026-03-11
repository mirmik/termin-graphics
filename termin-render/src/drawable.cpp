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

Mat44f Drawable::get_model_matrix(const Entity& entity) const {
    double m[16];
    entity.transform().world_matrix(m);

    Mat44f result;
    for (int i = 0; i < 16; ++i) {
        result.data[i] = static_cast<float>(m[i]);
    }

    return result;
}

const tc_drawable_vtable Drawable::cxx_drawable_vtable = {
    &Drawable::_cb_has_phase,
    &Drawable::_cb_draw_geometry,
    &Drawable::_cb_get_geometry_draws,
    &Drawable::_cb_override_shader
};

} // namespace termin
