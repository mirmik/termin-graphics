#include <termin/gui_native/color_picker.hpp>
#include <termin/gui_native/draw_list_renderer.hpp>
#include <termin/gui_native/ui_icon_registry.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <string_view>

#include <tcbase/tc_log.h>
#include <tgfx/tgfx2_interop.h>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/draw_list2d.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>

namespace termin::gui_native {
    namespace {

        tgfx::CanvasSrgbColor canvas_color(tc_ui_srgb_color color) {
            // CanvasSrgbColor is the renderer boundary; the backend performs the
            // single sRGB -> linear composition step for authored UI colors.
            return tgfx::CanvasSrgbColor{color.r, color.g, color.b, color.a};
        }

        tgfx::CanvasTextureSampling texture_sampling(tc_ui_texture_sampling sampling) {
            return sampling == TC_UI_TEXTURE_SAMPLING_NEAREST ? tgfx::CanvasTextureSampling::Nearest
                                                              : tgfx::CanvasTextureSampling::Linear;
        }

        float physical_coordinate(float logical, float scale) {
            return scale == 1.0f ? logical : std::round(logical * scale);
        }

        float physical_length(float logical, float scale) {
            if (scale == 1.0f || logical == 0.0f) {
                return logical;
            }
            const float snapped = std::round(logical * scale);
            return logical > 0.0f ? std::max(1.0f, snapped) : snapped;
        }

        tc_ui_point physical_point(tc_ui_point logical, float scale) {
            return {
                physical_coordinate(logical.x, scale),
                physical_coordinate(logical.y, scale),
            };
        }

        tc_ui_rect physical_rect(tc_ui_rect logical, float scale) {
            if (scale == 1.0f) {
                return logical;
            }
            const float left = std::round(logical.x * scale);
            const float top = std::round(logical.y * scale);
            const float right = std::round((logical.x + logical.width) * scale);
            const float bottom = std::round((logical.y + logical.height) * scale);
            return {left, top, right - left, bottom - top};
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
            const float scale = batch.presentation_metrics.density_scale;
            const float effective_font_scale =
                tc_ui_presentation_metrics_effective_font_scale(&batch.presentation_metrics);
            size_t clip_depth = 0;
            const size_t batch_end = batch.first_command + batch.command_count;
            for (size_t index = batch.first_command; index < batch_end; ++index) {
                const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, index);
                if (!command) {
                    tc_log_error("[termin-gui-native] draw list command disappeared at "
                                 "index %zu",
                                 index);
                    continue;
                }

                switch (command->type) {
                case TC_UI_DRAW_FILL_RECT: {
                    const tc_ui_rect rect = physical_rect(command->rect, scale);
                    canvas_.draw_rect(rect.x, rect.y, rect.width, rect.height, canvas_color(command->color));
                    break;
                }
                case TC_UI_DRAW_STROKE_RECT: {
                    const tc_ui_rect rect = physical_rect(command->rect, scale);
                    canvas_.draw_rect_outline(rect.x,
                                              rect.y,
                                              rect.width,
                                              rect.height,
                                              canvas_color(command->color),
                                              physical_length(command->thickness, scale));
                    break;
                }
                case TC_UI_DRAW_FILL_ROUNDED_RECT: {
                    const tc_ui_rect rect = physical_rect(command->rect, scale);
                    canvas_.draw_rect(rect.x,
                                      rect.y,
                                      rect.width,
                                      rect.height,
                                      canvas_color(command->color),
                                      physical_length(command->radius, scale));
                    break;
                }
                case TC_UI_DRAW_STROKE_ROUNDED_RECT: {
                    const tc_ui_rect rect = physical_rect(command->rect, scale);
                    canvas_.draw_rounded_rect_outline(tgfx::CanvasRoundedRectOutline{
                        rect.x,
                        rect.y,
                        rect.width,
                        rect.height,
                        physical_length(command->radius, scale),
                        canvas_color(command->color),
                        physical_length(command->thickness, scale),
                        command->segments > 0 ? command->segments : 6,
                    });
                    break;
                }
                case TC_UI_DRAW_FILL_CIRCLE: {
                    const tc_ui_point center = physical_point(command->p0, scale);
                    canvas_.draw_circle(center.x,
                                        center.y,
                                        physical_length(command->radius, scale),
                                        canvas_color(command->color),
                                        command->segments > 0 ? command->segments : 24);
                    break;
                }
                case TC_UI_DRAW_STROKE_CIRCLE: {
                    const tc_ui_point center = physical_point(command->p0, scale);
                    canvas_.draw_circle_outline(center.x,
                                                center.y,
                                                physical_length(command->radius, scale),
                                                canvas_color(command->color),
                                                physical_length(command->thickness, scale),
                                                command->segments > 0 ? command->segments : 24);
                    break;
                }
                case TC_UI_DRAW_ARC: {
                    const tc_ui_point center = physical_point(command->p0, scale);
                    canvas_.draw_arc(tgfx::CanvasArc{
                        {center.x, center.y},
                        physical_length(command->radius, scale),
                        command->start_radians,
                        command->end_radians,
                        canvas_color(command->color),
                        physical_length(command->thickness, scale),
                        command->segments,
                    });
                    break;
                }
                case TC_UI_DRAW_LINE: {
                    const tc_ui_point p0 = physical_point(command->p0, scale);
                    const tc_ui_point p1 = physical_point(command->p1, scale);
                    canvas_.draw_line(p0.x,
                                      p0.y,
                                      p1.x,
                                      p1.y,
                                      canvas_color(command->color),
                                      physical_length(command->thickness, scale));
                    break;
                }
                case TC_UI_DRAW_POLYLINE:
                    for (size_t point_index = 1; point_index < command->point_count; ++point_index) {
                        const tc_ui_point p0 = physical_point(command->points[point_index - 1], scale);
                        const tc_ui_point p1 = physical_point(command->points[point_index], scale);
                        canvas_.draw_line(p0.x,
                                          p0.y,
                                          p1.x,
                                          p1.y,
                                          canvas_color(command->color),
                                          physical_length(command->thickness, scale));
                    }
                    break;
                case TC_UI_DRAW_TEXTURE: {
                    const tc_ui_rect rect = physical_rect(command->rect, scale);
                    canvas_.draw_texture(tgfx::TextureHandle{command->texture_id},
                                         rect.x,
                                         rect.y,
                                         rect.width,
                                         rect.height,
                                         canvas_color(command->color),
                                         command->flip_v,
                                         texture_sampling(command->texture_sampling));
                    break;
                }
                case TC_UI_DRAW_ICON: {
                    const tc_ui_rect rect = physical_rect(command->rect, scale);
                    constexpr float kMaxIconExtent = 256.0f;
                    const float rounded_width = std::round(rect.width);
                    const float rounded_height = std::round(rect.height);
                    if (!std::isfinite(rounded_width) || !std::isfinite(rounded_height) || rounded_width < 1.0f ||
                        rounded_height < 1.0f || rounded_width > kMaxIconExtent || rounded_height > kMaxIconExtent) {
                        tc_log_error("[termin-gui-native] skipping invalid physical icon extent %.1fx%.1f",
                                     rounded_width,
                                     rounded_height);
                        break;
                    }
                    const uint32_t icon_width = static_cast<uint32_t>(rounded_width);
                    const uint32_t icon_height = static_cast<uint32_t>(rounded_height);
                    const tgfx::TextureHandle texture = sync_icon_texture(
                        context.device(), command->text ? command->text : "", icon_width, icon_height);
                    if (texture) {
                        canvas_.draw_texture(texture,
                                             rect.x,
                                             rect.y,
                                             rect.width,
                                             rect.height,
                                             canvas_color(command->color),
                                             false,
                                             tgfx::CanvasTextureSampling::Linear);
                    }
                    break;
                }
                case TC_UI_DRAW_PUSH_CLIP: {
                    const tc_ui_rect rect = physical_rect(command->rect, scale);
                    canvas_.begin_clip(rect.x, rect.y, rect.width, rect.height);
                    clip_depth += 1;
                    break;
                }
                case TC_UI_DRAW_POP_CLIP:
                    if (clip_depth == 0) {
                        tc_log_error("[termin-gui-native] ignoring unmatched UI pop-clip command");
                    } else {
                        canvas_.end_clip();
                        clip_depth -= 1;
                    }
                    break;
                case TC_UI_DRAW_TEXT:
                    if (canvas_.default_font()) {
                        // tc_ui draw commands use a baseline origin, while
                        // Canvas2DRenderer's left anchor uses the top of the font line.
                        // Keep that difference at this backend boundary instead of
                        // leaking canvas semantics into every widget's layout code.
                        const tc_ui_point baseline = physical_point(command->p0, scale);
                        const float physical_font_size = command->font_size * effective_font_scale;
                        const float line_top =
                            baseline.y - static_cast<float>(owned_font_->ascent_px(physical_font_size));
                        canvas_.draw_text(command->text ? command->text : "",
                                          baseline.x,
                                          line_top,
                                          physical_font_size,
                                          canvas_color(command->color));
                    } else if (!missing_font_logged_) {
                        tc_log_error("[termin-gui-native] skipping UI text commands "
                                     "because no default font is configured");
                        missing_font_logged_ = true;
                    }
                    break;
                case TC_UI_DRAW_CANVAS2D_LIST: {
                    const auto* nested = static_cast<const tgfx::DrawList2D*>(command->canvas2d_list);
                    if (!nested) {
                        tc_log_error("[termin-gui-native] UI DrawList2D command has null payload");
                        break;
                    }
                    UiDrawResources resources(canvas_.default_font());
                    tgfx::DrawList2DBuilder transformed_builder;
                    const bool transformed_ok = transformed_builder.push_transform(termin::Affine2f::scaling(scale)) &&
                                                transformed_builder.append(*nested) &&
                                                transformed_builder.pop_transform();
                    auto transformed = transformed_ok ? transformed_builder.freeze() : std::nullopt;
                    if (!transformed || !canvas_.execute(*transformed, resources)) {
                        tc_log_error("[termin-gui-native] failed to transform or execute nested "
                                     "DrawList2D");
                    }
                    break;
                }
                default:
                    tc_log_error("[termin-gui-native] unknown UI draw command type %d",
                                 static_cast<int>(command->type));
                    break;
                }
            }
            if (clip_depth != 0) {
                tc_log_error("[termin-gui-native] UI draw-list batch ended with %zu "
                             "unclosed clip command(s)",
                             clip_depth);
                while (clip_depth > 0) {
                    canvas_.end_clip();
                    --clip_depth;
                }
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
