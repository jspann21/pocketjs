# Phase-0 A1099 hardware result

Date: 2026-08-31

## Device

```text
Model: iPod Photo/Color A1099, P98, M9829, 60 GB
Interface revision: 0x00060000
Storage: iFlash with 64 GB SD
Filesystem: Windows/MBR/FAT32
```

## Hardware-executed image

The operator installed and executed this reversible FAT32 handoff image:

```text
File: pocketjs-a1099-probe-type0-handoff.ipod
Wrapper bytes: 5,360
Wrapper SHA-256: 652f4c86030a02f010603a015fb78bd18f3cbbd657e8313dd365cef1f45af141
Payload bytes: 5,352
Payload SHA-256: 24f575204aff295f726f0cfae64429c2de7403754fbb579b48b851dba78015bb
```

The operator reported success for all requested Phase-0 checks:

- normal loader handoff;
- backlight and visible framebuffer;
- moving heartbeat/liveness marker;
- Menu, Left, Select, Right, and Play indicators;
- wheel clockwise, counter-clockwise, position, and touch state;
- Hold indication and reboot-chord suppression;
- Select pattern changes;
- Menu+Play hardware reset;
- Select+Play boot-ROM disk mode.

No Rockbox runtime, plugin ABI, scheduler, filesystem service, display service,
or driver was called after the loader transferred control.

## Exact CI reproduction

GitHub Actions run `33437984940` built commit
`d7f49ca0dd1a7c9f6a34471cd767029b5dba28b6` with the official Swift 6.2.1
LLVM 17.0.0 toolchain commit
`10999b6d034fe318f3d56c83bddb6572593a8bb0`.

Artifact:

```text
Name: pocketjs-a1099-phase0-d7f49ca0dd1a7c9f6a34471cd767029b5dba28b6
Artifact ID: 9775138408
Artifact ZIP SHA-256: b84112b48a3c09862b4d16ceafcfa54b6be809f81006453fe51265b0fb8c7b43
```

The CI-produced wrapper and payload are byte-for-byte identical to the
hardware-executed image above. The exact-hash gate, image verifier, host tests,
automatic-panel build, direct-image compile gate, and alternate-panel compile
matrix all passed.

## Restoration state still required on the device

The most recent pasted `handoff.py status` still showed the probe active:

```text
active target SHA-256: 652f4c86030a02f010603a015fb78bd18f3cbbd657e8313dd365cef1f45af141
backup SHA-256: efdb430638b8b73c26ed8fb40208bdf5675144e6df54ce6ebcafd589ffb8a4b2
transaction state: version 2, populated
```

Therefore the final restoration is not yet evidenced by status output. Before
any Phase-1 candidate is installed, run `handoff.py restore`, then require:

```text
target SHA-256: efdb430638b8b73c26ed8fb40208bdf5675144e6df54ce6ebcafd589ffb8a4b2
backupExists: false
state: null
staleStateFiles: []
```

Confirm that the original Rockbox image boots normally and append that clean
result here before another device experiment.

## Qualification boundary

This result qualifies the exact Phase-0 bytes and the source/toolchain path
that reproduces them. It does not qualify direct OSOS installation, cold LCD
ownership, interrupts, cache enablement, charging, storage, shutdown, QuickJS,
or the complete PocketJS runtime. Those remain explicit later gates.
