# Opaque fog projection (opt-in)

`-scene-display-fog` enables an experimental scalar room-fog path on the main perspective view when SSAA and the AO depth prepass are active. Omit the switch for the reference. It does not change the density, color, or resolution of the volumetric field. Both paths use the same room boundaries. The depth prepass already exists; fog does not request another geometry pass.

## What is shared

The reference reads four fog columns per shaded fragment, interpolates at the receiver's depth, and traces the actual boundary geometry if the columns disagree strongly. Doing that separately for every supersample of a wall is expensive.

The new compute pass uses display-resolution opaque depth to choose a useful depth interval for each fog column. It checks linear fits against the stored knots. Four neighboring columns must agree on a valid interval and must not require the reference boundary repair anywhere in it. The pass fits a screen-space plane at each end of the interval, checks the eight corner errors after half-precision packing, and writes a single 16-byte record. A typical record image is **321 × 182**, independent of whether the output is 720p or 4K.

A solid fragment fetches one record and evaluates it at **its own** screen position and depth. An empty record or depth outside the interval takes the original path. A wall behind a silhouette cannot donate its fog to a foreground fragment at an unsupported depth. Blends, steam, exterior fog, transient screen-expanded geometry and mixed/signed/HDR-color fields keep the reference calculation. The projection is rebuilt per view, never reprojected from a previous frame. The result is an approximation, not a promise of bit-identical pixels.

Two earlier layouts were removed: a coefficient and depth per display pixel, and a coarser display tile with a local depth slope. The full-size table alone took about 0.70 ms to build at 4K; fixed screen tiling often straddled the fog lattice and had to fall back even on smooth walls.

## Measurement and visual verification

Base: `a789dce7`, RX 6600. Deterministic save restores on a hidden desktop. 2× SSAA, 4× MSAA, AO and bloom; the 4K preset also uses motion blur. The timing build changed paths every 24 frames and inverted their order in the second run. GPU timestamps bracketed the actual opaque pass; whole-frame GPU intervals were recorded separately. Startup, transitions, duplicate asynchronous samples and capture frames were excluded. Benchmark-only program duplication and pass queries are not present in the playable executable.

| 4K save | Opaque pass, paired median change | Cost to prepare records | Whole-frame results |
| --- | ---: | ---: | --- |
| 001, smoky room | −0.78 ms (6/6 pairs faster) | 0.042 ms | −0.35 ms in this run |
| 007, fog room | −0.53 and −0.71 ms in two series (5/6 pairs faster in each) | 0.047 ms | +0.58 ms in one series, −1.41 ms in the other |

Whole-frame intervals include unrelated rendering and scheduling. A reference-versus-reference control in 007 moved −0.47 ms on that metric. The narrower conclusion is defensible: the opaque stage got cheaper in these high-resolution scenes; the whole game did not show a uniform FPS gain. The experiment remains opt-in.

The diagnostic switch `-scene-display-fog-oracle` with a capture samples one in 64 raster positions on eligible opaque geometry. At cache hits it also runs the reference, counting missed edge repairs and transmission differences above 0.0015. It still outputs the candidate color. `-scene-display-fog-audit` reports table construction time and occupancy. Neither diagnostic reads back during ordinary play.

- 4K 001: 496,011 hits / 897,618 sampled eligible fragments; worst transmission difference 0.000366.
- 4K 007: 494,907 / 712,444; worst difference 0.000207. Neither save missed a reference edge repair.
- Moving fog roof, frames 150–153: no missed repairs or over-budget errors. Across the four final RGB comparisons, no channel differed by more than 2/255. The reference and candidate were also inspected visually.
- Native/no-AO, forced vector fog, and the mirror save with dynamic shadows enabled matched their respective reference screenshots exactly. These are fallback checks, not claims about every possible level.
