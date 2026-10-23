"""Regenerate the README images in docs/images/.

    pcb_panel.png        KiCad 3D render (kicad-cli, eda container) of the breakaway panel, top view
    webui_now.png        Web UI screenshots (headless Chrome / Chromium) of the simulator: Now tab,
    webui_trends.png     Trends (7 days: temperature, dew point, wind, gust + wind rose),
    webui_calibration.png  Calibration (station health) and
    webui_setup.png      Setup

The enclosure renders come from the CAD pipeline (hardware/releases/v1/enclosure/renders/) and are linked directly.

Usage:
    python3 station/scripts/mock_webui_server.py --product-look &   # simulator, no dev-only scenario picker
    python3 docs/render_readme_images.py [--url http://127.0.0.1:8765] [--skip-pcb] [--skip-web]
"""

from __future__ import annotations

import argparse
import inspect
import json
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

_SCRIPT_FILE = Path(inspect.getfile(lambda: None)).resolve()
REPO = _SCRIPT_FILE.parents[1]
OUT = REPO / "docs/images"
PANEL = "hardware/releases/v1/panel/panel.kicad_pcb"
# (file, deep link, window height): one screenful each, 1440 px wide
VIEWS = [
    ("webui_now.png", "", 1070),
    ("webui_trends.png", "#trends?range=7d&metrics=temp,dew,wind,gust,rose", 930),
    ("webui_calibration.png", "#calibration", 1000),
    ("webui_setup.png", "#settings", 1060),
]
CHROMES = ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "google-chrome", "google-chrome-stable",
           "chromium", "chromium-browser"]


def crop(path: Path, box: tuple[float, float, float, float]) -> None:
    """Trim the render margins (fractions of width / height); skipped without Pillow."""
    try:
        from PIL import Image
    except ImportError:
        return
    im = Image.open(path).convert("RGB")
    w, h = im.size
    im.crop((int(w * box[0]), int(h * box[1]), int(w * box[2]), int(h * box[3]))).save(path, optimize=True)


def render_pcb() -> None:
    tmp = "hardware/build/readme/pcb_panel.png"
    (REPO / tmp).parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["docker", "compose", "--profile", "hardware", "run", "--rm", "-T", "eda",
                    (f"kicad-cli pcb render --side top --width 1800 --height 820 --quality high --zoom 1.45 "
                     f"--background opaque -o {tmp} {PANEL}")], cwd=REPO, check=True, capture_output=True)
    shutil.copy2(REPO / tmp, OUT / "pcb_panel.png")
    crop(OUT / "pcb_panel.png", (0.165, 0.12, 0.835, 0.88))


def render_web(url: str) -> None:
    chrome = next((c for c in CHROMES if shutil.which(c) or Path(c).exists()), None)
    if chrome is None:
        sys.exit("Chrome / Chromium not found")
    # the everyday "breeze" scenario (a fresh reset: no scenario-switch jumps in the data-quality checks)
    req = urllib.request.Request(f"{url}/api/action", data=json.dumps({"action": "set_scenario", "scenario": "breeze"}).encode(),
                                 headers={"Content-Type": "application/json"})
    urllib.request.urlopen(req, timeout=120).read()
    for name, link, height in VIEWS:
        subprocess.run([chrome, "--headless=new", "--disable-gpu", "--hide-scrollbars", f"--window-size=1440,{height}",
                        "--virtual-time-budget=12000", f"--screenshot={OUT / name}", f"{url}/{link}"],
                       check=True, capture_output=True)
        crop(OUT / name, (0, 0, 1, 1))   # re-save optimised


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default="http://127.0.0.1:8765", help="running simulator / mock Web UI server")
    ap.add_argument("--skip-pcb", action="store_true")
    ap.add_argument("--skip-web", action="store_true")
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    if not args.skip_pcb:
        render_pcb()
    if not args.skip_web:
        render_web(args.url.rstrip("/"))
    for f in sorted(OUT.glob("*.png")):
        print(f"{f.relative_to(REPO)}  {f.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
