#include "termin_visual_scene/tc_visual_scene_item2d.h"

#include <cmath>
#include <cstring>
#include <exception>
#include <optional>
#include <vector>

#include <tcbase/tc_log.hpp>
#include <tgfx2/path2d.hpp>

#include "termin_visual_scene/graphic_item2d.hpp"

namespace {

using termin::visual::GeometricClip2D;
using termin::visual::GraphicItem2D;
using termin::visual::TcVisualScene;

tc_graphic_item *resolve(tc_visual_scene_handle scene,
                         tc_graphic_item_handle item, const char *operation) {
  tc_graphic_item *result = tc_visual_scene_resolve_item(scene, item);
  if (result == nullptr) {
    tc::Log::error(operation, ": stale or cross-scene item handle");
  }
  return result;
}

GraphicItem2D *resolve_object(tc_visual_scene_handle scene,
                              tc_graphic_item_handle item,
                              const char *operation) {
  tc_graphic_item *value = resolve(scene, item, operation);
  if (value == nullptr || value->body == nullptr)
    return nullptr;
  if (value->native_language != TC_LANGUAGE_CXX) {
    tc::Log::error(operation, ": item has no native C++ body");
    return nullptr;
  }
  return static_cast<GraphicItem2D *>(value->body);
}

std::optional<tgfx::FillRule> fill_rule(tc_visual_fill_rule2d value) {
  if (value == TC_VISUAL_FILL_RULE_NON_ZERO) {
    return tgfx::FillRule::NonZero;
  }
  if (value == TC_VISUAL_FILL_RULE_EVEN_ODD) {
    return tgfx::FillRule::EvenOdd;
  }
  return std::nullopt;
}

bool make_path(tc_visual_path2d_view source, tgfx::Path2f &out) {
  if ((source.verb_count != 0 && source.verbs == nullptr) ||
      (source.point_count != 0 && source.points == nullptr)) {
    tc::Log::error("visual path contains a null input array");
    return false;
  }
  std::vector<tgfx::Path2Verb> verbs;
  verbs.reserve(source.verb_count);
  for (std::size_t index = 0; index < source.verb_count; ++index) {
    switch (source.verbs[index]) {
    case TC_VISUAL_PATH_MOVE_TO:
      verbs.push_back(tgfx::Path2Verb::MoveTo);
      break;
    case TC_VISUAL_PATH_LINE_TO:
      verbs.push_back(tgfx::Path2Verb::LineTo);
      break;
    case TC_VISUAL_PATH_QUADRATIC_TO:
      verbs.push_back(tgfx::Path2Verb::QuadraticTo);
      break;
    case TC_VISUAL_PATH_CUBIC_TO:
      verbs.push_back(tgfx::Path2Verb::CubicTo);
      break;
    case TC_VISUAL_PATH_CLOSE:
      verbs.push_back(tgfx::Path2Verb::Close);
      break;
    default:
      tc::Log::error("visual path contains an unknown verb");
      return false;
    }
  }
  const std::span<const termin::Vec2f> points{source.points,
                                              source.point_count};
  if (!out.try_assign(verbs, points)) {
    tc::Log::error("visual path verb/point stream is invalid");
    return false;
  }
  return true;
}

} // namespace

extern "C" {

bool tc_visual_scene_item_is_valid(tc_visual_scene_handle scene,
                                   tc_graphic_item_handle item) {
  return tc_visual_scene_resolve_item(scene, item) != nullptr;
}

const char *tc_visual_scene_item_type_name(tc_visual_scene_handle scene,
                                           tc_graphic_item_handle item) {
  const tc_graphic_item *value =
      tc_visual_scene_resolve_item_const(scene, item);
  return value != nullptr ? tc_graphic_item_type_name(value) : nullptr;
}

bool tc_visual_scene_item_is_type(tc_visual_scene_handle scene,
                                  tc_graphic_item_handle item,
                                  const char *type_name) {
  const char *actual = tc_visual_scene_item_type_name(scene, item);
  return actual != nullptr && type_name != nullptr &&
         std::strcmp(actual, type_name) == 0;
}

bool tc_visual_scene_item_get_transform(tc_visual_scene_handle scene,
                                        tc_graphic_item_handle item,
                                        tc_affine2f *out_transform) {
  const auto *object = resolve_object(scene, item, "get item transform");
  if (object == nullptr || out_transform == nullptr)
    return false;
  *out_transform = object->local_transform();
  return true;
}

bool tc_visual_scene_item_set_transform(tc_visual_scene_handle scene,
                                        tc_graphic_item_handle item,
                                        tc_affine2f transform) {
  auto *object = resolve_object(scene, item, "set item transform");
  if (object == nullptr || !tc_affine2f_is_finite(transform)) {
    tc::Log::error("set item transform: transform must be finite");
    return false;
  }
  object->set_local_transform(transform);
  return true;
}

#define TC_VISUAL_ITEM_BOOL_PROPERTY(name)                                     \
  bool tc_visual_scene_item_get_##name(tc_visual_scene_handle scene,           \
                                       tc_graphic_item_handle item,            \
                                       bool *out_value) {                      \
    auto *object = resolve_object(scene, item, "get item " #name);             \
    if (object == nullptr || out_value == nullptr)                             \
      return false;                                                            \
    *out_value = object->name();                                               \
    return true;                                                               \
  }                                                                            \
  bool tc_visual_scene_item_set_##name(                                        \
      tc_visual_scene_handle scene, tc_graphic_item_handle item, bool value) { \
    auto *object = resolve_object(scene, item, "set item " #name);             \
    if (object == nullptr)                                                     \
      return false;                                                            \
    object->set_##name(value);                                                 \
    return true;                                                               \
  }

TC_VISUAL_ITEM_BOOL_PROPERTY(visible)
TC_VISUAL_ITEM_BOOL_PROPERTY(enabled)

#undef TC_VISUAL_ITEM_BOOL_PROPERTY

bool tc_visual_scene_item_get_opacity(tc_visual_scene_handle scene,
                                      tc_graphic_item_handle item,
                                      float *out_opacity) {
  auto *object = resolve_object(scene, item, "get item opacity");
  if (object == nullptr || out_opacity == nullptr)
    return false;
  *out_opacity = object->opacity();
  return true;
}

bool tc_visual_scene_item_set_opacity(tc_visual_scene_handle scene,
                                      tc_graphic_item_handle item,
                                      float opacity) {
  auto *object = resolve_object(scene, item, "set item opacity");
  if (object == nullptr || !std::isfinite(opacity) || opacity < 0.0f ||
      opacity > 1.0f) {
    tc::Log::error("set item opacity: value must be in [0, 1]");
    return false;
  }
  object->set_opacity(opacity);
  return true;
}

bool tc_visual_scene_item_get_z_order(tc_visual_scene_handle scene,
                                      tc_graphic_item_handle item,
                                      int64_t *out_z_order) {
  auto *object = resolve_object(scene, item, "get item z-order");
  if (object == nullptr || out_z_order == nullptr)
    return false;
  *out_z_order = object->z_order();
  return true;
}

bool tc_visual_scene_item_set_z_order(tc_visual_scene_handle scene,
                                      tc_graphic_item_handle item,
                                      int64_t z_order) {
  auto *object = resolve_object(scene, item, "set item z-order");
  if (object == nullptr)
    return false;
  object->set_z_order(z_order);
  return true;
}

tc_graphic_item_handle
tc_visual_scene_item_parent(tc_visual_scene_handle scene,
                            tc_graphic_item_handle item) {
  tc_graphic_item *value = resolve(scene, item, "get item parent");
  return value != nullptr && value->parent != nullptr
             ? value->parent->handle
             : tc_graphic_item_handle_invalid();
}

size_t tc_visual_scene_item_child_count(tc_visual_scene_handle scene,
                                        tc_graphic_item_handle item) {
  tc_graphic_item *value = resolve(scene, item, "get item child count");
  return value != nullptr ? value->child_count : 0;
}

tc_graphic_item_handle
tc_visual_scene_item_child_at(tc_visual_scene_handle scene,
                              tc_graphic_item_handle item, size_t index) {
  tc_graphic_item *value = resolve(scene, item, "get item child");
  if (value == nullptr || index >= value->child_count) {
    tc::Log::error("get item child: index is out of range");
    return tc_graphic_item_handle_invalid();
  }
  return value->children[index]->handle;
}

bool tc_visual_scene_item_set_parent(tc_visual_scene_handle scene,
                                     tc_graphic_item_handle item,
                                     tc_graphic_item_handle parent,
                                     size_t index) {
  tc_graphic_item *value = resolve(scene, item, "set item parent");
  if (value == nullptr)
    return false;
  if (tc_graphic_item_handle_is_invalid(parent)) {
    return tc_graphic_item_detach(value);
  }
  tc_graphic_item *parent_value = resolve(scene, parent, "set item parent");
  if (parent_value == nullptr || index > parent_value->child_count) {
    tc::Log::error("set item parent: child index is out of range");
    return false;
  }
  if (value->parent == parent_value) {
    if (!tc_graphic_item_detach(value))
      return false;
    if (index > parent_value->child_count) {
      index = parent_value->child_count;
    }
  }
  return tc_graphic_item_insert_child(parent_value, index, value);
}

bool tc_visual_scene_item_get_local_bounds(tc_visual_scene_handle scene,
                                           tc_graphic_item_handle item,
                                           tc_bounds2f *out_bounds) {
  tc_graphic_item *value = resolve(scene, item, "get local bounds");
  if (value == nullptr || out_bounds == nullptr)
    return false;
  const auto bounds = TcVisualScene{scene}.local_bounds(*value);
  if (!bounds)
    return false;
  *out_bounds = *bounds;
  return true;
}

bool tc_visual_scene_item_get_world_bounds(tc_visual_scene_handle scene,
                                           tc_graphic_item_handle item,
                                           tc_bounds2f *out_bounds) {
  tc_graphic_item *value = resolve(scene, item, "get world bounds");
  if (value == nullptr || out_bounds == nullptr)
    return false;
  const auto bounds = TcVisualScene{scene}.world_bounds(*value);
  if (!bounds)
    return false;
  *out_bounds = *bounds;
  return true;
}

bool tc_visual_scene_item_set_clip_path(tc_visual_scene_handle scene,
                                        tc_graphic_item_handle item,
                                        tc_visual_path2d_view path,
                                        tc_visual_fill_rule2d rule) {
  auto *object = resolve_object(scene, item, "set item clip");
  tgfx::Path2f owned_path;
  const auto owned_rule = fill_rule(rule);
  if (object == nullptr || !owned_rule || !make_path(path, owned_path) ||
      owned_path.empty()) {
    tc::Log::error("set item clip: invalid clip");
    return false;
  }
  object->set_clip(GeometricClip2D{std::move(owned_path), *owned_rule});
  return true;
}

bool tc_visual_scene_item_set_clip_rect(tc_visual_scene_handle scene,
                                        tc_graphic_item_handle item,
                                        tc_rect2f rect) {
  const tc_visual_path_verb2d verbs[] = {
      TC_VISUAL_PATH_MOVE_TO, TC_VISUAL_PATH_LINE_TO, TC_VISUAL_PATH_LINE_TO,
      TC_VISUAL_PATH_LINE_TO, TC_VISUAL_PATH_CLOSE,
  };
  const tc_vec2f points[] = {
      {rect.x, rect.y},
      {rect.x + rect.width, rect.y},
      {rect.x + rect.width, rect.y + rect.height},
      {rect.x, rect.y + rect.height},
  };
  return tc_visual_scene_item_set_clip_path(scene, item, {verbs, 5, points, 4},
                                            TC_VISUAL_FILL_RULE_NON_ZERO);
}

bool tc_visual_scene_item_clear_clip(tc_visual_scene_handle scene,
                                     tc_graphic_item_handle item) {
  auto *object = resolve_object(scene, item, "clear item clip");
  if (object == nullptr)
    return false;
  object->set_clip(std::nullopt);
  return true;
}

} // extern "C"
