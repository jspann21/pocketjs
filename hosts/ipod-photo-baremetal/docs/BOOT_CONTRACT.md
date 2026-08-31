# Boot contract and memory layout

## Entry forms

The source can build two related images:

1. **Bootloader handoff** — eight ARM vectors at offset zero, compatibility tag
   at `0x20`, and reset code at `0x100`. This is the only Phase-0 hardware-test
   artifact.
2. **Direct-image compile path** — vectors at offset zero and reset code at
   `0x20`, with no compatibility tag. It is continuously built but not approved
   for firmware-partition installation.

Either build can be entered through SDRAM's native `0x10000000` alias or an
already-established low alias. Before deciding, code uses only PC-relative
references, fixed IRAM addresses, and fixed MMIO addresses.

## Startup sequence

```text
vector[0] -> reset_handler
    mask IRQ/FIQ in CPSR
    identify CPU versus COP
    COP: request hardware sleep and park
    CPU: finitely confirm COP sleep bit
    temporary IRAM SVC stack
    if cache enabled: request whole-cache clean and finitely await completion
    disable unified cache
    detect native versus low SDRAM alias
    copy 40-byte remap stub to 0x40000000 when needed
    MMAP0 logical  = 0x00003E00
    MMAP0 physical = 0x10000F84
    jump to low post_remap
    establish separate ABT/UND/FIQ/IRQ stacks
    zero BSS
    switch to 64 KiB SDRAM main stack
    kernel_main
```

If the COP never confirms sleep, cache clean never completes, or the entry
alias is neither supported value, the CPU parks before touching MMAP0.

The currently installed bootloader also flushes its PP5020 cache before jumping
to a loaded `/rockbox.ipod`. The probe performs its own clean anyway so the
image is not coupled to that loader-specific guarantee.

## Link-time layout

```text
0x00000000  vectors
0x00000020  handoff tag (handoff build) or reset (direct build)
0x00000100  reset entry (handoff build)
0x0000018C  bounded cache-clean wait (type-0 handoff)
0x000001A4  cache disable (type-0 handoff)
0x000001F4  copied remap stub start (type-0 handoff)
             text / rodata / data
0x000014E8  __image_end / __bss_start (type-0 handoff)
             BSS: primary framebuffer + panic framebuffer + state
0x00027200  persistent .noinit crash record
0x00027250  64 KiB main stack bottom
0x00037250  __stack_top / __heap_start
             future bounded runtime heap
0x02000000  end of 32 MiB SDRAM
```

The type-0 handoff flat image is 5,352 bytes. Framebuffers, crash state, and
stack are `NOLOAD` allocations and do not inflate the firmware payload.

## Exception behavior

Every non-reset vector records:

- reason code;
- corrected fault PC;
- SPSR;
- r0-r12 and LR as visible in the exception mode.

If LCD initialization has completed, the record is presented as a red
diagnostic screen. The record resides in `.noinit` for later recovery work.
IRQs remain globally masked in Phase 0, so IRQ/FIQ entry indicates unexpected
inherited hardware state or a fault in the probe assumptions.
