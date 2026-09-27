#!/usr/bin/env python3
"""Generate a per-device AES-256 key and a local ESP-IDF provisioning fragment."""

import argparse
import secrets
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("device_key.txt"))
    parser.add_argument("--sdkconfig-fragment", type=Path, default=Path("factory_key.conf"))
    args = parser.parse_args()

    key_hex = secrets.token_hex(32)
    args.output.write_text(key_hex + "\n", encoding="ascii")
    args.sdkconfig_fragment.write_text(
        f'CONFIG_OTA_FACTORY_AES_KEY_HEX="{key_hex}"\n', encoding="ascii"
    )
    print(f"Wrote {args.output} and {args.sdkconfig_fragment}")
    print("Keep both files private. Use the fragment only for the initial USB image.")


if __name__ == "__main__":
    main()