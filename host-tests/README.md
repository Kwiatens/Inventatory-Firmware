# Host unit tests

This directory builds the firmware's Unity suites for a desktop computer and runs
them. It needs no hardware, and CI runs it on every change to the firmware.

```sh
make -C host-tests test                              # build and run
make -C host-tests SANITIZE=address,undefined test   # the same, with sanitizers
make -C host-tests clean
```

Requirements: a C++17 compiler (GCC or Clang), `make`, and the OpenSSL development
headers (`libssl-dev` on Debian and Ubuntu, `openssl-devel` on Fedora).

The Makefile needs POSIX shell tools, so on Windows run it from MSYS2 or WSL.

## What runs

| Suite | Source | Purpose |
| --- | --- | --- |
| Shared suite | `../test/*.cpp` | The Unity suite the device also runs. The same files compile for both targets. |
| Host-only suites | `tests/*.cpp` | Tests that need the NVS stand-in: outbox durability under write failure and power loss, and the stand-in itself. |

`main.cpp` runs the shared suite through its `setup()` and then the host-only suite
through `runHostSuite()`. The process exits non-zero if either suite has a failing
assertion.

## Stand-ins (`shim/`)

The firmware headers include Arduino, NVS and mbedtls headers. The shims provide only
what those headers call:

- `Arduino.h`: a `String` with the Arduino semantics the firmware relies on (length,
  `indexOf`, `substring`, `remove`, `toInt`, `getBytes`, and so on), plus `min`/`max`.
- `Preferences.h`: an in-memory NVS. It enforces the 15-character namespace and key
  limits, and `host_nvs::faults()` injects write failures and power loss (a write
  budget after which every write fails). `host_nvs::reboot()` clears the faults and
  keeps the stored data.
- `mbedtls/md.h`: HMAC-SHA-256 backed by OpenSSL.
- `U8g2lib.h`: the font symbols `display_style.h` takes addresses of.

`main.cpp` resets the NVS stand-in before every test, so no test depends on another.

## Adding a host-only test

1. Add a file under `tests/` with functions named `test_host_<what>`.
2. Declare each one in `tests/host_suite.cpp` and add a `RUN_TEST` line there.

Device-compatible tests go in `../test/` instead, so the board runs them too.

## Known defects

A test that documents a defect is marked with `TEST_IGNORE_MESSAGE("KNOWN DEFECT: ...")`.
It shows as ignored in every run, and the message explains what to remove once the
defect is fixed. Do not weaken an assertion to make a defect pass.

## Limits

- The shims are stand-ins, not the real libraries. They do not reproduce NVS
  partition layout, flash wear, or the ESP32's 32-bit `unsigned long`. Timing helpers
  therefore use fixed-width types, so they behave the same on both targets.
- The suite does not replace the on-board run (`pio test -e tenstar_esp32s3_zero_usb`),
  which is still required before a hardware release.
