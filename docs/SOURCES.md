## World editor formats - 2026-10-19

Room PAC, `.TEX` bank and slot-3 geometry layouts (`docs/WORLD_EDITOR.md`) were derived
only from the user's own disc data (whole-disc parse counts) and this project's
disassembly of the original executable (loader 0x120720, activation 0x11FFB0). The CSM1
palette order follows the GS User's Manual CLUT storage description and was confirmed
statistically on the decoded images. No external tool, format document or editor code was
used or copied; `editor/src/disc.hpp` reimplements this project's own `volume.py` rules.

## Door-section object family 0x46df00 - 2026-10-11

Live stop at unconfigured `0x002abf60` from the generic JALR `0x00209574`
(slot `+0x0c`, receiver `a0=0x017f3550`, return `0x0020957c`). Own scan of the
pinned ELF finds the word `0x002abf60` exactly once, at `0x0046df0c`, making the
64-byte table `0x0046df00` (null prefix at `0x46df00/04`, next null prefix at
`0x46df40`) the receiver class. Its literal is materialized by installers
`0x20a6c8..cc` and by destructor `0x2abf18..1c`. Slots `+08..+3c`:
`2abf00, 2abf60, 2abf80, 2abf90, 2abfa0, 2097f0, 2abfb0, 2abfc0, 2a8950,
2ac010, 2abf70, 2abff0, 2abfe0, 128080`. Every uncompiled body was decoded
independently: constant getters (`LUI v0,0x3f; JR ra; ADDIU`), indexed getters,
a return-0 stub, a tail jump to the known member helper `0x100b40`, and a
destructor freeing through `0x100490`. The layout mirrors the stairs table
`0x46dfc0` used at the same sites. Additive AOT roots only; runtime dispatch
still uses the guest register, and unknown targets still fault.

## Installer-proven 0x2631d0 interface family - 2026-10-08

Original `0x002631c4..d4` loads an object vptr, reads slot `+0x2c`, and calls it
at `0x002631d0`.  The live 156.5M stop has receiver `0x0180f690`, vptr
`0x0046d410`, and exact original/live slot target `0x0025fd00`.  Fourteen
initialized tables share the same null-bounded 15-slot ABI: `0x0046d370`,
`0x0046d3c0`, `0x0046d410`, `0x0046d460`, `0x0046d4b0`, `0x0046d500`,
`0x0046d550`, `0x0046d5a0`, `0x0046d5f0`, `0x0046d640`, `0x0046f610`,
`0x0046f660`, `0x00476450`, and `0x00476b60`.

Own decoding independently ties every table to original constructor block
`0x00263224..0x002634cc`, which materializes each literal vptr before its store.
The exact union contains 54 executable original methods.  Configuring that union
at the existing `jalr` is additive AOT discovery only: runtime target selection
still comes from the guest register and targets outside the proved set still
fault.  No external implementation or renderer material is used.

## Post-portrait VU1 scene14 composite - 2026-10-08

The exact corrected route stops first on unconfigured VU1 entry zero with
MicroMem SHA-256 `be37654bb1ee41665dcc29043dc88492079b39c063c6360b99cae55bd0d5ed3f`.
Threaded publication is at original VIF1 status read `0x0010ca44`; synchronous
publication is immediate at original VIF activation `0x001b795c`.  Both retain
byte-identical MicroMem and preview, independently excluding device-thread
ordering as the source of the identity.

Every byte through micro `0x1318` matches the pinned USA ELF in six contiguous
ranges: `0x0000 <- 0x003a9548` x256 pairs, `0x0800 <- 0x003a9d50` x96,
`0x0b00 <- 0x003a4ec0` x16, `0x0b80 <- 0x003a2ba0` x106,
`0x0ed0 <- 0x003aaf50` x38, and `0x1000 <- 0x003ab088` x99.  The assembled
611-pair range has SHA-256 `29d60202f07f5148ce6b72a14840b2b9e1748c113b160ca84365b9e04cad1e33`.
The project's independent VU structural decoder reports no unsupported pair,
external direct branch, or missing delay slot.  Original END pairs at
`0x0048/0x0ad8/0x0b60/0x0eb0/0x12f0` prove resume entries
`0x0058/0x0ae8/0x0b70/0x0ec0/0x1300`; entry zero is directly observed.  This
is build-time AOT input provenance only—no runtime decoding or external
renderer/emulator implementation is used.

## Bounded post-door descriptor family - 2026-10-07

The latest live stop is original target `0x002f39e0`, selected with receiver
`0x00fc6fe0` and return `0x002f609c` through the already proved member helper.
Rather than infer nearby functions, the batch follows only original descriptor
records and the original stores that install them into object offset `0x110`.
The complete relevant record block is `0x0041a1d0..0x0041a2b0`; every 16-byte
record begins `{0, 0xffffffff, target, 0}`.  Its targets in order are
`2f5c80, 2f4330, 2f59a0, 2f55c0, 2f5280, 2f55c0, 2f4e40, 2f4a30,
2f46b0, 2f59a0, 2f59a0, 2f4090, 2f39e0, 2f3d30, 2f3d30`.

Own decoding proves the install graph from the routines themselves.  Original
`2f60b0` loads records `41a1d0/41a1e0`; `2f5c80` loads `41a1f0`;
`2f59a0` loads `41a200/210/220`; `2f5280` loads `41a230/240`;
`2f4a30` loads `41a250/260/270`; `2f4330` loads `41a280`; and
`2f4090` loads `41a290/2a0/2b0`.  These loads are followed by the original
three-word stores to object `+0x110/+0x114/+0x118`.  The only not-yet-compiled
members are `2f39e0`, `2f3d30`, `2f46b0`, `2f4a30`, `2f4e40`, and `2f5280`.
Their decoded return/delay boundaries are respectively `2f3d24/28`,
`2f4084/88`, `2f4a20/24`, `2f4e30/34`, `2f5278/7c`, and `2f55b0/b4`;
distinct successor prologues follow after padding where present.  This is a
closed original-data/original-code graph tied to the dynamically observed
descriptor.  It is not a proximity scan, wholesale object-table rooting,
external implementation, runtime decoder, or relaxed unsupported-address rule.

The exact original body byte lengths/SHA-256 values are: `2f39e0..2f3d28`,
840, `abab5adbe0fa3ce4f4b833abe2fbaf39167f89ccc31146bbbf51571949c4091d`;
`2f3d30..2f4088`, 856,
`c7e53f6f8411e94ee0ebdb54af49a3b7b74006799fd772ec8e6ee438a9a45725`;
`2f46b0..2f4a28`, 888,
`e3ac780d7598d3bc5ddf16578a0ff6a512e25abe8c26317a44941bbc0668fb2e`;
`2f4a30..2f4e38`, 1,032,
`b7ce96f49de287350bc7f89fb754614993a168c0ca32fd8d866409c6888df3b1`;
`2f4e40..2f527c`, 1,084,
`60a6dd7cbe5a857a812268bce3f55970e7f23123d5b23644ade1c278747d4877`;
and `2f5280..2f55b8`, 824,
`16a34de5250446377952eaf017d2680d03e7cbb4badfeecf6d18bdc9c47fd078`.

## Original post-door direct-member callback 2f4090 - 2026-10-07

The interactive run stopped explicitly at EE `0x002f4090`, with receiver
`0x00fc6fe0` and return `0x002f609c`, through the already independently decoded
negative-descriptor member helper at `0x00100b6c`.  Own decoding bounds the
original function to `[0x002f4090,0x002f4328)`: `JR ra` is at `0x002f4320`, its
delay instruction is at `0x002f4324`, padding follows, and the distinct already
rooted `0x002f4330` prologue begins afterward.  All 664 file-backed original
bytes equal the retained live EE RAM range, SHA-256
`abe671bbfa2f52c5f30185425e2879ce995f6beac4fac96beca0238a7fb3146d`.
Only this observed target was added to the existing helper target set.  No
adjacent routine was inferred, no original instruction was replaced, and no
runtime decoder, emulator backend, external implementation, renderer material,
or guest-clock change was introduced.

## Post-door VU1 composite identity - 2026-10-07

The recovered 1,321-event route and authentic 2,064-byte TOC record are external diagnostic inputs under `%TEMP%/haunting-toc-probe/`; neither contains repository source data. Independent synchronous and ordinary threaded-device executions stop on entry zero with the same 16 KiB VU1 MicroMem SHA-256 `757b4fb550785f5d1119eed3b9dac8ab6aa299a7f5f4b1dd6a946ce168f0027c` and identical retained GS counts. This establishes the identity as guest-produced state rather than host queue timing.

Every byte in the active `0x0000..0x1318` MicroMem range maps to one of six unique file-backed ranges in the pinned original `SLUS_210.75`: micro/source/pairs are `0000/003a6118/256`, `0800/003a6920/94`, `0af0/003a4eb0/18`, `0b80/003a2ba0/106`, `0ed0/003aaf50/38`, and `1000/003ab088/99`. Their individual SHA-256 values are `5f21d91a2076c5c7fa62d7d0990b1deb6b3b4019b8c15e175314a218c780a324`, `da3780e15a00ec8858e4cfecb4748dcd2e06b3107c397a4ad349cf55620d3173`, `4f59ccd1d260a7826b58605c3bf12157a8ea0ec6fc8ff6c576b1ef2a45c739c6`, `c0b868260971959a4fbf5ed8783285c2a01852a09cda054c895f847427c53ce5`, `26f122ab4e6efddbd60811db179d1e663095c9252c825ebd2e5343adf2efe22c`, and `2f430d6c777f29023c52fcfc5b350ed86d3de437784c08d9b2b203a4fbee7372`. Concatenation yields the exact captured range SHA-256 `795330eb080f8283a906feb169f16046e021db2e64fe7c2dfcd6f38afb2aa34e`.

Own VU decoder inspection finds no unsupported pair, missing delay slot, or external direct branch. END pairs are `0x0000`, `0x0ad0`, `0x0b60`, `0x0eb0`, and `0x12f0`; their architectural next-pair resume entries are `0x0010`, `0x0ae0`, `0x0b70`, `0x0ec0`, and `0x1300`, with entry zero retained for MSCAL. Dynamic replay first confirms resume `0x0ae0`; the remaining END successors are compiled in the same batch from the exact decoded image rather than rediscovered one freeze at a time. The AOT config names only those exact original ranges and entries. No PCSX2/shared-project VU implementation, runtime microinstruction decoder, guessed instruction, renderer material, or emulator dependency was used.

## Original bounded staircase object46dfc0: complete remaining static method coverage - 2026-09-22

Latest manual c277ea1723404267b5f23dd71725f966, evidence project-link-visible/20260922T071804Z-4497884a223e48d5bd53d7c9ce48f0ca/native.log:1204, stops at2ac570/a017f355c/RA20962c. Saved receiver holds46dfc0. Original inspector5f0db0dfd3b2484aa0ed1231130a6745 decodes209390's selector<6 and same-receiver calls2095dc(+10),209600(+14),209624(+18), selecting2ac550/560/570. The live hit is only2ac570. These getters return3f93a0,3f9530,3f95f0 using original LUI/JR/delay ADDIU, not host replacements.

Original installer701fc4e52f004d65bf7fef58ebbb79d8 independently locates20a6f8..20a704 publishing46dfc0, then20a708..20a714 publishing distinct46e000, followed by46e040/46b7f0. Saved receiver words match that sequence. The64-byte table46dfc0..46dfff matches original file3596352 and saved RAM4644800: SHAd7ebd6d8de5a91273c9de390de9d3c4d63cd04d88661e0704a08e610cfd53b99. This provides original installer/boundary and live-use evidence for this specific family, reusing the independently documented46e4c0 workflow in config. It does not justify scanning other tables or treating every nearby pointer as callable.

Eight absent methods identified within that bounded table: slot08=2ac4d0,10=2ac550,14=2ac560,18=2ac570,20=2097e0,24=2ac580,34=2ac5b0,38=2ac5a0. Six other initialized entries2ac530/540/5d0,2a8940,2097f0,128080 were already compiled. Inspectors5c3c99d85fc6499fab98e33716f269dc anddd83e4f51a34410c88b9be3609e222fc independently decode all newly rooted bodies. Original/live256-byte2ac4d0..5cf match231848725dcb6c005199ab6d86866df1613cdcfe3cc77fa95876beaed57b1e57 (file1754448/RAM2802896); original/live16-byte2097e0 match1baba6d44296e1c7ecb565588d009ec11e0bf75c4f0b3f3baf2167c6a859d96a (file1087584/RAM2136032). The default returns zero via actual JR/DADDU instructions; the conditional delete and indexed loads retain their original operations and checked paths. No runtime decoding, forced calls, guessed return values, external implementation or BSS-table extent assumption.

Configd2c998fa19148db9acc27a023ab7b027f86bf701b97885e86879aee8636aa989 adds three precise getter callsite entries and five explicit bounded-table roots, retaining every earlier repair. Exact pre-edit81772f41... is preserved by backup6da48db24c154d3fb49ad985d27b3b1f; plan/hash manifest stairs-object-46dfc0-preedit-20260922-g1.txt. Emitter8594a73e76fa46389c10b2363db10e31 adds exactly50 original words in shards0010/0015 only; generated preimages are in project-link-emit/20260922T073009Z-82326dd30ebd4020b5009323b0e16117. Body inspection confirms normal budgets, delay slots, returns and checked loads. Build0c9fb87d155648cfb1cfd66294056f79 produces game07a17eabed38b187659677758cf5b45da34a4280fd28b110593ba2ca60cebcaa; fixed verifier5d147ab1ec2147d79fd21e9d1a87d69f matches149-input provenance and expected original33M state. Potential family coverage is not a claim that all methods ran or that the complete staircase route passed; see PROGRESS for the manual retest requirement. Audio and throughput remain unresolved.

## Original staircase slot+30 getter2ac540 - 2026-09-22

Manual444f97023d454284a6eb624b8a9da788/native.log646, evidence project-link-visible/20260922T065755Z-2697cdfbcf484f14a093708fffe18461, stops at2ac540 with a017f355c/ra2091a0/sp1ffea20. Saved object17f355c holds vptr46dfc0; original/live64-byte table agrees SHAd7ebd6d8de5a91273c9de390de9d3c4d63cd04d88661e0704a08e610cfd53b99, slot+30=46dff0 contains2ac540. This is distinct from previously compiled+0c/2ac530 and+28/2ac5d0.

Original inspector2421f0d1bdcc4d7a9b43bef6287c361b decodes209184..2091cc. The first call selects receiver base+4*selector+120, loads its vptr then+30 at209190/94, JALR209198 sets RA2091a0, and delay20919c supplies a0. Paired2091b0..2091c8 independently selects the same object/slot and stores the returned pointer in original state at2091d0. Its potential use is supported by original code, not a claimed dynamic hit. Inspector ee86c73dcf1d41d2af8a3c91418953c5 decodes2ac540/44/48: LUIv0,0x40; JRra; ADDIUv0,v0,-27936 in delay, returning3f92e0. There is no further call within this getter. Full16-byte original file1754560/saved EE2803008 match SHA c76d2d8a815bb74bad3e84685dd7fd0483065489aab0d1ac8dca9987ee1738ec, including padding. Pinned original ELF remains3b374d53a499d2c17b205274ee9eb34280768f294f970ebf6ae6731f6a2dacb8.

Config81772f417dea1d0eccb17d9dae7d49a6c012738fe51fddf2f3219b803c6d9efa appends exactly209198->2ac540 and2091c4->2ac540, retaining all earlier work. Dirty original2a50b98b... exact backup37ee07c5b4f14526a4f2bd2e2bb3a9d0; external plan stairs-2ac540-preedit-20260922-f1.txt. Configured emitter57806f8b46a24eb19e4b8a09b1cf7d0c saves prior bytes at project-link-emit/20260922T070559Z-2b210ef2cfea412eb3647ce25e74fa5f, changing only shard0015 as C++ and adding exactly3 original words. Shard05bdfcbb... lines49603..49621 retains ordinary budgets, trace selection, JR target snapshot, delay arithmetic, last-transfer and return handling. No host pointer-return stub, instruction patch, runtime decoder, emulator dependency or guest timing change. Python e29add19... passes130 tests. Build/replay/freshness and live stairs results are separate evidence in PROGRESS; this static proof does not certify the entire script or stair route.

## Original second staircase dispatch2ac5d0 and callback2ac600 - 2026-09-22

Manual3005b1c29cff45ad99918cf7832882db/native.log1106 (project-link-visible/20260922T064324Z-c54b636f936049019d5e5f53fc6c5fe4) stops at2ac5d0,a017f355c,a1=0,ra2032c4. Saved framebuffer67dbf852... shows the stair landing. Original caller inspector98329158f3a34d5abda3bdb19a72edc0 verifies2032b4/b8 vptr/slot+28 loads,JALR2032bc,delay2032c0. Saved object17f355c holds46dfc0. Original/live64-byte vtable identityd7ebd6d8de5a91273c9de390de9d3c4d63cd04d88661e0704a08e610cfd53b99 proves slot46dfe8->2ac5d0; earlier slot+0c/2ac530 is distinct and preserved.

Inspector63108a06b6484d63b6b9d225a238133f decodes original dispatcher: mask initial a1 to8bits, multiply by12, move old a2/a3 to a1/a2, tail-jump100b40 with t9=01990900+12*index. Captured selector0 chooses saved BSS01990900={0,-1,2ac600}; existing independently decoded helper100b6c handles that negative-descriptor path. This BSS record is captured guest state, not claimed file-backed data, dynamic execution of the next function, or proof of a256-entry table. Only its selected callback is rooted. The callback's next virtual call2ac624 resolves in saved state via456df8->fcda00->46ecf0,+18->2c91d0, already compiled in shard0016; no additional root added.

Original ELF SHA3b374d53a499d2c17b205274ee9eb34280768f294f970ebf6ae6731f6a2dacb8. Full original/live152-byte region at file1754704/RAM2803152 matches08e46fa5a3c78004034f5006d162fe7348436d49abbe2f56241b69943e6990d3:40-byte dispatcher,8 padding,104-byte2ac600..664 callback. Keep actual saves, virtual call, conditional stores, JRra2ac660/stack-restore delay2ac664. No emulator or shared implementation used.

Only config2032bc->2ac5d0 and existing100b6c->2ac600 added; new config2a50b98bfb1af41d99f897d1abbcfb941b0324a4f008291ca502dff7a394b26e. Exact dirty predecessorb8d3101f... preserved in backup33be0e762ffe44ccb0c19f9895f484ff. External stairs-2ac5d0-preedit-20260922-e1.txt records proof/plan/hashes. Emitterbd0cd304d15f487ebe70c9c0341187b2 archives generated originals at project-link-emit/20260922T065246Z-a4d75a31edd2445a9fe6857343e95172; exactly36 added words, only shard0015 C++ changed (e6b836246e65dcb14e295c315e67102f1392e06148ff43f99a0cc3b977434a17). Cases49603/49657 preserve normal checked static execution. Game1fe0fcdb73b9c78ff081941fdb6663e08826e1d7129e28d241c02e427e50957d built4a077e31... with149-input manifest67a1a896.... Protected host9704ed0b..., prior roots and pending VU mechanism remain unchanged. Standard33M qualification and actual100M staircase retest are separate; see PROGRESS. An initial documentation write during the verifier was rejected without changes; this write occurs after terminal tests.

## Original menu-close callbacks3977d0 and3974a0 - 2026-09-22

User's saved sessionb26be6103c9c4acf961ad659cf0fabf9/native.log820 stops at3977d0 with a0180f680/ra3999d8. Original caller3999d0 calls100b40 and delay3999d4 selects object+16fc; saved01810d7c contains {0,-1,3977d0}, while adjacent+1708 still contains396900. Caller inspector46bffe01... and the already independently decoded negative-descriptor JR100b6c establish this dispatch. The identical previously blocked target inspection succeeded on user-requested retry as1ec288e8f1a044019044cd67c9b6ec72, through the same configured task, not a fallback. Complete724-byte original/live body3977d0..397aa0 matches SHAa9602af38a50208d0e10645641ab3dad43a0ad52b3a38eb5c12b090bb7258d1b (ELFfile2717776; RAM3766224); JRra397a9c restores352 stack bytes in delay397aa0.

Original3978f8/397904 loads descriptor44b1d0,397908 calls existing nonzero-descriptor helper100bd0,397924/28/2c copies its three words into object+16fc/+1700/+1704. Descriptor is {0,-1,3974a0}, proving a potential successor of the same observed dispatch without guessing adjacent functions. Inspector7ca164d5089842f7b00064c5f0d46666 decodes this successor; full812-byte original/live body3974a0..3977c8 matches SHA83ee781cbb23de6fb7e846c71e5bb8aca1993cb11bb269f57f6219a5542d5d38 (ELFfile2716960; RAM3765408). It loads44b1e0 at3976c0/cc and installs {0,-1,398850} at397714/18/20. That final callback is already compiled, so no third new root is needed. The32-byte descriptor pair at original44b1d0..1ef and saved RAM agrees SHA069f91053eeb1b7756b0eaaa1e4eff39515c500b2162c3072f57b30b42117888. Original ELF pinned3b374d53a499d2c17b205274ee9eb34280768f294f970ebf6ae6731f6a2dacb8. These observations prove static coverage requirements, not all downstream behavior or successful live menu close.

Only config site100b6c gains3977d0/3974a0; currentb8d3101f33e40b67bbcfa5cc0f7acafe3a9899a8cf418ee6bec4d2eb004d6711,161602bytes. Exact dirty original12393103... preserved in automatic backupd7fece70d66844c4969b7a08fb63c6ae; external planclose-menu-3977d0-family-preedit-20260922.txt. Configured hg_emit35b8201409954c61917c784460e57772 archives original generated bytes at project-link-emit/20260922T063803Z-efe99298d87a4a6d907e82b8e197d853 and changes only translated-shard-001c.cpp as C++ (384 additional words, exactly the two verified bodies), plus analysis/listing. New shardSHA3e38cfc44724deeda08f92b83b8257a9f4c19744a639d2b7d2e55f47ce27c88d contains both entry cases and ordinary checked budget/delay/return paths. Python c8ef3d914ca94f3bbe9a76475b14b62f passes130 tests; build58e9b25b02d144d2aecebc14173ff143 succeeds, executable2094e2cd8e2bdd4f0bcef49b3d14ca90ba6db939c166b30ffe25d61e8571d3d9,149-input manifest95c9f948ed016e89d1184e414a43ab7dc88f2a8ef70cba63325f29031ea607a3. F1/Tab/redraw host9704ed0b... and all previous coverage retained. No original instruction replacement, runtime decoding, audio/clock/rendering changes or emulator dependency. Live open/close test remains separate from fixed33M regression; see PROGRESS.

## Original Select-menu member396900 and three installed successors - 2026-09-22

Manual session93c3705fa75c447b93bde4547d095d73 (project-link-visible/20260922T060315Z-2b6cfc946afd44cfb159f18fef315246) delivered Select buttons65534 at native.log748, then stopped at396900/a0180f680/ra397f64 at line762. Original397f58 sets a0=s2;397f5c JAL100b40 and delay397f60 set t9=s2+1708. Saved EE RAM01810d88 contains {0,-1,396900}; the independently decoded member helper uses its negative-offset JR100b6c path. Own inspectorad68cf0740374d149a61feb1c2c86ad5 records this caller. Target inspectorsa8dff78c18f74af396ae6948013dfb00/59aca2fca079460cab3133ac917f9e16 identify normal prologue396900 and JRra3970c4/stack-restore delay3970c8. Complete1996-byte original ELF body at file2713984 and saved RAM at3762432 agree SHAfb5d15b9b0a62785886f2fb7f976191d6f8d092ec569650a386ccd5db6257aac. Original ELF identity remains3b374d53a499d2c17b205274ee9eb34280768f294f970ebf6ae6731f6a2dacb8.

To avoid single-callback discovery for this same menu family, inspected ORIGINAL396900 descriptor installers, not adjacent functions by proximity. Sources44b200/44b210/44b220 are12-byte {0,-1,target} descriptors for394e40/395630/394670. Original/live96-byte44b200..44b25f region matches SHA8d77461e73dece559d617e7f2e3000df48a6a4a7954b157b220f738bef33cf07. Exact primary stores:396e3c/40/48,396ed0/d4/d8,396fdc/e0/e8 ->object+1708/170c/1710, the known caller's field. Helper100bd0 ORs the three words and returns nonzero for every descriptor. These are statically proven potential successor calls; not claimed to have occurred in the captured run. Secondary1714 copies share existing targets and add no roots. Already compiled393bd0 remains. Inspectors9321d6303ff34ae286d6c4f0040fb928,ac16345aa9334ef7a1cfed6b03d3cf18,ac7fd36f9e99496887f0ebe26e4317fd record installers/guard. Original prologue inspectors07c8c86a...,04ec9ee1...,201bb9cd... independently decode all three target entries.

Config12393103... adds exactly these four roots to existing indirect_targets site100b6c, preserving staircase2ac530/unpause2f55c0 and all prior work. Original configd90c7919... exact backup4ababbf5ff094fdab9deb00af72c1d07; intermediate396900-only config3a4a79a5... backupc90e46405ea54ee58f53484b1fa00bef. External planselect-396900-preedit.txt plus pre-code PROGRESS amendment record scope. Configured emit011587b0980c42a4bf1e9a8db52c9f48 saves exact previous generated bytes under project-link-emit/20260922T061352Z-5a43a2db1eb64e34af2af7a7d9572822; newly discovered direct callees follow normal AOT CFG rules. No original call/renderer/fault/arithmetic/clock bypass or emulator dependency. Current F1/Tab host9704ed0b... stays unchanged. Synthetic and standard33M regression are not a live Select-menu success claim; see PROGRESS for build and runtime evidence.

## Original unpause direct-member target2f55c0 - 2026-09-22

User's manual unpause sessionecd7f4c9b0754e2b9d2110c0ef697e21 stops explicitly at2f55c0 with a0fc6fe0/ra2f609c (saved native.log1094). Original2f6090 sets a0=s0,2f6094 calls100b40, and delay2f6098 sets t9=s0+110. Saved EE RAMfc70f0 begins {0,-1,2f55c0}; the original helper100b40/44/48 loads the three words,100b4c takes the negative-offset branch, and100b6c JRt9 enters the direct callback. Inspectorscf9a925db267417582bc0cd0ef5e3e63 and3d3f3fbc6237472d8b068ed6d3cdfc73 establish caller/helper independently.

Inspector5558618e1da444259886b357655f7146 decodes the original body2f55c0..2f599c. It has normal stack/register/FPU saves, original branches and calls, JRra at2f5998, and stack restore in delay2f599c before the already-known2f59a0 routine. Full992-byte original ELF slice(file2053696) and saved live EE code(VA3102144) agree SHAd15a1113d610faed2e3f6662a697a96198f77ac6eb322a9721e2a9e558f60b58. Original ELF remains3b374d53...; no external implementation, guessed adjacent callback or rendering reference is used.

Config change appends only2f55c0 to existing indirect_targets site100b6c, retaining previously verified2f5c80/2f59a0 and the separate staircase2ac530 root. Old config77332e15... saved automatically in backup5eeb0dc61e2243c08abb457eaee98dd6; newd90c7919.... Pre-edit provenance and exact hashes in external unpause-2f55c0-preedit.txt. Build/regression and actual live unpause verification are separately recorded in PROGRESS; compiling the missing callback alone does not prove its downstream calls or full unpause now succeed.

## Original live virtual target2ac530 - 2026-09-22

Manual visible jobc1fa9cfc84bc40bfaad9712c131e0ffc stopped at EE2ac530, ra20957c, a017f355c. Saved EE RAM at17f355c contains vptr46dfc0; slot46dfcc contains2ac530. Original caller20956c loads the vptr,209570 reads+0c,209574 JALR t9 preserves the return20957c, and209578 sets a0 in the delay slot. Own hg_inspect jobs5e1deef013e04e1badb6278240006d6f andf7977dab48d348ecb396afb32cc0e830 establish original target/caller instructions, not an external implementation.

Pinned original ELF3b374d53... PT_LOAD maps file0x80 to VA0x100000. Its64-byte table at file3596352 matches saved RAM at46dfc0 exactly (original slice SHA d7ebd6d8de5a91273c9de390de9d3c4d63cd04d88661e0704a08e610cfd53b99). Target2ac530/34/38 is LUI v0,0x40; JRra; ADDIU v0,v0,-28256 in delay slot, returning3f91a0. Config adds only indirect_targets site209574 ->2ac530. No neighboring method is assumed reachable, no original call is bypassed and no renderer/clock/arithmetic/fault behavior is changed.

Configured hg_emitb0129f4346b0439282aee2efe6468458 backs up all prior generated bytes under project-link-emit/20260922T053246Z-dbc5de424d2a4c44bf8db98e75cf151d. Only translated.cpp and translated-shard-0015.cpp change as C++ (plus analysis/listing). The new page_2ac body at shard0015 lines49581..49604 preserves the normal budget/trace prologues, static target selection, delay-slot arithmetic, return and unsupported-address fault.130Python tests3a2d0275f45f4a819e990bcf4f82b8c9 pass; build/replay/live re-cross results belong in PROGRESS, not inferred from emission. Config backupf5bc653dd3ce41e1a0e5031270be7035 restores exact e6a72c5f... if needed; current77332e15....

Evidence project-link-visible/20260922T052458Z-71c5108ac15b4b9a80b5a56e4112e3fc retains the later staircase-landing framebuffer640x448, EE/IOP/GS/VU states and fault log. PrintWindow returned white despite that saved image, so it is not a faithful capture here. Missing audio is separately explained by the launch command having no speaker option; the next manual launch must use audio=speakers. This does not certify complete SPU2 mixing or synchronization. No manual input recording was produced, so the later route requires a new live test.

## Build freshness and rejected GL error-notification trial - 2026-09-21

Primary documentation consulted: CMake add_custom_command POST_BUILD (https://cmake.org/cmake/help/latest/command/add_custom_command.html), GLFW context sharing (https://www.glfw.org/docs/latest/context_guide.html#context_sharing), and Khronos KHR_debug specification (https://registry.khronos.org/OpenGL/extensions/KHR/KHR_debug.txt). The retained post-link/preflight hash code is independently written project infrastructure, outside native execution timing. The rejected GL trial derived its callback guarantee from KHR_debug sections2.5,5.5 and5.5.7: an ordinary context may emit no messages, whereas the selected debug-context synchronous mode supplies API-error notifications before the offending call returns. The trial preserved polling fallback and original fault points and passed an actual-driver expected INVALID_ENUM startup test, but regressed qualified throughput and was removed. No emulator/shared-renderer code, algorithms or comments were used. See HG-DIAG-028/030, HG-FAIL-028 and PERFORMANCE for the measured scope; documentation alone is not proof of physical-console fidelity or improved performance.

HG-PERF-111 reset-notification provenance (2026-10-10): GLFW Window Guide/header documentation defines `GLFW_CONTEXT_ROBUSTNESS` as the strategy actually used by a context and `GLFW_NO_RESET_NOTIFICATION` as `0x00031001` (decimal 200705): https://www.glfw.org/docs/latest/window and https://www.glfw.org/docs/latest/glfw3_8h.html. Khronos `KHR_robustness` section 2.3.1rob states that `NO_RESET_NOTIFICATION` never delivers reset-event notification and `GetGraphicsResetStatus` always returns `NO_ERROR`; it separately warns that preserving context state under such resets is recommended but not guaranteed: https://registry.khronos.org/OpenGL/extensions/KHR/KHR_robustness.txt. `WGL_ARB_create_context_robustness` defines `WGL_NO_RESET_NOTIFICATION_ARB` as the default reset strategy when the WGL robustness facility is used: https://registry.khronos.org/OpenGL/extensions/ARB/WGL_ARB_create_context_robustness.txt. A temporary current-driver query on the project's ordinary 4.3 core GLFW test context returned `context_robustness=200705` on NVIDIA GeForce RTX 5070 / 617.42. `runtime/game_host.cpp` requests the same relevant 4.3 core context with no robustness override when GPU rendering is enabled. Therefore a post-`glFlush()` `glGetError()` is not a specified reset-notification mechanism for this configured strategy; do not infer that context state can never be lost, only that moving the ordinary error poll across `glFlush()` does not forfeit a guaranteed reset notification that this context was configured to deliver.

## Original Start direct-member callback2f5c80 - 2026-09-20

Saved live fault reports EE PC2f5c80, a0=fc6fe0, ra2f609c. Independently decoded
original2f6094 calls member helper100b40 and sets t9=s0+0x110 in its delay slot.
Helper100b40 reads three descriptor words; negative word1 reaches direct JR t9 at
100b6c. Saved live EE RAM atfc70f0 is 00000000 ffffffff 002f5c80, proving this
specific direct target at that observed call. Original2f5c80 has a normal prologue,
body and JR ra at2f603c with stack restore delay slot2f6040;2f6050 starts the next
routine. No neighboring address or external implementation was used as the proof.

Config therefore adds only2f5c80 to site100b6c. Project Link hg_emit regenerated
the normal independent emitter output: prior backup contains no page_2f5/case2f5c80;
new translated-shard-0017.cpp contains `case 0x002f5c80u: step_2f5c80`, and
translated.cpp dispatches page_2f5. analysis.json marks target3103872 compiled=true.
Emitter backup/manifest: TEMP/haunting-toc-probe/project-link-emit/
20260920T204149Z-312ef9d99f9542d88c5fef2ef9b3fb81. Built hg_game SHA256
806b231e7fbc6e50b915e02659e9ab1b2e7a9a6ca103601a27b1a6722e2e903a.
Tests/build pass as recorded in PROGRESS. Runtime crossing of the former Start fault
remains unverified because no replay/launch task is exposed; this is static-coverage
and build evidence only, with no gameplay-FPS or audio claim.

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

## Original virtual callback124da0 - 2026-09-20

After the bounded VU0 underflow correction, the exact42-event recording advances
from36.445516M to39.245766M and stops at missing124da0. Captured a0=cb8b40,
[a0]=46c220, t9=124da0 and ra=1728b0 agree with independently decoded original
172894..9c installing that table and1728a0/a4 selecting slot+10 before JALR1728a8.
Original ELF and live RAM46c230 both contain124da0. Original124da0/a4 is JRra/NOP
03e00008/00000000. A narrow authorized shared manual-config address lookup had
no match; the original instructions and observed path establish this target.
Only that indirect-site target is added. Normal old/new emission matches all26
current generated files and adds exactly2 original words in shard0009; no words
removed, main/other shards unchanged. Proofs callback124da0-regeneration-proof.json
and callback124da0-applied-proof.json. The large-file patch reader failed before
mutation; a verified580-byte insertion exactly equals normal emitter output and
preserves all outside bytes.105 Python tests pass; callback build/replay pending.
No host success stub, skipped instruction, external implementation or guessed
neighbor callback. Current baseline live-callback-124da0-baseline preserves files
and pre-callback corrected executable4488fc9d... with manifests.

## Bounded VU0 accumulated product-underflow - 2026-09-20

Sony VU User's Manual v6.0, April2002, p42 MADD exception table, macroVMADDbc
p273 referring to microMADDbc p88. User-local original PDF SHA256
 a858ef55cd2638accdfca612d5359eb6e48a48c7fd6792b18080d866e610fde5.
Transcript: https://studylib.net/doc/25815876/vuusersmanual.158394566
Coordinate-based extraction in TEMP/haunting-toc-probe/sony-vu-page42-tables.json
separates ACC normal/productUDF row: current U/O0, final-result Z/S, stickyUS1.
For nonzero normal ACC (encoded exponent1..254), adding the flushed signed-zero
product leaves that ACC exactly unchanged. Separate accumulated stickyU from
current flags; retain atomic original snapshots/commits. Unsupported zero/denormal
ACC underflow, product overflow, exceptional ACC and exceptional sums still fault.
No EE COP1 or external renderer policy borrowed; VU1 helpers are not a flag oracle.
49152 synthetic runtime cases, 12 atomic rejection cases and translated delay-slot
checks pass. External actual-operand fixture produces expected vector/ACC/flags.
Original-game continuation validation pending. Existing bounded24-bit profile
and physical-console least-bit uncertainty remain. No timing changes.

## Compile-time VF readiness proof retained - 2026-09-20

Proof uses existing native model only: successful VF check establishes selected
lanes ready; produced_vf writes issue_cycle+4; each pair advances one cycle.
Conservative ages ignore additional stalls. Reset proof at entries/targets/control
boundaries. Unit advance and WAITQ detect unsigned wrap and restore original
masks for the remaining invocation. Require-target advances cannot wrap. Zero
advance cannot additionally complete Q/P after the pair's initial positive advance.
No new hardware-timing assumption or interpreter.49152 compiled synthetic states,
105 Python tests and four original captures pass; scene2.631% lower. This validates
preservation of existing behavior, not complete physical-console timing fidelity.

## Combined exact product/add host call retained - 2026-09-20

Existing independently implemented integer product and exact add bodies are
inlined only inside the AVX2-specific source; one guarded dispatch combines calls.
Product rounding/saturation happens before addition, and exception masks are ORed
as before. No host FMA or guest timing change.4M chained scalar comparisons/four
MXCSR modes, existing32M arithmetic comparisons and four original captures pass.
Scene0.702% lower, both pairs faster. This removes host overhead, not guest work.

## VU SQ snapshot elision candidate - 2026-09-20

Own original microprogram bytes and existing decoded dependency metadata identify
131 reachable SQ sites,20 overlapping same-pair upper VF writes. SQ consumes VF
lanes/definedness and VI address only; upper operations change no VI. If store
lanes and upper VF writes are disjoint, the post-upper operands equal the saved
pre-upper operands. Overlapping sites retain the original entire-state snapshot,
including fault ordering. Existing dual-issue behavior remains unchanged; no new
console assumption. Original proof vu-sq-snapshot-static-proof.json. Incremental
regeneration matches old emitter block exactly before inserting normal new output.
Build/104 Python tests and four original captures pass (RTC only).
Matched scene1.410% lower; retained.

## Resident masked-transfer proof - 2026-09-20

Derived from own PSMT8 byte mapping and existing CPU expression. The same eight
word indices/byte masks carry the16 host input bytes. GPU updates use uint32
(old & ~mask) | (value & mask); merging duplicate words combines masks and retains
last writer for each bit. One shader invocation per unique word prevents RMW races.
Queued draws precede updates; later draws and overlapping CPU access flush them.
Unmodified bits stay on GPU until ordinary coherence materialization. No external
rendering reference or guest timing change.1042 differential cases per GPU mode
and four original captures validate existing modeled behavior, not total console
fidelity. HG-DIAG-017 and masked-transfer-summary.json record scope/evidence.

## External native stack diagnostic - 2026-09-20

Independent external sampler follows Microsoft DbgHelp StackWalk64 contract:
https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-stackwalk
Installed SDK Windows/DbgHelp headers supply structs. No external implementation
copied. CONTEXT_FULL with RIP/RSP/RBP initializes the walk; remote reads and PE
unwind tables use SymFunctionTableAccess64/SymGetModuleBase64. Per-sample suspension
uses scoped resume and no allocation while suspended. Five-second run is excluded
from benchmarks; observed call chains support the GS download/flush lead only.

## Four64-bit-lane integer product candidate - 2026-09-20

Derived solely from existing independently implemented Fpu::product and SSE2
broadcast_products. Four unsigned24x24 products fit48 bits; bit47 selects exact
right shift23/24 and exponent carry. Signed exponent range remains -126..384;
64-bit compare masks select underflow zero or overflow Fmax. Exponent-zero input
retains signed zero with no underflow flag; exponent255 remains finite per existing
model. AVX2 lane packing selects the low32 bits of each64-bit lane; masks retain
X/Y/Z/W order. No host FP arithmetic, clock changes or emulator implementation.
Existing CPU/OS AVX2 guard and scalar/SSE2 fallback remain.16M scalar product
comparisons across full exponent pairs, edge mantissas/signs and random inputs
passed in fourMXCSR modes without exception flags. All four original timing/state trials pass (verified RTC-only IOP differences).
Scene mean2.476% lower; retained. This establishes implementation equivalence,
not new console fidelity.

## Options connected state group - 2026-09-20

Own original393c5c/393ca0/393ce4/393d28/393d6c/3940c4 load complete direct-member
records44b408/418/428/438/448/458, test them with100bd0, and store all three words
atobject+1708/170c/1710. Their targets are393880,393480,3930c0,392b50,392670,392360;
original397f5c and397ea0 dispatch that same field through100b40. Original3998c4..
3998fc loads44b170 ->398100 and storesobject+16fc. Captured397d60 stop likewise
uses the live object+16fc descriptor; original44b1a0 retains its template. Scoped
original-byte proof: options-state-group-proof.json. No unrelated descriptor list
was rooted automatically; own decoder evidence was checked against load/store code.

## Options body descriptor - 2026-09-20

Original397f5c calls100b40 with object+1708 inr25. Hidden replay captures this
at8fe5c8 as {0,-1,393bd0}; original44b060 contains the same descriptor. Add393bd0
to100b6c static targets. Shared non-rendering functions.manual.toml393bd0 extent
and narrow Options lead consulted; all target facts independently read from own
ELF and options-transition-replay EE RAM. No copied implementation.

## 2026-09-20 Options transition continuation

Captured replay and fresh live session both reach3984d0 through100b6c; object
8fe5bc now contains {0,-1,3984d0}, matching original44b160. Original398724..39875c
loads {0,-1,397f00} at44b180, checks it via100bd0, then stores its three words at
object+16fc/1700/1704 for the same helper. Original398878 checks state<16 before
398898 loads table4643a0..4643e0 and jumps. All16 words independently read from ELF.
The narrow shared non-rendering functions.manual.toml3984d0 extent was consulted
as a lead; target/table/store proof is from original ELF and live snapshots.

## 2026-09-20 live direct-member callbacks

Own digest-verified original SLUS_210.75 descriptors44af30 and44b050 contain
{0,0xffffffff,0x3838f0} and {0,0xffffffff,0x399920}. Captured live EE objects
17f3afc and8fe5bc respectively contain identical descriptors. Original helpers
384ba0/3999c0 route these through100b40, whose direct transfer is100b6c.
Independently inspected target bodies; add only these live-proven static targets.
Narrow shared HG non-rendering functions.manual.toml extent leads corroborated the
addresses; no implementation or rendering material was copied. Artifacts:
live-member-3838f0-proof.json, live-paced-audio-check.*, manual-intro-recording.*.

User video reference: https://www.youtube.com/watch?v=JvdAHmVA-vI&t=28s,
SHN Survival Horror Network longplay. Visual landmark comparison only, not a
physical-console timing oracle or proof of internal gameplay FPS.

GPU triangle host-interface reference: Khronos ARB_gpu_shader_int64, inspected
2026-09-19: https://registry.khronos.org/OpenGL/extensions/ARB/ARB_gpu_shader_int64.txt
64-bit GLSL arithmetic and packing interfaces only. Triangle coverage/interpolation,
texture/depth/color stages derive from our existing independently implemented CPU
renderer and recorded GS manuals, not an external rendering implementation.

Independent manual check after rendering-reference request: original GS User's
Manual v6 printedpp15/17/35/51/95/162 inspected from existing local PDF. Page35
specifies parallel pixel processing and setup gradients; page162 describes8KiB
pages and column access; page51 requires TEXFLUSH invalidation when newly written
image/CLUT data is used for textures. Physical cache behavior and alias pixel order
remain separate verification questions; host GPU coherence is not a substitute
for those guest semantics. Extract outside repo: gs-pipeline-reference.txt.
Wide host integer division interface verified against Microsoft documentation:
https://learn.microsoft.com/en-us/cpp/intrinsics/udiv128?view=msvc-170
The new helper checks divisor/quotient width and preserves portable checked fallback.

## Rendering reference comparison requested by user - 2026-09-19

User explicitly asked to learn from PCSX2 rendering and original PS2 behavior.
The initial independent GPU sprite implementation and measured batching result
precede this inspection. Inspected official PCSX2 GS documentation:
https://github.com/PCSX2/pcsx2/discussions/4311
https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Docs/GameIndex.md
Search results also exposed excerpts of GS.cpp, GSRendererHW.cpp and
Renderers/OpenGL/GSTextureOGL.cpp in that same official repository. These were
used only as architecture leads: texture/target caching, readback and synchronization
costs, and why games submit narrow clearing strips. No source, comments, algorithms,
game fixes or mappings copied into our renderer. Original GS User Manual remains
the authority for independently derived behavior; PCSX2 is not a runtime dependency.

Open leads, not established console facts: verify page-cache/drawing-group effects
and alias ordering against original manuals/probes. Our CPU row-major feedback
order is not automatically physical-console proof. GPU per-draw alias remains
rejected. Resident VRAM and synchronization only at true consumers are candidates;
do not copy PCSX2 cache/invalidation heuristics or game-specific substitutions.

## OpenGL sprite acceleration provenance - 2026-09-19

runtime/gl_gs.cpp is an independent port of our existing CPU CT32 sprite pixel
math, with host-prepared addresses/fractions and integer filtering/TFX/ALPHA.
No external emulator or shared project rendering implementation was consulted.
OpenGL interface/ordering reference: Khronos ARB_shader_storage_buffer_object,
https://registry.khronos.org/OpenGL/extensions/ARB/ARB_shader_storage_buffer_object.txt
(accessed2026-09-19): std430 SSBO layout and barrier-controlled buffer visibility.
GL_BUFFER_UPDATE_BARRIER_BIT precedes synchronous buffer readback. Separate
invocations write distinct destination words; texture/frame page overlap is rejected.
OpenGL4.3 context verified on RTX5070 driver616.92. No shader int64 needed for
this first path: interpolation stays in checked CPU setup, channel math is32-bit.

## Original scene display-wait counter (2026-09-19)

Read-only own-decoder inspection of the original USA ELF (configured digest).
1bf190..1bf1b0 registers1beda0 for INTC cause2 through26be80 (syscall16),
and1bf1c0..1bf1d4 registers1bed80 for cause3. Handler1beda0 sets GP-30444
and increments the word at GP-30436;1bed80 sets GP-30440. The1beef0 wait
routine clears/polls these flags and compares that counter with object+0x1c,
waiting when the difference is below2 (1bef40..1befd4). This corroborates
original display synchronization; it is not a free-running frame counter or
proof of measured game FPS. Do not relabel callback counts as completed frames.
External inspect artifacts: frame-boundary-inspect, frame-counter-writer,
frame-counter-registration and frame-counter-syscall under TEMP/haunting-toc-probe.
No game data or generated code added to version control; no runtime behavior changed.

## VU character projection and data hazards - 2026-09-19

External character-vu-0 before/after captures select scene4 entry58. Independent
weighted-bone/matrix projection predicts x1969.9667,y2109.4265,z87909.20.
Using captured previous Q=.01104458142 instead produces1956.3359,2094.8307,
87300.93, matching native output1956.3125,2094.8125,87300 after quantization.
Original DIV198 to MULq1c8 has six pair intervals; dependency stalls were absent.
Sony VU User's Manual v6 pp44-48,365-366 (VU_Users_Manual.pdf from
https://github.com/ninjadynamics/PS2Docs) independently specifies per-field VF
hazards, simultaneous-pipeline stalls, four-cycle FMAC/load latency, and seven-cycle
DIV latency. Q itself does not interlock. AOT emission now supplies explicit
register dependencies to bounded runtime readiness tracking, advancing Q/P on
stalls without instruction decoding. Integer loads use4, integer arithmetic1.
Actual game replay validation pending. Existing flag/XGKICK/EFU timing limitations
are not closed by this change. No external renderer implementation consulted.
The separate vu-instruction-manual.pdf is a BigBoss/Jules reference, not the Sony
manual; it was not used as authority for this timing correction.

## PACKED XYZF2 bit fields - 2026-09-19

Independent visual inspection of Sony EE User's Manual v6.0 p154, section7.3.2
(local hg-ee-users-manual.pdf), proves PACKED Z occupies bits68..91 and F
bits100..107; ADC is111. Corrected parser shifts of high64 by4/36 before
packing the GS register. Synthetic nonzero reserved-nibble test checks depth,
fog and ADC. This is a verified parser defect; its contribution to Fiona's
observed geometry/color corruption remains unproven. No external rendering
implementation consulted.

## Post-opening VU1 program identity - 2026-09-18

Fresh continue-vu1-micro capture retains exact VU1 MicroMem under HG-DIAG-006.
MicroMem[0,0x298) matches original ELF3a7558 byte-for-byte:83 instruction pairs.
The immediately preceding original VIF code at3a7554 is4a530000, independently
decoding as MPG NUM=0x53,destination0. MicroMem[0x298,0x2f0) still matches the
tail of the prior95-pair opening program at3ac6b0; it is a stale partial-overwrite
tail. A fault-message-only diagnostic records the actual activation entry as0.
Structural vu-inspect of the83-pair payload reports5 direct control pairs, both
END pairs and every branch/delay slot inside the payload, with no unsupported
decoded pairs. Build-time emit_vu1 succeeds from entry0 without adding operations.
Therefore the second AOT identity is source3a7558,micro0,count83,entry0. No shared
rendering configuration, mapping, algorithm or implementation was consulted.

## Eighth 1bbed0 object family 478b70 - 2026-09-18

Fresh continue-sharded-fc30 replay clears2e5660 and reaches original34e9e0,
RA1bbed8, object1ffeaa0 vptr478b70. Original ELF and fresh EE RAM agree exactly
on478b70=0,478b74=0,478b78=347640,478b7c=34e9e0,478b80=0,478b84=0, bounding
the family to[478b78,478b80). Original347658..664 materializes478b70 and stores
it to object+0. Both methods were missing before this batch. The following478b80
family is left unrooted until live evidence reaches it. Shared non-rendering data
only corroborated the34e9e0 boundary after independent original/live proof.

## Seventh 1bbed0 object family 46fc30 - 2026-09-18

Fresh continue-sharded-d790 replay clears2685f0 and reaches original2e5660,
RA1bbed8, object17f6c50 vptr46fc30. The common caller1bbebc..d0 loads slot+0c.
Original ELF and fresh EE RAM agree exactly on46fc30=0,46fc34=0,46fc38=126170,
46fc3c=2e5660,46fc40=0,46fc44=0, bounding this family to[46fc38,46fc40).
Original126188..194 materializes46fc30 and stores it to object+0. Both methods
were missing before this batch. The adjacent46fc40 family is not rooted here
because it is not yet the live blocker. Shared non-rendering data only corroborated
function extents after the original/live proof; no rendering material was used.

## Remaining post-opening graphics callback descriptors - 2026-09-18

Fresh continue-object46eb60 replay clears2bb3e0 and reaches1b8910 through
member helper100b40, RA1b8e64. The live descriptor block47e300 contains seven
direct triples:1b8ce0,1b8c90,1b8910,1b88c0,1b8890,1b8860,1b87f0. The first
two were already rooted. Original constants3b2f68..3b2fd0 contain the same seven
targets in order, and original constructor464920..464a60 copies the corresponding
three-word descriptors into47e300..47e350. Fresh EE RAM matches every copied word.
Root the remaining five callbacks together. Own decoding shows the small neighboring
bodies directly;1b88c0's only virtual dependency resolves through live global44e550
to an already-compiled target. The supplied non-rendering shared list corroborates
function extents only; no rendering mapping, algorithm or implementation is used.

## Fifth 1bbed0 object family 46eb60 - 2026-09-18

Fresh continue-object46b1c0 replay clears1f2b80 and reaches original2bb3e0,
RA1bbed8, with live object1ffeaf0 holding vptr46eb60. The common caller1bbebc..d0
loads object slot+0c. Original ELF and the fresh EE RAM capture agree exactly on
46eb60=0,46eb64=0,46eb68=2bb220,46eb6c=2bb3e0,46eb70=0,46eb74=0, bounding the
two-method family to[46eb68,46eb70). Original2bb238..244 materializes46eb60 and
stores it to object+0. The three immediate virtual calls inside2bb3e0 resolve in
the same capture to already-compiled1bb980,1c0f10 and1bbbb0, so no speculative
dependency roots are added. The supplied non-rendering shared function list only
corroborates the2bb3e0 extent; all table, installer and live-use evidence is ours.

## Fourth 1bbed0 object family 46b1c0 - 2026-09-17

Fresh continue-vuconvert replay clears the VFTOI0 stop and reaches1f2b80, RA1bbed8.
Live objecta85110 holds46b1c0; original ELF and fresh EE RAM agree on16f680/1f2b80
at46b1c8/cc. Null words46b1c0/c4 prefix the pair and the next table begins with
nulls at46b1d0/d4, independently bounding this family to[46b1c8,46b1d0).
Original16f754..75c materializes and stores46b1c0 into object+0. The caller1bbed0
loads slot+0c and therefore selects1f2b80 for this live object. Added only this
proven two-method family to the existing explicit target union. No rendering
reference material or implementation was used.

## VU0 macro fixed-point conversions - 2026-09-17

Fresh continue-object46ada0 replay clears1bdf80 and reaches original10dda4,
word4be5217c (VFTOI0.xyzw VF5,VF4), RA1b6d54. Sony VU User's Manual v6.0
pp253-256/264-267 defines macro encodings13c..13f/17c..17f; pp77-84
and26-28 define lane masking, toward-zero conversion, signed saturation and
unchanged flags. Source: https://studylib.net/doc/25815876/vuusersmanual.158394566
The macro emitter now calls vu_convert with constant operands/scale/direction.
It reuses the independently derived integer-bit conversion primitives already
in Vu1State; no runtime decoder or reference implementation is introduced.
All four fractional widths0/4/12/15 in both directions are covered together.
Inputs are snapshotted, VF0 remains architectural, masked lanes/ACC/Q/flags
are preserved. Tests cover all masks, aliases, poisoned VF0, integer limits,
exponent-zero/255 inputs, rounding and a translated branch delay slot.
43 Python tests and3 targeted native CTests pass; game emit344672 words /
4525 boundaries. Connected build/replay pending. No new diagnostic introduced.

## Third1bbed0 object family46ada0 - 2026-09-17

Fresh continue-object46d770 replay clears268090 and reaches1bdf80, RA1bbed8.
Objecta84f50 holds46ada0. Original ELF and RAM agree on16f6e0/1bdf80 at
46ada8/ac, bounded by null prefix46ada0/a4 and following46adb0/b4.
Original16f6f8..704 and16f78c..794 install46ada0. Added this exact pair to
1bbed0's verified target union. Permitted exact CSV lookup gave an abbreviated
1bdf80 boundary only; all table/call/installer evidence is original and live.

## Second1bbed0 object family46d770 - 2026-09-17

Fresh continue-object46c770 replay clears25e2b0 and reaches268090, RA1bbed8.
Objectfcda60 holds46d770; original ELF/live RAM agree on267500/268090 at
46d778/77c, bounded by null prefix46d770/774 and trailing46d780/784.
Original267518..24 installs at object+0;2d11d4..dc installs at enclosing
object+40. The caller1bbed0 target set is now the explicit union of these
and the prior independently bounded46c770 pair. Exact permitted non-rendering
CSV lookup only corroborated268090 extent; no implementation copied.

## Object family46c770 - 2026-09-17

Fresh paired2cf3a0 replay clears both switches and reaches25e2b0 from1bbed0,
RA1bbed8. Original caller loads object's slot+0c. Live objectfc72c0 holds
46c770; initialized ELF and RAM contain25c850/25e2b0 at46c778/77c, bounded
by null prefix46c770/774 and following null46c780/784. Original25c868..70
and2d1370..7c publish46c770. Rooted only this proven two-method family.
Exact permitted non-rendering shared function-list lookup was a boundary lead
only (its reported25e2b0 extent is short); original control flow is authoritative.

## Paired selector switches in2cf3a0 - 2026-09-17

After the1fbfb0 root, connected replay clears1fbfc0 and stops at2cf4e4,
RA207854, selector1. Original caller20784c invokes2cf3a0. Its byte-masked
selector is bounded below8 at2cf3bc before dispatch2cf3dc through45d7a0.
The same routine bounds the selector below8 at2cf5e8 and dispatches at2cf608
through45d780. Both complete eight-word tables match fresh
continue-switch1fbfb0.ram.ee-ram.bin and contain executable internal labels.
The permitted non-rendering shared lookup supplied a same-routine table lead;
all addresses, bounds and targets were independently checked in original ELF
and current RAM. No reference implementation or rendering material used.

## Selector switch 1fbfb0 - 2026-09-17

Post46f480 replay clears2d3e80 and reaches1fbfc0 with a1=f1, RA202bf4.
Original1fbf70 masks the selector to a byte, bounds f0..fa, subtracts f0,
and1fbf90 explicitly bounds the resulting index below11.1fbf9c..b0 loads
and jumps through table[456420,45644c). Its11 executable case labels agree
with fresh continue-46f480 RAM and end at null45644c. Exact permitted shared
non-rendering lookup only corroborates enclosing routine1fbf70..1fc028.
Original bounds and data independently authorize this AOT root family.

## Object family 46f480 � 2026-09-17

Fresh connected capture `%TEMP%/haunting-toc-probe/continue-2d3e80.ram`
confirms object18eca40 vptr46f480 and virtual slot+0c target2d3e80 at caller
1ffd34. Original ELF installers2d3aac..2d3ab8 and39aa38..39aa48 publish
that vptr. The 24 initialized executable words in [46f488,46f4e8) agree
with live RAM, with null prefix46f480/484 and trailing null46f4e8/ec.
The bounded family is rooted for AOT translation. Exact permitted non-rendering
shared function-list lookup corroborated only the2d3e80 function extent;
no implementation or rendering material was consulted.

# Sources and provenance

## Connected object-dispatch table 46e4c0, 2026-09-15

Original USA ELF code and initialized data independently establish this root
batch.  Connected execution reaches the original virtual call at
`0x0020956c..0x00209578`; it loads slot `+0x0c`, whose live target is
`0x002b1100`.  That pointer occurs exactly once in initialized data at
`0x0046e4cc`, fixing table base `0x0046e4c0`.  Original
`0x0020a868..0x0020a874` installs the base, while `0x0020a878` begins the next
table at `0x0046e500`, bounding this family to `[0x0046e4c0,0x0046e500)`.

The non-null file-backed executable entries are `2b10a0`, `2b1100`, `2b1120`,
`2b1130`, `209800`, `2097f0`, `2b1140`, `2b1150`, `2b11a0`, `2a8940`,
`2b1110`, `2b1180`, `2b1170`, and `128080`.  They are rooted as one bounded
family to avoid redundant rebuild/replay cycles.  Generation after the batch is
254865 reachable EE words / 2463 unresolved items.  The Release rebuild completed
successfully; post-build connected replay is pending.

Only after the original table address, installer, bounds, live slot, and executable
entries were established was the supplied non-rendering shared HG configuration
searched for function-boundary corroboration.  No shared rendering material,
rendering mapping, algorithm, or implementation was inspected or copied.

## Connected object-dispatch tables 46d750/46d7b0/46eb40/46ec60, 2026-09-15

Original ELF code and initialized data are the authority for this root batch.
At original `0x266d4c..0x266d5c`, the live object call loads vptr slot `+0x0c`;
the fault target `0x2674f0` occurs exactly once in initialized data at
`0x46d75c`, fixing table base `0x46d750`. Original `0x267328..0x267334` installs
that table, while `0x267338..0x267340` installs distinct base `0x46d730`. The
bounded executable entries at `+08..+18` are `267310`, `2674f0`, `2674e0`,
`2673e0`, and `267370`.

The same original dispatcher later calls `0x26b480` through slot `+0x0c`.
Original `0x267020..0x26702c` and independent triage at `0x2083b0..0x2083bc`
install `0x46d7b0`; its five file-backed executable entries are `26b1e0`,
`26b480`, `26b350`, `26b2c0`, and `26b240`. After these roots, the identical
connected replay clears `0x26b480` and stops at original `0x2bb390`, RA
`0x266eb8`, after 29,068,787 slices. This is direct runtime evidence that the
earlier virtual target no longer blocks the path.

Original `0x267038..0x267044` installs `0x46eb40`; independent triage also finds
the earlier installer at `0x208348`. File-backed words at `+08..+18` are
`2bafd0`, `2bb390`, `2bb280`, `2bb1a0`, and `2bb030`, followed by zero at
`+0x1c`. The immediately following original branch `0x267050..0x26705c`
installs `0x46ec60`; triage also finds installers `0x208f18` and `0x2242e0`.
That table contains `2c64e0`, `2c6630`, `2c6620`, `2c6570`, and `2c6540`, again
followed by zero at `+0x1c`.

Only after each table, installer, and bounded executable entry was independently
derived from the original inputs were exact non-rendering shared-configuration
searches used to corroborate function-boundary leads. No shared rendering data,
rendering mappings, algorithms, or implementation were inspected or copied.
The roots preserve the original bodies and remain ordinary AOT discovery inputs;
no runtime decoding/interpreter/JIT fallback is introduced.

## Packed matrix word interleave75,2026-09-15

Sony EE Core Instruction Set Manual v6.0 (April2002),pp205/208,
independently establishes PEXTLW/PEXTUW:opcode28,subcode18,functions8/40.
The selected low/high64-bit source halves are interleaved by32-bit words,
with rt supplying each low word and rs each high word. Both128-bit source
registers are snapshotted before a destination write;architectural zero
reads and discarded writes are preserved. No arithmetic flags change.
Primary manual archive (HTML):
https://www.scribd.com/document/784545190/EE-Core-Instruction-Set-Manual

Original10dc10..1c contains71286488,71286ca8,716a7488,716a7ca8;
our original-only decoder inspection through10dc58 verifies the immediate
matrix rearrangement and subsequent previously implemented VU chain.
No shared rendering material or emulator implementation was consulted.
Native helper selection is fixed at generation;no runtime decoding added.
Synthetic tests cover all source/destination registers,both aliases,poisoned
zero storage,every input bit,unchanged unrelated registers and a JR delay
slot that overwrites the jump-source register. Candidate75 validation and
post-opening replay are pending. This is instruction coverage,not timing
or physical-console parity. No diagnostic family added.

## Bounded VU broadcast products74,2026-09-15

Sony VU User's Manual v6.0 (April2002), macro VMADDbc p273,
VMADDAbc p277 and VMULAbc p303 independently establish the new static
instruction encodings. ACC forms match low11 bits with bc masked:
VMULA 0x1bc, VMADDA 0x0bc; VF-destination VMADD has function8..11.
The original matrix-vector words at10dabc..10dac8 select these forms.
Primary manual archive: https://studylib.net/doc/25815876/vuusersmanual.158394566
No shared rendering material or emulator implementation was consulted.

decode.py and emit.py select native vu_broadcast_product calls at build time.
VMULA writes selected ACC lanes, VMADDA adds into selected ACC lanes, and
VMADD writes selected VF lanes while preserving ACC. Inputs are snapshotted
before output changes, including broadcast-source/destination aliasing and
architectural VF0. The manual's numeric rules pp26-29 and flags pp39-42,
together with micro MADDbc pp88-89, define the bounded arithmetic profile.
Normal accumulated-result Z/S and sticky Z/S describe the final sum; a
negative or zero intermediate product alone does not set them. Inactive
MAC lanes clear; unrelated I/D and accumulated status bits remain.

The existing independently written integer24-bit product/add primitives
provide truncation and signed-zero handling. Physical-hardware least-bit
agreement remains unmeasured. VMULA retains documented product saturation
and underflow flags. Accumulating forms explicitly fault on product overflow,
product underflow, ACC exponent255 or final addition overflow/underflow:
these combinations are not inferred from COP1's different MADD policy or
from ambiguously flattened exception-table rows. Results and flags commit
only after all selected lanes succeed. These correctness faults are not
diagnostic overrides; no new diagnostic family is introduced.

Tests cover all16 masks/four broadcasts/three forms, aliases, VF0, signed
zero, final-result flags and atomic exceptional rejection. An independent
synthetic program checks ACC chains, a VF result in a JR delay slot, budget
boundaries and the exact delay-slot fault PC.40 Python tool tests pass.
Generation yields203680 reachable words/1801 issues with no budget
exhaustion. ALL_BUILD passes and all25 CTests pass13.08s.74skip advances
past the broadcast matrix transform to original10dc10 unsupported71286488,
RA10e170,modeled28234190us.359 sampled images retain the known final hash.
No new scene or speed measurement is established.

### EE COP1 MSUBA.S gameplay path (2026-09-17)

Fresh connected gameplay execution reaches original EE 0x1f11b0, word
0x4616b01f, after the independently rooted 0x46b0d0 resource interface.
The EE COP1 single-precision arithmetic encoding uses function 0x1f with
the destination field zero for MSUBA.S, the accumulator-only subtract form
paired with the already modeled MSUB.S/MADDA.S operations. The independent
decoder now recognizes that exact encoding, the emitter calls a dedicated
runtime helper, and the helper updates ACC through the existing verified COP1
msub arithmetic while preserving the ordinary destination registers.
Focused decode/emission tests reject nonzero destination-field encodings, and
the FPU test covers an ACC chain 6 -> 12 -> 6 through MULA/MADDA/MSUBA.

## Original post-movie view setup73,2026-09-15

Original33e2a0 selects global44e4b8/object1961500/table469a60. Its virtual
slots+1c/+5c/+28/+14 select1225e0/122030/1225b0/122810 in order;72skip faults
at the first call,RA33e318. Original122810 selects+18=1225f0,+bc=121b40 and
+44=122090. All seven original/live72 slots agree and lacked compiled labels.
All seven are added as bounded AOT roots, preserving original bodies and direct
discovery.1225f0 is inspected through return/delay122804/08;121b40 through
121c48/4c;122090 tails to original10dd68 at122098/9c.122810's complete original
body ends122a14/18. Original1225f0's alternate branch uses global44e550,
object887a00,table46ab50+18=1a4510; original/live agree and it is already compiled.
Evidence external post-opening-view-dependencies-72skip.json records input hashes,
slot checks and the alternate dependency. Earlier view-and-load72 proof records
use sites and following asset-finalization candidates. Only original executable
and native captures used; no shared rendering material or emulator implementation.
Candidate73 generates203665 words/1803 issues without cap exhaustion; both
diagnostics build and six startup regressions pass4.84s.73skip passes position,
scalar and target writes and enters122810/1225f0. It faults at original10dabc
unsupported4be821bc,RA12272c,modeled28234190us.359 sampled images,last hash
aa51ff064036e5a49ef1278494344a67796499ebfb13b0985ebcca7e16b4ad88.
No completion of all seven callbacks,asset loading or newly rendered scene is
claimed. Captured view scalar,position,target and normalized direction fields
are retained in73skip RAM. Original10daa8 uses broadcast multiply/accumulate
words4be821bc,4be828bd,4be830be,4be83a4b before writing its transformed vector.

## Original post-movie state batch71,2026-09-15

Original44c7b0/44c7a0 triples equal70skip's888444/18db890 descriptors,
selecting3a06e0/3a04a0. Original3a0628/38/40 reads44c7c0=(0,ffffffff,3a0390),
then3a0644/4c/54 installs it at object+1053450. Original3a0390 is verified
through return/delay3a0498/49c. Shared non-rendering lists supplied only an
extent lead; original instructions established the actual boundary.

Original3a052c/3a0698 selects44e568/objectfc77b0/table46c3e0+0c=21c760.
Both copy/reset branches retain the13-word loop and dispatch21c798 through
+90=21b2b0; its21b324 dispatches+60=21b160. These three slots agree in the
original executable and70skip capture. Complete original bodies were inspected,
including returns21c7d4/d8,21b3cc/d0 and21b1ac/b0. Original3a0558 selects
44fe08/objectfd0640/table46c320+0c=21af90,which tails through global44e4e0's
46a1e0+0c=16bbd0. Original/live slots agree. Original3a03dc uses that same
object80adc0's+24=16b510. Both16bbd0/16b510 were already statically translated.
Seven missing roots only are added; all original branches and direct discovery
remain. No shared rendering material or emulator implementation was consulted.

Evidence external post-opening-state-batch-70skip.json retains original/capture
hashes,three state descriptors and initial virtual-slot/installation evidence.
The following+90/+60 and44e4e0 slots were verified by bounded read-only checks.
Two optional combined inspection commands were tool-blocked; narrower checks
succeeded. No blocked command's proposed output is claimed.
Generation71 reaches the configured200000-word discovery cap with1779 boundaries;
all seven root labels are present. Both diagnostics built and six startup checks
passed4.86s,but71skip failed early at EE16af48,RA16af40,3309039us,zero images.
That PC is explicitly listed among the ten instruction-budget frontier entries;
analysis-71-capped.json preserves the exact report. Candidate72 raises only the
bounded discovery work limit to400000. Fresh generation finishes at202888 words,
1795 boundaries and zero instruction-budget exhaustion. Both diagnostics build;
all25 CTests pass8.22s.72skip reaches EE1225e0/RA33e318 at28234189us,
with358 sampled images. This passes3a06e0 into original33e2a0 view setup.



## Original view-initialization callbacks70,2026-09-15

69full's original3a0b04 dispatches global44e4b8/object1961500 through
469a60+0c=122a30. Original20e260 stores the global and installs469a60 at
20e270. All original/live slot words agree. Complete original122a30..122ac4
initializes scalar/vector fields and tails through table+78=121fd0; original
121fd0..121fe0 stores0,-1,-1 to object+290/+294/+298. These two missing
entries alone are added as AOT roots. Following3a0b20/3a0b34 callbacks through
global44e560/object887204/table46bf2c select2106b0/210640; both match original
words and were already compiled. No copied or guessed rendering behavior.
Evidence: external post-opening-view-init-69full.json includes ELF/capture
hashes, live/original words and installation provenance. Generation70 yields
171110 reachable EE words/1190 boundaries. Both diagnostic targets build;
all25 CTest checks pass14.02s.70skip passes both view callbacks and stops at
missing EE3a06e0,modeled28234187us. Eleven selected view fields agree with the
original writes;359 sampled images retained. No new scene is established.


## Full OPENING and later view-initialization boundary69,2026-09-15

The unskipped69full replay completes6300 OPENING CSC starts/54366 sectors.
All6518 startup guest conversion timestamps equal61full. First-to-last
OPENING takes210.7389host seconds for210.241230 modeled seconds. Console
identifies missing EE122a30,RA3a0b0c,a01961500,a14f1838,234941153us.
Generic native-iop-fault metadata is not the processor classification.
Refreshed hg_diagnostic builds; all six original startup regressions pass4.17s.
This confirms the extended original path after the full movie, not a new scene
or asynchronous asset-load completion. See PERFORMANCE.md for timing limits.


## Post-OPENING graphics callback descriptors,2026-09-15

Candidate68 built with171027 reachable EE words/1189 boundaries; all six
refreshed startup checks pass (4.16s).68skip passes the original16-record
initialization and reaches1b8ce0,RA1b8e64,a04f1920,a1=0,modeled28201035us.
359 sampled images and complete sidecars retained. New scene pixels and
asynchronous asset-load completion are not established.

Original1f9d90 selects1b8d30 twice with a2=0 and a2=1 (1f9e68/1f9f04).
That body computes47e300+selector*12 and calls original100b40, which tails
through the descriptor's target when its middle word is negative.68skip's
first descriptor is(0,ffffffff,1b8ce0); the second is(0,ffffffff,1b8c90).
These runtime addresses are BSS, not file-backed original words. Original
constructor464920 copies the corresponding original constants at3b2f68 and
3b2f78 into those descriptors, including stores464940/464960 and
464970/464984/4649ac. Those independently inspected source triples agree.
Both complete original bodies clamp/adjust the input and replicate its byte;
candidate69 adds only1b8ce0 and1b8c90, retaining original instruction behavior.
No shared rendering reference was used. A combined optional capture-comparison
command was tool-blocked and did not create post-opening-palette-68skip.json;
do not claim its proposed16-record byte comparison was performed.

## Post-OPENING indexed record initialization,2026-09-15

Candidate67 built with170990 reachable EE words/1189 boundaries. All six
refreshed startup regressions pass (4.14s).67skip passes the prior1a4110
boundary and stops at original EE1fa5e0,RA1f9cfc,a017f4600,a11ffebe0,
modeled28201030us;359 sampled images and complete sidecars are retained.
This is further original execution, not completed asset loading or a new scene.

Original1f9c80 clears object fields, prepares12 words on its stack and calls
object table+20 at1f9cf4 for indices0..15. Our original-image analysis records
table46b300+20=1fa5e0, with its table installation at2d0900. The observed EE
fault selects that exact target. Original1fa5e0..1fa670 copies those12 words
to object+20+index*30 (hex offsets), retaining the original loads/stores.
Candidate68 adds this one observed root. Record semantics beyond the field
copy remain unproven; no shared reference was consulted for this potentially
rendering-adjacent initialization. No guessed implementation or hardware change.

External post-opening-asset-requests-66skip.json now records all ten original/
live table comparisons for candidate67, original filenames and both input hashes.
Fresh bounded67skip field reads agree with original46b300+20=1fa5e0.
Saved original returns1f9da8 and3a0a4c establish the later call chain after
3a0a34 returned from176550. The six-slot startup list still holds1510c80,
16be3c0 and four nulls, with the expected request-method slots. Thus both
original request callbacks returned before this reset; asynchronous asset-load
completion remains unproven. External post-opening-records-67skip.json records
the original/live words, saved returns and input hashes. Its three following
reset dependencies1c2930,1c0f10,1b8d30 are already statically translated.

## Post-OPENING asset requests,2026-09-15

Candidate66 built with170654 reachable EE words/1175 boundaries; all six
refreshed game startup tests pass (4.19s).66skip passes both startup initializers
and reaches the following list traversal176550, stopping at EE1a4110,
RA17659c,a01510c80,modeled28200869us. The second initializer's original
object+f3668 store is now2.355 sampled images and complete sidecars retained.
This verifies further original execution; no new scene pixels are claimed.

Original176594 uses slot+14 of the same six-entry list. Original/live tables
46aab0 and46a120 select1a4110 and168830. These bodies select filenames through
object+f0's table slots+a0/+a4/+a8, then call already-translated16bbd0 through
global44e4e0,live table46a1e0+0c. First resource objecta84f40/table470620
selects2f7b80/2f7b70/2f7b30; original bodies return O_FIS/FIS_000.PCK,
O_FIN/FIN_000.MRK and O_FIS/FIS_000.TEX strings (original separator backslash).
Second objecta84380/table46b240 selects1f8090/1f8010/1f7f00. The first two
use original bounded selector branches; the third returns a fixed string.
Original1a4294 also selects global44e558's objectfc8900/table46c540+48,
target221e70, whose original body computes object+10c0+selector*2000.
All relevant original/live slots agree. Candidate67 adds only these nine missing
roots;16bbd0 is already compiled. The exact non-rendering shared-config lookup
only confirmed1a4110's original extent. No renderer material was consulted.

## Post-OPENING startup initializers,2026-09-15

Candidate64 builds successfully and all25 refreshed CTests pass (13.29s).
Exploratory64skip passes3a1990 and stops at original1a4340,RA17669c,
a01510c80. Original176650 bounds the list44f800 to six entries; the live
list contains1510c80 and16be3c0 followed by four nulls. Their tables46aab0
and46a120 are installed by original2d078c and2d07ec. Original176694 calls
slot+0c, selecting1a4340 and168a10. The original/live words agree.
Original1a43b8 uses the first table's+5c slot19ac70; its direct125cc0 call
tails through+60 to19ab40. Original19ae44/19aea0 additionally uses global
44e550's live object887a00, table46ab50+1c=1a44c0. Original1a44d0 then
calls that table's+10=1a45a0. Both original/live entries agree. These six
specific roots are added together; no other methods are inferred as needed.
The original1a45a0 body reads and tempers a random word; no substitute is used.
Exact non-rendering shared-config lookup for1a4340 only confirmed the bounds
already established by the original return at1a43c8. No implementation or
rendering reference material was used. External post-opening-startup-object-64skip.json
records the original-image and live-table evidence. Candidate65 built and all
six refreshed game startup regressions passed (4.12s). Its65skip replay reaches
the first initializer/reset chain, then stops at missing EE target1aab80,
RA125c14,a017b1b80,a1=-1.358 sampled images were retained. The generic
native-iop-fault metadata does not override the explicit EE console fault.

Original125c0c selects global44e588's object17b1b80, table46abb0+28=1aab80.
Original2d07e8/2d07f4 installs that table;2d07cc stores the global object.
The complete original1aab80..1aabb4 either returns for selector-1 or clears
the selected object+4 bit. Original125c68 then selects46aab0+98=17fd40,
whose original two instructions return1. This selects125c98's call through
global44e4d8's object888480, table47a7e8+34=177cc0. That original body
ends at177dac and resets bounded related records through2a84c0/2a8500.
All three original/live slots agree. These three precise roots form candidate66;
their original bodies are translated without substitutes. Narrow non-rendering
shared-config address lookups only confirm original function extents.

## Post-OPENING scene dispatch,2026-09-15

Exploratory63skip advances past2c9480 and stops at3a1990 through2d1bc0,
live object888440 slot+0c. All four original/live methods in47a7d0 agree;
2d06bc/2d06d0 forms that pointer and2d06e0 installs it.2d06e8 independently
installs47a7e8 at object+40, proving the next subobject table boundary.
3a1990 increments object+73ee40 then tails into the original11f9c0 state
routine. Exact shared non-rendering lookup confirms this sequence and leads
to3a07d0's switch, which was independently decoded:3a0c44 zero-extends the
selector,3a0c48 checks unsigned<9,3a0c64 reads464420+index*4,and3a0c68
jumps to it. All nine original/live table words match. Four method roots and
that bounded indirect table are added; no substituted implementation used.
Evidence: external post-opening-scene-63skip.json. Candidate63's25 refreshed
CTest checks pass; candidate64 runtime verification is pending.


## Post-OPENING object table,2026-09-15

61full completes6300 original OPENING conversions, then the original EE call
at120ca0 selects slot+8 of live objectfcda00's table46ecf0: target2c9480.
Original2d1174/2d1184 materializes and installs46ecf0;2d0d84/2d0d8c also
installs it. All16 words at46ecf0..46ed2c agree between the verified ELF and
61 EE RAM: two leading nulls,eleven methods at+8..+30,then null46ed24 and
alignment before the next table46ed30. Original2c9480 clears four fields
and initializes64 entries at stride b0 using25fc50 and1f40f0. This is
independently decoded object initialization, with no substituted rendering.
The eleven bounded method roots were added together; no runtime discovery
or instruction fallback was introduced. External post-opening-table-61full.json
records exact slots, image hash, live address and constructor/caller evidence.

Only after this original-input derivation, an exact non-rendering initializer
lookup of2c9480 in the authorized shared HG configuration confirmed its bounds
and described the independently verified2c9470 slot+0c tail thunk to slot+8.
No shared rendering material, implementation, or algorithm was used. Candidate63
generation reaches163566 words with1081 recorded boundaries; runtime validation
is pending. Static coverage counts do not establish later game execution.

### Per-frame metadata qualification

External original-frame-timestamps.json records independent ffprobe frame reads.
CAPCOM has181 available monotonic timestamps,0..6.0s, plus0.033333s last dwell.
LOOP_DEMO's last frame lacks a best-effort timestamp; its available timestamps
are not all strictly increasing. OPENING has6300 timestamps,0..209.966667s at
the endpoints, but these also are not all strictly increasing. All three have
repeat_pict=0 and no interlaced frames in this analyzer. Do not silently fill
missing timestamps or equate nominal30fps intervals with every source PTS.
The frame-count/duration analysis and native CSC timing remain distinct.


## Original full movie extents and pacing comparison (2026-09-15)

Read-only original ISO extents come from our existing CVM index at origin
2991302656bytes. LOOP_DEMO: ISO offset4138942464,size63602688,SHA256
e4ee4587bcc36cd71c2a618cda4e19976006724584bc2ee45f7234c1da939ba4.
OPENING: ISO offset4328001536,size111341568,SHA256
b5679f5137f44e77fa3de969fbff5b852b266dda9bbff1bc12d011322bb77d55.
External copies and original-movie-timing.json are in haunting-movie-reference60.
Installed ffprobe independently decoded/counts2968 and6300 video frames,
respectively, each512x448 MPEG2 at30/1fps. LOOP reports98.9s video and99.008667s
container duration; OPENING reports210s for both. Last-frame dwell and CSC
first-to-last spans must be distinguished. No analyzer is a runtime dependency.

Original fixed-input native58/59 execute identical1066 OPENING timestamps and
end with identical EE/GS/machine state.59 paced host span35.5496s versus58's
34.9217s,for35.611639s modeled time. This is a partial original movie measurement,
not its full210s and not an inspected longplay/physical-console comparison.


## Host playback pacing and movie-rate validation (2026-09-14)

Native53 CAPCOM:181 CSC starts span6.090484 modeled seconds in4.7007 host
seconds; OPENING1066 starts span35.611639 modeled seconds in36.1589 host seconds.
These independent native observations identify stage-specific overspeed that the
opening average alone hid. Original CAPCOM bytes were copied read-only to an
external temporary file and inspected using the installed ffprobe analyzer:
512x448 MPEG2,181 decoded frames,30/1fps,6.033333s stream duration. ffprobe is
not a runtime or build dependency. Artifact: haunting-movie-reference54/capcom-timing.json.
WorldofLongplays US reference: https://www.youtube.com/watch?v=eY1SSSzx9DA .
The page title, creator and4:44:38 duration were read; transported screenshots
omitted images, so visual landmarks and capture provenance remain unverified.

HostDeadlineWaiter is independently written against Microsoft's API contracts:
https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw
https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-setwaitabletimer
https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject
The timer is unnamed, non-inheritable, high-resolution, one-shot and relative in
100ns units, with no APC or resume request. Handle lifetime is scoped to the
runner; system-wide timer settings are not changed. Host deadline policy consumes
elapsed modeled time only and changes no guest state, time allocation or inputs.
Windows requires high-resolution timer support; creation/wait failures are explicit.


## OPENING audio buffer overlap (2026-09-13)

Replay18 starts OPENING but stalls after four conversions. Original2429c8
gets audio position through242a88/1d3210/1d30c8. Replay19 shows8192 produced
samples minus two8192 queued counts, clamped to0 by original1d31c0..1d31e4.
The live audio synchronization buffer121b00..1223d0 matches2254/2256 bytes of
the original DS2O_S1.IRX at file offset1d60. Its0x880-byte transport payload
at121b40 exactly matches EE3cf1c0. External opening-audio-buffer-overlap.json
records hashes and extents. Original inputs and our native captures only;
no emulator or shared implementation used to derive the allocation fix.

MODLOAD's native scratch arena started at1MiB independently of original
SYSMEM, whose SIF heap client had allocated this live audio buffer. Connected
ordinary native storage now delegates to original compiled SYSMEM exports4/5.
Static module space and native call stacks are reserved; worker stacks and
HEAPLIB storage share that allocator. This executes original AOT routines,
with CPU context preserved on a separate call stack, without decoding code
at runtime or substituting audio time. Connected verification is pending.

## GS device progress after CAPCOM (2026-09-13)

Replay12 reaches100M modeled microseconds without fault but retains4830
submitted draws after the last15355completed draws, leaving scanout stale.
Original submissions continue each field. GS User Manual v6 pp38-43 states
drawing begins when a kick has sufficient vertices and maintains register and
transfer order. The connected device phase now services the existing ordered
draw queue after VIF/GIF submission, independently of preview observation.
FINISH/transfer ordering checks remain. This is functional execution, not exact
pixel-pipeline latency. Derived from the local manual and native evidence only;
no shared rendering material or emulator implementation inspected or copied.

## Visible CAPCOM movie evidence (2026-09-13)

startup-movie-services runs the independently translated original ELF through input streaming, intra/non-intra BDEC, native original motion-copy code, CSC and original draw submission. External movie-services-frames/manifest.json hashes sequential captured PPM bytes; PNG006/007/008/013/028 visibly show the CAPCOM logo with Fiona/Hewie fading in. OpenGL watch-display uses the same published PPM. Runtime reached500M slices without a fault; final native-iop-budget sidecars are retained. This establishes advancing movie imagery, not full playability or real-time/audio correctness. Registry expansion used seven14-entry tables, verified against original ELF and RAM in movie-service-tables.json; no shared rendering reference was used.

## Motion prediction transport and instructions (2026-09-13)

Original2a0dd4..e6c loads two8-entry callback tables3ed6d0..710, storescontext+5c8/+5d8; consumers2a0f20/34. All entries agree with startup-csc RAM. QFSRV/PEXTLB/PEXTUB/PCGTH/PSLLH/PSRLH/PSRAH derive from EE Core Manual pp286/203/206/185/261/267/264. Tests check all16 byte-funnel shifts, source aliases, byte order, signed comparison and lane shifts. No shared rendering material consulted. toSPR source-chain derives from EE User Manual DMA chapter45-46/73-76 and original tags1990500..530 in startup-nonintra. Supports CNT/NEXT/REF/REFE/END, TIE, saved transfer continuation, SADR wrap; unsupported modes remain faults.

## Non-intra BDEC and prediction copy (2026-09-13)

Original2a1fe4 issues20010000 in startup-csc; adjacent2a28d0 copy uses PADDH. EE manual pp191-192 and H.262 TableB.9/PDF140 define CBP. H.262 sections7.4.2-7.4.4 define inverse quantization. CBP table independently transcribed with64-entry, complete-mask and prefix-free validation. Signed floor quantization and IDCT limitations are recorded in ORACLE.md. PADDH is derived from EE Core Instruction Manual p161 with synthetic modulo-overflow/lane-alias checks.

## CSC RGB32 (2026-09-13)

Original2a7110 issues70000380 (896 macroblocks), captured startup-movie-callbacks. RAW8/RGB32 layout and conversion derive independently from EE manual pp177-180,197,205-206. Integer coefficient and threshold formulas plus measured rounding discrepancy are recorded in docs/ORACLE.md. RGB32 without dithering only; native input/output pressure and multi-block state retain actual data.

## Movie pixel-copy translations (2026-09-13)

Original ELF routine2a2998 uses ADDI, PMAXH, PMINH and PPACB. Independently derived from EE Core Instruction Manual pages25/396,222,235,256. Page396 opcode map resolves the erroneous ADDI encoding graphic on page25. Native synthetic tests cover overflow, sign extension, signed lane selection, source/destination aliasing and byte order. Reserved-field mask word00ff00ff is separately measured in docs/ORACLE.md; only that exact encoding is admitted as DSRA32.

## IPU output and intra BDEC (2026-09-13)

Original2a392c arms DMA3 to scratchpad80001800/QWC48;2a1fe4 issues BDEC
2c010000. EE manual pp174-176,180,191-192,201 defines RAW16 output,
commands, frame layout and symbols; DMA register chapter defines normal
fromIPU transport. Native output waits for real data and validates destinations.

Coefficient/DC tables, inverse scanning, inverse quantization and mathematical
IDCT are independently derived from ITU H.262 (2000), sections7.2-7.5,
Annex A and tablesB.12-B.16. Official PDF downloaded to %TEMP%/hg-itu-h262.pdf,
SHA2563d38fcd0096da6d27b4c63f3483cc65e55bbd61e254c9213fccf8f0d2df23617.
Source https://www.itu.int/rec/T-REC-H.262-200002-S/en . Numeric codebooks
are specification data, not copied software algorithms. runtime/ipu.cpp uses
a directly derived separable cosine transform. Intra no-mismatch and0..255
clipping follow the measured synthetic profile in docs/ORACLE.md, whose
limitations must not be represented as physical-console accuracy. Non-intra,
MPEG1, field-DCT and CSC remain explicit gaps. hg_ipu is a separate native
library to avoid repeated AOT recompilation for block-decoder implementation
changes. This is data decoding for the statically recompiled program, not
a runtime instruction interpreter or an emulator execution backend.

## IPU VDEC (2026-09-13)

Original2a2174 in startup-picture-switch writes30000000, address increment.
EE User Manual v6 pp193,202-204 specifies all seven table variants: address,
I/P/B/D type, motion and DMVector. ipu_vlc.hpp contains declarative data
transcribed from those specification tables, including MP1-only stuffing
and packed length/result. Cross-check for address increments: ITU H.262
Annex B.1, https://www.itu.int/rec/T-REC-H.262-200002-S/en . No third-party
implementation read or copied. Tests cover every entry at all128 offsets,
independent packed examples, error/ECD and pending input before/after symbol
consumption. Cycle timing and memory-oracle parity are not claimed.

## Shared IPU stream and FDEC (2026-09-13)

Independent implementation from EE User Manual v6 pp182-188,194-196: separate
eight-qword FIFO/internal qwords, MSB-first FDEC with FB0..32 and a retained
32-bit result, CMD/TOP busy reads, BCLR, and shared SETIQ/SETVQ consumption.
Commands wait for absent input and resume when DMA/CPU supplies qwords.
Synthetic observation matches and limitations are in docs/ORACLE.md. The
empty-input FB crossing discrepancy is explicitly rejected. Non-byte-aligned
table extraction follows the manual and has synthetic tests; it is not an
oracle measurement. Reset-only prefetch timing remains unverified; reset
retains the existing no-prefetch profile until BCLR or a stream command.
No image decode/output or cycle-accurate processing is claimed. No external
implementation or shared rendering material was consulted.

## IPU input DMA endpoint (2026-09-13)

Native startup-movie-timing reaches original24ed44, writing CHCR30000105
to1000b400 after original24f230/24f26c constructs REF/REFE tags through
24ed68. EE User Manual v6.0 pp44-46 defines normal/source-chain copying;
p62 defines suspend/restart and retained TAG, pp73-76 identifies channel4
and its registers. Page182 specifies an eight-qword IPU input FIFO.
Implemented independent channel4 transport for normal and CNT/NEXT/REF/REFE/END
chains, saved MADR/QWC continuation, FIFO backpressure and completion flags.
The connected runner pumps one bounded transfer step per slice. Full FIFO
retains STR and pending bytes; it never invents consumption. Unsupported
tag priority/stall/stack and tag transfer remain explicit faults. IPU decode
commands and output are still absent. Tests exercise copying, pressure/resume,
chain continuation/IRQ completion, invalid input and mode rejection.
No shared HG rendering material or PCSX2 implementation was consulted.

## EE second-pipeline division (2026-09-13)

Connected startup-stream-copy stops at1e3774, original word7066001a.
EE Core Instruction Set Manual v6.0 pp138/140 defines MMI functions26/27,
zero rd/sa fields, signed/unsigned 32-bit division into sign-extended LO1/HI1.
Original1e3770 executes DIV and1e3774 DIV1, then reads HI/HI1 separately
to compare ring-buffer remainders. Extended the existing checked division
helper with bank selection and added both encodings; undefined zero-divisor
and noncanonical operands retain explicit diagnostics. Synthetic AOT tests
cover four sign combinations, INT_MIN/-1, unsigned results/sign extension,
bank preservation and fault atomicity. Timing remains the existing functional
interlock model, without cycle-accurate pipeline latency. No reference
implementation was consulted; shared non-rendering address search had no lead.

## EE unaligned-store byte preservation (2026-09-13)

EE Core Instruction Set Manual v6.0 pp99-102 (SDL/SDR),117-120 (SWL/SWR),
read from external hg-ee-core-instruction-manual.pdf, defines left stores into
the low memory lanes and right stores into the high lanes. Our EE helpers
incorrectly reused load-direction masks while shifting store data in the
opposite direction, clearing unrelated bytes and retaining stale destination
bits. Corrected only the two store masks. Byte-wise synthetic tests cover
every offset for both widths, individual partial writes, and paired writes in
both orders with nonzero neighboring bytes; the old implementation fails.
Original2437f0 constructs a descriptor with capacity input minus padding, and
243d48..78 copies it using LDL/LDR and SDL/SDR. Stream1 record10091bc has
its descriptor at10091cc (four-byte offset); its zero capacity and callback
pointer in startup-parser.ram are consistent with this corruption. Connected
replay startup-unaligned-fixed confirms capacity0x5b800 and callback0x3cffa8
are now preserved; it reaches callback2419d0 before the former parser error.
All21 Windows Release tests pass. No creation parameter is patched.
No emulator implementation or shared rendering material used.

## Normal scratchpad DMA (2026-09-13)

Original23c144 selects channel8;23c160 writes SADR and23c170 calls the
original normal transfer helper10d888, which stops at STR write10d8f4.
Live startup-movie-vtable capture has s16=1000d000, destination197aa80,
QWC argument400 (1024 qwords). Derived normal burst copying independently
from EE User Manual v6 pp43-44, fixed channels p73, DIR applicability p74,
main-memory MADR p75 and14-bit SADR p78. Local manual is
%TEMP%/hg-ee-users-manual.pdf. Both fixed directions8/9 are implemented in
the connected runner; full RAM-span validation precedes copying, SADR wraps
to14 bits, completion clears STR and raises channel D_STAT. Chain/interleave,
MFIFO/stall modes remain explicit gaps. Cycle-accurate arbitration is not claimed.
No shared rendering or emulator implementation material was consulted.

## Positive SQRT power-of-two profile (2026-09-13)

Original1c6c50/54 loads40000000 into ft22;1c6c64 executes SQRT.S fd20,ft22.
EE Core Instruction Set Manual v6.0 p376 specifies the operand fields,
square-root result and I/D/SI flags. Synthetic memory-only observations in
ORACLE.md establish truncated b504f3 significand for positive normal powers
of two at2.0 and0.5, and positive-result I/D clearing with sticky/U/O retained.
The independent integer-root implementation now accepts that significand
profile in addition to exact squares. sqrt(5) and negative/extended operands
remain explicit validation boundaries; do not infer universal truncation from
the supported cases. External fpu-sqrt-oracle/verified.json has five agreeing
views,26 cases, hashes and UTC provenance. No emulator implementation used.

## Connected EE timer clock (2026-09-13)

Original startup-global-callbacks capture stops at EE25c04c, reading T1_COUNT
10000800 after25c01c sets mode82 (enabled BUSCLK/256). Virtual time197454482us.
The existing timer model had no clock connected in the system runner. Sony EE
User's Manual v6.0 p36 defines BUSCLK147.456MHz and the three internal divisors;
p20 independently states the same bus frequency. Local source is external
hg-ee-users-manual.pdf. Public documentation:
https://psi-rockin.github.io/ps2tek/#eetimers and
https://ps2dev.github.io/ps2sdk/timer_8h.html . Only register/frequency definitions
were consulted; no emulator implementation was used.

EeTimerClock converts the existing diagnostic microseconds to bus cycles with
an integer fractional remainder. Timer flag rising edges feed INTC9..12;
acknowledging INTC alone does not create another timer edge. Tests cover576
ticks per1000us at /256, wrap, disabled counting, compare/overflow delivery,
flag acknowledgement/rearm, and transactional rejection of unsupported gating.
This preserves the existing1us/slice compatibility clock, not cycle-accurate
EE instruction timing. External/HBLANK and gated counting remain explicit gaps.

## Odd indexed host-to-local transfer stride

The original submitted packet captured in `startup-transfer-provenance.ram.ee-ram.bin`
contains BITBLTBUF0x130b220000000000 at0x4f20f0 and TRXREG720x540 at0x4f2110.
Its IMAGE REF at0x4f2140 names source0x8f0080/QWC0x5eec. The independently decoded
original builder at0x1bb230 stores this layout and derives DBW as width>>6;
the exact dynamic builder invocation has not been watched yet.

Sony GS User's Manual v6.0, printedp101, specifies BITBLTBUF widths1..32 in
64-pixel units; pp162-163 specify8KiB pages,128-pixel indexed page widths and
row-major page arrangement. These pages alone do not establish odd-DBW rounding.
An independently assembled synthetic upload observed as memory-only evidence
settles DBW11's five-page row stride for PSMT8 and PSMT4; see docs/ORACLE.md for
all inputs, observed physical offsets, identities and limitations. Runtime code
uses those observations with the existing independently derived page layout;
no shared rendering material or emulator implementation was consulted.
Further independent DBW1/3/31 observations establish zero/one/fifteen-page
transfer strides and alias ordering; their identities and exact physical bytes
are documented in ORACLE.md and covered by the GIF regressions.
Manual: https://usermanual.wiki/Pdf/GSUsersManual.1012076781.pdf

## Odd indexed texture sampling: observed caller and measured profile

The original queued draw62385 in `startup-indexed-stride.ram.gs.json` has
TEX0=0x2007e006a932e200: PSMT8,TBP0x2200,TBW11,TW10,TH10,CBP0x3f00,
CLD1. It follows the now-supported720x540 indexed upload and reaches the
explicit texture-width guard at EE0x10d744. This identifies the actual next
path; it does not establish how the unsupported texture width should sample.

Sony GS User's Manual v6.0 printedp27 requires128-pixel buffer alignment for
PSMT8/PSMT4; p125 defines TEX0 fields and p162 indexed page dimensions.
The prepared synthetic sampling probe therefore treats odd widths as behavior
requiring measurement rather than claiming the manual guarantees support.
Its framebuffer download follows the GS manual p77 FINISH/CSR/BUSDIR ordering;
the VIF1 FIFO-direction bit23/register address follows the independently
documented hardware register interface at https://psi-rockin.github.io/ps2tek/ .
The former approval blocker is resolved. At2026-09-12T17:18:40Z an independent
one-pixel sprite probe produced all56 expected colors and200 unchanged sentinel
pixels in three stable guest-memory readbacks. PSMT8/PSMT4 at TBW1,2,3,10,11,30,31
use the observed whole-page row stride, including the zero/one-page alias cases.
ORACLE.md records hashes, exact indices, capture provenance and limitations.
The source coordinates and all expected pixels are unchanged from the original
point probe. Sprite coverage follows the original GS manual printedp46; a
separate drawing FINISH wait precedes the documented readback setup sequence.
The native even-width-only preparation check also passes24 samples, but it is
not the independent measurement. Runtime support uses our existing physical
address layout with fixed-marker regressions derived from observed values.
The earlier point probe still missed pixel55 after extra FINISH and reordered
submissions; that discrepancy is preserved as unresolved evidence, not silently
accepted. No emulator rendering source, algorithms or shared configuration
were read or copied. Physical-console behavior is not claimed.

## Prompt-selection sound completion

Own `startup-clut-left-cross.log` reaches EE0x21f280 through original SIFRPC
completion (return0x26fad4) immediately after the diagnostic Left pulse at180m
slices. Our original-ELF decoder verifies `sync; jr ra; ei` at0x21f280..288.
Original0x21fd80/0x21fda0 constructs that callback in a7 and0x21fda4 passes it
to0x2700e8;0x220114/0x220118 supplies it on a second call path. The user-supplied
non-rendering manual function list corroborates the12-byte range. Only this
observed root is added; generated instruction effects come from the original ELF.
No sound completion, menu branch or guest state is synthesized.

## Controller configuration protocol

Next own `startup-controller-config.ram.json` requests41h with nine bytes.
The implementation follows ps2tek's mode-dependent response: zero capability
bytes in digital mode; maskffff03 plus trailing5a in analog mode. psx-spx
instead describes a constant mask and zero final byte, so this distinction
is explicitly provisional and should be checked against a physical/oracle
observation if original protocol negotiation fails. No data was copied from
an emulator. The connected runner still faults on unsupported commands.

Own `startup-controller-states.ram.json` records SIO2 input `[1,67,0,1,0]`
and SEND3=0x140540: enter configuration mode from the initial digital mode.
The virtual pad now retains configuration, analog/lock, axes and motor-map
state. Commands42/43/44/45/46/47/4c/4d follow the documented DualShock2
wire protocol, preserving the old mode in an enter-config reply and the
old map in a rumble-map reply. The controller remains a native device model,
not a native replacement for the original DS2O protocol worker. Pressure,
watchdog reset timing and host input/rumble output remain unfinished.
SIO2 validates equal5/9-byte send/receive profiles and commits controller
state only after a valid transaction. Unsupported commands still fault.
Protocol sources: https://psx-spx.consoledev.net/controllersandmemorycards/
and https://psi-rockin.github.io/ps2tek/ . These are hardware protocol
references; no emulator or shared recompilation implementation was used.

## Display-woken controller state dispatch

Own `startup-display-edges.log` reaches DS2O_S1 at0x2c2c0 after publishing
real timed display edges to WaitVblankStart. Original module base0x2c000
code at+0x294..+0x2b8 loads the controller state byte+66, checks unsigned
<14, then dispatches through relocated table0x2df10..0x2df48. Added only
that bounded table hint; the original code remains the controller protocol
implementation. Searches of supplied non-rendering lists found no entry.

## Configured display timing and EE INTC chains

Original SetGsCrt arguments in `startup-intc-state.log` establish interlaced
NTSC mode2, frame0. The independent timing producer uses27MHz units,
858 units per half-line,525 half-lines per field, and240 active lines;
that yields exactly60 fields per1001ms. TI's TVP5147M1 data sheet documents
525-line NTSC and13.5MHz/858 full-line sampling (equivalently27MHz/1716).
TI's TVP5158 data sheet gives480 active lines per interlaced frame. The
native phase starts with active video on configuration, followed by blank;
this establishes a deterministic startup phase, not a measured hardware
power-on phase. Custom scan registers and other video modes still fault.
Sources: https://www.ti.com/document-viewer/TVP5147M1/datasheet and
https://www.ti.com/lit/pdf/sles243 .

ps2tek's EE BIOS specification defines AddIntcHandler insertion:0 at the
front,-1 at the back, otherwise before the existing ID. The new dispatcher
reconstructs this order from stable registered IDs, retaining removed nodes
when reconstructing historical relative insertions. The original observed
callbacks return0; unverified other returns remain explicit faults. CPU
state and pending kernel-frame depth are preserved around the chain; a
new edge during it remains latched for subsequent service. The diagnostic
uses its existing cooperative interrupt-enable profile rather than claiming
full architectural exception/CP0 interrupt-mask emulation. PS2SDK kernel.h
provides the public one-argument and extended callback prototypes.
https://psi-rockin.github.io/ps2tek/
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/include/kernel.h
No emulator implementation or shared-project rendering material was used.

## Startup two-flag wait

Latest own `startup-intc-state.log` records configured display interlace1,
mode2, frame0; INTC status0/mask0x80c and CP0 status0x70010001. Registered
cause2 handler is0x1beda0; cause3 has both0x1aac30 and0x1bed80. Prior
unconfigured-display fault snapshots preceded this initialization. This
establishes missing edge generation/delivery as the current wait boundary,
while timing and handler-chain semantics still require implementation.

Own `startup-formatter-switch.log` reaches0x1bed00 from object dispatch
0x1696c8 while0x169680 formats original `ST_%03X` at ELF0x44f268.
Original pointer0x46ae0c is slot+0x1c of table0x46adf0, installed at
0x1be7c8/0x1be7d4. The method clears/polls GP-relative bytes-30444 and
-30440. Original0x1beda0 and0x1bed80 set these respective flags. Original
0x1bf198..0x1bf1d4 registers them for INTC causes2 and3. The native
`startup-two-flag-wait.log` records the registrations and then reaches its
30m-slice budget at0x1bed1c. Neither callback has executed. Existing EE
runtime only dispatches SIF0 DMA, and has no display-edge producer.
The supplied manual range for the initial wait target corroborated that
entry only; further display/interrupt investigation uses original inputs
and hardware documentation exclusively. No shared rendering material used.

## EE formatter switch

Own `startup-multi-record.log` advances past DVD transfer and reaches
EE0x26e054. Original0x26e010..0x26e03c adjusts the format character,
checks unsigned index<89, and loads a destination from0x45ae00..0x45af64.
Our ELF reader verifies19 distinct executable targets across the89 entries.
Configured that bounded table as an additive static hint. Exact-address
searches in the supplied non-rendering lists found no match. Runtime
continues to use the actual original format character and target.

## Multi-record DVD DMA

Own `startup-ee-read-complete.log` reaches channel3 descriptor MADR0xc3d88,
BCR0x0056000c, CHCR0x41000200. This is86 blocks of12 words, or two2064-byte
DVD records. Original CDVDMAN0xbaa68..0xbaa98 constructs blocksize12 and
blockcount43 times the transfer record count. The prior native endpoint
accepted one record only and always cleared the entire descriptor afterward.
The extension validates whole records and the remaining command/RAM bounds,
advances MADR and decrements BA per record, keeps STR active between records,
and publishes DMA/device completion at the end of that descriptor. The
successful local-image device IRQ remains the existing native completion
policy; physical drive timing is not modeled. Synthetic tests cover two
independent payloads, no premature IRQ, final completion, and rejection of a
descriptor longer than the command before writes.
DMA block-count/STR rules are independently documented at
https://psx-spx.consoledev.net/dmachannels/
(the sync-mode1 block count decrements to zero while the blocksize remains).
The supplied non-rendering sceCdRead label adds no DMA implementation evidence.

## Archive-sector DVD retry profile

Own `startup-archive-dma-complete.log` advances through IOP completion to
EE callback0x10f0a8, dispatched by original SIFRPC0x26facc with argument
0x3adf80. Own ELF decoding shows leading/trailing byte-fragment copies
through coherent RAM aliases followed by tail transfer0x10ee98. The supplied
non-rendering list labels this `_sceCd_cd_read_intr`; that label corroborates
the observed path. Added an AOT root with no native replacement.

After enabling the observed profile, own `startup-archive-retry0.ram` contains
all2048 bytes of the local DATA.CVM header exactly at IOP0xc3d94. Completion
then reaches0xceb58 from the original CDVD DMA handler, ra0xb82b0. Original
CDVDFSV0xcf0f0/0xcf0f4 and0xcf118/0xcf11c construct that callback and pass
it on the stack to original read calls. Own decoding verifies callback+0xb58
sets event bit0x20 through imported thevent:7 and returns zero. Added this
static root; no matching reference-list entry. Capture is native execution,
not emulator output, and remains external under TEMP/haunting-toc-probe.

Own `startup-cdvd-n-command.log` requests DVD LBA0x164972/count1 with
packet bytes `[114,73,22,0,1,0,0,0,0,2,0]`. This is the independently
verified DATA.CVM extent. Original CDVDMAN0xba9f0/0xba9f8 copies byte0
of the caller's mode structure into packet byte8, while0xbaa0c supplies
converted spindle2 and0xbaa14 writes final0. Existing successful local-image
reads accepted only retry16; now the observed retry0 profile also proceeds
through the same real sector/DMA path. Other profiles remain checked faults;
error retry/default-retry semantics are not implemented. A synthetic test
verifies retry0 LBA, transferred payload and completion state. Supplied
non-rendering lists label sceCdRead but provide no parameter implementation.

## CDVDFSV N-command RPC coverage

Own `startup-backend-extended.log` reaches IOP0xd1c1c with command14
and buffer0xd4288, returning to original SIFCMD0x998bc. Identity-checked
CDVDFSV at base0xce000 constructs handler0xd1c1c in a2 at0xd2508/0xd250c
and registers it at0xd2520 for ID0x80000595. Original handler bounds
unsigned(command-1)<19 at0xd1c8c..0xd1c94 and dispatches via0xd1cac
through relocated table0xd33d0..0xd341c. Added the module+0x3c1c root
and table hint+0x53d0/count19. Exact-address and non-rendering RPC searches
of the supplied reference lists found no applicable entry. Original module
bytes and our relocation/decoder provide all implementation evidence.

## Archive object callback

Own `startup-archive-dispatch.log` reaches0x1e6198 via0x1eb138. The
original wrapper loads backend slot+0x68, confirmed at0x455630; own decoder
verifies its tail jump to0x1d8668. The external non-rendering manual list
also identifies that wrapper. Extended stream dispatch uses slot+0x60;
its original pointer0x1db790 was already configured. Added only0x1e6198.

Own `startup-service-backend.log` reaches0x1e9260 through0x1e70b8,
slot+0x30 of the descriptor installed at0x1e6c2c. Original instructions
at0x1e6c18..0x1e6c2c establish the base as0x4558a0 (the constructor
pointer is at0x4558ac, slot+0xc). Non-null original methods end at+0x60,
followed by zero and strings; rooted the related methods together. Original
0x1e9260 polls archive state under the existing critical-section helpers.
External non-rendering range agrees; no implementation imported.

Own `startup-stream-status.log` reaches0x1ed1e0 via0x1d74ec after the
status switch. Original method tail-calls0x1e70a0 and is slot0 in original
archive service table0x3d5b18. Related non-null entries through+0x48
are rooted together from that table; the external non-rendering range
only corroborates the observed20-byte wrapper.

Own `startup-worker-table.log` then reaches0x1dadcc through the stream
status switch at0x1dad74. Original instructions at0x1dad58 bound the index
to ten entries; original pointer table0x453090..0x4530b8 contains four
distinct branch destinations. Configured that bounded table as an indirect
target hint, preserving the actual guest-selected status branch. The
supplied non-rendering function range for0x1dad28 agrees; no code imported.

Own `startup-backend-table.log` next reaches0x1df1c8 from0x1d74ec.
Our original ELF decoder shows a40-record worker loop, each record56 bytes,
calling0x1df1c0 for active records. Its pointer is slot0 of original table
0x3cacc0; related non-null entries through+0x38 are statically rooted.
The supplied non-rendering list agrees on the entry range, but all pointers
and generated instruction effects derive from the original ELF.

Own `startup-stream-table.log` passes the stream methods and reaches backend
0x1e6168 via indirect call0x1eb078, which loads slot+0x30. Original ELF
table0x4555c8 contains that pointer at0x4555f8. Own decoder verifies the
method tail-transfers to0x1d78b0; the supplied non-rendering analysis range
agrees on its20-byte entry. Remaining non-null table entries through+0x38
are rooted as a batch from original pointers, with existing0x1e6078 retained.

Own connected `startup-archive-found.log` reaches0x1e8580 from the object
slot+0x18 dispatch at0x1e6d48. Original ELF decoding verifies the entry,
argument checks, and tail transfer at0x1e8648 to0x1e8650. The supplied
non-rendering analysis list agrees on the entry range; no implementation
was imported. An explicit root lets our decoder follow its reachable code.
The next native stop0x1eae68 at0x1e7dc4 identifies slot2 of the original
15-pointer wrapper table0x455c38 (followed by zero). Related wrapper entries
are included together from the original ELF. The resulting wrapper dispatch
at0x1eae88 reaches backend0x1e6078, also found in original table0x4555c8
at+0x18. That observed backend entry is separately rooted; table contents
are evidence of static pointers, not a claim that every method has executed.
Next observed stream dispatch0x1d7360 reaches0x1db268. Its original code
updates a bounded stream position for origins0/1/2, and its pointer occurs
at0x3c50b0 in table0x3c5098. Non-null original callbacks through table+0x40
are rooted together, excluding already configured roots. This is independent
ELF discovery; the external analysis range was only corroboration.

## CDVD DMA interrupt delivery

Follow-up: own `startup-dvd-header-check.ram.json` showed the callback reading
synthetic TOC header0x29 before DVD DMA. `startup-read-error-write.log` identifies
original0xba268 setting error0x20 from that callback's header validation flags.
Completion was incorrectly latched while channel35 was disabled, then delivered
when a new DVD operation enabled it. Native channel-enable gating now prevents
that stale completion; global CPU interrupt disabling still retains pending
enabled-channel completions. DICR per-channel enable gates completion flags:
https://psx-spx.consoledev.net/dmachannels/#1f8010f4h-dicr-dma-interrupt-register-rw.
This is the native CDVD route's mapping; complete raw IOP DICR emulation remains
outside this change. The decoder-status zero was not the cause of error0x20.

After this correction, own `startup-dvd-gated-irq.ram` has a DATA.CVM search
record at0xd4698 (LBA0x164972, size0x5e018000) and error0 at0xccfd1.
EE next reaches original callback0x1ed1d0 via0x1d6a70. Own ELF decoding
confirms its three instructions return0x3d5b80. The supplied non-rendering
manual list contains the same range; our explicit root follows the observed
call and original bytes, without importing code or rendering material.

Identity-checked original CDVDMAN at0xb70b0..0xb70c4 registers interrupt35,
mode1, callback0xb8270. The read setup at0xb8488/0xb848c enables interrupt35
when the descriptor requests completion handling. The native sector copy
previously cleared DMA busy but never published channel3 completion, and
the cooperative dispatcher omitted the35 route. Successful DMA completion
now marks channel3 pending after copying the bytes; the existing dispatcher
delivers the registered handler when enabled, before the CDVD device IRQ in
the connected scheduler. No emulator implementation or shared rendering
material was used.

Own `startup-dvd-irq.ram.json` then reaches indirect DVD callback0xb98cc
from the DMA handler at0xb82a8. Its pointer also appears in the original
retained DMA descriptor at0x179458 in `startup-dvd-profile.ram`. Our decoder
confirms the original prologue at module+0x78cc; this is now an explicit
static translation root. The supplied non-rendering lists have no match.

## Eleven-byte DVD read command

Own native capture `startup-dvd-builder.ram.json` follows the original caller
at0xbaab4 into0xb8524 with command8, a packet pointer, and length11.
Identity-checked relocated CDVDMAN code at0xba9e8/0xba9ec writes LBA/count;
0xba9f0/0xba9f8 copies mode byte0 as retry count;0xba9fc..0xbaa0c
converts mode byte1 through0xb91c8 and stores the spindle selection;
0xbaa14 sets the final byte to zero. Original diagnostic format at0xbf3ac
names `spin` and `trycnt`, confirming their roles. The observed packet has
retry16, spindle2, final0. The runtime accepts only this successful local-image
profile, retaining explicit faults for other modes and incomplete packets.
Retry/error recovery and physical spindle timing are not implemented.
The same original builder at0xbaa68..0xbaa98 constructs block size12 and
block count43 for a one-sector read. Its retained descriptor in own capture
`startup-dvd-profile.ram` at0x179450 is0x002b000c, representing516 words.
ReadDvd DMA validation now checks total words rather than requiring the
alternative129-block/four-word encoding. It still requires the supported
control register value, alignment, RAM bounds, and exactly one DVD record.
No shared-project implementation was used. The supplied reference label
sceCdRead at EE0x110380 did not establish this IOP protocol.

## Native IOP event status

The identity-checked ROM THREADMAN module, relocated by our own tooling,
contains status wrappers at +0x36dc and +0x3774. Its copier at +0x36a0
establishes the five-word output: attributes, option, initial bits, current
bits, waiter count. Native handlers use this ABI with the existing native
event IDs. Waiters remain counted until the cooperative wait resumes.
Own startup capture `startup-toc-completion-state.ram.json` showed the
original encoded-handle validator rejecting native ID4 with -409.
Targeted searches of the supplied non-rendering lists found no event-status
lead; no external implementation was used.

## CDVD successful-completion result

Own native captures `startup-toc-write-trace.log` and decoding of the original
CDVDMAN establish a descriptor byte written to0x1f402006 at0xb84a4, followed
by GetToc and the IRQ reading that byte as an error. The runtime previously
retained it even after validated successful DMA completion. Completion now
publishes error0 before raising the IRQ, for both TOC and DVD reads. This is
the native success-result contract; exact hardware reset timing and the
write-side semantics of0x1f402006 remain unverified. The ps2tek register table
labels the error port read-only, insufficient to justify its existing write
alias: <https://psi-rockin.github.io/ps2tek/#cdvdioports>. No emulator code was
used; search results containing emulator sources were not used as evidence.

## Local input

- The user supplied a separate `HG/config` set containing Ghidra-derived
  function ranges and ps2recomp-oriented configuration as low-confidence
  reference material. It is not an implementation or discovery dependency:
  no translated code, rendering behavior, runtime binding, function boundary,
  or patch is imported from it. At most, it may be consulted after this native
  diagnostic has already observed an exact target, followed by independent
  verification against the original ELF bytes. The files remain outside this
  repository.
- `Haunting Ground (USA)/SYSTEM.CNF` identifies `SLUS_210.75` as BOOT2, VER 1.01,
  NTSC. Treat these as disc metadata, not independently verified release history.
- Executable SHA-256:
  `3b374d53a499d2c17b205274ee9eb34280768f294f970ebf6ae6731f6a2dacb8`.
- All instruction observations so far come from that executable's bytes through
  this project's own ELF reader. PCSX2 memory observations validate selected ABI results; see docs/ORACLE.md.
  No PCSX2 implementation was used.

## Hardware and host specifications consulted

- Sony **SPU2 Overview Manual, version 6.0**, archived at
  <https://github.com/ninjadynamics/PS2Docs/blob/main/SPU2_Overview_Manual.pdf>,
  p12 and p55: the host can transfer data to SPU2 local memory through DMA;
  TSAH/TSAL select its source/destination address, the low three TSA bits must
  be zero, and changing TSA during a transfer makes the result indeterminate.
  The native endpoint implements only the exact bounded IOP-to-SPU descriptor
  reached by the supplied game and retains bytes without claiming synthesis.
- SPU2 AutoDMA boundary (2026-09-08): the same Sony manual, pp10 and 28,
  defines 48 kHz processing and per-channel 512-short-word sound-input areas,
  each split into 256-short-word halves. Page 55's TSA transfer list excludes
  AutoDMA write; it must not be implemented as an ordinary copy to TSA.
  Independently decoded supplied LIBSD at relocated `0x0003208c..0x000320c8`
  clears TSA and writes `1 << core` to `0xbf9001b0 + core*0x400` before DMA.
  Its interrupt path at `0x00031dcc..0x00031df0` reloads BCR/MADR/CHCR, then
  calls the registered CRI callback at `0x00031e4c`. Native external trace
  `%TEMP%/haunting-toc-probe/resume-worker.ram.json` records that callback
  returning immediately before CRI worker 19 consumes semaphore 7 again.
  A nonzero per-core AutoDMA control now faults before copying or completing
  DMA. Sound-input pacing, buffer routing, and consumption remain unimplemented.
  Search results included SDK implementation and emulator-related snippets;
  none were used. This boundary derives from the supplied module and Sony manual.
- Paced input implementation (2026-09-08): independent decoding of supplied
  CRI_ADXI `0x00014744..0x000147d4` proves 512-byte blocks assigned alternately
  to the two channel queues; `0x00014850..0x00014864` returns the queue selected
  by channel index. The new input transport maps alternating channel blocks to
  Sony's documented MEMIN(L/R) areas (short-word addresses `0x2000/0x2200` and
  `0x2400/0x2600`). Channel-index 0=L and 1=R is an inference from this layout,
  pending a nonzero original-hardware observation. No SDK implementation is used.
  The endpoint consumes one sample pair per 1/48000 second, makes a consumed
  256-sample half available, and fills it from a bounded pending DMA. Completion
  follows transfer of every requested byte, not an arbitrary timeout.
  Initially priming both empty halves and starting phase zero at the first fill
  is an explicit native startup policy, not a measured hardware phase. Raw input
  is exposed to a callback and bounded, explicitly truncated diagnostic history;
  this is not mixed audio output. Underrun, unsupported control values, malformed
  descriptors, and active-register edits fault. Sony pp13-14 establish short-word
  addressing; pp10/28 establish the clock and buffer shape.
- Streamed-audio synchronization verification (2026-09-15): original USA EE code
  independently identifies the two queue terms used by the movie clock. Object
  table `0x003cff70` slot 8 enters `0x1e34a0 -> 0x1e34f0`; the helper copies the
  submitted descriptor bytes into its ring and `0x1e35f4` adds the descriptor
  length to `0x003d0128+0x0c`. `0x1e3464` subtracts consumed bytes, while the
  selector-1 getter `0x1e3268` returns this byte count and `0x1d3c58` divides it
  by two. Callback A reaches `0x1e3f20/0x1e3f68` and sums `+0x0c` byte lengths
  over the list rooted at `0x003d4040+0x18`; live queue methods `0x1e4318` and
  `0x1e41b0` update the node descriptor containing that length. The corrected
  live node in the focused capture is `0x003ccc60`, contribution word
  `0x003ccc6c`.
  HG-DIAG-013 external capture `spu2-sync-dma-19p4m.csv.events.csv` correlates
  those original writes with the native SPU2 endpoint: each freed 256-frame half
  accepts 1024 bytes and each observed 2048-byte descriptor completes after two
  halves/512 frames. The longer original-movie sync capture measures native core-0
  consumption at 47,999.59 and 47,999.89 frames/s in two sustained segments,
  without growing guest-clock drift. First nonzero raw frame 924676 maps to
  ~19.355198s guest time; the next original movie-sync sample reports position
  1024 at 19.358531s. No runtime clock or queue value was substituted. This
  validates the implemented streaming timing for the observed game path, while
  synthetic TOC/issue-slot diagnostics and lack of physical-console timing
  measurement remain explicit limitations.
- Sony **EE Core User's Manual, version 6.0**, archived at
  <https://github.com/ninjadynamics/PS2Docs/blob/main/EE_Core_Users_Manual.pdf>,
  pp156,158–159,162–165: floating-point formats, FCR31 masks, signed zero,
  overflow saturation, underflow to signed zero, and round-toward-zero without
  guard/round/sticky bits. Add, multiply, MADD and MSUB use integer mantissa
  arithmetic so discarded bits are chopped without host floating-point.
- Sony **EE Core Instruction Set Manual, version 6.0**, MADD.S pp359–360 and
  MUL.S p372, supplies the accumulator/product operation shapes used by that
  independent integer implementation.
- DIV.S continuation: original instruction manual p357 and core manual
  pp156,158–165 specify signed-zero operands, finite exponent255, saturation,
  exception flags and truncation. `Fpu::divide` independently computes the
  normalized integer quotient and discards the remainder; it uses no host FP
  or emulator implementation. Synthetic tests cover sign, both normalization
  branches, 1/3 versus round-to-nearest, zeros, exponent255, range limits and
  sticky flags. The core manual warns that hardware can differ from IEEE
  truncation in the last bit; no divider microalgorithm or oracle comparison
  establishes those deviations yet. This is a specification-based implementation,
  not a claim of hardware-bit-exact division. Manuals were read from external
  `hg-ee-core-instruction-manual.pdf` and `hg-ee-core-users-manual.pdf`; core
  text also checked at <https://docs.alexrp.com/mips/ee.pdf>.
- The EE Core User's Manual DMAC chapters (pp62 and 71–72) define the global
  hold state and `D_ENABLER`/`D_ENABLEW`; active channel registers remain
  editable while held and resume only after the documented release.
- The same manual's IPU register/command chapters (pp175–176, 182–188,
  195–196, 199) define reset, BCLR, SETIQ, SETVQ and SETTH. The runtime retains
  only their input FIFO, tables, thresholds and command completion; MPEG decode,
  output and rendering are still explicit unsupported boundaries.
- EE instruction manual p121: SYNC ordering; p287: SQ masks low four address bits;
  pp344/354: ADDA.S and CTC1. SYNC has a host ordering fence because implemented
  guest operations are synchronous; asynchronous devices will require more work.
- NIST FIPS 180-4: <https://csrc.nist.gov/pubs/fips/180-4/upd1/final>.
  Independent SHA-256 implementation checked with empty, abc and million-a vectors.
- PS2SDK public ABI declarations and call-number definitions (not implementations):
  <https://ps2dev.github.io/ps2sdk/kernel_8h.html> and
  <https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/syscallnr.h>.
  These identify 0x3c/0x3d as SetupThread/SetupHeap and their parameter interfaces.
- PS2SDK's public EE DECI2 declaration identifies operation `0x10` as `kputs`
  and its first argument as a string pointer:
  <https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/include/deci2.h>.
  The runtime uses only that public ABI shape for a bounded diagnostic sink;
  its return value is an explicit native compatibility policy pending an
  original-hardware observation, and no emulator implementation is used.
- PS2SDK's public EE kernel declarations and thread-status constants identify
  the `SuspendThread` interface and dormant thread state:
  <https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/include/kernel.h>.
  The supplied game probes a dormant thread during startup; returning `-1`
  without mutating it is documented as a native compatibility choice rather
  than claimed as hardware-verified behavior.
- PS2SDK's public IOP INTRMAN ABI declarations at
  <https://ps2dev.github.io/ps2sdk/intrman_8h.html> identify ordinal 8 as
  `CpuDisableIntr()`: it changes CPU interrupt delivery, not individual cause
  masks, and returns a status. The native adapter derives only that contract;
  it does not reuse any INTRMAN implementation.

- Sony **EE Core Instruction Set Manual, version 6.0**, archived at
  <https://github.com/ninjadynamics/PS2Docs/blob/main/EE_Core_Instruction_Set_Manual.pdf>.
  Pages 146–147 (MFHI1/MFLO1), 172 (PADDUW), 284 (PSUBW), 285 (PXOR), 152–153 (MTSAB/MTSAH) distinguish packed
  saturating addition from XOR and define shift-count behavior. The downloaded
  reference is outside the repository in the OS temporary directory. No emulator
  implementation or the archive's unrelated implementation handbooks were used.
- Sony **VU User's Manual, version 6.0**, pp285–286, independently consulted at
  <https://studylib.net/doc/25815876/vuusersmanual.158394566>. This specifies
  macro-mode VMOVE's component mask and VMR32's y/z/w/x source-field mapping.
  The implementation is a direct raw-bit field transfer; it uses no emulator or
  host floating-point behavior.
- The same manual, pp240 and 66, defines macro-mode `VABS` as its upper-pipeline
  ABS counterpart over selected VF x/y/z/w fields. The AOT implementation
  clears only the selected IEEE-754 sign bits as a raw-bit operation, preserving
  unselected fields and NaN payloads without using host floating-point behavior.
- The VU microinstruction layout and lower-pipeline branch encodings were
  consulted from `vu-instruction-manual.pdf` in the Sony-manual archive at
  <https://github.com/ninjadynamics/PS2Docs/blob/main/vu-instruction-manual.pdf>.
  The build-time pair decoder retains only the documented 64-bit pairing,
  immediate/end flags, and lower control-flow fields; it does not execute VU
  instructions or use emulator code.
- Sony **VU User's Manual, version 6.0**, §3.4.8 (p50) specifies a one-pair
  branch delay slot and defines branch targets relative to that slot. The VU
  structural report records that slot and flags it when a supplied capture ends
  before it, instead of treating a partial capture as a valid CFG.
- Sony **EE Core Instruction Set Manual, version 6.0**, BREAK instruction
  entry: BREAK is a synchronous guest exception. The AOT emitter preserves it
  as an explicit fault with the encoded trap code; it is never treated as an
  unsupported opcode or a no-op. The analyzer knows a branch slot containing
  BREAK cannot reach the branch target (except the documented annulled
  not-taken path of a branch-likely instruction).
- GLFW official getting-started documentation:
  <https://www.glfw.org/docs/latest/quick.html> (context/window lifecycle).
- Khronos OpenGL reference pages:
  <https://registry.khronos.org/OpenGL-Refpages/> (rendering API reference).
- PS2TEK GIF reference, sections "GIFtags" and "GIF Data Formats":
  <https://psi-rockin.github.io/ps2tek/index.html>. This documents GIFtag bit
  fields, little-endian descriptor order, PACKED quadword payloads, REGLIST
  doubleword payloads and odd-entry padding. The initial decoder only preserves
  those structural transfers and explicitly rejects IMAGE until a checked GS
  HWREG/VRAM endpoint exists.
- Sony **GS User's Manual, version 6.0**, pp79,162–169, archived at
  <https://github.com/ninjadynamics/PS2Docs/blob/main/GS_Users_Manual.pdf>:
  GS local memory is 4 MiB; PSMCT32 has 64x32-pixel pages, 8x8-pixel blocks,
  8x2-pixel columns, and the illustrated page/block/column word arrangement.
  The initial image-transfer endpoint independently implements only that
  documented PSMCT32, PSMCT24, PSMCT16, PSMT8, and PSMT4 layouts and p79's
  transfer packing. The corresponding PSMZ32/24/16/16S layouts now accept the
  same documented 32/24/16-bit host packing through their distinct Z swizzles;
  unsupported formats continue to fault.
- GS User's Manual pp74–77 and pp132–134 defines BITBLTBUF source/destination
  fields, TRXPOS's four traversal directions, TRXREG dimensions, and synchronous
  local-to-local activation through TRXDIR.XDIR=2. Same-depth copies now convert
  between every independently implemented CT/Z/indexed layout in the selected
  traversal order so overlapping rectangles retain GS ordering, validate the
  documented width and destination-X restrictions, and treat XDIR=3 as transfer
  deactivation. Local-to-host FIFO reads stay an explicit boundary.
- GS User's Manual p76 requires transfer widths of 2/8/4/8 pixels for
  32/24/16/indexed storage respectively, with PSMT8/8H destination X aligned to
  two pixels and PSMT4/4HL/4HH to four. Both host-to-local and local-to-local
  setup validate those constraints and BITBLTBUF's 1..32 width units before
  modifying local memory.
- EE User's Manual pp26 and 149 maps GS privileged registers into
  `0x12000000..0x13ffffff`, requires LD/SD, and defines address bits 12 and 9:4
  as the mirrored GS register selector. GS User's Manual pp94–95, 122, 144–146,
  154, and 156 defines SIGNAL/FINISH/LABEL events and masked IDs, CSR event
  clearing/FIFO state, IMR masks, BUSDIR, and SIGLBLID. The native bridge models
  that checked subset and latches an unmasked GS event into EE INTC cause 0;
  CSR reset/flush and the second-SIGNAL drawing stall remain explicit.
- GS User's Manual pp85–88 and pp147–150 defines the PCRTC rectangular read
  circuits: DISPFB selects swizzled buffer base/width/format and DBX/DBY, while
  DISPLAY supplies screen placement, magnification, and output dimensions.
  The native single-circuit extractor reverses the magnification to a bounded
  source rectangle and expands PSMCT32/24/16/16S alpha as documented. The
  two-circuit PMODE merge and PS-GPU24 are not inferred.
- GS User's Manual pp125–126 defines TEX0's base, buffer-width, PSM,
  power-of-two dimensions, CLUT base/storage format/mode, and the CSM2
  `CSA=0` constraint; p130 defines TEXCLUT's CBW/COU/COV units. The
  renderer-facing extraction uses these with pp30–31's CSM1 IDTEX8/IDTEX4
  palette arrangements and p29/p72's direct texel and RGBA16 rules for checked
  PSMCT32, PSMCT24, PSMCT16, plus indexed textures through CSM1 and CSM2
  16-bit (CSA=0) CLUTs. The load/cache behavior is specified separately below.
- GS User's Manual **pp55–57, 125–126 and 128** (sections 3.4.7 and the
  TEX0/TEX2 register reference), also available as a transcription at
  <https://www.scribd.com/document/784545197/GS-Users-Manual>, defines the shared
  1 KiB CLUT temporary buffer. CSA selects the destination entry `16*CSA`,
  not a source VRAM offset. Source CSM1 palettes begin at CBP's upper-left:
  pp30–31 give the IDTEX4 8x2 and IDTEX8 16x16 arrangements. The p56 layout
  pairs halfword entries n and n+256 for PSMCT32; PSMCT16/16S use individual
  halfwords. TEX0/TEX2 access performs CLD 1–5 loads, preserves other banks,
  and tracks independent CBP0/CBP1 comparison registers. CLD 0 retains cached
  entries even if VRAM/CBP later changes. TEXA expansion remains at sample time.
  The native implementation flushes pending draws before reading or replacing
  a palette, so old draws keep their old cache and may supply the new VRAM
  palette. Synthetic tests cover bank 9/15/31, all four-bit index layouts,
  cross-context TEX2 reuse, 16/32-bit plane sharing, eight-to-four-bit reuse,
  both conditional registers and queued-draw ordering. Uninitialized cache
  reads/comparison registers, reserved CLD, CSM2 conditional loads, and loads
  crossing the bounded cache/source row remain explicit boundaries; no reset
  contents or wraparound behavior is guessed. Existing CSM2 16S handling is
  retained; no additional pixel formats are inferred.
- GS User's Manual p128 defines TEX2_1/2 as context-specific subsets of
  TEX0 containing PSM, CBP, CPSM, CSM, CSA, and CLD. A TEX2 write now merges
  exactly those fields into its corresponding TEX0 state while preserving
  TBP0, TBW, TW, TH, TCC, and TFX; texture extraction therefore observes later
  TEX2 format/CLUT changes instead of silently using stale TEX0 fields.
- GS User's Manual pp117–118 defines PRMODE's attribute bits and
  PRMODECONT.AC: PRIM always supplies primitive type, while AC selects PRIM or
  PRMODE for IIP/TME/FGE/ABE/AA1/FST/CTXT/FIX. Draw records now resolve that
  selection at the drawing kick and use the selected context's XYOFFSET.
- GS User's Manual pp115–116 describes the drawing environment registers and
  permits environment changes between drawing kicks (including within connected
  primitives). Each queued draw therefore captures the complete register file at
  its kick; deferred texture, fog, tests, blending, frame, and depth work cannot
  observe later register writes. Because drawing and transfers share local
  memory, a valid host-to-local TRXDIR commits earlier queued draws before IMAGE
  payload writes, preserving their command order for frame/depth/texture/CLUT
  aliases.
- GS User's Manual pp37–38 fixes Point and Sprite shading to flat and
  antialiasing to off, independent of their attribute bits; p43 says flat color
  is the value set immediately before the drawing kick. Sprite rasterization
  consequently ignores IIP/AA1 and uses the second (drawing-kick) vertex color.
- GS User's Manual pp54–55, section 3.4.6, defines RGB5-to-RGB8 expansion and
  TEXA's TA0/TA1/AEM alpha conversion for RGB24 and RGBA16 (including RGBA16
  values fetched through a CLUT).  TEX0 extraction applies those rules before
  exposing portable host RGBA pixels.
- GS User's Manual pp52–54 and p102 defines point-sampling coordinates and
  CLAMP's REPEAT, CLAMP, REGION_CLAMP, and REGION_REPEAT fields. The portable
  point sampler applies those modes before indexing the checked TEX0 image.
- GS User's Manual p135 defines UV as unsigned 10.4 texel coordinates and
  specifies that PRIM.FST selects them. Presentation records preserve both
  fixed UV values and the FST selection for backend texture submission.
  For the perspective path, p134's ST floating-point pair and RGBAQ.Q are now
  retained together on every kicked vertex; perspective division itself stays
  explicit until its interpolation/rounding behavior is implemented.
- GS User's Manual p59 defines TEX0 TFX/TCC texture-function modes and its
  0x80-unity `A*B=(A x B)>>7` multiplication. The portable texture-function
  helper implements MODULATE, DECAL, HIGHLIGHT, and HIGHLIGHT2 before host
  submission is introduced.
- GS User's Manual pp69–70 and p100 define ALPHA's `(A-B)*C>>7+D` RGB
  calculation, its A/B/C/D selectors, and later frame-buffer clamping. The
  portable blend stage preserves unclamped RGB results for the later writer.
  Page 115 defines PABE's source-alpha-MSB per-pixel blend gate, applied by
  the common frame path before selecting the blend calculation.
- GS User's Manual pp66–68 and p124 define TEST's alpha, destination-alpha,
  and depth gates. Portable predicates reproduce those comparisons before
  blending/frame-buffer work; prohibited ZTE=0 rejects explicitly.
- GS User's Manual pp70–72 and p110 define PSMCT32 frame-buffer writing,
  PSMCT24's RGB-only packing, COLCLAMP, FBA, and FBMSK. The portable PSMCT32
  and PSMCT24 writers apply those stages to GS local memory; RGB24 preserves
  its unused stored byte and uses its documented 0x80 destination alpha.
  PSMCT16 additionally has a checked non-dithered/unmasked RGB5A1 path with
  documented alpha-bit correction and 0x80/zero destination-alpha expansion.
  Pages 70 and 104–105 specify DIMX's signed offsets and DTHE's coordinate
  selection; the portable writer applies them before clamp/RGB5 conversion.
  Page 72 maps PSMCT16 FBMSK's pre-conversion R[7:3], G[15:11], B[23:19], and
  A[31] bits to RGB5A1; the writer follows that map.
- GS User's Manual pp165-166 specifies PSMCT16S's separate 8×4 block table
  with the same 16×2 column/lane arrangement. The frame writer, destination
  alpha, normal depth draw, and TEX0 direct-color extraction each use that
  layout rather than aliasing PSMCT16.
- GS User's Manual pp29 and 164 specifies PSMT8H, PSMT4HL, and PSMT4HH as
  CT32-addressed indexed texels in bits 31:24, 27:24, and 31:28,
  respectively. TEX0 extraction routes those lanes through the existing
  checked CSM1/CSM2 CLUT conversion. Page 79's unpadded IDTEX8/IDTEX4 IMAGE
  payload packing is used for host-to-local uploads: IDTEX8 bytes write the
  high byte and IDTEX4's low-first nibbles write the indicated high nibble
  lane, preserving all other CT32 bits.
- GS User's Manual p125 permits PSMCT16S as TEX0.CPSM. Both CSM1 and CSM2
  indexed conversion paths select PSMCT16S's documented storage layout when
  CPSM is 10, retaining the existing palette-index rules.
- GS User's Manual pp141 and 162–165 defines ZBUF, ZMSK, and the distinct
  PSMZ32/PSMZ24 page/block arrangement. The portable Z32/Z24 paths use that
  layout with the frame buffer's documented width; Z24 preserves the unused
  upper local-memory byte. PSMZ16 uses p165's separate block order with the
  documented 16-bit column lanes; PSMZ16S uses its separate p165 block table.
- GS User's Manual pp24, 44, and 121 defines pixel centers, the point
  closest-pixel rule, and inclusive SCISSOR bounds. The PSMCT32/PSMZ32 point
  rasterizer follows that path. Textured points support either FST/UV or the
  single vertex's normalized S/Q and T/Q coordinates; the STQ conversion uses
  exact IEEE-single mantissa/exponent ratios rather than host floating-point.
- GS User's Manual pp36 and 44 defines independent Line and connected
  LineStrip primitives, their per-pixel diamond coverage, and the included
  start/excluded endpoint rule. The portable line path implements that exact
  coverage geometry with rational interval comparisons rather than host
  floating point. Lines use their dominant-axis DDA position for Gouraud
  RGBA, depth, fog, and FST UV while diamond traversal independently selects
  covered pixels; horizontal, vertical, and diagonal cases share the checked
  pixel/texture pipeline. FST-clear lines use signed dominant-axis weights to
  form exact weighted S, T, and Q sums, including diamond samples outside the
  endpoint projection; the common interpolation denominator cancels in the
  perspective division. Antialiasing remains explicit.
- GS User's Manual Supplement v6.0 pp17–18, archived at
  <https://github.com/ninjadynamics/PS2Docs/blob/main/GS_Users_Manual_Supplement.pdf>,
  specifies AA1 Line/LineStrip as a one-pixel widening perpendicular to the
  dominant axis, with coverage converted from 0..1 to alpha 0..0x80, forced
  antialias blending, no Z-buffer write, and no special shared-endpoint handling
  for strips. The current non-AA rational diamond path does not approximate
  these effects; AA1 remains an explicit fault until its widened coverage and
  blend rules are implemented together.
- GS User's Manual p120 defines SCANMSK as a primitive-only framebuffer-row
  parity mask. The common primitive pixel path rejects its prohibited even or
  odd rows before pixel tests or framebuffer/depth writes.
- GS User's Manual pp66-68 defines pixel-test order and alpha-test failure
  controls. Primitive pixels now carry failed alpha tests through destination-
  alpha and depth tests, then apply KEEP, FB_ONLY, ZB_ONLY, or RGBA32-only
  RGB_ONLY at output selection; non-RGBA32 RGB_ONLY follows FB_ONLY.
- GS User's Manual p64 defines fogging as independent unsigned `>>8` blends
  between texture-function RGB and FOGCOL, retaining source alpha. The checked
  primitive paths apply it before pixel tests, with line and triangle fog
  interpolated by their existing coverage weights.
- GS User's Manual p46 defines sprites as rectangles with top/left-inclusive
  and bottom/right-exclusive coverage. The portable sprite path implements the
  flat subset with that coverage rule. Per p43, its second vertex supplies Z
  (the first is ignored). For FST sprites, p135's unsigned 10.4 UV endpoints
  are interpolated independently over the two rectangle axes before the checked
  TEX0 point sampler. FST-clear sprites interpolate S, T, and Q on those same
  axes, then resolve each normalized coordinate from exact weighted integer
  ratios; this avoids host floating-point and preserves negative-floor behavior.
  Varying Gouraud color remains explicit.
- GS User's Manual pp44–46 defines triangle DDA coverage: pixel centers on top
  or left edges draw, while bottom/right-edge centers do not. The portable
  triangle path normalizes winding before applying that rule. FST triangles barycentrically
  interpolate p135's unsigned 10.4 UV coordinates at each covered pixel before
  using the checked TEX0 sampler; winding normalization swaps vertex attributes
  with geometry. The same integer edge weights interpolate p43's Gouraud RGBA
  and p64's fog value, keeping every varying attribute on one coverage sample.
  The same weights interpolate unsigned 32-bit Z exactly in 64-bit arithmetic;
  the 16-bit coordinate domain bounds the weighted sum below `2^64`. Page 43
  specifies flat color as the RGBAQ value immediately before the drawing kick;
  FST-clear triangles use the same barycentric weights to form exact weighted
  S, T, and Q sums, then divide those sums for perspective-correct texels.
- GS User's Manual p136 defines context-specific XYOFFSET's 12.4 OFX/OFY
  fields as the primitive-to-window coordinate conversion offset. Completed
  draw records retain the active context's raw XYOFFSET, and presentation
  subtracts it in signed 12.4 units before handing coordinates to the host.
- Sony **EE User's Manual, version 6.0**, pp86–87 and 121–122, archived at
  <https://github.com/ninjadynamics/PS2Docs/blob/main/EE_Users_Manual.pdf>:
  VIF packets are 32-bit VIFcodes in DMA data; VIF1 DIRECT/DIRECTHL forward
  the following `IMMEDIATE` 128-bit units to GIF PATH2 (`IMMEDIATE=0` means
  65536). The bounded VIF1 transport implements only those commands plus the
  listed state-only VIFcodes. The bounded VIF1 path also transports MPG's
  documented 64-bit instruction units into checked VU1 MicroMem without
  executing them; activation remains explicit.
- The same manual, pp89–96 and 123, specifies UNPACK's V4-32 payload length,
  VU1 vector destination, and the separate cycle/mask/TOPS behaviors. The
  bounded VIF path writes S-32/S-16/S-8 and V4-32/V4-16/V4-8/V4-5 vectors with
  documented scalar replication, signed/unsigned, or RGBA5 expansion to checked
  VU1 data memory only. V2/V3 are accepted only when every documented
  indeterminate component is explicitly masked, filled, or preserved; otherwise
  they and VU execution remain explicit.
- Page 97 specifies normal, offset, and difference additive decompression.
  VIF input-selected lanes add Row fields in offset/difference mode and update
  Row only for difference mode; Row/Col/preserved masked lanes do not consume
  input or apply addition.
- Page 99 specifies that OFFSET initializes VIF1_TOPS from BASE and that
  FLG-relative UNPACK destinations add TOPS. The VIF path models that initial
  destination state; MSCAL-driven double-buffer switching remains explicit.
- The same manual, pp118–119, specifies STROW/STCOL as four following words
  stored in the VIF Row/Col registers. The VIF path preserves those split DMA
  payloads for the later documented masked/fill UNPACK behavior.
- Page 117 specifies STMASK as one following word. The VIF path retains its
  pattern separately. For V4 UNPACK, the VIF path applies its documented
  input/Row/Col/preserve selectors, including `CL < WL` fill cycles that
  consume only their documented input vectors; selector-0 fill slots fault
  because no input exists to supply them.
- Pages 88 and 120 specify MPG's 64-bit payload alignment and `NUM` following 64-bit microinstructions
  (`NUM=0` means 256) and its destination in eight-byte units. The VIF1 path
  preserves those words in bounded VU1 MicroMem across split DMA input; it does
  not infer or invoke VU execution.
- The same EE User's Manual, pp150–160, specifies GIF packet alignment,
  descriptor packing, packed XYZ ADC routing, and the distinct PACKED/REGLIST
  rules. In particular PRE applies to PACKED only, and REGLIST A+D is a NOP;
  these are covered by synthetic GIF transport tests.
- EE User's Manual p153–154 additionally specifies PACKED ST's internal Q and
  RGBAQ consumption: Q initializes to 1.0 whenever a GIFtag is read, ST updates
  it for later packed RGBAQ records in that tag. The GS transport stores this as
  explicit GIF state rather than reusing incidental host register contents.
- Sony **GS User's Manual, version 6.0**, pp38–42, 116, and 137–139: PRIM
  initializes the vertex queue; XYZ2/XYZF2 perform a vertex and drawing kick,
  while XYZ3/XYZF3 only advance the queue. Primitive topology and raw vertex
  ordering are retained in a bounded host-side queue; no rendering semantics
  beyond those documented queue events are claimed.
- GLFW 3.4 source dependency (window/context library, not PS2 implementation):
  <https://github.com/glfw/glfw/tree/7b6aead9fb88b3623e3b3725ebb42670cbe4c579>.
  Its upstream license stays with the dependency in the ignored build tree.

## Oracle observations and further work

For each needed PCSX2 memory observation record the executable hash, emulator
version, game state/reproduction steps, EE address range, capture hash, and the
hypothesis being tested. Keep captures outside version control. Summarize findings
in your own words and implement them independently; do not transcribe emulator
code, algorithms or implementation details. Compare against synthetic tests and
manuals, since emulator behavior alone is not proof of hardware semantics.

## Additional implemented specifications
EE Core Instruction Set Manual: DIV/DIVU pp51/53, MULT variants pp154-157,
MADD variants pp142-145, MFHI1/MFLO1 pp146-147, PMFHL.LW p228, PSUBB p270, PSUBW p284, PCPYH/LD/UD pp189-191,
SUB p114, PADDSB p162, PADDSH p164, PADDSW p166, PADDUB p168, PADDUH p170, PSUBH p271, PSUBSB p272, PSUBSH p274, PSUBSW p276, C.EQ.S p349, C.OLT.S p354, CVT.W.S p356, DIV.S p357, MADD.S p359, MADDA.S p360, MSUB.S p367, NEG.S p370, MUL.S p372, SQRT.S p376, SUB.S p377,
EI/DI pp314-315, ERET p316. COP0 register map/ErrorEPC: EE Core User's Manual
pp62/88. Operations with documented undefined results remain explicit validation
boundaries rather than fabricated guest exceptions or guessed arithmetic results.
EE User's Manual: timer configuration/counting pp34-37 and INTC pp28-31,
archived at https://github.com/ninjadynamics/PS2Docs/blob/main/EE_Users_Manual.pdf.
Native timer and interrupt delivery scheduling remains unfinished.
Public thread descriptor/status ABI: kernel.h declarations. Console ConfigParam
bit layout: https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/osd_config.h.
Native settings are constructed fields, not copied captures; native default UTC
differs from the original launch's observed timezone field 270.

DMA register/status and destination-chain semantics: EE User's Manual pp48,55-56,
61,65,73-79. SIF0 receives explicit qwords; other endpoints, cnts/stall control,
tag priority and automatic timing remain unsupported. Register fields whose reset
values are unspecified initialize to zero as native policy, not claimed hardware state.
SifSetDChain void ABI and channel setup: public EE kernel declarations at
https://ps2dev.github.io/ps2sdk/kernel_8h.html and
https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/kernel.h.
SIF register IDs/descriptor layout: public sifdma.h. Search also surfaced unrelated
implementation snippets; they were not used to derive the native receiver.

Read-only volume parser: ECMA-119 fourth edition (2019), §§7.2.3/7.3.3,
8.4, 9.1, directly from the standards organization:
https://ecma-international.org/wp-content/uploads/ECMA-119_4th_edition_june_2019.pdf.
Downloaded reference stays outside the repository at `%TEMP%/hg-ecma119.pdf`.
The CVM origin 0x1800 is independently established from the local file: the primary
descriptor is at 0x9800, logical block size 2048, and root extent 22 resolves to valid
directory records only with that origin. No CRI reader/decryption implementation used.

IOP import/export ABI declarations: public
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/kernel/include/irx.h.
The module inventory independently validates local ELF section and relocation bounds,
unlinked library table headers/stub encodings and export ordinals. Relocated zero
export pointers are retained. No IOP implementation or emulator code is used.
An unrelated emulator source result appeared during a header search and was not
used; subsequent inspection uses local module bytes and the public ABI above.

Build-time MIPS relocations: MIPS ABI supplement, chapter 4 pp4-18 through 4-20,
https://refspecs.linuxfoundation.org/elf/mipsabi.pdf. The local IRX subset uses
symbol index zero and types 2/4/5/6 only; every HI16 is immediately followed by LO16.
The independent relocator supports those paired records and explicitly rejects
other symbol/relocation forms. No IOP executable image is claimed from the dry-run.

MIPS-I processor-family timing reference: IDT R3051/52 hardware manual (1992),
chapter 2 p2-9 describes one-instruction load and branch delays:
https://www.bitsavers.org/components/idt/risc/1992_IDTR3051_R3052_RISController_Hardware_Users_Manual_Rev1.3_19920821.pdf.
The distinct IOP runtime implements dependency-free delayed loads and branches;
load-use/write hazards explicitly fault pending PS2-specific validation. No cache,
device or coprocessor behavior is inferred from this different chip's implementation.
The CW33000 manual was also consulted for the MIPS-I instruction overview; no device
implementation was imported. The small MODMSIN function uses dependency-free schedules.
The same IDT manual chapter2 p2-15 describes signed/unsigned multiply and divide
HI/LO results. Used only for architectural integer operations; timing and undefined
division results remain outside the implementation. Reference PDF is external TEMP.

SYSCLIB public ABI declarations and export ordinals only:
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/system/sysclib/include/sysclib.h.
The tested memcpy(12), memset(14), strlen(27) execute independently translated bytes
from the supplied SYSCLIB member; no SDK function implementations were used.
sprintf(19) also tested through its supplied prnt callback and relocated format table.
The callback offset0x16b0 is loaded by sprintf/vsprintf; thirteen prnt callsites load
it from stack+104. The character range check is <121 and indexes table offset0x18c0.
Those relationships were decoded directly from the supplied SYSCLIB member.

LOADCORE and INTRMAN public function signatures/ordinals were consulted at
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/system/loadcore/include/loadcore.h
and https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/system/intrman/include/intrman.h.
The loadcore header includes an internal structure attributed to PCSX2; that structure
is excluded from use. Boot-mode behavior/layout was independently decoded from the
supplied LOADCORE: export12 offset0x6e0, export13 offset0x684, pointer initialization
at0xcc..0xd8 writes low-memory0x3f0/0x3f4. Tests run those original translated routines.
Original LOADCORE linker helper offset0x11d8 was decoded and executed directly:
it writes J target into each valid ordinal stub, preserves its ADDIU ordinal delay
word, and sets import flags. A diagnostic lowering only an import table's minor
version proves that it accepts a newer same-major export. Combined AOT planning
uses that observed compatibility relation, but still rejects newer imports,
different majors, and ambiguous compatible providers; guarded code checks the
exact planned words.
FlushIcache/FlushDcache adapters implement the public void ABI with native coherent
memory fences/epochs, not copied hardware cache loops.

IOP DMA register addresses, DMACMAN ordinals, and start/chain flags use public
declarations only:
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/system/dmacman/include/dmacman.h.
IOP MIPS-I load-delay behavior follows the R3000 User's Manual load scheduling
rules. The supplied CRI_ADXI bytes provide the concrete overwrite-in-delay-slot
case used by startup; a direct register write or a newer load to the same
destination cancels the older pending load rather than consuming its value:
https://usermanual.wiki/Document/r3000manual.723589236/pdf.
The public PS2SDK IOP register declaration identifies the channel-shaped DMAC2
register bank at `0x1f801560..0x1f801568`:
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/common/include/iop_regs.h.
The supplied SIFMAN bytes independently establish the live completion-status
read of its MADR at `0x1f801560`; the runtime currently retains this otherwise
unused bank as explicit register state and does not attach a transfer endpoint.
DPCR/DPCR2's four-bit channel fields (priority bits 0..2 and enable bit 3),
including channels 11/12 at DPCR2 bits 16..23, are from
https://psi-rockin.github.io/ps2tek/index.html#iopdma. The native adapters update
only those configuration fields; they do not implement a DMA endpoint or completion.
SIF register/initialization flag ABI:
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/system/sifman/include/sifman.h.
IOP service error codes:
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/kernel/include/kerr.h.
Native INTRMAN adapters retain handlers, masks and nested interrupt state; delivery
is pending. No SDK implementation files were used. Hardware probe observations are
documented separately in ORACLE.md; they are not a full device emulation profile.
HEAPLIB public ABI declarations and import ordinals only:
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/system/heaplib/include/heaplib.h.
The bounded native heap uses checked guest-RAM addresses and independent metadata;
it does not use an SDK allocator implementation. INTRMAN callback setter signatures
and ordinals28/30 are from the public intrman.h header cited above.
SECRMAN memory-card callback setter signatures and ordinals 4/5 are from the
public ABI declaration at
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/security/secrman/include/secrman.h.
The native state retains or clears those checked pointers only. Card
authentication ordinal 6 is not implemented or reported successful.
IOP timer register banks, width, mode-write reset/mode-read acknowledgement and
clock source: https://psi-rockin.github.io/ps2tek/index.html#ioptimers. The native
implementation independently uses a fixed-point 36.864 MHz clock and checked
compare/overflow delivery; exact gated/external-clock behavior and all timer
mode combinations remain unsupported.
CDVD N-command parameter layout, DVD 2064-byte sector framing, I_STAT meanings
and one-sector DMA sequencing: https://psi-rockin.github.io/ps2tek/index.html#cdvdioports.
Only the documented ReadDvd (`0x08`) path is modeled. Local ISO bytes are read at
runtime through an explicit bounded sector request; no extracted game bytes enter
source control.

The IOP SIF0 source-chain contract is independently derived from original SIFMAN
descriptor construction plus the EE DMA tag definitions cited above. It accepts
the observed one-, two-, whole-qword, and six-word payload shapes; a non-final
EE CNT tag remains pending and is never promoted to a completion without its
own documented terminal transition.

The ROMDIR subset is independently decoded from the game-provided IOPRP300.IMG:
16-byte entries have a 10-byte name, 16-bit extended-info size and 32-bit member size;
members advance at 16-byte boundaries. Empty RESET precedes ROMDIR (320 bytes) and
EXTINFO (608 bytes). Metadata validates extents and extended-info totals; no BIOS
or emulator implementation is used, and no modules are extracted into the repository.

Connected SIF transport uses independently decoded SIFMAN tags and the EE User's
Manual source/destination-chain definitions. Original six-word reply padding was
validated as retaining the previous payload qword's upper two lanes; see the
external capture provenance in ORACLE.md. No captured payload bytes are embedded.
Original SIFCMD entry/export4/export14 and registered callbacks implement the
actual handshake. Native event/interrupt services use public ABI declarations:
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/system/threadman/include/thbase.h
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/system/threadman/include/thevent.h
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/include/kernel.h
Only header declarations were used, not SDK implementations.

CACHE DHWBIN opcode0x18 is from EE Core Instruction Set Manual p299; coherent
native RAM uses a store fence/epoch. The original RPC sender at0x26f6e8 uses a
0x20000000 RAM alias. Native compatibility explicitly permits that alias and
orders the same RAM stores; the hardware manual leaves uncached CACHE behavior
undefined, so this is a native policy rather than a claim of hardware accuracy.
Other uncached/accelerated/SPR cases remain unsupported. The bounded RAM aliases
are part of the native boot mapping, evidenced by original SIF buffer construction.

### Native IOP worker services, 2026-09-04 22:03
Thread/semaphore structures and export ordinals use public ps2sdk ABI headers
`iop/system/threadman/include/thbase.h` and `thsemap.h`; console signature from
`iop/system/sysmem/include/sysmem.h`. Implementations are independently written.
Offsets come from export tables in the supplied THREADMAN/SYSMEM/STDIO modules.
FILEIO worker pointers and heap service0x80000003 are from its original bytes.
Cooperative scheduling and the 1us/slice clock are explicit native compatibility
policy, not copied or inferred emulator timing behavior. Independent decoding of
the supplied THREADMAN `CreateThread` export establishes its SYSMEM ordinal-4,
mode-1 stack allocation and 256-byte size rounding. The native scheduler uses
that checked high-side system arena. The public `thbase.h` declaration identifies
ordinal 5 as `DeleteThread`; lifecycle semantics are independently implemented
and no SDK implementation file is used.

### IOP BIOS selector-32 context transfer, 2026-09-05

The user's external SCPH-39001 BIOS (SHA-256
`f4c948e61a291d4b3f92a141e550cf8357204287a31ff784cacbbedaef910c9d`)
was used only as an original hardware-software input; no BIOS bytes enter the
repository or generated source. A coherent external 2 MiB IOP RAM observation
(SHA-256 `0ff7a8e6da65772560219a4794a5c8b465f6f06746f6c91926cc4dc3d109da04`)
shows syscall-table slot `0x5a00` selecting handler `0x5830`. Independently
decoded instructions establish that this handler constructs a 0x98-byte frame,
saves GPRs, HI/LO, status and EPC+4, calls the registered new-context callback
through `0x59d0`, and restores the callback-selected context. The live callback
values were `0x0000c368` (new context) and `0x00012098` (preemption decision).

The supplied game THREADMAN bytes independently establish the only configured
selector-32 site: wrapper `+0x6640` chooses scheduler field `+0x5c` or `+0x60`
from boolean `a3`, publishes it at field `+0x54`, then invokes selector 32 at
`+0x665c`. The native adapter maps this exact, identity-checked boundary onto
the already-native cooperative thread contexts. It advances scheduling past
the current owner, releases masked ownership, and records the mode/site/count;
unknown syscalls continue to fault. This is an independent semantic adapter,
not emulator code and not a runtime interpreter.

### IOP unaligned words and CD/DVD register subset
LWL/LWR/SWL/SWR follow IDT R3051 manual Table2.4 (PDFp33). The MIPS-I
same-destination forwarding exception is explicit in NEC's archived MIPS IV
instruction-set errata, AppendixA-29:
https://www.bitsavers.org/components/nec/mips/MIPS_IV_Instruction_Set_Errata_199511.pdf
This rule permits unaligned-load merging of a preceding pending load; ordinary
register reads keep the existing delay hazard check. Synthetic tests cover all four
byte alignments with consecutive paired loads and guarded paired stores.
The same MIPS-I instruction reference defines BREAK as an exception-producing
terminator with an encoded code field. IOP AOT output preserves that code in an
explicit guest fault and does not discover or execute a fallthrough successor.
CDVD register addresses/status/W1C and ReadRTC byte layout are cross-checked with
https://psi-rockin.github.io/ps2tek/index.html#cdvdioports and the original CDVDMAN
poll/read/write sequence. Only the register protocol is used: emulator-specific
version constants, timing formulas and reported implementation choices on that page
are excluded. Native RTC is a UTC startup snapshot; empty result reads preserve a
zero-initialized last-result latch as explicit native policy. No disc command is
reported complete, and unknown S commands fault.
That hardware reference defines drive-status bits 1 (spindle spinning) and 3
(paused); the mounted local DVD compatibility profile therefore initializes
current and sticky status to `0x0a`. This is a documented physical profile state,
not a fabricated command completion.
The configured local input is classified as PS2 DVD for the read-only disk-type
register (`0x1f40200f` -> `0x14`); original CDVDMAN's decoded classifier accepts
that value. This is only mounted-media identity; the separately bounded ReadDvd
N-command, one-record DMA, and sector-read completion do not imply broader CDVD
support.
Public `sceCdGetToc` ABI documentation declares a 1024-byte destination buffer;
the independently decoded original CDVDMAN request emits N-command `0x09` with
one zero byte and arms the documented 2064-byte CDVD DMA shape. The TOC payload
itself is not inferred from any emulator implementation. The diagnostic accepts
it only as a user-supplied, exactly 2064-byte external `--toc-record` input at
run time; it is copied through the original DMA completion path and never added
to the repository. Without that artifact it remains an explicit boundary.
The original supplied CDVDMAN bytes also establish a request-frame signature:
its GetToc wrapper at module `+0x70b0` calls the internal implementation at
`+0x6f1c`, whose frame stores the `4 x 0x81` DMA descriptor at `sp+0x18` and
the wrapper return `module+0x70c4` at `sp+0x48`. The oracle helper uses both
facts only to locate a same-stage destination; it never treats a generic DVD
read descriptor or module-relative buffer guess as TOC provenance.
The same hardware reference's independent opcode tables identify COP2 `QMFC2`,
`QMTC2`, and the EE-specific `SQC2` encoding. The runtime models only their
documented 128-bit data movement and checked EE-memory store; it does not derive
any VU arithmetic, microprogram behavior, or data payload from emulator code.
Its COP0 register map identifies register 28 as TagLo; the EE Instruction Set
Manual's MFC0 definition supplies the sign-extended transfer semantics. Cache
tag operations themselves remain outside this register-preservation work.
The original CDVDMAN request path also writes its request error byte to
`0x1f402006` before programming DMA channel 3; the native boundary retains this
byte as a readable latch. Its error meanings and any asynchronous hardware effects
are deliberately not inferred.
The supplied CDVDMAN setup routine at module `+0x505c` passes module `+0x4288`
as the callback while registering interrupt cause 2, establishing that callback
as a static root without dynamic target guessing. Its completion path reads only
the low nibble of byte register `0x1f402013`. That register is not named by the
available hardware table, so the native synchronous endpoint exposes only an
explicit zero idle/decoder state after it has already committed the bounded DMA;
other widths and writes remain faults. This is a native completion policy derived
from the original consumer branch, not a claim about undocumented hardware bits.
The supplied `.iso` is a separate ECMA-119 volume at origin zero (its `SLUS_210.75;1`
entry hashes to the configured executable digest); `DATA.CVM` retains its distinct
`0x1800` volume origin. CDVDMAN's channel-3 CHCR `0x41000200` setup is retained as
an active, guarded DMA arm prior to a CDVD command. The checked media endpoint
handles only an exact one-record ReadDvd transfer and its IRQ completion.

Controller version query03:00 uses an explicit native compatibility profile1.0.0
(status0, version bytes1/0/0), not an emulator's device identity. The supplied
CDVDMAN decodes the three version bytes as big-endian and selects older feature
paths below0x010700 (own routine0xb54d4). Unknown subcommands still fault; this
identity does not imply that all hardware functions of that revision are implemented.
The existing external RAM capture identifies a CDVDMAN export table at0x2e730
with entry0x22830 and exports47/50 at0x228d0/0x27548; no captured code is translated,
and no emulator-specific version bytes are embedded.

### SIO2 startup boundary (2026-09-05)
- PS2SDK's public DMACMAN header identifies ordinals 28/32 as `sceSetSliceDMA`
  and `sceStartDMA`; the runtime retains checked register setup and faults when
  a transfer lacks a peripheral endpoint.
- ps2tek's hardware table identifies `0x1f808200..0x1f808280`, including the
  SEND buffers, byte FIFOs, control register, and result registers. The native
  register bank implements reset/configuration state only; starting a command
  without a controller or card endpoint faults.
- PS2SDK's VBLANK header identifies ordinal 5 as `WaitVblankEnd`; the cooperative
  native wait uses the diagnostic clock. Its SECRMAN header identifies ordinal
  6 as `SecrAuthCard`; linking is permitted but invocation remains an explicit
  fault until a real memory-card/authentication endpoint exists.
- https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/system/dmacman/include/dmacman.h
- https://psi-rockin.github.io/ps2tek/index.html
- https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/system/vblank/include/vblank.h
- https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/security/secrman/include/secrman.h

### Native reboot boundary (2026-09-04)
- PS2SDK ABI headers only: ee/kernel/include/syscallnr.h identifies SifStopDma0x6b;
  kernel.h describes its void SIF0-disable ABI. Native implementation supports idle
  packet boundaries only and leaves queued data/status intact.
- ee/kernel/include/sifdma.h names hardware IDs1..4; shared flags use direction-specific
  set/clear semantics. Returning the submitted value is an explicit native policy.
- iop/system/sifcmd/include/sifcmd.h identifies export10 as AddCmdHandler. Native
  reboot gateway is registered through the original export, receives actual DMA
  dispatch, and parses the layout derived from original EE routine0x275700.
  Original IOP0x98740 clears packet size before copying to callback storage.
- https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/syscallnr.h
- https://github.com/ps2dev/ps2sdk/blob/master/iop/system/sifcmd/include/sifcmd.h

### SPU2 voice-transfer completion inference (2026-09-08)
The user's original LIBSD, independently decoded by `hgtool.iop_build.prepare`,
selects voice-transfer mode at relocated `0x31a60..0x31a68`, polls bit 7 of
`0xbf900344 + core*0x400` at `0x31a80..0x31ab8`, clears ATTR bits 4:5 at
`0x31ac0..0x31adc`, and then dispatches its completion callback. The native
endpoint now reports this observed completion condition only after its checked
one-shot RAM-to-SPU copy has committed. A transfer-mode change retires the
condition; AutoDMA does not assert this voice-transfer condition. This is an
independent, bounded inference from the original consumer and actual completed
copy, not verified general STATX/DREQ semantics or a hardware timing claim.
Sony's SPU2 Overview v6 pp12/55 establishes local-memory DMA and TSA semantics
but does not document this status register. Broader status fields remain work.
Web searches for status documentation returned SDK implementation and PCSX2
archive snippets; those were not used as implementation sources. No emulator
code or algorithms were consulted to implement the endpoint.

### EE startup interrupt context (2026-09-08)
Original EE `0x26cc70` returns `(Status ^ 1) & 1`; StartThread's user wrapper
at `0x26d2f8..0x26d304` rejects a nonzero result. The native launch profile's
old `0x70010000` cleared IE, so all such calls were rejected before the syscall.
The native executable-launch profile now enables IE (`0x70010001`); it is not
represented as a hardware reset value. Independent hardware register reference
https://psi-rockin.github.io/ps2tek/index.html#eecop0exceptionhandling identifies
IE bit0, EIE bit16, EXL bit1 and ERL bit2. DMA dispatch now gates on both enables
and neither exception level, clears IE/EIE during the native callback, and
restores the saved context on return. Synthetic tests exercise the gates and
original consumer-compatible callback context. No emulator implementation used.

### EE C.LE.S startup clamp (2026-09-08)
Sony EE Core Instruction Set Manual v6 p351 specifies function bits `110110`
(0x36), zero fd field, exact less-or-equal comparison into FCR31 bit23 and
signed-zero equality. The original object method reached at `0x20e9a0` uses
this instruction at `0x20e9c4` to clamp its input against 1.0. The independent
EE decoder/emitter now supports `c.le.s`; the runtime preserves all other
control bits. Native FPU tests cover equality, signed zeros and either side
of positive/negative bounds. Existing `c.olt.s` is retained as the project's
older spelling for the manual's C.LT.S encoding. No host NaN semantics used.

### DS2O_S1 load and worker (2026-09-08)
The connected original LOADFILE path requests `MODULES/DS2O_S1.IRX` after sound
setup. Its existing configured digest and base are used without emulator data.
Original DS2O entry +0x3c/+0x40 constructs relocated worker +0x164 and stores it
at stack+40 before CreateThread (thbase ordinal4) at +0x54. That supplies the
worker's AOT root. PS2SDK's public vblank.h defines ordinal4 as WaitVblankStart:
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/system/vblank/include/vblank.h
The import can be linked, but invocation explicitly faults pending a connected
display-timing source. This is not implemented by aliasing the old diagnostic
WaitVblankEnd delay. Module plans are now shared between initial load and reboot.

### Corrected LOADCORE import-version comparison (2026-09-08)
The original supplied LOADCORE bytes, read with identity-checked iop_source and
independently relocated/decoded, show the full link chain: `0x96cac` scans import
tables, `0x96d14` invokes `0x97138`, and `0x9717c` calls `0x97038`. That helper
loads both version halfwords at +8, shifts right8 and subtracts; a nonzero major
difference rejects, while a zero difference proceeds to patch stubs at `0x9718c`.
No minor comparison occurs on this link path. The separate minor comparator
`0x96ff8` is called by export-registration/replacement code at `0x969c8`.
The earlier lower-import-minor experiment proved only one direction and the
planner's inferred newer-export-only constraint was too strict. Planning now
accepts unique same-major providers in either minor direction, still rejecting
major differences and ambiguity. Synthetic tests cover both minor directions.
This resolves the actual DS2O_S1 request for SIO2MAN 2.6 against the supplied
SIO2MAN 2.4 export table; all three requested ordinals are present.


### First native SIO2 digital poll (2026-09-08)
The preserved original request is FIFO `01 42 00 00 00`, SEND3[0]
`0x00140540`, SEND1[0] `0xffc00505`, SEND2[0] `0x00020014`, CTRL
`0x3bd`. External native capture: `%TEMP%/haunting-toc-probe/sio2-packet.ram.json`.
Hardware reverse-engineering reference (register/protocol descriptions only):
https://psi-rockin.github.io/ps2tek/index.html#sio2controllercommands
and its SIO2 Registers / IOP Interrupts sections establish digital poll header,
active-low button payload, connected RECV1 `0x1100`, and IRQ17. The bounded
native virtual controller accepts only this five-byte port-zero poll and exposes
mutable button bits, initially released. Analog/configuration, other ports,
DMA and memory cards remain unsupported. Completion is a cooperative native
policy after response publication, not cycle-accurate serial timing. Other
response/status registers retain the prior incomplete register-bank behavior.
Original identity-checked SIO2MAN +0x77c/+0x780 installs callback +0x584 for
IRQ17 at +0x784. Own AOT decoding of that callback shows interrupt-status
read/write then event flag `0x2000`, returning 1. The new IRQ dispatcher invokes
this original callback and restores interrupted CPU context. No emulator code
or algorithms used.


### Vblank wait before display configuration (2026-09-08)
The connected original DS2O worker calls vblank ordinal4 at `0x2de3c`,
return `0x2c1a0`, before any native SetGsCrt call: configured=0, interlace=0,
mode=0, frame=0. External evidence: `%TEMP%/haunting-toc-probe/display-mode.log`.
Public ABI names ordinal4 WaitVblankStart (vblank.h cited above). The native
wait now parks its calling worker and consumes only a subsequently published
start event, allowing independent EE startup to run. A display-event publisher
API wakes existing waiters once; elapsed instruction/time budgets do not wake
them. No display clock is invented before a mode exists. Actual display timing
production and EE/IOP vertical interrupt routing remain unimplemented.


### Second controller port (2026-09-08)
The original next poll differs only in SEND3 bit0: `0x00140541`, with
`01 42 00 00 00` input. Captured in external `dbcman-rpc.ram.json`.
The ps2tek SIO2 Registers description identifies SEND3 bits0:1 as port and
RECV1 `0x1d100` as disconnected (`0x1100` connected):
https://psi-rockin.github.io/ps2tek/index.html#sio2registers
Native launch policy now has two independently configurable ports, first
connected and second absent. For the bounded digital poll, an absent port
publishes five idle `0xff` bytes and disconnected status, then completion.
The idle-byte fill is an explicit native absent-device policy, not a claimed
hardware capture of all timeout behavior. Both connected ports use independent
active-low button words. Multitap port values2/3 and other commands still fault.


### User-shared HG configuration inspection (2026-09-08)
User authorized looking at four files under the separate `../HG/config/`
project while reiterating no copying from ps2recomp. Read the headers and
initial content of `game.toml`, `recomp.base.toml`, `functions.manual.toml`
and `functions.analysis.csv` for provenance. The base config explicitly
identifies PS2Recomp/ElfAnalyzer output; manual entries explicitly cite
PS2Recomp identification among mixed evidence. The CSV appears to be a
function-map export but its provenance is not established by its own header.
No maps, addresses, boundaries, patches, stubs, runtime bindings or behavioral
claims from these files were imported or used to guide implementation.
Continue independent discovery from the user's checked original executable.


### Reference-use clarification (2026-09-08)
The user clarified that looking at the shared HG material as a reference is
allowed, while copying it verbatim is not. It may inform context and ideas;
implementation still requires independent verification and independently written
code. The earlier provenance inspection imported nothing. This clarification
does not authorize using emulator implementation code.


### MCMAN driver initialization (2026-09-08)
Identity-checked original relocation gives driver descriptor `0x4700c`, whose
operations pointer at +16 is `0x46fa0`. The first operations entry points to
`0x44c10`, matching live IOMAN dispatch (return `0xa79b0`). Own decoding
shows `jr ra` with a delay-slot zero return. This original two-instruction
callback is now an AOT root at MCMAN +0xfc10; no host success stub replaces it.
The following operations table spans through `0x47008`; remaining callbacks
are not added until their role/use is established.


### Rendering restriction clarified (2026-09-08)
User explicitly said not to copy ANYTHING related to rendering from the shared
reference project. This includes rendering code, configuration, mappings,
patches and algorithms. Rendering remains independently derived from original
inputs and hardware specifications. No rendering material has been imported.


### MCSERV startup callbacks (2026-09-08)
Original MCSERV entry +0x10c/+0x110 constructs worker +0x280 and stores it
in the CreateThread descriptor at +0x120. That worker builds service ID
`0x80000400` at +0x2e4/+0x2e8 and handler +0x324 at +0x2ec/+0x2f0 before
SifRegisterRpc at +0x304. Both callbacks are now AOT roots derived with the
project's identity-checked original-module decoder. MCSERV is selected in the
bundle and shared initial/reboot module plan using its existing verified digest
and base `0x5d000`. No reference-project material used for these changes.


### VIF1 startup register access (2026-09-08)
Own original ELF decoding at `0x10bfb0..0x10bfd0` proves a word write of1 to
`0x10003c10`, followed by2 to `0x10003c20`. Hardware register descriptions:
https://psi-rockin.github.io/ps2tek/index.html#vifioregisters
identify these as VIF1 FBRST reset and ERR ME0 respectively. The independent
runtime resets its VIF interface/FIFO state, retaining separate VU memories,
and retains the supported ERR bit. Other stall/interrupt/error-mask operations
remain explicit gaps. Tests exercise cached/uncached MMIO routing, FIFO/phase
reset, preserved VU memory, mask readback and rejected widths/masks. No shared
HG/ps2recomp rendering material was used to derive or implement this change.


### CPU VIF1 quadword submission (2026-09-08)
The original EE stores a complete GPR quadword to `0x10005000` at `0x10c004`
and again at `0x10c00c`. The independent runtime's SQ path now sends both
64-bit halves in order into its existing VIF1 parser, including GS interrupt
synchronization after parsing. The public PS2SDK register header identifies this address as VIF1 FIFO:
https://github.com/ps2dev/ps2sdk/blob/master/common/include/ee_regs.h
Only register definitions were used; incidental emulator search results were
not consulted for implementation. Tests cover uncached alias routing, commands in both halves,
zero-register NOP submission, and unsupported scalar FIFO width rejection.
No reference-project rendering code, configuration or algorithms used.


### GIF CTRL reset (2026-09-08)
The original store at `0x10c014` writes1 to `0x10003000`. The hardware
register description identifies GIF_CTRL bit0 as GIF reset:
https://psi-rockin.github.io/ps2tek/index.html#gifio
The independent GIF endpoint currently retains only an incomplete packet
buffer; reset clears that transport/parser state without resetting the separate
GS register/VRAM object. Stop/restart operations remain explicit gaps. Tests
cover the uncached register alias and preserve state on unsupported width or
control writes. No reference-project rendering material used.


### GS CSR system reset (2026-09-08)
Sony GS User's Manual v6 pp145-146 identifies CSR bit9 as system reset and
bit8 as the distinct FLUSH operation. Page154 specifies that all five IMR
mask bits start at1 after reset. Read from the existing external manual
`%TEMP%/hg-gs-users-manual.pdf` (archived source cited earlier).
CSR reset now cancels the native GS draw/vertex/transfer state, restores its
initial register/display profile and masks events. It retains allocated local
memory contents. The consulted manual does not enumerate every drawing/display
register reset value or specify a VRAM erasure; defaults and retained VRAM are
explicit native initialization policy, not hardware-capture-verified values.
FLUSH remains an explicit unsupported operation. Tests cover cancellation,
masking, self-cleared reset indication, and retained local memory. No shared
reference-project rendering material was used.


### Controller full pressure reply (2026-09-09)
Native startup-controller-query.ram.json contains the original DS2O packet
01 4f 00 ff ff 03 00 00 00 with descriptor0x240940. Non-rendering reference
configuration searches for pressure/0x4f/SIO2/DS2O gave no applicable lead.
Derived from https://psx-spx.consoledev.net/controllersandmemorycards/
(Analog Buttons) and https://psi-rockin.github.io/ps2tek/ (SIO2 input format):
the full18-byte mask enables digital buttons, axes and twelve pressure bytes,
yielding mode79 and21 wire bytes. Other masks explicitly fault. Command44
resets pressure mode. Pressure values are retained input state, initially zero;
host input is unfinished. The config trailing zero follows psx-spx, whose
noted last-byte variability remains unverified on hardware. Protocol moved to
runtime/controller.cpp to avoid recompiling all IOP translations per edit.
No emulator or shared implementation used.


### Native IOP ExitThread (2026-09-09)
Original IOPRP300 THREADMAN export thbase:8 points at+0x1288; independently
parsed export table and decoded body mark status16 at current TCB+12,
enqueue dormant state and request scheduler transfer. DS2O at0x2c72c calls
its linked thbase:8 stub0x2ddcc; the startup-controller-pressure capture then
reaches the original panic when an unimplemented host lifecycle resumes it.
PS2SDK thbase.h API declaration confirms ordinal8 is ExitThread:
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/system/threadman/include/thbase.h
No SDK implementation read. Native adapter retires the current worker into
existing dormant sentinel state, retaining stack and ID. Tests check immediate
quantum termination, retained state and subsequent deletion; invalid owners fail.
Shared non-rendering configuration break/ExitThread searches supplied no useful
IOP lead. No trap bypass or guest panic patch was introduced.


### EE interrupt thread-status query (2026-09-09)
Original wrapper0x26c0b0 loads v1=-49 before syscall0x26c0b4. The native
startup-thread-exit run reaches it from0x1cb6a4 with thread4 and interrupt
stack buffer0x5ef90. ps2tek BIOS EE Thread Services specifies syscall31
as iReferThreadStatus, identical status output to30. The interrupt-only
negative selector now routes to31 and shares the existing copier. No
reschedule occurs. Regression compares all48 bytes against the ordinary call.
The following jr/slot are now discovered via returning_syscalls=-49;
EE58,372 words/353 unresolved. Reference non-rendering syscall searches
provided no further applicable lead. Source: https://psi-rockin.github.io/ps2tek/


### Short controller mode probe (2026-09-09)
Native startup-status-return packet01 42 00 00 00 clocks only five bytes
while the controller has negotiated pressure mode. SIO2 descriptor0x140540
requests five input/output bytes. ps2tek describes immediate serial replies
per clocked byte; the returned mode/length byte lets the driver discover the
full format before its next poll. Native42 now accepts five/nine-byte prefixes
of a longer reply, retaining mode79 and truncating at the requested wire length.
Full pressure polling remains21 bytes. A regression checks the precise prefix.


### Startup service callback (2026-09-09)
Native startup-pressure-60m faults at0x1ca660 with return0x1e5c18. Original
ELF0x1e5c00 loads a callback from the runtime table rooted at0x480e20,
then jalr0x1e5c10 invokes it with its adjacent argument. Independent ELF
body0x1ca660 calls0x1d3e58 and returns zero. Added the reached entry as
an AOT root, expanding reachable original code to68,314 words. No runtime
bypass; newly discovered unsupported boundaries still fault. Shared function
inventory provided only a matching32-byte entry; no implementation copied.


### Paired service callback registration (2026-09-09)
Original ELF setters1da7d0 and1da7e0 write a1/a2 to object20/24 and28/2c.
Original dispatcher1da860/1da8b4 calls those stored pointers. The native
startup-service-callback stop at1e4ee0 matches original registration1e514c,
which builds a1=1e4ee0 around an unrelated load into a0. Callback bindings
plus destination-specific invalidation for integer loads now discover both
pairs at callers1e0f34/1e0f4c and1e5138/1e514c: targets1e09c8,1e0a28,
1e4e40,1e4ee0. Only known straight-line constants are accepted; joins,
unknown operations and calls still discard stale state. Synthetic tests
exercise overwritten and unrelated destinations for all12 integer load forms.
No reference implementation copied; source is the original verified ELF.


### Service object table3d3fa8 (2026-09-09)
Native startup-paired-callbacks dispatches1e4010 through jalr1e4f94,
object3d3fe0, slot18. Original constructor1e3c10 builds table3d3fa8 and
stores it at object+0 in1e3c28. Original table has nine non-null methods
from3d3fb4 through3d3fd4; retained them as roots (one already present).
The service-method table itself and all targets came from the verified ELF,
not shared mappings. EE69,101 words/474 unresolved after generation.


### Embedded ck01 module source (2026-09-09)
Native startup-service-table.ram contains ck01 relocated at100030 after
original MODLOAD attempts LOADCORE linking. Original SLUS_210.75 contains
the source ELF at file34aa20; full section-derived extent359 bytes, SHA256
4ca27d8c457807cd6bcf5a8b4fb79f51b4a1ba00b8c10a0dc94936be9339ec51.
Container SHA256 is3b374d53a499d2c17b205274ee9eb34280768f294f970ebf6ae6731f6a2dacb8.
The captured source buffer13dc00 matches those bytes exactly. Independent IRX
parser reports entry0, one144-byte load segment,112-byte text, and cdvdman:50
import stub54. Current module source reader does not support an ELF region;
this remains a required static source/lifecycle extension. Reference config
search for ck01/cdvdman50 supplied no applicable lead. No check patched.


### Embedded region source support and MODLOAD buffer ABI (2026-09-09)
Source reader now supports offset/size with independently checked whole-file
and extracted-module SHA256 identities. Existing native region loader verifies
both identities again before relocation. Synthetic source/bounds/corruption
checks pass. Original MODLOAD exports9/10 resolve to a9440/a948c; own decoding
shows10 calling9 with zero address/offset. API names confirmed only from:
https://raw.githubusercontent.com/ps2dev/ps2sdk/master/iop/system/modload/include/modload.h
No SDK implementation read. Runtime buffered-load allocation integration is
still pending; existing explicit LOADCORE failure remains in place.


### Buffered CK01 lifecycle (2026-09-09)
CK01 static placement0xe0030 reserves its0x30-byte original MODLOAD prefix
and0x90-byte segment. Existing high-water reservation protects that range.
AOT entry hook at original MODLOAD+0x440 observes buffer/address/offset
arguments, matches all identity-checked original source bytes and selects the
allocation plan. It then executes the unchanged original prologue and loader.
No module success value is synthesized; original allocation/relocation/linking
and entry execution remain required. Source bytes are retained from verified
local files, not embedded in redistributable source. Tests check generated hook
execution, original stack adjustment, allocation, duplicate rejection, freeing,
reload, source corruption and unsupported placement without changing selection.


### CRI paired transfer callbacks (2026-09-09)
New startup runs reach original CRI_ADXI callbacks10e90 and110e8 through
jalr10b94 and10c24. Own relocation/decode shows registrations at11454/1146c:
a1 is built at11448/11460, then passed to original setters10b0c/10b18.
Both callbacks added as AOT roots. Non-rendering reference search yielded no
applicable IOP lead. No shared implementation or synthesized audio result used.
The compiled synthetic bundle now includes standalone, ROMDIR and embedded
inputs; all21 regression targets pass, including retained buffer identity,
planned allocation and ordinary execution from the embedded source.


### Object table46a1e0 (2026-09-09)
Native startup-cri-paired reaches169420 via object slot30 at2cfa3c, a0=80adc0.
Original ELF table46a210 contains that unique pointer. All14 non-null methods
from46a1e8 through46a21c were retained together, skipping roots already present.
Original destructor1692a4 explicitly publishes vptr46a1e0 before base teardown.
Wrapper169420 invokes slot2c and multiplies the result by2048. The table and
methods were read only from the verified original ELF; no shared mappings used.


### CK01 original-loader execution evidence (2026-09-09)
External startup-object-table.ram.json includes watch0xe0030 with pc0xe0030,
a0=1,a1=0x17ef80,gp=0xe80b0,sp=0x17ef70,ra=0xaa324. This proves the
original MODLOAD path reached compiled CK01 after identity selection, static
allocation and linking. No module-result or check bypass was introduced.
The run subsequently stops on EE object method1e3268 through jalr1d1f98.
Original table3cff70 slot24 at3cff94 contains that pointer; its full method
set still needs discovery. No playable-frame claim follows from this checkpoint.


### Object table3cff70 (2026-09-09)
Original constructor1e30b8 builds table3cff70 and1e30d0 stores it at object+0.
Native startup-object-table reaches its slot24 (1e3268) through jalr1d1f98.
Retained all nine non-null original table entries3cff7c..3cff9c together,
skipping already configured roots. EE71,536 words/490 unresolved. Source is
verified original ELF bytes; function inventory supplied only the matching
method boundary. No shared implementation or rendering material used.

### Priority restoration and CRI command callbacks (2026-09-09)
Original EE1cafa8 saves ChangeThreadPriority's result at3bfbf0;1cb02c loads it
for restoration at1cb030. The recorded oracle0->1 transition returned old0,
which does not establish a universal zero result. Runtime now returns old priority.
PS2tek's EE syscall list also describes the interrupt variant returning old priority:
https://psi-rockin.github.io/ps2tek/ . Shared non-rendering configuration supplied
the InitThread priority lead only; original call sites establish this behavior.
The corrected run reaches IOP17af0. Original relocated CRI_ADXI17dfc registers
17af0 through10b0c, and17e14 registers17b9c through10b18; both are retained
as static roots. Shared function inventory had no matching17af0 lead.

Original EE23ac58..23ac70 constructs240990 and registers it via2400e8.
startup-command-callbacks reaches this exact target through1e5c10 (ra1e5c18).
Retained callback root240990; shared inventory confirmed boundary only.

Original service registrars1e5898 and1e59e0 save callback/context pairs at
1e5918/1c and1e5a78/7c. Wrappers2400e8 and240198 preserve those arguments.
Declared callback ABIs allow existing constant analysis to recover original
registration pairs1ca7a4->1ca660,1ca7b8->1ca680,23ac6c->240990,
23ac7c->2409d8 for dispatcher1e5c10. This derives from original EE code,
without shared callback mappings. EE72,690 words/496 unresolved.

### Original secondary object table46bf2c (2026-09-09)
startup-service-registrars-120m reaches2107d0 through2cfa98, slot4c,
object887204. Original20e024/2c constructs46bf20/46bf2c;20e030/38
publishes these primary/secondary vptrs. Original secondary table contains
91 non-null methods46bf34..46c09c, ending before zero words46c0a0/4.
Retained all these original roots; EE74,696 words/514 unresolved.
2107d0 adjusts this by-4 and tail-calls20f810. No shared rendering material
or implementation was consulted; the original constructor/table is the evidence.

### Larger raw SIF payload (2026-09-09)
startup-secondary-object reaches original21f8e0 SifSetDma with descriptor
{1992340,136500,2800,0}. Original21f8b4..cc writes source,destination,size,
attributes directly from its arguments. The old4096-byte cap was a native
diagnostic restriction, not a hardware transfer bound. Raw snapshots now use
the available native kernel staging area60000..80000, checking each packet's
aggregate extent before mutation. Source-chain QWC remains within16bits.
Tests transfer/check every byte of a2800-byte payload through the existing
bounded FIFO, preserve snapshot semantics, and reject single/combined staging
overflow without changing transfer IDs or tags. Larger staging and other DMA
attribute/count profiles remain explicit gaps.

startup-large-sif subsequently reaches original CDVDFSVce920 via alarm
dispatch (ra a3b84). Originalcf14c/cf150 andcfba0/cfba4 construct/register
ce920 in a1; retained module offset920. The callback performs original
CDVD service calls and is not replaced with an invented timeout result.

### IOP GetThreadId interrupt return (2026-09-09)
startup-cdvd-timeout reaches9fe94 from original CDVDMANb8060 while inside
the alarm interrupt. Original THREADMAN9fea8 calls import a4b30
(intrman23, QueryIntrContext);9feb0 branches on nonzero to9fed4, which
returns-100. The native adapter now preserves that defined error result;
non-interrupt calls without an established worker still fail explicitly.
Regression checks interrupt context with and without a selected worker, and
the ordinary worker ID result. The focused IOP suite passes.

### CDVD BREAK investigation (2026-09-09)
Original CDVDMANb81b0 writes1 to1f402007; b81b4 reads the same address,
thenb81b8 overwrites its load destination. PS2tek CDVD register section
https://psi-rockin.github.io/ps2tek/ documents any write stopping the current
N-command, but does not establish readback or cancellation interrupt timing.
No emulator implementation was opened. External metadata now retains CDVD
command, remaining sectors, DMA descriptor, error/status and recent MMIO so
the original alarm's cause can be examined before implementing uncertain effects.

### IOP event grant ownership (2026-09-09)
Original THREADMAN iSetEventFlag at a11d8 copies the current pattern to the
waiter's result pointer; a11f0 clears it for mode0x10, before a1200/a120c
removes/wakes the waiter. Native event signaling now performs those effects
at grant time and retains a completed grant for the cooperative retry boundary.
Tests preserve later signals and the original captured result; waiter count
becomes zero immediately after signal. CDVD's current mode0 wait is separately
diagnosed: worker8 is ready but lower-priority than continuously ready worker19.
### EE semaphore overflow (2026-09-09)
PS2tek EE syscall42/43 documents failure return-1:
https://psi-rockin.github.io/ps2tek/index.html . The higher-IOP-budget experiment
reaches SignalSema with a full semaphore4 at26c1d4,ra10eeec. Original caller
10eee4 signals then overwrites v0 at10eef0. Native overflow now returns-1 and
preserves the count; tests verify ordinary and interrupt forms. This does not
claim the diagnostic CPU budget is cycle-accurate or fix every scheduling race.

### CDVD descriptor versus command completion (2026-09-09)
Original IRQ2 path b6404..b6458 reaches request completion b5b18, which
signals event0x29 atb5b48 and disables DMA IRQ35 atb5b54. Publishing
drive I_STAT1 after a partial DMA descriptor therefore terminates the original
request early. DMA completion now remains distinct from drive command
completion: intermediate descriptors only raise their DMA source; the final
descriptor publishes the existing full-command status. A split two-descriptor
regression verifies data, pending command, DMA IRQ and final drive IRQ.
Budget1/4/32 comparisons all reached the prior failure; no scheduling-speed
change is needed to establish this independently decoded lifecycle mismatch.

startup-cdvd-command-completion verifies progress beyond the stranded read:
read_pending0,sectors0,LBA2113120,error0, then dispatches SNDDRV86018 via
original SIFRPC (ra998bc). Original85524..85544 registers server77777778
and callback86018; retained module offsetb018. No RPC result was fabricated.

startup-snd-bank reaches original EE21f290 through SIFRPC26facc. Original
21fac8 and21faf4 select21f2b0/21f290 and pass that completion pointer onward;
the callbacks clear flag bits0/1 and return through original interrupt code.
Retained both callbacks from original executable evidence. EE74,710/514.

startup-snd-completion reaches1bb9d0 via2a7ae0, object4f1920. Original
table46ac78 is slot28 of46ac50; original1aae34 publishes that vptr.
Retained36 non-null methods46ac58..46ace4 together, deriving every address
from the original ELF. No shared rendering configuration or implementation
was consulted. EE89,219 words/590 unresolved after transitive discovery.

startup-object-state reaches2d1b20 through member-pointer helper100b6c,
called by2d1f5c. Original static descriptors414370 and414380 have fields
{0,-1,2d1b20} and{0,-1,2d1aa0}. Retained these two independently verified
direct targets for the existing member helper. EE89,472/598.

startup-member-state reaches2cfbd0 through2d1c2c, slot24 of original table
46f360. Original20dbd8 publishes this vptr; retained all eight non-null
methods46f368..46f384. EE93,808/622 after transitive discovery. Original
executable only; no shared rendering material used.

### VIF1 idle status and MARK (2026-09-09)

Original EE10ca44 reads10003c00 and masks1f000003, checking VPS/FQC.
Sony EE User Manual v6.0 pp143-144 defines idle VPS=0, empty FQC=0 and
MRK bit6, set by MARK and cleared by an EE MARK-register write. Source:
https://github.com/ninjadynamics/PS2Docs/blob/main/EE_Users_Manual.pdf
(local previously downloaded hg-ee-users-manual.pdf independently extracted).
The synchronous transport exposes completed/empty status and tracks MARK.
Incomplete buffered packets still fail explicitly: parser storage is not a
hardware FIFO, so its length is not misrepresented as FQC. VU activation and
interrupt/stall profiles remain checked unsupported boundaries. No shared
rendering configuration or emulator implementation was consulted.

### GIF completed-packet status (2026-09-09)

Original EE10cabc reads GIF_STAT10003020, masking APATH bits11:10.
Sony EE User Manual v6.0 pp149/164 defines idle APATH/OPH/FQC, VIF
mask M3P bit1 and BUSDIR-mirrored DIR bit12. The synchronous transport
reports these existing states, with explicit failure for incomplete packet
status (no invented FIFO occupancy/path arbitration). Unsupported control
and mode writes continue to fail. Source: same original manual linked above;
no shared rendering material or emulator implementation consulted.

### VIF1 DMA tag transfer (2026-09-09)

Connected startup-vif-control proves original EE10d740 writes CHCR145.
EE manual pp45/74 define TTE bit6, sending the tag before its payload;
p86 separates low64 DMAtag from upper two VIF stream words. Independent
transport forwards upper64 into the parser at byte phase8 before data.
A tag crossing an incomplete VIF packet remains an explicit boundary until
physical phase tracking handles the skipped low64 at such crossings.

### Native texture sampling cost correction (2026-09-09)

Own-process context sample PID14356/thread39448,RIP7ff609130d28,
imagebase7ff609120000 (RVA10d28), disassembled with MSVC dumpbin,
identified texture/CLUT extraction during long startup-object-draw run.
Independent runtime point_sample_tex0 invoked full texture extraction per
fragment. Refactored existing conversion into texel_from_tex0 shared by
extraction and sampling, preserving live VRAM feedback without a cache.
No external rendering algorithm or mapping consulted or copied.

### GS address-unit audit (2026-09-09)

Original GS User Manual v6.0 p162 explicitly specifies linear32-bit words
with page8192bytes,block256bytes,column64bytes,32blocks/page and
4columns/block. Existing native *_word helpers mistakenly used these byte
counts as word strides. Captured source textures show corruption, requiring
correction and independent page-coverage verification; no emulator/shared
rendering materials were used in this finding.

GS address audit correction: original rendered GS manual pp164/165
block grids prove CT32/T8 blockx bit2 maps toblockbit4; CT16/T4 blocky
bit2 maps toblockbit4. Pp167/169 indexed column grids alternate wordbit3
by rowbit1/columnparity (XOR), preserving byte/nibble lane assignments.
Full-page synthetic tests now enforce unique coverage of each8192-byte
page across all8 underlying formats and independent next-page separation.

### DualShock2 pressure negotiation (2026-09-09)

Micah Dowty controller-sniffer and packet-generator observations:
https://gist.github.com/brettwsnare/65acb5f2f007f6947c21ae78e0a9af4f
Command 4F ends its response with 5A. Correcting that missing byte lets
the original connected run startup-pad-reply advance to command 40 with
input 01 40 00 00 02 00 00 00 00, captured externally in its IOP JSON.
Command 40 initializes one of twelve pressure sensors with parameter 02;
its six-byte payload response is 00 00 02 00 00 5A. The bounded native
profile records initialized sensors and rejects other parameters explicitly.
Synthetic tests verify every sensor, exact replies, and invalid index.
Shared non-rendering config search found only pad-state function leads;
no shared implementation was read or used. No emulator was used.

DBCMAN original callback verification: identity-checked DS dump module, native
relocation base26000. Instructions263c8/263cc form callback262f4 in a2
before registration call263d0. Connected startup-pad-sensors dispatches
that address, ra9c3ac. Added only the verified AOT root+2f4; its ten words
call the original imported service and return. Bundle now91684 words.

### EE idle thread identity (2026-09-09)

Connected startup-ee-idle reproduces GetThreadId26d078 from original
1cb6c8, during INTC dispatch with current_thread0 and all seven threads
waiting or suspended. PS2SDK kernel.h declares thread0 as the idle thread:
https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/kernel.h
Original caller checks thread status4/12 before querying the running ID.
Native kernel now returns idle ID0 for interrupt queries after scheduler
initialization; accidental execution with no thread outside interrupts still
faults. Synthetic scheduler test parks both threads, queries ID in interrupt,
and verifies neither waiting thread was resumed. No emulator code used.

DBCMAN RPC switch: startup-idle-id reaches27cf8 with command80001303.
Original27c94..27ca4 normalizes by80001301 and bounds selector<99.
27cb0..27cc0 indexes table28560 and jumps to its relocated target.
Configured table offset2560,count99 at site1cc0. This supersedes the
incomplete manually rooted interior cases; generated bundle92344words.

DS2O driver table: original2da58..2daa4 initializes six callbacks at
2e1a0:2db80,2db88,2db90,2dbb0,2dbe4,2dc1c. Original2dab8 registers
the descriptor with pointer at stack+24. Connected startup-dbc-switch
reaches slot3 via DBCMAN27818 (selector3 supplied at27804). Added these
six statically verified roots; generated28 modules/92641 reachable words.

### Initial VU0 macro arithmetic (2026-09-09)

Original EE10db68..10db84 loads a vector, VMUL.xyz squares components,
VADDbc accumulates y/z into x, and VSQRT/WAITQ publishes its length in Q.
startup-vu-multiply captures VF4=(0,0,0,1), VF5=0 at VSQRT10db78.
Independent local original VU User Manual v6.0 pp26-29,39-42,244,296
defines finite exponent255, exponent-zero flush,24-bit truncation, flags,
VMUL and broadcast ADD. Shared own integer FPU arithmetic follows these
same declared number rules; hardware least-bit differences are unmeasured.
SQRT pp193/314 derives a truncated integer significand square root of
absolute input, updates invalid/divide status and sticky bits, and writes
a pending Q value. WAITQ publishes it; unsynchronized Q reads or another
producer explicitly fail in this bounded profile. No cycle-accurate Q
pipeline or microprogram execution is claimed.

Audit pp237-239 also corrects pre-existing decoder errors: QMFC2 rs1,
QMTC2 rs5 (bit0 interlock allowed), SQC2 opcode62. Former rs2/6 are
control transfers and remain unsupported; CACHE opcode47 is never a
vector store. Existing self-consistent decoder expectations were corrected
and negative tests prevent those misclassifications. No shared rendering
material, emulator implementation, or algorithms were consulted.

### Correctly decoded cache tag scan (2026-09-09)

startup-vu-q reaches original26ca04 CACHE0x10; formerly misdecoded as
SQC2. Original EE Instruction Manual p303 defines DXLTG: index[11:6],
way[0], load tag into TagLO. P306 defines DXWBIN opcode14hex. Native
coherent RAM retains no dirty cache copy, so tag lookup explicitly writes
invalid/clean TagLO0; index writeback orders coherent stores. Unsupported
tag-store operations remain faults, so they cannot create hidden valid
lines. Synthetic tag test verifies TagLO replacement and unchanged RAM.
Original26ca0c reads tag, checks PFN against requested range, and skips
nonmatching entries. Reference non-rendering cache-name search had no lead.

### VU FBRST and corrected synthetic fixture (2026-09-09)

startup-cache-tags reaches original10bfd8 CFC2 control28, then OR0200
and CTC2 control28 at10bfe0. Original VU manual pp21/200-203 defines
FBRST reset/enable bits and Ready-state reset. Added CFC2/CTC2 static
transfer decode (control accesses no longer use VF registers), bounded
FBRST reset and enable storage; force break/reserved bits still fault.
Reset command bits readzero and do not clear vector/local-memory data.
VU1 micro activation remains unsupported, so reset begins in Ready state.
Native full CTest found stale synthetic generator encodings atf714/18/1c;
corrected those to specification too. This was a fixture error exposed by
the decoder correction, not a reason to restore the wrong instruction map.

VU Ready status: startup-vu-reset original10ca84 reads VPU-STAT29.
VU manual203-204 documents both VUs idle after reset. Native read returns
zero while no Q result is pending; pending-Q polling remains an explicit
timed-completion gap. Original10db84..10dba0 normalization sequence
requires VNOP,VDIV,VSUB,VMULq. Manual244/253/298/304/315 and lower DIV
p129 define these. Added only these statically decoded operations with
masked arithmetic/flag state and synchronized Q. Exact VNOP preserves
status; it is not an unknown instruction fallback. Synthetic normalization
uses power-of-two length4 to independently verify final unit vector.

VMULbc original10e64c multiplies loaded VF4 by scalarVF5.x. VU manual
p299 specifies four broadcast selectors. Added static selector path with
aliased-destination test. VF0 transfer audit fixes old helpers that used
all-zeroVF0 or permitted writes: VU fixed floating register has x/y/z0,
w1. Reads now preserve that constant across vector/register/memory
transfers; writes ignored after required source-memory validation.

Original member dispatch: startup-vu-broadcast reaches37fef0 from3806bc
through100b40, object888440. Independently decoded380510 copies original
44ae70..44aed8 descriptors into a compact local array, then selects by
object+18. Seven descriptor targets added as roots. Shared non-rendering
config exact-address search found a name lead at functions.manual5780;
implementation and table facts come only from the original ELF.

Default member callback383440: startup-member-states reaches it via
384ba0 tail-call100b40, descriptorobject888570+4. Original384538/384544
loads descriptor44aef0;38456c..78 installs its three words atobject+4.
Added the original two-instruction return function as a static root.
Reference exact-address search found a non-rendering name lead only.

Vtable46f350: startup-default-member reaches2cf8c0 from1bbed0,
slot0c of object94fa10. Original2d1050..5c constructs tablepointer46f350
and writes object+0; slots8/0c contain16cc40/2cf8c0. Both are now
explicit roots. Exact-address shared-config search supplied no implementation;
verification used own ELF reader/decoder and native fault provenance.

Vtable469a60 original lifecycle1218b8..c4 and original startup callsites
2cf8f0/904/918/97c prove four needed slot targets1225c0,121970,121960,
1225d0. Only these startup-selected entries added. Next native capture
startup-object-9a60 reaches VOPMULA10db28, called by2cf92c.
VU manual116-117/305-306 defines cyclic y*z,z*x,x*y products intoACC,
then ACC minus reversed products intoVF, xyz fixed and w preserved.
Independent bounded implementation preserves intermediate sticky flags
and final MAC flags; exceptional OPMSUB accumulator cases remain explicit
faults. VADD p241 shares masked integer24-bit arithmetic. Unit-axis cross
and aliased destination tests verify order and w preservation. Hardware
manual notes one-bit multiply deviations; no hardware oracle measurement
is claimed. No external rendering algorithms or implementations used.

Object46ae90: startup-vu-cross reaches1bf340 from2bf414 slot0c.
Original1bf568..84 installs primary46ae90 atobject+0 and secondary
46aeb4 atobject+0c. Seven primary targets and secondary thunk1bf870
are independently verified from these tables and added as AOT roots.

Original3821f0 byte-command switch: startup-object-ae90 reaches38228c.
3821f8 checks selector<30;382204..218 indexes table463bd0 and jumps.
Configured exact30-entry range463bd0..463c48 as indirect targets, rather
than inventing a function entry for the interior case.

Member service226220: startup-byte-switch reaches it via22653c and
100b40, descriptor atobject4f1850+14. Original1bf350/35c loads3b3028
and1bf378..80 stores its words atobject+14..1c. Added verified function
root; committed display capture at this stop remains black.

### Empty EE PollSema (2026-09-09)

startup-service-member reaches semaphore11 empty PollSema26c1f4.
Original1113cc calls it;1113d8 tests return<0 and conditionally calls
WaitSema. PS2tek kernel documentation45h/46h specifies -1 on failure:
https://psi-rockin.github.io/ps2tek/index.html
PS2SDK kernel documentation says empty polling returns error without
putting the caller into wait state. Native empty case now returns sign-
extended-1, leaves count0 and scheduler untouched. Successful returnID
remains as independently verified by the original USA caller10fa88.
Synthetic tests cover both depletion and unchanged thread state. No
emulator implementation used; reference PollSema-name search gave no lead.

### IOP loader-header collision (2026-09-09)

External startup-sio-callback.ram.json records the only write to original
SIO2MAN callback slot 0x7afdc: thread2, LOADCORE PC0x96840, RA0xab20c,
old0/new8. Original relocated LOADCORE instruction is sw v0,12(a1),
writing the module ID into SNDDRV's header at0x7afd0. The callback setter
0x7ab18 never executes. Original SIO2MAN memory extent is0xff0 bytes,
so its old base0x7a000 overlaps that header by0x20 bytes. Moved SIO2MAN
base to0x79fd0; its full allocation [0x79fa0,0x7afc0) now fits between
SIO2D ending0x79d60 and SNDDRV header0x7afd0. Regenerated original IRX
relocations/bindings, without copying any implementation. Planner now
reserves each complete rounded allocation including the 0x30-byte prefix;
reserved_low is0xffd0 to include CRI_ADXI's existing first header. Runtime
static plan registration also rejects header overlaps before execution.
Synthetic tests cover non-overlapping images with colliding headers,
exact adjacent allocation boundaries and reserved-low rejection. Narrow
shared configuration module-name leads did not explain this corruption;
the original LOADCORE write and local capture establish its cause.

### Original DMACMAN slice setup (2026-09-09)

startup-module-spacing passes the invalid callback and reaches SIO2MAN's
memory-card DMA setup at relocated0x7adf8. Original packet at0x47020 is
81 11 00 00, SEND3=0x100472 (port2, DMA both ways, four serial bytes),
BCR=one block of36 words. Original SIO2MAN0x7a274 calls SetSliceDMA and
0x7a27c separately calls StartDMA. Read-only original BIOS inspection:
emu/ps2 bios/scph39001.bin SHA256
f4c948e61a291d4b3f92a141e550cf8357204287a31ff784caccbedaef910c9d;
ROMDIR at0x2740, DMACMAN at0x287f0 size0x36f5, member SHA256
b0c35cb83cc61385cc86b1665b7a65e49dda71588feffc3ba70575267f1cc559.
Own relocation/decoder identifies ordinal28+0xcc4: CHCR gets0x200 plus
direction bit0 and bit23 for device-to-memory; ordinal32+0xe84 adds bit24.
Corrected native SetSliceDMA which previously started prematurely and
omitted request-mode bits. No emulator source or algorithms consulted.

The bounded SIO2 DMA endpoint accepts only one 81/11 absent-card probe,
one36-word block each direction, and a four-byte serial exchange. It
validates all buffers and modes before mutation, raises the SIO2 missing-
ACK result0x1d100 and completion, and accounts for the full DMA block.
PS2SDK hardware register definitions identify SEND3 port bits0..1,
TX/RX DMA bits4/5, normal-transfer bit6 and byte counts8/18:
https://ps2dev.github.io/ps2sdk/sio2regs_8h_source.html
Wisi's own hardware investigation reports DMA blocks synchronized to
queue elements, with receive DMA triggered after a queue element:
https://www.psx-place.com/threads/mx4sio-sio2sd-sd-card-adapter-and-sd-driver-for-the-ps2-sio2-interface.29210/page-11
SCPH-39001 absent-card RECV1 observations:
https://redpanda4552.github.io/ps2-sio2-docs/#recv1-test-results-memcards-only
Fidelity limitation: the four absent-device reply bytes are pulled-high;
the remaining140 DMA bytes currently use the same0xff profile value.
This unused tail has NOT been measured on hardware or a memory oracle.
Do not claim byte-exact DMA padding, cycle timing, general queued DMA,
present memory cards, or save support. Other packet/mode profiles fault.

The next startup-card-dma capture confirms the 81/11 probe completed:
RECV1=0x1d100 and RX block is FF. MCMAN then requests a five-byte81/F3
probe (SEND3=0x140572), so the absent-card endpoint now accepts any
81-selected serial packet fitting one36-word block on card ports2/3.
It does not interpret commands for an absent device. Multi-block DMA and
other device selections remain explicit faults. This is a disconnected
card profile, not a card protocol implementation. Tail fidelity remains
unverified as above. Original-state capture is external; no game bytes
were added to tracked inputs. Synthetic tests include either card port
and differing serial lengths.

startup-card-probes proceeds to a PIO81/52 probe with SEND3=0xc0342,
three input/output bytes on card port2. Added the corresponding absent-
card PIO transport, returning pulled-high serial bytes and missing-ACK
status; the original module continues to own command/error handling.
SIO2 method bodies now reside in runtime/iop_sio2.cpp, linked through
hg_iop_peripherals, so peripheral-only edits no longer rebuild the large
AOT translation units. No runtime translation or dispatch fallback added.

### MCMAN alarm callback root (2026-09-09)

startup-card-pio reaches original relocated MCMAN0x3ef84 from native
THREADMAN0xa3b84 while serving an alarm. Original MCMAN+0xa13c/+0xa140
forms callback+0x9f84 in a1; +0xa144 calls thbase ordinal35 via import
stub+0x11b48. Three later sites install the same callback. Its body
signals the semaphore at relocated0x48f10 through thsemap ordinal7 and
returns0. Added only this verified AOT root (10words). Narrow supplied
HG configuration SetAlarm/memory-card names yielded no relevant lead.
No emulator implementation or captured code was translated.

### EE memory-card information completion (2026-09-09)

startup-card-alarm reaches EE111450 via original SIFRPC completion26fad4,
with a0=47c200, a1=198af80. Original1115a8/1115bc constructs111450 in
register11 (a7), and1115cc submits the RPC through2700e8. Added this
verified22-word callback, which copies requested information outputs
from the uncached reply. No source/reference implementation copied;
narrow shared configuration111450/mcGetInfo lookup yielded no lead.

### Rendered memory-card check and prompt state (2026-09-09)

startup-card-info runs beyond90million connected slices and captures
8061 submitted/7898 rasterized draws. The independently rendered640x448
scanout visibly reads Checking the memory card (8MB) for PlayStation2
and the do-not-turn-off warning. External capture:
%TEMP%/haunting-toc-probe/startup-card-info.ram.display.ppm.png.
This is a new visible startup screen, not playability or save support.
The next EE stop383550 is an object+4 member callback dispatched by
384bdc/100b40, object94f898. Original descriptor44af10 contains target
383550 at+8;3842e0/3842e4 loads it and38430c..384318 installs the member.
Added the verified230-word state callback. Derived only from original
ELF; no shared rendering-related configuration or implementation read.

### Independent native rendering and static-register optimization (2026-09-14)

Our replay42 GS capture independently records the movie sprite PRIM0x116,
TEX0=0x664022200, CLAMP=0x6fc007fc00a, FRAME=0x10a0000 and TEST=0x30000.
Candidate43 reduces redundant texture address/shading work only for the checked
neutral PSMCT32, TCC1/TFX0, REGION_CLAMP, RGB24 path. It composes the X and Y
contributions of our existing CT32 address function and retains live VRAM reads,
original destination addressing, masks and pixel order. No shared rendering
material or emulator implementation was used. Full-VRAM differential cases
include feedback overlap and address wrap; invalid states retain explicit faults.

Candidate44 changes only how our EE emitter expresses already decoded scalar
register operands: constant storage locations, architectural zero reads, and
evaluated discarded results. Runtime helper semantics and dynamic index checks
are retained. Fixed-input replays43fixed/44fixed have identical full EE/GS/machine
state, all1066 guest CSC timestamps and the order of1315 shared sampled images.
Synthetic original-independent programs cover every destination, aliases, upper
halves, zero storage, loads that fault, branch annulment and discarded JALR links.
The diagnostic input-at facility replays observed button transitions at exact
slices and is accounted for as HG-DIAG-006; it changes no execution clock rules.

### Checked scratchpad and CSC arithmetic optimization (2026-09-14)

Candidate46 extends our existing fixed-width byte-lane RAM operations to the
already implemented16KiB scratchpad range. Alignment and complete spans remain
checked, with the unchanged general memory methods as the differential reference.
Candidate47 tabulates only the existing CSC integer coefficients for byte inputs
and uses equivalent clipping before a final shift. No external rendering material
was consulted. Exhaustive16,777,216 RAW8 colors match a separately expressed
rational formula; threshold/fallback and stream-pressure regressions also pass.
The arithmetic profile's physical-console limitations remain in ORACLE.md.
Replays46fixed/47fixed preserve all1066 timestamps and full EE/GS/machine state.

### IOP history and host presentation optimization (2026-09-14)

Candidate49 separates our diagnostic IOP instruction ring from existing SIFMAN
event snapshots. Static hooks preserve those events without instruction history;
committed register reads preserve original delayed-load behavior. No new guest
code roots, adapters, clock rules or service substitutions were introduced.
All23 CTest entries pass and connected49fixed preserves47fixed guest timestamps,
EE/GS/machine state,1316 images and retained SIF/SNDDRV diagnostic reports.
Candidate50 changes only the independent OpenGL preview consumer: file polling,
handle lifetime, texture reuse and optional host presentation measurements.
No shared rendering material or emulator implementation was consulted.
### Texture product saturation (2026-09-19)

Sony GS User's Manual6.0 p59, section3.4.9, defines the texture product as
(A*B)>>7 with the result clamped to0..255. Our MODULATE path previously packed
the wider product directly, allowing RGB carry into adjacent channels and alpha
wrap. The shared product helper now saturates before packing; HIGHLIGHT's
subsequent nonnegative alpha addition and saturation remain equivalent.
Synthetic tests isolate bright red, green, blue and alpha overflow. Derived
from the local hardware manual, with no external rendering implementation.

### Scene texture filtering gap (2026-09-19)

Original captured scene draw environments (stq-after2.ram.gs.json) include
18132 textured draws with TEX1=0x60 and1488 with TEX1=0. Character texture
groups also use0x60. GS manual6.0 p127 defines MMAG bit5=LINEAR and MMIN
bits8:6=1=LINEAR; p62 specifies level-zero bilinear for that MMIN mode.
At audit time the native shade_point_tex0 path ignored those filtering fields
and sampled nearest texels. Texture-product saturation corrected the color tint.
Manual pp28/50/58 specifies fractional texel coordinates, half-texel centers,
the four-neighbor interpolation and independent wrapping of each neighbor.
The new bounded path retains fractional UV/STQ through rasterization.56 synthetic
UV samples across four wrap modes match external completed memory-oracle
readbacks and native packet replay (ORACLE.md). Mixed min/mag selection and
mipmapping remain explicit gaps; physical-console/STQ precision parity is unproven.
No external renderer code or algorithm was consulted.
# Optimization architecture reference review (2026-09-19)

Follow-up opposite-lane sprite candidate derives solely from captured original
draws and our GS-manual-derived swizzles. Retained33M draw history includes narrow
PSMCT16S strips whose source/destination share a32-bit word but occupy different
16-bit lanes. A host proof checks every row/column component, rejects filtering
fractions and repeated destination words, and retains depth/frame isolation.
Since no texture-source bit is modified, no new feedback-cache assumption is needed
for this bounded case. Existing wider GS-cache/precision limits remain. Tests cover
both lane directions, VRAM wrap, masks, DATE/blending and broken-proof fallback.

The bounded VU optimization changes host inlining and an algebraically equivalent
MAC bit packing expression only; arithmetic, register checks and readiness remain
the existing independently derived definitions. Six original state/image comparisons
and focused arithmetic/hazard tests pass (RTC excepted). This does not establish
previously unverified EFU/FPU last-bit precision or physical-console timing.

Sprite-format expansion implements this project's existing CPU address/TEXA,
TFX, blend, DATE/Z, dither and FRAME-mask formulas. Original GS-manual-derived
definitions remain in gs.hpp/gs.cpp; no external implementation was used. The
host descriptor/address decomposition and paired16-bit word ownership were
derived from our existing swizzles and independently compared against CPU writes.
853 differential cases include accepted GPU paths, alias fallback and CLUT faults;
original replay preserves images/EE/GS/VU (RTC excepted). This establishes bounded
equivalence, not full physical-console precision/cache/timing parity.

User explicitly requested comparison with PCSX2 and other PS2 recompilation
projects. Read primary documentation only for architectural leads:
- https://pcsx2.net/docs/troubleshooting/performance/ distinguishes internal FPS,
  video output and speed; its counters prioritize readbacks/uploads/copies.
- https://pcsx2.net/blog/2026/pcsx2-2.8/ reports gains from avoiding transfers and
  excessive render passes, and notes ordered blending has backend-specific costs.
  These are optimization leads, not independent proof of PS2 behavior. Adjacent
  sections describing game fixes/cache behavior were exposed; none adopted.
- https://github.com/ran-j/PS2Recomp/blob/main/README.md documents literal ELF-to-C++
  translation, runtime services and optional stubs/overrides. This is an experimental
  framework, not evidence that equivalent faithful HG work runs at console speed.
  No implementation or shared HG rendering configuration was inspected/copied.
- https://opengoal.dev/docs/porting-info/porting_to_x86/ documents a game-specific
  native port with separate game/system services. Its historical status details
  are not treated as current feature coverage.
- https://opengoal.dev/docs/porting-info/drawable_and_tfrag/porting_tfrag/ describes
  first understanding a faithful slow renderer, then offline preparation and a
  specialized PC renderer with fewer draws. We reviewed architectural prose,
  including Jak-specific pipeline descriptions; no code/algorithm was imported.
  OpenGOAL is a GOAL decompilation/native port, not a drop-in generic PS2 AOT runtime.

Search also exposed PCSX2 GS.cpp snippets about renderer lifecycle/cache readback
and timing counters; no implementation copied. Our resident triangle implementation
predates this review and derives arithmetic from our own checked CPU path/manuals.
Applicable next steps are workload measurements, fewer ownership transitions and
batched host work. Game-specific high-level replacements would require independently
proving HG inputs, outputs, memory effects and ordering before adoption. No external
reference authorizes skipping guest work, changing clocks or weakening faults.

## Broader completed-project survey (2026-09-19, user requested)

Searched completed/playable PS2 static recompilations and decompilation native
ports, plus named Sly/Ratchet/Colossus leads and project catalogs. Catalogs were
only discovery aids; checked primary project statements. This is a bounded search,
not proof of an exhaustive worldwide inventory. No external code was imported.

- OpenGOAL Jak and Daxter,Jak II,Jak3: official FAQ says feature complete and
  completable; II/3 remain beta with audio/graphics caveats. Three games sharing
  one project, not three unrelated rendering systems. https://opengoal.dev/docs/faq/
- OpenGOAL graphics prose documents bucket-specific renderers,shared textures,
  ordered frame synchronization and batching of consecutive compatible primitives.
  https://opengoal.dev/docs/porting-info/graphics/ is historical architecture,not
  a current feature-coverage list. Its embedded illustrative code was exposed but
  not adopted. Prior tfrag review already describes offline geometry preparation
  and fewer host draws; no need to copy/re-review its detailed algorithms.
- Q2 report: remaining fidelity issues include inherited blend state and ocean
  environment mapping. Completion does not establish bit-exact console behavior.
  https://opengoal.dev/blog/progress-report-q2-2026/
- 3rd Strike: game code fully decompiled,CRI middleware partially so:
  https://github.com/crowded-street/3s-decomp/blob/main/README.md . Native successor
  https://github.com/crowded-street/3sx uses SDL3 rendering and supports desktop
  platforms, but its 1.0 roadmap remains open and releases are labelled prerelease:
  https://github.com/crowded-street/3sx/releases . Reviewed README/docs inventory
  and releases; no rendering implementation inspected. Count as additional released
  native-port reference, not verified finished/static-binary recompilation.
- BT3-Recomp README claims near30fps fights but notes graphical defects and skipped
  FMVs. Not counted finished. Read status prose only; no runtime source/configuration
  or mappings inspected. Adjacent README exposed its experimental external GS
  backend and benchmark claims; neither adopted nor independently verified.
  https://github.com/z3xox/BT3-Recomp/blob/main/README.md
- Sly1 primary README is WIP matching PS2 code run via PCSX2,not native desktop
  rendering: https://github.com/TheOnlyZac/sly1 . Silent Hill Origins/GTA VCS
  PS2Recomp fork search status remained experimental/incomplete VU1:
  https://github.com/BlackLineInteractive/SHO-GTA-VCS-PS2Recomp . No implementation
  inspected. Other discoveries (.hack announcement,Fate PSP/PS2 hybrid) lacked
  verified completion and were not treated as finished reference systems.

Inference for HG: large gains may require independently proved game-specific
native batches/assets rather than only scalar helper tuning. Verify all observable
RAM/VRAM/state/order effects before such optimization; retain faithful reference
and explicit faults. Immediate PSMT8 span candidate predates this survey and uses
only our existing original-spec-derived addressing. No architecture reference
changes the project's AOT-only,OpenGL,independent-runtime constraints.


## 2026-09-20 — per-batch GL command storage API provenance

Khronos glBufferData reference confirms a new store is created and supplied data
copied for initialization; STREAM is a usage hint, not a performance guarantee.
Read official source XML because registry XHTML could not be fetched by browser:
https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glBufferData.xml
Canonical: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBufferData.xhtml
Independent host change replaces scratch command/tile storage only. No external
rendering implementation/algorithm copied. Existing ordered GL behavior must remain
and GPU/original-state comparisons verify our usage; speed remains experimental.


### Selective MSVC whole-program compilation experiment (2026-09-20)

Microsoft /GL and /LTCG documentation supplies compiler option semantics only:
https://learn.microsoft.com/en-us/cpp/build/reference/gl-whole-program-optimization?view=msvc-170
https://learn.microsoft.com/en-us/cpp/build/reference/ltcg-link-time-code-generation?view=msvc-170
Local same-toolset IR allows cross-unit optimization during linking; explicit
/LTCG avoids linker restart. Restrict trial to generated VU main and own helpers;
no external rendering/runtime implementation used. Performance must be measured.

### MSVC profile-guided optimization experiment (2026-09-27, rejected)

Microsoft PGO documentation supplied build-tool semantics only:
https://learn.microsoft.com/en-us/cpp/build/profile-guided-optimizations?view=msvc-170
https://learn.microsoft.com/en-us/cpp/build/reference/genprofile-fastgenprofile-generate-profiling-instrumented-build?view=msvc-170
https://learn.microsoft.com/en-us/cpp/build/reference/useprofile?view=msvc-170
The local14.51 toolchain independently supplied `/FASTGENPROFILE`, `/LTCG`,
`/USEPROFILE` and `pgomgr`. Runtime instrumentation writes scenario PGC data; the
valid helper-only trial explicitly merged the one fresh exact-link PGC into its PGD
before USE. Training used only our existing native build and fixed original33M
recording; no external code, algorithm or runtime backend was introduced. The USE
candidate preserved exact state but was slower than adjacent OFF, so all PGO build/
provenance/test plumbing was removed. PGD/PGC/training-wrapper files remain ignored
external evidence only and are not production inputs. See HG-DIAG-085/HG-FAIL-045.

### Packed integer add host-ISA references (2026-09-20)

Own scalar Fpu::add (EE Core manual already cited) remains arithmetic reference.
Microsoft intrinsic lists and CPUID documentation supply host primitive/feature
semantics only, not a copied arithmetic algorithm:
https://learn.microsoft.com/en-us/cpp/intrinsics/x86-intrinsics-list?view=msvc-170
https://learn.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex?view=msvc-170
Intel SDM supplies AVX OSXSAVE/XGETBV and AVX2 feature requirements:
https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf
No host floating-point rounding is used. AVX2 is isolated from generic dispatch.


Packed-add refinement: exact integer-to-binary32 conversion is used only for bit
length of m<=2^24-1, where all inputs are exactly representable. Guest arithmetic
and chopping remain integers.16M tests in4rounding modes verify scalar equality and
unchanged host FP exception flags. This independently derived bound revises the
initial all-integer-normalization experiment; no external arithmetic code copied.


## Live gameplay secondary command switch - 2026-09-20

Paced live capture live-paced-input-check stopped at EE202d3c after32805801
slices. Own decoder on configured USA ELF SHA2563b374d53a499d2c17b205274ee9eb342
80768f294f970ebf6ae6731f6a2dacb8 verifies SLTIU202d14 bound8 and BEQ202d18 rejecting indexes>=8, index shift202d24, base456a40 built202d20/28, load202d30
and JR202d34. Eight original pointers through exclusive456a60 target202d3c,
202d64,202d6c,202d7c,202de0,202df0,202e00,202e10. External proof artifact
live-secondary-dispatch-table.json retains exact addresses/hash. Narrow lookup
of ../HG/config/functions.manual.toml for these non-rendering addresses supplied
corroborating bounds only; no implementation, patches or rendering material used.
Register exactly that range for offline translation; no runtime decoder/fallback.
## Staircase object family 46c780 - 2026-09-27

User live capture `%TEMP%/hg-stairs-live-20260927-145012` stopped explicitly at
original EE `0x0025f0a0`, RA `0x001bbed8`, object `0x00fc8990`. The common
original caller `0x001bbebc..0x001bbed4` loads object slot `+0x0c`; captured RAM
holds vptr `0x0046c780` and target `0x0025f0a0` at that slot. Original ELF and
captured RAM agree exactly on the null-bounded pair
`[0x0046c788,0x0046c790) = {0x0025e950,0x0025f0a0}`, with null words at
`0x0046c780/84` and `0x0046c790/94`. Original `0x0025e968..70` materializes and
installs `0x0046c780`. Independently decoded return/delay boundaries are
`0x0025e9ac/b0` and `0x0025f574/78`; the next body starts at `0x0025f580`.
Original/live code hashes match byte-for-byte:
`25e950..25e9b4 = 884404a0...` and `25f0a0..25f57c = d83d380f...`.

The user-shared configuration had an exact-address function-list entry, but this
body constructs graphics work, so that reference result was discarded and did
not supply the boundary, target set, implementation, mapping or algorithm. The
bounded AOT change is independently derived solely from the original ELF and
captured live state: append both proved family methods to the existing
`0x001bbed0` target union. Generation added740 original instruction cases in one
EE shard and removed none. Candidate executable `8d01457f...` matched151-input
post-link provenance; fixed evidence `%TEMP%/hg-stairs-25f0a0-fixed-20260927`
qualified all864 original writers and every retained state digest with zero
validation failures. Only a live replay can prove traversal beyond this later
manual stop.

## Door member callback 197640 - 2026-09-27

After the independently verified `25f0a0` coverage repair, the user traversed the
stairs and reached a new explicit stop while opening a door. External capture
`%TEMP%/hg-stairs-25f0a0-live-20260927-150328` reports EE `0x00197640`, RA
`0x001a367c`, object `0x01510c80`. Original caller `0x001a3674` invokes the
existing generic member helper `0x00100b40` with object descriptor at `+0xa0`;
captured RAM contains `{0,-1,0x00197640}` there. Four initialized original ELF
records contain that exact descriptor (`0x003b2608`, `0x003b26c8`, `0x003b26d8`,
`0x003b2e98`). Original and live body `0x00197640..0x00197fe8` are byte-identical,
SHA256 `93aa6e2f...`; own decoding proves return/delay at `0x00197fe0/1fe4` and
the next prologue at `0x00197ff0`.

The shared analysis list contained only a truncated start entry and the shared
manual list omitted this body, so neither supplied a usable function boundary,
mapping or implementation. The AOT change roots only the observed callback.
Generation adds exactly618 original instruction cases from entry through return
delay, loses none, and leaves twelve dynamic virtual calls as explicit checked
transfers. Candidate executable `9750e473...` matches151-input provenance;
fixed evidence `%TEMP%/hg-door-197640-fixed-20260927` qualifies all864 writers
and every retained state digest with zero validation failures. Live door replay
is still required; no audio, renderer, clock or runtime fallback changed.

## Sequential door member callback 196fc0 - 2026-09-27

The qualified `197640` build executed that callback and advanced to a second
explicit door-state callback. External capture
`%TEMP%/hg-door-197640-live-20260927-151207` reports EE `0x00196fc0`, RA
`0x001a367c`, object `0x01510c80`. The same live descriptor at object `+0xa0`
now contains `{0,-1,0x00196fc0}`, exactly matching the unique initialized
original descriptor at `0x003b28e8`. Original/live body
`0x00196fc0..0x00197640` is byte-identical, SHA256 `80d017c5...`; own decoding
proves return/delay `0x00197638/3c` and the already verified next prologue at
`0x00197640`. The authorized shared manual function list corroborates only that
independently established extent; no implementation or mapping is imported.

Rooting only `0x00196fc0` adds416 body instructions plus independently reached
direct callees,1,761 cases total in one shard, with no existing cases lost; two
dynamic transfers remain explicit checked operations. Candidate executable
`db4e2b27...` matches151-input provenance. Fixed evidence
`%TEMP%/hg-door-196fc0-fixed-20260927` qualifies864 original writers, zero
validation failures and every retained state digest. The actual door route still
requires live continuation. Earlier `25f0a0` and `197640` coverage is retained.

## Door-transition virtual callback 1aa040 - 2026-09-27

The qualified `196fc0` build advanced through the door sequence and stopped at
the next explicit missing AOT target. External capture
`%TEMP%/hg-door-196fc0-live-20260927-152451` reports EE `0x001aa040`, RA
`0x001271bc`, object `0x017b1b80`, and argument `0x01511fb0`. Original caller
`0x001271ac..0x001271b8` loads the object's vptr and slot `+0x0c`, performs the
virtual call at `0x001271b4`, and preserves the observed return address. Captured
RAM holds vptr `0x0046abb0` and target `0x001aa040` in that exact slot. The table
contains additional non-null sibling methods, so they were deliberately not
rooted without observed or independently bounded reachability evidence.

Original ELF and captured RAM body `0x001aa040..0x001aa718` are byte-identical,
SHA256 `1e5210bf850b6de9be9baf77bb543e39e1f8480e6be48360e1957acbebf6ae31`.
Own decoding proves return/delay at `0x001aa710/14` and the next body begins at
`0x001aa720`. The authorized shared manual function list corroborates only this
independently established non-rendering extent; its truncated analysis output
was not used for a mapping or implementation. Rooting only `0x001aa040` adds596
generated instruction cases, including all438 instructions in the bounded body
and independently reached direct callees, with no previous case lost. Four
dynamic calls remain explicit checked transfers.

Release executable `d1c17b67...` matches its151-input post-link provenance.
Fixed evidence `%TEMP%/hg-door-1aa040-fixed-20260927/evidence` qualifies all864
original writer events with zero caller, trace, stack, writer or state failures;
frame, EE, GS VRAM/state, VU and RTC-normalized IOP digests remain exact. The
measured10.6981 FPS is ordinary host variance for this coverage-only change and
is not an optimization result. Live continuation is required to prove progress
beyond this callback. No renderer, audio, clock, input or runtime fallback changed.

## Door-transition virtual callback 1a9f90 - 2026-09-27

The qualified `1aa040` build advanced to a distinct virtual call in the same
door-transition object. External capture
`%TEMP%/hg-door-1aa040-live-20260927-154203` reports EE `0x001a9f90`, RA
`0x001271e8`, object `0x017b1b80`. Original caller `0x001271d8..e4` loads the
object vptr then slot `+0x14`, calls it at `0x001271e0`, and supplies the object
in the delay slot. Captured RAM contains vptr `0x0046abb0` and target
`0x001a9f90` at that exact slot. The surrounding live/original table has many
other initialized entries and is therefore not evidence to root its siblings.

Original ELF and captured RAM body `[0x001a9f90,0x001a9fe4)` are byte-identical:
84 bytes, SHA256 `0cadda6beff13f1c5168256f053b11c7301474ea4759d3c7ac79f7e408115cb9`.
Own decoding proves return/delay at `0x001a9fdc/0x001a9fe0`, padding follows,
and the next prologue begins at `0x001a9ff0`. The authorized shared non-rendering
manual list corroborates only this independently established extent; no code,
mapping or implementation was imported. The AOT change roots only the observed
entry and adds exactly21 original instruction cases, with no old cases lost and
no unresolved instruction inside the body.

Release executable `bbaea93f...` matches its151-input post-link provenance.
Fixed evidence `%TEMP%/hg-door-1a9f90-fixed-20260927/evidence` qualifies all864
original writer events, zero validation failures, and exact retained frame, EE,
GS VRAM/state, VU and RTC-normalized IOP digests. The measured11.2036 FPS is
ordinary host variance for a coverage-only change, not an optimization claim.
Live continuation is required to establish the next route state. No renderer,
audio, clock, input or runtime fallback changed.

## Door-transition direct-member callback 1a4c60 - 2026-09-27

The game-scoped HG-DIAG-006 recording run progressed through the newly compiled
`1a9f90` entry and stopped at its first internal member dispatch. External
capture `%TEMP%/hg-door-route-recording-20260927-160542` reports EE
`0x001a4c60`, RA `0x001a9fc0`, object `0x017b1b80`, member `0x017b1b90`.
The original `0x001a9fb8` call invokes the existing `0x00100b40` helper with
descriptor address `member+0x1004c = 0x017c1bdc`; captured RAM contains
`{0,-1,0x001a4c60}` there. The identical initialized original descriptor occurs
at VA `0x003b2f28`, independently confirming this specific dynamic target.

Original ELF and captured RAM body `[0x001a4c60,0x001a4cbc)` are byte-identical:
92 bytes, SHA256 `d78f4286c5f80366bed3f33db4c54065206a1ff5dafe6fc6194344e11bcf1ac9`.
Own decoding proves return/delay at `0x001a4cb4/0x001a4cb8` and the next prologue
at `0x001a4cc0`. The authorized shared non-rendering manual list corroborates
only that independently established extent. Rooting only `0x001a4c60` adds
exactly23 original instruction cases with no old cases lost and no unresolved
operation inside the body.

Release executable `3882ee01...` matches its151-input post-link provenance.
Fixed evidence `%TEMP%/hg-door-1a4c60-fixed-20260927/evidence` qualifies all864
original writer events, zero validation failures, and exact frame, EE, GS
VRAM/state, VU and RTC-normalized IOP digests. The measured11.1543 FPS is host
variance for a coverage-only change. The user's107 observed controller-state
transitions are retained only in external evidence; the derived108-event replay
adds one disclosed neutral release after the stopped guest boundary. No global
keyboard input, renderer, audio, clock or runtime fallback changed.

Unattended evidence `%TEMP%/hg-door-route-replay-20260927-161726` delivered all
108 scheduled states at their recorded slices with live host input disabled.
The repaired build passed the former `0x001a4c60` boundary and completed the
350,000,000-slice budget without another EE fault, retaining external final
IOP/EE/GS/VU/display state. This proves the recorded prefix can reproduce and
clear the door route through that boundary; it does not claim correctness for
unrecorded movement after the final neutral state or complete-game playability.

## Authentic TOC DVD CAV read profile - 2026-10-06

A bounded PCSX2 guest-state observation recorded in `docs/ORACLE.md` supplies
the exact external GetToc DMA payload for the current local disc. With that
payload, the original compiled CDVDMAN path issues ReadDvd packet
`[16,0,0,0,1,0,0,0,16,131,0]` for ISO LBA 16. This independently proves the
game uses spindle byte `0x83` on the single-layer directory path. The existing
native model rejected that byte before touching disc data.

As a semantics cross-check only, official PCSX2 source commit
`9fffbdbd59b962d63a2259b150f419ad3773e7b4` treats the low six spindle bits as
the DVD speed selector and the high bit as CAV; `0x83` therefore selects the
documented maximum DVD speed class rather than an invalid read mode. The native
local-image path now accepts only its two observed successful profiles, `0x02`
and `0x83`; all other mode bytes, nonzero final bytes, unsupported retry values,
physical seek timing and retry behavior remain explicit/unsupported. No PCSX2
implementation was copied.

## Exact wide perspective-STQ fallback - 2026-10-06

The post-door retained GS state selected an ordinary textured Gouraud triangle
whose finite S/T/Q inputs and edge weights produced a valid signed texel
coordinate but required more than 64 bits while aligning the three floating
mantissas. The renderer change is independently derived from the existing GS
equation: accumulate signed weighted mantissas at a common binary scale in a
two-limb unsigned magnitude, cancel signs exactly, then divide the S or T sum by
the Q sum with the existing dimension shift. A right shift retains a sticky bit
for the established negative floor rule. Native `_udiv128` or compiler
`unsigned __int128` is used only for exact 128-by-64 division; a restoring
division supplies the portable equivalent. A bounded exact quotient search is
used when the denominator itself occupies both limbs. Inputs whose aligned sum
does not fit 128 bits retain the explicit validation fault.

Read-only inspection of official PCSX2 source at pinned commit
`9fffbdbd59b962d63a2259b150f419ad3773e7b4` was used only to corroborate that
finite varying-Q triangle STQ is handled as ordinary perspective interpolation,
not as an invalid packet class. No external implementation, algorithm, comment,
configuration, mapping, or rendering code was copied. Synthetic tests contain
only constructed bit patterns; the faulting game packet and replay remain
external under `%TEMP%`.
## EE PREF instruction semantics (2026-10-06)

- Sony Computer Entertainment Inc., *EE Core Instruction Set Manual*, version
  6.0, April 2002, `PREF : Prefetch`, page 96. The primary opcode is `110011`;
  the fields are base, five-bit hint and signed 16-bit offset. Hint zero requests
  a load prefetch, while reserved hints 1..31 behave the same as zero. The manual
  states that addressing-related exceptions are ignored, lists no architectural
  exception, and defines only advisory address translation/prefetch activity.
  Source consulted as the independently published Sony manual at
  `https://docs.alexrp.com/mips/ee_insns.pdf`; the searchable transcription at
  `https://studylib.net/doc/28989088/ee-insns` was used to locate and verify the
  exact page text. This supports the cache-neutral static AOT implementation;
  it is not emulator-derived behavior.

## EE CACHE DXIN instruction semantics (2026-10-07)

- Sony Computer Entertainment Inc., *EE Core Instruction Set Manual*, version
  6.0, April 2002, `CACHE DXIN : Cache Operation (Index Invalidate)`, page 301.
  The instruction uses the `CACHE` primary opcode `101111` with operation field
  `10110` (`0x16`) and format `CACHE 0x16, offset(base)`. The signed base-plus-
  offset virtual address supplies data-cache index bits `[11:6]` and way bit
  `[0]`. DXIN clears Lock, Valid and Dirty while preserving LRF, and the manual
  requires the applicable cache-operation sequence to be directly preceded by
  `SYNC.L`; the listed instruction exception is Coprocessor Unusable. The source
  is the same independently published Sony manual and searchable transcription
  cited above for PREF. This establishes the decoder and cache-state semantics
  independently of emulator code. In the native runtime's coherent-cache model,
  every represented data-cache line is already invalid, clean and unlocked, so
  this exact indexed invalidation changes no modeled cache state and performs no
  guest RAM transfer.
## Live direct-member target `0x002f4330` (2026-10-07)

The corrected native-renderer manual run recorded an explicit EE static-translation
fault at `0x002f4330`, with `a0=0x00fc6fe0`, `ra=0x002f609c`, near slice 64M.
The external raw log is `%TEMP%/haunting-toc-probe/manual-authentic.err.txt` and
is not stored in the repository.  The return address and object agree with the
already independently decoded `0x002f6094 -> 0x00100b40` member-descriptor call
and direct `JR t9` path at `0x00100b6c` documented below.  This proves only that
`0x002f4330` was the next live direct target.

Independent decoding now bounds that target to `[0x002f4330,0x002f46a4)`: its
own return is `JR ra` at `0x002f469c`, with stack restoration in the delay slot
at `0x002f46a0`; three padding words follow and a distinct stack prologue begins
at `0x002f46b0`.  The complete 884 original file-backed bytes exactly match the
same range in the retained live EE RAM capture
`%TEMP%/haunting-toc-probe/edge-adjacent-control-w0-20261007.iop.bin.ee-ram.bin`,
SHA-256 `9c9030c522e3483e83e29451db17e359a481b12e9721ca90e7b3dac8f4f6fee3`.
This proves only this callback and body; it does not root the neighboring
`0x002f46b0` function or resolve the callback's remaining dynamic transfers.

## VU MADD exception envelope (2026-10-08)

Sony Computer Entertainment Inc., *VU User's Manual*, version 6.0, April 2002,
sections 2.1--2.4 and 3.3.1--3.3.6, especially pages 26--29 and 39--43.
Unlike IEEE 754, every exponent-255 mantissa is a finite VU value `1.F * 2^128`;
exponent zero is signed zero.  The MADD exception table states that canonical
`+MAX/-MAX` ACC remains saturated for a nonexceptional or underflowed product,
while an overflowing product supplies its own saturated sign.  Other finite ACC
encodings use the same 24-bit truncating add, including exponent-255 operands
and final addition overflow/underflow.  Final-add flags populate current and
sticky fields; multiplication zero, sign, underflow and overflow contribute to
sticky fields only.
The temporary source PDF is `%TEMP%/hg-vu-users-manual.pdf`, SHA-256
`a858ef55cd2638accdfca612d5359eb6e48a48c7fd6792b18080d866e610fde5`,
retrieved from `https://docs.alexrp.com/mips/ee_vu.pdf`.  This supports only the
documented VU arithmetic envelope.  It does not establish physical-hardware
least-bit behavior beyond the manual's stated truncation allowance.

## SPU2 voice synthesis (2026-10-13)

- Sony SPU2 Overview Manual v6.0 (<https://github.com/ninjadynamics/PS2Docs/blob/main/SPU2_Overview_Manual.pdf>): voice pipeline (ADPCM decode, pitch transformation, PMON P'=P(1+OUTX), envelope, volume, mixing switches), PITCH (0x1000 = original pitch), ADSR1/ADSR2 field layout, VOLL/VOLR direct/sweep modes, ENVX/VOLX/ENDX/NAX/LSAX (4 Ts rewrite rule) semantics, KON/KOF, VMIX/MMIX bit assignments, sound-data input areas.
- psx-spx (nocash) SPU chapter (<https://psx-spx.consoledev.net/soundprocessingunitspu/>) and CDROM format chapter (XA/SPU-ADPCM decode, filter tables pos 0,60,115,98,122 / neg 0,0,-52,-55,-60, reserved shifts 13..15 = 9): ADPCM block/flag format, pitch counter and clamp, 4-point Gaussian table (512 hardware constants; groups sum 0x7F7F..0x7F81, verified on import), envelope step algorithm, noise generator, sweep fields, SPUCNT enable/unmute bits (used for SPU2 ATTR bits 15/14, matching the game's 0xC000/0xC081 writes). The counter reset after a step is inferred from the documented step-period semantics. SPU2 runs this voice hardware at 48 kHz.
- EE Core User's Manual v6.0 Table 8-4 p162 (<https://docs.alexrp.com/mips/ee.pdf>): SQRT.S of a negative operand returns sqrt(|x|) with I=1.
- No emulator implementation was consulted.

## 2026-10-23 Hardware-captured EE FPU results (ps2autotests)

`unknownbrackets/ps2autotests` (ISC license): test programs run on a PS2 with their captured outputs. Only the `.expected` result files of `tests/cpu/ee_fpu/` (arithmetic, muldiv, sqrt) are used, as data: downloaded by user approval to `%TEMP%\hg-fpu-vectors\` (not in the repository); the named operand values (CF_*) were read from the suite's `shared.h`. No code was copied. `tools/fpu_vector_check.cpp` (target `fpu_vector_check <dir>`) evaluates `hg::Fpu` on every vector. Findings: add/sub match all 36 each; sqrt matches after giving +0 for exponent-zero inputs (hardware sqrt(-0) = +0; changed); MUL/MADD/MSUB differ on 3 vectors each (1.0 x 3FFFFFFFh = 3FFFFFFEh on hardware while 3FFFFFFFh x 1.0 is exact: the multiplier is inexact and operand-order dependent); DIV differs on 1 (1.0 / 3FFFFFFFh = 3F000001h). A truncated-column radix-4 Booth model (ft recoded, partial-product columns below T not computed) fits all 36 MUL vectors for every T in 1..22, so the vectors do not determine the hardware rule; the exact-chop product stays until more hardware data exists. VU arithmetic is not covered by these vectors.

## 2026-10-24 Real-PS2 EE FPU / VU0 capture (user's console)

`tools/hwtest/fpu_capture.c` (our program, ps2sdk `ps2dev/ps2dev:v2.0.0`) run by the user on their own PS2; result `RESULTS.BIN` (786,432 bytes, 16,384 cases x 12 words; kept out of version control, copy in `%TEMP%\hg-fpu-vectors\RESULTS-hw.BIN`). Findings (`tools/hwtest/analyze.py`, `fpu_vector_check <vectors> RESULTS.BIN`):
- ADD.S/SUB.S: the aligned smaller operand keeps exactly one guard bit, then the result is chopped: 16,384/16,384 adds and subtracts reproduced (0 or 2+ guard bits fail). Implemented as `Fpu::add_cop1` for COP1 ADD/SUB/ADDA and the MADD/MSUB add step.
- MADD.S = the hardware MUL product then that add: 16,384/16,384.
- MUL.S: exact chop or one unit lower (1,721 of 16,384 lower; operand order changes the result in 3,413). VU0 VMULx.x equals the FPU MUL on all cases. Searched truncated Booth radix-4 designs (per-row column truncation, one's/two's-complement rows, correction row) and carry-save trees (linear, Wallace) with a dropped low-part carry: best leaves 265/32,768 mismatches; the exact rule is still open.
- DIV.S: exact chop or one unit higher (6,560 higher); not a clean remainder threshold (SRT-like). SQRT.S: our nearest result or one lower (2,443). Rules still open.
- PCSX2 (default settings) matches the hardware on ADD/SUB exactly but uses exact-chop MUL and nearest SQRT.

## 2026-10-25 Real-PS2 VU0 capture (user's console)

`tools/hwtest/vu0_capture.c` run by the user; `VU0.BIN` (1,048,576 bytes, 16,384 cases x 16 words; not in version control, copy `%TEMP%\hg-fpu-vectors\VU0-hw.BIN`). VU0 macro VADD/VSUB follow the same one-guard-bit add as the FPU (16,384/16,384 each); VMADD = VU0 product + that add (16,384/16,384); VU0 VMUL equals the FPU MUL bit for bit; VU0 Q after VDIV equals FPU DIV.S and Q after VRSQRT equals FPU RSQRT.S on every case. So the EE FPU and VU0 share one arithmetic; `Fpu::add` (used by both, and by VU1, whose FMAC is the same design - inferred, not measured) now implements the measured add, including the AVX2 packed path. The AVX-512 embedded-rounding MADD experiment (host IEEE adds) is retired. Divider: exact chop or +1; corrected non-restoring division reproduces exact chop and the uncorrected variant does not match (7,434/16,384 off); an SRT-style digit-selection model is the next candidate.

Divider structure (2026-10-25, both files, 32,768 DIV results): the unit forms an inner quotient of the *unnormalized* significands, Q25 ~ ma*2^24/mb (last digit weight 2^-24), equal to floor or floor+1, and the result is Q25 truncated: when ma >= mb one bit is dropped, when ma < mb none. Evidence: with ma >= mb a +1 appears only when the dropped bit is 1 (0 of 11,000+ cases otherwise); with ma < mb it appears at any remainder; exact quotients and power-of-two divisors are always exact. So the inner unit's final remainder (in units of 2^-24, divisor scaled to [1/2,1)) lies roughly in [-max(1/2, D-1/4), D-1/8]. SQRT follows the same pattern: +1 over chop only when the next root bit is 1 (one extra bit, then truncate). The digit rule is still open. Not matching: radix-2 SRT with constant or per-divisor thresholds (exact remainder or carry-save estimate, 1-6 fraction bits, restoring first digit, extra leading steps, overlapped stages, dropped carry-in), estimate-based final correction, radix-4 {-3..3} with exact remainder, and reciprocal-times-dividend (per-divisor violations 0.2-0.5 unit). Best radix-2 carry-save fit: ~90% of observable cases; failures are always hardware +1 on dividends with random low bits. A structured capture (fixed divisors, consecutive dividends) is the planned way to pin it down. PCSX2 oracle rounding, measured with our capture programs run in it (dry runs, `%TEMP%\hg-fpu-vectors\*-pcsx2.BIN`): COP1 DIV.S rounds to nearest (16,384/16,384 and 26,624/26,624 structured), COP1 SQRT.S to nearest, MUL.S chops; VU0 Q after VDIV and VSQRT chop (16,384/16,384 each). PCSX2's VU0 VADD/VSUB/VMADD take the exact sum and chop (16,384/16,384 each), unlike the console's one-guard-bit adder (they differ in ~27% of adds); PCSX2's COP1 add does match the console. This is the remaining slot-07 drift source: Hewie's path spline input (PC 0x1a92e8 region, frame 85) comes from VU0 vector code. Opt-in HG-DIAG-126 reproduces PCSX2's VU0 add for oracle comparisons only. On the console FPU and VU0 give identical results, so PCSX2's FPU/VU split is an emulator setting, not hardware. Against the 32,768 hardware DIV results, nearest matches 85% (4,865 off) and chop 60% (13,227 off); COP1 DIV.S therefore now rounds to nearest (closest available model, equal to the oracle), VU DIV/SQRT keep chop (equal to the oracle), until the exact rule is fitted.

RSQRT (VU0.BIN): the hardware result equals chop(a / hardware-SQRT(b)) or one above it on all 16,384 cases (+1 on 6,169), i.e. the divider applied to the root unit's output; it needs no separate rule once DIV and SQRT are fitted.

Third capture (planned, needs the user's console): `tools/hwtest/struct_capture.c` writes `mc0:/HGFPU/STRUCT.BIN` (1 MB, 58,794 records of op/a/b/result): DIV with 8 fixed divisors x 2,560 consecutive dividends and 3 fixed dividends x 2,048 consecutive divisors; SQRT over 12,288 consecutive radicands (both exponent parities); MUL with all-ones x sparse patterns, consecutive mantissas x all-ones, near-all-ones pairs and single-bit x consecutive; RSQRT over 4,096 consecutive inputs; then the SPU2 envelope probe (1,476 records, see "SPU2 sustain rate 0x7F" below; about 25 s, silent). Dry run in PCSX2 completes (user's slot-1 card backed up as `Mcd001.ps2.before-struct-capture-20261025.bak` and restored byte for byte).


## 2026-10-25 SPU2 sustain rate 0x7F (open)

Haunting Ground's SNDDRV voices use ADSR2 0x5FEF/0x5FCF/0x5FC0 (sustain: linear, decrease, shift 0x1F, step 3 = rate 0x7F). psx-spx "Envelope Operation" (PS1 SPU, hardware tests): a step value of all ones (rate 0x7F) never steps. `spu2_voices.hpp` follows that; the PCSX2 oracle instead lets the level fall slowly (SNDDRV ENVX copies 0x7FE1/0x7FD2 where ours stay 0x7FFF). Whether the PS2 SPU2 matches the PS1 here is unmeasured. Probe added to the third capture (user's choice: one trip): `tools/hwtest/spu2probe/spu2probe.c`, our IOP module run with the console ROM's LIBSD, keys a silent looping voice (core 0 voice 0, volume 0) with ADSR1 0x80FF and nine ADSR2 values (the game's 0x5FEF/0x5FCF/0x5FC0, rates 0x7E/0x7D/0x7B/0x77, exponential 0x7F, increase 0x7F) and records ENVX every ~20 ms for 2 s, then every ~2 ms for 128 ms after key-off; results come back by SIF DMA into STRUCT.BIN (ops 5/6). PCSX2 dry run: rate 0x7F decreasing goes 0x7FFF -> 0x7FFA (1 s) -> 0x7FF5 (2 s), rates 0x7B/0x77 identical to 0x7F; psx-spx predicts no change at 0x7F. Release at the game's linear/exponential shift 0x0F: 0x7FF0 -> 0x73C8 after 129 ms.

## 2026-10-25 Third real-PS2 capture (STRUCT.BIN, user's console)

`tools/hwtest/struct_capture.c` + `spu2probe/` run by the user; `STRUCT.BIN` (1 MB; not in version control, copy `%TEMP%\hg-fpu-vectors\STRUCT-hw.BIN`); summary via `analyze_struct.py`.

SPU2 envelope (op 5/6, ADSR1 0x80FF): (1) shift 1Fh never steps for steps 1..3: rates 7Dh/7Eh/7Fh, linear or exponential decrease, held 7FF7h for 2 s (psx-spx's rule extended to all of shift 1Fh; PCSX2's slow decay is wrong). (2) Shifts 1Dh/1Eh with step 3 decrease by 5 every 683 ms = 8000h Ts (increment 1), as modelled. (3) The level after attack + decay with sustain level Fh is 7FF7h, not 7FFFh: decay takes one step (-8 at decay shift Fh) before the sustain check, so the check follows an applied step. (4) A linear release at shift 0 reads 3FF7h, then FFF7h (negative) for one sample, then 0 (voice off). (5) Exponential release at shift Fh: 7FF7h -> 7FC7h -> 7F8Fh -> 7F5Fh at ~2 ms spacing, matching the model. `spu2_voices.hpp` now implements (1), (3), (4); `spu2_voice_tests` covers them.

MUL (15,786 structured + 81,920 random results): 1.0 x all-ones is one low while all-ones x 1.0 is exact; (1+2^j) x (0xFFFF80+k) errs only for j >= 17 at k multiples of 2^(24-j). Best structure so far: radix-4 Booth rows of the second operand with ones'-complement negatives and their +1s, reduced by a carry-save tree, high part added from column 17 up with the low columns' carry discarded: separate correction row with 4:2 compressor groups (or hot ones in the next row) = 692 of 97,706 off (99.3%; all 15,786 structured cases exact); Wallace 3:2 grouping 758 off; plain truncated-array models >11,000 off. The remaining misses depend on the tree wiring: moving the correction row, hot ones in the next row, reversed row order and pre-tree column truncation all plateau at ~0.7%. 649 of the 692 are model -1 / hardware exact, e.g. 0x800000 x 0x802000 (exact product): the console keeps the carry out of a negative row's low run of ones plus its +1, which this model drops, so the low end of the real tree is arranged differently. Not adopted (chop stays, = PCSX2). Scripts: `%TEMP%`-side scratch `mulmodel.py` (self-contained loader/model).

DIV (26,624 structured): fixed-divisor sweeps over consecutive dividends confirm path dependence: for 0xA00000 near dividend 0x800000 only remainder fraction 0.8 rounds up (period 5), near 0xC66000 fractions 0.2..0.8 vary; for one divisor a 0.4 can round up while a 0.6 does not. Consistent with the 25-bit inner quotient and an SRT-like digit rule still unfitted. SQRT sweeps (12,288 consecutive radicands) show the same shape: a 25-bit inner root rounded up or not, then truncated (+1 only when the extra bit is 1; near radicand 1.0 with even exponent every such case rounds up, with odd exponents it varies), so DIV and SQRT likely share one digit-recurrence unit and one rule.
