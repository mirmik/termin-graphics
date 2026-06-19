// render_target_config.cpp - RenderTargetConfig implementation
#include <termin/render_target_config.hpp>
#include <tcbase/tgfx_intern_string.h>
#include <cstring>

namespace termin {

tc_render_target_config RenderTargetConfig::to_c() const {
    tc_render_target_config c;
    tc_render_target_config_init(&c);

    c.name = name.empty() ? nullptr : tgfx_intern_string(name.c_str());
    c.kind = kind.empty() ? tgfx_intern_string("texture_2d") : tgfx_intern_string(kind.c_str());
    c.camera_uuid = camera_uuid.empty() ? nullptr : tgfx_intern_string(camera_uuid.c_str());
    c.xr_origin_uuid = xr_origin_uuid.empty() ? nullptr : tgfx_intern_string(xr_origin_uuid.c_str());
    c.width = width;
    c.height = height;
    c.dynamic_resolution = dynamic_resolution;
    c.color_format = color_format.empty() ? nullptr : tgfx_intern_string(color_format.c_str());
    c.depth_format = depth_format.empty() ? nullptr : tgfx_intern_string(depth_format.c_str());
    c.clear_color = clear_color;
    std::memcpy(c.clear_color_value, clear_color_value, sizeof(c.clear_color_value));
    c.clear_depth = clear_depth;
    c.clear_depth_value = clear_depth_value;
    c.pipeline_uuid = pipeline_uuid.empty() ? nullptr : tgfx_intern_string(pipeline_uuid.c_str());
    c.pipeline_name = pipeline_name.empty() ? nullptr : tgfx_intern_string(pipeline_name.c_str());
    c.layer_mask = layer_mask;
    c.enabled = enabled;
    if (!pipeline_params.empty()) {
        c.pipeline_params = tc_value_dict_new();
        for (const auto& [slot, value] : pipeline_params) {
            if (slot.empty() || value.empty()) continue;
            tc_value_dict_set(&c.pipeline_params, slot.c_str(), tc_value_string(value.c_str()));
        }
    }

    return c;
}

RenderTargetConfig RenderTargetConfig::from_c(const tc_render_target_config* c) {
    RenderTargetConfig cfg;
    if (!c) return cfg;

    cfg.name = c->name ? c->name : "";
    cfg.kind = c->kind ? c->kind : "texture_2d";
    cfg.camera_uuid = c->camera_uuid ? c->camera_uuid : "";
    cfg.xr_origin_uuid = c->xr_origin_uuid ? c->xr_origin_uuid : "";
    cfg.width = c->width;
    cfg.height = c->height;
    cfg.dynamic_resolution = c->dynamic_resolution;
    cfg.color_format = c->color_format ? c->color_format : "rgba16f";
    cfg.depth_format = c->depth_format ? c->depth_format : "depth32f";
    cfg.clear_color = c->clear_color;
    std::memcpy(cfg.clear_color_value, c->clear_color_value, sizeof(cfg.clear_color_value));
    cfg.clear_depth = c->clear_depth;
    cfg.clear_depth_value = c->clear_depth_value;
    cfg.pipeline_uuid = c->pipeline_uuid ? c->pipeline_uuid : "";
    cfg.pipeline_name = c->pipeline_name ? c->pipeline_name : "";
    cfg.layer_mask = c->layer_mask;
    cfg.enabled = c->enabled;
    if (c->pipeline_params.type == TC_VALUE_DICT) {
        for (size_t i = 0; i < tc_value_dict_size(&c->pipeline_params); i++) {
            const char* key = nullptr;
            tc_value* value = tc_value_dict_get_at(const_cast<tc_value*>(&c->pipeline_params), i, &key);
            if (!key || !key[0] || !value || value->type != TC_VALUE_STRING || !value->data.s) {
                continue;
            }
            cfg.pipeline_params[key] = value->data.s;
        }
    }

    return cfg;
}

} // namespace termin
