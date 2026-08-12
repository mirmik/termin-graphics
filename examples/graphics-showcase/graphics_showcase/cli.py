from __future__ import annotations

import argparse
import logging
from pathlib import Path
import sys

from .model import ShowcaseConfig
from .runner import run_showcase


def _arguments(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--headless",
        action="store_true",
        help="run the required deterministic offscreen showcase",
    )
    mode.add_argument(
        "--windowed",
        action="store_true",
        help="open the optional SDL-backed integration showcase",
    )
    parser.add_argument("--output", type=Path, help="headless integration PNG path")
    parser.add_argument("--report", type=Path, help="headless JSON report path")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=820)
    parser.add_argument(
        "--frames",
        type=int,
        default=0,
        help="windowed mode: exit after this many rendered frames",
    )
    parser.add_argument(
        "--seconds",
        type=float,
        default=0.0,
        help="windowed mode: exit after this many seconds",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _arguments(argv)
    logging.basicConfig(
        level=logging.INFO,
        format="%(levelname)s %(name)s: %(message)s",
    )
    if args.windowed:
        from .windowed import run_windowed_showcase

        try:
            return run_windowed_showcase(
                width=args.width,
                height=args.height,
                frame_limit=args.frames,
                second_limit=args.seconds,
            )
        except Exception:
            logging.getLogger("graphics_showcase").exception(
                "windowed graphics showcase failed"
            )
            return 1
    if args.output is None or args.report is None:
        print("--headless requires --output and --report", file=sys.stderr)
        return 2
    try:
        report = run_showcase(
            ShowcaseConfig(
                width=args.width,
                height=args.height,
                output_path=args.output.resolve(),
                report_path=args.report.resolve(),
            )
        )
    except Exception:
        logging.getLogger("graphics_showcase").exception("graphics showcase failed")
        return 1
    print(
        f"graphics showcase passed {len(report['sections'])} sections; "
        f"artifact={report['artifact']}"
    )
    return 0
