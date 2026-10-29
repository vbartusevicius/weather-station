import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "hardware/circuits"))
from product_spec import load_spec

SPEC = load_spec()
VARIANT = SPEC["active_variant"]
BOARDS = (*SPEC["variants"][VARIANT]["boards"], *SPEC["variants"][VARIANT].get("legacy_boards", ()))


def layout_path(board):
    return REPO / "hardware/layout" / VARIANT / board / f"{board}.kicad_pcb"


def build_path(board, filename):
    return REPO / "hardware/build" / VARIANT / board / filename
