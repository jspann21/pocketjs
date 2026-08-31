# Stage-one hardware test sheet

This image is untested and must remain reversible.

## Before boot

Record:

```text
Device: A1099 / M9829 60 GB
Storage: iFlash + 64 GB SD
Format: Windows/MBR/FAT32
Firmware backup path:
Firmware backup SHA-256:
Current known-good .ipod SHA-256:
Bring-up .ipod SHA-256:
Select + Play disk mode confirmed: yes/no
```

Keep the known-good boot file and firmware-partition backup off-device. Do not
write stage one directly into the only OSOS slot.

## Expected screen

1. Red, green, blue bands are in that order.
2. The full white outer frame is visible on all four edges.
3. The black crosshair is centered.
4. The lower-right liveness box toggles.
5. Menu/Left/Select/Right/Play each light exactly one top box.
6. Clockwise wheel movement flashes yellow; counter-clockwise flashes cyan.
7. Hold makes the lower-left box orange and suppresses the reboot chord.
8. Menu + Play for two seconds reboots.

## Report back

Photograph the screen and record:

```text
Boot method:
Boot success/failure:
Panel bits (two lower-center boxes, left=bit0, right=bit1):
Color order:
Orientation/mirroring:
Missing rows/columns:
Each button:
Wheel CW/CCW:
Hold:
Reboot chord:
Number of repeated boots:
Unexpected disk/USB/battery behavior:
```

A failed or blank display is not permission to try direct OSOS installation.
Return to the known-good image through the confirmed recovery path.
