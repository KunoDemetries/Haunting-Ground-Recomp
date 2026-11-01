## 2026-10-11 HG-PERF-116/118/119 — heavy gameplay at 30 FPS (paced) / ~42 updates/s (unlimited)

Same contract (`gameplay180m_threaded`, heavy window 150M–179M, 839 writers, byte-identical captures). HG-PERF-116: all GS ATST modes on GPU triangles, aggregate 31.90 vs 26–31 controls. HG-PERF-118: `--preview-file` encode/write moved off stage C, aggregate 42.5 vs 36.6, p10 41–42 vs 28.5–29.9. HG-PERF-119: speculative VU1 workers on by default (3 on ≥8 threads). Final production EXE `e053f17b…` with no overrides: unlimited **42.22 / 42.52** (0 of 28 intervals below 29.9); `--realtime` **29.96** aggregate, 27 of 28 intervals at 29.91–30.08 (one 29.01). Without workers the same build gives 24.7 / 23.6. Host: Ryzen 7 9800X3D / RTX 5070 / NVIDIA 617.42 / Balanced power plan. Other scenes and hosts are unmeasured.

## 2026-10-11 HG-PERF-114/115 normal-threaded benchmark and deferred resident GL error polling

New benchmark contract `gameplay180m_threaded`: the pinned `gameplay180m_authentic` inputs without `--profile`, plus `--frame-rate-probe` (HG-DIAG-114). The metric is the 30-writer checkpoint rate over 150M–179M (839 writers). On a Ryzen 7 9800X3D / RTX 5070 / NVIDIA 617.42: `--profile` 13.28; threaded 24.30; threaded + `HG_VU_WORKERS=3` 27.0–27.6. Stage C (GIF/GS/GL) is saturated (HG-DIAG-091). HG-DIAG-115 sampling showed the resident post-`glFlush` `glGetError` was 14.5% of stage C. HG-PERF-115 defers it to coherence points: heavy-window mean 28.10 vs 27.09 (+3.7%), median +5.0%, five interleaved pairs, byte-identical captures. Provisionally retained.

## 2026-10-11 HG-PERF-112 doubled GPU triangle capacity � verified activation, rejected throughput

On the same authentic 180M recorded-input route the candidate increased the triangle job payload flush cap from 1024 to 2048; 21,985,289 triangles stayed the same while triangle batches fell 36,047 to 25,670 (-28.79%) and overall GPU flushes 58,247 to 47,869 (-17.82%). Both candidate and fresh restored control reached 5,270 qualified original frame writers, zero unexpected writers/caller faults, with exact final frame, EE/GS VRAM/GS state, VU1, console and input captures; IOP raw RTC bytes differed. Final 30-writer interval candidate **14.0522** versus adjacent control **14.8173** updates/s (-5.16%). Host time 282.100 versus 270.743 seconds. Large run-to-run variation prohibits claiming a stable causal regression, but no >=5% speed gain is established. Candidate rejected and production restored; don't chase cap-count as synonymous with GPU bottleneck. Evidence Project Link `961182539b3e4d23bf35f6501f22aa94` and `5abc546a528c4592badf45b5a2fcf6b1`. HG-FAIL-068.

## 2026-10-10/11 HG-PERF-111 � exact 180M candidate/control replay, rejected

Same `gameplay180m_authentic` fixed 180,000,000-slice recorded input on RTX 5070/NVIDIA 617.42, exact original writer 5,270 with zero writer/caller/fault violations: first restored control 13.3947, candidate 11.7817, subsequent restored control 14.8537 qualified 30-writer updates/s at 180M; 175M checkpoint respectively 11.8604, 12.0715, 14.4079. Both controls exceed candidate's final interval; candidate misses >=5% retention gate. Whole-run host time 299.194/316.639/320.857 s emphasizes host variability and prevents assigning the entire candidate deficit causally. Candidate's earlier ~22% resident synthetic flush improvement therefore did **not** demonstrate authentic gameplay throughput improvement. All three match final frame, EE RAM, GS VRAM/state, VU1, metadata, console, recorded input and 5,270 writer sequence; raw IOP RAM differs in the previously documented RTC byte region, and complete masked IOP equivalence remains separately unproved. The original source was byte-restored to `5354e22bd...` and game rebuilt as EXE `99509686...`. Replay jobs control/candidate/restored-control: `7272e41e`, `d4fb425c`, `71886911`. HG-FAIL-067; no HG-PERF-111 production retention. Next: independently rank removable cost at the current triangle batch-size ceiling before considering another change.

## 2026-10-09 HG-PERF-107 exact first-covered edge stepping rejected flat

Replacing repeated first-covered edge determinants with exact signed-64 edge stepping
preserved every retained artifact and passed build/runtime/translation/VU plus 1,159
resident GPU/CPU cases. Authentic 180M candidate/control both reached 5,270 qualified
original writers with no writer/caller/fault violations. The candidate measured
12.0187 vs 11.9157 updates/s at 175M (+0.86%) and 12.0512 vs 11.9113 in the final
175M--180M interval (+1.17%). The change is correct and measurably cheaper but misses
the >=5% retention gate, so `gs.cpp` is restored exactly and the temporary replay
harness removed. HG-FAIL-064.

## 2026-10-09 HG-PERF-106 wide-STQ fallback rejected flat

A native-relaxed-only widened STQ fallback safely moved all 6,195 product-overflow
perspective triangles in the exact 180M route onto the GPU. The candidate reached
5,270 qualified original writers with zero writer/caller/fault violations; display,
EE RAM, GS VRAM/state, VU1 captures/state and console are byte-identical to the
adjacent disabled control. The focused GPU differential passes 1,159 cases and the
runtime/translation/VU suites pass. Despite complete activation, adjacent tail
measurements are effectively flat: 175M candidate/control 11.9849/11.9479 (+0.31%)
and final 180M 11.9595/11.9406 (+0.16%). The 6,195 recovered triangles are only
~0.028% of ~21.99M triangle draws, so the candidate fails the material-gain gate.
Capability remains disabled; temporary CMake replay plumbing is removed. HG-FAIL-063.

## 2026-10-05 Linux: speculative parallel VU1 + device-path batching, 6.2 -> ~9.3 FPS (exact)

Linux, 8 logical CPUs (2 P-cores with HT, 4 E-cores), `CPUS=0-7`, gameplay33m qualified replay, all digests identical to `st-base3`. `HG_VU_WORKERS` unset: 6.09-6.68 FPS. Lane-granular speculative VU1 (`HG_VU_WORKERS=3`, exe e3ad3499...): 8.35-9.06; 2 workers 7.6-8.2; 1 worker no gain; 4-5 workers 7.2-7.4. VU1 re-executions 103356 -> 439 of 155375 jobs after lane-granular reads. Subsequent exact host-path changes (incremental UNPACK-union validation, GIF boundary tracking/block submission, VIF incomplete-command skip; exe 01204282...): 8.7-9.5. Rejected: B never stealing jobs (HG-FAIL-057), `stream_gif_packets=true` again (9.48/9.03 vs 9.51/9.26). Steady-window CPU: stage C ~95%+ busy (GS raster, GIF decode, GPU observer), stage B ~half VU work from stealing, EE thread ~80% with ~16-20% waiting in transport-read joins that drain in-flight VU jobs.

## 2026-09-27 HG-DIAG-088 XYZ ADD/SUB SIMD candidate restored; adjacent pair favors candidate but host drift prevents retention

Exact-state candidate `37aabe2e...` measured10.9755428FPS with steady windows10.8044644/11.1521262. After byte-restoring its three files, clean executable `b5beb2db...` measured10.4785007FPS with10.2415644/10.7266597 in verifier `d3563b0c...`; all retained captures and864 original writers match in both. This adjacent pair favors088 by ~4.74%, but the entire host moved far below the earlier clean13.5107444FPS sample. Production therefore remains restored clean and088 is not retained from one drift-affected pair. A future revisit must repeat the exact candidate/control reversal under close host conditions.

## 2026-09-27 HG-DIAG-085 helper-only PGO rejected; final clean OFF 13.793135 FPS

Helper-only MSVC PGO trained successfully on the exact fixed33M route and preserved all retained state, but it did not improve qualified heavy gameplay. USE executable `cf904288...` measured13.3542847FPS and13.2833878FPS in two exact-state runs; adjacent ordinary OFF `5fc22723...` measured13.4984342FPS. Candidate heavy subwindows13.2998/13.4092 and13.2838/13.2830 are all below control13.4622/13.5349. The predeclared>=5% retention gate fails; PGO is removed rather than retained as complexity.

After byte-restoring the PGO-specific CMake/provenance/test plumbing and rebuilding ordinary production, current executable `93de29f8...` / verifier `dad485f3619d40c48bf17dafcc5ba957` qualifies151-input provenance,864 original writers, zero validation failures and exact frame/EE/GS/VU/RTC-normalized IOP state at **13.79313516 FPS**, windows13.7588171/13.8276248. This higher OFF sample is host/build variance after restoration, not a claimed optimization gain.30FPS remains unmet.

## 2026-09-27 clean qualified baseline after079

Exact-restored game6396dcd3/manifestbff77fa5 passes fixed gameplay verifier3a909f8c (evidence79b12154): final60 original writer boundaries in4.56449s = **13.14495157 FPS**, two windows13.42396/12.87731,864 original events and all retained frame/EE/GS-VRAM/VU/RTC-normalized IOP captures exact; passes_30fps=false. No079 probe or optimization remains. This is a source-identical clean control against prior13.11040, not a new speedup.

## 2026-09-27 VU VF readiness attribution (079)

Original33M profile c2df25ec reached budget/864 original writers/no fault. Heavy31M..33M nonzero VF readiness calls12,489,360:96.535% zero-wait and3.465% stall. One-in256 timing sampled1,554,700ns zero-wait and69,400ns stall, fixed256x estimate0.3980032s+0.0177664s=0.4157696s within the~4.5s host interval. Clock/branch overhead and changed inlining make this an upper-biased approximate host cost; instrumented FPS invalid. Full removal would cap the latest clean13.1104 rate around14.42FPS at fixed60 frames, so this helper alone cannot solve30FPS. All source probes restored byte-exact and clean game rebuilt (6396dcd3); no optimization retained.

## 2026-09-27 IMAGE qword CPU attribution (077)

One original33M unpaced heavy replay sampled1/64 completed IMAGE qwords by GS destination format. PSMT8 6,389,760 calls estimates0.2456576s, PSMT4 245,760 estimates0.0715264s, CT32 103,680 estimates0.0085568s; total~0.3257408s/31M..33M. This is a nested part of complete GIF~0.741s and must not be added to it. Even full removal has a ~14.1FPS upper bound from13.11, not30. Probe removed and clean source/executable rebuilt. No optimization retained; instrumented rate is not FPS evidence.

## 2026-09-27 clean post-diagnostic qualification

Exact-restored current game5c7d7f20/manifest0b36814d passed fixed33M frame verifier e3bca4ed (evidence8080370e):60 final original0x001bef84 boundaries over4.57652s = **13.11039829 FPS**, individual windows13.38867/12.84346. All864 original writer events and retained frame/EE/GS-VRAM/VU/RTC-normalized IOP captures match prior clean. No diagnostic code or speedup retained;30FPS unmet.

## 2026-09-27 complete EE/IOP/SIF attribution (076)

Complete heavy31M..33M scopes measured4.4590229s wall,0.4942611s EE run/observation,0.1860953s IOP run/I-O,0.0537283s SIF. These three scopes are mutually disjoint; compared across runs to HG-DIAG-049's1.3101594s complement, they account for ~0.734s and leave ~0.576s other/overhead with host drift and instrumentation caveats. Original replay passed33M/864 writer events/no fault. Probe removed, clean game rebuilt. Instrumented13.5333 interval rate is not qualified FPS.

## 2026-09-27 heavy phase-sample limitation (075)

The existing1/256 per-slice phase sampler collected7,825 samples in31M..33M. Scaled phase sum1.3766912s versus4.3229158s measured wall, missing~2.946s of bursty work. Thus phase shares from this sample cannot partition the exclusive-profile1.31s complement or justify a renderer rewrite. Unlike sampled program completions in073/074, selected slices are unrepresentative of costly bursts. Probe removed; no FPS claim.

## 2026-09-27 VU program attribution (074)

Heavy31M..33M original replay sampled1/16 completed program activations by independently verified AOT identity. Sampled total116.284ms, fixed16x ~1.860544s, close to prior exclusive VU1.907775s. Program1 ~0.703173s, mostly the073 loop ~0.667296s. Others collectively ~1.157371s, largest individual program3 ~0.238402s, program8 ~0.229874s, program9 ~0.194954s, program5 ~0.161112s. Sampled intervals are approximate and include timing bias; no qualified FPS from probe. No single other program has enough cost to reach30FPS alone. Probe removed and clean AOT game rebuilt.

## 2026-09-26 sampled hot VU loop attribution (073)

Unpaced original33M replay7ce957bf sampled43,722 completed of2,798,220 loop attempts in heavy31M..33M. Whole-loop sampled time10.4265ms and nested fused-transform4.3807ms; fixed64x estimates0.667296s loop,0.280365s transform,0.386931s remainder. These are host costs with timer/sampling bias, not exact exclusive totals; the nested interval is part of the loop and must not be added. Compared with older ~1.908s exclusive VU/~4.49s full heavy host span, the hot loop is substantial but not the sole30FPS limiter. Its transform alone is ~6% of host time even if completely eliminated. Probe removed and clean executable rebuilt. Instrumented13.083 interval rate is not FPS evidence.

## 2026-09-26 VU whole-loop memory proof hoist (072), rejected flat

Candidatea6892685/90f4132d/evidence09d9f1d4:12.91580830FPS; exact-restored clean d97629a6/6544881e/evidence16e7f98d:12.58608357FPS; same candidate rebuilt8fedbf88/48431931/evidenceea0c0b92:12.60928043FPS. All864 original writers and retained frame/EE/GS/VRAM/VU/normalized IOP captures match.132 Python, runtime/translation,262144 arithmetic and81920 compiled CFG/fault comparisons pass. Reversal closes a nonrepeatable initial lead. No optimization retained; source/generated code/executable restored clean with post-link provenance. HG-FAIL-049.

## 2026-09-26 batch GS register copy trial (066), rejected flat

Candidate987a0ebc, verifier6dc0554d, evidence149b938182694bddbb122f271b625349:13.01247463FPS. Exact-restored control545047b7, verifier29b29d02, evidencef585927bb88b48bab0bab7d6182afd7e:12.99300327FPS.80 differential state/fault cases and runtime suite passed; all relevant retained captures and864 writers match. Earlier13.49206 is a drifted nonadjacent control. No meaningful improvement, both source/test changes removed and candidate archived; HG-FAIL-045.

## Final clean qualification - 2026-09-26

Restored game7dd58858... /a03d48e7 qualifies13.49205992FPS with all retained captures and864 original boundaries exact. Thus06313.49534 is effectively flat; no speedup retained.06510.68711 remains rejected. Current code/test products are restored and no experimental worker/probe remains. Runtimec2b21c61 and VU25491fbc pass after final rebuild4d1c44ff.30FPS unmet; Windows evidence only.

## 2026-09-26 - prepared coefficients and coarse graphics worker

063 predecoded invariant coefficient signs/exponents/mantissas once per proved static loop.132 Python tests,262144 arithmetic groups including reuse/fallback and81920 full CFG/fault comparisons passed. Qualified20669097 /97e86cd4 measured13.49533736FPS. Restored37d4df70 controls310a593b and7baa379a measured13.16456363 and13.31100043; prior retained samples13.39..13.42 overlap the candidate range. Archive without a repeatable material-gain claim; all source/test/emitter changes restored.

064 count-only census:60 heavy nonempty VIF drains,14,457,736 pump iterations,85,680,900 VU issue cycles,635,882 GS draws and49,564 CLUT loads.065 used one persistent VIF/VU producer and main GS/GL consumer with bounded ordered immutable packets. Possible guest fault/event/CLUT/transfer packets joined synchronously. Candidate5e5a35f5 /849cdad4 qualified exact retained captures and864 original frame events but only10.68710992FPS.3072 classifier cases and80 full ordered DMA/VU/event/partial-packet fault tests passed. Explanation census:95,409 safe packets versus56,877 joins,17,694,308 transfers; synchronous packets contained6,739,200 IMAGE transfers,52,144 CLUT writes,0 event writes,10,980 transfer-control/HWREG writes. Instrumented replay is not FPS evidence. Reject this architecture as implemented, restore sources/remove new headers and hooks. Final clean rebuildfe5f7a2f ->7dd58858; qualification pending.30FPS is unmet; no visuals or guest clocks changed.

## Retained exact VU transform fusion - 2026-09-25

Final restored executableb4418029... qualifies at12.52048FPS (91d45e3d, outlier), then13.38861FPS (c6b9bd9d, identical binary) with all captures exact. Record this variability alongside the earlier reversal rather than promising a fixed13.4FPS. Current practical speed remains far below30FPS.

HG-DIAG-061 replaces two full-lane four-stage transforms inside the guarded original program1 loop with an exact integer helper that exports only live VF/ACC/MAC/current and all sticky flags. All lower operations, Q/P/readiness, bounds and fallback faults remain. No cache, intermediate stage array, renderer approximation or guest-clock changes. 262144 scalar/packed/reference arithmetic comparisons across16MXCSR modes and81920 full CFG/fault comparisons (25265 fast iterations),132 Python, runtime and translation tests pass.

Qualified candidate8ddd14b7... gives13.39465107 and13.40776131FPS. Adjacent disabled-path control e2c7448c... gives12.95554951FPS; re-enable/rebuild b12a31bc... gives13.42152768FPS. All864 original frame writers and retained frame/EE/GS/VU/metadata/same-date RTC-normalized IOP captures match. Retain the modest ~3.5% adjacent-control gain; earlier clean13.2027 illustrates host variance and30FPS remains unmet. Jobs ce92cef5/77731c37/b27da501/4c64b302, evidence db09b6ae/d8173ebd/84f7e924/f6c78f5f. Source/proof/backups in PROGRESS and DIAGNOSTICS.

HG-DIAG-062 follow-up packed both independent chains into eight32-bit lanes for addition, reducing six add4 operations to three add8 operations while retaining product rounding, flags and outputs. Full tests/captures remained exact, but qualified cf7ace95... /482171bd... /9f5f81c3... measured13.28229979FPS against adjacent06113.42153. Reject; fpu_add4_avx2.cpp restored to8d81eb8e via b93e7eae backup, rejected bytes archivedc535876a. Wider arithmetic alone did not improve this workload.

Heavy-only profiling correction: HG-DIAG-057 finds339 dirty CPU raster scopes/1340 pages in31M..33M;3111/75809 are whole-replay totals. HG-DIAG-058 heavy readbacks154.0117ms plus backend flush154.7594ms are secondary to the slow window. HG-DIAG-060 all-slice phase attribution puts3430.6ms in VIF/GIF,495.2ms EE, each other phase<=187.3ms, with substantial timer overhead. Nested measurements must not be added; diagnostic timings are not qualified FPS. All057..060 probes were removed byte-exact.

## Historical renderer measurements - 2026-09-24

Retained renderer includes native STQ, bulk page coherence, CPU same-pixel PSMT8H feedback, GPU alpha/AFAIL and PSMZ24 sprites, and CT32-special area threshold2048. Historical qualified13.5356/13.5555FPS; current same-session clean verifier5c135a0a... gives12.97244005FPS with identical retained relaxed frame/VRAM/core state. Host drift matters; historical values are not current controls.30FPS remains unmet.

Rejected state-correct candidates: CT32 shader specialization12.92153827FPS (6b9325c7...), independent sprite waves12.91191705 (11e3ac0e...), deferred empty-triangle STQ setup12.92042525 (bee97f13...). Waves really reduce52398 dispatches to22390, but do not reduce total time materially.1177 GPU/CPU cases per mode and runtime tests passed; eight added queued-sprite dependency cases remain. Production sources restored exactly gs.cpp050855fa..., gl_gs.cppf40ac711..., runner12d9ef7c.... No performance gain claimed for these experiments.

HG-DIAG-048 heavy31M..33M replay04f386d7... (project-link-runtime/20260924T033513Z-66f924fa35ff441789c43b3d69230517) measures240 triangle-list calls14.2906ms,601922 strips265.3297ms,33900 sprites163.5473ms, total443.1676ms inclusive primitive dispatch. Excludes per-draw environment setup, batch-final flushes and unrelated VIF/VU/GIF work; not total GPU time or a strict renderer speedup ceiling. Probe removed exactly. Whole-run sampled phase counters miss infrequent expensive bursts and cannot replace heavy scoped evidence.

Compact VU SQ operand capture remains inconclusive: candidate12.6677/12.7920, restored12.3700/12.5842, earlier control13.5537. Full state/fault comparisons passed, machine-code stack shrank0x8f8->0x208, but host drift prevents a reliable material improvement claim. Archived rather than labeled an inherent regression; original emitter restored.

## Prior measurement - post-audit synchronous VIF/VU baseline 11.428332 FPS, 2026-09-23

Fine-grained VIF/VU host-thread overlap is closed. State-correct variants measured8.746445FPS (full snapshot),8.584612FPS (dirty VU/VIF tracking) and8.749468FPS (VIF-only merge), while atomic spin/yield feeder was much worse in whole-run time. Actual HG VIF1 double-buffer use is real, but measured completed UNPACK work is too small and ~126180 host handoffs in the final2M region dominate any overlap benefit. Production is restored to synchronous VIF/VU; independently proven split-tag/TTE phase correctness remains.

Fresh restored build2739b191e08e472cacea9b2ea60a9c07 -> game203820c84068e970f509c54215d93f09ca5e98d5825b1b24744305259881b402,36319744bytes,149-input manifesta08a85cc2772eb41349433074bfbaeeedf5ba4289ece28d81f30859d50bf925a. Replay233a96bca99e494b8a6eeae64f73afe2 reaches33M/no fault with48195sprites/305856triangles/842masked batches in24.5939s. Final full-run profile returns ee_vif_gif to~2.99M sampled ns instead of~44-50M under overlap experiments.

Fixed verifier98f8f5f6df344edd84628dfd5ff2e7a6/evidenceproject-link-verifier/90a9667c4b734842b398184ad73865f6 passes149-input provenance,864 original1bef84/caller2d1c50 events, zero trace/caller/stack failures, and exact frame/EE/GS/VU/current-date normalized IOP hashes. Qualified heavy60/5.25011s=11.4283319778FPS; windows11.4172629015/11.4394225380. This is the current production baseline.30FPS remains unmet.

## PRIOR MEASUREMENT - scheduled VU clock rejected; restored same-date control 10.079036 FPS, 2026-09-22

Root-cause audit candidate moved~92% of static VU pair clock mutations into a function-level deferred scheduler:3194 scheduled neutral advances,283 residual exact advance_pipeline(1) calls. Correctness was strong:132 Python tests,65536 compiled VU readiness/memory comparisons including uint64 wrap, runtime/translation suites, replay,149-input provenance,864 original writer events and exact frame/EE/GS/VU state all passed. Candidate f723b1dbac484c8ba14f826c65ea94d5/evidenceabbddece8a274a3085e0fcab19610a52 measured60/6.23549s=9.62233922274FPS (9.540528/9.705565).

All scheduler/header/emitter/test changes were restored to exact production hashes and translated.cpp returned to clean6979f9a8.... Restored game0775404e1c7d96b522383344f993e4feaa274f7bcd19cd9b2bb3f6f4ea02d870,36318208bytes,149-input manifest823f27c40afe07877348b59a27adbb43d95ee335b12915ca376d38c998b4ff5d. Replaye06a862069644fd5b24d2d0a45660f50 reaches33M/no fault with48195sprites/305856triangles/842masked batches. Fixed control246279beabfc4655a23e0000e7490214/evidencec97d79d6082c4db79931963e531ec4ba measures60/5.95295s=10.0790364441FPS (10.008374/10.150704), ~4.75% higher FPS /4.53% lower heavy host time than candidate. Candidate and control both occur after UTC date rollover and share normalized IOP hashb29aec44..., eliminating the prior RTC-date ambiguity. Reject scheduler as HG-FAIL-040. Candidate executable was~348KiB larger, so code-size/I-cache pressure is plausible but not isolated; do not repeat unchanged.

Architecture audit remains active: VIF1/VU1 double-buffer serialization is independently proven by the game command stream and remains the next target.30FPS unmet.

## PRIOR MEASUREMENT - compact VU MUL and selective IMAGE paths rejected; exact production control 11.173892 FPS, 2026-09-22

Two bounded follow-ups were closed without retained production changes.

Compact broadcast-MUL specialization kept only Mask/Broadcast static and register indices dynamic. Its helper passed786432 exact complete-state comparisons /427520 matching faults across16 MXCSR modes in addition to the full-static fixture, linked only four mask14 variants, and kept the executable at36318208bytes. Candidate58c00d53e11e42bd9c50754773b9ec1e/evidenceproject-link-verifier/1cb368a762b343f7a25f1b731969a090 measured60/5.65587s=10.6084475067FPS. Exact gameplay-route reversal3924b77052274163b7954aab25a3cbec/evidenceproject-link-verifier/129dc91fe6d247e4a73bb88a3065a620 measured60/5.59952s=10.7152041604FPS. An immediately earlier original-route sample was11.1457868926FPS, so the latest ~1% difference lies inside observed host drift; there is no demonstrated gain. Keep original multiply_vector route. Test-only compact/full-static helper fixtures remain. HG-FAIL-036.

Selective single-tag EOP IMAGE/IMAGE2 direct apply bypassed GifTransfer materialization only when a complete GIF packet was exactly one EOP IMAGE tag; mixed/register/multi-tag packets remained the original decode/apply path. Runtime regression5825 exact checkpoints, including zero-loop and multi-tag fallback plus capacity/fault/pending/partial-GS cases, passed. Candidate0d8ed3b943214a6f89fc65d1ca61bb41/evidenceproject-link-verifier/1cb357d1760c4081834468c6c29520c7 measured60/5.3436s=11.2283853582FPS, windows11.2396174/11.2171757. Exact whole-file gs.cpp reversal9839e36d0911452f9d2ba88d9ab5732f/evidenceproject-link-verifier/22b5420e942249108c0541f93581d1eb measured60/5.36966s=11.1738918293FPS, windows11.2405860/11.1079844. The ~0.49% host-time difference and overlapping intervals do not establish a repeatable gain. Production runtime/gs.cpp is exact pre-candidate e03185c7...; strengthened test fixture remains. HG-FAIL-037.

Both GIF candidate/control and all VU comparisons pass149-input freshness,864 original1bef84/caller2d1c50 events,zero lost/unexpected/caller/stack failures, expected frame/EE/GS/VU and same-date RTC-only normalized IOPbdbf3a11... hashes. Current production game after exact GIF reversal is ade19962517bf3278ef4fa32ee2aed31cbe6c7b758d4cfd9671d8d4ec22f6caa,36318208bytes, manifestf6d93ac79f1d608ef92561d107b8b8f3fd31803c684ba33d52f0cbedbfc8fdd8. Replaycd08b9daa09444c991b419ae3bf8b023 reaches33M/no fault with48195sprites/305856triangles/842masked batches.30FPS remains unmet.

## PRIOR MEASUREMENT - full-static broadcast VU MUL rejected; original route 11.145787 FPS, 2026-09-22

A current-coverage A/B finally isolates the long-pending full static-operand broadcast MUL route. Candidate generated masks14/15 as hg::vu_aot_multiply<Destination,Source,Other,Mask,Broadcast>; fresh qualified candidate5fddf7c20c19439e9ffde1c1bce41f77/evidencec45f3d39ed084ea2a4a1092da38193ae measured60/6.34941s=9.44969690097FPS. Exact control changed only vu_emit routing back to existing Vu1State::multiply_vector and removed vu_specialized.hpp from generated-main includes; helper/fixtures remain. Control3be4caa8540541c0b6b360561cc2a476/evidencecf0b4a45fa2945988fe573d3f1086a11 measured60/5.3832s=11.1457868926FPS, windows11.1431373132/11.1484377323. Heavy host time falls17.95%. This is large and the control windows agree closely, so reject the full-static route.

Both candidate/control use identical current gameplay coverage configd2c998fa... and protected host9704ed0b.... Both pass149-input provenance,864 original1bef84/caller2d1c50 events,zero lost/unexpected/caller/stack failures, and expected frame/EE RAM/GS VRAM/state/VU/same-date RTC-only normalized IOPbdbf3a11... hashes. Candidate game07a17eab... was36624896bytes; control gameeb3704710d8cadcdcb1166247f4fe931668244f2caeedb006dcbd257b062a35d is36318208bytes (~306688bytes smaller). Code size is a plausible contributor but not established as the sole microarchitectural cause. Control generated main6979f9a8... has no vu_aot_multiply calls or vu_specialized include; current map likewise has no helper symbol. Control replayc860f16aa... reaches33M/no fault with original48195sprite/305856triangle/842masked batches. All Python/runtime/translation/readiness suites pass.

Decision: keep original multiply_vector gameplay route. Retain vu_specialized.hpp and vu_specialization_regression.hpp only as reproducibility fixtures. Do not repeat full Destination/Source/Other/Mask/Broadcast templating unchanged. This result does NOT rule out a compact eight-variant <Mask,Broadcast>-only helper with dynamic register indices; that is the next bounded candidate.30FPS remains unmet.

## PRIOR MEASUREMENT - bounded staircase object coverage, 9.142119 FPS; throughput unresolved, 2026-09-22

Config-only coverage of eight missing methods in independently bounded original table46dfc0..46e000 adds50 original words to shards0010/0015. All earlier roots and protected F1/Tab/event-redraw host9704ed0b... remain; no optimization, audio, renderer, runtime or guest-clock change. Build0c9fb87d155648cfb1cfd66294056f79 succeeds28.10s; executable07a17eabed38b187659677758cf5b45da34a4280fd28b110593ba2ca60cebcaa,36624896bytes, manifest8245e6f484d697311f28a902ecff3bedeb480d3156940313020aeab2774bc0e7.

Fixed verifier5d147ab1ec2147d79fd21e9d1a87d69f/evidenceproject-link-verifier/846ae4895bcf45548703b8bf170e2bfb matches149 inputs,864 original1bef84/caller2d1c50 events with zero lost/unexpected/caller/stack failures, expected image/EE RAM/GS VRAM/state/VU and same-date RTC-only normalized IOPbdbf3a11... hashes. Heavy60 boundaries/6.56303host seconds=9.14211880793FPS; intervals9.34489192632/8.94795867236. Prior repaired8.664585 and previous fresh control9.209375 remain below. This single higher sample does not establish a gain, stable performance or resolve the earlier throughput reduction.30FPS remains unmet; pending VU optimization is still unaccepted.

Recorded33M replay198e3186e66a4f7da07ab94cafeb7904/evidenceproject-link-runtime/20260922T073232Z-4aa418a79a794877a1607d46c12d9a80 reaches normal budget/no fault with48195sprite/305856triangle/842masked batches. Python7c4c3623... passes130; runtime86f9d1b5... passes inherited regressions. None of these tests traverses the user's later staircase route. No additional audio test or audio fix; latest manual session recorded394 underruns before fault2ac570. Do not present12-19 title updates/s or the lighter menu rate as qualified heavy-gameplay FPS.

## PRIOR MEASUREMENT - 2ac540 required coverage, 8.664585FPS; lower throughput unresolved, 2026-09-22

Only two original callsites209198/2091c4 are rooted to2ac540; exactly3 original words added in EE shard0015. No runtime, host, audio, clock or optimization mechanism changed. F1/Tab/redraw host9704ed0b... retained. The initial repaired measurement was below prior10.790105, so a bounded candidate/control/candidate sequence was completed rather than dismissing that reduction:

| Build | Qualified FPS | Heavy host seconds / 60 boundaries | Verifier / evidence suffix |
| --- | --- | --- | --- |
| First repaired6f77e526... | 9.1950640896 | 6.52524 | 938e39bd3b934063a27f8cb03697f371 / 88065449e41d4240968c00b7eeef9ff1 |
| Fresh pre-repair control94a92457... | 9.2093751439 | 6.51510 | 89cc022d32d3444384008596db356633 / 0276bf55827f4719adbe4fcb9c6c722e |
| Exact repair reapplied282e3ff7... | 8.6645852407 | 6.92474 | d2b957fbfc7a4593869479fbaaf54b64 / 4c2ca60a9ce94fc2bf44270eebe37d7e |

All evidence directories are under project-link-verifier. Each passes149-input freshness,864 qualified original1bef84/caller2d1c50 events with zero trace/writer/caller/stack failures, and expected frame/EE RAM/GS VRAM/state/VU/same-date RTC-only normalized IOPbdbf3a11... hashes. The first and last candidate input digests are identical f7a0ac9462dc362b22d3b34757c408118e5337f36852aafa61b7cb4b5f5c175a. The fresh control input digestf0ce5945... equals the previous control's; generated original sharde6b83624... was restored exactly, and reapplication restores exact candidate shard05bdfcbb.... The low9.20 result is also present without this repair. However, the last repaired8.664585 is lower still; this sequence does NOT establish speed neutrality or identify the underlying host/compiler-layout cause. Do not select only the favorable earlier sample or claim improved performance. Retain the original required function coverage because otherwise the game explicitly stops; speed investigation remains open.30FPS/audio remain unmet.

Final rebuild1e88a046d502432ea6fdd6cc90a55c5c: game282e3ff7f51d4d8e6b16ec6a1cbec07f55bfcb537223802f3fcc6a35173380b5,36621824bytes; manifest77e5c9ca316c5f9aa5e627e7f0722025416a7b3d76037a9b66e8d9eb16619185. Current config81772f41... contains the repair, not the control. Original config backup37ee07c5..., candidate restore backup7acc8b8a..., and final emitter87a02329... retain precise rollback evidence. Python130/runtime suites and original33M replayddeb5ed9... pass; standard33M does not exercise the actual stair landing. Do not run the known-faulting control for manual gameplay. No new audio test or audio-code change.

## PRIOR MEASUREMENT - second staircase coverage repair10.790105FPS, 2026-09-22

Config-only2032bc->2ac5d0 and100b6c->2ac600 add exactly36 original words in shard0015; protected F1/Tab/redraw host9704ed0b..., original runtime/clock/renderer/audio and pending VU candidate unchanged. Build4a077e3187c549938b34901423cff715 succeeds12.40s, executable1fe0fcdb73b9c78ff081941fdb6663e08826e1d7129e28d241c02e427e50957d,36621312bytes. Verifiera2251c6a7d244c868b0754403f3e7273/evidenceproject-link-verifier/8ab87396dc624f089ce3fe7cfbf20612 passes149-input provenance67a1a896...,864 original1bef84/caller2d1c50 events,zero lost/unexpected/caller/stack failures, and established frame/EE/GS/VU/same-date RTC-only normalized IOPbdbf3a11... hashes. Latest heavy60/5.56065s=10.7901054733FPS; intervals10.7917550991/10.7884563517. Prior10.2973355644 retained below; one higher sample is NOT a dependable gain, and no performance mechanism was added.30FPS unmet.

Replay7b66b76acb8d4c69a8d521a9c407bc24/evidenceproject-link-runtime/20260922T065401Z-e0c478ab989148e0ad031101f5c59b7b completes33M/no fault with48195sprites,305856triangles,842masked batches. Runtime276d8128... and130Pythond2336bbb... pass; this does not exercise the user's later100M staircase path. No new audio test/fix. Latest manual stop2ac5d0 differs from old2ac530 and recorded450 speaker underruns; live title/video counts are not the qualified heavy metric. Preserve both new roots on any subsequent VU-only reversal.

## PRIOR MEASUREMENT - menu-close coverage repair10.297336FPS, 2026-09-22

Only config roots3977d0/3974a0 at100b6c were added; original bodies contribute384 newly compiled words in one EE shard. Host9704ed0b... (F1/Tab and event-redraw fix), renderer/runner/parser/audio/clocks and pending VU candidate unchanged. Build58e9b25b02d144d2aecebc14173ff143 produces36619264-byte game2094e2cd8e2bdd4f0bcef49b3d14ca90ba6db939c166b30ffe25d61e8571d3d9. Fixed verifierfa260aa4865b4fcf9bfde0f36103b3fe/evidenceproject-link-verifier/c9c581a6755143e0a1721c90cb7a1711 matches149-input provenance,864 qualified original1bef84/caller2d1c50 writers, zero lost/unexpected/caller/stack failures and all expected image/EE/GS/VU/same-date RTC-only normalized IOPbdbf3a11... hashes.

Heavy60/5.82675host seconds=10.2973355644FPS; intervals10.5541268395/10.0527433937. Previous corresponding build10.1128254223FPS is retained below; one higher observation does not establish a speed gain or resolve prior drift.30FPS remains unmet. Preserve this required menu-close coverage on BOTH sides of any later VU-only comparison. Recorded replay84cb026f47b64049b5df653ca169c90d/evidence20260922T063956Z-7212e4332b2e488c806f6bcf67d2fb05 completes33M/no faults and original48195sprite/305856triangle/842masked batches. This standard replay does not exercise the user's later menu-close input. No audio-code change or new audio claim.

## PRIOR MEASUREMENT - Select-menu coverage build10.112825FPS; no speed gain, 2026-09-22

Current game0972a30eb23046931ee3af8612b08ac43b946b6774c67202e7b8e104adb3907c,36596736bytes, built e31e3fa32c56458389fe4b641a4daf7b after config-only addition of observed member396900 and three original-installed successor callbacks394e40/395630/394670. F1/Tab/event-redraw host9704ed0b..., runnerc8c2d536..., parser and pending VU mechanism are unchanged. No new runtime performance mechanism or guest-clock/input/render/fault change. Original proof and byte backups are in SOURCES/PROGRESS.

Fixed verifier53369591d83c4a2eb1b46750e3e6f0d7/evidenceproject-link-verifier/4e163caf6201471dbbad67858e69df7e:149-input provenance manifest8b2e7991...,864 original1bef84/caller2d1c50 writes,zero lost/unexpected/caller/stack failures. Established frame/EE RAM/GS VRAM/state/VU and SAME-DATE RTC-only normalized IOPbdbf3a11... hashes all match. Final two heavy intervals60/5.93306host seconds=10.1128254223FPS (10.3442568686 and9.8915229648).30FPS remains unmet. The latest pre-change F1/Tab buildca4f82b4... had qualified10.3913197842FPS,60/5.77405s (bb2d8648113a4439907525efe65f6a6b/evidence7cc7b5d8f5dd40158a2a92011892e09f), not the older9.917 sample that remained at this document's top. Current result is lower; no improvement or stable-equivalence claim, and these individual runs do not isolate a causal slowdown. Preserve the newly required menu roots on future VU-only comparisons.

Unpaced replay0d00aca090a549a98612a91441139e49/evidence20260922T061800Z-b48139cce1724b8e99e54ac81f1535fb completes33M/no faults and retains original48195sprite/305856triangle/842masked batches. Its whole-run/presentation counts are not FPS. Separate raw-audio/speakers replay929ee941048344b6860a0667be7a53b9 records73 queue underruns while preserving expected frame/EE/GS/VU hashes; it is audio diagnostic evidence, not a qualified speed sample. Manual user reported12-19 video updates/s; keep that observation separate. The standard replay does not exercise Select, so actual repaired-menu operation still requires live retest.

## PRIOR MEASUREMENT - unpause coverage build9.917273FPS; lower sample unresolved, 2026-09-22

Only verified direct-member root2f55c0 was appended at100b6c; hg_emit6dadd150... changed only generated shard0017 (248 original words), leaving generated main/VU candidate and runtime/host unchanged. Buildd7faa74ab14248888889c2f48e1baf90 produces game732a2a94f17f96ae67db3f392525e9aa28e5b910772f0995bdfcf034e030972a,36343808bytes. Verifierdf1e2499045a418f816024b12b539a81/evidenceproject-link-verifier/e44d4071930a4b8e889bc771f75f47c7 qualifies148-input provenance,864 original writer/caller events, no trace/caller/stack failures, all expected image/EE/GS/VU and same-date normalized IOPbdbf3a11... hashes.60 heavy boundaries/6.05005host seconds=9.91727341096FPS; windows9.85030815047/9.98515540230.30FPS unmet.

This sample is lower than prior10.8529319196, and the difference is NOT dismissed or claimed as unchanged performance. The callback is required coverage; no time-saving mechanism was added, and one observation does not isolate code-layout versus host-condition effects. Attempted identical-build repeat request hg-unpause-qualify-repeat-20260922-a1 was blocked by the platform before a job existed. No retry/substitute measurement was attempted; recent jobs confirm only the completed measurement above. Retain both results and investigate with an authorized comparable control/repeat later, preserving2ac530/2f55c0 on any VU trial rollback. Do not promote pending VU specialization as a gain. Recorded33M replay7d91d216... completes without faults with normal original draw counts; it does not exercise later manual unpause and its whole-run timing is not FPS.

## PRIOR MEASUREMENT - pending VU candidate plus static coverage repair10.852932FPS, 2026-09-22

Verifier50aa2939d58d44bd80963af41120ca32, evidenceproject-link-verifier/05f3470af08d41b6b0a63254e308bd3c:60 original heavy boundaries/5.52846 host seconds=10.8529319196FPS, windows10.6807177442 and11.0307906135.148-input post-link provenance matches executable0dd2b06c44841d796dee87faff8b84d57a90b980d5e3b679964182eb283c0900.864 original1bef84/caller2d1c50 events qualify with no lost trace/unexpected writer/caller/stack failure; established frame/EE RAM/GS VRAM/state/VU and same-date RTC-only normalized IOPbdbf3a11... hashes match.30FPS remains unmet.

This is the first qualified result available here for the pending static-operand VU multiply route, now including only the independently proven209574 ->2ac530 static-target coverage repair after manual gameplay hit an untranslated function. No fresh route-only control has been measured; prior original11.114260151FPS is not proof of a gain or an isolated causal regression. Do not promote this candidate. Later A/B must keep the new target on both sides and regenerate from current config. Manual staircase route and speaker output are separate live checks, not qualified FPS. Recorded33M replaya096e457... preserved normal work and completed without faults; its whole-run time is not gameplay FPS.

## PRIOR ORIGINAL CONTROL - /Ob3 trial rejected; restored control11.114260FPS, 2026-09-21

Only per-source automatic /Ob3 was added to the main AOT VU unit's existing/O2 setting, leaving/O1 large EE shards/runtime/host/emitter/original arithmetic unchanged. Generated project and actual compiler override confirm the setting. Candidate built106.35s, grew executable5120bytes, and passed33M recorded replay669ad988... with all expected original work/image/EE/GS/VU. Qualified candidate04f39bd225e740a7bbf4e5130a205cc3, evidence6a179db53b724cbcab73d34af82e0b94:60 boundaries/6.5205s=9.201748332FPS. Exact original CMake2b9e0b6b... restored and rebuilt184f60eec8944ddf830866cc2d3a4a32; control34348808501e43729ed7f2380f37fda0/evidencebff1d79d44d0454e870c25316cd62076:60/5.39847s=11.114260151FPS. Both147-input manifests,864-writer qualifications and all established image/EE/GS/VU/normalized IOPbdbf3a11... match without trace/caller/fault failures.30FPS remains unmet.

This paired slowdown reverses when the candidate is removed; retain exact original/O2, not /Ob3. The pair does not identify register-spill/cache/inlining cause or prove universal inferiority. Compiler-only setting differs from forced-inlining failure007; selective /GL+/LTCG was already separately rejected017 and is not an untested next candidate. See HG-FAIL-034. Current rebuilt source-matched executable2dc877908c68de315501fa3fbda271ff89e6be22868a0db98bd3d9056e233f1e,36022272bytes; manifest56562a9647a3c46dac350165066290a7e2801a2be6c2e3ab89302f00f51c7f59. Candidate CMake backupfa1333528bde47f7a2b369a33aa1841f. Protected host1a122b9f..., original runnerc8c2d536... and parser5ab22f5b... unchanged.

HG-DIAG-037 native-worker sampling before this trial is fully removed and recorded in DIAGNOSTICS/HG-LEARN-041.330 samples/290 native show dispersed VU arithmetic/generated bodies and GS work;40 outside-image samples lack caller attribution. No per-function percentage or FPS is inferred. Original state/image match; an offline pinned full-IOP comparison finds only bfcd1/bfcd2 differences. Runtime and temporary comparison fixture were restored exactly and clean game rebuilt before this compiler trial.

## PRIOR - incomplete GIF-payload batching not retained; exact control10.861439FPS, 2026-09-21

All runs use fixed gameplay33m/frames original0x001bef84/caller002d1c50, final two heavy windows,147-input matched post-link provenance,864 qualified writer events and unchanged image/EE/GS/VU/same-date normalized IOP bdbf3a11... hashes. No lost trace, unexpected writers, caller mismatches or invalid stacks. Protected event/refresh host1a122b9f... and original runnerc8c2d536... never changed.30FPS remains unmet.

| Variant / verifier evidence directory | Heavy boundaries | Host seconds | Qualified FPS |
|---|---:|---:|---:|
| Candidate fa3283542166432f8058a0e932dcd847 |60|5.44413|11.021044685|
| Exact original f048f775d8754a55b7c49772e64335c2 |60|5.50208|10.904966849|
| Candidate4f708348e1c243b39043e861a449f87b |60|5.49867|10.911729564|
| Exact original1b05c22d19ff484581c6d713951f95f5 |60|5.52413|10.861438815|

The respective1.053%/0.461% host-time reductions are small relative to observed drift; second pair's per-window directions disagree. Four runs do not establish a dependable benefit. Restore exact original runtime/vif.cpp SHA5ab22f5b... rather than retain a more complex path on that evidence. Final game build5a3e4885cc644e5f91108a80c8492b84 and verifier21cf3aba83b442e8b083b61788c5e2b8 confirm source/current exe37f7d62d3dc9acfa7aeed8009f97c3853cadc287c16c1e2bacd35e51787f6cdc match;36022272 bytes. Tests936 serial cases/7873 checkpoints retained. This is a performance non-retention decision, not a discovered semantic mismatch. See HG-FAIL-033.

Candidate was really compiled: process_pending VA1421c5b40 from hg_vif:vif.obj;416 native bytes at fileoffset35412829 SHAf96a28c947e6cf76e2c96311c1f16a002e1876c0bc42bc5733e089a18c42dfd9 show original submit followed by inlined payload/tag/capacity guards and vector range insert. Initial candidate exe68072820..., repeated candidate3b5b590c... have identical147 input hashes. Replay20260922T042721Z-f34ee87e87814c02ba5125c0d82802a7 reaches33M with exact expected640x448 image,48195 sprites/305856 triangles/842 masked batches, no runtime faults. Candidate source archived in automatic backups bffa0a620e0a4400be7203efcf2025c3/ab4f8269661946a0bcf6ce570345470e. No production probe or incomplete-payload helper retained.

## PRIOR DIRECT TRANSPORT TRIALS - no new runtime optimization retained, 2026-09-21

The retained event/refresh host1a122b9f... is unchanged. Exact original outlined-parser control1945b31d1f4d440393e22906ae0a9d37 measured11.020640FPS and original-loop control7eae15ea84ae4c94ada58c1e73808fef later11.125079FPS. Those are newer observations than the historical11.401 result below; changing host conditions cause noticeable spread, so no isolated lower/higher sample proves a code regression or gain. All qualified comparisons retain original0x001bef84/caller002d1c50,864 events, source/binary provenance and established frame/EE/GS/VU/same-date normalized-IOP hashes.

Complete-EOP DIRECT batching: candidate80d93dc088d54b0486e8496abe29f38b=10.983700FPS and7f89e3b7d40447a0aa3ab7940c142c3d=10.638468 versus exact original11.020640. A flag-disabled helper control drifted7.564354/8.917942 and was not the original native placement; it cannot justify a speed claim. Source restored exactly5ab22f5b...;791-case/5124-checkpoint fixture retained. HG-DIAG-035 shows only564374/7306814 heavy DIRECT qwords (7.724%) covered, all PACKED first-tags and none IMAGE, despite57906 eligible packets. Evidence20260922T022141Z-10b6ddd9ec4a48c7b6544089db0ae5bd; HG-FAIL-031.

Inert incomplete-DIRECT DMA batching: unfiltered candidate7c07aef8...10.943853FPS, controleccd2a496...10.273726, candidatee9e0f1d0...11.237597, control7eae15ea...11.125079. Second pair only1% less host time; first6% pair depends on a slower control. Prefiltered calleree470df3566642e6ac6ea86ca68a2130=10.956803FPS did not establish a gain.2338 exact state/fault cases and all original hashes passed. HG-DIAG-036 counted25986 accepted batches/7151582buffered qwords/7306154calls, proving real coverage but not a reliable speedup. Runner restored to c8c2d536..., helper compiled only into runtime_tests, not production hg_vif. All probes removed. See HG-FAIL-032 and artifacts/vif-incomplete-dma-preedit.txt/vif-inert-prefilter-preedit.txt for scope/backups.

## HISTORICAL -11.401187FPS; guarded arithmetic route disabled, 2026-09-21

Fixed qualified candidate9e7d985c63b6448c8312d3d912fa8039 measured60 original heavy boundaries/5.26853s=11.388375885FPS, executablebe497bc0.... Subsequent control47eac334b33a4a2b9f0a5b3acd8fce84 with only use_embedded_rounding_madd=false measured60/5.26261s=11.401186864FPS, windows11.38766/11.41474, executable78a0c91d159da2b8472688a93594298b144a9b7c21c1c5c600b62a6aff094dcd. Both pass143-input post-link provenance,864 original writer events and all established frame/EE/GS/VU/normalized-IOP hashes. Roughly0.11% difference is inconclusive, not a repeatable gain or established regression. New helper and direct fixtures remain, but its gameplay route stays disabled. Direct tests previously passed2113152 vectors/8452608 scalar lanes across16 MXCSR modes, boundary guards, atomic rejection and exact flags. No correctness mismatch was observed within that coverage.

The host redraw fix is unchanged SHA1a122b9f... and remains retained; current performance is11.4FPS, not the historical4FPS condition. Game build14d202aae83044fdb10560e15b3672f2 is current and source-matched. This update changes neither the verifier metric nor original guest input/clocks/renderer/error checks. See HG-FAIL-029 and artifacts/madd-er-qualified-control-20260921-update.txt. All future rollback operations must preserve the proven event/refresh host.

## RETAINED BASELINE - host redraw fix recovers11.3-11.4FPS; GIF word append inconclusive, 2026-09-21

Event/refresh-aware runtime/game_host.cpp (SHA1a122b9f3c1ed5521f6270d2189171e0966c18db868c0c46b04b7af2bb408436) reversibly recovers performance without changing guest work: old4.082260FPS ->candidate11.326161 ->old4.058046 ->candidate11.370032. Exact input/image/EE/GS/VU/normalized-IOP hashes and864 original-writer qualifications match all four runs. Fresh-frame, refresh/size-change/initial redraws remain; only duplicate clear/draw/swaps are omitted, with bounded idle event waits and unchanged input delivery. HG-DIAG-031 measured6235 duplicate swaps of7097. All128 Python/native/translation/65536 readiness regressions pass. Controlled smoke also confirms original NO->YES selection and CAPCOM transition from delivered left/cross, but all-white PrintWindow capture cannot certify physical on-screen/resize behavior. See HG-LEARN-040 and PROGRESS for evidence.

A following GIF serialization-only candidate replaces sixteen scalar byte stores with two endian-guarded8-byte memcpy stores. Native bytes prove the intended simplification;4608 independent byte-exact growth/unaligned-prefix/reset/EOP/fault tests and5788 existing packet state/fault checkpoints pass. Replay20260921T204123Z-118f31dbdede441ca8dc1952a7776b19 completes with normal rendering counts. Candidate verifier44acbd452024405ba5cc096470452bcb=11.434888FPS (60/5.24710s), restored original transport f7c69da5b3fb4717a648c09ea450424b=11.419045FPS (60/5.25438s). Windows disagree in direction;~0.14% aggregate is not a demonstrated gain. Original gs.cpp is restored, useful byte-serialization regressions remain. This was not the rejected streaming-decoder or buffer-storage mechanism. Current restored-host control exe c8be3a4d88bb539b097e575db1e17f5ff83ec3e79d1f7791fd61fce669ed242f matches142-input manifest and all established state hashes. Do not report historical4FPS below as current.

## HISTORICAL RESTORED BASELINE - qualified4.002887FPS before host fix, 2026-09-21

Game rebuild d607ee921baf4b7ba46948506d297acc produced36,021,248-byte executable7de5e17780c2108fa5dc585318cad33e7a76f3c9c3647c9289eff38757bb85f0. Final verifier job2646d0ea287148be9510a061631582c8/evidence project-link-verifier/9628d990fb19447a8d1ea0c3491d34f7 measured60 original heavy boundaries in14.98918host seconds,4.002887416FPS; windows3.982017/4.023978FPS.142-input freshness manifest matched; isolated executable is byte-identical to build/Release/hg_game.exe. All established image/EE/GS/VU/normalized-IOP hashes and864-writer provenance match.30FPS is not reached. Current source and game binary are synchronized.

Restored runtime/translation/65536-readiness tests pass after final test rebuild;128 Python tests pass. GL debug callbacks/context changes, VU early-return and temporary GL timers are removed; every original fault/error check remains. Only build-freshness verification and documentation are retained, plus an empty unreferenced helper placeholder pending supported deletion. No qualified performance improvement was retained. Same-binary host/GPU/driver conditions are the next unresolved investigation, not an established new compiler regression; see the unchanged archived-control evidence below.

## CURRENT - build freshness repaired; unchanged binary reproduces a host/driver latency shift, 2026-09-21

Retained HG-DIAG-028: CMake post-link manifest hashes first-party native/generated/config/generator sources and build configuration against the linked executable. Fixed frames verification rejects missing/stale/wrong binaries before native launch and pins its checked manifest. Twelve new synthetic cases passed within128 Python tests; real missing-manifest and changed-vif.hpp cases rejected without launching. Bounded native-machine-code inspection separately proved the VU candidate compiled and that the restored control's first640 bytes at vu1_program_1_run exactly match the older clean build. The guard relies on build dependency tracking; it is not an independent compiler-correctness or fresh-generation proof.

Contemporaneous qualified results, all with864 original writer events and established frame/EE/GS/VU/normalized-IOP hashes: VU early-return candidate7f597008... =4.050141FPS (verifier e11b510d1a094cbf8e911e887bdf7c15), restored polling control9492514d... =4.040570FPS (0321d8e9233f4e2a921d3812a72c8bb3), synchronous-debug-error callbackc984fcee... =3.595859FPS (a8086a1ce14a41ebae73903e79a5fa87). The VU shortcut has no demonstrated gain; callback trial regressed. Both are fully restored to original runtime behavior. Independent-context trial also left flush15.1996s/readback3.6567s unchanged and was reverted without a qualified improvement claim. No error detection, guest instructions, rendering, clocks or input were removed.

HG-DIAG-029 GL-only stage timing isolated15.115755s of15.224291s flush time at existing glGetError calls; ownership/CPU clear/upload/dispatch submission groups were small. This locates exposed host wait time, not its underlying cause. The synchronous-callback trial moved/added cost rather than curing it. Both GL stage probes are removed; production retains original error checks. See project-link-runtime/20260921T193358Z-e1b5664fb18a4a06a4837c8e5a20c198 and HG-FAIL-027/028.

Unchanged archived-binary control: old project-link-runtime/20260921T023355Z-e962461879604a5e9776d9d8db125a5b and new20260921T195021Z-5c12f01e72cc4234bae8c012c45afcdd contain exactly the same35,827,712-byte executable SHA2608296bcff3c2a47887f67b2cf233fd41a30eaf9792f50df85f21c1ac3d5eae. Input-events, final frame, full EE RAM and full GS VRAM are byte-identical; work counts and RTX5070/NVIDIA616.92 string match. Nevertheless flush0.9963266s ->15.1869414s and readback1.5088076s ->3.5050519s. This demonstrates that a large latency shift can occur without changing or rebuilding the executable. The exact current host/driver/GPU condition is unresolved; do not infer a specific driver defect, clock limit, load source or instrumentation cause. Hidden replay statistics are not qualified FPS.

Final cleanup/rebuild verification is recorded above this section. Runtime/gl_gs.cpp, runtime/game_host.cpp, vif.hpp and runtime_tests.cpp are restored; empty unreferenced gl_error_state.hpp placeholder remains because connector deletion is unavailable. Rejected helper is archived outside the project. Build freshness infrastructure stays retained. Current qualified throughput is about4FPS under these conditions, not the historical9-9.5FPS. Do not repeat VU/renderer speculation until same-binary runtime conditions are controlled.

## REJECTED - staged bounding fragmented readbacks increased transfer work without gameplay gain, 2026-09-21

Full33M observation project-link-runtime/20260921T103855Z-dbe0e619b99a418ab34b14ee61f1eada found multi-page CPU reads dominating resident GPU materialization:1731 dirty events expanded to3537 glGetBufferSubData ranges,326778 dirty8KiB pages,2676965376bytes and1006640500ns,98.93% of measured readback time. A second unchanged-transfer observation project-link-runtime/20260921T104328Z-311b52eb2a58411eb94a3697ae3b1591 measured351042 first-to-last bounding pages for327338 dirty pages:23704 extra pages, +7.241% volume, with1083 one-range,458 two-range and194 five-to-eight-range events.

A bounded production candidate retained the direct path for contiguous dirty sets; fragmented sets used one bounding glGetBufferSubData into a reusable host staging buffer and copied only actual dirty runs into resident VRAM/uploaded_copy. Clean gap pages were never copied or marked valid, gpu_dirty clearing and existing barriers/order/faults were unchanged. Hidden replay project-link-runtime/20260921T104813Z-6724e7c1730a4ccda7d3d5603d87f622 completed without fault with the same48195 GPU sprites,305856 triangles/1014 batches and63070688 masked words/842 batches, but download volume rose to2880323584bytes and readback time rose to1056517700ns.

Qualified verifier d043105abbb14e42a897f37a3ec532a3 preserved the established frame02723437..., EE RAM8747a80e..., GS VRAMa30ba399..., GS stateeaf454f2..., VU state5adb6544... and normalized IOP01dc90c6... hashes;864 original0x001bef84 writes qualified with no unexpected writers/lost trace/caller mismatch/invalid stack. Final steady windows were3.16779s/3.22969s =9.470325/9.288817FPS, aggregate9.378693FPS. This does not improve on the stronger recent9.554140FPS layout-matched control, and the mechanism's own readback metric regressed. Candidate source was reverted byte-exactly. HG-FAIL-025: do not repeat bounding-span over-read/staging unchanged; next measure synchronization/readback only in31M..33M heavy gameplay before selecting a genuinely different transfer mechanism.

## REJECTED - proved PSMT8H same-pixel feedback specialization regressed qualified gameplay, 2026-09-21

The exact guard proof completed before implementation: gameplay31M..33M had18378/18378 feedback triangles with Q bits identical and Q==1.0, and every vertex had exact4-bit STQ-minus-screen delta dx=dy=8. This complements the prior794236/794236 covered-pixel self-map. The candidate admitted only nearest PSMT8H with TEX0.TBP0==FRAME.FBP, equal texture/frame stride, Q==1, all vertex deltas in[0,15], and a conservative wrap-identity proof. The existing per-pixel triangle list retained draw order and sampled the current destination CT32 word high byte as its CLUT index. All failures stayed CPU.

Build plus runtime/translation/65536 VU-readiness regressions passed. A full33M hidden replay completed without fault and GPU triangle work rose305856 ->328503 (+22647 over the full replay). Candidate fixed verifier b894c58157df40d79cfafcf2ec461843 qualified864 original0x001bef84 writes with no unexpected writers/lost trace/caller mismatches/invalid stacks and measured heavy windows3.15966/3.16915s,9.480455FPS. Frame SHA02723437..., EE RAM8747a80e..., GS VRAMa30ba399..., GS stateeaf454f2..., VU state5adb6544..., and normalized IOP01dc90c6... match the established state.

For a layout-matched control, the guard/shader/backend code remained compiled but only the job admission marker was forced off. Control verifier e093bf9d0518463e8661e387e22aadad measured3.13418/3.14582s,9.554140FPS with the same qualified state hashes. Thus enabling the specialization increases the two-window host time6.28000 ->6.32881s (+0.777%); both windows regress. Reject despite correctness. Specialization/backend/shader/focused tests are restored; temporary proof logging is removed. HG-FAIL-024 records the scope: do not repeat this same in-place compute specialization unchanged. The proof remains useful evidence about the original workload.

## INCONCLUSIVE - VI readiness elision; pre-experiment generated code restored, 2026-09-21

Recovered the completed VI candidate build and tested it through the configured fixed realtime gameplay33m verifier. Candidate runs ee05d5ca... finish with last-three interval rates14.5677/9.4416/9.2817 and14.5254/9.1977/9.4039. Disabling only experimental VI elision and rebuilding produces baseline5f3087dc... with14.1780/9.1655/9.1188 and14.2080/9.1564/9.2021. All four runs reach the fixed33M budget and pass the existing original-boundary qualification (864 writes, no lost trace/unexpected writers/caller mismatches/invalid stacks), but all fail the30FPS target. Five-window arithmetic means must not stand in for heavy-gameplay throughput.

The older baseline c9d6e8fd... had16.4377/10.9296/10.9765; the fresh baseline is slower too. Therefore the initial comparison to that older run did NOT establish a VI-induced regression. The small candidate advantage against the contemporaneous baseline is also not accepted as a reliable gain: order was candidate/candidate/rebuild/baseline/baseline, not warmed alternating controls, and the reason for the absolute timing shift is unresolved. Keep VI elision disabled in production until a controlled comparison establishes repeatable benefit. This is an inconclusive experiment, not a claim that VI proof is incorrect or universally slower.

Implementation: tools/hgtool/vu_emit.py has a separate build-time optimize_vi_readiness flag. Explicit synthetic proof tests retain the experimental path, while emit_vu1 passes false and retains the already accepted VF proof plus all unconditional VI checks. Added a regression showing the disabled option changes only VI guards, not advances or VF proof.108 Python tests pass; refreshed native runtime/translation and65536 compiled-readiness checks passed during this continuation. Restored out/translated.cpp SHA256 b03c024bfe71528a887556d68aed272dd305a813e288ae32e03c5ee92a4f9b41 exactly matches the pre-VI emit manifest; only this generated C++ file changed. Complete keyboard work and prior compatibility fixes are preserved.

Evidence and run IDs: external vi-readiness-recovery-comparison-20260921.json. Candidate1 versus older baseline final-frame SHA02723437..., GS-state JSON SHAeaf454f2... and VU-state JSON SHA5adb6544... match. Full RAM/VRAM/binary-state comparisons were not performed here; do not claim comprehensive original-state equivalence from these selected checks. Automatic backup da58531f26fa4789b1dc4f798bec4461 preserves the active candidate emitter,638b3bbc10f54ecd8c37de1138cddf2a preserves the prior test file, and the configured hg_emit task saved prior generated bytes/exe/map. No guest timing, quality, input catalog, verifier or execution boundary was changed.

## CURRENT - previous execution path restored and replay verified, 2026-09-20

Rejected matrix183227 implementation after9.377% slower mean scene, both pairs;
all four candidate/control state comparisons passed. Archived source/tests/exe/map
in matrix183227-rejected. Restored emitter/CMake/generated main match exact
matrix-postcompat-baseline bytes. The two new matrix test files were archived
outside active tests; inherited unused helper sources remain as in that baseline.
Restored hg_game build exit0; full105 Python tests pass. Fresh restored33M replay
ends native-iop-budget and matches original image/EE RAM+registers/devices/GS/VU,
with only verified RTC IOP differences. Current exe SHA256
1a89f68ad5db75e80d7ef66adc9689f4c08600c34fb37ebbe1ed95e6ff2783ea.
Proof matrix183227-restored-validation.json. Compatibility fixes remain intact.
No matrix speed gain or verified30 gameplayFPS. No new visible game was launched.

## REJECTED - prepared matrix arithmetic slowed both pairs; restoring, 2026-09-20

Original matrix build and all four33M captures pass: image, EE RAM/registers/devices,
GS and VU exact; IOP differs only verified RTC bfcd1/2/3. Two warmups excluded.
Control scenes7.6884/7.9125s, candidates8.8191/8.2447s. Mean7.80045 ->8.53190s,
9.377% slower, both pairs. Reject this implementation despite synthetic correctness.
Archive matrix183227-rejected preserves candidate sources/tests/exe/map and hashes.
Evidence matrix183227-summary.json and all four captures; no performance gain.

## VERIFIED - live compatibility fixes and saved regressions, 2026-09-20

VU0 normal-ACC product-underflow and original callback124da0 are built. The exact
42-event replay reaches45M native-iop-budget, passing both former faults. Both
warmups and four33M regressions ended native-iop-budget; all recorded image/EE/GS/
VU comparisons pass, IOP differs only RTC bfcd1/2/3. Saved livecompat results reused.
Mean matched scene 8.46275 -> 8.52585s (+0.746%); pairs disagree,
so no repeatable speed effect is established. Retain required compatibility fixes.
Current executable SHA256 2608296bcff3c2a47887f67b2cf233fd41a30eaf9792f50df85f21c1ac3d5eae.
30 gameplayFPS remains unverified/unmet. No new tests/build/replay needed for launch.

## CURRENT - compile-time VF readiness proof retained, 2026-09-20

Build,105 Python tests and49152 compiled baseline/optimized scheduler comparisons
pass, including branches/loops/delays, arbitrary ready times, Q/P completion,
uint64 wrap, partial lanes and fault states. All four original captures match
images/EE/GS/VU, IOP verified RTC only. Two warmups excluded; controls7.1184/7.1328s
vs candidates6.9515/6.9247s. Mean7.12560 ->6.93810s (2.631% lower),
both pairs faster. Retained; all advances/timestamps remain, only proven no-op
VF guards are skipped/narrowed. VI checks and wrap fallback remain. Evidence
vu-readiness-proof-summary.json and vureadiness-* captures. New CMake/CTest target
vu_readiness_tests; hg-build-add4.cmd now includes it. All native products current.
No build/replay/profile active; launcher14640 completed exit0.30FPS still unmet.

Next larger lead: static four-stage MULA/MADDA/MADDA/MADD blocks. Own opportunity
scan finds many with no internal entry/control and no lower write to future
operands (vu-matrix-block-opportunity.json). Consider precomputing the four exact
products together while retaining each original add, flag commit, lower operation
and pipeline step at its original point. Must guard all inputs and retain scalar
fallback/fault order. No matrix batching implemented yet. Local CPU is verified
AMD Ryzen7 9800X3D; no AVX512 capability assumed or used. Latest visible rate still
needs refresh; hidden changed-image rate11.16/s predates recent small gains.

## CURRENT - combined exact MADD host call retained, 2026-09-20

Build/focused FPU/GIF/runtime checks pass:4M new scalar-chain comparisons across
four MXCSR modes, existing16M products/16M additions and frozen VU checks. Four
original captures match images/EE/GS/VU, IOP verified RTC only. Two warmups excluded;
control scenes7.1994/7.1980s vs candidates7.1342/7.1621s. Mean7.19870 ->7.14815s
(0.702% lower), both pairs faster. Retained; modest gain. These remain
two exact guest arithmetic operations, not host FMA. CPU/OS guard/fallback stays.
Artifacts vu-combined-madd-summary.json and vucombinedmadd-*; baseline preserves
prior accepted SQ-elision game/map. Current build products match retained source.
No build/profile/replay active; launcher48252 completed exit0.30FPS remains unmet.

Next substantial lead: own static block analysis shows many repeated VU VF readiness
checks are provably already satisfied. Opportunity counts in vu-static-readiness-
opportunity.json, not a speed claim. Need conservative per-block proof, branch/
entry resets and exact uint64 wrap fallback; preserve every pipeline advance,
Q/P completion, ready timestamp, arithmetic operation and explicit fault. This
would differ from rejected runtime combined dependency helper (HG-FAIL-015).
No such readiness change has been implemented. Do not omit guards on assumption.

## CURRENT - nonconflicting SQ snapshot elision retained, 2026-09-20

All104 Python tests, build and four original captures pass; image/EE/GS/VU exact,
IOP verified RTC only. Two warmups/four alternating hidden33M runs: controls
7.2876/7.2536s, candidates7.1809/7.1552s. Mean7.27060 ->7.16805s
(1.410% less scene time), both pairs faster. Retained.20 overlapping
SQ sites keep snapshots;111 independent sites omit them. No guest clocks changed.
Evidence vu-sq-snapshot-summary.json/static-proof.json and vusqsnapshot-*.
Game/map and all build products current. No build/profile/replay active; launcher
14600 completed exit0.30FPS goal unmet. Latest hidden changed-image rate11.16/s
predates this small gain; visible rate still needs refresh. Continue measured work.

## CURRENT - masked resident transfers retained, 2026-09-20

Masked PSMT8 writes passed1042 GPU/CPU cases in both modes, including confirmed
resident execution, repeated masks/indices, partial transfers, intervening reads,
later GPU draws, copy and pending-write accelerator teardown. Memory1920/GIF1285/
DMA/runtime checks pass; full game and final fixture builds exit0. All four original
33M captures match images/EE/GS/VU, IOP verified RTC only. Two warmups excluded;
control scenes7.5312/7.5349 vs candidates7.3364/7.3433s. Mean7.53305 ->7.33985s
(2.565% less), both pairs faster. Retained. No timing or guest work change.
Evidence masked-transfer-summary.json, pair-times and maskedtransfer-* captures.
Accepted binary/map, native library, headless and tests now reflect this candidate
plus accepted AVX2 products and untextured GPU triangles. Full-vector helper reuse
remains reverted. No build/replay/profile active; launcher20964 completed exit0.

30FPS goal remains unmet. Latest visible changed-image measurement remains about9/s;
hidden fixed-work gains are not internal game FPS. Audio recording gaps unresolved;
Options captured path works. Next measurable milestone: refreshed presentation/
frame-boundary evidence and remaining CPU/GPU hotspot selection. Continue beyond
this checkpoint; avoid repeating rejected full-vector/XYZ arithmetic or read-page
narrowing changes unchanged. Automated replay windows remain hidden, live input
sessions retain control. Current launcher wrapper runs hg-maskedtransfer-pairs.py;
use fresh artifact names before another run. Sources/headers require no restoration.

## CURRENT - stack profile isolates GS transfer waits, 2026-09-20

External StackWalk64 sample:325 contexts, zero context failures, depths6..20;
276 walks end with API false below20,49 reach the20-frame cap. Do not claim every
stack fully unwound. Among62 top-ntdll samples,39 unwind through Accelerator::
download and15 through flush. Many download chains directly reach PSMT8 host
transfers or CPU raster scopes. This uses unwind metadata, not stack-word guesses.
Original images/EE/GS/VU match; only verified IOP RTC offsets differ. Replay exit2
native-iop-budget, sampler/launcher exit0. No replay/build/profile remains active.
Evidence post-stackwalk-gameplay-native-sample.log, summary and equivalence JSON.

Next candidate lead: resident masked host-transfer writes can preserve untouched
GPU word bits without downloading their pages first. Scope PSMT8 fast16-byte
columns, queued unique-word masked updates; must preserve CPU reads, queued draw
order, partial transfers, overwritten lanes and memory lifecycle. Need written
implementation plan/backups and GPU/CPU dependency tests before changes. Unlike
rejected read-footprint narrowing, this removes a demonstrated readback boundary.
No such production transfer change implemented yet. Accepted game/map remain
AVX2 product + untextured triangle build restored from vu-full-vector-baseline.
Library objects/headless/tests require rebuild after rejected full-vector revert.
30FPS remains unmet; latest visible changed-image measurement still~9/s.

## CURRENT - full-vector extension rejected, accepted binary restored, 2026-09-20

Full-vector multiply/add passed focused checks and all four original state/image
comparisons (IOP verified RTC only), but scene mean7.5568 ->7.5691s (0.163% slower).
First pair slower, second effectively flat; no repeatable gain. Reverted only
vif.hpp, vu_products.cpp, gif_tests.cpp and restored accepted hg_game.exe/map from
vu-full-vector-baseline, byte equality verified. Candidate retained externally in
vu-full-vector-rejected. Native library objects/tests/headless may still reflect
the rejected candidate; next build must refresh them. Sources touched accordingly.
Accepted AVX2 products and untextured GPU triangles remain.30goal unmet.
Next: bounded external StackWalk64 gameplay profile to resolve DLL caller costs.

## Retained - AVX2 integer VU products, 2026-09-20

All four original captures match images/EE/GS/VU; IOP differs only at verified RTC
bytes. Two warmups and four alternating hidden33M runs: controls7.8081/7.8231s,
candidates7.6233/7.6208s; mean7.8156 ->7.62205s (2.476% less scene time), both
pairs faster. Build and focused FPU/GIF/runtime checks pass;16M product comparisons
and16M additions across four rounding modes preserve bits/flags with no host FP
exceptions. Retain integer AVX2 product helper with existing CPU/OS guard and
SSE2/scalar fallback. Evidence vu-product4-avx2-summary.json and vuproduct4-*.
Accepted exe/map currently include this change and untextured GPU triangles.

## Retained untextured GPU triangles - 2026-09-20

Own --profile-gs identified292 large independent-triangle fallbacks with PRIM4b
(untextured Gouraud/blended), CT32/Z24 and depth writes masked. Scene independent
triangles cost0.3636s before extension. Existing bounded GPU triangle path now also
handles TME-off using interpolated/flat RGBA directly; no texture reads or STQ work.
Existing depth/alpha/blend/mask/alias limitations and CPU/fault fallbacks remain.
1018 GPU/CPU cases in each memory mode pass, including actual acceptance, unused
invalid texture state and ordering. Original initial run plus4measured captures
match all images/EE/GS/VU; IOP differs only verifiedRTC0xbfcd1/2/3.

Two warmups then alternating controls8.2821/8.2323s vs candidates7.7525/7.7150s.
Mean8.2572 ->7.73375s,6.339% less scene time; both pairs improve6.39%/6.28%.
Whole replay mean27.09279 ->26.46466s,2.318% less. BOTH binaries used --hidden-host;
these are matched fixed-work timings, not visible-frame-rate measurements and not
comparable absolute timings to earlier visible runs. Goal30gameplayFPS remains unmet.
Initial candidate accepts305856 triangles versus302650 before (includes strips),
uploads594911232 vs678273024 bytes. Detailed fallback probe removed after diagnosis.
Backup untextured-triangle-baseline; untextured-triangle-pair-times/summary.json and
untextured-{control,after}{1,2}-equivalence.json retained externally. Retain change.
Own CPU equivalence does not establish full physical-console fidelity.

Draw-local texture setup (2026-09-19): hoisted immutable CT32/PSMT8/PSMT8H
setup and factored own PSMT8 address formula, preserving live texels and accessed
CLUT validity.864 scalar triangle/fault cases and241 GPU cases pass. Four native
runs with the same video-rate instrumentation: before27.4915/25.6973s,
after21.6677/21.5711s. Mean26.5944 ->21.6194s (-18.71%). Images/EE/GS/VU equal;
IOP RTC only, including independently verified UTC-day transition for first control.
Evidence external gpu-texture-times.json and texture-*-equivalence.json.

GPU-resident coherence experiment: regression (2026-09-19)
Original33M scene: saved pre-wrapper24.7895s; current wrapper with --gpu-sprites
29.5581s; --gpu-resident30.2274s. Single matched sequence, not a stable mean.
Control is19.24% slower; residency is21.94% slower than the saved baseline.
Compared images/EE/GS/VU match, IOP RTC fields only. All241 GPU comparisons pass
in both modes. The CPU wrapper overhead must be explained before this change can
be accepted for performance. Out-of-line GsLocalMemory operators in the linker map
are an investigation lead, not attribution. Evidence external gpu-resident-times.json
and resident-*-equivalence.json. No original-game FPS claim follows from these times.

## Guarded native wide STQ division - 2026-09-19

The exact shifted ratio uses native128/64 division only with checked divisor,
shift0..63 and quotient bounds. Other cases retain portable checked long division.
No unshifted quotient is computed unnecessarily on the fast path. 200000 bit-serial
comparisons and GIF suite pass. Native33M pairs26.653/23.8881 and24.5814/24.4012s;
means25.6172/24.14465s (5.75% less), with substantial run variation. All compared
images/EE/GS/VU equal, only verified IOP RTC differs. gpu-wide-times.json and
wide-{before,after}{1,2}-equivalence.json are external evidence. Windows x64 tested;
other host branches not independently built in this session.

Solid-fill candidate was reverted: two pairs yield2.09% mean reduction but disagree
in direction. Added GPU coverage raised flushes/cost. HG-FAIL-006 records scope.

## OpenGL sprite batching - 2026-09-19

Matched native CPU33M scene31.6796s, synchronous GPU34.5168s (8.96% slower).
The16384-pixel threshold missed medium strips, and each dispatch synchronized.
Batch candidate lowers threshold4096 and groups ordered dispatches within existing
pending-draw calls, flushing before CPU primitives/fallback and return/fault.
Native scene22.9379s (27.59% less than CPU), total48.8219 versus63.9807s.
32848 draws,442843448 pixels,721 flushes,877961000ns measured flush cost.
All compared EE/GS/VU/images match triangle-after2; IOP differs only RTC bfcd1/bfcd2.
Artifacts gpu-sprite-batch*; snapshot external gpu-sprites-batched. Default remains
off until broader validation; whole-console fidelity and target speed unproved.

## Exact triangle arithmetic - 2026-09-19

Prepared bounded signed STQ coefficients share Q; bounded reciprocal estimates
are corrected with integer products to exact quotients. 300000 synthetic quotient
checks and six triangle differential/fault cases pass. Alternating33M scene times:
31.4615/30.0923 and31.7059/28.1449s (before/after), means31.5837/29.1186,
7.81% reduction. Images and EE/GS/VU agree, IOP differs only verified RTC fields.
Artifacts triangle-times/equivalence/iop-equivalence.json outside repository.
Console-perfect whole-renderer behavior is not established by this comparison.

# Performance measurement

## Exact STQ division and native presentation, 2026-09-19

A checked 64-bit shifted numerator uses native quotient/remainder when it fits;
the existing wider long-division path remains for all other operands. Four
alternating33M replays: before76.922885/76.016258s, after75.433051/74.990333s.
Scene30M..33M: before47.274/46.8707s, after44.8424/44.8798s (4.70% lower mean).
All compared EE/GS/VU state and images match; IOP differences are RTC bytes only.
Artifacts: TEMP/haunting-toc-probe/divide-times.json and divide-equivalence.json.

The in-process hg_game controlled33M run independently matches this headless
state and image, with898 produced/897 presented snapshots and a nonblack OpenGL
pixel readback check. This is presentation validation, not a speed benchmark or
internal game FPS measurement. It ran concurrently with startup regression tests.


## Full unskipped OPENING recheck69,2026-09-15

69full completes all6300 OPENING starts and54366 movie sectors. All6518
startup conversion sequence/guest timestamps exactly match61full, including
the218 pre-OPENING conversions. OPENING first/last host times25.3751/236.114s
span210.7389s for210.241230 modeled seconds:99.763845% modeled speed and
99.633559% against6299 nominal30fps intervals. This is0.3345s longer than
61full's210.4044s; no new speed improvement is claimed.6506 sampled images
were retained. Final native EE fault122a30 at234941153us is later than61's
2c9480; the complete original movie precedes the extended initialization path.
Evidence: external full-opening-speed-69full.json, replay command, final log
and all sidecars. The same experimental clock/TOC and scripted inputs apply;
no displayed-FPS, audio-sync or physical-console parity claim.


Additional full-replay comparison:6505 sampled framebuffer hashes are shared
in identical order out of6506 per capture. One sample in each is absent from
the other; sampling does not prove every displayed frame or pixel correctness.
69 median CSC host interval is33ms. Consecutive60-interval windows range
85.0122%..104.3999% modeled speed (median99.9934%), so the near-unity overall
rate does not establish uniform cadence. Host-stall cause remains unproven.
Evidence: external full-opening-frame-order-61-to-69.json, including the
slowest windows. No new runtime timing change was made from these observations.

## Complete OPENING and rejected preview cache,2026-09-15

61full executes all6300 OPENING CSC starts and consumes all54366 original
movie sectors. First/last starts: guest24367072..234608302us and
host25.2756..235.6800s. The210.241230 modeled-second span takes210.4044host
seconds:99.922% modeled rate,99.792% against6299 nominal30fps intervals.
All1284 CSC timestamps from59 agree with the full-run prefix;6506 sampled
images retained. EE then stops at missing static target2c9480,RA120ca8,
guest234907646us. Generic dump kind native-iop-fault does not classify the
processor; the console explicitly identifies the EE boundary.
Evidence: external full-opening-speed-61full.json and complete61 sidecars.
This remains a conversion-event measurement with the experimental clock/TOC,
not verified physical scanout, audio sync, or physical-console timing.

Candidate62 cached the last successfully published preview and skipped exact
RGBA/dimension repeats. Build and all25 CTests pass. Fixed60M replay62 takes
35.3766host seconds versus58's34.9217s for the same35.611639 modeled seconds,
1.303% longer. All1066 guest timestamps, EE RAM, GS VRAM/metadata and machine
JSON agree. Both captures have1316 unique images;1315 shared hashes retain
their order. All49 SIFMAN/SNDDRV report lines agree. The final pre-dump
counter is1762 requests,1329 publications,394 unchanged,0 reader retries;
remaining requests lacked a supported scanout. IOP byte parity is not asserted.
The reduced preview-write count did not establish a whole-run speed gain.
Cache and its counters are rejected and removed; retained host pacing and
history optimization remain unchanged. Explicit utility include is retained
for the runner's existing std::move use. Evidence:speed-opening-58fixed-to-62fixed.json
and sif-events-58fixed-to-62fixed.json outside Git.

### Completed unskipped startup demo, candidate60

120M slices complete without a runtime fault:181 CAPCOM plus2968 LOOP_DEMO
CSC starts. All31056 LOOP sectors are consumed. The first1402 guest timestamps
match previous51noskip;3051 sampled images retained. Full LOOP first-to-last
conversion span is99.073583 modeled seconds in100.3592host seconds (98.719%
modeled rate). Against2967 nominal30fps intervals (98.9s), this is98.546% speed.
CAPCOM spans6.0544host seconds.13 rebases,10.200066s total waiting. The next
asset reads occur after the final LOOP conversion. This verifies the unskipped
movie path, not whole-game playability or audio/display scanout correctness.
Evidence: external full-loop-speed-60noskip.json and complete60noskip sidecars.


## Current movie pacing evidence, 2026-09-15

Candidate54 history optimization is retained. Current57 engine was compared
without pacing (58fixed) and with the explicit --realtime limiter (59paced),
using the same60M slices, ten exact inputs, viewer53, profile and PNG capture.

| Segment | Modeled first-to-last CSC span | 58 unlimited host span | 59 paced host span |
|---|---:|---:|---:|
| CAPCOM,181 starts |6.090484s|4.53804s|6.0546s|
| LOOP_DEMO,interrupted37 starts |1.283789s|1.4903s|1.4930s|
| OPENING,first1066 starts |35.611639s|34.9217s|35.5496s|

59 reaches100.175% modeled OPENING rate and100.593% CAPCOM rate. Two-second
OPENING windows range98.317%..103.416%; this does not establish exact cadence.
All1066 guest timestamps and complete EE RAM/GS VRAM/GS metadata/machine JSON
match. All1316 common sampled images retain order. All49 retained SIFMAN/SNDDRV
report lines match; the same49-line checks now also pass for56/57 versus54.
IOP byte parity is not asserted.59 reports1564 waits,8.441174s actual total
waiting,1147us maximum overshoot and10 timeline rebases.

Earlier57 paced OPENING took41.2561s (86.32%) on the SAME executable. Its
sampled execution phases also increased.58/59 establish that this slowdown is
not consistently reproduced by that pacing policy; the cause of host variance
remains unknown.56 used10ms lag tolerance and reached92.30%;55 ordinary Windows
sleep reached73.27% and is rejected. Retain the100ms bound and high-resolution
wait backend; do not infer a policy fix from uncontrolled wall-clock variation.

Original ffprobe evidence now establishes OPENING has6300 frames at30fps and
210s reported duration. Therefore all previous1066-start timing checks measured
only its beginning. LOOP_DEMO has2968 frames at30fps,98.9s reported video duration
and99.008667s container duration.60noskip exercises120M slices without later
skip/start inputs.60 adds per-checkpoint host wait accounting; all25 CTests pass.
These checks use synthetic TOC HG-DIAG-007 and issue-slot clock HG-DIAG-008.
They are not physical-console timing or presentation-scanout verification.

Evidence outside Git: haunting-toc-probe/speed-opening-58fixed-to-59paced.json,
movie-segment-speed-through59.json,pacing-analysis-through58.json and
haunting-movie-reference60/original-movie-timing.json.


## Active measurement,2026-09-13 continuation

Latest resumed checks supersede paused notes below. Replay25 verifies the
RGB24 reduction: matching26M..31M takes9.1022s versus10.7102s in23 (1.177x
throughput); runner raster cost falls3.1498242s to0.9096532s for298draws.
150 matching movie CSC starts span4.969730modeled seconds and9.0565host
seconds (median59.5ms), still below natural30fps. Full32M ends in39.126s with
native-iop-budget. Guest CSC times and OPENING read range agree exactly.
472of473 sampled hashes agree in order; unique samples are in the pre-opening
corridor/menu transitions, not conflicting opening images. Complete-VRAM
synthetic tests were already passing before this run.

A zero-tail IDCT experiment passed1280 sparse/dense/cancellation reference
cases but replay26 took9.9283s over the same interval, with raster also slower.
No speed benefit established; the experiment was reverted. Keep the expanded
mathematical regression. Device sampling now separates IPU/scratchpad, VIF/GIF
and raster phases; replay27 measures the restored transform. External evidence:
%TEMP%/haunting-toc-probe/speed-replays23-26.json. These are host optimization
checks under the experimental issue-slot clock, not physical-console parity.

Replay28 verifies aligned CSC input-lane consumption against restored27:
26M..31M9.9756s ->9.1673s (1.088x throughput);150 CSC starts span the same
4.969730modeled seconds,9.9176s ->9.1200s host. All474 sampled image hashes
match in order. Final EE RAM, GS VRAM, GS JSON and native machine JSON are
byte-identical; IOP RAM differs only at RTC second/minute bytes0xbfcd1/2.
Synthetic byte-stream comparison covers every initial bit offset0..127,
input starvation, full output FIFO and two consecutive macroblocks.
External speed-replays27-28.json records timing/image counts. This change
consumes the same bytes at the same device-service boundaries; it adds no
guest throughput multiplier and preserves the unaligned extraction path.

Replay29 direct static intra-page branches:26M..31M8.8138s versus9.1673in28.
Final EE RAM, GS VRAM, GS metadata and native machine JSON match exactly;
473common image hashes retain order (one preview sample omitted).150 matching
CSC starts span8.7698host seconds, median56.9ms. This modest single-run gain
does not establish statistical significance alone. Native synthetic loops
compare every budget0..63 against single-dispatch execution, including likely
annulment, J/JAL, delay slots, trace PCs and register state. All21CTest passed.
External speed-replays28-29.json retains the comparison. Indirect/cross-page
targets still use the checked static dispatcher; no runtime decoding is added.

Replay30 restricts MMIO selection and kernel-overlay searches to addresses
that require them.26M..31M8.4318s vs29's8.8138s; EE RAM, GS VRAM/metadata and
machine JSON remain identical. Refreshed21CTest pass including relinked EE
diagnostic. Aliases, protected mapping spans and scratchpad are covered.

Replay31 sustains1066 OPENING CSC events over35.611639modeled seconds in
58.8315host seconds (median53.8ms), versus22's77.9186s. Full60M83.9024s,
native-iop-budget. All opening guest timestamps agree exactly;1313common
sampled images retain order. Final GS VRAM/metadata and machine JSON agree.
EE RAM differs from22 at one retained byte0x47e373 (12 ->11), already present
in25 and stable through31; input pulse timings differ. Original references
place it in the0x47e360 input-related object, but exact field semantics are
not yet established. Do not call22->31 a fully byte-identical state comparison.
IOP RTC bytes differ with launch time. Evidence speed-opening-replay31.json.

IPU31 changes the separable transform loop order across independent X outputs
while preserving each output's U/V summation order.1280 mathematical cases
pass. An external alternating microbenchmark shows matching checksums and
roughly13-15% gain on AC workloads (%TEMP%/haunting-ipu-loop-bench/results.json).
The DCT lookup indexes the existing tables in source order with two64KiB byte
arrays; exhaustive tests compare every16-bit prefix and low-bit extremes to
the linear lookup. Invalid codes and sign/stream consumption remain unchanged.
The combined connected interval improves8.4318s ->8.1394s. Neither optimization
changes modeled timing. Natural host playback remains below target (~61%).

Replay32 AOT fixed-width scalar accesses reduce sustained OPENING58.8315s
to53.6108s (same35.611639modeled seconds/1066events; median49.3ms). All1313
sampled hashes match in order. Final EE RAM, GS VRAM/metadata and machine
JSON match byte-for-byte. Full60M76.8996s, native-iop-budget. All21 refreshed
tests pass, including scalar width/sign/alias/protection/span/watch comparisons.
Evidence speed-opening-replay32.json. The methods inline only aligned valid
user RAM; protected/kernel, device and watched accesses use existing checks.
Current natural-rate fraction is about66.4%, not a displayed-FPS measurement.

Replay33 replaces per-thread modulo in EE/IOP selection with one normalized
cursor and increment/wrap, and combines IOP delayed wakeups with selection.
Priority, equal-priority cyclic order and all wakeups remain unchanged before
execution.160 readiness/cursor combinations per scheduler plus existing locked
worker/blocking fixtures pass. Sustained opening53.1226s vs32's53.6108s;
this small single-run difference is not statistical proof. Final EE RAM,
GS VRAM/metadata and machine JSON match;1311common captures retain order.
All21CTest pass. Evidence speed-opening-replay33.json.

Replay34 word-based IPU prefix reader is rejected:53.42s sustained opening
vs33's53.1226s, despite identical final EE/GS/machine state and all1311sampled
hashes. Existing bit-offset/width and full stream tests pass, but no connected
speed gain is established. Restore the original inline byte reader; retain
the previously verified33 optimizations. Evidence speed-opening-replay34.json.

User paused work during replay24. RGB24 build is complete and all refreshed
regression targets/21CTest pass. Replay24 was interrupted before a complete
opening timing comparison, so no connected RGB24 speedup is established.
Resume with a fresh numbered replay and compare against23; do not treat the
interrupted partial run as a completed benchmark.

Replay22 clears the original audio stall:1066 OPENING CSC starts over35.611639
modeled seconds/77.9186host seconds, median34.463/71.4ms respectively. Reads
11202sectors2113282..2124483; visible cellar scenes advance. Full60M budget
takes111.424seconds. See external opening-replay22-evidence.json. There are
additional conversions between CAPCOM and OPENING; do NOT label every CSC
after sequence185 as OPENING. First OPENING CSC in22 is219 at24,367,072us.
Replay23 directly times nonempty runner GS batches.26M..31M:3.15seconds in
rasterization of10.7host seconds. Inline FINISH/CLUT rasterization is excluded.
Checked RGB24 sprite pixel-stage reduction is building; generic pipeline/full
VRAM equivalence fixture including masking and texture feedback passes. No
connected speed improvement yet; verify fresh replay24 after the build.

Replay18 enters OPENING and reads1322sectors2113282..2114603. Four CSC
conversions then stall through60M modeled us; host runtime57.7958s is therefore
not a playback-speed measure. Original audio clock clamps to zero because
produced8192 minus queued16384 is negative. Native MODLOAD temporary storage
overlapped the original SYSMEM audio synchronization allocation; original
DS2O_S1.IRX bytes exactly identify the overwrite. Connected native allocations
now use the original AOT SYSMEM allocator, with dedicated call stacks reserved.
Reboot and native/Python suites pass. Replay20 reaches a previously uncompiled
CRI transfer-command switch at12.383634M us, before movie playback. The bounded
14-entry original/live table is added; connected replay after rebuild is pending.
No natural playback rate or sustained OPENING advancement is yet verified.

Replay16 after fixed-UV hoisting:185 CAPCOM CSC starts, median host interval
55.35ms versus53.05ms in replay15 (fresh extraction from its log). Modeled median
33.3655ms. No connected gain demonstrated. Replay16 times out from the active
New Game menu to missing demo case12e534 at54,059,827us/77.2441s; its Cross was
too early during fade. Replay17 waits for
the highlighted New Game row and advances to missing menu-switch case12e578.
Seven original/live table entries at44e8a0 are independently verified and added,
along with six downstream movie/demo descriptors. No OPENING consumption yet.
Replay17 stops24,263,393us/30.1982s; median CAPCOM host55.85ms. Replays16/17
both preserve replay15's first149 captured hashes exactly. External comparison:
clock-bursts-16-17-timing.json. Replay16 is NOT a budget exit.

Latest handoff: replay15 visibly reaches title and New Game menu, then missing
130a40 at41,780,650modeled us /52.6577host seconds. No OPENING consumption yet.
Pending fixed-UV sprite optimization hoists identical integer U calculations
per column and V per row while retaining row-major shading. gif_tests passes;
diagnostic regeneration/build and connected measurement remain pending.
Do not claim a speedup: latest measured CAPCOM median is33.365ms modeled and
55-56ms host (~18 conversion events/sec, not measured displayed FPS).

Replay10 after ordinary-return/DC changes stopped at missing1cda10 after
19,274,631us in22.1177 host seconds. All149 sampled hashes match replay9
in identical order; capture spans18.0188s vs18.2978s. No measured overall
speedup or natural timing claim. Original CAPCOM raw picture-start-code count
is187 (nominal6.2333s at30fps); packet demultiplexing/presentation timing still
needs verification before treating that count as exact movie duration.

Original1cd4e8/f4 registers1cda10 through1c5388; replay10 object+78 agrees.
That callback is now rooted; downstream3cff70 table matches original and its
slot18 target1e32b8 was already compiled. No shared implementation copied;
narrow non-rendering reference search only corroborated containing function.
External stream-completion-callback.json retains evidence.

IPU CBP/DC/DCT lookups now extract one32-bit prefix and compare shifted views,
retaining exact codebook, sign, consumption and faults. Existing complete
codebook/mask native tests and all21 CTests pass. No isolated speedup claimed.
Optional profile now logs successful contiguous disc-read ranges and observed
CSC begin/end times (decode/conversion events, not screen presentations).
From original ISO DATA.CVM extent1460594 plus CVM origin3sectors, CAPCOM starts
at LBA2111088 (1704sectors), OPENING at2113282 (54366sectors).

Replay11 ends at default callback1c4ce8 after19,274,818us/21.6912host seconds.
Disc telemetry verifies all1704CAPCOM sectors consumed; no OPENING read.
182 CSC starts span6.732996modeled/10.6956host seconds, median interval
33.365ms modeled/55.4ms host.158/181host intervals exceed50ms; only2modeled
intervals do. This separates host slowness from the roughly30Hz median modeled
conversion cadence; it does not establish frame presentation or complete decoding.
Evidence: clock-bursts-11-csc-timing.json and startup-clock-bursts-11.log.

Byte extraction optimization collects up to8bits per source byte while
preserving MSB-first stream order, little-endian qword storage and read-only
position semantics. Independent bit-by-bit tests cover8patterns, all128start
positions and widths0..32, including qword boundaries. Runtime tests pass;
replay12 passes both former stream callbacks and reaches100Mquanta without
fault. Its185 CSC conversions have median host interval56.05ms: no measured
speed gain over replay11. The first149 sampled image hashes remain identical.

Replay12 also exposed4830submitted but unexecuted draws after CAPCOM. Servicing
GS drawing in the device phase (GS manual pp38-43) independently of previews
reveals the red corridor, replay13 capture149. The original scene waits for
Start; replay14 presses it through the controller and progresses until an
oversized IMAGE upload. This is a functional transport stop, not decoder
performance. Synthetic memory-oracle evidence and the pending correction are
documented in ORACLE.md. No OPENING consumption or natural host rate yet.

Target: reproduce the original program's presentation cadence on desktop hardware.
Current diagnostic slices are cooperative work boundaries, not PS2 cycles or video
frames. One slice advances the diagnostic clock by one microsecond. Faster host
execution must not silently change this clock, device order or instruction budget.
Real-time gameplay is not verified.

## Profiling

`hg_system_diagnostic --profile` samples roughly one in4096 slices using a mixed
slice index to avoid locking to periodic loops. It prints host nanoseconds by
phase, elapsed throughput and the most frequent sampled EE boundary PCs. With
`--checkpoint-every`, reports appear during a long run as well as at exit/fault.
Boundary-PC frequency is not a per-function CPU-time profile. Small phases include
timer overhead; rare I/O or rendering spikes can be missed. Elapsed time includes
final diagnostic output. Compare identical workloads and retain state captures.

## Windows startup dispatch experiment, 2026-09-13

Release,30M slices, same28-module IOP bundle, synthetic TOC probe, no host input or
preview. All runs used sampling. External logs/captures live in
`%TEMP%/haunting-toc-probe/` with the following prefixes:

| Prefix | EE/IOP dispatch | Host seconds | Slices/second |
|---|---|---:|---:|
| profile-baseline | monolithic/monolithic |35.9562|834348|
| profile-pages | partitioned/monolithic |14.6356|2049800|
| profile-both-pages | partitioned/partitioned |4.56141|6576920|

The observed overall gain is7.88x on this workload, not a gameplay-FPS claim or
a controlled multi-run statistical estimate. Initial sampled costs: EE47%, IOP31%,
diagnostic history2.5%. Both generators now emit separate, non-inlined C++ functions
for4KiB address regions and a static outer dispatch. All instruction effects,
delay slots, trace boundaries, faults, native waits and execution budgets remain.
This is build-time organization of native code, with no instruction decoding/JIT.

EE RAM and GS VRAM match byte for byte. Every captured metadata field matches.
IOP RAM differs only at0xbfcd1/2, the seconds/minutes of the original RTC result;
the runner initializes RTC from host UTC at launch. Comparison retained in
`profile-state-comparison.json`. Native tests cover cross-region delay slots,
multi-step budgets, unknown addresses and whole-budget return on blocked IOP waits.

Next: profile connected movie playback, quantify actual guest presentation events
separately from preview sampling, and measure frame-time distribution. Do not
increase per-slice quanta or skip waits merely to report faster playback. Changes
to the provisional timing model require independent hardware/program evidence.


## Experimental work/time correction

The legacy diagnostic schedules one EE boundary per microsecond, while its
bootstrap uses a larger special budget. This is not an EE cycle model. Local
EE manual p36 gives147.456MHz bus; Core manual p78 gives processor/bus ratio2.
Core pp14/18 describe dual issue and stalls, so an AOT boundary cannot be called
one accurate hardware cycle. IOP clock profile is36.864MHz.

Opt-in `--clock-profile issue-slots` now allocates294/295 EE and36/37 IOP native
boundaries between1us device updates, retaining fractional budgets exactly.
Cooperative bursts yield at native services, syscalls and JR RA returns. A
build-time-proved NOP*/RAM-load/zero-branch/NOP loop may yield while its real
aligned RAM flag remains zero; MMIO, watched loops and overlays are excluded.
This avoids millions of redundant host polling instructions. It does not signal
completion or change the flag. Unused budgets at yields, branch bundles,
interrupt service budgets and cache/pipeline effects remain timing limitations.
Legacy timing remains default; this experimental path is not yet validated for
natural movie playback. The first burst probe exposed an overrun past the IOP
bootstrap return sentinel; return-boundary regression coverage was added.


Connected burst replay4 completed30M microsecond quanta in52.1107 host seconds,
with129 unique sampled images including advancing CAPCOM characters. This is
about0.576 modeled seconds per host second, NOT natural movie FPS. Earlier replay3
at the card prompt reached1.009 modeled seconds per host second. Workload matters.
A real nested-bootstrap stack collision was fixed using an independently allocated
0x2000-byte IOP stack for synchronous nested initializers; suspended bootstrap CPU
and RAM stack now survive. Full21 CTests pass after this change. Preview publication
for experimental pacing follows field events with a16ms host cap; RGB output uses
one bulk write. It does not flush guest draws. Short host input pulses replace the
old1.5-second holds, which repeated menu selection at faster execution speeds.

User explicitly authorized PCSX2 inspection for timing, without copying (2026-09-13).
Local installed `Documents/PCSX2/inis/PCSX2.ini` inspection found EECycleRate=0,
EECycleSkip=0, FramerateNTSC=59.94, NominalScalar=1. This corroborates our existing
scan cadence, not instruction timing or full-speed movie playback. No PCSX2 source,
rendering algorithm or game patch was inspected/copied in this check.


The IPU diagnostic pump previously moved one128-bit qword per microsecond,
imposing a16MB/s cap unrelated to hardware throughput. EE manual pp41-43 defines
peripheral DMA arbitration slices of eight qwords. The opt-in profile now services
up to eight real FIFO transfers per direction per quantum, stopping on backpressure.
This does not implement cycle-exact bus arbitration. Replay5 -> replay6 reduced
CAPCOM-to-scene host time58.8558s ->27.7091s (different subsequent scene stop).
All148 captured unique frame hashes are identical in order; this is sampled-image
agreement, not a claim to capture every original video frame. Replay6 reached
20,447,297us and missing3913b0, passing prior120e80. Focused4 regressions passed.
Original/live table47a790 has14 methods; all independently verified/rooted as a
batch,157508 words976 boundaries. Long opening remains unverified.

Profiler now additionally weights EE quantum-entry PCs by sampled EE execution
cost, so frequently visited cheap waits can be distinguished from expensive
quanta. A quantum can enter another function: this is still not per-function
CPU attribution. Native straight-line dispatch experiment is under validation;
retain profile-before-fallthrough as its exact legacy30M baseline.


Original movie target independently verified: first sequence headers of CAPCOM
and OPENING both carry frame_rate_code5, sequence-extension n=0,d=0 and
progressive_sequence=1. H.262 Table6-4 (PDF p52) therefore specifies30 progressive
frames/s for these sequences. This is distinct from NTSC59.94 field scanout.
External `movie-rate-evidence.json` records raw header bytes, original CVM offsets
and parsed fields. No source-code or PCSX2-derived movie implementation was used.


Straight-line EE goto dispatch passed86 Python/native translation tests and exact
legacy benchmark state comparison (`profile-fallthrough-comparison.json`): EE RAM,
GS VRAM and metadata match; only IOP RTC second/minute bytes differ. Timing4.56995s
before vs4.66269s after shows no legacy single-boundary gain. Replay7 burst reached
scene in25.8812s vs replay6 27.7091s, but different scene roots/input make that an
uncontrolled comparison. Replay8 removed four obsolete movie instruction watches
that enabled per-instruction trace recording;20,447,349us took22.4743s. Capture poll
now10ms so it can observe faster content. These are not yet30fps playback results.

Current unpromoted optimization recognizes only a LW/SRL8/ANDI1/NOP*/BNE/NOP
loop, whose loaded register already equals1. It may park at the header only for
IPU CHCR3/4 when the actual modeled STR bit is set. CHCR reads have no completion
side effect; DMA continues through real FIFO transfers outside the CPU burst.
All other addresses execute normally; watches and overlays disable parking.
87 Python tests and native tests check release on actual STR clearing, unrelated
RAM fallback, watched-loop execution and mutation/mask rejection. Connected replay
validation is pending the generated build.


Replay9 with bounded DMA poll parking reached20,580,407us in21.5898s and stopped
at130460, a follow-on scene descriptor already identified for the next build.
No natural30fps or long opening verification yet. Final pending build43368 changes
ordinary returns to preserve the remaining CPU quantum; only actual host return
sentinels stop on JR RA. Native tests cover both ordinary continuation and sentinel
stopping. An IPU DC-only fast path uses the same two basis products and existing
rounding when reconstructed AC terms are all zero. Runtime tests pass for constant
inverse-transform values across six block destinations, four precision settings,
negative/positive values and clipping edges. Connected validation is pending;
these optimizations have no claimed measured speedup yet. Fresh replay10 helper is
prepared outside the repo. Rebuild/relink after43368 before starting it.


## Major speed audit and denser sampling (2026-09-13)

Replay35 uses1/256 host-only sampling and separate IPU input/output phases.
At matching26M..31M checkpoints sampled EE cost13.2965ms exceeds IPU input
5.1271ms, VIF/GIF4.6609ms, IOP3.0359ms and IPU output2.3574ms. These are
sampled totals, not full phase wall times; large sparse device events vary.
Full32M diagnostic completes in34.4431s with native-iop-budget. No speedup
claim is based on sampling alone. Earlier1/4096 reports remain historical.

Fresh full original-input audit and unresolved-site classification are recorded
in DECODING_SCAN.md. No evidence currently attributes measured opening host
slowdown to an omitted executed EE routine. Guest polling and incomplete cycle
modeling still require care; explicit translation gaps remain for later paths.

Local PCSX2 timing settings remain unmodified at normal cycle rate/skip and
NominalScalar1. Official https://pcsx2.net/docs/troubleshooting/performance/
distinguishes internal FPS, video output VPS and speed percentage; conversion
throughput here must not be reported as presented FPS. The page also identifies
host power configuration as a performance consideration. This host is a Ryzen7
9800X3D using Balanced; no power-plan changes or new live oracle run were made.
No PCSX2 implementation, algorithm or rendering material was copied.


Byte-copy batching replay36 was rejected:1066 events retain exact guest times,
final EE RAM/GS/machine state and all1311sampled images match33; host span
54.2326s versus53.1226s shows no gain. Recognizer/helper and candidate-only
tests were removed. Original115bb8..115bf0 inspection shows wider copying
precedes this remainder loop. Next candidate reduces redundant ordinary-RAM
checks for128-bit access, with full-span atomic store validation and fallbacks.


Replay37 retains guarded128-bit ordinary-RAM access:51.8379host seconds for
1066 conversions across35.611639modeled seconds, median48ms (~68.7% modeled
rate). Guest timestamps, final EE RAM/GS/machine state and1311sampled images
match33 exactly. All21 refreshed tests pass; six startup tests rerun after
successful relink. Earlier link collided with test execution; serial relink
resolved the file lock. No timing run overlapped compilation or tests.

Separate original-formula IDCT benchmark built with MSVC /arch:AVX2 is about10%
faster across1/16/64 coefficient inputs with matching sampled checksums. Results
are external haunting-ipu-arch-bench/results.json, not whole-game evidence.
No project architecture flag was changed. Native function sampling38 is next;
that replay is diagnostic-only because thread sampling perturbs host timing.


Native profile39 captured3254 main-thread instruction pointers with0context
errors over five seconds during opening. Map-based attribution: EE trace_pc161,
EE r174/w77, rasterize_sprite196, psmct32_word160, IPU rgba126 and IDCT53.
This diagnostic run is not a speed benchmark. It identifies tiny accessor and
disabled trace call overhead as more valuable than AVX2 IDCT work (~1.6% of
samples in IDCT). Candidate40 adds explicit empty-watch guard at emitted EE
trace call sites and forces checked r/w inline. No guest behavior changes intended.
All rendering observations are from this independent native runtime only.


Combined EE trace/inlining40 was rejected and reverted. Guest timestamps,
EE RAM/GS/machine state and1311shared samples match37, but53.2729host seconds
versus51.8379s shows no gain. All21checks passed. Sampling attribution alone
does not establish that a proposed inline change helps. Next41 reuses additive
X/Y terms of our existing framebuffer address formula, preserving live pixel
order and validating the whole GS coordinate domain at three strides/wrap.


Framebuffer-address41 was rejected and reverted: final state and all1066guest
times match37,1311shared images retain order, but opening57.5964s. Total raster
12.4892s vs12.6544s37 did not translate to a whole-run gain. Isolated candidate42
adds build-time EE history removal (default ON; local experiment OFF), separately
from rejected forced inlining. Its synthetic disabled-watch fault and branch
checks are being validated; no connected speed claim yet.


Initial source-publication validation: a fresh headless Release build, with no
game-derived output configured, passes all16CTest entries using Python3.11.
This includes87Python cases and the new trace-disabled synthetic execution test.
The first auto-selected Python3.14 run hit sandbox temporary-file permission
errors; the unchanged tests pass with the project's verified3.11 interpreter.
No new Linux or physical-console verification is claimed.


Isolated trace-disabled42 improves the bounded opening segment from51.8379s37
to50.2381s (1066 conversions,35.611639modeled seconds, median46.3ms,~70.9%
modeled rate). Exact guest timestamps, final EE RAM/GS/machine state and all1311
sampled images match37. This is an approximately3.2% measured throughput gain;
physical-console parity and presentation FPS remain unverified. Unlike combined
rejected40, this change only compiles disabled EE history calls out. Default
source configuration keeps tracing ON; local throughput configuration is OFF.
External evidence: speed-opening-replay42.json and startup-clock-bursts-42 files.

### Texture addressing and repeatable input (2026-09-14)

Candidate43 specializes the existing neutral PSMCT32 textured sprite path into
RGB24, retaining live VRAM reads, destination addressing, masks, and pixel order.
All 24 full-VRAM differential cases and four invalid-state cases pass. First
replay43 takes46.9078s versus50.2381s42 for the identical1066 guest CSC timestamps
spanning35.611639 modeled seconds (75.9% modeled rate). GS VRAM, GS metadata and
machine JSON match;1308 shared sampled images retain order. EE RAM differs only
at47e373 (11 to10), an input-related byte previously variable across replays.
Do not claim complete EE state equivalence from this comparison.

Fresh baseline42fresh takes50.3102s. Repeat43fresh takes46.1804s but starts the
opening66.734ms later in guest time and contains1064 events at the60M-slice
cutoff. Its different final state is not an equivalent-run measurement.
Host-timed image-triggered input is an uncontrolled variable. The runner now
accepts explicit ordered input-at events; replay43fixed uses all ten exact
button transitions logged by42fresh. It takes45.7028host seconds (77.9% modeled
rate), and all1066 guest timestamps, full EE RAM, GS VRAM, GS metadata and machine
JSON match42fresh. The initial two shared unique-image positions swap because
the fixed helper begins capture before the older helper's post-prompt start;
the remaining1309 shared images have identical order. This establishes state
equivalence but does not attribute the entire wall-time difference to texture
addressing, because the capture/input helper also changed.
The historical helper measurements remain distinct from fixed-input results.

Candidate44 resolves scalar EE register indices in the emitter, preserving
zero-register discarded-expression evaluation and scalar upper halves. New
synthetic cases cover all32 destinations, aliasing, zero storage, invalid loads,
annulled slots and discarded JALR links. Both translation test configurations and
the Python tools checks pass. Dynamic runtime accessor checks and guest budgets
are unchanged.

Fixed-input replay44 takes39.4124host seconds versus45.7028s43fixed, a13.76%
elapsed reduction. The1066 conversions span35.611639 modeled seconds (90.36%
modeled rate; median36.0ms). Exact guest timestamps, full EE RAM, GS VRAM, GS
metadata and machine JSON match;1315 shared sampled images retain order.
The same input/capture helper and ten recorded events were used for both runs.
Evidence: external speed-opening-43fixed-to-44fixed.json and replay command files.
All22 refreshed CTest entries passed:0failures in12.43s, including both
translation configurations, Python tools, devices/rendering and six startup checks.

Next45 samples the current binary using a regenerated ignored Release linker
map. Adding the map preserves native .text SHA256
dbc37ee676820582ebe7d7fc160763f8d57a43ed47b7fa2217ff2174fe8e8597.
The bounded sampler now uses the observed module size rather than a historical
hard-coded address limit. Sampling runs remain excluded from speed comparisons.

Fresh native profile45 completed normally:3322 samples,0context errors. Largest
named costs include IPU rgba158, CT32 addressing150, State load107/memory106,
raster107, and IOP history60. Sampler host timings are not speed evidence.
Candidate46 extends our existing checked fixed-width RAM access to the exact
16KiB scratchpad window, including full-span quad-store validation. Boundaries,
misalignment, zero destinations, aliases and fault/state parity are compared
against the unchanged general memory methods. All22 refreshed CTest entries pass.
Replay46fixed takes36.2996host seconds for the same35.611639modeled seconds,
1066events (98.10% modeled rate, median33.5ms),7.90% less elapsed than44fixed.
Full EE RAM, GS VRAM/metadata and machine JSON are equal; all1315 captured
images and1066guest timestamps agree. Evidence:speed-opening-44fixed-to-46fixed.json.
No natural-speed or physical-console parity claim is established yet.

Candidate47 tabulates the existing CSC integer contributions for byte inputs
and clips before the final half-bit shift. Non-byte scalar inputs retain the
formula path; FIFO/input consumption is unchanged. Exhaustive24-bit RAW8 color
and threshold-boundary regressions are added. ALL_BUILD succeeds and all22
refreshed CTest entries pass (13.35s), including16,777,216 RAW8 comparisons.
Replay47fixed completes1066 events over35.611639 modeled seconds in36.0054
host seconds (98.91%, median32.9ms),0.81% less elapsed than46fixed. This small
single-run difference is not yet a repeatability claim. All1066 guest timestamps,
full EE RAM, GS VRAM/metadata and machine JSON match46fixed. Captures contain
1315/1316 unique images; all1315 shared images retain order. IOP RAM byte parity
was not asserted. Evidence:speed-opening-46fixed-to-47fixed.json. Natural1:1
playback and physical-console parity remain unestablished. Next: fresh native
profile48 and sustained-cadence analysis before choosing another optimization.

Replay47fixed shorter-window analysis remains below sustained natural speed:
60-event windows range95.68%..101.89%, median98.26%, with12/17 below100%.
The CSC interval p95 is41.6ms, maximum54.3ms. These are conversion events.
Profile48 of47 completes normally (3321 samples,0context errors); CT32 address181,
raster126, rgba106, IDCT99 and IOP history76 are leading named costs. Sampling
perturbs timing and48profile is excluded from throughput comparisons.

Candidate49 splits HG-DIAG-002 instruction history from SIFMAN event snapshots.
The default ON configuration retains history. The local OFF build removes its
per-dispatch call but statically records the same SIF entry/return events.
PC watches reject explicitly when OFF. The full IOP suite is compiled in both
configurations, with new snapshot, pending-load and unsupported-PC checks.
The28-module static inventory is unchanged (94540 reachable words,663 guarded
bindings,147 native adapter sites). ALL_BUILD succeeds and all23 refreshed CTest
entries pass (13.20s), including the full IOP suite with history enabled/disabled.
Replay49fixed takes35.2681host seconds for35.611639modeled seconds (100.97%,
median32.2ms),2.05% less elapsed than47fixed. All1066 timestamps, full EE RAM,
GS VRAM/metadata, machine JSON and all1316 captured images agree. The retained
SIFMAN submissions/return timing and SNDDRV transfer reports are also identical.
IOP RAM byte parity is not asserted. Two-second windows range97.06%..104.70%,
with8/17 below100%; four-second minimum98.81%. Average throughput above100%
does not establish sustained natural cadence. Evidence:speed-opening-47fixed-to-49fixed.json,
speed-windows-49fixed.json and sif-events-47fixed-to-49fixed.json.

Candidate50 addresses a separate host preview bottleneck: the viewer previously
polled for new images only every250ms. It now checks each swap-paced iteration,
releases the file before pixel conversion, skips identical image uploads and
uses texture subimage updates for unchanged dimensions. Existing GL readback
and error checks remain. HG-DIAG-011 optionally records changed-image submission
timestamps separately from swap counts. Synthetic and connected validation are
pending. This changes no guest rendering, timing, waits or instruction budgets.
## Scene address calculation audit (2026-09-19)

Current-map native sampling of the corrected first scene collected3328 samples
with zero context errors. PSMCT32 address calculation accounted for400 (12.0%);
frame/depth/pixel and exact STQ operations dominate the other leading symbols.
The first attempt used an obsolete Sep14 map and is invalid; the repeated sample
uses the Sep19 map rebuilt alongside the executable. Native thread suspension is
diagnostic overhead, so this sampled replay is not a timing benchmark.

Candidate factors our existing PSMCT32 block/column formula into independent
64-entry X and32-entry Y compile-time offset tables. Width validation, 11-bit
coordinate masking, unsigned overflow and final VRAM wrapping remain intact.
No external rendering implementation was consulted. Exhaustive original-formula
comparison covers every2048x2048 coordinate at widths64/512/2048/ffffffc0.
An external20M-call benchmark gives original43.4/44.5ms, candidate28.3/26.0ms,
with matching checksums. Alternating32M baseline/candidate runs completed:
49.910/46.625/49.358/46.464s overall; the30M..32M scene segment took
24.848/23.217/24.813/23.246s. Mean scene cost falls6.44%, overall6.21%.
Images, EE RAM, GS VRAM/draws, VU data/micro/defined masks/registers and all
checkpoints are identical across all four runs. IOP differs only at RTC minute/
second bytes bfcd1/bfcd2. GIF tests including exhaustive mapping checks pass.
Retained. Evidence: TEMP/haunting-toc-probe/address-times.json and
address-equivalence.json; command driver TEMP/hg-address-replays.py.

Next candidate avoids six variable integer divides in ordinary perspective
weighted-sum overflow validation: decoded mantissas are below2^24, so weights
at most INT64_MAX>>24 cannot overflow their product. Larger weights retain
the exact original division check. Signed sums, shifts, exceptional values and
quotient/floor arithmetic are unchanged. Boundary tests exercise both signs,
weights immediately around2^39, a large valid subnormal and real overflow.
Alternating32M runs took46.646/46.359/46.294/46.249s overall and
23.2334/22.9585/23.2038/23.1608s in the scene segment. The mean scene
reduction is0.68%, close enough to host variation that no material speedup is
claimed separately. Retained as a bounded arithmetic simplification: identical
images, EE/GS/VU state and checkpoints; only expected IOP RTC bytes differ.
New boundary tests and the full GIF test executable pass. Evidence:
TEMP/haunting-toc-probe/stq-times.json and stq-equivalence.json.
## Filtered-scene preview rate (2026-09-19)

After faithful bounded bilinear sampling was added, an instrumented33M replay
took88.695s. During31M..33M the external HG-DIAG-006 observer counted61 PPM
publications and60 changed images in40.867s:1.49 publications/s and1.47 changes/s.
These are preview producer observations, not internal game FPS or physical
scanout. The user's earlier~5FPS estimate was never measured.

Skipping zero-weight texture reads and directly sampling exact texel centers
preserves the entire61-image scene sequence, final EE RAM/GS VRAM/draws/VU
data/state; only IOP RTC bytes differ. Native GIF tests and56 synthetic oracle
pixels pass. Retained. Candidate total78.200s, measured scene37.675s,
1.62 publications/s and1.59 changed images/s. This is one before/after pair;
do not overstate statistical precision or call these internal game frames.
External files: scene-linear[-center]-fps.json, scene-linear-center-equivalence.json.
Preview polling/hash overhead is included and not separately isolated.

Post-change current-map sampling (native-profile-after-host.json):3330 samples,
0context errors. CT32 address226, weighted-sum194, bilinear182, texture decode176,
depth/frame pixel159/154, STQ body70. Host sampling ran beside the live game and
must not be used as elapsed-speed evidence. Next candidates are repeated address
and texture setup work; no approximations or missing correctness checks implied.

## Scene slowdown diagnosis, 2026-09-19

Current Release build uses /O2 for GS and has both instruction-history options
OFF. Native OpenGL presentation does not move GS rasterization to the GPU.
At least2356/3330 (70.8%) post-change native samples resolve to named GS graphics
functions; this excludes unidentified helper lambdas and symbols outside the top35.
This identifies software pixel work as a major bottleneck, not missing map calls.
Original rendered draw count rises63644 at30M to861346 at33M; this is primitive
work, not game frames. The recent divide-after2 replay spent44.8798 host seconds
on30M..33M. The issue-slot clock labels that interval3 modeled seconds; its ratio
is not physical-console timing proof. Fresh-image rate and native game-frame rate
must remain separate. User withdrew the missing-entry-text report.

Candidate A factors TEX0/TEXA setup and wraps each needed axis coordinate once
within a bilinear fragment. It reads each required texel/CLUT entry live, including
render-to-texture feedback. No cached texture payload, work skipping or guest clock
change. Relevant synthetic tests and connected alternating comparisons gate it.

Candidate A decision: revert. Before scene42.9333/44.1434s; after42.7344/44.1626s,
mean change0.21%, below run variation. All compared EE/GS/VU and final images
match; IOP seconds/minutes RTC bytes differ as expected. Detailed artifacts:
setup-times.json, setup-equivalence.json. Candidate source/executable preserved
with the baseline backup; failed lesson HG-FAIL-005 records the limited conclusion.

Candidate B reuses the tested destination and goes directly to the shared blend/
frame commit. Both entry points keep their required validation/tests; all alpha-
fail actions, depth writes and masks remain. First comparison matches EE/GS/VU/
image. IOP differs only at bfcd1..bfcd3: RTC payload changes from
00 50 59 21 00 19 09 26 to00 13 01 22 00 19 09 26. The runner explicitly builds
ReadRTC bytes in seconds/minutes/hours order; the extra changed hour is explained
by crossing22:00UTC, not silently excluded as an arbitrary mismatch.

Candidate B retained: before total83.2780/78.1330s and scene49.5443/45.5808s;
after total73.4852/71.0840s and scene42.8382/41.1125s. Mean scene47.56255 ->
41.97535s (11.75% lower; paired13.54%/9.80%). Four final images, EE RAM, GS VRAM/
draw records and VU data/registers/micro/defined masks are byte-identical. All IOP
differences are within the verified RTC seconds/minutes/hour fields. Artifacts:
commit-times.json, commit-equivalence.json and commit-iop-equivalence.json.
No other native game instance was running during these comparisons. These are
headless replay times with matched diagnostic options, not a claimed game FPS.

## Per-draw CT32 sprite preparation (2026-09-19)

Moved raster bodies from gs.hpp to gs.cpp so future changes rebuild the GS unit,
not generated EE shards. Optional GS timing counted all861346 completed draws,
matched guest/image state, and attributed74.24% of full-run raster time to sprites.
The scene30M..33M portion alone had20.895s sprites and11.737s triangle strips.
Checked CT32 sprite preparation retains live texture feedback, row-major writes,
filtering, texture functions, blending, masks, PABE/FBA and color clamp/wrap.
72 scalar-reference cases pass. Four33M replays: before73.434/75.428s total,
43.6655/42.8853s scene; after61.716/62.691s total,32.3042/30.8470s scene.
Scene mean reduction27.04%. All compared images and EE/GS/VU state match.
Artifacts sprite-times.json/sprite-equivalence.json. Subsequent32M timing probe
shows31M..32M triangle strips4.706s, sprites4.067s and other triangles0.107s;
triangle arithmetic is the next target. Profiling runs are attribution, not speed
benchmarks. --profile-gs defaults off and does not change guest state.
# Resident triangle matched comparison (2026-09-19)

Same binary, controlled33M original replay, resident mode/video-rate logging on both
sides; candidate adds --gpu-triangles. Scene pairs22.5453 ->17.7089s and22.3612
->17.5342s. Means22.45325 ->17.62155s (21.52% reduction). All images/EE/GS/VU
equal; IOP differs only at independently identified RTC bytes. External artifacts:
gpu-triangle-pair-times.json, triangle-{control1,gpu1,control2,gpu2}-equivalence.json.
Separate profiled candidate19.0666s:302650 triangles,962triangle batches,
1683totalflushes,0.959s flush,0.564s readback,1.110GB uploaded. Sprite CPU phase
remains8.113s.345 GPU/CPU cases pass resident, including cross-service batches.
30 updated frames/s remains unmet; changed-image output is not internal game FPS.
# Broader sprite-format matched comparison (2026-09-19)

Movement measurement from full once-per-second intervals between the30M and33M
checkpoints (first straddling interval excluded): controls4.288/4.709 changed
images/s; candidates6.222/5.523. This measures presented-image differences, not
an independently instrumented game simulation boundary. External
gpu-movement-rate-comparison.json records counts and interval durations.

Same original33M input/slice schedule, resident+triangle flags and video-rate
logging, saved triangle-only control binary versus broader sprite candidate.
Scene pairs18.4264 ->14.1483s and18.8841 ->14.3099s; mean18.65525 ->14.22910s,
23.73% reduction. Five candidate/control replays preserve compared images/EE/GS/VU;
IOP differences are RTC seconds/minutes.853 resident GPU/CPU cases include all
accepted formats plus alias fallback and missing-palette faults; GIF passes.
Movement intervals are about6 changed images/s, still below30. Extra sprite
coverage increases full-run transfers to3.154GB upload/2.906GB download and
readback to1.931s in the profiled first run. Retain benefit, reprofile next.
Evidence: gpu-sprite-format-pair-times.json, sprite-*-equivalence.json,
sprite-formats-first.log. Separate instrumented replay excluded from timings.

## Bounded VU helper inlining (2026-09-19)

Fresh native samples selected integer multiplication, register/readiness and flag
helpers. First aggressive version also forced compound vector arithmetic and ADD;
MSVC grew to36.75GB private memory, so it was stopped before linking. See
HG-FAIL-007. Narrow candidate retains small helpers and exact MAC bit placement.
Eight focused checks pass, including65536 MAC/STATUS combinations. Executable
33,736,192 ->35,317,760 bytes (+4.69%). Original pairs13.5466 ->12.8638s and
13.2277 ->13.2360s; one extra reverse-order pair12.8191(candidate)/13.3136(control)
resolves whether the initial benefit repeats. Mean13.36263 ->12.97297s (-2.92%).
Retain as a modest workload-scoped gain with material variance; no30fps claim.
All six images/EE/GS/VU states match, IOP RTC only. Artifacts vu-inline-pair-times,
vu-reverse-pair-times and corresponding equivalence reports remain external.

### Accepted opposite-halfword sprite copies — 2026-09-19

Backup: %TEMP%/haunting-toc-probe/lane-copy-baseline. Bounded CT16/16S
in-place sprites now use GPU only when every source is the opposite halfword
of its own unique destination word. No fractional sampling; <=8 distinct columns;
separable address equality and prior frame/depth hazard checks remain.
896 GPU/CPU cases pass resident and synchronous, including required rejections.
Original matched scene controls12.5730/12.5905s ->11.2842/11.0869s;
means12.58175 ->11.18555s,11.10% less elapsed. Captured images and EE/GS/VU
match; IOP only RTC0xbfcd1/2/3. All5888 original alias rejections eliminated.
First candidate48195 sprite draws,2343 flushes,2.950GB uploads,2.772GB downloads,
1.894s readback. Complete-interval changed presented images7.087/7.890 per second
(control6.740/6.823). Counts depend on presentation sampling; not internal game FPS.
Evidence: lane-copy-pair-times.json, lane-after{1,2}-equivalence.json,
lane-movement-rates.json in the external task directory. Retained; reprofile next.

### Exact resident upload comparison — 2026-09-19

Accepted: full-page byte comparison against valid GPU-content mirrors suppresses
unchanged CPU-dirty uploads. GPU writes invalidate mirrors; transfers refresh them.
896 GPU/CPU cases pass resident and synchronous. Four original33M replays match
images/EE/GS/VU; only verified IOP RTC offsets differ. Controls11.3223/11.2996s,
candidates11.0778/11.1080s; means11.31095 ->11.09290s (1.93% less).
First candidate uploads678273024 bytes versus about2.950GB baseline; readback
still1.944s. Extra4MiB mirror plus exact compare/copy CPU work. Modest scene gain,
not30fps or console-fidelity proof. Artifacts upload-compare-pair-times.json,
upload-after{1,2}-equivalence.json; backup upload-compare-baseline outside repo.


### Bulk read-only scanout — 2026-09-19

Moved display_image to gs.cpp and reads committed VRAM through one const data
materialization per image, retaining address/format math and guest pending draws.
901 GPU/CPU checks pass both memory modes, including four wrapped scanout formats
and an uncommitted invalid draw; GIF passes. Four original33M comparisons agree
on images/EE/GS/VU, only verified RTC bytes differ. Scene controls11.0874/11.0056s
versus10.9283/10.9685s; means11.0465 ->10.9484s (0.888% lower, small).
Full replay means33.67749 ->32.20324s (4.38% lower). Readback~1.33-1.35s,
downloads~2.90GB (increased); uploads0.678GB. Retained for repeated full-run gain,
not a strong scene-only speed claim. Backup scanout-bulk-baseline; artifacts
scanout-bulk-pair-times.json and scanout-after{1,2}-equivalence.json externally.


### Fused VU accumulator flag collection — 2026-09-19

Retained: multiply_acc/madd_vector gather temporary MAC/STATUS flags alongside
results and retain original checked-read/commit ordering.49152 frozen-reference
cases pass, plus fpu/gif/runtime/translation and901 GPU cases both modes.
Four original33M states/images match (verified RTC only). Scene controls10.9880/
10.8317s versus10.7528/10.7669s; mean10.90985 ->10.75985s (1.375% lower).
Executable remains35318784 bytes. Modest, variable benefit; not30fps. Artifacts
vu-flags-fused-pair-times.json,vuflags-after{1,2}-equivalence.json; baseline
vu-flags-fused-baseline outside repo. Current linker map confirms Fpu::add and
these VU helpers originate in hg_ee_translated:translated.obj (development /O1).


### Rejected runtime /O2 arithmetic relocation — 2026-09-19

Moving unchanged Fpu::add and five compound VU bodies to a Release MaxSpeed
library preserved source bodies and all checked state. Focused suites/901 GPU
cases both modes/four original replays pass (RTC only). But scene controls
10.7410/10.7936s versus10.9307/10.9589s: means10.76730 ->10.94480s
(1.65% slower). Map verified new-library symbols. Rejected this combined
placement/inlining change; no assertion that /O2 is generally slower. Source
and candidate exe/map preserved in vu-o2-rejected; original accepted inputs
restored from vu-o2-baseline. Rebuild combined with next bounded GS change.


### Inconclusive PSMT8 table relocation — 2026-09-19

50,331,648 address comparisons plus focused suites/901 GPU cases both modes and
four original replays preserve state (RTC only). But scene controls10.7988/10.6680s
versus10.6827/10.8587s; means10.7334 ->10.7707s (+0.35%), one faster/one slower.
Reverted candidate rather than claim benefit. This combined out-of-line/table
change does not disprove the decomposition (already used in DrawTexture). Source
and binary preserved in psmt8-table-rejected; accepted hg_game/map restored from
psmt8-table-baseline. Restored source timestamps refreshed for future rebuild.


### Exact full-lane SSE2 products — 2026-09-19

Retained: guarded integer-only four-lane broadcast products in vu_products.cpp
for full-lane accumulator multiply/MADD and vector MADD, scalar fallback outside
predicate.1M vector product and100000 full-helper external cases pass; integrated
245760 scalar-reference cases,fpu/gif/runtime/translation and901 GPU cases both
modes pass. Four original33M states/images match (RTC only). Controls10.7520/
11.7526s versus10.6471/11.5384s: means11.25230 ->11.09275s (-1.418%).
Both second runs were slower; paired gains0.98%/1.82%, not a fixed absolute rate.
Native executable35323904 bytes. Prototype gains23-53% do not describe game speed.
Artifacts vu-sse2-pair-times.json,vusimd-after{1,2}-equivalence.json,product4-*
and acc-prototype.log; backup vu-sse2-baseline. Static own-AOT counts then found
MUL/MADDA mask15:259 sites,mask14:624; MADD mask15:87,mask14:248. Counts are
static coverage leads, not dynamic execution frequency (vu-static-mask-counts.json).


### Inconclusive XYZ SIMD extension — 2026-09-19

442368 reference cases/direct undefined-W guards and901 GPU cases both modes
pass; four original states/images agree (RTC only). Controls10.9060/10.5211s
versus10.6894/10.7778s; means10.71355 ->10.73360s (+0.19%), paired results
disagree. Reverted XYZ extension, retaining earlier full-lane SIMD. Static site
counts alone did not predict benefit. Candidate code/binary retained externally
in vu-xyz-rejected. Accepted source/exe/map restored from vu-xyz-baseline;
source timestamps refreshed. Artifacts vu-xyz-pair-times/equivalence reports.


## Aligned PSMT8 IMAGE spans (2026-09-19)

Accepted bounded host-transfer optimization:16 aligned pixels in a single row
share8physical words and opposite byte lanes in one16-word column. Compute row
and GS address once,synchronize checked span once,perform8masked paired stores.
Remaining cases retain unchanged scalar path. Same-page observer shortcut now
accepts spans; cross-page path unchanged. No shader arithmetic,clocks or work
changed. Derived from existing independently verified mapping,not external code.
3168 transfer scenarios compare each qword against scalar pixel writes,observer
bounds/fault checks pass,GIF/DMA/runtime pass,and910 GPU/CPU cases pass both modes.
Four original33M replays preserve image,EE RAM,GS VRAM/registers,VU state/data/micro/
defined;IOP differences only verified RTC bytes bfcd1/2/3.
Controls10.8613/10.7998s,candidates9.8051/9.8838s: means10.83055 ->9.84445s,
9.10% less scene time (paired9.72%/8.48%). Full means32.78106 ->31.16580s.
Complete video intervals show8.754/8.759 changed presented images/s vs7.291/7.382;
these are presentation-sampling measurements,not internal game FPS.30target unmet.
Backup psmt8-bulk-baseline;psmt8-bulk-pair-times.json,psmtbulk-after*-equivalence.json,
psmtbulk-movement-rates.json. Preserve existing console-fidelity limitations.

## FPU bit-scan normalization — reverted (2026-09-19)

Replaced repeated left shifts with bounded leading-bit scan.3M+12240 scalar
result/control comparisons and focused suites/910GPU cases pass;all4original
states/images match except known RTC bytes. Synthetic /O1 input sets5-6%faster,
near-cancellation~52%,but original scene timings mixed:10.4448->9.7420s and
9.6214->9.8563s. Means10.03310->9.79915s (-2.33%),dominated by first slow control;
second pair2.44%slower. No reliable scene gain established;reverted to retained
PSMT8 accepted build. Candidate preserved fpu-normalize-rejected,baseline
fpu-normalize-baseline,reports fpu-normalize-pair-times.json/fpunorm-*-equivalence.
No correctness defect identified;revisit only with evidence of frequent costly
normalization in the actual workload. Sources restored/touched;future build must
refresh libraries although restored accepted hg_game.exe/map are immediately usable.

## Displayed-page scanout synchronization (2026-09-19)

Retained checked read_span and physical-page selection for display_image.
216format/rectangle/stride/base cases compare pixels and exact observer page sets;
spans cover coordinate/VRAM wrap and4096-wide images. Memory/GIF/DMA/runtime and
910GPU cases both modes pass. Four original33M captures agree on images,EE/GS/VU;
IOP differences only bfcd1/2/3 RTC bytes. No clocks/work/presentation dropped.
Controls9.8558/9.6641s,candidates9.6999/9.6757s: scene means9.75995->9.68780s
(-0.74%). Paired1.58%faster/0.12%slower: scene benefit small and uncertain.
Full replay means31.03089->30.71498s (-1.02%),both pairs faster.
Downloads2.893/2.900GB->2.763/2.765GB;flushes2396/2399->2337/2339.
Readback1.383/1.344s->1.612/1.599s increases,offsetting reduced flush cost.
Retain narrower faithful ownership synchronization and small full-run benefit;
not a major FPS claim. Backup scanout-pages-baseline,reports scanout-pages-pair-times
and scanpage-after*-equivalence. FPU bit-scan experiment remains reverted.

## MSVC speed optimization of VU/main source (2026-09-19)

Retained source-level Release /O2 for HG_TRANSLATED_SOURCE under the MSVC
fast-build branch. Large EE page shards remain /O1. Actual generated vcxproj
confirms main Optimization=MaxSpeed;1.19MB main contains VU1 program bodies and
checked dispatcher. No generated code/arithmetic/work/timing changes. Compile
observed~10GB private memory and completed normally;exe35,382,784bytes.
910GPU cases both modes pass;four original33M images/EE/GS/VU state match,IOP
only RTC bfcd1/2/3. Controls9.7443/9.9046s,candidates9.2617/9.2294s:
means9.82445->9.24555s (-5.89%,paired4.95%/6.82%). Full replay means31.04963
->30.76895s (-0.90%). Scene gain is stronger than whole-startup gain.
Presentation-sampled changes8.730/9.641s^-1 vs controls8.649/8.548;interval
variation substantial,not internal game FPS.30target unmet. Backup vu-main-o2-baseline;
vu-main-o2-pair-times.json,vumain-after*-equivalence.json,vumain-movement-rates.json.
Unlike rejected helper relocation HG-FAIL-008,this changes complete program call
sites while leaving source and helper placement intact. Other compilers unchanged.

## VIF V4-32 bulk copy — reverted (2026-09-19)

Complete unmasked mode0/CL==WL/in-bounds V4-32 memcpy candidate preserves600scalar
reference configurations and original4states/images (RTC excepted);DMA/runtime and
910GPU checks both modes pass. Controls9.8396/9.2611s,candidates9.2263/9.2730s.
Mean9.55035->9.24965s(-3.15%)is dominated by first slow control;second0.13%slower
and candidate matches earlier accepted~9.25s. No reliable gain;reverted source/
tests/native exe/map. Backup vif-bulk-baseline,rejected candidate vif-bulk-rejected,
reports vif-bulk-pair-times.json/vifbulk-after*-equivalence. Build objects must
refresh before next candidate,restored input timestamps updated.
Repeated first-control variance motivates explicit warm-up of BOTH executables
before future alternating measured pairs;do not count those warm-ups as speed data.

## Packed full-lane SIMD flags — retained (2026-09-19)

runtime/vu_products.cpp now collects UF/OF masks during unchanged exact additions,
extracts zero/sign masks with SSE2 and reverses nibble lane order into MAC. Signed
zero remains zero without negative flag. Fault guards/commit boundaries unchanged.
245760frozen-reference cases plus256zero/sign combinations pass;runtime and910GPU
cases both modes pass. Both binaries warmed before4measured alternating originals.
All4images/EE/GS/VU state equal,IOP only verified RTC offsets. Warm-ups excluded.
Controls9.0484/9.1173s,candidates8.8467/8.8974s: means9.08285->8.87205s(-2.32%,
paired2.23%/2.41%). Full means30.32942->30.11131s(-0.72%).
Changed presented images9.229/9.743s^-1 vs8.986/9.860controls;sampling varies,
not internal game FPS.30target unmet. Backup vu-packed-flags-baseline;reports
vu-packed-flags-pair-times.json,vupack-after*-equivalence,vupack-movement-rates.
Future timing helpers retain both-binary warm-up because prior first-control
variance was material. No external arithmetic/rendering implementation used.


## 2026-09-19 — user-requested early-startup versus gameplay bottleneck audit

Read-only full coverage scan and two bounded native sampling replays; no runtime
changes. Both stop native-iop-budget. Early sample after5M:3258samples/0errors;
five scalar indexed texture functions account618(18.97%),including PSMT4 addressing
148 and bilinear shade155. ntdll424 lacks stack attribution. Gameplay after30M:
3254samples/0errors,Fpu add174,full multiply163,VIF121,637outside-main. GS downloads
2.764GB and1.594s in gameplay-profile run. Sampling suspends/resumes hottest thread;
these runs are excluded from speed comparisons. Not a complete guest call trace.
Coverage summary:4598EE indirect sites,7syscalls,2unsupported CACHE words at26cb74/
26cbb0,1ERET;2769uncompiled table candidates and509member candidates need live-use
and extent proof. IOP all29configured modules,no unconfigured files;121indirect+
4outside-executable residuals,244explicit traps. Missing translations fault; no
observed missing-call fault in bounded replays. Incomplete native service fidelity
and unvisited paths remain distinct concerns. Artifacts startup-coverage-audit,
startup-block-*,early-startup-block-*;summary includes matching binary/map hashes.
XYZscalar remains provisional:controls8.826/8.7659 versus8.8021/8.6720s;
means8.79595->8.73705(-0.67%),all four original state/images match RTC-only.


## Prepared PSMT4 CPU draw sampler retained

Startup5M-30M controls18.49245/18.40903s,candidates17.84835/17.7338s:
mean3.58%less. Whole replay30.185885->29.480315s(-2.34%). Gameplay30M-33M
8.85085->8.84305s(-0.09%,effectivelyflat). Both binaries warmed first;all4original
captures/EE/GS/VU agree,IOP verifiedRTC-only.128synthetic sprite differential cases
cover filters,wrap,CSA,livealiases,masks andpartial fault state;910GPUboth modes
andruntime suites pass. Existing DrawTexture now handles CT32CLUT PSMT4 with
CSA<16 andis used forPSMT4sprites. Original word/nibble addressing retained;
other settings fall back. No claim of improved gameplay FPS orfullconsolefidelity.
Artifacts psmt4-draw-baseline,psmt4-draw-pair-times,psmt4draw-*-equivalence.


## 2026-09-20 — sparse raster ownership rejected

Whole33Mreadback attribution:wholeCPUwrite1393calls,2334957568bytes,1.0986323s;
partialCPUwrite9837calls,80584704bytes,.4600694s;readonly317calls,363560960bytes,
.2082424s. These totals are NOT gameplay-onlycost. OriginalstatesmatchRTC-only.
Sparse8KiBpageaccesscache correctnesspassed memoryfault/ownership tests,910GPUboth
modes and4originalcaptures. Gameplay8.87655->9.5299s(+7.36%);whole29.67116->
31.35295s(+5.67%). Grouped128KiBcommittedGPUdirtyprefetch lowersreadbackpenalty
butstillloses:gameplay8.9007->9.01775s(+1.32%);whole29.617759->30.159122s(+1.83%).
AllfourgroupedstatecomparisonsmatchRTC-only. Bothvariantsreverted;fullscope restored.
Temporaryreadback-originprobe removed. Archives sparse-single-page-rejected,
sparse-grouped-rejected;timings sparse-scope-pair-times,sparsegroup-scope-pair-times.
Livehg_gamebinary/map restoredexactly fromreadback-origin-baseline (preparedPSMT4).
Currentheadless/testobjectsawaitrefreshafterheaderrestoration;do notclaimthemcurrent.

XYZscalar disposition:retain small0.67%warmedscene improvement (bothpairsfaster),
notmajorFPSgain. Only872XYZcalls andonegeneratedincludechanged;295168arithmetic
cases,15emittertests,910GPUbothmodes and4originalstates equalRTC-only. Helperfallbacks
preserve invalid/undefined faultorder,partialwrites,aliases,inactiveW andstickyflags.

## 2026-09-20 � three bounded candidates rejected after warmed comparisons

Every measured replay stopped at native-iop-budget and matched original images,
EE RAM, GS VRAM/registers and VU data/state/micro/defined; IOP differed only at
verified RTC offsets 0xbfcd1/2/3. All910 GPU cases passed both modes. State equality
is not proof of complete console fidelity (HG-DIAG-007/008 remain).

| Candidate | Control scene seconds | Candidate scene seconds | Decision |
|---|---|---|---|
| Fresh command/tile buffer storage | 8.9090 / 8.8766 | 8.9705 / 8.9203 | Revert, 0.59% slower mean |
| Combined VU dependency clock | 8.8356 / 8.8921 | 9.6117 / 9.6280 | Revert, 8.53% slower mean |
| Packed32 VU normalization | 9.5683 / 9.5361 | 9.4325 / 9.5404 | Revert, benefit not repeatable |

Both executables warmed before four alternating runs; warm-ups excluded. Compare
within each experiment; absolute host times changed between experiments. Backups,
rejected binaries/maps, timing JSON and per-run equivalence files are external in
haunting-toc-probe, under command-storage, vu-pair-clock and vu-packed-products.
No retained gain or new FPS claim from these experiments.

Gameplay-only native sample gameplay-cost has3248 samples/0 errors: Fpu::add194,
full multiply182, VIF103, ntdll546. Follow-up DLL exports included thread query and
wait calls. Heuristic512-byte stack scanning found main-image candidates chiefly
in download/flush/cpu_access. This is not a full unwind and does not establish
ignored guest functions, allocation overhead or one definitive largest fix.


## 2026-09-20 � selective MSVC /GL and /LTCG rejected

Generated main and own VU helpers compiled to IR, large EE shards retained /O1.
Focused runtime/arithmetic/memory and910 GPU tests both modes passed. Four original
states/images equal, IOP RTC-only. Warmed scene controls8.9025/9.0140s versus
candidates10.5853/10.5489s:17.96% slower mean. Startup18.250895->17.76108s improved,
but whole30.1276809->31.18553915s worsened. Reverted. No game logic, clocks or
arithmetic changes. Evidence vu-ltcg-pair-times.json and vultcg-* equivalence files.


## 2026-09-20 � bounded raster write ownership retained

1920 per-pixel footprint cases plus memory/observer/fault, arithmetic/runtime and
910 GPU cases both modes pass. Four originals match all images/EE/GS/VU, IOP RTC
only. Warmed controls8.9633/8.9445s versus8.8814/8.9045s; mean0.68% less gameplay
time, whole30.05580955->29.8615458s (-0.65%). A small measured gain, not30FPS.
Readback policy unchanged; bounded dirty output pages replace whole-VRAM marks.
Evidence raster-write-pages-baseline, raster-write-pages-pair-times.json,
rasterpages-* equivalence files. Source gs_raster_pages.hpp and scoped memory.


## 2026-09-20 � bounded input reads and deferred fallback flush rejected

Read narrowing: controls8.8914/8.8404s, candidates8.9223/8.8808s (+0.40% mean).
Combined with resident hazard-driven fallback flush: controls8.8726/8.8403s,
candidates8.8834/8.9098s (+0.45%). Each experiment has4original state/image matches
with RTC-only IOP differences.5760read/1920write footprint checks pass; GPU910 then
914 cases both modes pass. Read narrowing reduced download bytes~2.765GB->1.919GB
but did not improve scene throughput. Both changes reverted to write-only scope.
Four new CPU/GPU ordering fixtures retained. Rejected source/binary/map with hashes:
raster-read-deferred-rejected. Timing/equivalence files have rasterreads/rasterdefer
prefixes. Latest write-only presentation samples8.972/9.105 changed images/s versus
controls8.993/9.354; sampled cadence varies, not internal game FPS. Use matched
fixed-work scene time for the small retained write-only gain.


## 2026-09-20 � exact packed VU full-lane addition retained

AVX2 isolated unit and CPU/OS feature guard; scalar fallback remains. Integer
alignment/add/subtract/flags reproduce scalar Fpu::add. After carry shift m<=2^24-1,
CVTDQ2PS is exact under every rounding mode; extracting its exponent yields exact
normalization shift. Zero is separately masked.16M comparisons over4MXCSR modes
pass with no host FP flags, VU reference cases and914GPU both modes pass.
General five-step integer normalization was0.65% slower and rejected. Refined
controls8.8747/8.8376s versus8.7783/8.7869s:0.83% less gameplay time. Whole
29.7032782->29.6603303s,0.14% lower.4original captures match images/EE/GS/VU and
IOP RTC-only. Other-host performance unverified; this is not full console proof.
Artifacts vu-add4-baseline, vu-add4-general-rejected, vu-add4-exact-pair-times.json,
vuadd4exact-* equivalence files. No timing or rendering changes in this candidate.


## 2026-09-20 � XYZ packed-add extension rejected

Original XYZ validity guards/scalar products with zero-padded add4 preserved all
VU reference checks and4original image/state captures, IOP RTC-only. Warmed scene
controls9.3955/9.4357s versus9.4951/9.4536s: mean9.4156->9.47435s (+0.62%).
Whole times also worse; host startup timing shifted during the experiment, so do
not compare absolute times with previous experiments. Both scene pairs slower;
reverted XYZ source and accepted binary/map with hash verification. Full-lane add4
remains accepted. Archived vu-xyz-add4-rejected; vu-xyz-add4-pair-times.json and
vuxyzadd4-* equivalence files. No new profile has run after this reversion.


## Accepted-build post-add4 gameplay profile - 2026-09-20

One instrumented original33M replay, unchanged deterministic slice inputs, issue-slot
clock and synthetic TOC; --gpu-resident --gpu-triangles --profile-gs --video-rate-log.
External hg-post-add4-gameplay-profile.py invokes hg-post-add4-gameplay-sample.py
once checkpoint30000001 is visible, selecting the busiest native thread by CPU time.
Five-second SuspendThread/GetThreadContext/ResumeThread sample:3295 hits,0 errors;
sampler exit0, replay exit2, dump kind native-iop-budget. This is a perturbed partial
scene profile, not a throughput benchmark, full unwind or inclusive CPU-time census.

| Module | Samples | Share of all samples |
| --- | ---: | ---: |
| hg_game.exe | 2593 | 78.69% |
| ntdll.dll | 595 | 18.06% |
| VCRUNTIME140.dll | 87 | 2.64% |
| nvoglv64.dll | 12 | 0.36% |
| MSVCP140.dll | 4 | 0.12% |
| ucrtbase.dll | 3 | 0.09% |
| KERNELBASE.dll | 1 | 0.03% |

Leading application symbols:full multiply_acc140(4.25%), fpu_add4_avx2 135(4.10%),
VIF process_pending122(3.70%), XYZ prepare<true>74(2.25%), CPU triangle64(1.94%),
Fpu::add62(1.88%), GIF submit_qword53(1.61%), multiply_vector53(1.61%),
XYZ prepare<false>52(1.58%), arithmetic_q52(1.58%), VIF submit_qword50(1.52%),
full madd_vector47(1.43%), decode_gif_packet46(1.40%). Nearest linker symbols
can include inlined callees; sample proportions are not exact execution costs.
DLL totals do not establish GPU wait or allocation causes. Prior heuristic stack
candidates remain leads, not a verified unwind. No startup cost inference is made
from this gameplay sample, and no whole-replay GS total is assigned to the scene.

All8 captured image/EE/GS/VU comparisons against triangle-after2 are byte-equal.
IOP length equal; differences from texture-before1 only0xbfcd1/2/3 (verified RTC).
External artifacts:post-add4-gameplay-native-profile.json, -summary.json,
-equivalence.json, -completion.json, -profile.log and -profile.* under
%TEMP%/haunting-toc-probe. Source/binary/map still match pre-run hashes:
- runtime/vu_xyz.cpp: f468e046c6a02c22419ecb2986525a3e2283c009f22b3035a2c506ef428f072e
- hg_game.exe: b3be7b35136ad96be439079f155e72e62c967a17cb4e807c8f8c1cc0cfedb8de
- hg_game.map: f487e0f7aacac1b1c0d7b6b5560b5e78c271ba0086df1edb1d4e29d7cb6f0fc8
Dirty documentation and accepted products backed up in post-add4-profile-baseline.
No runtime changes, rebuild or speed claim. Latest accepted changed-image rate
remains8.979-9.104/s; internal game FPS unmeasured, target30 unmet.

### Next bounded candidate: qword append capacity checks (not implemented)

Fresh sample contains103 hits(3.13%) in combined VIF/GIF submit_qword. Source in
runtime/include/hg/vif.hpp and gs.hpp appends each byte via push_back before
unchanged packet parsing. Hypothesis: one resize per qword followed by16 explicit
little-endian byte stores removes repeated size/capacity updates. This is separate
from rejected V4-32 UNPACK memcpy (HG-FAIL-012); no new unpack or parser fast path.
Samples include nested/inlined work, so3.13% is only a rough combined-symbol scope,
not expected savings. No proof yet that append instructions dominate these samples.

Implementation scope: Vif1Path::submit_qword and GifPath::submit_qword only. Save
old size, resize by16, then store low/high bytes at the saved offset. Keep portable
explicit shifts; no host-endian casts, speculative pointers, global reserve policy,
packet batching or new public API. Leave submit_dma_tag and all parsing unchanged.
Keep capacity fault checks after complete qword append, and parser invocation once
per qword. Preserve pending bytes, phase, partial state at guest faults, reset,
GS application ordering, explicit unsupported behavior and all guest clocks.
Host allocation failure is not modeled guest behavior; propagate it, never suppress.

Before implementation record proposal/back up dirty headers/tests and preserve the
accepted exe/map. Differential tests compare pending bytes/phase, VU definedness,
GS state and throws with frozen current append routines: capacity growth, empty and
retained partial packets, split VIF payloads, multi-tag GIF/IMAGE/REGLIST, reset,
invalid commands/descriptors and bounded-capacity crossing. Run focused GIF/DMA/
runtime checks and914GPU cases in both modes after clean /m:1 rebuild; stale native
library/test/headless products must not stand in for the accepted binary.
Then warm both executables once and measure control1/candidate1/control2/candidate2
original33M runs with matching options and no competing tests/sampler. Compare all
state/images RTC-only, report startup and scene separately. Retain only repeatable
scene improvement (both pairs faster); otherwise restore candidate-only edits and
record rejection. Do not extend to parser changes merely to rescue this hypothesis.


## Qword append provisional result - 2026-09-20

One resize plus16 little-endian stores in VIF/GIF submit_qword; parser unchanged.
Backup qword-append-baseline, dirty headers/tests/accepted binary/map+manifest.
1285 frozen-append cases,16M arithmetic,1920page,DMA/runtime,914GPU both modes pass.
All4 original captures byte-equal EE/GS/VU/images; IOP only verifiedRTC differences.
2warmups excluded; scene controls10.2509/9.5677,candidates9.3281/9.5305s. Mean
9.9093->9.4293(-4.84%), but pair effects9.00% and0.389% with startup/host drift.
No stable4.84% claim; candidate provisional until confirmation without userplay.
User reported sped-up menus/gameplay and disabled controls in unpaced deterministic
benchmark windows. Normal-paced live session prepared to distinguish settings from
actual timing bug. No guest timing modification made. See external qword-append-*
and qwordappend-*-equivalence.json.
