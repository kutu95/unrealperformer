"""Pre-flight gate: verify Godfrey_World exhibit actors are loaded, not (Unloaded).

Run headless before any user PASS gate:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    /Game/Godfrey_World
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/verify_godfrey_exhibit.py"
    -unattended -nop4 -nosplash -log

Exit code non-zero + Saved/GodfreyExhibitSanity.txt on FAIL.
"""
from __future__ import annotations

import os
import sys

import unreal

REPORT = "GodfreyExhibitSanity.txt"
PERFORMER_LABELS = ("BP_Godfrey_Performer", "BP_Kristofer")  # Phase 6+: prefer shell label
REQUIRED_LOADED = (
    "Exhibit_Floor",
    "Exhibit_Fill_Key",
    "Exhibit_Fill_Rim",
    "Lumen_DirectionalLight",
    "Lumen_SkyLight",
    "Kristofer_PostProcess",
)
STAGE_REQUIRED_LOADED: tuple[str, ...] = ()  # Phase 4 baseline — user adds backdrop via editor UI
STAGE_PACKAGE_LABELS: dict[str, tuple[str, bytes]] = {}
# Historic failed headless spawns only — 9/ and A/ are legitimate after Phase 5 stage actors
FORBIDDEN_ORPHAN_WP_PREFIXES = ("0/", "3/", "7/")
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[ExhibitSanity] {msg}")


def fail(msg: str) -> None:
    log(f"FAIL: {msg}")


def pass_msg(msg: str) -> None:
    log(f"PASS: {msg}")


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def load_all_cells() -> None:
    world = unreal.EditorLevelLibrary.get_editor_world()
    if not world:
        raise RuntimeError("No editor world — open /Game/Godfrey_World")
    unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")
    log("Ran wp.Editor.LoadAllCells")


def check_orphan_wp_packages() -> bool:
    root = os.path.join(
        unreal.Paths.project_content_dir(),
        "__ExternalActors__",
        "Godfrey_World",
    )
    orphans: list[str] = []
    if os.path.isdir(root):
        for entry in os.listdir(root):
            rel = f"{entry}/"
            if rel in FORBIDDEN_ORPHAN_WP_PREFIXES or entry in ("0", "3", "7"):
                orphans.append(entry)
    if orphans:
        fail(f"Orphan WP cell folders present (Phase 5 split): {', '.join(sorted(orphans))}")
        return False
    pass_msg("No orphan WP cell folders (0/3/7)")
    return True


def check_required_actors_loaded() -> bool:
    loaded_labels = {a.get_actor_label() for a in actors().get_all_level_actors()}
    missing = [label for label in REQUIRED_LOADED if label not in loaded_labels]
    performer_hits = [label for label in PERFORMER_LABELS if label in loaded_labels]
    if len(performer_hits) == 0:
        missing.append(f"one of ({', '.join(PERFORMER_LABELS)})")
    elif len(performer_hits) > 1:
        fail(f"Multiple performer actors loaded: {', '.join(performer_hits)}")
        return False
    if missing:
        fail(f"Required actors not loaded: {', '.join(missing)}")
        log(f"Loaded actor labels ({len(loaded_labels)}): {', '.join(sorted(loaded_labels))}")
        return False
    pass_msg(
        f"All {len(REQUIRED_LOADED)} exhibit actors + performer ({performer_hits[0]}) loaded"
    )
    return True


def check_performer_placement() -> bool:
    for actor in actors().get_all_level_actors():
        label = actor.get_actor_label()
        if label not in PERFORMER_LABELS:
            continue
        loc = actor.get_actor_location()
        if abs(loc.x) < 1.0 and abs(loc.y) < 1.0 and abs(loc.z) < 1.0:
            fail(f"{label} at world origin {loc} — likely broken placement")
            return False
        log(f"{label} at ({loc.x:.1f}, {loc.y:.1f}, {loc.z:.1f})")
        pass_msg(f"{label} has exhibit placement")
        return True
    fail(f"No performer actor ({' or '.join(PERFORMER_LABELS)}) in level")
    return False


def check_single_exhibit_floor_package() -> bool:
    root = os.path.join(
        unreal.Paths.project_content_dir(),
        "__ExternalActors__",
        "Godfrey_World",
    )
    hits: list[str] = []
    if os.path.isdir(root):
        for dirpath, _, filenames in os.walk(root):
            for name in filenames:
                if not name.endswith(".uasset"):
                    continue
                path = os.path.join(dirpath, name)
                with open(path, "rb") as handle:
                    if b"Exhibit_Floor" in handle.read():
                        rel = path.replace(unreal.Paths.project_content_dir(), "Content/").replace("\\", "/")
                        hits.append(rel)
    if len(hits) > 1:
        fail(f"Duplicate Exhibit_Floor WP packages on disk: {', '.join(hits)}")
        return False
    if len(hits) == 1:
        log(f"Single Exhibit_Floor package: {hits[0]}")
        pass_msg("Exactly one Exhibit_Floor external actor package")
        return True
    fail("No Exhibit_Floor external actor package found")
    return False


def check_stage_actors_loaded() -> bool:
    if not STAGE_REQUIRED_LOADED:
        log("Stage actors check skipped (Phase 4 baseline)")
        return True
    loaded_labels = {a.get_actor_label() for a in actors().get_all_level_actors()}
    missing = [label for label in STAGE_REQUIRED_LOADED if label not in loaded_labels]
    if missing:
        fail(f"Stage actors not loaded: {', '.join(missing)}")
        return False
    pass_msg(f"All {len(STAGE_REQUIRED_LOADED)} stage actors loaded")
    return True


def check_stage_packages_on_disk() -> bool:
    if not STAGE_PACKAGE_LABELS:
        log("Stage package check skipped (Phase 4 baseline)")
        return True
    root = os.path.join(
        unreal.Paths.project_content_dir(),
        "__ExternalActors__",
        "Godfrey_World",
    )
    ok = True
    if not os.path.isdir(root):
        fail("No Godfrey_World external actors folder")
        return False
    for label, (needle, class_hint) in STAGE_PACKAGE_LABELS.items():
        hits: list[str] = []
        label_bytes = needle.encode("ascii")
        for dirpath, _, filenames in os.walk(root):
            for name in filenames:
                if not name.endswith(".uasset"):
                    continue
                path = os.path.join(dirpath, name)
                with open(path, "rb") as handle:
                    data = handle.read()
                if label_bytes in data and class_hint in data:
                    rel = path.replace(unreal.Paths.project_content_dir(), "Content/").replace("\\", "/")
                    hits.append(rel)
        if len(hits) == 0:
            fail(f"No WP package on disk for {label}")
            ok = False
        elif len(hits) > 1:
            fail(f"Duplicate WP packages for {label}: {', '.join(hits)}")
            ok = False
        else:
            log(f"{label} package: {hits[0]}")
    if ok:
        pass_msg("Stage actor packages OK")
    return ok


def check_no_duplicate_loaded_labels() -> bool:
    counts: dict[str, int] = {}
    for actor in actors().get_all_level_actors():
        label = actor.get_actor_label()
        counts[label] = counts.get(label, 0) + 1
    dupes = [f"{label} x{count}" for label, count in counts.items() if count > 1]
    if dupes:
        fail(f"Duplicate loaded actor labels: {', '.join(dupes)}")
        return False
    pass_msg("No duplicate loaded actor labels")
    return True


def write_report(passed: bool) -> str:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if passed else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")
    return path


def main() -> None:
    ok = True
    load_all_cells()
    ok = check_orphan_wp_packages() and ok
    ok = check_single_exhibit_floor_package() and ok
    ok = check_required_actors_loaded() and ok
    ok = check_stage_actors_loaded() and ok
    ok = check_stage_packages_on_disk() and ok
    ok = check_no_duplicate_loaded_labels() and ok
    ok = check_performer_placement() and ok
    write_report(ok)
    if not ok:
        raise RuntimeError(
            "Godfrey_World exhibit sanity check FAILED — do not ask user to test. "
            "See Saved/GodfreyExhibitSanity.txt"
        )


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as exc:
        unreal.log_error(str(exc))
        sys.exit(1)
