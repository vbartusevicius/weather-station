"""Tests for the shared GitHub Release packager (scripts/package_release.py)."""

from __future__ import annotations

import hashlib
import sys
import zipfile
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "scripts"))
import package_release as pr


def test_stdlib_variant_parser_matches_yaml():
    text = (REPO / "hardware/product/spec.yaml").read_text(encoding="utf-8")
    parsed_stdlib = pr._parse_variants_stdlib(text)
    yaml = pytest.importorskip("yaml")
    parsed_yaml = yaml.safe_load(text)["variants"]
    assert parsed_stdlib == parsed_yaml


def test_package_release_single_variant_layout_and_checksums(tmp_path):
    fw_dir = tmp_path / "fw"
    fw_dir.mkdir()
    (fw_dir / "usb-flash-firmware.bin").write_bytes(b"USB-IMAGE-BYTES")
    (fw_dir / "firmware.bin").write_bytes(b"OTA-IMAGE-BYTES")
    # Stray intermediate/developer files in firmware dir must never be staged
    (fw_dir / "firmware.elf").write_bytes(b"ELF")
    (fw_dir / "firmware.map").write_text("MAP", encoding="utf-8")
    (fw_dir / "helper.py").write_text("print(1)\n", encoding="utf-8")

    out_dir = tmp_path / "dist"
    notes = tmp_path / "notes.md"

    packaged = pr.package_release(
        repo=REPO,
        spec_path=REPO / "hardware/product/spec.yaml",
        hardware_root=REPO / "hardware/releases",
        firmware_dir=fw_dir,
        output_dir=out_dir,
        notes_path=notes,
    )

    assert sorted(p.name for p in out_dir.iterdir()) == [
        "SHA256SUMS.txt",
        "aeromet-hardware.zip",
        "firmware.bin",
        "usb-flash-firmware.bin",
    ]
    assert [p.name for p in packaged] == [
        "usb-flash-firmware.bin",
        "firmware.bin",
        "aeromet-hardware.zip",
        "SHA256SUMS.txt",
    ]

    # Verify SHA256SUMS.txt covers every release asset with exact hashes
    sum_lines = (out_dir / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines()
    assert len(sum_lines) == 3
    for line in sum_lines:
        digest, name = line.split("  ", 1)
        expected = hashlib.sha256((out_dir / name).read_bytes()).hexdigest()
        assert digest == expected

    # Verify release notes table mentions every file
    notes_text = notes.read_text(encoding="utf-8")
    assert "| File | Use |" in notes_text
    for asset in ("usb-flash-firmware.bin", "firmware.bin", "aeromet-hardware.zip", "SHA256SUMS.txt"):
        assert f"| `{asset}` |" in notes_text

    # Verify internal structure of aeromet-hardware.zip
    with zipfile.ZipFile(out_dir / "aeromet-hardware.zip") as zf:
        names = set(zf.namelist())

    assert all(n.startswith("aeromet-hardware/") for n in names)
    top_entries = {n.split("/")[1] for n in names}
    assert top_entries == {
        "README.md",
        "assembly_parts.csv",
        "order_lcsc.csv",
        "order_elsewhere.csv",
        "boards",
        "enclosure",
        "optics",
        "docs",
    }

    # Every variant board + panel is grouped under boards/
    for board in ("main", "crown", "reflector", "dome", "dome_legacy", "probe", "power", "panel"):
        assert f"aeromet-hardware/boards/{board}/{board}_gerbers.zip" in names
        assert f"aeromet-hardware/boards/{board}/{board}_odb.zip" in names
        assert f"aeromet-hardware/boards/{board}/{board}.kicad_pcb" in names

    # Enclosure print plates and assembly STEP live under enclosure/
    assert "aeromet-hardware/enclosure/assembly.step" in names
    assert "aeromet-hardware/enclosure/print/base.stl" in names

    # Custom optical cover RFQ files live under optics/ and are not duplicated in enclosure/
    for optics_file in (
        "ptfe_dome_lathe.step",
        "ptfe_dome_lathe.stl",
        "ptfe_dome_lathe.svg",
        "ptfe_dome_cnc.step",
        "ptfe_dome_cnc.stl",
        "ptfe_dome_cnc.svg",
        "jgs1_window.svg",
    ):
        assert f"aeromet-hardware/optics/{optics_file}" in names
    assert not any(
        n.startswith("aeromet-hardware/enclosure/")
        and ("ptfe_dome_" in n or "jgs1_window" in n)
        for n in names
    )

    # All builder docs live under docs/
    for doc in ("building.md", "hardware.md", "CALIBRATION.md", "REVIEW_CHECKLIST.md"):
        assert f"aeromet-hardware/docs/{doc}" in names

    # No OS clutter, KiCad local GUI state, or helper scripts in archive
    assert not any(n.endswith(".DS_Store") for n in names)
    assert not any(n.endswith(".kicad_prl") for n in names)
    assert not any(Path(n).suffix in {".py", ".sh", ".mjs"} for n in names)


def test_multi_variant_naming_and_board_scoping(tmp_path):
    spec = tmp_path / "spec.yaml"
    spec.write_text(
        "variants:\n"
        "  core:\n"
        "    boards: [main, probe]\n"
        "  pro:\n"
        "    boards: [main, crown]\n",
        encoding="utf-8",
    )

    hw_root = tmp_path / "releases"
    for variant, boards in (("core", ("main", "probe", "stray_board")), ("pro", ("main", "crown", "probe"))):
        vdir = hw_root / variant
        for b in boards:
            bdir = vdir / b
            bdir.mkdir(parents=True)
            (bdir / f"{b}.kicad_pcb").write_text("pcb", encoding="utf-8")
            (bdir / f"{b}.kicad_prl").write_text("gui", encoding="utf-8")
        enc = vdir / "enclosure"
        (enc / "drawings").mkdir(parents=True)
        (enc / "base.stl").write_text("stl", encoding="utf-8")
        (enc / "ptfe_dome_lathe.step").write_text("step", encoding="utf-8")
        (enc / "drawings/ptfe_dome_lathe.svg").write_text("<svg/>", encoding="utf-8")
        for bom in pr.TOP_LEVEL_BOMS:
            (vdir / bom).write_text("col\n", encoding="utf-8")

    fw_dir = tmp_path / "fw"
    fw_dir.mkdir()
    (fw_dir / "usb-flash-firmware.bin").write_bytes(b"USB")
    (fw_dir / "firmware.bin").write_bytes(b"OTA")

    out_dir = tmp_path / "dist"
    notes = tmp_path / "notes.md"

    pr.package_release(
        repo=REPO,
        spec_path=spec,
        hardware_root=hw_root,
        firmware_dir=fw_dir,
        output_dir=out_dir,
        notes_path=notes,
    )

    assert sorted(p.name for p in out_dir.iterdir()) == [
        "SHA256SUMS.txt",
        "aeromet-core-hardware.zip",
        "aeromet-pro-hardware.zip",
        "firmware.bin",
        "usb-flash-firmware.bin",
    ]

    with zipfile.ZipFile(out_dir / "aeromet-core-hardware.zip") as zf:
        core_boards = {n.split("/")[2] for n in zf.namelist() if n.startswith("aeromet-core-hardware/boards/")}
        assert core_boards == {"main", "probe"}
        assert not any(n.endswith(".kicad_prl") for n in zf.namelist())

    with zipfile.ZipFile(out_dir / "aeromet-pro-hardware.zip") as zf:
        pro_boards = {n.split("/")[2] for n in zf.namelist() if n.startswith("aeromet-pro-hardware/boards/")}
        assert pro_boards == {"main", "crown"}

    notes_text = notes.read_text(encoding="utf-8")
    assert "| `aeromet-core-hardware.zip` |" in notes_text
    assert "| `aeromet-pro-hardware.zip` |" in notes_text


def test_notes_inside_output_dir_rejected(tmp_path):
    fw_dir = tmp_path / "fw"
    fw_dir.mkdir()
    (fw_dir / "usb-flash-firmware.bin").write_bytes(b"USB")
    (fw_dir / "firmware.bin").write_bytes(b"OTA")

    out_dir = tmp_path / "dist"
    with pytest.raises(ValueError, match="outside --output"):
        pr.package_release(
            repo=REPO,
            spec_path=REPO / "hardware/product/spec.yaml",
            hardware_root=REPO / "hardware/releases",
            firmware_dir=fw_dir,
            output_dir=out_dir,
            notes_path=out_dir / "notes.md",
        )
