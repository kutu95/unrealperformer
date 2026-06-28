"""Phase 6 step 1a — duplicate Kristofer -> BP_Godfrey_Performer (asset only, no bridge).

Headless-safe (does not spawn level actors).

  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/provision_phase6_godfrey_performer_asset.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

import godfrey_performer_shell as shell  # noqa: E402

REPORT = "Phase6Step1GodfreyShellAsset.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase6ShellAsset] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def main() -> None:
    bp = shell.ensure_godfrey_performer_blueprint(replace_existing=False)
    shell.assert_shell_valid(bp)
    audit = shell.audit_shell_blueprint(bp)
    log(f"Created/verified {shell.GODFREY_PERFORMER_BP}")
    log(f"Mesh components: {', '.join(audit['mesh_found'])}")
    log("No Godfrey/ACE exhibition components (step 1 OK)")
    write_report(True)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase6ShellAsset] {exc}")
        write_report(False)
        sys.exit(1)
