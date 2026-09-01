#include "power.h"

#include "pp5020.h"
#include "timer.h"

#define PCF50605_ADDRESS 0x08u
#define PCF50605_ADCC1 0x2fu
#define PCF50605_ADCS1 0x30u
#define PCF50605_BATTERY_CHANNEL 0x02u
#define I2C_SEND 0x80u
#define I2C_READ 0x20u
#define I2C_BUSY 0x40u
#define I2C_TRIES 3u
#define I2C_WAIT_US 5000u
#define BATTERY_MIN_MV 2500u
#define BATTERY_MAX_MV 5000u

static bool wait_idle(void)
{
    uint32_t started = timer_now_us();
    while ((PP_I2C_STATUS & I2C_BUSY) != 0u) {
        if ((uint32_t)(timer_now_us() - started) >= I2C_WAIT_US) return false;
    }
    return true;
}

static void recover_bus(void)
{
    PP_DEV_EN |= PP_DEV_I2C;
    PP_DEV_RS |= PP_DEV_I2C;
    pp_nop3();
    PP_DEV_RS &= ~PP_DEV_I2C;
}

static bool send_bytes(uint8_t address, const uint8_t *data, uint32_t length)
{
    if (data == 0 || length == 0u || length > 4u || !wait_idle()) return false;
    PP_I2C_ADDR = (uint8_t)(address << 1);
    PP_I2C_CTRL &= (uint8_t)~I2C_READ;
    for (uint32_t index = 0u; index < length; ++index) PP_I2C_DATA(index) = data[index];
    PP_I2C_CTRL = (uint8_t)((PP_I2C_CTRL & (uint8_t)~0x06u) | ((length - 1u) << 1));
    PP_I2C_CTRL |= I2C_SEND;
    return wait_idle();
}

static bool read_bytes(uint8_t address, uint8_t *data, uint32_t length)
{
    if (data == 0 || length == 0u || length > 4u || !wait_idle()) return false;
    PP_I2C_ADDR = (uint8_t)((address << 1) | 1u);
    PP_I2C_CTRL |= I2C_READ;
    PP_I2C_CTRL = (uint8_t)((PP_I2C_CTRL & (uint8_t)~0x06u) | ((length - 1u) << 1));
    PP_I2C_CTRL |= I2C_SEND;
    if (!wait_idle()) return false;
    for (uint32_t index = 0u; index < length; ++index) data[index] = PP_I2C_DATA(index);
    return true;
}

static bool pcf_write(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    for (uint32_t attempt = 0u; attempt < I2C_TRIES; ++attempt) {
        if (send_bytes(PCF50605_ADDRESS, data, 2u)) return true;
        recover_bus();
    }
    return false;
}

static bool pcf_read_multiple(uint8_t reg, uint8_t *data, uint32_t length)
{
    for (uint32_t attempt = 0u; attempt < I2C_TRIES; ++attempt) {
        if (send_bytes(PCF50605_ADDRESS, &reg, 1u) &&
            read_bytes(PCF50605_ADDRESS, data, length)) return true;
        recover_bus();
    }
    return false;
}

uint16_t power_battery_mv_from_raw(uint16_t raw)
{
    if (raw > 1023u) raw = 1023u;
    return (uint16_t)(((uint32_t)raw * 6000u) >> 10);
}

uint8_t power_decode_source_flags(uint32_t gpioc, uint32_t gpiod,
                                  uint32_t gpo32_input)
{
    uint8_t flags = 0u;
    if ((gpioc & 0x04u) == 0u) flags |= PJS_POWER_FIREWIRE;
    if ((gpiod & 0x08u) != 0u) flags |= PJS_POWER_USB;
    if ((gpo32_input & 0x01u) == 0u) flags |= PJS_POWER_CHARGING;
    return flags;
}

uint8_t power_source_flags_read(void)
{
    uint8_t flags = 0u;
    if ((PP_GPIOC_INPUT_VAL & 0x04u) == 0u) flags |= PJS_POWER_FIREWIRE;
    if ((PP_GPIOD_INPUT_VAL & 0x08u) != 0u) flags |= PJS_POWER_USB;
    return flags;
}

void power_telemetry_init(void)
{
    recover_bus();
    /* PP5020 iPod targets select the 24 MHz source with the controller's
     * standard divider value. Resetting to zero first mirrors the proven
     * controller initialization sequence and avoids inheriting a loader's
     * stale clock selection. */
    PP_I2C_CLOCK = 0u;
    PP_I2C_CLOCK = 0x80u;
    /* Make GPO32 bit 0 observable as the LTC4066 charging indication. */
    PP_GPO32_INPUT_ENABLE |= 0x01u;
    PP_GPO32_ENABLE &= ~0x01u;
}

void power_telemetry_sample(PjsPowerTelemetry *telemetry)
{
    if (telemetry == 0) return;
    PP_GPO32_INPUT_VAL = 0u;
    uint8_t flags = power_decode_source_flags(PP_GPIOC_INPUT_VAL, PP_GPIOD_INPUT_VAL,
                                              PP_GPO32_INPUT_VAL);
    uint8_t adc[2] = {0u, 0u};
    bool ok = pcf_write(PCF50605_ADCC1,
                        (uint8_t)((PCF50605_BATTERY_CHANNEL << 1) | 1u)) &&
              pcf_read_multiple(PCF50605_ADCS1, adc, 2u);
    if (ok) {
        uint16_t raw = (uint16_t)(((uint16_t)adc[0] << 2) | (adc[1] & 0x03u));
        uint16_t mv = power_battery_mv_from_raw(raw);
        telemetry->battery_raw = raw;
        if (mv >= BATTERY_MIN_MV && mv <= BATTERY_MAX_MV) {
            telemetry->battery_mv = mv;
            telemetry->consecutive_failures = 0u;
            flags |= PJS_POWER_ADC_VALID;
        } else {
            if (telemetry->consecutive_failures != 0xffu) ++telemetry->consecutive_failures;
            flags |= PJS_POWER_ADC_RANGE_FAULT;
        }
    } else {
        if (telemetry->consecutive_failures != 0xffu) ++telemetry->consecutive_failures;
        flags |= PJS_POWER_I2C_FAULT;
    }
    telemetry->flags = flags;
    ++telemetry->samples;
}
