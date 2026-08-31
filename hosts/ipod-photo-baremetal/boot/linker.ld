OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY {
    SDRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 32M
}

SECTIONS {
    .text : ALIGN(4) {
        KEEP(*(.text.startup))
        *(.text*)
        *(.rodata*)
        *(.glue_7)
        *(.glue_7t)
        . = ALIGN(4);
    } > SDRAM

    .data : ALIGN(4) {
        *(.data*)
        . = ALIGN(4);
    } > SDRAM

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > SDRAM

    .noinit (NOLOAD) : ALIGN(16) {
        __noinit_start = .;
        *(.noinit*)
        . = ALIGN(16);
        __noinit_end = .;
    } > SDRAM

    __image_end = .;

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.eh_frame*)
        *(.note*)
    }
}

/* PP5020 exposes 96 KiB of firmware-usable IRAM at 0x40000000. */
_remap_stub_base = 0x40000000;
_und_stack_top   = 0x40000600;
_abt_stack_top   = 0x40000800;
_fiq_stack_top   = 0x40000a00;
_panic_stack_top = 0x40001200;
_stack_limit     = 0x40002000;
_irq_stack_top   = 0x40018000;
_stack_top       = 0x40017c00;

ASSERT(_stack_top > _stack_limit, "supervisor stack is empty")
ASSERT(__image_end < 0x01f00000, "image leaves no bounded heap/work area")
ASSERT(SIZEOF(.text) + SIZEOF(.data) < 0x00800000,
       "loaded image unexpectedly exceeds 8 MiB")
