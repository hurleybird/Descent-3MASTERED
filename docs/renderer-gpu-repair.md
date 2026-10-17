# GPU repair verification — October 17, 2026

Worktree: `codex/renderer-rearchitecture`, based on `0809c9e`. Uncommitted work
includes earlier migration work; this is not a clean, isolated GPU-only diff.
Candidate, Previous Candidate and the main checkout have not been changed.

## Architecture

AO now estimates from a logical-resolution, single-sample depth/surface prepass.
World material shading applies that estimate to eligible surface light before
fog/compositing. This replaces the full-SSAA/MSAA floating-point eligible-radiance
attachment and its late subtraction pass. Opaque scene export is depth-only.
World color and display targets use RGBA8 (the old renderer's range); motion and
effect intermediates retain floating point where needed. Presentation reads the
world or motion result directly rather than unconditionally copying it first.

Visual changes to watch: logical single-sample edge coverage; material-local
highlight suppression instead of the former filtered opaque-color suppression;
restored LDR accumulation/clamping. These are not pixel-identical algorithms.

## Evidence

All paths below are under `C:\tmp\D3RendererVerification`.

- `gpu-repair-20261017-scaling/summary.csv`: nine paired old/new cases, saveg004,
  fixed seed/time, frames 15–84. GPU median milliseconds:

| Case | Repaired | Old |
| --- | ---: | ---: |
| 720 clean | 0.248 | 0.332 |
| 1440 clean | 0.734 | 0.804 |
| 2160 clean | 1.640 | 1.723 |
| 720 SSAA2 | 0.739 | 0.784 |
| 720 MSAA4 | 0.301 | 0.395 |
| 720 AO | 0.874 | 1.096 |
| 720 bloom | 0.429 | 0.453 |
| 720 SSAA2/MSAA4/AO/bloom | 2.149 | 2.577 |
| 1440 SSAA2/MSAA4/AO/bloom | 9.439 | 11.677 |

These are GPU intervals, not total frame time. Small differences require repeat
runs; effects differ between renderers. CPU render time remains worse: the
static clean case is 2.50 ms versus 0.81 ms. Dense saveg003 preparation alone is
about 12 ms. No overall performance win is claimed.

- `gpu-repair-20261017-comparison`: eight matched save captures, old late-AO
  intermediate versus repaired schedule (NOT legacy-renderer parity). Largest
  fraction exceeding two RGB levels: 0.62435%, saveg001. The world views have
  been visually inspected, including fog, translucent effects and bright edges.
- `gpu-repair-20261017-motion-noao`: motion/deformation/history plus resolution
  and MSAA transition fixture, with cockpit/HUD. Completed and image inspected.
- `gpu-repair-20261017-ao-debug`: fullscreen grayscale AO with cockpit/HUD left
  in color; built and visually inspected. Capture-only flag
  `-scene-ao-debug-probe` does not modify saved preferences.
- Earlier AO-enabled transition evidence: `gpu-repair-20261015-history-resources`.

## CPU continuation

Canonical quad and quad-batch ownership now lives in SceneAssets, rather than
rebuilding/validating identical topology at every caller. Lifetime/reset tests
pass. `cpu-repair-20261017-primitives` is pixel-identical to
`cpu-repair-20261017-before` outside the comparison banner region. The initial
CPU timing change is small and not isolated from run variance; it is not the
CPU repair. Additional model-stage markers identify the next bottleneck.

An old-first heavy-case repeat on the checkpoint build measured 12.77 ms old
versus 9.69 ms repaired GPU median. CPU render means were 0.98 versus 4.18 ms.
Evidence: `gpu-repair-20261017-repeat-heavy-{old,new}`. This confirms the GPU
direction across runs, not exact reproducibility of the absolute timings.

Surface dependency publication now uses reusable contiguous scratch instead of
one map/set allocation per dependency. Dense-save publication drops from about
1.48 to 1.34 ms in the measured pair; total preparation remains about 11.3 ms.
Dependency deduplication and held-frame isolation tests pass.

The final eight-save rerun (`gpu-repair-20261017-checkpoint-corpus`) matches seven
October 15 repaired images exactly. Save002 differs only in a 69x67 Guidebot
region: its prepared pose hash differs at frame 60 (same initial pose/position),
while other model pose hashes match. Repeated current captures match each other.
Reverting the pose-timing instrumentation did not remove the difference. The
source of this simulation/pose discrepancy is unresolved; do not call all eight
captures pixel-identical or dismiss it as random noise.

Contract suite: 5,761 checks pass. Current binary identity is recorded in the
latest capture/profile manifests. The inspection launcher remains
`C:\tmp\D3RendererVerification\scene-view-test\Launch Repaired Renderer.cmd`.

Still an inspection build: simulation visibility feedback and remaining migration
domains are not gameplay-qualified. No shadows added. No desktop items added.
