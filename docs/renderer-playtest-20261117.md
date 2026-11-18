# First normal-play checkpoint

Branch: `codex/renderer-playable`, based on the shared `456ccc5c`. Tested code:
`9f0d9251`. Windows Release / RX 6600. This is a private playtest, **not** a
release qualification or replacement for Candidate.

The world, cockpit, HUD, foreground and automap no longer abort because the
inspection switch is absent. They use the same published scene as inspection;
real validation errors still fail. Inspection remains optional for diagnostics.
Normal gameplay no longer draws the inspection banner, including inside the map.
The private launcher starts at the menu with a copied pilot and save directory;
its read-only Windows settings mode reads the regular defaults but does not
write setting values back to the shared registry. Saves in this runtime are
real writes to the private copy. Background runs never register a Win32 raw
mouse handler, clip/release the desktop pointer, or hide the cursor.

## Checked on the packaged executable

- Clean build, all three CTest targets pass. Revision information belongs to
  the menu translation unit rather than every consumer of the public game header.
- Animated main menu; fresh starts of retail levels 1 and 2 through frame 1800;
  turning and firing from save003 with the headlight; reload003 → 007 (successful
  restore in the same process); video options over gameplay, change from no AA
  to SSAA2/MSAA4 through the normal menu close path, then return to flight.
- Images inspected for flight, levels 1 and 2, and video settings. Normal vs
  inspection flight differs only inside the banner rectangle (x12–512, y15–45).
- Paired 2560×1440 headlight-on captures with SSAA2/MSAA4: AO with and without
  color scissoring is byte-identical. Bloom-on reference/control is also
  byte-identical (bloom needs color in the margin and opts out).
- Re-run gameplay evidence: `tools/renderer-gameplay-smoke.ps1 -Runtime <private-runtime> -Output <new-dir>`.
  Default cases automate menu, level starts, flight, video settings and reload.
  These captures suppress real input; they do not test input latency or
  substitute for navigating the whole game by hand.

Local handoff: `C:\tmp\D3Playable1117\Play Renderer Preview.cmd`.
Evidence: `C:\tmp\D3Playable1117\shots\playtest`.
EXE SHA256: `86D753E45F308A38E57939C4FA4DF160436384BC5088C6C0A79C260D7898F1B8`.

## Fable's color overscan change

Integrated `5d5266cb` alone, not the worker thread, motion prepass or alternate
AO blurs. In 003, 1440p SSAA2/MSAA4, 107% AO overscan, no bloom/motion blur,
separate process GPU interval medians varied: reference 11.754, clipped 13.514,
clipped 13.475, reference 14.853 ms. Standalone runs do not establish direction.

A measurement-only build alternated the reference and clipped paths every 120
gameplay frames; a second run swapped phase. Both ran 1440 deterministic frames,
pinned to CPU2. For each matched block from 2 through 11, take its middle 80
frames (discard warmup and both switch edges), then take the GPU interval median.
Clipping won all ten blocks. Mean of the block medians: **15.048 → 13.858 ms
(-7.9%)**. This is a workload-specific GPU interval, not an FPS claim. Raw data:
`shots/cross-{0,1}/frames.tsv`. The alternating switch was not shipped.

## Still open

Longer manual play, level progression rather than direct entry, save creation
and restore from the menus, pause/automap transitions, live resolution/fullscreen
switching, and multiplayer are not signed off by this checkpoint. No new
24-case comparison against the old renderer was performed. No shadows,
experimental room adapter, or release deployment.
