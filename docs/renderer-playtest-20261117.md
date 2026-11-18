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

### Gameplay hitches after loading

Compared with the preceding playable checkpoint (`4f43bb57`), alternating
before/after/after/before, loading each save directly in a fresh process at
1280×720, native resolution, AA and post effects off. These are CPU frame
markers in ordinary gameplay, not screenshot captures or GPU intervals.

| Save and frame | Previous preview | Hitch fix |
| --- | ---: | ---: |
| 006 (mirror), first | 112–115 ms | 29.7 ms |
| 007 (fog), first | 268–510 ms | 96–127 ms |
| 007 (fog), second | 42–52 ms | 5.8–7.9 ms |

Three gameplay costs were avoidable. Common shaders linked during the first
live draw, and were discarded on every level change despite not depending on
level assets. They now live with the GL context; common variants link during
loading. Release gameplay also ran synchronous GL self-tests intended for
development. Those remain available with `-scene-gl-audit`. Finally, the
terrain compiler built a heap-backed polygon for each terrain triangle, even
on indoor starts. It now emits triangles into contiguous material batches.
On 007, creating one unrelated bitmap on frame two invalidated all static
room materials; only changes to formats actually used in those decisions
now invalidate them. The independent bitmap audit is still available.

Time from marker initialization through the first mirror frame was
1.56–1.66 s before and 1.58–1.66 s after. Moving shader linking into loading
is not a claim that loading itself got faster. The capture-only inventory
log also reopened the same file for each printed line; that fix is **not**
included as a gain in the ordinary-play figures. The first fog frame still
exceeds a 60 Hz frame budget by several times.

Separate turning/forward captures measured steady frame-start intervals
(main thread pinned, frames 400–2199, two runs per build and save). The
99th percentile on 005 was 2.66–2.74 ms before, 2.82–2.93 ms after;
on 007 it was 3.17–3.60 ms before, 3.01–3.08 ms after. No interval exceeded
16.67 ms in those windows. These results do not establish a general
steady-flight improvement. Outdoor terrain, moving 005, fog 007, in-process
level restoration, and a mirror with SSAA2/MSAA4/AO/bloom each matched the
previous preview pixel for pixel at the sampled frames.

### Ready before gameplay (compared with 37f89f21)

On a successful level entry, after save restoration and with the simulation
clock stopped, the game now builds its immutable room/model material bindings,
terrain, image dependencies and fog boundaries. The first live frame can
reuse these bindings without treating the load as pose or camera history.
Procedural textures resolve their image handles without evaluating an extra
tick. Transient effects, cockpit and visibility observations stay in gameplay.

On a field-fog level the renderer also executes an isolated 8×8 offscreen
scene with an opaque, lightmapped wall and an additive, depth-reading quad.
It uses the normal visibility, pass and GL execution paths, then waits for
completion during loading. No swap or gameplay feedback. Only the shader
programs survive retiring this private scene. Linking alone did not prewarm
the executable pipelines; a stand-in GL draw with hand-built state left
another 220–234 ms of first-use work. The stand-in was discarded.

Native 1280×720, effects and AA off; ordinary gameplay with frame markers
on a noninteractive desktop. Each crossover leg used a fresh executable path,
same filename, without clearing the machine's shader cache. On 007, rendering
with fog disabled, exiting, and reloading with fog enabled reproduced a much
larger stall than simply loading the save from scratch:

| 007, first fog encounter after the clear run | Previous | Ready build |
| --- | ---: | ---: |
| First playable frame | 61–69 ms | 5.7–6.2 ms |
| Second playable frame | 428–454 ms | 2.0–2.7 ms |

Direct first launches on four independent paths (before/after/after/before)
took 3.25–3.31 s before and 3.23 s after from marker initialization through
the first image. First live frames were 58–68 and 6.4–8.1 ms respectively.
A repeat launch was about 1.3 s either way. Thus smoother input handoff,
not a claim of half a second saved from loading. The earlier clear run can
itself prepare the level's fog on the new build; the direct-launch check
charges that preparation. No general GPU, resolution or frame-rate claim.

Image checks: 000–007 with deterministic turning/forward input, outdoor,
an in-process 006→007 reload, and 006/007 at SSAA2 + MSAA4 with AO and
bloom, all compared at the same frame. All 12 RGB images matched the
previous preview exactly. A contract test checks that the loading scene
still exercises both draw passes, opaque depth sampling and fog topology.
