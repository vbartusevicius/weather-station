"""PlatformIO pre-build script that injects STATION_FW_VERSION from FW_VERSION."""

from __future__ import annotations

import os

Import("env")  # type: ignore[name-defined]

fw_ver = os.environ.get("FW_VERSION", "").strip()
if fw_ver:
    env.Append(CPPDEFINES=[("STATION_FW_VERSION", env.StringifyMacro(fw_ver))])  # type: ignore[name-defined]
