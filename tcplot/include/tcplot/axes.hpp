// axes.hpp - Axis tick placement and value formatting for tcplot.
//
// Port of tcplot/tcplot/axes.py. Used by both 2D and 3D engines.
#pragma once

#include <string>
#include <vector>

#include "tcplot/tcplot_api.h"

namespace tcplot {
namespace axes {

// Generate human-friendly tick positions for the range [lo, hi].
// Returns tick values aligned to "nice" numbers (1, 2, 5 * 10^n).
// If hi <= lo the result is {lo}.
TCPLOT_API std::vector<double> nice_ticks(double lo, double hi, int max_ticks = 10);

// Format a tick value for display. Uses scientific notation for very
// large or very small magnitudes and a "%g"-style representation
// otherwise. Trailing zeros are trimmed.
TCPLOT_API std::string format_tick(double value);

}  // namespace axes
}  // namespace tcplot
