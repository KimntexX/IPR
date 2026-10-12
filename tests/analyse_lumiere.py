#!/usr/bin/env python3
"""Native Lumières protocol entry point; old analyse_lumieres stays historical."""
from analysis.cli import run_group

if __name__ == "__main__":
    raise SystemExit(run_group("lumiere"))
