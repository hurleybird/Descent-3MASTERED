# Gameplay observation migration

Status: inspected compatibility query and transaction implemented; gameplay
commit remains disabled. Do not equate the inspection report with acceptance.

## Ownership and timing

The game publishes simulation observation bounds separately from model bounds.
The game collects main/auxiliary reports in a frame-owned transaction, then seals
it after world views and before foreground composition. No GPU pass writes
Objects, Rooms, automap discovery, or AI lists.

The previous implementation resets AI_NumRendered at GameRenderFrame entry.
AIFrameAll consumes the previous rendered list in the following simulation
iteration. A commit must preserve that interval, not notify AI during drawing.
Object handles include allocation generation; stale-frame/scene/handle reports
are rejected. Exact view replays are idempotent; conflicting replays are errors.
First-observation order is retained, not resorted by allocation handle. Automap
rooms and effect-eligibility rooms remain independent unions.

## Source census

- RenderRoom marks AutomapVisMap and room.last_render_time after portal admission,
  not after pixel visibility. Empty/backfacing rooms can still be observed.
- BuildRoomList admits all source-room objects. Door rooms bypass aperture
  bounds; other rooms use simulation min_xyz/max_xyz against the portal window.
- CheckToRenderMineObjects additionally excludes RT_NONE, the camera object and
  observer piggyback target; it tests a simulation sphere (streamer override),
  with an electrical-weapon exception. Posed model bounds are not this contract.
- RenderObjectInternal rejects dummy/hidden attachment children, then marks
  OF_RENDERED and renderframe and queues AI. Transparent replay must not repeat
  these writes. OF_SAFE_TO_RENDER is a traversal scratch bit, not persistent
  observation output to preserve in the replacement.
- PostRender processes separated polygon objects front-to-back, then other
  objects back-to-front. The candidate query retains these two order classes.
- Mirrors run RenderRoomObjects on reflected objects, force admission, and
  update room.last_render_time and object/AI bookkeeping. RenderMirroredRoom
  does not itself mark automap discovery. Mirror observations require a distinct
  explicit policy, not reuse of a main view's feedback bits.
- RenderAllTerrainObjects adds distance, terrain-occlusion and portal tests;
  RenderTerrain updates Last_terrain_render_time. Viseffect creation tests
  room/terrain timestamps against a five-second interval.
- Auxiliary world views historically reach the same room/object side effects.
  Save-thumbnail/module/automap policy and mirror ordering still need the full
  call-site/deletion audit before enabling writes.

## Implemented inspection boundary

ObjectState carries immutable observation bounds, radius, eligibility,
electrical-style unbounded admission and the legacy observation order class.
PrepareGameModelInstances resolves these from game data without drawing.
RoomState names door-room aperture bypass explicitly.

InspectSceneObservations is a pure candidate query over published inputs and
portal windows. It does not consume renderView's triangle list or model LOD.
Its sphere uses the eye plane rather than the geometry near plane. Missing
inputs, unknown spatial roots and unsupported view contracts are rejected.
Exterior/reflection coverage gaps are explicit; there is no all-visible terrain
fallback. ObservationProbe collects/logs candidates without committing them.

## Qualification still required

- Compare candidate room/object sets and order with legacy traces, including
  doorway crossings and mirror/auxiliary views.
- Qualify boundary differences from legacy IsPointVisible (its approximate FOV
  normals, cached camera state and far clip); the candidate uses explicit
  view-local camera planes. This is not asserted bitwise equivalent.
- Publish observer/piggyback exclusions, terrain occlusion/distance and mirror
  policies; account for legacy postrender capacity/order ties explicitly.
- Verify room/effect timestamps and next-iteration AI notification behavior,
  pause, reload and allocation reuse before removing CommitGameVisibility's
  compile barrier. No pending renderer domain has been cleared by this work.
