"""Phase 6 step 4 — add ACEAudioCurveSourceComponent (inactive, no speech stack).

Requires NV_ACE_Reference plugin (see Docs/ACEPlugin.txt).

  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase6_step4_ace_audio_curve_source.py"
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

REPORT = "Phase6Step4AceAudioCurveSource.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase6Step4] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def main() -> None:
    ace_class = unreal.load_class(None, wiring.ACE_CURVE_SOURCE_CLASS)
    if not ace_class:
        raise RuntimeError(
            f"Class not loaded: {wiring.ACE_CURVE_SOURCE_CLASS}. "
            "Link NV_ACE_Reference plugin (Docs/ACEPlugin.txt) and restart editor."
        )
    log(f"C++ class OK: {wiring.ACE_CURVE_SOURCE_CLASS}")

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    added = wiring.add_ace_curve_source(bp)
    log("Added ACEAudioCurveSource" if added else "ACEAudioCurveSource already present")

    face_changes = wiring.ensure_face_anim_bp(bp)
    for key, value in face_changes.items():
        log(f"{key} = {value}")

    wiring.configure_inert_bridge(bp)
    ace_changes = wiring.configure_ace_curve_source(bp)
    for key, value in ace_changes.items():
        log(f"ACE.{key} = {value}")

    audit = wiring.audit_step4_blueprint(bp)
    for key, value in audit.items():
        log(f"audit.{key} = {value}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_assets([bp])
    write_report(True)
    log("Phase 6 step 4 complete — zoom-test 30+ s (no audio playback expected yet)")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase6Step4] {exc}")
        write_report(False)
        sys.exit(1)
