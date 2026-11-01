# Project rules and map

## Goal and authority
Build an independent ahead-of-time static recompilation of Haunting Ground (USA)
for Windows, Linux, and other desktop systems where the dependencies permit.
Use OpenGL for graphics. The user's instructions and root SKILL.md govern work.
This project is solely a static recompilation, not a general-purpose PS2 emulator
(user reaffirmed 2026-09-13). Translate the original game code ahead of time to
native code; implement only independently derived runtime services required by
that compiled program. Never introduce runtime instruction decoding, an
interpreter, JIT, emulator execution backend, or an emulator dependency.

## Hard rules
- Never copy ps2recomp implementation into this project. The user permits looking at the shared HG configuration for context and ideas; independently verify relevant facts from original inputs/specifications and write our own implementation.
- For new non-rendering stops, check the user-supplied HG configuration lists for relevant leads while continuing diagnostics. Independently verify and try applicable leads; if they do not help, continue tracing. Rendering is excluded from this reference workflow.
- The user explicitly prohibits copying ANY rendering-related material from the shared HG/ps2recomp project, including code, configuration, mappings, patches and algorithms. Derive rendering independently from original inputs and hardware specifications.
- Active-root renderer exception authorized 2026-09-23: rendering may be implemented natively and may differ internally from literal PS2 GS execution when that preserves game-visible behavior and visual output. The frozen `v1/` tree is the pre-rule-loosen reference and must not be modified. This exception does not permit copied external renderer implementations, guest-clock changes, skipped guest-visible framebuffer/texture/readback behavior, or relaxed CPU/EE/VU/game-logic correctness.
- For the native renderer, exact internal GS VRAM/state identity is reference evidence rather than the sole acceptance criterion. Any intentional divergence must be confined to renderer-private representation; guest-observable reads/materialization, ordering needed by the game, visible frames and explicit faults must remain correct and be validated rather than silently ignored.
- The user permits scanning PCSX2 for timing (2026-09-13) and comparing its
  rendering approach with original PS2 behavior (2026-09-19).
  Do not copy its code, algorithms, comments or implementations. Treat inspected
  settings/implementation as leads; independently verify behavior from the original
  executable and hardware specifications. Record provenance. PCSX2 remains an
  external reference only, never an execution backend or dependency.
- The user authorizes access to the local PCSX2 installation and supporting
  files under `emu/` for memory-oracle checks. The current working debugger/oracle
  package is `emu/PCSX2-MCP-v1.0.0-win64/` with pinned `pcsx2-qt.exe`, DebugServer
  at `127.0.0.1:21512` and PINE at `127.0.0.1:28011`. Do not substitute the older
  `emu/PS2 emu/` folder when using the MCP oracle. This authorization persists
  across continuations; the independence and external-reference-only restrictions
  above still apply.
- No runtime instruction interpreter/JIT fallback. Unsupported translations and
  unimplemented hardware must fail explicitly, never silently become no-ops.
- Keep game files, BIOS, emulator files, memory captures and generated game-derived
  source out of version control. Consume the user's local dump at build/run time.
- Temporary files belong outside the repository. If a temporary image is necessary
  inside it, use only `UNEEDED images/` (the user's existing spelling).
- On Windows, if MSBuild/CL fails because both `PATH` and `Path` are present, do
  not use the Python full-environment normalization workaround
  (`env={k.upper(): v for k,v in os.environ.items()}`) or an equivalent command
  that rewrites and forwards the entire inherited environment. That invocation
  has triggered the outer tool safety review. Use a normal clean Visual Studio
  developer/build environment with a single PATH entry and the existing
  single-worker (`/m:1`) build workflow instead.
- Maintain this file as the rules and project map, not a growing work log.
  Refresh `docs/PROGRESS.md` every 20–30 minutes during active work, before handoff,
  and at the end of a work session. Update this map when structure/rules change.
- On handoff use SKILL.md's complete briefing structure and outer-fence rule.
- Every code handoff to the web/Codex Web must also include the complete current
  `AGENTS.md` so the receiving worker gets the project rules and constraints with
  the handoff. Do this on every such handoff, even when the code/task context is
  otherwise self-contained.
- Do not claim playability or cross-platform verification without evidence.
- Label and account for diagnostic code and experimental substitutes in
  `docs/DIAGNOSTICS.md` using stable `HG-DIAG-NNN` identifiers. Record the source
  location, purpose, activation/default, guest-state effects, measured or unknown
  host cost, and removal/retention criterion. Link identifiers from source when
  adding or changing a probe. Update the inventory and active entries in
  `docs/PROGRESS.md` during optimization and before handoff. Do not leave temporary
  watches, logging, synthetic inputs or timing overrides disguised as normal
  behavior. Keep disabled diagnostics cheap; preserve explicit correctness faults.
- The user explicitly requested continued work until told to stop. Keep advancing
  through milestones without treating a successful checkpoint as a stopping point.
- The user requested faster progress. Prioritize the startup/EE-IOP communication
  path; batch validation around meaningful changes and avoid unrelated leaf-function expansion.
- Standing user preference: target faithful 1:1 behavior while minimizing wasted
  time and usage. Do not linger on repeated analysis, setup or low-value checks.
  Choose the next action by the concrete blocker it resolves and evidence it yields;
  reuse verified findings, batch related investigations and validation, and automate
  repeated startup/input sequences. Repeat expensive replays or research only when
  new evidence or a relevant change justifies them. Speed must not weaken correctness,
  independence constraints or explicit-stop diagnostics. Report substantive runtime
  progress and remaining blockers concisely.
- 2026-10-11: user requested work "until fps is 30". Met for the pinned heavy route on the dev host (paced 30, unlimited ~42, byte-exact). Broader scene/host qualification is pending; see PROGRESS.
- Current user priority (2026-09-23): pursue renderer throughput using the newly authorized native-renderer freedom before returning to VU/EE micro-optimization. Preserve game-visible behavior and visual output, use the existing strict renderer/`v1/` as reference, and prefer structural GPU-native wins over more exact-GS micro-tuning. The in-process OpenGL host and direct presentation/input remain required.

## Active work and continuation
- At the start of a continuation, identify the next measurable major milestone
  and its verification evidence in `docs/PROGRESS.md`. Prefer a later original
  startup stage, a newly rendered screen responding to input, or a subsystem
  exercised end to end by the original program.
- Minor edits, successful builds, isolated test passes, removal of one explicit
  fault and exhaustion of a diagnostic slice budget are checkpoints, not reasons
  to end the work session. Trace the next observed blocker and continue.
- A milestone counts only with captured runtime evidence and appropriate
  regression checks. Never suppress an unsupported-hardware fault, substitute
  guessed behavior or weaken a test just to report progress.
- Keep advancing after a major milestone while the user's continuation request
  remains active and tools permit. Batch independent investigation and validation;
  report meaningful findings without asking again for already authorized work.
- Stop only for the user's instruction, completion of the authorized goal, a real
  external/permission blocker, or an actual session/tool limit. Do not promise
  background or unlimited execution. Before yielding or forced handoff, record
  the active milestone, evidence, exact blocker, live command sessions and next
  executable action in `docs/PROGRESS.md`; carry them into the SKILL.md handoff.

## Read first

Read root `rules.md` and `learned skills.md` before adjustments or implementation.
Follow the plan, written-change, backup, test and correctness/performance review
workflow. Add reusable successful and disproved approaches to learned skills,
with evidence and scope; do not confuse an unhelpful optimization with a universal
failure. Current priority: diagnose and improve speed before substantial new gameplay
work. User withdrew the missing map-entry text report; do not pursue it as a bug.
At each continuation, scan every project `.md` file, including ignored folders,
for current instructions, evidence and pending work. Read the current sections
before acting; dated historical notes do not override newer verified state.

1. `docs/PROGRESS.md`: verified state, blockers, exact next steps and commands.
2. `README.md`: build and analysis workflow.
3. `docs/ARCHITECTURE.md`: execution model and staged roadmap.
4. `docs/SOURCES.md`: specification sources and provenance rules.
5. `docs/WORLD_EDITOR.md`: standalone world editor plan and room/texture/geometry format evidence.
6. `docs/NATIVE_RENDERER_PLAN.md`: 2026-10-11 independent research and native renderer draw-seam architecture proposal; no performance claim or external renderer implementation.
7. 2026-10-11 HG-ARCH-007/008 renderer-native progress: `native_psmt8_texture.hpp` stages only observer-coherent 8 KiB source pages; `native_psmt8_cache.hpp` tracks source generations and completed-readback GPU producer pages; optional resident `gl_gs.cpp` observer forwards CPU writes, GPU enqueue and completed readback, but the factory epoch parameter defaults null. HG-ARCH-009: `gs_gpu_tests --native-epochs` (also part of every `--resident` run) qualifies the live resident GL hook in 14 cases; discarded pages are superseded rather than left pending. HG-ARCH-010: `runtime/gl_native_indexed.{hpp,cpp}` (gs_gpu_tests only) holds GL_R8UI indices and palette SSBOs keyed by minted `content_id`/palette `identity`; `gs_gpu_tests --native-texture` gives 50,412 exact point samples. Native triangle pixel/Z parity remains unqualified. Build tests from Git Bash with `MSYS_NO_PATHCONV=1 cmake --build build --config Release --target <t> -- -m:1`. Preserve the existing GL observer/barriers and frozen `v1/`.

## Map
- `runtime/include/hg/controls_panel.hpp`: host-only F1 keyboard-reference panel,
  edge-latched Escape dismissal and Tab-as-Select alias (Backspace retained).
  `game_host.cpp` builds the panel texture lazily and draws it only within the
  retained fresh/refresh/resize redraw boundary. No guest pixels/clocks are changed;
  opening help releases live input but does not pause the original game.
  `tests/controls_panel_regression.hpp` covers hotkeys, alias/focus combinations
  and synthetic UI pixels; `docs/CONTROLS.md` lists all bindings. This authorized
  UI addition changes the historical host hash1a122b9f...; preserve its redraw
  fix AND the new controls on later rollbacks. Current active host20da4bc7... is
  recorded with exact Project Link backup621129e3c1024d79a823a45881d09773
  in PROGRESS; host9704ed0b... is a historical strict-renderer revision and must
  not replace it in the relaxed native-renderer baseline.
- `tools/hg_build_provenance.py`: HG-DIAG-028 post-link source/configuration and
  executable hash manifest. Fixed frames verification fails before native launch
  if inputs or the isolated executable do not match the Release build manifest
  (Windows `build/Release` or a single-config tree such as `build/linux`).
  A source-only revert or a test-only build is not a current game executable.
  Manifest hashing is outside native timing; inspect intended machine-code changes
  separately and regenerate after emitter/config changes before building.
- Performance benchmark (HG-PERF-114): pinned `gameplay180m_authentic` inputs run
  threaded (no `--profile`) with `--frame-rate-probe` (HG-DIAG-114). The metric is the
  30-writer checkpoint rate over 150M–179M (839 writers; report median/p10/count
  below 29.9 across several interleaved pairs; host variance is large). `--profile`
  forces synchronous devices and understates threaded play about 2×. Verified
  2026-10-11 on EXE e053f17b…: unlimited ~42/s, `--realtime` locked at 30. Control refreshed 2026-10-14
  (`20261014-control-73201b00`) after the IOP masking/timer fixes changed guest timing.
  `runtime/host_sampler.cpp` (HG-DIAG-115, `HG_SAMPLE_FILE`, window
  `HG_SAMPLE_FROM/TO`) plus CMake `-DHG_HOST_SYMBOLS=ON` (PDBs, profiling only;
  production OFF) give per-thread stack attribution.
- `docs/DIAGNOSTICS.md`: diagnostic/probe inventory, activation, performance cost
  and cleanup criteria; distinguish observation from guest-affecting experiments.
- `tools/hgtool/`: independent ELF parser, EE decoder, CFG/callback discovery, C++ emitter.
- EE/IOP emitters partition static C++ dispatch by4KiB address region, preserving
  original budget/delay-slot/wait semantics. `docs/PERFORMANCE.md` records profiling
  and state-equivalence evidence; diagnostic `--profile` samples host phase costs.
  MSVC fast Release builds use /O1 for large EE shards and /O2 for the smaller
  generated main/VU1 unit; original AOT behavior remains unchanged.
- EE static intra-page branches jump to compiled labels through their existing
  budget/trace prologues. Fixed-width scalar RAM methods retain checked fallback
  for kernel mappings, devices, invalid spans/alignment and diagnostic watches.
- EE scalar operands use build-time register locations; architectural zero reads,
  discarded-result evaluation, upper halves, delay slots and budgets are preserved.
  Dynamic runtime register helpers retain their checked interfaces.
- `runtime/vu_matrix.cpp` / `fpu_add4_avx2.cpp`: exact fused two-transform
  helper selected by a guarded AOT15-pair loop in `tools/hgtool/vu_emit.py`.
  Publish live VF/ACC/MAC/current outputs and all sticky flags at original
  consuming boundaries; preserve lower operations, Q/P timing, readiness and
  checked CFG fallback. HG-DIAG-061 / HG-LEARN-053;262144 arithmetic and81920
  full-state/fault comparisons. Paired eight-lane follow-up062 was rejected.
- `tools/hgtool/vu_emit.py`: VU1 program identity is per activation entry over its reachable pairs (HG-LEARN-090). Omit SQ snapshots only for nonoverlapping VF lanes;
  conservative basic-block VF readiness proof skips already-satisfied guards,
  retaining original checks after clock wrap. All pipeline advances remain.
  `tests/generate_vu_readiness.py` / `vu_readiness_tests` compares emitted baseline
  and optimized CFGs, including wrap/branch/fault states. HG-LEARN-031/033.
- `runtime/include/hg/gs_math.hpp` / `runtime/gs.cpp`: prepared triangle S/T/Q
  coefficient bounds use exact native 64x64->128 product overflow checks on
  MSVC x64 and GCC/Clang, with the quotient definition as portable fallback.
  This removes up to nine variable divisions per textured triangle without
  changing readiness, admission or faults; HG-DIAG-102 / HG-LEARN-081.
- `runtime/vu_xyz.cpp` / `runtime/include/hg/vu_xyz.hpp`: scalar XYZ broadcast
  helpers selected by AOT VU emission; exact arithmetic and original fault fallbacks.
  No dynamic decoding. Small measured benefit; see PERFORMANCE/HG-LEARN-025.
- `runtime/vu_products.cpp`: guarded SSE2 integer products for full-lane VU
  accumulator multiply/MADD and vector MADD. Partial masks, undefined/invalid
  inputs and other host architectures retain the checked native scalar path.
- `runtime/fpu_add4.cpp` / `fpu_add4_avx2.cpp`: CPU/OS-guarded packed full-lane
  addition and four64-bit-lane integer products, with scalar/SSE2 fallback. Exact 24-bit integer conversion supplies bit
  length only; guest alignment, addition, saturation and flags remain exact.
  Sixteen million arithmetic comparisons and original captures pass; measured
  scene gain is modest. See HG-LEARN-027 and PERFORMANCE.
- `runtime/gs.cpp` / `gl_gs.cpp` / `gs_memory.hpp`: resident PSMT8 masked-word
  uploads merge repeated bit writes before unique-word GPU dispatch. CPU consumers,
  later draws, copy and teardown retain coherence; CPU-owned pages use scalar
  stores. Large complete IMAGE runs instead materialize each physical page once
  and apply ordered lane writes directly; proven complete aligned replacements
  may discard old resident contents after ordering prior GPU users (HG-LEARN-080).
  Short/partial runs retain masked dispatch. Validated differential cases and
  exact original captures.
- `runtime/include/hg/gs_raster_pages.hpp`: conservative clipped frame/depth
  write-page bounds for CPU sprites/triangles. Full readback and eager GPU flush
  remain; texture-read narrowing/deferred flush were rejected (HG-FAIL-018).
- `tools/hg.py analyze --triage`: groups boundaries and reports installed object-table
  candidates with store provenance, slot offsets and compiled/missing targets.
  Bounded pointer runs require independent extent/live-use verification; never auto-root them.
  Scoped exception (user-requested 2026-10-11): installer-proven object-table targets
  that pass automated entry/body checks were batch-rooted (`vtable_batch_*`, 2,720
  targets; see PROGRESS). Rerun the same checks for future batches; never root
  rejected targets blindly. User approval 2026-10-14 extends this to unreferenced
  frame-allocating prologues that pass the same checks (`sweep_*`, 740), conditional on
  keeping 30 FPS on the pinned benchmark; `analysis.max_instructions` must stay above
  the executable segment (emit fails on exhaustion).
- `config/haunting_ground_us.toml`: executable identity, manual functions/targets, pointer tables and static overlays.
- `runtime/include/hg/`: portable state, memory, FPU, timers, INTC and DMA/SIF0 primitives, including checked EE word/doubleword unaligned merges.
- `runtime/device_thread.cpp` / `device_link.hpp`: host device threads (VIF1+VU1 stage,
  GIF+GS+GL stage) fed by ordered SPSC rings from the EE thread; joins only at guest
  observations; IMR/CSR/BUSDIR mirror; `--sync-devices`/`HG_SYNC_DEVICES=1` fallback.
  `tests/device_thread_tests.cpp` checks sync equivalence. HG-LEARN-076.
- `runtime/include/hg/vu1_spec.hpp` / `device_thread.cpp` `VuJob`: speculative
  parallel VU1 activations, **default on since HG-PERF-119** (3 workers with ≥8 host
  threads, 2 with ≥6, else 0; `HG_VU_WORKERS=N` overrides, `0` disables). Required
  for 30 FPS heavy gameplay (without: ~24/s). Workers run the same AOT programs on snapshots; stage B commits in order,
  validating lane-granular memory reads (`Vu1AccessLog`), emitter live-in VF/VI/ACC/I
  (`vu1_aot_spec_info`), consumed clip bits, Q/P and readiness, else re-executes
  exactly. Transport reads/calls drain jobs (fault ordering). HG-LEARN-077.
- `GifPath::submit_words` / `track_forward`: GIF packet boundaries are tracked per
  qword and complete packets applied once, from the caller's block when possible;
  `Vif1Path::pending_need` skips re-parsing incomplete VIF commands. Faults and
  injected `pending` bytes fall back to the original rescanning behavior.
- `runtime/include/hg/iop_dmac.hpp` / `sif_link.hpp`: bounded bidirectional DMA and shared transport state.
- `runtime.hpp` / `dmac.hpp`: connected normal scratchpad DMA channels8/9;
  checked RAM spans,14-bit SADR wrap and channel completion. toSPR also supports
  bounded source chains; fromSPR chain/interleave and cycle-accurate arbitration
  remain unfinished.
- `runtime/system_diagnostic.cpp` / `runtime/include/hg/system.hpp`: connected EE/IOP startup, checked IOP reboot, original service startup and FIFO servicing; `--verify-loadfile` checks native LOADFILE RPC registration before reboot.
- `runtime/include/hg/iop_interrupt.hpp`: cooperative AOT IOP DMA/CDVD/SIO2/timer interrupt delivery, gated by `iop_cpu_interruptible` (no delivery while a masked runnable worker owns the CPU; HG-LEARN-092).
- `runtime/include/hg/iop_reboot.hpp`: validated native reboot request callback; lifecycle remains in progress.
- `runtime/include/hg/iop_thread.hpp`: cooperative native worker scheduling and reserved stacks. THREADMAN owner rule (2026-10-25): the running worker keeps the CPU against equal-priority ready workers until it blocks, yields (reschedule) or exits; no time slicing.
- `runtime/include/hg/iop.hpp`: IOP state/services, event wakeups, partial CD/DVD command protocol and RTC profile.
- Buffered IOP inputs retain verified source bytes through `iop_image.cpp`; AOT entry hooks select static allocation plans before the original MODLOAD body executes.
- Connected MODLOAD scratch, worker stacks and HEAPLIB arenas use original
  AOT SYSMEM alongside SIF heap clients. Static module spans and native call
  stacks are reserved there; isolated tests retain bounded synthetic arenas.
- `iop.hpp` parks WaitVblankStart callers until the connected display timing producer publishes an edge.
- SIO2 in `runtime/iop_sio2.cpp` / `iop_interrupt.hpp`: bounded two-port CPU FIFO transport
  with original IRQ17 delivery. `controller.hpp` / `runtime/controller.cpp` retain
  virtual DualShock2 config, axes, pressure and motor-map state; partial reply
  a one-block absent-card DMA probe is bounded. Partial reply masks, general
  DMA and saves remain unfinished; native keyboard movement and gamepad input are user-verified (gamepad 2026-10-13).
- SPU2 in `iop.hpp` / `spu2_input.hpp` / `spu2_voices.hpp`: bounded one-shot DMA, paced stereo input halves, and 48-voice synthesis/mixing (ADPCM, Gaussian, ADSR, sweep, noise, loop/ENDX, VMIX/MMIX, core0->core1) feeding the speaker; reverb (Hall network, ESA work area); sound-data output areas not synthesized (HG-DIAG-119); `spu2_voice_tests`. IOP timers honor mode bit 3 reset-at-target (SNDDRV 240 Hz sequencer clock).
- `runtime/include/hg/ee_interrupt.hpp`: resumable native EE SIF0 callback dispatch/context restoration.
- `runtime/include/hg/display_timing.hpp`: configured interlaced NTSC field edges;
  `ee_interrupt.hpp` now also dispatches ordered EE INTC callback chains.
  Connected display starts wake IOP WaitVblankStart workers. Other timing modes
  and complete architectural interrupt exceptions remain unfinished.
- `runtime/include/hg/ee_thread.hpp`: cooperative EE priority scheduling with per-thread CPU/kernel-frame preservation and semaphore/sleep waits.
- `vif.hpp` / `runtime.hpp`: VIF1 FBRST reset, ERR ME0, completed-packet STAT and MARK access; CPU SQ FIFO submission uses the existing VIF1 parser.
- `runtime.hpp`: bounded VU0 macro arithmetic/ACC/flags and fixed-point conversions, synchronized Q, FBRST and idle status; timed VU0 Q polling remains an explicit gap. VU1 uses original-byte-validated
  AOT microprogram graphs from tools/hgtool/vu_emit.py, with per-field VF/VI-load
  readiness advancing pending Q/P on dependency stalls; complete flag, EFU and
  XGKICK timing remain unfinished.
- IPU input DMA in `dmac.hpp` / `runtime.hpp`: normal and bounded source-chain
  channel4 transport, saved MADR/QWC continuation and eight-qword FIFO pressure.
  Shared internal bit buffering, FDEC and pending SETIQ/SETVQ consume this stream.
  Normal IPU output DMA3 writes real FIFO output to RAM/scratchpad.
  runtime/ipu.cpp contains bounded MPEG2 intra/non-intra frame BDEC and RAW8-to-RGB32 CSC;
  ipu_dct_lookup.hpp indexes the independently recorded H.262 numeric codebooks;
  invalid prefixes retain explicit faults and full-prefix regression coverage.
  MPEG1/field-DCT, RGB16 and dithering remain explicit gaps.
  Measured arithmetic profiles and physical-console limitations are in ORACLE.md.
- `runtime/`: checked ELF loader and native entry points. `hg_game` uses
  `game_host.cpp` / `native_host.hpp` for in-process OpenGL presentation and live
  keyboard/gamepad input. `hg_native_runtime` is shared with the headless runner;
  `opengl_support.hpp` shares GL setup with the diagnostic viewer. CPU GS remains
  the reference/fallback; opt-in gl_gs.cpp compute handles checked sprite batches
  and bounded integer triangle tiles, including untextured flat/Gouraud triangles
  within the same checked frame/depth/blend limits. gs_memory.hpp observers track optional
  GPU residency and materialize committed work before CPU consumers. See
  docs/ARCHITECTURE.md and HG-DIAG-016/017/018 for coverage and fidelity limits.
- `system_diagnostic --preview-file` publishes sampled committed framebuffers;
  `hg_opengl_host --watch-display` shows them in a persistent OpenGL window.
  Keep the user able to see startup tests; display meaningful captured frames
  in conversation too. Previewing must not flush or otherwise mutate guest draws.
- Connected device service executes submitted GS draws after VIF/GIF pumping;
  FINISH is an ordering/completion event, not a prerequisite for drawing.
  GS texture sampling supports matching nearest/linear minification and
  magnification filters; mixed LOD selection and mipmapping remain explicit gaps.
  Bilinear UV wrapping/rounding oracle evidence is recorded in `docs/ORACLE.md`.
  Completed host transfers retain measured surplus-payload state separately
  from uninitialized/cancelled transfers; limitations are in `docs/ORACLE.md`.
- `editor/`: standalone world editor (user requirement 2026-10-19: separate from the game).
  `hg_world_editor` (GLFW/OpenGL, CMake `HG_WORLD_EDITOR`) reads the user's DATA.CVM or
  .iso read-only and links no game runtime; `editor/hgworld/` holds the Python reference
  parsers/exporters (outputs outside the repo or under `out/`). `editor/src/edit.hpp` (structural
  add/remove, resizable rooms), `library.hpp` (object library), `character.hpp` (PCK character models as library entries), `ui.hpp`/`font5x7.hpp` (own 2D UI).
  Plan, stages and format
  evidence: `docs/WORLD_EDITOR.md`. Getting edits into the game needs a host file-override
  hook that is not built; ask the user before changing the game host for it.
- `tools/naming/`: evidence-only function naming (EE self-naming strings + syscall wrappers;
  IOP export/import tables) from `ps2tek_names.json`; outputs in ignored `out/`. `hg.py emit`
  embeds an EE symbol table (`runtime/include/hg/symbols.hpp`) used only by stop messages.
  Never assign a name without evidence; record the evidence with each name.
- `tools/oracle/`: PCSX2 memory-oracle drivers (`pcsx2_drive.py` PINE/DebugServer/keys;
  `section_check.py` save-slot checks that pause both sides at the same frame-counter write;
  `watch_trace.py`). Our side uses HG-DIAG-124/125. PCSX2 is not hardware-exact for VU0 adds
  or DIV/SQRT rounding (`docs/ORACLE.md`); HG-DIAG-126 `HG_PCSX2_VU0_ADD=1` mirrors its VU0
  adder for comparisons only. `tools/saves/`: .max unpacker and memory-card injector.
- `tools/hwtest/`: our ps2sdk capture programs for the user's real PS2 (`fpu_capture.c`,
  `vu0_capture.c`, `struct_capture.c` with the embedded IOP SPU2 envelope probe `spu2probe/`;
  build in `ps2dev/ps2dev:v2.0.0`), analyzers
  (`analyze.py`, `analyze_struct.py`) and `tools/fpu_vector_check.cpp`. Captures (*.BIN) and
  build outputs stay out of version control; results and fitted rules are in `docs/SOURCES.md`.
- `runtime/include/hg/autopilot.hpp`: HG-DIAG-123 scripted synthetic input; routes from
  `editor/hgworld/route.py`.
- `tools/hg.py inspect`: own-decoder instruction/caller views of external native snapshots.
- `tools/hg.py assets`: read-only CVM/ECMA-119 index and streaming asset hashes.
- `tools/hg.py modules`: IRX import/export/relocation inventory and build-time relocation dry-run.
- `config/iop_modules.toml` / `tools/hg.py iop-plan`: checked IOP placement, manual roots and import bindings.
- `tools/hgtool/iop_source.py`: verifies standalone modules, ROMDIR members, or bounded embedded regions and container identities.
- `tools/hg.py iop-emit`: separate MIPS-I AOT path; `runtime/iop_*.cpp` supplies checked module loading/diagnostics.
- `tools/hg.py iop-audit` / `iop-bundle`: all-module discovery audit and combined static IOP builds.
- `tools/hg.py reboot`: read-only inventory of embedded modules in the game's IOPRP image.
- `.devcontainer/`: Debian trixie Linux dev container (CMake/Ninja/GCC/clang,
  GLFW/Mesa, gdb/perf, Claude Code). Root `.env` (gitignored, `HG_GAME_DIR`) mounts
  the user's dump read-only at its own path; post-create links `Haunting Ground (USA)`.
  Builds go to `build/linux`; container builds run inside via podman (no docker
  socket). `initialize.sh` builds the image on the host with plain `docker build`
  (the tooling's compose/bake build is refused). Notices are in `welcome.sh`;
  start without VS Code via `claude.sh`.
  No host credentials are bound: ssh, git config and Claude state are volumes.
- `tests/`: synthetic, redistributable instruction/ELF tests; no game bytes.
- `out/`: ignored local reports and game-derived generated C++.
- `build/`: ignored native build products.
- `Haunting Ground (USA)/`: user-provided dump; read-only inputs.
- `emu/`: user-provided emulator; memory oracle only; observations in `docs/ORACLE.md`.

## Completion criteria
A playable build requires verified EE/VU translations, kernel/IOP services,
DMA/VIF/GIF/GS rendering, audio, input, saves and asset streaming. A diagnostic
host or clear-color OpenGL window is not a playable recompilation.
