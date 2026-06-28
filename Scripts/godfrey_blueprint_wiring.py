"""Blueprint subobject helpers for Godfrey exhibition wiring (UE 5.6)."""
from __future__ import annotations

import unreal

GODFREY_PERFORMER_BP = "/Game/MetaHumans/Godfrey/BP_Godfrey_Performer"
BRIDGE_CLASS = "/Script/UnrealPerformer.GodfreyPerformerAnimationBridgeComponent"
BRIDGE_LABEL = "GodfreyPerformerAnimationBridge"

FORBIDDEN_STEP2_COMPONENTS = (
    "GodfreyPerformanceStateComponent",
    "GodfreyAceWarmupComponent",
    "GodfreyGazeReactionComponent",
    "ACEAudioCurveSourceComponent",
    "AudioCaptureComponent",
    "GodfreyDirectSpeechComponent",
)


def set_prop(obj, names: list[str], value) -> str | None:
    for name in names:
        try:
            obj.set_editor_property(name, value)
            return name
        except Exception:
            continue
    return None


def _subobject_label(data) -> str:
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    var_name = str(lib.get_variable_name(data))
    if var_name and var_name != "None":
        return var_name
    return str(lib.get_display_name(data))


def iter_all_components(bp):
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    if not subsystem:
        return
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    for handle in subsystem.k2_gather_subobject_data_for_blueprint(bp):
        data = lib.get_data(handle)
        if not data or not lib.is_component(data):
            continue
        component = lib.get_object_for_blueprint(data, bp)
        if component:
            yield _subobject_label(data), component, handle, data


def find_component(bp, label: str):
    target = label.lower()
    for comp_label, component, _handle, _data in iter_all_components(bp):
        if comp_label.lower() == target:
            return component, comp_label
    return None, None


def find_component_by_class(bp, class_substring: str):
    for comp_label, component, _handle, _data in iter_all_components(bp):
        if class_substring in component.get_class().get_name():
            return component, comp_label
    return None, None


def find_subobject_handles(bp) -> tuple[object | None, object | None]:
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    if not subsystem:
        return None, None

    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    actor_handle = None
    root_handle = None

    for handle in subsystem.k2_gather_subobject_data_for_blueprint(bp):
        data = lib.get_data(handle)
        if not data:
            continue
        if lib.is_actor(data):
            actor_handle = handle
        if lib.is_root_component(data) or lib.is_default_scene_root(data):
            root_handle = handle
        if _subobject_label(data).lower() == "root" and lib.is_component(data):
            root_handle = handle

    return actor_handle, root_handle


def component_already_present(bp, class_path: str, desired_label: str) -> bool:
    cls_name = class_path.rsplit(".", 1)[-1]
    for comp_label, component, _handle, _data in iter_all_components(bp):
        if cls_name in component.get_class().get_name():
            return True
        if comp_label.lower() == desired_label.lower():
            return True
    return False


def add_component_to_blueprint(bp, label: str, class_path: str, parent_kind: str) -> bool:
    if component_already_present(bp, class_path, label):
        return True

    component_class = unreal.load_class(None, class_path)
    if not component_class:
        raise RuntimeError(f"Could not load class {class_path} — rebuild UnrealPerformer C++ module first.")

    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    if not subsystem:
        raise RuntimeError("SubobjectDataSubsystem unavailable")

    actor_handle, root_handle = find_subobject_handles(bp)
    parent_handle = actor_handle if parent_kind == "actor" else (root_handle or actor_handle)
    if not parent_handle:
        raise RuntimeError(f"No parent handle for {label}")

    params = unreal.AddNewSubobjectParams()
    for prop, value in (
        ("ParentHandle", parent_handle),
        ("parent_handle", parent_handle),
        ("NewClass", component_class),
        ("new_class", component_class),
        ("BlueprintContext", bp),
        ("blueprint_context", bp),
    ):
        try:
            params.set_editor_property(prop, value)
        except Exception:
            pass

    try:
        new_handle = subsystem.add_new_subobject(params)
    except TypeError:
        fail_reason = unreal.Text()
        new_handle = subsystem.add_new_subobject(params, fail_reason)

    if not new_handle:
        raise RuntimeError(f"add_new_subobject failed for {label}")

    return True


def configure_inert_bridge(bp) -> dict[str, str]:
    """Phase 6 step 2 — bridge present but bAutoActivate=false (no MetaHuman intervention)."""
    body_comp, _ = find_component(bp, "Body")
    bridge, bridge_label = find_component_by_class(bp, "GodfreyPerformerAnimationBridgeComponent")
    if not bridge:
        raise RuntimeError("GodfreyPerformerAnimationBridgeComponent not found on performer BP")

    changes: dict[str, str] = {}
    if body_comp:
        set_prop(bridge, ["target_skeletal_mesh", "TargetSkeletalMesh"], body_comp)
        changes["TargetSkeletalMesh"] = "Body"

    for names, value, key in (
        (["b_auto_activate", "bAutoActivate"], False, "bAutoActivate"),
        (["b_auto_resolve_metahuman_body_mesh", "bAutoResolveMetaHumanBodyMesh"], True, "bAutoResolveMetaHumanBodyMesh"),
        (["b_manage_meta_human_garments_at_runtime", "bManageMetaHumanGarmentsAtRuntime"], False, "bManageMetaHumanGarmentsAtRuntime"),
        (["b_auto_wire_clothing_leader_pose_to_body", "bAutoWireClothingLeaderPoseToBody"], False, "bAutoWireClothingLeaderPoseToBody"),
        (["b_log_meta_human_shirt_diagnostics", "bLogMetaHumanShirtDiagnostics"], False, "bLogMetaHumanShirtDiagnostics"),
        (["b_auto_assign_placeholder_montages", "bAutoAssignPlaceholderMontages"], False, "bAutoAssignPlaceholderMontages"),
        (["b_enable_idle_micro_motion", "bEnableIdleMicroMotion"], False, "bEnableIdleMicroMotion"),
        (["b_prefer_speaking_idle_loop_only", "bPreferSpeakingIdleLoopOnly"], True, "bPreferSpeakingIdleLoopOnly"),
    ):
        if set_prop(bridge, names, value):
            changes[key] = str(value)

    changes["bridge_label"] = bridge_label or BRIDGE_LABEL
    return changes


def audit_step2_blueprint(bp) -> dict[str, object]:
    bridge, _ = find_component_by_class(bp, "GodfreyPerformerAnimationBridgeComponent")
    if not bridge:
        raise RuntimeError("Missing GodfreyPerformerAnimationBridgeComponent")

    forbidden: list[str] = []
    for comp_label, component, _h, _d in iter_all_components(bp):
        cls = component.get_class().get_name()
        for token in FORBIDDEN_STEP2_COMPONENTS:
            if token in cls:
                forbidden.append(f"{comp_label} ({cls})")

    auto_activate = None
    for names in (["b_auto_activate", "bAutoActivate"],):
        try:
            auto_activate = bridge.get_editor_property(names[0])
            break
        except Exception:
            try:
                auto_activate = bridge.get_editor_property(names[1])
                break
            except Exception:
                pass

    if auto_activate is not False:
        raise RuntimeError(f"Bridge bAutoActivate must be False for step 2 (got {auto_activate!r})")
    if forbidden:
        raise RuntimeError("Step 2 must not include other Godfrey/ACE components yet: " + ", ".join(forbidden))

    return {
        "bridge_present": True,
        "bAutoActivate": auto_activate,
        "forbidden_absent": True,
    }
