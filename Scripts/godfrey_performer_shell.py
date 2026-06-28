"""Shared helpers for Phase 6 Godfrey performer shell (meshes only, no bridge)."""
from __future__ import annotations

import unreal

KRISTOFER_DONOR = "/Game/MetaHumans/Kristofer/BP_Kristofer"
GODFREY_PACKAGE = "/Game/MetaHumans/Godfrey"
GODFREY_PERFORMER_BP = "/Game/MetaHumans/Godfrey/BP_Godfrey_Performer"
LEVEL_PERFORMER_LABEL = "BP_Godfrey_Performer"
LEGACY_PERFORMER_LABEL = "BP_Kristofer"

MESH_LABELS = ("Body", "Face", "Torso", "Legs", "Feet")
FORBIDDEN_COMPONENT_TOKENS = (
    "GodfreyPerformerAnimationBridge",
    "GodfreyPerformanceState",
    "GodfreyAceWarmup",
    "GodfreyGazeReaction",
    "GodfreyDirectSpeech",
    "ACEAudioCurveSource",
)


def _subobject_label(data) -> str:
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    var_name = str(lib.get_variable_name(data))
    if var_name and var_name != "None":
        return var_name
    return str(lib.get_display_name(data))


def iter_blueprint_components(bp):
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    if not subsystem:
        return
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    seen: set[str] = set()
    for handle in subsystem.k2_gather_subobject_data_for_blueprint(bp):
        data = lib.get_data(handle)
        if not data or not lib.is_component(data):
            continue
        label = _subobject_label(data)
        if label in seen:
            continue
        seen.add(label)
        component = lib.get_object_for_blueprint(data, bp)
        if component:
            yield label, component


def ensure_godfrey_performer_blueprint(*, replace_existing: bool = False) -> unreal.Blueprint | None:
    if not unreal.EditorAssetLibrary.does_asset_exist(KRISTOFER_DONOR):
        raise RuntimeError(
            f"Missing donor {KRISTOFER_DONOR} — import Kristofer from Quixel Bridge first."
        )

    if not unreal.EditorAssetLibrary.does_directory_exist(GODFREY_PACKAGE):
        unreal.EditorAssetLibrary.make_directory(GODFREY_PACKAGE)

    if unreal.EditorAssetLibrary.does_asset_exist(GODFREY_PERFORMER_BP):
        if not replace_existing:
            return unreal.load_asset(GODFREY_PERFORMER_BP)
        unreal.EditorAssetLibrary.delete_asset(GODFREY_PERFORMER_BP)

    duplicated = unreal.EditorAssetLibrary.duplicate_asset(KRISTOFER_DONOR, GODFREY_PERFORMER_BP)
    if not duplicated and not unreal.EditorAssetLibrary.does_asset_exist(GODFREY_PERFORMER_BP):
        raise RuntimeError(f"Failed to duplicate {KRISTOFER_DONOR} -> {GODFREY_PERFORMER_BP}")

    bp = unreal.load_asset(GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Could not load {GODFREY_PERFORMER_BP}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_assets([bp])
    return bp


def audit_shell_blueprint(bp) -> dict[str, list[str]]:
    """Return mesh_found, forbidden, missing_meshes lists."""
    mesh_found: list[str] = []
    forbidden: list[str] = []
    for label, component in iter_blueprint_components(bp):
        if label in MESH_LABELS:
            mesh_found.append(label)
        cls = component.get_class().get_name()
        for token in FORBIDDEN_COMPONENT_TOKENS:
            if token in cls:
                forbidden.append(f"{label} ({cls})")
    missing = [label for label in MESH_LABELS if label not in mesh_found]
    return {
        "mesh_found": mesh_found,
        "forbidden": forbidden,
        "missing_meshes": missing,
    }


def assert_shell_valid(bp) -> None:
    audit = audit_shell_blueprint(bp)
    if audit["missing_meshes"]:
        raise RuntimeError(f"Shell missing mesh components: {', '.join(audit['missing_meshes'])}")
    if audit["forbidden"]:
        raise RuntimeError(
            "Phase 6 step 1 must not include Godfrey/ACE components yet: "
            + ", ".join(audit["forbidden"])
        )
