# Room batching: first execution-contract probe

Status: **experimental, default off; performance gate NOT passed**. Based on
456ccc5 / qualified source 5b0e9bd. No full delta-publication migration,
lightmap arrays, particle instancing, shadows, or Candidate deployment.

## What is implemented

- GPU geometry owns a stable face-table allocation, recycled with its asset
  revision. Addressing is allocation base + existing local polygon ID, not the
  frame's compiled surface index. This is a backend prototype, not yet a public
  stable scene-handle contract.
- A 64-byte/four-texel room record separates UV transforms, light-list ranges,
  and reflection/UV-stream addresses from the 400-byte draw constants.
- The retained intermediate adapter still constructs the old record first.
  It is deliberately not a claim that per-face preparation has been removed.
- Face-table reads happen in the vertex stage. Light indices still use the
  existing influence buffer in per-pixel lighting. Reflection source ranges
  occupy spare lanes of the existing flat integer interpolator. No additional
  varying slot; no light-list truncation.
- Compatible consecutive room faces use one indexed draw, not one multidraw
  command per face. Index storage is reused; upload occurs once per view.
- Optional 128-unit depth buckets group only contiguous opaque-room runs.
  Non-room work remains an ordering barrier; transparency is not reordered.
  This bucket policy is exploratory, not an accepted coplanar ordering contract.
- Optional dedicated room programs keep non-room shaders on the original path.

Flags (all diagnostic, none enabled by default):

- `-scene-room-batch-probe`: vertex-table path, original ordering.
- Add `-scene-room-bucket-probe`: coarse-depth grouping.
- Add `-scene-room-isolated-program`: dedicated room shader programs.
- `-scene-room-shader-only`: table-capable shared shader, no table-backed draws.
- `-scene-room-single-draws`: table-backed draws without merging.

The original frame publication and draw path remain because this is the agreed
go/no-go prototype. They must not become permanent parallel architectures after
an eventual successful migration.

## Evidence and measurements

Private runtime: `C:\tmp\D3RendererVerification\integration-runtime-20261022`.
GPU pairs: RX6600, 1280x720 output, headlight on, native or SSAA2, no AO/bloom/
motion blur, uncapped, main render thread pinned to CPU2. 1500 deterministic
frames per process; medians of frames400–1199 (800 samples). Same executable
within each pair. GPU intervals include submission gaps, not hardware-busy
counters. CPU wall intervals varied substantially between repeated runs; do not
claim a CPU win from one paired median.

Capture root is `C:\tmp\D3RendererVerification`. Naming:
`room-table-{a,b,c,d,e}-{saveg003,saveg007}-ss{1,2}-{ref,probe}`.

| Probe | Dense SSAA reference → probe, ms | Fog SSAA reference → probe, ms |
|---|---:|---:|
| A: fragment/vertex table fetches, original order (removed) | .610 → .644 | .835 → .882 |
| B: vertex-only face-table fetches, original order | .608 → .627 | .828 → .851 |
| C: B + coarse depth grouping | .612 → .630 | .827 → .871 |
| D: short light IDs carried flat (removed; longer lists retained old path) | .608 → .651 | .830 → .871 |
| E: C + dedicated room programs | .611 → .627 | .829 → .861 |

E native: dense .256 → .263; fog .366 → .373. These are **regressions**,
not a new benchmark victory. The old 24/24 qualification remains evidence for
the pre-probe checkpoint, not for enabling this experiment.

Dense captured view:301 eligible faces; original-order table path272 indexed
draws; coarse grouping115. The complete-key census has67 unique compatible
keys, or37 if lightmap texture identity is hypothetically removed. Those are
upper bounds before ordering and array size/format constraints, not achieved
draw counts. The table consumes32576 bytes; visible indices7200 bytes.

Attribution captures `room-attribution-{ref,bucket}-003`, instrumented frames30–79:

| CPU phase, all views/frame | Reference | Grouped adapter |
|---|---:|---:|
| DrawRecords (includes construction/sorting/uploads) | .22692 | .33660 |
| Submission | .34668 | .31056 |
| ExecuteView, inclusive | .74630 | .82856 |

These heavily instrumented short captures establish attribution, not production
frametimes. ExecuteView includes the other rows. Grouping saves submission work,
but the adapter/sort costs more. It would be incorrect to extrapolate that
removing the adapter automatically eliminates the observed GPU penalty.

Shader/lookup/ordering ablation, dense SSAA, `room-ablation-{ref,shader,single,bucket}-003ss2`:
GPU .609 / .613 / .625 / .628 ms. This separates shared-shader changes from
table-backed execution and coarse grouping, but not every individual GPU cause.

## Correctness

- Root CTest:3/3 passed, including new record-layout/canonicalization checks.
- B: all four paired screenshots pixel-exact.
- C/D/E: dense native and both fog configurations pixel-exact; dense SSAA has
  five changed pixels, maximum channel delta5, one pixel above delta2. The
  difference first appears with reordered draws, not vertex-only table reads.
  Do not call grouped SSAA exact or dismiss possible moving-view edge behavior.
- Visually inspected headlight-on dense003 and fog007 images.
- Full-effects007 with SSAA2/MSAA4/AO/bloom and scene retirement/GL audit runs
  complete and pixel-exact; see `room-lifecycle-ref-007` and
  `room-lifecycle-final-007`. Final rebuilt code asserts zero surviving table,
  index and allocation resources after the frame20 retirement/reset and exit.
- New table/index resources participate in scene destruction and retirement
  assertions. No persistent gameplay settings saved by the capture runs.

Reproduce the paired timing/image report with Windows PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/renderer-room-probe-report.ps1 -Tag room-table-e
```

## Decision / unfinished work

Do **not** enable the room probe by default or declare the full migration
validated. Stable addressing and true grouping are working, but this particular
execution design is slower. Lightmap storage alone is not the immediate answer:
ordering already prevents many same-texture faces from grouping.

The CPU-side replacement should compile persistent room material groups when
assets/materials change, select compact group IDs per view, and construct shared
constants once per batch—not create and compare400-byte records per face, then
try to recover the groups. That replacement is NOT implemented here. GPU work
still needs to distinguish influence indirection, vertex table cost, and
ordering/overdraw before committing all consumers to this contract. Particle
instancing remains a separate authorized opportunity; full delta publication
remains deferred. Retain the qualified checkpoint as the comparison baseline.
