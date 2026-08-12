"""Double helix plus deterministic 3D scatter."""

from _gallery import helix_plot
from _host import run_demo


make_plot = helix_plot


if __name__ == "__main__":
    run_demo("tcplot — 3D Helix", make_plot)
