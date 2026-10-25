# Signed firmware updates over Wi-Fi

This page defines the signed update package and the pull protocol that the scanner uses to
fetch it from the desktop app. The desktop app (Inventatory-Software) and the test server in
`tools/ota_serve.py` implement the server side. The scanner implements the client side in
`src/ota/` and `include/InventatoryScanFirmwarePackage.h`.

## Trust model

- The scanner is DIY hardware. It has no Secure Boot and no eFuses are burned. USB flashing
  always works and installs any image.
- An update over Wi-Fi is installed only if it is signed by a key that the running firmware
  trusts. The trusted public keys are compiled in from `src/config/ota_keys.h`, two slots, so a
  key can be rotated. A build with no key has no Wi-Fi updates at all. There is no unsigned mode.
- Official releases are signed by the owner on their own PC with `tools/sign_firmware.py`. The
  private key never goes to CI or GitHub.
- The desktop app is only a courier. It is authenticated by the existing pairing token, but the
  scanner does not trust it for the image: a desktop that serves a wrong image is caught by the
  signature and the image hash.
- People who build their own firmware put their own public key in `ota_keys.h`, sign their own
  packages, and flash their build once over USB.

## Package format (`.ota`, format 1)

All integers are little-endian. Text fields are ASCII, printable (0x20 to 0x7e), terminated by
NUL inside the field, and padded with zero bytes to the field size.

| Offset | Size | Field | Rule on the scanner |
| --- | --- | --- | --- |
| 0 | 4 | magic `IVOT` | must match |
| 4 | 2 | format, `1` | must be 1 |
| 6 | 2 | header size, `256` | must be 256 |
| 8 | 8 | key id | must name a trusted key (first 8 bytes of SHA-256 of the 65-byte public key) |
| 16 | 32 | product, `Inventatory-Scanner-R1` | must equal the running firmware's product |
| 48 | 32 | hardware, `waveshare-esp32s3-nano` | must equal the running firmware's hardware |
| 80 | 32 | version, for example `0.2.0` | shown on the LCD; not compared |
| 112 | 4 | security counter | must be at or above the scanner's minimum |
| 116 | 4 | image size in bytes | 1 to the update partition size |
| 120 | 32 | SHA-256 of the image | the downloaded image must match it |
| 152 | 104 | reserved | must be zero |
| 256 | 64 | signature | ECDSA P-256 over SHA-256 of bytes 0 to 255, raw `r ‖ s`, 32 bytes each |
| 320 | image size | `firmware.bin` | the app image as `pio run` writes it |

The signature covers the header, and the header holds the image hash, so one signature covers
the whole package.

`tools/sign_firmware.py` takes the product, hardware, version and security counter from the
identity string the image carries (`IVFWID1;version=…;product=…;hardware=…;counter=…;`, built from
`src/config/version.h`), so the header cannot describe a different build than the image.

### Security counter

The counter stops an attacker from installing an older signed release that has a known flaw.
Raise it in `src/config/version.h` (`INVENTATORY_FIRMWARE_SECURITY_COUNTER`) only when a release fixes a
security problem. Ordinary releases keep the same counter, so downgrading to them stays possible.

The scanner's minimum is the larger of the running firmware's own counter and the value stored
in NVS. The stored value is raised to the running counter once the new image is confirmed
healthy (the rollback confirmation in `src/ota/firmware_validity.cpp`). An image that rolled
back never raises it.

## Pull protocol

The scanner pulls. The desktop never pushes an image. Every request and response uses the
existing authenticated transport: `POST`, the headers `X-Inventatory-Protocol`,
`X-Inventatory-Device`, `X-Inventatory-Counter`, `X-Inventatory-Mac`, a `Content-Length`, no
chunked encoding, and `Connection: close`. The request and response MACs are computed exactly
as for `/api/v1/device/sync` (`deviceRequestMac`, `deviceResponseMac` in
`include/InventatoryScanSecurity.h`). For a binary response the MAC input is the raw body bytes.
A response with a missing or wrong MAC is treated as no response.

### How an update starts

1. The user picks an update in the desktop app. The desktop downloads the `.ota` asset from the
   GitHub release, checks its signature against the keys it knows, and marks the package as
   offered to that scanner. The next sync response then carries `"firmwareOffer": true`. This
   flag is a hint only: it carries nothing the scanner trusts.
2. The scanner never starts an update on its own. While the hint is set, its Ready screen shows
   "Update available" and `D Update`. The user presses D there, or opens Menu, Device settings,
   Software update (C, 2, 4) at any time.
3. The scanner then calls `offer`, checks the header and the signature, and shows the new
   version. It installs only after a key press on the scanner (A installs, C declines, no answer
   in 60 s declines). Every update needs that press.

The mention gives way to a standing problem or queued scans, and shows only while the desktop
is reachable. After an offer was declined, refused or empty, the scanner hides the mention until
it restarts, so a desktop that keeps the flag set does not keep advertising it. Starting an
update from the menu asks again at once.

### `POST /api/v1/device/firmware/offer`

Request body:

```json
{"product":"Inventatory-Scanner-R1","hardware":"waveshare-esp32s3-nano","version":"0.1.0",
 "securityCounter":1,"keyIds":["0123456789abcdef"]}
```

`keyIds` lists the ids of the keys the scanner trusts, so the desktop can tell the user when its
package will be refused. The desktop must not rely on it for security.

Response `200`, an update is offered:

```json
{"available":true,"package":"<640 lowercase hex characters: header and signature>"}
```

Response `200`, nothing is offered: `{"available":false}`.

### `POST /api/v1/device/firmware/chunk`

Request body:

```json
{"imageSha256":"<64 lowercase hex characters>","offset":0,"length":8192}
```

`offset` counts from the first byte of the image, not of the package. `length` is 1 to 8192, and
`offset + length` is at most the image size. The scanner asks for the chunks in order.

| Status | Body | Meaning |
| --- | --- | --- |
| `200` | exactly `length` image bytes, `Content-Type: application/octet-stream` | the chunk |
| `404` | `{"error":"no_offer"}` | no package with this hash is offered any more |
| `416` | `{"error":"bad_range"}` | the range is outside the image |

Any other answer, a timeout or a wrong MAC fails the chunk. The scanner retries a failed chunk
twice. Then it stops and keeps the running firmware.

### `POST /api/v1/device/firmware/status`

The scanner reports the outcome, so the desktop can show it. This is best effort: a lost status
changes nothing on the scanner.

```json
{"imageSha256":"<64 hex>","state":"installing","error":"","reason":""}
```

`state` is one of `declined`, `installing` (the user pressed the key), `installed` (hash
matched, the scanner restarts into the new image), `failed`. For `failed`, `error` is the LCD
code (`OTA-01`, or empty for `battery_low`) and `reason` one of the reasons below. When the
header itself was refused, `imageSha256` is all zeros, since the scanner trusts no field of
it. Response `200` with `{}`.

After the restart the next sync request reports the new `firmwareVersion`. If the new image
does not confirm within its first run, the bootloader returns to the old one, and the sync shows
the old version.

## Checks on the scanner, in order

| Step | Check | Reason on failure | LCD |
| --- | --- | --- | --- |
| 1 | header is 320 bytes and well formed | `bad_header` | OTA-01 |
| 2 | key id is trusted and the signature verifies | `bad_signature` | OTA-01 |
| 3 | product and hardware match | `wrong_device` | OTA-01 |
| 4 | security counter at or above the minimum | `older_release` | OTA-01 |
| 5 | image fits the inactive partition | `too_large` | OTA-01 |
| 6 | battery at 30 % or more, or charging | `battery_low` | warning, no code |
| 7 | user presses A | (`declined`) | none |
| 8 | every chunk arrives | `download_failed` | OTA-01 |
| 9 | SHA-256 of the written image matches the header, before the boot partition changes | `hash_mismatch` | OTA-01 |

An authenticated answer that is not a valid offer counts as `bad_header`. No offer, or no
answer from the desktop within 120 s, shows OTA-02. In every failure the boot
partition is not changed, so the scanner keeps the running firmware. A power cut or lost Wi-Fi
during the download leaves the old image too.

## Release flow

1. Raise the version (and, for a security fix, the security counter) in `src/config/version.h`.
2. Tag the release. CI builds `firmware.bin` and publishes its SHA-256.
3. Download `firmware.bin`, check its SHA-256 against the CI log, then sign it on your own PC:
   `python3 tools/sign_firmware.py sign --key <private key> --image firmware.bin --out firmware.ota`.
4. Attach `firmware.ota` to the GitHub release.

To test without the desktop app: `python3 tools/ota_serve.py --package firmware.ota --token
<pairing token> --device <device id>`. It answers the three endpoints above and a minimal sync,
so a paired scanner on the same network can pull the update.
