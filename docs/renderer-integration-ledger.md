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
