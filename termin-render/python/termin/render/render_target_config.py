"""RenderTargetConfig serialization helpers."""

from __future__ import annotations

from termin.render._render_native import RenderTargetConfig

__all__ = [
    "RenderTargetConfig",
    "serialize_render_target_config",
    "deserialize_render_target_config",
]


def serialize_render_target_config(config: RenderTargetConfig) -> dict:
    """Serialize RenderTargetConfig to a scene-dict representation."""
    result = {
        "name": config.name,
    }
    if config.kind and config.kind != "texture_2d":
        result["kind"] = config.kind
    if config.camera_uuid:
        result["camera_uuid"] = config.camera_uuid
    if config.xr_origin_uuid:
        result["xr_origin_uuid"] = config.xr_origin_uuid
    if config.dynamic_resolution:
        result["dynamic_resolution"] = True
    else:
        result["width"] = config.width
        result["height"] = config.height
    if config.color_format:
        result["color_format"] = config.color_format
    if config.depth_format:
        result["depth_format"] = config.depth_format
    if config.clear_color:
        result["clear_color"] = [float(v) for v in config.clear_color_value]
    if config.clear_depth:
        result["clear_depth"] = float(config.clear_depth_value)
    if config.pipeline_uuid:
        result["pipeline_uuid"] = config.pipeline_uuid
    if config.pipeline_name:
        result["pipeline_name"] = config.pipeline_name
    if config.layer_mask != 0xFFFFFFFFFFFFFFFF:
        result["layer_mask"] = hex(config.layer_mask)
    if not config.enabled:
        result["enabled"] = config.enabled
    if config.pipeline_params:
        result["pipeline_params"] = dict(config.pipeline_params)
    return result


def deserialize_render_target_config(data: dict) -> RenderTargetConfig:
    """Deserialize RenderTargetConfig from a scene-dict representation."""
    config = RenderTargetConfig()
    config.name = data.get("name", "")
    config.kind = data.get("kind", "texture_2d")
    config.camera_uuid = data.get("camera_uuid", "")
    config.xr_origin_uuid = data.get("xr_origin_uuid", "")
    config.width = data.get("width", 512)
    config.height = data.get("height", 512)
    config.dynamic_resolution = data.get("dynamic_resolution", False)
    config.color_format = data.get("color_format", "rgba16f")
    config.depth_format = data.get("depth_format", "depth32f")
    clear_color = data.get("clear_color", None)
    if isinstance(clear_color, (list, tuple)) and len(clear_color) >= 4:
        config.clear_color = True
        config.clear_color_value = tuple(float(v) for v in clear_color[:4])
    else:
        config.clear_color = False
        config.clear_color_value = (0.0, 0.0, 0.0, 1.0)
    clear_depth = data.get("clear_depth", None)
    if clear_depth is not None:
        config.clear_depth = True
        config.clear_depth_value = float(clear_depth)
    else:
        config.clear_depth = False
        config.clear_depth_value = 1.0
    config.pipeline_uuid = data.get("pipeline_uuid", "")
    config.pipeline_name = data.get("pipeline_name", "")

    layer_mask_raw = data.get("layer_mask", 0xFFFFFFFFFFFFFFFF)
    if isinstance(layer_mask_raw, str):
        config.layer_mask = int(layer_mask_raw, 16) if layer_mask_raw.startswith("0x") else int(layer_mask_raw)
    else:
        config.layer_mask = int(layer_mask_raw)

    config.enabled = data.get("enabled", True)
    pipeline_params = data.get("pipeline_params", {})
    if isinstance(pipeline_params, dict):
        config.pipeline_params = {
            str(slot): str(value)
            for slot, value in pipeline_params.items()
            if slot and value
        }
    return config
