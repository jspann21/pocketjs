#ifndef POCKETJS_IPOD_PHOTO_PP5020_H
#define POCKETJS_IPOD_PHOTO_PP5020_H

#include <stdint.h>

#define MMIO32(address) (*(volatile uint32_t *)(uintptr_t)(address))

/* Core identity and control. */
#define PP_PROCESSOR_ID MMIO32(0x60000000u)
#define PP_CPU_CTL      MMIO32(0x60007000u)
#define PP_COP_CTL      MMIO32(0x60007004u)
#define PP_PROC_SLEEP   0x80000000u
#define PP_DEV_SYSTEM   0x00000004u

/* Interrupt controller. */
#define PP_CPU_INT_DIS       MMIO32(0x60004028u)
#define PP_COP_INT_DIS       MMIO32(0x60004038u)
#define PP_INT_FORCED_CLR    MMIO32(0x6000401cu)
#define PP_CPU_HI_INT_DIS    MMIO32(0x60004128u)
#define PP_COP_HI_INT_DIS    MMIO32(0x60004138u)
#define PP_HI_INT_FORCED_CLR MMIO32(0x6000411cu)

/* Timers and clock/reset controls. */
#define PP_USEC_TIMER MMIO32(0x60005010u)
#define PP_DEV_RS     MMIO32(0x60006004u)
#define PP_DEV_RS2    MMIO32(0x60006008u)
#define PP_DEV_EN     MMIO32(0x6000600cu)
#define PP_DEV_EN2    MMIO32(0x60006010u)
#define PP_DEV_INIT1  MMIO32(0x70000010u)
#define PP_DEV_INIT2  MMIO32(0x70000020u)
#define PP_GPO32_VAL    MMIO32(0x70000080u)
#define PP_GPO32_ENABLE MMIO32(0x70000084u)

#define PP_DEV_OPTO  0x00010000u
#define PP_DEV_PWM   0x00020000u
#define PP_DEV_LCD   0x04000000u
#define PP_INIT_BUTTONS 0x00040000u

/* Cache/MMAP. MMAP0 maps SDRAM's native 0x10000000 window to 0x00000000. */
#define PP_CACHE_CTL       MMIO32(0x6000c000u)
#define PP_CACHE_OPERATION MMIO32(0xf000f044u)
#define PP_CACHE_CTL_ENABLE 0x0001u
#define PP_CACHE_CTL_BUSY   0x8000u
#define PP_CACHE_OP_FLUSH   0x0002u
#define PP_MMAP0_LOGICAL   MMIO32(0xf000f000u)
#define PP_MMAP0_PHYSICAL  MMIO32(0xf000f004u)
#define PP_MMAP_32M_MASK   0x00003e00u
#define PP_MMAP_SDRAM_FLAGS 0x10000f84u

/* GPIO A/B, plus PP502x atomic bitwise aliases (+0x800). */
#define PP_GPIOA_ENABLE     MMIO32(0x6000d000u)
#define PP_GPIOB_ENABLE     MMIO32(0x6000d004u)
#define PP_GPIOA_OUTPUT_EN  MMIO32(0x6000d010u)
#define PP_GPIOB_OUTPUT_EN  MMIO32(0x6000d014u)
#define PP_GPIOA_OUTPUT_VAL MMIO32(0x6000d020u)
#define PP_GPIOB_OUTPUT_VAL MMIO32(0x6000d024u)
#define PP_GPIOA_INPUT_VAL  MMIO32(0x6000d030u)
#define PP_GPIOB_INPUT_VAL  MMIO32(0x6000d034u)

#define PP_GPIOA_ENABLE_ATOMIC     MMIO32(0x6000d800u)
#define PP_GPIOB_ENABLE_ATOMIC     MMIO32(0x6000d804u)
#define PP_GPIOA_OUTPUT_EN_ATOMIC  MMIO32(0x6000d810u)
#define PP_GPIOB_OUTPUT_EN_ATOMIC  MMIO32(0x6000d814u)
#define PP_GPIOA_OUTPUT_VAL_ATOMIC MMIO32(0x6000d820u)
#define PP_GPIOB_OUTPUT_VAL_ATOMIC MMIO32(0x6000d824u)

static inline void pp_gpio_set(volatile uint32_t *atomic_reg, uint32_t mask)
{
    *atomic_reg = (mask << 8) | mask;
}

static inline void pp_gpio_clear(volatile uint32_t *atomic_reg, uint32_t mask)
{
    *atomic_reg = mask << 8;
}

/* Color LCD bridge. */
#define PP_LCD2_PORT         MMIO32(0x70008a0cu)
#define PP_LCD2_BLOCK_CTRL   MMIO32(0x70008a20u)
#define PP_LCD2_BLOCK_CONFIG MMIO32(0x70008a24u)
#define PP_LCD2_BLOCK_DATA   MMIO32(0x70008b00u)
#define PP_LCD2_BUSY_MASK    0x80000000u
#define PP_LCD2_CMD_MASK     0x80000000u
#define PP_LCD2_DATA_MASK    0x81000000u
#define PP_LCD2_BLOCK_READY  0x04000000u
#define PP_LCD2_BLOCK_TXOK   0x01000000u

/* A1099 backlight PWM. */
#define PP_PWM_BACKLIGHT MMIO32(0x7000a010u)

/* Synaptics/Opto click wheel controller. */
#define PP_WHEEL_CTRL0 MMIO32(0x7000c100u)
#define PP_WHEEL_CTRL1 MMIO32(0x7000c104u)
#define PP_WHEEL_DATA  MMIO32(0x7000c140u)

static inline uint32_t pp_current_core_id(void)
{
    return PP_PROCESSOR_ID & 0xffu;
}

static inline void pp_nop3(void)
{
    __asm__ volatile("nop\n\tnop\n\tnop" ::: "memory");
}

static inline void pp_reboot(void)
{
    PP_CPU_INT_DIS = 0xffffffffu;
    PP_COP_INT_DIS = 0xffffffffu;
    PP_DEV_RS |= PP_DEV_SYSTEM;
    for (;;) {
        __asm__ volatile("nop");
    }
}

#endif
