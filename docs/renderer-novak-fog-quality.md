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
