"""Polynomial and damped-oscillation plots side by side."""

from _gallery import multi_plot
from _host import run_demo


make_row = multi_plot


if __name__ == "__main__":
    run_demo("tcplot — Multi Plot", make_row, size=(1200, 700))
