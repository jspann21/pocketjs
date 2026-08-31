.syntax unified
.cpu arm7tdmi
.arm

/* This image is linked at 0 and may be entered through SDRAM's native
 * 0x10000000 alias. Everything before post_remap is PC-relative or fixed MMIO. */

.section .text.vectors, "ax", %progbits
.global _start
.type _start, %function
_start:
    b reset_handler
    b vector_undef
    b vector_swi
    b vector_prefetch_abort
    b vector_data_abort
    b vector_reserved
    b vector_irq
    b vector_fiq
.size _start, . - _start

#if PJS_BOOTLOADER_HANDOFF
/* Compatibility tag checked by the installed iPod bootloader before it hands
 * over a /rockbox.ipod body. It is data only; no Rockbox runtime is linked. */
.section .text.handoff, "ax", %progbits
.global pjs_handoff_signature
pjs_handoff_signature:
    .ascii "Rockbox"
    .byte 1
    .space 216
#endif

.section .text.startup, "ax", %progbits
.global reset_handler
.type reset_handler, %function
reset_handler:
    msr cpsr_c, #0xd3

    /* The PP5020 presents 0x55 on the CPU and 0xaa on the COP. Only the CPU
     * owns startup; the COP is parked until a later, measured worker design. */
    mov r0, #0x60000000
    ldr r1, [r0]
    and r1, r1, #0xff
    cmp r1, #0x55
    beq cpu_start

    orr r0, r0, #0x7000
    mov r1, #0x80000000
    str r1, [r0, #4]
    nop
    nop
    nop
cop_park:
    b cop_park

cpu_start:
    /* Do not change SDRAM aliases while the second core could still fetch from
     * the native window. The COP path above requests sleep; wait for the
     * hardware sleep bit with a finite pre-remap budget and fail closed if it
     * never arrives. A chainloader that started only the CPU must already have
     * parked the COP for this invariant to pass. */
    mov r0, #0x60000000
    orr r0, r0, #0x00007000
    mov r2, #0x00100000
9:
    ldr r1, [r0, #4]
    tst r1, #0x80000000
    bne 8f
    subs r2, r2, #1
    bne 9b
    b pre_remap_hang
8:
    /* Temporary SVC stack in the top of the 96 KiB IRAM window. */
    ldr sp, =0x40017c00

    /* A chainloader may leave the PP5020 unified cache enabled. Clean the
     * complete cache before disabling it so a freshly loaded image cannot lose
     * dirty data when SDRAM aliases change. The Apple boot ROM enters with the
     * cache off, so direct boot takes the short path. Every wait is bounded. */
    mov r0, #0x60000000
    orr r0, r0, #0x0000c000       /* CACHE_CTL = 0x6000c000 */
    ldr r1, [r0]
    tst r1, #0x1                  /* CACHE_CTL_ENABLE */
    beq cache_disable

    mov r3, #0xf0000000
    orr r3, r3, #0x0000f000
    orr r3, r3, #0x44             /* CACHE_OPERATION = 0xf000f044 */
    ldr r1, [r3]
    orr r1, r1, #0x2              /* CACHE_OP_FLUSH */
    str r1, [r3]
    mov r2, #0x00100000
cache_clean_wait:
    ldr r1, [r0]
    tst r1, #0x8000               /* CACHE_CTL_BUSY */
    beq cache_disable
    subs r2, r2, #1
    bne cache_clean_wait
    b pre_remap_hang

cache_disable:
    mov r1, #0
    str r1, [r0]
    nop
    nop
    nop
    nop

    /* A chainloader may already have established the low alias. */
    adr r0, reset_handler
    and r1, r0, #0xff000000
    cmp r1, #0
    beq post_remap
    cmp r1, #0x10000000
    bne pre_remap_hang

    adr r0, remap_stub_start
    adr r1, remap_stub_end
    mov r2, #0x40000000
1:
    cmp r0, r1
    ldrlo r3, [r0], #4
    strlo r3, [r2], #4
    blo 1b
    mov pc, #0x40000000

.align 2
remap_stub_start:
    mov r0, #0xf0000000
    orr r0, r0, #0x0000f000
    mov r1, #0x00003e00
    str r1, [r0]
    mov r1, #0x10000000
    orr r1, r1, #0x00000f00
    orr r1, r1, #0x84
    str r1, [r0, #4]
    ldr pc, remap_target
remap_target:
    .word post_remap
remap_stub_end:

pre_remap_hang:
    b pre_remap_hang

post_remap:
    msr cpsr_c, #0xd7
    ldr sp, =0x40002000
    msr cpsr_c, #0xdb
    ldr sp, =0x40004000
    msr cpsr_c, #0xd1
    ldr sp, =0x40006000
    msr cpsr_c, #0xd2
    ldr sp, =0x40008000
    msr cpsr_c, #0xd3
    ldr sp, =0x40017c00

    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
2:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 2b

    ldr sp, =__stack_top
    bl kernel_main
    mov r0, #0xff
    bl panic_code
3:
    b 3b
.size reset_handler, . - reset_handler

/* All non-reset exceptions are recorded and parked. IRQs remain masked in the
 * probe, so an IRQ/FIQ entry indicates unexpected inherited hardware state. */
.macro FAULT_VECTOR name, reason, adjust
.global \name
.type \name, %function
\name:
    sub lr, lr, #\adjust
    stmdb sp!, {r0-r12, lr}
    mov r0, #\reason
    mov r1, lr
    mrs r2, spsr
    mov r3, sp
    bl panic_fault
1:
    b 1b
.size \name, . - \name
.endm

FAULT_VECTOR vector_undef,          1, 4
FAULT_VECTOR vector_swi,            2, 4
FAULT_VECTOR vector_prefetch_abort, 3, 4
FAULT_VECTOR vector_data_abort,     4, 8
FAULT_VECTOR vector_reserved,       5, 4
FAULT_VECTOR vector_irq,            6, 4
FAULT_VECTOR vector_fiq,            7, 4
