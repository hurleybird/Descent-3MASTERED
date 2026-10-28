# Renderer recovery checkpoint — 2026-09-22

## October 27 — resumed original task after Sol continuation

Active worktree remains renderer-rearchitecture, branch codex/renderer-rearchitecture.
Sol continuation01a164b5-04fe-78d0-b4f8-619a0f143d48 was idle/interrupted at82bd23a.
Finished its pending independent movie canvas inb0e87b2. Further checkpoints:
10fc2ff main-menu display canvas/target-owned raster mapping and obsolete copy API
removal;9e9f73c nested-modal display ownership;3dcb666 TelCom draw-local sampling.
Current automap work separates room-asset publication from full world extraction
and retains compiled meshes with exact invalidation; ledger contains tests and
bounded CPU timings12.339->0.742ms and5.198->0.543ms. World003/006 images exact.

Use docs/renderer-integration-ledger.md as the current integration inventory.
Private runtime C:/tmp/D3RendererVerification/integration-runtime-20261022.
No Candidate/Previous Candidate/desktop/shadow changes. Preserve lib/gitinfo.h.
An interactive test captured Jeff's mouse and was immediately closed: continue
background-only unless he authorizes an interactive test window. Last usage
check94%consumed, credits62044.8730600000 unchanged this turn; check again and
stop before included usage reaches zero. Ordinary no-probe qualification remains
closed. TelCom still changes screen mode and its full shell/map composition is
not yet separated; don't claim otherwise. Do not resume cancelled cockpit edits.

Automap lifecycle and settings scenarios now pass003/006: repeated real TelCom
entry around scene retirement and five SSAA/MSAA changes, with fresh geometry
audit and all eight producer caches cleared. Evidence in qual-map-lifecycle-
20261027 and qual-map-settings-20261027. No 3MASTERED process remained at the
latest process check. Baseline executables still exist at out/baseline-pre-rewrite.exe
and out/observation-baseline.exe; their provenance still needs checking before use.

Further Oct27:94d0281 resolves independently supersampled images to declared
logical dimensions before publication/readback/dependent sampling. Image1/2/4x,
retirement and quality-transition captures pass; ordinary003/006 remain exact.
Subsequent view-pass scratch reuse passes contracts and exact003/007 captures.
Current baseline hash was verified A6286E9315A60F3C93B3A764C93A808B8FA385924ADDFACE1B573433374F3DA4:
this is the reconstructed bfb59647 IPO baseline, NOT the deleted original binary.
ABBA integrated clean720p1x/MSAA0 corpus completed; see ledger for results.
e5057ec fixes producer resource-history cache thrashing and removes redundant
publication dependency sorting. All eight exact-mode images unchanged; scene
retirement passes. Pinned uncapped field-mode ABBA1500frames completed Oct28:
CPU000/005/007 still slower than reconstructed legacy baseline.005 GPU wins;
000/007 GPU lose. Ledger records exact medians and evidence paths. No universal
performance or visual-parity claim: darker barrels in007 remain unresolved.
Same private runtime swapped serially and restored in finally. No interactive
windows. TelCom isolation and ordinary no-probe release qualification unfinished.

Latest user asks yield at the next natural checkpoint with roughly5% usage left.
Placed-vertex lighting experiment was tested and removed: no reliable benefit.
No experimental renderer source remains. Tracked corpus now supports Uncapped
and RenderThreadCpu, records them, and cleans up only its owned child on failure.
Pinned005/007 screenshots exact; forced timeout leaves no game. No new Sol
handoff requested. Continue here when asked; do not resume stale subagents.

## October 22 — display-resolution soft particles qualified for testing

User selected scaling idea 2 only: display-resolution soft particles. No other
scaling ideas, shadows, cockpit or unrelated regression work. Source parent
0cb2f5c; active worktree remains renderer-rearchitecture. Unrelated untracked
lib/gitinfo.h preserved. Candidate, Previous Candidate, desktop, main checkout
and older delivered previews untouched.

Enabled the repaired particle layer for supersampled main-world views. Native
resolution (even with MSAA), image views, auxiliary coverage and ineligible
materials keep their ordinary schedule. -scene-native-particles selects the
full-raster reference. Old -scene-display-particles is no longer needed.
Scene shader variants now follow the actual pass interface, not a global flag:
views without a SoftVolumes pass compile the ordinary fragment shader.

The existing opt-in prototype initially LOST at 4K: whole frame about54ms vs48ms,
despite saving particle-pass time. An additional4109x2312 RGBA16F image (~76MB)
coincided with much slower later resolves/reductions. WDDM counters also showed
more shared memory. Memory pressure/driver residency is an inference, not a
proven driver diagnosis. Reusing the existing RGBA16F SSAA-reduction destination
removed the extra allocation and the downstream slowdown. Its color is idle
during this main-world phase: clear/render ordered particles, composite them
onto the distinct scene target, then normal reduction overwrites the scratch.
Opaque depth remains separate. No precision reduction, new image allocation,
frame-to-frame storage churn, atomics or readback. Sampling policy uses a scoped
linear/clamp sampler, not mutations of the borrowed image's texture parameters.

Preserved depth-edge full-resolution repair, conservative bounds, sorted order,
multiplicative exclusion and late-depth barriers. Added alpha/additive transition
barriers: normalized destination saturation must occur before subsequent alpha
blends, rather than allowing an HDR accumulator to carry that excess forward.
Tests cover adjacent same-blend grouping plus those barriers. AO debug output
also skips the new late-world pass, just as it skips ordinary transparency.

Final tested binary out/particle-scaling-candidate.exe and owned scene-view-test
runtime SHA25665A8C3E66964920EDAD5FA19D4E4102F40FE61A6BDE3C88BF524B41A93C9686C.
Built before this checkpoint commit (pre-commit version stamp). Tests3/3 pass.
Launch Particle Scaling.cmd and Launch Native Particles Reference.cmd are in
C:/tmp/D3RendererVerification/scene-view-test. Both start saved007; no timer,
forced graphics settings or desktop changes. Use SSAA>1 to exercise the layer.

Whole-frame intervals, never CPU+GPU sums, fixed simulation/seed, headlight on,
render thread CPU2, frame cap off, 2xSSAA/4xMSAA/AO/bloom:
- Same-binary 4K toggle ABBA,300frames, mean100..249: native48.690/48.090ms;
  display46.457/46.403ms. Means48.390->46.430ms (~4.1%). These isolate the path.
- Dense0071080p frozen0cb2f5c vs final,1000frames, mean400..899, ABBA:
  old8.743/8.842ms; new8.215/8.114ms (~7.1% faster). GPU8.113->7.514ms.
- Sparse0021080p two reversed pairs: old7.009/6.969ms; new7.031/7.081ms.
  Small cost, not a win (~0.067ms). Do not claim every scene is faster.
- Native720p path unchanged;5000-frame reversed comparison warmed old1.234ms
  vs new1.204ms, GPU~.509ms both, screenshots EXACT. Earlier CPU timings drifted
  upward/downward across launches, so no native CPU speedup claim.
Earlier frozen-executable 4K ABBA suggested12%; DO NOT present that as the
isolated gain. Same-binary toggle above gives the more conservative result.

Quality: final moving002/007 frames996..1000 (heading begins990), all max3/255;
mean RGB errors .014..044 and .125..137 respectively. Inspected platform/smoke
and steam/wall images. 4K static max3,720p4xSSAA max3, offset/offscreen fixtures
max3, SSAA2 single-sample max2, exact fog mode max2, AO debug EXACT. No logged
scene GL errors in qualification runs. Not a guarantee for every effect/scene.

Raw evidence under C:/tmp/D3RendererVerification: particle-final-perf-*,
particle-toggle-4k-*, particle-final-qa-*, particle-final-exact-fog-*,
particle-final-motion-*, particle-native-confirm-*, particle-sparse-confirm-*.
IGNORE particle-reuse-4k-reuse1: its copy raced the linker; it actually ran the
old opt-in binary while compilation was finishing. It was discarded, never
used as new-path timing/quality evidence. Later profiles record verified hashes.
All owned capture processes exited; runtime restored to the final candidate.

## October 22 — focused slot007 GPU regression repaired and verified

User requests fixing only the ~45% native GPU regression, keeping the modern
architecture; no other regression work, no shadows. All current changes are
limited to optical fog preparation/execution and tests. Baseline committed source
was73d2cc2; unrelated lib/gitinfo.h remains untracked. Frozen before-fix binary:
out/slot007-gpu-control.exe (B50792A4F9506E6DA43D7A436CD8C1041A3A1B21DB72E5AE48BD12613D8DA036).
Current qualified-candidate binary: out/slot007-gpu-fixed.exe
(4C4382BE54BFD505146CEECCE1325D3D2705D67B51095B5F90C5FBBD9CFF4315);
build and owned scene-view-test runtime match it. No Candidate/Previous/desktop
changes. This is the focused repair checkpoint, not whole-renderer acceptance.

Cause isolated: original field build .312ms; diagnostic no-traversal .083ms.
Rejected and removed:32-ray workgroups, parallel-depth workers, angular node
bounds and CPU per-tile lists (the latter hurt CPU despite a GPU win). Kept:
view-owned homogeneous boundary equations, GPU-built conservative tile bitsets,
stable source crossing order, unchanged64 depth slices/resolution/density, and
scalar optical-depth sampling with common color reconstructed once. GPU mask
buffer is retained scratch, no readback/atomics/fixed-capacity lists. Portable
GL4.3 compute/SSBO only. Before/after native3000-frame screenshot EXACT so far.
All3 contract suites pass, including6000 random prepared-equation comparisons,
shared diagonals, reflected/cropped/eye-plane views, no geometry/mirror-plane
clipping of optical prefixes, retained world-node payload, GPU tile rejection,
held output independence, empty fields and projection/address validation.

Latest native repeats,3000frames,mean500..2499,720p SSAA1/MSAA0/noAO/noBloom,
CPU2,uncapped. Order fixed1/control1/legacy1/legacy2/control2/fixed2:
whole-frame1.177615/1.326463/1.625893/1.678123/1.358419/1.248145ms;
GPU .509415/.746373/.533122/.543811/.745205/.509788ms.
Raw slot007-final-native-{labels}-20261022 under C:/tmp/D3RendererVerification.
Field build before .312ms -> .129ms; scalar sampling then saves another~.045ms.
Legacy is reconstructed frozen pre-rewrite A6286E...; whole-renderer strict
pixel equivalence with it remains unclaimed. Do not add CPU and GPU timings.

Final binary's additional native3000frame run:GPU.5122785, whole1.267462ms
(slot007-final-state-native20261022). GPU regression is gone in the tested scene;
new .509-.512 versus repeated legacy .533-.544. No universal GPU win claimed.

Matched4K output SSAA2/MSAA4 AO+bloom,300frames means100..249:
before-fix whole59.082193/59.789393ms; fixed56.306920/55.838100ms.
Raw slot007-final-4k-{fixed1,control1,control2,fixed2}-20261022. GPU similarly
58.569/59.108 ->55.723/55.369ms. Earlier pre-rewrite4K runs90.98/90.18ms versus
before-fix55.60/53.66ms (compare4kcombined-*); do not mix different timing rounds.
NativeCPU remains variable, with no added per-tile CPU construction overhead.

Qualification: eight90frame saves slot007-qa-{control,fixed}-{000..007}-20261022:
seven pixel-exact,002 max1/255 on8pixels. 4K300frame comparison max1/255 on4pixels.
Moving1080pSSAA2/MSAA4/AO/bloom steam frames90..94 and continuous roof150..153
are exact or max1/255 on at most9pixels. Vector-field and offset/offscreen-image
producer fixtures exact. All independent512sample optical/occupancy audit error
reports match the before-fix binary exactly, including all roof/moving frames and
three multiview producers. GL audits have no logged errors. Roof and moving steam
screenshots visually inspected headlight-on. No quality settings were reduced.
No remaining benchmark processes. Final scope: only this fog/GPU regression;
other migration/gameplay gates and unrelated regressions remain untouched.
Literal4x SSAA + saved107% AO overscan fails framebuffer creation in BOTH
renderers; out-of-scope and left untouched. Do not call2x/4x the literal max.
Included usage last23% consumed, credits unchanged62044.8730600000.

## October 22 continuation — flat owned feedback aggregation

SceneFeedbackTransaction now owns its publication and deduplicates through dense
frame ordinals, not a node per room/object. View replay IDs use SceneLookup;
exact replay payloads live in flat packed arrays, not per-view copied vectors.
Domain separation, first object order, stale identity rejection and whole-report
validation before acceptance remain unchanged. All capacity growth precedes
acceptance; geometric growth avoids linear reallocation on cold multi-view runs.
Transaction storage returns to a publisher-thread pool only after destruction.

VisibilityReport recycles released ID-list capacity. Copy is independent; move
assignment swaps retiring capacity into the temporary instead of freeing the
renderer's empty report buffer when eligibility replaces it. No asset/source
pointers are cached. Foreign-thread releases free normally and never touch the
publisher's pool. Tests cover copy/move capacity reuse, foreign release, owned
publication lifetime, retired handle/index reuse and all prior replay/domain
checks. All3 contract suites pass. AI/automap commits remain disabled.

feedback-flat-abba002-{new1,old1,old2,new2}: CPU1.260/1.485/1.215/1.363ms,
GPU.694/.690/.695/.690. Control out/feedback-flat-control.exe is the verified
pre-change binary from prepared-images-perf006 (SHA256
6BB8BFF972DBCF9713F547CE0BA0F0A76FD63B48D31CA18C6819A53149840154).
The timing spread does not support a claimed speedup. Images old1/new1 EXACT.
feedback-flat-outdoor moving1080pSSAA2MSAA4AO/bloom/main+rear image EXACT against
rain-camera-moving-hq, with no logged GL errors and rain pending=0.
Latest build/runtime current. Candidate/Previous/desktop unchanged; unrelated
untracked lib/gitinfo.h preserved. Usage17%, credits unchanged62044.8730600000.
Remaining: actual frame feedback commit/order qualification (not just pure
reports), observer/guided/cinematic/module call-site audit, other effect families
including snow, final visual/performance acceptance and legacy code removal.
No shadows until renderer qualification is complete.

## October 22 continuation — actual prepared image producers

prepareViewImages validates/orders the complete dependency set, then owns each
producer's PreparedView plus image ID/scale/MSAA. GPU image execution accepts
only PreparedViewImage and never queries the camera/visibility again. The normal
request-list convenience entry compiles this same plan. Mirror inspection now
consumes the actual executed producer; the second diagnostic preparation is gone.
No production AI/automap commits enabled. Prepared plans retain their exact
publication and camera across source mutation/later frames, including copies.

Image-plan vector capacity is recycled through a publisher-thread pool; released
entries drop their frame references. Existing image dependency ordering now uses
retained flat sorted producers/edges and a min-heap instead of per-node unordered
maps/sets/nested vectors. Duplicate dependencies are deduplicated; source-order
ties preserved, absent/duplicate/cyclic producers still reject. Existing graph
tests and new prepared ownership/copy/empty/invalid-plan tests pass (all3 suites).

prepared-images-mirror006 and prepared-images-heading006 images EXACT to their
observe-mirror equivalents; mirror still reports rooms54/56 and objects2048/2088
in static capture. Actual image inspected. prepared-images-chain002 verifies
reversed source/copy declarations and visible picture-in-picture, GL audit clean.
Its capture-save-output creates only owned imageplan-saveprobe20261022 seed;
prepared-images-reload002 successfully loads it. Native mirror3000frames CPU.401,
GPU.459ms; no measured speedup claimed from the dependency-storage change.

Remaining upstream feedback work: transaction storage/report copies still use
per-frame node/vector allocations; fix the owned aggregation path before enabling
AI/automap. Save-thumbnail planner still re-prepares its consumer as an image;
rare independent capture path, not a new production mirror double-query. Whole
renderer validation/gates remain incomplete; shadows and Candidates untouched.

## October 22 continuation — captured main camera and owned rain foreground

CaptureGameMainView now selects the physical camera/rear role/overscan/logical
crop once before publication. Main world execution and camera-bound effects
consume that value. Guided selection no longer temporarily swaps Viewer_object.
Independent thumbnails omit first-person effects rather than borrow live main
camera state. Real guided gameplay still needs qualification; camera-editing
diagnostic probes are not proof of physical first-person overlay behavior.

cameraOverlayView derives an isolated display-resolution collection from the
same captured optics/attachment, preserving sample coordinates through overscan
cropping. No portal/AI/automap/light ownership, mirror or auxiliary source allowed.
Tests cover crop/sample coordinates, perturbation exactly once, independent held
copies and invalid source/identity. Rain droplets publish into this collection
between HUD and cockpit; world puddles publish ordinary depth-tested planar
quads. Existing source sizes/fades/textures retained. No legacy effect draw call.
Real rear-view capture flag -capture-main-rear verifies windshield=0, versus7
in the forward save. Native and moving1080pSSAA2MSAA4AO/bloom captures inspected;
the supplied rain scene now reports pending=0. Snow/other missing families in
other scenes remain unfinished. Actual rear monitor still works.

ForegroundLayer now recycles released scene/display view-list capacity through
a bounded publisher-thread pool. Held copies do not share live values; foreign
thread destruction cannot mutate the pool. Existing HUD/cockpit builders also
reuse this storage. Tests verify capacity reuse/copy isolation/foreign release.
All3 contract suites pass. Removed old cockpit-snapshot and close-screen queue
replay from GameDrawPostPresentFrame; that adapter now only owns unmigrated
console UI. No g3 camera reconstruction for obsolete cockpit replay remains in
that call path. Unchanged images after pooling/replay removal: rain-foreground-
final vs rain-camera-native EXACT; camera-foreground-final007 vs camera-source-
rear007 EXACT; camera-foreground-final002 vs rain-strokes-default002-field EXACT.
007 vs earlier observe-rear-hq007 has2pixel max3delta, not blanket legacy parity.

With all rain families restored, repeated720p outdoor runs CPU1.304/1.362ms,
GPU.331/.326ms (outdoor-rain-complete-new{1,2}); earlier legacy1.941/1.932CPU,
.805/.785GPU. Noninterleaved follow-up, remaining whole-renderer differences
still limit equivalence claims. No gameplay gates cleared, no shadows, no
Candidate/Previous/desktop changes. Usage14%, credits unchanged62044.8730600000.
Next upstream feedback boundary: actual image producers should own PreparedView
results for game inspection; mirror diagnostics currently prepare a second query.
Do not enable production AI/automap commits by copying that diagnostic pattern.

## October 22 continuation — generic pixel-width strokes restore rain

PreparedMesh now publishes a world centerline plus logical-pixel stroke width.
Canonical immutable fading-stroke topology is shared for the scene lifetime;
no per-line topology/vertex allocation. View-local transverse direction clips at
the near plane before perspective division; behind-eye/end-on lines collapse.
Existing vertex raster offsets and spare draw-record components implement the
extrusion: no added attributes, varyings, UBO bytes or render-state fallback.
Publication requires unlit, translucent, non-depth-writing Surface-domain strokes.
Tests cover aspect, independent cameras, near crossing, degeneracy, ownership,
canonical asset reuse, endpoint opacity and unsupported depth writes. All3 suites
pass. Rain and weapon FADING_LINE inputs now publish these ordinary world meshes.
Other pending weather remains untouched: windshield droplets, puddles and snow.
Unlit pixel markers no longer disable world emitter-frustum culling; they never
consume that lighting. No new per-frame renderer allocation was introduced.

rain-strokes-user-native and rain-strokes-user-hq inspected: actual rain streaks
present, main/rear cameras, HQ1080p SSAA2MSAA4 AO/bloom, GL audit without errors.
rain-strokes-default002-field image EXACT to uniform-abba002-new2, GPU0.700ms;
no demonstrated ordinary-scene speed gain. rain-strokes-default002 (without-field)
used different fog arguments and is NOT a valid comparative performance run.

New real outdoor benchmark, ABBA3000frames, CPU2,720pSSAA1MSAA0/noAO/noBloom,
means500..2499: new1 CPU1.243/GPU.340ms; old1=1.941/.805;
old2=1.932/.785; new2=1.386/.334. Old frozen pre-rewrite comparator.
These are NOT full visual/feature equivalence claims: remaining weather and
known migrated effect differences still apply. Source is cleaner/measurement
improved, not permission to clear gameplay gates. Candidate/Previous untouched.

## October 22 continuation — real outdoor fixture and imported UV repair

User replaced first live save slot. Original benchmark saveg000 remains intact in
seeds. New immutable copy seeds/savegame/outdoor-user20261022 SHA256
F2B3C6BE67C02467BC5D915A438B7A1D2F9753A8FE0865F7AAFE0142BABDDB9C.
True exterior camera, terrain/building/sky, actual rear monitor verified visually.
observe-user-outdoor-{old,new}: room membership8 and ordered objects11 match.
observe-user-outdoor-heading-{old,new}: at90frames main0rooms/0objects and
rear4rooms/5ordered objects match. HQ1080p SSAA2 MSAA4 AO/bloom capture passes
and inspected, no logged GL errors. This does NOT enable gameplay commits.
Initial comparison falsely reported9extra objects because its regex excluded
negative room IDs; corrected signed parsing proves exact object agreement.

Historical search found no true outdoor save: two unique archived copies were
indoor. Old D-Day benchmark starts in a sky-open mine. Reproduced anyway:
observe-dday-outdoor-{old,new} room membership10/ordered objects4 match.
Archive user-crash-save-20260820/saveg000 uncovered a real migration crash:
MUDSickleFull.OOF imports4nonfinite UV components. Model compiler now normalizes
only nonfinite UV components to0, preserves topology/finite coordinates/source,
logs repairs and applies the same canonicalization in revision matching. Scene
validation remains strict. Intended invalid coordinates cannot be recovered.
Asset from runtime d3.hog SHA256374be39eaaffc487bcbc5a068df88dfe6e0e49e3698c91c76aa20ab8c39e2362.
observe-archive-crash-fixed completes and inspected; legacy comparison matches
18room membership and37objects in order. Tests cover NaN/infinity, source
immutability, repaired publication, invalid direct publication, revision reuse.
All3 contract targets pass. Temporary per-face probes removed.

Optional particle uniforms now skip location=-1 on ordinary scene execution.
uniform-abba002 comparisons pixel-exact; HQ opt-in uniform-guard-particles007
also pixel-exact to particles007-bounded. Timings drifted too much (new1CPU1.34,
old1=1.51,old2=1.09,new2=1.04ms) to claim a performance gain/regression.
Default007 heading has1pixel max3delta; no blanket exact-image claim for that run.

New exterior fixture exposes missing rain streaks: FADING_LINE is explicitly
pending in PrepareGameEffects; no world rain line publication yet. Next focused
work can migrate generic stroke primitives then rain, rather than restore legacy
draw-state fallback. No shadows, Candidate/Previous/desktop changes. Unrelated
untracked lib/gitinfo.h preserved. Credits unchanged62044.8730600000; usage12%.

## October 22 continuation — mirror inspection contract

Read-only prepared-view observation inspection now accepts terminal mirror
producers with an explicit physical clip plane and aperture. Objects wholly
behind that plane or outside the aperture are excluded, including in the spatial
root room. Mirror automap feedback is explicitly rejected. Main/auxiliary policy
unchanged. Gameplay commits remain disabled. Capture diagnostics inspect mirror
requests under existing -scene-observation-probe only; the extra diagnostic query
is NOT part of the normal production path. Future commit integration must consume
the actual prepared image producer rather than add a second production query.

Inspection scratch now retains capacity and uses source ordinal tie-breaks with
std::sort, avoiding stable_sort's scratch allocation while preserving equal-depth
AI order. Tests cover this, mirror clip/aperture rejection and domain ownership;
all3 contract suites pass. observe-mirror006 (static2frames) matches the previous
image EXACTLY. Actual mirror shows player2048 and object2088; inspection reports
both (image inspected). observe-mirror-heading006 includes real rear view and GL
audit: terminal mirror reports remain stable; no logged GL errors.

All8 benchmark saves were checked for exterior coverage: they are indoors.
Asked user asynchronously for an existing outdoor save; exterior qualification
is still pending. Continue independent work while awaiting that answer.

## October 22 continuation — real cockpit auxiliary view crash fixed

FOLLOW-UP:007 rear difference is explained by another legacy global-camera leak.
IsPointVisible derives sphere planes from global Render_FOV=90 even in the rear
window using zoom=.726. Actual rear projection scales=1.83655; object6330 radius
7.00691 is8.32669 outside its bottom plane and correctly rejected. Evidence
observe-aux6330-new and observe-aux-fov-old. Added an independent-view zoom test.
Temporary probes removed again. No modern observation policy change was needed.
out/observation-baseline.exe retains generic view/room/object diagnostics; the
baseline-diagnostic build output now has only the temporary FOV trace instead.

Real rear-window fixture exposed a crash: SmallViews queried legacy gauge->pts,
which migrated cockpit rendering never populates. Invalid monitor viewport then
failed validateView. Replaced that upstream dependency with immutable ViewRegion
metadata, published from the same transformed monitor quad and camera as gauges.
SmallViews consumes those regions. Removed GetCockpitWindowCoords and its legacy
g3 render-state dependency entirely. Region storage is recycled with frame data;
freeze checks positive extents and unique identities. No cockpit geometry changed.
Capture-only -capture-rear-smallview opens the real left rear monitor. Observation
headers now include frame/view/kind even when the report has no room/object lines.

All3 contract targets pass (including held region ownership/invalid metadata).
observe-rear-fixed002/007 complete at720p; actual rear monitor inspected in007.
observe-rear-hq007 also completes with heading,1080pSSAA2MSAA4AO/bloom and GL audit;
image inspected, no logged GL errors. region-default007 image is EXACT against
observe-heading007-new without a rear window. Candidate/Previous untouched.
002 main+rear room sets and ordered objects match legacy.007 main matches;
rear room sets match and the object6330 difference is explained above. Do not
enable AI/automap commits yet; exterior and mirror policy still need qualification.

Durable capture-only legacy observation comparator now out/observation-baseline.exe:
records view begin/end, main/rear, rooms/objects and supports the rear-window
fixture. Source instrumentation reverted; baseline worktree clean. It is NOT the
timing comparator. Evidence observe-rear{002,007}-old and fixed equivalents.
Next: resolve remaining auxiliary boundary difference, then exterior/mirror policy.

## October 22 continuation — prepared observation apertures

Active continuation is already the requested Sol6.1/high fork. Do not fork again.
Moving heading captures90frames in002/003/006/007 exposed two legacy false
results; the new policy was not changed to reproduce them:
* 007 object4168: radius11.69999. Legacy cached-camera top distance12.23606
  rejects it. Fresh g3 camera gives11.52247; new explicit camera gives11.52087.
  Cached eye is one camera update behind. Evidence observe-sphere-fresh007-old
  and observe-diagnostic007-new. All other ordered object lists match.
* 002 room184: only facing entry from188 (face271) is outside the frustum.
  Independently clipping logged homogeneous vertices gives an empty polygon.
  Legacy portal code emits rectangle[0,-992.38,3698.70,720] against1280x720,
  admitting it. Evidence observe-entry002-new and observe-combined002-old.
  Other room sets match. New/old camera images inspected and agree geometrically.

ViewVisibility now owns the portal windows/exterior aperture already computed
for geometry. The query fills recycled view storage; observation inspection takes
a PreparedView and requested diagnostic domains, reusing its camera/apertures and
the frame's room identity index. No second portal walk or rebuilt room map.
Original standalone inspection stays as an independent reference API. Tests cover
prepared/reference parity, held main/auxiliary views, and invalid domain masks.
All3 contract targets pass. Four moving captures observe-shared-heading{002,003,
006,007} have identical reports AND exact images against observe-heading*-new.
AI/automap remain uncommitted: exterior, real auxiliary and mirror policies still
need qualification. Temporary numeric probes removed; baseline source is clean.
Baseline-diagnostic executable currently contains temporary numeric probes only;
rebuild generic observation instrumentation before using it for further audits.
Frozen pre-rewrite timing comparator remains out/baseline-pre-rewrite.exe.
Included usage last5% consumed, credits unchanged62044.8730600000 in this fork.

## October 22 checkpoint — observation comparison

User reset usage, but has a smaller allowance. Requested continuation only to
the next natural checkpoint, then a fork set to gpt-6.1-sol / high.
Completed the previously launched four-save observation comparison. Evidence:
C:/tmp/D3RendererVerification/observations{002,003,006,007}-{old,new}.
Two capture frames, fixed delta0.000001 to minimize simulation divergence.
Compared last legacy frame's non-mirror reports against new main view1 report.
Room sets match:002=4,003=3,006=2,007=3. Object lists, including order, match
exactly:002=16,003=8,006=0,007=4.002 room order differs, not room membership.
This is STATIC MAIN-VIEW evidence only; it does not qualify AI behavior,
moving/auxiliary views, exterior cases, or mirror observations. No automap/AI
commit was enabled. The new diagnostic now logs room/object identities under
the existing -scene-observation-probe flag, only on capture frames.
Baseline source instrumentation was reverted after building; baseline worktree
is clean. Its out/build/baseline-diagnostic executable is observation-instrumented,
not the frozen timing comparator. Unrelated lib/gitinfo.h remains untracked.
Next: qualify observation policy more broadly before enabling gameplay feedback;
reuse prepared visibility upstream rather than introducing a second production
portal walk. Keep particle optimization opt-in. Latest runtime is current renderer
with observation logging; Candidate/Previous/delivered previews untouched.

## Current recovery continuation — 2026-10-21 (supersedes hypotheses below)

Opt-in display-resolution smoke prototype: -scene-display-particles. NOT default
and NOT globally qualified. Extraction explicitly tags non-planar FT_SMOKE with
soft depth as RasterDomain::SoftVolume. Planner preserves sorted transparency,
groups only adjacent eligible draws, and stops using the layer after any late
depth writer. Alpha/additive use premultiplied color+coverage; multiply excluded.
Display-resolution RGBA16F layer uses the opaque-depth snapshot, then composites
onto the SSAA/MSAA target. Default schedule retains its original fast path.
Tests cover default behavior, mixed alpha/additive grouping, glass boundaries,
and late depth barriers. All 3 test suites pass.

FOLLOW-UP: depth-discontinuity repair is now the opt-in path's default. A shared
shader predicate excludes edge pixels from the layer composite and renders the
same ordered particle run at full resolution there. No double blending. Composite
is scissored to projected particle bounds plus filter padding; near-plane crossings
conservatively use the full raster. Scope restores scissor state. Diagnostic
-scene-particle-bilinear-reference bypasses repair for comparison.
particles002-bounded GPU7.070ms (full7.06), max error2/255 (was27 with bilinear).
particles007-bounded GPU8.135ms (full8.83); max3/255. Bounded and unbounded repair
images match exactly. Moving002/007 frames996/999 max3/255, inspected002 platform.
Single-sample SSAA2 plus auxiliary-offset/offscreen-image fixtures max3/255;
no logged GL errors (particles007-single-{full,bounded}). Cached R8 edge-mask
experiment was pixel-identical but slower8.18 vs8.01ms in007 and was REMOVED.
Do not report the original1.8ms saving as the quality-preserving result.
Default-path particle shader code is now compiled out unless the opt-in flag is
present. particles007-off-{control,final,specialized} images match exactly; small
GPU timing differences .75..78ms are not a demonstrated optimization. Latest
lossless cleanup saved another336,399,510 bytes across97 captures, pixels checked.

Evidence particles007-{full,display}{,-repeat}, 1080p SSAA2 MSAA4 AO/bloom,
1000 frames / mean400..899: repeat CPU1.58 vs1.59ms; GPU8.83 vs7.01ms;
whole frame9.46 vs7.63ms. Static frame1000 max channel difference3/255.
Corrected moving captures particles007-edge-{full,display}, heading starts990,
frames996..999: max3/255, mean RGB error .125..135. Inspected steam/wall edges.
Earlier particles007-motion-* turned away from steam: NOT quality evidence.
New capture-only -capture-heading-from supports delayed motion tests.

IMPORTANT: particles002-{full,display} only saves7.06->6.91ms GPU and exposes
localized max27/255 difference at smoke/platform intersection (x1528..1631,
y766..796). Bilinear upsampling is NOT fully qualified; investigate edge-aware
reconstruction or full-resolution edge fallback before enabling. Delivered
previews, Candidate and Previous Candidate remain untouched. Particle-control
binary out/particle-control.exe preserves pre-prototype03c1adc implementation.

Opaque-depth MSAA snapshot now borrows the framebuffer's separate resolved
texture for the duration of LateWorld, avoiding the second full-resolution copy.
Single-sample targets still copy (sampling attached draw depth would be feedback).
Qualified exact images at1080pSSAA2MSAA4AO/bloom in002/007; offset auxiliary view
plus offscreen view-image producer at1280SSAA2MSAA4; and single-sample fallback.
Flags -scene-depth-copy-reference and -scene-offset-view-probe are capture-only
qualification aids. Evidence depthcopy{002,007}-{control,new}, depth-offset-*,
depth-single-check. GPU0027.3645->7.0039;0079.1359->8.8382.002new had GL audit
enabled: do NOT attribute its CPU change to the optimization. Snapshot stage
002.9308->.57364ms. Latest runtime/build contain this optimization.

REFRESHED PRE-REWRITE720p3000frame corpus (mean500..2499), steam restored:
save oldCPU newCPU oldGPU newGPU
0001.9551 .9812 .7160 .5010
0012.2551 .76341.08081.0767
0023.12381.40821.6075 .6890
0032.17941.12371.0201 .3868
004 .7159 .2897 .4248 .1626
005 .8432 .6668 .3312 .1351
006 .8074 .3562 .4380 .4555
0071.0482 .8990 .5184 .7500
All CPU baselines win14..66%; GPU is NOT universally faster. Capture folders
restored-{000..007}-{old,new}; exact pixel equivalence with legacy not claimed
(headlight/fog semantics and gameplay feedback differ). No build during timing.
007 highquality LateWorld costs2.3152ms: soft-particle full-SSAA/MSAA fill is a
remaining target. Offscreen low-frequency particle rendering is only being
considered, not implemented. Reference NVIDIA GPU Gems3 chapter23 highlights
occlusion halos/undersampling and mixed-resolution edge recovery requirements.

Fog traversal now packs view-local bounds and threaded skips into two vec4s,
removing shader source-node indirection. Uses a second RGBA32F texture view of
the existing influence buffer (unit13), not another allocation/upload. Source
asset retains triangles; no sampling/geometry/math change. Tests3/3 pass.
Image equality:007,002; roofprobe with continuous183<-184 fog and movingheading
frames90..93 are byte-identical decoded PNGs against feedback-control.exe.
6000frame ABBA save002 (means1000..4999): new GPU.689143/.693006 vs control
.730067/.728199; CPU1.444354/1.395941 vs1.408484/1.392489; cycles6.697M/6.472M
vs6.534M/6.465M. Modest GPU win with small CPU variation, NOT a universal speedup.
HQ1080pSSAA2MSAA4AObloom save002: GPU7.303062 vs7.315384, CPU1.981688 vs2.039972.
Steam-restored HQ cost remains high. Runtime currently feedback-control after
comparison; build out/build/renderer is newest compact-node implementation.
out/feedback-control.exe is003899e semantics, includes restored steam and shared
identity validation; older empty-view-candidate lacks that fix. No live game.
Included usage last80% consumed; credits unchanged.

Lossless cleanup reclaimed19,750,504,080 bytes by recompressing2843 generated
capture PNGs under C:/tmp/D3RendererVerification. Every replacement was decoded
and byte-compared with original decompressed PNG data. No screenshot/frame lost;
preview/runtime/seed directories excluded. Helper out/recompress-captures.cjs.
Headlight audit confirms barrel owners4324/4325 are outside canonical radius:
canonical center attenuation0; original-radius object formula .265122/.219930.
Evidence recover007-light-audit/capture.log and -scene-model-light-audit flag.
This confirms light-response split rather than fog. Keep unified response for now.

Restored effect eligibility for normal main/auxiliary views from their already
prepared portal-room lists. GameLoop commits room/terrain timestamps after views;
AI and automap remain uncommitted. Mirrors/diagnostic views remain excluded.
Removed feedback's per-frame duplicate whole-level identity hash tables; validation
uses Presentation's existing room/object indices. All three contract tests pass.
Frame1000 save007 now retains steam (368 world meshes versus former328).

Barrel diagnosis: matching frame90 captures with `-capture-no-fog` in BOTH renderers
still differ with headlight ON. With headlight OFF the barrels visually match.
Old object-volume headlight uses original radius/linear falloff; modern canonical
light uses room-headlight radius*.67, falloff1.5, strength1.5. This is a candidate
explanation, not yet a complete proof excluding influence-culling differences.
Do not restore object-specific brightness or blame fog without further evidence.
Baseline diagnostic source change was reverted after building the no-fog binary.

Evidence C:/tmp/D3RendererVerification/recover007-*:
old/new-clear90; old/new-clear-unlit; new-fog90; steam1000.
Corrected sustained720p/3000frames/CPU2 means500..2499:
perf-old CPU1.485728 GPU.567498; shared-index CPU.93 GPU.78 (rounded).
Before identity-index fix perf-new CPU1.717993 GPU.782932.
direct-fog CPU.94 GPU1.57: keep field fog, direct integration is slower here.
Earlier particle-starved sustained wins are NOT qualified comparisons.
The runtime currently holds the corrected modern build. Candidate/PreviousCandidate
untouched. Builds recreated in out/build/{renderer,scene-tests,baseline-diagnostic}.
Preserved out/empty-view-candidate.exe is the pre-fix comparator, not latest.
Credits unchanged62451.4387600000; included usage last77% consumed.

## Active 2026-10-21 flare regression investigation

## Compact recovery — latest state, 2026-10-21

RECOVERY CLEANUP COMPLETED: removed3,341,364,761 bytes across2653 generated files
using Node with exact-root/realpath/link/tracked-file validation, NOT PowerShell.
Deleted out/build, out/perf-checkpoints, superseded root out/*.exe/*.obj/*.log.
Preserved source, all benchmark outputs outside worktree, scripts, small evidence,
out/empty-view-candidate.exe (current8-lane ab6eb0f semantics),
out/baseline-pre-rewrite.exe (A6286E... verified copy before build deletion),
and out/sample-render-thread.exe. Build caches/PDBs are gone; reconfigure before
building. Superseded binaries/logs are not recoverable directly; source revisions
remain in git. All source pushed through2d9d163 before cleanup. No active game.
Use THIS paragraph over older build paths/retained-executable claims below.

User requests push latest work and recover this SAME task after Bad Request.
No handoff, no deletion (especially no PowerShell deletion). Continue renderer
work after recovery, not shadows or cancelled cockpit geometry. Stop before
credits: last checked65% used, credits62451.4387600000 unchanged.

Active worktree: C:/Users/Jeff/.codex/worktrees/renderer-rearchitecture/DESCENT-3MASTERED
Branch codex/renderer-rearchitecture, latest implementation ab6eb0f.
Only unrelated untracked lib/gitinfo.h; do not stage. No game/build running.
Main/Candidate/PreviousCandidate/desktop/delivered preview untouched.
Completed: aee6973 no-medium fog exit;34773af mirror aperture; eef3393 packed
fog nodes/capture-only field timestamps;ab6eb0f sparse image-scratch reset.
Build/tests/image evidence and measurements are summarized immediately below.

IMPORTANT binary identities: out/empty-view-candidate.exe is ab6eb0f semantics,
SHA5047D04BB83303B32AD25737A2918219F9DC815518BB6458A9EDB088291B829B.
Owned runtime C:/tmp/D3RendererVerification/scene-view-test currently has it.
out/build/renderer executable is REJECTED16-lane fog experiment; source restored
to8, so rebuild before deploying that build.16-lane007 GPU .563949 vs8 .558496,
no win; source fully reverted. Sparse fog-bin experiment also rejected.
Old baseline out/build/renderer-ipo is rebuilt bfb59647 (NOT modern IPO build),
SHA A6286E9315A60F3C93B3A764C93A808B8FA385924ADDFACE1B573433374F3DA4.

Scaling runs under C:/tmp/D3RendererVerification/scale{002,006,007}-{new,old}:
1080p output SSAA2/MSAA4/AO/bloom,1000frames means400..899,CPU2,-limitframe0.
GPU new/old:0025.943174/6.280078;0064.476182/5.834370;0075.709490/6.451678.
007repeat new/old5.648724/6.405546. CPU007 drifts/inconclusive, not universal win.
Effects differ, so NOT strict visual/work equivalence. Earlier sustained720p
eight-save CPU wins remain separately qualified; don't mix timing regimes.

CURRENT INVESTIGATION (read this instead of old transcript): save007 old/new
headlight-on images differ substantially: broad pale highlight on left wall in
old, absent in new. Still present difference with AO/bloom off (scale007-plain-*).
With NO HEADLIGHT atframe90, old/new BOTH show that pale contribution:
wall007-{old,new}-unlit/saveg003.png. Thus investigate headlight interaction with
static specular/reflections; not established cause. Fog-debug wall007-fog shows
only weak fog on that wall. Do NOT call this an intentional fog improvement.
No code changes made for this issue yet. Last inspected gl_scene.cpp fragment
static+dynamic specular (~414..458), vertex reflection packing (~195..220),
reflection GPU table (~775..850), and SceneLights.cpp headlight setup.
Potential next narrow test: headlight-on frame90 at720p matching the unlit pair,
then isolate shader reflection contribution/NaNs or packed influence addresses.
Do not infer failure from unmatched frame/resolution captures alone.

Keep recovery small: one investigation, bounded tool output, no new agents,
no sprawling historical search. Existing profile/image scripts are ignored out/
helpers; use one owned runtime and one comparator, no runtime clones.


LATEST after eef3393: image scratch resets only touched binding/pixel slots,
not both full registries (~144K ints/frame). Zero sentinel/index+1 preserves
source aliases; touched list retains capacity, never assets or cross-frame IDs.
005 sustained ABBA new/control/control/new CPU .616462/.638409/.645107/.612961;
GPU .133941/.133134/.134374/.133456. 002 pair CPU1.171495/1.215888,
GPU .680062/.677071. Images exact005/heavy002 with resource/geometry/image
revision audits and late-view probes. Evidence slots* under verification root.
Comparator now packed-fog eef3393 semantics, SHA
8A825179FC149B5CF060ADB2C5EFB3CED3ACAFE0F6C1E3DC81AC77A46BA0E238.
Build has sparse slot reset; runtime currently comparator after last timing.
Looking at residual preparation/attachment representation. Do not confuse
inspecting this path with authorizing cancelled cockpit geometry changes.

LATEST after34773af: packed immutable fog BVH nodes into two vec4s (bounds,
first/count); removed unused asset escape and third mostly-empty fetch. Both
field builder and analytic fragment refinement updated. Added capture-frame-only
GPU timestamp pair for field build (results read after view submission).
Field costs ~.36ms before, ~.335-.340ms packed in002/007 capture diagnostics.
Sustained007 ABBA new/control/control/new GPU .568490/.576816/.575273/.556072;
CPU .914597/.790870/.831632/.773197 (drift, no attributed CPU benefit).
002 pair GPU .673430/.695699. Images exact007/002, roof flare, heavy002
SSAA2/MSAA4/AO/bloom/multiview; field occupancy audit passes; all3 tests pass.
Evidence packed* under verification root. Sparse per-ray bin initialization
was rejected: same images but007 GPU .591662/.579663/.576691/.588530 ABBA;
source fully removed. Fog workgroup remains8 (32 previously rejected).
Comparator out/empty-view-candidate.exe currently34773af semantics, hash
A8028CC470E059798417DB4A462210686D954A0331FAAB29F99EC95FD3BB8547.
Runtime/build now packed-fog; Candidate/Previous/delivered preview untouched.
Latest usage65% consumed, credits unchanged. Continue, do not yield.

LATEST after aee6973: consumer-owned view coverage now constrains mirror
producer portal visibility and rasterization without changing projection,
allocation, resolution or sampling. Near-plane crossing conservatively keeps
the full image. Coverage cannot narrow main/gameplay-feedback views. All three
contract targets pass, including aperture selection/projection/scale/near-plane.
Pixel-exact: 006 720p/4K, 006 1080p SSAA2/MSAA4/AO/bloom, heavy002 multi-view,
and ceiling/flare roof reproduction. Evidence aperture* under verification root.
720p sustained ABBA new/control/control/new (3000 frames,500..2499):
CPU .391691/.387266/.391094/.392676; GPU .446367/.473308/.474834/.445740.
Mirror surfaces 73 -> 66. 4K ABBA (1000frames,400..899):
CPU .425156/.401622/.402040/.446080; GPU 1.799880/1.839620/1.834632/1.798590.
GPU gain modest; no CPU win claim, especially 4K. Comparator remains aee6973
semantics AF7862... at out/empty-view-candidate.exe. Owned runtime has aperture.
Surface-output shader specialization was rejected (no measurable benefit),
all source removed; evidence out/rejected-surface-output.txt. Do not repeat.
Next: remaining save007 fog GPU cost. User says STOP STOPPING: keep working,
no final/yield at checkpoints. Usage last64%, credits unchanged62451.4387600000.
No deletion, Candidate/PreviousCandidate/delivered preview unchanged.

LATEST afterf786976: sustained eight-save corpus is CPU-faster than reconstructed
old in EVERY save (roughly34-69% first sweep;005 repeated advantage23-36%).
out/sustained-baseline-results.txt holds exact numbers and evidence paths.
Same720p/noAO/bloom/SSAA/MSAA,CPU2,fixed dt,seed/headlight; BOTH -limitframe0.
3000frames,rows500..2499. Do NOT mix with paced120FPS results, where005 lagged.
New approved fieldfog differs from old legacyfog: not strict effect parity.
GPU006/007 stillslower by~.045ms in that sweep.006 has NO fog at all.

Rejected32-thread fog groups:007 GPU .646376 vs8-thread .580300;006 unchanged
as expected for no fog. Pixel exact. Source reverted to8. Details in
out/rejected-fog-lanes.txt. No shared-memory accumulation changes implemented.

Validated no-medium early exit in applySceneFog: transmission stays1, radiance
unchanged when interior count0/exterior disabled.006720p and4K images exact.
4K1000frames400..899 ABBA new/control/control/new GPU
1.829818/1.919534/1.916514/1.827844 (~.089ms,4.6%); CPU
.378016/.406526/.399322/.397430.720p GPU .473781/.489691 (single pair).
Build includes early exit and8-thread fog, SHA
AF7862F6655F782BC4EFF40490148866646DFF5C8243C6CFA47CB73F06310109.
Added explicit UNIX argument-test definition/POSIX declaration; all3 contract
targets pass on Windows including simulated GNU spelling (not a Linux build).
Next: shader output contract specialization for absent surface-record consumers.
Usage63% used, credits unchanged62451.4387600000. Continue without yielding.

LATEST after572463c: room surface BVH experiment REJECTED and all source/tests
removed. Conservative correctness and exact heavy image passed, but both corner
and interval node tests regressed CPU. out/rejected-room-index.txt has evidence.
Do not resurrect without a different cost model. Existing comparator now holds
572463c semantics, SHA FB5956BB66AED50FB4FFDB4E5CDE9428304C772311A1C7B7175F9813FAA207CE.

Uncommitted: FindArg no longer allocates a temporary string per lookup. Preserve
live exported GameArgs edits; no stale-result cache. Also bound lookup/GetArg
indices. New standalone argument tests cover duplicate/case/long/missing options,
live mutations, full table bounds and zero allocations across60000 lookups;
Windows and GNU spelling branches pass, as do scene contracts. Heavy multiview
image is exact. args005 new/control/control/new CPU1.202974/1.134726/1.161596/1.050180,
GPU .143328/.148486/.144218/.145634. Drift prevents a firm speedup claim.
Current runtime/build hold this candidate; no BVH remains. Source args_tests.cpp
is untracked and must be staged with args.cpp/CMakeLists on checkpoint.
Benchmark runs have been paced120FPS; next compare sustained throughput using
-limitframe 0 on BOTH renderers, fixed capture dt remains unchanged. Do not
compare those timings directly with earlier paced runs. No OS power changes.
Usage59% used, credits unchanged. Continue, do not stop after checkpoints.

CURRENT uncommitted after 0e93c7f: model asset lookup is a bounded touched-slot
array, and previous-instance identity is resolved once per object (checked
ordinal with full-identity fallback), not once per LOD. Capture-only mesh
inventory shows005 has385 meshes,278 model attachments,20 position streams,
7 UV streams. Heavy multi-view/SSAA2/MSAA4/AO/bloom audit passes, pixel exact
against foreground-coverage-multiview; image visually inspected. 005 is exact.
Lookup ABBA new/control/control/new CPU .989792/.996054/1.113586/.988628;
GPU .146354/.142892/.146078/.149980. Control drift precludes a large gain claim.
Evidence lives in C:/tmp/D3RendererVerification/lookup* (NOT worktree/out).
Long command-line capture paths truncate: keep the established short root.

Verified: immutable ModelAsset owns ordered attachment indices;
reused model materials traverse only these candidates, preserving mask/custom
and hierarchy exclusion. Full material/audit traversal still uses shared append
logic. Contract tests pass for sparse/full role equivalence. Heavy multi-view
image remains exact. Against reconstructed IPO-ON old005 (new/old/old/new),
CPU1.081220/.915870/.918290/1.064262; GPU .143820/.313464/.315288/.142794.
Thus005 is still CPU-slower; do NOT claim the goal is met. Evidence attachment*.
Next investigate immutable room spatial indexing to reduce per-face query work.

Pre-rewrite source baseline was reconstructed cleanly at bfb59647 in managed
worktree C:/Users/Jeff/.codex/worktrees/renderer-baseline-rebuild/DESCENT-3MASTERED.
Reused out/build/renderer-ipo for THAT baseline (not modern IPO experiments).
Current old build IPO ON hash A6286E9315A60F3C93B3A764C93A808B8FA385924ADDFACE1B573433374F3DA4.
It is NOT the missing historical frozen binary. out/reconstructed-baseline-results.txt
records the first IPO-OFF corpus. Both IPO ON,005 old/new/new/old CPU
1.215698/1.242192/1.248760/1.205988, GPU .355526/.151030/.153606/.357338.
007 CPU1.506008/1.312890/1.169590/1.128914, GPU .563614/.592722/.576442/.510162;
whole distribution drifts despite repeat images matching. No definitive007 win.
out/empty-view-candidate.exe is now the 0e93c7f control (FD3018DA...086B82).
Sampling old005 shows driver/string lookup; new005 about half render samples
in preparation, especially resources/model preparation and destruction. Sampler
uses GameRenderFrame warmup for BOTH and perturbs timing; sampled timings invalid.
No PowerShell deletion. Usage58% used, credits unchanged62451.4387600000.
User again says STOP STOPPING: checkpoints are not stopping points.

LATEST after b53e1d2: foreground composition consumes its existing RGBA8 layer
directly when SSAA is off; MSAA still resolves on read, SSAA keeps its reduction.
BlendLayerOverPostPresent replaces the hardwired post-composite input. The
fullscreen pass writes source alpha directly (ONE,ZERO), exactly replacing
clear-alpha-then-source-over. Prepared rendering no longer clears alpha first.
Legacy adapter retains its clear. -scene-copy-foreground selects the reference
copy/clear schedule for measurement; no source/GPU quality reduction.
Build/contract tests pass. 006 short and 4K long,005 long, MSAA4/AO/bloom006 and
heavy SSAA2/MSAA4/AO/bloom multiview are pixel-exact. Heavy image inspected.
Requested1293x731 fell back1280x720; do NOT claim odd dimensions were tested.
Same-binary ABBA 4K006 copy/direct/direct/copy (frames400..899 of1000):
CPU .621204/.646870/.639166/.619816; GPU2.056648/1.837332/1.838224/2.064036.
GPU improves ~.223ms (~11%); CPU slightly worse, NOT a CPU win claim.
720p005 CPU1.179576/1.174798/1.193564/1.146526;
GPU .171166/.157860/.154590/.163896. CPU remains mixed/no demonstrated benefit.
Evidence foreground-final4k006-*,foreground-final005-*,foreground-msaa006-*,
foreground-coverage-multiview. Runtime/build contain this tested candidate.

Rejected per-view reflection-interface specialization: seven fewer vec4 varyings
when no active authored fields, pixel-exact006 but no GPU gain at720p or4K.
ALL experiment source removed. out/rejected-reflection-interface.txt has numbers.
No extra binary copy retained. User emphatically reiterated do not stop; continue
after checkpoints. No PowerShell deletion; no cleanup deletions occurred.
Next: reconstruct pre-rewrite source bfb59647 as a clearly labeled NEW baseline,
reuse the owned runtime/build storage where possible, do not claim old hash parity.

LATEST after e4983b1: camera winding now travels in the per-view fog traversal
entry; immutable fog geometry no longer receives glBufferSubData per volume/view.
Geometry headers shrink from four to three vec4 records. Exact fog retains its
boolean-inside initial interval; field integration retains signed winding.
Build and scene contract tests pass. fog-view-state-exact002, -multiview
(SSAA2/MSAA4/AO/bloom plus auxiliary views), -roof and 1000-frame002/007 captures
are pixel-exact against their controls. Heavy image visually inspected.
ABBA007 CPU 1.108336/1.096470/1.080328/1.055240; GPU
.595246/.640312/.596194/.640292. ABBA002 CPU
1.471862/1.477848/1.464004/1.436532; GPU
.724650/.702894/.746810/.706006 (control/split/split/control).
These measurements do NOT demonstrate a timing improvement. Retain this as an
ownership correction, not a performance win. Runtime holds tested split binary;
out/empty-view-candidate.exe remains the preceding comparison binary. No extra
executable copy was made. SceneFogShaders.h and gl_scene.cpp are the code scope.

User confirmed they removed the missing historical benchmark during cleanup.
Do not search indefinitely or substitute Candidate for that exact binary.
Old source bfb59647a3c3b4a451c264b3941c441573ecc061 remains available; a rebuild
would be a NEW baseline, explicitly labeled. Cleanup manifest
out/renderer-artifact-cleanup-20261021.json was created, but deletion was blocked:
NO files were deleted. User explicitly says do not delete via PowerShell.
Do not retry that operation. Keep artifact creation bounded: active build plus
one existing comparison executable, compact evidence, no new runtime clones.
Usage51% consumed; credit balance unchanged62451.4387600000.

LATEST current source: ExecuteSceneView plans passes before GPU preparation.
Empty non-clearing views skip GPU uploads/setup, but still advance view history;
depth clears, sky clears and exported main-world products continue executing.
Stage-local glGetError checks are always on in non-NDEBUG; Release opts in with
-scene-gl-audit. Same-binary audit on/off ABBA005 CPU1.0535/1.0652/1.0631/1.1684
does not demonstrate a reliable speedup from removing polling alone.
Empty-view ABBA005 CPU control1.090722 / scheduled1.075722 /
scheduled1.072942 / control1.094944; GPU .16148/.16198/.16239/.16176.
Small consistent ~.019ms CPU improvement, NOT the all-baseline goal complete.
Contract tests/build pass. empty-view005 image exact; empty-view-multiview
(1920x1080 SSAA2 MSAA4 AO+bloom, image/deformation/resource probes and GL,
geometry/image/fog audits) pixel-exact against scalar-fog-multiview. Visually
inspected heavy image, no new defect apparent. out/empty-view-candidate.exe
preserves final tested binary; runtime restored to that after benchmarking.

LATEST after82f2525: texture producer-revision audit found writable GameTextures
exported to external game modules (Game2DLL/DMFC). Do NOT replace the table scan
with only in-repo writer notifications. Fresh current82 stack005 profile:
5534 samples,422 preparation,819 total rendering; prepareImages116,
procedural bitmap evaluation77, publication93, models86; material scan7 only.
View preparation122, driver123, foreground198 (inclusive; not additive).
Attempted bounded immutable fire/water input storage reuse. Tests/build passed,
pixel-input005 image exact, but ABBA CPU .983354/1.151224/1.039534/1.063990
did NOT show a gain. All source removed; out/rejected-pixel-input-pool.txt and
out/pixel-input-candidate.exe preserve experiment. Runtime restored control82.
The old frozen perf-old-runtime directory is now also absent; 45 selected
executables plus140 worktree-out executables did not yield old SHA DED4E006.
Candidate/Prev hashEDAB17AA is a DIFFERENT build, not the benchmark replacement.
Asked user asynchronously if the frozen baseline was moved. Do not substitute
another baseline silently. Continue independent work, no blocker stop.
Current uncommitted gl_scene.cpp moves four unconditional glGetError polls per
view behind Release -scene-gl-audit (always enabled non-NDEBUG). Build running
out/scene-gl-audit-build.log; not yet benchmarked. Plan same-binary audit on/off
and heavy GL-audit capture. Next potential upstream improvement: build the view
pass schedule BEFORE GPU preparation; an empty non-clearing view has no GPU work
but currently still uploads state. Preserve main world products, clears/history.
Usage49% consumed; credits unchanged62451.4387600000. No preview touched.

LATEST after 0a2a8d6: opt-in -capture-thread-cycles appends raw render_thread_cycles
to the TSV (-1 unavailable/disabled). Windows QueryThreadCycleTime includes
user/kernel thread work; never convert these counts to elapsed milliseconds.
Two queries only when capture perf logging + explicit flag are enabled. Build
passes and captured counts are valid. Wall/cycle changes largely track each
other here, so scheduling interruptions alone do not explain timing drift.
Warm image owner local is no longer const, permitting implicit move on return.
Different-binary image-owner005 gave move~.96ms/control1.01-1.09ms, pixel-exact,
BUT same-binary temporary copy/move switch did NOT confirm a large gain:
image-owner-same005 copy/move/move/copy CPU.999/1.034/1.028/1.054, cycles
4.673m/4.841m/4.779m/4.908m. Do not attribute the earlier gap to this one line.
Temporary copy probe removed from source. out/image-owner-probe.exe preserves
that experiment only; current owned runtime still probe exe at this checkpoint.
out/image-owner-move-candidate.exe has the final no-probe source semantics.
Usage48% consumed; credits unchanged62451.4387600000.

LATEST after a908e90: frame compilation retains collection membership across
recycled snapshot storage. Exact ordered model collection IDs and mesh
(collection,has-receiver) pairs prove reuse; payload changes do not rebuild
membership. Changed layout rebuilds, with validity cleared before allocation.
Only IDs/ordinals retained, no assets or mutable source pointers. Tests cover
membership changes, reordering, receiver changes and held snapshots. Tests/build
pass. collection-pair005 and collection-layout-multiview are pixel-exact.
ABBA005 CPU1.2898/1.1274/1.0979/1.1070 had strong drift; reverse BAAB1000frames,
rows400..899: layout1.1024/control1.1318/control1.2326/layout1.1111. Favors
reuse but control variability prevents a precise percentage claim. GPU~.162ms.
out/collection-layout-candidate.exe is the owned runtime; no preview modified.

LATEST after fafef18: water height impulses use retained integer disk row spans
and wrapping SSE2 additions (scalar fallback). No per-frame allocation. Tests
compare every radius0..255, six clipped/interior centers and strengths0/1/255,
including overflow. Contract/build pass; disc-pair005b image is pixel-exact.
Isolated kernel benchmark: radius16/32/64 speedups5.12/7.69/8.91x; radius3~1.09x.
Whole-frame ABBA CPU1.2264/1.2196/1.1892/1.1648 shows NO convincing frame gain.
Retained as a measured kernel improvement, not credited toward overall target.
out/disc-stamp-candidate.exe is the tested binary. Owned runtime ended on scalar
control after ABBA. Harness old manifest disappeared externally during this run;
out/profile-renderer.ps1 now owns the exact verified launch arguments instead of
depending on that historical artifact. First failed disc-pair005-control folder
is incomplete; use disc-pair005b-* results. Other delivered builds untouched.

LATEST after e868ed3: common-color fog fields now store scalar optical depth
(R16F/R32F), reconstructing weighted RGB from the common color. Mixed-color
visible volumes retain RGBA. Exact equality only; different densities are valid.
No sampling resolution, edge refinement, topology or fog equation change.
-scene-fog-vector-field is the diagnostic control. Contract tests cover empty,
mixed, density-varied, subset and nearly-equal colors. Release/tests pass.
scalar-fog007 is pixel-exact against vector; scalar-fog-multiview max RGB2,
mean0.00167; scalar-roof comparison max1. Heavy and roof images inspected.
Full-precision scalar audit max error6.33e-8. Half audit max0.000474 in heavy.
ABBA scalar-pair007 vector/scalar/scalar/vector GPU .7394/.6739/.6719/.7000;
heavy002 at1920x1080 SSAA2 MSAA4 AO+bloom GPU6.9209/6.8218/6.8078/6.8593.
This is a modest roughly0.05-0.08ms saving, not a dramatic overall speedup.
Field storage/bandwidth per texel reduced75% for common-color media. CPU does
not show a gain. out/scalar-fog-verified-candidate.exe is current owned runtime.
No delivered preview, Candidate or Previous Candidate changed.

Fresh packed-baseline005 ABBA old/new/new/old CPU1.2586/1.1893/1.211/1.22
(last two rounded); 007 about1.473/1.276/1.25/1.44. CPU005 still barely ahead,
007 GPU still regresses versus old. Current sampler cpu-packed-stack005-fixed
has294 preparation /505 total render samples: preparation resources74,
publication71, models60; view preparation76. Earlier slash-path sample captured
nothing; only the -fixed report is usable. Sampled timings are NOT benchmarks.

LATEST after 0b166cb: explicit scene shader interface packing, requested for
portability. 14 smooth vec4 slots + one flat ivec2 slot, reserving a sixteenth
slot for clip distance. All original values/qualifiers preserved. Reflection
payload is seven vec4 rather than nine vec3; normal/alpha, UV pairs and surface
lighting/position payloads also packed. packed-interface006 vs its control and
packed-interface-multiview vs ipo-default-multiview are pixel-exact; heavy image
visually inspected. Runtime shader compilation passed. RX6600 reports 64 varying
components, 128 vertex outputs and 128 fragment inputs; these are API limits,
not measured hardware register allocation. Mobile hardware remains untested.
Reflection-only packing ABBA showed no measurable RX6600 GPU gain, including
1920x1080 SSAA2 MSAA4. Retained for the explicit portability requirement.
Shelved shader feature specialization and empty-reflection-lobe masking: no
repeatable improvement (masking regressed CPU). Their rejected patches/binaries
remain under out/; neither experiment is in source. No delivered preview changed.
Current packed binary: out/packed-interface-candidate.exe; original IPO control:
out/ipo-verified-candidate.exe. Usage 43% consumed, credit balance unchanged.

LATEST after6fa4372: supported Windows Release IPO now defaults on through CMake
CheckIPOSupported, with D3_ENABLE_RELEASE_IPO=OFF or explicit CMake IPO override
available. Debug unaffected. ipo-default-build.log passes; /GL verified.
ABBA ipo-combined003 CPU1.7067/1.6235/1.6071/1.7263;
0051.4057/1.2174/1.2229/1.3768;
0071.3907/1.2710/1.3600/1.4044. Work CPU improves too. All three images exact.
Heavy ipo-default-multiview audits pass; image differs only locally around
Buddybot (plus its secondary-view image), whose pose hash differs despite same
position. Ordinary ipo-heavy control/IPO similarly differs only bbox
1279,561..1393,657:0.279% pixels above2. All other pose hashes match. Inspected:
no fog/cockpit/material defect apparent, but this is NOT a pixel-exact heavy
comparison and whole-program optimization may alter simulation rounding.
Normal no-IPO backup out/bloom-demand-candidate.exe; default IPO backup
out/ipo-default-candidate.exe; owned runtime currently latter. No preview changes.

LATEST after f0aba62: shelved ordered canvas collection composition. Exact
normal/heavy images, tests passed, but ABBA005 CPU1.33/1.36/1.32/1.29ms did not
show a win. All source changes removed; out/rejected-canvas-composition.patch
and out/canvas-composition-candidate.exe preserve the experiment only.

Bloom protection now allocated/written only when bloom consumes it. Bloom-only
settings changes update framebuffer resources; zero attachment explicitly
detaches retired storage. Removed obsolete scene attachment3 blend policy.
Capture-only -scene-bloom-resource-probe tests off/on/restore without changing
dimensions or user settings. bloom-demand006 and bloom-demand-multiview images
exact against controls (heavy AO/MSAA/SSAA + resource audits). Toggle captures
False/True versus steady have maximum RGB delta2, no pixels above2; inspected.
Build bloom-demand-build3.log passes. ABBA006 GPU .523/.521/.517/.532ms;
007 .694/.683/.743/.687ms: no clear timing win claimed. This removes unused
storage/work but does not solve GPU006/007 regressions. Owned runtime is
out/bloom-demand-candidate.exe; delivered v3.1 and Candidate remain untouched.
Last usage39% consumed; credits unchanged62451.4387600000.

LATEST after732e1c5: rejected the shared GPU constant upload arena. Persistent
and subdata paths matched the heavy multiview image exactly, but initially put
fence retirement mid-submission and increased measured GPU interval. Moving
retirement to presentation boundary restored GPU~.17ms; ABBA005 CPU
1.2141/1.2626/1.2749/1.3450 did not establish a useful win. Removed ALL arena
source changes. Preserved out/rejected-scene-stream.patch and
out/rejected-SceneStreamArena.h, binaries out/scene-stream*-candidate.exe.
Do not accidentally deploy those binaries. Normal build restored afterward.

Isolated whole-program build out/build/renderer-ipo (CMake
CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=ON), source732e1c5, no tracked build
configuration edits. ipo-pair005 control/IPO/IPO/control CPU
1.2727/1.2298/1.1872/1.3839; GPU~.17 throughout; image exact. Promising but only
one scene tested, not a renderer-architecture speedup claim. Keep this baseline
binary before rebuilding IPO with further changes.

Image metadata now derived from validated contiguous mip base extent/count,
eliminating a per-revision heap vector. make_shared also combines image and
ownership block allocations. image-layout-tests.log PASS, image-layout-build2.log
PASS; first game build failed Windows max macro, fixed parenthesized std::max.
image-layout005 and image-layout-audit003 are pixel-exact against their controls;
003 includes resource and image-revision audits. No wall-time claim yet.
Owned scene-view-test now contains normal image-layout build; delivered v3.1
unchanged. Last usage37% consumed, credits unchanged62451.4387600000.

LATEST: upstream ViewGeometryDemand qualified by geometry-demand-tests.log,
geometry-demand-build.log and geometry-demand-audit-build.log. Optional
-scene-demand-audit reconstructs resource demand from the draw list and compares
all five source-ordered lists. Audit passed save000 500 frames, including pinned
core2 (geometry-demand-audit000-pinned). Current non-audit capture
geometry-demand-current000-pinned matches geometry-control000-long exactly.
Save005/007 and heavy multi-view save002 (SSAA2/MSAA4/AO/bloom plus resource,
deformation and image audits) also exact. Short save000 control exact.
Earlier pre-audit binary geometry-demand000 and repeat had different door/bot
poses at frame500; audit build and control match. Unpinned audit/plain captures
have the same tiny bot pose difference. Cause of long simulation variation is
NOT proven; do not call it a shading change or silently suppress it. The fixed
capture seed controls ps_rand, not necessarily script DLL rand state.
Pinned700-frame save005 ABBA demand-pair005-{control,demand,demand-repeat,control-repeat}
CPU roughly1.29/1.25/1.22/1.26ms, GPU~.17ms throughout, images exact.
This is a small improvement, not completion of the performance goal.
Owned runtime now contains out/geometry-demand-auditable.exe; v3.1 unchanged.
Latest usage36% consumed; credits unchanged62451.4387600000.
Next: remaining preparation/execution costs in005, then GPU006/007.

CURRENT HEAD66591e2 (sine stamps committed). Fresh eight-save pinned-core2,
500frame rows100..449 comparison in refresh-baseline-{old,new}000..007:
CPU old/new:0002.059/1.488,0012.220/1.081,0023.081/1.835,0032.547/1.869,
0041.008/.546,0051.184/1.250,0061.090/.645,0071.472/1.393ms.
GPU old/new:000.731/.546,0011.107/.922,0021.681/.771,0031.265/.507,
004.429/.200,005.334/.172,006.462/.525,007.556/.770ms.
Old runtime verified hash DED4E006BBD00AC30E58E7E0ABF38DCB008A46D68A16F76F1B17B9717176B44A
matches prior baseline. CPU005 still regresses;007 small win only. Six clear CPU
wins, not all-scene goal complete. GPU006/007 remain worse. Timing drifts.

REJECTED fog-field screen rectangle experiment: volume AABB crosses eye plane in
007, requiring full320x180 grid; no work reduction and shader mask overhead.
Removed ALL source changes; patch retained out/rejected-fog-window.patch.
fog-window007 initial capture FAILED shader reserved word active; corrected to
in_field for v2. Own failed process55832 stopped after validating exact capture
path. v2 image exact, optical audit max5.84e-5, GPU~.806 vs previous~.770.
Do not resurrect or claim cropping improvement. Deliveredv3.1 unaffected.

CURRENT UNCOMMITTED: ViewGeometryDemand in SceneView.h is produced in source
order by queryResolvedView (room/model/part/terrain/mesh inventories), pooled
with ViewVisibility, consumed by PrepareSceneViewTables. Removes whole-level
room/mesh/terrain scans for each cockpit/HUD/world view and removes backend
resource rediscovery/sorting. Initial backend-only version visible-submission005
and000 images exact; CPU noisy1.30/1.42. Upstream version currently building via
geometry-demand-tests.log/geometry-demand-build.log; NOT yet runtime verified.
Tests added for grouped model demand, terrain demand, room source order, pooled
storage clearing. Owned runtime currently contains backend-only version, not
latest upstream source. No other source experiments active. lib/gitinfo.h remains
untracked/unrelated. Continue until user stops or included usage reaches zero.

LATEST HEAD698aadb commits validated owner hints. New sine-stamp change compiled
and tested: authored water sine disturbances compile integer additions once per
source element/radius/height; positions/ticks remain execution inputs. Kernels
retire with FreeStaticProceduralsForTexture; no steady-state pixel allocation.
SceneProceduralStamp.h pure tests compare original sqrt/FixCos/truncation and
clipping at radius0..255, strength0..255, edge/outside positions and signed wrap.
sine-stamp-tests2.log PASS; sine-stamp-build.log PASS. First test run failed
because test called InitMathTables twice and hit intentional RNG guard; removed
redundant test-only initialization (pose suite already initializes math).
Fresh images sine-pinned007 (700frames), sine-stamp005 (500), sine-stamp003
(90frames, resource/image audit) EXACT against controls.007 visually inspected.
Isolated kernel benchmark out/sine-stamp-benchmark.cpp:2000 radius64 stamps
legacy64.8-65.4ms vs compiled15.4-16.1ms,4.07-4.22x; exact final integer grids.
Pinned007 ABBA wholeframe CPU stamp1.315/1.222 vs control1.600/1.431ms; GPU
~.69ms both. CPU drift still means do NOT treat raw ratio as robust game gain.
Runtime scene-view-test has sine stamp candidate. Deliveredv3.1 untouched.

CURRENT HEAD4b648d6 commits indexed whole-material groups and producer palettes.
Owner hint qualification complete:005/007 and roof flare EXACT. Pinned007
ABBA CPU hints1.405/1.349 vs controls1.404/1.415ms; small/noisy, not major win.
Unpinned ABBA reverses direction between005 and007: do not claim its raw delta.
Tests cover invalid hints, reorder, allocation generation change and held frames.
Further experiment: adjacent multidraw range coalescing produced exact images
but no repeatable improvement. REJECTED, source removed. ABAB coalesce-pair-*
CPU control1.429/1.351 vs merged1.432/1.365; do not attribute earlier007 drop
to it. Runtime timing drift also moves old007 to1.219 vs historical1.508.
Current uncommitted owner-hint change avoids reconstructing owner membership
that model producer already knows. Publication validates index against exact
generation handle, falls back to lookup for stale/reordered builders. Tests
owner-hints-tests.log pass, game owner-hints-build.log passes; owner-pair007
images EXACT. Initial timing favourable but drifting; owner-abba005/007 runs
in progress at this edit. No marked-win claim yet. out/model-palettes-qualified.exe
is4b648d6 control, out/owner-hints-candidate.exe new experiment.
Fresh samples: cpu-model-palettes-stack005/007.txt; caller attribution corrected
in cpu-model-palettes-callers007.txt (skip duplicate top stackwalk frame).
Largest lookup leaf belongs to publication model owner resolution, motivating
hint change. Sampling runs never used as timing benchmarks. Usage34%, credits
still62451.4387600000. No shadows, no deployed preview overwrite.

LATEST UPDATE: image ownership committed24628d2 (supersedes uncommitted text
below). Whole opaque material groups replace contiguous runs, with static GPU
index buffers and glDrawElementsBaseVertex; per-primitive light addressing
retained. Depth-disabled/read-only, transparent, deformed and per-corner
reflection paths retain individual face ordering. Cockpit/static producers now
publish palettes too; alternate cockpit original-alpha filter retains face
bindings. No geometry/style change. Cockpit base submissions149->19.
Final tests/build: model-palettes-final-tests.log/model-palettes-final-build.log.
Captures model-palettes005/007/006/vertexlight/final001 exact against matching
controls;003 retains earlier one-pixel max5 delta. model-palettes-multiview
SSAA2/MSAA4/AO/bloom/audits differs max1, visually inspected. Roof flare capture
model-palettes-roof-flare EXACT against flare-fixed-closeup; ordinary flare
model-palettes-flare visually correct too. Sparse multidraw experiment rejected
for GPU regression, not retained. Static indices released on asset retirement
and renderer teardown. No per-frame index construction.
500-frame rows100..449: model-palettes005 CPU~1.33 GPU~.17ms;007 CPU~1.50
GPU~.75ms. Old CPU1.334/1.508ms: around parity, NOT marked improvement.
Delivered v3.1, Candidate and Previous Candidate untouched. Owned scene-view-test
contains current performance experiment. Usage32%, credits unchanged.

LATEST: model batching committed18db51c. Final tests pass; final001 and
vertexlight images exact. New uncommitted image ownership cleanup separates
ImageExtent metadata from pixels(level); deferred ImageAsset now returns recipe
bytes directly instead of copying them through a second call_once/vector.
Tests verify metadata does not evaluate and vector address is shared; held
images still valid. image-owner-tests.log passes, image-owner-build.log passes;
image-owner005/007500frame images exact. CPU1.43/1.63ms vs model-runs1.38/1.62:
NO reliable timing win claimed, buffer duplication eliminated. Sample now
out/cpu-model-runs-stack005.txt (40sec,2200frames, do not use timing):8048samples,
prep1023/render1945; prep resources285/models234/publication224/procedural154;
render query300/GLexecute483/viewtables149. Owned diagnostic process finished.
Runtime scene-view-test contains image ownership experiment, v3.1 unchanged.
out/model-runs-qualified.exe preserves previous executable. Usage30%, credits
unchanged. Continue targeting current dominant costs, not just old hotspots.

MODEL EXPERIMENT VALIDATION (not in v3.1): primitive light lists + contiguous
opaque material runs reduce005CPU1.56->1.38ms,0071.81->1.53-1.62ms. Both images
exact, GPU005~.17,007~.75-.81ms. Still not marked old-renderer wins. First run
model-runs005/007 accidentally omitted field flag; do NOT compare its007GPU
to field baseline. Correct runs model-runs-field*, then model-runs-winding*.
GPU model culling initially used terrain front-face convention: visible holes
in006 ship. Corrected to clockwise for polymodels, including reflection parity;
batch compatibility now separates model from other winding classes.006 exact;
003 differs one pixel by5 levels (depth-order tie),001/005/007 and vertexlight
exact before winding fix. Fresh combined MSAA4/SSAA2/AO/bloom/deform/resource/
image-audit view: model-runs-control-multiview vs model-runs-winding-multiview
EXACT. Older fog-refine-msaa-multiview reference differs and is not current
baseline. Last source compatibility/comment edit needs final build. Experiments
remain uncommitted until final verification. Keep stash backup.

DELIVERED v3.1 separate folder C:/tmp/D3RendererVerification/
fog-field-preview-20261021-v3.1, commit ef5db93, SHA256
E043BD4CB8B12642D27B2EBB0C5E3A9439A0DBE2B043F6C7DD979C72E3EFC3A9.
Packaged smoke flare-v31-packaged-smoke pixel-identical to fixed closeup;
pilot restored from v3 after harness. Links delivered via commentary.
V3/Candidate/Previous Candidate unchanged. Model experiment stash APPLIED
successfully after packaging; stash retained as backup. Continue its runtime
validation, now on flare-fixed source. Last usage28%, credits unchanged.

User reports v3 fog looks clean in roof room, but flares show random coloured
noise (clipboard-41724c53-9832-4822-b897-abdbfacdcabd.png). Priority is reproduce
and fix this regression; continue work, do not yield unprompted. V3 remains
immutable. Added capture-only -capture-fire-flare at frame10 for reproduction.
Source baseline is v3 7e179a0 plus that capture hook.

CONFIRMED/FIXED: RT_WEAPON initialization leaves tmap_override zero; old weapon
draw never consumed it. Migrated SceneModels applied it to every face. Flare
trace authored textures50/880/901/902 all resolved0; bitmap0 is intentionally
random diagnostic pixels (bitmap/bitmain.cpp). Resolve override only for
RT_POLYOBJ, including the material cache key. Saved weapons also corrected.
Capture-only -scene-weapon-view-probe places camera4units behind first weapon.
Before/fixed closeups flare-before-closeup/flare-fixed-closeup visually confirm
random triangle becomes authored yellow material. flare-fixed-loaded verifies
saved weapon (seeds/savegame/flareprobe20261021). Ordinary save002120frame image
flare-before-regression002 vs flare-fix-regression002 pixel-identical. Scene
contract tests pass (flare-material-tests.log). Fixed exe preserved temporarily
out/flare-fixed-qualified.exe; final build needed after diagnostic before toggle
was restored in source. No fog changes in this fix.

Unverified model batching experiment preserved in stash commit
b0a96f2bb1919ca4c89c9490f1bc2a4eb5024ebf, named "Paused unverified model batching
for flare regression isolation". Seven renderer/test files; no unrelated edits.
It moves per-face light lists to primitive data and groups opaque undeformed
same-material model runs. Tests pass, primitive-light-only images005/007 exact
but no timing gain; material-run runtime validation still outstanding. Not in v3.
Restore only after flare isolation, preserving intervening changes. lib/gitinfo.h
is unrelated, never stage. Last usage 27% consumed, credits unchanged.

## Current handoff state — view-level fog pipelines verified

LATEST USER: continuous fog through ceiling space requested (async answer), and
v2 screenshot still visibly stair-steps at the diagonal green/clear roof edge.
Do not mistake filling that space for fixing undersampling. Keep clear-space
fixture as regression. User preview was running briefly; left untouched.

Authoring preview now supports -scene-inspection -scene-fog-room-source TARGET
SOURCE. It copies optical properties in the owned frame before fog compilation,
not simulation/level data; no hardcoded room ID or default appearance change.
Roof preview uses183 184. Build continuous-roof-preview-build.log; new broad,
shallow three-room connector tests verify merging, grazing rays, held assets and
restoration (continuous-roof-tests.log passes). Captures roof-continuous-control,
roof-continuous-preview,roof-continuous-below visually inspected: ceiling fogged.
These use640field, no AO/bloom,720p. No packaged preview update yet.

Rejected room-culling experiment (removed): omit per-face room aperture tests,
keep portal visibility/backface/model bounds. Four images pixel-identical but
CPU0051.64->1.34,0071.45->1.71,006.73->.80,0021.66->2.04ms; GPU mostly worse.
Raw room-cull-clean-control*/room-cull-coarse*. Earlier room-cull-control* ran
near a build and must NOT be used for timing. Runtime subsequently restored.

Rejected fog reconstruction experiment (removed): interpolate depth per column,
convert each to visible contribution before bilinear XY blending. Four reads
instead of one; roof720motion error/flicker .676838/.777628, effectively unchanged.
Raw fog-series-edge-motion-visible320, out/fog-visible320-metrics.csv. Saturation
order does not explain the roof undersampling in this test.
Rejected cubic B-spline reconstruction (removed): four trilinear reads, unchanged
depth law. Roof720motion error/flicker .917781/1.024906, worse than linear.
Raw fog-series-edge-motion-cubic320 /out/fog-cubic320-metrics.csv.

NEW selective fog refinement: field shader reads four neighbouring columns at
the endpoint depth, compares visible RGB/opacity contribution ranges, and uses
the exact boundary integrator if any range exceeds max(1/255,25% of maximum).
Otherwise bilinear field reconstruction remains. Positive contrast heuristic,
NOT a guaranteed error bound (thin features missed by every ray can evade it).
No history/jitter. Exact/no-fog programs retain their specialized paths.
Initial absolute-only1/255 rule improved quality but cost4.54ms4K. Relative rule
retains almost all quality and costs2.50ms4K. 720pmotion error/flicker .068799/.098485
vs linear .676760/.777600; full64frame4K .069469/.099052 vs .668850/.769273 (~90%/87%
reductions). Raw fog-series-{edge-motion,roof4k-motion}-relative320, metrics in
out/fog-relative{320,4k320}-metrics.csv. Initial absolute trial adaptive320 and
adaptive4k*; normal4K image visually inspected with original clear boundary.
Fresh500frame4K save002 paired old/new CPU3.10/2.15 GPU2.64/2.54ms; withAO/bloom
CPU3.59/2.41 GPU12.52/11.96ms. Modest GPU win, effects not strictly identical.
Raw fog-refine-paired-{old4k,new4k,old4k-effects,new4k-effects}.
Contract tests fog-refinement-tests.log pass. Runtime currently refined320.
Qualification completed: fog-refine-roof-continuous{4k,-exact4k},
fog-refine-msaa-multiview (1920x1080SSAA2MSAA4AO/bloom with auxiliary image,
deformation/resource/geometry/image audits). All visually inspected; continuous
roof vs exact meanRGB.202904,max4,>2 pixels .008365%. No claim exact.

V3 DELIVERED via commentary links: C:/tmp/D3RendererVerification/
fog-field-preview-20261021-v3/Launch Roof Preview.cmd (continuous), plus
Launch Clear Ceiling Test.cmd (original boundary, sampling improvement only).
Also Launch Fog Preview.cmd (save002,continuous), Launch Roof Reference.cmd
(exact current rewrite,continuous; NOT old renderer). New isolated2.18GB copy,
v2/Candidate/main untouched, no desktop files. Exe7e179a0 SHA256
D2D8CB7C6C461F767D81CB6E7261BB2E8E51EF49827D0E727EC18B2CA7562871.
Build fog-preview-v3-build.log; packaged smoke fog-preview-v3-smoke visually
inspected; JEFF.plt restored from v2 AFTER smoke harness overwrote it with seed.
No timer/audits/forcedgraphics settings on interactive launchers. Authoring room
override now scoped to initial scene_lifetime; loading/travelling/restarting
disables it to avoid changing unrelated levels. Relaunch to inspect it again.
No updates to this delivered preview without a new explicitly separate artifact.

LATEST: bounded compact optical-field storage. Field remains opt-in, exact fog
remains default. The field now selects RGBA16F when a conservative optical-depth
bound including color magnitude and repeated store-rounding fits a half-range
margin; otherwise RGBA32F. No normalization overflow or saturated-depth truncation.
Diagnostic -scene-fog-full-precision forces32. Earlier -scene-fog-half-field trial
flag removed (old capture manifests still name it). Two compute programs own
format-specific layouts/uniforms; texture reallocates on dimension/format change.
Unit tests cover ordinary/dense/long/invalid/rounding-growth bounds. Build/tests
fog-half-bound-* pass; final flag wiring fog-half-final-build.log, both runtime
formats audited in fog-precision-{default,full}. Runtime scene-view-test matches.

640x320x65 memory203.1MiB->101.6MiB. 4K500frame002 GPU clean3.23->2.98ms;
AO/bloom12.62->12.54ms (small/noisy improvement, NOT general victory). Old clean
2.72ms, so high-quality field still behind old clean GPU. Full64frame4K roof:
32biterror/flicker .412680/.465575;16bit .414803/.466743. Tiny precision cost,
still much better than320column .668850/.769273. Audits001/003/007 maxerrors
.00021653/.00002555/.00005943; multiview .00048548/.00045305, references agree.
Normal4K half screenshot visually inspected. Capture maxRGB75 in effects image
falls in known moving-object region; do not attribute that peak to quantization.
Raw fog4k-half-*,fog-half-audit-*,fog-half-multiview,roof4k-motion-half640.
No preview/Candidate/main changes. C:free26GB; avoid duplicate full4K series.
Publication now compiles model/mesh owner and attachment ordinals into the owned
frame. View queries no longer repeat those identity lookups. Builder-supplied
ordinals are overwritten, reordered frames recompile, and held frames retain their
own indices. New tests cover these cases and detachment; contract tests/build pass
(owner-ordinal-tests-final.log/owner-ordinal-build.log). Runtime scene-view-test
includes this change. 005/007 and combined multiview/deformation/resource/audit
captures are pixel-identical to aperture-clip baselines. CPU005 1.61->1.58ms,
007 1.66->1.71ms: mixed/no meaningful performance claim. 007 uses full-precision
field320 to isolate this CPU change from the newer default field storage.
Raw owner-ordinal005/007/owner-ordinal-multiview. Preview v2 remains unchanged.

Native4K roof qualification completed (3840x2160). Full64frame rotating series
fog-series-roof4k-motion-{ref,field320,field640}. Scaled diagnostic ROI/band by
image height;720p results unchanged. 4K boundary meanerror/flicker320 .668850/.769273,
640 .412680/.465575: ~38%/39% reductions. Not elimination; clear ceiling remains.
Metrics out/fog-roof4k-field{320,640}-metrics.csv. Normal4K640 AO/bloom screenshot
visually inspected, also actual pre-rewrite roof screenshot (small cockpit text
is shared by this fixture/config, not evidence of a new regression).
Dedicated500frame save002 native4K AO+bloom GPU:32011.35ms,64012.62ms,
old12.58ms,exactrewrite23.42ms. CPU2.29/2.36/3.69/2.30. Single runs; effect
differences limit strict old/new comparison. Series timings never used.
Raw fog4k-effects{320,640,-old,-exact}; normal fog-roof4k-{normal640,old-normal}.
No new preview/package/settings changes. C:free29GB after full-resolution series;
avoid unnecessary duplicate series. No captures/builds/metrics processes active.

ProjectedMesh now omits duplicate lateral camera-plane checks when its aperture
is contained in[-1,1]. Near and mirror/world planes remain, and wider apertures
retain all planes. This can only admit additional conservative edge candidates,
never remove previously visible geometry. Tests pass; 005/007 and multiview
images pixel-exact, logged per-view surface counts unchanged. Raw aperture-clip*.
CPU0051.50->1.61,0071.80->1.66: mixed, no whole-frame speed claim.
Next quality check should cover native4K: user screenshots are ~3840x2160,
where the fixed320-column preview spans12 display pixels per column. Existing
moving-edge metrics are1280x720 and must not be represented as4K qualification.

Latest source: portal aperture bounds stream clipped vertices/intersections
directly, removing two temporary polygons per traversed portal. A bounded ring
queue and queued/exterior scratch retain capacity; no view result borrows them.
512 deterministic tilted/eye-crossing camera cases compare exactly to the old
materialized clipping algorithm. Existing cycle/widening/door/exterior tests pass.
Build portal-stream-build.log; tests portal-stream-tests.log. 005/007 and combined
AO/bloom/resource/deformation/auxiliary images pixel-exact to plane checkpoint.
CPU whole-frame timings drifted upward (0051.37->1.50,0071.48->1.80); do NOT
claim a measured speedup. This removes needless intermediate work/allocations,
but remaining CPU goal unmet. Fresh interleaved old/new BEFORE this change:
0051.18/1.37ms,0071.41/1.48ms. Raw plane-baseline-*,portal-stream-*.
Runtime matches source; no user preview changes, no active captures/builds.

October21 continuation after commit1f0bee2: rejected and removed three trials:
fused diffuse/specular emitter traversal (pixel-exact but006GPU .88->.91ms),
direct-slot model asset transaction cache (005CPU1.36->1.38,0071.44->1.41;
no persuasive win), and rasterized65-layer optical integration (002GPU.71->2.10).
Raster trial used signed exit-entry depths with viewport-layer instancing;
roof sampled optical maxerror7.43e-5,0078.55e-7, but expensive layered fill.
All their source changes removed. Raw fused-lighting*,model-slot*,fog-raster*.
No user preview changed. Runtime restored before next experiment.

Group-width experiments32 and16 gave no consistent cross-scene improvement;
kept8, now centralized SceneFogFieldLanes for shared indexing and dispatch.
Raw fog-lanes{16,32}-{clean002,007}; images unchanged. No tuning flag retained.

Current source precomputes triangle cross-product normals once per fog asset
upload in existing vertex padding (a.w,b.w,c.w); no extra texels/fetches. Both
exact and field use it. Build fog-plane-build.log and scene tests pass.
500frame002 GPU field .71->.70ms, exact1.57->1.50ms (single runs; modest/noisy).
Both benchmark images pixel-exact. Roof audit max1.743e-6; multiview audits
1.9595e-6/2.4289e-6, CPU references agree. Full64frame rotating roof edge:
meanerror .6767627 vs .6767604; residualflicker .7776005 vs .7775997. Effectively
unchanged, NOT a claimed shimmer fix. Final image maxdiff1 on .00059% pixels.
Raw fog-plane-* and fog-series-edge-motion-plane320. Runtime matches source.
Lastusage17% used, credits unchanged. User preview still immutable effbe85.

Working source now specializes the scene fragment program at view level into
no interior fog / exact boundaries / field sampling. No per-material variants.
Exterior analytic fog remains common. Table upload precedes pipeline selection,
and each program owns its cached uniform locations. Missing uniforms are legal
glUniform(-1) calls. Build fog-pipeline-build.log and existing scene tests pass.
Clean 500-frame save006 GPU .95ms control -> .88ms specialized, repeat .878ms;
field save002 .71ms both control and specialized. This is a modest no-fog win,
NOT an explanation for the remaining old-renderer GPU gap. Both images pixel
exact against effbe85 controls. Roof audit and multiview/resource/deformation
audits pass; multiview screenshot visually inspected. Raw fog-pipeline-* runs.
Preview v2 remains immutable effbe85; only scene-view-test runtime updated.

Fresh CPU sampler completed normally:14807 total samples,1563 preparation.
Preparation inclusive resources424, publication397, models313, procedural231;
these overlap, not additive times. Whole-level publication remains a meaningful
cost; do not bypass immutable/multiview ownership by skipping hidden resources.
No sampler running. Last included usage16% used, ordinaryUsageAllowed=true.

## Previous checkpoint — preview v2 available; CPU investigation continuing

Commit effbe85. Preview delivered in commentary (not a turn-ending yield):
C:\tmp\D3RendererVerification\fog-field-preview-20261021-v2
Launch Fog Preview.cmd starts save002, Launch Roof Preview.cmd starts
roofprobe20261021; both width320. Reference launchers use exact CURRENT integrator,
not old renderer. Original preview/Candidate/main unchanged; no desktop files.
Package smoke test passed and normal roof image visually inspected:
fog-preview-v2-roof-smoke/saveg003.png. Clear ceiling strip still present, explicitly
disclosed. ExecutableSHA2569E1655FDE2441B9F1D08C4A75547B19FE6905325E471ABDB5507F42D407B00A9.
Package is a separate2.18GB copy; never overwrite it while user may be testing.

Continuing toward remaining CPU baseline misses005/007, not stopping for pending
art-direction answer. Fresh read-only stack sampler running against exact
scene-view-test runtime (NOT preview): out/cpu-streamed-stack005{,-errors}.txt.
35second sampler,6000frame capture cpu-streamed-sampled005. Sampled timings are
INVALID for performance claims. Current build/PDB match. Check processes/sessions
before any build/runtime replacement. Last included usage14% used, allowed.
Multiview field audits maxerrors1.96e-6 and2.43e-6, both CPU references agree.

## LATEST: streamed column integration; sharper optional preview being prepared

Supersedes implementation/cost details immediately below. Depth128 experiment
rejected: moving-edge error1.319/flicker1.427 vs64depth1.386/1.480, butGPUclean
.59->1.16ms,effects6.02->6.75. Kept64slices, centralized SceneFogFieldSlices for
allocation, GLSL compiler define, diagnostics; fragment lookup derives actual
texture depth. No new user setting. XYwidth320 DOES halve boundary error/flicker:
still1.264/.223, rotation.677/.778, vswidth1602.928/.272 and1.386/1.480.

Reduced shader temporary storage12KiB->4KiB per8-ray workgroup: remove vec4
density_samples array, integrate each volume's scalar occupied length directly,
add its colored prefix into the owned output column. Coherent read/write image,
memoryBarrierImage before each subsequent volume's readback. Every invocation
owns its full column; zero-contribution columns explicitly cleared. No races or
cross-invocation blending. Same result to <=1 display-level rounding on tiny
fractions of pixels; audits512 samples in roof/001/003/007 <=1.04e-6 against both
CPU definitions (which agree exactly). Single compute pass, no history/jitter.

500frame GPU width160 .59->.42ms; width3201.39->.74ms clean,7.11->6.31ms effects.
CPUstreamed1601.71/3201.77/effects1.80ms. Single runs, do NOT claim all-baseline
victory. Raw fog-streamed{,320}-{clean,effects}, old fog-single320-*. Normal
images inspected previously; mirrored probe pixel-exact but its room has NO fog
(do NOT claim reflected fog qualification). Offscreen fog view plus resource,
geometry, deformation audits pass fog-streamed-multiview; six-save audit suite
fog-streamed-suite-*. Build out/fog-streamed-build.log. Runtime matches source.
Source uncommitted atop e3e2c38; next checkpoint/package optional new preview.
No active captures/build. Lastusage14% consumed, ordinaryUsageAllowed=true.
No Candidate/main/old-preview changes. Async roof-art question remains pending.

## LATEST: temporal fog rejected; single-pass field verified against controls

Supersedes ALL history/normalized-half-storage details below. Centered spatial
control beat bounded temporal on hard-edge error AND flicker. Per-column
scrambled jitter also failed (still3.466/.719, rotation2.148/2.117 vs centered
2.927/.272 and1.387/1.481). Removed temporal reconstruction, history textures,
history lifecycle, jitter toggles and intermediate density textures. No dormant
alternative. Compile centered ray crossings, analytic per-cell occupancy and
forward integral in ONE compute dispatch into65-layerRGBA32F. Density is direct
1/authoredDepth, no normalized-half units. Shell classification fix retained.
This removes ~170lines, two compute stages and three volume textures. It does
NOT solve spatial aliasing or change the authored clear roof space.

Source uncommitted atop60c9cae; tests/build PASS out/fog-single-pass-{tests,build}.
Runtime scene-view-test matches. Completed full64frame
fog-series-edge-{still,motion}-single, then500frame fog-single-{clean,effects}.
Boundary-band single-pass error/flicker: still2.92757/.27179, rotation1.38570/1.48045,
matching centered-control results to rounding. GPU clean .80->.59ms, effects
6.24->6.02ms, sparse003 .55->.40ms. Single runs, not an all-baselines claim.
CPUclean1.81/effects1.85/sparse1.77ms. Roof CPU/GPU audit512 endpoint samples:
maxerror5.96e-7; both independent CPU definitions agree exactly. Normal effects
image inspected. Raw fog-single-roof-audit/fog-single-sparse. No active processes.
No release/Candidate/old-preview change. User requested continuous work, no
shadows, no credits. Last usage11% consumed; no agents. Nonblocking roof-art
question still pending; do NOT stop waiting for it. Latest spoken update:
removing temporal because controls beat it, measuring simpler field next.

## October 21 continuation — full-cycle roof-edge validation

Added `-capture-series-from N`: captures each frame N..target, exits only at
target. Intermediate names append `.frameNNNNNN.png`; final name unchanged.
Save-output is final-frame-only. Five-frame smoke check preserved the final
single-capture image exactly. Series introduce readback/diagnostic overhead;
NEVER use their frame times as performance evidence.

Full 64-frame cycle (90..153), stationary and rotating roofprobe20261021,
`fog-series-edge-{still,motion}-{ref,raw,temporal,wide}`. Reference is exact
post-shell integrator, raw disables history, wide uses width320, others160.
Grayscale fog diagnostic; metric ROI y100..429, reference low optical depth
(0<gray<80) in either frame. This is a low-density-region proxy, not an isolated
geometric contour or HUD-free mask. Residual flicker measures change in
(candidate-reference), removing common scene motion; units are 8-bit levels.

| Mode | Still mean error / flicker | Rotation mean error / flicker |
|---|---|---|
| Raw jitter | 1.274 / 1.605 | .884 / 1.101 |
| Temporal160 | 1.023 / .285 | 1.274 / .589 |
| Temporal320 | .807 / .225 | 1.094 / .500 |
| Temporal160, local history bound | .959 / .293 | 1.095 / .606 |

Local history bound clamps reprojected density to current cell plus six axis
neighbors. Reduces accumulated boundary smear, modestly worsens flicker. Build
out/fog-history-bound-build.log; runtime captures fog-series-bound-*, normal
color fog-bound-color-{ref,field}, both inspected. Clear ceiling strip persists.
Single500frame clean/effects GPU .80/6.24ms vs before bound .79/6.26ms. CPU
1.79/1.97 vs1.67/1.82ms (no CPU-path change; not a controlled CPU conclusion).
Effects1920x1080 SSAA2 MSAA4 AO+bloom, clean1280x720 no effects. Rows100..449.
Full CSVs and reproduction script under ignored out/fog-series*metrics.csv and
out/measure-fog-temporal.ps1. Old preview and Candidate NOT updated.

REJECTED EXPERIMENT: round field range up to power-of-two depth bands to
reduce camera-rotation rescaling. Captures fog-series-depth-* measured still
error/flicker1.044/.296, rotation1.243/.613: worse than local bound with fitted
range. Removed; loss of depth resolution outweighed hoped-for stability.
Bound-only executable preserved out/perf-checkpoints/fog-history-bound.exe.
Latest usage9% consumed, ordinary allowed.

Additional boundary-band metric includes clear pixels within a four-pixel cross
of nonzero reference fog (high<80); still includes some HUD/geometry transitions.
Tracked reproducer tools/renderer-fog-temporal-metrics.ps1 requires -Root and
supports -BoundaryBand. Full-cycle before/after local bound: still error
3.647->3.539, flicker .892->.925; rotation error2.384->2.090, flicker2.176->2.087.
Thus edge-local measurements support modest improvement, NOT solved shimmer.

Cross-scene500frame suite fog-bound-suite-savegNNN-{ref,field}, clean settings:
GPU ref->field000 .31->.56;0011.82->.89;003 .30->.55;004 .22->.21;
005 .19->.19;006 .93->.98;0071.48->.86. Images003..006 pixel-exact;
001meanRGB .388 max7;007 .045 max2. 000 image comparison INVALID for fog-only
inference: visually different animated door/robot poses between runs (mean2.91).
This reveals wasted field work in003: camera-cone fog candidates beyond visible
surfaces still trigger injection. Next safe direction: conservative maximum
endpoint depth from submitted geometry, shared optical candidate rejection,
NOT camera-room membership hacks. No implementation of that culling yet.

FOLLOW-UP: endpoint culling was implemented, measured, and REJECTED. First
whole-instance bounds, then upstream ViewVisibility.maximum_endpoint_depth from
submitted vertices. Both preserved tested exact-reference images. Save003 nodes
152->79, but still one candidate and field GPU .55ms vs exact .29ms; tight-query
CPU1.97field/1.80ref vs prior1.77/1.71 (single runs, unfavorable). Global endpoint
depth does not resolve per-ray occlusion. All eight edited source/test files
restored via inverse patch; experimental diff out/rejected-fog-endpoint.patch,
evidence fog-endpoint-* and fog-query-endpoint-*. Revert tests/build PASS
out/fog-endpoint-revert-{tests,build}. Runtime restored to retained local-bound
source60c9cae. No active captures/build. Included usage11% used, allowed.

IMPORTANT NEW CONTROL: `-scene-fog-jitter-off -scene-fog-history-off` centered
sampling beats temporal on the actual clear/fog boundary band! Full64frames:
centered still error2.927/flicker.272, rotation1.387/1.481, versus bounded temporal
3.539/.925 and2.090/2.087. Prior comparison against jittered raw overstated the
practical benefit. Temporal reconstruction has NOT passed acceptance; field
remains opt-in and original user preview/Candidate untouched. Captures
fog-series-edge-{still,motion}-centered; CSVout/fog-centered-boundary-metrics.csv.

CURRENT EXPERIMENT: per-column scrambled XY jitter instead of one coherent
Halton offset across the entire field, testing whether spatially correlated
error causes the moving edge. Uncommitted SceneFogShaders.h plus diagnostic
gl_scene.cpp toggle. out/fog-scrambled-build.log PASS, runtime matches. Captures
fog-series-scrambled-edge-{still,motion}-temporal in progress. Must compare to
CENTERED control, not only raw jitter. Reject if unjustified; temporary shader
uses (.5,.5) diagnostic sentinel, clean up explicitly if retaining experiment.

Asked user nonblocking art-direction question: soften fog into authored clear
roof space vs continuous fog vs preserveclear/fixshimmer. No response yet. Do
not pause; continue performance/sampling regardless. Looked at official Flax
VolumetricFog.shader (GitHub master): density/scattering history beforeintegration,
extra samples when history unavailable. No code copied or new feature adopted.

## October 21 continuation — density history, roof reproduction, snapshot repair

LATEST UPDATE (supersedes storage/injection details below): actual upstream bug
found and FIXED in SceneExtraction.cpp: bounds_volume ignored FF_NOT_SHELL, unlike
physics fvi_QuickRoomCheck. Detail surfaces remain renderable but no longer bound
fog. Geometry audit now recognizes shell flag edits; extraction tests cover this
and retained-frame isolation. Roof connected fog boundary1452 ->420triangles.
Before fix, rawfield audit vs old signed CPU integration differed .306; a new
independent CPU positive-winding interval oracle agreed within .0041. After shell
fix both CPU references agree EXACTLY on all512samples; max field error .002885.
This resolves that cancellation discrepancy, NOT the authored clear roof space.
Capture: fog-shell-roof-audit. Successful tests/build: out/fog-shell-tests.log,
out/fog-shell-build.log. Current benchmark runtime matches this source build.

Injection now integrates occupied fraction per depth cell analytically from
crossing slope/depth events rather than one jittered depth point. XY jitter only.
Temporal sample centers are physical mid-depth of quadratic cells (and inverse
reprojection accounts for that offset). Cell integration is exact for valid
non-overlapping boundaries; overlapping sheets are clamped per cell (approximate).
fogOccupiedLength is a SLOW sorted-crossing audit/test oracle, not a render-loop
query. Both oracle definitions remain reported; do not silently redefine parity.
Pre-shell cell-vs-point residual flicker improved only10–15%; need remeasure after
shell fix. fog-cell-edge-{still,motion}-{raw,temporal}-90..95 captures exist.

Storage now uses asset-wide normalized density (minimum authored depth) in three
RGBA16F raw/history textures; final accumulated optical depth is RGBA32F to avoid
half overflow on long paths. Before cell/shell changes this costs .88ms clean,
6.42effects (fog-units-*), meanRGB .017,max2 against32F. ~37% memory saving rather
than previous all-half50%. Histories absent for >1presentation retired, once per
presentation. Added image-write barrier BEFORE scratch reuse as required for
prior sampler reads; confirmed Khronos glMemoryBarrier reference. Fetch barriers
remain between producers/consumers. No field-default change, no user-preview update.

ACTIVE authorization: keep working; do NOT yield merely for a checkpoint or a
blocked subtask. User may be absent. Move to other safe renderer work if a task
blocks. Stop before credits. Latest usage check: 7% used, ordinaryUsageAllowed=true.
No subagents. No shadows. User preview/Candidate/main worktree remain untouched.

The roof defect is now reproduced by actually moving the ship upward after moving
out from under a nearby beam. From save002: -capture-forward -capture-forward-until
30 -capture-up -capture-up-from 40, capture frame120. Image/logs:
C:\tmp\D3RendererVerification\roof-path30-audit (save attempt here failed; do not reuse).
Successful saved fixture: roof-save-image\roofsave; immutable test-seed copy named
roofprobe20261021 in seeds\savegame. roof-reload proves the new save reloads.
Eye2657.94336,-209.141159,3611.18555, room183, fog_enabled=0, depth100.
Its lower portal0 leads to fog room184 (same connected volume as188), at y~-209.98.
Upper portals89/90/91/92/126 lead to fog room24 through grating at y~-199.89.
Thus a roughly10unit clear space separates authored fog regions. This explains
the broad un-fogged upper structure from ceiling height; not a rare viewing angle.
No room-number-specific fix and no authored fog changes made. A soft transition
would be a deliberate appearance change, NOT restored geometric equivalence.
User correctly identified this hard edge as a prime shimmer/undersampling test.

Second opt-in -scene-fog-field prototype replaces cumulative-history blending with:
jittered raw medium density64layers -> world-position reprojection/history of
density -> forward-depth integration65endpoints. Halton2/3/5, history time constant
.08s. Asset revision/camera history/sequence checks invalidate old medium history.
Per-view density pingpong; raw/final scratch shared and re-integrated on replay.
Still authored linear fog law, NOT fully lit/shadowed physical volumetrics.
fogWinding exposes actual signed occupancy; injection uses winding>0 rather than
doubling density for overlapping sheets. Unit test covers winding2.
Current experiment uses RGBA16F for all four textures (32F binaries preserved).
Storage range/degenerate authored densities and history resource retirement still
need review. Orthographic views still use exact reference. Defaults unchanged.

Diagnostics: -scene-fog-debug emits optical amount in grayscale; -scene-fog-history-off
disables temporal blending for controlled tests. -scene-fog-boundary-audit prints
containment/room diagnostics; -scene-fog-boundary-dump adds triangles and portal data.
Capture controls now support up, forward-until, up-from, heading-until. Pitch and
heading view-only fixtures exist too but do not move ship/headlight.

Temporal evidence: fog-stable-{ref,raw,temporal}-90..95, fog-edge-still-*90..95 and
fog-edge-motion-*90..95 under C:\tmp\D3RendererVerification. Motion uses physical
-capture-heading from saved roof pose. All grayscale diagnostics, not perf runs.
out/measure-fog-temporal.ps1 compares (candidate-reference) residual changes to
subtract real motion; ROI y100..429, low-optical-depth edge mask reference-owned.
Stationary edge mean residual flicker ~1.65raw -> .293temporal (0..255 units),
moving edge ~.784raw -> .507temporal. This compares the new jittered pipeline with
and without history, NOT against the first rejected unjittered preview. Six frames
is preliminary; do not claim shimmer solved. Normal images inspected as well.

500frame measurements, averages100..449, save002; all captures serialized and no
user game running. fog-bench-* folders, matching logs/settings. Clean1280x720,
SSAA1/MSAA0/noAO/noBloom. Effects1920x1080/SSAA2/MSAA4/AO+bloom:
  rewrite reference: CPU1.75/GPU2.46 clean; CPU2.12/GPU23.75 effects
  density32: CPU1.73/GPU1.10 clean; CPU1.93/GPU6.58 effects
  density16: CPU1.75/GPU .84 clean; CPU1.87/GPU6.30 effects
  density16 width320: CPU1.78/GPU2.57 clean; CPU1.86/GPU8.20 effects
  pre-rewrite: CPU2.38/GPU1.37 clean; CPU2.82/GPU6.22 effects
Single runs only, not a qualified overall win. Old/new effects differ and high-
settings GPU time is roughly tied, NOT faster.32Fvs16F clean image meanRGB .0513,
max2;16Fvsrewrite-reference effects image meanRGB .388,max11. Wide field is costly.
Executables preserved out/perf-checkpoints/fog-density-f32.exe and -f16.exe.

Incidental REAL migration bug repaired: SGSSnapshot called RenderModuleSceneView
outside ModuleSceneScope, then also needed an independent image rather than the
live display target after composition. PrepareGamePresentation(bool advance=true)
now supports non-advancing snapshot extraction, cockpit/HUD preparation and avoids
replacing previous_models. Save owns its published presentation, plans independent
SaveThumbnail image and mirror dependencies, reads it through explicit blocking
readViewImage(Presentation,key) API (RGBA8 bottom-up), packs existing1555 save format.
No g3 rendering state borrow, live-frame overwrite, or thumbnail present/flip.
Readback checks scene lifetime and presentation sequence; GL pack/FBO state restored.
Save+reload both succeeded; scene-contract tests pass. No save files outside test
runtime/seed were touched. Thumbnail visual QA and extended capture cases remain.

Current built source: out/fog-fp16-build.log. Latest tests out/fog-thumbnail-tests.log
pass. lib/gitinfo.h remains unrelated untracked; do not stage it. The previous
checkpoint below documents the FIRST rejected prototype and is superseded here.

## October 21 fog investigation — ACTIVE after user reset

User renewed work authorization after a usage reset: continue renderer improvements,
stop before credits. Last usage check was 1% used (99% remaining), not the old zero
below. No subagents authorized. Main checkout/Candidate/Previous Candidate untouched.

IMPORTANT: user TESTED and REJECTED the first optical-grid preview for visibly lower
quality and shimmer at face boundaries. The reported roof discontinuity remains.
Do not promote this prototype or claim the fog problem is solved. It is opt-in only.
User argues a complete modern volumetric implementation may outperform the exact
legacy conversion; agreed this is a serious candidate, not inherently too expensive.
No shadows. Pixel equivalence may be relaxed; common-case appearance and stability
in motion remain requirements. User was asked optionally to save the bad roof pose.

New experiment: -scene-fog-field, optional -scene-fog-field-width 32..640 (default160).
Each grid column traverses fog boundaries once, bins signed crossings into slope/
intercept prefix events, and emits64 quadratic-depth samples. Fragment interpolation
is LINEAR IN ACTUAL DEPTH between those slices. Grid sampling replaces per-fragment
BVH traversal. Shared workgroup storage8x1 avoids large local-array spills. NO temporal
reprojection/jitter/reconstruction: this omission is a known reason not to ship it.
Sources: renderer/SceneFogShaders.h, gl_scene.cpp, gl_local.h; ShaderProgram compute
link support gl_shared.h/gl_shader.cpp. -scene-camera-pitch degrees is a capture-only
view fixture in ScenePresentation.cpp. Default renderer still uses original integrator.

Stable USER PREVIEW copy (do not overwrite while testing):
C:\tmp\D3RendererVerification\fog-field-preview-20261021
Launch Fog Preview.cmd starts save002 with grid160; Launch Reference Fog.cmd uses
same executable without grid (rewrite reference, NOT pre-rewrite renderer).
Preview SHA256 CCC3863D64BCE8EBC0DB7B2AD20F5C46B4FCCF96349519F7972971DDCED39BB1.
Preview predates per-volume path clamping fix below. No desktop files created.
Do not benchmark while user game process runs. Three ff-bound* AUDIT captures
overlapped user preview: use numerical results/images only, NEVER their timings.

First same-binary fresh measurements (90frames, NoMarkers, avg15..84):
ff-ref002 CPU1.70 GPU2.52; ff-first002 CPU1.77 GPU.93; shared storage ffs-first002
CPU1.71 GPU.83. ff-ref007 CPU1.39 GPU1.80; ff-first007 CPU1.30 GPU.81;
ffs-first007 CPU1.34 GPU.77. Preliminary only, not qualified benchmarks.
Width80 ff80-first002 GPU.494,007.489; NOT visually qualified or offered as default.
First001 GPU1.05;006 .92. IMPORTANT006 has ZERO interior fog volumes and exterior=0;
its regression is separate, despite earlier summaries grouping it with fog scenes.
First160 images vs ga refs:002 meanRGB.230,max12;001 .460,max8;007 .041,max3;006exact.
Static small differences did NOT predict the user's visible shimmer. Motion QA required.

GPU numerical audit -scene-fog-field-audit reads512actual texels and compares against
independent CPU double-precision fogSegmentLength. Initial002 maxerror .005872 was
because grid omitted reference per-volume clamp(path,0,ray interval). Current source
restores clamp per volume, using scalar events and accumulated color samples. Audits:
ff-bound002 max1.28e-6,0012.08e-6,0071.34e-7. Final shader output not performance-tested
after this correction. Audit never belongs in timing captures. Initial ff-audit002
was stopped after audit failed; no screenshot. Later ff-audit2-* only diagnosed it.

Current isolated benchmark runtime scene-view-test exe matches latest fog-boundary
build/PDB, including clamp/audit/pitch fixture. Tests PASS out/fog-field-tests.log;
latest build PASS out/fog-boundary-build.log. Unrelated lib/gitinfo.h remains untracked.
No user preview mutation after delivery. No active capture or build at this checkpoint.

Roof investigation: -scene-fog-boundary-audit logs geometric containment vs view.room,
room convexity and nonplanar faces. fog-roof20 and fog-roof60 are save002 captures
with pitch20/60 and REFERENCE fog (not grid). Both ceilings still fogged; not yet exact
reported bad pose. Main eye2675.83887,-264.354462,3646.73877 room188, fogvolume21,
membership=contains=1,depth200,boundary1452triangles, rooms184/188/189/190. These rooms
are very non-convex (room188308/311face planes violated), so a single convex hull/box
is not an adequate replacement. Several authored faces elsewhere are nonplanar.
Looking upward admits many fog components behind opaque geometry into the optical
cone; current traversal does not use surface depth to reject whole-view candidates.

Research correction: old renderer is NOT just a single fog-plane algorithm.
SetupRoomFog retains that older path, but BeginRoomMaterialFog/PrepareRoomFogEntryMap
and generic.frag RoomFogAmount use rasterized portal entry-depth maps. Must trace
actual executed path before attributing the roof fault to old plane-selection code.
Modern references inspected: Epic volumetric fog docs (low-res grid plus jitter and
temporal reprojection), Epic local fog volumes docs (analytical integrals), Wronski
SIGGRAPH2014 publication page, Frostbite SIGGRAPH2015 abstract, Flax volumetric docs
and shader snippets (density/scattering temporal filtering BEFORE final integration).
Do not merely blur the accumulated optical field to hide unresolved room geometry.

## October 21 resumed work — supersedes restart pause below

STOPPED AT INCLUDED USAGE 0% (tool reported100% used). User allows only a small
accidental overrun; do not resume without renewed user direction/allowance.
Final finalbase-* benchmark loop and its exact capture child were stopped.
That sweep is PARTIAL and must not be reported as a completed comparison.
No active sampler/build. Runtime debug-opt-in.exe remains isolated and matches
the last compiled PDB. Main/Candidate/Previous Candidate untouched. Goal NOT met.
Last complete full comparison and subsequent two-scene results are below.

Latest runtime debug-opt-in.exe separates -scene-inspection renderer selection
from synchronous GL debug callbacks. Explicit -scene-gl-debug or existing
-scene-geometry-revision-audit enables those diagnostics. Unconditional GL error
checks remain. This fixes a measurement/configuration confound, NOT the CPU gap:
same-binary long ABBA dbg-* debug-on/off005(1.355,1.255)/(1.234,1.255),
007(1.425,1.336)/(1.666,1.280). GPU unchanged; both images and dbg-probe exact.
Current runtime/PDB match; final tests PASS before this GL configuration-only
change, build PASS after it. Latest included usage1%; stop at zero.

Last small demand-only MultiColor lookup experiment REJECTED; source restored.
color-demand.exe and cd-* are evidence only, NOT the checkpoint/runtime.
Long ABBA parent/new005(1.242,1.196)/(1.305,1.215),
007(1.298,1.330)/(1.447,1.388). Images/probe exact but no measured improvement.
Post-arena sampler out/cpu-geometry-arena-stack005.txt completed normally:
515 preparation/1155 rendering samples;resources132,models123,publication112,
procedurals81,extraction37. Driver-inclusive185; VAO-binding leaf4 vs prior30.
The sampler perturbs timing; use sample distribution, not sampled frame times.
Remaining upstream direction: producer-owned persistent presentation state for
unchanged room/model metadata, with explicit mutation/generation ownership and
lazy view-dependent evaluation. Current whole-level frame reconstruction is a
fixed tax in sparse views. This is a reasoned direction, NOT an implemented or
performance-proven solution; do not start it without checking the current state.
Latest included usage1% remaining. No shadows or cockpit geometry work resumed.

Newest source replaces one-VBO/VAO-per-geometry with shared geometry pages.
SceneGeometryAllocator owns free vertex ranges, coalesces retirements and uses
size classes for oversized allocations. GL owns one VAO/VBO per page; mesh GPU
records borrow names/ranges. Local polygon offsets are converted to page draw
offsets; all dynamic position/UV/reflection stream addresses account for them.
No shader math/ordering changes. Expired mesh assets and revised geometry return
ranges; scene teardown deletes pages once. Tests cover fragmentation, random
non-overlap, oversized ranges, retirement validation and reset. Tests/build PASS.
Temporary runtime geometry-arena-sized.exe (matches latest PDB); gas-probe exact.
ga-* eight short images, combined probe, quality and mirror exact on repeats.
First000 had the known alternate Buddybot pose/position; ga-repeat000 EXACT.
Long ga-* ABBA parent/new CPU:003(1.872,2.055)/(1.804,1.864),
005(1.454,1.251)/(1.221,1.227),007(1.360,1.738)/(1.325,1.356). GPU unchanged.
Observed uploaded geometry fits one1MiB page in sampled scenes. No general
all-baselines victory: gabase-* four short old/new pairs yield005 old.895 new1.163,
007 old1.132 new1.389. Preserve all rounds; variation remains substantial.
Full pre-arena iibase-* 3round normal-cap CPU new/old:
0001.342/1.544;0011.002/1.709;0021.666/2.295;0031.989/2.600;
004.543/.835;0051.140/.939;006.669/.872;0071.363/1.166.
Thus six of eight wins in that sweep, two misses; goal NOT achieved.
Fog GPU regressions still unresolved (0012.329/.905,0022.437/1.356,
006.954/.448,0071.785/.527 ms new/old). Do not conflate CPU and GPU results.
Newest sampler out/cpu-identity-stack005.txt completed normally before arena:
616 preparation samples/1247 total rendering; resources163,publication147,
models144,extraction39; foregrounds330,driver203,viewquery184. Driver leaf
nearest VAO binding30 samples motivated pooling; no post-arena sampler yet.
No active captures/builds/samplers at checkpoint. Latest included usage2%.
Main/Candidate/Previous Candidate untouched; unrelated lib/gitinfo.h untracked.

Newest SOURCE adds SceneIdentityIndex: exact ordered-key proofs retain room,
object and model lookup compilation across recycled frames. No source/asset
pointers retained. Inventory edits rebuild; failed compilation invalidates its
proof. Sealed portal references no longer revalidate unchanged polygon indices.
Tests/build PASS (reorder/removal/generation/duplicate/exception/held-frame and
portal mutation coverage). identity-index.exe and ii-probe images EXACT.
Normal-cap long ABBA ii-* parent/new CPU:003(2.295,2.067)/(1.830,2.249),
005(1.443,1.192)/(1.539,1.256),007(1.425,1.630)/(1.701,1.357). Mixed.
Room-only predecessor room-index.exe/ri-* retained as diagnostic evidence.
High-cap diagnostic iifast-* (1500frames,avg300..1199,framecap1000) parent/new:
003(1.201,1.302)/(1.325,1.203),005(1.068,.747)/(.796,.766). Both images exact.
Simulation timings also vary:005 .070/.041 parent vs .050/.043 new. Raising cap
changes absolute costs but does NOT eliminate variation. Do not substitute it
for normal-cap qualification or attribute all changes to the renderer patch.
Isolated /O2 same-process benchmark out/identity-index-bench.cpp/.txt,8rounds:
2200 room keys11.35us->.92us,500object keys2.30us->.20us,500model keys2.23us->.20us.
This establishes ~14.5us eliminated indexing work, NOT a whole-frame victory.
Latest usage4% remaining. Main/Candidate untouched. Continue baseline comparison.

Newest source shares a published model material palette between instances of
the same asset/appearance. Transaction-local borrowed canonical source; baked
per-object UV layouts are excluded. Pose/history/modulation/light candidates
remain per instance. Other appearances retain their own material history.
Tests cover shared bindings with independent animation/color and edit detachment.
Temporary runtime material-share-log.exe; tests/build pass. No active captures.
Storage counts at capture: 003506 variants/278 tables,005467/234,007495/263.
005 unique material surfaces8905 vs12301 before sharing. This is consolidation,
not a claim that all CPU baselines now win. NoMarkers long ABBA msn-* current
parent/new:003(1.851,1.874)/(1.866,2.099),005(1.256,1.298)/(1.243,1.277),
007(1.369,1.472)/(1.318,1.326). GPU unchanged. Raw marked ms-* also retained;
unit-test compilation overlapped that initial series, so prefer clean msn-*.
Eight short ms-saveg* images, ms-probe, ms-quality and ms-mirror match references
on repeat. First000 differed and logged different Buddybot position/pose;
ms-repeat000 from unchanged binary matches exactly, including its pose hash.
Latest included usage5% remaining. Continue; stop at zero, no deliberate credits.
Main checkout and Candidate/Previous Candidate remain untouched.

Upload allocator experiment REMOVED from source. ring-* wrapped within frames.
epoch-* and local-* had inflated GPU elapsed intervals because the retirement
fence was inserted after the next frame's GPU timer started. Moving retirement
to Flip after GPU timer end (upload-retire.exe / retire-1-*) restored .30/.19/1.60
ms. Per-pass queries were essentially unchanged. Earlier diagnosis of a large
mapped-memory GPU execution regression was NOT established and is superseded.
Correctly retired persistent mapping (upload-mapped.exe / mapped-* ABBA) CPU:
003 parent(2.20,2.04) new(2.06,2.34); 005 (1.62,1.61)/(1.44,1.57);
007 (1.74,1.71)/(1.69,1.60). Mixed, no clear overall win; allocator removed.
Binaries/raw results retained as evidence only, not qualified checkpoints.
Bounds clip-mask experiment REMOVED: eight images exact, tests passed, mixed
long ABBA (clip-*): 003 parent2.21/2.12 new2.17/2.14;0051.51/1.64 vs1.44/1.48;
0071.65/1.59 vs1.66/1.72. No clear overall win. clip-mask.exe evidence only.
NoMarkers short 3-scene comparison plain-* confirms remaining CPU regression:
legacy0032.53/2.48,005.88/.89,0071.11/1.07;
mesh-streams0031.94/2.28,0051.08/1.18,0071.34/1.17.
Fine-grained markers do add overhead but do NOT explain away misses.
New uninstrumented sampler out/cpu-plain-stack-005.txt completed normally.
Disposable no-poly-clip-diagnostic.exe (source already reverted) bypassed face
frustum tests, not backface/room/part culling. noclip-* NoMarkers long ABBA:
003 parent2.14/1.96 new1.99/2.24;0051.36/1.49 vs1.46/1.40;
0071.38/1.39 vs1.59/1.72. Extra geometry offset savings; not a candidate.
Current uncommitted changes: consumer-demand light lists (disabled views/unlit
surfaces omit preparation), empty position/UV/influence streams retain initial
GL storage without dummy uploads, identical preceding nonempty light lists
reuse their range (enables existing ordered draw batching). light-ranges.exe.
Eight short images and combined probe exact on repeats. First save002 capture
differed on a small region and had DIFFERENT Buddybot published pose hash:
827298972120713296 vs reference18368182606202919774; three repeats of the SAME
binary exact, as were single-draw and normal diagnostic reruns. Diagnostic flag
removed. Do not call that a proven renderer regression or hide the anomaly.
Full NoMarkers lrbase-* three rounds, avg render ms current/old:
0001.52/1.48;0011.03/1.65;0021.76/2.23;0031.93/2.55;
004.54/.76;0051.09/.90;006.66/.84;0071.35/1.14. Goal STILL NOT MET.
Timing variation substantial, especially current003/007; preserve all rounds.
Newest SOURCE and binary view-placements-direct.exe also move placement transforms
out of per-polygon VisibleSurface into view-owned pooled storage. Records <=48
bytes; room/terrain use identity sentinel, one placement per model part/mesh.
All consumers migrated. Tests cover copy/move/ownership/missing indices and
existing posed/sky/motion behavior. Tests/build PASS. Eight short images,
combined probe, SSAA2MSAA4 AO/bloom and mirror are EXACT. Long NoMarkers vp-*
ABBA vp-* was mixed/slower dense (parent1.88/1.88 vs2.24/2.10). Removed a new
per-polygon placement-table lookup from CPU consideration: pass the already
resolved per-part transform directly. vpd-* marked ABBA then measured dense
parent2.06/2.18 vs new1.95/2.00 render ms; wall1.65/1.37 vs1.45/1.69 (mixed).
Dense main-view preparation itself .277/.292 -> .235/.239 ms (~17% reduction).
Wall main-view preparation .139/.115 -> .111/.128 (small/mixed). Preparation
outside visibility varied substantially despite being unchanged; do not claim
whole-frame universal improvement. Long dense/wall images and vpd-probe EXACT.
Current temporary runtime view-placements-direct.exe; sampler/captures stopped
at this checkpoint. Main checkout, Candidate/Previous Candidate untouched.
Latest included usage 7% remaining; stop at zero, no deliberate credit use.

Newest mesh-streams.exe shares canonical effect light candidates and appends
prepared UVs directly into upload storage (no per-mesh temporary). Tests pass;
eight short scenes and combined probe EXACT. Long ABBA parent/new CPU:
003 (2.00,1.87)/(1.98,2.06), 005 (1.34,1.51)/(1.36,1.28),
007 (1.46,1.50)/(1.38,1.47). Mixed; goal still unmet.
Latest sampler out/cpu-mesh-streams-stack-005.txt adds external-leaf nearest
renderer call sites. Sampling finished normally; no sampler active.
Upload experiment completed/rejected as documented above.

Newest binary shared-pose.exe: frame-owned composed model transforms shared by
cockpit layers; cockpit ambient now instance modulation, so stable part/material
tables survive animation and lighting changes. General composed-pose execution
supports owned transforms without animation tracks, previous poses, deformation
and conservative bounds. Audit compares composed inputs explicitly (first probe
failed the obsolete track-only audit; fixed probe2 passes). Tests pass. All eight
short, three long, combined probe2, mirror, quality and headlight-off long images
EXACT. Ambient-off screenshot visually inspected. No cockpit geometry changes.
Includes publication's mirror-presence summary to skip mirror-free material scans.
Long ABBA parent/new render: 003 (1.93,2.14)/(1.98,2.34),
005 (1.33,1.26)/(1.32,1.26), 007 (1.47,1.43)/(1.59,1.42).
NO general performance improvement established. Raw share-*, ambient-*, shared-pose-*.
No active captures/sampler. Next: eliminate per-mesh light-list/temporary UV allocations.
Latest sampler before these changes: out/cpu-pose-reuse-stack-005.txt.

Latest binary pose-reuse.exe separates ResolvedModel execution data from the
PreparedModel authoring table. Demanded parts occupy one frame-owned arena;
dead snapshots release material owners but retain deformation buffer capacity.
No per-demand model allocation or material-table COW clone. Tests cover stable
model/part ranges, shared immutable bindings, buffer reuse and held-reader
isolation. Tests/build pass; all eight images plus combined probe, mirror and
SSAA2/MSAA4/AO/bloom EXACT to lights-*; quality image visually inspected.
Includes bounded source room lookup/reused model lookup, unsorted image hash
publication, combined model address/bounds walk, empty planar schedule return.
Performance NOT a general win: reuse ABBA long render means parent/new:
003 (1.94,1.87)/(1.91,1.97); 005 (1.47,1.30)/(1.26,1.58);
007 (1.71,1.43)/(1.39,1.41). Substantial temporal noise remains.
Intermediate compact/pose-values/pose-arena binaries retained for comparison;
no separate legacy implementation in source. Current raw reuse-* and
pose-reuse-* captures. No active captures/sampler at this checkpoint.
Continue CPU work; do not treat this checkpoint as completion or a yield.

Latest normal old/current recheck (bitmap-metadata, three90-frame rounds):
003 new1.994 old2.512; 005 new1.194 old.960; 007 new1.389 old1.121 ms render.
Both former misses remain misses. Raw meta-base-*. Main/Candidate untouched.

Newest binary bitmap-metadata.exe. Bitmap allocation/free/format producers own
a material revision; all 21 direct format assignments migrated to bm_SetFormat.
Normal preparation scans the bitmap registry only after revision changes;
image-audit mode scans every frame and rejects missed notifications. Eight
audited saves + combined probe and three unaudited long images EXACT. Build
passes. Long paired parent/new CPU means: dense 2.314/2.282, 005 1.569/1.463,
007 1.786/1.652 ms; temporal noise remains, not a full baseline win claim.
Raw metadata-* and bitmap-meta-*; normal old/current recheck of 003/005/007 next.
CPU2 pinning did not eliminate drift; NOT a shipping change, default harness
remains unpinned. Last sampler finished (cpu-light-surface-stack-005.txt), no
samplers or captures active at this checkpoint. Last usage 18% remaining.

Latest continuation: view-block-only.exe replaces separate camera/environment
uniform calls with one 240-byte std140 block. Three long scene images EXACT;
block+canvas experiment also matched combined probe/mirror/quality. No reliable
general CPU win established. Canvas fusion was removed: added compatibility
rules and mixed/noisy performance did not justify keeping it. Its binaries
canvas-views/view-block remain as measurement artifacts, not current source.
1000-FPS-cap exploratory timings are separate from the standard 120-FPS corpus.

Current binary light-surface.exe: view UBO, motion once per demanded model part,
polygon bounds once per light-candidate list. Unit tests/build pass, all eight
short images + combined probe/mirror/SSAA2/MSAA4/AO/bloom EXACT to coalesced.
Three long images exact. Sparse demand lists were removed entirely: both broad
and narrowed experiments failed to establish a benefit. No dead experiment API.
Standard long light-bound change improves save005 mean ~1.627 -> 1.532 ms;
dense/fog mixed. Motion dense mean ~2.173 -> 2.070, others mixed. Not general wins.
Current diagnostic pin-{1..4}-* captures compare part-demand/light-surface with
ONLY the temporary game's main thread pinned to logical CPU2. No global or
shipping affinity change. Wait for that loop before build/copy/sampling.
Goal remains unmet (last full normal corpus six CPU wins/two misses).
Last usage 19% remaining. No Candidate/main/desktop changes; lib/gitinfo.h is
unrelated and must remain untouched. Continue, not a requested yield.

Newest: part-demand.exe built, dense/wall/fog long images and combined probe
exact. Dense CPU modestly improved, others mixed. Full coalesced vs legacy
three-round CPU corpus still six ahead/two behind (005/007); see ownership doc.
No capture/sampler active. Next: fuse compatible display-canvas views while
preserving foreground layer ordering, clears/resolves and independent view APIs.

Newest: draw-coalesced.exe qualified against all eight images, combined probe,
mirror, high-quality dense scene; all EXACT. Includes owned indexed fire pixels,
lazy clip codes, SAH fog asset compilation, adjacent equal-UBO multidraw.
Fog-heavy high-quality image exact too; GPU 19.27 -> 9.73 ms from SAH alone.
Long base fog007 GPU now ~1.60 ms, still not legacy parity. CPU improvements
mixed; fresh full old/current comparison next, goal not established.
No active captures/samplers at checkpoint. Last included usage 23% remaining.
Evidence and exact component timings at top of renderer-cpu-ownership-repair.md.

Newest: fog-ray.exe is built/tested and all eight images, long save007, combined
probe, mirror and SSAA2/MSAA4/AO/bloom are exact. View-pruned boundary traversal,
explicit triangle edges and shared inverse-ray setup reduce long save007 GPU
from ~8.53 to ~3.50 ms (paired component tests documented in CPU ownership repair).
Still far behind legacy GPU; CPU goal remains unmet. No captures/samplers active
at this checkpoint. Runtime uses fog-ray.exe. Last included usage: 24% remaining.
Continue CPU profiling/repair; do not yield unless requested or usage reaches 0.

Newest: compiled-storage.exe built/tested; dense/wall/save007 long captures,
combined probe and mirror surface all exact. CPU gains NOT demonstrated (mixed
timings); goal still unmet. All capture/sampler sessions finished at checkpoint.
Next investigation: per-view pruning of fog's threaded boundary acceleration
tree, not just whole-volume rejection. Preserve exact geometry/integration.

Newest verification binary: fog-view-final.exe. All eight images, combined probe,
SSAA2/MSAA4/AO/bloom exact to water-owned. View storage/order and fog source/camera
candidate work tested; CPU timings mixed, not a claimed win. Fog GPU remains slow
despite candidates 29 -> 1 in save007. Three-round water-owned vs old comparison
is six ahead/two behind (005/007), not the marked-win goal. Last usage 28% remaining.
Uncommitted next change: frame-compilation storage recycling; build/tests running
under cpu-compiled-storage-*; do not assume that work is verified yet.

Newest: water-owned pixel recipes built/tested, all eight save images and combined
probe exact. Long paired dense/wall CPU wins; full old/current baseline repeats
are running under `cpu-water-baseline-*` (three rounds). Do not build while those
captures run. Verification uses water-owned.exe. No samplers running. Last usage
check 29% remaining. Goal NOT achieved. Continue until requested stop/0% included.
Indirect batching was tested and removed because its measured tradeoff was not
good enough; ignored patch/binaries retain the experiment. No Candidate changes.

Newest state: prepared-view handoff and sparse demand-resolved model ambient
are built, tested and image-compared. Verification uses `prepared-view.exe`.
No captures or samplers running at this checkpoint. The fresh short corpus is
still two ahead/six behind, with considerable timing noise; target NOT achieved.
Continue CPU work; do not yield unless requested or included usage reaches 0.
Last usage check 33% remaining. Exact evidence is at the top of
`renderer-cpu-ownership-repair.md`. Main checkout/Candidate/Previous untouched.
Next: separate frame-constant GPU tables from per-view stream uploads; profile
reflection dependency traversal. Use longer paired timings for contested gains.

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
