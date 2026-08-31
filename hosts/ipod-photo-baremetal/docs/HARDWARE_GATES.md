# A1099 hardware gates

## Phase 0 — reversible board probe

The Phase-0 source is mergeable as an explicitly experimental board-support
milestone. Merging it does not qualify direct OSOS installation.

Before calling a particular build hardware-qualified:

- download the artifact produced from the commit being tested;
- record its commit SHA, byte length, and SHA-256;
- retain a complete firmware-partition backup and the current working
  `rockbox.ipod` off-device;
- confirm Select + Play boot-ROM disk mode before staging anything;
- install only through `tools/handoff.py`;
- verify RGB order/orientation and all four display edges;
- verify Menu, Left, Select, Right, Play, wheel CW/CCW, and Hold;
- verify heartbeat, pattern switching, and Menu + Play reset;
- repeat cold/warm boots and restore the original file successfully;
- record any disk, USB, battery, or power anomaly and stop testing on one.

The 5,360-byte image with SHA-256
`652f4c86030a02f010603a015fb78bd18f3cbbd657e8313dd365cef1f45af141`
is the known-good reference build. A different CI hash must be treated as a new
candidate and tested as such.

## Phase 1 — complete standalone board support

Required before direct OSOS replacement:

- cold LCD wake/sleep independent of inherited loader state;
- timer IRQ and bounded scheduler;
- qualified cache enable/clean/invalidate ownership;
- PCF50605 battery, charging, USB-power, and low-voltage state machine;
- disk-safe shutdown and reboot-to-disk-mode path;
- ATA PIO, partition discovery, and FAT32 read-only support;
- embedded recovery package and last-known-good lineage;
- an independent firmware installer with bounds checks, aligned writes,
  complete read-back verification, and restore testing.

## Phase 2 — PocketJS runtime

- bounded allocator with image/stack/framebuffer guards;
- target-thinned `.pocket` admission;
- QuickJS with explicit heap and stack limits;
- retained `pocketjs-core` and RGB565 damage rendering;
- fixed-step simulation with independently paced LCD presentation;
- native/JS fault recovery to an embedded package;
- capabilities advertised only after their observable contracts pass.
