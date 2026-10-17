# CPU ownership repair — October 17, 2026

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
