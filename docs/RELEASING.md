# Releasing

This document contains the checklist for cutting a release for the Inventatory Scan R1 firmware. Note that releasing is a manual process and is not automated.

## Pre-Release Checklist

- [ ] **Version Bump**: Update `kFirmwareVersion` in `src/config/version.h` to the new version.
- [ ] **Changelog**: Update `CHANGELOG.md` with the new version and release date.
- [ ] **Builds**: Run all firmware builds to ensure successful compilation:
  - `pio run -e tenstar_esp32s3_zero_usb`
  - `pio run -e tenstar_esp32s3_zero_ota`
- [ ] **Hardware Testing**: Run Unity assertions on physical hardware to fulfill the open release gates listed in `docs/release-audit-remediation.md`:
  - `pio test -e tenstar_esp32s3_zero_usb`
- [ ] **Secret Scan**: Perform a secret scan of the commit history to ensure no credentials or tokens were accidentally committed.
- [ ] **Tagging**: Create a git tag for the release in the format `vX.Y.Z`.
- [ ] **Release Artifacts**: Build `firmware.bin` from a clean checkout of the tagged commit with a placeholder-free private `config.h` that contains no real secrets, and attach it with its SHA-256 sum. CI does not currently upload artifacts, so this step is manual.
- [ ] **Safety**: Ensure there is no flashing in CI.

## Repository settings checklist (maintainer, on GitHub)

These settings are not stored in the repository and must be set by an owner
before or right after the repository is made public:

- [ ] Add a repository description and topics (for example `esp32-s3`,
      `platformio`, `barcode-scanner`, `inventory`, `firmware`).
- [ ] Enable **Private vulnerability reporting** (Settings, Code security),
      because `SECURITY.md` points reporters to it.
- [ ] Enable Dependabot alerts and security updates, secret scanning and push
      protection.
- [ ] Protect `main`: require pull requests and the `Firmware CI` build job,
      disallow force pushes and branch deletion.
- [ ] Set the default workflow token permission to read-only and require
      approval for workflows from first-time contributors (Settings, Actions).
- [ ] Do not register a self-hosted runner for a public repository unless
      pull request workflows from forks are disabled for it; the optional
      hardware test job only runs on manual dispatch.
- [ ] Confirm the community profile (`/community`) shows every item complete.
