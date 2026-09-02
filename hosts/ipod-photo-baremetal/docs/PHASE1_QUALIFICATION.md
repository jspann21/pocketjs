# iPod Photo A1099 Phase 1 hardware qualification

Status: hardware-qualified on a physical iPod Photo/Color A1099 for the scope listed below.

Qualification date: 2026-09-01
Boot path: reversible Rockbox bootloader `/rockbox.ipod` handoff only.

## Original fully qualified runtime candidate

- File: `pocketjs-a1099-phase1-runtime-hold.ipod`
- SHA-256: `c9a1d513ad2e440c054ceb2f116d26e334c5737071c8d5e87460cfc9f8251152`
- Valid disk guest: `/POCKETJS/APP.PKT`
- APP.PKT SHA-256: `0a85c16910a804faf0b5a1d486eea1b70a18e749dedd539a693bce8962781074`

This qualification applies only to the exact candidate and package hashes above.
Every newly built image—including the direct-build workflow artifact—starts as
unqualified until its own static report and physical result are captured. Use
`PHASE1_BATCH_TEST.md` to exercise the complete gate in one reversible staging
session and retain `PHASE1_BATCH_EVIDENCE.json` with the tested artifact.

## Expanded HostOps qualification

- File: `pocketjs-a1099-phase1-hostops-hold2.ipod`
- SHA-256: `6babe891ac53f09a0a3c71da1e14849713f6bc60136bad73571862e4bdcdb3f7`
- APP.PKT SHA-256: `fc5cc221f6c6c1a951d3657eabf832ec5c0b906c703d5b73dfac0a0cfe3ff579`
- Original Rockbox restored SHA-256: `e1735b38b0c261a3c0bb65f513568ebc608cb1b44fc9da092b398d37f0907cbd`

This exact candidate physically qualified typed-array property batching,
PSM_8888 texture upload and image binding, explicit core animation with
cancellation/restart, focus/active HostOps calls, and a 250 ms bounded QuickJS
frame watchdog. The runtime chip remained cyan through five Hold transitions.
Input and heartbeat remained prompt, the checker rendered correctly, both
animation directions completed, and no partial-update artifacts appeared.

The animation was visibly slow/choppy and the performance chip briefly reached
orange on a Hold edge. Neither event produced a runtime fault, dropped-tick
report, stopped heartbeat, input delay, or rendering artifact. The exact
Rockbox backup was restored and no transaction state remained.

The corrupt-package/fallback matrix was not repeated for this later artifact;
that part of the qualification remains attached to the original candidate
above until a future combined campaign exercises it again.

## Ordinary framework app qualification

- File: `pocketjs-a1099-phase1-standard-app2.ipod`
- SHA-256: `3695faaab21e2632a20dcb1305b92fb0a7f4007cc4106f185bd509e5560395d4`
- APP.PKT SHA-256: `d2a412b62f62ba7ce64ee65aa62ff7abfe700259ee0df66da6a9daa759d89d1b`
- Embedded fallback package SHA-256: `d2a412b62f62ba7ce64ee65aa62ff7abfe700259ee0df66da6a9daa759d89d1b`
- Original Rockbox restored SHA-256: `e1735b38b0c261a3c0bb65f513568ebc608cb1b44fc9da092b398d37f0907cbd`

This exact candidate physically qualified the canonical `ipod-photo` target,
the ordinary `pocket build --target ipod-photo` compiler/packer path, a
generated Solid application, baked styles and font atlases, the standard
`frame(buttons, analog?, touches?, hits?, touchSurfaces?)` callback, portable
button translation, initial retained rendering, and reactive text replacement.
The labelled app rendered correctly; center Select and wheel input changed
their respective `WAIT` labels to `OK`; the screen stayed readable without
observed artifacts.

The staged disk package and embedded fallback were intentionally byte-exact,
so this observation does not distinguish which source supplied the guest.
Large multi-cluster FAT32 package loading therefore remains a separate storage
gate. The installation record verified both staged hashes, and restoration
verified the original Rockbox hash with no backup, transaction, or stale-state
files remaining.

## Ordinary app disk-source qualification

- File: `pocketjs-a1099-phase1-storage-source2.ipod`
- SHA-256: `25e7f8767c1b5118eadd2bcb148f6b03d6a5a2842bed46d8beb1774a1c771577`
- Disk APP.PKT SHA-256: `d2a412b62f62ba7ce64ee65aa62ff7abfe700259ee0df66da6a9daa759d89d1b`
- Embedded fallback SHA-256: `d2a412b62f62ba7ce64ee65aa62ff7abfe700259ee0df66da6a9daa759d89d1b`
- On-device boot-source evidence: `DISK R00376`
- Original Rockbox restored SHA-256: `e1735b38b0c261a3c0bb65f513568ebc608cb1b44fc9da092b398d37f0907cbd`

This exact candidate closes the source-selection caveat above. The ordinary
app rendered and remained responsive, while the native source strip reported
`DISK` and 376 sector reads. This physically qualifies the large,
multi-cluster FAT32 load of the 174,792-byte package rather than relying on the
byte-identical embedded fallback. The first source-strip candidate drew its
panel but clipped the label above the screen; this candidate uses the 15-pixel
slot-0 glyph cell fully on-screen. Installation and restoration records verify
the candidate, disk package, original Rockbox hash, and absence of remaining
handoff state.

## Package-lifecycle qualification

- File: `pocketjs-a1099-phase1-package-lifecycle.ipod`
- SHA-256: `a55eeb41b3a9afb35b525aa1c5cfbf5ad015d1b9136ea724a32c57b85f963702`
- Valid active package SHA-256: `d2a412b62f62ba7ce64ee65aa62ff7abfe700259ee0df66da6a9daa759d89d1b`
- Rejected pending package SHA-256: `eca8936f299161b3668622247e740bae0de93ddd6dfcf432525e6404cf1df630`
- Active-path evidence: `ACTV P02 R00756`
- Embedded-fallback evidence: `EMBD P02 R00420`
- Original Rockbox restored SHA-256: `e1735b38b0c261a3c0bb65f513568ebc608cb1b44fc9da092b398d37f0907cbd`

This candidate adds the read-only `PENDING.PKT` → `ACTIVE.PKT` →
`LASTGOOD.PKT` → `APP.PKT` → embedded selection order. QuickJS boot and the
first guest frame must succeed before a disk slot is accepted. One firmware
installation physically qualified rejection of the corrupt pending package,
selection of the valid active package, and embedded fallback after the active
package was removed. The ordinary app rendered in both boots; center and wheel
worked during the first boot. The exact original Rockbox image and prior
package-slot state were restored, with no backup, handoff state, stale state,
or package transaction remaining.

## Multi-app launcher qualification

- File: `pocketjs-a1099-phase1-multi-app.ipod`
- SHA-256: `de5e44d771b10a4d0d7b2d8d11411d428a5ed90ff0b7632de7d2d5d13653323b`
- Launcher package SHA-256: `8a114f182fed434faa2838a1d2bad2bdd28dd4c61045c0ef560d784ef7b5a42e`
- ALPHA.PKT SHA-256: `d489c03fdfbbdffc2e15922bdd185ce8b07d7722061a8516392162c151664735`
- BETA.PKT SHA-256: `de70763dab50c79381e38c3ead29fe2361584a33a157745038f7ff31a85fe7a9`
- Expected Alpha evidence: `APP 1/2 R#####`
- Expected Beta evidence: `APP 2/2 R#####`
- Observed Alpha evidence: `APP 1/2 R00847`
- Observed Beta evidence: `APP 2/2 R00847`
- Original Rockbox restored SHA-256: `e1735b38b0c261a3c0bb65f513568ebc608cb1b44fc9da092b398d37f0907cbd`

This candidate adds bounded read-only discovery under `/POCKETJS/APPS`,
deterministic filename sorting, a pinned packaged launcher, and selection of
two distinct ordinary framework apps from one installation. The physical gate
used two boots separated by Menu+Play reboot, without reconnecting or staging
different files between boots. The launcher listed both apps, wheel selection
opened Beta, and both distinct packages launched with the expected ordinal and
identical 847-sector read counts. Both launches were observed to be somewhat
slow, but they completed. The restore record verifies the exact original
Rockbox hash, no remaining backup or handoff state, and no remaining multi-app
transaction.

## Qualified scope

- Rockbox bootloader handoff and standalone ARMv4T startup
- LCD, backlight, five buttons, click wheel, wheel touch, Hold and Menu+Play reset
- Timer1 IRQ, fixed-step scheduler and responsive input polling
- Rust `pocketjs-core` backend and retained UI renderer
- Damage tracking and partial LCD updates without observed corruption
- PP5020 cache ownership/self-test
- Read-only PCF50605 battery ADC plus USB/charging telemetry
- QuickJS runtime and generic PocketJS HostOps bridge
- Target-admitted `ipod-photo` `.pocket` package execution
- Read-only PIO ATA + FAT32 loading of `/POCKETJS/APP.PKT`
- Multi-cluster ordinary-app disk loading with explicit source/read evidence
- Invalid/corrupt disk package rejection with embedded recovery `.pocket` fallback
- Read-only pending, active, last-good, legacy, and embedded package selection
- Bounded multi-app discovery, packaged launcher, and two-app selection
- Runtime Hold transition with no QuickJS fault or scheduler drop

## Observed performance

Input and heartbeat remained prompt. The diagnostic performance chip commonly remained yellow and briefly reached orange during a Hold transition. No red scheduler-drop indication was observed in the final candidate. This is an optimization target, not a correctness failure.

## Still outside this qualification

- ATA/FAT writes and filesystem mutation
- ATA power/sleep ownership changes
- audio output and codec control
- USB device stack
- charger-control writes
- automatic low-battery shutdown / PMU standby writes
- direct firmware-partition/OSOS installation

This branch contains normal source files. Building it does not require the historical Phase 1 reconstruction patch stack.
