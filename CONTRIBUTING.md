<!-- SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski -->
<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Contributing to Inventatory Scan R1 Firmware

Thank you for contributing to the Inventatory Scan R1 firmware!

## Coding Agents
If you are an autonomous coding agent, `AGENTS.md` is your authoritative operating guide. You must read and follow its boundaries before proposing changes.

## Licensing
This project is licensed under GPL-3.0-only. All contributions must be under the same license.
New source files (C, C++, Python) must start with the SPDX headers:
`SPDX-FileCopyrightText: <year> <author>`
`SPDX-License-Identifier: GPL-3.0-only`
Documentation and configuration files are covered by the repository `LICENSE`.

## Security and Vulnerabilities
Report suspected vulnerabilities privately as described in `SECURITY.md`. **Never** open a public issue or pull request for a security vulnerability.

## Building the Firmware
This project targets the Tenstar ESP32-S3-Zero and uses PlatformIO. 
To set up your environment, copy the example configuration (this must never be committed):
```bash
cp src/config/config.example.h src/config/config.h                   # Linux / macOS
```
```powershell
Copy-Item src/config/config.example.h src/config/config.h            # Windows
```

Use these commands to build the firmware targets:
```powershell
# Build standard USB firmware (default environment)
pio run -e tenstar_esp32s3_zero_usb

# Build isolated OTA firmware
pio run -e tenstar_esp32s3_zero_ota
```

## Testing
Run the host suite first. It needs no hardware, and it runs the same Unity assertions the device runs, with stand-ins for the Arduino, NVS and mbedtls APIs (see `host-tests/`). It needs a C++17 compiler, `make` and the OpenSSL development headers:
```bash
make -C host-tests test                              # build and run
make -C host-tests SANITIZE=address,undefined test   # the same, with sanitizers (what CI runs)
```

The embedded Unity suite in `test/` also runs on the board. CI compiles that image but does not upload it or run its assertions:
```powershell
pio test -e tenstar_esp32s3_zero_usb --without-uploading --without-testing
```
Running the assertions on a board is a manual step, which needs an attached Scan R1 and is recorded in the pull request.

## Zero Sensitive Logging
The firmware enforces a zero-sensitive-logging rule. Passwords, tokens, HMAC keys, raw UART buffers, and decoded barcodes must never be logged. CI fails the build if any of these `git grep` checks matches. Run them from the repository root before opening a pull request; each must print nothing (exit code 1):
```bash
git grep -n -E -e "GM65 rx|GM65 scan:" -- src
git grep -n -E -e "sendDebugMessage\(.*(scannedCode_|trimmed|forwardedCode|manufacturerPartNumber)" -- src
git grep -n -E -e "(Serial\.(print|println|printf)|sendDebugMessage)\(.*(scannedCode_|forwardedCode|wifiPassword|deviceToken|clientConfig_\.token|settings\.deviceToken)" -- src
```
The patterns in `.github/workflows/firmware.yml` are authoritative; keep this list in step with them.

## Code of Conduct
Participation is governed by [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## Commit Message Format
Follow conventional commit formatting:
`type(scope): subject`

- **Allowed types**: `feat`, `fix`, `refactor`, `perf`, `style`, `test`, `docs`, `build`, `ci`, `chore`, `revert`.
- **Allowed scopes**: `app`, `net`, `scan`, `display`, `keypad`, `storage`, `setup`, `security`, `ota`, `build`, `ci`, `docs`.
- Formatting: Imperative present tense, lowercase start, no trailing period, 72 characters or fewer.

## Pull Request Expectations
- **Testing**: Ensure the build and Unity compile-only checks pass. State whether changes were tested on physical hardware.
- **Safety**: CI never flashes firmware to hardware or performs OTA updates. Do not attempt to add these steps to CI workflows.
- **Secrets**: Do not include secrets, credentials, or modified `config.h` files in your PR.
