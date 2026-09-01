#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("build_embedded_pocket", ROOT / "tools" / "build_embedded_pocket.py")
assert SPEC and SPEC.loader
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)

source = (ROOT / "recovery" / "app.js").read_bytes()
first = module.build_package(source)
second = module.build_package(source)
assert first == second
module.verify_package(first, source)
assert b"ipod-photo" in first
assert first.count(source) == 1
print("embedded pocket package tests: OK")
