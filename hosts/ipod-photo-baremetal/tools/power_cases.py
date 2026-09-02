#!/usr/bin/env python3
"""Run the Campaign 3 fake-voltage and source-policy cases.

This mirrors the constants and debounce rules in include/power.h and is kept
dependency-free so it can be copied with a hardware kit. It does not touch an
iPod or claim a charger; it is the pre-install check for threshold behavior.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass

LOW_MV = 3450
RECOVER_MV = 3550
SHUTOFF_MV = 3300
LOW_SAMPLES = 3
CRITICAL_SAMPLES = 5
GOOD_SAMPLES = 3

BATTERY_UNKNOWN = 0
BATTERY_NORMAL = 1
BATTERY_LOW = 2
BATTERY_CRITICAL = 3
USB = 1 << 1
FIREWIRE = 1 << 0
SOURCE_MASK = USB | FIREWIRE | (1 << 2)


@dataclass
class Debounce:
    state: int = BATTERY_UNKNOWN
    low_samples: int = 0
    critical_samples: int = 0
    good_samples: int = 0

    def update(self, mv: int | None) -> int:
        if mv is None:
            return self.state
        if mv <= SHUTOFF_MV:
            self.low_samples = 0
            self.critical_samples += 1
            self.good_samples = 0
            if self.critical_samples >= CRITICAL_SAMPLES:
                self.state = BATTERY_CRITICAL
            return self.state
        self.critical_samples = 0
        if mv <= LOW_MV:
            self.low_samples += 1
            self.good_samples = 0
            if self.low_samples >= LOW_SAMPLES and self.state != BATTERY_CRITICAL:
                self.state = BATTERY_LOW
            return self.state
        self.low_samples = 0
        if mv >= RECOVER_MV:
            self.good_samples += 1
            if self.good_samples >= GOOD_SAMPLES:
                self.state = BATTERY_NORMAL
        else:
            self.good_samples = 0
        return self.state


def run_case(
    name: str, samples: list[int | None], source: int, quiet: bool
) -> tuple[int, bool]:
    battery = Debounce()
    for sample in samples:
        battery.update(sample)
    source_stable = True
    shutdown_due = (
        source_stable
        and battery.state == BATTERY_CRITICAL
        and (source & SOURCE_MASK) == 0
        and battery.critical_samples >= CRITICAL_SAMPLES
    )
    if not quiet:
        print(f"{name}: state={battery.state} shutdown={'YES' if shutdown_due else 'NO'}")
    return battery.state, shutdown_due


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    cases = [
        ("noise-does-not-latch", [3440, 3560, 3440], 0, BATTERY_UNKNOWN, False),
        ("low-requires-three", [3440, 3440, 3440], 0, BATTERY_LOW, False),
        ("critical-requires-five", [3290] * 4, 0, BATTERY_UNKNOWN, False),
        ("critical-shuts-off-on-battery", [3290] * 5, 0, BATTERY_CRITICAL, True),
        ("invalid-keeps-last-good", [3440, 3440, 3440, None], 0, BATTERY_LOW, False),
        ("external-power-blocks-shutoff", [3290] * 5, USB, BATTERY_CRITICAL, False),
    ]
    failed = 0
    for name, samples, source, expected_state, expected_shutdown in cases:
        state, shutdown = run_case(name, samples, source, args.quiet)
        if (state, shutdown) != (expected_state, expected_shutdown):
            failed += 1
    if not args.quiet:
        print(f"cases={'PASS' if failed == 0 else 'FAIL'} count={len(cases)}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
