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
