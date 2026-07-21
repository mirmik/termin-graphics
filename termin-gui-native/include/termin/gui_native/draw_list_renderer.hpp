#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <termin/gui_native/tc_ui_document.h>

#include <tgfx2/canvas2d_renderer.hpp>

namespace termin::gui_native {

class ColorPicker;
struct ColorPickerSurface;

class UiDrawListRenderer {
private:
    struct ColorPickerSurfaceTexture {
        tgfx::TextureHandle texture;
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t revision = 0;
    };
    struct ColorPickerTextures {
        ColorPickerSurfaceTexture saturation_value;
        ColorPickerSurfaceTexture hue;
        ColorPickerSurfaceTexture alpha;
    };
    std::unique_ptr<tgfx::FontAtlas> owned_font_;
    tgfx::Canvas2DRenderer canvas_;
    std::unordered_map<ColorPicker*, ColorPickerTextures> color_picker_textures_;
    tgfx::IRenderDevice* color_picker_device_ = nullptr;
    bool missing_font_logged_ = false;

public:
    bool set_default_font_path(const std::string& path, int default_size_px = 14);
    void bind_text_measurer(tc_ui_document_handle document);
    // Upload generated picker surfaces to the active tgfx2 device and assign
    // their handles to the widget. Call before the document is painted.
    void sync_color_picker_surfaces(tgfx::RenderContext2& context, ColorPicker& picker);
    // Release GPU data before the corresponding widget is destroyed.
    void release_color_picker_surfaces(ColorPicker& picker);
    void render(tgfx::RenderContext2& context, const tc_ui_draw_list* draw_list, int width, int height);
    void release_gpu();

private:
    static bool measure_text(
        void* user_data,
        const char* text_utf8,
        size_t text_byte_length,
        float font_size,
        tc_ui_text_metrics* out_metrics
    );

    static void destroy_picker_surface_texture(
        tgfx::IRenderDevice* device,
        ColorPickerSurfaceTexture& surface
    );
    static void destroy_picker_textures(
        tgfx::IRenderDevice* device,
        ColorPickerTextures& textures
    );
    static bool sync_picker_surface(
        tgfx::IRenderDevice& device,
        const ColorPickerSurface& source,
        ColorPickerSurfaceTexture& target
    );
};

} // namespace termin::gui_native
