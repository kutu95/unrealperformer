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
ACE_WARMUP_CLASS = "/Script/UnrealPerformer.GodfreyAceWarmupComponent"
ACE_WARMUP_LABEL = "GodfreyAceWarmup"
DIRECT_SPEECH_CLASS = "/Script/UnrealPerformer.GodfreyDirectSpeechComponent"
DIRECT_SPEECH_LABEL = "GodfreyDirectSpeech"
QUEUE_POLL_CLASS = "/Script/UnrealPerformer.GodfreyExhibitionQueuePollComponent"
QUEUE_POLL_LABEL = "GodfreyExhibitionQueuePoll"
GM_GODFREY_EXHIBIT = "/Game/Godfrey/GM_Godfrey_Exhibit"
GODFREY_CHARACTER_TAG = "GodfreyCharacter"
FACE_ANIM_BP_PATH = "/Game/MetaHumans/Common/Face/Face_AnimBP"

FORBIDDEN_STEP7_COMPONENTS = (
    "GodfreyGazeReactionComponent",
    "AudioCaptureComponent",
)

FORBIDDEN_STEP7_QUEUE_POLL_COMPONENTS = (
    "GodfreyGazeReactionComponent",
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


def _get_bool_prop(component, names: tuple[str, ...]) -> object:
    for name in names:
        try:
            return component.get_editor_property(name)
        except Exception:
            continue
    return None


def audit_ace_inert(bp) -> dict[str, object]:
    ace, ace_label = find_component_by_class(bp, "ACEAudioCurveSourceComponent")
    if not ace:
        raise RuntimeError("Missing ACEAudioCurveSourceComponent")

    auto_activate = _get_bool_prop(ace, ("b_auto_activate", "bAutoActivate"))
    if auto_activate is not False:
        raise RuntimeError(f"ACE bAutoActivate must be False (got {auto_activate!r})")

    debug = _get_bool_prop(ace, ("b_enable_attenuation_debug", "bEnableAttenuationDebug"))
    if debug is not False:
        raise RuntimeError(f"ACE bEnableAttenuationDebug must be False (got {debug!r})")

    return {
        "ace_present": True,
        "ace_label": ace_label,
        "ace_bAutoActivate": auto_activate,
    }


def audit_step4_blueprint(bp) -> dict[str, object]:
    forbidden = _forbidden_components(bp, FORBIDDEN_STEP4_COMPONENTS)
    if forbidden:
        raise RuntimeError("Step 4 must not include speech/warmup/capture yet: " + ", ".join(forbidden))

    core = audit_core_performer_stack(bp)
    core.update(audit_ace_inert(bp))
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

    core = audit_core_performer_stack(bp)
    core.update(audit_ace_inert(bp))

    state, state_label = find_component_by_class(bp, "GodfreyPerformanceStateComponent")
    if not state:
        raise RuntimeError("Missing GodfreyPerformanceStateComponent")

    auto_activate = _get_bool_prop(state, ("b_auto_activate", "bAutoActivate"))
    if auto_activate is not False:
        raise RuntimeError(f"PerformanceState bAutoActivate must be False (got {auto_activate!r})")

    core["performance_state_present"] = True
    core["performance_state_label"] = state_label
    return core


def configure_active_ace(bp) -> dict[str, str]:
    ace, ace_label = find_component_by_class(bp, "ACEAudioCurveSourceComponent")
    if not ace:
        raise RuntimeError("ACEAudioCurveSourceComponent not found on performer BP")

    changes: dict[str, str] = {"ace_label": ace_label or ACE_CURVE_SOURCE_LABEL}
    for names, value, key in (
        (["b_auto_activate", "bAutoActivate"], True, "bAutoActivate"),
        (["b_enable_attenuation_debug", "bEnableAttenuationDebug"], False, "bEnableAttenuationDebug"),
        (["volume", "Volume"], 1.0, "Volume"),
    ):
        if set_prop(ace, names, value):
            changes[key] = str(value)
    return changes


def add_ace_warmup(bp) -> bool:
    return add_component_to_blueprint(bp, ACE_WARMUP_LABEL, ACE_WARMUP_CLASS, "actor")


def configure_ace_warmup(bp) -> dict[str, str]:
    warmup, label = find_component_by_class(bp, "GodfreyAceWarmupComponent")
    if not warmup:
        raise RuntimeError("GodfreyAceWarmupComponent not found on performer BP")

    changes: dict[str, str] = {"warmup_label": label or ACE_WARMUP_LABEL}
    for names, value, key in (
        (["b_warmup_on_begin_play", "bWarmupOnBeginPlay"], True, "bWarmupOnBeginPlay"),
        (["ace_provider_name", "AceProviderName"], "LocalA2F-Mark", "AceProviderName"),
        (["warmup_sample_rate", "WarmupSampleRate"], 24000, "WarmupSampleRate"),
    ):
        if set_prop(warmup, names, value):
            changes[key] = str(value)
    return changes


def add_direct_speech(bp) -> bool:
    return add_component_to_blueprint(bp, DIRECT_SPEECH_LABEL, DIRECT_SPEECH_CLASS, "actor")


def configure_direct_speech_lipsync_only(bp) -> dict[str, str]:
    speech, label = find_component_by_class(bp, "GodfreyDirectSpeechComponent")
    if not speech:
        raise RuntimeError("GodfreyDirectSpeechComponent not found on performer BP")

    changes: dict[str, str] = {"speech_label": label or DIRECT_SPEECH_LABEL}
    for names, value, key in (
        (["godfrey_brain_base_url", "GodfreyBrainBaseUrl"], "http://localhost:3000", "GodfreyBrainBaseUrl"),
        (["ace_provider_name", "AceProviderName"], "LocalA2F-Mark", "AceProviderName"),
        (["stream_sample_rate", "StreamSampleRate"], 24000, "StreamSampleRate"),
        (["stream_num_channels", "StreamNumChannels"], 1, "StreamNumChannels"),
        (["b_begin_thinking_on_submit", "bBeginThinkingOnSubmit"], False, "bBeginThinkingOnSubmit"),
        (["b_return_to_listening_after_reply", "bReturnToListeningAfterReply"], False, "bReturnToListeningAfterReply"),
        (["b_enable_dev_keyboard_submit", "bEnableDevKeyboardSubmit"], True, "bEnableDevKeyboardSubmit"),
        (["b_auto_submit_test_prompt_on_begin_play", "bAutoSubmitTestPromptOnBeginPlay"], False, "bAutoSubmitTestPromptOnBeginPlay"),
        (
            ["default_test_prompt", "DefaultTestPrompt"],
            "Tell me about your voyage to the New World.",
            "DefaultTestPrompt",
        ),
    ):
        if set_prop(speech, names, value):
            changes[key] = str(value)
    return changes


def audit_ace_active(bp) -> dict[str, object]:
    ace, ace_label = find_component_by_class(bp, "ACEAudioCurveSourceComponent")
    if not ace:
        raise RuntimeError("Missing ACEAudioCurveSourceComponent")

    auto_activate = _get_bool_prop(ace, ("b_auto_activate", "bAutoActivate"))
    if auto_activate is not True:
        raise RuntimeError(f"ACE bAutoActivate must be True (got {auto_activate!r})")

    return {
        "ace_present": True,
        "ace_label": ace_label,
        "ace_bAutoActivate": auto_activate,
    }


def audit_step7_speech_lipsync_blueprint(bp) -> dict[str, object]:
    forbidden = _forbidden_components(bp, FORBIDDEN_STEP7_COMPONENTS)
    if forbidden:
        raise RuntimeError("Step 7 must not include gaze/mic capture yet: " + ", ".join(forbidden))

    bridge, _ = find_component_by_class(bp, "GodfreyPerformerAnimationBridgeComponent")
    if not bridge:
        raise RuntimeError("Missing GodfreyPerformerAnimationBridgeComponent")
    bridge_auto = _bridge_auto_activate(bridge)
    if bridge_auto is not False:
        raise RuntimeError(f"Bridge bAutoActivate must stay False (got {bridge_auto!r})")

    state, _ = find_component_by_class(bp, "GodfreyPerformanceStateComponent")
    if not state:
        raise RuntimeError("Missing GodfreyPerformanceStateComponent")
    if _get_bool_prop(state, ("b_auto_speaking_state_from_utterance", "bAutoSpeakingStateFromUtterance")) is not False:
        raise RuntimeError("PerformanceState bAutoSpeakingStateFromUtterance must stay False")
    if _get_bool_prop(state, ("b_route_performance_cues_to_states", "bRoutePerformanceCuesToStates")) is not False:
        raise RuntimeError("PerformanceState bRoutePerformanceCuesToStates must stay False")

    warmup, _ = find_component_by_class(bp, "GodfreyAceWarmupComponent")
    if not warmup:
        raise RuntimeError("Missing GodfreyAceWarmupComponent")
    if _get_bool_prop(warmup, ("b_warmup_on_begin_play", "bWarmupOnBeginPlay")) is not True:
        raise RuntimeError("AceWarmup bWarmupOnBeginPlay must be True")

    speech, _ = find_component_by_class(bp, "GodfreyDirectSpeechComponent")
    if not speech:
        raise RuntimeError("Missing GodfreyDirectSpeechComponent")

    core = audit_core_performer_stack(bp)
    core.update(audit_ace_active(bp))
    core["ace_warmup_present"] = True
    core["direct_speech_present"] = True
    core["bridge_inert"] = True
    core["body_actions_disabled"] = True
    return core


def remove_component_by_class(bp, class_substring: str) -> bool:
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    if not subsystem:
        raise RuntimeError("SubobjectDataSubsystem unavailable")

    actor_handle, _root_handle = find_subobject_handles(bp)
    if not actor_handle:
        raise RuntimeError("No actor handle for blueprint subobject delete")

    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    for handle in subsystem.k2_gather_subobject_data_for_blueprint(bp):
        data = lib.get_data(handle)
        if not data or not lib.is_component(data):
            continue
        if lib.is_native_component(data):
            continue
        component = lib.get_object_for_blueprint(data, bp)
        if not component or class_substring not in component.get_class().get_name():
            continue
        deleted = subsystem.delete_subobject(actor_handle, handle, bp)
        return deleted > 0
    return False


def remove_direct_speech(bp) -> bool:
    return remove_component_by_class(bp, "GodfreyDirectSpeechComponent")


def add_exhibition_queue_poll(bp) -> bool:
    return add_component_to_blueprint(bp, QUEUE_POLL_LABEL, QUEUE_POLL_CLASS, "actor")


def configure_exhibition_queue_poll(bp) -> dict[str, str]:
    poll, label = find_component_by_class(bp, "GodfreyExhibitionQueuePollComponent")
    if not poll:
        raise RuntimeError("GodfreyExhibitionQueuePollComponent not found")

    changes: dict[str, str] = {"poll_label": label or QUEUE_POLL_LABEL}
    for names, value, key in (
        (["godfrey_brain_base_url", "GodfreyBrainBaseUrl"], "http://localhost:3000", "GodfreyBrainBaseUrl"),
        (["ace_provider_name", "AceProviderName"], "LocalA2F-Mark", "AceProviderName"),
        (["stream_sample_rate", "StreamSampleRate"], 24000, "StreamSampleRate"),
        (["stream_num_channels", "StreamNumChannels"], 1, "StreamNumChannels"),
        (["poll_interval_seconds", "PollIntervalSeconds"], 1.0, "PollIntervalSeconds"),
        (["b_poll_on_begin_play", "bPollOnBeginPlay"], True, "bPollOnBeginPlay"),
        (["character_actor_tag", "CharacterActorTag"], GODFREY_CHARACTER_TAG, "CharacterActorTag"),
    ):
        if set_prop(poll, names, value):
            changes[key] = str(value)
    return changes


def audit_step7_exhibition_queue_performer(bp) -> dict[str, object]:
    forbidden = _forbidden_components(bp, FORBIDDEN_STEP7_QUEUE_POLL_COMPONENTS)
    if forbidden:
        raise RuntimeError(
            "Exhibition queue performer must not include DirectSpeech/gaze/mic: " + ", ".join(forbidden)
        )

    bridge, _ = find_component_by_class(bp, "GodfreyPerformerAnimationBridgeComponent")
    if not bridge:
        raise RuntimeError("Missing GodfreyPerformerAnimationBridgeComponent")
    if _bridge_auto_activate(bridge) is not False:
        raise RuntimeError("Bridge bAutoActivate must stay False")

    state, _ = find_component_by_class(bp, "GodfreyPerformanceStateComponent")
    if not state:
        raise RuntimeError("Missing GodfreyPerformanceStateComponent")

    warmup, _ = find_component_by_class(bp, "GodfreyAceWarmupComponent")
    if not warmup:
        raise RuntimeError("Missing GodfreyAceWarmupComponent")

    core = audit_core_performer_stack(bp)
    core.update(audit_ace_active(bp))
    core["ace_warmup_present"] = True
    core["direct_speech_absent"] = True
    core["exhibition_queue_on_gamemode"] = True
    return core


def audit_gamemode_exhibition_queue(bp) -> dict[str, object]:
    poll, poll_label = find_component_by_class(bp, "GodfreyExhibitionQueuePollComponent")
    if not poll:
        raise RuntimeError("Missing GodfreyExhibitionQueuePollComponent on GameMode")

    if _get_bool_prop(poll, ("b_poll_on_begin_play", "bPollOnBeginPlay")) is not True:
        raise RuntimeError("Queue poll bPollOnBeginPlay must be True")

    interval = None
    for names in (["poll_interval_seconds", "PollIntervalSeconds"],):
        try:
            interval = poll.get_editor_property(names[0])
        except Exception:
            try:
                interval = poll.get_editor_property(names[1])
            except Exception:
                pass
    if interval is None or abs(float(interval) - 1.0) > 0.01:
        raise RuntimeError(f"Queue poll PollIntervalSeconds should be ~1.0 (got {interval!r})")

    return {
        "queue_poll_present": True,
        "queue_poll_label": poll_label,
        "bPollOnBeginPlay": True,
        "PollIntervalSeconds": interval,
    }


def tag_godfrey_performer_in_level(performer_label: str = "BP_Godfrey_Performer") -> str:
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not eas:
        raise RuntimeError("EditorActorSubsystem unavailable — open Godfrey_World in editor")

    for actor in eas.get_all_level_actors():
        if not actor:
            continue
        if actor.get_actor_label() != performer_label:
            continue
        tags = list(actor.tags)
        if GODFREY_CHARACTER_TAG not in tags:
            tags.append(GODFREY_CHARACTER_TAG)
            actor.tags = tags
        return f"Tagged {GODFREY_CHARACTER_TAG} on {performer_label}"

    raise RuntimeError(f"No level actor with label {performer_label}")
