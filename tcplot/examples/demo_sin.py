"""Basic sine/cosine plot demo, restored on the native GUI bridge."""

from _gallery import sine_plot
from _host import run_demo


make_plot = sine_plot


if __name__ == "__main__":
    run_demo("tcplot — Sine Demo", make_plot)
