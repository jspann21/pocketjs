# Phase 1 confined persistence gate

This batch adds the first writable storage boundary for the iPod Photo host.
**It does not add a general writable filesystem.** Firmware resolves two exact,
installer-created 512-byte files and writes only the first data sector already
allocated to each file:

- `/POCKETJS/STATE0.BIN`
- `/POCKETJS/STATE1.BIN`

FAT tables, directories, allocation chains, package files, and firmware files
remain read-only. Resolution rejects missing files, wrong sizes, invalid data
clusters, non-terminating one-cluster chains, directory-cluster aliases, and
slots that resolve to the same LBA. The installer and gate assume the mounted
FAT32 volume has no pre-existing cross-linked files.

Each slot contains a versioned record with generation, payload, commit marker,
and CRC-32. A normal update writes and flushes an uncommitted record to the
inactive slot, verifies it by reading the sector back, then writes, flushes,
and verifies the committed record. The previous slot remains unchanged. Boot
selects the newest committed record whose CRC is valid. The abort action stops
after the verified uncommitted write, providing a deterministic recovery gate
without claiming physical power-loss qualification.

ATA command completion requires ready with busy, data-request, device-fault,
and error all clear. Each complete state transaction has a five-second overall
deadline, so stacked command waits cannot leave the UI unresponsive for an
unbounded interval.

The target build keeps this behavior behind `PERSISTENCE_GATE=1`. The default
firmware build retains the prior read-only runtime behavior.

## One-install hardware gate

Use `phase1-persistence-simple`. Its PowerShell transaction backs up Rockbox,
`APP.PKT`, and the exact prior contents or absence of both state files. The
installer seeds slot 0 at generation 1 and slot 1 as uncommitted. The device
shows only one position-stable line:

```text
Boot 1:       PERS L0 G00001
Center:       PERS C1 G00002
Boot 2:       PERS L1 G00002
Play:         PERS A0 G00003
Boot 3:       PERS L1 G00002
```

`L` means a committed slot loaded, `C` means a committed update passed flush
and read-back verification, and `A` means the intentional uncommitted update
passed its write/read-back gate. `PERS E##` is a failure. Menu+Play performs
the already-qualified reboot between boots. No cable cycle or reinstall is
needed until the final verified restore.

This gate does not expose persistence to JavaScript, promote packages, mutate
FAT metadata, or qualify arbitrary app files, low-battery writes, suspend, or
physical power interruption.

## Qualification

The exact candidate was physically qualified with all five expected lines.
The committed update survived reboot, the intentionally uncommitted newer
record was rejected on the following boot, the verified restore removed the
transaction state, and Rockbox booted normally afterward.
