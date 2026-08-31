# Hardware gates before direct OSOS installation

## Stage 1 — reversible bring-up

- Verify a complete firmware-partition backup and SHA-256 off-device.
- Confirm Select + Play disk mode before changing any boot artifact.
- Use a reversible alternate-image path; do not make this the only OSOS.
- Verify the `ipco` SHA-256 against `build/SHA256SUMS.txt` after copying.
- Cold/warm boot repeatedly and record the detected panel strap value.
- Validate RGB ordering, all four edges, buttons, wheel directions, and Hold.
- Confirm Menu + Play held for two seconds reboots.
- Test each known panel family only through a reversible loader.
- Do not test fault-injection builds until a debugger or persistent evidence
  reader exists.

## Stage 2 — complete A1099 board support

Required before direct OSOS replacement:

- cold LCD wake/sleep independent of inherited loader state;
- timer IRQ and bounded scheduler;
- cache enable/clean/invalidate qualification;
- PCF50605 battery, charging, USB-power, and low-voltage state machine;
- disk-safe shutdown and reboot-to-disk-mode path;
- ATA PIO, partition discovery, and FAT32 read-only support;
- embedded recovery `.pocket` package;
- pending -> active -> last-good -> embedded failure lineage;
- independent installer with backup, bounds checks, aligned writes, complete
  read-back comparison, and restore;
- repeated recovery tests without losing boot-ROM disk mode.

## Stage 3 — PocketJS runtime

- bounded native allocator with image/stack/framebuffer guards;
- target-thinned package admission;
- QuickJS with explicit heap and stack limits;
- retained `pocketjs-core` and RGB565 damage rendering;
- fixed 60 Hz simulation with independently paced LCD presentation;
- JS/native fault recovery to the embedded package;
- capabilities advertised only after the whole observable contract passes.
