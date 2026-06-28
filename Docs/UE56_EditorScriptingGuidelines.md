# UE 5.6 Editor Scripting Guidelines (Godfrey_World)

Basis for all future programmatic changes in this project.  
Engine: **UE 5.6.1** | Map: **Godfrey_World** (World Partition + One File Per Actor)

Official references:
- [EditorActorSubsystem (Python 5.6)](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorActorSubsystem?application_version=5.6)
- [WorldPartitionBlueprintLibrary (Python)](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/WorldPartitionBlueprintLibrary)
- [One File Per Actor](https://dev.epicgames.com/documentation/en-us/unreal-engine/one-file-per-actor-in-unreal-engine)
- [Level.use_external_actors](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/Level?application_version=5.6)

---

## Rules for this project

### 1. Actor placement: editor UI first for WP levels

Per Epic OFPA docs, external actors are saved per-actor files under  
`Content/__ExternalActors__/<LevelName>/`. Cell assignment follows **actor bounds and placement**, not script intent.

**Do not spawn exhibit actors via `UnrealEditor-Cmd` headless.**  
Use the editor UI (Place Actors panel) or run Python from **Tools → Execute Python Script** with the level open and the exhibit region loaded.

`EditorActorSubsystem.spawn_actor_from_class` doc: *"The actor will be created in the current level"* — in WP, that still maps to an external-actor cell based on world location and bounds.

### 2. Use documented Python APIs only

| Task | API (5.6) |
|------|-----------|
| Spawn / destroy actors | `unreal.get_editor_subsystem(unreal.EditorActorSubsystem)` |
| List loaded actors | `EditorActorSubsystem.get_all_level_actors()` — **loaded only**, excludes pending kill |
| WP actor descriptors | `unreal.WorldPartitionBlueprintLibrary.get_actor_descs()` |
| Load/unload by descriptor | `WorldPartitionBlueprintLibrary.load_actors()` / `unload_actors()` |
| Level uses external actors | `Level.use_external_actors` property |

**Do not use** deprecated or missing APIs we hit in this project:
- `World.get_world_partition()` — not exposed in Python
- `WorldPartitionEditorBlueprintLibrary` — not in 5.6 Python API
- `EditorLevelLibrary.get_editor_world()` — deprecated; prefer editor subsystem patterns

### 3. Verification before any user test

Always run headless (read-only gate):

```powershell
UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
  /Game/Godfrey_World
  -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/verify_godfrey_exhibit.py"
  -unattended -nop4 -nosplash -log
```

Read `Saved/GodfreyExhibitSanity.txt`. **Do not ask the user to test if RESULT is FAIL.**

Optional audit:

```powershell
... -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/audit_godfrey_world_actors.py"
```

### 4. Scripts by purpose

| Script | When | Headless OK? |
|--------|------|--------------|
| `verify_godfrey_exhibit.py` | Pre-flight gate | Yes (read-only) |
| `audit_godfrey_world_actors.py` | Diagnostics | Yes (read-only) |
| `fix_godfrey_world_partition_load.py` | Load exhibit region | Editor preferred |
| `restore_godfrey_world_lighting.py` | Lighting restore | Editor preferred |
| `save_stage_backdrop_transform.py` | After user places backdrop in UI | **Editor only** |
| `restore_stage_backdrop.py` | Re-apply saved transform | **Editor only** |
| `brighten_stage_backdrop.py` | Material + fill light | **Editor only** |
| `provision_phase6_godfrey_performer_asset.py` | Phase 6 step 1a — duplicate shell BP | Yes (asset only) |
| `setup_phase6_godfrey_performer_shell.py` | Phase 6 step 1 — swap performer in level | **Editor only** |
| `setup_phase6_step2_animation_bridge.py` | Phase 6 step 2 — add inert animation bridge | Yes (BP asset edit) |
| `setup_phase6_step3_body_anim_instance.py` | Phase 6 step 3 — GodfreyBodyAnimInstance on Body | Yes (BP asset edit) |
| `setup_phase6_step4_ace_audio_curve_source.py` | Phase 6 step 4 — ACE audio curve source (inactive) | Yes (BP asset edit) |
| ~~`provision_stage_backdrop.py`~~ | **Deprecated** — caused WP cell splits | **No** |

### 5. Source control (OFPA)

Epic: *"When using OFPA, content and Actor files should be submitted to source control from within the Editor."*  
Save level (`Ctrl+S`) after UI or editor-script changes.

### 6. Phase 4 checkpoint (`phase4-exhibit-stable` / commit `f7ff111`)

Known-good exhibit:
- BP_Kristofer, Exhibit_Floor (single package `5/9Z/…`), fill lights, Lumen, post-process
- **No** Stage_Backdrop / Stage_SkySphere (user adds backdrop via UI for Phase 5 retry)

Rollback:

```powershell
cd "D:/UE Projects/MetaHuman_Baseline_Test"
git checkout f7ff111 -- Content/__ExternalActors__/Godfrey_World/ Content/Godfrey_World.umap
Remove-Item -Force Content/__ExternalActors__/Godfrey_World/2/O2/KKQRBJFPGMYGCN8CQXNTH2.uasset  # duplicate floor
```

---

## User workflow: add backdrop via UI

1. Open `Godfrey_World`, console: `wp.Editor.LoadAllCells`
2. Place **Plane** (Place Actors → Basic → Plane) near Kristofer
3. Label: `Stage_Backdrop`, material: `MI_Stage_Backdrop` or `M_Redgate_Background`
4. Optional: add **Rect Light** labeled `Stage_BackdropFill`
5. Save level (`Ctrl+S`)
6. Run `save_stage_backdrop_transform.py` from editor to backup transform to `Config/StageBackdropTransform.json`
