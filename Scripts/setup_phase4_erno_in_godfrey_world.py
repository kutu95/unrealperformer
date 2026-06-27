"""Place BP_Erno in Godfrey_World for Phase 4 zoom test.

Run after fresh Erno import via Quixel Bridge (see Docs/Phase4ErnoImport.txt).

  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    /Game/Godfrey_World
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase4_erno_in_godfrey_world.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import unreal

REPORT = "Phase4ErnoPlacement.txt"
ERN0_LABEL = "BP_Erno_Phase4"
REFERENCE_LABELS = ("BP_Kristofer", "Kristofer")
OFFSET_Y = 200.0
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase4Erno] {msg}")


def warn(msg: str) -> None:
    _lines.append(f"WARN: {msg}")
    unreal.log_warning(f"[Phase4Erno] {msg}")


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def find_erno_blueprint() -> unreal.Blueprint | None:
    asset_paths = [
        "/Game/MetaHumans/Erno/BP_Erno",
        "/Game/MetaHumans/Erno/BP_Erno.BP_Erno",
    ]
    for path in asset_paths:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            asset = unreal.EditorAssetLibrary.load_asset(path)
            if asset:
                log(f"Found blueprint: {path}")
                return asset
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = registry.get_assets_by_path("/Game/MetaHumans/Erno", recursive=True)
    for data in assets:
        name = str(data.asset_name)
        if name.startswith("BP_Erno") or name == "BP_Erno":
            path = data.get_full_name().split(" ", 1)[-1]
            loaded = unreal.EditorAssetLibrary.load_asset(path)
            if loaded:
                log(f"Found blueprint via registry: {path}")
                return loaded
    return None


def find_reference_actor():
    for actor in actors().get_all_level_actors():
        label = actor.get_actor_label()
        if label in REFERENCE_LABELS or any(t in label for t in REFERENCE_LABELS):
            return actor
    return None


def remove_existing_phase4_actors() -> None:
    for actor in actors().get_all_level_actors():
        if actor.get_actor_label() == ERN0_LABEL:
            actors().destroy_actor(actor)
            log(f"Removed existing {ERN0_LABEL}")


def write_report() -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines))
    log(f"Report: {path}")


def main() -> None:
    bp = find_erno_blueprint()
    if not bp:
        raise RuntimeError(
            "BP_Erno not found. Import Erno via Quixel Bridge first "
            "(Docs/Phase4ErnoImport.txt)."
        )

    generated = unreal.EditorAssetLibrary.load_blueprint_class(
        unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(bp)
    )
    if not generated:
        raise RuntimeError("Could not load BP_Erno generated class.")

    reference = find_reference_actor()
    if reference:
        loc = reference.get_actor_location()
        rot = reference.get_actor_rotation()
        spawn_loc = unreal.Vector(loc.x, loc.y + OFFSET_Y, max(loc.z, 100.0))
        log(f"Reference {reference.get_actor_label()} at {loc}")
    else:
        spawn_loc = unreal.Vector(440.0, OFFSET_Y, 100.0)
        rot = unreal.Rotator(0.0, 0.0, 0.0)
        warn("No Kristofer reference — using default spawn")

    remove_existing_phase4_actors()
    actor = actors().spawn_actor_from_class(generated, spawn_loc, rot)
    actor.set_actor_label(ERN0_LABEL)
    log(f"Spawned {ERN0_LABEL} at {spawn_loc}")

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("save_dirty_packages OK")
    write_report()


if __name__ == "__main__":
    main()
