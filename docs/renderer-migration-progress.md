# Renderer migration checkpoint

## Scope and status

The user approved renderer rearchitecture and background save-game comparisons,
with an explicit gate: **do not touch shadows until the new renderer is verified**.
No renderer replacement or shadow implementation has been made at this checkpoint.
The completed work is baseline preservation, a clean-source build, and validation
of the screenshot comparison harness. The migration and full acceptance corpus
remain unfinished. Do not mistake these baseline results for new-renderer results.

## Locations and provenance

- Original checkout: `C:/Repos/DESCENT-3MASTERED`, master `664aefc`.
- Implementation worktree:
  `C:/Users/Jeff/.codex/worktrees/renderer-rearchitecture/DESCENT-3MASTERED`.
- Branch: `codex/renderer-rearchitecture`.
- Baseline commit: `bfb59647a3c3b4a451c264b3941c441573ecc061`.
  This preserves copies of the original working tree's 18 modified/untracked
  files, including unrelated user work. It is not a commit in the original
  checkout. Those unrelated changes must not be attributed to this migration.
- All runtime/capture artifacts: `C:/tmp/D3RendererVerification`.
- `baseline-runtime` is a copy of Candidate, not the live installation.
- `source-baseline` is a separate copy with the baseline source build's executable
  and shader HOG. Its source includes the preexisting capture-headlight change.
- `seeds` contains copied pilot and all eight saves. The harness restores copied
  pilot/save/counter data into the isolated runtime before each capture. The
  original pilot/saves are never used as writable runtime data.

Candidate executable SHA-256, verified unchanged after testing:
`EDAB17AAD7BA9A60EFCA54F72789F3D173446B916EB1631FB18FCA9FFCA1EFD4`.
Candidate shader HOG SHA-256, also unchanged:
`7BF1AC01E9E178E5D6D382C5D2013100F5A8366EE4C86D5F784189828EC01CCB`.
Prev Candidate was not touched. The cancelled cockpit refinement was not resumed;
its existing source/assets were preserved, and the test toggle was not enabled.

## Build

The source-baseline Release build completed with existing compiler warnings.
From a VS 2022 x64 developer environment in the isolated worktree:

```text
cmake -P tools/UpdateRevision.cmake lib/gitinfo.h
cmake -S . -B out/build/renderer -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/renderer --target Descent3Mastered --parallel 8
```

The first step is currently necessary on a fresh checkout: dmfc can compile
before revision_check creates gitinfo.h. This existing build dependency issue
was not disguised by copying an old generated header. lib/gitinfo.h is generated
and is intentionally not committed with this checkpoint. No install command ran.

## Capture harness

`tools/renderer-capture-corpus.ps1` runs a copied installation in hidden/background
mode, logs successful headlight/seed activation and shutdown, hashes executable,
HOG, pilot and saves, and records commands/settings/results. Output directories
are immutable. Global settings are read and checked for change, not rewritten.
The script accepts only runtimes directly under the dedicated verification root.
It times out and kills only a process it launched itself.

Example from the worktree:

```powershell
.\tools\renderer-capture-corpus.ps1 `
  -Runtime C:\tmp\D3RendererVerification\source-baseline `
  -Seeds C:\tmp\D3RendererVerification\seeds `
  -Output C:\tmp\D3RendererVerification\unique-new-output
```

`tools/renderer-compare-corpus.ps1` requires matching save hashes, pilot, profile,
frame, RNG seed and registry settings before reporting per-pixel RGB errors.
It dispatches image reading to Windows PowerShell/.NET Framework for System.Drawing
compatibility; any execution-policy override is limited to that child process.
It has no automatic visual acceptance threshold.

Captures use 1280x720, gameplay frame 60, dt 1/60, seed 12345, forced headlight,
per-pixel lighting, no audio and no motion blur. The effects profile explicitly
uses 4x MSAA, 2x SSAA, AO and bloom. Other live preferences are recorded in
settings.json; if those preferences change, do not compare corpora as equivalent.

## Verified results

The eight headlight-on source-baseline frames were visually inspected, including
portals, room geometry, bright bloom, green fog, objects/effects, original Pyro
cockpit/HUD, the stalactite, and close geometry. They are all interior views; this
does not cover terrain, every ship, module callbacks, all blend modes or temporal
behavior. Those remain required by the rearchitecture plan.

The authoritative repeated effects runs are `seeded-effects-a` and
`seeded-effects-b`. All 16 captures loaded, rendered, saved and exited normally.
The comparison report is `seeded-repeatability.json`.

| Save | Mean RGB absolute error (0-255) | Pixels with any channel difference >2 |
| --- | ---: | ---: |
| saveg000 | 0.000001085 | 0% |
| saveg001 | 0 | 0% |
| saveg002 | 0.001082538 | 0.015842014% |
| saveg003 | 0 | 0% |
| saveg004 | 0 | 0% |
| saveg005 | 0 | 0% |
| saveg006 | 0 | 0% |
| saveg007 | 0 | 0% |

saveg000 differs at three pixels by at most one channel level. saveg002's maximum
channel difference is six. This residual is measured but not conclusively
diagnosed; do not hide future coherent artifacts behind a global noise threshold.

## Lessons from setup (preserve this evidence)

- A long worktree runtime path hit a legacy path-length limit before level load.
  Only the copied test installation was moved to the short verification root.
- Native WinMain returns 1 on normal completion. Accept it only together with the
  capture and clean-shutdown log markers; zero is not the normal success code.
- The preserved Candidate did not log support for capture-headlight. Its first
  image is not a valid forced-headlight golden. Use the rebuilt source baseline.
- Unseeded runs showed major differences in saveg003 and some in saveg002.
  Existing `-capture-seed` support resolved nearly all of that variation. Earlier
  `source-effects-*` and `source-clean-a` runs lack this control and are diagnostic
  artifacts, not the accepted deterministic baseline.
- Save loading increments a sibling .cnt file. Early tests loaded copied seeds
  directly; the harness was corrected to restore/load runtime copies, and seed
  counters were restored from the original save directory. Original saves and
  counters were never loaded by the test process.

## Next work / no completion claim

Finish stage 0's remaining view-feedback/module/rare-material inventory, missing
coverage fixtures and performance budgets. Then replace the top-level core scene
contract and migrate its consumers as planned, with no old-world fallback.
Keep the baseline runtimes/captures frozen while the development branch is broken.
Renderer verification and deletion audit must complete before any shadow work.
