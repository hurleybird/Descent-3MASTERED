# GPU bottleneck investigation — November 6

## November10: reflection-interface capability investigation

speccost1110 disabled specular entirely, ONLY a cost diagnostic:
0011.566/1.500,1.558/1.498 with24717changedpixels;0071.092/1.046,
1.078/1.067 with exact image. Never retained as an optimization.
Metadata-safe whole-view specialization diffuseview1110 was exact:
0011.553/1.543,1.551/1.546;0071.082/1.058,1.073/1.0555.
Then tested ordered diffuse/specular subdivisions. All12 timing pairs and all8
dr-fx images exact, but native001 .622/.638,.623/.639 regressed. Subdivision
removed: reducing shader work did not repay extra pass/program switching.

Current capability union over EXISTING passes preserves grouping and order.
dpnative001 .623/.619,.622/.621;007 .419/.411,.420/.410;
dpssaa0011.547/1.5355,1.5505/1.5485;0071.082/1.047 twice;
dpfull0013.2645/3.2255,3.252/3.259 (neutral/noisy),0072.6345/2.609,2.634/2.612.
All12 pairs exact. Benefits are small, mostly007; not a baseline victory.
Final default-enabled qualification passed; reference flag
-scene-general-specular-reference. No new render targets or per-frame programs.
Root3/3; diffusepass-fx all8 exact vs bloomearly-fx, aux003/006 exact,
image-settings and gameplay reload003 pass. Entirely diffuse views now select
the diffuse program as primary, avoiding setup/compilation of an unused general
program. Final dpprimary-fx all8 exact, auxiliary003/006 pass.

CPU cycles were variable, not a proven regression or gain. For final primary
selection, five paired007 runs (dpprimary rounds1/2 plus dpvariance1/2/3) have
median-of-run-medians control3663906 vs diffuse3670473.5 cycles (+.18%). GPU
consistently .419-.420 vs .410-.412ms. Do not cherry-pick the initial noisy
dpcpu0073.810M/4.624M or favorable dpprimary001 runs as a CPU conclusion.

## November10: brightness-first bloom qualified

cellbounds1110 tested RG32F min/max depth per particle reconstruction cell,
computed once/view. Same edge threshold and samples, all4 image pairs exact.
SSAA2/noeffects reference/probe0011.5665/1.5795,1.556/1.561;
0071.101/1.103,1.083/1.098. Extra compute/storage outweighed fewer composite
fetches. Entire probe removed; no new intermediate resource retained.

Separate half-display-resolution smoke probe rejected as a default. At SSAA2
it saved roughly .13ms in full effects, with small actual image changes.
At SSAA4 full effects hv-s4 reference/half0019.6955/9.9165ms (regression),
0077.827/7.701ms. Coarser reconstruction also widens full-resolution repair
coverage; less shading in the layer is not necessarily less total work.
All8 effects pairs and moving001 captures completed, but no equal-quality claim.
Optional probe source removed; normal smoke resolution unchanged.

SampleBloom now computes the unchanged source threshold first. Zero-contribution
taps need no depth, alpha-occlusion or protection lookup. All13 taps, weights,
formats, gamma and mask behavior retained. Shared BloomRadiance avoids duplicated
threshold arithmetic. Capture-only -scene-bloom-mask-first-reference compares
the previous ordering. Scalar power shortcut rejected as negligible extra gain.

bloomq reference/early GPU medians (720p,SSAA2,MSAA4,AO,bloom,motion blur):
0001.850/1.764;0013.353/3.2715;0022.6695/2.589;0032.161/2.071;
0041.9145/1.831;0051.761/1.679;0061.9435/1.8625;0072.729/2.647ms.
All8 paired images exact; bloomearly-fx all8 exact against aocache-fx.
Root3/3; image-settings and auxiliary003/006 lifecycle runs pass.
This saves .0805-.090ms total GPU, not enough alone for overall baseline victory.

## November10: rejected fog gather and low-resolution particle depth probes

Additional measured rejections: infubo1110 uniform16KiB influence-index prefix
reference/probe0013.385/3.3725,3.363/3.370;0072.732/2.744,2.729/2.7325.
Draw-uniform-only infdraw1110:0013.3295/3.360,3.332/3.3625;
0072.7095/2.724,2.718/2.723. All8 images exact, no repeatable gain.
packdepth1110 matched packed D32FS8 snapshot storage for direct image copy:
SSAA2/noeffects0011.5515/1.571,1.552/1.5705;0071.076/1.117,1.089/1.1135.
Slower with extra memory; removed. Packed-depth blit was NOT tested.

Additional rejected probes (all experimental source removed):
volcolor1110 dropped unused motion/identity/protection exports from specialized
SoftVolumes shader; all4 images exact, neutral/slower: reference/probe
0013.3335/3.331,3.3325/3.3375;0072.710/2.716,2.7155/2.720.
fusedfg1110 fused final SSAA foreground reduction and source-over, explicitly
rounding to RGBA8 before blending. Reference/probe0013.3055/3.302,3.320/3.294;
0072.709/2.692,2.7055/2.695. Small gain but6605 differing pixels in001; no new
blend/reduction policy retained. Would require rounding/format investigation
and broader proof, not a claim of exact equivalence.

foggather1110 round1/2 reference/probe0013.3625/3.3645,3.359/3.3565;
0072.736/2.728,2.725/2.727. Replaced3D field storage with2D-array slices and
two XY gathers plus softwareZ interpolation (same scalar field contents and
contrast/refinement rule). Neutral; changed2916/668 pixels from interpolation
rounding, same pattern as earlier explicit-Z-fetch probe. Entire probe removed.
Khronos textureGather reference confirms gather component order (0,1),(1,1),
(1,0),(0,0): https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/textureGather.xml

voldpth1110 round1/2 reference/probe0013.3655/3.381,3.3435/3.380;
0072.728/2.7655,2.7155/2.7635. Downsample opaque depth into existing particle
scratch depth once/view, enable early depth testing for zero-depth-offset
soft volumes. All4 images exact, but added copy cost outweighs avoided shading.
Entire probe removed; no extra depth copy retained.

## November10: qualified quality-keyed AO shader cache

AOResources owns generic plus3 retained sampling patterns. Exact loop bounds
specialized for <=128 sample patterns; larger patterns keep generic shader,
without truncation. Warm frames allocate/compile nothing. Context destruction
destroys every cached program. Reference -scene-ao-dynamic-loop-reference.
Capture pattern probe traverses1,4,12,24,32,64,128,256,1024,32,16,32 settings,
exercising eviction, fallback and return to prior patterns.

aocache1110-{1,2}-{reference,eligible}-savegNNN-20261107 GPU medians:
0013.3905/3.330,3.3615/3.3115;0072.766/2.7145,2.764/2.710 ms.
All4 images exact. Root3/3; aocache-fx-20261110 all8 exact vs unlitworld-fx;
aux003/006 exact; image-settings passes; all12 pattern snapshots exact vs generic
and GL-audited. No changed sample locations/counts, AO parameters or resolution.

Fresh gpu-mx-*-aocache-20261110 old/new (rounded):001native.74/.62,
SSAA2/noMSAA/noeffects1.36/1.54,full3.19/3.33;007native.40/.42,
SSAA.89/1.08,full2.83/2.71. Nativeold001 still varies; do not overclaim it.
No overall victory. Remaining SSAA gap exists without AO and requires world/
particle work, not further attributing it to AO. Baseline effects differ as
previously documented; reconstructed baseline is not the deleted original.

## November7 checkpoint: particle stages and AO loop investigation

Accepted production changes through31cc21a remain. This slice adds only
capture-gated particle stage timestamps; no ordinary-frame query/readback or
allocation. All timestamps are queued before any results are read after view
submission. Flag: -scene-particle-stage-audit.

particle-stages-saveg001/007-20261107, capture120, full effects:

| Scene | Low-resolution shading | Composite/classification | Edge repair |
|---|---:|---:|---:|
|001|.353760 ms|.126160 ms|.353360 ms|
|007|.194160 ms|.123800 ms|.139880 ms|

These are diagnostic capture intervals, not whole-frame medians. Explicit early
depth/stencil tests on read-only SoftVolumes were neutral: earlyparticles round1/2
reference/probe0013.325/3.331,3.348/3.344;0072.753/2.747,2.7485/2.748.
All4 images exact. Probe removed. Confirms the earlier explicit-early-tests
finding; do not repeat it without a changed pipeline or new evidence.

AO static-loop probe replaced uniform bounds with constants, preserving the
benchmark's32 samples (8 directions x4 steps), locations, and arithmetic. Guard
rejected other settings. aostatic32 round1/2 reference/probe GPU medians:
0013.340/3.333,3.401/3.354;0072.748/2.711,2.769/2.7215. All4 images exact.
This is a promising next lead, not an accepted general implementation. Removed
fixed-setting override before checkpoint. Next: bounded specialization by actual
quality pattern with safe generic fallback and settings/context-lifetime tests;
qualify all8 before accepting. No fewer samples or changed sample pattern.
Initial aostatic-* used wrong128 assumption, guard rejected it; incomplete run
is INVALID evidence. Actual default32 is Descent3/init.cpp605.

User requested natural-checkpoint yield/push. No refreshed baseline matrix or
overall GPU victory claimed in this slice. Final build retains production
behavior and capture-only diagnostics; experimental shader overrides removed.

Investigation, not a shipped optimization. Production source remains at 075ae88;
temporary diagnostic modes were removed afterward. Candidate and Previous
Candidate were not changed. No instancing implementation was started.

## Method

AMD Radeon RX 6600, driver 32.0.21045.16010. Hidden, input-suppressed private
runtime; one game at a time, no concurrent compilation. 1500 deterministic
frames, median GPU elapsed intervals over frames 400..1199 (800 samples), main
thread pinned to CPU2, uncapped. Default 1280x720, 1xSSAA, no MSAA/AO/bloom,
headlight enabled. GPU elapsed intervals are not hardware utilization counters.
No CPU/GPU time addition. Capture-frame pass timestamps are diagnostic samples,
not the 800-frame medians. All evidence below is under
`C:/tmp/D3RendererVerification`.

Baseline is the reconstructed bfb59647 executable, NOT the deleted original.
It used the matching baseline-pre-rewrite-assets-20261106.hog archive. Runtime
swaps restored the current executable and archive after baseline tests.

## Confirmed resource-binding fault in the mirror scene (save006)

Resolution sweep, new versus reconstructed old GPU ms:

| Resolution | New | Old | Difference |
|---|---:|---:|---:|
| 640x360 | .545 | .221 | .324 |
| 1280x720 | .676 | .327 | .349 |
| 2560x1440 | 1.146 | .786 | .360 |

Evidence: gpu-investigate-{new,old}006-{640,1280,2560}-20261106.
Mostly fixed extra interval; this alone did not identify a cause.

Controlled isolation (gpu-isolate006-*-20261106):

| Diagnostic | GPU ms |
|---|---:|
| Normal control | .674 |
| Rasterizer discard, full shader still linked | .4165 |
| Constant fragment shader (also prunes unused vertex outputs) | .087 |
| Tiny fragment consumer retaining every vertex output | .157 |
| Same consumer plus rasterizer discard | .080 |
| Consumer plus fragment draw/view UBO dependencies | .168 |
| Consumer plus original output attachment declarations | .1565 |
| Full shader with uniform-controlled early return | .499 |
| Early return plus rasterizer discard | .417 |
| Early return, planar sampler redirected to base-image unit | .116 |
| Early return, another sampler redirected (lightmap/specular/AO/depth) | .4985–.501 |

Thus the large interval cannot be attributed simply to draw count, varying
count, fragment math, or output count. The decisive variable was the image
referenced by the planar sampler.

Capture audit in early-alias-depth (planar binding unchanged): reflection-view
opaque and late passes had unit10 texture28, size1280x720, while the current
color attachment was ALSO texture28. The sampler inherited the previous
view/frame's mirror input while that image was being rendered again. The
explicit bind_view_image self-target check only runs when binding a requested
view image; it does not catch this inherited binding. Reflection views don't
execute planar sampling, but the shader still declares that sampler.

Permanently redirecting the sampler was NOT accepted: it replaced the mirror
with its base texture (294717 differing pixels). This was an isolation test,
not a valid performance improvement.

The valid experiment cleared texture unit10 at view entry, immediately before
the pass loop, restoring active unit0 afterward. Normal bind_view_image calls
still supplied the actual mirror when needed. Nothing else changed.

| Setting | Control GPU ms | Clear inherited binding | Pixel differences |
|---|---:|---:|---:|
| 1xSSAA, no MSAA, repeat A | .672 | .201 | 0 |
| 1xSSAA, no MSAA, repeat B | .672 | .201 | 0 |
| 2xSSAA, no MSAA | 1.035 | .559 | 0 |
| 1xSSAA, 4xMSAA | .267 | .266 | 0 |
| 2xSSAA, 4xMSAA, AO+bloom | 1.995 | 1.998 | 0 |

Evidence: gpu-isolate006-{clear-a,control-d,clear-b}-20261106;
gpu-binding-{ssaa2,msaa4,effects}006-{control,clear}-20261106.
The plain captures were visually inspected; mirror and reflected ship remain.
MSAA audit shows unit10 texture30 versus color attachment object2, not the
same image. This explains why the fix has no benefit in that configuration.
Without MSAA the corrected experiment beats the old .327ms GPU measurement.
This is a binding-related GPU/driver penalty. Its exact internal mechanism
(barriers, decompression, validation, etc.) was NOT established by counters.

## Other regression scenes have a different bottleneck

Clearing unit10 preserves every captured pixel but does not improve saves000,
001,003,007: respectively .504→.504, .934→.934, .425→.426,
.5175→.517ms. Evidence gpu-binding-savegNNN-{control,clear}-20261106.

| Scene | Control | No fog application | No boundary refinement | Rasterizer discard |
|---|---:|---:|---:|---:|
| save000 | .504 | .458 | .496 | .329 |
| save001 | .934 | .6695 | .849 | .331 |
| save007 | .5175 | .387 | .484 | .215 |

These destructive shader experiments retain fog-field generation. They are NOT
quality-preserving fixes and were removed. Evidence gpu-binding-savegNNN-
{no-fog,no-refine,discard}-20261106. Differences are not additive cost accounting:
compiler optimization, overlap and downstream work can change with each probe.

Final capture-frame timestamps in the control runs:

| Scene | Fog field total | Tile setup | Integration | Opaque world | Late world |
|---|---:|---:|---:|---:|---:|
| save000 | .2406 | .0217 | .2189 | .1364 | absent |
| save001 | .2306 | .0137 | .2169 | .1663 | .4325 (24 items) |
| save007 | .1285 | .0115 | .1170 | .1203 | .1751 (10 items) |

Field generation is a substantial fixed cost. In save001/007, fragment work in
the late pass is also substantial. Boundary refinement is only part of that
cost; disabling it does not eliminate the gap and would sacrifice tested edge
quality. Historical Nov3 old GPU medians were .321/.7125/.400ms for000/001/007;
those are NOT fresh paired baselines in this investigation.

## Evidence-led next steps

1. Correct the per-view resource-binding contract: a view must not inherit its
   output image as a live shader input. Preserve actual mirror dependencies.
   Audit the other optional sampler slots and auxiliary views, rather than
   adding a mirror-scene special case or clearing dozens of units per draw.
2. Measure fog integration work and late-pass fragment work separately over
   steady frames. Test work reduction without reducing field resolution or
   dropping the boundary refinement that removed visible aliasing.
3. Re-run matched old/new comparisons and visual/effects/lifecycle cases after
   a production correction. No claim that all GPU baselines are now beaten.

Instancing is not justified as the next response to the mirror regression:
the same draw submissions reached .201ms once their binding hazard was removed.

## Follow-up after production binding fix 4e75b3b

The binding correction and capture-only input/output alias check are now
implemented. See the recovery checkpoint for image/lifecycle verification.
The following fog experiments were diagnostic only; all probe code was removed.

Workgroups8/16/32/64 retained every image pixel but provided no consistent win.
Evidence perf-fog-lanesN-savegNNN-20261106. Increasing storage from16 to32 bits
raised field integration000 .218→.362ms and001 .219→.333ms; scalar007 stayed
.117→.118ms (capture-frame timings). Evidence perf-fog-full-storage-*.

Instrumented crossing counts, diag-fog-events-savegNNN-20261106, 60 frames:

| Scene | Columns | Columns intersecting any volume AABB | Boundary crossings | Maximum crossings/column |
|---|---:|---:|---:|---:|
| 000 | 57600 | 2233 | 3614 | 14 |
| 001 | 57600 | 57600 | 89964 | 5 |
| 007 | 57600 | 57600 | 74122 | 9 |

The counter probes use GPU atomics; their timings are NOT performance evidence.
For000, roughly96% of columns cannot contribute volume fog, yet the normal
kernel serially writes all65 prefix texels for each of these empty columns.
Exterior fog is separate and is not omitted by this classification.

Two measured alternatives, normal-shading1500-frame runs, same800-frame median:

| Scene | Normal | Bulk clear + omit empty-column stores | Validity-plane sparse columns |
|---|---:|---:|---:|
| 000 | .503 | .6715 | .419 |
| 001 | .932 | 1.167 | .986 |
| 007 | .516 | .585 | .540 |

Bulk clear used glClearTexImage with the texture-update barrier. The operation
is already available through the loader. The API and barrier semantics were
checked against https://registry.khronos.org/OpenGL/extensions/ARB/ARB_clear_texture.txt.
It preserved all pixels but lost performance; discard, don't promote.
Evidence perf-fog-bulk-clear-savegNNN-20261106. Field timing includes the clear.

The sparse experiment retained64 integration slices and exact boundary
refinement, appended one validity plane, skipped empty-column prefix writes,
and masked invalid columns during the four-tap field sample.000 remained
pixel-exact;001/007 each differed at one pixel (normalization changes when the
physical depth is66 instead of65). More importantly, the read-side cost regressed
dense fields. Evidence perf-fog-sparse-savegNNN-20261106. Not accepted as default.

Next investigation should move column occupancy/work ownership upstream so
empty columns don't require dense writes, while dense views don't pay sparse
sampling overhead. Do not hardcode save numbers, lower field resolution, or
drop refinement. No new fog representation has been qualified or shipped.

## Upstream correction: portal admission for room-owned fog

Fog previously used frustum/AABB admission alone. Geometry already computes a
conservative portal-visible room set; fog ignored it. Diagnostic visibility
captures showed every candidate in save000 belonged to hidden rooms. Save001
also admitted a hidden two-room, differently colored volume. Entire connected
volumes now require at least one portal-visible member room. Unknown, unowned,
or incomplete room metadata stays admitted. No boundary faces are clipped away.

1500-frame runs, median GPU over frames400..1199,720p,1x,noMSAA/AO/bloom:

| Scene | Frustum-only | Portal admission |
|---|---:|---:|
| 000 | .504 | .167 |
| 001 | .929 | .813 |
| 007 | .513 | .513 |

Evidence perf-fog-portals-savegNNN-{control,enabled}-20261106.000/007 pixel-exact;
001 field differs slightly as its range shrinks833.975 to122.049 and becomes
single-color R16F. Against exact fog, mean RGB error improves .369 to .150 code
values; pixels exceeding2 codes fall .305% to .0243%. Exact fog before/after
admission is pixel-identical. Capture-only -scene-fog-all-volumes-reference
keeps this independent comparison available without changing normal behavior.

Verification: root3/3 tests; seven admission cases added. All8 clean exact
captures match qual-view-inputs-clean-20261106. All8 effects captures pass;
000/003/004/005/006 exact,001/002/007 small field-range differences (peak3/3/2).
Matched exact/reference effects captures001/002/007 all pixel-identical:
fog-admit-savegNNN-{exact,ref,field}-fx-20261106.

Moving roof fixture,1920x1080,2xSSAA,4MSAA,AO/bloom,frames150..153,continuous
ceiling source183<-184: exact/reference all four images pixel-identical.
Field final-frame mean error against exact improves .203 to .163 codes;
pixels exceeding2 codes fall .00627% to .00149%, peak3 either way. Inspected
the headlight-on result; no visible new boundary defect. Evidence
fog-admit-roof-{field,fieldref,exact,ref}-20261106. This is not exhaustive
temporal qualification. Auxiliary lifecycle003/006 with motion blur is exact
against qual-view-inputs-auxiliary-20261106; gameplay roundtrip003 all8 images
exact against qual-view-inputs-roundtrip-20261106. Evidence fog-admit-aux-mb and
fog-admit-trip-20261106. The earlier aux run omitted motion blur and is NOT a
matched comparison.

An intermediate sparse tile-coverage experiment improved000 .510 to .403ms
but needed new sampler/storage/variants. Discarded: portal admission is simpler
and removes the entire unnecessary field, reaching .167ms. No sparse sampling,
lower resolution, reduced refinement, or per-scene rule is retained.

One failed capture used an output filename longer than the game's99-character
argument limit; exclude qual-fog-admission-saveg001-exact-reference-effects
from evidence. Short-path reruns above succeeded. Candidate/Previous Candidate
remain untouched. Dense001/007 still need GPU investigation; these results do
not establish a win over the pre-rewrite renderer on every scene/settings.

### Refreshed pre-rewrite comparison and late-draw isolation

gpu-admit-{old,new}-savegNNN-20261106,1500frames,median400..1199,
720p,1x,noMSAA/AO/bloom,headlight,render thread CPU2. Old is reconstructed
bfb59647 with its matching archive, not the deleted original executable.
Current executable and archive were restored after the sequential runs.

| Save | Reconstructed old GPU ms | Current GPU ms |
|---|---:|---:|
| 000 | .3190 | .1680 |
| 001 | .7370 | .8090 |
| 002 | 1.4625 | .4815 |
| 003 | .5900 | .2700 |
| 004 | .3470 | .1640 |
| 005 | .2390 | .1380 |
| 006 | .3280 | .2000 |
| 007 | .4030 | .5150 |

Six of eight improve;001/007 still regress. This is NOT an effects-scaled or
strict image-equivalence comparison between the two renderers.

Capture-frame per-draw timestamps (queued, read only after view submission)
localize001 late-pass .41488ms to24 sprites, bitmap2923..2931;007 .17576ms to10
sprites in the same range. Largest001 draws cost .048-.054ms apiece. Evidence
gpu-late-saveg{001,007}-20261106. Shader-cost isolation removed surface lighting
and specular code globally, retaining fog/soft-depth/material sampling. This
INVALIDATES opaque appearance; it is NOT a proposed implementation. The late
pass only improves to .37484/.15720ms (~10%). Thus lightweight unlit shaders
alone cannot eliminate the remaining cost. Evidence gpu-unlit-*. All temporary
per-draw and destructive shader probe code removed after measurement.

### Compact 2D fog compute groups

Keep each invocation's exact column integration, but group8x4 adjacent columns
instead of8x1. This uses16KiB shared event storage, no new resource, and no
sampling change. The producer's existing32-thread row experiment did not show
this gain: 2D layout matters on the measured GPU. Mechanism is inferred from
the layout experiment; no hardware cache/occupancy counter claim is made.

First gpu-{control,square}-saveg{001,007}-20261106:
001 GPU .800→.780ms;007 .515→.502. Reversed-order repeat
gpu-repeat-{group,row}-savegNNN-20261106:001 .800→.7805,007 .515→.503.
Capture-frame integration001 .15064→.13340ms;007 .11760→.10556.
Root3/3 pass. All8 effects/MotionBlur captures pixel-identical to the admission
candidate (fog-group-fx-20261106). Roof motion150..153 all four frames exact
between groups (fog-roof-{group,row}-20261106). Odd311-wide field with full
precision is exact (fog-odd-*). All controls use the capture-only
-scene-fog-row-group-reference flag; ordinary execution uses8x4. Small but
repeatable gain, not a solution to all remaining dense-scene overhead.

### Effects scaling and particle edge repair

gpu-fx-{new,old}-saveg{001,007}-20261106, same1500-frame median window,
720p output,2xSSAA,4MSAA,AO,bloom,motion blur. Current0014.7395ms versus old
3.212;0073.3025 versus2.822. Effect implementations differ; not strict visual
equivalence. New particle passes cost1.61968/.63652ms on the capture frame.
Native-particle reference is slower: total4.830/3.557, passes2.0514/.88004.
Disabling edge repair (INVALID quality reference) drops totals3.426/2.9445,
passes .65044/.31364. Evidence gpu-native-fx-* and gpu-noedge-fx-*.

Tested once-per-view R8 edge classification as an explicit schedule product.
It preserved every pixel but costs~.066ms itself. Mask/reference totals
0014.6505/4.6825 and0073.318/3.306; too small/inconsistent to justify a new
resource/pass. Evidence gpu-edge-{mask,ref}-*. Removed the complete experiment,
including schedule/tests/resources. Do not revive it as an established win.

Next diagnostic: the old edge predicate compares depth range, so even one
steep planar surface may trigger expensive full-resolution repair. A planar
depth residual test can distinguish such gradients from discontinuities.
This changes reconstruction choices and requires native-reference quality and
temporal checks, not just timing. No appearance change accepted from this yet.

Planar residual probe rejected: matched0014.4585→4.3265ms,0073.298→3.303.
001 mean RGB error against native worsens .2992→.3235, peak4→5;007 essentially
unchanged. Evidence gpu-plane-{control,planar}-*. Probe source removed and
production rebuilt/root3/3 passed. Boundary audits show001 rooms26/27 and007
room81 are non-convex; do not substitute a same-room uniform-fog shortcut.

## RenderDoc hardware-counter investigation

Installed RenderDoc1.29, hidden embedded-Python capture/replay, AMD RX6600.
One89MB capture: C:/tmp/D3RendererVerification/rdoc-gpu001_frame1500.rdc.
This is RenderDoc's present number1500, NOT the earlier game-frame1500 baseline.
Replay timings/counters identify work; they are not ordinary-frame medians.
rdoc-counters.json has counter descriptions, rdoc-counter-values.json values,
rdoc-actions.json actions, rdoc-events.json resources, rdoc-shaders.json sources.
26 counters collected across replay; don't add busy-cycle counters or treat
separate collections as perfectly synchronized. Helpers are out/rdoc_*.py.

Main scene has AO overscan2740x1542,4MSAA, logical1370x771. Selected events:
-163 fog integration: ~.14ms,58880 CS invocations,6.78MB writes,0 LDS bank
 conflicts. Memory-unit busy337022cycles vs349845CSbusy; memory stalls14918.
 This does not prove external-memory bandwidth saturation.
-1245 AO sampling: ~.34ms,1056270 pixel invocations; texture unit busy,
 low memory stalls. Output RG16F1370x771.
-2359 nearest-depth resolve: ~.22ms,4225080 invocations,12.32MB fetched,
16.92MB written, substantial memory stalls. Output depth only.
-2653 particle repair: ~.10ms,3558829 pixel invocations,484700 samples passed,
16.39MB fetched.2668:~.07ms,3483703 invocations,ZERO passing samples,
15.12MB fetched. Pixel export stalls are zero; repeated shader-side rejection
 and sampling are real work, not just blend-output bandwidth.
-2858 post-protection mask MSAA resolve:~.41ms, output RGBA8 full scene extent.
-2902 bloom threshold:~.24ms,263725 invocations,38.03MB fetched and high
 memory stalls; shader performs13 thresholded taps, each consulting protection,
 depth, and alpha-occlusion inputs. Protection remains full SSAA resolution.

Next evidence-led targets: protection-mask format/resolve and its consumption,
and reducing particle repair invocations rather than another per-pixel cache.
No new source experiment retained from this investigation yet. Capture helper
and replay processes exited; no game/mouse capture left running.

## November7: scalar bloom-protection contract

Separate storage probes, gpu-mask-{control,rg,texture,both}-savegNNN-20261107:
RG8 instead ofRGBA8 saves~.08ms in001/007, images exact. MSAA texture storage
instead of renderbuffer alone provides no meaningful gain. Both probes removed.

The live scene writes only fog protection; surface class already belongs to
SceneSurfaceResources. Removed the unused Begin/EndTerrainSurface API (no game
callers), its state, and terrain reconstruction branches/parameters in bloom.
Bloom composition no longer obtains/binds a depth input solely for that branch.
Protection is now scalar R8, producer/consumers consistently useR; same8bit
precision, same sample count and resolve. Capture-only
-scene-mask-rgba-reference isolates storage without changing scalar semantics.
Net~130 source lines removed; no new resource or pass.

Same-binary1500-frame medians400..1199, effects2xSSAA/4MSAA/AO/bloom/motion:
gpu-scalar-{r8,rgba}-savegNNN-20261107:0014.4865 vs4.653;0073.151 vs3.2905ms.
Do not subtract these from older runs with clock/thermal drift as if paired.
Root3/3 pass. All8 effects60 images EXACT fog-group-fx-20261106. R8/RGBA1500
images exact; separate save002MSAA0/8 references exact. Auxiliary003/006 and
gameplay roundtrip003 all PNGs exact against fog-admit-aux-mb/trip references.
Evidence mask-r8-{fx,aux,trip}-20261107 and mask-{r8,rgba}-msaa{0,8}-20261107.
Candidate/Previous Candidate/main checkout remain untouched.

## November7: follow-up rejection and depth-culling probes

All timing pairs below are 1500 frames, median400..1199, effects2xSSAA/4MSAA/
AO/bloom/motion. Same binary/reference switch; no retained quality reduction.

- Particle alpha+soft-depth rejection before edge classification was slower:
  gpu-reject-*0014.5475 vs4.3075ms, images exact. Rejected.
- Cheap alpha rejection alone: gpu-cutout-*0014.2865 vs4.2955,
  0073.161 vs3.1445. Images exact; no useful consistent gain. Rejected.
- Bloom color-threshold rejection before depth/protection reads:
  gpu-bloom-*0014.307 vs4.3055,0073.077 vs3.076. Images exact; neutral.
  Rejected. The sampled mask bandwidth alone does not establish that changing
  its read order will improve throughput.
- Explicit early fragment tests, diagnostic across the whole scene shader:
  gpu-depth-*0014.5565 vs4.5945,0073.139 vs3.163. Images exact in these two
  frames, but this is NOT generally valid for depth-writing alpha cutouts.
  Small total gain does not explain the large repair cost. Removed.

Additional counters from the existing RenderDoc capture, rdoc-depth-values.json:
repair events2653/2668/2678 have zero HiZ tiles rejected and zero quads culled.
Event2668 has3483703 pixel invocations and zero passing samples. Actual pipeline
state confirms depth enabled, LEQUAL, writes disabled. This alone cannot tell
how much rejection is due to scene occlusion versus the shader edge predicate;
the forced-early test above does not show a large hidden-geometry saving.
Next investigation is hardware rejection of non-edge pixels rather than
reordering texture reads in a shader that still runs over every particle quad.

## November7: stencil classification for particle repair

The stencil predicate is exactly the existing five-depth-sample predicate, with
the same scissor, mapping and complementary layer composite. Classify once per
soft-volume run, then stencil-test its repair draws; no sampling-quality change.
Framebuffer now declares optional multisample stencil storage. Only supersampled
world targets request it. Raster depth becomes D32F/S8; resolved depth stays D32F.
If packed storage is unavailable, retry depth-only at the SAME MSAA count before
considering the existing lower-sample fallback. The shader predicate remains the
fallback and capture reference (-scene-particle-shader-repair-reference).
No per-frame allocation, readback, new image product, or additional resolve.
At2740x1542/4MSAA the wider raster attachment costs~64.5MiB nominal extra storage;
there is one world framebuffer. Native-resolution targets do not request it.

Rejected setup alternatives: standalone S8+existingD32F attachment returns
FRAMEBUFFER_UNSUPPORTED onRX6600. Changing every framebuffer's depth format
breaks depth-copy format compatibility. A temporary packed repair target with
per-sample depth copy is exact but costs too much: gpu-stencil2-*0014.2755 vs
4.313,0073.493 vs3.153ms. All temporary target/copy code removed.

Two paired sweeps show the in-place multisample version is useful. gpu-stencil3-*
0013.824 vs4.2985,0073.045 vs3.152ms. All8 repeat gpu-st4-{normal,stencil}-*:

| Save | Shader repair ms | Stencil repair ms |
|---|---:|---:|
|000|1.898|1.897|
|001|4.3555|3.8525|
|002|2.944|2.929|
|003|2.197|2.195|
|004|1.956|1.958|
|005|1.813|1.811|
|006|2.014|2.013|
|007|3.154|3.066|

All8 paired images exact. Production stencil-fx-20261107 all8 images exact
against mask-r8-fx-20261107; auxiliary003/006 and roundtrip003 all8 PNGs exact
against mask-r8-aux/trip. st-q-* coversSSAA2/MSAA0,2,8 andSSAA4/MSAA4: exact
shader-reference images, GL audit enabled. Root3/3 passes. Dense image inspected.

### Important profiler observer effect

The first counter comparison misleadingly suggested stencil did NOT reduce pixel
invocations. Isolated counter-set tests on the SAME captured frame explain why:
requesting GL_SAMPLES_PASSED changes this driver's automatic early-test behavior.
With duration+pixel-invocations ONLY, original2653→stencil2679 goes3558829→171562;
2668→2694 goes3483703→171556;2678→2704 goes2793979→171382. Adding only the
samples-passed query restores the high invocation counts. Replay draw durations
also change. These counter-instrumented timings are NOT ordinary-frame timings.
Evidence rdoc-{gpu001,stencil,early}-lean-values.json and
rdoc-stencil-occlusion-values.json, using existing captures rather than new games.

A separate read-only early_fragment_tests shader variant was tested to check
this. Normal-run st-early-*001 early3.863/automatic3.855/original4.3295,
0073.077/3.075/3.158; exact images. Without occlusion queries, automatic and forced
early tests have IDENTICAL invocation counts. Extra variants/source removed.
The simpler production path already receives early rejection in ordinary runs.
Forced-early samples-passed counts differ by definition because shader discards
occur after those tests; do not treat that counter as final visible coverage.
References: https://wikis.khronos.org/opengl/Early_Fragment_Test and RenderDoc1.29
https://raw.githubusercontent.com/baldurk/renderdoc/v1.29/renderdoc/driver/gl/gl_counters.cpp
(FillTimers starts requested generic queries together around each draw).
Three bounded~90MB captures retained: original, stencil, forced-early; no helpers
or games remain running after their respective captures/replays.

Final production rebuild/root3/3; st-final-* repeats0014.637→4.050 and
0073.158→3.0545ms, exact images. Clock drift exists between suites: use matched
pairs, do not subtract across suites. Four st-roof-* motion frames150..153 are
exact against shader repair at1920x1080/SSAA2/MSAA4/AO/bloom, including the accepted
continuous ceiling fixture. No experimental early-test variants remain.

## November7: sprite footprint and soft-depth rejection

Read back nine sprite textures from the existing capture (no new RDC), including
all five mip levels. Base64x64 alpha occupies675..2391 of4096 texels, but bounds
expand at lower mips; naive base-level geometry trimming is not safe with the
existing filtering contract. No sprite geometry/filtering changed. Evidence
rdoc-particle-footprint.json; helper out/rdoc_particle_footprint.py exited.

Retained a smaller exact optimization: inside the soft-depth path, reject an
already-cut-out texel before the depth fetch. Intersection fading is in[0,1], so
it cannot rescue rejected alpha. The existing final cutoff still handles fading.
This covers the logical particle layer and native rendering, unlike the earlier
rejected experiment that moved cutoff ahead of edge classification only.
Capture reference -scene-soft-alpha-late-reference; no additional resources.

soft-a-* early/lateGPU001 effects3.8585/3.902,clean.771/.788;
007 effects3.0645/3.077,clean.498/.503. Reversed-order soft-b-*:
001 effects3.808/3.845,clean.776/.786;007 effects3.051/3.065,clean.498/.503.
All initial pairs exact. soft-alpha-fx-20261107 all8 exact against stencil-fx.
This is a small~5–44us improvement, not a solution to the remaining GPU gap.

Rejected lazy-ray setup probe: defer direction/inverse/boundary setup until the
field actually requests refinement or exterior fog. fog-ray-* initially improves
001 clean.771→.753/effects3.830→3.793,007 clean.500→.489/effects3.0505→3.0315.
But reversed-order fog-ray2-*001 regresses4.062→4.205 (0073.068→3.0435).
Insufficiently reliable performance case; ALL probe source removed. All8 effects,
four roof motion frames and exact001/002/007 images are unchanged. Do not mistake
the first run's small gain for a qualified optimization.

## November7: consolidate particle classification into its composite

The layer composite already discards precisely the repair pixels. Clear stencil
to0, mark drawn/non-edge pixels1 during that EXISTING composite, then repair only
stencil0. Removed the standalone classifier program, uniforms, full-screen draw
and repeated depth reads. Same predicate/coverage; no extra resource or shader
variant. The shader-repair reference remains available.

Encoding matters on this GPU. First attempt cleared1/wrote0/repair1 and was
slower (st-combo-*0013.9205 vs3.8655,0073.104 vs3.074). Clear0/write1/repair0
improves st-combo0-*0013.8305→3.7675,0073.055→3.000. Three-way same-binary
st-enc-* confirms separate/clear1/clear0:0013.817/3.849/3.7685,
0073.0415/3.0615/2.988. Images exact; do not infer speed from pass count alone.
Temporary clear1/separate diagnostic paths removed after comparison.

Final production root3/3. st-composite-fx-20261107 all8 exact against soft-alpha-fx;
st-cq-* SSAA2/MSAA2,8 andSSAA4/MSAA4 exact against shader repair withGL audit.
st-composite-aux003/006, trip003 all8 PNGs and roof150..153 exact against retained
references. Source reduction removes the classifier added in d9d9e20.

## November7: completed feature matrix and depth resolve reuse

gpu-mx-* (1500 frames, median400..1199) compares reconstructed bfb59647 old
exe AND assets with fdcd6ff. Not identical effects; not the deleted original exe.
720p old/new GPU milliseconds, rounded:

| Configuration | save001 old/new | save007 old/new |
|---|---|---|
| Native, no effects | .59/.77 | .40/.50 |
| SSAA2, MSAA0, no effects | 1.33/2.32 | .88/1.40 |
| SSAA2, MSAA4, no effects | 1.65/2.18 | 1.16/1.56 |
| Above + AO | 2.76/3.22 | 2.42/2.51 |
| Above no AO + bloom | 1.95/2.52 | 1.47/1.90 |
| AO + bloom | 3.08/3.64 | 2.72/2.89 |
| AO + bloom + motion blur | 3.16/3.74 | 2.81/3.00 |

Gap precedes AO/bloom. AO incremental cost is comparable or lower. Dense001
SSAA without MSAA is slower than with MSAA: stencil acceleration currently only
exists on multisampled depth. Next investigate format-aware depth copy to extend
stencil to single-sample SSAA, without aliasing the opaque snapshot.

SceneSamples GLSL now returns the winning depth alongside its sample index;
both depth-only and depth/velocity/identity resolves reuse it instead of fetching
again. Same strict-less tie selection, no resource/pass/precision change.
depth-{1,2}-{refetch,reuse}-* paired effects profiles:
001 3.7575→3.714 and reversed order 3.745→3.7095;
007 3.004→2.974 and reversed order 3.005→2.969 ms. All four images exact.
Root integration3/3 passed. depth-reuse-fx-20261107 all8 images exact against
st-composite-fx-20261107, including mirror/auxiliary save006.

## November7: stencil repair without MSAA

Framebuffer optional coverage now supports single-sample SSAA. Packed D32F/S8
cannot raw-blit to D32F, so CopyDepthRegionTo owns that conversion, reusing the
existing snapshot/export copy rather than adding a pass. Compatible plain D32F
and resolved MSAA sources retain their fast blit. Opaque snapshot remains distinct
from raster depth; nearest selection, D32F precision and ordering are unchanged.
Resource operations scope stencil and clip-plane enables as well as prior state.
Storage cost is nominal4bytes/rasterpixel extra on the single world target:
~16.1MiB at2740x1542, scaling with SSAA area. Native-resolution targets unchanged.

single-st-{1,2}-{shader,stencil}-* SSAA2/MSAA0/no-effects, median400..1199:
001 2.331→1.755; reversed2.331→1.754ms.
007 1.419→1.189; reversed1.414→1.197ms. Four images exact, GL audit passes.
single-check-* less-particle-heavy scenes000 .522→.522,0021.1315→1.098,
006 .566→.564ms; all exact. single-s4-* SSAA4 effects image exact.

single-fx-{old,new} all8 no-MSAA effects images exact; single-aux003/006 and
single-trip003 (all8 PNGs) exact. single-msaa4 all8 exact vs depth-reuse-fx.
single-settings image-settings passes dynamic SSAA/MSAA transitions/readback.
Root3/3 passed. Visually inspected dense001 headlight-on effects capture.
Reference executable for corpus is fdcd6ff (before depth-reuse, irrelevant to
single-sample output); same-binary flag isolates the timed stencil comparison.

## November7: continued investigation, rejected gather layout

Fresh bounded rdoc-refresh_frame1500.rdc retains low repair invocation counts:
events2666/2676/2686 are171562/171556/171382. However replay duration values
disagree substantially with ordinary in-game timestamps, even after two warm-up
collections. Do not use those replay milliseconds to rank the next optimization.
Capture frame1800 reports opaque.647, depth snapshot.188, soft volumes.960ms;
these are single-frame diagnostic numbers, not median benchmarks. No extra depth
resolve was demonstrated from the conservative end-view dirty mark; no change.

Tested 2D-array fog field with two scalar texture gathers instead of four3D
column samples. Same320-wide/64-slice field, boundary refinement and precision.
Gather ordering from Khronos GLSL4.60 specification:
https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html
fog-gather-{1,2}-{3d,gather}-* effects0013.7315→3.742 and3.7105→3.725;
0072.961→2.969 and2.9575→2.9655ms. Also changes2916/668 pixels in first001/007
captures from interpolation differences. Rejected: all source and reference
flag removed. Do not retain this representation change as an optimization.

## November7: enforce AO radiance eligibility before sampling

The shader set unlit surface_radiance to0 and excluded multiply materials from
the final AO correction, but still sampled AO and computed highlight suppression
and correction for those draws. Guard the complete AO block with unlit==0 and
multiply_material==0. Same visible contract, no extra resource or shader variant.
Temporary comparison macro/flag removed from final source.
ao-elig-{1,2}-{reference,eligible}-* effects medians:
0013.7115→3.6385 and reversed3.711→3.640;
0072.957→2.937 and reversed2.965→2.939ms. Four images exact.
Final root3/3 passed; ao-eligible-fx all8 exact vs single-msaa4;
ao-eligible-s4 SSAA4/MSAA0 effects exact vs single-s4-stencil, GL audit passed.

## November7: skip illumination discarded by unlit materials

Initialize illumination to white; only evaluate baked/directional/dynamic light
for lit materials. Previously those results were computed then overwritten with
white. Baked value remains available to the lit specular path. No precision,
lighting, fog or material-policy change. Temporary comparison flag removed.
unlit-{1,2}-{reference,eligible}-* effects0013.6275→3.580 and3.640→3.578;
0072.946→2.9185 and2.941→2.919ms. First-pair images exact.
Native clean unlit-clean-*001 .779→.746,007 .504→.487ms, both exact.
Final root3/3 passed. unlit-final-fx all8 exact vs ao-eligible-fx.

## November7: refreshed old/current comparison after retained fixes

gpu-mx-*-final-20261107 swaps both verified old exe/assets and current exe/assets.
720p medians400..1199, old/new (rounded):001 native.68/.75,
SSAA2/MSAA0 no-effects1.35/1.72, full effects3.22/3.78;
007 native.40/.49, SSAA2/MSAA0 .89/1.18, full2.82/2.93.
Repeat full-only gpu-mx-*-repeat-20261107 gives0013.19/3.63,
0072.84/2.93. Dense final image is identical to prior unlit-2-eligible image.
Do not attribute the dense3.78 outlier to a new visual/workload change, or add
timing gains from different sessions. The new renderer still DOES NOT beat the
reconstructed old renderer on these GPU cases. Effect implementations differ.

Further candidate, not implemented or proven: SoftVolumes is an explicitly
unlit pass but still links the general lit program/varying interface. Specializing
its program might reduce interpolation/register costs. First measure; don't add
per-draw uniform copying or a second material interpretation to production.
All accepted source pushed through7615c39; private runtime restored to current.

## November7: specialize programs by pass contract

SoftVolumes scheduling already guarantees unlit materials. Compile its shader
with that fact, sharing the same source, view/draw UBOs and initialization path.
Only fog parameters are uploaded to both programs once per view; no per-draw
uniform copies, driver readbacks, or extra image storage. Ordinary passes no
longer carry the particle-layer interface merely because the view has particles.
Shader programs remain cached and released through the existing lifecycle.

volspec-{1,2}-{reference,eligible}-* isolates unlit specialization:
0013.561→3.5045 and3.567→3.513;0072.917→2.895 and2.921→2.897ms.
opaquespec-* then isolates removing the layer interface from ordinary surfaces:
0013.542→3.530 and3.546→3.520;0072.9175→2.900 and2.914→2.898ms.
Paired images exact. Capture-only scene-general-volume-reference and
scene-general-opaque-reference keep the comparisons reproducible.
Final root3/3, volspec-fx all8 exact vs unlit-final-fx; volspec-msaa0 all8 exact
vs single-fx-new; aux003/006 exact vs st-composite-aux; image-settings passes.

## November7: specialize nearest-surface resolve by sample count

Compile the existing loop with the actual2/4/8 sample count. Same strict-less
tie rule and winning depth/identity/velocity; no precision or storage change.
Framebuffer caches three programs because SwapStorage may change its sample
count without destroying shader ownership. Surface-record resource recreation
already resets its own program. Reference: scene-dynamic-samples-reference.
depthspec-{1,2}-{reference,eligible}-*0013.559→3.471 and3.518→3.4615;
0072.881→2.831 and2.881→2.830ms. All four images exact.
Final root3/3; depthspec-fx all8 exact vs volspec-fx; MSAA2/8 pairs exact with
GL audit; image-settings transitions pass. No CPU/GPU readback on normal path.

Early rejection of fully fogged surfaces was considered but ruled out for the
target scenes: producer optical upper bounds are .915366 for001 and .105906
for007 (no fully opaque interior fog). No speculative early-fog code added.

## November7: rejected follow-up probes

Sparse fog event-bin initialization (two-word validity mask) preserved images
but made field compute slower: .145080 to .153280ms in001. Skipping entirely
empty volumes still lost (.144360 to .149400ms). Both source probes removed.

Direct SSBO light reads preserved images but lost against texture-buffer reads.
lightstore rounds001:3.495/3.706 and3.475/3.499ms reference/probe;
007:2.863/2.8785 and2.851/2.878. Removed the probe completely.

Early light-radius/cone rejection before remaining record reads and powers also
preserved images but lost: lightreject0013.487/3.496,3.486/3.5015;
0072.860/2.865,2.851/2.865. Removed completely. Avoid inferring speed from
fewer source operations: branch/code-generation costs matter on this GPU.

The existing full-resolution particle reference remains materially slower:
nativeparticle001 split/direct3.469/4.1985 and3.476/4.198;
0072.852/3.153 and2.849/3.152. The reduced layer and edge repair earn their
cost here. This is not an image-equivalence claim for direct-vs-reconstructed
particles, nor permission to lower particle quality further.

Scalar-only fog sampling specialization saved only2–7us; removed rather than
adding program variants for a negligible gain. Explicit level-zero fog sampling
was neutral/slightly worse; removed. All paired images exact.

Fresh depthrefresh baseline comparison, old/current rounded GPU milliseconds:
001 native.63/.74, SSAA2/MSAA0 1.32/1.65, full3.15/3.46;
007 native.40/.49, SSAA2/MSAA0 .89/1.16, full2.81/2.84. Still no victory.

Shader ablation (INVALID appearance; diagnostic only), shadercost-*:
001 reference3.471, omit dynamic specular3.467, static specular3.457,
dynamic diffuse3.340, fragment fog sampling2.913.
007 reference2.854, omit dynamic specular2.852, static specular2.847,
dynamic diffuse2.740, fragment fog sampling2.486.
Fog field construction remained enabled in the last ablation; these numbers
isolate its fragment consumers, not total fog cost. All ablation source removed.
Specular loop fusion has little upside in these scenes; focus on fog/diffuse.

Tiled projected boundary reuse for exact surface refinement was tested using
the same per-view bitsets as field construction (SSBO binding3, explicit view
ownership). fogtilesurface images exact but round2 reference/probe001
3.465/3.485,0072.845/2.870ms. Direct tiled integration (no field sampling or
integration dispatch) was much worse: fogtileexact round2 0013.506/4.3635,
0072.830/3.2515. Removed both implementations and their bindings. These were
measured alternatives, not accepted renderer changes.

## November7: defer geometric fog-ray setup until needed

Field sampling needs projected position, not normalized ray/clip-plane path or
inverse-direction bounds data. Construct the latter only for sharp-edge
refinement or exterior fog. Same arithmetic, same exterior/interior blend order.
foglazyray paired0013.457→3.434 and3.470→3.4395;
0072.838→2.823 and2.8445→2.828ms. Four paired images exact. Production change
is a direct shader refactor, no extra shader variants/resources. Root3/3;
lazyray-fx all8 pixel-identical to depthspec-fx; lazyray-aux003/006 pass.

Explicit two-texel column interpolation was separately rejected: first-round
fogfetch0013.451→3.485,0072.835→2.848ms. It also changed2916/668 pixels through
interpolation rounding; no quality or timing benefit claimed. Source removed.

## November7: preserve uniform lighting data frequency

2D fog-column stripe layout lost (fogflat round2 0013.454/3.516,
0072.835/2.877ms) and was removed. A linear-falloff branch also lost; removed.
Draw-uniform range selection alone was neutral. Uniform-buffer storage alone
was weak, but combining it with a distinct draw-uniform diffuse lookup path
gave repeatable gains: lightuniformdraw0013.468→3.428,3.470→3.425;
0072.815→2.7845,2.810→2.787. All four paired images exact.

Production uses one light backing store: at least16KiB for a256-light std140
block, or the existing unbounded texture-buffer view when there are more lights.
No emitter truncation or second CPU light table. Program cache separates the
interfaces; model per-polygon influence ranges remain supported. Small buffers
are orphaned then filled over their live range, with no render-thread allocation.
Capture reference: scene-texture-lights-reference. Root3/3; uniformlight-fx all8
exact vs lazyray-fx; auxiliary003/006 exact; GL-audited interface switches18..22
produce five images identical to the texture-buffer reference.

## November7: compact fog crossing bins with explicit overflow repair

The old producer reserves64 slope/depth bins per ray (16KiB per8x4 group).
Sparse rays now keep up to8 sorted occupied bins (3KiB/group), preserving the
original addition order within each bin. Overflow flags append to the existing
tile SSBO; a second dispatch uses the original full-capacity shader only for
flagged columns. No missing crossings, resolution reduction, precision change,
or ordinary-frame readback. Worst-case overflowing rays pay both attempts.

fogcompact paired0013.4265→3.392 and3.439→3.425;
0072.810→2.794 and2.794→2.765ms. Four benchmark images exact. Isolated dense
field capture .145-ish→.082120ms; do NOT equate that to total-frame savings.
Forced capacity1 sent15843/57920 columns through repair and gave the same image
and numerical audit. Wider8x8 grouping was not repeatably better, removed.
An invalid fogcompactwide-* launch raced a rebuild and was stopped; only
fogcompactwide-valid-* is usable evidence for the grouping comparison.

Production root3/3, compactfog-fx all8 exact vs uniformlight-fx; auxiliary003/006
exact; image-settings passes. fc-odd (311 wide, full precision), fc-vector,
fc-overflow pairs exact; four fc-roof frames150..153 exact. Independent CPU
field audits agree with reference. Capture reference scene-fog-full-bins-reference;
scene-fog-compact-single-probe exercises overflow without dropping crossings.

Latest lightrefresh baseline, before this compact producer, old/current rounded:
001 native.78/.72 (old varied; not a reliable victory),SSAA1.44/1.59,full3.21/3.43;
007 native.40/.47,SSAA.89/1.10,full2.81/2.79. Overall victory still outstanding.

## November7: unlit world runs independent of supersampling

buildViewPasses now emits stable UnlitWorld runs for late unlit materials,
including native-resolution and auxiliary views. No draw reorder, new image,
or per-draw uniform copies. Existing soft-layer blend saturation boundaries and
late depth-writer barriers remain intact. Program key explicitly represents
light storage/fog/layer interface/lighting contract; only used variants compile.

unlitnative paired001 .657→.619,.659→.620;007 .435→.420,.4355→.420ms.
unlitworld full-effects neutral:0013.323/3.330,3.342/3.334;
0072.758/2.755,2.762/2.763. All eight paired images exact. Capture reference
scene-general-unlit-reference uses general programs with the same schedule.
Root3/3 (including mixed lit/unlit schedule tests); unlitworld-fx all8 exact
against compactfog-fx, uw-native all8 reference/unlit pairs exact; auxiliary003/006
exact and image-settings passes. Native gains are not claimed for SSAA volumes,
which already used specialized unlit shaders.
