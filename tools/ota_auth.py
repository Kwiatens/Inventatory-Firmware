# SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
# SPDX-License-Identifier: GPL-3.0-only

from pathlib import Path
import re

Import("env")

OTA_PASSWORD_PLACEHOLDER = "paste-your-ota-password-here"
MIN_OTA_PASSWORD_LENGTH = 12


def read_ota_password():
    config_path = Path(env["PROJECT_DIR"]) / "src" / "config" / "config.h"
    if not config_path.is_file():
        raise RuntimeError(f"Missing private config file: {config_path}")

    text = config_path.read_text(encoding="utf-8", errors="ignore")
    match = re.search(r'^\s*const\s+char\s+OTA_PASSWORD\[\]\s*=\s*"([^"]*)"\s*;\s*$', text, re.MULTILINE)
    if not match:
        raise RuntimeError(
            "OTA_PASSWORD is not set in src/config/config.h, or it is not a plain string. "
            "Add const char OTA_PASSWORD[] = \"...\"; to your private config. "
            "The password cannot contain quotes or backslashes."
        )
    password = match.group(1)
    if password == OTA_PASSWORD_PLACEHOLDER or len(password) < MIN_OTA_PASSWORD_LENGTH:
        raise RuntimeError(
            "OTA_PASSWORD in src/config/config.h must be changed from the example placeholder "
            f"and be at least {MIN_OTA_PASSWORD_LENGTH} characters long."
        )
    return password


def add_ota_auth_flags(*args, **kwargs):
    password = read_ota_password()
    env.Append(UPLOAD_FLAGS=["--auth=" + password])


add_ota_auth_flags()
