# A1099 Phase 1 power-telemetry hardware test

This candidate preserves the hardware-qualified cache/render/input path and
enables only bounded power observation. It starts a PCF50605 battery ADC
conversion once per second and reads the A1099 power-source and LTC4066 status
inputs. It does not change charging policy, current limits, shutdown policy, or
storage state.

## Screen meanings

- First chip green: cache self-test passed.
- Second chip blue: USB power detected.
- Third chip green: hardware reports charging.
- Third chip dark: not currently charging; this can be normal near full charge.
- Third chip red: I2C transfer fault.
- Third chip orange: ADC completed but returned an implausible voltage.
- Battery bar green/yellow/red with a partial width: valid battery ADC sample.
- Battery bar full red: I2C fault.
- Battery bar full orange: implausible ADC result.
- Fourth chip should remain green during ordinary interaction.

Observe the screen for at least 30 seconds. Report the battery-bar color and
approximate fill, third-chip color, whether either changes, interaction latency,
and any flicker or corruption.
