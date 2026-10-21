# CPU ownership repair — October 17, 2026

## October 20 continuation

October 21: sealed model summaries now retain the draw mask, and model assets
record whether they contain view-dependent attachments. Unchanged source models
without attachments skip per-part source traversal entirely. Saved-lightmap
topology checks still execute; the UV audit still walks baked parts. Attachment
metadata and held draw-mask tests pass. Dense/save005 trials: 3.504/2.406 ms;
save005 and the combined audit/effects probe are pixel-exact to frame-storage.

October 21: released presentations now recycle top-level frame-vector capacity,
with at most two spare sets per publisher. Held snapshots are never reclaimed;
foreign-thread releases free normally, and a snapshot can outlive its publisher.
Spare sets own no scene resources or pool reference. Source extraction acquires
empty builders from this pool. Lifetime/reset/thread tests pass. Initial corpus
000–007: 2.612, 1.945, 3.501, 4.135, 0.913, 2.698, 1.086, 2.831 ms. Combined
effects/audit probe and all eight captures match the instance-modulation
checkpoint pixel-exactly. No all-baseline win claimed.
Temporary publication-stage instrumentation was removed after measuring save005:
model validation ~0.12 ms, world validation ~0.08, model bounds/sealing ~0.04,
material policies ~0.03, other stages ~0.01 each. Raw data is
`C:/tmp/D3RendererVerification/cpu-freeze-stages-20261021.tsv`.

October 21: conservative emitter lists use ranges in one frame-owned packed
table. Deferred instances in the same room share one list; resolving a model
copies only the range into its parts, not a vector per part. Eager posed parts
retain their finer bounds filtering. Publication validates index contents once
and validates each range (including retained summaries). Held-frame, missing
emitter/range and sharing tests pass; save005 and combined probe are pixel-exact.
Initial save005 was 2.777 ms; no total speedup claimed. Later dense timings became
unstable even for retained control binaries (5.67–7.66 ms); the 13.35 ms first
light-range dense run is not useful evidence of a code regression or improvement.
System sampling was unavailable: xperf rejected the kernel flags and WPR reported
that performance-profiling policy could not be enabled. WPR confirms no recording.

October 21: world model material inputs now describe the entire binding table,
not identical records constructed for every part. Unchanged published tables
reuse directly; changed inputs rebuild without copying the old parts first.
Dynamic attachments/pose remain independent, and per-object lightmap topology
validation plus the baked-UV revision audit are preserved. Contract tests cover
each appearance input, exact subobject masks, palette/layout changes and absent
history. Combined audit/effects probe and save005 image match exactly. Initial
save005 trial: 2.601 ms total, models ~0.51 ms, parts ~0.14 ms (previous portal
trial 2.854 / ~0.65 / ~0.23). These are single-run measurements, not a final win.

Portal lists now have a sealed copy-on-write lifetime independent of frame room
scalars. Extraction compares the complete authored connection tuple before
reusing storage; traversal/two-sided policy detaches only when it changes.
Published material policy is reused for unchanged portal surfaces. Tests cover
source reconnection/flags, shared unchanged topology, held-frame edits and mutable
builder aliases. Existing door/cycle/exterior tests pass. Combined probe matches
exactly. Dense/save005/small trials are 3.943/2.854/1.173 ms; no net speedup claim.
Save005's remaining measured CPU is dominated by preparation (~2.02 ms), including
models ~0.65, resources ~0.56 (procedural simulation ~0.32), freeze ~0.32 and
extraction ~0.17 ms. View execution is ~0.59 ms. These stages are nested, not additive.

Instance ambient light, tint and opacity are now separate from immutable model
materials. Effective blend/depth policy is derived for visibility/submission,
not baked into a fresh surface table whenever an instance fades. All eight
captures and the combined probe match the preceding checkpoint exactly.
Contract tests cover 144 blend/depth/opacity combinations, modulation factors,
held-frame immutability, fade completion and invalid alpha rejection.
Full corpus was noisy (3.209, 2.107, 3.924, 4.211, 1.154, 3.678, 1.304, 3.598 ms),
so both binaries were retained under `out/perf-checkpoints` and interleaved with
the original renderer, with detailed markers disabled. Two trials, parent/new:
dense 4.052/3.896 and 4.405/3.836; save005 2.964/2.764 and 2.845/2.762;
small 0.971/0.996 and 0.946/0.944 ms. This supports keeping the separation but
does not establish an all-baseline win. Concurrent legacy trials: dense
2.992/3.406, save005 1.053/1.010, small 0.837/0.815 ms. Save005 remains a large gap.

October 21: GL raster state is shared across consecutive identical policies within
each pass (ordering is unchanged, and external/image passes cannot poison the
cache). Shader uniform locations and fixed sampler bindings are resolved at link
time, rather than queried by name for every view. Submission drops about
0.53 -> 0.29 ms in the dense scene. Full raster/interface corpus: 2.892, 2.071,
3.810, 3.883, 1.025, 2.673, 1.131, 3.180 ms. Six captures exactly match the
preceding corpus; save000/save002 reproduce their known local moving-object
variants. Combined AO/bloom/auxiliary/resource/source-audit probe matches exactly.
The old-renderer target remains unmet.

Prepared model parts now have frame-local packed indices. GPU preparation uses
reusable arrays for per-part light/position ranges and per-mesh position ranges,
and appends deformed positions directly into the upload buffer. No transient
pointer-keyed nodes or intermediate position vectors are required for these paths.
The effects/auxiliary/source-audit probe is pixel-identical to the flat-model
checkpoint and was visually inspected. Tests verify distinct eager/deferred part
addresses even when immutable bindings are shared. Initial dense CPU was 4.51 ms;
full corpus: 3.292, 3.702, 4.632, 4.757, 1.192, 2.901, 1.333, 3.310 ms. Mixed timing
continues; these results do not meet the pre-rewrite CPU target.

Model variants now occupy one frame-owned array. Instances hold checked,
non-overlapping ranges rather than individually allocated vectors. Source world,
cockpit, preview, resource, lighting and GPU consumers use that layout. Filtering
instances explicitly compacts variant storage; publication rejects overlap,
out-of-range, empty and orphaned ranges. Contract tests and all three LOD source
audit fixtures pass. Dense instrumented/uninstrumented trials are 4.95/4.64 ms:
no proven whole-frame speedup is claimed for flattening alone.

This migration exposed a stale motion helper that read canonical unevaluated
parts after poses became deferred. It now consumes resolved previous poses;
41 animated/deformed regression samples verify the temporal transform. The
combined AO/bloom/auxiliary/resource probe now exactly matches the pre-deferred
owned-animation probe (removing the known two-pixel, max-three-channel delta).
The plain dense capture exactly matches the preceding summary checkpoint.

Model publication now retains aggregate surface count, conservative motion radius,
draw membership, required light count and unique image-validation dependencies.
Held tables share the summary; any part edit invalidates it. Publication/validation
in the dense probe drops approximately 0.70 -> 0.38 ms. The full summary corpus
is 3.131, 2.430, 4.241, 4.414, 1.150, 3.052, 1.489, 3.774 ms. Still no all-baseline win.
The combined effects/auxiliary/deformation/resource/audit capture is pixel-identical
to the collection checkpoint. Contract tests cover shared summary identity and
changed draw membership as well as previous stale-proof rejection cases.

Texture-name bloom classification is cached against the exact name (not texture
slot alone), preserving direct legacy rename/slot-reuse behavior without repeated
name-list scans. New focused tests pass. Finer resource markers identify about
0.23 ms of procedural simulation in the dense scene; pixel publication is only
about 0.01 ms, and dependency-list rebuilding averages about 0.04 ms. Do not
attribute the remaining resource cost primarily to allocation or pixel copies.

Resource preparation now retains the exact union of immutable dependency lists,
while still resolving current texture frames and producer revisions every frame.
The resource-layout corpus is 3.46, 2.35, 4.58, 4.67, 1.06, 3.34, 1.30, 3.56 ms.
Fresh uninstrumented old/new pairs remain losses: dense 2.61/4.46 ms; small
0.85/1.01 ms. These are not completion results.

Collection membership is compiled into flat index spans at publication, avoiding
world scans for cockpit/HUD queries. Decal receiver lookup uses sorted reusable
storage, not per-face hash nodes. Model part publication proofs are asset-keyed;
edits detach/invalidate the proof, and current-frame image/light names remain
validated. Added membership, held-frame, edited-pose and changed-topology tests pass.
The combined effects/auxiliary/source-audit probe matches the resource-layout
checkpoint exactly. Collection/proof corpus timings were mixed (3.32, 2.85,
5.57, 5.38, 1.78, 3.15, 1.38, 3.72 ms); unchanged-build repeats of dense/small
were 4.66/1.28 ms. No overall performance win is claimed for these latter changes.

Room appearance now consumes a producer epoch rather than comparing every face
each frame. Tracked writers include texture setters, destruction, script face
edits, lightmap/specmap clearing and save restoration. Legacy multiplayer DLLs
have writable room pointers, so callback return is an explicit conservative
invalidation boundary. Geometry invalidation also invalidates appearance.
Only material-relevant face flags cross that boundary; source audits still
detect unannounced edits. Extraction falls roughly 0.48 to 0.20 ms in save003.

Visibility transforms/tests each demanded vertex once per geometry placement
and aperture, using cached outcode intersection with the original tolerances.
600 scalar-reference cases plus the existing portal/mirror tests pass. Query
time in the initial dense pair is 0.60 -> 0.42 ms, but total timing remains noisy.
`cpu-projected-mesh-corpus-20261020-savegNNN` means: 4.10, 2.76, 5.93, 5.72,
1.22, 4.58, 1.52, 4.45 ms. Initial dense capture was 5.00 ms: do not cherry-pick it.

The blanket material-retention exclusion for mirror rooms is removed. Planar
membership/planes are part of the sealed table's exact publication key; changing
or removing mirrors recompiles policy. Unchanged mirror rooms reuse materials.
All eight `cpu-planar-retention-audit-20261020-savegNNN` source audits pass.
Seven captures match the prior corpus; save000 matches its previously reproduced
moving-object variant. The combined effects/auxiliary probe matches exactly.
New tests verify retained mirror storage and removal of stale planar policy.
Initial dense/small totals are 5.36/1.09 ms, still slower than pre-rewrite.
Resource-stage diagnostics now split room/model/other image preparation: dense
capture has 551 bindings, 548 pixel sources, only four revised sources at frame90.
Most remaining resource CPU work is dependency traversal, not copying new pixels.

Model part bindings now have a publication lifetime independent of frame pose,
light candidates and surface-table offsets. Unchanged tables are shared across
frames; editing detaches them. Deferred models carry current light candidates,
surface base and previous draw mask separately, applying them only to demanded
poses. Asset publication compiles hierarchy exclusion; role selection no longer
allocates a vector or walks the authored hierarchy per instance per frame.
Tests cover shared table identity across pose changes, detachment/held frames,
frame-local indices, visibility history and sprite attachments; the full suite
passes 20,143 checks (many are packed-index test cases).

`cpu-model-bindings-corpus-20261020-savegNNN` clean CPU means are 4.04, 3.02,
5.93, 5.40, 1.12, 4.46, 1.49, 4.09 ms. Dense model preparation falls to 0.77 ms
(parts 0.27), light preparation to 0.05, total presentation preparation to 3.10.
These are whole-frame means, not a claim that old has been beaten: it has not.
Seven corpus images match the packed-relations captures exactly; save000 matches
the unchanged-build repeat rather than its earlier moving-object variant.
All eight final source audits pass. Seven final-audit images match that corpus;
save002 has the previously reproduced pickup-animation delta (mean 0.030214,
maximum 157). The auxiliary/deformation/resource/AO/bloom probe matches exactly.

Identity lookup uses packed, frame-owned tables rather than per-entry hash nodes.
Extraction/model history reuse the previous presentation's indices. Light/room
reachability uses one dense relation and a reusable traversal queue; exterior
reachability is computed once per light, not once per terrain patch. Contract
tests include growth, duplicates, signed/sparse/high-bit identities and copies.
`cpu-packed-relations-20261020-savegNNN` CPU means: 4.87, 3.38, 6.63, 6.38,
1.41, 5.48, 1.74, 5.38 ms. Mixed/noisy timings do not establish a corpus speedup.
Seven captures match the deferred checkpoint exactly; save000's first capture
has a localized moving-object difference, and an unchanged-executable repeat
matches exactly. The combined auxiliary/deformation/resource audit probe matches
exactly. These changes eliminate node allocations, not whole-frame allocation.

World model evaluation is now demand-driven. Published frames retain owned
animation inputs and conservative animation bounds; a view resolves the selected
variant only after room/coarse-bound rejection. Current/previous deformation and
attached sprites consume the same resolved pose, including auxiliary views.
Observation candidates are added after exact posed-bound rejection, not merely
because the conservative animation envelope intersects the view.

`cpu-deferred-audit-20261020-savegNNN` passes all eight source/pose audits and
matches all eight preceding owned-animation captures exactly outside the banner.
The combined view-image/deformation/resource/AO/bloom probe was visually inspected;
against the preceding probe its mean channel difference is 0.000155, with only
two pixels differing by more than two levels (maximum three). It is not pixel-exact.
Contract tests cover 41 hierarchical animation/deformation samples, conservative
bounds, previous-pose reconstruction, held frames, repeated/auxiliary queries and
invalid inputs. Clean CPU means for saves 000–007 are 4.83, 3.37, 6.95, 6.43,
1.38, 4.90, 1.48, 5.32 ms. **Every baseline still loses to pre-rewrite.** Dense
total time is effectively unchanged; pose preparation fell 0.38 to 0.08 ms but
this is not a demonstrated dense-frame speedup. Whole-level instance/material
assembly, resource preparation, validation and frame storage remain work.

Model assets now own animation tracks as well as topology. The original and
owned-track pose APIs share one evaluator/composer; no second animation math
implementation was introduced. Asset publication severs mutable builder aliases
and verifies that animation and geometry hierarchies agree. Source revision
audits now compare animation keys too. World/static/cockpit sampling uses the
owned tracks. This is a prerequisite for demand-driven model evaluation, not
that scheduling change itself: poses are still eagerly assembled at this point.
All eight `cpu-owned-animation-audit-20261020-savegNNN` images match the preceding
checkpoint exactly. The auxiliary-view/deformation/resource/AO/bloom probe also
matches exactly. Tests compare 404 keyed samples against the original evaluator
plus rest, source-lifetime and mutable-alias cases. Dense clean CPU is 6.45 ms
in the latest run; do not attribute the small variation to the ownership change.

Latest checkpoint: authored special-face reflection data and lightmap-info
assignments have producer revisions; the material-input scan falls from about
0.49 to 0.03 ms. Geometry audit retains the full comparison and completed all
eight saves. Frame-resource lookup borrows images instead of reference-counting
every dependency, and publication builds one packed name index for validation
and draw lookup (including sparse/high names and collisions in tests).

World/cockpit/preview model assets now use one revision-aware acquisition path.
Cockpit no longer recompiles its complete topology every frame or reconstructs
unchanged material tables; animation/placement still update. This does not edit
cockpit geometry. `cpu-shared-model-corpus-20261020-savegNNN` total CPU means:
4.98, 4.28, 6.73, 6.89, 1.37, 5.30, 1.61, 5.85 ms. All remain slower than old.
Seven images match start-of-day exactly; save002 matches the known alternate
pickup capture. The AO/bloom/deformation/resource/auxiliary-view probe matches
exactly and was visually inspected. `cpu-shared-model-assets-audit-20261020`
also verifies the newly shared cockpit asset path. Contract suite passes.
Dense preparation remains about 4.5 ms; whole-level model assembly is still
the largest substage. Do not claim completion or old-renderer visual parity.
Terrain publication now also consumes the existing Terrain_render_revision
(loader/min-max rebuild and terrain deformation already advance it), plus the
four lightmap assignments. It no longer compares all 65,536 cells every frame.
All eight `cpu-terrain-audit-20261020-savegNNN` captures pass the full source
comparison. Clean save003/save004 images match exactly; their latest CPU means
are 6.63/1.43 ms, with enough run-to-run noise that the small-scene change is not
a demonstrated total-time win. Terrain source revision is not a visibility skip.

Second checkpoint: model assets retain rest poses; unchanged instance inputs
reuse previous poses, and identical animated inputs share one evaluation within
a frame. Geometry audit independently evaluates every pose. Immutable fog
validation moved to fog publication. Sky geometry is retained against its
authored geometry inputs, separately from frame rotation and material state.
Transient upload addresses no longer clear the whole-level table every view.
Resource index scratch replaces per-frame hash-node allocations.

`cpu-sky-corpus-20261020-savegNNN` completes all eight saves. Seven images match
the start-of-day rewrite exactly. Save002 had a small animated-pickup delta;
`cpu-sky-repeat-20261020-saveg002` matches exactly. This is capture variability,
not proof of old-renderer parity. Full pose/image audits complete all eight
(`cpu-pose-corpus-20261020-savegNNN`); view/deformation/resource probe with AO and
bloom also completes (`cpu-sky-viewprobe-20261020`). Dense total render CPU is
7.83 ms in the latest clean capture, still far above the 2.93 ms old baseline.
Detailed markers are not the root cause: disabled-marker paired runs measured
8.33 ms new / 2.63 ms old before the sky change. Contract suite passes.
Light assignment now retains conservative room/part candidates and refines
polygons at submission, instead of allocating per-polygon lists throughout the
level. Dense light preparation drops about 0.59 to 0.26 ms; view upload remains
about 0.39 ms. `cpu-demand-lights-20261020-savegNNN` CPU totals are 5.63, 4.23,
7.26, 7.73, 1.70, 6.97, 2.02, 6.44 ms. Still no baseline victories. Images match
the pre-change rewrite (save002 matches the repeat/original rather than the
alternate pickup capture). The AO/bloom auxiliary-view/deformation/resource
probe is pixel-identical. Tests cover held-frame light refinement, output range
append behavior and deformed/posed positions. Whole-level model assembly,
resource bindings, extraction and publication remain too expensive.

Frame-owned image bindings replace surface-owned pixel revisions. Surface
tables keep stable resource names; each frame owns the immutable images bound
to them. Texture animation selects the frame image once per resource; scrolling
UVs are evaluated only for submitted surfaces. Pixel changes and ordinary
animation no longer detach/revalidate/recompile whole material tables. Image
format changes still recompile affected material policy and publication rejects
stale format metadata. Held-frame/image-sequence isolation tests cover this.
Authored object lightmap layouts now have a producer revision (setup/free),
with explicit invalidation for future writers. The geometry audit still compares
all UVs; ordinary frames retain them. The model/layout compatibility check that
protects model swaps and the Level 2 regression remains in place.

Fresh paired 720p clean captures, render CPU mean ms over frames 15–84:

| Save | Pre-rewrite | Start of October 20 | Resource separation |
| --- | ---: | ---: | ---: |
| 000 | 1.65 | 11.02 | 7.56 |
| 001 | 1.76 | 9.12 | 5.05 |
| 002 | 2.61 | 13.63 | 8.85 |
| 003 | 2.93 | 11.80 | 8.83 |
| 004 | 0.91 | 2.57 | 2.00 |
| 005 | 1.15 | 11.90 | 7.09 |
| 006 | 0.98 | 2.81 | 2.35 |
| 007 | 1.32 | 11.28 | 8.64 |

No case beats the pre-rewrite CPU baseline. These are whole CPU render times,
not preparation substages. Evidence: `cpu-baselines-20261020-savegNNN-{old,new}`
and `cpu-resources-20261020-savegNNN` under the verification directory.
All eight resource-separation screenshots match the start-of-day rewrite
screenshots exactly outside the banner. This is not old-renderer visual parity.

5,774 contract checks pass. Eight AO/bloom runs with image and geometry audits
complete (`cpu-resources-20261020-effects-savegNNN`). Independent-view image,
deformation and resource-transition fixture completes with both audits
(`cpu-resources-20261020-viewprobe`); screenshot visually inspected.
Whole-level model preparation remains expensive. Continuing there; no shadows,
gameplay qualification, Candidate mutation or main-checkout edits.

The sections below describe the preceding checkpoint.

This is a partial repair, not a claim that the renderer is fast or qualified
for gameplay. Main checkout, Candidate and Previous Candidate are untouched.

## Faults repaired

- Resource publication compared bitmap/lightmap pixel contents every frame to
  discover changes. Writers now advance a per-slot content revision. Scene
  publication skips unchanged images, and still preserves immutable held frames.
  A dirty write that leaves identical bytes does not create a new GPU asset.
- Extraction compared every room vertex/UV/volume and model geometry again.
  Geometry creation/editing/freeing now invalidates producer epochs. Full content
  comparison happens after invalidation, not during normal unchanged frames.
- GPU upload tables were built for the entire presentation despite already
  having the view's visibility result. Transient tables and position streams now
  select visible rooms, mesh instances and model parts. Every auxiliary view
  supplies its own demand; there is no main-camera-only visibility shortcut.
  Reflection assets remain resident across views, and scratch vectors retain
  capacity. Visible rooms/parts still upload their complete binding tables.

## Evidence and limits

All evidence is under `C:\tmp\D3RendererVerification`.
The original dense fixture is `cpu-repair-20261017-publication`.
The staged repair is `cpu-view-residency-20261017-after`.
Means across marker frames 16–84, milliseconds (nested costs, not additive):

| Stage | Before | Repaired staged run |
| --- | ---: | ---: |
| PreparePresentation | 11.27 | 9.78 |
| Extract | 1.92 | 0.78 |
| PrepareResources | 1.77 | 0.80 |
| Upload.Tables | 1.29 | 0.63 |
| PrepareModels | 2.93 | 2.77 |

Run variance is material. These are not simultaneous measurements or a claim of
an overall old-renderer win. The image revision change and visibility upload
change preserve the dense screenshot exactly outside the diagnostic banner.
Seven of eight effect captures match exactly across the demand-upload change.
Save002 has a localized moving-object difference; earlier unchanged-reference
reproduction of pose variation is documented in renderer-gpu-repair.md, but this
new pair should not be called pixel-identical.

`-scene-image-revision-audit` forces content comparison and fails if a producer
revision missed an image change. `-scene-geometry-revision-audit` does the same
for room/model geometry. Combined audits pass across eight saves in
`cpu-epochs-20261017-audit-savegNNN` and
`cpu-view-demand-20261017-savegNNN`. This samples runtime behavior; it is not proof
that an arbitrary future direct-memory writer can omit invalidation safely.
New geometry writers must call their producer invalidation API, pixel writers
must call bm_MarkChanged/lm_MarkChanged. GPU-cache invalidation alone is not a
content mutation and does not advance these revisions.

5,766 contract checks pass, including missing-invalidation detection, image
revision cache behavior and existing immutable-held-frame checks.
`cpu-view-residency-20261017-viewprobe` successfully exercises independent view
images, MSAA view targets and deformed model position uploads, with both audits.
Its screenshot was visually inspected: world, inset view and cockpit are present.

Final build (scratch-capacity reuse included):
`cpu-ownership-20261017-final` measures 9.42 ms preparation, 0.74 ms extraction,
0.76 ms resources, 0.55 ms upload tables and 2.72 ms model preparation.
The dense screenshot remains identical. All eight final audit runs complete;
seven effect images match the pre-demand run exactly, with the same localized
save002 difference. Final executable SHA256:
`7DB6D15ECC2509B2F714D50C73F5EE573807AE9A6CEDA3008FF5C69039FD6C29`.

## Remaining root problem

Preparation still materializes whole-level model poses/materials before view
demand. Dense save003 prepares 504 model variants. Animated/procedural/sliding
textures invalidate whole material tables; changed image refs detach tables;
publication then revalidates/recompiles them. These are wrong invalidation and
scheduling boundaries, not reasons why this game inherently needs 9–10 ms CPU
preparation. Separate stable surface/material ownership from per-frame image,
UV/appearance inputs, then plan demand before expensive model preparation while
preserving mirrors, auxiliary views, foregrounds and temporal history. Do not
remove checks or hide content merely to report a lower number.
