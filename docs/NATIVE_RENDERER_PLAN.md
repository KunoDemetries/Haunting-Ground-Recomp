# Haunting Ground — Independent Native Renderer Research and Implementation Plan
**Research date:** October 11, 2026  
**Project:** `C:\Users\johnn\Documents\ChatGPT\Haunting`  
**Status:** Architecture recommendation; no FPS improvement claimed from this research.

## HG-ARCH-010 (October 11, 2026)

- **Native indexed texture residency verified (shadow only):** `runtime/gl_native_indexed.{hpp,cpp}` stores staged PSMT8 indices as `GL_R8UI` and CT32 palettes as SSBOs, reused only by minted content/palette identities. 50,412 GPU point samples through all GS CLAMP modes equal `GsRegisterState::point_sample_tex0`. Linked into `gs_gpu_tests` only. Next: shadow native PSMT8 triangle pixel/Z parity into private targets (HG-ARCH-011).

## HG-ARCH-009 (October 11, 2026)

- **Live resident GL epoch qualification passed:** `tests/gs_gpu_tests.cpp` `native_epoch_gate()` (14 cases, `--native-epochs`) shows on real OpenGL that GPU enqueue invalidates, `glFlush` is not completion, readback completes exactly the read pages, and CPU/masked/discard/raster-scope writes, owner swap, GL faults and teardown behave conservatively. Discarded pages are now superseded instead of left pending. Production still passes null epochs. Next: `GL_R8UI` indexed texture + versioned palette residency with a shadow sampling parity test, still with no frame writes.

## HG-ARCH-007 / HG-ARCH-008 current implementation (October 11, 2026)

- **HG-ARCH-007 (verified):** original indexed PSMT8 staging now requests only needed coherent 8 KiB GS physical VRAM pages through existing observer-backed `read_span`; no whole-4MiB `data() const` synchronization on a staging miss. New instrumented observer checks 1-page (8 KiB) and 2-page physical-wrap (16 KiB) synchronization requests and validates 21,292 first-party PS2 indexed texels. All native unit gates and the provenance-verified game build succeeded. Not a measured FPS improvement; still not an active native-renderer draw path.
- **HG-ARCH-008 (code compiled, synthetic epoch tests passed; live GL hook unqualified):** resident OpenGL `Accelerator::Observer` preserves original CPU/GPU coherence and optionally forwards CPU/scoped/discard mutations, queued sprite/triangle/masked writes, and completed-readback page sets to `NativeVramPageEpochs`; `glFlush` only submits and never releases pending epochs. An optional fourth shared-epoch parameter to `make_gl_sprite_accelerator` defaults null in the production path. Memory-owner retirement and explicit GL error invalidate trust. Synthetic multi-write/single-readback completion tests pass, and the game links. **An opt-in live resident GL validation runner is still required before claiming the hook is behaviorally qualified.**
- Native indexed texture/palette OpenGL uploads, shader alpha/Z/coverage parity and GPU-authoritative native display remain unimplemented. No 30 FPS claim, no proven normal-threaded gameplay throughput change, frozen `v1/` unchanged.

## Verified implementation milestones (2026-10-11)

**HG-ARCH-002/003 — renderer-neutral capture seam.** `runtime/include/hg/native_indexed_draw.hpp` contains copied original `GsDraw` vertices and effective per-draw GS registers, an immutable 256-entry CT32 CLUT snapshot keyed by `clut_load_count`, PSMT8 NOTEQUAL/GEQUAL/AFAIL-KEEP preflight, and a disabled-by-default `NativeIndexedDrawObserver`. The original rasterizer executes first; the optional observer only sees successfully completed draws, never faults. Exact pinned authentic 180M replay with observer null: 5,270 original writer boundaries, matching frame/EE/GS/VU/input/console against clean control (raw IOP host RTC differs); no performance claim.

**HG-ARCH-004 — conservative physical page hazard screen.** `runtime/include/hg/native_psmt8_pages.hpp` calculates PSMT8 source read and frame/depth destination pages for clipped native draw candidates using independently derived original GS addresses, including TBP0 non-page-alignment, physical wrap and CLAMP modes. All **38,400** reference sampled-address membership checks and conservative alias/clip tests pass. Source/read nonintersection is only an upper-bound hazard screen until ordered texture writes, page generation and GPU fences are tracked.

**HG-ARCH-005 — native indexed texture byte staging.** `runtime/include/hg/native_psmt8_texture.hpp` extracts original PSMT8 8-bit texels into a dense row-major integer texture envelope, independently verifying the GS swizzle and physical wrap while honoring REGION CLAMP/REPEAT coordinates outside nominal TW/TH. **21,260** staged bytes matched the reference `GsRegisterState::pixel8`. Staging currently requests fully CPU-visible GS VRAM, which can cause a full GPU readback, so this helper is intentionally **not enabled in gameplay**. These changes pass the runtime, translation and VU readiness tests and strict provenanced game build; all existing EE/VU/VIF/GS/raster logic and `v1/` are unchanged. **No 30 FPS or any native-rendering speedup is established.**

**Remaining before switching pixels to native GPU:** independently validated source-page generation and CLUT epochs; incremental integer texture/CLUT GPU residency instead of full readback/upload; exact GS coverage, shade, alpha and Z parity against original; source/frame/depth feedback and explicit materialization/interrupt/error barriers. At that point test opt-in native PSMT8 drawing on a synthetic scene, then pinned original-gameplay clips with on/off native renderer and complete state comparisons. Real **normal-threaded** playback benchmarking is still required; `--profile` 180M is synchronous.

## Implemented native-renderer infrastructure through HG-ARCH-006 (October 11, 2026)

- HG-ARCH-002: `runtime/include/hg/native_indexed_draw.hpp` independently derived PSMT8 ordered-geometry and immutable CT32 palette snapshot contract; explicit **texture VRAM hazard and native pixel parity** proof flags.
- HG-ARCH-003: opt-in, default-null, **post-raster** completed draw observer in `runtime/gs.cpp`, proven with complete 180M authentic replay (5,270 exact original writer boundaries and retained frame/EE/GS/VU/input equality). No callback on original renderer fault.
- HG-ARCH-004: `native_psmt8_pages.hpp` conservative physical source vs FRAME/Z page overlap pre-screen including TBP0 alignment and VRAM physical wrap; 38,400 original address membership tests passed.
- HG-ARCH-005: `native_psmt8_texture.hpp` row-major indexed-byte source staging for a future GL_R8UI shader, including clamp coordinate envelopes beyond nominal TW/TH; 21,260 exact original PS2 indexed-texel comparisons passed.
- HG-ARCH-006: `native_psmt8_cache.hpp` 512 physical VRAM page epoch tracker, producer-enqueue pending GPU masks, complete-fence baseline requirement, bounded four-entry texture cache, immutable source generation stamps, O(1) same-epoch hot lookup and strict single-GS-VRAM-instance ownership. Synthetic tests passed for CPU invalidation, multiple outstanding GPU writes, disjoint reuse, texture wrap and LRU. The C++ test-only memory observer **MUST NOT** be installed over an active resident GL observer; correct future integration requires notifications from the existing GS coherence path.
- Current linked production game successfully built as SHA256 `98b486050e8bb211a4ae335f6188f9a039a954175b3090f3be733ccbbf9974b7`, with 160 provenanced inputs. All runtime/translation/VU suites pass after HG-ARCH-006. These helpers are inactive; current gameplay rendering unchanged.

**Critical next integration:** add opt-in notifications to the *existing* `GsMemoryObserver` and `runtime/gl_gs.cpp` resident **GPU enqueue/flush/visibility events**, never replace the existing observer or turn CPU/GPU materialization off. Build a pinned synthetic test proving exact generation and CLUT-epoch cache invalidation across host-to-local IMAGE, framebuffer and depth writes, GPU queued completion, and CPU readbacks. Before native GL output can replace any pixel, validate exact original PSMT8 swizzle + indexed palette + GS ATST NOTEQUAL/GEQUAL + texture function + Z test + clipping/coverage and guest-visible framebuffer/readback state. Keep original GS renderer as fallback. Avoid per-triangle full CPU memory materialization and keep grouped cached texture changes only when source page generations prove residency. **No real gameplay FPS improvement claimed.**

## Actual heavy original textured draw eligibility: HG-DIAG-112 (2026-10-11)

The next read-only aggregate source-level census succeeded on the pinned 180M route with 5,270 original frame writers and exact frame/EE/GS/VU/input captures. Of 68,497,028 textured original GS triangle records, PSMT8 provided **52,473,068**, 51,114,452 using GS ATST NOTEQUAL and 1,358,616 GEQUAL. All PSMT8 tested the depth buffer; none used DATE or non-KEEP alpha-fail. Broad state screening admitted **49,160,981 PSMT8** records (CT32 frame, no ABE/fog/AA/DATE/frame mask, alpha compares allowed), in **46,236** coarse identical-state runs up to 5,769 triangles. PSMT8H admitted **11,643,740** more records under the broad screen, but has observed same texture/frame base-page cases (1,459,426 PSMT8H), and texture fetches from framebuffer alpha require much stricter feedback verification. Together, 60,804,721 original triangle records (~88.8% of all textured triangles) satisfy **preliminary, deliberately optimistic** flags-only conditions. These are *not* verified native-raster-safe draws: GS swizzle, palette versions, memory page alias/feedback, alpha/depth and raster edge behavior remain unproven. Source was byte-restored. The 33M profile could not launch due pinned TOC mismatch, so the 180M route was preserved; its sampled updates/s are not real normal-threaded FPS.

**Updated recommendation:** initial independent shader/backend implementation should target **PSMT8 8-bit indexed texture with explicit palette snapshots/epochs, alpha NOTEQUAL/GEQUAL, depth test**, not ordinary CT32 or untextured-only rendering. Treat PSMT8H as a later render-to-texture/feedback class. Before drawing on GL framebuffers, introduce a no-pixel-change immutable native draw-state contract after existing GS parse/raster-state validation, and only allow reuse across a texture or CLUT change after actual source/consumer ownership/version proves it safe. GLSL fixed pipeline alpha/depth alone is not enough to prove PS2 equivalence. The source-level 46,236 same-key runs are *upper bounds* because palette reloads need not change TEX0 values.

## New original-scene evidence: HG-DIAG-111 (2026-10-11)

A direct aggregate census at the existing ordered, fully resolved `GsDraw` boundary (180M pinned route, original 5,270 writer events and exact retained frame/EE/GS/VU state) reports **68,565,026 triangle draw records**, 68,497,028 textured: **52,473,068 PSMT8**, **15,978,284 PSMT8H**, **64,470 PSMT4**, **48,604 CT32** and 600 other format. All triangle frame targets are CT32. This is a different numerator than ~21.99M triangles accepted by the current GPU compute path. The active PSMT8/PSMT8H, paletted and framebuffer-high-byte texture workloads are the decisive first renderer classes. Untextured or direct CT32 native drawing can validate architecture but cannot plausibly dominate frame time on this route.

The first strict alpha-testing-disabled/native-opaque preliminary screen selected **zero** textured draws. Only 1,473,952 triangle records had both alpha-test enable and destination-alpha-test enable cleared, even though 62,464,426 had no ABE/fog/AA. That rule was too narrow: a future indexed native fragment shader needs exact PS2 ALPHA TEST modes, alpha comparison/failure actions, and GS feedback read/write semantics rather than assuming opaque/no-test drawing. No support for hardware texture formats, actual backend pixel identity, or native throughput is proven by these counts. See `docs/PROGRESS.md` HG-DIAG-111 and original external capture `project-link-runtime/20261011T063921Z-1e5cbbbe064a4e9f9a18e3f6716cef42`. Probe was restored.

**Design revision:** Phase 1 must classify active alpha test modes, representative indexed texture/CLUT source lifetime, texture/frame alias hazards and compatible-state run lengths before a production fast path. A hardware-FBO-only opaque CT32 prototype should be treated as a smoke test, not an FPS plan. Avoid an unversioned static palette texture; preserved ordered TEX0/TEX2 palette reloads and CPU observation boundaries are mandatory.

## Decision

Replace the current **rendering path for proven-safe 3D draws**, not the game's compiled execution path.

Keep the original EE/IOP, VIF1, VU1, XGKICK, GIF/GS command parsing, clocks, exception boundaries and GPU/CPU readback behavior. After the original commands have been translated into ordered `GsDraw` records and their effective GS state has been established, allow a new **native OpenGL 4.3 renderer** to render eligible geometry directly into native render targets. Retain the existing CPU/compute GS renderer for unsupported, feedback-sensitive, or otherwise unproven operations.

The key new capability would be *native draw primitives and framebuffer residency across consecutive compatible draws*, not a bigger compute-job batch, another error-query reorder, or copying an emulator renderer.

**This change alone is not proven capable of 30 FPS.** Existing audits also show substantial VU and graphics-transport CPU costs. The successful long-term architecture must combine native rendering with safe overlap in the existing graphics pipeline.

## Findings from the project's Markdown documentation

- `AGENTS.md` allows the active renderer to be high-level/native while requiring guest-visible graphics and strict EE/VU/VIF behavior. It prohibits external renderer code reuse and modification of `v1/`.
- `docs/ROOT_CAUSE_AUDIT.md` identifies VIF1/VU1/GIF as the principal structural bottleneck. Historically, heavy-scene exclusive scopes were about 1.9 s VU execution, 0.74 s GIF processing and 0.51 s rasterization in a roughly 4.66 s test window. These are **historical**, not current real-gameplay benchmarks.
- `docs/RENDERER_ROUTE_AUDIT.md` found that renderer-only host construction and upload was insufficient to reach 30 FPS. A coarse GS worker failed because tens of thousands of CLUT/texture transfer observation joins consumed the anticipated parallelism. Do not repeat that design unchanged.
- HG-PERF-111 cut synthetic resident OpenGL flush cost by ~22%, but did not improve the authentic gameplay replay.
- HG-PERF-112 reduced GPU triangle batches from 36,047 to 25,670, but its authentic final-window rate was 14.05 updates/s versus 14.82 restored-control updates/s. Fewer GPU jobs do not automatically deliver better performance.
- **Crucial workload mix:** the full authentic run recorded 21,931,299 textured GPU triangles out of 21,985,289 accepted GPU triangles (99.75%) in native-relaxed mode. This figure is grounded in the `native_float_stq` counter's code path in `runtime/gl_gs.cpp`; it is not the count of all original GS draws. A native renderer for untextured triangles only would be a correctness prototype, **not a plausible 30-FPS solution**. The first performance-relevant investigation must characterize textured CT32/indexed draws, their CLUT changes, blending and feedback.
- HG-DIAG-110 established a critical benchmark distinction: the fixed `gameplay180m_authentic` runner uses `--profile`, which disables the asynchronous `DeviceThread`. Its ~14–15 original-writer updates/s is a **synchronous profiling metric**, not a verified normal threaded gameplay FPS. Older ~20 FPS measurements cannot be treated as directly comparable without a matching run.

## Other recompilation and native-port projects reviewed

| Project | Transferable architectural lesson | Why it is not a drop-in solution |
|---|---|---|
| **OpenGOAL (Jak PC port)** | Separate game-produced DMA work from graphics backends; specialize rendering for known categories; batch compatible draws while preserving ordering. | Game-specific reverse engineering and native renderer, not a general PS2 GS backend. |
| **BT3-Recomp (Budokai Tenkaichi 3)** | Native graphics-engine seam, independent native rendering path and fallbacks for unhandled graphics work. | Its most aggressive path replaces VU/VIF work, which is **outside the current Haunting Ground rules**; reported native graphics still have some visual defects. |
| **PS2Recomp (ran-j)** | Separate ahead-of-time CPU translation and runtime services. | Hardware and renderer implementation remain experimental; not a usable drop-in graphics renderer. |
| **PSXRecomp (PS1)** | Keep a reference software renderer while developing an optionally GPU-authoritative renderer. | Different console and an interpreter fallback disallowed for this project. |

**Public research sources:**
- https://opengoal.dev/docs/porting-info/graphics/
- https://opengoal.dev/docs/porting-info/drawable_and_tfrag/porting_tfrag/
- https://github.com/z3xox/BT3-Recomp
- https://github.com/z3xox/BT3-Recomp/blob/main/docs/NATIVE-RENDER-SEAM.md
- https://github.com/ran-j/PS2Recomp
- https://github.com/RetroPortingToolKit/psxrecomp

Only public **architecture descriptions and documentation** informed this report. No external renderer source, shader algorithms, game-specific mappings or patches are to be transplanted.

## Recommended insertion point in our source

**Preserve** the following first-party pipeline:

`Original compiled EE → VIF1/VU1 → XGKICK / DIRECT → GIF / GS state events → GsDraw → renderer`

The new seam should be at `runtime/gs.cpp`, specifically `GsRegisterState::rasterize_pending_draws()` / `rasterize_draw_current()`, after the effective GS environment is resolved and before the existing backend makes the actual pixel updates.

Relevant existing interfaces:
- `runtime/include/hg/gs.hpp`: `GsDraw`, `GsDrawEnvironmentEvent`, TEX0/TEX2/CLUT/SIGNAL/FINISH handling.
- `runtime/include/hg/gs_triangle_acceleration.hpp`: bounded, opt-in triangle accelerator interface.
- `runtime/include/hg/gs_memory.hpp`: CPU/GPU observer and page coherence.
- `runtime/gl_gs.cpp`: existing bounded compute draw execution and texture/readback obligations.
- `runtime/device_thread.cpp`: normal-mode, ordered host B/C device threads; preserve these.
- `runtime/include/hg/vif.hpp`: independently verified original XGKICK source and fault checking.

Introduce **one internal native draw contract** containing ordered vertex data, exact effective state, memory source/destination page dependencies, draw sequence number and renderer eligibility. Preserve the existing checked backend as fallback.

## Execution plan

### Phase 0 — Correct benchmark
Establish an independently approved **fixed recorded-input normal-threaded** heavy-gameplay comparison, separate from the existing synchronous `--profile` task. Require matching original frame-writer counts, inputs, state captures and no changes to guest clock. Compare clean control, candidate, and restored control under similar host conditions; measure CPU thread occupancy and nonblocking GPU timing separately. Do not claim real normal-gameplay FPS from the existing synchronous 180M tool.

### Phase 1 — Read-only eligibility census
Measure how many heavy-scene draws are eligible for a native render pass, **especially textured geometry**, which dominates accepted GPU triangles. Classify CT32 versus indexed/CLUT/PSMT8H formats, alpha/depth mode, texture feedback, destination-alpha/blend requirements, relevant page aliases, render-target changes and interruptions. An untextured-only smoke test does not count as substantial workload coverage. Determine longest consecutive compatible streak and the measurable removable CPU cost. If safe coverage is small, reject the native fast path before writing shaders.

### Phase 2 — Renderer-neutral ordered draw records
Implement a disabled-by-default, bounded `NativeDraw` capture path fed **after** current GIF/GS checks. Still render every draw through the old backend. Prove draw count, submission order, effective state, memory dependencies and fault boundaries exactly match the original.

### Phase 3 — First native OpenGL draw class
First use native vertex/index buffers and an OpenGL framebuffer for a strictly proved class of opaque, untextured, nonfeedback triangles **as a small correctness smoke test only**. The next necessary step for material throughput is a validated **opaque textured** class that preserves exact UV sampling and GS memory semantics. Retain the current compute/CPU renderer for other operations. Correctly reproduce clipped coordinates, coverage, alpha/depth tests, blending and PS2 pixel-format outputs in the admitted class; if precise parity cannot be demonstrated, retain fallback. Avoid assuming ordinary OpenGL rasterization is automatically pixel-identical to the PS2 GS.

### Phase 4 — Extend carefully
Add ordinary textured opaque geometry, indexed textures and palettes, compatible sprites and additional blend modes only after exact synthetic oracle and game-scene tests. Keep source-page versioning and materialize GPU framebuffer content before any guest/CPU-visible read, transfer, alias or feedback use. Preserve draw submission order and faults.

### Phase 5 — Actual pipeline throughput
Batch **only consecutive, dependency-compatible** native draws. Optimize reused vertex buffers and texture residency. Then reevaluate overlap with the already existing normal threaded VIF/VU/GIF pipeline at **proven guest observation fences**—not the failed historical per-packet worker design. Retain guest-executed VU1 and VIF1 in full.

## Acceptance and rollback requirements

A candidate must pass build and native regression gates and an authentic original-program replay with the same frame-writer count, guest clock/input schedule, framebuffer pixels, materialized EE/GS/VU state, ordering and explicit faults. Normalize the documented host-RTC bytes for IOP memory comparisons. Keep an exact pre-edit backup and restore on failed fidelity or throughput evidence.

**Performance retention gate:** a repeatable gain of at least 5% on the appropriate, paired normal-threaded heavy-gameplay metric; any claim of 30 FPS requires direct real-gameplay measurement. Synthetic shader speed, fewer flushes, and synchronous profile rates are supporting evidence only.

## Immediate status and next action

HG-PERF-113 (16-byte DMA source copy) built but had **not** been qualified. The user requested the structural renderer rethink before the next candidate. Its original source was restored byte-exact, and a clean rebuild was initiated. Record it as **superseded/unqualified**, not a benchmark failure.

The next high-value implementation task is the **Phase 1 draw-eligibility census** at the existing GS draw-record boundary. This establishes whether the proposed native renderer is viable in Haunting Ground's real heavy scene before committing to an expensive shader/VRAM rewrite.

**Immutable boundary:** never modify `v1/`, use PCSX2 as a runtime dependency, borrow another recompilation's graphics implementation, or bypass the game's compiled VU/VIF execution without separately changing the project's rules.