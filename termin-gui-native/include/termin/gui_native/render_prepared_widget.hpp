#pragma once

namespace tgfx {
    class FontAtlas;
    class RenderContext2;
}

namespace termin::gui_native {

    // Optional rendering-side contract for native widgets that produce GPU
    // resources consumed by their ordinary paint() implementation. The native
    // document painter discovers these widgets after layout and invokes them
    // before the UI render pass is opened, so implementations may safely run
    // offscreen render passes in the canonical graphics domain.
    class RenderPreparedWidget {
    public:
        virtual ~RenderPreparedWidget() = default;

        virtual void prepare_render(tgfx::RenderContext2& context,
                                    tgfx::FontAtlas& default_font,
                                    float density_scale) = 0;
        virtual void release_render_resources() = 0;
    };

} // namespace termin::gui_native
