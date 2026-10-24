#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
# SPDX-License-Identifier: GPL-3.0-only
"""Create the signing key, sign a firmware image, and check a signed package.

The package format is in docs/ota-protocol.md. Signing happens on the owner's PC, never in CI.
The private key is a passphrase-protected PEM file; openssl asks for the passphrase.

Usage, from the repository root:

    python3 tools/sign_firmware.py keygen --out ~/inventatory-ota-key.pem
    python3 tools/sign_firmware.py pubkey --key ~/inventatory-ota-key.pem
    python3 tools/sign_firmware.py sign --key ~/inventatory-ota-key.pem --image firmware.bin --out firmware.ota
    python3 tools/sign_firmware.py verify --package firmware.ota

`sign` reads the version, product, hardware and security counter from the identity string inside the
image (src/config/version.h), so a package always describes the image it carries.
`verify` checks against the keys in src/config/ota_keys.h unless --pubkey is given.

Needs only Python 3 and the openssl command (OpenSSL 1.1.1 or later).
"""

import argparse
import base64
import hashlib
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEYS_HEADER = ROOT / "src" / "config" / "ota_keys.h"

MAGIC = b"IVOT"
FORMAT = 1
HEADER_BYTES = 256
SIGNATURE_BYTES = 64
PREFIX_BYTES = HEADER_BYTES + SIGNATURE_BYTES
TEXT_BYTES = 32
KEY_ID_BYTES = 8
# The app partitions in partitions.csv; the scanner refuses a larger image anyway.
PARTITION_BYTES = 0x190000
# First byte of an ESP32 app image.
ESP_IMAGE_MAGIC = 0xE9
# Order of the P-256 group, for the low-S form and the DER range check.
P256_ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551


class SignError(Exception):
    pass


def openssl(args, stdin=None, passin=None):
    command = ["openssl"] + args
    if passin:
        command += ["-passin", passin]
    result = subprocess.run(command, input=stdin, capture_output=True)
    if result.returncode != 0:
        message = result.stderr.decode("utf-8", "replace").strip()
        raise SignError(f"openssl {args[0]} failed: {message}")
    return result.stdout


def read_image_identity(image):
    """Reads version, product, hardware and security counter from the identity string the firmware carries
    (INVENTATORY_FIRMWARE_IDENTITY in src/config/version.h), so the header always describes this image."""
    matches = re.findall(rb"IVFWID1;([\x20-\x7e]*?)\x00", image)
    if len(matches) != 1:
        raise SignError("the image carries no firmware identity, or more than one; build it from this repository")
    values = {}
    for item in matches[0].decode("ascii").split(";"):
        if item:
            name, _, value = item.partition("=")
            values[name] = value
    if set(values) != {"version", "product", "hardware", "counter"} or not values["counter"].isdigit():
        raise SignError(f"the firmware identity is malformed: {matches[0].decode('ascii')}")
    values["counter"] = int(values["counter"])
    return values


def read_trusted_keys(path=KEYS_HEADER):
    text = path.read_text(encoding="utf-8")
    keys = []
    for name in ("kOtaTrustedKey0", "kOtaTrustedKey1"):
        match = re.search(r"constexpr\s+char\s+" + name + r'\[\]\s*=\s*"([0-9a-fA-F]*)"\s*;', text)
        if not match:
            raise SignError(f"{name} not found in {path}")
        if match.group(1):
            keys.append(parse_public_key_hex(match.group(1)))
    return keys


def parse_public_key_hex(text):
    try:
        key = bytes.fromhex(text.strip())
    except ValueError as error:
        raise SignError("a public key must be hex") from error
    if len(key) != 65 or key[0] != 0x04:
        raise SignError("a public key must be 130 hex characters of an uncompressed P-256 point (starting 04)")
    return key


def key_id(public_key):
    return hashlib.sha256(public_key).digest()[:KEY_ID_BYTES]


def text_field(value, name):
    data = value.encode("ascii", "strict") if isinstance(value, str) else value
    if not data or len(data) >= TEXT_BYTES or any(byte < 0x20 or byte > 0x7E for byte in data):
        raise SignError(f"{name} must be 1 to {TEXT_BYTES - 1} printable ASCII characters")
    return data.ljust(TEXT_BYTES, b"\0")


def build_header(public_key, product, hardware, version, security_counter, image_size, image_sha256,
                 reserved=b""):
    """Builds the 256-byte header. `reserved` exists only for test vectors; real headers leave it zero."""
    if not 0 <= security_counter <= 0xFFFFFFFF or not 0 <= image_size <= 0xFFFFFFFF:
        raise SignError("security counter and image size must fit in 32 bits")
    header = (MAGIC + struct.pack("<HH", FORMAT, HEADER_BYTES) + key_id(public_key) +
              text_field(product, "product") + text_field(hardware, "hardware") +
              text_field(version, "version") + struct.pack("<II", security_counter, image_size) + image_sha256)
    header += reserved.ljust(HEADER_BYTES - len(header), b"\0")
    assert len(header) == HEADER_BYTES
    return header


def der_signature_to_raw(der):
    """Converts an ASN.1 DER ECDSA signature to raw r ‖ s, 32 bytes each, in low-S form."""
    def read_length(data, offset):
        first = data[offset]
        if first < 0x80:
            return first, offset + 1
        count = first & 0x7F
        if count == 0 or count > 2:
            raise SignError("unexpected DER length")
        return int.from_bytes(data[offset + 1:offset + 1 + count], "big"), offset + 1 + count

    if not der or der[0] != 0x30:
        raise SignError("unexpected DER signature")
    length, offset = read_length(der, 1)
    if offset + length != len(der):
        raise SignError("unexpected DER signature length")
    values = []
    for _ in range(2):
        if der[offset] != 0x02:
            raise SignError("unexpected DER integer")
        length, offset = read_length(der, offset + 1)
        values.append(int.from_bytes(der[offset:offset + length], "big"))
        offset += length
    r, s = values
    if not (0 < r < P256_ORDER and 0 < s < P256_ORDER):
        raise SignError("signature value out of range")
    if s > P256_ORDER // 2:
        s = P256_ORDER - s
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def raw_signature_to_der(raw):
    def integer(value):
        data = value.to_bytes(32, "big").lstrip(b"\0") or b"\0"
        if data[0] & 0x80:
            data = b"\0" + data
        return b"\x02" + bytes([len(data)]) + data

    body = integer(int.from_bytes(raw[:32], "big")) + integer(int.from_bytes(raw[32:], "big"))
    return b"\x30" + bytes([len(body)]) + body


def public_key_from_private(key_path, passin=None):
    der = openssl(["pkey", "-in", str(key_path), "-pubout", "-outform", "DER"], passin=passin)
    # A P-256 SubjectPublicKeyInfo ends with the 65-byte uncompressed point.
    point = der[-65:]
    if len(der) != 91 or point[0] != 0x04:
        raise SignError("the key is not a P-256 key")
    return point


def public_key_pem(public_key):
    # SubjectPublicKeyInfo prefix for id-ecPublicKey with prime256v1.
    prefix = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d030107034200")
    der = prefix + public_key
    body = base64.encodebytes(der).decode("ascii").replace("\n", "")
    lines = [body[index:index + 64] for index in range(0, len(body), 64)]
    return ("-----BEGIN PUBLIC KEY-----\n" + "\n".join(lines) + "\n-----END PUBLIC KEY-----\n").encode("ascii")


def sign_header(key_path, header, passin=None):
    der = openssl(["dgst", "-sha256", "-sign", str(key_path)], stdin=header, passin=passin)
    return der_signature_to_raw(der)


def verify_signature(public_key, header, signature):
    with tempfile.TemporaryDirectory() as folder:
        pem = Path(folder) / "public.pem"
        sig = Path(folder) / "signature.der"
        pem.write_bytes(public_key_pem(public_key))
        sig.write_bytes(raw_signature_to_der(signature))
        result = subprocess.run(["openssl", "dgst", "-sha256", "-verify", str(pem), "-signature", str(sig)],
                                input=header, capture_output=True)
        return result.returncode == 0


def build_package(key_path, image, product, hardware, version, security_counter, passin=None):
    if len(image) == 0 or len(image) > PARTITION_BYTES:
        raise SignError(f"the image must be 1 to {PARTITION_BYTES} bytes, it is {len(image)}")
    if image[0] != ESP_IMAGE_MAGIC:
        raise SignError("this is not an ESP32 app image (first byte is not 0xE9); sign firmware.bin")
    public_key = public_key_from_private(key_path, passin)
    header = build_header(public_key, product, hardware, version, security_counter, len(image),
                          hashlib.sha256(image).digest())
    signature = sign_header(key_path, header, passin)
    if not verify_signature(public_key, header, signature):
        raise SignError("the new signature did not verify; nothing was written")
    return header + signature + image, public_key


def check_package(package, trusted_keys):
    """Returns a dict of the header fields; raises SignError on the first failed check."""
    if len(package) < PREFIX_BYTES:
        raise SignError("bad_header: the file is shorter than the header and signature")
    header, signature, image = package[:HEADER_BYTES], package[HEADER_BYTES:PREFIX_BYTES], package[PREFIX_BYTES:]
    magic, file_format, header_size = header[:4], *struct.unpack("<HH", header[4:8])
    if magic != MAGIC or file_format != FORMAT or header_size != HEADER_BYTES:
        raise SignError("bad_header: not a format 1 Inventatory update package")
    matching = [key for key in trusted_keys if key_id(key) == header[8:16]]
    if not matching:
        raise SignError(f"bad_signature: signed by key id {header[8:16].hex()}, which is not trusted")
    if not verify_signature(matching[0], header, signature):
        raise SignError("bad_signature: the signature does not verify")
    if any(header[152:]):
        raise SignError("bad_header: reserved bytes are not zero")
    fields = {}
    for name, offset in (("product", 16), ("hardware", 48), ("version", 80)):
        raw = header[offset:offset + TEXT_BYTES]
        text = raw.split(b"\0", 1)[0]
        if not text or len(text) == TEXT_BYTES or raw[len(text):].strip(b"\0"):
            raise SignError(f"bad_header: the {name} field is malformed")
        fields[name] = text.decode("ascii")
    fields["security_counter"], fields["image_size"] = struct.unpack("<II", header[112:120])
    fields["image_sha256"] = header[120:152].hex()
    fields["key_id"] = header[8:16].hex()
    if len(image) != fields["image_size"]:
        raise SignError(f"download_failed: the image is {len(image)} bytes, the header says {fields['image_size']}")
    if hashlib.sha256(image).hexdigest() != fields["image_sha256"]:
        raise SignError("hash_mismatch: the image does not match the signed hash")
    return fields


def passin_argument(args):
    return f"env:{args.passphrase_env}" if args.passphrase_env else None


def command_keygen(args):
    out = Path(args.out).expanduser()
    if out.exists():
        raise SignError(f"{out} exists; refusing to overwrite a key")
    command = ["genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-aes-256-cbc", "-out", str(out)]
    if args.passphrase_env:
        command += ["-pass", f"env:{args.passphrase_env}"]
    old_umask = os.umask(0o077)
    try:
        # Not captured, so openssl can ask for the passphrase on the terminal.
        if subprocess.run(["openssl"] + command).returncode != 0:
            raise SignError("openssl genpkey failed")
    finally:
        os.umask(old_umask)
    public_key = public_key_from_private(out, passin_argument(args))
    print(f"Private key written to {out}. Keep it and its passphrase safe; never commit it.")
    print_public_key(public_key)


def print_public_key(public_key):
    print(f"Key id: {key_id(public_key).hex()}")
    print("Public key for src/config/ota_keys.h:")
    print(public_key.hex())


def command_pubkey(args):
    print_public_key(public_key_from_private(Path(args.key).expanduser(), passin_argument(args)))


def command_sign(args):
    image = Path(args.image).read_bytes()
    identity = read_image_identity(image)
    package, public_key = build_package(Path(args.key).expanduser(), image, identity["product"],
                                        identity["hardware"], identity["version"], identity["counter"],
                                        passin_argument(args))
    out = Path(args.out)
    if out.exists() and not args.force:
        raise SignError(f"{out} exists; pass --force to replace it")
    out.write_bytes(package)
    print(f"Signed {args.image} ({len(image)} bytes, SHA-256 {hashlib.sha256(image).hexdigest()})")
    print(f"{identity['product']} on {identity['hardware']}, version {identity['version']}, "
          f"security counter {identity['counter']}, key id {key_id(public_key).hex()}")
    print(f"Wrote {out} ({len(package)} bytes)")


def command_verify(args):
    keys = [parse_public_key_hex(args.pubkey)] if args.pubkey else read_trusted_keys()
    if not keys:
        raise SignError("no trusted key: src/config/ota_keys.h has none, and --pubkey was not given")
    fields = check_package(Path(args.package).read_bytes(), keys)
    print(f"OK: {fields['product']} on {fields['hardware']}, version {fields['version']}, "
          f"security counter {fields['security_counter']}, {fields['image_size']} bytes, key id {fields['key_id']}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    def add_passphrase(command):
        command.add_argument("--passphrase-env", metavar="VAR",
                             help="read the key passphrase from this environment variable instead of asking")

    keygen = sub.add_parser("keygen", help="create a passphrase-protected P-256 signing key")
    keygen.add_argument("--out", required=True)
    add_passphrase(keygen)
    keygen.set_defaults(handler=command_keygen)

    pubkey = sub.add_parser("pubkey", help="print the public key and key id of a signing key")
    pubkey.add_argument("--key", required=True)
    add_passphrase(pubkey)
    pubkey.set_defaults(handler=command_pubkey)

    sign = sub.add_parser("sign", help="sign firmware.bin into a .ota package")
    sign.add_argument("--key", required=True)
    sign.add_argument("--image", required=True)
    sign.add_argument("--out", required=True)
    sign.add_argument("--force", action="store_true", help="replace an existing output file")
    add_passphrase(sign)
    sign.set_defaults(handler=command_sign)

    verify = sub.add_parser("verify", help="check a .ota package as the scanner would")
    verify.add_argument("--package", required=True)
    verify.add_argument("--pubkey", help="130 hex characters; default: the keys in src/config/ota_keys.h")
    verify.set_defaults(handler=command_verify)

    args = parser.parse_args(argv)
    try:
        args.handler(args)
    except (SignError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
