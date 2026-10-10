## Summary
<!-- Describe the changes and the rationale behind them. -->

## Change Type
<!-- Choose one or more that apply -->
- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Documentation update
- [ ] Refactoring / Chore

## Pre-commit Checklist
<!-- Please review the following checklist from AGENTS.md section 14. -->
- [ ] **Authority**: Checked `README.md`, `SECURITY.md`, and `docs/release-audit-remediation.md`.
- [ ] **Autonomy**: No physical flashing, OTA deployment, or credential resets performed in CI.
- [ ] **Memory safety**: Zero dynamic `String` loops, bounded buffers, zero PSRAM respected.
- [ ] **Concurrency**: No display calls from Core 0 worker; no blocking network calls on Core 1.
- [ ] **Persistence**: NVS namespaces <= 15 chars; outbox pop staged before commit; pop failure retains event.
- [ ] **Durability**: Newly captured scans that fail NVS write show error banner and are not queued.
- [ ] **Security**: HMAC mutual auth, directional keys, monotonic counter reservation preserved.
- [ ] **Zero logging**: Passed CI grep rules (no raw barcodes, UART buffers, or secrets logged).
- [ ] **Verification**: `pio test ... --without-uploading --without-testing` and firmware builds pass.
- [ ] **Working tree**: No modified `config.h`, `.pio/`, `.bin` files, or unrelated user edits in diff.
- [ ] **Commit format**: Follows `type(scope): description` without AI attribution.

## Hardware Testing
<!-- Specify if these changes were tested on physical hardware -->
- [ ] Yes, tested on physical ESP32-S3 hardware
- [ ] No, compile-only / statically verified
