#!/usr/bin/env python3
"""Prepare and capture one offline iPod Photo Phase-1 qualification batch.

The firmware diagnostics are intentionally visual, so a hardware run needs a
small amount of operator-entered evidence.  This tool keeps the static build
facts and the visual observations together.  ``prepare`` is safe to run in CI
and emits a candidate manifest plus a result template.  ``capture`` validates
an edited result, checks that it belongs to the exact candidate, and computes a
conservative assessment without touching a device.

The tool does not install firmware or access a raw disk.  Device staging and
restoration remain the explicit, reversible ``handoff.py`` operations.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import subprocess
from pathlib import Path
from typing import Any


SCHEMA = 1
TARGET = "ipod-photo-a1099"
IPCO_MODEL = b"ipco"
CHECKSUM_SEED = 3
DEFAULT_IMAGE_NAME = "pocketjs-a1099-phase1-qualified"

NATIVE_FIELDS = (
    "bootloaderHandoff",
    "framebuffer",
    "displayEdges",
    "heartbeat",
    "controls",
    "wheel",
    "hold",
    "selectPattern",
    "menuPlayReset",
    "diskModeRecovery",
    "restoreSucceeded",
)

TELEMETRY_FIELDS = (
    "batteryPlausible",
    "usbTransitionObserved",
    "chargingTransitionObserved",
    "cacheGreen",
    "performanceNotRed",
)

RUNTIME_FIELDS = (
    "guestPulseChanging",
    "guestLaneVisible",
    "guestWheelPrompt",
    "guestTouchCyan",
    "guestButtonsReacted",
    "guestHoldReacted",
    "holdRuntimeStayedCyan",
)

STORAGE_FIELDS = (
    "validDiskGuestLoaded",
    "corruptPackageRejected",
    "embeddedFallbackLoaded",
)

COLOR_VALUES = {"green", "yellow", "orange", "red", "blue", "cyan", "magenta", "dark", "other"}
LATENCY_VALUES = {"prompt", "normal", "delayed", "slow", "stopped", "not-working", "other"}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while True:
            chunk = source.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def json_write(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def json_read(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"cannot read JSON {path}: {error}") from error


def relative_path(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return str(path.resolve())


def artifact_fact(path: Path, root: Path, role: str) -> dict[str, object]:
    if not path.is_file():
        raise SystemExit(f"missing Phase-1 {role}: {path}")
    return {
        "role": role,
        "path": relative_path(path, root),
        "bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def git_commit() -> str | None:
    try:
        completed = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            check=True,
            text=True,
            capture_output=True,
            timeout=10,
        )
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return None
    value = completed.stdout.strip()
    return value or None


def validate_candidate(build_dir: Path, image_name: str) -> tuple[dict[str, object], dict[str, object]]:
    """Return artifact facts and static wrapper checks for one build directory."""

    paths = {
        "elf": build_dir / f"{image_name}.elf",
        "bin": build_dir / f"{image_name}.bin",
        "ipod": build_dir / f"{image_name}.ipod",
        "map": build_dir / f"{image_name}.map",
        "disassembly": build_dir / f"{image_name}.dis",
        "recoveryPackage": build_dir / "generated" / "recovery.pocket",
    }
    roles = {
        "elf": "ELF",
        "bin": "flat image",
        "ipod": "ipco wrapper",
        "map": "link map",
        "disassembly": "disassembly",
        "recoveryPackage": "embedded recovery package",
    }
    artifacts = {
        key: artifact_fact(path, build_dir, roles[key]) for key, path in paths.items()
    }

    image = paths["bin"].read_bytes()
    wrapper = paths["ipod"].read_bytes()
    checks: dict[str, object] = {
        "artifactSetComplete": True,
        "ipcoModel": False,
        "ipcoChecksum": False,
        "wrapperContainsFlatImage": False,
        "handoffMarker": False,
        "imageWordAligned": len(image) % 4 == 0,
    }
    if len(wrapper) < 8:
        raise SystemExit(f"ipco wrapper is truncated: {paths['ipod']}")
    checks["ipcoModel"] = wrapper[4:8] == IPCO_MODEL
    if not checks["ipcoModel"]:
        raise SystemExit("Phase-1 wrapper model is not ipco")
    checks["wrapperContainsFlatImage"] = wrapper[8:] == image
    if not checks["wrapperContainsFlatImage"]:
        raise SystemExit("Phase-1 ipco wrapper does not contain the flat image")
    declared = struct.unpack_from(">I", wrapper, 0)[0]
    checks["ipcoChecksum"] = declared == (CHECKSUM_SEED + sum(image)) & 0xFFFFFFFF
    if not checks["ipcoChecksum"]:
        raise SystemExit("Phase-1 ipco checksum does not match the flat image")
    checks["handoffMarker"] = image[0x20:0x28] == b"Rockbox\x01"
    if not checks["handoffMarker"]:
        raise SystemExit("Phase-1 handoff marker is missing at image offset 0x20")
    if not checks["imageWordAligned"]:
        raise SystemExit("Phase-1 flat image is not 4-byte aligned")
    return artifacts, checks


def blank_result(manifest: dict[str, object]) -> dict[str, object]:
    candidate = manifest["candidate"]
    assert isinstance(candidate, dict)
    artifacts = manifest["artifacts"]
    assert isinstance(artifacts, dict)
    ipod = artifacts["ipod"]
    assert isinstance(ipod, dict)
    return {
        "schema": SCHEMA,
        "kind": "phase1-qualification-result",
        "candidate": {
            "imageName": candidate["imageName"],
            "commit": candidate["commit"],
            "workflowRun": candidate["workflowRun"],
            "ipodSha256": ipod["sha256"],
        },
        "operator": "",
        "device": {
            "model": "",
            "storage": "",
            "panel": "",
        },
        "timing": {
            "firstScreenSeconds": None,
            "observationSeconds": None,
            "heartbeat": None,
            "inputLatency": None,
            "performanceChip": None,
            "partialUpdateArtifacts": "",
            "runtimeRedAfterSeconds": None,
        },
        "native": {field: None for field in NATIVE_FIELDS},
        "telemetry": {
            "batteryBarColor": None,
            "batteryFillPercent": None,
            "usbChip": None,
            "chargingChip": None,
            "cacheChip": None,
            **{field: None for field in TELEMETRY_FIELDS},
        },
        "runtime": {
            "chip": None,
            **{field: None for field in RUNTIME_FIELDS},
        },
        "storage": {field: None for field in STORAGE_FIELDS},
        "notes": "",
    }


def require_object(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{name} must be an object")
    return value


def require_text(value: Any, name: str, missing: list[str]) -> None:
    if not isinstance(value, str) or not value.strip():
        missing.append(name)


def require_bool(value: Any, name: str, missing: list[str]) -> None:
    if not isinstance(value, bool):
        missing.append(name)


def validate_result(manifest: dict[str, object], result: dict[str, object], *, strict: bool) -> dict[str, object]:
    """Validate the operator result and return a pass/fail/incomplete assessment."""

    missing: list[str] = []
    failed: list[str] = []
    candidate = require_object(manifest.get("candidate"), "manifest.candidate")
    artifacts = require_object(manifest.get("artifacts"), "manifest.artifacts")
    manifest_ipod = require_object(artifacts.get("ipod"), "manifest.artifacts.ipod")
    result_candidate = require_object(result.get("candidate"), "result.candidate")
    expected_hash = manifest_ipod.get("sha256")
    actual_hash = result_candidate.get("ipodSha256")
    if actual_hash != expected_hash:
        failed.append("candidate.ipodSha256")
    for key in ("imageName", "commit", "workflowRun"):
        if result_candidate.get(key) != candidate.get(key):
            failed.append(f"candidate.{key}")

    device = require_object(result.get("device"), "result.device")
    for key in ("model", "storage", "panel"):
        require_text(device.get(key), f"device.{key}", missing)

    timing = require_object(result.get("timing"), "result.timing")
    first_screen = timing.get("firstScreenSeconds")
    if not isinstance(first_screen, (int, float)) or isinstance(first_screen, bool) or first_screen < 0:
        missing.append("timing.firstScreenSeconds")
    observation = timing.get("observationSeconds")
    if not isinstance(observation, (int, float)) or isinstance(observation, bool):
        missing.append("timing.observationSeconds")
    elif observation < 30:
        failed.append("timing.observationSeconds")
    if timing.get("heartbeat") not in LATENCY_VALUES:
        missing.append("timing.heartbeat")
    if timing.get("inputLatency") not in LATENCY_VALUES:
        missing.append("timing.inputLatency")
    if timing.get("performanceChip") not in COLOR_VALUES:
        missing.append("timing.performanceChip")
    if not isinstance(timing.get("partialUpdateArtifacts"), str):
        missing.append("timing.partialUpdateArtifacts")
    runtime_red = timing.get("runtimeRedAfterSeconds")
    if runtime_red is not None and (
        not isinstance(runtime_red, (int, float)) or isinstance(runtime_red, bool) or runtime_red < 0
    ):
        missing.append("timing.runtimeRedAfterSeconds")

    for section_name, fields in (
        ("native", NATIVE_FIELDS),
        ("telemetry", TELEMETRY_FIELDS),
        ("runtime", RUNTIME_FIELDS),
        ("storage", STORAGE_FIELDS),
    ):
        section = require_object(result.get(section_name), f"result.{section_name}")
        for field in fields:
            value = section.get(field)
            require_bool(value, f"{section_name}.{field}", missing)
            if value is False:
                failed.append(f"{section_name}.{field}")

    static_checks = require_object(manifest.get("staticChecks"), "manifest.staticChecks")
    for name, value in static_checks.items():
        if value is not True:
            failed.append(f"staticChecks.{name}")

    telemetry = require_object(result.get("telemetry"), "result.telemetry")
    for field in ("batteryBarColor", "usbChip", "chargingChip", "cacheChip"):
        if telemetry.get(field) not in COLOR_VALUES:
            missing.append(f"telemetry.{field}")
    fill = telemetry.get("batteryFillPercent")
    if not isinstance(fill, (int, float)) or isinstance(fill, bool) or not 0 <= fill <= 100:
        missing.append("telemetry.batteryFillPercent")

    runtime = require_object(result.get("runtime"), "result.runtime")
    if runtime.get("chip") not in COLOR_VALUES:
        missing.append("runtime.chip")

    if telemetry.get("cacheChip") == "red":
        failed.append("telemetry.cacheChip")
    if telemetry.get("chargingChip") == "red":
        failed.append("telemetry.chargingChip")
    if timing.get("performanceChip") == "red":
        failed.append("timing.performanceChip")
    if runtime.get("chip") != "cyan":
        failed.append("runtime.chip")
    if timing.get("heartbeat") not in {"normal", "prompt"}:
        failed.append("timing.heartbeat")
    if timing.get("inputLatency") not in {"normal", "prompt"}:
        failed.append("timing.inputLatency")
    if str(timing.get("partialUpdateArtifacts", "")).strip().lower() not in {
        "none",
        "none observed",
        "no",
    }:
        failed.append("timing.partialUpdateArtifacts")
    if timing.get("runtimeRedAfterSeconds") is not None:
        failed.append("timing.runtimeRedAfterSeconds")
    if result.get("notes") is not None and not isinstance(result.get("notes"), str):
        missing.append("notes")

    if missing:
        status = "incomplete"
    elif failed:
        status = "fail"
    else:
        status = "pass"
    if strict and status != "pass":
        detail = ", ".join(missing + failed)
        raise SystemExit(f"Phase-1 qualification result is {status}: {detail}")
    return {
        "status": status,
        "missing": sorted(set(missing)),
        "failed": sorted(set(failed)),
        "requiresRestore": True,
        "observationSecondsMinimum": 30,
    }


def render_markdown(manifest: dict[str, object], result: dict[str, object], assessment: dict[str, object] | None) -> str:
    candidate = require_object(manifest["candidate"], "manifest.candidate")
    artifacts = require_object(manifest["artifacts"], "manifest.artifacts")
    ipod = require_object(artifacts["ipod"], "manifest.artifacts.ipod")
    result_candidate = require_object(result.get("candidate"), "result.candidate")
    timing = require_object(result.get("timing"), "result.timing")
    native = require_object(result.get("native"), "result.native")
    telemetry = require_object(result.get("telemetry"), "result.telemetry")
    runtime = require_object(result.get("runtime"), "result.runtime")
    storage = require_object(result.get("storage"), "result.storage")

    def value(section: dict[str, Any], key: str) -> str:
        item = section.get(key)
        if item is None:
            return ""
        if isinstance(item, bool):
            return "yes" if item else "no"
        return str(item)

    status = assessment.get("status", "pending") if assessment else "pending"
    lines = [
        "# PocketJS iPod Photo Phase-1 batch result",
        "",
        f"Assessment: **{status}**",
        "",
        "## Candidate (static evidence)",
        "",
        f"- Image: `{candidate.get('imageName', '')}.ipod`",
        f"- Commit: `{candidate.get('commit', '')}`",
        f"- Workflow run: `{candidate.get('workflowRun', '')}`",
        f"- `.ipod` bytes: `{ipod.get('bytes', '')}`",
        f"- `.ipod` SHA-256: `{ipod.get('sha256', '')}`",
        f"- Result candidate hash: `{result_candidate.get('ipodSha256', '')}`",
        "",
        "## Observation",
        "",
        f"- Operator: {result.get('operator', '')}",
        f"- Device: {value(require_object(result.get('device'), 'result.device'), 'model')}",
        f"- Storage: {value(require_object(result.get('device'), 'result.device'), 'storage')}",
        f"- Panel: {value(require_object(result.get('device'), 'result.device'), 'panel')}",
        f"- First screen (seconds): {value(timing, 'firstScreenSeconds')}",
        f"- Observation (seconds): {value(timing, 'observationSeconds')}",
        f"- Heartbeat: {value(timing, 'heartbeat')}",
        f"- Input latency: {value(timing, 'inputLatency')}",
        f"- Performance chip: {value(timing, 'performanceChip')}",
        f"- Partial-update artifacts: {value(timing, 'partialUpdateArtifacts')}",
        "",
        "## Native and recovery",
        "",
    ]
    lines.extend(f"- {field}: {value(native, field)}" for field in NATIVE_FIELDS)
    lines.extend(["", "## Power and performance telemetry", ""])
    lines.extend(
        [
            f"- Battery bar: {value(telemetry, 'batteryBarColor')} ({value(telemetry, 'batteryFillPercent')}%)",
            f"- USB chip: {value(telemetry, 'usbChip')}",
            f"- Charging chip: {value(telemetry, 'chargingChip')}",
            f"- Cache chip: {value(telemetry, 'cacheChip')}",
        ]
    )
    lines.extend(f"- {field}: {value(telemetry, field)}" for field in TELEMETRY_FIELDS)
    lines.extend(["", "## QuickJS guest", "", f"- Runtime chip: {value(runtime, 'chip')}"])
    lines.extend(f"- {field}: {value(runtime, field)}" for field in RUNTIME_FIELDS)
    lines.extend(["", "## Storage admission and fallback", ""])
    lines.extend(f"- {field}: {value(storage, field)}" for field in STORAGE_FIELDS)
    if assessment:
        lines.extend(
            [
                "",
                "## Assessment details",
                "",
                f"- Missing fields: {', '.join(assessment.get('missing', [])) or 'none'}",
                f"- Failed checks: {', '.join(assessment.get('failed', [])) or 'none'}",
            ]
        )
    lines.extend(["", "## Notes", "", str(result.get("notes", "")), ""])
    return "\n".join(lines)


def prepare(args: argparse.Namespace) -> int:
    build_dir = args.build_dir.resolve()
    artifacts, checks = validate_candidate(build_dir, args.image_name)
    commit = args.commit or git_commit() or "unknown"
    workflow_run = args.workflow_run or os.environ.get("GITHUB_RUN_ID", "local")
    manifest: dict[str, object] = {
        "schema": SCHEMA,
        "kind": "phase1-qualification-batch",
        "target": TARGET,
        "candidate": {
            "imageName": args.image_name,
            "commit": commit,
            "workflowRun": workflow_run,
        },
        "artifacts": artifacts,
        "staticChecks": checks,
        "installation": {
            "method": "reversible /rockbox.ipod handoff",
            "installCommand": "tools/handoff.py install",
            "restoreCommand": "tools/handoff.py restore",
            "deviceMutation": "mounted FAT32 rockbox.ipod only",
            "singleStagingCycle": True,
        },
        "hardwareBatch": {
            "observationSecondsMinimum": 30,
            "runNativeControlsBeforeCableTransitions": True,
            "restoreBeforeDisconnecting": True,
            "resultTemplate": args.result_template.name if args.result_template else None,
        },
    }
    if args.build_report:
        report_path = args.build_report.resolve()
        report = json_read(report_path)
        if not isinstance(report, dict):
            raise SystemExit(f"build report is not a JSON object: {report_path}")
        report_image = report.get("image")
        report_ipod = report.get("ipod")
        if (
            report.get("schema") != SCHEMA
            or report.get("target") != TARGET + "-phase1"
            or report.get("commit") != commit
            or not isinstance(report_image, dict)
            or not isinstance(report_ipod, dict)
            or report_image.get("sha256") != artifacts["bin"]["sha256"]
            or report_image.get("bytes") != artifacts["bin"]["bytes"]
            or report_ipod.get("sha256") != artifacts["ipod"]["sha256"]
            or report_ipod.get("bytes") != artifacts["ipod"]["bytes"]
        ):
            raise SystemExit("Phase-1 build report does not match the candidate artifacts")
        checks["buildReportAligned"] = True
        manifest["buildReport"] = {
            "path": relative_path(report_path, build_dir),
            "sha256": sha256_file(report_path),
            "schema": report.get("schema"),
        }
    json_write(args.output.resolve(), manifest)
    if args.result_template:
        json_write(args.result_template.resolve(), blank_result(manifest))
    print(f"prepared Phase-1 batch manifest: {args.output}")
    print(f"candidate .ipod SHA-256: {artifacts['ipod']['sha256']}")
    print(f"static checks: {sum(bool(value) for value in checks.values())}/{len(checks)} passed")
    return 0


def capture(args: argparse.Namespace) -> int:
    manifest = json_read(args.manifest.resolve())
    result = json_read(args.result.resolve())
    if not isinstance(manifest, dict):
        raise SystemExit("qualification manifest is not a JSON object")
    if not isinstance(result, dict):
        raise SystemExit("qualification result is not a JSON object")
    if manifest.get("schema") != SCHEMA or manifest.get("kind") != "phase1-qualification-batch":
        raise SystemExit("unsupported Phase-1 batch manifest")
    if result.get("schema") != SCHEMA or result.get("kind") != "phase1-qualification-result":
        raise SystemExit("unsupported Phase-1 qualification result")
    try:
        assessment = validate_result(manifest, result, strict=args.strict)
    except ValueError as error:
        raise SystemExit(f"invalid Phase-1 qualification result: {error}") from error
    output = {
        "schema": SCHEMA,
        "kind": "phase1-qualification-evidence",
        "target": manifest.get("target"),
        "candidate": manifest.get("candidate"),
        "artifact": require_object(require_object(manifest.get("artifacts"), "manifest.artifacts").get("ipod"), "manifest.artifacts.ipod"),
        "staticChecks": manifest.get("staticChecks"),
        "assessment": assessment,
        "result": result,
    }
    json_write(args.output.resolve(), output)
    if args.markdown:
        markdown_path = args.markdown.resolve()
        markdown_path.parent.mkdir(parents=True, exist_ok=True)
        markdown_path.write_text(
            render_markdown(manifest, result, assessment), encoding="utf-8"
        )
    print(f"captured Phase-1 batch evidence: {args.output}")
    print(f"assessment: {assessment['status']}")
    if assessment["missing"]:
        print(f"missing fields: {', '.join(assessment['missing'])}")
    if assessment["failed"]:
        print(f"failed checks: {', '.join(assessment['failed'])}")
    return 0


def parser() -> argparse.ArgumentParser:
    command = argparse.ArgumentParser(description=__doc__)
    subparsers = command.add_subparsers(dest="command", required=True)

    prepare_parser = subparsers.add_parser(
        "prepare", help="validate a build and emit a batch manifest/result template"
    )
    prepare_parser.add_argument("--build-dir", type=Path, required=True)
    prepare_parser.add_argument("--image-name", default=DEFAULT_IMAGE_NAME)
    prepare_parser.add_argument("--build-report", type=Path)
    prepare_parser.add_argument("--commit")
    prepare_parser.add_argument("--workflow-run")
    prepare_parser.add_argument("--output", type=Path, required=True)
    prepare_parser.add_argument("--result-template", type=Path)
    prepare_parser.set_defaults(handler=prepare)

    capture_parser = subparsers.add_parser(
        "capture", help="validate an edited result and emit evidence"
    )
    capture_parser.add_argument("--manifest", type=Path, required=True)
    capture_parser.add_argument("--result", type=Path, required=True)
    capture_parser.add_argument("--output", type=Path, required=True)
    capture_parser.add_argument("--markdown", type=Path)
    capture_parser.add_argument(
        "--strict",
        action="store_true",
        help="fail unless every required observation passes the qualification checks",
    )
    capture_parser.set_defaults(handler=capture)
    return command


def main() -> int:
    args = parser().parse_args()
    return args.handler(args)


if __name__ == "__main__":
    raise SystemExit(main())
