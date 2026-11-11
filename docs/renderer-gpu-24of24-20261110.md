# Renderer GPU comparison — November 10, 2026

The completed eight-save, three-configuration sweep is **24/24 lower GPU
intervals** versus the reconstructed pre-rewrite baseline. All six cases from
the two previously difficult saves also win when renderer order is reversed.
This is a measured benchmark milestone, not whole-game release qualification
or a claim that every architectural opportunity is resolved.

## Method and limits

- RX6600, 1280x720 output, headlight on, uncapped, render thread pinned to CPU2.
- Each process runs1500 frames; table uses the median of800 samples, frames400–1199.
- Native: SSAA1/MSAA0, AO/bloom/motion blur off. SSAA: SSAA2/MSAA0, same effects off.
- Full: SSAA2/MSAA4, AO/bloom/motion blur on.
- GPU timestamps include submission gaps; these are not isolated hardware-busy counters.
- Old is reconstructed `bfb59647`, not the deleted original executable. Its
  SHA256 is `A6286E9315A60F3C93B3A764C93A808B8FA385924ADDFACE1B573433374F3DA4`;
  assets are `6D8011DF08046995C8ED4DFA8911CBF91725883B47161153407317BD90B40775`.
- New source through `5b0e9bd`; executable SHA256
  `13B51A7D1C5D167764D6721DA52280B64024895845C57F7B95DE145022440388`;
  assets `3524C6A189CE9CC0E484FA01AB3BA05197D439EE7BAB12A6EDECA79C4F237706`.
- Existing fog/headlight/effect differences prevent strict old/new image equivalence.
  Optimization image tests below compare the new renderer with its own reference paths.
- Evidence folders: `C:\tmp\D3RendererVerification\gpu-mx-{case}-{old,new}-{save}-ownership-20261110`.

## GPU intervals in milliseconds: old → new

| Save | Native | SSAA2 | Full effects |
|---|---:|---:|---:|
|000|0.3165 → 0.1330|0.7350 → 0.3680|2.5360 → 1.5790|
|001|0.7340 → 0.5630|1.4270 → 1.3230|3.1590 → 3.0680|
|002|1.3980 → 0.3540|1.7390 → 0.8190|3.1940 → 2.4160|
|003|0.5690 → 0.2540|1.0650 → 0.6110|2.9540 → 1.9400|
|004|0.3520 → 0.1460|0.8110 → 0.4640|2.5270 → 1.7210|
|005|0.2380 → 0.1110|0.5470 → 0.3160|2.3670 → 1.5115|
|006|0.3260 → 0.1600|0.7530 → 0.3740|2.5130 → 1.6750|
|007|0.4030 → 0.3660|0.8920 → 0.8350|2.8120 → 2.4620|

Full sweep reductions range2.9–74.7%. These percentages are not FPS gains.

Reversed-order repeat (`ownership-reverse-20261110`):

| Save | Native | SSAA2 | Full effects |
|---|---:|---:|---:|
|001|0.6535 → 0.5625|1.3285 → 1.3120|3.1570 → 3.0645|
|007|0.4030 → 0.3650|0.8900 → 0.8300|2.8030 → 2.4650|

**001 SSAA remains a narrow margin**:1.2% in the reversed repeat. The old
renderer has varied substantially between runs. Do not characterize all24
as large or equally robust wins. Third paired check (`ownership-check3-20261110`)
also wins:001 SSAA1.3605 → 1.3160ms (3.3%).

## CPU render interval cross-check

The same fresh native runs have lower CPU render intervals on all eight saves.
These are wall-time render intervals, not isolated CPU-cycle measurements;
driver stalls and GPU backpressure can contribute. Do not add them to GPU times.

| Save | Old ms | New ms | Reduction |
|---|---:|---:|---:|
|000|1.8580|0.7670|58.7%|
|001|2.0720|0.7350|64.5%|
|002|3.0495|1.5920|47.8%|
|003|2.0795|1.5415|25.9%|
|004|0.7200|0.3110|56.8%|
|005|0.7950|0.6840|14.0%|
|006|0.7155|0.4160|41.9%|
|007|0.9945|0.7980|19.8%|

The November3 old/new/new/old CPU corpus already recorded8/8 wins,13–48%,
in `renderer-recovery-checkpoint.md`, including at commit `e3e1225`.
October21 results are historical, not the latest CPU baseline comparison.

## Changes that closed the remaining gaps

1. `0bea594`: compile identity-transfer reduction without redundant powers;
   preserve the same filter and a general-gamma program.
2. `9c1b391`: final world reduction produces display color directly when no
   linear-color consumer remains; no intermediate fullscreen color copy.
3. `f5126ef`: compatible native display color changes owner without copying
   pixels. Framebuffer/depth/stencil/auxiliary attachments retain their owners.
4. `5b0e9bd`: prepared pass schedule proves when opaque depth remains read-only.
   Supported contexts borrow that depth rather than copy it. Later depth writers,
   unsupported contexts and unsupported formats retain the independent snapshot.

No shadows, cancelled cockpit changes, Candidate, Previous Candidate, or main
checkout changes. Changes pushed to `codex/renderer-rearchitecture`.

## Qualification

- Root integration3/3, including positive/negative read-only schedule tests.
- Native color transfer: all8 native captures and native AO007 exactly match;
  GPU ownership audit checks content, retained depth, rejection of mismatched
  extents, and independent destruction.
- Depth borrowing: all8 SSAA2 captures and SSAA4/AO007 exactly match copy references;
  long1500-frame001/007 paired screenshots also exact.
- Depth borrowing is active001/002/007;003 correctly uses the copy fallback.
- Settings/gamma/SSAA transitions, auxiliary003/006 and gameplay reload pass.
- Direct display reduction: all8 frame60 captures and paired gamma-transition
  captures exact. Earlier1500-frame probe had33/49 pixels differing by one code.
- Earlier identity-transfer specialization had isolated one-code differences;
  do not describe the whole optimization series as universally pixel-identical.
- Headlight-on output inspected visually. Private runtime restored to both current
  build assets after comparisons; no test process left running at sweep end.

## Architectural review follow-up

The corrected Fable review identifies real remaining issues: whole-level
publication scans, fine-grained draw records, and limited particle batching.
Persistent assets/COW avoid rebuilding everything but are not a mutation-only
publication contract. Adjacent identical-record merging does not replace
per-face GPU tables and broader compatible batching. Those are substantive
future design candidates, not invalidated by this benchmark milestone.

The old note saying an experiment added no SSBO lookup describes that experiment,
not a permanent ban. CPU-boundedness depends on configuration. MSAA count alone
does not establish fragment-shader invocation count. Moving opaque fog after
reduction is a real candidate, but resolved depth is not sufficient by itself to
preserve mixed-depth edge coverage; it needs a deliberate coverage/transparency
contract and comparisons against the accepted non-shimmering output.
