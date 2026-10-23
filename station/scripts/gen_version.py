"""PlatformIO pre-build script: STATION_FW_VERSION = $FW_VERSION, else the commit time (no hand-written version).

Version = UTC commit time of HEAD as yyyy.mm.dd.hhmm (e.g. 2026.10.24.0040), "-dirty" with uncommitted changes, "dev"
without git. The GitHub release of that commit carries the same name (.github/workflows/ci.yml), and
station/scripts/release_firmware.py passes the same value as FW_VERSION.
"""

from __future__ import annotations

import os
import subprocess
import time

Import("env")  # type: ignore[name-defined]


def commit_version(cwd: str) -> str:
    git = ["git", "-c", "safe.directory=*"]
    try:
        ts = int(subprocess.run([*git, "show", "-s", "--format=%ct", "HEAD"], cwd=cwd, capture_output=True, text=True,
                                check=True).stdout.strip())
        dirty = subprocess.run([*git, "status", "--porcelain", "--untracked-files=no"], cwd=cwd, capture_output=True,
                               text=True, check=True).stdout.strip()
    except (OSError, ValueError, subprocess.CalledProcessError):
        return "dev"
    return time.strftime("%Y.%m.%d.%H%M", time.gmtime(ts)) + ("-dirty" if dirty else "")


fw_ver = os.environ.get("FW_VERSION", "").strip() or commit_version(env.subst("$PROJECT_DIR"))  # type: ignore[name-defined]
env.Append(CPPDEFINES=[("STATION_FW_VERSION", env.StringifyMacro(fw_ver))])  # type: ignore[name-defined]
print(f"STATION_FW_VERSION = {fw_ver}")
