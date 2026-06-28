"""Shared guards for Godfrey_World exhibit editor scripts."""
from __future__ import annotations

import unreal


def reject_headless_spawn() -> None:
    """Headless saves split World Partition actors into unloaded cells."""
    cmd = unreal.SystemLibrary.get_command_line().lower()
    if "-unattended" in cmd or "unrealeditor-cmd" in cmd:
        raise RuntimeError(
            "This script must run from the open Unreal Editor "
            "(Tools > Execute Python Script), NOT UnrealEditor-Cmd. "
            "Headless runs break World Partition and leave actors (Unloaded)."
        )


def require_loaded(labels: tuple[str, ...]) -> None:
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    loaded = {a.get_actor_label() for a in eas.get_all_level_actors()}
    missing = [label for label in labels if label not in loaded]
    if missing:
        raise RuntimeError(
            f"Required actors not loaded: {', '.join(missing)}. "
            "Run wp.Editor.LoadAllCells in the console, then retry."
        )
