# Renderer recovery checkpoint — 2026-09-22

## October 21 resumed work — supersedes restart pause below

Latest: compact terrain patch/material separation and support/rest bounds are
built and image-verified. Current binary `out/perf-checkpoints/terrain-bindings.exe`.
Latest paired corpus has three scenes ahead, five behind; exact evidence and
sampling-helper status are at the top of renderer-cpu-ownership-repair.md.
Read-only sampler session may still be finishing; do not overwrite a running
verification executable. Last usage check: 37% remaining.

User asked to continue, aiming to stop at 0% included usage; a small accidental
credit overrun is acceptable, not deliberate credit use. Last check 38% remaining.
See the top of `renderer-cpu-ownership-repair.md` for completed verification and
fresh paired timings (still seven of eight behind pre-rewrite).
Current verification binary: `out/perf-checkpoints/object-history.exe`, deployed
only to `C:/tmp/D3RendererVerification/scene-view-test`. Mesh alpha summary,
positive lightmap compatibility proof reuse, contiguous publication uniqueness
checks and compact source transform history are built/tested. Dense/wall/combined
probe images match the preceding checkpoint exactly. No captures are pending
at this checkpoint. Continue improving CPU preparation; do not claim completion.

## October 21 restart pause — latest state

User requested a yield to restart. Do not resume until requested. Last included
usage check had 39% remaining. Performance target below remains unmet.

Latest checkpoint retains short text layouts as immutable mesh assets (bounded
64-entry cache, font-layout revision invalidation). Generic GPU mesh storage is
now keyed by asset lifetime/revision with weak ownership, so two retained revisions
of a reused source slot cannot overwrite one another within a frame. Expired GPU
storage is retired at frame boundaries; context teardown clears it explicitly.
Tests cover text reuse/invalidation, held assets, eviction, and GPU revision ownership.
Build and scene tests passed; logs: `out/cpu-text-assets-build.log` and
`out/cpu-text-assets-tests.log`.

Current verification executable and build matched SHA256
`951BAAC39209F702C2B4C89E891BC34317060787F5BDF9695506640197D66F0F`.
Retained executable: `out/perf-checkpoints/text-owned.exe`. No verification game
process was running at pause. Candidate and Previous Candidate remain untouched.

Completed captures under `C:/tmp/D3RendererVerification`:
`cpu-text-owned-saveg003-20261021`, `cpu-text-owned-saveg005-20261021`,
`cpu-text-owned-probe-20261021`, `cpu-text-msaa-owned-20261021`, and control
`cpu-text-msaa-bounds-20261021`. The last two use SSAA2/MSAA4/AO/bloom.
Final image comparisons and timing extraction are PENDING: do not claim these
final captures demonstrate parity or a speedup. Earlier text-only captures before
the GPU ownership fix matched bounds images for save003/save005; that does not
substitute for checking the final build. Resume with these existing results,
not by repeating the captures unnecessarily.

Remaining architectural work includes the whole-world preparation cost (especially
save005); camera-culling source snapshots would violate independent-view support.
The generic mesh cache fix does not establish revision-safe ownership for all other
GPU asset cache types. Preserve the Level 2 object-lightmap compatibility guard.
Never stage unrelated `lib/gitinfo.h`.

## October 21 continuation (supersedes timings below)

Active target is a **marked improvement over every pre-rewrite CPU baseline**,
not mere parity, without compromising architecture. User explicitly requested
overnight continuation, stopping before included usage exhausts (no purchased
credits). Use the read-only usage tool periodically; first check reported 40%
remaining. Do not wait for 0% if a safety margin is needed to avoid credit use.
Latest no-marker corpus has one trial ahead and
seven behind; dense total ~3.068 vs 2.574 ms, wall-facing save005 ~2.005 vs
0.967 ms. This is not completion or gameplay qualification. See the top of
`renderer-cpu-ownership-repair.md` for exact measurements/capture paths.
The inspection runtime is `C:/tmp/D3RendererVerification/scene-view-test`;
Candidate, Previous Candidate, main checkout and Desktop remain untouched.
No shadows or cancelled cockpit art work resumed.

Current repairs include retained table dependency unions, frame storage reuse,
lazy/rest model poses, packed light candidates and animation inputs, exact
procedural kernels, separate samplers, and reusable image upload storage.
All eight latest current images match the earlier procedural-kernel checkpoint;
this is not an assertion of pre-rewrite visual equivalence. `lib/gitinfo.h` is
untracked user/generated material; do not add it to checkpoints.

## October 17 continuation (supersedes the performance status below)

October 20: frame-owned image/resource bindings now preserve material tables
across pixel updates and animation. Eight clean captures match exactly;
5,774 contract checks pass. Fresh CPU baselines and current measurements are
at the top of renderer-cpu-ownership-repair.md. Still slower in all eight cases.
User has asked to continue until every CPU baseline beats pre-rewrite, without
compromising architecture. Do not call the partial resource repair completion.

Latest CPU work: see [renderer-cpu-ownership-repair.md](renderer-cpu-ownership-repair.md).
Producer-owned image/geometry revisions and visibility-driven transient uploads
are implemented. Dense preparation remains roughly 9–10 ms: not fixed overall.
5,766 contract checks pass. No shadows or cancelled cockpit art work resumed.

See [renderer-gpu-repair.md](renderer-gpu-repair.md). GPU schedule repair built,
eight-save visual comparisons inspected, nine paired settings benchmarks run,
AO debug and no-AO motion/resource transitions checked. CPU preparation remains
slow; dense saveg003 prepares 504 model variants before visibility. Canonical
quad assets now have scene-lifetime ownership and surface publication uses
contiguous scratch instead of per-dependency tree allocations. Images unchanged
in the dense CPU fixture. Main checkout/Candidate/Previous Candidate untouched.

## Latest status: one-hour performance repair, 2026-09-23

See [renderer-hour-result.md](renderer-hour-result.md) for current implementation,
measurements, exact runtime paths, build identity and remaining work. The
historical status below predates the performance failure and its repair.
Current result: repeatable shorter GPU interval in saveg003, but unequal old/new
effect output prevents attributing it entirely to pipeline efficiency. CPU
rendering is still ~20.5 ms versus ~3.6 ms old; the requested old-renderer win
has not been established. 5,754 checks
pass. Changes remain uncommitted in this same isolated worktree. Do not resume
shadows/cockpit art or treat the inspection build as gameplay-qualified.

## Current focused result (supersedes unresolved-bug notes below)

The TelCom black monitor was an early capture of its authored power-on animation,
not failed map geometry. Capture now waits until the fully open phase was drawn.
Temporary intermediate screenshot instrumentation has been removed.

Moving the warning into the monitor exposed a real scene/canvas viewport leak:
ExecuteSceneView now restores the incoming GL viewport after its own execution.
This fixes subsequent canvas placement without a TelCom-specific renderer patch.

Full build `out/telcom-viewport-build.log` passes. `telcom-viewport-fixed/saveg003.png`
was visually inspected: map, ship, menus, warning and surround are visible and
correctly placed. `telcom-viewport-regression` checks the normal effects-profile
HUD/messages/cockpit/reticle against `scene-hud-policy-normal`. Both capture runs
completed normally. No broad test expansion or other migration domain was started.
The earlier checkpoint below is historical evidence, not the current active scope.

The user requested recovery after repeated `{"detail":"Bad Request"}` messages.
Renderer changes are paused for this recovery handoff. Tool calls were responding
normally when this checkpoint was written; that does not establish that the app's
transport/context problem is resolved. No game processes remained running.

## Workspace and authority

- Work ONLY in `C:\Users\Jeff\.codex\worktrees\renderer-rearchitecture\DESCENT-3MASTERED`.
- Branch: `codex/renderer-rearchitecture`; HEAD: `0809c9e`.
- Original `C:\Repos\DESCENT-3MASTERED` has unrelated user/cockpit edits; preserve it.
- Preserve untracked `lib/gitinfo.h`; it is not part of this work.
- Candidate and Previous Candidate remain untouched. Runtime copies only under
  `C:\tmp\D3RendererVerification\scene-view-test`.
- No shadows until renderer verification. No cancelled cockpit art work.
- Keep AO eligible-radiance approach; defer relative performance tradeoffs.
- User wants persistent implementation, upstream migration and actual screenshot
  inspection. Do not treat build/capture success as visual acceptance.
- All seven PendingDomains remain set. This is NOT gameplay-qualified.
- Do not proactively spawn agents. No applicable AGENTS.md was found in the
  repository and ancestor scans; recheck if the workspace changes.

## Current uncommitted implementation

Automap's immediate room/terrain/object renderer has been removed from
TelComAutoMap.cpp. SceneAutomap.cpp publishes discovered rooms, secret/unexplored
portal appearances, baked corner colors, terrain, player ship and marker disks.
SceneAutomapGeometry.cpp batches appearance/detail groups; room-vertex color
seams are preserved. Mesh depth_reference/maximum_depth makes detail cutoffs
view-local (300/600), not shared draw state. Twelve tests cover these contracts.

PrepareGameStaticModel in SceneModels.cpp publishes models without a gameplay
object. Facing/glow helpers now accept optional owners; standalone attachments
share the same preparation, with explicit collection/model-instance/variant.
The actual automapship.oof includes glows: the first runtime rejected them,
prompting this correction. Do not revert to silently dropping attachments.

PrepareGameSceneResources was factored from normal preparation and is used by
automap. resolveMaterial now accepts texture-less surfaces regardless of source
domain (previously only models), allowing baked-light-only automap terrain.
PrepareGameInspectionBanner is shared by gameplay and map preparation.

Diagnostic fixtures:

- AutomapProbe: replaces normal presentation with a deterministic map frame,
  camera behind/above player, no gameplay feedback.
- AutomapTerrainProbe: map terrain with an elevated terrain camera.
- AutomapTelcomProbe: enters actual TelCom via the pending capture boundary,
  captures its composed map screen, exits automatically. Capture function is
  exported in gameloop.h. TelCom.cpp captures before swap when map callback is
  active; the callback uses deterministic .375 display time for this fixture.
- Harness manifests/comparison guards distinguish these fixtures.

Actual TCAMCallback uses a Scene-target Automap view, then the existing 2D menu,
then an inspection banner Scene canvas. The old 2D TelCom shell remains; this is
not a world fallback. The menu/banner integration is NOT visually correct yet.

## Verified evidence

Full game build `out/scene-automap-telcom-build2.log` passed. Tests:
`out/build/scene-tests/renderer_scene_tests.exe` reports **5736 checks**.

Captures under `C:\tmp\D3RendererVerification`:

- `scene-automap-interior`: FAILED; standalone ship glow rejection. Preserve.
- `scene-automap-owned/saveg003.png`: passed and visually inspected; room geometry
  and ship including glows are present.
- `scene-automap-terrain`: FAILED missing texture -1 in material resolver. Preserve.
- `scene-automap-baked-terrain/saveg000.png` and `saveg002.png`: passed and inspected;
  baked-only terrain is visible. Generic full-detail terrain differs from old
  coarse automap LOD; appearance/performance parity is not yet established.
- `scene-automap-telcom/saveg003.png`: capture/clean exit passed BUT VISUAL FAILURE:
  640x480 TelCom surround/title visible; main map screen black, menu missing,
  inspection banner obscured by shell. The map query logs 44 surfaces. This is
  the CURRENT unresolved integration bug, not a passing automap acceptance.
- `scene-automap-regression`: effects save003 with HUD/messages/cockpit/reticle
  passes and is pixel-identical to `scene-hud-policy-normal`.
  Report: `scene-automap-regression.json`.

## Resume investigation at this boundary

Determine where TelCom map/menu pixels disappear. Do not just tweak scene geometry:
standalone map works, but actual legacy UI composition obscures it.

Relevant code inspected:

- TelcomRenderScreen in TelCom.cpp: StartFrame -> background effects -> map
  callback -> overlays -> text flush -> TelcomDrawScreen -> EndFrame -> capture -> Flip.
- TCAMCallback in TelComAutoMap.cpp: nested StartFrame -> renderView map ->
  EndFrame restoring parent -> TCAMDrawMenu -> FlushTextLayer -> renderView banner.
- game.cpp EndFrame restores parent StartFrame, which clears depth.
- gl_scene.cpp ExecuteSceneView sets raw GL blend/depth/cull state and ends with
  glDisable(GL_BLEND), glEnable(GL_DEPTH_TEST), ClearBoundTextures and VAO unbind.
- gl_main.cpp SetAlphaType has a cached early return; SetAlwaysAlpha(false)
  enables blending, but is only reached when type changes. OpenGL_blending_on
  is another cache. Investigate coherence at scene-to-canvas boundary rather
  than adding TelCom-specific state patches. This is a lead, NOT a proven cause.
- gl_draw.cpp DrawSimpleBitmap sets AT_CONSTANT_TEXTURE, alpha255, LS_NONE,
  CM_MONO, OT_NONE then DrawScaledBitmap. TelCom shell uses chunked bitmaps.
- Screenshot readback may change shader/bindings for SSAA; avoid adding intermediate
  captures without considering those side effects. Existing failed TelCom is clean
  SSAA1/MSAA0, so a controlled intermediate capture could locate the overwrite.

After fixing: inspect real TelCom screenshot, normal regression, then checkpoint
and continue remaining domains. See renderer-migration-progress.md and
renderer-rearchitecture-plan.md for broader work. Major remaining domains include
module ABI draw translation, cinematic/UI publication, gameplay visibility feedback
qualification/commit, terrain LOD, rare content and deletion/performance audit.

## Commands

Use PowerShell login:false, explicit isolated workdir. Build:

    cmd /c 'C:\PROGRA~1\MICROS~3\2022\COMMUN~1\VC\Auxiliary\Build\vcvars64.bat && cmake --build out/build/renderer --target Descent3Mastered --parallel 8 > out/<unique>-build.log 2>&1'

Copy executable and hog ONLY to scene-view-test after build/captures finish.
Run tools/renderer-capture-corpus.ps1 with Runtime above, Seeds
`C:\tmp\D3RendererVerification\seeds`, a NEW unique Output, SceneInspection,
saveg003, clean profile and AutomapTelcomProbe. Normal regression uses effects,
HudProbe/HudMessageProbe/CockpitProbe/ReticleProbe. Never parallelize captures;
they share copied runtime/config. Keep corpus outputs immutable.

Native exit 1 is normal only with capture saved and clean-shutdown markers.
No processes need terminating at this checkpoint. No files need cleanup/deletion.
