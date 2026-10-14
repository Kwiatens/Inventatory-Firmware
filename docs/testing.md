# Testing

This page explains how the firmware is tested, which commands to run, and how to add a
test. It also lists what is not yet covered. The release gates that need a board are in
[release-audit-remediation.md](release-audit-remediation.md).

## Layers

| Layer | What it checks | Where | Runs |
| --- | --- | --- | --- |
| Compile-time | Pin reservations, RTC GPIOs, NVS name lengths, record sizes (`static_assert`) | the headers and sources | every build |
| Shared unit suite | Pure logic and parsers, written once and run on the board and on the host | `test/*.cpp` | host, board image compile, board run (manual) |
| Host-only suite | Storage with fault injection (write failures, power cuts), migrations, the replay counter | `host-tests/tests/*.cpp` | host |
| Sanitizers | Memory and undefined behaviour in the host build | `make -C host-tests SANITIZE=address,undefined test` | CI |
| Hardware | The same Unity suite and the release checklist on a board | `pio test -e waveshare_esp32s3_nano_usb` | manual, opt-in |

The host build compiles the shared suite, the outbox, the setup store and the sync client. It
replaces the Arduino, NVS, mbedtls, WiFi, mDNS and TCP APIs with stand-ins in `host-tests/shim/`.
The network stand-ins use a fake clock, so timeouts are deterministic. The shims
must not be more permissive than the real libraries. For example, the NVS getters are not
`const` on the device, so they are not `const` in the shim either.

## Commands

```bash
make -C host-tests test                              # build and run the host suites
make -C host-tests SANITIZE=address,undefined test   # the same, with sanitizers (what CI runs)
make -C host-tests coverage                          # line coverage of src/ and include/
make -C host-tests clean
```

The embedded image is compiled but not run by CI, and it is not uploaded:

```bash
pio test -e waveshare_esp32s3_nano_usb --without-uploading --without-testing
pio run -e waveshare_esp32s3_nano_usb
pio run -e waveshare_esp32s3_nano_ota
```

Running the assertions on a board uploads the image. That needs an explicit request.

## Adding a test

1. A test that uses only pure headers and Arduino `String` goes in `test/`. It then runs on
   the board as well. Register it with a `register…Tests()` function and call that from
   `setup()` in `test/test_inventatory_scan_core.cpp`.
2. A test that needs the NVS fault injection goes in `host-tests/tests/`. Declare it in
   `host-tests/tests/host_suite.cpp` and add a `RUN_TEST` line.
3. Each test starts from empty NVS (`setUp` resets it). Do not depend on another test.
4. Use the fake clock and fixed inputs. No real time, no sockets, no randomness without a seed.
5. For a bug fix, write the test first and check that it fails without the fix. Run the mutant
   by reverting the change in a scratch copy.
6. A test in `test/` runs on the board's 8 KiB Arduino loop stack. Keep its locals small. One
   `SyncResponse` is about half a kilobyte, and a test that kept several alive overflowed the
   stack and reset the board. To check a test's stack use, run `xtensa-esp32s3-elf-objdump -d`
   on its object under `.pio/build/waveshare_esp32s3_nano_usb/test/` and read the `entry a1, N`
   line. `N` is the frame size in bytes.

## Known defects

A confirmed defect that is not yet fixed is kept as a test marked
`TEST_IGNORE_MESSAGE("KNOWN DEFECT: …")`. It shows as ignored in every run, and the message
says what to remove once the defect is fixed. Do not weaken an assertion to make a defect
pass. The release branch must not contain an ignore without a tracking issue.

Currently known:

- `test_host_outbox_power_cut_during_repair_does_not_duplicate_events`: `repair()` rewrites the
  salvaged slots before it commits the header. A power cut in between can leave the old header
  pointing at a slot that now holds another event. This is an operator-confirmed repair of an
  already damaged queue, but the event that was overwritten is lost. The fix is a repair that
  writes the header first, or into free slots.

## Coverage

`make -C host-tests coverage` prints the best line coverage per file across the translation
units. At the time of writing, every host-compiled header is at 93.7–100%, `outbox_store.cpp` is
at 96.5%, and `setup_state_store.cpp` is at 100%. The remaining uncovered outbox lines are the
repair branches for a queue with nothing to salvage and some write-failure paths.

Compiled by no host test, so covered only by the board run:

- `gm65/gm65_scanner.cpp` (the serial driver; its frame logic is covered in `include/InventatoryScanGm65Frame.h`)
- `keypad/keypad.cpp` (its debounce and power-hold logic are not yet extracted)
- `setup/ble_provisioning.cpp` (its request parser is covered in `include/InventatoryScanBleRequest.h`)
- `app/*`, `display/*`, `power/*`, `storage/device_settings_store.cpp`

The network client, `net/inventatory_client.cpp`, is compiled on the host. Its tests run it
against a scripted server (`host-tests/shim/WiFiClient.h`), so the framing rules are checked
with real bytes and the real MAC helpers.

## Roadmap

Each step below is a separate change. The order follows the risk.

1. Keep the device build and the host suite green with each change. Both are required for a
   merge (see [CONTRIBUTING.md](../CONTRIBUTING.md)).
2. Extract the keypad debounce and the `*` power-off hold into pure modules, and test them with
   scripted scan sequences.
3. Done: the client runs on the host against a scripted server (`host-tests/tests/test_client_framing.cpp`).
4. Add a shared conformance corpus of sync requests and responses with their MACs, used by both
   this repository and the desktop repository. The desktop side must check the same vectors.
5. Add fuzzing for the sync response parser, the DataMatrix decoder, the GM65 frame assembler
   and the outbox header loader. Use the corpus as the seed.
6. Add a thread-sanitizer run of the network hand-off, once a FreeRTOS stand-in exists.
7. Write a release runbook for the board checks, with an evidence template for each gate.
