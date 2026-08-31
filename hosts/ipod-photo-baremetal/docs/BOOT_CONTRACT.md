# Standalone A1099 boot contract

## Handoff assumed by the image

1. The image payload is loaded at native SDRAM address `0x10000000`.
2. Execution begins at payload offset zero in ARM state.
3. MMU/cache assumptions are treated conservatively: interrupts are masked and
   cache is explicitly disabled before remapping.
4. Either core may reach the vector table. CPU ID `0x55` continues; COP ID
   `0xAA` requests sleep and parks.
5. The CPU refuses to remap if the COP sleep-status bit does not appear within
   a bounded loop.

## Remap sequence

A position-independent stub is copied to IRAM `0x40000000`. It writes:

```text
MMAP0_LOGICAL  0xF000F000 = 0x00003E00
MMAP0_PHYSICAL 0xF000F004 = 0x10000F84
```

The stub then loads the linked low address of `post_remap` from its own copied
literal and transfers control there.

## Post-remap ownership

- Separate abort, undefined, FIQ, IRQ, panic, and supervisor stacks are placed
  in the 96 KiB firmware-usable IRAM window.
- `.bss` is cleared; `.noinit` is not.
- IRQ/FIQ remain masked.
- Cache remains disabled.
- The C entry point owns only LCD, PWM/backlight, click-wheel polling, and the
  system-reset bit.

Any exception whose LR still lies outside low linked memory bypasses C and
requests a fixed-MMIO hardware reset. Post-remap exceptions record cause, LR,
SPSR, phase, and timer state before rebooting.
