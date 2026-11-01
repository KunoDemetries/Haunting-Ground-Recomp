# World editor

A standalone program (user requirement 2026-10-19: "keep the editor separate from the
game itself"). It reads the user's own disc image read-only and shares no code with the
game runtime; the only shared piece is the project's own ECMA-119 reader logic, which
the Python tools import and the C++ editor reimplements.

| Part | Location | Role |
|---|---|---|
| Viewer/editor | `editor/src/` → `hg_world_editor` (CMake option `HG_WORLD_EDITOR`, ON) | GLFW + OpenGL 3.3 room viewer |
| Research tools | `editor/hgworld/` (`python editor/hgworld ...`) | reference parsers, extraction, PNG/OBJ export |

Game-derived output (extracted files, textures, OBJ) must go outside the repository or
under the ignored `out/`; `archive.checked_output` enforces this.

## Stages

1. **Viewer (done 2026-10-19).** Browse all 168 rooms, fly freely, jump to each room's fixed
   cameras, wireframe/texture/translucency toggles. `--check-all` parses every room.
2. **Inspector (in progress).** Done 2026-10-19: navigation-mesh overlay (N), objects
   overlay (O: slot-2 shapes, slot-7 portals, slot-4 lights, slot-5 cameras), left-click
   picking with values in the title bar, geometry toggle (G), door destinations from the
   executable's door table (Enter follows a selected door). Remaining: slots 6, 12-15,
   item/enemy placement, and proving the probable meanings below from game code.
3. **Placement editing (started).** Lights, cameras, doors, slot-2 shapes and individual room
   meshes (furniture etc.; each mesh record has its own placement matrix) can be moved
   (arrows along the view direction, R/F up/down, Shift = 5 units, Ctrl+Z undo) and the
   room saved with Ctrl+S to `%LOCALAPPDATA%\HauntingGroundRecomp\mods\ST_xxx\ST_yyy.PAC`
   (or `--mods DIR`). Edits patch floats in place, so file size and layout never change;
   verified by a scripted move whose saved file differs from the disc copy only in the
   moved floats. Moving a door does not move its trigger triangles or navigation data, and
   moving a mesh does not move the navigation mesh (Fiona's walkable floor). In-game check:
   lifting two leaf meshes of room 0x21's plant removed exactly those leaves from the
   game's own frame (5,512 changed pixels). Room text can be edited with
   `python editor/hgworld text 21 --dump FILE` then `text-apply 21 FILE` (writes into the
   same mods folder, so Ctrl+E exports it); the text must still fit in the room's existing
   text slot (0-31 spare bytes per room, median 23), so a longer message needs other
   messages shortened. Unedited text re-encodes byte for byte in all 168 rooms.
3b. **Adding and removing (done 2026-10-19, user request).** `editor/src/edit.hpp` splits a
   room into its 17 sections, changes one and rebuilds the file (original section order,
   0x20 alignment, shared slots kept; unchanged rooms rebuild byte-identically, checked
   for all 168 by `--check-all`); rooms over the loader's 0x2A0000-byte buffer are refused.
   Insert/Ctrl+D duplicates the selection 3 units to the right, Delete removes it:
   - meshes: copies go to the end of their block (existing mesh numbers unchanged);
     deletion removes the record;
   - lights: record added/removed and the count updated;
   - cameras: added at the end; only editor-added cameras can be removed (the room's own
     cameras are referenced by number by paths and camera switching);
   - trigger shapes: added at the end of the table (reusing a free entry); removal clears
     the table entry so other shapes keep their numbers;
   - doors: cannot be added or removed (door numbers are tied to the executable's table).
   Ctrl+C copies a mesh with its texture; Ctrl+V pastes it 20 units in front of the camera
   in any room, reusing an identical texture or appending it to the room's bank. All 167
   banks store texture data contiguously in entry order (zero tail), so banks are rebuilt
   cleanly; pastes stay within the shipped game's envelope (<= 16 textures and <= 1,064,960
   texture bytes per room, room 0x2A). Scripted tests: `--move "dup KIND N"`,
   `--move "remove KIND N"`, `--move "copyfrom ROOM mesh N"`, `--move "paste X Y Z"`.
3c. **Object library (L key, done 2026-10-19, user request).** `editor/src/library.hpp`
   catalogues every room's placeable objects: small mesh records (< 25 units) whose
   bounding boxes touch are grouped; a group is kept when it is < 30 units across, has
   >= 16 triangles and <= 64 parts (walls/floors chain into larger groups and drop out).
   Identical objects (same records apart from placement, same texture bytes, same part
   layout) are listed once with every room they occur in: 3,611 objects from 168 rooms,
   built in about 2 s from the disc copies. The panel (`editor/src/ui.hpp`, own 5x7 font
   in `font5x7.hpp`, generated from hand-drawn glyphs) lists them with room, triangles,
   size and extra rooms; `/` searches the labels (e.g. "ROOM 021"); clicking selects and
   shows a rotating textured preview; Enter or double-click drops the object on the room
   surface under the crosshair (centre of the area right of the panel). The placed parts
   become one group selection (arrows/R/F move it, Del removes it, Ctrl+C copies it);
   Shift+click adds or removes meshes from a group. Pasted meshes have the high byte of
   their header flags cleared: values 0x15-0x1C mark meshes that room events show or hide
   (about 1,200 meshes; room 0x00's vase and its shards use 0x16/0x17). Evidence: the
   vase placed on room 0x21's bed was invisible in the game with its original flags and
   appeared in the game's frame once the byte was cleared. Test flags: `--library`,
   `--lib-select N`, `--move "place N [X Y Z]"`.
3d. **Walkable floor (K key, done 2026-10-19).** `editor/src/navedit.hpp` cuts the selected
   object's footprint (convex hull of its vertices from above, grown 1 unit) out of the
   navigation mesh for triangles within 3 units of the object's base. Triangles straddling
   the outline get their longest edge split (on both sides, so the mesh stays conforming)
   until edges are < 1.5 units; triangles whose centre is inside are disconnected from all
   neighbours (made unreachable) rather than deleted. Existing triangle numbers never
   change: a split keeps the number for one half and appends the other; new pieces inherit
   attribute and flag byte and are appended wherever their parent is listed (slot 7 portal
   lists, slot 14 groups). Slot 1 is rebuilt as exactly the open edges of reachable
   triangles, keeping the original edge kind where a new edge lies on an original wall.
   Costs: centroid distance (matches 186,224 stored costs bit for bit, the rest to float
   rounding, max 5e-6); unchanged edges keep their stored bits, so untouched triangles stay
   byte-identical (287 of 304 in the test). Verified: edge convention (neighbour k across
   v[k]->v[k+1], back links on all 218,536 links), slot-0 serializer reproduces all 168
   rooms (`--check-all`), and after a cut every link is symmetric, shares its edge and has
   the right cost. In-game: with a vase placed and the floor cut at (11, 0, -40) in room
   0x21, Fiona's recorded walk stopped at x ~ 4.6 instead of reaching x ~ 20.7 (pinned
   160.12M replay; her position found as vectors moving together on the nav mesh in RAM,
   e.g. 0x515430).
   J re-opens floor under the selection (same footprint and margin): triangles cut off
   earlier are reconnected by exact edge matching (cut then open restores the original
   topology: in room 0x21, 75 reconnected, wall list back to its 44 edges); then open wall
   edges inside the footprint are chained and filled by ear clipping: a closed loop (hole)
   as is, an open chain (notch) closed with a straight segment. A fill must wind like the
   game's triangles (all 77,754 have negative XZ area) and must not lie on existing floor,
   which rejects the room's own outline and U-shaped wall runs. `editor_nav_tests`
   (synthetic grids) checks hole fill, notch fill, outline refusal and cut/open
   restoration. Not yet demonstrated on a real furniture notch from the disc (room 0x21's
   floor runs under most of its furniture).
3e. **Play modded (F5, done 2026-10-19).** Exports and starts the unmodified game on the
   modded image (`editor/src/launch.hpp`): hg_game next to the editor (beta folder) or in
   build\Release (or `--game`), working folder = the folder holding `Haunting Ground (USA)`,
   TOC and memory card as the existing launchers use (beta `data\toc.bin` / `saves\`, else
   %TEMP%\haunting-toc-probe\synthetic-index-pattern.bin and Documents\Haunting Ground
   Saves). Verified: launched from the editor on an exported image and running at 31.7
   video updates/s.
3f. **Characters and creatures in the object library (done 2026-10-21).** The 115 PCK
   models (format below) are decoded by `editor/src/character.hpp` into static bind-pose
   geometry; 50 distinct models (identical geometry + texture bytes listed once) follow the
   3,611 room objects as `CHAR xxx_nnn` entries (search `/ CHAR` or `/ FIONA`), with a
   textured preview. Dropping one converts it to room mesh records (one per texture; opaque
   parts in block 0, the model's translucent parts such as hair cards in block 1 with header
   flags 1) and carries its 256x256 textures into the room bank through the existing paste
   path; vertex colour = the room's median block-0 colour, so the model is lit like the
   room's own prelit geometry. Model units are room units (Fiona 16.9 tall, doors 22).
   In-game evidence (pinned 157.8M replay, `--disc` image): Fiona's model placed at
   (-10, 0, -8) in room 0x21 (room grew to 2,707,168 of 2,752,512 bytes and was relocated)
   is drawn textured, with face, eyes and hair, in front of the playing Fiona (5,348 changed
   pixels). They are scenery only: no animation, collision or AI (K can cut floor under one).
4. **Getting edits into the game (done, no game change).** Ctrl+E (or `--export`) writes
   a copy of the disc image with every edited room patched in place at its own disc
   position and size (`%LOCALAPPDATA%\HauntingGroundRecomp\modded-disc\Haunting Ground
   (USA).iso`, or `--export-to`); the first export copies the ISO, later ones only patch.
   A `.patched-rooms.txt` manifest restores rooms that are no longer edited. Run the
   unmodified game with `--disc "<that iso>"`. Saved edits in the mods folder are loaded
   again when the editor starts. Evidence (2026-10-19): an exported image differed from
   the original in exactly the edited bytes (3 bytes for one float); the pinned 157.8M
   replay on it showed the edited value in the resident room PAC in EE RAM (light 0 y
   22.77 vs 12.77), and raising room 0x21's doors by 20 units removed door 0 from its frame
   in the game's own frame (9,355 changed pixels, all in the doorway). This also confirms
   the door placement rule (door drawn at portal A).
   Rooms may change size: a room that still fits its original sectors is written in place
   (directory size updated); a larger one moves into the free space after DATA.CVM's data
   (10,254 sectors, about 21 MB, up to the end of the outer volume) and the export updates
   the room's inner directory record (extent, size), the inner volume size, the CVM header
   size fields (0x1C total, 0x808 = total-0x80C, 0x830 data; big-endian) and DATA.CVM's
   outer directory size. Every export starts from the original metadata. In-game checks on
   the pinned 157.8M replay: (1) a grown room relocated to the free space loaded completely
   (its RAM copy differs from the file only in the same 439 slot-0 bytes the game itself
   rewrites in an unmodified run) and the duplicated leaves appeared; (2) removing room
   0x21's mattress mesh and adding a moved leaf copy showed both in the game's frame
   (7,822 changed pixels); (3) a mesh pasted from room 0x40 with its texture appended
   (room 0x21: 11 -> 12 textures) was drawn textured in front of Fiona. The source ISO is
   never written (export refuses to target it); SHA-256 of the user's untouched image:
   3f8c658eee1b35147672c597c0dbe8e36552172abf83571c52753eec9a50bcca.
5. **Geometry editing / import.** Replace or add meshes and textures.

## Formats (evidence)

### Character models — `O_xxx/xxx_nnn.PCK` (+ `.TEX`)
Checked against all 115 PCK files (`editor/src/character.hpp`; renders of every model on
the disc match the expected characters). u32 section count (4), then section offsets
(0 = absent). Section 0: `{u32 bones, u32 skinned table, u32 rigid table, ...}`; bones are
0x70-byte records from +0x30. Skinned table: u32 count, 12 bytes, count × 0x30-byte records
whose offsets are relative to the record: w0 vertex count, w1 `{int32 x,y,z, pad}` + int16
xyz deltas (position = running sum / 4096, model space, bind pose, feet at y=0), w2 int16 UV
(/32768), w3 normals, w4 weights, w5 bone indices (×4), w6 one strip byte per vertex
(1 = restart/no triangle, 0 = draw; every strip starts 1,1), w7 texture, w8 1 = translucent
pass, w9/w10 bone palette, w11 weights per vertex. The strip bytes were found by the
w6 table matching the long position steps; ignoring them leaves 679 stray triangles in
Fiona, using them none. Rigid table (eyes, earrings, whole props like the wheelchair):
0x20-byte records `{count, positions, UV, normals, strip bytes, texture, ?, bone}`.
Section 1, morph meshes (faces and hands; Fiona's body has a hole where the face goes):
u32 count, 12 bytes, 0x40-byte entries `{kind, count, UV, strip, frame table, bone, ?, ?,
translucent, ..., int32 pivot xyz at +0x30}`, offsets relative to the entry. Kind 0x10
(faces) has int32 strip words (0xFFFF8000 restart, 0x7FFF draw), kind 0xA (hands) strip
bytes; frame f = int16 xyz / 4096 + pivot at `table + table[f]` (Fiona's face: 1,610
vertices, 104 frames of 0x25C0 bytes). Morph meshes sample texture 0. Some entries are all
zero (skipped). Sections 2 (per-bone records) and 3 (motion) are not needed for static
models. A model's textures are the `.TEX` of the same name, else its folder's
`xxx_000.TEX` (FIN_001..006 share FIN_000.TEX).

All counts are over the whole USA disc unless stated.

### Room PAC — `ST_%03X\ST_%03X.PAC`
- Loader `0x120720(loader, room, buffer)` formats the name from room id `r` (directory
  `r & ~7`), rejects `r >= 0x110`, and reads into one of two 0x2A0000-byte buffers at
  `loader+0x99C0`. RAM dump (pinned 158M route): rooms 0x21 and 0x10 resident at
  0xFD0C80/0x1270C80, ids at `loader+0x3C0`.
- 0x800-byte header of u32 offsets (0 = absent); 17 slots used (max index 16), offsets
  nondecreasing and 0x20-aligned; an offset equal to the file size is an empty section.
  All 168 files parse.
- Room activation `0x11FFB0` resolves each slot to `loader+0x9980+4*slot` and hands
  them to subsystems: 0/1/16 → `0x17CC00(loader+0x3E0)`; 2 → `*(0x44E4D0)` vt+0xC;
  3 → `0x25D370`; 4 → `*(0x44E4C8)` vt+0xC; 5 → `*(0x44E4B8)` vt+0x78; 7 →
  `0x223B60(loader+0x1640)`; 8 → `loader+0x1640` vt+0x24; 9 and 11 → texture manager
  `*(0x44E4E8)` vt+0x10 (bank 0 and 0x15); 10 → stored at 0x1991EC4; 12/15 →
  `loader+0x6740` vt+0x10; 13 → `0x266CD0`; 14 → `0x2A88A0(loader+0x9360)`.
  Slots 11 and 12 often share one offset (96 rooms).

| Slot | Meaning | Evidence |
|---|---|---|
| 3 | Room geometry | renders as the room seen in game (below) |
| 0 | Navigation mesh | see "Room layout slots" |
| 1 | Navigation border edges | 14,717 of 14,728 records name a neighbourless edge |
| 2 | Trigger areas (polygons) and trip lines on the floor (exact use unproven) | parses in all 163 rooms; mostly on walkable floor |
| 4 | Lights (probable) | crosses land on the lamps/chandelier in room 0x21; exact 1/r and r² |
| 5 | Fixed cameras: 32-byte `{eye xyz, fov}{target xyz, 0}` pairs, 0xFFFFFFFF end | renders from them match game framing; 564 cameras |
| 6 | Camera paths (eye/target keyframe curves) | layout fits all 154 rooms; 219 of 430 paths start exactly at a slot-5 camera |
| 7 | Portals = doors | marker sits on the doorway; the slot-8 model placed at it fills the door frame |
| 8 | Door models, one per portal | chain count equals portal count in 131 of 132 rooms |
| 10 | Room message text (examine/dialogue) | decodes to readable English in all rooms |
| 9 | Room texture bank (`.TEX` format) | same parser as `.TEX`; passed to texture manager |
| 11 | Second texture-manager input (bank 0x15) | activation code |
| 16 | Per-navigation-triangle flag bytes | setup routine 0x17CC00 |
| 12-15 | unknown | — |

### Door table (executable)
`SLUS_210.75` at 0x3DCFC0: 16-byte entries `{u16 room, u8 door, u8 0, u16 value}` x 2 +
u32 flags, ending at room 0xFFFF (303 entries). Read by the door-graph object (vtable
0x46C3E0, global `*(0x44E568)`): 0x21C610 returns the other side's room (used by
0x39D070 to choose which room to preload), 0x21C6A0 the other side's door number,
0x21C550 maps an entry (< 0x190) to a room. Door number k of a room is slot-7 portal k
when k < portal count: 266 of the 269 portals are listed (unlisted: room 0x19 door 1,
room 0x33 doors 0 and 1). Higher door numbers (84 rooms) are exits with no door model;
where those exits sit is not in the room data (most likely per-room event code: stairs,
holes, scripted transitions). The door graph's builder 0x21B2B0 registers each entry's two
sides first-come-first-served, skipping entries whose bit is set in a 400-bit "disabled"
set (methods 0x21B160 test, 0x21B1C0 set, 0x21B210 clear; 0x21C760 loads it from 13 saved
words), so events can switch connections and that state is saved. Method 0x21BC60 tests
per-side flag bits (0x40 and 0x10 for side A, 0x80 and 0x08 for side B), probably lock
states. `value` is not a nav triangle near
the door and not a room message number; `flags` takes values such as 0, 1, 0x40, 0x41,
0x50, 0x51, 0xC1, 0xD9 (meaning unknown, perhaps locks/keys). Room 0x5A appears in the
table but has no room file. `python editor/hgworld doors` lists every room's exits.

### Room layout slots
- **Slot 0, navigation mesh.** `0x17CC00` stores u32 count and the triangles (stride 0x50)
  and clears the low mantissa bit of every vertex x/z. Triangle = 3 x `{x,y,z,1}`,
  `{3 x i32 neighbour (-1 = border), u32 runtime flags}`, `{3 x f32 cost, u32 attribute}`.
  Each cost equals the distance between the two triangle centroids exactly (pathfinding
  edge cost). The attribute colours contiguous surfaces (tile floor, stairs, upper floor
  in room 0x21); floor material is a hypothesis. 77,754 triangles.
- **Slot 16.** One byte per navigation triangle: bit 0 sets runtime flag 0x4000, bit 1
  sets 0x80000 (word +0x3C). Some rooms' slot 16 is shorter than the triangle count and
  the game reads past it; the editor treats missing bytes as 0.
- **Slot 1.** u32 count + 16-byte records `{triangle, edge, edge, kind 0..2}`: border edges
  (walls). Kind 2 runs along railings and furniture edges in room 0x21; meaning unproven.
- **Slot 2.** u32 offset table in 4-byte units (0 = absent entry), ending where the lowest
  record starts. Record = `{u32 kind, u32, f32 value, u32}` + 2-5 points `{x,y,z,1}`;
  kind 1 = closed polygon (most have 4 points and value 8), kind 0 = two-point line. The
  value is 8, 12, 58, 78, 0 or -2 (not always a height); the overlay extrudes by it.
  Several flat polygons sit beside furniture, doors and the painting in room 0x21. Over
  all rooms, 1,147 of 1,211 lines and 1,035 of 1,118 polygons have their centre on
  walkable floor, and few lie along walls, so they are things the player walks into or
  across rather than obstacles. Polygons cover only 5-50% of a room's floor (not camera
  zones); the most common is an 8 x 16 rectangle (examine-spot sized), others are large.
  Best current reading: event/examine trigger areas and trip lines; which event each
  fires is presumably chosen by per-room code. 2,329 records.
- **Slot 6, camera paths.** Header `{u32 count, u32 0, count x {u32 channels, u32 keys}}`
  padded to 16 bytes, then per path `channels x keys` 32-byte keys `{f32 time, f32 value,
  f32 in dt, f32 in dvalue, f32 out dt, f32 out dvalue, u32 mode, u32}`. All 154 rooms
  with slot 6 fit exactly; every path has 6 channels (eye xyz, target xyz). Room 0x21 path
  1 starts at camera 1's eye (-36.19, 15.66, 56.77) and eases over times 1-100. Handles are
  a third of the key interval (cubic Bezier; mode 3). What drives a path (time or the
  player's position) is not known. 430 paths; drawn in the objects overlay.
- **Slot 4.** `{u32 count, ambient rgb}` + 0x30-byte lights `{pos,1}{rgb, intensity}{range,
  1/range, range^2, 0}`. 882 lights.
- **Slot 7.** Byte-offset table ending at the lowest record. Record = `{u32 nav triangle,
  A xyz, B xyz, u32, f32 angle, u32, u32 count1, count1 nav triangles, u32 count2,
  count2 nav triangles}` (room 0x10's records have a second list; room 0x21's are empty).
  Across all 269 records the id is in its own first list, B lies on a listed triangle and
  the angle is cardinal. No destination room id is stored here; the door graph must come
  from game code or other data (open question).
- **Slot 8, door models.** Mesh records (same layout as geometry) in door-local space,
  one 0xFF-terminated chain per portal, starting at the first table offset (later table
  entries may point at the previous chain's terminator). 270 chains for 269 portals; room
  0x2D has one chain and no portal. Door k is drawn at portal k's point A, rotated about Y
  by its angle (`x' = x cos a + z sin a`, `z' = -x sin a + z cos a`): of the four A/B x
  ±angle conventions only this one seats the door in its frame (room 0x21, camera 3).
- **Slot 10, room text.** u16 index of the last message, then last + 1 u16 byte offsets
  from the slot start (the first equals the header size). Offsets are not ascending and
  may repeat (unused entries share another message's bytes; each empty message has its own
  0x00). A message runs to the next larger distinct offset, because control codes carry
  argument bytes that can be 0x00: room 0x02's "Pull the lever?" prompt uses 0x0E 00 00
  ... 0x0F FF FF "YES" 0x0F FF FF "NO". Control bytes: 0x01 line break, 0x02 page break,
  0x0E/0x0F choice codes; 0x03, 0x04, 0x0A, 0x10-0x14, 0x1E, 0x81, 0xFF also occur
  (meaning unknown). Re-encoding with the original sharing reproduces every room's slot
  byte for byte. `python editor/hgworld text 21` prints a room's text.
- **Slot 14, switchable navigation regions.** `{u32 count, count x u32 byte offset}` then
  groups `{u32 n, n x nav triangle}`. Class vtable 0x46DB40 (object at loader+0x9360,
  set by 0x2A88A0): methods 0x2A8590/0x2A85B0 set/clear runtime flag bits on every
  triangle of a group, 0x2A86B0 tests membership, 0x2A85D0/0x2A8640 set/clear one
  triangle (nav mesh object `*(0x44E570)`, 0x50-byte triangles, word +0x3C). Events use it
  to open or block parts of a room's floor.
- Python (`editor/hgworld/layout.py`, `mesh.door_models`) and C++ agree on every total
  (3,061,466 triangles including door models).

### Texture bank — `*.TEX`, room slot 9
u32 count, 12 zero bytes, `count` × `{u32 psm, u16 w, u16 h, u32 size, u32 offset}`
(offset relative to the entry). Data = indices (PSMT8 0x13 or PSMT4 0x14) then a 256-entry
RGBA palette (alpha 0..0x80). `size = (palette_qwords << 16) | index_qwords` holds for all
1,811 entries. Palettes use GS CSM1 order (entries 8–15 ↔ 16–23 per 32); decoded images
are smoother with it in every sample. 396 banks (229 files + 167 rooms) parse.

### Room geometry — slot 3
Eight u32 block offsets. Blocks 0, 1, 2, 4, 5 are used (167/109/125/13/17 rooms), all
with the same records, each block closed by 16 bytes of 0xFF:
`{u32 count, u32 texture, u32 flags, u32 extra}`, 4×4 float matrix (rows; translation in
column 3), `count` UV float pairs, `count` RGBA (alpha 0..0x80), `count` × `{xyz float,
u32 strip}`; arrays padded to 16 bytes. `strip` 0x8000 starts a triangle strip, 0x7FFF
draws (GS kick convention). `texture` indexes the slot-9 bank (floors/walls get the
right images; 0xFFFFFFFF = none). Blocks 1/2/5 carry translucent vertex alpha (drawn
blended; e.g. the fireplace glow in room 0x21). Space is Y-up. Totals: 3,037,823
triangles; Python and C++ parsers agree exactly. `flags`, `extra` and the per-block
meaning (beyond translucency) are not yet known.

## Commands
```
hg_world_editor [path to DATA.CVM or the .iso] [--room HEX] [--camera N]
hg_world_editor --check-all
hg_world_editor --room 21 --camera 1 --screenshot <outside-repo>.ppm [--nav] [--objects] [--top] [--no-geometry] [--pick NX NY]
python editor/hgworld rooms | room 21 | text 21 | doors | extract ST_000 --out <dir> | textures room:21 --out <dir>
python editor/hgworld path ROOM X Z --door N | --to X Z | --list   (nav-mesh route as autopilot script lines, HG-DIAG-123; editor/hgworld/route.py: A* over slot-0 triangles, string-pulled with a wall margin)
hg_world_editor --room 21 --mods <dir> --move "door 1 2 0 0" --save [--export --export-to <iso>] --screenshot <file>.ppm   (scripted edit test)
hg_game --disc "<exported iso>" ...   (play the edits; same arguments as usual otherwise)
```
Viewer keys: PgUp/PgDn (or [ ]) room, 1–9 room camera, right-drag look, WASD + Q/E
(Space/Ctrl) move, Shift fast, Tab wireframe, T textures, X translucent, B brightness,
N navigation overlay, O objects overlay, G hide/show room geometry, left-click pick,
Enter follow the selected door, arrows/R/F move the selection, Ins/Ctrl+D duplicate,
Del remove, Ctrl+C/Ctrl+V copy/paste a mesh or group (works across rooms), Shift+click
group meshes, L object library (rooms' objects, then characters/creatures), K cut / J re-open the walkable floor under the selection,
Ctrl+Z undo, Ctrl+S save, Ctrl+E export the modded disc image, F5 export and play. Overlay: cut (unreachable) floor is dark red.
Overlay colours: navigation triangles by attribute (white/black = slot-16 bits), border
edges red/green/blue = kind 0/1/2; shapes yellow (polygon)/orange (line); portals and
their trigger triangles magenta; lights as crosses with range circles; cameras cyan.
