"""Phase 7 step 3 — ACE lip sync on MetaHuman Face_AnimBP.

Stock Bridge Face_AnimBP does not include Apply ACE Face Animations. Test_Live_Audio's
copy was patched for ACE; this script copies that donor (if present) and verifies the
performer Face mesh still uses /Game/MetaHumans/Common/Face/Face_AnimBP.

Prerequisites:
  - Phase 7 step 2 (queue poll + active ACE on performer)
  - Donor: D:/UE Projects/Test_Live_Audio/Content/MetaHumans/Common/Face/Face_AnimBP.uasset

  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase7_step3_face_ace_lipsync.py"
    -unattended -nop4 -nosplash -log

PIE: queue speech — expect lip sync (log: [ACE sync] First curve weights applied).
"""
from __future__ import annotations

import os
import shutil
import sys
from pathlib import Path

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

import godfrey_blueprint_wiring as wiring  # noqa: E402

REPORT = "Phase7Step3FaceAceLipsync.txt"
DONOR_FACE_ANIM_BP = Path(
    r"D:/UE Projects/Test_Live_Audio/Content/MetaHumans/Common/Face/Face_AnimBP.uasset"
)
PROJECT_FACE_ANIM_REL = "Content/MetaHumans/Common/Face/Face_AnimBP.uasset"
ACE_NODE_TOKENS = (b"ApplyACE", b"AnimNode_ApplyACE")
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase7Step3] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def project_face_anim_path() -> Path:
    return Path(unreal.Paths.project_dir()).resolve() / PROJECT_FACE_ANIM_REL


def uasset_has_ace_node(path: Path) -> bool:
    if not path.is_file():
        return False
    data = path.read_bytes()
    return all(token in data for token in ACE_NODE_TOKENS)


def sync_face_anim_bp_from_donor() -> None:
    target = project_face_anim_path()
    target.parent.mkdir(parents=True, exist_ok=True)

    if not DONOR_FACE_ANIM_BP.is_file():
        if uasset_has_ace_node(target):
            log(f"Donor missing; local {target.name} already contains ACE node")
            return
        raise RuntimeError(
            f"Missing donor {DONOR_FACE_ANIM_BP} and local Face_AnimBP has no ACE node. "
            "Open Face_AnimBP in editor and add Apply ACE Face Animations, or copy donor from Test_Live_Audio."
        )

    if not uasset_has_ace_node(DONOR_FACE_ANIM_BP):
        raise RuntimeError(f"Donor {DONOR_FACE_ANIM_BP} does not contain ApplyACE node")

    if target.is_file() and target.read_bytes() == DONOR_FACE_ANIM_BP.read_bytes():
        log("Face_AnimBP already matches ACE donor — no copy needed")
        return

    shutil.copy2(DONOR_FACE_ANIM_BP, target)
    log(f"Copied ACE Face_AnimBP from {DONOR_FACE_ANIM_BP.parent.parent.parent.name}/Test_Live_Audio")


def compile_and_wire_performer() -> None:
    face_anim_asset = unreal.load_asset(wiring.FACE_ANIM_BP_PATH)
    if not face_anim_asset:
        raise RuntimeError(f"Could not load {wiring.FACE_ANIM_BP_PATH} after copy")

    unreal.BlueprintEditorLibrary.compile_blueprint(face_anim_asset)
    log("Compiled Face_AnimBP")

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    face_changes = wiring.ensure_face_anim_bp(bp)
    for key, value in face_changes.items():
        log(f"{key} = {value}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_assets([face_anim_asset, bp])
    log("Compiled and saved BP_Godfrey_Performer")


def verify_ace_node_present() -> None:
    path = project_face_anim_path()
    if not uasset_has_ace_node(path):
        raise RuntimeError(f"{path} still missing ApplyACE node after sync")
    log("Verified Face_AnimBP.uasset contains ApplyACE / AnimNode_ApplyACE")


def main() -> None:
    sync_face_anim_bp_from_donor()
    verify_ace_node_present()
    compile_and_wire_performer()
    write_report(True)
    log("Phase 7 step 3 complete — PIE: queue speech; expect lip sync + First curve weights applied log")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase7Step3] {exc}")
        write_report(False)
        sys.exit(1)
