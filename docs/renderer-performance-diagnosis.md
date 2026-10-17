# Focused performance diagnosis

Measured on copied saveg003, 1280x720, SSAA1/MSAA0, AO/bloom/motion blur off,
fixed simulation step and seeded capture. Compared frames 15..84 (70 frames),
excluding startup and screenshot readback. This is a matched stationary scene,
not a general gameplay benchmark. Candidate/Previous Candidate were untouched.

Evidence under C:\tmp\D3RendererVerification:

- perf-diagnosis-clean.tsv: shipped implementation, frame telemetry only.
- perf-old-clean: copied source-baseline executable, identical clean settings.
- perf-cpu-breakdown and perf-upload-breakdown: diagnostic CPU stage markers.

Uninstrumented-stage new render CPU time: 214.461 ms average (total214.828ms).
Baseline render CPU time: 4.717 ms (total6.440ms). Reported GPU interval averages
4.255ms new / 2.299ms baseline; CPU stage timings independently locate the cost.
Simulation averages .265ms new / .343ms baseline. Queue waiting ~.003ms.
Stage-instrumented runs reproduce the slowdown (~203ms), so diagnostic markers
did not introduce the original regression.

Upload breakdown, ms/frame:

- CPU lookup-table construction: 89.211
- Prepared position assembly: .232
- GL buffer upload calls: .043

At capture: 82,093 bindings, 1,584 position pairs, 37,959 UVs, 2,693 light
indices. reflection_rebuild=0: static reflection payload caching is working;
this is not a recurring reflection-field bake or bulk GPU transfer bottleneck.

Presentation preparation totals104.409ms, including:

- Room material resolution28.132
- Resource preparation22.170
- Final freeze/validation/copy/material compilation/bounds16.631
- Model preparation15.385
- Extraction8.634
- Light preparation3.931

These are inclusive stages; do not add their parent totals to child totals.
All view visibility queries combined ~1.837ms/frame. They are not the dominant
problem in this scene.

Source findings:

UploadSceneFrame clears/repopulates five pointer-keyed maps for every surface
each new presentation, including empty metadata ranges. Binding addresses are
frame-local, forcing churn. PrepareGamePresentation reconstructs all room surface
state; model preparation recompiles source geometry to compare with persistent
assets; prepareImages compares source pixels; freeze repeats validation and
deep-copy/compilation work. Assets persist, but too much derived/static state
does not. Individual resource/material subcosts were not separately sampled.

Conclusion: a serious CPU data-lifetime/update-boundary implementation mistake,
not evidence that renderer-owned scenes, explicit views or AO radiance separation
must be abandoned. It is not just an expensive user setting or profiler switch.
Nor is it one tiny fix: even eliminating all89ms of lookup construction leaves
roughly100ms of presentation work. Do not claim a projected FPS as achieved.

Required corrective direction: stable indexed surface records and persistent
material/model/image derived state, explicit invalidation for actual changes,
compact per-frame instance/animation/light deltas. Preserve held-frame ownership
and availability to every view; do not restore camera-pruned scene extraction.
Validate with the same timing sample and pixel comparison before packaging again.

No corrective renderer implementation was applied during this diagnosis. Only
gated CPU timing instrumentation and out/profile-renderer.ps1 were added. The
interactive-test package is unchanged and still has the reported performance bug.
