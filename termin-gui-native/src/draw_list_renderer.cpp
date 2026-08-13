#include <termin/gui_native/color_picker.hpp>
#include <termin/gui_native/draw_list_renderer.hpp>
#include <termin/gui_native/ui_icon_registry.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <tcbase/tc_log.h>
#include <tgfx/tgfx2_interop.h>
#include <tgfx2/composition2d.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/draw_list2d.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>

namespace termin::gui_native {
    namespace {

        termin::LinearColor linear_color(tc_ui_srgb_color color) {
            // DrawList2D is a linear-working-space vocabulary. Keep the single
            // authored-sRGB conversion at this frontend lowering boundary.
            return termin::srgb_to_linear(termin::SrgbColor{color.r, color.g, color.b, color.a});
        }

        tgfx::DrawTextureSampling2D texture_sampling(tc_ui_texture_sampling sampling) {
            return sampling == TC_UI_TEXTURE_SAMPLING_NEAREST ? tgfx::DrawTextureSampling2D::Nearest
                                                              : tgfx::DrawTextureSampling2D::Linear;
        }

        termin::Affine2f uniform_affine(const tc_ui_uniform_transform& transform) {
            return termin::Affine2f::translation(transform.translation.x, transform.translation.y) *
                   termin::Affine2f::scaling(transform.scale);
        }

        float geometric_scale(const tgfx::CompositionEvaluator2D& evaluator) {
            const tc_affine2f& transform = evaluator.state().local_to_world;
            return std::hypot(transform.m00, transform.m10);
        }

        std::optional<termin::Vec2f>
        snapped_local_point(const tgfx::CompositionEvaluator2D& evaluator, termin::Vec2f local, bool snap_to_pixels) {
            if (!std::isfinite(local.x) || !std::isfinite(local.y)) {
                return std::nullopt;
            }
            if (!snap_to_pixels) {
                return local;
            }
            termin::Vec2f physical{};
            if (!evaluator.map_point_to_world(local, physical)) {
                return std::nullopt;
            }
            physical.x = std::round(physical.x);
            physical.y = std::round(physical.y);
            termin::Vec2f snapped{};
            if (!evaluator.map_point_from_world(physical, snapped)) {
                return std::nullopt;
            }
            return snapped;
        }

        std::optional<termin::Rect2f>
        snapped_local_rect(const tgfx::CompositionEvaluator2D& evaluator, tc_ui_rect rect, bool snap_to_pixels) {
            if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) ||
                !std::isfinite(rect.height) || rect.width <= 0.0f || rect.height <= 0.0f) {
                return std::nullopt;
            }
            const auto top_left = snapped_local_point(evaluator, {rect.x, rect.y}, snap_to_pixels);
            const auto bottom_right =
                snapped_local_point(evaluator, {rect.x + rect.width, rect.y + rect.height}, snap_to_pixels);
            if (!top_left || !bottom_right || bottom_right->x <= top_left->x || bottom_right->y <= top_left->y) {
                return std::nullopt;
            }
            return termin::Rect2f{
                top_left->x, top_left->y, bottom_right->x - top_left->x, bottom_right->y - top_left->y};
        }

        float snapped_local_length(float logical, float scale) {
            if (logical == 0.0f || !std::isfinite(logical) || !std::isfinite(scale) || scale <= 0.0f) {
                return logical;
            }
            const float physical = std::round(logical * scale);
            const float snapped = logical > 0.0f ? std::max(1.0f, physical) : physical;
            return snapped / scale;
        }

        std::optional<tgfx::Path2f> rect_path(termin::Rect2f rect) {
            tgfx::Path2f path;
            if (!path.move_to({rect.x, rect.y}) || !path.line_to({rect.x + rect.width, rect.y}) ||
                !path.line_to({rect.x + rect.width, rect.y + rect.height}) ||
                !path.line_to({rect.x, rect.y + rect.height}) || !path.close()) {
                return std::nullopt;
            }
            return path;
        }

        tgfx::FillPaint fill_paint(tc_ui_srgb_color color) {
            return tgfx::FillPaint{linear_color(color), tgfx::FillRule::NonZero};
        }

        tgfx::StrokePaint stroke_paint(tc_ui_srgb_color color, float width) {
            tgfx::StrokePaint paint;
            paint.color = linear_color(color);
            paint.width = width;
            return paint;
        }

        class UiDrawResources final : public tgfx::DrawResourceResolver2D {
        public:
            explicit UiDrawResources(tgfx::FontAtlas* font)
                : font_(font) {}

            tgfx::FontAtlas* resolve_font(tgfx::FontHandle) override {
                return font_;
            }

        private:
            tgfx::FontAtlas* font_ = nullptr;
        };

    } // namespace

    void UiDrawListRenderer::destroy_picker_surface_texture(tgfx::IRenderDevice* device,
                                                            ColorPickerSurfaceTexture& surface) {
        if (device && surface.texture) {
            device->destroy(surface.texture);
        }
        surface = {};
    }

    void UiDrawListRenderer::destroy_picker_textures(tgfx::IRenderDevice* device, ColorPickerTextures& textures) {
        destroy_picker_surface_texture(device, textures.saturation_value);
        destroy_picker_surface_texture(device, textures.hue);
        destroy_picker_surface_texture(device, textures.alpha);
    }

    bool UiDrawListRenderer::sync_picker_surface(tgfx::IRenderDevice& device,
                                                 const ColorPickerSurface& source,
                                                 ColorPickerSurfaceTexture& target) {
        if (source.width == 0 || source.height == 0 || source.rgba.empty()) {
            tc_log_error("[termin-gui-native] ColorPicker generated an empty surface");
            return false;
        }
        if (target.texture && target.width == source.width && target.height == source.height &&
            target.revision == source.revision) {
            return true;
        }
        if (target.texture) {
            device.destroy(target.texture);
            target = {};
        }
        tgfx::TextureDesc desc{};
        desc.width = source.width;
        desc.height = source.height;
        // ColorPickerSurface stores authored sRGB bytes. The texture format
        // owns the decode to linear working space before filtering and UI
        // composition, matching solid SrgbColor preview swatches.
        desc.format = tgfx::PixelFormat::RGBA8_sRGB;
        desc.usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::CopyDst;
        target.texture = device.create_texture(desc);
        if (!target.texture) {
            tc_log_error("[termin-gui-native] failed to create ColorPicker surface texture");
            return false;
        }
        device.upload_texture(target.texture, source.rgba);
        target.width = source.width;
        target.height = source.height;
        target.revision = source.revision;
        return true;
    }

    void UiDrawListRenderer::destroy_icon_textures() {
        const bool device_is_live = icon_device_ != nullptr && tgfx2_interop_get_device() == icon_device_;
        if (device_is_live) {
            for (const IconTexture& icon : icon_textures_) {
                if (icon.texture)
                    icon_device_->destroy(icon.texture);
            }
        }
        icon_textures_.clear();
        icon_device_ = nullptr;
    }

    tgfx::TextureHandle UiDrawListRenderer::sync_icon_texture(tgfx::IRenderDevice& device,
                                                              std::string_view icon_id,
                                                              uint32_t width,
                                                              uint32_t height) {
        if (icon_device_ != &device) {
            destroy_icon_textures();
            icon_device_ = &device;
        }
        const auto found = std::find_if(icon_textures_.begin(), icon_textures_.end(), [&](const IconTexture& icon) {
            return icon.icon_id == icon_id && icon.width == width && icon.height == height;
        });
        if (found != icon_textures_.end())
            return found->texture;

        std::vector<uint8_t> rgba = UiIconRegistry::builtin().rasterize(icon_id, width, height);
        if (rgba.empty()) {
            tc_log_error("[termin-gui-native] failed to rasterize UI icon '%.*s'",
                         static_cast<int>(icon_id.size()),
                         icon_id.data());
            return {};
        }
        tgfx::TextureDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.format = tgfx::PixelFormat::RGBA8_UNorm;
        desc.usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::CopyDst;
        const tgfx::TextureHandle texture = device.create_texture(desc);
        if (!texture) {
            tc_log_error("[termin-gui-native] failed to create UI icon texture");
            return {};
        }
        device.upload_texture(texture, rgba);
        icon_textures_.push_back(IconTexture{std::string(icon_id), width, height, texture});
        return texture;
    }

    bool UiDrawListRenderer::set_default_font_path(const std::string& path, int default_size_px) {
        try {
            owned_font_ = std::make_unique<tgfx::FontAtlas>(path, default_size_px);
            canvas_.set_default_font(owned_font_.get());
            missing_font_logged_ = false;
            return true;
        } catch (const std::exception& e) {
            tc_log_error("[termin-gui-native] failed to load UI font '%s': %s", path.c_str(), e.what());
            owned_font_.reset();
            canvas_.set_default_font(nullptr);
            return false;
        }
    }

    tgfx::FontAtlas* UiDrawListRenderer::default_font() const {
        return owned_font_.get();
    }

    void UiDrawListRenderer::bind_text_measurer(tc_ui_document_handle document) {
        tc_ui_document_set_text_measurer(document, &UiDrawListRenderer::measure_text, this);
    }

    void UiDrawListRenderer::sync_color_picker_surfaces(tgfx::RenderContext2& context, ColorPicker& picker) {
        tgfx::IRenderDevice& device = context.device();
        if (color_picker_device_ != &device) {
            const bool device_is_live =
                color_picker_device_ != nullptr && tgfx2_interop_get_device() == color_picker_device_;
            if (device_is_live) {
                for (auto& [_, textures] : color_picker_textures_) {
                    destroy_picker_textures(color_picker_device_, textures);
                }
            }
            color_picker_textures_.clear();
            color_picker_device_ = &device;
        }

        ColorPickerTextures& textures = color_picker_textures_[&picker];
        const bool has_sv = sync_picker_surface(
            device, picker.surface(ColorPickerSurfaceKind::SaturationValue), textures.saturation_value);
        const bool has_hue = sync_picker_surface(device, picker.surface(ColorPickerSurfaceKind::Hue), textures.hue);
        bool has_alpha = true;
        if (picker.model()->show_alpha()) {
            has_alpha = sync_picker_surface(device, picker.surface(ColorPickerSurfaceKind::Alpha), textures.alpha);
        } else {
            destroy_picker_surface_texture(color_picker_device_, textures.alpha);
        }

        if (has_sv && has_hue && has_alpha) {
            picker.set_texture_ids(ColorPickerTextureIds{
                textures.saturation_value.texture.id,
                textures.hue.texture.id,
                textures.alpha.texture.id,
            });
        } else {
            picker.set_texture_ids({});
        }
    }

    void UiDrawListRenderer::release_color_picker_surfaces(ColorPicker& picker) {
        const auto found = color_picker_textures_.find(&picker);
        if (found == color_picker_textures_.end()) {
            return;
        }
        const bool device_is_live =
            color_picker_device_ != nullptr && tgfx2_interop_get_device() == color_picker_device_;
        if (device_is_live) {
            destroy_picker_textures(color_picker_device_, found->second);
        }
        color_picker_textures_.erase(found);
    }

    bool UiDrawListRenderer::measure_text(void* user_data,
                                          const char* text_utf8,
                                          size_t text_byte_length,
                                          float font_size,
                                          tc_ui_text_metrics* out_metrics) {
        auto* self = static_cast<UiDrawListRenderer*>(user_data);
        if (!self || !self->owned_font_ || !out_metrics) {
            return false;
        }
        const std::string_view text(text_utf8 ? text_utf8 : "", text_byte_length);
        self->owned_font_->ensure_glyphs(text, font_size);
        const tgfx::FontAtlas::Size2f measured = self->owned_font_->measure_text(text, font_size);
        out_metrics->width = measured.width;
        out_metrics->height = measured.height;
        out_metrics->ascent = static_cast<float>(self->owned_font_->ascent_px(font_size));
        out_metrics->descent = static_cast<float>(self->owned_font_->descent_px(font_size));
        out_metrics->line_height = static_cast<float>(self->owned_font_->line_height_px(font_size));
        return true;
    }

    void UiDrawListRenderer::render(tgfx::RenderContext2& context,
                                    const tc_ui_draw_list* draw_list,
                                    int width,
                                    int height,
                                    std::span<const UiDrawListBatch> batches) {
        if (!draw_list) {
            tc_log_error("[termin-gui-native] cannot render null UI draw list");
            return;
        }
        if (width <= 0 || height <= 0) {
            tc_log_error("[termin-gui-native] cannot render UI draw list into invalid viewport %dx%d", width, height);
            return;
        }

        canvas_.begin(context, width, height);
        const size_t count = tc_ui_draw_list_command_count(draw_list);
        for (const UiDrawListBatch& batch : batches) {
            if (!tc_ui_presentation_metrics_is_valid(&batch.presentation_metrics) || batch.first_command > count ||
                batch.command_count > count - batch.first_command) {
                tc_log_error("[termin-gui-native] skipping invalid UI draw-list batch");
                continue;
            }
            tgfx::DrawList2DBuilder builder;
            tgfx::CompositionEvaluator2D evaluator;
            enum class ScopeKind {
                Transform,
                Clip,
                EmptyClip,
                SuppressedTransform,
                SuppressedClip,
            };
            std::vector<ScopeKind> scope_stack;
            size_t empty_clip_depth = 0;
            const size_t batch_end = batch.first_command + batch.command_count;
            const bool snap_to_pixels = batch.presentation_metrics.density_scale != 1.0f;
            bool batch_ok = true;
            size_t failed_index = batch.first_command;
            tc_ui_draw_command_type failed_command_type = TC_UI_DRAW_FILL_RECT;
            tc_ui_rect failed_rect{};
            const char* failure_reason = "command lowering failed";
            try {
                evaluator.begin_batch(&builder);
                tgfx::CompositionLayer2D density_layer;
                density_layer.transform = termin::Affine2f::scaling(batch.presentation_metrics.density_scale);
                batch_ok = evaluator.push(density_layer);

                for (size_t index = batch.first_command; batch_ok && index < batch_end; ++index) {
                    failed_index = index;
                    const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, index);
                    if (!command) {
                        batch_ok = false;
                        failure_reason = "command lookup failed";
                        break;
                    }
                    failed_command_type = command->type;
                    failed_rect = command->rect;
                    if (empty_clip_depth > 0) {
                        switch (command->type) {
                        case TC_UI_DRAW_PUSH_UNIFORM_TRANSFORM:
                            scope_stack.push_back(ScopeKind::SuppressedTransform);
                            break;
                        case TC_UI_DRAW_POP_TRANSFORM:
                            batch_ok = !scope_stack.empty() &&
                                       scope_stack.back() == ScopeKind::SuppressedTransform;
                            if (batch_ok)
                                scope_stack.pop_back();
                            break;
                        case TC_UI_DRAW_PUSH_CLIP:
                            scope_stack.push_back(ScopeKind::SuppressedClip);
                            break;
                        case TC_UI_DRAW_POP_CLIP:
                            batch_ok = !scope_stack.empty() &&
                                       (scope_stack.back() == ScopeKind::SuppressedClip ||
                                        scope_stack.back() == ScopeKind::EmptyClip);
                            if (batch_ok) {
                                if (scope_stack.back() == ScopeKind::EmptyClip)
                                    --empty_clip_depth;
                                scope_stack.pop_back();
                            }
                            break;
                        default:
                            break;
                        }
                        continue;
                    }
                    const float scale = geometric_scale(evaluator);
                    const auto snapped_rect = [&]() {
                        return snapped_local_rect(evaluator, command->rect, snap_to_pixels);
                    };
                    const auto snapped_point = [&](tc_ui_point point) {
                        return snapped_local_point(evaluator, {point.x, point.y}, snap_to_pixels);
                    };

                    switch (command->type) {
                    case TC_UI_DRAW_PUSH_UNIFORM_TRANSFORM: {
                        if (!tc_ui_uniform_transform_is_valid(&command->transform)) {
                            batch_ok = false;
                            break;
                        }
                        tgfx::CompositionLayer2D layer;
                        layer.transform = uniform_affine(command->transform);
                        batch_ok = evaluator.push(layer);
                        if (batch_ok)
                            scope_stack.push_back(ScopeKind::Transform);
                        break;
                    }
                    case TC_UI_DRAW_POP_TRANSFORM:
                        batch_ok =
                            !scope_stack.empty() && scope_stack.back() == ScopeKind::Transform && evaluator.pop();
                        if (batch_ok)
                            scope_stack.pop_back();
                        break;
                    case TC_UI_DRAW_PUSH_CLIP: {
                        if (!std::isfinite(command->rect.x) || !std::isfinite(command->rect.y) ||
                            !std::isfinite(command->rect.width) || !std::isfinite(command->rect.height)) {
                            batch_ok = false;
                            break;
                        }
                        const auto rect = snapped_rect();
                        if (!rect) {
                            scope_stack.push_back(ScopeKind::EmptyClip);
                            ++empty_clip_depth;
                            break;
                        }
                        const auto path = rect_path(*rect);
                        if (!path) {
                            batch_ok = false;
                            break;
                        }
                        tgfx::CompositionLayer2D layer;
                        layer.clip = tgfx::CompositionClip2D{*path, tgfx::FillRule::NonZero};
                        batch_ok = evaluator.push(layer);
                        if (batch_ok)
                            scope_stack.push_back(ScopeKind::Clip);
                        break;
                    }
                    case TC_UI_DRAW_POP_CLIP:
                        batch_ok = !scope_stack.empty() && scope_stack.back() == ScopeKind::Clip && evaluator.pop();
                        if (batch_ok)
                            scope_stack.pop_back();
                        break;
                    case TC_UI_DRAW_FILL_RECT: {
                        const auto rect = snapped_rect();
                        batch_ok = !rect || builder.rect(*rect, fill_paint(command->color));
                        break;
                    }
                    case TC_UI_DRAW_STROKE_RECT: {
                        const auto rect = snapped_rect();
                        const float thickness = snapped_local_length(command->thickness, scale);
                        if (!rect || thickness <= 0.0f) {
                            break;
                        }
                        const float t = std::min(thickness, std::min(rect->width, rect->height));
                        batch_ok = builder.rect({rect->x, rect->y, rect->width, t}, fill_paint(command->color)) &&
                                   builder.rect({rect->x, rect->y + rect->height - t, rect->width, t},
                                                fill_paint(command->color)) &&
                                   builder.rect({rect->x, rect->y, t, rect->height}, fill_paint(command->color)) &&
                                   builder.rect({rect->x + rect->width - t, rect->y, t, rect->height},
                                                fill_paint(command->color));
                        break;
                    }
                    case TC_UI_DRAW_FILL_ROUNDED_RECT: {
                        const auto rect = snapped_rect();
                        batch_ok = !rect || builder.rounded_rect(*rect,
                                                                 snapped_local_length(command->radius, scale),
                                                                 fill_paint(command->color));
                        break;
                    }
                    case TC_UI_DRAW_STROKE_ROUNDED_RECT: {
                        const auto rect = snapped_rect();
                        if (!rect)
                            break;
                        batch_ok = builder.rounded_rect(
                            *rect,
                            snapped_local_length(command->radius, scale),
                            fill_paint(tc_ui_srgb_color{0.0f, 0.0f, 0.0f, 0.0f}),
                            stroke_paint(command->color, snapped_local_length(command->thickness, scale)));
                        break;
                    }
                    case TC_UI_DRAW_FILL_CIRCLE:
                    case TC_UI_DRAW_STROKE_CIRCLE: {
                        const auto center = snapped_point(command->p0);
                        const float radius = snapped_local_length(command->radius, scale);
                        if (!center || radius <= 0.0f) {
                            batch_ok = false;
                            break;
                        }
                        const termin::Rect2f bounds{
                            center->x - radius, center->y - radius, radius * 2.0f, radius * 2.0f};
                        std::optional<tgfx::StrokePaint> stroke;
                        tgfx::FillPaint fill = fill_paint(command->color);
                        if (command->type == TC_UI_DRAW_STROKE_CIRCLE) {
                            fill = fill_paint(tc_ui_srgb_color{0.0f, 0.0f, 0.0f, 0.0f});
                            stroke = stroke_paint(command->color, snapped_local_length(command->thickness, scale));
                        }
                        batch_ok = builder.ellipse(bounds, fill, std::move(stroke));
                        break;
                    }
                    case TC_UI_DRAW_ARC: {
                        constexpr float kTau = 6.2831853071795864769f;
                        const float sweep = command->end_radians - command->start_radians;
                        if (!std::isfinite(sweep) || std::fabs(sweep) <= 0.0001f)
                            break;
                        int segments = command->segments;
                        if (segments <= 0)
                            segments = static_cast<int>(std::ceil(24.0f * std::fabs(sweep) / kTau));
                        segments = std::clamp(segments, 2, 192);
                        std::vector<termin::Vec2f> points;
                        points.reserve(static_cast<size_t>(segments) + 1);
                        for (int segment = 0; segment <= segments; ++segment) {
                            const float t = static_cast<float>(segment) / static_cast<float>(segments);
                            const float angle = command->start_radians + sweep * t;
                            const auto point = snapped_local_point(evaluator,
                                                                   {command->p0.x + std::cos(angle) * command->radius,
                                                                    command->p0.y + std::sin(angle) * command->radius},
                                                                   snap_to_pixels);
                            if (!point) {
                                batch_ok = false;
                                break;
                            }
                            points.push_back(*point);
                        }
                        if (batch_ok)
                            batch_ok = builder.polyline(
                                points, stroke_paint(command->color, snapped_local_length(command->thickness, scale)));
                        break;
                    }
                    case TC_UI_DRAW_LINE: {
                        const auto p0 = snapped_point(command->p0);
                        const auto p1 = snapped_point(command->p1);
                        if (!p0 || !p1) {
                            batch_ok = false;
                            break;
                        }
                        const termin::Vec2f points[]{*p0, *p1};
                        batch_ok = builder.polyline(
                            points, stroke_paint(command->color, snapped_local_length(command->thickness, scale)));
                        break;
                    }
                    case TC_UI_DRAW_POLYLINE: {
                        std::vector<termin::Vec2f> points;
                        points.reserve(command->point_count);
                        for (size_t point_index = 0; point_index < command->point_count; ++point_index) {
                            const auto point = snapped_point(command->points[point_index]);
                            if (!point) {
                                batch_ok = false;
                                break;
                            }
                            points.push_back(*point);
                        }
                        if (batch_ok)
                            batch_ok = builder.polyline(
                                points, stroke_paint(command->color, snapped_local_length(command->thickness, scale)));
                        break;
                    }
                    case TC_UI_DRAW_TEXTURE: {
                        const auto rect = snapped_rect();
                        if (!rect)
                            break;
                        if (!command->flip_v) {
                            batch_ok = builder.image(tgfx::TextureHandle{command->texture_id},
                                                     *rect,
                                                     {0.0f, 0.0f, 1.0f, 1.0f},
                                                     linear_color(command->color),
                                                     texture_sampling(command->texture_sampling));
                        } else {
                            const float x0 = rect->x;
                            const float y0 = rect->y;
                            const float x1 = x0 + rect->width;
                            const float y1 = y0 + rect->height;
                            const tgfx::DrawVertex2D vertices[]{
                                {{x0, y0}, {0.0f, 1.0f}},
                                {{x0, y1}, {0.0f, 0.0f}},
                                {{x1, y1}, {1.0f, 0.0f}},
                                {{x0, y0}, {0.0f, 1.0f}},
                                {{x1, y1}, {1.0f, 0.0f}},
                                {{x1, y0}, {1.0f, 1.0f}},
                            };
                            batch_ok = builder.custom_batch(vertices,
                                                            linear_color(command->color),
                                                            tgfx::TextureHandle{command->texture_id},
                                                            texture_sampling(command->texture_sampling));
                        }
                        break;
                    }
                    case TC_UI_DRAW_ICON: {
                        const auto rect = snapped_rect();
                        if (!rect)
                            break;
                        constexpr float kMaxIconExtent = 256.0f;
                        const float physical_width = std::round(rect->width * scale);
                        const float physical_height = std::round(rect->height * scale);
                        if (!std::isfinite(physical_width) || !std::isfinite(physical_height) ||
                            physical_width < 1.0f || physical_height < 1.0f) {
                            batch_ok = false;
                            break;
                        }
                        const tgfx::TextureHandle texture =
                            sync_icon_texture(context.device(),
                                              command->text ? command->text : "",
                                              static_cast<uint32_t>(std::min(physical_width, kMaxIconExtent)),
                                              static_cast<uint32_t>(std::min(physical_height, kMaxIconExtent)));
                        batch_ok =
                            texture &&
                            builder.image(texture, *rect, {0.0f, 0.0f, 1.0f, 1.0f}, linear_color(command->color));
                        break;
                    }
                    case TC_UI_DRAW_TEXT:
                        if (canvas_.default_font()) {
                            const auto baseline = snapped_point(command->p0);
                            const float physical_font_size =
                                command->font_size * scale * batch.presentation_metrics.font_scale;
                            if (!baseline || !std::isfinite(physical_font_size) || physical_font_size <= 0.0f) {
                                batch_ok = false;
                                break;
                            }
                            const termin::Vec2f line_top{
                                baseline->x,
                                baseline->y - static_cast<float>(owned_font_->ascent_px(physical_font_size)) / scale,
                            };
                            batch_ok = builder.text(command->text ? command->text : "",
                                                    line_top,
                                                    command->font_size,
                                                    linear_color(command->color),
                                                    tgfx::FontHandle{1});
                        } else if (!missing_font_logged_) {
                            tc_log_error(
                                "[termin-gui-native] skipping UI text commands because no default font is configured");
                            missing_font_logged_ = true;
                        }
                        break;
                    case TC_UI_DRAW_CANVAS2D_LIST: {
                        const auto* nested = static_cast<const tgfx::DrawList2D*>(command->canvas2d_list);
                        batch_ok = nested && builder.append(*nested);
                        break;
                    }
                    default:
                        batch_ok = false;
                        break;
                    }
                }

                if (batch_ok && !scope_stack.empty()) {
                    failed_index = batch_end;
                    failure_reason = "unclosed composition scope";
                    batch_ok = false;
                }
                if (batch_ok) {
                    batch_ok = evaluator.pop(); // density layer
                    if (!batch_ok) {
                        failed_index = batch_end;
                        failure_reason = "density scope pop failed";
                    }
                }
                if (batch_ok) {
                    batch_ok = evaluator.end_batch();
                    if (!batch_ok) {
                        failed_index = batch_end;
                        failure_reason = "composition batch finalization failed";
                    }
                }
            } catch (const std::exception& error) {
                tc_log_error("[termin-gui-native] UI draw-list lowering threw: %s", error.what());
                batch_ok = false;
            }

            if (!batch_ok) {
                if (evaluator.active())
                    evaluator.abort_batch();
                tc_log_error("[termin-gui-native] malformed UI draw-list batch near command %zu "
                             "(type=%d, rect=[%.3f, %.3f, %.3f, %.3f], reason=%s); discarding batch",
                             failed_index,
                             static_cast<int>(failed_command_type),
                             failed_rect.x,
                             failed_rect.y,
                             failed_rect.width,
                             failed_rect.height,
                             failure_reason);
                continue;
            }

            auto lowered = builder.freeze();
            UiDrawResources resources(canvas_.default_font());
            if (!lowered || !canvas_.execute(*lowered, resources, batch.presentation_metrics.font_scale)) {
                tc_log_error("[termin-gui-native] failed to execute lowered UI DrawList2D batch");
            }
        }
        canvas_.end();
    }

    void UiDrawListRenderer::release_gpu() {
        canvas_.release_gpu();
        if (owned_font_) {
            owned_font_->release_gpu();
        }
        const bool device_is_live =
            color_picker_device_ != nullptr && tgfx2_interop_get_device() == color_picker_device_;
        if (device_is_live) {
            for (auto& [_, textures] : color_picker_textures_) {
                destroy_picker_textures(color_picker_device_, textures);
            }
        }
        color_picker_textures_.clear();
        color_picker_device_ = nullptr;
        destroy_icon_textures();
    }

} // namespace termin::gui_native
