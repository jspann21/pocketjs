#ifndef POCKETJS_A1099_H
#define POCKETJS_A1099_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define A1099_LCD_WIDTH  220u
#define A1099_LCD_HEIGHT 176u
#define A1099_FB_PIXELS  (A1099_LCD_WIDTH * A1099_LCD_HEIGHT)

/* PortalPlayer PP5020 fixed MMIO facts. */
#define PP_PROCESSOR_ID       0x60000000u
#define PP_CPU_ID             0x55u
#define PP_COP_ID             0xaau

#define PP_CPU_INT_DISABLE    0x60004028u
#define PP_COP_INT_DISABLE    0x60004038u
#define PP_CPU_HI_INT_DISABLE 0x60004128u
#define PP_COP_HI_INT_DISABLE 0x60004138u

#define PP_USEC_TIMER         0x60005010u

#define PP_DEVICE_RESET       0x60006004u
#define PP_DEVICE_ENABLE      0x6000600cu
#define PP_DEVICE_ENABLE2     0x60006010u
#define PP_DEVICE_INIT1       0x70000010u
#define PP_DEVICE_INIT2       0x70000020u
#define PP_GPO32_VALUE        0x70000080u
#define PP_GPO32_ENABLE       0x70000084u

#define PP_CPU_CONTROL        0x60007000u
#define PP_COP_CONTROL        0x60007004u
#define PP_PROC_SLEEP         0x80000000u

#define PP_CACHE_CONTROL      0x6000c000u

#define PP_GPIOA_ENABLE       0x6000d000u
#define PP_GPIOB_ENABLE       0x6000d004u
#define PP_GPIOA_OUTPUT_EN    0x6000d010u
#define PP_GPIOB_OUTPUT_EN    0x6000d014u
#define PP_GPIOB_OUTPUT_VALUE 0x6000d024u
#define PP_GPIOA_INPUT_VALUE  0x6000d030u

#define PP_MMAP0_LOGICAL      0xf000f000u
#define PP_MMAP0_PHYSICAL     0xf000f004u

#define PP_DEVICE_SYSTEM      0x00000004u
#define PP_DEVICE_OPTO        0x00010000u
#define PP_DEVICE_PWM         0x00020000u
#define PP_DEVICE_LCD         0x04000000u
#define PP_INIT_BUTTONS       0x00040000u

#define PP_LCD2_PORT          0x70008a0cu
#define PP_LCD2_BLOCK_CTRL    0x70008a20u
#define PP_LCD2_BLOCK_CONFIG  0x70008a24u
#define PP_LCD2_BLOCK_DATA    0x70008b00u
#define PP_LCD2_BUSY          0x80000000u
#define PP_LCD2_COMMAND       0x80000000u
#define PP_LCD2_DATA          0x81000000u
#define PP_LCD2_BLOCK_READY   0x04000000u
#define PP_LCD2_BLOCK_TX_OK   0x01000000u

#define PP_PWM0_DUTY          0x7000a010u

#define PP_WHEEL_CONTROL0     0x7000c100u
#define PP_WHEEL_CONTROL1     0x7000c104u
#define PP_WHEEL_DATA         0x7000c140u

#define A1099_BUTTON_SELECT   (1u << 0)
#define A1099_BUTTON_RIGHT    (1u << 1)
#define A1099_BUTTON_LEFT     (1u << 2)
#define A1099_BUTTON_PLAY     (1u << 3)
#define A1099_BUTTON_MENU     (1u << 4)
#define A1099_BUTTON_HOLD     (1u << 5)
#define A1099_WHEEL_CW        (1u << 6)
#define A1099_WHEEL_CCW       (1u << 7)

#define A1099_CRASH_MAGIC     0x504a4352u /* PJCR */
#define A1099_CRASH_VERSION   1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t boot_count;
    uint32_t fault_count;
    uint32_t fault_cause;
    uint32_t fault_lr;
    uint32_t fault_spsr;
    uint32_t fault_usec;
    uint32_t last_phase;
    uint32_t lcd_type;
    uint32_t lcd_error;
    uint32_t input_raw;
    uint32_t reserved[5];
} A1099CrashRecord;

typedef struct {
    uint32_t buttons;
    uint32_t raw;
    int8_t wheel_delta;
    uint8_t wheel_position;
    bool wheel_touched;
    bool valid;
} A1099InputState;

extern volatile A1099CrashRecord a1099_crash_record;

void a1099_system_init(void);
void a1099_reboot(void) __attribute__((noreturn));
uint32_t a1099_usec(void);
void a1099_delay_us(uint32_t microseconds);
void a1099_fault(uint32_t cause, uint32_t lr, uint32_t spsr) __attribute__((noreturn));

bool a1099_lcd_init(void);
bool a1099_lcd_present(const uint16_t *pixels, uint32_t stride,
                       uint32_t x, uint32_t y,
                       uint32_t width, uint32_t height);
uint32_t a1099_lcd_type(void);
void a1099_backlight_set(uint8_t brightness);

void a1099_input_init(void);
A1099InputState a1099_input_poll(void);

void kernel_main(void) __attribute__((noreturn));

#endif
