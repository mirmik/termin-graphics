#pragma once

#include <optional>

#include "tcplot/plot_frame2d.hpp"

namespace tcplot
{

    // Frontend-neutral navigation state for a retained 2D chart. Frontends pass
    // framebuffer-space pointer coordinates and apply returned ranges through
    // their chart facade. The controller owns drag state, not chart or scene
    // state.
    class TCPLOT_API ChartInteraction2D final
    {
    public:
        bool
        pointer_down(const PlotFrame2D& frame, float x, float y, int button);
        std::optional<PlotRange2D> pointer_move(float x, float y) const;
        bool pointer_up(int button);
        std::optional<PlotRange2D> wheel(const PlotFrame2D& frame,
                                         float x,
                                         float y,
                                         float steps,
                                         bool x_only) const;

        void cancel();
        bool dragging() const
        {
            return dragging_;
        }

    private:
        bool dragging_ = false;
        int drag_button_ = -1;
        float drag_start_x_ = 0.0f;
        float drag_start_y_ = 0.0f;
        PlotFrame2D drag_frame_{};
    };

} // namespace tcplot
