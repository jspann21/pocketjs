# Clean-room implementation boundary

This stage-one implementation is original PocketJS host code. It contains no
Apple firmware bytes, no disassembled Apple instructions, and no linked or
copied Rockbox/iPodLinux source bodies.

The implementation uses hardware and file-format facts:

- PortalPlayer PP5020 MMIO addresses and bit meanings;
- ARMv4T reset/vector and exception-mode behavior;
- the observed Apple loader handoff and MMAP0 values;
- A1099 panel strap interpretation and controller command numbers;
- click-wheel packet fields and acknowledgement values;
- the public `ipco` model/checksum transport contract.

Firmware analysis is kept as metadata, hashes, offsets, register facts, and
behavioral observations. Proprietary payloads remain outside the repository.
