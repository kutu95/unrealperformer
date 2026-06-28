"""Phase 7 step 1 — speech + lip sync only (no body montage actions).

Adds GodfreyAceWarmup + GodfreyDirectSpeech on BP_Godfrey_Performer, activates ACE curve
source. Bridge and performance-state body routing stay OFF.

Prerequisites:
  - Rebuild UnrealPerformerEditor and restart editor after pulling C++ changes
  - NV_ACE_Reference plugin junction present (Docs/ACEPlugin.txt)
  - Godfrey Brain running at http://localhost:3000 for PIE test

  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase7_step1_speech_lipsync_only.py"
    -unattended -nop4 -nosplash -log

PIE test: Godfrey_World, press G (dev keyboard) — expect voice + lip sync, no body gestures.
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

import godfrey_blueprint_wiring as wiring  # noqa: E402

REPORT = "Phase7Step1SpeechLipsyncOnly.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase7Step1] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def verify_cpp_classes() -> None:
    for class_path in (
        wiring.ACE_WARMUP_CLASS,
        wiring.DIRECT_SPEECH_CLASS,
    ):
        if not unreal.load_class(None, class_path):
            raise RuntimeError(
                f"Class not loaded: {class_path}. "
                "Rebuild UnrealPerformerEditor and restart the editor."
            )
        log(f"C++ class OK: {class_path}")


def main() -> None:
    verify_cpp_classes()

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    wiring.configure_inert_bridge(bp)
    wiring.configure_performance_state(bp)

    added_warmup = wiring.add_ace_warmup(bp)
    log("Added GodfreyAceWarmup" if added_warmup else "GodfreyAceWarmup already present")

    added_speech = wiring.add_direct_speech(bp)
    log("Added GodfreyDirectSpeech" if added_speech else "GodfreyDirectSpeech already present")

    ace_changes = wiring.configure_active_ace(bp)
    for key, value in ace_changes.items():
        log(f"ACE.{key} = {value}")

    warmup_changes = wiring.configure_ace_warmup(bp)
    for key, value in warmup_changes.items():
        log(f"AceWarmup.{key} = {value}")

    speech_changes = wiring.configure_direct_speech_lipsync_only(bp)
    for key, value in speech_changes.items():
        log(f"DirectSpeech.{key} = {value}")

    audit = wiring.audit_step7_speech_lipsync_blueprint(bp)
    for key, value in audit.items():
        log(f"audit.{key} = {value}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_assets([bp])
    write_report(True)
    log("Phase 7 step 1 complete — PIE: Brain at :3000, press G for test utterance")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase7Step1] {exc}")
        write_report(False)
        sys.exit(1)
