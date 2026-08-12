"""Interactive retained marker with nearest-sample snapping and close action."""

from _gallery import marker_plot
from _host import run_demo


make_plot = marker_plot


if __name__ == "__main__":
    run_demo("tcplot — Retained Marker Demo", make_plot)
