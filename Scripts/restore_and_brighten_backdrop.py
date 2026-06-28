"""Restore Stage_Backdrop beside Kristofer (same WP cell) + apply bright material.

EDITOR ONLY — Tools > Execute Python Script with Godfrey_World open:
  1. wp.Editor.LoadAllCells  (console, if needed)
  2. Scripts/restore_and_brighten_backdrop.py
  3. Save level (Ctrl+S)
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import brighten_stage_backdrop  # noqa: E402
import godfrey_exhibit_guard  # noqa: E402
import restore_stage_backdrop  # noqa: E402


def main() -> None:
    godfrey_exhibit_guard.reject_headless_spawn()
    restore_stage_backdrop.main()
    brighten_stage_backdrop.main()


if __name__ == "__main__":
    main()
