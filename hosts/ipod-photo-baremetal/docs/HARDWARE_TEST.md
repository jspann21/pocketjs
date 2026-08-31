# Phase-0 A1099 hardware test sheet

Test the exact CI artifact from the commit being evaluated. The previously
successful reference image is 5,360 bytes with SHA-256
`652f4c86030a02f010603a015fb78bd18f3cbbd657e8313dd365cef1f45af141`.
A candidate with any other hash is a new build and must be reported with its
actual hash.

## Before boot

```text
Device model/capacity:
Storage adapter/media:
Commit SHA:
Workflow run:
Artifact name:
Candidate .ipod bytes:
Candidate .ipod SHA-256:
Firmware backup path and SHA-256:
Current working rockbox.ipod backup path and SHA-256:
Select + Play disk mode confirmed: yes/no
handoff.py install and status succeeded: yes/no
```

Do not write the probe to OSOS. Keep the current working boot file and complete
firmware backup off-device.

## Expected display and controls

- framebuffer is stable and all four edges are visible;
- five bottom squares map to Menu, Left, Select, Right, and Play;
- the wheel marker follows absolute wheel position and indicates touch;
- the controller-status chip reports valid packets;
- the panel bits report type 0 on the P98/M9829 target;
- Hold produces the red center band and suppresses the reboot chord;
- Select cycles framebuffer patterns;
- the upper-right heartbeat moves;
- Menu + Play held for two seconds resets the SoC.

## Result

```text
Normal bootloader handoff occurred:
Backlight on:
Visible framebuffer:
RGB order/orientation/all edges:
Heartbeat:
Menu/Left/Select/Right/Play:
Wheel CW/CCW/touch:
Hold:
Select pattern change:
Menu+Play reset:
Cold boots completed:
Warm boots completed:
Unexpected disk/USB/battery/power behavior:
Select+Play disk-mode recovery:
handoff.py restore succeeded:
Restored rockbox.ipod SHA-256:
Photo/video link:
```

Stop after any unexplained storage, USB, battery, or power behavior. Restore the
original file before another experiment.
