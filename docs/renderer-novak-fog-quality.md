# Novak ceiling and fog quality

The default presentation of Retribution level 2 now extends the waste room's
live fog (184) into the shallow ceiling connector (183). This is the appearance
of the V3 preview, not a change to fog density throughout the game. It requires
the campaign, the level file and a portal connecting the two rooms, and respects
the fog setting and an explicitly fogged target. Gameplay/saved room flags are
not modified. The original clear boundary remains available to automated edge
tests with `-capture-frame N -scene-authored-room-fog`. The opaque fog projection
is still a separate opt-in optimization.

## Is there more banding than before the rewrite?

There is a small, repeatable difference in a smooth fog gradient, independent
of the ceiling correction. Captures used deterministic restores (seed 12345,
frame 190, saved headlight state) of 001/002/007 on the RX 6600. Compared with
reconstructed pre-rewrite `bfb59647` (binary A6286E93…, matching asset archive
6D8011DF…), at 1280×720 native and 2560×1440 with 2× SSAA, 4× MSAA, AO
and bloom. Steam/lighting also differ between renderers: a whole-frame old/new
pixel delta is not a measure of banding.

On the smooth green wall in 002 (1440p crop x1196:1618, y470:690), mean
absolute differences in the **green** channel, in 8-bit output codes:

| Fog path | vs. pre-rewrite | vs. current exact |
| --- | ---: | ---: |
| Default 320-wide field, 64 depth intervals | 1.086 | 0.916 |
| Same, with opaque projection | 1.120 | 0.952 |
| Twice the field width | 1.085 | 0.913 |
| 32-bit field storage | 1.039 | 0.864 |
| 32-bit storage, 128 depth intervals (diagnostic build) | 0.654 | 0.450 |
| Exact per-fragment integration | 0.273 | 0 |

The wall crop was bit-identical with and without the ceiling correction, both
on the default path and on the exact path. An additional native-resolution
fog-only capture removes material texture and lighting: on the back wall
(x578:752, y225:346) the mean absolute opacity-code error vs. exact was
0.923 at full precision/64 slices and 0.468 at full precision/128. Across
the full 1440p 001/007 images, only 0.034% / less than 0.001% of pixels
differed by more than two codes between default field and exact. This is not
a blanket claim about other saves, gamma settings, or what a player sees in motion.

The field stores the integrated fog at a series of distances. For a ray through
uniform fog the integral grows linearly, but it stops growing at an exit wall.
When the wall lies between two stored depths, linear interpolation across that
bend underestimates fog at the wall. As wall depth crosses successive intervals
the error repeats in faint contours. Its sign, spacing and reduction with twice
the depth resolution agree with the measured gradient. This is in addition
to ordinary 8-bit banding present in both renderers. A next experiment would
reconstruct just the partially traversed boundary interval rather than doubling
the whole 3-D field or hiding the contour with noise. No global sampling or
precision change is shipped by this checkpoint.

## Default banding correction (follow-up to 92b8d115)

For fog volumes sharing one color, the field now stores two straight parts of
the optical curve per depth record. A solitary entry or exit puts their join
at the actual boundary, not at a fixed slice distance. For multiple crossings
or clipped intervals the producer retains the old intervening knot; contributions
from different volumes with incompatible joins are combined at that knot.
Thus 32 intervals suffice instead of 64. This does not use dithering, change
the fog density, or route ambiguous cells through a new per-pixel geometry
trace. The existing angular edge repair is still applied. Scenes with genuinely
different fog colors retain the original 64-interval vector field. An opaque
projection may only reuse the linear side containing its receiver.

**Images.** On the same 1440p wall crop above, the mean green-channel error fell
from **0.916 to 0.138** against exact fog, and **1.086 to 0.380** against
pre-rewrite. In the full 1440p scenes, mean RGB error vs exact was about 0.059
(001), 0.049 (002), and 0.014 (007). The forced compact-bin overflow repair and
full-bin path matched the ordinary 002 result pixel-for-pixel in the native check.
The separate projected-fog oracle reported no missed edge repairs or over-budget
transmission differences in the sampled 001/002/007 and moving roof frames.
The field audit now also checks between stored knots using a fixed ray direction:
round-tripping a float world endpoint can perturb the ray across an edge. This
is sampled validation, not an assertion that all gradients are free of banding.

**Cost.** RX 6600, prior checkpoint vs corrected executable; hidden-desktop
restores, 360 frames/run, median unique GPU intervals for gameplay frames
100–335. Two four-run groups with reversed order. Native 720p save 002 cost
about **0.03 ms** extra. At 1440p SSAA2/MSAA4/AO/bloom, 002 moved both ways;
001 cost roughly 0.2–0.5 ms more. At 4K with motion blur also enabled, 002
cost about **0.3–1.1 ms** more across groups. These are whole GPU intervals,
not isolated shader costs. At 320×180 the ordinary single-color field uses
about 7.4 MiB more memory (33 RGBA16F layers instead of 65 R16F); it is
still independent of output resolution. This checkpoint trades some GPU
time for image quality, not a claimed speedup.
