"""Pack station/data/{index.html,app.css,app.js} into station/build/littlefs.bin
and station/src/firmware/WebAssets.hpp."""

from __future__ import annotations

import inspect
import struct
from pathlib import Path

_SCRIPT_FILE = Path(inspect.getfile(lambda: None)).resolve()
ROOT = _SCRIPT_FILE.parents[1]
DATA_DIR = ROOT / "data"
BUILD_DIR = ROOT / "build"
HEADER_OUT = ROOT / "src" / "firmware" / "WebAssets.hpp"

FILES = [
    ("/index.html", "text/html; charset=utf-8", DATA_DIR / "index.html"),
    ("/app.css", "text/css; charset=utf-8", DATA_DIR / "app.css"),
    ("/app.js", "application/javascript; charset=utf-8", DATA_DIR / "app.js"),
]

MAGIC = b"AMWFS1\x00\x00"


def build_littlefs_bin() -> bytes:
    header_size = 12 + len(FILES) * 80
    entries = bytearray()
    payloads = bytearray()
    cur_offset = header_size

    for route, mime, path in FILES:
        raw = path.read_bytes()
        route_b = route.encode("ascii")[:31].ljust(32, b"\x00")
        mime_b = mime.encode("ascii")[:39].ljust(40, b"\x00")
        entries += route_b + mime_b + struct.pack("<II", cur_offset, len(raw))
        payloads += raw
        cur_offset += len(raw)

    return MAGIC + struct.pack("<I", len(FILES)) + bytes(entries) + bytes(payloads)


def build_cpp_header() -> str:
    lines = [
        "#pragma once",
        "// clang-format off",
        "",
        "#include <cstddef>",
        "",
        "namespace station::firmware::web_assets {",
        "",
    ]
    for route, _, path in FILES:
        ident = route.strip("/").replace(".", "_")
        content = path.read_text(encoding="utf-8")
        lines.append(f'inline constexpr const char {ident}_data[] = R"WEBUI({content})WEBUI";')
        lines.append(f"inline constexpr std::size_t {ident}_size = sizeof({ident}_data) - 1;")
        lines.append("")
    lines.append("}")
    lines.append("// clang-format on")
    lines.append("")
    return "\n".join(lines)


def main() -> None:
    if not all(p.exists() for _, _, p in FILES):
        return
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    HEADER_OUT.parent.mkdir(parents=True, exist_ok=True)
    bin_path = BUILD_DIR / "littlefs.bin"
    img = build_littlefs_bin()
    if not bin_path.exists() or bin_path.read_bytes() != img:
        bin_path.write_bytes(img)
    hdr = build_cpp_header()
    if not HEADER_OUT.exists() or HEADER_OUT.read_text(encoding="utf-8") != hdr:
        HEADER_OUT.write_text(hdr, encoding="utf-8")


main()
