// font_atlas.cpp - stb_truetype-backed implementation of tgfx2::FontAtlas.
//
// Layout parity with the prior Python implementation (tgfx/font.py):
//   - Cell size: (ink_width, line_height) where ink_width = x1 - x0
//     from stbtt_GetCodepointBitmapBox and line_height = ascent_px +
//     descent_px (no line_gap).
//   - Glyph bitmap is placed at (0, ascent_px + y0) within its cell,
//     where y0 is the stb bitmap top offset (typically negative for
//     chars with ascenders). Baseline sits at cell y = ascent_px,
//     identical for every glyph — this is what keeps vertical text
//     alignment consistent.
//   - ink_width = x1 - x0 is used as measurement width, not the
//     typographic advance. Matches what Python did; see header note.

#include "tgfx2/font_atlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

extern "C" {
#include <tcbase/tc_log.h>
}

#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"

// stb_truetype - single-header; define the implementation in this TU
// only. The header itself lives at termin-graphics/third/stb/.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace tgfx2 {

namespace {

// Same ~140 preload characters as tgfx/font.py. Keeping the set
// identical means "first-frame glyph misses" of any Python code path
// translate 1:1 to the C++ port — no surprise rasterisations on the
// first real frame.
static const char* kPreloadUtf8 =
    // ASCII printable (U+0020..U+007E)
    " !\"#$%&'()*+,-./0123456789:;<=>?@"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
    "abcdefghijklmnopqrstuvwxyz{|}~"
    // Symbols from Python preload.
    "\xE2\x96\xB6\xE2\x96\xBC\xE2\x96\xB2\xE2\x97\x80"  // ▶▼▲◀
    "\xE2\x96\xA0\xE2\x96\xA1\xE2\x96\xA3\xE2\x96\xAB"  // ■□▣▫
    "\xE2\x97\x8B\xE2\x97\x8F\xE2\x97\x88\xE2\x97\x86"  // ○●◈◆
    "\xE2\x80\xA2\xE2\x80\xA3\xE2\x97\xA6"              // •‣◦
    "\xE2\x9C\x93\xE2\x9C\x97\xE2\x9C\x95"              // ✓✗✕
    "\xE2\x96\xB7\xE2\x96\xBD\xE2\x97\x81\xE2\x96\xB3"  // ▷▽◁△
    "\xE2\x86\x90\xE2\x86\x92\xE2\x86\x91\xE2\x86\x93"  // ←→↑↓
    "\xE2\x8C\x82";                                     // ⌂

// Small, liberal UTF-8 → uint32 decoder. Returns the codepoint and
// advances `i` past the sequence. On malformed input emits U+FFFD and
// advances one byte so the caller always makes progress.
uint32_t utf8_decode(const std::string_view& s, size_t& i) {
    if (i >= s.size()) return 0;
    unsigned char b0 = static_cast<unsigned char>(s[i]);

    auto take_cont = [&](size_t off) -> int {
        if (i + off >= s.size()) return -1;
        unsigned char b = static_cast<unsigned char>(s[i + off]);
        if ((b & 0xC0) != 0x80) return -1;
        return b & 0x3F;
    };

    if (b0 < 0x80) {
        i += 1;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0) {
        int b1 = take_cont(1);
        if (b1 < 0) { i += 1; return 0xFFFD; }
        i += 2;
        return (static_cast<uint32_t>(b0 & 0x1F) << 6) | static_cast<uint32_t>(b1);
    }
    if ((b0 & 0xF0) == 0xE0) {
        int b1 = take_cont(1);
        int b2 = take_cont(2);
        if (b1 < 0 || b2 < 0) { i += 1; return 0xFFFD; }
        i += 3;
        return (static_cast<uint32_t>(b0 & 0x0F) << 12) |
               (static_cast<uint32_t>(b1) << 6) |
               static_cast<uint32_t>(b2);
    }
    if ((b0 & 0xF8) == 0xF0) {
        int b1 = take_cont(1);
        int b2 = take_cont(2);
        int b3 = take_cont(3);
        if (b1 < 0 || b2 < 0 || b3 < 0) { i += 1; return 0xFFFD; }
        i += 4;
        return (static_cast<uint32_t>(b0 & 0x07) << 18) |
               (static_cast<uint32_t>(b1) << 12) |
               (static_cast<uint32_t>(b2) << 6) |
               static_cast<uint32_t>(b3);
    }
    i += 1;
    return 0xFFFD;
}

std::vector<uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        throw std::runtime_error("FontAtlas: cannot open " + path);
    }
    auto size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size > 0 && !f.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("FontAtlas: failed to read " + path);
    }
    return bytes;
}

}  // namespace

// -- Impl: owns the stb_truetype state so we can keep the stb header
// out of the public surface.
struct FontAtlas::Impl {
    std::vector<uint8_t> ttf_bytes;
    stbtt_fontinfo font{};
    float scale = 1.0f;
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

FontAtlas::FontAtlas(const std::string& ttf_path,
                     int rasterize_size_px,
                     int atlas_width,
                     int atlas_height)
    : rasterize_size_(rasterize_size_px),
      atlas_w_(atlas_width),
      atlas_h_(atlas_height),
      atlas_(static_cast<size_t>(atlas_width) * static_cast<size_t>(atlas_height), 0u) {
    impl_ = new Impl();
    impl_->ttf_bytes = read_file_bytes(ttf_path);

    int offset = stbtt_GetFontOffsetForIndex(impl_->ttf_bytes.data(), 0);
    if (offset < 0) {
        delete impl_;
        impl_ = nullptr;
        throw std::runtime_error("FontAtlas: no font at index 0 in " + ttf_path);
    }
    if (!stbtt_InitFont(&impl_->font, impl_->ttf_bytes.data(), offset)) {
        delete impl_;
        impl_ = nullptr;
        throw std::runtime_error("FontAtlas: stbtt_InitFont failed for " + ttf_path);
    }

    impl_->scale = stbtt_ScaleForPixelHeight(&impl_->font,
                                             static_cast<float>(rasterize_size_));

    int ascent_u = 0, descent_u = 0, line_gap_u = 0;
    stbtt_GetFontVMetrics(&impl_->font, &ascent_u, &descent_u, &line_gap_u);
    // Match Python: line_height = ascent + descent (descent_u is
    // negative in stb's convention; make its magnitude positive).
    ascent_px_ = static_cast<int>(std::round(ascent_u * impl_->scale));
    descent_px_ = static_cast<int>(std::round(-descent_u * impl_->scale));
    line_height_ = ascent_px_ + descent_px_;

    // Warm up the default glyph set so the first real frame doesn't
    // stall on rasterising "Hello 123".
    size_t i = 0;
    std::string_view preload{kPreloadUtf8};
    while (i < preload.size()) {
        uint32_t cp = utf8_decode(preload, i);
        ensure_glyph(cp);
    }
    // Mark dirty so the first ensure_texture() uploads the preload.
    dirty_ = true;
}

FontAtlas::~FontAtlas() {
    // Best-effort GPU release — destructor may fire after the GL
    // context is already gone. release_gpu() is a no-op if gpu_device_
    // is null, and we never touch GL directly here.
    release_gpu();
    delete impl_;
    impl_ = nullptr;
}

// ---------------------------------------------------------------------------
// Glyph rasterisation
// ---------------------------------------------------------------------------

FontAtlas::PackedCell FontAtlas::pack_(int cell_w, int cell_h) {
    if (shelf_x_ + cell_w > atlas_w_) {
        shelf_y_ += shelf_h_;
        shelf_x_ = 0;
        shelf_h_ = 0;
    }
    if (shelf_y_ + cell_h > atlas_h_) {
        return {-1, -1};
    }
    PackedCell out{shelf_x_, shelf_y_};
    shelf_x_ += cell_w;
    shelf_h_ = std::max(shelf_h_, cell_h);
    return out;
}

bool FontAtlas::ensure_glyph(uint32_t codepoint) {
    if (!impl_) return false;
    if (glyphs_.count(codepoint) != 0) return false;

    int glyph_idx = stbtt_FindGlyphIndex(&impl_->font, static_cast<int>(codepoint));
    if (glyph_idx == 0) {
        // Font does not define this codepoint. Don't register a
        // fallback — matches Python behaviour of dropping unknown
        // chars silently at render time.
        return false;
    }

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&impl_->font, glyph_idx,
                            impl_->scale, impl_->scale,
                            &x0, &y0, &x1, &y1);

    int gw = x1 - x0;
    int gh = line_height_;  // Python: constant line_height for every cell.

    if (gw <= 0) {
        // Zero-advance glyph (combining marks, some whitespace) —
        // register with empty UVs so measure_text sees 0 width.
        GlyphInfo g{};
        g.width_px = 0.0f;
        g.height_px = static_cast<float>(gh);
        glyphs_[codepoint] = g;
        return true;
    }

    // Pad by 2 px between cells so bilinear filtering of adjacent
    // glyphs doesn't bleed. Matches Python's _PADDING = 2.
    constexpr int kPadding = 2;
    int cell_w = gw + kPadding;
    int cell_h = gh + kPadding;

    auto cell = pack_(cell_w, cell_h);
    if (cell.x < 0) {
        tc_log(TC_LOG_WARN, "[FontAtlas] Atlas full — cannot rasterise U+%04X", codepoint);
        return false;
    }

    // Rasterise directly into the atlas. Glyph bitmap is `gw * gh`
    // bytes; we place it at column 0 of the cell (Python equivalent of
    // draw text at (-bbox[0], 0)) and row (ascent + y0) so the
    // baseline is at cell row = ascent_px.
    const int dst_row0 = cell.y + ascent_px_ + y0;
    // Guard against glyphs whose y0 would push them above the cell
    // (extremely tall ascenders). Clip at the top; stb_truetype does
    // the same clipping internally but being defensive doesn't hurt.
    const int gbh = y1 - y0;
    const int clip_top = std::max(0, -(dst_row0 - cell.y));
    const int dst_row0_clamped = dst_row0 + clip_top;
    const int rows_to_write = std::max(0, std::min(gbh - clip_top,
                                                   (cell.y + gh) - dst_row0_clamped));

    if (rows_to_write > 0) {
        uint8_t* dst = atlas_.data()
                       + static_cast<size_t>(dst_row0_clamped) * atlas_w_
                       + cell.x;
        stbtt_MakeGlyphBitmap(&impl_->font,
                              dst,
                              gw, rows_to_write,
                              atlas_w_,  // dst stride
                              impl_->scale, impl_->scale,
                              glyph_idx);
        // stb renders starting from the top of the glyph bitmap, so if
        // we clipped top rows we need to request the un-clipped render
        // into a scratch buffer. For the clip_top==0 common case the
        // above call is correct and cheap.
        if (clip_top > 0) {
            // Re-render into scratch and memcpy the surviving rows.
            std::vector<uint8_t> scratch(static_cast<size_t>(gw) * gbh, 0);
            stbtt_MakeGlyphBitmap(&impl_->font,
                                  scratch.data(),
                                  gw, gbh, gw,
                                  impl_->scale, impl_->scale,
                                  glyph_idx);
            uint8_t* dst2 = atlas_.data()
                            + static_cast<size_t>(dst_row0_clamped) * atlas_w_
                            + cell.x;
            for (int r = 0; r < rows_to_write; ++r) {
                std::memcpy(dst2 + static_cast<size_t>(r) * atlas_w_,
                            scratch.data() + static_cast<size_t>(r + clip_top) * gw,
                            static_cast<size_t>(gw));
            }
        }
    }

    GlyphInfo g{};
    g.u0 = static_cast<float>(cell.x) / static_cast<float>(atlas_w_);
    g.v0 = static_cast<float>(cell.y) / static_cast<float>(atlas_h_);
    g.u1 = static_cast<float>(cell.x + gw) / static_cast<float>(atlas_w_);
    g.v1 = static_cast<float>(cell.y + gh) / static_cast<float>(atlas_h_);
    g.width_px = static_cast<float>(gw);
    g.height_px = static_cast<float>(gh);
    glyphs_[codepoint] = g;

    dirty_ = true;
    return true;
}

void FontAtlas::ensure_glyphs(std::string_view text_utf8, RenderContext2* ctx) {
    bool added_any = false;
    size_t i = 0;
    while (i < text_utf8.size()) {
        uint32_t cp = utf8_decode(text_utf8, i);
        if (ensure_glyph(cp)) {
            added_any = true;
        }
    }
    if (added_any && ctx != nullptr) {
        sync_gpu_(ctx);
    }
}

// ---------------------------------------------------------------------------
// Measurement
// ---------------------------------------------------------------------------

FontAtlas::Size2f FontAtlas::measure_text(std::string_view text_utf8,
                                          float font_size) const {
    Size2f out{};
    if (text_utf8.empty()) return out;

    const float scale = font_size / static_cast<float>(rasterize_size_);
    size_t i = 0;
    float width = 0.0f;
    while (i < text_utf8.size()) {
        uint32_t cp = utf8_decode(text_utf8, i);
        auto it = glyphs_.find(cp);
        if (it != glyphs_.end()) {
            width += it->second.width_px * scale;
        }
    }
    out.width = width;
    out.height = font_size;
    return out;
}

const FontAtlas::GlyphInfo* FontAtlas::get_glyph(uint32_t codepoint) const {
    auto it = glyphs_.find(codepoint);
    return (it != glyphs_.end()) ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// GPU upload
// ---------------------------------------------------------------------------

TextureHandle FontAtlas::ensure_texture(RenderContext2* ctx) {
    if (!ctx) return TextureHandle{};

    // Holder identity change → drop old handle.
    if (gpu_owner_ != nullptr && gpu_owner_ != ctx) {
        release_gpu();
    }

    if (gpu_texture_.id == 0) {
        IRenderDevice& device = ctx->device();
        TextureDesc desc{};
        desc.width = static_cast<uint32_t>(atlas_w_);
        desc.height = static_cast<uint32_t>(atlas_h_);
        desc.mip_levels = 1;
        desc.sample_count = 1;
        desc.format = PixelFormat::R8_UNorm;
        desc.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
        gpu_texture_ = device.create_texture(desc);
        gpu_owner_ = ctx;
        gpu_device_ = &device;

        device.upload_texture(
            gpu_texture_,
            std::span<const uint8_t>(atlas_.data(), atlas_.size()));
        dirty_ = false;
    } else if (dirty_) {
        sync_gpu_(ctx);
    }
    return gpu_texture_;
}

void FontAtlas::sync_gpu_(RenderContext2* ctx) {
    if (!ctx || gpu_texture_.id == 0) return;
    IRenderDevice& device = ctx->device();
    device.upload_texture(
        gpu_texture_,
        std::span<const uint8_t>(atlas_.data(), atlas_.size()));
    dirty_ = false;
}

void FontAtlas::release_gpu() {
    if (gpu_device_ != nullptr && gpu_texture_.id != 0) {
        gpu_device_->destroy(gpu_texture_);
    }
    gpu_texture_ = TextureHandle{};
    gpu_owner_ = nullptr;
    gpu_device_ = nullptr;
    // CPU atlas + glyphs stay — the caller may re-ensure on a new
    // context without paying the rasterisation cost again.
    dirty_ = true;
}

}  // namespace tgfx2
