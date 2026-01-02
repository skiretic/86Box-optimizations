#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path

def parse_ratios(text):
    ratios = {}
    capture = False
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("comparison (primary/baseline)"):
            capture = True
            continue
        if not capture or not stripped:
            continue
        match = re.match(r"^(\S+)\s*:\s*([0-9]+\.?[0-9]*)$", stripped)
        if match:
            ratios[match.group(1)] = float(match.group(2))
    return ratios


def main():
    parser = argparse.ArgumentParser(description="Parse mmx_neon_micro benchmark logs.")
    parser.add_argument("log", type=Path, help="Path to the benchmark log")
    parser.add_argument("--output", type=Path, default=Path("mmx_neon_micro.json"))
    parser.add_argument("--min-ratio", type=float, default=0.0,
                        help="Fail if any ratio falls below this (default: no check)")
    args = parser.parse_args()

    if not args.log.exists():
        raise SystemExit(f"Missing log: {args.log}")

    text = args.log.read_text()
    ratios = parse_ratios(text)
    if not ratios:
        raise SystemExit("No comparison section found in log")

    payload = {
        "source": str(args.log),
        "ratios": ratios
    }

    args.output.write_text(json.dumps(payload, indent=2))
    print(f"Parsed {len(ratios)} ratios from {args.log} -> {args.output}")

    if args.min_ratio > 0.0:
        below = [name for name, value in ratios.items() if value < args.min_ratio]
        if below:
            print(f"Warning: ratios below {args.min_ratio}: {', '.join(below)}")
            raise SystemExit(1)


if __name__ == "__main__":
    main()
