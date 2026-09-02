# Phase 1 read-only multi-app launcher

This batch adds bounded, deterministic discovery of PocketJS packages in
`/POCKETJS/APPS` and a packaged launcher at `/POCKETJS/LAUNCHER.PKT`.
**The device performs no filesystem writes.** The launcher receives only the
sorted display labels and returns the selected index to native code. The
selected package still passes the existing target, host ABI, package-hash,
QuickJS boot, and first-frame gates before it becomes active.

The initial device gate intentionally keeps the storage contract narrow:

- `/POCKETJS/APPS` may contain at most six visible `.PKT` files;
- visible names use uppercase 8.3 characters (`A-Z`, `0-9`, `_`, and `-`);
- labels come from the filename and sort lexicographically;
- the exact launcher package is pinned by its internal package hash;
- only the selected app is admitted and evaluated;
- Menu+Play reboots to the launcher; hot return and resume are not included.

Use `phase1-multi-app-simple` for the physical gate. One installation stages
the launcher, `ALPHA.PKT`, and `BETA.PKT`, while preserving the exact previous
launcher and complete `APPS` tree. Boot once, press center on Alpha, then hold
Menu+Play for two seconds. On the second boot, turn the wheel to Beta and press
center. No reconnect, reinstall, or package staging is needed between those
boots.

The selected app has one native, position-stable source line:
`APP 1/2 R#####` for Alpha and `APP 2/2 R#####` for Beta. `R#####` is the
cumulative sector-read count and varies with FAT32 layout. `RESTORE.ps1`
restores the original Rockbox image and the exact prior launcher and `APPS`
contents or absence.
