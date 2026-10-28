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

## Implemented source module canvas and loaded consumers

The actual GameAPI table now translates buffered/raw HUD text, metrics,
font/color/alpha state, bitmap/chunked quads, fill/line and sampling state during
a game-owned ModuleCanvasScope. The recorder has fixed text bytes/records and
bounded command/preview/nesting budgets; text copies bytes/style at submission,
emits at Flush, and preserves the old reset behavior. Nested glyph clipping
crops positions and atlas UVs and participates in the geometry cache key.
Unsupported 3D/LFB exports reject during capture, never fall through to world
GPU callbacks. Outside capture the separately declared legacy UI boundary is
unchanged. No module callback is installed in the GPU executor.

Custom HUD items execute at their normal array position in the post-observation
source overlay publication. HUD interval order derives from cockpit/view policy:
before ordinary HUD without cockpit, above cockpit with cockpit, and after HUD
for observer/letterbox. Hidden and clean-screenshot paths suppress it. The ABI's
window/aspect/viewer variables are source-scoped and restored, including unwind.
The public HUD struct layout is unchanged. Private registration identities and
module owners retire callbacks before DLL unload and prevent slot-reuse aliasing.

CPU contracts cover text byte/style ownership, Flush/reset, metrics, bitmap/HUD
coordinates, nested glyph/bitmap clipping, line policy, bounded modes and the
HUD module order. All three suites and the engine build pass. The ordinary003
capture remains byte-identical (SHA0C5AFFFD…A79FBB44).

Real shipped Anarchy and Co-op DLLs were loaded through the native loader in a
private offline fixture. A synthetic remote player enters through the actual
module event, and its real score callback draws name/score. F7 invokes the actual
stats panel; Co-op emits its real309x309 MSAA4 player-view request. F6 draws the
actual Anarchy menu. These were visually inspected; all captures are GL-clean.
The fixture leaves Game_mode single-player, disables its packet-send exports and
database persistence, and is NOT evidence for transport/two-process gameplay.

The unload fixture freezes the Co-op panel, unloads code and frees its source
bitmaps before executing that publication. Frame9 still draws panel/logo/view;
frame10 has no module UI. Reload allocates a new owner/view identity, allocates
its image once, then reuses it. Evidence under C:/tmp/D3RendererVerification:
integration-module-live-{scores,stats}-20261022,
integration-module-coop-stats-20261022, integration-module-menu-20261022,
integration-module-{unload,reload}-20261022 and
integration-module-canvas-normal-final-20261022.

I21/I22/I28 remain pending broader ordinary multiplayer/observer/replay and
scene-transition qualification. A remaining upstream issue is conservative
CallGameDLL invalidation of all room appearances, including read-only HUD events;
do not claim the restored module path is performance-qualified or simply remove
that invalidation without replacing the writable-room mutation contract.

## Allocation-owned fragments (I12 producer integration)

RT_SHARD and RT_SPLINTER now publish their live game producers' geometry. Local
vertices/UVs become an allocation-owned immutable mesh once, keyed by full object
handle and scene lifetime; subsequent publications carry pose and alpha only.
SceneAssets::createMesh supports this ownership without a sparse global slot
array. Dead allocations release source references while retained frames keep
their own geometry. Tests cover distinct allocation identities and retention
across scene retirement. Source splinter row-matrix rotation, single-sidedness,
model-face material/UVs and lifetime fade are explicit; shards preserve two-sided
handling, authored UVs and0.2+0.8*material-alpha policy.

The private fixture calls BreakGlassFace and CreateSplintersFromBody, then stages
and scales their outputs for inspection. It emits22shards and10splinters; exactly
32asset births are logged, with32instances still using those assets at frame22.
Engine and all three suites pass. GL-clean captures were inspected. The ordinary
corridor capture remains byte-identical (SHA0C5AFFFD…A79FBB44).
Evidence: integration-fragments{,-scaled,-normal}-20261023.

The existing CreateSmolderingObject API also reaches an owned upright axis
billboard using the authored fixed fourth frame, width20, max_size height and
alpha0.4. No live gameplay caller has been established. Its fixture visibly
produces a rectangular haze, so appearance is NOT signed off merely because the
API executes. Evidence: integration-fragments-smolder-20261023. I12 remains open
for natural destruction/portal/mirror/reuse qualification and this content review.

## Independent weapon contributions and canonical rings (I10 partial)

CompileWeaponVisuals is the shared source contract for body Model/Sprite/
Electrical/None and independent ring/streamer admission. Model and effect
publishers consume it; an invisible or nonsprite body no longer discards those
contributions.80CPU combinations cover body precedence and independent flags.
Streamers publish shared one-pixel fading stroke topology with authored lifetime
alpha/endpoints/color. Sprite rings use the authored20camera-facing blobs and
lifetime animation selection. Model rings use one of two shared alternating-
sector assets, with pose/radius/alpha only in the instance. Owner handles retain
allocation generations. Electrical body and close-screen domain work remain.

The real ObjCreate/ObjInitWeapon fixture uses private copies of authored sprite
and model definitions, with visible and invisible variants. It emits21independent
meshes for each sprite variant and2for each model variant, while only visible
bodies appear. integration-weapon-contributions-20261023 and
integration-weapon-plan-final-20261023 execute GL-clean. The sprite rings and
body suppression are plainly visible; the low-opacity model-ring/stroke output
still needs more isolated visual qualification, not just admission counts.

Existing blast/gravity rings also now share canonical source-created assets.
Their old path rebuilt nested polygon vectors and procedural position arrays
every publication; expansion/phase now compose an affine instance transform.
The paired ring fixture differs in0.0384%of pixels, maximum1/255/channel, none
above2; ordinary003 differs in0.0051%, also maximum1, from transform arithmetic.
No appearance degradation was detected. Evidence: integration-ring-{before,after}
and integration-weapon-normal-20261023. Engine and all three suites pass. No
whole-frame performance claim or I10/I11 completion is made from these fixtures.

## Mass-driver variants and legacy snow (I09 remaining producer families)

Both mass-driver trails now publish two instances of one immutable16-sided tube
asset, with authored inner/outer radius, colors, lifetime alpha and texture
repeat. Normal trails also emit their capped400-planar-ring profile using the
shared quad. This replaces per-quad source geometry construction; no draw or RNG
is invoked by publication. The legacy snow family publishes shared billboards
with authored fade-in/lifetime opacity, color, size and soft-depth policy.
Enhanced snow and close-screen snow admission remain separate open work.

The fixture fires the real mass-driver weapon through FireWeaponFromObject,
stages that trail for inspection, and creates a boss record plus three legacy
flakes through VisEffectCreate. An initial outdoor frame0 run failed the existing
terrain-visibility creation gate; moving the fixture after its first real
observation (frame1) fixes the fixture, not the gate. Indoor flakes correctly
retire below world height1, so only the outdoor capture qualifies snow. It logs
mass_drivers=1,boss_mass_drivers=1,legacy_snow=3 and visibly shows both trails and
the three flakes. No GL errors. Evidence:
integration-mass-snow-outdoor-after-observation-20261023. The boss record is not
evidence for natural boss firing. The ordinary003 capture matches the prior
weapon checkpoint SHA A0E0C7E7…55641834. Engine builds; source CPU suites remain
passing from that checkpoint. I09 still needs broad natural/multiview/content
qualification; no release bit was cleared.

## Enhanced snow and physical camera inputs (I15 partial)

The game-owned enhanced-snow pool now publishes its real particles, including
four atlas variants, flutter/tumble, lifetime/impact fade and settled surface
orientation. The legacy flagged VisEffect representation uses the same source
compiler. Resource preparation exposes only the atlas handle; no g3/viewer state
or GPU draw is called. Every flake uses shared quad topology and per-binding UV
scale/offset, not per-particle vertex/UV arrays. Simulation emission/collision
remains in weather.cpp, once per game frame.

View now explicitly owns physical camera velocity. MakeGameSceneView captures
it; rear views keep the physical velocity and mirror requests reflect it.
Velocity-aligned billboards carry motion/bank/stretch parameters; the pure view
compiler resolves their shape from each camera, including conservative geometry.
No shader varyings or renderer-global viewer substitutions were added. Zero
relative motion has a stable bank instead of deriving orientation from signed
zero atan2 inputs. Tests cover source ownership, rear policy, mirror reflection,
per-view relative stretch, nonmutation and invalid motion inputs. Engine and all
three CPU suites pass.

Actual outdoor DoWeatherForFrame/DoSnowEffect emission with -force-snow produces
512published flakes at frame120. Main and real rear-monitor captures are
GL-clean and visually inspected: integration-enhanced-snow-{outdoor,rear}-20261023.
The ordinary003 capture remains byte-identical to the prior weapon/mass checkpoint
(SHA A0E0C7E7…55641834), integration-enhanced-snow-normal-20261023. These are not
formal performance or settled-collision/transition signoff. Sky lightning/star
history and the broader I11 close-screen routing still remain open.

## Non-array training/demo status (I20 partial)

The training-control/demo-status policy is shared between the retained UI path
and source canvases, not duplicated or routed through GPU callbacks. It captures
binding-query scratch before later queries can overwrite it, uses bounded text
formatting and emits owned glyph geometry. Source item layout reports its own
top-left text footprint; training placement never reads a legacy text-pass global.
HUDCanvasLayout admits gameplay status independently of the HUD item mask, while
special camera modes retain their explicit suppression policy. CPU layout tests
cover this distinction; engine and all three suites pass.

Status is an explicit display-overlay role above cockpit/auxiliary HUD (order55).
The first recording fixture was occluded by the cockpit in the world-HUD layer;
the corrected composition makes its bottom-center label readable without an
angle/position-specific offset. Normal003 remains byte-identical, SHA
A0E0C7E7…55641834. Training/record/paused source-input fixtures are GL-clean and
visually inspected: integration-hud-status-{training,record,paused,record-layer,
normal}-20261023. These override only layout inputs, not the live demo recorder.

The actual training.mn3 mission was then started normally and captured at frame360
with no status probe. Its real mission script enabled Forward (controls=1); the
owned status canvas shows that binding alongside the script's move-forward
instruction. Evidence: integration-training-mission-20261023. This does not yet
qualify actual demo recording/playback files or the omitted diagnostics/player
labels. A separate visual concern in this scene is bright floor bands appearing
through the monitor areas; investigate I18 opacity/depth/composition before
promotion, without assuming this is new or altering cancelled cockpit geometry.

## Game-owned object emitters (I13 implementation; broader qualification pending)

Powerup sparkles and damaged/virus lightning now advance after object movement,
once per logical game frame. Their old renderobject callbacks and draw-owned
sparkle clock are removed. Eligibility is the previous accepted world observation
union, stamped with full object handle, scene lifetime and 64-bit logical frame;
neither independent publications nor a wrapped 16-bit renderframe can enable it.
Admission gaps explicitly reset local clocks. Each bounded 30/60Hz event owns a
local RNG stream, so allocation failure, replay or additional cameras cannot
consume gameplay randomness or multiply an object's emissions. Newly attached
arcs resolve initial endpoints from one lazy object-pose sample, not a zero-length
placeholder awaiting another update. No per-frame heap storage was added.

The real producer is linked into CPU tests with only allocation/light/pose game
boundaries substituted; the existing throwing ps_rand fixture catches accidental
shared RNG use. Tests cover paused/repeated/reordered updates, 16-bit frame wrap,
stale/foreign-scene/recycled-handle observations, bounded cadence, explicit
admission breaks, scene resets, allocation failure, and initial attachments.
Engine and all three CPU suites pass.

Native ObjCreate fixtures (not direct effect injection) produce damage and virus
arcs plus powerup sparkles. Actual rear-monitor capture at frame180 records
4620 sparkles,16 damage bolts,21 virus bolts; both arc colors are visible in the
inspected output. Evidence: integration-object-emissions-{main,rear}-20261023.
The same frame180 run without the rear monitor produces2473 sparkles but the
same16 damage/21 virus bolts: additional observed powerups are admitted, not
duplicate emission from the already observed front objects.
The ordinary003 save now naturally includes the previously omitted sparkles;
its image is intentionally not byte-identical (0.3335% changed pixels, confined
to world effects at x378..964/y178..410). Evidence: integration-object-emissions-
normal-20261023. These short GL-audited runs are not performance or full natural
combat/destruction qualification, and do not close the remaining I13/viewer-wide
deformation and other release gates.

## Electrical pulses and camera-owned presentation (I10 implementation)

Electrical weapons no longer depend on DrawElectricalWeapon to trace their
visual endpoint, emit wall-contact sparks or expire the pulse. The game-frame
owner resolves these once after simulation/multiplayer updates. The bounded
source/gun cadence uses scene/allocation identity and an explicit local RNG;
the common spark initializer also retains its existing gameplay-RNG entry point
for unchanged callers. The retained diagnostic draw consumes the same resolved
pulse without simulation writes. Target-player contact correction no longer
depends on whichever global viewer happened to draw it.

Source compilation publishes shared immutable five-sided tube/tip and eight-sided
gradient-disk assets, affine segment placements, captured camera selection and
directional opacity. No per-segment geometry vectors/assets are allocated, and no
game/weapon branch or extra varying was added to GPU execution. Directional
opacity fits existing visibility-record padding (48bytes unchanged). Material
policy, scheduling and GPU modulation consume the same view-local opacity.
Frame validation reuses the compiled object lookup for full camera-owner identity.

An integrated CPU query caught an upstream camera-contract issue: resolveView
previously erased attached_camera to prevent duplicate shake. Attachment is now
preserved, with camera_effects_resolved separately tracking that preparation.
Tests cover idempotent shake, immutable materials, forward/rear/mirror selection,
directional fade, stale owners and malformed axes, along with actual pulse
resolution, collision cadence, replay, pause, expiry and recycled/foreign identities.
Engine and all three CPU suites pass.

GL-clean, inspected captures: integration-electrical-native-final-20261023
(native FireWeaponFromObject for player and robot source, real rear monitor),
integration-electrical-mirror-final-20261023 (mirror room plus rear monitor), and
integration-electrical-player-input-20261023. The last uses normal SelectWeapon
and held primary input, with no electrical producer probe: the real Omega bank
emits two gun pulses. The normal003 no-firing image is unchanged, SHA
24543B04…96EFA1F, integration-electrical-normal-final-20261023. These do not
qualify full weapon/target/death/reload gameplay, close-screen I11 routing or
performance under sustained combat; all release gates remain open.

## Shared view audiences and close-screen routing (I11 implementation)

Models and prepared meshes may now belong to a primary and an alternate
collection without duplicating geometry, model variants, poses or material
tables. The compiled membership proof includes both audiences and reuses its
storage. Primary audience selection is separate from visual camera selection,
so late routing cannot overwrite Omega's authored camera variants. Camera-kind
masks preserve auxiliary/mirror world content while excluding it from the main
world pass only. Owner validation reuses the compiled full-handle lookup; stale
membership, duplicate audiences and unsupported isolated surface receivers reject.

The source family policy precedes close/windshield flags. Physical lines, thick
lightning, trails and mass-driver tubes remain world content. Ordinary flagged
effects receive a main-camera audience, while actual windshield rain/snow is
camera-local only. This removes blanket drops that were omitting content. In
ordinary003, the five previously pending effects are four thick-lightning
records and an axis billboard carrying a reused windshield bit; all now compile
through their proper world family. Other cameras do not inherit a view-space
windshield layer. A source scope applies routing to every contribution emitted
by a family, not just its first sprite.

Weapon routing samples the captured physical main camera and generation-keyed
source marks, plus the existing attached/near-Napalm policy, once per source
object. It applies the audience to model bodies and all owned contributions.
The renderer no longer asks WeaponIsCloseScreenEffectObject (and thus mutable
Viewer_object) while compiling sprite content. The camera layer is scheduled
after all producers. Its isolated depth is explicit, and soft-particle world
depth sampling is disabled consistently in pass planning and GPU parameters.
No shader varyings or draw-time callbacks were added; 48byte visibility records
remain unchanged. The extra audience is metadata, not another object renderer.

CPU tests cover one model/variant and one mesh shared by both audiences,
main/auxiliary/mirror admission, independent held frames, membership changes,
family/flag combinations, camera-kind validation, and isolated depth dependencies.
Engine and all three suites pass. GL-clean inspected captures include
integration-close-napalm-input-20261023 (normal held primary, actual Napalm bank,
real rear monitor), integration-close-model-mirror-20261023 (native ObjCreate
model pair, one marked by the existing source API; both visible in mirror), and
integration-close-normal-audit-20261023 (no producer probe, pending effects zero).
The ordinary003 image intentionally changes around the restored world effects;
it is not a zero-difference claim. Sustained combat/observer/rear/settings/weather
transitions and performance qualification are still pending.

The monitor-opacity concern was traced to inherited alternate-cockpit styling:
the old path explicitly draws weapon imagery at half opacity over scanlines
with zero base darkness. The source path carries the same policy. No cockpit
geometry or styling was changed; broader I18 composition qualification remains.

## Startup argument safety found during module qualification

The fixed, exported 64x100-byte argument table was being overrun by a 109-byte
diagnostic module path. As a final operand it appeared to work; adding another
option corrupted the path. Both startup parsers now reject excessive operands
or argument counts atomically, with a clear startup error. Repeated parsing,
termination, quoting, whitespace, empty values and boundary lengths are tested
in both argument suites, including zero-allocation parsing. Windows, SDL and
Linux startup callers check failure before application creation. The public
module table remains unchanged; this does not introduce long-argument support.
Final native-module captures use a short, task-owned directory alias instead.

## Negotiated module room mutations (I08/I21 implementation)

Shipped modules explicitly declare tracked room writes; legacy/unknown modules
retain conservative invalidation. Entropy additionally requires successful
binding of a versioned, owner-tagged room mutation extension. Neither the old
function table nor public room/HUD layouts change. Invalid/stale owners, room
indices and change masks reject. Engine goal-room painting and Entropy share
one authored face policy, preserving replication flags while invalidating only
when material values actually change. Auditing the shipped modules found direct
material writes in Entropy; other goal painters use the tracked engine API.

The real Entropy HUD exposed a signed-byte depth-setter ABI mismatch in the new
canvas adapter. Its signature now matches the native function; ten setter bridge
signatures are compile-time checked. All nine module targets, engine and all
three CPU suites build/pass. Tests cover binding negotiation, descriptor layout,
invalid descriptors, authored goal/alpha/emission exclusions and no-op paint.

Inspected GL-clean native captures use the final bounded-parser path:
modroom-entropy-final-20261023 paints room152 through the real level-start event,
then repeats the event without changing appearance revision57989. Its real HUD
also leaves that revision unchanged. modroom-reload-final-20261023 unloads and
reloads Anarchy with distinct full owners and unchanged HUD appearance revision.
Old-module fallback captures retain invalidation; tracked and conservative
Anarchy controls produce exactly identical pixels.

Same executable/module, equal argument counts, pinned render thread, 480frames:
the two conservative controls average3.054/3.169ms actual frame-start intervals;
tracked controls average3.030/2.839ms, using frames240–479. GPU averages remain
0.270–0.274ms. This low-resolution CPU-sensitive case is noisy, supports only a
modest local improvement, and does not qualify whole-renderer performance or
two-process multiplayer. All broader release gates remain open.

## Game-owned sky lightning (I15 partial implementation)

Lightning's flash/bolt clock and branch generation now belong to the weather
game phase. One bounded immutable strike is shared by all views; publication,
rear monitors, exports and diagnostic legacy drawing cannot advance it or use
a draw-owned RNG. The gameplay weather trigger and thunder RNG remain in
DoWeatherForFrame. The branch generator uses an explicit local event seed and
fixed storage; its 512-segment cap prevents extreme camera heights from creating
unbounded geometry. Ordinary strike generation does not clear that storage on
every inactive frame. Flash geometry and bolt ribbon topology are retained;
segments use shared topology plus placements, not per-segment geometry assets.

Intentional normalization: a triggered strike lasts one logical flash frame,
then one bolt frame, even indoors. The old draw-count clock could consume both
phases in a multi-view frame or keep an indoor strike pending indefinitely.
The strike is anchored once at the physical weather observer, rather than
regenerated for each camera. Alpha, color, sky footprint, origin sprites and
nearby bolt shape retain the authored policy. Sky-only depth/medium behavior
is explicit; foreground terrain/rooms continue to occlude it.

Engine and all three CPU suites pass. Controller tests cover replay, pause,
scene retirement, reordered frames, deterministic topology, disable, missing
camera and bounded/invalid positions. Inspected GL-clean native captures
sky-native-{flash,bolt}-20261023 use SetLightningState and the real weather
trigger with a live rear monitor. sky-after-final-20261023 shows termination.
sky-normal003-20261023 is pixel-identical to the prior no-producer-probe003
capture. The first outdoor attempt rejected an incorrect raster-stroke alignment;
that was fixed before these successful captures. Star-streak history and broad
weather/lifecycle qualification remain open; I15 is not fully signed off.

## Owned display labels and executable-camera receipt (I19 implementation)

Auxiliary labels and multiplayer callsigns now compile into their own crisp,
display-resolution final-overlay collection. The bounded game scratch queue owns
text/style values, supports centering/shadow/additive text, and admits no borrowed
strings or callbacks. Legacy small-view queue calls are translated at this source
edge; execution receives ordinary immutable text geometry and image resources.
Clean gameplay screenshots suppress the label layer. The queue is reset by the
game frame owner, not consumed by a GPU draw/replay.

The observation transaction retains the exact executable main camera, viewport,
room, cut and exclusions, using pooled storage. Labels reuse this receipt instead
of resolving/copying another View or re-traversing portals. Conflicting cameras
with the same history identity now reject even when their observed object sets
happen to match. Source callbacks/rays decide module color and semantic sight
before overlay publication; full allocation lookup, cloak, local-player exclusion,
connection state, view cone and clipping are retained. An inherited sight bug
accepted only polygon hits on the target; ordinary object hits on that same target
are now also accepted, without accepting intervening objects/walls. The unused
global-g3-projection player-name implementation was removed.

All three CPU suites pass, covering queue ownership/capacity/invalid styles,
near/behind/cone/crop/rear projection, target-versus-occluder hit policy and exact
camera receipt/replay/conflict. Engine builds. Inspected GL-clean native captures:
labels-visible-distance-20261023 has a real Anarchy-colored callsign and rear label;
labels-blocked-distance-20261023 uses an intervening native robot and omits the
callsign; labels-cloaked-qualified-20261023 omits the cloaked player's name.
These use actual module events/ObjCreate and no network transport. Early blocked
fixtures overlapped the target sphere and therefore were not occlusion evidence;
the qualified distance-separated fixture hits the intervening robot instead.
labels-normal-final-20261023 is pixel-identical to sky-normal003-20261023.
Real two-process gameplay, all auxiliary modes and broader foreground composition
remain in the end-to-end gate; I19 is not by itself a deployment signoff.

## View-owned world deformation and explicit surface velocity (I16 implementation)

MakeGameSceneView snapshots the physical viewer's EF_DEFORM range/decay and a
deterministic visual phase. Rooms, terrain and placed models consume a shared
vertex-shader field; visibility still uses simulation volumes. No per-view CPU
geometry copies, source writes or gameplay RNG are introduced. Camera-local
foregrounds clear the deformation descriptor; display canvases and sky remain
undistorted. Model bindings publish smaller vertex and whole-object responses,
and attached accents inherit their parent's full-owner-keyed rigid movement.
The GPU sees response coefficients, not player/weapon branches. Callsign anchors
reuse that published rigid response and the exact main-camera receipt.

Intentional visual/RNG normalization: a spatially coherent, bounded-gradient
noise field replaces the old independent draw-order jitter. It retains the
authored room/terrain displacement scale and decay, with the smaller model
response and whole-object wobble. Early independent point hashing produced
severe face folds, and applying the room scale to models was excessive; neither
is the qualified implementation. This is not an exact legacy random-pattern
equivalence claim and still needs gameplay appearance review.

View history carries current/previous deformation with the camera pair, rejects
conflicting same-publication replay, and resets across cuts/reloads/independent
streams. Warped mirror films use conservative full producer coverage rather
than clipping to an undistorted aperture. Normal mirror footprint optimization
is unchanged. Current and previous shader positions use the corresponding field.
This exposed an upstream motion contract issue: explicit velocity was inferred
from nonzero object identity. Static deforming surfaces now publish valid
velocity with identity zero, and motion-blur/debug/temporal-AO consumers honor
the validity channel independently. No fake object ID or extra varying is used.

Engine and all three CPU suites pass. Tests cover source range/decay/phase,
shared-point continuity, invalid view/response data, articulated/attached response,
history/replay/pause/cut/reload, independent streams, foreground opt-out and
mirror coverage. Native zero-shield-loss ApplyDamageToPlayer with an authored
microwave weapon exercises the real damage entry point; benchmark invulnerability
flags are temporarily cleared and immediately restored only inside that fixture.
Inspected GL-clean captures: warp-{indoor,outdoor,mirror}-field-20261023 and
warp-blur-20261023. The latter also exercises real modern motion-blur composition.
warp-motion-final-20261023 audits actual GPU records: over1.05million static pixels
have valid finite deformation velocity during the hit. warp-expiry-final-20261023
has zero displacement and zero static deformation velocities at frame100; its
image differs from the no-hit control by at most2/255 per channel (mean0.0353),
not a pixel-identical claim. warp-zero-final-20261023 is pixel-identical to the
pre-feature ordinary003 image.

Current-renderer reference only, not a pre-rewrite comparison: at1280x720,
SSAA2/MSAA4, AO/bloom/modern motion blur, pinned CPU2, frames240–479 of480,
warp-perf-current-20261023 averages3.31ms actual frame-start intervals (median3.22),
2.69ms GPU and2.34ms CPU rendering. These are overlapping intervals, not additive.
The extra velocity validity read and active deformation cost still require the
broader equal-workload scaling/performance gate. No overall speedup claim follows
from this slice; I16 implementation is not whole-renderer deployment signoff.

## Owned auxiliary lifecycle and explicit scene composition (I23 implementation)

The game captures left/right full-generation camera owners, gun pose, static
bitmap frame, labels and layout inputs before world publication. It resolves
embedded monitor regions against that publication and commits admitted popup
timers once, before drawing. Hidden/animating views freeze their display clocks;
replay cannot tick them again. A dead source becomes a bounded static interval,
then restores the saved base view. An explicit non-popup replacement cancels a
stale saved popup base. Zero zoom now honors the documented default-zoom API.

SmallViews no longer owns immediate UI drawing, StartFrame/EndFrame, global
camera/visibility changes or timer advancement during GPU execution. Static,
scanlines, borders and crosshairs are owned canvas geometry; labels use the
existing owned display queue. AfterWorldViews is an explicit pre-resolve phase,
before cockpit/HUD display composition, not a numeric ordering workaround.
When the main camera hides the cockpit, auxiliary views use floating rectangles
instead of silently waiting for unpublished embedded monitor regions. Moving
cockpit regions still retain their original skip/frozen-clock policy.

Independent world views explicitly clear depth only in their own rectangles.
Their temporal policy publishes zero velocity with validity one and object ID
zero; there is no fake gameplay handle. Metadata writes target the metadata
resource's actual extent and sample count, including the separate logical AO
prepass rather than assuming attachments exist on the supersampled color FBO.
Native GPU self-tests cover single-sample logical and multisample metadata,
categorical resolve and bounded writes. No diagnostic readback runs normally.

All three CPU suites and the engine build pass. Native GL-clean, visually
inspected captures aux-{rear,static,restored,mainrear,reduced,gun,timed}-depth-
20261023 exercise SSAA2/MSAA4, AO/bloom/modern blur, both monitors, native object
deletion and same-slot generation replacement, static animation, popup restore,
rear reversal under a rear main camera, gun pose and timed closure. The
aux-{animating,letterbox}-owned-20261023 captures exercise skipped admission.
Actual GPU records at monitor centers have zero velocity, validity one, ID zero.

Failed preliminary captures are not signoff: the initial canvas ran after
display resolve; an initial mask assumed color-FBO attachments; and visual
inspection caught monitor geometry testing against main-view depth after the
implicit legacy StartFrame clear was removed. The explicit phase, metadata
resource write and per-view depth-clear contracts above replace those mistakes.
Earlier *-owned and *-phase rear/static captures predate the depth fix.
Guided missile small/full switching, interactive pause/restore, standard versus
alternate cockpit and broader settings/lifecycle scenarios remain release gates.
aux-zero-final-20261023 is pixel-identical to warp-zero-final-20261023 with no
auxiliary requests. aux-noao-depth-20261023 also passes the real stationary-mask
audit with SSAA/MSAA and modern blur but without AO's independent prepass.

## Authored room coronas and abandoned frame-buffer leases (I14 implementation)

The game main-view schedule captures authored corona candidates from the exact
prepared visible-face inventory, with no second portal walk. Persistent source
geometry is keyed by room asset/revision and face; appearance and material-table
revisions invalidate cached color/type/eligibility. The existing material-input
census now includes light RGB and corona type. Source rays and the bounded100-
source visibility/fade clock run before main-view GPU execution, once per logical
frame. Paused/repeated exposures cannot advance it. Synthetic unbound/clipped
main-role diagnostics explicitly have no gameplay corona exposure.

Published additive billboards render after the main world and before auxiliary
windows, from owned resources and the exact executable camera. Rendering performs
no live face lookup, raycast, fade update or legacy glow call. Normal coronas keep
depth disabled; FastCoronas retains its short visibility ray and half-size depth
bias with read-only depth. Both keep near/distance/facing attenuation, authored
color and pulse/strobe modulation. Visibility fades do not restart during a dark
lamp phase. The physical camera object is explicitly ignored by its own ray.
Old PreUpdateAllLightGlows/PostUpdateAllLightGlows scheduling is removed; the new
path does not repopulate or render the legacy LightGlows array.

Three CPU suites and the engine build pass. Clock tests cover pre-draw fade,
pause/replay/reorder, disappearance, source/material revision replacement, scene
retirement, capacity/preflight and near/facing/light attenuation. Actual saved
rooms001/002/006 produce native authored coronas. Inspected captures
coro-saveg{001,006}-{before,after}-20261023 compare with the frozen pre-feature
renderer: visible halo additions are intentional, not an image-equivalence claim.
corona-{normal,fast,blocked,recolor}-final-20261023 exercise native face sources,
both depth policies, a real intervening robot and live authored light RGB change,
with rear-window composition, SSAA2/MSAA4/AO/bloom/modern blur. The blocked source
is absent after its fade. coro-pulse-pooled-20261023 keeps6visible sources while
the strobe is dark (zero draws), with cached same-publication replay. The earlier
corona-pulse-final capture predates that visibility/intensity separation fix.
coro-resource-pooled-20261023 passes real SSAA/MSAA resource changes through
1/0,2/2,4/4,1/0,2/4 and retains5native glows. No scene GL errors were observed.

Timing exposed a buffer-lease ownership defect: acquireFrameData builders returned
capacity only after successful publication. An optional empty producer could
therefore discard warm world buffers. FrameData now returns its own lease on
destruction, including abandoned builders and failed publication; Presentation
hands it the compilation storage on last-reader release. Foreign-thread releases
remain non-pooling; copied builders cannot mutate another reader's compilation.
A tracked-operator-new CPU test proves256warm acquire/resize/abandon iterations
perform zero allocations and retain4096-object capacity. Existing held-reader,
foreign-thread, publisher-exit and copied-compilation tests still pass.

Current-reference performance is noisy, not an overall renderer speedup claim.
At1280x720 SSAA2/MSAA4 AO/bloom/modern blur, pinned CPU2, frames240–479,
interleaved coro-cycles-{a,c} (frozen pre-feature) average3.63/3.62ms whole-frame;
{b,d} (coronas plus lease fix) average3.36/3.40ms, CPU rendering2.46/2.46ms and
GPU2.71/2.70ms. Other interleavings show substantial host variance, including one
fixed-build3.86ms run; do not infer a dependable percentage win from these pairs.
The abandoned-buffer defect is independently proven/fixed, but the full dense,
active-corona, high-resolution and pre-rewrite performance gates remain open.
Door occlusion, complete reload/fade timing and broader gameplay qualification
also remain release gates. This implementation is not whole-renderer acceptance.
coro-zero-pooled-20261023 is pixel-identical to the preceding ordinary003 frame
(a view without admitted coronas). coro-mirror-policy-20261023 confirms an
independent synthetic mirror camera does not inherit a stale main-view glow set.

## Retained star streaks and per-camera exposure (I15 implementation)

The600stars remain one retained2400-vertex marker asset, not rebuilt CPU trails.
The game sky producer publishes current/previous sky placement with a qualified
world predecessor; independent thumbnail publication cannot consume its exposure.
Generic raster-trail metadata carries absolute displacement/reference length,
tail fraction, alpha response and camera audience. The Descent source retains
the old9000-unit threshold,90000-unit scale,.75tail and.6minimum head alpha.
These are source parameters, not constants or star branches in the GPU executor.

The vertex shader expands marker quads from current/previous view-space points,
clips crossing tails at the near plane and fades head to tail. Prior camera
placement includes the eye-following sky translation, so translation cannot be
mistaken for angular sky motion. Source geometry/material changes, missing view
history, cuts and resize suppress extension; paused views suppress it explicitly.
The game star audience permits main only: auxiliary/mirror/thumbnail views remain
points. Generic metadata also supports other declared audiences. No game RNG or
draw-time last-star array is used. Ordinary materials skip the trail math.
The draw UBO is384bytes (one additional vec4, with reference length packed into
an unused component); the varying interface remains14smooth vec4 plus flat
integer and clip-distance slots, unchanged. No extra interpolator was added.

All3CPU suites and the engine pass. Tests cover displacement threshold/cap,
pixel width, alpha, near crossing/behind-eye collapse, independent exposure holes,
replay/conflict/reorder, asset/scene reset, camera predecessor and camera cut,
audience/pause policy, eye-following prior placement and rejected invalid inputs.
Inspected GL-clean native captures star-{turn,cut,resume}-{trail,point}-20261023
show main streaks, point rear stars, an exactly pixel-identical point-only camera
cut, and streaks returning the following frame. The earlier stars-* comparisons
used nondeterministic level-generated star fields and are not parity evidence.
The final fixture uses a local fixed PSRand stream, not gameplay RNG. Its black
sky/forced star field/controlled turn are diagnostic setup, not production changes.

star-export-fixed-20261023 invokes actual SaveGameState/thumbnail rendering
between world publications: mesh and camera history stay valid on frames20–22.
The resulting task-owned stars-owned-save-20261023 loads successfully through the
real CLI loader in star-owned-reload-20261023; the new scene starts with invalid
motion history as expected. This is save/export/reload evidence, not normal
save-dialog or quicksave-key UI acceptance. star-zero-20261023 differs from the
preceding ordinary003 image only by1/255 at3pixels (mean absolute RGB1.18e-6).

Performance caution: 720p SSAA2/MSAA4 AO/bloom/modern blur is effectively stable
in this local pair (whole3.46->3.31ms,GPU2.61->2.62ms), not a dependable speedup.
Short120-frame4K+SSAA2/MSAA4 fog runs initially showed a repeatable apparent GPU
increase. GPU split traces locate the difference in the unchanged bloom stage;
main-world rendering is20.07/20.03ms. A matched600-frame pair without thread-cycle
sampling gives whole30.14/30.13ms and GPU29.70/29.67ms on frames240–299; other
windows still vary. A process-memory snapshot showed3589MiB dedicated and9MiB
shared, not demonstrated VRAM exhaustion. Do not suppress the short-run result
or assert a global performance win: benchmark stability and the full scaling gate
remain open. Active-streak throughput and broader natural fast-turn appearance
also require final qualification. Candidate/Previous Candidate remain untouched.

## Root/CI/release contract gate (I29 testing integration)

Root CMake now includes CTest and adds the independent contract project when
BUILD_TESTING is enabled (the normal default). renderer_integration_check depends
on the real Descent3Mastered target and all CPU suites, then runs root CTest.
Thus the full game-facing source set and shipped module dependencies compile/link
alongside the focused fixtures. This is not a GPU or proprietary-content CI test.
The standalone tools/renderer-scene-tests project remains independently usable.
Its CMake3.15 minimum also applies to root builds with testing enabled.

The renderer-contracts workflow runs standalone Debug/Release on pull requests,
branch pushes and manual dispatch, then a Release root integration build. It
retains source/configuration/target identity, compiler/cache metadata, JUnit and
CTest logs, plus the real game executable hash for the integration build.
The normal Windows packager forces x64 BUILD_TESTING=ON and runs Release CTest
before install/staging/archive creation. Its x86 bridge-only build disables the
unused suites. Release workflow artifacts retain the exact package binary hash,
source/dirty-tree state and contract logs. A passing pure suite does not bypass
the ordinary-gameplay acceptance gate.

Locally verified: root Ninja Release renderer_integration_check builds and all3
tests pass; the workflow's Visual Studio17/x64 generator also builds and passes
all3suites in both Debug and Release (MSVC19.42.34436). Debug initially exposed
the zero-allocation test's assumption about MSVC checked-iterator proxies. Debug
keeps checked iterators and verifies capacity reuse; Release retains the strict
tracked-operator-new zero-allocation assertion. No production allocation check
was relaxed. JUnit output and the packaging script's PowerShell syntax pass.
The packager itself was not executed locally and no cleanup/deletion was run.
Hosted CI execution is not claimed by these local results. Runtime scenario
registry, interactive lifecycle and full deployment/performance gates remain.

Normal root entry point (from an MSVC developer environment):

    cmake -S . -B out/build/renderer -DBUILD_TESTING=ON
    cmake --build out/build/renderer --target renderer_integration_check --config Release --parallel 4

## Explicit scene retirement and publisher-cache ownership (I28/R2)

The actual end-level FlushDataCache boundary now retires both the CPU scene and
its GPU residency, before freeing native texture/model source data. A separate
rend_RetireScene/IRenderer::RetireScene contract preserves window, final-display
and legacy UI resources; rend_ResetCache retains its narrower image-remap role.
The backend releases world geometry pages, image/view/depth caches, buffers,
samplers, programs and temporal history. Repeated retirement is harmless. This
does not flush a held immutable presentation or invoke game callbacks in GL.

SceneAssets owns publisher-cache data through typed weak SceneCache handles.
The last world publication, sky/star exposure, model transaction scratch, effect
topologies/fragments, text meshes and room-corona publication now share the scene
lifetime instead of surviving until their next producer call. A producer's active
borrow may finish; a held snapshot retains its own immutable assets, not the
mutable publisher cache. Pure source revision/ID caches remain epoch-qualified.
No callbacks, per-frame registration or warm-path allocation were introduced.

CPU tests cover independent owners, reset/destruction, held producer borrows,
held/released immutable snapshot geometry and fresh state after retirement.
256 warm cache acquisitions retain one state and perform zero tracked operator-
new allocations, including checked-STL Debug. Root Release and standalone Debug
all3suites pass. Native retire-history-003-20261023 exercises double retirement
atframe20, validates invalid camera/motion history20 and recovered history21–22,
then checks the real end-level flush. Seven publisher caches and all world GPU
residency retire at exit. retire-mirror-006-20261023 also retires/recreates a real
mirror image; retire-fog-settings-002-20261023 combines reset with SSAA/MSAA
1/0->2/2->4/4->1/0->2/4 transitions. Both are GL-clean and visually inspected,
with AO, bloom and modern blur. The no-reset retire-plain-003-20261023 image is
pixel-identical to frozen integration-stars-candidate (retire-base-003-20261023).
Forced live retirement intentionally restarts corona exposure/history; it is
not an assertion that these temporal pixels equal an uninterrupted scene.

The first hosted run caught an ignored version-header dependency in standalone
tests;2092b79 generates their version header in the build tree. Both hosted
Debug/Release suites then passed. The integration configure next exposed an
unquoted PowerShell '-DD3_GAMEDIR=./' operand becoming empty; reproduced locally
from a fresh build directory and fixed by quoting it in b704c8f. A full hosted
integration pass is still pending, not implied by the local passes. All ordinary
interactive save/reload, mission transitions, context teardown and promotion
gates remain open. Candidate/Previous Candidate are unchanged.

## Declared UI compatibility consumers (I25 decision and deletion boundary)

Keep the small immediate UI/preview backend supported, not a second gameplay
world backend. No arbitrary UI/game callback may be dispatched by the scene
executor. UI animation may advance at its own caller cadence; it is not world
observation or simulation advancement. Converted world/foreground/map/module
presentations remain immutable and use the owned scene executor.

| Live caller | Retained dependency and composition contract |
| --- | --- |
| newui_core.cpp BeginGameplayModalUIFrame/DoUI | Immediate 2D into the dedicated modal UI framebuffer, composed over the resolved display. Preserve aspect, scaling, input mapping and close/resume. |
| TelCom.cpp TelcomRender/TCSSSCallback | Immediate shell and bounded ship-selection model preview. Its automap viewport is the scene-owned map producer, not legacy world rendering. |
| pilot.cpp UI3DWindow::OnDraw | Bounded immediate ship/custom-logo preview with native material/custom texture and UI-time rotation. Unavailable model returns before starting a G3 frame. |
| movie/d3movie.cpp CallbackShowFrame | Decoded mutable bitmap upload and immediate 2D scaling. bm_MarkChanged publishes each decoded frame's source revision. No world history/observations. |
| GameLoop.cpp GameDrawPostPresentFrame | Message consoles only, after resolved world/owned foregrounds. No old cockpit snapshot or late-effect replay. |
| multi_dll_mgr.cpp exported drawing API | Live shipped module canvases capture typed scene-owned drawing/view requests during publication; outside that interval the declared UI compatibility API remains callable. Preserve ABI. |

Therefore keep the required rend_*, G3 model/2D/text and mutable-bitmap support
until these live consumers are migrated or their support is explicitly removed.
Do not remove them solely because world submissions no longer call them. This
inventory authorizes no automatic world fallback, pending-bit clearance or
old world-code re-entry. Old world-only traversal/render paths still await the
ordinary acceptance gate and a reference/call-site audit before deletion.
The decision is made; full interactive consumer qualification remains pending.

## Native save round-trip audit and versioned runtime scenarios (I26/I29)

The tracked corpus runner now reads renderer-integration-scenarios.json rather
than leaving new qualification cases in private scripts. Named, revisioned cases
cover ordinary publication, double retirement, independent native saves, saves
after settings changes, and actual auxiliary popup/deletion/replacement/restoring.
Manifests identify exact executable/HOG/pilot/harness/registry hashes, arguments,
copied save/count inputs, expected log assertions, raw whole-frame timing output,
result images and export evidence. Output directories cannot be reused. Mutable
pilot/save destinations reject reparse-point traversal; evidence stays outside
runtime/seed directories. The runner checks the shipped 64x100 argument budget.

Explicit -scene-save-image-audit adds no normal render-loop work. Each real save
keeps its own temporary audit data, decodes the embedded bitmap through the real
GetGameStateInfo/LGSSnapshot reader and compares every 1555 pixel with the writer's
160x120 result, including full opacity. Distinct per-save evidence prefixes retain
both pre-world and post-composition exports, source PNGs and before/after target
readbacks. The thumbnail renderer is checked against simulation RNG, time, delta
and FrameCount changes. No fake thumbnail or mocked serializer is involved.

The new audit exposed an earlier fixture bug: 'Renderer interleaved publication
check' exceeded the save format's31-byte description field. Release's old strcpy
overflowed the stack after a debug-only assertion. The API now bounded-validates
and zero-pads the disk field before any file/audit side effects, rejects invalid
paths/descriptions, and the fixture uses 'Renderer export check'. CPU tests cover
null/overlength/max-length/empty input, unchanged failure output, canaries and
padding. This is a real input-contract fix, not an image-comparison exception.

qual-save-v2-20261023 passes003/006 with two actual saves each, zero decoded-pixel
mismatches/transparent pixels, unchanged target PNG bytes and valid main history
20–22. Source/decoded mirror images were visually inspected: upright, world-only,
with the authored mirror present. qual-save-settings-20261023 passes002 after all
SSAA/MSAA transitions; qual-aux-20261023 passes the registered lifecycle case.
The first qual-save-20261023 run is FAILED evidence of the long-description bug,
not qualified output. These runs do not accept save-dialog/quicksave UI, an
autosave caller (none found in the current native call-site inventory), or demos.

Important open export defect found by inspecting evidence: a pre-world screenshot
readback uses the active cleared render target instead of the previous completed
display image. Its before/after equality proves target immutability, not correct
completed-output selection. Post-composition readbacks and native thumbnails are
correct in these runs. Fix the display-output publication/readback contract before
accepting pre-render demo/movie exports or calling final-output qualification done.

Hosted CI39972233564 at467aa64 passes standalone Debug/Release and the full game/
module build plus all3root contracts. The preceding metadata-step failure was
reproduced: piping native cmake --version to Select-Object -First1 produced exit1
in99/100 Windows PowerShell trials; draining output first gives0/100 failures.
467aa64 fixes both CI identity and package metadata capture without masking native
errors. This does not replace GPU/interactive qualification or promote Candidate.

## Owned display publication and export (I25/I26)

The pre-world export defect above is fixed at display publication, not by making
readback reconstruct a frame from the current scratch target. DisplayImagePublication
tracks empty/composing/presented base state and an independently published modal
overlay. Swapping preserves readable output; new base publication and resize
invalidate old overlay membership. World/auxiliary/thumbnail execution cannot
retire the last completed display. Paused modal frames reuse its frozen base
without rerunning world postprocessing or accumulating the preceding UI overlay.

PNG and legacy bitmap export now read this owned base. UI exports first finish
deferred modal painting; immediate nonmodal UI explicitly publishes its painted
frame. Modal layers retain native-density GPU presentation. Only export performs
CPU premultiplied source-over composition (bilinear layer sampling, UI-only gamma);
normal presentation gains no full-frame copy/readback or CPU pixel work. Common
GL readback preserves read/draw targets, read mode, PBO and all used pack stride/
offset state. An explicit GL audit checks exact pixels under hostile PBO/packing
state. CPU contracts cover publication transitions and export blending/validation.

Native evidence, private runtime only:
- qual-display-v1-20261023: corridor003 and actual mirror006 pass registered
  save-export revision2. The first pre-world export is byte-identical to the
  separately saved preceding displayed frame19, not merely equal before/after
  a thumbnail. Both actual saves decode with zero pixel/opacity mismatches;
  main temporal history remains valid. Inspected corridor readback contains the
  correct complete world/HUD/cockpit. Ordinary final003 matches the preceding
  qualified executable pixel-for-pixel. Hostile GL readback audit passes.
- qual-display-retire-20261023: registered mirror006 retirement passes after
  explicit scene reset/double GPU retirement/rebuild.
- qual-modal-20261024: new registered modal-ui case opens the real OptionsMenu
  after20 gameplay frames, exports after five real menu paints, then resumes
  gameplay through22. Inspected UI PNG has the options over the frozen corridor;
  final gameplay has no residual UI or black backdrop. Both exports succeed.

Root Release and standalone Debug pass all3 contracts. This closes the identified
display-source defect, not the entire UI/export gate: real pilot/custom-logo/
movie/console consumers, normal save/quicksave UI, clean/doubled screenshots and
demo movie caller coverage still need qualification. No autosave caller was found.

## Native screenshot/movie callers and UI ship preview (I25/I26/I29)

Native DoScreenshot now records its actual result/path/extent/scale/format in the
optional capture log. Registered screenshot-normal/clean/double/tga cases request
the native screenshot path at20; they do not call a substitute writer. The clean
request precedes publication, so existing producer policy suppresses HUD overlays
while retaining cockpit instruments/reticle. Doubled output is the existing exact
2x nearest duplication, not new rendering or simulation. The TGA path exercises
the legacy bitmap readback and native top-left BGRA writer.

Tracked renderer-export-images.ps1 compares every pixel and full opacity against
an independently exported same-frame reference, with no spatial/crop/tolerance
waivers. TGA alone uses its defined (channel>>3)<<3 precision. The comparator was
checked on Windows PowerShell5 and bundled PowerShell7, rejects wrong extents,
and detects728950 differing pixels when given a different frame.
qual-shot-{normal,clean,double,tga}-v2-20261024 all pass003 with zero mismatched or
transparent pixels; double is2560x1440, the others1280x720. Clean image inspected.
The first normal run failed comparator compilation due to PowerShell7's split
Drawing assemblies, not rendering. Interface-derived assembly references fix it.

Registered demo-movie takes an explicit copied DemoFile, bounded to2–60frames,
replays through real DemoPlaybackFile/DemoFrame with -makemovie, and checks every
native movie screenshot against its preceding presented frame. It records native
demo RNG policy, not the save-load-only fixed seed override. qual-demo-final-
20261024 passes5movie images from the retained Secret2 demo, each byte-identical
to its separately captured preceding display. The earlier registered run had all
five exact matches but failed the inappropriate save-load RNG-log assertion;
its failed result remains evidence, not a qualified run.

qual-pilot-v2-20261024 captures the real nonmodal pilot configuration screen.
qual-pilot-ship-v2-20261024 enters real PltSelectShip, waits for five real preview
paints (not loading/wait UI callbacks), and captures once. Both inspected images
are correct; ship is visibly rendered in the actual customization window. The
retained pilot has no custom logo, so uploaded/animated logos remain unqualified.
The initial pilot run timed out in intro playback before reaching the menu; the
successful capture explicitly skips startup intro, and does not qualify movies.

Root Release contracts pass. Hosted CI40162017498 at0c2a1d4 passes all3jobs.
Normal SaveGameDialog/QuickSave UI, interactive movie/console/module consumers and
the full gameplay acceptance matrix remain open; no Candidate promotion occurred.

## Completed UI paint boundary and actual quicksave caller (I25/I26)

UI export handling now runs after all painting owned by the UI frame. A tiled
window's late draw callback and cursor may follow ui_DoFrame (the native save
dialog uses this for its thumbnail). Ending its deferred modal layer and reading
it inside DoUIFrame was too early. CompleteUIFrameExports now services both native
PrintScreen and capture-exit requests at the owning completion boundary; readback
rejects unfinished deferred painting instead of forcibly ending it. No callbacks
were moved into the renderer, and ordinary world execution gains no pixel work.

qual-save-dialog-20261024 captures the real save dialog after five late preview
paints over completed corridor003. Its inspected PNG includes the native slot0
thumbnail and complete menu, with gameplay correctly preserved behind it. The
registered case explicitly copies/hashes its preview input; it cancels, does not
accept a slot or serialize a save. qual-modal-boundary-20261024 also passes real
options UI over mirror006 after this scheduling change.

Registered quicksave invokes actual QuickSaveGame against its explicitly copied
slot3, not SaveGameState directly. qual-quicksave-v2-20261024 passes: embedded
thumbnail decode has zero pixel/opacity mismatches; pre/post-save display hashes
equal the independently captured presented frame19; GPU camera history remains
valid20–22. The saved native file reloads successfully in qual-quicksave-reload-
20261024; its actual corridor image was inspected. Launch/input and final-input
hashes are recorded separately because quicksave intentionally changes its copy.
The first quicksave run preserved all pixels but failed missing history-log
assertions. A separate independent-export audit enables that exact check without
also triggering the interleaved-save fixture. No history failure was suppressed.

A suspected disappearing ship menu during visual inspection was a false alarm:
the unchanged saved PNG contains the full panel, confirmed by reopening it and
three exact-command repetitions. No renderer changes resulted from that suspicion.
Root Release contracts pass. Remaining UI acceptance includes actually accepting
the save-description/overwrite dialogs, uploaded logos, interactive movies and
consoles/module consumers. The no-probe promotion gate remains closed.

## Explicit display-color frames and decoded movie consumer (I25)

rend_BeginDisplayFrame declares a replacement full-display canvas of already
encoded color. It publishes/clears owned RGBA8 display storage directly, drops
old overlay membership and current scene motion inputs, and never resolves a
scratch world image or applies world AO/bloom/motion/SSAA/game gamma. The normal
world composite path is unchanged. This is a source-selected output contract,
not a decoder-specific conditional in the world executor. Movie callbacks,
subtitle policy and playback cadence remain outside rendering.

CallbackShowFrame now uses this entry point for its real decoded mutable bitmap.
The obsolete DoMissionMovie windowed-mode skip is removed: native windowed decode
and return to gameplay are qualified, not replaced with a synthetic image.
Bounded explicit capture requests retain consecutive presented frames and the
actual decoded bitmap source; normal playback/export timing is unchanged.

qual-decoded-movie-v2-20261024 passes real mainmenu.mve at640x480 with SSAA4/MSAA4,
AO/bloom/modern blur enabled. The movie image is byte-identical to the clean
SSAA1/MSAA0/effects-off run; world effect settings cannot leak into display color.
qual-decoded-palette-20261024 passes real dolby1.mv8 palette decoding at640x360.
Both have increasing bitmap revisions and distinct consecutive frames. Every GPU
pixel matches its decoded source after the exact current1555 upload conversion,
with zero opacity failures. Native source PNG bit-replicates channels, whereas
gl_image's unpacked RGBA8 table truncates255*(n/31) (gl_init disables packed pixels).
The comparator implements that exact conversion, not a per-channel tolerance;
the initially tried nearest-byte normalized expansion was rejected by evidence.
Both movie and resumed-game images were visually inspected. These clips have no
enabled subtitles, so subtitle layout remains a separate qualification target.

qual-ui-regress-{before,after}-20261024 compares all eight ordinary saves at30
against the frozen executable preceding the UI completion change: all eight are
pixel-identical, full-frame/full-opacity. This is not a pre-rewrite performance
comparison. Root Release contracts pass; hosted CI40163618373 at1a2d734 is green.

## Subtitle timeline/layout and explicit text completion (I25)

Subtitle admission is now an allocation-free per-playback timeline, reset at
track open/close even for a missing/disabled track. The old manual initialization
flag and draw-function static cursor are removed. Duplicate frames are stable,
rewind rebuilds admission, forward skips discard expired cues, and three active
lines are bounded transactionally. Parsing bounds the native500-cue array and
rejects reversed/unsorted ranges before writing. Layout/font scale use the actual
decoded canvas height rather than a fixed480-line target or inherited HUD style.
Movie subtitle strings are rendered literally, not as printf format strings.
The ineffective old subtitle clear-rectangle call is removed: each decoded frame
owns a fresh complete display canvas. The public compatibility API is retained.

The first real subtitle export failed usefully: all3lines were admitted, but the
readback matched the decoded source with zero text pixels. grtext_Flush queues
glyphs; it does not submit the backend font batch. The movie producer now finishes
that batch before its completed-image export. Late message consoles and UI export
completion do likewise. Display readback rejects unfinished text targeting its
owned display/overlay, while allowing unrelated in-flight scratch-target work.
Readback does not quietly draw pending geometry or infer a different image source.

qual-subtitle-short-v2-20261024 passes actual MV8 decode at640x360 plus the real
subtitle parser/callback and3simultaneous authored test lines. The inspected PNG
shows all three, including literal '100% decoded color'. Full opacity is checked;
every source pixel outside the declared bottom caption band is unchanged, with
2478intentional changed pixels inside it. Caption shape/words were inspected, not
claimed pixel-identical to an uncaptioned source. The first failed run remains
failed evidence of missing glyph submission, not qualified output.
The fixture input is copied into immutable evidence, never into the movies
junction/protected data. Game RNG/time/delta/FrameCount remain unchanged across
native movie playback, including its screen-state transitions.

qual-subtitle-none-20261024 passes the uncaptioned paletted clip with exact source
pixels. qual-subtitle-save-20261024 passes independent native save/history; qual-
subtitle-dialog-20261024 passes the real deferred save preview after the stricter
completion check. Root Release and standalone Debug pass all3contracts, including
timeline reset/replay/skips/overflow and240/360/480/1080-line layout. Hosted
CI40164486113 atf4d6ed2 is green. Full release qualification remains open.

## Real message-console lifecycle (I25)

Registered message-consoles uses native message/console APIs: HUD rollback opens
at10, switching to the game-message console at20 closes the first, and both close
at25. Sparse capture stride retains15/22/29 plus final30 without a large frame
dump. qual-consoles-20261024 passes003/006; inspected images show the correct
headers, actual loaded mission messages, wrapped fixture text and translucent
panel over the world, then no residual panel after closing. No renderer world
callback or alternate traversal was introduced. Root Release contracts pass;
hosted CI40165585015 at4b31483 is green. Source/UI coverage is advancing, but
ordinary no-probe release acceptance remains closed.

## Independent media canvas extent (I25, October 27)

Completed the inherited media-canvas work after reviewing the Sol continuation.
Decoded images now declare their own extent instead of changing game render
preferences or switching screen modes. Presentation fits the owned image to the
window; native screenshots query the published image extent. Subtitle layout and
font submission follow that same canvas. Mission playback pauses elapsed game
time without recreating world targets or retiring world camera history.

Root renderer_integration_check passes all three contracts. Private runtime
qual-media-native-checked-20261027 and qual-media-subtitles-checked-20261027 pass
real 640x480 MVE/native TGA export and 640x360 paletted/subtitled playback with
SSAA4/MSAA4, effects and motion blur. Configuration/generation and simulation
state are unchanged; resumed frames20/21/22 retain camera history. Exact decoded
source comparisons pass outside intentional subtitles; native TGA matches the
owned movie image. Subtitle words and resumed gameplay were visually inspected.

Fixed a qualification bug discovered during review: the movie pixel comparison
overwrote prior failed scenario checks. It now accumulates failure, and both
scenarios were rerun with that correction. Interactive fullscreen switching and
ordinary no-probe release qualification remain open; these captures do not claim
either one or a performance improvement.

## Main-menu display producer and compatibility raster coordinates (October 27)

The actual MenuScene producer now begins an encoded display canvas. Its decoded
looping video/static art and subsequent UI draw into that canvas, not world
radiance targets. Removed the unused framebuffer-copy API, its backend chunked
bitmap state/conversion and the sequence decoder's meaningless enable/disable
calls (193 deleted lines). Repository call-site/export search found no consumers
of the copy entry; the separate live chunked UI drawing APIs remain supported.
Historical renderer/ref source is not compiled and remains untouched.

Background main-menu qualification runs 1200 real UI iterations, logs decoder
loop boundaries, exports the completed menu and exits through normal menu
destruction. The explicit capture flag requires background mode and suppresses
input. Persistent settings are not saved. The first effects/native comparison
exposed 2128 differing pixels: FillRect still mapped its one-pixel border through
world SSAA/overscan while targeting the display canvas. A target-owned raster
mapping now governs compatibility rectangle clears and point size; queued text
is submitted before a rectangle clear. Pure tests cover native, overscanned and
offset/scaled modal spaces. World framebuffer-sizing helpers retain their
world-only meaning rather than becoming dependent on incidental current targets.

menu-raster-{effects,clean}-20261027 both complete with repeated decoder loops.
Their 640x480 exports are exactly identical, fully opaque, comparing AO/bloom,
SSAA4/MSAA4 against effects-off SSAA1/MSAA0. Menu artwork/text were inspected.
Root integration build and all3contracts pass. Earlier ordinary003/006 captures
pass after menu routing; no performance improvement is claimed from these tests.
One attempted interactive run captured the user's mouse; it was immediately
closed, and further verification is background-only. Interactive acceptance is
still open, not silently substituted by these fixtures. Candidate, Previous
Candidate and lib/gitinfo.h remain untouched.

## Nested modal presentation ownership (October 27)

The pre-change TelCom fixture reproduced a stale game-loop decision: its nested
automap presenter had consumed the game display and presented its own frames,
but the outer loop still called EndPostPresentFrame based on its earlier Begin.
The loop now reads pending ownership after nested export/modal callers. The
backend rejects missing ownership before any blit/swap/framebuffer rotation,
instead of only when updating the publication after those side effects.
qual-telcom-ownership-20261027 passes the actual TelCom map capture and exits;
qual-telcom-before-20261027 remains failed evidence. This does not qualify map
controls or return to active gameplay. No ownership assertion was weakened.

## TelCom draw-local sampling (October 27)

TelCom no longer rewrites Render_preferred_state.mipping/filtering or calls
SetPreferredState merely to configure UI texture sampling. It disables mipmaps
and filtering through existing draw-local state and restores the caller's state
on exit. Its existing screen-mode transition is unchanged and remains a larger
composition/lifecycle migration target.

qual-telcom-sampling-20261027 completes and was visually inspected. This is not
pixel-identical: scene-owned automap geometry now honors configured world
mipmapping rather than TelCom's temporary preference override. In the fixed map
monitor region6677pixels differ, maximum channel difference22/255; animated
shell lights also differ. The image remains comparable, but this explicit policy
change is not claimed as exact legacy equivalence or a measured speedup. Root
integration and all3contracts pass. No interactive window was launched.
