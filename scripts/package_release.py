"""Package AeroMet GitHub Release assets from built firmware and hardware releases.

Shared by CI (.github/workflows/ci.yml) and manual release runs:
  - Stages direct-flash firmware images (`usb-flash-firmware.bin`, `firmware.bin`)
  - Builds one self-contained hardware archive per variant (`aeromet-hardware.zip`,
    or `aeromet-<variant>-hardware.zip` when multiple variants exist in spec.yaml)
    organized into `boards/`, `enclosure/`, `optics/`, and `docs/`
  - Writes `SHA256SUMS.txt` covering every release file
  - Optionally writes a concise `| File | Use |` Markdown release table (`--notes`)

Usage:
  python3 scripts/package_release.py
  python3 scripts/package_release.py --output dist --notes notes.md
  python3 scripts/package_release.py --firmware-dir station/build/firmware --output dist
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PROJECT = "aeromet"
SPEC_PATH = REPO / "hardware/product/spec.yaml"

FIRMWARE_ASSETS: tuple[tuple[str, str], ...] = (
    (
        "usb-flash-firmware.bin",
        (
            "First flash of a new station over USB: "
            "`esptool.py --chip esp32c6 write_flash 0x0 usb-flash-firmware.bin`"
        ),
    ),
    (
        "firmware.bin",
        "Web UI / OTA update (Setup → Firmware update); includes the embedded crown firmware",
    ),
)

DOC_FILES: tuple[tuple[str, str], ...] = (
    ("docs/building.md", "docs/building.md"),
    ("docs/hardware.md", "docs/hardware.md"),
    ("CALIBRATION.md", "docs/CALIBRATION.md"),
    ("REVIEW_CHECKLIST.md", "docs/REVIEW_CHECKLIST.md"),
)

TOP_LEVEL_BOMS: tuple[str, ...] = (
    "assembly_parts.csv",
    "order_lcsc.csv",
    "order_elsewhere.csv",
)

OPTICS_PREFIXES: tuple[str, ...] = ("ptfe_dome_", "jgs1_window")

EXCLUDED_NAMES: frozenset[str] = frozenset({".DS_Store", "Thumbs.db"})
EXCLUDED_DIRS: frozenset[str] = frozenset({"__pycache__", ".git", ".pytest_cache"})
EXCLUDED_ARCHIVE_SUFFIXES: frozenset[str] = frozenset(
    {".kicad_prl", ".pyc", ".pyo", ".py", ".sh", ".mjs"}
)
BANNED_TOP_LEVEL_SUFFIXES: frozenset[str] = frozenset(
    {".py", ".sh", ".mjs", ".lpf", ".json", ".elf", ".map", ".svg"}
)


def _strip_yaml_comment(line: str) -> str:
    in_quote: str | None = None
    for idx, ch in enumerate(line):
        if ch in ("'", '"'):
            if in_quote is None:
                in_quote = ch
            elif in_quote == ch:
                in_quote = None
        elif ch == "#" and in_quote is None:
            return line[:idx]
    return line


def _parse_yaml_scalar(raw: str) -> str:
    val = raw.strip()
    if len(val) >= 2 and val[0] == val[-1] and val[0] in ("'", '"'):
        return val[1:-1]
    return val


def _parse_variants_stdlib(text: str) -> dict[str, dict]:
    """Minimal stdlib parser for the `variants:` section of hardware/product/spec.yaml."""
    variants: dict[str, dict] = {}
    in_variants = False
    current_variant: str | None = None
    current_list_key: str | None = None

    for raw_line in text.splitlines():
        cleaned = _strip_yaml_comment(raw_line).rstrip()
        if not cleaned.strip():
            continue
        indent = len(cleaned) - len(cleaned.lstrip(" "))
        stripped = cleaned.strip()

        if indent == 0:
            in_variants = stripped == "variants:"
            current_variant = None
            current_list_key = None
            continue
        if not in_variants:
            continue

        if indent == 2 and stripped.endswith(":"):
            current_variant = stripped[:-1].strip()
            variants[current_variant] = {}
            current_list_key = None
            continue

        if current_variant is None:
            continue

        if indent == 4 and ":" in stripped:
            key, _, rest = stripped.partition(":")
            key = key.strip()
            rest = rest.strip()
            if rest.startswith("[") and rest.endswith("]"):
                items = [
                    _parse_yaml_scalar(item)
                    for item in rest[1:-1].split(",")
                    if item.strip()
                ]
                variants[current_variant][key] = items
                current_list_key = None
            elif rest == "":
                variants[current_variant][key] = []
                current_list_key = key
            else:
                variants[current_variant][key] = _parse_yaml_scalar(rest)
                current_list_key = None
        elif indent >= 6 and stripped.startswith("- ") and current_list_key:
            variants[current_variant][current_list_key].append(
                _parse_yaml_scalar(stripped[2:])
            )

    return variants


def load_variants(spec_path: Path = SPEC_PATH) -> dict[str, dict]:
    """Load hardware variants and their board lists from hardware/product/spec.yaml."""
    text = spec_path.read_text(encoding="utf-8")
    try:
        import yaml  # type: ignore[import-not-found]
    except ImportError:
        variants = _parse_variants_stdlib(text)
    else:
        data = yaml.safe_load(text) or {}
        variants = data.get("variants") or {}
    if not variants:
        raise ValueError(f"no variants defined in {spec_path}")
    return variants


def hardware_archive_name(variant: str, all_variants: tuple[str, ...]) -> str:
    if len(all_variants) <= 1:
        return f"{PROJECT}-hardware.zip"
    return f"{PROJECT}-{variant}-hardware.zip"


def _is_excluded(path: Path) -> bool:
    if any(part in EXCLUDED_DIRS for part in path.parts):
        return True
    name = path.name
    if name in EXCLUDED_NAMES or name.startswith("._"):
        return True
    return path.suffix.lower() in EXCLUDED_ARCHIVE_SUFFIXES


def _is_optics_file(path: Path) -> bool:
    return any(path.name.startswith(prefix) for prefix in OPTICS_PREFIXES)


def _archive_readme(
    variant: str,
    boards: tuple[str, ...],
    legacy_boards: tuple[str, ...],
    has_panel: bool,
    has_optics: bool,
) -> str:
    board_list = ", ".join(f"`boards/{b}/`" for b in boards)
    legacy_note = (
        f" (plus optional legacy alternative {', '.join(f'`boards/{b}/`' for b in legacy_boards)})"
        if legacy_boards
        else ""
    )
    panel_note = "`boards/panel/` (all boards on one breakaway panel), plus " if has_panel else ""
    optics_row = (
        "| `optics/` | Custom optical UV-cover RFQ files: flanged PTFE dome (lathe & CNC STEP/STL/SVG drawings) and JGS1 fused-silica window drawing |\n"
        if has_optics
        else ""
    )
    return (
        f"# AeroMet ({variant}) — Hardware Manufacturing Package\n\n"
        "Self-contained manufacturing and assembly package. See `docs/building.md` for which PCB files go to "
        "which fab house (AISLER, PCBWay, Eurocircuits, JLCPCB), 3D-printing settings, weatherproofing and harnesses.\n\n"
        "| Folder / File | Contents |\n"
        "| :--- | :--- |\n"
        f"| `boards/` | {panel_note}per-board folders {board_list}{legacy_note}: ODB++ (`*_odb.zip`), "
        "Gerber X2 + drills + IPC-D-356 (`*_gerbers.zip`), MPN BOM/CPL, JLCPCB BOM/CPL, hand-fit lists, "
        "hand-assembly PDFs, STEP models, routed KiCad PCBs, DRC reports, and layer views |\n"
        "| `enclosure/` | Assembled-position STL/STEP parts, full `assembly.step`, 3D renders (`renders/`), "
        "and the 8 print-ready plates in `enclosure/print/` (order/print these, not the assembled STLs) |\n"
        f"{optics_row}"
        "| `docs/` | Builder reference (`hardware.md`), ordering/printing/sealing guide (`building.md`), "
        "commissioning & calibration (`CALIBRATION.md`), and open review items (`REVIEW_CHECKLIST.md`) |\n"
        "| `assembly_parts.csv` | Complete off-board electronics, utility hardware (fasteners, O-rings, gland, "
        "coatings, UV cover), and printed parts list |\n"
        "| `order_lcsc.csv` | Merged LCSC BOM Tool upload for self-assembling one station |\n"
        "| `order_elsewhere.csv` | Electronics not sold in the LCSC store (Mouser / Digi-Key / TME) |\n"
    )


def _collect_hardware_entries(
    repo: Path,
    hardware_root: Path,
    variant: str,
    variant_cfg: dict,
    archive_stem: str,
) -> tuple[dict[str, Path], str]:
    """Return (arcname -> source_path, generated README text) for one hardware variant."""
    release_dir = hardware_root / variant
    if not release_dir.is_dir():
        raise FileNotFoundError(f"hardware release directory not found: {release_dir}")

    boards = tuple(variant_cfg.get("boards") or ())
    legacy_boards = tuple(variant_cfg.get("legacy_boards") or ())
    if not boards:
        raise ValueError(f"variant {variant!r} defines no boards in spec.yaml")

    entries: dict[str, Path] = {}

    def add_file(src: Path, rel_in_archive: str) -> None:
        if not src.is_file() or _is_excluded(src):
            return
        arcname = f"{archive_stem}/{rel_in_archive}"
        if arcname in entries:
            raise ValueError(f"duplicate archive entry: {arcname}")
        entries[arcname] = src

    def add_tree(src_dir: Path, dst_prefix: str) -> None:
        for path in sorted(src_dir.rglob("*")):
            if path.is_file() and not _is_excluded(path):
                rel = path.relative_to(src_dir).as_posix()
                add_file(path, f"{dst_prefix}/{rel}")

    # 1. Boards belonging strictly to this variant (+ combined breakaway panel if present)
    expected_boards = (*boards, *legacy_boards)
    for board in expected_boards:
        board_dir = release_dir / board
        if not board_dir.is_dir():
            raise FileNotFoundError(f"missing board release folder: {board_dir}")
        add_tree(board_dir, f"boards/{board}")

    panel_dir = release_dir / "panel"
    has_panel = panel_dir.is_dir()
    if has_panel:
        add_tree(panel_dir, "boards/panel")

    # 2. Enclosure parts & custom-part optical RFQ folder (`optics/`)
    enclosure_dir = release_dir / "enclosure"
    if not enclosure_dir.is_dir():
        raise FileNotFoundError(f"missing enclosure release folder: {enclosure_dir}")
    has_optics = False
    for path in sorted(enclosure_dir.rglob("*")):
        if not path.is_file() or _is_excluded(path):
            continue
        rel = path.relative_to(enclosure_dir)
        if _is_optics_file(path):
            has_optics = True
            add_file(path, f"optics/{path.name}")
        else:
            add_file(path, f"enclosure/{rel.as_posix()}")

    # 3. Variant self-assembly BOMs / parts lists
    for bom_name in TOP_LEVEL_BOMS:
        bom_path = release_dir / bom_name
        if not bom_path.is_file():
            raise FileNotFoundError(f"missing variant parts file: {bom_path}")
        add_file(bom_path, bom_name)

    # 4. Builder documentation
    for src_rel, dst_rel in DOC_FILES:
        doc_path = repo / src_rel
        if not doc_path.is_file():
            raise FileNotFoundError(f"missing documentation file: {doc_path}")
        add_file(doc_path, dst_rel)

    readme_text = _archive_readme(variant, boards, legacy_boards, has_panel, has_optics)
    return entries, readme_text


def build_hardware_zip(
    repo: Path,
    hardware_root: Path,
    variant: str,
    variant_cfg: dict,
    out_zip: Path,
) -> Path:
    archive_stem = out_zip.stem
    entries, readme_text = _collect_hardware_entries(
        repo, hardware_root, variant, variant_cfg, archive_stem
    )
    out_zip.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        readme_info = zipfile.ZipInfo(f"{archive_stem}/README.md")
        readme_info.compress_type = zipfile.ZIP_DEFLATED
        readme_info.external_attr = 0o644 << 16
        zf.writestr(readme_info, readme_text.encode("utf-8"))
        for arcname in sorted(entries):
            zf.write(entries[arcname], arcname)
    return out_zip


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def render_release_notes(asset_rows: list[tuple[str, str]]) -> str:
    lines = [
        "| File | Use |",
        "| :--- | :--- |",
    ]
    for filename, description in asset_rows:
        lines.append(f"| `{filename}` | {description} |")
    lines.append("| `SHA256SUMS.txt` | SHA-256 checksums covering every release file above |")
    return "\n".join(lines) + "\n"


def _validate_release_dir(output_dir: Path, expected_assets: list[str]) -> None:
    actual = sorted(p.name for p in output_dir.iterdir())
    expected = sorted([*expected_assets, "SHA256SUMS.txt"])
    if actual != expected:
        raise RuntimeError(f"unexpected release files in {output_dir}: {actual} != {expected}")
    for name in expected_assets:
        path = output_dir / name
        if path.suffix.lower() in BANNED_TOP_LEVEL_SUFFIXES or not path.suffix:
            raise RuntimeError(f"banned top-level release asset: {name}")
        if path.stat().st_size == 0:
            raise RuntimeError(f"empty release asset: {name}")


def _clean_output_dir(output_dir: Path, protected: tuple[Path, ...]) -> None:
    resolved_out = output_dir.resolve()
    for p in protected:
        resolved_p = p.resolve()
        if resolved_out == resolved_p or resolved_out in resolved_p.parents:
            raise ValueError(
                f"refusing to clean output directory {output_dir} (overlaps protected path {p})"
            )
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)


def package_release(
    *,
    repo: Path = REPO,
    spec_path: Path = SPEC_PATH,
    hardware_root: Path | None = None,
    firmware_dir: Path | None = None,
    output_dir: Path | None = None,
    notes_path: Path | None = None,
    variants: tuple[str, ...] | None = None,
    include_firmware: bool = True,
) -> list[Path]:
    """Build all GitHub Release assets, SHA256SUMS.txt, and optional release notes."""
    hardware_root = hardware_root or (repo / "hardware/releases")
    firmware_dir = firmware_dir or (repo / "station/build/firmware")
    output_dir = output_dir or (repo / "dist")

    all_variant_map = load_variants(spec_path)
    all_variants = tuple(all_variant_map.keys())
    selected = variants or all_variants
    unknown = [v for v in selected if v not in all_variant_map]
    if unknown:
        raise ValueError(f"unknown variant(s) {unknown}; available: {list(all_variants)}")

    protected = [repo, hardware_root]
    if include_firmware:
        protected.append(firmware_dir)
    _clean_output_dir(output_dir, tuple(protected))

    asset_rows: list[tuple[str, str]] = []
    staged_files: list[Path] = []

    if include_firmware:
        for filename, description in FIRMWARE_ASSETS:
            src = firmware_dir / filename
            if not src.is_file():
                raise FileNotFoundError(
                    f"missing firmware image {src} (run `python3 station/scripts/release_firmware.py` first)"
                )
            dst = output_dir / filename
            shutil.copy2(src, dst)
            staged_files.append(dst)
            asset_rows.append((filename, description))

    for variant in selected:
        zip_name = hardware_archive_name(variant, all_variants)
        zip_path = build_hardware_zip(
            repo=repo,
            hardware_root=hardware_root,
            variant=variant,
            variant_cfg=all_variant_map[variant],
            out_zip=output_dir / zip_name,
        )
        staged_files.append(zip_path)
        variant_label = f" ({variant})" if len(all_variants) > 1 else ""
        asset_rows.append(
            (
                zip_name,
                (
                    f"Complete manufacturing package{variant_label}: `boards/` (per-board & panel ODB++, "
                    "Gerber X2 + drills + IPC-D-356, MPN & JLCPCB BOM/CPL, routed PCBs, assembly PDFs), "
                    "`enclosure/` (STL/STEP, 8 print plates, renders), `optics/` (PTFE dome & JGS1 window "
                    "RFQ drawings/STEP), `docs/` (build, hardware, calibration, checklist), and self-assembly BOMs"
                ),
            )
        )

    checksum_lines = [f"{sha256_file(p)}  {p.name}" for p in staged_files]
    checksums_path = output_dir / "SHA256SUMS.txt"
    checksums_path.write_text("\n".join(checksum_lines) + "\n", encoding="utf-8")

    _validate_release_dir(output_dir, [p.name for p in staged_files])

    if notes_path is not None:
        resolved_out = output_dir.resolve()
        resolved_notes = notes_path.resolve()
        if resolved_out == resolved_notes.parent or resolved_out in resolved_notes.parents:
            raise ValueError(
                f"--notes path ({notes_path}) must be outside --output ({output_dir}) so it is not uploaded as an asset"
            )
        notes_path.parent.mkdir(parents=True, exist_ok=True)
        notes_path.write_text(render_release_notes(asset_rows), encoding="utf-8")

    return [*staged_files, checksums_path]


def _default_tag(repo: Path) -> str:
    """UTC commit time `yyyy.mm.dd.hhmm` matching station/scripts/gen_version.py."""
    import subprocess
    import time

    ts = int(
        subprocess.run(
            ["git", "show", "-s", "--format=%ct", "HEAD"],
            cwd=repo,
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    )
    return time.strftime("%Y.%m.%d.%H%M", time.gmtime(ts))


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        default=REPO / "dist",
        help="output directory for release assets and SHA256SUMS.txt (default: dist/)",
    )
    ap.add_argument(
        "--notes",
        type=Path,
        default=None,
        help="write the Markdown `| File | Use |` release body to this path (must be outside --output)",
    )
    ap.add_argument(
        "--firmware-dir",
        type=Path,
        default=REPO / "station/build/firmware",
        help="directory containing firmware.bin and usb-flash-firmware.bin",
    )
    ap.add_argument(
        "--hardware-root",
        type=Path,
        default=REPO / "hardware/releases",
        help="root directory containing per-variant hardware release folders",
    )
    ap.add_argument(
        "--variant",
        action="append",
        dest="variants",
        help="variant to package (repeatable; default: all variants in hardware/product/spec.yaml)",
    )
    ap.add_argument(
        "--hardware-only",
        action="store_true",
        help="package only the hardware archive(s) and SHA256SUMS.txt (skip firmware binaries)",
    )
    ap.add_argument(
        "--print-tag",
        action="store_true",
        help="print the UTC commit-time release tag (`yyyy.mm.dd.hhmm`) and exit",
    )
    args = ap.parse_args(argv)

    if args.print_tag:
        print(_default_tag(REPO))
        return 0

    files = package_release(
        repo=REPO,
        spec_path=SPEC_PATH,
        hardware_root=args.hardware_root,
        firmware_dir=args.firmware_dir,
        output_dir=args.output,
        notes_path=args.notes,
        variants=tuple(args.variants) if args.variants else None,
        include_firmware=not args.hardware_only,
    )
    print(f"Packaged {len(files)} release files into {args.output}:")
    for path in files:
        digest = sha256_file(path) if path.name != "SHA256SUMS.txt" else "-" * 12
        print(f"  {path.name:28s} {path.stat().st_size:>10} bytes  {digest[:12]}")
    if args.notes is not None:
        print(f"Release notes table written to {args.notes}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
