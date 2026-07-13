from pathlib import Path
import re

Import("env")


def read_ota_password():
    config_path = Path(env["PROJECT_DIR"]) / "src" / "config" / "config.h"
    if not config_path.is_file():
        raise RuntimeError(f"Missing private config file: {config_path}")

    text = config_path.read_text(encoding="utf-8", errors="ignore")
    match = re.search(r'^\s*const\s+char\s+OTA_PASSWORD\[\]\s*=\s*"([^"]*)"\s*;\s*$', text, re.MULTILINE)
    if not match:
        raise RuntimeError(
            "OTA_PASSWORD is not set in src/config/config.h. "
            "Add const char OTA_PASSWORD[] = \"...\"; to your private config."
        )
    return match.group(1)


def add_ota_auth_flags(*args, **kwargs):
    password = read_ota_password()
    env.Append(UPLOAD_FLAGS=["--auth=" + password])


add_ota_auth_flags()
