"""Phase 6 step 3 — assign GodfreyBodyAnimInstance on Body mesh (bridge still inert).

  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase6_step3_body_anim_instance.py"
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

REPORT = "Phase6Step3BodyAnimInstance.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase6Step3] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def main() -> None:
    body_anim = unreal.load_class(None, wiring.BODY_ANIM_CLASS_PATH)
    if not body_anim:
        raise RuntimeError(
            f"Class not loaded: {wiring.BODY_ANIM_CLASS_PATH}. Rebuild UnrealPerformerEditor."
        )

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    changes = wiring.assign_body_anim_instance(bp)
    for key, value in changes.items():
        log(f"{key} = {value}")

    wiring.configure_inert_bridge(bp)
    audit = wiring.audit_step3_blueprint(bp)
    for key, value in audit.items():
        log(f"audit.{key} = {value}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_assets([bp])
    write_report(True)
    log("Phase 6 step 3 complete — zoom-test 30+ s before step 4")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase6Step3] {exc}")
        write_report(False)
        sys.exit(1)
