# Reversible stage-one boot on the A1099

This procedure uses the already-installed iPod bootloader as a file loader
only. Rockbox is not part of the stage-one runtime. Nothing in the firmware
partition or OSOS image is modified.

The loader checks `/.rockbox/rockbox.ipod` first, then `/rockbox.ipod`. It reads
the standard eight-byte additive-checksum wrapper, verifies the payload, loads
that payload at native SDRAM address `0x10000000`, and transfers control there.
The `Rockbox\1` marker at payload offset `0x20` is used only when falling back
to an image embedded in OSOS; it is not required for a FAT32-loaded file.

## Required recovery facts

Before copying anything:

- retain the current working `rockbox.ipod` off-device;
- verify the complete firmware-partition backup off-device;
- verify Select + Play disk mode again;
- keep the iPod connected to external power for the first boot;
- do not alter `apple_os.ipod`, the firmware partition, or the bootloader.

The captured current working file has SHA-256:

```text
8dc29b572f0eeee69dfc9471fe6fae6beb2bf9ec35f15d6ffa3fb9b67e26f3d7
```

## Copy

With the iPod mounted as storage:

```sh
cd /path/to/ipod/.rockbox
cp rockbox.ipod rockbox.ipod.known-good
cp /path/to/pocketjs-a1099-bringup.ipod rockbox.ipod.new
sync
cmp rockbox.ipod.new /path/to/pocketjs-a1099-bringup.ipod
mv rockbox.ipod.new rockbox.ipod
sync
```

On systems where `sync` is not available, eject through the operating system
and wait for all writes to finish. The stage-one file SHA-256 must match the
`SHA256SUMS.txt` in the same CI artifact.

## Boot

1. Eject cleanly.
2. Leave Hold off and do not hold Menu or Play during boot.
3. Reset/reboot normally.
4. Observe the diagnostic screen and complete `HARDWARE_TEST.md`.
5. Menu + Play held for two seconds asks stage one to reboot.

## Recovery

At any blank screen or unexpected behavior:

1. Hold Menu + Select until the iPod resets.
2. Immediately hold Select + Play to enter Apple disk mode.
3. Mount the data partition.
4. Restore `.rockbox/rockbox.ipod.known-good` as `.rockbox/rockbox.ipod`.
5. Eject cleanly and boot normally.

The installed bootloader also selects Apple firmware when Hold was active or
Menu was held at boot, but disk mode is the preferred recovery route because
it permits restoring the file without executing stage one again.

## Forbidden during stage one

- no direct OSOS installation;
- no firmware-partition write;
- no deletion of the known-good file or off-device backups;
- no repeated test after unexplained power, disk, or USB behavior;
- no assumption that a valid checksum proves hardware safety.
