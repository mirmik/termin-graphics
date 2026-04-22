// font_atlas.cpp - stb_truetype-backed implementation of tgfx::FontAtlas.
//
// Size-aware layout: every glyph is baked per requested display size
// and cached under key (codepoint, px_size). See header for the
// architectural rationale (same GlyphInfo shape works for a future
// SDF backend — caller always asks "glyph at display_px", atlas
// produces display-px metrics however it wants internally).
//
// Per-size bake produces its own cell height (ascent + descent at
// that size) so baseline alignment is consistent within one size.
// Mixing sizes in one draw call is fine — each glyph's quad uses its
// own `height_px` and advance, and the renderer positions them by the
// caller-provided baseline.
//
// Horizontal oversampling (×2) + stb's box prefilter carried over
// from the previous design; see kOversampleX comment in ensure_glyph.

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
#include <tc_profiler.h>
}

#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"

#include "internal/utf8_decode.hpp"

extern "C" {
#include "tgfx/tgfx2_interop.h"
}

// stb_truetype - single-header; define the implementation in this TU
// only. The header itself lives at termin-graphics/third/stb/.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace tgfx {

namespace {

// Same ~140 preload characters as tgfx/font.py. Keeping the set
// identical means "first-frame glyph misses" of any Python code path
// translate 1:1 to the C++ port — no surprise rasterisations on the
// first real frame. Preload only runs at `default_preload_size_` —
// other sizes populate lazily on first use.
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
    // Font vmetrics in font-design units. We stash them here so each
    // per-size metrics resolution is a cheap multiply without touching
    // stb again.
    int ascent_u = 0;
    int descent_u = 0;  // stb convention: negative
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

int FontAtlas::quantise_size_(float display_px) {
    // Round to nearest integer px, clamp to the minimum. No bucketing
    // beyond that — start simple, tighten later if atlas pressure
    // forces it. See header kMinPxSize.
    int px = static_cast<int>(std::lround(display_px));
    if (px < kMinPxSize) px = kMinPxSize;
    return px;
}

const FontAtlas::SizeMetrics& FontAtlas::metrics_for_(int px_size) const {
    auto it = size_metrics_.find(px_size);
    if (it != size_metrics_.end()) return it->second;

    SizeMetrics sm{};
    sm.scale = stbtt_ScaleForPixelHeight(&impl_->font,
                                         static_cast<float>(px_size));
    // Match Python: line_height = ascent + descent (descent is stored
    // negative in stb's convention; take its magnitude).
    sm.ascent_px = static_cast<int>(std::round(impl_->ascent_u * sm.scale));
    sm.descent_px = static_cast<int>(std::round(-impl_->descent_u * sm.scale));
    sm.line_height = sm.ascent_px + sm.descent_px;
    return size_metrics_.emplace(px_size, sm).first->second;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

FontAtlas::FontAtlas(const std::string& ttf_path,
                     int default_preload_size_px,
                     int atlas_width,
                     int atlas_height)
    : default_preload_size_(default_preload_size_px),
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

    // Stash vmetrics in font-design units; every per-size resolution
    // scales from here.
    int line_gap_u = 0;
    stbtt_GetFontVMetrics(&impl_->font,
                          &impl_->ascent_u,
                          &impl_->descent_u,
                          &line_gap_u);

    // Warm up the preload character set at the default size so the
    // first real frame of "typical" UI text doesn't stall on
    // rasterisation. Other sizes populate lazily.
    const float preload_px = static_cast<float>(default_preload_size_);
    size_t i = 0;
    std::string_view preload{kPreloadUtf8};
    while (i < preload.size()) {
        uint32_t cp = internal::utf8_decode(preload, i);
        ensure_glyph(cp, preload_px);
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

bool FontAtlas::ensure_glyph(uint32_t codepoint, float display_px) {
    if (!impl_) return false;
    const int px_size = quantise_size_(display_px);
    const uint64_t key = make_key_(codepoint, px_size);
    if (glyphs_.count(key) != 0) return false;

    // Horizontal oversampling: rasterise the glyph at 2× its display
    // horizontal resolution and apply stb's box prefilter. At draw
    // time the bilinear sampler interpolates between these 2× texels,
    // which gives smoother transitions when the quad ends up at a
    // non-integer display position. ImGui default. Vertical stays at
    // 1× — doubling the atlas vertically is rarely worth it.
    //
    // Side-effect: stb's box prefilter phase-shifts the apparent
    // glyph position by -(K-1)/(2K) = -0.25 display px (for K=2). We
    // don't compensate — it's a uniform shift across every glyph, so
    // relative alignment inside a text run is preserved, and 0.25 px
    // is below perceptual threshold.
    constexpr int kOversampleX = 2;

    int glyph_idx = stbtt_FindGlyphIndex(&impl_->font, static_cast<int>(codepoint));
    if (glyph_idx == 0) {
        // Font does not define this codepoint. Don't register a
        // fallback — matches Python behaviour of dropping unknown
        // chars silently at render time.
        return false;
    }

    const SizeMetrics& sm = metrics_for_(px_size);
    const int gh_cell = sm.line_height;

    // Bitmap box at (oversampled_x, normal_y) scale. stb returns the
    // rendered-pixel bbox — x1 - x0 is in oversampled texels, y1 - y0
    // in display pixels.
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&impl_->font, glyph_idx,
                            sm.scale * kOversampleX,
                            sm.scale,
                            &x0, &y0, &x1, &y1);

    const int gw_over = x1 - x0;

    // Horizontal advance — correct metric for cursor motion. Differs
    // from the ink-bbox width (that's just how much of the glyph is
    // painted). Space glyphs have ink width == 0 but a real advance.
    int adv_w = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&impl_->font, glyph_idx, &adv_w, &lsb);
    const float advance_px = adv_w * sm.scale;

    if (gw_over <= 0) {
        // No ink but still occupies layout space (U+0020 and similar).
        // Register with real advance so cursor moves; UVs stay zero.
        GlyphInfo g{};
        g.width_px = 0.0f;
        g.height_px = static_cast<float>(gh_cell);
        g.advance_px = advance_px;
        glyphs_[key] = g;
        return true;
    }

    // Stored width in atlas texels = rendered width + (K-1) skirt
    // columns. The box prefilter is a trailing running average of
    // kernel K; it extends the signal by K-1 zero-initialised columns
    // on the right, which we leave in the atlas to preserve the
    // filter's reach under bilinear sampling.
    constexpr int kPadding = 2;
    const int stored_w = gw_over + (kOversampleX - 1);
    const int cell_w = stored_w + kPadding;
    const int cell_h = gh_cell + kPadding;

    auto cell = pack_(cell_w, cell_h);
    if (cell.x < 0) {
        tc_log(TC_LOG_WARN,
               "[FontAtlas] Atlas full — cannot rasterise U+%04X at %d px",
               codepoint, px_size);
        return false;
    }

    // Place bitmap at column 0 of the cell and row (ascent + y0) so
    // the baseline sits at cell row = sm.ascent_px. Identical for every
    // glyph at this size → baseline alignment stays consistent.
    const int dst_row0 = cell.y + sm.ascent_px + y0;
    // Guard against glyphs whose y0 would push them above the cell
    // (extremely tall ascenders). Clip at the top; stb_truetype does
    // the same clipping internally but being defensive doesn't hurt.
    const int gbh = y1 - y0;
    const int clip_top = std::max(0, -(dst_row0 - cell.y));
    const int dst_row0_clamped = dst_row0 + clip_top;
    const int rows_to_write = std::max(0, std::min(gbh - clip_top,
                                                   (cell.y + gh_cell) - dst_row0_clamped));

    if (rows_to_write > 0) {
        uint8_t* dst = atlas_.data()
                       + static_cast<size_t>(dst_row0_clamped) * atlas_w_
                       + cell.x;
        if (clip_top == 0) {
            // Fast path: render oversampled glyph straight into atlas.
            stbtt_MakeGlyphBitmapSubpixel(&impl_->font,
                                          dst,
                                          gw_over, rows_to_write,
                                          atlas_w_,  // dst stride
                                          sm.scale * kOversampleX,
                                          sm.scale,
                                          0.0f, 0.0f,
                                          glyph_idx);
        } else {
            // Clipped top: render un-clipped into scratch so stb sees
            // the full bitmap, then memcpy surviving rows. Skirt stays
            // zero — prefilter below populates it.
            std::vector<uint8_t> scratch(static_cast<size_t>(gw_over) * gbh, 0);
            stbtt_MakeGlyphBitmapSubpixel(&impl_->font,
                                          scratch.data(),
                                          gw_over, gbh, gw_over,
                                          sm.scale * kOversampleX,
                                          sm.scale,
                                          0.0f, 0.0f,
                                          glyph_idx);
            for (int r = 0; r < rows_to_write; ++r) {
                std::memcpy(dst + static_cast<size_t>(r) * atlas_w_,
                            scratch.data() + static_cast<size_t>(r + clip_top) * gw_over,
                            static_cast<size_t>(gw_over));
            }
        }

        // Horizontal box prefilter across the full stored width
        // (including the zero-initialised skirt). Accessible here
        // because stb_truetype's implementation is compiled into
        // this TU via STB_TRUETYPE_IMPLEMENTATION; the symbol is
        // static but visible within the TU.
        if (kOversampleX > 1) {
            stbtt__h_prefilter(dst,
                               stored_w, rows_to_write,
                               atlas_w_,
                               kOversampleX);
        }
    }

    GlyphInfo g{};
    g.u0 = static_cast<float>(cell.x) / static_cast<float>(atlas_w_);
    g.v0 = static_cast<float>(cell.y) / static_cast<float>(atlas_h_);
    g.u1 = static_cast<float>(cell.x + stored_w) / static_cast<float>(atlas_w_);
    g.v1 = static_cast<float>(cell.y + gh_cell) / static_cast<float>(atlas_h_);
    // Display width = stored texels / oversample. Fractional for odd
    // stored_w, fine — Text2DRenderer snaps the quad's left edge to a
    // pixel and the right edge floats, which keeps glyph shape intact.
    g.width_px = static_cast<float>(stored_w) / static_cast<float>(kOversampleX);
    g.height_px = static_cast<float>(gh_cell);
    g.advance_px = advance_px;
    glyphs_[key] = g;

    dirty_ = true;
    return true;
}

void FontAtlas::ensure_glyphs(std::string_view text_utf8,
                              float display_px,
                              RenderContext2* ctx) {
    bool added_any = false;
    size_t i = 0;
    while (i < text_utf8.size()) {
        uint32_t cp = internal::utf8_decode(text_utf8, i);
        if (ensure_glyph(cp, display_px)) {
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
                                          float display_px) const {
    Size2f out{};
    if (text_utf8.empty()) return out;

    const int px_size = quantise_size_(display_px);
    size_t i = 0;
    float width = 0.0f;
    while (i < text_utf8.size()) {
        uint32_t cp = internal::utf8_decode(text_utf8, i);
        auto it = glyphs_.find(make_key_(cp, px_size));
        if (it != glyphs_.end()) {
            // Advance is already in display pixels at this size.
            width += it->second.advance_px;
        }
    }
    out.width = width;
    // Return the requested display size — measurement row height is
    // the caller-side concept (line_height_px is a separate getter).
    out.height = display_px;
    return out;
}

const FontAtlas::GlyphInfo* FontAtlas::get_glyph(uint32_t codepoint,
                                                 float display_px) const {
    const int px_size = quantise_size_(display_px);
    auto it = glyphs_.find(make_key_(codepoint, px_size));
    return (it != glyphs_.end()) ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Size-aware metrics
// ---------------------------------------------------------------------------

int FontAtlas::ascent_px(float display_px) const {
    return metrics_for_(quantise_size_(display_px)).ascent_px;
}

int FontAtlas::descent_px(float display_px) const {
    return metrics_for_(quantise_size_(display_px)).descent_px;
}

int FontAtlas::line_height_px(float display_px) const {
    return metrics_for_(quantise_size_(display_px)).line_height;
}

// ---------------------------------------------------------------------------
// GPU upload
// ---------------------------------------------------------------------------

TextureHandle FontAtlas::ensure_texture(RenderContext2* ctx) {
    if (!ctx) return TextureHandle{};

    const bool profile = tc_profiler_enabled();

    IRenderDevice& device = ctx->device();

    // Ownership is keyed on the IRenderDevice, not the RenderContext2.
    // Every tgfx2 RenderContext2 that wraps the same process-global
    // IRenderDevice shares handle pools, so a TextureHandle minted by
    // one context resolves on the other without re-upload. Keying on
    // RenderContext2* caused editor setups with an in-scene UIRenderer
    // and a parent editor UIRenderer (both on one device, different
    // RenderContext2 wrappers) to release+recreate the atlas every
    // single frame — an expensive round-trip for an R8 texture and the
    // dominant cost inside UIWidgetPass on OpenGL. Only real device
    // swaps (hot-reload, multi-device hosts if we ever support those)
    // actually need to drop the GPU texture.
    if (gpu_device_ != nullptr && gpu_device_ != &device) {
        if (profile) tc_profiler_begin_section("atlas.device_swap");
        release_gpu();
        if (profile) tc_profiler_end_section();
    }

    // Track the last context for diagnostics / legacy callers; ownership
    // is still the device above.
    gpu_owner_ = ctx;

    if (gpu_texture_.id == 0) {
        if (profile) tc_profiler_begin_section("atlas.create+upload");
        TextureDesc desc{};
        desc.width = static_cast<uint32_t>(atlas_w_);
        desc.height = static_cast<uint32_t>(atlas_h_);
        desc.mip_levels = 1;
        desc.sample_count = 1;
        desc.format = PixelFormat::R8_UNorm;
        desc.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
        gpu_texture_ = device.create_texture(desc);
        gpu_device_ = &device;

        device.upload_texture(
            gpu_texture_,
            std::span<const uint8_t>(atlas_.data(), atlas_.size()));
        dirty_ = false;
        if (profile) tc_profiler_end_section();
    } else if (dirty_) {
        if (profile) tc_profiler_begin_section("atlas.reupload");
        sync_gpu_(ctx);
        if (profile) tc_profiler_end_section();
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
    // Same lifetime story as Text2DRenderer::release_gpu: on Python
    // interpreter shutdown BackendWindow may tear down the device
    // before we get here, leaving `gpu_device_` dangling. Only destroy
    // when the pointer is still the live interop target.
    if (gpu_device_ != nullptr && gpu_texture_.id != 0) {
        void* live = tgfx2_interop_get_device();
        if (live == gpu_device_) {
            gpu_device_->destroy(gpu_texture_);
        }
    }
    gpu_texture_ = TextureHandle{};
    gpu_owner_ = nullptr;
    gpu_device_ = nullptr;
    // CPU atlas + glyphs stay — the caller may re-ensure on a new
    // context without paying the rasterisation cost again.
    dirty_ = true;
}

}  // namespace tgfx
