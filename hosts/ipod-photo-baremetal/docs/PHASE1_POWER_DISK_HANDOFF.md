# Phase 1 native-kernel handoff campaign

This one-install campaign extends the qualified runtime and lineage path with
four bounded native-kernel actions. **It claims and resets the PP5020 LCD
device gate, filters power-source pins, proves a reversible panel sleep/wake,
and enters Apple disk mode through the PP5020 boot marker after an idle ATA
check.**

The candidate keeps one position-stable status strip. Expected states are:

```text
PWR BAT
LCD WAKE
SHDN READY
PWR USB
DISK GO
```

`KERN E##` is a refusal and the terminal reset does not occur.

## LCD boundary

`lcd_init()` now enables and pulses reset on `PP_DEV_LCD`, waits for the bridge
to become idle, and runs the already-qualified A1099 type-0 panel sequence.
It deliberately preserves the loader's shared `CLOCK_SOURCE` and
`CLCD_CLOCK_SRC` selections because no A1099-specific evidence supports
rewriting them while the CPU is live.

The center-button action turns the backlight off, sends Rockbox's type-0 sleep
sequence, waits 750 ms, repeats the full wake sequence, restores the entire
framebuffer, and only then turns the backlight on. Odd panel types fail closed.

## Power and shutdown preflight

The power-source filter requires three matching samples before changing its
stable state. USB, FireWire, and charging validity are tracked separately so
an unconfigured input cannot be reported as a stable absence. The campaign
uses the existing qualified telemetry initialization; the filter itself only
reads source pins.

The Play action is a dry run. It requires a live guest, valid running lineage,
intact memory guards, a ready LCD, a stable source state, and a bounded idle
ATA observation. It does not stop the runtime or write a PMU, charger, ATA
command, sector, FAT structure, or power-rail register.

## Terminal disk-mode order

With stable USB power present, holding Menu+Play for two seconds performs this
terminal sequence:

1. publish `DISK WAIT`;
2. commit the running lineage record as clean;
3. observe `PP_ATA_ALT_STATUS` for at most 250 ms and refuse on busy data,
   fault, unknown, or timeout states;
4. publish `DISK GO`, render it, and retain Rockbox's two-second handoff
   settling window without issuing ATA standby;
5. stop QuickJS, release the disk package, stop Timer1, and disable IRQs;
6. turn off the backlight and put the panel to sleep;
7. write Rockbox's exact PP5020 `diskmode\0\0hotstuff\0\0\1` marker at
   `0x40017f00` and reset.

The marker address and bytes match the local Rockbox PP5020 bootloader and USB
handoff implementations. PP5022 uses a different address and is not selected
by this A1099 build.

This gate does not issue ATA standby or flush commands, switch the disk rail,
request PMU standby, change charger policy, write packages or FAT metadata, or
install directly into OSOS. True power-off and independent shared-clock
selection remain unqualified.
