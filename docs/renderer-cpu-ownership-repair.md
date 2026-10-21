# CPU ownership repair — October 17, 2026

## October 20 continuation

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
