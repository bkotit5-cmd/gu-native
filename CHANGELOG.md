# Changelog

## 1.0.1.4 — 2026-11-27

### Memory layout and diagnostics

- Expanded the Forge plugin image region from 12 MiB to 50 MiB.
- Kept the existing 1 MiB trampoline region and 1 MiB Forge-stub region.
- Rebalanced the fixed guest address-space span before the game's heap: HLE Heap is now 26 MiB, with `HEAP_BASE` unchanged at `0x08000000`.
- Derived the plugin, trampoline, stub and HLE boundaries from shared layout macros and added a compile-time minimum-size guard.
- Added plugin-region usage/free-space and HLE-heap startup usage logs.
- Improved the HLE heap-exhaustion message to report used capacity and configured limit.

### Build and validation

- Android Release build and on-device runtime validation have not yet been performed for this version.
