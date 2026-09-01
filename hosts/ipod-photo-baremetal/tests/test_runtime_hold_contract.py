#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
qjs = (root / "src/qjs_runtime.c").read_text()
qh = (root / "include/qjs_runtime.h").read_text()
bridge = (root / "include/core_bridge.h").read_text()
rust = (root / "rust-core/src/lib.rs").read_text()
assert "PJS_QJS_FRAME_BUDGET_US 250000u" in qjs
assert "PJS_QJS_ERROR_FRAME_BUDGET 8u" in qh
assert "PJS_QJS_ERROR_BOOT_BUDGET 9u" in qh
assert "PJS_RUNTIME_ERROR_BUDGET 5u" in bridge
assert "220.0, 8.0, abgr(230, 12, 32, 255)" in rust
assert "5 => abgr(255, 145, 35, 255)" in rust
print("runtime-hold contract OK")
