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

## Mirror thing 2 follow-up (saveg006)

The saved player's headlight is **on**. With no input, the main lamp is pale
but its reflection was red-orange. Two independent regressions:

- The headlight shader uses a cone and radius, not the receiver normal.
  Publication rejected the headlight when the ship was behind a polygon's
  plane. This can switch the entire face off in a mirror although its pixels
  are still inside the beam. Keep the radius and directed portal tests; do
  not use portal-style facing rejection on headlight receivers. Main and
  reflected views consume the same light table. No extra reflected light.
- The legacy room renderer draws the lit mirror coating with the mirror depth
  surface and again with the translucent walls. One blend left the rewrite's
  mirror surface too dim and its reflection too prominent. Two identical
  source-over blends transmit `(1-alpha)^2` of the reflection. Evaluate that
  in one draw, giving only the coating's share to the main view's AO.

Reference: installed pre-rewrite `664aefc7`, which includes the August mirror
fix `b3bacdb4`. The older installation named “Release” (`ec109de5`) itself
renders the tip orange; it is **not** the good reference. Compared isolated
copies at 1280×720, per-pixel lighting, native/no MSAA, AO/bloom/motion blur
off, fixed 1/60 s, seed 12345. Original save SHA256:
`AE6D8719311EFE4767F27166564C5AEFBB8D0302F3FCEDB36A48F79F5AC85101`.

At frame 60 the tip at (916,302) was RGB (217,55,12) before, (188,176,143)
after, (189,177,144) in the reference. A separate camera-still control with
only the saved headlight bit cleared gave (188,42,4) after vs (189,42,4)
reference. At (1133,322), the mirror surface went from (37,17,6) to (60,28,10),
matching the reference. Compared the unmodified save again while applying
both `-capture-heading` and `-capture-forward` at frames 4,8,12,16,20,24,32.
In the fixed world rectangle [150,1250) × [68,468), mean absolute RGB-channel
error fell from 7.02–12.26 to 0.30–0.41 (0–255). Inspected the crops and
motion comparison; this is not sign-off on every mirror or the GPU corpus.
