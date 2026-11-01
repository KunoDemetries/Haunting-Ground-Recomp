# Building and verification notes

The full earlier README; some sections are historical. See PROGRESS.md for the current state.

An independent ahead-of-time static recompilation of Haunting Ground (USA, SLUS-210.75)
for Windows: the original PS2 game code is translated to native code at build time and
runs on our own runtime services, with OpenGL graphics. It is not an emulator and has no
interpreter or JIT.

**This repository contains no game files.** No ISO, ELF, IOP modules, BIOS, memory cards
or generated game-derived source. You need your own legally obtained copy of the game;
the build translates it locally. Everything derived from the game stays in ignored
folders (`Haunting Ground (USA)/`, `out/`, `build/`, `builds/`).

## Current status

Playable on the development PC, but not finished and not yet played start to finish.

- Speed: 30 fps on the heavy benchmark route on the development PC.
- Game logic: 12 sample save slots match the PCSX2 reference at frame 3000 (about 100
  seconds each) and one matches through frame 5000, with a comparison-only switch on.
- Known issues: right-channel music static after some music underruns at track changes;
  the last-bit rules of multiply, divide and square root are not yet hardware-exact.
- Windows only for now; Linux is not currently verified.

Detailed state and evidence: `docs/PROGRESS.md`. Older notes further down this README are
historical and may be out of date.


Current native viewer: build target `hg_game` (requires generated translations and
OpenGL dependencies), then run `build/Release/hg_game.exe`. It runs the AOT game
in-process and presents directly through OpenGL; the GS rasterizer remains on the
CPU. Startup currently still requires the explicit experimental `--toc-record`
input documented in `docs/PROGRESS.md`. There is no bundled replacement record.

Controls: arrows = D-pad, WASD = left stick, IJKL = right stick, Space = Cross,
X/C/V = Circle/Square/Triangle, Enter = Start, Tab or Backspace = Select, Q/E = L1/R1,
1/3 = L2/R2, left/right Shift = L3/R3. F1 toggles the on-screen controls reference;
Escape closes the reference first, or exits when it is already closed. The game
continues while the reference is open; pause with Enter first. See [full keyboard controls](CONTROLS.md). A GLFW
mapped gamepad is also supported. Keyboard movement is user-verified; gamepad support awaits
verification. The title reports video updates/s, not internal game FPS. Diagnostic
runner arguments remain available; `--unlimited` disables the default pacing.


Independent AOT tools and native runtime source. **Not playable yet.** The verified
Windows diagnostic reaches the original menus and completes all6300 frames of
the original OPENING movie. Its first-to-last conversion span with explicit
`--realtime` was210.241230 modeled seconds in210.4044 host seconds (99.922%
modeled rate;99.792% against6299 nominal30fps intervals). This uses fixed
diagnostic input, the experimental clock profile, and instruction histories OFF.
The first full replay then stopped at a missing static translation in the
post-movie transition; further original execution is being completed.
This is not physical-console parity, displayed FPS, or verified audio sync. See
[performance evidence](PERFORMANCE.md) and [diagnostic inventory](DIAGNOSTICS.md).

This repository contains source and synthetic tests. Generate game translations
locally from the user-provided inputs; game/BIOS files, generated game source,
memory captures and binaries are excluded from version control.

`python tools/hg.py analyze --triage` also reports literal method tables installed
by original code into object slot zero. `installed_table_candidates` records each
store site, table slot and whether the pointed-to address is already compiled;
`missing_table_targets` deduplicates candidates for batch investigation. These
bounded pointer runs are leads only: adjacent tables can share a run, and their
extent and live use require verification before adding AOT roots. The scanner
does not change discovery or runtime dispatch.
The same report includes `direct_member_candidates` and `missing_member_targets`
for loaded three-word `(0, -1, target)` descriptors, with each source load PC.
This covers pointer bits copied with `LWC1` as well as integer loads. It recognizes
only that observed direct-call profile and still requires use-site verification.

An early, independent ahead-of-time recompilation project for the user-provided
USA executable. **Not playable yet.** No ps2recomp dependency or implementation,
and no PCSX2 code is used. PCSX2 is reserved for optional memory observations.

The connected native diagnostic executes original EE/IOP startup and can capture
readable controller and memory-card check screens through the independent GS path.
Controller negotiation and bounded absent-card probes run; subsequent startup
and present-card/save support are still being completed. The separate
OpenGL host can show the connected diagnostic's latest framebuffer with
`--watch-display`, or run its synthetic scanout smoke test without that option.
See [progress](PROGRESS.md) for tested state.

To watch a diagnostic run, add `--preview-file "$env:TEMP/haunting-live.ppm"`
to its existing arguments, then open a separate PowerShell window and run:

```powershell
./build/Release/hg_opengl_host.exe --watch-display "$env:TEMP/haunting-live.ppm"
```

The preview updates every million diagnostic slices and retains the last complete
frame after a fault. It reads committed display memory without flushing guest
draws. Escape closes the viewer; it does not stop the diagnostic. This is a
sampled diagnostic view, not a real-time playability claim.

Add `--realtime` to the system diagnostic to limit host playback against its
modeled clock. It is opt-in; unlimited execution remains available for benchmarks.
On Windows it uses a high-resolution waitable timer,10ms deadline checks and a
100ms catch-up bound. It does not change guest work, clocks or scripted inputs.
Hardware/host performance can still cause slow playback. This is experimental
viewing support, not verified physical-console speed or an audio synchronizer.

For raw SPU2 input inspection, add both `--spu2-input-wav <external.wav>` and
`--spu2-input-core 0` (or `1`). The diagnostic writes the selected core's existing
48kHz stereo 16-bit input stream without scaling or mixing, and reports per-core
sample statistics. The WAV path must be outside the repository. This is
HG-DIAG-013 observability for the modeled SPU2 sound-input transport; it is not
the final SPU2 synthesis/effects/cross-core mix.

On Windows, `--spu2-output-core 0` (or `1`) sends that already-modeled 48kHz
stereo PCM stream to the default speaker device through WinMM. This HG-DIAG-014
host-presentation path applies 3/4 (~-2.5dB) speaker-only headroom and does not
feed timing or state back into the guest. It is
useful for hearing the current movie stream, but it bypasses unfinished SPU2
voice synthesis, volume/effects and cross-core mixing and is therefore not the
final game-audio implementation. Host presentation uses eight 1024-frame buffers,
starts after four are queued (~85ms), and reports host-only underruns at exit.

For the current connected movie diagnostic, `tools/run_movie.ps1` launches the
OpenGL viewer and the realtime system diagnostic together with core-0 speaker
output and the established fixed input schedule. It still depends on the
external synthetic TOC record and remains a diagnostic launch, not a playable
release path.

For streamed-audio synchronization provenance, add `--spu2-sync-log <external.csv>`.
It records the original movie-clock callbacks and queue state in that CSV and
writes `<external.csv>.events.csv` with the relevant original EE queue/ring writes,
method calls, and core-0 AutoDMA state transitions. Both files are observation-only
HG-DIAG-013 output and require an EE instruction-history build. Use this for
diagnostic correlation, not as a playback clock or physical-console timing claim.

For interactive diagnostic Left/Cross input, initialize an external text file
with `none` and add `--input-file path.txt` instead of scripted pulse arguments.
Replace its contents with `left`, `cross`, or `start` to hold that button, and `none` to
release it. The runner polls every10000 slices; invalid input stops explicitly.
Use atomic file replacement when changing commands during a run.

For bounded VU geometry inspection, pair `--capture-vu1 <external-prefix>` with
`--capture-vu1-tex0 <64-bit-value>`. This HG-DIAG-006 observer records the first
64 AOT calls whose current TEX0 register matches, with entry and before/after
VU memory/registers. It does not change the translated execution or guest state.

For repeatable diagnostic replays, supply ordered `--input-at slice command`
options, for example `--input-at 100000 left --input-at 200000 none`.
Each event sets the same button and pressure state as `--input-file`, at the
exact scheduler slice. Commands are `none`, `left`, `cross`, and `start`.
Events must be strictly increasing and inside the run budget; at most 1024
events are accepted. Do not combine them with other input options.
This is explicit diagnostic input (HG-DIAG-006), not a guest timing adjustment.

## Requirements

- Python 3.11+ (standard library only for the tools).
- CMake 3.24+, a C++17 compiler, and a desktop OpenGL 3.3 driver.
- GLFW 3.3+ installed, or `HG_FETCH_GLFW=ON` to fetch pinned GLFW 3.4.
- The user's local `Haunting Ground (USA)/SLUS_210.75` for game analysis.

Windows is locally tested with MSVC. Ubuntu 24.04 under WSL2 passes the headless
GCC 13 build and the 13-test suite, including the original SIF handshake.
The OpenGL smoke test also passes under WSLg/Mesa.
macOS is not verified yet.
Other systems depend on C++17, GLFW and OpenGL support;
universal operating-system compatibility is not promised.

## Analyze and generate

Read the local asset archive without extracting it:

```powershell
python tools/hg.py assets
python tools/hg.py assets --asset '/ADX00/AD_01.ADX;1'
```

The first command writes only an ignored metadata index to `out/assets.json`;
the second streams one asset to SHA-256. The independently inspected CVM contains
an ECMA-119 volume at offset `0x1800`. For a plain ISO use `--archive path.iso
--volume-offset 0`. Extended attributes, interleaving and multi-extent files are
explicitly unsupported. No archive contents are copied into the repository.

Inventory the supplied IOP modules with `python tools/hg.py modules`. This writes
only ignored section/import/export/relocation metadata to `out/modules.json`.
It does not relocate or execute modules, and uses no EE instruction assumptions.
Add `--relocation-base 0x10000` to dry-run build-time relocations and record section
hashes. Each module is checked independently at that base; this is not a linked
module layout or executable IOP runtime.

`python tools/hg.py iop-plan` validates `config/iop_modules.toml`, including exact
module hashes, load-segment/BSS placement and local import/export bindings. Manual
IOP roots use `functions = [{ name = "example", offset = 0x100 }]` in a module entry.
Offsets are relative to that module, independently of EE function addresses.
The ignored `out/iop-plan.json` records unresolved import stubs; those must trap
until an actual provider exists. Import binding requires a unique same-major
provider with the requested ordinal; minor versions may differ, following the
independently inspected original LOADCORE link path.
Manual IOP callback hints use `indirect_targets = [{ site = 0x230, targets = [0x16b0] }]`
within a module entry. All addresses are module offsets. A bounded relocated pointer
table can use `table_offset` and `table_count` in place of `targets`. Sites must decode
as indirect jumps/calls and targets must be executable. Hints add static coverage;
execution still takes its actual target from the guest register.
An original IOP syscall may be replaced only through a narrowly validated module
`syscall_handlers` entry. The planner requires the configured instruction to be
`syscall` and its immediately preceding original instruction to load the exact
allowed selector into `v0`; currently only MODLOAD selector 12's
`invoke_in_kmode` transfer is supported. Other selectors remain explicit faults.
`python tools/hg.py iop-audit` reports reachable code and unsupported boundaries for
every configured module without overwriting the selected generated C++ source.

`python tools/hg.py reboot` reads the game-supplied `IOPRP300.IMG` in memory and
indexes its 16 embedded IRXs without extracting them. The archive reader currently
accepts the empty-RESET ROMDIR layout present in this dump.
Embedded module entries add `member = "SIFCMD"` and `container_sha256` alongside
the module's own `sha256`. Build and runtime verify the whole container and selected
member. The current plan includes29 modules (the connected build selects28); duplicate providers remain unresolved.

To build the first IOP module diagnostic:

```sh
python tools/hg.py iop-emit --iop-module modmsin
cmake -S . -B build -DHG_IOP_SOURCE="/absolute/path/to/out/iop-translated.cpp"
cmake --build build --config Release
```

Run `hg_iop_diagnostic --verify-modmsin` from the repository root to exercise five
cases of its original descriptor-validation function as compiled native code.
Without that flag, its entry stops at the first unresolved interrupt-service import.
This is a separate 32-bit IOP translation path, with explicit load-delay checks.

For one read-only, comprehensive AOT decoder inventory across the configured EE
image and every configured IOP module, run:

```sh
python tools/hg.py major-scan
```

It writes ignored `out/major-scan.json`, retaining every reachable-word count,
opcode family, unresolved boundary, individual IOP module report, and one
all-configured-module static-link pass. Defined terminating BREAK instructions
are inventoried separately by trap code and remain explicit runtime faults. It
does not infer dynamic targets or execute guest code.
`docs/DECODING_SCAN.md` keeps the compact human-readable checkpoint and priority
list.

To exercise the supplied SYSCLIB memory/string routines, emit with
`--iop-module rom_sysclib`, rebuild, then run
`hg_iop_diagnostic --verify-sysclib --module "Haunting Ground (USA)/MODULES/IOPRP300.IMG"`.
The diagnostic runs 101 memory/string/formatting cases against independently constructed
inputs. Emit `rom_loadcore` and use `--verify-bootmodes` with the same container path
to test boot-mode registration/query using an isolated initialization fragment.
`rom_sifcmd` can also be compiled; its entry currently stops at `loadcore:12`.
Single-module builds keep imports blocked even when the plan identifies a provider;
cross-module execution requires compilation and initialization of those providers.
The combined planner follows the behavior verified by executing the supplied
LOADCORE linker: exact versions bind, and a newer minor export may satisfy an
older same-major import. Newer imports, different majors, and ambiguous compatible
providers remain explicit boundaries.

`python tools/hg.py iop-bundle --iop-modules modmsin rom_sysclib rom_loadcore rom_sifcmd rom_sifman rom_threadman rom_timemani rom_fileio rom_stdio rom_ioman rom_sysmem rom_modload rom_cdvdman rom_cdvdfsv rom_loadfile rom_eesync`
generates a combined static address space. Rebuild, then run
`hg_iop_diagnostic --verify-bundle --module "Haunting Ground (USA)/MODULES"`.
The bundle loader verifies all files/members before committing any memory changes.
The diagnostic runs LOADCORE's original linker helper and verifies a SIFCMD import
reaches its translated provider. A guarded import requires both stub words to match
the statically planned target; unknown targets never trigger runtime decoding.
`--start-sif` runs an isolated empty-boot-mode startup profile through SIFMAN entry
and into SIFCMD initialization. It currently waits for the EE transport signal.

With both generated EE and twelve-module IOP sources configured, use
`hg_system_diagnostic --verify-sif` to verify two original commands, the IOP reply,
both DMA callbacks and event wakeups. Configure `HG_SIF_BOOTSTRAP_TESTS=ON` to
include this game-dependent check in CTest; leave it off for single-module builds.
Run `hg_system_diagnostic` without the verification switch to continue startup
until the next unsupported operation. This is still a diagnostic, not a playable game.
Complete IOP boot and EE integration remain unfinished.

The original module-load path issues CDVD GetToc before its first file read. A
plain ISO contains only user sectors, not that drive control-data record. When an
independently acquired 2064-byte CDVD DMA record is available outside the
repository, pass it as `--toc-record path`; the diagnostic validates its exact
size and commits it through the original channel-3 completion route. Without it,
the diagnostic stops explicitly rather than manufacturing a TOC.
For native-state analysis, `hg_system_diagnostic --dump-iop <external-path>`
writes exact 2 MiB IOP RAM when startup faults. The destination must be outside
the repository. An adjacent `<external-path>.json` sidecar records the exact IOP
PC, all 32 GPRs, HI/LO, delayed-load state, current native thread, and virtual
time at that fault. Up to 64 repeated `--watch-iop-pc` observations retain bounded
histories with the argument, target, stack, global, and callee-saved registers
needed to establish callback-object provenance. Runtime-state dumps never enter
version control.
Up to 64 repeated `--watch-ee-pc` arguments provide the equivalent read-only EE
history directly from translated instruction boundaries. They do not alter
dispatch or infer targets; only observed register values may later justify a
guarded configured target set.
For a bounded high-resolution memory-oracle check, `oracle_capture.py` also
accepts `--iop-base ... --iop-module rom_cdvdman --iop-toc-offset <same-stage-MADR>
--monitor-iop-toc-ms N --out <external-directory>`. It waits for the supplied-module
anchor, polls only that explicit 2,064-byte range, and retains a state only after
two consecutive identical reads. The DMA destination is caller-provided and is
never inferred from the module anchor. The report records whether the module anchor remained stable;
monitor output is candidate evidence, never automatic runtime input.
For a launch-race-resistant observation, `--monitor-iop-gettoc-request-ms N`
scans a supplied-module-verified 2 MiB IOP mapping for the original CDVDMAN
GetToc call frame. It requires both the decoded `4 x 129-word` descriptor and
the exact wrapper return address, follows CDVDMAN across the IOP reboot, and
records weaker CDVD-frame evidence separately without promoting it to a TOC.
All captured bytes still require an external `--out` directory.
At an original debugger stop where GetToc channel 3 is active,
`--locate-iop-toc-dma` scans only readable, non-executable host mappings for the
exact observed BCR/CHCR pair and reports any aligned in-range preceding MADR.
It does not inspect executable/JIT pages or infer the address from module layout.
Add `--monitor-locate-iop-toc-ms N` for a bounded repeated scan that caches the
readable-region inventory and stops on the first exact triple. With a separately
verified `--iop-base`, `--iop-module rom_cdvdman`, and external `--out`, that same
hit immediately captures the detected MADR twice and retains it only if both
reads match. Output remains candidate evidence and is never accepted
automatically by the recompilation runtime.

Native adapters are explicit: module `native_functions` maps the two cache-maintenance
exports to coherent native memory fences and tracked flush epochs; top-level
`native_imports` maps selected INTRMAN ABIs to stateful interrupt registration,
enable, suspend and resume services. Module `syscall_handlers` is separately
original-byte and selector checked. Unrecognized adapter/ABI combinations reject.

Module `entry_hooks` can validate a buffered-module load before its original
entry instruction executes. Embedded inputs use `offset`, `size` and both
container/module identities; their original bytes select a static allocation
plan. Unknown buffer images fail explicitly. The original loader still performs
allocation, relocation, linking and entry execution.

From the repository root:

```sh
python tools/hg.py analyze
python tools/hg.py emit
python tools/hg.py analyze --triage
```

These verify the configured SHA-256, then write ignored local `out/analysis.json`,
`out/listing.txt`, and (for emit) `out/translated.cpp`. An exit status of zero means
the tool succeeded, not that translation is complete. Inspect the report's issues.
Unknown instructions remain explicit runtime faults; there is no interpreter.
`--triage` additionally writes `out/analysis-triage.json`: a deterministic grouped
queue of every already-recorded boundary, without a second discovery pass or target
inference. It also records strictly adjacent initialized file-pointer-load candidates
for batch validation, but never promotes them to targets or suppresses their runtime
boundaries. It is intended to select the next decoder implementation in one run.

For an externally captured raw VU MicroMem program, run:

```sh
python tools/hg.py vu-inspect --vu-program /external/path/vu1-pairs.bin --vu-base 0
```

This writes ignored `out/vu-analysis.json` with 64-bit pair structure, I/E flags,
lower-pipeline control edges and their required delay slots, plus opcode-family
counts. It is a build-time inspection aid only; it does not execute VU
instructions or provide an interpreter fallback.

## Build and check

```sh
cmake -S . -B build -DHG_FETCH_GLFW=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For headless core tests use `-DHG_OPENGL_HOST=OFF` instead. Dependency downloads
stay inside ignored `build/`; no packages are installed globally by CMake.

On Windows run `build/Release/hg_opengl_host.exe --frames 3`. On a typical
single-configuration Unix build run `build/hg_opengl_host --frames 3`.
Omit `--frames` for an interactive window; Escape closes it. The colored image is
a synthetic swizzled-GS/PCRTC scanout probe, not a game frame.

To compile the local game prefix too, configure with
`-DHG_TRANSLATED_SOURCE=/absolute/path/to/out/translated.cpp`. Then run
`hg_diagnostic` in the build output directory. Its expected exit code is **2**,
with a boundary at `0x001001c8`. `--verify-prefix` checks this boundary and returns 0;
`--boot` enables the native boot-service subset and runs to the next unresolved
operation. Use `--elf path` for an input outside the default dump folder.
This program has no game asset loader or full kernel/device implementation.
Do not distribute generated source, ELF files or game assets with the toolchain.

## Hardware captures and oracle checks

Float rules are checked against our own capture programs run on a real PS2
(`tools/hwtest/`, built with the ps2sdk Docker image `ps2dev/ps2dev:v2.0.0`, e.g.
`docker run --rm -v <tools/hwtest>:/src -w /src ps2dev/ps2dev:v2.0.0 make EE_BIN=struct_capture.elf EE_OBJS=struct_capture.o`).
`struct_capture` embeds our IOP module `spu2probe/` (SPU2 envelope probe):
`make EE_BIN=struct_capture.elf EE_OBJS="struct_capture.o spu2probe_irx.o"`.
Each writes a `.BIN` to `mc0:/HGFPU/`; captures stay out of version control.
`build/Release/fpu_vector_check <vectors-dir> RESULTS.BIN VU0.BIN` compares our model
with them and `python tools/hwtest/analyze_struct.py STRUCT.BIN` summarizes the
structured capture. Results and fitted rules are in `docs/SOURCES.md`.

Save-based section checks against the PCSX2 oracle (`docs/ORACLE.md`):

```sh
HG_CHECK_LIMIT=200000000 HG_CHECK_FRAME=2100 HG_CHECK_DETAIL=1 python tools/oracle/section_check.py 7
```

Both sides load the same memory-card save and pause at the same game-frame counter
write (our side: HG-DIAG-124). PCSX2's VU0 adder is not the console's; add
`HG_PCSX2_VU0_ADD=1` (HG-DIAG-126) only to look past that known emulator difference.

## Manual function configuration

Edit [config/haunting_ground_us.toml](config/haunting_ground_us.toml).
The input path is relative to the config file; addresses are aligned EE virtual
addresses expressed as TOML integers, typically hexadecimal.

```toml
[[functions]]
name = "your_verified_function"
address = 0x00123400
end = 0x00123500

[[indirect_targets]]
site = 0x00123410
targets = [0x00125000, 0x00126000]

[[data_ranges]]
start = 0x00127000
end = 0x00127100
```

These are illustrative addresses, not discovered functions. Each function is an
additional discovery root. Optional `end` is an exclusive stop boundary, not an
assertion that every preceding word is code or a hard sandbox on outgoing calls.
Indirect target sites must decode as `jr`, `jalr` or `eret`. Targets add reachable code;
execution still uses the guest register. Unknown register destinations fault if
not translated. Data ranges prevent discovery from decoding known embedded data.
Wrong image hashes, duplicate names/addresses, malformed ranges and unknown keys
are rejected. Keep evidence for newly assigned names in the progress notes.

## Persistent project memory

Read [rules.md](rules.md) and [learned skills.md](learned%20skills.md) before
adjustments. Plan and describe each change, preserve the current dirty files,
validate correctness and speed, then retain evidence-backed lessons and rejected
approaches with their scope. `AGENTS.md` makes this workflow mandatory.


[AGENTS.md](AGENTS.md) is the rules and project map. [docs/PROGRESS.md](PROGRESS.md)
records outcomes, blockers and the immediate next steps. During active work refresh
it every 20–30 minutes and before stopping. [SKILL.md](SKILL.md) defines handoffs.

## Static copied-code mappings

Known copied code is configured before compilation:

```toml
[[overlays]]
name = "verified_patch"
source = 0x003eb8a8
address = 0x80076000
size = 0x740
entry_offsets = [0]
```

Source is an ELF virtual address; address is its runtime destination. The game
must perform the copy itself. Each emitted overlay instruction checks that memory
still contains its compiled word. Missing or changed bytes fault. Entry offsets
select discovery roots within the mapping; this is not runtime code generation.
`analysis.returning_syscalls` permits discovery past explicitly listed syscall
numbers when the immediately preceding instruction loads that number into v1.
It does not supply an implementation for those services.

For a verified array of absolute function pointers, replace `targets` with
`table_start` and `table_end` (exclusive). The tool reads aligned 32-bit pointers
from the checked ELF and validates each destination. Bounds must be supplied from
independent analysis; the tool does not guess where a table ends. This supports
the startup constructor table without storing a generated pointer list in config.

## Inspect a native execution boundary

`hg_diagnostic --boot --dump-state /external/path/native-current` writes guest RAM
and register metadata to `.ram` and `.json` files. Keep these outside the repository.
Then run `python tools/hg.py inspect --state /external/path/native-current` to show
independently decoded instruction/caller windows and possible object pointers.
For standalone inspection use `python tools/hg.py inspect --pc 0x00100008 --count 16`.
These commands only inspect; they never add discovery targets automatically.

`hg_system_diagnostic --dump-iop /external/path/iop.ram` saves IOP RAM and
adjacent JSON metadata on a fault, budget exhaustion, or diagnostic LOADFILE
stop. Metadata distinguishes `native-iop-fault`, `native-iop-budget`, and
`native-iop-loadfile-stop`; a budget snapshot does not imply a guest fault.
The output path must be outside the repository.

## Declared callback arguments

```toml
[[callback_bindings]]
callee = 0x00100340
argument = 5
site = 0x00100414
```

This declares an independently verified ABI: the helper receives a callback in
GPR 5 and invokes it at the configured indirect site. A conservative pass tracks
constants within straight-line code, includes the call delay slot, and discards
constants at joins, calls and unsupported effects. Proven executable targets feed
another discovery pass. `analysis.json` records each caller/argument/target in
`callback_evidence`. Uncertain values are not guessed. Ordinary `jr ra` returns
are listed in `return_sites`, separately from unresolved indirect transfers.

`hg_system_diagnostic --verify-services` verifies original FILEIO heap-server registration
and an EE bind through DMA. Enable `HG_FILEIO_BOOTSTRAP_TESTS=ON` for this integration
test with the ten-module bundle. `--slices N` controls the diagnostic budget (up to10million).

`hg_system_diagnostic --verify-loadfile` verifies the original LOADFILE worker's
`0x80000006` RPC registration and its native handler/queue record. Enable
`HG_LOADFILE_BOOTSTRAP_TESTS=ON` when the generated bundle includes `rom_modload`
and `rom_loadfile`.

Continuing beyond heap binding currently also requires `rom_cdvdman rom_cdvdfsv`.
Their original startup runs against a partial native drive protocol; disc reads remain unfinished.


Performance diagnostics: `hg_system_diagnostic --profile` reports sampled host
phase costs. `--clock-profile issue-slots` selects experimental bounded native
CPU/DMA quanta with field-driven preview; `legacy` remains the default. Slices
are scheduler quanta, not video frames or exact PS2 cycles. See
[performance evidence and timing limitations](PERFORMANCE.md).

## Reproduce the source-only checks

A fresh headless build needs no game or BIOS input:

```sh
cmake -S . -B /absolute/path/to/external-build -DHG_OPENGL_HOST=OFF
cmake --build /absolute/path/to/external-build --config Release
ctest --test-dir /absolute/path/to/external-build -C Release --output-on-failure
```

Use an external build directory for temporary validation. Existing game-dependent
checks require locally generated EE/IOP sources and their corresponding inputs.

`HG_EE_INSTRUCTION_TRACE` defaults to `ON`. Throughput experiments can configure
`-DHG_EE_INSTRUCTION_TRACE=OFF` to compile EE instruction-history calls out of the
game translation. A PC watch in that build fails explicitly before execution;
re-enable the option and rebuild to capture EE instruction history. Other
diagnostics remain separately accounted for in `docs/DIAGNOSTICS.md`. This option
does not change guest instructions, execution budgets, or hardware fault checks.
