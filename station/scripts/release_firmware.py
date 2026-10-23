"""Build the station firmware release into station/build/firmware/ (git-ignored; CI publishes it as an artifact).

    firmware.bin            the application only: web update (Setup -> Firmware update) or OTA. Carries the crown
                            firmware, which the station installs on the crown by itself after restarting.
    usb-flash-firmware.bin  one image for address 0x0 over USB (first flash of a new board, or recovery): bootloader
                            (with app rollback), partition table, OTA data and the application. The history area
                            is not included; the firmware initialises it on first start.

Steps (all in the cpp-lint container): crown firmware build -> embed it (pack_crown_image.py) -> pack the Web UI
(pack_webui.py) -> `pio run -e release_c6` -> esptool merge_bin -> copy; sizes and SHA-256 are printed.

Usage:  python3 station/scripts/release_firmware.py [--version 2026.10.24.0040] [--skip-crown]
Flash:  esptool.py --chip esp32c6 write_flash 0x0 usb-flash-firmware.bin   (or ESP Web Tools in Chrome / Edge)
"""

from __future__ import annotations

import argparse
import hashlib
import inspect
import shutil
import subprocess
import sys
import time
from pathlib import Path

_SCRIPT_FILE = Path(inspect.getfile(lambda: None)).resolve()
REPO = _SCRIPT_FILE.parents[2]
BUILD = REPO / "station/.pio/build/release_c6"
OUT = REPO / "station/build/firmware"
# Offsets of the ESP32-C6 boot chain and station/partitions.csv
IMAGES = [("0x0", "bootloader.bin"), ("0x8000", "partitions.bin"), ("0x19000", "ota_data_initial.bin"),
          ("0x20000", "firmware.bin")]


def container(cmd: str, version: str | None) -> None:
    env = ["-e", f"FW_VERSION={version}"] if version else []
    subprocess.run(["docker", "compose", "--profile", "lint", "run", "--rm", "-T", *env, "cpp-lint", cmd],
                   cwd=REPO, check=True)


def default_version() -> str:
    """Same rule as scripts/gen_version.py: UTC commit time yyyy.mm.dd.hhmm (+ "-dirty"), the release name."""
    try:
        ts = int(subprocess.run(["git", "show", "-s", "--format=%ct", "HEAD"], cwd=REPO, capture_output=True,
                                text=True, check=True).stdout.strip())
        dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=REPO, capture_output=True,
                               text=True, check=True).stdout.strip()
    except (OSError, ValueError, subprocess.CalledProcessError):
        return "dev"
    return time.strftime("%Y.%m.%d.%H%M", time.gmtime(ts)) + ("-dirty" if dirty else "")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", help="firmware version string (default: commit time, e.g. 2026.10.24.0040)")
    ap.add_argument("--skip-crown", action="store_true", help="reuse the embedded crown image (no STM32 build)")
    args = ap.parse_args()
    args.version = args.version or default_version()   # passed to the build as FW_VERSION

    if not args.skip_crown:
        container("pio run -d crown -e crown", None)
        subprocess.run([sys.executable, str(REPO / "station/scripts/pack_crown_image.py")], check=True)
    subprocess.run([sys.executable, str(REPO / "station/scripts/pack_webui.py")], check=True)
    container("cd station && pio run -e release_c6", args.version)
    parts = " ".join(f"{off} .pio/build/release_c6/{name}" for off, name in IMAGES)
    container("cd station && pio pkg exec --package tool-esptoolpy -- esptool.py --chip esp32c6 merge_bin "
              "--flash_mode keep --flash_freq keep --flash_size 4MB "
              f"-o .pio/build/release_c6/usb-flash-firmware.bin {parts}", None)

    OUT.mkdir(parents=True, exist_ok=True)
    shutil.copy2(BUILD / "firmware.bin", OUT / "firmware.bin")
    shutil.copy2(BUILD / "usb-flash-firmware.bin", OUT / "usb-flash-firmware.bin")
    print(f"AeroMet station firmware {args.version}")
    for name in ("firmware.bin", "usb-flash-firmware.bin"):
        f = OUT / name
        print(f"  {name:24s} {f.stat().st_size:>9} bytes  sha256 {sha256(f)}")


if __name__ == "__main__":
    main()
