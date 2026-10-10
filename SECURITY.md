# Security policy

## Supported versions

Security fixes are provided for the latest released Inventatory Scan R1
firmware. Before reporting a problem, reproduce it against the current release
when doing so is safe and does not risk inventory data or a production device.

## Reporting a vulnerability

Use GitHub's **Security** tab and **Report a vulnerability** to open a private
security advisory for this repository. Do not include credentials, device
tokens, Wi-Fi passwords, private inventory data, or exploit details in a public
issue. If private vulnerability reporting is unavailable, ask the repository
maintainers for a private reporting channel without disclosing the issue.

Include the affected firmware version or commit, hardware revision, required
attacker access, reproduction steps, observed impact, and any relevant logs
after removing secrets and inventory identifiers. Do not test against devices,
networks, or data you do not own or have explicit permission to assess.

## Security boundary

The Scan R1 transport authenticates requests and responses and persists replay
counters. It does not encrypt payloads; deployment therefore assumes a trusted
private LAN in addition to protocol authentication. BLE provisioning requires
a passkey shown on the device, changed after every attempt, with a lockout that only a keypress on the device lifts, and stores the provisioned token in ESP32 NVS. Production
device policy for secure boot, flash encryption, JTAG, and eFuses is controlled
by manufacturing/provisioning and is not established by this source tree.

See the [shared transport v1 protocol specification](https://github.com/Kwiatens/Inventatory-Software/blob/main/docs/scanner-transport-security.md) for the
cross-device protocol and operational assumptions.
