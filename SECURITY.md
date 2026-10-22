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

## Scope

In scope:

- The Scan R1 transport: request signing, response verification, replay
  counters, and the response size limits.
- BLE provisioning: the passkey, the lockout after failed attempts, and storage
  of the provisioned token.
- Over-the-air update checks and rollback.

Known limitation: the transport authenticates requests and responses and
detects replay, but it does not encrypt payloads. Deployment therefore assumes a
trusted private LAN as well as protocol authentication. BLE provisioning requires
a passkey shown on the device, changed after every attempt, with a lockout that
only a keypress on the device lifts. The provisioned token is stored in ESP32
NVS. Production device policy for secure boot, flash encryption, JTAG, and eFuses
is set by manufacturing and provisioning, and this source tree does not establish
it.

Over-the-air updates: an update is accepted only after the operator asks for one on the keypad, only for
two minutes, and only with the private `OTA_PASSWORD` from `config.h`. The firmware refuses OTA while that
password is the example placeholder or shorter than 12 characters. ArduinoOTA checks the password with a
challenge and response; the firmware image itself is not signed, so anyone who knows the password and is on
the same network during that window can install firmware. Use a long, random password, keep it out of
release builds you share, and update on a network you trust.

Other assumptions on the local network: the desktop is found by mDNS, so any host can answer in its place.
It then receives the scan data, which is not encrypted, but it cannot forge a response without the pairing
token. The device log is kept on the USB serial port in a release; its broadcast to the network is a bench
option (`kLogOverAirEnabled`) that any host on the network can read.

For a device that may be lost or handed on, enable ESP32 flash encryption and NVS encryption when it is
provisioned: without them, the Wi-Fi password and the pairing token can be read from the flash chip.

Out of scope: vulnerabilities in third-party components, such as U8g2, Unity or
the ESP32 Arduino core. Report those upstream, and tell us as well if this
firmware makes them worse.

The cross-device protocol and its operational assumptions are described in the
[shared transport v1 protocol specification](https://github.com/Kwiatens/Inventatory-Software/blob/main/docs/scanner-transport-security.md)
in the application repository.
