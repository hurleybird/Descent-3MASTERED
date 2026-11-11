# Retained room batches: recovery of the first prototype

Status: **experimental, default off**. Continues 5681dbe5. This is not a
replacement of the qualified 456ccc5 / 5b0e9bd release path, a new 24/24 claim,
or a Candidate deployment. Particle instancing and full delta publication are
not implemented. No shadows or cockpit geometry changes.

## What was actually wrong

The first probe combined three different costs:

1. Moving light counts/list addresses from draw uniforms into per-face values
   changed the fragment shader's lighting execution. Keeping lighting uniform,
   including its existing short-list shortcut, recovered most of the GPU loss.
   Long lists are still complete; no lights were truncated to obtain this.
2. Coarse material sorting increased opaque fragment invocations. Hardware
   pipeline counters, not an elapsed-time guess, confirmed this: dense003 at
   frame90 went from 4,574,455 to 4,668,542 fragment invocations; fog007 went from
   3,924,318 to 4,056,832. Primitive counts were unchanged. An index-only ablation
   without face-table reads showed substantially the same increase.
3. An index buffer alone did not reduce vertex work: it initially indexed every
   expanded triangle corner separately. Reusing identical polygon-fan corners
   now reduces vertex invocations, with the same triangles, winding and streams.

The room configuration now uses dedicated room programs, draw-uniform lighting
and vertex reuse. Narrow relative-depth sorting was also tested, but its CPU
cost did not earn a repeatable whole-frame benefit. The candidate now preserves
the existing front-to-back order and merges adjacent compatible room faces.
The vertex table adds no varying slot and is not fetched at fragment frequency.

## The CPU contract now implemented

The old adapter constructed a 400-byte record for each face, then attempted to
merge identical records. The retained path instead:

- Owns lazy material templates with the published room material table and GPU
  geometry revision. A new material publication invalidates them; retaining the
  publication prevents pointer-address reuse from falsely identifying a match.
- Separates twelve genuinely view-dependent words (lighting, illumination,
  exterior-fog enablement, soft-depth range and deformation-history validity).
- Selects shared records with a reused open-addressed table. Unchanged room
  materials no longer reconstruct or compare wide records per face.
- Keeps per-face UV transforms and stream addresses in the geometry-addressed
  face table. Exact content changes trigger uploads; unchanged values remain
  resident. Selected index contents are likewise retained when unchanged.
- Binds shared uniform records/element buffers only when their binding changes.
  Element-binding state is invalidated when the VAO changes.

In dense003 frame90, all 301 eligible faces reuse their templates and need only
10 room uniform records. The entire main view goes from 1,596 to 1,305 records;
1,295 non-room records remain. At frame1500 the effect population is smaller:
935 surfaces become 644 records. Both frames still have 301 room faces and
211 room draws with fine sorting, or 272 with original depth order. Do not
compare different simulation frames as identical work.

Scratch storage retains capacity. The draw-record array is bulk-zeroed once
per view, including its upper-bound unused tail; only emitted records are
uploaded. This deliberately avoids a small memset per non-room draw. It does
not claim that all per-face CPU work or publication work has disappeared.

## Evidence and limits

Evidence root: `C:\tmp\D3RendererVerification`. These comparisons use the same
experimental executable, default room execution versus the retained flag,
deterministic saves/headlight, 1280x720, render-thread affinity CPU2. Timings are
not sums of CPU and GPU: those intervals overlap.

- `room-resident-1111-{saveg003,saveg007}-ss2-{ref,probe}`: GPU medians match at
  .608 and .829 ms respectively; images exact. CPU wall medians are worse in
  this pair, so this pair is not evidence of a CPU win.
- `room-bindings-1111-{saveg003,saveg007}-ss2-{ref,probe}`: CPU 1.507 to 1.4705 ms
  and 1.0615 to .988 ms; GPU .608 to .609 and .829 to .833 ms. Images exact.
  Independent reversed-order native runs still fluctuate enough to change the
  CPU ranking. Do not promote these selected favorable medians as a proven win.
- `room-retained-attribution-1111-{ref,rebuild,probe}`: native frame30..79
  instrumented DrawRecords .26294 / .37340 / .33714 ms. Caching templates saves
  about .036 ms versus rebuilding them with the same room execution, but the
  original implementation still spends more preparing batches than reference.
- `room-resident-attribution-1111-{ref,stream,probe}` isolates exact-change GPU
  uploads. RoomPlan remains about .044 ms. This is why retention hit counts
  alone are not enough to claim performance success.

`-scene-room-alternate-probe 0` or `1` alternates reference/retained execution
every 200 capture frames, reversing phase between paired runs. This is a
capture-only control for process-level timing variation, not a gameplay mode.
Both modes warm before measuring; discard the first 400 frames and transition
edges. Compare the same simulation frames across runs, take each 160-frame
block's median log(probe/reference), then average eight block medians. Opposite
phases balance fixed process-speed differences. Negative changes are better.

Fine-sorted crossover (`room-crossover-1111`):

| Save / SSAA | CPU render wall change | Render-thread cycle change | GPU interval change |
|---|---:|---:|---:|
| 003 / 1 | -2.03% | -1.98% | -1.35% |
| 003 / 2 | +0.00% | +0.27% | +0.01% |
| 007 / 2 | +0.44% | +0.67% | -0.13% |

This is near parity, not the proposed substantial improvement. RoomPlan takes
about .044 ms to remove about 60 additional dense-scene submissions beyond
adjacent merging. The ordered candidate removes that extra sorting work.

The report helper supports this paired crossover with `-Crossover`, using the
same tag/save/scale arguments and `phase0` / `phase1` evidence directories.

Original-order crossover (`room-ordered-cross-1111`, opposite process launch
order to the fine-sorted experiment):

| Save / SSAA | CPU render wall change | Render-thread cycle change | GPU interval change |
|---|---:|---:|---:|
| 003 / 1 | +2.67% | +2.39% | -0.10% |
| 003 / 2 | +0.59% | +0.56% | +0.08% |
| 007 / 2 | +3.10% | +2.72% | -0.21% |

Removing sorting reduces RoomPlan from about .044 to .0083 ms, but also loses
draw merging. The whole renderer still does not improve convincingly.
`room-ordered-attribution-1111-{ref,probe}` shows DrawRecords .20882 to .23664 ms
and submission .34370 to .34852 ms. The material-cache fast path plus face-table
maintenance is not free: Materials is .17300 versus .19610 ms in this pair.

**Decision: performance gate still failed.** The GPU regression is recovered
and unchanged room materials have a real retained representation, but neither
sorting policy has established the required CPU improvement. Do not enable it
by default, describe it as a faster renderer, or justify a larger migration
with selected favorable standalone timings. A meaningful next step needs to
remove more per-face/instance execution work upstream, not add another sort
heuristic. The particle population remains outside this room-only work.

## Correctness checked

- CTest 3/3 passed: fan index topology/overflow, material/view reconstruction,
  batch-key collisions/reuse and publication invalidation are covered.
- `room-retained-corpus-1111-saveg00[0-7]-ss2-{ref,probe}`: seven images exact;
  002 differs by one channel level in two pixels. GL audit runs completed.
- `room-retained-{retirement,deformation,auxiliary}-1111-saveg007-ss2-{ref,probe}`:
  SSAA2, MSAA4, AO, bloom and motion blur. Retirement and auxiliary lifecycle
  images exact; deformation differs in ten pixels, maximum channel delta2.
  This small difference is reported, not called pixel-exact.
- Headlight-on dense003 and fog007 images inspected visually; no missing
  surfaces, broken fog boundary or obvious changed shading in those captures.

Final original-order build (`434DFA98F05CA1E59011A21F43094D23694AE5AD2533236C8578ABF9138B5FE0`):

- `room-ordered-corpus-1111`: repeated all eight pairs, same seven exact / two
  pixels at delta1 in 002 result.
- `room-ordered-{retirement,deformation,auxiliary}-1111`: all three full-effects
  lifecycle pairs are now pixel-exact, including deformation without sorting.
- `room-ordered-reload-1111`: load003, reload007 at frame20, capture60; exact.
- CTest 3/3 passed again; final full-effects/deformation image inspected.

Builds and captures used only the private verification runtime. No Candidate,
Previous Candidate, user desktop shortcut or persistent gameplay settings were
changed. Unrelated untracked `lib/gitinfo.h` is not part of this change.

## Diagnostics / unfinished

Use `-scene-room-retained-batches` for the coherent candidate configuration.
`-scene-room-material-reference` rebuilds material constants with the same
execution; `-scene-room-stream-reference` forces the face/index uploads.
Older shader/order/index-only ablations remain diagnostic, default off.
`-scene-pipeline-audit` requests capture-frame-only hardware pipeline counts
when ARB_pipeline_statistics_query is supported.

The performance gate is not yet established. Remaining work includes repeatable
CPU attribution, deciding whether room grouping earns its complexity, and
removing failed ablation paths before any default promotion. Lightmap storage
and particle instancing are separate work, not implied by this checkpoint.
