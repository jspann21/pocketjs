# Phase 1 one-candidate qualification batch

For current package bring-up, use the self-contained
`phase1-package-lifecycle-simple` folder. It performs one firmware install and
two sparse, text-only boots: corrupt pending → valid active, then corrupt
pending → embedded fallback. Its PowerShell scripts preserve and verify the
original Rockbox image and all four fixed package slots. The larger diagnostic
campaign below remains the reference for the earlier native-control and power
qualification; do not repeat its unchanged screen observations for the
package-lifecycle candidate.

Use one exact CI candidate for the complete Phase-1 hardware pass. The batch
combines the native controls, power observation, QuickJS guest, disk package,
fallback, Hold transition, and restoration checks. It does not install a new
firmware image between those checks.

## Build the evidence bundle

The direct-build workflow emits these files beside the firmware:

- `PHASE1_BUILD_REPORT.json` — symbol and image facts from the existing
  `build_phase1_report.py` tool;
- `PHASE1_BATCH_MANIFEST.json` — the candidate hash, wrapper checks, static
  artifact set, and reversible installation commands;
- `PHASE1_BATCH_RESULT.json` — an operator-editable result template;
- `PHASE1_BATCH_SHA256SUMS.txt` — hashes for the immutable firmware and static
  evidence files. The result template is intentionally not included because
  the operator edits it after the bundle is downloaded.

For a local build, prepare the same bundle after the existing `make ... test`
target succeeds:

```sh
H=hosts/ipod-photo-baremetal
python3 "$H/tools/build_phase1_report.py" \
  --elf "$H/build-phase1/pocketjs-a1099-phase1-qualified.elf" \
  --bin "$H/build-phase1/pocketjs-a1099-phase1-qualified.bin" \
  --ipod "$H/build-phase1/pocketjs-a1099-phase1-qualified.ipod" \
  --output "$H/build-phase1/PHASE1_BUILD_REPORT.json" \
  --commit "$(git rev-parse HEAD)"
python3 "$H/tools/phase1_batch.py" prepare \
  --build-dir "$H/build-phase1" \
  --image-name pocketjs-a1099-phase1-qualified \
  --build-report "$H/build-phase1/PHASE1_BUILD_REPORT.json" \
  --commit "$(git rev-parse HEAD)" \
  --output "$H/build-phase1/PHASE1_BATCH_MANIFEST.json" \
  --result-template "$H/build-phase1/PHASE1_BATCH_RESULT.json"
(
  cd "$H/build-phase1"
  sha256sum \
    pocketjs-a1099-phase1-qualified.{elf,bin,ipod,map,dis} \
    generated/recovery.pocket \
    PHASE1_{BUILD_REPORT,BATCH_MANIFEST}.json \
    > PHASE1_BATCH_SHA256SUMS.txt
  sha256sum -c PHASE1_BATCH_SHA256SUMS.txt
)
```

The bundle is tied to the `.ipod` hash. Do not substitute a locally rebuilt
image after the manifest was prepared; prepare a new bundle so its commit and
hash remain aligned.

## One device session

Before connecting the device, save the existing `rockbox.ipod` and a complete
firmware-partition backup off-device, and confirm Select + Play enters boot-ROM
disk mode. The artifact's `generated/recovery.pocket` is the valid disk guest;
copy it to `/POCKETJS/APP.PKT`. Create a rejected-package fixture from the same
bytes by changing one footer byte, and keep it off-device until the fallback
step:

```sh
cd hosts/ipod-photo-baremetal
cp build-phase1/generated/recovery.pocket build-phase1/APP.VALID.PKT
python3 - <<'PY'
from pathlib import Path
source = Path("build-phase1/APP.VALID.PKT")
corrupt = bytearray(source.read_bytes())
corrupt[-1] ^= 1
Path("build-phase1/APP.CORRUPT.PKT").write_bytes(corrupt)
PY
```

Stage `APP.VALID.PKT` as `/POCKETJS/APP.PKT` before installing the candidate.

Install and inspect the transaction with the existing reversible tool:

```sh
python3 tools/handoff.py install --mount /path/to/IPOD \
  --probe build-phase1/pocketjs-a1099-phase1-qualified.ipod
python3 tools/handoff.py status --mount /path/to/IPOD
```

After the normal handoff, complete the checks below without staging another
firmware image:

1. Confirm the framebuffer, backlight, five buttons, wheel direction/touch,
   Hold, heartbeat, Select pattern, and Menu + Play reset.
2. Observe the battery bar, USB chip, charging chip, cache chip, and
   performance chip for at least 30 seconds. If cable-state transitions are
   needed, perform them during this same candidate session and record both
   states.
3. With the valid `APP.PKT` present, confirm the cyan runtime chip, guest pulse,
   guest lane, wheel/button/Hold reactions, and absence of partial-update
   artifacts.
4. For the storage fallback check, enter disk mode, replace only `APP.PKT` with
   `APP.CORRUPT.PKT`, reboot the same candidate, and confirm the embedded
   fallback indicator. Restore `APP.VALID.PKT` before the final reboot. This
   changes user data only; the firmware image is unchanged.
5. Enter disk mode and run `handoff.py restore`. Run `handoff.py status` again
   and retain the output with the result. The original `rockbox.ipod` hash must
   match the pre-install record and the transaction state must be absent.

The result is intentionally entered offline. Copy the template, fill every
observation, and let the tool reject candidate drift or an incomplete run:

```sh
python3 tools/phase1_batch.py capture \
  --manifest build-phase1/PHASE1_BATCH_MANIFEST.json \
  --result build-phase1/PHASE1_BATCH_RESULT.json \
  --output build-phase1/PHASE1_BATCH_EVIDENCE.json \
  --markdown build-phase1/PHASE1_BATCH_RESULT.md \
  --strict
```

After capture, hash the completed result alongside the evidence file when
archiving the session:

```sh
sha256sum build-phase1/PHASE1_BATCH_RESULT.json \
  build-phase1/PHASE1_BATCH_EVIDENCE.json \
  build-phase1/PHASE1_BATCH_RESULT.md
```

`--strict` requires the 30-second observation, all native/recovery checks,
telemetry assertions, valid-package and fallback outcomes, a cyan runtime
chip, and a successful restore. A non-strict capture is useful for preserving
an interrupted session; it reports `incomplete` or `fail` and is not a
qualification result.

Stop and preserve the evidence after any unexplained storage, USB, battery, or
power behavior. The batch tool never writes to the device and cannot replace
the required off-device backups.
