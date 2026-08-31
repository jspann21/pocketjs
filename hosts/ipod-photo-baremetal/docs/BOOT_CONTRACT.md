# A1099 boot contract used by stage 1

The known RetailOS 5.1.2.1 firmware directory describes `soso` as loading at
`0x10000000` with entry offset zero. The image is therefore linked at zero but
initially executes through SDRAM's native PP5020 window.

Before the MMAP0 write, startup uses only:

- PC-relative branches and `adr`;
- PC-relative literal loads whose values are fixed MMIO/IRAM constants;
- fixed PortalPlayer registers;
- a temporary IRAM stack.

The remap sequence is copied to `0x40000000` and programs:

```text
MMAP0_LOGICAL  0xf000f000 = 0x00003e00
MMAP0_PHYSICAL 0xf000f004 = 0x10000f84
```

The first value creates a 32 MiB logical window at address zero. The second
maps the PP5020 SDRAM native base with read/write/data/code access flags. The
stub then branches to the zero-based `post_remap` symbol.

Stage 1 leaves the unified cache and all interrupts disabled. That is slower
but removes cache ownership, vector-remap and IRQ-dispatch variables from the
first LCD/input qualification pass.

After MMAP0 is live, stage 1 also recreates the read window used by shipping
firmware for the immutable boot-ROM/flash region:

```text
MMAP1_LOGICAL  0xf000f008 = 0x20003c00
MMAP1_PHYSICAL 0xf000f00c = 0x00003f84
```

The only current consumer is the hardware-revision word at `0x20002084`, used
to select panel type 0 on revision `0x00060000`. No write is issued through
that window.
