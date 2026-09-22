# Renderer migration checkpoint

## Scope and status

The user approved renderer rearchitecture and background save-game comparisons,
with an explicit gate: **do not touch shadows until the new renderer is verified**.
The replacement has started at the game/renderer scene boundary. The development
game target is intentionally **not buildable** while downstream code is migrated.
The independent scene/extraction tests build and pass. No new renderer executable
has been produced or visually verified, and no shadow implementation was added.
Do not mistake the frozen baseline results below for new-renderer results.

## First replacement cut

- `renderer/Scene.h/.cpp`: an engine/GL-independent owned scene contract,
  renderer-owned room assets with lifetime/revision identities, immutable
  presentation publication, explicit camera/view requests, and visibility
  reports. Old published frames retain assets safely across update/unload/reuse.
- `Descent3/SceneExtraction.h/.cpp`: reads all used rooms and all live objects,
  independently of camera visibility. Publishes room geometry only when source
  geometry/UVs change, snapshots material/portal/fog bindings per presentation,
  and tracks object transforms by allocation handle rather than slot. History
  advances once per successful extraction, not per camera. This does NOT yet
  prepare polymodel poses, effects, lights, materials or terrain.
- Removed the implementation of `GameRenderWorld`. Main and auxiliary views now
  receive the same extracted presentation. Rear-view orientation is copied into
  the view, not temporarily written into a simulation object. Small views no
  longer swap `Viewer_object`, restore global g3 camera state, or temporarily
  disable global dynamic lighting; auxiliary lighting policy is explicit.
- Room unload resets the scene lifetime and presentation history. The original
  `RenderRoomClustersFreeLevel` cleanup (including the Level 2 regression fix)
  remains in place. The scene store's destruction order allows atexit room
  cleanup; it is not a late-constructed function-local static.
- Pending scene domains reject execution. `IRenderer::RenderSceneView` is pure
  virtual, with no legacy replay implementation. Old `GameRenderWorld` and
  `RenderMine` declarations are compile-time barriers, not selectable fallbacks.

Geometry revision detection currently compares source geometry fields once per
extraction because the simulation has no authoritative geometry revision counter.
This scans, but does not clone, unchanged level geometry. Its CPU cost is NOT yet
benchmarked. It must be measured before acceptance. Asset publication must not
be changed to pointer equality or a hash-only correctness test.

### Verified first-cut tests

Standalone target `tools/renderer-scene-tests` compiles the real scene core and
real game extraction against minimal simulation globals, without linking any
draw, RNG, GL or game-runtime implementation. Release checks are ordinary test
conditions, not `assert` removed by NDEBUG.

```text
cmake -S tools/renderer-scene-tests -B out/build/scene-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/scene-tests
ctest --test-dir out/build/scene-tests --output-on-failure
```

Checks cover immutable asset/frame ownership, unchanged-geometry sharing,
geometry/material updates, removal and slot/level reuse, invalid topology/NaNs,
duplicate identities, incomplete bindings and execution refusal, presentation
history, object allocation generations, clock rewind, non-mutating rear views,
auxiliary policy and exclusion of transient camera/specular face marks.

### Explicit build blockers / migration queue

The full game build was attempted with Ninja `-k 0` to expose all compilation
failures rather than stop at the first. Log: `out/renderer-migration-build.log`.
The observed blockers are:

1. `renderer/renderer.cpp`: GL4 is abstract until the new scene executor exists.
2. Main/small-view callers: `CommitGameVisibility` is deleted pending the explicit
   feedback implementation. No guessed feedback policy is being used.
3. `gamesave.cpp`: save thumbnails still call the deleted world entry; migrate
   them to an explicit non-gameplay view without advancing main-view history.
4. `Game2DLL.cpp` exports `GameRenderWorld` in its function table. Preserve this
   slot and binary calling convention with the module adapter. **Do not remove
   the slot or restore the old core implementation to silence the error.**
5. `terrainrender.cpp` calls deleted `RenderMine`; split terrain/portal visibility
   from execution and port to the scene owner.
6. `render.cpp` still defines the deleted mine function. Its useful traversal and
   appearance algorithms must be migrated, then obsolete execution removed.

The material, object preparation, terrain, effects, lights, decals and cockpit
domain bits remain pending. GPU/pass/compatibility migration has NOT happened.
Most old implementation files remain as migration source; removal is not done.
Multiplayer name queuing formerly inside GameRenderWorld must move to the
feedback/HUD preparation step. No screenshot comparison is meaningful until the
new executor can render; keep the accepted baseline frozen meanwhile.

## Locations and provenance

- Original checkout: `C:/Repos/DESCENT-3MASTERED`, master `664aefc`.
- Implementation worktree:
  `C:/Users/Jeff/.codex/worktrees/renderer-rearchitecture/DESCENT-3MASTERED`.
- Branch: `codex/renderer-rearchitecture`.
- Baseline commit: `bfb59647a3c3b4a451c264b3941c441573ecc061`.
  This preserves copies of the original working tree's 18 modified/untracked
  files, including unrelated user work. It is not a commit in the original
  checkout. Those unrelated changes must not be attributed to this migration.
- All runtime/capture artifacts: `C:/tmp/D3RendererVerification`.
- `baseline-runtime` is a copy of Candidate, not the live installation.
- `source-baseline` is a separate copy with the baseline source build's executable
  and shader HOG. Its source includes the preexisting capture-headlight change.
- `seeds` contains copied pilot and all eight saves. The harness restores copied
  pilot/save/counter data into the isolated runtime before each capture. The
  original pilot/saves are never used as writable runtime data.

Candidate executable SHA-256, verified unchanged after testing:
`EDAB17AAD7BA9A60EFCA54F72789F3D173446B916EB1631FB18FCA9FFCA1EFD4`.
Candidate shader HOG SHA-256, also unchanged:
`7BF1AC01E9E178E5D6D382C5D2013100F5A8366EE4C86D5F784189828EC01CCB`.
Prev Candidate was not touched. The cancelled cockpit refinement was not resumed;
its existing source/assets were preserved, and the test toggle was not enabled.

## Build

The source-baseline Release build completed with existing compiler warnings.
From a VS 2022 x64 developer environment in the isolated worktree:

```text
cmake -P tools/UpdateRevision.cmake lib/gitinfo.h
cmake -S . -B out/build/renderer -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/renderer --target Descent3Mastered --parallel 8
```

The first step is currently necessary on a fresh checkout: dmfc can compile
before revision_check creates gitinfo.h. This existing build dependency issue
was not disguised by copying an old generated header. lib/gitinfo.h is generated
and is intentionally not committed with this checkpoint. No install command ran.

## Capture harness

`tools/renderer-capture-corpus.ps1` runs a copied installation in hidden/background
mode, logs successful headlight/seed activation and shutdown, hashes executable,
HOG, pilot and saves, and records commands/settings/results. Output directories
are immutable. Global settings are read and checked for change, not rewritten.
The script accepts only runtimes directly under the dedicated verification root.
It times out and kills only a process it launched itself.

Example from the worktree:

```powershell
.\tools\renderer-capture-corpus.ps1 `
  -Runtime C:\tmp\D3RendererVerification\source-baseline `
  -Seeds C:\tmp\D3RendererVerification\seeds `
  -Output C:\tmp\D3RendererVerification\unique-new-output
```

`tools/renderer-compare-corpus.ps1` requires matching save hashes, pilot, profile,
frame, RNG seed and registry settings before reporting per-pixel RGB errors.
It dispatches image reading to Windows PowerShell/.NET Framework for System.Drawing
compatibility; any execution-policy override is limited to that child process.
It has no automatic visual acceptance threshold.

Captures use 1280x720, gameplay frame 60, dt 1/60, seed 12345, forced headlight,
per-pixel lighting, no audio and no motion blur. The effects profile explicitly
uses 4x MSAA, 2x SSAA, AO and bloom. Other live preferences are recorded in
settings.json; if those preferences change, do not compare corpora as equivalent.

## Verified results

The eight headlight-on source-baseline frames were visually inspected, including
portals, room geometry, bright bloom, green fog, objects/effects, original Pyro
cockpit/HUD, the stalactite, and close geometry. They are all interior views; this
does not cover terrain, every ship, module callbacks, all blend modes or temporal
behavior. Those remain required by the rearchitecture plan.

The authoritative repeated effects runs are `seeded-effects-a` and
`seeded-effects-b`. All 16 captures loaded, rendered, saved and exited normally.
The comparison report is `seeded-repeatability.json`.

| Save | Mean RGB absolute error (0-255) | Pixels with any channel difference >2 |
| --- | ---: | ---: |
| saveg000 | 0.000001085 | 0% |
| saveg001 | 0 | 0% |
| saveg002 | 0.001082538 | 0.015842014% |
| saveg003 | 0 | 0% |
| saveg004 | 0 | 0% |
| saveg005 | 0 | 0% |
| saveg006 | 0 | 0% |
| saveg007 | 0 | 0% |

saveg000 differs at three pixels by at most one channel level. saveg002's maximum
channel difference is six. This residual is measured but not conclusively
diagnosed; do not hide future coherent artifacts behind a global noise threshold.

## Lessons from setup (preserve this evidence)

- A long worktree runtime path hit a legacy path-length limit before level load.
  Only the copied test installation was moved to the short verification root.
- Native WinMain returns 1 on normal completion. Accept it only together with the
  capture and clean-shutdown log markers; zero is not the normal success code.
- The preserved Candidate did not log support for capture-headlight. Its first
  image is not a valid forced-headlight golden. Use the rebuilt source baseline.
- Unseeded runs showed major differences in saveg003 and some in saveg002.
  Existing `-capture-seed` support resolved nearly all of that variation. Earlier
  `source-effects-*` and `source-clean-a` runs lack this control and are diagnostic
  artifacts, not the accepted deterministic baseline.
- Save loading increments a sibling .cnt file. Early tests loaded copied seeds
  directly; the harness was corrected to restore/load runtime copies, and seed
  counters were restored from the original save directory. Original saves and
  counters were never loaded by the test process.

## Next work / no completion claim

Continue the replacement through scene preparation, view-local visibility,
feedback and GPU execution. Resolve the explicit build blockers above without
reconnecting the removed world draw path. Remaining coverage fixtures, rare
materials, module behavior and performance budgets are still acceptance work;
do not restart with more baseline preparation instead of implementing.
Keep the baseline runtimes/captures frozen while the development branch is broken.
Renderer verification and deletion audit must complete before any shadow work.
