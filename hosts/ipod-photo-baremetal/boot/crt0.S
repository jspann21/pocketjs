    .syntax unified
    .section .text.startup, "ax", %progbits
    .arm
    .global _start
    .type _start, %function

/* The Apple loader enters at image offset zero while SDRAM is still visible
 * at 0x10000000. The reset branch and every instruction before remapping are
 * position-independent. */
_start:
    b reset_handler
    b undef_entry
    b swi_entry
    b prefetch_abort_entry
    b data_abort_entry
    b reserved_entry
    b irq_entry
    b fiq_entry

reset_handler:
    msr cpsr_c, #0xd3

    /* A defensive dual-core gate. CPU id is 0x55, COP id is 0xaa. */
    ldr r0, =0x60000000
    ldrb r1, [r0]
    cmp r1, #0xaa
    beq park_cop

    /* Request COP sleep from the CPU side as well, then wait for the status
     * bit with a bounded loop. This closes the race where CPU reaches MMAP0
     * while COP is still fetching reset code from the native SDRAM window. */
    ldr r0, =0x60007004
    mov r1, #0x80000000
    str r1, [r0]
    nop
    nop
    nop
    mov r2, #0x00100000
0:
    ldr r3, [r0]
    tst r3, r1
    bne 7f
    subs r2, r2, #1
    bne 0b
7:

    /* Mask all normal/high interrupts and disable the unified cache before
     * changing the SDRAM mapping. */
    mvn r1, #0
    ldr r0, =0x60004028
    str r1, [r0]
    ldr r0, =0x60004128
    str r1, [r0]
    ldr r0, =0x6000c000
    mov r1, #0
    str r1, [r0]

    ldr sp, =_stack_top

    /* Copy the remap sequence to IRAM, then execute it there. */
    adr r0, remap_stub
    adr r1, remap_stub_end
    ldr r2, =_remap_stub_base
1:
    cmp r0, r1
    ldrlo r3, [r0], #4
    strlo r3, [r2], #4
    blo 1b
    ldr pc, =_remap_stub_base

park_cop:
    ldr r0, =0x60007004
    mov r1, #0x80000000
    str r1, [r0]
    nop
    nop
    nop
2:
    b 2b

    .align 2
remap_stub:
    /* MMAP0 logical: 32 MiB window at zero. */
    mov r0, #0xf0000000
    orr r0, r0, #0x0000f000
    mov r1, #0x00003e00
    str r1, [r0]

    /* MMAP0 physical: SDRAM native base 0x10000000 + PP502x access flags. */
    mov r1, #0x10000000
    orr r1, r1, #0x00000f00
    orr r1, r1, #0x00000084
    str r1, [r0, #4]
    ldr pc, remap_target
remap_target:
    .word post_remap
remap_stub_end:

post_remap:
    /* Install separate banked stacks before any exception can be handled. */
    msr cpsr_c, #0xd7
    ldr sp, =_abt_stack_top
    msr cpsr_c, #0xdb
    ldr sp, =_und_stack_top
    msr cpsr_c, #0xd1
    ldr sp, =_fiq_stack_top
    msr cpsr_c, #0xd2
    ldr sp, =_irq_stack_top
    msr cpsr_c, #0xd3
    ldr sp, =_stack_top

    /* The loader placed .data at its final VMA/LMA. Only BSS is cleared. */
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
3:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 3b

    bl kernel_main
4:
    b 4b

/* Fault stubs preserve only the small, stable record needed for the first
 * hardware phase. Each mode already has a banked IRAM stack. */
undef_entry:
    mov r0, #1
    b fault_common
swi_entry:
    mov r0, #2
    b fault_common
prefetch_abort_entry:
    mov r0, #3
    b fault_common
data_abort_entry:
    mov r0, #4
    b fault_common
reserved_entry:
    mov r0, #5
    b fault_common
irq_entry:
    mov r0, #6
    b fault_common
fiq_entry:
    mov r0, #7

fault_common:
    mov r1, lr
    mrs r2, spsr

    /* A fault before MMAP0 is live cannot safely call zero-linked C code.
     * LR still carries the native 0x10000000 SDRAM window in that case;
     * request a hardware reset directly from fixed MMIO instead. */
    mov r3, r1, lsr #24
    cmp r3, #0x10
    beq early_fault_reset

    ldr sp, =_panic_stack_top
    bl a1099_fault
5:
    b 5b

early_fault_reset:
    ldr r3, =0x60006004
    ldr r4, [r3]
    orr r4, r4, #4
    str r4, [r3]
6:
    b 6b

    .size _start, . - _start
