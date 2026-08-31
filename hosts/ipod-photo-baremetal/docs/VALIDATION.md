# Stage-one validation record

The checked-in source was built twice from clean trees with LLVM 17 using the
freestanding ARM7TDMI flags in `Makefile`. Both runs produced byte-identical
artifacts. No A1099 hardware execution is claimed.

```text
ELF     73,344 bytes  db54a823543c9263535468c3803a08d2f473e30e0b92d92c00765e6d4ad16152
BIN      4,464 bytes  7126a8551190ba16b09e38ccdd4ebca5e31be6777c4c55fcbaab21f070a5ba8c
IPOD     4,472 bytes  4004d02982febb9d09935391d7dc9174d2c9aa833c1c5ef5b3064650744b1df9
MAP      9,890 bytes  edff032e8e064a8f7d13b5ee0a10692509a3893887bf8a376d8282410b8265fd
DIS     53,081 bytes  fe18b6ea4ce9af706742d548ba96c17e2bbef292dd94bf6dfc2a02bfd05b3732
```

The verifier checked:

- ELF32 little-endian ARM executable, entry address zero;
- at least one `PT_LOAD` beginning at zero and no load extent above 32 MiB;
- no dynamic/interpreter program headers or relocation/dynamic sections;
- exact reproduction of all file-backed load segments in the flat binary;
- ARM B/BL instructions in all eight vector slots;
- `ipco` model bytes and additive checksum seed 3;
- exact image bytes followed only by required zero padding;
- four standard-library host tests for packaging, directory parsing and ZIP
  inventory.
