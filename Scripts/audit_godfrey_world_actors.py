"""List all actor labels/descriptors on Godfrey_World (loaded + stale)."""
from __future__ import annotations

import os
import sys

import unreal

REPORT = "GodfreyWorldActorAudit.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[ActorAudit] {msg}")


def load_all_cells() -> None:
    world = unreal.EditorLevelLibrary.get_editor_world()
    if not world:
        raise RuntimeError("No editor world")
    unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")


def audit_loaded_actors() -> None:
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    counts: dict[str, int] = {}
    for actor in eas.get_all_level_actors():
        label = actor.get_actor_label()
        counts[label] = counts.get(label, 0) + 1
        loc = actor.get_actor_location()
        log(f"LOADED {label} ({actor.get_class().get_name()}) @ ({loc.x:.1f},{loc.y:.1f},{loc.z:.1f})")

    log("--- duplicate labels among loaded actors ---")
    for label, count in sorted(counts.items()):
        if count > 1:
            log(f"DUPLICATE LOADED: {label} x{count}")


def audit_umap_strings() -> None:
    umap = os.path.join(unreal.Paths.project_content_dir(), "Godfrey_World.umap")
    if not os.path.isfile(umap):
        log("Godfrey_World.umap not found")
        return
    with open(umap, "rb") as handle:
        data = handle.read()
    text = data.decode("latin-1", errors="ignore")
    for needle in (
        "Exhibit_Floor",
        "Stage_Backdrop",
        "BP_Godfrey_Performer",
        "BP_Kristofer",
        "JK8XG654",
        "4EDBL63",
    ):
        count = text.count(needle)
        if count:
            log(f"Godfrey_World.umap contains '{needle}' x{count}")


def audit_external_packages() -> None:
    root = os.path.join(
        unreal.Paths.project_content_dir(), "__ExternalActors__", "Godfrey_World"
    )
    floor_hits: list[str] = []
    for dirpath, _, filenames in os.walk(root):
        for name in filenames:
            if not name.endswith(".uasset"):
                continue
            path = os.path.join(dirpath, name)
            with open(path, "rb") as handle:
                data = handle.read()
            if b"Exhibit_Floor" in data:
                rel = path.replace(unreal.Paths.project_content_dir(), "Content/")
                floor_hits.append(rel.replace("\\", "/"))
    log(f"External packages with Exhibit_Floor label: {len(floor_hits)}")
    for hit in floor_hits:
        log(f"  {hit}")


def main() -> None:
    load_all_cells()
    audit_loaded_actors()
    audit_expected_missing()
    audit_umap_strings()
    audit_external_packages()


def audit_expected_missing() -> None:
    expected = (
        "BP_Godfrey_Performer",
        "BP_Kristofer",
        "Exhibit_Floor",
        "Stage_Backdrop",
        "Stage_BackdropFill",
    )
    loaded = {a.get_actor_label() for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}
    log("--- expected exhibit actors ---")
    for label in expected:
        status = "LOADED" if label in loaded else "MISSING"
        log(f"{status}: {label}")
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines) + "\n")
    log(f"Report: {path}")


if __name__ == "__main__":
    main()
