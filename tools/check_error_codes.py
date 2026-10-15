#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
# SPDX-License-Identifier: GPL-3.0-only
"""Check the LCD error-code catalogue.

The catalogue is include/InventatoryScanErrorCodes.h. This script fails when:

- a code is not three capital letters, a dash and two digits;
- inside an area the numbers do not run 01, 02, ... with no gap, in header order;
- no firmware file under src/ uses a code constant (the code can never show);
- the host test catalogue in test/test_inventatory_scan_core.cpp omits a code;
- with --site, the public reference lists different codes, or in a different order.

Usage, from the repository root:

    python3 tools/check_error_codes.py
    python3 tools/check_error_codes.py --site path/to/Inventatory-Site/src/content/docs/docs/scanner/error-codes.md

Using a constant in src/ proves that a firmware path names it, not that the path is reachable on
the hardware. Reachability is still a review question.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "include" / "InventatoryScanErrorCodes.h"
HOST_TEST = ROOT / "test" / "test_inventatory_scan_core.cpp"
SOURCE_DIR = ROOT / "src"

CONSTANT = re.compile(r'constexpr char (k\w+Code)\[\] = "([^"]*)";')
CODE = re.compile(r"^([A-Z]{3})-(\d{2})$")
CATALOGUE_ARRAY = re.compile(r"const char\* const codes\[\] = \{(.*?)\};", re.DOTALL)
SITE_TABLE_ROW = re.compile(r"^\| \[([A-Z]{3}-\d{2})\]\(#", re.MULTILINE)
SITE_HEADING = re.compile(r"^### ([A-Z]{3}-\d{2}) ", re.MULTILINE)


def read_catalogue(errors):
    entries = CONSTANT.findall(HEADER.read_text(encoding="utf-8"))
    if not entries:
        errors.append(f"no code constants found in {HEADER.relative_to(ROOT)}")
    return entries


def check_format_and_sequence(entries, errors):
    last_number = {}
    for name, code in entries:
        match = CODE.match(code)
        if not match:
            errors.append(f'{name} = "{code}" is not three capital letters, a dash and two digits')
            continue
        area, number = match.group(1), int(match.group(2))
        expected = last_number.get(area, 0) + 1
        if number != expected:
            errors.append(
                f'{name} = "{code}": the next {area} code must be {area}-{expected:02d}; '
                f"numbers in an area run 01, 02, ... with no gap, in header order"
            )
        last_number[area] = number


def firmware_text():
    paths = [p for p in SOURCE_DIR.rglob("*") if p.suffix in {".cpp", ".h", ".c"}]
    return "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in paths)


def check_used_in_firmware(entries, errors):
    text = firmware_text()
    for name, code in entries:
        if not re.search(rf"\b{name}\b", text):
            errors.append(f"{name} ({code}) is not used by any file under src/: nothing can show it")


def check_host_test_catalogue(entries, errors):
    match = CATALOGUE_ARRAY.search(HOST_TEST.read_text(encoding="utf-8"))
    if not match:
        errors.append(f"no codes array found in {HOST_TEST.relative_to(ROOT)}")
        return
    listed = set(re.findall(r"\bk\w+Code\b", match.group(1)))
    for name, code in entries:
        if name not in listed:
            errors.append(f"{name} ({code}) is missing from the catalogue in {HOST_TEST.relative_to(ROOT)}")


def check_public_reference(entries, site_path, errors):
    text = site_path.read_text(encoding="utf-8")
    codes = [code for _, code in entries]
    table = SITE_TABLE_ROW.findall(text)
    headings = SITE_HEADING.findall(text)
    if table != codes:
        errors.append(f"{site_path.name} summary table lists {table}; firmware has {codes}")
    if headings != codes:
        errors.append(f"{site_path.name} code headings are {headings}; firmware has {codes}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--site", type=Path, help="path to the public error-codes.md to compare against")
    args = parser.parse_args()

    errors = []
    entries = read_catalogue(errors)
    if entries:
        check_format_and_sequence(entries, errors)
        check_used_in_firmware(entries, errors)
        check_host_test_catalogue(entries, errors)
        if args.site:
            check_public_reference(entries, args.site, errors)

    if errors:
        for message in errors:
            print(f"error: {message}", file=sys.stderr)
        return 1

    checked = "firmware and host test" if not args.site else "firmware, host test and public reference"
    print(f"{len(entries)} error codes: numbers run without gaps, all used, all listed; checked {checked}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
