### HG-LEARN-099 - Equal-priority IOP threads must not time-slice

CRI's ADX streaming split a left/right block pair across two IOP threads of priority 39 because our cooperative scheduler rotated equal-priority ready workers each quantum; the right channel's free-space report fell a step behind and its ring mirror stopped refreshing (audible right-ear static). THREADMAN only switches on block/yield/higher-priority wakeup. When per-channel or per-buffer state drifts apart only in our build, check scheduling semantics (owner vs. round robin) before suspecting arithmetic; trace call order per channel (instruction-trace build + PC-watch log) rather than reading code.

### HG-LEARN-098 - Ask the console the narrow question; PCSX2 was wrong on SPU2 rate 0x7F

When PCSX2 and the PS1 reference disagreed on SPU2 sustain rate 0x7F, a 25-second IOP probe (our module + ROM LIBSD, silent voice, ENVX sampled over time, results by SIF DMA) folded into the next capture settled it: the console holds the level, and it also exposed two envelope details neither source had (decay takes one step before the sustain check, giving 0x7FF7; a linear release reads negative for one sample). Structured sweeps (fixed operand, consecutive other operand) likewise pinned the multiplier to a Booth + carry-save structure where random data had not. Prefer one more structured hardware trip over guessing between two references.

### HG-LEARN-097 - Measure the oracle with the same probe before "fixing" a drift

Slot 07 Hewie drifted 0.003 against PCSX2 after every hardware-exact float fix. Running the hardware capture programs inside PCSX2 showed its VU0 adder (exact sum, chop) and DIV/SQRT rounding are not the console's, while ours matched the console exactly. When the oracle and the hardware disagree, keep the hardware rule as default and add an opt-in oracle profile (HG-DIAG-126) for comparisons; do not bend the default toward the emulator. Where hardware is still unfitted, pick the measured-closest rule (COP1 DIV.S nearest: 85% vs chop 60%) and document it as interim.

### HG-LEARN-096 - Find a float drift's origin in three steps

1) Bisect with exact-pause section checks (both sides paused at the same frame-counter write) to the first differing frame and object words. 2) PCSX2 write memcheck on a differing word gives the writer PC; read registers and the stack there. 3) Compare the instruction's inputs with ours: equal inputs mean an arithmetic rule differs; different inputs move the search upstream. Polling traces (watch_trace) sample mid-frame and are only good for a coarse range.

### HG-LEARN-095 - Pin a unit's result structure before fitting its internal rule

For the PS2 divider, the decisive step was testing which results can be +1: with ma >= mb, +1 appears only when the dropped low bit is 1 (0 of 11,000+ otherwise), which proves an inner 25-bit quotient truncated afterwards; SQRT has the same shape and RSQRT = DIV(SQRT). Only then fit digit rules. Families disproved on simple divisors (radix-2 SRT exact/carry-save with constant or per-divisor thresholds, overlapped stages, dropped carry-in, estimate correction, radix-4 {-3..3} exact, reciprocal-multiply) are recorded in SOURCES; prefer a structured hardware capture (fixed divisors, consecutive dividends) over more blind model search.

### HG-LEARN-094 - Discovery caps must fail loudly

A configured instruction budget (650,000) was reached exactly after a root batch; discovery recorded "instruction budget exhausted" in analysis.json but emission continued, silently dropping reachable code and producing a "no static translation" stop at 94M on the pinned route. Keep the limit above the executable segment size and make emit/analyze exit on exhaustion. After any root batch, check the reachable-word count is not a round configured limit and rerun the pinned replay.

### HG-LEARN-093 - Missing music can be a stalled IOP timer, not audio synthesis

Sony MIDI sequencing (MODMIDI/MODHSYN) advances only on its driver's hardware-timer IRQ. Root-counter mode bit 3 (reset at target) was ignored, so a repeating 240 Hz tick fired once. Diagnose by voice-usage stats (whole cores silent) and a timer-configuration trace before touching the synthesizer.

### HG-LEARN-092 - Cooperative quanta must not reopen interrupts for a masked worker

A worker paused at a quantum boundary inside CpuSuspendIntr still owns the CPU on hardware. Scheduling already honored this (`locked_thread`), but IRQ delivery read the idle context's enable flag, letting SIFMAN's SIF0 completion handler re-arm the channel mid-SetDma (2026-10-13 ~448M stop). Gate every asynchronous delivery on the owner's mask (`iop_cpu_interruptible`). Expect guest timing (EE RAM) to change versus older controls; refresh the pinned control after such corrections.

### HG-LEARN-091 - "Only music in gameplay" meant the voice path was unimplemented

Streamed music/cutscenes use SPU2 sound-data input (AutoDMA), which was modeled; every sound effect uses voices (here core 1). Trace guest SPU2 writes first (HG-DIAG-118) to confirm offsets and routing (core 0 output -> core 1 SIN via AVOL), then synthesize per Ts in guest time. Verify with a headless mix WAV (HG-DIAG-120): the mix must equal the input where only music plays and add content where input is silent. Guest captures stayed byte-exact on the pinned replay.

### HG-LEARN-090 - VU1 program identity must cover only the code an activation can execute

Whole-composite MicroMem identity made every new area's upload combination a fresh "no static translation" stop, although the reachable code from each entry was already proven (2026-10-12 ~325M: all four entries equal scene14). Checking per entry over the reachable pair set (plus delay pairs; relative branches only) is exact and keeps explicit faults for any changed executed pair. Verify a card save by its slot payload, not by the save directory's existence (HG save files are created with 12 blank templates first).

### HG-LEARN-089 - A rejected optimization can become decisive in another scene; re-census per scene

HG-FAIL-053 dropped a GPU path for own-pixel PSMT8H feedback because it was flat where that class was rare. In the Debilitas encounter the class was every GPU refusal (882k triangles in 40M slices), and the CPU fallback plus forced readback/re-upload saturated stage C (17–25/s). Implemented exactly (tile lists preserve per-pixel order, so an own-pixel read sees the same frame word as the scalar path), it raised that window to ~40/s and the pinned benchmark from ~42 to ~54, byte-exact. Before dismissing an earlier failure, rerun a cheap rejection census (counters at each refusal) on the slow scene in hand. Treat "flat in scene A" as scene-scoped evidence, not a universal verdict.

### HG-LEARN-088 - Every VU lower store needs the same-pair snapshot, not just SQ

The upper and lower halves of a VU pair read their operands at issue, so a lower store paired with an upper that writes the stored VF lanes must store the pair-start value. The emitter applied this only to `SQ`. `SQI` (and any future `SQD`) went through live state, so `ITOF12 VF19 | SQI VF19,(VI4++)` wrote the converted floats instead of the header. The GIF then parsed the floats as a ~118k-entry tag ("XGKICK packet exceeds VU1 data memory"). Diagnose such faults by checking the kicked memory against the VIF-unpacked input (HG-DIAG-116 trace) before suspecting hardware wrap. Values that are an exact transform of another register point to an in-pair ordering bug. When adding a store or move form to the emitter, check the same-pair snapshot condition explicitly.

### HG-LEARN-087 - Benchmark the execution mode players use, then sample the saturated thread with real symbols

The synchronous `--profile` replay (13–15/s) hid the real limits. The same pinned inputs in normal threaded mode (with `--frame-rate-probe`) ran 24–27/s, and HG-DIAG-091 stage stats showed stage C (GIF/GS/GL) at 0% idle. Only then did a suspend-and-copy stack sampler with PDB/inline resolution (HG-DIAG-115) find concrete stage-C costs. These were a driver-blocking `glGetError` after `glFlush` (14.5%), alpha-test modes forcing CPU triangles, and a diagnostic PPM writer running synchronously at up to 60 Hz (13%). Fixing them took the heavy scene from ~27 to ~42 updates/s, and to a locked 30 FPS paced. Map-file symbolization alone mis-attributed inlined code badly. Judge changes on per-interval distributions (median, p10, count below 29.9) over several interleaved pairs: host variance between runs was up to ±20%. A flat aggregate can hide whether the slowest frames improved. Diagnostic and harness work on the critical thread counts as real cost until it is moved off it.

### HG-FAIL-069 - Coalescing forwarded GIF qwords into blocks did not reduce stage-C time

HG-PERF-117 batched consecutive PATH3/DIRECT qwords on stage B into owned `gif_words` blocks for `GifPath::submit_words`, preserving order through every flush point. It was exact but flat (34.11 vs 34.84 mean aggregate). The per-qword ring/parse overhead is small next to the packet application (A+D register writes, CLUT loads, draws) it feeds. Do not retry ring-level batching without new evidence.

### HG-LEARN-086 - Key GPU residency by minted content identity, and make reuse tests defeat LRU eviction

GPU uploads of staged guest texture bytes may only be reused under an identity minted when the epoch-verified CPU cache staged those bytes (`content_id`, and palette `identity` per snapshot). Process-wide counters avoid aliasing between GS instances or reused addresses. TEX0, `clut_load_count` or a pointer are not safe keys. HG-ARCH-010 verified this with 50,412 exact GPU point samples. The reuse test first missed a deliberate TEX0-keyed mutation: the preceding fixture loop overflowed the 8-entry LRU, so the stale entry had already been evicted. A cache-staleness test must make the old content resident immediately before the mutation and assert that the observed value changes. Then run the weakened-key mutation to confirm it fails.

### HG-LEARN-085 - Qualify renderer-private trackers at real GL boundaries, and prove the test bites with mutations

Synthetic tracker tests alone missed a liveness defect: resident `discard_write` cleared GPU-dirty state but not epoch pending counts. A live resident fixture (`gs_gpu_tests --native-epochs`) queried pending/stamp state between each real GL operation: enqueue, `glFlush`, one-page `read_span`, `finish`, masked, discard, CPU raster scope, triangle, owner swap/destruction, a deliberate `GL_INVALID_ENUM` and teardown. It compared VRAM with the scalar renderer after every step, exposed the defect and passed after a one-call fix. Two intentional source mutations (drop the fix; mark completion at `glFlush`) each failed with the specific message, which proves the fixture distinguishes submission from completion. Restore mutations with a fresh mtime (`touch` after copying, or copy without `-p`). `cp -p` kept an older timestamp, MSBuild skipped recompiling, and a full-matrix run silently used the mutated binary. Rehash the source and check that the object recompiles before trusting results.

### HG-LEARN-084 - First-touch indexed VRAM pages avoid unnecessary full-memory readback on cache misses

HG-ARCH-007 replaced the standalone PSMT8 staging helper's `GsLocalMemory::data() const` (full-4 MiB CPU observer read) with one observer-backed `read_span` per distinct physical 8 KiB page, cached only within a single immutable staging operation. It preserved 21,292 original indexed-byte parity comparisons, page-511/0 wrap and the 38,400 physical address checks. A synthetic counting observer proved a 128x64 texture requests only 8 KiB, or 16 KiB with unaligned TBP0 wrap, rather than the entire 4 MiB. This avoids gratuitous synchronization requests, not necessarily measured GPU transfers; host timing and real cache-hit behavior remain unqualified. The helper is off-path and still requires a coherent source generation and native GPU upload/parity before live rendering.

### HG-FAIL-068 - Doubling resident GPU triangle payload capacity saves batches but not gameplay time

HG-PERF-112 raised only the 1024-triangle payload limit to 2048. It passed build/runtime/translation/VU, complete authentic 180M replay and exact original-frame, EE, GS, VU, console, and input comparisons against the restored control. All 5,270 qualified original writers matched with zero unexpected writer/caller failures; raw IOP retained known RTC differences. GPU triangle batches fell from 36,047 to 25,670 (28.79% fewer), flushes from 58,247 to 47,869, but heavy final 30-writer throughput was **14.0522 updates/s** candidate vs **14.8173** adjacent restored control. The material >=5% speed improvement was not observed. Host scheduling and residency vary between runs, so the test does not establish a universal deterministic slowdown; it does establish that reducing dispatch count alone is inadequate. This exact isolated capacity expansion was rejected and byte-restored. Future candidates must attack measured host submission/coherence cost or game-visible dependencies rather than only increase job batch size.

### HG-FAIL-067 - Moving resident error polling ahead of glFlush fails the authentic gameplay gain gate

HG-PERF-111 pre-`glFlush` resident `glGetError()` (one poll per resident flush, nonresident path untouched) preserved identical frame, EE RAM, GS VRAM/state, VU1 data/state, console, pinned input and 5,270 qualified original frame writers across exact authentic 180M candidate/early-control/late-control runs, with zero unexpected writers/caller mismatches/faults. Raw IOP RAM diverges at the known host-RTC payload; full RTC-masked memory equivalence has not been separately proved. Despite ~22% less resident synthetic flush time, candidate **did not beat either adjacent authentic control** in the final 30-writer tail: 11.7817 updates/s versus controls 13.3947 and 14.8537. Full-run elapsed host time varies across controls, so do not assert a fixed causal slowdown. Preserve the useful *mechanism* as a driver-specific observation, but **reject and restore the source change** because the >=5% actual throughput gain is absent. Driver waits can shift to later GPU/CPU coherence, and per-flush microbenchmarks are not gameplay FPS. Reconsider only with a demonstrably different synchronization strategy and rigorous output/fault proof, never by reapplying the same one-line query relocation.

### HG-LEARN-083 - Separate API-error observation from resident GPU submission when the driver serializes the pair

On RTX 5070 / NVIDIA 617.42, current resident flush attribution shows the final
post-`glFlush` `glGetError()` consumes ~88% of flush time while `glFlush()` itself is
only ~2.6%. Moving the same single error observation to immediately before `glFlush()`,
after all renderer GL calls for the batch, preserves ordinary renderer API-error
reporting and reduces identical resident synthetic flush time by ~22% (1,079-flush full
suite: 103.2423 ms -> 80.5049 ms). A deliberate NVIDIA `GL_INVALID_ENUM` still reaches
the existing `GS compute dispatch/readback failed` fault, and 1,159 resident GPU/CPU
cases pass. A follow-up current-driver query shows the matching 4.3 core GLFW context
uses `GLFW_NO_RESET_NOTIFICATION` (200705) on this NVIDIA stack. KHR robustness specifies
that this strategy does not deliver reset-event notification, so the old post-`glFlush`
poll was not a guaranteed one-flush-earlier reset detector; context state loss itself is
still not guaranteed impossible. This removes the specific notification-order objection, but the exact authentic
180M qualification subsequently **rejected** the proposed relocation for missing its
>=5% gameplay-throughput gain. See HG-FAIL-067. Do not remove polling entirely or
infer material gameplay throughput from nonresident or isolated flush timings.

### HG-FAIL-066 - Match synthetic mode to the production branch before using an A/B

A resident-only renderer change cannot be evaluated by the default `gs_gpu_tests`
invocation. The ordinary 1,159-case run creates its main accelerator with `resident=false`;
a later HG-DIAG-108 timer proved the 1,058-flush workload had zero resident `glFlush`
time. Therefore the apparent 3.85% HG-PERF-110 candidate/control difference on that
workload was ordinary run variance and does not measure the moved resident buffer-update
barrier. The candidate is restored and remains unqualified, not proven slow. For future
renderer A/Bs, pass `--resident` (and `--triangles` when appropriate) or otherwise prove
activation from resident-only counters before interpreting timing. A test that passes
correctness but misses the changed branch provides no performance ceiling.

### HG-FAIL-065 - Deferring a disjoint CPU-fallback flush can only move GPU submission

Do not remove the eager triangle CPU-fallback flush merely because the retained bulk
`GsCpuMemoryScope` proves the fallback pages are disjoint from queued GPU pages. In a
contiguous retained final-draw sample, 305/306 self-feedback rejects need no bulk-scope
flush even under deliberately over-broad queued sprite masks. However the preceding
triangle batch is already flushed by the first sprite; the sprite commands then remain
queued. Current fallback submits those commands at the first reject, while the deferred
candidate would submit the same commands at the first later accepted triangle because
`render_triangle()` flushes when `commands` is nonempty. Thus the actual dispatch is
moved, not removed. Resident `flush()` calls `glFlush()` specifically to overlap GPU
execution with later guest/CPU work, so deferring across a long scalar fallback island
can lose overlap. This structurally explains the old ~+0.45% result and removes the
credible >=5% ceiling for unchanged HG-PERF-109. Preserve the eager flush unless a new
mechanism can prove it removes work rather than only postponing submission.

### HG-LEARN-082 - Use stage deltas to set optimization ceilings before writing code

When a production-shaped fixed-rate observer already brackets the hot path, subtract
adjacent checkpoint aggregates before proposing another renderer mechanism. In the
exact 175M--180M triangle tail, 2,703 sampled complete dispatches add 2.109 ms while
measured inner stages explain 1.864 ms (~88.4%); ordered backend flush alone contributes
0.821 ms (~39.0%). Setup, coverage, packing, conversion, palette and tile insertion
are each much smaller, matching the weak or negative results of recent micro-changes.
Treat whole-route flush/readback counters as context, not tail attribution, and do not
infer GPU saturation from host flush time when independent GL timing shows headroom.
The next useful experiment is flush-reason attribution at the same exact tail, not
another arithmetic/payload tweak. If the exact qualifier is unavailable, keep
production unchanged and record the diagnostic boundary instead of stacking an
unqualified candidate.

### HG-FAIL-064 - Exact first-covered edge stepping is too small alone

The pre-admission triangle search can replace three determinant recomputations per
candidate pixel with exact signed-64 edge stepping while preserving the identical
row-major first-covered sample and top-left rule. Build/runtime/translation/VU and
1,159 GPU/CPU cases pass, and the exact 180M candidate/control artifacts are byte-
identical. The matched 175M checkpoint improves only 12.0187 vs 11.9157 (+0.86%) and
the final tail 12.0512 vs 11.9113 (+1.17%). The arithmetic factoring is valid but its
standalone ceiling is below the 5% gate; the source is restored. Do not retry this
unchanged. A larger win must remove work across broader triangle setup/replay rather
than only the first-covered determinant scan.

### HG-FAIL-063 - Rare wide-STQ fallback recovery has too little performance ceiling

The native-relaxed renderer can safely recover the perspective triangles rejected
only because `coefficient * area` exceeds the signed-64 prepared representation.
A widened checked path passed 1,159 GPU/CPU cases plus runtime/translation/VU suites,
activated exactly 6,195 additional GPU triangles on the authentic 180M route, and
preserved display, EE, GS, VU and console artifacts byte-for-byte. But 6,195 is only
about 0.028% of the ~21.99M triangle draws. An immediately adjacent enabled/disabled
pair measured 11.9849 vs 11.9479 updates/s at 175M (+0.31%) and 11.9595 vs 11.9406
at 180M (+0.16%), far below the retention gate. Keep the capability disabled and
do not spend more complexity optimizing this narrow overflow class unless a later
workload census shows it has become materially more common.

### HG-FAIL-062 - Do not prepack native-float STQ in producer validation

The native GL consumer formerly converted nine validated int64 STQ coefficients
to float after copying each triangle job. Moving those identical casts into the
producer's existing per-vertex validation loop was byte/pixel exact and removed
all 15.67M backend conversions, but reduced the saturated tail from
20.1939/20.1994 to19.1972FPS. The likely code-shape/register-pressure cost in the
already large producer outweighs the cold consumer pass. The candidate is removed;
keep the conversion consumer-side unless a future job layout removes the int64
representation entirely without expanding the producer hot loop.

### HG-FAIL-061 - PSMT8 page revisits do not cross a safe local cache boundary

A per-`write_image_transfers` 512-page pointer cache is semantically safe when
cleared before scalar fallback and does collapse revisits in a synthetic wrapped
call. It has no material production reuse. The unchanged 4,164,551
`bulk_cpu_scopes` initially appeared to quantify IMAGE mappings, but source audit
shows it counts `GsCpuMemoryScope` raster coherence instead; do not infer one
observer interface's traffic from another's counter. Initializing the unused
table regressed the tail from
20.1939/20.1994 to19.1746FPS. The candidate was removed. Do not retry a local
page-pointer table; a future reduction must safely coalesce owned IMAGE payloads
before application or change the coherence interface with explicit ordered
lifetime/version proof.

### HG-LEARN-081 - Replace repeated quotient overflow proofs with exact wide products

`PreparedCoordinate::prepare` used `magnitude > INT64_MAX / area` for every S/T/Q
vertex coefficient, causing up to nine variable 64-bit divisions before triangle
coverage rejection or GPU admission. A native 64x64->128 product tests the exact
same bound (`_umul128` on MSVC x64, `unsigned __int128` on GCC/Clang), with the
division definition retained on other hosts. 200,004 direct comparisons, native
regressions and two byte-exact 180M qualifications pass. The saturated tail rises
from18.86--18.93 to20.1939/20.1994FPS (+6.66% to+7.10%) without changing the
15.72M accepted triangles or27,266 batches. Prefer exact wide arithmetic when a
hot validation predicate otherwise divides; retain the portable definition and
do not weaken coefficient/fault bounds.

The same substitution is not automatically profitable after GPU admission. Three
shifted coefficient proofs in job packing were exact but reduced the tail to
19.9468FPS versus the retained20.1939/20.1994. They were removed. Keep the wide
product in the nine-call preparation loop; do not broaden it to the packer without
a different code shape or new evidence.

### HG-FAIL-060 - Scalar triangle quotient setup is not the slow-tail ceiling

Deferring `GsTriangleQuotient` construction until the scalar pixel fallback was
semantically exact: native regressions passed and the 180M replay matched display,
EE, GS, VU and console artifacts. Despite avoiding the reciprocal for uncovered
and GPU-admitted triangles, the tail measured18.1571FPS versus the retained
18.8598--18.9329. The change was removed. Do not target this constructor again;
the remaining renderer work lies in accepted GPU triangle construction/replay or
larger state/coherence costs, not scalar interpolation preparation.

### HG-LEARN-080 - Choose masked GPU RMW or page materialization by transfer size

The retained per-word GPU masked path is still right for small partial PSMT8
uploads, but the 180M scene issued 2.55B masked word updates. For complete IMAGE
runs of at least256 qwords, materializing each physical GS page once and applying
all ordered lane writes directly removes the host merge/index storm. Two exact
runs preserve every retained non-RTC artifact and improve the saturated tail from
17.35--17.41 to18.31--18.41FPS (+5.2% to+6.1%). This intentionally accepts more
page upload/readback traffic; the saved CPU merge work is larger in this workload.
Keep the size split, page/order/fault tests and scalar fallback. Do not generalize
that readback is always cheaper than masked GPU writes or lower the threshold
without adjacent evidence.

When one supplied run proves a complete aligned 128x64 PSMT8 page replacement,
old resident contents need ordering but not download. Invalidate only those old
pages, write every byte in the host shadow, then upload the replacement normally.
Strict guards reduced downloads from ~11.0GB to ~7.9GB and raised the tail again
to18.86--18.93FPS with exact captures. Never use discard semantics for partial
pages, wrapped coordinates, incomplete IMAGE runs or an unproved format.

### HG-FAIL-059 - Removing both sides of repeated triangle-palette work still has too little ceiling

On the exact 180M route, lazy GS-side CLUT expansion plus one versioned palette
copy per ordered GL batch was correct and mechanically strong: triangle batches
dropped 46,227 -> 39,848 and GL flush time dropped 10.263 -> 9.142 s. Yet the
saturated 175M--180M interval improved only 17.3488 -> 17.5313 updates/s (~1.05%),
far below the 5% gate. This extends HG-FAIL-042 beyond backend-only deduplication:
even eliminating per-job zero/fill work does not address the dominant tail cost.
Do not retry palette packing, hashes or payload thresholds without new evidence;
target per-triangle construction/coverage or GPU per-pixel replay.

### HG-LEARN-078 - Model cache index operations as cache-state transitions, not RAM traffic

When a reachable EE CACHE encoding is new to the AOT decoder, derive the exact operation from the official instruction manual before assigning any host effect. `DXIN` (`CACHE` op `0x16`) clears the selected data-cache line's Lock/Valid/Dirty state while preserving LRF and requires the documented synchronization sequence. In this runtime's coherent-cache abstraction every modeled data-cache line is already invalid, clean, and unlocked, so the exact modeled transition is state-neutral: keep an explicit decoder/emitter/runtime helper, but do not turn the indexed cache operation into a RAM read/write or a generic ignored instruction. Test invalid-looking base values so accidental address translation or host-memory access faults are caught, and keep writeback/hit operations separate because those can have ordering or memory effects.

### HG-FAIL-058 - Bigger VU1 speculation window and spinning workers did not help a saturated host

With ~2100 VU1 activations per gameplay frame, raising the in-flight window from 7 to 12/20 or letting workers spin 50/200 us before sleeping did not raise FPS (200 us spin cost ~8% from hyperthread contention). Workers looked ~45% busy, but the 8 logical CPUs (2 HT P-cores, 4 E-cores) already carry EE, VIF/VU, GS and worker threads. Once total busy time is near the host's real core capacity, reduce work per activation rather than adding parallelism.

### HG-FAIL-057 - Stopping the bottleneck stage from stealing VU1 jobs made it slower

A profile showed ~50% of stage B (the busiest device thread) in VU arithmetic from jobs it stole from workers, so B looked like it was doing the workers' job instead of parsing VIF. Forbidding steals (B runs only the job it must wait for) cut FPS from 9.25-9.52 to 8.77-8.95 with 3 workers. B steals only when its ring is empty or it must wait for a commit, so that time was idle. Measure idle and wait directly (HG-DIAG-091 per-interval counters, `/proc/<pid>/task/*/stat` CPU deltas) before treating a hot profile entry on a spinning/stealing thread as a bottleneck.

### HG-LEARN-077 - Speculative parallel VU1 activations need lane-granular read and write sets

Running VU1 batch activations on worker threads from predicted start state and committing them in order, with exact sequential re-execution on any input mismatch, kept every replay digest identical. Gameplay FPS went from 6.61 to 8.70 with 3 workers on 8 logical CPUs (2 workers: 7.90; 1: none; 4-5 workers: 7.2-7.4, oversubscribed). Inputs must be precise or the jobs re-execute. Vector-granular reads made 103k of 155k jobs re-execute, because the double-buffered programs SQ.xyz and then read the same vector. Lane bits that exclude lanes the activation already wrote cut this to 439 re-executions (154.6k valid). Write sets must also be per lane (partial SQ, V2/V3/masked UNPACK), or the merge corrupts VU memory (`s-w2g`). Clip/Q/P inputs are tracked dynamically (only bits FCAND consumed; Q/P only if an old value was read); static liveness covers VF/VI/ACC/I. Test the dependency logic in a synthetic stream where it is the only reason to re-execute. In the dense stream other reasons hid both mutations. Evidence: `~/hg-evidence/l-w2a`, `l-f{1,2,3}-w*`.

### HG-LEARN-076 - Coarse EE / device thread split is exact and faster when joins are rare

Measure the join budget first (HG-DIAG-090: only GIF STAT reads observed device state in gameplay). Then give whole device subsystems to host threads, fed by an ordered SPSC ring of the synchronous inputs, and join only at guest observations. Unlike HG-FAIL-041 (one handoff per VU activation), this kept every digest identical and raised gameplay FPS from ~3.4 to 4.4-6.0 on Linux. Splitting VIF1/VU1 from GIF/GS needs only a GIF packet-boundary proxy (`gif_packet_size` rules) for XGKICK's idle check. Answering transport reads at the VIF1/VU1 stage avoids draining GS work. Judge by gameplay FPS with interleaved sync controls (`HG_SYNC_DEVICES=1`), not whole-run wall, which is real-time paced.

### HG-FAIL-056 - Two-field same-pair SQ snapshots instead of whole-Vu1State copies gave no gain

The emitter copies all of `Vu1State` (~1.8 KB) for each same-pair SQ whose source VF the upper op writes (20 ordinary sites plus the fused program-1 loop). Capturing only `vf[fs]`/`vf_defined[fs]` passed 81920 compiled comparisons against the old whole-state form (a defined-bit mutation was caught), but replay instructions were 318.62G -> 318.72G with cycles in noise: GCC already turns the copy into a few wide moves. Reverted. The test-only differential switch idea is reusable if SQ emission changes again.

### HG-FAIL-055 - GCC -O2 for the generated main/VU unit did not help on Linux

Mirroring the MSVC /O2 main-unit override for GCC (`out/translated.cpp` at -O2, shards kept -O1) passed the exact replay but gave 317.94G -> 317.26G instructions (-0.2%) with user cycles 96.5G -> 98.7G, and a 6-minute rebuild. Reverted. The GCC build keeps -O1 for all generated units.

### HG-FAIL-054 - Packed XYZ product/MADD in `vu_xyz.cpp` repeated HG-FAIL-019 on Linux

Routing `vu_xyz_multiply_acc`/`vu_xyz_madd` through the exact four-lane `try_fpu_product4`/`try_fpu_madd4` (W computed, never published) passed 196608 differential state/fault comparisons and the exact replay, but instructions went 318.62G -> 317.94G (-0.2%) with flat cycles. A scratch microbenchmark had shown 3x per call, but its random operands mispredicted scalar branches; the game's operands are predictable. Do not size VU helper changes with random-operand microbenchmarks. Samples inside these helpers are spread evenly, not concentrated in the arithmetic.

## HG-LEARN-075 - Submit GL compute work when it is queued, not when it is read back

A resident GPU renderer that dispatches compute work but defers readback must call `glFlush()` after dispatching. Without it, Mesa (iris) keeps the batch on the CPU until the next `glGetBufferSubData`, so the GPU starts only at the sync point and the CPU waits for the whole batch. Adding `glFlush()` at the end of each resident `flush()` cut replay readback wait from 8.75 s to 1.70 s (wall -7%) with unchanged instructions and exact state. To find sync costs, attribute each readback to its caller stack (scratch `backtrace` probe) instead of removing one sync class, which only moves the wait (HG-LEARN-074).

## HG-LEARN-074 — Rank CPU raster fallback by first rejecting GPU gate and include flush/readback time

A per-draw classifier that records the first GPU admission gate a CPU-fallback primitive fails, with bbox pixels and host time from the flush through the CPU loop, found that two unsupported texture formats (PSMT4/PSMT8 FST sprites, ~580 draws) cost more wall time than 70K+ other fallbacks. Porting the exact CPU `DrawTexture` scope into the GPU sprite shader, using wrapped texel coordinates instead of separable addresses, cut Linux user instructions by 15.7% with exact replay state. Keep the probe out of the tree; record its ranking in PROGRESS. On an iGPU, `readback_ns` is dominated by waiting for queued GPU work. Removing one class of readback, such as presentation, only moves the wait to the next sync point.

### HG-FAIL-053 — GPU self-texel feedback triangles repeat HG-LEARN-046 without CPU savings

An exact per-tile GPU path for the proven same-pixel PSMT8H feedback class (flagged job reading its own frame byte) passed 64 new differential cases and exact replay state, but Linux user instructions were flat (-0.02%) because the scalar fallback already uses the collapsed direct-byte path. Windows previously measured this move slower. Do not revisit without new evidence that the scalar path, not batching, dominates.

## HG-LEARN-073 — On a noisy host, compare user-mode retired instructions, then locate hot paths with LBR stacks

On the Linux dev container (i3-1315U, `powersave`, P-cores capped at 1.2 GHz), identical fixed gameplay33M runs vary by ±10% in both FPS and `cpu_core/cycles/u`. `cpu_core/instructions/u` repeats to within 0.01% (382.749B and 382.724B). Pin the run with `taskset -c 2` (a P-core) and use `perf stat -e cpu_core/instructions/u`. That verdict is deterministic for CPU-work changes. Confirm a positive result with wall-clock pairs on a quieter host, because instructions ignore cache and memory stalls. For hot-path structure, `perf record -e cpu_core/cycles/u --call-graph lbr` works in the container without frame pointers or extra capabilities. Driving the replay through `hg_runtime_verifier.verify_frames` with only the Windows-layout provenance check bypassed keeps every writer, fault and state-digest check. Evidence: 2026-09-28 session, `~/hg-evidence/st-*`.

### HG-FAIL-052 — Compiler inlining/optimization level alone does not reduce VU helper cost

Compiling the GCC main unit (VU1 AOT programs) at `-O2` instead of `-O1` cut whole-replay user instructions by 0.4%. Force-inlining twelve `Vu1State` helpers (arithmetic_q, multiply_vector, add_vector, convert_integer/fixed, lane/broadcast/ACC variants, clip_test, load/store_qword) so emitted constant register indices and masks could fold cut them by 1.2% (379.1B vs 383.4B) and doubled full rebuild time. Neither produced a measurable FPS change, and exact retained state held in both. The helpers' cost is the checked soft-float, flag and definedness work itself, not call overhead or constant masks. Do not retry optimization-level or inlining changes for VU helpers; reduce the work that is executed (for example flag liveness or proven-defined lanes) instead.

## HG-LEARN-072 — Severe host drift requires adjacent reversal before judging an optimization

HG-DIAG-088 initially looked like a large regression only when compared with a nonadjacent clean13.5107FPS sample: the exact-state candidate measured10.9755FPS. Byte-restoring the three changed files and rebuilding immediately produced an exact-state adjacent clean control of only10.4785FPS, making the candidate ~4.74% faster instead. Therefore do not classify a candidate from an older clean sample when the host has drifted materially. Preserve correctness first, use the closest exact-restored control, and if the apparent gain/loss matters retain it only after a second close candidate/control repeat. Scope: fixed33M Haunting Ground workload; production088 remains restored because the positive result is not yet repeated.

## HG-LEARN-071 — Calibrate sampled-operation timing against the timer floor

A high dynamic call count multiplied by sampled scope time can badly overstate removable work when each timed scope approaches the clock-query overhead. HG-DIAG-087 sampled about1/256 completed VU helper calls only in31M..33M and preserved exact state. Convert measured35.91ns/sample and Q arithmetic36.20ns/sample, establishing an empirical ~36ns timer/scope floor; their raw256x totals therefore are ranking evidence, not literal host-cost estimates. Lane ADD/SUB measured55.69ns/sample across2,974,020 calls and EFU79.66ns/sample across759,600 calls, giving a stronger excess-over-floor signal. Use both coverage and excess per sampled call to choose the next target, remove probes before FPS qualification, and never compare instrumented FPS to clean production. Evidence: `vu-family-sampled-timing-20260927/summary.json`; clean post-probe verifier `ca3a899a53754af284188c4a898dec4f`.

## HG-LEARN-070 — Measure CPU construction before a native render-target rewrite

A low-GPU-occupancy result does not by itself justify native render targets. Partition the CPU frontend and backend first with non-overlapping scopes. In this workload the complete GL CPU construction/upload/dispatch/materialization path was only453.062ms of a5.16963s heavy span (8.76%), while complete packet application was1.479s inclusive and overlapped some materialization. Even an intentionally impossible bound deleting both categories stayed below30 original boundaries/s. Scope: Haunting Ground current31M..33M scene and retained renderer. Result: close standalone GPU-native render targets and preserve them only as a component of a future multi-bucket design; do not repeat renderer-local batching/readback/worker variants without new measured ceiling. Evidence: HG-DIAG-083/084 and `%TEMP%/hg-diag084-cpu-gs-20260927`.
### HG-LEARN-069 - Log delivered controller states, then separate observed replay from inferred cleanup

HG-DIAG-006 captured107 changed HostInput states with exact guest slices while the user drove the door route. Convert each line losslessly to `slice pad:buttons,4axes,12pressures`, validate strict ordering and every field, and preserve that observed file unchanged. Because the guest faulted while the final action button was held, a physical release could not be delivered; keep a second replay file that adds one explicitly labeled neutral state at the next1024-slice poll. Never disguise that inferred cleanup as observed input or capture keys outside hg_game. This makes later AOT repair replays unattended without weakening input semantics.
### HG-LEARN-068 - Repeated calls on one object still prove only the observed virtual slot

After1aa040 executed, the same object17b1b80 reached1a9f90 through a different caller slot+0x14. The vtable was already known to contain many initialized methods, but that proximity did not make them reachable. Capture the new callsite and slot, independently verify exact original/live bytes and return boundary, and root only the newly observed entry. Here the complete84-byte method added exactly21 instruction cases, lost none and preserved fixed state. Sequential object activity is evidence of route progress, not permission for a vtable sweep.
### HG-LEARN-067 - An observed virtual slot does not bound its neighboring vtable methods

A later door-transition stop reached1aa040 through caller1271b4 and live object17b1b80 vptr46abb0 slot+0c. The table contains many other non-null methods, so the observed dispatch proves only1aa040, not a callable family or extent for its siblings. Independently bound the body from original/live byte identity and decoded return/delay1aa710/1aa714, root only the observed entry, and leave four internal dynamic transfers explicit. Generation added596 cases with no losses and fixed qualification preserved all state. Continue from the live route; do not sweep adjacent vtable entries merely because they are initialized.
### HG-LEARN-066 - A mutable member descriptor can expose sequential original callbacks

The same live object+a0 descriptor changed from197640 to196fc0 after the first door callback executed. Each value had independent initialized-original provenance and byte-identical live code. Preserve the earlier root and add only the newly observed callback; do not assume that a repeated helper return means the prior fix failed or import every descriptor variant. Callback196fc0 added416 body instructions plus independently reached direct callees, retained explicit dynamic-transfer faults, and passed fixed state qualification. Continue live capture until the interaction completes.
### HG-LEARN-065 - Preserve successful route progress and root only the next observed member callback

After25f0a0 was compiled, the user passed the staircase and reached a different explicit door stop at197640 through generic member helper100b40. This distinguishes a successful earlier repair from a recurrence. Live object1510c80 contained descriptor{0,-1,197640} at+a0; four initialized original records matched it, and original/live197640..197fe8 agreed byte-for-byte through the independently decoded return. Root only197640. The shared analysis entry was truncated and the manual list omitted the body, so independent original/live evidence supplied the entire scope. Generation added exactly618 cases and fixed qualification preserved all retained state. Continue the actual route with fault capture; do not roll back the proved staircase family or infer neighboring callbacks.
### HG-LEARN-064 - Treat a visual staircase freeze as an explicit AOT boundary first

The repeated staircase stop was not a renderer deadlock: external capture reported original EE target25f0a0 from common virtual caller1bbed0. Live objectfc8990 held vptr46c780 and slot+0c target25f0a0; original/live null-bounded table pair and both method bodies matched exactly, and original25e968..970 installed the table. Root only the independently proved two-method family25e950/25f0a0. Because the body constructs graphics work, discard the shared function-list lead and derive scope solely from original ELF and live RAM. Generation added740 original instruction cases with no losses; fixed qualification preserved all state. A standard33M replay cannot prove the later manual route, so retain external fault capture and require another live staircase traversal.
### HG-LEARN-063 - Measure true GPU elapsed time before a render-target rewrite

Nonblocking `GL_TIME_ELAPSED` queries around accepted heavy-window batches measured only703,482,880ns of GPU execution across2,045 sprite/triangle/masked dispatches in a roughly4.8-second host interval, with zero query-ring skips. CPU materialization downloaded83,722,240 bytes in97,509,700ns, and only339 of18,761 bulk scopes touched GPU-dirty storage. The GPU therefore has substantial headroom, but replacing backing storage or readback alone cannot close the30FPS gap. A credible GPU-native renderer change must also remove or overlap the CPU packet/state and draw-construction work that feeds those batches while preserving guest materialization and first-fault order. Poll timing queries without waits during guest execution, collect only after timed work, and never treat CPU submit time as GPU occupancy. HG-DIAG-083 was removed byte-exact and the final fixed verifier retained exact state.
### HG-LEARN-062 - A feasible coarse packet span can still fail the fixed throughput gate

HG-DIAG-082 proved that the heavy raw GIF stream can be preflighted into only120 observation/display barriers:152,526 packets,100% eligible,4.76MiB peak owned data,~7.49ms packet-copy time and zero control/fault divergence. A persistent bounded consumer then preserved every retained frame/EE/GS/VU/normalized-IOP digest and original writer provenance. That architectural correctness did not establish useful speed. Unpaced adjacent samples favored it, but the fixed realtime verifier measured13.06890581FPS versus the protected13.12060460FPS baseline. Prefer the fixed qualifier over favorable diagnostic timing, and reject a worker that merely moves packet/state application when display/readback/raster dependencies still serialize the dominant work. The useful reusable result is the120-barrier dependency proof; do not rebuild the same packet worker unchanged. For the next native-renderer route, first measure nonblocking GPU elapsed time for actual render targets and their materialization points.
### HG-LEARN-061 - Resolve sampled native PCs with the complete linker map

Visual Studio CPU ETL can sample an attached game even when WPR policy blocks a standalone trace, but the collector may omit a usable module-load record for a process already running. Cross-match exact instruction PCs against a same-executable trace with a loader event to recover the ASLR base; parse both ordinary `f` and optional `f i` map entries. Parsing only `f i` left924 symbols versus13,389 complete map functions and falsely attributed the hottest AVX2/XYZ code to unrelated CRT symbols. High-rate exclusive samples rank targets, but without exact original-slice markers or inclusive stack attribution they do not provide per-function wall time or justify an FPS claim. HG-DIAG-081 ranks dispersed VIF/VU/GS hotspots and confirms no single helper has a plausible30FPS ceiling. Keep the temporary symbolizer and ETL outside the repository.
### HG-LEARN-060 - Count complete packet intersections before another graphics worker

The original heavy31M..33M path applied152,526 complete GIF packets, partitioned into95,762 plain,50,224 CLUT-write-only,3,240 IMAGE-only and3,300 transfer-control/HWREG-only;49,564 distinct CLUT packets actually reloaded palette data. IMAGE-only admission therefore cannot remove the ~50K palette boundaries that slowed the earlier coarse worker. The IMAGE packets alone carry~108MB, so an async design must account for payload ownership/copy cost as well as synchronization. This does not prove a fully validated ordered GS shadow is impossible, only that the narrow worker is not the path to30FPS. Diagnostic080 was removed, clean verifier13.12060FPS/state passed. For temporary probes, avoid modifying widely included gs.hpp when a private header or cpp-only interface can suffice: touching it rebuilt generated EE shards twice and consumed minutes without changing the measured question.
### HG-LEARN-059 - Bound a shared VU guard by sampled completed calls

HG-DIAG-079 measured the nonzero-register VF readiness helper across the original heavy scene:12,489,360 calls,96.535% zero-wait. One-in256 completed-call timing estimates~0.41577s total, including timer overhead and perturbed inlining, inside ~4.5s heavy host wall. Removing every measured call would move13.11FPS only to~14.42FPS. Do not prioritize a narrow zero-wait guard/advance rewrite as a route to30FPS merely because call count is high. A different shared VU operation could still matter if separately measured. Probe was removed and clean source/game rebuilt.

### HG-LEARN-058 - Bound IMAGE conversion before a bulk-upload rewrite

HG-DIAG-077 sampled completed original IMAGE qword CPU application by destination format in the heavy gameplay window. Fixed64x estimates are~0.2457s PSMT8,~0.0715s PSMT4 and~0.0086s CT32, total~0.3257s. This is real work but only~7% of the ~4.5s heavy host interval, nested inside the~0.741s complete GIF bucket. Even deleting it all could raise13.11FPS only to~14.1FPS; a complex bulk converter cannot meet30FPS alone. Prefer structural VU/GIF/GS overlap with exact event/readback/fault behavior. Restore the sampled probe before performance qualification, as done here.

### HG-LEARN-057 - Use complete disjoint scopes for the residual host budget

After sparse slice sampling missed ~2.95s of bursty work, HG-DIAG-076 timed every completed heavy-window EE, IOP and SIF phase scope instead. It measured0.4942611s EE run/observation,0.1860953s IOP run/I-O and0.0537283s SIF in4.4590229s wall. Those scopes are mutually disjoint, but comparison with the earlier exclusive VU/GIF/raster run carries host drift and timestamp overhead. They explain ~0.734s of its1.310s other/overhead complement, so a renderer-only change to the remaining service work cannot plausibly double FPS. Keep exact guest clocks and fault boundaries; prefer structural removed work or useful overlap covering more than one major bucket. The probe was removed.

## HG-FAIL-051 - Four-stage XYZ matrix precompute is exact but loses to the original per-op helpers

HG-DIAG-086 used a whole-program census before implementation:86 equal-frequency matrix-like chains across Programs2-12 cover4,115,520 non-hot XYZ operations, and the initial candidate admitted only statically stable full `mulax+madday+maddaz+maddw` chains. It reused the existing exact state-pure matrix preparation and preserved original per-pair clocks/readiness/lower/fault boundaries;98,304 compiled full-state/fault comparisons passed with14,044 actual fast-path hits, and the original33M route retained864 writers plus exact frame/EE/GS/VU/normalized-IOP state. Performance nevertheless failed the close reversal: candidate13.58237FPS versus exact-restored adjacent control13.65387FPS, about0.52% slower. Do not repeat unchanged `prepare_vu_matrix` plus four stage commits across these chains: precompute/copy/duplicated generated-path overhead outweighs the arithmetic helper savings here. This does not rule out a materially different compile-time or batched arithmetic mechanism that removes more measured work without adding per-chain preparation.

## HG-FAIL-050 - Sparse per-slice phase sampling misses bursty rendering work

HG-DIAG-075 tried to divide the heavy31M..33M host time by differencing the existing1/256 slice sampler. Although it selected7,825 slices, scaling all phase samples yielded only1.3766912s against4.3229158s measured wall. ~2.946s of bursty work fell outside selected slices; apparent phase proportions are invalid for choosing a graphics or VU target. Prefer complete scoped timing around actual costly operations or sample completed operations, and reconcile estimated category totals with full wall before drawing a bottleneck conclusion. The diagnostic was removed and the clean game rebuilt.

### HG-LEARN-056 - Attribute AOT program time before broadening a local VU optimization

HG-DIAG-074 sampled1/16 completed VU1 activations in the heavy original scene by independently verified static program identity. The fixed16x estimate1.860544s closely agrees with an earlier exclusive1.907775s VU measurement. Program1 alone ~0.703173s and its already identified loop ~0.667296s; the other ~1.157371s is dispersed across12 programs, with the largest individual ~0.238s. An optimization confined to one of those programs has a low upside even if it is exact. Prefer a proved shared-cost mechanism or measured CPU-side graphics work, and do not choose from dynamic pair/call count alone. The sample was removed before FPS qualification.

### HG-LEARN-055 - Bound hot-loop upside with nested sampled timing

HG-DIAG-073 measured the original program1 loop rather than inferring host cost from its41.97M dynamic pair count. In heavy31M..33M,1/64 completed iterations sampled10.4265ms whole loop and4.3807ms nested fused transform, yielding approximate0.6673s and0.2804s after64x scaling. The loop is ~35% of the earlier1.908s exclusive VU bucket, but the transform only ~6% of the ~4.49s complete heavy interval. Even perfect transform arithmetic cannot reach30FPS. Use measured host time as a bound, account for nested timing and probe overhead, remove instrumentation before qualified FPS, and look for larger eliminated work across remaining VU and GIF/GS CPU service. This does not rule out a structural loop improvement.

## HG-FAIL-049 - Whole-loop VU memory preflight was exact but flat

HG-DIAG-072 proved a counter-bounded constant-stride program1 loop's complete VU-memory bounds, all future LQ definition masks and disjoint read/write ranges before guest mutation, then used direct selected-lane memory effects. Failure kept the original checked CFG and fault order. Python/runtime/translation,262144 arithmetic and81920 full VU state/fault comparisons passed; original frame, EE, GS, VU and normalized IOP captures plus864 writers matched. Yet a close reversal measured candidate12.91581 -> clean12.58608 -> same candidate12.60928FPS. The apparent first lead was host drift. Do not repeat unchanged preflight/direct-memory expansion; its up-front scan and larger generated code did not remove enough work. This does not rule out a different VU bottleneck. Full evidence in PROGRESS.

## HG-FAIL-048 - Direct VU-memory emission was exact but lost to the checked helpers

HG-DIAG-071 reused the retained static-loop proofs to bypass seven generic LQ/SQ/ISW helper calls per completed hot-loop iteration. It preserved exact selected-lane values/definition bits, the overlapping same-pair SQ snapshot, original checked CFG/fault fallback, arithmetic, clocks, readiness and overlap behavior. Python/runtime/translation,262144 arithmetic and81920 compiled full-state/fault comparisons passed, and the qualified original replay retained exact captures plus864 writers.

Performance failed reversal: direct-memory candidate `5078e954...` measured13.34493FPS, while exact-restored/regenerated clean `e3442936...` measured13.72106FPS immediately afterward. Source-level helper-call frequency therefore overstated the eliminable cost; the compiler/retained helper shape is already cheap enough that hand-expanding the lane operations does not help this workload. Do not retry unchanged direct LQ/SQ/ISW emission or choose another VU target from call count alone. Require measured pair/helper attribution or an operation that removes real arithmetic/state work.

### HG-LEARN-054 - Measure GPU elapsed time before replacing an already-GPU raster path

A host flush duration or draw count does not establish that the GPU is the frame limiter. HG-DIAG-069 used bounded nonblocking GL_TIME_ELAPSED queries only during heavy31M..33M and measured0.371610336s of current compute execution:0.245050592s triangles,0.122800640s sprites and0.003759104s masked transfers, with2045/2046 results available,0 pool skips and no forced query wait. Adjacent clean heavy host time is4.48794s, so GPU execution is only ~8.3% of that non-additive interval. Even an impossible fully serialized removal of all measured GPU work would reach only~14.58FPS from the current workload.

Therefore do not justify a large native render-target/hardware-rasterization rewrite by shader execution cost alone. Such a rewrite may still be useful only if it also removes substantial measured CPU-side GIF/GS preparation/synchronization. Keep asynchronous GPU timing as the gate: never glFinish or block on immediate query results in the measured path, and restore diagnostic query code before qualified FPS runs.

## HG-FAIL-047 - Caching VU static-program identity was exact but slower than rescanning MicroMem

HG-DIAG-068 used the existing static AOT contract to cache only a successfully matched VU program id, keyed to a MicroMem generation invalidated after each complete MPG commit. Partial MPG payloads did not mutate or invalidate; misses retained the original ordered full-image matchers and explicit no-translation fault. Python/runtime/translation/VU readiness plus qualified gameplay all preserved exact state and864 original frame-writer events.

Despite126,180 measured heavy-window VU activations versus only3,960 MPG uploads, the cached candidate was slower: identical game `ea4f0575...` measured13.37745 and13.38073FPS, while exact-restored/regenerated adjacent control `28338556...` measured13.63652FPS. Do not assume repeated static MicroMem comparisons are material merely from comparison count; on this host they are cheaper than adding cache state/branches and are not the heavy-scene VU limiter. Keep the original ordered identity scan unless a materially different measured dispatch mechanism appears.

## HG-FAIL-046 - Whole-transform AVX-512 embedded rounding lost to the retained AVX2 path

The exact two-transform helper can use the independently derived AVX-512 RTZ+SAE multiply/add primitive atomically: keep all eight stages host-private, reject exponent/range edge cases before publication, and fall back to the existing AVX2 whole transform without partial state.262144 arithmetic comparisons across16MXCSR modes,81920 compiled CFG/fault comparisons, runtime and translation tests all pass, and qualified gameplay retains exact frame/EE/GS/VRAM/VU/normalized-IOP state plus864 original writers.

Performance nevertheless fails adjacent reversal. Corrected candidate game70fc003a measured13.39402 and13.19900FPS; exact-restored clean game1f9bc032 measured13.62447FPS immediately afterward. The earlier per-instruction ER route and this whole-transform ER route are both closed for this workload. Do not infer that fewer integer arithmetic instructions or embedded rounding is faster on the current CPU; retain the proven AVX2 fused transform unless a materially different measured mechanism appears.

## HG-FAIL-045 - Batch GS register restoration did not improve throughput

066 saved/restored the live128-register bank once per pending batch, retaining every captured environment and original uncaptured-draw semantics.80 differential mixed/fault cases and real captured state passed. Candidate13.01247 versus adjacent restored12.99300FPS is flat; the earlier13.49206 control had drifted. Removed source/test trial; do not infer memory-copy count alone establishes a hot cost, or claim a regression against the nonadjacent control. Evidence and archives in PROGRESS.

# Learned skills

Project memory for implementation decisions. Read before changes together with
`rules.md` and `AGENTS.md`. Entries describe evidence-backed techniques and their
limits; they are not permission to substitute guessed behavior or skip validation.
Update an existing entry when possible. Do not promote a hypothesis to a fact.

## Successful techniques

### HG-LEARN-053 - Fuse exact arithmetic around live state, then validate with reversal controls

Two full-lane VU transforms can share a host helper when AOT proof and per-entry/per-iteration guards establish defined stable operands, no intervening observable ACC/MAC/STATUS use, no intermediate fault, and preserved lower-operation/Q/P/readiness boundaries. Preserve separate product/add rounding and sticky flags from every stage; publish only the two final VF results, final live ACC, MAC/current and combined sticky flags at original consuming boundaries. A runtime cache or arrays of all intermediate results are not required. The original checked CFG remains mandatory on guard failure.

HG-DIAG-061 has262144 scalar/packed/reference arithmetic comparisons across16MXCSR modes and81920 complete CFG/fault comparisons. Qualified candidates13.39465/13.40776, adjacent disabled12.95555 and restored candidate13.42153FPS preserve all captures. Retain the modest win; do not generalize to arbitrary blocks without the proof. Combining both chains into eight-lane additions (062) remains exact but measures13.28230FPS and is rejected: fewer packed instructions alone does not establish a host gain.

Heavy-window attribution must share the actual timing scope.057 found only339 dirty raster scopes/1340 pages in31M..33M, whereas3111/75809 were whole-replay totals.058 readback plus flush costs about0.31s in that heavy window, directing attention back to the much larger VIF/VU/GIF CPU path. Never use whole-route counters as evidence of a slow-scene bottleneck.

### HG-LEARN-052 - Optimize coherence by actual dirty intersections, not geometric page-count potential

A31M..33M census of the proved PSMT8H same-pixel CPU-feedback class found a real geometric difference:18,378 draws/794,236 covered samples used98,312 conservative frame+depth rectangle page references but only66,374 pages were actually touched (-32.49%);8,376 draws had reducible masks. All18,378 simultaneously required PSMZ24 GEQUAL reads (ZTST2,ZMSK1), so depth visibility itself could not be removed. An exact pre-raster coverage pass then used only the actually touched frame+depth pages for CPU coherence while leaving the original raster unchanged and preserved all qualified state.

That exact mask shrink did not improve the real dependency boundary: nearby clean backend download was1,526,636,544B/readback1.3018276s, candidate1,532,370,944B/1.3554876s, with identical3111 dirty CPU scopes and75809 dirty pages; adjacent qualified FPS was12.7348789 candidate versus12.7180079 clean. Therefore page references that are clean or CPU-owned do not matter. Future readback work must classify and remove **GPU-dirty intersections / synchronization events**, not merely prove that a geometric footprint can be made smaller. Do not repeat coverage prepasses or mask-minimization based only on total page counts.

### HG-LEARN-051 - A proved scheduler fast-exit still needs a large eliminated cost to matter

The cooperative EE poll emitter can safely bypass generic MMIO-aware `State::load()` only after it has independently proved an aligned in-bounds ordinary-RAM address and no trace watch covering the recognized wait loop. Doing so preserved all guest state, faults, original writer qualification and retained capture hashes. But the fixed candidate executable measured12.9970 then12.8335FPS while nearby clean controls ranged12.7235..12.9311FPS. The host-call shortcut is therefore below useful run variance and must not be retained on source simplicity alone.

Use this as a scale filter: even extremely hot cooperative boundaries are not automatically worth changing if the removed operation is already cheap. Prefer mechanisms that eliminate measured GPU/CPU synchronization, VU arithmetic, or similarly large exclusive work. Keep the original proof structure around wait loops; do not broaden a direct RAM access to MMIO/alias cases merely to enlarge activation.

### HG-LEARN-050 - Adjacent reversal controls close exact VU micro-optimizations despite host drift

The program1 `0x0170` lower SQ overlap can be implemented without copying the full `Vu1State`: capture only VF10 X/Y/Z raw bits, its defined mask and VI4 before the upper MULq, then perform the original bounds check/store after the upper operation. Extensive synthetic comparison had already proved that compact snapshot exact, but old gameplay timings were confounded by host drift. A close 2026-09-24 A/B resolves it: the same candidate executable qualified at12.8547096 and12.8780513FPS, while exact emitter regeneration and a fresh clean rebuild qualified at12.9310902FPS with the same retained frame/EE/GS/VU/RTC-normalized IOP state and864 original writers.

Therefore the full-state copy looks expensive in source but is not the heavy-scene throughput limiter. Do not retry compact SQ or similar state-copy micro-tuning without a new measured reason. When host throughput drifts, keep candidate binaries fixed for repeats, then regenerate/rebuild the exact source control immediately and compare qualified original-writer FPS before retaining a small optimization.

### HG-LEARN-049 - Collapse renderer dispatches only when the replacement execution model is cheaper

The CT32 sprite workload really does contain large dependency-safe batches: both the rejected tile-list renderer and the ordered-fragment-interlock renderer moved **40,640 sprites into only 2,607 batches** while preserving the qualified final frame, GS VRAM, EE/VU/GS state, RTC-normalized IOP state and all 864 original frame-writer events. That proves the dependency classification, but not the host execution strategy. Per-pixel tile replay measured12.8024581FPS and hardware rectangle rasterization with ordered ARB fragment interlock measured12.7043817FPS against nearby clean ~12.9-13.0 controls. Fewer dispatches/barriers therefore do not automatically beat the existing compute renderer on this GPU.

Reuse the dependency proof if a materially different renderer architecture needs it, but do not retry unchanged tile lists, wave grouping or fragment-interlock rectangles. Measure the real replacement work, synchronization and GPU pipeline cost, not just dispatch count. Likewise, a scheduler-only idle DMA pre-gate that removed repeated no-op pump entry was exact but flat at12.9777712 versus adjacent clean13.0059025FPS. Small host-call elimination is low priority unless it removes a measured dominant cost. Current exclusive heavy-window evidence instead puts VU execution around1.91s versus whole raster around0.51s, so the next optimization should attack structurally repeated VU work or another comparably large measured bucket.

### HG-LEARN-048 - Verify generated-object freshness during rollback; fewer static dispatches are not automatically faster

A source-level rollback can still leave a previously compiled generated translation object linked into `hg_game.exe`. After rejecting an IOP straight-line switch-fallthrough candidate, restoring `iop_emit.py`, its tests, CMake and `out/iop-translated.cpp` did not restore the binary: MSBuild reused the candidate IOP object. Force the affected game-derived generator to run with the exact configured module set, force its object to recompile, then verify the post-link manifest and executable hash. If CMake regeneration leaves only project metadata newer than an otherwise clean executable, a no-op source touch may be used only to force the relink; restore that touch immediately and relink once more before measurement.

The IOP fallthrough itself preserved one budget decrement, trace point, `finish_instruction()` and `s.pc` update per original dispatch and kept control/native/import/fault boundaries intact, but qualified gameplay measured 12.9132509 FPS versus the adjacent clean 13.0059025 FPS control with 864 original writer events and matching retained frame/EE/GS/VU/RTC-normalized IOP state. Therefore reduced `switch(s.pc)` count is not a demonstrated win for this workload; do not repeat the unchanged mechanism. Always compare against an adjacent provenance-valid qualified control rather than assuming code-shape simplification improves host throughput.

### HG-LEARN-047 - Profile sprite fallbacks by pixel cost, then port exact missing GS state into the existing GPU path

Draw count alone hid the useful sprite targets. A31M..33M pixel-weighted classifier showed the remaining CPU sprite work was dominated by alpha-test sprites (960 draws/6.88M pixels), then PSMZ24 framebuffer sprites (480/3.44M), not the small-draw bucket. Extending the already-correct GPU sprite path with exact CPU TEST/AFAIL semantics raised qualified gameplay from12.25..12.30 to12.87..12.90FPS while preserving retained relaxed frame/VRAM/core hashes. Adding PSMZ24 destination support then reached13.21..13.24FPS after fixing its separable Z32-layout origin: `psmz32_word(0,width,0,0)` contributes1536 words, so X-only+Y-only decomposition must subtract that constant once. The same pixel-weighted evidence exposed720 CT32-special CPU sprites averaging3584 pixels, just below that specialization's4096 GPU threshold; lowering only that proved CT32 path to2048 raised qualified gameplay again to13.54..13.56FPS while exact hashes remained unchanged.

Keep extensions narrow: port the scalar semantics already carried by the job rather than weakening admission broadly, and validate exact frame/VRAM for format additions. Alpha AFAIL KEEP/FB_ONLY/ZB_ONLY/RGB_ONLY, DATE/depth ordering, frame masks and PSMZ24 unused-high-byte preservation must remain identical. When separable GS address functions have nonzero origins, explicitly normalize one axis; do not assume address(0,0)==0. Threshold tuning should likewise be class-specific and evidence-driven: do not lower a generic GPU threshold because a distinct proved specialization has a profitable medium-size workload.

### HG-LEARN-046 - Keep proved framebuffer feedback on CPU when the proof can collapse texture work

For Haunting Ground's measured same-pixel PSMT8H feedback class, moving the draw onto the GPU was state-correct but slower because batching/hazard overhead exceeded the saved CPU work. The better use of the proof is inside the scalar fallback: when nearest PSMT8H, CT32 FRAME, same base/stride, Q==1, conservative wrap identity, and exact 12.4 vertex source-minus-screen deltas all lie in[0,15], every covered sample reads the high byte of its own current destination CT32 word. Read that byte directly as the CLUT index and apply the existing texture function; skip per-pixel perspective STQ division, wrap and PSMT8H address calculation. Any guard failure keeps the generic path.

Combined with retained bulk page-mask coherence, qualified runs measure12.2492477 and12.2974255FPS with exact retained relaxed frame/VRAM/EE/VU/IOP/GS state; GPU draw/batch counts are unchanged. This technique is valid only for a separately proved self-feedback class. Do not generalize own-destination sampling or constant-Q assumptions from format/base equality alone.

### HG-LEARN-045 - Bulk page-mask coherence beats per-range CPU raster synchronization

With resident GPU VRAM, a CPU raster fallback should synchronize its proven read/write page sets through one backend coherence operation, then detach the observer for the scalar pixel loop. Expressing the same mask as many ordinary range accesses reduces bytes but fragments dependency checks/flushes and does not improve throughput. On the33M route the retained bulk-mask implementation handles87456 CPU raster scopes, only3889 of which require dirty-page synchronization, and reduces download volume from roughly2.72GB to1.85GB while preserving exact retained relaxed frame/VRAM/EE/VU/IOP/GS state.

Qualified runs on the same executable measure12.0725075 and12.1955681FPS versus the previous clean11.74..11.98FPS range. Keep the conservative TEX0/output page-footprint fallbacks: unsupported/uncertain state must use the old full scope rather than guess. The win comes from one dependency/download decision per CPU raster scope, not from blindly minimizing bytes or coalescing synchronous readbacks.

### HG-LEARN-044 - Native float STQ is a safe small renderer-only win under the relaxed contract

After freezing strict state under `v1/`, the active renderer may approximate renderer-private GS arithmetic while preserving visible/game-observable behavior. On the existing GPU triangle path, upload float approximations of the already-validated S/T/Q coefficients and use float weighted sums/division/floor only for the game host's relaxed mode; keep exact integer coverage, depth, color, texture addressing, ordering and coherence, and keep strict factory mode as the differential/reference path. This activates on302650 triangles in the33M route.

Strict controls measure11.36245,11.40322 and11.33980FPS; relaxed samples measure11.46176,11.49945 and11.43419FPS. EE/VU/normalized-IOP and GS register/draw state remain identical. Renderer-private GS VRAM changes, but the final640x448 frame differs at only65/286720 pixels (0.02267%), maximum channel delta2/255, no delta>=4, PSNR87.56dB. Do not generalize this permission to guest-visible readback/order or CPU/EE/VU math. The gain is small; use it as a base for larger native GPU arithmetic/render-target changes rather than further exact-STQ micro-tuning.

### HG-LEARN-043 - Keep a user controls reference outside guest rendering and test input aliases

The user-requested F1 panel is independently drawn host UI in controls_panel.hpp, using only the existing presentation shader/VAO/texture path. Build/upload its fixed bitmap lazily once, then render only inside the retained fresh/refresh/resize boundary; closing help requests one refresh and must not restore unconditional swaps. Do not alter guest framebuffers or auto-pause clocks to show help. While help is open, release live input and explicitly label that the original game keeps running. Edge-latch Escape so dismissing the panel cannot also quit on the next poll while the key remains held. Keep Backspace and add Tab as an OR-alias for Select; never replace the old mapping unintentionally.

Runtime33980605f16646659c29df526c7500a6 passes262144 complete input/focus/alias comparisons, hotkey and held-Escape checks, bitmap bounds and the existing regressions. Synthetic740x490 preview SHA28bd69d434f39397c7d06304667fad6db4e6d905e6ce3c1a6ed2205862e31e82 shows all rows without clipping; this is NOT a capture of the panel inside a live game. Translation/readiness tests also pass. Full game-state/throughput and physical F1/Select/pause interaction are separate checks in PROGRESS. Authorized host UI changes replace the historical1a122b9f... hash with9704ed0bbf59c9ede823a03821f224aa7aa42512978dc22d153f0439de9b6d0f while preserving its redraw fix. Exact pre-panel backup78fec5d1ba344f79892d0a2cda4f5510; protect both the fix and new controls during later unrelated rollbacks. No audio fix is implied.

### HG-LEARN-042 - Separate an explicit coverage stop and muted launch from performance/audio regressions

Bounded-family follow-up20260922: repeated staircase stops justified checking the ORIGINAL object installer rather than another isolated getter. Latest2ac570/caller209624 uses the same live46dfc0 receiver. Original20a6f8..704 installs46dfc0;20a708..714 begins distinct46e000, with saved following receiver words and original/live64-byte table agreement. This matches the existing documented46e4c0-family technique. After independently decoding every missing body, add the eight absent initialized methods only within this bounded family; retain six existing entries and all older coverage. Exact50 additional words in shards0010/0015, matching original/live bytes and fixed verifier5d147ab1..., support static coverage, not dynamic execution of every method or a successful whole stair route. Do not generalize from one observed pointer to an unbounded table, adjacent object families or256 BSS selectors. Getter callsites2095dc/209600/209624 were independently decoded; two default methods are original zero-return code, not substitutes for unsupported behavior. Current proof/backups are in SOURCES and stairs-object-46dfc0-preedit-20260922-g1.txt. Required next evidence remains the user's actual staircase traversal; original33M checks alone cannot provide it.

Staircase slot+30 follow-up20260922: latest444f9702... stops at2ac540/ra2091a0, not previously repaired2ac530/2ac5d0. Original/live receiver and vtable plus caller209198 prove the getter; paired2091c4 in the same original function consumes the same slot, so root both callsites without claiming both were observed. Original2ac540 is only LUI/JR/delay ADDIU returning3f92e0; emit those three instructions, not a host shortcut. No nested function chain exists inside this getter, and nearby getters must not be assumed live merely because they share a table. Config81772f41... and generation57806f8b... add exactly3 words in one shard; proof/backups in SOURCES and stairs-2ac540-preedit-20260922-f1.txt. Exposed controlled pulses support only left/cross/start: they cannot replay this moving staircase route, and standard33M completion cannot be described as route success. Do not alter pinned recordings to hide this verification gap.

Second staircase stop20260922: same scene/object is not proof of regression. Old2ac530 was slot+0c; new fault2ac5d0 is original2032bc slot+28 on the same vptr46dfc0. Its original40-byte dispatcher selects BSS01990900+12*(initial a1&255). Captured a1=0 and descriptor{0,-1,2ac600} prove one next target; do not generalize to every byte index or root adjacent descriptors. Original/live dispatcher and104-byte callback match; next virtual target2c91d0 is already compiled. Add both required roots, preserve all old roots/host controls, regenerate and qualify. Exactly36 new original words and verifiera2251c6a... preserve expected state; actual staircase traversal still needs live evidence. See SOURCES/PROGRESS and stairs-2ac5d0-preedit-20260922-e1.txt. No audio/performance fix implied.

Menu-close extension20260922: opening-menu coverage at object+1708 did not cover its outer close-state callback at+16fc. Original caller3999d0 and live01810d7c descriptor prove3977d0; follow precise original stores397924/28/2c from44b1d0 to obtain3974a0, then397714/18/20 from44b1e0 reaches already compiled398850. Full original/live724/812-byte bodies and32-byte descriptor pair agree. Add only the two missing roots, not neighboring functions. This prevents assuming every menu dispatch shares one callback field. Configb8d3101f.../one-shard generation35b82014... and qualified verifierfa260aa4... preserve expected state; actual later open/close success still requires a live test. Same permitted inspection retry succeeded after the user asked to retry a prior platform block; no alternate route was used. Source proof, backups and latest10.297336FPS are in SOURCES/PROGRESS/PERFORMANCE; no audio fix or dependable speed gain is inferred.

Select-menu extension (2026-09-22): manual93c3705f... proves396900 via object180f680+1708 descriptor and397f5c/helper100b6c. Follow the ORIGINAL reached routine's precise descriptor loads/stores into that SAME consumed field:396e3c/40/48,396ed0/d4/d8 and396fdc/e0/e8 establish three successor roots394e40/395630/394670 from original/live44b200/210/220. Original100bd0 checks descriptor nonzero; all pass. This is stronger than rooting adjacent functions or every apparent pointer run, and reduces repeated single-callback stops without weakening faults. Full initial1996-byte body/original descriptors agree with saved RAM. New roots build and standard33M qualification53369591... preserves expected state; actual Select operation/successor traversal remains a live-test requirement. Details and exact hashes/backups: SOURCES and select-396900-preedit.txt. Preserve current F1/Tab host9704ed0b..., not the historical pre-panel hash.

Audio distinction: manual414 queue underruns precede the Select coverage stop; configured33M audio replay929ee941... separately records73 underruns and1579627 raw48kHz frames. The speaker path still consumes raw AutoDMA input, not completed synthesis/effects/cross-core mixing (DIAG013/014 and iop.hpp1222). Queue starvation supports the dropout diagnosis, while raw-WAV activity does not prove source/mix fidelity. Do not promise that speakers=on, a larger queue or achieving30FPS alone completes audio. No audio code was changed in this repair.

Manual sessionc1fa9cfc... appeared frozen but native.log gave EE2ac530/no static translation with saved memory. Read the explicit fault and saved call target before adding timing probes. Original/live vtable46dfc0 slot+0c and caller209574 independently prove the three-instruction target; root only that verified target, regenerate, confirm linked page_2ac and fresh manifest, and preserve unsupported-address faults. Original/live16-byte code slices match. Rebuilt original33M regression qualifies all expected hashes and864 writers (50aa2939...), but it does not cross the later79M manual staircase stop: retest the actual route before declaring it fixed. Preserve newly verified coverage on BOTH sides of later optimization reversals. See SOURCES/PROGRESS and manual-freeze-2ac530-preedit.txt.

That launch explicitly used audio=off, explaining missing speaker output without evidence of an audio-engine regression. Use existing audio=speakers for manual sound tests; enabling output alone is not proof of complete mixing or sync. PrintWindow was all-white while saved framebuffer showed the staircase, so use saved pixels for diagnostic inspection and do not claim the actual window was blank.

Later manual unpause sessionecd7f4c9... independently reaches a different direct member target2f55c0, not the already-compiled pause callbacks2f5c80/2f59a0. The same caller2f6094/helper100b6c and live {0,-1,target} descriptorfc70f0 prove the new root; full992-byte original/live code comparison agrees. Add this observed target and regenerate rather than guessing neighboring routines or treating pause success as unpause coverage. Its built33M regression preserves expected state, but actual unpause success still needs a live retest. Speaker-enabled sessions4594260a... andecd7f4c9... record151/426 queue underruns, respectively: sound is genuinely still faulty, separate from the earlier muted launch and coverage faults. Default L1Q/L2top1/R1E/R2top3 already exist; preserve the protected host when the user merely needs those bindings identified.

### HG-LEARN-041 - Sample native instruction pointers without per-guest-operation timers

A bounded own-worker Windows-x64 sampler (HG-DIAG-037) took330 contexts/290 native samples during31M..33M, with zero suspend/context errors or unexpected suspend counts. Duplicate only the current native worker handle; preallocate sample/context storage and never acquire locks, allocate, symbolize, read guest state or print while it is suspended. Always pair suspension with immediate resume, join on exits, and resolve main-image RVAs against the exact copied map after the window. Microsoft GetThreadContext/SuspendThread/ResumeThread/DuplicateHandle contracts are recorded in DIAGNOSTICS.330 samples over5.06921s shows nominal1..4ms Sleep is not a precise high-frequency sampler; do not change host timer settings just to claim a requested rate.

Observed work is dispersed: fpu_madd4_avx2(15),multiply_vector(12),VIF process_pending(11),convert_fixed(10),several generated VU bodies and GS helpers.40 outside-image samples are explicitly unattributed, not proof of GPU waits or allocation. Nearest-symbol/self samples do not provide inclusive stacks or exact per-function costs. Guest frame/EE/GS/VU match; pinned complete IOP comparison through temporary offline test shows only bfcd1/bfcd2 differences and identical existing RTC-normalized digest. Probe timing is not FPS. Replay20260922T043923Z-777a3f5a342d4ff2b8b420755c5e06d3 and129-test jobdf58fe42022548ffbeedc6829ce2f61d retain evidence. Runner and temporary test restored exactly, clean game rebuilt. Reuse results before another instrumentation pass; a sampled small helper is not automatically the best optimization target.

### HG-LEARN-040 - Count actual host swaps, then eliminate only redundant presentation work

The old UI called clear/draw/swap without a fresh mailbox frame: a count-only probe measured6235 duplicates among7097 actual swaps. Event/refresh-aware drawing preserves every fresh mailbox image (even identical pixels), initial paint, resize/refresh requests, input polling and all guest GS/EE/VU work, while idle iterations use a bounded event wait. Four interleaved qualified observations reversed4.08226 ->11.32616 ->4.05805 ->11.37003FPS with matching input/frame/EE/GS/VU/normalized-IOP hashes and864 original writer events. This is retained and must not be lost when restoring unrelated renderer trials. A later controlled input smoke moved original no-card NO->YES and Cross advanced to CAPCOM, with delivered press/release logs; PrintWindow yielded all-white despite valid saved game pixels, so physical-screen and resize behavior remain unverified by that capture method. Evidence PROGRESS top retained section; verifiers6307f56d...,c6a43b6f...,7f64ea2a...,f7e7e60d... and visible20260921T203204Z-0baaca1564d54a90a3572e41a034b953. glGetError was where contention surfaced, not necessarily its cause. The unchanged archived-binary test ruled out recent recompilation as necessary, not an interaction involving the unchanged old host loop. Exact driver-internal cause of the earlier shift remains unknown.

### HG-LEARN-038 - Tie qualified measurements to post-link input and executable hashes

A restored source tree or successful test build does not imply hg_game.exe is current. A real mismatch left the HG-DIAG-027 executable active after probe removal and a later VU header edit. The hg_game post-link manifest now hashes first-party native/generated/config/generator inputs plus build configuration and executable; the fixed verifier rejects missing/stale/wrong binaries before native launch and pins the checked manifest. Twelve synthetic cases plus real missing-manifest and modified-vif.hpp rejections pass (128 Python tests total). Both fresh candidate and control then passed manifest checks. Inspect native function bytes for the intended change too; neither changed whole-file SHA nor equal executable size proves intended code or identical layout. This guard relies on configured build dependencies and does not independently prove correct compilation or fresh generation. Evidence: HG-DIAG-028, tools/hg_build_provenance.py, verifiers a2f951df1cf246a7bd894393d47f9f25 and3b66612128714d64aa2aac308679a940, current candidate/control e11b510d1a094cbf8e911e887bdf7c15/0321d8e9233f4e2a921d3812a72c8bb3.

### HG-LEARN-039 - Replay an unchanged archived executable before blaming a source edit for timing drift

Pinned archived executable SHA2608296b... is byte-identical in old and new captures; input-events, final frame, full EE RAM and GS VRAM match, and the same RTX5070/NVIDIA616.92 string and draw counts are reported. Yet full-run flush0.9963266s becomes15.1869414s and readback1.5088076s becomes3.5050519s. This shows the current latency shift can occur without recompilation or any new code. Runtime/driver/host conditions remain to be isolated; it does not identify a specific driver bug, clock limit or competing process. A narrow GL-only probe attributes15.115755s to glGetError, but replacing polling with synchronous notifications fails to remove the cost: API wait location is not necessarily the underlying cause. Do not reject useful compiler/algorithm ideas universally from a drifting host baseline. Evidence old project-link-runtime/20260921T023355Z-e962461879604a5e9776d9d8db125a5b and new20260921T195021Z-5c12f01e72cc4234bae8c012c45afcdd, HG-DIAG-029.


### HG-LEARN-001 — Isolate CPU cost without changing guest work

Use a current executable/linker map, bounded host samples and phase profiles to
locate hot paths. Compare alternating deterministic original-program replays after
the change. Keep input, clocks, diagnostics and competing load comparable; check
EE/IOP/GS/VU state and images. Ignore only independently explained differences
(IOP RTC seconds/minutes, and hour when the run crosses an hour boundary), never arbitrary mismatches. Exact STQ division
reduced the measured scene time 4.70% while preserving compared state/output.
This proves equivalence for that scene, not whole-console fidelity.
Evidence: `docs/PERFORMANCE.md`, external `divide-times.json` and
`divide-equivalence.json`.

### HG-LEARN-002 — Factor exact address arithmetic

Small compile-time tables can factor the independently derived GS X/Y address
formula without caching mutable VRAM. Exhaustively compare coordinate/stride
coverage and replay the scene. CT32 factoring improved the measured nearest-filter
scene by 6.44%; do not extrapolate that number to other filters/workloads.
Evidence: `docs/PERFORMANCE.md` (scene address audit).

### HG-LEARN-003 — Trace corrupt geometry before changing the rasterizer

Capture bounded before/after original AOT VU execution and independently calculate
the expected projection. VF/VI dependencies also advance pending Q/P operations;
omitting those stalls produced Fiona's long triangles. Correct readiness tracking
matched the matrix calculation and removed the spikes. No runtime decoding.
Evidence: `docs/SOURCES.md` (VU character projection), `docs/PROGRESS.md`.

### HG-LEARN-004 — Validate pixel math against register semantics

Derive packing, saturation, filtering and wrap behavior from hardware manuals;
use synthetic edge cases and permitted independent observations. Correct texture
product saturation removed Fiona's green artifacts without changing EE/VU state.
Bilinear UV tests matched 56 external oracle samples across four wrap modes.
Physical-console STQ precision and full rendering fidelity remain unproved.
Evidence: `docs/SOURCES.md`, `docs/ORACLE.md`, GIF tests.

### HG-LEARN-005 — Separate presentation from guest execution

Use committed scanout pixels in a bounded mailbox; keep GLFW/OpenGL ownership on
the UI thread and AOT execution on its worker. Never flush guest draws to produce
a preview. Controlled33M native/headless state and images matched (RTC excepted);
nonblack GL readback and cooperative close passed; user confirmed keyboard movement.
This is direct presentation, not a GPU GS renderer. On Windows select
`WinSta0\Default` for the visible launcher; a private-desktop HWND is not visibility.
Evidence: `docs/PROGRESS.md`, HG-DIAG-011/015.

### HG-LEARN-006 - Reuse validated pixel state across adjacent stages

When no guest state changes between depth testing and frame commit, reuse the
already-read destination and passed alpha/DATE results. Share the final blend/
write helper with the standalone frame path; retain validation, masks, AFAIL,
SCANMSK, depth-test order and frame/depth alias behavior. Four controlled33M runs
kept compared EE/GS/VU state and images identical (only verified IOP RTC fields
differed). Scene means47.56255s ->41.97535s,11.75% less elapsed; individual pairs
13.54% and9.80%. Relevant GIF tests pass. This is exact factoring of the existing
pixel pipeline, not independent proof that the entire renderer is console-perfect.
Evidence: `runtime/gs.cpp`, `docs/PERFORMANCE.md`, external commit-times.json,
commit-equivalence.json and commit-iop-equivalence.json.

### HG-LEARN-007 - Specialize measured draw classes, retaining live memory order

After per-primitive timing identified sprites as the largest GS cost, a checked
CT32 sprite path prepared address components once per column/row. Reuse decoded
draw state; keep live texel reads and row-major writes, all applicable blending/
mask/alpha/clamp behavior, and the checked general path outside the predicate.
72 scalar-reference cases exercise wrap/filter/blend/masks and overlapping texture/
frame feedback. Four33M replays match images and EE/GS/VU state, with27.04% lower
mean scene time. Scope is this supported sprite class, not all GS behavior.
Moving raster bodies into gs.cpp also avoids rebuilding translated shards for
renderer-only edits. Evidence: PERFORMANCE.md and sprite-equivalence.json.

### HG-LEARN-008 - Correct estimates back to exact integer results

A floating reciprocal can seed a quotient when integer product bounds are proved;
correct the estimate using integer comparisons before returning. Keep the checked
wide path for values outside the proof. Preparing bounded STQ coefficients once
per triangle and sharing Q reduced scene time7.81%; 300000 integer checks, six
triangle/fault cases and four33M state/image comparisons passed (RTC excepted).
This is exact arithmetic factoring, not a license for approximate guest math.

### HG-LEARN-009 - Amortize GPU transfers at existing ordering boundaries

A per-draw synchronous sprite experiment was8.96% slower in one matched native
comparison. Grouping ordered compute dispatches inside existing pending-draw
flushes and including medium strips reduced the same scene27.59%. Read back before
CPU fallback, guest return or fault; use shader-storage barriers between dispatches
and a buffer-update barrier before CPU reads. Reject per-draw feedback alias.
232 GPU/CPU cases cover wrap, blending, feedback, mixed CPU draws and faults;
original33M images/EE/GS/VU match (RTC excepted). Broader console fidelity remains
unproved. Do not assume GPU execution alone eliminates transfer/dispatch cost.
Evidence: PERFORMANCE.md, gpu-sprite-batch-equivalence.json, gl_gs.cpp.

### HG-LEARN-010 - Hoist immutable texture setup per draw

Unlike the inconclusive fragment-local setup experiment, specializing once per
triangle for CT32 and CT32-palette PSMT8/PSMT8H removed repeated validation and
format decoding. Keep live memory reads and validate only accessed palette entries;
retain checked fallback.864 scalar comparisons and four original-scene replays
preserve state/images (RTC excepted), with18.71% less mean scene time. This is
workload-scoped, not full console parity. See PERFORMANCE.md and gpu-texture-times.json.

### HG-LEARN-011 - Batch GPU work across safe ownership boundaries

Triangle compute initially regressed the original scene to71.4774s with49814flushes
and208.935GB uploads. Cull empty coverage, retain exact bounded coefficients and
defer resident submission until checked consumers require visibility. Revised
profile has1683flushes,1.110GB uploads; matched pairs reduce scene mean22.45325
to17.62155s (21.52%).345 GPU/CPU cases and original state/image comparisons pass
(RTC excepted). Host ownership coherence does not prove complete console cache or
timing fidelity. Preserve faults and CPU fallback; fewer bytes alone is not success.

### HG-LEARN-012 - Expand GPU coverage only with exact word ownership

The remaining sprite cost justified revisiting HG-FAIL-006 after resident batching.
Port existing integer texture/frame stages; for16-bit frames assign both lanes of
each word to one invocation to avoid read/modify/write races. Keep conservative
feedback/depth aliases on CPU and retain palette faults.853 GPU/CPU cases and five
original replays preserve compared state/images (RTC excepted). Matched scene mean
18.65525 ->14.22910s (23.73% reduction), despite more transfer/readback cost.
This is bounded CPU-reference equivalence, not full console fidelity. See SOURCES
and PERFORMANCE; sprite-format-pair-times/equivalence artifacts remain external.

### HG-LEARN-013 - Keep AOT helper specialization bounded

For this scene, small VU register/readiness/flag helpers and integer multiplication
benefit from inlining; compound arithmetic forced into the giant CFG does not build
economically (HG-FAIL-007). Exact MAC bit placement replaces its inner branch loop.
Eight focused tests and six original state/image comparisons pass, RTC excepted.
Scene mean13.36263 ->12.97297s (-2.92%), one neutral pair; executable grows4.69%.
Retain this bounded version, but expect workload/compiler variation. This establishes
unchanged checked arithmetic, not previously unverified console precision or timing.

### Reference-review practice

Compare architecture at the measured bottleneck. PCSX2 documents transfer/ordering
costs; OpenGOAL documents a game-specific port with offline preparation; PS2Recomp
documents literal AOT translation and runtime services. These are leads, not proof
that a borrowed replacement preserves HG behavior. Keep independently derived
implementation and state/performance evidence. See SOURCES.md for exact documents.

## Failed skills / approaches to avoid

### HG-FAIL-045 - Current helper-only MSVC PGO is exact but slower; broader instrumented surfaces are unstable

HG-DIAG-085 tested compiler profile guidance after renderer and local VU micro-optimizations failed their gates. Generated-main NOEXACT instrumentation reached12M then access-violated before13M; combined generated-main+helper instrumentation failed even earlier. Helper-only `hg_vu_products` instrumentation was stable and produced one exact-link merged PGD from the fixed33M route. The resulting USE executable passed all synthetic/runtime/translation/readiness checks, original replay and exact864-writer/state qualification twice, but measured13.35428/13.28339FPS versus adjacent ordinary OFF13.49843FPS; every candidate heavy subwindow was slower. PGO support was fully removed and final clean OFF requalified13.79314FPS. Do not repeat the same helper-only `/GL` + NOEXACT/USEPROFILE surface or broaden back to the already unstable generated-main surface. This is a workload/toolchain result, not a universal claim that profile-guided optimization cannot help other code. Use measured VU structural work instead.

### HG-FAIL-044 - Coarse drain launches do not eliminate fine-grained correctness joins

HG-DIAG-064 measured only60 nonempty VIF drains in the heavy window, containing14,457,736 pump iterations and635,882 draws. A persistent VIF/VU producer with main-thread GS/GL consumer therefore avoided per-activation launches. Conservative packet validation allowed95,409 packets to queue asynchronously while56,877 event/transfer/CLUT/fault-capable packets still required producer waits. The implementation preserved all retained captures and864 original frame boundaries, passed3072 classifier cases and80 ordered DMA/VU/event/partial-fault comparisons, yet qualified at10.68711FPS versus nearby clean13.16456/13.31100. It was removed, including hooks/counters/new headers; external archives are recorded in PROGRESS.

Count synchronization points inside a coarse task, not just task launches. In this case49,564 actual CLUT loads versus52,144 CLUT writes also rules out large savings from merely admitting no-load texture writes. Do not retry unchanged queues or drop correctness joins. A different architecture needs a proved mechanism for reducing those dependencies or separately validating fault-capable operations before overlap; this result does not prove all graphics threading is ineffective.

Final restored control7dd58858 /a03d48e7 measured13.49206FPS, effectively matching06313.49534 and confirming no material gain to retain.

HG-DIAG-063 independently tried preparing invariant transform signs/exponents/mantissas once per AOT loop. It was exact but13.49534FPS versus nearby controls and historical13.39..13.42 did not establish a repeatable material win. Archive it as unproven for this workload, not universally slower.

### HG-FAIL-043 - Renderer operation counts alone did not predict an overall speed gain

On the current retained alpha/AFAIL + PSMZ24 + CT32-threshold renderer, exact CT32 shader specialization, page-independent multi-sprite dispatch, and deferring pure STQ preparation until a triangle has a covered sample all preserved fixed captures but stayed around12.91..12.92FPS versus clean12.97. Multi-sprite dispatch genuinely reduced52398 dispatches to22390 and passed1177 GPU/CPU cases in each mode, yet flush/readback changes were tiny and total throughput flat. These are workload-specific non-wins, not invalid algorithms; do not repeat unchanged or retain complexity based only on counts. Candidate backups/results are in PROGRESS. Eight queued-sprite dependency tests remain useful independently.

Compact overlapping VU SQ operand capture is separately INCONCLUSIVE, not a proven regression:122880 compiled comparisons/89970 matched faults passed, stack0x8f8->0x208, candidate12.67/12.79 versus restored12.37/12.58 with earlier control13.55. Host drift prevents a reliable material-gain claim; retain the original emitter for now. Reuse the archived candidate and tests rather than reimplementing it if a better-controlled comparison becomes justified.

### HG-FAIL-042 - Shrinking/reusing GPU triangle palette payload reduced batches but not gameplay time

Under the relaxed native-renderer contract, the triangle SSBO was first changed to omit the256-word palette for untextured/CT32 jobs, then refined to reuse an identical indexed palette inside each batch. Both preserve the retained relaxed frame/GS-VRAM plus exact EE/VU/normalized-IOP/GS-register state. The cache is mechanically effective: across305856 GPU triangles it needs only1258 palette uploads, reuses301392 palettes, omits3206, and lowers triangle batches1014 to936. But qualified FPS does not improve repeatably: simple omission11.566555FPS; cache12.016655 then11.842425FPS versus recent clean native-STQ controls11.7379..11.9805 (representative11.8450). Do not keep extra host-side cache/packing complexity merely because upload volume/batch count falls; this workload remains dominated elsewhere. Restore pre-palette `gl_gs.cpp` eedd42e4... and target measured renderer coverage/state bottlenecks instead.

### HG-FAIL-041 - Fine-grained host-thread VIF1/VU1 overlap costs more than HG can overlap

The architecture premise is real: captured Haunting Ground VIF1 traffic uses BASE0/OFFSET0x200, TOPS-relative UNPACK and MSCAL/MSCNT, matching Sony's double-buffer producer/consumer mechanism. Multiple host implementations preserved exact frame/EE/GS/VU/current-date normalized IOP state, but all lost throughput versus synchronous production: full-snapshot overlap8.746445FPS, dirty VU/VIF tracking8.584612FPS, VIF-only tracking/bulk merge8.749468FPS; atomic spin/yield handoff was even worse in whole-run time. Retained diagnostics show only~0.18s completed UNPACK work available to overlap with~2.56s VU execution in the final2M region, while ~126180 MSCAL/MSCNT activations create frequent host scheduling boundaries. Do not retry fine-grained one-host-job-per-activation designs unchanged. Split-tag/TTE physical phase tracking discovered by this work is independently correct and remains. Clean synchronous production verifier98f8f5f6... is11.428331978FPS with exact state and becomes the post-audit baseline. Future work should target actual VU AOT/EE/GS cost or a much coarser architecture mechanism only if its overlap budget is independently proven.

### HG-FAIL-040 - Hoisting most VU pair clock updates into a function-level deferred scheduler regressed gameplay

The root-cause audit correctly identified runtime VU scoreboard traffic as substantial host work, but the tested fix was counterproductive. An overflow-safe scheduled-clock design moved3194/3477 static pair advances off direct architectural mutation while preserving Q/P timing, readiness, exceptions and uint64 wrap. It passed132 Python tests,65536 compiled full-state/memory readiness comparisons, runtime/translation, replay and all established frame/EE/GS/VU checks. Qualified candidate9.622339223FPS versus exact restored same-date control10.079036444FPS is~4.7% slower; both share post-date-rollover normalized IOPb29aec44.... Candidate executable grew~348KiB, so extra generated control/sync code and I-cache pressure are plausible contributors. Restore original per-pair route; do not repeat this deferred scheduler unchanged. The failure does NOT invalidate the audit's independent VIF1/VU1 double-buffer serialization finding; move to actual device overlap rather than more scoreboard bookkeeping.

### HG-FAIL-037 - Single-tag EOP IMAGE materialization bypass was within qualified noise

A narrow gs.cpp path recognized only complete packets consisting of one EOP IMAGE/IMAGE2 tag, then sent its qwords directly to the existing write_image_qword endpoint. Mixed/register/multi-tag GIF packets retained the exact decoder/application route; zero-loop IMAGE, capacity, pending bytes, partial GS effects and fault checkpoints matched the frozen reference in5825 runtime checkpoints. Candidate11.228385358FPS versus exact whole-file reversed control11.173891829FPS differs by only~0.49% host time, with overlapping heavy windows (candidate11.2396/11.2172; control11.2406/11.1080). All149-input/864-writer/frame/EE/GS/VU/RTC-normalized IOP checks pass. This does not show a dependable gameplay gain; production gs.cpp is restored exactly e03185c7.... Keep the strengthened test cases, not the fast path. Do not repeat unchanged by appealing to the older perturbed GIF subphase timing.

### HG-FAIL-036 - Compact Mask/Broadcast-only VU MUL specialization had no demonstrated gain

After full five-operand templating failed, a materially smaller helper kept only Mask/Broadcast static and destination/source/other dynamic. It passed786432 exact complete-state comparisons and427520 matching faults across16 MXCSR modes, linked only four mask14 variants, and did not enlarge the executable. Candidate10.608447507FPS versus fresh reversed original multiply_vector control10.715204160FPS is about1% slower; an immediately earlier original-route sample was11.145786893FPS, demonstrating larger host drift. Retain original multiply_vector gameplay route and the compact helper only as a test fixture. This result is workload-specific and does not invalidate all compile-time constants, but there is no basis to re-enable this route unchanged.

### HG-FAIL-035 - Full static-operand broadcast VU MUL specialization regressed the current qualified build

The AOT route templated Destination/Source/Other/Mask/Broadcast for broadcast MUL masks14/15 and used a noinline exact-order helper. Its104-case fixture passes851968 complete-state comparisons and466304 matching faults across16 MXCSR modes, but correctness tests alone did not establish speed. With identical current gameplay coverage/config and host, fresh candidate5fddf7c... measured9.449696901FPS (60/6.34941s); switching only generated routing back to existing multiply_vector and removing the helper include produced control3be4caa8... at11.145786893FPS (60/5.3832s), with near-identical11.1431/11.1484 heavy windows. Both pass149-input/864-writer and exact frame/EE/GS/VU/RTC-normalized IOP qualification. Reject full five-operand templating; keep the helper/fixture test-only. Candidate executable was~306688bytes larger, which may contribute but is not established as the sole cause. Do not repeat unchanged or infer that all compile-time specialization is bad: a compact Mask/Broadcast-only helper with dynamic register indices is materially different and may be tested separately.

### HG-FAIL-034 - More aggressive automatic VU-unit inlining regressed this qualified comparison

Adding only /Ob3 beside existing /O2 on the generated main/VU source built in106.35s and passed original replay plus147-input/864-writer/state qualification. Candidate6a179db53b724cbcab73d34af82e0b94 measured60/6.5205s=9.201748332FPS. Exact CMake reversal and rebuild restored60/5.39847s=11.114260151FPS (bff1d79d44d0454e870c25316cd62076), with unchanged image/EE/GS/VU/RTC-normalized IOP and preserved host1a122b9f.... Reject /Ob3 on this evidence; do not keep a slower build while describing only its passing tests. One pair shows a substantial reversible loss, not its compiler microarchitectural cause or a universal /Ob3 verdict. No forced-inline annotations or /O1 large EE shard settings changed. CMake restored2b9e0b6b..., current control game2dc87790.... Candidate CMake backupfa1333528bde47f7a2b369a33aa1841f; source/build plan vu-auto-inline-ob3-preedit.txt. Do not repeat unchanged. Selective main+VU /GL/LTCG was separately rejected already (HG-FAIL-017); that is not an untested follow-up.

### HG-FAIL-033 - Batching still-incomplete GIF payload bytes did not establish a dependable gain

The private outlined-VIF helper batches opaque payload bytes within an already-complete DIRECT command, leaving tags, completion/application, max_pending overflow and first-fault bytes on original submit_qword. This is distinct from whole-EOP coverage failure031 and DMA-arrival mechanism032.936 serial-reference cases/7873 checkpoints passed, the native linked helper/callsite was inspected, and the original recorded replay and all four147-input qualified runs preserve864 writer events plus exact frame/EE/GS/VU/same-date normalized IOP hashes. Candidate11.021045/10.911730FPS versus interleaved exact controls10.904967/10.861439 gives only1.053%/0.461% less host time, with downward sequence drift and the second pair's individual windows disagreeing. This is not strong evidence for retention, nor proof the host-only byte-copy equivalence is wrong or universally useless. Original parser5ab22f5b... rebuilt; protected host1a122b9f... and original runnerc8c2d536... remain. Useful fixture retained. Do not repeat unchanged or advertise comparison against older isolated10.476FPS as a gain.

Evidence verifier directories fa3283542166432f8058a0e932dcd847,f048f775d8754a55b7c49772e64335c2,4f708348e1c243b39043e861a449f87b,1b05c22d19ff484581c6d713951f95f5. Restored control game37f7d62d3dc9acfa7aeed8009f97c3853cadc287c16c1e2bacd35e51787f6cdc;60/5.52413s=10.861438815FPS. Candidate backups bffa0a620e0a4400be7203efcf2025c3 and ab4f8269661946a0bcf6ce570345470e. Next investigate sampled native hot code instead of assuming source-level qword-copy savings dominate the1.6s inclusive DIRECT measurement.

### HG-FAIL-032 - Inert DIRECT DMA arrival batching had no stable end-to-end gain

A guarded helper concatenated only known-incomplete DIRECT/DIRECTHL RAM/scratch input inside the existing synchronous pump loop, leaving both command and DMA completion to the original final qword.2338 state/fault cases including256 caller-command guard cases passed; original gameplay image/EE/GS/VU/same-date IOP hashes and864 writer events matched. Qualified unfiltered candidates10.943853/11.237597FPS versus original controls10.273726/11.125079 had one slower first control and only~1% advantage in the second pair. A DIRECT-only caller prefilter also measured10.956803FPS, not a demonstrated advantage. Keep production runner original; preserve the helper only in runtime_tests, not the game library.

HG-DIAG-036 confirmed real coverage:25986 accepted batches buffered7151582qwords among7306154 helper attempts in heavy gameplay. This is distinct from HG-FAIL-031's low-coverage complete-EOP batching; large operation-count savings still did not establish a reliable qualified gain, and the earlier6% pair must not be advertised. Future work should attack actual completed DIRECT/GIF application rather than repeat the same incomplete-input helper. Evidence7c07aef8...,ccd2a496...,e9e0f1d0...,7eae15ea...,ee470df3566642e6ac6ea86ca68a2130 and project-link-runtime/20260922T024554Z-c6cc6abda3b043de9a8e92cd6fb3153f. No semantic error was observed within the tested scope; this does not claim the mechanism can never help another workload.

### HG-FAIL-031 - Complete DIRECT packet batching covered little of the actual payload

The candidate kept the original materializing decoder and complete-EOP commit/fault semantics;791 cases/5124 exact checkpoints and original frame/EE/GS/VU/same-date IOP hashes passed. Qualified enabled10.983700/10.638468FPS did not improve over the exact original parser11.020640FPS (prior11.038888). A flag-disabled outlined helper gave drifting7.564354/8.917942FPS and must not be treated as proof of improvement versus the original pre-helper implementation. Restore exact pre-experiment code when control code placement differs.

Counter-only HG-DIAG-035 on the ORIGINAL serial path showed57906 eligible packets but only564374 of7306814 heavy DIRECT qwords (7.724%) fit the complete-EOP guard; every eligible packet began with PACKED format, none IMAGE.3240 commands began with a prior GIF suffix. Thus high packet count did not mean coverage of the large payload; reject/restore this candidate, retain fixtures, and do not repeat unchanged expecting the1.6s inclusive DIRECT cost to disappear. Evidence candidate80d93dc0...,7f89e3b7..., exact original1945b31d1f4d440393e22906ae0a9d37 and coverage project-link-runtime/20260922T022141Z-10b6ddd9ec4a48c7b6544089db0ae5bd. Probe sources restored exactly; host1a122b9f... protected.

### HG-FAIL-030 - Whole-span VIF word bounds check showed no useful qualified gain

The overflow-safe4-byte span guard preserved535050 value/exception comparisons, generated game state and the original invalid-range .at path. Candidate88bac4c386484cf2b68bb33f16da4e7b measured10.92770775FPS (60/5.49063s); contemporaneous restored control4b1d9bef5834489fb798137145ba3893 measured10.94219603FPS (60/5.48336s). Both143-input manifests,864 frame-writer events and all established frame/EE/GS/VU plus same-date normalized IOP bdbf3a11... hashes match. An earlier unchanged candidate was10.33145014FPS, illustrating host variance. No repeatable gain is established; the shortcut is byte-restored and tests retained. Do not repeat this word-helper guard unchanged or treat fewer source-level bounds checks as proof of faster gameplay. Protected event/refresh host1a122b9f... remains unchanged. This does not rule out distinct loop-level UNPACK indexing changes.


### HG-FAIL-029 - Guarded embedded-rounding MADD did not demonstrate a gameplay gain

2026-10-25 update: the embedded-rounding path is now retired for correctness too: host IEEE adds cannot reproduce the console's one-guard-bit adder (real-PS2 captures, SOURCES.md), so `fpu_madd4_er_available()` returns false.

The isolated guarded helper passed2113152 direct vectors/8452608 scalar lanes,16 MXCSR modes, boundary/exponent/signed-zero checks, exact flags and rejection atomicity. Original gameplay with the route enabled passes all established state/image hashes and864 frame-writer events, but candidate11.388375885FPS (60/5.26853s) versus subsequent disabled-route control11.401186864FPS (60/5.26261s) is essentially flat (~0.11%). This is an inconclusive performance result, not proof the arithmetic is wrong or universally slower. Keep use_embedded_rounding_madd=false; preserve helper/direct fixtures for reproducibility and the original integer/AVX2 route in gameplay. Do not re-enable unchanged on the assumption that ISA-level simplification implies end-to-end improvement. Both runs used the retained event/refresh host and143-input freshness. Evidence project-link-verifier/9e7d985c63b6448c8312d3d912fa8039 and47eac334b33a4a2b9f0a5b3acd8fce84; current exe78a0c91d.... Host fix1a122b9f... remains protected and11.4FPS qualified. This is not the rejected prepared-matrix path or a loss of the host recovery.


### HG-FAIL-026 - Two small VU shortcuts had no demonstrated qualified gain

Validated MicroMem-generation caching passed state/regressions but cache-on9.3733/9.3863FPS versus controls9.5073/9.3338FPS overlapped variance; equal executable sizes were not identical machine-code layouts. The later no-Q/no-P advance_pipeline early-return passed65536 compiled readiness cases and exact original state, but fresh candidate4.050141FPS versus restored4.040570FPS is effectively flat while both have the large GL stalls. Both changes are removed. Their correctness or usefulness in other workloads is not disproved; do not repeat either unchanged expecting to explain the current GPU latency. See PROGRESS and verifiers9394004a...,48240a60...,9798d9a8...,37fc1136...,e11b510d... and0321d8e9....

### HG-FAIL-027 - Independent worker GL context did not reduce present driver stalls

Only the worker context share pointer was changed to nullptr after proving the CPU pixel mailbox is the only cross-thread image transfer. All GL error checks/commands remained. Replay20260921T193643Z-94beeb0eb8514953b68accb9143a01cf still spent15.1996s flushing and3.6567s reading back, essentially unchanged. No qualified improvement was established; game_host.cpp restored exactly. Do not repeat unsharing alone unchanged for this observed condition.

### HG-FAIL-028 - Guaranteed synchronous GL error notifications moved cost rather than improving throughput

A requested DEBUG worker context, synchronous KHR_debug API-error callback and actual-driver INVALID_ENUM self-test preserved fault detection; ordinary contexts retained polling. Portable513-length/first-error tests and original state hashes passed. Nonetheless replay20260921T194610Z-a12e18b8014d4cee870168eaafd6daa1 had18.3585s flush/2.7917s readback, and qualified verifier a8086a1ce14a41ebae73903e79a5fa87 measured3.595859FPS versus4.040570 control. Backend/host/tests restored; helper implementation archived outside project, empty unreferenced placeholder remains because connector deletion is unavailable. Do not disable errors to make glGetError timing vanish. Ordinary non-debug contexts may legally emit zero notifications and cannot safely replace polling; synchronous debug mode itself can reduce driver performance. Primary specification KHR_debug sections2.5,5.5 and5.5.7; see HG-DIAG-030.


### HG-FAIL-001 — Treating preview rate as game FPS

File changes, generated snapshots and repeated GL swaps count different things.
None proves how many original game frames executed. Never claim console-speed
parity from those counters. Establish an original frame boundary and host elapsed
time; retain explicitly labeled video-update measurements until then.

### HG-FAIL-002 — Comparing uncontrolled input as deterministic state

Wall-clock button pulses and a live-input-enabled window can diverge with host
timing. A native-host test diverged after31M; the controlled-input repeat matched.
Use identical guest-slice input events and disable live controls during equivalence
tests. This does not imply live controls are incorrect.

### HG-FAIL-003 — Retaining an optimization merely because it sounds faster

The earlier preview cache avoided394 writes but increased elapsed time
(35.3766s vs34.9217s); it was removed. Sparse IDCT26, bit reader34, byte batching36,
combined trace/inlining40 and address41 also lacked connected speed benefit.
These are workload-specific failed experiments, not universal bans on caching,
inlining or lookup tables. Revisit only with a new measured bottleneck and a
different, explicit hypothesis. Evidence: `docs/DIAGNOSTICS.md` and performance log.

### HG-FAIL-004 — Trading fidelity for apparent throughput

Skipping required draws, forcing initialization calls, changing guest clocks to
inflate FPS, silent unsupported operations, and interpreter/JIT fallbacks cannot
satisfy this project's 1:1 independent AOT goal. Diagnose the original path and
optimize host work instead. This is a project constraint, not a benchmark result.

### HG-FAIL-005 - Fragment-local texture setup factoring did not speed this scene

Candidate A shared TEX0/TEXA decoding and per-axis wrapping across bilinear
neighbors. Four controlled33M replays preserved all compared images/EE/GS/VU state,
but scene times before42.9333/44.1434s and after42.7344/44.1626s differ by only
0.21% on average, below the observed variation. Reverted this candidate; retained
its external source/binary for reference. This rejects that implementation as a
useful scene optimization, not the general idea of hoisting texture setup.
Revisit only with a different scope (e.g. per draw) and evidence of saved cost.
Evidence: external setup-times.json and setup-equivalence.json.

### HG-FAIL-006 - More GPU coverage can lose its benefit to extra flushes

Adding solid CT32 fills offloaded116.5M more pixels but raised flush count721 to
1227 and host flush time~0.89 to1.49s. Four matched native runs preserved state,
but scene mean improved only2.09%, with one pair slower and one faster. Reverted
that candidate rather than retain an unproven optimization. This is inconclusive
for solid fills generally; revisit with less synchronization or new cost evidence.
External gpu-solid-times.json and solid-*-equivalence.json retain the measurements.

### HG-FAIL-007 - Do not force-inline compound VU arithmetic into the giant CFG

Forcing Fpu::add plus multiply_acc/madd_vector/multiply_vector into the generated
VU unit passed focused tests but drove MSVC to36.75GB private/14.12GB resident
memory during code generation; the build was stopped. No native speed result exists.
Preserved candidate/log in external vu-inline-aggressive. Narrow to small access,
readiness, multiplication and flag helpers, then measure. This rejects that combined
aggressive configuration, not all inlining or the exact arithmetic it implements.

## Open questions (not learned facts)

- User corrected the map-entry text report: no text should appear there. Do not
  infer missing initialization calls from that withdrawn report.
- Current scene speed is inadequate. CPU raster/texture/address work is prominent
  in host samples; quantify sustained phase costs before asserting a sole cause.
- Experimental issue-slot timing and synthetic TOC input remain fidelity gaps.

## Entry template

ID / technique; when applicable; observed outcome; correctness source and limits;
performance evidence; files/artifacts; retention or retry condition.

### Pending investigation — CPU cost of GPU coherence

A page-ownership wrapper passed241 GPU/CPU cases and original-scene state/image
comparisons, but slowed the scene24.7895 ->29.5581s even with residency off.
Resident mode took30.2274s. Do not count reduced upload bytes as a speed win or
accept this wrapper as performance-neutral. Profile the disabled observer path and
verify generated access code before expansion. This is not yet a generally failed
technique: the cause remains unproved. See docs/PERFORMANCE.md.

Disjoint CPU-write follow-up: test dependencies against both queued read and write
pages before submitting GPU work. Unrelated writes commute, but writes to queued
texture inputs must still submit first.856 cases and four original replays preserve
state/images; flushes3007 ->2415, scene mean1.66% less. Synchronous tests must end
the explicit service batch before CPU mutation; resident tests must exercise queued
work surviving that boundary. See HG-DIAG-017 and disjoint-flush-pair-times.json.

### HG-LEARN-014 — Prove disjoint bits inside aliased VRAM words

Page overlap alone can reject safe in-place work. For bounded CT16/16S sprites,
prove each pixel reads the opposite16-bit lane of its own unique destination
word, with zero filter fractions and distinct destination columns. Separable
row/column deltas establish word equality including wrap; <=8 columns bounds
ownership. All sampled bits remain unchanged during the draw. Keep other hazards
and fall back when any proof fails.896 GPU/CPU checks and four original replays
passed, scene time11.10% lower; no external rendering algorithm copied.
Scope: these proven sprites only, not arbitrary feedback or console cache fidelity.
Evidence: docs/PERFORMANCE.md; external lane-copy-baseline and lane-* artifacts.

### HG-LEARN-015 — Suppress conservatively dirtied uploads by exact bytes

A CPU scope may mark untouched pages dirty. Compare only against a known GPU
copy, invalidate after GPU writes and memory-owner changes, refresh on transfers.
Full memcmp, not hash equality, preserves correctness.896 GPU cases both modes
and four original replays agree; uploads2.950GB ->0.678GB but scene only1.93%
faster. Retain modest benefit; bytes saved do not equal elapsed savings. Extra
4MiB host storage and compare/copy work included. See PERFORMANCE.md.


### HG-LEARN-016 — Synchronize committed scanout before its pixel loop

A const bulk VRAM read removes per-pixel observer overhead without flushing
uncommitted guest draws.901 GPU/CPU cases both modes and four original replays
match (RTC only). Full replay4.38% faster, scene only0.888%; downloads increased.
Measure both scope and phase: do not turn the full-run benefit into a gameplay
FPS claim. Existing conversion/wrap/fault behavior retained; see PERFORMANCE.md.


### HG-LEARN-017 — Gather VU flags while lane results are live

Fuse temporary flag collection into multiply_acc/madd_vector computation, but
commit flags/results at the original boundaries so faults and aliasing match.
49152 frozen-reference cases and four original replays agree (RTC only); scene
mean1.375% lower. No compound force-inlining; arithmetic/timing unchanged.
Evidence PERFORMANCE.md and vu-flags-fused artifacts. Benefit is modest/variable.


### HG-FAIL-008 — Runtime /O2 placement alone did not improve VU arithmetic

Moving Fpu::add and five compound VU helpers from the /O1 AOT unit into one /O2
runtime unit preserved state but slowed two matched pairs by1.65% on average.
Reverted; retained fused flags. This combined placement/inline-boundary experiment
does not prove /O2 generally slower. Revisit only with a changed hypothesis and
measurement. Candidate/source/map in vu-o2-rejected; PERFORMANCE.md has evidence.


### HG-FAIL-009 — PSMT8 table relocation lacked a measured scene benefit

Moving psmt8_word to gs.cpp with proven X/Y table factoring passed exhaustive
address checks and original equality, but two pairs disagree and mean is0.35%
slower. Reverted the combined change. Tables remain valid in existing DrawTexture;
revisit only with new measured evidence/scope. Artifacts psmt8-table-rejected and
psmt8-table-pair-times.json. Refresh restored build-input timestamps so CMake/MSBuild
reconfigure/recompile rather than reuse stale candidate dependencies.


### HG-LEARN-018 — Prototype exact SIMD at the complete helper boundary

SSE2 integer broadcast products preserve the scalar finite-exponent profile.
Product-only timing overstates comparison if compiled scalar settings differ;
compare against the actual /O1 scalar helper, including checks,flags and addition.
Full helper synthetic23-53% benefit becomes only1.42% mean scene gain; four
original states/images match (RTC only),245760 integrated cases pass. Inspect
AOT masks before widening scope: XYZ sites exceed XYZW here, but static counts
are not runtime frequencies. Source/bench evidence in PERFORMANCE.md; other-host
scalar fallback remains and cross-platform performance is not verified.


### HG-FAIL-010 — More SIMD mask coverage did not improve the matched scene

XYZ extension preserved all checked state and inactive W behavior but paired
timings disagreed; mean0.19% slower. Reverted extension, retained full-lane path.
Static AOT site counts are useful leads but cannot substitute for dynamic cost
and matched timing. See vu-xyz-rejected and PERFORMANCE.md.


### HG-LEARN-019 — Batch proven packed IMAGE word ownership

PSMT8 aligned16-pixel rows pair opposite byte lanes in8words within one16-word
column. Compute address/row once and synchronize an explicitly checked mutable
span; avoid16independent observer/address operations. Retain scalar fallbacks for
unaligned,partial or row-crossing qwords and exact completion/padding semantics.
3168scalar transfer scenarios,910GPU cases both modes and4original states match
(RTC excepted);scene mean9.10% lower. Unlike HG-FAIL-009,address-only relocation,
this removes repeated coherence and per-pixel division together. See PERFORMANCE.

### HG-FAIL-011 — Synthetic cancellation gains did not establish scene benefit

FPU add normalization bit scan preserved3M+12240results/flags and original states,
but original paired timings disagreed (one6.73%faster,one2.44%slower). Mean2.33%
gain was dominated by slower first control;reverted instead of assuming synthetic
5-52%gains transfer to this game. Technique valid but workload benefit unproven.
Evidence fpu-normalize-rejected and PERFORMANCE. Retain original add loop.

### HG-LEARN-020 — Bound scanout ownership by physical pages

Use original format page geometry to select displayed VRAM pages,coalesce runs,
and retain exact pixel conversion via const pointers.216cases check both bytes
and exact observer footprint;910GPU cases and4original captures agree (RTC only).
Readbacks decrease~130MB but readback time rises;full replay1.02%faster and scene
0.74%mean benefit uncertain. Retain narrower ordering;do not equate bytes saved
with speed. See PERFORMANCE and scanout-pages artifacts.

### HG-LEARN-021 — Tune optimization per generated translation unit

MSVC /O1 remains appropriate for huge EE shards during development,but the1.19MB
main containing VU program bodies benefits from /O2. Source-level override retains
shard compile settings;observed~10GB compile memory,completed normally. Four original
states match RTC excepted,scene mean5.89%faster;whole replay only0.90%. Rejected
helper-only relocation does not predict complete-call-site optimization. Verify
actual generated build options and matching map;other-host performance unverified.

### HG-FAIL-012 — Contiguous VIF fast path lacked reliable workload benefit

V4-32 unmasked mode0/CL==WL memcpy is equivalent under checked bounds,but original
second pair is flat(+0.13%)and mean gain comes from slow first control. Reverted;
600scalar cases and4original equality checks show correctness,not value on this
scene. Actual fast-path frequency was not measured. Future timing helpers warm
both binaries before measured pairs due recurring first-control variance.

### HG-LEARN-022 — Pack full-vector flags together, and warm both binaries

Exact SIMD products/additions already produce lane arrays. Gather zero/sign masks
once,retain UF/OF masks,and reverse lane nibbles for MAC rather than packing each
lane separately.245760+256cases and4original states agree RTC excepted. After
warming both binaries,scene means9.08285->8.87205s(-2.32%),both pairs faster.
Keep warm-ups excluded;changed-image interval counts still fluctuate and are not
internal FPS. Scope full-lane native helper only,not arbitrary VU flag behavior.


### HG-LEARN-023 — Profile startup and gameplay separately; distinguish call boundaries

Early5M-start sample is dominated among identified functions by scalar PSMT4/
bilinear texture work,while30M-start sample emphasizes FPU/VU. Selecting fixes
from gameplay alone misses startup costs. Unresolved indirect-site counts are not
counts of missing or skipped functions:4598EE indirect boundaries coexist with
successful bounded replays because runtime targets may already be compiled.
Verify original callback extent/live use before roots;explicit fault behavior
checks cannot prove native service fidelity or cover unvisited paths. Evidence:
startup-coverage-audit/summary.json and PERFORMANCE.md. No runtime changes.


### HG-LEARN-024 — Prepare indexed sprite state without caching sampled pixels

PSMT4/CT32CLUT sprites reuse immutable TEX0/wrap/CSA state but read texture words
and CLUT validity live in original row-major order.128scalar alias/fault fixtures,
910GPUbothmodes and4original states agree RTCexcepted. Startup3.58%faster,whole
replay2.34%;gameplayflat. Preserve this phase distinction. See PERFORMANCE.


### HG-FAIL-013 — Sparse raster readbacks saved bytes but lost throughput

Demand8KiBreadbacks plusper-pixelownershipcache reducedtransferbytesbut increased
GPUwait/CPUcost;gameplayscene7.36%slower.128KiBdirty-pageprefetchreducedpenaltybut
still1.32%slower. Bothpreservedtestedstate;bothreverted. Whole-replayreadbacktime
cannotbe assigned togameplayscene withoutphaseattribution. Nexttransferworkneeds
coalescedprovenfootprints orfewerCPUfallbacks,notrepeatdemandpagevariant. Evidence
PERFORMANCE.md,sparse-single-page-rejected,sparse-grouped-rejected.

### HG-LEARN-025 — Narrow XYZ AOT specialization has a modest measured benefit

Newruntimevu_xyz helpersselectedonlyfororiginalbroadcastXYZ MULAbc/MADDAbc/MADDbc
keep scalarproduct/add arithmetic andoriginalfaultfallbacks.872AOTcalls changed;
295168referencecases,15emittertests and4originalstates agreeRTCexcepted. Warmed
scene0.67%lower,bothpairsfaster;retainwithsmall-effect caveat. Not evidenceforbroad
SIMDXYZ orcompoundforce-inlining. See vu-xyz-scalar artifacts andPERFORMANCE.

### HG-FAIL-014 — Replacing command storage did not reduce scene wait

Fresh glBufferData storage for each immutable command/tile batch preserved all
910 GPU cases in both modes and four original captures, but warmed scene mean
8.8928->8.9454s (+0.59%). Reverted. Driver wait attribution alone does not show
that overwriting command storage causes the wait. Keep existing sub_data path.
Artifacts command-storage-baseline/rejected and command-storage-pair-times.json.

### HG-FAIL-015 — Combining dependency clocks increased native cost

AOT max of next issue cycle and all operand readiness timestamps preserved
131072 randomized clock/Q/P cases and original state, but scene mean
8.86385->9.61985s (+8.53%). Reverted emitter/generated prologues. Equivalent
fewer helper calls do not guarantee cheaper generated code; keep original sequence.
Artifacts vu-pair-clock-baseline/rejected and vu-pair-clock-pair-times.json.

### HG-FAIL-016 — Packed32 product normalization lacked repeatable benefit

Four packed32 normalization lanes after exact SSE2 24x24 multiplication passed
295168 arithmetic cases and four original state comparisons. Warmed scene
9.5522->9.48645s, but pairs disagree (1.42% better then effectively flat/slower)
and whole process was slower. Reverted; keep original two64-lane normalization.
Artifacts vu-packed-products-baseline/rejected and corresponding pair-times.json.

### HG-FAIL-017 — Selective MSVC LTCG hurt gameplay throughput

/GL on generated main and VU helper sources with /LTCG preserved all four original
states (RTC only), but warmed scene8.95825->10.5671s (+17.96%). Startup improved
18.250895->17.76108s, illustrating phase-specific tradeoffs. Reverted CMake and
binary/map. Linker used about16GB private memory and completed; no memory failure.
Do not infer faster execution from broader compiler visibility. Artifacts
vu-ltcg-baseline/rejected, vu-ltcg-pair-times.json and vultcg-*.

### HG-LEARN-026 — Separate bulk read visibility from raster write ownership

Keep original full readback but mark only conservative clipped frame/depth pages
writable. This avoids false dirty state without per-pixel checks.1920 page cases,
910 GPU cases both modes and4original states agree RTC-only; warmed scene0.68%
faster, both pairs better. Small gain; do not equate write-footprint reduction with
readback savings. Inputs need an independent footprint proof before narrowing.

### HG-FAIL-018 — Bounded raster reads saved bytes, not gameplay time

Whole wrapped-texture/output page bounds passed5760 read/1920 write coverage cases,
910 GPU comparisons and4original states (RTC only). Downloads fell~31%, but scene
8.8659->8.90155s (+0.40%). Deferring eager CPU-fallback flush through resident
hazards passed914 cases and4originals but8.85645->8.8966s (+0.45%). Both reverted;
write-only scope remains. Coalesced ranges still expose readback latency; bytes or
flush counts alone are not the optimization objective. Evidence raster-read-pages,
raster-deferred-flush and raster-read-deferred-rejected artifacts. Retain four new
GPU/CPU dependency fixtures; no deferred-flush API or texture-read helper remains.

### HG-LEARN-027 — Exact small-integer conversion can supply packed bit length

For post-carry addition mantissas0..2^24-1, binary32 integer conversion is exact.
Its exponent replaces five packed normalization steps without rounding guest sums.
16M scalar comparisons across4host rounding modes plusVU/GPU/original-state checks
pass; scene0.83% lower, both pairs faster. AVX2 runtime CPU/OS guard and scalar
fallback retained. Scope full-lane validVU MADD; not general host-float replacement.
General packed binary-normalization variant was0.65% slower, so SIMD alone was
insufficient. Evidence PERFORMANCE and vu-add4-exact artifacts.

### HG-FAIL-019 — Padding XYZ to reuse add4 did not improve the original scene

Three scalar products plus padded packed addition preserved VU tests and4original
states (RTC only), but scene9.4156->9.47435s (+0.62%), both pairs slower. Reverted
XYZ only; full-lane exact add4 remains. Packing/setup overhead can outweigh one
shared SIMD operation for three lanes. Artifacts vu-xyz-add4-rejected and
vu-xyz-add4-pair-times.json. Do not generalize full-lane gains to partial lanes.

### HG-LEARN-028 - Profile costly fallback cases before extending GPU coverage

A small count can dominate a primitive class:292 untextured blended triangles
cost0.364s in the measured scene. Extending the existing exact integer tile path
to TME-off removed STQ/texture work and corresponding texture dependencies while
retaining conservative frame/depth/alpha checks.1018 cases per GPU memory mode,
actual acceleration checks and original snapshots pass. Matched hidden scene
mean8.2572 ->7.73375s (6.339% lower), both pairs faster; no visible30FPS claim.
Remove detailed diagnostic logging once the case is identified. Scope limited to
validated original/CPU behavior; physical-console completeness remains unproved.
Evidence: PERFORMANCE and external untextured-triangle-summary.json.

### HG-LEARN-029 - Keep exact wide product lanes through normalization

Four AVX2 unsigned24x24 products retain64-bit lanes for carry/exponent selection
and saturation. Unlike rejected packed32 normalization, this reduced matched
scene time2.476%, both pairs faster.16M scalar comparisons across four rounding
modes and four original image/state captures pass. Keep CPU/OS dispatch and
SSE2/scalar fallback; SIMD instruction counts alone do not establish a speed gain.
Evidence vu-product4-avx2-summary.json. No clock or guest arithmetic changes.

### HG-FAIL-020 - Extending packed helpers to plain full vectors did not help

Full-lane multiply/add/subtract reuse passed49152 new frozen-reference checks and
four original captures (RTC only), but matched scene7.5568 ->7.5691s (+0.163%).
First pair slower, second flat. Reverted; packed arithmetic success in MADD does
not establish profitable reuse in every vector helper. Saved implementation and
captures under vu-full-vector-rejected and vufullvector-*.

### HG-LEARN-030 - Masked GPU stores can avoid CPU readback before partial writes

Profiled PSMT8 host writes were waiting for GPU-owned pages only to preserve other
bytes. Merge masks/values per unique word and execute exact integer RMW on GPU;
keep normal materialization at CPU consumers and draw hazards. CPU-owned pages
retain scalar stores.1042 cases per mode and four original captures pass; scene
7.53305 ->7.33985s (2.565% lower), both pairs faster. Retained with opt-in residency.
This removes a demonstrated synchronization boundary rather than merely reducing
readback bytes. Full console fidelity and30FPS remain unproved.

### HG-LEARN-031 - Snapshot only operands that a paired instruction can change

Static VU lane/write metadata proves111 of131 SQ sites cannot observe same-pair
upper VF changes. SQ reads no ACC/flags and upper writes no VI, so these sites
need no whole-state copy;20 overlaps retain original snapshots.104 Python tests
and four original captures pass. Scene7.2706 ->7.16805s (1.410% lower), both pairs
faster. Generated VU block replaced only after exact old-emitter matching; no
heavy arithmetic inlining or timing change. Evidence vu-sq-snapshot-* artifacts.

### HG-LEARN-032 - Combine host arithmetic calls without fusing guest rounding

One guarded AVX2 call inlines the existing integer product followed by exact add,
retaining product saturation and combined UF/OF masks.4M chained comparisons and
four original captures pass; scene7.1987 ->7.14815s (0.702% lower), both pairs faster.
Inlining stays in the small ISA source, outside the generated CFG. No host FMA,
clock change or general claim that broader inlining helps. Evidence PERFORMANCE.

### HG-LEARN-033 - Prove repeated readiness checks redundant at build time

Track per-lane readiness within static blocks, reset at every alternate entry,
and retain original masks after clock wrap. Skip only proven no-op VF checks;
keep all advances/ready writes/Q/P completions and VI checks.49152 compiled state
comparisons cover wraps, loops, delays and faults; four originals pass RTC-only.
Scene7.1256 ->6.9381s (2.631% lower), both pairs faster. Unlike rejected runtime
combined-clock helpers, this removes host checks without changing the clock path.
Evidence vu-readiness-proof-summary.json; model fidelity limits still apply.


### HG-FAIL-021 - Four-stage prepared matrix path regressed the scene

This guarded MULA/MADDA/MADDA/MADD preparation with four runtime commit calls
passes69632 compiled CFG and1048576 stage comparisons plus four original captures
(RTC only), but warmed alternating scene7.80045 ->8.53190s is9.377% slower, both
pairs. Reverted emitter/generated/CMake activation and archived new tests/source
under matrix183227-rejected. Preparation, flag storage and commit dispatch costs
are hypotheses, not isolated causes. Revisit only with a different measured
bottleneck or lower-overhead design; fewer arithmetic dispatches alone is no gain.
Existing compatibility fixes and readiness optimization remain. Scope this scene
and implementation; no general proof against matrix batching. See PERFORMANCE.

### HG-LEARN-034 - Measure actual fallback classes before adding renderer coverage

In gameplay31M..33M, an observation-only triangle census counted264257 triangle draws after clipping:244 untextured,264013 textured STQ-ready,0 textured FST,0 STQ-unready and0 fogged. The existing GPU path attempted all264257, accepted245616 and rejected18641 to CPU fallback. Therefore adding FST triangle support would not help this measured gameplay at all despite looking like an obvious source-code gap. Profile the actual rejection class before implementing new coverage; target the18641 proven backend/predicate rejections instead. Scope is this fixed gameplay segment, not a claim that FST is unused elsewhere. Evidence project-link-runtime/20260921T075705Z-9767c8ed0c76495590730899ff6f8e85.

### HG-LEARN-035 - Split backend rejection causes before broadening GPU semantics

A second31M..33M census resolved the18641 triangle CPU fallbacks:263 fail the front-end texture-format restriction and18378 are rejected only by the OpenGL backend's texture/output page-alias guard; frame/depth alias and every other front-end/backend category are zero. Do not jump from 'alias rejection' to unsafe parallel feedback rendering. First tighten conservative ownership footprints using already-proved wrap/swizzle/page rules; admit only cases whose tighter read/write sets are disjoint, leaving true feedback on CPU. Evidence project-link-runtime/20260921T080133Z-cf47199dc55a474ea17fc0669cdfa971.

### HG-LEARN-036 - Verify semantic output before treating overlap as removable

The18378 measured triangle backend alias rejects were split by actual output semantics in gameplay31M..33M. Every reject overlapped framebuffer pages, zero overlapped depth pages, and zero used FRAME.FBMSK=0xffffffff or otherwise had no-effect writes. So this workload's feedback class is not a false dependency created by fully masked color output. Stop no-op-output admission work here and investigate genuine ordered framebuffer feedback instead. Evidence project-link-runtime/20260921T081347Z-cf5afc140bad4634a1c56ca8265a5f8c.

### HG-LEARN-037 - Reduce genuine feedback to the exact texture interpretation before designing synchronization

The PCSX2-guided HG feedback-map capture found an unusually narrow class: all18378 rejected gameplay triangles use nearest-filtered PSMT8H, all use TEX0.TBP0 equal to FRAME.FBP and texture stride equal to framebuffer stride; CT32, PSMT8, linear and other formats are zero. PSMT8H reads the high byte of the same CT32 word-address layout and then uses that byte as a CLUT index. This turns the next correctness question from general framebuffer feedback into exact source-word versus destination-word dependence per covered pixel. Prove that relation before choosing framebuffer-fetch versus snapshot/copy semantics. Evidence project-link-runtime/20260921T083235Z-0a4b130471b5400693210ae98b0b9330.

### HG-FAIL-023 - Tighter whole-texture page bounds did not admit triangle work

The backend's broad texture interval looked capable of false feedback aliasing, so a tested whole wrapped texture-page footprint (including REGION_CLAMP minima, REGION_REPEAT bounds, base straddles and VRAM wrap) replaced it only for GPU triangle hazard admission. Coverage tests passed and the qualified candidate preserved frame/EE/GS/VU/normalized-IOP state, but GPU triangles remained exactly305856 and steady gameplay measured9.060FPS; no rejected triangle was newly admitted. The18378 measured alias fallbacks therefore still overlap the output pages at this page granularity. Reverted helper/backend/tests. Do not repeat page-bound tightening for this workload; next inspect whether the output side marks semantic no-op writes as hazards before considering true feedback support.

### HG-FAIL-022 - Moving CLUT draw barriers did not remove rendering work

Gameplay31M..33M showed23399 CLUT loads with pending work and zero conservative
FRAME/ZBUF overlap with the incoming palette source, but preserving old palettes
with per-draw snapshots and deferring those barriers regressed qualified steady
FPS from9.3183 to4.2280 in the exact layout-matched control. GPU work stayed
essentially identical:48195 sprites,305856 triangles,1014 triangle batches and842
masked-transfer batches with similar flush/readback time. The candidate also kept
more completed draw history and paid overlap/snapshot bookkeeping. Matching frame,
EE RAM,GS VRAM,VU and normalized IOP prove this was throughput,not visible state.
A costly call site can contain required work merely charged there; moving the
synchronization point is not an optimization unless it actually reduces work or a
measured host synchronization. Reverted all snapshot/probe/test source; keep the
observation evidence only. Evidence verifier d0149b1954... vs6a47185539c0....

### HG-FAIL-024 - Proven same-pixel PSMT8H feedback was correct but slower on the current compute backend

The exact feedback proof succeeded: all18378 measured gameplay31M..33M rejects had Q=1.0 at every vertex and exact 12.4 source-minus-screen deltas dx=dy=8, while794236/794236 covered samples independently mapped to their own destination CT32 word. A narrow candidate therefore admitted only nearest PSMT8H same-base/same-stride draws satisfying the constant-Q positive-subtexel proof and wrap-identity guard; its per-pixel ordered shader read the current destination word high byte as the palette index. The full33M replay moved GPU triangles305856 ->328503 (+22647 across the full replay) without faults. The fixed verifier preserved frame SHA02723437..., EE RAM8747a80e..., GS VRAMa30ba399..., GS stateeaf454f2..., VU state5adb6544... and normalized IOP01dc90c6....

Despite semantic equivalence, a layout-matched marker-off A/B was slower with feedback enabled: candidate final heavy windows3.15966/3.16915s =9.480455FPS versus control3.13418/3.14582s =9.554140FPS. Candidate total heavy host time6.32881s versus6.28000s (+0.777%), and both individual heavy windows regress. Reverted the specialization/backend/shader/tests. Keep the proof as reusable knowledge, but do not repeat this in-place per-pixel PSMT8H compute specialization unchanged; moving these CPU fallbacks to the existing GPU tile backend adds enough batching/synchronization/work to erase the saved CPU raster cost. Evidence candidate verifier b894c58157df40d79cfafcf2ec461843; layout-matched control e093bf9d0518463e8661e387e22aadad; semantic proof project-link-runtime/20260921T084132Z-1247e861480f4e5a9833ce0ee51da2b4.

### HG-FAIL-025 - Coalescing fragmented readbacks by over-reading did not improve gameplay

A full33M classification found resident multi-page CPU reads responsible for1006640500ns of1017485600ns measured readback time (98.93%) and2676965376 of2681552896 downloaded bytes (99.83%).1731 dirty multi-read events expanded to3537 synchronous glGetBufferSubData ranges. A second observation showed one first-to-last bounding read per event would add only23704 pages over327338 dirty pages (+7.241% transfer volume), so a production candidate fetched fragmented events into a reusable staging buffer and copied back only the original dirty runs; clean/CPU-owned gap pages, ordering, barriers, faults and gpu_dirty semantics were preserved.

The candidate preserved the established frame/EE/GS/VU/normalized-IOP hashes and qualified864 original0x001bef84 writes, but throughput did not improve. Full replay readback rose to1056517700ns and download volume to2880323584bytes. Qualified steady gameplay was9.378693FPS (windows9.470325/9.288817), below the stronger recent layout-matched9.554140FPS control. Source was restored byte-exactly. Reducing glGetBufferSubData range count by bounding-span over-read/staging is therefore not useful on this workload; do not repeat this mechanism unchanged. Next isolate readback/synchronization inside the actual31M..33M heavy window before investing in a genuinely different mapping/fence/staging mechanism. Evidence project-link-runtime/20260921T103855Z-dbe0e619b99a418ab34b14ee61f1eada, project-link-runtime/20260921T104328Z-311b52eb2a58411eb94a3697ae3b1591, candidate project-link-runtime/20260921T104813Z-6724e7c1730a4ccda7d3d5603d87f622 and verifier d043105abbb14e42a897f37a3ec532a3.

### HG-FAIL-044 - Exact CPU micro-optimizations inside the qualified drift band are not wins

Two low-level candidates preserved the fixed gameplay verifier but failed to clear current host variance. IOP straight-line switch fallthrough kept every original dispatch budget/trace/load-delay boundary and measured12.963275FPS. Replacing IPU/GIF/VIF1/SIF1 source-qword byte assembly with portable native64-bit loads passed runtime/translation/VU tests and exact retained state, but measured13.005903FPS. Adjacent clean qualified runs span12.913251..13.083097FPS. Both candidates were restored byte-exactly. Do not stack tiny dispatch/load changes whose only evidence sits inside that band; target a measured cost large enough to produce a repeatable paired gain.

### HG-LEARN-054 - Keep source portable to GCC/Clang pp-number and target-attribute rules

MSVC accepts `0x4e+context`, but GCC/Clang lex it as one invalid preprocessing number because hex digit `e` followed by `+`/`-` looks like an exponent. Put spaces around `+`/`-` after any hex literal ending in `e`/`E` (grep `0[xX][0-9a-fA-F]*[eE][+-]`). GCC lambdas also do not inherit a surrounding `__attribute__((target("avx2")))`, so always-inline AVX2 helpers called from lambdas fail to inline; compile AVX2-only, CPU-guarded dispatch files with per-file `-mavx2` (mirroring MSVC `/arch:AVX2`). Evidence: 2026-09-28 Debian trixie GCC build of the base runtime and full `hg_game`.

### HG-LEARN-079 - Compress queued renderer state across draws, not just the raster swap

The exact 180M route rasterizes 61,992,917 primitives.  Replacing each draw's
1 KiB GS-register snapshot with one base plus ordered register deltas, and carrying
a persistent replay cursor across raster service calls, improves the final heavy
interval repeatably from 16.1883 to 17.4105/17.3488 updates per second while all
retained frame, EE, GS VRAM/state, VU and console captures remain byte-identical.
The earlier selected-register swap was flat/regressive because it left snapshot
construction/storage intact and added per-draw swapping.  Delta replay must be
strictly linear: restarting from the base at every service call creates quadratic
stage-C stalls.  Keep large working snapshots heap-backed with deep-copy value
semantics; placing two more 1 KiB arrays inline overflowed the monolithic GIF test
stack frame.  This removes a measured structural renderer cost but does not alone
reach 30 FPS; current evidence remains about 17.35--17.41 in the worst interval.
