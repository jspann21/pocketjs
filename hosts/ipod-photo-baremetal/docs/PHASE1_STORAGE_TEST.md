# iPod Photo Phase 1 read-only storage test

This candidate keeps the hardware-qualified cache, timer, input, LCD, QuickJS,
and read-only power telemetry paths. The new boundary is read-only PIO ATA plus
FAT32 loading of a PocketJS package from the user data partition.

The firmware never issues an ATA write command. It does not alter the firmware
partition, FAT, directory entries, or package file. Disk power transitions are
also left to the state inherited from the reversible Rockbox loader.

## Required FAT layout

Before installing the candidate, create this exact 8.3 path on the iPod data
volume:

    /POCKETJS/APP.PKT

`APP.PKT` is supplied with the test artifact. The short names are intentional so
the first FAT gate does not need long-filename parsing.

## Expected display

- Runtime chip cyan: `APP.PKT` was read from FAT32, admitted as an `ipod-photo`
  PocketJS package, evaluated by QuickJS, and its frame function is live.
- Runtime chip magenta: the embedded fallback guest is running instead; disk
  loading did not complete.
- Runtime chip red: package or QuickJS runtime failure.
- Cache chip remains green.
- Battery telemetry remains plausible.
- Guest lane, pulse, wheel marker, buttons, Hold, and reset behave exactly as in
  the qualified QuickJS test.

## Hardware result

Record runtime chip color, first-screen time, guest responsiveness, performance
chip, and any display corruption. A cyan runtime chip is the pass criterion for
this storage gate.
