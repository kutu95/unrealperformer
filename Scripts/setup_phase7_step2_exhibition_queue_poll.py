"""Phase 7 step 2 — exhibition queue poll (legacy Test_Live_Audio path).

Removes GodfreyDirectSpeech from BP_Godfrey_Performer (no G-key dev path).
Adds GodfreyExhibitionQueuePollComponent on GM_Godfrey_Exhibit (~1s timer →
PullQueuedGodfreySpeechToAudio). Tags level performer GodfreyCharacter.

Prerequisites:
  - Rebuild UnrealPerformerEditor after pulling C++ (GodfreyExhibitionQueuePollComponent)
  - Phase 7 step 1 already applied (ACE + warmup on performer)
  - Godfrey Brain at http://localhost:3000
  - NvAudio2FaceMark + NV_ACE_Reference plugins (Docs/ACEPlugin.txt)

Headless (BP + GM edit; level tag skipped if no editor actors):
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    /Game/Godfrey_World
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase7_step2_exhibition_queue_poll.py"
    -unattended -nop4 -nosplash -log

PIE test: queue speech from exhibition site / Brain — voice + lip sync without pressing G.
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

import godfrey_blueprint_wiring as wiring  # noqa: E402

REPORT = "Phase7Step2ExhibitionQueuePoll.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase7Step2] {msg}")


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
        wiring.QUEUE_POLL_CLASS,
        wiring.ACE_WARMUP_CLASS,
    ):
        if not unreal.load_class(None, class_path):
            raise RuntimeError(
                f"Class not loaded: {class_path}. "
                "Rebuild UnrealPerformerEditor and restart the editor."
            )
        log(f"C++ class OK: {class_path}")


def configure_performer(bp) -> None:
    wiring.configure_inert_bridge(bp)
    wiring.configure_performance_state(bp)

    removed = wiring.remove_direct_speech(bp)
    log("Removed GodfreyDirectSpeech" if removed else "GodfreyDirectSpeech already absent")

    ace_changes = wiring.configure_active_ace(bp)
    for key, value in ace_changes.items():
        log(f"ACE.{key} = {value}")

    warmup_changes = wiring.configure_ace_warmup(bp)
    for key, value in warmup_changes.items():
        log(f"AceWarmup.{key} = {value}")

    audit = wiring.audit_step7_exhibition_queue_performer(bp)
    for key, value in audit.items():
        log(f"performer.audit.{key} = {value}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_assets([bp])


def configure_gamemode(gm_bp) -> None:
    added = wiring.add_exhibition_queue_poll(gm_bp)
    log("Added GodfreyExhibitionQueuePoll" if added else "GodfreyExhibitionQueuePoll already present")

    poll_changes = wiring.configure_exhibition_queue_poll(gm_bp)
    for key, value in poll_changes.items():
        log(f"QueuePoll.{key} = {value}")

    gm_audit = wiring.audit_gamemode_exhibition_queue(gm_bp)
    for key, value in gm_audit.items():
        log(f"gm.audit.{key} = {value}")

    unreal.BlueprintEditorLibrary.compile_blueprint(gm_bp)
    unreal.EditorAssetLibrary.save_loaded_assets([gm_bp])


def tag_performer_if_editor() -> None:
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not eas:
        log("EditorActorSubsystem unavailable — skip level tag (poll falls back to BP_Godfrey_Performer label)")
        return
    try:
        msg = wiring.tag_godfrey_performer_in_level()
        log(msg)
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    except Exception as exc:
        log(f"Level tag skipped: {exc}")


def main() -> None:
    verify_cpp_classes()

    performer_bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not performer_bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")
    configure_performer(performer_bp)

    gm_bp = unreal.load_asset(wiring.GM_GODFREY_EXHIBIT)
    if not gm_bp:
        raise RuntimeError(f"Missing {wiring.GM_GODFREY_EXHIBIT} — run setup_fix_godfrey_exhibit_play_sphere.py")
    configure_gamemode(gm_bp)

    tag_performer_if_editor()

    write_report(True)
    log(
        "Phase 7 step 2 complete — PIE: Brain at :3000, queue speech from exhibition site "
        "(no G key)"
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase7Step2] {exc}")
        write_report(False)
        sys.exit(1)
