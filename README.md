# unrealperformer

Clean MetaHuman baseline for the Godfrey live-audio exhibition performer (UE 5.6).

## Known-good checkpoint (Phase 0)

- **Project:** `UnrealPerformer` (`D:\UE Projects\UnrealPerformer`)
- **Level:** `Content/Godfrey_World.umap`
- **MetaHuman:** Kristofer (Christopher from Quixel Bridge) — all migration phases use this character, not Erno
- **Test:** Viewport zoom in/out — clothing stays stable (no shirt explosion)

## Open in Unreal

1. Epic Games Launcher → UE **5.6**
2. Open `UnrealPerformer.uproject`
3. Level: **Godfrey_World**

## Revert to this state

```powershell
git checkout main
git pull
```

Or restore a tagged release:

```powershell
git checkout phase0-kristofer-stable
```

## Not in this repo

- **`Content/MetaHumans/`** (~1.3 GB) — stock Quixel Bridge assets; re-import locally (see `Docs/MetaHumanAssets.txt`)
- `Saved/`, `Intermediate/`, `Binaries/` — regenerated locally
- `Test_Live_Audio` (legacy contaminated project) — reference only, not versioned here

This keeps git small and tracks only **your** customizations: levels, config, and future exhibition wiring.

## Migration plan

See `Docs/MigrationPlan.txt` for phased steps to reintroduce ACE, C++, and exhibition wiring.
