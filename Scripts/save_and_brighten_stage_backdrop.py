"""Save backdrop transform + brighten — EDITOR ONLY."""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import brighten_stage_backdrop  # noqa: E402
import godfrey_exhibit_guard  # noqa: E402
import save_stage_backdrop_transform  # noqa: E402


def main() -> None:
    godfrey_exhibit_guard.reject_headless_spawn()
    save_stage_backdrop_transform.main()
    brighten_stage_backdrop.main()


if __name__ == "__main__":
    main()
