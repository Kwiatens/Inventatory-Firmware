#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for sign_firmware.py and ota_serve.py. Run from the repository root:

    python3 -m unittest discover -s tools -p 'test_*.py'
"""

import hashlib
import http.client
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import HTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ota_serve  # noqa: E402
import sign_firmware as sf  # noqa: E402

TOKEN = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
DEVICE = "Inventatory-SCAN-R1"


def make_key(folder, name):
    path = Path(folder) / f"{name}.pem"
    subprocess.run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", str(path)],
                   check=True, capture_output=True)
    return path, sf.public_key_from_private(path)


class SignFirmwareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory()
        cls.key, cls.public = make_key(cls.folder.name, "a")
        cls.other_key, cls.other_public = make_key(cls.folder.name, "b")
        cls.image = b"\xe9" + bytes(range(256)) * 40

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def package(self, **overrides):
        values = dict(product="Inventatory-Scan-R1", hardware="waveshare-esp32s3-nano", version="0.2.0",
                      security_counter=1)
        values.update(overrides)
        package, _ = sf.build_package(self.key, self.image, **values)
        return bytearray(package)

    def assertRefused(self, package, reason, keys=None):
        with self.assertRaises(sf.SignError) as caught:
            sf.check_package(bytes(package), keys or [self.public])
        self.assertTrue(str(caught.exception).startswith(reason), str(caught.exception))

    def test_signed_package_checks_out(self):
        fields = sf.check_package(bytes(self.package()), [self.other_public, self.public])
        self.assertEqual(fields["version"], "0.2.0")
        self.assertEqual(fields["image_size"], len(self.image))
        self.assertEqual(fields["image_sha256"], hashlib.sha256(self.image).hexdigest())

    def test_layout_matches_the_format(self):
        package = self.package(security_counter=7)
        self.assertEqual(package[:4], b"IVOT")
        self.assertEqual(struct.unpack("<HH", package[4:8]), (1, 256))
        self.assertEqual(package[8:16], sf.key_id(self.public))
        self.assertEqual(struct.unpack("<II", package[112:120]), (7, len(self.image)))
        self.assertEqual(package[sf.PREFIX_BYTES:], self.image)
        self.assertFalse(any(package[152:256]))
        # Low-S form.
        self.assertLessEqual(int.from_bytes(package[288:320], "big"), sf.P256_ORDER // 2)

    def test_each_refusal(self):
        self.assertRefused(self.package()[:300], "bad_header")
        bad_magic = self.package()
        bad_magic[0] ^= 1
        self.assertRefused(bad_magic, "bad_header")
        self.assertRefused(self.package(), "bad_signature", keys=[self.other_public])
        changed_header = self.package()
        changed_header[90] ^= 1
        self.assertRefused(changed_header, "bad_signature")
        changed_signature = self.package()
        changed_signature[260] ^= 1
        self.assertRefused(changed_signature, "bad_signature")
        self.assertRefused(self.package()[:-1], "download_failed")
        changed_image = self.package()
        changed_image[-1] ^= 1
        self.assertRefused(changed_image, "hash_mismatch")

    def test_refuses_images_that_are_not_app_images(self):
        with self.assertRaises(sf.SignError):
            sf.build_package(self.key, b"\x00" + self.image[1:], "Inventatory-Scan-R1", "waveshare-esp32s3-nano",
                             "0.2.0", 1)
        with self.assertRaises(sf.SignError):
            sf.build_package(self.key, b"\xe9" * (sf.PARTITION_BYTES + 1), "Inventatory-Scan-R1",
                             "waveshare-esp32s3-nano", "0.2.0", 1)
        with self.assertRaises(sf.SignError):
            sf.build_package(self.key, self.image, "Inventatory-Scan-R1", "waveshare-esp32s3-nano", "x" * 32, 1)

    def test_der_round_trip(self):
        raw = bytes(range(1, 33)) + bytes(range(2, 34))
        self.assertEqual(sf.der_signature_to_raw(sf.raw_signature_to_der(raw)), raw)

    def test_reads_the_identity_from_the_image(self):
        identity = b"IVFWID1;version=0.2.0;product=Inventatory-Scan-R1;hardware=waveshare-esp32s3-nano;counter=3;\0"
        values = sf.read_image_identity(self.image + identity + b"\x01\x02")
        self.assertEqual(values, {"version": "0.2.0", "product": "Inventatory-Scan-R1",
                                  "hardware": "waveshare-esp32s3-nano", "counter": 3})
        with self.assertRaises(sf.SignError):
            sf.read_image_identity(self.image)
        with self.assertRaises(sf.SignError):
            sf.read_image_identity(self.image + identity + identity)
        with self.assertRaises(sf.SignError):
            sf.read_image_identity(self.image + b"IVFWID1;version=0.2.0;counter=x;\0")
        self.assertIsInstance(sf.read_trusted_keys(), list)


class OtaServeTests(unittest.TestCase):
    # Computed by deviceRequestMac and deviceResponseMac in include/InventatoryScanSecurity.h.
    def test_macs_match_the_firmware(self):
        self.assertEqual(ota_serve.request_mac(TOKEN, "POST", "/api/v1/device/firmware/chunk", DEVICE, 42,
                                               b'{"offset":0}'),
                         "ab390b42ad5e13e82b773225a6918d615a73bb6be6fded3ff3799cc4c68f52c8")
        self.assertEqual(ota_serve.response_mac(TOKEN, 42, 200, b"abc"),
                         "57e008fac417a21c157d6bc51dd2caacf366d89a9f232426a490fcfa3fe04601")

    @classmethod
    def setUpClass(cls):
        cls.image = bytes((index * 31 + 7) & 0xFF for index in range(20000))
        header = bytearray(range(256))
        header[120:152] = hashlib.sha256(cls.image).digest()
        cls.prefix = bytes(header) + bytes(64)
        ota_serve.Handler.state = ota_serve.OtaState(cls.prefix + cls.image, TOKEN, DEVICE)
        cls.server = HTTPServer(("127.0.0.1", 0), ota_serve.Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.counter = 100

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def post(self, path, value, mac=None, counter=None):
        type(self).counter += 1
        counter = counter or self.counter
        body = json.dumps(value).encode()
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)
        connection.request("POST", path, body, {
            "X-Inventatory-Protocol": "1", "X-Inventatory-Device": DEVICE, "X-Inventatory-Counter": str(counter),
            "X-Inventatory-Mac": mac or ota_serve.request_mac(TOKEN, "POST", path, DEVICE, counter, body),
            "Content-Type": "application/json"})
        response = connection.getresponse()
        data = response.read()
        connection.close()
        if response.status != 401:
            self.assertEqual(response.getheader("X-Inventatory-Counter"), str(counter))
            self.assertEqual(response.getheader("X-Inventatory-Mac"),
                             ota_serve.response_mac(TOKEN, counter, response.status, data))
        return response.status, data

    def test_offer_chunks_and_status(self):
        status, body = self.post("/api/v1/device/firmware/offer", {"version": "0.1.0"})
        self.assertEqual(status, 200)
        self.assertEqual(bytes.fromhex(json.loads(body)["package"]), self.prefix)
        sha = hashlib.sha256(self.image).hexdigest()
        received = b""
        while len(received) < len(self.image):
            length = min(8192, len(self.image) - len(received))
            status, body = self.post("/api/v1/device/firmware/chunk",
                                     {"imageSha256": sha, "offset": len(received), "length": length})
            self.assertEqual(status, 200)
            self.assertEqual(len(body), length)
            received += body
        self.assertEqual(received, self.image)
        self.assertEqual(self.post("/api/v1/device/firmware/chunk",
                                   {"imageSha256": sha, "offset": 19999, "length": 2})[0], 416)
        self.assertEqual(self.post("/api/v1/device/firmware/chunk",
                                   {"imageSha256": "00" * 32, "offset": 0, "length": 1})[0], 404)
        status, body = self.post("/api/v1/device/sync", {"requestId": "r1"})
        self.assertEqual(json.loads(body)["firmwareOffer"], True)
        self.assertEqual(self.post("/api/v1/device/firmware/status", {"imageSha256": sha, "state": "installed"})[0],
                         200)
        status, body = self.post("/api/v1/device/sync", {"requestId": "r2"})
        self.assertEqual(json.loads(body)["firmwareOffer"], False)
        ota_serve.Handler.state.offering = True

    def test_a_changed_image_is_still_offered_under_the_signed_hash(self):
        changed = bytearray(self.image)
        changed[-1] ^= 1
        state = ota_serve.OtaState(self.prefix + bytes(changed), TOKEN, DEVICE)
        self.assertEqual(state.image_sha256, hashlib.sha256(self.image).hexdigest())

    def test_refuses_a_wrong_mac_or_replayed_counter(self):
        self.assertEqual(self.post("/api/v1/device/firmware/offer", {}, mac="0" * 64)[0], 401)
        self.assertEqual(self.post("/api/v1/device/firmware/offer", {}, counter=1)[0], 401)

    def test_a_short_body_is_refused(self):
        body = json.dumps({"requestId": "short"}).encode()
        type(self).counter += 1
        counter = self.counter
        mac = ota_serve.request_mac(TOKEN, "POST", "/api/v1/device/sync", DEVICE, counter, body)
        request = (f"POST /api/v1/device/sync HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Inventatory-Protocol: 1\r\n"
                   f"X-Inventatory-Device: {DEVICE}\r\nX-Inventatory-Counter: {counter}\r\n"
                   f"X-Inventatory-Mac: {mac}\r\nContent-Length: {len(body)}\r\nConnection: close\r\n\r\n").encode()
        with socket.create_connection(("127.0.0.1", self.server.server_port), timeout=5) as sock:
            sock.sendall(request + body[:5])
            sock.shutdown(socket.SHUT_WR)
            reply = sock.recv(200)
        self.assertTrue(reply.startswith(b"HTTP/1.1 401"), reply)


if __name__ == "__main__":
    unittest.main()
