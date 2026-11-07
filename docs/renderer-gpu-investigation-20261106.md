# GPU bottleneck investigation — November 6

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
