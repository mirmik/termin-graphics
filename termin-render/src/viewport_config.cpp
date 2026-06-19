// viewport_config.cpp - ViewportConfig implementation
#include <termin/viewport_config.hpp>
#include <tcbase/tgfx_intern_string.h>

extern "C" {
#include <tc_viewport_config.h>
}

namespace termin {

tc_viewport_config ViewportConfig::to_c() const {
    tc_viewport_config c;
    tc_viewport_config_init(&c);

    c.name = name.empty() ? nullptr : tgfx_intern_string(name.c_str());
    c.display_name = display_name.empty() ? nullptr : tgfx_intern_string(display_name.c_str());
    c.render_target_name = render_target_name.empty() ? nullptr : tgfx_intern_string(render_target_name.c_str());
    c.region[0] = region_x;
    c.region[1] = region_y;
    c.region[2] = region_w;
    c.region[3] = region_h;
    c.depth = depth;
    c.input_mode = input_mode.empty() ? nullptr : tgfx_intern_string(input_mode.c_str());
    c.block_input_in_editor = block_input_in_editor;
    c.enabled = enabled;

    return c;
}

ViewportConfig ViewportConfig::from_c(const tc_viewport_config* c) {
    ViewportConfig cfg;
    if (!c) return cfg;

    cfg.name = c->name ? c->name : "";
    cfg.display_name = c->display_name ? c->display_name : "Main";
    cfg.render_target_name = c->render_target_name ? c->render_target_name : "";
    cfg.region_x = c->region[0];
    cfg.region_y = c->region[1];
    cfg.region_w = c->region[2];
    cfg.region_h = c->region[3];
    cfg.depth = c->depth;
    cfg.input_mode = c->input_mode ? c->input_mode : "simple";
    cfg.block_input_in_editor = c->block_input_in_editor;
    cfg.enabled = c->enabled;

    return cfg;
}

} // namespace termin
