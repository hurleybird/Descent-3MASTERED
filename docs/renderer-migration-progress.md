# Renderer migration checkpoint

## Continuation: unclipped intermediate radiance

Scene, resolve, composition and bloom-pyramid color storage now uses RGBA16F;
coverage masks remain normalized RGBA8. No new tone curve, texture color-space
conversion or lighting response was introduced. Intermediate clipping cannot
discard bright additive radiance before eligible-light subtraction. The GPU
self-test confirms RGB (2, .5, .125) survives the actual MSAA color resolve;
the reference algebra also tests overbright additive composition and AO.

Full build and 4,887 CPU checks pass. Both eight-save corpora complete in
`scene-float-radiance-clean` / `scene-float-radiance-effects`; 001/002 effects
images were inspected. Clean-image mean errors are .0014-.3911 (largest on
layered smoke); effects mean errors .2626-.6602 with larger local highlight/
edge differences. These are precision/clipping changes, not byte parity.
Memory/performance and the ordering of AO versus nonlinear SSAA filtering still
need qualification; storage precision alone does not settle that ordering.

## Continuation: depth-owned material classification

The former motion resource is now a scene surface-record resource: RG velocity,
B correspondence validity, A semantic surface class, plus paired integer
identity. AO reads class from this nearest-depth-selected record, not the
averaged post-effect coverage mask. Non-temporal AO also requests the record;
opaque export resolves it before canvas depth changes, including paused draws.
The GPU self-test additionally verifies exact class 3 from the selected sample.

Surface bindings now own a validated occlusion-class enum. Game preparation
resolves terrain/model/interior/exterior and the existing rock/rubble content
heuristic; the executor no longer infers class from geometry kind. The old room
adapter uses the same content-classification function. No new image allocation
was needed: classification occupies the surface record's fourth component.

Full build and 4,886 CPU checks pass. All eight `scene-surface-record-effects`
and both rotating/deformed probe captures pass strict GL checks. The subsequent
`scene-authored-class-effects` corpus also passes all eight: six images are
byte-identical to the preceding run; saves 005/006 change with restored material
classification (mean RGB error 0.1451 / 0.000784). Images 000/002/005 inspected.
Paused interactive behavior, memory/performance and full compositing remain
qualification work, not implied complete by these capture results.

## Continuation: owned model motion and explicit temporal validity

Model motion now composes previous object/part transforms and paired current/
previous prepared vertices, including deformation starting or ending. New
objects, changed model assets, previously hidden parts and invalid camera
history cannot claim a stationary correspondence. The scene writes velocity,
validity and identity with opaque depth; the coherent MSAA resolve preserves
validity. Temporal AO rejects invalid dynamic and static-camera projections.

Full build and 4,884 CPU checks pass. All eight clean and effects saves complete
in `scene-motion-record-clean` / `scene-motion-record-effects`. Seven clean
images match the prior sample-resolve checkpoint exactly; 002 has the known
Buddybot pose variation. Effects 001/004/005 are exact; other changes are small
overall, with the largest expected temporal differences on moving models.
Fogged 001/002 and deformed 000 screenshots were inspected.

Inspection-only GPU readback in `scene-motion-gpu-readback` verifies actual
model records, not just the synthetic resolver: both saves 000/002 have zero
valid model pixels on frame 0 and the frame-30 cut, then nonzero motion and
validity on every model pixel at frames 31/59. The rotating-camera/deformation
fixture does not mutate simulation state. Material-class sample coherence,
blur-specific policy and full frame composition remain pending.

## Continuation: coherent multisample surface records

Scene framebuffer color/depth attachments are now texture-backed even under
MSAA. A shared nearest-depth-sample kernel defines the resolved surface. Depth,
soft-particle depth, velocity and integer identity use that same rule; velocity
and identity resolve together, replacing two independent blits that could pair
averaged velocity with another sample's object ID. Opaque motion resolves before
later canvas work can clear live depth. Color still uses normal sample averaging.

An inspection-only GPU self-test writes deliberately conflicting per-sample
depths, vectors and IDs, including a depth tie. Actual GPU readback confirms
sample 1 / depth 0.2 / velocity (1,11) / identity 101. No game assets or simulation
state are involved. Full build and 4,875 CPU checks pass; all eight clean and all
eight effects captures complete in `scene-sample-resolve-clean` and
`scene-sample-resolve-corpus`. Seven clean images are identical to the radiance
checkpoint; save 002 has the previously observed Buddybot pose variation.
Effects changes are small overall but include expected depth-edge changes;
001/002 were visually inspected. This does not complete dynamic-model motion,
AO material-class sample selection or performance qualification.

## Continuation: view-owned temporal history

A pure history store keys completed views by kind and history identity. Preparing
a view does not advance history; replaying the same presentation retains the
same predecessor. Reusing a key for a different camera in one presentation is
an error. Scene lifetime, camera owner/mode, explicit cuts, projection/viewport
changes and skipped presentations invalidate reuse. Main-world post processing
consumes this state rather than letting a later canvas draw choose the previous
camera. Game view extraction supplies forward/rear mode explicitly.

Full build and 4,872 CPU checks pass. All eight `scene-history-effects` captures
complete; differences from the prior radiance corpus are at most two channel
levels (AO history/jitter now starts from the explicit reset). A new rotating
camera fixture cuts at frame 30, with distinct manifests so it cannot be mistaken
for normal-view parity. Both `scene-history-probe-cut` and
`scene-history-probe-motion` complete for saves 001/002; cut 001 and moving 002
screenshots were inspected. The motion fixture repeats exactly for 002; 001 has
only eight changed pixels, maximum one channel level, not a byte-identical claim.
Dynamic-object motion vectors and effect/consumer-specific history validity still
need migration; this checkpoint establishes camera-history ownership only.

## Continuation: scene image products and surface-radiance AO

Removed the game-facing `CaptureBloomSource` API and late game-loop capture. The
view schedule now exports opaque color/depth before blended effects, and exports
AO-eligible radiance after those effects. Post consumers receive matrices from
the exact resolved scene camera rather than the last legacy draw callback.
Shared perspective unprojection replaces three negative-eye-Z/unit-near-plane
assumptions; AO depth linearization and off-axis reconstruction derive directly
from projection coefficients. CPU tests exercise translated/rotated/off-axis
cameras, a non-unit near plane, background rejection and product ordering.

The scene emits an independent half-float eligible-radiance image. Fog/emissive
radiance is excluded at its source; alpha layers attenuate the underlying image,
additive layers preserve it, and multiplicative layers multiply it. Deferred AO
subtracts only the occluded portion of that image. The previous fog-suppression
estimate and min-of-two-coverage rule were removed from this compositor. Material
class metadata remains owned by the solid depth sample, not particles. Legacy
bloom protection still needs its own upstream migration; this is not a claim of
complete frame-composition replacement. Overbright/clamped color precision,
4x supersampling reduction and temporal-history lifecycle need qualification.

Full build and 4,855 CPU checks pass. All eight `scene-radiance-final-clean`
images exactly match `scene-output-clean`; all eight combined AO/bloom + 4x MSAA
/ 2x SSAA captures in `scene-radiance-final-effects` complete with strict GL error
checking. Saves 000/001/002/003/007 were visually inspected across the radiance
runs, including fog, smoke and ordinary room/model views. Earlier
`scene-output-clean` differed from the effects checkpoint only in the known
Buddybot pose variation in save 000. This remains inspection-only, not gameplay
or full performance acceptance. Candidate/Previous Candidate remain untouched.
No shadows.

## Continuation: owned billboards, material blending and pass dependencies

Visual-effect extraction now publishes immutable sprite instances, explicit
animation frames and material blend modes over one shared quad. Camera-facing
alignment is resolved from the same view transform in CPU visibility and GPU
execution; planar effects retain a world basis. Unsupported beams/weather remain
counted as pending, not dispatched through old drawing callbacks. Animation time
and angle selection share pure kernels with the remaining simulation adapters.

The view schedule owns background, opaque, depth-snapshot and blended phases.
Soft particles consume an explicit opaque-depth product, including MSAA resolve,
instead of reading a moving render target. Indexed geometry and typed material
policy are shared with rooms, models and terrain. Four legacy draw-only helpers
incorrectly changed read-buffer state; those side effects are removed. Strict GL
error diagnostics in scene inspection now pass all eight saved views and both
smoke-heavy views with 4x MSAA / 2x supersampling.

Full build and 4,838 CPU checks pass. `scene-effects-final-clean` completes all
eight saves; 001/002/007 screenshots were inspected. Repeat 001/007 images are
byte-identical. The small 000 delta coincides with a different Buddybot pose hash
and position (also present in earlier captures); it is not certified pixel parity.
`scene-effects-antialias` 001/002 were inspected without AO/bloom. Combined
`scene-effects-msaa` captures expose a real remaining composition gap: the new
scene lacks the old compositor's coverage contract, and its opaque capture occurs
after translucency. Fogged walls are incorrectly darkened by AO. The next change
must move scene outputs/pass ownership upstream, not special-case these saves.

This is still inspection-only, not a complete playable renderer. Candidate and
Previous Candidate are untouched. No shadows were added.

## Continuation: fog belongs to immutable connected scene space

Fog-enabled rooms with matching authored color/depth connect through reciprocal
open portals. Internal portal faces are removed from the compiled boundary;
remaining room walls/portals close it. Floating trigger sheets explicitly do not
bound space. A renderer-owned immutable fog asset holds this boundary and a
stackless spatial index; exact source keys include geometry revisions, fog values
and portal connectivity. Unchanged frames reuse it, and level reset preserves
already-held assets while rejecting them from the new lifetime.

CPU containment and GPU segment queries traverse the same prepared index. Fog
measures entry/exit intervals, including clear gaps, instead of inheriting the
current room's mutable draw state. All world geometry uses the same fragment
evaluator; additive materials attenuate without adding fog color. Authored linear
camera-forward depth remains the transfer law. Exterior distance/color is frame
data. Background meshes are excluded. Multiple different fog colors currently
use optical-distance-weighted linear mixing: heterogeneous overlapping/crossed
volumes still need targeted visual qualification, not an assumption of old pass
order parity. Mirror/particle/composition integration remains pending.

The corpus includes levels with 2,636, 9,204 and 12,060 boundary triangles; neither
CPU preparation nor GPU upload repeats full geometry for an unchanged asset.
Only view containment updates per camera. There is no alternate unindexed draw
path. 4,820 CPU checks and the full game build pass. Checks cover entry/exit,
shared diagonals, connected-room seams, clear intervals, trigger exclusion,
authored color separation, revision retention and cross-level rejection.

All eight `scene-fog-clean` captures complete: five are identical to the sky
checkpoint, while saves 001/002/007 gain fog. Those three were visually inspected;
001/002 were also compared visually against the preserved complete renderer.
They repeat byte-for-byte in `scene-fog-repeat`. All eight `scene-fog-indexed`
captures exactly match the initial query implementation, and all eight
`scene-fog-owned` captures exactly match indexed output. Both exterior fixtures
were visually inspected and repeat exactly in `scene-fog-owned-sky`. These checks
do not establish complete renderer parity or interactive performance. No shadows.

## Continuation: owned environment meshes and background depth policy

Generic immutable mesh assets now carry optional authored vertex colors and
prepared transforms/materials. A shared material layer explicitly distinguishes
background from world geometry: background draws first, never tests/writes world
depth, and retains authored compositing order. View-local camera anchoring keeps
the sky's half-height parallax without modifying prepared assets. The executor
owns viewport-scoped background color clearing and mesh GPU lifetime.

Sky dome and horizon extraction consumes authored geometry/UVs/colors and the
existing high-resolution image catalog (moved out of terrain drawing). Rotation
evaluates simulation time instead of repeatedly mutating horizon vectors during
main-view drawing. This deliberately replaces visibility-dependent accumulated
rotation, so phase is not claimed identical to the old implementation.

Full build and 4,600 CPU checks pass. All eight `scene-environment-clean` images
are pixel-identical to `scene-terrain-final-clean`. Both downward outdoor probes
and both new horizon-facing `scene-sky-probe` images were visually inspected;
the latter exercise the textured domes and terrain/building depth separation.
Probe manifests distinguish the camera fixture; these are NOT legacy exterior
goldens. Stars, satellites, weather, fog and temporal sky streaks remain pending.
No domain is marked complete, Candidate remains untouched, and no shadows added.

## Continuation: renderer-owned terrain grid, patches and exposure

Terrain extraction publishes immutable height/material/visibility/clip-plane
revisions, never LOD scratch or camera-selected cells. One CPU geometry kernel
expands the authored diagonal, texture rotation/tiling and lightmap coordinates;
16-cell patches group by material for GPU upload/draw. Existing seam planes clip
this canonical geometry with interpolated UVs (including the original .005 plane
tolerance), so shader-only cuts are no longer hidden from other consumers.
The scene executor owns terrain GPU buffers and uses the shared image assets,
material policy and emitter table. View-local patch queries respect exterior
portal reachability; lighting uses conservative patch bounds and exterior
eligibility, independently of camera visibility. No old terrain draw fallback.

The authored eight-bit height exposure field is owned by the terrain asset. The
same pure interpolation routine serves model ambient lighting and the simulation
adapter used by outdoor damage. It preserves the legacy 43-unit vertical interval
and boundary sampling; below-ground heights are now clamped rather than invoking
a negative bit shift. The old sampling implementation was removed from drawing.

4,590 CPU checks pass, including all 65,025 cells under all four rotations,
asset lifetime/revision retention, hidden cells, clipping/UV interpolation,
patch/portal visibility, light eligibility and 1,000 exposure interpolation cases.
Full build passes. All eight `scene-terrain-final-clean` images are pixel-identical
to `scene-camera-clean`. Two outdoor fixtures in `scene-terrain-probe` were
visually inspected; both repeat identically through the clipping/exposure changes
in `scene-terrain-clipped-probe` and `scene-terrain-final-probe`. These fixtures
prove outdoor execution, NOT parity against a complete legacy exterior view.

Still pending: sky/effects/fog, legacy quality/LOD policy, targeted entrance seam
visuals, exterior golden captures, performance and full frame composition. Terrain
is not marked complete. Candidate/Prev Candidate remain untouched; no shadows.

## Continuation: camera shake no longer rewrites simulation poses

Removed ShakePlayer/UnshakePlayer and the player-orientation save/restore loop
around rendering. Simulation advances shake decay and controller feedback once;
presentation publishes owner-keyed rotation with deterministic visual noise.
View resolution applies it without moving physical geometry, and the canonical
headlight direction uses the same rotation. CPU visibility and GPU camera uniforms
consume the same resolved camera. Detached/other-object views remain unaffected,
and resolving an already resolved view cannot apply the rotation again.
The controller shake effect also no longer consumes gameplay RNG.

3,432 scene checks pass, full build passes. All eight `scene-camera-clean` captures
passed the presentation RNG guard. Seven are pixel-identical to
`scene-quality-pixel`; save003 is the actual nonzero-shake fixture and intentionally
uses a different deterministic perturbation (mean RGB delta 14.022338). Its first
and target logs confirm nonidentity rotation. Saves 002/003 were visually inspected
and repeat byte-for-byte in `scene-camera-repeat`. This corpus also includes the
explicit texture-clock change from the preceding checkpoint. Pause/device hardware
and full cockpit composition still require interactive qualification. No shadows.

## Continuation: frame lighting quality and material clock

The published frame owns vertex/per-pixel lighting selection. Both shader stages
use one GLSL emitter evaluator, canonical light table and influence indices;
there is no low-quality legacy-renderer fallback. This is a vertex-sampled
approximation of the same lighting field, not parity with the old mutable
low-quality lightmap implementation. All eight `scene-quality-pixel` captures
are pixel-identical to `scene-material-clean`. All eight vertex-mode captures
passed; saves 003/005 were visually inspected, including expected coarse lighting
on close geometry. The harness records quality and requires diagnostic mode for
cross-quality comparisons. CPU tests validate frame ownership and invalid enums.

Material animation/procedural resolution now accepts the presentation clock;
the legacy GetTextureBitmap accessor is only a game-clock adapter. This final
clock-only change builds but still needs the next runtime corpus. Earlier quality
captures predate it. No domain or full renderer declared complete.

## Continuation: published material coverage policy

Publication now compiles solid/cutout/alpha/additive policy from surface, image
format and vertex alpha. Visibility sorting, GPU blending/depth writes and portal
transparency consume this contract instead of independently inferring state.
Fractional texture alpha no longer silently uses opaque depth writes; additive
materials consume their texture alpha even without TF_TMAP2. Room 4444 specular
masks remain explicitly excluded from color coverage by material authoring.

3,424 checks pass; the full build and all eight `scene-material-clean` captures
pass. Seven saves are pixel-identical to `scene-procedural-rng-guard`; save002 has
mean RGB delta 0.026181, and its Guide-Bot pose fingerprint differs. That image was
visually inspected. The corpus does not establish GPU coverage for every alpha
format; those policies have CPU tests and need dedicated visual fixtures as the
remaining effects/material families migrate. No pending domain marked complete.

## Continuation: procedural evaluation inputs

Procedural kernels now receive a local evaluation context carrying canonical
60 Hz time, texture/tick-seeded randomness and destination storage. Removed shared
procedural RNG/tick/destination and all gameplay RNG/wall-clock reads from those
kernels. The existing bounded catch-up rule remains; disabled procedural detail
uses simulation time, initializes its first image, and handles clock rewinds.
Persistent fire/water state and the shared legacy element allocator still need
ownership migration; this is not a claim that the entire procedural subsystem is
independent of resource pressure or historical evaluation order.

3,417 scene checks pass and the full game builds. All eight saves passed both
`scene-procedural-clean` and `scene-procedural-rng-guard`; the latter asserts that
the whole presentation preparation leaves gameplay RNG unchanged. Six saves are
pixel-identical to `scene-image-byte-exact`. Saves 002/003 differ (mean RGB error
0.333739/14.261854), and were visually inspected. Both repeat byte-for-byte in
`scene-procedural-repeat`. The earlier player/model pose fingerprints also differ
from the new runs, so these are not equivalent-state texture-only comparisons.
Removing renderer consumption of game RNG intentionally changes subsequent
simulation draws; do not claim every observed pixel change is fully diagnosed.
No Candidate deployment, cockpit edits or shadow work.

## Scope and status

The user approved renderer rearchitecture and background save-game comparisons,
with an explicit gate: **do not touch shadows until the new renderer is verified**.
The replacement has started at the game/renderer scene boundary. The development
game target now builds with an explicit **room-geometry inspection executor**.
Normal presentation still rejects incomplete domains; this is not a playable or
accepted full replacement. Scene/extraction tests pass, and all eight saved-game
inspection captures loaded/rendered/exited successfully. No shadows were added.
Do not mistake the frozen baseline results below for new-renderer results.

## Runnable inspection checkpoint

The user requested an explicit checkpoint before continuation. Annotated Git tag
`codex/renderer-room-inspection-checkpoint` points to `f20ddf8`. Its executable,
shader HOG, launcher source and migration notes are copied under
`C:/tmp/D3RendererVerification/checkpoints/room-inspection-f20ddf8`.
The preserved executable hash matches the verified test build below.

## Continuation: shared camera and view-local visibility

`renderer/SceneView.h/.cpp` now owns the canonical positive-eye-Z projection,
legacy `1 - 1/z` depth convention, anchored/overscan camera preparation and
conservative frustum query. The GPU consumes those projection coefficients;
it no longer computes a separate projection or selects/sorts faces itself.

Queries return frame-identified per-view surface lists and object-sphere
candidates without modifying the presentation. Only visible room assets are
uploaded on demand, but off-camera room geometry remains in the complete scene.
This is a conservative frustum reference, NOT portal/occlusion traversal or a
gameplay visibility report. Object bounds/poses and rendering remain unfinished.

Validation:

- 71 scene/extraction/view checks pass, including 4,000 deterministic points
  comparing CPU half-spaces to the GPU homogeneous clipping contract.
- Tests cover wide/portrait/anchored views, near-plane tangency/crossings,
  large polygons covering the viewport while their vertices are outside,
  translated/rotated/rear cameras, transparency ordering, object exclusion,
  immutable off-camera data and independence between main/auxiliary queries.
- Full application builds. New verification runtime is `scene-view-test`;
  the original user-facing `scene-test` runtime was NOT overwritten.
- Two complete eight-save runs passed: `scene-view-corpus-clean` and
  `scene-view-repeat-clean`.
- `scene-view-parity.json`: seven saves are pixel-identical to the room-inspection
  checkpoint. save002 mean RGB delta is 0.001001881/255; 0.02235243% of pixels
  differ by more than two levels, maximum nine.
- `scene-view-repeatability.json`: the same seven saves repeat identically;
  save002 has mean delta 0.001346933/255 and 0.03125% above two, maximum seven.
  This supports run-to-run variation, not a claim that every save002 difference
  is proven harmless. The save002 image was visually inspected.
- No objects, terrain, lighting, effects, cockpit, portal feedback or shadows
  were silently marked complete by this change. Continue their migration.

## Continuation: instance-owned model pose evaluation

`model/ModelPose.h/.cpp` is now the single production animation evaluator.
It reads authored tracks, accepts explicit simulation time, and returns owned
submodel angles/translations without touching shared `bsp_info` scratch, RNG,
render state or motion history. Hierarchy composition returns owned local-to-model
transforms, handles parents appearing after children, and rejects invalid/cyclic
parent chains. It preserves the legacy matrix-interpolation and timed/null-input
conventions; this is an ownership migration, not a new animation aesthetic.

The old interpolation implementations and separate SetModelAngles/SetModelInterpPos
entry points were removed. SetModelAnglesAndPos is a remaining simulation/legacy
scratch adapter to the one evaluator, with its old mask semantics. Both
GetPolyModelPointInWorld overloads now own their pose and transforms instead of
rewriting model scratch. Existing explicit submodel offset adjustments are applied
to the local pose; no cancelled cockpit geometry work was resumed. Attached
effects no longer pre-write shared pose before their point queries.

Validation and discovered defects:

- 2,119 CPU checks pass in total. A test-only frozen copy of the former evaluator
  compares 404 combinations of timed/uniform tracks, model formats, animation
  inputs and clocks, plus null inputs. Checks also cover independent held poses,
  no source mutation/RNG calls, single-key tracks, invalid input, rest-pose bank
  initialization, and hierarchy position/direction parity with the old point walk.
- The full game builds. Initial captures passed saves 000/001 but failed 002 in
  collision evaluation: SetNormalizedTimeObj could leave timed player/weapon and
  unassigned track inputs uninitialized. It now initializes every output slot.
  All in-repo callers provide MAX_SUBOBJECTS arrays. This was fixed at normalization,
  not hidden by weakening the evaluator's input checks. Failed evidence is retained
  in `model-pose-corpus-clean`; the focused successful retry is
  `model-pose-normalized-clean`.
- Final eight-save run `model-pose-final-clean` loaded/captured/exited successfully.
  `model-pose-final-parity.json` compares against `scene-view-corpus-clean`:
  seven exact captures; save002 mean RGB delta 0.001182364/255, 0.027126736% pixels
  above two levels, maximum seven. Save002 was visually inspected. These magnitudes
  are within previously measured repeat variation, not proof of full equivalence.
- The prior view-query executable/HOG is preserved in
  `checkpoints/view-query-b7951bf`; current verification binary is in `scene-view-test`.
  User-facing `scene-test`, Candidate and Previous Candidate were not replaced.
  Candidate executable/HOG hashes remain unchanged.

Limits/next upstream cut: this does NOT publish model assets/instance poses into
the immutable scene yet, render objects, or complete ObjectPreparation. The
evaluator still reads legacy authored model storage at preparation time. Move
authored model geometry/hierarchy into revisioned immutable assets, normalize
instance animation/effects once during preparation, and publish owned poses for
all views. Collision still consumes the explicit scratch adapter; audit/migrate
its consumers rather than claiming all shared state has disappeared. No shadows.

## Continuation: immutable model assets and shared mesh execution

Room and model polygons now share `MeshGeometry`, one conservative surface query,
one opaque/transparent ordering, one GPU mesh upload implementation and one surface
material resolver. `SceneAssets` owns revisioned model topology, hierarchy,
vertex normals, material slots and flat colors. Published frames own instance
poses/materials; multiple objects share geometry without sharing animation scratch.
Object poses are evaluated once in PrepareGamePresentation and shared by main and
auxiliary views. Previous submodel poses are retained by object handle/asset revision.

`model/SceneModelAsset` compiles legacy authored model data without game globals.
The first runtime attempt exposed non-finite imported vertex normals. Invalid or
missing shading normals are reconstructed from incident polygon normals; unused
vertices have zero normals. Raw source data is unchanged. Capture diagnostics record
repair counts (PyroGL, NapalmRocket, Smart, cloak in save003). The scene boundary
continues rejecting invalid geometry/materials. This is tested with synthetic NaNs.

2,143 CPU checks pass. Full application builds. `scene-model-corpus-clean` and
`scene-model-final-clean` each passed eight captures; all eight first-run images
were inspected (save003 in `scene-model-normal-fixed` is the same visual stage).
Visible pickups, robots, signs and door parts now pass through the new executor.
Models are deliberately not fully lit yet. This is NOT full-renderer parity.
`scene-model-first` preserves the failed asset-validation evidence.

Remaining model limitations are explicit: old-format geometry, camera LOD, detached
debris pivot policy, special facing/glow/custom submodels, deformation/cloak,
instance lightmap UVs, dynamic/static object lighting and complete pose-history
cut policy. Model geometry is compiled/compared once per referenced model per
presentation (not once per instance/view); source revision tracking/performance
optimization remains. Pending domains are not cleared. User-facing `scene-test`
and Candidate are unchanged; working binary/captures use `scene-view-test`.

Next upstream cut: canonical scene emitters/material evaluation, removing the
simulation-to-dynamic-lightmap/per-draw transport rather than reconnecting it.

## Continuation: canonical scene emitter table

Visual emission now happens in PrepareGamePresentation, independent of camera
visibility and simulation draw marks. `SceneLights.cpp` compiles player headlights,
player glow/balls, object/weapon/explosion lights and visual-effect lights into one
owned emitter table. Object allocation handles plus source channels identify
emitters; visual-effect allocation generations were added outside its public ABI
struct. Signed artistic colors and existing pulse/flicker/radius/cone conventions
are retained. The shared visual-noise kernel takes explicit simulation time.

The old DoObjectLight implementation and simulation calls, visual-effect movement
light application, and GameRenderFrame's per-lightmap renderer upload were removed.
DoObjectLight and ApplyLightingToWorld are compile barriers. The former world
application body remains named RetiredApplyLightingToWorld for the deletion audit;
it is not an execution fallback. Destructive/baked lightmap updates and their cleanup
are not casually deleted with the obsolete dynamic-light transport.

`SceneLighting` compiles uncapped surface index lists referencing the authoritative
table. Room portals conservatively restrict reachable indoor influence; geometric
range and directional-facing checks are view-independent. Exterior influence is
still deliberately conservative pending terrain migration. The GPU uploads the
emitter table and index buffer once per presentation and reuses them for all views;
no alternate copied SSBO/uniform light definitions exist on this scene path.
Auxiliary-view lighting policy remains explicit. Legacy additive-then-clamp room
diffuse evaluation is retained, with no shadow visibility term.

2,259 CPU checks pass; game builds; all eight `scene-light-corpus-clean` captures
succeeded. Headlight illumination was visually inspected in save005
(`scene-light-first`) and save002. Model ambient/lightmap/specular behavior and
exterior qualification remain unfinished. No pending domain is cleared yet.

Additional model repeatability finding: `scene-model-same-build-repeat.json`
shows six exact captures, save002's previous small residual, and a larger save000
variation (mean 0.031828342/255, 0.3519965% pixels above two, maximum 180).
This repeats across the same binary and must be diagnosed before acceptance;
do not dismiss it under the earlier room-only noise bound. Both images were viewed.
The model checkpoint executable/HOG is preserved in `checkpoints/scene-model-e2bbf7e`.

## Continuation: prepared surface lighting

Room assets now own authored RGB332 volume grids with immutable revisions.
One pure sampler and explicit-time pulse/strobe/flicker kernel serve scene
preparation and remaining simulation/retired-renderer adapters. The former
GetRoomDynamicScalar implementation was removed; ambient queries no longer
rewrite global Room_light_val. Room pulse modulation is prepared per surface.

Model instances now own ambient colors, directional-ambient policy and baked
corner UVs. Saved lightmap layouts are checked before indexing, including model
swaps, preserving the Level 2 layout safety fix. UVs stay per instance; normals
belong to shared mesh geometry. The renderer uploads one instance-UV stream per
presentation, reusing immutable geometry and the same scene shader for rooms and
models. Baked/ambient and emitter terms remain separate until evaluation.

2,371 CPU checks pass, and all eight `scene-surface-corpus-clean` captures pass.
All eight views were visually inspected (002/005 first in `scene-surface-first`).
Full parity is not claimed: per-pixel quality policy, specular and the previously
listed pending domains still need migration. Candidate/Prev Candidate unchanged.

Repeatability investigation: image comparison now reports changed-region bounds.
The earlier save000 difference is confined to x=556..646, y=376..464, around the
blue object. New capture-only field-wise pose and texture fingerprints distinguish
simulation variation from renderer/material variation. `scene-state-repeat-a/b`
have identical model texture fingerprints but different prepared pose/position
for object 6978, model 186 (position differs by about 0.0004 world units).
This is evidence of differing inputs, not proof that every earlier pixel delta
has that cause. First/target-frame probes are being used to trace the origin.

## Continuation: shared prepared geometry

`SceneGeometry` supplies one mesh-position view and triangle-corner expansion
for visibility, light influence, immutable uploads and per-frame position/UV
streams. Model instances own current/previous deformed positions; rigid instances
reuse authored positions. Cloak, fade, timed powerup deformation and object damage
color/shape are prepared before view execution. Radial jitter preserves its D3
shape rule, but uses explicit time/object/part noise instead of advancing gameplay
RNG in a draw. Viewer-dependent distortion, special models and effects remain
pending; this does not silently claim that entire domain is complete.

2,380 CPU checks pass, including an instance displaced beyond its authored mesh:
both visibility and light selection must use the prepared positions. Full build
and all eight `scene-deformation-corpus-clean` captures pass. Six are exact against
the preceding surface-lighting corpus; 000/002 retain their measured variations.
The explicit `-scene-deformation-probe` fixture modifies presentation only and is
restricted to inspection. Its save000 Guide-Bot and door geometry visibly deform
in `scene-deformation-bot-probe`, verifying the GPU stream. The initial save001
probe/control happened to show no difference and is not evidence of that path.

## Continuation: view-local portal apertures

`ScenePortals` replaces room/face visitation marks with owned per-view NDC windows.
It unions multiple openings, converges through cycles without a recursion-depth
cutoff, clips apertures at the eye rather than the geometry near plane, and consumes
prepared transparency and closed-door traversal policy. Both room and model
polygons use those windows. The complete scene remains available to light queries;
portal visibility never removes source geometry from the published frame.
Exterior connectivity remains conservative pending terrain migration. The
frustum-only reference is still available as a pure query for qualification.

2,391 CPU checks pass, including cycles, opaque/closed portals, the viewer inside
a closed doorway, pre-near apertures, offscreen/backfacing openings and multiple
windows into one destination. All eight `scene-portal-corpus-clean` captures pass
and were visually inspected. Six match the preceding corpus exactly. 000/002
differ; capture probes also show differing Guide-Bot pose inputs, so those deltas
are not an isolated visibility-parity measurement. Gameplay feedback remains a
compiler barrier; no AI/automap/effect timing is silently changed by this query.

## Continuation: immutable image assets

Scene surfaces now retain revisioned color/baked image assets, including authored
mips, truecolor bytes and lightmap storage extents. Publication compares borrowed
source bytes once per referenced image per frame and clones only changed revisions.
Held frames survive source edits and level unload. Scene execution no longer calls
BindBitmap/BindLightmap or reads GameBitmaps/GameLightmaps; renderer-owned GL images
are uploaded by asset identity. The remaining canvas cache is explicitly invalidated
at the boundary. Missing prepared images fail execution rather than selecting a
legacy upload fallback.

An ownership bug in old FreeImages was corrected: it deleted assumed sequential
GL IDs, not the actual names allocated to that cache. Cleanup now deletes its
recorded names, leaving scene/framebuffer resources alone.

2,403 CPU checks pass, including exhaustive 65,536-value conversion comparisons
for both packed formats, immutable revisions/mips/truecolor and malformed inputs.
Initial `scene-image-corpus-clean` exposed widespread one/two-level color changes
from driver 5-bit expansion. The compiler now preserves the original float-to-byte
truncation. All eight `scene-image-byte-exact` captures pass; six match the portal
checkpoint exactly. 000/002 retain the documented differing input/pose captures.
Lightmap padding is explicitly zero, not uninitialized upload-buffer contents.
Procedural texture evaluation still needs isolation from gameplay RNG/time and is
the next preparation-boundary work; image ownership alone does not fix that.

## Preserved room-inspection test build

- Runtime: `C:/tmp/D3RendererVerification/scene-test` (separate 2 GB copy).
- User launcher: `Launch Renderer Test.cmd` in that runtime. It invokes
  `tools/renderer-scene-test-launch.ps1`, copies pilot/save/counter from the
  preserved seeds, and defaults to saveg003. Supply `-Save saveg004` etc. to choose
  another of the eight copies. `-Capture` performs a deterministic hidden capture.
- Executor: `renderer/gl_scene.cpp`. Owns room GPU buffers keyed by scene
  lifetime/revision, uses explicit camera projection and per-view face lists,
  renders base textures/lightmaps and basic alpha materials. It never calls
  RenderMine/RenderTerrain/RenderObject. GPU caches are cleared across level
  lifetimes and before renderer/context teardown.
- Inspection is explicitly requested by `-scene-inspection`, not an automatic
  unsupported-material fallback, and is labeled on screen. The pending-domain
  mask remains intact. This does NOT mark gameplay visibility or claim full
  immutable texture/lighting/resource ownership has been migrated.
- `SceneMaterialBindings` contains the existing split diffuse/specular asset
  resolver moved out of render.cpp, not duplicated. The initial capture showed
  specular-mask textures as wall colors; moving and using this resolver corrected
  the inspected hazard-stripe scene. Destroyed textures and legacy texture-slide
  periods are resolved during preparation.
- Save thumbnails and the existing Game2DLL world-view slot now translate to
  the explicit view entry. Thumbnails do not advance gameplay transform history.
  ABI slot ordering and argument types remain unchanged. Full module compatibility
  has NOT been tested. Legacy terrain-to-mine calls fail conspicuously; the old
  mine implementation is renamed static migration source, not a callable fallback.
- World-side matcen/render-event drawing and cockpit/cinematic rendering are
  withheld in inspection mode, not represented as migrated. Normal mode remains
  unavailable. The launcher disables AO, bloom and motion blur for this first
  geometry test; post-pass integration is pending.

### Inspection evidence and limits

- 41 standalone scene/extraction checks, including save-thumbnail history isolation.
- `scene-corpus-clean`: eight deterministic inspection captures, all normal exits.
- `baseline-clean-seeded`: eight matching clean-profile baseline captures.
- `scene-vs-baseline-diagnostic.json`: explicit diagnostic comparison, NOT parity.
  Mean RGB differences span 14.33 to 55.73 (0–255). Missing content/appearance is
  substantial and has not been hidden behind an acceptance threshold.
- All eight inspection images were visually opened. Stable room landmarks and
  base textures are visible; save005 is dark without migrated dynamic lighting.
  The first failed-material and subsequent diagnostic captures are retained as
  evidence (`scene-first`, `scene-alpha-fixed`, `scene-material-fixed`).
- The exact launcher was smoke-tested with `-Capture`. Its final-build saveg003
  PNG is byte-identical to the corpus image (SHA-256
  `6E2842522E8A8A0F543C839E488971395C788428F5597BB74548A4EBDB034956`).
  Final test executable SHA-256:
  `194A10434746AEA051C9D4A59DEC9CC892414A709A98A772312FF62C1BED3953`.
  Candidate EXE/HOG hashes were rechecked and match the preserved values below.
- Test normal flight/look controls only, not combat or mission progress: objects
  (including doors and enemies), terrain, fog, specular, dynamic/headlight lighting,
  effects and cockpit remain unfinished. Simulation is still live, so invisible
  objects can still collide/interact. Use the isolated copies and do not save over
  original gameplay files. No runtime was deployed to Candidate or Prev Candidate.

The executable is something the user can inspect, not the end of renderer work.
Continue migrating the omitted domains and pass/visibility/feedback ownership;
keep shadows gated until full acceptance, not merely this inspection milestone.

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

### First-cut build blockers (historical) / migration queue

At the first boundary cut, the full game build was attempted with Ninja `-k 0` to expose all compilation
failures rather than stop at the first. Log: `out/renderer-migration-build.log`.
The observed blockers were (see runnable checkpoint above for current disposition):

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
