"""Web UI development server backed by the REAL firmware pipeline.

Serves station/data/{index.html,app.css,app.js} plus the firmware REST/WebSocket API. Every value
comes from tools/sim/station_sim: a host build of src/core + processors + publishers fed with
modelled raw sensor readings, so the UI shows exactly what the station firmware computes.

    python3 station/scripts/mock_webui_server.py   ->   http://127.0.0.1:8765
"""

from __future__ import annotations

import base64
import hashlib
import inspect
import json
import shutil
import socket
import struct
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

_SCRIPT_FILE = Path(inspect.getfile(lambda: None)).resolve()
STATION = _SCRIPT_FILE.parents[1]
DATA_DIR = STATION / "data"
SIM_SRC = STATION / "tools" / "sim" / "station_sim.cpp"
SIM_BIN = STATION / "build" / "station_sim"
JSON_INC = STATION / ".pio" / "libdeps" / "native" / "ArduinoJson" / "src"
SCENARIOS = ("breeze", "storm", "freeze", "heatwave", "plume", "train", "night_dark", "night_moon")


def gas_index_dir() -> Path:
    return STATION / ".pio" / "libdeps" / "native" / "Sensirion Gas Index Algorithm" / "src"


def build_sim() -> None:
    """(Re)build the simulator when any firmware core source is newer than the binary."""
    sources = [p for p in (STATION / "src").rglob("*.cpp") if "firmware" not in p.parts]
    headers = [p for p in (STATION / "src").rglob("*.hpp") if "firmware" not in p.parts]
    newest = max(p.stat().st_mtime for p in [*sources, *headers, SIM_SRC])
    if SIM_BIN.exists() and SIM_BIN.stat().st_mtime >= newest:
        return
    gia = gas_index_dir()
    if not JSON_INC.exists() or not gia.exists():
        raise SystemExit("PlatformIO libraries missing in station/.pio/libdeps/native: run the native tests once")
    cxx = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if cxx is None or cc is None:
        raise SystemExit("a C/C++17 compiler is required to build the simulator")
    SIM_BIN.parent.mkdir(parents=True, exist_ok=True)
    algo_obj = SIM_BIN.parent / "sensirion_gas_index_algorithm.o"
    print("building simulator:", SIM_BIN, flush=True)
    subprocess.run([cc, "-O2", "-w", "-c", str(gia / "algorithm" / "sensirion_gas_index_algorithm.c"), "-o", str(algo_obj)], check=True)
    cmd = [cxx, "-std=c++17", "-O2", f"-I{STATION / 'src'}", f"-I{JSON_INC}", f"-I{gia}",
           *map(str, sources), str(SIM_SRC), str(algo_obj), "-o", str(SIM_BIN)]
    subprocess.run(cmd, check=True)


class Sim:
    """Line-protocol client for the station_sim process (see tools/sim/station_sim.cpp)."""

    def __init__(self) -> None:
        self.proc = subprocess.Popen([str(SIM_BIN)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.lock = threading.Lock()
        self.scenario = "breeze"
        self.location = {"latitude": 54.6872, "longitude": 25.2797, "altitude_m": 112.0}
        self.state = self.call("state")

    def call(self, line: str) -> str:
        with self.lock:
            assert self.proc.stdin is not None and self.proc.stdout is not None
            self.proc.stdin.write(line + "\n")
            self.proc.stdin.flush()
            return self.proc.stdout.readline().strip()

    def tick(self) -> None:
        self.state = self.call("tick")


SIM: Sim


def ticker() -> None:
    while True:
        time.sleep(1.0)
        SIM.tick()


def send_ws_text(sock: socket.socket, text: str) -> None:
    raw = text.encode("utf-8")
    n = len(raw)
    if n < 126:
        hdr = struct.pack("!BB", 0x81, n)
    elif n < 65536:
        hdr = struct.pack("!BBH", 0x81, 126, n)
    else:
        hdr = struct.pack("!BBQ", 0x81, 127, n)
    sock.sendall(hdr + raw)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args: object) -> None:
        return

    def _send_raw_json(self, raw_json: str) -> None:
        raw = raw_json.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _send_json(self, obj: object) -> None:
        self._send_raw_json(json.dumps(obj))

    def _send_file(self, filename: str, mime: str) -> None:
        raw = (DATA_DIR / filename).read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self) -> None:
        url = urlparse(self.path)
        if url.path == "/ws" and self.headers.get("Upgrade", "").lower() == "websocket":
            key = self.headers.get("Sec-WebSocket-Key", "")
            accept = base64.b64encode(
                hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")).digest()
            ).decode("ascii")
            self.send_response(101, "Switching Protocols")
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", accept)
            self.end_headers()
            try:
                while True:
                    send_ws_text(self.request, SIM.state)
                    time.sleep(1.0)
            except OSError:
                return
        if url.path in ("/", "/index.html"):
            self._send_file("index.html", "text/html; charset=utf-8")
        elif url.path == "/app.css":
            self._send_file("app.css", "text/css; charset=utf-8")
        elif url.path == "/app.js":
            self._send_file("app.js", "application/javascript; charset=utf-8")
        elif url.path == "/api/state":
            self._send_raw_json(SIM.state)
        elif url.path == "/api/history/minutes":
            self._send_raw_json(SIM.call("minutes"))
        elif url.path == "/api/history":
            q = parse_qs(url.query)
            tier = q.get("tier", ["hour"])[0]
            metrics = q.get("metrics", [""])[0].replace(" ", "")
            self._send_raw_json(SIM.call(f"history {tier} {metrics}".strip()))
        elif url.path == "/api/config":
            self._send_json(
                {
                    "location_set": True,
                    **SIM.location,
                    "tz": "EET-2EEST,M3.5.0/3,M10.5.0/4",
                    "scenario": SIM.scenario,
                    "scenarios": SCENARIOS,
                    "simulated": True,
                }
            )
        else:
            self.send_error(404)

    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8") if length > 0 else "{}"
        try:
            data = json.loads(body)
        except json.JSONDecodeError:
            data = {}
        if self.path == "/api/config":
            if "latitude" in data and "longitude" in data:
                SIM.location = {
                    "latitude": float(data["latitude"]),
                    "longitude": float(data["longitude"]),
                    "altitude_m": float(data.get("altitude_m", SIM.location["altitude_m"])),
                }
                loc = SIM.location
                SIM.call(f"config {loc['latitude']} {loc['longitude']} {loc['altitude_m']}")
            self._send_json({"ok": True, "reboot_required": False})
        elif self.path == "/api/action":
            act = data.get("action", "")
            if act == "set_scenario" and data.get("scenario") in SCENARIOS:
                SIM.scenario = data["scenario"]
                SIM.call(f"scenario {SIM.scenario}")
                SIM.tick()
            elif act == "reset_rain":
                SIM.call("action reset_rain")
            self._send_json({"ok": True, "action": act, "scenario": SIM.scenario})
        else:
            self.send_error(404)


def main() -> None:
    global SIM
    build_sim()
    SIM = Sim()
    threading.Thread(target=ticker, daemon=True).start()
    srv = ThreadingHTTPServer(("127.0.0.1", 8765), Handler)
    print("AeroMet Web UI (real firmware pipeline via station_sim) on http://127.0.0.1:8765", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
