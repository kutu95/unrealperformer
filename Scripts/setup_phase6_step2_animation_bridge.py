"""Phase 6 step 2 — add GodfreyPerformerAnimationBridge (bAutoActivate=false).

Requires rebuilt UnrealPerformer C++ module (bridge compiled into baseline).

  Tools > Execute Python Script (editor open; live coding or editor restart after C++ build)

Headless asset edit (no level spawn):
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase6_step2_animation_bridge.py"
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

REPORT = "Phase6Step2AnimationBridge.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase6Step2] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def main() -> None:
    if not unreal.EditorAssetLibrary.does_asset_exist(wiring.GODFREY_PERFORMER_BP):
        raise RuntimeError(
            f"Missing {wiring.GODFREY_PERFORMER_BP} — run setup_phase6_godfrey_performer_shell.py first."
        )

    bridge_class = unreal.load_class(None, wiring.BRIDGE_CLASS)
    if not bridge_class:
        raise RuntimeError(
            f"Class not loaded: {wiring.BRIDGE_CLASS}. "
            "Build UnrealPerformerEditor (Development) and restart the editor."
        )
    log(f"C++ class OK: {wiring.BRIDGE_CLASS}")

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Could not load {wiring.GODFREY_PERFORMER_BP}")

    added = wiring.add_component_to_blueprint(bp, wiring.BRIDGE_LABEL, wiring.BRIDGE_CLASS, "actor")
    log("Added GodfreyPerformerAnimationBridge" if added else "Bridge already present")

    changes = wiring.configure_inert_bridge(bp)
    for key, value in changes.items():
        log(f"Bridge.{key} = {value}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    wiring.audit_step2_blueprint(bp)
    log("Audit OK — bridge only, bAutoActivate=false, no ACE/speech/performance state")

    unreal.EditorAssetLibrary.save_loaded_assets([bp])
    write_report(True)
    log("Phase 6 step 2 asset wiring complete — zoom-test 30+ s before step 3")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase6Step2] {exc}")
        write_report(False)
        sys.exit(1)
