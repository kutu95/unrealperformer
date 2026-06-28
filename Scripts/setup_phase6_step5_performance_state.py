"""Phase 6 step 5 — add GodfreyPerformanceStateComponent (idle, no speech pipeline).

  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase6_step5_performance_state.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

import godfrey_blueprint_wiring as wiring  # noqa: E402

REPORT = "Phase6Step5PerformanceState.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase6Step5] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def main() -> None:
    state_class = unreal.load_class(None, wiring.PERFORMANCE_STATE_CLASS)
    if not state_class:
        raise RuntimeError(
            f"Class not loaded: {wiring.PERFORMANCE_STATE_CLASS}. "
            "Rebuild UnrealPerformerEditor and restart the editor."
        )
    log(f"C++ class OK: {wiring.PERFORMANCE_STATE_CLASS}")

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    added = wiring.add_performance_state(bp)
    log("Added GodfreyPerformanceState" if added else "GodfreyPerformanceState already present")

    wiring.configure_inert_bridge(bp)
    wiring.configure_ace_curve_source(bp)
    state_changes = wiring.configure_performance_state(bp)
    for key, value in state_changes.items():
        log(f"PerformanceState.{key} = {value}")

    audit = wiring.audit_step5_blueprint(bp)
    for key, value in audit.items():
        log(f"audit.{key} = {value}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_assets([bp])
    write_report(True)
    log("Phase 6 step 5 complete — zoom-test 30+ s before step 6")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase6Step5] {exc}")
        write_report(False)
        sys.exit(1)
