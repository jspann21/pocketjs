# Stage-one validation record

Built twice from clean trees with LLVM 17.0.0. Both builds were byte-identical.
No hardware execution is claimed.

```text
ELF   73,016 bytes  28ffac5efeb86aecb5dc9a4a980b1b3b6a4efd8464a5b5035889e522f29cf7c3
BIN    4,420 bytes  d7e18735c825598eb75c8df25337dda3f35f7f32680896d06df545a78e543e17
IPOD   4,428 bytes  e6940bd944317e2c8bb12d130146a46c76e9bda0677be986d5cc53e8ca0eef51
MAP    9,016 bytes  799b87eab3ef530196b3a323585ee72ab26269ccb2fc311b66ec05d462d1bbfa
DIS   53,170 bytes  7f84346c0c99eafcb48eb327e3a346a4f2ad89a72f35f08af7aef61028d9228e
```

The verifier established:

- ELF32 little-endian ARM executable with entry address zero;
- two bounded `PT_LOAD` segments, the first at address zero;
- no interpreter, dynamic program header, or relocation/dynamic section;
- exact reproduction of every file-backed load byte in the flat image;
- ARM B/BL instructions in all eight vector slots;
- no undefined symbols;
- standard `ipco` model bytes and checksum seed 3;
- exact image payload followed only by four-byte alignment padding;
- deterministic second clean build;
- host unit tests for packaging and firmware-directory metadata.

The actual 5.1.2.1 reference image was also passed through the metadata parser;
its size, SHA-256, OSOS offset/length/load address, and OSOS additive checksum
matched the independently derived evidence in `FIRMWARE_REFERENCE.md`.
