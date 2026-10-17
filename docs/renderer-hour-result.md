# One-hour renderer repair: measured result

2026-09-23 UTC. Work started 04:42:57; authorized deadline 05:42:57.
Workspace: `C:\Users\Jeff\.codex\worktrees\renderer-rearchitecture\DESCENT-3MASTERED`.
Branch `codex/renderer-rearchitecture`, base HEAD `0809c9e`, uncommitted changes.
This report supersedes the implementation/status conclusions in the earlier
performance diagnosis, not its historical measurements.

## Verdict

There is repeatable evidence of a substantially shorter GPU-frame interval in
saveg003, following replacement of per-polygon named-uniform traffic with a
packed draw-record stream. HOWEVER, old/new output is not equivalent: gameplay
feedback is disabled in the inspection build and visible effects differ even
in a near-static comparison. The interval alone does not prove how much of the
old/new GPU difference is a fundamental pipeline improvement. There is NOT an
overall performance win against the old renderer, nor a fully controlled proof
of superiority in the underlying draw path. CPU scene preparation remains
fundamentally too expensive. The user's requested bar has not yet been met.
This build remains an inspection renderer, not gameplay-qualified.

## Measurements

Copied saves/pilot, 1280x720, SSAA1/MSAA0, AO/bloom/motion blur off, per-pixel
lighting, headlight enabled, fixed 1/60 simulation step, seed 12345. Mean of
gameplay frames 15..84 (70 per run); screenshot at frame 90 is excluded.
CPU stage markers use frame numbers 16..85. Stages include their children:
do not add parent and child timings together. Release `/O2` build.

Machine: Ryzen 9 7900X, Radeon RX 6600 present. No fixed-clock/power-state control
was applied. Three final old/new pairs were run alternately, not concurrently.

| saveg003 run | New CPU render ms | Old CPU render ms | New GPU interval ms | Old GPU interval ms |
|---|---:|---:|---:|---:|
| 1 | 20.40 | 3.60 | 0.50 | 1.85 |
| 2 | 20.23 | 3.47 | 0.50 | 1.75 |
| 3 | 21.00 | 3.75 | 0.50 | 1.92 |

New GPU medians are ~0.49 ms; old medians are 1.41, 1.55 and 1.65 ms.
The GPU query interval includes GPU idle gaps caused by command delivery; it is
not a shader-only timer. These results support more efficient GPU feeding in
this scene as a hypothesis, not a universal 3.7x faster renderer claim; unequal
effect workloads prevent attributing the whole improvement to submission.

A final control used a 0.000001-second simulation step to minimize pose/time
divergence (`hour-static-new`, `hour-static-old`, and their `2` repeats).
First pair: new CPU20.91/GPU.54 ms, old CPU3.31/GPU1.46 ms; GPU medians .53/.60.
Repeat: new CPU20.99/GPU.55 ms, old CPU3.33/GPU1.61 ms; GPU medians .53/1.19.
Actual image inspection still shows a bright effect in the old image absent
from the new one. This is a pre-existing migration difference, not proven to
come from feedback alone. This control therefore does NOT establish equivalent
rendering workloads and weakens any claim based solely on mean GPU intervals.

The second scene, saveg000, is an important counterexample: new CPU 19.92 ms vs
old 2.21 ms; GPU means .65 vs .75 ms, but medians .64 vs .34 ms. There is no
convincing GPU win there. Do not cherry-pick saveg003 as a general result.

At the start of this hour the migration measured CPU 82.70 ms / GPU 3.76 ms in
saveg003. Final CPU ~20.54 ms is a ~4x repair during this hour. The previously
reported 214 ms regression predates earlier fixes; do not attribute that whole
reduction to this hour. Neither comparison clears the old renderer's CPU bar.

Final run 2 CPU stages, ms/frame:

| Stage | ms |
|---|---:|
| Scene preparation, inclusive | 15.66 |
| Model preparation | 4.00 |
| Source extraction | 2.56 |
| Publication/validation/compilation | 2.31 |
| Resource preparation | 2.16 |
| Room material preparation | 1.56 |
| Light preparation | .73 |
| All view execution, inclusive | 3.81 |
| Frame table assembly/upload | 1.90 |
| All visibility queries | .68 |
| Draw record construction/upload | .22 |
| Submission/passes | .88 |

The old renderer has no directly equivalent isolated draw-record/submission
timer. Do not compare our .22+.88 to its whole-frame render time as a speedup.

## Architectural changes, not settings reductions

- Published room/model appearance tables are retained immutable storage.
  Mutable builders copy on publication; edits to published tables detach.
  The game can instead explicitly transfer an exclusively owned builder.
- Light assignments and room pulse values no longer invalidate static material
  tables. Frame-local surface bases replace per-surface pointer maps.
- Shared model material palettes separate appearance from polygon topology and
  preserve vertex-alpha policy. In saveg003, model binding records fell from
  51,924 to 15,018; 22,908 of 28,887 room bindings are retained unchanged.
- Resident model geometry is compared without constructing a temporary full
  model. Rigid part bounds are transformed from resident bounds; deformed
  positions still determine their own bounds.
- GPU draws use a packed, aligned std140 record stream, one upload per view,
  rather than dozens of named uniform calls per polygon. Storage capacity is
  retained. Texture sampling parameters change only when their inputs change.
- Validated room/object/model lookup indices belong to the published frame,
  shared by every view instead of rebuilt in each cockpit/HUD/world query.
- Image/reflection dependency metadata and reusable room GPU ranges belong to
  retained appearance; dynamic light indices remain frame-local.
- Final review added an explicit reflection-bake revision and bitmap-format
  invalidation. Pixel-only image updates do not have the same material-policy
  implications as a format change; the latter recompiles appearance.

No visibility pruning of the published world, feature disabling, shadows or
cockpit-art changes were used to obtain these results.

## Correctness evidence and its limits

Full Release build passes. `renderer_scene_tests.exe`: 5,754 checks pass.
New checks cover held-frame isolation, retained-table sharing, exclusive
ownership transfer, model source edits, repaired normals, vertex-alpha palette
separation and rejecting an invalid switch from per-face UVs to a palette.

Clean captures are byte-identical to the pre-change migration in saveg003 and
saveg000. A moving-camera + deformed-model capture is also byte-identical.
Runtime SSAA/MSAA transitions 1/0 -> 2/2 -> 4/4 -> 1/0 completed and returned to
the identical clean saveg003 image. These checks preserve the migration's
existing output; they do NOT establish pixel parity with the old renderer.

Effects profile (SSAA2/MSAA4, AO+bloom): saveg003 is exact. Initial saveg000
comparison had mean RGB error .0144/255, 2.203% of pixels changed, .150% more
than two channel levels, maximum channel difference 40. A repeat with the SAME
new executable exactly matched the pre-change image. This is observed capture
variation, not yet a diagnosed cause; full effects qualification is unfinished.
Clean, moving and effects screenshots were actually viewed, not just hashed.

## What still needs the upstream change

The simulation still exposes mutable rooms/models/textures without a complete
revision/dirty-change boundary. This implementation conservatively scans those
sources and prepares too much whole-level instance state every frame. Animated
materials can invalidate an entire room table. UI/transient mesh preparation,
model variants and some GPU lookup/UV tables still have recurring assembly and
allocation. This is NOT merely a slow optional AO/bloom setting.

The next fundamental work is an explicit mutation-to-render-scene update
contract: asset changes publish new resident data; ordinary frames update
compact instance/animation/light records. Static geometry/material validation
and compilation must leave the ordinary frame path. Dynamic material/instance
fields must not force replacement of an entire appearance table. Keep all
views able to reference the same resident world; do not hide the cost by
camera-pruning extraction or charging preparation outside the render timer.

## Evidence and reproduction

All runtime evidence is under `C:\tmp\D3RendererVerification`:

- `hour-final-new1..3`, `hour-final-old1..3`: alternating matched timing runs.
- `hour-final-save000`, `hour-old-save000`: second-scene timing.
- `hour-start-new`: start-of-hour comparison.
- `hour-before-save000`, `hour-before-motion`: preserved pre-change executable.
- `hour-final-motion`, `hour-final-resources`: movement/deformation and targets.
- `hour-invalidation-final`: final guard-fix build, CPU 20.289 ms / GPU .498 ms,
  identical saveg003 screenshot, full build and all 5,754 checks still passing.
- `hour-before-effects`, `hour-final-effects`, `hour-effects-repeat`: manifests
  and effects images; `hour-effects-comparison` and the repeat comparison files.
- `hour-timing-summary.csv`: mean/median/p95; `hour-stage-summary.csv`: stages.

Local reproducible scripts: `out/profile-renderer.ps1` (parameters Runtime,
Output, Legacy, Save, ExtraArguments, FixedDelta), `out/hour-summary.ps1`, and tracked
`tools/renderer-capture-corpus.ps1` / `tools/renderer-compare-corpus.ps1`.
The profile helper always names the image saveg003.png, including when Save is
saveg000; the run directory and invocation identify the actual save.

Executable used for the three alternating pairs, SHA256:
`14FF6191BC3F455557F303F17B86958F0962B20477475FCE027C5A35BB8FC422`.
Final executable after the invalidation guards, SHA256:
`B6BEA5F64D02453635DCBB3BBA085379F0401B09AD68E2EE92111E180C1C0E7E`.
Old baseline executable SHA256:
`DED4E006BBD00AC30E58E7E0ABF38DCB008A46D68A16F76F1B17B9717176B44A`.

Candidate, Previous Candidate, the old interactive-test package, unrelated main
checkout edits, and untracked lib/gitinfo.h were preserved. No desktop item,
commit, push or pull request was created during this time box.

Optional manual inspection launcher:
`C:\tmp\D3RendererVerification\scene-view-test\Launch Repaired Renderer.cmd`.
Read its neighboring REPAIRED-RENDERER.txt first: unfinished gameplay feedback,
shared graphics preferences and remaining CPU slowdown all still apply.
