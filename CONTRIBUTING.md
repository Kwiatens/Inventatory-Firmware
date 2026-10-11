<!-- SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski -->
<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Contributing to Inventatory Scan R1 Firmware

Thank you for contributing to the Inventatory Scan R1 firmware!

## Start with an issue

For a bug, open an issue with the steps to reproduce it, the firmware version or commit, and the hardware
revision. For a larger change, open an issue first so the approach can be agreed before you write the code.

Security problems never go in issues. Report them as described in [SECURITY.md](SECURITY.md).

## Build and test

This project targets the Waveshare ESP32-S3-Nano and uses PlatformIO. To set up your environment, copy the
example configuration (this must never be committed):
```bash
cp src/config/config.example.h src/config/config.h                   # Linux / macOS
```
```powershell
Copy-Item src/config/config.example.h src/config/config.h            # Windows
```

Use these commands to build the firmware targets:
```powershell
# Build standard USB firmware (default environment)
pio run -e waveshare_esp32s3_nano_usb

# Build isolated OTA firmware
pio run -e waveshare_esp32s3_nano_ota
```

Run the host suite first. It needs no hardware, and it runs the same Unity assertions the device runs, with
stand-ins for the Arduino, NVS and mbedtls APIs (see `host-tests/`). It needs a C++17 compiler, `make` and the
OpenSSL development headers:
```bash
make -C host-tests test                              # build and run
make -C host-tests SANITIZE=address,undefined test   # the same, with sanitizers (what CI runs)
```

The embedded Unity suite in `test/` also runs on the board. CI compiles that image but does not upload it or
run its assertions:
```powershell
pio test -e waveshare_esp32s3_nano_usb --without-uploading --without-testing
```
Running the assertions on a board is a manual step, which needs an attached Scan R1 and is recorded in the
pull request.

## Code

- Follow the code around your change and the ownership and invariant rules in [AGENTS.md](AGENTS.md).
- Source files (C, C++, Python) carry the SPDX header described in the
  [shared project standards](https://github.com/Kwiatens/Inventatory-Software/blob/main/docs/project-standards.md).

## Commits and pull requests

- Write each commit subject as `type(scope): subject`, with a scope from [AGENTS.md](AGENTS.md) section 12,
  72 characters or fewer. The shared rules are in the
  [shared project standards](https://github.com/Kwiatens/Inventatory-Software/blob/main/docs/project-standards.md).
- Keep one logical change per commit.
- **Testing**: The build and the Unity compile-only check must pass. State whether the change was tested on
  physical hardware.
- **Safety**: CI never flashes firmware to hardware or performs OTA updates. Do not add these steps to CI
  workflows.

## Data, secrets and logging

The firmware enforces a zero-sensitive-logging rule. Passwords, tokens, HMAC keys, raw UART buffers, and
decoded barcodes must never be logged. CI fails the build if any of these `git grep` checks matches. Run them
from the repository root before opening a pull request; each must print nothing (exit code 1):
```bash
git grep -n -E -e "GM65 rx|GM65 scan:" -- src
git grep -n -E -e "sendDebugMessage\(.*(scannedCode_|trimmed|forwardedCode|manufacturerPartNumber)" -- src
git grep -n -E -e "(Serial\.(print|println|printf)|sendDebugMessage)\(.*(scannedCode_|forwardedCode|wifiPassword|deviceToken|clientConfig_\.token|settings\.deviceToken)" -- src
```
The patterns in `.github/workflows/firmware.yml` are authoritative; keep this list in step with them.

Do not include secrets, credentials, or a modified `config.h` in a commit or pull request.

## Licensing

- The firmware is licensed under GPL-3.0-only; see [LICENSE](LICENSE). By contributing, you agree that your
  contribution is licensed under the same terms.
- New source files start with the SPDX header described in the shared standards.
- Bundled third-party code and artwork are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Add an
  entry when you bring in new code or artwork.

## Automated coding agents

[AGENTS.md](AGENTS.md) is the authoritative operating guide for autonomous coding agents. Read it, and follow
its boundaries, before proposing changes.
