#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
# SPDX-License-Identifier: GPL-3.0-only

"""Show the Scan R1 log sent over the air.

The device broadcasts each log line as one UDP datagram on the local network, "<milliseconds> <text>".
Run this on a PC on the same Wi-Fi network as the device:

    python3 tools/device_log_listener.py
    python3 tools/device_log_listener.py --file scan-r1.log

The script only listens. Stop it with Ctrl+C.
"""

import argparse
import socket
import sys
from datetime import datetime

DEFAULT_PORT = 5140


def main() -> int:
    parser = argparse.ArgumentParser(description="Print the Scan R1 log lines sent over the air.")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="UDP port (default: %(default)s)")
    parser.add_argument("--file", help="also append the lines to this file")
    args = parser.parse_args()

    sink = open(args.file, "a", encoding="utf-8") if args.file else None
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", args.port))
    print(f"Listening for the Scan R1 log on UDP port {args.port}. Ctrl+C stops it.", file=sys.stderr)
    try:
        while True:
            data, address = sock.recvfrom(2048)
            text = data.decode("ascii", errors="replace").rstrip("\r\n")
            line = f"{datetime.now():%H:%M:%S} {address[0]} {text}"
            print(line, flush=True)
            if sink is not None:
                sink.write(line + "\n")
                sink.flush()
    except KeyboardInterrupt:
        return 0
    finally:
        sock.close()
        if sink is not None:
            sink.close()


if __name__ == "__main__":
    sys.exit(main())
