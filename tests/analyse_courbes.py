#!/usr/bin/env python3
"""Native protocol-group analysis; same CLI and JSON schema as analyse_groupes."""
from analysis.cli import run_group

if __name__ == "__main__":
    raise SystemExit(run_group("courbes"))
