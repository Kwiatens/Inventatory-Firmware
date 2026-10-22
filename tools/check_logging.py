#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
# SPDX-License-Identifier: GPL-3.0-only
"""Check the zero sensitive logging rule (AGENTS.md section 4, invariant 1).

Fails when a logging call in src/ or include/ names a barcode, Wi-Fi credential, token, OTA password or
pairing passkey, or when a raw scanner payload log line comes back. Each file is read whole, so a
logging call split over several lines is checked up to its ';'.

Usage, from the repository root:

    python3 tools/check_logging.py

String literals and comments are ignored, so a ';' or a word in a message does not hide or fake a match.
A pass proves that no logging call names one of these values directly. It does not follow a value
through other variables, so a review still checks new log lines.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIRS = (ROOT / "src", ROOT / "include")
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp"}

# deviceLog is the firmware's log: it reaches the USB serial port and, when enabled, the network.
LOG_CALL = (
    r"(?:deviceLog\.(?:print|println|printf|write)|Serial\d?\.(?:print|println|printf|write)|sendDebugMessage"
    r"|\blog_[ewidv]|\bESP_LOG[EWIDV]|(?<![\w.])printf)\("
)
SENSITIVE = (
    r"scannedCode_|scannedEventCode_|scannedManufacturer_|scannedManufacturerPartNumber_|scannedPartName_|"
    r"activeLookupCode_|forwardedCode|wifiPassword|wifiSsid|deviceToken|clientConfig_\.token|config_\.token|"
    r"settings\.deviceToken|OTA_PASSWORD|pairingCode|token|password|passkey"
)
# The payload rule matches log message text, so it reads the file as written.
RAW_RULES = (("a raw scanner payload log line", re.compile(r"GM65 rx|GM65 scan:")),)
RULES = (
    ("a logging call that names a sensitive value", re.compile(LOG_CALL + r"[^;]*\b(?:" + SENSITIVE + r")")),
    (
        "a diagnostic message that names scanned data",
        re.compile(r"sendDebugMessage\([^;]*\b(?:trimmed|manufacturerPartNumber)"),
    ),
)


# String literals and comments are blanked before matching, keeping their newlines, so a ';' or a word in a
# message neither ends a call early nor counts as a value.
LITERAL_OR_COMMENT = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*.*?\*/', re.DOTALL)


def blank(match):
    text = match.group(0)
    if text.startswith(("//", "/*")):
        return re.sub(r"[^\n]", " ", text)
    return text[0] + re.sub(r"[^\n]", " ", text[1:-1]) + text[-1]


def source_files():
    for directory in SOURCE_DIRS:
        for path in sorted(directory.rglob("*")):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES and path.name != "config.h":
                yield path


def main() -> int:
    failures = 0
    checked = 0
    for path in source_files():
        checked += 1
        raw = path.read_text(encoding="utf-8", errors="replace")
        code = LITERAL_OR_COMMENT.sub(blank, raw)
        for text, rules in ((raw, RAW_RULES), (code, RULES)):
            for description, pattern in rules:
                for match in pattern.finditer(text):
                    line = text.count("\n", 0, match.start()) + 1
                    print(f"{path.relative_to(ROOT)}:{line}: {description}", file=sys.stderr)
                    failures += 1
    if failures:
        print(f"{failures} logging rule violation(s).", file=sys.stderr)
        return 1
    print(f"{checked} source files: no logging call names a sensitive value.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
