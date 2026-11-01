## PCSX2 arithmetic profile versus the console (2026-10-25)

Audio note: PCSX2 lets SPU2 sustain rate 0x7F decay slowly; the user's PS2 (STRUCT.BIN) holds it for 2 s, as psx-spx says and as we implement, so PCSX2 is wrong here. It also shows 0x7FFF after attack+decay where the console shows 0x7FF7. Measured directly with the struct_capture SPU2 probe in PCSX2: 0x7FFF -> 0x7FF5 over 2 s at rate 0x7F, and rates 0x7B/0x77 behave the same. SNDDRV ENVX copies therefore differ (0x7FFF ours vs 0x7FE1 PCSX2); not counted as a game-logic divergence (SOURCES.md 2026-10-25).

Measured by running our capture programs (`tools/hwtest/*_capture.c`) in the pinned PCSX2 and comparing with the same programs on the user's PS2 (`docs/SOURCES.md`). PCSX2 (default settings): COP1 ADD/SUB equal the console's one-guard-bit adder; COP1 MUL chops (console: chop or -1); COP1 DIV.S and SQRT.S round to nearest (console: chop or +1); VU0 VADD/VSUB/VMADD take the exact sum and chop, differing from the console in ~27% of adds; VU0 DIV/SQRT (Q) chop. On the console FPU and VU0 results are identical, so PCSX2's FPU/VU split is an emulator setting. Consequences: oracle comparisons drift wherever game logic uses VU0 macro adds (slot 07: Hewie's path spline input, first different at frame 86); use HG-DIAG-126 to mirror PCSX2 when looking for other divergences, and treat hardware captures, not PCSX2, as the reference for float rules.

DebugServer notes from this work: write memchecks catch `sw`/`swc1` stores but did not fire for the VU0 vector (`sqc2`) store of a stack local, so find such values from the consuming instruction's registers and stack (`read_registers`, `read_memory` at sp). A script that single-steps from a memcheck inside kernel interrupt code (0x8000369x) stalled, and PCSX2 then refused savestate loads; restart PCSX2 rather than retrying. Kernel context saves also hit stack memchecks; skip PCs >= 0x80000000. Right after a fresh PCSX2 start, the save-slot load twice reached a different game state (slot 07: Fiona x 52.04 instead of 67.74); a warm, reused instance is stable, so start PCSX2 once per session and let `section_check.py` reuse it (`%TEMP%/pcsx2pid.txt`). With HG-DIAG-126 all 12 save slots match PCSX2 at frame 3000 (Fiona/Hewie bit-identical), and slot 07 matches PCSX2 bit for bit in Fiona/Hewie and in the stalker's position through frame 5000.

## Live PCSX2-MCP debugger oracle (2026-09-21)

The current external observation/debug oracle is the pinned package at
`emu/PCSX2-MCP-v1.0.0-win64/pcsx2-qt.exe`, not the older `emu/PS2 emu/`
folder. Project Link launches only this pinned Haunting Ground USA session.
DebugServer is `127.0.0.1:21512`; PINE identity/status is `127.0.0.1:28011`.
Verified game identity: SLUS-21075, CRC901AAC09, game version1.01; PINE reported
PCSX2 d75a0ad during the integration smoke test.

Working bounded operations include tracked launch/shutdown, pause/resume/continue,
EE/IOP registers, EE/IOP guest-memory reads, native disassembly, execution
breakpoints, memory watchpoints with effective-address metadata, single-step,
GS privileged/display-register capture, tracked-window lossless PNG capture and
PCSX2 logs. Internal GS context registers such as PRIM/FRAME/ZBUF/TEST/ALPHA/TEX0,
GS local-memory reads and GS per-draw capture are not exposed by the current bridge.
No memory/register writes, set-PC, arbitrary MCP passthrough, process-memory access
or emulator execution fallback are permitted. PCSX2 remains an external oracle only;
never copy its renderer code/comments/algorithms or introduce it as a runtime
dependency.

Smoke-test evidence includes the original frame-related writer at EE0x001BEF84
(`sw v1,0x1c(s0)`) with s0=0x004f14c0 and effective write0x004f14dc. A watchpoint
on0x004f14dc stopped at that writer and reported the correct PC/address/access/
opcode/base-register metadata. Temporary execution/single-step breakpoints cleaned
up automatically. Lossless frame capture returned1050x666 PNG data with SHA256 and
EE PC. GS privileged reads returned distinct plausible PMODE/SMODE/DISPFB/DISPLAY/
CSR/IMR values rather than the former repeated-CSR wrapper bug. Use this live
debugger aggressively when a precise original-game behavior question can remove
guesswork; it is not a source for host-side native performance measurements.

## Bilinear UV sampling and wrapping (2026-09-19)

Independent synthetic ELF uploads an8x8 CT32 pattern, then draws56 one-pixel
sprites with fractional UV coordinates and all four wrap modes into a separate
CT32 target. Separate FINISH fences precede GS local-to-host/VIF1 DMA readback.
No game program or external renderer implementation is used by this fixture.
ELF SHA256:52d2981f8a570b2f7b51a532663bf4ccf7a1fb027da3bab667cda21bcfd313ff.
External PCSX2 SHA256:474dc24acf00ee398183235a15c97934bf099c2fa458dcc0cb174750aebe3cff.
Existing oracle configuration unchanged. Three mapped views report completed
stage4, identical stable readbacks,56 matching predictions and200 untouched
sentinel pixels. Our native GS replay of the exact synthetic packets independently
matches all256 downloaded pixels. Capture retained externally under
TEMP/haunting-toc-probe/bilinear-oracle; native check log TEMP/hg-linear-native.out.
Thirteen observer self-tests reject incomplete/unstable/wrong readbacks.

Evidence supports the documented half-texel offset,4-bit UV fractions, weighted
RGBA sum followed by truncation, and independent wrapping of all four neighbors
for these cases. It does not prove physical-console parity, STQ precision,
mixed min/mag selection or mipmapping. The first fixture accidentally used a
15-byte marker in a16-byte slice, shifting its packet; it produced no valid
observation and was corrected before this verified run. Oracle PID14608 was
closed after capture; failed initial PID22720 was also closed.

## GS surplus IMAGE payload probe - 2026-09-13

Synthetic ELF f12bce57ea15548218c71b4a28f6f1fcd4cb9d9ed3ff0e13a183d89c1432ca09,
external gs-surplus-oracle. Independent helper haunting-gs-surplus-probe.py
initializes an8192-byte GS page, uploads32x4 rectangles in CT32,CT16,T8,T4
with exact then doubled payload, and reads each full page back with GS
local-to-host/VIF1 DMA. All eight cases complete at stage4. One complete
consecutive-identical64KiB RAM read; four aliases cannot span the requested
range and are excluded. Exact/doubled page pairs match for all four formats.
Changed bytes from initializer:512,256,128,63 respectively, proving uploads
executed. Manifest, original synthetic packets, observation.json, output and
comparison.json are retained externally. Same oracle executable hash474dc24acf00ee398183235a15c97934bf099c2fa458dcc0cb174750aebe3cff.
Launched PID44816 was closed after capture. No emulator implementation read.

Original replay14 IMAGE tag4f23b0 requests17920qwords, whereas its PSMT4
640x448 rectangle requires8960. Original data/configuration was inspected
independently; no shared rendering reference was consulted. Native transfer
state now distinguishes valid completion from uninitialized/cancelled state.
Surplus payload in the four measured formats leaves VRAM unchanged after
completion. New TRXDIR invalidates this state. Other formats retain explicit
stops. Synthetic tests verify exact/double equality, pre-start/cancel rejection,
and actual writes after restart. This is an observed reference profile, not
physical-console proof or a general transfer-timing claim.

## Non-intra BDEC probe – 2026-09-13

External ipu-nonintra-oracle/probe.elf SHA256 ad9606d2a19a4901a523efa257e6ddaa67fb99871eed0be1705862539676af0a; generator %TEMP%/haunting-ipu-nonintra-probe.py. Same oracle executable474dc24acf00ee398183235a15c97934bf099c2fa458dcc0cb174750aebe3cff; PID39848 closed. One complete stable double-read at stage4; four alias regions could not span the entire output and were excluded, not counted as disagreement. verified.json/output.bin retain all32cases/12288 signed residuals. CBP63, one coefficient per block (zero listed level denotes +1 first-coefficient shortcut), non-intra matrix16/nonlinearQ1/scan0. Existing manifest description inherits obsolete intra wording; the actual generator uses non-intra24010000 and CBP, and is authoritative.

Independent signed quantization floor division, no mismatch correction, direct cosine transform and half-tie tolerance1e-9 agree on12264/12288 samples. Remaining24 differ by one, confined to case29/run38/level37; transform integer-rounding parity remains unresolved. This is explicitly not an exact emulator/physical PS2 match. Truncation toward zero differs on456samples; adding mismatch correction increases differences. Native implementation follows the independently derived transform with measured floor quantization; tests cover all63 valid CBP masks, skipped blocks, signed residual and negative half tie. MPEG1 and field-DCT stay explicit faults. No reference implementation consulted.

## CSC RGB32 synthetic probe – 2026-09-13

External %TEMP%/haunting-toc-probe/ipu-csc-oracle contains probe.elf, manifest.json, verified.json and output.bin; generator haunting-ipu-csc-probe.py. ELF SHA256 a0c340a6248698f11e99341d058d3bf667cd8321dd2925d1b8def0b063813d43. Same oracle executable hash474dc24acf00ee398183235a15c97934bf099c2fa458dcc0cb174750aebe3cff. PID40060 closed. Five stable agreeing stage4 views. Four24-qword RAW8 inputs: Y0..255, pseudorandom chroma seeded2a7110; threshold pairs(0,0),(0,128),(1,256),(64,192). Normal input/output DMA;64 output qwords each.

Independent manual-coefficient calculation matches all1024 RGBA samples when Y-16 clamps at zero and signed green contributions -50*Cb and -104*Cr each floor-divide by64 before addition, followed by floor((sum+1)/2). Literal manual pp205-206 subtracts already-rounded positive contributions; that interpretation differs on700 samples. This discrepancy is explicitly retained as a measured emulator profile, not physical PS2 accuracy. No implementation consulted. Native synthetic tests retain independent examples for rounding/clipping/thresholds and two-block FIFO pressure/empty-input behavior. RGB16, dithering, zero-count and nonaligned CSC remain explicit unsupported cases.

## Movie-copy mask-word probe – 2026-09-13

Original ELF and live RAM contain four executed 0x00ff00ff words at2a2a30..3c, before JR RA. Synthetic ELF SHA256 02b2fe37965d5a9771682bf8b6ddd2ba4b529a309e773cf55c343782d2bf098d; external movie-pack-oracle/verified.json records stable agreeing completion-stage4 views, process9176, executable hash474dc24acf00ee398183235a15c97934bf099c2fa458dcc0cb174750aebe3cff. Process closed after capture. Generator %TEMP%/haunting-movie-pack-probe.py.

Inputs r31=123456789abcdef0/fedcba9876543210, r7=deadbeef. Exact word preserved zero and unrelated r1=777; rd1 variant00ff08ff and canonical DSRA32 control001f10ff both produced 0000000002468acf / ffffffffffdb9753. Supports interpreting the exact original word as DSRA32 with reserved rs ignored in this measured profile. Only exact00ff00ff is accepted by our build-time decoder; other reserved shifts remain faults. No emulator implementation consulted; physical PS2 reserved-encoding behavior unverified.

# Guest memory observations

## 2026-09-13: synthetic intra BDEC output profile

Independent generator %TEMP%/haunting-ipu-bdec-probe.py creates synthetic
MPEG2 intra blocks: DC512/precision2, table1, scan0, nonlinear q1, matrix16,
zero or one signed escape AC per block. No game data or emulator source used.
Directory %TEMP%/haunting-toc-probe/ipu-bdec-oracle retains captures/provenance.
CPU-output v1 stalled at stage10; cpu-v1-observation.json records that failure.
DMA-output v2 ELF857e6ad676a57ab019a545535ad3b745778fa1716a517c740caa5abe5ff766b7
completed eight cases (v2-probe.elf/v2-verified.json/v2-output.bin).
Expanded v3 ELF e6e6a10101faa6cd61f299b712d4536ce6d9029adcb983c68ba19dc53701fd09
completed32 cases, random seed2a1fe4, at stage4. Current manifest.json,
verified.json, output.bin retain exact inputs and observations. One RAM view
supplied two identical full reads; four aliases lacked the complete span and
are excluded. Observer verifies ELF/signature and the same PCSX2 executable
SHA256474dc24acf00ee398183235a15c97934bf099c2fa458dcc0cb174750aebe3cff.
PID16968 (v1),36660 (v2),35604 (v3) are closed; last close was interrupted
then retried after user resumed and verified process identity.

The independently calculated separable mathematical IDCT matches all12288
sampled pixels when intra mismatch correction is omitted and pixels clip to
0..255. With MPEG mismatch correction some values differ by1; negative intra
values are observed as0. Reports mathematical-comparison.json (v2),
no-mismatch-comparison.json and clipped-comparison.json explain differences.
This is a measured emulator profile, NOT physical-console or exhaustive IDCT
parity. Multi-AC rounding, other quantization matrices and non-intra output
are not covered by these probes. Native intra implementation uses this bounded
profile; non-intra, MPEG1 and field-DCT commands currently fault explicitly.

## 2026-09-13: synthetic IPU FDEC and input-buffer accounting

Independent synthetic ELF generator %TEMP%/haunting-ipu-fdec-probe.py writes
bytes0..127, sends CPU qwords to IPU, and records CMD64/TOP64/BP32/CTRL32 in
guest RAM. No game data or emulator implementation was read. Read-only observer
verifies executable identity, ELF hash, signature105f00 and stage4 at105f10,
then reads each result span twice. Both probes supplied five stable agreeing
RAM views. External directory %TEMP%/haunting-toc-probe/ipu-fdec-oracle holds
probe-v1.elf/verified-v1.json and probe-v2.elf/verified-v2.json; current
probe.elf/manifest.json/observation.json are v2. PCSX2 executable SHA256
474dc24acf00ee398183235a15c97934bf099c2fa458dcc0cb174750aebe3cff, same portable
installation/profile as the earlier probes. V1 PID33196 was closed; v2 PID31460
was closed before pausing. No oracle remains running.

V1 ELF SHA256 b2131dea25a2c9ea6e19886001d18eb099efab452569f6c47ffd5e19e13a7dbf;
capture2026-09-13T14:18:40.091311+00:00. V2 ELF SHA256
597f9b3cfd71aca4590963ca42d2a633e849c9085dad90aa67f68842311f6faa;
capture2026-09-13T10:45:29.372845-05:00. This is reference-profile evidence,
not physical PS2 parity or cycle-accurate timing validation.

After reset+BCLR0+eight qwords, BP10700/CTRL7 means one internal qword and
seven FIFO qwords. FDEC FB sequence0,0,1,7,24,32,32,31,1,32,32 returns
00010203,00010203,00020406,01020304,04050607,08090a0b,0c0d0e0f,
88088909,10111213,14151617,18191a1b. TOP agrees. Cumulative positions are
0,0,1,8,32,64,96,127,128,160,192: only FB advances, extraction is MSB first.
At127 BP2067f (FP2/IFC6); at128 BP10600 (FP1/IFC6).
BCLR0 then FDEC0 without input sets busy in CMD/TOP bit63 and CTRL31, retaining
old low results. One qword resolves it to00010203, BP10000/CTRL0.
BCLR120 plus one qword and FDEC0 remains busy atBP10078; a second qword
resolves0f101112, BP20078. These support explicit pending-input handling.

V2 also measured a discrepancy requiring care: BCLR120, FDEC32 with no input
reports BP18 busy; one incoming qword gives BP118 still busy; the second yields
03040506, BP10118/CTRL1. A straightforward stream interpretation would skip
the first qword and yield13141516. Do not blindly reproduce this apparent
reference-profile edge behavior; resolve it or keep that case explicitly
unsupported. With eight qwords at BCLR0, SETIQ consumes four and leaves
BP10300/CTRL3, then a second SETIQ leaves BP0/CTRL0. TOP remains unchanged;
CMD low after table commands is not a specified FDEC result and must not be
used to infer decoding behavior. Reset-only prefetch (without BCLR) and
nonzero-offset table consumption were not independently measured.

Implementation should derive from EE Users Manual chapter8: eight-qword FIFO,
FP internal qwords distinct from IFC, remaining bits=(IFC+FP)*128-BP;
FDEC FB0..32 advances only FB and requires32 following bits. Existing ipu.hpp
still has only FIFO/table setup and lacks FDEC/internal-buffer modeling.

## 2026-09-13: positive-normal SQRT batch

The automated connected replay startup-sqrt-supported cleared1c6c64, then
stopped at1c6d20 on another inexact SQRT in the same original routine. To avoid
per-input replay/patch cycles, independently generated a synthetic 2048-input
MIPS loop using positive normal values, exponent boundaries and deterministic
random seed1c6d20. No emulator implementation was read.

External evidence: %TEMP%/haunting-toc-probe/fpu-sqrt-batch/{manifest.json,
sqrt.elf,observation.json,verified.json}; generator %TEMP%/haunting-sqrt-batch.py.
ELF SHA256 ef3f70d8ea8a89a72147ee4b4b03539183219717a0627f2fa1698b22985e0b9b.
PCSX2 executable hash/profile is the same as the earlier SQRT observation.
PID42640 completed stage4; one RAM view supplied two identical complete2048-row
reads. Four alias views could not supply the complete span and were excluded,
not counted as agreement. All2048 results match independently computed integer
nearest rounding. The process was closed after measurement.

Implementation compares integer radicand-root^2 with root (the squared midpoint
is root^2+root+1/4, so no tie is possible), and handles significand carry.
Negative normal and exponent255 inputs still fail explicitly. Existing
exponent-zero behavior remains. This validates the measured emulator reference
profile, NOT physical-console 1:1 rounding: EE User Manual p164 says rounding
toward zero with possible least-significant-bit differences from IEEE zero
rounding. Hardware-level rounding parity remains unverified.

## 2026-09-13: bounded positive SQRT profile

At06:47:18.465908UTC, owned PID36444 yielded five stable agreeing stage4 RAM
views of26 synthetic SQRT/control results. External fpu-sqrt-oracle/verified.json
retains each view, exact operands and controls. ELF SHA256
d58de526e2ba3ce53e992bfb8104f9baef34912665b186742264ad03291a1059;
executable474dc24acf00ee398183235a15c97934bf099c2fa458dcc0cb174750aebe3cff,
same isolated portable installation/BIOS as indexed sampling below. Inputs are
13 values under initial FCR0 and3c078. Only synthetic guest RAM was read;
no emulator implementation was consulted. Successful process was stopped afterward.

sqrt(2.0)=3fb504f3 and sqrt(0.5)=3f3504f3. Positive results clear I/D cause
bits while retaining U/O and sticky flags: seeded3c078 produces FCR0100c079.
This validates the power-of-two normal-input significand profile needed by
original1c6c50..64 (ft22=40000000), not arbitrary SQRT rounding.
sqrt(5)=400f1bbd is above a simply truncated result, so that profile stays
unsupported. Observed negative zero becomes positive zero with invalid flag,
in conflict with the manual's signed-zero wording; existing signed-zero behavior
was not broadened or changed from this observation. No physical-console claim.

Probe writes signature at105f00, completion4 at105f10 and26 output/control
pairs at106000, then spins. Manifest, ELF and observer are external. Initial
low uncached-address variant faulted with TLB misses and is retained as invalid;
it is not result evidence. PCSX2 on this setup rejected forward-slash launch
paths as nonexistent; native Windows backslashes succeeded.

## 2026-09-12: complete indexed sampling observation through one-pixel sprites

At2026-09-12T17:18:40.594034UTC the read-only observer found three complete,
stable stage4 readbacks in owned PID27892 at guest-mapping bases0x2a17edf0000,
0x2a19edf0000 and0x7ff770000000. All56 expected framebuffer colors match,
and all200 undrawn pixels remain44556677. Result: passed:true, observer exit0.
Evidence is external indexed-stride-oracle/sampling-verified.json, with the
current generator, ELF, packet stream and manifest beside it. ELF SHA256:
a56cdc58624680f6d6e52df37c9caca5744a4299b7df20c23506cf26868df699.
Executable and BIOS identities remain those recorded below. The1674-qword probe
uses58 physical CT32 uploads,14 indexed texture cases and56 one-pixel sprites;
1668 qwords precede the drawing FINISH wait, followed by6 transfer-setup qwords
and a second FINISH wait. Signature/stage/readback guest addresses are unchanged.
All56 source coordinates, destination pixels and expected colors are identical
to the original point probe. Constant UV at both sprite corners selects the
same texel; Sony GS manual printedp46 defines the one-pixel rectangle coverage.
The exact owned process was stopped through explicit escalation after capture.

Observed palette indices, four samples per case, hexadecimal:

| TBW | PSMT8 | PSMT4 |
| --- | --- | --- |
| 1 | 75,75,75,97 | d,d,d,e |
| 2 | 31,53,97,97 | 9,b,e,e |
| 3 | 31,53,97,97 | 9,b,e,e |
| 10 | 31,53,75,97 | 9,b,d,e |
| 11 | 31,53,75,97 | 9,b,d,e |
| 30 | 31,53,75,97 | 9,b,d,e |
| 31 | 31,53,75,97 | 9,b,d,e |

The measurements establish the indexed whole-page sampling stride and aliases
for these profiles in this memory oracle, not physical-console fidelity or
game movie playback. Runtime regression markers use fixed physical offsets
and the independent colors above rather than computing expected addresses
through the accessor under test. No emulator implementation was consulted.

Two preserved negative experiments preceded this successful observation:
draw-fence-* files record ELF44ba90d2d226506af45b36289ab50bedd2dcfee96e63afa68b9e3a25f07d282a,
PID53816, observed17:13:45Z, still missing pixel55 despite the extra FINISH.
swapped-* files record ELFd17d9e695dcf9f6e04aec26208c42e4c8f3d8284cd36f6a589a46c9d4aa8f68f,
PID31332, observed17:16:27Z. Reversing the final two point submissions still
missed the same spatial pixel55, so a lost final command is not established.
Both had three stable stage4 views and all sentinels intact; both remain FAIL.
Both owned processes were stopped. The original point-drawing discrepancy
remains unexplained; the successful sprite observation does not resolve it.

## 2026-09-12: sampling launch approved; final sample missing

The former approval-capacity rejection below is historical. Explicit escalation
launched the isolated synthetic ELF at2026-09-12T07:06:46.1096843Z, owned
PID33332. The read-only observer executed at07:06:54.828759UTC. It checked60442
candidate allocation/region bases and found three complete, stable stage4 views
at0x269f76e0000,0x26a176e0000,0x7ff770000000. Their readbacks agree.

Pixels0..54 match the existing manifest. Pixel55 (last PSMT4/TBW31 point)
is44556677, the undrawn sentinel, instead of8054f10e. All24 even-width control
samples pass; pixels56..255 remain sentinel. The observer returns passed:false.
Do not accept55 samples or claim this establishes the complete sampling test.
Possible draw/readback synchronization is a hypothesis, not a proven cause.

Preserved external evidence: indexed-stride-oracle/
sampling-first-incomplete-final-pixel.json and
indexed-sampling-before-draw-fence.elf. ELF SHA256:
3583dcfef65ca5817c6defa4fb081eb459e500bdd66c024b505828cd1701ec28.
Executable SHA256:
474dc24acf00ee398183235a15c97934bf099c2fa458dcc0cb174750aebe3cff.
BIOS identity remains the one recorded below. SignatureHG_TBW_SAMPLE_01 at
guest0x100f00, stage at0x100f10,1024-byte framebuffer readback at0x101000.
The exact owned process was stopped with explicit escalation after default
cleanup was denied; sampling-pid.txt is stale. No emulator implementation was
read or copied. Only independently generated synthetic guest memory was read.

The proposed next experiment separates a drawing FINISH/wait from a subsequent
readback-setup FINISH/wait. Sony GS User's Manual v6.0 printedpp77/95 were
visually rechecked: local-to-host setup precedes its FINISH/BUSDIR switch, and
FINISH also provides drawing completion ordering. These support testing the
sequence but do not prove why the earlier final pixel is missing.

## 2026-09-12: indexed transfer stride extremes and aliases

The independently assembled `indexed-stride-extremes.elf` submits528 GIF
qwords/24 uploads, repeating the four256-byte tiles below with DBW1,3,31.
ELF SHA256 `6e0f3e0a37c4c93b82ae025ccca3c8d8622d504ce0db35b2e069607e6f572156`.
PCSX2 executable/BIOS identities are the same as the DBW11 observation below.
At2026-09-12T06:09:33.330587UTC, read-only checks of owned PID23948 verified
each known synthetic256-byte marker twice in four4MiB non-executable mappings.
`indexed-stride-oracle/extremes-verified.json` records their addresses and results;
the ELF, manifest and logs remain in the same external temporary directory.

Final physical byte offsets and repeated marker bytes, all hexadecimal:

| DBW | PSMT8 offsets:bytes | PSMT4 offsets:bytes |
| --- | --- | --- |
| 1 | 0:75,2000:97 | 80000:dd,82000:ee |
| 3 | 100000:31,102000:53,104000:97 | 180000:99,182000:bb,184000:ee |
| 31 | 200000:31,21e000:53,23c000:75,220000:97 | 280000:99,29e000:bb,2bc000:dd,2a0000:ee |

These observations establish zero, one and fifteen complete8KiB pages per row
for these transfers. With DBW1 the third tile overwrites the first two; with
DBW3 the fourth overwrites the third. Synthetic GIF regressions replay the
uploads, check every word of each physical block both immediately and after
aliasing writes, preserve adjacent sentinels, and exercise local-copy reads.
They do not establish odd TEX0 sampling or physical-console behavior.

## 2026-09-12: sampling probe offline validation (not an oracle result)

The generated ELF and packet bytes remain unchanged. An independent packet
walk verified the ELF payload extent,1616 qwords,404 A+D writes,58 IMAGE
payloads and14 TEX0 cases. The local-to-host trailer follows the FINISH/CSR/
BUSDIR sequence in Sony GS User's Manual v6.0 printedp77; that page and the
BUSDIR register onp144 were visually checked against the original manual.

External `sampling_control_replay.cpp` and `.vcxproj` build a native checker
using this project's own GS implementation. It replays the prepared packet
stream, omitting only the32 odd-width draw kicks and stopping before the
unimplemented local-to-host transfer. Six even-width controls (PSMT8/PSMT4,
TBW2,10,30) produce all24 independently expected colors; the remaining232
framebuffer pixels retain their sentinel. Native build and execution pass;
`sampling-control-replay.json` contains the result. This is a probe-preparation
check against our model, not evidence for odd TEX0 sampling or DMA readback.

`observe_indexed_sampling.py` now considers allocation bases as well as region
bases, so split guest-RAM mappings are not excluded by the former large-region
size test. Before reading process data it verifies the saved PID's executable
path plus the recorded executable and ELF hashes; each fixed read also checks
that its target is readable, non-executable and not guarded. Only stage4
readbacks with repeatable expected pixels and intact sentinels can pass.
`--self-test` opens no process and passes13 checks for accepted complete data,
missing/short/incorrect/unstable/inconsistent data, stage0/3 rejection, split
allocation candidates and excluded executable/guarded mappings. Results are
in `sampling-observer-selftest.json`. Live observation remains untested.

After verifying the isolated paths and input identities, a renewed explicit
launch approval was rejected with the same automatic-review usage-limit error.
It reported September15,2026 at8:02PM as the retry time without specifying a
timezone. No alternate launch followed. No oracle process is active and no new
sampling observation exists. Resume the documented launch only when approval
can succeed; do not treat the offline checks as the missing measurement.

### Subsequent continuation check, 2026-09-12T06:59:49Z

The prior segment's final06:57:23Z record reports another approval-capacity
rejection, zero owned probe processes, and no new validated-launch log.
This continuation did not retry the rejected launch or execute the observer's
live path. A fresh `Get-Process` check found no `pcsx2-qt`,
`hg_system_diagnostic`, or `MSBuild` process; the expected external
`pcsx2-sampling-validated.log` was still absent. A CIM ownership query was
denied, so its empty output is not evidence of a successful process check.
Reading the prepared generator and observer produced no new sampling
observation. Approval capacity remains the missing prerequisite; the earlier
offline results and all pending-evidence limitations above are unchanged.

## Pending: independent odd TEX0 sampling probe (not observed)

`%TEMP%/haunting-toc-probe/indexed-stride-oracle/make_indexed_sampling.py`
generates `indexed-sampling.elf`, `sampling-packets.bin` and
`sampling-manifest.json`. ELF SHA256:
`3583dcfef65ca5817c6defa4fb081eb459e500bdd66c024b505828cd1701ec28`.
The1616-qword packet first uploads constant CT32 blocks at chosen physical
addresses, then samples them as PSMT8/PSMT4 with TBW1,2,3,10,11,30,31.
A known CSM1 palette maps the indices to56 distinguishable CT32 framebuffer
pixels. The EE program requests GS local-to-host/VIF1 DMA readback to0x101000.
The synthetic signature is at0x100f00; stage4 at0x100f10 would indicate DMA
completion. `observe_indexed_sampling.py` reads only the known synthetic data
through a read-only handle; stage4, stable pixels, correct even-width controls,
and expected odd-width pixels must all be checked before accepting evidence.

The first launch (owned PID22588) stopped at the settings-path error without
executing the ELF. Its observation JSON contains no matched guest mapping.
The process was stopped. The corrected isolated portable launch was blocked;
an explicit escalation was rejected because automatic approval review had hit
the Codex usage limit. No workaround was attempted after that rejection, no
oracle process remains active, and no sampling outcome is claimed. The runtime
odd indexed TEX0 guard remains in place pending successful observation.

## 2026-09-12: independently generated odd indexed transfer stride

Synthetic ELF, independently assembled from EE instructions and GS register
descriptions; no game program or emulator implementation supplied this probe.
PCSX2 v2.6.3 executable SHA256
`474dc24acf00ee398183235a15c97934bf099c2fa458dcc0cb174750aebe3cff`;
ELF SHA256 `6303068fca5aa5f9fdfa903cd1fc0b1bb48bedcebb06cbe3c6acb11b5eaf8bbc`.
The user's SCPH-39001 BIOS SHA256 is
`f4c948e61a291d4b3f92a141e550cf8357204287a31ff784caccbedaef910c9d`.
Probe ELF, packet bytes, manifest, logs and observation JSON remain external at
`%TEMP%/haunting-toc-probe/indexed-stride-oracle/`.

The program submits176 GIF DMA qwords containing eight host-to-local uploads.
PSMT8: DBP0x800, DBW11,16x16 tiles at(0,0),(0,64),(0,128),(128,64),
filled respectively with bytes31,53,75,97 hex. PSMT4: DBP0x1800,DBW11,
32x16 tiles at(0,0),(0,128),(0,256),(128,128), filled99,bb,dd,ee hex.
At06:05:56UTC, read-only non-executable memory inspection of probe PID5900
found four4MiB mappings with the same complete256-byte marker blocks.
Every block was checked twice against the synthetic input. Observed VRAM byte
offsets: PSMT8 0x80000,0x8a000,0x94000,0x8c000; PSMT4
0x180000,0x18a000,0x194000,0x18c000. Source-packet copies were distinguished
by their352-byte spacing and are not the VRAM evidence. Exact mapping addresses
and process/ELF identities are in `verified-observation.json`.

Thus DBW11 advances five8KiB pages per indexed page row, including the second
row; it does not round up or alternate half-page increments. The independently
implemented address calculation retains whole pages (`buffer_width/128`).
BITBLTBUF widths are validated in64-pixel units; TEX0's documented128-pixel
indexed texture-width alignment stays separate. Synthetic regression tests
assert these physical offsets directly and exercise a cross-page local copy.
This is a memory-oracle observation, not a physical-console verification or
general proof of all GS transfer modes. No emulator source/algorithm was read.
The probe ran in an external portable copy because the first launch could not
write its normal settings path; the user's existing settings were not edited.

## 2026-09-04: RPC ready and IOP reply padding

PCSX2 paused at original syscall return0x26c5a8 after RPC initialization.
SifSetReg inputs: id0x80000002,value1; return v0=1. This directly verifies the
value1 software-register case; general native software-register assignment
returns its assigned value as a compatibility policy.
External capture: `C:/Users/johnn/AppData/Local/Temp/haunting-oracle/sif-rpc-ready-20260904.p2s`.
SHA256 `8b6e2ae3eb08cfe381a08e403531f27c0855f9851756f1c75f0f8aa8f49c6dea`.

IOP source tag at0x1c2bc is word-aligned, not qword-aligned. It specifies six
words from0x1e930, EE CNT/IRQ QWC2, destination0x1989480. Completed IOP MADR
is0x1e948 (24 bytes advanced), TADR0x1c2cc, CHCR0x701. EE MADR advanced32 bytes.
The final two EE words equal the upper two lanes of the preceding payload qword,
and differ from the source words beyond the six-word extent. This validates
retained-lane padding, not a read past the declared source or zero padding.
Native code supports that two-word fragment after a full qword; one-/three-word
fragments and sub-qword-only packets remain unsupported until validated.
The packet's first byte is cleared by the original EE handler after receipt.

## 2026-09-04: first EE SIF DMA submission

PCSX2 v2.6.3 paused at original executable return breakpoint0x0026c568.
External capture: `C:/Users/johnn/AppData/Local/Temp/haunting-oracle/sif-first-dma-20260904.p2s`.
SHA256 `08b225ceaa14c4839278bc6027ea8d2c5cf76f39f58d2c86cf71f7e40b4b8700`.
Only guest RAM and hardware-register data were inspected; no emulator source or
captured BIOS implementation was decoded/copied. Python3.14 reads the Zstd ZIP.

EE channel6 completed with CHCR0x84, QWC0, MADR0x21a70, TADR0x212a0.
The preceding source tag at0x21290 is refe, QWC3, source0x21a40.
Its first qword contains IOP destination0x1e640 with end/IRQ bits31/30 and
word count8; the upper64 bits are zero. The following two qwords match the
IOP destination bytes exactly. IOP channel10 has MADR0x1e660, BCR0x20,
CHCR0x40000300 after completion. The original command size is20 bytes; the
transfer includes32 payload bytes, including source padding. Native tests use
invented addresses/data and validate this structural interpretation.

IOP source-tag interpretation separately comes from the user's ROM_SIFMAN
offset0x544 builder and offset0x610 queue/start function: source/IRQ/end,
word count, EE CNT tag/QWC, destination. EE source/destination tag behavior
comes from the EE User's Manual, not from the capture.

## 2026-09-04: USA SetupThread return

PCSX2 v2.6.3, user's linked Haunting Ground game, reset and pause at execution
breakpoint 0x001001cc. Input executable digest is in config/haunting_ground_us.toml.
Guest RAM was identified by comparing three 64-byte samples to the local ELF,
then captured with a read-only Windows process handle. No emulator code was read
or transcribed. No game/BIOS code from the capture is used as translated input.

Capture and JSON metadata are in OS temp, basename
`haunting-oracle/ee-ram-20260904T231224539237Z.bin`; SHA-256:
`b79ae856c5d8fa068f2cb8233c53097696685ca68a4ca6504c060ccf709a1b3f`.

- Return register: 0x01ffed60. Another volatile register held 0x01ff7000, consistent
  with a stack base, but that interpretation needs broader verification.
- Argument block at 0x019709c0 holds count 1 and an argv table beginning at +4.
  Its first string starts at +68; the next pointer is null. The string agrees with
  the executable boot path in SYSTEM.CNF. The native implementation constructs
  this layout rather than copying captured bytes.
- The BSS clear range is `[0x0047b200,0x01992000)`. At this post-kernel boundary,
  its only nonzero bytes were those in the populated argument block (26 bytes).

The native stack ceiling 0x01fff000 is a placement-profile inference, not a claim
that all BIOS revisions choose that address. The native heap boundary follows its
own reserved stack range; compare EndOfHeap later if gameplay depends on it.

Capture tool pitfall: EE RAM mappings can split into 4 KiB read-only pages and
write-combined regions. Scan readable non-executable pages, mask protection flags,
and verify multiple game samples. Several mirror addresses can identify the same
RAM; never assume a host base persists between process launches.

## EnableIntc first enable (2026-09-04 18:42 America/Chicago)
PCSX2 2.6.3, same process/session. Execute breakpoint at independently decoded
post-syscall address 0x0026bee8. Observed GPR v0=1, a0=11, v1=0xffffffffb000f010.
Only first-enable result validated; already-enabled and error returns are unknown.
No capture bytes or emulator implementation used for this service. Native INTC
mask writes follow EE User's Manual pp30-31. Debugger remains paused there.

## ChangeThreadPriority success (2026-09-04 18:48 America/Chicago)
Execute breakpoint at independently decoded 0x0026c038 after syscall 0x29.
GPR v0=0, a0=1 (running thread), a1=1 (requested priority). This validates
the startup transition from priority zero returning zero, not a universal success
value. Original game code at 0x1cafa8 saves the returned previous priority and
restores it at 0x1cb030 (independently decoded 2026-09-09).
Debugger was paused here with three breakpoints:
0x001001cc, 0x0026bee8, 0x0026c038. No new capture or emulator code inspected.

## Console settings (2026-09-04 18:49 America/Chicago)
Paused at 0x0026c258 after first GetOsdConfigParam, destination a0=0x01ffece0.
Read-only EE RAM capture `C:/Users/johnn/AppData/Local/Temp/haunting-oracle/ee-ram-20260904T234911830789Z.bin`,
SHA-256 `cbd783d57bc43e67a58bfe1c810838e0ed3c5a58ea50de8fa47c7f049a0bac05`.
Decoded public ConfigParam fields: SPDIF enabled, 4:3, RGB, non-Japanese flag 1,
PS1 driver 0, version 1, language English (1), timezone field 270. The raw capture
is not embedded. Native ConsoleSettings constructs fields and deliberately defaults
to UTC (timezone 0); language/display defaults agree with the observed launch.
Query/set services preserve typed native settings, not console implementation.
PCSX2 remains paused here; four execution breakpoints: 0x001001cc, 0x0026bee8,
0x0026c038, 0x0026c258. No image files saved.

## SIF receive setup (2026-09-04 19:21 America/Chicago)
Same game/session, breakpoint 0x0026c588 after syscall 0x78. Observed v0=0x184,
a0=0x184, v1 low32=0xb000c000. These are register observations, not a full MMIO
capture: navigating memory view to 0x1000c000 displayed ??, so no channel-memory
value was established by that view. Public kernel ABI independently documents
destination-chain SIF0 enable with TIE/STR and a void return. Native code constructs
those fields and preserves v0 as unspecified for the void ABI; it does not copy
incidental volatile register contents. The DMA receiver is independently derived
from EE User's Manual pp48/61 and accepts actual payload, without an IOP producer yet.
PCSX2 is paused here. Breakpoint 0x0026c258 disabled because settings calls repeat;
0x001001cc, 0x0026bee8, 0x0026c038 and 0x0026c588 remain enabled. No capture/image saved.

## DMA handler and enable (2026-09-04 19:27 America/Chicago)
Stepped over the game's calls selected from our own ELF decoding, without entering
kernel implementation. At 0x0026f190 after AddDmacHandler channel 5, v0=3 (opaque
handler ID; native first DMA handler is ID 1 and retains a separate metadata table).
At 0x0026f1a0 after the game's EnableDmac wrapper, v0=1, a0=5, v1=0x00200000.
Our decoded wrapper preserves the syscall result across interrupt restoration.
Native first enable sets channel 5's mask and returns 1; repeated-enable ABI remains
unvalidated. Current debugger pause is 0x0026f1a0. No image/RAM capture saved.
# IOP processor and platform probes (2026-09-04)

Paused guest-state archive saved through PCSX2's UI, outside the repository:
`C:/Users/johnn/AppData/Local/Temp/haunting-oracle/iop-registers-20260904.p2s`.
SHA256 `cf7d646f103021beb20a104de1c0e4ba337ad72070e3cbff9202e479507ca69c`.
The serialized guest-register data was located by comparing IOP GPR values to the
debugger, without reading emulator code or serialization implementation. GPR block
offset0x552, followed by HI/LO and COP0 block0x5da; COP0 register15 is0x1f.
Python3.14 reads the archive's Zstandard compression; Python3.11 does not.
`iopHwRegs.bin` offset0x1450 reads1. IOP memory view at0x1d000060 reads0; the EE
hardware backing snapshot at0xf260 reads0xff, so these are not assumed identical.
These observations establish a native read-only startup probe profile, not general
MMIO behavior. Writes still fault; no SIF ready flags or command buffers are copied.
Oracle remained paused at EE0x26f1a0 / IOP0x120c4. No capture bytes enter the runtime.

## Archived CDVD state check (2026-09-05)

The three existing external savestates (`iop-registers`, `sif-first-dma`, and
`sif-rpc-ready`) were read with Python 3.14's Zstandard ZIP support only. At IOP
hardware offsets corresponding to CDVD current status, sticky status, current
N-command and I_STAT (`0x200a`, `0x200b`, `0x2004`, `0x2008`), all values are
zero in each snapshot. The later native CDVDMAN DMA destination `0x000c3464`
contains its preexisting `0x0000400d` fill pattern in each snapshot. These states
therefore predate the original GetToc request and provide no TOC payload. No
emulator serialization or implementation was inspected; this is a negative
provenance result only.

## Live ISO startup capture (2026-09-05)

PCSX2 v2.6.3 was launched against the user-local ISO through its documented
command line and inspected only with `tools/oracle_capture.py`. The tool matched
the executable fingerprints before and after the read-only 32 MiB EE RAM capture:
`haunting-oracle/ee-ram-20260905T070936748799Z.bin`, SHA-256
`0cfed09190338323938e63c38ecf1688caf5fddcd7aababb72b01eeb58c94c85`.
The process could not be paused through the available host controls, so this is
not a temporally consistent capture and cannot establish a register ABI or TOC
record. It does contain the in-flight `cdrom0:\MODULES\SIO2MAN.IRX` request,
which only confirms that the original launch reached the same module-load stage.
No capture byte is incorporated into source or runtime behavior.

`tools/oracle_capture.py --suspend` now opens the process with the Windows
suspend/resume right in addition to VM-read/query, suspends it before fingerprint
validation and capture, and resumes it in `finally`. It performs no process or
guest-memory write. This makes one bounded capture coherent without relying on
the debugger UI; use it only for an explicitly scoped observation and retain the
capture hash and state description here.

The helper was exercised against a fresh v2.6.3 process (PID 53232) and produced
two coherent 32 MiB EE captures outside the repository:
`ee-ram-20260905T071406292896Z.bin`
(`e127a376c8d08db7c6f4db5fae10754293240a5c67a27dee736f5a8a5adcc00e`) and,
after 30 seconds,
`ee-ram-20260905T071456581210Z.bin`
(`213628c306ffe545ae25b96fa127f46a40a444abdc476e7501a977fc15afdc23`).
Neither contains the full SIO2MAN request path, so neither establishes the CDVD
operation or TOC record. The process was then stopped; no emulator source or
implementation was inspected.

A later fresh `-nogui -unlimited` launch (PID 47424) remained at 0.09375 CPU
seconds after 55 seconds and produced no new oracle artifact. It was stopped by
the launcher. This is negative launch evidence only: it neither supplies a TOC
payload nor changes the native runtime contract.

A second bounded launch added the documented `-fastboot` option and requested
an external log (PID 18008). It reached only 0.11 CPU seconds after 20 seconds,
then exited before the 50-second follow-up; no log or oracle artifact was
created. This rules out the missing fast-boot flag as a sufficient fix for the
headless capture path, without inspecting emulator implementation details.

## Coherent IOP mapping capture (2026-09-05)

The oracle helper now selects a unique 64-byte unrelocated `.text` window from
the locally supplied, identity-checked CDVDMAN IRX and searches only PCSX2
non-executable host mappings for that data. This is a memory observation aid;
it does not decode, copy, or translate any captured code. A fresh process yielded
one anchor at host `0x7ff778f22904`, module offset `0xd4`. Capturing the verified
2 MiB map at host `0x7ff778f00000` produced external file
`C:/Users/johnn/AppData/Local/Temp/haunting-oracle/iop-ram-20260905T071852847745Z.bin`,
SHA-256 `a83fb1d7b33937c6fb4139fb7b788f84dfad2faeb0ff4520cbb657d4abbd5acc`.
The anchor was at guest `0x22904`, establishing this launch's CDVDMAN guest base
as `0x22830`. At `0x000c3464`, the current data was allocator/request state rather
than a TOC block, so this snapshot predates the pending GetToc DMA delivery and
supplies no payload bytes. PCSX2 was resumed automatically and then stopped after
the observation.

The repeatable two-command procedure is:

```powershell
python tools/oracle_capture.py --pid <pcsx2-pid> --locate-iop rom_cdvdman --suspend
python tools/oracle_capture.py --pid <pcsx2-pid> --iop-base <verified-host-base> --iop-module rom_cdvdman --iop-toc-offset <same-stage-MADR> --iop-toc-candidate --out "$env:TEMP/haunting-oracle" --suspend --note '<guest stage>'
```

The second command refuses a map without the supplied-module anchor before and
after its read. It must never be pointed at repository output. Its optional
candidate output is exactly the observed `0x000c3464`/2064-byte range and has
its own hash/parent-capture metadata; it is deliberately labeled a candidate,
not a usable TOC record, until its command-stage provenance is independently
established. Hashes with recorded negative provenance are instead labeled
`rejected` by the helper, so the repeated pre-command buffer cannot be mistaken
for an admissible `--toc-record` input.

The captured mapping is IOP RAM only, not the `0x1f40xxxx` CDVD MMIO aperture.
Consequently, a candidate needs independently observed command-stage evidence
from a valid hardware-register view; bytes at similarly numbered RAM offsets
must not be interpreted as CDVD registers. This extracts no guest code and does
not infer a command outcome.

## Batch-mode TOC-stage observation (2026-09-05)

A fresh bounded `-batch -fastboot` launch of the local ISO executed the game
(16.31 CPU seconds at the second observation). Its supplied-CDVDMAN anchor was
found at host `0x7ff778f22904`, confirming the existing 2 MiB IOP base
`0x7ff778f00000`. Two suspended, full-map read-only captures were taken 15
seconds apart outside the repository:

- `iop-ram-20260905T083823755666Z.bin`, SHA-256
  `44d572d45b0cde442b9859da0d534c52c69e1b61ceec3320f959db9587d9b12b`;
- `iop-ram-20260905T083905844374Z.bin`, SHA-256
  `f6a33ae63bf6e39f3c0c0d782bd3395cb91c2203ec523e9c234b9d6d85e61e3d`.

Their independently extracted `0x000c3464` candidates have the identical
SHA-256 `b54e0963af0a5cfe515672bb81c7c4e3abd35f4d3a523f2aab2404846bd55e90`.
The first physical-format bytes are zero, so this stable range is rejected as a
pre-TOC/request buffer rather than used as a record. A third coherent 32 MiB EE
capture (`ee-ram-20260905T083944338161Z.bin`, SHA-256
`e819a146adf5996b84e59add8fa0d1b9a4d0a0bb0aeac905e25f36e49ef2decf`) contains
the SIO2MAN request material, but does not make the IOP candidate a completed
DMA record. The batch process was then stopped. No captured byte was put into
the runtime or repository.

A separate fresh batch run was allowed to execute for roughly 30 seconds before
the same coherent capture. Its parent map hash was
`7bf83d8702715b7745e3f724234901ed16ac9e9677c49ed2ad7997a60e24e141`, but its
candidate hash remained `b54e0963af0a5cfe515672bb81c7c4e3abd35f4d3a523f2aab2404846bd55e90`.
This repeats the pre-TOC conclusion at a later active-run interval; the process
was stopped after the bounded observation.

A fresh launch was then sampled 16 times at roughly 0.65-second intervals after
startup. The first candidate had SHA-256
`6602314dc16454765b397f53e52891c3ab6f3ceeb6c7e525e3212fbdd476710a` and a
repeating allocator-like `0x0000400d` prefix; it is rejected as an earlier
buffer state. The remaining 15 samples converged on the prior all-zero-prefix
candidate hash. This high-frequency read-only check found no completed TOC
record; all samples and parent maps remain external.

An immediate-from-launch window sampled through the point at which the verified
CDVDMAN anchor first appeared. Seven earlier samples correctly failed anchor
verification; the next three showed the allocator-pattern candidate and the
last two the zero-prefix candidate. Thus the observable initialization interval
also contains no completed TOC record. The process was stopped after the sample.

## Repeatable batch negative observation (2026-09-05)

A fresh local `-batch -fastboot` PCSX2 process was used only as a memory oracle.
The supplied CDVDMAN anchor at guest `0x00022904` (module offset `0xd4`) again
identified the 2 MiB IOP map at host `0x7ff778f00000`. Two suspended,
module-anchored maps outside the repository were captured at approximately 16
and 27 CPU seconds:

- `iop-ram-20260905T102739427677Z.bin`, SHA-256
  `04f570100e3964ce9824875d89bfd539df01ef5358317611f0cdec1ddf439aa4`;
- `iop-ram-20260905T102834723926Z.bin`, SHA-256
  `eb5861c0e845503761cbefcc85d82b86f08cff2481ab08df9ee3e45d15cefe39`.

Both independently extracted `0x000c3464`/2064-byte candidates have the prior
pre-TOC SHA-256 `b54e0963af0a5cfe515672bb81c7c4e3abd35f4d3a523f2aab2404846bd55e90`.
Neither snapshot establishes the original pending GetToc command stage or a
completed DMA payload, so neither candidate may be passed to `--toc-record`.
The batch process was stopped after the second capture. No emulator code,
serialization, or captured game bytes were inspected or added to the project.

## Fresh bounded batch recheck (2026-09-05)

A new hidden `-batch -fastboot` session was allowed to run from the local ISO
for approximately 20 and 40 wall-clock seconds. The supplied `rom_cdvdman`
unrelocated anchor again established an IOP map at host `0x7ff738f00000`, with
guest module base `0x00022830`. Suspended, module-anchored, read-only captures
were written outside the repository:

- `iop-ram-20260905T130401799785Z.bin`, SHA-256
  `860bbe03df12a43b16a6b8a66e45d96e24f9462449541dde842e59b71120ac5b`;
- `iop-ram-20260905T130430744106Z.bin`, SHA-256
  `338a18afef34870371cd99d5347a57849d35dc1bde2e6a253faf7f15d411101d`.

Although the parent-map hashes differ, each independently extracted
`0x000c3464`/2064-byte candidate is the previously rejected
`b54e0963af0a5cfe515672bb81c7c4e3abd35f4d3a523f2aab2404846bd55e90`.
This is additional negative provenance only: it does not establish a completed
GetToc DMA record and must not be supplied to the native diagnostic. The
PCSX2 process launched for this bounded observation was stopped afterward; no
emulator source, implementation, or captured game byte was used in the build.

## Native all-residual provenance snapshot (2026-09-05)

This observation used the independent native `hg_system_diagnostic`, not an
emulator. One 20-million-slice run watched all 56 then-current unresolved
indirect PCs and stopped at the explicit GetToc boundary. The exact 2 MiB RAM
and JSON register-history sidecar remain outside the repository:

- `haunting-native-iop-all-residuals.ram`, SHA-256
  `2eef24f11d039e8d9210842dd03577fa006423121a5423ec9d9dd7e56badcbbb`;
- its `.json` sidecar, SHA-256
  `05f48fe97f6c41a84f245a2a89230f0fceafc68f6d434e490b6c2f49ce259b4c`.

Four watched sites executed before the gate. Their live transfer registers and
executable owning modules were:

- LOADCORE `+0x076c`: `$a3=0x000da080`, EESYNC `+0x0080`;
- SIFCMD `+0x18b4`: `$v0=0x000d557c`, LOADFILE `+0x057c`;
- SIFCMD `+0x0804`: `$v1=0x00099264`, SIFCMD `+0x1264`;
- MODLOAD `+0x2910`: `$v0=0x000ab95c`, MODLOAD `+0x295c`.

These values add guarded AOT targets only. Generated dispatch still faults on
any different runtime value, so the observation is not treated as proof that a
mutable callback has no other possible target.

## Continuous GetToc-range observation (2026-09-05)

A fresh hidden PCSX2 batch process was used only as a read-only memory oracle.
The supplied CDVDMAN anchor first appeared in the selected 2 MiB IOP mapping,
after which the observed `0x000c3464` range was polled continuously. A
15-second run collected 2,426,056 reads; a separate 45-second run collected
7,914,964 reads. Each launched process was stopped after its bounded window.

The repeatable states were the zeroed buffer, the `0x0000400d` allocator fill,
partial request initialization, and the previously recorded request-metadata
state. Sparse one-read hashes occurred while the guest rewrote that structure
and are not coherent payload evidence. No TOC-shaped 2,064-byte state appeared,
including during the IOP lifecycle transition in which the supplied CDVDMAN
anchor moved. These hashes are now explicitly rejected by
`oracle_capture.py`; none may be supplied to the native runtime.

The monitor now requires two consecutive identical `ReadProcessMemory` results
before retaining a unique state and reports final anchor stability. It writes
only to an explicitly external directory and neither reads emulator code nor
writes process memory. The negative result proves that this guest address is not
a usable completed-record source in the observed PCSX2 startup profile; it does
not justify synthesizing a TOC from the ISO.

An additional 45-second run tested and rejected a relocation hypothesis. The
static bundle places CDVDMAN at `0x000b2000`, which makes native MADR
`0x000c3464` look like module offset `0x11464`. Applying that offset to the
oracle anchor-derived base `0x22830` produced guest `0x33c94`, but 7,220,372
reads yielded one stable executable-code state (SHA-256
`e3ee9ebdb8b690c1cf2a63af53f016cdbcbd0a2f03f5848ee7dafb1f3e140c54`), not a
TOC record. The inference is invalid because channel-3 MADR is loaded from the
request descriptor passed to CDVDMAN; a module anchor cannot relocate that
caller-provided pointer. `oracle_capture.py` therefore requires an explicit
`--iop-toc-offset` obtained at the same original command stage.

## Repeated host-descriptor observation (2026-09-05)

`oracle_capture.py --monitor-locate-iop-toc-ms` now caches the readable,
non-executable virtual-region inventory and repeats the exact aligned
`[MADR, BCR=0x00810004, CHCR=0x41000200]` search for a bounded 100–60,000 ms.
If a descriptor is found and a supplied-module-verified IOP base is provided,
the tool immediately reads the detected 2,064-byte guest range twice and writes
only consecutive-identical candidate data to an external directory.

A fresh hidden batch boot located the supplied CDVDMAN anchor at host
`0x7ff728f22904`, corresponding to guest anchor `0x22904` and verified IOP base
`0x7ff728f00000`. Seven descriptor passes scanned 9,104,101,668 readable bytes
over 60,108.816 ms. No exact descriptor appeared, no candidate was written, and
the launched PCSX2 process was explicitly stopped and confirmed absent. This
negative result means the current PCSX2 profile does not expose the native
channel state as that raw contiguous triple in scanned non-executable memory;
it does not prove that GetToc did not execute, nor does it authorize inferring
MADR or synthesizing TOC bytes. A same-stage guest-register/debugger observation
remains necessary.

## Original GetToc request-frame monitor (2026-09-05)

Independent decoding of the supplied CDVDMAN module identifies the internal
GetToc implementation at module `+0x6f1c`. Its wrapper at `+0x70b0` saves the
return address `module+0x70c4` at frame `+0x48`; the implementation constructs
the DVD `4 x 0x81` DMA descriptor at frame `+0x18`. `oracle_capture.py
--monitor-iop-gettoc-request-ms` now requires both values before accepting a
same-stage MADR, while retaining generic CDVD descriptor sightings only as
non-authoritative diagnostics. It re-finds the immutable supplied-module anchor
on every sample so an IOP reboot cannot leave a stale relocation base.

A fresh hidden batch launch was attached in the same host operation and sampled
the complete 2 MiB IOP RAM 10,111 times over 25 seconds. It observed CDVDMAN at
guest bases `0x120370` (45 samples) and `0x22830` (10,066 samples), directly
confirming the reboot relocation, but saw neither an exact GetToc frame nor a
generic CDVD request frame. The external report is
`toc-request-20260905T172028230433Z.json`; no candidate bytes were promoted or
placed in the repository. This negative result means the request frame is not
available in these polling snapshots. PCSX2's documented `-debugger` entry
break with an R3000 breakpoint remains the next timing-controlled observation.

## BIOS context-transfer syscall observation (2026-09-05)

The coherent external IOP RAM snapshot
`iop-ram-20260905T171348625300Z.bin` (SHA-256
`0ff7a8e6da65772560219a4794a5c8b465f6f06746f6c91926cc4dc3d109da04`)
was decoded with this project's own MIPS decoder. Cause-8 dispatch at `0x5738`
uses selector 32 as a byte offset into the table at `0x59e0`; word `0x5a00`
contains handler `0x5830`. The handler saves a complete 0x98-byte context frame,
passes it to the callback stored at `0x59d0`, and restores the context returned
by that callback. Snapshot values were `0x0000c368` at `0x59d0` and
`0x00012098` at `0x59d4`. The first callback returns the selected THREADMAN
context pointer; the second updates its scheduling selector when current and
candidate thread pointers differ. No captured code or RAM is translated or
stored in the repository.

## Quarantined post-GetToc dependency probe (2026-09-05)

A deliberately synthetic 2,064-byte index pattern was supplied only to determine
the next original consumer boundary. The pattern and resulting IOP dumps remain
under `%LOCALAPPDATA%\\Temp\\haunting-toc-probe`; they are not valid GetToc
provenance and must never be promoted to `--toc-record` for a correctness run.
The first run reached previously undiscovered CDVD interrupt callback
`0x000b6288`; original setup at `0x000b705c` independently proves that callback
is registered for cause 2. After adding the root, the next run reached a byte
read at `0x1f402013`. With the synchronous endpoint's explicit zero idle state,
the original interrupt completed and MODLOAD issued
`cdrom0:\\MODULES\\SIO2MAN.IRX`, returning `-203`. Thus the content-agnostic
probe established control-flow dependencies only. MODLOAD helper `0x000ab8d8`
returns `-2`, so the guarded device-operation transfer at `0x000ab2a4` is not
yet reached; that helper/result is the next startup investigation. A real TOC
payload remains separately required for correctness.

## Live GetToc DMA capture (2026-10-06)

The pinned `emu/PCSX2-MCP-v1.0.0-win64/pcsx2-qt.exe` was launched against the
user-local game image and observed through its guest IOP debugger only. At the
original CDVD interrupt callback (`rom_cdvdman` relocation plus `0x4288`, guest
`0x00026ab8` in this boot), the live IOP DMA3 registers were
MADR=`0x00033c94`, BCR=`0x00810004`, CHCR=`0x40000200`. The corresponding
2,064-byte external capture is `%TEMP%/hg-door-real-toc-20261006.bin`, SHA-256
`1bcd3fd5348ab8dcfb5f8e5253912a51ab7505e2691191615d02f99be2d3e1d4`.
It begins `04 02 f2 00 86 72 00 00 00 00 00 00 01 02 01 00` and has a
zero final 16-byte trailer. The process was stopped with an identity check;
no oracle process remains.

This supersedes the quarantined synthetic TOC for local diagnostic launches,
but is still a PCSX2-oracle observation rather than physical-console evidence.
Current official PCSX2 source at commit
`9fffbdbd59b962d63a2259b150f419ad3773e7b4` was read only for sequencing clues:
its GetToc path writes the 2,048-byte payload at live DMA3 MADR and completes
the DMA/command around that transfer. No implementation, algorithm, comment or
captured game byte is copied into the repository.
