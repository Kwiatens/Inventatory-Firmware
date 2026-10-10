<!-- SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski -->
<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Contributing to Inventatory Scan R1 Firmware

Thank you for contributing to the Inventatory Scan R1 firmware!

## Coding Agents
If you are an autonomous coding agent, `AGENTS.md` is your authoritative operating guide. You must read and follow its boundaries before proposing changes.

## Licensing
This project is licensed under GPL-3.0-only. All contributions must be under the same license.
New files must include the SPDX headers:
`SPDX-FileCopyrightText: <year> <author>`
`SPDX-License-Identifier: GPL-3.0-only`

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
The embedded Unity assertions require physical hardware. Our CI runs a compile-only check:
```powershell
pio test -e tenstar_esp32s3_zero_usb --without-uploading --without-testing
```
This check verifies syntax and linkage but does not run runtime tests.

## Zero Sensitive Logging
The firmware enforces a zero-sensitive-logging rule. Passwords, tokens, HMAC keys, raw UART buffers, and decoded barcodes must never be logged. CI fails the build if any of these `git grep` checks matches (the authoritative patterns live in `.github/workflows/firmware.yml`; run them locally before opening a pull request):
```bash
git grep -n -E "GM65 rx|GM65 scan:" src
git grep -n -E "(Serial\.(print|println|printf)|sendDebugMessage)\(.*(scannedCode_|forwardedCode|wifiPassword|deviceToken|clientConfig_\.token)" src
```
Both commands must print nothing (exit code 1).

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
