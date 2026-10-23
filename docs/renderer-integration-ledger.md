# Renderer game integration

Goal: an ordinary, no-probe executable suitable for deployment qualification.
Parent: 7855096. Audit: Jeff's Descent_3MASTERED_Renderer_Integration_Plan.txt,
22 October 2026, pinned to 0cb2f5c with the 7855096 delta reviewed.

## Architecture and explicit policy

- Game state advances in the game-owned logical frame schedule. Publication,
  GPU execution, mirrors, thumbnails and repeated drawing cannot run gameplay
  callbacks or consume simulation RNG.
- Reuse prepared spatial visibility for typed game observations; do not derive
  AI from rasterized surfaces or introduce a second portal traversal.
- Commit one validated observation transaction at the existing post-world,
  pre-foreground boundary, before FrameCount advances. Object compatibility
  stamps use FrameCount, not publication IDs.
- Main and auxiliary views may discover rooms and observe objects. Actual mirror
  producers may observe objects and keep effects alive, but never reveal map.
- Stable first-observation order and per-logical-frame object deduplication are
  intentional changes from legacy duplicate, per-camera AI notifications.
- Main weather visibility is independent of the union of auxiliary/mirror
  admission. Preview, map, thumbnail and foreground views are observation-free.
- Weather emission follows the generation-validated physical main-camera owner,
  including guided/observer cameras, without temporarily changing Viewer_object.
- One sampled world optical state per logical frame is intentional; do not
  reproduce different old main/auxiliary fog interpolation phases.
- Stateful visual producers advance once per logical frame. Their geometry and
  layers are immutable descriptions. Retain meaningful gameplay callback timing,
  including cinematic mutations first affecting the next published world.
- Keep compatibility translation for shipped module/UI APIs at the source edge,
  never as a second world renderer or arbitrary callback inside GPU execution.
- No acceptance by clearing pending bits alone. Each domain needs real producer,
  normal caller, replay/lifetime tests and representative runtime evidence.

## Dependency-ordered completion inventory

- [ ] I01/I02/I03/I04/I08: frame ownership, typed observation commit and weather.
- [ ] I05/I06/I07/I13: game-owned Matcen, event, cinematic and emitter advancement.
- [ ] I09–I17: omitted effects, independent weapon contributions, debris,
  overlays/coronas, weather/sky history, viewer deformation and depth policy.
- [ ] I18–I24: production foreground policy, labels/status/training, shipped
  module canvas and exact ABI/view requests, auxiliary lifecycle and real map.
- [ ] I25/I26: declared UI compatibility boundary and actual save/export callers.
- [ ] I27/I28: independent temporal progression and explicit scene retirement.
- [ ] I29/I30: CI, game-facing integration fixtures and runtime scenario registry.
- [ ] Ordinary launch/load/play/pause/map/cinematic/save/settings/exit qualification
  on the exact promotion candidate, then release-gate and dead-world-code removal.

Keep performance regression checks throughout, with equal-workload caveats until
content integration is complete. Candidate and Previous Candidate are protected.

## First implemented slice — game-owned observations

The real SceneFrame.cpp commit is linked into the CPU contract suite against
minimal live Rooms/Objects/AI/map/weather fixtures. Tests prove full preflight
before any writes, live allocation-generation rejection, once-only commit,
FrameCount65535->65536 wrapping independently of publication IDs, independent
thumbnail publication holes, scene retirement, mirror-without-map admission and
auxiliary exterior admission without setting main weather visibility. All3
suites and the full engine build pass. No observation notification runs in GL.

Prepared mirror/main/auxiliary views feed the transaction before GPU execution.
Commit remains after the world-view schedule and before foregrounds. Obsolete
effect-only return plumbing was removed rather than retained as a second path.
Weather owns its observer input and no longer reads the retired traversal flag.

Private runtime C:/tmp/D3RendererVerification/integration-runtime-20261022 shares
only static HOG/DLL assets and immutable asset directories with scene-view-test;
its executable, saves, pilot, captures and mutable output directories are separate.
The previously delivered particle runtime and launchers were not overwritten.

Actual120-frame main+rear captures passed for003,006 and the user's outdoor save.
003 committed10map rooms and17/18observed objects;006 exercised a real mirror
producer (rooms54/56, objects2048/2088) and committed the game transaction;
outdoor committed11objects including6AI and main_exterior=1. No scene GL errors.
These prove wiring and execution, not final AI/gameplay or rain visual acceptance.
Evidence: integration-observers-{saveg003,saveg006,outdoor-user20261022}-20261022.
All release/pending-domain gates remain. Next: once-only visual event scheduling
and Matcen/cinematic/emitter mutation-vs-description separation.

## Screen overlay event integration

The three real RENDER_EVENT families (damage, energy drain and fusion flash)
now advance in the game-owned pre-publication phase and emit RGBA descriptions.
No event callback runs during renderer execution. A generation-tagged due-list
prevents reallocated slots and zero-delay rescheduling from advancing twice in
one logical frame. Replay is idempotent; unknown callbacks are rejected before
any queue changes. The allocator/scheduler is linked into the CPU contract suite.

Descriptions become owned, shared-topology canvas meshes after the main world
and before auxiliary views/cockpit/HUD. The explicit InScene composition mode
does not complete presentation or clear world depth. Depth-independent canvases
also no longer invalidate the world depth resolve. All three suites and the
engine build pass.

Private control/event captures at1280x720,SSAA2,MSAA4,AO+bloom exercised the real
damage/energy/fusion callbacks on frames20–22 with a rear-view monitor. Visual
inspection confirms world tinting while cockpit/HUD/monitor remain unaffected;
no GL errors. Evidence: integration-frame-events-{control,events}-20261022.
This closes the event producer/caller gap, not the other pending effect families.

## Matcen, cinematic and normal foreground scheduling

Matcen advancement now runs with DoMatcensFrame, once with simulation, not per
camera. It creates/updates persistent VisEffects; const disk descriptions become
ordinary shared-topology world meshes. Recycled VisEffect indices are checked
against allocation generations. Corrected the old object-definition bounds bug:
MAX_OBJECT_TYPES is26 categories, not the910-entry Object_info table. The private
fixture uses real definition267 and exercises the real production update.

Line/gray lightning and sine effects are published with shared constant-opacity
raster-stroke topology. Paths are fixed for the published frame without consuming
simulation RNG. Sine paths are bounded to256points and terminate at their endpoint;
lightning retains its50point bound. Matcen effects now appear in the next world
publication uniformly across views, rather than being born after main drawing
and possibly first appearing in an auxiliary camera. This timing normalization
is intentional. Generic VisEffect billboard depth offsets now honor radius,
VF_NO_Z_ADJUST and the no-radius-bias policy for soft smoke.

Cinematic start/transition/text-mode updates are game-owned. Text, wipe gradient,
fade/white/wacky overlays become ordinary immutable canvases in the explicit
post-HUD phase. Frame callbacks commit once after the current world is frozen
and drawn, so mutations first affect the next publication. A stop/restart
generation prevents a stale callback from a retired cinematic. Post-stop tails
remain active without Cinematic_inuse. Removed the draw-time bitmap rewriting
and the now-unused black/white/temp/wipe bitmap allocations.

Cockpit/reticle/HUD publication no longer depends on three opt-in probe flags.
One captured HUD layout owns cockpit admission too: hidden/guided/zoom/rear/
cinematic/observer views cannot inherit it. A non-dormant cockpit may still
animate closed in fullscreen mode. Independent thumbnail publication has no
main-view foreground consumers. The ordinary launch gate is still retained.

All three CPU suites and the engine build pass. Actual private captures at
1280x720,SSAA2,MSAA4,AO+bloom show the Matcen disk/beam and three line families;
active cinematic text, post-stop fade, restored cockpit, wacky/wipe mode,
transition-before-text and fade-white all execute without GL errors. Deliberate
duplicate game callback commits do not duplicate callbacks. Visual inspection
covered active text, fade tail, restored game, wacky and white outputs. Evidence:
integration-matcen-qualified-20261022 and integration-cinematic-*-20261022.
These do not qualify real mission scripts, multiplayer Matcens or every remaining
effect family. Next: omitted weapon/debris/weather/corona producers, real module
consumers, lifecycle/history and normal no-probe qualification.

## Independent publication identity and world exposure history

Publication IDs are allocated independently of world/object history. A world
publication explicitly names its temporal predecessor; UI/export publications
have no world predecessor and do not advance object stamps. Camera and paused
image-motion history follow that predecessor, not a numeric sequence difference
of one. A skipped world exposure still invalidates history; an unrelated
publication between consecutive world exposures does not. Frame validation
rejects a predecessor that is not older than the publication.

All three CPU suites and the engine build pass. Tests cover real extraction
with interleaved thumbnail/UI identities, camera history, paused exposures and
missing world exposures. The actual SaveGameState caller exports a thumbnail
between world IDs20 and22 in the private integration-history-export-gpu-20261022
capture. GPU readback on frames20–22 confirms camera_history=1 and all265/269/270
visible model pixels have valid motion records. The export and backup are
nonempty; final imagery was visually inspected and no GL errors were logged.
This does not yet qualify reloading the exported save, scene retirement or
the forthcoming real module/UI publication stream.

## Post-observation game overlay publication

HUD/reticle/cinematic/banner descriptions now publish after the game's visibility
commit, separately from world/cockpit extraction. The game captures HUD layout
once; both publications consume it. An allocation-free merge preserves layer
order (including HUD before late cockpit and auxiliary HUD afterward). Overlay
publication allocates an independent ID, has no world predecessor and performs
no second world extraction. This establishes the source-owned callback phase;
it does not yet restore shipped module callbacks or their graphics API.

View-image storage retirement now follows the explicit world predecessor.
Independent publications cannot age that storage; exact publication checks
still reject stale image reads. An actual mirror with an interleaved save keeps
its MSAA storage on frames20–22 (reallocated=0), with camera_history=1. Evidence:
integration-overlay-mirror-20261022, visually inspected.

The compiler also derives reflection-field demand, including sealed room/model
tables. Field-free publications cannot evict the resident immutable field table;
there is no HUD/view-kind exception. CPU tests cover compiled demand, independent
image-storage aging and retained/replayed requests. Engine and all three suites
pass. Normal-overlay output is byte-identical to bf50d11's prior capture, including
after cache qualification; cinematic text/letterbox composition was inspected.
Evidence: integration-overlay-{publication,cinematic,cache-qualified}-20261022.
No GL errors were logged. Full module, lifecycle and deployment gates remain.

## Explicit cross-publication image consumers

View-image inputs may retain an exact earlier immutable producer publication.
Empty producer ownership still means a local dependency in the existing image
graph. Publication/preparation rejects cross-lifetime or non-earlier producers;
the image graph rejects local producers that would overwrite external inputs.
GPU sampling checks the exact producer sequence, never a latest-image fallback.
This is the resource boundary needed to place module world previews among canvas
commands without recursively extracting worlds or invoking callbacks on the GPU.
It does not yet restore the real module canvas API or repair/qualify its ABI.

Engine and three CPU suites pass. The live fixture renders a world image and a
dependent canvas image, then consumes that same product from world ID45 and HUD
ID46. At1280x720,SSAA2,MSAA4,AO+bloom, the two320x180 final-overlay rectangles
match exactly:0of57,600pixels differ. Their image storage is reused. No GL errors;
both the image and final composition were visually inspected. Evidence:
integration-external-image-final-20261022. The initial comparison before cockpit
composition was not a valid pixel-parity oracle: subsequent cockpit coverage
affected the two screen locations differently. Isolating the canvases and then
placing them in FinalOverlay established exact agreement.

Canvas/gauge/cockpit materials also explicitly opt out of world medium transport;
the material compiler owns that policy, without camera-kind exceptions. World
materials retain transport by default. Preview replacement quads ignore texture
alpha explicitly; general effect canvases retain continuous-alpha support.
Neither policy change caused the initial comparison discrepancy. Real fog-roof
captures before/after the policy change are byte-identical and were inspected:
integration-canvas-transport-roof-{before,after}-20261022. These checks do not
claim a new fog appearance fix or complete lifecycle/export/module qualification.

## Source-captured module world-preview ABI

The world-view export now has the exact six-argument type used by shipped DMFC.
It records an independent image request and an ordered opaque canvas quad,
retaining the existing published world. No GPU call or recursive extraction
occurs in that adapter. Source capture owns a bounded frame stack, callback
preview namespace and requested samples. Full absolute rectangles become image
extents and viewport-relative canvas placement. Outside a source canvas the
world export explicitly rejects the request; no implicit last world is used.

The module StartFrame/EndFrame/GetFrameParameters/ResetFacings/depth-state exports
translate inside this source context and retain the UI path outside it. The
game schedules recorded preview images, including planar dependencies, before
their display-canvas consumers. Preview admission remains observation-free.
The other module graphics exports and real custom/HUD-interval callbacks are
NOT restored yet; do not enable them before their translator is complete.

CPU suites pass: exact ABI type, two offset cameras, copied values/ownership,
separate histories, requested MSAA, frame balancing/nesting limits, bounded
preview count, out-of-scope rejection and no output growth on budget failure.
The engine build passes. The capture-only fixture calls actual GetGameAPI table
slots247–252, matching dmfcdllinit.h, with six-argument function pointers. Its
forward/rear previews at(120,120)/(440,150) were visually inspected. Stable
240x135/MSAA4 storage is reused; the resize fixture changes both images to
280x160/MSAA2 on frame10, reallocates once, then reuses on frame11. No GL errors.
Evidence: integration-module-api-20261022 and integration-module-api-resize-20261022.
This is actual exported-table coverage, NOT loaded-DMFC callback qualification
or two-player/menu/observer acceptance. I21/I22 remain open until those callers,
the 2D translator and their ordering/lifecycle are qualified together.
The ordinary inspection path without module probes remains byte-identical to
the prior normal capture: integration-module-normal-20261022 (SHA0C5AFFFD…A79FBB44).

## Per-binding image sampling for source canvas compatibility

The shipped DMFC taunt/icon caller changes filtering around its bitmap draw.
Filtering is now Default/Nearest/Linear on an immutable surface binding, not a
GPU-global mutation from that callback. Text/quad descriptions carry the choice;
text layout cache hits reuse geometry while replacing sampling policy. Binding
caches and draw batching distinguish policies. Asset and view-image consumers
share the existing bounded sampler cache; view images no longer rewrite texture
filter/wrap parameters per draw. Default sampling preserves the prior policy.

Engine and three suites pass. The live canvas fixture samples one2x2 image twice
consecutively in the same pass: nearest yields adjacent edge values255/0, linear
yields129/126. Both were visually inspected with real module previews present;
no GL errors. Without the fixture, the normal capture remains byte-identical
(SHA0C5AFFFD…A79FBB44). Evidence: integration-canvas-sampler{,-normal}-20261022.
This provides the required renderer capability; the source API setters and
actual module callbacks still need to be connected to the complete translator.

### Next bounded slice: actual module canvas producers

Extend ModuleCanvasScope, not the GPU executor. Translate the graphics exports
at Game2DLL.cpp into descriptions when that scope is active. Stock call sites
(excluding pointer declarations in dmfcfunctions.cpp) require HUD/raw text and
metrics, alpha/color/font state, bitmap quads, filtering/wrap, lines and the
chunked Outrage-logo bitmap. DMFC stats uses the preview exports already added.
Unsupported 3D/LFB operations must reject inside capture rather than draw through
the legacy backend; outside capture retain the declared UI compatibility path.

Use fixed bounded text records/byte storage, not per-Printf string allocations.
Preserve explicit grtext_Flush ordering: it emits buffered text, then resets
alpha255/scale1/flags0/color black while retaining font. HUD text uses virtual HUD
coordinates; RenderHUDTextFlags uses screen coordinates. Capture requested
filtering into the new per-binding policy, never the renderer-global setter.
Nested text clipping needs an explicit implementation or a census-backed bound.

SceneHUDLayout::item must invoke custom callbacks at their actual item position,
sharing collection/order with built-in descriptions. Pass their image inputs
into the owning HUD canvas and world requests into GameLoop's source schedule.
Restore EVT_CLIENT_HUD_INTERVAL in the game-owned post-observation preparation
phase. Derive its display order from HUD/cockpit policy, not one guessed layer.
Do not invoke callbacks from GPU execution, image production or thumbnail work.
Then qualify loaded shipped DMFC consumers, not only the existing table fixture.
