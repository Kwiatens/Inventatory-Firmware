#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
# SPDX-License-Identifier: GPL-3.0-only
"""Serve a signed .ota package to a paired scanner, for testing without the desktop app.

It answers the pull protocol in docs/ota-protocol.md (offer, chunk, status) and a minimal sync,
with the same request and response MACs as the desktop. The sync accepts no events, so scans the
scanner has queued stay queued until the desktop is back. Close the desktop app first: both answer
the same mDNS service.

Usage, from the repository root:

    python3 tools/ota_serve.py --package firmware.ota --device Inventatory-SCANNER-R1 --token <pairing token>

The pairing token is the 64 hex characters the desktop shows for the scanner. Pass it with
--token-env VAR to keep it out of the shell history. The package is checked against
src/config/ota_keys.h first (or --pubkey); --no-verify serves it anyway, to test the scanner's
refusals. --mdns publishes the service with avahi-publish-service; without it, set FALLBACK_HOST
and FALLBACK_PORT in config.h.
"""

import argparse
import hashlib
import hmac
import json
import os
import re
import shutil
import signal
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sign_firmware as sf  # noqa: E402

PROTOCOL_VERSION = 1
MAX_BODY_BYTES = 8192
MAX_CHUNK_BYTES = 8192
TOKEN_PATTERN = re.compile(r"^[0-9a-fA-F]{64}$")


def direction_key(token, direction):
    return hmac.new(bytes.fromhex(token), f"Inventatory Scanner R1 transport v1 {direction}".encode(), "sha256").digest()


def request_mac(token, method, path, device_id, counter, body):
    message = f"Inventatory Scanner R1/v1\nrequest\n{method}\n{path}\n{device_id}\n{counter}\n".encode() + body
    return hmac.new(direction_key(token, "client-to-server"), message, "sha256").hexdigest()


def response_mac(token, counter, status, body):
    message = f"Inventatory Scanner R1/v1\nresponse\n{counter}\n{status}\n".encode() + body
    return hmac.new(direction_key(token, "server-to-client"), message, "sha256").hexdigest()


class OtaState:
    def __init__(self, package, token, device_id):
        self.prefix = package[:sf.PREFIX_BYTES]
        self.image = package[sf.PREFIX_BYTES:]
        # The offer is named by the hash in the signed header, as the desktop does, not by the bytes served:
        # a package with a changed image byte (--no-verify) must still download, so the scanner reports
        # hash_mismatch.
        self.image_sha256 = self.prefix[120:152].hex()
        self.token = token
        self.device_id = device_id
        self.last_counter = 0
        # Stops hinting once the scanner has installed or declined, so it is not asked again.
        self.offering = True


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    state = None

    def log_message(self, fmt, *args):
        pass

    def reply(self, status, body, counter, content_type="application/json"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Inventatory-Protocol", str(PROTOCOL_VERSION))
        self.send_header("X-Inventatory-Counter", str(counter))
        self.send_header("X-Inventatory-Mac", response_mac(self.state.token, counter, status, body))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def reply_json(self, status, value, counter):
        self.reply(status, json.dumps(value, separators=(",", ":")).encode(), counter)

    def refuse(self, reason):
        # Unauthenticated, as the desktop does for a request it cannot attribute to the paired scanner.
        print(f"refused {self.path}: {reason}")
        self.send_response(401)
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True

    def do_POST(self):
        state = self.state
        try:
            length = int(self.headers.get("Content-Length", ""))
        except ValueError:
            return self.refuse("no Content-Length")
        if not 0 <= length <= MAX_BODY_BYTES:
            return self.refuse("body too large")
        body = self.rfile.read(length)
        device = self.headers.get("X-Inventatory-Device", "")
        counter_text = self.headers.get("X-Inventatory-Counter", "")
        if self.headers.get("X-Inventatory-Protocol") != str(PROTOCOL_VERSION) or device != state.device_id:
            return self.refuse("wrong protocol or device")
        if not counter_text.isdigit():
            return self.refuse("bad counter")
        counter = int(counter_text)
        expected = request_mac(state.token, "POST", self.path, device, counter, body)
        if not hmac.compare_digest(expected, self.headers.get("X-Inventatory-Mac", "")):
            # A short body means the sender stopped early; the agent shows who sent it.
            agent = self.headers.get("User-Agent", "no User-Agent")
            return self.refuse(f"wrong MAC (check the token; body {len(body)} of {length} bytes, from {agent})")
        if counter <= state.last_counter:
            return self.refuse("replayed counter")
        state.last_counter = counter
        try:
            request = json.loads(body)
        except ValueError:
            return self.reply_json(400, {"error": "bad_json"}, counter)
        if not isinstance(request, dict):
            return self.reply_json(400, {"error": "bad_json"}, counter)

        if self.path == "/api/v1/device/sync":
            return self.reply_json(200, {"protocolVersion": PROTOCOL_VERSION, "requestId": request.get("requestId", ""),
                                         "acceptedEventIds": [], "results": [], "firmwareOffer": state.offering},
                                   counter)
        if self.path == "/api/v1/device/firmware/offer":
            print(f"offer asked: running {request.get('version')} counter {request.get('securityCounter')}, "
                  f"keys {request.get('keyIds')}")
            return self.reply_json(200, {"available": True, "package": state.prefix.hex()}, counter)
        if self.path == "/api/v1/device/firmware/chunk":
            offset, size = request.get("offset"), request.get("length")
            if request.get("imageSha256") != state.image_sha256:
                print(f"\nchunk refused: no offer for image {request.get('imageSha256')}")
                return self.reply_json(404, {"error": "no_offer"}, counter)
            if (not isinstance(offset, int) or not isinstance(size, int) or offset < 0 or not 1 <= size <= MAX_CHUNK_BYTES
                    or offset + size > len(state.image)):
                print(f"\nchunk refused: bad range {offset}+{size}")
                return self.reply_json(416, {"error": "bad_range"}, counter)
            done = (offset + size) * 100 // len(state.image)
            print(f"\rchunk {offset}+{size} ({done}%)", end="", flush=True)
            return self.reply(200, state.image[offset:offset + size], counter, "application/octet-stream")
        if self.path == "/api/v1/device/firmware/status":
            print(f"\nstatus: {request.get('state')} {request.get('error', '')} {request.get('reason', '')}".rstrip())
            if request.get("state") in ("installed", "declined"):
                state.offering = False
            return self.reply_json(200, {}, counter)
        return self.reply_json(404, {"error": "unknown_path"}, counter)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--package", required=True)
    parser.add_argument("--device", required=True, help="the scanner's DEVICE_ID")
    token = parser.add_mutually_exclusive_group(required=True)
    token.add_argument("--token", help="the pairing token, 64 hex characters")
    token.add_argument("--token-env", metavar="VAR", help="read the pairing token from this environment variable")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--mdns", action="store_true", help="publish _inventatory._tcp with avahi-publish-service")
    parser.add_argument("--pubkey", help="check the package against this key instead of src/config/ota_keys.h")
    parser.add_argument("--no-verify", action="store_true", help="serve the package without checking it")
    args = parser.parse_args(argv)

    token_value = os.environ.get(args.token_env, "") if args.token_env else args.token
    if not TOKEN_PATTERN.match(token_value or ""):
        print("error: the pairing token must be 64 hex characters", file=sys.stderr)
        return 1
    package = Path(args.package).read_bytes()
    if len(package) < sf.PREFIX_BYTES:
        print("error: the file is shorter than a package header", file=sys.stderr)
        return 1
    if not args.no_verify:
        try:
            keys = [sf.parse_public_key_hex(args.pubkey)] if args.pubkey else sf.read_trusted_keys()
            if not keys:
                raise sf.SignError("no trusted key in src/config/ota_keys.h; pass --pubkey")
            fields = sf.check_package(package, keys)
        except sf.SignError as error:
            print(f"error: {error} (--no-verify serves it anyway)", file=sys.stderr)
            return 1
        print(f"Package OK: version {fields['version']}, {fields['image_size']} bytes")

    Handler.state = OtaState(package, token_value.lower(), args.device)
    server = HTTPServer((args.bind, args.port), Handler)
    publisher = None
    if args.mdns:
        if shutil.which("avahi-publish-service") is None:
            print("error: avahi-publish-service not found; set FALLBACK_HOST and FALLBACK_PORT instead",
                  file=sys.stderr)
            return 1
        publisher = subprocess.Popen(["avahi-publish-service", "Inventatory OTA test", "_inventatory._tcp",
                                      str(args.port)], stdout=subprocess.DEVNULL)
    print(f"Serving on port {args.port}. On the scanner: Quick settings, 4, or wait for the next sync.")
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        if publisher is not None:
            publisher.terminate()
    return 0


if __name__ == "__main__":
    sys.exit(main())
