# Security policy

## Supported versions

Security fixes are provided for the latest released Inventatory Scanner R1
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

Over-the-air updates are signed (docs/ota-protocol.md). The scanner installs an update only if an ECDSA P-256
signature by a key in `src/config/ota_keys.h` covers its header, the header names this product and hardware,
its security counter is not below the scanner's minimum, and the downloaded image matches the signed SHA-256.
The boot partition changes only after all of that, and only after a key press on the scanner. The desktop and
the network are only couriers: a host that intercepts or answers in the desktop's place can stop an update,
but cannot install its own firmware. The owner signs releases on their own PC; the private key is never in
CI. A build with no trusted key accepts no update over Wi-Fi.

This does not lock the hardware. There is no Secure Boot, so anyone with the device and a USB cable can flash
any image, and the signature check runs only in the firmware that receives the update. The security counter
stops the reinstall of an older signed release with a known flaw only after a release that raised it has
been confirmed on the device. If the signing key leaks, ship a release that trusts a new key in the second
slot and drops the old one.

Other assumptions on the local network: the desktop is found by mDNS, so any host can answer in its place.
It then receives the scan data, which is not encrypted, but it cannot forge a response without the pairing
token. The device log is kept on the USB serial port in a release; its broadcast to the network is a bench
option (`kLogOverAirEnabled`) that any host on the network can read.

For a device that may be lost or handed on, enable ESP32 flash encryption and NVS encryption when it is
provisioned: without them, the Wi-Fi password and the pairing token can be read from the flash chip. Sync the
queue before handing a device on: "Pair with new PC" keeps queued scans, which then go to the new PC.

Out of scope: vulnerabilities in third-party components, such as U8g2, Unity or
the ESP32 Arduino core. Report those upstream, and tell us as well if this
firmware makes them worse.

The cross-device protocol and its operational assumptions are described in the
[shared transport v1 protocol specification](https://github.com/Kwiatens/Inventatory-Software/blob/main/docs/scanner-transport-security.md)
in the application repository.
