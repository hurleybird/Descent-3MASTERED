# Renderer rearchitecture and dynamic shadows

Status: proposed plan, not implementation authorization.

## Decision

Replace the core 3D rendering contract, not just the shadow implementation.
The game will publish renderable scene state and view requests; the renderer
will own visibility queries, pass construction, lighting evaluation, resource
lifetimes, and execution. Rendering a second view must not replay game-side
object preparation or mutate simulation state.

This is a coordinated replacement on an isolated development branch. Intermediate
builds may be broken. There will not be a permanent old/new world-renderer toggle,
nor a successful release with core scene families still using a fallback path.
Milestones below are dependency and verification gates, not separately shippable
compatibility layers. Git history and a preserved executable/data baseline are
the rollback mechanism.

Keep OpenGL, game data formats, the authored visual style, and useful algorithms.
Do not turn this into a new graphics API, PBR conversion, or gameplay rewrite.

## Audit basis and confidence

Inspected checkout: `C:/Repos/DESCENT-3MASTERED`, master at `664aefc`, with existing
tracked and untracked user changes. No applicable AGENTS.md was found in the
ancestor directories or repository scan. The cancelled cockpit work is not part
of this proposal. Candidate and Prev Candidate were not changed or rebuilt.

This is source/history analysis, not a measured performance or runtime audit.
The recovered August shadow patches provide evidence about the old design, but
do not establish a unique cause for every visual artifact. Existing screenshot
and capture infrastructure is reusable; a full regression corpus still needs
to be assembled and exercised.

Key evidence:

| Existing location | Finding | Architectural consequence |
| --- | --- | --- |
| `Descent3/render.cpp`, BuildRoomList / RenderMine | Camera visibility and mutable face/room marks drive submission; room/terrain rendering call into one another | Scene availability must precede per-view visibility; query results must be view-local |
| `Descent3/render.cpp`, RenderRoom | Drawing updates automap and last_render_time | Visibility feedback must be separate from GPU execution |
| `Descent3/renderobject.cpp`, RenderObjectInternal | Drawing sets rendered flags and populates AI_RenderedList | Additional passes must not count as additional gameplay observations |
| `Descent3/viseffect.cpp`, effect creation | Recent room/terrain rendering gates effect creation | Preserve the existing feedback timing deliberately; simply deleting writes changes behavior |
| `Descent3/renderobject.cpp`, RenderObjectOpaque / RenderObjectTransparents | Transparent rendering replays object rendering and restores random state | Prepare visual state once, then classify its draw items |
| `model/retained_polymodel.cpp`, PrepareSubmodel | GPU deformation reproduces a global random stream consumed by the CPU path | Freeze visual deformation state once per presentation frame, independently of pass count |
| `model/retained_polymodel.cpp`, NeedsLegacyClip | Geometry can switch back to CPU rendering near the camera because clipping conventions differ | Establish one geometry/depth contract, including near-plane behavior |
| `model/polymodel.cpp`, PolymodelMotionBeginTransientTransform | Motion-history machinery supplies transforms even for models without motion IDs | Transform ownership must not depend on whether motion tracking happens to be active |
| `Descent3/lighting.cpp`, ApplyLightingToWorld / UpdatePerPixelLightmapRendererData / ClearDynamicLightmaps | Light propagation, copied per-surface lights, mutable lightmaps and object lighting are intertwined | Publish lights once; separate influence selection and legacy appearance evaluation from light identity |
| `renderer/gl_main.cpp`, SetPerPixelDynamicLighting / UpdatePerPixelLightmapLighting | Separate uniform and SSBO encodings carry different fields | One authoritative light record; all influence lists reference IDs |
| `hogfiles/descent3mastered/generic.frag`, ApplyDynamicLightmapLighting | Dynamic illumination is added to baked illumination and clamped | Shadow visibility belongs inside lighting evaluation, before clamping |
| `Descent3/terrainrender.cpp` | Game-side terrain code owns OpenGL resources and state; has retained/immediate alternatives | Preserve terrain generation/LOD, move GPU execution and ownership behind the renderer boundary |
| `renderer/gl_main.cpp`, CaptureBloomSource / BeginPostPresentFrameInternal | A bloom-named call owns broader scene captures, matrices, alpha and temporal boundaries | Replace incidental call ordering with an explicit frame schedule |
| `Descent3/Game2DLL.cpp`, exported function table | Modules can call legacy rend_* and g3_* functions | Preserve binary ABI through a bounded translator, not a second world renderer |

The recovered shadow code intercepted camera-submitted retained draws and replayed
them for the light. It could not recover geometry already rejected by camera
visibility. Its early replay omitted index-buffer identity and opacity sampling.
The stencil experiment changed counting method and side winding while debugging.
These findings support replacing scene ownership; they do not justify blaming
PCF or the absence of adjacency as exhaustive causes.

## Target architecture

### Game-to-renderer boundary

Game state remains authoritative for simulation, rooms/portals, object lifetimes,
asset loading, animation inputs, and authored data. An extraction layer publishes:

- Persistent level geometry and material assets, with explicit revision/lifetime
  handles. Do not copy the entire level every frame.
- A frozen presentation-frame packet of instances, current/previous transforms,
  submodel poses, deformation state, lights, transient geometry/effects, and views.
- Explicit asset updates for destruction, doors, material animation, lightmap
  changes, and level transitions. Distinguish permanent authored/baked changes
  from temporary dynamic illumination.

All core 3D content enters this contract: rooms, terrain, old/new polymodels,
procedural effects, decals, cockpit, mirrors, small views, cinematics, automap
and model previews. Some are separate layers/views, not separate renderers.

Extraction is not a wrapper that invokes the old drawing code into a recorder.
Split visual preparation from execution. Evaluate shared pose/deformation once;
derive view-dependent billboards and clipping from explicit view inputs. Preserve
required once-per-frame effect updates and separate visual RNG from gameplay RNG.
Pause, interpolation, teleport and repeated presentation of one simulation state
need explicit rules.

### Renderer-owned scene and views

- `SceneAsset`: mesh or specialized terrain asset, material ranges, bounds,
  baked-light references, sidedness and opacity data; generation-safe handles.
- `RenderInstance`: asset ID, pose/deformation, current/previous transform,
  object identity, spatial membership and draw policy.
- `RenderLight`: stable emitter ID plus lifetime generation, physical origin,
  type/cone/range, color and legacy attenuation policy. GPU indices may change;
  identity must not. Negative/artistic lights have an explicit supported policy.
- `Material`: opaque/cutout/blended/additive/multiplicative mode, textures,
  alpha test, lighting model, fog behavior, depth policy and effect participation.
  Authored specular fields are appearance data, not automatically physical lights.
- `RenderView`: camera/projection/depth convention, clipping, viewport/layer,
  history key, and whether it contributes gameplay visibility feedback.

These are proposed responsibilities, not prescribed final C++ names.

World positions and geometric normals have distinct meanings. One shared geometry
evaluation produces consistent positions for depth, color, velocity and shadows.
Declare projection/depth conversion once and reuse it across consumers. Resolve
the present near-clip discrepancy instead of retaining a CPU fallback. Preserve
current depth mapping where practical; reverse-Z is not a separate project goal.

Portal and terrain algorithms become renderer-side visibility/geometry services.
They produce per-view result lists, not shared global visibility flags. Light
queries use the same available scene, but not the camera's result list. Geometry
outside the main view remains eligible to cast into it. Conservative light-range
selection is a valid first correct query; portal pruning is an optimization that
must be checked against that reference. Room traversal is not a substitute for
geometric shadow visibility.

Return an explicit visibility report to a game-owned feedback step. Record and
preserve which existing main, mirror and auxiliary views currently affect AI,
automap and effect eligibility. Shadow views never do. Commit bookkeeping with
defined deduplication and timing; GPU pass count must not alter it.

### Lighting

Use one canonical light table and one semantic lighting implementation shared by
material shader variants. Surface/room/terrain influence lists may remain for
performance, but contain light IDs, not alternate light definitions. Selection
and overflow are deterministic, observable and shared with shadow allocation.

Preserve the intended D3 lighting response, including existing non-PBR choices.
Evaluate baked/ambient terms separately from dynamic terms. Apply each emitter's
visibility to its dynamic diffuse/specular contribution before the existing
clamping and fog operations. Do not subtract a guessed light contribution from
the completed scene. Intentional specular-origin approximations need named
semantics, not silent disagreement between paths.

Retire dynamic lightmap mutation/restoration as the core renderer's transport for
dynamic lights. Preserve any real game/module-facing lighting behavior through
explicit queries or adapters after auditing its readers. A supported lower-quality
lighting option can select vertex evaluation using the same scene/light data;
it must not select the old renderer. Removing user-facing options or changing the
art direction requires a separate decision.

### Frame execution and resources

Introduce a concrete pass scheduler with declared inputs, outputs, dependencies,
sample counts, dimensions and color/depth conventions. It need not be a generic
render-graph framework. The schedule explicitly represents world depth/color,
velocity, shadow maps, fog, transparent effects, AO, bloom, motion processing,
auxiliary views, cockpit, HUD and modal UI.

Do not blindly prescribe one universal transparent or fog order: preserve the
observed layer semantics, then encode them as material/pass rules. In particular,
opaque depth for AO/particles is distinct from later raster depth used by legacy
transparent ordering. Only opaque/cutout visibility participates in that solid
depth product. MSAA resolve policies must be explicit and compatible across
color, depth, velocity and classification products.

Masks can remain when they encode necessary semantics. Their meaning and writer
must be declared; framebuffer alpha cannot silently double as unrelated state.
The acceptance goal is fewer implicit dependencies, not an arbitrary mask count.

Keep existing effect kernels where sound; replace their orchestration. Give each
view its own temporal history and define invalidation for reload, resize, sample
changes, camera cuts, F2/pause, and renderer recreation. GPU resources belong to
the renderer, not terrain/game-side code. Retain platform/context/presentation
and image loading code where it has no conflicting ownership.

### Compatibility boundary

Keep exported ABI slots, signatures and necessary coordinate helpers intact.
The module adapter snapshots legacy state into explicit transient draw packets
for the new executor. Core world code may not call it. UI translation is allowed
at the canvas boundary; it must not bring back implicit world pass scheduling.

Already projected external geometry may only support view-local rendering; do
not invent world-space casters from insufficient data. Document and test this
limit separately from complete core-world shadow support. Inventory actual
module draw callbacks before finalizing their scheduling and readback behavior.

## Execution plan and gates

### 0. Freeze evidence and the migration contract

Before destructive code changes, preserve the intended working-tree baseline,
including relevant uncommitted work, without stashing/resetting/staging unrelated
edits. Record commit, patch provenance, binary/HOG hashes, configuration and saves.
Use an isolated `codex/` worktree for implementation; no deployment to Candidate
or Prev Candidate during the broken migration.

Assemble the regression corpus below. Add missing test instrumentation for scene
IDs, emitted packets, per-view visibility, light influences and pass resources.
Set CPU/GPU/memory budgets from measured baseline results, not invented targets.
Finish the remaining module/feedback/rare-material census. Produce a finite
mapping from every core producer to a replacement and every old path to removal.

Gate: baseline is reproducible, all producers have destinations, necessary
compatibility policies and intended visual differences are written down.

### 1. Replace ownership at the top; break downstream callers

Introduce the scene/frame/view contract, presentation preparation and feedback
boundary. Change the core game frame entry to publish/submit it. Make the old
world rendering entry points unavailable to core code, using deletion or a
compile-time migration barrier. Do not keep automatic fallbacks for unsupported
core objects/materials. Temporary incomplete support must fail conspicuously.

Gate: missing migration work appears as compiler/test failures, not old-path
draws. Replaying a frozen frame cannot advance RNG, animation or game state.

### 2. Migrate the entire scene domain and visibility

Move room and terrain assets, old/new model preparation, all instance poses,
transient effects and material translation into the new ownership. Reuse sound
mesh builders/LOD algorithms, not their legacy stateful execution scaffolding.
Replace camera-global traversal with view-local queries. Route model previews,
automap, mirrors, cockpit and small views through explicit views/layers. Centralize
asset invalidation and generation handling. Move gameplay observations to feedback.

Gate: every core scene family has a representation; one frozen frame can be
queried from two cameras and a light without changing it. Off-camera casters are
available. Near-plane geometry, destruction, submodels and reload are covered.

### 3. Replace shading and the frame executor

Implement canonical light upload, influence IDs, explicit materials, shared
geometry evaluation and declared frame scheduling. Port existing effect kernels
onto named resource products. Migrate 2D/presentation entry points and external
module translation where they touch frame ownership. Remove old world submission,
light transports, random-state replay, and bloom-named frame-boundary machinery.

Gate: the supported game works through the replacement with shadows disabled;
all regression scenes and required module tests pass, and no legacy core path
can be selected. No requirement to preserve pixels already identified as bugs,
but every intentional visual departure must be documented and inspected.

### 4. Implement shadows as an ordinary scene consumer

Use shadow maps: spotlight projection for a headlight-sized cone and cubemaps
for selected point lights. Use the canonical emitter, geometry/material opacity,
and independent light visibility query. Deterministically budget shadowed lights;
avoid unstable selection and expose exclusions in diagnostics. Preserve baked
illumination. Start with hard visibility, then small PCF; add PCSS only after
contact, slope, motion and coverage correctness are demonstrated.

A fixed offset-light/caster/receiver scene is the first verification fixture,
not the headlight. An actual headlight remains a required test, but camera-aligned
lighting cannot be promised dramatic visible silhouettes. Do not offset only the
shadow source to manufacture them. Transparent materials have explicit caster
and receiver policies; cutouts use the same opacity test in color and depth.
Terrain, rooms and animated core models use the same scene contract.

Gate: off-screen caster, camera-independent position, animation, portal/door,
cutout and headlight tests pass in motion and across sample configurations.
Neither old draw interception nor post-composite light subtraction exists.

### 5. Deletion audit and release qualification

Verify removal by symbol/dependency checks and runtime counters, not comments
calling code obsolete. Remove temporary migration switches and debug substitutes.
Run the whole regression corpus, performance/memory comparisons, resource-lifetime
stress and external module tests. Review actual captures and moving sequences.
Only then prepare a proposed Candidate deployment; preserve Prev Candidate.

Gate: one core architecture owns all supported rendering, no pending fallback
migration, no unexplained material regression, and measured performance fits the
agreed budgets. A build-only pass is not completion.

## Explicit retirement ledger

| Retire from the core renderer | Replacement |
| --- | --- |
| World draws controlled by rend_* setter sequences and retained/immediate eligibility fallbacks | Scene assets/instances/materials and explicit draw packets |
| Shared Rooms_visited / face-facing / global camera state as cross-view authority | Per-view visibility results |
| RenderObject opaque/transparent function replay and RNG restoration | Once-prepared visual state with independently classified packets |
| Per-draw uniform lights versus copied lightmap SSBO light definitions | Canonical light table plus IDs/influence lists |
| Dynamic lightmap edits/restoration as normal light transport | Explicit baked data plus dynamic light evaluation |
| Transform ownership hidden in motion tracking | Instance transform/pose snapshots; motion history as a consumer |
| Direct OpenGL execution/resource ownership in terrain/game extraction | Renderer-owned GPU assets and executor |
| CaptureBloomSource as implicit general frame boundary, dispersed post-present state machine | Named passes/resources and explicit presentation layers |
| CPU/GPU near-plane switching | One declared clipping/depth contract |
| Core use of the exported legacy draw adapter | ABI-only translation, checked by dependency tests |

Keep legitimate portal topology, terrain LOD, model loaders, artistic lighting
formulas, shader specializations, material blend distinctions and platform code.
Removal targets competing ownership, not every preexisting function or branch.

## Regression and acceptance corpus

- Main mine view: bright and dark rooms, headlight on/off, static specular and
  baked lighting. Include the preserved stalactite shadow test.
- Room/terrain transition, exterior room, portal/door opening and closing,
  mirrored views, fog entry/exit, near-camera clipping and ultra-wide view.
- Animated robot/player/door, deform/jitter/cloak, cutout surface, shield sphere,
  weapon trails, glows, particles and scorch decals; verify effects happen once.
- All supported ship cockpits and their monitor views, rear/guided views,
  cinematics, automap, model previews, HUD and modal F2 UI. No cockpit redesign.
- AI/rendered bookkeeping, automap discovery and effect eligibility with auxiliary
  views; adding shadow views must leave their results unchanged.
- MSAA off/on and supported settings; SSAA 1x/2x/4x; AO/bloom/motion settings;
  temporal stability under pause/F2, camera movement, cuts and view switches.
- Repeated Level 2 load and cross-level reload, resize/fullscreen/context changes,
  object deletion/ID reuse and light destruction. Preserve the room-cluster fix.
- Representative shipped/custom module drawing and readback; preserve API slots.
- Shadow fixtures: stationary light with moving camera; caster leaves view;
  moving/animated caster; receiver slope/contact; light crosses portal; light
  count exceeds budget; fade/range boundaries; opaque versus cutout policy.

Use existing automated capture options (`-capture-frame`, `-capture-dt`,
`-capture-output`, `-capture-headlight`, reload and motion/fire controls) after
verifying their invocation against the chosen executable and saves. Check logs
to confirm headlight activation, rather than assuming a save preserves it.

Capture final images plus depth, normals, light IDs/contributions and visibility
where applicable. Inspect well-lit final frames and moving sequences, not just
numeric image deltas or a dark composite that conceals a broken intermediate.
Golden images detect differences; human visual inspection decides whether an
intentional difference improves or damages the authored appearance.

## Remaining risks and decisions

The highest risks are extracting once-only effect/AI feedback correctly,
preserving clipping and transparent depth semantics, and covering module/rare
material behavior. Before stage 1, resolve exact view-feedback rules, module
callback/readback boundaries, supported build/platform matrix, and measured
performance budgets. These are bounded entry tasks, not reasons to keep a second
renderer indefinitely.

This is a substantial renderer replacement across game extraction, model code,
shaders and GL orchestration. No credible wall-clock estimate follows from this
source audit alone. Do not describe it as a quick cleanup or deliver shadows
while leaving the replacement unfinished.
