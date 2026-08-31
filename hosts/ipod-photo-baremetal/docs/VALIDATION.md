# Phase-0 validation record

The default source rebuilt locally with LLVM/LLD 17.0.0 to the exact prior
reference artifact:

```text
flat image:        5,352 bytes
flat SHA-256:      24f575204aff295f726f0cfae64429c2de7403754fbb579b48b851dba78015bb
ipco wrapper:      5,360 bytes
ipco SHA-256:      652f4c86030a02f010603a015fb78bd18f3cbbd657e8313dd365cef1f45af141
ipco checksum:     0x00087B1D
reset offset:      0x00000100
cache wait:        0x0000018C
cache disable:     0x000001A4
remap stub:        0x000001F4..0x0000021C (40 bytes)
relocations:       none
undefined symbols: none
```

The verifier additionally checks all eight unconditional ARM vector branches,
the handoff-tag location, copied remap target, image/BSS/stack/heap ordering,
32 MiB RAM ceiling, transport model/checksum, and wrapper/body identity.

Host tests cover:

- `ipco` round trip and corruption rejection;
- click-wheel packet decoding;
- atomic install/status/restore;
- volume-root and `.rockbox` discovery;
- writable Windows state flush;
- safe recovery from an interrupted pre-install.

GitHub Actions rebuilds the source from the pull-request head, runs the same
static and host gates, builds fallback/compile-only variants, emits a build
report, and uploads the exact candidate artifact. Hardware qualification is
attached to the commit and artifact hash actually tested, not merely to this
reference hash.
