"""Blueprint subobject helpers for Godfrey exhibition wiring (UE 5.6)."""
from __future__ import annotations

import unreal

GODFREY_PERFORMER_BP = "/Game/MetaHumans/Godfrey/BP_Godfrey_Performer"
BRIDGE_CLASS = "/Script/UnrealPerformer.GodfreyPerformerAnimationBridgeComponent"
BRIDGE_LABEL = "GodfreyPerformerAnimationBridge"
BODY_ANIM_CLASS_PATH = "/Script/UnrealPerformer.GodfreyBodyAnimInstance"
KRISTOFER_DONOR_BP = "/Game/MetaHumans/Kristofer/BP_Kristofer"
FACE_ANIM_TOKEN = "Face_AnimBP"

FORBIDDEN_STEP2_COMPONENTS = (
    "GodfreyPerformanceStateComponent",
    "GodfreyAceWarmupComponent",
    "GodfreyGazeReactionComponent",
    "ACEAudioCurveSourceComponent",
    "AudioCaptureComponent",
    "GodfreyDirectSpeechComponent",
)

FORBIDDEN_STEP4_COMPONENTS = (
    "GodfreyPerformanceStateComponent",
    "GodfreyAceWarmupComponent",
    "GodfreyGazeReactionComponent",
    "AudioCaptureComponent",
    "GodfreyDirectSpeechComponent",
)

FORBIDDEN_STEP5_COMPONENTS = (
    "GodfreyAceWarmupComponent",
    "GodfreyGazeReactionComponent",
    "AudioCaptureComponent",
    "GodfreyDirectSpeechComponent",
)

ACE_CURVE_SOURCE_CLASS = "/Script/ACERuntime.ACEAudioCurveSourceComponent"
ACE_CURVE_SOURCE_LABEL = "ACEAudioCurveSource"
PERFORMANCE_STATE_CLASS = "/Script/UnrealPerformer.GodfreyPerformanceStateComponent"
PERFORMANCE_STATE_LABEL = "GodfreyPerformanceState"
FACE_ANIM_BP_PATH = "/Game/MetaHumans/Common/Face/Face_AnimBP"


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


def _forbidden_components(bp, tokens: tuple[str, ...]) -> list[str]:
    hits: list[str] = []
    for comp_label, component, _h, _d in iter_all_components(bp):
        cls = component.get_class().get_name()
        for token in tokens:
            if token in cls:
                hits.append(f"{comp_label} ({cls})")
    return hits


def _bridge_auto_activate(bridge) -> object:
    for names in (["b_auto_activate", "bAutoActivate"],):
        try:
            return bridge.get_editor_property(names[0])
        except Exception:
            try:
                return bridge.get_editor_property(names[1])
            except Exception:
                pass
    return None


def audit_core_performer_stack(bp) -> dict[str, object]:
    bridge, _ = find_component_by_class(bp, "GodfreyPerformerAnimationBridgeComponent")
    if not bridge:
        raise RuntimeError("Missing GodfreyPerformerAnimationBridgeComponent")

    auto_activate = _bridge_auto_activate(bridge)
    if auto_activate is not False:
        raise RuntimeError(f"Bridge bAutoActivate must be False (got {auto_activate!r})")

    body_comp, _ = find_component(bp, "Body")
    face_comp, _ = find_component(bp, "Face")
    body_anim = _anim_class_name(body_comp)
    face_anim = _anim_class_name(face_comp)

    if "GodfreyBodyAnimInstance" not in body_anim:
        raise RuntimeError(f"Body AnimClass must be GodfreyBodyAnimInstance (got {body_anim})")
    if FACE_ANIM_TOKEN not in face_anim and face_anim != "None" and "Face" not in face_anim:
        raise RuntimeError(f"Face AnimClass should remain stock MetaHuman Face_AnimBP (got {face_anim})")

    return {
        "body_anim": body_anim,
        "face_anim": face_anim,
        "bridge_inert": True,
    }


def audit_step2_blueprint(bp) -> dict[str, object]:
    forbidden = _forbidden_components(bp, FORBIDDEN_STEP2_COMPONENTS)
    if forbidden:
        raise RuntimeError("Step 2 must not include other Godfrey/ACE components yet: " + ", ".join(forbidden))

    bridge, _ = find_component_by_class(bp, "GodfreyPerformerAnimationBridgeComponent")
    if not bridge:
        raise RuntimeError("Missing GodfreyPerformerAnimationBridgeComponent")

    return {
        "bridge_present": True,
        "bAutoActivate": _bridge_auto_activate(bridge),
        "forbidden_absent": True,
    }


def _anim_class_name(component) -> str:
    if not component:
        return "None"
    try:
        anim = component.get_editor_property("anim_class")
    except Exception:
        try:
            anim = component.get_editor_property("AnimClass")
        except Exception:
            return "(unreadable)"
    if not anim:
        return "None"
    try:
        return anim.get_name()
    except Exception:
        return str(anim)


def assign_body_anim_instance(bp) -> dict[str, str]:
    body_anim = unreal.load_class(None, BODY_ANIM_CLASS_PATH)
    if not body_anim:
        raise RuntimeError(f"Could not load {BODY_ANIM_CLASS_PATH}")

    body_comp, _ = find_component(bp, "Body")
    if not body_comp:
        raise RuntimeError("Body skeletal mesh component not found on performer BP")

    changes: dict[str, str] = {"Body.anim_before": _anim_class_name(body_comp)}
    prop = set_prop(body_comp, ["anim_class", "AnimClass"], body_anim)
    if not prop:
        raise RuntimeError("Failed to set Body AnimClass")
    changes["Body.anim_class"] = body_anim.get_name()
    return changes


def audit_step3_blueprint(bp) -> dict[str, object]:
    forbidden = _forbidden_components(bp, FORBIDDEN_STEP2_COMPONENTS)
    if forbidden:
        raise RuntimeError("Step 3 must not include ACE/speech components yet: " + ", ".join(forbidden))
    return audit_core_performer_stack(bp)


def ensure_face_anim_bp(bp) -> dict[str, str]:
    face_comp, _ = find_component(bp, "Face")
    if not face_comp:
        raise RuntimeError("Face component not found")

    changes: dict[str, str] = {"Face.anim_before": _anim_class_name(face_comp)}
    if FACE_ANIM_TOKEN in changes["Face.anim_before"] or "Face_AnimBP" in changes["Face.anim_before"]:
        changes["Face.anim_class"] = changes["Face.anim_before"]
        return changes

    face_anim_bp = unreal.load_asset(FACE_ANIM_BP_PATH)
    if not face_anim_bp:
        raise RuntimeError(f"Missing {FACE_ANIM_BP_PATH}")

    generated = face_anim_bp.generated_class() if hasattr(face_anim_bp, "generated_class") else None
    anim_cls = generated or unreal.load_class(None, f"{FACE_ANIM_BP_PATH}.Face_AnimBP_C")
    if not anim_cls:
        raise RuntimeError(f"Could not load Face_AnimBP class from {FACE_ANIM_BP_PATH}")

    prop = set_prop(face_comp, ["anim_class", "AnimClass"], anim_cls)
    if not prop:
        raise RuntimeError("Failed to set Face AnimClass")
    changes["Face.anim_class"] = anim_cls.get_name()
    return changes


def configure_ace_curve_source(bp) -> dict[str, str]:
    ace, ace_label = find_component_by_class(bp, "ACEAudioCurveSourceComponent")
    if not ace:
        raise RuntimeError("ACEAudioCurveSourceComponent not found on performer BP")

    changes: dict[str, str] = {"ace_label": ace_label or ACE_CURVE_SOURCE_LABEL}
    for names, value, key in (
        (["b_auto_activate", "bAutoActivate"], False, "bAutoActivate"),
        (["b_enable_attenuation_debug", "bEnableAttenuationDebug"], False, "bEnableAttenuationDebug"),
    ):
        if set_prop(ace, names, value):
            changes[key] = str(value)
    return changes


def add_ace_curve_source(bp) -> bool:
    return add_component_to_blueprint(bp, ACE_CURVE_SOURCE_LABEL, ACE_CURVE_SOURCE_CLASS, "root")


def audit_step4_blueprint(bp) -> dict[str, object]:
    forbidden = _forbidden_components(bp, FORBIDDEN_STEP4_COMPONENTS)
    if forbidden:
        raise RuntimeError("Step 4 must not include speech/warmup/capture yet: " + ", ".join(forbidden))

    ace, ace_label = find_component_by_class(bp, "ACEAudioCurveSourceComponent")
    if not ace:
        raise RuntimeError("Missing ACEAudioCurveSourceComponent")

    core = audit_core_performer_stack(bp)
    auto_activate = None
    for names in (["b_auto_activate", "bAutoActivate"],):
        try:
            auto_activate = ace.get_editor_property(names[0])
            break
        except Exception:
            try:
                auto_activate = ace.get_editor_property(names[1])
                break
            except Exception:
                pass

    if auto_activate is not False:
        raise RuntimeError(f"ACE bAutoActivate must be False for step 4 (got {auto_activate!r})")

    debug = None
    for names in (["b_enable_attenuation_debug", "bEnableAttenuationDebug"],):
        try:
            debug = ace.get_editor_property(names[0])
            break
        except Exception:
            try:
                debug = ace.get_editor_property(names[1])
                break
            except Exception:
                pass

    if debug is not False:
        raise RuntimeError(f"ACE bEnableAttenuationDebug must be False (got {debug!r})")

    core["ace_present"] = True
    core["ace_label"] = ace_label
    core["ace_bAutoActivate"] = auto_activate
    return core


def add_performance_state(bp) -> bool:
    return add_component_to_blueprint(
        bp, PERFORMANCE_STATE_LABEL, PERFORMANCE_STATE_CLASS, "actor"
    )


def configure_performance_state(bp) -> dict[str, str]:
    state, state_label = find_component_by_class(bp, "GodfreyPerformanceStateComponent")
    if not state:
        raise RuntimeError("GodfreyPerformanceStateComponent not found on performer BP")

    changes: dict[str, str] = {"state_label": state_label or PERFORMANCE_STATE_LABEL}
    for names, value, key in (
        (["b_auto_activate", "bAutoActivate"], False, "bAutoActivate"),
        (
            ["b_auto_speaking_state_from_utterance", "bAutoSpeakingStateFromUtterance"],
            False,
            "bAutoSpeakingStateFromUtterance",
        ),
        (
            ["b_route_performance_cues_to_states", "bRoutePerformanceCuesToStates"],
            False,
            "bRoutePerformanceCuesToStates",
        ),
    ):
        if set_prop(state, names, value):
            changes[key] = str(value)
    return changes


def audit_step5_blueprint(bp) -> dict[str, object]:
    forbidden = _forbidden_components(bp, FORBIDDEN_STEP5_COMPONENTS)
    if forbidden:
        raise RuntimeError("Step 5 must not include speech capture/warmup/gaze yet: " + ", ".join(forbidden))

    core = audit_step4_blueprint(bp)

    state, state_label = find_component_by_class(bp, "GodfreyPerformanceStateComponent")
    if not state:
        raise RuntimeError("Missing GodfreyPerformanceStateComponent")

    auto_activate = None
    for names in (["b_auto_activate", "bAutoActivate"],):
        try:
            auto_activate = state.get_editor_property(names[0])
            break
        except Exception:
            try:
                auto_activate = state.get_editor_property(names[1])
                break
            except Exception:
                pass

    if auto_activate is not False:
        raise RuntimeError(f"PerformanceState bAutoActivate must be False (got {auto_activate!r})")

    core["performance_state_present"] = True
    core["performance_state_label"] = state_label
    return core
