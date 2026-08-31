# Known user-owned RetailOS reference

The previously verified iPod Photo 5.1.2.1 partition image has:

```text
whole image size     6,514,176 bytes (0x636600)
whole image SHA-256  55845b4694263be104e8bfded72f11d1b1d5b9cbeec64f9ffaced80b0bcdc2f5
directory offset     0x3a00
physical bias        0x600

soso file offset     0x003e00
soso length          0x523514
soso load address    0x10000000
soso entry offset    0
soso checksum        0x1de43ec5
soso SHA-256         9321189b846a7317f4f575075696056e9a18c79644886a00055a402259c6fadc

dpua file offset     0x527600
dpua length          0x10eed4
dpua load address    0x10000000
dpua checksum        0x0bc3a366
```

The reference establishes container and handoff facts. Apple code is not
linked, embedded, copied or required at runtime.
