# Renderer recovery checkpoint — 2026-09-22

## Active 2026-10-21 flare regression investigation

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
