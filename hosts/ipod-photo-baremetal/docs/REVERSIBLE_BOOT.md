# Reversible Phase-0 boot

This procedure changes only the normal FAT32 file loaded by the already-
installed iPod bootloader. It does not modify the firmware partition, OSOS
directory, Apple firmware image, or bootloader.

## Preconditions

1. Keep a complete, hash-verified firmware-partition backup off-device.
2. Keep a separate copy of the current working `rockbox.ipod` off-device.
3. Confirm Select + Play enters boot-ROM disk mode.
4. Keep the first test on reliable external power.
5. Use the `.ipod` file from the exact CI commit being evaluated and record its
   SHA-256 before installation.

The handoff probe contains the compatibility tag expected by the installed
loader, which loads the wrapper body at `0x10000000` and jumps to offset zero.
After that jump no Rockbox runtime code or service is used.

## Install atomically

`--mount` may name the volume root or its `.rockbox` directory.

```sh
python3 hosts/ipod-photo-baremetal/tools/handoff.py install \
  --mount /path/to/IPOD \
  --probe /path/to/pocketjs-a1099-probe.ipod

python3 hosts/ipod-photo-baremetal/tools/handoff.py status \
  --mount /path/to/IPOD
```

The helper validates both wrappers, creates and verifies an exact backup,
records original/probe hashes, writes through a temporary file, flushes it,
renames atomically, and verifies the installed bytes. It also recovers the
known Windows interrupted-preinstall state only when the active and backup
hashes prove that the original image is unchanged.

Eject cleanly and reboot normally. Complete `HARDWARE_TEST.md`.

## Restore

At any blank screen, unexpected peripheral behavior, or after the planned test:

1. reset and enter Select + Play boot-ROM disk mode;
2. mount the data volume;
3. run:

```sh
python3 hosts/ipod-photo-baremetal/tools/handoff.py restore \
  --mount /path/to/IPOD
```

The helper refuses restoration unless the backup matches the recorded original
hash, verifies the replacement, and only then removes transaction state.

## Forbidden in Phase 0

- direct OSOS or firmware-partition installation;
- deleting the off-device backups;
- using the compile-only direct-image variant;
- repeated power cycling after an unexplained failure;
- treating a valid checksum or reference hash as proof of hardware safety.
